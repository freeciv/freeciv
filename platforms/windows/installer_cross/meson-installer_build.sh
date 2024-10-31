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

add_common_env() {
  cp $1/bin/libcurl-4.dll  $2/ &&
  cp $1/bin/libz.dll.1.3.1 $2/ &&
  cp $1/bin/liblzma-5.dll  $2/ &&
  cp $1/bin/libzstd-1.dll  $2/ &&
  cp $1/bin/libintl-8.dll  $2/ &&
  cp $1/bin/libiconv-2.dll $2/ &&
  cp $1/bin/libsqlite3-0.dll  $2/ &&
  cp $1/lib/icuuc66.dll       $2/ &&
  cp $1/lib/icudt66.dll       $2/ &&
  cp $1/bin/libfreetype-6.dll $2/ &&
  cp $1/bin/libpng16-16.dll   $2/ &&
  cp $1/bin/libharfbuzz-0.dll $2/ &&
  cp $1/bin/libpsl-5.dll $2/
}

add_glib_env() {
  cp $1/bin/libglib-2.0-0.dll $2/ &&
  cp $1/bin/libpcre2-8-0.dll $2/
}

add_sdl2_mixer_env() {
  cp $1/bin/SDL2.dll $2/ &&
  cp $1/bin/SDL2_mixer.dll $2/ &&
  cp $1/bin/libvorbis-0.dll $2/ &&
  cp $1/bin/libvorbisfile-3.dll $2/ &&
  cp $1/bin/libogg-0.dll $2/
}

add_gtk_common_env() {
  cp $1/bin/libcairo-2.dll $2/ &&
  cp $1/bin/libcairo-gobject-2.dll $2/ &&
  cp $1/bin/libgdk_pixbuf-2.0-0.dll $2/ &&
  cp $1/bin/libgio-2.0-0.dll $2/ &&
  cp $1/bin/libgobject-2.0-0.dll $2/ &&
  cp $1/bin/libpango-1.0-0.dll $2/ &&
  cp $1/bin/libpangocairo-1.0-0.dll $2/ &&
  cp $1/bin/libpangowin32-1.0-0.dll $2/ &&
  cp $1/bin/libfontconfig-1.dll $2/ &&
  cp $1/bin/libpixman-1-0.dll $2/ &&
  cp $1/bin/libgmodule-2.0-0.dll $2/ &&
  cp $1/bin/libjpeg-9.dll $2/ &&
  cp $1/bin/libepoxy-0.dll $2/ &&
  cp $1/bin/libfribidi-0.dll $2/ &&
  cp $1/bin/libatk-1.0-0.dll $2/ &&
  cp $1/bin/libffi-8.dll $2/ &&
  cp $1/bin/libxml2-2.dll $2/ &&
  cp $1/bin/libMagickWand-7.Q16HDRI-10.dll $2/ &&
  cp $1/bin/libMagickCore-7.Q16HDRI-10.dll $2/ &&
  mkdir -p $2/lib &&
  cp -R $1/lib/gdk-pixbuf-2.0 $2/lib/ &&
  mkdir -p $2/share/icons &&
  cp -R $1/share/locale $2/share/ &&
  cp -R $1/share/icons/Adwaita $2/share/icons/ &&
  mkdir -p $2/bin &&
  cp $1/bin/gdk-pixbuf-query-loaders.exe $2/bin/ &&
  add_glib_env $1 $2
}

add_gtk3_env() {
  add_gtk_common_env $1 $2 &&
  cp $1/bin/libgdk-3-0.dll $2/ &&
  cp $1/bin/libgtk-3-0.dll $2/ &&
  mkdir -p $2/etc &&
  cp -R $1/etc/gtk-3.0 $2/etc/ &&
  cp $1/bin/gtk-update-icon-cache.exe $2/bin/ &&
  cp "${SRC_DIR}/helpers/installer-helper-gtk3.cmd" $2/bin/installer-helper.cmd
}

add_gtk4_env() {
  add_gtk_common_env $1 $2 &&
  cp $1/bin/libgtk-4-1.dll $2/ &&
  cp $1/bin/libgraphene-1.0-0.dll $2/ &&
  cp $1/bin/libcairo-script-interpreter-2.dll $2/ &&
  cp $1/bin/libtiff-6.dll $2/ &&
  cp $1/bin/gdbus.exe $2/ &&
  cp $1/bin/gtk4-update-icon-cache.exe $2/bin/ &&
  cp "${SRC_DIR}/helpers/installer-helper-gtk4.cmd" $2/bin/installer-helper.cmd
}

add_qt6_env() {
  add_glib_env $1 $2 &&
  cp -R $1/qt6/plugins $2/ &&
  cp $1/bin/Qt6Core.dll $2/ &&
  cp $1/bin/Qt6Gui.dll $2/ &&
  cp $1/bin/Qt6Widgets.dll $2/ &&
  cp $1/bin/libpcre2-16-0.dll $2/ &&
  cp $1/bin/libMagickWand-7.Q16HDRI-10.dll $2/ &&
  cp $1/bin/libMagickCore-7.Q16HDRI-10.dll $2/ &&
  mkdir -p $2/bin &&
  cp "${SRC_DIR}/helpers/installer-helper-qt.cmd" $2/bin/installer-helper.cmd
}

add_sdl2_env() {
  cp $1/bin/SDL2_image.dll $2/ &&
  cp $1/bin/SDL2_ttf.dll $2/
}

if test "$1" = "" || test "$1" = "-h" || test "$1" = "--help" ||
   test "$2" = "" ; then
  echo "Usage: $0 <crosser dir> <gui>"
  exit 1
fi

DLLSPATH="$1"
GUI="$2"

case "${GUI}" in
  gtk3.22)
    GUINAME="GTK3.22"
    MPGUI="gtk3"
    FCMP="gtk3" ;;
  gtk4)
    GUINAME="GTK4"
    MPGUI="gtk4"
    FCMP="gtk4" ;;
  gtk4x)
    GUINAME="GTK4x"
    MPGUI="gtk4x"
    FCMP="gtk4x" ;;
  sdl2)
    GUINAME="SDL2"
    FCMP="gtk4" ;;
  qt6)
    GUINAME="Qt6"
    CLIENT="qt"
    MPGUI="qt"
    FCMP="qt" ;;
  qt6x)
    GUINAME="Qt6x"
    CLIENT="qt"
    MPGUI="qt"
    FCMP="qt" ;;
  ruledit)
    ;;
  *)
    echo "Unknown gui type \"${GUI}\"" >&2
    exit 1 ;;
esac

if test "${CLIENT}" = "" ; then
  CLIENT="${GUI}"
fi

if ! test -d "${DLLSPATH}" ; then
  echo "Dllstack directory \"${DLLSPATH}\" not found!" >&2
  exit 1
fi

SRC_DIR="$(cd "$(dirname "$0")" || exit 1 ; pwd)"
SRC_ROOT="$(cd "${SRC_DIR}/../../.." || exit 1 ; pwd)"
BUILD_ROOT="$(pwd)"

if ! "${SRC_DIR}/meson-winbuild.sh" "${DLLSPATH}" "${GUI}" ; then
  exit 1
fi

SETUP=$(grep "CrosserSetup=" "${DLLSPATH}/crosser.txt" | sed -e 's/CrosserSetup="//' -e 's/"//')

VERREV="$("${SRC_ROOT}/fc_version")"

if ! ( cd "${BUILD_ROOT}/meson/build/${SETUP}-${GUI}" && ninja langstat_core.txt ) ; then
  echo "langstat_core.txt creation failed!" >&2
  exit 1
fi

if test "${GUI}" = "ruledit" &&
   ! ( cd "${BUILD_ROOT}/meson/build/${SETUP}-${GUI}" && ninja langstat_ruledit.txt ) ; then
  echo "langstat_ruledit.txt creation failed!" >&2
  exit 1
fi

if test "${INST_CROSS_MODE}" != "release" ; then
  if test -d "${SRC_ROOT}/.git" || test -f "${SRC_ROOT}/.git" ; then
    VERREV="$VERREV-$(cd "${SRC_ROOT}" && git rev-parse --short HEAD)"
  fi
fi

INSTDIR="${BUILD_ROOT}/meson/install/freeciv-${VERREV}-${SETUP}-${GUI}"

if ! mv "${INSTDIR}/bin/"* "${INSTDIR}/" ||
   ! mv "${INSTDIR}/share/freeciv" "${INSTDIR}/data" ||
   ! mv "${INSTDIR}/share/doc" "${INSTDIR}/" ||
   ! mkdir -p "${INSTDIR}/doc/freeciv/installer" ||
   ! cat "${SRC_DIR}/licenses/header.txt" "${SRC_ROOT}/COPYING" \
     > "${INSTDIR}/doc/freeciv/installer/COPYING.installer" ||
   ! cp "${BUILD_ROOT}/meson/build/${SETUP}-${GUI}/langstat_"*.txt \
       "${INSTDIR}/doc/freeciv/installer/" ||
   ! rm -Rf "${INSTDIR}/lib" ||
   ! cp "${SRC_DIR}/Freeciv.url" "${INSTDIR}/"
then
  echo "Rearranging install directory failed!" >&2
  exit 1
fi

if ! add_common_env "${DLLSPATH}" "${INSTDIR}" ; then
  echo "Copying common environment failed!" >&2
  exit 1
fi

NSI_DIR="${BUILD_ROOT}/meson/nsi"

if ! mkdir -p "${NSI_DIR}" ; then
  echo "Creating \"${NSI_DIR}\" directory failed" >&2
  exit 1
fi

if test "${GUI}" = "ruledit" ; then
  if ! cp "${SRC_DIR}/freeciv-ruledit.cmd" "${INSTDIR}/"
  then
    echo "Adding cmd-file failed!" >&2
    exit 1
  fi
  if ! add_qt6_env "${DLLSPATH}" "${INSTDIR}" ; then
    echo "Copying Qt6 environment failed!" >&2
    exit 1
  fi

  NSI_FILE="${NSI_DIR}/ruledit-${SETUP}-${VERREV}.nsi"

  if ! "${SRC_DIR}/create-freeciv-ruledit-nsi.sh" \
         "${INSTDIR}" "${BUILD_ROOT}/meson/output" "${VERREV}" "qt6" "Qt6" "${SETUP}" \
           > "${NSI_FILE}"
  then
    exit 1
  fi
else
  if ! cp "${SRC_DIR}/freeciv-server.cmd" "${SRC_DIR}/freeciv-${CLIENT}.cmd" \
         "${SRC_DIR}/freeciv-mp-${FCMP}.cmd" "${INSTDIR}/"
  then
    echo "Adding cmd-files failed!" >&2
    exit 1
  fi

  if ! add_sdl2_mixer_env "${DLLSPATH}" "${INSTDIR}" ; then
    echo "Copying SDL2_mixer environment failed!" >&2
    exit 1
  fi

  case $GUI in
    gtk3.22)
      if ! add_gtk3_env "${DLLSPATH}" "${INSTDIR}" ; then
        echo "Copying gtk3 environment failed!" >&2
        exit 1
      fi
      ;;
    gtk4|gtk4x)
      if ! add_gtk4_env "${DLLSPATH}" "${INSTDIR}" ; then
        echo "Copying gtk4 environment failed!" >&2
        exit 1
      fi
      ;;
    sdl2)
      # For gtk4 modpack installer
      if ! add_gtk4_env "${DLLSPATH}" "${INSTDIR}" ; then
        echo "Copying gtk4 environment failed!" >&2
        exit 1
      fi
      if ! add_sdl2_env "${DLLSPATH}" "${INSTDIR}" ; then
        echo "Copying sdl2 environment failed!" >&2
        exit 1
      fi
      ;;
    qt6|qt6x)
      if ! cp "${SRC_DIR}/freeciv-ruledit.cmd" "${INSTDIR}"
      then
        echo "Adding cmd-file failed!" >&2
        exit 1
      fi
      if ! add_qt6_env "${DLLSPATH}" "${INSTDIR}" ; then
        echo "Copying Qt6 environment failed!" >&2
        exit 1
      fi
      ;;
  esac

  if test "${GUI}" = "qt6" || test "${GUI}" = "qt6x" ; then
    EXE_ID="qt"
  else
    EXE_ID="${GUI}"
  fi

  if test "$GUI" = "gtk3.22" || test "$GUI" = "gtk4" ||
     test "$GUI" = "gtk4x" || test "$GUI" = "sdl2" ; then
    UNINSTALLER="${SRC_DIR}/helpers/uninstaller-helper-gtk3.sh"
  else
    UNINSTALLER=""
  fi

  NSI_FILE="${NSI_DIR}/client-${SETUP}-${VERREV}-${GUI}.nsi"

  if test "${GUI}" = "sdl2" ; then
    if ! "${SRC_DIR}/create-freeciv-sdl2-nsi.sh" \
           "${INSTDIR}" "${BUILD_ROOT}/meson/output" "${VERREV}" "${SETUP}" "${UNINSTALLER}" \
             > "${NSI_FILE}"
    then
      exit 1
    fi
  else
    if ! "${SRC_DIR}/create-freeciv-gtk-qt-nsi.sh" \
           "${INSTDIR}" "${BUILD_ROOT}/meson/output" "${VERREV}" "${GUI}" "${GUINAME}" \
           "${SETUP}" "${MPGUI}" "${EXE_ID}" "${UNINSTALLER}" \
             > "${NSI_FILE}"
    then
      exit 1
    fi
  fi
fi

if ! mkdir -p "${BUILD_ROOT}/meson/output" ; then
  echo "Creating meson/output directory failed" >&2
  exit 1
fi

if ! makensis -NOCD "${NSI_FILE}"
then
  echo "Creating installer failed!" >&2
  exit 1
fi
