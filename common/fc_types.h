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

#define BUG_EMAIL_ADDRESS "bugs@freeciv.org"
#define WEBSITE_URL "http://www.freeciv.org/"

/* MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS <= 32 !!!! */
#define MAX_NUM_PLAYERS  30
#define MAX_NUM_BARBARIANS   2
#define MAX_NUM_CONNECTIONS (2 * (MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS))
#define MAX_NUM_ITEMS   200     /* eg, unit_types */
#define MAX_NUM_TECH_LIST 10
#define MAX_NUM_UNIT_LIST 10
#define MAX_NUM_BUILDING_LIST 10
#define MAX_LEN_VET_SHORT_NAME 8
#define MAX_VET_LEVELS 10

enum output_type_id {
  O_FOOD, O_SHIELD, O_TRADE, O_GOLD, O_LUXURY, O_SCIENCE, O_LAST
};
#define O_COUNT num_output_types
#define O_MAX O_LAST /* Changing this breaks network compatibility. */

typedef signed short Continent_id;
typedef int Terrain_type_id;
typedef int Specialist_type_id;
typedef int Impr_type_id;
typedef enum output_type_id Output_type_id;
typedef enum unit_activity Activity_type_id;
typedef int Nation_type_id;
typedef int Team_type_id;

struct city;
struct government;
struct nation_type;
struct player;
struct tile;
struct unit;
struct impr_type;
struct output_type;

/* Changing these will break network compatibility. */
#define SP_MAX 20
#define MAX_NUM_REQS 4

#define MAX_NUM_RULESETS 16
#define MAX_RULESET_NAME_LENGTH 64
#define RULESET_SUFFIX ".serv"

/* This has to be put here for now, otherwise movement.h and unittype.h
 * would have a recursive dependency. */
enum unit_move_type {
  LAND_MOVING = 1,
  SEA_MOVING,
  HELI_MOVING,
  AIR_MOVING
};

#endif /* FC__FC_TYPES_H */
