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
#ifndef __MAPHAND_H
#define __MAPHAND_H

#include <map.h>
struct player;
struct section_file;

void relight_square_if_known(struct player *pplayer, int x, int y);
void give_map_from_player_to_player(struct player *pfrom, struct player *pdest);
void give_seamap_from_player_to_player(struct player *pfrom, struct player *pdest);
void send_all_known_tiles(struct player *dest);
void send_tile_info(struct player *dest, int x, int y, enum known_type type);
void lighten_tile(struct player *dest, int x, int y);
void lighten_area(struct player *pplayer, int x, int y);
void light_square(struct player *pplayer, int x, int y, int len);
void send_map_info(struct player *dest);
void global_warming(int effect);

void upgrade_city_rails(struct player *pplayer, int discovery);

void map_save(struct section_file *file);
void map_startpos_load(struct section_file *file);
void map_tiles_load(struct section_file *file);
void map_load(struct section_file *file);

#endif
