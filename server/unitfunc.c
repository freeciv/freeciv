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
#include <stdlib.h>
#include <string.h>

#include "city.h"
#include "events.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "shared.h"
#include "unit.h"

#include "cityhand.h"
#include "citytools.h"
#include "cityturn.h"
#include "civserver.h"
#include "gamelog.h"
#include "gotohand.h"
#include "maphand.h"
#include "plrhand.h"
#include "settlers.h"
#include "unithand.h"
#include "unittools.h"

#include "aiunit.h"
#include "aitools.h"

#include "unitfunc.h"

extern struct move_cost_map warmap;

static int upgrade_would_strand(struct unit *punit, int upgrade_type);

/******************************************************************************
 A spy (but not a diplomat) can poison a cities water supply.  There is 
 no protection from this other than a defending spy/diplomat.

 For now, this wipes the foodstock and one worker.  However, a 
 city of size one will not be destroyed - only the foodstock
 will be effected.

 - Kris Bubendorfer
****************************************************************************/


void spy_poison(struct player *pplayer, struct unit *pdiplomat, struct city *pcity){
  
  struct player *cplayer = city_owner(pcity);
  
  if(unit_flag(pdiplomat->type, F_SPY)){
    if(!diplomat_infiltrate_city(pplayer, cplayer, pdiplomat, pcity))
      return;  /* failed against defending diplomats/spies */
    
     /* Kill off a worker (population unit) */

    if (pcity->size >1) {
      pcity->size--;
      city_auto_remove_worker(pcity);
    } 
    
    notify_player_ex(pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
		     "Game: Your spy poisoned the water supply of %s.", pcity->name);
    
    notify_player_ex(cplayer, pcity->x, pcity->y, E_DIPLOMATED,
		     "Game: %s is suspected of poisoning the water supply of %s.",
		     pplayer->name, pcity->name);
    
    diplomat_leave_city(pplayer, pdiplomat, pcity);
    city_refresh(pcity);  
    send_city_info(0, pcity, 0);

  }
}
/******************************************************************************
  Either a Diplomat or Spy can investigate a city - but for Spies
  it is free.  There is no risk and no loss of movement points.

  As usual, diplomats die afterwards.
									       
 - Kris Bubendorfer
****************************************************************************/

void diplomat_investigate(struct player *pplayer, struct unit *pdiplomat, struct city *pcity)
{
  if (pcity) {
    send_city_info(pplayer, pcity, -1); /* flag value for investigation */
  } /* why isn't the following if in this if?? -- Syela */
  if (!unit_flag(pdiplomat->type, F_SPY))
    wipe_unit(0, pdiplomat);
}

/******************************************************************************
  Sabotage an enemy unit (only spies can do this)
 
  - Can occur to stacked units (takes the top one)
  - Destroys 50% of a units remaining hp (there must be more than 1 hp).
  - Runs the same risk of being captured after action as in a city.
  - Spy is not returned home after the battlefield action.

****************************************************************************/

void spy_sabotage_unit(struct player *pplayer, struct unit *pdiplomat, struct unit *pvictim){
  
  if(pvictim && pvictim->hp > 1){
    pvictim->hp /= 2;
    notify_player_ex(get_player(pvictim->owner), 
    		     pvictim->x, pvictim->y, E_DIPLOMATED,
		     "Your %s was sabotaged by %s!", 
		     unit_name(pvictim->type), pplayer->name);
    send_unit_info(0, pvictim, 0);
  }
  
  /* Now lets see if the spy survives */

  if (myrand(game.diplchance)) {
    
    /* Attacking Spy/Diplomat dies (N-1:N) chance */
    
    notify_player_ex(pplayer, pdiplomat->x, pdiplomat->y, E_NOEVENT,
		     "Game: Your spy was captured after completing her mission.");
    wipe_unit(0, pdiplomat);
  } else {
    
    /* Survived (1:N) chance */
    
    notify_player_ex(pplayer, pdiplomat->x, pdiplomat->y, E_NOEVENT,
		     "Game: Your spy has successfully completed her mission.");
    
  }
}

/******************************************************************************
  bribe an enemy unit
  rules:
  1) can't bribe a unit if owner runs a democracy
  2) if player don't have enough money
  3) can't bribe a unit unless it's the only unit on the square. 
     (this is handled outside this function)
  if these conditions are fulfilled, the unit will be bribed
****************************************************************************/
void diplomat_bribe(struct player *pplayer, struct unit *pdiplomat,
		    struct unit *pvictim)
{
  if(pvictim->bribe_cost == -1) {
    freelog(LOG_NORMAL, "Bribe cost -1 in diplomat_bribe by %s",
	    pplayer->name);
    pvictim->bribe_cost = unit_bribe_cost(pvictim);
  }
  if(pplayer->economic.gold>=pvictim->bribe_cost) {
    if(government_has_flag(get_gov_iplayer(pvictim->owner), G_UNBRIBABLE))
      notify_player_ex(pplayer, pdiplomat->x, pdiplomat->y, E_NOEVENT, 
	"Game: You can't bribe a unit from this nation.");
    else {
      pplayer->economic.gold-=pvictim->bribe_cost;
      notify_player_ex(&game.players[pvictim->owner], 
		    pvictim->x, pvictim->y, E_DIPLOMATED, 
		    "Game: Your %s was bribed by %s.",
		    unit_name(pvictim->type),pplayer->name);
      notify_player_ex(pplayer, pvictim->x, pvictim->y, E_MY_DIPLOMAT,
		    "Game: Your %s succeeded in bribing %s's %s.",
		    unit_name(pdiplomat->type),
		    get_player(pvictim->owner)->name,
		    unit_name(pvictim->type));
      
      create_unit_full(pplayer, pvictim->x, pvictim->y,
		  pvictim->type, pvictim->veteran, pdiplomat->homecity,
		  pvictim->moves_left, pvictim->hp);
      light_square(pplayer, pvictim->x, pvictim->y,
                   get_unit_type(pvictim->type)->vision_range);
      wipe_unit(0, pvictim);
      pdiplomat->moves_left=0;
      send_unit_info(pplayer, pdiplomat, 0);
      send_player_info(pplayer, pplayer);
    }
  }
}

/****************************************************************************
  diplomat try to steal a tech from an enemy city
  rules:
  1) if there is a spy in the city the attempt will fail.
  2) if there has already been stolen from this city the attempt will fail

  That is, you can only steal tech from a city once!  Ever.

****************************************************************************/
void diplomat_get_tech(struct player *pplayer, struct unit *pdiplomat, 
		       struct city  *pcity, int tech)
{
  int tec;
  int i;
  int j=0;
  struct player *target=&game.players[pcity->owner];
  struct player *cplayer = city_owner(pcity);

  if (pplayer==target)
    return;
  
  /* Check if the Diplomat/Spy succeeds against defending Diplomats or Spies */
  /* - Kris Bubendorfer <Kris.Bubendorfer@MCS.VUW.AC.NZ>                     */
  
  if(!diplomat_infiltrate_city(pplayer, cplayer, pdiplomat, pcity))
    return;  /* failed against defending diplomats/spies */
  
  /* If tech is specified, then a spy chose the tech to steal.  Diplomats can't do this */
  
  if(!unit_flag(pdiplomat->type, F_SPY) || !tech){  
    for (i=1;i<A_LAST;i++) {
      if (get_invention(pplayer, i)!=TECH_KNOWN && get_invention(target, i)== TECH_KNOWN) {
 	j++;
      }
    }
    if (!j) {
      if (target->future_tech > pplayer->future_tech) {
 	notify_player_ex(pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
 			 "Game: Your diplomat stole Future Tech. %d from %s",
 			 ++(pplayer->future_tech), target->name);
 	notify_player_ex(target, pcity->x, pcity->y, E_DIPLOMATED,
 			 "Game: A%s %s diplomat stole Future Tech. %d from %s.", 
			 n_if_vowel(get_race_name(pplayer->race)[0]),
			 get_race_name(pplayer->race), pplayer->future_tech, 
			 pcity->name);
 	return;
      } else {
 	notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT,
 			 "Game: No new technology found in %s", pcity->name);
 	return;
      }
    }
    
    if (pcity->steal) {
      notify_player_ex(pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
 		       "Game: Your diplomat was caught in the attempt of stealing technology from %s.", pcity->name);
      notify_player_ex(target, pcity->x, pcity->y, E_DIPLOMATED,
 		       "Game: %s's diplomat failed to steal technology from %s",
		       pplayer->name, pcity->name);
      wipe_unit(0,pdiplomat);
      return;
    }
    
    j=myrand(j)+1;
    for (i=1;i<A_LAST;i++) {
      if (get_invention(pplayer, i)!=TECH_KNOWN && 
 	  get_invention(target, i)== TECH_KNOWN) 
 	j--;
      if (!j) break;
    }
    if (i==A_LAST) {
      freelog(LOG_NORMAL, "Bug in diplomat_a_tech");
      return;
    }
  }else{
    i = tech;
    
    if (pcity->steal) {
      notify_player_ex(pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
  		       "Game: Your spy was caught in the attempt of stealing technology from %s.", pcity->name);
      notify_player_ex(target, pcity->x, pcity->y, E_DIPLOMATED,
 		       "Game: %s's spy was caught stealing technology from %s", 
		       pplayer->name, pcity->name);
      wipe_unit(0,pdiplomat);
      return;
    }
  }
    
  pcity->steal=1;
  notify_player_ex(pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
		   "Game: Your %s stole %s from %s",
		   unit_name(pdiplomat->type),
		   advances[i].name, target->name); 
  notify_player_ex(target, pcity->x, pcity->y, E_DIPLOMATED,
		   "Game: %s's %s stole %s from %s.", 
		   pplayer->name, unit_name(pdiplomat->type),
		   advances[i].name, pcity->name); 
  if (i==game.rtech.construct_rail) {
    upgrade_city_rails(pplayer, 0);
  }
  gamelog(GAMELOG_TECH,"%s steals %s from the %s",
          get_race_name_plural(pplayer->race),
          advances[i].name,
          get_race_name_plural(target->race));

  set_invention(pplayer, i, TECH_KNOWN);
  update_research(pplayer);
  do_conquer_cost(pplayer);
  pplayer->research.researchpoints++;
  if (pplayer->research.researching==i) {
    tec=pplayer->research.researched;
    if (!choose_goal_tech(pplayer))
      choose_random_tech(pplayer);
    pplayer->research.researched=tec;
  }
  
  /* Check if a spy survives her mission                 */
  /* - Kris Bubendorfer <Kris.Bubendorfer@MCS.VUW.AC.NZ> */
  
  diplomat_leave_city(pplayer, pdiplomat, pcity);
}


/**************************************************************************
diplomat_infiltrate_city

This code determines if a subverting diplomat/spy succeeds in infiltrating
the city, that is - if there are defending diplomats/spies they have a 
n-1/n chance of defeating the infiltrator.
**************************************************************************/

int diplomat_infiltrate_city(struct player *pplayer, struct player *cplayer,
			     struct unit *pdiplomat, struct city *pcity)
{
  /* For EVERY diplomat/spy on a square, there is a 1/N chance of succeeding.
   * This needs to be changed to take into account veteran status.
   */
  unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit)
    if (unit_flag(punit->type, F_DIPLOMAT)) {
      if (myrand(game.diplchance)) {
	
	/* Attacking Spy/Diplomat dies (N-1:N) */
	
	notify_player_ex(pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
			 "Game: Your %s was eliminated"
			 " by a defending %s in %s.", 
			 unit_name(pdiplomat->type),
			 unit_name(punit->type),
			 pcity->name);
	notify_player_ex(cplayer, pcity->x, pcity->y, E_DIPLOMATED,
			 "Game: A%s %s %s was eliminated"
			 " while infiltrating %s.", 
			 n_if_vowel(get_race_name(pplayer->race)[0]), 
			 get_race_name(pplayer->race),
			 unit_name(pdiplomat->type),
			 pcity->name);
	wipe_unit(0, pdiplomat);
	return 0;
      } else {
	
	/* Defending Spy/Diplomat dies (1:N) */
	
	notify_player_ex(cplayer, pcity->x, pcity->y, E_DIPLOMATED,
			 "Game: Your %s has been eliminated defending"
			 " against a %s in %s.",
			 unit_name(punit->type),
			 unit_name(pdiplomat->type),
			 pcity->name);
	wipe_unit(0, punit);
      }
    }
  unit_list_iterate_end; 
  return 1;
}

/**************************************************************************
diplomat_leave_city

This code determines if a subverting diplomat/spy survives infiltrating a 
city.  A diplomats always dies, a spy has a 1/game.diplchance
**************************************************************************/

void diplomat_leave_city(struct player *pplayer, struct unit *pdiplomat,
			 struct city *pcity)
{
  if (unit_flag(pdiplomat->type, F_SPY)) {
    if (myrand(game.diplchance)) {
      
      /* Attacking Spy/Diplomat dies (N-1:N) chance */
      
      notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT, 
		     "Game: Your spy was captured after completing her mission in %s.", pcity->name);
    } else {

      /* Survived (1:N) chance */
      
      struct city *spyhome = find_city_by_id(pdiplomat->homecity);

      if(!spyhome){
	freelog(LOG_NORMAL, "Bug in diplomat_leave_city");
	return;
      }
      
      notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT, 
		       "Game: Your spy has successfully completed her mission and returned unharmed to %s.", spyhome->name);
      
       /* being teleported costs 1 move */
      
      maybe_make_veteran(pdiplomat);
      
      teleport_unit_to_city(pdiplomat, spyhome, 3);
      
      return;
    }
  }
  wipe_unit(0, pdiplomat);
}

/**************************************************************************
 Inciting a revolution will fail if
 1) the attacked player is running a democracy
 2) the attacker don't have enough credits
 3) 1/n probabilty of elminating a defending spy 
**************************************************************************/
void diplomat_incite(struct player *pplayer, struct unit *pdiplomat,
		     struct city *pcity)
{
  struct player *cplayer;
  struct city *pnewcity;
  int revolt_cost;

  if (!pcity)
    return;
 
  cplayer=city_owner(pcity);
  if (cplayer==pplayer || cplayer==NULL) 
    return;
  if(government_has_flag(get_gov_pplayer(cplayer), G_UNBRIBABLE)) {
      notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT, 
		       "Game: You can't subvert a city from this nation.");
      return;
  }
  
  if(pcity->incite_revolt_cost == -1) {
    freelog(LOG_NORMAL, "Incite cost -1 in diplomat_incite by %s for %s",
	    pplayer->name, pcity->name);
    city_incite_cost(pcity);
  }
  revolt_cost = pcity->incite_revolt_cost;
  if(pplayer->player_no == pcity->original) revolt_cost/=2;
  
  if (pplayer->economic.gold < revolt_cost) { 
    notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT, 
		     "Game: You don't have enough credits to subvert %s.", pcity->name);
    
    return;
  }

  /* Check if the Diplomat/Spy succeeds against defending Diplomats or Spies */

  if (!diplomat_infiltrate_city(pplayer, cplayer, pdiplomat, pcity))
    return;  /* failed against defending diplomats/spies */

  pplayer->economic.gold -= revolt_cost;
  if (pcity->size >1) {
    pcity->size--;
    city_auto_remove_worker(pcity);
  }
  notify_player_ex(pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
		   "Game: Revolt incited in %s, you now rule the city!", 
		   pcity->name);
  notify_player_ex(cplayer, pcity->x, pcity->y, E_DIPLOMATED, 
		   "Game: %s has revolted, %s influence suspected", pcity->name, 
		   get_race_name(pplayer->race));

  /* Transfer city and units supported by this city to the new owner */

  pnewcity = transfer_city(pplayer,cplayer,pcity);
  pnewcity->shield_stock=0; /* this is not done automatically */ 
  transfer_city_units(pplayer, cplayer, pnewcity, pcity, 0, 1);
  remove_city(pcity);  /* don't forget this! */

  /* Resolve Stack conflicts */

  unit_list_iterate(pplayer->units, punit) 
    resolve_unit_stack(punit->x, punit->y, 1);
  unit_list_iterate_end;
  
  /* buying a city should result in getting new tech from the victim too */
  /* but no money!                                                       */

  get_a_tech(pplayer, cplayer);
   
  map_set_city(pnewcity->x, pnewcity->y, pnewcity);
  if (terrain_control.may_road &&
      (get_invention(pplayer, game.rtech.construct_rail)==TECH_KNOWN) &&
      (get_invention(cplayer, game.rtech.construct_rail)!=TECH_KNOWN) &&
      (!(map_get_special(pnewcity->x, pnewcity->y)&S_RAILROAD))) {
    notify_player(pplayer, "Game: The people in %s are stunned by your technological insight!\n      Workers spontaneously gather and upgrade the city with railroads.",pnewcity->name);
    map_set_special(pnewcity->x, pnewcity->y, S_RAILROAD);
    send_tile_info(0, pnewcity->x, pnewcity->y, TILE_KNOWN);
  }

  reestablish_city_trade_routes(pnewcity); 

  city_check_workers(pplayer,pnewcity);
  update_map_with_city_workers(pnewcity);
  city_refresh(pnewcity);
  initialize_infrastructure_cache(pnewcity);
  send_city_info(0, pnewcity, 0);
  send_player_info(pplayer, pplayer);

  /* Check if a spy survives her mission */
  diplomat_leave_city(pplayer, pdiplomat, pnewcity);
}  

/**************************************************************************
Sabotage of enemy building, build order will fail if:
 1) there is a defending spy in the city.
 2) there is a chance it'll fail if the sabotage is a building
 The sabotage will be 50/50 between wiping the current production or
 an existing building.
 NOTE: Wonders and palace can't be sabotaged
**************************************************************************/
void diplomat_sabotage(struct player *pplayer, struct unit *pdiplomat, struct city *pcity, int improvement)
{
  struct player *cplayer;
  char *prod;
  struct city *capital;

  if (!pcity)
    return;

  cplayer=city_owner(pcity);
  if (cplayer==pplayer ||cplayer==NULL) return;

  /* Check if the Diplomat/Spy succeeds against defending Diplomats or Spies */

  if (!diplomat_infiltrate_city(pplayer, cplayer, pdiplomat, pcity))
    return;  /* failed against defending diplomats/spies */


  if(!unit_flag(pdiplomat->type, F_SPY)) {

    /* If advance to steal is not specified for a spy or unit is a diplomat*/

    switch (myrand(2)) {
    case 0:
      pcity->shield_stock=0;
      if (pcity->is_building_unit) 
	prod=unit_name(pcity->currently_building);
      else
	prod=get_improvement_name(pcity->currently_building);
      notify_player_ex(pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
		       "Game: Your diplomat succeeded in destroying the production of %s in %s", 
		       prod, pcity->name);
      notify_player_ex(cplayer, pcity->x, pcity->y, E_DIPLOMATED, 
		       "Game: The production of %s was destroyed in %s, %s are suspected.", 
		       prod, pcity->name, get_race_name_plural(pplayer->race));
      
      break;
    case 1:
      {
	int building;
	int i;
	for (i=0;i<10;i++) {
	  building=myrand(B_LAST);
	  if (city_got_building(pcity, building) 
	      && !is_wonder(building) && building!=B_PALACE) {
	    pcity->improvements[building]=0;
	    break;
	  }
	}
	if (i<10) {
	  notify_player_ex(pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
			   "Game: Your diplomat destroyed the %s in %s.", 
			   get_improvement_name(building), pcity->name);
	  notify_player_ex(cplayer, pcity->x, pcity->y, E_DIPLOMATED,
			   "Game: The %s destroyed the %s in %s.", 
			   get_race_name_plural(pplayer->race),
			   get_improvement_name(building), pcity->name);
	} else {
	  notify_player_ex(pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
			   "Game: Your Diplomat was caught in the attempt of industrial sabotage!");
	  notify_player_ex(cplayer, pcity->x, pcity->y, E_DIPLOMATED,
			   "Game: You caught a%s %s diplomat attempting sabotage in %s!",
			   n_if_vowel(get_race_name(pplayer->race)[0]),
			   get_race_name(pplayer->race), pcity->name);
	  
	  wipe_unit(0, pdiplomat);
	  return;
	}
      }
      break;
    }
  }else{

    /*
     *check we're not trying to sabotage city walls or inside the capital 
     * If we are, then there is a 50% chance of failing.  Also, wonders of
     * the world cannot be sabotaged, and neither can the Palace.
     */

    improvement--;

    if (is_wonder(improvement) || improvement == B_PALACE) {
	notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT,
		 "Game: You cannot sabotage a wonder or a Palace!");
	return;
    }

    capital=find_palace(city_owner(pcity));

    if(pcity == capital || improvement==B_CITY){
      if(myrand(2)){

	/* Lost */
	
	notify_player_ex(pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
			 "Game: Your spy was caught in the attempt of sabotage!");
	notify_player_ex(cplayer, pcity->x, pcity->y, E_DIPLOMATED,
			 "Game: You caught a%s %s spy attempting to sabotage the %s in %s!",
			 n_if_vowel(get_race_name(pplayer->race)[0]),
			 get_race_name(pplayer->race), 
			 get_improvement_name(improvement), pcity->name);
	
	wipe_unit(0, pdiplomat);
	return;
      }
    }

    /* OK, sabotage */

    if(improvement == -1){
      /* Destroy Production */
 
      pcity->shield_stock=0;
      if (pcity->is_building_unit) 
	prod=unit_name(pcity->currently_building);
      else
	prod=get_improvement_name(pcity->currently_building);
      notify_player_ex(pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
		       "Game: Your spy succeeded in destroying the production of %s in %s", 
		       prod, pcity->name);
      notify_player_ex(cplayer, pcity->x, pcity->y, E_DIPLOMATED, 
		       "Game: The production of %s was destroyed in %s, %s are suspected.", 
		       prod, pcity->name, get_race_name_plural(pplayer->race));
            
    }else{
      pcity->improvements[improvement]=0;
      
      notify_player_ex(pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
		       "Game: Your spy destroyed %s in %s.", 
		       get_improvement_name(improvement), pcity->name);
      notify_player_ex(cplayer, pcity->x, pcity->y, E_DIPLOMATED,
		       "Game: The %s destroyed %s in %s.", 
		       get_race_name_plural(pplayer->race),
		       get_improvement_name(improvement), pcity->name);
      
    }
  }

  send_city_info(0, pcity, 0);
  
  /* Check if a spy survives her mission */
  
  diplomat_leave_city(pplayer, pdiplomat, pcity);
}


/***************************************************************************
 Return 1 if upgrading this unit could cause passengers to
 get stranded at sea, due to transport capacity changes
 caused by the upgrade.
 Return 0 if ok to upgrade (as far as stranding goes).
***************************************************************************/
static int upgrade_would_strand(struct unit *punit, int upgrade_type)
{
  int old_cap, new_cap, tile_cap, tile_ncargo;
  
  if (!is_sailing_unit(punit))
    return 0;
  if (map_get_terrain(punit->x, punit->y) != T_OCEAN)
    return 0;

  /* With weird non-standard unit types, upgrading these could
     cause air units to run out of fuel; too bad. */
  if (unit_flag(punit->type, F_CARRIER) || unit_flag(punit->type,F_SUBMARINE))
    return 0;

  old_cap = get_transporter_capacity(punit);
  new_cap = unit_types[upgrade_type].transport_capacity;

  if (new_cap >= old_cap)
    return 0;

  tile_cap = 0;
  tile_ncargo = 0;
  unit_list_iterate(map_get_tile(punit->x, punit->y)->units, punit2) {
    if (is_sailing_unit(punit2) && is_ground_units_transport(punit2)) { 
      tile_cap += get_transporter_capacity(punit2);
    } else if (is_ground_unit(punit2)) {
      tile_ncargo++;
    }
  }
  unit_list_iterate_end;

  if (tile_ncargo <= tile_cap - old_cap + new_cap)
    return 0;

  freelog(LOG_VERBOSE, "Can't upgrade %s at (%d,%d)"
	  " because would strand passenger(s)",
	  get_unit_type(punit->type)->name, punit->x, punit->y);
  return 1;
}

/***************************************************************************
Restore unit move points and hitpoints.
Do Leonardo's Workshop upgrade if applicable.
Now be careful not to strand units at sea with the Workshop. --dwp
Adjust air units for fuel: air units lose fuel unless in a city
or on a Carrier (or, for Missles, on a Submarine).  Air units
which run out of fuel get wiped.
Carriers and Submarines can now only supply fuel to a limited
number of units each, equal to their transport_capacity. --dwp
(Hitpoint adjustments include Helicopters out of cities, but
that is handled within unit_restore_hitpoints().)
Triremes will be wiped with 50% chance if they're not close to
land, and player doesn't have Lighthouse.
****************************************************************************/
void player_restore_units(struct player *pplayer)
{
  int leonardo, leonardo_variant;
  int lighthouse_effect=-1;	/* 1=yes, 0=no, -1=not yet calculated */
  int upgrade_type, done;

  leonardo = player_owns_active_wonder(pplayer, B_LEONARDO);
  leonardo_variant = improvement_variant(B_LEONARDO);

  /* get Leonardo out of the way first: */
  if (leonardo) {
    unit_list_iterate(pplayer->units, punit) {
      if (leonardo &&
	  (upgrade_type=can_upgrade_unittype(pplayer, punit->type))!=-1
	  && !upgrade_would_strand(punit, upgrade_type)) {
	if (punit->hp==get_unit_type(punit->type)->hp) 
	  punit->hp = get_unit_type(upgrade_type)->hp;
	notify_player(pplayer,
		      "Game: Leonardo's workshop has upgraded %s to %s%s",
		      get_unit_type(punit->type)->name,
		      get_unit_type(upgrade_type)->name,
		      get_location_str_in(pplayer, punit->x, punit->y, ", "));
	punit->type = upgrade_type;
	punit->veteran = 0;
	leonardo = leonardo_variant;
      }
    }
    unit_list_iterate_end;
  }
  
  /* Temporarily use 'fuel' on Carriers and Subs to keep track
     of numbers of supported Air Units:   --dwp */
  unit_list_iterate(pplayer->units, punit) {
    if (unit_flag(punit->type, F_CARRIER) || 
	unit_flag(punit->type, F_SUBMARINE)) {
      punit->fuel = get_unit_type(punit->type)->transport_capacity;
    }
  }
  unit_list_iterate_end;
  
  unit_list_iterate(pplayer->units, punit) {
    unit_restore_hitpoints(pplayer, punit);
    unit_restore_movepoints(pplayer, punit);

    if(punit->hp<=0) {
      /* This should usually only happen for heli units,
	 but if any other units get 0 hp somehow, catch
	 them too.  --dwp  */
      send_remove_unit(0, punit->id);
      notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST, 
		       "Game: Your %s has run out of hit points",
		       unit_name(punit->type));
      gamelog(GAMELOG_UNITF, "%s lose a %s (out of hp)", 
	      get_race_name_plural(pplayer->race),
	      unit_name(punit->type));
      wipe_unit(0, punit);
    }
    else if(is_air_unit(punit)) {
      punit->fuel--;
      if(map_get_city(punit->x, punit->y))
	punit->fuel=get_unit_type(punit->type)->fuel;
      else {
	done = 0;
	if (unit_flag(punit->type, F_MISSILE)) {
	  /* Want to preferentially refuel Missiles from Subs,
	     to leave space on co-located Carriers for normal air units */
	  unit_list_iterate(map_get_tile(punit->x, punit->y)->units, punit2) 
	    if (!done && unit_flag(punit2->type,F_SUBMARINE) && punit2->fuel) {
	      punit->fuel = get_unit_type(punit->type)->fuel;
	      punit2->fuel--;
	      done = 1;
	    }
	  unit_list_iterate_end;
	}
	if (!done) {
	  /* Non-Missile or not refueled by Subs: use Carriers: */
	  unit_list_iterate(map_get_tile(punit->x, punit->y)->units, punit2) 
	    if (!done && unit_flag(punit2->type,F_CARRIER) && punit2->fuel) {
	      punit->fuel = get_unit_type(punit->type)->fuel;
	      punit2->fuel--;
	      done = 1;
	    }
	  unit_list_iterate_end;
	}
      }
      if(punit->fuel<=0) {
	send_remove_unit(0, punit->id);
	notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST, 
			 "Game: Your %s has run out of fuel",
			 unit_name(punit->type));
	gamelog(GAMELOG_UNITF, "%s lose a %s (fuel)", 
		get_race_name_plural(pplayer->race),
		unit_name(punit->type));
	wipe_unit(0, punit);
      }
    } else if (unit_flag(punit->type, F_TRIREME) && (lighthouse_effect!=1) &&
	       !is_coastline(punit->x, punit->y)) {
      if (lighthouse_effect == -1) {
	lighthouse_effect = player_owns_active_wonder(pplayer, B_LIGHTHOUSE);
      }
      if ((!lighthouse_effect) && (myrand(100) >= 50)) {
	notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST, 
			 "Game: Your Trireme has been lost on the high seas");
	gamelog(GAMELOG_UNITTRI, "%s Trireme lost at sea",
		get_race_name_plural(pplayer->race));
	wipe_unit_safe(pplayer, punit, &myiter);
      }
    }
  }
  unit_list_iterate_end;
  
  /* Clean up temporary use of 'fuel' on Carriers and Subs: */
  unit_list_iterate(pplayer->units, punit) {
    if (unit_flag(punit->type, F_CARRIER) || 
	unit_flag(punit->type, F_SUBMARINE)) {
      punit->fuel = 0;
    }
  }
  unit_list_iterate_end;
}

/****************************************************************************
  add hitpoints to the unit, hp_gain_coord returns the amount to add
  united nations will speed up the process by 2 hp's / turn, means helicopters
  will actually not loose hp's every turn if player have that wonder.
  Units which have moved don't gain hp, except the United Nations and
  helicopter effects still occur.
*****************************************************************************/
void unit_restore_hitpoints(struct player *pplayer, struct unit *punit)
{
  struct city *pcity = map_get_city(punit->x,punit->y);
  int was_lower;

  was_lower=(punit->hp < get_unit_type(punit->type)->hp);

  if(!punit->moved) {
    punit->hp+=hp_gain_coord(punit);
  }
  
  if (player_owns_active_wonder(pplayer, B_UNITED)) 
    punit->hp+=2;
    
  if(!pcity && (is_heli_unit(punit)))
    punit->hp-=get_unit_type(punit->type)->hp/10;

  if(punit->hp>=get_unit_type(punit->type)->hp) {
    punit->hp=get_unit_type(punit->type)->hp;
    if(was_lower&&punit->activity==ACTIVITY_SENTRY)
      set_unit_activity(punit,ACTIVITY_IDLE);
  }
  if(punit->hp<0)
    punit->hp=0;

  punit->moved=0;
}
  

/***************************************************************************
 move points are trivial, only modifiers to the base value is if it's
  sea units and the player has certain wonders/techs
***************************************************************************/
void unit_restore_movepoints(struct player *pplayer, struct unit *punit)
{
  punit->moves_left=unit_move_rate(punit);
}

/**************************************************************************
  after a battle this routine is called to decide whether or not the unit
  should become a veteran, if unit isn't already.
  there is a 50/50% chance for it to happend, (100% if player got SUNTZU)
**************************************************************************/
void maybe_make_veteran(struct unit *punit)
{
    if (punit->veteran) 
      return;
    if(player_owns_active_wonder(get_player(punit->owner), B_SUNTZU)) 
      punit->veteran = 1;
    else
      punit->veteran=myrand(2);
}

/**************************************************************************
  This is the basic unit versus unit combat routine.
  1) ALOT of modifiers bonuses etc is added to the 2 units rates.
  2) the combat loop, which continues until one of the units are dead
  3) the aftermath, the looser (and potentially the stack which is below it)
     is wiped, and the winner gets a chance of gaining veteran status
**************************************************************************/
void unit_versus_unit(struct unit *attacker, struct unit *defender)
{
  int attackpower=get_total_attack_power(attacker,defender);
  int defensepower=get_total_defense_power(attacker,defender);

  freelog(LOG_VERBOSE, "attack:%d, defense:%d", attackpower, defensepower);
  if (!attackpower) {
      attacker->hp=0; 
  } else if (!defensepower) {
      defender->hp=0;
  }
  while (attacker->hp>0 && defender->hp>0) {
    if (myrand(attackpower+defensepower)>= defensepower) {
      defender->hp -= get_unit_type(attacker->type)->firepower
	* game.firepower_factor;
    } else {
      if (is_sailing_unit(defender) && map_get_city(defender->x, defender->y))
	attacker->hp -= game.firepower_factor;      /* pearl harbour */
      else
	attacker->hp -= get_unit_type(defender->type)->firepower
	  * game.firepower_factor;
    }
  }
  if (attacker->hp<0) attacker->hp=0;
  if (defender->hp<0) defender->hp=0;

  if (attacker->hp)
    maybe_make_veteran(attacker); 
  else if (defender->hp)
    maybe_make_veteran(defender);
}

/***************************************************************************
 return the modified attack power of a unit.  Currently they aren't any
 modifications...
***************************************************************************/
int get_total_attack_power(struct unit *attacker, struct unit *defender)
{
  int attackpower=get_attack_power(attacker);

  return attackpower;
}

/***************************************************************************
 Like get_virtual_defense_power, but don't include most of the modifications.
 (For calls which used to be g_v_d_p(U_HOWITZER,...))
 Specifically, include:
 unit def, terrain effect, fortress effect, ground unit in city effect
***************************************************************************/
int get_simple_defense_power(int d_type, int x, int y)
{
  int defensepower=unit_types[d_type].defense_strength;
  struct city *pcity = map_get_city(x, y);
  enum tile_terrain_type t = map_get_terrain(x, y);
  int db;

  if (unit_types[d_type].move_type == LAND_MOVING && t == T_OCEAN) return 0;
/* I had this dorky bug where transports with mech inf aboard would go next
to enemy ships thinking the mech inf would defend them adequately. -- Syela */

  db = get_tile_type(t)->defense_bonus;
  if (map_get_special(x, y) & S_RIVER)
    db += (db * terrain_control.river_defense_bonus) / 100;
  defensepower *= db;

  if (map_get_special(x, y)&S_FORTRESS && !pcity)
    defensepower+=(defensepower*terrain_control.fortress_defense_bonus)/100;
  if (pcity && unit_types[d_type].move_type == LAND_MOVING)
    defensepower*=1.5;

  return defensepower;
}

int get_virtual_defense_power(int a_type, int d_type, int x, int y)
{
  int defensepower=unit_types[d_type].defense_strength;
  int m_type = unit_types[a_type].move_type;
  struct city *pcity = map_get_city(x, y);
  enum tile_terrain_type t = map_get_terrain(x, y);
  int db;

  if (unit_types[d_type].move_type == LAND_MOVING && t == T_OCEAN) return 0;
/* I had this dorky bug where transports with mech inf aboard would go next
to enemy ships thinking the mech inf would defend them adequately. -- Syela */

  db = get_tile_type(t)->defense_bonus;
  if (map_get_special(x, y) & S_RIVER)
    db += (db * terrain_control.river_defense_bonus) / 100;
  defensepower *= db;

  if (unit_flag(d_type, F_PIKEMEN) && unit_flag(a_type, F_HORSE)) 
    defensepower*=2;
  if (unit_flag(d_type, F_AEGIS) &&
       (m_type == AIR_MOVING || m_type == HELI_MOVING)) defensepower*=5;
  if (m_type == AIR_MOVING && pcity) {
    if (city_got_building(pcity, B_SAM))
      defensepower*=2;
    if (city_got_building(pcity, B_SDI) && unit_flag(a_type, F_MISSILE))
      defensepower*=2;
  } else if (m_type == SEA_MOVING && pcity) {
    if (city_got_building(pcity, B_COASTAL))
      defensepower*=2;
  }
  if (!unit_flag(a_type, F_IGWALL)
      && (m_type == LAND_MOVING || m_type == HELI_MOVING
	  || (improvement_variant(B_CITY)==1 && m_type == SEA_MOVING))
      && pcity && city_got_citywalls(pcity)) {
    defensepower*=3;
  }
  if (map_get_special(x, y)&S_FORTRESS && !pcity)
    defensepower+=(defensepower*terrain_control.fortress_defense_bonus)/100;
  if (pcity && unit_types[d_type].move_type == LAND_MOVING)
    defensepower*=1.5;

  return defensepower;
}

/***************************************************************************
 return the modified defense power of a unit.
 An veteran aegis cruiser in a mountain city with SAM and SDI defense 
 being attacked by a missile gets defense 288.
***************************************************************************/
int get_total_defense_power(struct unit *attacker, struct unit *defender)
{
  int defensepower=get_defense_power(defender);
  if (unit_flag(defender->type, F_PIKEMEN) && unit_flag(attacker->type, F_HORSE)) 
    defensepower*=2;
  if (unit_flag(defender->type, F_AEGIS) && (is_air_unit(attacker) || is_heli_unit(attacker)))
    defensepower*=5;
  if (is_air_unit(attacker)) {
    if (unit_behind_sam(defender))
      defensepower*=2;
    if (unit_behind_sdi(defender) && unit_flag(attacker->type, F_MISSILE))
      defensepower*=2;
  } else if (is_sailing_unit(attacker)) {
    if (unit_behind_coastal(defender))
      defensepower*=2;
  }
  if (!unit_really_ignores_citywalls(attacker)
      && unit_behind_walls(defender)) 
    defensepower*=3;
  if (unit_on_fortress(defender) && 
      !map_get_city(defender->x, defender->y)) 
    defensepower*=2;
  if ((defender->activity == ACTIVITY_FORTIFY || 
       map_get_city(defender->x, defender->y)) && 
      is_ground_unit(defender))
    defensepower*=1.5;

  return defensepower;
}

/**************************************************************************
 creates a unit, and set it's initial values, and put it into the right 
 lists.
 TODO: Maybe this procedure should refresh its homecity? so it'll show up 
 immediately on the clients? (refresh_city + send_city_info)
**************************************************************************/

/* This is a wrapper */

void create_unit(struct player *pplayer, int x, int y, Unit_Type_id type, int make_veteran, int homecity_id, int moves_left){
  create_unit_full(pplayer,x,y,type,make_veteran,homecity_id,moves_left,-1);
}

/* This is the full call.  We don't want to have to change all other calls to
   this function to ensure the hp are set */

void create_unit_full(struct player *pplayer, int x, int y, Unit_Type_id type, int make_veteran, int homecity_id, int moves_left, int hp_left)
{
  struct unit *punit;
  struct city *pcity;
  punit=fc_calloc(1,sizeof(struct unit));
  punit->type=type;
  punit->id=get_next_id_number();
  punit->owner=pplayer->player_no;
  punit->x = map_adjust_x(x); /* was = x, caused segfaults -- Syela */
  punit->y=y;
  if (y < 0 || y >= map.ysize) {
    freelog(LOG_NORMAL, "Whoa!  Creating %s at illegal loc (%d, %d)",
	    get_unit_type(type)->name, x, y);
  }
  punit->goto_dest_x=0;
  punit->goto_dest_y=0;
  
  pcity=find_city_by_id(homecity_id);
  punit->veteran=make_veteran;
  punit->homecity=homecity_id;

  if(hp_left == -1)
    punit->hp=get_unit_type(punit->type)->hp;
  else
    punit->hp = hp_left;
  set_unit_activity(punit, ACTIVITY_IDLE);

  punit->upkeep=0;
  punit->upkeep_food=0;
  punit->upkeep_gold=0;
  punit->unhappiness=0;

  /* 
     See if this is a spy that has been moved (corrupt and therefore unable 
     to establish an embassy.
  */
  if(moves_left != -1 && unit_flag(punit->type, F_SPY))
    punit->foul=1;
  else
    punit->foul=0;
  
  punit->fuel=get_unit_type(punit->type)->fuel;
  punit->ai.control=0;
  punit->ai.ai_role = AIUNIT_NONE;
  punit->ai.ferryboat = 0;
  punit->ai.passenger = 0;
  punit->ai.bodyguard = 0;
  punit->ai.charge = 0;
  unit_list_insert(&pplayer->units, punit);
  unit_list_insert(&map_get_tile(x, y)->units, punit);
  if (pcity)
    unit_list_insert(&pcity->units_supported, punit);
  punit->bribe_cost=-1;		/* flag value */
  if(moves_left < 0)  
    punit->moves_left=unit_move_rate(punit);
  else
    punit->moves_left=moves_left;

  /* Assume that if moves_left<0 then the unit is "fresh",
     and not moved; else the unit has had something happen
     to it (eg, bribed) which we treat as equivalent to moved.
     (Otherwise could pass moved arg too...)  --dwp
  */
  punit->moved = (moves_left>=0);
  
  send_unit_info(0, punit, 0);
}


/**************************************************************************
  Removes the unit from the game, and notifies the clients.
  TODO: Find out if the homecity is refreshed and resend when this happends
  otherwise (refresh_city(homecity) + send_city(homecity))
**************************************************************************/
void send_remove_unit(struct player *pplayer, int unit_id)
{
  int o;
  
  struct packet_generic_integer packet;

  packet.value=unit_id;

  for(o=0; o<game.nplayers; o++)           /* dests */
    if(!pplayer || &game.players[o]==pplayer)
      send_packet_generic_integer(game.players[o].conn, PACKET_REMOVE_UNIT,
				  &packet);
}

/**************************************************************************
  iterate through all units and update them.
**************************************************************************/
void update_unit_activities(struct player *pplayer)
{
  unit_list_iterate(pplayer->units, punit)
    update_unit_activity(pplayer, punit);
  unit_list_iterate_end;
}

/**************************************************************************
  Calculate the total amount of activity performed by all units on a tile
  for a given task.
**************************************************************************/
static int total_activity (int x, int y, enum unit_activity act)
{
  struct tile *ptile;
  int total = 0;

  ptile = map_get_tile (x, y);
  unit_list_iterate (ptile->units, punit)
    if (punit->activity == act)
      total += punit->activity_count;
  unit_list_iterate_end;
  return total;
}

/**************************************************************************
  Calculate the total amount of activity performed by all units on a tile
  for a given task and target.
**************************************************************************/
static int total_activity_targeted (int x, int y,
				    enum unit_activity act,
				    int tgt)
{
  struct tile *ptile;
  int total = 0;

  ptile = map_get_tile (x, y);
  unit_list_iterate (ptile->units, punit)
    if ((punit->activity == act) && (punit->activity_target == tgt))
      total += punit->activity_count;
  unit_list_iterate_end;
  return total;
}

/**************************************************************************
  progress settlers in their current tasks, 
  and units that is pillaging.
  also move units that is on a goto.
**************************************************************************/
void update_unit_activity(struct player *pplayer, struct unit *punit)
{
  int id = punit->id;
  int mr = get_unit_type (punit->type)->move_rate;

  punit->activity_count += mr/3;

  if (punit->activity == ACTIVITY_EXPLORE) {
    ai_manage_explorer(pplayer, punit);
    if (unit_list_find(&pplayer->units, id))
      handle_unit_activity_request(pplayer, punit, ACTIVITY_EXPLORE);
    else return;
  }

  if(punit->activity==ACTIVITY_PILLAGE) {
    if (punit->activity_target == 0) {     /* case for old save files */
      if (punit->activity_count >= 1) {
	int what =
	  get_preferred_pillage(
	    get_tile_infrastructure_set(
	      map_get_tile(punit->x, punit->y)));
	if (what != S_NO_SPECIAL) {
	  map_clear_special(punit->x, punit->y, what);
	  send_tile_info(0, punit->x, punit->y, TILE_KNOWN);
	  set_unit_activity(punit, ACTIVITY_IDLE);
	}
      }
    }
    else if (total_activity_targeted (punit->x, punit->y,
				      ACTIVITY_PILLAGE, punit->activity_target) >= 1) {
      int what_pillaged = punit->activity_target;
      map_clear_special(punit->x, punit->y, what_pillaged);
      unit_list_iterate (map_get_tile (punit->x, punit->y)->units, punit2)
        if ((punit2->activity == ACTIVITY_PILLAGE) &&
	    (punit2->activity_target == what_pillaged)) {
          set_unit_activity(punit2, ACTIVITY_IDLE);
	  send_unit_info(0, punit2, 0);
	}
      unit_list_iterate_end;
      send_tile_info(0, punit->x, punit->y, TILE_KNOWN);
    }
  }

  if(punit->activity==ACTIVITY_POLLUTION) {
    if (total_activity (punit->x, punit->y, ACTIVITY_POLLUTION) >= 3) {
      map_clear_special(punit->x, punit->y, S_POLLUTION);
      unit_list_iterate (map_get_tile (punit->x, punit->y)->units, punit2)
        if (punit2->activity == ACTIVITY_POLLUTION) {
          set_unit_activity(punit2, ACTIVITY_IDLE);
	  send_unit_info(0, punit2, 0);
	}
      unit_list_iterate_end;
      send_tile_info(0, punit->x, punit->y, TILE_KNOWN);
    }
  }

  if(punit->activity==ACTIVITY_FORTRESS) {
    if (total_activity (punit->x, punit->y, ACTIVITY_FORTRESS) >= 3) {
      map_set_special(punit->x, punit->y, S_FORTRESS);
      unit_list_iterate (map_get_tile (punit->x, punit->y)->units, punit2)
        if (punit2->activity == ACTIVITY_FORTRESS) {
          set_unit_activity(punit2, ACTIVITY_IDLE);
	  send_unit_info(0, punit2, 0);
	}
      unit_list_iterate_end;
      send_tile_info(0, punit->x, punit->y, TILE_KNOWN);
    }
  }
  
  if(punit->activity==ACTIVITY_IRRIGATE) {
    if (total_activity (punit->x, punit->y, ACTIVITY_IRRIGATE) >=
        map_build_irrigation_time(punit->x, punit->y)) {
      map_irrigate_tile(punit->x, punit->y);
      unit_list_iterate (map_get_tile (punit->x, punit->y)->units, punit2)
        if (punit2->activity == ACTIVITY_IRRIGATE) {
          set_unit_activity(punit2, ACTIVITY_IDLE);
	  send_unit_info(0, punit2, 0);
	}
      unit_list_iterate_end;
      send_tile_info(0, punit->x, punit->y, TILE_KNOWN);
    }
  }

  if(punit->activity==ACTIVITY_ROAD) {
    if (total_activity (punit->x, punit->y, ACTIVITY_ROAD) >=
        map_build_road_time(punit->x, punit->y)) {
      map_set_special(punit->x, punit->y, S_ROAD);
      unit_list_iterate (map_get_tile (punit->x, punit->y)->units, punit2)
        if (punit2->activity == ACTIVITY_ROAD) {
          set_unit_activity(punit2, ACTIVITY_IDLE);
	  send_unit_info(0, punit2, 0);
	}
      unit_list_iterate_end;
      send_tile_info(0, punit->x, punit->y, TILE_KNOWN);
    }
  }

  if(punit->activity==ACTIVITY_RAILROAD) {
    if (total_activity (punit->x, punit->y, ACTIVITY_RAILROAD) >= 3) {
      map_set_special(punit->x, punit->y, S_RAILROAD);
      unit_list_iterate (map_get_tile (punit->x, punit->y)->units, punit2)
        if (punit2->activity == ACTIVITY_RAILROAD) {
          set_unit_activity(punit2, ACTIVITY_IDLE);
	  send_unit_info(0, punit2, 0);
	}
      unit_list_iterate_end;
      send_tile_info(0, punit->x, punit->y, TILE_KNOWN);
    }
  }
  
  if(punit->activity==ACTIVITY_MINE) {
    if (total_activity (punit->x, punit->y, ACTIVITY_MINE) >=
        map_build_mine_time(punit->x, punit->y)) {
      map_mine_tile(punit->x, punit->y);
      unit_list_iterate (map_get_tile (punit->x, punit->y)->units, punit2)
        if (punit2->activity == ACTIVITY_MINE) {
          set_unit_activity(punit2, ACTIVITY_IDLE);
	  send_unit_info(0, punit2, 0);
	}
      unit_list_iterate_end;
      send_tile_info(0, punit->x, punit->y, TILE_KNOWN);
    }
  }

  if(punit->activity==ACTIVITY_TRANSFORM) {
    if (total_activity (punit->x, punit->y, ACTIVITY_TRANSFORM) >=
        map_transform_time(punit->x, punit->y)) {
      map_transform_tile(punit->x, punit->y);
      send_tile_info(0, punit->x, punit->y, TILE_KNOWN);
      unit_list_iterate (map_get_tile (punit->x, punit->y)->units, punit2)
        if (punit2->activity == ACTIVITY_TRANSFORM) {
          set_unit_activity(punit2, ACTIVITY_IDLE);
	  send_unit_info(0, punit2, 0);
	}
      unit_list_iterate_end;
    }
  }

  if(punit->activity==ACTIVITY_GOTO) {
    if (!punit->ai.control && (!is_military_unit(punit) ||
       punit->ai.passenger || !pplayer->ai.control)) {
/* autosettlers otherwise waste time; idling them breaks assignment */
/* Stalling infantry on GOTO so I can see where they're GOing TO. -- Syela */
      do_unit_goto(pplayer, punit);
    }
    return;
  }
  
  if(punit->activity==ACTIVITY_IDLE && 
     map_get_terrain(punit->x, punit->y)==T_OCEAN &&
     is_ground_unit(punit))
    set_unit_activity(punit, ACTIVITY_SENTRY);

  send_unit_info(0, punit, 0);
}


/**************************************************************************
 nuke a square
 1) remove all units on the square
 2) half the size of the city on the square
 if it isn't a city square or an ocean square then with 50% chance 
 add some pollution, then notify the client about the changes.
**************************************************************************/
void do_nuke_tile(int x, int y)
{
  struct unit_list *punit_list;
  struct city *pcity;
  punit_list=&map_get_tile(x, y)->units;
  
  while(unit_list_size(punit_list)) {
    struct unit *punit=unit_list_get(punit_list, 0);
    wipe_unit(0, punit);
  }

  if((pcity=map_get_city(x,y))) {
    if (pcity->size > 1) { /* size zero cities are ridiculous -- Syela */
      pcity->size/=2;
      auto_arrange_workers(pcity);
      send_city_info(0, pcity, 0);
    }
  }
  else if ((map_get_terrain(x,y)!=T_OCEAN && map_get_terrain(x,y)<=T_TUNDRA) &&
           (!(map_get_special(x,y)&S_POLLUTION)) && myrand(2)) { 
    map_set_special(x,y, S_POLLUTION);
    send_tile_info(0, x, y, TILE_KNOWN);
  }
}

/**************************************************************************
  nuke all the squares in a 3x3 square around the center of the explosion
**************************************************************************/
void do_nuclear_explosion(int x, int y)
{
  int i,j;
  for (i=0;i<3;i++)
    for (j=0;j<3;j++)
      do_nuke_tile(map_adjust_x(x+i-1),map_adjust_y(y+j-1));
}


/**************************************************************************
Move the unit if possible 
  if the unit has less moves than it costs to enter a square, we roll the dice:
  1) either succeed
  2) or have it's moves set to 0
  a unit can always move atleast once
**************************************************************************/
int try_move_unit(struct unit *punit, int dest_x, int dest_y) 
{
  if (myrand(1+map_move_cost(punit, dest_x, dest_y))>punit->moves_left && punit->moves_left<unit_move_rate(punit)) {
    punit->moves_left=0;
    send_unit_info(&game.players[punit->owner], punit, 0);
  }
  return punit->moves_left;
}

/**************************************************************************
  go by airline, if both cities have an airport and neither has been used this
  turn the unit will be transported by it and have it's moves set to 0
**************************************************************************/
int do_airline(struct unit *punit, int x, int y)
{
  struct city *city1, *city2;
  

  if (!(city1=map_get_city(punit->x, punit->y))) 
    return 0;
  if (!(city2=map_get_city(x,y)))
    return 0;
  if (!unit_can_airlift_to(punit,city2))
    return 0;
  city1->airlift=0;
  city2->airlift=0;
  punit->moves_left = 0;

  unit_list_unlink(&map_get_tile(punit->x, punit->y)->units, punit);
  punit->x = x; punit->y = y;
  unit_list_insert(&map_get_tile(x, y)->units, punit);

  connection_do_buffer(game.players[punit->owner].conn);
  send_unit_info(&game.players[punit->owner], punit, 0);
  send_city_info(&game.players[city1->owner], city1, 0);
  send_city_info(&game.players[city2->owner], city2, 0);
  notify_player_ex(&game.players[punit->owner], punit->x, punit->y, E_NOEVENT,
		   "Game: unit transported succesfully.");
  connection_do_unbuffer(game.players[punit->owner].conn);

  punit->moved=1;
  
  return 1;
}

/**************************************************************************
  called when a player conquers a city, remove buildings (not wonders and always palace) with game.razechance% chance
  set the city's shield stock to 0
**************************************************************************/
void raze_city(struct city *pcity)
{
  int i;
  pcity->improvements[B_PALACE]=0;
  for (i=0;i<B_LAST;i++) {
    if (city_got_building(pcity, i) && !is_wonder(i) 
	&& (myrand(100) < game.razechance)) {
      pcity->improvements[i]=0;
    }
  }
  if (pcity->shield_stock > 0)
    pcity->shield_stock=0;
  /*  advisor_choose_build(pcity);  we add the civ bug here :)*/
}

/**************************************************************************
  if target has more techs than pplayer, pplayer will get a random of these
  the clients will both be notified.
  TODO: Players with embassies in these countries should be notified aswell
**************************************************************************/
void get_a_tech(struct player *pplayer, struct player *target)
{
  int tec;
  int i;
  int j=0;
  for (i=0;i<A_LAST;i++) {
    if (get_invention(pplayer, i)!=TECH_KNOWN && 
	get_invention(target, i)== TECH_KNOWN) {
      j++;
    }
  }
  if (!j)  {
    if (target->future_tech > pplayer->future_tech) {
      notify_player(pplayer, "Game: You acquire Future Tech %d from %s",
                   ++(pplayer->future_tech), target->name);
      notify_player(target, "Game: %s discovered Future Tech. %d in the city.", 
                   pplayer->name, pplayer->future_tech);
    }
    return;
  }
  j=myrand(j)+1;
  for (i=0;i<A_LAST;i++) {
    if (get_invention(pplayer, i)!=TECH_KNOWN && 
	get_invention(target, i)== TECH_KNOWN) 
      j--;
    if (!j) break;
  }
  if (i==A_LAST) {
    freelog(LOG_NORMAL, "Bug in get_a_tech");
  }
  gamelog(GAMELOG_TECH,"%s acquire %s from %s",
          get_race_name_plural(pplayer->race),
          advances[i].name,
          get_race_name_plural(target->race));

  set_invention(pplayer, i, TECH_KNOWN);
  update_research(pplayer);
  do_conquer_cost(pplayer);
  pplayer->research.researchpoints++;
  notify_player(pplayer, "Game: You acquired %s from %s",
		advances[i].name, target->name); 
  notify_player(target, "Game: %s discovered %s in the city.", pplayer->name, 
		advances[i].name); 
  if (i==game.rtech.construct_rail) {
    upgrade_city_rails(pplayer, 0);
  }
  if (pplayer->research.researching==i) {
    tec=pplayer->research.researched;
    if (!choose_goal_tech(pplayer))
      choose_random_tech(pplayer);
    pplayer->research.researched=tec;
  }
}

/**************************************************************************
  finds a spot around pcity and place a partisan.
**************************************************************************/
void place_partisans(struct city *pcity,int count)
{
  int x,y,i;
  int ok[25];
  int total=0;

  for (i = 0,x = pcity->x -2; x < pcity->x + 3; x++) {
    for (y = pcity->y - 2; y < pcity->y + 3; y++, i++) {
      ok[i]=can_place_partisan(map_adjust_x(x), map_adjust_y(y));
      if(ok[i]) total++;
    }
  }

  while(count && total)  {
    for(i=0,x=myrand(total)+1;x;i++) if(ok[i]) x--;
    ok[--i]=0; x=(i/5)-2+pcity->x; y=(i%5)-2+pcity->y;
    create_unit(&game.players[pcity->owner], map_adjust_x(x), map_adjust_y(y),
		get_role_unit(L_PARTISAN,0), 0, 0, -1);
    count--; total--;
  }
}

/**************************************************************************
  if requirements to make partisans when a city is conquered is fullfilled
  this routine makes a lot of partisans based on the city's size.
  To be candidate for partisans the following things must be satisfied:
  1) Guerilla warfare must be known by atleast 1 player
  2) The owner of the city is the original player.
  3) The player must know about communism and gunpowder
  4) the player must run either a democracy or a communist society.
**************************************************************************/
void make_partisans(struct city *pcity)
{
  struct player *pplayer;
  int i, partisans;

  if (num_role_units(L_PARTISAN)==0)
    return;
  if (!tech_exists(game.rtech.u_partisan)
      || !game.global_advances[game.rtech.u_partisan]
      || pcity->original != pcity->owner)
    return;

  if (!government_has_flag(get_gov_pcity(pcity), G_INSPIRES_PARTISANS))
    return;
  
  pplayer = city_owner(pcity);
  for(i=0; i<MAX_NUM_TECH_LIST; i++) {
    int tech = game.rtech.partisan_req[i];
    if (tech == A_LAST) break;
    if (get_invention(pplayer, tech) != TECH_KNOWN) return;
    /* Was A_COMMUNISM and A_GUNPOWDER */
  }
  
  partisans = myrand(1 + pcity->size/2) + 1;
  if (partisans > 8) 
    partisans = 8;
  
  place_partisans(pcity,partisans);
}


/**************************************************************************
this is a highlevel routine
Remove the unit, and passengers if it is a carrying any.
Remove the _minimum_ number, eg there could be another boat on the square.
Parameter iter, if non-NULL, should be an iterator for a unit list,
and if it points to a unit which we wipe, we advance it first to
avoid dangling pointers.
NOTE: iter should not be an iterator for the map units list, but
city supported, or player units, is ok.  (For the map units list, would
have to pass iter on inside transporter_min_cargo_to_unitlist().)
**************************************************************************/
void wipe_unit_spec_safe(struct player *dest, struct unit *punit,
		    struct genlist_iterator *iter, int wipe_cargo)
{
  if(get_transporter_capacity(punit) 
     && map_get_terrain(punit->x, punit->y)==T_OCEAN
     && wipe_cargo) {
    
    struct unit_list list;
    
    transporter_min_cargo_to_unitlist(punit, &list);
    
    unit_list_iterate(list, punit2) {
      if (iter && ((struct unit*)ITERATOR_PTR((*iter))) == punit2) {
	freelog(LOG_VERBOSE, "iterating over %s in wipe_unit_safe",
		unit_name(punit2->type));
	ITERATOR_NEXT((*iter));
      }
      notify_player_ex(get_player(punit2->owner), 
		       punit2->x, punit2->y, E_UNIT_LOST,
		       "Game: You lost a%s %s when %s lost",
		       n_if_vowel(get_unit_type(punit2->type)->name[0]),
		       get_unit_type(punit2->type)->name,
		       get_unit_type(punit->type)->name);
      gamelog(GAMELOG_UNITL, "%s lose a%s %s when %s lost", 
	      get_race_name_plural(game.players[punit2->owner].race),
	      n_if_vowel(get_unit_type(punit2->type)->name[0]),
	      get_unit_type(punit2->type)->name,
	      get_unit_type(punit->type)->name);
      send_remove_unit(0, punit2->id);
      game_remove_unit(punit2->id);
    }
    unit_list_iterate_end;
    unit_list_unlink_all(&list);
  }

  send_remove_unit(0, punit->id);
  if (punit->homecity) {
    /* Get a handle on the unit's home city before the unit is wiped */
    struct city *pcity = find_city_by_id(punit->homecity);
    if (pcity) {
       game_remove_unit(punit->id);
       city_refresh(pcity);
       send_city_info(dest, pcity, 0);
    } else {
      /* can this happen? --dwp */
      freelog(LOG_NORMAL, "Can't find homecity of unit at (%d,%d)",
	      punit->x, punit->y);
      game_remove_unit(punit->id);
    }
  } else {
    game_remove_unit(punit->id);
  }
}

/**************************************************************************
...
**************************************************************************/

void wipe_unit_safe(struct player *dest, struct unit *punit,
		    struct genlist_iterator *iter){
  wipe_unit_spec_safe(dest, punit, iter, 1);
}


/**************************************************************************
...
**************************************************************************/
void wipe_unit(struct player *dest, struct unit *punit)
{
  wipe_unit_safe(dest, punit, NULL);
}


/**************************************************************************
this is a highlevel routine
the unit has been killed in combat => all other units on the
tile dies unless ...
**************************************************************************/
void kill_unit(struct unit *pkiller, struct unit *punit)
{
  struct city   *incity    = map_get_city(punit->x, punit->y);
  struct player *pplayer   = get_player(punit->owner);
  struct player *destroyer = get_player(pkiller->owner);
  char *loc_str   = get_location_str_in(pplayer, punit->x, punit->y, ", ");
  int  num_killed = unit_list_size(&(map_get_tile(punit->x, punit->y)->units));
  
  if( (incity) || 
      (map_get_special(punit->x, punit->y)&S_FORTRESS) || 
      (num_killed == 1)) {
    notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST,
		     "Game: You lost a%s %s under an attack from %s's %s%s",
		     n_if_vowel(get_unit_type(punit->type)->name[0]),
		     get_unit_type(punit->type)->name, destroyer->name,
		     unit_name(pkiller->type), loc_str);
    gamelog(GAMELOG_UNITL, "%s lose a%s %s to the %s",
	    get_race_name_plural(pplayer->race),
	    n_if_vowel(get_unit_type(punit->type)->name[0]),
	    get_unit_type(punit->type)->name,
	    get_race_name_plural(destroyer->race));
    
    send_remove_unit(0, punit->id);
    game_remove_unit(punit->id);
  }
  else {
    notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST,
		     "Game: You lost %d units under an attack from %s's %s%s",
		     num_killed, destroyer->name,
		     unit_name(pkiller->type), loc_str);
    unit_list_iterate(map_get_tile(punit->x, punit->y)->units, punit2) {
	notify_player_ex(&game.players[punit2->owner], 
			 punit2->x, punit2->y, E_UNIT_LOST,
			 "Game: You lost a%s %s under an attack from %s's %s",
			 n_if_vowel(get_unit_type(punit2->type)->name[0]),
			 get_unit_type(punit2->type)->name, destroyer->name,
                         unit_name(pkiller->type));
	gamelog(GAMELOG_UNITL, "%s lose a%s %s to the %s",
		get_race_name_plural(pplayer->race),
		n_if_vowel(get_unit_type(punit2->type)->name[0]),
		get_unit_type(punit2->type)->name,
		get_race_name_plural(destroyer->race));
	send_remove_unit(0, punit2->id);
	game_remove_unit(punit2->id);
    }
    unit_list_iterate_end;
  } 
}

/**************************************************************************
  send the unit to the players who need the info 
  dest = NULL means all players, dosend means send even if the player 
  can't see the unit.
**************************************************************************/
void send_unit_info(struct player *dest, struct unit *punit, int dosend)
{
  int o;
  struct packet_unit_info info;

  info.id=punit->id;
  info.owner=punit->owner;
  info.x=punit->x;
  info.y=punit->y;
  info.homecity=punit->homecity;
  info.veteran=punit->veteran;
  info.type=punit->type;
  info.movesleft=punit->moves_left;
  info.hp=punit->hp / game.firepower_factor;
  info.activity=punit->activity;
  info.activity_count=punit->activity_count;
  info.unhappiness=punit->unhappiness;
  info.upkeep=punit->upkeep;
  info.upkeep_food=punit->upkeep_food;
  info.upkeep_gold=punit->upkeep_gold;
  info.ai=punit->ai.control;
  info.fuel=punit->fuel;
  info.goto_dest_x=punit->goto_dest_x;
  info.goto_dest_y=punit->goto_dest_y;
  info.activity_target=punit->activity_target;

  for(o=0; o<game.nplayers; o++)           /* dests */
    if(!dest || &game.players[o]==dest)
      if(dosend || map_get_known(info.x, info.y, &game.players[o]))
	 send_packet_unit_info(game.players[o].conn, &info);
}

/**************************************************************************
  Returns a pointer to a (static) string which gives an informational
  message about location (x,y), in terms of cities known by pplayer.
  One of:
    "in Foo City"  or  "at Foo City" (see below)
    "outside Foo City"
    "near Foo City"
    "" (if no cities known)
  There are two variants for the first case, one when something happens
  inside the city, otherwise when it happens "at" but "outside" the city.
  Eg, when an attacker fails, the attacker dies "at" the city, but
  not "in" the city (since the attacker never made it in).
  The prefix is prepended to the message, _except_ for the last case; eg,
  for adding space/punctuation between rest of message and location string.
  Don't call this function directly; use the wrappers below.
**************************************************************************/
static char *get_location_str(struct player *pplayer, int x, int y,
				       char *prefix, int use_at)
{
  static char buffer[MAX_LEN_NAME+64];
  struct city *incity, *nearcity;

  incity = map_get_city(x, y);
  if (incity) {
    if (use_at) {
      sprintf(buffer, "%sat %s", prefix, incity->name);
    } else {
      sprintf(buffer, "%sin %s", prefix, incity->name);
    }
  } else {
    nearcity = dist_nearest_city(pplayer, x, y);
    if (nearcity) {
      if (is_tiles_adjacent(x, y, nearcity->x, nearcity->y)) {
	sprintf(buffer, "%soutside %s", prefix, nearcity->name);
      } else {
	sprintf(buffer, "%snear %s", prefix, nearcity->name);
      }
    } else {
      strcpy(buffer, "");
    }
  }
  return buffer;
}

char *get_location_str_in(struct player *pplayer, int x, int y, char *prefix)
{
  return get_location_str(pplayer, x, y, prefix, 0);
}

char *get_location_str_at(struct player *pplayer, int x, int y, char *prefix)
{
  return get_location_str(pplayer, x, y, prefix, 1);
 }
