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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif
#ifdef HAVE_LIBREADLINE
#include <readline/history.h>
#include <readline/readline.h>
#endif
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_WINSOCK
#include <winsock.h>
#endif

/* utility */
#include "capability.h"
#include "fciconv.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "netintf.h"
#include "shared.h"
#include "support.h"
#include "timing.h"

/* common */
#include "dataio.h"
#include "events.h"
#include "game.h"
#include "packets.h"

/* server */
#include "aiiface.h"
#include "auth.h"
#include "connecthand.h"
#include "console.h"
#include "ggzserver.h"
#include "meta.h"
#include "plrhand.h"
#include "srv_main.h"
#include "stdinhand.h"
#include "voting.h"

#include "sernet.h"

static struct connection connections[MAX_NUM_CONNECTIONS];

#ifdef GENERATING_MAC      /* mac network globals */
TEndpointInfo serv_info;
EndpointRef serv_ep;
#else
static int *listen_socks;
static int listen_count;
static int socklan;
#endif

#if defined(__VMS)
#  if defined(_VAX_)
#    define lib$stop LIB$STOP
#    define sys$qiow SYS$QIOW
#    define sys$assign SYS$ASSIGN
#  endif
#  include <descrip.h>
#  include <iodef.h>
#  include <stsdef.h>
#  include <starlet.h>
#  include <lib$routines.h>
#  include <efndef.h>
   static unsigned long int tt_chan;
   static char input_char = 0;
   static char got_input = 0;
   void user_interrupt_callback();
#endif

#define SPECLIST_TAG timer
#define SPECLIST_TYPE struct timer
#include "speclist.h"
#define timer_list_iterate(ARG_list, NAME_item) \
  TYPED_LIST_ITERATE(struct timer, (ARG_list), NAME_item)
#define timer_list_iterate_end LIST_ITERATE_END

#define PROCESSING_TIME_STATISTICS 0

static int server_accept_connection(int sockfd);
static void start_processing_request(struct connection *pconn,
				     int request_id);
static void finish_processing_request(struct connection *pconn);
static void connection_ping(struct connection *pconn);
static void send_ping_times_to_all(void);

static void get_lanserver_announcement(void);
static void send_lanserver_response(void);

static bool no_input = FALSE;

/* Avoid compiler warning about defined, but unused function
 * by defining it only when needed */
#if defined(HAVE_LIBREADLINE) || \
    (!defined(SOCKET_ZERO_ISNT_STDIN) && !defined(HAVE_LIBREADLINE))  
/*****************************************************************************
  This happens if you type an EOF character with nothing on the current line.
*****************************************************************************/
static void handle_stdin_close(void)
{
  /* Note this function may be called even if SOCKET_ZERO_ISNT_STDIN, so
   * the preprocessor check has to come inside the function body.  But
   * perhaps we want to do this even when SOCKET_ZERO_ISNT_STDIN? */
#ifndef SOCKET_ZERO_ISNT_STDIN
  log_normal(_("Server cannot read standard input. Ignoring input."));
  no_input = TRUE;
#endif /* SOCKET_ZERO_ISNT_STDIN */
}

#endif /* HAVE_LIBREADLINE || (!SOCKET_ZERO_ISNT_STDIN && !HAVE_LIBREADLINE) */

#ifdef HAVE_LIBREADLINE
/****************************************************************************/

#define HISTORY_FILENAME  ".freeciv-server_history"
#define HISTORY_LENGTH    100

static char *history_file = NULL;

static bool readline_handled_input = FALSE;

static bool readline_initialized = FALSE;

/*****************************************************************************
  Readline callback for input.
*****************************************************************************/
static void handle_readline_input_callback(char *line)
{
  char *line_internal;

  if (no_input)
    return;

  if (!line) {
    handle_stdin_close();	/* maybe print an 'are you sure?' message? */
    return;
  }

  if (line[0] != '\0')
    add_history(line);

  con_prompt_enter();		/* just got an 'Enter' hit */
  line_internal = local_to_internal_string_malloc(line);
  (void) handle_stdin_input(NULL, line_internal, FALSE);
  free(line_internal);
  free(line);

  readline_handled_input = TRUE;
}
#endif /* HAVE_LIBREADLINE */

/****************************************************************************
  Close the connection (very low-level). See also
  server_conn_close_callback().
****************************************************************************/
static void close_connection(struct connection *pconn)
{
  if (!pconn) {
    return;
  }

  if (pconn->server.ping_timers != NULL) {
    timer_list_iterate(pconn->server.ping_timers, timer) {
      free_timer(timer);
    } timer_list_iterate_end;
    timer_list_destroy(pconn->server.ping_timers);
    pconn->server.ping_timers = NULL;
  }

  conn_pattern_list_destroy(pconn->server.ignore_list);
  pconn->server.ignore_list = NULL;

  /* safe to do these even if not in lists: */
  conn_list_remove(game.all_connections, pconn);
  conn_list_remove(game.est_connections, pconn);

  pconn->playing = NULL;
  pconn->access_level = ALLOW_NONE;
  connection_common_close(pconn);

  send_updated_vote_totals(NULL);
}

/*****************************************************************************
  Close all network stuff: connections, listening sockets, metaserver
  connection...
*****************************************************************************/
void close_connections_and_socket(void)
{
  int i;
  lsend_packet_server_shutdown(game.all_connections);

  for(i=0; i<MAX_NUM_CONNECTIONS; i++) {
    if(connections[i].used) {
      close_connection(&connections[i]);
    }
    conn_list_destroy(connections[i].self);
  }

  /* Remove the game connection lists and make sure they are empty. */
  conn_list_destroy(game.all_connections);
  conn_list_destroy(game.est_connections);

  for (i = 0; i < listen_count; i++) {
    fc_closesocket(listen_socks[i]);
  }
  fc_closesocket(socklan);

#ifdef HAVE_LIBREADLINE
  if (history_file) {
    write_history(history_file);
    history_truncate_file(history_file, HISTORY_LENGTH);
  }
#endif

  send_server_info_to_metaserver(META_GOODBYE);
  server_close_meta();

  fc_shutdown_network();
}

/****************************************************************************
  Now really close connections marked as 'is_closing'.
  Do this here to avoid recursive sending.
****************************************************************************/
static void really_close_connections(void)
{
  struct connection *closing[MAX_NUM_CONNECTIONS];
  struct connection *pconn;
  int i, num;

  do {
    num = 0;

    for (i = 0; i < MAX_NUM_CONNECTIONS; i++) {
      pconn = connections + i;
      if (pconn->used && pconn->server.is_closing) {
        closing[num++] = pconn;
        /* Remove closing connections from the lists (hard detach)
         * to avoid sending to closing connections. */
        conn_list_remove(game.est_connections, pconn);
        conn_list_remove(game.all_connections, pconn);
        if (NULL != conn_get_player(pconn)) {
          conn_list_remove(conn_get_player(pconn)->connections, pconn);
        }
      }
    }

    for (i = 0; i < num; i++) {
      /* Now really close them. */
      pconn = closing[i];
      lost_connection_to_client(pconn);
      close_connection(pconn);
    }
  } while (0 < num); /* May some errors occured, let's check. */
}

/****************************************************************************
  Break a client connection. You should almost always use
  connection_close_server() instead of calling this function directly.
****************************************************************************/
static void server_conn_close_callback(struct connection *pconn)
{
  /* Do as little as possible here to avoid recursive evil. */
  pconn->server.is_closing = TRUE;
}

/****************************************************************************
  If a connection lags too much this function is called and we try to cut
  it.
****************************************************************************/
static void cut_lagging_connection(struct connection *pconn)
{
  if (!pconn->server.is_closing
      && game.server.tcptimeout != 0
      && pconn->last_write
      && conn_list_size(game.all_connections) > 1
      && pconn->access_level != ALLOW_HACK
      && read_timer_seconds(pconn->last_write) > game.server.tcptimeout) {
    /* Cut the connections to players who lag too much.  This
     * usually happens because client animation slows the client
     * too much and it can't keep up with the server.  We don't
     * cut HACK connections, or cut in single-player games, since
     * it wouldn't help the game progress.  For other connections
     * the best thing to do when they lag too much is to be
     * disconnected and reconnect. */
    log_verbose("connection (%s) cut due to lagging player",
                conn_description(pconn));
    connection_close_server(pconn, _("lagging connection"));
  }
}

/****************************************************************************
  Attempt to flush all information in the send buffers for upto 'netwait'
  seconds.
*****************************************************************************/
void flush_packets(void)
{
  int i;
  int max_desc;
  fd_set writefs, exceptfs;
  struct timeval tv;
  time_t start;

  (void) time(&start);

  for(;;) {
    tv.tv_sec = (game.server.netwait - (time(NULL) - start));
    tv.tv_usec=0;

    if (tv.tv_sec < 0)
      return;

    FC_FD_ZERO(&writefs);
    FC_FD_ZERO(&exceptfs);
    max_desc=-1;

    for(i=0; i<MAX_NUM_CONNECTIONS; i++) {
      struct connection *pconn = &connections[i];
      if (pconn->used
          && !pconn->server.is_closing
          && 0 < pconn->send_buffer->ndata) {
	FD_SET(pconn->sock, &writefs);
	FD_SET(pconn->sock, &exceptfs);
	max_desc=MAX(pconn->sock, max_desc);
      }
    }

    if (max_desc == -1) {
      return;
    }

    if(fc_select(max_desc+1, NULL, &writefs, &exceptfs, &tv)<=0) {
      return;
    }

    for(i=0; i<MAX_NUM_CONNECTIONS; i++) {   /* check for freaky players */
      struct connection *pconn = &connections[i];
      if (pconn->used && !pconn->server.is_closing) {
        if(FD_ISSET(pconn->sock, &exceptfs)) {
          log_verbose("connection (%s) cut due to exception data",
                      conn_description(pconn));
          connection_close_server(pconn, _("network exception"));
        } else {
	  if(pconn->send_buffer && pconn->send_buffer->ndata > 0) {
	    if(FD_ISSET(pconn->sock, &writefs)) {
	      flush_connection_send_buffer_all(pconn);
	    } else {
	      cut_lagging_connection(pconn);
	    }
	  }
        }
      }
    }
  }
}

struct packet_to_handle {
  void *data;
  enum packet_type type;
};

/*****************************************************************************
  Simplify a loop by wrapping get_packet_from_connection.
*****************************************************************************/
static bool get_packet(struct connection *pconn, 
                       struct packet_to_handle *ppacket)
{
  ppacket->data = get_packet_from_connection(pconn, &ppacket->type);

  return NULL != ppacket->data;
}

/*****************************************************************************
  Handle all incoming packets on a client connection.
  Precondition - we have read_socket_data.
  Postcondition - there are no more packets to handle on this connection.
*****************************************************************************/
static void incoming_client_packets(struct connection *pconn) 
{
  struct packet_to_handle packet;
#if PROCESSING_TIME_STATISTICS
  struct timer *request_time = NULL;
#endif

  while (get_packet(pconn, &packet)) {
    bool command_ok;

#if PROCESSING_TIME_STATISTICS
    int request_id;

    request_time = renew_timer_start(request_time, TIMER_USER, TIMER_ACTIVE);
#endif /* PROCESSING_TIME_STATISTICS */

    pconn->server.last_request_id_seen
      = get_next_request_id(pconn->server.last_request_id_seen);

#if PROCESSING_TIME_STATISTICS
    request_id = pconn->server.last_request_id_seen;
#endif /* PROCESSING_TIME_STATISTICS */

    connection_do_buffer(pconn);
    start_processing_request(pconn, pconn->server.last_request_id_seen);

    command_ok = server_packet_input(pconn, packet.data, packet.type);
    free(packet.data);

    finish_processing_request(pconn);
    connection_do_unbuffer(pconn);

#if PROCESSING_TIME_STATISTICS
    log_verbose("processed request %d in %gms", request_id, 
                read_timer_seconds(request_time) * 1000.0);
#endif /* PROCESSING_TIME_STATISTICS */

    if (!command_ok) {
      connection_close_server(pconn, _("rejected"));
    }
  }

#if PROCESSING_TIME_STATISTICS
  free_timer(request_time);
#endif /* PROCESSING_TIME_STATISTICS */
}


/*****************************************************************************
Get and handle:
- new connections,
- input from connections,
- input from server operator in stdin

This function also handles prompt printing, via the con_prompt_*
functions.  That is, other functions should not need to do so.  --dwp
*****************************************************************************/
enum server_events server_sniff_all_input(void)
{
  int i, s;
  int max_desc;
  bool excepting;
  fd_set readfs, writefs, exceptfs;
  struct timeval tv;
#ifdef SOCKET_ZERO_ISNT_STDIN
  char *bufptr;    
#endif

  con_prompt_init();

#ifdef HAVE_LIBREADLINE
  {
    if (!no_input && !readline_initialized) {
      char *home_dir = user_home_dir();

      if (home_dir) {
	history_file
	  = fc_malloc(strlen(home_dir) + 1 + strlen(HISTORY_FILENAME) + 1);
	if (history_file) {
	  strcpy(history_file, home_dir);
	  strcat(history_file, "/");
	  strcat(history_file, HISTORY_FILENAME);
	  using_history();
	  read_history(history_file);
	}
      }

      rl_initialize();
      rl_callback_handler_install((char *) "> ",
				  handle_readline_input_callback);
      rl_attempted_completion_function = freeciv_completion;

      readline_initialized = TRUE;
      atexit(rl_callback_handler_remove);
    }
  }
#endif /* HAVE_LIBREADLINE */

  while (TRUE) {
    con_prompt_on();		/* accepting new input */
    
    if (force_end_of_sniff) {
      force_end_of_sniff = FALSE;
      con_prompt_off();
      return S_E_FORCE_END_OF_SNIFF;
    }

    get_lanserver_announcement();

    /* end server if no players for 'srvarg.quitidle' seconds,
     * but only if at least one player has previously connected. */
    if (srvarg.quitidle != 0) {
      static time_t last_noplayers;
      static bool connections;

      if (conn_list_size(game.est_connections) > 0) {
	connections = TRUE;
      }
      if (connections && conn_list_size(game.est_connections) == 0) {
	if (last_noplayers != 0) {
	  if (time(NULL) > last_noplayers + srvarg.quitidle) {
	    save_game_auto("Lost all connections", AS_QUITIDLE);
            log_normal(_("Restarting for lack of players."));
            set_meta_message_string("restarting for lack of players");
	    (void) send_server_info_to_metaserver(META_INFO);

            set_server_state(S_S_OVER);
            force_end_of_sniff = TRUE;

	    if (srvarg.exit_on_end) {
	      /* No need for anything more; just quit. */
	      server_quit();
	    }

            /* Do not restart before someone has connected and left again */
            connections = FALSE;
	  }
	} else {
	  last_noplayers = time(NULL);

          log_normal(_("Restarting in %d seconds for lack of players."),
                     srvarg.quitidle);

          set_meta_message_string(N_("restarting soon for lack of players"));
	  (void) send_server_info_to_metaserver(META_INFO);
	}
      } else {
        last_noplayers = 0;
      }
    }

    /* Pinging around for statistics */
    if (time(NULL) > (game.server.last_ping + game.server.pingtime)) {
      /* send data about the previous run */
      send_ping_times_to_all();

      conn_list_iterate(game.all_connections, pconn) {
        if ((!pconn->server.is_closing
             && 0 < timer_list_size(pconn->server.ping_timers)
	     && read_timer_seconds(timer_list_get(pconn->server.ping_timers, 0))
	        > game.server.pingtimeout) 
            || pconn->ping_time > game.server.pingtimeout) {
          /* cut mute players, except for hack-level ones */
          if (pconn->access_level == ALLOW_HACK) {
            log_verbose("connection (%s) [hack-level] ping timeout ignored",
                        conn_description(pconn));
          } else {
            log_verbose("connection (%s) cut due to ping timeout",
                        conn_description(pconn));
            connection_close_server(pconn, _("ping timeout"));
          }
        } else {
          connection_ping(pconn);
        }
      } conn_list_iterate_end;
      game.server.last_ping = time(NULL);
    }

    /* if we've waited long enough after a failure, respond to the client */
    conn_list_iterate(game.all_connections, pconn) {
      if (srvarg.auth_enabled
          && !pconn->server.is_closing
          && pconn->server.status != AS_ESTABLISHED) {
        auth_process_status(pconn);
      }
    } conn_list_iterate_end

    /* Don't wait if timeout == -1 (i.e. on auto games) */
    if (S_S_RUNNING == server_state() && game.info.timeout == -1) {
      call_ai_refresh();
      (void) send_server_info_to_metaserver(META_REFRESH);
      return S_E_END_OF_TURN_TIMEOUT;
    }

    tv.tv_sec = 1;
    tv.tv_usec = 0;

    FC_FD_ZERO(&readfs);
    FC_FD_ZERO(&writefs);
    FC_FD_ZERO(&exceptfs);

    if (!no_input) {
#ifdef SOCKET_ZERO_ISNT_STDIN
      fc_init_console();
#else /* SOCKET_ZERO_ISNT_STDIN */
#   if !defined(__VMS)
      FD_SET(0, &readfs);
#   endif /* VMS */	
#endif /* SOCKET_ZERO_ISNT_STDIN */
    }

    max_desc = 0;
    for (i = 0; i < listen_count; i++) {
      FD_SET(listen_socks[i], &readfs);
      FD_SET(listen_socks[i], &exceptfs);
      max_desc = MAX(max_desc, listen_socks[i]);
    }

    if (with_ggz) {
#ifdef GGZ_SERVER
      int ggz_sock = get_ggz_socket();

      FD_SET(ggz_sock, &readfs);
      max_desc = MAX(max_desc, ggz_sock);
#endif /* GGZ_SERVER */
    }

    for (i = 0; i < MAX_NUM_CONNECTIONS; i++) {
      struct connection *pconn = connections + i;
      if (pconn->used && !pconn->server.is_closing) {
        FD_SET(pconn->sock, &readfs);
        if (0 < pconn->send_buffer->ndata) {
          FD_SET(pconn->sock, &writefs);
        }
        FD_SET(pconn->sock, &exceptfs);
        max_desc = MAX(pconn->sock, max_desc);
      }
    }
    con_prompt_off();		/* output doesn't generate a new prompt */

    if (fc_select(max_desc + 1, &readfs, &writefs, &exceptfs, &tv) == 0) {
      /* timeout */
      call_ai_refresh();
      (void) send_server_info_to_metaserver(META_REFRESH);
      if (game.info.timeout > 0
	  && S_S_RUNNING == server_state()
	  && game.server.phase_timer
	  && (read_timer_seconds(game.server.phase_timer)
	      > game.info.seconds_to_phasedone)) {
	con_prompt_off();
	return S_E_END_OF_TURN_TIMEOUT;
      }

      if (!no_input) {
#if defined(__VMS)
	{
	  struct {
	    short numchars;
	    char firstchar;
	    char reserved;
	    int reserved2;
	  } ttchar;
	  unsigned long status;

	  status = sys$qiow(EFN$C_ENF, tt_chan,
			    IO$_SENSEMODE | IO$M_TYPEAHDCNT, 0, 0, 0,
			    &ttchar, sizeof(ttchar), 0, 0, 0, 0);
	  if (!$VMS_STATUS_SUCCESS(status)) {
	    lib$stop(status);
	  }
	  if (ttchar.numchars) {
	    FD_SET(0, &readfs);
	  } else {
	    continue;
	  }
	}
#else  /* !__VMS */
#ifndef SOCKET_ZERO_ISNT_STDIN
        really_close_connections();
        continue;
#endif /* SOCKET_ZERO_ISNT_STDIN */
#endif /* !__VMS */
      }
    }

    if (!with_ggz) { /* No listening socket when using GGZ. */
      excepting = FALSE;
      for (i = 0; i < listen_count; i++) {
        if (FD_ISSET(listen_socks[i], &exceptfs)) {
          excepting = TRUE;
          break;
        }
      }
      if (excepting) {                  /* handle Ctrl-Z suspend/resume */
	continue;
      }
      for (i = 0; i < listen_count; i++) {
        s = listen_socks[i];
        if (FD_ISSET(s, &readfs)) {     /* new players connects */
          log_verbose("got new connection");
          if (-1 == server_accept_connection(s)) {
            /* There will be a log_error() message from
             * server_accept_connection() if something
             * goes wrong, so no need to make another
             * error-level message here. */
            log_verbose("failed accepting connection");
          }
        }
      }
    }
    for (i = 0; i < MAX_NUM_CONNECTIONS; i++) {
      /* check for freaky players */
      struct connection *pconn = &connections[i];

      if (pconn->used
          && !pconn->server.is_closing
          && FD_ISSET(pconn->sock, &exceptfs)) {
        log_verbose("connection (%s) cut due to exception data",
                    conn_description(pconn));
        connection_close_server(pconn, _("network exception"));
      }
    }
#ifdef GGZ_SERVER
    if (with_ggz) {
      /* This is intentionally after all the player socket handling because
       * it may cut a client. */
      int ggz_sock = get_ggz_socket();

      if (FD_ISSET(ggz_sock, &readfs)) {
	input_from_ggz(ggz_sock);
      }
    }
#endif /* GGZ_SERVER */
    
#ifdef SOCKET_ZERO_ISNT_STDIN
    if (!no_input && (bufptr = fc_read_console())) {
      char *bufptr_internal = local_to_internal_string_malloc(bufptr);

      con_prompt_enter();	/* will need a new prompt, regardless */
      handle_stdin_input(NULL, bufptr_internal, FALSE);
      free(bufptr_internal);
    }
#else  /* !SOCKET_ZERO_ISNT_STDIN */
    if(!no_input && FD_ISSET(0, &readfs)) {    /* input from server operator */
#ifdef HAVE_LIBREADLINE
      rl_callback_read_char();
      if (readline_handled_input) {
	readline_handled_input = FALSE;
	con_prompt_enter_clear();
      }
      continue;
#else  /* !HAVE_LIBREADLINE */
      ssize_t didget;
      char *buffer = NULL; /* Must be NULL when calling getline() */
      char *buf_internal;

#ifdef HAVE_GETLINE
      size_t len = 0;

      didget = getline(&buffer, &len, stdin);
      if (didget >= 1) {
        buffer[didget-1] = '\0'; /* overwrite newline character */
        didget--;
        log_debug("Got line: \"%s\" (%ld, %ld)", buffer,
                  (long int) didget, (long int) len);
      }
#else  /* HAVE_GETLINE */
      buffer = malloc(BUF_SIZE + 1);

      didget = read(0, buffer, BUF_SIZE);
      if (didget < 0) {
        didget = 0; /* Avoid buffer underrun below. */
      }
      *(buffer+didget)='\0';
#endif /* HAVE_GETLINE */
      if (didget <= 0) {
        handle_stdin_close();
      }

      con_prompt_enter();	/* will need a new prompt, regardless */

      if (didget >= 0) {
        buf_internal = local_to_internal_string_malloc(buffer);
        handle_stdin_input(NULL, buf_internal, FALSE);
        free(buf_internal);
      }
      free(buffer);
#endif /* !HAVE_LIBREADLINE */
    } else
#endif /* !SOCKET_ZERO_ISNT_STDIN */
     
    {                             /* input from a player */
      for(i = 0; i < MAX_NUM_CONNECTIONS; i++) {
        struct connection *pconn = connections + i;
        int nb;

        if (!pconn->used
            || pconn->server.is_closing
            || !FD_ISSET(pconn->sock, &readfs)) {
          continue;
	}

        nb = read_socket_data(pconn->sock, pconn->buffer);
        if (0 <= nb) {
          /* We read packets; now handle them. */
          incoming_client_packets(pconn);
        } else if (-2 == nb) {
          connection_close_server(pconn, _("client disconnected"));
        } else {
          /* Read failure; the connection is closed. */
          connection_close_server(pconn, _("read error"));
        }
      }

      for (i = 0; i < MAX_NUM_CONNECTIONS; i++) {
        struct connection *pconn = &connections[i];

        if (pconn->used
            && !pconn->server.is_closing
            && pconn->send_buffer
	    && pconn->send_buffer->ndata > 0) {
	  if (FD_ISSET(pconn->sock, &writefs)) {
	    flush_connection_send_buffer_all(pconn);
	  } else {
	    cut_lagging_connection(pconn);
	  }
        }
      }
    }
    really_close_connections();
    break;
  }
  con_prompt_off();

  call_ai_refresh();

  if (game.info.timeout > 0
      && S_S_RUNNING == server_state()
      && game.server.phase_timer
      && (read_timer_seconds(game.server.phase_timer)
          > game.info.seconds_to_phasedone)) {
    return S_E_END_OF_TURN_TIMEOUT;
  }
  return S_E_OTHERWISE;
}

/********************************************************************
  Make up a name for the connection, before we get any data from
  it to use as a sensible name.  Name will be 'c' + integer,
  guaranteed not to be the same as any other connection name,
  nor player name nor user name, nor connection id (avoid possible
  confusions).   Returns pointer to static buffer, and fills in
  (*id) with chosen value.
********************************************************************/
static const char *makeup_connection_name(int *id)
{
  static unsigned short i = 0;
  static char name[MAX_LEN_NAME];

  for(;;) {
    if (i==(unsigned short)-1) i++;              /* don't use 0 */
    fc_snprintf(name, sizeof(name), "c%u", (unsigned int)++i);
    if (NULL == player_by_name(name)
        && NULL == player_by_user(name)
        && NULL == conn_by_number(i)
        && NULL == conn_by_user(name)) {
      *id = i;
      return name;
    }
  }
}
  
/********************************************************************
  Server accepts connection from client:
  Low level socket stuff, and basic-initialize the connection struct.
  Returns 0 on success, -1 on failure (bad accept(), or too many
  connections).
********************************************************************/
static int server_accept_connection(int sockfd)
{
  /* This used to have size_t for some platforms.  If this is necessary
   * it should be done with a configure check not a platform check. */
  socklen_t fromlen;

  int new_sock;
  union fc_sockaddr fromend;
  bool nameinfo = FALSE;
#ifdef IPV6_SUPPORT
  char host[NI_MAXHOST], service[NI_MAXSERV];
  char dst[INET6_ADDRSTRLEN];
#else  /* IPv6 support */
  struct hostent *from;
  const char *host = NULL;
  const char *dst;
#endif /* IPv6 support */

  fromlen = sizeof(fromend);

  if ((new_sock = accept(sockfd, &fromend.saddr, &fromlen)) == -1) {
    log_error("accept failed: %s", fc_strerror(fc_get_errno()));
    return -1;
  }

#ifdef IPV6_SUPPORT
  if (fromend.saddr.sa_family == AF_INET6) {
    inet_ntop(AF_INET6, &fromend.saddr_in6.sin6_addr,
              dst, sizeof(dst));
  } else if (fromend.saddr.sa_family == AF_INET) {
    inet_ntop(AF_INET, &fromend.saddr_in4.sin_addr, dst, sizeof(dst));
  } else {
    fc_assert(FALSE);

    log_error("Unsupported address family in server_accept_connection()");

    return -1;
  }
#else  /* IPv6 support */
  dst = inet_ntoa(fromend.saddr_in4.sin_addr);
#endif /* IPv6 support */

  if (0 != game.server.maxconnectionsperhost) {
    int count = 0;

    conn_list_iterate(game.all_connections, pconn) {
      if (0 != strcmp(dst, pconn->server.ipaddr)) {
        continue;
      }
      if (++count >= game.server.maxconnectionsperhost) {
        log_verbose("Rejecting new connection from %s: maximum number of "
                    "connections for this address exceeded (%d).",
                    dst, game.server.maxconnectionsperhost);

        /* Disconnect the accepted socket. */
        fc_closesocket(new_sock);

        return -1;
      }
    } conn_list_iterate_end;
  }

#ifdef IPV6_SUPPORT
  nameinfo = (0 == getnameinfo(&fromend.saddr, fromlen, host, NI_MAXHOST,
                               service, NI_MAXSERV, NI_NUMERICSERV)
              && '\0' != host[0]);
#else  /* IPv6 support */
  from = gethostbyaddr((char *) &fromend.saddr_in4.sin_addr,
                       sizeof(fromend.saddr_in4.sin_addr), AF_INET);
  if (NULL != from && '\0' != from->h_name[0]) {
    host = from->h_name;
    nameinfo = TRUE;
  }
#endif /* IPv6 support */

  return server_make_connection(new_sock,
                                (nameinfo ? host : dst), dst);
}

/********************************************************************
  Server accepts connection from client:
  Low level socket stuff, and basic-initialize the connection struct.
  Returns 0 on success, -1 on failure (bad accept(), or too many
  connections).
********************************************************************/
int server_make_connection(int new_sock, const char *client_addr, const char *client_ip)
{
  int i;

  fc_nonblock(new_sock);

  for(i=0; i<MAX_NUM_CONNECTIONS; i++) {
    struct connection *pconn = &connections[i];
    if (!pconn->used) {
      connection_common_init(pconn);
      pconn->sock = new_sock;
      pconn->observer = FALSE;
      pconn->playing = NULL;
      pconn->capability[0] = '\0';
      pconn->access_level = access_level_for_next_connection();
      pconn->notify_of_writable_data = NULL;
      pconn->server.currently_processed_request_id = 0;
      pconn->server.last_request_id_seen = 0;
      pconn->server.auth_tries = 0;
      pconn->server.auth_settime = 0;
      pconn->server.status = AS_NOT_ESTABLISHED;
      pconn->server.ping_timers = timer_list_new();
      pconn->server.granted_access_level = pconn->access_level;
      pconn->server.ignore_list =
          conn_pattern_list_new_full(conn_pattern_destroy);
      pconn->server.is_closing = FALSE;
      pconn->ping_time = -1.0;
      pconn->incoming_packet_notify = NULL;
      pconn->outgoing_packet_notify = NULL;

      sz_strlcpy(pconn->username, makeup_connection_name(&pconn->id));
      sz_strlcpy(pconn->addr, client_addr);
      sz_strlcpy(pconn->server.ipaddr, client_ip);

      conn_list_append(game.all_connections, pconn);

      log_verbose("connection (%s) from %s (%s)", 
                  pconn->username, pconn->addr, pconn->server.ipaddr);
      connection_ping(pconn);
      return 0;
    }
  }

  log_error("maximum number of connections reached");
  fc_closesocket(new_sock);
  return -1;
}

/********************************************************************
 open server socket to be used to accept client connections
 and open a server socket for server LAN announcements.
********************************************************************/
int server_open_socket(void)
{
  /* setup socket address */
  union fc_sockaddr addr;
  struct ip_mreq mreq4;
  const char *cause, *group;
  int j, on, s;
  int lan_family;
  struct fc_sockaddr_list *list;
  int name_count;

#ifdef IPV6_SUPPORT
  struct ipv6_mreq mreq6;
#endif

#ifdef IPV6_SUPPORT
  list = net_lookup_service(srvarg.bind_addr, srvarg.port, FC_ADDR_ANY);
#else  /* IPV6_SUPPORT */
  list = net_lookup_service(srvarg.bind_addr, srvarg.port, FC_ADDR_IPV4);
#endif /* IPV6_SUPPORT */

  name_count = fc_sockaddr_list_size(list);

  /* Lookup addresses to bind. */
  if (name_count <= 0) {
    log_fatal(_("Server: bad address: <%s:%d>."),
              srvarg.bind_addr, srvarg.port);
    exit(EXIT_FAILURE);
  }

  cause = NULL;
  on = 1;

  /* Loop to create sockets, bind, listen. */
  listen_socks = fc_calloc(name_count, sizeof(listen_socks[0]));
  listen_count = 0;

  fc_sockaddr_list_iterate(list, paddr) {
    /* Create socket for client connections. */
    s = socket(paddr->saddr.sa_family, SOCK_STREAM, 0);
    if (s == -1) {
      /* Probably EAFNOSUPPORT or EPROTONOSUPPORT.
       * Kernel might have disabled AF_INET6. */
      cause = "socket";
      continue;
    }

    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, 
                   (char *)&on, sizeof(on)) == -1) {
      log_error("setsockopt SO_REUSEADDR failed: %s",
                fc_strerror(fc_get_errno()));
      sockaddr_debug(paddr);
    }

    /* AF_INET6 sockets should use IPv6 only,
     * without stealing IPv4 from AF_INET sockets. */
#ifdef IPV6_SUPPORT
    if (paddr->saddr.sa_family == AF_INET6) {
#ifdef IPV6_V6ONLY
      if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY,
                     (char *)&on, sizeof(on)) == -1) {
        log_error("setsockopt IPV6_V6ONLY failed: %s",
                  fc_strerror(fc_get_errno()));
        sockaddr_debug(paddr);
      }
#endif /* IPV6_V6ONLY */
    }
#endif /* IPv6 support */

    if (bind(s, &paddr->saddr, sockaddr_size(paddr)) == -1) {
      cause = "bind";

      if (fc_get_errno() == EADDRNOTAVAIL) {
        /* Close only this socket. This address is not available.
         * This can happen with the IPv6 wildcard address if this
         * machine has no IPv6 interfaces. */
        fc_closesocket(s);
        continue;
      } else {
        /* Close all sockets. Another program might have bound to
         * one of our addresses, and might hijack some of our
         * connections. */
        fc_closesocket(s);
        for (j = 0; j < listen_count; j++) {
          fc_closesocket(listen_socks[j]);
        }
        listen_count = 0;
        break;
      }
    }

    if (listen(s, MAX_NUM_CONNECTIONS) == -1) {
      cause = "listen";
      fc_closesocket(s);
      continue;
    }
    listen_socks[listen_count] = s;
    listen_count++;
  } fc_sockaddr_list_iterate_end;

  if (listen_count == 0) {
    log_fatal("%s failed: %s", cause, fc_strerror(fc_get_errno()));
    fc_sockaddr_list_iterate(list, paddr) {
      sockaddr_debug(paddr);
    } fc_sockaddr_list_iterate_end;
    exit(EXIT_FAILURE);
  }

  fc_sockaddr_list_destroy(list);

  connections_set_close_callback(server_conn_close_callback);

  if (srvarg.announce == ANNOUNCE_NONE) {
    return 0;
  }

#ifdef IPV6_SUPPORT
  if (srvarg.announce == ANNOUNCE_IPV6) {
    lan_family = AF_INET6;
  } else
#endif /* IPV6 support */
  {
    lan_family = AF_INET;
  }

  /* Create socket for server LAN announcements */
  if ((socklan = socket(lan_family, SOCK_DGRAM, 0)) < 0) {
    log_error("socket failed: %s", fc_strerror(fc_get_errno()));
  }

  if (setsockopt(socklan, SOL_SOCKET, SO_REUSEADDR,
                 (char *)&on, sizeof(on)) == -1) {
    log_error("SO_REUSEADDR failed: %s", fc_strerror(fc_get_errno()));
  }

  fc_nonblock(socklan);

  group = get_multicast_group(srvarg.announce == ANNOUNCE_IPV6);

  memset(&addr, 0, sizeof(addr));

  addr.saddr.sa_family = lan_family;

#ifdef IPV6_SUPPORT
  if (addr.saddr.sa_family == AF_INET6) {
    addr.saddr_in6.sin6_family = AF_INET6;
    addr.saddr_in6.sin6_port = htons(SERVER_LAN_PORT);
    addr.saddr_in6.sin6_addr = in6addr_any;
  } else
#endif /* IPv6 support */
  if (addr.saddr.sa_family == AF_INET) {
    addr.saddr_in4.sin_family = AF_INET;
    addr.saddr_in4.sin_port = htons(SERVER_LAN_PORT);
    addr.saddr_in4.sin_addr.s_addr = htonl(INADDR_ANY);
  } else {
    fc_assert(FALSE);

    log_error("Unsupported address family in server_open_socket()");
  }

  if (bind(socklan, &addr.saddr, sockaddr_size(&addr)) < 0) {
    log_error("Lan bind failed: %s", fc_strerror(fc_get_errno()));
  }

#ifndef IPV6_SUPPORT
  if (addr.saddr.sa_family == AF_INET) {
#ifdef HAVE_INET_ATON
    inet_aton(group, &mreq4.imr_multiaddr);
#else  /* HEVE_INET_ATON */
    mreq4.imr_multiaddr.s_addr = inet_addr(group);
#endif /* HAVE_INET_ATON */
#else  /* IPv6 support */
  if (addr.saddr.sa_family == AF_INET6) {
    inet_pton(AF_INET6, group, &mreq6.ipv6mr_multiaddr.s6_addr);
    mreq6.ipv6mr_interface = 0; /* TODO: Interface selection */
    if (setsockopt(socklan, IPPROTO_IPV6, FC_IPV6_ADD_MEMBERSHIP,
                   (const char*)&mreq6, sizeof(mreq6)) < 0) {
      log_error("FC_IPV6_ADD_MEMBERSHIP (%s) failed: %s",
                group, fc_strerror(fc_get_errno()));
    }
  } else if (addr.saddr.sa_family == AF_INET) {
    inet_pton(AF_INET, group, &mreq4.imr_multiaddr.s_addr);
#endif /* IPv6 support */
    mreq4.imr_interface.s_addr = htonl(INADDR_ANY);

    if (setsockopt(socklan, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   (const char*)&mreq4, sizeof(mreq4)) < 0) {
      log_error("IP_ADD_MEMBERSHIP (%s) failed: %s",
                group, fc_strerror(fc_get_errno()));
    }
  } else {
    fc_assert(FALSE);

    log_error("Unsupported address family for broadcasting.");
  }

  return 0;
}


/********************************************************************
  Initialize connection related stuff. Attention: Logging is not
  available within this functions!
********************************************************************/
void init_connections(void)
{
  int i;

  game.all_connections = conn_list_new();
  game.est_connections = conn_list_new();

  for(i=0; i<MAX_NUM_CONNECTIONS; i++) { 
    struct connection *pconn = &connections[i];
    pconn->used = FALSE;
    pconn->self = conn_list_new();
    conn_list_prepend(pconn->self, pconn);
  }
#if defined(__VMS)
  {
    unsigned long status;
    $DESCRIPTOR (tt_desc, "SYS$INPUT");
    status = sys$assign(&tt_desc,&tt_chan,0,0);
    if (!$VMS_STATUS_SUCCESS(status)) lib$stop(status);
  }
#endif /* VMS */
}

/**************************************************************************
  Starts processing of request packet from client.
**************************************************************************/
static void start_processing_request(struct connection *pconn,
                                     int request_id)
{
  fc_assert_ret(request_id);
  fc_assert_ret(pconn->server.currently_processed_request_id == 0);
  log_debug("start processing packet %d from connection %d",
            request_id, pconn->id);
  conn_compression_freeze(pconn);
  send_packet_processing_started(pconn);
  pconn->server.currently_processed_request_id = request_id;
}

/**************************************************************************
  Finish processing of request packet from client.
**************************************************************************/
static void finish_processing_request(struct connection *pconn)
{
  if (!pconn || !pconn->used) {
    return;
  }
  fc_assert_ret(pconn->server.currently_processed_request_id);
  log_debug("finish processing packet %d from connection %d",
            pconn->server.currently_processed_request_id, pconn->id);
  send_packet_processing_finished(pconn);
  pconn->server.currently_processed_request_id = 0;
  conn_compression_thaw(pconn);
}

/****************************************************************************
  Ping a connection.
****************************************************************************/
static void connection_ping(struct connection *pconn)
{
  log_debug("sending ping to %s (open=%d)", conn_description(pconn),
            timer_list_size(pconn->server.ping_timers));
  timer_list_append(pconn->server.ping_timers,
                    new_timer_start(TIMER_USER, TIMER_ACTIVE));
  send_packet_conn_ping(pconn);
}

/**************************************************************************
  Handle response to ping.
**************************************************************************/
void handle_conn_pong(struct connection *pconn)
{
  struct timer *timer;

  if (timer_list_size(pconn->server.ping_timers) == 0) {
    log_error("got unexpected pong from %s", conn_description(pconn));
    return;
  }

  timer = timer_list_get(pconn->server.ping_timers, 0);
  timer_list_remove(pconn->server.ping_timers, timer);
  pconn->ping_time = read_timer_seconds(timer);
  free_timer(timer);
  log_debug("got pong from %s (open=%d); ping time = %fs",
            conn_description(pconn),
            timer_list_size(pconn->server.ping_timers), pconn->ping_time);
}

/**************************************************************************
  Send ping time info about all connections to all connections.
**************************************************************************/
static void send_ping_times_to_all(void)
{
  struct packet_conn_ping_info packet;
  int i;

  i = 0;
  conn_list_iterate(game.est_connections, pconn) {
    if (!pconn->used) {
      continue;
    }
    fc_assert(i < ARRAY_SIZE(packet.conn_id));
    packet.conn_id[i] = pconn->id;
    packet.ping_time[i] = pconn->ping_time;
    i++;
  } conn_list_iterate_end;
  packet.connections = i;

  lsend_packet_conn_ping_info(game.est_connections, &packet);
}

/********************************************************************
  Listen for UDP packets multicasted from clients requesting
  announcement of servers on the LAN.
********************************************************************/
static void get_lanserver_announcement(void)
{
  char msgbuf[128];
  struct data_in din;
  int type;
  fd_set readfs, exceptfs;
  struct timeval tv;

  if (with_ggz) {
    return;
  }

  FD_ZERO(&readfs);
  FD_ZERO(&exceptfs);
  FD_SET(socklan, &exceptfs);
  FD_SET(socklan, &readfs);

  tv.tv_sec = 0;
  tv.tv_usec = 0;

  while (fc_select(socklan + 1, &readfs, NULL, &exceptfs, &tv) == -1) {
    if (errno != EINTR) {
      log_error("select failed: %s", fc_strerror(fc_get_errno()));
      return;
    }
    /* EINTR can happen sometimes, especially when compiling with -pg.
     * Generally we just want to run select again. */
  }

  if (FD_ISSET(socklan, &readfs)) {
    if (0 < recvfrom(socklan, msgbuf, sizeof(msgbuf), 0, NULL, NULL)) {
      dio_input_init(&din, msgbuf, 1);
      dio_get_uint8(&din, &type);
      if (type == SERVER_LAN_VERSION) {
        log_debug("Received request for server LAN announcement.");
        send_lanserver_response();
      } else {
        log_debug("Received invalid request for server LAN announcement.");
      }
    }
  }
}

/********************************************************************
  This function broadcasts an UDP packet to clients with
  that requests information about the server state.
********************************************************************/
static void send_lanserver_response(void)
{
#ifndef HAVE_WINSOCK
  unsigned char buffer[MAX_LEN_PACKET];
#else  /* HAVE_WINSOCK */
  char buffer[MAX_LEN_PACKET];
#endif /* HAVE_WINSOCK */
  char hostname[512];
  char port[256];
  char version[256];
  char players[256];
  int nhumans;
  char humans[256];
  char status[256];
  struct data_out dout;
  union fc_sockaddr addr;
  int socksend, setting = 1;
  const char *group;
  size_t size;
#ifndef HAVE_WINSOCK
  unsigned char ttl;
#endif

  /* Create a socket to broadcast to client. */
  if ((socksend = socket(AF_INET,SOCK_DGRAM, 0)) < 0) {
    log_error("socket failed: %s", fc_strerror(fc_get_errno()));
    return;
  }

  /* Set the UDP Multicast group IP address of the packet. */
  group = get_multicast_group(srvarg.announce == ANNOUNCE_IPV6);
  memset(&addr, 0, sizeof(addr));
  addr.saddr_in4.sin_family = AF_INET;
  addr.saddr_in4.sin_addr.s_addr = inet_addr(group);
  addr.saddr_in4.sin_port = htons(SERVER_LAN_PORT + 1);

/* this setsockopt call fails on Windows 98, so we stick with the default
 * value of 1 on Windows, which should be fine in most cases */
#ifndef HAVE_WINSOCK
  /* Set the Time-to-Live field for the packet.  */
  ttl = SERVER_LAN_TTL;
  if (setsockopt(socksend, IPPROTO_IP, IP_MULTICAST_TTL, 
                 (const char*)&ttl, sizeof(ttl))) {
    log_error("setsockopt failed: %s", fc_strerror(fc_get_errno()));
    return;
  }
#endif /* HAVE_WINSOCK */

  if (setsockopt(socksend, SOL_SOCKET, SO_BROADCAST, 
                 (const char*)&setting, sizeof(setting))) {
    log_error("setsockopt failed: %s", fc_strerror(fc_get_errno()));
    return;
  }

  /* Create a description of server state to send to clients.  */
  if (fc_gethostname(hostname, sizeof(hostname)) != 0) {
    sz_strlcpy(hostname, "none");
  }

  fc_snprintf(version, sizeof(version), "%d.%d.%d%s",
              MAJOR_VERSION, MINOR_VERSION, PATCH_VERSION, VERSION_LABEL);

  switch (server_state()) {
  case S_S_INITIAL:
    /* TRANS: Game state for local server */
    fc_snprintf(status, sizeof(status), _("Pregame"));
    break;
  case S_S_RUNNING:
    /* TRANS: Game state for local server */
    fc_snprintf(status, sizeof(status), _("Running"));
    break;
  case S_S_OVER:
    /* TRANS: Game state for local server */
    fc_snprintf(status, sizeof(status), _("Game over"));
    break;
  }

   fc_snprintf(players, sizeof(players), "%d",
               normal_player_count());

   nhumans = 0;
   players_iterate(pplayer) {
     if (pplayer->is_alive && !pplayer->ai_controlled) {
       nhumans++;
     }
   } players_iterate_end;
   fc_snprintf(humans, sizeof(humans), "%d", nhumans);

   fc_snprintf(port, sizeof(port), "%d",
              srvarg.port );

  dio_output_init(&dout, buffer, sizeof(buffer));
  dio_put_uint8(&dout, SERVER_LAN_VERSION);
  dio_put_string(&dout, hostname);
  dio_put_string(&dout, port);
  dio_put_string(&dout, version);
  dio_put_string(&dout, status);
  dio_put_string(&dout, players);
  dio_put_string(&dout, humans);
  dio_put_string(&dout, get_meta_message_string());
  size = dio_output_used(&dout);

  /* Sending packet to client with the information gathered above. */
  if (sendto(socksend, buffer,  size, 0, &addr.saddr,
      sockaddr_size(&addr)) < 0) {
    log_error("landserver response sendto failed: %s",
              fc_strerror(fc_get_errno()));
    return;
  }

  fc_closesocket(socksend);
}
