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

#include "packets.h"		/* MAX_LEN_CAPSTR */
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

#define CAPABILITY "+1.8 caravan1 nuke clientcommands" \
    " +terrainrulesets1 +governmentrulesets2 +num_units +tilespec" \
    " +ruleset_control +ask_pillage +gen_tech2 +nationsruleset2" \
    " +long_names +paratroopers2 +helptext2 dconn_in_sel_nat" \
    " +airbase"

/* "caravan1" means the server automatically establishes a traderoute
   when a caravan type unit moves into an enemy city.  For older
   servers the client has to explicitly ask for a trade route.

   "nuke" means that it is possible to explode nuclear units
   at a tile without enemy units.  Maybe it should be mandatory
   because it improves player's fighting capabilities.

   "clientcommands" indicates that the server supports server commands
   sent by clients.

   "terrainrulesets1" means that the protocol is extended to handle
   the new, expanded terrain rulesets.  (jjm@codewell.com 13aug1999)

   "governmentrulesets2" is for the new government.ruleset packages;
   includes unit upkeep changes and split flags into actual flags
   and hints.
  
   "num_units" means the number of unit types is variable.
 
   "tilespec" means units etc send strings for graphic instead of
   numbers, to be used with tilespec system.
   
   "ruleset_control" means new ruleset_control packet, and related
   new handling of some ruleset info.

   "ask_pillage" means that the player is asked for what to pillage.

   "gen_tech2" means generalised some tech data (packets changed),
   added flags for techs and variable number of techs

   "nationsruleset2" is for new nations.ruleset changes;
   this implies/requires/replaces old "citynamesuggest" tag;
   now with multiple leaders and other changes compared to
   original "nationsruleset" tag.

   "long_names" means that player names longer than 9 chars are allowed.

   "paratroopers2" is for the support of the "Paratroopers" unit flag
   and some paratroopers specific fields.

   "helptext2" means unit type and tech helptext is sent from server.

   "dconn_in_sel_nat" means that the server can handle the last client
   disconnecting from the Select Nation dialog.

   "airbase" means the airbase map special
*/

void init_our_capability(void)
{
  char *s;

  s = getenv("FREECIV_CAPS");
  if (s == NULL) {
    s = CAPABILITY;
  }
  strncpy(our_capability_internal, s, MAX_LEN_CAPSTR-1);
}
