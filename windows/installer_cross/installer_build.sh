#!/bin/bash

add_glib_env() {
  mkdir -p $2/share/glib-2.0/schemas &&
  cp -R $1/share/glib-2.0/schemas/*.gschema.xml $2/share/glib-2.0/schemas/ &&
  cp $1/bin/libglib-2.0-0.dll $2/ &&
  cp $1/bin/libpcre-1.dll $2/ &&
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
  cp $1/bin/libpangoft2-1.0-0.dll $2/ &&
  cp $1/bin/libxml2-2.dll $2/ &&
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
  cp $1/bin/SDL2_ttf.dll $2/ &&
  cp $1/bin/libtiff-5.dll $2/
}

add_qt5_env() {
  cp -R $1/qt5/plugins $2/ &&
  cp $1/bin/Qt5Core.dll $2/ &&
  cp $1/bin/Qt5Gui.dll $2/ &&
  cp $1/bin/Qt5Widgets.dll $2/ &&
  cp $1/bin/libpcre2-16-0.dll $2/ &&
  mkdir -p $2/bin &&
  cp ./helpers/installer-helper-qt.cmd $2/bin/installer-helper.cmd
}

add_qt6_env() {
  cp -R $1/qt6/plugins $2/ &&
  cp $1/bin/Qt6Core.dll $2/ &&
  cp $1/bin/Qt6Gui.dll $2/ &&
  cp $1/bin/Qt6Widgets.dll $2/ &&
  mkdir -p $2/bin &&
  cp ./helpers/installer-helper-qt.cmd $2/bin/installer-helper.cmd
}

add_common_env() {
  cp $1/bin/libcurl-4.dll $2/ &&
  cp $1/bin/liblzma-5.dll $2/ &&
  cp $1/bin/libzstd-1.dll $2/ &&
  cp $1/bin/libintl-8.dll $2/ &&
  cp $1/bin/libsqlite3-0.dll $2/ &&
  cp $1/bin/libiconv-2.dll $2/ &&
  cp $1/bin/libz.dll.1.2.11 $2/ &&
  cp $1/lib/icuuc64.dll     $2/ &&
  cp $1/lib/icudt64.dll     $2/ &&
  cp $1/bin/libpng16-16.dll $2/ &&
  cp $1/bin/libfreetype-6.dll $2/ &&
  cp $1/bin/libharfbuzz-0.dll $2/ &&
  add_glib_env $1 $2
}

if test "x$1" = x || test "x$1" = "x-h" || test "x$1" = "x--help" || test "x$2" = "x" ; then
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
  qt5)
    GUINAME="Qt5"
    CLIENT="qt"
    MPGUI="qt"
    FCMP="qt" ;;
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
  ruledit) ;;
  *)
    echo "Unknown gui type \"$GUI\"" >&2
    exit 1 ;;
esac

if test "x$CLIENT" = "x" ; then
  CLIENT="$GUI"
fi

if ! test -d "$DLLSPATH" ; then
  echo "Dllstack directory \"$DLLSPATH\" not found!" >&2
  exit 1
fi

if ! ./winbuild.sh "$DLLSPATH" $GUI ; then
  exit 1
fi

SETUP=$(grep "CrosserSetup=" $DLLSPATH/crosser.txt | sed -e 's/CrosserSetup="//' -e 's/"//')

VERREV="$(../../fc_version)"

if test "x$INST_CROSS_MODE" != "xrelease" ; then
  if test -d ../../.git || test -f ../../.git ; then
    VERREV="$VERREV-$(cd ../.. && git rev-parse --short HEAD)"
    GITREVERT=true
  fi
fi

INSTDIR="freeciv-$SETUP-${VERREV}-${GUI}"

if test "x$GUI" = "xruledit" ; then
  if ! make -C build-${SETUP}-${GUI}/translations/ruledit update-po ||
     ! make -C build-${SETUP}-${GUI}/bootstrap langstat_ruledit.txt
  then
    echo "Langstat creation failed!" >&2
    exit 1
  fi
  if test "$GITREVERT" = true ; then
    git checkout ../../translations/ruledit
  fi
else
  if ! make -C build-${SETUP}-${GUI}/translations/core update-po ||
     ! make -C build-${SETUP}-${GUI}/bootstrap langstat_core.txt
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

if test "x$GUI" = "xruledit" ; then
  if ! cp freeciv-ruledit.cmd $INSTDIR/
  then
    echo "Adding cmd-file failed!" >&2
    exit 1
  fi

  if ! add_qt5_env $DLLSPATH $INSTDIR ; then
    echo "Copying Qt5 environment failed!" >&2
    exit 1
  fi

  if ! ./create-freeciv-ruledit-nsi.sh $INSTDIR $VERREV $SETUP > Freeciv-ruledit-$SETUP-$VERREV.nsi
  then
    exit 1
  fi

  if ! mkdir -p Output ; then
    echo "Cannot create Output directory" >&2
    exit 1
  fi
  if ! makensis Freeciv-ruledit-$SETUP-$VERREV.nsi
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
    qt5)
      if ! cp freeciv-ruledit.cmd $INSTDIR/
      then
        echo "Adding cmd-file failed!" >&2
        exit 1
      fi
      if ! add_qt5_env $DLLSPATH $INSTDIR ; then
        echo "Copying Qt5 environment failed!" >&2
        exit 1
      fi ;;
    qt6)
      if ! cp freeciv-ruledit.cmd $INSTDIR/
      then
        echo "Adding cmd-file failed!" >&2
        exit 1
      fi
      if ! add_qt6_env $DLLSPATH $INSTDIR ; then
        echo "Copying Qt6 environment failed!" >&2
        exit 1
      fi ;;
    gtk4)
      if ! add_gtk4_env $DLLSPATH $INSTDIR ; then
        echo "Copying gtk4 environment failed!" >&2
        exit 1
      fi ;;
  esac

  if test "x$GUI" = "xsdl2" ; then
    if ! ./create-freeciv-sdl2-nsi.sh $INSTDIR $VERREV $SETUP helpers/uninstaller-helper-gtk3.sh \
           > Freeciv-$SETUP-$VERREV-$GUI.nsi
    then
      exit 1
    fi
  else
    if test "x$GUI" = "xqt5" || test "x$GUI" = "xqt6" ; then
      EXE_ID="qt"
    else
      EXE_ID="$GUI"
    fi
    if test "x$GUI" = "xgtk3.22" || test "x$GUI" = "xgtk4" ; then
      UNINSTALLER="helpers/uninstaller-helper-gtk3.sh"
    else
      UNINSTALLER=""
    fi
    if ! ./create-freeciv-gtk-qt-nsi.sh $INSTDIR $VERREV $GUI $GUINAME \
         $SETUP $MPGUI $EXE_ID $UNINSTALLER > Freeciv-$SETUP-$VERREV-$GUI.nsi
    then
      exit 1
    fi
  fi

  mkdir -p Output
  if ! makensis Freeciv-$SETUP-$VERREV-$GUI.nsi
  then
    echo "Creating installer failed!" >&2
    exit 1
  fi
fi
