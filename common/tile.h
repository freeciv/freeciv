/********************************************************************** 
 Freeciv - Copyright (C) 2005 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifndef FC__TILE_H
#define FC__TILE_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* utility */
#include "bitvector.h"

/* common */
#include "base.h"
#include "fc_types.h"
#include "player.h"
#include "road.h"
#include "terrain.h"
#include "unitlist.h"

/* network, order dependent */
enum known_type {
 TILE_UNKNOWN = 0,
 TILE_KNOWN_UNSEEN = 1,
 TILE_KNOWN_SEEN = 2,
};

/* Convenience macro for accessing tile coordinates.  This should only be
 * used for debugging. */
#define TILE_XY(ptile) ((ptile) ? index_to_map_pos_x(tile_index(ptile))      \
                                : -1),                                       \
                       ((ptile) ? index_to_map_pos_y(tile_index(ptile))      \
                                : -1)

struct tile {
  int index; /* Index coordinate of the tile. Used to calculate (x, y) pairs
              * (index_to_map_pos()) and (nat_x, nat_y) pairs
              * (index_to_native_pos()). */
  Continent_id continent;
  bv_special special;
  bv_bases bases;
  /* We're in transition from storing roads in specials to storing roads
   * in roads vector. Roads vector does not yet always contain correct
   * information. */
  bv_roads roads;
  struct resource *resource;		/* NULL for no resource */
  struct terrain *terrain;		/* NULL for unknown tiles */
  struct unit_list *units;
  struct city *worked;			/* NULL for not worked */
  struct player *owner;			/* NULL for not owned */
  struct tile *claimer;
  char *label;                          /* NULL for no label */
  char *spec_sprite;
};

/* 'struct tile_list' and related functions. */
#define SPECLIST_TAG tile
#define SPECLIST_TYPE struct tile
#include "speclist.h"
#define tile_list_iterate(tile_list, ptile)                                 \
  TYPED_LIST_ITERATE(struct tile, tile_list, ptile)
#define tile_list_iterate_end LIST_ITERATE_END

/* 'struct tile_hash' and related functions. */
#define SPECHASH_TAG tile
#define SPECHASH_KEY_TYPE struct tile *
#define SPECHASH_DATA_TYPE void *
#include "spechash.h"
#define tile_hash_iterate(hash, ptile)                                      \
  TYPED_HASH_KEYS_ITERATE(struct tile *, hash, ptile)
#define tile_hash_iterate_end HASH_KEYS_ITERATE_END


/* Tile accessor functions. */
int tile_index(const struct tile *ptile);

struct city *tile_city(const struct tile *ptile);

#define tile_continent(_tile) ((_tile)->continent)
/*Continent_id tile_continent(const struct tile *ptile);*/
void tile_set_continent(struct tile *ptile, Continent_id val);

#define tile_owner(_tile) ((_tile)->owner)
/*struct player *tile_owner(const struct tile *ptile);*/
void tile_set_owner(struct tile *ptile, struct player *pplayer,
                    struct tile *claimer);
#define tile_claimer(_tile) ((_tile)->claimer)

#define tile_resource(_tile) ((_tile)->resource)
#define tile_resource_is_valid(_tile) BV_ISSET((_tile)->special, S_RESOURCE_VALID)
/*const struct resource *tile_resource(const struct tile *ptile);*/
void tile_set_resource(struct tile *ptile, struct resource *presource);

#define tile_terrain(_tile) ((_tile)->terrain)
/*struct terrain *tile_terrain(const struct tile *ptile);*/
void tile_set_terrain(struct tile *ptile, struct terrain *pterrain);

#define tile_worked(_tile) ((_tile)->worked)
/* struct city *tile_worked(const struct tile *ptile); */
void tile_set_worked(struct tile *ptile, struct city *pcity);

/* Specials are a bit different */
bv_special tile_specials(const struct tile *ptile);
void tile_set_specials(struct tile *ptile, bv_special specials);
bool tile_has_special(const struct tile *ptile,
		      enum tile_special_type to_test_for);
bool tile_has_any_specials(const struct tile *ptile);
void tile_set_special(struct tile *ptile, enum tile_special_type spe);
void tile_clear_special(struct tile *ptile, enum tile_special_type spe);
void tile_clear_all_specials(struct tile *ptile);

bv_bases tile_bases(const struct tile *ptile);
bv_roads tile_roads(const struct tile *ptile);
void tile_set_bases(struct tile *ptile, bv_bases bases);
bool tile_has_base(const struct tile *ptile, const struct base_type *pbase);
bool tile_has_any_bases(const struct tile *ptile);
void tile_add_base(struct tile *ptile, const struct base_type *pbase);
void tile_remove_base(struct tile *ptile, const struct base_type *pbase);
bool tile_has_base_flag(const struct tile *ptile, enum base_flag_id flag);
bool tile_has_base_flag_for_unit(const struct tile *ptile,
                                 const struct unit_type *punittype,
                                 enum base_flag_id flag);
bool tile_has_native_base(const struct tile *ptile,
                          const struct unit_type *punittype);
bool tile_has_claimable_base(const struct tile *ptile,
                             const struct unit_type *punittype);
int tile_bases_defense_bonus(const struct tile *ptile,
                             const struct unit_type *punittype);

bool tile_has_road(const struct tile *ptile, const struct road_type *proad);
void tile_add_road(struct tile *ptile, const struct road_type *proad);
void tile_remove_road(struct tile *ptile, const struct road_type *proad);
int tile_roads_output_incr(const struct tile *ptile, enum output_type_id o);
int tile_roads_output_bonus(const struct tile *ptile, enum output_type_id o);

/* Vision related */
enum known_type tile_get_known(const struct tile *ptile,
			      const struct player *pplayer);

/* An arbitrary somewhat integer value.  Activity times are multiplied by
 * this amount, and divided by them later before being used.  This may
 * help to avoid rounding errors; however it should probably be removed. */
#define ACTIVITY_FACTOR 10
int tile_activity_time(enum unit_activity activity,
		       const struct tile *ptile);
int tile_activity_base_time(const struct tile *ptile,
                            Base_type_id base);
int tile_activity_road_time(const struct tile *ptile,
                            Road_type_id road);

/* These are higher-level functions that handle side effects on the tile. */
void tile_change_terrain(struct tile *ptile, struct terrain *pterrain);
void tile_add_special(struct tile *ptile, enum tile_special_type special);
void tile_remove_special(struct tile *ptile, enum tile_special_type special);
bool tile_apply_activity(struct tile *ptile, Activity_type_id act);

#define TILE_LB_TERRAIN_RIVER    (1 << 0)
#define TILE_LB_RIVER_RESOURCE   (1 << 1)
#define TILE_LB_RESOURCE_POLL    (1 << 2)
const char *tile_get_info_text(const struct tile *ptile, int linebreaks);

/* Virtual tiles are tiles that do not exist on the game map. */
struct tile *tile_virtual_new(const struct tile *ptile);
void tile_virtual_destroy(struct tile *vtile);
bool tile_virtual_check(struct tile *vtile);

void *tile_hash_key(const struct tile *ptile);

bool tile_set_label(struct tile *ptile, const char *label);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__TILE_H */
