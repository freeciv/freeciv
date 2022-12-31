#!/bin/sh

# ./create-freeciv-qt-nsi.sh <Freeciv files dir> <Output dir> <version> <win32|win64|win>

./create-freeciv-gtk-qt-nsi.sh "$1" "$2" "$3" "qt5" "Qt5" "$4" "" "qt"
