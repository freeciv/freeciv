#!/bin/bash -e
#/***********************************************************************
# Freeciv - Copyright (C) 2022 The Freeciv project
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

if test "$1" = "-h" || test "$1" = "--help" || test "$1" = "" || test "$2" = "" ||
   test "$3" = "" ; then
  echo "Usage: $(basename $0) <target file> <build root directory> <stable/development> [date] [URL]"
  exit
fi

export BDIR="$(cd ${2} | exit 1 ; pwd)"

cd "$(dirname $0)"

FCVER="$(../fc_version)"

if test "$3" != "stable" && test "$3" != "development" ; then
  echo "Unknown release type \"$3\"" >&2
  exit 1
fi
RELTYPE="$3"

if test "$4" != "" ; then
  if test "$5" != "" ; then
    RELEASETAG="<release version=\"${FCVER}\" type=\"${RELTYPE}\" date=\"$4\"><url>$5</url></release>"
  else
    RELEASETAG="<release version=\"${FCVER}\" type=\"${RELTYPE}\" date=\"$4\" />"
  fi
else
  # Tag with no date causes format checker warning, but
  # flatpak builder outright errors if there's no <release> tag
  # at all within <releases> ... </releases> list
  if test "$5" = "" ; then
    RELEASETAG="<release version=\"${FCVER}\" type=\"${RELTYPE}\" />"
  else
    RELEASETAG="<release version=\"${FCVER}\" type=\"${RELTYPE}\"><url>$5</url></release>"
  fi
fi

sed -e "s,\[release\],${RELEASETAG}," "${1}.in" > "${BDIR}/${1}"
