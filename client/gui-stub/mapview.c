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

#include "fcintl.h"
#include "game.h"
#include "support.h"

#include "civclient.h"
#include "climisc.h"
#include "control.h"
#include "mapctrl_common.h"

#include "mapctrl.h"
#include "mapview.h"

/* Mapview dimensions. */
int map_view_x0, map_view_y0;
int canvas_width, canvas_height;
int mapview_tile_width, mapview_tile_height;

/**************************************************************************
  This function can be used by mapview_common code to determine the
  location and dimensions of the mapview canvas.
**************************************************************************/
void get_mapview_dimensions(int *map_view_topleft_map_x,
			    int *map_view_topleft_map_y,
			    int *map_view_pixel_width,
			    int *map_view_pixel_height)
{
  /* PORTME */
  *map_view_topleft_map_x = map_view_x0;
  *map_view_topleft_map_y = map_view_y0;
  *map_view_pixel_width = canvas_width;
  *map_view_pixel_height = canvas_height;
}

/**************************************************************************
  Typically an info box is provided to tell the player about the state
  of their civilization.  This function is called when the label is
  changed.
**************************************************************************/
void update_info_label(void)
{
  /* PORTME */
  char buffer[512];

  my_snprintf(buffer, sizeof(buffer),
	      _("Population: %s\n"
		"Year: %s\n"
		"Gold %d\n"
		"Tax: %d Lux: %d Sci: %d"),
	      population_to_text(civ_population(game.player_ptr)),
	      textyear(game.year), game.player_ptr->economic.gold,
	      game.player_ptr->economic.tax,
	      game.player_ptr->economic.luxury,
	      game.player_ptr->economic.science);

  /* ... */
}

/**************************************************************************
  Update the information label which gives info on the current unit and
  the square under the current unit, for specified unit.  Note that in
  practice punit is always the focus unit.

  Clears label if punit is NULL.

  Typically also updates the cursor for the map_canvas (this is related
  because the info label may includes  "select destination" prompt etc).
  And it may call update_unit_pix_label() to update the icons for units
  on this square.
**************************************************************************/
void update_unit_info_label(struct unit *punit)
{
  /* PORTME */
}

/**************************************************************************
  Update the timeout in the client window.  The timeout is the time until
  the turn ends, in seconds.
**************************************************************************/
void update_timeout_label(void)
{
  /* PORTME */
  char buffer[512];

  if (game.timeout <= 0) {
    sz_strlcpy(buffer, Q_("?timeout:off"));
  } else {
    format_duration(buffer, sizeof(buffer), seconds_to_turndone);
  }

  /* ... */
}

/**************************************************************************
  If do_restore is FALSE it should change the turn button style (to draw
  the user's attention to it).  If do_restore is TRUE this should reset
  the turn done button to the default style.
**************************************************************************/
void update_turn_done_button(bool do_restore)
{
  static bool flip = FALSE;
  
  if (!get_turn_done_button_state()) {
    return;
  }

  if ((do_restore && flip) || !do_restore) {
    /* ... */

    flip = !flip;
  }
  /* PORTME */
}

/**************************************************************************
  Set information for the indicator icons typically shown in the main
  client window.  The parameters tell which sprite to use for the
  indicator.
**************************************************************************/
void set_indicator_icons(int bulb, int sol, int flake, int gov)
{
  /* PORTME */
}

/**************************************************************************
  Set the dimensions for the map overview, in map units (tiles).
  Typically each tile will be a 2x2 rectangle, although this may vary.
**************************************************************************/
void set_overview_dimensions(int map_width, int map_height)
{
  /* PORTME */
}

/**************************************************************************
  Update the tile for the given map position on the overview.
**************************************************************************/
void overview_update_tile(int map_x, int map_y)
{
  /* PORTME */
}

/**************************************************************************
  Center the mapview around (map_x, map_y).

  This function is almost identical between all GUIs.
**************************************************************************/
void center_tile_mapcanvas(int map_x, int map_y)
{
  base_center_tile_mapcanvas(map_x, map_y, &map_view_x0, &map_view_y0,
			     mapview_tile_width, mapview_tile_height);

  update_map_canvas_visible();
  update_map_canvas_scrollbars();
  refresh_overview_viewrect();

  if (hover_state == HOVER_GOTO || hover_state == HOVER_PATROL) {
    create_line_at_mouse_pos();
  }
}

/**************************************************************************
  Draw a description for the given city.  (canvas_x, canvas_y) is the
  canvas position of the city itself.
**************************************************************************/
void show_city_desc(struct city *pcity, int canvas_x, int canvas_y)
{
  /* PORTME */
}

/**************************************************************************
  Draw the given map tile at the given canvas position in non-isometric
  view.
**************************************************************************/
void put_one_tile(int map_x, int map_y, int canvas_x, int canvas_y)
{
  /* PORTME */
}

/**************************************************************************
  Draw some or all of a tile onto the mapview canvas.
**************************************************************************/
void gui_map_put_tile_iso(int map_x, int map_y,
			  int canvas_x, int canvas_y,
			  int offset_x, int offset_y, int offset_y_unit,
			  int width, int height, int height_unit,
			  enum draw_type draw)
{
  /* PORTME */
}

/**************************************************************************
  Draw some or all of a sprite onto the mapview or citydialog canvas.
**************************************************************************/
void gui_put_sprite(struct canvas_store *pcanvas_store,
		    int canvas_x, int canvas_y,
		    struct Sprite *sprite,
		    int offset_x, int offset_y, int width, int height)
{
  /* PORTME */
}

/**************************************************************************
  Flush the given part of the canvas buffer (if there is one) to the
  screen.
**************************************************************************/
void flush_mapcanvas(int canvas_x, int canvas_y,
		     int pixel_width, int pixel_height)
{
  /* PORTME */
}


/**************************************************************************
  Update (refresh) the locations of the mapview scrollbars (if it uses
  them).
**************************************************************************/
void update_map_canvas_scrollbars(void)
{
  /* PORTME */
}

/**************************************************************************
  Update (refresh) all of the city descriptions on the mapview.
**************************************************************************/
void update_city_descriptions(void)
{
  update_map_canvas_visible();
}

/**************************************************************************
  If necessary, clear the city descriptions out of the buffer.
**************************************************************************/
void prepare_show_city_descriptions(void)
{
  /* PORTME */
}

/**************************************************************************
  Draw a cross-hair overlay on a tile.
**************************************************************************/
void put_cross_overlay_tile(int map_x, int map_y)
{
  /* PORTME */
}

/**************************************************************************
  Draw in information about city workers on the mapview in the given
  color.
**************************************************************************/
void put_city_workers(struct city *pcity, int color)
{
  /* PORTME */
}

/**************************************************************************
  Draw a single frame of animation.  This function needs to clear the old
  image and draw the new one.  It must flush output to the display.
**************************************************************************/
void draw_unit_animation_frame(struct unit *punit,
			       bool first_frame, bool last_frame,
			       int old_canvas_x, int old_canvas_y,
			       int new_canvas_x, int new_canvas_y)
{
  /* PORTME */
}

/**************************************************************************
 This function is called to decrease a unit's HP smoothly in battle when
 combat_animation is turned on.
**************************************************************************/
void
decrease_unit_hp_smooth(struct unit *punit0, int hp0,
			struct unit *punit1, int hp1)
{
  /* PORTME */
}

/**************************************************************************
  Draw a nuke mushroom cloud at the given tile.
**************************************************************************/
void put_nuke_mushroom_pixmaps(int map_x, int map_y)
{
  /* PORTME */
}

/**************************************************************************
  Refresh (update) the entire map overview.
**************************************************************************/
void refresh_overview_canvas(void)
{
  /* PORTME */
}

/**************************************************************************
  Refresh (update) the viewrect on the overview. This is the rectangle
  showing the area covered by the mapview.
**************************************************************************/
void refresh_overview_viewrect(void)
{
  /* PORTME */
}

/**************************************************************************
  Draw a segment (e.g., a goto line) from the given tile in the given
  direction.
**************************************************************************/
void draw_segment(int src_x, int src_y, int dir)
{
  /* PORTME */
}

/**************************************************************************
  This function is called when the tileset is changed.
**************************************************************************/
void tileset_changed(void)
{
  /* PORTME */
  /* Here you should do any necessary redraws (for instance, the city
   * dialogs usually need to be resized). */
}
