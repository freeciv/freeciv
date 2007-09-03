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

#include "capability.h"
#include "capstr.h"
#include "city.h"
#include "dataio.h"
#include "effects.h"
#include "events.h"
#include "game.h"
#include "government.h"
#include "map.h"
#include "nation.h"
#include "packets.h"
#include "player.h"
#include "tech.h"
#include "unitlist.h"
#include "version.h"

#include "auth.h"
#include "barbarian.h"
#include "cityhand.h"
#include "citytools.h"
#include "cityturn.h"
#include "connecthand.h"
#include "console.h"
#include "diplhand.h"
#include "gamehand.h"
#include "ggzserver.h"
#include "handchat.h"
#include "maphand.h"
#include "meta.h"
#include "plrhand.h"
#include "report.h"
#include "ruleset.h"
#include "sanitycheck.h"
#include "savegame.h"
#include "score.h"
#include "script_signal.h"
#include "sernet.h"
#include "settlers.h"
#include "spacerace.h"
#include "srv_main.h"
#include "stdinhand.h"
#include "techtools.h"
#include "unithand.h"
#include "unittools.h"

#include "advdiplomacy.h"
#include "advmilitary.h"
#include "aicity.h"
#include "aidata.h"
#include "aihand.h"
#include "aisettler.h"
#include "citymap.h"

#include "mapgen.h"

static void end_turn(void);
static void announce_player(struct player *pplayer);
static void srv_loop(void);

/* command-line arguments to server */
struct server_arguments srvarg;

/* server state information */
enum server_states server_state = PRE_GAME_STATE;
bool nocity_send = FALSE;

/* this global is checked deep down the netcode. 
   packets handling functions can set it to none-zero, to
   force end-of-tick asap
*/
bool force_end_of_sniff;

/* this counter creates all the id numbers used */
/* use get_next_id_number()                     */
static unsigned short global_id_counter=100;
static unsigned char used_ids[8192]={0};

/* server initialized flag */
static bool has_been_srv_init = FALSE;

/**************************************************************************
  Initialize the game seed.  This may safely be called multiple times.
**************************************************************************/
void init_game_seed(void)
{
  if (game.seed == 0) {
    /* We strip the high bit for now because neither game file nor
       server options can handle unsigned ints yet. - Cedric */
    game.seed = time(NULL) & (MAX_UINT32 >> 1);
    freelog(LOG_DEBUG, "Setting game.seed:%d", game.seed);
  }
 
  if (!myrand_is_init()) {
    mysrand(game.seed);
  }
}

/**************************************************************************
...
**************************************************************************/
void srv_init(void)
{
  i_am_server(); /* Tell to libcivcommon that we are server */

  /* NLS init */
  init_nls();

  /* init server arguments... */

  srvarg.metaserver_no_send = DEFAULT_META_SERVER_NO_SEND;
  sz_strlcpy(srvarg.metaserver_addr, DEFAULT_META_SERVER_ADDR);

  srvarg.bind_addr = NULL;
  srvarg.port = DEFAULT_SOCK_PORT;

  srvarg.loglevel = LOG_NORMAL;

  srvarg.log_filename = NULL;
  srvarg.ranklog_filename = NULL;
  srvarg.load_filename[0] = '\0';
  srvarg.script_filename = NULL;
  srvarg.saves_pathname = "";

  srvarg.quitidle = 0;

  srvarg.auth_enabled = FALSE;
  srvarg.auth_allow_guests = FALSE;
  srvarg.auth_allow_newusers = FALSE;

  srvarg.save_ppm = FALSE;

  /* mark as initialized */
  has_been_srv_init = TRUE;

  /* init character encodings. */
  init_character_encodings(FC_DEFAULT_DATA_ENCODING, FALSE);

  /* Initialize callbacks. */
  game.callbacks.unit_deallocate = dealloc_id;

  /* done */
  return;
}

/**************************************************************************
  Returns TRUE if any one game end condition is fulfilled, FALSE otherwise.

  This function will notify players but will not set the server_state. The
  caller must set the server state to GAME_OVER_STATE if the function
  returns TRUE.
**************************************************************************/
bool check_for_game_over(void)
{
  int barbs = 0, alive = 0;
  struct player *victor = NULL, *spacer = NULL;

  /* quit if we are past the year limit */
  if (game.info.year > game.info.end_year) {
    notify_conn(game.est_connections, NULL, E_GAME_END, 
		_("Game ended in a draw as end year exceeded"));
    ggz_report_victory();
    return TRUE;
  }

  /* count barbarians and observers */
  players_iterate(pplayer) {
    if (is_barbarian(pplayer)) {
      barbs++;
    }
  } players_iterate_end;

  /* count the living */
  players_iterate(pplayer) {
    if (pplayer->is_alive
        && !is_barbarian(pplayer)
        && !pplayer->surrendered) {
      alive++;
      victor = pplayer;
    }
  } players_iterate_end;

  /* the game does not quit if we are playing solo */
  if (game.info.nplayers == (barbs + 1)
      && alive >= 1) {
    return FALSE;
  }

  /* check for a spacerace win */
  if ((spacer = check_spaceship_arrival())) {
    bool loner = TRUE;
    victor = spacer;
 
    notify_player(NULL, NULL, E_SPACESHIP,
                  _("The %s spaceship has arrived at Alpha Centauri."),
                  nation_name_for_player(victor));

    /* this guy has won, now check if anybody else wins with him */
    players_iterate(pplayer) {
      if (pplayer->team == victor->team && pplayer != victor) {
        loner = FALSE;
        break;
      }
    } players_iterate_end;

    if (!loner) {
      notify_conn(NULL, NULL, E_GAME_END,
                  _("Team victory to %s"), team_name_translation(victor->team));
      players_iterate(pplayer) {
	if (pplayer->team == victor->team) {
	  ggz_report_victor(pplayer);
	}
      } players_iterate_end;
      ggz_report_victory();
    } else {
      notify_conn(NULL, NULL, E_GAME_END,
                  _("Game ended in victory for %s"), victor->name);
      ggz_report_victor(victor);
      ggz_report_victory();
    }

    return TRUE;
  }

  /* quit if we have team victory */
  team_iterate(pteam) {
    bool win = TRUE; /* optimistic */

    /* If there are any players alive and unconceded outside our
     * team, we have not yet won. */
    players_iterate(pplayer) {
      if (pplayer->is_alive
          && !pplayer->surrendered
          && pplayer->team != pteam) {
        win = FALSE;
        break;
      }
    } players_iterate_end;
    if (win) {
      notify_conn(game.est_connections, NULL, E_GAME_END,
		     _("Team victory to %s"), team_name_translation(pteam));
      players_iterate(pplayer) {
	if (pplayer->is_alive
	    && !pplayer->surrendered) {
	  ggz_report_victor(pplayer);
	}
      } players_iterate_end;
      ggz_report_victory();
      return TRUE;
    }
  } team_iterate_end;

  /* quit if only one player is left alive */
  if (alive == 1) {
    notify_conn(game.est_connections, NULL, E_GAME_END,
		   _("Game ended in victory for %s"), victor->name);
    ggz_report_victor(victor);
    ggz_report_victory();
    return TRUE;
  } else if (alive == 0) {
    notify_conn(game.est_connections, NULL, E_GAME_END, 
		   _("Game ended in a draw"));
    ggz_report_victory();
    return TRUE;
  }

  return FALSE;
}

/**************************************************************************
  Send all information for when game starts or client reconnects.
  Ruleset information should have been sent before this.
**************************************************************************/
void send_all_info(struct conn_list *dest)
{
  conn_list_iterate(dest, pconn) {
      send_attribute_block(pconn->player,pconn);
  }
  conn_list_iterate_end;

  send_game_info(dest);
  send_map_info(dest);
  send_player_info_c(NULL, dest);
  send_conn_info(game.est_connections, dest);
  send_spaceship_info(NULL, dest);
  send_all_known_tiles(dest);
  send_all_known_cities(dest);
  send_all_known_units(dest);
  send_player_turn_notifications(dest);
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
...
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
    if (myrand((map_num_tiles() + 19) / 20) <= *accum) {
      upset_action_fn((map.xsize / 10) + (map.ysize / 10) + ((*accum) * 5));
      *accum = 0;
      *level += (map_num_tiles() + 999) / 1000;
    }
  }

  freelog(LOG_DEBUG,
	  "environmental_upset: cause=%-4d current=%-2d level=%-2d accum=%-2d",
	  cause, *current, *level, *accum);
}

/**************************************************************************
  Remove illegal units when armistice turns into peace treaty.
**************************************************************************/
static void remove_illegal_armistice_units(struct player *plr1,
                                           struct player *plr2)
{
  /* Remove illegal units */
  unit_list_iterate_safe(plr1->units, punit) {
    if (punit->tile->owner == plr2
        && is_military_unit(punit)) {
      notify_player(plr1, NULL, E_DIPLOMACY, _("Your %s unit %s was "
                    "disbanded in accordance with your peace treaty with "
                    "the %s."),
                    unit_name_translation(punit),
                    get_location_str_at(plr1, punit->tile),
                    nation_plural_for_player(plr2));
      wipe_unit(punit);
    }
  } unit_list_iterate_safe_end;
  unit_list_iterate_safe(plr2->units, punit) {
    if (punit->tile->owner == plr1
        && is_military_unit(punit)) {
      notify_player(plr2, NULL, E_DIPLOMACY, _("Your %s unit %s was "
                    "disbanded in accordance with your peace treaty with "
                    "the %s."),
                    unit_name_translation(punit),
                    get_location_str_at(plr2, punit->tile),
                    nation_plural_for_player(plr1));
      wipe_unit(punit);
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
      struct player_diplstate *state = &plr1->diplstates[player_index(plr2)];
      struct player_diplstate *state2 = &plr2->diplstates[player_index(plr1)];

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
          notify_player(plr1, NULL, E_DIPLOMACY,
                        _("Concerned citizens point out that the cease-fire "
                          "with %s will run out soon."), plr2->name);
          notify_player(plr2, NULL, E_DIPLOMACY,
                        _("Concerned citizens point out that the cease-fire "
                          "with %s will run out soon."), plr1->name);
          break;
        case 0:
          notify_player(plr1, NULL, E_DIPLOMACY, _("The cease-fire with "
                        "%s has run out. You are now at war with the %s."),
                        plr2->name,
                        nation_plural_for_player(plr2));
          notify_player(plr2, NULL, E_DIPLOMACY, _("The cease-fire with "
                        "%s has run out. You are now at war with the %s."),
                        plr1->name,
                        nation_plural_for_player(plr1));
          state->type = DS_WAR;
          state2->type = DS_WAR;
          state->turns_left = 0;
          state2->turns_left = 0;
          check_city_workers(plr1);
          check_city_workers(plr2);

          /* Avoid love-love-hate triangles */
          players_iterate(plr3) {
            if (plr3->is_alive && plr3 != plr1 && plr3 != plr2
                && pplayers_allied(plr3, plr1)
                && pplayers_allied(plr3, plr2)) {
              notify_player(plr3, NULL, E_TREATY_BROKEN,
                            _("Ceasefire between %s and %s has run out. "
                              "They are at war. You cancel your alliance "
                              "with both."), plr1->name, plr2->name);
              plr3->diplstates[player_index(plr1)].has_reason_to_cancel = TRUE;
              plr3->diplstates[player_index(plr2)].has_reason_to_cancel = TRUE;
              handle_diplomacy_cancel_pact(plr3, player_number(plr1), CLAUSE_ALLIANCE);
              handle_diplomacy_cancel_pact(plr3, player_number(plr2), CLAUSE_ALLIANCE);
            }
          } players_iterate_end;
          break;
        }
      }
    } players_iterate_end;
  } players_iterate_end;
}

/**************************************************************************
  Called at the start of each (new) phase to do AI activities.
**************************************************************************/
static void ai_start_phase(void)
{
  phase_players_iterate(pplayer) {
    if (pplayer->ai.control) {
      ai_do_first_activities(pplayer);
      flush_packets();			/* AIs can be such spammers... */
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
  freelog(LOG_DEBUG, "Begin turn");

  /* Reset this each turn. */
  if (is_new_turn) {
    game.info.simultaneous_phases = game.simultaneous_phases_stored;
  }
  if (game.info.simultaneous_phases) {
    game.info.num_phases = 1;
  } else {
    game.info.num_phases = game.info.nplayers;
  }
  send_game_info(NULL);

  if (is_new_turn) {
    script_signal_emit("turn_started", 2,
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
  }

  /* find out if users attached to players have been attached to those players
   * for long enough. The first user to do so becomes "associated" to that
   * player for ranking purposes. */
  players_iterate(pplayer) {
    if (strcmp(pplayer->ranked_username, ANON_USER_NAME) == 0
        && pplayer->user_turns++ > TURNS_NEEDED_TO_RANK) {
      sz_strlcpy(pplayer->ranked_username, pplayer->username);
    }
  } players_iterate_end;

  /* See if the value of fog of war has changed */
  if (is_new_turn && game.info.fogofwar != game.fogofwar_old) {
    if (game.info.fogofwar) {
      enable_fog_of_war();
      game.fogofwar_old = TRUE;
    } else {
      disable_fog_of_war();
      game.fogofwar_old = FALSE;
    }
  }

  if (is_new_turn && game.info.simultaneous_phases) {
    freelog(LOG_DEBUG, "Shuffleplayers");
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
  freelog(LOG_DEBUG, "Begin phase");

  conn_list_do_buffer(game.est_connections);

  phase_players_iterate(pplayer) {
    pplayer->phase_done = FALSE;
  } phase_players_iterate_end;
  send_player_info(NULL, NULL);

  send_start_phase_to_clients();

  if (is_new_phase) {
    /* Unit "end of turn" activities - of course these actually go at
     * the start of the turn! */
    phase_players_iterate(pplayer) {
      update_unit_activities(pplayer); /* major network traffic */
      flush_packets();
    } phase_players_iterate_end;
  }

  phase_players_iterate(pplayer) {
    freelog(LOG_DEBUG, "beginning player turn for #%d (%s)",
	    player_number(pplayer), pplayer->name);
    /* human players also need this for building advice */
    ai_data_phase_init(pplayer, is_new_phase);
    if (!pplayer->ai.control) {
      ai_manage_buildings(pplayer); /* building advisor */
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
      if (pplayer->ai.control && !is_barbarian(pplayer)) {
	ai_diplomacy_actions(pplayer);
      }
    } phase_players_iterate_end;

    freelog(LOG_DEBUG, "Aistartturn");
    ai_start_phase();
  }

  sanity_check();

  game.info.seconds_to_phasedone = (double)game.info.timeout;
  game.phase_timer = renew_timer_start(game.phase_timer,
				       TIMER_USER, TIMER_ACTIVE);
  send_game_info(NULL);

  if (game.info.num_phases == 1) {
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
  freelog(LOG_DEBUG, "Endphase");
 
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
    if (get_player_research(pplayer)->researching == A_UNSET) {
      if (choose_goal_tech(pplayer) == A_UNSET) {
        choose_random_tech(pplayer);
      }
      update_tech(pplayer, 0);
    }
  } phase_players_iterate_end;

  /* Freeze sending of cities. */
  nocity_send = TRUE;

  /* AI end of turn activities */
  players_iterate(pplayer) {
    unit_list_iterate(pplayer->units, punit) {
      punit->ai.hunted = 0;
    } unit_list_iterate_end;
  } players_iterate_end;
  phase_players_iterate(pplayer) {
    if (pplayer->ai.control) {
      ai_settler_init(pplayer);
    }
    auto_settlers_player(pplayer);
    if (pplayer->ai.control) {
      ai_do_last_activities(pplayer);
    }
  } phase_players_iterate_end;

  /* Refresh cities */
  phase_players_iterate(pplayer) {
    get_player_research(pplayer)->got_tech = FALSE;
  } phase_players_iterate_end;
  
  phase_players_iterate(pplayer) {
    do_tech_parasite_effect(pplayer);
    player_restore_units(pplayer);
    update_city_activities(pplayer);
    get_player_research(pplayer)->researching_saved = A_UNKNOWN;
    flush_packets();
  } phase_players_iterate_end;

  kill_dying_players();

  /* Unfreeze sending of cities. */
  nocity_send = FALSE;
  phase_players_iterate(pplayer) {
    send_player_cities(pplayer);
  } phase_players_iterate_end;
  flush_packets();  /* to curb major city spam */

  do_reveal_effects();
  do_have_embassies_effect();
}

/**************************************************************************
  Handle the end of each turn.
**************************************************************************/
static void end_turn(void)
{
  int food = 0, shields = 0, trade = 0, settlers = 0;

  freelog(LOG_DEBUG, "Endturn");

  /* Hack: because observer players never get an end-phase packet we send
   * one here. */
  conn_list_iterate(game.est_connections, pconn) {
    if (!pconn->player) {
      send_packet_end_phase(pconn);
    }
  } conn_list_iterate_end;

  lsend_packet_end_turn(game.est_connections);

  map_calculate_borders();

  /* Output some AI measurement information */
  players_iterate(pplayer) {
    if (!pplayer->ai.control || is_barbarian(pplayer)) {
      continue;
    }
    unit_list_iterate(pplayer->units, punit) {
      if (unit_has_type_flag(punit, F_CITIES)) {
        settlers++;
      }
    } unit_list_iterate_end;
    city_list_iterate(pplayer->cities, pcity) {
      shields += pcity->prod[O_SHIELD];
      food += pcity->prod[O_FOOD];
      trade += pcity->prod[O_TRADE];
    } city_list_iterate_end;
    freelog(LOG_DEBUG, "%s T%d cities:%d pop:%d food:%d prod:%d "
            "trade:%d settlers:%d units:%d", pplayer->name, game.info.turn,
            city_list_size(pplayer->cities), total_player_citizens(pplayer),
            food, shields, trade, settlers, unit_list_size(pplayer->units));
  } players_iterate_end;

  freelog(LOG_DEBUG, "Season of native unrests");
  summon_barbarians(); /* wild guess really, no idea where to put it, but
			  I want to give them chance to move their units */

  update_environmental_upset(S_POLLUTION, &game.info.heating,
			     &game.info.globalwarming, &game.info.warminglevel,
			     global_warming);
  update_environmental_upset(S_FALLOUT, &game.info.cooling,
			     &game.info.nuclearwinter, &game.info.coolinglevel,
			     nuclear_winter);
  update_diplomatics();
  make_history_report();
  stdinhand_turn();
  send_player_turn_notifications(NULL);

  freelog(LOG_DEBUG, "Gamenextyear");
  game_advance_year();

  freelog(LOG_DEBUG, "Updatetimeout");
  update_timeout();

  freelog(LOG_DEBUG, "Sendplayerinfo");
  send_player_info(NULL, NULL);

  freelog(LOG_DEBUG, "Sendgameinfo");
  send_game_info(NULL);

  freelog(LOG_DEBUG, "Sendyeartoclients");
  send_year_to_clients(game.info.year);
}

/**************************************************************************
Unconditionally save the game, with specified filename.
Always prints a message: either save ok, or failed.

Note that if !HAVE_LIBZ, then game.info.save_compress_level should never
become non-zero, so no need to check HAVE_LIBZ explicitly here as well.
**************************************************************************/
void save_game(char *orig_filename, const char *save_reason)
{
  char filename[600];
  char *dot;
  struct section_file file;
  struct timer *timer_cpu, *timer_user;

  if (!orig_filename) {
    filename[0] = '\0';
  } else {
    sz_strlcpy(filename, orig_filename);
  }

  /* Strip extension. */
  if ((dot = strchr(filename, '.'))) {
    *dot = '\0';
  }

  /* If orig_filename is NULL or empty, use "civgame.info.year>m". */
  if (filename[0] == '\0'){
    my_snprintf(filename, sizeof(filename),
	"%s%+05dm", game.save_name, game.info.year);
  }
  
  timer_cpu = new_timer_start(TIMER_CPU, TIMER_ACTIVE);
  timer_user = new_timer_start(TIMER_USER, TIMER_ACTIVE);
    
  section_file_init(&file);
  game_save(&file, save_reason);

  /* Append ".sav" to filename. */
  sz_strlcat(filename, ".sav");

  if (game.info.save_compress_level > 0) {
    switch (game.info.save_compress_type) {
#ifdef HAVE_LIBZ
    case FZ_ZLIB:
      /* Append ".gz" to filename. */
      sz_strlcat(filename, ".gz");
      break;
#endif
#ifdef HAVE_LIBBZ2
    case FZ_BZIP2:
      /* Append ".bz2" to filename. */
      sz_strlcat(filename, ".bz2");
      break;
#endif
    case FZ_PLAIN:
      break;
    default:
      freelog(LOG_NORMAL, "Unsupported compression type %d",
              game.info.save_compress_type);
      break;
    }
  }

  if (!path_is_absolute(filename)) {
    char tmpname[600];

    /* Ensure the saves directory exists. */
    make_dir(srvarg.saves_pathname);

    sz_strlcpy(tmpname, srvarg.saves_pathname);
    if (tmpname[0] != '\0') {
      sz_strlcat(tmpname, "/");
    }
    sz_strlcat(tmpname, filename);
    sz_strlcpy(filename, tmpname);
  }

  if (!section_file_save(&file, filename, game.info.save_compress_level,
                         game.info.save_compress_type))
    con_write(C_FAIL, _("Failed saving game as %s"), filename);
  else
    con_write(C_OK, _("Game saved as %s"), filename);

  section_file_free(&file);

  freelog(LOG_VERBOSE, "Save time: %g seconds (%g apparent)",
	  read_timer_seconds_free(timer_cpu),
	  read_timer_seconds_free(timer_user));
}

/**************************************************************************
Save game with autosave filename
**************************************************************************/
void save_game_auto(const char *save_reason)
{
  char filename[512];

  assert(strlen(game.save_name)<256);
  
  my_snprintf(filename, sizeof(filename),
	      "%s%+05d.sav", game.save_name, game.info.year);
  save_game(filename, save_reason);
  save_ppm();
}

/**************************************************************************
...
**************************************************************************/
void start_game(void)
{
  if(server_state!=PRE_GAME_STATE) {
    con_puts(C_SYNTAX, _("The game is already running."));
    return;
  }

  /* Remove ALLOW_CTRL from whoever has it (gotten from 'first'). */
  conn_list_iterate(game.est_connections, pconn) {
    if (pconn->access_level == ALLOW_CTRL) {
      notify_conn(NULL, NULL, E_SETTING,
		  _("%s lost control cmdlevel on "
		    "game start.  Use voting from now on."),
		  pconn->username);
      pconn->access_level = ALLOW_INFO;
    }
  } conn_list_iterate_end;

  con_puts(C_OK, _("Starting game."));

  server_state = RUN_GAME_STATE; /* loaded ??? */
  force_end_of_sniff = TRUE;
  send_server_settings(NULL);
}

/**************************************************************************
 Quit the server and exit.
**************************************************************************/
void server_quit(void)
{
  server_state = GAME_OVER_STATE;
  server_game_free();
  diplhand_free();
  stdinhand_free();
  close_connections_and_socket();
  exit(EXIT_SUCCESS);
}

/**************************************************************************
...
**************************************************************************/
void handle_report_req(struct connection *pconn, enum report_type type)
{
  struct conn_list *dest = pconn->self;
  
  if (server_state != RUN_GAME_STATE && server_state != GAME_OVER_STATE) {
    freelog(LOG_ERROR, "Got a report request %d before game start", type);
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

  notify_conn(dest, NULL, E_BAD_COMMAND,
	      _("request for unknown report (type %d)"), type);
}

/**************************************************************************
...
**************************************************************************/
void dealloc_id(int id)
{
  used_ids[id/8]&= 0xff ^ (1<<(id%8));
}

/**************************************************************************
...
**************************************************************************/
static bool is_id_allocated(int id)
{
  return TEST_BIT(used_ids[id / 8], id % 8);
}

/**************************************************************************
...
**************************************************************************/
void alloc_id(int id)
{
  used_ids[id/8]|= (1<<(id%8));
}

/**************************************************************************
...
**************************************************************************/

int get_next_id_number(void)
{
  while (is_id_allocated(++global_id_counter) || global_id_counter == 0) {
    /* nothing */
  }
  return global_id_counter;
}

/**************************************************************************
Returns 0 if connection should be closed (because the clients was
rejected). Returns 1 else.
**************************************************************************/
bool handle_packet_input(struct connection *pconn, void *packet, int type)
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

    freelog(LOG_ERROR,
	    _("Warning: rejecting old client %s"), conn_description(pconn));

    dio_output_init(&dout, buffer, sizeof(buffer));
    dio_put_uint16(&dout, 0);

    /* 1 == PACKET_LOGIN_REPLY in the old client */
    dio_put_uint8(&dout, 1);

    dio_put_bool32(&dout, FALSE);
    dio_put_string(&dout, _("Your client is too old. To use this server "
			    "please upgrade your client to a "
			    "Freeciv 2.0 or later."));
    dio_put_string(&dout, "");

    {
      size_t size = dio_output_used(&dout);
      dio_output_rewind(&dout);
      dio_put_uint16(&dout, size);

      /* 
       * Use send_connection_data instead of send_packet_data to avoid
       * compression.
       */
      send_connection_data(pconn, buffer, size);
    }

    return FALSE;
  }

  if (type == PACKET_SERVER_JOIN_REQ) {
    return handle_login_request(pconn,
				(struct packet_server_join_req *) packet);
  }

  /* May be received on a non-established connection. */
  if (type == PACKET_AUTHENTICATION_REPLY) {
    return handle_authentication_reply(pconn,
				((struct packet_authentication_reply *)
				 packet)->password);
  }

  if (type == PACKET_CONN_PONG) {
    handle_conn_pong(pconn);
    return TRUE;
  }

  if (!pconn->established) {
    freelog(LOG_ERROR, "Received game packet from unaccepted connection %s",
	    conn_description(pconn));
    return TRUE;
  }
  
  /* valid packets from established connections but non-players */
  if (type == PACKET_CHAT_MSG_REQ
      || type == PACKET_SINGLE_WANT_HACK_REQ
      || type == PACKET_NATION_SELECT_REQ
      || type == PACKET_EDIT_MODE
      || type == PACKET_EDIT_TILE
      || type == PACKET_EDIT_UNIT
      || type == PACKET_EDIT_CREATE_CITY
      || type == PACKET_EDIT_PLAYER) {
    if (!server_handle_packet(type, packet, NULL, pconn)) {
      freelog(LOG_ERROR, "Received unknown packet %d from %s",
	      type, conn_description(pconn));
    }
    return TRUE;
  }

  pplayer = pconn->player;

  if(!pplayer) {
    /* don't support these yet */
    freelog(LOG_ERROR, "Received packet from non-player connection %s",
 	    conn_description(pconn));
    return TRUE;
  }

  if (server_state != RUN_GAME_STATE
      && type != PACKET_NATION_SELECT_REQ
      && type != PACKET_PLAYER_READY
      && type != PACKET_CONN_PONG
      && type != PACKET_REPORT_REQ) {
    if (server_state == GAME_OVER_STATE) {
      /* This can happen by accident, so we don't want to print
	 out lots of error messages. Ie, we use LOG_DEBUG. */
      freelog(LOG_DEBUG, "got a packet of type %d "
			  "in GAME_OVER_STATE", type);
    } else {
      freelog(LOG_ERROR, "got a packet of type %d "
	                 "outside RUN_GAME_STATE", type);
    }
    return TRUE;
  }

  pplayer->nturns_idle=0;

  if((!pplayer->is_alive || pconn->observer)
     && !(type == PACKET_REPORT_REQ || type == PACKET_CONN_PONG)) {
    freelog(LOG_ERROR, _("Got a packet of type %d from a "
			 "dead or observer player"), type);
    return TRUE;
  }
  
  /* Make sure to set this back to NULL before leaving this function: */
  pplayer->current_conn = pconn;

  if (!server_handle_packet(type, packet, pplayer, pconn)) {
    freelog(LOG_ERROR, "Received unknown packet %d from %s",
	    type, conn_description(pconn));
  }

  if (server_state == RUN_GAME_STATE
      && type != PACKET_PLAYER_READY) {
    /* HACK: the player-ready packet puts the server into RUN_GAME_STATE
     * but doesn't actually start the game.  The game isn't started until
     * the main loop is re-entered, so kill_dying_players would think
     * all players are dead.  This should be solved by adding a new
     * game state GAME_GENERATION_STATE. */
    kill_dying_players();
  }

  pplayer->current_conn = NULL;
  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
void check_for_full_turn_done(void)
{
  bool connected = FALSE;

  /* fixedlength is only applicable if we have a timeout set */
  if (game.info.fixedlength && game.info.timeout != 0) {
    return;
  }

  /* If there are no connected players, don't automatically advance.  This is
   * a hack to prevent all-AI games from running rampant.  Note that if
   * timeout is set to -1 this function call is skipped entirely and the
   * server will run rampant. */
  players_iterate(pplayer) {
    if (pplayer->is_connected) {
      connected = TRUE;
      break;
    }
  } players_iterate_end;
  if (!connected) {
    return;
  }

  phase_players_iterate(pplayer) {
    if (game.info.turnblock && !pplayer->ai.control && pplayer->is_alive
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

/**************************************************************************
  Checks if the player name belongs to the default player names of a
  particular player.
**************************************************************************/
static bool is_default_nation_name(const char *name,
				   const struct nation_type *nation)
{
  int choice;

  for (choice = 0; choice < nation->leader_count; choice++) {
    if (mystrcasecmp(name, nation->leaders[choice].name) == 0) {
      return TRUE;
    }
  }

  return FALSE;
}

/**************************************************************************
  Check if this name is allowed for the player.  Fill out the error message
  (a translated string to be sent to the client) if not.
**************************************************************************/
static bool is_allowed_player_name(struct player *pplayer,
				   const struct nation_type *nation,
				   const char *name,
				   char *error_buf, size_t bufsz)
{
  struct connection *pconn = find_conn_by_user(pplayer->username);

  /* An empty name is surely not allowed. */
  if (strlen(name) == 0) {
    if (error_buf) {
      my_snprintf(error_buf, bufsz, _("Please choose a non-blank name."));
    }
    return FALSE;
  }

  /* Any name already taken is not allowed. */
  players_iterate(other_player) {
    if (other_player == pplayer) {
      /* We don't care if we're the one using the name/nation. */
      continue;
    }
    /* FIXME: currently cannot use nation_of_player(other_player)
     * as the nation debug code is buggy and doesn't test nation for NULL
     */
    if (other_player->nation == nation) {
      if (error_buf) {
	my_snprintf(error_buf, bufsz, _("That nation is already in use."));
      }
      return FALSE;
    } else {
      /* Check to see if name has been taken.
       * Ignore case because matches elsewhere are case-insenstive.
       * Don't limit this check to just players with allocated nation:
       * otherwise could end up with same name as pre-created AI player
       * (which have no nation yet, but will keep current player name).
       * Also want to keep all player names strictly distinct at all
       * times (for server commands etc), including during nation
       * allocation phase.
       */
      if (mystrcasecmp(other_player->name, name) == 0) {
	if (error_buf) {
	  my_snprintf(error_buf, bufsz,
		      _("Another player already has the name '%s'.  Please "
			"choose another name."), name);
	}
	return FALSE;
      }
    }
  } players_iterate_end;

  /* Any name from the default list is always allowed. */
  if (is_default_nation_name(name, nation)) {
    return TRUE;
  }

  /* To prevent abuse, only players with HACK access (usually local
   * connections) can use non-ascii names.  Otherwise players could use
   * confusing garbage names in multi-player games. */
    /* FIXME: is there a better way to determine if a *player* has hack
     * access? */
  if (!is_ascii_name(name)
      && (!pconn || pconn->access_level != ALLOW_HACK)) {
    if (error_buf) {
      my_snprintf(error_buf, bufsz, _("Please choose a name containing "
				      "only ASCII characters."));
    }
    return FALSE;
  }

  return TRUE;
}

/****************************************************************************
  Initialize the list of available nations.

  Call this on server start, or when loading a scenario.
****************************************************************************/
void init_available_nations(void)
{
  bool start_nations;
  int i;

  if (map.num_start_positions > 0) {
    start_nations = TRUE;

    for (i = 0; i < map.num_start_positions; i++) {
      if (map.start_positions[i].nation == NO_NATION_SELECTED) {
	start_nations = FALSE;
	break;
      }
    }
  } else {
    start_nations = FALSE;
  }

  if (start_nations) {
    nations_iterate(nation) {
      nation->is_available = FALSE;
    } nations_iterate_end;
    for (i = 0; i < map.num_start_positions; i++) {
      map.start_positions[i].nation->is_available = TRUE;
    }
  }
  nations_iterate(nation) {
    /* Even though this function is called init_available_nations(),
     * nation->player should never have value assigned to it
     * (since it has beeen initialized in load_rulesets() ). */
    if (nation->player != NULL) {

      freelog(LOG_ERROR, "Player assigned to nation before "
                         "init_available_nations()");

      /* When we enter this execution branch, assert() will always
       * fail. This one just provides more informative message than
       * simple assert(FAIL); */
      assert(nation->player == NULL);

      /* Try to handle error situation as well as we can */
      if (nation->player->nation == nation) {
        /* At least assignment is consistent. Leave nation assigned,
         * and make sure that nation is also marked available. */
        nation->is_available = TRUE;
      } else {
        /* Not consistent. Just initialize the pointer and hope for the best */
        nation->player = NULL;
      }
    }
  } nations_iterate_end;
}

/**************************************************************************
  Handles a pick-nation packet from the client.  These packets are
  handled by connection because ctrl users may edit anyone's nation in
  pregame, and editing is possible during a running game.
**************************************************************************/
void handle_nation_select_req(struct connection *pc,
			      int player_no,
			      Nation_type_id nation_no, bool is_male,
			      char *name, int city_style)
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
      notify_conn(pplayer->connections, NULL, E_NATION_SELECTED,
		  _("%s nation is not available in this scenario."),
		  nation_name_translation(new_nation));
      return;
    }
    if (new_nation->player && new_nation->player != pplayer) {
      notify_conn(pplayer->connections, NULL, E_NATION_SELECTED,
		  _("%s nation is already in use."),
		  nation_name_translation(new_nation));
      return;
    }

    remove_leading_trailing_spaces(name);

    if (!is_allowed_player_name(pplayer, new_nation, name,
				message, sizeof(message))) {
      notify_conn(pplayer->connections, NULL, E_NATION_SELECTED,
		  "%s", message);
      return;
    }

    name[0] = my_toupper(name[0]);
    sz_strlcpy(pplayer->name, name);

    notify_conn(NULL, NULL, E_NATION_SELECTED,
		_("%s is the %s ruler %s."),
		pplayer->username,
		nation_name_translation(new_nation),
		pplayer->name);

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
  bool old_ready;

  if (server_state != PRE_GAME_STATE
      || !pplayer) {
    return;
  }

  if (pplayer != requestor) {
    /* Currently you can only change your own readiness. */
    return;
  }

  old_ready = pplayer->is_ready;
  pplayer->is_ready = is_ready;
  send_player_info(pplayer, NULL);

  /* Note this is called even if the player has pressed /start once
   * before.  This is a good thing given that no other code supports
   * is_started yet.  For instance if a player leaves everyone left
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
      notify_conn(NULL, NULL, E_SETTING,
		  _("Waiting to start game: %d out of %d players "
		    "are ready to start."),
		  num_ready, num_ready + num_unready);
    } else {
      notify_conn(NULL, NULL, E_SETTING,
		  _("All players are ready; starting game."));
      start_game();
    }
  }
}

/****************************************************************************
  Fill or remove players to meet the given aifill.
****************************************************************************/
void aifill(int amount)
{
  int remove, filled = 0;

  if (server_state != PRE_GAME_STATE || !game.info.is_new_game) {
    return;
  }

  if (amount == 0) {
    /* Special case for value 0: do nothing. */
    return;
  }

  amount = MIN(amount, game.info.max_players);

  while (game.info.nplayers < amount) {
    const int old_nplayers = game.info.nplayers;
    struct player *pplayer = player_by_number(old_nplayers);
    char player_name[ARRAY_SIZE(pplayer->name)];

    server_player_init(pplayer, FALSE, TRUE);
    player_set_nation(pplayer, NULL);
    do {
      my_snprintf(player_name, sizeof(player_name),
		  "AI%d", ++filled);
    } while (find_player_by_name(player_name));
    sz_strlcpy(pplayer->name, player_name);
    sz_strlcpy(pplayer->username, ANON_USER_NAME);
    pplayer->ai.skill_level = game.info.skill_level;
    pplayer->ai.control = TRUE;
    set_ai_level_directer(pplayer, game.info.skill_level);

    freelog(LOG_NORMAL,
	    _("%s has been added as %s level AI-controlled player."),
            pplayer->name,
	    ai_level_name(pplayer->ai.skill_level));
    notify_conn(NULL, NULL, E_SETTING,
		_("%s has been added as %s level AI-controlled player."),
		pplayer->name,
		ai_level_name(pplayer->ai.skill_level));

    game.info.nplayers++;

    send_game_info(NULL);
    send_player_info(pplayer, NULL);
  }

  remove = game.info.nplayers - 1;
  while (game.info.nplayers > amount && remove >= 0) {
    struct player *pplayer = player_by_number(remove);

    if (!pplayer->is_connected && !pplayer->was_created) {
      server_remove_player(pplayer);
    }
    remove--;
  }
}

/**************************************************************************
   generate_players() - Selects a nation for players created with
   server's "create <PlayerName>" command.  If <PlayerName> matches
   one of the leader names for some nation, we choose that nation.
   (I.e. if we issue "create Shaka" then we will make that AI player's
   nation the Zulus if the Zulus have not been chosen by anyone else.
   If they have, then we pick an available nation at random.)

   If the AI player name is one of the leader names for the AI player's
   nation, the player sex is set to the sex for that leader, else it
   is chosen randomly.  (So if English are ruled by Elisabeth, she is
   female, but if "Player 1" rules English, may be male or female.)
**************************************************************************/
static void generate_players(void)
{
  char player_name[MAX_LEN_NAME];

  /* Select nations for AI players generated with server
   * 'create <name>' command
   */
  players_iterate(pplayer) {
    if (pplayer->nation != NO_NATION_SELECTED) {
      announce_player(pplayer);
      continue;
    }

    /* See if the player name matches a known leader name. */
    nations_iterate(pnation) {
      if (is_nation_playable(pnation)
	  && pnation->is_available
	  && !pnation->player
	  && check_nation_leader_name(pnation, pplayer->name)) {
	player_set_nation(pplayer, pnation);
	pplayer->city_style = city_style_of_nation(pnation);
	pplayer->is_male = get_nation_leader_sex(pnation, pplayer->name);
	break;
      }
    } nations_iterate_end;
    if (pplayer->nation != NO_NATION_SELECTED) {
      announce_player(pplayer);
      continue;
    }

    player_set_nation(pplayer, pick_a_nation(NULL, FALSE, TRUE,
                                             NOT_A_BARBARIAN));
    assert(pplayer->nation != NO_NATION_SELECTED);

    pplayer->city_style = city_style_of_nation(nation_of_player(pplayer));

    pick_random_player_name(nation_of_player(pplayer), player_name);
    sz_strlcpy(pplayer->name, player_name);

    if (check_nation_leader_name(nation_of_player(pplayer), player_name)) {
      pplayer->is_male = get_nation_leader_sex(nation_of_player(pplayer), player_name);
    } else {
      pplayer->is_male = (myrand(2) == 1);
    }

    announce_player(pplayer);
  } players_iterate_end;

  (void) send_server_info_to_metaserver(META_INFO);
}

/*************************************************************************
 Used in pick_random_player_name() below; buf has size at least MAX_LEN_NAME;
*************************************************************************/
static bool good_name(char *ptry, char *buf) {
  if (!(find_player_by_name(ptry) || find_player_by_user(ptry))) {
     (void) mystrlcpy(buf, ptry, MAX_LEN_NAME);
     return TRUE;
  }
  return FALSE;
}

/*************************************************************************
  Returns a random ruler name picked from given nation
     ruler names, given that nation's number. If that player name is already 
     taken, iterates through all leader names to find unused one. If it fails
     it iterates through "Player 1", "Player 2", ... until an unused name
     is found.
 newname should point to a buffer of size at least MAX_LEN_NAME.
*************************************************************************/
void pick_random_player_name(const struct nation_type *nation, char *newname) 
{
   int i, names_count;
   struct leader *leaders;

   leaders = get_nation_leaders(nation, &names_count);

   /* Try random names (scattershot), then all available,
    * then "Player 1" etc:
    */
   for(i=0; i<names_count; i++) {
     if (good_name(leaders[myrand(names_count)].name, newname)) {
       return;
     }
   }
   
   for(i=0; i<names_count; i++) {
     if (good_name(leaders[i].name, newname)) {
       return;
     }
   }
   
   for(i=1; /**/; i++) {
     char tempname[50];
     my_snprintf(tempname, sizeof(tempname), _("Player %d"), i);
     if (good_name(tempname, newname)) return;
   }
}

/*************************************************************************
...
*************************************************************************/
static void announce_player (struct player *pplayer)
{
   freelog(LOG_NORMAL,
	   _("%s rules the %s."),
	   pplayer->name,
	   nation_plural_for_player(pplayer));

  players_iterate(other_player) {
    notify_player(other_player, NULL, E_GAME_START,
		  _("%s rules the %s."), pplayer->name,
		  nation_plural_for_player(pplayer));
  } players_iterate_end;
}

/**************************************************************************
Play the game! Returns when server_state == GAME_OVER_STATE.
**************************************************************************/
static void main_loop(void)
{
  struct timer *eot_timer;	/* time server processing at end-of-turn */
  int save_counter = 0;
  bool is_new_turn = game.info.is_new_game;

  /* We may as well reset is_new_game now. */
  game.info.is_new_game = FALSE;

  eot_timer = new_timer_start(TIMER_CPU, TIMER_ACTIVE);

  /* 
   * This will freeze the reports and agents at the client.
   * 
   * Do this before the body so that the PACKET_THAW_CLIENT packet is
   * balanced. 
   */
  lsend_packet_freeze_client(game.est_connections);

  assert(server_state == RUN_GAME_STATE);
  while (server_state == RUN_GAME_STATE) {
    /* The beginning of a turn.
     *
     * We have to initialize data as well as do some actions.  However when
     * loading a game we don't want to do these actions (like AI unit
     * movement and AI diplomacy). */
    begin_turn(is_new_turn);

    if (game.info.num_phases != 1) {
      /* We allow everyone to begin adjusting cities and such
       * from the beginning of the turn.
       * With simultaneous movement we send begin_turn packet in
       * begin_phase() only after AI players have finished their actions. */
      lsend_packet_begin_turn(game.est_connections);
    }

    for (; game.info.phase < game.info.num_phases; game.info.phase++) {
      freelog(LOG_DEBUG, "Starting phase %d/%d.", game.info.phase,
	      game.info.num_phases);
      begin_phase(is_new_turn);
      is_new_turn = TRUE;

      force_end_of_sniff = FALSE;

      /* 
       * This will thaw the reports and agents at the client.
       */
      lsend_packet_thaw_client(game.est_connections);

      /* Before sniff (human player activites), report time to now: */
      freelog(LOG_VERBOSE, "End/start-turn server/ai activities: %g seconds",
	      read_timer_seconds(eot_timer));

      /* Do auto-saves just before starting sniff_packets(), so that
       * autosave happens effectively "at the same time" as manual
       * saves, from the point of view of restarting and AI players.
       * Post-increment so we don't count the first loop.
       */
      if (game.info.phase == 0) {
	if (save_counter >= game.info.save_nturns && game.info.save_nturns > 0) {
	  save_counter = 0;
	  save_game_auto("Autosave");
	}
	save_counter++;
      }

      freelog(LOG_DEBUG, "sniffingpackets");
      check_for_full_turn_done(); /* HACK: don't wait during AI phases */
      while (sniff_packets() == 1) {
	/* nothing */
      }

      /* After sniff, re-zero the timer: (read-out above on next loop) */
      clear_timer_start(eot_timer);

      conn_list_do_buffer(game.est_connections);

      sanity_check();

      /* 
       * This will freeze the reports and agents at the client.
       */
      lsend_packet_freeze_client(game.est_connections);

      end_phase();

      conn_list_do_unbuffer(game.est_connections);

      if (server_state == GAME_OVER_STATE) {
	break;
      }
    }
    end_turn();
    freelog(LOG_DEBUG, "Sendinfotometaserver");
    (void) send_server_info_to_metaserver(META_REFRESH);

    if (server_state != GAME_OVER_STATE && check_for_game_over()) {
      server_state = GAME_OVER_STATE;
      /* this goes here, because we don't rank users after an /endgame */
      rank_users();
    }
  }

  /* This will thaw the reports and agents at the client.  */
  lsend_packet_thaw_client(game.est_connections);

  free_timer(eot_timer);
}

/**************************************************************************
  Server initialization.
**************************************************************************/
void srv_main(void)
{
  /* make sure it's initialized */
  if (!has_been_srv_init) {
    srv_init();
  }

  my_init_network();

  con_log_init(srvarg.log_filename, srvarg.loglevel);
  
#if IS_BETA_VERSION
  con_puts(C_COMMENT, "");
  con_puts(C_COMMENT, beta_message());
  con_puts(C_COMMENT, "");
#endif
  
  con_flush();

  stdinhand_init();
  diplhand_init();

  /* init network */  
  init_connections(); 
  if (!with_ggz) {
    server_open_socket();
  }

  server_game_init();

  /* load a saved game */
  if (srvarg.load_filename[0] != '\0') {
    (void) load_command(NULL, srvarg.load_filename, FALSE);
  } 

  if(!(srvarg.metaserver_no_send)) {
    freelog(LOG_NORMAL, _("Sending info to metaserver [%s]"),
	    meta_addr_port());
    server_open_meta(); /* open socket for meta server */ 
  }

  maybe_automatic_meta_message(default_meta_message_string());
  (void) send_server_info_to_metaserver(META_INFO);

  /* accept new players, wait for serverop to start..*/
  server_state = PRE_GAME_STATE;

  /* load a script file */
  if (srvarg.script_filename
      && !read_init_script(NULL, srvarg.script_filename, TRUE)) {
    exit(EXIT_FAILURE);
  }

  /* Run server loop */
  while (TRUE) {
    srv_loop();

    /* Recalculate the scores in case of a spaceship victory */
    players_iterate(pplayer) {
      calc_civ_score(pplayer);
    } players_iterate_end;

    send_game_state(game.est_connections, CLIENT_GAME_OVER_STATE);
    report_final_scores();
    show_map_to_all();
    notify_player(NULL, NULL, E_GAME_END, _("The game is over..."));
    send_server_info_to_metaserver(META_INFO);
    if (game.info.save_nturns > 0
	&& conn_list_size(game.est_connections) > 0) {
      /* Save game on game-over, but not when the gameover was caused by
       * the -q parameter. */
      save_game_auto("Game over");
    }

    /* Remain in GAME_OVER_STATE until players log out */
    while (conn_list_size(game.est_connections) > 0) {
      (void) sniff_packets();
    }

    if (game.info.timeout == -1 || srvarg.exit_on_end) {
      /* For autogames or if the -e option is specified, exit the server. */
      server_quit();
    }

    /* Reset server */
    server_game_free();
    server_game_init();
    game.info.is_new_game = TRUE;
    server_state = PRE_GAME_STATE;
  }

  /* Technically, we won't ever get here. We exit via server_quit. */
}

/**************************************************************************
  Apply some final adjustments from the ruleset on to the game state.
  We cannot do this during ruleset loading, since some players may be
  added later than that.
**************************************************************************/
static void final_ruleset_adjustments()
{
  players_iterate(pplayer) {
    struct nation_type *pnation = nation_of_player(pplayer);

    pplayer->government = pnation->init_government;

    if (pnation->init_government == game.government_when_anarchy) {
      /* If we do not do this, an assert will trigger. This enables us to
       * select a valid government on game start. */
      pplayer->revolution_finishes = 0;
    }
  } players_iterate_end;
}

/**************************************************************************
  Server loop, run to set up one game.
**************************************************************************/
static void srv_loop(void)
{
  freelog(LOG_NORMAL, _("Now accepting new client connections."));
  while(server_state == PRE_GAME_STATE) {
    sniff_packets(); /* Accepting commands. */
  }

  (void) send_server_info_to_metaserver(META_INFO);

  if (game.info.auto_ai_toggle) {
    players_iterate(pplayer) {
      if (!pplayer->is_connected && !pplayer->ai.control) {
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
    generate_players();
    final_ruleset_adjustments();
  }
   
  /* If we have a tile map, and map.generator==0, call map_fractal_generate
   * anyway to make the specials, huts and continent numbers. */
  if (map_is_empty() || (map.generator == 0 && game.info.is_new_game)) {
    struct unit_type *utype = crole_to_unit_type(game.info.start_units[0], NULL);

    map_fractal_generate(TRUE, utype);
    game_map_init();
  }

  /* start the game */

  server_state = RUN_GAME_STATE;
  (void) send_server_info_to_metaserver(META_INFO);
  send_server_settings(NULL);

  if(game.info.is_new_game) {
    /* Before the player map is allocated (and initiailized)! */
    game.fogofwar_old = game.info.fogofwar;

    players_iterate(pplayer) {
      player_map_allocate(pplayer);
      init_tech(pplayer);
      player_limit_to_government_rates(pplayer);
      pplayer->economic.gold = game.info.gold;
    } players_iterate_end;

    players_iterate(pplayer) {
      give_initial_techs(pplayer);
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
        break;
      }
      
      for (i = 0; i < game.info.tech; i++) {
        give_random_initial_tech(pplayer);
      }
    } players_iterate_end;
    
    
    if(game.info.is_new_game) {
      /* If we're starting a new game, reset the rules.max_players to be the
       * number of players currently in the game.  But when loading a game
       * we don't want to change it. */
      game.info.max_players = game.info.nplayers;
    }
  }

  /* Set up alliances based on team selections */
  if (game.info.is_new_game) {
   players_iterate(pplayer) {
     players_iterate(pdest) {
      if (players_on_same_team(pplayer, pdest)
          && player_number(pplayer) != player_number(pdest)) {
        pplayer->diplstates[player_index(pdest)].type = DS_TEAM;
        give_shared_vision(pplayer, pdest);
	BV_SET(pplayer->embassy, player_index(pdest));
      }
    } players_iterate_end;
   } players_iterate_end;
  }
  
  players_iterate(pplayer) {
    ai_data_analyze_rulesets(pplayer);
  } players_iterate_end;
  if (!game.info.is_new_game) {
    players_iterate(pplayer) {
      if (pplayer->ai.control) {
	set_ai_level_direct(pplayer, pplayer->ai.skill_level);
      }
    } players_iterate_end;
  } else {
    players_iterate(pplayer) {
      ai_data_init(pplayer); /* Initialize this again to be sure */
    } players_iterate_end;
  }

  lsend_packet_freeze_hint(game.est_connections);
  send_all_info(game.est_connections);
  lsend_packet_thaw_hint(game.est_connections);
  
  if(game.info.is_new_game) {
    init_new_game();
  }

  send_game_state(game.est_connections, CLIENT_GAME_RUNNING_STATE);

  /*** Where the action is. ***/
  main_loop();
}

/**************************************************************************
  Initialize game data for the server (corresponds to server_game_free).
**************************************************************************/
void server_game_init(void)
{
  game_init();

  /* Rulesets are loaded on game initialization, but may be changed later
   * if /load or /rulesetdir is done. */
  load_rulesets();
}

/**************************************************************************
  Free game data that we reinitialize as part of a server soft restart.
  Bear in mind that this function is called when the 'load' command is
  used, for instance.
**************************************************************************/
void server_game_free(void)
{
  /* Free all the treaties that were left open when game finished. */
  free_treaties();

  players_iterate(pplayer) {
    unit_list_iterate(pplayer->units, punit) {
      vision_free(punit->server.vision);
      punit->server.vision = NULL;
    } unit_list_iterate_end;

    city_list_iterate(pplayer->cities, pcity) {
      vision_free(pcity->server.vision);
      pcity->server.vision = NULL;
    } city_list_iterate_end;

    player_map_free(pplayer);
  } players_iterate_end;
  game_free();
}
