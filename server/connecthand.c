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

#include <string.h>

#include "capability.h"
#include "capstr.h"
#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "support.h"
#include "version.h"

#include "auth.h"
#include "diplhand.h"
#include "gamehand.h"
#include "maphand.h"
#include "meta.h"
#include "plrhand.h"
#include "ruleset.h"
#include "sernet.h"
#include "srv_main.h"
#include "stdinhand.h"

#include "connecthand.h"

/**************************************************************************
  This is used when a new player joins a server, before the game
  has started.  If pconn is NULL, is an AI, else a client.

  N.B. this only attachs a connection to a player if 
       pconn->username == player->username
**************************************************************************/
void establish_new_connection(struct connection *pconn)
{
  struct conn_list *dest = pconn->self;
  struct player *pplayer;
  struct packet_server_join_reply packet;
  char hostname[512];

  /* zero out the password */
  memset(pconn->server.password, 0, sizeof(pconn->server.password));

  /* send join_reply packet */
  packet.you_can_join = TRUE;
  sz_strlcpy(packet.capability, our_capability);
  my_snprintf(packet.message, sizeof(packet.message), _("%s Welcome"),
              pconn->username);
  sz_strlcpy(packet.challenge_file, new_challenge_filename(pconn));
  packet.conn_id = pconn->id;
  send_packet_server_join_reply(pconn, &packet);

  /* "establish" the connection */
  pconn->established = TRUE;
  pconn->server.status = AS_ESTABLISHED;

  conn_list_append(game.est_connections, pconn);
  if (conn_list_size(game.est_connections) == 1) {
    /* First connection
     * Replace "restarting in x seconds" meta message */
    maybe_automatic_meta_message(default_meta_message_string());
    (void) send_server_info_to_metaserver(META_INFO);
  }

  /* introduce the server to the connection */
  if (my_gethostname(hostname, sizeof(hostname)) == 0) {
    notify_conn(dest, NULL, E_CONNECTION,
		_("Welcome to the %s Server running at %s port %d."),
                freeciv_name_version(), hostname, srvarg.port);
  } else {
    notify_conn(dest, NULL, E_CONNECTION,
		_("Welcome to the %s Server at port %d."),
                freeciv_name_version(), srvarg.port);
  }

  /* FIXME: this (getting messages about others logging on) should be a 
   * message option for the client with event */

  /* notify the console and other established connections that you're here */
  freelog(LOG_NORMAL, _("%s has connected from %s."),
          pconn->username, pconn->addr);
  conn_list_iterate(game.est_connections, aconn) {
    if (aconn != pconn) {
      notify_conn(aconn->self, NULL, E_CONNECTION,
		  _("Server: %s has connected from %s."),
                  pconn->username, pconn->addr);
    }
  } conn_list_iterate_end;

  send_rulesets(dest);
  send_server_settings(dest);

  if ((pplayer = find_player_by_user(pconn->username))) {
    /* a player has already been created for this user, reconnect */
    attach_connection_to_player(pconn, pplayer, FALSE);

    if (game.info.auto_ai_toggle && pplayer->ai.control) {
      toggle_ai_player_direct(NULL, pplayer);
    }

    if (S_S_RUNNING == server_state()) {
      /* Player and other info is only updated when the game is running.
       * See the comment in lost_connection_to_client(). */
      send_packet_freeze_hint(pconn);
      send_all_info(dest);
      send_diplomatic_meetings(pconn);
      send_packet_thaw_hint(pconn);
      dsend_packet_start_phase(pconn, game.info.phase);
    } else {
      send_game_info(dest);
      /* send new player connection to everybody */
      send_player_info_c(NULL, game.est_connections);
      send_conn_info(game.est_connections, dest);
    }
  } else {
    send_game_info(dest);

    if (S_S_INITIAL == server_state() && game.info.is_new_game) {
      if (attach_connection_to_player(pconn, NULL, FALSE)) {
        /* temporarily set player_name() to username */
        sz_strlcpy(pconn->playing->name, pconn->username);
        aifill(game.info.aifill); /* first connect */

        /* send new player connection to everybody */
        send_player_info_c(NULL, game.est_connections);
      } else {
        notify_conn(dest, NULL, E_CONNECTION,
                    _("Couldn't attach your connection to new player."));
        freelog(LOG_VERBOSE, "%s is not attached to a player", pconn->username);

        /* send old player connections to self */
        send_player_info_c(NULL, dest);
      }
    } else {
      /* send old player connections to self */
      send_player_info_c(NULL, dest);
    }
    send_conn_info(game.est_connections, dest);
  }
  /* redundant self to self cannot be avoided */
  send_conn_info(dest, game.est_connections);

  /* remind the connection who he is */
  if (NULL == pconn->playing) {
    notify_conn(dest, NULL, E_CONNECTION,
		_("You are logged in as '%s' connected to no player."),
                pconn->username);
  } else if (strcmp(player_name(pconn->playing), ANON_PLAYER_NAME) == 0) {
    notify_conn(dest, NULL, E_CONNECTION,
		_("You are logged in as '%s' connected to an "
		  "anonymous player."),
		pconn->username);
  } else {
    notify_conn(dest, NULL, E_CONNECTION,
		_("You are logged in as '%s' connected to %s."),
                pconn->username,
                player_name(pconn->playing));
  }

  /* if need be, tell who we're waiting on to end the game.info.turn */
  if (S_S_RUNNING == server_state() && game.info.turnblock) {
    players_iterate(cplayer) {
      if (cplayer->is_alive
          && !cplayer->ai.control
          && !cplayer->phase_done
          && cplayer != pconn->playing) {  /* skip current player */
        notify_conn(dest, NULL, E_CONNECTION,
		    _("Turn-blocking game play: "
		      "waiting on %s to finish turn..."),
                    player_name(cplayer));
      }
    } players_iterate_end;
  }

  /* if the game is running, players can just view the Players menu? --dwp */
  if (S_S_RUNNING != server_state()) {
    show_players(pconn);
  }

  if (game.info.is_edit_mode) {
    notify_conn(dest, NULL, E_SETTING,
                _(" *** Server is in edit mode. *** "));
  }

  reset_all_start_commands();
  (void) send_server_info_to_metaserver(META_INFO);
}

/**************************************************************************
  send the rejection packet to the client.
**************************************************************************/
void reject_new_connection(const char *msg, struct connection *pconn)
{
  struct packet_server_join_reply packet;

  /* zero out the password */
  memset(pconn->server.password, 0, sizeof(pconn->server.password));

  packet.you_can_join = FALSE;
  sz_strlcpy(packet.capability, our_capability);
  sz_strlcpy(packet.message, msg);
  packet.challenge_file[0] = '\0';
  packet.conn_id = -1;
  send_packet_server_join_reply(pconn, &packet);
  freelog(LOG_NORMAL, _("Client rejected: %s."), conn_description(pconn));
  flush_connection_send_buffer_all(pconn);
}

/**************************************************************************
 Returns FALSE if the clients gets rejected and the connection should be
 closed. Returns TRUE if the client get accepted.
**************************************************************************/
bool handle_login_request(struct connection *pconn, 
                          struct packet_server_join_req *req)
{
  char msg[MAX_LEN_MSG];
  
  freelog(LOG_NORMAL, _("Connection request from %s from %s"),
          req->username, pconn->addr);
  
  /* print server and client capabilities to console */
  freelog(LOG_NORMAL, _("%s has client version %d.%d.%d%s"),
          pconn->username, req->major_version, req->minor_version,
          req->patch_version, req->version_label);
  freelog(LOG_VERBOSE, "Client caps: %s", req->capability);
  freelog(LOG_VERBOSE, "Server caps: %s", our_capability);
  sz_strlcpy(pconn->capability, req->capability);
  
  /* Make sure the server has every capability the client needs */
  if (!has_capabilities(our_capability, req->capability)) {
    my_snprintf(msg, sizeof(msg),
                _("The client is missing a capability that this server needs.\n"
                   "Server version: %d.%d.%d%s Client version: %d.%d.%d%s."
                   "  Upgrading may help!"),
                MAJOR_VERSION, MINOR_VERSION, PATCH_VERSION, VERSION_LABEL,
                req->major_version, req->minor_version,
                req->patch_version, req->version_label);
    reject_new_connection(msg, pconn);
    freelog(LOG_NORMAL, _("%s was rejected: Mismatched capabilities."),
            req->username);
    return FALSE;
  }

  /* Make sure the client has every capability the server needs */
  if (!has_capabilities(req->capability, our_capability)) {
    my_snprintf(msg, sizeof(msg),
                _("The server is missing a capability that the client needs.\n"
                   "Server version: %d.%d.%d%s Client version: %d.%d.%d%s."
                   "  Upgrading may help!"),
                MAJOR_VERSION, MINOR_VERSION, PATCH_VERSION, VERSION_LABEL,
                req->major_version, req->minor_version,
                req->patch_version, req->version_label);
    reject_new_connection(msg, pconn);
    freelog(LOG_NORMAL, _("%s was rejected: Mismatched capabilities."),
            req->username);
    return FALSE;
  }

  remove_leading_trailing_spaces(req->username);

  /* Name-sanity check: could add more checks? */
  if (!is_valid_username(req->username)) {
    my_snprintf(msg, sizeof(msg), _("Invalid username '%s'"), req->username);
    reject_new_connection(msg, pconn);
    freelog(LOG_NORMAL, _("%s was rejected: Invalid name [%s]."),
            req->username, pconn->addr);
    return FALSE;
  } 

  /* don't allow duplicate logins */
  conn_list_iterate(game.all_connections, aconn) {
    if (mystrcasecmp(req->username, aconn->username) == 0) { 
      my_snprintf(msg, sizeof(msg), _("'%s' already connected."), 
                  req->username);
      reject_new_connection(msg, pconn);
      freelog(LOG_NORMAL, _("%s was rejected: Duplicate login name [%s]."),
              req->username, pconn->addr);
      return FALSE;
    }
  } conn_list_iterate_end;

  if (srvarg.auth_enabled) {
    return authenticate_user(pconn, req->username);
  } else {
    sz_strlcpy(pconn->username, req->username);
    establish_new_connection(pconn);
    return TRUE;
  }
}

/**************************************************************************
  High-level server stuff when connection to client is closed or lost.
  Reports loss to log, and to other players if the connection was a
  player.  Also removes player in pregame, applies auto_toggle, and
  does check for turn done (since can depend on connection/ai status).
  Note caller should also call close_connection() after this, to do
  lower-level close stuff.
**************************************************************************/
void lost_connection_to_client(struct connection *pconn)
{
  struct player *pplayer = pconn->playing;
  const char *desc = conn_description(pconn);

  freelog(LOG_NORMAL, _("Lost connection: %s."), desc);
  
  /* _Must_ avoid sending to pconn, in case pconn connection is
   * really lost (as opposed to server shutting it down) which would
   * trigger an error on send and recurse back to here.
   * Safe to unlink even if not in list: */
  conn_list_unlink(game.est_connections, pconn);
  delayed_disconnect++;
  notify_conn(game.est_connections, NULL, E_CONNECTION,
	      _("Lost connection: %s."), desc);

  if (!pplayer) {
    delayed_disconnect--;
    return;
  }

  detach_connection_to_player(pconn, FALSE);
  send_conn_info_remove(pconn->self, game.est_connections);
  notify_if_first_access_level_is_available();

  if (game.info.is_new_game
      && !pplayer->is_connected /* eg multiple controllers */
      && !pplayer->ai.control    /* eg created AI player */
      && S_S_INITIAL == server_state()) {
    server_remove_player(pplayer);
  } else {
    if (game.info.auto_ai_toggle
        && !pplayer->ai.control
        && !pplayer->is_connected /* eg multiple controllers */) {
      toggle_ai_player_direct(NULL, pplayer);
    }

    check_for_full_turn_done();
  }
  /* send_player_info() was formerly updated by toggle_ai_player_direct(),
   * so it must be safe to send here now?
   *
   * At other times, data from send_conn_info() is used by the client to
   * display player information.  See establish_new_connection().
   */
  freelog(LOG_VERBOSE, "lost_connection_to_client() calls send_player_info_c()");
  send_player_info_c(pplayer, game.est_connections);

  reset_all_start_commands();

  delayed_disconnect--;
}

/**************************************************************************
  Fill in packet_conn_info from full connection struct.
**************************************************************************/
static void package_conn_info(struct connection *pconn,
                              struct packet_conn_info *packet)
{
  packet->id           = pconn->id;
  packet->used         = pconn->used;
  packet->established  = pconn->established;
  packet->player_num   = (NULL != pconn->playing)
                         ? player_number(pconn->playing)
                         : player_count();
  packet->observer     = pconn->observer;
  packet->access_level = pconn->access_level;

  sz_strlcpy(packet->username, pconn->username);
  sz_strlcpy(packet->addr, pconn->addr);
  sz_strlcpy(packet->capability, pconn->capability);
}

/**************************************************************************
  Handle both send_conn_info() and send_conn_info_removed(), depending
  on 'remove' arg.  Sends conn_info packets for 'src' to 'dest', turning
  off 'used' if 'remove' is specified.
**************************************************************************/
static void send_conn_info_arg(struct conn_list *src,
                               struct conn_list *dest, bool remove)
{
  struct packet_conn_info packet;

  if (!dest) {
    dest = game.est_connections;
  }
  
  conn_list_iterate(src, psrc) {
    package_conn_info(psrc, &packet);
    if (remove) {
      packet.used = FALSE;
    }
    lsend_packet_conn_info(dest, &packet);
  }
  conn_list_iterate_end;
}

/**************************************************************************
  Send conn_info packets to tell 'dest' connections all about
  'src' connections.
**************************************************************************/
void send_conn_info(struct conn_list *src, struct conn_list *dest)
{
  send_conn_info_arg(src, dest, FALSE);
}

/**************************************************************************
  Like send_conn_info(), but turn off the 'used' bits to tell clients
  to remove info about these connections instead of adding it.
**************************************************************************/
void send_conn_info_remove(struct conn_list *src, struct conn_list *dest)
{
  send_conn_info_arg(src, dest, TRUE);
}

/**************************************************************************
  Search for first uncontrolled player
**************************************************************************/
struct player *find_uncontrolled_player(void)
{
  players_iterate(played) {
    if (!played->is_connected && !played->was_created) {
      return played;
    }
  } players_iterate_end;

  return NULL;
}

/**************************************************************************
  Setup pconn as a client connected to pplayer:
  Updates pconn->playing, pplayer->connections, pplayer->is_connected
  and pconn->observer.

  If pplayer is NULL, take the next available player that is not connected.
  Note "observer" connections do not count for is_connected.
  Note take_command() needs to know if this function will success before
       it's time to call this. Keep take_command() checks in sync when
       modifying this.
**************************************************************************/
bool attach_connection_to_player(struct connection *pconn,
                                 struct player *pplayer,
                                 bool observing)
{
  if (observing) {
    assert(NULL != pplayer);
  } else {
    if (NULL == pplayer) {
      /* search for uncontrolled player */
      pplayer = find_uncontrolled_player();

      if (NULL == pplayer) {
        /* no uncontrolled player found */
        if (game.info.nplayers >= game.info.max_players
            || game.info.nplayers - server.nbarbarians >= server.playable_nations) {
          return FALSE;
        }
        assert(game.info.nplayers < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS);

        /* add new player */
        pplayer = &game.players[game.info.nplayers];
        dlsend_packet_player_control(game.est_connections, ++game.info.nplayers);
      }
    }

    team_remove_player(pplayer);
    server_player_init(pplayer, FALSE, TRUE);

    sz_strlcpy(pplayer->username, pconn->username);
    pplayer->user_turns = 0; /* reset for a new user */
    pplayer->is_connected = TRUE;

    /* If we are attached to a player in pregame from
     * find_uncontrolled_player above, then that player
     * will be an AI created by aifill. So turn off AI
     * mode if it is still on. */
    if (server_state() == S_S_INITIAL && pplayer->ai.control) {
      pplayer->ai.control = FALSE;
    }
  }

  pconn->observer = observing;
  pconn->playing = pplayer;
  conn_list_append(pplayer->connections, pconn);

  return TRUE;
}
  
/**************************************************************************
  Remove pconn as a client connected to pplayer:
  Updates pconn->playing, pconn->playing->connections,
  pconn->playing->is_connected and pconn->observer.

  pconn remains a member of game.est_connections.

  The 'observing' parameter should be TRUE if 'pconn' is to become
  a global observer, FALSE otherwise.
**************************************************************************/
bool detach_connection_to_player(struct connection *pconn,
                                 bool observing)
{
  pconn->observer = observing;

  if (NULL == pconn->playing) {
    return FALSE; /* no player is attached to this conn */
  }

  conn_list_unlink(pconn->playing->connections, pconn);

  pconn->playing->is_connected = FALSE;

  /* If any other (non-observing) conn is attached to 
   * this player, the player is still connected. */
  conn_list_iterate(pconn->playing->connections, aconn) {
    if (!aconn->observer) {
      pconn->playing->is_connected = TRUE;
      break;
    }
  } conn_list_iterate_end;

  pconn->playing = NULL;

  return TRUE;
}
