#!/bin/sh

# ./create-freeciv-qt-nsi.sh <Freeciv files directory> <version> <win32|win64|win>

./create-freeciv-gtk-qt-nsi.sh $1 $2 "qt5" "Qt5" $3 "" "qt"
