#!/bin/bash
#/***********************************************************************
# Freeciv - Copyright (C) 2022
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

if ! test -f ChangeLog ; then
  echo "This directory has no ChangeLog file!" >&2
  exit 1
fi

if ! test -e .git ; then
  echo "This does not look like a git repo!" >&2
  exit 1
fi

OLDREV="$(head -n 1 ChangeLog | sed 's/.* //')"

git log --no-decorate --pretty=medium ${OLDREV}..HEAD > ChangeLog.new
echo >> ChangeLog.new
cat ChangeLog >> ChangeLog.new
mv ChangeLog.new ChangeLog
