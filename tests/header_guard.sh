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

files=`find $1 -name "*.h" \
       | sort \
       | grep -v utility/spec \
       | grep -v \./common/packets_gen\.h \
       | grep -v tolua_.*_gen\.h \
       | grep -v fc_config\.h \
       | grep -v dependencies \
       | grep -v SDL3_rotozoom\.h \
       | grep -v utility/md5\.h `

echo "# Header files without any include guard:"
for file in $files; do
    grep "^#ifndef FC_.*_H$" $file >/dev/null \
     || grep ifndef $file >/dev/null \
     || echo $file
done
echo

echo "# Header files with a misnamed guard (doesn't match 'FC_.*_H'):"
for file in $files; do
    grep "^#ifndef FC_.*_H$" $file >/dev/null \
     || (grep ifndef $file >/dev/null \
         && grep ifndef $file /dev/null | head -n1)
done
echo
