/********************************************************************** 
   Copyright (C) 1996 - 2004  The Freeciv Project
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

#include "map.h"
#include "rand.h"

#include "height_map.h"
#include "mapgen_topology.h"
#include "utilities.h" 

int *height_map = NULL;
int hmap_shore_level = 0, hmap_mountain_level = 0;

/****************************************************************************
  Lower the land near the polar region to avoid too much land there.

  See also renomalize_hmap_poles
****************************************************************************/
void normalize_hmap_poles(void)
{
  whole_map_iterate(x, y) {
    if (near_singularity(x, y)) {
      hmap(x, y) = 0;
    } else if (map_colatitude(x, y) < 2 * ICE_BASE_LEVEL) {
      hmap(x, y) *= map_colatitude(x, y) / (2.5 * ICE_BASE_LEVEL);
    } else if (map.separatepoles 
	       && map_colatitude(x, y) <= 2.5 * ICE_BASE_LEVEL) {
      hmap(x, y) *= 0.1;
    } else if (map_colatitude(x, y) <= 2.5 * ICE_BASE_LEVEL) {
      hmap(x, y) *= map_colatitude(x, y) / (2.5 * ICE_BASE_LEVEL);
    }
  } whole_map_iterate_end;
}

/****************************************************************************
  Invert the effects of normalize_hmap_poles so that we have accurate heights
  for texturing the poles.
****************************************************************************/
void renormalize_hmap_poles(void)
{
  whole_map_iterate(x, y) {
    if (hmap(x, y) == 0 || map_colatitude(x, y) == 0) {
      /* Nothing. */
    } else if (map_colatitude(x, y) < 2 * ICE_BASE_LEVEL) {
      hmap(x, y) *= (2.5 * ICE_BASE_LEVEL) / map_colatitude(x, y);
    } else if (map.separatepoles 
	       && map_colatitude(x, y) <= 2.5 * ICE_BASE_LEVEL) {
      hmap(x, y) *= 10;
    } else if (map_colatitude(x, y) <= 2.5 * ICE_BASE_LEVEL) {
      hmap(x, y) *= (2.5 * ICE_BASE_LEVEL) /  map_colatitude(x, y);
    }
  } whole_map_iterate_end;
}

/**********************************************************************
 Create uncorrelated rand map and do some call to smoth to correlate 
 it a little and creante randoms shapes
 **********************************************************************/
void make_random_hmap(int smooth)
{
  int i = 0;
  height_map = fc_malloc (sizeof(int) * MAX_MAP_INDEX);

  INITIALIZE_ARRAY(height_map, MAX_MAP_INDEX, myrand(1000 * smooth) );

  for (; i < smooth; i++) {
    smooth_int_map(height_map, TRUE);
  }

  adjust_int_map(height_map, hmap_max_level);
}

/**************************************************************************
  Recursive function which does the work for generator 5.

  All (x0,y0) and (x1,y1) are in native coordinates.
**************************************************************************/
static void gen5rec(int step, int x0, int y0, int x1, int y1)
{
  int val[2][2];
  int x1wrap = x1; /* to wrap correctly */ 
  int y1wrap = y1; 

  /* All x and y values are native. */

  if (((y1 - y0 <= 0) || (x1 - x0 <= 0)) 
      || ((y1 - y0 == 1) && (x1 - x0 == 1))) {
    return;
  }

  if (x1 == map.xsize)
    x1wrap = 0;
  if (y1 == map.ysize)
    y1wrap = 0;

  val[0][0] = hnat(x0, y0);
  val[0][1] = hnat(x0, y1wrap);
  val[1][0] = hnat(x1wrap, y0);
  val[1][1] = hnat(x1wrap, y1wrap);

  /* set midpoints of sides to avg of side's vertices plus a random factor */
  /* unset points are zero, don't reset if set */
#define set_midpoints(X, Y, V)						\
  do_in_map_pos(map_x, map_y, (X), (Y)) {                               \
    if (!near_singularity(map_x, map_y)					\
	&& map_colatitude(map_x, map_y) >  ICE_BASE_LEVEL/2		\
	&& hnat((X), (Y)) == 0) {					\
      hnat((X), (Y)) = (V);						\
    }									\
  } do_in_map_pos_end;

  set_midpoints((x0 + x1) / 2, y0,
		(val[0][0] + val[1][0]) / 2 + myrand(step) - step / 2);
  set_midpoints((x0 + x1) / 2,  y1wrap,
		(val[0][1] + val[1][1]) / 2 + myrand(step) - step / 2);
  set_midpoints(x0, (y0 + y1)/2,
		(val[0][0] + val[0][1]) / 2 + myrand(step) - step / 2);  
  set_midpoints(x1wrap,  (y0 + y1) / 2,
		(val[1][0] + val[1][1]) / 2 + myrand(step) - step / 2);  

  /* set middle to average of midpoints plus a random factor, if not set */
  set_midpoints((x0 + x1) / 2, (y0 + y1) / 2,
		((val[0][0] + val[0][1] + val[1][0] + val[1][1]) / 4
		 + myrand(step) - step / 2));

#undef set_midpoints

  /* now call recursively on the four subrectangles */
  gen5rec(2 * step / 3, x0, y0, (x1 + x0) / 2, (y1 + y0) / 2);
  gen5rec(2 * step / 3, x0, (y1 + y0) / 2, (x1 + x0) / 2, y1);
  gen5rec(2 * step / 3, (x1 + x0) / 2, y0, x1, (y1 + y0) / 2);
  gen5rec(2 * step / 3, (x1 + x0) / 2, (y1 + y0) / 2, x1, y1);
}

/**************************************************************************
Generator 5 makes earthlike worlds with one or more large continents and
a scattering of smaller islands. It does so by dividing the world into
blocks and on each block raising or lowering the corners, then the 
midpoints and middle and so on recursively.  Fiddling with 'xdiv' and 
'ydiv' will change the size of the initial blocks and, if the map does not 
wrap in at least one direction, fiddling with 'avoidedge' will change the 
liklihood of continents butting up to non-wrapped edges.

  All X and Y values used in this function are in native coordinates.
**************************************************************************/
void make_pseudofractal1_hmap(void)
{
  const bool xnowrap = !topo_has_flag(TF_WRAPX);
  const bool ynowrap = !topo_has_flag(TF_WRAPY);

  /* 
   * How many blocks should the x and y directions be divided into
   * initially. 
   */
  const int xdiv = 6;		
  const int ydiv = 5;

  int xdiv2 = xdiv + (xnowrap ? 1 : 0);
  int ydiv2 = ydiv + (ynowrap ? 1 : 0);

  int xmax = map.xsize - (xnowrap ? 1 : 0);
  int ymax = map.ysize - (ynowrap ? 1 : 0);
  int xn, yn;
  /* just need something > log(max(xsize, ysize)) for the recursion */
  int step = map.xsize + map.ysize; 
  /* edges are avoided more strongly as this increases */
  int avoidedge = (50 - map.landpercent) * step / 100 + step / 3; 

  height_map = fc_malloc(sizeof(int) * MAX_MAP_INDEX);

 /* initialize map */
  INITIALIZE_ARRAY(height_map, MAX_MAP_INDEX, 0);

  /* set initial points */
  for (xn = 0; xn < xdiv2; xn++) {
    for (yn = 0; yn < ydiv2; yn++) {
      do_in_map_pos(x, y, (xn * xmax / xdiv), (yn * ymax / ydiv)) {
	/* set initial points */
	hmap(x, y) = myrand(2 * step) - (2 * step) / 2;

	if (near_singularity(x, y)) {
	  /* avoid edges (topological singularities) */
	  hmap(x, y) -= avoidedge;
	}

	if (map_colatitude(x, y) <= ICE_BASE_LEVEL / 2 ) {
	  /* separate poles and avoid too much land at poles */
	  hmap(x, y) -= myrand(avoidedge);
	}
      } do_in_map_pos_end;
    }
  }

  /* calculate recursively on each block */
  for (xn = 0; xn < xdiv; xn++) {
    for (yn = 0; yn < ydiv; yn++) {
      gen5rec(step, xn * xmax / xdiv, yn * ymax / ydiv, 
	      (xn + 1) * xmax / xdiv, (yn + 1) * ymax / ydiv);
    }
  }

  /* put in some random fuzz */
  whole_map_iterate(x, y) {
    hmap(x, y) = 8 * hmap(x, y) + myrand(4) - 2;
  } whole_map_iterate_end;

  adjust_int_map(height_map, hmap_max_level);
}
