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
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "support.h"
#include "version.h"

#include "user_db.h"
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

#define GUEST_NAME "guest"

#define MIN_PASSWORD_LEN  6  /* minimum length of password */
#define MIN_PASSWORD_CAPS 0  /* minimum number of capital letters required */
#define MIN_PASSWORD_NUMS 0  /* minimum number of numbers required */

#define MAX_AUTHENTICATION_TRIES 3

/* after each wrong guess for a password, the server waits this
 * many seconds to reply to the client */
static const int auth_fail_period[] = { 1, 1, 2, 3 };

static void establish_new_connection(struct connection *pconn);
static void reject_new_connection(const char *msg, struct connection *pconn);

static bool is_guest_name(const char *name);
static void get_unique_guest_name(char *name);
static bool is_good_password(const char *password, char *msg);

/**************************************************************************
  This is used when a new player joins a server, before the game
  has started.  If pconn is NULL, is an AI, else a client.

  N.B. this only attachs a connection to a player if 
       pconn->name == player->username
**************************************************************************/
static void establish_new_connection(struct connection *pconn)
{
  struct conn_list *dest = &pconn->self;
  struct player *pplayer;
  struct packet_server_join_reply packet;
  char hostname[512];

  /* zero out the password */
  memset(pconn->server.password, 0, sizeof(pconn->server.password));

  /* send off login_replay packet */
  packet.you_can_join = TRUE;
  sz_strlcpy(packet.capability, our_capability);
  my_snprintf(packet.message, sizeof(packet.message), _("%s Welcome"),
              pconn->username);
  sz_strlcpy(packet.challenge_file, create_challenge_filename());
  packet.conn_id = pconn->id;
  send_packet_server_join_reply(pconn, &packet);

  /* "establish" the connection */
  pconn->established = TRUE;

  /* introduce the server to the connection */
  if (my_gethostname(hostname, sizeof(hostname)) == 0) {
    notify_conn(dest, _("Welcome to the %s Server running at %s port %d."),
                freeciv_name_version(), hostname, srvarg.port);
  } else {
    notify_conn(dest, _("Welcome to the %s Server at port %d."),
                freeciv_name_version(), srvarg.port);
  }

  /* FIXME: this (getting messages about others logging on) should be a 
   * message option for the client with event */

  /* notify the console and other established connections that you're here */
  freelog(LOG_NORMAL, _("%s has connected from %s."),
          pconn->username, pconn->addr);
  conn_list_iterate(game.est_connections, aconn) {
    if (aconn != pconn) {
      notify_conn(&aconn->self, _("Server: %s has connected from %s."),
                  pconn->username, pconn->addr);
    }
  } conn_list_iterate_end;

  /* a player has already been created for this user, reconnect him */
  if ((pplayer = find_player_by_user(pconn->username))) {
    attach_connection_to_player(pconn, pplayer);

    if (server_state == RUN_GAME_STATE) {
      /* Player and other info is only updated when the game is running.
       * See the comment in lost_connection_to_client(). */
      send_packet_freeze_hint(pconn);
      send_rulesets(dest);
      send_all_info(dest);
      send_game_state(dest, CLIENT_GAME_RUNNING_STATE);
      send_player_info(NULL,NULL);
      send_diplomatic_meetings(pconn);
      send_packet_thaw_hint(pconn);
      send_packet_start_turn(pconn);
    }

    if (game.auto_ai_toggle && pplayer->ai.control) {
      toggle_ai_player_direct(NULL, pplayer);
    }
  } else if (server_state == PRE_GAME_STATE && game.is_new_game) {
    if (!attach_connection_to_player(pconn, NULL)) {
      notify_conn(dest, _("Couldn't attach your connection to new player."));
      freelog(LOG_VERBOSE, "%s is not attached to a player", pconn->username);
    } else {
      sz_strlcpy(pconn->player->name, pconn->username);
    }
  }

  /* remind the connection who he is */
  if (!pconn->player) {
    notify_conn(dest, _("You are logged in as '%s' connected to no player."),
                pconn->username);
  } else if (strcmp(pconn->player->name, ANON_PLAYER_NAME) == 0) {
    notify_conn(dest, _("You are logged in as '%s' connected to an "
                        "anonymous player."),
		pconn->username);
  } else {
    notify_conn(dest, _("You are logged in as '%s' connected to %s."),
                pconn->username, pconn->player->name);
  }

  /* if need be, tell who we're waiting on to end the game turn */
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

  /* if the game is running, players can just view the Players menu? --dwp */
  if (server_state != RUN_GAME_STATE) {
    show_players(pconn);
  }

  send_conn_info(dest, &game.est_connections);
  conn_list_insert_back(&game.est_connections, pconn);
  send_conn_info(&game.est_connections, dest);
  (void) send_server_info_to_metaserver(TRUE, FALSE);
}

/**************************************************************************
  send the rejection packet to the client.
**************************************************************************/
static void reject_new_connection(const char *msg, struct connection *pconn)
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
  if (strlen(req->username) == 0 || my_isdigit(req->username[0])
      || mystrcasecmp(req->username, "all") == 0
      || mystrcasecmp(req->username, "none") == 0
      || mystrcasecmp(req->username, ANON_USER_NAME) == 0) {
    my_snprintf(msg, sizeof(msg), _("Invalid username '%s'"), req->username);
    reject_new_connection(msg, pconn);
    freelog(LOG_NORMAL, _("Rejected connection from %s with invalid name."),
            pconn->addr);
    return FALSE;
  } 

  /* don't allow duplicate logins */
  conn_list_iterate(game.all_connections, aconn) {
    if (strcmp(req->username, aconn->username) == 0) { 
      my_snprintf(msg, sizeof(msg), _("'%s' already connected."), 
                  req->username);
      reject_new_connection(msg, pconn);
      freelog(LOG_NORMAL,
              _("Rejected connection from %s with duplicate login name."),
              aconn->addr);
      return FALSE;
    }
  } conn_list_iterate_end;

  if (srvarg.auth_enabled) {
    /* assign the client a unique guest name/reject if guests aren't allowed */
    if (is_guest_name(req->username)) {
      if (srvarg.auth_allow_guests) {
        char old_guest_name[MAX_LEN_NAME];

        sz_strlcpy(old_guest_name, req->username);
        get_unique_guest_name(req->username);

        if (strncmp(old_guest_name, req->username, MAX_LEN_NAME) != 0) {
          notify_conn(&pconn->self, _("Warning: the guest name '%s' has been "
                                      "taken, renaming to user '%s'."),
                      old_guest_name, req->username);
        }
      } else {
        reject_new_connection(_("Guests are not allowed on this server. "
                                "Sorry."), pconn);
        return FALSE;
      }
    } else {
      /* we are not a guest, we need an extra check as to whether a 
       * connection can be established: the client must authenticate itself */
      char tmpname[MAX_LEN_NAME] = "\0";
      char buffer[MAX_LEN_MSG];

      sz_strlcpy(pconn->username, req->username);

      switch(user_db_load(pconn)) {
      case USER_DB_ERROR:
        if (srvarg.auth_allow_guests) {
          sz_strlcpy(tmpname, pconn->username);
          get_unique_guest_name(tmpname); /* don't pass pconn->username here */
          sz_strlcpy(pconn->username, tmpname);

          freelog(LOG_ERROR, "Error reading database; connection -> guest");
          notify_conn(&pconn->self, _("There was an error reading the user "
                                      "database, logging in as guest "
                                      "connection '%s'."), pconn->username);
          establish_new_connection(pconn);
        } else {
          reject_new_connection(_("There was an error reading the user "
                                  "database and guest logins are not "
                                  "allowed. Sorry"), pconn);
          return FALSE;
        }
        break;
      case USER_DB_SUCCESS:
        /* we found a user */
        my_snprintf(buffer, sizeof(buffer), _("Enter password for %s:"),
                    pconn->username);
        dsend_packet_authentication_req(pconn, AUTH_LOGIN_FIRST, buffer);
        pconn->server.status = AS_REQUESTING_OLD_PASS;
        break;
      case USER_DB_NOT_FOUND:
        /* we couldn't find the user, he is new */
        if (srvarg.auth_allow_newusers) {
          sz_strlcpy(buffer, _("Enter a new password (and remember it)."));
          dsend_packet_authentication_req(pconn, AUTH_NEWUSER_FIRST, buffer);
          pconn->server.status = AS_REQUESTING_NEW_PASS;
        } else {
          reject_new_connection(_("This server allows only preregistered "
                                  "users. Sorry."), pconn);
          return FALSE;
        }
        break;
      default:
        assert(0);
        break;
      }
      return TRUE;
    }
  }

  sz_strlcpy(pconn->username, req->username);
  establish_new_connection(pconn);
  return TRUE;
}

/**************************************************************************
 sniff_packets calls this when pconn->server.authentication_stop == time(NULL)
 after an authentication fails
**************************************************************************/
void unfail_authentication(struct connection *pconn)
{
  assert(pconn->server.status == AS_FAILED);

  if (pconn->server.authentication_tries >= MAX_AUTHENTICATION_TRIES) {
    pconn->server.status = AS_NOT_ESTABLISHED;
    reject_new_connection(_("Sorry, too many wrong tries..."), pconn);
  } else {
    struct packet_authentication_req request;

    pconn->server.status = AS_REQUESTING_OLD_PASS;
    request.type = AUTH_LOGIN_RETRY;
    sz_strlcpy(request.message,
               _("Your password is incorrect. Try again."));
    send_packet_authentication_req(pconn, &request);
  }
}

/**************************************************************************
  Receives a password from a client and verifies it.
**************************************************************************/
bool handle_authentication_reply(struct connection *pconn, char *password)
{
  char msg[MAX_LEN_MSG];

  if (pconn->server.status == AS_REQUESTING_NEW_PASS) {

    /* check if the new password is acceptable */
    if (!is_good_password(password, msg)) {
      if (pconn->server.authentication_tries++ >= MAX_AUTHENTICATION_TRIES) {
        reject_new_connection(_("Sorry, too many wrong tries..."), pconn);
      } else {
        dsend_packet_authentication_req(pconn, AUTH_NEWUSER_RETRY,msg);
      }
      return TRUE;
    }

    /* the new password is good, create a database entry for
     * this user; we establish the connection in handle_db_lookup */
    sz_strlcpy(pconn->server.password, password);

    switch(user_db_save(pconn)) {
    case USER_DB_SUCCESS:
      break;
    case USER_DB_ERROR:
      notify_conn(&pconn->self, _("Warning: There was an error in saving "
                                  "to the database. Continuing, but your "
                                  "stats will not be saved."));
      freelog(LOG_ERROR, "Error writing to database for %s", pconn->username);
      break;
    default:
      assert(0);
    }

    establish_new_connection(pconn);
  } else if (pconn->server.status == AS_REQUESTING_OLD_PASS) { 
    if (strncmp(pconn->server.password, password, MAX_LEN_NAME) == 0) {
      pconn->server.status = AS_ESTABLISHED;
      establish_new_connection(pconn);
    } else {
      pconn->server.status = AS_FAILED;
      pconn->server.authentication_tries++;
      pconn->server.authentication_stop = time(NULL) + 
                          auth_fail_period[pconn->server.authentication_tries];
    }
  } else {
    freelog(LOG_VERBOSE, "%s is sending unrequested auth packets", 
            pconn->username);
  }

  return TRUE;
}

/**************************************************************************
  see if the name qualifies as a guest login name
**************************************************************************/
static bool is_guest_name(const char *name)
{
  return (mystrncasecmp(name, GUEST_NAME, strlen(GUEST_NAME)) == 0);
}

/**************************************************************************
  return a unique guest name
  WARNING: do not pass pconn->username to this function: it won't return!
**************************************************************************/
static void get_unique_guest_name(char *name)
{
  unsigned int i;

  /* first see if the given name is suitable */
  if (is_guest_name(name) && !find_conn_by_user(name)) {
    return;
  } 

  /* next try bare guest name */
  mystrlcpy(name, GUEST_NAME, MAX_LEN_NAME);
  if (!find_conn_by_user(name)) {
    return;
  }

  /* bare name is taken, append numbers */
  for (i = 1; ; i++) {
    my_snprintf(name, MAX_LEN_NAME, "%s%u", GUEST_NAME, i);


    /* attempt to find this name; if we can't we're good to go */
    if (!find_conn_by_user(name)) {
      break;
    }
  }
}

/**************************************************************************
 Verifies that a password is valid. Does some [very] rudimentary safety 
 checks. TODO: do we want to frown on non-printing characters?
 Fill the msg (length MAX_LEN_MSG) with any worthwhile information that 
 the client ought to know. 
**************************************************************************/
static bool is_good_password(const char *password, char *msg)
{
  int i, num_caps = 0, num_nums = 0;
   
  /* check password length */
  if (strlen(password) < MIN_PASSWORD_LEN) {
    my_snprintf(msg, MAX_LEN_MSG,
                _("Your password is too short, the minimum length is %d. "
                  "Try again."), MIN_PASSWORD_LEN);
    return FALSE;
  }
 
  my_snprintf(msg, MAX_LEN_MSG,
              _("The password must have at least %d capital letters, %d "
                "numbers, and be at minimum %d [printable] characters long. "
                "Try again."), 
              MIN_PASSWORD_CAPS, MIN_PASSWORD_NUMS, MIN_PASSWORD_LEN);

  for (i = 0; i < strlen(password); i++) {
    if (my_isupper(password[i])) {
      num_caps++;
    }
    if (my_isdigit(password[i])) {
      num_nums++;
    }
  }

  /* check number of capital letters */
  if (num_caps < MIN_PASSWORD_CAPS) {
    return FALSE;
  }

  /* check number of numbers */
  if (num_nums < MIN_PASSWORD_NUMS) {
    return FALSE;
  }

  if (!is_sane_name(password)) {
    return FALSE;
  }

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
    delayed_disconnect--;
    return;
  }

  unattach_connection_from_player(pconn);

  send_conn_info_remove(&pconn->self, &game.est_connections);
  if (server_state == RUN_GAME_STATE) {
    /* Player info is only updated when the game is running; this must be
     * done consistently or the client will end up with inconsistent errors.
     * At other times, the conn info (send_conn_info) is used by the client
     * to display player information.  See establish_new_connection(). */
    send_player_info(pplayer, NULL);
  }
  notify_if_first_access_level_is_available();

  /* Cancel diplomacy meetings */
  if (!pplayer->is_connected) { /* may be still true if multiple connections */
    players_iterate(other_player) {
      if (find_treaty(pplayer, other_player)) {
        handle_diplomacy_cancel_meeting_req(pplayer, other_player->player_no);
      }
    } players_iterate_end;
  }

  if (game.is_new_game
      && !pplayer->is_connected /* eg multiple controllers */
      && !pplayer->ai.control    /* eg created AI player */
      && (server_state == PRE_GAME_STATE 
          || server_state == SELECT_RACES_STATE)) {
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

  If pplayer is NULL, take the next available player that is not already 
  associated.
  Note "observer" connections do not count for is_connected. You must set
       pconn->obserber to TRUE before attaching!
**************************************************************************/
bool attach_connection_to_player(struct connection *pconn,
                                 struct player *pplayer)
{
  /* if pplayer is NULL, attach to first non-connected player slot */
  if (!pplayer) {
    if (game.nplayers > game.max_players 
        || game.nplayers > MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS) {
      return FALSE; 
    } else {
      pplayer = &game.players[game.nplayers];
      game.nplayers++;
    }
  }

  if (!pconn->observer) {
    sz_strlcpy(pplayer->username, pconn->username);
    pplayer->is_connected = TRUE;
  }

  pconn->player = pplayer;
  conn_list_insert_back(&pplayer->connections, pconn);
  conn_list_insert_back(&game.game_connections, pconn);

  return TRUE;
}
  
/**************************************************************************
  Remove pconn as a client connected to pplayer:
  Update pplayer->connections, pplayer->is_connected.

  pconn remains a member of game.est_connections.
**************************************************************************/
bool unattach_connection_from_player(struct connection *pconn)
{
  if (!pconn->player) {
    return FALSE; /* no player is attached to this conn */
  }

  conn_list_unlink(&pconn->player->connections, pconn);
  conn_list_unlink(&game.game_connections, pconn);

  pconn->player->is_connected = FALSE;
  pconn->observer = FALSE;

  /* If any other (non-observing) conn is attached to 
   * this player, the player is still connected. */
  conn_list_iterate(pconn->player->connections, aconn) {
    if (!aconn->observer) {
      pconn->player->is_connected = TRUE;
      break;
    }
  } conn_list_iterate_end;

  pconn->player = NULL;

  return TRUE;
}
