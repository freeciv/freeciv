/***********************************************************************
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

#include <stdlib.h>
#include <string.h>
#include <math.h> /* pow, sqrt, exp */

/* utility */
#include "distribute.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "support.h"

/* common */
#include "ai.h"
#include "citizens.h"
#include "counters.h"
#include "effects.h"
#include "game.h"
#include "government.h"
#include "improvement.h"
#include "map.h"
#include "movement.h"
#include "packets.h"
#include "specialist.h"
#include "traderoutes.h"
#include "unit.h"

/* aicore */
#include "cm.h"

#include "city.h"

/* Define this to add in extra (very slow) assertions for the city code. */
#undef CITY_DEBUGGING

static char *citylog_map_line(int y, int city_radius_sq, int *city_map_data);
#ifdef FREECIV_DEBUG
/* Only used for debugging */
static void citylog_map_index(enum log_level level);
static void citylog_map_radius_sq(enum log_level level);
#endif /* FREECIV_DEBUG */

/* Get city tile information using the city tile index. */
static struct iter_index *city_map_index = NULL;
/* Get city tile information using the city tile coordinates. This is an
 * [x][y] array of integer values corresponding to city_map_index. The
 * coordinates x and y are in the range [0, CITY_MAP_MAX_SIZE] */
static int city_map_xy[CITY_MAP_MAX_SIZE][CITY_MAP_MAX_SIZE];

/* Number of tiles of a city; depends on the squared city radius */
static int city_map_numtiles[CITY_MAP_MAX_RADIUS_SQ + 1];

/* Definitions and functions for the tile_cache */
struct tile_cache {
  int output[O_LAST];
};

static inline void city_tile_cache_update(const struct civ_map *nmap,
                                          struct city *pcity);
static inline int city_tile_cache_get_output(const struct city *pcity,
                                             int city_tile_index,
                                             enum output_type_id o);

static const struct city *nearest_gov_center(const struct city *pcity,
                                             int *min_dist)
  fc__attribute((nonnull (1, 2)));

struct citystyle *city_styles = NULL;

/* One day these values may be read in from the ruleset. In the meantime
 * they're just an easy way to access information about each output type. */
struct output_type output_types[O_LAST] = {
  {O_FOOD, N_("Food"), "food", TRUE, UNHAPPY_PENALTY_SURPLUS},
  {O_SHIELD, N_("Shield"), "shield", TRUE, UNHAPPY_PENALTY_SURPLUS},
  {O_TRADE, N_("Trade"), "trade", TRUE, UNHAPPY_PENALTY_NONE},
  {O_GOLD, N_("Gold"), "gold", FALSE, UNHAPPY_PENALTY_ALL_PRODUCTION},
  {O_LUXURY, N_("Luxury"), "luxury", FALSE, UNHAPPY_PENALTY_NONE},
  {O_SCIENCE, N_("Science"), "science", FALSE, UNHAPPY_PENALTY_ALL_PRODUCTION}
};

/**********************************************************************//**
  Returns the coordinates for the given city tile index taking into account
  the squared city radius.
**************************************************************************/
bool city_tile_index_to_xy(int *city_map_x, int *city_map_y,
                           int city_tile_index, int city_radius_sq)
{
  fc_assert_ret_val(city_radius_sq >= CITY_MAP_MIN_RADIUS_SQ, FALSE);
  fc_assert_ret_val(city_radius_sq <= CITY_MAP_MAX_RADIUS_SQ, FALSE);

  /* tile indices are sorted from smallest to largest city radius */
  if (city_tile_index < 0
      || city_tile_index >= city_map_tiles(city_radius_sq)) {
    return FALSE;
  }

  *city_map_x = CITY_REL2ABS(city_map_index[city_tile_index].dx);
  *city_map_y = CITY_REL2ABS(city_map_index[city_tile_index].dy);

  return TRUE;
}

/**********************************************************************//**
  Returns the index for the given city tile coordinates taking into account
  the squared city radius.
**************************************************************************/
int city_tile_xy_to_index(int city_map_x, int city_map_y,
                          int city_radius_sq)
{
  fc_assert_ret_val(city_radius_sq >= CITY_MAP_MIN_RADIUS_SQ, 0);
  fc_assert_ret_val(city_radius_sq <= CITY_MAP_MAX_RADIUS_SQ, 0);
  fc_assert_ret_val(is_valid_city_coords(city_radius_sq, city_map_x,
                                         city_map_y), 0);

  return city_map_xy[city_map_x][city_map_y];
}

/**********************************************************************//**
  Returns the current squared radius of the city.
**************************************************************************/
int city_map_radius_sq_get(const struct city *pcity)
{
  /* a save return value is only the minimal squared radius */
  fc_assert_ret_val(pcity != NULL, CITY_MAP_MIN_RADIUS_SQ);

  return pcity->city_radius_sq;
}

/**********************************************************************//**
  Sets the squared radius for the city.
**************************************************************************/
void city_map_radius_sq_set(struct city *pcity, int radius_sq)
{
  fc_assert_ret(radius_sq >= CITY_MAP_MIN_RADIUS_SQ);
  fc_assert_ret(radius_sq <= CITY_MAP_MAX_RADIUS_SQ);

  pcity->city_radius_sq = radius_sq;
}

/**********************************************************************//**
  Maximum city radius in this ruleset.
**************************************************************************/
int rs_max_city_radius_sq(void)
{
  int max_rad = game.info.init_city_radius_sq
    + effect_cumulative_max(EFT_CITY_RADIUS_SQ, NULL, 0);

  return MIN(max_rad, CITY_MAP_MAX_RADIUS_SQ);
}

/**********************************************************************//**
  Return the number of tiles for the given city radius. Special case is
  the value -1 for no city tiles.
**************************************************************************/
int city_map_tiles(int city_radius_sq)
{
  if (city_radius_sq == CITY_MAP_CENTER_RADIUS_SQ) {
    /* special case: city center; first tile of the city map */
    return 0;
  }

  fc_assert_ret_val(city_radius_sq >= CITY_MAP_MIN_RADIUS_SQ, -1);
  fc_assert_ret_val(city_radius_sq <= CITY_MAP_MAX_RADIUS_SQ, -1);

  return city_map_numtiles[city_radius_sq];
}

/**********************************************************************//**
  Return TRUE if the given city coordinate pair is "valid"; that is, if it
  is a part of the citymap and thus is workable by the city.
**************************************************************************/
bool is_valid_city_coords(const int city_radius_sq, const int city_map_x,
                          const int city_map_y)
{
  /* The city's valid positions are in a circle around the city center.
   * Depending on the value for the squared city radius the circle will be:
   *
   *  - rectangular (max radius = 5; max squared radius = 26)
   *
   *        0    1    2    3    4    5    6    7    8    9   10
   *
   *  0                        26   25   26                       -5
   *  1              25   20   17   16   17   20   25             -4
   *  2         25   18   13   10    9   10   13   18   25        -3
   *  3         20   13    8    5    4    5    8   13   20        -2
   *  4    26   17   10    5    2    1    2    5   10   17   26   -1
   *  5    25   16    9    4    1    0    1    4    9   16   25   +0
   *  6    26   17   10    5    2    1    2    5   10   17   26   +1
   *  7         20   13    8    5    4    5    8   13   20        +2
   *  8         25   18   13   10    9   10   13   18   25        +3
   *  9              25   20   17   16   17   20   25             +4
   * 10                        26   25   26                       +5
   *
   *       -5   -4   -3   -2   -1   +0   +1   +2   +3   +4   +5
   *
   * - hexagonal (max radius = 5; max squared radius = 26)
   *
   *        0    1    2    3    4    5    6    7    8    9   10
   *
   *  0                             25   25   25   25   25   25   -5
   *  1                        25   16   16   16   16   16   25   -4
   *  2                   25   16    9    9    9    9   16   25   -3
   *  3              25   16    9    4    4    4    9   16   25   -2
   *  4         25   16    9    4    1    1    4    9   16   25   -1
   *  5    25   16    9    4    1    0    1    4    9   16   25   +0
   *  6    25   16    9    4    1    1    4    9   16   25        +1
   *  7    25   16    9    4    4    4    9   16   25             +2
   *  8    25   16    9    9    9    9   16   25                  +3
   *  9    25   16   16   16   16   16   25                       +4
   * 10    25   25   25   25   25   25                            +5
   *
   *       -5   -4   -3   -2   -1   +0   +1   +2   +3   +4   +5
   *
   * The following tables show the tiles per city radii / squared city radii.
   * '-' indicates no change compared to the previous value
   *
   * radius            |  0 |  1 |    |    |  2 |    |    |    |    |  3
   * radius_sq         |  1 |  2 |  3 |  4 |  5 |  6 |  7 |  8 |  9 | 10
   * ------------------+----+----+----+----+----+----+----+----+----+----
   * tiles rectangular |  5 |  9 |  - | 13 | 21 |  - |  - | 25 | 29 | 37
   * tiles hexagonal   |  7 |  - |  - | 19 |  - |  - |  - |  - | 37 |  -
   *
   * radius            |    |    |    |    |    |    |  4 |    |    |
   * radius_sq         | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20
   * ------------------+----+----+----+----+----+----+----+----+----+----
   * tiles rectangular |  - |  - | 45 |  - |  - | 49 | 57 | 61 |  - | 69
   * tiles hexagonal   |  - |  - |  - |  - |  - | 61 |  - |  - |  - |  -
   *
   * radius            |    |    |    |    |    |  5
   * radius_sq         | 21 | 22 | 23 | 24 | 25 | 26
   * ------------------+----+----+----+----+----+----
   * tiles rectangular |  - |  - |  - |  - | 81 | 89
   * tiles hexagonal   |  - |  - |  - |  - | 91 |  -
   *
   * So radius_sq == 5 (radius == 2) corresponds to the "traditional"
   * used city map.
   */
  int dist = map_vector_to_sq_distance(CITY_ABS2REL(city_map_x),
                                       CITY_ABS2REL(city_map_y));

  return dist <= city_radius_sq;
}

/**********************************************************************//**
  Finds the city map coordinate for a given map position and a city
  center. Returns whether the map position is inside of the city map.
**************************************************************************/
bool city_tile_to_city_map(int *city_map_x, int *city_map_y,
                           const int city_radius_sq,
                           const struct tile *city_center,
                           const struct tile *map_tile)
{
  map_distance_vector(city_map_x, city_map_y, city_center, map_tile);

  *city_map_x += CITY_MAP_MAX_RADIUS;
  *city_map_y += CITY_MAP_MAX_RADIUS;

  return is_valid_city_coords(city_radius_sq, *city_map_x, *city_map_y);
}

/**********************************************************************//**
  Finds the city map coordinate for a given map position and a
  city. Returns whether the map position is inside of the city map.
**************************************************************************/
bool city_base_to_city_map(int *city_map_x, int *city_map_y,
                           const struct city *const pcity,
                           const struct tile *map_tile)
{
  return city_tile_to_city_map(city_map_x, city_map_y,
                               city_map_radius_sq_get(pcity), pcity->tile,
                               map_tile);
}

/**********************************************************************//**
  Returns TRUE iff pcity's city map includes the specified tile.
**************************************************************************/
bool city_map_includes_tile(const struct city *const pcity,
                            const struct tile *map_tile)
{
  int tmp_x, tmp_y;

  return city_base_to_city_map(&tmp_x, &tmp_y, pcity, map_tile);
}

/**********************************************************************//**
  Finds the map position for a given city map coordinate of a certain
  city. Returns true if the map position found is real.
**************************************************************************/
struct tile *city_map_to_tile(const struct civ_map *nmap,
                              const struct tile *city_center,
                              int city_radius_sq, int city_map_x,
                              int city_map_y)
{
  int tile_x, tile_y;

  fc_assert_ret_val(is_valid_city_coords(city_radius_sq, city_map_x,
                                         city_map_y), NULL);

  index_to_map_pos(&tile_x, &tile_y, tile_index(city_center));
  tile_x += CITY_ABS2REL(city_map_x);
  tile_y += CITY_ABS2REL(city_map_y);

  return map_pos_to_tile(nmap, tile_x, tile_y);
}

/**********************************************************************//**
  Compare two integer values, as required by qsort.
**************************************************************************/
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

/**********************************************************************//**
  Compare two iter_index values from the city_map_index.

  This function will be passed to qsort(). It should never return zero,
  or the sort order will be left up to qsort and will be undefined. This
  would mean that server execution would not be reproducible.
**************************************************************************/
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
  fc_assert(0 != value);
  return value;
}

/**********************************************************************//**
  Return one line (y coordinate) of a city map. *city_map_data is a pointer
  to an array containing the data which should be printed. Its size is
  defined by city_map_tiles(city_radius_sq).
**************************************************************************/
#define CITYLOG_MAX_VAL 9999 /* maximal value displayed in the citylog */
static char *citylog_map_line(int y, int city_radius_sq, int *city_map_data)
{
  int x, mindex;
  static char citylog[128], tmp[8];

  fc_assert_ret_val(city_map_data != NULL, NULL);

  /* print y coordinates (absolute) */
  fc_snprintf(citylog, sizeof(citylog), "%2d ", y);

  /* print values */
  for (x = 0; x < CITY_MAP_MAX_SIZE; x++) {
    if (is_valid_city_coords(city_radius_sq, x, y)) {
      mindex = city_tile_xy_to_index(x, y, city_radius_sq);
      /* show values between -10000 and +10000 */
      if (city_map_data[mindex] >= -CITYLOG_MAX_VAL
          && city_map_data[mindex] <= CITYLOG_MAX_VAL) {
        fc_snprintf(tmp, sizeof(tmp), "%5d", city_map_data[mindex]);
        sz_strlcat(citylog, tmp);
      } else {
        fc_snprintf(tmp, sizeof(tmp), " ####");
        sz_strlcat(citylog, tmp);
      }
    } else {
      fc_snprintf(tmp, sizeof(tmp), "     ");
      sz_strlcat(citylog, tmp);
    }
  }

  /* print y coordinates (relative) */
  fc_snprintf(tmp, sizeof(tmp), " %+4d", CITY_ABS2REL(y));
  sz_strlcat(citylog, tmp);

  return citylog;
}
#undef CITYLOG_MAX_VAL

/**********************************************************************//**
  Display 'map_data' on a city map with the given radius 'radius_sq' for the
  requested log level. The size of 'map_data' is defined by
  city_map_tiles(radius_sq).
**************************************************************************/
void citylog_map_data(enum log_level level, int radius_sq, int *map_data)
{
  int x, y;
  char line[128], tmp[8];

  if (!log_do_output_for_level(level)) {
    return;
  }

  log_base(level, "(max squared city radius = %d)", CITY_MAP_MAX_RADIUS_SQ);

  /* print x coordinates (absolute) */
  fc_snprintf(line, sizeof(line), "   ");
  for (x = 0; x < CITY_MAP_MAX_SIZE; x++) {
    fc_snprintf(tmp, sizeof(tmp), "%+5d", x);
    sz_strlcat(line, tmp);
  }
  log_base(level, "%s", line);

  for (y = 0; y < CITY_MAP_MAX_SIZE; y++) {
    log_base(level, "%s", citylog_map_line(y, radius_sq, map_data));
  }

  /* print x coordinates (relative) */
  fc_snprintf(line, sizeof(line), "   ");
  for (x = 0; x < CITY_MAP_MAX_SIZE; x++) {
    fc_snprintf(tmp, sizeof(tmp), "%+5d", CITY_ABS2REL(x));
    sz_strlcat(line, tmp);
  }
  log_base(level, "%s", line);
}

/**********************************************************************//**
  Display the location of the workers within the city map of pcity.
**************************************************************************/
void citylog_map_workers(enum log_level level, struct city *pcity)
{
  int *city_map_data = NULL;
  const struct civ_map *nmap = &(wld.map);

  fc_assert_ret(pcity != NULL);

  if (!log_do_output_for_level(level)) {
    return;
  }

  city_map_data = fc_calloc(city_map_tiles(city_map_radius_sq_get(pcity)),
                            sizeof(*city_map_data));

  city_map_iterate(city_map_radius_sq_get(pcity), cindex, x, y) {
    struct tile *ptile = city_map_to_tile(nmap,
                                          city_tile(pcity),
                                          city_map_radius_sq_get(pcity),
                                          x, y);
    city_map_data[cindex] = (ptile && tile_worked(ptile) == pcity)
                            ? (is_free_worked_index(cindex) ? 2 : 1) : 0;
  } city_map_iterate_end;

  log_base(level, "[%s (%d)] workers map:", city_name_get(pcity), pcity->id);
  citylog_map_data(level, city_map_radius_sq_get(pcity), city_map_data);
  FC_FREE(city_map_data);
}

#ifdef FREECIV_DEBUG
/**********************************************************************//**
  Log the index of all tiles of the city map.
**************************************************************************/
static void citylog_map_index(enum log_level level)
{
  int *city_map_data = NULL;

  if (!log_do_output_for_level(level)) {
    return;
  }

  city_map_data = fc_calloc(city_map_tiles(CITY_MAP_MAX_RADIUS_SQ),
                            sizeof(*city_map_data));

  city_map_iterate(CITY_MAP_MAX_RADIUS_SQ, cindex, x, y) {
    city_map_data[cindex] = cindex;
  } city_map_iterate_end;

  log_debug("city map index:");
  citylog_map_data(level, CITY_MAP_MAX_RADIUS_SQ, city_map_data);
  FC_FREE(city_map_data);
}

/**********************************************************************//**
  Log the radius of all tiles of the city map.
**************************************************************************/
static void citylog_map_radius_sq(enum log_level level)
{
  int *city_map_data = NULL;

  if (!log_do_output_for_level(level)) {
    return;
  }

  city_map_data = fc_calloc(city_map_tiles(CITY_MAP_MAX_RADIUS_SQ),
                            sizeof(*city_map_data));

  city_map_iterate(CITY_MAP_MAX_RADIUS_SQ, cindex, x, y) {
    city_map_data[cindex] = map_vector_to_sq_distance(CITY_ABS2REL(x),
                                                      CITY_ABS2REL(y));
  } city_map_iterate_end;

  log_debug("city map squared radius:");
  citylog_map_data(level, CITY_MAP_MAX_RADIUS_SQ, city_map_data);
  FC_FREE(city_map_data);
}
#endif /* FREECIV_DEBUG */

/**********************************************************************//**
  Fill the arrays city_map_index, city_map_xy and city_map_numtiles. This
  may depend on topology and ruleset settings.
**************************************************************************/
void generate_city_map_indices(void)
{
  int i, dx, dy, city_x, city_y, dist, city_count_tiles = 0;
  struct iter_index city_map_index_tmp[CITY_MAP_MAX_SIZE
                                       * CITY_MAP_MAX_SIZE];

  /* initialise map information for each city radii */
  for (i = 0; i <= CITY_MAP_MAX_RADIUS_SQ; i++) {
     city_map_numtiles[i] = 0; /* will be set below */
  }

  /* We don't use city-map iterators in this function because they may
   * rely on the indices that have not yet been generated. Furthermore,
   * we don't know the number of tiles within the city radius, so we need
   * an temporary city_map_index array. Its content will be copied into
   * the real array below. */
  for (dx = -CITY_MAP_MAX_RADIUS; dx <= CITY_MAP_MAX_RADIUS; dx++) {
    for (dy = -CITY_MAP_MAX_RADIUS; dy <= CITY_MAP_MAX_RADIUS; dy++) {
      dist = map_vector_to_sq_distance(dx, dy);

      if (dist <= CITY_MAP_MAX_RADIUS_SQ) {
        city_map_index_tmp[city_count_tiles].dx = dx;
        city_map_index_tmp[city_count_tiles].dy = dy;
        city_map_index_tmp[city_count_tiles].dist = dist;

        for (i = CITY_MAP_MAX_RADIUS_SQ; i >= 0; i--) {
          if (dist <= i) {
            /* increase number of tiles within this squared city radius */
            city_map_numtiles[i]++;
          }
        }

        city_count_tiles++;
      }

      /* Initialise city_map_xy. -1 defines a invalid city map positions. */
      city_map_xy[CITY_REL2ABS(dx)][CITY_REL2ABS(dy)] = -1;
    }
  }

  fc_assert(NULL == city_map_index);
  city_map_index = fc_malloc(city_count_tiles * sizeof(*city_map_index));

  /* copy the index numbers from city_map_index_tmp into city_map_index */
  for (i = 0; i < city_count_tiles; i++) {
    city_map_index[i] = city_map_index_tmp[i];
  }

  qsort(city_map_index, city_count_tiles, sizeof(*city_map_index),
        compare_iter_index);

  /* set the static variable city_map_xy */
  for (i = 0; i < city_count_tiles; i++) {
    city_x = CITY_REL2ABS(city_map_index[i].dx);
    city_y = CITY_REL2ABS(city_map_index[i].dy);
    city_map_xy[city_x][city_y] = i;
  }

#ifdef FREECIV_DEBUG
  citylog_map_radius_sq(LOG_DEBUG);
  citylog_map_index(LOG_DEBUG);

  for (i = CITY_MAP_MIN_RADIUS_SQ; i <= CITY_MAP_MAX_RADIUS_SQ; i++) {
    log_debug("radius_sq = %2d, tiles = %2d", i, city_map_tiles(i));
  }

  for (i = 0; i < city_count_tiles; i++) {
    city_x = CITY_REL2ABS(city_map_index[i].dx);
    city_y = CITY_REL2ABS(city_map_index[i].dy);
    log_debug("[%2d]: (dx,dy) = (%+2d,%+2d), (x,y) = (%2d,%2d), "
              "dist = %2d, check = %2d", i,
              city_map_index[i].dx, city_map_index[i].dy, city_x, city_y,
              city_map_index[i].dist, city_map_xy[city_x][city_y]);
  }
#endif /* FREECIV_DEBUG */

  cm_init_citymap();
}

/**********************************************************************//**
  Free memory allocated by generate_citymap_index
**************************************************************************/
void free_city_map_index(void)
{
  FC_FREE(city_map_index);
}

/**********************************************************************//**
  Return an id string for the output type.  This string can be used
  internally by rulesets and tilesets and should not be changed or
  translated.
**************************************************************************/
const char *get_output_identifier(Output_type_id output)
{
  fc_assert_ret_val(output >= 0 && output < O_LAST, NULL);
  return output_types[output].id;
}

/**********************************************************************//**
  Return a translated name for the output type.  This name should only be
  used for user display.
**************************************************************************/
const char *get_output_name(Output_type_id output)
{
  fc_assert_ret_val(output >= 0 && output < O_LAST, NULL);
  return _(output_types[output].name);
}

/**********************************************************************//**
  Return the output type for this index.
**************************************************************************/
struct output_type *get_output_type(Output_type_id output)
{
  fc_assert_ret_val(output >= 0 && output < O_LAST, NULL);
  return &output_types[output];
}

/**********************************************************************//**
  Find the output type for this output identifier.
**************************************************************************/
Output_type_id output_type_by_identifier(const char *id)
{
  Output_type_id o;

  for (o = 0; o < O_LAST; o++) {
    if (fc_strcasecmp(output_types[o].id, id) == 0) {
      return o;
    }
  }

  return O_LAST;
}

/**********************************************************************//**
  Return the extended name of the building.
**************************************************************************/
const char *city_improvement_name_translation(const struct city *pcity,
					      const struct impr_type *pimprove)
{
  static char buffer[256];
  const char *state = NULL;

  if (is_great_wonder(pimprove)) {
    if (great_wonder_is_available(pimprove)) {
      state = Q_("?wonder:W");
    } else if (great_wonder_is_destroyed(pimprove)) {
      state = Q_("?destroyed:D");
    } else {
      state = Q_("?built:B");
    }
  }
  if (pcity) {
    struct player *pplayer = city_owner(pcity);

    if (improvement_obsolete(pplayer, pimprove, pcity)) {
      state = Q_("?obsolete:O");
    } else if (is_improvement_redundant(pcity, pimprove)) {
      state = Q_("?redundant:*");
    }
  }

  if (state) {
    fc_snprintf(buffer, sizeof(buffer), "%s(%s)",
                improvement_name_translation(pimprove), state);
    return buffer;
  } else {
    return improvement_name_translation(pimprove);
  }
}

/**********************************************************************//**
  Return the extended name of the current production.
**************************************************************************/
const char *city_production_name_translation(const struct city *pcity)
{
  static char buffer[256];

  switch (pcity->production.kind) {
  case VUT_IMPROVEMENT:
    return city_improvement_name_translation(pcity, pcity->production.value.building);
  default:
    /* fallthru */
    break;
  };
  return universal_name_translation(&pcity->production, buffer, sizeof(buffer));
}

/**********************************************************************//**
  Return TRUE when the current production is of this genus.
**************************************************************************/
bool city_production_is_genus(const struct city *pcity,
                              enum impr_genus_id genus)
{
  return VUT_IMPROVEMENT == pcity->production.kind
      && (pcity->production.value.building->genus == genus);
}

/**********************************************************************//**
  Return TRUE when the current production has this flag.
**************************************************************************/
bool city_production_has_flag(const struct city *pcity,
			      enum impr_flag_id flag)
{
  return VUT_IMPROVEMENT == pcity->production.kind
      && improvement_has_flag(pcity->production.value.building, flag);
}

/**********************************************************************//**
  Return the number of shields it takes to build current city production.
**************************************************************************/
int city_production_build_shield_cost(const struct city *pcity)
{
  return universal_build_shield_cost(pcity, &pcity->production);
}

/**********************************************************************//**
  Return TRUE if the city could use the additional build slots provided by
  the effect City_Build_Slots. Within 'num_units' the total number of units
  the city can build considering the current shield stock is returned.
**************************************************************************/
bool city_production_build_units(const struct city *pcity,
                                 bool add_production, int *num_units)
{
  const struct unit_type *utype;
  struct universal target;
  int build_slots = city_build_slots(pcity);
  int shields_left = pcity->shield_stock;
  int unit_shield_cost, i;

  fc_assert_ret_val(num_units != NULL, FALSE);
  (*num_units) = 0;

  if (pcity->production.kind != VUT_UTYPE) {
    /* not a unit as the current production */
    return FALSE;
  }

  utype = pcity->production.value.utype;
  if (utype_pop_value(utype, pcity) != 0 || utype_has_flag(utype, UTYF_UNIQUE)) {
    /* unit with population cost or unique unit means that only one unit can
     * be build */
    (*num_units)++;
    return FALSE;
  }

  if (add_production) {
    shields_left += pcity->prod[O_SHIELD];
  }

  unit_shield_cost = utype_build_shield_cost(pcity, NULL, utype);

  for (i = 0; i < build_slots; i++) {
    if (shields_left < unit_shield_cost) {
      /* not enough shields */
      break;
    }

    (*num_units)++;
    shields_left -= unit_shield_cost;

    if (worklist_length(&pcity->worklist) > i) {
      (void) worklist_peek_ith(&pcity->worklist, &target, i);
      if (target.kind != VUT_UTYPE
          || utype_index(target.value.utype) != utype_index(utype)) {
        /* stop if there is a build target in the worklist not equal to the
         * unit we build */
        break;
      }
    }
  }

  return TRUE;
}

/************************************************************************//**
  How many veteran levels will created unit of this type get?
****************************************************************************/
int city_production_unit_veteran_level(struct city *pcity,
                                       const struct unit_type *punittype)
{
  int levels = get_unittype_bonus(city_owner(pcity), pcity->tile, punittype,
                                  NULL, EFT_VETERAN_BUILD);
  int max_levels = utype_veteran_levels(punittype) - 1;

  levels = CLIP(0, levels, max_levels);

  return levels;
}

/**********************************************************************//**
 Calculates the turns which are needed to build the requested
 production in the city.  GUI Independent.
**************************************************************************/
int city_production_turns_to_build(const struct city *pcity,
                                   bool include_shield_stock)
{
  return city_turns_to_build(pcity, &pcity->production, include_shield_stock);
}

/**********************************************************************//**
  Return whether given city can build given building, ignoring whether
  it is obsolete.
**************************************************************************/
bool can_city_build_improvement_direct(const struct city *pcity,
                                       const struct impr_type *pimprove)
{
  if (!can_player_build_improvement_direct(city_owner(pcity), pimprove)) {
    return FALSE;
  }

  if (city_has_building(pcity, pimprove)) {
    return FALSE;
  }

  return are_reqs_active(&(const struct req_context) {
                           .player = city_owner(pcity),
                           .city = pcity,
                           .tile = pcity->tile,
                         },
                         NULL,
                         &(pimprove->reqs), RPT_CERTAIN);
}

/**********************************************************************//**
  Return whether given city can build given building; returns FALSE if
  the building is obsolete.
**************************************************************************/
bool can_city_build_improvement_now(const struct city *pcity,
                                    const struct impr_type *pimprove)
{
  if (!can_city_build_improvement_direct(pcity, pimprove)) {
    return FALSE;
  }
  if (improvement_obsolete(city_owner(pcity), pimprove, pcity)) {
    return FALSE;
  }

  return TRUE;
}

/**********************************************************************//**
  Return whether player can eventually build given building in the city;
  returns FALSE if improvement can never possibly be built in this city.
**************************************************************************/
bool can_city_build_improvement_later(const struct city *pcity,
                                      const struct impr_type *pimprove)
{
  const struct req_context city_ctxt = {
    .player = city_owner(pcity),
    .city = pcity,
    .tile = city_tile(pcity),
  };

  /* Can the _player_ ever build this improvement? */
  /* NOTE: It checks for obsoletion player-level. What aboult checking
   * for it city-level? That may unlist from a worklist some things
   * we'll be able to switch to after e.g. selling something else */
  if (!can_player_build_improvement_later(city_owner(pcity), pimprove)) {
    return FALSE;
  }

  /* Check for requirements that aren't met and that are unchanging (so
   * they can never be met). */
  requirement_vector_iterate(&pimprove->reqs, preq) {
    if (is_req_preventing(&city_ctxt, NULL, preq, RPT_POSSIBLE)) {
      return FALSE;
    }
  } requirement_vector_iterate_end;

  return TRUE;
}

/**********************************************************************//**
  Return whether given city can build given unit, ignoring whether unit
  is obsolete.
**************************************************************************/
bool can_city_build_unit_direct(const struct civ_map *nmap,
                                const struct city *pcity,
                                const struct unit_type *punittype)
{
  if (!can_player_build_unit_direct(city_owner(pcity), punittype, FALSE)) {
    return FALSE;
  }

  /* Check unit build requirements.
   * Above player level check already checked anything with range >= REQ_RANGE_PLAYER.
   * Don't recheck those. Not only for optimization, but also not to override the
   * special handling of tech requirements for barbarians */
  if (!are_reqs_active_ranges(0, /* The lowest range; REQ_RANGE_LOCAL */
                              REQ_RANGE_PLAYER - 1,
                              &(const struct req_context) {
                                .player = city_owner(pcity),
                                .city = pcity,
                                .tile = city_tile(pcity),
                                .unittype = punittype,
                              },
                              NULL,
                              &punittype->build_reqs, RPT_CERTAIN)) {
    return FALSE;
  }

  /* You can't build naval units inland. */
  if (!uclass_has_flag(utype_class(punittype), UCF_BUILD_ANYWHERE)
      && !is_native_near_tile(nmap, utype_class(punittype),
                              pcity->tile)) {
    return FALSE;
  }

  if (punittype->city_slots > 0
      && city_unit_slots_available(pcity) < punittype->city_slots) {
    return FALSE;
  }

  return TRUE;
}

/**********************************************************************//**
  Return whether given city can build given unit; returns FALSE if unit is
  obsolete.
**************************************************************************/
bool can_city_build_unit_now(const struct civ_map *nmap,
                             const struct city *pcity,
                             const struct unit_type *punittype)
{
  if (!can_city_build_unit_direct(nmap, pcity, punittype)) {
    return FALSE;
  }

  while ((punittype = punittype->obsoleted_by) != U_NOT_OBSOLETED) {
    /* TODO: Decide if fulfilled impr_req is needed to make unit obsolete,
     *       i.e., should the 'consider_reg_impr_req' be TRUE or FALSE. */
    if (can_player_build_unit_direct(city_owner(pcity), punittype, TRUE)) {
      return FALSE;
    }
  }

  return TRUE;
}

/**********************************************************************//**
  Returns whether player can eventually build given unit in the city;
  returns FALSE if unit can never possibly be built in this city.
**************************************************************************/
bool can_city_build_unit_later(const struct civ_map *nmap,
                               const struct city *pcity,
			       const struct unit_type *punittype)
{
  /* Can the _player_ ever build this unit? */
  if (!can_player_build_unit_later(city_owner(pcity), punittype)) {
    return FALSE;
  }

  /* Some units can be built only in certain cities -- for instance,
     ships may be built only in cities adjacent to ocean. */
  if (!uclass_has_flag(utype_class(punittype), UCF_BUILD_ANYWHERE)
      && !is_native_near_tile(nmap, utype_class(punittype),
                              pcity->tile)) {
    return FALSE;
  }

  return TRUE;
}

/**********************************************************************//**
  Returns whether city can immediately build given target,
  unit or improvement. This considers obsolete targets still buildable.
**************************************************************************/
bool can_city_build_direct(const struct civ_map *nmap,
                           const struct city *pcity,
                           const struct universal *target)
{
  switch (target->kind) {
  case VUT_UTYPE:
    return can_city_build_unit_direct(nmap, pcity, target->value.utype);
  case VUT_IMPROVEMENT:
    return can_city_build_improvement_direct(pcity, target->value.building);
  default:
    break;
  };
  return FALSE;
}

/**********************************************************************//**
  Returns whether city can immediately build given target,
  unit or improvement. This considers obsolete targets no longer buildable.
**************************************************************************/
bool can_city_build_now(const struct civ_map *nmap,
                        const struct city *pcity,
                        const struct universal *target)
{
  switch (target->kind) {
  case VUT_UTYPE:
    return can_city_build_unit_now(nmap, pcity, target->value.utype);
  case VUT_IMPROVEMENT:
    return can_city_build_improvement_now(pcity, target->value.building);
  default:
    break;
  };
  return FALSE;
}

/**********************************************************************//**
  Returns whether city can ever build given target, unit or improvement.
**************************************************************************/
bool can_city_build_later(const struct civ_map *nmap,
                          const struct city *pcity,
                          const struct universal *target)
{
  switch (target->kind) {
  case VUT_UTYPE:
    return can_city_build_unit_later(nmap, pcity, target->value.utype);
  case VUT_IMPROVEMENT:
    return can_city_build_improvement_later(pcity, target->value.building);
  default:
    break;
  };
  return FALSE;
}

/**********************************************************************//**
  Return number of free unit slots in a city.
**************************************************************************/
int city_unit_slots_available(const struct city *pcity)
{
  int max = get_city_bonus(pcity, EFT_UNIT_SLOTS);
  int current;

  current = 0;
  unit_list_iterate(pcity->units_supported, punit) {
    current += unit_type_get(punit)->city_slots;
  } unit_list_iterate_end;

  return max - current;
}

/**********************************************************************//**
  Returns TRUE iff the given city can use this kind of specialist.
**************************************************************************/
bool city_can_use_specialist(const struct city *pcity,
                             Specialist_type_id type)
{
  return are_reqs_active(&(const struct req_context) {
                           .player = city_owner(pcity),
                           .city = pcity,
                         },
                         NULL,
                         &specialist_by_number(type)->reqs, RPT_POSSIBLE);
}

/**********************************************************************//**
  Returns TRUE iff the given city can change what it is building
**************************************************************************/
bool city_can_change_build(const struct city *pcity)
{
  return !pcity->did_buy || pcity->shield_stock <= 0;
}

/**********************************************************************//**
  Always tile_set_owner(ptile, pplayer) sometime before this!
**************************************************************************/
void city_choose_build_default(const struct civ_map *nmap, struct city *pcity)
{
  if (NULL == city_tile(pcity)) {
    /* When a "dummy" city is created with no tile, then choosing a build
     * target could fail. This currently might happen during map editing.
     * FIXME: assumes the first unit is always "valid", so check for
     * obsolete units elsewhere. */
    pcity->production.kind = VUT_UTYPE;
    pcity->production.value.utype = utype_by_number(0);
  } else {
    struct unit_type *u = best_role_unit(pcity, L_FIRSTBUILD);

    if (u) {
      pcity->production.kind = VUT_UTYPE;
      pcity->production.value.utype = u;
    } else {
      bool found = FALSE;

      /* Just pick the first available item. */
      improvement_iterate(pimprove) {
        if (can_city_build_improvement_direct(pcity, pimprove)) {
          found = TRUE;
          pcity->production.kind = VUT_IMPROVEMENT;
          pcity->production.value.building = pimprove;
          break;
        }
      } improvement_iterate_end;

      if (!found) {
        unit_type_iterate(punittype) {
          if (can_city_build_unit_direct(nmap, pcity, punittype)) {
#ifndef FREECIV_NDEBUG
            /* Later than this, 'found' is only needed in an fc_assert() */
            found = TRUE;
#endif /* FREECIV_NDEBUG */
            pcity->production.kind = VUT_UTYPE;
            pcity->production.value.utype = punittype;
          }
        } unit_type_iterate_end;
      }

      fc_assert_msg(found, "No production found for city %s!",
                    city_name_get(pcity));
    }
  }
}

/**********************************************************************//**
  Return the name of the city.
**************************************************************************/
const char *city_name_get(const struct city *pcity)
{
  return (pcity->name != NULL) ? pcity->name : "City missing a name";
}

/**********************************************************************//**
  Set a new name for the city.
**************************************************************************/
void city_name_set(struct city *pcity, const char *new_name)
{
  if (pcity->name != NULL) {
    free(pcity->name);
  }

  if (strlen(new_name) < MAX_LEN_CITYNAME) {
    pcity->name = fc_strdup(new_name);
  } else {
    log_warn(_("City name \"%s\" too long"), new_name);
    pcity->name = fc_malloc(MAX_LEN_CITYNAME);
    sz_strlcpy(pcity->name, new_name);
  }
}

/**********************************************************************//**
  Add a (positive or negative) value to the city size. As citizens is an
  unsigned value use int for the parameter 'add'.
**************************************************************************/
void city_size_add(struct city *pcity, int add)
{
  citizens size = city_size_get(pcity);

  fc_assert_ret(pcity != NULL);
  fc_assert_ret(MAX_CITY_SIZE - size >= add);

  /* Client sets size to zero to start stacking citizens in */
  fc_assert_ret(size > -add || (!is_server() && size == add));

  city_size_set(pcity, size + add);
}

/**********************************************************************//**
  Set the city size.
**************************************************************************/
void city_size_set(struct city *pcity, citizens size)
{
  fc_assert_ret(pcity != NULL);

  /* Set city size. */
  pcity->size = size;
}

/**********************************************************************//**
 Returns how many thousand citizen live in this city.
**************************************************************************/
int city_population(const struct city *pcity)
{
  /*  Sum_{i=1}^{n} i  ==  n*(n+1)/2  */
  return city_size_get(pcity) * (city_size_get(pcity) + 1) * 5;
}

/**********************************************************************//**
  Returns the total amount of gold needed to pay for all buildings in the
  city.
**************************************************************************/
int city_total_impr_gold_upkeep(const struct city *pcity)
{
  int gold_needed = 0;

  if (pcity == NULL) {
    return 0;
  }

  city_built_iterate(pcity, pimprove) {
    gold_needed += city_improvement_upkeep(pcity, pimprove);
  } city_built_iterate_end;

  gold_needed -= get_city_bonus(pcity, EFT_IMPR_UPKEEP_REDUCTION);

  return MAX(gold_needed, 0);
}

/**********************************************************************//**
  Get the total amount of gold needed to pay upkeep costs for all supported
  units of the city. Takes into account EFT_UNIT_UPKEEP_FREE_PER_CITY.
**************************************************************************/
int city_total_unit_gold_upkeep(const struct city *pcity)
{
  int gold_needed = 0;

  if (pcity == NULL || pcity->units_supported == NULL) {
    return 0;
  }

  unit_list_iterate(pcity->units_supported, punit) {
    gold_needed += punit->upkeep[O_GOLD];
  } unit_list_iterate_end;

  return gold_needed;
}

/**********************************************************************//**
  Return TRUE iff the city has this building in it.
**************************************************************************/
bool city_has_building(const struct city *pcity,
                       const struct impr_type *pimprove)
{
  if (NULL == pimprove) {
    /* Callers should ensure that any external data is tested with
     * valid_improvement_by_number() */
    return FALSE;
  }
  return (pcity->built[improvement_index(pimprove)].turn > I_NEVER);
}

/**********************************************************************//**
  Return the upkeep (gold) needed each turn to upkeep the given improvement
  in the given city.
**************************************************************************/
int city_improvement_upkeep(const struct city *pcity,
                            const struct impr_type *b)
{
  int upkeep;

  if (NULL == b) {
    return 0;
  }
  if (!is_building_sellable(b)) {
    return 0;
  }

  upkeep = b->upkeep;
  if (upkeep <= get_building_bonus(pcity, b, EFT_UPKEEP_FREE)) {
    return 0;
  }

  return upkeep;
}

/**********************************************************************//**
  Calculate the output for the tile.
  pcity may be NULL.
  is_celebrating may be speculative.
  otype is the output type (generally O_FOOD, O_TRADE, or O_SHIELD).

  This can be used to calculate the benefits celebration would give.
**************************************************************************/
int city_tile_output(const struct city *pcity, const struct tile *ptile,
                     bool is_celebrating, Output_type_id otype)
{
  int prod;
  const struct req_context city_ctxt = {
    .player = pcity ? city_owner(pcity) : NULL,
    .city = pcity,
    .tile = ptile,
  };
  struct terrain *pterrain = tile_terrain(ptile);
  const struct output_type *output = &output_types[otype];

  fc_assert_ret_val(otype >= 0 && otype < O_LAST, 0);

  if (T_UNKNOWN == pterrain) {
    /* Special case for the client.  The server doesn't allow unknown tiles
     * to be worked but we don't necessarily know what player is involved. */
    return 0;
  }

  prod = pterrain->output[otype];
  if (tile_resource_is_valid(ptile)) {
    prod += tile_resource(ptile)->data.resource->output[otype];
  }

  switch (otype) {
  case O_SHIELD:
    if (pterrain->mining_shield_incr != 0) {
      prod += pterrain->mining_shield_incr
        * get_target_bonus_effects(NULL, &city_ctxt, NULL, EFT_MINING_PCT)
        / 100;
    }
    break;
  case O_FOOD:
    if (pterrain->irrigation_food_incr != 0) {
      prod += pterrain->irrigation_food_incr
        * get_target_bonus_effects(NULL, &city_ctxt, NULL,
                                   EFT_IRRIGATION_PCT) / 100;
    }
    break;
  case O_TRADE:
  case O_GOLD:
  case O_SCIENCE:
  case O_LUXURY:
  case O_LAST:
    break;
  }

  prod += tile_roads_output_incr(ptile, otype);
  prod += (prod * tile_roads_output_bonus(ptile, otype) / 100);

  prod += get_tile_output_bonus(pcity, ptile, output, EFT_OUTPUT_ADD_TILE);
  if (prod > 0) {
    int penalty_limit = get_tile_output_bonus(pcity, ptile, output,
                                              EFT_OUTPUT_PENALTY_TILE);

    if (prod >= game.info.granularity) {
      prod += get_tile_output_bonus(pcity, ptile, output,
                                    EFT_OUTPUT_INC_TILE);

      if (is_celebrating) {
        prod += get_tile_output_bonus(pcity, ptile, output,
                                      EFT_OUTPUT_INC_TILE_CELEBRATE);
      }
    }

    prod += (prod
             * get_tile_output_bonus(pcity, ptile, output,
                                     EFT_OUTPUT_PER_TILE))
            / 100;
    if (penalty_limit > 0 && prod > penalty_limit) {
      if (prod <= game.info.granularity) {
        prod = 0;
      } else {
        prod -= game.info.granularity;
      }
    }
  }

  prod -= (prod
           * get_tile_output_bonus(pcity, ptile, output,
                                   EFT_OUTPUT_TILE_PUNISH_PCT))
           / 100;

  if (NULL != pcity && is_city_center(pcity, ptile)) {
    prod = MAX(prod, game.info.min_city_center_output[otype]);
  }

  return prod;
}

/**********************************************************************//**
  Calculate the production output the given tile is capable of producing
  for the city. The output type is given by 'otype' (generally O_FOOD,
  O_SHIELD, or O_TRADE).

  NOTE: As of now, return value does not represent output on end of turn
  if city stops celebrating, because the server side tile output cache
  uses base_city_celebrating() instead of city_celebrating() on
  city_tile_cache_update().
**************************************************************************/
int city_tile_output_now(const struct city *pcity, const struct tile *ptile,
                         Output_type_id otype)
{
  return city_tile_output(pcity, ptile, city_celebrating(pcity), otype);
}

/**********************************************************************//**
  Returns TRUE when a tile is available to be worked, or the city itself is
  currently working the tile (and can continue).

  The parameter 'restriction', which is usually client_player(), allow a
  player to handle with its real knownledge to guess it the work of this
  tile is possible.

  This function shouldn't be called directly, but with city_can_work_tile()
  (common/city.[ch]) or client_city_can_work_tile() (client/climap.[ch]).
**************************************************************************/
bool base_city_can_work_tile(const struct player *restriction,
                             const struct city *pcity,
                             const struct tile *ptile)
{
  struct player *powner = city_owner(pcity);
  int city_map_x, city_map_y;
  struct player *towner;

  if (NULL == ptile) {
    return FALSE;
  }

  if (!city_base_to_city_map(&city_map_x, &city_map_y, pcity, ptile)) {
    return FALSE;
  }

  if (NULL != restriction
      && TILE_UNKNOWN == tile_get_known(ptile, restriction)) {
    return FALSE;
  }

  towner = tile_owner(ptile);
  if (NULL != towner && towner != powner
      && !gives_shared_tiles(towner, powner)) {
    return FALSE;
  }
  /* TODO: civ3-like option for borders */

  if (NULL != tile_worked(ptile) && tile_worked(ptile) != pcity) {
    return FALSE;
  }

  if (powner == restriction
      && TILE_KNOWN_SEEN != tile_get_known(ptile, powner)) {
    return FALSE;
  }

  if (!is_free_worked(pcity, ptile)
      && NULL != unit_occupies_tile(ptile, powner)) {
    return FALSE;
  }

  if (get_city_tile_output_bonus(pcity, ptile, NULL, EFT_TILE_WORKABLE) <= 0) {
    return FALSE;
  }

  return TRUE;
}

/**********************************************************************//**
  Returns TRUE when a tile is available to be worked, or the city itself is
  currently working the tile (and can continue).

  See also client_city_can_work_tile() (client/climap.[ch]).
**************************************************************************/
bool city_can_work_tile(const struct city *pcity, const struct tile *ptile)
{
  return base_city_can_work_tile(city_owner(pcity), pcity, ptile);
}

/**********************************************************************//**
  Returns TRUE iff it is illegal to found a city on the specified tile
  because of citymindist.
**************************************************************************/
bool citymindist_prevents_city_on_tile(const struct civ_map *nmap,
                                       const struct tile *ptile)
{
  /* citymindist minimum is 1, meaning adjacent is okay */
  int citymindist = game.info.citymindist;

  square_iterate(nmap, ptile, citymindist - 1, ptile1) {
    if (tile_city(ptile1)) {
      return TRUE;
    }
  } square_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  Returns TRUE if the given unit can build a city at the given map
  coordinates.

  punit is the founding unit. It may be NULL if a city is built out of the
  blue (e.g., through editing).
**************************************************************************/
bool city_can_be_built_here(const struct civ_map *nmap,
                            const struct tile *ptile,
                            const struct unit *punit,
                            bool hut_test)
{
  if (!city_can_be_built_tile_only(nmap, ptile)) {
    return FALSE;
  }

  if (punit == NULL) {
    /* The remaining checks tests if punit can found a city here */
    return TRUE;
  }

  if (hut_test) {
    struct player *towner;

    /* Huts can be found only from native tiles, owned by the unit owner.
     * Unlike actual city building, this behavior is not affected
     * by the ruleset. */
    if (!can_unit_exist_at_tile(nmap, punit, ptile)) {
      return FALSE;
    }

    towner = tile_owner(ptile);

    if (towner == NULL || towner == unit_owner(punit)) {
      return TRUE;
    }

    return FALSE;
  }

  action_by_result_iterate(paction, ACTRES_FOUND_CITY) {
    if (!utype_can_do_action(unit_type_get(punit), action_id(paction))) {
      /* This action can't be done by this unit type at all. */
      continue;
    }

    /* Non native tile detection */
    if (!can_unit_exist_at_tile(nmap, punit, ptile)
        /* The ruleset may allow founding cities on non native terrain. */
        && !utype_can_do_act_when_ustate(unit_type_get(punit), paction->id,
                                         USP_LIVABLE_TILE, FALSE)) {
      /* Many rulesets allow land units to build land cities and sea units
       * to build ocean cities. Air units can build cities anywhere. */
      continue;
    }

    /* Foreign tile detection. */
    if (tile_owner(ptile) && tile_owner(ptile) != unit_owner(punit)
        /* The ruleset may allow founding cities on foreign terrain. */
        && !can_utype_do_act_if_tgt_diplrel(unit_type_get(punit),
                                            paction->id,
                                            DRO_FOREIGN, TRUE)) {
      /* Cannot steal borders by settling. This has to be settled by
       * force of arms. */
      continue;
    }

    return TRUE;
  } action_by_result_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  Returns TRUE if the given unit can build a city at the given map
  coordinates.

  It may still be illegal for any unit to build a city at the specified
  tile.
**************************************************************************/
bool city_can_be_built_tile_only(const struct civ_map *nmap,
                                 const struct tile *ptile)
{
  if (terrain_has_flag(tile_terrain(ptile), TER_NO_CITIES)) {
    /* No cities on this terrain. */
    return FALSE;
  }

  if (citymindist_prevents_city_on_tile(nmap, ptile)) {
    return FALSE;
  }

  return TRUE;
}

/**********************************************************************//**
  Return TRUE iff this city is its nation's capital.  The capital city is
  special-cased in a number of ways.
**************************************************************************/
bool is_capital(const struct city *pcity)
{
  return pcity->capital != CAPITAL_NOT;
}

/**********************************************************************//**
  Return TRUE iff this city is governmental center.
**************************************************************************/
bool is_gov_center(const struct city *pcity)
{
  return (get_city_bonus(pcity, EFT_GOV_CENTER) > 0);
}

/**********************************************************************//**
 This can be City Walls, Coastal defense... depending on attacker type.
 If attacker type is not given, just any defense effect will do.
**************************************************************************/
bool city_got_defense_effect(const struct city *pcity,
                             const struct unit_type *attacker)
{
  if (!attacker) {
    /* Any defense building will do */
    return get_city_bonus(pcity, EFT_DEFEND_BONUS) > 0;
  }

  return get_unittype_bonus(city_owner(pcity), pcity->tile, attacker,
                            NULL, EFT_DEFEND_BONUS) > 0;
}

/**********************************************************************//**
  Return TRUE iff the city is happy.  A happy city will start celebrating
  soon.
  A city can only be happy if half or more of the population is happy,
  none of the population is unhappy or angry, and it has sufficient size.
**************************************************************************/
bool city_happy(const struct city *pcity)
{
  return (city_size_get(pcity) >= game.info.celebratesize
	  && pcity->feel[CITIZEN_ANGRY][FEELING_FINAL] == 0
	  && pcity->feel[CITIZEN_UNHAPPY][FEELING_FINAL] == 0
	  && pcity->feel[CITIZEN_HAPPY][FEELING_FINAL] >= (city_size_get(pcity) + 1) / 2);
}

/**********************************************************************//**
  Return TRUE iff the city is unhappy.  An unhappy city will fall
  into disorder soon.
**************************************************************************/
bool city_unhappy(const struct city *pcity)
{
  return (pcity->feel[CITIZEN_HAPPY][FEELING_FINAL]
	< pcity->feel[CITIZEN_UNHAPPY][FEELING_FINAL]
	  + 2 * pcity->feel[CITIZEN_ANGRY][FEELING_FINAL]);
}

/**********************************************************************//**
  Return TRUE if the city was celebrating at the start of the turn,
  and it still has sufficient size to be in rapture.
**************************************************************************/
bool base_city_celebrating(const struct city *pcity)
{
  return (city_size_get(pcity) >= game.info.celebratesize && pcity->was_happy);
}

/**********************************************************************//**
cities celebrate only after consecutive happy turns
**************************************************************************/
bool city_celebrating(const struct city *pcity)
{
  return base_city_celebrating(pcity) && city_happy(pcity);
}

/**********************************************************************//**
  Returns whether city is growing by rapture.
**************************************************************************/
bool city_rapture_grow(const struct city *pcity)
{
  /* .rapture is checked instead of city_celebrating() because this
     function is called after .was_happy was updated. */
  return (pcity->rapture > 0 && pcity->surplus[O_FOOD] > 0
	  && (pcity->rapture % game.info.rapturedelay) == 0
          && get_city_bonus(pcity, EFT_RAPTURE_GROW) > 0);
}

/**********************************************************************//**
  Returns TRUE iff the city is occupied.
**************************************************************************/
bool city_is_occupied(const struct city *pcity)
{
  if (is_server()) {
    /* The server sees the units inside the city. */
    return (unit_list_size(city_tile(pcity)->units) > 0);
  } else {
    /* The client gets the occupied property from the server. */
    return pcity->client.occupied;
  }
}

/**********************************************************************//**
  Find city with given id from list.
**************************************************************************/
struct city *city_list_find_number(struct city_list *This, int id)
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

/**********************************************************************//**
  Find city with given name from list.
**************************************************************************/
struct city *city_list_find_name(struct city_list *This, const char *name)
{
  city_list_iterate(This, pcity) {
    if (fc_strcasecmp(name, pcity->name) == 0) {
      return pcity;
    }
  } city_list_iterate_end;

  return NULL;
}

/**********************************************************************//**
  Comparison function for qsort for city _pointers_, sorting by city name.
  Args are really (struct city**), to sort an array of pointers.
  (Compare with old_city_name_compare() in game.c, which use city_id's)
**************************************************************************/
int city_name_compare(const void *p1, const void *p2)
{
  return fc_strcasecmp((*(const struct city **) p1)->name,
                       (*(const struct city **) p2)->name);
}

/**********************************************************************//**
  Returns the city style that has the given (translated) name.
  Returns -1 if none match.
**************************************************************************/
int city_style_by_translated_name(const char *s)
{
  int i;

  for (i = 0; i < game.control.num_city_styles; i++) {
    if (0 == strcmp(city_style_name_translation(i), s)) {
      return i;
    }
  }

  return -1;
}

/**********************************************************************//**
  Returns the city style that has the given (untranslated) rule name.
  Returns -1 if none match.
**************************************************************************/
int city_style_by_rule_name(const char *s)
{
  const char *qs = Qn_(s);
  int i;

  for (i = 0; i < game.control.num_city_styles; i++) {
    if (0 == fc_strcasecmp(city_style_rule_name(i), qs)) {
      return i;
    }
  }

  return -1;
}

/**********************************************************************//**
  Return the (translated) name of the given city style.
  You don't have to free the return pointer.
**************************************************************************/
const char *city_style_name_translation(const int style)
{
  return name_translation_get(&city_styles[style].name);
}

/**********************************************************************//**
  Return the (untranslated) rule name of the city style.
  You don't have to free the return pointer.
**************************************************************************/
const char *city_style_rule_name(const int style)
{
   return rule_name_get(&city_styles[style].name);
}

/* Cache of what city production caravan shields are allowed to help. */
static bv_imprs caravan_helped_impr;
static bv_unit_types caravan_helped_utype;

/**********************************************************************//**
  Initialize the cache of what city production can use shields from
  caravans.
**************************************************************************/
void city_production_caravan_shields_init(void)
{
  struct requirement prod_as_req;

#define log_ca_s_init log_debug

  /* Remove old data. */
  BV_CLR_ALL(caravan_helped_impr);
  BV_CLR_ALL(caravan_helped_utype);

  /* Common for all production kinds. */
  prod_as_req.range = REQ_RANGE_LOCAL;
  prod_as_req.survives = FALSE;
  prod_as_req.present = TRUE;

  /* Check improvements */
  prod_as_req.source.kind = VUT_IMPROVEMENT;

  improvement_iterate(itype) {
    /* Check this improvement. */
    prod_as_req.source.value.building = itype;

    action_enabler_list_iterate(action_enablers_for_action(
                                  ACTION_HELP_WONDER),
                                enabler) {
      if (!does_req_contradicts_reqs(&prod_as_req,
                                     &(enabler->target_reqs))
          && !req_vec_wants_type(&(enabler->target_reqs), VUT_UTYPE)
          && !req_vec_wants_type(&(enabler->target_reqs), VUT_UCLASS)
          && !req_vec_wants_type(&(enabler->target_reqs), VUT_UTFLAG)
          && !req_vec_wants_type(&(enabler->target_reqs), VUT_UCFLAG)) {
        /* This improvement kind can receive caravan shields. */

        BV_SET(caravan_helped_impr, improvement_index(itype));

        /* Move on to the next improvement */
        break;
      }
    } action_enabler_list_iterate_end;

    log_ca_s_init("Help Wonder: %s for %s",
                  (BV_ISSET(caravan_helped_impr, improvement_index(itype))
                   ? "possible" : "impossible"),
                  improvement_rule_name(itype));
  } improvement_iterate_end;

  /* Check units. */
  prod_as_req.source.kind = VUT_UTYPE;

  unit_type_iterate(putype) {
    /* Check this utype. */
    prod_as_req.source.value.utype = putype;

    action_enabler_list_iterate(action_enablers_for_action(
                                  ACTION_HELP_WONDER),
                                enabler) {
      if (!does_req_contradicts_reqs(&prod_as_req,
                                     &(enabler->target_reqs))
          && !req_vec_wants_type(&(enabler->target_reqs), VUT_IMPROVEMENT)
          && !req_vec_wants_type(&(enabler->target_reqs), VUT_IMPR_GENUS)) {
        /* This unit type kind can receive caravan shields. */

        BV_SET(caravan_helped_utype, utype_index(putype));

        /* Move on to the next unit type */
        break;
      }
    } action_enabler_list_iterate_end;

    log_ca_s_init("Help Wonder: %s for %s",
                  (BV_ISSET(caravan_helped_utype, utype_index(putype))
                   ? "possible" : "impossible"),
                  utype_rule_name(putype));
  } unit_type_iterate_end;

#undef log_ca_s_init
}

/**********************************************************************//**
  Returns TRUE iff the specified production should get shields from
  units that has done ACTION_HELP_WONDER.
**************************************************************************/
bool city_production_gets_caravan_shields(const struct universal *tgt)
{
  switch (tgt->kind) {
  case VUT_IMPROVEMENT:
    return BV_ISSET(caravan_helped_impr,
                    improvement_index(tgt->value.building));
  case VUT_UTYPE:
    return BV_ISSET(caravan_helped_utype,
                    utype_index(tgt->value.utype));
  default:
    fc_assert(FALSE);
    return FALSE;
  };
}

/**********************************************************************//**
 Compute and optionally apply the change-production penalty for the given
 production change (to target) in the given city (pcity).
 Always returns the number of shields which would be in the stock if
 the penalty had been applied.

 If we switch the "class" of the target sometime after a city has produced
 (i.e., not on the turn immediately after), then there's a shield loss.
 But only on the first switch that turn.  Also, if ever change back to
 original improvement class of this turn, restore lost production.
**************************************************************************/
int city_change_production_penalty(const struct city *pcity,
                                   const struct universal *target)
{
  int shield_stock_after_adjustment;
  enum production_class_type orig_class;
  enum production_class_type new_class;
  int unpenalized_shields = 0, penalized_shields = 0;

  switch (pcity->changed_from.kind) {
  case VUT_IMPROVEMENT:
    if (is_wonder(pcity->changed_from.value.building)) {
      orig_class = PCT_WONDER;
    } else {
      orig_class = PCT_IMPROVEMENT;
    }
    break;
  case VUT_UTYPE:
    orig_class = PCT_UNIT;
    break;
  default:
    orig_class = PCT_LAST;
    break;
  };

  switch (target->kind) {
  case VUT_IMPROVEMENT:
    if (is_wonder(target->value.building)) {
      new_class = PCT_WONDER;
    } else {
      new_class = PCT_IMPROVEMENT;
    }
    break;
  case VUT_UTYPE:
    new_class = PCT_UNIT;
    break;
  default:
    new_class = PCT_LAST;
    break;
  };

  /* Changing production is penalized under certain circumstances. */
  if (orig_class == new_class
      || orig_class == PCT_LAST) {
    /* There's never a penalty for building something of the same class. */
    unpenalized_shields = pcity->before_change_shields;
  } else if (city_built_last_turn(pcity)) {
    /* Surplus shields from the previous production won't be penalized if
     * you change production on the very next turn. But you can only use
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
  if (city_production_gets_caravan_shields(target)) {
    unpenalized_shields += pcity->caravan_shields;
  } else {
    penalized_shields += pcity->caravan_shields;
  }

  shield_stock_after_adjustment =
    unpenalized_shields + penalized_shields / 2;

  return shield_stock_after_adjustment;
}

/**********************************************************************//**
  Calculates the turns which are needed to build the requested
  improvement in the city. GUI Independent.
**************************************************************************/
int city_turns_to_build(const struct city *pcity,
                        const struct universal *target,
                        bool include_shield_stock)
{
  int city_shield_surplus = pcity->surplus[O_SHIELD];
  int city_shield_stock = include_shield_stock ?
      city_change_production_penalty(pcity, target) : 0;
  int cost = universal_build_shield_cost(pcity, target);

  if (target->kind == VUT_IMPROVEMENT
      && is_great_wonder(target->value.building)
      && !great_wonder_is_available(target->value.building)) {
    return FC_INFINITY;
  }

  if (include_shield_stock && (city_shield_stock >= cost)) {
    return 1;
  } else if (city_shield_surplus > 0) {
    return (cost - city_shield_stock - 1) / city_shield_surplus + 1;
  } else {
    return FC_INFINITY;
  }
}

/**********************************************************************//**
  Calculates the turns which are needed for the city to grow. A value
  of FC_INFINITY means the city will never grow. A value of 0 means
  city growth is blocked. A negative value of -x means the city will
  shrink in x turns. A positive value of x means the city will grow in
  x turns.
**************************************************************************/
int city_turns_to_grow(const struct city *pcity)
{
  if (pcity->surplus[O_FOOD] > 0) {
    return (city_granary_size(city_size_get(pcity)) - pcity->food_stock +
	    pcity->surplus[O_FOOD] - 1) / pcity->surplus[O_FOOD];
  } else if (pcity->surplus[O_FOOD] < 0) {
    /* Turns before famine loss */
    return -1 + (pcity->food_stock / pcity->surplus[O_FOOD]);
  } else {
    return FC_INFINITY;
  }
}

/**********************************************************************//**
  Return TRUE iff the city can grow to the given size.
**************************************************************************/
bool city_can_grow_to(const struct city *pcity, int pop_size)
{
  return (get_city_bonus(pcity, EFT_SIZE_UNLIMIT) > 0
          || pop_size <= get_city_bonus(pcity, EFT_SIZE_ADJ));
}

/**********************************************************************//**
  Return enemy city on the tile, if one exist
**************************************************************************/
struct city *tile_enemy_city(const struct tile *ptile,
                             const struct player *pplayer)
{
  struct city *pcity = tile_city(ptile);

  if (pcity != NULL && pplayers_at_war(pplayer, city_owner(pcity))) {
    return pcity;
  }

  return NULL;
}

/**********************************************************************//**
  Return friendly city on the tile, if one exist
**************************************************************************/
struct city *tile_allied_city(const struct tile *ptile,
                              const struct player *pplayer)
{
  struct city *pcity = tile_city(ptile);

  if (pcity != NULL && pplayers_allied(pplayer, city_owner(pcity))) {
    return pcity;
  }

  return NULL;
}

/**********************************************************************//**
  Return non-attack city on the tile, if one exist
**************************************************************************/
struct city *tile_non_attack_city(const struct tile *ptile,
                                  const struct player *pplayer)
{
  struct city *pcity = tile_city(ptile);

  if (pcity != NULL && pplayers_non_attack(pplayer, city_owner(pcity))) {
    return pcity;
  }

  return NULL;
}

/**********************************************************************//**
  Return non-allied city on the tile, if one exist
**************************************************************************/
struct city *tile_non_allied_city(const struct tile *ptile,
                                  const struct player *pplayer)
{
  struct city *pcity = tile_city(ptile);

  if (pcity != NULL && !pplayers_allied(pplayer, city_owner(pcity))) {
    return pcity;
  }

  return NULL;
}

/**********************************************************************//**
  Return TRUE if there is a friendly city near to this unit
**************************************************************************/
bool is_unit_near_a_friendly_city(const struct civ_map *nmap,
                                  const struct unit *punit,
                                  int distance)
{
  return is_friendly_city_near(nmap, unit_owner(punit), unit_tile(punit),
                               distance);
}

/**********************************************************************//**
  Return TRUE if there is a friendly city near to this tile.
**************************************************************************/
bool is_friendly_city_near(const struct civ_map *nmap,
                           const struct player *owner,
                           const struct tile *ptile,
                           int distance)
{
  square_iterate(nmap, ptile, distance, ptile1) {
    struct city *pcity = tile_city(ptile1);

    if (pcity && pplayers_allied(owner, city_owner(pcity))) {
      return TRUE;
    }
  } square_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  Return TRUE iff a city exists within a city radius of the given
  location. may_be_on_center determines if a city at x,y counts.
**************************************************************************/
bool city_exists_within_max_city_map(const struct civ_map *nmap,
                                     const struct tile *ptile,
                                     bool may_be_on_center)
{
  city_tile_iterate(nmap, CITY_MAP_MAX_RADIUS_SQ, ptile, ptile1) {
    if (may_be_on_center || !same_pos(ptile, ptile1)) {
      if (tile_city(ptile1)) {
        return TRUE;
      }
    }
  } city_tile_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  Generalized formula used to calculate granary size.

  The AI may not deal well with non-default settings.  See food_weighting().
**************************************************************************/
int city_granary_size(int city_size)
{
  int food_inis = game.info.granary_num_inis;
  int food_inc = game.info.granary_food_inc;
  int base_value;

  /* If the city has no citizens, there is no granary. */
  if (city_size == 0) {
    return 0;
  }

  /* Granary sizes for the first food_inis citizens are given directly.
   * After that we increase the granary size by food_inc per citizen. */
  if (city_size > food_inis) {
    base_value = game.info.granary_food_ini[food_inis - 1];
    base_value += food_inc * (city_size - food_inis);
  } else {
    base_value = game.info.granary_food_ini[city_size - 1];
  }

  return MAX(base_value * game.info.foodbox / 100, 1);
}

/**********************************************************************//**
  Give base happiness in any city owned by pplayer.
  A positive number is a number of content citizens. A negative number is
  a number of angry citizens (a city never starts with both).
**************************************************************************/
static int player_base_citizen_happiness(const struct player *pplayer)
{
  int cities = city_list_size(pplayer->cities);
  int content = get_player_bonus(pplayer, EFT_CITY_UNHAPPY_SIZE);
  int basis = get_player_bonus(pplayer, EFT_EMPIRE_SIZE_BASE);
  int step = get_player_bonus(pplayer, EFT_EMPIRE_SIZE_STEP);

  if (basis + step <= 0) {
    /* Value of zero means effect is inactive */
    return content;
  }

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

/**********************************************************************//**
  Give base number of content citizens in any city owned by pplayer.
**************************************************************************/
citizens player_content_citizens(const struct player *pplayer)
{
  int content = player_base_citizen_happiness(pplayer);

  return CLIP(0, content, MAX_CITY_SIZE);
}

/**********************************************************************//**
  Give base number of angry citizens in any city owned by pplayer.
**************************************************************************/
citizens player_angry_citizens(const struct player *pplayer)
{
  if (!game.info.angrycitizen) {
    return 0;
  } else {
    /* Create angry citizens only if we have a negative number of possible
     * content citizens. This can happen when empires grow really big. */
    int content = player_base_citizen_happiness(pplayer);

    return CLIP(0, -content, MAX_CITY_SIZE);
  }
}

/**********************************************************************//**
 Return the factor (in %) by which the city's output should be multiplied.
**************************************************************************/
int get_final_city_output_bonus(const struct city *pcity, Output_type_id otype)
{
  struct output_type *output = &output_types[otype];
  int bonus1 = 100 + get_city_tile_output_bonus(pcity, NULL, output,
						EFT_OUTPUT_BONUS);
  int bonus2 = 100 + get_city_tile_output_bonus(pcity, NULL, output,
						EFT_OUTPUT_BONUS_2);

  return MAX(bonus1 * bonus2 / 100, 0);
}

/**********************************************************************//**
  Return the amount of gold generated by buildings under "tithe" attribute
  governments.
**************************************************************************/
int get_city_tithes_bonus(const struct city *pcity)
{
  int tithes_bonus = 0;

  if (get_city_bonus(pcity, EFT_HAPPINESS_TO_GOLD) <= 0) {
    return 0;
  }

  tithes_bonus += get_city_bonus(pcity, EFT_MAKE_CONTENT);
  tithes_bonus += get_city_bonus(pcity, EFT_FORCE_CONTENT);

  return tithes_bonus;
}

/**********************************************************************//**
  Add the incomes of a city according to the taxrates (ignore # of
  specialists). trade should be in output[O_TRADE].
**************************************************************************/
void add_tax_income(const struct player *pplayer, int trade, int *output)
{
  const int SCIENCE = 0, TAX = 1, LUXURY = 2;
  unsigned rates[3];
  int result[3];

  if (game.info.changable_tax) {
    rates[SCIENCE] = pplayer->economic.science;
    rates[LUXURY] = pplayer->economic.luxury;
    rates[TAX] = 100 - rates[SCIENCE] - rates[LUXURY];
  } else {
    rates[SCIENCE] = game.info.forced_science;
    rates[LUXURY] = game.info.forced_luxury;
    rates[TAX] = game.info.forced_gold;
  }

  /* ANARCHY */
  if (government_of_player(pplayer) == game.government_during_revolution) {
    rates[SCIENCE] = 0;
    rates[LUXURY] = 100;
    rates[TAX] = 0;
  }

  distribute(trade, 3, rates, result);

  output[O_SCIENCE] += result[SCIENCE];
  output[O_GOLD] += result[TAX];
  output[O_LUXURY] += result[LUXURY];
}

/**********************************************************************//**
  Return TRUE if the city built something last turn (meaning production
  was completed between last turn and this).
**************************************************************************/
bool city_built_last_turn(const struct city *pcity)
{
  return pcity->turn_last_built + 1 >= game.info.turn;
}

/**********************************************************************//**
  Find government center nearest to given city.
**************************************************************************/
static const struct city *nearest_gov_center(const struct city *pcity,
                                             int *min_dist)
{
  const struct city *gov_center = NULL;

  *min_dist = FC_INFINITY;

  /* Check the special case that city itself is gov center
   * before expensive iteration through all cities. */
  if (is_gov_center(pcity)) {
    *min_dist = 0;
    return pcity;
  } else {
    city_list_iterate(city_owner(pcity)->cities, gc) {
      /* Do not recheck current city */
      if (gc != pcity && is_gov_center(gc)) {
        int dist = real_map_distance(gc->tile, pcity->tile);

        if (dist < *min_dist) {
          gov_center = gc;
          *min_dist = dist;
        }
      }
    } city_list_iterate_end;
  }

  return gov_center;
}

/**********************************************************************//**
  Calculate output (food, trade and shields) generated by the worked tiles
  of a city. This will completely overwrite the output[] array.

  'workers_map' is an boolean array which defines the placement of the
  workers within the city map. It uses the tile index and its size is
  defined by city_map_tiles_from_city(_pcity). See also cm_state_init().
**************************************************************************/
static inline void get_worked_tile_output(const struct civ_map *nmap,
                                          const struct city *pcity,
                                          int *output, bool *workers_map)
{
  bool is_worked;
#ifdef CITY_DEBUGGING
  bool is_celebrating = base_city_celebrating(pcity);
#endif
  struct tile *pcenter = city_tile(pcity);

  memset(output, 0, O_LAST * sizeof(*output));

  city_tile_iterate_index(nmap, city_map_radius_sq_get(pcity), pcenter, ptile,
                          city_tile_index) {
    if (workers_map == NULL) {
      struct city *pwork = tile_worked(ptile);

      is_worked = (NULL != pwork && pwork == pcity);
    } else {
      is_worked = workers_map[city_tile_index];
    }

    if (is_worked) {
      output_type_iterate(o) {
#ifdef CITY_DEBUGGING
        /* This assertion never fails, but it's so slow that we disable
         * it by default. */
        fc_assert(city_tile_cache_get_output(pcity, city_tile_index, o)
                  == city_tile_output(pcity, ptile, is_celebrating, o));
#endif /* CITY_DEBUGGING */
        output[o] += city_tile_cache_get_output(pcity, city_tile_index, o);
      } output_type_iterate_end;
    }
  } city_tile_iterate_index_end;
}

/**********************************************************************//**
  Calculate output (gold, science, and luxury) generated by the specialists
  of a city.  The output[] array is not cleared but is just added to.
**************************************************************************/
void add_specialist_output(const struct city *pcity, int *output)
{
  specialist_type_iterate(sp) {
    int count = pcity->specialists[sp];

    output_type_iterate(stat_index) {
      int amount = get_specialist_output(pcity, sp, stat_index);

      output[stat_index] += count * amount;
    } output_type_iterate_end;
  } specialist_type_iterate_end;
}

/**********************************************************************//**
  This function sets all the values in the pcity->bonus[] and
  pcity->abs_bonus[] arrays.
  Called near the beginning of city_refresh_from_main_map().

  It doesn't depend on anything else in the refresh and doesn't change
  as workers are moved around, but does change when buildings are built,
  etc.
**************************************************************************/
static inline void set_city_bonuses(struct city *pcity)
{
  output_type_iterate(o) {
    pcity->bonus[o] = get_final_city_output_bonus(pcity, o);
    pcity->abs_bonus[o] = get_city_output_bonus(pcity,
                                                &output_types[o],
                                                EFT_OUTPUT_BONUS_ABSOLUTE);
    /* Total bonus cannot be negative, as that could lead to unresolvable
     * negative balance. */
    pcity->abs_bonus[o] = MIN(pcity->abs_bonus[o], 0);
  } output_type_iterate_end;
}

/**********************************************************************//**
  This function sets the cache for the tile outputs, the pcity->tile_cache[]
  array. It is called near the beginning of city_refresh_from_main_map().

  It doesn't depend on anything else in the refresh and doesn't change
  as workers are moved around, but does change when buildings are built,
  etc.

  TODO: use the cached values elsewhere in the code!
**************************************************************************/
static inline void city_tile_cache_update(const struct civ_map *nmap,
                                          struct city *pcity)
{
  bool is_celebrating = base_city_celebrating(pcity);
  int radius_sq = city_map_radius_sq_get(pcity);

  /* Initialize tile_cache if needed */
  if (pcity->tile_cache == NULL || pcity->tile_cache_radius_sq == -1
      || pcity->tile_cache_radius_sq != radius_sq) {
    pcity->tile_cache = fc_realloc(pcity->tile_cache,
                                   city_map_tiles(radius_sq)
                                   * sizeof(*(pcity->tile_cache)));
    pcity->tile_cache_radius_sq = radius_sq;
  }

  /* Any unreal tiles are skipped - these values should have been memset
   * to 0 when the city was created. */
  city_tile_iterate_index(nmap, radius_sq, pcity->tile, ptile, city_tile_index) {
    output_type_iterate(o) {
      (pcity->tile_cache[city_tile_index]).output[o]
        = city_tile_output(pcity, ptile, is_celebrating, o);
    } output_type_iterate_end;
  } city_tile_iterate_index_end;
}

/**********************************************************************//**
  This function returns the output of 'o' for the city tile 'city_tile_index'
  of 'pcity'.
**************************************************************************/
static inline int city_tile_cache_get_output(const struct city *pcity,
                                             int city_tile_index,
                                             enum output_type_id o)
{
  fc_assert_ret_val(pcity->tile_cache_radius_sq
                    == city_map_radius_sq_get(pcity), 0);
  fc_assert_ret_val(city_tile_index < city_map_tiles_from_city(pcity), 0);

  return (pcity->tile_cache[city_tile_index]).output[o];
}

/**********************************************************************//**
  Set the final surplus[] array from the prod[] and usage[] values.
**************************************************************************/
static void set_surpluses(struct city *pcity)
{
  output_type_iterate(o) {
    int surplus = pcity->prod[o] - pcity->usage[o];

    /* Add 'surplus' waste to 'usage'. */
    if (surplus > 0) {
      struct output_type *output = get_output_type(o);
      int waste_level = get_city_output_bonus(pcity, output, EFT_SURPLUS_WASTE_PCT);
      int waste_by_rel_dist
        = get_city_output_bonus(pcity, output, EFT_SURPLUS_WASTE_PCT_BY_REL_DISTANCE);

      if (waste_by_rel_dist > 0) {
        int min_dist;
        const struct city *gov_center = nearest_gov_center(pcity, &min_dist);

        if (gov_center == NULL) {
          /* No gov center - no income */
          waste_level = 100;
        } else {
          waste_level += waste_by_rel_dist * 50 * min_dist / 100
            / MAX(MAP_NATIVE_WIDTH, MAP_NATIVE_HEIGHT);
        }
      }

      if (waste_level > 0) {
        if (waste_level < 100) {
          pcity->usage[o] += (surplus * waste_level / 100);
        } else {
          pcity->usage[o] = pcity->prod[o];
        }
      }
    }

    pcity->surplus[o] = pcity->prod[o] - pcity->usage[o];
  } output_type_iterate_end;
}

/**********************************************************************//**
  Copy the happiness array in the city to index i from index i-1.
**************************************************************************/
static void happy_copy(struct city *pcity, enum citizen_feeling i)
{
  int c = 0;

  for (; c < CITIZEN_LAST; c++) {
    pcity->feel[c][i] = pcity->feel[c][i - 1];
  }
}

/**********************************************************************//**
  Create content, unhappy and angry citizens.
**************************************************************************/
static void citizen_base_mood(struct city *pcity)
{
  struct player *pplayer = city_owner(pcity);
  citizens *happy = &pcity->feel[CITIZEN_HAPPY][FEELING_BASE];
  citizens *content = &pcity->feel[CITIZEN_CONTENT][FEELING_BASE];
  citizens *unhappy = &pcity->feel[CITIZEN_UNHAPPY][FEELING_BASE];
  citizens *angry = &pcity->feel[CITIZEN_ANGRY][FEELING_BASE];
  citizens size = city_size_get(pcity);
  citizens spes = city_specialists(pcity);

  /* This is the number of citizens that may start out content, depending
   * on empire size and game's city unhappysize. This may be bigger than
   * the size of the city, since this is a potential. */
  citizens base_content = player_content_citizens(pplayer);
  /* Similarly, this is the potential number of angry citizens. */
  citizens base_angry = player_angry_citizens(pplayer);

  /* Create content citizens. Take specialists from their ranks. */
  *content = MAX(0, MIN(size, base_content) - spes);

  /* Create angry citizens. Specialists never become angry. */
  fc_assert_action(base_content == 0 || base_angry == 0, *content = 0);
  *angry = MIN(base_angry, size - spes);

  /* Create unhappy citizens. In the beginning, all who are not content,
   * specialists or angry are unhappy. This is changed by luxuries and
   * buildings later. */
  *unhappy = (size - spes - *content - *angry);

  /* No one is born happy. */
  *happy = 0;
}

/**********************************************************************//**
  Make people happy:
   * angry citizen are eliminated first
   * then content are made happy, then unhappy content, etc.
   * each conversions costs 2 or 4 luxuries.
**************************************************************************/
static inline void citizen_luxury_happy(struct city *pcity, int *luxuries)
{
  citizens *happy = &pcity->feel[CITIZEN_HAPPY][FEELING_LUXURY];
  citizens *content = &pcity->feel[CITIZEN_CONTENT][FEELING_LUXURY];
  citizens *unhappy = &pcity->feel[CITIZEN_UNHAPPY][FEELING_LUXURY];
  citizens *angry = &pcity->feel[CITIZEN_ANGRY][FEELING_LUXURY];

  while (*luxuries >= game.info.happy_cost && *angry > 0) {
    /* Upgrade angry to unhappy: costs HAPPY_COST each. */
    (*angry)--;
    (*unhappy)++;
    *luxuries -= game.info.happy_cost;
  }
  while (*luxuries >= game.info.happy_cost && *content > 0) {
    /* Upgrade content to happy: costs HAPPY_COST each. */
    (*content)--;
    (*happy)++;
    *luxuries -= game.info.happy_cost;
  }
  while (*luxuries >= 2 * game.info.happy_cost && *unhappy > 0) {
    /* Upgrade unhappy to happy.  Note this is a 2-level upgrade with
     * double the cost. */
    (*unhappy)--;
    (*happy)++;
    *luxuries -= 2 * game.info.happy_cost;
  }
  if (*luxuries >= game.info.happy_cost && *unhappy > 0) {
    /* Upgrade unhappy to content: costs HAPPY_COST each. */
    (*unhappy)--;
    (*content)++;
    *luxuries -= game.info.happy_cost;
  }
}

/**********************************************************************//**
  Make citizens happy due to luxury.
**************************************************************************/
static inline void citizen_happy_luxury(struct city *pcity)
{
  int x = pcity->prod[O_LUXURY];

  citizen_luxury_happy(pcity, &x);
}

/**********************************************************************//**
  Make citizens content due to city improvements.
**************************************************************************/
static inline void citizen_content_buildings(struct city *pcity)
{
  citizens *content = &pcity->feel[CITIZEN_CONTENT][FEELING_EFFECT];
  citizens *unhappy = &pcity->feel[CITIZEN_UNHAPPY][FEELING_EFFECT];
  citizens *angry = &pcity->feel[CITIZEN_ANGRY][FEELING_EFFECT];
  int faces = get_city_bonus(pcity, EFT_MAKE_CONTENT);

  /* make people content (but not happy):
     get rid of angry first, then make unhappy content. */
  while (faces > 0 && *angry > 0) {
    (*angry)--;
    (*unhappy)++;
    faces--;
  }
  while (faces > 0 && *unhappy > 0) {
    (*unhappy)--;
    (*content)++;
    faces--;
  }
}

/**********************************************************************//**
  Apply effects of citizen nationality to happiness
**************************************************************************/
static inline void citizen_happiness_nationality(struct city *pcity)
{
  citizens *happy = &pcity->feel[CITIZEN_HAPPY][FEELING_NATIONALITY];
  citizens *content = &pcity->feel[CITIZEN_CONTENT][FEELING_NATIONALITY];
  citizens *unhappy = &pcity->feel[CITIZEN_UNHAPPY][FEELING_NATIONALITY];

  if (game.info.citizen_nationality) {
    int pct = get_city_bonus(pcity, EFT_ENEMY_CITIZEN_UNHAPPY_PCT);

    if (pct > 0) {
      int enemies = 0;
      int unhappy_inc;
      struct player *owner = city_owner(pcity);

      citizens_foreign_iterate(pcity, pslot, nationality) {
        if (pplayers_at_war(owner, player_slot_get_player(pslot))) {
          enemies += nationality;
        }
      } citizens_foreign_iterate_end;

      unhappy_inc = enemies * pct / 100;

      /* First make content => unhappy, then happy => unhappy,
       * then happy => content. No-one becomes angry. */
      while (unhappy_inc > 0 && *content > 0) {
        (*content)--;
        (*unhappy)++;
        unhappy_inc--;
      }
      while (unhappy_inc > 1 && *happy > 0) {
        (*happy)--;
        (*unhappy)++;
        unhappy_inc -= 2;
      }
      while (unhappy_inc > 0 && *happy > 0) {
        (*happy)--;
        (*content)++;
        unhappy_inc--;
      }
    }
  }
}

/**********************************************************************//**
  Make citizens happy/unhappy due to units.

  This function requires that pcity->martial_law and
  pcity->unit_happy_cost have already been set in city_support().
**************************************************************************/
static inline void citizen_happy_units(struct city *pcity)
{
  citizens *happy = &pcity->feel[CITIZEN_HAPPY][FEELING_MARTIAL];
  citizens *content = &pcity->feel[CITIZEN_CONTENT][FEELING_MARTIAL];
  citizens *unhappy = &pcity->feel[CITIZEN_UNHAPPY][FEELING_MARTIAL];
  citizens *angry = &pcity->feel[CITIZEN_ANGRY][FEELING_MARTIAL];
  citizens  amt = pcity->martial_law;

  /* Pacify discontent citizens through martial law.  First convert
   * angry => unhappy, then unhappy => content. */
  while (amt > 0 && *angry > 0) {
    (*angry)--;
    (*unhappy)++;
    amt--;
  }
  while (amt > 0 && *unhappy > 0) {
    (*unhappy)--;
    (*content)++;
    amt--;
  }

  /* Now make citizens unhappier because of military units away from home.
   * First make content => unhappy, then happy => unhappy,
   * then happy => content. */
  amt = pcity->unit_happy_upkeep;
  while (amt > 0 && *content > 0) {
    (*content)--;
    (*unhappy)++;
    amt--;
  }
  while (amt > 1 && *happy > 0) {
    (*happy)--;
    (*unhappy)++;
    amt -= 2;
  }
  while (amt > 0 && *happy > 0) {
    (*happy)--;
    (*content)++;
    amt--;
  }
  /* Any remaining unhappiness is lost since angry citizens aren't created
   * here. */
  /* FIXME: Why not? - Per */
}

/**********************************************************************//**
  Make citizens happy due to wonders.
**************************************************************************/
static inline void citizen_happy_wonders(struct city *pcity)
{
  citizens *happy = &pcity->feel[CITIZEN_HAPPY][FEELING_FINAL];
  citizens *content = &pcity->feel[CITIZEN_CONTENT][FEELING_FINAL];
  citizens *unhappy = &pcity->feel[CITIZEN_UNHAPPY][FEELING_FINAL];
  citizens *angry = &pcity->feel[CITIZEN_ANGRY][FEELING_FINAL];
  int bonus = get_city_bonus(pcity, EFT_MAKE_HAPPY);

  /* First create happy citizens from content, then from unhappy
   * citizens; we cannot help angry citizens here. */
  while (bonus > 0 && *content > 0) {
    (*content)--;
    (*happy)++;
    bonus--;
  }
  while (bonus > 1 && *unhappy > 0) {
    (*unhappy)--;
    (*happy)++;
    bonus -= 2;
  }
  /* The rest falls through and lets unhappy people become content. */

  if (get_city_bonus(pcity, EFT_NO_UNHAPPY) > 0) {
    *content += *unhappy + *angry;
    *unhappy = 0;
    *angry = 0;
    return;
  }

  bonus += get_city_bonus(pcity, EFT_FORCE_CONTENT);

  /* get rid of angry first, then make unhappy content */
  while (bonus > 0 && *angry > 0) {
    (*angry)--;
    (*unhappy)++;
    bonus--;
  }
  while (bonus > 0 && *unhappy > 0) {
    (*unhappy)--;
    (*content)++;
    bonus--;
  }
}

/**********************************************************************//**
  Set food, tax, science and shields production to zero if city is in
  disorder.
**************************************************************************/
static inline void unhappy_city_check(struct city *pcity)
{
  if (city_unhappy(pcity)) {
    output_type_iterate(o) {
      switch (output_types[o].unhappy_penalty) {
      case UNHAPPY_PENALTY_NONE:
	pcity->unhappy_penalty[o] = 0;
	break;
      case UNHAPPY_PENALTY_SURPLUS:
	pcity->unhappy_penalty[o] = MAX(pcity->prod[o] - pcity->usage[o], 0);
	break;
      case UNHAPPY_PENALTY_ALL_PRODUCTION:
	pcity->unhappy_penalty[o] = pcity->prod[o];
	break;
      }

      pcity->prod[o] -= pcity->unhappy_penalty[o];
    } output_type_iterate_end;
  } else {
    memset(pcity->unhappy_penalty, 0,
 	   O_LAST * sizeof(*pcity->unhappy_penalty));
  }
}

/**********************************************************************//**
  Calculate the pollution from production and population in the city.
**************************************************************************/
int city_pollution_types(const struct city *pcity, int shield_total,
			 int *pollu_prod, int *pollu_pop, int *pollu_mod)
{
  int prod, pop, mod;

  /* Add one one pollution per shield, multipled by the bonus. */
  prod = 100 + get_city_bonus(pcity, EFT_POLLU_PROD_PCT);
  prod = shield_total * MAX(prod, 0) / 100;

  /* Add one pollution per citizen for baseline combined bonus (100%). */
  pop = (100 + get_city_bonus(pcity, EFT_POLLU_POP_PCT))
      * (100 + get_city_bonus(pcity, EFT_POLLU_POP_PCT_2))
      / 100;
  pop = (city_size_get(pcity) * MAX(pop, 0)) / 100;

  /* Then there is base pollution (usually a negative number). */
  mod = game.info.base_pollution;

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

/**********************************************************************//**
  Calculate pollution for the city.  The shield_total must be passed in
  (most callers will want to pass pcity->shield_prod).
**************************************************************************/
int city_pollution(const struct city *pcity, int shield_total)
{
  return city_pollution_types(pcity, shield_total, NULL, NULL, NULL);
}

/**********************************************************************//**
  Gets whether cities that pcity trades with had the plague. If so, it
  returns the health penalty in tenth of percent which depends on the size
  of both cities. The health penalty is given as the product of the ruleset
  option 'game.info.illness_trade_infection' (in percent) and the square
  root of the product of the size of both cities.
 *************************************************************************/
static int get_trade_illness(const struct city *pcity)
{
  float illness_trade = 0.0;

  trade_partners_iterate(pcity, trade_city) {
    if (trade_city->turn_plague != -1
        && game.info.turn - trade_city->turn_plague < 5) {
      illness_trade += (float)game.info.illness_trade_infection
                       * sqrt(1.0 * city_size_get(pcity)
                              * city_size_get(trade_city)) / 100.0;
    }
  } trade_partners_iterate_end;

  return (int)illness_trade;
}

/**********************************************************************//**
  Get any effects regarding health from the buildings of the city. The
  effect defines the reduction of the possibility of an illness in percent.
**************************************************************************/
static int get_city_health(const struct city *pcity)
{
  return get_city_bonus(pcity, EFT_HEALTH_PCT);
}

/**********************************************************************//**
  Calculate city's illness in tenth of percent:

  base illness        (the maximum value for illness in percent is given by
                       'game.info.illness_base_factor')
  + trade illness     (see get_trade_illness())
  + pollution illness (the pollution in the city times
                       'game.info.illness_pollution_factor')

  The illness is reduced by the percentage given by the health effect.
  Illness cannot exceed 999 (= 99.9%), or be less then 0
 *************************************************************************/
int city_illness_calc(const struct city *pcity, int *ill_base,
                      int *ill_size, int *ill_trade, int *ill_pollution)
{
  int illness_size = 0, illness_trade = 0, illness_pollution = 0;
  int illness_base, illness_percent;

  if (game.info.illness_on
      && city_size_get(pcity) > game.info.illness_min_size) {
    /* offset the city size by game.info.illness_min_size */
    int use_size = city_size_get(pcity) - game.info.illness_min_size;

    illness_size = (int)((1.0 - exp(- (float)use_size / 10.0))
                         * 10.0 * game.info.illness_base_factor);
    if (is_server()) {
      /* On the server we recalculate the illness due to trade as we have
       * all the information */
      illness_trade = get_trade_illness(pcity);
    } else {
      /* On the client we have to rely on the value saved within the city
       * struct */
      illness_trade = pcity->illness_trade;
    }

    illness_pollution = pcity->pollution
                        * game.info.illness_pollution_factor / 100;
  }

  illness_base = illness_size + illness_trade + illness_pollution;
  illness_percent = 100 - get_city_health(pcity);

  /* returning other data */
  if (ill_size) {
    *ill_size = illness_size;
  }

  if (ill_trade) {
    *ill_trade = illness_trade;
  }

  if (ill_pollution) {
    *ill_pollution = illness_pollution;
  }

  if (ill_base) {
    *ill_base = illness_base;
  }

  return CLIP(0, illness_base * illness_percent / 100 , 999);
}

/**********************************************************************//**
  Returns whether city had a plague outbreak this turn.
**************************************************************************/
bool city_had_recent_plague(const struct city *pcity)
{
  /* Correctly handles special case turn_plague == -1 (never) */
  return (pcity->turn_plague == game.info.turn);
}

/**********************************************************************//**
  The maximum number of units a city can build per turn.
**************************************************************************/
int city_build_slots(const struct city *pcity)
{
  return get_city_bonus(pcity, EFT_CITY_BUILD_SLOTS);
}

/**********************************************************************//**
  A city's maximum airlift capacity.
  (Note, this still returns a finite number even if airliftingstyle allows
  unlimited airlifts)
**************************************************************************/
int city_airlift_max(const struct city *pcity)
{
  return get_city_bonus(pcity, EFT_AIRLIFT);
}

/**********************************************************************//**
   Set food, trade and shields production in a city.

   This initializes the prod[] and waste[] arrays.  It assumes that
   the bonus[] and citizen_base[] arrays are alread built.
**************************************************************************/
inline void set_city_production(struct city *pcity)
{
  int trade;

  /* Calculate city production!
   *
   * This is a rather complicated process if we allow rules to become
   * more generalized. We can assume that there are no recursive dependency
   * loops, but there are some dependencies that do not follow strict
   * ordering. For instance corruption must be calculated before
   * trade taxes can be counted up, which must occur before the science bonus
   * is added on. But the calculation of corruption must include the
   * trade bonus. To do this without excessive special casing means that in
   * this case the bonuses are multiplied on twice (but only saved the second
   * time).
   */

  output_type_iterate(o) {
    pcity->prod[o] = pcity->citizen_base[o];
  } output_type_iterate_end;

  /* Add on special extra incomes: trade routes and tithes. */
  trade_routes_iterate(pcity, proute) {
    struct city *tcity = game_city_by_number(proute->partner);
    bool can_trade;

    /* Partner city may have not yet been sent to the client, or
     * there's just a placeholder city with a placeholder owner
     * created for some tile->worked. */
    if (!is_server()
        && (tcity == NULL
            || city_owner(tcity)->slot == NULL)) {
      continue;
    }

    fc_assert_action(tcity != NULL, continue);

    can_trade = can_cities_trade(pcity, tcity);

    if (!can_trade) {
      enum trade_route_type type = cities_trade_route_type(pcity, tcity);
      struct trade_route_settings *settings = trade_route_settings_by_type(type);

      if (settings->cancelling == TRI_ACTIVE) {
        can_trade = TRUE;
      }
    }

    if (can_trade) {
      int value;

      value =
        trade_base_between_cities(pcity, game_city_by_number(proute->partner));
      proute->value = trade_from_route(pcity, proute, value);
      pcity->prod[O_TRADE] += proute->value
        * (100 + get_city_bonus(pcity, EFT_TRADE_ROUTE_PCT)) / 100;
    } else {
      proute->value = 0;
    }
  } trade_routes_iterate_end;
  pcity->prod[O_GOLD] += get_city_tithes_bonus(pcity);

  /* Account for waste.  Note that waste is calculated before tax income is
   * calculated, so if you had "science waste" it would not include taxed
   * science.  However waste is calculated after the bonuses are multiplied
   * on, so shield waste will include shield bonuses. */
  output_type_iterate(o) {
    int prod = pcity->prod[o] * pcity->bonus[o] / 100 + pcity->abs_bonus[o];

    prod = MAX(prod, 0);
    pcity->waste[o] = city_waste(pcity, o, prod, NULL);
  } output_type_iterate_end;

  /* Convert trade into science/luxury/gold, and add this on to whatever
   * science/luxury/gold is already there. */
  trade = pcity->prod[O_TRADE] * pcity->bonus[O_TRADE] / 100
    + pcity->abs_bonus[O_TRADE];
  trade = MAX(trade, 0);
  add_tax_income(city_owner(pcity),
                 trade - pcity->waste[O_TRADE] - pcity->usage[O_TRADE],
                 pcity->prod);

  /* Add on effect bonuses and waste. Note that the waste calculation
   * (above) already includes the bonuses. */
  output_type_iterate(o) {
    int prod = pcity->prod[o] * pcity->bonus[o] / 100 + pcity->abs_bonus[o];

    prod = MAX(prod, 0);
    pcity->prod[o] = prod;
    pcity->prod[o] -= pcity->waste[o];
  } output_type_iterate_end;
}

/**********************************************************************//**
  Query unhappiness caused by a given unit.
**************************************************************************/
int city_unit_unhappiness(const struct civ_map *nmap,
                          struct unit *punit, int *free_unhappy)
{
  struct city *pcity;
  const struct unit_type *ut;
  struct player *plr;
  int happy_cost;

  if (punit == NULL || free_unhappy == NULL) {
    return 0;
  }

  pcity = game_city_by_number(punit->homecity);
  if (pcity == NULL) {
    return 0;
  }

  ut = unit_type_get(punit);
  plr = unit_owner(punit);
  happy_cost = utype_happy_cost(ut, plr);

  if (happy_cost <= 0) {
    return 0;
  }

  fc_assert_ret_val(0 <= *free_unhappy, 0);

  if (!unit_being_aggressive(nmap, punit) && !is_field_unit(punit)) {
    return 0;
  }

  happy_cost -= get_city_bonus(pcity, EFT_MAKE_CONTENT_MIL_PER);
  if (happy_cost <= 0) {
    return 0;
  }

  if (*free_unhappy >= happy_cost) {
    *free_unhappy -= happy_cost;
    return 0;
  } else {
    happy_cost -= *free_unhappy;
    *free_unhappy = 0;
  }

  return happy_cost;
}

/**********************************************************************//**
  Calculate upkeep costs. This builds the pcity->usage[] array as well
  as setting some happiness values.
**************************************************************************/
static inline void city_support(const struct civ_map *nmap,
                                struct city *pcity)
{
  int free_unhappy;
  int max_mart_units;

  /* Clear all usage values. */
  memset(pcity->usage, 0, O_LAST * sizeof(*pcity->usage));
  pcity->martial_law = 0;
  pcity->unit_happy_upkeep = 0;

  /* Building and unit gold upkeep depends on the setting
   * 'game.info.gold_upkeep_style':
   * GOLD_UPKEEP_CITY: The upkeep for buildings and units is paid by the
   *                   city.
   * GOLD_UPKEEP_MIXED: The upkeep for buildings is paid by the city.
   *                    The upkeep for units is paid by the nation.
   * GOLD_UPKEEP_NATION: The upkeep for buildings and units is paid by the
   *                     nation. */
  fc_assert_msg(gold_upkeep_style_is_valid(game.info.gold_upkeep_style),
                "Invalid gold_upkeep_style %d", game.info.gold_upkeep_style);
  switch (game.info.gold_upkeep_style) {
  case GOLD_UPKEEP_CITY:
    pcity->usage[O_GOLD] += city_total_unit_gold_upkeep(pcity);
    fc__fallthrough; /* No break */
  case GOLD_UPKEEP_MIXED:
    pcity->usage[O_GOLD] += city_total_impr_gold_upkeep(pcity);
    break;
  case GOLD_UPKEEP_NATION:
    /* nothing */
    break;
  }
  /* Food consumption by citizens. */
  pcity->usage[O_FOOD] += game.info.food_cost * city_size_get(pcity);

  max_mart_units = get_city_bonus(pcity, EFT_MARTIAL_LAW_MAX);
  if (max_mart_units > 0) {
    int best_units[max_mart_units];
    int sel_count = 0;
    int i;

    unit_list_iterate(pcity->tile->units, punit) {
      if (is_martial_law_unit(punit)
          && unit_owner(punit) == city_owner(pcity)) {
        int current = get_target_bonus_effects(NULL,
                                               &(const struct req_context) {
                                                 .player = city_owner(pcity),
                                                 .city = pcity,
                                                 .tile = city_tile(pcity),
                                                 .unit = punit,
                                                 .unittype = unit_type_get(punit)
                                               },
                                               NULL, EFT_MARTIAL_LAW_BY_UNIT);
        if (current > 0) {
          if (sel_count < max_mart_units) {
            best_units[sel_count++] = current;
          } else if (current > best_units[max_mart_units - 1]) {
            for (i = max_mart_units - 1; i >= 0 && current > best_units[i]; i--) {
              if (i + 1 < max_mart_units) {
                best_units[i + 1] = best_units[i];
              }
            }

            best_units[i + 1] = current;
          }
        }
      }
    } unit_list_iterate_end;

    pcity->martial_law = 0;
    for (i = 0; i < sel_count; i++) {
      pcity->martial_law += best_units[i];
    }

    pcity->martial_law = MIN(pcity->martial_law, MAX_CITY_SIZE);
  }

  free_unhappy = get_city_bonus(pcity, EFT_MAKE_CONTENT_MIL);
  unit_list_iterate(pcity->units_supported, punit) {
    pcity->unit_happy_upkeep += city_unit_unhappiness(nmap, punit, &free_unhappy);
    output_type_iterate(o) {
      if (O_GOLD != o) {
        /* O_GOLD is handled with "game.info.gold_upkeep_style", see over. */
        pcity->usage[o] += punit->upkeep[o];
      }
    } output_type_iterate_end;
  } unit_list_iterate_end;
}

/**********************************************************************//**
  Refreshes the internal cached data in the city structure.

  !full_refresh will not update tile_cache[] or bonus[].  These two
  values do not need to be recalculated for AI CMA testing.

  'workers_map' is an boolean array which defines the placement of the
  workers within the city map. It uses the tile index and its size is
  defined by city_map_tiles_from_city(_pcity). See also cm_state_init().

  If 'workers_map' is set, only basic updates are needed.
**************************************************************************/
void city_refresh_from_main_map(const struct civ_map *nmap,
                                struct city *pcity, bool *workers_map)
{
  if (workers_map == NULL) {
    /* do a full refresh */

    /* Calculate the bonus[] array values. */
    set_city_bonuses(pcity);
    /* Calculate the tile_cache[] values. */
    city_tile_cache_update(nmap, pcity);
    /* Manage settlers, and units */
    city_support(nmap, pcity);
  }

  /* Calculate output from citizens (uses city_tile_cache_get_output()). */
  get_worked_tile_output(nmap, pcity, pcity->citizen_base, workers_map);
  add_specialist_output(pcity, pcity->citizen_base);

  set_city_production(pcity);
  citizen_base_mood(pcity);
  /* Note that pollution is calculated before unhappy_city_check() makes
   * deductions for disorder; so a city in disorder still causes pollution */
  pcity->pollution = city_pollution(pcity, pcity->prod[O_SHIELD]);

  happy_copy(pcity, FEELING_LUXURY);
  citizen_happy_luxury(pcity);	/* With our new found luxuries */

  happy_copy(pcity, FEELING_EFFECT);
  citizen_content_buildings(pcity);

  happy_copy(pcity, FEELING_NATIONALITY);
  citizen_happiness_nationality(pcity);

  /* Martial law & unrest from units */
  happy_copy(pcity, FEELING_MARTIAL);
  citizen_happy_units(pcity);

  /* Building (including wonder) happiness effects */
  happy_copy(pcity, FEELING_FINAL);
  citizen_happy_wonders(pcity);

  unhappy_city_check(pcity);
  set_surpluses(pcity);
}

/**********************************************************************//**
  Give corruption/waste generated by city. otype gives the output type
  (O_SHIELD/O_TRADE). 'total' gives the total output of this type in the
  city. If non-NULL, 'breakdown' should be an OLOSS_LAST-sized array
  which will be filled in with a breakdown of the kinds of waste
  (not cumulative).
**************************************************************************/
int city_waste(const struct city *pcity, Output_type_id otype, int total,
               int *breakdown)
{
  int penalty_waste = 0;
  int penalty_size = 0;  /* Separate notradesize/fulltradesize from normal
                          * corruption */
  int total_eft = total; /* Normal corruption calculated on total reduced by
                          * possible size penalty */
  const struct output_type *output = get_output_type(otype);
  int waste_level = get_city_output_bonus(pcity, output, EFT_OUTPUT_WASTE);
  bool waste_all = FALSE;

  if (otype == O_TRADE) {
    /* FIXME: special case for trade: it is affected by notradesize and
     * fulltradesize server settings.
     *
     * If notradesize and fulltradesize are equal then the city gets no
     * trade at that size. */
    int notradesize = MIN(game.info.notradesize, game.info.fulltradesize);
    int fulltradesize = MAX(game.info.notradesize, game.info.fulltradesize);

    if (city_size_get(pcity) <= notradesize) {
      penalty_size = total_eft; /* Then no trade income. */
    } else if (city_size_get(pcity) >= fulltradesize) {
      penalty_size = 0;
     } else {
      penalty_size = total_eft * (fulltradesize - city_size_get(pcity))
                     / (fulltradesize - notradesize);
    }
  }

  /* Apply corruption only to anything left after tradesize */
  total_eft -= penalty_size;

  /* Distance-based waste.
   * Don't bother calculating if there's nothing left to lose. */
  if (total_eft > 0) {
    int waste_by_dist = get_city_output_bonus(pcity, output,
                                              EFT_OUTPUT_WASTE_BY_DISTANCE);
    int waste_by_rel_dist = get_city_output_bonus(pcity, output,
                                                  EFT_OUTPUT_WASTE_BY_REL_DISTANCE);
    if (waste_by_dist > 0 || waste_by_rel_dist > 0) {
      int min_dist;
      const struct city *gov_center = nearest_gov_center(pcity, &min_dist);

      if (gov_center == NULL) {
        waste_all = TRUE; /* No gov center - no income */
      } else {
        waste_level += waste_by_dist * min_dist / 100;
        if (waste_by_rel_dist > 0) {
	  /* Multiply by 50 as an "standard size" for which EFT_OUTPUT_WASTE_BY_DISTANCE
	   * and EFT_OUTPUT_WASTE_BY_REL_DISTANCE would give same result. */
          waste_level += waste_by_rel_dist * 50 * min_dist / 100
	    / MAX(MAP_NATIVE_WIDTH, MAP_NATIVE_HEIGHT);
        }
      }
    }
  }

  if (waste_all) {
    penalty_waste = total_eft;
  } else {
    int waste_pct = get_city_output_bonus(pcity, output,
                                          EFT_OUTPUT_WASTE_PCT);

    /* Corruption/waste calculated only for the actually produced amount */
    if (waste_level > 0) {
      penalty_waste = total_eft * waste_level / 100;
    }

    /* Bonus calculated only for the actually produced amount */
    penalty_waste -= penalty_waste * waste_pct / 100;

    /* Clip */
    penalty_waste = MIN(MAX(penalty_waste, 0), total_eft);
  }

  if (breakdown) {
    breakdown[OLOSS_WASTE] = penalty_waste;
    breakdown[OLOSS_SIZE]  = penalty_size;
  }

  /* Add up total penalty */
  return penalty_waste + penalty_size;
}

/**********************************************************************//**
  Give the number of citizens who are specialists in a city.
  Does not count superspecialists
**************************************************************************/
citizens city_specialists(const struct city *pcity)
{
  citizens count = 0;

  normal_specialist_type_iterate(sp) {
    fc_assert_ret_val(MAX_CITY_SIZE - count > pcity->specialists[sp], 0);
    count += pcity->specialists[sp];
  } normal_specialist_type_iterate_end;

  return count;
}

/**********************************************************************//**
  Give the number of superspecialists in a city
**************************************************************************/
int city_superspecialists(const struct city *pcity)
{
  int count = 0;

  super_specialist_type_iterate(sp) {
    count += pcity->specialists[sp];
  } super_specialist_type_iterate_end;

  return count;
}

/**********************************************************************//**
  Return the "best" specialist available in the game. This specialist will
  have the most of the given type of output. If pcity is given then only
  specialists usable by pcity will be considered.
**************************************************************************/
Specialist_type_id best_specialist(Output_type_id otype,
                                   const struct city *pcity)
{
  int best = DEFAULT_SPECIALIST;
  int val = get_specialist_output(pcity, best, otype);

  normal_specialist_type_iterate(i) {
    if (!pcity || city_can_use_specialist(pcity, i)) {
      int val2 = get_specialist_output(pcity, i, otype);

      if (val2 > val) {
	best = i;
	val = val2;
      }
    }
  } normal_specialist_type_iterate_end;

  return best;
}

/**********************************************************************//**
 Adds an improvement (and its effects) to a city.
**************************************************************************/
void city_add_improvement(struct city *pcity,
			  const struct impr_type *pimprove)
{
  pcity->built[improvement_index(pimprove)].turn = game.info.turn; /*I_ACTIVE*/

  if (is_server() && is_wonder(pimprove)) {
    /* Client just read the info from the packets. */
    wonder_built(pcity, pimprove);
  }
}

/**********************************************************************//**
 Removes an improvement (and its effects) from a city.
**************************************************************************/
void city_remove_improvement(struct city *pcity,
			     const struct impr_type *pimprove)
{
  log_debug("Improvement %s removed from city %s",
            improvement_rule_name(pimprove), pcity->name);

  pcity->built[improvement_index(pimprove)].turn = I_DESTROYED;

  if (is_server() && is_wonder(pimprove)) {
    /* Client just read the info from the packets. */
    wonder_destroyed(pcity, pimprove);
  }
}

/**********************************************************************//**
 Returns TRUE iff the city has set the given option.
**************************************************************************/
bool is_city_option_set(const struct city *pcity, enum city_options option)
{
  return BV_ISSET(pcity->city_options, option);
}

/**********************************************************************//**
  Allocate memory for this amount of city styles.
**************************************************************************/
void city_styles_alloc(int num)
{
  int i;

  city_styles = fc_calloc(num, sizeof(*city_styles));
  game.control.num_city_styles = num;

  for (i = 0; i < game.control.num_city_styles; i++) {
    requirement_vector_init(&city_styles[i].reqs);
  }
}

/**********************************************************************//**
  De-allocate the memory used by the city styles.
**************************************************************************/
void city_styles_free(void)
{
  int i;

  for (i = 0; i < game.control.num_city_styles; i++) {
    requirement_vector_free(&city_styles[i].reqs);
  }

  free(city_styles);
  city_styles = NULL;
  game.control.num_city_styles = 0;
}

/**********************************************************************//**
  Create virtual skeleton for a city.
  Values are mostly sane defaults.

  Always tile_set_owner(ptile, pplayer) sometime after this!
**************************************************************************/
struct city *create_city_virtual(struct player *pplayer,
                                 struct tile *ptile, const char *name)
{
  int i,len;

  /* Make sure that contents of city structure are correctly initialized,
   * if you ever allocate it by some other mean than fc_calloc() */
  struct city *pcity = fc_calloc(1, sizeof(*pcity));

  fc_assert_ret_val(NULL != name, NULL);        /* No unnamed cities! */

  /* Do this early, so any logging later will have the city name */
  city_name_set(pcity, name);

  pcity->tile = ptile;
  fc_assert_ret_val(NULL != pplayer, NULL);     /* No unowned cities! */
  pcity->owner = pplayer;
  pcity->acquire_t = CACQ_FOUNDED;

  if (is_server()) {
    pcity->original = pplayer;
  }

  /* City structure was allocated with fc_calloc(), so contents are initially
   * zero. There is no need to initialize it a second time. */

  /* Now set some useful default values. */
  pcity->capital = CAPITAL_NOT;
  city_size_set(pcity, 1);
  pcity->specialists[DEFAULT_SPECIALIST] = 1;
  pcity->wlcb = WLCB_SMART;

  output_type_iterate(o) {
    pcity->bonus[o] = 100;
    pcity->abs_bonus[o] = 0;
  } output_type_iterate_end;

  pcity->turn_plague = -1; /* -1 = never */
  pcity->did_buy = FALSE;
  pcity->city_radius_sq = game.info.init_city_radius_sq;
  pcity->turn_founded = game.info.turn;
  pcity->turn_last_built = game.info.turn;

  pcity->tile_cache_radius_sq = -1; /* -1 = tile_cache must be initialised */

  /* pcity->ai.act_cache: worker activities on the city map */

  /* Initialise improvements list */
  for (i = 0; i < ARRAY_SIZE(pcity->built); i++) {
    pcity->built[i].turn = I_NEVER;
  }

  /* Set up the worklist */
  worklist_init(&pcity->worklist);

  pcity->units_supported = unit_list_new();
  pcity->routes = trade_route_list_new();
  pcity->task_reqs = worker_task_list_new();

  if (is_server()) {
    pcity->server.mgr_score_calc_turn = -1; /* -1 = never */

    CALL_FUNC_EACH_AI(city_alloc, pcity);
  } else {
    pcity->client.info_units_supported =
        unit_list_new_full(unit_virtual_destroy);
    pcity->client.info_units_present =
        unit_list_new_full(unit_virtual_destroy);
    /* collecting_info_units_supported set by fc_calloc().
     * collecting_info_units_present set by fc_calloc(). */
  }

  len = counters_get_city_counters_count();
  pcity->counter_values = fc_malloc(sizeof(int) * len);

  for (i = 0; i < len; i++) {
    pcity->counter_values[i] = counter_by_index(i, CTGT_CITY)->def;
  }

  return pcity;
}

/**********************************************************************//**
  Removes the virtual skeleton of a city. You should already have removed
  all buildings and units you have added to the city before this.
**************************************************************************/
void destroy_city_virtual(struct city *pcity)
{
  CALL_FUNC_EACH_AI(city_free, pcity);

  citizens_free(pcity);

  /* Free worker tasks */
  while (worker_task_list_size(pcity->task_reqs) > 0) {
    struct worker_task *ptask = worker_task_list_get(pcity->task_reqs, 0);

    worker_task_list_remove(pcity->task_reqs, ptask);

    free(ptask);
  }
  worker_task_list_destroy(pcity->task_reqs);

  /* Free rally points */
  city_rally_point_clear(pcity);

  unit_list_destroy(pcity->units_supported);
  trade_route_list_destroy(pcity->routes);
  if (pcity->tile_cache != NULL) {
    free(pcity->tile_cache);
  }

  if (pcity->cm_parameter) {
    free(pcity->cm_parameter);
  }

  if (pcity->counter_values) {
    free(pcity->counter_values);
  }

  if (!is_server()) {
    unit_list_destroy(pcity->client.info_units_supported);
    unit_list_destroy(pcity->client.info_units_present);
    /* Handle a rare case where the game is freed in the middle of a
     * spy/diplomat investigate cycle. */
    if (pcity->client.collecting_info_units_supported != NULL) {
      unit_list_destroy(pcity->client.collecting_info_units_supported);
    }
    if (pcity->client.collecting_info_units_present != NULL) {
      unit_list_destroy(pcity->client.collecting_info_units_present);
    }
  }

  free(pcity->name);

  memset(pcity, 0, sizeof(*pcity)); /* ensure no pointers remain */
  free(pcity);
}

/**********************************************************************//**
  Check if city with given id still exist. Use this before using
  old city pointers when city might have disappeared.
**************************************************************************/
bool city_exist(int id)
{
  /* Check if city exist in game */
  if (game_city_by_number(id)) {
    return TRUE;
  }

  return FALSE;
}

/**********************************************************************//**
  Return TRUE if the city is a virtual city. That is, it is a valid city
  pointer but does not correspond to a city that exists in the game.

  NB: A return value of FALSE implies that either the pointer is NULL or
  that the city exists in the game.
**************************************************************************/
bool city_is_virtual(const struct city *pcity)
{
  if (!pcity) {
    return FALSE;
  }

  return pcity != game_city_by_number(pcity->id);
}

/**********************************************************************//**
  Return TRUE if the city is worked without using up a citizen.
**************************************************************************/
bool is_free_worked(const struct city *pcity, const struct tile *ptile)
{
  return is_city_center(pcity, ptile);
}

/**********************************************************************//**
  Return pointer to ai data of given city and ai type.
**************************************************************************/
void *city_ai_data(const struct city *pcity, const struct ai_type *ai)
{
  return pcity->server.ais[ai_type_number(ai)];
}

/**********************************************************************//**
  Attach ai data to city
**************************************************************************/
void city_set_ai_data(struct city *pcity, const struct ai_type *ai,
                      void *data)
{
  pcity->server.ais[ai_type_number(ai)] = data;
}

/**********************************************************************//**
  Clear rally point set for the city.
**************************************************************************/
void city_rally_point_clear(struct city *pcity)
{
  /* Free rally points */
  if (pcity->rally_point.length > 0) {
    pcity->rally_point.length = 0;
    pcity->rally_point.persistent = FALSE;
    pcity->rally_point.vigilant = FALSE;
    free(pcity->rally_point.orders);
    pcity->rally_point.orders = NULL;
  }
}

/**********************************************************************//**
  Fill city rally point from a packet.
**************************************************************************/
void city_rally_point_receive(const struct packet_city_rally_point *packet,
                              struct city *pcity)
{
  struct unit_order *checked_orders;
  const struct civ_map *nmap = &(wld.map);

  if (NULL == pcity) {
    /* Probably lost. */
    log_verbose("handle_city_rally_point() bad city number %d.",
                packet->id);
    return;
  }

  if (0 > packet->length || MAX_LEN_ROUTE < packet->length) {
    /* Shouldn't happen */
    log_error("city_rally_point_receive() invalid packet length %d (max %d)",
              packet->length, MAX_LEN_ROUTE);
    return;
  }

  pcity->rally_point.length = packet->length;

  if (packet->length == 0) {
    pcity->rally_point.vigilant = FALSE;
    pcity->rally_point.persistent = FALSE;
    if (pcity->rally_point.orders) {
      free(pcity->rally_point.orders);
      pcity->rally_point.orders = NULL;
    }
  } else {
    checked_orders = create_unit_orders(nmap, packet->length,
                                        packet->orders);
    if (!checked_orders) {
      pcity->rally_point.length = 0;
      log_error("invalid rally point orders for %s.",
                city_name_get(pcity));
      return;
    }

    pcity->rally_point.persistent = packet->persistent;
    pcity->rally_point.vigilant = packet->vigilant;
    pcity->rally_point.orders = checked_orders;
  }
}
