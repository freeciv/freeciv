#!/bin/sh

# emsbuild.sh: Build freeciv using emsdk
#
# (c) 2023 Freeciv team
#
# This script is licensed under Gnu General Public License version 2 or later.
# See COPYING available from the same location you got this script.

if test "$1" = "" || test "$1" = "-h" || test "$1" = "--help" ; then
  echo "Usage: $0 <emsdk root dir>"
  exit 1
fi

EMSDK_ROOT="$1"
BUILD_ROOT="$(pwd)"
PLATFORM_ROOT="$(cd $(dirname "$0") && pwd)"

if test "${PLATFORM_ROOT}" = "${BUILD_ROOT}" ; then
  echo "Run $0 from a separate build directory." >&2
  exit 1
fi

if ! test -f "${EMSDK_ROOT}/emsdk_env.sh" ; then
  echo "No environment setup script emsdk_env.sh in \"${EMSDK_ROOT}\"" >&2
  exit 1
fi

# Sometimes emsdk environment setup script requires
# cwd to be it's own directory.
cd "${EMSDK_ROOT}" || exit 1
if ! . "./emsdk_env.sh" ; then
  echo "Sourcing \"${EMSDK_ROOT}/emsdk_env.sh\" failed!" >&2
  exit 1
fi
cd "${BUILD_ROOT}" || exit 1

# Add more emsdk directories to PATH
export PATH="${EMSDK_ROOT}/upstream/emscripten:$PATH"

sed -e "s,<EMSDK_ROOT>,${EMSDK_ROOT}," \
    "${PLATFORM_ROOT}/setups/cross-ems.tmpl" > cross.txt

if ! CC=emcc CXX=em++ AR=emar meson setup \
     --cross-file=cross.txt \
     -Ddefault_library=static \
     -Ddebug=true \
     -Dmwand=false \
     -Druledit=false \
     -Dclients=sdl2,stub \
     -Dfcmp=[] \
     "${PLATFORM_ROOT}/../../"
then
  echo "Setup with meson failed!" >&2
  exit 1
fi

if ! ninja ; then
  echo "Build with ninja failed!" >&2
  exit 1
fi
