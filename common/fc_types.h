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
 * definition in another file.
 *
 * Nothing in this file should require anything else from the common/
 * directory! */

enum output_type {
  O_FOOD, O_SHIELD, O_TRADE, O_GOLD, O_LUXURY, O_SCIENCE, O_LAST
};
#define O_COUNT num_output_types
#define O_MAX O_LAST /* Changing this breaks network compatibility. */

typedef signed short Continent_id;
typedef int Terrain_type_id;
typedef int Specialist_type_id;
typedef int Impr_Type_id;
typedef enum output_type Output_type_id;

struct city;
struct government;
struct player;
struct tile;
struct unit;

#define SP_MAX 20

#endif /* FC__FC_TYPES_H */
