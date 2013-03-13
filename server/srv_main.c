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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_SYS_TERMIO_H
#include <sys/termio.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_WINSOCK
#include <winsock.h>
#endif

/* utility */
#include "astring.h"
#include "bitvector.h"
#include "capability.h"
#include "fciconv.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "netintf.h"
#include "rand.h"
#include "registry.h"
#include "shared.h"
#include "support.h"
#include "timing.h"

/* common/aicore */
#include "citymap.h"

/* common */
#include "capstr.h"
#include "city.h"
#include "dataio.h"
#include "effects.h"
#include "events.h"
#include "fc_interface.h"
#include "game.h"
#include "government.h"
#include "map.h"
#include "mapimg.h"
#include "nation.h"
#include "packets.h"
#include "player.h"
#include "research.h"
#include "tech.h"
#include "unitlist.h"
#include "version.h"

/* generator */
#include "mapgen.h"

/* server/scripting */
#include "script_server.h"
#include "luascript_types.h"

/* server */
#include "aiiface.h"
#include "auth.h"
#include "barbarian.h"
#include "cityhand.h"
#include "citytools.h"
#include "cityturn.h"
#include "connecthand.h"
#include "console.h"
#include "fcdb.h"
#include "diplhand.h"
#include "edithand.h"
#include "gamehand.h"
#include "ggzserver.h"
#include "handchat.h"
#include "maphand.h"
#include "meta.h"
#include "notify.h"
#include "plrhand.h"
#include "report.h"
#include "ruleset.h"
#include "sanitycheck.h"
#include "savegame2.h"
#include "score.h"
#include "sernet.h"
#include "settings.h"
#include "spacerace.h"
#include "stdinhand.h"
#include "techtools.h"
#include "unithand.h"
#include "unittools.h"
#include "voting.h"

/* server/advisors */
#include "advdata.h"
#include "autosettlers.h"
#include "advbuilding.h"
#include "infracache.h"

#include "srv_main.h"

static void end_turn(void);
static void announce_player(struct player *pplayer);
static void fc_interface_init_server(void);

static enum known_type mapimg_server_tile_known(const struct tile *ptile,
                                                const struct player *pplayer,
                                                bool knowledge);
static struct terrain
  *mapimg_server_tile_terrain(const struct tile *ptile,
                              const struct player *pplayer, bool knowledge);
static struct player *mapimg_server_tile_owner(const struct tile *ptile,
                                               const struct player *pplayer,
                                               bool knowledge);
static struct player *mapimg_server_tile_city(const struct tile *ptile,
                                              const struct player *pplayer,
                                              bool knowledge);
static struct player *mapimg_server_tile_unit(const struct tile *ptile,
                                              const struct player *pplayer,
                                              bool knowledge);

static int mapimg_server_plrcolor_count(void);
static struct rgbcolor *mapimg_server_plrcolor_get(int i);

/* command-line arguments to server */
struct server_arguments srvarg;

/* server aggregate information */
struct civserver server;

/* server state information */
static enum server_states civserver_state = S_S_INITIAL;

/* this global is checked deep down the netcode. 
   packets handling functions can set it to none-zero, to
   force end-of-tick asap
*/
bool force_end_of_sniff;

#define IDENTITY_NUMBER_SIZE (1+MAX_UINT16)
BV_DEFINE(bv_identity_numbers, IDENTITY_NUMBER_SIZE);
bv_identity_numbers identity_numbers_used;

/* server initialized flag */
static bool has_been_srv_init = FALSE;

/**************************************************************************
  Initialize the game seed.  This may safely be called multiple times.
**************************************************************************/
void init_game_seed(void)
{
  if (game.server.seed == 0) {
    /* We strip the high bit for now because neither game file nor
       server options can handle unsigned ints yet. - Cedric */
    game.server.seed = time(NULL) & (MAX_UINT32 >> 1);
    log_debug("Setting game.seed:%d", game.server.seed);
  }
 
  if (!fc_rand_is_init()) {
    fc_srand(game.server.seed);
  }
}

/**************************************************************************
  Initialize freeciv server.
**************************************************************************/
void srv_init(void)
{
  i_am_server(); /* Tell to libfreeciv that we are server */

  /* NLS init */
  init_nls();

  /* This is before ai module initializations so that if ai module
   * wants to use registry files, it can. */
  registry_module_init();

  /* This must be before command line argument parsing.
     This allocates default ai, and we want that to take place before
     loading additional ai modules from command line. */
  ai_init();

  /* init server arguments... */

  srvarg.metaserver_no_send = DEFAULT_META_SERVER_NO_SEND;
  sz_strlcpy(srvarg.metaserver_addr, DEFAULT_META_SERVER_ADDR);
  srvarg.metaserver_name[0] = '\0';
  srvarg.serverid[0] = '\0';

  srvarg.bind_addr = NULL;
  srvarg.port = DEFAULT_SOCK_PORT;

  srvarg.bind_meta_addr = NULL;

  srvarg.loglevel = LOG_NORMAL;

  srvarg.log_filename = NULL;
  srvarg.fatal_assertions = -1;
  srvarg.ranklog_filename = NULL;
  srvarg.load_filename[0] = '\0';
  srvarg.script_filename = NULL;
  srvarg.saves_pathname = "";
  srvarg.scenarios_pathname = "";

  srvarg.quitidle = 0;

  srvarg.fcdb_enabled = FALSE;
  srvarg.fcdb_conf = NULL;
  srvarg.auth_enabled = FALSE;
  srvarg.auth_allow_guests = FALSE;
  srvarg.auth_allow_newusers = FALSE;

  /* mark as initialized */
  has_been_srv_init = TRUE;

  /* init character encodings. */
  init_character_encodings(FC_DEFAULT_DATA_ENCODING, FALSE);

  /* Initialize callbacks. */
  game.callbacks.unit_deallocate = identity_number_release;

  /* Initialize global mutexes */
  fc_init_mutex(&game.server.mutexes.city_list);

  /* done */
  return;
}

/**************************************************************************
  Handle client info packet
**************************************************************************/
void handle_client_info(struct connection *pc, enum gui_type gui,
                        const char *distribution)
{
  log_debug("%s's client has %s gui.", pc->username, gui_type_name(gui));
  if (strcmp(distribution, "")) {
    log_debug("It comes from %s distribution.", distribution);
  }
}

/**************************************************************************
  Return current server state.
**************************************************************************/
enum server_states server_state(void)
{
  return civserver_state;
}

/**************************************************************************
  Set current server state.
**************************************************************************/
void set_server_state(enum server_states newstate)
{
  civserver_state = newstate;
}

/**************************************************************************
  Returns iff the game was started once upon a time.
**************************************************************************/
bool game_was_started(void)
{
  return (!game.info.is_new_game || S_S_INITIAL != server_state());
}

/****************************************************************************
  Returns TRUE if any one game end condition is fulfilled, FALSE otherwise.

  This function will notify players but will not set the server_state(). The
  caller must set the server state to S_S_OVER if the function
  returns TRUE.
****************************************************************************/
bool check_for_game_over(void)
{
  int candidates, defeated;
  struct player *victor;
  int winners = 0;
  struct astring str = ASTRING_INIT;

  /* Check for scenario victory; dead players can win if they are on a team
   * with the winners. */
  players_iterate(pplayer) {
    if (player_status_check(pplayer, PSTATUS_WINNER)) {
      if (winners) {
        /* TRANS: Another entry in winners list (", the Tibetans") */
        astr_add(&str, Q_("?winners:, the %s"),
                 nation_plural_for_player(pplayer));
      } else {
        /* TRANS: Beginning of the winners list ("the French") */
        astr_add(&str, Q_("?winners:the %s"),
                 nation_plural_for_player(pplayer));
      }
      ggz_report_victor(pplayer);
      winners++;
    }
  } players_iterate_end;
  if (winners) {
    notify_conn(game.est_connections, NULL, E_GAME_END, ftc_server,
                /* TRANS: There can be several winners listed */
                _("Scenario victory to %s."), astr_str(&str));
    ggz_report_victory();
    astr_free(&str);
    return TRUE;
  }
  astr_free(&str);

  /* Count candidates for the victory. */
  candidates = 0;
  defeated = 0;
  victor = NULL;
  /* Do not use player_iterate_alive here - dead player must be counted as
   * defeated to end the game with a victory. */
  players_iterate(pplayer) {
    if (is_barbarian(pplayer)) {
      continue;
    }

    if ((pplayer)->is_alive
        && !player_status_check((pplayer), PSTATUS_SURRENDER)) {
      candidates++;
      victor = pplayer;
    } else {
      defeated++;
    }
  } players_iterate_end;

  if (0 == candidates) {
    notify_conn(game.est_connections, NULL, E_GAME_END, ftc_server,
                _("Game is over."));
    ggz_report_victory();
    return TRUE;
  } else if (0 < defeated) {
    /* If nobody conceded the game, it mays be a solo game or a single team
     * game. */
    fc_assert(NULL != victor);

    /* Quit if we have team victory. */
    if (1 < team_count()) {
      teams_iterate(pteam) {
        const struct player_list *members = team_members(pteam);
        int team_candidates = 0, team_defeated = 0;

        if (1 == player_list_size(members)) {
          /* This is not really a team, single players are handled below. */
          continue;
        }

        player_list_iterate(members, pplayer) {
          if (pplayer->is_alive
              && !player_status_check((pplayer), PSTATUS_SURRENDER)) {
            team_candidates++;
          } else {
            team_defeated++;
          }
        } player_list_iterate_end;

        fc_assert(team_candidates + team_defeated
                  == player_list_size(members));

        if (team_candidates == candidates && team_defeated < defeated) {
          /* We need a player in a other team to conced the game here. */
          notify_conn(game.est_connections, NULL, E_GAME_END, ftc_server,
                      _("Team victory to %s."),
                      team_name_translation(pteam));
          /* All players of the team win, even dead and surrended ones. */
          player_list_iterate(members, pplayer) {
            ggz_report_victor(pplayer);
          } player_list_iterate_end;
          ggz_report_victory();
          return TRUE;
        }
      } teams_iterate_end;
    }

    /* Check for allied victory. */
    if (1 < candidates && game.server.allied_victory) {
      struct player_list *winner_list = player_list_new();

      /* Try to build a winner list. */
      players_iterate_alive(pplayer) {
        if (is_barbarian(pplayer)
            || player_status_check((pplayer), PSTATUS_SURRENDER)) {
          continue;
        }

        player_list_iterate(winner_list, aplayer) {
          if (!pplayers_allied(aplayer, pplayer)) {
            player_list_destroy(winner_list);
            winner_list = NULL;
            break;
          }
        } player_list_iterate_end;

        if (NULL == winner_list) {
          break;
        }
        player_list_append(winner_list, pplayer);
      } players_iterate_alive_end;

      if (NULL != winner_list) {
        bool first = TRUE;

        fc_assert(candidates == player_list_size(winner_list));

        astr_init(&str);
        player_list_iterate(winner_list, pplayer) {
          if (first) {
            /* TRANS: Beginning of the winners list ("the French") */
            astr_add(&str, Q_("?winners:the %s"),
                     nation_plural_for_player(pplayer));
            first = FALSE;
          } else {
            /* TRANS: Another entry in winners list (", the Tibetans") */
            astr_add(&str, Q_("?winners:, the %s"),
                     nation_plural_for_player(pplayer));
          }
          ggz_report_victor(pplayer);
        } player_list_iterate_end;
        notify_conn(game.est_connections, NULL, E_GAME_END, ftc_server,
                    /* TRANS: There can be several winners listed */
                    _("Allied victory to %s."), astr_str(&str));
        ggz_report_victory();
        astr_free(&str);
        player_list_destroy(winner_list);
        return TRUE;
      }
    }

    /* Check for single player victory. */
    if (1 == candidates && NULL != victor) {
      bool found = FALSE; /* We need at least one enemy defeated. */

      players_iterate(pplayer) {
        if (pplayer != victor
            && !is_barbarian(pplayer)
            && (!pplayer->is_alive
                 || player_status_check((pplayer), PSTATUS_SURRENDER))
            && pplayer->team != victor->team
            && (!game.server.allied_victory
                || !pplayers_allied(victor, pplayer))) {
          found = TRUE;
          break;
        }
      } players_iterate_end;

      if (found) {
        notify_conn(game.est_connections, NULL, E_GAME_END, ftc_server,
                    _("Game ended in victory for %s."), player_name(victor));
        ggz_report_victor(victor);
        ggz_report_victory();
        return TRUE;
      }
    }
  }

  /* Quit if we are past the turn limit. */
  if (game.info.turn > game.server.end_turn) {
    notify_conn(game.est_connections, NULL, E_GAME_END, ftc_server,
                _("Game ended as the turn limit was exceeded."));
    ggz_report_victory();
    return TRUE;
  }

  /* Check for a spacerace win. */
  while ((victor = check_spaceship_arrival())) {
    const struct player_list *members;
    bool win;

    notify_player(NULL, NULL, E_SPACESHIP, ftc_server,
                  _("The %s spaceship has arrived at Alpha Centauri."),
                  nation_adjective_for_player(victor));

    if (!game.server.endspaceship) {
      /* Games does not end on spaceship arrival. At least print all the
       * arrival messages. */
      continue;
    }

    /* This guy has won, now check if anybody else wins with him. */
    members = team_members(victor->team);
    win = FALSE;
    player_list_iterate(members, pplayer) {
      if (pplayer->is_alive
          && !player_status_check((pplayer), PSTATUS_SURRENDER)) {
        /* We need at least one player to be a winner candidate in the
         * team. */
        win = TRUE;
        break;
      }
    } player_list_iterate_end;

    if (!win) {
      /* Let's try next arrival. */
      continue;
    }

    if (1 < player_list_size(members)) {
      notify_conn(NULL, NULL, E_GAME_END, ftc_server,
                  _("Team victory to %s."),
                  team_name_translation(victor->team));
      /* All players of the team win, even dead and surrended ones. */
      player_list_iterate(members, pplayer) {
        ggz_report_victor(pplayer);
      } player_list_iterate_end;
      ggz_report_victory();
    } else {
      notify_conn(NULL, NULL, E_GAME_END, ftc_server,
                  _("Game ended in victory for %s."), player_name(victor));
      ggz_report_victor(victor);
      ggz_report_victory();
    }
    return TRUE;
  }

  return FALSE;
}

/**************************************************************************
  Send all information for when game starts or client reconnects.
  Initial packets should have been sent before calling this function.
  See comment in connecthand.c::establish_new_connection().
**************************************************************************/
void send_all_info(struct conn_list *dest)
{
  conn_list_iterate(dest, pconn) {
    if (conn_controls_player(pconn)) {
      send_attribute_block(pconn->playing, pconn);
    }
  } conn_list_iterate_end;

  /* Resend player info because it could have more infos (e.g. embassy). */
  send_player_all_c(NULL, dest);
  send_map_info(dest);
  send_all_known_tiles(dest);
  send_all_known_cities(dest);
  send_all_known_units(dest);
  send_spaceship_info(NULL, dest);
}

/**************************************************************************
  Give map information to players with EFT_REVEAL_CITIES or
  EFT_REVEAL_MAP effects (traditionally from the Apollo Program).
**************************************************************************/
static void do_reveal_effects(void)
{
  phase_players_iterate(pplayer) {
    if (get_player_bonus(pplayer, EFT_REVEAL_CITIES) > 0) {
      players_iterate(other_player) {
	city_list_iterate(other_player->cities, pcity) {
	  map_show_tile(pplayer, pcity->tile);
	} city_list_iterate_end;
      } players_iterate_end;
    }
    if (get_player_bonus(pplayer, EFT_REVEAL_MAP) > 0) {
      /* map_know_all will mark all unknown tiles as known and send
       * tile, unit, and city updates as necessary.  No other actions are
       * needed. */
      map_show_all(pplayer);
    }
  } phase_players_iterate_end;
}

/**************************************************************************
  Give contact to players with the EFT_HAVE_EMBASSIES effect (traditionally
  from Marco Polo's Embassy).
**************************************************************************/
static void do_have_embassies_effect(void)
{
  phase_players_iterate(pplayer) {
    if (get_player_bonus(pplayer, EFT_HAVE_EMBASSIES) > 0) {
      players_iterate(pother) {
	/* Note this gives pplayer contact with pother, but doesn't give
	 * pother contact with pplayer.  This may cause problems in other
	 * parts of the code if we're not careful. */
	make_contact(pplayer, pother, NULL);
      } players_iterate_end;
    }
  } phase_players_iterate_end;
}

/**************************************************************************
  Handle environmental upsets, meaning currently pollution or fallout.
**************************************************************************/
static void update_environmental_upset(enum tile_special_type cause,
				       int *current, int *accum, int *level,
				       void (*upset_action_fn)(int))
{
  int count;

  count = 0;
  whole_map_iterate(ptile) {
    if (tile_has_special(ptile, cause)) {
      count++;
    }
  } whole_map_iterate_end;

  *current = count;
  *accum += count;
  if (*accum < *level) {
    *accum = 0;
  } else {
    *accum -= *level;
    if (fc_rand((map_num_tiles() + 19) / 20) <= *accum) {
      upset_action_fn((map.xsize / 10) + (map.ysize / 10) + ((*accum) * 5));
      *accum = 0;
      *level += (map_num_tiles() + 999) / 1000;
    }
  }

  log_debug("environmental_upset: cause=%-4d current=%-2d "
            "level=%-2d accum=%-2d", cause, *current, *level, *accum);
}

/**************************************************************************
  Remove illegal units when armistice turns into peace treaty.
**************************************************************************/
static void remove_illegal_armistice_units(struct player *plr1,
                                           struct player *plr2)
{
  /* Remove illegal units */
  unit_list_iterate_safe(plr1->units, punit) {
    if (tile_owner(unit_tile(punit)) == plr2
        && is_military_unit(punit)) {
      notify_player(plr1, unit_tile(punit), E_DIPLOMACY, ftc_server,
                    _("Your %s was disbanded in accordance with "
                      "your peace treaty with the %s."),
                    unit_tile_link(punit),
                    nation_plural_for_player(plr2));
      wipe_unit(punit, ULR_ARMISTICE, NULL);
    }
  } unit_list_iterate_safe_end;
  unit_list_iterate_safe(plr2->units, punit) {
    if (tile_owner(unit_tile(punit)) == plr1
        && is_military_unit(punit)) {
      notify_player(plr2, unit_tile(punit), E_DIPLOMACY, ftc_server,
                    _("Your %s was disbanded in accordance with "
                      "your peace treaty with the %s."),
                    unit_tile_link(punit),
                    nation_plural_for_player(plr1));
      wipe_unit(punit, ULR_ARMISTICE, NULL);
    }
  } unit_list_iterate_safe_end;
}

/**************************************************************************
  Check for cease-fires and armistices running out; update cancelling 
  reasons and contact information.
**************************************************************************/
static void update_diplomatics(void)
{
  players_iterate(plr1) {
    players_iterate(plr2) {
      struct player_diplstate *state = player_diplstate_get(plr1, plr2);
      struct player_diplstate *state2 = player_diplstate_get(plr2, plr1);

      state->has_reason_to_cancel = MAX(state->has_reason_to_cancel - 1, 0);
      state->contact_turns_left = MAX(state->contact_turns_left - 1, 0);

      if (state->type == DS_ARMISTICE) {
        state->turns_left--;
        if (state->turns_left <= 0) {
          state->type = DS_PEACE;
          state2->type = DS_PEACE;
          state->turns_left = 0;
          state2->turns_left = 0;
          remove_illegal_armistice_units(plr1, plr2);
        }
      }

      if (state->type == DS_CEASEFIRE) {
        state->turns_left--;
        switch(state->turns_left) {
        case 1:
          notify_player(plr1, NULL, E_DIPLOMACY, ftc_server,
                        _("Concerned citizens point out that the cease-fire "
                          "with %s will run out soon."), player_name(plr2));
          /* Message to plr2 will be done when plr1 and plr2 will be swapped.
           * Else, we will get a message duplication.  Note the case is not
           * the below, because the state will be changed for both players to
           * war. */
          break;
        case 0:
          notify_player(plr1, NULL, E_DIPLOMACY, ftc_server,
                        _("The cease-fire with %s has run out. "
                          "You are now at war with the %s."),
                        player_name(plr2),
                        nation_plural_for_player(plr2));
          notify_player(plr2, NULL, E_DIPLOMACY, ftc_server,
                        _("The cease-fire with %s has run out. "
                          "You are now at war with the %s."),
                        player_name(plr1),
                        nation_plural_for_player(plr1));
          state->type = DS_WAR;
          state2->type = DS_WAR;
          state->turns_left = 0;
          state2->turns_left = 0;

          enter_war(plr1, plr2);

          city_map_update_all_cities_for_player(plr1);
          city_map_update_all_cities_for_player(plr2);
          sync_cities();

          /* Avoid love-love-hate triangles */
          players_iterate_alive(plr3) {
            if (plr3 != plr1 && plr3 != plr2
                && pplayers_allied(plr3, plr1)
                && pplayers_allied(plr3, plr2)) {
              notify_player(plr3, NULL, E_TREATY_BROKEN, ftc_server,
                            _("The cease-fire between %s and %s has run out. "
                              "They are at war. You cancel your alliance "
                              "with both."),
                            player_name(plr1),
                            player_name(plr2));
              player_diplstate_get(plr3, plr1)->has_reason_to_cancel = TRUE;
              player_diplstate_get(plr2, plr2)->has_reason_to_cancel = TRUE;
              handle_diplomacy_cancel_pact(plr3, player_number(plr1), CLAUSE_ALLIANCE);
              handle_diplomacy_cancel_pact(plr3, player_number(plr2), CLAUSE_ALLIANCE);
            }
          } players_iterate_alive_end;
          break;
        }
      }
    } players_iterate_end;
  } players_iterate_end;
}

/****************************************************************************
  Check all players to see whether they are dying.

  WARNING: do not call this while doing any handling of players, units,
  etc.  If a player dies, all his units will be wiped and other data will
  be overwritten.
****************************************************************************/
static void kill_dying_players(void)
{
  bool voter_died = FALSE;

  players_iterate_alive(pplayer) {
    /* cities or units remain? */
    if (0 == city_list_size(pplayer->cities)
        && 0 == unit_list_size(pplayer->units)) {
      player_status_add(pplayer, PSTATUS_DYING);
    }
    /* also UTYF_GAMELOSS in unittools server_remove_unit() */
    if (player_status_check(pplayer, PSTATUS_DYING)) {
      /* Can't get more dead than this. */
      voter_died = voter_died || pplayer->is_connected;
      kill_player(pplayer);
    }
  } players_iterate_alive_end;

  if (voter_died) {
    send_updated_vote_totals(NULL);
  }
}

/**************************************************************************
  Called at the start of each (new) phase to do AI activities.
**************************************************************************/
static void ai_start_phase(void)
{
  phase_players_iterate(pplayer) {
    if (pplayer->ai_controlled) {
      CALL_PLR_AI_FUNC(first_activities, pplayer, pplayer);
    }
  } phase_players_iterate_end;
  kill_dying_players();
}

/**************************************************************************
Handle the beginning of each turn.
Note: This does not give "time" to any player;
      it is solely for updating turn-dependent data.
**************************************************************************/
static void begin_turn(bool is_new_turn)
{
  log_debug("Begin turn");

  event_cache_remove_old();

  /* Reset this each turn. */
  if (is_new_turn) {
    game.info.phase_mode = game.server.phase_mode_stored;
  }

  /* NB: Phase logic must match is_player_phase(). */
  switch (game.info.phase_mode) {
  case PMT_CONCURRENT:
    game.server.num_phases = 1;
    break;
  case PMT_PLAYERS_ALTERNATE:
    game.server.num_phases = player_count();
    break;
  case PMT_TEAMS_ALTERNATE:
    game.server.num_phases = team_count();
    break;
  default:
    log_error("Unrecognized phase mode %d in begin_turn().",
              game.info.phase_mode);
    game.server.num_phases = 1;
    break;
  }
  send_game_info(NULL);

  if (is_new_turn) {
    script_server_signal_emit("turn_started", 2,
                              API_TYPE_INT, game.info.turn,
                              API_TYPE_INT, game.info.year);
  }

  if (is_new_turn) {
    /* We build scores at the beginning of every turn.  We have to
     * build them at the beginning so that the AI can use the data,
     * and we are sure to have it when we need it. */
    players_iterate(pplayer) {
      calc_civ_score(pplayer);
    } players_iterate_end;
    log_civ_score_now();
  }

  /* find out if users attached to players have been attached to those players
   * for long enough. The first user to do so becomes "associated" to that
   * player for ranking purposes. */
  players_iterate(pplayer) {
    if (strcmp(pplayer->ranked_username, ANON_USER_NAME) == 0
        && pplayer->user_turns++ > TURNS_NEEDED_TO_RANK
	&& pplayer->is_alive) {
      sz_strlcpy(pplayer->ranked_username, pplayer->username);
    }
  } players_iterate_end;

  /* See if the value of fog of war has changed */
  if (is_new_turn && game.info.fogofwar != game.server.fogofwar_old) {
    if (game.info.fogofwar) {
      enable_fog_of_war();
      game.server.fogofwar_old = TRUE;
    } else {
      disable_fog_of_war();
      game.server.fogofwar_old = FALSE;
    }
  }

  if (is_new_turn && game.info.phase_mode == PMT_CONCURRENT) {
    log_debug("Shuffleplayers");
    shuffle_players();
  }

  if (is_new_turn) {
    game.info.phase = 0;
  }

  sanity_check();
}

/**************************************************************************
  Begin a phase of movement.  This handles all beginning-of-phase actions
  for one or more players.
**************************************************************************/
static void begin_phase(bool is_new_phase)
{
  log_debug("Begin phase");

  conn_list_do_buffer(game.est_connections);

  phase_players_iterate(pplayer) {
    pplayer->phase_done = FALSE;
  } phase_players_iterate_end;
  send_player_all_c(NULL, NULL);

  dlsend_packet_start_phase(game.est_connections, game.info.phase);

  /* Must be the first thing as it is needed for lots of functions below! */
  phase_players_iterate(pplayer) {
    /* human players also need this for building advice */
    adv_data_phase_init(pplayer, is_new_phase);
    CALL_PLR_AI_FUNC(phase_begin, pplayer, pplayer, is_new_phase);
  } phase_players_iterate_end;

  if (is_new_phase) {
    /* Unit "end of turn" activities - of course these actually go at
     * the start of the turn! */
    phase_players_iterate(pplayer) {
      update_unit_activities(pplayer); /* major network traffic */
      flush_packets();
    } phase_players_iterate_end;
  }

  phase_players_iterate(pplayer) {
    log_debug("beginning player turn for #%d (%s)",
              player_number(pplayer), player_name(pplayer));
    if (!pplayer->ai_controlled) {
      building_advisor(pplayer);
    }
  } phase_players_iterate_end;

  phase_players_iterate(pplayer) {
    send_player_cities(pplayer);
  } phase_players_iterate_end;

  flush_packets();  /* to curb major city spam */
  conn_list_do_unbuffer(game.est_connections);

  phase_players_iterate(pplayer) {
    update_revolution(pplayer);
  } phase_players_iterate_end;

  if (is_new_phase) {
    /* Try to avoid hiding events under a diplomacy dialog */
    phase_players_iterate(pplayer) {
      if (pplayer->ai_controlled && !is_barbarian(pplayer)) {
        CALL_PLR_AI_FUNC(diplomacy_actions, pplayer, pplayer);
      }
    } phase_players_iterate_end;

    log_debug("Aistartturn");
    ai_start_phase();
  }

  sanity_check();

  if (game.info.turn == 0 && game.server.first_timeout != -1) {
    game.info.seconds_to_phasedone = (double)game.server.first_timeout;
  } else {
    game.info.seconds_to_phasedone = (double)game.info.timeout;
  }
  game.server.phase_timer = timer_renew(game.server.phase_timer,
                                        TIMER_USER, TIMER_ACTIVE);
  timer_start(game.server.phase_timer);
  send_game_info(NULL);

  if (game.server.num_phases == 1) {
    /* All players in the same phase.
     * This means that AI has been handled above, and server
     * will be responsive again */
    lsend_packet_begin_turn(game.est_connections);
  }
}

/**************************************************************************
  End a phase of movement.  This handles all end-of-phase actions
  for one or more players.
**************************************************************************/
static void end_phase(void)
{
  log_debug("Endphase");
 
  /* 
   * This empties the client Messages window; put this before
   * everything else below, since otherwise any messages from the
   * following parts get wiped out before the user gets a chance to
   * see them.  --dwp
   */
  phase_players_iterate(pplayer) {
    /* Unlike the start_phase packet we only send this one to the active
     * player. */
    lsend_packet_end_phase(pplayer->connections);
  } phase_players_iterate_end;

  phase_players_iterate(pplayer) {
    if (A_UNSET == player_research_get(pplayer)->researching) {
      Tech_type_id next_tech =
          player_research_step(pplayer,
                               player_research_get(pplayer)->tech_goal);

      if (A_UNSET != next_tech) {
        choose_tech(pplayer, next_tech);
      } else {
        choose_random_tech(pplayer);
      }
      /* add the researched bulbs to the pool; do *NOT* checvk for finished
       * research */
      update_bulbs(pplayer, 0, FALSE);
    }
  } phase_players_iterate_end;

  /* Freeze sending of cities. */
  send_city_suppression(TRUE);

  /* AI end of turn activities */
  players_iterate(pplayer) {
    unit_list_iterate(pplayer->units, punit) {
      CALL_PLR_AI_FUNC(unit_turn_end, pplayer, punit);
    } unit_list_iterate_end;
  } players_iterate_end;
  phase_players_iterate(pplayer) {
    auto_settlers_player(pplayer);
    if (pplayer->ai_controlled) {
      CALL_PLR_AI_FUNC(last_activities, pplayer, pplayer);
    }
  } phase_players_iterate_end;

  /* Refresh cities */
  phase_players_iterate(pplayer) {
    player_research_get(pplayer)->got_tech = FALSE;
  } phase_players_iterate_end;

  phase_players_iterate(pplayer) {
    do_tech_parasite_effect(pplayer);
    player_restore_units(pplayer);
    update_city_activities(pplayer);
    player_research_get(pplayer)->researching_saved = A_UNKNOWN;
    /* reduce the number of bulbs by the amount needed for tech upkeep and
     * check for finished research */
    update_bulbs(pplayer, -player_research_get(pplayer)->tech_upkeep, TRUE);
    flush_packets();
  } phase_players_iterate_end;

  kill_dying_players();

  /* Unfreeze sending of cities. */
  send_city_suppression(FALSE);

  phase_players_iterate(pplayer) {
    send_player_cities(pplayer);
  } phase_players_iterate_end;
  flush_packets();  /* to curb major city spam */

  do_reveal_effects();
  do_have_embassies_effect();

  phase_players_iterate(pplayer) {
    CALL_PLR_AI_FUNC(phase_finished, pplayer, pplayer);
    /* This has to be after all access to advisor data. */
    /* We used to run this for ai players only, but data phase
       is initialized for human players also. */
    adv_data_phase_done(pplayer);
  } phase_players_iterate_end;
}

/**************************************************************************
  Handle the end of each turn.
**************************************************************************/
static void end_turn(void)
{
  int food = 0, shields = 0, trade = 0, settlers = 0;

  log_debug("Endturn");

  /* Hack: because observer players never get an end-phase packet we send
   * one here. */
  conn_list_iterate(game.est_connections, pconn) {
    if (NULL == pconn->playing) {
      send_packet_end_phase(pconn);
    }
  } conn_list_iterate_end;

  lsend_packet_end_turn(game.est_connections);

  map_calculate_borders();

  /* Output some AI measurement information */
  players_iterate(pplayer) {
    if (!pplayer->ai_controlled || is_barbarian(pplayer)) {
      continue;
    }
    unit_list_iterate(pplayer->units, punit) {
      if (unit_has_type_flag(punit, UTYF_CITIES)) {
        settlers++;
      }
    } unit_list_iterate_end;
    city_list_iterate(pplayer->cities, pcity) {
      shields += pcity->prod[O_SHIELD];
      food += pcity->prod[O_FOOD];
      trade += pcity->prod[O_TRADE];
    } city_list_iterate_end;
    log_debug("%s T%d cities:%d pop:%d food:%d prod:%d "
              "trade:%d settlers:%d units:%d", player_name(pplayer),
              game.info.turn, city_list_size(pplayer->cities),
              total_player_citizens(pplayer), food, shields, trade,
              settlers, unit_list_size(pplayer->units));
  } players_iterate_end;

  log_debug("Season of native unrests");
  summon_barbarians(); /* wild guess really, no idea where to put it, but
                        * I want to give them chance to move their units */

  if (game.server.migration) {
    log_debug("Season of migrations");
    check_city_migrations();
  }

  check_disasters();

  if (game.info.global_warming) {
    update_environmental_upset(S_POLLUTION, &game.info.heating,
                               &game.info.globalwarming,
                               &game.info.warminglevel, global_warming);
  }

  if (game.info.nuclear_winter) {
    update_environmental_upset(S_FALLOUT, &game.info.cooling,
                               &game.info.nuclearwinter,
                               &game.info.coolinglevel, nuclear_winter);
  }

  update_diplomatics();
  make_history_report();
  settings_turn();
  stdinhand_turn();
  voting_turn();
  send_city_turn_notifications(NULL);

  log_debug("Gamenextyear");
  game_advance_year();

  log_debug("Updatetimeout");
  update_timeout();

  log_debug("Sendgameinfo");
  send_game_info(NULL);

  log_debug("Sendplayerinfo");
  send_player_all_c(NULL, NULL);

  log_debug("Sendyeartoclients");
  send_year_to_clients(game.info.year);
}

/**************************************************************************
Unconditionally save the game, with specified filename.
Always prints a message: either save ok, or failed.

Note that if !HAVE_LIBZ, then game.server.save_compress_level should never
become non-zero, so no need to check HAVE_LIBZ explicitly here as well.
**************************************************************************/
void save_game(const char *orig_filename, const char *save_reason,
               bool scenario)
{
  char filepath[600];
  char *dot, *filename;
  struct section_file *file;
  struct timer *timer_cpu, *timer_user;

  if (!orig_filename) {
    filepath[0] = '\0';
    filename = filepath;
  } else {
    sz_strlcpy(filepath, orig_filename);
    if ((filename = strrchr(filepath, '/'))) {
      filename++;
    } else {
      filename = filepath;
    }

    /* Ignores the dot at the start of the filename. */
    for (dot = filename; '.' == *dot; dot++) {
      /* Nothing. */
    }
    if ('\0' == *dot) {
      /* Only dots in this file name, consider it as empty. */
      filename[0] = '\0';
    } else {
      char *end_dot;
      char *strip_extensions[] = { ".sav", ".gz", ".bz2", ".xz", NULL };
      bool stripped = TRUE;

      while ((end_dot = strrchr(dot, '.')) && stripped) {
	int i;
        stripped = FALSE;

	for (i = 0; strip_extensions[i] != NULL && !stripped; i++) {
          if (!strcmp(end_dot, strip_extensions[i])) {
            *end_dot = '\0';
            stripped = TRUE;
          }
        }
      }
    }
  }

  /* If orig_filename is NULL or empty, use a generated default name. */
  if (filename[0] == '\0'){
    /* manual save */
    generate_save_name(game.server.save_name, filename,
                       sizeof(filepath) + filepath - filename, "manual");
  }

  timer_cpu = timer_new(TIMER_CPU, TIMER_ACTIVE);
  timer_start(timer_cpu);
  timer_user = timer_new(TIMER_USER, TIMER_ACTIVE);
  timer_start(timer_user);

  /* Allowing duplicates shouldn't be allowed. However, it takes very too
   * long time for huge game saving... */
  file = secfile_new(TRUE);
  savegame2_save(file, save_reason, scenario);

  /* Append ".sav" to filename. */
  sz_strlcat(filepath, ".sav");

  if (game.server.save_compress_level > 0) {
    switch (game.server.save_compress_type) {
#ifdef HAVE_LIBZ
    case FZ_ZLIB:
      /* Append ".gz" to filename. */
      sz_strlcat(filepath, ".gz");
      break;
#endif
#ifdef HAVE_LIBBZ2
    case FZ_BZIP2:
      /* Append ".bz2" to filename. */
      sz_strlcat(filepath, ".bz2");
      break;
#endif
#ifdef HAVE_LIBLZMA
   case FZ_XZ:
      /* Append ".xz" to filename. */
      sz_strlcat(filepath, ".xz");
      break;
#endif
    case FZ_PLAIN:
      break;
    default:
      log_error(_("Unsupported compression type %d."),
                game.server.save_compress_type);
      notify_conn(NULL, NULL, E_SETTING, ftc_warning,
                  _("Unsupported compression type %d."),
                  game.server.save_compress_type);
      break;
    }
  }

  if (!path_is_absolute(filepath)) {
    char tmpname[600];

    if (!scenario) {
      /* Ensure the saves directory exists. */
      make_dir(srvarg.saves_pathname);

      sz_strlcpy(tmpname, srvarg.saves_pathname);
    } else {
      /* Make sure scenario directory exist */
      make_dir(srvarg.scenarios_pathname);

      sz_strlcpy(tmpname, srvarg.scenarios_pathname);
    }

    if (tmpname[0] != '\0') {
      sz_strlcat(tmpname, "/");
    }
    sz_strlcat(tmpname, filepath);
    sz_strlcpy(filepath, tmpname);
  }

  if (!secfile_save(file, filepath, game.server.save_compress_level,
                    game.server.save_compress_type)) {
    con_write(C_FAIL, _("Failed saving game as %s"), filepath);
  } else {
    con_write(C_OK, _("Game saved as %s"), filepath);
  }

  secfile_destroy(file);

#ifdef LOG_TIMERS
  log_verbose("Save time: %g seconds (%g apparent)",
              timer_read_seconds(timer_cpu), timer_read_seconds(timer_user));
#endif

  timer_destroy(timer_cpu);
  timer_destroy(timer_user);

  ggz_game_saved(filepath);
}

/**************************************************************************
Save game with autosave filename
**************************************************************************/
void save_game_auto(const char *save_reason, enum autosave_type type)
{
  char filename[512];
  const char *reason_filename = NULL;

  if (!(game.server.autosaves & (1 << type))) {
    return;
  }

  switch (type) {
   case AS_TURN:
     reason_filename = NULL;
     break;
   case AS_GAME_OVER:
     reason_filename = "final";
     break;
   case AS_QUITIDLE:
     reason_filename = "quitidle";
     break;
   case AS_INTERRUPT:
     reason_filename = "interrupted";
     break;
  }

  fc_assert(256 > strlen(game.server.save_name));

  generate_save_name(game.server.save_name, filename, sizeof(filename),
                     reason_filename);
  save_game(filename, save_reason, FALSE);
}

/**************************************************************************
  Start actual game. Everything has been set up already.
**************************************************************************/
void start_game(void)
{
  if (S_S_INITIAL != server_state()) {
    con_puts(C_SYNTAX, _("The game is already running."));
    return;
  }

  /* Remove ALLOW_CTRL from whoever has it (gotten from 'first'). */
  conn_list_iterate(game.est_connections, pconn) {
    if (pconn->access_level == ALLOW_CTRL) {
      notify_conn(NULL, NULL, E_SETTING, ftc_server,
                  _("%s lost control cmdlevel on "
                    "game start.  Use voting from now on."),
                  pconn->username);
      conn_set_access(pconn, ALLOW_BASIC, FALSE);
    }
  } conn_list_iterate_end;

  con_puts(C_OK, _("Starting game."));

  /* Prevent problems with commands that only make sense in pregame. */
  clear_all_votes();

  /* This value defines if the player data should be saved for a scenario. It
   * is only FALSE if the editor was used to set it to this value. For
   * such scenarios it has to be resetted at game start so that player data
   * is saved. */
  game.scenario.players = TRUE;

  force_end_of_sniff = TRUE;
  /* There's no stateful packet set to client until srv_ready(). */
}

/**************************************************************************
 Quit the server and exit.
**************************************************************************/
void server_quit(void)
{
  set_server_state(S_S_OVER);
  mapimg_free();
  server_game_free();
  diplhand_free();
  voting_free();
  ai_timer_free();

#ifdef HAVE_FCDB
  if (srvarg.fcdb_enabled) {
    /* If freeciv database has been initialized */
    fcdb_free();
  }
#endif /* HAVE_FCDB */

  settings_free();
  stdinhand_free();
  edithand_free();
  voting_free();
  close_connections_and_socket();
  registry_module_close();
  fc_destroy_mutex(&game.server.mutexes.city_list);
  free_nls();
  con_log_close();
  exit(EXIT_SUCCESS);
}

/**************************************************************************
  Handle request asking report to be sent to client.
**************************************************************************/
void handle_report_req(struct connection *pconn, enum report_type type)
{
  struct conn_list *dest = pconn->self;
  
  if (S_S_RUNNING != server_state() && S_S_OVER != server_state()) {
    log_error("Got a report request %d before game start", type);
    return;
  }

  if (NULL == pconn->playing && !pconn->observer) {
    log_error("Got a report request %d from detached connection", type);
    return;
  }

  switch(type) {
  case REPORT_WONDERS_OF_THE_WORLD:
    report_wonders_of_the_world(dest);
    return;
  case REPORT_TOP_5_CITIES:
    report_top_five_cities(dest);
    return;
  case REPORT_DEMOGRAPHIC:
    report_demographics(pconn);
    return;
  }

  notify_conn(dest, NULL, E_BAD_COMMAND, ftc_server,
              _("request for unknown report (type %d)"), type);
}

/**************************************************************************
  Mark identity number free.
**************************************************************************/
void identity_number_release(int id)
{
  BV_CLR(identity_numbers_used, id);
}

/**************************************************************************
  Marko identity number allocated.
**************************************************************************/
void identity_number_reserve(int id)
{
  BV_SET(identity_numbers_used, id);
}

/**************************************************************************
  Check whether identity number is currently allocated.
**************************************************************************/
static bool identity_number_is_used(int id)
{
  return BV_ISSET(identity_numbers_used, id);
}

/**************************************************************************
  Increment identity_number and return result.
**************************************************************************/
static int increment_identity_number(void)
{
  server.identity_number = server.identity_number+1 % IDENTITY_NUMBER_SIZE;
  return server.identity_number;
}

/**************************************************************************
  Truncation of unsigned short wraps at 65K, skipping IDENTITY_NUMBER_ZERO
  Setup in server_game_init()
**************************************************************************/
int identity_number(void)
{
  int retries = 0;

  while (identity_number_is_used(increment_identity_number())) {
    /* try again */
    if (++retries >= IDENTITY_NUMBER_SIZE) {
      /* Always fails. */
      fc_assert_exit_msg(IDENTITY_NUMBER_SIZE > retries,
                         "Exhausted city and unit numbers!");
    }
  }
  identity_number_reserve(server.identity_number);
  return server.identity_number;
}

/**************************************************************************
  Returns TRUE if the packet type is an edit packet sent by the client.

  NB: The first and last client edit packets here must match those
  defined in common/packets.def.
**************************************************************************/
static bool is_client_edit_packet(int type)
{
  return PACKET_EDIT_MODE <= type && type <= PACKET_EDIT_GAME;
}

/**************************************************************************
Returns 0 if connection should be closed (because the clients was
rejected). Returns 1 else.
**************************************************************************/
bool server_packet_input(struct connection *pconn, void *packet, int type)
{
  struct player *pplayer;

  /* a NULL packet can be returned from receive_packet_goto_route() */
  if (!packet)
    return TRUE;

  /* 
   * Old pre-delta clients (before 2003-11-28) send a
   * PACKET_LOGIN_REQUEST (type 0) to the server. We catch this and
   * reply with an old reject packet. Since there is no struct for
   * this old packet anymore we build it by hand.
   */
  if (type == 0) {
    unsigned char buffer[4096];
    struct data_out dout;

    log_normal(_("Warning: rejecting old client %s"),
               conn_description(pconn));

    dio_output_init(&dout, buffer, sizeof(buffer));
    dio_put_uint16(&dout, 0);

    /* 1 == PACKET_LOGIN_REPLY in the old client */
    dio_put_uint8(&dout, 1);

    dio_put_bool32(&dout, FALSE);
    dio_put_string(&dout, _("Your client is too old. To use this server, "
			    "please upgrade your client to a "
			    "Freeciv 2.2 or later."));
    dio_put_string(&dout, "");

    {
      size_t size = dio_output_used(&dout);
      dio_output_rewind(&dout);
      dio_put_uint16(&dout, size);

      /* 
       * Use send_connection_data instead of send_packet_data to avoid
       * compression.
       */
      connection_send_data(pconn, buffer, size);
    }

    return FALSE;
  }

  if (type == PACKET_SERVER_JOIN_REQ) {
    return handle_login_request(pconn,
				(struct packet_server_join_req *) packet);
  }

  /* May be received on a non-established connection. */
  if (type == PACKET_AUTHENTICATION_REPLY) {
    return auth_handle_reply(pconn,
				((struct packet_authentication_reply *)
				 packet)->password);
  }

  if (type == PACKET_CONN_PONG) {
    handle_conn_pong(pconn);
    return TRUE;
  }

  if (!pconn->established) {
    log_error("Received game packet %s(%d) from unaccepted connection %s.",
              packet_name(type), type, conn_description(pconn));
    return TRUE;
  }
  
  /* valid packets from established connections but non-players */
  if (type == PACKET_CHAT_MSG_REQ
      || type == PACKET_SINGLE_WANT_HACK_REQ
      || type == PACKET_NATION_SELECT_REQ
      || type == PACKET_REPORT_REQ
      || type == PACKET_CLIENT_INFO
      || type == PACKET_CONN_PONG
      || type == PACKET_SAVE_SCENARIO
      || is_client_edit_packet(type)) {

    /* Except for PACKET_EDIT_MODE (used to set edit mode), check
     * that the client is allowed to send the given edit packet. */
    if (is_client_edit_packet(type) && type != PACKET_EDIT_MODE
        && !can_conn_edit(pconn)) {
      notify_conn(pconn->self, NULL, E_BAD_COMMAND, ftc_editor,
                  _("You are not allowed to edit."));
      return TRUE;
    }

    if (!server_handle_packet(type, packet, NULL, pconn)) {
      log_error("Received unknown packet %d from %s.",
                type, conn_description(pconn));
    }
    return TRUE;
  }

  pplayer = pconn->playing;

  if (NULL == pplayer || pconn->observer) {
    /* don't support these yet */
    log_error("Received packet %s(%d) from non-player connection %s.",
              packet_name(type), type, conn_description(pconn));
    return TRUE;
  }

  if (S_S_RUNNING != server_state()
      && type != PACKET_NATION_SELECT_REQ
      && type != PACKET_PLAYER_READY
      && type != PACKET_VOTE_SUBMIT) {
    if (S_S_OVER == server_state()) {
      /* This can happen by accident, so we don't want to print
       * out lots of error messages. Ie, we use log_debug(). */
      log_debug("Got a packet of type %s(%d) in %s.",
                packet_name(type), type, server_states_name(S_S_OVER));
    } else {
      log_error("Got a packet of type %s(%d) outside %s.",
                packet_name(type), type, server_states_name(S_S_RUNNING));
    }
    return TRUE;
  }

  pplayer->nturns_idle = 0;

  if (!pplayer->is_alive && type != PACKET_REPORT_REQ) {
    log_error("Got a packet of type %s(%d) from a dead player.",
              packet_name(type), type);
    return TRUE;
  }
  
  /* Make sure to set this back to NULL before leaving this function: */
  pplayer->current_conn = pconn;

  if (!server_handle_packet(type, packet, pplayer, pconn)) {
    log_error("Received unknown packet %d from %s.",
              type, conn_description(pconn));
  }

  if (S_S_RUNNING == server_state()
      && type != PACKET_PLAYER_READY) {
    /* handle_player_ready() calls start_game(), but the game isn't started
     * until the main loop is re-entered, so kill_dying_players would think
     * all players are dead.  This should be solved by adding a new
     * game state (now S_S_GENERATING_WAITING). */
    kill_dying_players();
  }

  pplayer->current_conn = NULL;
  return TRUE;
}

/**************************************************************************
  Check if turn is really done. Returns nothing, but as a side effect sets
  force_end_of_sniff if no more input is expected this turn (i.e. turn done)
**************************************************************************/
void check_for_full_turn_done(void)
{
  bool connected = FALSE;

  if (S_S_RUNNING != server_state()) {
    /* Not in a running state, no turn done. */
    return;
  }

  /* fixedlength is only applicable if we have a timeout set */
  if (game.server.fixedlength && game.info.timeout != 0) {
    return;
  }

  /* If there are no connected players, don't automatically advance.  This is
   * a hack to prevent all-AI games from running rampant.  Note that if
   * timeout is set to -1 this function call is skipped entirely and the
   * server will run rampant. */
  players_iterate_alive(pplayer) {
    if (pplayer->is_connected && !pplayer->ai_controlled) {
      connected = TRUE;
      break;
    }
  } players_iterate_alive_end;

  if (!connected) {
    return;
  }

  phase_players_iterate(pplayer) {
    if (game.server.turnblock && !pplayer->ai_controlled && pplayer->is_alive
	&& !pplayer->phase_done) {
      /* If turnblock is enabled check for human players, connected
       * or not. */
      return;
    } else if (pplayer->is_connected && pplayer->is_alive
	       && !pplayer->phase_done) {
      /* In all cases, we wait for any connected players. */
      return;
    }
  } phase_players_iterate_end;

  force_end_of_sniff = TRUE;
}

/****************************************************************************
  Initialize the list of available nations.

  Call this on server start, or when loading a scenario.
****************************************************************************/
void init_available_nations(void)
{
  if (!game_was_started() && 0 < map_startpos_count()) {
    nations_iterate(pnation) {
      fc_assert_action_msg(NULL == pnation->player,
        if (pnation->player->nation == pnation) {
          /* At least assignment is consistent. Leave nation assigned,
           * and make sure that nation is also marked available. */
          pnation->is_available = TRUE;
        } else if (NULL != pnation->player->nation) {
          /* Not consistent. Just initialize the pointer and hope for the
           * best. */
          pnation->player->nation->player = NULL;
          pnation->player = NULL;
        } else {
          /* Not consistent. Just initialize the pointer and hope for the
           * best. */
          pnation->player = NULL;
        }, "Player assigned to nation before %s()!", __FUNCTION__);

      if (nation_barbarian_type(pnation) != NOT_A_BARBARIAN) {
        /* Always allow land and sea barbarians. */
        pnation->is_available = TRUE;
      } else {
        pnation->is_available = FALSE;
        map_startpos_iterate(psp) {
          if (startpos_nation_allowed(psp, pnation)) {
            pnation->is_available = TRUE;
            break;
          }
        } map_startpos_iterate_end;
      }
    } nations_iterate_end;
  } else {
    /* No start positions, all nations are available. */
    nations_iterate(pnation) {
      pnation->is_available = TRUE;
    } nations_iterate_end;
  }
}

/**************************************************************************
  Handles a pick-nation packet from the client.  These packets are
  handled by connection because ctrl users may edit anyone's nation in
  pregame, and editing is possible during a running game.
**************************************************************************/
void handle_nation_select_req(struct connection *pc, int player_no,
                              Nation_type_id nation_no, bool is_male,
                              const char *name, int city_style)
{
  struct nation_type *new_nation;
  struct player *pplayer = player_by_number(player_no);

  if (!pplayer || !can_conn_edit_players_nation(pc, pplayer)) {
    return;
  }

  new_nation = nation_by_number(nation_no);

  if (new_nation != NO_NATION_SELECTED) {
    char message[1024];

    /* check sanity of the packet sent by client */
    if (city_style < 0 || city_style >= game.control.styles_count
	|| city_style_has_requirements(&city_styles[city_style])) {
      return;
    }

    if (!new_nation->is_available) {
      notify_player(pplayer, NULL, E_NATION_SELECTED, ftc_server,
                    _("%s nation is not available in this scenario."),
                    nation_adjective_translation(new_nation));
      return;
    }
    if (new_nation->player && new_nation->player != pplayer) {
      notify_player(pplayer, NULL, E_NATION_SELECTED, ftc_server,
                    _("%s nation is already in use."),
                    nation_adjective_translation(new_nation));
      return;
    }

    if (!server_player_set_name_full(pc, pplayer, new_nation, name,
                                     message, sizeof(message))) {
      notify_player(pplayer, NULL, E_NATION_SELECTED,
                    ftc_server, "%s", message);
      return;
    }

    notify_conn(NULL, NULL, E_NATION_SELECTED, ftc_server,
                _("%s is the %s ruler %s."),
                pplayer->username,
                nation_adjective_translation(new_nation),
                player_name(pplayer));

    pplayer->is_male = is_male;
    pplayer->city_style = city_style;
  }

  (void) player_set_nation(pplayer, new_nation);
  send_player_info_c(pplayer, game.est_connections);
}

/****************************************************************************
  Handle a player-ready packet.
****************************************************************************/
void handle_player_ready(struct player *requestor,
			 int player_no,
			 bool is_ready)
{
  struct player *pplayer = player_by_number(player_no);

  if (NULL == pplayer || S_S_INITIAL != server_state()) {
    return;
  }

  if (pplayer != requestor) {
    /* Currently you can only change your own readiness. */
    return;
  }

  pplayer->is_ready = is_ready;
  send_player_info_c(pplayer, NULL);

  /* Note this is called even if the player has pressed /start once
   * before.  For instance, when a player leaves everyone remaining
   * might have pressed /start already but the start won't happen
   * until someone presses it again.  Also you can press start more
   * than once to remind other people to start (which is a good thing
   * until somebody does it too much and it gets labeled as spam). */
  if (is_ready) {
    int num_ready = 0, num_unready = 0;

    players_iterate(pplayer) {
      if (pplayer->is_connected) {
	if (pplayer->is_ready) {
	  num_ready++;
	} else {
	  num_unready++;
	}
      }
    } players_iterate_end;
    if (num_unready > 0) {
      notify_conn(NULL, NULL, E_SETTING, ftc_server,
                  _("Waiting to start game: %d out of %d players "
                    "are ready to start."),
                  num_ready, num_ready + num_unready);
    } else {
      /* Check minplayers etc. and then start */
      start_command(NULL, FALSE, TRUE);
    }
  }
}

/****************************************************************************
  Fill or remove players to meet the given aifill.
****************************************************************************/
void aifill(int amount)
{
  int limit = MIN(amount, game.server.max_players);

  /* Limit to nations provided by ruleset */
  limit = MIN(limit, server.playable_nations);

  if (game_was_started()) {
    return;
  }

  if (limit < player_count()) {
    int remove = player_slot_count() - 1;

    while (limit < player_count() && 0 <= remove) {
      struct player *pplayer = player_by_number(remove);
      remove--;
      if (!pplayer) {
        continue;
      }

      if (!pplayer->is_connected && !pplayer->was_created) {
        server_remove_player(pplayer);
      }
    }

    /* 'limit' can be different from 'player_count()' at this point if
     * there are more human or created players than the 'limit'. */
    return;
  }

  while (limit > player_count()) {
    char leader_name[MAX_LEN_NAME];
    int filled = 1;
    struct player *pplayer;

    pplayer = server_create_player(-1, default_ai_type_name(), NULL);
    if (!pplayer) {
      break;
    }
    server_player_init(pplayer, FALSE, TRUE);

    player_set_nation(pplayer, NULL);

    do {
      fc_snprintf(leader_name, sizeof(leader_name), "AI*%d", filled++);
    } while (player_by_name(leader_name));
    server_player_set_name(pplayer, leader_name);
    sz_strlcpy(pplayer->username, ANON_USER_NAME);

    pplayer->ai_common.skill_level = game.info.skill_level;
    pplayer->ai_controlled = TRUE;
    set_ai_level_directer(pplayer, game.info.skill_level);

    CALL_PLR_AI_FUNC(gained_control, pplayer, pplayer);

    log_normal(_("%s has been added as %s level AI-controlled player (%s)."),
               player_name(pplayer),
               ai_level_name(pplayer->ai_common.skill_level),
               ai_name(pplayer->ai));
    notify_conn(NULL, NULL, E_SETTING, ftc_server,
                _("%s has been added as %s level AI-controlled player (%s)."),
                player_name(pplayer),
                ai_level_name(pplayer->ai_common.skill_level),
                ai_name(pplayer->ai));

    send_player_info_c(pplayer, NULL);
  }
}

/****************************************************************************
  Tool for generate_players().
****************************************************************************/
#define SPECHASH_TAG startpos
#define SPECHASH_KEY_TYPE struct startpos *
#define SPECHASH_DATA_TYPE int
#define SPECHASH_DATA_TO_PTR FC_INT_TO_PTR
#define SPECHASH_PTR_TO_DATA FC_PTR_TO_INT
#include "spechash.h"
#define startpos_hash_iterate(hash, psp, c)                                 \
  TYPED_HASH_ITERATE(struct startpos *, intptr_t, hash, psp, c)
#define startpos_hash_iterate_end HASH_ITERATE_END

/****************************************************************************
  Tool for generate_players().
****************************************************************************/
static void player_set_nation_full(struct player *pplayer,
                                   struct nation_type *pnation)
{
  /* Don't change the name of a created player. */
  player_nation_defaults(pplayer, pnation, !pplayer->was_created);
}

/****************************************************************************
  Set nation for player with nation default values.
****************************************************************************/
void player_nation_defaults(struct player *pplayer, struct nation_type *pnation,
                            bool set_name)
{
  struct nation_leader *pleader;

  fc_assert(NO_NATION_SELECTED != pnation);
  player_set_nation(pplayer, pnation);
  fc_assert(pnation == pplayer->nation);

  pplayer->city_style = city_style_of_nation(nation_of_player(pplayer));

  if (set_name) {
    server_player_set_name(pplayer, pick_random_player_name(pnation));
  }

  if ((pleader = nation_leader_by_name(pnation, player_name(pplayer)))) {
    pplayer->is_male = nation_leader_is_male(pleader);
  } else {
    pplayer->is_male = (fc_rand(2) == 1);
  }
}

/****************************************************************************
  Assign random nations to players at game start. This includes human
  players, AI players created with "set aifill <X>", and players created
  with "create <PlayerName>".

  If a player's name matches one of the leader names for some nation, and
  that nation is available, choose that nation, and set the player sex
  appropriately. For example, when the Britons have not been chosen by
  anyone else, a player called Boudica whose nation has not been specified
  (for instance if they were created with "create Boudica") will become
  the Britons, and their sex will be female. Otherwise, the sex is chosen
  randomly, and the nation is chosen as below.

  If this is a scenario and the scenario has specific start positions for
  some nations, try to pick those nations, favouring those with start
  positions which already-assigned players can't use. Otherwise, pick
  available nations using pick_a_nation(), which tries to pick nations
  that look good with nations already in the game.

  For 'aifill' players, the player name/sex is then reset to that of a
  random leader for the chosen nation.
****************************************************************************/
static void generate_players(void)
{
  int nations_to_assign = 0;

  /* Announce players who already have nations, and select nations based
   * on player names. */
  players_iterate(pplayer) {
    if (pplayer->nation != NO_NATION_SELECTED) {
      announce_player(pplayer);
      continue;
    }

    /* See if the player name matches a known leader name. */
    nations_iterate(pnation) {
      struct nation_leader *pleader;
      const char *name = player_name(pplayer);

      if (is_nation_playable(pnation)
          && pnation->is_available
          && NULL == pnation->player
          && (pleader = nation_leader_by_name(pnation, name))) {
        player_set_nation(pplayer, pnation);
        pplayer->city_style = city_style_of_nation(pnation);
        pplayer->is_male = nation_leader_is_male(pleader);
        break;
      }
    } nations_iterate_end;
    if (pplayer->nation != NO_NATION_SELECTED) {
      announce_player(pplayer);
    } else {
      nations_to_assign++;
    }
  } players_iterate_end;

  /* Calculate the union of the nation sets of assigned nations.
   * Further assignments (here and throughout the game) will be limited
   * to this subset of nations. */
  {
    struct nation_group_list *sets = nation_group_list_new();
    players_iterate(pplayer) {
      int nsets = 0;
      if (!pplayer->nation) {
        continue;
      }
      nation_group_list_iterate(pplayer->nation->groups, pgroup) {
        if (nation_group_is_a_set(pgroup)) {
          if (!nation_group_list_search(sets, pgroup)) {
            nation_group_list_append(sets, pgroup);
          }
          nsets++;
        }
      } nation_group_list_iterate_end;
      if (nsets == 0) {
        /* Nation is in no explicit sets. Treat it as being in a virtual
         * set of all nations. This is signalled as NULL. */
        nation_group_list_destroy(sets);
        sets = NULL;
        /* Since the union can't get any bigger after this, bail out now. */
        break;
      }
    } players_iterate_end;

    /* Were there any assigned nations? If so, the list will either have
     * some members or have been set to NULL to indicate no restrictions. */
    if (sets && nation_group_list_size(sets) == 0) {
      /* No -- fall back to default behaviour.
       * If there are any sets defined, use the first one (we rely on
       * sets being loaded from the ruleset before other groups),
       * otherwise no restrictions. */
      struct nation_group *first = nation_group_by_number(0);
      if (first && nation_group_is_a_set(first)) {
        nation_group_list_append(sets, first);
      } else {
        nation_group_list_destroy(sets);
        sets = NULL;
      }
    }
    /* Configure pick_a_nation(). */
    set_allowed_nation_groups(sets);
  }

  if (0 < nations_to_assign && 0 < map_startpos_count()) {
    /* We're running a scenario game with specified start positions.
     * Prefer nations assigned to those positions. */
    struct startpos_hash *hash = startpos_hash_new();
    struct nation_type *picked;
    int c, max = -1;
    int i, min;

    /* Initialization. */
    map_startpos_iterate(psp) {
      if (startpos_allows_all(psp)) {
        continue;
      }

      /* Count the already-assigned players whose nations can use this
       * start position. */
      c = 0;
      players_iterate(pplayer) {
        if (NO_NATION_SELECTED != pplayer->nation
            && startpos_nation_allowed(psp, pplayer->nation)) {
          c++;
        }
      } players_iterate_end;

      startpos_hash_insert(hash, psp, c);
      if (c > max) {
        max = c;
      }
    } map_startpos_iterate_end;

    /* Try to assign nations with start positions to the unassigned
     * players, preferring nations whose start positions aren't usable
     * by already-assigned players. */
    players_iterate(pplayer) {
      if (NO_NATION_SELECTED != pplayer->nation) {
        continue;
      }

      picked = NO_NATION_SELECTED;
      min = max;
      i = 0;

      nations_iterate(pnation) {
        if (!is_nation_playable(pnation)
            || !pnation->is_available
            || NULL != pnation->player) {
          /* Not available. */
          continue;
        }

        startpos_hash_iterate(hash, psp, c) {
          if (!startpos_nation_allowed(psp, pnation)) {
            continue;
          }

          if (c < min) {
            /* Pick this nation, as fewer nations already in the game
             * can use this start position. */
            picked = pnation;
            min = c;
            i = 1;
          } else if (c == min && 0 == fc_rand(++i)) {
            /* More than one nation is equally desirable. Pick one at
             * random. */
            picked = pnation;
          }
        } startpos_hash_iterate_end;
      } nations_iterate_end;

      if (NO_NATION_SELECTED != picked) {
        player_set_nation_full(pplayer, picked);
        nations_to_assign--;
        announce_player(pplayer);
        /* Update the counts for the newly assigned nation. */
        startpos_hash_iterate(hash, psp, c) {
          if (startpos_nation_allowed(psp, picked)) {
            startpos_hash_replace(hash, psp, c + 1);
          }
        } startpos_hash_iterate_end;
      } else {
        /* No need to continue; we failed to pick a nation this time,
         * so we're not going to succeed next time. Fall back to
         * standard nation selection. */
        break;
      }
    } players_iterate_end;

    startpos_hash_destroy(hash);
  }

  if (0 < nations_to_assign) {
    players_iterate(pplayer) {
      if (NO_NATION_SELECTED == pplayer->nation) {
        /* Pick random race. */
        player_set_nation_full(pplayer, pick_a_nation(NULL, FALSE, TRUE,
                                                      NOT_A_BARBARIAN));
        nations_to_assign--;
        announce_player(pplayer);
      }
    } players_iterate_end;
  }

  fc_assert(0 == nations_to_assign);

  (void) send_server_info_to_metaserver(META_INFO);
}

/****************************************************************************
  Returns a random ruler name picked from given nation ruler names, given
  that nation's number. If it fails it iterates through "Player 1",
  "Player 2", ... until an unused name is found.
****************************************************************************/
const char *pick_random_player_name(const struct nation_type *pnation)
{
  const char *choice = NULL;
  int i = 0;

  nation_leader_list_iterate(nation_leaders(pnation), pleader) {
    const char *name = nation_leader_name(pleader);

    if (NULL == player_by_name(name)
        && NULL == player_by_user(name)
        && 0 == fc_rand(++i)) {
      choice = name;
    }
  } nation_leader_list_iterate_end;

  return choice;
}

/*************************************************************************
  Announce what nation player rules to everyone.
*************************************************************************/
static void announce_player(struct player *pplayer)
{
   log_normal(_("%s rules the %s."),
              player_name(pplayer), nation_plural_for_player(pplayer));

  notify_conn(game.est_connections, NULL, E_GAME_START,
              ftc_server, _("%s rules the %s."),
              player_name(pplayer), nation_plural_for_player(pplayer));
}

/**************************************************************************
  Play the game! Returns when S_S_RUNNING != server_state().
**************************************************************************/
static void srv_running(void)
{
  struct timer *eot_timer;	/* time server processing at end-of-turn */
  int save_counter = 0, i;
  bool is_new_turn = game.info.is_new_game;
  bool need_send_pending_events = !game.info.is_new_game;

  /* We may as well reset is_new_game now. */
  game.info.is_new_game = FALSE;

  log_verbose("srv_running() mostly redundant send_server_settings()");
  send_server_settings(NULL);

  eot_timer = timer_new(TIMER_CPU, TIMER_ACTIVE);
  timer_start(eot_timer);

  /* 
   * This will freeze the reports and agents at the client.
   * 
   * Do this before the body so that the PACKET_THAW_CLIENT packet is
   * balanced. 
   */
  lsend_packet_freeze_client(game.est_connections);

  fc_assert(S_S_RUNNING == server_state());
  while (S_S_RUNNING == server_state()) {
    /* The beginning of a turn.
     *
     * We have to initialize data as well as do some actions.  However when
     * loading a game we don't want to do these actions (like AI unit
     * movement and AI diplomacy). */
    begin_turn(is_new_turn);

    if (game.server.num_phases != 1) {
      /* We allow everyone to begin adjusting cities and such
       * from the beginning of the turn.
       * With simultaneous movement we send begin_turn packet in
       * begin_phase() only after AI players have finished their actions. */
      lsend_packet_begin_turn(game.est_connections);
    }

    for (; game.info.phase < game.server.num_phases; game.info.phase++) {
      log_debug("Starting phase %d/%d.", game.info.phase,
                game.server.num_phases);
      begin_phase(is_new_turn);
      if (need_send_pending_events) {
        /* When loading a savegame, we need to send loaded events, after
         * the clients switched to the game page (after the first
         * packet_start_phase is received). */
        conn_list_iterate(game.est_connections, pconn) {
          send_pending_events(pconn, TRUE);
        } conn_list_iterate_end;
        need_send_pending_events = FALSE;
      }
      is_new_turn = TRUE;

      force_end_of_sniff = FALSE;

      /* 
       * This will thaw the reports and agents at the client.
       */
      lsend_packet_thaw_client(game.est_connections);

#ifdef LOG_TIMERS
      /* Before sniff (human player activites), report time to now: */
      log_verbose("End/start-turn server/ai activities: %g seconds",
                  timer_read_seconds(eot_timer));
#endif

      /* Do auto-saves just before starting server_sniff_all_input(), so that
       * autosave happens effectively "at the same time" as manual
       * saves, from the point of view of restarting and AI players.
       * Post-increment so we don't count the first loop. */
      if (game.info.phase == 0) {
        /* Create autosaves if requested. */
        if (save_counter >= game.server.save_nturns
            && game.server.save_nturns > 0) {
	  save_counter = 0;
	  save_game_auto("Autosave", AS_TURN);
	}
	save_counter++;

        /* Save map image(s). */
        for (i = 0; i < mapimg_count(); i++) {
          struct mapdef *pmapdef = mapimg_isvalid(i);
          if (pmapdef != NULL) {
            mapimg_create(pmapdef, FALSE, game.server.save_name,
                          srvarg.saves_pathname);
          } else {
            log_error("%s", mapimg_error());
          }
        }
      }

      log_debug("sniffingpackets");
      check_for_full_turn_done(); /* HACK: don't wait during AI phases */
      while (server_sniff_all_input() == S_E_OTHERWISE) {
        /* nothing */
      }

      /* After sniff, re-zero the timer: (read-out above on next loop) */
      timer_clear(eot_timer);
      timer_start(eot_timer);

      conn_list_do_buffer(game.est_connections);

      sanity_check();

      /* 
       * This will freeze the reports and agents at the client.
       */
      lsend_packet_freeze_client(game.est_connections);

      end_phase();

      conn_list_do_unbuffer(game.est_connections);

      if (S_S_OVER == server_state()) {
	break;
      }
    }
    end_turn();
    log_debug("Sendinfotometaserver");
    (void) send_server_info_to_metaserver(META_REFRESH);

    if (S_S_OVER != server_state() && check_for_game_over()) {
      set_server_state(S_S_OVER);
      if (game.info.turn > game.server.end_turn) {
	/* endturn was reached - rank users based on team scores */
	rank_users(TRUE);
      } else { 
	/* game ended for victory conditions - rank users based on survival */
	rank_users(FALSE);
      }
    } else if ((check_for_game_over() && game.info.turn > game.server.end_turn)
	       || S_S_OVER == server_state()) {
      /* game terminated by /endgame command - calculate team scores */
      rank_users(TRUE);
    }
  }

  /* This will thaw the reports and agents at the client.  */
  lsend_packet_thaw_client(game.est_connections);

  timer_destroy(eot_timer);
}

/**************************************************************************
  Server initialization.
**************************************************************************/
static void srv_prepare(void)
{
#ifdef HAVE_FCDB
  if (!srvarg.auth_enabled) {
    con_write(C_COMMENT, _("This freeciv-server program has player "
                           "authentication support, but it's currently not "
                           "in use."));
  }
#endif /* HAVE_FCDB */

  /* make sure it's initialized */
  if (!has_been_srv_init) {
    srv_init();
  }

  fc_init_network();

  /* must be before con_log_init() */
  init_connections();
  con_log_init(srvarg.log_filename, srvarg.loglevel,
               srvarg.fatal_assertions);
  /* logging available after this point */

  if (!with_ggz) {
    server_open_socket();
  }

#if IS_BETA_VERSION
  con_puts(C_COMMENT, "");
  con_puts(C_COMMENT, beta_message());
  con_puts(C_COMMENT, "");
#endif
  
  con_flush();

  settings_init();
  stdinhand_init();
  edithand_init();
  voting_init();
  diplhand_init();
  voting_init();
  ai_timer_init();

  server_game_init();
  mapimg_init(mapimg_server_tile_known, mapimg_server_tile_terrain,
              mapimg_server_tile_owner, mapimg_server_tile_city,
              mapimg_server_tile_unit, mapimg_server_plrcolor_count,
              mapimg_server_plrcolor_get);

#ifdef HAVE_FCDB
  if (srvarg.fcdb_enabled) {
    bool success;

    success = fcdb_init(srvarg.fcdb_conf);
    free(srvarg.fcdb_conf); /* Never needed again */
    srvarg.fcdb_conf = NULL;
    if (!success) {
      exit(EXIT_FAILURE);
    }
  }
#endif /* HAVE_FCDB */

  /* load a saved game */
  if ('\0' == srvarg.load_filename[0]
   || !load_command(NULL, srvarg.load_filename, FALSE)) {
    /* Rulesets are loaded on game initialization, but may be changed later
     * if /load or /rulesetdir is done. */
    load_rulesets(NULL);
  }

  maybe_automatic_meta_message(default_meta_message_string());

  if(!(srvarg.metaserver_no_send)) {
    log_normal(_("Sending info to metaserver <%s>."), meta_addr_port());
    /* Open socket for meta server */
    if (!server_open_meta()
        || !send_server_info_to_metaserver(META_INFO)) {
      con_write(C_FAIL, _("Not starting without explicitly requested metaserver connection."));
      exit(EXIT_FAILURE);
    }
  }
}

/**************************************************************************
  Score calculation.
**************************************************************************/
static void srv_scores(void)
{
  /* Recalculate the scores in case of a spaceship victory */
  players_iterate(pplayer) {
    calc_civ_score(pplayer);
  } players_iterate_end;

  log_civ_score_now();

  report_final_scores(NULL);
  show_map_to_all();
  notify_player(NULL, NULL, E_GAME_END, ftc_server,
                _("The game is over..."));
  send_server_info_to_metaserver(META_INFO);

  if (game.server.save_nturns > 0
      && conn_list_size(game.est_connections) > 0) {
    /* Save game on game_over, but not when the gameover was caused by
     * the -q parameter. */
    save_game_auto("Game over", AS_GAME_OVER);
  }
}

/**************************************************************************
  Apply some final adjustments from the ruleset on to the game state.
  We cannot do this during ruleset loading, since some players may be
  added later than that.
**************************************************************************/
static void final_ruleset_adjustments(void)
{
  players_iterate(pplayer) {
    struct nation_type *pnation = nation_of_player(pplayer);

    pplayer->government = pnation->init_government;

    if (pnation->init_government == game.government_during_revolution) {
      /* If we do not do this, an assertion will trigger. This enables us to
       * select a valid government on game start. */
      pplayer->revolution_finishes = 0;
    }
  } players_iterate_end;
}

/**************************************************************************
  Set up one game.
**************************************************************************/
static void srv_ready(void)
{
  (void) send_server_info_to_metaserver(META_INFO);

  if (game.server.auto_ai_toggle) {
    players_iterate(pplayer) {
      if (!pplayer->is_connected && !pplayer->ai_controlled) {
	toggle_ai_player_direct(NULL, pplayer);
      }
    } players_iterate_end;
  }

  init_game_seed();

#ifdef TEST_RANDOM /* not defined anywhere, set it if you want it */
  test_random1(200);
  test_random1(2000);
  test_random1(20000);
  test_random1(200000);
#endif

  if (game.info.is_new_game) {
    game.info.year = game.server.start_year;
    /* Must come before assign_player_colors() */
    generate_players();
    final_ruleset_adjustments();
  }

  /* If we have a tile map, and MAPGEN_SCENARIO == map.server.generator,
   * call map_fractal_generate anyway to make the specials, huts and
   * continent numbers. */
  if (map_is_empty()
      || (MAPGEN_SCENARIO == map.server.generator
          && game.info.is_new_game)) {
    int i;
    bool retry_ok = (map.server.seed == 0 && map.server.generator != MAPGEN_SCENARIO);
    int max = retry_ok ? 2 : 1;
    bool created = FALSE;
    struct unit_type *utype = NULL;
    int sucount = strlen(game.server.start_units);

    for (i = 0; utype == NULL && i < sucount; i++) {
      utype = crole_to_unit_type(game.server.start_units[i], NULL);
    }

    fc_assert(utype != NULL);

    for (i = 0; !created && i < max ; i++) {
      created = map_fractal_generate(TRUE, utype);
      if (!created && retry_ok) {
        if (i == 0 && max > 1) {
          /* We will retry only if max attempts allow it */
          log_error(_("Failed to create suitable map, retrying with another mapseed"));
        }
        /* Reset mapseed so generator knows to use new one */
        map.server.seed = 0;
        /* One should never set this to false in scenario map that had resources
         * placed. We are safe side here as map generation is retried only if this is
         * not scenario map at all. */
        map.server.have_resources = FALSE;

        /* Remove old information already present in tiles */
        map_free();
        map_allocate(); /* NOT map_init() as that would overwrite settings */
      }
    }
    if (!created) {
      log_error(_("Cannot create suitable map with given settings."));
       /* TRANS: No full stop after the URL, could cause confusion. */
      log_error(_("Please report this message at %s"), BUG_URL);
      exit(EXIT_FAILURE);
    }

    if (map.server.generator != MAPGEN_SCENARIO) {
      script_server_signal_emit("map_generated", 0);
    }
    game_map_init();
  }

  /* start the game */
  set_server_state(S_S_RUNNING);
  (void) send_server_info_to_metaserver(META_INFO);

  if (game.info.is_new_game) {
    /* If we're starting a new game, reset the max_players to be at
     * least the number of players currently in the game. */
    game.server.max_players = MAX(player_count(), game.server.max_players);

    /* Before the player map is allocated (and initialized)! */
    game.server.fogofwar_old = game.info.fogofwar;

    players_iterate(pplayer) {
      player_map_init(pplayer);
      init_tech(pplayer, TRUE);
      pplayer->economic = player_limit_to_max_rates(pplayer);
      pplayer->economic.gold = game.info.gold;
    } players_iterate_end;

    /* Give nation technologies, as specified in the ruleset. */
    players_iterate(pplayer) {
      give_nation_initial_techs(pplayer);
    } players_iterate_end;

    players_iterate(pplayer) {
      int i;
      bool free_techs_already_given = FALSE;
      
      players_iterate(eplayer) {
        if (players_on_same_team(eplayer, pplayer) &&
	    player_number(eplayer) < player_number(pplayer)) {
          free_techs_already_given = TRUE;
	  break;
        }
      } players_iterate_end;
      
      if (free_techs_already_given) {
        continue;
      }
      
      /* Give global technologies, as specified in the ruleset. */
      give_global_initial_techs(pplayer);
      /* Give random free technologies thanks to the techlevel setting. */
      for (i = 0; i < game.info.tech; i++) {
        give_random_initial_tech(pplayer);
      }
    } players_iterate_end;

    /* Set up alliances based on team selections */
    players_iterate(pplayer) {
      players_iterate(pdest) {
        if (players_on_same_team(pplayer, pdest)
            && player_number(pplayer) != player_number(pdest)) {
          player_diplstate_get(pplayer, pdest)->type = DS_TEAM;
          give_shared_vision(pplayer, pdest);
          BV_SET(pplayer->real_embassy, player_index(pdest));
        }
      } players_iterate_end;
    } players_iterate_end;

    /* Assign colors from the ruleset for any players who weren't
     * explicitly assigned colors during the pregame.
     * This must come after generate_players() since it can depend on
     * assigned nations. */
    assign_player_colors();

    /* Save all settings for the 'reset game' command. */
    settings_game_start();
  }

  /* FIXME: can this be moved? */
  players_iterate(pplayer) {
    adv_data_analyze_rulesets(pplayer);
  } players_iterate_end;

  if (!game.info.is_new_game) {
    players_iterate(pplayer) {
      if (pplayer->ai_controlled) {
	set_ai_level_direct(pplayer, pplayer->ai_common.skill_level);
      }
    } players_iterate_end;
  } else {
    players_iterate(pplayer) {
      /* Initialize this again to be sure */
      adv_data_default(pplayer);
    } players_iterate_end;
  }

  conn_list_compression_freeze(game.est_connections);
  send_all_info(game.est_connections);
  conn_list_compression_thaw(game.est_connections);

  if (game.info.is_new_game) {
    init_new_game();
  }

  if (game.server.revealmap & REVEAL_MAP_START) {
    players_iterate(pplayer) {
      map_show_all(pplayer);
    } players_iterate_end;
  }
}

/**************************************************************************
  Initialize game data for the server (corresponds to server_game_free).
**************************************************************************/
void server_game_init(void)
{
  /* was redundantly in game_load() */
  server.playable_nations = 0;
  server.nbarbarians = 0;
  server.identity_number = IDENTITY_NUMBER_SKIP;

  BV_CLR_ALL(identity_numbers_used);
  identity_number_reserve(IDENTITY_NUMBER_ZERO);

  event_cache_init();
  playercolor_init();
  game_init();
}

/**************************************************************************
  Free game data that we reinitialize as part of a server soft restart.
  Bear in mind that this function is called when the 'load' command is
  used, for instance.
**************************************************************************/
void server_game_free(void)
{
  CALL_FUNC_EACH_AI(game_free);

  /* Free all the treaties that were left open when game finished. */
  free_treaties();

  /* Free the vision data, without sending updates. */
  players_iterate(pplayer) {
    unit_list_iterate(pplayer->units, punit) {
      /* don't bother using vision_clear_sight() */
      vision_layer_iterate(v) {
        punit->server.vision->radius_sq[v] = -1;
      } vision_layer_iterate_end;
      vision_free(punit->server.vision);
      punit->server.vision = NULL;
    } unit_list_iterate_end;

    city_list_iterate(pplayer->cities, pcity) {
      /* don't bother using vision_clear_sight() */
      vision_layer_iterate(v) {
        pcity->server.vision->radius_sq[v] = -1;
      } vision_layer_iterate_end;
      vision_free(pcity->server.vision);
      pcity->server.vision = NULL;
      adv_city_free(pcity);
    } city_list_iterate_end;
  } players_iterate_end;

  /* Destroy all players; with must be separate as the player information is
   * needed above. This also sends the information to the clients. */
  players_iterate(pplayer) {
    server_remove_player(pplayer);
  } players_iterate_end;

  event_cache_free();
  log_civ_score_free();
  playercolor_free();
  citymap_free();
  game_free();
}

/**************************************************************************
  Server main loop.
**************************************************************************/
void srv_main(void)
{
  fc_interface_init_server();

  srv_prepare();

  /* Run server loop */
  do {
    set_server_state(S_S_INITIAL);

    /* Load a script file. */
    if (NULL != srvarg.script_filename) {
      /* Adding an error message more here will duplicate them. */
      (void) read_init_script(NULL, srvarg.script_filename, TRUE, FALSE);
    }

    aifill(game.info.aifill);
    if (!game_was_started()) {
      event_cache_clear();
    }

    log_normal(_("Now accepting new client connections."));
    /* Remain in S_S_INITIAL until all players are ready. */
    while (S_E_FORCE_END_OF_SNIFF != server_sniff_all_input()) {
      /* When force_end_of_sniff is used in pregame, it means that the server
       * is ready to start (usually set within start_game()). */
    }

    if (S_S_RUNNING > server_state()) {
      /* If restarting for lack of players, the state is S_S_OVER,
       * so don't try to start the game. */
      srv_ready(); /* srv_ready() sets server state to S_S_RUNNING. */
      srv_running();
      srv_scores();
    }

    /* Remain in S_S_OVER until players log out */
    while (conn_list_size(game.est_connections) > 0) {
      server_sniff_all_input();
    }

    if (game.info.timeout == -1 || srvarg.exit_on_end) {
      /* For autogames or if the -e option is specified, exit the server. */
      server_quit();
    }

    /* Reset server */
    server_game_free();
    server_game_init();
    mapimg_reset();
    load_rulesets(NULL);
    game.info.is_new_game = TRUE;
  } while (TRUE);

  /* Technically, we won't ever get here. We exit via server_quit. */
}

/***************************************************************
  Initialize client specific functions.
***************************************************************/
struct color;
static inline void server_gui_color_free(struct color *pcolor)
{
  fc_assert_ret(pcolor == NULL);

  return;
}

/***************************************************************
  Initialize client specific functions.
***************************************************************/
static void fc_interface_init_server(void)
{
  struct functions *funcs = fc_interface_funcs();

  funcs->destroy_base = destroy_base;
  funcs->player_tile_vision_get = map_is_known_and_seen;
  funcs->gui_color_free = server_gui_color_free;

  /* Keep this function call at the end. It checks if all required functions
     are defined. */
  fc_interface_init();
}

/***************************************************************************
  Helper function for the mapimg module - tile knowledge.
****************************************************************************/
static enum known_type mapimg_server_tile_known(const struct tile *ptile,
                                                const struct player *pplayer,
                                                bool knowledge)
{
  if (knowledge && pplayer) {
    return tile_get_known(ptile, pplayer);
  }

  return TILE_KNOWN_SEEN;
}

/****************************************************************************
  Helper function for the mapimg module - tile terrain.
****************************************************************************/
static struct terrain
  *mapimg_server_tile_terrain(const struct tile *ptile,
                              const struct player *pplayer, bool knowledge)
{
  if (knowledge && pplayer) {
    struct player_tile *plrtile = map_get_player_tile(ptile, pplayer);
    return plrtile->terrain;
  }

  return tile_terrain(ptile);
}

/****************************************************************************
  Helper function for the mapimg module - tile owner.
****************************************************************************/
static struct player *mapimg_server_tile_owner(const struct tile *ptile,
                                               const struct player *pplayer,
                                               bool knowledge)
{
  if (knowledge && pplayer
      && tile_get_known(ptile, pplayer) != TILE_KNOWN_SEEN) {
    struct player_tile *plrtile = map_get_player_tile(ptile, pplayer);
    return plrtile->owner;
  }

  return tile_owner(ptile);
}

/****************************************************************************
  Helper function for the mapimg module - city owner.
****************************************************************************/
static struct player *mapimg_server_tile_city(const struct tile *ptile,
                                              const struct player *pplayer,
                                              bool knowledge)
{
  struct city *pcity = tile_city(ptile);

  if (!pcity) {
    return NULL;
  }

  if (knowledge && pplayer) {
    struct vision_site *pdcity = map_get_player_city(ptile, pplayer);

    if (pdcity) {
      return pdcity->owner;
    } else {
      return NULL;
    }
  }

  return city_owner(tile_city(ptile));
}

/****************************************************************************
  Helper function for the mapimg module - unit owner.
****************************************************************************/
static struct player *mapimg_server_tile_unit(const struct tile *ptile,
                                              const struct player *pplayer,
                                              bool knowledge)
{
  int unit_count = unit_list_size(ptile->units);

  if (unit_count == 0) {
    return NULL;
  }

  if (knowledge && pplayer
      && tile_get_known(ptile, pplayer) != TILE_KNOWN_SEEN) {
    return NULL;
  }

  return unit_owner(unit_list_get(ptile->units, 0));
}

/****************************************************************************
  Helper function for the mapimg module - number of player colors.
****************************************************************************/
static int mapimg_server_plrcolor_count(void)
{
  return playercolor_count();
}

/****************************************************************************
  Helper function for the mapimg module - one player color.
****************************************************************************/
static struct rgbcolor *mapimg_server_plrcolor_get(int i)
{
  return playercolor_get(i);
}
