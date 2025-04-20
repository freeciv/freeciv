/***********************************************************************
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
#include "actions.h"
#include "base.h"
#include "fc_interface.h"
#include "fc_types.h"
#include "map_types.h"
#include "terrain.h"            /* enum tile_special_type */
#include "unittype.h"
#include "vision.h"

struct road_type;
struct unit_move_data; /* Actually defined in "server/unittools.c". */

/* Changing this enum will break network compatibility.
 * Different orders take different parameters; see struct unit_order. */
enum unit_orders {
  /* Move without performing any action (dir) */
  ORDER_MOVE = 0,
  /* Perform activity (activity, extra) */
  ORDER_ACTIVITY = 1,
  /* Pause to regain movement points (no parameters) */
  ORDER_FULL_MP = 2,
  /* Move; if necessary prompt for action/target when order executed (dir) */
  ORDER_ACTION_MOVE = 3,
  /* Perform pre-specified action (action, target tile, sub target) */
  ORDER_PERFORM_ACTION = 4,
  /* and plenty more for later... */
  ORDER_LAST
};

enum unit_focus_status {
  FOCUS_AVAIL, FOCUS_WAIT, FOCUS_DONE
};

enum goto_route_type {
  ROUTE_GOTO, ROUTE_PATROL
};

enum unit_upgrade_result {
  UU_OK,
  UU_NO_UNITTYPE,
  UU_NO_MONEY,
  UU_NOT_IN_CITY,
  UU_NOT_CITY_OWNER,
  UU_NOT_ENOUGH_ROOM,
  UU_NOT_TERRAIN,          /* The upgraded unit could not survive. */
  UU_UNSUITABLE_TRANSPORT, /* Can't upgrade inside current transport. */
  UU_NOT_ACTIVITY          /* Can't upgrade during this activity (e.g., CONVERT) */
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
  /* Only valid for ORDER_PERFORM_ACTION. Validity and meaning depend on
   * 'action'. See action_target_kind and action_sub_target_kind */
  int target;
  int sub_target;
  /* Only valid for ORDER_PERFORM_ACTION and ORDER_ACTIVITY
   * TODO: Drop ORDER_ACTIVITY, always have activities as result of actions. */
  int action;
  /* Valid for ORDER_MOVE and ORDER_ACTION_MOVE. */
  enum direction8 dir;
};

/* Used in the network protocol */
#define SPECENUM_NAME unit_ss_data_type
/* The player wants to be reminded to ask what actions the unit can perform
 * to a certain target tile. */
#define SPECENUM_VALUE0 USSDT_QUEUE
/* The player no longer wants the reminder to ask what actions the unit can
 * perform to a certain target tile. */
#define SPECENUM_VALUE1 USSDT_UNQUEUE
/* The player wants to record that the unit now belongs to the specified
 * battle group. */
#define SPECENUM_VALUE2 USSDT_BATTLE_GROUP
/* The player wants the unit to stop bothering them unless:
 *  - a hostile units comes within 3 tiles
 *  - the unit has its hitpoints restored
 *  - the unit is bounced
 * if 1, 0 to still be bothered. */
#define SPECENUM_VALUE3 USSDT_SENTRY
#include "specenum_gen.h"

/* Used in the network protocol */
#define SPECENUM_NAME server_side_agent
#define SPECENUM_VALUE0 SSA_NONE
#define SPECENUM_VALUE0NAME N_("?serveragent:None")
#define SPECENUM_VALUE1 SSA_AUTOWORKER
#define SPECENUM_VALUE1NAME N_("AutoWorker")
#define SPECENUM_VALUE2 SSA_AUTOEXPLORE
#define SPECENUM_VALUE2NAME N_("Autoexplore")
#define SPECENUM_COUNT SSA_COUNT
#include "specenum_gen.h"

struct unit;
struct unit_list;

struct unit {
  const struct unit_type *utype; /* Cannot be NULL. */
  struct tile *tile;
  int refcount;
  enum direction8 facing;
  struct player *owner; /* Cannot be NULL. */
  struct player *nationality;
  int id;
  int homecity;

  int upkeep[O_LAST]; /* unit upkeep with regards to the homecity */

  int moves_left;
  int hp;
  int veteran;
  int fuel;

  struct tile *goto_tile; /* May be NULL. */

  enum unit_activity activity;
  enum gen_action action;

  /* The amount of work that has been done on the current activity.  This
   * is counted in turns but is multiplied by ACTIVITY_FACTOR (which allows
   * fractional values in some cases). */
  int activity_count;

  struct extra_type *activity_target;

  /* Previous activity, so it can be resumed without loss of progress
   * if the user changes their mind during a turn. */
  enum unit_activity changed_from;
  int changed_from_count;
  struct extra_type *changed_from_target;

  enum server_side_agent ssa_controller;
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

  struct goods_type *carrying;

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

  /* The unit may want the player to choose an action. */
  enum action_decision action_decision_want;
  struct tile *action_decision_tile;

  bool stay; /* Unit is prohibited from moving */

  int birth_turn;
  int current_form_turn;

  union {
    struct {
      /* Only used at the client (the server is omniscient; ./client/). */

      enum unit_focus_status focus_status;

      int transported_by; /* Used for unit_short_info packets where we can't
                           * be sure that the information about the
                           * transporter is known. */
      bool occupied;      /* TRUE if at least one cargo on the transporter. */

      /* Equivalent to pcity->client.color. Only for cityfounder units. */
      bool colored;
      int color_index;

      bool asking_city_name;

      /* Used in a follow up question about a selected action. */
      struct act_prob *act_prob_cache;
    } client;

    struct {
      /* Only used in the server (./ai/ and ./server/). */

      bool debug;

      struct unit_adv *adv;
      void *ais[FREECIV_AI_MOD_LAST];

      /* ord_map and ord_city are the order index of this unit in tile.units
       * and city.units_supported; they are only used for save/reload */
      int ord_map;
      int ord_city;

      struct vision *vision;
      time_t action_timestamp;
      int action_turn;
      struct unit_move_data *moving;

      /* The unit is in the process of dying. */
      bool dying;

      /* Call back to run on unit removal. */
      void (*removal_callback)(struct unit *punit);

      /* The upkeep that actually was paid. */
      int upkeep_paid[O_LAST];
    } server;
  };
};

#ifdef FREECIV_DEBUG
#define CHECK_UNIT(punit)                                                   \
  (fc_assert(punit != NULL),                                                \
   fc_assert(unit_type_get(punit) != NULL),                                 \
   fc_assert(unit_owner(punit) != NULL),                                    \
   fc_assert(player_by_number(player_index(unit_owner(punit)))              \
             == unit_owner(punit)),                                         \
   fc_assert(game_unit_by_number(punit->id) != NULL))
#else  /* FREECIV_DEBUG */
#define CHECK_UNIT(punit) /* Do nothing */
#endif /* FREECIV_DEBUG */

#define activity_type_list_iterate(_act_list_, _act_)                        \
{                                                                            \
  int _act_i_;                                                               \
  for (_act_i_ = 0; _act_list_[_act_i_] != ACTIVITY_LAST; _act_i_++) {       \
    Activity_type_id _act_ = _act_list_[_act_i_];

#define activity_type_list_iterate_end                                       \
  }                                                                          \
}

/* Iterates over the types of unit activity. */
#define activity_type_iterate(_act_)					    \
{									    \
  Activity_type_id _act_;                                                   \
  for (_act_ = 0; _act_ != ACTIVITY_LAST; _act_++) {                        \

#define activity_type_iterate_end                                           \
  }                                                                         \
}

extern const Activity_type_id tile_changing_activities[];

#define tile_changing_activities_iterate(_act_)                             \
{                                                                           \
  activity_type_list_iterate(tile_changing_activities, _act_)

#define tile_changing_activities_iterate_end                                \
  activity_type_list_iterate_end                                            \
}

bool are_unit_orders_equal(const struct unit_order *order1,
                           const struct unit_order *order2);

int unit_shield_value(const struct unit *punit,
                      const struct unit_type *punittype,
                      const struct action *paction);
bool unit_can_help_build_wonder_here(const struct civ_map *nmap,
                                     const struct unit *punit);
bool unit_can_est_trade_route_here(const struct unit *punit);
enum unit_airlift_result
    test_unit_can_airlift_to(const struct civ_map *nmap,
                             const struct player *restriction,
                             const struct unit *punit,
                             const struct city *pdest_city);
bool unit_can_airlift_to(const struct civ_map *nmap,
                         const struct unit *punit, const struct city *pcity)
  fc__attribute((nonnull (3)));
bool unit_has_orders(const struct unit *punit);

bool could_unit_be_in_transport(const struct unit *pcargo,
                                const struct unit *ptrans);
bool could_unit_load(const struct unit *pcargo, const struct unit *ptrans);
bool can_unit_load(const struct unit *punit, const struct unit *ptrans);
bool can_unit_unload(const struct unit *punit, const struct unit *ptrans);
bool can_unit_deboard_or_be_unloaded(const struct civ_map *nmap,
                                     const struct unit *pcargo,
                                     const struct unit *ptrans);
bool can_unit_teleport(const struct civ_map *nmap, const struct unit *punit);
bool can_unit_paradrop(const struct civ_map *nmap, const struct unit *punit);
bool can_unit_change_homecity_to(const struct civ_map *nmap,
                                 const struct unit *punit,
                                 const struct city *pcity);
bool can_unit_change_homecity(const struct civ_map *nmap,
                              const struct unit *punit);
const char *get_activity_text(enum unit_activity activity);
bool can_unit_continue_current_activity(const struct civ_map *nmap,
                                        struct unit *punit);
bool can_unit_do_activity(const struct civ_map *nmap,
                          const struct unit *punit,
                          enum unit_activity activity,
                          enum gen_action action);
bool can_unit_do_activity_targeted(const struct civ_map *nmap,
                                   const struct unit *punit,
                                   enum unit_activity activity,
                                   enum gen_action action,
                                   struct extra_type *target);
bool can_unit_do_activity_targeted_at(const struct civ_map *nmap,
                                      const struct unit *punit,
                                      enum unit_activity activity,
                                      enum gen_action action,
                                      struct extra_type *target,
                                      const struct tile *ptile);
void set_unit_activity(struct unit *punit, enum unit_activity new_activity,
                       enum gen_action trigger_action);
void set_unit_activity_targeted(struct unit *punit,
                                enum unit_activity new_activity,
                                struct extra_type *new_target,
                                enum gen_action trigger_action);
int get_activity_rate(const struct unit *punit);
int get_activity_rate_this_turn(const struct unit *punit);
int get_turns_for_activity_at(const struct unit *punit,
                              enum unit_activity activity,
                              const struct tile *ptile,
                              struct extra_type *tgt);
bool activity_requires_target(enum unit_activity activity);
bool can_unit_do_autoworker(const struct unit *punit);
bool is_unit_activity_on_tile(enum unit_activity activity,
                              const struct tile *ptile);
bv_extras get_unit_tile_pillage_set(const struct tile *ptile);
bool is_attack_unit(const struct unit *punit);
bool is_martial_law_unit(const struct unit *punit);
bool is_occupying_unit(const struct unit *punit);
bool is_enter_borders_unit(const struct unit *punit);
bool is_guard_unit(const struct unit *punit);
bool is_special_unit(const struct unit *punit);
bool is_flagless_to_player(const struct unit *punit,
                           const struct player *pplayer);
bool unit_can_do_action(const struct unit *punit,
                        const action_id act_id);
bool unit_can_do_action_result(const struct unit *punit,
                               enum action_result result);
bool unit_can_do_action_sub_result(const struct unit *punit,
                                   enum action_sub_result sub_result);
bool is_square_threatened(const struct civ_map *nmap,
                          const struct player *pplayer,
                          const struct tile *ptile, bool omniscient);
bool is_field_unit(const struct unit *punit);
bool is_hiding_unit(const struct unit *punit);
bool unit_can_add_or_build_city(const struct civ_map *nmap,
                                const struct unit *punit);

struct astring; /* Forward declaration. */

void unit_activity_astr(const struct unit *punit, struct astring *astr);
void unit_upkeep_astr(const struct unit *punit, struct astring *astr);

int get_transporter_capacity(const struct unit *punit);

#define unit_home(_pu_) (game_city_by_number((_pu_)->homecity))
#define is_unit_homeless(_pu_) (punit->homecity == IDENTITY_NUMBER_ZERO)
#define unit_owner(_pu) ((_pu)->owner)
#define unit_tile(_pu) ((_pu)->tile)
struct player *unit_nationality(const struct unit *punit);
void unit_tile_set(struct unit *punit, struct tile *ptile);


struct unit *tile_allied_unit(const struct tile *ptile,
                              const struct player *pplayer);

/**********************************************************************//**
  Are there allied units, and only allied units, on the tile?
**************************************************************************/
static inline bool is_allied_unit_tile(const struct tile *ptile,
                                       const struct player *pplayer)
{
  return NULL != tile_allied_unit(ptile, pplayer);
}

struct unit *tile_enemy_unit(const struct tile *ptile,
                             const struct player *pplayer);

/**********************************************************************//**
  Are there any enemy unit(s) on tile?
**************************************************************************/
static inline bool is_enemy_unit_tile(const struct tile *ptile,
                                      const struct player *pplayer)
{
  return NULL != tile_enemy_unit(ptile, pplayer);
}

struct unit *tile_non_allied_unit(const struct tile *ptile,
                                  const struct player *pplayer,
                                  bool everyone_non_allied);

/**********************************************************************//**
  Are there any non-allied unit(s) on tile?
**************************************************************************/
static inline bool is_non_allied_unit_tile(const struct tile *ptile,
                                           const struct player *pplayer,
                                           bool everyone_non_allied)
{
  return NULL != tile_non_allied_unit(ptile, pplayer, everyone_non_allied);
}

struct unit *tile_other_players_unit(const struct tile *ptile,
                                     const struct player *pplayer);

/**********************************************************************//**
  Are there any unit(s) on tile owned by someone else than pplayer?
**************************************************************************/
static inline bool is_other_players_unit_tile(const struct tile *ptile,
                                              const struct player *pplayer)
{
  return NULL != tile_other_players_unit(ptile, pplayer);
}

struct unit *tile_non_attack_unit(const struct tile *ptile,
                                  const struct player *pplayer);

/**********************************************************************//**
  Are there any unit(s) on tile preventing player's units from
  moving towards, either to attack enemy or to enter the tile with allies.
**************************************************************************/
static inline bool is_non_attack_unit_tile(const struct tile *ptile,
                                           const struct player *pplayer)
{
  return NULL != tile_non_attack_unit(ptile, pplayer);
}

struct unit *unit_occupies_tile(const struct tile *ptile,
                                const struct player *pplayer);

bool is_plr_zoc_srv(const struct player *unit_owner, const struct tile *ptile,
                    const struct civ_map *zmap);
bool is_plr_zoc_client(const struct player *unit_owner, const struct tile *ptile,
                       const struct civ_map *zmap);

/**********************************************************************//**
  Is this square controlled by the unit_owner? This function can be
  used both by the server and the client side.

  Here "is_my_zoc" means essentially a square which is *not* adjacent to an
  enemy unit (that has a ZOC) on a terrain that has zoc rules.
**************************************************************************/
static inline bool is_my_zoc(const struct player *unit_owner, const struct tile *ptile,
                             const struct civ_map *zmap)
{
  return is_server()
    ? is_plr_zoc_srv(unit_owner, ptile, zmap) : is_plr_zoc_client(unit_owner, ptile, zmap);
}

bool unit_being_aggressive(const struct civ_map *nmap,
                           const struct unit *punit);
bool unit_type_really_ignores_zoc(const struct unit_type *punittype);

bool is_build_activity(enum unit_activity activity);
bool is_clean_activity(enum unit_activity activity);
bool is_terrain_change_activity(enum unit_activity activity);
bool is_tile_activity(enum unit_activity activity);
bool is_targeted_activity(enum unit_activity activity);

struct unit *unit_virtual_create(struct player *pplayer, struct city *pcity,
                                 const struct unit_type *punittype,
                                 int veteran_level);
void unit_virtual_destroy(struct unit *punit);
bool unit_is_virtual(const struct unit *punit);
void free_unit_orders(struct unit *punit);

int get_transporter_occupancy(const struct unit *ptrans);
struct unit *transporter_for_unit(const struct unit *pcargo);
struct unit *transporter_for_unit_at(const struct unit *pcargo,
                                     const struct tile *ptile);

enum unit_upgrade_result
unit_transform_result(const struct civ_map *nmap,
                      const struct unit *punit,
                      const struct unit_type *to_unittype);
enum unit_upgrade_result unit_upgrade_test(const struct civ_map *nmap,
                                           const struct unit *punit,
                                           bool is_free);
enum unit_upgrade_result unit_upgrade_info(const struct civ_map *nmap,
                                           const struct unit *punit,
                                           char *buf, size_t bufsz);
bool unit_can_convert(const struct civ_map *nmap, const struct unit *punit);

int unit_pays_mp_for_action(const struct action *paction,
                            const struct unit *punit);

int hp_gain_coord(const struct unit *punit);
int unit_gain_hitpoints(const struct unit *punit);
bool is_losing_hp(const struct unit *punit);
bool unit_type_is_losing_hp(const struct player *pplayer,
                            const struct unit_type *punittype);

bool unit_is_alive(int id);

void *unit_ai_data(const struct unit *punit, const struct ai_type *ai);
void unit_set_ai_data(struct unit *punit, const struct ai_type *ai,
                      void *data);

int unit_bribe_cost(const struct unit *punit, const struct player *briber,
                    const struct unit *briber_unit);
int stack_bribe_cost(const struct tile *ptile, const struct player *briber,
                     const struct unit *briber_unit);

int unit_upkeep_cost(const struct unit *punit, Output_type_id otype);

bool unit_transport_load(struct unit *pcargo, struct unit *ptrans,
                         bool force);
bool unit_transport_unload(struct unit *pcargo);
struct unit *unit_transport_get(const struct unit *pcargo);

#define unit_transported_server(_pcargo_) ((_pcargo_)->transporter != NULL)

/* Evaluates parameter twice! */
#define unit_transported_client(_pcargo_)  \
  ((_pcargo_)->client.transported_by != -1 \
   || (_pcargo_)->transporter != NULL)

bool unit_transported(const struct unit *pcargo);
struct unit_list *unit_transport_cargo(const struct unit *ptrans);
bool unit_transport_check(const struct unit *pcargo,
                          const struct unit *ptrans);
bool unit_contained_in(const struct unit *pcargo, const struct unit *ptrans);
int unit_cargo_depth(const struct unit *pcargo);
int unit_transport_depth(const struct unit *ptrans);

bool unit_is_cityfounder(const struct unit *punit);

/* Iterate all transporters carrying '_pcargo', directly or indirectly. */
#define unit_transports_iterate(_pcargo, _ptrans) {                         \
  struct unit *_ptrans;                                                     \
  for (_ptrans = unit_transport_get(_pcargo); NULL != _ptrans;              \
       _ptrans = unit_transport_get(_ptrans)) {
#define unit_transports_iterate_end }}

struct cargo_iter;
size_t cargo_iter_sizeof(void) fc__attribute((const));

struct iterator *cargo_iter_init(struct cargo_iter *iter,
                                 const struct unit *ptrans);
#define unit_cargo_iterate(_ptrans, _pcargo)                                \
  generic_iterate(struct cargo_iter, struct unit *, _pcargo,                \
                  cargo_iter_sizeof, cargo_iter_init, _ptrans)
#define unit_cargo_iterate_end generic_iterate_end

bool unit_order_list_is_sane(const struct civ_map *nmap,
                             int length, const struct unit_order *orders);
struct unit_order *create_unit_orders(const struct civ_map *nmap,
                                      int length,
                                      const struct unit_order *orders);

enum gen_action activity_default_action(enum unit_activity act);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__UNIT_H */
