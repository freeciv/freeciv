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
#ifndef __AITOOLS_H
#define __AITOOLS_H


struct point  {
  int x, y;
};

struct square {
  int top, left, bottom, right;
};

struct aiislandglobal  {
  struct point center; /* "center" tile on island */
  struct square loc; 
  int shared;        /* how many players have cities on this island */
};

struct ai_choice {
  int choice;            /* what the advisor wants */ 
  int want;              /* how bad it wants it (0-100) */
  int type;              /* unit/building or other depending on question */
};


struct ai_map_struct {
  struct aiislandglobal island_data[100];
  int nr_islands;
  int *city_value;
};
#include <player.h>

#define ISLAND_ITERATE(n) { int x,y;  \
  for (y = ai_map.island_data[n].loc.top; y < ai_map.island_data[n].loc.bottom; y++) \
    for (x = ai_map.island_data[n].loc.left; x < ai_map.island_data[n].loc.right; x++) \
      if (map_get_continent(x, y) == n) 
	
#define ISLAND_END }



void ai_init_island_info(int nr_islands);


int map_get_city_value(int x, int y);
void map_set_city_value(int x, int y, int val);

int is_free_work_tile(struct player *pplayer, int x, int y);

struct city *dist_nearest_enemy_city(struct player *pplayer, int x, int y);
struct unit *dist_nearest_enemy_unit(struct player *pplayer, int x, int y);
void ai_update_player_island_info(struct player *pplayer);
void ai_calculate_city_value(int isle);

void ai_government_change(struct player *pplayer, int gov);

int ai_gold_reserve(struct player *pplayer);

extern struct ai_map_struct ai_map;

void adjust_choice(int type, struct ai_choice *choice);
void copy_if_better_choice(struct ai_choice *cur, struct ai_choice *best);
void ai_advisor_choose_building(struct city *pcity, struct ai_choice *choice);



#endif
