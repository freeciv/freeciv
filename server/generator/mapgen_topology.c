/********************************************************************** 
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
#include <config.h>
#endif

#include <math.h> /* sqrt */

/* utility */
#include "log.h"

/* common */
#include "game.h"
#include "map.h"

#include "mapgen_topology.h"

int ice_base_colatitude = 0 ;

/****************************************************************************
  Returns the colatitude of this map position.  This is a value in the
  range of 0 to MAX_COLATITUDE (inclusive).
  This function is wanted to concentrate the topology information
  all generator code has to use  colatitude and others topology safe 
  functions instead (x,y) coordinate to place terrains
  colatitude is 0 at poles and MAX_COLATITUDE at equator
****************************************************************************/
int map_colatitude(const struct tile *ptile)
{
  double x, y;

  fc_assert_ret_val(ptile != NULL, MAX_COLATITUDE / 2);

  if (map.server.alltemperate) {
    /* An all-temperate map has "average" temperature everywhere.
     *
     * TODO: perhaps there should be a random temperature variation. */
    return MAX_COLATITUDE / 2;
  }

  do_in_natural_pos(ntl_x, ntl_y, ptile->x, ptile->y) {
    if (!topo_has_flag(TF_WRAPX) && !topo_has_flag(TF_WRAPY)) {
      /* A FLAT (unwrapped) map 
       *
       * We assume this is a partial planetary map.  A polar zone is placed
       * at the top and the equator is at the bottom.  The user can specify
       * all-temperate to avoid this. */
      return MAX_COLATITUDE * ntl_y / (NATURAL_HEIGHT - 1);
    }

    /* Otherwise a wrapping map is assumed to be a global planetary map. */

    /* we fold the map to get the base symetries
     *
     * ...... 
     * :c__c:
     * :____:
     * :____:
     * :c__c:
     * ......
     *
     * C are the corners.  In all cases the 4 corners have equal temperature.
     * So we fold the map over in both directions and determine
     * x and y vars in the range [0.0, 1.0].
     *
     * ...>x 
     * :C_
     * :__
     * V
     * y
     *
     * And now this is what we have - just one-quarter of the map.
     */
    x = ((ntl_x > (NATURAL_WIDTH / 2 - 1)
	 ? NATURAL_WIDTH - 1.0 - (double)ntl_x
	 : (double)ntl_x)
	 / (NATURAL_WIDTH / 2 - 1));
    y = ((ntl_y > (NATURAL_HEIGHT / 2 - 1)
	  ? NATURAL_HEIGHT - 1.0 - (double)ntl_y
	  : (double)ntl_y)
	 / (NATURAL_HEIGHT / 2 - 1));
  } do_in_natural_pos_end;

  if (topo_has_flag(TF_WRAPX) && !topo_has_flag(TF_WRAPY)) {
    /* In an Earth-like topology the polar zones are at north and south.
     * This is equivalent to a Mercator projection. */
    return MAX_COLATITUDE * y;
  }
  
  if (!topo_has_flag(TF_WRAPX) && topo_has_flag(TF_WRAPY)) {
    /* In a Uranus-like topology the polar zones are at east and west.
     * This isn't really the way Uranus is; it's the way Earth would look
     * if you tilted your head sideways.  It's still a Mercator
     * projection. */
    return MAX_COLATITUDE * x;
  }

  /* Otherwise we have a torus topology.  We set it up as an approximation
   * of a sphere with two circular polar zones and a square equatorial
   * zone.  In this case north and south are not constant directions on the
   * map because we have to use a more complicated (custom) projection.
   *
   * Generators 2 and 5 work best if the center of the map is free.  So
   * we want to set up the map with the poles (N,S) along the sides and the
   * equator (/,\) in between.
   *
   * ........
   * :\ NN /:
   * : \  / :
   * :S \/ S:
   * :S /\ S:
   * : /  \ :
   * :/ NN \:
   * ''''''''
   */

  /* Remember that we've already folded the map into fourths:
   *
   * ....
   * :\ N
   * : \
   * :S \
   *
   * Now flip it along the X direction to get this:
   *
   * ....
   * :N /
   * : /
   * :/ S
   */
  x = 1.0 - x;

  /* Since the north and south poles are equivalent, we can fold along the
   * diagonal.  This leaves us with 1/8 of the map
   *
   * .....
   * :P /
   * : /
   * :/
   *
   * where P is the polar regions and / is the equator. */
  if (x + y > 1.0) {
    x = 1.0 - x;
    y = 1.0 - y;
  }

  /* This projection makes poles with a shape of a quarter-circle along
   * "P" and the equator as a straight line along "/".
   *
   * This is explained more fully in RT 8624; the discussion can be found at
   * http://thread.gmane.org/gmane.games.freeciv.devel/42648 */
  return MAX_COLATITUDE * (1.5 * (x * x * y + x * y * y)
                           - 0.5 * (x * x * x + y * y * y)
                           + 1.5 * (x * x + y * y));
}

/****************************************************************************
  Return TRUE if the map in a city radius is SINGULAR.  This is used to
  avoid putting (non-polar) land near the edge of the map.
****************************************************************************/
bool near_singularity(const struct tile *ptile)
{
  return is_singular_tile(ptile, CITY_MAP_MAX_RADIUS);
}


/****************************************************************************
  Set the map xsize and ysize based on a base size and ratio (in natural
  coordinates).
****************************************************************************/
static void set_sizes(double size, int Xratio, int Yratio)
{
  /* Some code in generator assumes even dimension, so this is set to 2.
   * Future topologies may also require even dimensions. */
  /* The generator works also for odd values; keep this here for autogenerated
   * height and width. */
  const int even = 2;

  /* In iso-maps we need to double the map.ysize factor, since xsize is
   * in native coordinates which are compressed 2x in the X direction. */ 
  const int iso = MAP_IS_ISOMETRIC ? 2 : 1;

  /* We have:
   *
   *   1000 * size = xsize * ysize
   *
   * And to satisfy the ratios and other constraints we set
   *
   *   xsize = i_size * xratio * even
   *   ysize = i_size * yratio * even * iso
   *
   * For any value of "i_size".  So with some substitution
   *
   *   1000 * size = i_size * i_size * xratio * yratio * even * even * iso
   *   i_size = sqrt(1000 * size / (xratio * yratio * even * even * iso))
   * 
   * Make sure to round off i_size to preserve exact wanted ratios,
   * that may be importante for some topologies.
   */
  const int i_size
    = sqrt((float)(1000 * size)
	   / (float)(Xratio * Yratio * iso * even * even)) + 0.49;

  /* Now build xsize and ysize value as described above. */
  map.xsize = Xratio * i_size * even;
  map.ysize = Yratio * i_size * even * iso;

  /* Now make sure the size isn't too large for this ratio or the overall map
   * size (MAP_INDEX_SIZE) is larger than the maximal allowed size
   * (MAP_MAX_SIZE * 1000). If it is then decrease the size and try again. */
  if (MAX(MAP_WIDTH, MAP_HEIGHT) > MAP_MAX_LINEAR_SIZE
      || MAP_INDEX_SIZE > MAP_MAX_SIZE * 1000) {
    fc_assert(size > 0.1);
    set_sizes(size - 0.1, Xratio, Yratio);
    return;
  }

  /* If the ratio is too big for some topology the simplest way to avoid
   * this error is to set the maximum size smaller for all topologies! */
  if (map.server.size > size + 0.9) {
    /* Warning when size is set uselessly big */ 
    log_error("Requested size of %d is too big for this topology.",
              map.server.size);
  }

  /* xsize and ysize must be between MAP_MIN_LINEAR_SIZE and
   * MAP_MAX_LINEAR_SIZE. */
  map.xsize = CLIP(MAP_MIN_LINEAR_SIZE, map.xsize, MAP_MAX_LINEAR_SIZE);
  map.ysize = CLIP(MAP_MIN_LINEAR_SIZE, map.ysize, MAP_MAX_LINEAR_SIZE);

  log_normal(_("Creating a map of size %d x %d = %d tiles (%d requested)."),
             map.xsize, map.ysize, map.xsize * map.ysize,
             map.server.size * 1000);
}

/*
 * The default ratios for known topologies
 *
 * The factor Xratio * Yratio determines the accuracy of the size.
 * Small ratios work better than large ones; 3:2 is not the same as 6:4
 */
#define AUTO_RATIO_FLAT           {1, 1}
#define AUTO_RATIO_CLASSIC        {3, 2} 
#define AUTO_RATIO_URANUS         {2, 3} 
#define AUTO_RATIO_TORUS          {1, 1}

/***************************************************************************
  This function sets sizes in a topology-specific way then calls
  map_init_topology. Set 'autosize' to TRUE if the xsize/ysize should be
  calculated.
***************************************************************************/
void generator_init_topology(bool autosize)
{
  int sqsize, map_size;

  /* The server behavior to create the map is defined by 'map.server.mapsize'.
   * Calculate the xsize/ysize if it is not directly defined. */
  if (autosize) {
    /* Changing or reordering the topo_flag enum will break this code. */
    const int default_ratios[4][2] =
      {AUTO_RATIO_FLAT, AUTO_RATIO_CLASSIC,
       AUTO_RATIO_URANUS, AUTO_RATIO_TORUS};
    const int id = 0x3 & map.topology_id;

    /* see map.h for the definitions of TF_WRAP(X|Y) */
    fc_assert(TF_WRAPX == 0x1 && TF_WRAPY == 0x2);

    switch (map.server.mapsize) {
    case MAPSIZE_XYSIZE:
      map.server.size = (float)(map.xsize * map.ysize) / 1000.0 + 0.5;
      log_normal(_("Creating a map of size %d x %d = %d tiles (map size: "
                   "%d)."), map.xsize, map.ysize, map.xsize * map.ysize,
                 map.server.size);
      break;

    case MAPSIZE_PLAYER:
      map_size = player_count() * map.server.tilesperplayer
                 / map.server.landpercent / 10;

      map.server.size = CLIP(MAP_MIN_SIZE, map_size, MAP_MAX_SIZE);

      if (map_size < MAP_MIN_SIZE) {
        log_normal(_("Map size calculated for %d (land) tiles per player "
                     "and %d player(s) too small. Setting map size to the "
                     "minimal size %d."), map.server.tilesperplayer,
                   player_count(), map.server.size);
      } else if (map_size > MAP_MAX_SIZE) {
        log_normal(_("Map size calculated for %d (land) tiles per player "
                     "and %d player(s) too large. Setting map size to the "
                     "maximal size %d."), map.server.tilesperplayer,
                   player_count(), map.server.size);
      } else {
        log_normal(_("Setting map size to %d (approx. %d (land) tiles for "
                   "each of the %d player(s))."), map.server.size,
                   map.server.tilesperplayer, player_count());
      }
      /* no break */

    case MAPSIZE_FULLSIZE:
      /* Set map.xsize and map.ysize based on map.size. */
      set_sizes(map.server.size, default_ratios[id][0], default_ratios[id][1]);
      break;
    }
  }

  sqsize = get_sqsize();

  /* initialize the ICE_BASE_LEVEL */

  /* if maps has strip like poles we get smaller poles
   * (less playables than island poles)
   *   5% for little maps
   *   2% for big ones, if map.server.temperature == 50
   * exept if separate poles is set */
  if (map.server.separatepoles) {
    /* with separatepoles option strip poles are useless */
    ice_base_colatitude =
        (MAX(0, 100 * COLD_LEVEL / 3 - 1 *  MAX_COLATITUDE)
         + 1 *  MAX_COLATITUDE * sqsize) / (100 * sqsize);
  } else {
    /* any way strip poles are not so playable as isle poles */
    ice_base_colatitude =
        (MAX(0, 100 * COLD_LEVEL / 3 - 2 * MAX_COLATITUDE)
         + 2 * MAX_COLATITUDE * sqsize) / (100 * sqsize);
  }

  /* correction for single pole (Flat Earth) */
  if (!topo_has_flag(TF_WRAPX) && !topo_has_flag(TF_WRAPY)) {
    ice_base_colatitude /= 2;
  }

  map_init_topology(TRUE);
}

/*************************************************************************** 
  An estimate of the linear (1-dimensional) size of the map.
***************************************************************************/
int get_sqsize(void)
{
  int sqsize = sqrt(MAP_INDEX_SIZE / 1000);

  return MAX(1, sqsize);
}
