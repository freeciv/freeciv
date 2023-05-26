#!/bin/sh

# ./create-freeciv-gtk3.22-nsi.sh <freeciv files dir> <output dir> <version> <win32|win64|win>

./create-freeciv-gtk-qt-nsi.sh "$1" "$2" "$3" "gtk3.22" "GTK+3.22" "$4" "gtk3"
