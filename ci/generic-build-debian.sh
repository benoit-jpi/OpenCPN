#!/usr/bin/env bash

#
# Build the Travis Debian artifacts
#
set -xe
sudo apt-get -qq update
sudo apt-get install -q devscripts equivs

mk-build-deps ./ci/control --install --root-cmd=sudo --remove
sudo apt-get --allow-unauthenticated install -f

# Xenial finds webview header but not the library:
if [ "$OCPN_TARGET" = "xenial" ]; then
    WEBVIEW_OPT="-DOCPN_USE_WEBVIEW:BOOL=OFF"
fi

if [[ "$EXTRA_BUILD_OPTS" == *OCPN_FORCE_GTK3=ON* ]]; then
    sudo update-alternatives --set wx-config \
        /usr/lib/*-linux-*/wx/config/gtk3-unicode-3.0
fi

rm -rf build && mkdir build && cd build
cmake $WEBVIEW_OPT  $EXTRA_BUILD_OPTS\
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DOCPN_CI_BUILD:BOOL=ON \
    -DOCPN_USE_BUNDLED_LIBS=OFF \
    ..
make -sj2
make package

sudo apt-get install python3-pip python3-setuptools
