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

#include <assert.h>

#include "city.h"
#include "game.h"
#include "map.h"
#include "player.h"
#include "unit.h"

#include "maphand.h"

#include "sanitycheck.h"

/**************************************************************************
...
**************************************************************************/
static void check_specials(void)
{
  whole_map_iterate(x, y) {
    int terrain = map_get_terrain(x, y);
    int special = map_get_special(x, y);

    if (special & S_RAILROAD)
      assert(special & S_ROAD);
    if (special & S_FARMLAND)
      assert(special & S_IRRIGATION);
    if (special & S_SPECIAL_1)
      assert(!(special & S_SPECIAL_2));

    if (special & S_MINE)
      assert(get_tile_type(terrain)->mining_result == terrain);
    if (special & S_IRRIGATION)
      assert(get_tile_type(terrain)->irrigation_result == terrain);

    assert(terrain >= T_ARCTIC && terrain < T_UNKNOWN);
  } whole_map_iterate_end;
}

/**************************************************************************
...
**************************************************************************/
static void check_fow(void)
{
  whole_map_iterate(x, y) {
    players_iterate(pplayer) {
      struct player_tile *plr_tile = map_get_player_tile(x, y, pplayer->player_no);
      /* underflow of unsigned int */
      assert(plr_tile->seen < 60000);
      assert(plr_tile->own_seen < 60000);
      assert(plr_tile->pending_seen < 60000);

      assert(plr_tile->own_seen <= plr_tile->seen);
      if (map_get_known(x, y, pplayer))
	assert(plr_tile->pending_seen == 0);
    } players_iterate_end;
  } whole_map_iterate_end;
}

/**************************************************************************
...
**************************************************************************/
static void check_misc(void)
{
  int nbarbs = 0;
  players_iterate(pplayer) {
    if (pplayer->ai.is_barbarian)
      nbarbs++;
  } players_iterate_end;
  assert(nbarbs == game.nbarbarians);

  assert(game.nplayers <= MAX_NUM_PLAYERS);
}

/**************************************************************************
...
**************************************************************************/
static void check_map(void)
{
  whole_map_iterate(x, y) {
    struct tile *ptile = map_get_tile(x, y);
    struct city *pcity = map_get_city(x, y);
    int cont = map_get_continent(x, y);
    if (map_get_terrain(x, y) == T_OCEAN) {
      assert(cont == 0);
    } else {
      assert(cont != 0);
      adjc_iterate(x, y, x1, y1) {
	if (map_get_terrain(x1, y1) != T_OCEAN)
	  assert(map_get_continent(x1, y1) == cont);
      } adjc_iterate_end;
    }

    if (pcity) {
      assert(pcity->x == x && pcity->y == y);
    }

    unit_list_iterate(ptile->units, punit) {
      assert(punit->x == x && punit->y == y);
    } unit_list_iterate_end;
  } whole_map_iterate_end;
}

/**************************************************************************
...
**************************************************************************/
static void check_cities(void)
{
  players_iterate(pplayer) {
    city_list_iterate(pplayer->cities, pcity) {
      int x, y;
      assert(city_owner(pcity) == pplayer);
      assert(pcity->size >= 1);

      unit_list_iterate(pcity->units_supported, punit) {
	assert(punit->homecity == pcity->id);
	assert(unit_owner(punit) == pplayer);
      } unit_list_iterate_end;

      assert(pcity->x < map.xsize && pcity->x >= 0);
      assert(pcity->y < map.ysize && pcity->y >= 0);

      assert(map_get_terrain(pcity->x, pcity->y) != T_OCEAN);

      city_map_iterate(x, y) {
	int map_x = pcity->x + x - CITY_MAP_SIZE/2;
	int map_y = pcity->y + y - CITY_MAP_SIZE/2;

	if (normalize_map_pos(&map_x, &map_y)) {
	  struct tile *ptile = map_get_tile(map_x, map_y);
	  switch (get_worker_city(pcity, x, y)) {
	  case C_TILE_EMPTY:
	    assert(map_get_tile(map_x, map_y)->worked == NULL);
	    assert(!is_enemy_unit_tile(ptile, pplayer->player_no));
	    break;
	  case C_TILE_WORKER:
	    assert(map_get_tile(map_x, map_y)->worked == pcity);
	    assert(!is_enemy_unit_tile(ptile, pplayer->player_no));
	    break;
	  case C_TILE_UNAVAILABLE:
	    assert(map_get_tile(map_x, map_y)->worked != NULL
		   || is_enemy_unit_tile(ptile, pplayer->player_no)
		   || !map_get_known(map_x, map_y, pplayer));
	    break;
	  }
	} else {
	  assert(get_worker_city(pcity, x, y) == C_TILE_UNAVAILABLE);
	}
      }
    } city_list_iterate_end;
  } players_iterate_end;
}

/**************************************************************************
...
**************************************************************************/
static void check_units(void) {
  players_iterate(pplayer) {
    unit_list_iterate(pplayer->units, punit) {
      int x = punit->x;
      int y = punit->y;
      struct city *pcity;

      assert(unit_owner(punit) == pplayer);
      assert(is_real_tile(x, y));

      if (punit->homecity) {
	pcity = player_find_city_by_id(pplayer, punit->homecity);
	assert(pcity);
	assert(city_owner(pcity) == pplayer);
      }
      assert(can_unit_continue_current_activity(punit));

      pcity = map_get_city(x, y);
      if (pcity) {
	assert(pplayers_allied(city_owner(pcity), pplayer));
      }

      if (map_get_terrain(x, y) == T_OCEAN)
	assert(ground_unit_transporter_capacity(x, y, pplayer->player_no) >= 0);

      assert(punit->moves_left >= 0);
      assert(punit->hp > 0);
    } unit_list_iterate_end;
  } players_iterate_end;
}

/**************************************************************************
...
**************************************************************************/
static void check_players(void)
{
  players_iterate(pplayer) {
    int found_palace = 0;

    city_list_iterate(pplayer->cities, pcity) {
      if (city_got_building(pcity, B_PALACE)) {
	found_palace++;
      }
      assert(found_palace <= 1);
    } city_list_iterate_end;

    players_iterate(pplayer2) {
      assert(pplayer->diplstates[pplayer2->player_no].type
	     == pplayer2->diplstates[pplayer->player_no].type);
      if (pplayer->diplstates[pplayer2->player_no].type == DS_CEASEFIRE)
	assert(pplayer->diplstates[pplayer2->player_no].turns_left
	       == pplayer2->diplstates[pplayer->player_no].turns_left);
    } players_iterate_end;
  } players_iterate_end;
}

/**************************************************************************
...
**************************************************************************/
void sanity_check(void)
{
  check_specials();
  check_fow();
  check_misc();
  check_map();
  check_cities();
  check_units();
  check_players();
}
