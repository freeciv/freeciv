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

#include "base.h"
#include "fc_types.h"
#include "player.h"
#include "terrain.h"
#include "unitlist.h"

/* Convenience macro for accessing tile coordinates.  This should only be
 * used for debugging. */
#define TILE_XY(ptile) ((ptile) ? (ptile)->x : -1), \
                       ((ptile) ? (ptile)->y : -1)

struct tile {
  int x, y; /* Cartesian (map) coordinates of the tile. */
  int nat_x, nat_y; /* Native coordinates of the tile. */
  int index; /* Index coordinate of the tile. */
  struct terrain *terrain; /* May be NULL for unknown tiles. */
  bv_special special;
  struct resource *resource; /* available resource, or NULL */
  struct city *city;        /* city standing on the tile, NULL if none */
  struct unit_list *units;
  bv_player tile_known, tile_seen[V_COUNT];
  struct city *worked;      /* city working tile, or NULL if none */
  Continent_id continent;
  struct player *owner;     /* Player owning this tile, or NULL. */
  struct tile *owner_source; /* what makes it owned by owner */
  char *spec_sprite;
};

/* get 'struct tile_list' and related functions: */
#define SPECLIST_TAG tile
#define SPECLIST_TYPE struct tile
#include "speclist.h"

#define tile_list_iterate(tile_list, ptile) \
    TYPED_LIST_ITERATE(struct tile, tile_list, ptile)
#define tile_list_iterate_end  LIST_ITERATE_END

/* Tile accessor functions. */
struct player *tile_get_owner(const struct tile *ptile);
void tile_set_owner(struct tile *ptile, struct player *pplayer);
struct city *tile_get_city(const struct tile *ptile);
void tile_set_city(struct tile *ptile, struct city *pcity);
struct terrain *tile_get_terrain(const struct tile *ptile);
void tile_set_terrain(struct tile *ptile, struct terrain *pterrain);
bv_special tile_get_special(const struct tile *ptile);
bool tile_has_special(const struct tile *ptile,
		      enum tile_special_type to_test_for);
bool tile_has_any_specials(const struct tile *ptile);
void tile_set_special(struct tile *ptile, enum tile_special_type spe);
struct base_type *tile_get_base(const struct tile *ptile);
void tile_add_base(struct tile *ptile, const struct base_type *pbase);
void tile_remove_base(struct tile *ptile);
bool tile_has_base_flag(const struct tile *ptile, enum base_flag_id flag);
bool tile_has_base_flag_for_unit(const struct tile *ptile,
                                 const struct unit_type *punittype,
                                 enum base_flag_id flag);
bool tile_has_native_base(const struct tile *ptile,
                          const struct unit_type *punittype);
const struct resource *tile_get_resource(const struct tile *ptile);
void tile_set_resource(struct tile *ptile, struct resource *presource);
void tile_clear_special(struct tile *ptile, enum tile_special_type spe);
void tile_clear_all_specials(struct tile *ptile);
void tile_set_continent(struct tile *ptile, Continent_id val);
Continent_id tile_get_continent(const struct tile *ptile);
enum known_type tile_get_known(const struct tile *ptile,
			      const struct player *pplayer);

/* An arbitrary somewhat integer value.  Activity times are multiplied by
 * this amount, and divided by them later before being used.  This may
 * help to avoid rounding errors; however it should probably be removed. */
#define ACTIVITY_FACTOR 10
int tile_activity_time(enum unit_activity activity,
		       const struct tile *ptile);
int tile_activity_base_time(const struct tile *ptile,
                            enum base_type_id base);

/* These are higher-level functions that handle side effects on the tile. */
void tile_change_terrain(struct tile *ptile, struct terrain *pterrain);
void tile_add_special(struct tile *ptile, enum tile_special_type special);
void tile_remove_special(struct tile *ptile, enum tile_special_type special);
bool tile_apply_activity(struct tile *ptile, Activity_type_id act);

const char *tile_get_info_text(const struct tile *ptile);

#endif /* FC__TILE_H */
