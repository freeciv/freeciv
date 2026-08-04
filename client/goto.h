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
#ifndef FC__GOTO_H
#define FC__GOTO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pf_path;
struct city;
struct tile;
struct unit;
struct unit_list;

struct client_goto_pathfinder;

struct client_goto_path_info {
  int order_count;
  int destination_tile;
  uint64_t orders_digest;
  uint64_t route_signature;
  bool action_move;
};

struct client_rally_plan;

#define CLIENT_UNIT_ROUTE_MAX_WAYPOINTS 64

enum client_unit_route_mode {
  CLIENT_UNIT_ROUTE_GOTO,
  CLIENT_UNIT_ROUTE_PATROL
};

struct client_unit_route_plan;

struct client_unit_route_plan_info {
  int source_tile;
  int destination_tile;
  int target_tile;
  int waypoint_count;
  int order_count;
  uint64_t orders_digest;
  uint64_t route_signature;
  int final_action;
  int final_subtarget;
  bool action_move;
  bool repeat;
  bool vigilant;
};

struct client_rally_plan_info {
  int source_city_tile;
  int production_unit_type;
  int veteran_level;
  int target_tile;
  int order_count;
  uint64_t orders_digest;
  bool action_move;
};

enum goto_tile_state {
  GTS_TURN_STEP,
  GTS_MP_LEFT,
  GTS_EXHAUSTED_MP,

  GTS_COUNT
};

void init_client_goto(void);
void free_client_goto(void);

void enter_goto_state(struct unit_list *punits);
void exit_goto_state(void);

void goto_unit_killed(struct unit *punit);

bool goto_is_active(void);
bool goto_get_turns(int *min, int *max);
bool goto_tile_state(const struct tile *ptile, enum goto_tile_state *state,
                     int *turns, bool *waypoint);
bool goto_add_waypoint(void);
bool goto_pop_waypoint(void);

bool is_valid_goto_destination(const struct tile *ptile);
bool is_valid_goto_draw_line(struct tile *dest_tile);

void request_orders_cleared(struct unit *punit);
struct client_goto_pathfinder *
client_goto_pathfinder_new(const struct unit *punit);
void client_goto_pathfinder_destroy(struct client_goto_pathfinder *finder);
bool client_goto_pathfinder_destination(
  struct client_goto_pathfinder *finder, const struct tile *ptile,
  struct client_goto_path_info *info);
struct client_unit_route_plan *
client_unit_route_plan_new(struct unit *punit,
                           enum client_unit_route_mode mode,
                           struct tile *const *waypoints,
                           size_t waypoint_count);
struct client_unit_route_plan *
client_unit_goto_plan_new(struct unit *punit, struct tile *target);
struct client_unit_route_plan *
client_unit_action_route_plan_new(struct unit *punit, struct tile *target,
                                  int action, int subtarget);
struct client_unit_route_plan *
client_unit_connect_plan_new(struct unit *punit, struct tile *target,
                             enum unit_activity activity,
                             struct extra_type *extra);
void client_unit_route_plan_destroy(struct client_unit_route_plan *plan);
const struct client_unit_route_plan_info *
client_unit_route_plan_get_info(const struct client_unit_route_plan *plan);
int client_unit_route_plan_send(struct client_unit_route_plan *plan);
void send_goto_path(struct unit *punit, struct pf_path *path,
                    struct unit_order *last_order);
bool send_goto_tile(struct unit *punit, struct tile *ptile);
struct client_rally_plan *
client_rally_plan_new(struct city *pcity, const struct tile *ptile);
void client_rally_plan_destroy(struct client_rally_plan *plan);
const struct client_rally_plan_info *
client_rally_plan_get_info(const struct client_rally_plan *plan);
bool client_rally_plan_matches_city(const struct client_rally_plan *plan,
                                    const struct city *pcity,
                                    bool persistent);
int client_rally_plan_send(struct client_rally_plan *plan, bool persistent);
int client_rally_point_clear_forced(const struct city *pcity);
bool send_rally_tile(struct city *pcity, struct tile *ptile, bool persistent);
bool send_attack_tile(struct unit *punit, struct tile *ptile);
void send_patrol_route(void);
void send_goto_route(void);
void send_connect_route(enum unit_activity activity,
                        struct extra_type *tgt);

struct pf_path *path_to_nearest_allied_city(struct unit *punit);
struct tile *tile_before_end_path(struct unit *punit, struct tile *ptile);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__GOTO_H */
