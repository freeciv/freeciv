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
#include <string.h>
#include <ctype.h>

#include "fcintl.h"
#include "game.h"
#include "packets.h"
#include "player.h"
#include "shared.h"
#include "support.h"

#include "stdinhand.h"

#include "handchat.h"

#define MAX_LEN_CHAT_NAME (2*MAX_LEN_NAME+10)   /* for form_chat_name() names */

/**************************************************************************
  Formulate a name for this connection, prefering the player name when
  available and unambiguous (since this is the "standard" case), else
  connection name and/or player name.  Player name is always "plain",
  and connection name in brackets.  If connection name includes player
  name, assume connection name is a "modified" player name and don't use
  both (eg "(1-Shaka)" instead of "Shaka (1-Shaka)").  
**************************************************************************/
static void form_chat_name(struct connection *pconn, char *buffer, int len)
{
  struct player *pplayer = pconn->player;

  if (!pplayer) {
    my_snprintf(buffer, len, "(%s)", pconn->name);
  } else if (conn_list_size(&pplayer->connections)==1) {
    mystrlcpy(buffer, pplayer->name, len);
  } else if (strstr(pconn->name, pplayer->name)) {
    /* Fixme: strstr above should be case-independent */
    my_snprintf(buffer, len, "(%s)", pconn->name);
  } else {
    my_snprintf(buffer, len, "%s (%s)", pplayer->name, pconn->name);
  }
}
				
/**************************************************************************
  Complain to sender that name was ambiguous.
  'player_conn' is 0 for player names, 1 for connection names.
**************************************************************************/
static void complain_ambiguous(struct connection *pconn, const char *name,
			       int player_conn)
{
  struct packet_generic_message genmsg;
  genmsg.x = genmsg.y = genmsg.event = -1;

  if (player_conn==0) {
    my_snprintf(genmsg.message, sizeof(genmsg.message),
		_("Game: %s is an ambiguous player name-prefix."), name);
  } else {
    my_snprintf(genmsg.message, sizeof(genmsg.message),
		_("Game: %s is an ambiguous connection name-prefix."), name);
  }
  send_packet_generic_message(pconn, PACKET_CHAT_MSG, &genmsg);
}

/**************************************************************************
  Send private message to single connection.
**************************************************************************/
static void chat_msg_to_conn(struct connection *sender,
			     struct connection *dest, char *msg)
{
  char sender_name[MAX_LEN_CHAT_NAME], dest_name[MAX_LEN_CHAT_NAME];
  struct packet_generic_message genmsg;
  genmsg.x = genmsg.y = genmsg.event = -1;
  
  msg = skip_leading_spaces(msg);
  
  form_chat_name(sender, sender_name, sizeof(sender_name));
  form_chat_name(dest, dest_name, sizeof(dest_name));

  my_snprintf(genmsg.message, sizeof(genmsg.message),
	      "->*%s* %s", dest_name, msg);
  send_packet_generic_message(sender, PACKET_CHAT_MSG, &genmsg);

  if (sender != dest) {
    my_snprintf(genmsg.message, sizeof(genmsg.message),
		"*%s* %s", sender_name, msg);
    send_packet_generic_message(dest, PACKET_CHAT_MSG, &genmsg);
  }
}

/**************************************************************************
  Send private message to multi-connected player.
**************************************************************************/
static void chat_msg_to_player_multi(struct connection *sender,
				     struct player *pdest, char *msg)
{
  char sender_name[MAX_LEN_CHAT_NAME];
  struct packet_generic_message genmsg;
  genmsg.x = genmsg.y = genmsg.event = -1;
  
  msg = skip_leading_spaces(msg);
  
  form_chat_name(sender, sender_name, sizeof(sender_name));

  my_snprintf(genmsg.message, sizeof(genmsg.message),
	      "->[%s] %s", pdest->name, msg);
  send_packet_generic_message(sender, PACKET_CHAT_MSG, &genmsg);

  my_snprintf(genmsg.message, sizeof(genmsg.message),
	      "[%s] %s", sender_name, msg);
  conn_list_iterate(pdest->connections, dest_conn) {
    if (dest_conn != sender) {
      send_packet_generic_message(dest_conn, PACKET_CHAT_MSG, &genmsg);
    }
  } conn_list_iterate_end;
}

/**************************************************************************
  Handle a chat message packet from client:
  1. Work out whether it is a server command and if so run it;
  2. Otherwise work out whether it is directed to a single player, or
     to a single connection, and send there.  (For a player, send to
     all clients connected as that player, in multi-connect case);
  3. Else send to all connections (game.est_connections).

  In case 2, there can sometimes be ambiguity between player and
  connection names.  By default this tries to match player name first,
  and only if that fails tries to match connection name.  User can
  override this and specify connection only by using two colons ("::")
  after the destination name/prefix, instead of one.

  The message sent will name the sender, and via format differences
  also indicates whether the recipient is either all connections, a
  single connection, or multiple connections to a single player.

  Message is also echoed back to sender (with different format),
  avoiding sending both original and echo if sender is in destination
  set.
**************************************************************************/
void handle_chat_msg(struct connection *pconn,
		     struct packet_generic_message *packet)
{
  struct packet_generic_message genmsg;
  char sender_name[MAX_LEN_CHAT_NAME];
  char *cp;
  bool double_colon;

  /* this loop to prevent players from sending multiple lines
   * which can be abused */
  genmsg.x = -1;
  genmsg.y = -1;
  genmsg.event =-1;
  for (cp = packet->message; *cp != '\0'; ++cp)
    if(!isprint(*cp & 0x7f)) {
      *cp='\0';
      break;
    }

  /* Server commands are prefixed with '/', which is an obvious
     but confusing choice: even before this feature existed,
     novice players were trying /who, /nick etc.
     So consider this an incentive for IRC support,
     or change it in stdinhand.h - rp
  */
  if (packet->message[0] == SERVER_COMMAND_PREFIX) {
    /* pass it to the command parser, which will chop the prefix off */
    handle_stdin_input(pconn, packet->message);
    return;
  }

  /* Want to allow private messages with "player_name: message",
     (or "connection_name: message"), including unambiguously
     abbreviated player/connection name, but also want to allow
     sensible use of ':' within messages, and _also_ want to
     notice intended private messages with (eg) mis-spelt name.

     Approach:
     
     If there is no ':', or ':' is first on line,
          message is global (send to all players)
     else if the ':' is double, try matching part before "::" against
          connection names: for single match send to that connection,
	  for multiple matches complain, else goto heuristics below.
     else try matching part before (single) ':' against player names:
          for single match send to that player, for multiple matches
	  complain
     else try matching against connection names: for single match send
          to that connection, for multiple matches complain
     else if some heuristics apply (a space anywhere before first ':')
          then treat as global message,
     else complain (might be a typo-ed intended private message)
  */
  
  cp=strchr(packet->message, ':');

  if (cp && (cp != &packet->message[0])) {
    enum m_pre_result match_result_player, match_result_conn;
    struct player *pdest = NULL;
    struct connection *conn_dest = NULL;
    char name[MAX_LEN_NAME];
    char *cpblank;

    mystrlcpy(name, packet->message, MIN(sizeof(name), cp-packet->message+1));

    double_colon = (*(cp+1) == ':');
    if (double_colon) {
      conn_dest = find_conn_by_name_prefix(name, &match_result_conn);
      if (match_result_conn == M_PRE_AMBIGUOUS) {
	complain_ambiguous(pconn, name, 1);
	return;
      }
      if (conn_dest && match_result_conn < M_PRE_AMBIGUOUS) {
	chat_msg_to_conn(pconn, conn_dest, cp+2);
	return;
      }
    } else {
      /* single colon */
      pdest = find_player_by_name_prefix(name, &match_result_player);
      if (match_result_player == M_PRE_AMBIGUOUS) {
	complain_ambiguous(pconn, name, 0);
	return;
      }
      if (pdest && match_result_player < M_PRE_AMBIGUOUS) {
	int nconn = conn_list_size(&pdest->connections);
	if (nconn==1) {
	  chat_msg_to_conn(pconn, conn_list_get(&pdest->connections, 0), cp+1);
	  return;
	} else if (nconn>1) {
	  chat_msg_to_player_multi(pconn, pdest, cp+1);
	  return;
	}
	/* else try for connection name match before complaining */
      }
      conn_dest = find_conn_by_name_prefix(name, &match_result_conn);
      if (match_result_conn == M_PRE_AMBIGUOUS) {
	complain_ambiguous(pconn, name, 1);
	return;
      }
      if (conn_dest && match_result_conn < M_PRE_AMBIGUOUS) {
	chat_msg_to_conn(pconn, conn_dest, cp+1);
	return;
      }
      if (pdest && match_result_player < M_PRE_AMBIGUOUS) {
	/* Would have done something above if connected */
	my_snprintf(genmsg.message, sizeof(genmsg.message),
		    _("Game: %s is not connected."), pdest->name);
	send_packet_generic_message(pconn, PACKET_CHAT_MSG, &genmsg);
	return;
      }
    }
    /* Didn't match; check heuristics to see if this is likely
     * to be a global message
     */
    cpblank=strchr(packet->message, ' ');
    if (!cpblank || (cp < cpblank)) {
      if (double_colon) {
	my_snprintf(genmsg.message, sizeof(genmsg.message),
		    _("Game: There is no connection by the name %s."), name);
      } else {
	my_snprintf(genmsg.message, sizeof(genmsg.message),
		    _("Game: There is no player nor connection by the name %s."),
		    name);
      }
      send_packet_generic_message(pconn, PACKET_CHAT_MSG, &genmsg);
      return;
    }
  }
  /* global message: */
  form_chat_name(pconn, sender_name, sizeof(sender_name));
  my_snprintf(genmsg.message, sizeof(genmsg.message),
	      "<%s> %s", sender_name, packet->message);
  lsend_packet_generic_message(&game.est_connections, PACKET_CHAT_MSG, 
			       &genmsg);
}
