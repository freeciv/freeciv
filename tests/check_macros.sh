#!/bin/sh
#/***********************************************************************
# Freeciv - Copyright (C) 2011-2025
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

files=`find $1 -name "*.c" -o -name "*.cpp" -o -name "*.h" | sort`

echo "# check for __LINE__ (should be replaced by __FC_LINE__):"
for file in $files; do
  COUNT=`cat $file | grep _LINE__ | grep -v __FC_LINE__ | wc -l`
  if [ "${COUNT}" != "0" ]; then
    echo $file
  fi
done
echo
