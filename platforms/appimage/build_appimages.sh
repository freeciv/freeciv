#!/usr/bin/env bash

# build_appimages.sh: Build freeciv AppImages
#
# (c) 2024 Freeciv team
#
# This script is licensed under Gnu General Public License version 2 or later.
# See COPYING available from the same location you got this script.

LINUXDEPLOY_VERSION="1-alpha-20240109-1"

if test "$1" != "" ; then
  echo "Usage: $0"
  exit 1
fi

BUILD_ROOT="$(pwd)"
PLATFORM_ROOT="$(cd $(dirname "$0") && pwd)"
SRC_ROOT="$(cd "$PLATFORM_ROOT/../.." && pwd)"

if test "${PLATFORM_ROOT}" = "${BUILD_ROOT}" ; then
  echo "Run $0 from a separate build directory." >&2
  exit 1
fi

if ! mkdir tools ||
   ! mkdir -p AppDir/server ||
   ! mkdir AppDir/gtk4 ||
   ! mkdir -p build/server ||
   ! mkdir build/gtk4
then
  echo "Failed to create required directories!" >&2
  exit 1
fi

if ! ( cd tools &&
       wget "https://github.com/linuxdeploy/linuxdeploy/releases/download/${LINUXDEPLOY_VERSION}/linuxdeploy-x86_64.AppImage" )
then
  echo "Failed to download linuxdeploy!" >&2
  exit 1
fi
chmod u+x tools/linuxdeploy-x86_64.AppImage

cd build/server
if ! meson setup -Dappimage=true -Dprefix=/usr -Ddefault_library=static -Dclients=[] -Dfcmp=[] -Dtools=[] "${SRC_ROOT}"
then
  echo "Server setup with meson failed!" >&2
  exit 1
fi

if ! DESTDIR="${BUILD_ROOT}/AppDir/server" ninja install ; then
  echo "Server build with ninja failed!" >&2
  exit 1
fi

cd "${BUILD_ROOT}"
if ! tools/linuxdeploy-x86_64.AppImage --appdir AppDir/server --output appimage
then
  echo "Server image build with linuxdeploy failed!" >&2
  exit 1
fi

cd build/gtk4
if ! meson setup -Dappimage=true -Dprefix=/usr -Ddefault_library=static -Dclients=gtk4 -Dfcmp=[] -Dtools=[] "${SRC_ROOT}"
then
  echo "Gtk4-client setup with meson failed!" >&2
  exit 1
fi

if ! DESTDIR="${BUILD_ROOT}/AppDir/gtk4" ninja install ; then
  echo "Gtk4-client build with ninja failed!" >&2
  exit 1
fi

cd "${BUILD_ROOT}"
if ! tools/linuxdeploy-x86_64.AppImage --appdir AppDir/gtk4 --output appimage
then
  echo "Gtk4-client image build with linuxdeploy failed!" >&2
  exit 1
fi
