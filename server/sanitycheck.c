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

#include "log.h"

#include "city.h"
#include "game.h"
#include "government.h"
#include "map.h"
#include "movement.h"
#include "player.h"
#include "terrain.h"
#include "unit.h"
#include "unitlist.h"

#include "citytools.h"
#include "maphand.h"
#include "sanitycheck.h"
#include "unittools.h"

#ifdef SANITY_CHECKING

#define SANITY_CHECK(x)							\
  do {									\
    if (!(x)) {								\
      freelog(LOG_ERROR, "Failed sanity check: %s (%s:%d)",		\
	      #x, __FILE__,__LINE__);					\
    }									\
  } while(0)

#define SANITY_TILE(ptile, check)					\
  do {									\
    if (!(check)) {							\
      freelog(LOG_ERROR, "Failed sanity check at %s (%d, %d): "		\
              "%s (%s:%d)", ptile->city ? ptile->city->name 		\
              : ptile->terrain->name.vernacular, ptile->x, ptile->y, 	\
              #check, __FILE__,__LINE__);				\
    }									\
  } while(0)

#define SANITY_CITY(pcity, check)					\
  do {									\
    if (!(check)) {							\
      freelog(LOG_ERROR, "Failed sanity check in %s[%d](%d, %d): "	\
              "%s (%s:%d)", pcity->name, pcity->size, pcity->tile->x, 	\
               pcity->tile->y, #check, __FILE__,__LINE__);		\
    }									\
  } while(0)


/**************************************************************************
  Sanity checking on map (tile) specials.
**************************************************************************/
static void check_specials(void)
{
  whole_map_iterate(ptile) {
    const struct terrain *pterrain = tile_get_terrain(ptile);
    bv_special special = tile_get_special(ptile);

    if (contains_special(special, S_RAILROAD))
      SANITY_TILE(ptile, contains_special(special, S_ROAD));
    if (contains_special(special, S_FARMLAND))
      SANITY_TILE(ptile, contains_special(special, S_IRRIGATION));

    if (contains_special(special, S_MINE)) {
      SANITY_TILE(ptile, pterrain->mining_result == pterrain);
    }
    if (contains_special(special, S_IRRIGATION)) {
      SANITY_TILE(ptile, pterrain->irrigation_result == pterrain);
    }

    SANITY_TILE(ptile, terrain_index(pterrain) >= T_FIRST 
                       && terrain_index(pterrain) < terrain_count());
  } whole_map_iterate_end;
}

/**************************************************************************
  Sanity checking on fog-of-war (visibility, shared vision, etc.).
**************************************************************************/
static void check_fow(void)
{
  whole_map_iterate(ptile) {
    players_iterate(pplayer) {
      struct player_tile *plr_tile = map_get_player_tile(ptile, pplayer);

      vision_layer_iterate(v) {
	/* underflow of unsigned int */
	SANITY_TILE(ptile, plr_tile->seen_count[v] < 60000);
	SANITY_TILE(ptile, plr_tile->own_seen[v] < 60000);

	/* FIXME: It's not entirely clear that this is correct.  The
	 * invariants for when fogofwar is turned off are not clearly
	 * defined. */
	if (game.info.fogofwar) {
	  if (plr_tile->seen_count[v] > 0) {
	    SANITY_TILE(ptile, BV_ISSET(ptile->tile_seen[v], player_index(pplayer)));
	  } else {
	    SANITY_TILE(ptile, !BV_ISSET(ptile->tile_seen[v], player_index(pplayer)));
	  }
	}

	SANITY_TILE(ptile, plr_tile->own_seen[v] <= plr_tile->seen_count[v]);
      } vision_layer_iterate_end;

      /* Lots of server bits depend on this. */
      SANITY_TILE(ptile, plr_tile->seen_count[V_INVIS]
		   <= plr_tile->seen_count[V_MAIN]);
      SANITY_TILE(ptile, plr_tile->own_seen[V_INVIS]
		   <= plr_tile->own_seen[V_MAIN]);
    } players_iterate_end;
  } whole_map_iterate_end;

  SANITY_CHECK(game.government_when_anarchy != NULL);
  SANITY_CHECK(game.government_when_anarchy
	       == government_by_number(game.info.government_when_anarchy_id));
}

/**************************************************************************
  Miscellaneous sanity checks.
**************************************************************************/
static void check_misc(void)
{
  int nbarbs = 0;
  players_iterate(pplayer) {
    if (is_barbarian(pplayer)) {
      nbarbs++;
    }
  } players_iterate_end;
  SANITY_CHECK(nbarbs == game.info.nbarbarians);

  SANITY_CHECK(player_count() <= MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS);
  SANITY_CHECK(team_count() <= MAX_NUM_TEAMS);
}

/**************************************************************************
  Sanity checks on the map itself.  See also check_specials.
**************************************************************************/
static void check_map(void)
{
  whole_map_iterate(ptile) {
    struct city *pcity = tile_get_city(ptile);
    int cont = tile_get_continent(ptile), x, y;

    CHECK_INDEX(ptile->index);
    CHECK_MAP_POS(ptile->x, ptile->y);
    CHECK_NATIVE_POS(ptile->nat_x, ptile->nat_y);

    if (ptile->city) {
      SANITY_TILE(ptile, ptile->owner != NULL);
    }
    if (ptile->owner != NULL) {
      SANITY_TILE(ptile, ptile->owner_source != NULL);
    }

    index_to_map_pos(&x, &y, ptile->index);
    SANITY_TILE(ptile, x == ptile->x && y == ptile->y);

    index_to_native_pos(&x, &y, ptile->index);
    SANITY_TILE(ptile, x == ptile->nat_x && y == ptile->nat_y);

    if (is_ocean(tile_get_terrain(ptile))) {
      SANITY_TILE(ptile, cont < 0);
      adjc_iterate(ptile, tile1) {
	if (is_ocean(tile_get_terrain(tile1))) {
	  SANITY_TILE(ptile, tile_get_continent(tile1) == cont);
	}
      } adjc_iterate_end;
    } else {
      SANITY_TILE(ptile, cont > 0);
      adjc_iterate(ptile, tile1) {
	if (!is_ocean(tile_get_terrain(tile1))) {
	  SANITY_TILE(ptile, tile_get_continent(tile1) == cont);
	}
      } adjc_iterate_end;
    }

    if (pcity) {
      SANITY_TILE(ptile, same_pos(pcity->tile, ptile));
    }

    unit_list_iterate(ptile->units, punit) {
      SANITY_TILE(ptile, same_pos(punit->tile, ptile));

      /* Check diplomatic status of stacked units. */
      unit_list_iterate(ptile->units, punit2) {
	SANITY_TILE(ptile, pplayers_allied(unit_owner(punit), 
                                           unit_owner(punit2)));
      } unit_list_iterate_end;
      if (pcity) {
	SANITY_TILE(ptile, pplayers_allied(unit_owner(punit), 
                                           city_owner(pcity)));
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

  SANITY_CITY(pcity, pcity->size >= 1);
  SANITY_CITY(pcity, !terrain_has_flag(tile_get_terrain(pcity->tile),
                                       TER_NO_CITIES));
  SANITY_CITY(pcity, pcity->tile->owner == NULL
                     || pcity->tile->owner == pplayer);

  unit_list_iterate(pcity->units_supported, punit) {
    SANITY_CITY(pcity, punit->homecity == pcity->id);
    SANITY_CITY(pcity, unit_owner(punit) == pplayer);
  } unit_list_iterate_end;

  city_built_iterate(pcity, pimprove) {
    if (is_small_wonder(pimprove)) {
      SANITY_CITY(pcity, find_city_from_small_wonder(pplayer, pimprove) == pcity);
    } else if (is_great_wonder(pimprove)) {
      SANITY_CITY(pcity, find_city_from_great_wonder(pimprove) == pcity);
    }
  } city_built_iterate_end;

  /* Note that cities may be found on land or water. */

  city_map_iterate(x, y) {
    struct tile *ptile;

    if ((ptile = city_map_to_map(pcity, x, y))) {
      struct player *owner = tile_get_owner(ptile);

      switch (get_worker_city(pcity, x, y)) {
      case C_TILE_EMPTY:
	if (ptile->worked) {
	  freelog(LOG_ERROR, "Tile at %s->%d,%d%s marked as "
		  "empty but worked by %s!",
		  pcity->name, TILE_XY(ptile),
                  is_city_center(x, y) ? " (city center)" : "",
		  (ptile)->worked->name);
	}
	if (is_enemy_unit_tile(ptile, pplayer)) {
	  freelog(LOG_ERROR, "Tile at %s->%d,%d%s marked as "
		  "empty but occupied by an enemy unit!",
		  pcity->name, TILE_XY(ptile),
                  is_city_center(x, y) ? " (city center)" : "");
	}
	if (game.info.borders > 0 && owner && owner != pcity->owner) {
	  freelog(LOG_ERROR, "Tile at %s->%d,%d%s marked as "
		  "empty but in enemy territory!",
		  pcity->name, TILE_XY(ptile),
                  is_city_center(x, y) ? " (city center)" : "");
	}
	if (!city_can_work_tile(pcity, x, y)) {
	  /* Complete check. */
	  freelog(LOG_ERROR, "Tile at %s->%d,%d%s marked as "
		  "empty but is unavailable!",
		  pcity->name, TILE_XY(ptile),
                  is_city_center(x, y) ? " (city center)" : "");
	}
	break;
      case C_TILE_WORKER:
	if ((ptile)->worked != pcity) {
	  freelog(LOG_ERROR, "Tile at %s->%d,%d%s marked as "
		  "worked but main map disagrees!",
		  pcity->name, TILE_XY(ptile),
                  is_city_center(x, y) ? " (city center)" : "");
	}
	if (is_enemy_unit_tile(ptile, pplayer)) {
	  freelog(LOG_ERROR, "Tile at %s->%d,%d%s marked as "
		  "worked but occupied by an enemy unit!",
		  pcity->name, TILE_XY(ptile),
                  is_city_center(x, y) ? " (city center)" : "");
	}
	if (game.info.borders > 0 && owner && owner != pcity->owner) {
	  freelog(LOG_ERROR, "Tile at %s->%d,%d%s marked as "
		  "worked but in enemy territory!",
		  pcity->name, TILE_XY(ptile),
                  is_city_center(x, y) ? " (city center)" : "");
	}
	if (!city_can_work_tile(pcity, x, y)) {
	  /* Complete check. */
	  freelog(LOG_ERROR, "Tile at %s->%d,%d%s marked as "
		  "worked but is unavailable!",
		  pcity->name, TILE_XY(ptile),
                  is_city_center(x, y) ? " (city center)" : "");
	}
	break;
      case C_TILE_UNAVAILABLE:
	if (city_can_work_tile(pcity, x, y)) {
	  freelog(LOG_ERROR, "Tile at %s->%d,%d%s marked as "
		  "unavailable but seems to be available!",
		  pcity->name, TILE_XY(ptile),
                  is_city_center(x, y) ? " (city center)" : "");
	}
	break;
      }
    } else {
      SANITY_CITY(pcity, get_worker_city(pcity, x, y) == C_TILE_UNAVAILABLE);
    }
  } city_map_iterate_end;

  /* Sanity check city size versus worker and specialist counts. */
  city_map_iterate(x, y) {
    if (get_worker_city(pcity, x, y) == C_TILE_WORKER) {
      workers++;
    }
  } city_map_iterate_end;
  if (workers + city_specialists(pcity) != pcity->size + 1) {
    int diff = pcity->size + 1 - workers - city_specialists(pcity);

    SANITY_CITY(pcity, workers + city_specialists(pcity) == pcity->size + 1);
    if (diff > 0) {
      pcity->specialists[DEFAULT_SPECIALIST] += diff;
    } else if (diff < 0) {
      specialist_type_iterate(sp) {
	int num = MIN(-diff, pcity->specialists[sp]);

	diff += num;
	pcity->specialists[sp] -= num;
      } specialist_type_iterate_end;

      if (diff < 0) {
	city_map_checked_iterate(pcity->tile, city_x, city_y, ptile) {
	  if (ptile->worked == pcity && diff < 0) {
	    server_remove_worker_city(pcity, city_x, city_y);
	    diff++;
	  }
	} city_map_checked_iterate_end;
      }
    }

    generic_city_refresh(pcity, TRUE);
  }
}

/**************************************************************************
  Sanity checks on all cities in the world.
**************************************************************************/
static void check_cities(void)
{
  players_iterate(pplayer) {
    city_list_iterate(pplayer->cities, pcity) {
      SANITY_CITY(pcity, city_owner(pcity) == pplayer);

      sanity_check_city(pcity);
    } city_list_iterate_end;
  } players_iterate_end;

  whole_map_iterate(ptile) {
    if (ptile->worked) {
      struct city *pcity = ptile->worked;
      int city_x, city_y;
      bool is_valid;

      is_valid = map_to_city_map(&city_x, &city_y, pcity, ptile);
      SANITY_TILE(ptile, is_valid);

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
  Sanity checks on all units in the world.
**************************************************************************/
static void check_units(void) {
  players_iterate(pplayer) {
    unit_list_iterate(pplayer->units, punit) {
      struct tile *ptile = punit->tile;
      struct city *pcity;
      struct unit *transporter = NULL, *transporter2 = NULL;

      SANITY_CHECK(unit_owner(punit) == pplayer);

      if (punit->homecity != 0) {
	pcity = player_find_city_by_id(pplayer, punit->homecity);
	SANITY_CHECK(pcity != NULL);
	SANITY_CHECK(city_owner(pcity) == pplayer);
      }

      if (!can_unit_continue_current_activity(punit)) {
	freelog(LOG_ERROR, "%s at %d,%d (%s) has activity %s, "
		"which it can't continue!",
		unit_rule_name(punit),
		TILE_XY(ptile), tile_get_info_text(ptile, 0),
		get_activity_text(punit->activity));
      }

      pcity = tile_get_city(ptile);
      if (pcity) {
	SANITY_CHECK(pplayers_allied(city_owner(pcity), pplayer));
      }

      SANITY_CHECK(punit->moves_left >= 0);
      SANITY_CHECK(punit->hp > 0);

      if (punit->transported_by != -1) {
        transporter = game_find_unit_by_number(punit->transported_by);
        SANITY_CHECK(transporter != NULL);

	/* Make sure the transporter is on the tile. */
	unit_list_iterate(punit->tile->units, tile_unit) {
	  if (tile_unit == transporter) {
	    transporter2 = tile_unit;
	  }
	} unit_list_iterate_end;
	SANITY_CHECK(transporter2 != NULL);

        /* Also in the list of owner? */
        SANITY_CHECK(player_find_unit_by_id(transporter->owner,
				      punit->transported_by) != NULL);
        SANITY_CHECK(same_pos(ptile, transporter->tile));

        /* Transporter capacity will be checked when transporter itself
	 * is checked */
      }

      /* Check for ground units in the ocean. */
      if (!can_unit_exist_at_tile(punit, ptile)) {
        SANITY_CHECK(punit->transported_by != -1);
        SANITY_CHECK(can_unit_transport(transporter, punit));
      }

      /* Check for over-full transports. */
      SANITY_CHECK(get_transporter_occupancy(punit)
	     <= get_transporter_capacity(punit));
    } unit_list_iterate_end;
  } players_iterate_end;
}

/**************************************************************************
  Sanity checks on all players.
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

    SANITY_CHECK(!pplayer->nation || pplayer->nation->player == pplayer);

    city_list_iterate(pplayer->cities, pcity) {
      if (is_capital(pcity)) {
	found_palace++;
      }
      SANITY_CITY(pcity, found_palace <= 1);
    } city_list_iterate_end;

    players_iterate(pplayer2) {
      SANITY_CHECK(pplayer->diplstates[player_index(pplayer2)].type
	     == pplayer2->diplstates[player_index(pplayer)].type);
      if (pplayer->diplstates[player_index(pplayer2)].type == DS_CEASEFIRE) {
	SANITY_CHECK(pplayer->diplstates[player_index(pplayer2)].turns_left
	       == pplayer2->diplstates[player_index(pplayer)].turns_left);
      }
      if (pplayer->is_alive
          && pplayer2->is_alive
          && pplayers_allied(pplayer, pplayer2)) {
        SANITY_CHECK(pplayer_can_make_treaty(pplayer, pplayer2, DS_ALLIANCE)
                     != DIPL_ALLIANCE_PROBLEM);
      }
    } players_iterate_end;

    if (pplayer->revolution_finishes == -1) {
      if (government_of_player(pplayer) == game.government_when_anarchy) {
        freelog(LOG_FATAL, "%s's government is anarchy but does not finish",
                pplayer->name);
      }
      SANITY_CHECK(government_of_player(pplayer) != game.government_when_anarchy);
    } else if (pplayer->revolution_finishes > game.info.turn) {
      SANITY_CHECK(government_of_player(pplayer) == game.government_when_anarchy);
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
      SANITY_CHECK(unit_list_size(pplayer->units) == 0);
      SANITY_CHECK(city_list_size(pplayer->cities) == 0);
    }

    /* Dying players shouldn't be left around.  But they are. */
    SANITY_CHECK(!pplayer->is_dying);
  }

  nations_iterate(pnation) {
    SANITY_CHECK(!pnation->player || pnation->player->nation == pnation);
  } nations_iterate_end;
}

/****************************************************************************
  Sanity checking on teams.
****************************************************************************/
static void check_teams(void)
{
  int count[MAX_NUM_TEAMS], i;

  memset(count, 0, sizeof(count));
  players_iterate(pplayer) {
    /* For the moment, all players (including observers) have teams. */
    SANITY_CHECK(pplayer->team != NULL);
    if (pplayer->team) {
      count[team_index(pplayer->team)]++;
    }
  } players_iterate_end;

  for (i = 0; i < MAX_NUM_TEAMS; i++) {
    SANITY_CHECK(team_by_number(i)->players == count[i]);
  }
}

/**************************************************************************
  Sanity checking on connections.
**************************************************************************/
static void check_connections(void)
{
  /* est_connections is a subset of all_connections */
  SANITY_CHECK(conn_list_size(game.all_connections)
	       >= conn_list_size(game.est_connections));
}

/**************************************************************************
  Do sanity checks on the server state.  Call this once per turn or
  whenever you feel like it.

  But be careful, calling it too much would make the server slow down.  And
  at some times the server isn't supposed to be in a sane state so you
  can't call it in the middle of an operation that is supposed to be
  atomic.
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
  check_teams();
  check_connections();
}

#endif /* SANITY_CHECKING */
