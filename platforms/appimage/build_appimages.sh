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

client_appimage() {
  if ! mkdir "AppDir/$1" || ! mkdir "build/$1" ; then
    echo "Failed to create $1 directories!" >&2
    return 1
  fi

  cd "build/$1"
  if ! meson setup -Dappimage=true -Dprefix=/usr -Ddefault_library=static -Dclients=$1 -Dfcmp=[] -Dtools=[] "${SRC_ROOT}"
  then
    echo "$1 setup with meson failed!" >&2
    return 1
  fi

  if ! DESTDIR="${BUILD_ROOT}/AppDir/$1" ninja install ; then
    echo "$1 build with ninja failed!" >&2
    return 1
  fi

  cd "${BUILD_ROOT}"
  if ! tools/linuxdeploy-x86_64.AppImage --appdir "AppDir/$1" --output appimage
  then
    echo "$1 image build with linuxdeploy failed!" >&2
    return 1
  fi
  if ! mv Freeciv-x86_64.AppImage "Freeciv-$1-x86_64.AppImage" ; then
    echo "$1 appimage rename failed!" >&2
    return 1
  fi
}

if ! mkdir tools ||
   ! mkdir -p AppDir/server ||
   ! mkdir -p build/server
then
  echo "Failed to create server directories!" >&2
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

if ! client_appimage gtk4 ; then
  exit 1
fi
