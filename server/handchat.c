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

/**************************************************************************
  Handle a chat message packet from client:
  - Work out whether it is a server command and if so run it;
  - Otherwise work out whether it is directed to a single player,
    and send there (all connections for that player);
  - Else send to all connections (game.est_connections).
  Fixme: There is currently no way to direct chat message to a single
  connection for multi-connected players, or to non-player connections.
  (May need to enforce tighter contraints on connection names to make this
  unambiguous, or use a different/additional prefix/marker on chatline.)
  Sent message is echoed back to sender (back to all player connections
  for multi-connected player with message sent to single other player).
**************************************************************************/
void handle_chat_msg(struct connection *pconn,
		     struct packet_generic_message *packet)
{
  struct player *pplayer = pconn->player;
  struct packet_generic_message genmsg;
  char *sender_name = (pplayer ? pplayer->name : pconn->name);    /* ? */
  char *cp;

  /* this loop to prevent players from sending multiple lines
   * which can be abused */
  genmsg.x = -1;
  genmsg.y = -1;
  genmsg.event =-1;
  for(cp=packet->message; *cp; ++cp)
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
    if (pplayer) { /* fixme */
      handle_stdin_input(pplayer, packet->message);
    }
    return;
  }

  /* Want to allow private messages with "player_name: message",
     including unambiguously abbreviated player name, but also want
     to allow sensible use of ':' within messages, and _also_ want to
     notice intended private messages with (eg) mis-spelt player name.

     Approach:
     
     If there is no ':', or ':' is first on line,
          message is global (send to all players)
     else if the part before ':' matches one player unambiguously,
          send to that one player
     else if it matches several players ambiguously, complain,
     else if some heuristics apply (a space anywhere before first ':')
          then treat as global message,
     else complain (might be a typo-ed intended private message)
  */
  
  cp=strchr(packet->message, ':');

  if (cp != NULL && (cp != &packet->message[0])) {
    enum m_pre_result match_result;
    struct player *pdest = NULL;
    char name[MAX_LEN_NAME];
    char *cpblank;

    mystrlcpy(name, packet->message, MIN(sizeof(name), cp-packet->message+1));

    pdest = find_player_by_name_prefix(name, &match_result);
    
    if(pdest && match_result < M_PRE_AMBIGUOUS) {
      struct conn_list *echo_back_dest;
      if (pconn->player) {
	echo_back_dest = &pconn->player->connections;
      } else {
	echo_back_dest = &pconn->self;
      }
      my_snprintf(genmsg.message, sizeof(genmsg.message),
		  "->*%s* %s", pdest->name, cp+1+(*(cp+1)==' '));
      lsend_packet_generic_message(echo_back_dest, PACKET_CHAT_MSG, &genmsg);
      if (pdest != pconn->player) {   /* don't echo message to self twice */
	my_snprintf(genmsg.message, sizeof(genmsg.message),
		    "*%s* %s", sender_name, cp+1+(*(cp+1)==' '));
	lsend_packet_generic_message(&pdest->connections, PACKET_CHAT_MSG,
				     &genmsg);
      }
      return;
    }
    if(match_result == M_PRE_AMBIGUOUS) {
      my_snprintf(genmsg.message, sizeof(genmsg.message),
		  _("Game: %s is an ambiguous name-prefix."), name);
      send_packet_generic_message(pconn, PACKET_CHAT_MSG, &genmsg);
      return;
    }
    /* Didn't match; check heuristics to see if this is likely
     * to be a global message
     */
    cpblank=strchr(packet->message, ' ');
    if (!cpblank || (cp < cpblank)) {
      my_snprintf(genmsg.message, sizeof(genmsg.message),
		  _("Game: There's no player by the name %s."), name);
      send_packet_generic_message(pconn, PACKET_CHAT_MSG, &genmsg);
      return;
    }
  }
  /* global message: */
  my_snprintf(genmsg.message, sizeof(genmsg.message),
	      "<%s> %s", sender_name, packet->message);
  lsend_packet_generic_message(&game.est_connections, PACKET_CHAT_MSG, 
			       &genmsg);
}
