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
#include <stdlib.h>		/* free */
#include <string.h>
#include <assert.h>

#include "astring.h"
#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "player.h"
#include "shared.h"
#include "support.h"
#include "tech.h"

#include "unit.h"

/* get 'struct unit_list' functions: */
#define SPECLIST_TAG unit
#define SPECLIST_TYPE struct unit
#include "speclist_c.h"


struct unit_type unit_types[U_LAST];
/* the unit_types array is now setup in:
   server/ruleset.c (for the server)
   client/packhand.c (for the client) */

static char *move_type_names[] = {
  "Land", "Sea", "Heli", "Air"
};
static char *flag_names[] = {
  "Caravan", "Missile", "IgZOC", "NonMil", "IgTer", "Carrier",
  "OneAttack", "Pikemen", "Horse", "IgWall", "FieldUnit", "AEGIS",
  "Fighter", "Marines", "Submarine", "Settlers", "Diplomat",
  "Trireme", "Nuclear", "Spy", "Transform", "Paratroopers",
  "Airbase", "Cities", "IgTired"
};
static char *role_names[] = {
  "FirstBuild", "Explorer", "Hut", "HutTech", "Partisan",
  "DefendOk", "DefendGood", "AttackFast", "AttackStrong",
  "Ferryboat", "Barbarian", "BarbarianTech", "BarbarianBoat",
  "BarbarianBuild", "BarbarianBuildTech", "BarbarianLeader",
  "BarbarianSea", "BarbarianSeaTech"
};

/***************************************************************
...
***************************************************************/
char *get_unit_name(Unit_Type_id id)
{
  struct unit_type *ptype;
  static char buffer[256];
  ptype =get_unit_type(id);
  if (ptype->fuel) {
    my_snprintf(buffer, sizeof(buffer),
		"%s [%d/%d/%d(%d)]", ptype->name, ptype->attack_strength,
		ptype->defense_strength,
		ptype->move_rate/3,(ptype->move_rate/3)*ptype->fuel);
  } else {
    my_snprintf(buffer, sizeof(buffer),
		"%s [%d/%d/%d]", ptype->name, ptype->attack_strength,
		ptype->defense_strength, ptype->move_rate/3);
  }
  return buffer;
}

/***************************************************************
...
***************************************************************/
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
    val += player_knows_techs_with_flag(pplayer,TF_BOAT_FAST)*3;
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

  if(!pdiplomat->moves_left)
    return 0;

  return 1;
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
    if(pcity->owner!=pdiplomat->owner &&
       real_map_distance(pdiplomat->x, pdiplomat->y, pcity->x, pcity->y) <= 1) {
      if(action==DIPLOMAT_SABOTAGE)
        return 1;
      if(action==DIPLOMAT_EMBASSY &&
	 !is_barbarian(&game.players[pcity->owner]) &&
	 !player_has_embassy(&game.players[pdiplomat->owner], 
			     &game.players[pcity->owner]))
	return 1;
      if(action==SPY_POISON &&
	 pcity->size>1 &&
	 unit_flag(pdiplomat->type, F_SPY))
        return 1;
      if(action==DIPLOMAT_INVESTIGATE)
        return 1;
      if(action==DIPLOMAT_STEAL && !is_barbarian(&game.players[pcity->owner]))
        return 1;
      if(action==DIPLOMAT_INCITE)
        return 1;
      if(action==DIPLOMAT_ANY_ACTION)
        return 1;
      if (action==SPY_GET_SABOTAGE_LIST && unit_flag(pdiplomat->type, F_SPY))
	return 1;
    }
  } else {
    struct tile *ptile=map_get_tile(destx, desty);
    if((action==SPY_SABOTAGE_UNIT || action==DIPLOMAT_ANY_ACTION) &&
       unit_list_size(&ptile->units)==1 &&
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
Check for any free space on pplayer's transporters at (x,y).
(Return number of units which may be added to transporters to fill them.)
**************************************************************************/
int is_transporter_with_free_space(struct player *pplayer, int x, int y)
{
  int availability = 0;

  unit_list_iterate(map_get_tile(x, y)->units, punit) {
    if (punit->owner == pplayer->player_no) {
      if (is_ground_units_transport(punit))
	availability += get_transporter_capacity(punit);
      else if (is_ground_unit(punit))
	availability--;
    }
  }
  unit_list_iterate_end;

  return (availability > 0 ? availability : 0);
}

/**************************************************************************
Check for enough pplayer's transporters to carry pplayer's ground units at (x,y).
(Return true if capacity of transporters >= number of ground units.)
**************************************************************************/
int is_enough_transporter_space(struct player *pplayer, int x, int y)
{
  int availability = 0;

  unit_list_iterate(map_get_tile(x, y)->units, punit) {
    if (punit->owner == pplayer->player_no) {
      if (is_ground_units_transport(punit))
	availability += get_transporter_capacity(punit);
      else if (is_ground_unit(punit))
	availability--;
    }
  }
  unit_list_iterate_end;

  return (availability >= 0 ? 1 : 0);
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
int is_ground_unittype(Unit_Type_id id)
{
  return (unit_types[id].move_type == LAND_MOVING);
}

/**************************************************************************
...
**************************************************************************/
int is_air_unittype(Unit_Type_id id)
{
  return (unit_types[id].move_type == AIR_MOVING);
}

/**************************************************************************
...
**************************************************************************/
int is_heli_unittype(Unit_Type_id id)
{
  return (unit_types[id].move_type == HELI_MOVING);
}

/**************************************************************************
...
**************************************************************************/
int is_water_unit(Unit_Type_id id)
{
  return (unit_types[id].move_type == SEA_MOVING);
}

/**************************************************************************
...
**************************************************************************/
int is_military_unit(struct unit *punit)
{
  return (unit_flag(punit->type, F_NONMIL) == 0);
}

/**************************************************************************
...
**************************************************************************/
int is_diplomat_unit(struct unit *punit)
{
  return (unit_flag(punit->type, F_DIPLOMAT));
}

/**************************************************************************
...
**************************************************************************/
int utype_shield_cost(struct unit_type *ut, struct government *g)
{
  return ut->shield_cost * g->unit_shield_cost_factor;
}

/**************************************************************************
...
**************************************************************************/
int utype_food_cost(struct unit_type *ut, struct government *g)
{
  return ut->food_cost * g->unit_food_cost_factor;
}

/**************************************************************************
...
**************************************************************************/
int utype_happy_cost(struct unit_type *ut, struct government *g)
{
  return ut->happy_cost * g->unit_happy_cost_factor;
}

/**************************************************************************
...
**************************************************************************/
int utype_gold_cost(struct unit_type *ut, struct government *g)
{
  return ut->gold_cost * g->unit_gold_cost_factor;
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
int unit_flag(Unit_Type_id id, int flag)
{
  assert(flag>=0 && flag<F_LAST);
  return BOOL_VAL(unit_types[id].flags & (1<<flag));
}

/**************************************************************************
...
**************************************************************************/
int unit_has_role(Unit_Type_id id, int role)
{
  assert(role>=L_FIRST && role<L_LAST);
  return BOOL_VAL(unit_types[id].roles & (1<<(role-L_FIRST)));
}

/**************************************************************************
...
**************************************************************************/
int unit_value(Unit_Type_id id)
{
  return (unit_types[id].build_cost);
}


/**************************************************************************
...
**************************************************************************/
char *unit_name(Unit_Type_id id)
{
  return (unit_types[id].name);
}

/**************************************************************************
...
**************************************************************************/
struct unit_type *get_unit_type(Unit_Type_id id)
{
  return &unit_types[id];
}

/**************************************************************************
 Return a string with all the names of units with this flag
 Return NULL if no unit with this flag exists.
 The string must be free'd

 TODO: if there are more than 4 units with this flag return
       a fallback string (e.g. first unit name + "and similar units"
**************************************************************************/
char *get_units_with_flag_string(int flag)
{
  int count=num_role_units(flag);

  if(count==1)
    return mystrdup(unit_name(get_role_unit(flag,0)));

  if(count) {
    struct astring astr;

    astr_init(&astr);
    astr_minsize(&astr,1);
    astr.str[0] = 0;

    while(count--) {
      int u = get_role_unit(flag,count);
      char *unitname = unit_name(u);

      /* there should be something like astr_append() */
      astr_minsize(&astr,astr.n+strlen(unitname));
      strcat(astr.str,unitname);

      if(count==1) {
	char *and_str = _(" and ");
        astr_minsize(&astr,astr.n+strlen(and_str));
        strcat(astr.str, and_str);
      }
      else {
        if(count != 0) {
          astr_minsize(&astr,astr.n+strlen(", "));
          strcat(astr.str,", ");
        }
        else return astr.str;
      }
    }
  }
  return NULL;
}

/**************************************************************************
...
**************************************************************************/
int can_unit_build_city(struct unit *punit)
{
  int x, y;

  if(!unit_flag(punit->type, F_CITIES))
    return 0;
    
  if(map_get_terrain(punit->x, punit->y)==T_OCEAN)
    return 0;

  if(!punit->moves_left)
    return 0;

  if(game.civstyle==1) {
    if(map_get_city(punit->x, punit->y))
      return 0;
  } else {
    for(x=-1; x<=1; x++)
      for(y=-1; y<=1; y++)
	if(map_get_city(punit->x + x, punit->y + y))
	  return 0;
  }

  return 1;
}

/**************************************************************************
...
**************************************************************************/
int kills_citizen_after_attack(struct unit *punit) {
  return (game.killcitizen >> ((int)unit_types[punit->type].move_type-1)) & 1;
}

/**************************************************************************
...
**************************************************************************/
int can_unit_add_to_city(struct unit *punit)
{
  struct city *pcity;

  if(!unit_flag(punit->type, F_CITIES))
    return 0;
  if(!punit->moves_left)
    return 0;

  pcity = map_get_city(punit->x, punit->y);

  if(!pcity)
    return 0;
  if(pcity->size > game.add_to_size_limit)
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
int can_upgrade_unittype(struct player *pplayer, Unit_Type_id id)
{
  Unit_Type_id best_upgrade = -1;

  if (!can_player_build_unit_direct(pplayer, id))
    return -1;
  while (unit_type_exists(id = unit_types[id].obsoleted_by))
    if (can_player_build_unit_direct(pplayer, id))
      best_upgrade = id;

  return best_upgrade;
}

/**************************************************************************
...
**************************************************************************/
int unit_upgrade_price(struct player *pplayer, Unit_Type_id from, Unit_Type_id to)
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
Return whether the unit can connect with given activity (or with
any activity if activity arg is set to ACTIVITY_IDLE)
**************************************************************************/
int can_unit_do_connect (struct unit *punit, enum unit_activity activity) 
{
  struct player  *pplayer = get_player (punit->owner);

  if (!unit_flag(punit->type, F_SETTLERS))
    return 0;

  if (activity == ACTIVITY_IDLE)   /* IDLE here means "any activity" */
    return 1;

  if (activity == ACTIVITY_ROAD 
      || activity == ACTIVITY_IRRIGATE 
      || (activity == ACTIVITY_RAILROAD
	  && player_knows_techs_with_flag(pplayer, TF_RAILROAD))
      || (activity == ACTIVITY_FORTRESS 
	  && player_knows_techs_with_flag(pplayer, TF_FORTRESS)))
  return 1;

  return 0;
}

/**************************************************************************
Return name of activity in static buffer
**************************************************************************/
char* get_activity_text (int activity)
{
  char *text;

  switch (activity) {
  case ACTIVITY_IDLE:		text = _("Idle"); break;
  case ACTIVITY_POLLUTION:	text = _("Pollution"); break;
  case ACTIVITY_ROAD:		text = _("Road"); break;
  case ACTIVITY_MINE:		text = _("Mine"); break;
  case ACTIVITY_IRRIGATE:	text = _("Irrigation"); break;
  case ACTIVITY_FORTIFYING:	text = _("Fortifying"); break;
  case ACTIVITY_FORTIFIED:	text = _("Fortified"); break;
  case ACTIVITY_FORTRESS:	text = _("Fortress"); break;
  case ACTIVITY_SENTRY:		text = _("Sentry"); break;
  case ACTIVITY_RAILROAD:	text = _("Railroad"); break;
  case ACTIVITY_PILLAGE:	text = _("Pillage"); break;
  case ACTIVITY_GOTO:		text = _("Goto"); break;
  case ACTIVITY_EXPLORE:	text = _("Explore"); break;
  case ACTIVITY_TRANSFORM:	text = _("Transform"); break;
  case ACTIVITY_AIRBASE:	text = _("Airbase"); break;
  default:			text = _("Unknown"); break;
  }

  return text;
}

/**************************************************************************
Return whether the unit can be paradropped.
That is if the unit is in a friendly city or on an Airbase
special, have enough movepoints left and have not paradropped
before in this turn.
**************************************************************************/
int can_unit_paradrop(struct unit *punit)
{
  struct city *pcity;
  struct unit_type *utype;
  struct tile *ptile;

  if (!unit_flag(punit->type, F_PARATROOPERS))
    return 0;

  if(punit->paradropped)
    return 0;

  utype = get_unit_type(punit->type);

  if(punit->moves_left < utype->paratroopers_mr_req)
    return 0;

  ptile=map_get_tile(punit->x, punit->y);
  if(ptile->special&S_AIRBASE)
    return 1;

  if(!(pcity = map_get_city(punit->x, punit->y)))
    return 0;

  return 1;
}

/**************************************************************************
This function returns true if the tile at the given location can be
"reclaimed" from ocean into land.  This is the case only when there are
a sufficient number of adjacent tiles that are not ocean.
**************************************************************************/
static int can_reclaim_ocean(int x, int y)
{
  int i, j, landtiles;

  if (terrain_control.ocean_reclaim_requirement >= 9)
    return 0;

  landtiles = 0;
  for (i = -1; i <= 1; i++) {
    for (j = -1; j <= 1; j++) {
      if (i || j) {
	if (map_get_tile(x+i, y+j)->terrain != T_OCEAN) {
	  landtiles++;
	}
	if (landtiles >= terrain_control.ocean_reclaim_requirement)
	  return 1;
      }
    }
  }

  return 0;
}

/**************************************************************************
...
**************************************************************************/
int can_unit_do_activity(struct unit *punit, enum unit_activity activity)
{
  return can_unit_do_activity_targeted(punit, activity, 0);
}

/**************************************************************************
...
**************************************************************************/
int can_unit_do_activity_targeted(struct unit *punit,
				  enum unit_activity activity, int target)
{
  struct player *pplayer;
  struct tile *ptile;
  struct tile_type *type;

  pplayer = &game.players[punit->owner];
  ptile = map_get_tile(punit->x, punit->y);
  type = get_tile_type(ptile->terrain);

  switch(activity) {
  case ACTIVITY_IDLE:
    return 1;

  case ACTIVITY_POLLUTION:
    return unit_flag(punit->type, F_SETTLERS) && (ptile->special&S_POLLUTION);

  case ACTIVITY_ROAD:
    return terrain_control.may_road &&
           unit_flag(punit->type, F_SETTLERS) &&
           !(ptile->special&S_ROAD) && type->road_time &&
	   ((ptile->terrain!=T_RIVER && !(ptile->special&S_RIVER)) || 
	    player_knows_techs_with_flag(pplayer, TF_BRIDGE));

  case ACTIVITY_MINE:
    /* Don't allow it if someone else is irrigating this tile.
     * *Do* allow it if they're transforming - the mine may survive */
    if (terrain_control.may_mine &&
	unit_flag(punit->type, F_SETTLERS) &&
	( (ptile->terrain==type->mining_result && 
	   !(ptile->special&S_MINE)) ||
	  (ptile->terrain!=type->mining_result &&
	   type->mining_result!=T_LAST &&
	   (ptile->terrain!=T_OCEAN ||
	    can_reclaim_ocean(punit->x, punit->y)) &&
	   (type->mining_result!=T_OCEAN ||
	    !(map_get_city(punit->x, punit->y)))) )) {
      unit_list_iterate(ptile->units, tunit) {
	if(tunit->activity==ACTIVITY_IRRIGATE) return 0;
      }
      unit_list_iterate_end;
      return 1;
    } else return 0;

  case ACTIVITY_IRRIGATE:
    /* Don't allow it if someone else is mining this tile.
     * *Do* allow it if they're transforming - the irrigation may survive */
    if (terrain_control.may_irrigate &&
	unit_flag(punit->type, F_SETTLERS) &&
	(!(ptile->special&S_IRRIGATION) ||
	 (!(ptile->special&S_FARMLAND) &&
	  player_knows_techs_with_flag(pplayer, TF_FARMLAND))) &&
	( (ptile->terrain==type->irrigation_result && 
	   is_water_adjacent_to_tile(punit->x, punit->y)) ||
	  (ptile->terrain!=type->irrigation_result &&
	   type->irrigation_result!=T_LAST &&
	   (ptile->terrain!=T_OCEAN ||
	    can_reclaim_ocean(punit->x, punit->y)) &&
	   (type->irrigation_result!=T_OCEAN ||
	    !(map_get_city(punit->x, punit->y)))) )) {
      unit_list_iterate(ptile->units, tunit) {
	if(tunit->activity==ACTIVITY_MINE) return 0;
      }
      unit_list_iterate_end;
      return 1;
    } else return 0;

  case ACTIVITY_FORTIFYING:
    return is_ground_unit(punit) &&
	   (punit->activity != ACTIVITY_FORTIFIED) &&
           !unit_flag(punit->type, F_SETTLERS);

  case ACTIVITY_FORTIFIED:
    return 0;

  case ACTIVITY_FORTRESS:
    return unit_flag(punit->type, F_SETTLERS) &&
           player_knows_techs_with_flag(pplayer, TF_FORTRESS) &&
	   !(ptile->special&S_FORTRESS) && ptile->terrain!=T_OCEAN;

  case ACTIVITY_AIRBASE:
    return unit_flag(punit->type, F_AIRBASE) &&
           player_knows_techs_with_flag(pplayer, TF_AIRBASE) &&
           !(ptile->special&S_AIRBASE) && ptile->terrain!=T_OCEAN;

  case ACTIVITY_SENTRY:
    return 1;

  case ACTIVITY_RAILROAD:
    /* if the tile has road, the terrain must be ok.. */
    return terrain_control.may_road &&
           unit_flag(punit->type, F_SETTLERS) &&
           ((ptile->special&S_ROAD) || (punit->connecting
	    && (type->road_time &&
		((ptile->terrain!=T_RIVER && !(ptile->special&S_RIVER))
		 || player_knows_techs_with_flag(pplayer, TF_BRIDGE)))))
           && !(ptile->special&S_RAILROAD) &&
	   player_knows_techs_with_flag(pplayer, TF_RAILROAD);

  case ACTIVITY_PILLAGE:
    {
      int pspresent;
      int psworking;
      pspresent = get_tile_infrastructure_set(ptile);
      if (pspresent && is_ground_unit(punit)) {
	psworking = get_unit_tile_pillage_set(punit->x, punit->y);
	if (target == S_NO_SPECIAL)
	  return ((pspresent & (~psworking)) != 0);
	else
	  return ((pspresent & (~psworking) & target) != 0);
      } else {
	return 0;
      }
    }

  case ACTIVITY_EXPLORE:
    return (is_ground_unit(punit) || is_sailing_unit(punit));

  case ACTIVITY_TRANSFORM:
    return terrain_control.may_transform &&
	   (type->transform_result!=T_LAST) &&
	   (ptile->terrain!=type->transform_result) &&
	   (ptile->terrain!=T_OCEAN ||
	    can_reclaim_ocean(punit->x, punit->y)) &&
	   (type->transform_result!=T_OCEAN ||
	    !(map_get_city(punit->x, punit->y))) &&
	   unit_flag(punit->type, F_TRANSFORM);

  default:
    freelog(LOG_NORMAL,"Unknown activity %d\n",activity);
    return 0;
  }
}

/**************************************************************************
  assign a new task to a unit.
**************************************************************************/
void set_unit_activity(struct unit *punit, enum unit_activity new_activity)
{
  punit->activity=new_activity;
  punit->activity_count=0;
  punit->activity_target=0;
  punit->connecting = 0;
}

/**************************************************************************
  assign a new targeted task to a unit.
**************************************************************************/
void set_unit_activity_targeted(struct unit *punit,
				enum unit_activity new_activity, int new_target)
{
  punit->activity=new_activity;
  punit->activity_count=0;
  punit->activity_target=new_target;
  punit->connecting = 0;
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
int get_unit_tile_pillage_set(int x, int y)
{
  int tgt_ret = S_NO_SPECIAL;
  unit_list_iterate(map_get_tile(x, y)->units, punit)
    if(punit->activity==ACTIVITY_PILLAGE)
      tgt_ret |= punit->activity_target;
  unit_list_iterate_end;
  return tgt_ret;
}

/**************************************************************************
 ...
**************************************************************************/
char *unit_description(struct unit *punit)
{
  struct city *pcity;
  static char buffer[512];

  pcity = player_find_city_by_id(game.player_ptr, punit->homecity);

  my_snprintf(buffer, sizeof(buffer), "%s%s\n%s\n%s", 
	  get_unit_type(punit->type)->name, 
	  punit->veteran ? _(" (veteran)") : "",
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
  char *moves_str;
   
  switch(punit->activity) {
   case ACTIVITY_IDLE:
     moves_str = _("Moves");
     if(is_air_unit(punit)) {
       int rate,f;
       rate=get_unit_type(punit->type)->move_rate/3;
       f=((punit->fuel)-1);
       if(punit->moves_left%3) {
	 if(punit->moves_left/3>0) {
	   my_snprintf(text, sizeof(text), "%s: (%d)%d %d/3", moves_str,
		       ((rate*f)+(punit->moves_left/3)),
		       punit->moves_left/3, punit->moves_left%3);
	 } else {
	   my_snprintf(text, sizeof(text), "%s: (%d)%d/3", moves_str,
		       ((rate*f)+(punit->moves_left/3)),
		       punit->moves_left%3);
	 }
       } else {
	 my_snprintf(text, sizeof(text), "%s: (%d)%d", moves_str,
		     rate*f+punit->moves_left/3,
		     punit->moves_left/3);
       }
     } else {
       if(punit->moves_left%3) {
	 if(punit->moves_left/3>0) {
	   my_snprintf(text, sizeof(text), "%s: %d %d/3", moves_str,
		       punit->moves_left/3, punit->moves_left%3);
	 } else {
	   my_snprintf(text, sizeof(text),
		       "%s: %d/3", moves_str, punit->moves_left%3);
	 }
       } else {
	 my_snprintf(text, sizeof(text),
		     "%s: %d", moves_str, punit->moves_left/3);
       }
     }
     return text;
   case ACTIVITY_POLLUTION:
   case ACTIVITY_ROAD:
   case ACTIVITY_RAILROAD:
   case ACTIVITY_MINE: 
   case ACTIVITY_IRRIGATE:
   case ACTIVITY_TRANSFORM:
   case ACTIVITY_FORTIFYING:
   case ACTIVITY_FORTIFIED:
   case ACTIVITY_AIRBASE:
   case ACTIVITY_FORTRESS:
   case ACTIVITY_SENTRY:
   case ACTIVITY_GOTO:
   case ACTIVITY_EXPLORE:
     return get_activity_text (punit->activity);
   case ACTIVITY_PILLAGE:
     if(punit->activity_target == 0) {
       return get_activity_text (punit->activity);
     } else {
       my_snprintf(text, sizeof(text), "%s: %s",
		   get_activity_text (punit->activity),
		   map_get_infrastructure_text(punit->activity_target));
       return (text);
     }
   default:
    freelog(LOG_FATAL, "Unknown unit activity:%d in unit_activity_text()", punit->activity);
    exit(0);
  }
  return 0;
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
int unit_type_exists(Unit_Type_id id)
{
  if (id<0 || id>=U_LAST || id>=game.num_unit_types)
    return 0;
  else 
    return unit_types[id].tech_requirement!=A_LAST;
}

/**************************************************************************
Does a linear search of unit_types[].name
Returns U_LAST if none match.
**************************************************************************/
Unit_Type_id find_unit_type_by_name(char *s)
{
  int i;

  for( i=0; i<game.num_unit_types; i++ ) {
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
Unit order is in terms of the order in the units ruleset.
**************************************************************************/
static int first_init = 1;
static int n_with_role[L_LAST];
static Unit_Type_id *with_role[L_LAST];

/**************************************************************************
Do the real work for role_unit_precalcs, for one role (or flag), given by i.
**************************************************************************/
static void precalc_one(int i, int (*func_has)(Unit_Type_id, int))
{
  Unit_Type_id u;
  int j;

  /* Count: */
  for(u=0; u<game.num_unit_types; u++) {
    if(unit_type_exists(u) && func_has(u, i)) {
      n_with_role[i]++;
    }
  }
  if(n_with_role[i] > 0) {
    with_role[i] = fc_malloc(n_with_role[i]*sizeof(Unit_Type_id));
    for(j=0, u=0; u<game.num_unit_types; u++) {
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
Unit_Type_id get_role_unit(int role, int index)
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
Unit_Type_id best_role_unit(struct city *pcity, int role)
{
  Unit_Type_id u;
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

/**************************************************************************
  Convert unit_move_type names to enum; case insensitive;
  returns 0 if can't match.
**************************************************************************/
enum unit_move_type unit_move_type_from_str(char *s)
{
  enum unit_move_type i;

  /* a compile-time check would be nicer, but this will do: */
  assert(sizeof(move_type_names)/sizeof(char*)==AIR_MOVING-LAND_MOVING+1);

  for(i=LAND_MOVING; i<=AIR_MOVING; i++) {
    if (mystrcasecmp(move_type_names[i-LAND_MOVING], s)==0) {
      return i;
    }
  }
  return 0;
}

/**************************************************************************
  Convert flag names to enum; case insensitive;
  returns F_LAST if can't match.
**************************************************************************/
enum unit_flag_id unit_flag_from_str(char *s)
{
  enum unit_flag_id i;

  assert(sizeof(flag_names)/sizeof(char*)==F_LAST);
  
  for(i=0; i<F_LAST; i++) {
    if (mystrcasecmp(flag_names[i], s)==0) {
      return i;
    }
  }
  return F_LAST;
}

/**************************************************************************
  Convert role names to enum; case insensitive;
  returns L_LAST if can't match.
**************************************************************************/
enum unit_role_id unit_role_from_str(char *s)
{
  enum unit_role_id i;

  assert(sizeof(role_names)/sizeof(char*)==L_LAST-L_FIRST);
  
  for(i=L_FIRST; i<L_LAST; i++) {
    if (mystrcasecmp(role_names[i-L_FIRST], s)==0) {
      return i;
    }
  }
  return L_LAST;
}

/**************************************************************************
...
**************************************************************************/
struct player *unit_owner(struct unit *punit)
{
  return (&game.players[punit->owner]);
}
