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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <assert.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#ifdef HAVE_WINSOCK
#include <winsock.h>
#endif

#ifdef HAVE_LIBREADLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "netintf.h"
#include "packets.h"
#include "plrhand.h"
#include "shared.h"
#include "support.h"
#include "capability.h"
#include "console.h"
#include "meta.h"
#include "srv_main.h"
#include "stdinhand.h"

#include "sernet.h"

static struct connection connections[MAX_NUM_CONNECTIONS];

#ifdef GENERATING_MAC      /* mac network globals */
TEndpointInfo serv_info;
EndpointRef serv_ep;
#else
static int sock;
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

#define PROCESSING_TIME_STATISTICS 0

static int server_accept_connection(int sockfd);
static void start_processing_request(struct connection *pconn,
				     int request_id);
static void finish_processing_request(struct connection *pconn);

static int no_input = 0;

/*****************************************************************************
  This happens if you type an EOF character with nothing on the current line.
*****************************************************************************/
static void handle_stdin_close(void)
{
  freelog(LOG_NORMAL, _("Server cannot read standard input. Ignoring input."));
  no_input = 1;
}

#ifdef HAVE_LIBREADLINE
/****************************************************************************/

#define HISTORY_FILENAME  ".civserver_history"
#define HISTORY_LENGTH    100

static char *history_file = NULL;

static int readline_handled_input = 0;

static int readline_initialized = 0;

/*****************************************************************************
...
*****************************************************************************/
static void handle_readline_input_callback(char *line)
{
  if (no_input)
    return;

  if (!line) {
    handle_stdin_close();	/* maybe print an 'are you sure?' message? */
    return;
  }

  if (*line)
    add_history(line);

  con_prompt_enter();		/* just got an 'Enter' hit */
  handle_stdin_input((struct connection*)NULL, line);

  readline_handled_input = 1;
}

#endif /* HAVE_LIBREADLINE */
/*****************************************************************************
...
*****************************************************************************/
void close_connection(struct connection *pconn)
{
  /* safe to do these even if not in lists: */
  conn_list_unlink(&game.all_connections, pconn);
  conn_list_unlink(&game.est_connections, pconn);
  conn_list_unlink(&game.game_connections, pconn);

  my_closesocket(pconn->sock);
  pconn->used = 0;
  pconn->established = 0;
  pconn->player = NULL;
  pconn->access_level = ALLOW_NONE;
  free_socket_packet_buffer(pconn->buffer);
  free_socket_packet_buffer(pconn->send_buffer);
  pconn->buffer = NULL;
  pconn->send_buffer = NULL;
}

/*****************************************************************************
...
*****************************************************************************/
void close_connections_and_socket(void)
{
  int i;

  struct packet_generic_message gen_packet;
  gen_packet.message[0]='\0';

  lsend_packet_generic_message(&game.all_connections, PACKET_SERVER_SHUTDOWN,
			       &gen_packet);

  for(i=0; i<MAX_NUM_CONNECTIONS; i++) {
    if(connections[i].used) {
      close_connection(&connections[i]);
    }
  }
  my_closesocket(sock);

#ifdef HAVE_LIBREADLINE
  if (history_file) {
    write_history(history_file);
    history_truncate_file(history_file, HISTORY_LENGTH);
  }
  if (readline_initialized) {
    rl_callback_handler_remove();
  }
#endif

  server_close_udp();

  my_shutdown_network();
}

/*****************************************************************************
  Used for errors and other strangeness.  As well as some direct uses, is
  passed to packet routines as callback for when packet sending has a write
  error.  Closes the connection cleanly, calling lost_connection_to_client()
  to clean up server variables, notify other clients, etc.
*****************************************************************************/
static void close_socket_callback(struct connection *pc)
{
  lost_connection_to_client(pc);
  close_connection(pc);
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

  time(&start);

  for(;;) {
    tv.tv_sec=(game.netwait - (time(NULL) - start));
    tv.tv_usec=0;

    if (tv.tv_sec < 0)
      return;

    MY_FD_ZERO(&writefs);
    MY_FD_ZERO(&exceptfs);
    max_desc=-1;

    for(i=0; i<MAX_NUM_CONNECTIONS; i++) {
      struct connection *pconn = &connections[i];
      if(pconn->used && pconn->send_buffer->ndata) {
	FD_SET(pconn->sock, &writefs);
	FD_SET(pconn->sock, &exceptfs);
	max_desc=MAX(pconn->sock, max_desc);
      }
    }

    if (max_desc == -1) {
      return;
    }

    if(select(max_desc+1, NULL, &writefs, &exceptfs, &tv)<=0) {
      return;
    }

    for(i=0; i<MAX_NUM_CONNECTIONS; i++) {   /* check for freaky players */
      struct connection *pconn = &connections[i];
      if(pconn->used) {
        if(FD_ISSET(pconn->sock, &exceptfs)) {
	  freelog(LOG_NORMAL, "cut connection %s due to exception data",
		  conn_description(pconn));
	  close_socket_callback(pconn);
        } else {
	  if(pconn->send_buffer && pconn->send_buffer->ndata) {
	    if(FD_ISSET(pconn->sock, &writefs)) {
	      flush_connection_send_buffer_all(pconn);
	    } else {
	      if(game.tcptimeout && pconn->last_write
		 && (time(NULL)>pconn->last_write + game.tcptimeout)) {
	        freelog(LOG_NORMAL, "cut connection %s due to lagging player",
			conn_description(pconn));
		close_socket_callback(pconn);
	      }
	    }
	  }
        }
      }
    }
  }
}

/*****************************************************************************
Get and handle:
- new connections,
- input from connections,
- input from server operator in stdin
Returns:
  0 if went past end-of-turn timeout
  2 if force_end_of_sniff found to be set
  1 otherwise (got and processed something?)
This function also handles prompt printing, via the con_prompt_*
functions.  That is, other functions should not need to do so.  --dwp
*****************************************************************************/
int sniff_packets(void)
{
  int i;
  int max_desc;
  fd_set readfs, writefs, exceptfs;
  struct timeval tv;
  static int year;
#ifdef SOCKET_ZERO_ISNT_STDIN
  char *bufptr;    
#endif

  con_prompt_init();

#ifdef HAVE_LIBREADLINE
  {
    if (!no_input && !readline_initialized) {
      char *home_dir = user_home_dir();
      if (home_dir) {
	history_file =
	  fc_malloc(strlen(home_dir) + 1 + strlen(HISTORY_FILENAME) + 1);
	if (history_file) {
	  strcpy(history_file, home_dir);
	  strcat(history_file, "/");
	  strcat(history_file, HISTORY_FILENAME);
	  using_history();
	  read_history(history_file);
	}
      }

      rl_initialize();
      rl_callback_handler_install("> ", handle_readline_input_callback);
      rl_attempted_completion_function = freeciv_completion;

      readline_initialized = 1;
    }
  }
#endif /* HAVE_LIBREADLINE */

  if(year!=game.year) {
    if (server_state == RUN_GAME_STATE) year=game.year;
  }
  if (!game.timeout)
    game.turn_start = time(NULL);
  
  while(1) {
    con_prompt_on();		/* accepting new input */
    
    if(force_end_of_sniff) {
      force_end_of_sniff=0;
      con_prompt_off();
      return 2;
    }


    /* quit server if no players for 'srvarg.quitidle' seconds */
    if (srvarg.quitidle && server_state != PRE_GAME_STATE) {
      static time_t last_noplayers;
      if(conn_list_size(&game.est_connections) == 0) {
	if (last_noplayers) {
	  if (time(NULL)>last_noplayers + srvarg.quitidle) {
	    sz_strlcpy(srvarg.metaserver_info_line,
		       "restarting for lack of players");
	    freelog(LOG_NORMAL, srvarg.metaserver_info_line);
	    send_server_info_to_metaserver(1,0);

	    close_connections_and_socket();
	    exit(0);
	  }
	} else {
	  last_noplayers = time(NULL);
	  
	  my_snprintf(srvarg.metaserver_info_line,
		      sizeof(srvarg.metaserver_info_line),
		      "restarting in %d seconds for lack of players",
		      srvarg.quitidle);
	  freelog(LOG_NORMAL, srvarg.metaserver_info_line);
	  send_server_info_to_metaserver(1,0);
	}
      } else {
        last_noplayers = 0;
      }
    }

    /* send PACKET_CONN_PING & cut mute players */
    if ((time(NULL)>game.last_ping + game.pingtimeout)) {
      for(i=0; i<MAX_NUM_CONNECTIONS; i++) {
	struct connection *pconn = &connections[i];
	if (pconn->used) {
	  send_packet_generic_empty(pconn, PACKET_CONN_PING);

	  if (pconn->ponged) {
	    pconn->ponged = 0;
	  } else {
	    freelog(LOG_NORMAL, "cut connection %s due to ping timeout",
		    conn_description(pconn));
	    close_socket_callback(pconn);
	  }
	}
      }
      game.last_ping = time(NULL);
    }

    /* Don't wait if timeout == -1 (i.e. on auto games) */
    if (server_state != PRE_GAME_STATE && game.timeout == -1) {
      send_server_info_to_metaserver(0, 0);

      /* kick out of the srv_main loop */
      if (server_state == GAME_OVER_STATE) {
	server_state = PRE_GAME_STATE;
      }
      return 0;
    }

    tv.tv_sec=1;
    tv.tv_usec=0;

    MY_FD_ZERO(&readfs);
    MY_FD_ZERO(&writefs);
    MY_FD_ZERO(&exceptfs);

    if (!no_input) {
#ifdef SOCKET_ZERO_ISNT_STDIN
      my_init_console();
#else
#   if !defined(__VMS)
      FD_SET(0, &readfs);
#   endif	
#endif
    }

    FD_SET(sock, &readfs);
    FD_SET(sock, &exceptfs);
    max_desc=sock;

    for(i=0; i<MAX_NUM_CONNECTIONS; i++) {
      if(connections[i].used) {
	FD_SET(connections[i].sock, &readfs);
	if(connections[i].send_buffer->ndata) {
	  FD_SET(connections[i].sock, &writefs);
	}
	FD_SET(connections[i].sock, &exceptfs);
        max_desc=MAX(connections[i].sock, max_desc);
      }
    }
    con_prompt_off();		/* output doesn't generate a new prompt */

    if(select(max_desc+1, &readfs, &writefs, &exceptfs, &tv)==0) { /* timeout */
      send_server_info_to_metaserver(0,0);
      if((game.timeout) 
	&& (time(NULL)>game.turn_start + game.timeout)
	&& (server_state == RUN_GAME_STATE)){
	con_prompt_off();
	return 0;
      }

      if (!no_input) {
#if defined(__VMS)
      {
	struct { short numchars; char firstchar; char reserved; int reserved2; } ttchar;
	unsigned long status;
	status = sys$qiow(EFN$C_ENF,tt_chan,IO$_SENSEMODE|IO$M_TYPEAHDCNT,0,0,0,
			  &ttchar,sizeof(ttchar),0,0,0,0);
	if (!$VMS_STATUS_SUCCESS(status)) lib$stop(status);
	if (ttchar.numchars)
	  FD_SET(0, &readfs);
	else
	  continue;
      }
#else  /* !__VMS */
#ifndef SOCKET_ZERO_ISNT_STDIN
      continue;
#endif /* SOCKET_ZERO_ISNT_STDIN */
#endif /* !__VMS */
      }
    }
    if (!game.timeout)
      game.turn_start = time(NULL);

    if(FD_ISSET(sock, &exceptfs)) {	     /* handle Ctrl-Z suspend/resume */
      continue;
    }
    if(FD_ISSET(sock, &readfs)) {	     /* new players connects */
      freelog(LOG_VERBOSE, "got new connection");
      if(server_accept_connection(sock)==-1) {
	freelog(LOG_ERROR, "failed accepting connection");
      }
    }
    for(i=0; i<MAX_NUM_CONNECTIONS; i++) {   /* check for freaky players */
      struct connection *pconn = &connections[i];
      if(pconn->used && FD_ISSET(pconn->sock, &exceptfs)) {
 	freelog(LOG_ERROR, "cut connection %s due to exception data",
		conn_description(pconn));
	close_socket_callback(pconn);
      }
    }
    
#ifdef SOCKET_ZERO_ISNT_STDIN
    if (!no_input && (bufptr = my_read_console())) {
      con_prompt_enter();	/* will need a new prompt, regardless */
      handle_stdin_input((struct connection *)NULL, bufptr);
    }
#else  /* !SOCKET_ZERO_ISNT_STDIN */
    if(!no_input && FD_ISSET(0, &readfs)) {    /* input from server operator */
#ifdef HAVE_LIBREADLINE
      rl_callback_read_char();
      if (readline_handled_input) {
	readline_handled_input = 0;
	con_prompt_enter_clear();
      }
      continue;
#else  /* !HAVE_LIBREADLINE */
      int didget;
      char buf[BUF_SIZE+1];
      
      if((didget=read(0, buf, BUF_SIZE))==-1) {
	freelog(LOG_FATAL, "read from stdin failed");
	exit(1);
      }

      if(didget==0) {
        handle_stdin_close();
      }

      *(buf+didget)='\0';
      con_prompt_enter();	/* will need a new prompt, regardless */
      handle_stdin_input((struct connection *)NULL, buf);
#endif /* !HAVE_LIBREADLINE */
    }
    else
#endif /* !SOCKET_ZERO_ISNT_STDIN */
     
    {                             /* input from a player */
      for(i=0; i<MAX_NUM_CONNECTIONS; i++) {
  	struct connection *pconn = &connections[i];
	if(pconn->used && FD_ISSET(pconn->sock, &readfs)) {
	  if(read_socket_data(pconn->sock, pconn->buffer)>=0) {
	    char *packet;
	    int type, result;
	    while (1) {
	      packet = get_packet_from_connection(pconn, &type, &result);
	      if (result) {
		int command_ok;

		pconn->server.last_request_id_seen =
		    get_next_request_id(pconn->server.
					last_request_id_seen);
#if PROCESSING_TIME_STATISTICS
		{
		  int err;
		  struct timeval start, end;
		  struct timezone tz;
		  long us;

		  err = gettimeofday(&start, &tz);
		  assert(!err);
#endif
		connection_do_buffer(pconn);
		start_processing_request(pconn,
					 pconn->server.
					 last_request_id_seen);
		command_ok = handle_packet_input(pconn, packet, type);
		finish_processing_request(pconn);
		connection_do_unbuffer(pconn);
		if (!command_ok) {
		  close_connection(pconn);
		}
#if PROCESSING_TIME_STATISTICS
		  err = gettimeofday(&end, &tz);
		  assert(!err);

		  us = (end.tv_sec - start.tv_sec) * 1000000 +
		      end.tv_usec - start.tv_usec;

		  freelog(LOG_NORMAL,
			  "processed request %d in %ld.%03ldms",
			  pconn->server.last_request_id_seen, us / 1000,
			  us % 1000);
                }
#endif
	      } else {
		break;
	      }
	    }
	  } else {
	    close_socket_callback(pconn);
	  }
	}
      }

      for(i=0; i<MAX_NUM_CONNECTIONS; i++) {
        struct connection *pconn = &connections[i];
        if(pconn->used && pconn->send_buffer && pconn->send_buffer->ndata) {
	  if(FD_ISSET(pconn->sock, &writefs)) {
	    flush_connection_send_buffer_all(pconn);
	  } else {
	    if(game.tcptimeout && pconn->last_write
	       && (time(NULL)>pconn->last_write + game.tcptimeout)) {
	      freelog(LOG_NORMAL, "cut connection %s due to lagging player",
		      conn_description(pconn));
	      close_socket_callback(pconn);
	    }
	  }
        }
      }
    }
    break;
  }
  con_prompt_off();

  if((game.timeout) 
     && (time(NULL)>game.turn_start + game.timeout))
    return 0;
  return 1;
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
    my_snprintf(name, sizeof(name), "c%u", (unsigned int)++i);
    if (!find_player_by_name(name)
	&& !find_player_by_user(name)
	&& !find_conn_by_id(i)
	&& !find_conn_by_name(name)) {
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

# if defined(__VMS) && !defined(_DECC_V4_SOURCE)
    size_t fromlen;
# else
    int fromlen;
# endif

  int new_sock;
  struct sockaddr_in fromend;
  struct hostent *from;
  int i;

  fromlen = sizeof(fromend);

  if ((new_sock=accept(sockfd, (struct sockaddr *) &fromend, &fromlen)) == -1) {
    freelog(LOG_ERROR, "accept failed: %s", mystrerror(errno));
    return -1;
  }

  my_nonblock(new_sock);

  from=gethostbyaddr((char *)&fromend.sin_addr, sizeof(fromend.sin_addr),
		     AF_INET);

  for(i=0; i<MAX_NUM_CONNECTIONS; i++) {
    struct connection *pconn = &connections[i];
    if (!pconn->used) {
      pconn->used = 1;
      pconn->sock = new_sock;
      pconn->established = 0;
      pconn->observer = 0;
      pconn->player = NULL;
      pconn->buffer = new_socket_packet_buffer();
      pconn->send_buffer = new_socket_packet_buffer();
      pconn->last_write = 0;
      pconn->ponged = 1;
      pconn->first_packet = 1;
      pconn->byte_swap = 0;
      pconn->capability[0] = '\0';
      pconn->access_level = access_level_for_next_connection();
      pconn->delayed_disconnect = 0;
      pconn->notify_of_writable_data = NULL;
      pconn->server.currently_processed_request_id = 0;
      pconn->server.last_request_id_seen = 0;
      pconn->incoming_packet_notify = NULL;
      pconn->outgoing_packet_notify = NULL;

      sz_strlcpy(pconn->name, makeup_connection_name(&pconn->id));
      sz_strlcpy(pconn->addr,
		 (from ? from->h_name : inet_ntoa(fromend.sin_addr)));

      conn_list_insert_back(&game.all_connections, pconn);
  
      freelog(LOG_VERBOSE, "connection (%s) from %s", pconn->name, pconn->addr);
      return 0;
    }
  }

  freelog(LOG_ERROR, "maximum number of connections reached");
  return -1;
}

/********************************************************************
 open server socket to be used to accept client connections
********************************************************************/
int server_open_socket(void)
{
  /* setup socket address */
  struct sockaddr_in src;
  int opt;

  if((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    freelog(LOG_FATAL, "socket failed: %s", mystrerror(errno));
    exit(1);
  }

  opt=1; 
  if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, 
		(char *)&opt, sizeof(opt)) == -1) {
    freelog(LOG_ERROR, "SO_REUSEADDR failed: %s", mystrerror(errno));
  }

  memset(&src, 0, sizeof(src));
  src.sin_family = AF_INET;
  src.sin_addr.s_addr = htonl(INADDR_ANY);
  src.sin_port = htons(srvarg.port);

  if(bind(sock, (struct sockaddr *) &src, sizeof (src)) == -1) {
    freelog(LOG_FATAL, "bind failed: %s", mystrerror(errno));
    exit(1);
  }

  if(listen(sock, MAX_NUM_CONNECTIONS) == -1) {
    freelog(LOG_FATAL, "listen failed: %s", mystrerror(errno));
    exit(1);
  }

  close_socket_set_callback(close_socket_callback);
  return 0;
}


/********************************************************************
...
********************************************************************/
void init_connections(void)
{
  int i;
  for(i=0; i<MAX_NUM_CONNECTIONS; i++) { 
    struct connection *pconn = &connections[i];
    pconn->used = 0;
    conn_list_init(&pconn->self);
    conn_list_insert(&pconn->self, pconn);
    pconn->route = NULL;
  }
#if defined(__VMS)
  {
    unsigned long status;
    $DESCRIPTOR (tt_desc, "SYS$INPUT");
    status = sys$assign(&tt_desc,&tt_chan,0,0);
    if (!$VMS_STATUS_SUCCESS(status)) lib$stop(status);
  }
#endif
}

/**************************************************************************
...
**************************************************************************/
static void start_processing_request(struct connection *pconn,
				     int request_id)
{
  assert(request_id);
  assert(!pconn->server.currently_processed_request_id);
  freelog(LOG_DEBUG, "start processing packet %d from connection %d",
	  request_id, pconn->id);
  if (strlen(pconn->capability) == 0
      || has_capability("processing_packets", pconn->capability)) {
    send_packet_generic_empty(pconn, PACKET_PROCESSING_STARTED);
  }
  pconn->server.currently_processed_request_id = request_id;
}

/**************************************************************************
...
**************************************************************************/
static void finish_processing_request(struct connection *pconn)
{
  assert(pconn->server.currently_processed_request_id);
  freelog(LOG_DEBUG, "finish processing packet %d from connection %d",
	  pconn->server.currently_processed_request_id, pconn->id);
  if (strlen(pconn->capability) == 0
      || has_capability("processing_packets", pconn->capability)) {
    send_packet_generic_empty(pconn, PACKET_PROCESSING_FINISHED);
  }
  pconn->server.currently_processed_request_id = 0;
}
