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

/*
 * This bit sends "I'm here" packages to the metaserver.
 */

/*
The desc block should look like this:
1) GameName   - Freeciv
2) Version    - 1.5.0
3) State      - Running(Open for new players)
4) Host       - Empty
5) Port       - 5555
6) NoPlayers  - 3
7) InfoText   - Warfare - starts at 8:00 EMT

The info string should look like this:
  GameSpecific text block
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#ifdef HAVE_WINSOCK
#include <winsock.h>
#endif

#include "dataio.h"
#include "fcintl.h"
#include "log.h"
#include "netintf.h"
#include "support.h"
#include "timing.h"
#include "version.h"

#include "console.h"
#include "srv_main.h"

#include "meta.h"

bool server_is_open = FALSE;

static int			sockfd=0;
static union my_sockaddr   	meta_addr;


/*************************************************************************
  Return static string with default info line to send to metaserver.
  This is a function (instead of a define) to keep meta.h clean of
  including config.h and version.h
*************************************************************************/
const char *default_meta_server_info_string(void)
{
#if IS_BETA_VERSION
  return "unstable pre-" NEXT_STABLE_VERSION ": beware";
#else
#if IS_DEVEL_VERSION
  return "development version: beware";
#else
  return "(default)";
#endif
#endif
}

/*************************************************************************
...
*************************************************************************/
void meta_addr_split(void)
{
  char *metaserver_port_separator = strchr(srvarg.metaserver_addr, ':');

  if (metaserver_port_separator) {
    sscanf(metaserver_port_separator + 1, "%hu", &srvarg.metaserver_port);
  }
}

/*************************************************************************
...
*************************************************************************/
char *meta_addr_port(void)
{
  static char retstr[300];

  if (srvarg.metaserver_port == DEFAULT_META_SERVER_PORT) {
    sz_strlcpy(retstr, srvarg.metaserver_addr);
  } else {
    my_snprintf(retstr, sizeof(retstr),
		"%s:%d", srvarg.metaserver_addr, srvarg.metaserver_port);
  }

  return retstr;
}

/*************************************************************************
...
  Returns true if able to send 
*************************************************************************/
static bool send_to_metaserver(char *desc, char *info)
{
  unsigned char buffer[MAX_LEN_PACKET];
  struct data_out dout;
  size_t size;

  dio_output_init(&dout, buffer, sizeof(buffer));

  if(sockfd<=0)
    return FALSE;

  dio_put_uint16(&dout, 0);
  dio_put_uint8(&dout, PACKET_UDP_PCKT);
  dio_put_string(&dout, desc);
  dio_put_string(&dout, info);

  size = dio_output_used(&dout);

  dio_output_rewind(&dout);
  dio_put_uint16(&dout, size);

  my_writesocket(sockfd, buffer, size);
  return TRUE;
}

/*************************************************************************
...
*************************************************************************/
void server_close_udp(void)
{
  server_is_open = FALSE;

  if(sockfd<=0)
    return;
  my_closesocket(sockfd);
  sockfd=0;
}

/*************************************************************************
...
*************************************************************************/
static void metaserver_failed(void)
{
  con_puts(C_METAERROR, _("Not reporting to the metaserver in this game."));
  con_flush();
}

/*************************************************************************
...
*************************************************************************/
void server_open_udp(void)
{
  char *metaname = srvarg.metaserver_addr;
  int metaport;
  union my_sockaddr bind_addr;
  
  /*
   * Fill in the structure "meta_addr" with the address of the
   * server that we want to connect with, checks if server address 
   * is valid, both decimal-dotted and name.
   */
  metaport = srvarg.metaserver_port;
  if (!net_lookup_service(metaname, metaport, &meta_addr)) {
    freelog(LOG_ERROR, _("Metaserver: bad address: [%s:%d]."),
      metaname,
      metaport);
    metaserver_failed();
    return;
  }

  /*
   * Open a UDP socket (an Internet datagram socket).
   */
  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
    freelog(LOG_ERROR, "Metaserver: can't open datagram socket: %s",
	    mystrerror());
    metaserver_failed();
    return;
  }

  /*
   * Bind any local address for us and
   * associate datagram socket with server.
   */
  if (!net_lookup_service(srvarg.bind_addr, 0, &bind_addr)) {
    freelog(LOG_ERROR, _("Metaserver: bad address: [%s:%d]."),
	    srvarg.bind_addr, 0);
    metaserver_failed();
  }

  /* set source IP */
  if (bind(sockfd, &bind_addr.sockaddr, sizeof(bind_addr)) == -1) {
    freelog(LOG_ERROR, "Metaserver: bind failed: %s", mystrerror());
    metaserver_failed();
    return;
  }

  /* no, this is not weird, see man connect(2) --vasc */
  if (connect(sockfd, &meta_addr.sockaddr, sizeof(meta_addr))==-1) {
    freelog(LOG_ERROR, "Metaserver: connect failed: %s", mystrerror());
    metaserver_failed();
    return;
  }

  server_is_open = TRUE;
}


/**************************************************************************
...
**************************************************************************/
bool send_server_info_to_metaserver(bool do_send, bool reset_timer)
{
  static struct timer *time_since_last_send = NULL;
  char desc[4096], info[4096];
  int num_nonbarbarians = get_num_human_and_ai_players();

  if (reset_timer && time_since_last_send)
  {
    free_timer(time_since_last_send);
    time_since_last_send = NULL;
    return TRUE;
     /* use when we close the connection to a metaserver */
  }

  if (!do_send && time_since_last_send
      && ((int) read_timer_seconds(time_since_last_send)
	  < METASERVER_UPDATE_INTERVAL)) {
    return FALSE;
  }
  if (!time_since_last_send) {
    time_since_last_send = new_timer(TIMER_USER, TIMER_ACTIVE);
  }

  /* build description block */
  desc[0]='\0';
  
  cat_snprintf(desc, sizeof(desc), "Freeciv\n");
  cat_snprintf(desc, sizeof(desc), VERSION_STRING"\n");
  /* note: the following strings are not translated here;
     we mark them so they may be translated when received by a client */
  switch(server_state) {
  case PRE_GAME_STATE:
    cat_snprintf(desc, sizeof(desc), N_("Pregame"));
    break;
  case RUN_GAME_STATE:
    cat_snprintf(desc, sizeof(desc), N_("Running"));
    break;
  case GAME_OVER_STATE:
    cat_snprintf(desc, sizeof(desc), N_("Game over"));
    break;
  default:
    cat_snprintf(desc, sizeof(desc), N_("Waiting"));
  }
  cat_snprintf(desc, sizeof(desc), "\n");
  cat_snprintf(desc, sizeof(desc), "%s\n", "UNSET");
  cat_snprintf(desc, sizeof(desc), "%d\n", srvarg.port);
  cat_snprintf(desc, sizeof(desc), "%d\n", num_nonbarbarians);
  cat_snprintf(desc, sizeof(desc), "%s %s", srvarg.metaserver_info_line,
	       srvarg.extra_metaserver_info);

  /* now build the info block */
  info[0]='\0';
  cat_snprintf(info, sizeof(info),
	  "Players:%d  Min players:%d  Max players:%d\n",
	  num_nonbarbarians,  game.min_players, game.max_players);
  cat_snprintf(info, sizeof(info),
	  "Timeout:%d  Year: %s\n",
	  game.timeout, textyear(game.year));
    
    
  cat_snprintf(info, sizeof(info),
	       "NO:  NAME:               HOST:\n");
  cat_snprintf(info, sizeof(info),
	       "----------------------------------------\n");

  players_iterate(pplayer) {
    if (!is_barbarian(pplayer)) {
      /* Fixme: how should metaserver handle multi-connects?
       * Uses player_addr_hack() for now.
       */
      cat_snprintf(info, sizeof(info), "%2d   %-20s %s\n",
		   pplayer->player_no, pplayer->name,
		   player_addr_hack(pplayer));
    }
  } players_iterate_end;

  clear_timer_start(time_since_last_send);
  return send_to_metaserver(desc, info);
}
