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
#include "mem.h"
#include "packets.h"
#include "support.h"

#include "city.h"

/* start helper functions for generic_city_refresh */
static int content_citizens(struct player *pplayer);
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
static int citygov_free_gold(struct city *pcity, struct government *gov);
static void city_support(struct city *pcity,
			 void (*send_unit_info) (struct player * pplayer,
						 struct unit * punit));
/* end helper functions for generic_city_refresh */

static int improvement_upkeep_asmiths(struct city *pcity, Impr_Type_id i,
				      bool asmiths);

/* Iterate a city map, from the center (the city) outwards */

int city_map_iterate_outwards_indices[CITY_TILES][2] =
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

/**************************************************************************
...
**************************************************************************/
bool is_valid_city_coords(const int city_x, const int city_y)
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
bool is_city_center(int city_x, int city_y)
{
  return (city_x == (CITY_MAP_SIZE / 2))
      && (city_y == (CITY_MAP_SIZE / 2));
}

/**************************************************************************
Finds the city map coordinate for a given map position and a city
center. Returns whether the map position is inside of the city map.
**************************************************************************/
static bool base_map_to_city_map(int *city_map_x, int *city_map_y,
				 int city_center_x, int city_center_y,
				 int map_x, int map_y)
{
  map_distance_vector(city_map_x, city_map_y,
		      city_center_x, city_center_y, map_x, map_y);
  *city_map_x += CITY_MAP_SIZE / 2;
  *city_map_y += CITY_MAP_SIZE / 2;
  return is_valid_city_coords(*city_map_x, *city_map_y);
}

/**************************************************************************
Finds the city map coordinate for a given map position and a
city. Returns whether the map position is inside of the city map.
**************************************************************************/
bool map_to_city_map(int *city_map_x, int *city_map_y,
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
bool base_city_map_to_map(int *map_x, int *map_y,
			 int city_center_x, int city_center_y,
			 int city_map_x, int city_map_y)
{
  assert(is_valid_city_coords(city_map_x, city_map_y));
  *map_x = city_center_x + city_map_x - CITY_MAP_SIZE / 2;
  *map_y = city_center_y + city_map_y - CITY_MAP_SIZE / 2;

  /* We check the border first to avoid doing an unnecessary
   * normalization; this is just an optimization. */
  return (!IS_BORDER_MAP_POS(city_center_x, city_center_y,
                            CITY_MAP_SIZE / 2)
         || normalize_map_pos(map_x, map_y));
}

/**************************************************************************
Finds the map position for a given city map coordinate of a certain
city. Returns true if the map position found is real.
**************************************************************************/
bool city_map_to_map(int *map_x, int *map_y,
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
bool is_worker_here(struct city *pcity, int city_x, int city_y) 
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
bool wonder_replacement(struct city *pcity, Impr_Type_id id)
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
const char *get_impr_name_ex(struct city *pcity, Impr_Type_id id)
{
  static char buffer[256];
  const char *state = NULL;

  if (pcity) {
    switch (pcity->improvements[id]) {
    case I_REDUNDANT:	state = Q_("?redundant:*");	break;
    case I_OBSOLETE:	state = Q_("?obsolete:O");	break;
    default:						break;
    }
  } else if (is_wonder(id)) {
    if (game.global_wonders[id] != 0) {
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
  int cost, build = pcity->shield_stock;

  if (pcity->is_building_unit) {
    cost = unit_buy_gold_cost(pcity->currently_building, build);
  } else {
    cost = impr_buy_gold_cost(pcity->currently_building, build);
  }
  if (build == 0) {
    cost *= 2;
  }
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
bool city_has_terr_spec_gate(struct city *pcity, Impr_Type_id id)
{
  struct impr_type *impr;
  enum tile_terrain_type *terr_gate;
  enum tile_special_type *spec_gate;

  impr = get_improvement_type(id);
  spec_gate = impr->spec_gate;
  terr_gate = impr->terr_gate;

  if (*spec_gate==S_NO_SPECIAL && *terr_gate==T_LAST) return TRUE;

  for (;*spec_gate!=S_NO_SPECIAL;spec_gate++) {
    if (map_has_special(pcity->x, pcity->y, *spec_gate) ||
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
bool can_eventually_build_improvement(struct city *pcity, Impr_Type_id id)
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
static bool could_build_improvement(struct city *pcity, Impr_Type_id id)
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
bool can_build_improvement(struct city *pcity, Impr_Type_id id)
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
bool can_build_unit_direct(struct city *pcity, Unit_Type_id id)
{
  Impr_Type_id impr_req;

  if (!can_player_build_unit_direct(city_owner(pcity), id)) {
    return FALSE;
  }

  /* Check to see if the unit has a building requirement. */
  impr_req = get_unit_type(id)->impr_requirement;
  assert(impr_req <= B_LAST && impr_req >= 0);
  if (impr_req != B_LAST && !city_got_building(pcity, impr_req)) {
    return FALSE;
  }

  /* You can't build naval units inland. */
  if (!is_ocean_near_tile(pcity->x, pcity->y) && is_water_unit(id)) {
    return FALSE;
  }
  return TRUE;
}

/**************************************************************************
Whether given city can build given unit;
returns 0 if unit is obsolete.
**************************************************************************/
bool can_build_unit(struct city *pcity, Unit_Type_id id)
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
bool can_eventually_build_unit(struct city *pcity, Unit_Type_id id)
{
  /* Can the _player_ ever build this unit? */
  if (!can_player_eventually_build_unit(city_owner(pcity), id))
    return FALSE;

  /* Some units can be built only in certain cities -- for instance,
     ships may be built only in cities adjacent to ocean. */
  if (!is_ocean_near_tile(pcity->x, pcity->y) && is_water_unit(id)) {
    return FALSE;
  }

  return TRUE;
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
bool city_got_building(struct city *pcity,  Impr_Type_id id) 
{
  if (!improvement_exists(id))
    return FALSE;
  else 
    return (pcity->improvements[id] != I_NONE);
}

/**************************************************************************
...
**************************************************************************/
int improvement_upkeep(struct city *pcity, Impr_Type_id i) 
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
static int improvement_upkeep_asmiths(struct city *pcity, Impr_Type_id i,
				      bool asmiths)
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
  Calculate the shields for the tile.  If pcity is specified then
  (city_x, city_y) must be valid city coordinates and is_celebrating tells
  whether the city is celebrating.
**************************************************************************/
static int base_get_shields_tile(int map_x, int map_y, struct city *pcity,
				 int city_x, int city_y, bool is_celebrating)
{
  enum tile_special_type spec_t = map_get_special(map_x, map_y);
  enum tile_terrain_type tile_t = map_get_terrain(map_x, map_y);
  int s;

  if (contains_special(spec_t, S_SPECIAL_1)) {
    s = get_tile_type(tile_t)->shield_special_1;
  } else if (contains_special(spec_t, S_SPECIAL_2)) {
    s = get_tile_type(tile_t)->shield_special_2;
  } else {
    s = get_tile_type(tile_t)->shield;
  }

  if (contains_special(spec_t, S_MINE)) {
    s += get_tile_type(tile_t)->mining_shield_incr;
  }

  if (contains_special(spec_t, S_RAILROAD)) {
    s += (s * terrain_control.rail_shield_bonus) / 100;
  }

  if (pcity) {
    struct government *g = get_gov_pcity(pcity);
    int before_penalty = (is_celebrating ? g->celeb_shields_before_penalty
			  : g->shields_before_penalty);

    if (city_affected_by_wonder(pcity, B_RICHARDS)) {
      s++;
    }
    if (is_ocean(tile_t) && city_got_building(pcity, B_OFFSHORE)) {
      s++;
    }

    /* government shield bonus & penalty */
    if (s > 0) {
      s += (is_celebrating ? g->celeb_shield_bonus : g->shield_bonus);
    }
    if (before_penalty > 0 && s > before_penalty) {
      s--;
    }
  }

  if (contains_special(spec_t, S_POLLUTION)) {
    /* The shields here are icky */
    s -= (s * terrain_control.pollution_shield_penalty) / 100;
  }

  if (contains_special(spec_t, S_FALLOUT)) {
    s -= (s * terrain_control.fallout_shield_penalty) / 100;
  }

  if (pcity && is_city_center(city_x, city_y)) {
    s = MAX(s, game.rgame.min_city_center_shield);
  }

  return s;
}

/**************************************************************************
  Calculate the shields produced by the tile.  This obviously won't take
  into account any city or government bonuses.
**************************************************************************/
int get_shields_tile(int map_x, int map_y)
{
  return base_get_shields_tile(map_x, map_y, NULL, -1, -1, FALSE);
}

/**************************************************************************
  Calculate the shields the given tile is capable of producing for the
  city.
**************************************************************************/
int city_get_shields_tile(int city_x, int city_y, struct city *pcity)
{
  return base_city_get_shields_tile(city_x, city_y, pcity,
				    city_celebrating(pcity));
}

/**************************************************************************
  Calculate the shields the given tile would be capable of producing for
  the city if the city's celebration status were as given.

  This can be used to calculate the benefits celebration would give.
**************************************************************************/
int base_city_get_shields_tile(int city_x, int city_y, struct city *pcity,
			       bool is_celebrating)
{
  int map_x, map_y;

  if (!city_map_to_map(&map_x, &map_y, pcity, city_x, city_y)) {
    assert(0);
    return 0;
  }

  return base_get_shields_tile(map_x, map_y, pcity,
			       city_x, city_y, is_celebrating);
}

/**************************************************************************
  Calculate the trade for the tile.  If pcity is specified then
  (city_x, city_y) must be valid city coordinates and is_celebrating tells
  whether the city is celebrating.
**************************************************************************/
static int base_get_trade_tile(int map_x, int map_y, struct city *pcity,
			       int city_x, int city_y, bool is_celebrating)
{
  enum tile_special_type spec_t = map_get_special(map_x, map_y);
  enum tile_terrain_type tile_t = map_get_terrain(map_x, map_y);
  int t;

  if (contains_special(spec_t, S_SPECIAL_1)) {
    t = get_tile_type(tile_t)->trade_special_1;
  } else if (contains_special(spec_t, S_SPECIAL_2)) {
    t = get_tile_type(tile_t)->trade_special_2;
  } else {
    t = get_tile_type(tile_t)->trade;
  }

  if (contains_special(spec_t, S_RIVER) && !is_ocean(tile_t)) {
    t += terrain_control.river_trade_incr;
  }
  if (contains_special(spec_t, S_ROAD)) {
    t += get_tile_type(tile_t)->road_trade_incr;
  }
  if (t > 0) {
    if (contains_special(spec_t, S_RAILROAD)) {
      t += (t * terrain_control.rail_trade_bonus) / 100;
    }

    /* Civ1 specifically documents that Railroad trade increase is before 
     * Democracy/Republic [government in general now -- SKi] bonus  -AJS */
    if (pcity) {
      struct government *g = get_gov_pcity(pcity);
      int before_penalty = (is_celebrating ? g->celeb_trade_before_penalty
			    : g->trade_before_penalty);

      if (t > 0) {
	t += (is_celebrating ? g->celeb_trade_bonus : g->trade_bonus);
      }

      if (city_affected_by_wonder(pcity, B_COLLOSSUS)) {
	t++;
      }

      if (contains_special(spec_t, S_ROAD)
	  && city_got_building(pcity, B_SUPERHIGHWAYS)) {
	t += (t * terrain_control.road_superhighway_trade_bonus) / 100;
      }

      /* government trade penalty -- SKi */
      if (before_penalty > 0 && t > before_penalty) {
	t--;
      }
    }

    if (contains_special(spec_t, S_POLLUTION)) {
      /* The trade here is dirty */
      t -= (t * terrain_control.pollution_trade_penalty) / 100;
    }

    if (contains_special(spec_t, S_FALLOUT)) {
      t -= (t * terrain_control.fallout_trade_penalty) / 100;
    }
  }

  if (pcity && is_city_center(city_x, city_y)) {
    t = MAX(t, game.rgame.min_city_center_trade);
  }

  return t;
}

/**************************************************************************
  Calculate the trade produced by the tile.  This obviously won't take
  into account any city or government bonuses.
**************************************************************************/
int get_trade_tile(int map_x, int map_y)
{
  return base_get_trade_tile(map_x, map_y, NULL, -1, -1, FALSE);
}

/**************************************************************************
  Calculate the trade the given tile is capable of producing for the
  city.
**************************************************************************/
int city_get_trade_tile(int city_x, int city_y, struct city *pcity)
{
  return base_city_get_trade_tile(city_x, city_y,
				  pcity, city_celebrating(pcity));
}

/**************************************************************************
  Calculate the trade the given tile would be capable of producing for
  the city if the city's celebration status were as given.

  This can be used to calculate the benefits celebration would give.
**************************************************************************/
int base_city_get_trade_tile(int city_x, int city_y,
			     struct city *pcity, bool is_celebrating)
{
  int map_x, map_y;

  if (!city_map_to_map(&map_x, &map_y, pcity, city_x, city_y)) {
    assert(0);
    return 0;
  }

  return base_get_trade_tile(map_x, map_y,
			     pcity, city_x, city_y, is_celebrating);
}

/**************************************************************************
  Calculate the food for the tile.  If pcity is specified then
  (city_x, city_y) must be valid city coordinates and is_celebrating tells
  whether the city is celebrating.
**************************************************************************/
static int base_get_food_tile(int map_x, int map_y, struct city *pcity,
			      int city_x, int city_y, bool is_celebrating)
{
  const enum tile_special_type spec_t = map_get_special(map_x, map_y);
  const enum tile_terrain_type tile_t = map_get_terrain(map_x, map_y);
  struct tile_type *type = get_tile_type(tile_t);
  int f;
  const bool auto_water = (pcity && is_city_center(city_x, city_y)
			   && tile_t == type->irrigation_result
			   && terrain_control.may_irrigate);

  if (contains_special(spec_t, S_SPECIAL_1)) {
    f = get_tile_type(tile_t)->food_special_1;
  } else if (contains_special(spec_t, S_SPECIAL_2)) {
    f = get_tile_type(tile_t)->food_special_2;
  } else {
    f = get_tile_type(tile_t)->food;
  }

  if (contains_special(spec_t, S_IRRIGATION) || auto_water) {
    /* The center tile is auto-irrigated. */
    f += type->irrigation_food_incr;

    /* Farmland only affects cities with supermarkets.  The center tile is
     * auto-irrigated. */
    if (pcity
	&& (contains_special(spec_t, S_FARMLAND)
	    || (auto_water
		&& player_knows_techs_with_flag(city_owner(pcity),
						TF_FARMLAND)))
	&& city_got_building(pcity, B_SUPERMARKET)) {
      f += (f * terrain_control.farmland_supermarket_food_bonus) / 100;
    }
  }

  if (contains_special(spec_t, S_RAILROAD)) {
    f += (f * terrain_control.rail_food_bonus) / 100;
  }

  if (pcity) {
    struct government *g = get_gov_pcity(pcity);
    int before_penalty = (is_celebrating ? g->celeb_food_before_penalty
			  : g->food_before_penalty);

    if (is_ocean(tile_t) && city_got_building(pcity, B_HARBOUR)) {
      f++;
    }

    if (f > 0) {
      f += (is_celebrating ? g->celeb_food_bonus : g->food_bonus);
    }
    if (before_penalty > 0 && f > before_penalty) {
      f--;
    }
  }

  if (contains_special(spec_t, S_POLLUTION)) {
    /* The food here is yucky */
    f -= (f * terrain_control.pollution_food_penalty) / 100;
  }
  if (contains_special(spec_t, S_FALLOUT)) {
    f -= (f * terrain_control.fallout_food_penalty) / 100;
  }

  if (pcity && is_city_center(city_x, city_y)) {
    f = MAX(f, game.rgame.min_city_center_food);
  }

  return f;
}

/**************************************************************************
  Calculate the food produced by the tile.  This obviously won't take
  into account any city or government bonuses.
**************************************************************************/
int get_food_tile(int map_x, int map_y)
{
  return base_get_food_tile(map_x, map_y, NULL, -1, -1, FALSE);
}

/**************************************************************************
  Calculate the food the given tile is capable of producing for the
  city.
**************************************************************************/
int city_get_food_tile(int city_x, int city_y, struct city *pcity)
{
  return base_city_get_food_tile(city_x, city_y, pcity,
				 city_celebrating(pcity));
}

/**************************************************************************
  Calculate the food the given tile would be capable of producing for
  the city if the city's celebration status were as given.

  This can be used to calculate the benefits celebration would give.
**************************************************************************/
int base_city_get_food_tile(int city_x, int city_y, struct city *pcity,
			    bool is_celebrating)
{
  int map_x, map_y;

  if (!city_map_to_map(&map_x, &map_y, pcity, city_x, city_y)) {
    assert(0);
    return 0;
  }

  return base_get_food_tile(map_x, map_y, pcity,
			    city_x, city_y, is_celebrating);
}

/**************************************************************************
  Returns TRUE if the given unit can build a city at the given map
  coordinates.  punit is the founding unit, which may be NULL in some
  cases (e.g., cities from huts).
**************************************************************************/
bool city_can_be_built_here(int x, int y, struct unit *punit)
{
  if (punit) {
    enum unit_move_type move_type = unit_type(punit)->move_type;

    /* We allow land units to build land cities and sea units to build
     * ocean cities. */
    if ((move_type == LAND_MOVING && is_ocean(map_get_terrain(x, y)))
	|| (move_type == SEA_MOVING && !is_ocean(map_get_terrain(x, y)))) {
      return FALSE;
    }
  }

  /* game.rgame.min_dist_bw_cities minimum is 1, meaning adjacent is okay */
  square_iterate(x, y, game.rgame.min_dist_bw_cities-1, x1, y1) {
    if (map_get_city(x1, y1)) {
      return FALSE;
    }
  } square_iterate_end;

  return TRUE;
}

/**************************************************************************
  Return TRUE iff the two cities are capable of trade; i.e., if a caravan
  from one city can enter the other to sell its goods.

  See also can_establish_trade_route().
**************************************************************************/
bool can_cities_trade(struct city *pc1, struct city *pc2)
{
  return (pc1 && pc2 && pc1 != pc2
          && (pc1->owner != pc2->owner
	      || map_distance(pc1->x, pc1->y, pc2->x, pc2->y) > 8));
}

/**************************************************************************
  Find the worst (minimum) trade route the city has.  The value of the
  trade route is returned and its position (slot) is put into the slot
  variable.
**************************************************************************/
int get_city_min_trade_route(struct city *pcity, int *slot)
{
  int i, value = pcity->trade_value[0];

  if (slot) {
    *slot = 0;
  }
  /* find min */
  for (i = 1; i < NUM_TRADEROUTES; i++) {
    if (value > pcity->trade_value[i]) {
      if (slot) {
	*slot = i;
      }
      value = pcity->trade_value[i];
    }
  }

  return value;
}

/**************************************************************************
  Returns TRUE iff the two cities can establish a trade route.  We look
  at the distance and ownership of the cities as well as their existing
  trade routes.  Should only be called if you already know that
  can_cities_trade().
**************************************************************************/
bool can_establish_trade_route(struct city *pc1, struct city *pc2)
{
  int trade = -1;

  assert(can_cities_trade(pc1, pc2));

  if (!pc1 || !pc2 || pc1 == pc2
      || have_cities_trade_route(pc1, pc2)) {
    return FALSE;
  }
    
  if (city_num_trade_routes(pc1) == NUM_TRADEROUTES) {
    trade = trade_between_cities(pc1, pc2);
    /* can we replace traderoute? */
    if (get_city_min_trade_route(pc1, NULL) >= trade) {
      return FALSE;
    }
  }
  
  if (city_num_trade_routes(pc2) == NUM_TRADEROUTES) {
    if (trade == -1) {
      trade = trade_between_cities(pc1, pc2);
    }
    /* can we replace traderoute? */
    if (get_city_min_trade_route(pc2, NULL) >= trade) {
      return FALSE;
    }
  }  

  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
int trade_between_cities(struct city *pc1, struct city *pc2)
{
  int bonus = 0;

  if (pc1 && pc2) {
    bonus = (pc1->tile_trade + pc2->tile_trade + 4) / 8;

    /* Double if on different continents. */
    if (map_get_continent(pc1->x, pc1->y) !=
	map_get_continent(pc2->x, pc2->y)) {
      bonus *= 2;
    }

    if (pc1->owner == pc2->owner) {
      bonus /= 2;
    }
  }
  return bonus;
}

/**************************************************************************
 Return number of trade route city has
**************************************************************************/
int city_num_trade_routes(struct city *pcity)
{
  int i, n = 0;

  for (i = 0; i < NUM_TRADEROUTES; i++)
    if(pcity->trade[i] != 0) n++;
  
  return n;
}

/**************************************************************************
  Returns the revenue trade bonus - you get this when establishing a
  trade route and also when you simply sell your trade goods at the
  new city.

  Note if you trade with a city you already have a trade route with,
  you'll only get 1/3 of this value.
**************************************************************************/
int get_caravan_enter_city_trade_bonus(struct city *pc1, struct city *pc2)
{
  int i, tb;

  /* Should this be real_map_distance? */
  tb = map_distance(pc1->x, pc1->y, pc2->x, pc2->y) + 10;
  tb = (tb * (pc1->trade_prod + pc2->trade_prod)) / 24;

  /*  fudge factor to more closely approximate Civ2 behavior (Civ2 is
   * really very different -- this just fakes it a little better) */
  tb *= 3;

  /* Check for technologies that reduce trade revenues. */
  for (i = 0; i < num_known_tech_with_flag(city_owner(pc1),
					   TF_TRADE_REVENUE_REDUCE); i++) {
    tb = (tb * 2) / 3;
  }

  return tb;
}

/**************************************************************************
  Check if cities have an established trade route.
**************************************************************************/
bool have_cities_trade_route(struct city *pc1, struct city *pc2)
{
  int i;
  
  for (i = 0; i < NUM_TRADEROUTES; i++) {
    if (pc1->trade[i] == pc2->id || pc2->trade[i] == pc1->id) {
      /* Looks like they do have a traderoute. */
      return TRUE;
    }
  }
  return FALSE;
}

/*************************************************************************
  Calculate amount of gold remaining in city after paying for buildings 
  and units.
*************************************************************************/
int city_gold_surplus(struct city *pcity)
{
  bool asmiths = city_affected_by_wonder(pcity, B_ASMITHS);
  int cost = 0;

  built_impr_iterate(pcity, i) {
    cost += improvement_upkeep_asmiths(pcity, i, asmiths);
  } built_impr_iterate_end;

  unit_list_iterate(pcity->units_supported, punit) {
    cost += punit->upkeep_gold;
  } unit_list_iterate_end;

  return pcity->tax_total-cost;
}

/**************************************************************************
 Whether a city has an improvement, or the same effect via a wonder.
 (The Impr_Type_id should be an improvement, not a wonder.)
 Note also: city_got_citywalls(), and server/citytools:city_got_barracks()
**************************************************************************/
bool city_got_effect(struct city *pcity, Impr_Type_id id)
{
  return city_got_building(pcity, id) || wonder_replacement(pcity, id);
}

/**************************************************************************
 Whether a city has its own City Walls, or the same effect via a wonder.
**************************************************************************/
bool city_got_citywalls(struct city *pcity)
{
  if (city_got_building(pcity, B_CITY))
    return TRUE;
  return (city_affected_by_wonder(pcity, B_WALL));
}

/**************************************************************************
...
**************************************************************************/
bool city_affected_by_wonder(struct city *pcity, Impr_Type_id id)
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
bool city_happy(struct city *pcity)
{
  return (pcity->ppl_happy[4] >= (pcity->size + 1) / 2 &&
	  pcity->ppl_unhappy[4] == 0 && pcity->ppl_angry[4] == 0);
}

/**************************************************************************
...
**************************************************************************/
bool city_unhappy(struct city *pcity)
{
  return (pcity->ppl_happy[4] <
	  pcity->ppl_unhappy[4] + 2 * pcity->ppl_angry[4]);
}

/**************************************************************************
...
**************************************************************************/
bool base_city_celebrating(struct city *pcity)
{
  return (pcity->size >= get_gov_pcity(pcity)->rapture_size
	  && pcity->was_happy);
}

/**************************************************************************
cities celebrate only after consecutive happy turns
**************************************************************************/
bool city_celebrating(struct city *pcity)
{
  return base_city_celebrating(pcity) && city_happy(pcity);
}

/**************************************************************************
.rapture is checked instead of city_celebrating() because this function is
called after .was_happy was updated.
**************************************************************************/
bool city_rapture_grow(struct city *pcity)
{
  struct government *g = get_gov_pcity(pcity);

  return (pcity->rapture > 0 && pcity->food_surplus > 0
	  && (pcity->rapture % game.rapturedelay) == 0
	  && government_has_flag(g, G_RAPTURE_CITY_GROWTH));
}

/**************************************************************************
...
**************************************************************************/
struct city *city_list_find_id(struct city_list *This, int id)
{
  if (id != 0) {
    city_list_iterate(*This, pcity) {
      if (pcity->id == id) {
	return pcity;
      }
    } city_list_iterate_end;
  }

  return NULL;
}

/**************************************************************************
...
**************************************************************************/
struct city *city_list_find_name(struct city_list *This, const char *name)
{
  city_list_iterate(*This, pcity) {
    if (mystrcasecmp(name, pcity->name) == 0) {
      return pcity;
    }
  } city_list_iterate_end;

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
static int citygov_free_gold(struct city *pcity, struct government *gov)
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
int get_style_by_name(const char *style_name)
{
  int i;

  for (i = 0; i < game.styles_count; i++) {
    if (strcmp(style_name, city_styles[i].name) == 0) {
      break;
    }
  }
  if (i < game.styles_count) {
    return i;
  } else {
    return -1;
  }
}

/**************************************************************************
  Get index to city_styles for untranslated style name.
**************************************************************************/
int get_style_by_name_orig(const char *style_name)
{
  int i;

  for (i = 0; i < game.styles_count; i++) {
    if (strcmp(style_name, city_styles[i].name_orig) == 0) {
      break;
    }
  }
  if (i < game.styles_count) {
    return i;
  } else {
    return -1;
  }
}

/**************************************************************************
  Get name of given city style.
**************************************************************************/
char* get_city_style_name(int style)
{
   return city_styles[style].name;
}


/**************************************************************************
  Get untranslated name of city style.
**************************************************************************/
char* get_city_style_name_orig(int style)
{
   return city_styles[style].name_orig;
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
				   int target, bool is_unit, bool apply_it)
{
  int shield_stock_after_adjustment;
  enum production_class_type orig_class;
  enum production_class_type new_class;
  int unpenalized_shields = 0, penalized_shields = 0;

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

  /* Changing production is penalized under certain circumstances. */
  if (orig_class == new_class) {
    /* There's never a penalty for building something of the same class. */
    unpenalized_shields = pcity->before_change_shields;
  } else if (game_next_year(pcity->turn_last_built) >= game.year) {
    /* Surplus shields from the previous production won't be penalized if
     * you change production on the very next turn.  But you can only use
     * up to the city's surplus amount of shields in this way. */
    unpenalized_shields = MIN(pcity->last_turns_shield_surplus,
			      pcity->before_change_shields);
    penalized_shields = pcity->before_change_shields - unpenalized_shields;
  } else {
    /* Penalize 50% of the production. */
    penalized_shields = pcity->before_change_shields;
  }

  /* Do not put penalty on these. It shouldn't matter whether you disband unit
     before or after changing production...*/
  unpenalized_shields += pcity->disbanded_shields;

  /* Caravan shields are penalized (just as if you disbanded the caravan)
   * if you're not building a wonder. */
  if (new_class == TYPE_WONDER) {
    unpenalized_shields += pcity->caravan_shields;
  } else {
    penalized_shields += pcity->caravan_shields;
  }

  shield_stock_after_adjustment =
    unpenalized_shields + penalized_shields / 2;
  if (apply_it) {
    pcity->shield_stock = shield_stock_after_adjustment;
  }

  return shield_stock_after_adjustment;
}

/**************************************************************************
 Calculates the turns which are needed to build the requested
 improvement in the city.  GUI Independent.
**************************************************************************/
int city_turns_to_build(struct city *pcity, int id, bool id_is_unit,
			bool include_shield_stock)
{
  int city_shield_surplus = pcity->shield_surplus;
  int city_shield_stock = include_shield_stock ?
      city_change_production_penalty(pcity, id, id_is_unit, FALSE) : 0;
  int improvement_cost = id_is_unit ?
    unit_build_shield_cost(id) : impr_build_shield_cost(id);

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
 Calculates the turns which are needed for the city to grow.  A value
 of FC_INFINITY means the city will never grow.  A value of 0 means
 city growth is blocked.  A negative value of -x means the city will
 shrink in x turns.  A positive value of x means the city will grow in
 x turns.
**************************************************************************/
int city_turns_to_grow(struct city *pcity)
{
  if (pcity->food_surplus > 0) {
    return (city_granary_size(pcity->size) - pcity->food_stock +
	    pcity->food_surplus - 1) / pcity->food_surplus;
  } else if (pcity->food_surplus < 0) {
    /* turns before famine loss */
    return -1 + (pcity->food_stock / pcity->food_surplus);
  } else {
    return FC_INFINITY;
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
bool is_unit_near_a_friendly_city(struct unit *punit)
{
  return is_friendly_city_near(unit_owner(punit), punit->x, punit->y);
}

/**************************************************************************
...
**************************************************************************/
bool is_friendly_city_near(struct player *owner, int x, int y)
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
bool city_exists_within_city_radius(int x, int y, bool may_be_on_center)
{
  map_city_radius_iterate(x, y, x1, y1) {
    if (may_be_on_center || !same_pos(x, y, x1, y1)) {
      if (map_get_city(x1, y1)) {
	return TRUE;
      }
    }
  } map_city_radius_iterate_end;

  return FALSE;
}

/****************************************************************************
  Generalized formula used to calculate granary size.

  The AI may not deal well with non-default settings.  See food_weighting().
****************************************************************************/
int city_granary_size(int city_size)
{
  int food_inis = game.rgame.granary_num_inis;
  int food_inc = game.rgame.granary_food_inc;

  /* Granary sizes for the first food_inis citizens are given directly.
   * After that we increase the granary size by food_inc per citizen. */
  if (city_size > food_inis) {
    return (game.rgame.granary_food_ini[food_inis - 1] * game.foodbox +
	    food_inc * (city_size - food_inis) * game.foodbox / 100) ;
  } else {
    return game.rgame.granary_food_ini[city_size - 1] * game.foodbox;
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
    if (step != 0)
      content -= (cities - basis - 1) / step;
    /* the first penalty is at (basis + 1) cities;
       the next is at (basis + step + 1), _not_ (basis + step) */
  }
  return content;
}

/**************************************************************************
 Return the factor (in %) by which the shield should be multiplied.
**************************************************************************/
int get_city_shield_bonus(struct city *pcity)
{
  int shield_bonus = 100;

  if (city_got_building(pcity, B_FACTORY)) {
    shield_bonus += 50;
    if (city_got_building(pcity, B_MFG)) {
      shield_bonus += 50;
    }

    if (city_affected_by_wonder(pcity, B_HOOVER) ||
	city_got_building(pcity, B_POWER) ||
	city_got_building(pcity, B_HYDRO) ||
	city_got_building(pcity, B_NUCLEAR)) {
      shield_bonus = 100 + (3 * (shield_bonus - 100)) / 2;
    }
  }

  return shield_bonus;
}

/**************************************************************************
 Return the factor (in %) by which the tax and luxury should be
 multiplied.
**************************************************************************/
int get_city_tax_bonus(struct city *pcity)
{
  int tax_bonus = 100;

  if (city_got_building(pcity, B_MARKETPLACE)) {
    tax_bonus += 50;
    if (city_got_building(pcity, B_BANK)) {
      tax_bonus += 50;
      if (city_got_building(pcity, B_STOCK)) {
	tax_bonus += 50;
      }
    }
  }

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
  Return the factor (in %) by which the science should be multiplied.
**************************************************************************/
int get_city_science_bonus(struct city *pcity)
{
  int science_bonus = 100;

  if (city_got_building(pcity, B_LIBRARY)) {
    science_bonus += 50;
    if (city_got_building(pcity, B_UNIVERSITY)) {
      science_bonus += 50;
    }
    if (city_got_effect(pcity, B_RESEARCH)) {
      science_bonus += 50;
    }
  }
  if (city_affected_by_wonder(pcity, B_COPERNICUS)) {
    science_bonus += 50;
  }
  if (city_affected_by_wonder(pcity, B_ISAAC)) {
    science_bonus += 100;
  }
  if (government_has_flag(get_gov_pcity(pcity), G_REDUCED_RESEARCH)) {
    science_bonus /= 2;
  }

  return science_bonus;
}

/**************************************************************************
calculate the incomes according to the taxrates and # of specialists.
**************************************************************************/
static void set_tax_income(struct city *pcity)
{
  int sci, tax, lux, rate = pcity->trade_prod;
  int sci_rest, tax_rest, lux_rest;
  int sci_rate = city_owner(pcity)->economic.science;
  int lux_rate = city_owner(pcity)->economic.luxury;
  int tax_rate = 100 - sci_rate - lux_rate;
  
  if (government_has_flag(get_gov_pcity(pcity), G_REDUCED_RESEARCH)) {
    if (sci_rate > 50) {
      sci_rate = 50;
      tax_rate = 100 - sci_rate - lux_rate;
    }
  }

  /* ANARCHY */
  if (get_gov_pcity(pcity)->index == game.government_when_anarchy) {
    sci_rate = 0;
    lux_rate = 100;
    tax_rate = 100 - sci_rate - lux_rate;
  }

  freelog(LOG_DEBUG, "trade_prod=%d, rates=(sci=%d%%, tax=%d%%, lux=%d%%)",
	  pcity->trade_prod, sci_rate, tax_rate, lux_rate);

  /* 
   * Distribution of the trade among science, tax and luxury via a
   * modified Hare/Niemeyer algorithm (also known as "Hamilton's
   * Method"):
   *
   * 1) distributes the whole-numbered part of the three targets
   * 2) sort the remaining fractions (called *_rest)
   * 3) divide the remaining trade among the targets starting with the
   * biggest fraction. If two targets have the same fraction the
   * target with the smaller whole-numbered value is chosen.
   */

  sci = (rate * sci_rate) / 100;
  tax = (rate * tax_rate) / 100;
  lux = (rate * lux_rate) / 100;
  
  /* these are the fractions multiplied by 100 */
  sci_rest = rate * sci_rate - sci * 100;
  tax_rest = rate * tax_rate - tax * 100;
  lux_rest = rate * lux_rate - lux * 100;

  rate -= (sci + tax + lux);  

  freelog(LOG_DEBUG,
	  "  int parts (%d, %d, %d), rest (%d, %d, %d), remaing trade %d",
	  sci, tax, lux, sci_rest, tax_rest, lux_rest, rate);
  
  while (rate > 0) {
    if (sci_rest > lux_rest && sci_rest > tax_rest) {
      sci++;
      sci_rest = 0;
      rate--;
    }
    if (tax_rest > sci_rest && tax_rest > lux_rest && rate > 0) {
      tax++;
      tax_rest = 0;
      rate--;
    }
    if (lux_rest > tax_rest && lux_rest > sci_rest && rate > 0) {
      lux++;
      lux_rest = 0;
      rate--;
    }
    if (sci_rest == tax_rest && sci_rest > lux_rest && rate > 0) {
      if (sci < tax) {
	sci++;
	sci_rest = 0;
	rate--;
      } else {
	tax++;
	tax_rest = 0;
	rate--;
      }
    }
    if (sci_rest == lux_rest && sci_rest > tax_rest && rate > 0) {
      if (sci < lux) {
	sci++;
	sci_rest = 0;
	rate--;
      } else {
	lux++;
	lux_rest = 0;
	rate--;
      }
    }
    if (tax_rest == lux_rest && tax_rest > sci_rest && rate > 0) {
      if (tax < lux) {
	tax++;
	tax_rest = 0;
	rate--;
      } else {
	lux++;
	lux_rest = 0;
	rate--;
      }
    }
  }

  assert(sci + tax + lux == pcity->trade_prod);

  freelog(LOG_DEBUG, "  result (%d, %d, %d)", sci, tax, lux);

  pcity->science_total = sci;
  pcity->tax_total = tax;
  pcity->luxury_total = lux;

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
  /* this is the place to set them */
  pcity->tax_bonus = get_city_tax_bonus(pcity);
  pcity->science_bonus = get_city_science_bonus(pcity);
  pcity->shield_bonus = get_city_shield_bonus(pcity);

  pcity->shield_prod = (pcity->shield_prod * pcity->shield_bonus) / 100;
  pcity->luxury_total = (pcity->luxury_total * pcity->tax_bonus) / 100;
  pcity->tax_total = (pcity->tax_total * pcity->tax_bonus) / 100;
  pcity->science_total = (pcity->science_total * pcity->science_bonus) / 100;
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
  Create content, unhappy and angry citizens.
**************************************************************************/
static void citizen_happy_size(struct city *pcity)
{
  /* Number of specialists in city */
  int specialists = city_specialists(pcity);

  /* This is the number of citizens that may start out content, depending
   * on empire size and game's city unhappysize. This may be bigger than
   * the size of the city, since this is a potential. */
  int content = content_citizens(city_owner(pcity));

  /* Create content citizens. Take specialists from their ranks. */
  pcity->ppl_content[0] = MAX(0, MIN(pcity->size, content) - specialists);

  /* Create angry citizens only if we have a negative number of possible
   * content citizens. This happens when empires grow really big. */
  if (game.angrycitizen == FALSE) {
    pcity->ppl_angry[0] = 0;
  } else {
    pcity->ppl_angry[0] = MIN(MAX(0, -content), pcity->size - specialists);
  }

  /* Create unhappy citizens. In the beginning, all who are not content,
   * specialists or angry are unhappy. This is changed by luxuries and 
   * buildings later. */
  pcity->ppl_unhappy[0] = (pcity->size 
                           - specialists 
                           - pcity->ppl_content[0] 
                           - pcity->ppl_angry[0]);

  /* No one is born happy. */
  pcity->ppl_happy[0] = 0;
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
  while (x >= 2 && pcity->ppl_angry[1] > 0) {
    pcity->ppl_angry[1]--;
    pcity->ppl_unhappy[1]++;
    x -= 2;
  }
  while (x >= 2 && pcity->ppl_content[1] > 0) {
    pcity->ppl_content[1]--;
    pcity->ppl_happy[1]++;
    x -= 2;
  }
  while (x >= 4 && pcity->ppl_unhappy[1] > 0) {
    pcity->ppl_unhappy[1]--;
    pcity->ppl_happy[1]++;
    x -= 4;
  }
  if (x >= 2 && pcity->ppl_unhappy[1] > 0) {
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
  while (unhap > 0 && pcity->ppl_content[3] > 0) {
    pcity->ppl_content[3]--;
    pcity->ppl_unhappy[3]++;
    unhap--;
  }
  while (unhap >= 2 && pcity->ppl_happy[3] > 0) {
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
  while (faces > 0 && pcity->ppl_angry[2] > 0) {
    pcity->ppl_angry[2]--;
    pcity->ppl_unhappy[2]++;
    faces--;
  }
  while (faces > 0 && pcity->ppl_unhappy[2] > 0) {
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
    while (bonus > 0 && pcity->ppl_content[4] > 0) {
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
  while (bonus > 0 && pcity->ppl_angry[4] > 0) {
    pcity->ppl_angry[4]--;
    pcity->ppl_unhappy[4]++;
    bonus--;
  }
  while (bonus > 0 && pcity->ppl_unhappy[4] > 0) {
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
  struct player *pplayer = city_owner(pcity);

  pcity->pollution = pcity->shield_prod;
  if (city_got_building(pcity, B_RECYCLING))
    pcity->pollution /= 3;
  else if (city_got_building(pcity, B_HYDRO) ||
	   city_affected_by_wonder(pcity, B_HOOVER) ||
	   city_got_building(pcity, B_NUCLEAR))
    pcity->pollution /= 2;

  if (!city_got_building(pcity, B_MASS)) {
    pcity->pollution += (pcity->size *
			 num_known_tech_with_flag
			 (pplayer, TF_POPULATION_POLLUTION_INC)) / 4;
  }

  pcity->pollution = MAX(0, pcity->pollution - 20);
}

/**************************************************************************
...
**************************************************************************/
static void set_food_trade_shields(struct city *pcity)
{
  int i;
  bool is_celebrating = base_city_celebrating(pcity);

  pcity->food_prod = 0;
  pcity->shield_prod = 0;
  pcity->trade_prod = 0;

  pcity->food_surplus = 0;
  pcity->shield_surplus = 0;
  pcity->corruption = 0;
  pcity->shield_waste = 0;
  
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

  for (i = 0; i < NUM_TRADEROUTES; i++) {
    pcity->trade_value[i] =
	trade_between_cities(pcity, find_city_by_id(pcity->trade[i]));
    pcity->trade_prod += pcity->trade_value[i];
  }
  pcity->corruption = city_corruption(pcity, pcity->trade_prod);
  pcity->trade_prod -= pcity->corruption;

  pcity->shield_waste = city_waste(pcity, pcity->shield_prod);
  pcity->shield_prod -= pcity->shield_waste;
}

/**************************************************************************
...
**************************************************************************/
static void city_support(struct city *pcity, 
                         void (*send_unit_info) (struct player *pplayer,
                                                  struct unit *punit))
{
  struct government *g = get_gov_pcity(pcity);

  bool have_police = city_got_effect(pcity, B_POLICE);
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
    while (city_units > 0 && pcity->ppl_angry[3] > 0) {
      pcity->ppl_angry[3]--;
      pcity->ppl_unhappy[3]++;
      city_units--;
    }
    while (city_units > 0 && pcity->ppl_unhappy[3] > 0) {
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
    if (send_unit_info
        && (this_unit->unhappiness != old_unhappiness
            || this_unit->upkeep != old_upkeep
            || this_unit->upkeep_food != old_upkeep_food
            || this_unit->upkeep_gold != old_upkeep_gold)) {
      send_unit_info(unit_owner(this_unit), this_unit);
    }
  }
  unit_list_iterate_end;
}

/**************************************************************************
...
**************************************************************************/
void generic_city_refresh(struct city *pcity,
			  bool refresh_trade_route_cities,
			  void (*send_unit_info) (struct player * pplayer,
						  struct unit * punit))
{
  int prev_tile_trade = pcity->tile_trade;

  set_food_trade_shields(pcity);
  citizen_happy_size(pcity);
  set_tax_income(pcity);	/* calc base luxury, tax & bulbs */
  add_buildings_effect(pcity);	/* marketplace, library wonders.. */
  set_pollution(pcity);
  citizen_happy_luxury(pcity);	/* with our new found luxuries */
  citizen_happy_buildings(pcity);	/* temple cathedral colosseum */
  city_support(pcity, send_unit_info);	/* manage settlers, and units */
  citizen_happy_wonders(pcity);	/* happy wonders & fundamentalism */
  unhappy_city_check(pcity);

  if (refresh_trade_route_cities && pcity->tile_trade != prev_tile_trade) {
    int i;

    for (i = 0; i < NUM_TRADEROUTES; i++) {
      struct city *pcity2 = find_city_by_id(pcity->trade[i]);

      if (pcity2) {
	generic_city_refresh(pcity2, FALSE, send_unit_info);
      }
    }
  }
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
  Corruption is halved during love the XXX days.
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
  if (g->fixed_corruption_distance != 0) {
    dist = g->fixed_corruption_distance;
  } else {
    capital = find_palace(city_owner(pcity));
    if (!capital)
      dist = g->corruption_max_distance_cap;
    else {
      int tmp = real_map_distance(capital->x, capital->y, pcity->x, pcity->y);
      dist = MIN(g->corruption_max_distance_cap, tmp);
    }
  }
  dist =
      dist * g->corruption_distance_factor + g->extra_corruption_distance;

  /* Now calculate the final corruption.  Ordered to reduce integer
   * roundoff errors. */
  val = (trade * dist) * g->corruption_level;
  if (city_got_building(pcity, B_COURTHOUSE) ||
      city_got_building(pcity, B_PALACE)) {
    val /= 2;
  }
  val /= 100 * g->corruption_modifier;
  val = CLIP(trade_penalty, val, trade);
  return val;
}

/************************************************************************** 
  Waste is corruption for shields 
**************************************************************************/
int city_waste(struct city *pcity, int shields)
{
  struct government *g = get_gov_pcity(pcity);
  struct city *capital;
  int dist;
  int val, shield_penalty = 0;

  if (g->waste_level == 0) {
    return shield_penalty;
  }
  if (g->fixed_waste_distance != 0) {
    dist = g->fixed_waste_distance;
  } else {
    capital = find_palace(city_owner(pcity));
    if (!capital) {
      dist = g->waste_max_distance_cap;
    } else {
      int tmp = real_map_distance(capital->x, capital->y, pcity->x, pcity->y);
      dist = MIN(g->waste_max_distance_cap, tmp);
    }
  }
  dist = dist * g->waste_distance_factor + g->extra_waste_distance;
  /* Ordered to reduce integer roundoff errors */
  val = shields * dist;
  val *= g->waste_level;
  val /= g->waste_modifier;
  val /= 100;

  if (city_got_building(pcity, B_COURTHOUSE)
      || city_got_building(pcity, B_PALACE)) {
    val /= 2;
  }
  val = CLIP(shield_penalty, val, shields);
  return val;
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
**************************************************************************/
void city_add_improvement(struct city *pcity, Impr_Type_id impr)
{
  struct player *pplayer = city_owner(pcity);

  if (improvement_obsolete(pplayer, impr)) {
    mark_improvement(pcity, impr, I_OBSOLETE);
  } else {
    mark_improvement(pcity, impr, I_ACTIVE);
  }

  improvements_update_redundant(pplayer, pcity, 
                                map_get_continent(pcity->x, pcity->y),
                                improvement_types[impr].equiv_range);
}

/**************************************************************************
 Removes an improvement (and its effects) from a city, and updates the global
 arrays if the improvement has effects and/or an equiv_range that
 extend outside of the city.
**************************************************************************/
void city_remove_improvement(struct city *pcity,Impr_Type_id impr)
{
  struct player *pplayer = city_owner(pcity);
  
  freelog(LOG_DEBUG,"Improvement %s removed from city %s",
          improvement_types[impr].name, pcity->name);
  
  mark_improvement(pcity, impr, I_NONE);

  improvements_update_redundant(pplayer, pcity,
                                map_get_continent(pcity->x, pcity->y),
                                improvement_types[impr].equiv_range);
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
  CHECK_MAP_POS(map_x, map_y);

  *result_pcity = map_get_tile(map_x, map_y)->worked;
  if (*result_pcity) {
    *result_city_tile_type = C_TILE_WORKER;
  } else {
    *result_city_tile_type = C_TILE_EMPTY;
  }
}

/**************************************************************************
 Returns TRUE iff the city has set the given option.
**************************************************************************/
bool is_city_option_set(struct city *pcity, enum city_options option)
{
  return TEST_BIT(pcity->city_options, option);
}

/**************************************************************************
 Allocate memory for this amount of city styles.
**************************************************************************/
void city_styles_alloc(int num)
{
  city_styles = fc_calloc(num, sizeof(struct citystyle));
  game.styles_count = num;
}

/**************************************************************************
 De-allocate the memory used by the city styles.
**************************************************************************/
void city_styles_free(void)
{
  free(city_styles);
  city_styles = NULL;
  game.styles_count = 0;
}

/**************************************************************************
  Create virtual skeleton for a city.  It does not register the city so 
  the id is set to 0.  All other values are more or less sane defaults.
**************************************************************************/
struct city *create_city_virtual(struct player *pplayer, const int x,
                                 const int y, const char *name)
{
  int i;
  struct city *pcity;

  pcity = fc_malloc(sizeof(struct city));

  pcity->id = 0;
  pcity->owner = pplayer->player_no;
  pcity->x = x;
  pcity->y = y;
  sz_strlcpy(pcity->name, name);
  pcity->size = 1;
  pcity->ppl_elvis = 1;
  pcity->ppl_scientist = pcity->ppl_taxman=0;
  pcity->ppl_happy[4] = 0;
  pcity->ppl_content[4] = 1;
  pcity->ppl_unhappy[4] = 0;
  pcity->ppl_angry[4] = 0;
  pcity->was_happy = FALSE;
  pcity->steal = 0;
  for (i = 0; i < NUM_TRADEROUTES; i++) {
    pcity->trade_value[i] = pcity->trade[i] = 0;
  }
  pcity->food_stock = 0;
  pcity->shield_stock = 0;
  pcity->trade_prod = 0;
  pcity->tile_trade = 0;
  pcity->original = pplayer->player_no;

  /* Initialise improvements list */
  improvement_status_init(pcity->improvements,
                          ARRAY_SIZE(pcity->improvements));

  /* Set up the worklist */
  init_worklist(&pcity->worklist);

  {
    int u = best_role_unit(pcity, L_FIRSTBUILD);

    if (u < U_LAST && u >= 0) {
      pcity->is_building_unit = TRUE;
      pcity->currently_building = u;
    } else {
      pcity->is_building_unit = FALSE;
      pcity->currently_building = B_CAPITAL;
    }
  }
  pcity->turn_founded = game.turn;
  pcity->did_buy = TRUE;
  pcity->did_sell = FALSE;
  pcity->airlift = FALSE;

  pcity->turn_last_built = game.year;
  pcity->changed_from_id = 0;
  pcity->changed_from_is_unit = FALSE;
  pcity->before_change_shields = 0;
  pcity->disbanded_shields = 0;
  pcity->caravan_shields = 0;
  pcity->last_turns_shield_surplus = 0;
  pcity->anarchy = 0;
  pcity->rapture = 0;
  pcity->city_options = CITYOPT_DEFAULT;

  pcity->ai.trade_want = 1; /* we always want some */
  memset(pcity->ai.building_want, 0, sizeof(pcity->ai.building_want));
  pcity->ai.danger = 0;
  pcity->ai.urgency = 0;
  pcity->ai.grave_danger = 0;
  pcity->ai.wallvalue = 0;
  pcity->ai.downtown = 0;
  pcity->ai.invasion = 0;
  pcity->ai.bcost = 0;
  pcity->ai.attack = 0;

  pcity->corruption = 0;
  pcity->shield_waste = 0;
  pcity->shield_bonus = 100;
  pcity->tax_bonus = 100;
  pcity->science_bonus = 100;

  unit_list_init(&pcity->units_supported);
  pcity->debug = FALSE;

  return pcity;
}

/**************************************************************************
  Removes the virtual skeleton of a city. You should already have removed
  all buildings and units you have added to the city before this.
**************************************************************************/
void remove_city_virtual(struct city *pcity)
{
  unit_list_unlink_all(&pcity->units_supported);
  free(pcity);
}
