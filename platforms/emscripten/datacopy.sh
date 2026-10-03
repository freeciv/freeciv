#!/bin/sh

# datacopy.sh: Copy data to emscripten build directory
#
# (c) 2026 Freeciv team
#
# This script is licensed under Gnu General Public License version 2 or later.
# See COPYING available from the same location you got this script.

DATA_SRC="$(cd "$(dirname "$0")/../../" || exit 1 ; pwd)"
DATA_TGT="$(pwd)"

if ! mkdir -p "${DATA_TGT}/data/themes" ; then
  echo "Failed to create data directories!" >&2
  exit 1
fi

if ! cp "${DATA_SRC}/data/helpdata.txt" "${DATA_TGT}/data/" ||
   ! cp -R "${DATA_SRC}/data/themes/gui-sdl2" "${DATA_TGT}/data/themes/"
then
  echo "Copying data failed!" >&2
  exit 1
fi
