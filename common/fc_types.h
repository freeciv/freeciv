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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "bitvector.h"
#include "shared.h"

/* This file serves to reduce the cross-inclusion of header files which
 * occurs when a type which is defined in one file is needed for a function
 * definition in another file.
 *
 * Nothing in this file should require anything else from the common/
 * directory! */

enum eroad { ROAD_ROAD = 0, ROAD_RAILROAD, ROAD_LAST };

#define MAX_NUM_PLAYER_SLOTS 128
#define MAX_NUM_BARBARIANS   2
#define MAX_NUM_PLAYERS      MAX_NUM_PLAYER_SLOTS - MAX_NUM_BARBARIANS
#define MAX_NUM_CONNECTIONS (2 * (MAX_NUM_PLAYER_SLOTS))
#define MAX_NUM_ITEMS   200     /* eg, unit_types */
#define MAX_NUM_TECH_LIST 10
#define MAX_NUM_UNIT_LIST 10
#define MAX_NUM_BUILDING_LIST 10
#define MAX_LEN_VET_SHORT_NAME 8
#define MAX_VET_LEVELS 20 /* see diplomat_success_vs_defender() */
#define MAX_BASE_TYPES 32
#define MAX_ROAD_TYPES ROAD_LAST /* Cannot be changed without major code changes */
#define MAX_DISASTER_TYPES 10
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
  ACTIVITY_GEN_ROAD = 19,
  ACTIVITY_CONVERT = 20,
  ACTIVITY_LAST   /* leave this one last */
};

enum adv_unit_task { AUT_NONE, AUT_AUTO_SETTLER, AUT_BUILD_CITY };

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
typedef int Road_type_id;
typedef int Disaster_type_id;
typedef unsigned char citizens;

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
 * would have a recursive dependency. */
#define SPECENUM_NAME unit_move_type
#define SPECENUM_VALUE0 UMT_LAND
#define SPECENUM_VALUE0NAME "Land"
#define SPECENUM_VALUE1 UMT_SEA
#define SPECENUM_VALUE1NAME "Sea"
#define SPECENUM_VALUE2 UMT_BOTH
#define SPECENUM_VALUE2NAME "Both"
#include "specenum_gen.h"


/* The direction8 gives the 8 possible directions.  These may be used in
 * a number of ways, for instance as an index into the DIR_DX/DIR_DY
 * arrays.  Not all directions may be valid; see is_valid_dir and
 * is_cardinal_dir. */

/* The DIR8/direction8 naming system is used to avoid conflict with
 * DIR4/direction4 in client/tilespec.h
 *
 * Changing the order of the directions will break network compatability.
 *
 * Some code assumes that the first 4 directions are the reverses of the
 * last 4 (in no particular order).  See client/goto.c and
 * map.c:opposite_direction(). */

#define SPECENUM_NAME direction8
#define SPECENUM_VALUE0 DIR8_NORTHWEST
#define SPECENUM_VALUE0NAME "Northwest"
#define SPECENUM_VALUE1 DIR8_NORTH
#define SPECENUM_VALUE1NAME "North"
#define SPECENUM_VALUE2 DIR8_NORTHEAST
#define SPECENUM_VALUE2NAME "Northeast"
#define SPECENUM_VALUE3 DIR8_WEST
#define SPECENUM_VALUE3NAME "West"
#define SPECENUM_VALUE4 DIR8_EAST
#define SPECENUM_VALUE4NAME "East"
#define SPECENUM_VALUE5 DIR8_SOUTHWEST
#define SPECENUM_VALUE5NAME "Southwest"
#define SPECENUM_VALUE6 DIR8_SOUTH
#define SPECENUM_VALUE6NAME "South"
#define SPECENUM_VALUE7 DIR8_SOUTHEAST
#define SPECENUM_VALUE7NAME "Southeast"
#include "specenum_gen.h"

/* Some code requires compile time value for number of directions, and
 * cannot use specenum function call direction8_max(). */
#define DIR8_MAGIC_MAX 8

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
  struct road_type *road;

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
#define SPECENUM_VALUE20 VUT_ROAD
#define SPECENUM_VALUE20NAME "Road"
#define SPECENUM_COUNT VUT_COUNT
#include "specenum_gen.h"

struct universal {
  universals_u value;
  enum universals_n kind;		/* formerly .type and .is_unit */
};

struct ai_choice;			/* incorporates universals_u */

BV_DEFINE(bv_bases, MAX_BASE_TYPES);
BV_DEFINE(bv_roads, MAX_ROAD_TYPES);
BV_DEFINE(bv_startpos_nations, MAX_NUM_STARTPOS_NATIONS);

#define SPECENUM_NAME gui_type
/* Used for options which do not belong to any gui. */
#define SPECENUM_VALUE0 GUI_STUB
#define SPECENUM_VALUE0NAME "stub"
#define SPECENUM_VALUE1 GUI_GTK2
#define SPECENUM_VALUE1NAME "gtk2"
#define SPECENUM_VALUE2 GUI_GTK3
#define SPECENUM_VALUE2NAME "gtk3"
#define SPECENUM_VALUE3 GUI_SDL
#define SPECENUM_VALUE3NAME "sdl"
#define SPECENUM_VALUE4 GUI_XAW
#define SPECENUM_VALUE4NAME "xaw"
#define SPECENUM_VALUE5 GUI_QT
#define SPECENUM_VALUE5NAME "qt"
#define SPECENUM_VALUE6 GUI_WIN32
#define SPECENUM_VALUE6NAME "win32"
#define SPECENUM_VALUE7 GUI_FTWL
#define SPECENUM_VALUE7NAME "ftwl"
#include "specenum_gen.h"

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

#define SPECENUM_NAME reveal_map
#define SPECENUM_BITWISE
/* Reveal only the area around the first units at the beginning. */
#define SPECENUM_ZERO   REVEAL_MAP_NONE
/* Reveal the (fogged) map at the beginning of the game. */
#define SPECENUM_VALUE0 REVEAL_MAP_START
/* Reveal (and unfog) the map for dead players. */
#define SPECENUM_VALUE1 REVEAL_MAP_DEAD
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

enum tile_special_type {
  S_OLD_ROAD,
  S_IRRIGATION,
  S_OLD_RAILROAD,
  S_MINE,
  S_POLLUTION,
  S_HUT,
  S_OLD_FORTRESS,
  S_RIVER,
  S_FARMLAND,
  S_OLD_AIRBASE,
  S_FALLOUT,

  /* internal values not saved */
  S_LAST,
  S_RESOURCE_VALID = S_LAST,
};

/* S_OLD_xxx is used where code refers to compatibility
 * with old versions. S_xxx is still used where code
 * in question works the old way and needs updating.
 * These defines should be removed when transition
 * to use of road types is finished. */
#define S_ROAD S_OLD_ROAD
#define S_RAILROAD S_OLD_RAILROAD


#ifdef __cplusplus
}
#endif /* __cplusplus */

enum test_result {
  TR_SUCCESS,
  TR_OTHER_FAILURE,
  TR_ALREADY_SOLD
};

enum act_tgt_type { ATT_SPECIAL, ATT_BASE, ATT_ROAD };

union act_tgt_obj {
  enum tile_special_type spe;
  Base_type_id base;
  Road_type_id road;
};

struct act_tgt {
  enum act_tgt_type type;
  union act_tgt_obj obj;
};

#endif /* FC__FC_TYPES_H */
