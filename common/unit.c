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
#include <string.h>

#include <game.h>
#include <unit.h>
#include <tech.h>
#include <map.h>
#include <player.h>
#include <log.h>

/* cost ,flags, attack, defense, move, tech_req, vision, transport?, HP, FP, Obsolete, FUEL */

struct unit_type unit_types[U_LAST]={
  {"Settlers",      24, LAND_MOVING, 40,   0,  1,  1*3, A_NONE,        1,  0, 20, 1, U_ENGINEERS, 0, F_SETTLERS | F_NONMIL },
  {"Engineers",     42, LAND_MOVING, 40,   0,  2,  2*3, A_EXPLOSIVES,  1,  0, 20, 1, -1, 0, F_SETTLERS | F_NONMIL },
  {"Warriors",      18, LAND_MOVING, 10,   1,  1,  1*3, A_NONE,        1,  0, 10, 1, U_PIKEMEN, 0, 0},
  {"Phalanx",       21, LAND_MOVING, 20,   1,  2,  1*3, A_BRONZE,      1,  0, 10, 1, U_PIKEMEN, 0, 0},
  {"Archers",       28, LAND_MOVING, 30,   3,  2,  1*3, A_WARRIORCODE, 1,  0, 10, 1, U_MUSKETEERS, 0, 0},
  {"Legion",        16, LAND_MOVING, 40,   4,  2,  1*3, A_IRON,        1,  0, 10, 1, U_MUSKETEERS, 0, 0},
  {"Pikemen",       37, LAND_MOVING, 20,   1,  2,  1*3, A_FEUDALISM,   1,  0, 10, 1, U_MUSKETEERS, 0, F_PIKEMEN},
  {"Musketeers",    19, LAND_MOVING, 30,   3,  3,  1*3, A_GUNPOWDER,   1,  0, 20, 1, U_RIFLEMEN, 0,0 },
  {"Fanatics",      49, LAND_MOVING, 20,   4,  4,  1*3, A_LAST,        1,  0, 20, 1, -1, 0,0},
  {"Partisan",      36, LAND_MOVING, 50,   4,  4,  1*3, A_GUERILLA,    1,  0, 20, 1, -1, 0,F_IGTER | F_IGZOC},
  {"Alpine Troops", 45, LAND_MOVING, 50,   5,  5,  1*3, A_TACTICS,     1,  0, 20, 1, -1, 0,F_IGTER},  
  {"Riflemen",      22, LAND_MOVING, 40,   5,  4,  1*3, A_CONSCRIPTION,1,  0, 20, 1, -1, 0,0},
  {"Marines",       40, LAND_MOVING, 60,   8,  5,  1*3, A_AMPHIBIOUS,  1,  0, 20, 1, -1, 0,F_MARINES},
  {"Paratroopers",   0, LAND_MOVING, 60,   6,  4,  1*3, A_COMBINED,    1,  0, 20, 1, -1, 0,0},
  {"Mech. Inf.",    17, LAND_MOVING, 50,   6,  6,  3*3, A_LABOR,       1,  0, 30, 1, -1, 0,0},
  {"Horsemen",       8, LAND_MOVING, 20,   2,  1,  2*3, A_HORSEBACK,   1,  0, 10, 1, U_KNIGHTS, 0,F_HORSE},
  {"Chariot",        9, LAND_MOVING, 30,   3,  1,  2*3, A_WHEEL,       1,  0, 10, 1, U_KNIGHTS, 0,F_HORSE},
  {"Elephants",     39, LAND_MOVING, 40,   4,  1,  2*3, A_POLYTHEISM,  1,  0, 10, 1, U_CRUSADERS, 0,0}, 
  {"Crusaders",     38, LAND_MOVING, 40,   5,  1,  2*3, A_MONOTHEISM,  1,  0, 10, 1, U_DRAGOONS, 0,F_HORSE},   
  {"Knights",       15, LAND_MOVING, 40,   4,  2,  2*3, A_CHIVALRY,    1,  0, 10, 1, U_DRAGOONS, 0,F_HORSE},
  {"Dragoons",      32, LAND_MOVING, 50,   5,  2,  2*3, A_LEADERSHIP,  1,  0, 20, 1, U_CAVALRY, 0,F_HORSE},
  {"Cavalry",       29, LAND_MOVING, 60,   8,  3,  2*3, A_TACTICS,     1,  0, 20, 1, U_ARMOR, 0,0},           /* IS NOT a normal horse attack */
  {"Armor",          0, LAND_MOVING, 80,  10,  5,  3*3, A_MOBILE,      1,  0, 30, 1, -1, 0,0},
  {"Catapult",       7, LAND_MOVING, 40,   6,  1,  1*3, A_MATHEMATICS, 1,  0, 10, 1, U_CANNON, 0,0},
  {"Cannon",         4, LAND_MOVING, 40,   8,  1,  1*3, A_METALLURGY,  1,  0, 20, 1, U_ARTILLERY, 0,0},
  {"Artillery",     43, LAND_MOVING, 50,  10,  1,  1*3, A_MACHINE,     1,  0, 20, 2, U_HOWITZER, 0,0},
  {"Howitzer",       1, LAND_MOVING, 70,  12,  2,  2*3, A_ROBOTICS,    1,  0, 30, 2, -1, 0,F_IGWALL},
  {"Fighter",       12, AIR_MOVING,  60,   4,  3, 10*3, A_FLIGHT,      2,  0, 20, 2, U_SFIGHTER, 1,F_FIGHTER  | F_FIELDUNIT},
  {"Bomber",         3, AIR_MOVING, 120,  12,  1,  8*3, A_ADVANCED,    2,  0, 20, 2, U_SBOMBER, 2,F_FIELDUNIT|F_ONEATTACK},
  {"Helicopter",    44, HELI_MOVING,100,  10,  3,  6*3, A_COMBINED,    2,  0, 20, 2, -1, 0,F_ONEATTACK |F_FIELDUNIT},
  {"Stealth Fighter",47,AIR_MOVING,  80,   8,  4, 14*3, A_STEALTH,     2,  0, 20, 2, -1, 1,F_FIELDUNIT| F_FIGHTER},
  {"Stealth Bomber",46, AIR_MOVING, 160,  14,  5, 12*3, A_STEALTH,     2,  0, 20, 2, -1, 2,F_FIELDUNIT|F_ONEATTACK},
  {"Trireme",       27, SEA_MOVING,  40,   1,  1,  3*3, A_MAPMAKING,   1,  2, 10, 1, U_CARAVEL, 0,F_FIELDUNIT},
  {"Caravel",       23, SEA_MOVING,  40,   2,  1,  3*3, A_NAVIGATION,  1,  3, 10, 1, U_GALLEON, 0,0},
  {"Galleon",       35, SEA_MOVING,  40,   0,  2,  4*3, A_MAGNETISM,   1,  4, 20, 1, U_TRANSPORT, 0,0},
  {"Frigate",       13, SEA_MOVING,  50,   4,  2,  4*3, A_MAGNETISM,   1,  2, 20, 1, U_IRONCLAD, 0,F_FIELDUNIT},
  {"Ironclad",      14, SEA_MOVING,  60,   4,  4,  4*3, A_STEAM,       1,  0, 30, 1, U_DESTROYER, 0,F_FIELDUNIT},
  {"Destroyer",     31, SEA_MOVING,  60,   4,  4,  6*3, A_ELECTRICITY, 2,  0, 30, 1, -1, 0,F_FIELDUNIT},
  {"Cruiser",       10, SEA_MOVING,  80,   6,  6,  5*3, A_STEEL,       2,  0, 30, 2, U_AEGIS, 0,F_FIELDUNIT},
  {"AEGIS Cruiser", 48, SEA_MOVING, 100,   8,  8,  5*3, A_ROCKETRY,    2,  0, 30, 2, -1, 0,F_AEGIS | F_FIELDUNIT},
  {"Battleship",     2, SEA_MOVING, 160,  12, 12,  4*3, A_AUTOMOBILE,  2,  0, 40, 2, -1, 0,F_FIELDUNIT},
  {"Submarine",     25, SEA_MOVING,  60,  10,  2,  3*3, A_COMBUSTION,  2,  8, 30, 2, -1, 0,F_FIELDUNIT|F_SUBMARINE},
  {"Carrier",        6, SEA_MOVING, 160,   1,  9,  5*3, A_ADVANCED,    2,  8, 40, 2, -1, 0,F_FIELDUNIT|F_CARRIER},
  {"Transport",     26, SEA_MOVING,  50,   0,  3,  5*3, A_INDUSTRIALIZATION,2, 8, 30, 1, -1, 0,0},
  {"Cruise Missile",30, AIR_MOVING,  60,  18,  0, 12*3, A_ROCKETRY,    1,  0, 10, 3, -1, 1,F_FIELDUNIT | F_MISSILE | F_ONEATTACK},
  {"Nuclear",       20, AIR_MOVING, 160,  99,  0, 16*3, A_ROCKETRY,    1,  0, 10, 1, -1, 1,F_FIELDUNIT | F_ONEATTACK | F_MISSILE},
  {"Diplomat",      11, LAND_MOVING, 30,   0,  0,  2*3, A_WRITING,     1,  0, 10, 1, U_SPY, 0,F_DIPLOMAT | F_IGZOC | F_NONMIL},
  {"Spy",           41, LAND_MOVING, 30,   0,  0,  3*3, A_ESPIONAGE,   2,  0, 10, 1, -1, 0,F_DIPLOMAT | F_IGZOC | F_NONMIL},
  {"Caravan",        5, LAND_MOVING, 50,   0,  1,  1*3, A_TRADE,       1,  0, 10, 1, U_FREIGHT, 0,F_CARAVAN | F_IGZOC | F_NONMIL},
  {"Freight",       34, LAND_MOVING, 50,   0,  1,  2*3, A_CORPORATION, 1,  0, 10, 1, -1, 0,F_CARAVAN | F_IGZOC | F_NONMIL},
  {"Explorer",      33, LAND_MOVING, 30,   0,  1,  1*3, A_SEAFARING,   1,  0, 10, 1, U_PARTISAN, 0,F_NONMIL | F_IGTER | F_IGZOC}

};


/***************************************************************
...
***************************************************************/
char *get_unit_name(enum unit_type_id id)
{
  struct unit_type *ptype;
  static char buffer[256];
  ptype =get_unit_type(id);
  if (ptype->fuel)
    sprintf(buffer,"%s [%d/%d/%d(%d)]", ptype->name, ptype->attack_strength,
	    ptype->defense_strength,
	    ptype->move_rate/3,(ptype->move_rate/3)*ptype->fuel);
  else
    sprintf(buffer,"%s [%d/%d/%d]", ptype->name, ptype->attack_strength,
	    ptype->defense_strength, ptype->move_rate/3);
  return buffer;
}

struct unit *find_unit_by_id(int id)
{
  int i;

  for(i=0; i<game.nplayers; i++) {
    struct unit *punit;
    if((punit=unit_list_find(&game.players[i].units, id)))
      return punit;
  }
  return 0;
}


int unit_move_rate(struct unit *punit)
{
  int val;
  struct player *pplayer;
  pplayer = &game.players[punit->owner];
  val = get_unit_type(punit->type)->move_rate;
  if (!is_air_unit(punit) && !is_heli_unit(punit)) 
    val = (val * punit->hp) / get_unit_type(punit->type)->hp;
  if(is_sailing_unit(punit)) {
    struct city *pcity;
    
    pcity=city_list_find_id(&pplayer->cities, 
			    game.global_wonders[B_LIGHTHOUSE]);
    if(pcity && !wonder_obsolete(B_LIGHTHOUSE)) 
      val+=3;
    
    pcity=city_list_find_id(&pplayer->cities, 
			    game.global_wonders[B_MAGELLAN]);
    if(pcity && !wonder_obsolete(B_MAGELLAN)) 
      val+=3;
    if (get_invention(pplayer, A_POWER) == TECH_KNOWN)
      val+=3;
    if (val < 6) 
      val = 6;
  }
  if (val < 3) val = 3;
  return val;
}

char *string_center(const char *s, int size)
{
  int left;
  static char buf[100];
  left = (size -strlen(s))/2;
  sprintf(buf, "%*s%s%*s", left, "", s, size - strlen(s) - left, "");
  return buf;
}

/**************************************************************************
bribe unit
investigate
poison
make revolt
establish embassy
sabotage city
**************************************************************************/

/**************************************************************************
...
**************************************************************************/
int diplomat_can_do_action(struct unit *pdiplomat,
			   enum diplomat_actions action, 
			   int destx, int desty)
{
  struct city *pcity=map_get_city(destx, desty);
  struct tile *ptile=map_get_tile(destx, desty);
  
  if(is_tiles_adjacent(pdiplomat->x, pdiplomat->y, destx, desty)) {
    if(pcity) {  
      if(pcity->owner!=pdiplomat->owner) {
	if(action==DIPLOMAT_SABOTAGE)
	  return 1;
        if(action==DIPLOMAT_EMBASSY && 
	   !player_has_embassy(&game.players[pdiplomat->owner], 
			       &game.players[pcity->owner]))
	   return 1;
	if(action==DIPLOMAT_STEAL)
	  return 1;
	if(action==DIPLOMAT_INCITE)
	  return 1;
      }
    }
    else {
      if(action==DIPLOMAT_BRIBE && unit_list_size(&ptile->units)==1 &&
	 unit_list_get(&ptile->units, 0)->owner!=pdiplomat->owner)
	return 1;
    }
  }
  return 0;
}

/**************************************************************************
...
**************************************************************************/
int unit_can_airlift_to(struct unit *punit, struct city *pcity)
{
  struct city *city1;

  if(!punit->moves_left)
    return 0;
  if(!(city1=map_get_city(punit->x, punit->y))) 
    return 0;
  if(city1==pcity)
    return 0;
  if(city1->owner != pcity->owner) 
    return 0;
  if (city1->airlift + pcity->airlift < 2) 
    return 0;

  return 1;
}

/**************************************************************************
...
**************************************************************************/
int unit_can_help_build_wonder(struct unit *punit, struct city *pcity)
{
  return is_tiles_adjacent(punit->x, punit->y, pcity->x, pcity->y) &&
    punit->owner==pcity->owner && !pcity->is_building_unit  && 
    is_wonder(pcity->currently_building);
}


/**************************************************************************
...
**************************************************************************/
int unit_can_defend_here(struct unit *punit)
{
  if(is_ground_unit(punit) && map_get_terrain(punit->x, punit->y)==T_OCEAN)
    return 0;
  
  return 1;
}

/**************************************************************************
...
**************************************************************************/
int is_transporter_with_free_space(struct player *pplayer, int x, int y)
{
  int none_transporters, total_capacity=0;
  none_transporters=0;
  total_capacity=0;
  unit_list_iterate(map_get_tile(x, y)->units, punit) {
    if(get_transporter_capacity(punit) && !unit_flag(punit->type, F_SUBMARINE| F_CARRIER))
      total_capacity+=get_transporter_capacity(punit);
    else
      none_transporters++;
  }
  unit_list_iterate_end;
  
  return total_capacity>none_transporters;
}


/**************************************************************************
 can't use the unit_list_iterate macro here
**************************************************************************/
void transporter_cargo_to_unitlist(struct unit *ptran, struct unit_list *list)
{
  struct genlist_iterator myiter;
  struct unit_list *srclist;
  int cargo;
  
  unit_list_init(list);
    
  srclist=&map_get_tile(ptran->x, ptran->y)->units;
  
  genlist_iterator_init(&myiter, &srclist->list, 0);
  
  cargo=get_transporter_capacity(ptran);
  
  for(; cargo && ITERATOR_PTR(myiter);) {
    struct unit *punit=(struct unit *)ITERATOR_PTR(myiter);
    ITERATOR_NEXT(myiter);
    if(((is_ground_unit(punit) &&!unit_flag(ptran->type, F_SUBMARINE | F_CARRIER)) || 
        (is_air_unit(punit) && unit_flag(ptran->type, F_CARRIER)) ||
	(unit_flag(punit->type, F_MISSILE) && 
	 unit_flag(ptran->type, F_SUBMARINE))) &&
       (!map_get_city(punit->x, punit->y) || 
	punit->activity==ACTIVITY_SENTRY)) {
      unit_list_unlink(srclist, punit);
      unit_list_insert(list, punit);
      cargo--;
    }
  }

}

/**************************************************************************
...
**************************************************************************/
void move_unit_list_to_tile(struct unit_list *units, int x, int y)
{
  struct genlist_iterator myiter;
  
  genlist_iterator_init(&myiter, &units->list, 0);

  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter)) {
    struct unit *punit=(struct unit *)ITERATOR_PTR(myiter);
    punit->x=x;
    punit->y=y;
    unit_list_insert_back(&map_get_tile(x, y)->units, punit);
  }
}

/**************************************************************************
...
**************************************************************************/
int get_transporter_capacity(struct unit *punit)
{
  return unit_types[punit->type].transport_capacity;
}

/**************************************************************************
...
**************************************************************************/
int is_sailing_unit(struct unit *punit)
{
  return (unit_types[punit->type].move_type == SEA_MOVING);
}

/**************************************************************************
...
**************************************************************************/
int is_air_unit(struct unit *punit)
{
  return (unit_types[punit->type].move_type == AIR_MOVING);
}

/**************************************************************************
...
**************************************************************************/
int is_heli_unit(struct unit *punit)
{
  return (unit_types[punit->type].move_type == HELI_MOVING);
}

/**************************************************************************
...
**************************************************************************/
int is_ground_unit(struct unit *punit)
{
  return (unit_types[punit->type].move_type == LAND_MOVING);
}
/**************************************************************************
...
**************************************************************************/
int is_ground_unittype(enum unit_type_id id)
{
  return (unit_types[id].move_type == LAND_MOVING);
}

/**************************************************************************
...
**************************************************************************/
int is_air_unittype(enum unit_type_id id)
{
  return (unit_types[id].move_type == AIR_MOVING);
}

/**************************************************************************
...
**************************************************************************/
int is_heli_unittype(enum unit_type_id id)
{
  return (unit_types[id].move_type == HELI_MOVING);
}

/**************************************************************************
...
**************************************************************************/
int is_water_unit(enum unit_type_id id)
{
  return (unit_types[id].move_type == SEA_MOVING);
}

/**************************************************************************
...
**************************************************************************/
int is_military_unit(struct unit *this_unit)
{
  return (unit_flag(this_unit->type, F_NONMIL) == 0);
}

/**************************************************************************
...
**************************************************************************/
int is_field_unit(struct unit *punit)
{
  return ((unit_flag(punit->type, F_FIELDUNIT) > 0));

}

int unit_flag(enum unit_type_id id, int flag)
{
  return unit_types[id].flags & flag;
}

/**************************************************************************
...
**************************************************************************/

int unit_value(enum unit_type_id id)
{
  return (unit_types[id].build_cost);
}


/**************************************************************************
...
**************************************************************************/
char *unit_name(enum unit_type_id id)
{
  return (unit_types[id].name);
}

struct unit_type *get_unit_type(enum unit_type_id id)
{
  return &unit_types[id];
}


/**************************************************************************
...
**************************************************************************/
void raise_unit_top(struct unit *punit)
{
  struct tile *ptile;

  ptile=map_get_tile(punit->x, punit->y);

  unit_list_unlink(&ptile->units, punit);
  unit_list_insert(&ptile->units, punit);

  unit_list_get(&ptile->units, 0);
}

/**************************************************************************
...
**************************************************************************/
int can_unit_build_city(struct unit *punit)
{
  if(!unit_flag(punit->type, F_SETTLERS))
    return 0;

  if(map_get_city(punit->x, punit->y))
    return 0;
    
  if(map_get_terrain(punit->x, punit->y)==T_OCEAN)
    return 0;

  return 1;
}

/**************************************************************************
...
**************************************************************************/
int can_unit_change_homecity(struct unit *punit)
{
  struct city *pcity=map_get_city(punit->x, punit->y);
  return pcity && pcity->owner==punit->owner;
}

/**************************************************************************
...
**************************************************************************/
int can_upgrade_unittype(struct player *pplayer, enum unit_type_id id)
{
  if (get_invention(pplayer, 
		    unit_types[id].tech_requirement)!=TECH_KNOWN)
    return -1;
  if (unit_types[id].obsoleted_by == -1)
    return -1;
  if (get_invention(pplayer,
		    unit_types[unit_types[id].obsoleted_by].tech_requirement)!=TECH_KNOWN) 
    return -1;
  while (unit_types[id].obsoleted_by!=-1 && get_invention(pplayer, 
		       unit_types[unit_types[id].obsoleted_by].tech_requirement)==TECH_KNOWN) {
    id = unit_types[id].obsoleted_by;
  }
  return id;
}

int unit_upgrade_price(struct player *pplayer, enum unit_type_id from, enum unit_type_id to)
{
  int total, build;
  build = unit_value(from)/2;
  total = unit_value(to);
  if (build>=total)
    return 0;
  return (total-build)*2+(total-build)*(total-build)/20; 
}

/**************************************************************************
...
**************************************************************************/
int can_unit_do_activity(struct unit *punit, enum unit_activity activity)
{
  struct tile *ptile;
  struct tile_type *type;
  struct player *pplayer;
  pplayer = &game.players[punit->owner];
  
  ptile=map_get_tile(punit->x, punit->y);
  type=get_tile_type(ptile->terrain);
  
  if(activity==ACTIVITY_IDLE)
    return 1;

  if(punit->activity==ACTIVITY_IDLE) {
    if(activity==ACTIVITY_FORTIFY) { 
      return (is_ground_unit(punit));
    }
    if(activity==ACTIVITY_SENTRY)
      return 1;

    if(activity==ACTIVITY_PILLAGE) {
      if (is_ground_unit(punit) && (!is_military_unit(punit)))
        if((ptile->special&S_ROAD) || (ptile->special&S_RAILROAD) ||
	   (ptile->special&S_IRRIGATION) || (ptile->special&S_MINE))
	   if(!is_unit_activity_on_tile(ACTIVITY_PILLAGE, punit->x, punit->y))
	     return 1;
       return 0;
    }

    if(activity==ACTIVITY_FORTRESS) {
      if(unit_flag(punit->type, F_SETTLERS) && get_invention(pplayer, A_CONSTRUCTION)== TECH_KNOWN)
        if(!(ptile->special&S_FORTRESS) && ptile->terrain!=T_OCEAN)
          if(!is_unit_activity_on_tile(ACTIVITY_FORTRESS, punit->x, punit->y))
            return 1;
      return 0;
    }
    
    if(activity==ACTIVITY_POLLUTION) {
      if (unit_flag(punit->type, F_SETTLERS))
	if(ptile->special&S_POLLUTION)
	  if(!is_unit_activity_on_tile(ACTIVITY_POLLUTION, punit->x, punit->y))
	    return 1;
      return 0;
    }
    
    if(activity==ACTIVITY_IRRIGATE) {
      if(unit_flag(punit->type, F_SETTLERS))
	if((ptile->terrain==type->irrigation_result &&
	    !(ptile->special&S_IRRIGATION) && 
	    is_water_adjacent_to_tile(punit->x, punit->y)) ||
	   (ptile->terrain!=type->irrigation_result &&
	    type->irrigation_result!=T_LAST))
	  if(!is_unit_activity_on_tile(ACTIVITY_IRRIGATE, punit->x, punit->y))
	    return 1;
      return 0;
    }
    
    if(activity==ACTIVITY_ROAD) {
      if(unit_flag(punit->type, F_SETTLERS) && ptile->terrain!=T_OCEAN && 
	 (ptile->terrain!=T_RIVER || 
	  get_invention(pplayer, A_BRIDGE)==TECH_KNOWN)) {
	if(!(ptile->special&S_ROAD))
	  if(!is_unit_activity_on_tile(ACTIVITY_ROAD, punit->x, punit->y))
	    return 1;
      }
      return 0;
    }
    
    if(activity==ACTIVITY_RAILROAD) {
      if(unit_flag(punit->type, F_SETTLERS) && ptile->terrain!=T_OCEAN && 
	 (ptile->terrain!=T_RIVER || 
	  get_invention(&game.players[punit->owner], A_BRIDGE)==TECH_KNOWN)) {
	if((ptile->special&S_ROAD) && !(ptile->special&S_RAILROAD)) {
	  if(get_invention(&game.players[punit->owner], A_RAILROAD)==
	     TECH_KNOWN && 
	     !is_unit_activity_on_tile(ACTIVITY_RAILROAD, punit->x, punit->y))
	    return 1;
	}
      }
      return 0;
    }

    if(activity==ACTIVITY_MINE) {
      if(unit_flag(punit->type, F_SETTLERS))
	if(type->mining_result!=T_LAST && !(ptile->special&S_MINE))
	  if(!is_unit_activity_on_tile(ACTIVITY_MINE, punit->x, punit->y))
	    return 1;
      return 0;
    }

  }
  
  return 0;
}
/**************************************************************************
...
**************************************************************************/
int is_unit_activity_on_tile(enum unit_activity activity, int x, int y)
{
  unit_list_iterate(map_get_tile(x, y)->units, punit) 
    if(punit->activity==activity)
      return 1;
  unit_list_iterate_end;
  return 0;
}

/**************************************************************************
 ...
**************************************************************************/
char *unit_description(struct unit *punit)
{
  struct city *pcity;
  static char buffer[512];

  pcity=city_list_find_id(&game.player_ptr->cities, punit->homecity);

  sprintf(buffer, "%s\n%s\n%s", 
	  get_unit_type(punit->type)->name, 
	  unit_activity_text(punit), 
	  pcity ? pcity->name : "");

  return buffer;
}

/**************************************************************************
 ...
**************************************************************************/
char *unit_activity_text(struct unit *punit)
{
  static char text[64];
   
  switch(punit->activity) {
   case ACTIVITY_IDLE:
     if(is_air_unit(punit)) {
       int rate,f;
       rate=get_unit_type(punit->type)->move_rate/3;
       f=((punit->fuel)-1);
       if(punit->moves_left%3) {
        if(punit->moves_left/3>0)
          sprintf(text, "Moves: (%d)%d %d/3",
                  ((rate*f)+(punit->moves_left/3)),
                  punit->moves_left/3, punit->moves_left%3);
        else
          sprintf(text, "Moves: (%d)%d/3",
                  ((rate*f)+(punit->moves_left/3)),
                  punit->moves_left%3);
       }
       else
        sprintf(text, "Moves: (%d)%d", rate*f+punit->moves_left/3,
                punit->moves_left/3);
     }
     else {
       if(punit->moves_left%3) {
        if(punit->moves_left/3>0)
          sprintf(text, "Moves: %d %d/3", punit->moves_left/3, 
                  punit->moves_left%3);
        else
          sprintf(text, "Moves: %d/3", punit->moves_left%3);
       }
       else
        sprintf(text, "Moves: %d", punit->moves_left/3);
     }
     return text;
   case ACTIVITY_POLLUTION:
    return "Pollution";
   case ACTIVITY_ROAD:
    return "Road";
   case ACTIVITY_RAILROAD:
    return "Railroad";
   case ACTIVITY_MINE: 
    return "Mine";
    case ACTIVITY_IRRIGATE:
    return "Irrigation";
   case ACTIVITY_FORTIFY:
    return "Fortify";
   case ACTIVITY_FORTRESS:
    return "Fortress";
   case ACTIVITY_SENTRY:
    return "Sentry";
   case ACTIVITY_PILLAGE:
    return "Pillage";
   case ACTIVITY_GOTO:
    return "Goto";
   default:
    log(LOG_FATAL, "Unknown unit activity:%d in unit_activity_text()");
    exit(0);
  }
  return 0;
}

/**************************************************************************
...
**************************************************************************/
void unit_list_init(struct unit_list *This)
{
  genlist_init(&This->list);
}

/**************************************************************************
...
**************************************************************************/
struct unit *unit_list_get(struct unit_list *This, int index)
{
  return (struct unit *)genlist_get(&This->list, index);
}

/**************************************************************************
...
**************************************************************************/
struct unit *unit_list_find(struct unit_list *This, int id)
{
  struct genlist_iterator myiter;

  genlist_iterator_init(&myiter, &This->list, 0);

  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter))
    if(((struct unit *)ITERATOR_PTR(myiter))->id==id)
      return ITERATOR_PTR(myiter);

  return 0;
}

/**************************************************************************
...
**************************************************************************/
void unit_list_insert(struct unit_list *This, struct unit *punit)
{
  genlist_insert(&This->list, punit, 0);
}

/**************************************************************************
...
**************************************************************************/
void unit_list_insert_back(struct unit_list *This, struct unit *punit)
{
  genlist_insert(&This->list, punit, -1);
}


/**************************************************************************
...
**************************************************************************/
int unit_list_size(struct unit_list *This)
{
  return genlist_size(&This->list);
}

/**************************************************************************
...
**************************************************************************/
void unit_list_unlink(struct unit_list *This, struct unit *punit)
{
  genlist_unlink(&This->list, punit);
}
