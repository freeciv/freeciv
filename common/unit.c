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
#include <stdlib.h>		/* malloc,free */
#include <string.h>
#include <assert.h>

#include <game.h>
#include <unit.h>
#include <tech.h>
#include <map.h>
#include <player.h>
#include <log.h>

struct unit_type unit_types[U_LAST];
/* the unit_types array is now setup in:
   server/ruleset.c (for the server)
   client/packhand.c (for the client) */

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
    if(player_owns_active_wonder(pplayer, B_LIGHTHOUSE)) 
      val+=3;
    if(player_owns_active_wonder(pplayer, B_MAGELLAN)) 
      val += (improvement_variant(B_MAGELLAN)==1) ? 3 : 6;
    if (get_invention(pplayer, game.rtech.boat_fast/*A_POWER*/) == TECH_KNOWN)
      val+=3;
    if (val < 6) 
      val = 6;
  }
  if (val < 3) val = 3;
  return val;
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
Whether a diplomat can move to a particular tile and perform a
particular action there.
**************************************************************************/
int diplomat_can_do_action(struct unit *pdiplomat,
			   enum diplomat_actions action, 
			   int destx, int desty)
{
  if(!is_diplomat_action_available(pdiplomat, action, destx, desty))
    return 0;

  if(!is_tiles_adjacent(pdiplomat->x, pdiplomat->y, destx, desty))
    return 0;

  if(pdiplomat->moves_left >= unit_move_rate(pdiplomat))
    return 1;   /* Hasn't moved yet this turn */

  if(pdiplomat->moves_left >= map_move_cost(pdiplomat, destx, desty))
    return 1;

  return 0;
}

/**************************************************************************
Whether a diplomat can perform a particular action at a particular
tile.  This does _not_ check whether the diplomat can move there.
If the action is DIPLOMAT_ANY_ACTION, checks whether there is any
action the diplomat can perform at the tile.
**************************************************************************/
int is_diplomat_action_available(struct unit *pdiplomat,
				 enum diplomat_actions action, 
				 int destx, int desty)
{
  struct city *pcity=map_get_city(destx, desty);

  if(pcity) {  
    if(pcity->owner!=pdiplomat->owner) {
      if(action==DIPLOMAT_SABOTAGE)
        return 1;

      if(action==DIPLOMAT_EMBASSY &&
	 !player_has_embassy(&game.players[pdiplomat->owner], 
			     &game.players[pcity->owner]))
	return 1;
      if(action==SPY_POISON && unit_flag(pdiplomat->type, F_SPY))
        return 1;
      if(action==DIPLOMAT_INVESTIGATE)
        return 1;
      if(action==DIPLOMAT_STEAL)
        return 1;
      if(action==DIPLOMAT_INCITE)
        return 1;
      if(action==DIPLOMAT_ANY_ACTION)
        return 1;
    }
  }
  else {
    struct tile *ptile=map_get_tile(destx, desty);
    if((action==SPY_SABOTAGE_UNIT || action==DIPLOMAT_ANY_ACTION) &&
       unit_list_size(&ptile->units)>=1 &&
       unit_list_get(&ptile->units, 0)->owner!=pdiplomat->owner &&
       unit_flag(pdiplomat->type, F_SPY))
      return 1;
    if((action==DIPLOMAT_BRIBE || action==DIPLOMAT_ANY_ACTION) &&
       unit_list_size(&ptile->units)==1 &&
       unit_list_get(&ptile->units, 0)->owner!=pdiplomat->owner)
      return 1;
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
  return unit_flag(punit->type, F_CARAVAN) &&
    is_tiles_adjacent(punit->x, punit->y, pcity->x, pcity->y) &&
    punit->owner==pcity->owner && !pcity->is_building_unit  && 
    is_wonder(pcity->currently_building) &&
    (pcity->shield_stock < improvement_value(pcity->currently_building));
}


/**************************************************************************
...
**************************************************************************/
int unit_can_help_build_wonder_here(struct unit *punit)
{
  struct city *pcity = map_get_city(punit->x, punit->y);
  return pcity && unit_can_help_build_wonder(punit, pcity);
}


/**************************************************************************
...
**************************************************************************/
int unit_can_est_traderoute_here(struct unit *punit)
{
  struct city *phomecity, *pdestcity;

  if (!unit_flag(punit->type, F_CARAVAN)) return 0;
  pdestcity = map_get_city(punit->x, punit->y);
  if (!pdestcity) return 0;
  phomecity = find_city_by_id(punit->homecity);
  if (!phomecity) return 0;
  return can_establish_trade_route(phomecity, pdestcity);
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
    if(is_ground_units_transport(punit))
      total_capacity+=get_transporter_capacity(punit);
    else if (is_ground_unit(punit))
      none_transporters++;
  }
  unit_list_iterate_end;
  
  return(total_capacity>none_transporters ? total_capacity-none_transporters : 0);
}

/**************************************************************************
The above one isn't quite what I want - Kris
Specifically: only pplayer's transports count; only returns true/false;
and returns true if (capacity == passengers).  --dwp
**************************************************************************/
int is_enough_transporter_space (struct player *pplayer, int x, int y)
{
  int none_transporters, total_capacity=0;
  none_transporters=0;
  total_capacity=0;
  unit_list_iterate(map_get_tile(x, y)->units, punit) {
    if(is_ground_units_transport(punit)
       && punit->owner == pplayer->player_no)
      total_capacity+=get_transporter_capacity(punit);
    else if (is_ground_unit(punit))
      none_transporters++;
  }
  unit_list_iterate_end;
  
  return(total_capacity>=none_transporters ? 1 : 0);
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
    if(((is_ground_unit(punit) && is_ground_units_transport(ptran))
	|| (is_air_unit(punit) && unit_flag(ptran->type, F_CARRIER))
	|| (unit_flag(punit->type, F_MISSILE)
	    && unit_flag(ptran->type, F_SUBMARINE)))
       && (!map_get_city(punit->x, punit->y)
	   || punit->activity==ACTIVITY_SENTRY)) {
      unit_list_unlink(srclist, punit);
      unit_list_insert(list, punit);
      cargo--;
    }
  }
}

/**************************************************************************
 Get the _minimum_ cargo list, taking into account cities, other boats.
 Eg, for when unit is disbanded, or supporting city captured.
 Note: for Carriers and Subs the minimum list is defined as empty:
 the planes/missiles will still be in the air and have a chance to
 get to safety.
 Note: this modifies the map units list, so iterators beware.
**************************************************************************/
void transporter_min_cargo_to_unitlist(struct unit *ptran,
				       struct unit_list *list)
{
  int x = ptran->x;
  int y = ptran->y;
  int this_capacity = get_transporter_capacity(ptran);
  struct unit_list *srclist;
  int tile_capacity, tile_ncargo, nlost;
  
  unit_list_init(list);

  if (!is_sailing_unit(ptran)
      || !is_ground_units_transport(ptran)
      || (map_get_terrain(x, y) != T_OCEAN) /* includes cities */ ) {
    return;
  }

  srclist = &map_get_tile(x,y)->units;

  /* this iteration is non-destructive; just counting: */
  tile_capacity = 0;
  tile_ncargo = 0;
  unit_list_iterate((*srclist), punit) {
    if (is_sailing_unit(punit)
        && !is_ground_units_transport(ptran)) {
      tile_capacity += get_transporter_capacity(punit);
    } else if (is_ground_unit(punit)) {
      tile_ncargo++;
    }
  }
  unit_list_iterate_end;

  tile_capacity -= this_capacity;
  nlost = tile_ncargo - tile_capacity;

  if (nlost <= 0) {
    return;
  }

  /* actually, unit_list_iterate _is_ safe for this */
  unit_list_iterate((*srclist), punit) {
    if(is_ground_unit(punit)) {
      unit_list_unlink(srclist, punit);
      unit_list_insert(list, punit);
      nlost--;
      if (nlost==0) break;
    }
  }
  unit_list_iterate_end;
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
int is_ground_units_transport(struct unit *punit)
{
return (get_transporter_capacity(punit)
	&& !unit_flag(punit->type, F_SUBMARINE)
	&& !unit_flag(punit->type, F_CARRIER));
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
int is_ground_threat(struct player *pplayer, struct unit *punit)
{
  return ((pplayer->player_no != punit->owner)
	  && (unit_flag(punit->type, F_DIPLOMAT)
	      || (is_ground_unit(punit)
		  && is_military_unit(punit))));
}

/**************************************************************************
...
**************************************************************************/
int is_square_threatened(struct player *pplayer, int x, int y)
{
  int i,j;
  int threat=0;

  for(i=x-2;i<=x+2;i++) {
    for(j=y-2;j<=y+2;j++) {
      unit_list_iterate(map_get_tile(i,j)->units, punit) {
	threat += is_ground_threat(pplayer, punit);
      }
      unit_list_iterate_end;
    }
  }
  return threat;
}

/**************************************************************************
...
**************************************************************************/
int is_field_unit(struct unit *punit)
{
  return ((unit_flag(punit->type, F_FIELDUNIT) > 0));

}


/**************************************************************************
  Is the unit one that is invisible on the map, which is currently limited
  to subs and missiles in subs.
**************************************************************************/
int is_hiding_unit(struct unit *punit)
{
  if(unit_flag(punit->type, F_SUBMARINE)) return 1;
  if(unit_flag(punit->type, F_MISSILE)) {
    if(map_get_terrain(punit->x, punit->y)==T_OCEAN) {
      unit_list_iterate(map_get_tile(punit->x, punit->y)->units, punit2) {
	if(unit_flag(punit2->type, F_SUBMARINE)) return 1;
      } unit_list_iterate_end;
    }
  }
  return 0;
}


/**************************************************************************
...
**************************************************************************/
int unit_flag(enum unit_type_id id, int flag)
{
  assert(flag>=0 && flag<F_LAST);
  return unit_types[id].flags & (1<<flag);
}

/**************************************************************************
...
**************************************************************************/
int unit_has_role(enum unit_type_id id, int role)
{
  assert(role>=L_FIRST && role<L_LAST);
  return unit_types[id].roles & (1<<(role-L_FIRST));
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

  if(!punit->moves_left)
    return 0;

  return 1;
}

/**************************************************************************
...
**************************************************************************/
int can_unit_add_to_city(struct unit *punit)
{
  struct city *pcity;

  if(!unit_flag(punit->type, F_SETTLERS))
    return 0;
  if(!punit->moves_left)
    return 0;

  pcity = map_get_city(punit->x, punit->y);

  if(!pcity)
    return 0;
  if(pcity->size>8)
    return 0;

  if(improvement_exists(B_AQUEDUCT)
     && !city_got_building(pcity, B_AQUEDUCT) 
     && pcity->size >= game.aqueduct_size)
    return 0;
  
  if(improvement_exists(B_SEWER)
     && !city_got_building(pcity, B_SEWER)
     && pcity->size >= game.sewer_size)
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
Return whether the unit can be put in auto-mode.
(Auto-settler for settlers, auto-attack for military units.)
**************************************************************************/
int can_unit_do_auto(struct unit *punit) 
{
  if (unit_flag(punit->type, F_SETTLERS))
    return 1;
  if (is_military_unit(punit) && map_get_city(punit->x, punit->y))
    return 1;
  return 0;
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

  if (activity == ACTIVITY_EXPLORE) /* added 980803 by Syela */
    return (is_ground_unit(punit) || is_sailing_unit(punit));

  /* can't go directly from non-idle to non-idle activity */
  if(punit->activity!=ACTIVITY_IDLE) return 0;

  switch(activity)  {
  case ACTIVITY_POLLUTION:
    return unit_flag(punit->type, F_SETTLERS) && punit->moves_left &&
           (ptile->special&S_POLLUTION);

  case ACTIVITY_ROAD:
    return unit_flag(punit->type, F_SETTLERS) && punit->moves_left &&
           !(ptile->special&S_ROAD) && ptile->terrain!=T_OCEAN &&
	   (ptile->terrain!=T_RIVER || 
	    get_invention(pplayer, A_BRIDGE)==TECH_KNOWN);

  case ACTIVITY_MINE:
    /* Don't allow it if someone else is irrigating this tile.
     * *Do* allow it if they're transforming - the mine may survive */
    if (unit_flag(punit->type, F_SETTLERS) && punit->moves_left &&
	type->mining_result!=T_LAST && !(ptile->special&S_MINE)) {
      unit_list_iterate(ptile->units, tunit) {
	if(tunit->activity==ACTIVITY_IRRIGATE) return 0;
      }
      unit_list_iterate_end;
      return 1;
    } else return 0;

  case ACTIVITY_IRRIGATE:
    /* Don't allow it if someone else is mining this tile.
     * *Do* allow it if they're transforming - the irrigation may survive */
    if (unit_flag(punit->type, F_SETTLERS) && punit->moves_left &&
	   !(ptile->special&S_IRRIGATION) &&
	   ( (ptile->terrain==type->irrigation_result && 
	      is_water_adjacent_to_tile(punit->x, punit->y)) ||
	     (ptile->terrain!=type->irrigation_result &&
            type->irrigation_result!=T_LAST))) {
      unit_list_iterate(ptile->units, tunit) {
	if(tunit->activity==ACTIVITY_MINE) return 0;
      }
      unit_list_iterate_end;
      return 1;
    } else return 0;

  case ACTIVITY_FORTIFY:
    return is_ground_unit(punit) && punit->moves_left &&
           !unit_flag(punit->type, F_SETTLERS);

  case ACTIVITY_FORTRESS:
    return unit_flag(punit->type, F_SETTLERS) && punit->moves_left &&
           get_invention(pplayer, A_CONSTRUCTION) == TECH_KNOWN &&
	   !(ptile->special&S_FORTRESS) && ptile->terrain!=T_OCEAN;

  case ACTIVITY_SENTRY:
    return 1;

  case ACTIVITY_RAILROAD:
    /* if the tile has road, the terrain must be ok.. */
    return unit_flag(punit->type, F_SETTLERS) && punit->moves_left &&
           (ptile->special&S_ROAD) && !(ptile->special&S_RAILROAD) &&
	   get_invention(&game.players[punit->owner], A_RAILROAD)==TECH_KNOWN;

  case ACTIVITY_PILLAGE:
    return is_ground_unit(punit) && punit->moves_left &&
           ((ptile->special&S_ROAD) || (ptile->special&S_RAILROAD) ||
	    (ptile->special&S_IRRIGATION) || (ptile->special&S_MINE)) &&
	   !is_unit_activity_on_tile(ACTIVITY_PILLAGE, punit->x, punit->y);

  case ACTIVITY_TRANSFORM:
    return (ptile->terrain != T_OCEAN) && (ptile->terrain != T_RIVER) &&
	   unit_flag(punit->type, F_TRANSFORM) && punit->moves_left;

  default:
    freelog(LOG_NORMAL,"Unknown activity %d\n",activity);
    return 0;
  }
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

  sprintf(buffer, "%s%s\n%s\n%s", 
	  get_unit_type(punit->type)->name, 
	  punit->veteran ? " (veteran)" : "",
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
   case ACTIVITY_TRANSFORM:
    return "Transform";
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
   case ACTIVITY_EXPLORE:
    return "Explore";
   default:
    freelog(LOG_FATAL, "Unknown unit activity:%d in unit_activity_text()", punit->activity);
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

/**************************************************************************
...
**************************************************************************/
void unit_list_unlink_all(struct unit_list *This)
{
  genlist_unlink_all(&This->list);
}

/**************************************************************************
 Comparison function for genlist_sort, sorting by ord_map:
 The indirection is a bit gory:
 Read from the right:
   1. cast arg "a" to "ptr to void*"   (we're sorting a list of "void*"'s)
   2. dereference to get the "void*"
   3. cast that "void*" to a "struct unit*"
**************************************************************************/
static int compar_unit_ord_map(const void *a, const void *b)
{
  const struct unit *ua, *ub;
  ua = (const struct unit*) *(const void**)a;
  ub = (const struct unit*) *(const void**)b;
  return ua->ord_map - ub->ord_map;
}

/**************************************************************************
 Comparison function for genlist_sort, sorting by ord_city: see above.
**************************************************************************/
static int compar_unit_ord_city(const void *a, const void *b)
{
  const struct unit *ua, *ub;
  ua = (const struct unit*) *(const void**)a;
  ub = (const struct unit*) *(const void**)b;
  return ua->ord_city - ub->ord_city;
}

/**************************************************************************
...
**************************************************************************/
void unit_list_sort_ord_map(struct unit_list *This)
{
  if(unit_list_size(This) > 1) {
    genlist_sort(&This->list, compar_unit_ord_map);
  }
}

/**************************************************************************
...
**************************************************************************/
void unit_list_sort_ord_city(struct unit_list *This)
{
  if(unit_list_size(This) > 1) {
    genlist_sort(&This->list, compar_unit_ord_city);
  }
}

/**************************************************************************
Returns 1 if the unit_type "exists" in this game, 0 otherwise.
A unit_type doesn't exist if one of:
- id is out of range
- the unit_type has been flagged as removed by setting its
  tech_requirement to A_LAST.
**************************************************************************/
int unit_type_exists(enum unit_type_id id)
{
  if (id<0 || id>=U_LAST)
    return 0;
  else 
    return unit_types[id].tech_requirement!=A_LAST;
}

/**************************************************************************
Does a linear search of unit_types[].name
Returns U_LAST if none match.
**************************************************************************/
enum unit_type_id find_unit_type_by_name(char *s)
{
  int i;

  for( i=0; i<U_LAST; i++ ) {
    if (strcmp(unit_types[i].name, s)==0)
      return i;
  }
  return U_LAST;
}


/**************************************************************************
The following functions use static variables so we can quickly look up
which unit types have given flag or role.
For these functions flags and roles are considered to be in the same "space",
and any "role" argument can also be a "flag".
Only units which pass unit_type_exists are counted.
Unit order is in terms of the order in enum unit_type_id.
**************************************************************************/
static int first_init = 1;
static int n_with_role[L_LAST];
static enum unit_type_id *with_role[L_LAST];

/**************************************************************************
Do the real work for role_unit_precalcs, for one role (or flag), given by i.
**************************************************************************/
static void precalc_one(int i, int (*func_has)(enum unit_type_id, int))
{
  enum unit_type_id u;
  int j;

  /* Count: */
  for(u=0; u<U_LAST; u++) {
    if(unit_type_exists(u) && func_has(u, i)) {
      n_with_role[i]++;
    }
  }
  if(n_with_role[i] > 0) {
    with_role[i] = malloc(n_with_role[i]*sizeof(enum unit_type_id));
    for(j=0, u=0; u<U_LAST; u++) {
      if(unit_type_exists(u) && func_has(u, i)) {
	with_role[i][j++] = u;
      }
    }
    assert(j==n_with_role[i]);
  }
}

/**************************************************************************
Initialize; it is safe to call this multiple times (eg, if units have
changed due to rulesets in client).
**************************************************************************/
void role_unit_precalcs(void)
{
  int i;
  
  if(!first_init) {
    for(i=0; i<L_LAST; i++) {
      free(with_role[i]);
    }
  }
  for(i=0; i<L_LAST; i++) {
    with_role[i] = NULL;
    n_with_role[i] = 0;
  }

  for(i=0; i<F_LAST; i++) {
    precalc_one(i, unit_flag);
  }
  for(i=L_FIRST; i<L_LAST; i++) {
    precalc_one(i, unit_has_role);
  }
  first_init = 0;
}

/**************************************************************************
How many unit types have specified role/flag.
**************************************************************************/
int num_role_units(int role)
{
  assert((role>=0 && role<F_LAST) || (role>=L_FIRST && role<L_LAST));
  return n_with_role[role];
}

/**************************************************************************
Return index-th unit with specified role/flag.
Index -1 means (n-1), ie last one.
**************************************************************************/
enum unit_type_id get_role_unit(int role, int index)
{
  assert((role>=0 && role<F_LAST) || (role>=L_FIRST && role<L_LAST));
  if (index==-1) index = n_with_role[role]-1;
  assert(index>=0 && index<n_with_role[role]);
  return with_role[role][index];
}

/**************************************************************************
Return "best" unit this city can build, with given role/flag.
Returns U_LAST if none match.
**************************************************************************/
enum unit_type_id best_role_unit(struct city *pcity, int role)
{
  enum unit_type_id u;
  int j;
  
  assert((role>=0 && role<F_LAST) || (role>=L_FIRST && role<L_LAST));

  for(j=n_with_role[role]-1; j>=0; j--) {
    u = with_role[role][j];
    if (can_build_unit(pcity, u)) {
      return u;
    }
  }
  return U_LAST;
}
