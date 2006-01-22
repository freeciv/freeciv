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

#ifdef WIN32_NATIVE
#include <windows.h>	/* LoadLibrary() */
#endif

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef GGZ_GTK
#  include <ggz-embed.h>
#endif

#include "capstr.h"
#include "dataio.h"
#include "diptreaty.h"
#include "fciconv.h"
#include "fcintl.h"
#include "game.h"
#include "idex.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "netintf.h"
#include "packets.h"
#include "rand.h"
#include "support.h"
#include "timing.h"
#include "version.h"

#include "agents.h"
#include "attribute.h"
#include "audio.h"
#include "chatline_g.h"
#include "citydlg_g.h"
#include "cityrepdata.h"
#include "climisc.h"
#include "clinet.h"
#include "cma_core.h"		/* kludge */
#include "connectdlg_g.h"
#include "control.h" 
#include "dialogs_g.h"
#include "diplodlg_g.h"
#include "ggzclient.h"
#include "goto.h"
#include "gui_main_g.h"
#include "helpdata.h"		/* boot_help_texts() */
#include "mapctrl_g.h"
#include "mapview_g.h"
#include "menu_g.h"
#include "messagewin_g.h"
#include "options.h"
#include "packhand.h"
#include "pages_g.h"
#include "plrdlg_g.h"
#include "repodlgs_g.h"
#include "tilespec.h"
#include "themes_common.h"

#include "civclient.h"

/* this is used in strange places, and is 'extern'd where
   needed (hence, it is not 'extern'd in civclient.h) */
bool is_server = FALSE;

char *logfile = NULL;
char *scriptfile = NULL;
static char tileset_name[512] = "\0";
char sound_plugin_name[512] = "\0";
char sound_set_name[512] = "\0";
char server_host[512] = "\0";
char user_name[512] = "\0";
char password[MAX_LEN_PASSWORD] = "\0";
char metaserver[512] = "\0";
int  server_port = -1;
bool auto_connect = FALSE; /* TRUE = skip "Connect to Freeciv Server" dialog */
bool in_ggz = FALSE;

static enum client_states client_state = CLIENT_BOOT_STATE;

/* TRUE if an end turn request is blocked by busy agents */
bool waiting_for_end_turn = FALSE;

/* 
 * TRUE for the time between sending PACKET_TURN_DONE and receiving
 * PACKET_NEW_YEAR. 
 */
bool turn_done_sent = FALSE;

/**************************************************************************
  Convert a text string from the internal to the data encoding, when it
  is written to the network.
**************************************************************************/
static char *put_conv(const char *src, size_t *length)
{
  char *out = internal_to_data_string_malloc(src);

  if (out) {
    *length = strlen(out);
    return out;
  } else {
    *length = 0;
    return NULL;
  }
}

/**************************************************************************
  Convert a text string from the data to the internal encoding when it is
  first read from the network.  Returns FALSE if the destination isn't
  large enough or the source was bad.
**************************************************************************/
static bool get_conv(char *dst, size_t ndst,
		     const char *src, size_t nsrc)
{
  char *out = data_to_internal_string_malloc(src);
  bool ret = TRUE;
  size_t len;

  if (!out) {
    dst[0] = '\0';
    return FALSE;
  }

  len = strlen(out);
  if (ndst > 0 && len >= ndst) {
    ret = FALSE;
    len = ndst - 1;
  }

  memcpy(dst, out, len);
  dst[len] = '\0';
  free(out);

  return ret;
}

/**************************************************************************
  Set up charsets for the client.
**************************************************************************/
static void charsets_init(void)
{
  dio_set_put_conv_callback(put_conv);
  dio_set_get_conv_callback(get_conv);
}

/**************************************************************************
...
**************************************************************************/
int main(int argc, char *argv[])
{
  int i, loglevel;
  int ui_options = 0;
  bool ui_separator = FALSE;
  char *option=NULL;

  /* Load win32 post-crash debugger */
#ifdef WIN32_NATIVE
# ifndef NDEBUG
  if (LoadLibrary("exchndl.dll") == NULL) {
#  ifdef DEBUG
    fprintf(stderr, "exchndl.dll could not be loaded, no crash debugger\n");
#  endif
  }
# endif
#endif

  init_nls();
  audio_init();
  init_character_encodings(gui_character_encoding, gui_use_transliteration);

  /* default argument values are set in options.c */
  loglevel=LOG_NORMAL;

  i = 1;

  while (i < argc) {
    if (ui_separator) {
      argv[1 + ui_options] = argv[i];
      ui_options++;
    } else if (is_option("--help", argv[i])) {
      fc_fprintf(stderr, _("Usage: %s [option ...]\n"
			   "Valid options are:\n"), argv[0]);
      fc_fprintf(stderr, _("  -a, --autoconnect\tSkip connect dialog\n"));
#ifdef DEBUG
      fc_fprintf(stderr, _("  -d, --debug NUM\tSet debug log level (0 to 4,"
			   " or 4:file1,min,max:...)\n"));
#else
      fc_fprintf(stderr,
		 _("  -d, --debug NUM\tSet debug log level (0 to 3)\n"));
#endif
      fc_fprintf(stderr,
		 _("  -h, --help\t\tPrint a summary of the options\n"));
      fc_fprintf(stderr, _("  -l, --log FILE\tUse FILE as logfile "
			   "(spawned server also uses this)\n"));
      fc_fprintf(stderr, _("  -m, --meta HOST\t"
			   "Connect to the metaserver at HOST\n"));
      fc_fprintf(stderr, _("  -n, --name NAME\tUse NAME as name\n"));
      fc_fprintf(stderr,
		 _("  -p, --port PORT\tConnect to server port PORT\n"));
      fc_fprintf(stderr,
		 _("  -P, --Plugin PLUGIN\tUse PLUGIN for sound output %s\n"),
		 audio_get_all_plugin_names());
      fc_fprintf(stderr, _("  -r, --read FILE\tRead startup script FILE "
			   "(for spawned server only)\n"));
      fc_fprintf(stderr,
		 _("  -s, --server HOST\tConnect to the server at HOST\n"));
      fc_fprintf(stderr,
		 _("  -S, --Sound FILE\tRead sound tags from FILE\n"));
      fc_fprintf(stderr, _("  -t, --tiles FILE\t"
			   "Use data file FILE.tilespec for tiles\n"));
      fc_fprintf(stderr, _("  -v, --version\t\tPrint the version number\n"));
#ifdef GGZ_CLIENT
    fc_fprintf(stderr, _("  -z, --zone\t\tEnable GGZ mode\n"));
#endif
      fc_fprintf(stderr, _("      --\t\t"
			   "Pass any following options to the UI.\n"
			   "\t\t\tTry \"%s -- --help\" for more.\n"), argv[0]);
      exit(EXIT_SUCCESS);
    } else if (is_option("--version",argv[i])) {
      fc_fprintf(stderr, "%s %s\n", freeciv_name_version(), client_string);
      exit(EXIT_SUCCESS);
    } else if ((option = get_option_malloc("--log", argv, &i, argc))) {
      logfile = option; /* never free()d */
    } else  if ((option = get_option_malloc("--read", argv, &i, argc))) {
      scriptfile = option; /* never free()d */
    } else if ((option = get_option_malloc("--name", argv, &i, argc))) {
      sz_strlcpy(user_name, option);
      free(option);
    } else if ((option = get_option_malloc("--meta", argv, &i, argc))) {
      sz_strlcpy(metaserver, option);
      free(option);
    } else if ((option = get_option_malloc("--Sound", argv, &i, argc))) {
      sz_strlcpy(sound_set_name, option);
      free(option);
    } else if ((option = get_option_malloc("--Plugin", argv, &i, argc))) {
      sz_strlcpy(sound_plugin_name, option);
      free(option);
    } else if ((option = get_option_malloc("--port",argv,&i,argc))) {
      if (sscanf(option, "%d", &server_port) != 1) {
	fc_fprintf(stderr,
		   _("Invalid port \"%s\" specified with --port option.\n"),
		   option);
	fc_fprintf(stderr, _("Try using --help.\n"));
        exit(EXIT_FAILURE);
      }
      free(option);
    } else if ((option = get_option_malloc("--server", argv, &i, argc))) {
      sz_strlcpy(server_host, option);
      free(option);
    } else if (is_option("--autoconnect", argv[i])) {
      auto_connect = TRUE;
    } else if ((option = get_option_malloc("--debug", argv, &i, argc))) {
      loglevel = log_parse_level_str(option);
      if (loglevel == -1) {
	fc_fprintf(stderr,
		   _("Invalid debug level \"%s\" specified with --debug "
		     "option.\n"), option);
	fc_fprintf(stderr, _("Try using --help.\n"));
        exit(EXIT_FAILURE);
      }
      free(option);
    } else if ((option = get_option_malloc("--tiles", argv, &i, argc))) {
      sz_strlcpy(tileset_name, option);
      free(option);
#ifdef GGZ_CLIENT
    } else if (is_option("--zone", argv[i])) {
      with_ggz = TRUE;
#endif
    } else if (is_option("--", argv[i])) {
      ui_separator = TRUE;
    } else {
      fc_fprintf(stderr, _("Unrecognized option: \"%s\"\n"), argv[i]);
      exit(EXIT_FAILURE);
    }
    i++;
  } /* of while */

  /* Remove all options except those intended for the UI. */
  argv[1 + ui_options] = NULL;
  argc = 1 + ui_options;

  /* disallow running as root -- too dangerous */
  dont_run_as_root(argv[0], "freeciv_client");

  log_init(logfile, loglevel, NULL);

  /* after log_init: */

  (void)user_username(default_user_name, MAX_LEN_NAME);
  if (!is_valid_username(default_user_name)) {
    char buf[sizeof(default_user_name)];

    my_snprintf(buf, sizeof(buf), "_%s", default_user_name);
    if (is_valid_username(buf)) {
      sz_strlcpy(default_user_name, buf);
    } else {
      my_snprintf(default_user_name, sizeof(default_user_name),
		  "player%d", myrand(10000));
    }
  }

  /* initialization */

  game.all_connections = conn_list_new();
  game.est_connections = conn_list_new();

  ui_init();
  charsets_init();
  my_init_network();
  chatline_common_init();
  message_options_init();
  init_player_dlg_common();
  init_themes();
  settable_options_init();

  load_general_options();

  if (tileset_name[0] == '\0') {
    sz_strlcpy(tileset_name, default_tileset_name);
  }
  if (sound_set_name[0] == '\0') 
    sz_strlcpy(sound_set_name, default_sound_set_name); 
  if (sound_plugin_name[0] == '\0')
    sz_strlcpy(sound_plugin_name, default_sound_plugin_name); 
  if (server_host[0] == '\0')
    sz_strlcpy(server_host, default_server_host); 
  if (user_name[0] == '\0')
    sz_strlcpy(user_name, default_user_name); 
  if (metaserver[0] == '\0')
    sz_strlcpy(metaserver, default_metaserver); 
  if (server_port == -1) server_port = default_server_port;

  /* This seed is not saved anywhere; randoms in the client should
     have cosmetic effects only (eg city name suggestions).  --dwp */
  mysrand(time(NULL));
  control_init();
  helpdata_init();
  boot_help_texts();

  tilespec_try_read(tileset_name);

  audio_real_init(sound_set_name, sound_plugin_name);
  audio_play_music("music_start", NULL);

  if (with_ggz) {
    ggz_initialize();
  }
#ifdef GGZ_GTK
  {
    char buf[128];

    user_username(buf, sizeof(buf));
    cat_snprintf(buf, sizeof(buf), "%d", myrand(100));
    ggz_embed_ensure_server("Pubserver", "pubserver.freeciv.org",
			    5688, buf);
  }
#endif

  /* run gui-specific client */
  ui_main(argc, argv);

  /* termination */
  client_exit();
  
  /* not reached */
  return EXIT_SUCCESS;
}

/**************************************************************************
...
**************************************************************************/
void client_exit(void)
{
  attribute_flush();
  client_remove_all_cli_conn();
  my_shutdown_network();

  if (save_options_on_exit) {
    save_options();
  }
  
  tileset_free(tileset);
  
  ui_exit();
  
  control_done();
  chatline_common_done();
  message_options_free();
  client_game_free();

  helpdata_done(); /* client_exit() unlinks help text list */
  conn_list_free(game.all_connections);
  conn_list_free(game.est_connections);
  exit(EXIT_SUCCESS);
}


/**************************************************************************
...
**************************************************************************/
void handle_packet_input(void *packet, int type)
{
  if (!client_handle_packet(type, packet)) {
    freelog(LOG_ERROR, "Received unknown packet (type %d) from server!",
	    type);
  }
}

/**************************************************************************
...
**************************************************************************/
void user_ended_turn(void)
{
  send_turn_done();
}

/**************************************************************************
...
**************************************************************************/
void send_turn_done(void)
{
  freelog(LOG_DEBUG, "send_turn_done() turn_done_button_state=%d",
	  get_turn_done_button_state());

  if (!get_turn_done_button_state()) {
    /*
     * The turn done button is disabled but the user may have press
     * the return key.
     */

    if (agents_busy()) {
      waiting_for_end_turn = TRUE;
    }

    return;
  }

  waiting_for_end_turn = FALSE;
  turn_done_sent = TRUE;

  attribute_flush();

  dsend_packet_player_phase_done(&aconnection, game.info.turn);

  update_turn_done_button_state();
}

/**************************************************************************
...
**************************************************************************/
void send_report_request(enum report_type type)
{
  dsend_packet_report_req(&aconnection, type);
}

/**************************************************************************
 called whenever client is changed to pre-game state.
**************************************************************************/
void client_game_init()
{
  game_init();
  attribute_init();
  agents_init();
}

/**************************************************************************
...
**************************************************************************/
void client_game_free()
{
  free_client_goto();
  free_help_texts();
  attribute_free();
  agents_free();
  game_free();
}

/**************************************************************************
...
**************************************************************************/
void set_client_state(enum client_states newstate)
{
  bool connect_error = (client_state == CLIENT_PRE_GAME_STATE)
      && (newstate == CLIENT_PRE_GAME_STATE);
  enum client_states oldstate = client_state;

  if (newstate == CLIENT_GAME_OVER_STATE) {
    /*
     * Extra kludge for end-game handling of the CMA.
     */
    if (game.player_ptr) {
      city_list_iterate(game.player_ptr->cities, pcity) {
	if (cma_is_city_under_agent(pcity, NULL)) {
	  cma_release_city(pcity);
	}
      } city_list_iterate_end;
    }
    popdown_all_city_dialogs();
    popdown_all_game_dialogs();
    set_unit_focus(NULL);
  }

  if (client_state != newstate) {

    /* If changing from pre-game state to _either_ select race
       or running state, then we have finished getting ruleset data,
       and should translate data, for joining running game or for
       selecting nations.  (Want translated nation names in nation
       select dialog.)
    */
    if (client_state==CLIENT_PRE_GAME_STATE
	&& newstate == CLIENT_GAME_RUNNING_STATE) {
      translate_data_names();
      audio_stop();		/* stop intro sound loop */
    }
      
    client_state=newstate;

    if (client_state == CLIENT_GAME_RUNNING_STATE) {
      init_city_report_game_data();
      load_ruleset_specific_options();
      create_event(NULL, E_GAME_START, _("Game started."));
      precalc_tech_data();
      if (game.player_ptr) {
	update_research(game.player_ptr);
      }
      role_unit_precalcs();
      boot_help_texts();	/* reboot */
      can_slide = FALSE;
      update_unit_focus();
      can_slide = TRUE;
      set_client_page(PAGE_GAME);
    }
    else if (client_state == CLIENT_PRE_GAME_STATE) {
      popdown_all_city_dialogs();
      close_all_diplomacy_dialogs();
      popdown_all_game_dialogs();
      set_unit_focus(NULL);
      clear_notify_window();
      if (oldstate != CLIENT_BOOT_STATE) {
	client_game_free();
      }
      client_game_init();
      if (!aconnection.established && !with_ggz) {
	set_client_page(in_ggz ? PAGE_GGZ : PAGE_MAIN);
      } else {
	set_client_page(PAGE_START);
      }
    }
    update_menus();
  }
  if (!aconnection.established && client_state == CLIENT_PRE_GAME_STATE) {
    gui_server_connect();
    if (auto_connect) {
      if (connect_error) {
	freelog(LOG_NORMAL,
		_("There was an error while auto connecting; aborting."));
	exit(EXIT_FAILURE);
      } else {
	start_autoconnecting_to_server();
	auto_connect = FALSE;	/* don't try this again */
      }
    } 
  }
  update_turn_done_button_state();
  update_conn_list_dialog();
}


/**************************************************************************
...
**************************************************************************/
enum client_states get_client_state(void)
{
  return client_state;
}

/**************************************************************************
  Remove pconn from all connection lists in client, then free it.
**************************************************************************/
void client_remove_cli_conn(struct connection *pconn)
{
  if (pconn->player) {
    conn_list_unlink(pconn->player->connections, pconn);
  }
  conn_list_unlink(game.all_connections, pconn);
  conn_list_unlink(game.est_connections, pconn);
  assert(pconn != &aconnection);
  free(pconn);
}

/**************************************************************************
  Remove (and free) all connections from all connection lists in client.
  Assumes game.all_connections is properly maintained with all connections.
**************************************************************************/
void client_remove_all_cli_conn(void)
{
  while (conn_list_size(game.all_connections) > 0) {
    struct connection *pconn = conn_list_get(game.all_connections, 0);
    client_remove_cli_conn(pconn);
  }
}

/**************************************************************************
..
**************************************************************************/
void send_attribute_block_request()
{
  send_packet_player_attribute_block(&aconnection);
}

/**************************************************************************
..
**************************************************************************/
void wait_till_request_got_processed(int request_id)
{
  input_from_server_till_request_got_processed(aconnection.sock,
					       request_id);
}

/**************************************************************************
..
**************************************************************************/
bool client_is_observer(void)
{
  return aconnection.established && aconnection.observer;
}

/* Seconds_to_turndone is the number of seconds the server has told us
 * are left.  The timer tells exactly how much time has passed since the
 * server gave us that data. */
static double seconds_to_turndone = 0.0;
static struct timer *turndone_timer;

/* This value shows what value the timeout label is currently showing for
 * the seconds-to-turndone. */
static int seconds_shown_to_turndone;

/**************************************************************************
  Reset the number of seconds to turndone from an "authentic" source.

  The seconds are taken as a double even though most callers will just
  know an integer value.
**************************************************************************/
void set_seconds_to_turndone(double seconds)
{
  if (game.info.timeout > 0) {
    seconds_to_turndone = seconds;
    turndone_timer = renew_timer_start(turndone_timer, TIMER_USER,
				       TIMER_ACTIVE);

    /* Maybe we should do an update_timeout_label here, but it doesn't
     * seem to be necessary. */
    seconds_shown_to_turndone = ceil(seconds) + 0.1;
  }
}

/**************************************************************************
  Return the number of seconds until turn-done.  Don't call this unless
  game.info.timeout != 0.
**************************************************************************/
int get_seconds_to_turndone(void)
{
  if (game.info.timeout > 0) {
    return seconds_shown_to_turndone;
  } else {
    /* This shouldn't happen. */
    return FC_INFINITY;
  }
}

/**************************************************************************
  This function should be called at least once per second.  It does various
  updates (idle animations and timeout updates).  It returns the number of
  seconds until it should be called again.
**************************************************************************/
double real_timer_callback(void)
{
  double time_until_next_call = 1.0;

  {
    double autoconnect_time = try_to_autoconnect();
    time_until_next_call = MIN(time_until_next_call, autoconnect_time);
  }

  if (get_client_state() != CLIENT_GAME_RUNNING_STATE) {
    return time_until_next_call;
  }

  {
    double blink_time = blink_turn_done_button();

    time_until_next_call = MIN(time_until_next_call, blink_time);
  }

  if (get_num_units_in_focus() > 0) {
    double blink_time = blink_active_unit();

    time_until_next_call = MIN(time_until_next_call, blink_time);
  }

  /* It is possible to have game.info.timeout > 0 but !turndone_timer, in the
   * first moments after the timeout is set. */
  if (game.info.timeout > 0 && turndone_timer) {
    double seconds = seconds_to_turndone - read_timer_seconds(turndone_timer);
    int iseconds = ceil(seconds) + 0.1; /* Turn should end right on 0. */

    if (iseconds < seconds_shown_to_turndone) {
      seconds_shown_to_turndone = iseconds;
      update_timeout_label();
    }

    time_until_next_call = MIN(time_until_next_call,
			       seconds - floor(seconds) + 0.001);
  }

  /* Make sure we wait at least 50 ms, otherwise we may not give any other
   * code time to run. */
  return MAX(time_until_next_call, 0.05);
}

/**************************************************************************
  Returns TRUE if the client can issue orders (such as giving unit
  commands).  This function should be called each time before allowing the
  user to give an order.
**************************************************************************/
bool can_client_issue_orders(void)
{
  return (game.player_ptr
	  && !client_is_observer()
	  && get_client_state() == CLIENT_GAME_RUNNING_STATE);
}

/**************************************************************************
  Returns TRUE iff the client can do diplomatic meetings with another 
  given player.
**************************************************************************/
bool can_meet_with_player(const struct player *pplayer)
{
  return (can_client_issue_orders()
	  && game.player_ptr
	  && could_meet_with_player(game.player_ptr, pplayer));
}

/**************************************************************************
  Returns TRUE iff the client can get intelligence from another 
  given player.
**************************************************************************/
bool can_intel_with_player(const struct player *pplayer)
{
  return (client_is_observer()
	  || (game.player_ptr
	      && could_intel_with_player(game.player_ptr, pplayer)));
}

/**************************************************************************
  Return TRUE if the client can change the view; i.e. if the mapview is
  active.  This function should be called each time before allowing the
  user to do mapview actions.
**************************************************************************/
bool can_client_change_view(void)
{
  return ((game.player_ptr || client_is_observer())
	  && (get_client_state() == CLIENT_GAME_RUNNING_STATE
	      || get_client_state() == CLIENT_GAME_OVER_STATE));
}
