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
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <game.h>
#include <packets.h>
#include <player.h>
#include <shared.h>


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
    if(!isprint(*cp)) {
      *cp='\0';
      break;
    }
  
  cp=strchr(packet->message, ':');
  
  if(cp && *(cp+1)!=')') { /* msg someone mode */
    struct player *pdest=0;
    int nlen, nmatches=0;
    char name[MAX_LENGTH_NAME];

    nlen=MIN(MAX_LENGTH_NAME-1, cp-packet->message);
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
    }
    else if(nmatches>=2) {
      sprintf(genmsg.message, "Game: %s is an ambiguous name-prefix", name);
      send_packet_generic_message(pplayer->conn, PACKET_CHAT_MSG, &genmsg);
    }
    else {
      sprintf(genmsg.message, "Game: there's no player by the name %s", name);
      send_packet_generic_message(pplayer->conn, PACKET_CHAT_MSG, &genmsg);
    }
  }
  else {
    sprintf(genmsg.message, "<%s> %s", pplayer->name, packet->message);
    for(i=0; i<game.nplayers; i++)
      send_packet_generic_message(game.players[i].conn, PACKET_CHAT_MSG, 
				  &genmsg);
  }
}
