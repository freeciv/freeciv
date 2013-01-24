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

#include <ctype.h>
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

/* utility */
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "netintf.h"
#include "support.h"
#include "timing.h"

/* common */
#include "capstr.h"
#include "connection.h"
#include "dataio.h"
#include "game.h"
#include "version.h"

/* server */
#include "console.h"
#include "plrhand.h"
#include "settings.h"
#include "srv_main.h"

#include "meta.h"

static bool server_is_open = FALSE;

static union fc_sockaddr names[2];
static int   name_count;
static char  metaname[MAX_LEN_ADDR];
static int   metaport;
static char *metaserver_path;

static char meta_patches[256] = "";
static char meta_message[256] = "";

/*************************************************************************
 the default metaserver patches for this server
*************************************************************************/
const char *default_meta_patches_string(void)
{
  return "none";
}

/*************************************************************************
  Return static string with default info line to send to metaserver.
*************************************************************************/
const char *default_meta_message_string(void)
{
#if IS_BETA_VERSION
  return "unstable pre-" NEXT_STABLE_VERSION ": beware";
#else  /* IS_BETA_VERSION */
#if IS_DEVEL_VERSION
  return "development version: beware";
#else  /* IS_DEVEL_VERSION */
  return "-";
#endif /* IS_DEVEL_VERSION */
#endif /* IS_BETA_VERSION */
}

/*************************************************************************
 the metaserver patches
*************************************************************************/
const char *get_meta_patches_string(void)
{
  return meta_patches;
}

/*************************************************************************
 the metaserver message
*************************************************************************/
const char *get_meta_message_string(void)
{
  return meta_message;
}

/*************************************************************************
 The metaserver message set by user
*************************************************************************/
const char *get_user_meta_message_string(void)
{
  if (game.server.meta_info.user_message_set) {
    return game.server.meta_info.user_message;
  }

  return NULL;
}

/*************************************************************************
  Update meta message. Set it to user meta message, if it is available.
  Otherwise use provided message.
  It is ok to call this with NULL message. Then it only replaces current
  meta message with user meta message if available.
*************************************************************************/
void maybe_automatic_meta_message(const char *automatic)
{
  const char *user_message;

  user_message = get_user_meta_message_string();

  if (user_message == NULL) {
    /* No user message */
    if (automatic != NULL) {
      set_meta_message_string(automatic);
    }
    return;
  }

  set_meta_message_string(user_message);
}

/*************************************************************************
 set the metaserver patches string
*************************************************************************/
void set_meta_patches_string(const char *string)
{
  sz_strlcpy(meta_patches, string);
}

/*************************************************************************
 set the metaserver message string
*************************************************************************/
void set_meta_message_string(const char *string)
{
  sz_strlcpy(meta_message, string);
}

/*************************************************************************
 set user defined metaserver message string
*************************************************************************/
void set_user_meta_message_string(const char *string)
{
  if (string != NULL && string[0] != '\0') {
    sz_strlcpy(game.server.meta_info.user_message, string);
    game.server.meta_info.user_message_set = TRUE;
    set_meta_message_string(string);
  } else {
    /* Remove user meta message. We will use automatic messages instead */
    game.server.meta_info.user_message[0] = '\0';
    game.server.meta_info.user_message_set = FALSE;
    set_meta_message_string(default_meta_message_string());    
  }
}

/*************************************************************************
...
*************************************************************************/
char *meta_addr_port(void)
{
  return srvarg.metaserver_addr;
}

/*************************************************************************
 we couldn't find or connect to the metaserver.
*************************************************************************/
static void metaserver_failed(void)
{
  con_puts(C_METAERROR, _("Not reporting to the metaserver in this game."));
  con_flush();

  server_close_meta();
}

/****************************************************************************
  Insert a setting in the metaserver message. Return TRUE if it succeded.
****************************************************************************/
static inline bool meta_insert_setting(char *s, size_t rest,
                                       const char *set_name)
{
  const struct setting *pset = setting_by_name(set_name);
  char buf[256];

  fc_assert_ret_val_msg(NULL != pset, FALSE,
                        "Setting \"%s\" not found!", set_name);
  fc_snprintf(s, rest, "vn[]=%s&vv[]=%s&",
              fc_url_encode(setting_name(pset)),
              setting_value_name(pset, FALSE, buf, sizeof(buf)));
  return TRUE;
}

/*************************************************************************
 construct the POST message and send info to metaserver.
*************************************************************************/
static bool send_to_metaserver(enum meta_flag flag)
{
  static char msg[8192];
  static char str[8192];
  int rest = sizeof(str);
  int i;
  int players = 0;
  int humans = 0;
  int len;
  int sock = -1;
  char *s = str;
  char host[512];
  char state[20];

  if (!server_is_open) {
    return FALSE;
  }

  /* Try all (IPv4, IPv6, ...) addresses until we have a connection. */  
  for (i = 0; i < name_count; i++) {
    if ((sock = socket(names[i].saddr.sa_family, SOCK_STREAM, 0)) == -1) {
      /* Probably EAFNOSUPPORT or EPROTONOSUPPORT. */
      continue;
    }

    if (fc_connect(sock, &names[i].saddr,
                   sockaddr_size(&names[i])) == -1) {
      fc_closesocket(sock);
      sock = -1;
      continue;
    } else {
      /* We have a connection! */
      break;
    }
  }

  if (sock == -1) {
    log_error("Metaserver: %s", fc_strerror(fc_get_errno()));
    metaserver_failed();
    return FALSE;
  }

  switch(server_state()) {
  case S_S_INITIAL:
    sz_strlcpy(state, "Pregame");
    break;
  case S_S_RUNNING:
    sz_strlcpy(state, "Running");
    break;
  case S_S_OVER:
    sz_strlcpy(state, "Game Ended");
    break;
  }

  /* get hostname */
  if (srvarg.metaserver_name[0] != '\0') {
    sz_strlcpy(host, srvarg.metaserver_name);
  } else if (fc_gethostname(host, sizeof(host)) != 0) {
    sz_strlcpy(host, "unknown");
  }

  fc_snprintf(s, rest, "host=%s&port=%d&state=%s&",
              host, srvarg.port, state);
  s = end_of_strn(s, &rest);

  if (flag == META_GOODBYE) {
    fc_strlcpy(s, "bye=1&", rest);
    s = end_of_strn(s, &rest);
  } else {
    fc_snprintf(s, rest, "version=%s&", fc_url_encode(VERSION_STRING));
    s = end_of_strn(s, &rest);

    fc_snprintf(s, rest, "patches=%s&", 
                fc_url_encode(get_meta_patches_string()));
    s = end_of_strn(s, &rest);

    fc_snprintf(s, rest, "capability=%s&", fc_url_encode(our_capability));
    s = end_of_strn(s, &rest);

    fc_snprintf(s, rest, "serverid=%s&",
                fc_url_encode(srvarg.serverid));
    s = end_of_strn(s, &rest);

    fc_snprintf(s, rest, "message=%s&",
                fc_url_encode(get_meta_message_string()));
    s = end_of_strn(s, &rest);

    /* NOTE: send info for ALL players or none at all. */
    if (normal_player_count() == 0) {
      fc_strlcpy(s, "dropplrs=1&", rest);
      s = end_of_strn(s, &rest);
    } else {
      players = 0; /* a counter for players_available */
      humans = 0;

      players_iterate(plr) {
        bool is_player_available = TRUE;
        char type[15];
        struct connection *pconn = conn_by_user(plr->username);

        if (!plr->is_alive) {
          sz_strlcpy(type, "Dead");
        } else if (is_barbarian(plr)) {
          sz_strlcpy(type, "Barbarian");
        } else if (plr->ai_controlled) {
          sz_strlcpy(type, "A.I.");
        } else {
          sz_strlcpy(type, "Human");
        }

        fc_snprintf(s, rest, "plu[]=%s&", fc_url_encode(plr->username));
        s = end_of_strn(s, &rest);

        fc_snprintf(s, rest, "plt[]=%s&", type);
        s = end_of_strn(s, &rest);

        fc_snprintf(s, rest, "pll[]=%s&", fc_url_encode(player_name(plr)));
        s = end_of_strn(s, &rest);

        fc_snprintf(s, rest, "pln[]=%s&",
                    fc_url_encode(plr->nation != NO_NATION_SELECTED 
                                  ? nation_plural_for_player(plr)
                                  : "none"));
        s = end_of_strn(s, &rest);

        fc_snprintf(s, rest, "plh[]=%s&",
                    pconn ? fc_url_encode(pconn->addr) : "");
        s = end_of_strn(s, &rest);

        /* is this player available to take?
         * TODO: there's some duplication here with 
         * stdinhand.c:is_allowed_to_take() */
        if (is_barbarian(plr) && !strchr(game.server.allow_take, 'b')) {
          is_player_available = FALSE;
        } else if (!plr->is_alive && !strchr(game.server.allow_take, 'd')) {
          is_player_available = FALSE;
        } else if (plr->ai_controlled
                   && !strchr(game.server.allow_take,
                              (game.info.is_new_game ? 'A' : 'a'))) {
          is_player_available = FALSE;
        } else if (!plr->ai_controlled
                   && !strchr(game.server.allow_take,
                              (game.info.is_new_game ? 'H' : 'h'))) {
          is_player_available = FALSE;
        }

        if (pconn) {
          is_player_available = FALSE;
        }

        if (is_player_available) {
          players++;
        }
          
        if (!plr->ai_controlled && plr->is_alive) {
          humans++;
        }
      } players_iterate_end;

      /* send the number of available players. */
      fc_snprintf(s, rest, "available=%d&", players);
      s = end_of_strn(s, &rest);
      fc_snprintf(s, rest, "humans=%d&", humans);
      s = end_of_strn(s, &rest);
    }

    /* Send some variables: should be listed in inverted order? */
    {
      static const char *settings[] = {
        "timeout", "endturn", "minplayers", "maxplayers",
        "aifill", "allowtake", "generator"
      };
      int i;

      for (i = 0; i < ARRAY_SIZE(settings); i++) {
        if (meta_insert_setting(s, rest, settings[i])) {
          s = end_of_strn(s, &rest);
        }
      }

      /* HACK: send the most determinant setting for the map size. */
      switch (map.server.mapsize) {
      case MAPSIZE_FULLSIZE:
        if (meta_insert_setting(s, rest, "size")) {
          s = end_of_strn(s, &rest);
        }
        break;
      case MAPSIZE_PLAYER:
        if (meta_insert_setting(s, rest, "tilesperplayer")) {
          s = end_of_strn(s, &rest);
        }
        break;
      case MAPSIZE_XYSIZE:
        if (meta_insert_setting(s, rest, "xsize")) {
          s = end_of_strn(s, &rest);
        }
        if (meta_insert_setting(s, rest, "ysize")) {
          s = end_of_strn(s, &rest);
        }
        break;
      }
    }

    /* Turn and year. */
    fc_snprintf(s, rest, "vn[]=%s&vv[]=%d&",
                fc_url_encode("turn"), game.info.turn);
    s = end_of_strn(s, &rest);
    fc_snprintf(s, rest, "vn[]=%s&vv[]=",
                fc_url_encode("year"));
    s = end_of_strn(s, &rest);
    if (server_state() != S_S_INITIAL) {
      fc_snprintf(s, rest, "%d&",
                  game.info.year);
    } else {
      fc_snprintf(s, rest, "%s&",
                  fc_url_encode("Calendar not set up"));
    }
    s = end_of_strn(s, &rest);
  }

  len = fc_snprintf(msg, sizeof(msg),
    "POST %s HTTP/1.1\r\n"
    "Host: %s:%d\r\n"
    "Content-Type: application/x-www-form-urlencoded; charset=\"utf-8\"\r\n"
    "Content-Length: %lu\r\n"
    "\r\n"
    "%s\r\n",
    metaserver_path,
    metaname,
    metaport,
    (unsigned long) (sizeof(str) - rest + 1),
    str
  );

  fc_writesocket(sock, msg, len);

  fc_closesocket(sock);

  return TRUE;
}

/*************************************************************************
 
*************************************************************************/
void server_close_meta(void)
{
  server_is_open = FALSE;
}

/*************************************************************************
 lookup the correct address for the metaserver.
*************************************************************************/
bool server_open_meta(void)
{
  const char *path;
 
  if (metaserver_path) {
    free(metaserver_path);
    metaserver_path = NULL;
  }
  
  if (!(path = fc_lookup_httpd(metaname, &metaport, srvarg.metaserver_addr))) {
    return FALSE;
  }
  
  metaserver_path = fc_strdup(path);

  if (!net_lookup_service(metaname, metaport, &names[0], FALSE)) {
    log_error(_("Metaserver: bad address: <%s %d>."), metaname, metaport);
    metaserver_failed();
    return FALSE;
  }
  name_count = 1;
#ifdef IPV6_SUPPORT
  if (names[0].saddr.sa_family == AF_INET6) {
    /* net_lookup_service() prefers IPv6 address.
     * Check if there is also IPv4 address.
     * TODO: This would be easier using getaddrinfo() */
    if (net_lookup_service(metaname, metaport,
                           &names[1], TRUE /* force IPv4 */)) {
      name_count = 2;
    }
  }
#endif

  if (meta_patches[0] == '\0') {
    set_meta_patches_string(default_meta_patches_string());
  }
  if (meta_message[0] == '\0') {
    set_meta_message_string(default_meta_message_string());
  }

  server_is_open = TRUE;

  return TRUE;
}

/**************************************************************************
 are we sending info to the metaserver?
**************************************************************************/
bool is_metaserver_open(void)
{
  return server_is_open;
}

/**************************************************************************
 control when we send info to the metaserver.
**************************************************************************/
bool send_server_info_to_metaserver(enum meta_flag flag)
{
  static struct timer *last_send_timer = NULL;
  static bool want_update;

  /* if we're bidding farewell, ignore all timers */
  if (flag == META_GOODBYE) { 
    if (last_send_timer) {
      free_timer(last_send_timer);
      last_send_timer = NULL;
    }
    return send_to_metaserver(flag);
  }

  /* don't allow the user to spam the metaserver with updates */
  if (last_send_timer && (read_timer_seconds(last_send_timer)
                                          < METASERVER_MIN_UPDATE_INTERVAL)) {
    if (flag == META_INFO) {
      want_update = TRUE; /* we couldn't update now, but update a.s.a.p. */
    }
    return FALSE;
  }

  /* if we're asking for a refresh, only do so if 
   * we've exceeded the refresh interval */
  if ((flag == META_REFRESH) && !want_update && last_send_timer 
      && (read_timer_seconds(last_send_timer) < METASERVER_REFRESH_INTERVAL)) {
    return FALSE;
  }

  /* start a new timer if we haven't already */
  if (!last_send_timer) {
    last_send_timer = new_timer(TIMER_USER, TIMER_ACTIVE);
  }

  clear_timer_start(last_send_timer);
  want_update = FALSE;
  return send_to_metaserver(flag);
}
