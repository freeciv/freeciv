#!/bin/sh

# ./create-freeciv-sdl3-nsi.sh <freeciv files dir> <output dir> <version> <win32|win64|win>

./create-freeciv-sdl-nsi.sh "$1" "$2" "$3" "$4" "sdl3" "SDL3"
