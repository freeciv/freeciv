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
#include "log.h"
#include "packets.h"
#include "player.h"
#include "support.h"
#include "version.h"

#include "diplhand.h"
#include "gamehand.h"
#include "maphand.h"
#include "meta.h"
#include "plrhand.h"
#include "ruleset.h"
#include "srv_main.h"
#include "stdinhand.h"

#include "connecthand.h"

static void reject_new_player(char *msg, struct connection *pconn);

/**************************************************************************
 Send a join_game reply, accepting the connection.
 pconn->player should be set and non-NULL.
 'rejoin' is whether this is a new player, or re-connection.
 Prints a log message, and notifies other players.
**************************************************************************/
static void join_game_accept(struct connection *pconn, bool rejoin)
{
  struct packet_login_reply packet;
  struct player *pplayer = pconn->player;

  assert(pplayer != NULL);
  packet.you_can_login = TRUE;
  sz_strlcpy(packet.capability, our_capability);
  my_snprintf(packet.message, sizeof(packet.message),
              "%s %s.", (rejoin?"Welcome back":"Welcome"), pplayer->name);
  send_packet_login_reply(pconn, &packet);
  pconn->established = TRUE;
  conn_list_insert_back(&game.est_connections, pconn);
  conn_list_insert_back(&game.game_connections, pconn);

  send_conn_info(&pconn->self, &game.est_connections);

  freelog(LOG_NORMAL, _("%s has joined as player %s."),
          pconn->name, pplayer->name);
  conn_list_iterate(game.est_connections, aconn) {
    if (aconn!=pconn) {
      notify_conn(&aconn->self,
                  _("Game: Connection %s from %s has joined as player %s."),
                  pconn->name, pconn->addr, pplayer->name);
    }
  } conn_list_iterate_end;
}

/**************************************************************************
 Introduce connection/client/player to server, and notify of other players.
**************************************************************************/
static void introduce_game_to_connection(struct connection *pconn)
{
  struct conn_list *dest;
  char hostname[512];

  if (!pconn) {
    return;
  }
  dest = &pconn->self;
  
  if (my_gethostname(hostname, sizeof(hostname))==0) {
    notify_conn(dest, _("Welcome to the %s Server running at %s port %d."), 
                freeciv_name_version(), hostname, srvarg.port);
  } else {
    notify_conn(dest, _("Welcome to the %s Server at port %d."),
                freeciv_name_version(), srvarg.port);
  }

  /* tell who we're waiting on to end the game turn */
  if (game.turnblock) {
    players_iterate(cplayer) {
      if (cplayer->is_alive
          && !cplayer->ai.control
          && !cplayer->turn_done
          && cplayer != pconn->player) {  /* skip current player */
        notify_conn(dest, _("Turn-blocking game play: "
                            "waiting on %s to finish turn..."),
                    cplayer->name);
      }
    } players_iterate_end;
  }

  if (server_state==RUN_GAME_STATE) {
    /* if the game is running, players can just view the Players menu? --dwp */
    return;
  }

  /* Just use output of server command 'list' to inform about other
   * players and player-connections: */
  show_players(pconn);

  /* notify_conn(dest, _("Waiting for the server to start the game.")); */
  /* I would put this here, but then would need another message when
     the game is started. --dwp */
}

/**************************************************************************
  This is used when a new player joins a game, before the game
  has started.  If pconn is NULL, is an AI, else a client.
  ...
**************************************************************************/
void accept_new_player(char *name, struct connection *pconn)
{
  struct player *pplayer = &game.players[game.nplayers];

  server_player_init(pplayer, FALSE);
  /* sometimes needed if players connect/disconnect to avoid
   * inheriting stale AI status etc */
  
  sz_strlcpy(pplayer->name, name);
  sz_strlcpy(pplayer->username, name);

  if (pconn) {
    associate_player_connection(pplayer, pconn);
    join_game_accept(pconn, FALSE);
  } else {
    freelog(LOG_NORMAL, _("%s has been added as an AI-controlled player."),
            name);
    notify_player(NULL,
                  _("Game: %s has been added as an AI-controlled player."),
                  name);
  }

  game.nplayers++;

  introduce_game_to_connection(pconn);
  (void) send_server_info_to_metaserver(TRUE, FALSE);
}

/**************************************************************************
...
**************************************************************************/
static void reject_new_player(char *msg, struct connection *pconn)
{
  struct packet_login_reply packet;
  
  packet.you_can_login = FALSE;
  sz_strlcpy(packet.capability, our_capability);
  sz_strlcpy(packet.message, msg);
  send_packet_login_reply(pconn, &packet);
}
  
/**************************************************************************
  Try a single name as new connection name for set_unique_conn_name().
  If name is ok, copy to pconn->name and return 1, else return 0.
**************************************************************************/
static bool try_conn_name(struct connection *pconn, const char *name)
{
  char save_name[MAX_LEN_NAME];

  /* avoid matching current name in find_conn_by_name() */
  sz_strlcpy(save_name, pconn->name);
  pconn->name[0] = '\0';
  
  if (!find_conn_by_name(name)) {
    sz_strlcpy(pconn->name, name);
    return TRUE;
  } else {
    sz_strlcpy(pconn->name, save_name);
    return FALSE;
  }
}

/**************************************************************************
  Set pconn->name based on reqested name req_name: either use req_name,
  if no other connection has that name, else a modified name based on
  req_name (req_name prefixed by digit and dash).
  Returns 0 if req_name used unchanged, else 1.
**************************************************************************/
static bool set_unique_conn_name(struct connection *pconn, const char *req_name)
{
  char adjusted_name[MAX_LEN_NAME];
  int i;
  
  if (try_conn_name(pconn, req_name)) {
    return FALSE;
  }

  for(i = 1; i < 10000; i++) {
    my_snprintf(adjusted_name, sizeof(adjusted_name), "%d-%s", i, req_name);
    if (try_conn_name(pconn, adjusted_name)) {
      return TRUE;
    }
  }

  /* This should never happen */
  freelog(LOG_ERROR, "Too many failures in set_unique_conn_name(%s,%s)",
          pconn->name, req_name);
  sz_strlcpy(pconn->name, req_name);
  return FALSE;
}

/**************************************************************************
Returns 0 if the clients gets rejected and the connection should be
closed. Returns 1 if the client get accepted.
**************************************************************************/
bool handle_login_request(struct connection *pconn, 
                          struct packet_login_request *req)
{
  struct player *pplayer;
  char msg[MAX_LEN_MSG];
  char orig_name[MAX_LEN_NAME];
  const char *allow;                /* pointer into game.allow_connect */
  
  sz_strlcpy(orig_name, req->name);
  remove_leading_trailing_spaces(req->name);

  /* Name-sanity check: could add more checks? */
  if (strlen(req->name)==0) {
    my_snprintf(msg, sizeof(msg), _("Invalid name '%s'"), orig_name);
    reject_new_player(msg, pconn);
    freelog(LOG_NORMAL, _("Rejected connection from %s with invalid name."),
            pconn->addr);
    return FALSE;
  }

  if (!set_unique_conn_name(pconn, req->name)) {
    freelog(LOG_NORMAL, _("Connection request from %s from %s"),
            req->name, pconn->addr);
  } else {
    freelog(LOG_NORMAL,
            _("Connection request from %s from %s (connection named %s)"),
            req->name, pconn->addr, pconn->name);
  }
  
  freelog(LOG_NORMAL, _("%s has client version %d.%d.%d%s"),
          pconn->name, req->major_version, req->minor_version,
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
    reject_new_player(msg, pconn);
    freelog(LOG_NORMAL, _("%s was rejected: Mismatched capabilities."),
            pconn->name);
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
    reject_new_player(msg, pconn);
    freelog(LOG_NORMAL, _("%s was rejected: Mismatched capabilities."),
            pconn->name);
    return FALSE;
  }

  if( (pplayer=find_player_by_name(req->name)) || 
      (pplayer=find_player_by_user(req->name))     ) {
    if (is_barbarian(pplayer)) {
      if (!(allow = strchr(game.allow_connect, 'b'))) {
        reject_new_player(_("Sorry, no observation of barbarians "
                            "in this game."), pconn);
        freelog(LOG_NORMAL,
                _("%s was rejected: No observation of barbarians."),
                pconn->name);
        return FALSE;
      }
    } else if (!pplayer->is_alive) {
      if (!(allow = strchr(game.allow_connect, 'd'))) {
        reject_new_player(_("Sorry, no observation of dead players "
                            "in this game."), pconn);
        freelog(LOG_NORMAL,
                _("%s was rejected: No observation of dead players."),
                pconn->name);
        return FALSE;
      }
    } else if (pplayer->ai.control) {
      if (!(allow = strchr(game.allow_connect, 
                           (game.is_new_game ? 'A' : 'a')))) {
        reject_new_player(_("Sorry, no observation of AI players "
                            "in this game."), pconn);
        freelog(LOG_NORMAL,
                _("%s was rejected: No observation of AI players."),
                pconn->name);
        return FALSE;
      }
    } else {
      if (!(allow = strchr(game.allow_connect, 
                           (game.is_new_game ? 'H' : 'h')))) {
        reject_new_player(_("Sorry, no reconnections to human-mode players "
                            "in this game."), pconn);
        freelog(LOG_NORMAL,
                _("%s was rejected: No reconnection to human players."),
                pconn->name);
        return FALSE;
      }
    }

    allow++;
    if (conn_list_size(&pplayer->connections) > 0
        && !(*allow == '*' || *allow == '+' || *allow == '=')) {

      /* The player exists but is already connected and multiple
       * connections not permitted. */
      
      if (game.is_new_game && server_state == PRE_GAME_STATE) {
        /* Duplicate names cause problems even in PRE_GAME_STATE
         * because they are used to identify players for various
         * server commands. --dwp */
        reject_new_player(_("Sorry, someone else already has that name."),
                          pconn);
        freelog(LOG_NORMAL, _("%s was rejected: Name %s already used."),
                pconn->name, req->name);
        return FALSE;
      } else {
        my_snprintf(msg, sizeof(msg), _("Sorry, %s is already connected."), 
                    pplayer->name);
        reject_new_player(msg, pconn);
        freelog(LOG_NORMAL, _("%s was rejected: %s already connected."),
                pconn->name, pplayer->name);
        return FALSE;
      }
    }

    /* The player exists and not connected or multi-conn allowed: */
    if ((*allow == '=') || (*allow == '-')
        || (pplayer->is_connected && (*allow != '*'))) {
      pconn->observer = TRUE;
    }

    associate_player_connection(pplayer, pconn);
    join_game_accept(pconn, TRUE);
    introduce_game_to_connection(pconn);
    if (server_state == RUN_GAME_STATE) {
      send_packet_generic_empty(pconn, PACKET_FREEZE_HINT);
      send_rulesets(&pconn->self);
      send_all_info(&pconn->self);
      send_game_state(&pconn->self, CLIENT_GAME_RUNNING_STATE);
      send_player_info(NULL,NULL);
      send_diplomatic_meetings(pconn);
      send_packet_generic_empty(pconn, PACKET_THAW_HINT);
      send_packet_generic_empty(pconn, PACKET_START_TURN);
    }

    if (game.auto_ai_toggle && pplayer->ai.control) {
      toggle_ai_player_direct(NULL, pplayer);
    }
    return TRUE;
  }

  /* unknown name */

  if (server_state != PRE_GAME_STATE || !game.is_new_game) {
    reject_new_player(_("Sorry, the game is already running."), pconn);
    freelog(LOG_NORMAL, 
            _("%s was rejected: Game running and unknown name %s."),
            pconn->name, req->name);
    return FALSE;
  }

  if (game.nplayers >= game.max_players) {
    reject_new_player(_("Sorry, the game is full."), pconn);
    freelog(LOG_NORMAL, 
            _("%s was rejected: Maximum number of players reached."),
            pconn->name);    
    return FALSE;
  }

  if (!(allow = strchr(game.allow_connect, 'N'))) {
    reject_new_player(_("Sorry, no new players allowed in this game."), pconn);
    freelog(LOG_NORMAL, _("%s was rejected: No connections as new player."),
            pconn->name);
    return FALSE;
  }
  
  accept_new_player(req->name, pconn);
  return TRUE;
}

/**************************************************************************
  High-level server stuff when connection to client is closed or lost.
  Reports loss to log, and to other players if the connection was a
  player.  Also removes player if in pregame, applies auto_toggle, and
  does check for turn done (since can depend on connection/ai status).
  Note caller should also call close_connection() after this, to do
  lower-level close stuff.
**************************************************************************/
void lost_connection_to_client(struct connection *pconn)
{
  struct player *pplayer = pconn->player;
  const char *desc = conn_description(pconn);

  freelog(LOG_NORMAL, _("Lost connection: %s."), desc);
  
  /* _Must_ avoid sending to pconn, in case pconn connection is
   * really lost (as opposed to server shutting it down) which would
   * trigger an error on send and recurse back to here.
   * Safe to unlink even if not in list: */
  conn_list_unlink(&game.est_connections, pconn);
  delayed_disconnect++;
  notify_conn(&game.est_connections, _("Game: Lost connection: %s."), desc);

  if (!pplayer) {
    /* This happens eg if the player has not yet joined properly. */
    delayed_disconnect--;
    return;
  }

  unassociate_player_connection(pplayer, pconn);

  /* Remove from lists so this conn is not included in broadcasts.
   * safe to do these even if not in lists: */
  conn_list_unlink(&game.all_connections, pconn);
  conn_list_unlink(&game.game_connections, pconn);
  pconn->established = FALSE;
  
  send_conn_info_remove(&pconn->self, &game.est_connections);
  send_player_info(pplayer, NULL);
  notify_if_first_access_level_is_available();

  /* Cancel diplomacy meetings */
  if (!pplayer->is_connected) { /* may be still true if multiple connections */
    players_iterate(other_player) {
      if (find_treaty(pplayer, other_player)) {
        struct packet_diplomacy_info packet;
        packet.plrno0 = pplayer->player_no;
        packet.plrno1 = other_player->player_no;
        handle_diplomacy_cancel_meeting(pplayer, &packet);
      }
    } players_iterate_end;
  }

  if (game.is_new_game
      && !pplayer->is_connected /* eg multiple controllers */
      && !pplayer->ai.control    /* eg created AI player */
      && (server_state==PRE_GAME_STATE || server_state==SELECT_RACES_STATE)) {
    server_remove_player(pplayer);
  } else {
    if (game.auto_ai_toggle
        && !pplayer->ai.control
        && !pplayer->is_connected /* eg multiple controllers */) {
      toggle_ai_player_direct(NULL, pplayer);
    }
    check_for_full_turn_done();
  }
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
  packet->player_num   = pconn->player ? pconn->player->player_no : -1;
  packet->observer     = pconn->observer;
  packet->access_level = pconn->access_level;

  sz_strlcpy(packet->name, pconn->name);
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
  
  conn_list_iterate(*src, psrc) {
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
  Setup pconn as a client connected to pplayer:
  Updates pconn->player, pplayer->connections, pplayer->is_connected.
  Note "observer" connections do not count for is_connected.
**************************************************************************/
void associate_player_connection(struct player *pplayer,
                                 struct connection *pconn)
{
  assert(pplayer && pconn);
  
  pconn->player = pplayer;
  conn_list_insert_back(&pplayer->connections, pconn);
  if (!pconn->observer) {
    pplayer->is_connected = TRUE;
  }
}

/**************************************************************************
  Remove pconn as a client connected to pplayer:
  Update pplayer->connections, pplayer->is_connected.
  Sets pconn->player to NULL (normally expect pconn->player==pplayer
  when function entered, but not checked).
**************************************************************************/
void unassociate_player_connection(struct player *pplayer,
                                   struct connection *pconn)
{
  assert(pplayer && pconn);

  pconn->player = NULL;
  conn_list_unlink(&pplayer->connections, pconn);

  pplayer->is_connected = FALSE;
  conn_list_iterate(pplayer->connections, aconn) {
    if (!aconn->observer) {
      pplayer->is_connected = TRUE;
      break;
    }
  } conn_list_iterate_end;
}
