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

is_verbose=false
non_quiet=true

if [ "$1" = "-v" ]; then
  is_verbose=true
elif [ "$1" = "-q" ]; then
  non_quiet=false
fi

errors=0

data_dir="`dirname $0`/../data"
flags_dir="$data_dir/flags"
flags_makefile="$data_dir/flags/Makefile.am"

for nation in `find $data_dir/nation -name '*.ruleset'`; do
  $is_verbose && echo $nation
  flag=`grep -P "^flag\s*=\s*" "$nation" | sed -re 's/flag\s*=\s*\"(.*)\".*$/\1/'`
  $is_verbose && echo "flag='$flag'"
  for suf in ".png" "-large.png" "-shield.png" "-shield-large.png" ".svg"; do
    if [ -f "$flags_dir/$flag$suf" ]; then
      grep -q "[[:space:]]$flag$suf" "$flags_makefile"
      if [ $? -ne 0 ]; then
	$non_quiet && echo "....$flag$suf is missing in $flags_makefile"
	let 'errors += 1'
      fi
    else
      $non_quiet && echo "..$flag$suf is missing"
      let 'errors += 1'
    fi
  done
done

$non_quiet && echo "Total errors: $errors"

if [ $errors -gt 0 ]; then
  exit 3
else
  exit 0
fi

