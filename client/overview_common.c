/**********************************************************************
 Freeciv - Copyright (C) 1996-2005 - Freeciv Development Team
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

#include "log.h"

#include "civclient.h" /* can_client_change_view() */
#include "climap.h"
#include "control.h"
#include "options.h"
#include "overview_common.h"

#include "mapview_g.h"

int OVERVIEW_TILE_SIZE = 2;
struct overview overview;

/*
 * Set to TRUE if the backing store is more recent than the version
 * drawn into overview.window.
 */
static bool overview_dirty = FALSE;

/****************************************************************************
  Return color for overview map tile.
****************************************************************************/
static enum color_std overview_tile_color(struct tile *ptile)
{
  struct unit *punit;
  struct city *pcity;

  if (tile_get_known(ptile) == TILE_UNKNOWN) {
    return COLOR_STD_BLACK;
  } else if ((pcity = map_get_city(ptile))) {
    if (pcity->owner == game.player_idx) {
      return COLOR_STD_WHITE;
    } else {
      return COLOR_STD_CYAN;
    }
  } else if ((punit = find_visible_unit(ptile))) {
    if (punit->owner == game.player_idx) {
      return COLOR_STD_YELLOW;
    } else {
      return COLOR_STD_RED;
    }
  } else if (is_ocean(ptile->terrain)) {
    if (tile_get_known(ptile) == TILE_KNOWN_FOGGED && draw_fog_of_war) {
      return COLOR_STD_RACE4;
    } else {
      return COLOR_STD_OCEAN;
    }
  } else {
    if (tile_get_known(ptile) == TILE_KNOWN_FOGGED && draw_fog_of_war) {
      return COLOR_STD_BACKGROUND;
    } else {
      return COLOR_STD_GROUND;
    }
  }
}

/**************************************************************************
  Copies the overview image from the backing store to the window and
  draws the viewrect on top of it.
**************************************************************************/
static void redraw_overview(void)
{
  struct canvas *dest = get_overview_window();

  if (!dest || !overview.store) {
    return;
  }

  {
    struct canvas *src = overview.store;
    int x = overview.map_x0 * OVERVIEW_TILE_SIZE;
    int y = overview.map_y0 * OVERVIEW_TILE_SIZE;
    int ix = overview.width - x;
    int iy = overview.height - y;

    canvas_copy(dest, src, 0, 0, ix, iy, x, y);
    canvas_copy(dest, src, 0, y, ix, 0, x, iy);
    canvas_copy(dest, src, x, 0, 0, iy, ix, y);
    canvas_copy(dest, src, x, y, 0, 0, ix, iy);
  }

  {
    int i;
    int x[4], y[4];

    get_mapview_corners(x, y);

    for (i = 0; i < 4; i++) {
      int src_x = x[i];
      int src_y = y[i];
      int dest_x = x[(i + 1) % 4];
      int dest_y = y[(i + 1) % 4];

      canvas_put_line(dest, COLOR_STD_WHITE, LINE_NORMAL, src_x, src_y,
		      dest_x - src_x, dest_y - src_y);
    }
  }

  overview_dirty = FALSE;
}

/****************************************************************************
  Mark the overview as "dirty" so that it will be redrawn soon.
****************************************************************************/
static void dirty_overview(void)
{
  overview_dirty = TRUE;
}

/****************************************************************************
  Redraw the overview if it is "dirty".
****************************************************************************/
void flush_dirty_overview(void)
{
  /* Currently this function is called from mapview_common.  However it
   * should be made static eventually. */
  if (overview_dirty) {
    redraw_overview();
  }
}

/**************************************************************************
  Center the overview around the mapview.
**************************************************************************/
void center_tile_overviewcanvas(struct tile *ptile)
{
  /* The overview coordinates are equivalent to (scaled) natural
   * coordinates. */
  do_in_natural_pos(ntl_x, ntl_y, ptile->x, ptile->y) {
    /* NOTE: this embeds the map wrapping in the overview code.  This is
     * basically necessary for the overview to be efficiently
     * updated. */
    if (topo_has_flag(TF_WRAPX)) {
      overview.map_x0 = FC_WRAP(ntl_x - NATURAL_WIDTH / 2, NATURAL_WIDTH);
    } else {
      overview.map_x0 = 0;
    }
    if (topo_has_flag(TF_WRAPY)) {
      overview.map_y0 = FC_WRAP(ntl_y - NATURAL_HEIGHT / 2, NATURAL_HEIGHT);
    } else {
      overview.map_y0 = 0;
    }
    redraw_overview();
  } do_in_natural_pos_end;
}

/**************************************************************************
  Finds the overview (canvas) coordinates for a given map position.
**************************************************************************/
void map_to_overview_pos(int *overview_x, int *overview_y,
			 int map_x, int map_y)
{
  /* The map position may not be normal, for instance when the mapview
   * origin is not a normal position.
   *
   * NOTE: this embeds the map wrapping in the overview code. */
  do_in_natural_pos(ntl_x, ntl_y, map_x, map_y) {
    int ovr_x = ntl_x - overview.map_x0, ovr_y = ntl_y - overview.map_y0;

    if (topo_has_flag(TF_WRAPX)) {
      ovr_x = FC_WRAP(ovr_x, NATURAL_WIDTH);
    } else {
      if (MAP_IS_ISOMETRIC) {
	/* HACK: For iso-maps that don't wrap in the X direction we clip
	 * a half-tile off of the left and right of the overview.  This
	 * means some tiles only are halfway shown.  However it means we
	 * don't show any unreal tiles, which we'd otherwise be doing.  The
	 * rest of the code can't handle unreal tiles in the overview. */
	ovr_x--;
      }
    }
    if (topo_has_flag(TF_WRAPY)) {
      ovr_y = FC_WRAP(ovr_y, NATURAL_HEIGHT);
    }
    *overview_x = OVERVIEW_TILE_SIZE * ovr_x;
    *overview_y = OVERVIEW_TILE_SIZE * ovr_y;
  } do_in_natural_pos_end;
}

/**************************************************************************
  Finds the map coordinates for a given overview (canvas) position.
**************************************************************************/
void overview_to_map_pos(int *map_x, int *map_y,
			 int overview_x, int overview_y)
{
  int ntl_x = overview_x / OVERVIEW_TILE_SIZE + overview.map_x0;
  int ntl_y = overview_y / OVERVIEW_TILE_SIZE + overview.map_y0;

  if (MAP_IS_ISOMETRIC && !topo_has_flag(TF_WRAPX)) {
    /* Clip half tile left and right.  See comment in map_to_overview_pos. */
    ntl_x++;
  }

  NATURAL_TO_MAP_POS(map_x, map_y, ntl_x, ntl_y);
  if (!normalize_map_pos(map_x, map_y)) {
    /* All positions on the overview should be valid. */
    assert(FALSE);
  }
}

/**************************************************************************
  Redraw the entire backing store for the overview minimap.
**************************************************************************/
void refresh_overview_canvas(void)
{
  whole_map_iterate(ptile) {
    overview_update_tile(ptile);
  } whole_map_iterate_end;
  redraw_overview();
}

/**************************************************************************
  Redraw the given map position in the overview canvas.
**************************************************************************/
void overview_update_tile(struct tile *ptile)
{
  /* Base overview positions are just like natural positions, but scaled to
   * the overview tile dimensions. */
  do_in_natural_pos(ntl_x, ntl_y, ptile->x, ptile->y) {
    int overview_y = ntl_y * OVERVIEW_TILE_SIZE;
    int overview_x = ntl_x * OVERVIEW_TILE_SIZE;

    if (MAP_IS_ISOMETRIC) {
      if (topo_has_flag(TF_WRAPX)) {
	if (overview_x > overview.width - OVERVIEW_TILE_WIDTH) {
	  /* This tile is shown half on the left and half on the right
	   * side of the overview.  So we have to draw it in two parts. */
	  canvas_put_rectangle(overview.store, 
			       overview_tile_color(ptile),
			       overview_x - overview.width, overview_y,
			       OVERVIEW_TILE_WIDTH, OVERVIEW_TILE_HEIGHT); 
	}     
      } else {
	/* Clip half tile left and right.
	 * See comment in map_to_overview_pos. */
	overview_x -= OVERVIEW_TILE_SIZE;
      }
    } 
    
    canvas_put_rectangle(overview.store,
			 overview_tile_color(ptile),
			 overview_x, overview_y,
			 OVERVIEW_TILE_WIDTH, OVERVIEW_TILE_HEIGHT);

    dirty_overview();
  } do_in_natural_pos_end;
}

/**************************************************************************
  Called if the map size is know or changes.
**************************************************************************/
void calculate_overview_dimensions(void)
{
  int w, h;
  int xfact = MAP_IS_ISOMETRIC ? 2 : 1;

  static int recursion = 0; /* Just to be safe. */

  /* Clip half tile left and right.  See comment in map_to_overview_pos. */
  int shift = (MAP_IS_ISOMETRIC && !topo_has_flag(TF_WRAPX)) ? -1 : 0;

  if (recursion > 0 || map.xsize <= 0 || map.ysize <= 0) {
    return;
  }
  recursion++;

  get_overview_area_dimensions(&w, &h);

  freelog(LOG_DEBUG, "Map size %d,%d - area size %d,%d",
	  map.xsize, map.ysize, w, h);

  /* Set the scale of the overview map.  This attempts to limit the
   * overview to the size of the area available.
   *
   * It rounds up since this gives good results with the default settings.
   * It may need tweaking if the panel resizes itself. */
  OVERVIEW_TILE_SIZE = MIN((w - 1) / (map.xsize * xfact) + 1,
			   (h - 1) / map.ysize + 1);
  OVERVIEW_TILE_SIZE = MAX(OVERVIEW_TILE_SIZE, 1);

  overview.width
    = OVERVIEW_TILE_WIDTH * map.xsize + shift * OVERVIEW_TILE_SIZE; 
  overview.height = OVERVIEW_TILE_HEIGHT * map.ysize;

  if (overview.store) {
    canvas_free(overview.store);
  }
  overview.store = canvas_create(overview.width, overview.height);
  canvas_put_rectangle(overview.store, COLOR_STD_BLACK,
		       0, 0, overview.width, overview.height);
  update_map_canvas_scrollbars_size();

  /* Call gui specific function. */
  overview_size_changed();

  if (can_client_change_view()) {
    refresh_overview_canvas();
  }

  recursion--;
}
