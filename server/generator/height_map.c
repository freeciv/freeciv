/********************************************************************** 
 Freeciv - Copyright (C) 2004 - Marcelo J. Burda
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "shared.h"

#include "height_map.h"

int *height_map = NULL;
int hmap_shore_level = 0, hmap_mountain_level = 0;
