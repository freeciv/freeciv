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
#include <fc_config.h>
#endif

#include <stdio.h> /* for remove() */ 

/* utility */
#include "capability.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "rand.h"
#include "registry.h"
#include "shared.h"
#include "string_vector.h"
#include "support.h"

/* common */
#include "ai.h"
#include "events.h"
#include "game.h"
#include "improvement.h"
#include "movement.h"
#include "packets.h"

/* server */
#include "connecthand.h"
#include "ggzserver.h"
#include "maphand.h"
#include "notify.h"
#include "plrhand.h"
#include "srv_main.h"
#include "unittools.h"

/* server/advisors */
#include "advdata.h"

#include "gamehand.h"

#define CHALLENGE_ROOT "challenge"


#define SPECLIST_TAG startpos
#define SPECLIST_TYPE struct startpos
#include "speclist.h"
#define startpos_list_iterate(list, plink, psp)                             \
  TYPED_LIST_BOTH_ITERATE(struct startpos_list_link, struct startpos,       \
                          list, plink, psp)
#define startpos_list_iterate_end LIST_BOTH_ITERATE_END

/****************************************************************************
  Get unit_type for given role character
****************************************************************************/
struct unit_type *crole_to_unit_type(char crole,struct player *pplayer)
{
  struct unit_type *utype = NULL;
  enum unit_role_id role;

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
    fc_assert_ret_val(FALSE, NULL);
    return NULL;
  }

  /* Create the unit of an appropriate type, if it exists */
  if (num_role_units(role) > 0) {
    if (pplayer != NULL) {
      utype = first_role_unit_for_player(pplayer, role);
    }
    if (utype == NULL) {
      utype = get_role_unit(role, 0);
    }
  }

  return utype;
}

/****************************************************************************
  Place a starting unit for the player. Returns tile where unit was really
  placed.
****************************************************************************/
static struct tile *place_starting_unit(struct tile *starttile,
                                        struct player *pplayer,
                                        char crole)
{
  struct tile *ptile = NULL;
  struct unit_type *utype = crole_to_unit_type(crole, pplayer);

  if (utype != NULL) {
    iterate_outward(starttile, map.xsize + map.ysize, itertile) {
      if (!is_non_allied_unit_tile(itertile, pplayer)
          && is_native_tile(utype, itertile)) {
        ptile = itertile;
        break;
      }
    } iterate_outward_end;
  }

  if (ptile == NULL) {
    /* No place where unit may exist. */
    return NULL;
  }

  fc_assert_ret_val(!is_non_allied_unit_tile(ptile, pplayer), NULL);

  /* For scenarios or dispersion, huts may coincide with player starts (in 
   * other cases, huts are avoided as start positions).  Remove any such hut,
   * and make sure to tell the client, since we may have already sent this
   * tile (with the hut) earlier: */
  if (tile_has_special(ptile, S_HUT)) {
    tile_clear_special(ptile, S_HUT);
    update_tile_knowledge(ptile);
    log_verbose("Removed hut on start position for %s",
                player_name(pplayer));
  }

  /* Expose visible area. */
  map_show_circle(pplayer, ptile, game.server.init_vis_radius_sq);

  if (utype != NULL) {
    /* We cannot currently handle sea units as start units.
     * TODO: remove this code block when we can. */
    if (utype_move_type(utype) == UMT_SEA) {
      log_error("Sea moving start units are not yet supported, "
                "%s not created.",
                utype_rule_name(utype));
      notify_player(pplayer, NULL, E_BAD_COMMAND, ftc_server,
                    _("Sea moving start units are not yet supported. "
                      "Nobody gets %s."),
                    utype_name_translation(utype));
      return NULL;
    }

    (void) create_unit(pplayer, ptile, utype, FALSE, 0, 0);
    return ptile;
  }

  return NULL;
}

/****************************************************************************
  Find a valid position not far from our starting position.
****************************************************************************/
static struct tile *find_dispersed_position(struct player *pplayer,
                                            struct tile *pcenter)
{
  struct tile *ptile;
  int x, y;

  do {
    index_to_map_pos(&x, &y, tile_index(pcenter));
    x += fc_rand(2 * game.server.dispersion + 1) - game.server.dispersion;
    y += fc_rand(2 * game.server.dispersion + 1) - game.server.dispersion;
  } while (!((ptile = map_pos_to_tile(x, y))
             && tile_continent(pcenter) == tile_continent(ptile)
             && !is_ocean_tile(ptile)
             && !is_non_allied_unit_tile(ptile, pplayer)));

  return ptile;
}

/****************************************************************************
  Initialize a new game: place the players' units onto the map, etc.
****************************************************************************/
void init_new_game(void)
{
  struct startpos_list *impossible_list, *targeted_list, *flexible_list;
  struct tile *player_startpos[player_slot_count()];
  int placed_units[player_slot_count()];
  int players_to_place = player_count();

  randomize_base64url_string(server.game_identifier,
                             sizeof(server.game_identifier));

  fc_assert(player_count() <= map_startpos_count());

  /* Convert the startposition hash table in a linked lists, as we mostly
   * need now to iterate it now. And then, we will be able to remove the
   * assigned start postions one by one. */
  impossible_list = startpos_list_new();
  targeted_list = startpos_list_new();
  flexible_list = startpos_list_new();

  map_startpos_iterate(psp) {
    if (startpos_allows_all(psp)) {
      startpos_list_append(flexible_list, psp);
    } else {
      startpos_list_append(targeted_list, psp);
    }
  } map_startpos_iterate_end;

  fc_assert(startpos_list_size(targeted_list)
            + startpos_list_size(flexible_list) == map_startpos_count());

  memset(player_startpos, 0, sizeof(player_startpos));
  log_verbose("Placing players at start positions.");

  /* First assign start positions which requires certain nations only this
   * one. */
  if (0 < startpos_list_size(targeted_list)) {
    log_verbose("Assigning matching nations.");

    startpos_list_shuffle(targeted_list); /* Randomize. */
    do {
      struct nation_type *pnation;
      struct startpos_list_link *choice;
      bool removed = FALSE;

      /* Assign first players which can pick only one start position. */
      players_iterate(pplayer) {
        if (NULL != player_startpos[player_index(pplayer)]) {
          /* Already assigned. */
          continue;
        }

        pnation = nation_of_player(pplayer);
        choice = NULL;
        startpos_list_iterate(targeted_list, plink, psp) {
          if (startpos_nation_allowed(psp, pnation)) {
            if (NULL != choice) {
              choice = NULL;
              break; /* Many choices. */
            } else {
              choice = plink;
            }
          }
        } startpos_list_iterate_end;

        if (NULL != choice) {
          struct tile *ptile =
              startpos_tile(startpos_list_link_data(choice));

          player_startpos[player_index(pplayer)] = ptile;
          startpos_list_erase(targeted_list, choice);
          players_to_place--;
          removed = TRUE;
          log_verbose("Start position (%d, %d) matches player %s (%s).",
                      TILE_XY(ptile), player_name(pplayer),
                      nation_rule_name(pnation));
        }
      } players_iterate_end;

      if (!removed) {
        /* Make arbitrary choice for a player. */
        struct startpos *psp = startpos_list_back(targeted_list);
        struct tile *ptile = startpos_tile(psp);
        struct player *rand_plr = NULL;
        int i = 0;

        startpos_list_pop_back(targeted_list); /* Detach 'psp'. */
        players_iterate(pplayer) {
          if (NULL != player_startpos[player_index(pplayer)]) {
            /* Already assigned. */
            continue;
          }

          pnation = nation_of_player(pplayer);
          if (startpos_nation_allowed(psp, pnation) && 0 == fc_rand(++i)) {
            rand_plr = pplayer;
          }
        } players_iterate_end;

        if (NULL != rand_plr) {
          player_startpos[player_index(rand_plr)] = ptile;
          players_to_place--;
          log_verbose("Start position (%d, %d) matches player %s (%s).",
                      TILE_XY(ptile), player_name(rand_plr),
                      nation_rule_name(nation_of_player(rand_plr)));
        } else {
          /* This start position cannot be assigned. */
          log_verbose("Start position (%d, %d) cannot be assigned for "
                      "any player, keeping for the moment...",
                      TILE_XY(ptile));
          /* Keep it for later, we may need it. */
          startpos_list_append(impossible_list, psp);
        }
      }
    } while (0 < players_to_place && 0 < startpos_list_size(targeted_list));
  }

  /* Now assign left start positions to every players. */
  if (0 < players_to_place && 0 < startpos_list_size(flexible_list)) {
    struct tile *ptile;

    log_verbose("Assigning random start positions.");

    startpos_list_shuffle(flexible_list); /* Randomize. */
    players_iterate(pplayer) {
      if (NULL != player_startpos[player_index(pplayer)]) {
        /* Already assigned. */
        continue;
      }

      ptile = startpos_tile(startpos_list_front(flexible_list));
      player_startpos[player_index(pplayer)] = ptile;
      players_to_place--;
      startpos_list_pop_front(flexible_list);
      log_verbose("Start position (%d, %d) assigned randomly "
                  "to player %s (%s).", TILE_XY(ptile), player_name(pplayer),
                  nation_rule_name(nation_of_player(pplayer)));
      if (0 == startpos_list_size(flexible_list)) {
        break;
      }
    } players_iterate_end;
  }

  if (0 < players_to_place && 0 < startpos_list_size(impossible_list)) {
    struct tile *ptile;

    startpos_list_shuffle(impossible_list); /* Randomize. */
    players_iterate(pplayer) {
      if (NULL != player_startpos[player_index(pplayer)]) {
        /* Already assigned. */
        continue;
      }

      ptile = startpos_tile(startpos_list_front(impossible_list));
      player_startpos[player_index(pplayer)] = ptile;
      players_to_place--;
      startpos_list_pop_front(impossible_list);
      log_verbose("Start position (%d, %d) assigned by default "
                  "to player %s (%s).", TILE_XY(ptile), player_name(pplayer),
                  nation_rule_name(nation_of_player(pplayer)));
      if (0 == startpos_list_size(impossible_list)) {
        break;
      }
    } players_iterate_end;
  }

  fc_assert(0 == players_to_place);

  startpos_list_destroy(impossible_list);
  startpos_list_destroy(targeted_list);
  startpos_list_destroy(flexible_list);


  /* Loop over all players, creating their initial units... */
  players_iterate(pplayer) {
    /* We have to initialise the advisor and ai here as we could make contact
     * to other nations at this point. */
    adv_data_phase_init(pplayer, FALSE);
    CALL_PLR_AI_FUNC(phase_begin, pplayer, pplayer, FALSE);

    struct tile *ptile = player_startpos[player_index(pplayer)];

    fc_assert_action(NULL != ptile, continue);

    /* Place the first unit. */
    if (place_starting_unit(ptile, pplayer,
                            game.server.start_units[0]) != NULL) {
      placed_units[player_index(pplayer)] = 1;
    } else {
      placed_units[player_index(pplayer)] = 0;
    }
  } players_iterate_end;

  /* Place all other units. */
  players_iterate(pplayer) {
    int i;
    struct tile *const ptile = player_startpos[player_index(pplayer)];
    struct nation_type *nation = nation_of_player(pplayer);

    fc_assert_action(NULL != ptile, continue);

    /* Place global start units */
    for (i = 1; i < strlen(game.server.start_units); i++) {
      struct tile *rand_tile = find_dispersed_position(pplayer, ptile);

      /* Create the unit of an appropriate type. */
      if (place_starting_unit(rand_tile, pplayer,
                              game.server.start_units[i]) != NULL) {
        placed_units[player_index(pplayer)]++;
      }
    }

    /* Place nation specific start units (not role based!) */
    i = 0;
    while (NULL != nation->init_units[i] && MAX_NUM_UNIT_LIST > i) {
      struct tile *rand_tile = find_dispersed_position(pplayer, ptile);

      create_unit(pplayer, rand_tile, nation->init_units[i], FALSE, 0, 0);
      placed_units[player_index(pplayer)]++;
      i++;
    }
  } players_iterate_end;

  players_iterate(pplayer) {
    /* Close the active phase for advisor and ai for all players; it was
     * opened in the first loop above. */
    adv_data_phase_done(pplayer);
    CALL_PLR_AI_FUNC(phase_finished, pplayer, pplayer);

    fc_assert_msg(0 < placed_units[player_index(pplayer)],
                  _("No units placed for %s!"), player_name(pplayer));
  } players_iterate_end;

  shuffle_players();
}

/**************************************************************************
  Tell clients the year, and also update turn_done and nturns_idle fields
  for all players.
**************************************************************************/
void send_year_to_clients(int year)
{
  struct packet_new_year apacket;
  
  players_iterate(pplayer) {
    pplayer->nturns_idle++;
  } players_iterate_end;

  apacket.year = year;
  apacket.turn = game.info.turn;
  lsend_packet_new_year(game.est_connections, &apacket);

  /* Hmm, clients could add this themselves based on above packet? */
  notify_conn(game.est_connections, NULL, E_NEXT_YEAR, ftc_any,
              _("Year: %s"), textyear(year));
}

/**************************************************************************
  Send game_info packet; some server options and various stuff...
  dest==NULL means game.est_connections

  It may be sent at any time. It MUST be sent before any player info, 
  as it contains the number of players.  To avoid inconsistency, it
  SHOULD be sent after rulesets and any other server settings.
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

  /* Sometimes this function is called before the phase_timer is
   * initialized.  In that case we want to send the dummy value. */
  if (game.info.timeout > 0 && game.server.phase_timer) {
    /* Whenever the client sees this packet, it starts a new timer at 0;
     * but the server's timer is only ever reset at the start of a phase
     * (and game.info.seconds_to_phasedone is relative to this).
     * Account for the difference. */
    ginfo.seconds_to_phasedone = game.info.seconds_to_phasedone
        - read_timer_seconds(game.server.phase_timer);
  } else {
    /* unused but at least initialized */
    ginfo.seconds_to_phasedone = -1.0;
  }

  conn_list_iterate(dest, pconn) {
    send_packet_game_info(pconn, &ginfo);
  }
  conn_list_iterate_end;
}

/**************************************************************************
  Send current scenario info. dest NULL causes send to everyone
**************************************************************************/
void send_scenario_info(struct conn_list *dest)
{
  struct packet_scenario_info sinfo;

  if (!dest) {
    dest = game.est_connections;
  }

  sinfo = game.scenario;

  conn_list_iterate(dest, pconn) {
    send_packet_scenario_info(pconn, &sinfo);
  } conn_list_iterate_end;
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
  if (game.info.timeout < 1 || game.server.timeoutint == 0) {
    return game.info.timeout;
  }

  if (game.server.timeoutcounter >= game.server.timeoutint) {
    game.info.timeout += game.server.timeoutinc;
    game.server.timeoutinc *= game.server.timeoutincmult;

    game.server.timeoutcounter = 1;
    game.server.timeoutint += game.server.timeoutintinc;

    if (game.info.timeout > GAME_MAX_TIMEOUT) {
      notify_conn(game.est_connections, NULL, E_SETTING, ftc_server,
                  _("The turn timeout has exceeded its maximum value, "
                    "fixing at its maximum."));
      log_debug("game.info.timeout exceeded maximum value");
      game.info.timeout = GAME_MAX_TIMEOUT;
      game.server.timeoutint = 0;
      game.server.timeoutinc = 0;
    } else if (game.info.timeout < 0) {
      notify_conn(game.est_connections, NULL, E_SETTING, ftc_server,
                  _("The turn timeout is smaller than zero, "
                    "fixing at zero."));
      log_debug("game.info.timeout less than zero");
      game.info.timeout = 0;
    }
  } else {
    game.server.timeoutcounter++;
  }

  log_debug("timeout=%d, inc=%d incmult=%d\n   "
            "int=%d, intinc=%d, turns till next=%d",
            game.info.timeout, game.server.timeoutinc,
            game.server.timeoutincmult, game.server.timeoutint,
            game.server.timeoutintinc,
            game.server.timeoutint - game.server.timeoutcounter);

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
  if (game.info.timeout > 0 && game.server.timeoutaddenemymove > 0) {
    double maxsec = (read_timer_seconds(game.server.phase_timer)
		     + (double) game.server.timeoutaddenemymove);

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

  fc_snprintf(filename, sizeof(filename), "%s_%d_%d",
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
  static struct strvec *rulesets = NULL;
  size_t i;

  if (!rulesets) {
    /* This is only read once per server invocation.  Add a new ruleset
     * and you have to restart the server. */
    rulesets = fileinfolist(get_data_dirs(), RULESET_SUFFIX);
  }

  packet.ruleset_count = MIN(MAX_NUM_RULESETS, strvec_size(rulesets));
  for (i = 0; i < packet.ruleset_count; i++) {
    sz_strlcpy(packet.rulesets[i], strvec_get(rulesets, i));
  }

  send_packet_ruleset_choices(pc, &packet);
}


/**************************************************************************** 
  Opens a file specified by the packet and compares the packet values with
  the file values. Sends an answer to the client once it's done.
****************************************************************************/
void handle_single_want_hack_req(struct connection *pc,
                                 const struct packet_single_want_hack_req *
                                 packet)
{
  struct section_file *secfile;
  const char *token = NULL;
  bool you_have_hack = FALSE;

  if (!with_ggz) {
    if ((secfile = secfile_load(get_challenge_fullname(pc), FALSE))) {
      token = secfile_lookup_str(secfile, "challenge.token");
      you_have_hack = (token && strcmp(token, packet->token) == 0);
      secfile_destroy(secfile);
    } else {
      log_debug("Error reading '%s':\n%s", get_challenge_fullname(pc),
                secfile_error());
    }

    if (!token) {
      log_debug("Failed to read authentication token");
    }
  }

  if (you_have_hack) {
    conn_set_access(pc, ALLOW_HACK, TRUE);
  }

  dsend_packet_single_want_hack_reply(pc, you_have_hack);

  send_ruleset_choices(pc);
  send_conn_info(pc->self, NULL);
}
