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

#include "shared.h" /* For bool typedef */

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

/* Changing these will probably break network compatability. */
#define MAX_LEN_NAME     32
#define MAX_LEN_DEMOGRAPHY 16
#define MAX_LEN_ALLOW_TAKE 16
#define MAX_ID_LEN 33
#define MAX_GRANARY_INIS 24
#define MAX_LEN_STARTUNIT (20+1)

/* Server setting types.  Changing these will break network compatability. */
enum sset_type {
  SSET_BOOL, SSET_INT, SSET_STRING
};

/* The following classes determine what can be changed when.
 * Actually, some of them have the same "changeability", but
 * different types are separated here in case they have
 * other uses.
 * Also, SSET_GAME_INIT/SSET_RULES separate the two sections
 * of server settings sent to the client.
 * See the settings[] array for what these correspond to and
 * explanations.
 */
enum sset_class {
  SSET_MAP_SIZE,
  SSET_MAP_GEN,
  SSET_MAP_ADD,
  SSET_PLAYERS,
  SSET_GAME_INIT,
  SSET_RULES,
  SSET_RULES_FLEXIBLE,
  SSET_META,
  SSET_LAST
};

/* Invariants: V_MAIN vision ranges must always be more than V_INVIS
 * ranges. */
enum vision_layer {
  V_MAIN,
  V_INVIS,
  V_COUNT
};

enum output_type_id {
  O_FOOD, O_SHIELD, O_TRADE, O_GOLD, O_LUXURY, O_SCIENCE, O_LAST
};
#define O_COUNT num_output_types
#define O_MAX O_LAST /* Changing this breaks network compatibility. */

struct city_production {
  bool is_unit;
  int value; /* Unit_type_id or Impr_type_id */
};

/* Some integers here could be uint8 values perhaps, but this is probably
 * not worth it.  Storage space is not an issue for these types. */
typedef signed short Continent_id;
typedef int Terrain_type_id;
typedef int Resource_type_id;
typedef int Specialist_type_id;
typedef int Impr_type_id;
typedef enum output_type_id Output_type_id;
typedef enum unit_activity Activity_type_id;
typedef int Nation_type_id;
typedef int Team_type_id;
typedef int Unit_type_id;

struct city;
struct connection;
struct government;
struct nation_type;
struct player;
struct terrain;
struct tile;
struct unit;
struct impr_type;
struct output_type;
struct specialist;

/* Changing these will break network compatibility. */
#define SP_MAX 20
#define MAX_NUM_REQS 4

#define MAX_NUM_RULESETS 16
#define MAX_RULESET_NAME_LENGTH 64
#define RULESET_SUFFIX ".serv"

#define UCL_LAST 32

typedef int Unit_Class_id;

/* This has to be put here for now, otherwise movement.h and unittype.h
 * would have a recursive dependency.
 * Order must mach order in move_type_names array. */
enum unit_move_type {
  AIR_MOVING = 0,
  LAND_MOVING = 1,
  SEA_MOVING = 2,
  HELI_MOVING = 3,
  MOVETYPE_LAST = 4
};

/* The direction8 gives the 8 possible directions.  These may be used in
 * a number of ways, for instance as an index into the DIR_DX/DIR_DY
 * arrays.  Not all directions may be valid; see is_valid_dir and
 * is_cardinal_dir. */
enum direction8 {
  /* The DIR8/direction8 naming system is used to avoid conflict with
   * DIR4/direction4 in client/tilespec.h
   *
   * Changing the order of the directions will break network compatability.
   *
   * Some code assumes that the first 4 directions are the reverses of the
   * last 4 (in no particular order).  See client/goto.c. */
  DIR8_NORTHWEST = 0,
  DIR8_NORTH = 1,
  DIR8_NORTHEAST = 2,
  DIR8_WEST = 3,
  DIR8_EAST = 4,
  DIR8_SOUTHWEST = 5,
  DIR8_SOUTH = 6,
  DIR8_SOUTHEAST = 7
};
#define DIR8_LAST 8
#define DIR8_COUNT DIR8_LAST

/* AI levels. This must correspond to ai_level_names[] in player.c */
enum ai_level { AI_LEVEL_AWAY         = 1,
                AI_LEVEL_NOVICE       = 2,
                AI_LEVEL_EASY         = 3,
                AI_LEVEL_NORMAL       = 5,
                AI_LEVEL_HARD         = 7,
                AI_LEVEL_CHEATING     = 8,
                AI_LEVEL_EXPERIMENTAL = 10,
                AI_LEVEL_LAST};

#define AI_LEVEL_DEFAULT AI_LEVEL_NOVICE

enum editor_vision_mode {
  EVISION_ADD,
  EVISION_REMOVE,
  EVISION_TOGGLE,
  EVISION_LAST
};

enum editor_tech_mode {
  ETECH_ADD,
  ETECH_REMOVE,
  ETECH_TOGGLE,
  ETECH_LAST
};

#endif /* FC__FC_TYPES_H */
