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

#include <string.h>

/* utility */
#include "capability.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "support.h"

/* common */
#include "capstr.h"
#include "events.h"
#include "game.h"
#include "packets.h"
#include "player.h"
#include "version.h"

/* server */
#include "aiiface.h"
#include "auth.h"
#include "diplhand.h"
#include "edithand.h"
#include "gamehand.h"
#include "maphand.h"
#include "meta.h"
#include "notify.h"
#include "plrhand.h"
#include "report.h"
#include "ruleset.h"
#include "sernet.h"
#include "settings.h"
#include "srv_main.h"
#include "stdinhand.h"
#include "voting.h"

#include "connecthand.h"


static bool connection_attach_real(struct connection *pconn,
                                   struct player *pplayer,
                                   bool observing, bool connecting);

/**************************************************************************
  Set the access level of a connection, and re-send some needed info.  If
  granted is TRUE, then it will overwrite the granted_access_level too.
  Else, it will affect only the current access level.

  NB: This function does not send updated connection information to other
  clients, you need to do that yourself afterwards.
**************************************************************************/
void conn_set_access(struct connection *pconn, enum cmdlevel new_level,
                     bool granted)
{
  enum cmdlevel old_level = conn_get_access(pconn);

  pconn->access_level = new_level;
  if (granted) {
    pconn->server.granted_access_level = new_level;
  }

  if (old_level != new_level
      && (ALLOW_HACK == old_level || ALLOW_HACK == new_level)) {
    send_server_hack_level_settings(pconn->self);
  }
}

/**************************************************************************
  Restore access level for the given connection (user). Used when taking
  a player, observing, or detaching.

  NB: This function does not send updated connection information to other
  clients, you need to do that yourself afterwards.
**************************************************************************/
static void restore_access_level(struct connection *pconn)
{
  /* Restore previous privileges. */
  enum cmdlevel level = pconn->server.granted_access_level;

  /* Detached connections must have at most the same privileges
   * as observers, unless they were granted something higher than
   * ALLOW_BASIC in the first place. */
  if ((pconn->observer || !pconn->playing) && level == ALLOW_BASIC) {
    level = ALLOW_INFO;
  }

  conn_set_access(pconn, level, FALSE);
}

/**************************************************************************
  This is used when a new player joins a server, before the game
  has started.  If pconn is NULL, is an AI, else a client.

  N.B. this only attachs a connection to a player if 
       pconn->username == player->username

  Here we send initial packets:
  - ruleset datas.
  - server settings.
  - scenario info.
  - game info.
  - players infos (note it's resent in srv_main.c::send_all_info(),
      see comment there).
  - connections infos.
  - running vote infos.
  ... and additionnal packets if the game already started.
**************************************************************************/
void establish_new_connection(struct connection *pconn)
{
  struct conn_list *dest = pconn->self;
  struct player *pplayer;
  struct packet_server_join_reply packet;
  struct packet_chat_msg connect_info;
  char hostname[512];
  bool delegation_error = FALSE;

  /* zero out the password */
  memset(pconn->server.password, 0, sizeof(pconn->server.password));

  /* send join_reply packet */
  packet.you_can_join = TRUE;
  sz_strlcpy(packet.capability, our_capability);
  fc_snprintf(packet.message, sizeof(packet.message), _("%s Welcome"),
              pconn->username);
  sz_strlcpy(packet.challenge_file, new_challenge_filename(pconn));
  packet.conn_id = pconn->id;
  send_packet_server_join_reply(pconn, &packet);

  /* "establish" the connection */
  pconn->established = TRUE;
  pconn->server.status = AS_ESTABLISHED;

  pconn->server.delegation.status = FALSE;
  pconn->server.delegation.playing = NULL;
  pconn->server.delegation.observer = FALSE;

  conn_list_append(game.est_connections, pconn);
  if (conn_list_size(game.est_connections) == 1) {
    /* First connection
     * Replace "restarting in x seconds" meta message */
    maybe_automatic_meta_message(default_meta_message_string());
    (void) send_server_info_to_metaserver(META_INFO);
  }

  /* introduce the server to the connection */
  if (fc_gethostname(hostname, sizeof(hostname)) == 0) {
    notify_conn(dest, NULL, E_CONNECTION, ftc_any,
                _("Welcome to the %s Server running at %s port %d."),
                freeciv_name_version(), hostname, srvarg.port);
  } else {
    notify_conn(dest, NULL, E_CONNECTION, ftc_any,
                _("Welcome to the %s Server at port %d."),
                freeciv_name_version(), srvarg.port);
  }

  /* FIXME: this (getting messages about others logging on) should be a 
   * message option for the client with event */

  /* Notify the console that you're here. */
  log_normal(_("%s has connected from %s."), pconn->username, pconn->addr);

  conn_compression_freeze(pconn);
  send_rulesets(dest);
  send_server_setting_control(pconn);
  send_server_settings(dest);
  send_scenario_info(dest);
  send_game_info(dest);

  if ((pplayer = player_by_user_delegated(pconn->username))) {
    /* Force our control over the player. */
    struct connection *pdelegate = conn_by_user(pplayer->server.delegate_to);

    if (pdelegate && connection_delegate_restore(pdelegate)) {
        notify_conn(pdelegate->self, NULL, E_CONNECTION, ftc_server,
                    _("Player '%s' did reconnect. Restore player state."),
                    player_name(pplayer));
    } else {
      notify_conn(dest, NULL, E_CONNECTION, ftc_server,
                  _("Couldn't get control from delegation to %s."),
                  pdelegate->username);
      log_verbose("%s can't take control over its player %s from delegation.",
                  pconn->username, player_name(pplayer));
      delegation_error = TRUE;
    }
  }

  if (!delegation_error) {
    if ((pplayer = player_by_user(pconn->username))
        && connection_attach_real(pconn, pplayer, FALSE, TRUE)) {
      /* a player has already been created for this user, reconnect */

      if (S_S_INITIAL == server_state()) {
        send_player_info_c(NULL, dest);
      }
    } else {
      if (!game_was_started()) {
        if (!connection_attach(pconn, NULL, FALSE)) {
          notify_conn(dest, NULL, E_CONNECTION, ftc_server,
                      _("Couldn't attach your connection to new player."));
          log_verbose("%s is not attached to a player", pconn->username);
        }
      }
      send_player_info_c(NULL, dest);
    }
  }

  send_conn_info(game.est_connections, dest);

  send_pending_events(pconn, TRUE);
  send_running_votes(pconn, FALSE);

  if (NULL == pplayer) {
    /* Else this has already been done in connection_attach(). */
    restore_access_level(pconn);
    send_conn_info(dest, game.est_connections);
  }

  /* remind the connection who he is */
  if (NULL == pconn->playing) {
    notify_conn(dest, NULL, E_CONNECTION, ftc_server,
		_("You are logged in as '%s' connected to no player."),
                pconn->username);
  } else if (strcmp(player_name(pconn->playing), ANON_PLAYER_NAME) == 0) {
    notify_conn(dest, NULL, E_CONNECTION, ftc_server,
		_("You are logged in as '%s' connected to an "
		  "anonymous player."),
		pconn->username);
  } else {
    notify_conn(dest, NULL, E_CONNECTION, ftc_server,
		_("You are logged in as '%s' connected to %s."),
                pconn->username,
                player_name(pconn->playing));
  }

  /* Send information about delegation(s). */
  send_delegation_info(pconn);

  /* Notify the *other* established connections that you are connected, and
   * add the info for all in event cache. Note we must to do it after we
   * sent the pending events to pconn (from this function and also
   * connection_attach()), otherwise pconn will receive it too. */
  if (conn_controls_player(pconn)) {
    package_event(&connect_info, NULL, E_CONNECTION, ftc_server,
                  _("%s has connected from %s (player %s)."),
                  pconn->username, pconn->addr,
                  player_name(conn_get_player(pconn)));
  } else {
    package_event(&connect_info, NULL, E_CONNECTION, ftc_server,
                  _("%s has connected from %s."),
                  pconn->username, pconn->addr);
  }
  conn_list_iterate(game.est_connections, aconn) {
    if (aconn != pconn) {
      send_packet_chat_msg(aconn, &connect_info);
    }
  } conn_list_iterate_end;
  event_cache_add_for_all(&connect_info);

  /* if need be, tell who we're waiting on to end the game.info.turn */
  if (S_S_RUNNING == server_state() && game.server.turnblock) {
    players_iterate_alive(cplayer) {
      if (!cplayer->ai_controlled
          && !cplayer->phase_done
          && cplayer != pconn->playing) {  /* skip current player */
        notify_conn(dest, NULL, E_CONNECTION, ftc_any,
		    _("Turn-blocking game play: "
		      "waiting on %s to finish turn..."),
                    player_name(cplayer));
      }
    } players_iterate_alive_end;
  }

  if (game.info.is_edit_mode) {
    notify_conn(dest, NULL, E_SETTING, ftc_editor,
                _(" *** Server is in edit mode. *** "));
  }

  if (NULL != pplayer) {
    /* Else, no need to do anything. */
    reset_all_start_commands();
    (void) send_server_info_to_metaserver(META_INFO);
  }
  conn_compression_thaw(pconn);
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
  log_normal(_("Client rejected: %s."), conn_description(pconn));
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
  int kick_time_remaining;

  log_normal(_("Connection request from %s from %s"),
             req->username, pconn->addr);

  /* print server and client capabilities to console */
  log_normal(_("%s has client version %d.%d.%d%s"),
             pconn->username, req->major_version, req->minor_version,
             req->patch_version, req->version_label);
  log_verbose("Client caps: %s", req->capability);
  log_verbose("Server caps: %s", our_capability);
  sz_strlcpy(pconn->capability, req->capability);
  
  /* Make sure the server has every capability the client needs */
  if (!has_capabilities(our_capability, req->capability)) {
    fc_snprintf(msg, sizeof(msg),
                _("The client is missing a capability that this server needs.\n"
                   "Server version: %d.%d.%d%s Client version: %d.%d.%d%s."
                   "  Upgrading may help!"),
                MAJOR_VERSION, MINOR_VERSION, PATCH_VERSION, VERSION_LABEL,
                req->major_version, req->minor_version,
                req->patch_version, req->version_label);
    reject_new_connection(msg, pconn);
    log_normal(_("%s was rejected: Mismatched capabilities."),
               req->username);
    return FALSE;
  }

  /* Make sure the client has every capability the server needs */
  if (!has_capabilities(req->capability, our_capability)) {
    fc_snprintf(msg, sizeof(msg),
                _("The server is missing a capability that the client needs.\n"
                   "Server version: %d.%d.%d%s Client version: %d.%d.%d%s."
                   "  Upgrading may help!"),
                MAJOR_VERSION, MINOR_VERSION, PATCH_VERSION, VERSION_LABEL,
                req->major_version, req->minor_version,
                req->patch_version, req->version_label);
    reject_new_connection(msg, pconn);
    log_normal(_("%s was rejected: Mismatched capabilities."),
               req->username);
    return FALSE;
  }

  remove_leading_trailing_spaces(req->username);

  /* Name-sanity check: could add more checks? */
  if (!is_valid_username(req->username)) {
    fc_snprintf(msg, sizeof(msg), _("Invalid username '%s'"), req->username);
    reject_new_connection(msg, pconn);
    log_normal(_("%s was rejected: Invalid name [%s]."),
               req->username, pconn->addr);
    return FALSE;
  }

  if (conn_is_kicked(pconn, &kick_time_remaining)) {
    fc_snprintf(msg, sizeof(msg), _("You have been kicked from this server "
                                    "and cannot reconnect for %d seconds."),
                kick_time_remaining);
    reject_new_connection(msg, pconn);
    log_normal(_("%s was rejected: Connection kicked "
                 "(%d seconds remaining)."),
               req->username, kick_time_remaining);
    return FALSE;
  }

  /* don't allow duplicate logins */
  conn_list_iterate(game.all_connections, aconn) {
    if (fc_strcasecmp(req->username, aconn->username) == 0) { 
      fc_snprintf(msg, sizeof(msg), _("'%s' already connected."), 
                  req->username);
      reject_new_connection(msg, pconn);
      log_normal(_("%s was rejected: Duplicate login name [%s]."),
                 req->username, pconn->addr);
      return FALSE;
    }
  } conn_list_iterate_end;

  if (game.server.connectmsg[0] != '\0') {
    log_debug("Sending connectmsg: %s", game.server.connectmsg);
    dsend_packet_connect_msg(pconn, game.server.connectmsg);
  }

  if (srvarg.auth_enabled) {
    return auth_user(pconn, req->username);
  } else {
    sz_strlcpy(pconn->username, req->username);
    establish_new_connection(pconn);
    return TRUE;
  }
}

/****************************************************************************
  High-level server stuff when connection to client is closed or lost.
  Reports loss to log, and to other players if the connection was a
  player. Also removes player in pregame, applies auto_toggle, and
  does check for turn done (since can depend on connection/ai status).
  Note you shouldn't this function directly. You should use
  server_break_connection() if you want to close the connection.
****************************************************************************/
void lost_connection_to_client(struct connection *pconn)
{
  const char *desc = conn_description(pconn);

  fc_assert_ret(TRUE == pconn->server.is_closing);

  log_normal(_("Lost connection: %s."), desc);

  /* Special color (white on black) for player loss */
  notify_conn(game.est_connections, NULL, E_CONNECTION,
              conn_controls_player(pconn) ? ftc_player_lost : ftc_server,
              _("Lost connection: %s."), desc);

  connection_detach(pconn);
  send_conn_info_remove(pconn->self, game.est_connections);
  notify_if_first_access_level_is_available();

  check_for_full_turn_done();
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
                         : player_slot_count();
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
  } conn_list_iterate_end;
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

/****************************************************************************
  Setup pconn as a client connected to pplayer or observer:
  Updates pconn->playing, pplayer->connections, pplayer->is_connected
  and pconn->observer.

  - If pplayer is NULL and observing is FALSE: take the next available
    player that is not connected.
  - If pplayer is NULL and observing is TRUE: attach this connection to
    the game as global observer.
  - If pplayer is not NULL and observing is FALSE: take this player.
  - If pplayer is not NULL and observing is TRUE: observe this player.

  Note take_command() needs to know if this function will success before
       it's time to call this. Keep take_command() checks in sync when
       modifying this.
****************************************************************************/
static bool connection_attach_real(struct connection *pconn,
                                   struct player *pplayer,
                                   bool observing, bool connecting)
{
  fc_assert_ret_val(pconn != NULL, FALSE);
  fc_assert_ret_val_msg(!pconn->observer && pconn->playing == NULL, FALSE,
                        "connections must be detached with "
                        "connection_detach() before calling this!");

  if (!observing) {
    if (NULL == pplayer) {
      /* search for uncontrolled player */
      pplayer = find_uncontrolled_player();

      if (NULL == pplayer) {
        /* no uncontrolled player found */
        if (player_count() >= game.server.max_players
            || player_count() - server.nbarbarians >= server.playable_nations) {
          return FALSE;
        }
        /* add new player, or not */
        pplayer = server_create_player(-1, default_ai_type_name(), NULL);
        if (!pplayer) {
          return FALSE;
        }
      } else {
        team_remove_player(pplayer);
      }
      server_player_init(pplayer, FALSE, TRUE);

      /* Make it human! */
      pplayer->ai_controlled = FALSE;
    }

    sz_strlcpy(pplayer->username, pconn->username);
    pplayer->user_turns = 0; /* reset for a new user */
    pplayer->is_connected = TRUE;

    if (!game_was_started()) {
      if (!pplayer->was_created && NULL == pplayer->nation) {
        /* Temporarily set player_name() to username. */
        server_player_set_name(pplayer, pconn->username);
      }
      aifill(game.info.aifill);
    }

    if (game.server.auto_ai_toggle && pplayer->ai_controlled) {
      toggle_ai_player_direct(NULL, pplayer);
    }

    send_player_info_c(pplayer, game.est_connections);
  }

  /* We don't want the connection's username on another player. */
  players_iterate(aplayer) {
    if (aplayer != pplayer
        && 0 == strncmp(aplayer->username, pconn->username, MAX_LEN_NAME)) {
      sz_strlcpy(aplayer->username, ANON_USER_NAME);
      send_player_info_c(aplayer, NULL);
    }
  } players_iterate_end;

  pconn->observer = observing;
  pconn->playing = pplayer;
  if (pplayer) {
    conn_list_append(pplayer->connections, pconn);
  }

  restore_access_level(pconn);

  /* Reset the delta-state. */
  send_conn_info(pconn->self, game.est_connections);    /* Client side. */
  conn_reset_delta_state(pconn);                        /* Server side. */

  /* Initial packets don't need to be resent.  See comment for
   * connecthand.c::establish_new_connection(). */
  switch (server_state()) {
  case S_S_INITIAL:
    break;

  case S_S_RUNNING:
    conn_compression_freeze(pconn);
    send_all_info(pconn->self);
    if (game.info.is_edit_mode && can_conn_edit(pconn)) {
      edithand_send_initial_packets(pconn->self);
    }
    conn_compression_thaw(pconn);
    /* Enter C_S_RUNNING client state. */
    dsend_packet_start_phase(pconn, game.info.phase);
    /* Must be after C_S_RUNNING client state to be effective. */
    send_diplomatic_meetings(pconn);
    if (!connecting) {
      /* Those will be sent later in establish_new_connection(). */
      send_pending_events(pconn, FALSE);
      send_running_votes(pconn, TRUE);
    }
    break;

  case S_S_OVER:
    conn_compression_freeze(pconn);
    send_all_info(pconn->self);
    if (game.info.is_edit_mode && can_conn_edit(pconn)) {
      edithand_send_initial_packets(pconn->self);
    }
    conn_compression_thaw(pconn);
    report_final_scores(pconn->self);
    if (!connecting) {
      /* Those will be sent later in establish_new_connection(). */
      send_pending_events(pconn, FALSE);
      send_running_votes(pconn, TRUE);
      /* Send information about delegation(s). */
      send_delegation_info(pconn);
    }
    break;
  }

  send_updated_vote_totals(NULL);

  return TRUE;
}

/****************************************************************************
  Setup pconn as a client connected to pplayer or observer.
****************************************************************************/
bool connection_attach(struct connection *pconn, struct player *pplayer,
                       bool observing)
{
  return connection_attach_real(pconn, pplayer, observing, FALSE);
}

/****************************************************************************
  Remove pconn as a client connected to pplayer:
  Updates pconn->playing, pconn->playing->connections,
  pconn->playing->is_connected and pconn->observer.

  pconn remains a member of game.est_connections.
****************************************************************************/
void connection_detach(struct connection *pconn)
{
  struct player *pplayer;

  fc_assert_ret(pconn != NULL);

  if (NULL != (pplayer = pconn->playing)) {
    bool was_connected = pplayer->is_connected;

    send_remove_team_votes(pconn);
    conn_list_remove(pplayer->connections, pconn);
    pconn->playing = NULL;
    pconn->observer = FALSE;
    restore_access_level(pconn);
    strcpy(pplayer->ranked_username, ANON_USER_NAME);

    /* If any other (non-observing) conn is attached to  this player, the
     * player is still connected. */
    pplayer->is_connected = FALSE;
    conn_list_iterate(pplayer->connections, aconn) {
      if (!aconn->observer) {
        pplayer->is_connected = TRUE;
        break;
      }
    } conn_list_iterate_end;

    if (was_connected && !pplayer->is_connected) {
      if (!pplayer->was_created && !game_was_started()) {
        /* Remove player. */
        conn_list_iterate(pplayer->connections, aconn) {
          /* Detach all. */
          fc_assert_action(aconn != pconn, continue);
          notify_conn(aconn->self, NULL, E_CONNECTION, ftc_server,
                      _("Detaching from %s."), player_name(pplayer));
          /* Recursive... but shouldn't be problem. */
          connection_detach(aconn);
        } conn_list_iterate_end;

        /* Actually do the removal. */
        server_remove_player(pplayer);
        aifill(game.info.aifill);
        reset_all_start_commands();
      } else {
        /* Aitoggle the player if no longer connected. */
        if (game.server.auto_ai_toggle && !pplayer->ai_controlled) {
          toggle_ai_player_direct(NULL, pplayer);
          /* send_player_info_c() was formerly updated by
           * toggle_ai_player_direct(), so it must be safe to send here now?
           *
           * At other times, data from send_conn_info() is used by the
           * client to display player information.
           * See establish_new_connection().
           */
          log_verbose("connection_detach() calls send_player_info_c()");
          send_player_info_c(pplayer, NULL);

          reset_all_start_commands();
        }
      }
    }
  } else {
    pconn->observer = FALSE;
    restore_access_level(pconn);
  }

  cancel_connection_votes(pconn);
  send_updated_vote_totals(NULL);

  send_conn_info(pconn->self, game.est_connections);
}

/*****************************************************************************
 Use a delegation to get control over another player.
*****************************************************************************/
bool connection_delegate_take(struct connection *pconn,
                              struct player *pplayer)
{
  fc_assert_ret_val(pconn->server.delegation.status == FALSE, FALSE);

  /* Save the original player of this connection and the original username of
   * the player. */
  pconn->server.delegation.status = TRUE;
  pconn->server.delegation.playing = conn_get_player(pconn);
  pconn->server.delegation.observer = pconn->observer;
  if (conn_get_player(pconn) != NULL) {
    struct player *plr = conn_get_player(pconn);
    fc_assert_ret_val(strlen(plr->server.orig_username) == 0, FALSE);
    sz_strlcpy(plr->server.orig_username, plr->username);
  }
  fc_assert_ret_val(strlen(pplayer->server.orig_username) == 0, FALSE);
  sz_strlcpy(pplayer->server.orig_username, pplayer->username);

  /* Detach the current connection. */
  if (NULL != pconn->playing || pconn->observer) {
    connection_detach(pconn);
  }

  /* Try to attach to the new player */
  if (!connection_attach(pconn, pplayer, FALSE)) {

    /* Restore original connection. */
    fc_assert_ret_val(connection_attach(pconn,
                                        pconn->server.delegation.playing,
                                        pconn->server.delegation.observer),
                      FALSE);

    /* Reset all changes done above. */
    pconn->server.delegation.status = FALSE;
    pconn->server.delegation.playing = NULL;
    pconn->server.delegation.observer = FALSE;
    if (conn_get_player(pconn) != NULL) {
      struct player *plr = conn_get_player(pconn);
      plr->server.orig_username[0] = '\0';
    }
    pplayer->server.orig_username[0] = '\0';

    return FALSE;
  }

  return TRUE;
}

/*****************************************************************************
 Restore the original status after using a delegation.
*****************************************************************************/
bool connection_delegate_restore(struct connection *pconn)
{
  struct player *pplayer;

  if (!pconn->server.delegation.status) {
    return FALSE;
  }

  /* Save the current player. */
  pplayer = conn_get_player(pconn);

  /* There should be a player connected to pconn. */
  fc_assert_ret_val(pplayer, FALSE);

  /* Detach the current connection. */
  if (NULL != pconn->playing || pconn->observer) {
    connection_detach(pconn);
  }

  /* Try to attach to the original player */
  if ((NULL != pconn->server.delegation.playing
      || pconn->server.delegation.observer)
      && !connection_attach(pconn, pconn->server.delegation.playing,
                            pconn->server.delegation.observer)) {
    return FALSE;
  }

  /* Reset data. */
  pconn->server.delegation.status = FALSE;
  pconn->server.delegation.playing = NULL;
  pconn->server.delegation.observer = FALSE;
  if (conn_get_player(pconn) != NULL) {
    struct player *plr = conn_get_player(pconn);
    plr->server.orig_username[0] = '\0';
  }

  /* Restore the username of the player which delegated control. */
  sz_strlcpy(pplayer->username, pplayer->server.orig_username);
  pplayer->server.orig_username[0] = '\0';
  /* Send updated username to all connections. */
  send_player_info_c(pplayer, NULL);

  return TRUE;
}

/*****************************************************************************
 Close a connection. Use this in the server to take care of delegation stuff
 (reset the username of the controlled connection).
*****************************************************************************/
void connection_close_server(struct connection *pconn, const char *reason)
{
  /* Restore possible delegations before the connection is closed. */
  connection_delegate_restore(pconn);
  connection_close(pconn, reason);
}
