#!/bin/sh
#/***********************************************************************
# Freeciv - Copyright (C) 2026
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

srcdir="$(dirname "$0")"
top_srcdir="$(cd "$srcdir/.." && pwd)"

if test "$1" = "-h" || test "$1" = "--help" ; then
  echo "Usage: $0 [bin dir]"
  exit
fi

if test "$1" != "" ; then
  bindir="$1"
  if ! test -d "$1" ; then
    echo "bin dir \"$1\" does not exist!" >&2
    exit 1
  fi
fi

if ! meson setup -Dserver=disabled -Dclients=[] -Dfcmp=[] -Dtools=[] \
     -Daudio=none "${top_srcdir}" ||
   ! ninja ; then
  echo "Tolua build failed!" >&2
  exit 1
fi

if test "${bindir}" != "" ; then
  if ! cp tolua "${bindir}/" ; then
    echo "Copying tolua to \"${bindir}\" failed!" >&2
    exit 1
  fi
  echo "tolua is at \"${bindir}\""
else
  echo "tolua is at \"$(pwd)\""
fi
