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

/* +2.0 is the capability string for the 2.0.x release(s).
 *
 * "conn_ping_info" means the packet_conn_ping_info uses MAX_NUM_CONNECTIONS
 * not MAX_NUM_PLAYERS.
 *
 * "username_info" means that the username is sent in the player_info packet
 *
 * "ReportFreezeFix" allows clients to correctly freeze reports and agents
 *                   over turn change.
 *
 *   - No new manditory capabilities can be added to the release branch; doing
 *     so would break network capability of supposedly "compatible" releases.
 *
 *   - Avoid adding a new manditory capbility to the development branch for
 *     as long as possible.  We want to maintain network compatibility with
 *     the stable branch for as long as possible.
 */
#define CAPABILITY "+2.0.10 conn_ping_info username_info new_hack ReportFreezeFix"

void init_our_capability(void)
{
  const char *s;

  s = getenv("FREECIV_CAPS");
  if (!s) {
    s = CAPABILITY;
  }
  sz_strlcpy(our_capability_internal, s);
}
