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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "city.h"
#include "events.h"
#include "fcintl.h"
#include "government.h"
#include "idex.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "rand.h"
#include "shared.h"
#include "support.h"
#include "unit.h"

#include "barbarian.h"
#include "citytools.h"
#include "cityturn.h"
#include "gamelog.h"
#include "gotohand.h"
#include "mapgen.h"		/* assign_continent_numbers */
#include "maphand.h"
#include "plrhand.h"
#include "sernet.h"
#include "srv_main.h"
#include "unithand.h"

#include "aiunit.h"
#include "aitools.h"

#include "unittools.h"


static void unit_restore_hitpoints(struct player *pplayer, struct unit *punit);
static void unit_restore_movepoints(struct player *pplayer, struct unit *punit);
static void update_unit_activity(struct player *pplayer, struct unit *punit,
				 struct genlist_iterator *iter);
static void wakeup_neighbor_sentries(struct unit *punit);
static int upgrade_would_strand(struct unit *punit, int upgrade_type);
static void handle_leonardo(struct player *pplayer);

static void sentry_transported_idle_units(struct unit *ptrans);

static void refuel_air_units_from_carriers(struct player *pplayer);
static struct unit *find_best_air_unit_to_refuel(struct player *pplayer, 
						 int x, int y, int missile);
static struct unit *choose_more_important_refuel_target(struct unit *punit1,
							struct unit *punit2);

/**************************************************************************
  used to find the best defensive unit on a square
**************************************************************************/
static int rate_unit_d(struct unit *punit, struct unit *against)
{
  int val;

  if(!punit) return 0;
  val = get_total_defense_power(against,punit);

/*  return val*100+punit->hp;       This is unnecessarily crude. -- Syela */
  val *= punit->hp;
  val *= val;
  val /= get_unit_type(punit->type)->build_cost;
  return val; /* designed to minimize expected losses */
}

/**************************************************************************
get best defending unit which is NOT owned by pplayer
**************************************************************************/
struct unit *get_defender(struct player *pplayer, struct unit *aunit, 
			  int x, int y)
{
  struct unit *bestdef = NULL, *debug_unit = NULL;
  int unit_d, bestvalue=-1, ct=0;

  unit_list_iterate(map_get_tile(x, y)->units, punit) {
    debug_unit = punit;
    if (players_allied(pplayer->player_no, punit->owner))
      continue;
    ct++;
    if (unit_can_defend_here(punit)) {
      unit_d = rate_unit_d(punit, aunit);
      if (unit_d > bestvalue) {
	bestvalue = unit_d;
	bestdef = punit;
      }
    }
  }
  unit_list_iterate_end;
  if(ct&&!bestdef)
    freelog(LOG_ERROR, "Get_def bugged at (%d,%d). The most likely course"
	    " is a unit on an ocean square without a transport. The owner"
	    " of the unit is %s", x, y, game.players[debug_unit->owner].name);
  return bestdef;
}

/**************************************************************************
  used to find the best offensive unit on a square
**************************************************************************/
static int rate_unit_a(struct unit *punit, struct unit *against)
{
  int val;

  if(!punit) return 0;
  val = unit_types[punit->type].attack_strength *
        (punit->veteran ? 15 : 10);

  val *= punit->hp;
  val *= val;
  val /= get_unit_type(punit->type)->build_cost;
  return val; /* designed to minimize expected losses */
}

/**************************************************************************
 get unit at (x, y) that wants to kill aunit
**************************************************************************/
struct unit *get_attacker(struct player *pplayer, struct unit *aunit, 
			  int x, int y)
{
  struct unit *bestatt = 0;
  int bestvalue=-1, unit_a;
  unit_list_iterate(map_get_tile(x, y)->units, punit) {
    if (players_allied(pplayer->player_no, punit->owner))
      return 0;
    unit_a=rate_unit_a(punit, aunit);
    if(unit_a>bestvalue) {
      bestvalue=unit_a;
      bestatt=punit;
    }
  }
  unit_list_iterate_end;
  return bestatt;
}

/**************************************************************************
 returns the attack power, modified by moves left, and veteran status.
**************************************************************************/
int get_attack_power(struct unit *punit)
{
  int power;
  power=get_unit_type(punit->type)->attack_strength*10;
  if (punit->veteran) {
    power *= 3;
    power /= 2;
  }
  if (unit_flag(punit->type, F_IGTIRED)) return power;
  if ( punit->moves_left < SINGLE_MOVE )
     return (power*punit->moves_left)/SINGLE_MOVE;
    return power;
}

/**************************************************************************
  returns the defense power, modified by terrain and veteran status
**************************************************************************/
int get_defense_power(struct unit *punit)
{
  int power;
  int terra;
  int db;

  if (!punit || punit->type<0 || punit->type>=U_LAST
      || punit->type>=game.num_unit_types)
    abort();
  power=get_unit_type(punit->type)->defense_strength*10;
  if (punit->veteran) {
    power *= 3;
    power /= 2;
  }
  terra=map_get_terrain(punit->x, punit->y);
  db = get_tile_type(terra)->defense_bonus;
  if (map_get_special(punit->x, punit->y) & S_RIVER)
    db += (db * terrain_control.river_defense_bonus) / 100;
  power=(power*db)/10;

  return power;
}

/**************************************************************************
  a wrapper that returns whether or not a unit ignores citywalls
**************************************************************************/
int unit_ignores_citywalls(struct unit *punit)
{
  return (unit_flag(punit->type, F_IGWALL));
}

/**************************************************************************
  Takes into account unit move_type as well, and Walls variant.
**************************************************************************/
int unit_really_ignores_citywalls(struct unit *punit)
{
  return unit_ignores_citywalls(punit)
    || is_air_unit(punit)
    || (is_sailing_unit(punit) && !(improvement_variant(B_CITY)==1));
}

/**************************************************************************
 a wrapper function that returns whether or not the unit is on a citysquare
 with citywalls
**************************************************************************/
int unit_behind_walls(struct unit *punit)
{
  struct city *pcity;
  
  if((pcity=map_get_city(punit->x, punit->y)))
    return city_got_citywalls(pcity);
  
  return 0;
}

/**************************************************************************
 a wrapper function returns 1 if the unit is on a square with fortress
**************************************************************************/
int unit_on_fortress(struct unit *punit)
{
  return (map_get_special(punit->x, punit->y)&S_FORTRESS);
}

/**************************************************************************
 a wrapper function returns 1 if the unit is on a square with coastal defense
**************************************************************************/
int unit_behind_coastal(struct unit *punit)
{
  struct city *pcity;
  if((pcity=map_get_city(punit->x, punit->y)))
    return city_got_building(pcity, B_COASTAL);
  return 0;
}

/**************************************************************************
 a wrapper function returns 1 if the unit is on a square with sam site
**************************************************************************/
int unit_behind_sam(struct unit *punit)
{
  struct city *pcity;
  if((pcity=map_get_city(punit->x, punit->y)))
    return city_got_building(pcity, B_SAM);
  return 0;
}

/**************************************************************************
 a wrapper function returns 1 if the unit is on a square with sdi defense
**************************************************************************/
int unit_behind_sdi(struct unit *punit)
{
  struct city *pcity;
  if((pcity=map_get_city(punit->x, punit->y)))
    return city_got_building(pcity, B_SDI);
  return 0;
}

/**************************************************************************
  a wrapper function returns 1 if there is a sdi-defense close to the square
**************************************************************************/
struct city *sdi_defense_close(int owner, int x, int y)
{
  struct city *pcity;
  int lx, ly;
  for (lx=x-2;lx<x+3;lx++)
    for (ly=y-2;ly<y+3;ly++) {
      pcity=map_get_city(lx,ly);
      if (pcity && (pcity->owner!=owner) && city_got_building(pcity, B_SDI))
	return pcity;
    }
  return NULL;
}

/**************************************************************************
  returns a unit type with a given role, use -1 if you don't want a tech 
  role. Always try tech role and only if not available, return role unit.
**************************************************************************/
int find_a_unit_type(int role, int role_tech)
{
  int which[U_LAST];
  int i, num=0;

  if (role_tech != -1) {
    for(i=0; i<num_role_units(role_tech); i++) {
      int iunit = get_role_unit(role_tech, i);
      if (game.global_advances[get_unit_type(iunit)->tech_requirement] >= 2) {
	which[num++] = iunit;
      }
    }
  }
  if(num==0) {
    for(i=0; i<num_role_units(role); i++) {
      which[num++] = get_role_unit(role, i);
    }
  }
  if(num==0) {
    /* Ruleset code should ensure there is at least one unit for each
     * possibly-required role, or check before calling this function.
     */
    freelog(LOG_FATAL, "No unit types in find_a_unit_type(%d,%d)!",
	    role, role_tech);
    abort();
  }
  return which[myrand(num)];
}

/**************************************************************************
  Unit can't attack if:
 1) it doesn't have any attack power.
 2) it's not a fighter and the defender is a flying unit (except city/airbase).
 3) if it's not a marine (and ground unit) and it attacks from ocean.
 4) a ground unit can't attack a ship on an ocean square (except marines).
 5) the players are not at war.
**************************************************************************/
int can_unit_attack_unit_at_tile(struct unit *punit, struct unit *pdefender,
				 int dest_x, int dest_y)
{
  enum tile_terrain_type fromtile;
  enum tile_terrain_type totile;

  fromtile = map_get_terrain(punit->x, punit->y);
  totile   = map_get_terrain(dest_x, dest_y);
  
  if(!is_military_unit(punit))
    return 0;

  /* only fighters can attack planes, except for city or airbase attacks */
  if (!unit_flag(punit->type, F_FIGHTER) && is_air_unit(pdefender) &&
      !(map_get_city(dest_x, dest_y) || map_get_special(dest_x, dest_y)&S_AIRBASE)) {
    return 0;
  }
  /* can't attack with ground unit from ocean, except for marines */
  if(fromtile==T_OCEAN && is_ground_unit(punit) && !unit_flag(punit->type, F_MARINES)) {
    return 0;
  }

  if(totile==T_OCEAN && is_ground_unit(punit)) {
    return 0;
  }

  if (unit_flag(punit->type, F_NO_LAND_ATTACK) && totile!=T_OCEAN)  {
    return 0;
  }
  
  if (!players_at_war(punit->owner, pdefender->owner))
    return 0;

  /* Shore bombardement */
  if (fromtile==T_OCEAN && is_sailing_unit(punit) && totile!=T_OCEAN)
    return (get_attack_power(punit)>0);

  return 1;
}

/**************************************************************************
...
**************************************************************************/
int can_unit_attack_tile(struct unit *punit, int dest_x, int dest_y)
{
  struct unit *pdefender;
  pdefender=get_defender(&game.players[punit->owner], punit, dest_x, dest_y);
  return(can_unit_attack_unit_at_tile(punit, pdefender, dest_x, dest_y));
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

/**************************************************************************
...
**************************************************************************/
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
  if ((defender->activity == ACTIVITY_FORTIFIED || 
       map_get_city(defender->x, defender->y)) && 
      is_ground_unit(defender))
    defensepower*=1.5;

  return defensepower;
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
  int attackpower = get_total_attack_power(attacker,defender);
  int defensepower = get_total_defense_power(attacker,defender);
  int attack_firepower = get_unit_type(attacker->type)->firepower;
  int defense_firepower = get_unit_type(defender->type)->firepower;

  /* pearl harbour */
  if (is_sailing_unit(defender) && map_get_city(defender->x, defender->y))
    defense_firepower = 1;

  /* In land bombardment both units have their firepower reduced to 1 */
  if (is_sailing_unit(attacker)
      && map_get_terrain(defender->x, defender->y) != T_OCEAN
      && is_ground_unit(defender)) {
    attack_firepower = 1;
    defense_firepower = 1;
  }

  freelog(LOG_VERBOSE, "attack:%d, defense:%d, attack firepower:%d, defense firepower:%d",
	  attackpower, defensepower, attack_firepower, defense_firepower);
  if (!attackpower) {
      attacker->hp=0; 
  } else if (!defensepower) {
      defender->hp=0;
  }
  while (attacker->hp>0 && defender->hp>0) {
    if (myrand(attackpower+defensepower) >= defensepower) {
      defender->hp -= attack_firepower * game.firepower_factor;
    } else {
      attacker->hp -= defense_firepower * game.firepower_factor;
    }
  }
  if (attacker->hp<0) attacker->hp = 0;
  if (defender->hp<0) defender->hp = 0;

  if (attacker->hp)
    maybe_make_veteran(attacker); 
  else if (defender->hp)
    maybe_make_veteran(defender);
}






/**************************************************************************
  Returns whether the unit is allowed (by ZOC) to move from (x1,y1)
  to (x2,y2) (assumed adjacent).
  You CAN move if:
  1. You have units there already
  2. Your unit isn't a ground unit
  3. Your unit ignores ZOC (diplomat, freight, etc.)
  4. You're moving from or to a city
  5. You're moving from an ocean square (from a boat)
  6. The spot you're moving from or to is in your ZOC
**************************************************************************/
int zoc_ok_move_gen(struct unit *punit, int x1, int y1, int x2, int y2)
{
  struct tile *ptotile = map_get_tile(x2, y2);
  struct tile *pfromtile = map_get_tile(x1, y1);
  if (unit_really_ignores_zoc(punit))  /* !ground_unit or IGZOC */
    return 1;
  if (is_allied_unit_tile(ptotile, punit->owner))
    return 1;
  if (pfromtile->city || ptotile->city)
    return 1;
  if (map_get_terrain(x1,y1)==T_OCEAN)
    return 1;
  return (is_my_zoc(punit, x1, y1) || is_my_zoc(punit, x2, y2)); 
}

/**************************************************************************
  Convenience wrapper for zoc_ok_move_gen(), using the unit's (x,y)
  as the starting point.
**************************************************************************/
int zoc_ok_move(struct unit *punit, int x, int y)
{
  return zoc_ok_move_gen(punit, punit->x, punit->y, x, y);
}


/**************************************************************************
  Takes into account unit move_type as well as IGZOC
**************************************************************************/
int unit_really_ignores_zoc(struct unit *punit)
{
  return (!is_ground_unit(punit)) || (unit_flag(punit->type, F_IGZOC));
}

/**************************************************************************
  unit can be moved if:
  1) the unit is idle or on goto or connecting.
  2) the target location is on the map
  3) the target location is next to the unit
  4) there are no non-allied units on the target tile
  5) a ground unit can only move to ocean squares if there
     is a transporter with free capacity
  6) marines are the only units that can attack from a ocean square
  7) naval units can only be moved to ocean squares or city squares
  8) there are no peaceful but un-allied units on the target tile
  9) there is not a peaceful but un-allied city on the target tile
  10) there is no non-allied unit blocking (zoc) [or igzoc is true]
**************************************************************************/
int can_unit_move_to_tile(struct unit *punit, int dest_x, int dest_y, int igzoc)
{
  struct tile *pfromtile,*ptotile;
  int zoc;
  struct city *pcity;
  int src_x = punit->x;
  int src_y = punit->y;

  /* 1) */
  if (punit->activity!=ACTIVITY_IDLE
      && punit->activity!=ACTIVITY_GOTO
      && punit->activity!=ACTIVITY_PATROL
      && !punit->connecting)
    return 0;

  /* 2) */
  if (!normalize_map_pos(&dest_x, &dest_y))
    return 0;

  /* 3) */
  if (!is_tiles_adjacent(src_x, src_y, dest_x, dest_y))
    return 0;

  pfromtile = map_get_tile(src_x, src_y);
  ptotile = map_get_tile(dest_x, dest_y);

  /* 4) */
  if (is_non_allied_unit_tile(ptotile, punit->owner))
    return 0;

  if (is_ground_unit(punit)) {
    /* 5) */
    if (ptotile->terrain==T_OCEAN &&
	ground_unit_transporter_capacity(dest_x, dest_y, punit->owner) <= 0)
      return 0;

    /* Moving from ocean */
    if (pfromtile->terrain==T_OCEAN) {
      /* 6) */
      if (!unit_flag(punit->type, F_MARINES)
	  && is_enemy_city_tile(ptotile, punit->owner)) {
  	char *units_str = get_units_with_flag_string(F_MARINES);
  	if (units_str) {
  	  notify_player_ex(unit_owner(punit), src_x, src_y,
			   E_NOEVENT, _("Game: Only %s can attack from sea."),
			   units_str);
	  free(units_str);
	} else {
	  notify_player_ex(unit_owner(punit), src_x, src_y,
			   E_NOEVENT, _("Game: Cannot attack from sea."));
	}
	return 0;
      }
    }
  } else if (is_sailing_unit(punit)) {
    /* 7) */
    if (ptotile->terrain!=T_OCEAN
	&& ptotile->terrain!=T_UNKNOWN
	&& !is_allied_city_tile(ptotile, punit->owner))
      return 0;
  } 

  /* 8) */
  if (is_non_attack_unit_tile(ptotile, punit->owner)) {
    notify_player_ex(unit_owner(punit), src_x, src_y,
		     E_NOEVENT,
		     _("Game: Cannot attack unless you declare war first."));
    return 0;
  }

  /* 9) */
  pcity = ptotile->city;
  if (pcity && players_non_attack(pcity->owner, punit->owner)) {
    notify_player_ex(unit_owner(punit), src_x, src_y,
		     E_NOEVENT,
		     _("Game: Cannot attack unless you declare war first."));
    return 0;
  }

  /* 10) */
  zoc = igzoc || zoc_ok_move(punit, dest_x, dest_y);
  if (!zoc) {
    notify_player_ex(unit_owner(punit), src_x, src_y, E_NOEVENT,
		     _("Game: %s can only move into your own zone of control."),
		     unit_types[punit->type].name);
  }

  return zoc;
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
  if (unit_flag(punit->type, F_CARRIER)
      || unit_flag(punit->type, F_MISSILE_CARRIER))
    return 0;

  old_cap = get_transporter_capacity(punit);
  new_cap = unit_types[upgrade_type].transport_capacity;

  if (new_cap >= old_cap)
    return 0;

  tile_cap = 0;
  tile_ncargo = 0;
  unit_list_iterate(map_get_tile(punit->x, punit->y)->units, punit2) {
    if (punit2->owner == punit->owner) {
      if (is_sailing_unit(punit2) && is_ground_units_transport(punit2)) { 
	tile_cap += get_transporter_capacity(punit2);
      } else if (is_ground_unit(punit2)) {
	tile_ncargo++;
      }
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
Do Leonardo's Workshop upgrade(s). Select unit to upgrade by random. --Zamar
Now be careful not to strand units at sea with the Workshop. --dwp
****************************************************************************/
static void handle_leonardo(struct player *pplayer)
{
  int upgrade_type; 
  int leonardo_variant;
	
  struct unit_list candidates;
  int candidate_to_upgrade=-1;

  int i;

  leonardo_variant = improvement_variant(B_LEONARDO);
	
  unit_list_init(&candidates);
	
  unit_list_iterate(pplayer->units, punit) {
    if ((upgrade_type=can_upgrade_unittype(pplayer, punit->type))!=-1 &&
       !upgrade_would_strand(punit, upgrade_type))
      unit_list_insert(&candidates, punit); /* Potential candidate :) */
  } unit_list_iterate_end;
	
  if (!unit_list_size(&candidates))
    return; /* We have Leonardo, but nothing to upgrade! */
	
  if (!leonardo_variant)
    candidate_to_upgrade=myrand(unit_list_size(&candidates));

  i=0;	
  unit_list_iterate(candidates, punit) {
    if (leonardo_variant || i == candidate_to_upgrade) {
      upgrade_type=can_upgrade_unittype(pplayer, punit->type);
      notify_player(pplayer,
            _("Game: %s has upgraded %s to %s%s."),
            improvement_types[B_LEONARDO].name,
            get_unit_type(punit->type)->name,
            get_unit_type(upgrade_type)->name,
            get_location_str_in(pplayer, punit->x, punit->y));
      punit->veteran = 0;
      upgrade_unit(punit, upgrade_type);
    }
    i++;
  } unit_list_iterate_end;
	
  unit_list_unlink_all(&candidates);
}

/***************************************************************************
Select which unit is more important to refuel:
It's more important to refuel plane which has less fuel.
If those are equal then we refuel more valuable unit.
If those are equal then we refuel veteran unit.
If those are equal then we refuel unit which has more hp.
If those are equal then we refuel the first unit.
****************************************************************************/
static struct unit *choose_more_important_refuel_target(struct unit *punit1,
							struct unit *punit2)
{
  if (punit1->fuel != punit2->fuel)
    return punit1->fuel < punit2->fuel? punit1: punit2;
  
  if (get_unit_type(punit1->type)->build_cost !=
      get_unit_type(punit2->type)->build_cost)
    return get_unit_type(punit1->type)->build_cost > 
      get_unit_type(punit2->type)->build_cost? punit1: punit2;
  
  if (punit1->veteran != punit2->veteran)
    return punit1->veteran > punit2->veteran? punit1: punit2;
  
  if (punit1->hp != punit2->hp)
    return punit1->hp > punit2->hp? punit1: punit2;
  return punit1;
}

/***************************************************************************
...
****************************************************************************/
static struct unit *find_best_air_unit_to_refuel(struct player *pplayer, 
						 int x, int y, int missile)
{
  struct unit *best_unit=NULL;
  unit_list_iterate(map_get_tile(x, y)->units, punit) {
    if ((unit_owner(punit) == pplayer) && is_air_unit(punit) && 
        (!missile || unit_flag(punit->type, F_MISSILE))) {
      /* We must check that it isn't already refuelled. */ 
      if (punit->fuel < get_unit_type(punit->type)->fuel) { 
        if (!best_unit) 
          best_unit=punit;
        else 
          best_unit=choose_more_important_refuel_target(best_unit, punit);
      }
    }
  } unit_list_iterate_end;
  return best_unit;
}

/***************************************************************************
...
****************************************************************************/
static void refuel_air_units_from_carriers(struct player *pplayer)
{
  struct unit_list carriers;
  struct unit_list missile_carriers;
  
  struct unit *punit_to_refuel;
  
  unit_list_init(&carriers);
  unit_list_init(&missile_carriers);
  
  /* Temporarily use 'fuel' on Carriers and Subs to keep track
     of numbers of supported Air Units:   --dwp */

  unit_list_iterate(pplayer->units, punit) {
    if (unit_flag(punit->type, F_CARRIER)) {
      unit_list_insert(&carriers, punit);
      punit->fuel = get_unit_type(punit->type)->transport_capacity;
    } else if (unit_flag(punit->type, F_MISSILE_CARRIER)) {
      unit_list_insert(&missile_carriers, punit);
      punit->fuel = get_unit_type(punit->type)->transport_capacity;
    }
  } unit_list_iterate_end;

  /* Now iterate through missile_carriers and
   * refuel as many missiles as possible */

  unit_list_iterate(missile_carriers, punit) {
    while(punit->fuel) {
      punit_to_refuel= find_best_air_unit_to_refuel(
          pplayer, punit->x, punit->y, 1 /*missile */);
      if (!punit_to_refuel)
        break; /* Didn't find any */
      punit_to_refuel->fuel = get_unit_type(punit_to_refuel->type)->fuel;
      punit->fuel--;
    }
  } unit_list_iterate_end;

  /* Now refuel air units from carriers (also not yet refuelled missiles) */

  unit_list_iterate(carriers, punit) {
    while(punit->fuel) {
      punit_to_refuel= find_best_air_unit_to_refuel(
          pplayer, punit->x, punit->y, 0 /* any */);
      if (!punit_to_refuel) 
        break;
      punit_to_refuel->fuel = get_unit_type(punit_to_refuel->type)->fuel;
      punit->fuel--;
    }
  } unit_list_iterate_end;			

  /* Clean up temporary use of 'fuel' on Carriers and Subs: */
  unit_list_iterate(carriers, punit) {
    punit->fuel = 0;
  } unit_list_iterate_end;

  unit_list_unlink_all(&carriers);

  unit_list_iterate(missile_carriers, punit) {
    punit->fuel = 0;
  } unit_list_iterate_end;

  unit_list_unlink_all(&missile_carriers);
}

/***************************************************************************
Do Leonardo's Workshop upgrade if applicable.
Restore unit hitpoints. (movepoint-restoration moved to update_unit_activities)
Adjust air units for fuel: air units lose fuel unless in a city,
on a Carrier or on a airbase special (or, for Missles, on a Submarine).
Air units which run out of fuel get wiped.
Carriers and Submarines can now only supply fuel to a limited
number of units each, equal to their transport_capacity. --dwp
(Hitpoint adjustments include Helicopters out of cities, but
that is handled within unit_restore_hitpoints().)
Triremes will be wiped with a variable chance if they're not close to
land, and player doesn't have Lighthouse.
****************************************************************************/
void player_restore_units(struct player *pplayer)
{
  /* 1) get Leonardo out of the way first: */
  if (player_owns_active_wonder(pplayer, B_LEONARDO))
    handle_leonardo(pplayer);

  unit_list_iterate(pplayer->units, punit) {

    /* 2) Modify unit hitpoints. Helicopters can even lose them. */
    unit_restore_hitpoints(pplayer, punit);

    /* 3) Check that unit has hitpoints */
    if (punit->hp<=0) {
      /* This should usually only happen for heli units,
	 but if any other units get 0 hp somehow, catch
	 them too.  --dwp  */
      notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST, 
          _("Game: Your %s has run out of hit points."), 
          unit_name(punit->type));
      gamelog(GAMELOG_UNITF, "%s lose a %s (out of hp)", 
	      get_nation_name_plural(pplayer->nation),
	      unit_name(punit->type));
      wipe_unit_safe(punit, &myiter);
      continue; /* Continue iterating... */
    }

    /* 4) Check that triremes are near coastline, otherwise... */
    if (unit_flag(punit->type, F_TRIREME)
	&& myrand(100) < trireme_loss_pct(pplayer, punit->x, punit->y)) {
      notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST, 
		       _("Game: Your %s has been lost on the high seas."),
		       unit_name(punit->type));
      gamelog(GAMELOG_UNITTRI, "%s Trireme lost at sea",
	      get_nation_name_plural(pplayer->nation));
      wipe_unit_safe(punit, &myiter);
      continue; /* Continue iterating... */
    }

    /* 5) Resque planes if needed */
    if (is_air_unit(punit)) {
      /* Shall we emergency return home on the last vapors? */

      /* I think this is strongly against the spirit of client goto.
       * The problem is (again) that here we know too much. -- Zamar */

      if (punit->fuel == 1
	  && !is_airunit_refuel_point(punit->x, punit->y,
				      punit->owner, punit->type, 1)) {
	iterate_outward(punit->x, punit->y, punit->moves_left/3, x_itr, y_itr) {
	  if (is_airunit_refuel_point(x_itr, y_itr, punit->owner, punit->type, 0)
	      && (air_can_move_between(punit->moves_left/3, punit->x, punit->y,
				       x_itr, y_itr, punit->owner) >= 0) ) {
	    punit->goto_dest_x = x_itr;
	    punit->goto_dest_y = y_itr;
	    set_unit_activity(punit, ACTIVITY_GOTO);
	    do_unit_goto(punit, GOTO_MOVE_ANY, 0);
	    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT, 
			     _("Game: Your %s has returned to refuel."),
			     unit_name(punit->type));
	    goto OUT;
	  }
	} iterate_outward_end;
      }
    OUT:

      /* 6) Update fuel */
      punit->fuel--;

      /* 7) Automatically refuel air units in cities and airbases */
      if (map_get_city(punit->x, punit->y) ||
         map_get_special(punit->x, punit->y)&S_AIRBASE)
	punit->fuel=get_unit_type(punit->type)->fuel;
    }
  } unit_list_iterate_end;

  /* 8) Use carriers and submarines to refuel air units */
  refuel_air_units_from_carriers(pplayer);
  
  /* 9) Check if there are air units without fuel */
  unit_list_iterate(pplayer->units, punit) {
    if (is_air_unit(punit) && punit->fuel<=0) {
      notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST, 
		       _("Game: Your %s has run out of fuel."),
		       unit_name(punit->type));
      gamelog(GAMELOG_UNITF, "%s lose a %s (fuel)", 
	      get_nation_name_plural(pplayer->nation), unit_name(punit->type));
      wipe_unit_safe(punit, &myiter);
    } 
  } unit_list_iterate_end;
}

/****************************************************************************
  add hitpoints to the unit, hp_gain_coord returns the amount to add
  united nations will speed up the process by 2 hp's / turn, means helicopters
  will actually not loose hp's every turn if player have that wonder.
  Units which have moved don't gain hp, except the United Nations and
  helicopter effects still occur.
*****************************************************************************/
static void unit_restore_hitpoints(struct player *pplayer, struct unit *punit)
{
  int was_lower;

  was_lower=(punit->hp < get_unit_type(punit->type)->hp);

  if(!punit->moved) {
    punit->hp+=hp_gain_coord(punit);
  }
  
  if (player_owns_active_wonder(pplayer, B_UNITED)) 
    punit->hp+=2;
    
  if(is_heli_unit(punit)) {
    struct city *pcity = map_get_city(punit->x,punit->y);
    if(!pcity) {
      if(!(map_get_special(punit->x,punit->y) & S_AIRBASE))
        punit->hp-=get_unit_type(punit->type)->hp/10;
    }
  }

  if(punit->hp>=get_unit_type(punit->type)->hp) {
    punit->hp=get_unit_type(punit->type)->hp;
    if(was_lower&&punit->activity==ACTIVITY_SENTRY)
      set_unit_activity(punit,ACTIVITY_IDLE);
  }
  if(punit->hp<0)
    punit->hp=0;

  punit->moved=0;
  punit->paradropped=0;
}
  
/***************************************************************************
 move points are trivial, only modifiers to the base value is if it's
  sea units and the player has certain wonders/techs
***************************************************************************/
static void unit_restore_movepoints(struct player *pplayer, struct unit *punit)
{
  punit->moves_left=unit_move_rate(punit);
}

/**************************************************************************
  iterate through all units and update them.
**************************************************************************/
void update_unit_activities(struct player *pplayer)
{
  unit_list_iterate(pplayer->units, punit)
    update_unit_activity(pplayer, punit, &myiter);
  unit_list_iterate_end;
}

/**************************************************************************
  returns how many hp's a unit will gain on this square
  depends on whether or not it's inside city or fortress.
  barracks will regen landunits completely
  airports will regen airunits  completely
  ports    will regen navalunits completely
  fortify will add a little extra.
***************************************************************************/
int hp_gain_coord(struct unit *punit)
{
  int hp;
  struct city *pcity;
  if (unit_on_fortress(punit))
    hp=get_unit_type(punit->type)->hp/4;
  else
    hp=0;
  if((pcity=map_get_city(punit->x,punit->y))) {
    if ((city_got_barracks(pcity) &&
	 (is_ground_unit(punit) || improvement_variant(B_BARRACKS)==1)) ||
	(city_got_building(pcity, B_AIRPORT) && is_air_unit(punit)) || 
	(city_got_building(pcity, B_AIRPORT) && is_heli_unit(punit)) || 
	(city_got_building(pcity, B_PORT) && is_sailing_unit(punit))) {
      hp=get_unit_type(punit->type)->hp;
    }
    else
      hp=get_unit_type(punit->type)->hp/3;
  }
  else if (!is_heli_unit(punit))
    hp++;

  if(punit->activity==ACTIVITY_FORTIFIED)
    hp++;
  
  return hp;
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
  For each city adjacent to (x,y), check if landlocked.  If so, sell all
  improvements in the city that depend upon being next to an ocean tile.
  (Should be called after any ocean to land terrain changes.
   Assumes no city at (x,y).)
**************************************************************************/
static void city_landlocked_sell_coastal_improvements(int x, int y)
{
  /* FIXME: this should come from buildings.ruleset */
  static int coastal_improvements[] =
  {
    B_HARBOUR,
    B_PORT,
    B_COASTAL,
    B_OFFSHORE
  };
  #define coastal_improvements_count \
    (sizeof(coastal_improvements)/sizeof(coastal_improvements[0]))

  int i, j, k;
  struct city *pcity;
  struct player *pplayer;

  for (i=-1; i<=1; i++) {
    for (j=-1; j<=1; j++) {
      if ((i || j) &&
	  (pcity = map_get_city(x+i, y+j)) &&
	  (!(is_terrain_near_tile(x+i, y+j, T_OCEAN)))) {
	pplayer = city_owner(pcity);
	for (k=0; k<coastal_improvements_count; k++) {
	  if (city_got_building(pcity, coastal_improvements[k])) {
	    do_sell_building(pplayer, pcity, coastal_improvements[k]);
	    notify_player_ex(pplayer, x+i, y+j, E_IMP_SOLD,
			     _("Game: You sell %s in %s (now landlocked)"
			       " for %d gold."),
			     get_improvement_name(coastal_improvements[k]),
			     pcity->name,
			     improvement_value(coastal_improvements[k]));
	  }
	}
      }
    }
  }
}

/**************************************************************************
  Set the tile to be a river if required.
  It's required if one of the tiles nearby would otherwise be part of a
  river to nowhere.
  For simplicity, I'm assuming that this is the only exit of the river,
  so I don't need to trace it across the continent.  --CJM
  Also, note that this only works for R_AS_SPECIAL type rivers.  --jjm
**************************************************************************/
static void ocean_to_land_fix_rivers(int x, int y)
{
  /* clear the river if it exists */
  map_clear_special(x, y, S_RIVER);

  /* check north */
  if((map_get_special(x, y-1) & S_RIVER) &&
     (map_get_terrain(x-1, y-1) != T_OCEAN) &&
     (map_get_terrain(x+1, y-1) != T_OCEAN)) {
    map_set_special(x, y, S_RIVER);
    return;
  }

  /* check south */
  if((map_get_special(x, y+1) & S_RIVER) &&
     (map_get_terrain(x-1, y+1) != T_OCEAN) &&
     (map_get_terrain(x+1, y+1) != T_OCEAN)) {
    map_set_special(x, y, S_RIVER);
    return;
  }

  /* check east */
  if((map_get_special(x-1, y) & S_RIVER) &&
     (map_get_terrain(x-1, y-1) != T_OCEAN) &&
     (map_get_terrain(x-1, y+1) != T_OCEAN)) {
    map_set_special(x, y, S_RIVER);
    return;
  }

  /* check west */
  if((map_get_special(x+1, y) & S_RIVER) &&
     (map_get_terrain(x+1, y-1) != T_OCEAN) &&
     (map_get_terrain(x+1, y+1) != T_OCEAN)) {
    map_set_special(x, y, S_RIVER);
    return;
  }
}

/**************************************************************************
  Checks for terrain change between ocean and land.  Handles side-effects.
  (Should be called after any potential ocean/land terrain changes.)
  Also, returns an enum ocean_land_change, describing the change, if any.
**************************************************************************/
enum ocean_land_change { OLC_NONE, OLC_OCEAN_TO_LAND, OLC_LAND_TO_OCEAN };

static enum ocean_land_change check_terrain_ocean_land_change(int x, int y,
					      enum tile_terrain_type oldter)
{
  enum tile_terrain_type newter = map_get_terrain(x, y);

  if ((oldter == T_OCEAN) && (newter != T_OCEAN)) {
    /* ocean to land ... */
    ocean_to_land_fix_rivers(x, y);
    city_landlocked_sell_coastal_improvements(x, y);
    assign_continent_numbers();
    gamelog(GAMELOG_MAP, "(%d,%d) land created from ocean", x, y);
    return OLC_OCEAN_TO_LAND;
  } else if ((oldter != T_OCEAN) && (newter == T_OCEAN)) {
    /* land to ocean ... */
    assign_continent_numbers();
    gamelog(GAMELOG_MAP, "(%d,%d) ocean created from land", x, y);
    return OLC_LAND_TO_OCEAN;
  }
  return OLC_NONE;
}

/**************************************************************************
  progress settlers in their current tasks, 
  and units that is pillaging.
  also move units that is on a goto.
  restore unit move points (information needed for settler tasks)
**************************************************************************/
static void update_unit_activity(struct player *pplayer, struct unit *punit,
				 struct genlist_iterator *iter)
{
  int id = punit->id;
  int mr = get_unit_type (punit->type)->move_rate;
  int unit_activity_done = 0;
  int activity = punit->activity;
  enum ocean_land_change solvency = OLC_NONE;
  struct tile *ptile = map_get_tile(punit->x, punit->y);

  /* to allow a settler to begin a task with no moves left
     without it counting toward the time to finish */
  if (punit->moves_left){
    punit->activity_count += mr/3;
  }

  unit_restore_movepoints(pplayer, punit);

  if (punit->connecting && !can_unit_do_activity(punit, activity)) {
    punit->activity_count = 0;
    do_unit_goto(punit, get_activity_move_restriction(activity), 0);
  }

  /* if connecting, automagically build prerequisities first */
  if (punit->connecting && activity == ACTIVITY_RAILROAD &&
      !(map_get_tile(punit->x, punit->y)->special & S_ROAD)) {
    activity = ACTIVITY_ROAD;
  }

  if (activity == ACTIVITY_EXPLORE) {
    int more_to_explore = ai_manage_explorer(punit);
    if (more_to_explore && player_find_unit_by_id(pplayer, id))
      handle_unit_activity_request(punit, ACTIVITY_EXPLORE);
    else
      return;
  }

  if (activity==ACTIVITY_PILLAGE) {
    if (punit->activity_target == 0) {     /* case for old save files */
      if (punit->activity_count >= 1) {
	int what =
	  get_preferred_pillage(
	    get_tile_infrastructure_set(
	      map_get_tile(punit->x, punit->y)));
	if (what != S_NO_SPECIAL) {
	  map_clear_special(punit->x, punit->y, what);
	  send_tile_info(0, punit->x, punit->y);
	  set_unit_activity(punit, ACTIVITY_IDLE);
	}
      }
    }
    else if (total_activity_targeted (punit->x, punit->y,
				      ACTIVITY_PILLAGE, punit->activity_target) >= 1) {
      int what_pillaged = punit->activity_target;
      map_clear_special(punit->x, punit->y, what_pillaged);
      unit_list_iterate (map_get_tile(punit->x, punit->y)->units, punit2)
        if ((punit2->activity == ACTIVITY_PILLAGE) &&
	    (punit2->activity_target == what_pillaged)) {
	  set_unit_activity(punit2, ACTIVITY_IDLE);
	  send_unit_info(0, punit2);
	}
      unit_list_iterate_end;
      send_tile_info(0, punit->x, punit->y);
    }
  }

  if (activity==ACTIVITY_POLLUTION) {
    if (total_activity (punit->x, punit->y, ACTIVITY_POLLUTION) >= 3) {
      map_clear_special(punit->x, punit->y, S_POLLUTION);
      unit_activity_done = 1;
    }
  }

  if (activity==ACTIVITY_FALLOUT) {
    if (total_activity (punit->x, punit->y, ACTIVITY_FALLOUT) >= 3) {
      map_clear_special(punit->x, punit->y, S_FALLOUT);
      unit_activity_done = 1;
    }
  }

  if (activity==ACTIVITY_FORTRESS) {
    if (total_activity (punit->x, punit->y, ACTIVITY_FORTRESS) >= 3) {
      map_set_special(punit->x, punit->y, S_FORTRESS);
      unit_activity_done = 1;
    }
  }

  if (activity==ACTIVITY_AIRBASE) {
    if (total_activity (punit->x, punit->y, ACTIVITY_AIRBASE) >= 3) {
      map_set_special(punit->x, punit->y, S_AIRBASE);
      unit_activity_done = 1;
    }
  }
  
  if (activity==ACTIVITY_IRRIGATE) {
    if (total_activity (punit->x, punit->y, ACTIVITY_IRRIGATE) >=
        map_build_irrigation_time(punit->x, punit->y)) {
      enum tile_terrain_type old = map_get_terrain(punit->x, punit->y);
      map_irrigate_tile(punit->x, punit->y);
      solvency = check_terrain_ocean_land_change(punit->x, punit->y, old);
      unit_activity_done = 1;
    }
  }

  if (activity==ACTIVITY_ROAD) {
    if (total_activity (punit->x, punit->y, ACTIVITY_ROAD)
	+ total_activity (punit->x, punit->y, ACTIVITY_RAILROAD) >=
        map_build_road_time(punit->x, punit->y)) {
      map_set_special(punit->x, punit->y, S_ROAD);
      unit_activity_done = 1;
    }
  }

  if (activity==ACTIVITY_RAILROAD) {
    if (total_activity (punit->x, punit->y, ACTIVITY_RAILROAD) >= 3) {
      map_set_special(punit->x, punit->y, S_RAILROAD);
      unit_activity_done = 1;
    }
  }
  
  if (activity==ACTIVITY_MINE) {
    if (total_activity (punit->x, punit->y, ACTIVITY_MINE) >=
        map_build_mine_time(punit->x, punit->y)) {
      enum tile_terrain_type old = map_get_terrain(punit->x, punit->y);
      map_mine_tile(punit->x, punit->y);
      solvency = check_terrain_ocean_land_change(punit->x, punit->y, old);
      unit_activity_done = 1;
    }
  }

  if (activity==ACTIVITY_TRANSFORM) {
    if (total_activity (punit->x, punit->y, ACTIVITY_TRANSFORM) >=
        map_transform_time(punit->x, punit->y)) {
      enum tile_terrain_type old = map_get_terrain(punit->x, punit->y);
      map_transform_tile(punit->x, punit->y);
      solvency = check_terrain_ocean_land_change(punit->x, punit->y, old);
      unit_activity_done = 1;
    }
  }

  if (unit_activity_done) {
    send_tile_info(0, punit->x, punit->y);
    unit_list_iterate (map_get_tile(punit->x, punit->y)->units, punit2)
      if (punit2->activity == activity) {
	if (punit2->connecting) {
	  punit2->activity_count = 0;
	  do_unit_goto(punit2, get_activity_move_restriction(activity), 0);
	}
	else {
	  set_unit_activity(punit2, ACTIVITY_IDLE);
	}
	send_unit_info(0, punit2);
      }
    unit_list_iterate_end;
  }

  if (activity==ACTIVITY_FORTIFYING) {
    if (punit->activity_count >= 1) {
      set_unit_activity(punit,ACTIVITY_FORTIFIED);
    }
  }

  if (activity==ACTIVITY_GOTO) {
    if (!punit->ai.control && (!is_military_unit(punit) ||
       punit->ai.passenger || !pplayer->ai.control)) {
/* autosettlers otherwise waste time; idling them breaks assignment */
/* Stalling infantry on GOTO so I can see where they're GOing TO. -- Syela */
      do_unit_goto(punit, GOTO_MOVE_ANY, 1);
    }
    return;
  }

  if (punit->activity == ACTIVITY_PATROL)
    goto_route_execute(punit);

  send_unit_info(0, punit);

  unit_list_iterate(ptile->units, punit2) {
    if (!can_unit_continue_current_activity(punit2))
      handle_unit_activity_request(punit2, ACTIVITY_IDLE);
  } unit_list_iterate_end;

  /* Any units that landed in water or boats that landed on land as a
     result of settlers changing terrain must be moved back into their
     right environment.
     We advance the unit_list iterator passed into this routine from
     update_unit_activities() if we delete the unit it points to.
     We go to START each time we moved a unit to avoid problems with the
     tile unit list getting corrupted.

     FIXME:  We shouldn't do this at all!  There seems to be another
     bug which is expressed when units wind up on the "wrong" terrain;
     this is the bug that should be fixed.  Also, introduction of the
     "amphibious" movement category would allow the definition of units
     that can "safely" change land<->ocean -- in which case all others
     would (here) be summarily disbanded (suicide to accomplish their
     task, for the greater good :).   --jjm
  */
 START:
  switch (solvency) {
  case OLC_NONE:
    break; /* nothing */

  case OLC_LAND_TO_OCEAN:
    unit_list_iterate(ptile->units, punit2) {
      if (is_ground_unit(punit2)) {
	/* look for nearby land */
	adjc_iterate(punit->x, punit->y, x, y) {
	  struct tile *ptile2 = map_get_tile(x, y);
	  if (ptile2->terrain != T_OCEAN
	      && !is_non_allied_unit_tile(ptile2, punit2->owner)) {
	    if (get_transporter_capacity(punit2))
	      sentry_transported_idle_units(punit2);
	    freelog(LOG_VERBOSE,
		    "Moved %s's %s due to changing land to sea at (%d, %d).",
		    unit_owner(punit2)->name, unit_name(punit2->type),
		    punit2->x, punit2->y);
	    notify_player(unit_owner(punit2),
			  _("Game: Moved your %s due to changing"
			    " land to sea at (%d, %d)."),
			  unit_name(punit2->type), punit2->x, punit2->y);
	    move_unit(punit2, x, y, 1, 0, 0);
	    if (punit2->activity == ACTIVITY_SENTRY)
	      handle_unit_activity_request(punit2, ACTIVITY_IDLE);
	    goto START;
	  }
	} adjc_iterate_end;
	/* look for nearby transport */
	adjc_iterate(punit->x, punit->y, x, y) {
	  struct tile *ptile2 = map_get_tile(x, y);
	  if (ptile2->terrain == T_OCEAN
	      && ground_unit_transporter_capacity(x, y, punit2->owner) > 0) {
	    if (get_transporter_capacity(punit2))
	      sentry_transported_idle_units(punit2);
	    freelog(LOG_VERBOSE,
		    "Embarked %s's %s due to changing land to sea at (%d, %d).",
		    unit_owner(punit2)->name, unit_name(punit2->type),
		    punit2->x, punit2->y);
	    notify_player(unit_owner(punit2),
			  _("Game: Embarked your %s due to changing"
			    " land to sea at (%d, %d)."),
			  unit_name(punit2->type), punit2->x, punit2->y);
	    move_unit(punit2, x, y, 1, 0, 0);
	    if (punit2->activity == ACTIVITY_SENTRY)
	      handle_unit_activity_request(punit2, ACTIVITY_IDLE);
	    goto START;
	  }
	} adjc_iterate_end;
	/* if we get here we could not move punit2 */
	freelog(LOG_VERBOSE,
		"Disbanded %s's %s due to changing land to sea at (%d, %d).",
		unit_owner(punit2)->name, unit_name(punit2->type),
		punit2->x, punit2->y);
	notify_player(unit_owner(punit2),
		      _("Game: Disbanded your %s due to changing"
			" land to sea at (%d, %d)."),
		      unit_name(punit2->type), punit2->x, punit2->y);
	if (iter && ((struct unit*)ITERATOR_PTR((*iter))) == punit2)
	  ITERATOR_NEXT((*iter));
	wipe_unit_spec_safe(punit2, NULL, 0);
	goto START;
      }
    } unit_list_iterate_end;
    break;
  case OLC_OCEAN_TO_LAND:
    unit_list_iterate(ptile->units, punit2) {
      if (is_sailing_unit(punit2)) {
	/* look for nearby water */
	adjc_iterate(punit->x, punit->y, x, y) {
	  struct tile *ptile2 = map_get_tile(x, y);
	  if (ptile2->terrain == T_OCEAN
	      && !is_non_allied_unit_tile(ptile2, punit2->owner)) {
	    if (get_transporter_capacity(punit2))
	      sentry_transported_idle_units(punit2);
	    freelog(LOG_VERBOSE,
		    "Moved %s's %s due to changing sea to land at (%d, %d).",
		    unit_owner(punit2)->name, unit_name(punit2->type),
		    punit2->x, punit2->y);
	    notify_player(unit_owner(punit2),
			  _("Game: Moved your %s due to changing"
			    " sea to land at (%d, %d)."),
			  unit_name(punit2->type), punit2->x, punit2->y);
	    move_unit(punit2, x, y, 1, 1, 0);
	    if (punit2->activity == ACTIVITY_SENTRY)
	      handle_unit_activity_request(punit2, ACTIVITY_IDLE);
	    goto START;
	  }
	} adjc_iterate_end;
	/* look for nearby port */
	adjc_iterate(punit->x, punit->y, x, y) {
	  struct tile *ptile2 = map_get_tile(x, y);
	  if (is_allied_city_tile(ptile2, punit2->owner)
	      && !is_non_allied_unit_tile(ptile2, punit2->owner)) {
	    if (get_transporter_capacity(punit2))
	      sentry_transported_idle_units(punit2);
	    freelog(LOG_VERBOSE,
		    "Docked %s's %s due to changing sea to land at (%d, %d).",
		    unit_owner(punit2)->name, unit_name(punit2->type),
		    punit2->x, punit2->y);
	    notify_player(unit_owner(punit2),
			  _("Game: Docked your %s due to changing"
			    " sea to land at (%d, %d)."),
			  unit_name(punit2->type), punit2->x, punit2->y);
	    move_unit(punit2, x, y, 1, 1, 0);
	    if (punit2->activity == ACTIVITY_SENTRY)
	      handle_unit_activity_request(punit2, ACTIVITY_IDLE);
	    goto START;
	  }
	} adjc_iterate_end;
	/* if we get here we could not move punit2 */
	freelog(LOG_VERBOSE,
		"Disbanded %s's %s due to changing sea to land at (%d, %d).",
		unit_owner(punit2)->name, unit_name(punit2->type),
		punit2->x, punit2->y);
	notify_player(unit_owner(punit2),
		      _("Game: Disbanded your %s due to changing"
			" sea to land at (%d, %d)."),
		      unit_name(punit2->type), punit2->x, punit2->y);
	if (iter && ((struct unit*)ITERATOR_PTR((*iter))) == punit2)
	  ITERATOR_NEXT((*iter));
	wipe_unit_spec_safe(punit2, NULL, 0);
	goto START;
      }
    } unit_list_iterate_end;
    break;
  }
}








/**************************************************************************
...
**************************************************************************/
void advance_unit_focus(struct unit *punit)
{
  conn_list_iterate(unit_owner(punit)->connections, pconn) {
    struct packet_generic_integer packet;
    packet.value = punit->id;
    send_packet_generic_integer(pconn, PACKET_ADVANCE_FOCUS, &packet);
  } conn_list_iterate_end;
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
  Don't call this function directly; use the wrappers below.
**************************************************************************/
static char *get_location_str(struct player *pplayer, int x, int y, int use_at)
{
  static char buffer[MAX_LEN_NAME+64];
  struct city *incity, *nearcity;

  incity = map_get_city(x, y);
  if (incity) {
    if (use_at) {
      my_snprintf(buffer, sizeof(buffer), _(" at %s"), incity->name);
    } else {
      my_snprintf(buffer, sizeof(buffer), _(" in %s"), incity->name);
    }
  } else {
    nearcity = dist_nearest_city(pplayer, x, y, 0, 0);
    if (nearcity) {
      if (is_tiles_adjacent(x, y, nearcity->x, nearcity->y)) {
	my_snprintf(buffer, sizeof(buffer),
		   _(" outside %s"), nearcity->name);
      } else {
	my_snprintf(buffer, sizeof(buffer),
		    _(" near %s"), nearcity->name);
      }
    } else {
      buffer[0] = '\0';
    }
  }
  return buffer;
}

/**************************************************************************
  See get_location_str() above.
**************************************************************************/
char *get_location_str_in(struct player *pplayer, int x, int y)
{
  return get_location_str(pplayer, x, y, 0);
}

/**************************************************************************
  See get_location_str() above.
**************************************************************************/
char *get_location_str_at(struct player *pplayer, int x, int y)
{
  return get_location_str(pplayer, x, y, 1);
}

/**************************************************************************
...
**************************************************************************/
enum goto_move_restriction get_activity_move_restriction(enum unit_activity activity)
{
  enum goto_move_restriction restr;

  switch (activity) {
  case ACTIVITY_IRRIGATE:
    restr = GOTO_MOVE_CARDINAL_ONLY;
    break;
  case ACTIVITY_POLLUTION:
  case ACTIVITY_ROAD:
  case ACTIVITY_MINE:
  case ACTIVITY_FORTRESS:
  case ACTIVITY_RAILROAD:
  case ACTIVITY_PILLAGE:
  case ACTIVITY_TRANSFORM:
  case ACTIVITY_AIRBASE:
  case ACTIVITY_FALLOUT:
    restr = GOTO_MOVE_STRAIGHTEST;
    break;
  default:
    restr = GOTO_MOVE_ANY;
    break;
  }

  return (restr);
}

/**************************************************************************
...
**************************************************************************/
static int find_a_good_partisan_spot(struct city *pcity, int u_type,
				     int *x, int *y)
{
  int bestvalue = 0;
  /* coords of best tile in arg pointers */
  map_city_radius_iterate(pcity->x, pcity->y, x1, y1) {
    struct tile *ptile = map_get_tile(x1, y1);
    int value;
    if (ptile->terrain == T_OCEAN)
      continue;
    if (ptile->city)
      continue;
    if (unit_list_size(&ptile->units) > 0)
      continue;
    value = get_simple_defense_power(u_type, x1, y1);
    value *= 10;

    if (ptile->continent != map_get_continent(pcity->x, pcity->y))
      value /= 2;

    value -= myrand(value/3);

    if (value > bestvalue) {
      *x = x1;
      *y = y1;
      bestvalue = value;
    }
  } map_city_radius_iterate_end;

  return bestvalue > 0;
}

/**************************************************************************
  finds a spot around pcity and place a partisan.
**************************************************************************/
static void place_partisans(struct city *pcity, int count)
{
  int x, y;
  int u_type = get_role_unit(L_PARTISAN, 0);

  while (count-- && find_a_good_partisan_spot(pcity, u_type, &x, &y)) {
    struct unit *punit;
    punit = create_unit(city_owner(pcity), x, y, u_type, 0, 0, -1);
    if (can_unit_do_activity(punit, ACTIVITY_FORTIFYING)) {
      punit->activity = ACTIVITY_FORTIFIED; /* yes; directly fortified */
      send_unit_info(0, punit);
    }
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
...
**************************************************************************/
int enemies_at(struct unit *punit, int x, int y)
{
  int i, j, a = 0, d, db;
  struct player *pplayer = get_player(punit->owner);
  struct city *pcity = map_get_tile(x,y)->city;

  if (pcity && pcity->owner == punit->owner)
     return 0;

  db = get_tile_type(map_get_terrain(x, y))->defense_bonus;
  if (map_get_special(x, y) & S_RIVER)
    db += (db * terrain_control.river_defense_bonus) / 100;
  d = unit_vulnerability_virtual(punit) * db;
  for (j = y - 1; j <= y + 1; j++) {
    if (j < 0 || j >= map.ysize) continue;
    for (i = x - 1; i <= x + 1; i++) {
      if (same_pos(i, j, x, y)) continue; /* after some contemplation */
      if (!pplayer->ai.control && !map_get_known_and_seen(x, y, punit->owner)) continue;
      if (is_enemy_city_tile(map_get_tile(i, j), punit->owner)) return 1;
      unit_list_iterate(map_get_tile(i, j)->units, enemy)
        if (players_at_war(enemy->owner, punit->owner) &&
            can_unit_attack_unit_at_tile(enemy, punit, x, y)) {
          a += unit_belligerence_basic(enemy);
          if ((a * a * 10) >= d) return 1;
        }
      unit_list_iterate_end;
    }
  }
  return 0; /* as good a quick'n'dirty should be -- Syela */
}

/**************************************************************************
Disband given unit because of a stack conflict.
**************************************************************************/
static void disband_stack_conflict_unit(struct unit *punit, int verbose)
{
  freelog(LOG_VERBOSE, "Disbanded %s's %s at (%d, %d)",
	  get_player(punit->owner)->name, unit_name(punit->type),
	  punit->x, punit->y);
  if (verbose) {
    notify_player(get_player(punit->owner),
		  _("Game: Disbanded your %s at (%d, %d)."),
		  unit_name(punit->type), punit->x, punit->y);
  }
  wipe_unit(punit);
}

/**************************************************************************
Teleport punit to city at cost specified.  Returns success.
(If specified cost is -1, then teleportation costs all movement.)
                         - Kris Bubendorfer
**************************************************************************/
int teleport_unit_to_city(struct unit *punit, struct city *pcity,
			  int move_cost, int verbose)
{
  int src_x = punit->x;
  int src_y = punit->y;
  int dest_x = pcity->x;
  int dest_y = pcity->y;
  if (pcity->owner == punit->owner){
    freelog(LOG_VERBOSE, "Teleported %s's %s from (%d, %d) to %s",
	    unit_owner(punit)->name, unit_name(punit->type),
	    src_x, src_y, pcity->name);
    if (verbose) {
      notify_player(unit_owner(punit),
		    _("Game: Teleported your %s from (%d, %d) to %s."),
		    unit_name(punit->type), src_x, src_y, pcity->name);
    }

    if (move_cost == -1)
      move_cost = punit->moves_left;
    return move_unit(punit, dest_x, dest_y, 0, 0, move_cost);
  }
  return 0;
}

/**************************************************************************
  Resolve unit stack

  When in civil war (or an alliance breaks) there will potentially be units 
  from both sides coexisting on the same squares.  This routine resolves 
  this by teleporting the units in multiowner stacks to the closest city.

  That is, if a unit is closer to its city than the coexistent enemy unit,
  then the enemy unit is teleported to its owner's closest city.

                         - Kris Bubendorfer

  If verbose is true, the unit owner gets messages about where each
  units goes.  --dwp
**************************************************************************/
void resolve_unit_stack(int x, int y, int verbose)
{
  struct unit *punit, *cunit;
  struct tile *ptile = map_get_tile(x,y);

  /* We start by reducing the unit list until we only have allied units */
  while(1) {
    struct city *pcity, *ccity;

    if (unit_list_size(&ptile->units) == 0)
      return;

    punit = unit_list_get(&(ptile->units), 0);
    pcity = find_closest_owned_city(unit_owner(punit), x, y,
				    is_sailing_unit(punit), NULL);

    /* If punit is in an enemy city we send it to the closest friendly city
       This is not always caught by the other checks which require that
       there are units from two nations on the tile */
    if (ptile->city && !is_allied_city_tile(ptile, punit->owner)) {
      if (pcity)
	teleport_unit_to_city(punit, pcity, 0, verbose);
      else
	disband_stack_conflict_unit(punit, verbose);
      continue;
    }

    cunit = is_non_allied_unit_tile(ptile, punit->owner);
    if (!cunit)
      break;

    ccity = find_closest_owned_city(get_player(cunit->owner), x, y,
				    is_sailing_unit(cunit), NULL);

    if (pcity && ccity) {
      /* Both unit owners have cities; teleport unit farthest from its
	 owner's city to that city. This also makes sure we get no loops
	 from when we resolve the stack inside a city. */
      if (map_distance(x, y, pcity->x, pcity->y) 
	  < map_distance(x, y, ccity->x, ccity->y))
	teleport_unit_to_city(cunit, ccity, 0, verbose);
      else
	teleport_unit_to_city(punit, pcity, 0, verbose);
    } else {
      /* At least one of the unit owners doesn't have any cities;
	 if the other owner has any cities we teleport his units to
	 the closest. We take care not to teleport the unit to the
	 original square, as that would cause the while loop in this
	 function to potentially never stop. */
      if (pcity) {
	if (same_pos(x, y, pcity->x, pcity->y))
	  disband_stack_conflict_unit(cunit, verbose);
	else
	  teleport_unit_to_city(punit, pcity, 0, verbose);
      } else if (ccity) {
	if (same_pos(x, y, ccity->x, ccity->y))
	  disband_stack_conflict_unit(punit, verbose);
	else
	  teleport_unit_to_city(cunit, ccity, 0, verbose);
      } else {
	/* Neither unit owners have cities;
	   disband both units. */
	disband_stack_conflict_unit(punit, verbose);
	disband_stack_conflict_unit(cunit, verbose);
      }      
    }
  } /* end while */

  /* There is only one allied units left on this square.  If there is not 
     enough transporter capacity left send surplus to the closest friendly 
     city. */
  punit = unit_list_get(&(ptile->units), 0);

  if (ptile->terrain == T_OCEAN) {
  START:
    unit_list_iterate(ptile->units, vunit) {
      if (ground_unit_transporter_capacity(x, y, punit->owner) < 0) {
 	unit_list_iterate(ptile->units, wunit) {
 	  if (is_ground_unit(wunit) && wunit->owner == vunit->owner) {
 	    struct city *wcity =
 	      find_closest_owned_city(get_player(wunit->owner), x, y, 0, NULL);
 	    if (wcity)
 	      teleport_unit_to_city(wunit, wcity, 0, verbose);
 	    else
 	      disband_stack_conflict_unit(wunit, verbose);
 	    goto START;
 	  }
 	} unit_list_iterate_end; /* End of find a unit from that player to disband*/
      }
    } unit_list_iterate_end; /* End of find a player */
  }
}

/**************************************************************************
...
**************************************************************************/
int is_airunit_refuel_point(int x, int y, int playerid,
			    Unit_Type_id type, int unit_is_on_tile)
{
  struct player_tile *plrtile = map_get_player_tile(x, y, playerid);

  if ((is_allied_city_tile(map_get_tile(x, y), playerid)
       && !is_non_allied_unit_tile(map_get_tile(x, y), playerid))
      || (plrtile->special&S_AIRBASE
	  && !is_non_allied_unit_tile(map_get_tile(x, y), playerid)))
    return 1;

  if (unit_flag(type, F_MISSILE)) {
    int cap = missile_carrier_capacity(x, y, playerid);
    if (unit_is_on_tile)
      cap++;
    return cap>0;
  } else {
    int cap = airunit_carrier_capacity(x, y, playerid);
    if (unit_is_on_tile)
      cap++;
    return cap>0;
  }
}







/**************************************************************************
...
**************************************************************************/
void upgrade_unit(struct unit *punit, Unit_Type_id to_unit)
{
  struct player *pplayer = &game.players[punit->owner];
  int range = get_unit_type(punit->type)->vision_range;

  if (punit->hp==get_unit_type(punit->type)->hp) {
    punit->hp=get_unit_type(to_unit)->hp;
  }

  conn_list_do_buffer(&pplayer->connections);
  punit->type = to_unit;
  unfog_area(pplayer,punit->x,punit->y, get_unit_type(to_unit)->vision_range);
  fog_area(pplayer,punit->x,punit->y,range);
  send_unit_info(0, punit);
  conn_list_do_unbuffer(&pplayer->connections);
}

/**************************************************************************
 creates a unit, and set it's initial values, and put it into the right 
 lists.
 TODO: Maybe this procedure should refresh its homecity? so it'll show up 
 immediately on the clients? (refresh_city + send_city_info)
**************************************************************************/

/* This is a wrapper */

struct unit *create_unit(struct player *pplayer, int x, int y, Unit_Type_id type,
			 int make_veteran, int homecity_id, int moves_left)
{
  return create_unit_full(pplayer,x,y,type,make_veteran,homecity_id,moves_left,-1);
}

/* This is the full call.  We don't want to have to change all other calls to
   this function to ensure the hp are set */
struct unit *create_unit_full(struct player *pplayer, int x, int y,
			      Unit_Type_id type, int make_veteran, int homecity_id,
			      int moves_left, int hp_left)
{
  struct unit *punit;
  struct city *pcity;
  punit=fc_calloc(1,sizeof(struct unit));

  punit->type=type;
  punit->id=get_next_id_number();
  idex_register_unit(punit);
  punit->owner=pplayer->player_no;
  punit->x = map_adjust_x(x); /* was = x, caused segfaults -- Syela */
  punit->y=y;
  if (y < 0 || y >= map.ysize) {
    freelog(LOG_ERROR, "Whoa!  Creating %s at illegal loc (%d, %d)",
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
  if (pcity) {
    unit_list_insert(&pcity->units_supported, punit);
    assert(city_owner(pcity) == pplayer);
  }
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

  /* Probably not correct when unit changed owner (e.g. bribe) */
  punit->paradropped = 0;

  if( is_barbarian(pplayer) )
    punit->fuel = BARBARIAN_LIFE;

  /* ditto for connecting units */
  punit->connecting = 0;

  punit->transported_by = -1;
  punit->pgr = NULL;

  unfog_area(pplayer,x,y,get_unit_type(punit->type)->vision_range);
  send_unit_info(0, punit);
  maybe_make_first_contact(x, y, punit->owner);
  wakeup_neighbor_sentries(punit);

  /* The unit may have changed the available tiles in nearby cities. */
  map_city_radius_iterate(x, y, x1, y1) {
    struct city *pcity = map_get_city(x1, y1);
    if (pcity && pcity->owner != punit->owner) {
      if (city_check_workers(pcity, 1)) {
	send_city_info(city_owner(pcity), pcity);
      }
    }
  } map_city_radius_iterate_end;

  return punit;
}

/**************************************************************************
We remove the unit and see if it's disapperance have affected the homecity
and the city it was in.
**************************************************************************/
static void server_remove_unit(struct unit *punit)
{
  struct packet_generic_integer packet;
  struct city *pcity = map_get_city(punit->x, punit->y);
  struct city *phomecity = find_city_by_id(punit->homecity);
  int punit_x = punit->x, punit_y = punit->y, punit_owner = punit->owner;

  remove_unit_sight_points(punit);

  if (punit->pgr) {
    free(punit->pgr->pos);
    free(punit->pgr);
  }

  packet.value = punit->id;
  /* FIXME: maybe we should only send to those players who can see the unit,
     as the client automatically removes any units in a fogged square, and
     the send_unit_info() only sends units who are in non-fogged square.
     Leaving for now. */
  lsend_packet_generic_integer(&game.game_connections, PACKET_REMOVE_UNIT,
			       &packet);

  game_remove_unit(punit->id);  

  /* This unit may have blocked tiles of adjacent cities. Update them. */
  map_city_radius_iterate(punit_x, punit_y, x, y) {
    struct city *pcity2 = map_get_city(x, y);
    if (pcity2 && pcity2->owner != punit_owner) {
      if (city_check_workers(pcity2, 1)) {
	send_city_info(city_owner(pcity2), pcity2);
      }
    }
  } map_city_radius_iterate_end;

  if (phomecity) {
    city_refresh(phomecity);
    send_city_info(get_player(phomecity->owner), phomecity);
  }
  if (pcity && pcity != phomecity) {
    city_refresh(pcity);
    send_city_info(get_player(pcity->owner), pcity);
  }
}

/**************************************************************************
this is a highlevel routine
Remove the unit, and passengers if it is a carrying any.
Remove the _minimum_ number, eg there could be another boat on the square.
Parameter iter, if non-NULL, should be an iterator for a unit list,
and if it points to a unit which we wipe, we advance it first to
avoid dangling pointers.
NOTE: iter should not be an iterator for the map units list, but
city supported, or player units, is ok.
**************************************************************************/
void wipe_unit_spec_safe(struct unit *punit, struct genlist_iterator *iter,
			 int wipe_cargo)
{
  /* No need to remove air units as they can still fly away */
  if (is_ground_units_transport(punit)
      && map_get_terrain(punit->x, punit->y)==T_OCEAN
      && wipe_cargo) {
    int x = punit->x;
    int y = punit->y;
    int playerid = punit->owner;

    int capacity = ground_unit_transporter_capacity(x, y, playerid);
    capacity -= get_transporter_capacity(punit);

    unit_list_iterate(map_get_tile(x, y)->units, pcargo) {
      if (capacity >= 0)
	break;
      if (is_ground_unit(pcargo)) {
	if (iter && ((struct unit*)ITERATOR_PTR((*iter))) == pcargo) {
	  freelog(LOG_DEBUG, "iterating over %s in wipe_unit_safe",
		  unit_name(pcargo->type));
	  ITERATOR_NEXT((*iter));
	}
	notify_player_ex(get_player(playerid), x, y, E_UNIT_LOST,
			 _("Game: %s lost when %s was lost."),
			 get_unit_type(pcargo->type)->name,
			 get_unit_type(punit->type)->name);
	gamelog(GAMELOG_UNITL, "%s lose %s when %s lost", 
		get_nation_name_plural(game.players[playerid].nation),
		get_unit_type(pcargo->type)->name,
		get_unit_type(punit->type)->name);
	server_remove_unit(pcargo);
	capacity++;
      }
    } unit_list_iterate_end;
  }

  server_remove_unit(punit);
}

/**************************************************************************
...
**************************************************************************/

void wipe_unit_safe(struct unit *punit, struct genlist_iterator *iter){
  wipe_unit_spec_safe(punit, iter, 1);
}

/**************************************************************************
...
**************************************************************************/
void wipe_unit(struct unit *punit)
{
  wipe_unit_safe(punit, NULL);
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
  char *loc_str = get_location_str_in(pplayer, punit->x, punit->y);
  int num_killed[MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS];
  int ransom, unitcount = 0;
  
  /* barbarian leader ransom hack */
  if( is_barbarian(pplayer) && unit_has_role(punit->type, L_BARBARIAN_LEADER)
      && (unit_list_size(&(map_get_tile(punit->x, punit->y)->units)) == 1)
      && (is_ground_unit(pkiller) || is_heli_unit(pkiller)) ) {
    ransom = (pplayer->economic.gold >= 100)?100:pplayer->economic.gold;
    notify_player_ex(destroyer, pkiller->x, pkiller->y, E_UNIT_WIN_ATT,
		     _("Game: Barbarian leader captured, %d gold ransom paid."),
                     ransom);
    destroyer->economic.gold += ransom;
    pplayer->economic.gold -= ransom;
    send_player_info(destroyer,0);   /* let me see my new gold :-) */
    unitcount = 1;
  }

  if (!unitcount) {
    unit_list_iterate(map_get_tile(punit->x, punit->y)->units, vunit)
      if (players_at_war(pkiller->owner, vunit->owner))
	unitcount++;
    unit_list_iterate_end;
  }

  if( (incity) ||
      (map_get_special(punit->x, punit->y)&S_FORTRESS) ||
      (map_get_special(punit->x, punit->y)&S_AIRBASE) ||
      unitcount == 1) {
    notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST,
		     _("Game: %s lost to an attack by %s's %s%s."),
		     get_unit_type(punit->type)->name, destroyer->name,
		     unit_name(pkiller->type), loc_str);
    gamelog(GAMELOG_UNITL, "%s lose %s to the %s",
	    get_nation_name_plural(pplayer->nation),
	    get_unit_type(punit->type)->name,
	    get_nation_name_plural(destroyer->nation));

    wipe_unit(punit);
  } else { /* unitcount > 1 */
    int i;
    if (!(unitcount > 1)) {
      freelog(LOG_FATAL, "Error in kill_unit, unitcount is %i", unitcount);
      abort();
    }
    /* initialize */
    for (i = 0; i<MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS; i++) {
      num_killed[i] = 0;
    }

    /* count killed units */
    unit_list_iterate((map_get_tile(punit->x ,punit->y)->units), vunit)
      if (players_at_war(pkiller->owner, vunit->owner))
	num_killed[vunit->owner]++;
    unit_list_iterate_end;

    /* inform the owners */
    for (i = 0; i<MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS; i++) {
      if (num_killed[i]>0) {
	notify_player_ex(get_player(i), punit->x, punit->y, E_UNIT_LOST,
			 _("Game: You lost %d units to an attack"
			   " from %s's %s%s."),
			 num_killed[i], destroyer->name,
			 unit_name(pkiller->type), loc_str);
      }
    }

    /* remove the units */
    unit_list_iterate(map_get_tile(punit->x, punit->y)->units, punit2) {
      if (players_at_war(pkiller->owner, punit2->owner)) {
	notify_player_ex(unit_owner(punit2), 
			 punit2->x, punit2->y, E_UNIT_LOST,
			 _("Game: %s lost to an attack"
			   " from %s's %s."),
			 get_unit_type(punit2->type)->name, destroyer->name,
			 unit_name(pkiller->type));
	gamelog(GAMELOG_UNITL, "%s lose %s to the %s",
		get_nation_name_plural(get_player(punit2->owner)->nation),
		get_unit_type(punit2->type)->name,
		get_nation_name_plural(destroyer->nation));
	wipe_unit_spec_safe(punit2, NULL, 0);
      }
    }
    unit_list_iterate_end;
  }
}






/**************************************************************************
  Send the unit into to those connections in dest which can see the units
  position, or the specified (x,y) (if different).
  Eg, use x and y as where the unit came from, so that the info can be
  sent if the other players can see either the target or destination tile.
  dest = NULL means all connections (game.game_connections)
**************************************************************************/
void send_unit_info_to_onlookers(struct conn_list *dest, struct unit *punit,
				 int x, int y, int carried, int select_it)
{
  struct packet_unit_info info;

  if (dest==NULL) dest = &game.game_connections;
  
  package_unit(punit, &info, carried, select_it,
	       UNIT_INFO_IDENTITY, 0, FALSE);

  conn_list_iterate(*dest, pconn) {
    struct player *pplayer = pconn->player;
    if (pplayer==NULL && !pconn->observer) continue;
    if (pplayer==NULL
	|| map_get_known_and_seen(info.x, info.y, pplayer->player_no)
	|| map_get_known_and_seen(x, y, pplayer->player_no)) {
      send_packet_unit_info(pconn, &info);
    }
  }
  conn_list_iterate_end;
}

/**************************************************************************
  send the unit to the players who need the info 
  dest = NULL means all connections (game.game_connections)
**************************************************************************/
void send_unit_info(struct player *dest, struct unit *punit)
{
  struct conn_list *conn_dest = (dest ? &dest->connections
				 : &game.game_connections);
  send_unit_info_to_onlookers(conn_dest, punit, punit->x, punit->y, 0, 0);
}

/**************************************************************************
  For each specified connections, send information about all the units
  known to that player/conn.
**************************************************************************/
void send_all_known_units(struct conn_list *dest)
{
  int p;
  
  conn_list_do_buffer(dest);
  conn_list_iterate(*dest, pconn) {
    struct player *pplayer = pconn->player;
    if (pconn->player==NULL && !pconn->observer) {
      continue;
    }
    for(p=0; p<game.nplayers; p++) { /* send the players units */
      struct player *unitowner = &game.players[p];
      unit_list_iterate(unitowner->units, punit) {
	if (pplayer==NULL
	    || map_get_known_and_seen(punit->x, punit->y, pplayer->player_no)) {
	  send_unit_info_to_onlookers(&pconn->self, punit,
				      punit->x, punit->y, 0, 0);
	}
      }
      unit_list_iterate_end;
    }
  }
  conn_list_iterate_end;
  conn_list_do_unbuffer(dest);
  flush_packets();
}






/**************************************************************************
For all units which are transported by the given unit and that are
currently idle, sentry them.
**************************************************************************/
static void sentry_transported_idle_units(struct unit *ptrans)
{
  int x = ptrans->x;
  int y = ptrans->y;
  struct tile *ptile = map_get_tile(x, y);

  unit_list_iterate(ptile->units, pcargo) {
    if (pcargo->transported_by == ptrans->id
	&& pcargo->id != ptrans->id
	&& pcargo->activity == ACTIVITY_IDLE) {
      pcargo->activity = ACTIVITY_SENTRY;
      send_unit_info(unit_owner(pcargo), pcargo);
    }
  } unit_list_iterate_end;
}

/**************************************************************************
 nuke a square
 1) remove all units on the square
 2) half the size of the city on the square
 if it isn't a city square or an ocean square then with 50% chance 
 add some fallout, then notify the client about the changes.
**************************************************************************/
static void do_nuke_tile(int x, int y)
{
  struct unit_list *punit_list;
  struct city *pcity;
  punit_list=&map_get_tile(x, y)->units;
  
  while(unit_list_size(punit_list)) {
    struct unit *punit=unit_list_get(punit_list, 0);
    wipe_unit(punit);
  }

  if((pcity=map_get_city(x,y))) {
    if (pcity->size > 1) { /* size zero cities are ridiculous -- Syela */
      pcity->size/=2;
      auto_arrange_workers(pcity);
      send_city_info(0, pcity);
    }
  } else if ((map_get_terrain(x, y) != T_OCEAN &&
	      map_get_terrain(x, y) < T_UNKNOWN) && myrand(2)) {
    if (game.rgame.nuke_contamination == CONTAMINATION_POLLUTION) {
      if (!(map_get_special(x, y) & S_POLLUTION)) {
	map_set_special(x, y, S_POLLUTION);
	send_tile_info(0, x, y);
      }
    } else {
      if (!(map_get_special(x, y) & S_FALLOUT)) {
	map_set_special(x, y, S_FALLOUT);
	send_tile_info(0, x, y);
      }
    }
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
  if (myrand(1+map_move_cost(punit, dest_x, dest_y))>punit->moves_left &&
      punit->moves_left<unit_move_rate(punit)) {
    punit->moves_left=0;
    send_unit_info(&game.players[punit->owner], punit);
  }
  return punit->moves_left;
}

/**************************************************************************
  go by airline, if both cities have an airport and neither has been used this
  turn the unit will be transported by it and have it's moves set to 0
**************************************************************************/
int do_airline(struct unit *punit, int dest_x, int dest_y)
{
  struct city *city1, *city2;
  int src_x = punit->x;
  int src_y = punit->y;

  if (!(city1=map_get_city(src_x, src_y)))
    return 0;
  if (!(city2=map_get_city(dest_x, dest_y)))
    return 0;
  if (!unit_can_airlift_to(punit, city2))
    return 0;
  city1->airlift=0;
  city2->airlift=0;

  notify_player_ex(unit_owner(punit), dest_x, dest_y, E_NOEVENT,
		   _("Game: %s transported succesfully."),
		   unit_name(punit->type));

  move_unit(punit, dest_x, dest_y, 0, 0, punit->moves_left);

  send_city_info(city_owner(city1), city1);
  send_city_info(city_owner(city2), city2);

  return 1;
}

/**************************************************************************
Returns whether the drop was made or not. Note that it also returns 1 in
the case where the drop was succesfull, but the unit was killed by
barbarians in a hut
**************************************************************************/
int do_paradrop(struct unit *punit, int dest_x, int dest_y)
{
  if (!unit_flag(punit->type, F_PARATROOPERS)) {
    notify_player_ex(unit_owner(punit), punit->x, punit->y, E_NOEVENT,
		     _("Game: This unit type can not be paradropped."));
    return 0;
  }

  if (!can_unit_paradrop(punit))
    return 0;

  if (!map_get_known(dest_x, dest_y, unit_owner(punit))) {
    notify_player_ex(unit_owner(punit), dest_x, dest_y, E_NOEVENT,
		     _("Game: The destination location is not known."));
    return 0;
  }


  if (map_get_terrain(dest_x, dest_y) == T_OCEAN) {
    notify_player_ex(unit_owner(punit), dest_x, dest_y, E_NOEVENT,
		     _("Game: Cannot paradrop into ocean."));
    return 0;    
  }

  {
    int range = get_unit_type(punit->type)->paratroopers_range;
    int distance = real_map_distance(punit->x, punit->y, dest_x, dest_y);
    if (distance > range) {
      notify_player_ex(unit_owner(punit), dest_x, dest_y, E_NOEVENT,
		       _("Game: The distance to the target (%i) "
			 "is greater than the unit's range (%i)."),
		       distance, range);
      return 0;
    }
  }

  /* FIXME: this is a fog-of-war cheat.
     You get to know if there is an enemy on the tile*/
  if (is_non_allied_unit_tile(map_get_tile(dest_x, dest_y), punit->owner)) {
    notify_player_ex(unit_owner(punit), dest_x, dest_y, E_NOEVENT,
		     _("Game: Cannot paradrop because there are"
		       " enemy units on the destination location."));
    return 0;
  }

  /* All ok */
  {
    int move_cost = get_unit_type(punit->type)->paratroopers_mr_sub;
    punit->paradropped = 1;
    return move_unit(punit, dest_x, dest_y, 0, 0, move_cost);
  }
}

/**************************************************************************
Assigns units on ptrans' tile to ptrans if they should be. This is done by
setting their transported_by fields to the id of ptrans.
Checks a zillion things, some from situations that should never happen.
First drop all previously assigned units that do not fit on the transport.
If on land maybe pick up some extra units (decided by take_from_land variable)

This function is getting a bit larger and ugly. Perhaps it would be nicer
if it was recursive!?

FIXME: If in the open (not city), and leaving with only those units that are
       already assigned to us would strand some units try to reassign the
       transports. This reassign sometimes makes more changes than it needs to.

       Groundunit ground unit transports moving to and from ships never take
       units with them. This is ok, but not always practical.
**************************************************************************/
void assign_units_to_transporter(struct unit *ptrans, int take_from_land)
{
  int x = ptrans->x;
  int y = ptrans->y;
  int playerid = ptrans->owner;
  int capacity = get_transporter_capacity(ptrans);
  struct tile *ptile = map_get_tile(x, y);

  /*** FIXME: We kludge AI compatability problems with the new code away here ***/
  if (is_sailing_unit(ptrans)
      && is_ground_units_transport(ptrans)
      && unit_owner(ptrans)->ai.control) {
    unit_list_iterate(ptile->units, pcargo) {
      if (pcargo->owner == playerid) {
	pcargo->transported_by = -1;
      }
    } unit_list_iterate_end;

    unit_list_iterate(ptile->units, pcargo) {
      if ((ptile->terrain == T_OCEAN || pcargo->activity == ACTIVITY_SENTRY)
	  && capacity > 0
	  && is_ground_unit(pcargo)
	  && pcargo->owner == playerid) {
	pcargo->transported_by = ptrans->id;
	capacity--;
      }
    } unit_list_iterate_end;
    return;
  }

  /*** Ground units transports first ***/
  if (is_ground_units_transport(ptrans)) {
    int available = ground_unit_transporter_capacity(x, y, playerid);

    /* See how many units are dedicated to this transport, and remove extra units */
    unit_list_iterate(ptile->units, pcargo) {
      if (pcargo->transported_by == ptrans->id) {
	if (is_ground_unit(pcargo)
	    && capacity > 0
	    && (ptile->terrain == T_OCEAN || pcargo->activity == ACTIVITY_SENTRY)
	    && pcargo->owner == playerid
	    && pcargo->id != ptrans->id
	    && !(is_ground_unit(ptrans) && (ptile->terrain == T_OCEAN
					    || is_ground_units_transport(pcargo))))
	  capacity--;
	else
	  pcargo->transported_by = -1;
      }
    } unit_list_iterate_end;

    /** We are on an ocean tile. All units must get a transport **/
    if (ptile->terrain == T_OCEAN) {
      /* While the transport is not full and units will strand if we don't take
	 them with we us dedicate them to this transport. */
      if (available - capacity < 0 && !is_ground_unit(ptrans)) {
	unit_list_iterate(ptile->units, pcargo) {
	  if (capacity == 0)
	    break;
	  if (is_ground_unit(pcargo)
	      && pcargo->transported_by != ptrans->id
	      && pcargo->owner == playerid) {
	    capacity--;
	    pcargo->transported_by = ptrans->id;
	  }
	} unit_list_iterate_end;
      }
    } else { /** We are on a land tile **/
      /* If ordered to do so we take extra units with us, provided they
	 are not already commited to another transporter on the tile */
      if (take_from_land) {
	unit_list_iterate(ptile->units, pcargo) {
	  if (capacity == 0)
	    break;
	  if (is_ground_unit(pcargo)
	      && pcargo->transported_by != ptrans->id
	      && pcargo->activity == ACTIVITY_SENTRY
	      && pcargo->id != ptrans->id
	      && pcargo->owner == playerid
	      && !(is_ground_unit(ptrans) && (ptile->terrain == T_OCEAN
					      || is_ground_units_transport(pcargo)))) {
	    int has_trans = 0;
	    unit_list_iterate(ptile->units, ptrans2) {
	      if (ptrans2->id == pcargo->transported_by)
		has_trans = 1;
	    } unit_list_iterate_end;
	    if (!has_trans) {
	      capacity--;
	      pcargo->transported_by = ptrans->id;
	    }
	  }
	} unit_list_iterate_end;
      }
    }
    return;
    /*** Allocate air and missile units ***/
  } else if (is_air_units_transport(ptrans)) {
    struct player_tile *plrtile = map_get_player_tile(x, y, playerid);
    int is_refuel_point = is_allied_city_tile(map_get_tile(x, y), playerid)
      || (plrtile->special&S_AIRBASE
	  && !is_non_allied_unit_tile(map_get_tile(x, y), playerid));
    int missiles_only = unit_flag(ptrans->type, F_MISSILE_CARRIER)
      && !unit_flag(ptrans->type, F_CARRIER);

    /* Make sure we can transport the units marked as being transported by ptrans */
    unit_list_iterate(ptile->units, pcargo) {
      if (ptrans->id == pcargo->transported_by) {
	if (pcargo->owner == playerid
	    && pcargo->id != ptrans->id
	    && (!is_sailing_unit(pcargo))
	    && (unit_flag(pcargo->type, F_MISSILE) || !missiles_only)
	    && !(is_ground_unit(ptrans) && ptile->terrain == T_OCEAN)
	    && (capacity > 0)) {
	  if (is_air_unit(pcargo))
	    capacity--;
	  /* Ground units are handled below */
	} else
	  pcargo->transported_by = -1;
      }
    } unit_list_iterate_end;

    /** We are at a refuel point **/
    if (is_refuel_point) {
      unit_list_iterate(ptile->units, pcargo) {
	if (capacity == 0)
	  break;
	if (is_air_unit(pcargo)
	    && pcargo->id != ptrans->id
	    && pcargo->transported_by != ptrans->id
	    && pcargo->activity == ACTIVITY_SENTRY
	    && (unit_flag(pcargo->type, F_MISSILE) || !missiles_only)
	    && pcargo->owner == playerid) {
	  int has_trans = 0;
	  unit_list_iterate(ptile->units, ptrans2) {
	    if (ptrans2->id == pcargo->transported_by)
	      has_trans = 1;
	  } unit_list_iterate_end;
	  if (!has_trans) {
	    capacity--;
	    pcargo->transported_by = ptrans->id;
	  }
	}
      } unit_list_iterate_end;
    } else { /** We are in the open. All units must have a transport if possible **/
      int aircap = airunit_carrier_capacity(x, y, playerid);
      int miscap = missile_carrier_capacity(x, y, playerid);

      /* Not enough capacity. Take anything we can */
      if ((aircap < capacity || miscap < capacity)
	  && !(is_ground_unit(ptrans) && ptile->terrain == T_OCEAN)) {
	/* We first take nonmissiles, as missiles can be transported on anything,
	   but nonmissiles can not */
	if (!missiles_only) {
	  unit_list_iterate(ptile->units, pcargo) {
	    if (capacity == 0)
	      break;
	    if (is_air_unit(pcargo)
		&& pcargo->id != ptrans->id
		&& pcargo->transported_by != ptrans->id
		&& !unit_flag(pcargo->type, F_MISSILE)
		&& pcargo->owner == playerid) {
	      capacity--;
	      pcargo->transported_by = ptrans->id;
	    }
	  } unit_list_iterate_end;
	}
	
	/* Just take anything there's left.
	   (which must be missiles if we have capacity left) */
	unit_list_iterate(ptile->units, pcargo) {
	  if (capacity == 0)
	    break;
	  if (is_air_unit(pcargo)
	      && pcargo->id != ptrans->id
	      && pcargo->transported_by != ptrans->id
	      && (!missiles_only || unit_flag(pcargo->type, F_MISSILE))
	      && pcargo->owner == playerid) {
	    capacity--;
	    pcargo->transported_by = ptrans->id;
	  }
	} unit_list_iterate_end;
      }
    }

    /** If any of the transported air units have land units on board take them with us **/
    {
      int totcap = 0;
      int available = ground_unit_transporter_capacity(x, y, playerid);
      struct unit_list trans2s;
      unit_list_init(&trans2s);

      unit_list_iterate(ptile->units, pcargo) {
	if (pcargo->transported_by == ptrans->id
	    && is_ground_units_transport(pcargo)
	    && is_air_unit(pcargo)) {
	  totcap += get_transporter_capacity(pcargo);
	  unit_list_insert(&trans2s, pcargo);
	}
      } unit_list_iterate_end;

      unit_list_iterate(ptile->units, pcargo2) {
	int has_trans = 0;
	unit_list_iterate(trans2s, ptrans2) {
	  if (pcargo2->transported_by == ptrans2->id)
	    has_trans = 1;
	} unit_list_iterate_end;
	if (pcargo2->transported_by == ptrans->id)
	  has_trans = 1;

	if (has_trans
	    && is_ground_unit(pcargo2)) {
	  if (totcap > 0
	      && (ptile->terrain == T_OCEAN || pcargo2->activity == ACTIVITY_SENTRY)
	      && pcargo2->owner == playerid
	      && pcargo2 != ptrans) {
	    pcargo2->transported_by = ptrans->id;
	    totcap--;
	  } else
	    pcargo2->transported_by = -1;
	}
      } unit_list_iterate_end;

      /* Uh oh. Not enough space on the square we leave if we don't
	 take extra units with us */
      if (totcap > available && ptile->terrain == T_OCEAN) {
	unit_list_iterate(ptile->units, pcargo2) {
	  if (is_ground_unit(pcargo2)
	      && totcap > 0
	      && pcargo2->owner == playerid
	      && pcargo2->transported_by != ptrans->id) {
	    pcargo2->transported_by = ptrans->id;
	    totcap--;
	  }
	} unit_list_iterate_end;
      }
    }
  } else {
    unit_list_iterate(ptile->units, pcargo) {
      if (ptrans->id == pcargo->transported_by)
	pcargo->transported_by = -1;
    } unit_list_iterate_end;
    freelog (LOG_VERBOSE, "trying to assign cargo to a non-transporter "
	     "of type %s at %i,%i",
	     get_unit_name(ptrans->type), ptrans->x, ptrans->y);
  }
}

/*****************************************************************
  Will wake up any neighboring enemy sentry units
*****************************************************************/
static void wakeup_neighbor_sentries(struct unit *punit)
{
  /* There may be sentried units with a sightrange>3, but we don't
     wake them up if the punit is farther away than 3. */
  square_iterate(punit->x, punit->y, 3, x, y) {
    unit_list_iterate(map_get_tile(x, y)->units, penemy) {
      int range = get_unit_type(penemy->type)->vision_range;
      enum unit_move_type move_type = get_unit_type(penemy->type)->move_type;
      enum tile_terrain_type terrain = map_get_terrain(x, y);

      if (!players_allied(punit->owner, penemy->owner)
	  && penemy->activity == ACTIVITY_SENTRY
	  && map_get_known_and_seen(punit->x, punit->y, penemy->owner)
	  && range >= real_map_distance(punit->x, punit->y, x, y)
	  && player_can_see_unit(unit_owner(penemy), punit)
	  /* on board transport; don't awaken */
	  && !(move_type == LAND_MOVING && terrain == T_OCEAN)) {
	set_unit_activity(penemy, ACTIVITY_IDLE);
	send_unit_info(0, penemy);
      }
    } unit_list_iterate_end;
  } square_iterate_end;
}

/**************************************************************************
Does: 1) updates  the units homecity and the city it enters/leaves (the
         cities happiness varies). This also takes into account if the
	 unit enters/leaves a fortress.
      2) handles any huts at the units destination.
      3) awakes any sentried units on neightboring tiles.
      4) updates adjacent cities' unavailable tiles.

FIXME: Sometimes it is not neccesary to send cities because the goverment
       doesn't care if a unit is away or not.
**************************************************************************/
static void handle_unit_move_consequences(struct unit *punit, int src_x, int src_y,
					  int dest_x, int dest_y)
{
  struct city *fromcity = map_get_city(src_x, src_y);
  struct city *tocity = map_get_city(dest_x, dest_y);
  struct city *homecity = NULL;
  struct player *pplayer = get_player(punit->owner);
  /*  struct government *g = get_gov_pplayer(pplayer);*/
  int senthome = 0;
  if (punit->homecity)
    homecity = find_city_by_id(punit->homecity);

  wakeup_neighbor_sentries(punit);
  maybe_make_first_contact(dest_x, dest_y, punit->owner);

  if (tocity)
    handle_unit_enter_city(punit, tocity);

  /* We only do this for non-AI players to now make sure the AI turns
     doesn't take too long. Perhaps we should make a special refresh_city
     functions that only refreshed happiness. */
  if (!pplayer->ai.control) {
    /* might have changed owners or may be destroyed */
    tocity = map_get_city(dest_x, dest_y);

    if (tocity) { /* entering a city */
      if (tocity->owner == punit->owner) {
	if (tocity != homecity) {
	  city_refresh(tocity);
	  send_city_info(pplayer, tocity);
	}
      }

      if (homecity) {
	city_refresh(homecity);
	send_city_info(pplayer, homecity);
      }
      senthome = 1;
    }

    if (fromcity) { /* leaving a city */
      if (!senthome && homecity) {
	city_refresh(homecity);
	send_city_info(pplayer, homecity);
      }
      if (fromcity != homecity && fromcity->owner == punit->owner) {
	city_refresh(fromcity);
	send_city_info(pplayer, fromcity);
      }
      senthome = 1;
    }

    /* entering/leaving a fortress */
    if (map_get_tile(dest_x, dest_y)->special&S_FORTRESS
	&& homecity
	&& is_friendly_city_near(punit->owner, dest_x, dest_y)
	&& !senthome) {
      city_refresh(homecity);
      send_city_info(pplayer, homecity);
    }

    if (map_get_tile(src_x, src_y)->special&S_FORTRESS
	&& homecity
	&& is_friendly_city_near(punit->owner, src_x, src_y)
	&& !senthome) {
      city_refresh(homecity);
      send_city_info(pplayer, homecity);
    }
  }


  /* The unit block different tiles of adjacent enemy cities before and
     after. Update the relevant cities, without sending their info twice
     if possible... If the city is in the range of both the source and
     the destination we only update it when checking at the destination. */

  /* First check cities near the source. */
  map_city_radius_iterate(src_x, src_y, x1, y1) {
    struct city *pcity = map_get_city(x1, y1);

    int is_near_destination = 0;
    map_city_radius_iterate(dest_x, dest_y, x2, y2) {
      struct city *pcity2 = map_get_city(x2, y2);
      if (pcity == pcity2)
	is_near_destination = 1;
    } map_city_radius_iterate_end;

    if (pcity && pcity->owner != punit->owner
	&& !is_near_destination) {
      if (city_check_workers(pcity, 1)) {
	send_city_info(city_owner(pcity), pcity);
      }
    }
  } map_city_radius_iterate_end;

  /* Then check cities near the destination. */
  map_city_radius_iterate(dest_x, dest_y, x1, y1) {
    struct city *pcity = map_get_city(x1, y1);
    if (pcity && pcity->owner != punit->owner) {
      if (city_check_workers(pcity, 1)) {
	send_city_info(city_owner(pcity), pcity);
      }
    }
  } map_city_radius_iterate_end;
}

/**************************************************************************
Check if the units activity is legal for a move , and reset it if it isn't.
**************************************************************************/
static void check_unit_activity(struct unit *punit)
{
  if (punit->activity != ACTIVITY_IDLE
      && punit->activity != ACTIVITY_GOTO
      && punit->activity != ACTIVITY_SENTRY
      && punit->activity != ACTIVITY_EXPLORE
      && punit->activity != ACTIVITY_PATROL
      && !punit->connecting)
    set_unit_activity(punit, ACTIVITY_IDLE);
}

/**************************************************************************
Moves a unit. No checks whatsoever! This is meant as a practical function
for other functions, like do_airline, which do the checking themselves.
If you move a unit you should always use this function, as it also sets the
transported_by unit field correctly.
take_from_land is only relevant if you have set transport_units.
Note that the src and dest need not be adjacent.
**************************************************************************/
int move_unit(struct unit *punit, int dest_x, int dest_y,
	      int transport_units, int take_from_land, int move_cost)
{
  int src_x = punit->x;
  int src_y = punit->y;
  int playerid = punit->owner;
  struct player *pplayer = get_player(playerid);
  struct tile *psrctile = map_get_tile(src_x, src_y);
  struct tile *pdesttile = map_get_tile(dest_x, dest_y);

  if (!is_real_tile(dest_x, dest_y)) {
    freelog(LOG_ERROR, "Trying to move to non-adjusted tile pos. Trying to adjust...");
    assert(normalize_map_pos(&dest_x, &dest_y));
  }

  conn_list_do_buffer(&pplayer->connections);

  if (punit->ai.ferryboat) {
    struct unit *ferryboat;
    ferryboat = unit_list_find(&psrctile->units, punit->ai.ferryboat);
    if (ferryboat) {
      freelog(LOG_DEBUG, "%d disembarking from ferryboat %d",
	      punit->id, punit->ai.ferryboat);
      ferryboat->ai.passenger = 0;
      punit->ai.ferryboat = 0;
    }
  }
  /* A transporter should not take units with it when on an attack goto -- fisch */
  if ((punit->activity == ACTIVITY_GOTO) &&
      get_defender(pplayer, punit, punit->goto_dest_x, punit->goto_dest_y) &&
      psrctile->terrain != T_OCEAN) {
    transport_units = 0;
  }

  /* A ground unit cannot hold units on an ocean tile */
  if (transport_units
      && is_ground_unit(punit)
      && pdesttile->terrain == T_OCEAN)
    transport_units = 0;

  /* Make sure we don't accidentally insert units into a transporters list */
  unit_list_iterate(pdesttile->units, pcargo) { 
    if (pcargo->transported_by == punit->id)
      pcargo->transported_by = -1;
  } unit_list_iterate_end;

  /* Transporting units. We first make a list of the units to be moved and
     then insert them again. The way this is done makes sure that the
     units stay in the same order. */
  if (get_transporter_capacity(punit) && transport_units) {
    struct unit_list cargo_units;

    /* First make a list of the units to be moved. */
    unit_list_init(&cargo_units);
    assign_units_to_transporter(punit, take_from_land);
    unit_list_iterate(psrctile->units, pcargo) {
      if (pcargo->transported_by == punit->id) {
	unit_list_unlink(&psrctile->units, pcargo);
	unit_list_insert(&cargo_units, pcargo);
      }
    } unit_list_iterate_end;

    /* Insert them again. */
    unit_list_iterate(cargo_units, pcargo) {
      unfog_area(pplayer, dest_x, dest_y, get_unit_type(pcargo->type)->vision_range);
      pcargo->x = dest_x;
      pcargo->y = dest_y;
      unit_list_insert(&pdesttile->units, pcargo);
      check_unit_activity(pcargo);
      send_unit_info_to_onlookers(0, pcargo, src_x, src_y, 1, 0);
      fog_area(pplayer, src_x, src_y, get_unit_type(pcargo->type)->vision_range);
      handle_unit_move_consequences(pcargo, src_x, src_y, dest_x, dest_y);
    } unit_list_iterate_end;
    unit_list_unlink_all(&cargo_units);
  }

  /* We first unfog the destination, then move the unit and send the move,
     and then fog the old territory. This means that the player gets a chance to
     see the newly explored territory while the client moves the unit, and both
     areas are visible during the move */
  unfog_area(pplayer, dest_x, dest_y, get_unit_type(punit->type)->vision_range);

  unit_list_unlink(&psrctile->units, punit);
  punit->x = dest_x;
  punit->y = dest_y;
  punit->moved = 1;
  punit->transported_by = -1;
  punit->moves_left = MAX(0, punit->moves_left - move_cost);
  unit_list_insert(&pdesttile->units, punit);
  check_unit_activity(punit);

  /* set activity to sentry if boarding a ship */
  if (is_ground_unit(punit) &&
      pdesttile->terrain == T_OCEAN &&
      !(pplayer->ai.control)) {
    set_unit_activity(punit, ACTIVITY_SENTRY);
  }
  send_unit_info_to_onlookers(0, punit, src_x, src_y, 0, 0);
  fog_area(pplayer, src_x, src_y, get_unit_type(punit->type)->vision_range);

  handle_unit_move_consequences(punit, src_x, src_y, dest_x, dest_y);

  conn_list_do_unbuffer(&pplayer->connections);

  if (map_get_tile(dest_x, dest_y)->special&S_HUT)
    return handle_unit_enter_hut(punit);
  else
    return 1;
}

/**************************************************************************
Moves a unit according to its pgr (goto or patrol order). If two consequetive
positions in the route is not adjacent it is assumed to be a goto. The unit
is put on idle if a move fails.
If the activity is ACTIVITY_PATROL the map positions are put back in the
route (at the end).  To avoid infinite loops on railroad we stop for this
turn when the unit is back where it started, eben if it have moves left.
**************************************************************************/
void goto_route_execute(struct unit *punit)
{
  struct goto_route *pgr = punit->pgr;
  int index, x, y, res;
  int patrol_stop_index = pgr->last_index;
  int unitid = punit->id;
  struct player *pplayer = unit_owner(punit);

  assert(pgr);
  while (1) {
    freelog(LOG_DEBUG, "running a round\n");

    index = pgr->first_index;
    if (index == pgr->last_index) {
      free(punit->pgr);
      punit->pgr = NULL;
      handle_unit_activity_request(punit, ACTIVITY_IDLE);
      return;
    }
    x = pgr->pos[index].x; y = pgr->pos[index].y;
    freelog(LOG_DEBUG, "%i,%i -> %i,%i\n", punit->x, punit->y, x, y);

    /* Move unit */
    if (is_tiles_adjacent(punit->x, punit->y, x, y)) {
      int last_tile = (index+1)%pgr->length == pgr->last_index;
      freelog(LOG_DEBUG, "handling\n");
      res = handle_unit_move_request(punit, x, y, 0, !last_tile);
      if (!player_find_unit_by_id(pplayer, unitid))
	return;
      if (!res && punit->moves_left) {
	freelog(LOG_DEBUG, "move idling\n");
	handle_unit_activity_request(punit, ACTIVITY_IDLE);
	return;
      }
    } else {
      freelog(LOG_DEBUG, "goto tiles not adjacent; goto cancelled");
      handle_unit_activity_request(punit, ACTIVITY_IDLE);
      return;
    }

    if (!same_pos(x, y, punit->x, punit->y))
      return; /* Ran out of more points */

    pgr->first_index = (pgr->first_index+1) % pgr->length;

    /* When patroling we go in little circles; done by reinserting points */
    if (punit->activity == ACTIVITY_PATROL) {
      pgr->pos[pgr->last_index].x = x;
      pgr->pos[pgr->last_index].y = y;
      pgr->last_index = (pgr->last_index+1) % pgr->length;

      if (patrol_stop_index == pgr->first_index) {
	freelog(LOG_DEBUG, "stopping because we ran a round\n");
	return; /* don't patrol more than one round */
      }
    }
  } /* end while*/
}
