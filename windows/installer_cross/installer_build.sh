#!/usr/bin/env bash
#/***********************************************************************
# Freeciv - Copyright (C) 2017-2023
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2, or (at your option)
#   any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#***********************************************************************/

add_glib_env() {
  mkdir -p $2/share/glib-2.0/schemas &&
  cp -R $1/share/glib-2.0/schemas/*.gschema.xml $2/share/glib-2.0/schemas/ &&
  cp $1/bin/libglib-2.0-0.dll $2/ &&
  cp $1/bin/libpcre2-8-0.dll $2/ &&
  mkdir -p $2/bin &&
  cp $1/bin/glib-compile-schemas.exe $2/bin/
}

add_gtk_common_env() {
  mkdir -p $2/lib &&
  cp -R $1/lib/gdk-pixbuf-2.0 $2/lib/ &&
  mkdir -p $2/share/icons &&
  cp -R $1/share/locale $2/share/ &&
  cp -R $1/share/icons/Adwaita $2/share/icons/ &&
  cp $1/bin/libgobject-2.0-0.dll $2/ &&
  cp $1/bin/libpixman-1-0.dll $2/ &&
  cp $1/bin/libcairo-gobject-2.dll $2/ &&
  cp $1/bin/libcairo-2.dll $2/ &&
  cp $1/bin/libepoxy-0.dll $2/ &&
  cp $1/bin/libgdk_pixbuf-2.0-0.dll $2/ &&
  cp $1/bin/libgio-2.0-0.dll $2/ &&
  cp $1/bin/libfribidi-0.dll $2/ &&
  cp $1/bin/libpango-1.0-0.dll $2/ &&
  cp $1/bin/libpangocairo-1.0-0.dll $2/ &&
  cp $1/bin/libffi-8.dll $2/ &&
  cp $1/bin/libatk-1.0-0.dll $2/ &&
  cp $1/bin/libgmodule-2.0-0.dll $2/ &&
  cp $1/bin/libpangowin32-1.0-0.dll $2/ &&
  cp $1/bin/libfontconfig-1.dll $2/ &&
  cp $1/bin/libxml2-16.dll $2/ &&
  cp $1/bin/libjpeg-9.dll $2/ &&
  mkdir -p $2/bin &&
  cp $1/bin/gdk-pixbuf-query-loaders.exe $2/bin/
}

add_gtk3_env() {
  add_gtk_common_env $1 $2 &&
  mkdir -p $2/etc &&
  cp -R $1/etc/gtk-3.0 $2/etc/ &&
  cp $1/bin/libgtk-3-0.dll $2/ &&
  cp $1/bin/libgdk-3-0.dll $2/ &&
  cp $1/bin/gtk-update-icon-cache.exe $2/bin/ &&
  cp ./helpers/installer-helper-gtk3.cmd $2/bin/installer-helper.cmd
}

add_gtk4_env() {
  add_gtk_common_env $1 $2 &&
  mkdir -p $2/etc &&
  cp $1/bin/libgtk-4-1.dll $2/ &&
  cp $1/bin/libgraphene-1.0-0.dll $2/ &&
  cp $1/bin/libcairo-script-interpreter-2.dll $2/ &&
  cp $1/bin/libtiff-6.dll $2/ &&
  cp $1/bin/gdbus.exe $2/ &&
  cp $1/bin/gtk4-update-icon-cache.exe $2/bin/ &&
  cp ./helpers/installer-helper-gtk4.cmd $2/bin/installer-helper.cmd
}

add_sdl2_mixer_env() {
  cp $1/bin/SDL2.dll $2/ &&
  cp $1/bin/SDL2_mixer.dll $2/ &&
  cp $1/bin/libvorbisfile-3.dll $2/ &&
  cp $1/bin/libvorbis-0.dll $2/ &&
  cp $1/bin/libogg-0.dll $2/
}

add_sdl2_env() {
  cp $1/bin/SDL2_image.dll $2/ &&
  cp $1/bin/SDL2_ttf.dll $2/
}

add_qt6_env() {
  cp -R $1/qt6/plugins $2/ &&
  cp $1/bin/Qt6Core.dll $2/ &&
  cp $1/bin/Qt6Gui.dll $2/ &&
  cp $1/bin/Qt6Widgets.dll $2/ &&
  cp $1/bin/libpcre2-16-0.dll $2/ &&
  mkdir -p $2/bin &&
  cp ./helpers/installer-helper-qt.cmd $2/bin/installer-helper.cmd
}

add_common_env() {
  cp $1/bin/libcurl-4.dll     $2/ &&
  cp $1/bin/liblzma-5.dll     $2/ &&
  cp $1/bin/libzstd-1.dll     $2/ &&
  cp $1/bin/libintl-8.dll     $2/ &&
  cp $1/bin/libsqlite3-0.dll  $2/ &&
  cp $1/bin/libiconv-2.dll    $2/ &&
  cp $1/bin/libz.dll.1.3.1    $2/ &&
  cp $1/bin/icuuc71.dll       $2/ &&
  cp $1/bin/icudt71.dll       $2/ &&
  cp $1/bin/libpng16-16.dll   $2/ &&
  cp $1/bin/libfreetype-6.dll $2/ &&
  cp $1/bin/libharfbuzz-0.dll $2/ &&
  cp $1/bin/libharfbuzz-subset-0.dll $2/ &&
  cp $1/bin/libpsl-5.dll      $2/ &&
  cp $1/bin/libMagickWand-7.Q16HDRI-10.dll $2/ &&
  cp $1/bin/libMagickCore-7.Q16HDRI-10.dll $2/ &&
  add_glib_env $1 $2
}

if test "$1" = x || test "$1" = "-h" || test "$1" = "--help" || test "$2" = "" ; then
  echo "Usage: $0 <crosser dir> <gui>"
  exit 1
fi

DLLSPATH="$1"
GUI="$2"
NAMEP="-client-${GUI}"

case $GUI in
  gtk3.22)
    GUINAME="GTK3.22"
    MPGUI="gtk3"
    FCMP="gtk3" ;;
  qt6)
    GUINAME="Qt6"
    CLIENT="qt"
    MPGUI="qt"
    FCMP="qt" ;;
  sdl2)
    GUINAME="SDL2"
    FCMP="gtk4" ;;
  gtk4)
    GUINAME="GTK4"
    MPGUI="gtk4"
    FCMP="gtk4" ;;
  ruledit)
    NAMEP="-ruledit" ;;
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

if ! ./winbuild.sh "$DLLSPATH" "$GUI" ; then
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

INSTDIR="autotools/install/freeciv-$SETUP-${VERREV}${NAMEP}"

if test "$GUI" = "ruledit" ; then
  if ! make -C autotools/build/${SETUP}-${GUI}/translations/ruledit update-po ||
     ! make -C autotools/build/${SETUP}-${GUI}/bootstrap langstat_ruledit.txt
  then
    echo "Langstat creation failed!" >&2
    exit 1
  fi
  if test "$GITREVERT" = true ; then
    git checkout ../../translations/ruledit
  fi
else
  if ! make -C autotools/build/${SETUP}-client-${GUI}/translations/core update-po ||
     ! make -C autotools/build/${SETUP}-client-${GUI}/bootstrap langstat_core.txt
  then
    echo "Langstat creation failed!" >&2
    exit 1
  fi
  if test "$GITREVERT" = true ; then
    git checkout ../../translations/core
  fi
fi

if ! mv $INSTDIR/bin/* $INSTDIR/ ||
   ! mv $INSTDIR/share/freeciv $INSTDIR/data ||
   ! mv $INSTDIR/share/doc $INSTDIR/ ||
   ! mkdir -p $INSTDIR/doc/freeciv/installer ||
   ! cat licenses/header.txt ../../COPYING > $INSTDIR/doc/freeciv/installer/COPYING.installer ||
   ! rm -Rf $INSTDIR/lib ||
   ! cp Freeciv.url $INSTDIR/
then
  echo "Rearranging install directory failed!" >&2
  exit 1
fi

if ! add_common_env $DLLSPATH $INSTDIR ; then
  echo "Copying common environment failed!" >&2
  exit 1
fi

NSI_DIR="autotools/nsi"

if ! mkdir -p "$NSI_DIR" ; then
  echo "Cannot create \"$NSI_DIR\" directory" >&2
  exit 1
fi
if ! mkdir -p autotools/output ; then
  echo "Cannot create autotools/output directory" >&2
  exit 1
fi

if test "$GUI" = "ruledit" ; then
  if ! cp freeciv-ruledit.cmd "${INSTDIR}/"
  then
    echo "Adding cmd-file failed!" >&2
    exit 1
  fi

  if ! add_qt6_env "${DLLSPATH}" "${INSTDIR}" ; then
    echo "Copying Qt6 environment failed!" >&2
    exit 1
  fi

  if ! ./create-freeciv-ruledit-nsi.sh \
         "${INSTDIR}" "autotools/output" "${VERREV}" "qt6" "Qt6" "${SETUP}" \
           > "${NSI_DIR}/ruledit-${SETUP}-${VERREV}.nsi"
  then
    exit 1
  fi

  if ! makensis -NOCD "${NSI_DIR}/ruledit-${SETUP}-${VERREV}.nsi"
  then
    echo "Creating installer failed!" >&2
    exit 1
  fi
else
  if ! cp freeciv-server.cmd freeciv-$CLIENT.cmd freeciv-mp-$FCMP.cmd $INSTDIR/
  then
    echo "Adding cmd-files failed!" >&2
    exit 1
  fi

  if ! add_sdl2_mixer_env $DLLSPATH $INSTDIR ; then
    echo "Copying SDL2_mixer environment failed!" >&2
    exit 1
  fi

  case $GUI in
    gtk3.22)
      if ! add_gtk3_env $DLLSPATH $INSTDIR ; then
        echo "Copying gtk3 environment failed!" >&2
        exit 1
      fi ;;
    sdl2)
      if ! add_gtk4_env $DLLSPATH $INSTDIR ; then
        echo "Copying gtk4 environment failed!" >&2
        exit 1
      fi
      if ! add_sdl2_env $DLLSPATH $INSTDIR ; then
        echo "Copying SDL2 environment failed!" >&2
        exit 1
      fi ;;
    qt6)
      if ! cp freeciv-ruledit.cmd "${INSTDIR}/"
      then
        echo "Adding cmd-file failed!" >&2
        exit 1
      fi
      if ! add_qt6_env "${DLLSPATH}" "${INSTDIR}" ; then
        echo "Copying Qt6 environment failed!" >&2
        exit 1
      fi ;;
    gtk4)
      if ! add_gtk4_env $DLLSPATH $INSTDIR ; then
        echo "Copying gtk4 environment failed!" >&2
        exit 1
      fi ;;
  esac

  if test "$GUI" = "sdl2" ; then
    if ! ./create-freeciv-sdl-nsi.sh \
           "$INSTDIR" "autotools/output" "$VERREV" "$SETUP" \
           "helpers/uninstaller-helper-gtk3.sh" \
             > "${NSI_DIR}/client-${SETUP}-${VERREV}-${GUI}.nsi"
    then
      exit 1
    fi
  else
    if test "${GUI}" = "qt6" ; then
      EXE_ID="qt"
    else
      EXE_ID="${GUI}"
    fi
    if test "$GUI" = "gtk3.22" || test "$GUI" = "gtk4" ; then
      UNINSTALLER="helpers/uninstaller-helper-gtk3.sh"
    else
      UNINSTALLER=""
    fi
    if ! ./create-freeciv-gtk-qt-nsi.sh \
           "$INSTDIR" "autotools/output" "$VERREV" "$GUI" "$GUINAME" \
           "$SETUP" "$MPGUI" "$EXE_ID" "$UNINSTALLER" \
             > "${NSI_DIR}/client-${SETUP}-${VERREV}-${GUI}.nsi"
    then
      exit 1
    fi
  fi

  if ! makensis -NOCD "${NSI_DIR}/client-${SETUP}-${VERREV}-${GUI}.nsi"
  then
    echo "Creating installer failed!" >&2
    exit 1
  fi
fi
