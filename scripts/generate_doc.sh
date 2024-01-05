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

. $1/fc_version

doxy_srcdir="$1/" doxy_version="-${MAIN_VERSION}" doxygen "$1/doc/freeciv.doxygen"
