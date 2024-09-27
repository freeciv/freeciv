#!/bin/sh

# ./create-freeciv-gtk4x-nsi.sh <freeciv files dir> <output dir> <version> <win32|win64|win>

./create-freeciv-gtk-qt-nsi.sh "$1" "$2" "$3" "gtk4x" "GTK4x" "$4" "gtk4x"
