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

srcdir="$(dirname "$0")"
top_srcdir="$(cd "$srcdir/.." && pwd)"

run_test() {
  echo "Running $1"
  ${srcdir}/$1 ${top_srcdir} >> check-output_
}

rm -f check-output check-output_

run_test check_macros.sh
run_test copyright.sh
run_test fcintl.sh
run_test header_guard.sh
run_test va_list.sh
run_test trailing_spaces.sh

echo "Preparing check-output"
cat check-output_ | sed "s,${top_srcdir}/,," > check-output
rm -f check-output_
