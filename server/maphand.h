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

#include "map.h"

struct player;
struct section_file;

struct dumb_city{
  int id;
  char name[MAX_LEN_NAME];
  unsigned short size;
  unsigned char has_walls;
  unsigned char owner;
};

struct player_tile{
  enum tile_terrain_type terrain;
  enum tile_special_type special;
  unsigned short seen;
  struct dumb_city* city;
  short last_updated;
};

void global_warming(int effect);
void give_map_from_player_to_player(struct player *pfrom, struct player *pdest);
void give_seamap_from_player_to_player(struct player *pfrom, struct player *pdest);
void send_all_known_tiles(struct player *dest);
void send_tile_info(struct player *dest, int x, int y);
void unfog_area(struct player *pplayer, int x, int y, int len);
void send_NODRAW_tiles(struct player *pplayer, int x, int y, int len);
void fog_area(struct player *pplayer, int x, int y, int len);
void upgrade_city_rails(struct player *pplayer, int discovery);
void send_map_info(struct player *dest);
void map_save(struct section_file *file);
void map_startpos_load(struct section_file *file);
void map_tiles_load(struct section_file *file);
void map_load(struct section_file *file);
void map_rivers_overlay_load(struct section_file *file);
void map_fog_city_area(struct city *pcity);
void map_unfog_city_area(struct city *pcity);
void remove_unit_sight_points(struct unit *punit);
void show_area(struct player *pplayer,int x, int y, int len);
void map_unfog_pseudo_city_area(struct player *pplayer, int x,int y);
void map_fog_pseudo_city_area(struct player *pplayer, int x,int y);
void unfog_map_for_ai_players(void);
void unfog_map_for_player(struct player *pplayer);

int map_get_known_and_seen(int x, int y, int playerid);
int map_get_seen(int x, int y, int playerid);
void map_change_seen(int x, int y, int playerid, int change);
int map_get_known(int x, int y, struct player *pplayer);
void map_set_known(int x, int y, struct player *pplayer);
void map_clear_known(int x, int y, struct player *pplayer);
int map_get_sent(int x, int y, struct player *pplayer);
void map_set_sent(int x, int y, struct player *pplayer);
void map_clear_sent(int x, int y, struct player *pplayer);
void map_know_all(struct player *pplayer);
void map_know_and_see_all(struct player *pplayer);
void set_unknown_tiles_to_unsent(struct player *pplayer);
void player_map_allocate(struct player *pplayer);
struct player_tile *map_get_player_tile(int x, int y, int playerid);
void update_tile_knowledge(struct player *pplayer,int x, int y);
void update_player_tile_last_seen(struct player *pplayer, int x, int y);

#endif  /* FC__MAPHAND_H */
