/**********************************************************************
 Freeciv - Copyright (C) 2004 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifndef FC__FC_TYPES_H
#define FC__FC_TYPES_H

/* This file serves to reduce the cross-inclusion of header files which
 * occurs when a type which is defined in one file is needed for a fuction
 * definition in another file */

typedef signed short Continent_id;
typedef int Terrain_type_id;
typedef enum specialist_type Specialist_type_id;
typedef int Impr_Type_id;

struct city;
struct government;
struct player;
struct tile;
struct unit;

#endif /* FC__FC_TYPES_H */
