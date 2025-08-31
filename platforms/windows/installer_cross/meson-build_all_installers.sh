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

if test "$1" = "" || test "$1" = "-h" || test "$1" = "--help" ; then
  USAGE_NEEDED=yes
fi

if test "$2" != "snapshot" && test "$2" != "release" ; then
  USAGE_NEEDED=yes
fi

if test "${USAGE_NEEDED}" = "yes" ; then
  echo "Usage: $0 <crosser dir> <snapshot|release>"
  exit 1
fi

DLLSPATH="$1"
export INST_CROSS_MODE="$2"

if ! test -d "${DLLSPATH}" ; then
  echo "Dllstack directory \"${DLLSPATH}\" not found!" >&2
  exit 1
fi

if ! test -f "${DLLSPATH}/crosser.txt" ; then
  echo "Directory \"${DLLSPATH}\" does not look like crosser environment!" >&2
  exit 1
fi

RET=0

if grep "CROSSER_GTK4" "${DLLSPATH}/crosser.txt" | grep yes > /dev/null
then
  CROSSER_GTK4=yes
fi

if grep "CROSSER_QT6" "${DLLSPATH}/crosser.txt" | grep yes > /dev/null
then
  CROSSER_QT6=yes
fi

if ! ./meson-installer_build.sh "${DLLSPATH}" gtk3.22 ; then
  RET=1
  GTK322="Fail"
else
  GTK322="Success"
fi

if test "${CROSSER_GTK4}" != "yes" ; then
  GTK4="N/A"
elif ! ./meson-installer_build.sh "${DLLSPATH}" gtk4 ; then
  RET=1
  GTK4="Fail"
else
  GTK4="Success"
fi

if test "${CROSSER_QT6}" != "yes" ; then
  QT6="N/A"
elif ! ./meson-installer_build.sh "${DLLSPATH}" qt6 ; then
  RET=1
  QT6="Fail"
else
  QT6="Success"
fi

if test "${CROSSER_QT6}" != "yes" ; then
  QT7="N/A"
elif ! ./meson-installer_build.sh "${DLLSPATH}" qt6x ; then
  RET=1
  QT7="Fail"
else
  QT7="Success"
fi

# sdl2-client comes with gtk4 modpack installer
if test "${CROSSER_GTK4}" != "yes" ; then
  SDL2="N/A"
elif ! ./meson-installer_build.sh "${DLLSPATH}" sdl2 ; then
  RET=1
  SDL2="Fail"
else
  SDL2="Success"
fi

if test "${CROSSER_QT6}" != "yes" ; then
  RULEDIT="N/A"
elif ! ./meson-installer_build.sh "${DLLSPATH}" ruledit ; then
  RET=1
  RULEDIT="Fail"
else
  RULEDIT="Success"
fi

echo "Gtk3.22: ${GTK322}"
echo "Gtk4:    ${GTK4}"
echo "Qt6:     ${QT6}"
echo "Qt6x:    ${QT7}"
echo "Sdl2:    ${SDL2}"
echo "Ruledit: ${RULEDIT}"

exit ${RET}
