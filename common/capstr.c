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

#include <stdlib.h>		/* getenv() */
#include <string.h>

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

#define CAPABILITY "+1.14.0 conn_info +occupied team tech_impr_gfx " \
                   "city_struct_minor_cleanup obsolete_last class_legend " \
                   "+impr_req +waste +fastfocus +continent +small_dipl " \
                   "+no_nation_selected +diplomacy +no_extra_tiles " \
                   "+diplomacy2 +citizens_style +root_tech auth " \
                   "+nat_ulimit +retake +goto_pack borders dip"

/* "+1.14.0" is protocol for 1.14.0 release.
 *
 * "conn_info" is sending the conn_id field. To preserve compatability
 * with old clients trying to connect this should persist across releases.
 *
 * "occupied": don't send info about units which are inside enemy
 * cities but instead use the occupied flag of short_city_info.
 *
 * "team" is support for player teams
 *
 * "tech_impr_gfx" is support for loading of ruleset-specified
 * technology and city improvement icons.
 *
 * "city_struct_minor_cleanup" just removes one unused variable from the
 * city struct, which no longer needs to be sent to the client.
 *
 * "obsolete_last" means A_LAST is used to mark improvements that are never
 * obsoleted.  Previously A_NONE was used.
 *
 * "class_legend" means each nation has a class and an (optional) legend
 * text associated with it.
 *
 * "impr_req" is the ability to have city improvements as a prerequisite for
 * building specific types of units
 *
 * "waste" is support for penalizing city shield production based
 * on distance from capital city, varying by government type.
 *
 * "fastfocus" removes the server from client unit focus.
 *
 * "continent": the server gives the client continent information
 *
 * "small_dipl" makes the player diplomacy data in the player packet
 * smaller, by sending 8-bit instead of 32-bit values.
 *
 * "no_nation_selected" means that -1 (NO_NATION_SELECTED) is used for
 * players who have no assigned nation (rather than MAX_NUM_NATIONS).
 *
 * "diplomacy": changed requirements for diplomatic meetings
 *
 * "no_extra_tiles" means that the extra, unknown (NODRAW) tiles will not be
 * sent to clients (previously used to help with drawing the terrain).
 *
 * "diplomacy2": unified and simplified handling of diplomatic request
 *
 * "citizens_style": is support for loading of ruleset-specified
 * multi style citizens icons.
 *
 * "root_tech" restricts who can acquire technology
 *
 * "auth": client can authenticate and server has the ability to.
 *
 * "nat_ulimit" means that the MAX_NUM_NATIONS limit has been removed,
 * allowing easy adding of arbitrarily many nations.
 *
 * "retake" means that a client can switch players during a running game.
 *
 * "goto_pack" changes the goto route packet to send (255,255) instead of
 * map.xsize/map.ysize to mark the end of the goto chunk.
 *
 * "borders" is support for national borders.
 * 
 * "dip" is support for reporting the state of ability to do 
 *  diplomacy restrictions (see game.diplomacy)
 */

void init_our_capability(void)
{
  const char *s;

  s = getenv("FREECIV_CAPS");
  if (!s) {
    s = CAPABILITY;
  }
  sz_strlcpy(our_capability_internal, s);
}
