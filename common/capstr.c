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

#define CAPABILITY "+1.14.delta +last_turns_shield_surplus veteran +orders " \
                   "+starter +union +iso_maps +big_map_size +orders2client " \
                   "+change_production +tilespec1 +no_earth +trans " \
                   "+want_hack invasions bombard +killstack2 spec +spec2 " \
                   "+city_map startunits +turn_last_built +happyborders " \
                   "+connid +love2"

/* "+1.14.delta" is the new delta protocol for 1.14.0-dev.
 *
 * "last_turns_shield_surplus" means the surplus from the previous turn is
 * tracked by the server and sent to the client.  This information is used
 * in determining penalties when switching production.
 *
 * "orders" means client orders is used for client-side goto and patrol.
 *
 * "veteran" means the extended veteran system.
 *
 * "starter" means the Starter terrain flag is supported.
 *
 * "union" is team research ability
 *
 * "iso_maps" means iso-maps are supported by both server and client!
 *
 * "big_map_size" means [xy]size info is sent as UINT16 
 *
 * "orders2client" means that the server sends back the orders to the client.
 *
 * "change_production" is the E_CITY_PRODUCTION_CHANGED event.
 *
 * "tilespec1" means changed graphic strings in terrain.ruleset.
 *
 * "no_earth" means that the map.is_earth value is gone; replaced by
 * ptile->spec_sprite
 *
 * "trans" means that the transported_by field is sent to the client.
 *
 * "want_hack" means that the client is capable of asking for hack properly.
 * 
 * "invasions" means ground units moving from ocean to land now lose all
 * their movement; changable by ruleset option.
 * 
 * "killstack2" means ruleset option to ignore unit killstack effect, and now
 * it's a boolean.
 *
 * "bombard" means units support the bombard ability.
 * 
 * "spec" is configurable specialists
 *
 * "spec2" is semi-configurable specialists in an array
 *
 * "city_map" means the city_map is sent as an array instead of a bitfield.
 *
 * "startunits" means the initial units are stored as a server string var.
 *
 * "turn_last_built" means that turn_last_built is stored as a turn
 * 
 * "happyborders" means that units may not cause unhappiness inside
 * our own borders.
 * 
 * "connid" adds the connection id of whoever sends a message to the 
 * info sent to clients.
 * 
 * "love" means that we show the AI love for you in the client
 * "love2" includes a bugfix
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
