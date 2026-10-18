#!/bin/sh

# datacopy.sh: Copy data to emscripten build directory
#
# (c) 2026 Freeciv team
#
# This script is licensed under Gnu General Public License version 2 or later.
# See COPYING available from the same location you got this script.

DATA_SRC="$(cd "$(dirname "$0")/../../" || exit 1 ; pwd)"
DATA_TGT="$(pwd)"

if ! mkdir -p \
     "${DATA_TGT}/data/themes" \
     "${DATA_TGT}/data/amplio2" \
     "${DATA_TGT}/data/flags"
then
  echo "Failed to create data directories!" >&2
  exit 1
fi

if ! cp "${DATA_SRC}/data/helpdata.txt" "${DATA_TGT}/data/" ||
   ! cp -R "${DATA_SRC}/data/themes/gui-sdl2" "${DATA_TGT}/data/themes/" ||
   ! cp -R "${DATA_SRC}/data/misc" "${DATA_TGT}/data/" ||
   ! cp -R "${DATA_SRC}/data/buildings" "${DATA_TGT}/data/" ||
   ! cp -R "${DATA_SRC}/data/wonders" "${DATA_TGT}/data/" ||
   ! cp -R "${DATA_SRC}/data/flags/"*"-large.png" "${DATA_TGT}/data/flags/" ||
   ! cp -R "${DATA_SRC}/data/override" "${DATA_TGT}/data/" ||
   ! cp "${DATA_SRC}/data/hexemplio.tilespec" "${DATA_TGT}/data/" ||
   ! cp "${DATA_SRC}/data/hexemplio.modpack" "${DATA_TGT}/data/" ||
   ! cp -R "${DATA_SRC}/data/hexemplio" "${DATA_TGT}/data/" ||
   ! cp "${DATA_SRC}/data/amplio2/units."* "${DATA_TGT}/data/amplio2/" ||
   ! cp "${DATA_SRC}/data/amplio2/extra_units."* "${DATA_TGT}/data/amplio2/" ||
   ! cp "${DATA_SRC}/data/amplio2/animals."* "${DATA_TGT}/data/amplio2/" ||
   ! cp "${DATA_SRC}/data/amplio2/nuke."* "${DATA_TGT}/data/amplio2/" ||
   ! cp "${DATA_SRC}/data/amplio2/explosions."* "${DATA_TGT}/data/amplio2/"
then
  echo "Copying data failed!" >&2
  exit 1
fi
