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
#include <stdio.h> /* for remove() */ 

#include "events.h"
#include "fcintl.h"
#include "improvement.h"
#include "log.h"
#include "mem.h"
#include "packets.h"
#include "rand.h"
#include "registry.h"
#include "shared.h"
#include "support.h"

#include "maphand.h"
#include "plrhand.h"
#include "unittools.h"

#include "gamehand.h"

#define NUMBER_OF_TRIES 500
#ifndef __VMS
#  define CHALLENGE_ROOT ".freeciv-tmp"
#else /*VMS*/
#  define CHALLENGE_ROOT "freeciv-tmp"
#endif

static char challenge_filename[MAX_LEN_PATH];

/****************************************************************************
  Initialize the game.id variable to a random string of characters.
****************************************************************************/
static void init_game_id(void)
{
  static const char chars[] =
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  int i;

  for (i = 0; i < sizeof(game.id) - 1; i++) {
    game.id[i] = chars[myrand(sizeof(chars) - 1)];
  }
  game.id[i] = '\0';
}

/****************************************************************************
  Place a starting unit for the player.
****************************************************************************/
static void place_starting_unit(int x, int y, struct player *pplayer,
				enum unit_flag_id role)
{
  Unit_Type_id utype;

  assert(!is_non_allied_unit_tile(map_get_tile(x, y), pplayer));

  /* For scenarios or dispersion, huts may coincide with player starts (in 
   * other cases, huts are avoided as start positions).  Remove any such hut,
   * and make sure to tell the client, since we may have already sent this
   * tile (with the hut) earlier: */
  if (map_has_special(x, y, S_HUT)) {
    map_clear_special(x, y, S_HUT);
    send_tile_info(NULL, x, y);
    freelog(LOG_VERBOSE, "Removed hut on start position for %s",
	    pplayer->name);
  }

  /* Expose visible area. */
  circle_iterate(x, y, game.rgame.init_vis_radius_sq, cx, cy) {
    show_area(pplayer, cx, cy, 0);
  } circle_iterate_end;

  /* Create the unit of an appropriate type. */
  utype = get_role_unit(role, 0);
  (void) create_unit(pplayer, x, y, utype, FALSE, 0, -1);
}

/****************************************************************************
  Initialize a new game: place the players' units onto the map, etc.
****************************************************************************/
void init_new_game(void)
{
#define NO_START_POS -1
  int start_pos[game.nplayers];
  bool pos_used[map.num_start_positions];
  int i, num_used = 0;

  init_game_id();

  /* Shuffle starting positions around so that they match up with the
   * desired players. */

  /* First set up some data fields. */
  for (i = 0; i < map.num_start_positions; i++) {
    pos_used[i] = FALSE;
  }
  players_iterate(pplayer) {
    start_pos[pplayer->player_no] = NO_START_POS;
  } players_iterate_end;

  /* Second, assign a nation to a start position for that nation. */
  players_iterate(pplayer) {
    for (i = 0; i < map.num_start_positions; i++) {
      assert(pplayer->nation != NO_NATION_SELECTED);
      if (pplayer->nation == map.start_positions[i].nation) {
	start_pos[pplayer->player_no] = i;
	pos_used[i] = TRUE;
	num_used++;
      }
    }
  } players_iterate_end;

  /* Third, assign players randomly to the remaining start positions. */
  players_iterate(pplayer) {
    if (start_pos[pplayer->player_no] == NO_START_POS) {
      int which = myrand(map.num_start_positions - num_used);

      for (i = 0; i < map.num_start_positions; i++) {
	if (!pos_used[i]) {
	  if (which == 0) {
	    start_pos[pplayer->player_no] = i;
	    pos_used[i] = TRUE;
	    num_used++;
	    break;
	  }
	  which--;
	}
      }
    }
    assert(start_pos[pplayer->player_no] != NO_START_POS);
  } players_iterate_end;

  /* Loop over all players, creating their initial units... */
  players_iterate(pplayer) {
    struct start_position pos
      = map.start_positions[start_pos[pplayer->player_no]];

    /* Place the first unit. */
    assert(game.settlers > 0);
    place_starting_unit(pos.x, pos.y, pplayer, F_CITIES);
  } players_iterate_end;

  /* Place all other units. */
  players_iterate(pplayer) {
    int i, x, y;
    struct start_position p
      = map.start_positions[start_pos[pplayer->player_no]];

    for (i = 1; i < (game.settlers + game.explorer); i++) {
      do {
	x = p.x + myrand(2 * game.dispersion + 1) - game.dispersion;
	y = p.y + myrand(2 * game.dispersion + 1) - game.dispersion;
      } while (!(normalize_map_pos(&x, &y)
		 && map_get_continent(p.x, p.y) == map_get_continent(x, y)
		 && !is_ocean(map_get_terrain(x, y))
		 && !is_non_allied_unit_tile(map_get_tile(x, y),
					     pplayer)));


      /* Create the unit of an appropriate type. */
      place_starting_unit(x, y, pplayer,
			  (i < game.settlers) ? F_CITIES : L_EXPLORER);
    }
  } players_iterate_end;

  /* Initialise list of improvements with world-wide equiv_range */
  improvement_status_init(game.improvements, ARRAY_SIZE(game.improvements));
}

/**************************************************************************
...
**************************************************************************/
void send_start_turn_to_clients(void)
{
  lsend_packet_start_turn(&game.game_connections);
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
  lsend_packet_new_year(&game.game_connections, &apacket);

  /* Hmm, clients could add this themselves based on above packet? */
  notify_conn_ex(&game.game_connections, -1, -1, E_NEXT_YEAR, _("Year: %s"),
		 textyear(year));
}


/**************************************************************************
  Send specified state; should be a CLIENT_GAME_*_STATE ?
  (But note client also changes state from other events.)
**************************************************************************/
void send_game_state(struct conn_list *dest, int state)
{
  dlsend_packet_game_state(dest, state);
}


/**************************************************************************
  Send game_info packet; some server options and various stuff...
  dest==NULL means game.game_connections
**************************************************************************/
void send_game_info(struct conn_list *dest)
{
  struct packet_game_info ginfo;
  int i;

  if (!dest)
    dest = &game.game_connections;

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
  ginfo.diplomacy = game.diplomacy;
  ginfo.techpenalty = game.techpenalty;
  ginfo.foodbox = game.foodbox;
  ginfo.civstyle = game.civstyle;
  ginfo.spacerace = game.spacerace;
  ginfo.unhappysize = game.unhappysize;
  ginfo.angrycitizen = game.angrycitizen;
  ginfo.diplcost = game.diplcost;
  ginfo.freecost = game.freecost;
  ginfo.conquercost = game.conquercost;
  ginfo.cityfactor = game.cityfactor;
  for (i = 0; i < A_LAST /*game.num_tech_types */ ; i++)
    ginfo.global_advances[i] = game.global_advances[i];
  for (i = 0; i < B_LAST /*game.num_impr_types */ ; i++)
    ginfo.global_wonders[i] = game.global_wonders[i];
  /* the following values are computed every
     time a packet_game_info packet is created */
  if (game.timeout != 0) {
    ginfo.seconds_to_turndone =
	game.turn_start + game.timeout - time(NULL);
  } else {
    /* unused but at least initialized */
    ginfo.seconds_to_turndone = -1;
  }

  conn_list_iterate(*dest, pconn) {
    /* ? fixme: check for non-players: */
    ginfo.player_idx = (pconn->player ? pconn->player->player_no : -1);
    send_packet_game_info(pconn, &ginfo);
  }
  conn_list_iterate_end;
}

/**************************************************************************
  adjusts game.timeout based on various server options

  timeoutint: adjust game.timeout every timeoutint turns
  timeoutinc: adjust game.timeout by adding timeoutinc to it.
  timeoutintinc: every time we adjust game.timeout, we add timeoutintinc
                 to timeoutint.
  timeoutincmult: every time we adjust game.timeout, we multiply timeoutinc
                  by timeoutincmult
**************************************************************************/
int update_timeout(void)
{
  /* if there's no timer or we're doing autogame, do nothing */
  if (game.timeout < 1 || game.timeoutint == 0) {
    return game.timeout;
  }

  if (game.timeoutcounter >= game.timeoutint) {
    game.timeout += game.timeoutinc;
    game.timeoutinc *= game.timeoutincmult;

    game.timeoutcounter = 1;
    game.timeoutint += game.timeoutintinc;

    if (game.timeout > GAME_MAX_TIMEOUT) {
      notify_conn_ex(&game.game_connections, -1, -1, E_NOEVENT,
		     _("The turn timeout has exceeded its maximum value, "
		       "fixing at its maximum"));
      freelog(LOG_DEBUG, "game.timeout exceeded maximum value");
      game.timeout = GAME_MAX_TIMEOUT;
      game.timeoutint = 0;
      game.timeoutinc = 0;
    } else if (game.timeout < 0) {
      notify_conn_ex(&game.game_connections, -1, -1, E_NOEVENT,
		     _("The turn timeout is smaller than zero, "
		       "fixing at zero."));
      freelog(LOG_DEBUG, "game.timeout less than zero");
      game.timeout = 0;
    }
  } else {
    game.timeoutcounter++;
  }

  freelog(LOG_DEBUG, "timeout=%d, inc=%d incmult=%d\n   "
	  "int=%d, intinc=%d, turns till next=%d",
	  game.timeout, game.timeoutinc, game.timeoutincmult,
	  game.timeoutint, game.timeoutintinc,
	  game.timeoutint - game.timeoutcounter);

  return game.timeout;
}

/************************************************************************** 
find a suitably random file that we can write too, and return it's name.
**************************************************************************/
const char *create_challenge_filename(void)
{
  int i;
  unsigned int tmp = 0;

  /* find a suitable file to place the challenge in, we'll remove the file
   * once the challenge is  */
#ifndef CHALLENGE_PATH
  sz_strlcpy(challenge_filename, user_home_dir());
  sz_strlcat(challenge_filename, "/");
  sz_strlcat(challenge_filename, CHALLENGE_ROOT);
#else
  sz_strlcpy(challenge_filename, CHALLENGE_PATH);
#endif

  for (i = 0; i < NUMBER_OF_TRIES; i++) {
    char test_str[MAX_LEN_PATH];

    sz_strlcpy(test_str, challenge_filename);
    tmp = time(NULL);
    cat_snprintf(test_str, MAX_LEN_PATH, "-%d", tmp);

    /* file doesn't exist but we can create one and write to it */
    if (!is_reg_file_for_access(test_str, FALSE) &&
        is_reg_file_for_access(test_str, TRUE)) {
      cat_snprintf(challenge_filename, MAX_LEN_PATH, "-%d", tmp);
      break;
    } else {
      die("we can't seem to write to the filesystem!");
    }
  }

  return challenge_filename;
}

/************************************************************************** 
opens a file specified by the packet and compares the packet values with
the file values. Sends an answer to the client once it's done.
**************************************************************************/
void handle_single_want_hack_req(struct connection *pc, int challenge)
{
  struct section_file file;
  int entropy = 0;
  bool could_load = TRUE;
  bool you_have_hack = FALSE;

  if (section_file_load_nodup(&file, challenge_filename)) {
    entropy = secfile_lookup_int_default(&file, 0, "challenge.entropy");
    section_file_free(&file);
  } else {
    freelog(LOG_ERROR, "couldn't load temporary file: %s", challenge_filename);
    could_load = FALSE;
  }

  you_have_hack = (could_load && entropy && entropy == challenge);

  if (you_have_hack) {
    pc->access_level = ALLOW_HACK;
  }

  /* remove temp file */
  if (remove(challenge_filename) != 0) {
    freelog(LOG_ERROR, "couldn't remove temporary file: %s",
            challenge_filename);
  }

  dsend_packet_single_want_hack_reply(pc, you_have_hack);
}
