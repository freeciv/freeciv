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
#include <string.h>

#include "events.h"
#include "game.h"
#include "log.h"
#include "packets.h"
#include "shared.h"
#include "spaceship.h"

#include "civserver.h"
#include "plrhand.h"

#include "spacerace.h"

/* use shuffled order from civserver.c: */
extern struct player *shuffled[MAX_PLAYERS];

/**********************************************************************
Calculate and fill in the derived quantities about the spaceship.
Data reverse engineered from Civ1. --dwp
This could be in common, but its better for the client to take
the values the server calculates, in case things change.
***********************************************************************/
void spaceship_calc_derived(struct player_spaceship *ship)
{
  int i;
  /* these are how many are connected: */
  int fuel=0;
  int propulsion=0;
  int habitation=0;
  int life_support=0;
  int solar_panels=0;

  assert(ship->structurals <= NUM_SS_STRUCTURALS);
  assert(ship->components <= NUM_SS_COMPONENTS);
  assert(ship->modules <= NUM_SS_MODULES);
  
  ship->mass = 0;
  ship->support_rate = ship->energy_rate =
    ship->success_rate = ship->travel_time = 0.0;

  for(i=0; i<NUM_SS_STRUCTURALS; i++) {
    if (ship->structure[i]) {
      ship->mass += (i<6) ? 200 : 100;
      /* s0 to s3 are heavier; actually in Civ1 its a bit stranger
	 than this, but not worth figuring out --dwp */
    }
  }
  for(i=0; i<ship->fuel; i++) {
    if (ship->structure[components_info[i*2].required]) fuel++;
  }
  for(i=0; i<ship->propulsion; i++) {
    if (ship->structure[components_info[i*2+1].required]) propulsion++;
  }
  for(i=0; i<ship->habitation; i++) {
    if (ship->structure[modules_info[i*3].required]) habitation++;
  }
  for(i=0; i<ship->life_support; i++) {
    if (ship->structure[modules_info[i*3+1].required]) life_support++;
  }
  for(i=0; i<ship->solar_panels; i++) {
    if (ship->structure[modules_info[i*3+2].required]) solar_panels++;
  }

  ship->mass += 1600 * (habitation + life_support)
    + 400 * (solar_panels + propulsion + fuel);

  ship->population = habitation * 10000;

  if (habitation) {
    ship->support_rate = life_support / (double) habitation;
  }
  if (life_support+habitation) {
    ship->energy_rate = 2.0 * solar_panels / (double)(life_support+habitation);
  }
  if (fuel>0 && propulsion>0) {
    ship->success_rate = MIN(ship->support_rate,1) * MIN(ship->energy_rate,1);
  }

  /* The Success% can be less by up to a few % in some cases
     (I think if P != F or if P and/or F too small (eg <= 2?) ?)
     but probably not worth worrying about.
     Actually, the Civ1 manual suggests travel time is relevant. --dwp
  */

  ship->travel_time = ship->mass
    / (200.0 * MIN(propulsion,fuel) + 20.0);

}

/**************************************************************************
both src and dest can be NULL
NULL means all players
**************************************************************************/
void send_spaceship_info(struct player *src, struct player *dest)
{
  int o, i, j;
  
  for(o=0; o<game.nplayers; o++) {          /* dests */
    if(!dest || &game.players[o]==dest) {
      for(i=0; i<game.nplayers; i++) {      /* srcs  */
	if(!src || &game.players[i]==src) {
	  struct packet_spaceship_info info;
	  struct player_spaceship *ship = &game.players[i].spaceship;
	  
	  info.player_num = i;
	  info.sship_state = ship->state;
	  info.structurals = ship->structurals;
	  info.components = ship->components;
	  info.modules = ship->modules;
	  info.fuel = ship->fuel;
	  info.propulsion = ship->propulsion;
	  info.habitation = ship->habitation;
	  info.life_support = ship->life_support;
	  info.solar_panels = ship->solar_panels;
	  info.launch_year = ship->launch_year;
	  info.population = ship->population;
	  info.mass = ship->mass;
	  info.support_rate = ship->support_rate;
	  info.energy_rate = ship->energy_rate;
	  info.success_rate = ship->success_rate;
	  info.travel_time = ship->travel_time;
	  
	  for(j=0; j<NUM_SS_STRUCTURALS; j++) {
	    info.structure[j] = ship->structure[j] + '0';
	  }
	  info.structure[j] = '\0';
	  
	  send_packet_spaceship_info(game.players[o].conn, &info);
	}
      }
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_spaceship_launch(struct player *pplayer)
{
  struct player_spaceship *ship = &pplayer->spaceship;
  int arrival;
  
  if (ship->state >= SSHIP_LAUNCHED) {
    notify_player(pplayer, "Game: your spaceship is already launched!");
    return;
  }
  if (ship->state != SSHIP_STARTED
      || ship->success_rate == 0.0) {
    notify_player(pplayer, "Game: your spaceship can't be launched yet!");
    return;
  }
  
  ship->state = SSHIP_LAUNCHED;
  ship->launch_year = game.year;
  arrival = ship->launch_year + (int) ship->travel_time;
  
  notify_player_ex(0, 0, 0, E_SPACESHIP,
		"Game: The %s have launched a spaceship! It is estimated to "
		"arrive on Alpha Centauri in %s.",
		races[pplayer->race].name,
		textyear(arrival));

  send_spaceship_info(pplayer, 0);
}

/**************************************************************************
...
**************************************************************************/
void handle_spaceship_action(struct player *pplayer, 
			     struct packet_spaceship_action *packet)
{
  struct player_spaceship *ship = &pplayer->spaceship;
  int action = packet->action;
  int num = packet->num;
  
  if (ship->state == SSHIP_NONE) {
    notify_player(pplayer, "Game: spaceship action received,"
		  " but you don't have a spaceship!");
    return;
  }
  if (action == SSHIP_ACT_LAUNCH) {
    handle_spaceship_launch(pplayer);
    return;
  }
  if (ship->state >= SSHIP_LAUNCHED) {
    notify_player(pplayer, "Game: you can't modify your"
		  " spaceship after launch!");
    return;
  }
  if (action == SSHIP_ACT_PLACE_STRUCTURAL) {
    if (num<0 || num>=NUM_SS_STRUCTURALS || ship->structure[num]) {
      return;
    }
    if (num_spaceship_structurals_placed(ship) >= ship->structurals) {
      notify_player(pplayer, "Game: you don't have any unplaced"
		    " Space Structurals!");
      return;
    }
    if (num!=0 && !ship->structure[structurals_info[num].required]) {
      notify_player(pplayer, "Game: that Space Structural"
		    " would not be connected!");
      return;
    }
    ship->structure[num] = 1;
    spaceship_calc_derived(ship);
    send_spaceship_info(pplayer, 0);
    return;
  }
  if (action == SSHIP_ACT_PLACE_FUEL) {
    if (ship->fuel != num-1) {
      return;
    }
    if (ship->fuel + ship->propulsion >= ship->components) {
      notify_player(pplayer, "Game: you don't have any unplaced"
		    " Space Components!");
      return;
    }
    if (num > NUM_SS_COMPONENTS/2) {
      notify_player(pplayer, "Game: your spaceship already has"
		    " the maximum number of Fuel Components!");
      return;
    }
    ship->fuel++;
    spaceship_calc_derived(ship);
    send_spaceship_info(pplayer, 0);
    return;
  }
  if (action == SSHIP_ACT_PLACE_PROPULSION) {
    if (ship->propulsion != num-1) {
      return;
    }
    if (ship->fuel + ship->propulsion >= ship->components) {
      notify_player(pplayer, "Game: you don't have any unplaced"
		    " Space Components!");
      return;
    }
    if (num > NUM_SS_COMPONENTS/2) {
      notify_player(pplayer, "Game: your spaceship already has"
		    " the maximum number of Propulsion Components!");
      return;
    }
    ship->propulsion++;
    spaceship_calc_derived(ship);
    send_spaceship_info(pplayer, 0);
    return;
  }
  if (action == SSHIP_ACT_PLACE_HABITATION) {
    if (ship->habitation != num-1) {
      return;
    }
    if (ship->habitation + ship->life_support + ship->solar_panels
	>= ship->modules) {
      notify_player(pplayer, "Game: you don't have any unplaced"
		    " Space Modules!");
      return;
    }
    if (num > NUM_SS_MODULES/3) {
      notify_player(pplayer, "Game: your spaceship already has"
		    " the maximum number of Habitation Modules!");
      return;
    }
    ship->habitation++;
    spaceship_calc_derived(ship);
    send_spaceship_info(pplayer, 0);
    return;
  }
  if (action == SSHIP_ACT_PLACE_LIFE_SUPPORT) {
    if (ship->life_support != num-1) {
      return;
    }
    if (ship->habitation + ship->life_support + ship->solar_panels
	>= ship->modules) {
      notify_player(pplayer, "Game: you don't have any unplaced"
		    " Space Modules!");
      return;
    }
    if (num > NUM_SS_MODULES/3) {
      notify_player(pplayer, "Game: your spaceship already has"
		    " the maximum number of Life Support Modules!");
      return;
    }
    ship->life_support++;
    spaceship_calc_derived(ship);
    send_spaceship_info(pplayer, 0);
    return;
  }
  if (action == SSHIP_ACT_PLACE_SOLAR_PANELS) {
    if (ship->solar_panels != num-1) {
      return;
    }
    if (ship->habitation + ship->life_support + ship->solar_panels
	>= ship->modules) {
      notify_player(pplayer, "Game: you don't have any unplaced"
		    " Space Modules!");
      return;
    }
    if (num > NUM_SS_MODULES/3) {
      notify_player(pplayer, "Game: your spaceship already has"
		    " the maximum number of Solar Panel Modules!");
      return;
    }
    ship->solar_panels++;
    spaceship_calc_derived(ship);
    send_spaceship_info(pplayer, 0);
    return;
  }
  freelog(LOG_NORMAL, "Received unknown spaceship action %d from %s",
       action, pplayer->name);
}

/**************************************************************************
...
**************************************************************************/
void spaceship_lost(struct player *pplayer)
{
  notify_player_ex(0, 0, 0, E_SPACESHIP,
		"Game: With the capture of %s's capital, the %s"
		" spaceship is lost!", pplayer->name,
		get_race_name(pplayer->race));
  spaceship_init(&pplayer->spaceship);
  send_spaceship_info(pplayer, 0);
}

/**************************************************************************
...
Use shuffled order to randomly resolve ties.
**************************************************************************/
void check_spaceship_arrivals(void)
{
  int i;
  double arrival, best_arrival = 0;
  struct player *best_pplayer = NULL;
  struct player *pplayer;
  struct player_spaceship *ship;

  for(i=0; i<game.nplayers; i++) {
    if (!shuffled[i]) continue;

    pplayer = shuffled[i];
    ship = &pplayer->spaceship;
    
    if (ship->state == SSHIP_LAUNCHED) {
      arrival = ship->launch_year + ship->travel_time;
      if (game.year >= arrival
	  && (best_pplayer==NULL || arrival < best_arrival)) {
	best_arrival = arrival;
	best_pplayer = pplayer;
      }
    }
  }
  if (best_pplayer) {
    best_pplayer->spaceship.state = SSHIP_ARRIVED;
    server_state = GAME_OVER_STATE;
    notify_player_ex(0, 0, 0, E_SPACESHIP,
		     "Game: The %s spaceship has arrived at Alpha Centauri.",
		     get_race_name(best_pplayer->race));
  }
}

