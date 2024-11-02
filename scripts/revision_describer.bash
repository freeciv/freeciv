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

if test "$1" = "" ; then
  REVISION="HEAD"
else
  REVISION="$1"
fi

if test "$2" = "" ; then
  ONLY_CARE_ABOUT=
else
  ONLY_CARE_ABOUT="-- $2"
fi

FORMAT="%h %cI!%ce %s"
FORMAT="${FORMAT}%n%n"
FORMAT="${FORMAT}commit: %H%n"
FORMAT="${FORMAT}action stamp: %cI!%ce%n"
FORMAT="${FORMAT}author: %aN <%aE>"
FORMAT="${FORMAT} %ai%n"
FORMAT="${FORMAT}committer: %cN <%cE>"
FORMAT="${FORMAT} %ci%n"
FORMAT="${FORMAT}commit message:%n"
FORMAT="${FORMAT}%w(0, 2, 2)%B"


git log \
 --format="${FORMAT}" \
 "${REVISION}" -1 \
 ${ONLY_CARE_ABOUT}
