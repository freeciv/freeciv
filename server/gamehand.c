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

#include "fcintl.h"
#include "improvement.h"
#include "log.h"
#include "mem.h"
#include "packets.h"
#include "rand.h"

#include "maphand.h"
#include "plrhand.h"
#include "unittools.h"

#include "gamehand.h"


/**************************************************************************
...
**************************************************************************/
void init_new_game(void)
{
  int i, j, x, y;
  int dx, dy;
  Unit_Type_id utype;
  int start_pos[MAX_NUM_PLAYERS]; /* indices into map.start_positions[] */

  if (!map.fixed_start_positions) {
    /* except in a scenario which provides them,
       shuffle the start positions around... */
    assert(game.nplayers==map.num_start_positions);
    for (i=0; i<game.nplayers;i++) { /* no advantage to the romans!! */
      j=myrand(game.nplayers);
      x=map.start_positions[j].x;
      y=map.start_positions[j].y;
      map.start_positions[j].x=map.start_positions[i].x;
      map.start_positions[j].y=map.start_positions[i].y;
      map.start_positions[i].x=x;
      map.start_positions[i].y=y;
    }
    for(i=0; i<game.nplayers; i++) {
      start_pos[i] = i;
    } 
  } else {
  /* In a scenario, choose starting positions by nation.
     If there are too few starts for number of nations, assign
     to nations with specific starts first, then assign rest
     to random from remainder.  (Would be better to label start
     positions by nation etc, but this will do for now. --dwp)
  */
    const int npos = map.num_start_positions;
    bool *pos_used = fc_calloc(npos, sizeof(bool));
    int nrem = npos;		/* remaining unused starts */
    
    for(i=0; i<game.nplayers; i++) {
      int nation = game.players[i].nation;
      if (nation < npos) {
	start_pos[i] = nation;
	pos_used[nation] = TRUE;
	nrem--;
      } else {
	start_pos[i] = npos;
      }
    }
    for(i=0; i<game.nplayers; i++) {
      if (start_pos[i] == npos) {
	int k;
	assert(nrem>0);
	k = myrand(nrem);
	for(j=0; j<npos; j++) {
	  if (!pos_used[j] && (0==k--)) {
	    start_pos[i] = j;
	    pos_used[j] = TRUE;
	    nrem--;
	    break;
	  }
	}
	assert(start_pos[i] != npos);
      }
    }
    free(pos_used);
    pos_used = NULL;
  }

  /* Loop over all players, creating their initial units... */
  for (i = 0; i < game.nplayers; i++) {
    /* Start positions are warranted to be land. */
    x = map.start_positions[start_pos[i]].x;
    y = map.start_positions[start_pos[i]].y;
    /* Loop over all initial units... */
    for (j = 0; j < (game.settlers + game.explorer); j++) {
      /* Determine a place to put the unit within the dispersion area.
         (Always put first unit on start position.) */
      if ((game.dispersion <= 0) || (j == 0)) {
	dx = x;
	dy = y;
      } else {
	do {
	  dx = x + myrand(2 * game.dispersion + 1) - game.dispersion;
	  dy = y + myrand(2 * game.dispersion + 1) - game.dispersion;
	  normalize_map_pos(&dx, &dy);
	} while (!(is_real_tile(dx, dy)
                   && map_get_continent(x, y) == map_get_continent(dx, dy)
                   && map_get_terrain(dx, dy) != T_OCEAN
                   && !is_non_allied_unit_tile(map_get_tile(dx, dy),
                    			       get_player(i))));
      }
      /* For scenarios or dispersion, huts may coincide with player
	 starts (in other cases, huts are avoided as start positions).
	 Remove any such hut, and make sure to tell the client, since
	 we may have already sent this tile (with the hut) earlier:
      */
      if (map_has_special(dx, dy, S_HUT)) {
        map_clear_special(dx, dy, S_HUT);
	send_tile_info(NULL, dx, dy);
        freelog(LOG_VERBOSE, "Removed hut on start position for %s",
		game.players[i].name);
      }
      /* Expose visible area. */
      circle_iterate(dx, dy, game.rgame.init_vis_radius_sq, cx, cy) {
	show_area(&game.players[i], cx, cy, 0);
      } circle_iterate_end;
      /* Create the unit of an appropriate type. */
      utype = get_role_unit((j < game.settlers) ? F_CITIES : L_EXPLORER, 0);
      create_unit(&game.players[i], dx, dy, utype, FALSE, 0, -1);
    }
  }

  /* Initialise list of improvements with world-wide equiv_range */
  improvement_status_init(game.improvements);

  /* Free vector of effects with world-wide range. */
  geff_vector_free(&game.effects);

  /* Free vector of destroyed effects */
  ceff_vector_free(&game.destroyed_effects);
}

/**************************************************************************
...
**************************************************************************/
void send_start_turn_to_clients(void)
{
  lsend_packet_generic_empty(&game.est_connections, PACKET_START_TURN);
}

/**************************************************************************
  Tell clients the year, and also update turn_done and nturns_idle fields
  for all players.
**************************************************************************/
void send_year_to_clients(int year)
{
  struct packet_new_year apacket;
  int i;
  
  for(i=0; i<game.nplayers; i++) {
    struct player *pplayer = &game.players[i];
    pplayer->turn_done = FALSE;
    pplayer->nturns_idle++;
  }

  apacket.year = year;
  apacket.turn = game.turn;
  lsend_packet_new_year(&game.est_connections, &apacket);

  /* Hmm, clients could add this themselves based on above packet? */
  notify_conn(&game.est_connections, _("Year: %s"), textyear(year));
}


/**************************************************************************
  Send specifed state; should be a CLIENT_GAME_*_STATE ?
  (But note client also changes state from other events.)
**************************************************************************/
void send_game_state(struct conn_list *dest, int state)
{
  struct packet_generic_integer pack;
  pack.value=state;
  lsend_packet_generic_integer(dest, PACKET_GAME_STATE, &pack);
}


/**************************************************************************
  Send game_info packet; some server options and various stuff...
  dest==NULL means game.est_connections
**************************************************************************/
void send_game_info(struct conn_list *dest)
{
  struct packet_game_info ginfo;
  int i;

  if (!dest)
    dest = &game.est_connections;

  ginfo.gold = game.gold;
  ginfo.tech = game.tech;
  ginfo.researchcost = game.researchcost;
  ginfo.skill_level = game.skill_level;
  ginfo.timeout = game.timeout;
  ginfo.end_year = game.end_year;
  ginfo.year = game.year;
  ginfo.turn = game.turn;
  ginfo.min_players = game.min_players;
  ginfo.max_players = game.max_players;
  ginfo.nplayers = game.nplayers;
  ginfo.globalwarming = game.globalwarming;
  ginfo.heating = game.heating;
  ginfo.nuclearwinter = game.nuclearwinter;
  ginfo.cooling = game.cooling;
  ginfo.techpenalty = game.techpenalty;
  ginfo.foodbox = game.foodbox;
  ginfo.civstyle = game.civstyle;
  ginfo.spacerace = game.spacerace;
  ginfo.unhappysize = game.unhappysize;
  ginfo.angrycitizen = game.angrycitizen;
  ginfo.cityfactor = game.cityfactor;
  for (i = 0; i < A_LAST /*game.num_tech_types */ ; i++)
    ginfo.global_advances[i] = game.global_advances[i];
  for (i = 0; i < B_LAST /*game.num_impr_types */ ; i++)
    ginfo.global_wonders[i] = game.global_wonders[i];
  /* the following values are computed every
     time a packet_game_info packet is created */
  if (game.timeout)
    ginfo.seconds_to_turndone =
	game.turn_start + game.timeout - time(NULL);

  conn_list_iterate(*dest, pconn) {
    /* ? fixme: check for non-players: */
    ginfo.player_idx = (pconn->player ? pconn->player->player_no : -1);
    send_packet_game_info(pconn, &ginfo);
  }
  conn_list_iterate_end;
}
