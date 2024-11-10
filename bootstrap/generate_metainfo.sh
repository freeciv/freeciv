#!/usr/bin/env bash
#/***********************************************************************
# Freeciv - Copyright (C) 2022-2023 The Freeciv project
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

if test "$1" = "-h" || test "$1" = "--help" || test "$1" = "" || test "$2" = "" ; then
  echo "Usage: $(basename $0) <target file> <build root directory>"
  exit
fi

export BDIR="$(cd ${2} | exit 1 ; pwd)"

cd "$(dirname $0)"

VERSION_SCRIPT_SILENT=yes . ../fc_version

FCVER="${VERSION_STRING}${VERSION_REV}"

if test "${RELEASE_TYPE}" != "stable" && test "${RELEASE_TYPE}" != "development" ; then
  echo "Unknown release type \"${RELEASE_TYPE}\"" >&2
  exit 1
fi

if test "${RELEASE_DATE}" != "" ; then
  if test "${NEWS_URL}" != "" ; then
    RELEASETAG="<release version=\"${FCVER}\" type=\"${RELEASE_TYPE}\" date=\"${RELEASE_DATE}\"><url>${NEWS_URL}</url></release>"
  else
    RELEASETAG="<release version=\"${FCVER}\" type=\"${RELEASE_TYPE}\" date=\"${RELEASE_DATE}\" />"
  fi
else
  # Tag with no date causes format checker warning, but
  # flatpak builder outright errors if there's no <release> tag
  # at all within <releases> ... </releases> list
  if test "${NEWS_URL}" = "" ; then
    RELEASETAG="<release version=\"${FCVER}\" type=\"${RELEASE_TYPE}\" />"
  else
    RELEASETAG="<release version=\"${FCVER}\" type=\"${RELEASE_TYPE}\"><url>${NEWS_URL}</url></release>"
  fi
fi

sed -e "s,\[release\],${RELEASETAG}," "${1}.in" > "${BDIR}/${1}"
