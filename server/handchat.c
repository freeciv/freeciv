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

#include "stdinhand.h"

#include "handchat.h"

/**************************************************************************
...
**************************************************************************/
void handle_chat_msg(struct player *pplayer, 
		     struct packet_generic_message *packet)
{
  int i;
  struct packet_generic_message genmsg;
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
    handle_stdin_input(pplayer, packet->message);
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
    struct player *pdest=0;
    int nlen, nmatches=0;
    char name[MAX_LEN_NAME];
    char *cpblank;

    nlen=MIN(MAX_LEN_NAME-1, cp-packet->message);
    strncpy(name, packet->message, nlen);
    name[nlen]='\0';
     
    if(!(pdest=find_player_by_name(name))) {
      /* no full match,  now look for pre-match */
      for(i=0; i<game.nplayers; ++i) {
	int j;
	for(j=0; j<nlen; ++j)
	  if(tolower(packet->message[j])!=tolower(game.players[i].name[j]))
	    break;
	if(j==nlen) {
	  ++nmatches;
	  pdest=&game.players[i];
	}
      }
    }
			   
    if(pdest && nmatches<=1) {
      sprintf(genmsg.message, "->*%s* %s", pdest->name, cp+1+(*(cp+1)==' '));
      send_packet_generic_message(pplayer->conn, PACKET_CHAT_MSG, &genmsg);
      sprintf(genmsg.message, "*%s* %s",
	      pplayer->name, cp+1+(*(cp+1)==' '));
      send_packet_generic_message(pdest->conn, PACKET_CHAT_MSG, &genmsg);
      return;
    }
    if(nmatches>=2) {
      sprintf(genmsg.message, _("Game: %s is an ambiguous name-prefix"), name);
      send_packet_generic_message(pplayer->conn, PACKET_CHAT_MSG, &genmsg);
      return;
    }
    /* Didn't match; check heuristics to see if this is likely
     * to be a global message
     */
    cpblank=strchr(packet->message, ' ');
    if (!cpblank || (cp < cpblank)) {
      sprintf(genmsg.message, _("Game: there's no player by the name %s"), name);
      send_packet_generic_message(pplayer->conn, PACKET_CHAT_MSG, &genmsg);
      return;
    }
  }
  /* global message: */
  sprintf(genmsg.message, "<%s> %s", pplayer->name, packet->message);
  for(i=0; i<game.nplayers; i++)
    send_packet_generic_message(game.players[i].conn, PACKET_CHAT_MSG, 
				&genmsg);
}
