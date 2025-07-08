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
#ifndef FC__MOVEMENT_H
#define FC__MOVEMENT_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* common */
#include "fc_types.h"
#include "map.h"

struct tile;

#define SINGLE_MOVE     (terrain_control.move_fragments)
#define MOVE_COST_IGTER (terrain_control.igter_cost)
/* packets.def MOVEFRAGS */
#define MAX_MOVE_FRAGS  65535

struct unit_type;
struct terrain;

enum unit_move_result {
  MR_OK,
  MR_DEATH,
  MR_PAUSE,
  MR_NO_WAR,    /* Can't move here without declaring war. */
  MR_PEACE,     /* Can't move here because of a peace treaty. */
  MR_ZOC,
  MR_BAD_ACTIVITY,
  MR_BAD_DESTINATION,
  MR_BAD_MAP_POSITION,
  MR_DESTINATION_OCCUPIED_BY_NON_ALLIED_CITY,
  MR_DESTINATION_OCCUPIED_BY_NON_ALLIED_UNIT,
  MR_NO_TRANSPORTER_CAPACITY,
  MR_TRIREME,
  MR_CANNOT_DISEMBARK,
  MR_NON_NATIVE_MOVE,  /* Usually RMM_RELAXED road diagonally without link */
  MR_ANIMAL_DISALLOWED,
  MR_UNIT_STAY,
  MR_RANDOM_ONLY,
  MR_NOT_ALLOWED
};

int utype_move_rate(const struct unit_type *utype, const struct tile *ptile,
                    const struct player *pplayer, int veteran_level,
                    int hitpoints);
int unit_move_rate(const struct unit *punit);
int utype_unknown_move_cost(const struct unit_type *utype);

bool unit_can_defend_here(const struct civ_map *nmap, const struct unit *punit);
bool can_attack_non_native_hard_reqs(const struct unit_type *utype);
bool can_attack_non_native(const struct unit_type *utype);
bool can_attack_from_non_native(const struct unit_type *utype);

bool is_city_channel_tile(const struct civ_map *nmap,
                          const struct unit_class *punitclass,
                          const struct tile *ptile,
                          const struct tile *pexclude);
bool may_be_city_channel_tile(const struct civ_map *nmap,
                              const struct unit_class *punitclass,
                              const struct tile *ptile,
                              const struct player *pov_player);

bool is_native_tile(const struct unit_type *punittype,
                    const struct tile *ptile);

bool is_native_to_class(const struct unit_class *punitclass,
                        const struct terrain *pterrain,
                        const bv_extras *extras);

/****************************************************************************
  Check if this tile is native to given unit class.

  See is_native_to_class()
****************************************************************************/
static inline bool is_native_tile_to_class(const struct unit_class *punitclass,
                                           const struct tile *ptile)
{
  return is_native_to_class(punitclass, tile_terrain(ptile),
                            tile_extras(ptile));
}

bool is_native_move(const struct civ_map *nmap,
                    const struct unit_class *punitclass,
                    const struct tile *src_tile,
                    const struct tile *dst_tile);
bool is_native_near_tile(const struct civ_map *nmap,
                         const struct unit_class *uclass,
                         const struct tile *ptile);
bool can_exist_at_tile(const struct civ_map *nmap,
                       const struct unit_type *utype,
                       const struct tile *ptile);
bool could_exist_in_city(const struct civ_map *nmap,
                         const struct player *pov_player,
                         const struct unit_type *utype,
                         const struct city *pcity);
bool can_unit_exist_at_tile(const struct civ_map *nmap,
                            const struct unit *punit, const struct tile *ptile);
bool can_unit_survive_at_tile(const struct civ_map *nmap,
                              const struct unit *punit,
                              const struct tile *ptile);
bool can_step_taken_wrt_to_zoc(const struct unit_type *punittype,
                               const struct player *unit_owner,
                               const struct tile *src_tile,
                               const struct tile *dst_tile,
                               const struct civ_map *zmap);
bool unit_can_move_to_tile(const struct civ_map *nmap,
                           const struct unit *punit,
                           const struct tile *ptile,
                           bool igzoc,
                           bool enter_transport,
                           bool enter_enemy_city);
bool unit_can_teleport_to_tile(const struct civ_map *nmap,
                               const struct unit *punit,
                               const struct tile *ptile,
                               bool enter_transport,
                               bool enter_enemy_city);
enum unit_move_result
unit_move_to_tile_test(const struct civ_map *nmap,
                       const struct unit *punit,
                       enum unit_activity activity,
                       const struct tile *src_tile,
                       const struct tile *dst_tile,
                       bool igzoc,
                       bool enter_transport, struct unit *embark_to,
                       bool enter_enemy_city);
enum unit_move_result
unit_teleport_to_tile_test(const struct civ_map *nmap,
                           const struct unit *punit,
                           enum unit_activity activity,
                           const struct tile *src_tile,
                           const struct tile *dst_tile,
                           bool enter_transport, struct unit *embark_to,
                           bool enter_enemy_city);
bool can_unit_transport(const struct unit *transporter, const struct unit *transported);
bool can_unit_type_transport(const struct unit_type *transporter,
                             const struct unit_class *transported);
bool unit_can_load(const struct unit *punit);
bool unit_could_load_at(const struct unit *punit, const struct tile *ptile);

void init_move_fragments(void);
const char *move_points_text_full(int mp, bool reduce, const char *prefix,
                                  const char *none, bool align);
const char *move_points_text(int mp, bool reduce);

/* Simple algorithm that checks connectivity of _tile_ on _map_
 * to a tile where _found_ is true via adjacency of tiles where _conn_.
 * _found_ and _conn_ must be boolean expressions checking tile _piter_.
 * _conn_ is not checked at starting and finishing _tile_.
 * Assigns the found tile to _tile_, or nullptr if not found */
#define MAP_TILE_CONN_TO_BY(_map_, _tile_, _piter_, _found_, _conn_) {\
  struct dbv tile_processed;                                          \
  struct tile_list *process_queue = tile_list_new();                  \
  bool found = FALSE;                                                 \
                                                                      \
  dbv_init(&tile_processed, map_num_tiles());                         \
  for (;;) {                                                          \
    dbv_set(&tile_processed, tile_index(_tile_));                     \
    adjc_iterate(_map_, _tile_, _piter_) {                            \
      if (dbv_isset(&tile_processed, tile_index(_piter_))) {          \
        continue;                                                     \
      } else if (_found_) {                                           \
        _tile_ = _piter_;                                             \
        found = TRUE;                                                 \
        break;                                                        \
      } else if (_conn_) {                                            \
        tile_list_append(process_queue, _piter_);                     \
      } else {                                                        \
        dbv_set(&tile_processed, tile_index(_piter_));                \
      }                                                               \
    } adjc_iterate_end;                                               \
                                                                      \
    if (found) {                                                      \
      /* got it*/                                                     \
      break;                                                          \
    }                                                                 \
    if (0 == tile_list_size(process_queue)) {                         \
      /* No more tile to process. */                                  \
      _tile_ = nullptr;                                               \
       break;                                                         \
    } else {                                                          \
      _tile_ = tile_list_front(process_queue);                        \
      tile_list_pop_front(process_queue);                             \
    }                                                                 \
  }                                                                   \
                                                                      \
  dbv_free(&tile_processed);                                          \
  tile_list_destroy(process_queue);                                   \
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__MOVEMENT_H */
