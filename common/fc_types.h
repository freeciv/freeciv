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

#include "bitvector.h"
#include "shared.h"

/* This file serves to reduce the cross-inclusion of header files which
 * occurs when a type which is defined in one file is needed for a function
 * definition in another file.
 *
 * Nothing in this file should require anything else from the common/
 * directory! */

#define MAX_NUM_PLAYER_SLOTS 128
#define MAX_NUM_BARBARIANS   2
#define MAX_NUM_PLAYERS      MAX_NUM_PLAYER_SLOTS - MAX_NUM_BARBARIANS
#define MAX_NUM_CONNECTIONS (2 * (MAX_NUM_PLAYER_SLOTS))
#define MAX_NUM_ITEMS   200     /* eg, unit_types */
#define MAX_NUM_TECH_LIST 10
#define MAX_NUM_UNIT_LIST 10
#define MAX_NUM_BUILDING_LIST 10
#define MAX_LEN_VET_SHORT_NAME 8
#define MAX_VET_LEVELS 10
#define MAX_BASE_TYPES 32
#define MAX_NUM_USER_UNIT_FLAGS 4
#define MAX_NUM_LEADERS MAX_NUM_ITEMS
#define MAX_NUM_NATION_GROUPS 128
#define MAX_NUM_STARTPOS_NATIONS 1024

/* Changing these will probably break network compatability. */
#define MAX_LEN_NAME     48
#define MAX_LEN_DEMOGRAPHY 16
#define MAX_LEN_ALLOW_TAKE 16
#define MAX_LEN_GAME_IDENTIFIER 33
#define MAX_GRANARY_INIS 24
#define MAX_LEN_STARTUNIT (20+1)
#define MAX_LEN_ENUM     64

/* Line breaks after this number of characters; be carefull and use only 70 */
#define LINE_BREAK 70

/* symbol to flag missing numbers for better debugging */
#define IDENTITY_NUMBER_ZERO (0)

/* A bitvector for all player slots. */
BV_DEFINE(bv_player, MAX_NUM_PLAYER_SLOTS);

/* Changing this breaks network compatibility. */
enum output_type_id {
  O_FOOD, O_SHIELD, O_TRADE, O_GOLD, O_LUXURY, O_SCIENCE, O_LAST
};

/* Changing this enum will break savegame and network compatability. */
enum unit_activity {
  ACTIVITY_IDLE = 0,
  ACTIVITY_POLLUTION = 1,
  ACTIVITY_ROAD = 2,
  ACTIVITY_MINE = 3,
  ACTIVITY_IRRIGATE = 4,
  ACTIVITY_FORTIFIED = 5,
  ACTIVITY_FORTRESS = 6,
  ACTIVITY_SENTRY = 7,
  ACTIVITY_RAILROAD = 8,
  ACTIVITY_PILLAGE = 9,
  ACTIVITY_GOTO = 10,
  ACTIVITY_EXPLORE = 11,
  ACTIVITY_TRANSFORM = 12,
  ACTIVITY_UNKNOWN = 13,		/* savegame compatability. */
  ACTIVITY_AIRBASE = 14,
  ACTIVITY_FORTIFYING = 15,
  ACTIVITY_FALLOUT = 16,
  ACTIVITY_PATROL_UNUSED = 17,		/* savegame compatability. */
  ACTIVITY_BASE = 18,			/* building base */
  ACTIVITY_LAST   /* leave this one last */
};

typedef signed short Continent_id;
typedef int Terrain_type_id;
typedef int Resource_type_id;
typedef int Specialist_type_id;
typedef int Impr_type_id;
typedef int Tech_type_id;
typedef enum output_type_id Output_type_id;
typedef enum unit_activity Activity_type_id;
typedef int Nation_type_id;
typedef int Unit_type_id;
typedef int Base_type_id;

struct advance;
struct city;
struct connection;
struct government;
struct impr_type;
struct nation_type;
struct output_type;
struct player;
struct specialist;
struct terrain;
struct tile;
struct unit;


/* Changing these will break network compatibility. */
#define SP_MAX 20
#define MAX_NUM_REQS 10

#define MAX_NUM_RULESETS 16
#define MAX_RULESET_NAME_LENGTH 64
#define RULESET_SUFFIX ".serv"

/* Unit Class List, also 32-bit vector? */
#define UCL_LAST 32
typedef int Unit_Class_id;

/* This has to be put here for now, otherwise movement.h and unittype.h
 * would have a recursive dependency.
 * Order must mach order in move_type_names array. */
enum unit_move_type {
  LAND_MOVING = 0,
  SEA_MOVING,
  BOTH_MOVING,
  MOVETYPE_LAST
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
enum ai_level {
  AI_LEVEL_AWAY         = 1,
  AI_LEVEL_NOVICE       = 2,
  AI_LEVEL_EASY         = 3,
  AI_LEVEL_NORMAL       = 5,
  AI_LEVEL_HARD         = 7,
  AI_LEVEL_CHEATING     = 8,
  AI_LEVEL_EXPERIMENTAL = 10,
  AI_LEVEL_LAST
};

#define AI_LEVEL_DEFAULT AI_LEVEL_NOVICE

/*
 * pplayer->ai.barbarian_type and nations use this enum. Note that the values
 * have to stay since they are used in savegames.
 */
enum barbarian_type {
  NOT_A_BARBARIAN = 0,
  LAND_BARBARIAN = 1,
  SEA_BARBARIAN = 2
};

/*
 * Citytile requirement types. 
 */
enum citytile_type {
  CITYT_CENTER,
  CITYT_LAST
};

/* Sometimes we don't know (or don't care) if some requirements for effect
 * are currently fulfilled or not. This enum tells lower level functions
 * how to handle uncertain requirements.
 */
enum req_problem_type {
  RPT_POSSIBLE, /* We want to know if it is possible that effect is active */
  RPT_CERTAIN   /* We want to know if it is certain that effect is active  */
};

#define REVERSED_RPT(x) \
  (x == RPT_CERTAIN ? RPT_POSSIBLE : RPT_CERTAIN)

/* Originally in requirements.h, bumped up and revised to unify with
 * city_production and worklists.  Functions remain in requirements.c
 */
typedef union {
  struct advance *advance;
  struct government *govern;
  struct impr_type *building;
  struct nation_type *nation;
  struct specialist *specialist;
  struct terrain *terrain;
  struct unit_class *uclass;
  struct unit_type *utype;
  struct base_type *base;

  enum ai_level ai_level;
  enum citytile_type citytile;
  int minsize;
  int minyear;
  Output_type_id outputtype;
  int terrainclass;			/* enum terrain_class */
  int terrainalter;                     /* enum terrain_alteration */
  int special;				/* enum tile_special_type */
  int unitclassflag;			/* enum unit_class_flag_id */
  int unitflag;				/* enum unit_flag_id */
} universals_u;

/* The kind of universals_u (value_union_type was req_source_type). */
#define SPECENUM_NAME universals_n
#define SPECENUM_VALUE0 VUT_NONE
#define SPECENUM_VALUE0NAME "None"
#define SPECENUM_VALUE1 VUT_ADVANCE
#define SPECENUM_VALUE1NAME "Tech"
#define SPECENUM_VALUE2 VUT_GOVERNMENT
#define SPECENUM_VALUE2NAME "Gov"
#define SPECENUM_VALUE3 VUT_IMPROVEMENT
#define SPECENUM_VALUE3NAME "Building"
#define SPECENUM_VALUE4 VUT_SPECIAL
#define SPECENUM_VALUE4NAME "Special"
#define SPECENUM_VALUE5 VUT_TERRAIN
#define SPECENUM_VALUE5NAME "Terrain"
#define SPECENUM_VALUE6 VUT_NATION
#define SPECENUM_VALUE6NAME "Nation"
#define SPECENUM_VALUE7 VUT_UTYPE
#define SPECENUM_VALUE7NAME "UnitType"
#define SPECENUM_VALUE8 VUT_UTFLAG
#define SPECENUM_VALUE8NAME "UnitFlag"
#define SPECENUM_VALUE9 VUT_UCLASS
#define SPECENUM_VALUE9NAME "UnitClass"
#define SPECENUM_VALUE10 VUT_UCFLAG
#define SPECENUM_VALUE10NAME "UnitClassFlag"
#define SPECENUM_VALUE11 VUT_OTYPE
#define SPECENUM_VALUE11NAME "OutputType"
#define SPECENUM_VALUE12 VUT_SPECIALIST
#define SPECENUM_VALUE12NAME "Specialist"
/* Minimum size: at city range means city size */
#define SPECENUM_VALUE13 VUT_MINSIZE
#define SPECENUM_VALUE13NAME "MinSize"
/* AI level of the player */
#define SPECENUM_VALUE14 VUT_AI_LEVEL
#define SPECENUM_VALUE14NAME "AI"
/* More generic terrain type currently "Land" or "Ocean" */
#define SPECENUM_VALUE15 VUT_TERRAINCLASS
#define SPECENUM_VALUE15NAME "TerrainClass"
#define SPECENUM_VALUE16 VUT_BASE
#define SPECENUM_VALUE16NAME "Base"
#define SPECENUM_VALUE17 VUT_MINYEAR
#define SPECENUM_VALUE17NAME "MinYear"
/* Terrain alterations that are possible */
#define SPECENUM_VALUE18 VUT_TERRAINALTER
#define SPECENUM_VALUE18NAME "TerrainAlter"
/* Target tile is used by city. */
#define SPECENUM_VALUE19 VUT_CITYTILE
#define SPECENUM_VALUE19NAME "CityTile"
/* Keep this last. */
#define SPECENUM_COUNT VUT_COUNT
#include "specenum_gen.h"

struct universal {
  universals_u value;
  enum universals_n kind;		/* formerly .type and .is_unit */
};

struct ai_choice;			/* incorporates universals_u */

BV_DEFINE(bv_bases, MAX_BASE_TYPES);
BV_DEFINE(bv_startpos_nations, MAX_NUM_STARTPOS_NATIONS);

enum gui_type {
  GUI_STUB,
  GUI_GTK2,
  GUI_SDL,
  GUI_XAW,
  GUI_WIN32,
  GUI_FTWL,
  GUI_LAST
};

#define SPECENUM_NAME airlifting_style
#define SPECENUM_BITWISE
/* Like classical Freeciv.  One unit per turn. */
#define SPECENUM_ZERO   AIRLIFTING_CLASSICAL
/* Allow airlifting from allied cities. */
#define SPECENUM_VALUE0 AIRLIFTING_ALLIED_SRC
/* Allow airlifting to allied cities. */
#define SPECENUM_VALUE1 AIRLIFTING_ALLIED_DEST
/* Unlimited units to airlift from the source (but always needs an Airport
 * or equivalent). */
#define SPECENUM_VALUE2 AIRLIFTING_UNLIMITED_SRC
/* Unlimited units to airlift to the destination (doesn't require any
 * Airport or equivalent). */
#define SPECENUM_VALUE3 AIRLIFTING_UNLIMITED_DEST
#include "specenum_gen.h"

enum phase_mode_types {
  PMT_CONCURRENT = 0,
  PMT_PLAYERS_ALTERNATE,
  PMT_TEAMS_ALTERNATE
};

enum borders_mode {
  BORDERS_DISABLED = 0,
  BORDERS_ENABLED,
  BORDERS_SEE_INSIDE,
  BORDERS_EXPAND,
};

enum diplomacy_mode {
  DIPLO_FOR_ALL,
  DIPLO_FOR_HUMANS,
  DIPLO_FOR_AIS,
  DIPLO_FOR_TEAMS,
  DIPLO_DISABLED,
};

#endif /* FC__FC_TYPES_H */
