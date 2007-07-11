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
#ifndef FC__MAPHAND_H
#define FC__MAPHAND_H

#include "fc_types.h"
#include "map.h"
#include "packets.h"
#include "terrain.h"

#include "hand_gen.h"

struct section_file;
struct conn_list;

struct dumb_city{
  /* Values in this struct are copied using a memcpy, so don't put any
   * pointers in here. */
  int id;
  bool occupied;
  bool walls;
  bool happy, unhappy;
  char name[MAX_LEN_NAME];
  unsigned short size;
  struct player *owner; /* City owner - cannot be NULL. */

  bv_imprs improvements;
};

struct player_tile {
  struct terrain *terrain; /* May be NULL for unknown tiles. */
  bv_special special;
  struct resource *resource;
  unsigned short seen_count[V_COUNT];
  unsigned short own_seen[V_COUNT];
  /* If you build a city with an unknown square within city radius
     the square stays unknown. However, we still have to keep count
     of the seen points, so they are kept in here. When the tile
     then becomes known they are moved to seen. */
  struct dumb_city* city;
  short last_updated;
};

void assign_continent_numbers(void);

void global_warming(int effect);
void nuclear_winter(int effect);
void give_map_from_player_to_player(struct player *pfrom, struct player *pdest);
void give_seamap_from_player_to_player(struct player *pfrom, struct player *pdest);
void give_citymap_from_player_to_player(struct city *pcity,
					struct player *pfrom, struct player *pdest);
void send_all_known_tiles(struct conn_list *dest);
void send_tile_info(struct conn_list *dest, struct tile *ptile,
                    bool send_unknown);
void upgrade_city_rails(struct player *pplayer, bool discovery);
void send_map_info(struct conn_list *dest);

void map_show_tile(struct player *pplayer, struct tile *ptile);
void map_show_circle(struct player *pplayer,
		     struct tile *ptile, int radius_sq);
void map_show_all(struct player *pplayer);

bool map_is_known_and_seen(const struct tile *ptile, struct player *pplayer,
			   enum vision_layer vlayer);
void map_change_seen(struct tile *ptile, struct player *pplayer, int change,
		     enum vision_layer vlayer);
bool map_is_known(const struct tile *ptile, const struct player *pplayer);
void map_set_known(struct tile *ptile, struct player *pplayer);
void map_clear_known(struct tile *ptile, struct player *pplayer);
void map_know_and_see_all(struct player *pplayer);
void show_map_to_all(void);

void player_map_allocate(struct player *pplayer);
void player_map_free(struct player *pplayer);
struct player_tile *map_get_player_tile(const struct tile *ptile,
					const struct player *pplayer);
bool update_player_tile_knowledge(struct player *pplayer,struct tile *ptile);
void update_tile_knowledge(struct tile *ptile);
void update_player_tile_last_seen(struct player *pplayer, struct tile *ptile);

void give_shared_vision(struct player *pfrom, struct player *pto);
void remove_shared_vision(struct player *pfrom, struct player *pto);

void enable_fog_of_war(void);
void disable_fog_of_war(void);

void map_calculate_borders(void);
void map_claim_ownership(struct tile *ptile, struct player *owner,
                         struct tile *source);

void check_terrain_change(struct tile *ptile, struct terrain *oldter);
int get_continent_size(Continent_id id);
int get_ocean_size(Continent_id id);

/****************************************************************************
  Vision for units and cities:

  A vision source has a fixed owner and tile; it changes only in range.
  Vision range is given in radius squared; most such values will come from
  the ruleset.  All vision is circular.

  A vision source is created using vision_new; this creates the source
  without any sight points.  Call vision_change_sight to change the sight
  points of a vision source (generally called from city_refresh_vision
  and unit_refresh vision; this can be called liberally to do updates after
  an effect may have changed the source's vision range).  Clear the sight
  using vision_clear_sight before freeing it with vision_free.

  vision_get_sight returns the sight points of the source.  This should
  only rarely be necessary since all fogging and unfogging operations
  are taken care of internally.

  vision_reveal_tiles() controls whether the vision source can discover
  new (unknown) tiles or simply maintain vision on already-known tiles.
  By default, cities should pass FALSE for this since they cannot
  discover new tiles.

  ***** IMPORTANT *****
  To change any of the parameters given to vision_new - that is, to change
  the vision source's position (tile) or owner - you must create a new
  vision and then clear and free the old vision.  Order is very important
  here since you do not want to fog tiles intermediately.  You must store
  a copy of the old vision source, then create and attach and fill out the
  sight for a new vision source, and only then may you clear and free the
  old vision source.  In most operations you'll want to stick some other
  code in between so that for the bulk of the operation all tiles are
  visible.  For instance to move a unit:

    old_vision = punit->server.vision;
    punit->server.vision = vision_new(punit->owner, dest_tile);
    vision_change_sight(punit->server.vision,
                        get_unit_vision_at(punit, dest_tile));

    ...then do all the work of moving the unit...

    vision_clear_sight(old_vision);
    vision_free(old_vision);

  note that for all the code in the middle both the new and the old
  vision sources are active.  The same process applies when transferring
  a unit or city between players, etc.
****************************************************************************/
struct vision;
struct vision *vision_new(struct player *pplayer, struct tile *ptile);
bool vision_reveal_tiles(struct vision *vision, bool reveal_tiles);
int vision_get_sight(const struct vision *vision, enum vision_layer vlayer);
void vision_change_sight(struct vision *vision, enum vision_layer vlayer,
			 int radius_sq);
void vision_clear_sight(struct vision *vision);
void vision_free(struct vision *vision);

#endif  /* FC__MAPHAND_H */
