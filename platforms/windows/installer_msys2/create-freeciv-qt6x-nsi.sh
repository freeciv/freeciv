#!/bin/sh

# ./create-freeciv-qt6x-nsi.sh <freeciv files dir> <output dir> <version> <win32|win64|win>

./create-freeciv-gtk-qt-nsi.sh "$1" "$2" "$3" "qt6x" "Qt6x" "$4" "" "qt"
