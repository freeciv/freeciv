#!/bin/sh

# emssetup.sh: Setup emscripten environment for freeciv build
#
# (c) 2024-2025 Freeciv team
#
# This script is licensed under Gnu General Public License version 2 or later.
# See COPYING available from the same location you got this script.

# https://github.com/emscripten-core/emscripten/blob/main/ChangeLog.md
EMSDK_VER=4.0.5

if test "$1" = "" || test "$1" = "-h" || test "$1" = "--help" ; then
  echo "Usage: $0 <target directory>"
  exit 1
fi

if test -e "$1" ; then
  echo "$1 exist already!" >&2
  exit 1
fi

if ! git clone https://github.com/emscripten-core/emsdk "$1" ; then
  echo "Failed to clone https://github.com/emscripten-core/emsdk" >&2
  exit 1
fi

if ! cd "$1" ||
   ! ./emsdk install "${EMSDK_VER}" ||
   ! ./emsdk activate "${EMSDK_VER}"
then
  echo "emsdk setup failed!" >&2
  exit 1
fi
