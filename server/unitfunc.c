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

#include <player.h>
#include <unithand.h>
#include <unittools.h>
#include <unitfunc.h>
#include <packets.h>
#include <civserver.h>
#include <map.h>
#include <maphand.h>
#include <cityhand.h>
#include <citytools.h>
#include <cityturn.h>
#include <unit.h>
#include <plrhand.h>
#include <city.h>
#include <log.h>
#include <mapgen.h>
#include <events.h>
#include <shared.h>
#include <aiunit.h>
#include <sys/time.h>
#include <gotohand.h>
#include <settlers.h>

extern struct move_cost_map warmap;

/******************************************************************************
  bribe an enemy unit
  rules:
  1) can't bribe a unit if owner runs a democracy
  2) if player don't have enough money
  3) can't bribe a unit unless it's the only unit on the square. 
     (this is handled outside this function)
  if these conditions are fulfilled, the unit will be bribed
****************************************************************************/
void diplomat_bribe(struct player *pplayer, struct unit *pdiplomat, struct unit *pvictim)
{
  if(pplayer->economic.gold>=pvictim->bribe_cost) {
    if(game.players[pvictim->owner].government==G_DEMOCRACY)
      notify_player_ex(pplayer, pdiplomat->x, pdiplomat->y, E_NOEVENT, 
	"Game: You can't bribe a unit from a democratic nation.");
    else {
      pplayer->economic.gold-=pvictim->bribe_cost;
      notify_player_ex(&game.players[pvictim->owner], 
		    pvictim->x, pvictim->y, E_DIPLOMATED, 
		    "Game: One of your units was bribed!");
      notify_player_ex(pplayer, pvictim->x, pvictim->y, E_NOEVENT, 
		    "Game: Succeeded in bribing the enemy unit.");
      
      create_unit(pplayer, pvictim->x, pvictim->y,
		  pvictim->type, pvictim->veteran, pdiplomat->homecity);
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
		       struct city  *pcity)
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
  
  for (i=1;i<A_LAST;i++) {
    if (get_invention(pplayer, i)!=TECH_KNOWN && get_invention(target, i)== TECH_KNOWN) {
      j++;
    }
  }
  if (!j)
    if (target->future_tech > pplayer->future_tech) {
      notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT,
                      "Game: Your diplomat stole Future Tech. %d from %s",
                      ++(pplayer->future_tech), target->name);
      notify_player_ex(target, pcity->x, pcity->y, E_DIPLOMATED,
                      "Game: %s diplomat stole Future Tech. %d in %s.", 
                      pplayer->name, pplayer->future_tech, pcity->name);
      return;
    } else
	  return;
  if (pcity->steal) {
    notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT,
		       "Game: Your diplomat was caught in the attempt of stealing technology in %s.", pcity->name);
    notify_player_ex(target, pcity->x, pcity->y, E_DIPLOMATED,
		     "Game: %s diplomat failed in stealing technology in %s", pplayer->name, pcity->name);
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
    printf("Bug in diplomat_a_tech\n");
    return;
  }
  pcity->steal=1;
  notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT,
		   "Game: Your diplomat stole %s from %s",
		   advances[i].name, target->name); 
  notify_player_ex(target, pcity->x, pcity->y, E_DIPLOMATED,
		   "Game: %s diplomat stole %s in %s.", 
		   pplayer->name, advances[i].name, pcity->name); 
  if (i==A_RAILROAD) {
/*    struct city_list cl=pplayer->cities;*/
    struct genlist_iterator myiter;
    genlist_iterator_init(&myiter, &pplayer->cities.list, 0);
    notify_player(pplayer, "Game: The people are pleased to hear that your scientists finally know about railroads.\n      Workers spontaneously gather and upgrade all cities with railroads.", pcity->name);
    for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter)) {
      struct city *pcity1=(struct city *)ITERATOR_PTR(myiter);
      map_set_special(pcity1->x, pcity1->y, S_RAILROAD);
      send_tile_info(0, pcity1->x, pcity1->y, TILE_KNOWN);
    }
  }
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

int diplomat_infiltrate_city(struct player *pplayer, struct player *cplayer, struct unit *pdiplomat, struct city *pcity)
{
  /* For EVERY diplomat/spy on a square, there is a 1/N chance of succeeding.
   * This needs to be changed to take into account veteran status.
   */
  unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit)
    if (unit_flag(punit->type, F_DIPLOMAT))
      if (myrand(game.diplchance)) {
	
	/* Attacking Spy/Diplomat dies (N-1:N) */
	
	notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT, 
			 "Game: Your spy has been eliminated by a defending spy in %s.", pcity->name);
	notify_player_ex(cplayer, pcity->x, pcity->y, E_DIPLOMATED,
			 "Game: A%s spy has been eliminated while attempting to subvert %s.", n_if_vowel(get_race_name(cplayer->race)[0]), pcity->name);
	wipe_unit(0, pdiplomat);
	return 0;
      } else {
	
	/* Defending Spy/Diplomat dies (1:N) */
	
	notify_player_ex(cplayer, pcity->x, pcity->y, E_NOEVENT, 
			 "Game: Your spy has been eliminated defending against a spy in %s.", pcity->name);
	wipe_unit(0, punit);
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
  if (pdiplomat->type == U_SPY) {
    if (myrand(game.diplchance)) {
      
      /* Attacking Spy/Diplomat dies (N-1:N) chance */
      
      notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT, 
		     "Game: Your spy was captured after completing her mission in %s.", pcity->name);
    } else {

      /* Survived (1:N) chance */
      
      struct city *spyhome = find_city_by_id(pdiplomat->homecity);
/* is it possible for a spy not to have a spyhome? -- Syela */
      
      notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT, 
		     "Game: Your spy has successfully completed her mission and returned unharmed to  %s.", spyhome->name);
      
      /* move back to home city */ /* doing it the right way -- Syela */
      unit_list_unlink(&map_get_tile(pdiplomat->x, pdiplomat->y)->units, pdiplomat);

      pdiplomat->x = map_adjust_x(spyhome->x);
      pdiplomat->y = spyhome->y;
  
      unit_list_insert(&map_get_tile(pdiplomat->x, pdiplomat->y)->units, pdiplomat);

      pdiplomat->veteran = 1;
      pdiplomat->moves_left = 0;
      send_unit_info(0, pdiplomat, 0);

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
void diplomat_incite(struct player *pplayer, struct unit *pdiplomat, struct city *pcity)
{
  struct player *cplayer;
  struct city *pnewcity, *pc2;
  int i;

  if (!pcity)
    return;
 
  cplayer=city_owner(pcity);
  if (cplayer==pplayer || cplayer==NULL) 
    return;
  if(game.players[cplayer->player_no].government==G_DEMOCRACY) {
      notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT, 
		       "Game: You can't subvert a city from a democratic nation.");
      return;
  }
  if (pplayer->economic.gold<pcity->incite_revolt_cost) { 
    notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT, 
		     "Game: You don't have enough credits to subvert %s.", pcity->name);
    
    return;
  }
  if (diplomat_on_tile(pcity->x, pcity->y)) {
    notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT, 
		     "Game: Your spy has been eliminated by a defending spy in %s.", pcity->name);
    notify_player_ex(cplayer, pcity->x, pcity->y, E_DIPLOMATED,
		     "Game: A%s spy has been eliminated in %s..", n_if_vowel(get_race_name(cplayer->race)[0]), pcity->name);
    wipe_unit(0, pdiplomat);
    return;
  }

  /* Check if the Diplomat/Spy succeeds against defending Diplomats or Spies */

  if (!diplomat_infiltrate_city(pplayer, cplayer, pdiplomat, pcity))
    return;  /* failed against defending diplomats/spies */

  pplayer->economic.gold-=pcity->incite_revolt_cost;
  if (pcity->size >1) {
    pcity->size--;
    city_auto_remove_worker(pcity);
  }
  notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT, 
		   "Game: Revolt incited in %s, you now rule the city!", 
		   pcity->name);
  notify_player_ex(cplayer, pcity->x, pcity->y, E_DIPLOMATED, 
		   "Game: %s has revolted, %s influence suspected", pcity->name, get_race_name(pplayer->race));
  pnewcity=(struct city *)malloc(sizeof(struct city));
  *pnewcity=*pcity;

  for (i=0;i<4;i++) {
    pc2=find_city_by_id(pnewcity->trade[i]);
    if (can_establish_trade_route(pnewcity, pc2))    
      establish_trade_route(pnewcity, pc2);
  }

  pnewcity->id=get_next_id_number();
  add_city_to_cache(pnewcity);
  for (i = 0; i < B_LAST; i++) {
    if (is_wonder(i) && city_got_building(pnewcity, i))
      game.global_wonders[i] = pnewcity->id;
  }
  pnewcity->owner=pplayer->player_no;
  unit_list_init(&pnewcity->units_supported);
  city_list_insert(&pplayer->cities, pnewcity);

  /* Transfer units supported by this city to the new owner */

  transfer_city_units(pplayer, cplayer, pnewcity, pcity);

  /* Transfer wonders to new owner */

  for(i = B_APOLLO;i <= B_WOMENS; i++)
    if(game.global_wonders[i] == pcity->id)
      game.global_wonders[i] = pnewcity->id;

  remove_city(pcity);

  /* buying a city should result in getting new tech from the victim too */
  /* but not money!                                                      */

  get_a_tech(pplayer, cplayer);
   
  map_set_city(pnewcity->x, pnewcity->y, pnewcity);
  if ((get_invention(pplayer,A_RAILROAD)==TECH_KNOWN) &&
      (get_invention(cplayer, A_RAILROAD)!=TECH_KNOWN) &&
      (!(map_get_special(pnewcity->x, pnewcity->y)&S_RAILROAD))) {
    notify_player(pplayer, "Game: The people in %s are stunned by your technological insight!\n      Workers spontaneously gather and upgrade the city with railroads.",pnewcity->name);
    map_set_special(pnewcity->x, pnewcity->y, S_RAILROAD);
    send_tile_info(0, pnewcity->x, pnewcity->y, TILE_KNOWN);
  }

  pnewcity->shield_stock=0;
  city_check_workers(pplayer,pnewcity);
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
void diplomat_sabotage(struct player *pplayer, struct unit *pdiplomat, struct city *pcity)
{
  struct player *cplayer;
  char *prod;
  if (!pcity)
    return;
  cplayer=city_owner(pcity);
  if (cplayer==pplayer ||cplayer==NULL) return;

  /* Check if the Diplomat/Spy succeeds against defending Diplomats or Spies */

  if (!diplomat_infiltrate_city(pplayer, cplayer, pdiplomat, pcity))
    return;  /* failed against defending diplomats/spies */

  switch (myrand(2)) {
  case 0:
    pcity->shield_stock=0;
    if (pcity->is_building_unit) 
      prod=unit_name(pcity->currently_building);
    else
      prod=get_improvement_name(pcity->currently_building);
    notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT,
    "Game: Your Diplomat succeeded destroying the production of %s in %s", 
    prod, pcity->name);
    notify_player_ex(cplayer, pcity->x, pcity->y, E_DIPLOMATED, 
		     "Game: The production of %s was destroyed in %s, %s are suspected of sabotage.", 
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
	notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT,
			 "Game: Your Diplomat destroyed %s in %s.", 
			 get_improvement_name(building), pcity->name);
	notify_player_ex(cplayer, pcity->x, pcity->y, E_DIPLOMATED,
			 "Game: The %s destroyed %s in %s.", 
			 get_race_name_plural(pplayer->race),
                         get_improvement_name(building), pcity->name);
      } else {
	notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT,
		      "Game: Your Diplomat was caught in the attempt of industrial sabotage!");
	notify_player_ex(cplayer, pcity->x, pcity->y, E_DIPLOMATED,
			 "Game: You caught a%s %s diplomat in industrial sabotage!",
		n_if_vowel(get_race_name(pplayer->race)[0]),
		get_race_name(pplayer->race));
      }
    }
    break;
  }
  send_city_info(cplayer, pcity, 1);

  /* Check if a spy survives her mission */

  diplomat_leave_city(pplayer, pdiplomat, pcity);
}

/***************************************************************************
Restore adds move points/hitpoints and upgrade units if player
have leonardo's workshop.
Air units have to be on carriers or cities or they'll be wiped.
Missiles will have to be on submarines or in citites or they'll be wiped
choppers out of city will loose 1 hitpoint every turn.
triremes will be wiped with 50% chance 
if they're not close to land, and player  don't have lighthouse.
****************************************************************************/
void player_restore_units(struct player *pplayer)
{
  struct city *pcity;
  int leonardo=0;
  struct unit_list list;
  struct unit *next_unit;

  pcity=city_list_find_id(&pplayer->cities, 
			  game.global_wonders[B_LEONARDO]);
  if ((pcity && !wonder_obsolete(B_LEONARDO))) 
    leonardo=1;
  unit_list_iterate(pplayer->units, punit) {
    if (leonardo && can_upgrade_unittype(pplayer, punit->type)!=-1) {
      if (punit->hp==get_unit_type(punit->type)->hp) 
	punit->hp = get_unit_type(can_upgrade_unittype(pplayer, punit->type))->hp;
      notify_player(pplayer, "Game: Leonardo's workshop has upgraded %s to %s ",
		    unit_types[punit->type].name, 
		    unit_types[can_upgrade_unittype(pplayer, punit->type)].name);
      punit->type = can_upgrade_unittype(pplayer, punit->type);
      punit->veteran = 0;
      leonardo = 0;
    }

    unit_restore_hitpoints(pplayer, punit);
    unit_restore_movepoints(pplayer, punit);
    
    if(is_air_unit(punit)) {
      punit->fuel--;
      if(map_get_city(punit->x, punit->y))
	punit->fuel=get_unit_type(punit->type)->fuel;
      else {
	unit_list_iterate(map_get_tile(punit->x, punit->y)->units, punit2)
	  if (unit_flag(punit2->type,F_CARRIER))
	    punit->fuel=get_unit_type(punit->type)->fuel;
	  else if (unit_flag(punit->type, F_MISSILE) && 
		   unit_flag(punit2->type, F_SUBMARINE))
	    punit->fuel=get_unit_type(punit->type)->fuel;
	unit_list_iterate_end;
      }
      if(punit->fuel<=0) {
	send_remove_unit(0, punit->id);
	notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST, 
			 "Game: Your %s has run out of fuel",
			 unit_name(punit->type));
	wipe_unit(0, punit);
      }
    } else if (punit->type==U_TRIREME) {
      struct city *pcity;
      pcity=city_list_find_id(&pplayer->cities, 
			    game.global_wonders[B_LIGHTHOUSE]);
      if (!(pcity && !wonder_obsolete(B_LIGHTHOUSE))) { 
	if (!is_coastline(punit->x, punit->y) && (myrand(100) >= 50)) {
	  notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST, 
			   "Game: Your Trireme has been lost on the high seas");
          transporter_cargo_to_unitlist(punit, &list);
          printf("Trireme has gone Kaput!\n");
          while (1) {
            next_unit = (struct unit *)ITERATOR_PTR(myiter);
            if (next_unit && unit_list_find(&list, next_unit->id)) {
              ITERATOR_NEXT(myiter);
              printf("Iterating %d->%d\n", next_unit->id, (ITERATOR_PTR(myiter) ? 
                  ((struct unit *)ITERATOR_PTR(myiter))->id : 0));
            } else break;
          }
/* following filched from wipe_unit since t_c_2_u unlinks the damned -- Syela */
          unit_list_iterate(list, punit2) {
            send_remove_unit(0, punit2->id);
            game_remove_unit(punit2->id);
          }
          unit_list_iterate_end;
/* only now can we proceed to */
	  wipe_unit(pplayer, punit);
	}
      }
    }
  }
  unit_list_iterate_end;
}

/****************************************************************************
  add hitpoints to the unit, hp_gain_coord returns the amount to add
  united nations will speed up the process by 2 hp's / turn, means helicopters
  will actually not loose hp's every turn if player have that wonder.
*****************************************************************************/
void unit_restore_hitpoints(struct player *pplayer, struct unit *punit)
{
  struct city *pcity;
  
  punit->hp+=hp_gain_coord(punit);
  
  pcity=city_list_find_id(&pplayer->cities, game.global_wonders[B_UNITED]);

  if(pcity && !wonder_obsolete(B_UNITED))
    punit->hp+=2;
    
  if(punit->hp>get_unit_type(punit->type)->hp)
    punit->hp=get_unit_type(punit->type)->hp;
  if(punit->hp<0)
    punit->hp=0;
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
    struct city *pcity;
    if (punit->veteran) 
      return;
    pcity=city_list_find_id(&game.players[punit->owner].cities, 
			    game.global_wonders[B_SUNTZU]);
    if(pcity && !wonder_obsolete(B_SUNTZU)) 
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

  log(LOG_DEBUG, "attack:%d, defense:%d\n", attackpower, defensepower);
  if (!attackpower) {
      attacker->hp=0; 
  } else if (!defensepower) {
      defender->hp=0;
  }
  while (attacker->hp>0 && defender->hp>0) {
    if (myrand(attackpower+defensepower)>= defensepower) {
      defender->hp=defender->hp-get_unit_type(attacker->type)->firepower;
    } else {
      if (is_sailing_unit(defender) && map_get_city(defender->x, defender->y))
	attacker->hp=attacker->hp-1;              /* pearl harbour */
      else
	attacker->hp=attacker->hp-get_unit_type(defender->type)->firepower;
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

int get_virtual_defense_power(int a_type, int d_type, int x, int y)
{
  int defensepower=unit_types[d_type].defense_strength;
  int m_type = unit_types[a_type].move_type;
  struct city *pcity = map_get_city(x, y);

  defensepower *= get_tile_type(map_get_terrain(x, y))->defense_bonus;

  if (unit_flag(d_type, F_PIKEMEN) && unit_flag(a_type, F_HORSE)) 
    defensepower*=2;
  if (unit_flag(d_type, F_AEGIS) &&
       (m_type == AIR_MOVING || m_type == HELI_MOVING)) defensepower*=2;
  if (m_type == AIR_MOVING && pcity) {
    if (city_got_building(pcity, B_SAM))
      defensepower*=2;
    if (city_got_building(pcity, B_SDI) && unit_flag(a_type, F_MISSILE))
      defensepower*=2;
  } else if (m_type == SEA_MOVING && pcity) {
    if (city_got_building(pcity, B_COASTAL))
      defensepower*=2;
  } else if (!unit_flag(a_type, F_IGWALL) &&
          (m_type == LAND_MOVING || m_type == HELI_MOVING) &&
          pcity && city_got_citywalls(pcity))
    defensepower*=3;
  if (map_get_special(x, y)&S_FORTRESS && !pcity)
    defensepower*=2;
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
    defensepower*=2;
  if (is_air_unit(attacker)) {
    if (unit_behind_sam(defender))
      defensepower*=2;
    if (unit_behind_sdi(defender) && unit_flag(attacker->type, F_MISSILE))
      defensepower*=2;
  } else if (is_sailing_unit(attacker)) {
    if (unit_behind_coastal(defender))
      defensepower*=2;
  } else if (!unit_ignores_citywalls(attacker) && 
	     (is_heli_unit(attacker) || is_ground_unit(attacker)) && 
	     unit_behind_walls(defender)) 
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
void create_unit(struct player *pplayer, int x, int y, enum unit_type_id type,
		 int make_veteran, int homecity_id)
{
  struct unit *punit;
  struct city *pcity;
  punit=(struct unit *)malloc(sizeof(struct unit));
  punit->type=type;
  punit->id=get_next_id_number();
  punit->owner=pplayer->player_no;
  punit->x = map_adjust_x(x); /* was = x, caused segfaults -- Syela */
  punit->y=y;
  if (y < 0 || y >= map.ysize) printf("Whoa!  Creating %s at illegal loc (%d, %d)\n",
         get_unit_type(type)->name, x, y);
  punit->goto_dest_x=0;
  punit->goto_dest_y=0;
  
  pcity=game_find_city_by_id(homecity_id);
  punit->veteran=make_veteran;
  punit->homecity=homecity_id;
  punit->hp=get_unit_type(punit->type)->hp;
  punit->activity=ACTIVITY_IDLE;
  punit->activity_count=0;
  punit->upkeep=0;
  punit->unhappiness=0;
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
  punit->bribe_cost=unit_bribe_cost(punit);
  punit->moves_left=unit_move_rate(punit);
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
  assign a new task to a unit.
**************************************************************************/
void set_unit_activity(struct unit *punit, enum unit_activity new_activity)
{
  punit->activity=new_activity;
  punit->activity_count=0;
}

/**************************************************************************
  progress settlers in their current tasks, 
  and units that is pillaging.
  also move units that is on a goto.
**************************************************************************/
void update_unit_activity(struct player *pplayer, struct unit *punit)
{
  punit->activity_count+= (get_unit_type(punit->type)->move_rate)/3;
  
   if(punit->activity==ACTIVITY_PILLAGE && punit->activity_count>=1) {
      if(map_get_special(punit->x, punit->y)&S_IRRIGATION)
	map_clear_special(punit->x, punit->y, S_IRRIGATION);
      else if(map_get_special(punit->x, punit->y)&S_MINE)
        map_clear_special(punit->x, punit->y, S_MINE);
      else if(map_get_special(punit->x, punit->y)&S_RAILROAD)
	map_clear_special(punit->x, punit->y, S_RAILROAD);
      else 
	map_clear_special(punit->x, punit->y, S_ROAD);
    send_tile_info(0, punit->x, punit->y, TILE_KNOWN);
    set_unit_activity(punit, ACTIVITY_IDLE);
   }

  if(punit->activity==ACTIVITY_POLLUTION && punit->activity_count>=3) {
    map_clear_special(punit->x, punit->y, S_POLLUTION);
    send_tile_info(0, punit->x, punit->y, TILE_KNOWN);
    set_unit_activity(punit, ACTIVITY_IDLE);
  }

  if(punit->activity==ACTIVITY_FORTRESS && punit->activity_count>=3) {
    map_set_special(punit->x, punit->y, S_FORTRESS);
    send_tile_info(0, punit->x, punit->y, TILE_KNOWN);
    set_unit_activity(punit, ACTIVITY_IDLE);
  }
  
  if(punit->activity==ACTIVITY_IRRIGATE && 
     punit->activity_count>=map_build_irrigation_time(punit->x, punit->y)) {
    map_irrigate_tile(punit->x, punit->y);
    send_tile_info(0, punit->x, punit->y, TILE_KNOWN);
    punit->activity=ACTIVITY_IDLE;
    set_unit_activity(punit, ACTIVITY_IDLE);
  }

  if(punit->activity==ACTIVITY_ROAD && 
     punit->activity_count>map_build_road_time(punit->x, punit->y)) {
    map_set_special(punit->x, punit->y, S_ROAD);
    send_tile_info(0, punit->x, punit->y, TILE_KNOWN);
    punit->activity=ACTIVITY_IDLE;
    set_unit_activity(punit, ACTIVITY_IDLE);
  }

  if(punit->activity==ACTIVITY_RAILROAD && punit->activity_count>=3) {
    map_set_special(punit->x, punit->y, S_RAILROAD);
    send_tile_info(0, punit->x, punit->y, TILE_KNOWN);
    punit->activity=ACTIVITY_IDLE;
    handle_unit_activity_request(pplayer, punit, ACTIVITY_IDLE);
    set_unit_activity(punit, ACTIVITY_IDLE);
  }
  
  if(punit->activity==ACTIVITY_MINE && 
     punit->activity_count>=map_build_mine_time(punit->x, punit->y)) {
    map_mine_tile(punit->x, punit->y);
    send_tile_info(0, punit->x, punit->y, TILE_KNOWN);
    set_unit_activity(punit, ACTIVITY_IDLE);
  }

  if(punit->activity==ACTIVITY_GOTO) {
    if (!punit->ai.control && (!is_military_unit(punit) ||
       !is_ground_unit(punit) || !pplayer->ai.control)) {
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

  if((pcity=game_find_city_by_coor(x,y))) {
    pcity->size/=2;
    auto_arrange_workers(pcity);
    send_city_info(0, pcity, 0);
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
  
  if (!punit->moves_left)
    return 0;
  if (!(city1=map_get_city(punit->x, punit->y))) 
    return 0;
  if (!(city2=map_get_city(x,y)))
    return 0;
  if (city1->owner != city2->owner) 
    return 0;
  if (city1->airlift + city2->airlift < 2) 
    return 0;
  city1->airlift=0;
  city2->airlift=0;
  punit->moves_left = 0;
  unit_list_unlink(&map_get_tile(punit->x, punit->y)->units, punit);
  punit->x = x;
  punit->y = y;
  send_unit_info(&game.players[punit->owner], punit, 0);
  unit_list_insert(&map_get_tile(x, y)->units, punit);
  notify_player_ex(&game.players[punit->owner], punit->x, punit->y, E_NOEVENT,
		   "Game: unit transported succesfully.");
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
  if (!j)
    if (target->future_tech > pplayer->future_tech) {
      notify_player(pplayer, "Game: You acquire Future Tech %d from %s",
                   ++(pplayer->future_tech), target->name);
      notify_player(target, "Game: %s discovered Future Tech. %d in the city.", 
                   pplayer->name, pplayer->future_tech);
    }
  return;
  j=myrand(j)+1;
  for (i=0;i<A_LAST;i++) {
    if (get_invention(pplayer, i)!=TECH_KNOWN && 
	get_invention(target, i)== TECH_KNOWN) 
      j--;
    if (!j) break;
  }
  if (i==A_LAST) 
    printf("Bug in get_a_tech\n");
  set_invention(pplayer, i, TECH_KNOWN);
  update_research(pplayer);
  do_conquer_cost(pplayer);
  pplayer->research.researchpoints++;
  notify_player(pplayer, "Game: You acquired %s from %s",
		advances[i].name, target->name); 
  notify_player(target, "Game: %s discovered %s in the city.", pplayer->name, 
		advances[i].name); 
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
		U_PARTISAN, 0, 0);
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
  int partisans;
  if (!game.global_advances[A_GUERILLA] || pcity->original != pcity->owner)
    return;
  if (get_invention(city_owner(pcity), A_COMMUNISM) != TECH_KNOWN 
      && get_invention(city_owner(pcity), A_GUNPOWDER) != TECH_KNOWN)
    return;
  if (get_government(pcity->owner)!=G_DEMOCRACY
      && get_government(pcity->owner)!=G_COMMUNISM) 
    return;
  
  partisans = myrand(1 + pcity->size/2) + 1;
  if (partisans > 8) 
    partisans = 8;
  
  place_partisans(pcity,partisans);
}


/**************************************************************************
this is a highlevel routine
**************************************************************************/
void wipe_unit(struct player *dest, struct unit *punit)
{
  if(get_transporter_capacity(punit) && 
     map_get_terrain(punit->x, punit->y)==T_OCEAN) {
    struct unit_list list;
    
    transporter_cargo_to_unitlist(punit, &list);
    
    unit_list_iterate(list, punit2) {
      send_remove_unit(0, punit2->id);
      game_remove_unit(punit2->id);
    }
    unit_list_iterate_end;
  }
  send_remove_unit(0, punit->id);
  game_remove_unit(punit->id);
}

/**************************************************************************
this is a highlevel routine
the unit has been killed in combat => all other units on the
tile dies unless ...
**************************************************************************/
void kill_unit(struct unit *pkiller, struct unit *punit)
{
  int klaf;
  struct city *nearcity = dist_nearest_enemy_city(0, punit->x, punit->y);
  struct city *incity = map_get_city(punit->x, punit->y);
  struct player *dest = &game.players[pkiller->owner];
  klaf=unit_list_size(&(map_get_tile(punit->x, punit->y)->units));
  if( (incity) || 
      (map_get_special(punit->x, punit->y)&S_FORTRESS) || 
      (klaf == 1)) {
    if (incity) 
      notify_player_ex(&game.players[punit->owner], 
		       punit->x, punit->y, E_UNIT_LOST,
		       "Game: You lost a%s %s under an attack from %s's %s, in %s",
		       n_if_vowel(get_unit_type(punit->type)->name[0]),
		       get_unit_type(punit->type)->name, dest->name,
                       unit_name(pkiller->type), incity->name);
    else if (nearcity) 
      notify_player_ex(&game.players[punit->owner], 
		       punit->x, punit->y, E_UNIT_LOST,
		       "Game: You lost a%s %s under an attack from %s's %s, near %s",
		       n_if_vowel(get_unit_type(punit->type)->name[0]),
		       get_unit_type(punit->type)->name, dest->name,
                       unit_name(pkiller->type), nearcity->name);
    else
      notify_player_ex(&game.players[punit->owner], 
		       punit->x, punit->y, E_UNIT_LOST,
		       "Game: You lost a%s %s under an attack from %s's %s",
		       n_if_vowel(get_unit_type(punit->type)->name[0]),
		       get_unit_type(punit->type)->name, dest->name,
                       unit_name(pkiller->type));
    send_remove_unit(0, punit->id);
    game_remove_unit(punit->id);
  }  else {
      if (nearcity) notify_player_ex(&game.players[punit->owner], 
		       punit->x, punit->y, E_UNIT_LOST,
		       "Game: You lost %d units under an attack from %s's %s, near %s",
		       klaf, dest->name, unit_name(pkiller->type), nearcity->name);
      else notify_player_ex(&game.players[punit->owner], 
		       punit->x, punit->y, E_UNIT_LOST, 
		       "Game: You lost %d units under an attack from %s's %s",
		       klaf, dest->name, unit_name(pkiller->type));
      unit_list_iterate(map_get_tile(punit->x, punit->y)->units, punit2) {
	notify_player_ex(&game.players[punit2->owner], 
			 punit->x, punit->y, E_UNIT_LOST,
			 "Game: You lost a%s %s under an attack from %s's %s",
			 n_if_vowel(get_unit_type(punit2->type)->name[0]),
			 get_unit_type(punit2->type)->name, dest->name,
                         unit_name(pkiller->type));
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
  info.hp=punit->hp;
  info.activity=punit->activity;
  info.activity_count=punit->activity_count;
  info.unhappiness=punit->unhappiness;
  info.upkeep=punit->upkeep;
  info.ai=punit->ai.control;
  info.fuel=punit->fuel;
  info.goto_dest_x=punit->goto_dest_x;
  info.goto_dest_y=punit->goto_dest_y;
    
  for(o=0; o<game.nplayers; o++)           /* dests */
    if(!dest || &game.players[o]==dest)
      if(dosend || map_get_known(info.x, info.y, &game.players[o]))
	 send_packet_unit_info(game.players[o].conn, &info);
}
