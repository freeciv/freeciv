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

#include <assert.h>

#include "government.h"
#include "packets.h"
#include "spaceship.h"
#include "spacerace.h"

#include "advspace.h"

/*
 *  This borrows heavily from autoplace code already in the client source.
 *  AJS, 19990610
 */

int ai_spaceship_autoplace(struct player *pplayer, struct player_spaceship *ship)
{
  struct government *g = get_gov_pplayer(pplayer);
  struct packet_spaceship_action packet;
  int i,retval=0;
  
  while (ship->modules > (ship->habitation + ship->life_support
		       + ship->solar_panels)) {
    
    int nice = g->flags & G_IS_NICE;
    /* "nice" governments prefer to keep success 100%;
     * others build habitation first (for score?)  (Thanks Massimo.)
     */

    packet.action =
      (ship->habitation==0)   ? SSHIP_ACT_PLACE_HABITATION :
      (ship->life_support==0) ? SSHIP_ACT_PLACE_LIFE_SUPPORT :
      (ship->solar_panels==0) ? SSHIP_ACT_PLACE_SOLAR_PANELS :
      ((ship->habitation < ship->life_support)
       && (ship->solar_panels*2 >= ship->habitation + ship->life_support + 1))
                              ? SSHIP_ACT_PLACE_HABITATION :
      (ship->solar_panels*2 < ship->habitation + ship->life_support)
                              ? SSHIP_ACT_PLACE_SOLAR_PANELS :
      (ship->life_support<ship->habitation)
                              ? SSHIP_ACT_PLACE_LIFE_SUPPORT :
      (nice && (ship->life_support <= ship->habitation)
       && (ship->solar_panels*2 >= ship->habitation + ship->life_support + 1))
                              ? SSHIP_ACT_PLACE_LIFE_SUPPORT :
      (nice)                  ? SSHIP_ACT_PLACE_SOLAR_PANELS :
                                SSHIP_ACT_PLACE_HABITATION;

    if (packet.action == SSHIP_ACT_PLACE_HABITATION) {
      packet.num = ship->habitation + 1;
    } else if(packet.action == SSHIP_ACT_PLACE_LIFE_SUPPORT) {
      packet.num = ship->life_support + 1;
    } else {
      packet.num = ship->solar_panels + 1;
    }
    assert(packet.num<=NUM_SS_MODULES/3);

    handle_spaceship_action(pplayer, &packet);
    retval=1;
  }
  while (ship->components > ship->fuel + ship->propulsion) {
    if (ship->fuel <= ship->propulsion) {
      packet.action = SSHIP_ACT_PLACE_FUEL;
      packet.num = ship->fuel + 1;
    } else {
      packet.action = SSHIP_ACT_PLACE_PROPULSION;
      packet.num = ship->propulsion + 1;
    }
    handle_spaceship_action(pplayer, &packet);
    retval=1;
  }
  while (ship->structurals > num_spaceship_structurals_placed(ship)) {
    /* Want to choose which structurals are most important.
       Else we first want to connect one of each type of module,
       then all placed components, pairwise, then any remaining
       modules, or else finally in numerical order.
    */
    int req = -1;
    
    if (!ship->structure[0]) {
      /* if we don't have the first structural, place that! */
      packet.action = SSHIP_ACT_PLACE_STRUCTURAL;
      packet.num = 0;
      handle_spaceship_action(pplayer, &packet);
      retval=1;
    }
    
    if (ship->habitation >= 1
	&& !ship->structure[modules_info[0].required]) {
      req = modules_info[0].required;
    } else if (ship->life_support >= 1
	       && !ship->structure[modules_info[1].required]) {
      req = modules_info[1].required;
    } else if (ship->solar_panels >= 1
	       && !ship->structure[modules_info[2].required]) {
      req = modules_info[2].required;
    } else {
      int i;
      for(i=0; i<NUM_SS_COMPONENTS; i++) {
	if ((i%2==0 && ship->fuel > (i/2))
	    || (i%2==1 && ship->propulsion > (i/2))) {
	  if (!ship->structure[components_info[i].required]) {
	    req = components_info[i].required;
	    break;
	  }
	}
      }
    }
    if (req == -1) {
      for(i=0; i<NUM_SS_MODULES; i++) {
	if ((i%3==0 && ship->habitation > (i/3))
	    || (i%3==1 && ship->life_support > (i/3))
	    || (i%3==2 && ship->solar_panels > (i/3))) {
	  if (!ship->structure[modules_info[i].required]) {
	    req = modules_info[i].required;
	    break;
	  }
	}
      }
    }
    if (req == -1) {
      for(i=0; i<NUM_SS_STRUCTURALS; i++) {
	if (!ship->structure[i]) {
	  req = i;
	  break;
	}
      }
    }
    /* sanity: */
    assert(req!=-1);
    assert(!ship->structure[req]);
    
    /* Now we want to find a structural we can build which leads to req.
       This loop should bottom out, because everything leads back to s0,
       and we made sure above that we do s0 first.
     */
    while(!ship->structure[structurals_info[req].required]) {
      req = structurals_info[req].required;
    }
    packet.action = SSHIP_ACT_PLACE_STRUCTURAL;
    packet.num = req;
    handle_spaceship_action(pplayer, &packet);
    retval=1;
  }
  return retval;
}
