/********************************************************************** 
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef FC__UNITTYPE_H
#define FC__UNITTYPE_H

#include "shared.h"

struct player;
struct city;
struct government;
struct Sprite;			/* opaque; client-gui specific */
struct unit;

typedef int Unit_Type_id;
/*
  Above typedef replaces old "enum unit_type_id" (since no longer
  enumerate the unit types); keep as typedef to help code be
  self-documenting.

  It could potentially be some other type; "unsigned char" would be
  natural, since there are already built-in assumptions that values
  are not too large (less than U_LAST = MAX_NUM_ITEMS) since they must
  fit in 8-bit unsigned int for packets; and normal values are always
  non-negative.  But note sometimes use (-1) for obsoleted_by and some
  related uses, though these use already plain int rather than
  Unit_Type_id?  (Ideally, these should probably have used U_LAST as
  the flag value instead of (-1).)
  
  Decided to just use 'int' for several reasons:
  - "natural integer type" may be faster on some platforms
    (size advantage of using smaller type probably negligible);
  - avoids any potential problems with (-1) values as mentioned above;
  - avoids imposing any more limitations that there are already.
*/
  
#define U_LAST MAX_NUM_ITEMS
/*
  U_LAST is a value which is guaranteed to be larger than all
  actual Unit_Type_id values.  It is used as a flag value;
  it can also be used for fixed allocations to ensure able
  to hold full number of unit types.
*/

enum unit_move_type {
  LAND_MOVING = 1, SEA_MOVING, HELI_MOVING, AIR_MOVING
};

/* Classes for unit types.
 * (These must correspond to unit_class_names[] in unit.c.)
 */
enum unit_class_id {
  UCL_AIR,
  UCL_HELICOPTER,
  UCL_LAND,
  UCL_MISSILE,
  UCL_NUCLEAR,
  UCL_SEA,
  UCL_LAST	/* keep this last */
};

typedef enum unit_class_id Unit_Class_id;

/* Unit "special effects" flags:
   Note this is now an enumerated type, and not power-of-two integers
   for bits, though unit_type.flags is still a bitfield, and code
   which uses unit_flag() without twiddling bits is unchanged.
   (It is easier to go from i to (1<<i) than the reverse.)
   See data/default/units.ruleset for documentation of their effects.
*/
enum unit_flag_id { 
  F_TRADE_ROUTE=0,
  F_HELP_WONDER,
  F_MISSILE,   
  F_IGZOC,     
  F_NONMIL,      
  F_IGTER,       
  F_CARRIER,     
  F_ONEATTACK,   
  F_PIKEMEN,     
  F_HORSE,       
  F_IGWALL,      
  F_FIELDUNIT,   
  F_AEGIS,       
  F_FIGHTER,     
  F_MARINES,     
  F_PARTIAL_INVIS,    /* Invisibile except when adjacent (Submarine) */   
  F_SETTLERS,         /* Does not include ability to found cities */
  F_DIPLOMAT,    
  F_TRIREME,          /* Trireme sinking effect */
  F_NUCLEAR,          /* Nuclear attack effect */
  F_SPY,              /* Enhanced spy abilities */
  F_TRANSFORM,        /* Can transform terrain types (Engineers) */
  F_PARATROOPERS,
  F_AIRBASE,          /* Can build Airbases */
  F_CITIES,           /* Can build cities */
  F_IGTIRED,          /* Ignore tired negative bonus when attacking */
  F_MISSILE_CARRIER,  /* Like F_CARRIER, but missiles only (Submarine) */
  F_NO_LAND_ATTACK,   /* Cannot attack vs land squares (Submarine) */
  F_ADD_TO_CITY,      /* unit can add to city population */
  F_FANATIC,          /* Only Fundamentalist government can build
			 these units */
  F_LAST
};

/* Unit "roles": these are similar to unit flags but differ in that
   they don't represent intrinsic properties or abilities of units,
   but determine which units are used (mainly by the server or AI)
   in various circumstances, or "roles".
   Note that in some cases flags can act as roles, eg, we don't need
   a role for "settlers", because we can just use F_SETTLERS.
   (Now have to consider F_CITIES too)
   So we make sure flag values and role values are distinct,
   so some functions can use them interchangably.
   See data/default/units.ruleset for documentation of their effects.
*/
#define L_FIRST 64		/* should be >= F_LAST */
enum unit_role_id {
  L_FIRSTBUILD=L_FIRST, /* is built first when city established */
  L_EXPLORER,           /* initial explorer unit */
  L_HUT,                /* can be found in hut */
  L_HUT_TECH,           /* can be found in hut, global tech required */
  L_PARTISAN,           /* is created in Partisan circumstances */
  L_DEFEND_OK,          /* ok on defense (AI) */
  L_DEFEND_GOOD,        /* primary purpose is defense (AI) */
  L_ATTACK_FAST,        /* quick attacking unit (Horse..Armor) (unused)*/
  L_ATTACK_STRONG,      /* powerful attacking unit (Catapult..) (unused) */
  L_FERRYBOAT,	        /* is useful for ferrying (AI) */
  L_BARBARIAN,          /* barbarians unit, land only */
  L_BARBARIAN_TECH,     /* barbarians unit, global tech required */
  L_BARBARIAN_BOAT,     /* barbarian boat */
  L_BARBARIAN_BUILD,    /* what barbarians should build */
  L_BARBARIAN_BUILD_TECH, /* barbarians build when global tech */
  L_BARBARIAN_LEADER,   /* barbarian leader */
  L_BARBARIAN_SEA,      /* sea raider unit */
  L_BARBARIAN_SEA_TECH, /* sea raider unit, global tech required */
  L_LAST
};

struct unit_type {
  char name[MAX_LEN_NAME];
  char name_orig[MAX_LEN_NAME];	      /* untranslated */
  char graphic_str[MAX_LEN_NAME];
  char graphic_alt[MAX_LEN_NAME];
  char sound_move[MAX_LEN_NAME];
  char sound_move_alt[MAX_LEN_NAME];
  char sound_fight[MAX_LEN_NAME];
  char sound_fight_alt[MAX_LEN_NAME];
  struct Sprite *sprite;
  enum unit_move_type move_type;
  int build_cost;
  int pop_cost;  /* number of workers the unit contains (e.g., settlers, engineers)*/
  int attack_strength;
  int defense_strength;
  int move_rate;
  int tech_requirement;
  int vision_range;
  int transport_capacity;
  int hp;
  int firepower;
  int obsoleted_by;
  int fuel;

  unsigned int flags;
  unsigned int roles;

  int happy_cost;  /* unhappy people in home city */
  int shield_cost; /* normal upkeep cost */
  int food_cost;   /* settler food cost */
  int gold_cost;   /* gold upkeep (n/a now, maybe later) */

  int paratroopers_range; /* only valid for F_PARATROOPERS */
  int paratroopers_mr_req;
  int paratroopers_mr_sub;

  char *helptext;
};


extern struct unit_type unit_types[U_LAST];

bool unit_type_exists(Unit_Type_id id);
struct unit_type *get_unit_type(Unit_Type_id id);
struct unit_type *unit_type(struct unit *punit);

bool unit_type_flag(Unit_Type_id id, int flag);
bool unit_flag(struct unit *punit, enum unit_flag_id flag);
bool unit_has_role(Unit_Type_id id, int role);

bool is_water_unit(Unit_Type_id id);
bool is_air_unittype(Unit_Type_id id);
bool is_heli_unittype(Unit_Type_id id);
bool is_ground_unittype(Unit_Type_id id);

int unit_value(Unit_Type_id id);
int unit_pop_value(Unit_Type_id id);

char *unit_name(Unit_Type_id id);
const char *unit_class_name(Unit_Class_id id);

char *get_unit_name(Unit_Type_id id);
char *get_units_with_flag_string(int flag);

int utype_shield_cost(struct unit_type *ut, struct government *g);
int utype_food_cost(struct unit_type *ut, struct government *g);
int utype_happy_cost(struct unit_type *ut, struct government *g);
int utype_gold_cost(struct unit_type *ut, struct government *g);

int can_upgrade_unittype(struct player *pplayer, Unit_Type_id id);
int unit_upgrade_price(struct player *pplayer, Unit_Type_id from,
		       Unit_Type_id to);

Unit_Type_id find_unit_type_by_name(char *s);

enum unit_move_type unit_move_type_from_str(char *s);
Unit_Class_id unit_class_from_str(char *s);
enum unit_flag_id unit_flag_from_str(char *s);
enum unit_role_id unit_role_from_str(char *s);

bool can_player_build_unit_direct(struct player *p, Unit_Type_id id);
bool can_player_build_unit(struct player *p, Unit_Type_id id);
bool can_player_eventually_build_unit(struct player *p, Unit_Type_id id);

void role_unit_precalcs(void);
int num_role_units(int role);
Unit_Type_id get_role_unit(int role, int index);
Unit_Type_id best_role_unit(struct city *pcity, int role);

#define unit_type_iterate(m_i)                                                \
{                                                                             \
  Unit_Type_id m_i;                                                           \
  for (m_i = 0; m_i < game.num_unit_types; m_i++) {

#define unit_type_iterate_end                                                 \
  }                                                                           \
}

#endif  /* FC__UNITTYPE_H */
