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

#include "mapview.h"

/****************************************************************************
  Typically an info box is provided to tell the player about the state
  of their civilization.  This function is called when the label is
  changed.
****************************************************************************/
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

/****************************************************************************
  Update the information label which gives info on the current unit and
  the square under the current unit, for specified unit.  Note that in
  practice punit is always the focus unit.

  Clears label if punit is NULL.

  Typically also updates the cursor for the map_canvas (this is related
  because the info label may includes  "select destination" prompt etc).
  And it may call update_unit_pix_label() to update the icons for units
  on this square.
****************************************************************************/
void update_unit_info_label(struct unit *punit)
{
  /* PORTME */
}

/****************************************************************************
  Update the timeout in the client window.  The timeout is the time until
  the turn ends, in seconds.
****************************************************************************/
void update_timeout_label(void)
{
  /* PORTME */
    
  /* set some widget based on get_timeout_label_text() */
}

/****************************************************************************
  If do_restore is FALSE it should change the turn button style (to draw
  the user's attention to it).  If do_restore is TRUE this should reset
  the turn done button to the default style.
****************************************************************************/
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

/****************************************************************************
  Set information for the indicator icons typically shown in the main
  client window.  The parameters tell which sprite to use for the
  indicator.
****************************************************************************/
void set_indicator_icons(int bulb, int sol, int flake, int gov)
{
  /* PORTME */
}

/****************************************************************************
  Called when the map size changes. This may be used to change the
  size of the GUI element holding the overview canvas. The
  overview.width and overview.height are updated if this function is
  called.
****************************************************************************/
void map_size_changed(void)
{
  /* PORTME */
}

/****************************************************************************
  Draw a description for the given city.  This description may include the
  name, turns-to-grow, production, and city turns-to-build (depending on
  client options).

  (canvas_x, canvas_y) gives the location on the given canvas at which to
  draw the description.  This is the location of the city itself so the
  text must be drawn underneath it.  pcity gives the city to be drawn,
  while (*width, *height) should be set by show_ctiy_desc to contain the
  width and height of the text block (centered directly underneath the
  city's tile).
****************************************************************************/
void show_city_desc(struct canvas *pcanvas, int canvas_x, int canvas_y,
		    struct city *pcity, int *width, int *height)
{
  /* PORTME */
}

/****************************************************************************
  Create a canvas of the given size.
****************************************************************************/
struct canvas *canvas_create(int width, int height)
{
  /* PORTME */
  return NULL;
}

/****************************************************************************
  Free any resources associated with this canvas and the canvas struct
  itself.
****************************************************************************/
void canvas_free(struct canvas *store)
{
  /* PORTME */
}

/****************************************************************************
  Draw some or all of a sprite onto the mapview or citydialog canvas.
****************************************************************************/
void canvas_put_sprite(struct canvas *pcanvas,
		    int canvas_x, int canvas_y,
		    struct Sprite *sprite,
		    int offset_x, int offset_y, int width, int height)
{
  /* PORTME */
}

/****************************************************************************
  Draw a full sprite onto the mapview or citydialog canvas.
****************************************************************************/
void canvas_put_sprite_full(struct canvas *pcanvas,
			 int canvas_x, int canvas_y,
			 struct Sprite *sprite)
{
  /* PORTME */
}

/****************************************************************************
  Draw a full sprite onto the canvas.  If "fog" is specified draw it with
  fog.
****************************************************************************/
void canvas_put_sprite_fogged(struct canvas *pcanvas,
			      int canvas_x, int canvas_y,
			      struct Sprite *psprite,
			      bool fog, int fog_x, int fog_y)
{
  /* PORTME */
}

/****************************************************************************
  Draw a filled-in colored rectangle onto the mapview or citydialog canvas.
****************************************************************************/
void canvas_put_rectangle(struct canvas *pcanvas,
		       enum color_std color,
		       int canvas_x, int canvas_y, int width, int height)
{
  /* PORTME */
}

/****************************************************************************
  Fill the area covered by the sprite with the given color.
****************************************************************************/
void canvas_fill_sprite_area(struct canvas *pcanvas,
			     struct Sprite *psprite, enum color_std color,
			     int canvas_x, int canvas_y)
{
  /* PORTME */
}

/****************************************************************************
  Fill the area covered by the sprite with the given color.
****************************************************************************/
void canvas_fog_sprite_area(struct canvas *pcanvas, struct Sprite *psprite,
			    int canvas_x, int canvas_y)
{
  /* PORTME */
}

/****************************************************************************
  Draw a 1-pixel-width colored line onto the mapview or citydialog canvas.
****************************************************************************/
void canvas_put_line(struct canvas *pcanvas, enum color_std color,
		  enum line_type ltype, int start_x, int start_y,
		  int dx, int dy)
{
  /* PORTME */
}

/****************************************************************************
  Copies an area from the source canvas to the destination canvas.
****************************************************************************/
void canvas_copy(struct canvas *dest, struct canvas *src,
		     int src_x, int src_y, int dest_x, int dest_y, int width,
		     int height)
{
  /* PORTME */
}

/****************************************************************************
  Return a canvas that is the overview window.
****************************************************************************/
struct canvas *get_overview_window(void)
{
  /* PORTME */
  return NULL;
}

/****************************************************************************
  Flush the given part of the canvas buffer (if there is one) to the
  screen.
****************************************************************************/
void flush_mapcanvas(int canvas_x, int canvas_y,
		     int pixel_width, int pixel_height)
{
  /* PORTME */
}

/****************************************************************************
  Mark the rectangular region as 'dirty' so that we know to flush it
  later.
****************************************************************************/
void dirty_rect(int canvas_x, int canvas_y,
		int pixel_width, int pixel_height)
{
  /* PORTME */
}

/****************************************************************************
  Mark the entire screen area as "dirty" so that we can flush it later.
****************************************************************************/
void dirty_all(void)
{
  /* PORTME */
}

/****************************************************************************
  Flush all regions that have been previously marked as dirty.  See
  dirty_rect and dirty_all.  This function is generally called after we've
  processed a batch of drawing operations.
****************************************************************************/
void flush_dirty(void)
{
  /* PORTME */
}

/****************************************************************************
  Do any necessary synchronization to make sure the screen is up-to-date.
  The canvas should have already been flushed to screen via flush_dirty -
  all this function does is make sure the hardware has caught up.
****************************************************************************/
void gui_flush(void)
{
  /* PORTME */
}

/****************************************************************************
  Update (refresh) the locations of the mapview scrollbars (if it uses
  them).
****************************************************************************/
void update_map_canvas_scrollbars(void)
{
  /* PORTME */
}

/****************************************************************************
  Update the size of the sliders on the scrollbars.
****************************************************************************/
void update_map_canvas_scrollbars_size(void)
{
  /* PORTME */
}

/****************************************************************************
  Update (refresh) all of the city descriptions on the mapview.
****************************************************************************/
void update_city_descriptions(void)
{
  update_map_canvas_visible();
}

/****************************************************************************
  If necessary, clear the city descriptions out of the buffer.
****************************************************************************/
void prepare_show_city_descriptions(void)
{
  /* PORTME */
}

/****************************************************************************
  Draw a cross-hair overlay on a tile.
****************************************************************************/
void put_cross_overlay_tile(struct tile *ptile)
{
  /* PORTME */
}

/****************************************************************************
  Draw a single tile of the citymap onto the mapview.  The tile is drawn
  as the given color with the given worker on it.  The exact method of
  drawing is left up to the GUI.
****************************************************************************/
void put_city_worker(struct canvas *pcanvas,
		     enum color_std color, enum city_tile_type worker,
		     int canvas_x, int canvas_y)
{
  /* PORTME */
}

/****************************************************************************
 Area Selection
****************************************************************************/
void draw_selection_rectangle(int canvas_x, int canvas_y, int w, int h)
{
  /* PORTME */
}

/****************************************************************************
  This function is called when the tileset is changed.
****************************************************************************/
void tileset_changed(void)
{
  /* PORTME */
  /* Here you should do any necessary redraws (for instance, the city
   * dialogs usually need to be resized). */
}
