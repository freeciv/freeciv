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

#include "fc_types.h"
#include "player.h"
#include "terrain.h"
#include "unit.h"

/* Convenience macro for accessing tile coordinates.  This should only be
 * used for debugging. */
#define TILE_XY(ptile) ((ptile) ? (ptile)->x : -1), \
                       ((ptile) ? (ptile)->y : -1)

struct tile {
  const int x, y; /* Cartesian (map) coordinates of the tile. */
  const int nat_x, nat_y; /* Native coordinates of the tile. */
  const int index; /* Index coordinate of the tile. */
  Terrain_type_id terrain;
  enum tile_special_type special;
  struct city *city;        /* city standing on the tile, NULL if none */
  struct unit_list *units;
  bv_player tile_known, tile_seen;
  int assigned; /* these can save a lot of CPU usage -- Syela */
  struct city *worked;      /* city working tile, or NULL if none */
  Continent_id continent;
  struct player *owner;     /* Player owning this tile, or NULL. */
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
Terrain_type_id tile_get_terrain(const struct tile *ptile);
void tile_set_terrain(struct tile *ptile, Terrain_type_id ter);
enum tile_special_type tile_get_special(const struct tile *ptile);
bool tile_has_special(const struct tile *ptile,
		      enum tile_special_type to_test_for);
void tile_set_special(struct tile *ptile, enum tile_special_type spe);
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

void tile_change_terrain(struct tile *ptile, Terrain_type_id type);
void tile_irrigate(struct tile *ptile);
void tile_mine(struct tile *ptile);
void tile_transform(struct tile *ptile);

const char *tile_get_info_text(const struct tile *ptile);

#endif /* FC__TILE_H */
