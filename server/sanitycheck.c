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

#ifdef SANITY_CHECKING

/**************************************************************************
...
**************************************************************************/
static void check_specials(void)
{
  whole_map_iterate(ptile) {
    Terrain_type_id terrain = map_get_terrain(ptile);
    enum tile_special_type special = map_get_special(ptile);

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

    assert(terrain >= T_FIRST && terrain < T_COUNT);
  } whole_map_iterate_end;
}

/**************************************************************************
...
**************************************************************************/
static void check_fow(void)
{
  whole_map_iterate(ptile) {
    players_iterate(pplayer) {
      struct player_tile *plr_tile = map_get_player_tile(ptile, pplayer);
      /* underflow of unsigned int */
      assert(plr_tile->seen < 60000);
      assert(plr_tile->own_seen < 60000);
      assert(plr_tile->pending_seen < 60000);

      assert(plr_tile->own_seen <= plr_tile->seen);
      if (map_is_known(ptile, pplayer)) {
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
  whole_map_iterate(ptile) {
    struct city *pcity = map_get_city(ptile);
    int cont = map_get_continent(ptile), x, y;

    CHECK_INDEX(ptile->index);
    CHECK_MAP_POS(ptile->x, ptile->y);
    CHECK_NATIVE_POS(ptile->nat_x, ptile->nat_y);

    index_to_map_pos(&x, &y, ptile->index);
    assert(x == ptile->x && y == ptile->y);

    index_to_native_pos(&x, &y, ptile->index);
    assert(x == ptile->nat_x && y == ptile->nat_y);

    if (is_ocean(map_get_terrain(ptile))) {
      assert(cont < 0);
      adjc_iterate(ptile, tile1) {
	if (is_ocean(map_get_terrain(tile1))) {
	  assert(map_get_continent(tile1) == cont);
	}
      } adjc_iterate_end;
    } else {
      assert(cont > 0);
      adjc_iterate(ptile, tile1) {
	if (!is_ocean(map_get_terrain(tile1))) {
	  assert(map_get_continent(tile1) == cont);
	}
      } adjc_iterate_end;
    }

    if (pcity) {
      assert(same_pos(pcity->tile, ptile));
    }

    unit_list_iterate(ptile->units, punit) {
      assert(same_pos(punit->tile, ptile));

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
  assert(!terrain_has_flag(map_get_terrain(pcity->tile),
			   TER_NO_CITIES));

  unit_list_iterate(pcity->units_supported, punit) {
    assert(punit->homecity == pcity->id);
    assert(unit_owner(punit) == pplayer);
  } unit_list_iterate_end;

  /* Note that cities may be found on land or water. */

  city_map_iterate(x, y) {
    struct tile *ptile;

    if ((ptile = city_map_to_map(pcity, x, y))) {
      struct player *owner = map_get_owner(ptile);

      switch (get_worker_city(pcity, x, y)) {
      case C_TILE_EMPTY:
	if (ptile->worked) {
	  freelog(LOG_ERROR, "Tile at %s->%d,%d marked as "
		  "empty but worked by %s!",
		  pcity->name, TILE_XY(ptile),
		  (ptile)->worked->name);
	}
	if (is_enemy_unit_tile(ptile, pplayer)) {
	  freelog(LOG_ERROR, "Tile at %s->%d,%d marked as "
		  "empty but occupied by an enemy unit!",
		  pcity->name, TILE_XY(ptile));
	}
	if (game.borders > 0
	    && owner && owner->player_no != pcity->owner) {
	  freelog(LOG_ERROR, "Tile at %s->%d,%d marked as "
		  "empty but in enemy territory!",
		  pcity->name, TILE_XY(ptile));
	}
	if (!city_can_work_tile(pcity, x, y)) {
	  /* Complete check. */
	  freelog(LOG_ERROR, "Tile at %s->%d,%d marked as "
		  "empty but is unavailable!",
		  pcity->name, TILE_XY(ptile));
	}
	break;
      case C_TILE_WORKER:
	if ((ptile)->worked != pcity) {
	  freelog(LOG_ERROR, "Tile at %s->%d,%d marked as "
		  "worked but main map disagrees!",
		  pcity->name, TILE_XY(ptile));
	}
	if (is_enemy_unit_tile(ptile, pplayer)) {
	  freelog(LOG_ERROR, "Tile at %s->%d,%d marked as "
		  "worked but occupied by an enemy unit!",
		  pcity->name, TILE_XY(ptile));
	}
	if (game.borders > 0
	    && owner && owner->player_no != pcity->owner) {
	  freelog(LOG_ERROR, "Tile at %s->%d,%d marked as "
		  "worked but in enemy territory!",
		  pcity->name, TILE_XY(ptile));
	}
	if (!city_can_work_tile(pcity, x, y)) {
	  /* Complete check. */
	  freelog(LOG_ERROR, "Tile at %s->%d,%d marked as "
		  "worked but is unavailable!",
		  pcity->name, TILE_XY(ptile));
	}
	break;
      case C_TILE_UNAVAILABLE:
	if (city_can_work_tile(pcity, x, y)) {
	  freelog(LOG_ERROR, "Tile at %s->%d,%d marked as "
		  "unavailable but seems to be available!",
		  pcity->name, TILE_XY(ptile));
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

  whole_map_iterate(ptile) {
    if (ptile->worked) {
      struct city *pcity = ptile->worked;
      int city_x, city_y;
      bool is_valid;

      is_valid = map_to_city_map(&city_x, &city_y, pcity, ptile);
      assert(is_valid);

      if (pcity->city_map[city_x][city_y] != C_TILE_WORKER) {
	freelog(LOG_ERROR, "%d,%d is listed as being worked by %s "
		"on the map, but %s lists the tile %d,%d as having "
		"status %d\n",
		TILE_XY(ptile), pcity->name, pcity->name, city_x, city_y,
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
      struct tile *ptile = punit->tile;
      struct city *pcity;
      struct unit *transporter = NULL, *transporter2 = NULL;

      assert(unit_owner(punit) == pplayer);

      if (punit->homecity != 0) {
	pcity = player_find_city_by_id(pplayer, punit->homecity);
	assert(pcity != NULL);
	assert(city_owner(pcity) == pplayer);
      }

      if (!can_unit_continue_current_activity(punit)) {
	freelog(LOG_ERROR, "%s at %d,%d (%s) has activity %s, "
		"which it can't continue!",
		unit_type(punit)->name,
		TILE_XY(ptile), map_get_tile_info_text(ptile),
		get_activity_text(punit->activity));
      }

      pcity = map_get_city(ptile);
      if (pcity) {
	assert(pplayers_allied(city_owner(pcity), pplayer));
      }

      assert(punit->moves_left >= 0);
      assert(punit->hp > 0);

      if (punit->transported_by != -1) {
        transporter = find_unit_by_id(punit->transported_by);
        assert(transporter != NULL);

	/* Make sure the transporter is on the tile. */
	unit_list_iterate(punit->tile->units, tile_unit) {
	  if (tile_unit == transporter) {
	    transporter2 = tile_unit;
	  }
	} unit_list_iterate_end;
	assert(transporter2 != NULL);

        /* Also in the list of owner? */
        assert(player_find_unit_by_id(get_player(transporter->owner),
				      punit->transported_by) != NULL);
        assert(same_pos(ptile, transporter->tile));

        /* Transporter capacity will be checked when transporter itself
	 * is checked */
      }

      /* Check for ground units in the ocean. */
      if (!pcity
	  && is_ocean(map_get_terrain(ptile))
	  && is_ground_unit(punit)) {
        assert(punit->transported_by != -1);
        assert(!is_ground_unit(transporter));
        assert(is_ground_units_transport(transporter));
      } else if (!pcity
                 && !is_ocean(map_get_terrain(ptile))
	         && is_sailing_unit(punit)) {
        assert(punit->transported_by != -1);
        assert(!is_sailing_unit(transporter));
        assert(FALSE); /* assert(is_sailing_units_transport(transporter)); */
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

    if (!pplayer->is_alive) {
      /* Don't do these checks.  Note there are some dead-players
       * sanity checks below. */
      continue;
    }

    city_list_iterate(pplayer->cities, pcity) {
      if (is_capital(pcity)) {
	found_palace++;
      }
      assert(found_palace <= 1);
    } city_list_iterate_end;

    players_iterate(pplayer2) {
      assert(pplayer->diplstates[pplayer2->player_no].type
	     == pplayer2->diplstates[pplayer->player_no].type);
      if (pplayer->diplstates[pplayer2->player_no].type == DS_CEASEFIRE) {
	assert(pplayer->diplstates[pplayer2->player_no].turns_left
	       == pplayer2->diplstates[pplayer->player_no].turns_left);
      }
    } players_iterate_end;

    if (pplayer->revolution_finishes == -1) {
      if (pplayer->government == game.government_when_anarchy) {
        freelog(LOG_FATAL, "%s's government is anarchy but does not finish",
                pplayer->name);
      }
      assert(pplayer->government != game.government_when_anarchy);
    } else if (pplayer->revolution_finishes > game.turn) {
      assert(pplayer->government == game.government_when_anarchy);
    } else {
      /* Things may vary in this case depending on when the sanity_check
       * call is made.  No better check is possible. */
    }
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

/**************************************************************************
...
**************************************************************************/
void sanity_check(void)
{
  if (!map_is_empty()) {
    /* Don't sanity-check the map if it hasn't been created yet (this
     * happens when loading scenarios). */
    check_specials();
    check_map();
    check_cities();
    check_units();
    check_fow();
  }
  check_misc();
  check_players();
}

#endif /* SANITY_CHECKING */
