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
#include "game.h"
#include "improvement.h"
#include "log.h"
#include "mem.h"
#include "movement.h"
#include "packets.h"
#include "rand.h"
#include "registry.h"
#include "shared.h"
#include "support.h"

#include "connecthand.h"
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

  for (i = 0; i < ARRAY_SIZE(game.id) - 1; i++) {
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
  struct unit_type *utype;
  enum unit_flag_id role;

  assert(!is_non_allied_unit_tile(ptile, pplayer));

  /* For scenarios or dispersion, huts may coincide with player starts (in 
   * other cases, huts are avoided as start positions).  Remove any such hut,
   * and make sure to tell the client, since we may have already sent this
   * tile (with the hut) earlier: */
  if (tile_has_special(ptile, S_HUT)) {
    tile_clear_special(ptile, S_HUT);
    update_tile_knowledge(ptile);
    freelog(LOG_VERBOSE, "Removed hut on start position for %s",
	    pplayer->name);
  }

  /* Expose visible area. */
  map_show_circle(pplayer, ptile, game.info.init_vis_radius_sq);

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
    if (utype == NULL) {
      utype = get_role_unit(role, 0);
    }

    /* We cannot currently handle sea units as start units.
     * TODO: remove this code block when we can. */
    if (get_unit_move_type(utype) == SEA_MOVING) {
      freelog(LOG_ERROR, _("Sea moving start units are not yet supported, "
                           "%s not created."), utype->name);
      notify_player(pplayer, NULL, E_BAD_COMMAND,
		    _("Sea moving start units are not yet supported. "
		      "Nobody gets %s."), utype->name);
      return;
    }

    (void) create_unit(pplayer, ptile, utype, FALSE, 0, 0);
  }
}

/****************************************************************************
  Find a valid position not far from our starting position.
****************************************************************************/
static struct tile *find_dispersed_position(struct player *pplayer,
                                            struct start_position *p)
{
  struct tile *ptile;
  int x, y;

  do {
    x = p->tile->x + myrand(2 * game.info.dispersion + 1) 
        - game.info.dispersion;
    y = p->tile->y + myrand(2 * game.info.dispersion + 1)
        - game.info.dispersion;
  } while (!((ptile = map_pos_to_tile(x, y))
             && tile_get_continent(p->tile) == tile_get_continent(ptile)
             && !is_ocean(tile_get_terrain(ptile))
             && !is_non_allied_unit_tile(ptile, pplayer)));

  return ptile;
}

/****************************************************************************
  Initialize a new game: place the players' units onto the map, etc.
****************************************************************************/
void init_new_game(void)
{
  const int NO_START_POS = -1;
  int start_pos[game.info.nplayers];
  bool pos_used[map.num_start_positions];
  int i, num_used = 0;

  init_game_id();

  /* Shuffle starting positions around so that they match up with the
   * desired players. */

  /* First set up some data fields. */
  freelog(LOG_VERBOSE, "Placing players at start positions.");
  for (i = 0; i < map.num_start_positions; i++) {
    struct nation_type *n = map.start_positions[i].nation;

    pos_used[i] = FALSE;
    freelog(LOG_VERBOSE, "%3d : (%2d,%2d) : %d : %s",
	    i, map.start_positions[i].tile->x,
	    map.start_positions[i].tile->y,
	    n ? n->index : -1,
	    n ? n->name : "");
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

    /* Place the first unit. */
    place_starting_unit(pos.tile, pplayer, game.info.start_units[0]);
  } players_iterate_end;

  /* Place all other units. */
  players_iterate(pplayer) {
    int i;
    struct tile *ptile;
    struct nation_type *nation = get_nation_by_plr(pplayer);
    struct start_position p
      = map.start_positions[start_pos[pplayer->player_no]];

    /* Place global start units */
    for (i = 1; i < strlen(game.info.start_units); i++) {
      ptile = find_dispersed_position(pplayer, &p);

      /* Create the unit of an appropriate type. */
      place_starting_unit(ptile, pplayer, game.info.start_units[i]);
    }

    /* Place nation specific start units (not role based!) */
    i = 0;
    while (nation->init_units[i] != NULL && i < MAX_NUM_UNIT_LIST) {
      ptile = find_dispersed_position(pplayer, &p);
      create_unit(pplayer, ptile, nation->init_units[i], FALSE, 0, 0);
      i++;
    }
  } players_iterate_end;

  shuffle_players();
}

/**************************************************************************
  This is called once at the start of each phase to alert the clients to
  the new phase.  game.info.phase should be incremented before calling it.
**************************************************************************/
void send_start_phase_to_clients(void)
{
  /* This function is so simple it could probably be dropped... */
  dlsend_packet_start_phase(game.est_connections, game.info.phase);
}

/**************************************************************************
  Tell clients the year, and also update turn_done and nturns_idle fields
  for all players.
**************************************************************************/
void send_year_to_clients(int year)
{
  struct packet_new_year apacket;
  int i;
  
  for(i=0; i<game.info.nplayers; i++) {
    struct player *pplayer = &game.players[i];
    pplayer->nturns_idle++;
  }

  apacket.year = year;
  apacket.turn = game.info.turn;
  lsend_packet_new_year(game.est_connections, &apacket);

  /* Hmm, clients could add this themselves based on above packet? */
  notify_conn(game.est_connections, NULL, E_NEXT_YEAR, _("Year: %s"),
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
  dest==NULL means game.est_connections
**************************************************************************/
void send_game_info(struct conn_list *dest)
{
  struct packet_game_info ginfo;

  if (!dest) {
    dest = game.est_connections;
  }

  ginfo = game.info;

  /* the following values are computed every
     time a packet_game_info packet is created */
  if (game.info.timeout > 0 && game.phase_timer) {
    /* Sometimes this function is called before the phase_timer is
     * initialized.  In that case we want to send the dummy value. */
    ginfo.seconds_to_phasedone
      = game.info.seconds_to_phasedone - read_timer_seconds(game.phase_timer);
  } else {
    /* unused but at least initialized */
    ginfo.seconds_to_phasedone = -1.0;
  }

  conn_list_iterate(dest, pconn) {
    /* ? fixme: check for non-players: */
    ginfo.player_idx = (pconn->player ? pconn->player->player_no : -1);
    send_packet_game_info(pconn, &ginfo);
  }
  conn_list_iterate_end;
}

/**************************************************************************
  adjusts game.info.timeout based on various server options

  timeoutint: adjust game.info.timeout every timeoutint turns
  timeoutinc: adjust game.info.timeout by adding timeoutinc to it.
  timeoutintinc: every time we adjust game.info.timeout, we add timeoutintinc
                 to timeoutint.
  timeoutincmult: every time we adjust game.info.timeout, we multiply timeoutinc
                  by timeoutincmult
**************************************************************************/
int update_timeout(void)
{
  /* if there's no timer or we're doing autogame, do nothing */
  if (game.info.timeout < 1 || game.timeoutint == 0) {
    return game.info.timeout;
  }

  if (game.timeoutcounter >= game.timeoutint) {
    game.info.timeout += game.timeoutinc;
    game.timeoutinc *= game.timeoutincmult;

    game.timeoutcounter = 1;
    game.timeoutint += game.timeoutintinc;

    if (game.info.timeout > GAME_MAX_TIMEOUT) {
      notify_conn(game.est_connections, NULL, E_SETTING,
		     _("The turn timeout has exceeded its maximum value, "
		       "fixing at its maximum"));
      freelog(LOG_DEBUG, "game.info.timeout exceeded maximum value");
      game.info.timeout = GAME_MAX_TIMEOUT;
      game.timeoutint = 0;
      game.timeoutinc = 0;
    } else if (game.info.timeout < 0) {
      notify_conn(game.est_connections, NULL, E_SETTING,
		     _("The turn timeout is smaller than zero, "
		       "fixing at zero."));
      freelog(LOG_DEBUG, "game.info.timeout less than zero");
      game.info.timeout = 0;
    }
  } else {
    game.timeoutcounter++;
  }

  freelog(LOG_DEBUG, "timeout=%d, inc=%d incmult=%d\n   "
	  "int=%d, intinc=%d, turns till next=%d",
	  game.info.timeout, game.timeoutinc, game.timeoutincmult,
	  game.timeoutint, game.timeoutintinc,
	  game.timeoutint - game.timeoutcounter);

  return game.info.timeout;
}

/**************************************************************************
  adjusts game.seconds_to_turn_done when enemy moves a unit, we see it and
  the remaining timeout is smaller than the timeoutaddenemymove option.

  It's possible to use a similar function to do that per-player.  In
  theory there should be a separate timeout for each player and the
  added time should only go onto the victim's timer.
**************************************************************************/
void increase_timeout_because_unit_moved(void)
{
  if (game.info.timeout > 0 && game.timeoutaddenemymove > 0) {
    double maxsec = (read_timer_seconds(game.phase_timer)
		     + (double)game.timeoutaddenemymove);

    if (maxsec > game.info.seconds_to_phasedone) {
      game.info.seconds_to_phasedone = maxsec;
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
  gen_challenge_filename(pc);
  return get_challenge_filename(pc);
}


/************************************************************************** 
  Call this on a connection with HACK access to send it a set of ruleset
  choices.  Probably this should be called immediately when granting
  HACK access to a connection.
**************************************************************************/
static void send_ruleset_choices(struct connection *pc)
{
  struct packet_ruleset_choices packet;
  static char **rulesets = NULL;
  int i;

  if (pc->access_level != ALLOW_HACK) {
    freelog(LOG_ERROR, "Trying to send ruleset choices to "
	    "unprivileged client.");
    return;
  }

  if (!rulesets) {
    /* This is only read once per server invocation.  Add a new ruleset
     * and you have to restart the server. */
    rulesets = datafilelist(RULESET_SUFFIX);
  }

  for (i = 0; i < MAX_NUM_RULESETS && rulesets[i]; i++) {
    sz_strlcpy(packet.rulesets[i], rulesets[i]);
  }
  packet.ruleset_count = i;

  send_packet_ruleset_choices(pc, &packet);
}


/************************************************************************** 
opens a file specified by the packet and compares the packet values with
the file values. Sends an answer to the client once it's done.
**************************************************************************/
void handle_single_want_hack_req(struct connection *pc,
    				 struct packet_single_want_hack_req *packet)
{
  struct section_file file;
  char *token = NULL;
  bool you_have_hack = FALSE;

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

  send_ruleset_choices(pc);
  send_conn_info(pc->self, NULL);
}
