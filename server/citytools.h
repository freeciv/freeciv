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
#ifndef FC__CITYTOOLS_H
#define FC__CITYTOOLS_H

/* common */
#include "events.h"             /* enum event_type */
#include "packets.h"
#include "unitlist.h"

#define LOG_BUILD_TARGET LOG_DEBUG

#define SPECLIST_TAG trade_route_packet
#define SPECLIST_TYPE struct packet_trade_route_info
#include "speclist.h"
#define trade_route_packet_list_iterate(ptrlist, ptr) \
    TYPED_LIST_ITERATE(struct packet_trade_route_info, ptrlist, ptr)
#define trade_route_packet_list_iterate_end LIST_ITERATE_END

int build_points_left(struct city *pcity);

void transfer_city_units(struct player *pplayer, struct player *pvictim,
                         struct unit_list *units, struct city *pcity,
                         struct city *exclude_city,
                         int kill_outside, bool verbose);
bool transfer_city(struct player *ptaker, struct city *pcity,
                   int kill_outside, bool transfer_unit_verbose,
                   bool resolve_stack, bool raze, bool build_free);
struct city *find_closest_city(const struct tile *ptile,
                               const struct city *pexclcity,
                               const struct player *pplayer,
                               bool only_ocean, bool only_continent,
                               bool only_known, bool only_player,
                               bool only_enemy,
                               const struct unit_class *pclass);
bool unit_conquer_city(struct unit *punit, struct city *pcity);

bool send_city_suppression(bool now);
void send_city_info(struct player *dest, struct city *pcity);
void send_city_info_at_tile(struct player *pviewer, struct conn_list *dest,
                            struct city *pcity, struct tile *ptile);
void send_all_known_cities(struct conn_list *dest);
void send_player_cities(struct player *pplayer);
void broadcast_city_info(struct city *pcity);
void package_city(struct city *pcity, struct packet_city_info *packet,
                  struct packet_city_nationalities *nat_packet,
                  struct packet_city_rally_point *rally_packet,
                  struct packet_web_city_info_addition *web_packet,
                  struct trade_route_packet_list *routes,
                  bool dipl_invest);

void reality_check_city(struct player *pplayer, struct tile *ptile);
bool update_dumb_city(struct player *pplayer, struct city *pcity);
void refresh_dumb_city(struct city *pcity);
void remove_dumb_city(struct player *pplayer, struct tile *ptile);

void city_build_free_buildings(struct city *pcity);

void create_city(struct player *pplayer, struct tile *ptile,
                 const char *name, struct player *nationality);
bool create_city_for_player(struct player *pplayer, struct tile *ptile,
                            const char *name, struct player *nationality);
void remove_city(struct city *pcity);

struct trade_route *remove_trade_route(struct city *pc1,
                                       struct trade_route *proute,
                                       bool announce, bool source_gone);

bool city_illness_strike(struct city *pcity);

void do_sell_building(struct player *pplayer, struct city *pcity,
                      struct impr_type *pimprove, const char *reason);
struct city *build_or_move_building(struct city *pcity,
                                    struct impr_type *pimprove,
                                    struct player **oldcity_owner);
bool building_removed(struct city *pcity, const struct impr_type *pimprove,
                      const char *reason, struct unit *destroyer);
void building_lost(struct city *pcity, const struct impr_type *pimprove,
                   const char *reason, struct unit *destroyer);
void update_unit_upkeep(struct unit *punit, int free_uk[O_LAST]);
void city_units_upkeep(const struct city *pcity);

bool is_production_equal(const struct universal *one,
                         const struct universal *two);
void change_build_target(struct player *pplayer, struct city *pcity,
                         struct universal *target,
                         enum event_type event);

bool is_allowed_city_name(struct player *pplayer, const char *cityname,
                          char *error_buf, size_t bufsz);
const char *city_name_suggestion(struct player *pplayer, struct tile *ptile);

void city_freeze_workers(struct city *pcity);
void city_thaw_workers(struct city *pcity);
void city_freeze_workers_queue(struct city *pcity);
void city_thaw_workers_queue(void);

/* City map functions */
void city_map_update_empty(struct city *pcity, struct tile *ptile);
void city_map_update_worker(struct city *pcity, struct tile *ptile);

bool city_map_update_tile_frozen(struct tile *ptile);
bool city_map_update_tile_now(struct tile *ptile);

void city_map_update_all(struct city *pcity);
void city_map_update_all_cities_for_player(struct player *pplayer);

bool city_map_update_radius_sq(struct city *pcity);

void city_landlocked_sell_coastal_improvements(struct tile *ptile);
void city_refresh_vision(struct city *pcity);
void refresh_player_cities_vision(struct player *pplayer);

void sync_cities(void);

void clear_worker_task(struct city *pcity, struct worker_task *ptask);
void clear_worker_tasks(struct city *pcity);
void package_and_send_worker_tasks(struct city *pcity);

int city_production_buy_gold_cost(const struct city *pcity);

void city_add_improvement_with_gov_notice(struct city *pcity,
                                          const struct impr_type *pimprove,
                                          const char *format);

int city_original_owner(const struct city *pcity,
                        const struct player *known_for);

#endif /* FC__CITYTOOLS_H */
