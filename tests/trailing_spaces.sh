#!/bin/sh

#/***********************************************************************
# Freeciv - Copyright (C) 2022-2025
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

files=$(find $1 -name "*.ruleset" \
             -o -name "*.tileset" \
             -o -name "*.spec" \
             -o -name "*.lua" \
             -o -name "*.modpack" \
             -o -name "*.serv" \
             -o -name "*.txt" \
             -o -name "README*" \
             -o -name "*.xml" \
             -o -name "*.cpp" \
             -o -name "*.h" \
             -o -name "*.pkg" \
            | sort)

echo "# Check for trailing spaces:"
for file in $files; do
  COUNT=$(cat $file | grep "[ 	]$" | wc -l)
  if [ "$COUNT" != "0" ]; then
    echo $file
  fi
done
echo
