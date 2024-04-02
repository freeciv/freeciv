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

if ! mkdir AppDir ||
   ! mkdir server
then
  echo "Failed to create required directories!" >&2
  exit 1
fi

if ! wget "https://github.com/linuxdeploy/linuxdeploy/releases/download/${LINUXDEPLOY_VERSION}/linuxdeploy-x86_64.AppImage"
then
  echo "Failed to download linuxdeploy!" >&2
  exit 1
fi
chmod u+x linuxdeploy-x86_64.AppImage

cd server
if ! meson setup -Dack_experimental=true -Dappimage=true -Dprefix=/usr -Ddefault_library=static \
                 -Dclients=[] -Dfcmp=[] -Druledit=false "${SRC_ROOT}"
then
  echo "Setup with meson failed!" >&2
  exit 1
fi

if ! DESTDIR="${BUILD_ROOT}/AppDir" ninja install ; then
  echo "Build with ninja failed!" >&2
  exit 1
fi

cd "${BUILD_ROOT}"
if ! ./linuxdeploy-x86_64.AppImage --appdir AppDir --output appimage
then
  echo "Image build with linuxdeploy failed!" >&2
  exit 1
fi
