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
const Output_type_id num_output_types = O_LAST;

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
   * This diagram is for rectangular topologies only.  But this is taken
   * care of inside map_vector_to_sq_distance so it works for all topologies.
   */
  return (CITY_MAP_RADIUS * CITY_MAP_RADIUS + 1 >= dist);
}

/**************************************************************************
  Finds the city map coordinate for a given map position and a city
  center. Returns whether the map position is inside of the city map.
**************************************************************************/
bool base_map_to_city_map(int *city_map_x, int *city_map_y,
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

  /* We don't use city-map iterators in this function because they may
   * rely on the indices that have not yet been generated. */

  city_tiles = 0;
  for (dx = -CITY_MAP_RADIUS; dx <= CITY_MAP_RADIUS; dx++) {
    for (dy = -CITY_MAP_RADIUS; dy <= CITY_MAP_RADIUS; dy++) {
      if (is_valid_city_coords(dx + CITY_MAP_RADIUS, dy + CITY_MAP_RADIUS)) {
	city_tiles++;
      }
    }
  }

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


/****************************************************************************
  Return an id string for the output type.  This string can be used
  internally by rulesets and tilesets and should not be changed or
  translated.
*****************************************************************************/
const char *get_output_identifier(Output_type_id output)
{
  switch (output) {
  case O_FOOD:
    return "food";
  case O_SHIELD:
    return "shield";
  case O_TRADE:
    return "trade";
  case O_GOLD:
    return "gold";
  case O_LUXURY:
    return "luxury";
  case O_SCIENCE:
    return "science";
  case O_LAST:
    break;
  }
  die("Unknown output type in get_output_id: %d", output);
  return NULL;
}

/****************************************************************************
  Return a translated name for the output type.  This name should only be
  used for user display.
*****************************************************************************/
const char *get_output_name(Output_type_id output)
{
  switch (output) {
  case O_FOOD:
    return _("Food");
  case O_SHIELD:
    return _("Shield");
  case O_TRADE:
    return _("Trade");
  case O_GOLD:
    return _("Gold");
  case O_LUXURY:
    return _("Luxury");
  case O_SCIENCE:
    return _("Science");
  case O_LAST:
    break;
  }
  die("Unknown output type in get_output_name: %d", output);
  return NULL;
}

/**************************************************************************
  Return the effect for the production bonus for this output type.
**************************************************************************/
inline enum effect_type get_output_bonus_effect(Output_type_id otype)
{
  switch (otype) {
  case O_SHIELD:
    return EFT_PROD_BONUS;
  case O_GOLD:
    return EFT_TAX_BONUS;
  case O_LUXURY:
    return EFT_LUXURY_BONUS;
  case O_SCIENCE:
    return EFT_SCIENCE_BONUS;
  case O_FOOD:
  case O_TRADE:
    return EFT_LAST;
  case O_LAST:
    break;
  }

  assert(0);
  return EFT_LAST;
}

/**************************************************************************
  Return the effect for waste reduction for this output type.
**************************************************************************/
static inline enum effect_type get_output_waste_effect(Output_type_id otype)
{
  switch (otype) {
  case O_SHIELD:
    return EFT_WASTE_PCT;
  case O_TRADE:
    return EFT_CORRUPT_PCT;
  case O_FOOD:
  case O_GOLD:
  case O_LUXURY:
  case O_SCIENCE:
    return EFT_LAST;
  case O_LAST:
    break;
  }

  assert(0);
  return EFT_LAST;
}

/**************************************************************************
  Return the effect for add-tile city bonuses for this output type.
**************************************************************************/
static inline enum effect_type get_output_add_tile_effect(Output_type_id o)
{
  switch (o) {
  case O_FOOD:
    return EFT_FOOD_ADD_TILE;
  case O_SHIELD:
    return EFT_PROD_ADD_TILE;
  case O_TRADE:
    return EFT_TRADE_ADD_TILE;
  case O_GOLD:
  case O_LUXURY:
  case O_SCIENCE:
    return EFT_LAST;
  case O_LAST:
    break;
  }

  assert(0);
  return EFT_LAST;
}

/**************************************************************************
  Return the effect for inc-tile city bonuses for this output type.
**************************************************************************/
static inline enum effect_type get_output_inc_tile_effect(Output_type_id o)
{
  switch (o) {
  case O_FOOD:
    return EFT_FOOD_INC_TILE;
  case O_SHIELD:
    return EFT_PROD_INC_TILE;
  case O_TRADE:
    return EFT_TRADE_INC_TILE;
  case O_GOLD:
  case O_LUXURY:
  case O_SCIENCE:
    return EFT_LAST;
  case O_LAST:
    break;
  }

  assert(0);
  return EFT_LAST;
}

/**************************************************************************
  Return the effect for per-tile city bonuses for this output type.
**************************************************************************/
static inline enum effect_type get_output_per_tile_effect(Output_type_id o)
{
  switch (o) {
  case O_FOOD:
    return EFT_FOOD_PER_TILE;
  case O_SHIELD:
    return EFT_PROD_PER_TILE;
  case O_TRADE:
    return EFT_TRADE_PER_TILE;
  case O_GOLD:
  case O_LUXURY:
  case O_SCIENCE:
    return EFT_LAST;
  case O_LAST:
    break;
  }

  assert(0);
  return EFT_LAST;
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
    struct player *pplayer = city_owner(pcity);

    if (improvement_obsolete(pplayer, id)) {
      state = Q_("?obsolete:O");
    } else if (is_building_replaced(pcity, id)) {
      state = Q_("?redundant:*");
    }
  }
  if (is_great_wonder(id)) {
    if (great_wonder_was_built(id)) {
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

  return TRUE;
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
  while ((id = unit_types[id].obsoleted_by) != U_NOT_OBSOLETED) {
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
  int i;

  if (pcity->size < game.rgame.specialists[type].min_size) {
    return FALSE;
  }

  for (i = 0; i < MAX_NUM_REQS; i++) {
    struct requirement *req = &game.rgame.specialists[type].req[i];

    if (req->source.type == REQ_NONE) {
      break; /* Short-circuit any more checks. */
    } else if (!is_req_active(TARGET_CITY, city_owner(pcity), pcity,
			      B_LAST, NULL, req)) {
      return FALSE;
    }
  }

  return TRUE;
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
  Calculate the output for the tile.  If pcity is specified then
  (city_x, city_y) must be valid city coordinates and is_celebrating tells
  whether the city is celebrating.  otype gives the output type we're
  looking for (generally O_FOOD, O_TRADE, or O_SHIELD).
**************************************************************************/
static int base_get_output_tile(const struct tile *ptile,
				const struct city *pcity,
				int city_x, int city_y, bool is_celebrating,
				Output_type_id otype)
{
  const struct tile_type *ptype = get_tile_type(ptile->terrain);
  struct tile tile;
  int prod = get_tile_output_base(ptile, otype);
  const bool auto_water = (pcity && is_city_center(city_x, city_y)
			   && ptile->terrain == ptype->irrigation_result
			   && terrain_control.may_irrigate);

  assert(otype >= 0 && otype < O_LAST);

  /* create dummy tile which has the city center bonuses. */
  tile.terrain = map_get_terrain(ptile);
  tile.special = map_get_special(ptile);

  if (auto_water) {
    /* The center tile is auto-irrigated. */
    tile.special |= S_IRRIGATION;

    if (player_knows_techs_with_flag(city_owner(pcity), TF_FARMLAND)) {
      tile.special |= S_FARMLAND;
    }
  }

  switch (otype) {
  case O_SHIELD:
    if (contains_special(tile.special, S_MINE)) {
      prod += ptype->mining_shield_incr;
    }
    break;
  case O_FOOD:
    if (contains_special(tile.special, S_IRRIGATION)) {
      prod += ptype->irrigation_food_incr;
    }
    break;
  case O_TRADE:
    if (contains_special(tile.special, S_RIVER) && !is_ocean(tile.terrain)) {
      prod += terrain_control.river_trade_incr;
    }
    if (contains_special(tile.special, S_ROAD)) {
      prod += ptype->road_trade_incr;
    }
    break;
  case O_GOLD:
  case O_SCIENCE:
  case O_LUXURY:
  case O_LAST:
    break;
  }

  if (contains_special(tile.special, S_RAILROAD)) {
    prod += (prod * terrain_control.rail_tile_bonus[otype]) / 100;
  }

  if (pcity) {
    struct government *g = get_gov_pcity(pcity);
    int before_penalty = (is_celebrating
			  ? g->celeb_output_before_penalty[otype]
			  : g->output_before_penalty[otype]);
    enum effect_type add = get_output_add_tile_effect(otype);
    enum effect_type inc = get_output_inc_tile_effect(otype);
    enum effect_type per = get_output_per_tile_effect(otype);

    if (add != EFT_LAST) {
      prod += get_city_tile_bonus(pcity, ptile, add);
    }

    /* Government & effect bonus/penalty. */
    if (prod > 0) {
      prod += (is_celebrating
	    ? g->celeb_output_inc_tile[otype]
	    : g->output_inc_tile[otype]);
      if (inc != EFT_LAST) {
	prod += get_city_tile_bonus(pcity, ptile, inc);
      }
    }

    if (per != EFT_LAST) {
      prod += (prod * get_city_tile_bonus(pcity, ptile, per)) / 100;
    }

    if (before_penalty > 0 && prod > before_penalty) {
      prod--;
    }
  }

  if (contains_special(tile.special, S_POLLUTION)) {
    prod -= (prod * terrain_control.pollution_tile_penalty[otype]) / 100;
  }

  if (contains_special(tile.special, S_FALLOUT)) {
    prod -= (prod * terrain_control.fallout_tile_penalty[otype]) / 100;
  }

  if (pcity && is_city_center(city_x, city_y)) {
    prod = MAX(prod, game.rgame.min_city_center_output[otype]);
  }

  return prod;
}

/**************************************************************************
  Calculate the production output produced by the tile.  This obviously
  won't take into account any city or government bonuses.  The output
  type is given by 'otype' (generally O_FOOD, O_SHIELD, or O_TRADE).
**************************************************************************/
int get_output_tile(const struct tile *ptile, Output_type_id otype)
{
  return base_get_output_tile(ptile, NULL, -1, -1, FALSE, otype);
}

/**************************************************************************
  Calculate the production output the given tile is capable of producing
  for the city.  The output type is given by 'otype' (generally O_FOOD,
  O_SHIELD, or O_TRADE).
**************************************************************************/
int city_get_output_tile(int city_x, int city_y, const struct city *pcity,
			 Output_type_id otype)
{
  return base_city_get_output_tile(city_x, city_y, pcity,
				   city_celebrating(pcity), otype);
}

/**************************************************************************
  Calculate the shields the given tile would be capable of producing for
  the city if the city's celebration status were as given.

  This can be used to calculate the benefits celebration would give.
**************************************************************************/
int base_city_get_output_tile(int city_x, int city_y,
			      const struct city *pcity, bool is_celebrating,
			      Output_type_id otype)
{
  struct tile *ptile;

  if (!(ptile = city_map_to_map(pcity, city_x, city_y))) {
    assert(0);
    return 0;
  }

  return base_get_output_tile(ptile, pcity,
			      city_x, city_y, is_celebrating, otype);
}

/**************************************************************************
  Returns TRUE if the given unit can build a city at the given map
  coordinates.  punit is the founding unit, which may be NULL in some
  cases (e.g., cities from huts).
**************************************************************************/
bool city_can_be_built_here(const struct tile *ptile, const struct unit *punit)
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
    bonus = (pc1->citizen_base[O_TRADE]
	     + pc2->citizen_base[O_TRADE] + 4) / 8;

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
  tb = (tb * (pc1->surplus[O_TRADE] + pc2->surplus[O_TRADE])) / 24;

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
  Calculate how much is needed to pay for buildings in this city.
*************************************************************************/
int city_building_upkeep(const struct city *pcity, Output_type_id otype)
{
  int cost = 0;

  if (otype == O_GOLD) {
    built_impr_iterate(pcity, i) {
      cost += improvement_upkeep(pcity, i);
    } built_impr_iterate_end;
  }

  return cost;
}

/*************************************************************************
  Calculate how much is needed to pay for units in this city.
*************************************************************************/
int city_unit_upkeep(const struct city *pcity, Output_type_id otype)
{
  int cost = 0;

  unit_list_iterate(pcity->units_supported, punit) {
    cost += punit->upkeep[otype];
  } unit_list_iterate_end;

  return cost;
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

  return (pcity->rapture > 0 && pcity->surplus[O_FOOD] > 0
	  && (pcity->rapture % game.rapturedelay) == 0
	  && government_has_flag(g, G_RAPTURE_CITY_GROWTH));
}

/**************************************************************************
...
**************************************************************************/
struct city *city_list_find_id(struct city_list *This, int id)
{
  if (id != 0) {
    city_list_iterate(This, pcity) {
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
  city_list_iterate(This, pcity) {
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
  Return the number of free units of upkeep for unit support the city
  would get under the given government.
**************************************************************************/
int citygov_free_upkeep(const struct city *pcity,
			const struct government *gov, Output_type_id otype)
{
  if (gov->free_upkeep[otype] == G_CITY_SIZE_FREE) {
    return pcity->size;
  } else {
    return gov->free_upkeep[otype];
  }
}

/**************************************************************************
  Return how many citizens may be made content by military garrisons under
  this government type.
**************************************************************************/
int citygov_free_happy(const struct city *pcity, const struct government *gov)
{
  if (gov->free_happy == G_CITY_SIZE_FREE) {
    return pcity->size;
  } else {
    return gov->free_happy;
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
  Return the city style (used for drawing the city on the mapview in
  the client) for this city.  The city style depends on the
  start-of-game choice by the player as well as techs researched.
**************************************************************************/
int get_player_city_style(const struct player *plr)
{
  int replace, style, prev;

  style = plr->city_style;
  prev = style;

  while ((replace = city_styles[prev].replaced_by) != -1) {
    prev = replace;
    if (get_invention(plr, city_styles[replace].techreq) == TECH_KNOWN) {
      style = replace;
    }
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
  int city_shield_surplus = pcity->surplus[O_SHIELD];
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
  if (pcity->surplus[O_FOOD] > 0) {
    return (city_granary_size(pcity->size) - pcity->food_stock +
	    pcity->surplus[O_FOOD] - 1) / pcity->surplus[O_FOOD];
  } else if (pcity->surplus[O_FOOD] < 0) {
    /* turns before famine loss */
    return -1 + (pcity->food_stock / pcity->surplus[O_FOOD]);
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
				const struct player *pplayer)
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
				 const struct player *pplayer)
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
				     const struct player *pplayer)
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
				     const struct player *pplayer)
{
  struct city *pcity = ptile->city;

  if (pcity && !pplayers_allied(pplayer, city_owner(pcity)))
    return pcity;
  else
    return NULL;
}

/**************************************************************************
  Return TRUE if there is a friendly city near to this unit (within 3
  steps).
**************************************************************************/
bool is_unit_near_a_friendly_city(const struct unit *punit)
{
  return is_friendly_city_near(unit_owner(punit), punit->tile);
}

/**************************************************************************
  Return TRUE if there is a friendly city near to this tile (within 3
  steps).
**************************************************************************/
bool is_friendly_city_near(const struct player *owner,
			   const struct tile *ptile)
{
  square_iterate(ptile, 3, ptile1) {
    struct city * pcity = ptile1->city;
    if (pcity && pplayers_allied(owner, city_owner(pcity))) {
      return TRUE;
    }
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
static int content_citizens(const struct player *pplayer)
{
  int cities = city_list_size(pplayer->cities);
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
 Return the factor (in %) by which the city's output should be multiplied.
**************************************************************************/
int get_city_output_bonus(const struct city *pcity, Output_type_id otype)
{
  enum effect_type eft = get_output_bonus_effect(otype);
  int bonus = 100;

  if (eft != EFT_LAST) {
    bonus += get_city_bonus(pcity, eft);
  }

  if (otype == O_SCIENCE
      && government_has_flag(get_gov_pcity(pcity), G_REDUCED_RESEARCH)) {
    bonus /= 2;
  }

  return bonus;
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
  Add the incomes of a city according to the taxrates (ignore # of 
  specialists). trade should be in output[O_TRADE].
**************************************************************************/
void add_tax_income(const struct player *pplayer, int *output)
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

  distribute(output[O_TRADE], 3, rates, result);

  output[O_SCIENCE] += result[SCIENCE];
  output[O_GOLD] += result[TAX];
  output[O_LUXURY] += result[LUXURY];
}

/**************************************************************************
  Return TRUE if the city built something last turn (meaning production
  was completed between last turn and this).
**************************************************************************/
bool city_built_last_turn(const struct city *pcity)
{
  return pcity->turn_last_built + 1 >= game.turn;
}

/****************************************************************************
  Calculate output (food, trade and shields) generated by the worked tiles
  of a city.  This will completely overwrite the output[] array.
****************************************************************************/
static inline void get_worked_tile_output(const struct city *pcity,
					  int *output)
{
  memset(output, 0, O_COUNT * sizeof(*output));
  
  city_map_iterate(x, y) {
    if (pcity->city_map[x][y] == C_TILE_WORKER) {
      output_type_iterate(o) {
	output[o] += pcity->tile_output[x][y][o];
      } output_type_iterate_end;
    }
  } city_map_iterate_end;
}

/****************************************************************************
  Calculate output (gold, science, and luxury) generated by the specialists
  of a city.  The output[] array is not cleared but is just added to.
****************************************************************************/
static inline void add_specialist_output(const struct city *pcity,
					 int *output)
{
  specialist_type_iterate(sp) {
    int *bonus = game.rgame.specialists[sp].bonus;
    int count = pcity->specialists[sp];

    output_type_iterate(stat) {
      output[stat] += count * bonus[stat];
    } output_type_iterate_end;
  } specialist_type_iterate_end;
}

/****************************************************************************
  Calculate the base output (all types) of all workers and specialists in
  the city.  The output[] array is completely cleared in the process.  This
  does not account for output transformations (like trade->(sci,gold,lux))
  or city bonuses (pcity->bonus[]).
****************************************************************************/
void get_citizen_output(const struct city *pcity, int *output)
{
  get_worked_tile_output(pcity, output);
  add_specialist_output(pcity, output);
}

/****************************************************************************
  This function sets all the values in the pcity->bonus[] array.  This should
  be called near the beginning of generic_city_refresh.  It doesn't depend on
  anything else in the refresh and doesn't change when workers are moved
  around (but does change when buildings are built, etc.).
****************************************************************************/
static inline void set_city_bonuses(struct city *pcity)
{
  output_type_iterate(o) {
    pcity->bonus[o] = get_city_output_bonus(pcity, o);
  } output_type_iterate_end;
}

/****************************************************************************
  This function sets all the values in the pcity->tile_output[] array. This
  should be called near the beginning of generic_city_refresh.  It doesn't
  depend on anything else in the refresh and doesn't change when workers are
  moved around (but does change when buildings are built, etc.).
****************************************************************************/
static inline void set_city_tile_output(struct city *pcity)
{
  bool is_celebrating = base_city_celebrating(pcity);

  /* Any unreal tiles are skipped - these values should have been memset
   * to 0 when the city was created. */
  city_map_checked_iterate(pcity->tile, x, y, ptile) {
    output_type_iterate(o) {
      pcity->tile_output[x][y][o]
	= base_city_get_output_tile(x, y, pcity, is_celebrating, o);
    } output_type_iterate_end;
  } city_map_checked_iterate_end;
}

/**************************************************************************
  Set the final surplus[] array from the prod[] and usage[] values.
**************************************************************************/
static void set_surpluses(struct city *pcity)
{
  output_type_iterate(o) {
    pcity->surplus[o] = pcity->prod[o] - pcity->usage[o];
  } output_type_iterate_end;
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
  Move up to 'count' citizens from the source to the destination
  happiness categories. For instance

    make_citizens_happy(&pcity->angry[0], &pcity->unhappy[0], 1)

  will make up to 1 angry citizen unhappy.  The number converted will be
  returned.
**************************************************************************/
static inline int make_citizens_happy(int *from, int *to, int count)
{
  count = MIN(count, *from);
  *from -= count;
  *to += count;
  return count;
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
  while (*luxuries >= HAPPY_COST && *angry > 0) {
    /* Upgrade angry to unhappy: costs HAPPY_COST each. */
    (*angry)--;
    (*unhappy)++;
    *luxuries -= HAPPY_COST;
  }
  while (*luxuries >= HAPPY_COST && *content > 0) {
    /* Upgrade content to happy: costs HAPPY_COST each. */
    (*content)--;
    (*happy)++;
    *luxuries -= HAPPY_COST;
  }
  while (*luxuries >= 2 * HAPPY_COST && *unhappy > 0) {
    /* Upgrade unhappy to happy.  Note this is a 2-level upgrade with
     * double the cost. */
    (*unhappy)--;
    (*happy)++;
    *luxuries -= 2 * HAPPY_COST;
  }
  if (*luxuries >= HAPPY_COST && *unhappy > 0) {
    /* Upgrade unhappy to content: costs HAPPY_COST each. */
    (*unhappy)--;
    (*content)++;
    *luxuries -= HAPPY_COST;
  }
}

/**************************************************************************
  Make citizens happy due to luxury.
**************************************************************************/
static inline void citizen_happy_luxury(struct city *pcity)
{
  int x = pcity->prod[O_LUXURY];

  happy_copy(pcity, 0);

  citizen_luxury_happy(pcity, &x, &pcity->ppl_angry[1], &pcity->ppl_unhappy[1], 
                       &pcity->ppl_happy[1], &pcity->ppl_content[1]);
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
  Make citizens happy/unhappy due to units.

  This function requires that pcity->martial_law and
  pcity->unit_happy_cost have already been set in city_support.
**************************************************************************/
static inline void citizen_happy_units(struct city *pcity)
{
  int amt;

  happy_copy(pcity, 2);

  /* Pacify discontent citizens through martial law.  First convert
   * angry->unhappy and then unhappy->content. */
  amt = pcity->martial_law;
  amt -= make_citizens_happy(&pcity->ppl_angry[3], &pcity->ppl_unhappy[3],
			     amt);
  amt -= make_citizens_happy(&pcity->ppl_unhappy[3], &pcity->ppl_content[3],
			     amt);
  /* Any remaining martial law is unused. */

  /* Now make citizens unhappier because of military units away from home.
   * First make content people unhappy, then happy people unhappy,
   * then happy people content. */
  amt = pcity->unit_happy_upkeep;
  amt -= make_citizens_happy(&pcity->ppl_content[3], &pcity->ppl_unhappy[3],
			     amt);
  amt -= 2 * make_citizens_happy(&pcity->ppl_happy[3], &pcity->ppl_unhappy[3],
				 amt / 2);
  amt -= make_citizens_happy(&pcity->ppl_happy[3], &pcity->ppl_content[3],
			     amt);
  /* Any remaining unhappiness is lost since angry citizens aren't created
   * here. */
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
    pcity->unhappy_penalty[O_FOOD]
      = MAX(pcity->prod[O_FOOD] - pcity->usage[O_FOOD], 0);
    pcity->unhappy_penalty[O_SHIELD]
      = MAX(pcity->prod[O_SHIELD] - pcity->usage[O_SHIELD], 0);
    pcity->unhappy_penalty[O_GOLD] = pcity->prod[O_GOLD];
    pcity->unhappy_penalty[O_SCIENCE] = pcity->prod[O_SCIENCE];
    /* Trade and luxury are unaffected. */

    output_type_iterate(o) {
      pcity->prod[o] -= pcity->unhappy_penalty[o];
    } output_type_iterate_end;
  } else {
    memset(pcity->unhappy_penalty, 0,
 	   O_COUNT * sizeof(*pcity->unhappy_penalty));
  }
}

/**************************************************************************
  Calculate the pollution from production and population in the city.
**************************************************************************/
int city_pollution_types(const struct city *pcity, int shield_total,
			 int *pollu_prod, int *pollu_pop, int *pollu_mod)
{
  struct player *pplayer = city_owner(pcity);
  int prod, pop, mod;

  /* Add one one pollution per shield, multipled by the bonus. */
  prod = 100 + get_city_bonus(pcity, EFT_POLLU_PROD_PCT);
  prod = shield_total * MAX(prod, 0) / 100;

  /* Add one 1/4 pollution per citizen per tech, multiplied by the bonus. */
  pop = 100 + get_city_bonus(pcity, EFT_POLLU_POP_PCT);
  pop = (pcity->size
	 * num_known_tech_with_flag(pplayer, TF_POPULATION_POLLUTION_INC)
	 * MAX(pop, 0)) / (4 * 100);

  /* Then there's a base -20 pollution. */
  mod = -20;

  if (pollu_prod) {
    *pollu_prod = prod;
  }
  if (pollu_pop) {
    *pollu_pop = pop;
  }
  if (pollu_mod) {
    *pollu_mod = mod;
  }
  return MAX(prod + pop + mod, 0);
}

/**************************************************************************
  Calculate pollution for the city.  The shield_total must be passed in
  (most callers will want to pass pcity->shield_prod).
**************************************************************************/
int city_pollution(const struct city *pcity, int shield_total)
{
  return city_pollution_types(pcity, shield_total, NULL, NULL, NULL);
}

/**************************************************************************
   Set food, trade and shields production in a city.

   This initializes the prod[] and waste[] arrays.  It assumes that
   the bonus[] and citizen_base[] arrays are alread built.
**************************************************************************/
static inline void set_city_production(struct city *pcity)
{
  int i;

  output_type_iterate(o) {
    pcity->prod[o] = pcity->citizen_base[o];
  } output_type_iterate_end;

  /* Add on special extra incomes: trade routes and tithes. */
  for (i = 0; i < NUM_TRADEROUTES; i++) {
    pcity->trade_value[i] =
	trade_between_cities(pcity, find_city_by_id(pcity->trade[i]));
    pcity->prod[O_TRADE] += pcity->trade_value[i];
  }
  pcity->prod[O_GOLD] += get_city_tithes_bonus(pcity);

  /* Account for waste.  Waste is only calculated for trade and shields.
   * Note that waste is calculated BEFORE tax incomes and BEFORE effects
   * bonuses are included.  This means that shield-waste does not include
   * the shield-bonus from factories, which is surely a bug. */
  pcity->waste[O_TRADE] = city_waste(pcity, O_TRADE, pcity->prod[O_TRADE]);
  pcity->prod[O_TRADE] -= pcity->waste[O_TRADE];
  pcity->waste[O_SHIELD] = city_waste(pcity, O_SHIELD, pcity->prod[O_SHIELD]);
  pcity->prod[O_SHIELD] -= pcity->waste[O_SHIELD];

  /* Convert trade into science/luxury/gold, and add this on to whatever
   * science/luxury/gold is already there. */
  add_tax_income(city_owner(pcity), pcity->prod);

  /* Add on effect bonuses.  Note that this means the waste and tax income
   * above does NOT include the bonuses.  This works for the default
   * ruleset but won't work if there is shield-waste or if there were
   * a trade bonus. */
  output_type_iterate(o) {
    pcity->prod[o] = pcity->prod[o] * pcity->bonus[o] / 100;
  } output_type_iterate_end;
}

/**************************************************************************
  Calculate upkeep costs.  This builds the pcity->usage[] array as well
  as setting some happiness values.
**************************************************************************/
static inline void city_support(struct city *pcity, 
	 		        void (*send_unit_info) (struct player *pplayer,
						        struct unit *punit))
{
  struct government *g = get_gov_pcity(pcity);

  int free_happy = citygov_free_happy(pcity, g);
  int free_upkeep[O_COUNT];

  /* ??  This does the right thing for normal Republic and Democ -- dwp */
  free_happy += get_city_bonus(pcity, EFT_MAKE_CONTENT_MIL);

  output_type_iterate(o) {
    free_upkeep[o] = citygov_free_upkeep(pcity, g, o);
  } output_type_iterate_end;

  /* Clear all usage values. */
  memset(pcity->usage, 0, O_COUNT * sizeof(*pcity->usage));
  pcity->martial_law = 0;
  pcity->unit_happy_upkeep = 0;

  /* Add base amounts for building upkeep and citizen consumption. */
  pcity->usage[O_GOLD] += city_building_upkeep(pcity, O_GOLD);
  pcity->usage[O_FOOD] += 2 * pcity->size;

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
    unit_list_iterate(pcity->tile->units, punit) {
      if (pcity->martial_law < g->martial_law_max
	  && is_military_unit(punit)
	  && punit->owner == pcity->owner) {
	pcity->martial_law++;
      }
    } unit_list_iterate_end;
    pcity->martial_law *= g->martial_law_per;
  }

  /* loop over units, subtracting appropriate amounts of food, shields,
   * gold etc -- SKi */
  unit_list_iterate(pcity->units_supported, this_unit) {
    struct unit_type *ut = unit_type(this_unit);
    int upkeep_cost[O_COUNT], old_upkeep[O_COUNT];
    int happy_cost = utype_happy_cost(ut, g);
    bool changed = FALSE;

    /* Save old values so we can decide if the unit info should be resent */
    int old_unhappiness = this_unit->unhappiness;

    output_type_iterate(o) {
      upkeep_cost[o] = utype_upkeep_cost(ut, g, o);
      old_upkeep[o] = this_unit->upkeep[o];
    } output_type_iterate_end;

    /* set current upkeep on unit to zero */
    this_unit->unhappiness = 0;
    memset(this_unit->upkeep, 0, O_COUNT * sizeof(*this_unit->upkeep));

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
	pcity->unit_happy_upkeep += happy_cost;
	this_unit->unhappiness = happy_cost;
      }
    }
    changed |= (old_unhappiness != happy_cost);

    output_type_iterate(o) {
      if (upkeep_cost[o] > 0) {
	adjust_city_free_cost(&free_upkeep[o], &upkeep_cost[o]);
	if (upkeep_cost[o] > 0) {
	  pcity->usage[o] += upkeep_cost[o];
	  this_unit->upkeep[o] = upkeep_cost[o];
	}
      }
      changed |= (old_upkeep[o] != upkeep_cost[o]);
    } output_type_iterate_end;

    /* Send unit info if anything has changed */
    if (send_unit_info && changed) {
      send_unit_info(unit_owner(this_unit), this_unit);
    }
  } unit_list_iterate_end;
}

/**************************************************************************
  Refreshes the internal cached data in the city structure.

  There are two possible levels of refresh: a partial refresh and a full
  refresh.  A partial refresh is faster but can only be used in a few
  places.

  A full refresh updates all cached data: including but not limited to
  ppl_happy[], surplus[], waste[], citizen_base[], usage[], trade[],
  bonus[], and tile_output[].

  A partial refresh will not update tile_output[] or bonus[].  These two
  values do not need to be recalculated when moving workers around or when
  a trade route has changed.  A partial refresh will also not refresh any
  cities that have trade routes with us.  Any time a partial refresh is
  done it should be considered temporary: when finished, the city should
  be reverted to its original state.
**************************************************************************/
void generic_city_refresh(struct city *pcity,
			  bool full_refresh,
			  void (*send_unit_info) (struct player * pplayer,
						  struct unit * punit))
{
  int prev_tile_trade = pcity->citizen_base[O_TRADE];

  if (full_refresh) {
    set_city_bonuses(pcity);	/* Calculate the bonus[] array values. */
    set_city_tile_output(pcity); /* Calculate the tile_output[] values. */
    city_support(pcity, send_unit_info); /* manage settlers, and units */
  }
  get_citizen_output(pcity, pcity->citizen_base); /* Calculate output from citizens. */
  set_city_production(pcity);
  citizen_happy_size(pcity);
  pcity->pollution = city_pollution(pcity, pcity->prod[O_SHIELD]);
  citizen_happy_luxury(pcity);	/* with our new found luxuries */
  citizen_content_buildings(pcity);	/* temple cathedral colosseum */
  citizen_happy_units(pcity); /* Martial law & unrest from units */
  citizen_happy_wonders(pcity);	/* happy wonders & fundamentalism */
  unhappy_city_check(pcity);
  set_surpluses(pcity);

  if (full_refresh
      && pcity->citizen_base[O_TRADE] != prev_tile_trade) {
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
  Give corruption/waste generated by city.  otype gives the output type
  (O_SHIELD/O_TRADE).  'total' gives the total output of this type in the
  city.
**************************************************************************/
int city_waste(const struct city *pcity, Output_type_id otype, int total)
{
  struct government *g = get_gov_pcity(pcity);
  int dist;
  unsigned int val;
  int penalty = 0;
  struct gov_waste *waste = &g->waste[otype];
  enum effect_type eft = get_output_waste_effect(otype);

  if (otype == O_TRADE) {
    /* FIXME: special case for trade: it is affected by notradesize and
     * fulltradesize server settings. */
    assert(game.notradesize < game.fulltradesize);
    if (pcity->size <= game.notradesize) {
      penalty = total;
    } else if (pcity->size >= game.fulltradesize) {
      penalty = 0;
    } else {
      penalty = total * (game.fulltradesize - pcity->size) /
	(game.fulltradesize - game.notradesize);
    }
  }

  if (waste->level == 0) {
    return penalty;
  }
  if (waste->fixed_distance != 0) {
    dist = waste->fixed_distance;
  } else {
    const struct city *capital = find_palace(city_owner(pcity));

    if (!capital) {
      dist = waste->max_distance_cap;
    } else {
      int tmp = real_map_distance(capital->tile, pcity->tile);

      dist = MIN(waste->max_distance_cap, tmp);
    }
  }
  dist = dist * waste->distance_factor + waste->extra_distance;

  /* Now calculate the final waste.  Ordered to reduce integer
   * roundoff errors. */
  val = total * MAX(dist, 1) * waste->level;
  if (eft != EFT_LAST) {
    val -= (val * get_city_bonus(pcity, eft)) / 100;
  }
  val /= 100 * 100; /* Level is a % multiplied by 100 */
  val = CLIP(penalty, val, total);
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

/****************************************************************************
  Return the "best" specialist available in the game.  This specialist will
  have the most of the given type of output.  If pcity is given then only
  specialists usable by pcity will be considered.
****************************************************************************/
Specialist_type_id best_specialist(Output_type_id otype,
				   const struct city *pcity)
{
  int best = DEFAULT_SPECIALIST;
  int val = game.rgame.specialists[DEFAULT_SPECIALIST].bonus[otype];

  specialist_type_iterate(i) {
    if (!pcity || city_can_use_specialist(pcity, i)) {
      if (game.rgame.specialists[i].bonus[otype] > val) {
	best = i;
	val = game.rgame.specialists[i].bonus[otype];
      }
    }
  } specialist_type_iterate_end;

  return best;
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
  static char buf[5 * SP_MAX];

  specialist_type_iterate(sp) {
    char *separator = (len == 0) ? "" : "/";

    my_snprintf(buf + len, sizeof(buf) - len,
		"%s%d", separator, specialists[sp]);
    len += strlen(buf + len);
  } specialist_type_iterate_end;

  return buf;
}

/**************************************************************************
 Adds an improvement (and its effects) to a city.
**************************************************************************/
void city_add_improvement(struct city *pcity, Impr_Type_id impr)
{
  pcity->improvements[impr] = I_ACTIVE;
}

/**************************************************************************
 Removes an improvement (and its effects) from a city.
**************************************************************************/
void city_remove_improvement(struct city *pcity, Impr_Type_id impr)
{
  freelog(LOG_DEBUG,"Improvement %s removed from city %s",
          improvement_types[impr].name, pcity->name);
  
  pcity->improvements[impr] = I_NONE;
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
struct city *create_city_virtual(const struct player *pplayer,
		                 struct tile *ptile, const char *name)
{
  int i;
  struct city *pcity;

  pcity = fc_malloc(sizeof(struct city));

  pcity->id = 0;
  pcity->owner = pplayer->player_no;
  pcity->tile = ptile;
  sz_strlcpy(pcity->name, name);
  pcity->size = 1;
  memset(pcity->tile_output, 0, sizeof(pcity->tile_output));
  specialist_type_iterate(sp) {
    pcity->specialists[sp] = 0;
  } specialist_type_iterate_end;
  pcity->specialists[DEFAULT_SPECIALIST] = 1;
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
  pcity->original = pplayer->player_no;

  /* Initialise improvements list */
  for (i = 0; i < ARRAY_SIZE(pcity->improvements); i++) {
    pcity->improvements[i] = I_NONE;
  }

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

  memset(pcity->surplus, 0, O_COUNT * sizeof(*pcity->surplus));
  memset(pcity->waste, 0, O_COUNT * sizeof(*pcity->waste));
  memset(pcity->unhappy_penalty, 0,
	 O_COUNT * sizeof(*pcity->unhappy_penalty));
  memset(pcity->prod, 0, O_COUNT * sizeof(*pcity->prod));
  memset(pcity->citizen_base, 0, O_COUNT * sizeof(*pcity->citizen_base));
  output_type_iterate(o) {
    pcity->bonus[o] = 100;
  } output_type_iterate_end;

  pcity->units_supported = unit_list_new();

  pcity->client.occupied = FALSE;
  pcity->client.happy = pcity->client.unhappy = FALSE;
  pcity->client.colored = FALSE;

  pcity->debug = FALSE;

  return pcity;
}

/**************************************************************************
  Removes the virtual skeleton of a city. You should already have removed
  all buildings and units you have added to the city before this.
**************************************************************************/
void remove_city_virtual(struct city *pcity)
{
  unit_list_free(pcity->units_supported);
  free(pcity);
}
