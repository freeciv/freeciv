/********************************************************************** 
 Freeciv - Copyright (C) 2003 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

/**********************************************************************
  Functions for creating barbarians in huts, land and sea
  Started by Jerzy Klek <jekl@altavista.net>
  with more ideas from Falk Hueffner 
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "nation.h"
#include "rand.h"
#include "support.h"
#include "tech.h"
#include "terrain.h"

#include "gamehand.h"
#include "gamelog.h"
#include "maphand.h"
#include "plrhand.h"
#include "srv_main.h"
#include "stdinhand.h"
#include "unithand.h"
#include "unittools.h"

#include "aidata.h"
#include "aitools.h"

#include "barbarian.h"

/*
 IDEAS:
 1. Unrest factors configurable via rulesets (distance and gov factor)
 2. Separate nations for Sea Raiders and Land Barbarians

 - are these good ideas ??????
*/

/**************************************************************************
  Is player a land barbarian?
**************************************************************************/
bool is_land_barbarian(struct player *pplayer)
{
  return (pplayer->ai.barbarian_type == LAND_BARBARIAN);
}

/**************************************************************************
  Is player a sea barbarian?
**************************************************************************/
static bool is_sea_barbarian(struct player *pplayer)
{
  return (pplayer->ai.barbarian_type == SEA_BARBARIAN);
}

/**************************************************************************
  Creates the land/sea barbarian player and inits some stuff. If 
  barbarian player already exists, return player pointer. If barbarians 
  are dead, revive them with a new leader :-)

  Dead barbarians forget the map and lose the money.
**************************************************************************/
static struct player *create_barbarian_player(bool land)
{
  int newplayer = game.nplayers;
  struct player *barbarians;

  players_iterate(barbarians) {
    if ((land && is_land_barbarian(barbarians))
        || (!land && is_sea_barbarian(barbarians))) {
      if (!barbarians->is_alive) {
        barbarians->economic.gold = 0;
        barbarians->is_alive = TRUE;
        barbarians->is_dying = FALSE;
        pick_ai_player_name(game.nation_count - 1, barbarians->name);
	sz_strlcpy(barbarians->username, ANON_USER_NAME);
        /* I need to make them to forget the map, I think */
	whole_map_iterate(ptile) {
	  map_clear_known(ptile, barbarians);
	} whole_map_iterate_end;
      }
      barbarians->economic.gold += 100;  /* New leader, new money */
      return barbarians;
    }
  } players_iterate_end;

  if (newplayer >= MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS) {
    die("Too many players in server/barbarian.c");
  }

  barbarians = &game.players[newplayer];

  /* make a new player */

  server_player_init(barbarians, TRUE);

  barbarians->nation = game.nation_count - 1;
  pick_ai_player_name(game.nation_count - 1, barbarians->name);

  game.nplayers++;
  game.nbarbarians++;
  game.max_players = game.nplayers;

  sz_strlcpy(barbarians->username, ANON_USER_NAME);
  barbarians->is_connected = FALSE;
  barbarians->government = game.default_government;
  barbarians->target_government = game.default_government;
  assert(barbarians->revolution_finishes < 0);
  barbarians->capital = FALSE;
  barbarians->economic.gold = 100;

  barbarians->turn_done = TRUE;

  /* Do the ai */
  barbarians->ai.control = TRUE;
  if (land) {
    barbarians->ai.barbarian_type = LAND_BARBARIAN;
  } else {
    barbarians->ai.barbarian_type = SEA_BARBARIAN;
  }
  set_ai_level_directer(barbarians, game.skill_level);
  init_tech(barbarians);
  give_initial_techs(barbarians);

  /* Ensure that we are at war with everyone else */
  players_iterate(pplayer) {
    if (pplayer != barbarians) {
      pplayer->diplstates[barbarians->player_no].type = DS_WAR;
      barbarians->diplstates[pplayer->player_no].type = DS_WAR;
    }
  } players_iterate_end;

  freelog(LOG_VERBOSE, "Created barbarian %s, player %d",
          barbarians->name, barbarians->player_no);
  notify_player_ex(NULL, NULL, E_UPRISING,
                   _("Barbarians gain a leader by the name %s.  Dangerous "
                     "times may lie ahead."), barbarians->name);
  gamelog(GAMELOG_PLAYER, barbarians);

  send_game_info(NULL);
  send_player_info(barbarians, NULL);

  return barbarians;
}

/**************************************************************************
  Check if a tile is land and free of enemy units
**************************************************************************/
static bool is_free_land(struct tile *ptile, struct player *who)
{
  return (!is_ocean(map_get_terrain(ptile))
	  && !is_non_allied_unit_tile((ptile), who));
}

/**************************************************************************
  Check if a tile is sea and free of enemy units
**************************************************************************/
static bool is_free_sea(struct tile *ptile, struct player *who)
{
  return (is_ocean(map_get_terrain(ptile))
	  && !is_non_allied_unit_tile((ptile), who));
}

/**************************************************************************
  Unleash barbarians means give barbarian player some units and move them 
  out of the hut, unless there's no place to go.

  Barbarian unit deployment algorithm: If enough free land around, deploy
  on land, if not enough land but some sea free, load some of them on 
  boats, otherwise (not much land and no sea) kill enemy unit and stay in 
  a village. The return value indicates if the explorer survived entering 
  the vilage.
**************************************************************************/
bool unleash_barbarians(struct tile *ptile)
{
  struct player *barbarians;
  int unit, unit_cnt, land_cnt = 0, sea_cnt = 0;
  int boat;
  int i, me;
  struct tile *utile = NULL;
  bool alive = TRUE;     /* explorer survived */

  if (game.barbarianrate == 0 || (game.year < game.onsetbarbarian)) {
    unit_list_iterate_safe((ptile)->units, punit) {
      wipe_unit(punit);
    } unit_list_iterate_safe_end;
    return FALSE;
  }

  unit_cnt = 3 + myrand(4);

  barbarians = create_barbarian_player(TRUE);
  me = barbarians->player_no;

  for (i = 0; i < unit_cnt; i++) {
    unit = find_a_unit_type(L_BARBARIAN, L_BARBARIAN_TECH);
    (void) create_unit(barbarians, ptile, unit, 0, 0, -1);
    freelog(LOG_DEBUG, "Created barbarian unit %s", unit_types[unit].name);
  }

  adjc_iterate(ptile, tile1) {
    land_cnt += is_free_land(tile1, barbarians) ? 1 : 0;
    sea_cnt += is_free_sea(tile1, barbarians) ? 1 : 0;
  } adjc_iterate_end;

  if (land_cnt >= 3) {           /* enough land, scatter guys around */
    unit_list_iterate((ptile)->units, punit2) {
      if (punit2->owner == me) {
        send_unit_info(NULL, punit2);
	do {
	  do {
	    utile = rand_neighbour(ptile);
	  } while (!is_free_land(utile, barbarians));
        } while (!handle_unit_move_request(punit2, utile, TRUE, FALSE));
        freelog(LOG_DEBUG, "Moved barbarian unit from %d %d to %d, %d", 
                ptile->x, ptile->y, utile->x, utile->y);
      }
    } unit_list_iterate_end;
  } else {
    if (sea_cnt > 0) {         /* maybe it's an island, try to get on boats */
      struct tile *btile = NULL;

      /* FIXME: If anyone knows what this code is supposed to do, rewrite
       * this comment to explain it. */
      unit_list_iterate((ptile)->units, punit2) {
        if (punit2->owner == me) {
          send_unit_info(NULL, punit2);
          while(TRUE) {
	    utile = rand_neighbour(ptile);
	    if (can_unit_move_to_tile(punit2, utile, TRUE)) {
	      break;
            }
	    if (btile && can_unit_move_to_tile(punit2, btile, TRUE)) {
	      utile = btile;
              break;
	    }
	    if (is_free_sea(utile, barbarians)) {
              boat = find_a_unit_type(L_BARBARIAN_BOAT, -1);
	      (void) create_unit(barbarians, utile, boat, 0, 0, -1);
	      btile = utile;
	      break;
	    }
          }
          (void) handle_unit_move_request(punit2, utile, TRUE, FALSE);
        }
      } unit_list_iterate_end;
    } else {             /* The village is surrounded! Kill the explorer. */
      unit_list_iterate_safe((ptile)->units, punit2) {
        if (punit2->owner != me) {
          wipe_unit(punit2);
          alive = FALSE;
        } else {
          send_unit_info(NULL, punit2);
        }
      } unit_list_iterate_safe_end;
    }
  }

  /* FIXME: I don't know if this is needed */
  if (utile) {
    show_area(barbarians, utile, 3);
  }

  return alive;
}

/**************************************************************************
  Is sea not further than a couple of tiles away from land?
**************************************************************************/
static bool is_near_land(struct tile *tile0)
{
  square_iterate(tile0, 4, ptile) {
    if (!is_ocean(map_get_terrain(ptile))) {
      return TRUE;
    }
  } square_iterate_end;

  return FALSE;
}

/**************************************************************************
  Return this or a neighbouring tile that is free of any units
**************************************************************************/
static struct tile *find_empty_tile_nearby(struct tile *ptile)
{
  square_iterate(ptile, 1, tile1) {
    if (unit_list_size(&(tile1)->units) == 0) {
      return tile1;
    }
  } square_iterate_end;

  return NULL;
}

/**************************************************************************
  The barbarians are summoned at a randomly chosen place if:
  1. It's not closer than MIN_UNREST_DIST and not further than 
     MAX_UNREST_DIST from the nearest city. City owner is called 'victim' 
     here.
  2. The place or a neighbouring tile must be empty to deploy the units.
  3. If it's the sea it shouldn't be far from the land. (questionable)
  4. Place must be known to the victim
  5. The uprising chance depends also on the victim empire size, its
     government (civil_war_chance) and barbarian difficulty level.
  6. The number of land units also depends slightly on victim's empire 
     size and barbarian difficulty level.
  Q: The empire size is used so there are no uprisings in the beginning 
     of the game (year is not good as it can be customized), but it seems 
     a bit unjust if someone is always small. So maybe it should rather 
     be an average number of cities (all cities/player num)? Depending 
     on the victim government type is also questionable.
**************************************************************************/
static void try_summon_barbarians(void)
{
  struct tile *ptile, *utile;
  int i, boat, cap, dist, unit;
  int uprise = 1;
  struct city *pc;
  struct player *barbarians, *victim;

  /* We attempt the summons on a particular, random position.  If this is
   * an invalid position then the summons simply fails this time.  This means
   * that a particular tile's chance of being summoned on is independent of
   * all the other tiles on the map - which is essential for balanced
   * gameplay. */
  ptile = rand_map_pos();

  if (terrain_has_flag(map_get_terrain(ptile), TER_NO_BARBS)) {
    return;
  }


  if (!(pc = dist_nearest_city(NULL, ptile, TRUE, FALSE))) {
    /* any city */
    return;
  }

  victim = city_owner(pc);

  dist = real_map_distance(ptile, pc->tile);
  freelog(LOG_DEBUG,"Closest city to %d %d is %s at %d %d which is %d far", 
          ptile->x, ptile->y, pc->name, pc->tile->x, pc->tile->y, dist);
  if (dist > MAX_UNREST_DIST || dist < MIN_UNREST_DIST) {
    return;
  }

  /* I think Sea Raiders can come out of unknown sea territory */
  if (!(utile = find_empty_tile_nearby(ptile))
      || (!map_is_known(utile, victim)
	  && !is_ocean(map_get_terrain(utile)))
      || !is_near_land(utile)) {
    return;
  }

  /* do not harass small civs - in practice: do not uprise at the beginning */
  if ((int)myrand(UPRISE_CIV_MORE) >
           (int)city_list_size(&victim->cities) -
                UPRISE_CIV_SIZE/(game.barbarianrate-1)
      || myrand(100) > get_gov_pcity(pc)->civil_war) {
    return;
  }
  freelog(LOG_DEBUG, "Barbarians are willing to fight");

  if (map_has_special(utile, S_HUT)) {
    /* remove the hut in place of uprising */
    map_clear_special(utile, S_HUT);
    update_tile_knowledge(utile);
  }

  if (!is_ocean(map_get_terrain(utile))) {
    /* land (disembark) barbarians */
    barbarians = create_barbarian_player(TRUE);
    if (city_list_size(&victim->cities) > UPRISE_CIV_MOST) {
      uprise = 3;
    }
    for (i = 0; i < myrand(3) + uprise * game.barbarianrate; i++) {
      unit = find_a_unit_type(L_BARBARIAN, L_BARBARIAN_TECH);
      (void) create_unit(barbarians, utile, unit, 0, 0, -1);
      freelog(LOG_DEBUG, "Created barbarian unit %s", unit_types[unit].name);
    }
    (void) create_unit(barbarians, utile,
		       get_role_unit(L_BARBARIAN_LEADER, 0), 0, 0, -1);
  } else {                   /* sea raiders - their units will be veteran */
    struct unit *ptrans;

    barbarians = create_barbarian_player(FALSE);
    boat = find_a_unit_type(L_BARBARIAN_BOAT,-1);
    ptrans = create_unit(barbarians, utile, boat, 0, 0, -1);
    cap = get_transporter_capacity(unit_list_get(&utile->units, 0));
    for (i = 0; i < cap-1; i++) {
      unit = find_a_unit_type(L_BARBARIAN_SEA,L_BARBARIAN_SEA_TECH);
      (void) create_unit_full(barbarians, utile, unit, 0, 0, -1, -1,
			      ptrans);
      freelog(LOG_DEBUG, "Created barbarian unit %s", unit_types[unit].name);
    }
    (void) create_unit_full(barbarians, utile,
			    get_role_unit(L_BARBARIAN_LEADER, 0), 0, 0,
			    -1, -1, ptrans);
  }

  /* Is this necessary?  create_unit_full already sends unit info. */
  unit_list_iterate(utile->units, punit2) {
    send_unit_info(NULL, punit2);
  } unit_list_iterate_end;

  /* to let them know where to get you */
  show_area(barbarians, utile, 3);
  show_area(barbarians, pc->tile, 3);

  /* There should probably be a different message about Sea Raiders */
  if (is_land_barbarian(barbarians)) {
    notify_player_ex(victim, utile, E_UPRISING,
		     _("Native unrest near %s led by %s."), pc->name,
		     barbarians->name);
  } else if (map_is_known_and_seen(utile, victim)) {
    notify_player_ex(victim, utile, E_UPRISING,
		     _("Sea raiders seen near %s!"), pc->name);
  }
}

/**************************************************************************
  Summon barbarians out of the blue. Try more times for more difficult 
  levels - which means there can be more than one uprising in one year!
**************************************************************************/
void summon_barbarians(void)
{
  int i, n;

  if (game.barbarianrate == 0) {
    return;
  }

  if (game.year < game.onsetbarbarian) {
    return;
  }

  n = map_num_tiles() / MAP_FACTOR;
  if (n == 0) {
    /* Allow barbarians on maps smaller than MAP_FACTOR */
    n = 1;
  }

  for (i = 0; i < n * (game.barbarianrate - 1); i++) {
    try_summon_barbarians();
  }
}
