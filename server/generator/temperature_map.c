/***********************************************************************
 Freeciv - Copyright (C) 2004 - Marcelo J. Burda
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

/* utilities */
#include "log.h"

/* common */
#include "map.h"

/* generator */
#include "height_map.h"
#include "mapgen_topology.h"
#include "mapgen_utils.h"

#include "temperature_map.h"

static int *temperature_map;

#define tmap(_tile) (temperature_map[tile_index(_tile)])

/**********************************************************************//**
  Returns one line (given by the y coordinate) of the temperature map.
**************************************************************************/
#ifdef FREECIV_DEBUG
static char *tmap_y2str(int ycoor)
{
  static char buf[MAP_MAX_LINEAR_SIZE + 1];
  char *p = buf;
  int i, idx;

  for (i = 0; i < MAP_NATIVE_WIDTH; i++) {
    idx = ycoor * MAP_NATIVE_WIDTH + i;

    if (idx > MAP_NATIVE_WIDTH * MAP_NATIVE_HEIGHT) {
      break;
    }

    switch (temperature_map[idx]) {
    case TT_TROPICAL:
      *p++ = 't'; /* Tropical */
      break;
    case TT_TEMPERATE:
      *p++ = 'm'; /* Medium */
      break;
    case TT_COLD:
      *p++ = 'c'; /* Cold */
      break;
    case TT_FROZEN:
      *p++ = 'f'; /* Frozen */
      break;
    }
  }

  *p = '\0';

  return buf;
}
#endif /* FREECIV_DEBUG */

/**********************************************************************//**
  Return TRUE if temperateure_map is initialized
**************************************************************************/
bool temperature_is_initialized(void)
{
  return temperature_map != NULL;
}

/**********************************************************************//**
  Return true if the tile has tt temperature type
**************************************************************************/
bool tmap_is(const struct tile *ptile, temperature_type tt)
{
  return BOOL_VAL(tmap(ptile) & (tt));
}

/**********************************************************************//**
  Return true if at least one tile has tt temperature type
**************************************************************************/
bool is_temperature_type_near(const struct tile *ptile, temperature_type tt)
{
  adjc_iterate(&(wld.map), ptile, tile1) {
    if (BOOL_VAL(tmap(tile1) & (tt))) {
      return TRUE;
    };
  } adjc_iterate_end;

  return FALSE;
}

/**********************************************************************//**
   Free the tmap
**************************************************************************/
void destroy_tmap(void)
{
  fc_assert_ret(NULL != temperature_map);
  free(temperature_map);
  temperature_map = NULL;
}

/**********************************************************************//**
  Initialize the temperature_map
  if arg is FALSE, create a dummy tmap == map_colatitude
  to be used if hmap or oceans are not placed gen 2-4
**************************************************************************/
void create_tmap(bool real)
{
  int i;

  /* If map is defined this is not changed. */
  /* TODO: Load if from scenario game with tmap */
  /* to debug, never load at this time */
  fc_assert_ret(NULL == temperature_map);

  temperature_map = fc_malloc(sizeof(*temperature_map) * MAP_INDEX_SIZE);
  whole_map_iterate(&(wld.map), ptile) {
    /* The base temperature is equal to base map_colatitude */
    int t = map_colatitude(ptile);

    if (!real) {
      tmap(ptile) = t;
    } else {
      /* High land can be 30% cooler */
      float height = - 0.3 * MAX(0, hmap(ptile) - hmap_shore_level)
          / (hmap_max_level - hmap_shore_level);
      int tcn = count_terrain_class_near_tile(&(wld.map), ptile, FALSE, TRUE, TC_OCEAN);
      /* Near ocean temperature can be 15% more "temperate" */
      float temperate = (0.15 * (wld.map.server.temperature / 100 - t
                                 / MAX_COLATITUDE)
                         * 2 * MIN(50, tcn)
                         / 100);

      tmap(ptile) =  t * (1.0 + temperate) * (1.0 + height);
    }
  } whole_map_iterate_end;

  /* Adjust to get evenly distributed frequencies.
   * Only call adjust when the colatitude range is large enough for this to
   * make sense - if most variation comes from height and coast, don't try
   * to squish that back into its original narrow range */
  if (REAL_COLATITUDE_RANGE(wld.map) >= MAX_COLATITUDE * 2 / 5) {
    adjust_int_map(temperature_map, MIN_REAL_COLATITUDE(wld.map),
                   MAX_REAL_COLATITUDE(wld.map));
  }

  /* Now simplify to 4 base values */
  for (i = 0; i < MAP_INDEX_SIZE; i++) {
    int t = temperature_map[i];

    if (t >= TROPICAL_LEVEL) {
      temperature_map[i] = TT_TROPICAL;
    } else if (t >= COLD_LEVEL) {
      temperature_map[i] = TT_TEMPERATE;
    } else if (t >= 2 * ICE_BASE_LEVEL) {
      temperature_map[i] = TT_COLD;
    } else {
      temperature_map[i] = TT_FROZEN;
    }
  }

  log_debug("%stemperature map ({f}rozen, {c}old, {m}edium, {t}ropical):",
            real ? "real " : "");
  for (i = 0; i < MAP_NATIVE_HEIGHT; i++) {
    log_debug("%5d: %s", i, tmap_y2str(i));
  }
}
