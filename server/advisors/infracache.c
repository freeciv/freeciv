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
#include <fc_config.h>
#endif

/* common */
#include "city.h"
#include "map.h"
#include "player.h"
#include "tile.h"

/* server */
#include "citytools.h"
#include "maphand.h"

#include "infracache.h"

/* cache activities within the city map */
struct worker_activity_cache {
  int act[ACTIVITY_LAST];
};

static int adv_calc_irrigate(const struct city *pcity,
                             const struct tile *ptile);
static int adv_calc_mine(const struct city *pcity, const struct tile *ptile);
static int adv_calc_transform(const struct city *pcity,
                              const struct tile *ptile);
static int adv_calc_pollution(const struct city *pcity,
                              const struct tile *ptile, int best);
static int adv_calc_fallout(const struct city *pcity,
                            const struct tile *ptile, int best);
static int adv_calc_road(const struct city *pcity, const struct tile *ptile);
static int adv_calc_railroad(const struct city *pcity,
                             const struct tile *ptile);

/**************************************************************************
  Calculate the benefit of irrigating the given tile.

  The return value is the goodness of the tile after the irrigation.  This
  should be compared to the goodness of the tile currently (see
  city_tile_value(); note that this depends on the AI's weighting
  values).
**************************************************************************/
static int adv_calc_irrigate(const struct city *pcity,
                             const struct tile *ptile)
{
  int goodness, farmland_goodness;
  struct terrain *old_terrain, *new_terrain;

  fc_assert_ret_val(ptile != NULL, -1)

  old_terrain = tile_terrain(ptile);
  new_terrain = old_terrain->irrigation_result;

  if (new_terrain != old_terrain && new_terrain != T_NONE) {
    if (tile_city(ptile) && terrain_has_flag(new_terrain, TER_NO_CITIES)) {
      /* Not a valid activity. */
      return -1;
    }
    /* Irrigation would change the terrain type, clearing the mine
     * in the process.  Calculate the benefit of doing so. */
    struct tile *vtile = tile_virtual_new(ptile);
    tile_change_terrain(vtile, new_terrain);
    tile_clear_special(vtile, S_MINE);
    goodness = city_tile_value(pcity, vtile, 0, 0);
    tile_virtual_destroy(vtile);
    return goodness;
  } else if (old_terrain == new_terrain
             && !tile_has_special(ptile, S_IRRIGATION)) {
    /* The tile is currently unirrigated; irrigating it would put an
     * S_IRRIGATE on it replacing any S_MINE already there.  Calculate
     * the benefit of doing so. */
    struct tile *vtile = tile_virtual_new(ptile);
    tile_clear_special(vtile, S_MINE);
    tile_set_special(vtile, S_IRRIGATION);
    goodness = city_tile_value(pcity, vtile, 0, 0);
    tile_virtual_destroy(vtile);
    /* If the player can further irrigate to make farmland, consider the
     * potentially greater benefit.  Note the hack: autosettler ordinarily
     * discounts benefits by the time it takes to make them; farmland takes
     * twice as long, so make it look half as good. */
    if (player_knows_techs_with_flag(city_owner(pcity), TF_FARMLAND)) {
      int oldv = city_tile_value(pcity, ptile, 0, 0);
      vtile = tile_virtual_new(ptile);
      tile_clear_special(vtile, S_MINE);
      tile_set_special(vtile, S_IRRIGATION);
      tile_set_special(vtile, S_FARMLAND);
      farmland_goodness = city_tile_value(pcity, vtile, 0, 0);
      farmland_goodness = oldv + (farmland_goodness - oldv) / 2;
      if (farmland_goodness > goodness) {
        goodness = farmland_goodness;
      }
      tile_virtual_destroy(vtile);
    }
    return goodness;
  } else if (old_terrain == new_terrain
             && tile_has_special(ptile, S_IRRIGATION)
             && !tile_has_special(ptile, S_FARMLAND)
             && player_knows_techs_with_flag(city_owner(pcity), TF_FARMLAND)) {
    /* The tile is currently irrigated; irrigating it more puts an
     * S_FARMLAND on it.  Calculate the benefit of doing so. */
    struct tile *vtile = tile_virtual_new(ptile);
    fc_assert(!tile_has_special(vtile, S_MINE));
    tile_set_special(vtile, S_FARMLAND);
    goodness = city_tile_value(pcity, vtile, 0, 0);
    tile_virtual_destroy(vtile);
    return goodness;
  } else {
    return -1;
  }
}

/**************************************************************************
  Calculate the benefit of mining the given tile.

  The return value is the goodness of the tile after the mining.  This
  should be compared to the goodness of the tile currently (see
  city_tile_value(); note that this depends on the AI's weighting
  values).
**************************************************************************/
static int adv_calc_mine(const struct city *pcity, const struct tile *ptile)
{
  int goodness;
  struct terrain *old_terrain, *new_terrain;

  fc_assert_ret_val(ptile != NULL, -1)

  old_terrain = tile_terrain(ptile);
  new_terrain = old_terrain->mining_result;

  if (old_terrain != new_terrain && new_terrain != T_NONE) {
    if (tile_city(ptile) && terrain_has_flag(new_terrain, TER_NO_CITIES)) {
      /* Not a valid activity. */
      return -1;
    }
    /* Mining would change the terrain type, clearing the irrigation
     * in the process.  Calculate the benefit of doing so. */
    struct tile *vtile = tile_virtual_new(ptile);
    tile_change_terrain(vtile, new_terrain);
    tile_clear_special(vtile, S_IRRIGATION);
    tile_clear_special(vtile, S_FARMLAND);
    goodness = city_tile_value(pcity, vtile, 0, 0);
    tile_virtual_destroy(vtile);
    return goodness;
  } else if (old_terrain == new_terrain
             && !tile_has_special(ptile, S_MINE)) {
    /* The tile is currently unmined; mining it would put an S_MINE on it
     * replacing any S_IRRIGATION/S_FARMLAND already there.  Calculate
     * the benefit of doing so. */
    struct tile *vtile = tile_virtual_new(ptile);
    tile_clear_special(vtile, S_IRRIGATION);
    tile_clear_special(vtile, S_FARMLAND);
    tile_set_special(vtile, S_MINE);
    goodness = city_tile_value(pcity, vtile, 0, 0);
    tile_virtual_destroy(vtile);
    return goodness;
  } else {
    return -1;
  }
}

/**************************************************************************
  Calculate the benefit of transforming the given tile.

  The return value is the goodness of the tile after the transform.  This
  should be compared to the goodness of the tile currently (see
  city_tile_value(); note that this depends on the AI's weighting
  values).
**************************************************************************/
static int adv_calc_transform(const struct city *pcity,
                             const struct tile *ptile)
{
  int goodness;
  struct tile *vtile;
  struct terrain *old_terrain, *new_terrain;

  fc_assert_ret_val(ptile != NULL, -1)

  old_terrain = tile_terrain(ptile);
  new_terrain = old_terrain->transform_result;

  if (old_terrain == new_terrain || new_terrain == T_NONE) {
    return -1;
  }

  if (is_ocean(old_terrain) && !is_ocean(new_terrain)
      && !can_reclaim_ocean(ptile)) {
    /* Can't change ocean into land here. */
    return -1;
  }
  if (is_ocean(new_terrain) && !is_ocean(old_terrain)
      && !can_channel_land(ptile)) {
    /* Can't change land into ocean here. */
    return -1;
  }

  if (tile_city(ptile) && terrain_has_flag(new_terrain, TER_NO_CITIES)) {
    return -1;
  }

  vtile = tile_virtual_new(ptile);
  tile_change_terrain(vtile, new_terrain);
  goodness = city_tile_value(pcity, vtile, 0, 0);
  tile_virtual_destroy(vtile);

  return goodness;
}

/**************************************************************************
  Calculates the value of removing pollution at the given tile.

  The return value is the goodness of the tile after the cleanup.  This
  should be compared to the goodness of the tile currently (see
  city_tile_value(); note that this depends on the AI's weighting
  values).
**************************************************************************/
static int adv_calc_pollution(const struct city *pcity,
                              const struct tile *ptile, int best)
{
  int goodness;
  struct tile *vtile;

  fc_assert_ret_val(ptile != NULL, -1)

  if (!tile_has_special(ptile, S_POLLUTION)) {
    return -1;
  }

  vtile = tile_virtual_new(ptile);
  tile_clear_special(vtile, S_POLLUTION);
  goodness = city_tile_value(pcity, vtile, 0, 0);

  /* FIXME: need a better way to guarantee pollution is cleaned up. */
  goodness = (goodness + best + 50) * 2;

  tile_virtual_destroy(vtile);

  return goodness;
}

/**************************************************************************
  Calculates the value of removing fallout at the given tile.

  The return value is the goodness of the tile after the cleanup.  This
  should be compared to the goodness of the tile currently (see
  city_tile_value(); note that this depends on the AI's weighting
  values).
**************************************************************************/
static int adv_calc_fallout(const struct city *pcity,
                            const struct tile *ptile, int best)
{
  int goodness;
  struct tile *vtile;

  fc_assert_ret_val(ptile != NULL, -1)

  if (!tile_has_special(ptile, S_FALLOUT)) {
    return -1;
  }

  vtile = tile_virtual_new(ptile);
  tile_clear_special(vtile, S_FALLOUT);
  goodness = city_tile_value(pcity, vtile, 0, 0);

  /* FIXME: need a better way to guarantee fallout is cleaned up. */
  if (!city_owner(pcity)->ai_controlled) {
    goodness = (goodness + best + 50) * 2;
  }

  tile_virtual_destroy(vtile);

  return goodness;
}

/**************************************************************************
  Calculate the benefit of building a road at the given tile.

  The return value is the goodness of the tile after the road is built.
  This should be compared to the goodness of the tile currently (see
  city_tile_value(); note that this depends on the AI's weighting
  values).

  This function does not calculate the benefit of being able to quickly
  move units (i.e., of connecting the civilization).  See road_bonus() for
  that calculation.
**************************************************************************/
static int adv_calc_road(const struct city *pcity, const struct tile *ptile)
{
  int goodness = -1;

  fc_assert_ret_val(ptile != NULL, -1)

  if (!is_ocean_tile(ptile)
      && (!tile_has_special(ptile, S_RIVER)
          || player_knows_techs_with_flag(city_owner(pcity), TF_BRIDGE))
      && !tile_has_special(ptile, S_ROAD)) {
    struct tile *vtile = tile_virtual_new(ptile);
    set_special(&vtile->special, S_ROAD);
    goodness = city_tile_value(pcity, vtile, 0, 0);
    tile_virtual_destroy(vtile);
  }

  return goodness;
}

/**************************************************************************
  Calculate the benefit of building a railroad at the given tile.

  The return value is the goodness of the tile after the railroad is built.
  This should be compared to the goodness of the tile currently (see
  city_tile_value(); note that this depends on the AI's weighting
  values).

  This function does not calculate the benefit of being able to quickly
  move units (i.e., of connecting the civilization).  See road_bonus() for
  that calculation.
**************************************************************************/
static int adv_calc_railroad(const struct city *pcity,
                            const struct tile *ptile)
{
  int goodness = -1;
  struct road_type *proad = road_by_special(S_RAILROAD);

  fc_assert_ret_val(ptile != NULL, -1);

  if (proad == NULL) {
    /* No railroad type in ruleset */
    return -1;
  }

  if (!is_ocean_tile(ptile)
      && player_can_build_road(proad, city_owner(pcity), ptile)
      && !tile_has_road(ptile, proad)) {
    struct tile *vtile = tile_virtual_new(ptile);
    set_special(&vtile->special, S_ROAD);
    set_special(&vtile->special, S_RAILROAD);
    goodness = city_tile_value(pcity, vtile, 0, 0);
    tile_virtual_destroy(vtile);
  }

  return goodness;
}

/**************************************************************************
  Returns city_tile_value of the best tile worked by or available to pcity.
**************************************************************************/
static int best_worker_tile_value(struct city *pcity)
{
  struct tile *pcenter = city_tile(pcity);
  int best = 0;

  city_tile_iterate(city_map_radius_sq_get(pcity), pcenter, ptile) {
    if (is_free_worked(pcity, ptile)
	|| tile_worked(ptile) == pcity /* quick test */
	|| city_can_work_tile(pcity, ptile)) {
      int tmp = city_tile_value(pcity, ptile, 0, 0);

      if (best < tmp) {
	best = tmp;
      }
    }
  } city_tile_iterate_end;

  return best;
}

/**************************************************************************
  Do all tile improvement calculations and cache them for later.

  These values are used in settler_evaluate_improvements() so this function
  must be called before doing that.  Currently this is only done when handling
  auto-settlers or when the AI contemplates building worker units.
**************************************************************************/
void initialize_infrastructure_cache(struct player *pplayer)
{
  city_list_iterate(pplayer->cities, pcity) {
    struct tile *pcenter = city_tile(pcity);
    int radius_sq = city_map_radius_sq_get(pcity);
    int best = best_worker_tile_value(pcity);

    city_map_iterate(radius_sq, city_index, city_x, city_y) {
      activity_type_iterate(act) {
        adv_city_worker_act_set(pcity, city_index, act, -1);
      } activity_type_iterate_end;
    } city_map_iterate_end;

    city_tile_iterate_index(radius_sq, pcenter, ptile, cindex) {
      adv_city_worker_act_set(pcity, cindex, ACTIVITY_POLLUTION,
                              adv_calc_pollution(pcity, ptile, best));
      adv_city_worker_act_set(pcity, cindex, ACTIVITY_FALLOUT,
                              adv_calc_fallout(pcity, ptile, best));
      adv_city_worker_act_set(pcity, cindex, ACTIVITY_MINE,
                              adv_calc_mine(pcity, ptile));
      adv_city_worker_act_set(pcity, cindex, ACTIVITY_IRRIGATE,
                              adv_calc_irrigate(pcity, ptile));
      adv_city_worker_act_set(pcity, cindex, ACTIVITY_TRANSFORM,
                              adv_calc_transform(pcity, ptile));

      /* road_bonus() is handled dynamically later; it takes into
       * account settlers that have already been assigned to building
       * roads this turn. */
      adv_city_worker_act_set(pcity, cindex, ACTIVITY_ROAD,
                              adv_calc_road(pcity, ptile));
      adv_city_worker_act_set(pcity, cindex, ACTIVITY_RAILROAD,
                              adv_calc_railroad(pcity, ptile));
    } city_tile_iterate_index_end;
  } city_list_iterate_end;
}

/**************************************************************************
  Returns a measure of goodness of a tile to pcity.

  FIXME: foodneed and prodneed are always 0.
**************************************************************************/
int city_tile_value(const struct city *pcity, const struct tile *ptile,
                    int foodneed, int prodneed)
{
  int food = city_tile_output_now(pcity, ptile, O_FOOD);
  int shield = city_tile_output_now(pcity, ptile, O_SHIELD);
  int trade = city_tile_output_now(pcity, ptile, O_TRADE);
  int value = 0;

  /* Each food, trade, and shield gets a certain weighting.  We also benefit
   * tiles that have at least one of an item - this promotes balance and 
   * also accounts for INC_TILE effects. */
  value += food * FOOD_WEIGHTING;
  if (food > 0) {
    value += FOOD_WEIGHTING / 2;
  }
  value += shield * SHIELD_WEIGHTING;
  if (shield > 0) {
    value += SHIELD_WEIGHTING / 2;
  }
  value += trade * TRADE_WEIGHTING;
  if (trade > 0) {
    value += TRADE_WEIGHTING / 2;
  }

  return value;
}

/**************************************************************************
  Set the value for activity 'doing' on tile 'city_tile_index' of
  city 'pcity'.
**************************************************************************/
void adv_city_worker_act_set(struct city *pcity, int city_tile_index,
                             enum unit_activity act_id, int value)
{
  if (pcity->server.adv->act_cache_radius_sq
      != city_map_radius_sq_get(pcity)) {
    log_debug("update activity cache for %s: radius_sq changed from "
              "%d to %d", city_name(pcity),
              pcity->server.adv->act_cache_radius_sq,
              city_map_radius_sq_get(pcity));
    adv_city_update(pcity);
  }

  fc_assert_ret(NULL != pcity);
  fc_assert_ret(NULL != pcity->server.adv);
  fc_assert_ret(NULL != pcity->server.adv->act_cache);
  fc_assert_ret(pcity->server.adv->act_cache_radius_sq
                == city_map_radius_sq_get(pcity));
  fc_assert_ret(city_tile_index < city_map_tiles_from_city(pcity));

  (pcity->server.adv->act_cache[city_tile_index]).act[act_id] = value;
}

/**************************************************************************
  Return the value for activity 'doing' on tile 'city_tile_index' of
  city 'pcity'.
**************************************************************************/
int adv_city_worker_act_get(const struct city *pcity, int city_tile_index,
                            enum unit_activity act_id)
{
  fc_assert_ret_val(NULL != pcity, 0);
  fc_assert_ret_val(NULL != pcity->server.adv, 0);
  fc_assert_ret_val(NULL != pcity->server.adv->act_cache, 0);
  fc_assert_ret_val(pcity->server.adv->act_cache_radius_sq
                     == city_map_radius_sq_get(pcity), 0);
  fc_assert_ret_val(city_tile_index < city_map_tiles_from_city(pcity), 0);

  return (pcity->server.adv->act_cache[city_tile_index]).act[act_id];
}

/**************************************************************************
  Update the memory allocated for AI city handling.
**************************************************************************/
void adv_city_update(struct city *pcity)
{
  int radius_sq = city_map_radius_sq_get(pcity);

  fc_assert_ret(NULL != pcity);
  fc_assert_ret(NULL != pcity->server.adv);

  /* initialize act_cache if needed */
  if (pcity->server.adv->act_cache == NULL
      || pcity->server.adv->act_cache_radius_sq == -1
      || pcity->server.adv->act_cache_radius_sq != radius_sq) {
    pcity->server.adv->act_cache
      = fc_realloc(pcity->server.adv->act_cache,
                   city_map_tiles(radius_sq)
                   * sizeof(*(pcity->server.adv->act_cache)));
    /* initialize with 0 */
    memset(pcity->server.adv->act_cache, 0,
           city_map_tiles(radius_sq)
           * sizeof(*(pcity->server.adv->act_cache)));
    pcity->server.adv->act_cache_radius_sq = radius_sq;
  }
}

/**************************************************************************
  Allocate advisors related city data
**************************************************************************/
void adv_city_alloc(struct city *pcity)
{
  pcity->server.adv = fc_calloc(1, sizeof(*pcity->server.adv));

  pcity->server.adv->act_cache = NULL;
  pcity->server.adv->act_cache_radius_sq = -1;
  /* allocate memory for pcity->ai->act_cache */
  adv_city_update(pcity);
}

/**************************************************************************
  Free advisors related city data
**************************************************************************/
void adv_city_free(struct city *pcity)
{
  fc_assert_ret(NULL != pcity);

  if (pcity->server.adv) {
    if (pcity->server.adv->act_cache) {
      FC_FREE(pcity->server.adv->act_cache);
    }
    FC_FREE(pcity->server.adv);
  }
}
