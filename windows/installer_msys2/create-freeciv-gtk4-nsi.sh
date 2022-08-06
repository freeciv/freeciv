#!/bin/sh

# ./create-freeciv-gtk4-nsi.sh <Freeciv files directory> <version> <win32|win64|win>

./create-freeciv-gtk-qt-nsi.sh $1 $2 "gtk4" "GTK4" $3 "gtk4"
