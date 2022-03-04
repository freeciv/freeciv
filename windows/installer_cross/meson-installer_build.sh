#!/bin/bash

if test "$1" = "" || test "$1" = "-h" || test "$1" = "--help" ||
   test "$2" = "" ; then
  echo "Usage: $0 <crosser dir> <gui>"
  exit 1
fi

DLLSPATH="$1"
GUI="$2"

case $GUI in
  gtk3.22)
    GUINAME="GTK3.22"
    MPGUI="gtk3"
    FCMP="gtk3" ;;
  *)
    echo "Unknown gui type \"$GUI\"" >&2
    exit 1 ;;
esac

if test "$CLIENT" = "" ; then
  CLIENT="$GUI"
fi

if ! test -d "$DLLSPATH" ; then
  echo "Dllstack directory \"$DLLSPATH\" not found!" >&2
  exit 1
fi

if ! ./meson-winbuild.sh "$DLLSPATH" $GUI ; then
  exit 1
fi

SETUP=$(grep "CrosserSetup=" $DLLSPATH/crosser.txt | sed -e 's/CrosserSetup="//' -e 's/"//')

VERREV="$(../../fc_version)"

if test "$INST_CROSS_MODE" != "release" ; then
  if test -d ../../.git || test -f ../../.git ; then
    VERREV="$VERREV-$(cd ../.. && git rev-parse --short HEAD)"
    GITREVERT=true
  fi
fi

INSTDIR="meson-install/freeciv-${VERREV}-${SETUP}-${GUI}"

if ! mv $INSTDIR/bin/* $INSTDIR/ ||
   ! mv $INSTDIR/share/freeciv $INSTDIR/data ||
   ! mv $INSTDIR/share/doc $INSTDIR/ ||
   ! mkdir -p $INSTDIR/doc/freeciv/installer ||
   ! cp licenses/COPYING.installer $INSTDIR/doc/freeciv/installer/ ||
   ! rm -Rf $INSTDIR/lib ||
   ! cp Freeciv.url $INSTDIR/
then
  echo "Rearranging install directory failed!" >&2
  exit 1
fi

if test "$GUI" = "ruledit" ; then
  echo "Ruledit installer build not supported yet!" >&2
  exit 1
else
  if ! cp freeciv-server.cmd freeciv-${CLIENT}.cmd freeciv-mp-${FCMP}.cmd $INSTDIR/
  then
    echo "Adding cmd-files failed!" >&2
    exit 1
  fi

  if test "$GUI" = "sdl" ; then
    echo "sdl2 installer build not supported yet!" >&2
    exit 1
  else
    if test "$GUI" = "qt5" || test "$GUI" = "qt6" ; then
      echo "Qt installer build not supported yet!" >&2
      exit 1
      EXE_ID="qt"
    else
      EXE_ID="$GUI"
    fi

    if test "$GUI" = "gtk3.22" || test "$GUI" = "gtk4" ; then
      UNINSTALLER="helpers/uninstaller-helper-gtk3.sh"
    else
      UNINSTALLER=""
    fi

    if ! ./create-freeciv-gtk-qt-nsi.sh $INSTDIR $VERREV $GUI $GUINAME \
         $SETUP $MPGUI $EXE_ID $UNINSTALLER > meson-freeciv-$SETUP-$VERREV-$GUI.nsi
    then
      exit 1
    fi

    if ! mkdir -p Output ; then
      echo "Creating Output directory failed" >&2
      exit 1
    fi
    if ! makensis meson-freeciv-$SETUP-$VERREV-$GUI.nsi
    then
      echo "Creating installer failed!" >&2
      exit 1
    fi
  fi
fi
