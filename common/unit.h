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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* utility */
#include "bitvector.h"

/* common */
#include "ai.h"                 /* FC_AI_LAST */
#include "base.h"
#include "fc_types.h"
#include "terrain.h"		/* enum tile_special_type */
#include "unittype.h"
#include "vision.h"

/* Changing this enum will break network compatability. */
enum unit_orders {
  ORDER_MOVE = 0,
  ORDER_ACTIVITY = 1,
  ORDER_FULL_MP = 2,
  ORDER_BUILD_CITY = 3,
  ORDER_DISBAND = 4,
  ORDER_BUILD_WONDER = 5,
  ORDER_TRADE_ROUTE = 6,
  ORDER_HOMECITY = 7,
  /* and plenty more for later... */
  ORDER_LAST
};

enum unit_focus_status {
  FOCUS_AVAIL, FOCUS_WAIT, FOCUS_DONE  
};

/* Changing this enum will break network compatability. */
enum diplomat_actions {
  DIPLOMAT_MOVE = 0,	/* move onto city square - only for allied cities */
  DIPLOMAT_EMBASSY = 1,
  DIPLOMAT_BRIBE = 2,
  DIPLOMAT_INCITE = 3,
  DIPLOMAT_INVESTIGATE = 4,
  DIPLOMAT_SABOTAGE = 5,
  DIPLOMAT_STEAL = 6,
  SPY_POISON = 7, 
  SPY_SABOTAGE_UNIT = 8,
  DIPLOMAT_ANY_ACTION   /* leave this one last */
};

enum goto_move_restriction {
  GOTO_MOVE_ANY,
  GOTO_MOVE_CARDINAL_ONLY, /* No diagonal moves.  */
  GOTO_MOVE_STRAIGHTEST
};

enum goto_route_type {
  ROUTE_GOTO, ROUTE_PATROL
};

enum unit_add_build_city_result {
  UAB_BUILD_OK,         /* Unit OK to build city. */
  UAB_ADD_OK,           /* Unit OK to add to city. */
  UAB_BAD_CITY_TERRAIN, /* Equivalent to 'CB_BAD_CITY_TERRAIN'. */
  UAB_BAD_UNIT_TERRAIN, /* Equivalent to 'CB_BAD_UNIT_TERRAIN'. */
  UAB_BAD_BORDERS,      /* Equivalent to 'CB_BAD_BORDERS'. */
  UAB_NO_MIN_DIST,      /* Equivalent to 'CB_NO_MIN_DIST'. */
  UAB_NOT_ADDABLE_UNIT, /* Unit is not one that can be added to cities. */
  UAB_NOT_BUILD_UNIT,   /* Unit is not one that can build cities. */
  UAB_NO_MOVES_BUILD,   /* Unit does not have moves left to build a city. */
  UAB_NO_MOVES_ADD,     /* Unit does not have moves left to add to city. */
  UAB_NOT_OWNER,        /* Owner of unit is not owner of city. */
  UAB_TOO_BIG,          /* City is too big to be added to. */
  UAB_NO_SPACE          /* Adding takes city past limit. */
};

enum unit_upgrade_result {
  UU_OK,
  UU_NO_UNITTYPE,
  UU_NO_MONEY,
  UU_NOT_IN_CITY,
  UU_NOT_CITY_OWNER,
  UU_NOT_ENOUGH_ROOM,
  UU_NOT_TERRAIN        /* The upgraded unit could not survive. */
};

enum unit_airlift_result {
  /* Codes treated as success: */
  AR_OK,                /* This will definitely work */
  AR_OK_SRC_UNKNOWN,    /* Source city's airlift capability is unknown */
  AR_OK_DST_UNKNOWN,    /* Dest city's airlift capability is unknown */
  /* Codes treated as failure: */
  AR_NO_MOVES,          /* Unit has no moves left */
  AR_WRONG_UNITTYPE,    /* Can't airlift this type of unit */
  AR_OCCUPIED,          /* Can't airlift units with passengers */
  AR_NOT_IN_CITY,       /* Unit not in a city */
  AR_BAD_SRC_CITY,      /* Can't airlift from this src city */
  AR_BAD_DST_CITY,      /* Can't airlift to this dst city */
  AR_SRC_NO_FLIGHTS,    /* No flights available from src */
  AR_DST_NO_FLIGHTS     /* No flights available to dst */
};

struct unit_adv {
  enum adv_unit_task task;
};

struct unit_order {
  enum unit_orders order;
  enum unit_activity activity;  /* Only valid for ORDER_ACTIVITY. */
  Base_type_id base;            /* Only valid for activity ACTIVITY_BASE */
  enum direction8 dir;          /* Only valid for ORDER_MOVE. */
};

struct unit;
struct unit_list;

struct unit {
  struct unit_type *utype; /* Cannot be NULL. */
  struct tile *tile;
  enum direction8 facing;
  struct player *owner; /* Cannot be NULL. */
  int id;
  int homecity;

  int upkeep[O_LAST]; /* unit upkeep with regards to the homecity */

  int moves_left;
  int hp;
  int veteran;
  int fuel;

  struct tile *goto_tile; /* May be NULL. */

  enum unit_activity activity;

  /* The amount of work that has been done on the current activity.  This
   * is counted in turns but is multiplied by ACTIVITY_COUNT (which allows
   * fractional values in some cases). */
  int activity_count;

  enum tile_special_type activity_target;
  Base_type_id           activity_base;

  /* Previous activity, so it can be resumed without loss of progress
   * if the user changes their mind during a turn. */
  enum unit_activity changed_from;
  int changed_from_count;
  enum tile_special_type changed_from_target;
  Base_type_id           changed_from_base;

  bool ai_controlled; /* 0: not automated; 1: automated */
  bool moved;
  bool paradropped;

  /* This value is set if the unit is done moving for this turn. This
   * information is used by the client.  The invariant is:
   *   - If the unit has no more moves, it's done moving.
   *   - If the unit is on a goto but is waiting, it's done moving.
   *   - Otherwise the unit is not done moving. */
  bool done_moving;

  struct unit *transporter;   /* This unit is transported by ... */
  struct unit_list *transporting; /* This unit transports ... */

  /* The battlegroup ID: defined by the client but stored by the server. */
#define MAX_NUM_BATTLEGROUPS (4)
#define BATTLEGROUP_NONE (-1)
  int battlegroup;

  bool has_orders;
  struct {
    int length, index;
    bool repeat;   /* The path is to be repeated on completion. */
    bool vigilant; /* Orders should be cleared if an enemy is met. */
    struct unit_order *list;
  } orders;

  union {
    struct {
      /* Only used at the client (the server is omniscient; ./client/). */

      enum unit_focus_status focus_status;

      int transported_by; /* Used for unit_short_info packets where we can't
                           * be sure that the information about the
                           * transporter is known. */
      bool occupy;        /* TRUE if at least one cargo on the transporter. */

      /* Equivalent to pcity->client.color. Only for F_CITIES units. */
      bool colored;
      int color_index;

      bool asking_city_name;
    } client;

    struct {
      /* Only used in the server (./ai/ and ./server/). */

      bool debug;

      struct unit_adv *adv;
      void *ais[FC_AI_LAST];
      int birth_turn;

      /* ord_map and ord_city are the order index of this unit in tile.units
       * and city.units_supported; they are only used for save/reload */
      int ord_map;
      int ord_city;

      struct vision *vision;
      time_t action_timestamp;
      int action_turn;
    } server;
  };
};

#ifdef DEBUG
#define CHECK_UNIT(punit)                                                   \
  (fc_assert(punit != NULL),                                                \
   fc_assert(unit_type(punit) != NULL),                                     \
   fc_assert(unit_owner(punit) != NULL),                                    \
   fc_assert(player_by_number(player_index(unit_owner(punit)))              \
             == unit_owner(punit)),                                         \
   fc_assert(game_unit_by_number(punit->id) != NULL))
#else
#define CHECK_UNIT(punit) /* Do nothing */
#endif

bool is_real_activity(enum unit_activity activity);

/* Iterates over the types of unit activity. */
#define activity_type_iterate(act)					    \
{									    \
  Activity_type_id act;			         			    \
									    \
  for (act = 0; act < ACTIVITY_LAST; act++) {				    \
    if (is_real_activity(act)) {

#define activity_type_iterate_end     					    \
    }									    \
  }									    \
}

bool diplomat_can_do_action(const struct unit *pdiplomat,
			    enum diplomat_actions action,
			    const struct tile *ptile);
bool is_diplomat_action_available(const struct unit *pdiplomat,
				  enum diplomat_actions action,
				  const struct tile *ptile);

bool unit_can_help_build_wonder(const struct unit *punit,
				const struct city *pcity);
bool unit_can_help_build_wonder_here(const struct unit *punit);
bool unit_can_est_trade_route_here(const struct unit *punit);
enum unit_airlift_result
    test_unit_can_airlift_to(const struct player *restriction,
                             const struct unit *punit,
                             const struct city *pdest_city);
bool is_successful_airlift_result(enum unit_airlift_result result);
bool unit_can_airlift_to(const struct unit *punit, const struct city *pcity);
bool unit_has_orders(const struct unit *punit);

bool could_unit_load(const struct unit *pcargo, const struct unit *ptrans);
bool can_unit_load(const struct unit *punit, const struct unit *ptrans);
bool can_unit_unload(const struct unit *punit, const struct unit *ptrans);
bool can_unit_paradrop(const struct unit *punit);
bool can_unit_bombard(const struct unit *punit);
bool can_unit_change_homecity_to(const struct unit *punit,
				 const struct city *pcity);
bool can_unit_change_homecity(const struct unit *punit);
const char *get_activity_text(enum unit_activity activity);
bool can_unit_continue_current_activity(struct unit *punit);
bool can_unit_do_activity(const struct unit *punit,
			  enum unit_activity activity);
bool can_unit_do_activity_targeted(const struct unit *punit,
				   enum unit_activity activity,
				   enum tile_special_type target,
                                   Base_type_id base);
bool can_unit_do_activity_targeted_at(const struct unit *punit,
				      enum unit_activity activity,
				      enum tile_special_type target,
				      const struct tile *ptile,
                                      Base_type_id base);
bool can_unit_do_activity_base(const struct unit *punit,
                               Base_type_id base);
void set_unit_activity(struct unit *punit, enum unit_activity new_activity);
void set_unit_activity_targeted(struct unit *punit,
				enum unit_activity new_activity,
				enum tile_special_type new_target,
                                Base_type_id base);
void set_unit_activity_base(struct unit *punit,
                            Base_type_id base);
int get_activity_rate(const struct unit *punit);
int get_activity_rate_this_turn(const struct unit *punit);
int get_turns_for_activity_at(const struct unit *punit,
			      enum unit_activity activity,
			      const struct tile *ptile);
bool activity_requires_target(enum unit_activity activity);
bool can_unit_do_autosettlers(const struct unit *punit); 
bool is_unit_activity_on_tile(enum unit_activity activity,
			      const struct tile *ptile);
bv_special get_unit_tile_pillage_set(const struct tile *ptile);
bv_bases get_unit_tile_pillage_base_set(const struct tile *ptile);
bool is_attack_unit(const struct unit *punit);
bool is_military_unit(const struct unit *punit);           /* !set !dip !cara */
bool is_diplomat_unit(const struct unit *punit);
bool is_square_threatened(const struct player *pplayer,
			  const struct tile *ptile);
bool is_field_unit(const struct unit *punit);              /* ships+aero */
bool is_hiding_unit(const struct unit *punit);
bool unit_can_add_to_city(const struct unit *punit);
bool unit_can_build_city(const struct unit *punit);
bool unit_can_add_or_build_city(const struct unit *punit);
enum unit_add_build_city_result
unit_add_or_build_city_test(const struct unit *punit);
bool kills_citizen_after_attack(const struct unit *punit);

struct astring; /* Forward declaration. */
void unit_activity_astr(const struct unit *punit, struct astring *astr);
void unit_upkeep_astr(const struct unit *punit, struct astring *astr);
const char *unit_activity_text(const struct unit *punit);

int get_transporter_capacity(const struct unit *punit);

struct player *unit_owner(const struct unit *punit);
struct tile *unit_tile(const struct unit *punit);
void unit_tile_set(struct unit *punit, struct tile *ptile);

struct unit *is_allied_unit_tile(const struct tile *ptile,
				 const struct player *pplayer);
struct unit *is_enemy_unit_tile(const struct tile *ptile,
				const struct player *pplayer);
struct unit *is_non_allied_unit_tile(const struct tile *ptile,
				     const struct player *pplayer);
struct unit *is_non_attack_unit_tile(const struct tile *ptile,
				     const struct player *pplayer);
struct unit *unit_occupies_tile(const struct tile *ptile,
				const struct player *pplayer);

bool is_my_zoc(const struct player *unit_owner, const struct tile *ptile);
bool unit_being_aggressive(const struct unit *punit);
bool unit_type_really_ignores_zoc(const struct unit_type *punittype);

bool is_build_or_clean_activity(enum unit_activity activity);

struct unit *unit_virtual_create(struct player *pplayer, struct city *pcity,
                                 struct unit_type *punittype,
				 int veteran_level);
void unit_virtual_destroy(struct unit *punit);
bool unit_is_virtual(const struct unit *punit);
void free_unit_orders(struct unit *punit);

int get_transporter_occupancy(const struct unit *ptrans);
struct unit *transporter_for_unit(const struct unit *pcargo);

enum unit_upgrade_result unit_upgrade_test(const struct unit *punit,
                                           bool is_free);
enum unit_upgrade_result unit_upgrade_info(const struct unit *punit,
                                           char *buf, size_t bufsz);
bool unit_can_convert(const struct unit *punit);

bool is_losing_hp(const struct unit *punit);
bool unit_type_is_losing_hp(const struct player *pplayer,
                            const struct unit_type *punittype);

bool unit_alive(int id);

void *unit_ai_data(const struct unit *punit, const struct ai_type *ai);
void unit_set_ai_data(struct unit *punit, const struct ai_type *ai,
                      void *data);

int unit_bribe_cost(struct unit *punit);

bool unit_transport_load(struct unit *pcargo, struct unit *ptrans,
                         bool force);
bool unit_transport_unload(struct unit *pcargo);
struct unit *unit_transport_get(const struct unit *pcargo);
bool unit_transported(const struct unit *pcargo);
struct unit_list *unit_transport_cargo(const struct unit *ptrans);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__UNIT_H */
