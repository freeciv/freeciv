#!/usr/bin/env bash

# Freeciv - Copyright (C) 2022-2023 The Freeciv Team
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2, or (at your option)
#  any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.

if test "$1" = "" || test "$2" = "" ; then
  echo "Needs freeciv source and build directories as parameters!" >&2
  exit 1
fi

if test "$1" = "-h" || test "$1" = "--help" ; then
  echo "Usage: $0 <freeciv source directory> <freeciv build directory>"
  exit
fi

if ! test -x "$1/fc_version" ; then
  echo "\"$1\" doesn't look like a freeciv source directory (lacking fc_version)" >&2
  exit 1
fi

if ! test -f "$2/version_gen.h" ; then
  echo "\"$2\" is not a build directory with generated files (lacking version_gen.h)" >&2
  exit 1
fi

if ! mkdir -p doc ; then
  echo "Failed to create output doc directory!" >&2
  exit 1
fi

VERSION_SCRIPT_SILENT=yes . "$1/fc_version"

doxy_srcdir="$1/" doxy_builddir="$2" doxy_version="-${MAIN_VERSION}" \
           doxygen "$1/doc/freeciv.doxygen"
