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

#include "capability.h"
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
#include "srv_main.h"

#define CHALLENGE_ROOT "challenge"


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
static void place_starting_unit(struct tile *ptile, struct player *pplayer,
				char crole)
{
  Unit_Type_id utype;
  enum unit_flag_id role;

  assert(!is_non_allied_unit_tile(ptile, pplayer));

  /* For scenarios or dispersion, huts may coincide with player starts (in 
   * other cases, huts are avoided as start positions).  Remove any such hut,
   * and make sure to tell the client, since we may have already sent this
   * tile (with the hut) earlier: */
  if (map_has_special(ptile, S_HUT)) {
    map_clear_special(ptile, S_HUT);
    update_tile_knowledge(ptile);
    freelog(LOG_VERBOSE, "Removed hut on start position for %s",
	    pplayer->name);
  }

  /* Expose visible area. */
  circle_iterate(ptile, game.rgame.init_vis_radius_sq, ctile) {
    show_area(pplayer, ctile, 0);
  } circle_iterate_end;

  switch(crole) {
  case 'c':
    role = L_CITIES;
    break;
  case 'w':
    role = L_SETTLERS;
    break;
  case 'x':
    role = L_EXPLORER;
    break;
  case 'k':
    role = L_GAMELOSS;
    break;
  case 's':
    role = L_DIPLOMAT;
    break;
  case 'f':
    role = L_FERRYBOAT;
    break;
  case 'd':
    role = L_DEFEND_OK;
    break;
  case 'D':
    role = L_DEFEND_GOOD;
    break;
  case 'a':
    role = L_ATTACK_FAST;
    break;
  case 'A':
    role = L_ATTACK_STRONG;
    break;
  default: 
    assert(FALSE);
    return;
  }

  /* Create the unit of an appropriate type, if it exists */
  if (num_role_units(role) > 0) {
    utype = first_role_unit_for_player(pplayer, role);
    if (utype == U_LAST) {
      utype = get_role_unit(role, 0);
    }

    /* We cannot currently handle sea units as start units. */
    if (unit_types[utype].move_type == SEA_MOVING) {
      freelog(LOG_ERROR, _("Sea moving start units are not yet supported, "
                           "%s not created."), unit_types[utype].name);
      notify_player(pplayer, _("Sea moving start units are not yet supported. "
                               "Nobody gets %s."), unit_types[utype].name);
      return;
    }

    (void) create_unit(pplayer, ptile, utype, FALSE, 0, -1);
  }
}

/****************************************************************************
  Initialize a new game: place the players' units onto the map, etc.
****************************************************************************/
void init_new_game(void)
{
  const int NO_START_POS = -1;
  int start_pos[game.nplayers];
  bool pos_used[map.num_start_positions];
  int i, num_used = 0;

  init_game_id();

  /* Shuffle starting positions around so that they match up with the
   * desired players. */

  /* First set up some data fields. */
  freelog(LOG_VERBOSE, "Placing players at start positions.");
  for (i = 0; i < map.num_start_positions; i++) {
    Nation_Type_id n = map.start_positions[i].nation;

    pos_used[i] = FALSE;
    freelog(LOG_VERBOSE, "%3d : (%2d,%2d) : %d : %s",
	    i, map.start_positions[i].tile->x,
	    map.start_positions[i].tile->y,
	    n, (n >= 0 ? get_nation_name(n) : ""));
  }
  players_iterate(pplayer) {
    start_pos[pplayer->player_no] = NO_START_POS;
  } players_iterate_end;

  /* Second, assign a nation to a start position for that nation. */
  freelog(LOG_VERBOSE, "Assigning matching nations.");
  players_iterate(pplayer) {
    for (i = 0; i < map.num_start_positions; i++) {
      assert(pplayer->nation != NO_NATION_SELECTED);
      if (pplayer->nation == map.start_positions[i].nation) {
	freelog(LOG_VERBOSE, "Start_pos %d matches player %d (%s).",
		i, pplayer->player_no, get_nation_name(pplayer->nation));
	start_pos[pplayer->player_no] = i;
	pos_used[i] = TRUE;
	num_used++;
      }
    }
  } players_iterate_end;

  /* Third, assign players randomly to the remaining start positions. */
  freelog(LOG_VERBOSE, "Assigning random nations.");
  players_iterate(pplayer) {
    if (start_pos[pplayer->player_no] == NO_START_POS) {
      int which = myrand(map.num_start_positions - num_used);

      for (i = 0; i < map.num_start_positions; i++) {
	if (!pos_used[i]) {
	  if (which == 0) {
	    freelog(LOG_VERBOSE,
		    "Randomly assigning player %d (%s) to pos %d.",
		    pplayer->player_no, get_nation_name(pplayer->nation), i);
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

    /* don't give any units to observer */
    if (pplayer->is_observer) {
      continue;
    }

    /* Place the first unit. */
    place_starting_unit(pos.tile, pplayer, game.start_units[0]);
  } players_iterate_end;

  /* Place all other units. */
  players_iterate(pplayer) {
    int i, x, y;
    struct tile *ptile;
    struct start_position p
      = map.start_positions[start_pos[pplayer->player_no]];

    /* don't give any units to observer */
    if (pplayer->is_observer) {
      continue;
    }

    for (i = 1; i < strlen(game.start_units); i++) {
      do {
	x = p.tile->x + myrand(2 * game.dispersion + 1) - game.dispersion;
	y = p.tile->y + myrand(2 * game.dispersion + 1) - game.dispersion;
      } while (!((ptile = map_pos_to_tile(x, y))
		 && map_get_continent(p.tile) == map_get_continent(ptile)
		 && !is_ocean(map_get_terrain(ptile))
		 && !is_non_allied_unit_tile(ptile, pplayer)));


      /* Create the unit of an appropriate type. */
      place_starting_unit(ptile, pplayer, game.start_units[i]);
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
  notify_conn_ex(&game.game_connections, NULL, E_NEXT_YEAR, _("Year: %s"),
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
      notify_conn_ex(&game.game_connections, NULL, E_NOEVENT,
		     _("The turn timeout has exceeded its maximum value, "
		       "fixing at its maximum"));
      freelog(LOG_DEBUG, "game.timeout exceeded maximum value");
      game.timeout = GAME_MAX_TIMEOUT;
      game.timeoutint = 0;
      game.timeoutinc = 0;
    } else if (game.timeout < 0) {
      notify_conn_ex(&game.game_connections, NULL, E_NOEVENT,
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
  adjusts game.turn_start when enemy moves an unit, we see it and the 
  remaining timeout is smaller than the option
  It's possible to use a simular function to do that per player.
**************************************************************************/
void increase_timeout_because_unit_moved(void)
{
  if (game.timeout != 0){
    int seconds_to_turndone = game.turn_start + game.timeout - time(NULL);

    if (seconds_to_turndone < game.timeoutaddenemymove){
      game.turn_start = time(NULL) - game.timeout + game.timeoutaddenemymove;
      send_game_info(NULL);
    }	
  }
}

/************************************************************************** 
  generate challenge filename for this connection, cannot fail.
**************************************************************************/
static void gen_challenge_filename(struct connection *pc)
{
}

/************************************************************************** 
  get challenge filename for this connection.
**************************************************************************/
static const char *get_challenge_filename(struct connection *pc)
{
  static char filename[MAX_LEN_PATH];

  my_snprintf(filename, sizeof(filename), "%s_%d_%d",
      CHALLENGE_ROOT, srvarg.port, pc->id);

  return filename;
}

/************************************************************************** 
  get challenge full filename for this connection.
**************************************************************************/
static const char *get_challenge_fullname(struct connection *pc)
{
  static char fullname[MAX_LEN_PATH];

  interpret_tilde(fullname, sizeof(fullname), "~/.freeciv/");
  sz_strlcat(fullname, get_challenge_filename(pc));

  return fullname;
}

/************************************************************************** 
  find a file that we can write too, and return it's name.
**************************************************************************/
const char *new_challenge_filename(struct connection *pc)
{
  if (!has_capability("new_hack", pc->capability)) {
    return "";
  }

  gen_challenge_filename(pc);
  return get_challenge_filename(pc);
}


/************************************************************************** 
opens a file specified by the packet and compares the packet values with
the file values. Sends an answer to the client once it's done.
**************************************************************************/
void handle_single_want_hack_req(struct connection *pc,
    				 const struct packet_single_want_hack_req
				 *packet)
{
  struct section_file file;
  char *token = NULL;
  bool you_have_hack = FALSE;

  if (!has_capability("new_hack", pc->capability)) {
    dsend_packet_single_want_hack_reply(pc, FALSE);
    return ;
  }

  if (section_file_load_nodup(&file, get_challenge_fullname(pc))) {
    token = secfile_lookup_str_default(&file, NULL, "challenge.token");
    you_have_hack = (token && strcmp(token, packet->token) == 0);
    section_file_free(&file);
  }

  if (!token) {
    freelog(LOG_DEBUG, "Failed to read authentication token");
  }

  if (you_have_hack) {
    pc->access_level = ALLOW_HACK;
  }

  dsend_packet_single_want_hack_reply(pc, you_have_hack);
}
