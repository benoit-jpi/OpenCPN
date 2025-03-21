/***************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  OpenCPN Main wxWidgets Program
 * Author:   David Register
 *
 ***************************************************************************
 *   Copyright (C) 2010 by David S. Register                               *
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

#ifndef MESSAGEBOX_H
#define MESSAGEBOX_H

#include <wx/font.h>
#include <wx/html/htmlwin.h>
#include <wx/msgdlg.h>
#include <wx/timer.h>
#include <wx/window.h>

wxFont* GetOCPNScaledFont(wxString item, int default_size = 0);
wxFont GetOCPNGUIScaledFont(wxString item);

extern int OCPNMessageBox(wxWindow* parent, const wxString& message,
                          const wxString& caption = _T("Message"),
                          int style = wxOK, int timout_sec = -1, int x = -1,
                          int y = -1);

class OCPNMessageDialog : public wxDialog {
public:
  OCPNMessageDialog(wxWindow* parent, const wxString& message,
                    const wxString& caption = wxMessageBoxCaptionStr,
                    long style = wxOK | wxCENTRE,
                    const wxPoint& pos = wxDefaultPosition);

  void OnYes(wxCommandEvent& event);
  void OnNo(wxCommandEvent& event);
  void OnCancel(wxCommandEvent& event);
  void OnClose(wxCloseEvent& event);

private:
  int m_style;
  DECLARE_EVENT_TABLE()
};

class TimedMessageBox : wxEvtHandler {
public:
  TimedMessageBox(wxWindow* parent, const wxString& message,
                  const wxString& caption = _T("Message box"),
                  long style = wxOK | wxCANCEL, int timeout_sec = -1,
                  const wxPoint& pos = wxDefaultPosition);
  ~TimedMessageBox();
  int GetRetVal(void) { return ret_val; }
  void OnTimer(wxTimerEvent& evt);

  wxTimer m_timer;
  OCPNMessageDialog* dlg;
  int ret_val;

  DECLARE_EVENT_TABLE()
};

class OCPN_TimedHTMLMessageDialog : public wxDialog {
public:
  OCPN_TimedHTMLMessageDialog(wxWindow* parent, const wxString& message,
                              const wxString& caption = wxMessageBoxCaptionStr,
                              int tSeconds = -1, long style = wxOK | wxCENTRE,
                              bool bFixedFont = false,
                              const wxPoint& pos = wxDefaultPosition);

  void OnYes(wxCommandEvent& event);
  void OnNo(wxCommandEvent& event);
  void OnCancel(wxCommandEvent& event);
  void OnClose(wxCloseEvent& event);
  void OnTimer(wxTimerEvent& evt);
  void RecalculateSize(void);

private:
  int m_style;
  wxTimer m_timer;
  wxHtmlWindow* msgWindow;

  DECLARE_EVENT_TABLE()
};

#endif  // MESSAGE_BOX_H
