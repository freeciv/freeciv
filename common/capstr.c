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

#include <string.h>
#include <stdlib.h>		/* getenv() */

#include "connection.h"		/* MAX_LEN_CAPSTR */
#include "support.h"

#include "capstr.h"

static char our_capability_internal[MAX_LEN_CAPSTR];
const char * const our_capability = our_capability_internal;

/* Capabilities: original author: Mitch Davis (mjd@alphalink.com.au)
 *
 * The capability string is a string clients and servers trade to find
 * out if they can talk to each other, and using which protocol version,
 * and which features and behaviors to expect.  The string is a list of
 * words, separated by whitespace and/or commas, where each word indicates
 * a capability that this version of Freeciv understands.
 * If a capability word is mandatory, it should start with a "+".
 *
 * eg, #define CAPABILITY "+1.6, MapScroll, +AutoSettlers"
 *
 * Client and server functions can test these strings for a particular
 * capability by calling the functions in capability.c
 *
 * Each executable has a string our_capability (above), which gives the
 * capabilities of the running executable.  This is normally initialised
 * with CAPABILITY, but can be changed at run-time by setting the
 * FREECIV_CAPS environment variable, though that is probably mainly
 * useful for testing purposes.
 *
 * For checking the connections of other executables, each
 * "struct connection" has a capability string, which gives the
 * capability of the executable at the other end of the connection.
 * So for the client, the capability of the server is in
 * aconnection.capability, and for the server, the capabilities of 
 * connected clients are in game.players[i]->conn.capability
 * The client now also knows the capabilities of other clients,
 * via game.players[i]->conn.capability.
 *
 * Note the connection struct is a parameter to the functions to send and
 * receive packets, which may be convenient for adjusting how a packet is
 * sent or interpreted based on the capabilities of the connection.
 *
 * At the time of a major release, the capability string may be
 * simplified; eg, the example string above could be replaced by "+1.7".
 * (This should probably only happen if a mandatory capability has
 * been introduced since the previous release.)
 * Whoever makes such a change has responsibility to search the Freeciv
 * code, and look for places where people are using has_capability.
 * If you're taking a capability out of the string, because now every
 * client and server supports it, then you should take out the
 * if(has_capability()) code so that this code is always executed.
 *
 * (The savefile and ruleset files have strings which are used similarly,
 * and checked by the same has_capability function, but the strings there
 * are not directly related to the capability strings discussed here.)
 */

#define CAPABILITY "+1.11.6 conn_info pop_cost +turn +attributes"\
" new_bonus_tech fund_added +processing_packets angrycitizen +tile_trade"\
" init_techs short_worklists tech_cost_style +short_city_tile_trade"\
" +trade_size +new_nation_selection"
  
/* "+1.11.6" is protocol for 1.11.6 beta release.
  
   "conn_info" is sending the conn_id field. To preserve compatability
   with old clients trying to connect this should persist across releases.

   "pop_cost" is the capability of rulesets to specify how many units
   of population are consumed on unit creation and/or can be added to
   cities.

   "turn" additionally transfers game.turn to the client.

   "attributes" is the ability to request and transfer attribute blocks.

   "new_bonus_tech" is the ability to make every tech a bonus tech
   like A_PHILOSOPHY

   "fund_added" introduces support for fundamentalism form of government.

   "processing_packets" sends PACKET_PROCESSING_STARTED and
   PACKET_PROCESSING_FINISHED packets.

   "angrycitizen" introduces angry citizens, like those in
   civilization II.  They counts as 2 unhappy citizens, they are saved
   in ppl_angry[], and you must dealt with them before any citizens in
   a city can become unhappy.
   
   "tile_trade" sends the tile_trade field from struct city.

   "init_techs" allows global and nation specific initial techs to be
   specified in rulesets.

   "short_worklists" sends short worklists to reduce bandwidth used.

   "tech_cost_style" allows using different algorithms for calculation
   technology cost. The algorithm used is selected with
   game.ruleset.tech_cost_style and game.ruleset.tech_leakage.

   "short_city_tile_trade" sends the tile trade in the short_city
   packet.

   "trade_size" transfer game.notradesize and game.fulltradesize to
   the client.

   "new_nation_selection" transfer array of used nations instead of
   bit mask
*/

void init_our_capability(void)
{
  char *s;

  s = getenv("FREECIV_CAPS");
  if (!s) {
    s = CAPABILITY;
  }
  sz_strlcpy(our_capability_internal, s);
}
