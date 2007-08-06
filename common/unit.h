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

#include "mem.h"		/* unit_list_iterate_safe */

#include "fc_types.h"
#include "terrain.h"		/* enum tile_special_type */
#include "unittype.h"

struct unit_order;

#define BARBARIAN_LIFE    5

/* Changing this enum will break savegame and network compatability. */
enum unit_activity {
  ACTIVITY_IDLE, ACTIVITY_POLLUTION, ACTIVITY_ROAD, ACTIVITY_MINE,
  ACTIVITY_IRRIGATE, ACTIVITY_FORTIFIED, ACTIVITY_FORTRESS, ACTIVITY_SENTRY,
  ACTIVITY_RAILROAD, ACTIVITY_PILLAGE, ACTIVITY_GOTO, ACTIVITY_EXPLORE,
  ACTIVITY_TRANSFORM, ACTIVITY_UNKNOWN, ACTIVITY_AIRBASE, ACTIVITY_FORTIFYING,
  ACTIVITY_FALLOUT,
  ACTIVITY_PATROL_UNUSED, /* Needed for savegame compatability. */
  ACTIVITY_LAST   /* leave this one last */
};

/* Changing this enum will break savegame and network compatability. */
enum unit_orders {
  ORDER_MOVE, ORDER_FULL_MP, ORDER_ACTIVITY,
  ORDER_LAST
};

enum unit_focus_status {
  FOCUS_AVAIL, FOCUS_WAIT, FOCUS_DONE  
};

enum diplomat_actions {
  DIPLOMAT_BRIBE, DIPLOMAT_EMBASSY, DIPLOMAT_SABOTAGE,
  DIPLOMAT_STEAL, DIPLOMAT_INCITE, SPY_POISON, 
  DIPLOMAT_INVESTIGATE, SPY_SABOTAGE_UNIT,
  SPY_GET_SABOTAGE_LIST,
  DIPLOMAT_MOVE,	/* move onto city square - only for allied cities */
  DIPLOMAT_ANY_ACTION   /* leave this one last */
};

enum ai_unit_task { AIUNIT_NONE, AIUNIT_AUTO_SETTLER, AIUNIT_BUILD_CITY,
                    AIUNIT_DEFEND_HOME, AIUNIT_ATTACK, AIUNIT_FORTIFY,
                    AIUNIT_RUNAWAY, AIUNIT_ESCORT, AIUNIT_EXPLORE,
                    AIUNIT_PILLAGE, AIUNIT_RECOVER, AIUNIT_HUNTER };

enum goto_move_restriction {
  GOTO_MOVE_ANY,
  GOTO_MOVE_CARDINAL_ONLY, /* No diagonal moves.  */
  GOTO_MOVE_STRAIGHTEST
};

enum goto_route_type {
  ROUTE_GOTO, ROUTE_PATROL
};

enum unit_move_result {
  MR_OK, MR_BAD_TYPE_FOR_CITY_TAKE_OVER, MR_NO_WAR, MR_ZOC,
  MR_BAD_ACTIVITY, MR_BAD_DESTINATION, MR_BAD_MAP_POSITION,
  MR_DESTINATION_OCCUPIED_BY_NON_ALLIED_UNIT,
  MR_NO_SEA_TRANSPORTER_CAPACITY,
  MR_DESTINATION_OCCUPIED_BY_NON_ALLIED_CITY
};

enum add_build_city_result {
  AB_BUILD_OK,			/* Unit OK to build city */
  AB_ADD_OK,			/* Unit OK to add to city */
  AB_NOT_BUILD_LOC,		/* City is not allowed to be built at
				   this location */
  AB_NOT_ADDABLE_UNIT,		/* Unit is not one that can be added
				   to cities */
  AB_NOT_BUILD_UNIT,		/* Unit is not one that can build
				   cities */
  AB_NO_MOVES_BUILD,		/* Unit does not have moves left to
				   build a city */
  AB_NO_MOVES_ADD,		/* Unit does not have moves left to
				   add to city */
  AB_NOT_OWNER,			/* Owner of unit is not owner of
				   city */
  AB_TOO_BIG,			/* City is too big to be added to */
  AB_NO_SPACE			/* Adding takes city past limit */
};

enum unit_upgrade_result {
  UR_OK,
  UR_NO_UNITTYPE,
  UR_NO_MONEY,
  UR_NOT_IN_CITY,
  UR_NOT_CITY_OWNER,
  UR_NOT_ENOUGH_ROOM
};
    
struct unit_ai {
  bool control; /* 0: not automated    1: automated */
  enum ai_unit_task ai_role;
  /* The following are all unit ids */
  int ferryboat; /* the ferryboat assigned to us */
  int passenger; /* the unit assigned to this ferryboat */
  int bodyguard; /* the unit bodyguarding us */
  int charge; /* the unit this unit is bodyguarding */

  struct tile *prev_struct, *cur_struct;
  struct tile **prev_pos, **cur_pos;

  int target; /* target we hunt */
  int hunted; /* if a player is hunting us, set by that player */
};

struct unit {
  Unit_Type_id type;
  int id;
  int owner;
  struct tile *tile;
  int homecity;
  int moves_left;
  int hp;
  int veteran;
  int unhappiness;
  int upkeep;
  int upkeep_food;
  int upkeep_gold;
  int fuel;
  int bribe_cost;
  struct unit_ai ai;
  enum unit_activity activity;
  struct tile *goto_tile; /* May be NULL. */

  /* The amount of work that has been done on the current activity.  This
   * is counted in turns but is multiplied by ACTIVITY_COUNT (which allows
   * fractional values in some cases). */
  int activity_count;

  enum tile_special_type activity_target;
  enum unit_focus_status focus_status;
  int ord_map, ord_city;
  /* ord_map and ord_city are the order index of this unit in tile.units
     and city.units_supported; they are only used for save/reload */
  bool debug;
  bool moved;
  bool paradropped;

  /* This value is set if the unit is done moving for this turn. This
   * information is used by the client.  The invariant is:
   *   - If the unit has no more moves, it's done moving.
   *   - If the unit is on a goto but is waiting, it's done moving.
   *   - Otherwise the unit is not done moving. */
  bool done_moving;

  int transported_by;
  int occupy; /* number of units that occupy transporter */
  struct {
    /* Equivalent to pcity->client.color.  Only for F_CITIES units. */
    bool colored;
    int color_index;
  } client;

  bool has_orders;
  struct {
    int length, index;
    bool repeat;	/* The path is to be repeated on completion. */
    bool vigilant;	/* Orders should be cleared if an enemy is met. */
    struct unit_order *list;
  } orders;
};

/* get 'struct unit_list' and related functions: */
#define SPECLIST_TAG unit
#define SPECLIST_TYPE struct unit
#include "speclist.h"

#define unit_list_iterate(unitlist, punit) \
    TYPED_LIST_ITERATE(struct unit, unitlist, punit)
#define unit_list_iterate_end  LIST_ITERATE_END
#define SINGLE_MOVE 3
#define MOVE_COST_RIVER 1
#define MOVE_COST_RAIL 0
#define MOVE_COST_ROAD 1
#define MOVE_COST_AIR 1

#define unit_list_iterate_safe(unitlist, punit) \
{ \
  int _size = unit_list_size(&unitlist); \
  if (_size > 0) { \
    int _ids[_size]; \
    int _i = 0; \
    unit_list_iterate(unitlist, punit) { \
      _ids[_i++] = punit->id; \
    } unit_list_iterate_end; \
    for (_i=0; _i<_size; _i++) { \
      struct unit *punit = find_unit_by_id(_ids[_i]); \
      if (punit) { \

#define unit_list_iterate_safe_end \
      } \
    } \
  } \
}

struct unit *unit_list_find(struct unit_list *This, int id);

void unit_list_sort_ord_map(struct unit_list *This);
void unit_list_sort_ord_city(struct unit_list *This);

bool diplomat_can_do_action(struct unit *pdiplomat,
			    enum diplomat_actions action,
			    const struct tile *ptile);
bool is_diplomat_action_available(struct unit *pdiplomat,
				  enum diplomat_actions action,
				  const struct tile *ptile);

int unit_move_rate(struct unit *punit);
bool unit_can_help_build_wonder(struct unit *punit, struct city *pcity);
bool unit_can_help_build_wonder_here(struct unit *punit);
bool unit_can_est_traderoute_here(struct unit *punit);
bool unit_can_defend_here(struct unit *punit);
bool unit_can_airlift_to(struct unit *punit, struct city *pcity);
bool unit_has_orders(struct unit *punit);

bool can_unit_load(struct unit *punit, struct unit *ptrans);
bool can_unit_unload(struct unit *punit, struct unit *ptrans);
bool can_unit_paradrop(struct unit *punit);
bool can_unit_bombard(struct unit *punit);
bool can_unit_change_homecity_to(struct unit *punit, struct city *pcity);
bool can_unit_change_homecity(struct unit *punit);
const char *get_activity_text(enum unit_activity activity);
bool can_unit_continue_current_activity(struct unit *punit);
bool can_unit_do_activity(struct unit *punit, enum unit_activity activity);
bool can_unit_do_activity_targeted(struct unit *punit,
				   enum unit_activity activity,
				   enum tile_special_type target);
bool can_unit_do_activity_targeted_at(struct unit *punit,
				      enum unit_activity activity,
				      enum tile_special_type target,
				      const struct tile *ptile);
void set_unit_activity(struct unit *punit, enum unit_activity new_activity);
void set_unit_activity_targeted(struct unit *punit,
				enum unit_activity new_activity,
				enum tile_special_type new_target);
int get_activity_rate(struct unit *punit);
int get_activity_rate_this_turn(struct unit *punit);
int get_turns_for_activity_at(struct unit *punit,
			      enum unit_activity activity,
			      const struct tile *ptile);
bool can_unit_do_auto(struct unit *punit); 
bool is_unit_activity_on_tile(enum unit_activity activity,
			      const struct tile *ptile);
enum tile_special_type get_unit_tile_pillage_set(const struct tile *ptile);
bool is_attack_unit(struct unit *punit);
bool is_military_unit(struct unit *punit);           /* !set !dip !cara */
bool is_diplomat_unit(struct unit *punit);
bool is_square_threatened(struct player *pplayer, const struct tile *ptile);
bool is_field_unit(struct unit *punit);              /* ships+aero */
bool is_hiding_unit(struct unit *punit);
bool is_sailing_unit(struct unit *punit);
bool is_air_unit(struct unit *punit);
bool is_heli_unit(struct unit *punit);
bool is_ground_unit(struct unit *punit);
#define COULD_OCCUPY(punit) \
  ((is_ground_unit(punit) || is_heli_unit(punit)) && is_military_unit(punit))
bool can_unit_add_to_city (struct unit *punit);
bool can_unit_build_city (struct unit *punit);
bool can_unit_add_or_build_city (struct unit *punit);
enum add_build_city_result test_unit_add_or_build_city(struct unit *punit);
bool kills_citizen_after_attack(struct unit *punit);

const char *unit_activity_text(struct unit *punit);
int ground_unit_transporter_capacity(const struct tile *ptile,
				     struct player *pplayer);
int get_transporter_capacity(struct unit *punit);
bool is_ground_units_transport(struct unit *punit);
bool is_air_units_transport(struct unit *punit);
int missile_carrier_capacity(const struct tile *ptile,
			     struct player *pplayer,
			     bool count_units_with_extra_fuel);
int airunit_carrier_capacity(const struct tile *ptile,
			     struct player *pplayer,
			     bool count_units_with_extra_fuel);

struct player *unit_owner(struct unit *punit);

struct unit *is_allied_unit_tile(const struct tile *ptile,
				 struct player *pplayer);
struct unit *is_enemy_unit_tile(const struct tile *ptile,
				struct player *pplayer);
struct unit *is_non_allied_unit_tile(const struct tile *ptile,
				     struct player *pplayer);
struct unit *is_non_attack_unit_tile(const struct tile *ptile,
				     struct player *pplayer);

int unit_loss_pct(struct player *pplayer, const struct tile *ptile,
		  struct unit *punit);
int base_trireme_loss_pct(struct player *pplayer, struct unit *punit);
int base_unsafe_terrain_loss_pct(struct player *pplayer,
				 struct unit *punit);

bool is_my_zoc(struct player *unit_owner, const struct tile *ptile);
bool unit_being_aggressive(struct unit *punit);
bool can_step_taken_wrt_to_zoc(Unit_Type_id type, struct player *unit_owner,
			       const struct tile *src_tile,
			       const struct tile *dst_tile);
bool can_unit_exist_at_tile(struct unit *punit, const struct tile *ptile);
bool can_unit_survive_at_tile(struct unit *punit, const struct tile *ptile);
bool can_unit_move_to_tile(struct unit *punit, const struct tile *ptile,
			   bool igzoc);
enum unit_move_result test_unit_move_to_tile(Unit_Type_id type,
					     struct player *unit_owner,
					     enum unit_activity activity,
					     const struct tile *src_tile,
					     const struct tile *dst_tile,
					     bool igzoc);
bool unit_type_really_ignores_zoc(Unit_Type_id type);
bool zoc_ok_move(struct unit *punit, const struct tile *ptile);

bool is_build_or_clean_activity(enum unit_activity activity);

struct unit *create_unit_virtual(struct player *pplayer, struct city *pcity,
                                 Unit_Type_id type, int veteran_level);
void destroy_unit_virtual(struct unit *punit);
void free_unit_orders(struct unit *punit);

int get_transporter_occupancy(struct unit *ptrans);
struct unit *find_transporter_for_unit(struct unit *pcargo,
				       const struct tile *ptile);

enum unit_upgrade_result test_unit_upgrade(struct unit *punit, bool is_free);
enum unit_upgrade_result get_unit_upgrade_info(char *buf, size_t bufsz,
					       struct unit *punit);

#endif  /* FC__UNIT_H */
