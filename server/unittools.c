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
#include <unittools.h>
#include <unitfunc.h>

/**************************************************************************
  unit can be moved if:
  1) the unit is idle or on goto
  2) the target location is on the map
  3) the target location is next to the unit
  4) ground unit can only move to ocean squares if there is a transporter
     with free capacity
  5) marines are the only units that can attack from a ocean square
  6) naval units can only be moved to ocean squares or city squares
  7) if there is no enemy units blocking (zoc)
**************************************************************************/
int can_unit_move_to_tile(struct unit *punit, int x, int y)
{
  struct tile *ptile,*ptile2;
  int zoc;

  if(punit->activity!=ACTIVITY_IDLE && punit->activity!=ACTIVITY_GOTO)
    return 0;
  
  if(x<0 || x>=map.xsize || y<0 || y>=map.ysize)
    return 0;
  
  if(!is_tiles_adjacent(punit->x, punit->y, x, y))
    return 0;

  if(is_enemy_unit_tile(x,y,punit->owner))
    return 0;

  ptile=map_get_tile(x, y);
  ptile2=map_get_tile(punit->x, punit->y);
  if(is_ground_unit(punit)) {
    /* Check condition 4 */
    if(ptile->terrain==T_OCEAN &&
       !is_transporter_with_free_space(&game.players[punit->owner], x, y))
	return 0;

    /* Moving from ocean */
    if(ptile2->terrain==T_OCEAN) {
      /* Can't attack a city from ocean unless marines */
      if(!unit_flag(punit->type, F_MARINES) && is_enemy_city_tile(x,y,punit->owner)) {
        notify_player_ex(&game.players[punit->owner], punit->x, punit->y, E_NOEVENT, "Game: Only Marines can attack from sea.");
	return 0;
      }
    }
  } else if(is_sailing_unit(punit)) {
    if(ptile->terrain!=T_OCEAN && ptile->terrain!=T_UNKNOWN)
      if(!is_friendly_city_tile(x,y,punit->owner))
	return 0;
  } 
  zoc = zoc_ok_move(punit, x, y);
  if (!zoc) 
    notify_player_ex(&game.players[punit->owner], punit->x, punit->y, E_NOEVENT,
    "Game: %s can only move into your own zone of control.", unit_types[punit->type].name);
  return zoc;
}

/**************************************************************************
 is there an enemy city on this tile?
**************************************************************************/
int is_enemy_city_tile(int x, int y, int owner)
{
    struct city *pcity;

    pcity=map_get_city(x,y);

    if(pcity==NULL) return 0;

    return(pcity->owner != owner);
}

/**************************************************************************
 is there an enemy unit on this tile?
**************************************************************************/
int is_enemy_unit_tile(int x, int y, int owner)
{
    struct unit_list *punit_list;
    struct unit *punit;

    punit_list=&map_get_tile(x, y)->units;
    punit = unit_list_get(punit_list, 0);

    if(!punit) return 0;
    return(punit->owner != owner);
}

/**************************************************************************
 is there an friendly unit on this tile?
**************************************************************************/
int is_friendly_unit_tile(int x, int y, int owner)
{
    struct unit_list *punit_list;
    struct unit *punit;

    punit_list=&map_get_tile(x, y)->units;
    punit = unit_list_get(punit_list, 0);

    if(!punit) return 0;
    return(punit->owner == owner);
}

/**************************************************************************
 is there an friendly city on this tile?
**************************************************************************/
int is_friendly_city_tile(int x, int y, int owner)
{
    struct city *pcity;

    pcity=map_get_city(x,y);

    if(pcity==NULL) return 0;

    return(pcity->owner == owner);
}

/**************************************************************************
  is this square controlled by the units owner
**************************************************************************/
int is_my_zoc(struct unit *myunit, int x0, int y0)
{
  int x,y;
  int ax,ay;
  int owner=myunit->owner;

  for (x=x0-1;x<x0+2;x++) for (y=y0-1;y<y0+2;y++) {
    ax=map_adjust_x(x); ay=map_adjust_y(y);
    if (is_enemy_city_tile(ax,ay,owner))
      return 0;
    if (is_enemy_unit_tile(ax,ay,owner))
      if((is_sailing_unit(myunit) && (map_get_terrain(ax,ay)==T_OCEAN)) ||
	 (!is_sailing_unit(myunit) && (map_get_terrain(ax,ay)!=T_OCEAN)) )
        return 0;
  }
  return 1;
}

/**************************************************************************
  return whether or not the square the unit wants to enter is blocked by
  enemy units? 
  You CAN move if:
  1. You have units there already
  2. Your unit isn't a ground unit
  3. Your unit ignores ZOC (diplomat, freight, etc.)
  4. You're moving from or to a city
  5. The spot you're moving from or to is in your ZOC
**************************************************************************/
int zoc_ok_move(struct unit *punit,int x, int y)
{
  struct unit_list *punit_list;

  punit_list=&map_get_tile(x, y)->units;
  /* if you have units there, you can move */
  if (is_friendly_unit_tile(x,y,punit->owner))
    return 1;
  if (!is_ground_unit(punit) || unit_flag(punit->type, F_IGZOC))
    return 1;
  if (map_get_city(x,y) || map_get_city(punit->x, punit->y))
    return 1;
  return(is_my_zoc(punit, punit->x, punit->y) || 
         is_my_zoc(punit, x, y)); 
}

/**************************************************************************
 calculate how expensive it is to bribe the unit
 depends on distance to the capital, and government form
 settlers are half price
**************************************************************************/
int unit_bribe_cost(struct unit *punit)
{  
  int cost;
  struct city *capital;
  int dist;

  cost = (&game.players[punit->owner])->economic.gold+750;
  capital=find_palace(&game.players[punit->owner]);
  if (capital)
    dist=min(32, map_distance(capital->x, capital->y, punit->x, punit->y)); 
  else
    dist=32;
    if (get_government(punit->owner)==G_COMMUNISM)
      dist = min(10, dist);
  cost=(cost/(dist+2))*(get_unit_type(punit->type)->build_cost/10);
  if (unit_flag(punit->type, F_SETTLERS)) 
    cost/=2;
  return cost;
}

/**************************************************************************
 return whether or not there is a diplomat on this square
**************************************************************************/
int diplomat_on_tile(int x, int y)
{
  unit_list_iterate(map_get_tile(x, y)->units, punit)
    if (unit_flag(punit->type, F_DIPLOMAT))
      return 1;
  unit_list_iterate_end;
  return 0;
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
    if ((city_got_barracks(pcity) && is_ground_unit(punit)) ||
	(city_got_building(pcity, B_AIRPORT) && is_air_unit(punit)) || 
	(city_got_building(pcity, B_AIRPORT) && is_heli_unit(punit)) || 
	(city_got_building(pcity, B_PORT) && is_sailing_unit(punit))) {
      hp=get_unit_type(punit->type)->hp;
    }
    else
      hp=get_unit_type(punit->type)->hp/3;
  }
  else if (is_heli_unit(punit))
    hp-=(get_unit_type(punit->type)->hp/10);
  else
    hp++;

  if(punit->activity==ACTIVITY_FORTIFY)
    hp++;
  
  return hp;
}

/**************************************************************************
  this is a crude function!!!! 
  used to find the best defensive unit on a square
**************************************************************************/
int rate_unit(struct unit *punit, struct unit *against)
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
  struct unit *bestdef = 0;
  int bestvalue=-1;
  unit_list_iterate(map_get_tile(x, y)->units, punit) {
    if (pplayer->player_no==punit->owner)
      return 0;
    if(unit_can_defend_here(punit) && rate_unit(punit, aunit)>bestvalue) {
      bestvalue=rate_unit(punit, aunit);
      bestdef=punit;
    }
  }
  unit_list_iterate_end;
  return bestdef;
}

/**************************************************************************
 returns the attack power, modified by moves left, and veteran status.
**************************************************************************/
int get_attack_power(struct unit *punit)
{
  int power;
  power=get_unit_type(punit->type)->attack_strength*10;
  if (punit->veteran)
    power*=1.5;
  if (punit->moves_left==1)
    return power/3;
  if (punit->moves_left==2)
    return (power*2)/3;
  return power;
}

/**************************************************************************
  returns the defense power, modified by terrain and veteran status
**************************************************************************/
int get_defense_power(struct unit *punit)
{
  int power;
  int terra;
  if (!punit || punit->type<0 || punit->type>=U_LAST)
    abort();
  power=get_unit_type(punit->type)->defense_strength*10;
  if (punit->veteran)
    power*=1.5;
  
  terra=map_get_terrain(punit->x, punit->y);
  power=(power*get_tile_type(terra)->defense_bonus)/10;
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
  returns a unit type for the goodie huts
**************************************************************************/
int find_a_unit_type()
 {
  int num;
  
  num = 2;
  if (game.global_advances[A_CHIVALRY])
    num = 3;
  if (game.global_advances[A_GUNPOWDER]) 
    num = 4;

  switch (myrand(num)) {
  case 0:
    return U_HORSEMEN;
  case 1:
    return U_LEGION;
  case 2:
    return U_CHARIOT;
  case 3:
    return U_KNIGHTS;
  case 4:
    return U_MUSKETEERS;
  default:
    return U_HORSEMEN;
  }
}

/**************************************************************************
  unit can't attack if :
 1) it don't have any attack power
 2) it's not a fighter and the defender is a flying unit
 3) if it's not a marine (and ground unit) and it attacks from ocean
 4) a ground unit can't attack a ship on an ocean square
**************************************************************************/
int can_unit_attack_unit_at_tile(struct unit *punit, struct unit *pdefender, int dest_x, int dest_y)
{
  int fromtile=map_get_terrain(punit->x, punit->y);
  int totile=map_get_terrain(dest_x, dest_y);

  if(!is_military_unit(punit))
    return 0;

  /*only fighters can attack planes, except for city attacks */
  if (!unit_flag(punit->type, F_FIGHTER) && is_air_unit(pdefender) && !map_get_city(dest_x, dest_y)) {
    return 0;
  }
  /* can't attack with ground unit from ocean */
  if(fromtile==T_OCEAN && is_ground_unit(punit) && !unit_flag(punit->type, F_MARINES)) {
    return 0;
  }

  if(totile==T_OCEAN && is_ground_unit(punit)) {
    return 0;
  }

  if (unit_flag(punit->type, F_SUBMARINE) && totile!=T_OCEAN)  {
    return 0;
  }
  
  /* Shore bombardement */
  if (fromtile==T_OCEAN && is_sailing_unit(punit) && totile!=T_OCEAN)
    return (get_attack_power(punit)>0);
  return 1;
}


int can_unit_attack_tile(struct unit *punit, int dest_x, int dest_y)
{
  struct unit *pdefender;
  pdefender=get_defender(&game.players[punit->owner], punit, dest_x, dest_y);
  return(can_unit_attack_unit_at_tile(punit, pdefender, dest_x, dest_y));
}

/**************************************************************************
  calculate the remaining build points 
**************************************************************************/
int build_points_left(struct city *pcity)
{
 return (improvement_value(pcity->currently_building) - pcity->shield_stock);
}

/**************************************************************************
  return if it's possible to place a partisan on this square 
  possible if:
  1) square isn't a city square
  2) there is no units on this square
  3) it's not an ocean square 
**************************************************************************/
int can_place_partisan(int x, int y) 
{
  struct tile *ptile;
  ptile = map_get_tile(x, y);
  return (!map_get_city(x, y) && !unit_list_size(&ptile->units) && map_get_terrain(x,y) != T_OCEAN && map_get_terrain(x, y) < T_UNKNOWN); 
}

int enemies_at(struct unit *punit, int x, int y)
{
  int i, j, a = 0, d;
  for (j = y - 1; j <= y + 1; j++) {
    if (j < 0 || j >= map.ysize) continue;
    for (i = x - 1; i <= x + 1; i++) {
      if (is_enemy_city_tile(i, j, punit->owner)) return 1;
      unit_list_iterate(map_get_tile(i, j)->units, enemy)
        if (enemy->owner != punit->owner &&
            can_unit_attack_unit_at_tile(enemy, punit, x, y))
          a += unit_belligerence_basic(enemy);
      unit_list_iterate_end;
    }
  }
  d = unit_vulnerability_virtual(punit) *
      get_tile_type(map_get_terrain(x, y))->defense_bonus;
  return((a * a * 10) >= d); /* as good a quick'n'dirty should be -- Syela */
}
