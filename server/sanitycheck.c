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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>

#include "city.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "player.h"
#include "terrain.h"
#include "unit.h"

#include "citytools.h"
#include "maphand.h"
#include "sanitycheck.h"
#include "unittools.h"

#ifndef NDEBUG

/**************************************************************************
...
**************************************************************************/
static void check_specials(void)
{
  whole_map_iterate(x, y) {
    enum tile_terrain_type terrain = map_get_terrain(x, y);
    enum tile_special_type special = map_get_special(x, y);

    if (contains_special(special, S_RAILROAD))
      assert(contains_special(special, S_ROAD));
    if (contains_special(special, S_FARMLAND))
      assert(contains_special(special, S_IRRIGATION));
    if (contains_special(special, S_SPECIAL_1))
      assert(!contains_special(special,  S_SPECIAL_2));

    if (contains_special(special, S_MINE))
      assert(get_tile_type(terrain)->mining_result == terrain);
    if (contains_special(special, S_IRRIGATION))
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
      struct player_tile *plr_tile = map_get_player_tile(x, y, pplayer);
      /* underflow of unsigned int */
      assert(plr_tile->seen < 60000);
      assert(plr_tile->own_seen < 60000);
      assert(plr_tile->pending_seen < 60000);

      assert(plr_tile->own_seen <= plr_tile->seen);
      if (map_is_known(x, y, pplayer)) {
	assert(plr_tile->pending_seen == 0);
      }
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
    if (is_barbarian(pplayer)) {
      nbarbs++;
    }
  } players_iterate_end;
  assert(nbarbs == game.nbarbarians);

  assert(game.nplayers <= MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS);
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
    if (is_ocean(map_get_terrain(x, y))) {
      assert(cont == 0);
    } else {
      assert(cont != 0);
      adjc_iterate(x, y, x1, y1) {
	if (!is_ocean(map_get_terrain(x1, y1))) {
	  assert(map_get_continent(x1, y1) == cont);
	}
      } adjc_iterate_end;
    }

    if (pcity) {
      assert(same_pos(pcity->x, pcity->y, x, y));
    }

    unit_list_iterate(ptile->units, punit) {
      assert(same_pos(punit->x, punit->y, x, y));

      /* Check diplomatic status of stacked units. */
      unit_list_iterate(ptile->units, punit2) {
	assert(pplayers_allied(unit_owner(punit), unit_owner(punit2)));
      } unit_list_iterate_end;
      if (pcity) {
	assert(pplayers_allied(unit_owner(punit), city_owner(pcity)));
      }
    } unit_list_iterate_end;
  } whole_map_iterate_end;
}

/**************************************************************************
  Verify that the city has sane values.
**************************************************************************/
void real_sanity_check_city(struct city *pcity, const char *file, int line)
{
  int workers = 0;
  struct player *pplayer = city_owner(pcity);

  assert(pcity->size >= 1);
  assert(is_normal_map_pos(pcity->x, pcity->y));

  unit_list_iterate(pcity->units_supported, punit) {
    assert(punit->homecity == pcity->id);
    assert(unit_owner(punit) == pplayer);
  } unit_list_iterate_end;

  /* Note that cities may be found on land or water. */

  city_map_iterate(x, y) {
    int map_x, map_y;

    if (city_map_to_map(&map_x, &map_y, pcity, x, y)) {
      struct tile *ptile = map_get_tile(map_x, map_y);
      struct player *owner = map_get_owner(map_x, map_y);

      switch (get_worker_city(pcity, x, y)) {
      case C_TILE_EMPTY:
	if (map_get_tile(map_x, map_y)->worked) {
	  freelog(LOG_ERROR, "Tile at %s->%d,%d marked as "
		  "empty but worked by %s!",
		  pcity->name, x, y,
		  map_get_tile(map_x, map_y)->worked->name);
	}
	if (is_enemy_unit_tile(ptile, pplayer)) {
	  freelog(LOG_ERROR, "Tile at %s->%d,%d marked as "
		  "empty but occupied by an enemy unit!",
		  pcity->name, x, y);
	}
	if (game.borders > 0
	    && owner && owner->player_no != pcity->owner) {
	  freelog(LOG_ERROR, "Tile at %s->%d,%d marked as "
		  "empty but in enemy territory!",
		  pcity->name, x, y);
	}
	if (!city_can_work_tile(pcity, x, y)) {
	  /* Complete check. */
	  freelog(LOG_ERROR, "Tile at %s->%d,%d marked as "
		  "empty but is unavailable!",
		  pcity->name, x, y);
	}
	break;
      case C_TILE_WORKER:
	if (map_get_tile(map_x, map_y)->worked != pcity) {
	  freelog(LOG_ERROR, "Tile at %s->%d,%d marked as "
		  "worked but main map disagrees!",
		  pcity->name, x, y);
	}
	if (is_enemy_unit_tile(ptile, pplayer)) {
	  freelog(LOG_ERROR, "Tile at %s->%d,%d marked as "
		  "worked but occupied by an enemy unit!",
		  pcity->name, x, y);
	}
	if (game.borders > 0
	    && owner && owner->player_no != pcity->owner) {
	  freelog(LOG_ERROR, "Tile at %s->%d,%d marked as "
		  "worked but in enemy territory!",
		  pcity->name, x, y);
	}
	if (!city_can_work_tile(pcity, x, y)) {
	  /* Complete check. */
	  freelog(LOG_ERROR, "Tile at %s->%d,%d marked as "
		  "worked but is unavailable!",
		  pcity->name, x, y);
	}
	break;
      case C_TILE_UNAVAILABLE:
	if (city_can_work_tile(pcity, x, y)) {
	  freelog(LOG_ERROR, "Tile at %s->%d,%d marked as "
		  "unavailable but seems to be available!",
		  pcity->name, x, y);
	}
	break;
      }
    } else {
      assert(get_worker_city(pcity, x, y) == C_TILE_UNAVAILABLE);
    }
  } city_map_iterate_end;

  /* Sanity check city size versus worker and specialist counts. */
  city_map_iterate(x, y) {
    if (get_worker_city(pcity, x, y) == C_TILE_WORKER) {
      workers++;
    }
  } city_map_iterate_end;
  if (workers + city_specialists(pcity) != pcity->size + 1) {
    die("%s is illegal (size%d w%d e%d t%d s%d) in %s line %d",
        pcity->name, pcity->size, workers, pcity->specialists[SP_ELVIS],
        pcity->specialists[SP_TAXMAN], pcity->specialists[SP_SCIENTIST], file, line);
  }
}

/**************************************************************************
...
**************************************************************************/
static void check_cities(void)
{
  players_iterate(pplayer) {
    city_list_iterate(pplayer->cities, pcity) {
      assert(city_owner(pcity) == pplayer);

      sanity_check_city(pcity);
    } city_list_iterate_end;
  } players_iterate_end;

  whole_map_iterate(x, y) {
    struct tile *ptile = map_get_tile(x, y);
    if (ptile->worked) {
      struct city *pcity = ptile->worked;
      int city_x, city_y;
      bool is_valid;

      is_valid = map_to_city_map(&city_x, &city_y, pcity, x, y);
      assert(is_valid);

      if (pcity->city_map[city_x][city_y] != C_TILE_WORKER) {
	freelog(LOG_ERROR, "%d,%d is listed as being worked by %s "
		"on the map, but %s lists the tile %d,%d as having "
		"status %d\n",
		x, y, pcity->name, pcity->name, city_x, city_y,
		pcity->city_map[city_x][city_y]);
      }
    }
  } whole_map_iterate_end;
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
      CHECK_MAP_POS(x, y);

      if (punit->homecity != 0) {
	pcity = player_find_city_by_id(pplayer, punit->homecity);
	assert(pcity != NULL);
	assert(city_owner(pcity) == pplayer);
      }

      if (!can_unit_continue_current_activity(punit)) {
	freelog(LOG_ERROR, "%s at %d,%d (%s) has activity %s, "
		"which it can't continue!",
		unit_type(punit)->name,
		x, y, map_get_tile_info_text(x, y),
		get_activity_text(punit->activity));
      }

      pcity = map_get_city(x, y);
      if (pcity) {
	assert(pplayers_allied(city_owner(pcity), pplayer));
      } else if (is_ocean(map_get_terrain(x, y))) {
	assert(ground_unit_transporter_capacity(x, y, pplayer) >= 0);
      }

      assert(punit->moves_left >= 0);
      assert(punit->hp > 0);

      /* Check for ground units in the ocean. */
      if (!pcity
	  && is_ocean(map_get_terrain(punit->x, punit->y))
	  && is_ground_unit(punit)) {
	assert(punit->transported_by != -1);
	assert(!is_ground_unit(find_unit_by_id(punit->transported_by)));
      }

      /* Check for over-full transports. */
      assert(get_transporter_occupancy(punit)
	     <= get_transporter_capacity(punit));
    } unit_list_iterate_end;
  } players_iterate_end;
}

/**************************************************************************
...
**************************************************************************/
static void check_players(void)
{
  int player_no;

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

  /* Sanity checks on living and dead players. */
  for (player_no = 0; player_no < ARRAY_SIZE(game.players); player_no++) {
    struct player *pplayer = &game.players[player_no];

    if (!pplayer->is_alive) {
      /* Dead players' units and cities are disbanded in kill_player(). */
      assert(unit_list_size(&pplayer->units) == 0);
      assert(city_list_size(&pplayer->cities) == 0);
    }

    /* Dying players shouldn't be left around.  But they are. */
    assert(!pplayer->is_dying);
  }
}

#endif /* !NDEBUG */

/**************************************************************************
...
**************************************************************************/
void sanity_check(void)
{
#ifndef NDEBUG
  check_specials();
  check_fow();
  check_misc();
  check_map();
  check_cities();
  check_units();
  check_players();
#endif /* !NDEBUG */
}
