/***************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  OpenCPN Platform specific support utilities
 * Author:   David Register
 *
 ***************************************************************************
 *   Copyright (C) 2015 by David S. Register                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.         *
 **************************************************************************/

#include "wx/wxprec.h"

#include <cstdlib>
#include <string>
#include <vector>

#ifdef __MINGW32__
#undef IPV6STRICT  // mingw FTBS fix:  missing struct ip_mreq
#include <windows.h>
#endif

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif  // precompiled headers

#ifdef __WXMSW__
#include <windows.h>
#include <winioctl.h>
#include <initguid.h>
#include "setupapi.h"  // presently stored in opencpn/src
#endif

#include <wx/app.h>
#include <wx/apptrait.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include "wx/stdpaths.h"
#include <wx/textfile.h>
#include <wx/tokenzr.h>

#include "config.h"

#include "BasePlatform.h"
#include "logger.h"
#include "ocpn_utils.h"
#include "ocpn_plugin.h"

#ifdef __ANDROID__
#include <QDebug>
#include "androidUTIL.h"
#endif

#ifdef __WXOSX__
#include "macutils.h"
#endif

#ifdef __WXMSW__
static const char PATH_SEP = ';';
#else
static const char PATH_SEP = ':';
#endif

static const char* const DEFAULT_XDG_DATA_DIRS =
    "~/.local/share:/usr/local/share:/usr/share";

void appendOSDirSlash(wxString* pString);

extern wxString g_winPluginDir;

extern bool g_bportable;

extern wxLog* g_logger;

extern BasePlatform* g_BasePlatform;

#ifdef __ANDROID__
PlatSpec android_plat_spc;
#endif

static bool checkIfFlatpacked() {
  wxString id;
  if (!wxGetEnv("FLATPAK_ID", &id)) {
    return false;
  }
  return id == "org.opencpn.OpenCPN";
}

static wxString ExpandPaths(wxString paths, BasePlatform* platform);

static wxString GetLinuxDataPath() {
  wxString dirs;
  if (wxGetEnv("XDG_DATA_DIRS", &dirs)) {
    dirs = wxString("~/.local/share:") + dirs;
  } else {
    dirs = DEFAULT_XDG_DATA_DIRS;
  }
  wxString s;
  wxStringTokenizer tokens(dirs, ':');
  while (tokens.HasMoreTokens()) {
    wxString dir = tokens.GetNextToken();
    if (dir.EndsWith("/")) {
      dir = dir.SubString(0, dir.length() - 1);
    }
    if (!dir.EndsWith("/opencpn/plugins")) {
      dir += "/opencpn/plugins";
    }
    s += dir + (tokens.HasMoreTokens() ? ";" : "");
  }
  return s;
}

static wxString ExpandPaths(wxString paths, BasePlatform* platform) {
  wxStringTokenizer tokens(paths, ';');
  wxString s = "";
  while (tokens.HasMoreTokens()) {
    wxFileName filename(tokens.GetNextToken());
    filename.Normalize();
    s += platform->NormalizePath(filename.GetFullPath());
    if (tokens.HasMoreTokens()) {
      s += ';';
    }
  }
  return s;
}

//  OCPN Platform implementation
BasePlatform::BasePlatform() {
  m_isFlatpacked = checkIfFlatpacked();
  m_osDetail = new OCPN_OSDetail;
  DetectOSDetail(m_osDetail);
  InitializeLogFile();
}

//--------------------------------------------------------------------------
//      Per-Platform file/directory support
//--------------------------------------------------------------------------

wxStandardPaths& BasePlatform::GetStdPaths() {
#ifndef __ANDROID__
  return wxStandardPaths::Get();
#else
  return *dynamic_cast<wxStandardPaths*>(
      &(wxTheApp->GetTraits())->GetStandardPaths());
#endif
}

wxString BasePlatform::NormalizePath(const wxString& full_path) {
  if (!g_bportable) {
    return full_path;
  } else {
    wxString path(full_path);
    wxFileName f(path);
    // If not on another voulme etc. make the portable relative path
    if (f.MakeRelativeTo(GetPrivateDataDir())) {
      path = f.GetFullPath();
    }
    return path;
  }
}

wxString& BasePlatform::GetHomeDir() {
  if (m_homeDir.IsEmpty()) {
    //      Establish a "home" location
    wxStandardPaths& std_path = GetStdPaths();
    // TODO  Why is the following preferred?  Will not compile with gcc...
    //    wxStandardPaths& std_path = wxApp::GetTraits()->GetStandardPaths();

#ifdef __unix__
    std_path.SetInstallPrefix(wxString(PREFIX, wxConvUTF8));
#endif

#ifdef __WXMSW__
    m_homeDir =
        std_path
            .GetConfigDir();  // on w98, produces "/windows/Application Data"
#else
    m_homeDir = std_path.GetUserConfigDir();
#endif

#ifdef __ANDROID__
    m_homeDir = androidGetHomeDir();
#endif

    if (g_bportable) {
      wxFileName path(GetExePath());
      m_homeDir = path.GetPath();
    }

#ifdef __WXOSX__
    appendOSDirSlash(&m_homeDir);
    m_homeDir.Append(_T("opencpn"));
#endif

    appendOSDirSlash(&m_homeDir);
  }

  return m_homeDir;
}

wxString& BasePlatform::GetExePath() {
  if (m_exePath.IsEmpty()) {
    wxStandardPaths& std_path = GetStdPaths();
    m_exePath = std_path.GetExecutablePath();
  }

  return m_exePath;
}

wxString* BasePlatform::GetSharedDataDirPtr() { return &m_SData_Dir; }

wxString* BasePlatform::GetPrivateDataDirPtr() { return &m_PrivateDataDir; }

wxString& BasePlatform::GetSharedDataDir() {
  if (m_SData_Dir.IsEmpty()) {
    //      Establish a "shared data" location
    /*  From the wxWidgets documentation...
     *
     *     wxStandardPaths::GetDataDir
     *     wxString GetDataDir() const
     *     Return the location of the applications global, i.e. not
     * user-specific, data files. Unix: prefix/share/appname Windows: the
     * directory where the executable file is located Mac:
     * appname.app/Contents/SharedSupport bundle subdirectory
     */
    wxStandardPaths& std_path = GetStdPaths();
    m_SData_Dir = std_path.GetDataDir();
    appendOSDirSlash(&m_SData_Dir);

#ifdef __ANDROID__
    m_SData_Dir = androidGetSharedDir();
#endif

    if (g_bportable) m_SData_Dir = GetHomeDir();
  }

  return m_SData_Dir;
}

wxString GetPluginDataDir(const char* plugin_name) {
  static const wxString sep = wxFileName::GetPathSeparator();

  wxString datadirs = g_BasePlatform->GetPluginDataPath();
  wxLogMessage(_T("PlugInManager: Using data dirs from: ") + datadirs);
  wxStringTokenizer dirs(datadirs, ";");
  while (dirs.HasMoreTokens()) {
    wxString dir = dirs.GetNextToken();
    wxFileName tryDirName(dir);
    wxDir tryDir;
    if (!tryDir.Open(tryDirName.GetFullPath())) continue;
    wxString next;
    bool more = tryDir.GetFirst(&next);
    while (more) {
      if (next == plugin_name) {
        next = next.Prepend(tryDirName.GetFullPath() + sep);
        wxLogMessage(_T("PlugInManager: using data dir: %s"), next);
        return next;
      }
      more = tryDir.GetNext(&next);
    }
    tryDir.Close();
  }
  wxLogMessage(_T("Warnińg: no data directory found, using \"\""));
  return "";
}

wxString& BasePlatform::GetPrivateDataDir() {
  if (m_PrivateDataDir.IsEmpty()) {
    //      Establish the prefix of the location of user specific data files
    wxStandardPaths& std_path = GetStdPaths();

#ifdef __WXMSW__
    m_PrivateDataDir =
        GetHomeDir();  // should be {Documents and Settings}\......
#elif defined FLATPAK
    std::string config_home;
    if (getenv("XDG_CONFIG_HOME")) {
      config_home = getenv("XDG_CONFIG_HOME");
    } else {
      config_home = getenv("HOME");
      config_home += "/.var/app/org.opencpn.OpenCPN/config";
    }
    m_PrivateDataDir = config_home + "/opencpn";

#elif defined __WXOSX__
    m_PrivateDataDir =
        std_path.GetUserConfigDir();  // should be ~/Library/Preferences
    appendOSDirSlash(&m_PrivateDataDir);
    m_PrivateDataDir.Append(_T("opencpn"));
#else
    m_PrivateDataDir = std_path.GetUserDataDir();    // should be ~/.opencpn
#endif

    if (g_bportable) {
      m_PrivateDataDir = GetHomeDir();
      if (m_PrivateDataDir.Last() == wxFileName::GetPathSeparator())
        m_PrivateDataDir.RemoveLast();
    }

#ifdef __ANDROID__
    m_PrivateDataDir = androidGetPrivateDir();
#endif
  }
  return m_PrivateDataDir;
}

wxString BasePlatform::GetWinPluginBaseDir() {
  if (g_winPluginDir != "") {
    wxLogMessage("winPluginDir: Using value from ini file.");
    wxFileName fn(g_winPluginDir);
    if (!fn.DirExists()) {
      wxLogWarning("Plugin dir %s does not exist",
                   fn.GetFullPath().mb_str().data());
    }
    fn.Normalize();
    return fn.GetFullPath();
  }
  wxString winPluginDir;
  // Standard case: c:\Users\%USERPROFILE%\AppData\Local
  bool ok = wxGetEnv(_T("LOCALAPPDATA"), &winPluginDir);
  if (!ok) {
    wxLogMessage("winPluginDir: Cannot lookup LOCALAPPDATA");
    // Without %LOCALAPPDATA%: Use default location if it exists.
    std::string path(wxGetHomeDir().ToStdString());
    path += "\\AppData\\Local";
    if (ocpn::exists(path)) {
      winPluginDir = wxString(path.c_str());
      wxLogMessage("winPluginDir: using %s", winPluginDir.mb_str().data());
      ok = true;
    }
  }
  if (!ok) {
    // Usually: c:\Users\%USERPROFILE%\AppData\Roaming
    ok = wxGetEnv(_T("APPDATA"), &winPluginDir);
  }
  if (!ok) {
    // Without %APPDATA%: Use default location if it exists.
    wxLogMessage("winPluginDir: Cannot lookup APPDATA");
    std::string path(wxGetHomeDir().ToStdString());
    path += "\\AppData\\Roaming";
    if (ocpn::exists(path)) {
      winPluginDir = wxString(path.c_str());
      ok = true;
      wxLogMessage("winPluginDir: using %s", winPluginDir.mb_str().data());
    }
  }
  if (!ok) {
    // {Documents and Settings}\.. on W7, else \ProgramData
    winPluginDir = GetHomeDir();
  }
  wxFileName path(winPluginDir);
  path.Normalize();
  winPluginDir = path.GetFullPath() + "\\opencpn\\plugins";
  wxLogMessage("Using private plugin dir: %s", winPluginDir);
  return winPluginDir;
}

wxString& BasePlatform::GetPluginDir() {
  if (m_PluginsDir.IsEmpty()) {
    wxStandardPaths& std_path = GetStdPaths();

    //  Get the PlugIns directory location
    m_PluginsDir = std_path.GetPluginsDir();  // linux:   {prefix}/lib/opencpn
    // Mac:     appname.app/Contents/PlugIns
#ifdef __WXMSW__
    m_PluginsDir += _T("\\plugins");  // Windows: {exe dir}/plugins
#endif
    if (g_bportable) {
      m_PluginsDir = GetHomeDir();
      m_PluginsDir += _T("plugins");
    }

#ifdef __ANDROID__
    // something like: data/data/org.opencpn.opencpn
    wxFileName fdir = wxFileName::DirName(std_path.GetUserConfigDir());
    fdir.RemoveLastDir();
    m_PluginsDir = fdir.GetPath();
#endif
  }
  return m_PluginsDir;
}

wxString* BasePlatform::GetPluginDirPtr() { return &m_PluginsDir; }

bool BasePlatform::isPlatformCapable(int flag) {
#ifndef __ANDROID__
  return true;
#else
  if (flag == PLATFORM_CAP_PLUGINS) {
    long platver;
    wxString tsdk(android_plat_spc.msdk);
    if (tsdk.ToLong(&platver)) {
      if (platver >= 11) return true;
    }
  } else if (flag == PLATFORM_CAP_FASTPAN) {
    long platver;
    wxString tsdk(android_plat_spc.msdk);
    if (tsdk.ToLong(&platver)) {
      if (platver >= 14) return true;
    }
  }

  return false;
#endif
}

void appendOSDirSlash(wxString* pString) {
  wxChar sep = wxFileName::GetPathSeparator();
  if (pString->Last() != sep) pString->Append(sep);
}

wxString BasePlatform::GetWritableDocumentsDir() {
  wxString dir;

#ifdef __ANDROID__
  dir = androidGetExtStorageDir();  // Used for Chart storage, typically
#else
  wxStandardPaths& std_path = GetStdPaths();
  dir = std_path.GetDocumentsDir();
#endif
  return dir;
}

bool BasePlatform::DetectOSDetail(OCPN_OSDetail* detail) {
  if (!detail) return false;

  // We take some defaults from build-time definitions
  detail->osd_name = std::string(PKG_TARGET);
  detail->osd_version = std::string(PKG_TARGET_VERSION);

  // Now parse by basic platform
#ifdef __linux__
  if (wxFileExists(_T("/etc/os-release"))) {
    wxTextFile release_file(_T("/etc/os-release"));
    if (release_file.Open()) {
      wxString val;
      for (wxString str = release_file.GetFirstLine(); !release_file.Eof();
           str = release_file.GetNextLine()) {
        if (str.StartsWith(_T("NAME"))) {
          val = str.AfterFirst('=').Mid(1);
          val = val.Mid(0, val.Length() - 1);
          if (val.Length()) detail->osd_name = std::string(val.mb_str());
        } else if (str.StartsWith(_T("VERSION_ID"))) {
          val = str.AfterFirst('=').Mid(1);
          val = val.Mid(0, val.Length() - 1);
          if (val.Length()) detail->osd_version = std::string(val.mb_str());
        } else if (str.StartsWith(_T("ID="))) {
          val = str.AfterFirst('=');
          if (val.Length()) detail->osd_ID = ocpn::split(val.mb_str(), " ")[0];
        } else if (str.StartsWith(_T("ID_LIKE"))) {
          if (val.StartsWith('"')) {
            val = str.AfterFirst('=').Mid(1);
            val = val.Mid(0, val.Length() - 1);
          } else {
            val = str.AfterFirst('=');
          }

          if (val.Length()) {
            detail->osd_names_like = ocpn::split(val.mb_str(), " ");
          }
        }
      }

      release_file.Close();
    }
    if (detail->osd_name == _T("Linux Mint")) {
      if (wxFileExists(_T("/etc/upstream-release/lsb-release"))) {
        wxTextFile upstream_release_file(
            _T("/etc/upstream-release/lsb-release"));
        if (upstream_release_file.Open()) {
          wxString val;
          for (wxString str = upstream_release_file.GetFirstLine();
               !upstream_release_file.Eof();
               str = upstream_release_file.GetNextLine()) {
            if (str.StartsWith(_T("DISTRIB_RELEASE"))) {
              val = str.AfterFirst('=').Mid(0);
              val = val.Mid(0, val.Length());
              if (val.Length()) detail->osd_version = std::string(val.mb_str());
            }
          }
          upstream_release_file.Close();
        }
      }
    }
  }
#endif

  //  Set the default processor architecture
  detail->osd_arch = std::string("x86_64");

  // then see what is actually running.
  wxPlatformInfo platformInfo = wxPlatformInfo::Get();
  wxArchitecture arch = platformInfo.GetArchitecture();
  if (arch == wxARCH_32) detail->osd_arch = std::string("i386");

#ifdef ocpnARM
  //  arm supports a multiarch runtime environment
  //  That is, the OS may be 64 bit, but OCPN may be built as a 32 bit binary
  //  So, we cannot trust the wxPlatformInfo architecture determination.
  detail->osd_arch = std::string("arm64");
#ifdef ocpnARMHF
  detail->osd_arch = std::string("armhf");
#endif
#endif

#ifdef __ANDROID__
  detail->osd_arch = std::string("arm64");
  if (arch == wxARCH_32) detail->osd_arch = std::string("armhf");
#endif

  return true;
}

wxString& BasePlatform::GetConfigFileName() {
  if (m_config_file_name.IsEmpty()) {
    //      Establish the location of the config file
    wxStandardPaths& std_path = GetStdPaths();

#ifdef __WXMSW__
    m_config_file_name = "opencpn.ini";
    m_config_file_name.Prepend(GetHomeDir());

#elif defined __WXOSX__
    m_config_file_name =
        std_path.GetUserConfigDir();  // should be ~/Library/Preferences
    appendOSDirSlash(&m_config_file_name);
    m_config_file_name.Append("opencpn");
    appendOSDirSlash(&m_config_file_name);
    m_config_file_name.Append("opencpn.ini");
#elif defined FLATPAK
    m_config_file_name = GetPrivateDataDir();
    m_config_file_name.Append("/opencpn.conf");
    // Usually ~/.var/app/org.opencpn.OpenCPN/config/opencpn.conf
#else
    m_config_file_name = std_path.GetUserDataDir();  // should be ~/.opencpn
    appendOSDirSlash(&m_config_file_name);
    m_config_file_name.Append("opencpn.conf");
#endif

    if (g_bportable) {
      m_config_file_name = GetHomeDir();
#ifdef __WXMSW__
      m_config_file_name += "opencpn.ini";
#elif defined __WXOSX__
      m_config_file_name += "opencpn.ini";
#else
      m_config_file_name += "opencpn.conf";
#endif
    }

#ifdef __ANDROID__
    m_config_file_name = androidGetPrivateDir();
    appendOSDirSlash(&m_config_file_name);
    m_config_file_name += "opencpn.conf";
#endif
  }
  return m_config_file_name;
}

bool BasePlatform::InitializeLogFile(void) {
  //      Establish Log File location
  mlog_file = GetPrivateDataDir();
  appendOSDirSlash(&mlog_file);

#ifdef __WXOSX__

  wxFileName LibPref(mlog_file);  // starts like "~/Library/Preferences/opencpn"
  LibPref.RemoveLastDir();        // takes off "opencpn"
  LibPref.RemoveLastDir();        // takes off "Preferences"

  mlog_file = LibPref.GetFullPath();
  appendOSDirSlash(&mlog_file);

  mlog_file.Append(_T("Logs/"));  // so, on OS X, opencpn.log ends up in
                                  // ~/Library/Logs which makes it accessible to
                                  // Applications/Utilities/Console....
#endif

  // create the opencpn "home" directory if we need to
  wxFileName wxHomeFiledir(GetHomeDir());
  if (true != wxHomeFiledir.DirExists(wxHomeFiledir.GetPath()))
    if (!wxHomeFiledir.Mkdir(wxHomeFiledir.GetPath())) {
      wxASSERT_MSG(false, _T("Cannot create opencpn home directory"));
      return false;
    }

  // create the opencpn "log" directory if we need to
  wxFileName wxLogFiledir(mlog_file);
  if (true != wxLogFiledir.DirExists(wxLogFiledir.GetPath())) {
    if (!wxLogFiledir.Mkdir(wxLogFiledir.GetPath())) {
      wxASSERT_MSG(false, _T("Cannot create opencpn log directory"));
      return false;
    }
  }

  mlog_file.Append(_T("opencpn.log"));
  wxString logit = mlog_file;

#ifdef __ANDROID__
  wxCharBuffer abuf = mlog_file.ToUTF8();
  qDebug() << "logfile " << abuf.data();
#endif

  //  Constrain the size of the log file
  if (::wxFileExists(mlog_file)) {
    if (wxFileName::GetSize(mlog_file) > 1000000) {
      wxString oldlog = mlog_file;
      oldlog.Append(_T(".log"));
      //  Defer the showing of this messagebox until the system locale is
      //  established.
      large_log_message = (_T("Old log will be moved to opencpn.log.log"));
      ::wxRenameFile(mlog_file, oldlog);
    }
  }
#ifdef __ANDROID__
  if (::wxFileExists(mlog_file)) {
    //  Force new logfile for each instance
    // TODO Remove this behaviour on Release
    ::wxRemoveFile(mlog_file);
  }

  if (wxLog::GetLogLevel() > wxLOG_User) wxLog::SetLogLevel(wxLOG_Info);

#elif CLIAPP
  wxLog::SetActiveTarget(new wxLogStderr);
  wxLog::SetTimestamp("");
  wxLog::SetLogLevel(wxLOG_Warning);
#else
  g_logger = new OcpnLog(mlog_file.mb_str());
  m_Oldlogger = wxLog::SetActiveTarget(g_logger);
#endif

  return true;
}

void BasePlatform::CloseLogFile(void) {
  if (g_logger) {
    wxLog::SetActiveTarget(m_Oldlogger);
    delete g_logger;
  }
}

wxString BasePlatform::GetPluginDataPath() {
  if (g_bportable) {
    wxString sep = wxFileName::GetPathSeparator();
    wxString ret = GetPrivateDataDir() + sep + _T("plugins");
    return ret;
  }

  if (m_pluginDataPath != "") {
    return m_pluginDataPath;
  }
  wxString dirs("");
#ifdef __ANDROID__
  wxString pluginDir = GetPrivateDataDir() + "/plugins";
  dirs += pluginDir;
#else
  auto const osSystemId = wxPlatformInfo::Get().GetOperatingSystemId();
  if (isFlatpacked()) {
    dirs = "~/.var/app/org.opencpn.OpenCPN/data/opencpn/plugins";
  } else if (osSystemId & wxOS_UNIX_LINUX) {
    dirs = GetLinuxDataPath();
  } else if (osSystemId & wxOS_WINDOWS) {
    dirs = GetWinPluginBaseDir();
  } else if (osSystemId & wxOS_MAC) {
    dirs = "/Applications/OpenCPN.app/Contents/SharedSupport/plugins;";
    dirs +=
        "~/Library/Application Support/OpenCPN/Contents/SharedSupport/plugins";
  }
#endif

  m_pluginDataPath = ExpandPaths(dirs, this);
  if (m_pluginDataPath != "") {
    m_pluginDataPath += ";";
  }
  m_pluginDataPath += GetPluginDir();
  if (m_pluginDataPath.EndsWith(wxFileName::GetPathSeparator())) {
    m_pluginDataPath.RemoveLast();
  }
  wxLogMessage("Using plugin data path: %s", m_pluginDataPath.mb_str().data());
  return m_pluginDataPath;
}
