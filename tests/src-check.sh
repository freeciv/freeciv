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

rm -f check-output check-output_
${srcdir}/check_macros.sh ${top_srcdir} >> check-output_
${srcdir}/copyright.sh ${top_srcdir} >> check-output_
${srcdir}/fcintl.sh ${top_srcdir} >> check-output_
${srcdir}/header_guard.sh ${top_srcdir} >> check-output_
${srcdir}/va_list.sh ${top_srcdir} >> check-output_
${srcdir}/trailing_spaces.sh ${top_srcdir} >> check-output_
cat check-output_ | sed "s,${top_srcdir}/,," > check-output
rm -f check-output_
