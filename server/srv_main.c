/***********************************************************************
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

#include "fc_prehdrs.h"

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
#ifdef FREECIV_HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

/* utility */
#include "astring.h"
#include "bitvector.h"
#include "bugs.h"
#include "capability.h"
#include "fc_cmdline.h"
#include "fciconv.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "netintf.h"
#include "rand.h"
#include "registry.h"
#include "support.h"
#include "timing.h"

/* common/aicore */
#include "citymap.h"

/* common */
#include "achievements.h"
#include "calendar.h"
#include "capstr.h"
#include "city.h"
#include "culture.h"
#include "dataio.h"
#include "effects.h"
#include "events.h"
#include "fc_interface.h"
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
#include "victory.h"

/* server */
#include "aiiface.h"
#include "animals.h"
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
#include "handchat.h"
#include "maphand.h"
#include "meta.h"
#include "notify.h"
#include "plrhand.h"
#include "report.h"
#include "ruleset.h"
#include "sanitycheck.h"
#include "score.h"
#include "sernet.h"
#include "settings.h"
#include "spacerace.h"
#include "srv_log.h"
#include "stdinhand.h"
#include "techtools.h"
#include "unithand.h"
#include "unittools.h"
#include "voting.h"

/* server/advisors */
#include "advdata.h"
#include "autosettlers.h"
#include "advbuilding.h"
#include "advspace.h"
#include "infracache.h"

/* server/savegame */
#include "savemain.h"

/* server/scripting */
#include "script_server.h"
#include "luascript_types.h"

/* server/generator */
#include "mapgen.h"
#include "mapgen_utils.h"

/* ai */
#include "aitraits.h"
#include "difficulty.h"

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

static void handle_observer_ready(struct connection *pconn);

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

#define IDENTITY_NUMBER_SIZE 250000
BV_DEFINE(bv_identity_numbers, IDENTITY_NUMBER_SIZE);
bv_identity_numbers identity_numbers_used;

/* server initialized flag */
static bool has_been_srv_init = FALSE;

/* time server processing at end-of-turn */
static struct timer *eot_timer = NULL;

static struct timer *between_turns = NULL;

/**********************************************************************//**
  Initialize the game seed.  This may safely be called multiple times.
**************************************************************************/
void init_game_seed(void)
{
  if (game.server.seed_setting == 0) {
    /* We strip the high bit for now because neither game file nor
       server options can handle unsigned ints yet. - Cedric */
    game.server.seed = time(NULL) & (MAX_UINT32 >> 1);
#ifdef FREECIV_TESTMATIC
     /* Log command to reproduce the gameseed */
    log_testmatic("set gameseed %d", game.server.seed);
#else  /* FREECIV_TESTMATIC */
    log_debug("Setting game.seed:%d", game.server.seed);
#endif /* FREECIV_TESTMATIC */
  } else {
    game.server.seed = game.server.seed_setting;
  }

  if (!fc_rand_is_init()) {
    fc_srand(game.server.seed);
  }
}

/**********************************************************************//**
  Initialize freeciv server.
**************************************************************************/
void srv_init(void)
{
  i_am_server(); /* Tell to libfreeciv that we are server */

  /* NLS init */
  init_nls();
#ifdef ENABLE_NLS
  (void) bindtextdomain("freeciv-nations", get_locale_dir());
#endif

  /* This is before ai module initializations so that if ai module
   * wants to use registry files, it can. */
  registry_module_init();

  /* We want this before any AI stuff */
  timing_log_init();

  /* This must be before command line argument parsing.
     This allocates default ai, and we want that to take place before
     loading additional ai modules from command line. */
  ai_init();

  /* init server arguments... */

  srvarg.metaserver_no_send = DEFAULT_META_SERVER_NO_SEND;
  sz_strlcpy(srvarg.metaserver_addr, DEFAULT_META_SERVER_ADDR);
  srvarg.metaconnection_persistent = FALSE;
  srvarg.identity_name[0] = '\0';
  srvarg.serverid[0] = '\0';

  srvarg.bind_addr = NULL;
#ifdef FREECIV_JSON_CONNECTION
  srvarg.port = FREECIV_JSON_PORT;
#else  /* FREECIV_JSON_CONNECTION */
  srvarg.port = DEFAULT_SOCK_PORT;
#endif /* FREECIV_JSON_CONNECTION */

  srvarg.bind_meta_addr = NULL;

  srvarg.loglevel = LOG_NORMAL;

  srvarg.log_filename = NULL;
  srvarg.fatal_assertions = -1;
  srvarg.ranklog_filename = NULL;
  srvarg.load_filename[0] = '\0';
  srvarg.script_filename = NULL;
  srvarg.saves_pathname = "";
  srvarg.scenarios_pathname = "";
  srvarg.ruleset = NULL;

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
#ifdef ENABLE_NLS
  bind_textdomain_codeset("freeciv-nations", get_internal_encoding());
#endif

  /* Initialize callbacks. */
  game.callbacks.unit_deallocate = identity_number_release;

  /* Initialize global mutexes */
  fc_init_mutex(&game.server.mutexes.city_list);

  /* done */
  return;
}

/**********************************************************************//**
  Handle client info packet
**************************************************************************/
void handle_client_info(struct connection *pc, enum gui_type gui,
                        const char *distribution)
{
  pc->client_gui = gui;
  log_debug("%s's client has %s gui.", pc->username, gui_type_name(gui));
  if (strcmp(distribution, "")) {
    log_debug("It comes from %s distribution.", distribution);
  }
}

/**********************************************************************//**
  Return current server state.
**************************************************************************/
enum server_states server_state(void)
{
  return civserver_state;
}

/**********************************************************************//**
  Set current server state.
**************************************************************************/
void set_server_state(enum server_states newstate)
{
  civserver_state = newstate;
}

/**********************************************************************//**
  Returns iff the game was started once upon a time.
**************************************************************************/
bool game_was_started(void)
{
  return (!game.info.is_new_game || S_S_INITIAL != server_state());
}

/**********************************************************************//**
  Returns TRUE if any one game end condition is fulfilled, FALSE otherwise.

  This function will notify players but will not set the server_state(). The
  caller must set the server state to S_S_OVER if the function
  returns TRUE.

  Also send notifications about impending, predictable game end conditions.
**************************************************************************/
bool check_for_game_over(void)
{
  int candidates, defeated;
  struct player *victor;
  int winners = 0;
  struct astring str = ASTRING_INIT;

  /* Check for scenario victory; dead players can win if they are on a team
   * with the winners. */
  players_iterate(pplayer) {
    if (player_status_check(pplayer, PSTATUS_WINNER)
        || get_player_bonus(pplayer, EFT_VICTORY) > 0) {
      if (winners) {
        /* TRANS: Another entry in winners list (", the Tibetans") */
        astr_add(&str, Q_("?winners:, the %s"),
                 nation_plural_for_player(pplayer));
      } else {
        /* TRANS: Beginning of the winners list ("the French") */
        astr_add(&str, Q_("?winners:the %s"),
                 nation_plural_for_player(pplayer));
      }
      pplayer->is_winner = TRUE;
      winners++;
    }
  } players_iterate_end;
  if (winners) {
    notify_conn(game.est_connections, NULL, E_GAME_END, ftc_server,
                /* TRANS: There can be several winners listed */
                _("Scenario victory to %s."), astr_str(&str));
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
            pplayer->is_winner = TRUE;
          } player_list_iterate_end;
          return TRUE;
        }
      } teams_iterate_end;
    }

    /* Check for allied victory. */
    if (1 < candidates && victory_enabled(VC_ALLIED)) {
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
          pplayer->is_winner = TRUE;
        } player_list_iterate_end;
        notify_conn(game.est_connections, NULL, E_GAME_END, ftc_server,
                    /* TRANS: There can be several winners listed */
                    _("Allied victory to %s."), astr_str(&str));
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
            && (!victory_enabled(VC_ALLIED)
                || !pplayers_allied(victor, pplayer))) {
          found = TRUE;
          break;
        }
      } players_iterate_end;

      if (found) {
        notify_conn(game.est_connections, NULL, E_GAME_END, ftc_server,
                    _("Game ended in conquest victory for %s."), player_name(victor));
        victor->is_winner = TRUE;
        return TRUE;
      }
    }
  }

  /* Check for culture victory */
  if (victory_enabled(VC_CULTURE)) {
    struct player *best = NULL;
    int best_value = -1;
    int second_value = -1;

    players_iterate(pplayer) {
      if (is_barbarian(pplayer) || !pplayer->is_alive) {
        continue;
      }

      if (pplayer->score.culture > best_value) {
        best = pplayer;
        second_value = best_value;
        best_value = pplayer->score.culture;
      } else if (pplayer->score.culture > second_value) {
        second_value = pplayer->score.culture;
      }
    } players_iterate_end;

    if (best != NULL && best_value >= game.info.culture_vic_points
        && best_value > second_value * (100 + game.info.culture_vic_lead) / 100) {
      notify_conn(game.est_connections, NULL, E_GAME_END, ftc_server,
                  _("Game ended in cultural domination victory for %s."),
                  player_name(best));
      best->is_winner = TRUE;

      return TRUE;
    }
  }

  /* Quit if we are past the turn limit. */
  if (game.info.turn > game.server.end_turn) {
    notify_conn(game.est_connections, NULL, E_GAME_END, ftc_server,
                _("Game ended as the turn limit was exceeded."));
    return TRUE;
  } else if (game.info.turn == game.server.end_turn) {
    /* Give them a chance to decide to extend the game */
    notify_conn(game.est_connections, NULL, E_GAME_END, ftc_server,
                _("Notice: game will end at the end of this turn due "
                  "to 'endturn' server setting."));
  }

  /* Check for a spacerace win (and notify of imminent arrivals).
   * Check this after checking turn limit, because we are checking for
   * the spaceship arriving in the year corresponding to the turn
   * that's about to start. */
  {
    int n, i;
    struct player *arrivals[MAX_NUM_PLAYER_SLOTS];

    n = rank_spaceship_arrival(arrivals);

    for (i = 0; i < n; i++) {
      struct player *pplayer = arrivals[i];
      const struct player_list *members;
      bool win;

      if (game.info.year < (int)spaceship_arrival(pplayer)) {
        /* We are into the future arrivals */
        break;
      }

      /* Mark as arrived and notify everyone. */
      spaceship_arrived(pplayer);

      if (!game.server.endspaceship) {
        /* Games does not end on spaceship arrival. At least print all the
         * arrival messages. */
        continue;
      }

      /* This player has won, now check if anybody else wins with them. */
      members = team_members(pplayer->team);
      win = FALSE;
      player_list_iterate(members, pteammate) {
        if (pplayer->is_alive
            && !player_status_check((pteammate), PSTATUS_SURRENDER)) {
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
                    team_name_translation(pplayer->team));
        /* All players of the team win, even dead and surrendered ones. */
        player_list_iterate(members, pteammate) {
          pteammate->is_winner = TRUE;
        } player_list_iterate_end;
      } else {
        notify_conn(NULL, NULL, E_GAME_END, ftc_server,
                    _("Game ended in victory for %s."), player_name(pplayer));
        pplayer->is_winner = TRUE;
      }
      return TRUE;
    }

    /* Print notice(s) of imminent arrival. These are not infallible
     * (quite apart from risk of enemy action) because arrival is
     * year-based, and some effect may change the timeline between
     * now and the end of the next turn.
     * (Also the order of messages will not always indicate tie-breaks,
     * if the shuffled order is changed every turn, as it is for
     * PMT_CONCURRENT games.) */
    for (; i < n; i++) {
      const struct player *pplayer = arrivals[i];
      struct packet_game_info next_info = game.info; /* struct copy */

      /* Advance the calendar in a throwaway copy of game.info. */
      game_next_year(&next_info);

      if (next_info.year < (int)spaceship_arrival(pplayer)) {
        /* Even further in the future */
        break;
      }

      notify_player(NULL, NULL, E_SPACESHIP, ftc_server,
                    _("Notice: the %s spaceship will likely arrive at "
                      "Alpha Centauri next turn."),
                    nation_adjective_for_player(pplayer));
    }
  }

  return FALSE;
}

/**********************************************************************//**
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
  researches_iterate(presearch) {
    send_research_info(presearch, dest);
  } researches_iterate_end;
  send_map_info(dest);
  send_all_known_tiles(dest);
  send_all_known_cities(dest);
  send_all_known_units(dest);
  send_spaceship_info(NULL, dest);

  cities_iterate(pcity) {
    package_and_send_worker_tasks(pcity);
  } cities_iterate_end;
}

/**********************************************************************//**
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

/**********************************************************************//**
  Give contact to players with the EFT_HAVE_CONTACTS effect (traditionally
  from Marco Polo's Embassy).
**************************************************************************/
static void do_have_contacts_effect(void)
{
  phase_players_iterate(pplayer) {
    if (get_player_bonus(pplayer, EFT_HAVE_CONTACTS) > 0) {
      players_iterate(pother) {
	/* Note this gives pplayer contact with pother, but doesn't give
	 * pother contact with pplayer.  This may cause problems in other
	 * parts of the code if we're not careful. */
	make_contact(pplayer, pother, NULL);
      } players_iterate_end;
    }
  } phase_players_iterate_end;
}

/**********************************************************************//**
  Handle the vision granting effect EFT_BORDER_VISION
**************************************************************************/
static void do_border_vision_effect(void)
{
  if (game.info.borders != BORDERS_ENABLED) {
    /* Border_Vision is useless. If borders are disabled there are no
     * borders to see inside. If borders are seen they are seen already.
     * The borders setting can't change after the game has started. */
    return;
  }

  phase_players_iterate(plr) {
    bool new_border_vision;

    /* Check the Border_Vision effect for this player. */
    new_border_vision = (0 < get_player_bonus(plr, EFT_BORDER_VISION));

    if (new_border_vision != plr->server.border_vision) {
      /* Border vision changed. */

      /* Update the map */
      map_set_border_vision(plr, new_border_vision);
    }
  } phase_players_iterate_end;
}

/**********************************************************************//**
  Handle environmental upsets, meaning currently pollution or fallout.
**************************************************************************/
static void update_environmental_upset(enum environment_upset_type type,
                                       int *current, int *accum, int *level,
                                       int percent,
                                       void (*upset_action_fn)(int))
{
  int count;

  count = 0;
  extra_type_iterate(cause) {
    if (extra_causes_env_upset(cause, type)) {
      whole_map_iterate(&(wld.map), ptile) {
        if (tile_has_extra(ptile, cause)) {
          count++;
        }
      } whole_map_iterate_end;
    }
  } extra_type_iterate_end;

  *current = (count * percent) / 100;
  *accum += count;
  if (*accum < *level) {
    *accum = 0;
  } else {
    *accum -= *level;
    if (fc_rand((map_num_tiles() + 19) / 20) < *accum) {
      upset_action_fn((wld.map.xsize / 10) + (wld.map.ysize / 10) + ((*accum) * 5));
      *accum = 0;
      *level += (map_num_tiles() + 999) / 1000;
    }
  }

  log_debug("environmental_upset: type=%-4d current=%-2d "
            "level=%-2d accum=%-2d", type, *current, *level, *accum);
}

/**********************************************************************//**
  Notify about units at risk of disband due to armistice.
**************************************************************************/
static void notify_illegal_armistice_units(struct player *phost,
                                           struct player *pguest,
                                           int turns_left)
{
  int nunits = 0;
  struct unit *a_unit = NULL;

  unit_list_iterate(pguest->units, punit) {
    if (tile_owner(unit_tile(punit)) == phost && is_military_unit(punit)) {
      nunits++;
      a_unit = punit;
    }
  } unit_list_iterate_end;
  if (nunits > 0) {
    struct astring unitstr = ASTRING_INIT;

    astr_set(&unitstr,
             /* TRANS: "... 2 military units in Norwegian territory." */
             PL_("Warning: you still have %d military unit in %s territory.",
                 "Warning: you still have %d military units in %s territory.",
                 nunits),
             nunits, nation_adjective_for_player(phost));
    /* If there's one lousy unit left, we may as well include a link for it */
    notify_player(pguest, nunits == 1 ? unit_tile(a_unit) : NULL,
                  E_DIPLOMACY, ftc_server,
                  /* TRANS: %s is another, separately translated sentence,
                   * ending in a full stop. */
                  PL_("%s Any such units will be disbanded in %d turn, "
                      "in accordance with peace treaty.",
                      "%s Any such units will be disbanded in %d turns, "
                      "in accordance with peace treaty.", turns_left),
                  astr_str(&unitstr), turns_left);
    astr_free(&unitstr);
  }
}

/**********************************************************************//**
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

/**********************************************************************//**
  Check for cease-fires and armistices running out; update cancelling
  reasons and contact information.
**************************************************************************/
static void update_diplomatics(void)
{
  players_iterate(plr1) {
    players_iterate(plr2) {
      struct player_diplstate *state = player_diplstate_get(plr1, plr2);

      /* Players might just met when first of them was being handled
       * (pact with third player changed and units got bounced next
       *  to second unit to form first contact)
       * Do not decrease the counters for the other player yet in this turn */
      if (state->first_contact_turn != game.info.turn) {
        struct player_diplstate *state2 = player_diplstate_get(plr2, plr1);

        state->has_reason_to_cancel = MAX(state->has_reason_to_cancel - 1, 0);
        state->contact_turns_left = MAX(state->contact_turns_left - 1, 0);

        if (state->type == DS_ARMISTICE
            /* Don't count down if auto canceled this turn. Auto canceling
             * happens in this loop. */
            && state->auto_cancel_turn != game.info.turn) {
          state->turns_left--;
          if (state->turns_left <= 0) {
            state->type = DS_PEACE;
            state2->type = DS_PEACE;
            state->turns_left = 0;
            state2->turns_left = 0;
            remove_illegal_armistice_units(plr1, plr2);
          } else {
            notify_illegal_armistice_units(plr1, plr2, state->turns_left);
          }
        }

        if (state->type == DS_CEASEFIRE) {
          state->turns_left--;
          switch (state->turns_left) {
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
                struct player_diplstate *to1
                    = player_diplstate_get(plr3, plr1);
                struct player_diplstate *from1
                    = player_diplstate_get(plr1, plr3);
                struct player_diplstate *to2
                    = player_diplstate_get(plr3, plr2);
                struct player_diplstate *from2
                    = player_diplstate_get(plr2, plr3);
                const char *plr1name = player_name(plr1);
                const char *plr2name = player_name(plr2);
                bool cancel1;
                bool cancel2;

                if (players_on_same_team(plr3, plr1)) {
                  fc_assert(!players_on_same_team(plr3, plr2));

                  cancel1 = FALSE;
                  cancel2 = TRUE;

                  notify_player(plr3, NULL, E_TREATY_BROKEN, ftc_server,
                                _("The cease-fire between %s and %s has run out. "
                                  "They are at war. You cancel your alliance "
                                  "with %s."),
                                plr1name, plr2name, plr2name);
                } else if (players_on_same_team(plr3, plr2)) {

                  cancel1 = TRUE;
                  cancel2 = FALSE;

                  notify_player(plr3, NULL, E_TREATY_BROKEN, ftc_server,
                                _("The cease-fire between %s and %s has run out. "
                                  "They are at war. You cancel your alliance "
                                  "with %s."),
                                plr1name, plr2name, plr1name);
                } else {

                  cancel1 = TRUE;
                  cancel2 = TRUE;

                  notify_player(plr3, NULL, E_TREATY_BROKEN, ftc_server,
                                _("The cease-fire between %s and %s has run out. "
                                  "They are at war. You cancel your alliance "
                                  "with both."),
                                player_name(plr1),
                                player_name(plr2));
                }

                if (cancel1) {
                  /* Cancel the alliance. */
                  to1->has_reason_to_cancel = 1;
                  handle_diplomacy_cancel_pact(plr3, player_number(plr1), CLAUSE_ALLIANCE);

                  /* Avoid asymmetric turns_left for the armistice. */
                  to1->auto_cancel_turn = game.info.turn;
                  from1->auto_cancel_turn = game.info.turn;

                  /* Count down for this turn. */
                  to1->turns_left--;
                  from1->turns_left--;
                }

                if (cancel2) {
                  /* Cancel the alliance. */
                  to2->has_reason_to_cancel = 1;
                  handle_diplomacy_cancel_pact(plr3, player_number(plr2), CLAUSE_ALLIANCE);

                  /* Avoid asymmetric turns_left for the armistice. */
                  to2->auto_cancel_turn = game.info.turn;
                  from2->auto_cancel_turn = game.info.turn;

                  /* Count down for this turn. */
                  to2->turns_left--;
                  from2->turns_left--;
                }
              }
            } players_iterate_alive_end;
            break;
          }
        }
      }
    } players_iterate_end;
  } players_iterate_end;
}

/**********************************************************************//**
  Check all players to see whether they are dying.

  WARNING: do not call this while doing any handling of players, units,
  etc.  If a player dies, all his units will be wiped and other data will
  be overwritten.
**************************************************************************/
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

/**********************************************************************//**
  Called at the start of each (new) phase to do AI activities.
**************************************************************************/
static void ai_start_phase(void)
{
  phase_players_iterate(pplayer) {
    if (is_ai(pplayer)) {
      CALL_PLR_AI_FUNC(first_activities, pplayer, pplayer);
    }
  } phase_players_iterate_end;
  kill_dying_players();
}

/**********************************************************************//**
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
    if (game.info.phase_mode != game.server.phase_mode_stored) {
      event_cache_phases_invalidate();
      game.info.phase_mode = game.server.phase_mode_stored;
    }
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
    script_server_signal_emit("turn_begin", game.info.turn, game.info.year);
    script_server_signal_emit("turn_started",
                              game.info.turn > 0 ? game.info.turn - 1
                              : game.info.turn, game.info.year);

    /* We build scores at the beginning of every turn.  We have to
     * build them at the beginning so that the AI can use the data,
     * and we are sure to have it when we need it. */
    players_iterate(pplayer) {
      calc_civ_score(pplayer);
    } players_iterate_end;
    log_civ_score_now();

    /* Retire useless barbarian units */
    players_iterate(pplayer) {
      unit_list_iterate_safe(pplayer->units, punit) {
        struct tile *ptile = punit->tile;

        if (unit_can_be_retired(punit)
            && fc_rand(100) < get_unit_bonus(punit, EFT_RETIRE_PCT)) {
          notify_player(pplayer, ptile, E_UNIT_LOST_MISC, ftc_server,
                        /* TRANS: %s is a unit type */
                        _("%s retired!"), unit_tile_link(punit));
          wipe_unit(punit, ULR_RETIRED, NULL);
          continue;
        }
      } unit_list_iterate_safe_end;
    } players_iterate_end;
  }

  /* find out if users attached to players have been attached to those players
   * for long enough. The first user to do so becomes "associated" to that
   * player for ranking purposes. */
  players_iterate(pplayer) {
    if (pplayer->unassigned_ranked
        && pplayer->user_turns++ > TURNS_NEEDED_TO_RANK
	&& pplayer->is_alive) {
      sz_strlcpy(pplayer->ranked_username, pplayer->username);
      pplayer->unassigned_ranked = pplayer->unassigned_user;
    }
  } players_iterate_end;

  if (is_new_turn) {
    /* See if the value of fog of war has changed */
    if (game.info.fogofwar != game.server.fogofwar_old) {
      if (game.info.fogofwar) {
        enable_fog_of_war();
        game.server.fogofwar_old = TRUE;
      } else {
        disable_fog_of_war();
        game.server.fogofwar_old = FALSE;
      }
    }

    if (game.info.phase_mode == PMT_CONCURRENT) {
      log_debug("Shuffleplayers");
      shuffle_players();
    }

    game.info.phase = 0;
  }

  sanity_check();
}

/**********************************************************************//**
  Begin a phase of movement.  This handles all beginning-of-phase actions
  for one or more players.
**************************************************************************/
static void begin_phase(bool is_new_phase)
{
  log_debug("Begin phase");

  conn_list_do_buffer(game.est_connections);

  phase_players_iterate(pplayer) {
    if (is_new_phase || !game.server.turnblock) {
      /* Otherwise respect what was loaded from the savegame. */
      pplayer->phase_done = FALSE;
    }
    pplayer->ai_phase_done = FALSE;
  } phase_players_iterate_end;
  send_player_all_c(NULL, NULL);

  dlsend_packet_start_phase(game.est_connections, game.info.phase);

  if (!is_new_phase) {
    conn_list_iterate(game.est_connections, pconn) {
      send_diplomatic_meetings(pconn);
    } conn_list_iterate_end;
  }

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
      update_unit_activities(pplayer);
      flush_packets();
    } phase_players_iterate_end;
    /* Execute orders after activities have been completed (roads built,
     * pillage done, etc.). */
    phase_players_iterate(pplayer) {
      execute_unit_orders(pplayer);
      flush_packets();
    } phase_players_iterate_end;
    phase_players_iterate(pplayer) {
      finalize_unit_phase_beginning(pplayer);
    } phase_players_iterate_end;
    flush_packets();
  }

  phase_players_iterate(pplayer) {
    log_debug("beginning player turn for #%d (%s)",
              player_number(pplayer), player_name(pplayer));
    if (is_human(pplayer)) {
      building_advisor(pplayer);
    }
  } phase_players_iterate_end;

  phase_players_iterate(pplayer) {
    send_player_cities(pplayer);
  } phase_players_iterate_end;

  flush_packets();  /* to curb major city spam */
  conn_list_do_unbuffer(game.est_connections);

  alive_phase_players_iterate(pplayer) {
    update_revolution(pplayer);
  } alive_phase_players_iterate_end;

  if (is_new_phase) {
    /* Try to avoid hiding events under a diplomacy dialog */
    phase_players_iterate(pplayer) {
      if (is_ai(pplayer)) {
        CALL_PLR_AI_FUNC(diplomacy_actions, pplayer, pplayer);
      }
    } phase_players_iterate_end;

    log_debug("Aistartturn");
    ai_start_phase();
  } else {
    phase_players_iterate(pplayer) {
      if (is_ai(pplayer)) {
        CALL_PLR_AI_FUNC(restart_phase, pplayer, pplayer);
      }
    } phase_players_iterate_end;
  }

  sanity_check();

  game.tinfo.last_turn_change_time = (float)game.server.turn_change_time;
  game.tinfo.seconds_to_phasedone = (double)current_turn_timeout();
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

/**********************************************************************//**
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

  /* Enact any policy changes.
   * Do this first so that following end-phase activities take the
   * change into account. */
  phase_players_iterate(pplayer) {
    multipliers_iterate(pmul) {
      int idx = multiplier_index(pmul);

      if (!multiplier_can_be_changed(pmul, pplayer)) {
        if (pplayer->multipliers[idx] != pmul->def) {
          notify_player(pplayer, NULL, E_MULTIPLIER, ftc_server,
                        _("%s restored to the default value %d"),
                        multiplier_name_translation(pmul),
                        pmul->def);
          pplayer->multipliers[idx] = pmul->def;
        }
      } else {
        if (pplayer->multipliers[idx] != pplayer->multipliers_target[idx]) {
          notify_player(pplayer, NULL, E_MULTIPLIER, ftc_server,
                        _("%s now at value %d"),
                        multiplier_name_translation(pmul),
                        pplayer->multipliers_target[idx]);

          pplayer->multipliers[idx] =
            pplayer->multipliers_target[idx];
        }
      }
    } multipliers_iterate_end;
  } phase_players_iterate_end;

  phase_players_iterate(pplayer) {
    struct research *presearch = research_get(pplayer);

    if (A_UNSET == presearch->researching) {
      Tech_type_id next_tech = research_goal_step(presearch,
                                                  presearch->tech_goal);

      if (A_UNSET != next_tech) {
        choose_tech(presearch, next_tech);
      } else {
        choose_random_tech(presearch);
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
    if (is_ai(pplayer)) {
      CALL_PLR_AI_FUNC(last_activities, pplayer, pplayer);
    }
  } phase_players_iterate_end;

  /* Refresh cities */
  phase_players_iterate(pplayer) {
    research_get(pplayer)->got_tech = FALSE;
    research_get(pplayer)->got_tech_multi = FALSE;
  } phase_players_iterate_end;

  phase_players_iterate(pplayer) {
    do_tech_parasite_effect(pplayer);
    player_restore_units(pplayer);

    /* If player finished spaceship parts last turn already, and didn't place them
     * during this entire turn, autoplace them. */
    if (adv_spaceship_autoplace(pplayer, &pplayer->spaceship)) {
      notify_player(pplayer, NULL, E_SPACESHIP, ftc_server,
                    _("Automatically placed spaceship parts that were still not placed."));
    }

    update_city_activities(pplayer);
    city_thaw_workers_queue();
    pplayer->history += nation_history_gain(pplayer);
    research_get(pplayer)->researching_saved = A_UNKNOWN;
    /* reduce the number of bulbs by the amount needed for tech upkeep and
     * check for finished research */
    update_bulbs(pplayer, -player_tech_upkeep(pplayer), TRUE);
    flush_packets();
  } phase_players_iterate_end;

  /* Some player/global effect may have changed cities' vision range */
  phase_players_iterate(pplayer) {
    refresh_player_cities_vision(pplayer);
  } phase_players_iterate_end;

  kill_dying_players();

  /* Unfreeze sending of cities. */
  send_city_suppression(FALSE);

  phase_players_iterate(pplayer) {
    send_player_cities(pplayer);
  } phase_players_iterate_end;
  flush_packets();  /* to curb major city spam */

  do_reveal_effects();
  do_have_contacts_effect();
  do_border_vision_effect();

  phase_players_iterate(pplayer) {
    CALL_PLR_AI_FUNC(phase_finished, pplayer, pplayer);
    /* This has to be after all access to advisor data. */
    /* We used to run this for ai players only, but data phase
       is initialized for human players also. */
    adv_data_phase_done(pplayer);
  } phase_players_iterate_end;
}

/**********************************************************************//**
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
    if (!is_ai(pplayer) || is_barbarian(pplayer)) {
      continue;
    }
    unit_list_iterate(pplayer->units, punit) {
      if (unit_is_cityfounder(punit)) {
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
    if (check_city_migrations()) {
      /* Make sure everyone has updated information about BOTH ends of the
       * migration movements. */
      players_iterate(plr) {
        city_list_iterate(plr->cities, pcity) {
          send_city_info(NULL, pcity);
        } city_list_iterate_end;
      } players_iterate_end;
    }
  }

  check_disasters();

  /* Check for new achievements during the turn.
   * This is not within phase, as multiple players may
   * achieve at the same turn and everyone deserves equal opportunity
   * to win. */
  achievements_iterate(ach) {
    struct player_list *achievers = player_list_new();
    struct player *first = achievement_plr(ach, achievers);
    struct packet_achievement_info pack;

    pack.id = achievement_index(ach);
    pack.gained = TRUE;

    if (first != NULL) {
      notify_player(first, NULL, E_ACHIEVEMENT, ftc_server,
                    "%s", achievement_first_msg(ach));

      pack.first = TRUE;

      lsend_packet_achievement_info(first->connections, &pack);

      script_server_signal_emit("achievement_gained", ach, first, TRUE);

    }

    pack.first = FALSE;

    if (!ach->unique) {
      player_list_iterate(achievers, pplayer) {
        /* Message already sent to first one */
        if (pplayer != first) {
          notify_player(pplayer, NULL, E_ACHIEVEMENT, ftc_server,
                        "%s", achievement_later_msg(ach));

          lsend_packet_achievement_info(pplayer->connections, &pack);

          script_server_signal_emit("achievement_gained", ach, pplayer,
                                    FALSE);
        }
      } player_list_iterate_end;
    }

    player_list_destroy(achievers);
  } achievements_iterate_end;

  if (game.info.global_warming) {
    update_environmental_upset(EUT_GLOBAL_WARMING, &game.info.heating,
                               &game.info.globalwarming,
                               &game.info.warminglevel,
                               game.server.global_warming_percent,
                               global_warming);
  }

  if (game.info.nuclear_winter) {
    update_environmental_upset(EUT_NUCLEAR_WINTER, &game.info.cooling,
                               &game.info.nuclearwinter,
                               &game.info.coolinglevel,
                               game.server.nuclear_winter_percent,
                               nuclear_winter);
  }

  /* Handle disappearing extras before appearing extras ->
   * Extra never appears only to disappear at the same turn,
   * but it can disappear and reappear. */
  extra_type_by_rmcause_iterate(ERM_DISAPPEARANCE, pextra) {
    whole_map_iterate(&(wld.map), ptile) {
      if (tile_has_extra(ptile, pextra)
          && fc_rand(10000) < pextra->disappearance_chance
          && can_extra_disappear(pextra, ptile)) {
        tile_extra_rm_apply(ptile, pextra);

        update_tile_knowledge(ptile);

        if (tile_owner(ptile) != NULL) {
          /* TODO: Should notify players nearby even when borders disabled,
           *       like in case of barbarian uprising */
          notify_player(tile_owner(ptile), ptile,
                        E_SPONTANEOUS_EXTRA, ftc_server,
                        /* TRANS: Small Fish disappears from (32, 72). */
                        _("%s disappears from %s."),
                        extra_name_translation(pextra),
                        tile_link(ptile));
        }

        /* Unit activities at the target tile and its neighbors may now
         * be illegal because of present reqs. */
        unit_activities_cancel_all_illegal(ptile);
        adjc_iterate(&(wld.map), ptile, n_tile) {
          unit_activities_cancel_all_illegal(n_tile);
        } adjc_iterate_end;
      }
    } whole_map_iterate_end;
  } extra_type_by_rmcause_iterate_end;

  extra_type_by_cause_iterate(EC_APPEARANCE, pextra) {
    whole_map_iterate(&(wld.map), ptile) {
      if (!tile_has_extra(ptile, pextra)
          && fc_rand(10000) < pextra->appearance_chance
          && can_extra_appear(pextra, ptile)) {

        tile_extra_apply(ptile, pextra);

        update_tile_knowledge(ptile);

        if (tile_owner(ptile) != NULL) {
          /* TODO: Should notify players nearby even when borders disabled,
           *       like in case of barbarian uprising */
          notify_player(tile_owner(ptile), ptile,
                        E_SPONTANEOUS_EXTRA, ftc_server,
                        /* TRANS: Small Fish appears to (32, 72). */
                        _("%s appears to %s."),
                        extra_name_translation(pextra),
                        tile_link(ptile));
        }

        /* Unit activities at the target tile and its neighbors may now
         * be illegal because of !present reqs. */
        unit_activities_cancel_all_illegal(ptile);
        adjc_iterate(&(wld.map), ptile, n_tile) {
          unit_activities_cancel_all_illegal(n_tile);
        } adjc_iterate_end;
      }
    } whole_map_iterate_end;
  } extra_type_by_cause_iterate_end;

  update_diplomatics();
  make_history_report();
  settings_turn();
  stdinhand_turn();
  voting_turn();
  send_city_turn_notifications(NULL);

  log_debug("Gamenextyear");
  game_advance_year();
  players_iterate_alive(pplayer) {
    pplayer->turns_alive++;
  } players_iterate_alive_end;

  log_debug("Updatetimeout");
  update_timeout();

  log_debug("Sendgameinfo");
  send_game_info(NULL);

  log_debug("Sendplayerinfo");
  send_player_all_c(NULL, NULL);

  log_debug("Sendresearchinfo");
  researches_iterate(presearch) {
    send_research_info(presearch, NULL);
  } researches_iterate_end;

  log_debug("Sendyeartoclients");
  send_year_to_clients();
}

/**********************************************************************//**
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
  case AS_TIMER:
    reason_filename = "timer";
    break;
  }

  fc_assert(256 > strlen(game.server.save_name));

  if (type != AS_TIMER) {
    generate_save_name(game.server.save_name, filename, sizeof(filename),
                       reason_filename);
  } else {
    fc_snprintf(filename, sizeof(filename), "%s-timer", game.server.save_name);
  }
  save_game(filename, save_reason, FALSE);
}

/**********************************************************************//**
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
  set_running_game_access_level();

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

/**********************************************************************//**
  Quit the server and exit.
**************************************************************************/
void server_quit(void)
{
  if (server_state() == S_S_RUNNING) {
    /* Quitting mid-game. */

    phase_players_iterate(pplayer) {
      CALL_PLR_AI_FUNC(phase_finished, pplayer, pplayer);
      /* This has to be after all access to advisor data. */
      /* We used to run this for ai players only, but data phase
         is initialized for human players also. */
      adv_data_phase_done(pplayer);
    } phase_players_iterate_end;
  }

  if (game.server.save_timer != NULL) {
    timer_destroy(game.server.save_timer);
    game.server.save_timer = NULL;
  }
  if (between_turns != NULL) {
    timer_destroy(between_turns);
    between_turns = NULL;
  }
  if (eot_timer != NULL) {
    timer_destroy(eot_timer);
  }
  set_server_state(S_S_OVER);
  mapimg_free();
  server_game_free();
  diplhand_free();
  voting_free();
  adv_settlers_free();
  ai_timer_free();
  if (game.server.phase_timer != NULL) {
    timer_destroy(game.server.phase_timer);
    game.server.phase_timer = NULL;
  }
  if (game.server.save_timer != NULL) {
    timer_destroy(game.server.save_timer);
    game.server.save_timer = NULL;
  }

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
  generator_free();
  close_connections_and_socket();
  rulesets_deinit();
  ruleset_choices_free();
  CALL_FUNC_EACH_AI(module_close);
  timing_log_free();
  registry_module_close();
  fc_destroy_mutex(&game.server.mutexes.city_list);
  free_libfreeciv();
  free_nls();
  con_log_close();
  cmdline_option_values_free();
  exit(EXIT_SUCCESS);
}

/**********************************************************************//**
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

  switch (type) {
  case REPORT_WONDERS_OF_THE_WORLD:
    report_wonders_of_the_world(dest);
    return;
  case REPORT_TOP_5_CITIES:
    report_top_five_cities(dest);
    return;
  case REPORT_DEMOGRAPHIC:
    report_demographics(pconn);
    return;
  case REPORT_ACHIEVEMENTS:
    report_achievements(pconn);
    return;
  }

  notify_conn(dest, NULL, E_BAD_COMMAND, ftc_server,
              _("request for unknown report (type %d)"), type);
}

/**********************************************************************//**
  Mark identity number free.
**************************************************************************/
void identity_number_release(int id)
{
  BV_CLR(identity_numbers_used, id);
}

/**********************************************************************//**
  Mark identity number allocated.
**************************************************************************/
void identity_number_reserve(int id)
{
  BV_SET(identity_numbers_used, id);
}

/**********************************************************************//**
  Check whether identity number is currently allocated.
**************************************************************************/
static bool identity_number_is_used(int id)
{
  return BV_ISSET(identity_numbers_used, id);
}

/**********************************************************************//**
  Increment identity_number and return result.
**************************************************************************/
static int increment_identity_number(void)
{
  server.identity_number = (server.identity_number + 1) % IDENTITY_NUMBER_SIZE;
  return server.identity_number;
}

/**********************************************************************//**
  Identity ids wrap at IDENTITY_NUMBER_SIZE, skipping IDENTITY_NUMBER_ZERO
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

/**********************************************************************//**
  Returns TRUE if the packet type is an edit packet sent by the client.

  NB: The first and last client edit packets here must match those
  defined in common/networking/packets.def.
**************************************************************************/
static bool is_client_edit_packet(int type)
{
  return PACKET_EDIT_MODE <= type && type <= PACKET_EDIT_GAME;
}

/**********************************************************************//**
  Returns FALSE if connection should be closed (because the clients was
  rejected). Returns TRUE else.
**************************************************************************/
bool server_packet_input(struct connection *pconn, void *packet, int type)
{
  struct player *pplayer;

  /* a NULL packet can be returned from receive_packet_goto_route() */
  if (!packet) {
    return TRUE;
  }

  /* 
   * Old pre-delta clients (before 2003-11-28) send a
   * PACKET_LOGIN_REQUEST (type 0) to the server. We catch this and
   * reply with an old reject packet. Since there is no struct for
   * this old packet anymore we build it by hand.
   */
  if (type == 0) {
    unsigned char buffer[4096];
    struct raw_data_out dout;

    log_normal(_("Warning: rejecting old client %s"),
               conn_description(pconn));

    dio_output_init(&dout, buffer, sizeof(buffer));
    dio_put_uint16_raw(&dout, 0);

    /* 1 == PACKET_LOGIN_REPLY in the old client */
    dio_put_uint8_raw(&dout, 1);

    dio_put_bool32_raw(&dout, FALSE);
    dio_put_string_raw(&dout,
            _("Your client is too old. To use this server, "
              "please upgrade your client to a "
              "Freeciv 2.2 or later."));
    dio_put_string_raw(&dout, "");

    {
      size_t size = dio_output_used(&dout);
      dio_output_rewind(&dout);
      dio_put_uint16_raw(&dout, size);

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
      || type == PACKET_CLIENT_HEARTBEAT
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
    if (type == PACKET_PLAYER_READY && pconn->observer) {
      handle_observer_ready(pconn);
      return TRUE;
    }
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

/**********************************************************************//**
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
  if (game.server.fixedlength && current_turn_timeout() != 0) {
    return;
  }

  /* If there are no connected players, don't automatically advance.  This is
   * a hack to prevent all-AI games from running rampant.  Note that if
   * timeout is set to -1 this function call is skipped entirely and the
   * server will run rampant. */
  players_iterate_alive(pplayer) {
    if (pplayer->is_connected && (is_human(pplayer) || pplayer->phase_done)) {
      connected = TRUE;
      break;
    }
  } players_iterate_alive_end;

  if (!connected) {
    return;
  }

  phase_players_iterate(pplayer) {
    if (!pplayer->phase_done && pplayer->is_alive) {
      if (pplayer->is_connected) {
        /* In all cases, we wait for any connected players. */
        return;
      }
      if (game.server.turnblock && is_human(pplayer)) {
        /* If turnblock is enabled check for human players, connected
         * or not. */
        return;
      }
      if (is_ai(pplayer) && !pplayer->ai_phase_done) {
        /* AI player has not finished */
        return;
      }
    }
  } phase_players_iterate_end;

  force_end_of_sniff = TRUE;
}

/**********************************************************************//**
  Update information about which nations have start positions on the map.

  Call this on server start, or when loading a scenario.
**************************************************************************/
void update_nations_with_startpos(void)
{
  if (!game_was_started() && 0 < map_startpos_count()) {
    /* Restrict nations to those for which start positions are defined. */
    nations_iterate(pnation) {
      fc_assert_action_msg(NULL == pnation->player,
        if (pnation->player->nation == pnation) {
          /* At least assignment is consistent. Leave nation assigned,
           * and make sure that nation is also marked pickable. */
          pnation->server.no_startpos = FALSE;
          continue;
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
        /* Always allow land and sea barbarians regardless of start
         * positions. */
        pnation->server.no_startpos = FALSE;
      } else {
        /* Restrict the set of nations offered to players, based on
         * start positions.
         * If there are no start positions for a nation, remove it from the
         * available set. */
        pnation->server.no_startpos = TRUE;
        map_startpos_iterate(psp) {
          if (startpos_nation_allowed(psp, pnation)) {
            /* There is at least one start position that allows this nation,
             * so allow it to be picked.
             * (Depending on what nations players actually pick, it's not
             * guaranteed that the server can always find a match between
             * nations in this subset and start positions, in which case the
             * server may create mismatches.) */
            pnation->server.no_startpos = FALSE;
            break;
          }
        } map_startpos_iterate_end;
      }
    } nations_iterate_end;
  } else {
    /* Not restricting nations by start positions. */
    nations_iterate(pnation) {
      pnation->server.no_startpos = FALSE;
    } nations_iterate_end;
  }
}

/**********************************************************************//**
  Handles a pick-nation packet from the client.  These packets are
  handled by connection because ctrl users may edit anyone's nation in
  pregame, and editing is possible during a running game.
**************************************************************************/
void handle_nation_select_req(struct connection *pc, int player_no,
                              Nation_type_id nation_no, bool is_male,
                              const char *name, int style)
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
    if (style < 0 || style >= game.control.num_styles) {
      return;
    }

    if (!client_can_pick_nation(new_nation)) {
      notify_player(pplayer, NULL, E_NATION_SELECTED, ftc_server,
                    _("%s nation is not available for user selection."),
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

    /* Should be caught by is_nation_pickable() */
    fc_assert_ret(nation_is_in_current_set(new_nation));

    notify_conn(NULL, NULL, E_NATION_SELECTED, ftc_server,
                _("%s is the %s ruler %s."),
                pplayer->username,
                nation_adjective_translation(new_nation),
                player_name(pplayer));

    pplayer->is_male = is_male;
    pplayer->style = style_by_number(style);
  } else if (name[0] == '\0') {
    char message[1024];

    server_player_set_name_full(pc, pplayer, NULL, ANON_PLAYER_NAME,
                                message, sizeof(message));
  }

  (void) player_set_nation(pplayer, new_nation);
  send_player_info_c(pplayer, game.est_connections);
}

/**********************************************************************//**
  Handle a player-ready packet from global observer.
**************************************************************************/
static void handle_observer_ready(struct connection *pconn)
{
  if (pconn->access_level == ALLOW_HACK) {
    players_iterate(plr) {
      if (is_human(plr)) {
        return;
      }
    } players_iterate_end;

    start_command(NULL, FALSE, TRUE);
  }
}

/**********************************************************************//**
  Handle a player-ready packet.
**************************************************************************/
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

    players_iterate(other_player) {
      if (other_player->is_connected) {
	if (other_player->is_ready) {
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

/**********************************************************************//**
  Fill or remove players to meet the given aifill.
  If return is non-NULL, points to a translated string explaining why
  the total number of players is less than 'amount'.
**************************************************************************/
const char *aifill(int amount)
{
  char *limitreason = NULL;
  int limit;
 
  if (game_was_started()) {
    return NULL;
  }

  limit = MIN(amount, game.server.max_players);
  if (limit < amount) {
    limitreason = _("requested more than 'maxplayers' setting");
  }

  /* Limit to nations provided by ruleset */
  if (limit > server.playable_nations) {
    limit = server.playable_nations;
    if (nation_set_count() > 1) {
      limitreason = _("not enough playable nations in this nation set "
                      "(see 'nationset' setting)");
    } else {
      limitreason = _("not enough playable nations");
    }
  }

  if (limit < player_count()) {
    int removal = player_slot_count() - 1;

    while (limit < player_count() && 0 <= removal) {
      struct player *pplayer = player_by_number(removal);

      removal--;
      if (!pplayer) {
        continue;
      }

      if (!pplayer->is_connected && !pplayer->was_created) {
        server_remove_player(pplayer);
      }
    }

    /* 'limit' can be different from 'player_count()' at this point if
     * there are more human or created players than the 'limit'. */
    return limitreason;
  }

  while (limit > player_count()) {
    char leader_name[MAX_LEN_NAME];
    int filled = 1;
    struct player *pplayer;

    pplayer = server_create_player(-1, default_ai_type_name(),
                                   NULL, FALSE);
    /* !game_was_started() so no need to assign_player_colors() */
    if (!pplayer) {
      break;
    }
    server_player_init(pplayer, FALSE, TRUE);

    player_set_nation(pplayer, NULL);

    do {
      fc_snprintf(leader_name, sizeof(leader_name), "AI*%d", filled++);
    } while (player_by_name(leader_name));
    server_player_set_name(pplayer, leader_name);
    pplayer->random_name = TRUE;
    sz_strlcpy(pplayer->username, _(ANON_USER_NAME));
    pplayer->unassigned_user = TRUE;

    pplayer->ai_common.skill_level = game.info.skill_level;
    set_as_ai(pplayer);
    set_ai_level_directer(pplayer, game.info.skill_level);

    CALL_PLR_AI_FUNC(gained_control, pplayer, pplayer);

    log_normal(_("%s has been added as %s level AI-controlled player (%s)."),
               player_name(pplayer),
               ai_level_translated_name(pplayer->ai_common.skill_level),
               ai_name(pplayer->ai));
    notify_conn(NULL, NULL, E_SETTING, ftc_server,
                _("%s has been added as %s level AI-controlled player (%s)."),
                player_name(pplayer),
                ai_level_translated_name(pplayer->ai_common.skill_level),
                ai_name(pplayer->ai));

    send_player_info_c(pplayer, NULL);
  }

  return limitreason;
}

/**********************************************************************//**
  Tool for generate_players().
**************************************************************************/
#define SPECHASH_TAG startpos
#define SPECHASH_IKEY_TYPE struct startpos *
#define SPECHASH_INT_DATA_TYPE
#include "spechash.h"
#define startpos_hash_iterate(hash, psp, c)                                 \
  TYPED_HASH_ITERATE(struct startpos *, intptr_t, hash, psp, c)
#define startpos_hash_iterate_end HASH_ITERATE_END

/**********************************************************************//**
  Tool for generate_players().
**************************************************************************/
static void player_set_nation_full(struct player *pplayer,
                                   struct nation_type *pnation)
{
  /* Don't change the name of a created player. */
  player_nation_defaults(pplayer, pnation, pplayer->random_name);
}

/**********************************************************************//**
  Set nation for player with nation default values.
**************************************************************************/
void player_nation_defaults(struct player *pplayer, struct nation_type *pnation,
                            bool set_name)
{
  struct nation_leader *pleader;

  fc_assert(NO_NATION_SELECTED != pnation);
  player_set_nation(pplayer, pnation);
  fc_assert(pnation == pplayer->nation);

  pplayer->style = style_of_nation(pnation);

  if (set_name) {
    server_player_set_name(pplayer, pick_random_player_name(pnation));
  }

  if ((pleader = nation_leader_by_name(pnation, player_name(pplayer)))) {
    pplayer->is_male = nation_leader_is_male(pleader);
  } else {
    pplayer->is_male = (fc_rand(2) == 1);
  }

  ai_traits_init(pplayer);
}

/**********************************************************************//**
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
  positions which already-assigned players can't use. (Note that it's
  possible that we can't find enough nations with available start positions,
  depending on what nations players have already picked; in this case,
  it's OK to pick nations without start positions, as init_new_game() will
  fall back to mismatched start positions.)
 
  Otherwise, pick available nations using pick_a_nation(), which tries to
  pick nations that look good with nations already in the game.

  For 'aifill' players, the player name/sex is then reset to that of a
  random leader for the chosen nation.
**************************************************************************/
static void generate_players(void)
{
  int nations_to_assign = 0;

  /* Announce players who already have nations, and select nations based
   * on player names. */
  players_iterate(pplayer) {
    if (pplayer->nation != NO_NATION_SELECTED) {
      /* Traits are initialized here, and not already when nation gets picked,
       * as player may change his/her mind after picking one nation, and picks
       * another and we want to init traits only once, for the correct nation. */
      ai_traits_init(pplayer);
      announce_player(pplayer);
      continue;
    }

    /* See if the player name matches a known leader name.
     * If more than one nation has this leader name, pick one at random.
     * No attempt is made to avoid clashes to maximise the number of
     * nations that can be assigned in this way. */
    {
      struct nation_list *candidates = nation_list_new();
      int n = 0;

      allowed_nations_iterate(pnation) {
        if (is_nation_playable(pnation)
            && client_can_pick_nation(pnation)
            && NULL == pnation->player
            && (nation_leader_by_name(pnation, player_name(pplayer)))) {
          nation_list_append(candidates, pnation);
          n++;
        }
      } allowed_nations_iterate_end;
      if (n > 0) {
        player_set_nation_full(pplayer,
                               nation_list_get(candidates, fc_rand(n)));
      }
      nation_list_destroy(candidates);
    }
    if (pplayer->nation != NO_NATION_SELECTED) {
      announce_player(pplayer);
    } else {
      nations_to_assign++;
    }
  } players_iterate_end;

  if (0 < nations_to_assign && 0 < map_startpos_count()) {
    /* We're running a scenario game with specified start positions.
     * Prefer nations assigned to those positions (but we can fall back
     * to others, even if game.scenario.startpos_nations is set). */
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

      allowed_nations_iterate(pnation) {
        if (!is_nation_playable(pnation)
            || NULL != pnation->player) {
          /* Not available. */
          continue;
        }

        startpos_hash_iterate(hash, psp, val) {
          if (!startpos_nation_allowed(psp, pnation)) {
            continue;
          }

          if (val < min) {
            /* Pick this nation, as fewer nations already in the game
             * can use this start position. */
            picked = pnation;
            min = val;
            i = 1;
          } else if (val == min && 0 == fc_rand(++i)) {
            /* More than one nation is equally desirable. Pick one at
             * random. */
            picked = pnation;
          }
        } startpos_hash_iterate_end;
      } allowed_nations_iterate_end;

      if (NO_NATION_SELECTED != picked) {
        player_set_nation_full(pplayer, picked);
        nations_to_assign--;
        announce_player(pplayer);
        /* Update the counts for the newly assigned nation. */
        startpos_hash_iterate(hash, psp, val) {
          if (startpos_nation_allowed(psp, picked)) {
            startpos_hash_replace(hash, psp, val + 1);
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
    /* Pick random races. Try to select from the set permitted by
     * starting positions -- if we fell through here after failing to
     * match start positions, this will at least keep the picked
     * nations vaguely in keeping with the scenario.
     * However, even this may fail (if there are start positions that
     * can only be filled by nations outside the current nationset),
     * in which case we fall back to completely random nations. */
    bool needs_startpos = TRUE;
    players_iterate(pplayer) {
      if (NO_NATION_SELECTED == pplayer->nation) {
        struct nation_type *pnation = pick_a_nation(NULL, FALSE, needs_startpos,
                                                    NOT_A_BARBARIAN);
        if (pnation == NO_NATION_SELECTED && needs_startpos) {
          needs_startpos = FALSE;
          pnation = pick_a_nation(NULL, FALSE, needs_startpos, NOT_A_BARBARIAN);
        }
        fc_assert(pnation != NO_NATION_SELECTED);
        player_set_nation_full(pplayer, pnation);
        nations_to_assign--;
        announce_player(pplayer);
      }
    } players_iterate_end;
  }

  fc_assert(0 == nations_to_assign);

  (void) send_server_info_to_metaserver(META_INFO);
}

/**********************************************************************//**
  Returns a random ruler name picked from given nation's ruler names
  that is not already in use.
  May return NULL if no unique name is available.
**************************************************************************/
const char *pick_random_player_name(const struct nation_type *pnation)
{
  const char *choice = NULL;
  struct nation_leader_list *candidates = nation_leader_list_new();
  int n;

  nation_leader_list_iterate(nation_leaders(pnation), pleader) {
    const char *name = nation_leader_name(pleader);

    if (NULL == player_by_name(name)
        && NULL == player_by_user(name)) {
      nation_leader_list_append(candidates, pleader);
    }
  } nation_leader_list_iterate_end;

  n = nation_leader_list_size(candidates);
  if (n > 0) {
    choice = nation_leader_name(nation_leader_list_get(candidates,
                                                       fc_rand(n)));
  }
  nation_leader_list_destroy(candidates);

  return choice;
}

/**********************************************************************//**
  Announce what nation player rules to everyone.
**************************************************************************/
static void announce_player(struct player *pplayer)
{
   log_normal(_("%s rules the %s."),
              player_name(pplayer), nation_plural_for_player(pplayer));

  notify_conn(game.est_connections, NULL, E_GAME_START,
              ftc_server, _("%s rules the %s."),
              player_name(pplayer), nation_plural_for_player(pplayer));

  /* Let the clients knows the nation of the players as soon as possible.
   * When a player's nation is server assigned its client will think of it
   * as NULL until informed about the assigned nation. */
  send_player_info_c(pplayer, NULL);
}

/**********************************************************************//**
  Play the game! Returns when S_S_RUNNING != server_state().
**************************************************************************/
static void srv_running(void)
{
  int i;
  bool is_new_turn = game.info.is_new_game;
  bool skip_mapimg = !game.info.is_new_game; /* Do not overwrite start-of-turn image */
  bool need_send_pending_events = !game.info.is_new_game;
  int save_counter = game.info.is_new_game ? 1 : 0;

  /* We may as well reset is_new_game now. */
  game.info.is_new_game = FALSE;

  log_verbose("srv_running() mostly redundant send_server_settings()");
  send_server_settings(NULL);

  timer_start(eot_timer);

  if (game.server.autosaves & (1 << AS_TIMER)) {
    game.server.save_timer = timer_renew(game.server.save_timer,
                                         TIMER_USER, TIMER_ACTIVE);
    timer_start(game.server.save_timer);
  }

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

        if (!skip_mapimg) {
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
        } else {
          skip_mapimg = FALSE;
        }
      }

      log_debug("sniffingpackets");
      check_for_full_turn_done(); /* HACK: don't wait during AI phases */

      if (between_turns != NULL) {
        game.server.turn_change_time = timer_read_seconds(between_turns);
        log_debug("Inresponsive between turns %g seconds", game.server.turn_change_time);
      }

      while (server_sniff_all_input() == S_E_OTHERWISE) {
        /* nothing */
      }

      between_turns = timer_renew(between_turns, TIMER_USER, TIMER_ACTIVE);
      timer_start(between_turns);

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
      game.server.additional_phase_seconds = 0;
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
    } else if (S_S_OVER == server_state()) {
      /* game terminated by /endgame command - calculate team scores */
      rank_users(TRUE);
    }
  }

  /* This will thaw the reports and agents at the client.  */
  lsend_packet_thaw_client(game.est_connections);

  if (game.server.save_timer != NULL) {
    timer_destroy(game.server.save_timer);
    game.server.save_timer = NULL;
  }
  if (between_turns != NULL) {
    timer_destroy(between_turns);
    between_turns = NULL;
  }
  timer_clear(eot_timer);
}

/**********************************************************************//**
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

  server_open_socket();

#if IS_BETA_VERSION
  con_puts(C_COMMENT, "");
  con_puts(C_COMMENT, beta_message());
  con_puts(C_COMMENT, "");
#endif
  
  con_flush();

  settings_init(TRUE);
  stdinhand_init();
  edithand_init();
  voting_init();
  diplhand_init();
  voting_init();
  ai_timer_init();

  server_game_init(FALSE);
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

  if (srvarg.ruleset != NULL) {
    const char *testfilename;

    testfilename = fileinfoname(get_data_dirs(), srvarg.ruleset);
    if (testfilename == NULL) {
      log_fatal(_("Ruleset directory \"%s\" not found"), srvarg.ruleset);
      exit(EXIT_FAILURE);
    }
    sz_strlcpy(game.server.rulesetdir, srvarg.ruleset);
  }

  /* load a saved game */
  if ('\0' == srvarg.load_filename[0]
      || !load_command(NULL, srvarg.load_filename, FALSE, TRUE)) {
    /* Rulesets are loaded on game initialization, but may be changed later
     * if /load or /rulesetdir is done. */
    load_rulesets(NULL, NULL, FALSE, NULL, TRUE, FALSE, TRUE);
  }

  maybe_automatic_meta_message(default_meta_message_string());

  if (!(srvarg.metaserver_no_send)) {
    log_normal(_("Sending info to metaserver <%s>."), meta_addr_port());
    /* Open socket for meta server */
    if (!server_open_meta(srvarg.metaconnection_persistent)
        || !send_server_info_to_metaserver(META_INFO)) {
      con_write(C_FAIL, _("Not starting without explicitly requested metaserver connection."));
      exit(EXIT_FAILURE);
    }
  }

  eot_timer = timer_new(TIMER_CPU, TIMER_ACTIVE);
}

/**********************************************************************//**
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

/**********************************************************************//**
  Apply some final adjustments from the ruleset on to the game state.
  We cannot do this during ruleset loading, since some players may be
  added later than that.
**************************************************************************/
static void final_ruleset_adjustments(void)
{
  players_iterate(pplayer) {
    struct nation_type *pnation = nation_of_player(pplayer);

    pplayer->government = init_government_of_nation(pnation);

    if (pnation->init_government == game.government_during_revolution) {
      /* If we do not do this, an assertion will trigger. This enables us to
       * select a valid government on game start. */
      pplayer->revolution_finishes = 0;
    }

    multipliers_iterate(pmul) {
      pplayer->multipliers[multiplier_index(pmul)]
        = pplayer->multipliers_target[multiplier_index(pmul)]
          = pmul->def;
    } multipliers_iterate_end;
  } players_iterate_end;
}

/**********************************************************************//**
  Set up one game.
**************************************************************************/
static void srv_ready(void)
{
  (void) send_server_info_to_metaserver(META_INFO);

  if (game.server.auto_ai_toggle) {
    players_iterate(pplayer) {
      if (!pplayer->is_connected && is_human(pplayer)) {
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
    game.info.turn++; /* pregame T0 -> game T1 */
    fc_assert(game.info.turn == 1);
    game.info.year = game.server.start_year;
    /* Must come before assign_player_colors() */
    generate_players();
    final_ruleset_adjustments();
  }

  /* If we have a tile map, and MAPGEN_SCENARIO == map.server.generator,
   * call map_fractal_generate anyway to make the specials, huts and
   * continent numbers. */
  if (map_is_empty()
      || (MAPGEN_SCENARIO == wld.map.server.generator
          && game.info.is_new_game)) {
    struct {
      const char *name;
      char value[MAX_LEN_NAME * 2];
      char pretty[MAX_LEN_NAME * 2];
    } mapgen_settings[] = {
      { "generator", },
      { "startpos", },
      { "teamplacement", }
    };
    int i;
    /* If a specific seed has been requested, there's no point retrying,
     * as the map will be the same every time. */
    bool retry_ok = (wld.map.server.seed_setting == 0
                     && wld.map.server.generator != MAPGEN_SCENARIO);
    int max = retry_ok ? 3 : 1;
    bool created = FALSE;
    struct unit_type *utype = NULL;
    int sucount = strlen(game.server.start_units);

    if (sucount > 0) {
      for (i = 0; utype == NULL && i < sucount; i++) {
        utype = crole_to_unit_type(game.server.start_units[i], NULL);
      }
    } else {
      /* First unit the initial city might build. */
      utype = get_role_unit(L_FIRSTBUILD, 0);
    }
    fc_assert(utype != NULL);

    /* Register map generator setting main values. */
    for (i = 0; i < ARRAY_SIZE(mapgen_settings); i++) {
      const struct setting *pset = setting_by_name(mapgen_settings[i].name);

      fc_assert_action(pset != NULL, continue);
      (void) setting_value_name(pset, FALSE,
                                mapgen_settings[i].value,
                                sizeof(mapgen_settings[i].value));
      (void) setting_value_name(pset, TRUE,
                                mapgen_settings[i].pretty,
                                sizeof(mapgen_settings[i].pretty));
    }

    for (i = 0; !created && i < max ; i++) {
      created = map_fractal_generate(TRUE, utype);
      if (!created && max > 1) {
        int set;

        /* If we're retrying, seed_setting == 0, which will yield a new map
         * next time */
        fc_assert(wld.map.server.seed_setting == 0);
        if (i == 0) {
          /* We will retry only if max attempts allow it */
          log_normal(_("Failed to create suitable map, retrying with another mapseed."));
        } else {
          /* +1 - start human readable count from 1 and not from 0
           * +1 - refers to next round, not to one we just did
           * ==
           * +2 */
          log_normal(_("Attempt %d/%d"), i + 2, max);
        }
        wld.map.server.have_resources = FALSE;

        /* Remove old information already present in tiles */
        main_map_free();
        free_city_map_index();
        /* Restore the settings. */
        for (set = 0; set < ARRAY_SIZE(mapgen_settings); set++) {
          struct setting *pset = setting_by_name(mapgen_settings[set].name);
#ifdef FREECIV_NDEBUG
          setting_enum_set(pset, mapgen_settings[set].value, NULL, NULL, 0);
#else  /* FREECIV_NDEBUG */
          char error[128];
          bool success;

          fc_assert_action(pset != NULL, continue);
          success = setting_enum_set(pset, mapgen_settings[set].value,
                                     NULL, error, sizeof(error));
          fc_assert_msg(success == TRUE,
                        "Failed to restore '%s': %s",
                        mapgen_settings[set].name,
                        error);
#endif /* FREECIV_NDEBUG */
        }
        main_map_allocate(); /* NOT map_init() as that would overwrite settings */
      }
    }
    if (!created) {
      bugreport_request(_("Cannot create suitable map with given settings."));

      exit(EXIT_FAILURE);
    }

    if (wld.map.server.generator != MAPGEN_SCENARIO) {
      script_server_signal_emit("map_generated");
    }

    game_map_init();

    /* Test if main map generator settings have changed. */
    for (i = 0; i < ARRAY_SIZE(mapgen_settings); i++) {
      const struct setting *pset = setting_by_name(mapgen_settings[i].name);
      char pretty[sizeof(mapgen_settings[i].pretty)];

      fc_assert_action(pset != NULL, continue);
      if (0 == strcmp(setting_value_name(pset, TRUE, pretty,
                                         sizeof(pretty)),
                      mapgen_settings[i].pretty)) {
        continue; /* Setting didn't change. */
      }
      notify_conn(NULL, NULL, E_SETTING, ftc_server,
                  _("Setting '%s' has been adjusted from %s to %s."),
                  setting_name(pset),
                  mapgen_settings[i].pretty,
                  pretty);
      log_normal(_("Setting '%s' has been adjusted from %s to %s."),
                 setting_name(pset),
                 mapgen_settings[i].pretty,
                 pretty);
    }
  }

  CALL_FUNC_EACH_AI(map_ready);

  /* start the game */
  set_server_state(S_S_RUNNING);
  (void) send_server_info_to_metaserver(META_INFO);

  if (game.info.is_new_game) {
    shuffle_players();

    /* If we're starting a new game, reset the max_players to be at
     * least the number of players currently in the game. */
    game.server.max_players = MAX(normal_player_count(), game.server.max_players);

    /* Before the player map is allocated (and initialized)! */
    game.server.fogofwar_old = game.info.fogofwar;

    players_iterate(pplayer) {
      player_map_init(pplayer);
      pplayer->economic = player_limit_to_max_rates(pplayer);
      pplayer->economic.gold = game.info.gold;
      pplayer->economic.infra_points = game.info.infrapoints;
    } players_iterate_end;

    /* Give initial technologies, as specified in the ruleset and the
     * settings. */
    researches_iterate(presearch) {
      init_tech(presearch, TRUE);
      give_initial_techs(presearch, game.info.tech);
    } researches_iterate_end;

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
      if (is_ai(pplayer)) {
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
    /* Place players' initial units, etc */
    init_new_game();
    create_animals();

    if (game.server.revealmap & REVEAL_MAP_START) {
      players_iterate(pplayer) {
        map_show_all(pplayer);
      } players_iterate_end;
    }
  }

  if (game.scenario.is_scenario && game.scenario.players) {
    /* This is a heavy scenario. It may include research. The sciencebox
     * setting may have been changed. A change to the sciencebox setting
     * may have caused the stored amount of bulbs to be enough to finish
     * the current research. */

    players_iterate(pplayer) {
      /* Check for finished research. */
      update_bulbs(pplayer, 0, TRUE);
    } players_iterate_end;
  }

  CALL_FUNC_EACH_AI(game_start);
}

/**********************************************************************//**
  Initialize game data for the server (corresponds to server_game_free).
**************************************************************************/
void server_game_init(bool keep_ruleset_value)
{
  /* was redundantly in game_load() */
  server.playable_nations = 0;
  server.nbarbarians = 0;
  server.identity_number = IDENTITY_NUMBER_SKIP;

  BV_CLR_ALL(identity_numbers_used);
  identity_number_reserve(IDENTITY_NUMBER_ZERO);

  event_cache_init();
  game_init(keep_ruleset_value);
  /* game_init() set game.server.plr_colors to NULL. So we need to
   * initialize the colors after. */
  playercolor_init();

  game.server.turn_change_time = 0;
}

/**********************************************************************//**
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

/**********************************************************************//**
  Server main loop.
**************************************************************************/
void srv_main(void)
{
  fc_interface_init_server();
  advisors_init();

  srv_prepare();

  /* Run server loop */
  do {
    set_server_state(S_S_INITIAL);

    /* Load a script file. */
    if (NULL != srvarg.script_filename) {
      /* Adding an error message more here will duplicate them. */
      (void) read_init_script(NULL, srvarg.script_filename, TRUE, FALSE);
    }

    (void) aifill(game.info.aifill);
    if (!game_was_started()) {
      event_cache_clear();
    }

    log_normal(_("Now accepting new client connections on port %d."),
               srvarg.port);
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

    /* Close it even between games. */
    save_system_close();

    if (game.info.timeout == -1 || srvarg.exit_on_end) {
      /* For autogames or if the -e option is specified, exit the server. */
      server_quit();
    }

    /* Reset server */
    server_game_free();
    fc_rand_uninit();
    server_game_init(FALSE);
    mapimg_reset();
    load_rulesets(NULL, NULL, FALSE, NULL, TRUE, FALSE, TRUE);
    game.info.is_new_game = TRUE;
  } while (TRUE);

  /* Technically, we won't ever get here. We exit via server_quit. */
}

/**********************************************************************//**
  Initialize client specific functions.
**************************************************************************/
struct color;
static inline void server_gui_color_free(struct color *pcolor)
{
  fc_assert_ret(pcolor == NULL);

  return;
}

/**********************************************************************//**
  Returns the id of the city the player map of 'pplayer' has at 'ptile' or
  IDENTITY_NUMBER_ZERO if the player map don't have a city there.
**************************************************************************/
static int server_plr_tile_city_id_get(const struct tile *ptile,
                                       const struct player *pplayer)
{
  const struct player_tile *plrtile = map_get_player_tile(ptile, pplayer);

  return plrtile && plrtile->site ? plrtile->site->identity
                                  : IDENTITY_NUMBER_ZERO;
}

/**********************************************************************//**
  Returns the id of the server setting with the specified name.
**************************************************************************/
server_setting_id server_ss_by_name(const char *name)
{
  struct setting *pset = setting_by_name(name);

  if (pset) {
    return setting_number(pset);
  } else {
    log_error("No server setting named %s exists.", name);
    return SERVER_SETTING_NONE;
  }
}

/**********************************************************************//**
  Returns the name of the server setting with the specified id.
**************************************************************************/
const char *server_ss_name_get(server_setting_id id)
{
  struct setting *pset = setting_by_number(id);

  if (pset) {
    return setting_name(pset);
  } else {
    log_error("No server setting with the id %d exists.", id);
    return NULL;
  }
}

/**********************************************************************//**
  Returns the type of the server setting with the specified id.
**************************************************************************/
enum sset_type server_ss_type_get(server_setting_id id)
{
  struct setting *pset = setting_by_number(id);

  if (pset) {
    return setting_type(pset);
  } else {
    log_error("No server setting with the id %d exists.", id);
    return sset_type_invalid();
  }
}

/**********************************************************************//**
  Returns the value of the boolean server setting with the specified id.
**************************************************************************/
bool server_ss_val_bool_get(server_setting_id id)
{
  struct setting *pset = setting_by_number(id);

  if (pset) {
    return setting_bool_get(pset);
  } else {
    log_error("No server setting with the id %d exists.", id);
    return FALSE;
  }
}

/**********************************************************************//**
  Initialize server specific functions.
**************************************************************************/
static void fc_interface_init_server(void)
{
  struct functions *funcs = fc_interface_funcs();

  funcs->server_setting_by_name = server_ss_by_name;
  funcs->server_setting_name_get = server_ss_name_get;
  funcs->server_setting_type_get = server_ss_type_get;
  funcs->server_setting_val_bool_get = server_ss_val_bool_get;
  funcs->create_extra = create_extra;
  funcs->destroy_extra = destroy_extra;
  funcs->player_tile_vision_get = map_is_known_and_seen;
  funcs->player_tile_city_id_get = server_plr_tile_city_id_get;
  funcs->gui_color_free = server_gui_color_free;

  /* Keep this function call at the end. It checks if all required functions
     are defined. */
  fc_interface_init();
}

/**********************************************************************//**
  Helper function for the mapimg module - tile knowledge.
**************************************************************************/
static enum known_type mapimg_server_tile_known(const struct tile *ptile,
                                                const struct player *pplayer,
                                                bool knowledge)
{
  if (knowledge && pplayer) {
    return tile_get_known(ptile, pplayer);
  }

  return TILE_KNOWN_SEEN;
}

/**********************************************************************//**
  Helper function for the mapimg module - tile terrain.
**************************************************************************/
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

/**********************************************************************//**
  Helper function for the mapimg module - tile owner.
**************************************************************************/
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

/**********************************************************************//**
  Helper function for the mapimg module - city owner.
**************************************************************************/
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

/**********************************************************************//**
  Helper function for the mapimg module - unit owner.
**************************************************************************/
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

/**********************************************************************//**
  Helper function for the mapimg module - number of player colors.
**************************************************************************/
static int mapimg_server_plrcolor_count(void)
{
  return playercolor_count();
}

/**********************************************************************//**
  Helper function for the mapimg module - one player color.
**************************************************************************/
static struct rgbcolor *mapimg_server_plrcolor_get(int i)
{
  return playercolor_get(i);
}
