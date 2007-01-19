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
#include <stdlib.h>
#include <string.h>

#include "distribute.h"
#include "fcintl.h"
#include "log.h"
#include "support.h"

#include "game.h"
#include "government.h"
#include "map.h"
#include "mem.h"
#include "packets.h"

#include "cm.h"

#include "city.h"

/* Iterate a city map, from the center (the city) outwards */
struct iter_index *city_map_iterate_outwards_indices;

struct citystyle *city_styles = NULL;

int city_tiles;

/**************************************************************************
  Return TRUE if the given city coordinate pair is "valid"; that is, if it
  is a part of the citymap and thus is workable by the city.
**************************************************************************/
bool is_valid_city_coords(const int city_x, const int city_y)
{
  int dist = map_vector_to_sq_distance(city_x - CITY_MAP_RADIUS,
				       city_y - CITY_MAP_RADIUS);

  /* The city's valid positions are in a circle of radius CITY_MAP_RADIUS
   * around the city center.  Depending on the value of CITY_MAP_RADIUS
   * this circle will be:
   *
   *   333
   *  32223
   * 3211123
   * 3210123
   * 3211123
   *  32223
   *   333
   *
   * So CITY_MAP_RADIUS==2 corresponds to the "traditional" city map.
   *
   * FIXME: this won't work for hexagonal tiles.
   */
  return (CITY_MAP_RADIUS * CITY_MAP_RADIUS + 1 >= dist);
}

/**************************************************************************
  Finds the city map coordinate for a given map position and a city
  center. Returns whether the map position is inside of the city map.
**************************************************************************/
static bool base_map_to_city_map(int *city_map_x, int *city_map_y,
				 const struct tile *city_tile,
				 const struct tile *map_tile)
{
  map_distance_vector(city_map_x, city_map_y, city_tile, map_tile);
  *city_map_x += CITY_MAP_RADIUS;
  *city_map_y += CITY_MAP_RADIUS;
  return is_valid_city_coords(*city_map_x, *city_map_y);
}

/**************************************************************************
Finds the city map coordinate for a given map position and a
city. Returns whether the map position is inside of the city map.
**************************************************************************/
bool map_to_city_map(int *city_map_x, int *city_map_y,
		     const struct city *const pcity,
		     const struct tile *map_tile)
{
  return base_map_to_city_map(city_map_x, city_map_y, pcity->tile, map_tile);
}

/**************************************************************************
Finds the map position for a given city map coordinate of a certain
city. Returns true if the map position found is real.
**************************************************************************/
struct tile *base_city_map_to_map(const struct tile *city_tile,
				  int city_map_x, int city_map_y)
{
  int x, y;

  assert(is_valid_city_coords(city_map_x, city_map_y));
  x = city_tile->x + city_map_x - CITY_MAP_SIZE / 2;
  y = city_tile->y + city_map_y - CITY_MAP_SIZE / 2;

  return map_pos_to_tile(x, y);
}

/**************************************************************************
Finds the map position for a given city map coordinate of a certain
city. Returns true if the map position found is real.
**************************************************************************/
struct tile *city_map_to_map(const struct city *const pcity,
			     int city_map_x, int city_map_y)
{
  return base_city_map_to_map(pcity->tile, city_map_x, city_map_y);
}

/**************************************************************************
  Compare two integer values, as required by qsort.
***************************************************************************/
static int cmp(int v1, int v2)
{
  if (v1 == v2) {
    return 0;
  } else if (v1 > v2) {
    return 1;
  } else {
    return -1;
  }
}

/**************************************************************************
  Compare two iter_index values from the city_map_iterate_outward_indices.

  This function will be passed to qsort().  It should never return zero,
  or the sort order will be left up to qsort and will be undefined.  This
  would mean that server execution would not be reproducable.
***************************************************************************/
int compare_iter_index(const void *a, const void *b)
{
  const struct iter_index *index1 = a, *index2 = b;
  int value;

  value = cmp(index1->dist, index2->dist);
  if (value != 0) {
    return value;
  }

  value = cmp(index1->dx, index2->dx);
  if (value != 0) {
    return value;
  }

  value = cmp(index1->dy, index2->dy);
  assert(value != 0);
  return value;
}

/**************************************************************************
  Fill the iterate_outwards_indices array.  This may depend on topology and
  ruleset settings.
***************************************************************************/
void generate_city_map_indices(void)
{
  int i = 0, dx, dy;
  struct iter_index *array = city_map_iterate_outwards_indices;

  city_tiles = 0;
  city_map_iterate(city_x, city_y) {
    city_tiles++;
  } city_map_iterate_end;

  /* Realloc is used because this function may be called multiple times. */
  array = fc_realloc(array, CITY_TILES * sizeof(*array));

  for (dx = -CITY_MAP_RADIUS; dx <= CITY_MAP_RADIUS; dx++) {
    for (dy = -CITY_MAP_RADIUS; dy <= CITY_MAP_RADIUS; dy++) {
      if (is_valid_city_coords(dx + CITY_MAP_RADIUS, dy + CITY_MAP_RADIUS)) {
	array[i].dx = dx;
	array[i].dy = dy;
	array[i].dist = map_vector_to_sq_distance(dx, dy);
	i++;
      }
    }
  }
  assert(i == CITY_TILES);

  qsort(array, CITY_TILES, sizeof(*array), compare_iter_index);

#ifdef DEBUG
  for (i = 0; i < CITY_TILES; i++) {
    freelog(LOG_DEBUG, "%2d : (%2d,%2d) : %d", i,
	    array[i].dx + CITY_MAP_RADIUS, array[i].dy + CITY_MAP_RADIUS,
	    array[i].dist);
  }
#endif

  city_map_iterate_outwards_indices = array;

  cm_init_citymap();
}

/**************************************************************************
  Set the worker on the citymap.  Also sets the worked field in the map.
**************************************************************************/
void set_worker_city(struct city *pcity, int city_x, int city_y,
		     enum city_tile_type type)
{
  struct tile *ptile;

  if ((ptile = city_map_to_map(pcity, city_x, city_y))) {
    if (pcity->city_map[city_x][city_y] == C_TILE_WORKER
	&& ptile->worked == pcity) {
      ptile->worked = NULL;
    }
    pcity->city_map[city_x][city_y] = type;
    if (type == C_TILE_WORKER) {
      ptile->worked = pcity;
    }
  } else {
    assert(type == C_TILE_UNAVAILABLE);
    pcity->city_map[city_x][city_y] = type;
  }
}

/**************************************************************************
  Return the worker status of the given tile on the citymap for the given
  city.
**************************************************************************/
enum city_tile_type get_worker_city(const struct city *pcity, 
                                    int city_x, int city_y)
{
  if (!is_valid_city_coords(city_x, city_y)) {
    return C_TILE_UNAVAILABLE;
  }
  return pcity->city_map[city_x][city_y];
}

/**************************************************************************
  Return TRUE if this tile on the citymap is being worked by this city.
**************************************************************************/
bool is_worker_here(const struct city *pcity, int city_x, int city_y) 
{
  if (!is_valid_city_coords(city_x, city_y)) {
    return FALSE;
  }

  return get_worker_city(pcity, city_x, city_y) == C_TILE_WORKER;
}

/**************************************************************************
  Return the extended name of the building.
**************************************************************************/
const char *get_impr_name_ex(const struct city *pcity, Impr_Type_id id)
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
  Return the cost (gold) to buy the current city production.
**************************************************************************/
int city_buy_cost(const struct city *pcity)
{
  int cost, build = pcity->shield_stock;

  if (pcity->is_building_unit) {
    cost = unit_buy_gold_cost(pcity->currently_building, build);
  } else {
    cost = impr_buy_gold_cost(pcity->currently_building, build);
  }
  return cost;
}

/**************************************************************************
  Return the owner of the city.
**************************************************************************/
struct player *city_owner(const struct city *pcity)
{
  return (&game.players[pcity->owner]);
}

/**************************************************************************
 Returns 1 if the given city is next to or on one of the given building's
 terr_gate (terrain) or spec_gate (specials), or if the building has no
 terrain/special requirements.
**************************************************************************/
bool city_has_terr_spec_gate(const struct city *pcity, Impr_Type_id id)
{
  struct impr_type *impr;
  Terrain_type_id *terr_gate;
  enum tile_special_type *spec_gate;

  impr = get_improvement_type(id);
  spec_gate = impr->spec_gate;
  terr_gate = impr->terr_gate;

  if (*spec_gate == S_NO_SPECIAL && *terr_gate == T_NONE) {
    return TRUE;
  }

  for (;*spec_gate!=S_NO_SPECIAL;spec_gate++) {
    if (map_has_special(pcity->tile, *spec_gate) ||
        is_special_near_tile(pcity->tile, *spec_gate)) {
      return TRUE;
    }
  }

  for (; *terr_gate != T_NONE; terr_gate++) {
    if (pcity->tile->terrain == *terr_gate
        || is_terrain_near_tile(pcity->tile, *terr_gate)) {
      return TRUE;
    }
  }

  return FALSE;
}

/**************************************************************************
  Return whether given city can build given building, ignoring whether
  it is obsolete.
**************************************************************************/
bool can_build_improvement_direct(const struct city *pcity, Impr_Type_id id)
{
  const struct impr_type *building = get_improvement_type(id);

  if (!can_player_build_improvement_direct(city_owner(pcity), id)) {
    return FALSE;
  }

  if (city_got_building(pcity, id)) {
    return FALSE;
  }

  if (!city_has_terr_spec_gate(pcity, id)) {
    return FALSE;
  }

  if (building->bldg_req != B_LAST
      && !city_got_building(pcity, building->bldg_req)) {
    return FALSE;
  }

  return !improvement_redundant(city_owner(pcity),pcity, id, TRUE);
}

/**************************************************************************
  Return whether given city can build given building; returns FALSE if
  the building is obsolete.
**************************************************************************/
bool can_build_improvement(const struct city *pcity, Impr_Type_id id)
{  
  if (!can_build_improvement_direct(pcity, id)) {
    return FALSE;
  }
  if (improvement_obsolete(city_owner(pcity), id)) {
    return FALSE;
  }
  return TRUE;
}

/**************************************************************************
  Return whether player can eventually build given building in the city;
  returns FALSE if improvement can never possibly be built in this city.
**************************************************************************/
bool can_eventually_build_improvement(const struct city *pcity,
				      Impr_Type_id id)
{
  /* Can the _player_ ever build this improvement? */
  if (!can_player_eventually_build_improvement(city_owner(pcity), id)) {
    return FALSE;
  }

  if (!city_has_terr_spec_gate(pcity, id)) {
    return FALSE;
  }

  return TRUE;
}

/**************************************************************************
  Return whether given city can build given unit, ignoring whether unit 
  is obsolete.
**************************************************************************/
bool can_build_unit_direct(const struct city *pcity, Unit_Type_id id)
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
  if (!is_ocean_near_tile(pcity->tile) && is_water_unit(id)) {
    return FALSE;
  }
  return TRUE;
}

/**************************************************************************
  Return whether given city can build given unit; returns 0 if unit is 
  obsolete.
**************************************************************************/
bool can_build_unit(const struct city *pcity, Unit_Type_id id)
{  
  if (!can_build_unit_direct(pcity, id)) {
    return FALSE;
  }
  while (unit_type_exists((id = unit_types[id].obsoleted_by))) {
    if (can_player_build_unit_direct(city_owner(pcity), id)) {
	return FALSE;
    }
  }
  return TRUE;
}

/**************************************************************************
  Return whether player can eventually build given unit in the city;
  returns 0 if unit can never possibly be built in this city.
**************************************************************************/
bool can_eventually_build_unit(const struct city *pcity, Unit_Type_id id)
{
  /* Can the _player_ ever build this unit? */
  if (!can_player_eventually_build_unit(city_owner(pcity), id)) {
    return FALSE;
  }

  /* Some units can be built only in certain cities -- for instance,
     ships may be built only in cities adjacent to ocean. */
  if (!is_ocean_near_tile(pcity->tile) && is_water_unit(id)) {
    return FALSE;
  }

  return TRUE;
}

/****************************************************************************
  Returns TRUE iff if the given city can use this kind of specialist.
****************************************************************************/
bool city_can_use_specialist(const struct city *pcity,
			     Specialist_type_id type)
{
  return pcity->size >= game.rgame.specialists[type].min_size;
}

/****************************************************************************
  Returns TRUE iff if the given city can change what it is building
****************************************************************************/
bool city_can_change_build(const struct city *pcity)
{
  return !pcity->did_buy || pcity->shield_stock <= 0;
}

/**************************************************************************
 Returns how many thousand citizen live in this city.
**************************************************************************/
int city_population(const struct city *pcity)
{
  /*  Sum_{i=1}^{n} i  ==  n*(n+1)/2  */
  return pcity->size * (pcity->size + 1) * 5;
}

/**************************************************************************
  Return TRUE if the city has this building in it.
**************************************************************************/
bool city_got_building(const struct city *pcity, Impr_Type_id id) 
{
  if (!improvement_exists(id)) {
    return FALSE;
  } else {
    return (pcity->improvements[id] != I_NONE);
  }
}

/**************************************************************************
  Return the upkeep (gold) needed each turn to upkeep the given improvement
  in the given city.
**************************************************************************/
int improvement_upkeep(const struct city *pcity, Impr_Type_id i) 
{
  if (!improvement_exists(i))
    return 0;
  if (is_wonder(i))
    return 0;
  if (improvement_types[i].upkeep
      <= get_building_bonus(pcity, i, EFT_UPKEEP_FREE)) {
    return 0;
  }
  
  return (improvement_types[i].upkeep);
}

/**************************************************************************
  Calculate the shields for the tile.  If pcity is specified then
  (city_x, city_y) must be valid city coordinates and is_celebrating tells
  whether the city is celebrating.
**************************************************************************/
static int base_get_shields_tile(const struct tile *ptile,
				 const struct city *pcity,
				 int city_x, int city_y, bool is_celebrating)
{
  enum tile_special_type spec_t = map_get_special(ptile);
  Terrain_type_id tile_t = ptile->terrain;
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

    s += get_city_tile_bonus(pcity, ptile, EFT_PROD_ADD_TILE);

    /* Government & effect shield bonus/penalty. */
    if (s > 0) {
      s += (is_celebrating ? g->celeb_shield_bonus : g->shield_bonus);
      s += get_city_tile_bonus(pcity, ptile, EFT_PROD_INC_TILE);
    }

    s += (s * get_city_tile_bonus(pcity, ptile, EFT_PROD_PER_TILE)) / 100;

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
int get_shields_tile(const struct tile *ptile)
{
  return base_get_shields_tile(ptile, NULL, -1, -1, FALSE);
}

/**************************************************************************
  Calculate the shields the given tile is capable of producing for the
  city.
**************************************************************************/
int city_get_shields_tile(int city_x, int city_y, const struct city *pcity)
{
  return base_city_get_shields_tile(city_x, city_y, pcity,
				    city_celebrating(pcity));
}

/**************************************************************************
  Calculate the shields the given tile would be capable of producing for
  the city if the city's celebration status were as given.

  This can be used to calculate the benefits celebration would give.
**************************************************************************/
int base_city_get_shields_tile(int city_x, int city_y,
			       const struct city *pcity,
			       bool is_celebrating)
{
  struct tile *ptile;

  if (!(ptile = city_map_to_map(pcity, city_x, city_y))) {
    assert(0);
    return 0;
  }

  return base_get_shields_tile(ptile, pcity,
			       city_x, city_y, is_celebrating);
}

/**************************************************************************
  Calculate the trade for the tile.  If pcity is specified then
  (city_x, city_y) must be valid city coordinates and is_celebrating tells
  whether the city is celebrating.
**************************************************************************/
static int base_get_trade_tile(const struct tile *ptile,
			       const struct city *pcity,
			       int city_x, int city_y, bool is_celebrating)
{
  enum tile_special_type spec_t = map_get_special(ptile);
  Terrain_type_id tile_t = ptile->terrain;
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

  if (contains_special(spec_t, S_RAILROAD)) {
    t += (t * terrain_control.rail_trade_bonus) / 100;
  }

  /* Civ1 specifically documents that Railroad trade increase is before 
   * Democracy/Republic [government in general now -- SKi] bonus  -AJS */
  if (pcity) {
    struct government *g = get_gov_pcity(pcity);
    int before_penalty = (is_celebrating ? g->celeb_trade_before_penalty
			  : g->trade_before_penalty);

    t += get_city_tile_bonus(pcity, ptile, EFT_TRADE_ADD_TILE);

    if (t > 0) {
      t += (is_celebrating ? g->celeb_trade_bonus : g->trade_bonus);
      t += get_city_tile_bonus(pcity, ptile, EFT_TRADE_INC_TILE);
    }

    t += (t * get_city_tile_bonus(pcity, ptile, EFT_TRADE_PER_TILE)) / 100;

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

  if (pcity && is_city_center(city_x, city_y)) {
    t = MAX(t, game.rgame.min_city_center_trade);
  }

  return t;
}

/**************************************************************************
  Calculate the trade produced by the tile.  This obviously won't take
  into account any city or government bonuses.
**************************************************************************/
int get_trade_tile(const struct tile *ptile)
{
  return base_get_trade_tile(ptile, NULL, -1, -1, FALSE);
}

/**************************************************************************
  Calculate the trade the given tile is capable of producing for the
  city.
**************************************************************************/
int city_get_trade_tile(int city_x, int city_y, const struct city *pcity)
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
			     const struct city *pcity, bool is_celebrating)
{
  struct tile *ptile;

  if (!(ptile = city_map_to_map(pcity, city_x, city_y))) {
    assert(0);
    return 0;
  }

  return base_get_trade_tile(ptile, pcity, city_x, city_y, is_celebrating);
}

/**************************************************************************
  Calculate the food for the tile.  If pcity is specified then
  (city_x, city_y) must be valid city coordinates and is_celebrating tells
  whether the city is celebrating.
**************************************************************************/
static int base_get_food_tile(const struct tile *ptile,
			      const struct city *pcity,
			      int city_x, int city_y, bool is_celebrating)
{
  const enum tile_special_type spec_t = map_get_special(ptile);
  const Terrain_type_id tile_t = ptile->terrain;
  struct tile_type *type = get_tile_type(tile_t);
  struct tile tile;
  int f;
  const bool auto_water = (pcity && is_city_center(city_x, city_y)
			   && tile_t == type->irrigation_result
			   && terrain_control.may_irrigate);

  /* create dummy tile which has the city center bonuses. */
  tile.terrain = tile_t;
  tile.special = spec_t;

  if (auto_water) {
    /* The center tile is auto-irrigated. */
    tile.special |= S_IRRIGATION;

    if (player_knows_techs_with_flag(city_owner(pcity), TF_FARMLAND)) {
      tile.special |= S_FARMLAND;
    }
  }

  if (contains_special(tile.special, S_SPECIAL_1)) {
    f = type->food_special_1;
  } else if (contains_special(tile.special, S_SPECIAL_2)) {
    f = type->food_special_2;
  } else {
    f = type->food;
  }

  if (contains_special(tile.special, S_IRRIGATION)) {
    f += type->irrigation_food_incr;
  }

  if (contains_special(tile.special, S_RAILROAD)) {
    f += (f * terrain_control.rail_food_bonus) / 100;
  }

  if (pcity) {
    struct government *g = get_gov_pcity(pcity);
    int before_penalty = (is_celebrating ? g->celeb_food_before_penalty
			  : g->food_before_penalty);

    f += get_city_tile_bonus(pcity, &tile, EFT_FOOD_ADD_TILE);

    if (f > 0) {
      f += (is_celebrating ? g->celeb_food_bonus : g->food_bonus);
      f += get_city_tile_bonus(pcity, &tile, EFT_FOOD_INC_TILE);
    }

    f += (f * get_city_tile_bonus(pcity, &tile, EFT_FOOD_PER_TILE) / 100);

    if (before_penalty > 0 && f > before_penalty) {
      f--;
    }
  }

  if (contains_special(tile.special, S_POLLUTION)) {
    /* The food here is yucky */
    f -= (f * terrain_control.pollution_food_penalty) / 100;
  }
  if (contains_special(tile.special, S_FALLOUT)) {
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
int get_food_tile(const struct tile *ptile)
{
  return base_get_food_tile(ptile, NULL, -1, -1, FALSE);
}

/**************************************************************************
  Calculate the food the given tile is capable of producing for the
  city.
**************************************************************************/
int city_get_food_tile(int city_x, int city_y, const struct city *pcity)
{
  return base_city_get_food_tile(city_x, city_y, pcity,
				 city_celebrating(pcity));
}

/**************************************************************************
  Calculate the food the given tile would be capable of producing for
  the city if the city's celebration status were as given.

  This can be used to calculate the benefits celebration would give.
**************************************************************************/
int base_city_get_food_tile(int city_x, int city_y, const struct city *pcity,
			    bool is_celebrating)
{
  struct tile *ptile;

  if (!(ptile = city_map_to_map(pcity, city_x, city_y))) {
    assert(0);
    return 0;
  }

  return base_get_food_tile(ptile, pcity, city_x, city_y, is_celebrating);
}

/**************************************************************************
  Returns TRUE if the given unit can build a city at the given map
  coordinates.  punit is the founding unit, which may be NULL in some
  cases (e.g., cities from huts).
**************************************************************************/
bool city_can_be_built_here(const struct tile *ptile, struct unit *punit)
{
  if (terrain_has_flag(ptile->terrain, TER_NO_CITIES)) {
    /* No cities on this terrain. */
    return FALSE;
  }

  if (punit) {
    enum unit_move_type move_type = unit_type(punit)->move_type;
    Terrain_type_id t = ptile->terrain;

    /* We allow land units to build land cities and sea units to build
     * ocean cities. */
    if ((move_type == LAND_MOVING && is_ocean(t))
	|| (move_type == SEA_MOVING && !is_ocean(t))) {
      return FALSE;
    }
  }

  /* game.rgame.min_dist_bw_cities minimum is 1, meaning adjacent is okay */
  square_iterate(ptile, game.rgame.min_dist_bw_cities - 1, ptile1) {
    if (ptile1->city) {
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
bool can_cities_trade(const struct city *pc1, const struct city *pc2)
{
  /* If you change the logic here, make sure to update the help in
   * helptext_unit(). */
  return (pc1 && pc2 && pc1 != pc2
          && (pc1->owner != pc2->owner
	      || map_distance(pc1->tile, pc2->tile) > 8));
}

/**************************************************************************
  Find the worst (minimum) trade route the city has.  The value of the
  trade route is returned and its position (slot) is put into the slot
  variable.
**************************************************************************/
int get_city_min_trade_route(const struct city *pcity, int *slot)
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
bool can_establish_trade_route(const struct city *pc1, const struct city *pc2)
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
  Return the trade that exists between these cities, assuming they have a
  trade route.
**************************************************************************/
int trade_between_cities(const struct city *pc1, const struct city *pc2)
{
  int bonus = 0;

  if (pc1 && pc2) {
    bonus = (pc1->tile_trade + pc2->tile_trade + 4) / 8;

    /* Double if on different continents. */
    if (map_get_continent(pc1->tile) != map_get_continent(pc2->tile)) {
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
int city_num_trade_routes(const struct city *pcity)
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
int get_caravan_enter_city_trade_bonus(const struct city *pc1, 
                                       const struct city *pc2)
{
  int i, tb;

  /* Should this be real_map_distance? */
  tb = map_distance(pc1->tile, pc2->tile) + 10;
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
bool have_cities_trade_route(const struct city *pc1, const struct city *pc2)
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
  and units.  Does not count capitalization.
*************************************************************************/
int city_gold_surplus(const struct city *pcity, int tax_total)
{
  int cost = 0;

  built_impr_iterate(pcity, i) {
    cost += improvement_upkeep(pcity, i);
  } built_impr_iterate_end;

  unit_list_iterate(pcity->units_supported, punit) {
    cost += punit->upkeep_gold;
  } unit_list_iterate_end;

  return tax_total - cost;
}

/**************************************************************************
  Return TRUE iff this city is its nation's capital.  The capital city is
  special-cased in a number of ways.
**************************************************************************/
bool is_capital(const struct city *pcity)
{
  return (get_city_bonus(pcity, EFT_CAPITAL_CITY) != 0);
}

/**************************************************************************
 Whether a city has its own City Walls, or the same effect via a wonder.
**************************************************************************/
bool city_got_citywalls(const struct city *pcity)
{
  return (get_city_bonus(pcity, EFT_LAND_DEFEND) > 0);
}

/**************************************************************************
  Return TRUE iff the city is happy.  A happy city will start celebrating
  soon.
**************************************************************************/
bool city_happy(const struct city *pcity)
{
  return (pcity->ppl_happy[4] >= (pcity->size + 1) / 2 &&
	  pcity->ppl_unhappy[4] == 0 && pcity->ppl_angry[4] == 0);
}

/**************************************************************************
  Return TRUE iff the city is unhappy.  An unhappy city will start
  revolting soon.
**************************************************************************/
bool city_unhappy(const struct city *pcity)
{
  return (pcity->ppl_happy[4] <
	  pcity->ppl_unhappy[4] + 2 * pcity->ppl_angry[4]);
}

/**************************************************************************
  Return TRUE if the city was celebrating at the start of the turn.
**************************************************************************/
bool base_city_celebrating(const struct city *pcity)
{
  return (pcity->size >= get_gov_pcity(pcity)->rapture_size
	  && pcity->was_happy);
}

/**************************************************************************
cities celebrate only after consecutive happy turns
**************************************************************************/
bool city_celebrating(const struct city *pcity)
{
  return base_city_celebrating(pcity) && city_happy(pcity);
}

/**************************************************************************
.rapture is checked instead of city_celebrating() because this function is
called after .was_happy was updated.
**************************************************************************/
bool city_rapture_grow(const struct city *pcity)
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
  Return the number of free shields for unit support the city would get
  under the given government.
**************************************************************************/
int citygov_free_shield(const struct city *pcity, struct government *gov)
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
int citygov_free_happy(const struct city *pcity, struct government *gov)
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
int citygov_free_food(const struct city *pcity, struct government *gov)
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
static int citygov_free_gold(const struct city *pcity, 
                             struct government *gov)
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
int get_city_style(const struct city *pcity)
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
const char *get_city_style_name(int style)
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
int city_change_production_penalty(const struct city *pcity,
				   int target, bool is_unit)
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
  } else if (city_built_last_turn(pcity)) {
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

  return shield_stock_after_adjustment;
}

/**************************************************************************
 Calculates the turns which are needed to build the requested
 improvement in the city.  GUI Independent.
**************************************************************************/
int city_turns_to_build(const struct city *pcity, int id, bool id_is_unit,
			bool include_shield_stock)
{
  int city_shield_surplus = pcity->shield_surplus;
  int city_shield_stock = include_shield_stock ?
      city_change_production_penalty(pcity, id, id_is_unit) : 0;
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
int city_turns_to_grow(const struct city *pcity)
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

/****************************************************************************
  Return TRUE iff the city can grow to the given size.
****************************************************************************/
bool city_can_grow_to(const struct city *pcity, int pop_size)
{
  if (get_city_bonus(pcity, EFT_SIZE_UNLIMIT) > 0) {
    return TRUE;
  } else {
    int max_size;
                                                                               
    max_size = game.aqueduct_size + get_city_bonus(pcity, EFT_SIZE_ADJ);
    return (pop_size <= max_size);
  }
}

/**************************************************************************
 is there an enemy city on this tile?
**************************************************************************/
struct city *is_enemy_city_tile(const struct tile *ptile,
				struct player *pplayer)
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
struct city *is_allied_city_tile(const struct tile *ptile,
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
struct city *is_non_attack_city_tile(const struct tile *ptile,
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
struct city *is_non_allied_city_tile(const struct tile *ptile,
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
  return is_friendly_city_near(unit_owner(punit), punit->tile);
}

/**************************************************************************
...
**************************************************************************/
bool is_friendly_city_near(struct player *owner, const struct tile *ptile)
{
  square_iterate(ptile, 3, ptile1) {
    struct city * pcity = ptile1->city;
    if (pcity && pplayers_allied(owner, city_owner(pcity)))
      return TRUE;
  } square_iterate_end;

  return FALSE;
}

/**************************************************************************
  Return true iff a city exists within a city radius of the given 
  location. may_be_on_center determines if a city at x,y counts.
**************************************************************************/
bool city_exists_within_city_radius(const struct tile *ptile,
				    bool may_be_on_center)
{
  map_city_radius_iterate(ptile, ptile1) {
    if (may_be_on_center || !same_pos(ptile, ptile1)) {
      if (ptile1->city) {
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
  Give base number of content citizens in any city owner by pplayer.
**************************************************************************/
static int content_citizens(struct player *pplayer)
{
  int cities = city_list_size(&pplayer->cities);
  int content = game.unhappysize;
  int basis = game.cityfactor + get_gov_pplayer(pplayer)->empire_size_mod;
  int step = get_gov_pplayer(pplayer)->empire_size_inc;

  if (cities > basis) {
    content--;
    if (step != 0) {
      /* the first penalty is at (basis + 1) cities;
         the next is at (basis + step + 1), _not_ (basis + step) */
      content -= (cities - basis - 1) / step;
    }
  }
  return content;
}

/**************************************************************************
 Return the factor (in %) by which the shield should be multiplied.
**************************************************************************/
int get_city_shield_bonus(const struct city *pcity)
{
  const int bonus = 100 + get_city_bonus(pcity, EFT_PROD_BONUS);

  return MAX(bonus, 0);
}

/**************************************************************************
  Return the factor (in %) by which the tax should be multiplied.
**************************************************************************/
int get_city_tax_bonus(const struct city *pcity)
{
  const int bonus = 100 + get_city_bonus(pcity, EFT_TAX_BONUS);

  return MAX(bonus, 0);
}

/**************************************************************************
  Return the factor (in %) by which the luxury should be multiplied.
**************************************************************************/
int get_city_luxury_bonus(const struct city *pcity)
{
  const int bonus = 100 + get_city_bonus(pcity, EFT_LUXURY_BONUS);

  return MAX(bonus, 0);
}

/**************************************************************************
  Return the amount of gold generated by buildings under "tithe" attribute
  governments.
**************************************************************************/
int get_city_tithes_bonus(const struct city *pcity)
{
  int tithes_bonus = 0;

  if (!government_has_flag(get_gov_pcity(pcity), 
                           G_CONVERT_TITHES_TO_MONEY)) {
    return 0;
  }

  tithes_bonus += get_city_bonus(pcity, EFT_MAKE_CONTENT);
  tithes_bonus += get_city_bonus(pcity, EFT_FORCE_CONTENT);

  return tithes_bonus;
}

/**************************************************************************
  Return the factor (in %) by which the science should be multiplied.
**************************************************************************/
int get_city_science_bonus(const struct city *pcity)
{
  int science_bonus;

  science_bonus = 100 + get_city_bonus(pcity, EFT_SCIENCE_BONUS);

  if (government_has_flag(get_gov_pcity(pcity), G_REDUCED_RESEARCH)) {
    science_bonus /= 2;
  }

  return MAX(science_bonus, 0);
}

/**************************************************************************
  Get the incomes of a city according to the taxrates (ignore # of 
  specialists). trade should usually be pcity->trade_prod.
**************************************************************************/
void get_tax_income(struct player *pplayer, int trade, int *sci, 
                    int *lux, int *tax)
{
  const int SCIENCE = 0, TAX = 1, LUXURY = 2;
  int rates[3], result[3];

  if (game.rgame.changable_tax) {
    rates[SCIENCE] = pplayer->economic.science;
    rates[LUXURY] = pplayer->economic.luxury;
    rates[TAX] = 100 - rates[SCIENCE] - rates[LUXURY];
  } else {
    rates[SCIENCE] = game.rgame.forced_science;
    rates[LUXURY] = game.rgame.forced_luxury;
    rates[TAX] = game.rgame.forced_gold;
  }
  
  /* ANARCHY */
  if (get_gov_pplayer(pplayer)->index == game.government_when_anarchy) {
    rates[SCIENCE] = 0;
    rates[LUXURY] = 100;
    rates[TAX] = 0;
  }

  distribute(trade, 3, rates, result);

  *sci = result[SCIENCE];
  *tax = result[TAX];
  *lux = result[LUXURY];

  assert(*sci + *tax + *lux == trade);
}

/**************************************************************************
  Return TRUE if the city built something last turn (meaning production
  was completed between last turn and this).
**************************************************************************/
bool city_built_last_turn(const struct city *pcity)
{
  return pcity->turn_last_built + 1 >= game.turn;
}

/**************************************************************************
  Modify the incomes according to the taxrates and # of specialists.
**************************************************************************/
static inline void set_tax_income(struct city *pcity)
{
  get_tax_income(city_owner(pcity), pcity->trade_prod, &pcity->science_total, 
                 &pcity->luxury_total, &pcity->tax_total);

  pcity->luxury_total += (pcity->specialists[SP_ELVIS]
			  * game.rgame.specialists[SP_ELVIS].bonus);
  pcity->science_total += (pcity->specialists[SP_SCIENTIST]
			   * game.rgame.specialists[SP_SCIENTIST].bonus);
  pcity->tax_total += (pcity->specialists[SP_TAXMAN]
			* game.rgame.specialists[SP_TAXMAN].bonus);
  pcity->tax_total += get_city_tithes_bonus(pcity);
}

/**************************************************************************
  Modify the incomes according to various buildings.

  Note this does not set trade.  That's been done already.
**************************************************************************/
static void add_buildings_effect(struct city *pcity)
{
  /* this is the place to set them */
  pcity->tax_bonus = get_city_tax_bonus(pcity);
  pcity->luxury_bonus = get_city_luxury_bonus(pcity);
  pcity->science_bonus = get_city_science_bonus(pcity);
  pcity->shield_bonus = get_city_shield_bonus(pcity);

  pcity->shield_prod = (pcity->shield_prod * pcity->shield_bonus) / 100;
  pcity->luxury_total = (pcity->luxury_total * pcity->luxury_bonus) / 100;
  pcity->tax_total = (pcity->tax_total * pcity->tax_bonus) / 100;
  pcity->science_total = (pcity->science_total * pcity->science_bonus) / 100;
  pcity->shield_surplus = pcity->shield_prod;
}

/**************************************************************************
  Copy the happyness array in the city from index i to index i+1.
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
  Make people happy: 
   * angry citizen are eliminated first
   * then content are made happy, then unhappy content, etc.
   * each conversions costs 2 or 4 luxuries.
**************************************************************************/
static inline void citizen_luxury_happy(const struct city *pcity, int *luxuries,
                                        int *angry, int *unhappy, int *happy, 
                                        int *content)
{
  while (*luxuries >= 2 && *angry > 0) {
    (*angry)--;
    (*unhappy)++;
    *luxuries -= 2;
  }
  while (*luxuries >= 2 && *content > 0) {
    (*content)--;
    (*happy)++;
    *luxuries -= 2;
  }
  while (*luxuries >= 4 && *unhappy > 0) {
    (*unhappy)--;
    (*happy)++;
    *luxuries -= 4;
  }
  if (*luxuries >= 2 && *unhappy > 0) {
    (*unhappy)--;
    (*content)++;
    *luxuries -= 2;
  }
}

/**************************************************************************
  Make citizens happy due to luxury.
**************************************************************************/
static inline void citizen_happy_luxury(struct city *pcity)
{
  int x = pcity->luxury_total;

  happy_copy(pcity, 0);

  citizen_luxury_happy(pcity, &x, &pcity->ppl_angry[1], &pcity->ppl_unhappy[1], 
                       &pcity->ppl_happy[1], &pcity->ppl_content[1]);
}

/**************************************************************************
  Make given number of citizens unhappy due to units in the field.
**************************************************************************/
static inline void citizen_unhappy_units(struct city *pcity, int unhap)
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
  Make citizens content due to city improvements.
**************************************************************************/
static inline void citizen_content_buildings(struct city *pcity)
{
  int faces = 0;
  happy_copy(pcity, 1);

  faces += get_city_bonus(pcity, EFT_MAKE_CONTENT);

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
}

/**************************************************************************
  Make citizens happy due to wonders.
**************************************************************************/
static inline void citizen_happy_wonders(struct city *pcity)
{
  int bonus = 0, mod;

  happy_copy(pcity, 3);

  if ((mod = get_city_bonus(pcity, EFT_MAKE_HAPPY)) > 0) {
    bonus += mod;

    while (bonus > 0 && pcity->ppl_content[4] > 0) {
      pcity->ppl_content[4]--;
      pcity->ppl_happy[4]++;
      bonus--;
      /* well i'm not sure what to do with the rest, 
         will let it make unhappy content */
    }
  }

  bonus += get_city_bonus(pcity, EFT_FORCE_CONTENT);

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

  if (get_city_bonus(pcity, EFT_NO_UNHAPPY) > 0) {
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
  Set food, tax, science and shields production to zero if city is in
  revolt.
**************************************************************************/
static inline void unhappy_city_check(struct city *pcity)
{
  if (city_unhappy(pcity)) {
    pcity->food_surplus = MIN(0, pcity->food_surplus);
    pcity->tax_total = 0;
    pcity->science_total = 0;
    pcity->shield_surplus = MIN(0, pcity->shield_surplus);
  }
}

/**************************************************************************
  Calculate pollution for the city.  The shield_total must be passed in
  (most callers will want to pass pcity->shield_prod).
**************************************************************************/
int city_pollution(struct city *pcity, int shield_total)
{
  struct player *pplayer = city_owner(pcity);
  int mod, pollution;

  /* Add one one pollution per shield, multipled by the bonus. */
  mod = 100 + get_city_bonus(pcity, EFT_POLLU_PROD_PCT);
  mod = MAX(0, mod);
  pollution = shield_total * mod / 100;

  /* Add one 1/4 pollution per citizen per tech, multiplied by the bonus. */
  mod = 100 + get_city_bonus(pcity, EFT_POLLU_POP_PCT);
  mod = MAX(0, mod);
  pollution += (pcity->size
		* num_known_tech_with_flag(pplayer,
					   TF_POPULATION_POLLUTION_INC)
		* mod) / (4 * 100);

  pollution = MAX(0, pollution - 20);

  return pollution;
}

/**************************************************************************
  Calculate food, trade and shields generated by a city, and set
  associated variables given to us.
**************************************************************************/
void get_food_trade_shields(const struct city *pcity, int *food, int *trade,
                            int *shields)
{
  bool is_celebrating = base_city_celebrating(pcity);

  *food = 0;
  *trade = 0;
  *shields = 0;
  
  city_map_iterate(x, y) {
    if (get_worker_city(pcity, x, y) == C_TILE_WORKER) {
      *food += base_city_get_food_tile(x, y, pcity, is_celebrating);
      *shields += base_city_get_shields_tile(x, y, pcity, is_celebrating);
      *trade += base_city_get_trade_tile(x, y, pcity, is_celebrating);
    }
  } city_map_iterate_end;
}

/**************************************************************************
   Set food, trade and shields production in a city.
**************************************************************************/
static inline void set_food_trade_shields(struct city *pcity)
{
  int i;
  pcity->food_surplus = 0;
  pcity->shield_surplus = 0;

  get_food_trade_shields(pcity, &pcity->food_prod, &pcity->trade_prod,
                         &pcity->shield_prod);
  
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
  Calculate upkeep costs.
**************************************************************************/
static inline void city_support(struct city *pcity, 
	 		        void (*send_unit_info) (struct player *pplayer,
						        struct unit *punit))
{
  struct government *g = get_gov_pcity(pcity);

  int free_happy = citygov_free_happy(pcity, g);
  int free_shield = citygov_free_shield(pcity, g);
  int free_food = citygov_free_food(pcity, g);
  int free_gold = citygov_free_gold(pcity, g);

  /* ??  This does the right thing for normal Republic and Democ -- dwp */
  free_happy += get_city_bonus(pcity, EFT_MAKE_CONTENT_MIL);

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
    unit_list_iterate(pcity->tile->units, punit) {
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
    if (happy_cost > 0
	&& get_city_bonus(pcity, EFT_MAKE_CONTENT_MIL_PER) > 0) {
      happy_cost--;
    }

    /* subtract values found above from city's resources -- SKi */
    if (happy_cost > 0) {
      adjust_city_free_cost(&free_happy, &happy_cost);
      if (happy_cost > 0) {
	citizen_unhappy_units(pcity, happy_cost);
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
  pcity->pollution = city_pollution(pcity, pcity->shield_prod);
  citizen_happy_luxury(pcity);	/* with our new found luxuries */
  citizen_content_buildings(pcity);	/* temple cathedral colosseum */
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
  Give corruption generated by city. Corruption is halved during love 
  the XXX days.
**************************************************************************/
int city_corruption(const struct city *pcity, int trade)
{
  struct government *g = get_gov_pcity(pcity);
  struct city *capital;
  int dist;
  unsigned int val;
  int trade_penalty;
  int notradesize = MIN(game.notradesize, game.fulltradesize);
  int fulltradesize = MAX(game.notradesize, game.fulltradesize);

  if (pcity->size <= notradesize) {
    trade_penalty = trade;
  } else if (pcity->size >= fulltradesize) {
    trade_penalty = 0;
  } else {
    trade_penalty = trade * (fulltradesize - pcity->size) /
      (fulltradesize - notradesize);
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
      int tmp = real_map_distance(capital->tile, pcity->tile);
      dist = MIN(g->corruption_max_distance_cap, tmp);
    }
  }
  dist =
      dist * g->corruption_distance_factor + g->extra_corruption_distance;

  /* Now calculate the final corruption.  Ordered to reduce integer
   * roundoff errors. */
  val = trade * MAX(dist, 1) * g->corruption_level;
  val -= (val * get_city_bonus(pcity, EFT_CORRUPT_PCT)) / 100;
  val /= 100 * 100; /* Level is a % multiplied by 100 */
  val = CLIP(trade_penalty, val, trade);
  return val;
}

/************************************************************************** 
  Give amount of waste generated by city. Waste is corruption for shields.
**************************************************************************/
int city_waste(const struct city *pcity, int shields)
{
  struct government *g = get_gov_pcity(pcity);
  struct city *capital;
  int dist;
  int shield_penalty = 0;
  unsigned int val;

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
      int tmp = real_map_distance(capital->tile, pcity->tile);
      dist = MIN(g->waste_max_distance_cap, tmp);
    }
  }
  dist = dist * g->waste_distance_factor + g->extra_waste_distance;
  /* Ordered to reduce integer roundoff errors */
  val = shields * MAX(dist, 1) * g->waste_level;
  val /= 100 * 100; /* Level is a % multiplied by 100 */

  val -= (val * get_city_bonus(pcity, EFT_WASTE_PCT)) / 100;

  val = CLIP(shield_penalty, val, shields);
  return val;
}

/**************************************************************************
  Give the number of specialists in a city.
**************************************************************************/
int city_specialists(const struct city *pcity)
{
  int count = 0;

  specialist_type_iterate(sp) {
    count += pcity->specialists[sp];
  } specialist_type_iterate_end;

  return count;
}

/**************************************************************************
  Return a string showing the number of specialists in the array.

  For instance with a city with (0,3,1) specialists call

    specialists_string(pcity->specialists);

  and you'll get "0/3/1".
**************************************************************************/
const char *specialists_string(const int *specialists)
{
  size_t len = 0;
  static char buf[5 * SP_COUNT];

  specialist_type_iterate(sp) {
    char *separator = (len == 0) ? "" : "/";

    my_snprintf(buf + len, sizeof(buf) - len,
		"%s%d", separator, specialists[sp]);
    len += strlen(buf + len);
  } specialist_type_iterate_end;

  return buf;
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
                                map_get_continent(pcity->tile),
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
                                map_get_continent(pcity->tile),
                                improvement_types[impr].equiv_range);
}

/**************************************************************************
Return the status (C_TILE_EMPTY, C_TILE_WORKER or C_TILE_UNAVAILABLE)
of a given map position. If the status is C_TILE_WORKER the city which
uses this tile is also returned. If status isn't C_TILE_WORKER the
city pointer is set to NULL.
**************************************************************************/
void get_worker_on_map_position(const struct tile *ptile,
				enum city_tile_type *result_city_tile_type,
				struct city **result_pcity)
{
  *result_pcity = ptile->worked;
  if (*result_pcity) {
    *result_city_tile_type = C_TILE_WORKER;
  } else {
    *result_city_tile_type = C_TILE_EMPTY;
  }
}

/**************************************************************************
 Returns TRUE iff the city has set the given option.
**************************************************************************/
bool is_city_option_set(const struct city *pcity, enum city_options option)
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
struct city *create_city_virtual(struct player *pplayer, struct tile *ptile,
				 const char *name)
{
  int i;
  struct city *pcity;

  pcity = fc_malloc(sizeof(struct city));

  pcity->id = 0;
  pcity->owner = pplayer->player_no;
  pcity->tile = ptile;
  sz_strlcpy(pcity->name, name);
  pcity->size = 1;
  specialist_type_iterate(sp) {
    pcity->specialists[sp] = 0;
  } specialist_type_iterate_end;
  pcity->specialists[SP_ELVIS] = 1;
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
      pcity->currently_building = game.default_building;
    }
  }
  pcity->turn_founded = game.turn;
  pcity->did_buy = TRUE;
  pcity->did_sell = FALSE;
  pcity->airlift = FALSE;

  pcity->turn_last_built = game.turn;
  pcity->changed_from_id = pcity->currently_building;
  pcity->changed_from_is_unit = pcity->is_building_unit;
  pcity->before_change_shields = 0;
  pcity->disbanded_shields = 0;
  pcity->caravan_shields = 0;
  pcity->last_turns_shield_surplus = 0;
  pcity->anarchy = 0;
  pcity->rapture = 0;
  pcity->city_options = CITYOPT_DEFAULT;

  pcity->server.workers_frozen = 0;
  pcity->server.needs_arrange = FALSE;

  pcity->ai.founder_want = 0; /* calculating this is really expensive */
  pcity->ai.next_founder_want_recalc = 0; /* turns to recalc found_want */
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
  pcity->ai.next_recalc = 0;

  pcity->corruption = 0;
  pcity->shield_waste = 0;
  pcity->shield_bonus = 100;
  pcity->luxury_bonus = 100;
  pcity->tax_bonus = 100;
  pcity->science_bonus = 100;

  pcity->client.occupied = FALSE;
  pcity->client.happy = pcity->client.unhappy = FALSE;
  pcity->client.colored = FALSE;

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
