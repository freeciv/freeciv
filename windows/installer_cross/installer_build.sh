#!/bin/bash

if test "x$1" = x || test "x$1" = "x-h" || test "x$1" = "x--help" || test "x$2" = "x" ; then
  echo "Usage: $0 <crosser dir> <gui>"
  exit 1
fi

DLLSPATH="$1"
GUI="$2"

case $GUI in
  gtk3.22)
    GUINAME="GTK3.22"
    FCMP="gtk3" ;;
  qt)
    GUINAME="Qt"
    FCMP="qt" ;;
  sdl2)
    GUINAME="SDL2"
    FCMP="gtk3" ;;  
  *)
    echo "Unknown gui type \"$GUI\"" >&2
    exit 1 ;;
esac

if ! test -d "$DLLSPATH" ; then
  echo "Dllstack directory \"$DLLSPATH\" not found!" >&2
  exit 1
fi

if ! ./winbuild.sh "$DLLSPATH" $GUI ; then
  exit 1
fi

SETUP=$(grep "Setup=" $DLLSPATH/crosser.txt | sed -e 's/Setup="//' -e 's/"//')

if test -d ../../.svn ; then
  VERREV="$(../../fc_version)-r$(cd ../.. && svn info | grep Revision | sed 's/Revision: //')"
else
  VERREV="$(../../fc_version)"
fi

INSTDIR="freeciv-$SETUP-$VERREV"

make -C build-$SETUP/translations/core update-po
make -C build-$SETUP/bootstrap langstat_core.txt

mv $INSTDIR/bin/* $INSTDIR/
mv $INSTDIR/share/freeciv $INSTDIR/data
mv $INSTDIR/share/doc $INSTDIR/
mkdir -p $INSTDIR/doc/freeciv/installer
cp licenses/COPYING.installer $INSTDIR/doc/freeciv/installer/
rm -Rf $INSTDIR/lib
cp freeciv-server.cmd freeciv-$GUI.cmd freeciv-mp-$FCMP.cmd Freeciv.url $INSTDIR/

if test "x$GUI" = "xsdl2" ; then
  if ! ./create-freeciv-sdl2-nsi.sh $INSTDIR $VERREV $SETUP > Freeciv-$SETUP-$VERREV-$GUI.nsi
  then
    exit 1
  fi
elif ! ./create-freeciv-gtk-qt-nsi.sh $INSTDIR $VERREV $GUI $GUINAME $SETUP > Freeciv-$SETUP-$VERREV-$GUI.nsi
then
  exit 1
fi

mkdir -p Output
makensis Freeciv-$SETUP-$VERREV-$GUI.nsi
