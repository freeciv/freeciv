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

#include <assert.h>
#include <string.h>

#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "support.h"

#include "city.h"

/* get 'struct city_list' functions: */
#define SPECLIST_TAG city
#define SPECLIST_TYPE struct city
#include "speclist_c.h"

/* start helper functions for generic_city_refresh */
static int content_citizens(struct player *pplayer);
static int set_city_shield_bonus(struct city *pcity);
static int set_city_tax_bonus(struct city *pcity);
static int set_city_science_bonus(struct city *pcity);
static void set_tax_income(struct city *pcity);
static void add_buildings_effect(struct city *pcity);
static void happy_copy(struct city *pcity, int i);
static void citizen_happy_size(struct city *pcity);
static void citizen_happy_luxury(struct city *pcity);
static void citizen_happy_units(struct city *pcity, int unhap);
static void citizen_happy_buildings(struct city *pcity);
static void citizen_happy_wonders(struct city *pcity);
static void unhappy_city_check(struct city *pcity);
static void set_pollution(struct city *pcity);
static void set_food_trade_shields(struct city *pcity);
static void city_support(struct city *pcity);
/* end helper functions for generic_city_refresh */

static int improvement_upkeep_asmiths(struct city *pcity, int i, int asmiths);

/* Iterate a city map, from the center (the city) outwards */

int city_map_iterate_outwards_indices[(CITY_MAP_SIZE*CITY_MAP_SIZE)-4][2] =
{
  { 2, 2 },

  { 1, 2 }, { 2, 1 }, { 3, 2 }, { 2, 3 },
  { 1, 3 }, { 1, 1 }, { 3, 1 }, { 3, 3 },

  { 0, 2 }, { 2, 0 }, { 4, 2 }, { 2, 4 },
  { 0, 3 }, { 0, 1 },
  { 1, 0 }, { 3, 0 },
  { 4, 1 }, { 4, 3 },
  { 3, 4 }, { 1, 4 }
};

struct citystyle *city_styles = NULL;

/* from server/unittools.h */
void send_unit_info(struct player *dest, struct unit *punit);

/* from client/civclient.c or server/srv_main.c */
extern int is_server;

/**************************************************************************
...
**************************************************************************/
int is_valid_city_coords(const int city_x, const int city_y)
{
  if ((city_x == 0 || city_x == CITY_MAP_SIZE-1)
      && (city_y == 0 || city_y == CITY_MAP_SIZE-1))
    return FALSE;
  if (city_x < 0 || city_y < 0
      || city_x >= CITY_MAP_SIZE
      || city_y >= CITY_MAP_SIZE)
    return FALSE;

  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
int is_city_center(int city_x, int city_y)
{
  return (city_x == (CITY_MAP_SIZE / 2))
      && (city_y == (CITY_MAP_SIZE / 2));
}

/**************************************************************************
Finds the city map coordinate for a given map position and a city
center. Returns whether the map position is inside of the city map.
**************************************************************************/
int base_map_to_city_map(int *city_map_x, int *city_map_y,
			 int city_center_x, int city_center_y,
			 int map_x, int map_y)
{
  assert(is_real_tile(map_x, map_y));
  city_map_checked_iterate(city_center_x, city_center_y, cx, cy, mx, my) {
    if (mx == map_x && my == map_y) {
      *city_map_x = cx;
      *city_map_y = cy;
      return TRUE;
    }
  } city_map_checked_iterate_end;
  return FALSE;
}

/**************************************************************************
Finds the city map coordinate for a given map position and a
city. Returns whether the map position is inside of the city map.
**************************************************************************/
int map_to_city_map(int *city_map_x, int *city_map_y,
		    const struct city *const pcity, int map_x, int map_y)
{
  return base_map_to_city_map(city_map_x,
			      city_map_y, pcity->x, pcity->y,
			      map_x, map_y);
}

/**************************************************************************
Finds the map position for a given city map coordinate of a certain
city. Returns true if the map position found is real.
**************************************************************************/
int base_city_map_to_map(int *map_x, int *map_y,
			 int city_center_x, int city_center_y,
			 int city_map_x, int city_map_y)
{
  assert(is_valid_city_coords(city_map_x, city_map_y));
  *map_x = city_center_x + city_map_x - CITY_MAP_SIZE / 2;
  *map_y = city_center_y + city_map_y - CITY_MAP_SIZE / 2;
  return normalize_map_pos(map_x, map_y);
}

/**************************************************************************
Finds the map position for a given city map coordinate of a certain
city. Returns true if the map position found is real.
**************************************************************************/
int city_map_to_map(int *map_x, int *map_y,
		    const struct city *const pcity,
		    int city_map_x, int city_map_y)
{
  return base_city_map_to_map(map_x, map_y,
			      pcity->x, pcity->y, city_map_x, city_map_y);
}

/**************************************************************************
  Set the worker on the citymap.  Also sets the worked field in the map.
**************************************************************************/
void set_worker_city(struct city *pcity, int city_x, int city_y,
		     enum city_tile_type type)
{
  int map_x, map_y;

  if (city_map_to_map(&map_x, &map_y, pcity, city_x, city_y)) {
    struct tile *ptile = map_get_tile(map_x, map_y);

    if (pcity->city_map[city_x][city_y] == C_TILE_WORKER
	&& ptile->worked == pcity)
      ptile->worked = NULL;
    pcity->city_map[city_x][city_y] = type;
    if (type == C_TILE_WORKER)
      ptile->worked = pcity;
  } else {
    assert(type == C_TILE_UNAVAILABLE);
    pcity->city_map[city_x][city_y] = type;
  }
}

/**************************************************************************
...
**************************************************************************/
enum city_tile_type get_worker_city(struct city *pcity, int city_x, int city_y)
{
  if (!is_valid_city_coords(city_x, city_y)) 
    return C_TILE_UNAVAILABLE;
  return pcity->city_map[city_x][city_y];
}

/**************************************************************************
...
**************************************************************************/
int is_worker_here(struct city *pcity, int city_x, int city_y) 
{
  if (!is_valid_city_coords(city_x, city_y)) {
    return FALSE;
  }

  return get_worker_city(pcity, city_x, city_y) == C_TILE_WORKER;
}

/**************************************************************************
 FIXME: This should be replaced in the near future with
 improvment_redundant()
**************************************************************************/
int wonder_replacement(struct city *pcity, Impr_Type_id id)
{
  if(is_wonder(id)) return FALSE;
  switch (id) {
  case B_BARRACKS:
  case B_BARRACKS2:
  case B_BARRACKS3:
    if (city_affected_by_wonder(pcity, B_SUNTZU))
      return TRUE;
    break;
  case B_GRANARY:
    if (improvement_variant(B_PYRAMIDS)==0
	&& city_affected_by_wonder(pcity, B_PYRAMIDS))
      return TRUE;
    break;
  case B_CATHEDRAL:
    if (improvement_variant(B_MICHELANGELO)==0
	&& city_affected_by_wonder(pcity, B_MICHELANGELO))
      return TRUE;
    break;
  case B_CITY:  
    if (city_affected_by_wonder(pcity, B_WALL))
      return TRUE;
    break;
  case B_HYDRO:
  case B_POWER:
  case B_NUCLEAR:
    if (city_affected_by_wonder(pcity, B_HOOVER))
      return TRUE;
    break;
  case B_POLICE:
    if (city_affected_by_wonder(pcity, B_WOMENS))
      return TRUE;
    break;
  case B_RESEARCH:
    if (city_affected_by_wonder(pcity, B_SETI))
      return TRUE;
    break;
  default:
    break;
  }
  return FALSE;
}

/**************************************************************************
...
**************************************************************************/
char *get_impr_name_ex(struct city *pcity, Impr_Type_id id)
{
  static char buffer[256];
  char *state = NULL;

  if (pcity) {
    switch (pcity->improvements[id]) {
    case I_REDUNDANT:	state = Q_("?redundant:*");	break;
    case I_OBSOLETE:	state = Q_("?obsolete:O");	break;
    default:						break;
    }
  } else if (is_wonder(id)) {
    if (game.global_wonders[id]) {
      state = Q_("?built:B");
    } else {
      state = Q_("?wonder:w");
    }
  }
  
  if (state) {
    my_snprintf(buffer, sizeof(buffer), "%s(%s)",
		get_improvement_name(id), state); 
    return buffer;
  } else {
    return get_improvement_name(id);
  }
}

/**************************************************************************
...
**************************************************************************/
int city_buy_cost(struct city *pcity)
{
  int total,cost;
  int build=pcity->shield_stock;

  if (pcity->is_building_unit) {
    total=unit_value(pcity->currently_building);
    if (build>=total)
      return 0;
    cost=(total-build)*2+(total-build)*(total-build)/20; 
  } else {
    total=improvement_value(pcity->currently_building);
    if (build>=total)
      return 0;
    cost=(total-build)*2;
    if(is_wonder(pcity->currently_building))
      cost*=2;
  }
  if(!build)
    cost*=2;
  return cost;
}

/**************************************************************************
...
**************************************************************************/
struct player *city_owner(struct city *pcity)
{
  return (&game.players[pcity->owner]);
}

/**************************************************************************
 Returns 1 if the given city is next to or on one of the given building's
 terr_gate (terrain) or spec_gate (specials), or if the building has no
 terrain/special requirements.
**************************************************************************/
static int city_has_terr_spec_gate(struct city *pcity, Impr_Type_id id)
{
  struct impr_type *impr;
  enum tile_terrain_type *terr_gate;
  enum tile_special_type *spec_gate;

  impr = get_improvement_type(id);
  spec_gate = impr->spec_gate;
  terr_gate = impr->terr_gate;

  if (*spec_gate==S_NO_SPECIAL && *terr_gate==T_LAST) return TRUE;

  for (;*spec_gate!=S_NO_SPECIAL;spec_gate++) {
    if (map_get_special(pcity->x,pcity->y) & *spec_gate ||
        is_special_near_tile(pcity->x,pcity->y,*spec_gate)) return TRUE;
  }

  for (;*terr_gate!=T_LAST;terr_gate++) {
    if (map_get_terrain(pcity->x,pcity->y) == *terr_gate ||
        is_terrain_near_tile(pcity->x,pcity->y,*terr_gate)) return TRUE;
  }

  return FALSE;
}

/**************************************************************************
 Will this city ever be able to build this improvement?
 Doesn't check for building prereqs
**************************************************************************/
int can_eventually_build_improvement(struct city *pcity, Impr_Type_id id)
{
  /* also does an improvement_exists() */
  if (!could_player_eventually_build_improvement(city_owner(pcity),id))
    return FALSE;

  if (city_got_building(pcity, id))
    return FALSE;

  if (!city_has_terr_spec_gate(pcity,id))
    return FALSE;

  return !improvement_redundant(city_owner(pcity),pcity, id, TRUE);
}

/**************************************************************************
 Could this improvment be built in the city, without checking if the
 owner has the required tech, but if all other pre reqs are fulfiled? 
 modularized so the AI can choose the tech it wants -- Syela 
**************************************************************************/
int could_build_improvement(struct city *pcity, Impr_Type_id id)
{
  struct impr_type *impr;

  if (!can_eventually_build_improvement(pcity, id))
    return FALSE;

  impr = get_improvement_type(id);

  /* The building pre req */
  if (impr->bldg_req != B_LAST)
    if (!city_got_building(pcity,impr->bldg_req))
      return FALSE;

  return TRUE;
}

/**************************************************************************
Can this improvement get built in this city, by the player
who owns the city?
**************************************************************************/
int can_build_improvement(struct city *pcity, Impr_Type_id id)
{
  struct player *p=city_owner(pcity);
  if (!improvement_exists(id))
    return FALSE;
  if (!player_knows_improvement_tech(p,id))
    return FALSE;
  return(could_build_improvement(pcity, id));
}


/**************************************************************************
Whether given city can build given unit,
ignoring whether unit is obsolete.
**************************************************************************/
int can_build_unit_direct(struct city *pcity, Unit_Type_id id)
{
  if (!can_player_build_unit_direct(city_owner(pcity), id))
    return FALSE;
  if (!is_terrain_near_tile(pcity->x, pcity->y, T_OCEAN) && is_water_unit(id))
    return FALSE;
  return TRUE;
}

/**************************************************************************
Whether given city can build given unit;
returns 0 if unit is obsolete.
**************************************************************************/
int can_build_unit(struct city *pcity, Unit_Type_id id)
{  
  if (!can_build_unit_direct(pcity, id))
    return FALSE;
  while(unit_type_exists((id = unit_types[id].obsoleted_by)))
    if (can_player_build_unit_direct(city_owner(pcity), id))
	return FALSE;
  return TRUE;
}

/**************************************************************************
Whether player can eventually build given unit in the city;
returns 0 if unit can never possibily be built in this city.
**************************************************************************/
int can_eventually_build_unit(struct city *pcity, Unit_Type_id id)
{
  /* Can the _player_ ever build this unit? */
  if (!can_player_eventually_build_unit(city_owner(pcity), id))
    return 0;

  /* Some units can be built only in certain cities -- for instance,
     ships may be built only in cities adjacent to ocean. */
  if (!is_terrain_near_tile(pcity->x, pcity->y, T_OCEAN) && is_water_unit(id))
    return 0;

  return 1;
}


/**************************************************************************
 Returns how many thousand citizen live in this city.
**************************************************************************/
int city_population(struct city *pcity)
{
  /*  Sum_{i=1}^{n} i  ==  n*(n+1)/2  */
  return pcity->size * (pcity->size + 1) * 5;
}

/**************************************************************************
...
**************************************************************************/
int city_got_building(struct city *pcity,  Impr_Type_id id) 
{
  if (!improvement_exists(id))
    return FALSE;
  else 
    return (pcity->improvements[id]);
}

/**************************************************************************
...
**************************************************************************/
int improvement_upkeep(struct city *pcity, int i) 
{
  if (!improvement_exists(i))
    return 0;
  if (is_wonder(i))
    return 0;
  if (improvement_types[i].upkeep == 1 &&
      city_affected_by_wonder(pcity, B_ASMITHS)) 
    return 0;
  if (government_has_flag(get_gov_pcity(pcity), G_CONVERT_TITHES_TO_MONEY)
      && (i == B_TEMPLE || i == B_COLOSSEUM || i == B_CATHEDRAL)) {
    return 0;
  }
  
  return (improvement_types[i].upkeep);
}

/**************************************************************************
  Caller to pass asmiths = city_affected_by_wonder(pcity, B_ASMITHS)
**************************************************************************/
static int improvement_upkeep_asmiths(struct city *pcity, int i, int asmiths) 
{
  if (!improvement_exists(i))
    return 0;
  if (is_wonder(i))
    return 0;
  if (asmiths && improvement_types[i].upkeep == 1) 
    return 0;
  if (government_has_flag(get_gov_pcity(pcity), G_CONVERT_TITHES_TO_MONEY)
      && (i == B_TEMPLE || i == B_COLOSSEUM || i == B_CATHEDRAL)) {
    return 0;
  }
  return (improvement_types[i].upkeep);
}

/**************************************************************************
...
**************************************************************************/
int get_shields_tile(int x, int y)
{
  int s=0;
  enum tile_special_type spec_t = map_get_special(x, y);
  enum tile_terrain_type tile_t = map_get_terrain(x, y);

  if (spec_t & S_SPECIAL_1) 
    s = get_tile_type(tile_t)->shield_special_1;
  else if (spec_t & S_SPECIAL_2) 
    s = get_tile_type(tile_t)->shield_special_2;
  else
    s = get_tile_type(tile_t)->shield;

  if (spec_t & S_MINE)
    s += (get_tile_type(tile_t))->mining_shield_incr;
  if (spec_t & S_RAILROAD)
    s+=(s*terrain_control.rail_shield_bonus)/100;
  if (spec_t & S_POLLUTION)
    s-=(s*terrain_control.pollution_shield_penalty)/100; /* The shields here are icky */
  if (spec_t & S_FALLOUT)
    s-=(s*terrain_control.fallout_shield_penalty)/100;
  return s;
}

/**************************************************************************
...
**************************************************************************/
int city_get_shields_tile(int x, int y, struct city *pcity)
{
  return base_city_get_shields_tile(x, y, pcity, city_celebrating(pcity));
}

/**************************************************************************
...
**************************************************************************/
int base_city_get_shields_tile(int x, int y, struct city *pcity,
			       int is_celebrating)
{
  int is_real, map_x, map_y, s = 0;
  enum tile_special_type spec_t;
  enum tile_terrain_type tile_t;
  struct government *g = get_gov_pcity(pcity);
  int before_penalty = (is_celebrating ? g->celeb_shields_before_penalty
			: g->shields_before_penalty);
  
  is_real = city_map_to_map(&map_x, &map_y, pcity, x, y);
  assert(is_real);

  spec_t = map_get_special(map_x, map_y);
  tile_t = map_get_terrain(map_x, map_y);

  if (spec_t & S_SPECIAL_1) 
    s=get_tile_type(tile_t)->shield_special_1;
  else if (spec_t & S_SPECIAL_2) 
    s=get_tile_type(tile_t)->shield_special_2;
  else
    s=get_tile_type(tile_t)->shield;

  if (spec_t & S_MINE) {
    s += (get_tile_type(tile_t))->mining_shield_incr;
  }
  if (spec_t & S_RAILROAD)
    s+=(s*terrain_control.rail_shield_bonus)/100;
  if (city_affected_by_wonder(pcity, B_RICHARDS))
    s++;
  if (tile_t==T_OCEAN && city_got_building(pcity, B_OFFSHORE))
    s++;
  /* government shield bonus & penalty */
  if (s)
    s += (is_celebrating ? g->celeb_shield_bonus : g->shield_bonus);
  if (before_penalty && s > before_penalty)
    s--;
  if (spec_t & S_POLLUTION)
    s-=(s*terrain_control.pollution_shield_penalty)/100; /* The shields here are icky */
  if (spec_t & S_FALLOUT)
    s-=(s*terrain_control.fallout_shield_penalty)/100;

  if (s < game.rgame.min_city_center_shield && is_city_center(x, y))
    s=game.rgame.min_city_center_shield;

  return s;
}

/**************************************************************************
...
**************************************************************************/
int get_trade_tile(int x, int y)
{
  enum tile_special_type spec_t = map_get_special(x, y);
  enum tile_terrain_type tile_t = map_get_terrain(x,y);
  int t;
 
  if (spec_t & S_SPECIAL_1) 
    t = get_tile_type(tile_t)->trade_special_1;
  else if (spec_t & S_SPECIAL_2) 
    t = get_tile_type(tile_t)->trade_special_2;
  else
    t = get_tile_type(tile_t)->trade;

  if ((spec_t & S_RIVER) && (tile_t != T_OCEAN)) {
    t += terrain_control.river_trade_incr;
  }
  if (spec_t & S_ROAD) {
    t += (get_tile_type(tile_t))->road_trade_incr;
  }
  if (t) {
    if (spec_t & S_RAILROAD)
      t+=(t*terrain_control.rail_trade_bonus)/100;

    if (spec_t & S_POLLUTION)
      t-=(t*terrain_control.pollution_trade_penalty)/100; /* The trade here is dirty */
    if (spec_t & S_FALLOUT)
      t-=(t*terrain_control.fallout_trade_penalty)/100;
  }
  return t;
}

/**************************************************************************
...
**************************************************************************/
int city_get_trade_tile(int x, int y, struct city *pcity)
{
  return base_city_get_trade_tile(x, y, pcity, city_celebrating(pcity));
}

/**************************************************************************
...
**************************************************************************/
int base_city_get_trade_tile(int x, int y, struct city *pcity,
			     int is_celebrating)
{
  enum tile_special_type spec_t;
  enum tile_terrain_type tile_t;
  struct government *g = get_gov_pcity(pcity);
  int is_real, map_x, map_y, t;

  is_real = city_map_to_map(&map_x, &map_y, pcity, x, y);
  assert(is_real);

  spec_t = map_get_special(map_x, map_y);
  tile_t = map_get_terrain(map_x, map_y);
 
  if (spec_t & S_SPECIAL_1) 
    t=get_tile_type(tile_t)->trade_special_1;
  else if (spec_t & S_SPECIAL_2) 
    t=get_tile_type(tile_t)->trade_special_2;
  else
    t=get_tile_type(tile_t)->trade;

  if ((spec_t & S_RIVER) && (tile_t != T_OCEAN)) {
    t += terrain_control.river_trade_incr;
  }
  if (spec_t & S_ROAD) {
    t += (get_tile_type(tile_t))->road_trade_incr;
  }
  if (t) {
    int before_penalty = (is_celebrating ? g->celeb_trade_before_penalty
			  : g->trade_before_penalty);
    
    if (spec_t & S_RAILROAD)
      t+=(t*terrain_control.rail_trade_bonus)/100;

    /* Civ1 specifically documents that Railroad trade increase is before 
     * Democracy/Republic [government in general now -- SKi] bonus  -AJS */
    if (t)
      t += (is_celebrating ? g->celeb_trade_bonus : g->trade_bonus);

    if(city_affected_by_wonder(pcity, B_COLLOSSUS)) 
      t++;
    if((spec_t&S_ROAD) && city_got_building(pcity, B_SUPERHIGHWAYS))
      t+=(t*terrain_control.road_superhighway_trade_bonus)/100;
 
    /* government trade penalty -- SKi */
    if (before_penalty && t > before_penalty) 
      t--;
    if (spec_t & S_POLLUTION)
      t-=(t*terrain_control.pollution_trade_penalty)/100; /* The trade here is dirty */
    if (spec_t & S_FALLOUT)
      t-=(t*terrain_control.fallout_trade_penalty)/100;
  }

  if (t < game.rgame.min_city_center_trade && is_city_center(x, y))
    t=game.rgame.min_city_center_trade;

  return t;
}

/**************************************************************************
...
**************************************************************************/
int get_food_tile(int x, int y)
{
  int f;
  enum tile_special_type spec_t=map_get_special(x, y);
  enum tile_terrain_type tile_t=map_get_terrain(x, y);
  struct tile_type *type = get_tile_type(tile_t);

  if (spec_t & S_SPECIAL_1) 
    f=get_tile_type(tile_t)->food_special_1;
  else if (spec_t & S_SPECIAL_2) 
    f=get_tile_type(tile_t)->food_special_2;
  else
    f=get_tile_type(tile_t)->food;

  if (spec_t & S_IRRIGATION) {
    f += type->irrigation_food_incr;
    /* No farmland since we do not assume a city with a supermarket */
  }

  if (spec_t & S_RAILROAD)
    f+=(f*terrain_control.rail_food_bonus)/100;

  if (spec_t & S_POLLUTION)
    f-=(f*terrain_control.pollution_food_penalty)/100; /* The food here is yucky */
  if (spec_t & S_FALLOUT)
    f-=(f*terrain_control.fallout_food_penalty)/100;

  return f;
}

/**************************************************************************
...
**************************************************************************/
int city_get_food_tile(int x, int y, struct city *pcity)
{
  return base_city_get_food_tile(x, y, pcity, city_celebrating(pcity));
}

/**************************************************************************
Here the exact food production should be calculated. That is
including ALL modifiers. 
Center tile acts as irrigated...
**************************************************************************/
int base_city_get_food_tile(int x, int y, struct city *pcity,
			    int is_celebrating)
{
  int is_real, map_x, map_y, f;
  enum tile_special_type spec_t;
  enum tile_terrain_type tile_t;
  struct tile_type *type;
  struct government *g = get_gov_pcity(pcity);
  int before_penalty = (is_celebrating ? g->celeb_food_before_penalty
			: g->food_before_penalty);
  int city_auto_water;

  is_real = city_map_to_map(&map_x, &map_y, pcity, x, y);
  assert(is_real);

  spec_t = map_get_special(map_x, map_y);
  tile_t = map_get_terrain(map_x, map_y);

  type=get_tile_type(tile_t);
  city_auto_water = (is_city_center(x, y)
		     && tile_t == type->irrigation_result
		     && terrain_control.may_irrigate);

  if (spec_t & S_SPECIAL_1) 
    f=get_tile_type(tile_t)->food_special_1;
  else if (spec_t & S_SPECIAL_2) 
    f=get_tile_type(tile_t)->food_special_2;
  else
    f=get_tile_type(tile_t)->food;

  if ((spec_t & S_IRRIGATION) || city_auto_water) {
    f += type->irrigation_food_incr;
    if (((spec_t & S_FARMLAND) ||
	 (city_auto_water &&
	  player_knows_techs_with_flag(city_owner(pcity), TF_FARMLAND))) &&
	city_got_building(pcity, B_SUPERMARKET)) {
      f += (f * terrain_control.farmland_supermarket_food_bonus) / 100;
    }
  }

  if (tile_t==T_OCEAN && city_got_building(pcity, B_HARBOUR))
    f++;

  if (spec_t & S_RAILROAD)
    f+=(f*terrain_control.rail_food_bonus)/100;

  if (f)
    f += (is_celebrating ? g->celeb_food_bonus : g->food_bonus);
  if (before_penalty && f > before_penalty) 
    f--;

  if (spec_t & S_POLLUTION)
    f-=(f*terrain_control.pollution_food_penalty)/100; /* The food here is yucky */
  if (spec_t & S_FALLOUT)
    f-=(f*terrain_control.fallout_food_penalty)/100;

  if (f < game.rgame.min_city_center_food && is_city_center(x, y))
    f=game.rgame.min_city_center_food;

  return f;
}

/**************************************************************************
...
**************************************************************************/
int city_can_be_built_here(int x, int y)
{
  if (map_get_terrain(x, y) == T_OCEAN)
    return FALSE;

  /* game.rgame.min_dist_bw_cities minimum is 1, which means adjacent is okay */
  square_iterate(x, y, game.rgame.min_dist_bw_cities-1, x1, y1) {
    if (map_get_city(x1, y1)) {
      return FALSE;
    }
  } square_iterate_end;

  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
int can_establish_trade_route(struct city *pc1, struct city *pc2)
{
  int i;
  int r=1;
  if (!pc1 || !pc2 || pc1 == pc2
      || (pc1->owner == pc2->owner
	  && map_distance(pc1->x, pc1->y, pc2->x, pc2->y) <= 8))
    return FALSE;
  
  for(i=0;i<4;i++) {
    r*=pc1->trade[i];
    if (pc1->trade[i]==pc2->id) return 0;
  }
  if (r) return 0;
  r = 1;
  for (i=0;i<4;i++) 
    r*=pc2->trade[i];
  return (!r);
}

/**************************************************************************
...
**************************************************************************/
int trade_between_cities(struct city *pc1, struct city *pc2)
{
  int bonus=0;

  if (pc1 && pc2) {
    bonus=(pc1->tile_trade+pc2->tile_trade+4)/8;

    /* Double if on different continents. */
    if (map_get_continent(pc1->x, pc1->y) !=
	map_get_continent(pc2->x, pc2->y)) bonus *= 2;

    if (pc1->owner==pc2->owner)
      bonus/=2;
  }
  return bonus;
}

/**************************************************************************
 Return number of trade route city has
**************************************************************************/
int city_num_trade_routes(struct city *pcity)
{
  int i,n;
  for(i=0,n=0; i<4; i++)
    if(pcity->trade[i]) n++;
  
  return n;
}

/*************************************************************************
Calculate amount of gold remaining in city after paying for buildings
*************************************************************************/
int city_gold_surplus(struct city *pcity)
{
  int asmiths = city_affected_by_wonder(pcity, B_ASMITHS);
  int cost=0;
  int i;
  for (i=0;i<game.num_impr_types;i++) 
    if (city_got_building(pcity, i)) 
      cost+=improvement_upkeep_asmiths(pcity, i, asmiths);
  return pcity->tax_total-cost;
}

/**************************************************************************
 Whether a city has an improvement, or the same effect via a wonder.
 (The Impr_Type_id should be an improvement, not a wonder.)
 Note also: city_got_citywalls(), and server/citytools:city_got_barracks()
**************************************************************************/
int city_got_effect(struct city *pcity, Impr_Type_id id)
{
  return city_got_building(pcity, id) || wonder_replacement(pcity, id);
}

/**************************************************************************
 Whether a city has its own City Walls, or the same effect via a wonder.
**************************************************************************/
int city_got_citywalls(struct city *pcity)
{
  if (city_got_building(pcity, B_CITY))
    return TRUE;
  return (city_affected_by_wonder(pcity, B_WALL));
}

/**************************************************************************
...
**************************************************************************/
int city_affected_by_wonder(struct city *pcity, Impr_Type_id id)
{
  struct city *tmp;
  if (!improvement_exists(id))
    return FALSE;
  if (!is_wonder(id) || wonder_obsolete(id))
    return FALSE;
  if (city_got_building(pcity, id))
    return TRUE;
  
  /* For Manhatten it can be owned by anyone, and it doesn't matter
   * whether it is destroyed or not.
   *
   * (The same goes for Apollo, with respect to building spaceship parts,
   * but not for getting the map effect.  This function only returns true
   * for Apollo for the owner of a non-destroyed Apollo; for building
   * spaceship parts just check (game.global_wonders[id] != 0).
   * (Actually, this function is not currently used for either Manhatten
   * or Apollo.))
   *
   * Otherwise the player who owns the city needs to have it to
   * get the effect.
   */
  if (id==B_MANHATTEN) 
    return (game.global_wonders[id] != 0);
  
  tmp = player_find_city_by_id(city_owner(pcity), game.global_wonders[id]);
  if (!tmp)
    return FALSE;
  switch (id) {
  case B_ASMITHS:
  case B_APOLLO:
  case B_CURE:
  case B_GREAT:
  case B_WALL:
  case B_HANGING:
  case B_ORACLE:
  case B_UNITED:
  case B_WOMENS:
  case B_DARWIN:
  case B_LIGHTHOUSE:
  case B_MAGELLAN:
  case B_MICHELANGELO:
  case B_SETI:
  case B_PYRAMIDS:
  case B_LIBERTY:
  case B_SUNTZU:
    return TRUE;
  case B_ISAAC:
  case B_COPERNICUS:
  case B_SHAKESPEARE:
  case B_COLLOSSUS:
  case B_RICHARDS:
    return FALSE;
  case B_HOOVER:
  case B_BACH:
    if (improvement_variant(id)==1) {
      return (map_get_continent(tmp->x, tmp->y) ==
	      map_get_continent(pcity->x, pcity->y));
    } else {
      return TRUE;
    }
  default:
    return FALSE;
  }
}

/**************************************************************************
...
**************************************************************************/
int city_happy(struct city *pcity)
{
  return (pcity->ppl_happy[4] >= (pcity->size + 1) / 2 &&
	  pcity->ppl_unhappy[4] == 0 && pcity->ppl_angry[4] == 0);
}

/**************************************************************************
...
**************************************************************************/
int city_unhappy(struct city *pcity)
{
  return (pcity->ppl_happy[4] <
	  pcity->ppl_unhappy[4] + 2 * pcity->ppl_angry[4]);
}

/**************************************************************************
...
**************************************************************************/
int base_city_celebrating(struct city *pcity)
{
  return (pcity->size >= get_gov_pcity(pcity)->rapture_size
	  && pcity->was_happy);
}

/**************************************************************************
cities celebrate only after consecutive happy turns
**************************************************************************/
int city_celebrating(struct city *pcity)
{
  return base_city_celebrating(pcity) && city_happy(pcity);
}

/**************************************************************************
.rapture is checked instead of city_celebrating() because this function is
called after .was_happy was updated.
**************************************************************************/
int city_rapture_grow(struct city *pcity)
{
  struct government *g = get_gov_pcity(pcity);
  return (pcity->rapture>0 && pcity->food_surplus>0 &&
	  government_has_flag(g, G_RAPTURE_CITY_GROWTH));
}

/**************************************************************************
...
**************************************************************************/
struct city *city_list_find_id(struct city_list *This, int id)
{
  if(id) {
    struct genlist_iterator myiter;

    genlist_iterator_init(&myiter, &This->list, 0);
    
    for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter))
    if(((struct city *)ITERATOR_PTR(myiter))->id==id)
	return ITERATOR_PTR(myiter);
  }
  return NULL;
}

/**************************************************************************
...
**************************************************************************/
struct city *city_list_find_name(struct city_list *This, char *name)
{
  struct genlist_iterator myiter;

  genlist_iterator_init(&myiter, &This->list, 0);

  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter))
    if(!mystrcasecmp(name, ((struct city *)ITERATOR_PTR(myiter))->name))
      return ITERATOR_PTR(myiter);

  return NULL;
}

/**************************************************************************
Comparison function for qsort for city _pointers_, sorting by city name.
Args are really (struct city**), to sort an array of pointers.
(Compare with old_city_name_compare() in game.c, which use city_id's)
**************************************************************************/
int city_name_compare(const void *p1, const void *p2)
{
  return mystrcasecmp( (*(const struct city**)p1)->name,
		       (*(const struct city**)p2)->name );
}

/**************************************************************************
...
**************************************************************************/
int citygov_free_shield(struct city *pcity, struct government *gov)
{
  if (gov->free_shield == G_CITY_SIZE_FREE) {
    return pcity->size;
  } else {
    return gov->free_shield;
  }
}

/**************************************************************************
...
**************************************************************************/
int citygov_free_happy(struct city *pcity, struct government *gov)
{
  if (gov->free_happy == G_CITY_SIZE_FREE) {
    return pcity->size;
  } else {
    return gov->free_happy;
  }
}

/**************************************************************************
...
**************************************************************************/
int citygov_free_food(struct city *pcity, struct government *gov)
{
  if (gov->free_food == G_CITY_SIZE_FREE) {
    return pcity->size;
  } else {
    return gov->free_food;
  }
}

/**************************************************************************
...
**************************************************************************/
int citygov_free_gold(struct city *pcity, struct government *gov)
{
  if (gov->free_gold == G_CITY_SIZE_FREE) {
    return pcity->size;
  } else {
    return gov->free_gold;
  }
}

/**************************************************************************
Evaluate which style should be used to draw a city.
**************************************************************************/
int get_city_style(struct city *pcity)
{
  return get_player_city_style(city_owner(pcity));
}

/**************************************************************************
...
**************************************************************************/
int get_player_city_style(struct player *plr)
{
  int replace, style, prev;

  style = plr->city_style;
  prev = style;

  while ( (replace = city_styles[prev].replaced_by) != -1) {
    prev = replace;
    if (get_invention( plr, city_styles[replace].techreq) == TECH_KNOWN)
      style = replace;
  }
  return style;
}

/**************************************************************************
Get index to city_styles for style name.
**************************************************************************/
int get_style_by_name(char *style_name)
{
  int i;

  for( i=0; i<game.styles_count; i++) {
    if( !strcmp(style_name,city_styles[i].name) ) 
      break;
  }
  if( i < game.styles_count )
    return i;
  else
    return -1;
}

/**************************************************************************
 Compute and optionally apply the change-production penalty for the given
 production change (to target,is_unit) in the given city (pcity).
 Always returns the number of shields which would be in the stock if
 the penalty had been applied.

 If we switch the "class" of the target sometime after a city has produced
 (i.e., not on the turn immediately after), then there's a shield loss.
 But only on the first switch that turn.  Also, if ever change back to
 original improvement class of this turn, restore lost production.
**************************************************************************/
int city_change_production_penalty(struct city *pcity,
				   int target, int is_unit, int apply_it)
{
  int shield_stock_after_adjustment;
  enum production_class_type orig_class;
  enum production_class_type new_class;
  int put_penalty; /* boolean */

  if (pcity->changed_from_is_unit)
    orig_class=TYPE_UNIT;
  else if (is_wonder(pcity->changed_from_id))
    orig_class=TYPE_WONDER;
  else
    orig_class=TYPE_NORMAL_IMPROVEMENT;

  if (is_unit)
    new_class=TYPE_UNIT;
  else if (is_wonder(target))
    new_class=TYPE_WONDER;
  else
    new_class=TYPE_NORMAL_IMPROVEMENT;

  put_penalty = (orig_class != new_class &&
                 game_next_year(pcity->turn_last_built) < game.year)? 1 : 0;

  if (put_penalty)
    shield_stock_after_adjustment = pcity->before_change_shields/2;
  else
    shield_stock_after_adjustment = pcity->before_change_shields;

  /* Do not put penalty on these. It shouldn't matter whether you disband unit
     before or after changing production...*/
  shield_stock_after_adjustment += pcity->disbanded_shields;

  if (new_class == TYPE_WONDER) /* No penalty! */
    shield_stock_after_adjustment += pcity->caravan_shields;
  else /* Same as you had disbanded caravans. 50 percent penalty is logical! */
    shield_stock_after_adjustment += pcity->caravan_shields/2;

  if (apply_it) {
    pcity->shield_stock = shield_stock_after_adjustment;

    if (new_class != orig_class) {
      /* This is buggy; the interval between turns is not constant. */
      pcity->turn_changed_target = game.year; 
    } else {
      /* Pretend we have changed nothing */
      pcity->turn_changed_target = GAME_START_YEAR;
    }
  }

  return shield_stock_after_adjustment;
}

/**************************************************************************
 Calculates the turns which are needed to build the requested
 improvement in the city.  GUI Independent.
**************************************************************************/
int city_turns_to_build(struct city *pcity, int id, int id_is_unit,
                        int include_shield_stock )
{
  int city_shield_surplus = pcity->shield_surplus;
  int city_shield_stock = include_shield_stock ?
      city_change_production_penalty(pcity, id, id_is_unit, FALSE) : 0;
  int improvement_cost = id_is_unit ?
    get_unit_type(id)->build_cost : get_improvement_type(id)->build_cost;

  if (include_shield_stock && (city_shield_stock >= improvement_cost)) {
    return 1;
  } else if (city_shield_surplus > 0) {
    return
      (improvement_cost - city_shield_stock + city_shield_surplus - 1) /
      city_shield_surplus;
  } else {
    return 999;
  }
}

/**************************************************************************
 is there an enemy city on this tile?
**************************************************************************/
struct city *is_enemy_city_tile(struct tile *ptile, struct player *pplayer)
{
  struct city *pcity = ptile->city;

  if (pcity && pplayers_at_war(pplayer, city_owner(pcity)))
    return pcity;
  else
    return NULL;
}

/**************************************************************************
 is there an friendly city on this tile?
**************************************************************************/
struct city *is_allied_city_tile(struct tile *ptile,
				 struct player *pplayer)
{
  struct city *pcity = ptile->city;

  if (pcity && pplayers_allied(pplayer, city_owner(pcity)))
    return pcity;
  else
    return NULL;
}

/**************************************************************************
 is there an enemy city on this tile?
**************************************************************************/
struct city *is_non_attack_city_tile(struct tile *ptile,
				     struct player *pplayer)
{
  struct city *pcity = ptile->city;

  if (pcity && pplayers_non_attack(pplayer, city_owner(pcity)))
    return pcity;
  else
    return NULL;
}

/**************************************************************************
 is there an non_allied city on this tile?
**************************************************************************/
struct city *is_non_allied_city_tile(struct tile *ptile,
				     struct player *pplayer)
{
  struct city *pcity = ptile->city;

  if (pcity && !pplayers_allied(pplayer, city_owner(pcity)))
    return pcity;
  else
    return NULL;
}

/**************************************************************************
...
**************************************************************************/
int is_unit_near_a_friendly_city(struct unit *punit)
{
  return is_friendly_city_near(unit_owner(punit), punit->x, punit->y);
}

/**************************************************************************
...
**************************************************************************/
int is_friendly_city_near(struct player *owner, int x, int y)
{
  square_iterate (x, y, 3, x1, y1) {
    struct city * pcity = map_get_city (x1, y1);
    if (pcity && pplayers_allied(owner, city_owner(pcity)))
      return TRUE;
  } square_iterate_end;

  return FALSE;
}

/**************************************************************************
Return true iff a city exists within a city radius of the given location.
may_be_on_center determines if a city at x,y counts
**************************************************************************/
int city_exists_within_city_radius(int x, int y, int may_be_on_center)
{
  map_city_radius_iterate(x, y, x1, y1) {
    if (may_be_on_center || x != x1 || y != y1) {
      if (map_get_city(x1, y1)) {
	return TRUE;
      }
    }
  } map_city_radius_iterate_end;

  return FALSE;
}

/**************************************************************************
generalized formula used to calculate granary size.
for now, the AI doesn't understand settings other than the default,
i.e. granary_food_ini=1, granary_food_inc=100.
for more, see food_weighting().
**************************************************************************/
int city_granary_size(int city_size)
{
  return (game.rgame.granary_food_ini * game.foodbox) +
    (game.rgame.granary_food_inc * city_size * game.foodbox) / 100;
}

/****************************************************************
 Pretty sprints the info about a production in 4 columns (name, info,
 cost, turns to build). The columns must each have a size of
 column_size bytes.
*****************************************************************/
void id_to_info_row(char *buf[], int column_size, int id, int is_unit,
		    struct city *pcity)
{
  if (is_unit) {
    struct unit_type *ptype;

    my_snprintf(buf[0], column_size, unit_name(id));

    /* from unit.h get_unit_name() */
    ptype = get_unit_type(id);
    if (ptype->fuel) {
      my_snprintf(buf[1], column_size, "%d/%d/%d(%d)",
		  ptype->attack_strength, ptype->defense_strength,
		  ptype->move_rate / 3,
		  (ptype->move_rate / 3) * ptype->fuel);
    } else {
      my_snprintf(buf[1], column_size, "%d/%d/%d",
		  ptype->attack_strength, ptype->defense_strength,
		  ptype->move_rate / 3);
    }
    my_snprintf(buf[2], column_size, "%d", ptype->build_cost);

    if (pcity) {
      my_snprintf(buf[3], column_size, "%d",
		  city_turns_to_build(pcity, id, TRUE, FALSE));
    } else {
      buf[3][0] = 0;
    }
  } else {
    /* Total & turns left meaningless on capitalization */
    if (id == B_CAPITAL) {
      my_snprintf(buf[0], column_size, get_improvement_type(id)->name);
      buf[1][0] = '\0';
      my_snprintf(buf[2], column_size, "---");
      my_snprintf(buf[3], column_size, "---");
    } else {

      my_snprintf(buf[0], column_size, get_improvement_type(id)->name);

      /* from city.c get_impr_name_ex() */
      if (pcity && wonder_replacement(pcity, id)) {
	my_snprintf(buf[1], column_size, "*");
      } else {
	char *state = "";

	if (is_wonder(id)) {
	  state = _("Wonder");
	  if (game.global_wonders[id])
	    state = _("Built");
	  if (wonder_obsolete(id))
	    state = _("Obsolete");
	}
	my_snprintf(buf[1], column_size, state);
      }

      my_snprintf(buf[2], column_size, "%d",
		  get_improvement_type(id)->build_cost);
      if (pcity) {
	my_snprintf(buf[3], column_size, "%d",
		    city_turns_to_build(pcity, id, FALSE, FALSE));
      } else {
	buf[3][0] = 0;
      }
    }
  }
}

/**************************************************************************
v 1.0j code was too rash, only first penalty now!
**************************************************************************/
static int content_citizens(struct player *pplayer)
{
  int cities = city_list_size(&pplayer->cities);
  int content = game.unhappysize;
  int basis = game.cityfactor + get_gov_pplayer(pplayer)->empire_size_mod;
  int step = get_gov_pplayer(pplayer)->empire_size_inc;

  if (cities > basis) {
    content--;
    if (step)
      content -= (cities - basis - 1) / step;
    /* the first penalty is at (basis + 1) cities;
       the next is at (basis + step + 1), _not_ (basis + step) */
  }
  return content;
}

/**************************************************************************
...
**************************************************************************/
static int set_city_shield_bonus(struct city *pcity)
{
  int tmp = 0;
  if (city_got_building(pcity, B_FACTORY)) {
    if (city_got_building(pcity, B_MFG))
      tmp = 100;
    else
      tmp = 50;

    if (city_affected_by_wonder(pcity, B_HOOVER) ||
	city_got_building(pcity, B_POWER) ||
	city_got_building(pcity, B_HYDRO) ||
	city_got_building(pcity, B_NUCLEAR)) tmp *= 1.5;
  }

  pcity->shield_bonus = tmp + 100;
  return (tmp + 100);
}

/**************************************************************************
...
**************************************************************************/
static int set_city_tax_bonus(struct city *pcity)
{
  int tax_bonus = 100;
  if (city_got_building(pcity, B_MARKETPLACE)) {
    tax_bonus += 50;
    if (city_got_building(pcity, B_BANK)) {
      tax_bonus += 50;
      if (city_got_building(pcity, B_STOCK))
	tax_bonus += 50;
    }
  }
  pcity->tax_bonus = tax_bonus;
  return tax_bonus;
}

/**************************************************************************
...
**************************************************************************/
static int get_city_tithes_bonus(struct city *pcity)
{
  int tithes_bonus = 0;

  if (!government_has_flag
      (get_gov_pcity(pcity), G_CONVERT_TITHES_TO_MONEY)) {
    return 0;
  }

  if (city_got_building(pcity, B_TEMPLE))
    tithes_bonus += get_temple_power(pcity);
  if (city_got_building(pcity, B_COLOSSEUM))
    tithes_bonus += get_colosseum_power(pcity);
  if (city_got_effect(pcity, B_CATHEDRAL))
    tithes_bonus += get_cathedral_power(pcity);
  if (city_affected_by_wonder(pcity, B_BACH))
    tithes_bonus += 2;
  if (city_affected_by_wonder(pcity, B_CURE))
    tithes_bonus += 1;

  return tithes_bonus;
}

/**************************************************************************
...
**************************************************************************/
static int set_city_science_bonus(struct city *pcity)
{
  int science_bonus = 100;
  if (city_got_building(pcity, B_LIBRARY)) {
    science_bonus += 50;
    if (city_got_building(pcity, B_UNIVERSITY)) {
      science_bonus += 50;
    }
    if (city_got_effect(pcity, B_RESEARCH))
      science_bonus += 50;
  }
  if (city_affected_by_wonder(pcity, B_COPERNICUS))
    science_bonus += 50;
  if (city_affected_by_wonder(pcity, B_ISAAC))
    science_bonus += 100;
  pcity->science_bonus = science_bonus;
  return science_bonus;
}

/**************************************************************************
calculate the incomes according to the taxrates and # of specialists.
**************************************************************************/
static void set_tax_income(struct city *pcity)
{
  int sci = 0, tax = 0, lux = 0, rate;
  int sci_rate = city_owner(pcity)->economic.science;
  int lux_rate = city_owner(pcity)->economic.luxury;
  int tax_rate = 100 - sci_rate - lux_rate;

  pcity->science_total = 0;
  pcity->luxury_total = 0;
  pcity->tax_total = 0;
  rate = pcity->trade_prod;
  if (government_has_flag(get_gov_pcity(pcity), G_REDUCED_RESEARCH)) {
    if (sci_rate > 50) {
      sci_rate = 50;
      tax_rate = 100 - sci_rate - lux_rate;
    }
  }

  while (rate) {
    if (get_gov_pcity(pcity)->index != game.government_when_anarchy) {
      tax += tax_rate;
      sci += sci_rate;
      lux += lux_rate;
    } else {			/* ANARCHY */
      lux += 100;
    }
    if (tax >= 100 && rate) {
      tax -= 100;
      pcity->tax_total++;
      rate--;
    }
    if (sci >= 100 && rate) {
      sci -= 100;
      pcity->science_total++;
      rate--;
    }
    if (lux >= 100 && rate) {
      lux -= 100;
      pcity->luxury_total++;
      rate--;
    }
  }
  pcity->luxury_total += (pcity->ppl_elvis * 2);
  pcity->science_total += (pcity->ppl_scientist * 3);
  pcity->tax_total +=
      (pcity->ppl_taxman * 3) + get_city_tithes_bonus(pcity);
}

/**************************************************************************
Modify the incomes according to various buildings. 
**************************************************************************/
static void add_buildings_effect(struct city *pcity)
{
  int tax_bonus, science_bonus;
  int shield_bonus;

  /* this is the place to set them */
  tax_bonus = set_city_tax_bonus(pcity);	
  science_bonus = set_city_science_bonus(pcity);
  shield_bonus = set_city_shield_bonus(pcity);

  if (government_has_flag(get_gov_pcity(pcity), G_REDUCED_RESEARCH)) {
    science_bonus /= 2;
  }

  pcity->shield_prod = (pcity->shield_prod * shield_bonus) / 100;
  pcity->luxury_total = (pcity->luxury_total * tax_bonus) / 100;
  pcity->tax_total = (pcity->tax_total * tax_bonus) / 100;
  pcity->science_total = (pcity->science_total * science_bonus) / 100;
  pcity->shield_surplus = pcity->shield_prod;
}

/**************************************************************************
...
**************************************************************************/
static void happy_copy(struct city *pcity, int i)
{
  pcity->ppl_angry[i + 1] = pcity->ppl_angry[i];
  pcity->ppl_unhappy[i + 1] = pcity->ppl_unhappy[i];
  pcity->ppl_content[i + 1] = pcity->ppl_content[i];
  pcity->ppl_happy[i + 1] = pcity->ppl_happy[i];
}

/**************************************************************************
...
**************************************************************************/
static void citizen_happy_size(struct city *pcity)
{
  int workers, tmp;

  workers = pcity->size - city_specialists(pcity);
  tmp = content_citizens(city_owner(pcity));
  pcity->ppl_content[0] = MAX(0, MIN(workers, tmp));
  if (game.angrycitizen == 0)
    pcity->ppl_angry[0] = 0;
  else
    pcity->ppl_angry[0] = MIN(MAX(0, -tmp), pcity->size);
  pcity->ppl_unhappy[0] =
      workers - pcity->ppl_content[0] - pcity->ppl_angry[0];
  pcity->ppl_happy[0] = 0;	/* no one is born happy */
}

/**************************************************************************
...
**************************************************************************/
static void citizen_happy_luxury(struct city *pcity)
{
  int x = pcity->luxury_total;

  happy_copy(pcity, 0);

  /* make people happy: 
     angry citizen are eliminated first,
     then content are made happy, then unhappy content, etc.  
     each conversions costs 2 luxuries. */
  while (x >= 2 && pcity->ppl_angry[1]) {
    pcity->ppl_angry[1]--;
    pcity->ppl_unhappy[1]++;
    x -= 2;
  }
  while (x >= 2 && pcity->ppl_content[1]) {
    pcity->ppl_content[1]--;
    pcity->ppl_happy[1]++;
    x -= 2;
  }
  while (x >= 4 && pcity->ppl_unhappy[1]) {
    pcity->ppl_unhappy[1]--;
    pcity->ppl_happy[1]++;
    x -= 4;
  }
  if (x >= 2 && pcity->ppl_unhappy[1]) {
    pcity->ppl_unhappy[1]--;
    pcity->ppl_content[1]++;
    x -= 2;
  }
}

/**************************************************************************
...
 Note Suffrage/Police is always dealt with elsewhere now --dwp 
**************************************************************************/
static void citizen_happy_units(struct city *pcity, int unhap)
{
  while (unhap > 0 && pcity->ppl_content[3]) {
    pcity->ppl_content[3]--;
    pcity->ppl_unhappy[3]++;
    unhap--;
  }
  while (unhap >= 2 && pcity->ppl_happy[3]) {
    pcity->ppl_happy[3]--;
    pcity->ppl_unhappy[3]++;
    unhap -= 2;
  }
  if (unhap > 0) {
    if (pcity->ppl_happy[3] > 0) {	/* 1 unhap left */
      pcity->ppl_happy[3]--;
      pcity->ppl_content[3]++;
      unhap--;
    }
    /* everyone is unhappy now, units don't make angry citizen */
  }
}

/**************************************************************************
...
**************************************************************************/
static void citizen_happy_buildings(struct city *pcity)
{
  struct government *g = get_gov_pcity(pcity);
  int faces = 0;
  happy_copy(pcity, 1);

  if (city_got_building(pcity, B_TEMPLE)) {
    faces += get_temple_power(pcity);
  }
  if (city_got_building(pcity, B_COURTHOUSE) && g->corruption_level == 0) {
    faces++;
  }

  if (city_got_building(pcity, B_COLOSSEUM))
    faces += get_colosseum_power(pcity);
  if (city_got_effect(pcity, B_CATHEDRAL))
    faces += get_cathedral_power(pcity);
  /* make people content (but not happy):
     get rid of angry first, then make unhappy content. */
  while (faces && pcity->ppl_angry[2]) {
    pcity->ppl_angry[2]--;
    pcity->ppl_unhappy[2]++;
    faces--;
  }
  while (faces && pcity->ppl_unhappy[2]) {
    pcity->ppl_unhappy[2]--;
    pcity->ppl_content[2]++;
    faces--;
  }
/* no longer hijacking ppl_content[0]; seems no longer to be helpful -- Syela */
  /* TV doesn't make people happy just content...

     while (faces && pcity->ppl_content[2]) { 
     pcity->ppl_content[2]--;
     pcity->ppl_happy[2]++;
     faces--;
     }

   */
}

/**************************************************************************
...
**************************************************************************/
static void citizen_happy_wonders(struct city *pcity)
{
  int bonus = 0;

  happy_copy(pcity, 3);

  if (city_affected_by_wonder(pcity, B_HANGING)) {
    bonus += 1;
    if (city_got_building(pcity, B_HANGING))
      bonus += 2;
    while (bonus && pcity->ppl_content[4]) {
      pcity->ppl_content[4]--;
      pcity->ppl_happy[4]++;
      bonus--;
      /* well i'm not sure what to do with the rest, 
         will let it make unhappy content */
    }
  }
  if (city_affected_by_wonder(pcity, B_BACH))
    bonus += 2;
  if (city_affected_by_wonder(pcity, B_CURE))
    bonus += 1;
  /* get rid of angry first, then make unhappy content */
  while (bonus && pcity->ppl_angry[4]) {
    pcity->ppl_angry[4]--;
    pcity->ppl_unhappy[4]++;
    bonus--;
  }
  while (bonus && pcity->ppl_unhappy[4]) {
    pcity->ppl_unhappy[4]--;
    pcity->ppl_content[4]++;
    bonus--;
  }
  if (city_affected_by_wonder(pcity, B_SHAKESPEARE)) {
    pcity->ppl_content[4] += pcity->ppl_unhappy[4] + pcity->ppl_angry[4];
    pcity->ppl_unhappy[4] = 0;
    pcity->ppl_angry[4] = 0;
  }
  if (government_has_flag(get_gov_pcity(pcity), G_NO_UNHAPPY_CITIZENS)) {
    pcity->ppl_content[4] += pcity->ppl_unhappy[4] + pcity->ppl_angry[4];
    pcity->ppl_unhappy[4] = 0;
    pcity->ppl_angry[4] = 0;
  }
}

/**************************************************************************
...
**************************************************************************/
static void unhappy_city_check(struct city *pcity)
{
  if (city_unhappy(pcity)) {
    pcity->food_surplus = MIN(0, pcity->food_surplus);
    pcity->tax_total = 0;
    pcity->science_total = 0;
    pcity->shield_surplus = MIN(0, pcity->shield_surplus);
  }
}



/**************************************************************************
...
**************************************************************************/
static void set_pollution(struct city *pcity)
{
  int poppul = 0;
  struct player *pplayer = city_owner(pcity);

  pcity->pollution = pcity->shield_prod;
  if (city_got_building(pcity, B_RECYCLING))
    pcity->pollution /= 3;
  else if (city_got_building(pcity, B_HYDRO) ||
	   city_affected_by_wonder(pcity, B_HOOVER) ||
	   city_got_building(pcity, B_NUCLEAR))
    pcity->pollution /= 2;

  if (!city_got_building(pcity, B_MASS)) {
    int mod =
	player_knows_techs_with_flag(pplayer, TF_POPULATION_POLLUTION_INC);
    /* was: A_INDUSTRIALIZATION, A_AUTOMOBILE, A_MASS, A_PLASTICS */
    poppul = (pcity->size * mod) / 4;
    pcity->pollution += poppul;
  }

  pcity->pollution = MAX(0, pcity->pollution - 20);
}

/**************************************************************************
...
**************************************************************************/
static void set_food_trade_shields(struct city *pcity)
{
  int i;
  int is_celebrating = base_city_celebrating(pcity);

  pcity->food_prod = 0;
  pcity->shield_prod = 0;
  pcity->trade_prod = 0;

  pcity->food_surplus = 0;
  pcity->shield_surplus = 0;
  pcity->corruption = 0;

  city_map_iterate(x, y) {
    if (get_worker_city(pcity, x, y) == C_TILE_WORKER) {
      pcity->food_prod +=
	  base_city_get_food_tile(x, y, pcity, is_celebrating);
      pcity->shield_prod +=
	  base_city_get_shields_tile(x, y, pcity, is_celebrating);
      pcity->trade_prod +=
	  base_city_get_trade_tile(x, y, pcity, is_celebrating);
    }
  }
  city_map_iterate_end;
  pcity->tile_trade = pcity->trade_prod;

  pcity->food_surplus = pcity->food_prod - pcity->size * 2;

  for (i = 0; i < 4; i++) {
    pcity->trade_value[i] =
	trade_between_cities(pcity, find_city_by_id(pcity->trade[i]));
    pcity->trade_prod += pcity->trade_value[i];
  }
  pcity->corruption = city_corruption(pcity, pcity->trade_prod);
  pcity->trade_prod -= pcity->corruption;
}

/**************************************************************************
...
**************************************************************************/
static void city_support(struct city *pcity)
{
  struct government *g = get_gov_pcity(pcity);

  int have_police = city_got_effect(pcity, B_POLICE);
  int variant = improvement_variant(B_WOMENS);

  int free_happy = citygov_free_happy(pcity, g);
  int free_shield = citygov_free_shield(pcity, g);
  int free_food = citygov_free_food(pcity, g);
  int free_gold = citygov_free_gold(pcity, g);

  if (variant == 0 && have_police) {
    /* ??  This does the right thing for normal Republic and Democ -- dwp */
    free_happy += g->unit_happy_cost_factor;
  }

  happy_copy(pcity, 2);

  /*
   * If you modify anything here these places might also need updating:
   * - ai/aitools.c : ai_assess_military_unhappiness
   *   Military discontentment evaluation for AI.
   *
   * P.S.  This list is by no means complete.
   * --SKi
   */

  /* military units in this city (need _not_ be home city) can make
     unhappy citizens content
   */
  if (g->martial_law_max > 0) {
    int city_units = 0;
    unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit) {
      if (city_units < g->martial_law_max && is_military_unit(punit)
	  && punit->owner == pcity->owner)
	city_units++;
    }
    unit_list_iterate_end;
    city_units *= g->martial_law_per;
    /* get rid of angry first, then make unhappy content */
    while (city_units > 0 && pcity->ppl_angry[3]) {
      pcity->ppl_angry[3]--;
      pcity->ppl_unhappy[3]++;
      city_units--;
    }
    while (city_units > 0 && pcity->ppl_unhappy[3]) {
      pcity->ppl_unhappy[3]--;
      pcity->ppl_content[3]++;
      city_units--;
    }
  }

  /* loop over units, subtracting appropriate amounts of food, shields,
   * gold etc -- SKi */
  unit_list_iterate(pcity->units_supported, this_unit) {
    struct unit_type *ut = unit_type(this_unit);
    int shield_cost = utype_shield_cost(ut, g);
    int happy_cost = utype_happy_cost(ut, g);
    int food_cost = utype_food_cost(ut, g);
    int gold_cost = utype_gold_cost(ut, g);

    /* Save old values so we can decide if the unit info should be resent */
    int old_unhappiness = this_unit->unhappiness;
    int old_upkeep = this_unit->upkeep;
    int old_upkeep_food = this_unit->upkeep_food;
    int old_upkeep_gold = this_unit->upkeep_gold;

    /* set current upkeep on unit to zero */
    this_unit->unhappiness = 0;
    this_unit->upkeep = 0;
    this_unit->upkeep_food = 0;
    this_unit->upkeep_gold = 0;

    /* This is how I think it should work (dwp)
     * Base happy cost (unhappiness) assumes unit is being aggressive;
     * non-aggressive units don't pay this, _except_ that field units
     * still pay 1.  Should this be always 1, or modified by other
     * factors?   Will treat as flat 1.
     */
    if (happy_cost > 0 && !unit_being_aggressive(this_unit)) {
      if (is_field_unit(this_unit)) {
	happy_cost = 1;
      } else {
	happy_cost = 0;
      }
    }
    if (happy_cost > 0 && variant == 1 && have_police) {
      happy_cost--;
    }

    /* subtract values found above from city's resources -- SKi */
    if (happy_cost > 0) {
      adjust_city_free_cost(&free_happy, &happy_cost);
      if (happy_cost > 0) {
	citizen_happy_units(pcity, happy_cost);
	this_unit->unhappiness = happy_cost;
      }
    }
    if (shield_cost > 0) {
      adjust_city_free_cost(&free_shield, &shield_cost);
      if (shield_cost > 0) {
	pcity->shield_surplus -= shield_cost;
	this_unit->upkeep = shield_cost;
      }
    }
    if (food_cost > 0) {
      adjust_city_free_cost(&free_food, &food_cost);
      if (food_cost > 0) {
	pcity->food_surplus -= food_cost;
	this_unit->upkeep_food = food_cost;
      }
    }
    if (gold_cost > 0) {
      adjust_city_free_cost(&free_gold, &gold_cost);
      if (gold_cost > 0) {
	/* FIXME: This is not implemented -- SKi */
	this_unit->upkeep_gold = gold_cost;
      }
    }

    /* Send unit info if anything has changed */
    if ((this_unit->unhappiness != old_unhappiness
	 || this_unit->upkeep != old_upkeep
	 || this_unit->upkeep_food != old_upkeep_food
	 || this_unit->upkeep_gold != old_upkeep_gold) && is_server) {
      send_unit_info(unit_owner(this_unit), this_unit);
    }
  }
  unit_list_iterate_end;
}

/**************************************************************************
...
**************************************************************************/
void generic_city_refresh(struct city *pcity)
{
  set_food_trade_shields(pcity);
  citizen_happy_size(pcity);
  set_tax_income(pcity);	/* calc base luxury, tax & bulbs */
  add_buildings_effect(pcity);	/* marketplace, library wonders.. */
  set_pollution(pcity);
  citizen_happy_luxury(pcity);	/* with our new found luxuries */
  citizen_happy_buildings(pcity);	/* temple cathedral colosseum */
  city_support(pcity);		/* manage settlers, and units */
  citizen_happy_wonders(pcity);	/* happy wonders & fundamentalism */
  unhappy_city_check(pcity);
}

/**************************************************************************
  Here num_free is eg government->free_unhappy, and this_cost is
  the unhappy cost for a single unit.  We subtract this_cost from
  num_free as much as possible. 

  Note this treats the free_cost as number of eg happiness points,
  not number of eg military units.  This seems to make more sense
  and makes results not depend on order of calculation. --dwp
**************************************************************************/
void adjust_city_free_cost(int *num_free, int *this_cost)
{
  if (*num_free <= 0 || *this_cost <= 0) {
    return;
  }
  if (*num_free >= *this_cost) {
    *num_free -= *this_cost;
    *this_cost = 0;
  } else {
    *this_cost -= *num_free;
    *num_free = 0;
  }
}

/**************************************************************************
corruption, corruption is halfed during love the XXX days.
**************************************************************************/
int city_corruption(struct city *pcity, int trade)
{
  struct government *g = get_gov_pcity(pcity);
  struct city *capital;
  int dist;
  int val, trade_penalty;

  assert(game.notradesize < game.fulltradesize);
  if (pcity->size <= game.notradesize) {
    trade_penalty = trade;
  } else if (pcity->size >= game.fulltradesize) {
    trade_penalty = 0;
  } else {
    trade_penalty = trade * (game.fulltradesize - pcity->size) /
	(game.fulltradesize - game.notradesize);
  }

  if (g->corruption_level == 0) {
    return trade_penalty;
  }
  if (g->fixed_corruption_distance) {
    dist = g->fixed_corruption_distance;
  } else {
    capital = find_palace(city_owner(pcity));
    if (!capital)
      dist = 36;
    else {
      int tmp = map_distance(capital->x, capital->y, pcity->x, pcity->y);
      dist = MIN(36, tmp);
    }
  }
  dist =
      dist * g->corruption_distance_factor + g->extra_corruption_distance;
  val = trade * dist / g->corruption_modifier;

  if (city_got_building(pcity, B_COURTHOUSE) ||
      city_got_building(pcity, B_PALACE)) val /= 2;
  val *= g->corruption_level;
  val /= 100;
  val = CLIP(trade_penalty, val, trade);
  return (val);			/* how did y'all let me forget this one? -- Syela */
}

/**************************************************************************
...
**************************************************************************/
int city_specialists(struct city *pcity)
{
  return (pcity->ppl_elvis + pcity->ppl_scientist + pcity->ppl_taxman);
}

/**************************************************************************
...
**************************************************************************/
int get_temple_power(struct city *pcity)
{
  struct player *p = city_owner(pcity);
  int power = 1;
  if (get_invention(p, game.rtech.temple_plus) == TECH_KNOWN)
    power = 2;
  if (city_affected_by_wonder(pcity, B_ORACLE))
    power *= 2;
  return power;
}

/**************************************************************************
...
**************************************************************************/
int get_cathedral_power(struct city *pcity)
{
  struct player *p = city_owner(pcity);
  int power = 3;
  if (get_invention(p, game.rtech.cathedral_minus /*A_COMMUNISM */ ) ==
      TECH_KNOWN) power--;
  if (get_invention(p, game.rtech.cathedral_plus /*A_THEOLOGY */ ) ==
      TECH_KNOWN) power++;
  if (improvement_variant(B_MICHELANGELO) == 1
      && city_affected_by_wonder(pcity, B_MICHELANGELO))
    power *= 2;
  return power;
}

/**************************************************************************
...
**************************************************************************/
int get_colosseum_power(struct city *pcity)
{
  struct player *p = city_owner(pcity);
  int power = 3;
  if (get_invention(p, game.rtech.colosseum_plus /*A_ELECTRICITY */ ) ==
      TECH_KNOWN) power++;
  return power;
}

/**************************************************************************
 Adds an improvement (and its effects) to a city, and sets the global
 arrays if the improvement has effects and/or an equiv_range that
 extend outside of the city.
 N.B. The building is created "active" and its effects "inactive"; however,
      a global wonder (or other building) may render the building redundant,
      and the necessary techs/other effects/buildings may be present to
      activate the effects, so update_all_effects() must be called to resolve
      these dependencies.
**************************************************************************/
void city_add_improvement(struct city *pcity, Impr_Type_id impr)
{
  struct ceff_vector *ceffs[2];
  struct geff_vector *geffs[4];
  int i;

  get_effect_vectors(ceffs, geffs, impr, pcity);
  mark_improvement(pcity,impr,I_ACTIVE);

  /* Add affects at all ranges. */
  for (i=0; ceffs[i]; i++) {
    struct eff_city *eff;
    
    eff		= append_ceff(ceffs[i]);
    eff->impr	= impr;
    eff->active	= 0;
  }

  for (i=0; geffs[i]; i++) {
    struct eff_global *eff;
    
    eff		    = append_geff(geffs[i]);
    eff->eff.impr   = impr;
    eff->eff.active = 0;
    eff->cityid	    = pcity->id;
  }
}

/**************************************************************************
 Removes an improvement (and its effects) from a city, and updates the global
 arrays if the improvement has effects and/or an equiv_range that
 extend outside of the city.
 N.B. Must call update_all_effects() to resolve dependencies.
**************************************************************************/
void city_remove_improvement(struct city *pcity,Impr_Type_id impr)
{
  struct ceff_vector *ceffs[2];
  struct geff_vector *geffs[4];
  int i, j;

  freelog(LOG_DEBUG,"Improvement %s removed from city %s",
          improvement_types[impr].name,pcity->name);
  mark_improvement(pcity,impr,I_NONE);

  /* If the building had a larger equiv_range than just this city, there may
     be other improvements in the same range - so make sure they are restored */
  if (improvement_types[impr].equiv_range>EFR_CITY) {
    players_iterate(pothplayer) {
      city_list_iterate(pothplayer->cities,pothcity) {
        if (pothcity->improvements[impr]!=I_NONE) {
          mark_improvement(pothcity,impr,pothcity->improvements[impr]);
        }
      } city_list_iterate_end;
    } players_iterate_end;
  }

  get_effect_vectors(ceffs, geffs, impr, pcity);

  /* Now remove the effects. */
  for (j=0; ceffs[j]; j++) {
    for (i=0; i<ceff_vector_size(ceffs[j]); i++) {
      struct eff_city *eff=ceff_vector_get(ceffs[j], i);

      if (eff->impr==impr) {
	eff->impr=B_LAST;
	break;
      }
    }
  }

  for (j=0; geffs[j]; j++) {
    for (i=0; i<geff_vector_size(geffs[j]); i++) {
      struct eff_global *eff=geff_vector_get(geffs[j], i);

      if (eff->eff.impr==impr) {
	eff->eff.impr=B_LAST;
	break;
      }
    }
  }
}

/**************************************************************************
Return the status (C_TILE_EMPTY, C_TILE_WORKER or C_TILE_UNAVAILABLE)
of a given map position. If the status is C_TILE_WORKER the city which
uses this tile is also returned. If status isn't C_TILE_WORKER the
city pointer is set to NULL.
**************************************************************************/
void get_worker_on_map_position(int map_x, int map_y, enum city_tile_type
				*result_city_tile_type,
				struct city **result_pcity)
{
  assert(is_real_tile(map_x, map_y));

  *result_pcity = map_get_tile(map_x, map_y)->worked;
  if (*result_pcity) {
    *result_city_tile_type = C_TILE_WORKER;
  } else {
    *result_city_tile_type = C_TILE_EMPTY;
  }
}
