#!/bin/sh
#/***********************************************************************
# Freeciv - Copyright (C) 2004-2025
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

files=`find $1 -name "*.c" -o -name "*.h" -o -name "*.cpp" \
       | sort \
       | grep -v "Freeciv.h" \
       | fgrep -v "_gen." \
       | grep -v "fc_config.h" \
       | grep -v dependencies \
       | grep -v SDL3_rotozoom \
       | grep -v utility/md5\.. `

echo "# No Freeciv Copyright:"
echo "# Excludes: generated files, various 3rd party sources"
for file in $files; do
    grep "Freeciv - Copyright" $file >/dev/null || echo $file
done
echo

echo "# No or other GPL:"
for file in $files; do
    grep "GNU General Public License" $file >/dev/null || echo $file
done
echo
