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
#ifndef FC__UNIT_H
#define FC__UNIT_H

#include "genlist.h"
#include "shared.h"

struct player;
struct city;
struct government;
struct Sprite;			/* opaque; client-gui specific */

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
  - avoids imposing any more limitations that there are already.  */
  
#define U_LAST MAX_NUM_ITEMS
/*
  U_LAST is a value which is guaranteed to be larger than all
  actual Unit_Type_id values.  It is used as a flag value;
  it can also be used for fixed allocations to ensure able
  to hold full number of unit types.
*/

enum unit_activity {
  ACTIVITY_IDLE, ACTIVITY_POLLUTION, ACTIVITY_ROAD, ACTIVITY_MINE, 
  ACTIVITY_IRRIGATE, ACTIVITY_FORTIFY, ACTIVITY_FORTRESS, ACTIVITY_SENTRY,
  ACTIVITY_RAILROAD, ACTIVITY_PILLAGE, ACTIVITY_GOTO, ACTIVITY_EXPLORE,
  ACTIVITY_TRANSFORM, ACTIVITY_UNKNOWN, ACTIVITY_AIRBASE
};

enum unit_move_type {
  LAND_MOVING = 1, SEA_MOVING, HELI_MOVING, AIR_MOVING
};

enum unit_focus_status {
  FOCUS_AVAIL, FOCUS_WAIT, FOCUS_DONE  
};

enum diplomat_actions {
  DIPLOMAT_BRIBE, DIPLOMAT_EMBASSY, DIPLOMAT_SABOTAGE,
  DIPLOMAT_STEAL, DIPLOMAT_INCITE, SPY_POISON, 
  DIPLOMAT_INVESTIGATE, SPY_SABOTAGE_UNIT,
  DIPLOMAT_ANY_ACTION
};

struct unit_ai {
  int control;
  int ai_role;
  int ferryboat;
  int passenger;
  int bodyguard;
  int charge; /* couldn't find a better synonym -- Syela */
};

struct unit {
  Unit_Type_id type;
  int id;
  int owner;
  int x, y;                           
  int veteran;
  int homecity;
  int moves_left;
  int hp;
  int unhappiness;
  int upkeep;
  int upkeep_food;
  int upkeep_gold;
  int foul;
  int fuel;
  int bribe_cost;
  struct unit_ai ai;
  enum unit_activity activity;
  int goto_dest_x, goto_dest_y;
  int activity_count;
  int activity_target;
  enum unit_focus_status focus_status;
  int ord_map, ord_city;
  /* ord_map and ord_city are the order index of this unit in tile.units
     and city.units_supported; they are only used for save/reload */
  int moved;
  int paradropped;
};

/* Unit "special effects" flags:
   Note this is now an enumerated type, and not power-of-two integers
   for bits, though unit_type.flags is still a bitfield, and code
   which uses unit_flag() without twiddling bits is unchanged.
   (It is easier to go from i to (1<<i) than the reverse.)
*/
enum unit_flag_id { 
  F_CARAVAN=0,
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
  F_SUBMARINE,   
  F_SETTLERS,    
  F_DIPLOMAT,    
  F_TRIREME,          /* Trireme sinking effect */
  F_NUCLEAR,          /* Nuclear attack effect */
  F_SPY,              /* Enhanced spy abilities */
  F_TRANSFORM,        /* Can transform terrain types (Engineers) */
  F_PARATROOPERS,
  F_AIRBASE,          /* Can build Airbases */
  F_LAST
};

/* Unit "roles": these are similar to unit flags but differ in that
   they don't represent intrinsic properties or abilities of units,
   but determine which units are used (mainly by the server or AI)
   in various circumstances, or "roles".
   Note that in some cases flags can act as roles, eg, we don't need
   a role for "settlers", because we can just use F_SETTLERS.
   So we make sure flag values and role values are distinct,
   so some functions can use them interchangably.
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
  L_LAST
};

struct unit_type {
  char name[MAX_LEN_NAME];
  char name_orig[MAX_LEN_NAME];	      /* untranslated */
  char graphic_str[MAX_LEN_NAME];
  char graphic_alt[MAX_LEN_NAME];
  struct Sprite *sprite;
  enum unit_move_type move_type;
  int build_cost;
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


/* get 'struct unit_list' and related functions: */
#define SPECLIST_TAG unit
#define SPECLIST_TYPE struct unit
#include "speclist.h"

#define unit_list_iterate(unitlist, punit) \
    TYPED_LIST_ITERATE(struct unit, unitlist, punit)
#define unit_list_iterate_end  LIST_ITERATE_END

extern struct unit_type unit_types[U_LAST];

char *unit_name(Unit_Type_id id);

struct unit *unit_list_find(struct unit_list *This, int id);

void unit_list_sort_ord_map(struct unit_list *This);
void unit_list_sort_ord_city(struct unit_list *This);

int diplomat_can_do_action(struct unit *pdiplomat,
			   enum diplomat_actions action, 
			   int destx, int desty);
int is_diplomat_action_available(struct unit *pdiplomat,
                                 enum diplomat_actions action,
                                 int destx, int desty);

struct unit *find_unit_by_id(int id);

char *get_unit_name(Unit_Type_id id);
char *get_units_with_flag_string(int flag);

int unit_move_rate(struct unit *punit);
int unit_can_help_build_wonder(struct unit *punit, struct city *pcity);
int unit_can_help_build_wonder_here(struct unit *punit);
int unit_can_est_traderoute_here(struct unit *punit);
int unit_can_defend_here(struct unit *punit);
int unit_can_airlift_to(struct unit *punit, struct city *pcity);

int can_unit_paradropped(struct unit *punit);
int can_unit_change_homecity(struct unit *punit);
int can_unit_do_activity(struct unit *punit, enum unit_activity activity);
int can_unit_do_activity_targeted(struct unit *punit,
				  enum unit_activity activity, int target);
void set_unit_activity(struct unit *punit, enum unit_activity new_activity);
void set_unit_activity_targeted(struct unit *punit,
				enum unit_activity new_activity, int new_target);
int can_unit_do_auto(struct unit *punit); 
int is_unit_activity_on_tile(enum unit_activity activity, int x, int y);
int get_unit_tile_pillage_set(int x, int y);
int unit_value(Unit_Type_id id);
int is_military_unit(struct unit *this_unit);           /* !set !dip !cara */
int is_ground_threat(struct player *pplayer, struct unit *punit);
int is_square_threatened(struct player *pplayer, int x, int y);
int is_field_unit(struct unit *this_unit);              /* ships+aero */
int is_hiding_unit(struct unit *punit);
void raise_unit_top(struct unit *punit);
int is_water_unit(Unit_Type_id id);
int is_sailing_unit(struct unit *punit);
int is_air_unit(struct unit *punit);
int is_air_unittype(Unit_Type_id id);
int is_heli_unit(struct unit *punit);
int is_heli_unittype(Unit_Type_id id);
int is_ground_unit(struct unit *punit);
int is_ground_unittype(Unit_Type_id id);
int can_unit_build_city(struct unit *punit);
int can_unit_add_to_city(struct unit *punit);

struct unit_type *get_unit_type(Unit_Type_id id);
char *unit_activity_text(struct unit *punit);
char *unit_description(struct unit *punit);
int is_transporter_with_free_space(struct player *pplayer, int x, int y);
int is_enough_transporter_space(struct player *pplayer, int x, int y);
int get_transporter_capacity(struct unit *punit);
int is_ground_units_transport(struct unit *punit);

int utype_shield_cost(struct unit_type *ut, struct government *g);
int utype_food_cost(struct unit_type *ut, struct government *g);
int utype_happy_cost(struct unit_type *ut, struct government *g);
int utype_gold_cost(struct unit_type *ut, struct government *g);

void move_unit_list_to_tile(struct unit_list *units, int x, int y);
void transporter_cargo_to_unitlist(struct unit *ptran, struct unit_list *list);
void transporter_min_cargo_to_unitlist(struct unit *ptran,
				       struct unit_list *list);
int unit_flag(Unit_Type_id id, int flag);
int unit_has_role(Unit_Type_id id, int role);
int can_upgrade_unittype(struct player *pplayer, Unit_Type_id id);
int unit_upgrade_price(struct player *pplayer, Unit_Type_id from, Unit_Type_id to);

int unit_type_exists(Unit_Type_id id);
Unit_Type_id find_unit_type_by_name(char *s);

enum unit_move_type unit_move_type_from_str(char *s);
enum unit_flag_id unit_flag_from_str(char *s);
enum unit_role_id unit_role_from_str(char *s);

void role_unit_precalcs(void);
int num_role_units(int role);
Unit_Type_id get_role_unit(int role, int index);
Unit_Type_id best_role_unit(struct city *pcity, int role);

#endif  /* FC__UNIT_H */
