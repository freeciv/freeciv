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
#include <stdio.h>
#include <stdlib.h>

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Scrollbar.h>

#include "canvas.h"
#include "pixcomm.h"

#include "fcintl.h"
#include "game.h"
#include "government.h"		/* government_graphic() */
#include "map.h"
#include "player.h"
#include "rand.h"
#include "support.h"
#include "timing.h"
#include "unit.h"

#include "civclient.h"
#include "climisc.h"
#include "colors.h"
#include "control.h" /* set_unit_focus_no_center and get_unit_in_focus */
#include "goto.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapctrl.h"
#include "options.h"
#include "tilespec.h"

#include "mapview.h"

/*
The bottom row of the map was sometimes hidden.

As of now the top left corner is always aligned with the tiles. This is what
causes the problem in the first place. The ideal solution would be to align the
window with the bottom left tiles if you tried to center the window on a tile
closer than (screen_tiles_height/2 -1) to the south pole.

But, for now, I just grepped for occurences where the ysize (or the values
derived from it) were used, and those places that had relevance to drawing the
map, and I added 1 (using the EXTRA_BOTTOM_ROW constant).

-Thue
*/
#define EXTRA_BOTTOM_ROW 1

/* contains the x0, y0 coordinates of the upper left corner block */
int map_view_x0, map_view_y0;

/* used by map_canvas expose func */ 
int force_full_repaint;

static void pixmap_put_overlay_tile(Pixmap pixmap, int x, int y,
 				    struct Sprite *ssprite);
static void show_city_descriptions(void);
static void put_line(Pixmap pm, int x, int y, int dir);

/* the intro picture is held in this pixmap, which is scaled to
   the screen size */
Pixmap scaled_intro_pixmap;
int scaled_intro_pixmap_width, scaled_intro_pixmap_height;

/**************************************************************************
Finds the pixel coordinates of a tile.
Beside setting the results in canvas_x,canvas_y it returns whether the tile
is inside the visible map.
**************************************************************************/
static int get_canvas_xy(int map_x, int map_y, int *canvas_x, int *canvas_y)
{
  if (map_view_x0+map_canvas_store_twidth <= map.xsize)
    *canvas_x = map_x-map_view_x0;
  else if(map_x >= map_view_x0)
    *canvas_x = map_x-map_view_x0;
  else if(map_x < map_adjust_x(map_view_x0+map_canvas_store_twidth))
    *canvas_x = map_x+map.xsize-map_view_x0;
  else *canvas_x = -1;

  *canvas_y = map_y - map_view_y0;

  *canvas_x *= NORMAL_TILE_WIDTH;
  *canvas_y *= NORMAL_TILE_HEIGHT;

  return *canvas_x >= 0
      && *canvas_x < map_canvas_store_twidth * NORMAL_TILE_WIDTH
      && *canvas_y >= 0
      && *canvas_y < map_canvas_store_theight * NORMAL_TILE_HEIGHT;
}

/**************************************************************************
Finds the map coordinates corresponding to pixel coordinates.
**************************************************************************/
void get_map_xy(int canvas_x, int canvas_y, int *map_x, int *map_y)
{
  *map_x = map_adjust_x(map_view_x0 + canvas_x/NORMAL_TILE_WIDTH);
  *map_y = map_adjust_y(map_view_y0 + canvas_y/NORMAL_TILE_HEIGHT);
}

/**************************************************************************
...
**************************************************************************/
void decrease_unit_hp_smooth(struct unit *punit0, int hp0, 
			     struct unit *punit1, int hp1)
{
  static struct timer *anim_timer = NULL; 
  struct unit *losing_unit = (hp0 == 0 ? punit0 : punit1);
  int i;
  int canvas_x, canvas_y;

  if (!do_combat_animation) {
    punit0->hp = hp0;
    punit1->hp = hp1;

    set_units_in_combat(NULL, NULL);
    refresh_tile_mapcanvas(punit0->x, punit0->y, 1);
    refresh_tile_mapcanvas(punit1->x, punit1->y, 1);

    return;
  }

  set_units_in_combat(punit0, punit1);

  do {
    anim_timer = renew_timer_start(anim_timer, TIMER_USER, TIMER_ACTIVE);

    if (punit0->hp > hp0
	&& myrand((punit0->hp - hp0) + (punit1->hp - hp1)) < punit0->hp - hp0)
      punit0->hp--;
    else if (punit1->hp > hp1)
      punit1->hp--;
    else
      punit0->hp--;

    refresh_tile_mapcanvas(punit0->x, punit0->y, 1);
    refresh_tile_mapcanvas(punit1->x, punit1->y, 1);

    XSync(display, 0);
    usleep_since_timer_start(anim_timer, 10000);

  } while (punit0->hp > hp0 || punit1->hp > hp1);

  get_canvas_xy(losing_unit->x, losing_unit->y, &canvas_x, &canvas_y);
  for (i = 0; i < num_tiles_explode_unit; i++) {
    anim_timer = renew_timer_start(anim_timer, TIMER_USER, TIMER_ACTIVE);

    pixmap_put_tile(single_tile_pixmap, 0, 0,
		    losing_unit->x, losing_unit->y, 0);
    put_unit_pixmap(losing_unit, single_tile_pixmap, 0, 0);
    pixmap_put_overlay_tile(single_tile_pixmap, 0, 0, sprites.explode.unit[i]);

    XCopyArea(display, single_tile_pixmap, XtWindow(map_canvas), civ_gc,
	      0, 0,
	      NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT,
	      canvas_x, canvas_y);

    XSync(display, 0);
    usleep_since_timer_start(anim_timer, 20000);
  }

  set_units_in_combat(NULL, NULL);
  refresh_tile_mapcanvas(punit0->x, punit0->y, 1);
  refresh_tile_mapcanvas(punit1->x, punit1->y, 1);
}

/**************************************************************************
...
**************************************************************************/
void set_overview_dimensions(int x, int y)
{
  Dimension h, w;

  XtVaSetValues(overview_canvas, XtNwidth, 2*x, XtNheight, 2*y, NULL);

  XtVaGetValues(left_column_form, XtNheight, &h, NULL);
  XtVaSetValues(map_form, XtNheight, h, NULL);

  XtVaGetValues(below_menu_form, XtNwidth, &w, NULL);
  XtVaSetValues(menu_form, XtNwidth, w, NULL);
  XtVaSetValues(bottom_form, XtNwidth, w, NULL);

  overview_canvas_store_width=2*x;
  overview_canvas_store_height=2*y;

  if(overview_canvas_store)
    XFreePixmap(display, overview_canvas_store);
  
  overview_canvas_store=XCreatePixmap(display, XtWindow(overview_canvas), 
				      overview_canvas_store_width,
				      overview_canvas_store_height,
				      display_depth);
}


/**************************************************************************
...
**************************************************************************/
void update_turn_done_button(int do_restore)
{
   static int flip;
   Pixel fore, back;
 
   if(game.player_ptr->ai.control && !ai_manual_turn_done)
     return;
   if((do_restore && flip) || !do_restore) { 
   
      XtVaGetValues(turn_done_button, 
		    XtNforeground, &fore,
		    XtNbackground, &back, NULL);
      
      
      XtVaSetValues(turn_done_button, 
		    XtNforeground, back,
		    XtNbackground, fore, NULL);
      
      flip=!flip;
   }
}


/**************************************************************************
...
**************************************************************************/
void update_timeout_label(void)
{
  char buffer[512];

  if (game.timeout <= 0)
    sz_strlcpy(buffer, Q_("?timeout:off"));
  else
    format_duration(buffer, sizeof(buffer), seconds_to_turndone);
  xaw_set_label(timeout_label, buffer);
}


/**************************************************************************
...
**************************************************************************/
void update_info_label(void)
{
  char buffer[512]; int d;
  
  my_snprintf(buffer, sizeof(buffer),
	      _("%s People\n"
		"Year: %s\n"
		"Gold: %d\n"
		"Tax:%d Lux:%d Sci:%d"),
	  population_to_text(civ_population(game.player_ptr)),
	  textyear(game.year),
	  game.player_ptr->economic.gold,
	  game.player_ptr->economic.tax,
	  game.player_ptr->economic.luxury,
	  game.player_ptr->economic.science);
  xaw_set_label(info_command, buffer);

  set_indicator_icons(client_research_sprite(),
		      client_warming_sprite(),
		      client_cooling_sprite(),
		      game.player_ptr->government);

  d=0;
  for(;d<(game.player_ptr->economic.luxury)/10;d++)
    xaw_set_bitmap(econ_label[d], get_citizen_pixmap(0) ); /* elvis tile */
 
  for(;d<(game.player_ptr->economic.science+game.player_ptr->economic.luxury)/10;d++)
    xaw_set_bitmap(econ_label[d], get_citizen_pixmap(1) ); /* scientist tile */
 
   for(;d<10;d++)
    xaw_set_bitmap(econ_label[d], get_citizen_pixmap(2) ); /* taxman tile */
 
  update_timeout_label();
}


/**************************************************************************
  Update the information label which gives info on the current unit and the
  square under the current unit, for specified unit.  Note that in practice
  punit is almost always (or maybe strictly always?) the focus unit.
  Clears label if punit is NULL.
  Also updates the cursor for the map_canvas (this is related because the
  info label includes a "select destination" prompt etc).
  Also calls update_unit_pix_label() to update the icons for units on this
  square.
**************************************************************************/
void update_unit_info_label(struct unit *punit)
{
  if(punit) {
    char buffer[512];
    struct city *pcity;
    pcity=player_find_city_by_id(game.player_ptr, punit->homecity);
    my_snprintf(buffer, sizeof(buffer), "%s %s\n%s\n%s\n%s", 
		unit_type(punit)->name,
		(punit->veteran) ? _("(veteran)") : "",
		(hover_unit==punit->id) ? 
		_("Select destination") : unit_activity_text(punit), 
		map_get_tile_info_text(punit->x, punit->y),
		pcity ? pcity->name : "");
    xaw_set_label(unit_info_label, buffer);

    if (hover_unit != punit->id)
      set_hover_state(NULL, HOVER_NONE);

    switch (hover_state) {
    case HOVER_NONE:
      XUndefineCursor(display, XtWindow(map_canvas));
      break;
    case HOVER_PATROL:
      XDefineCursor(display, XtWindow(map_canvas), patrol_cursor);
      break;
    case HOVER_GOTO:
    case HOVER_CONNECT:
      XDefineCursor(display, XtWindow(map_canvas), goto_cursor);
      break;
    case HOVER_NUKE:
      XDefineCursor(display, XtWindow(map_canvas), nuke_cursor);
      break;
    case HOVER_PARADROP:
      XDefineCursor(display, XtWindow(map_canvas), drop_cursor);
      break;
    }
  } else {
    xaw_set_label(unit_info_label, "");
    XUndefineCursor(display, XtWindow(map_canvas));
  }

  update_unit_pix_label(punit);
}

/**************************************************************************
...
**************************************************************************/
Pixmap get_thumb_pixmap(int onoff)
{
  return sprites.treaty_thumb[BOOL_VAL(onoff)]->pixmap;
}

/**************************************************************************
...
**************************************************************************/
Pixmap get_citizen_pixmap(int frame)
{
  frame = CLIP(0, frame, NUM_TILES_CITIZEN-1);
  return sprites.citizen[frame]->pixmap;
}


/**************************************************************************
...
**************************************************************************/
void set_indicator_icons(int bulb, int sol, int flake, int gov)
{
  struct Sprite *gov_sprite;

  bulb = CLIP(0, bulb, NUM_TILES_PROGRESS-1);
  sol = CLIP(0, sol, NUM_TILES_PROGRESS-1);
  flake = CLIP(0, flake, NUM_TILES_PROGRESS-1);

  xaw_set_bitmap(bulb_label, sprites.bulb[bulb]->pixmap);
  xaw_set_bitmap(sun_label, sprites.warming[sol]->pixmap);
  xaw_set_bitmap(flake_label, sprites.cooling[flake]->pixmap);

  if (game.government_count==0) {
    /* not sure what to do here */
    gov_sprite = sprites.citizen[7]; 
  } else {
    gov_sprite = get_government(gov)->sprite;
  }
  xaw_set_bitmap(government_label, gov_sprite->pixmap);
}

/**************************************************************************
This function is now identical in all GUI's except BeOS.
**************************************************************************/
void refresh_tile_mapcanvas(int x, int y, int write_to_screen)
{
  int is_real = normalize_map_pos(&x, &y);

  assert(is_real);

  if (tile_visible_mapcanvas(x, y)) {
    update_map_canvas(x, y, 1, 1, write_to_screen);
  }
  overview_update_tile(x, y);
}

/**************************************************************************
...
**************************************************************************/
int tile_visible_mapcanvas(int x, int y)
{
  return (y>=map_view_y0 && y<map_view_y0+map_canvas_store_theight &&
	  ((x>=map_view_x0 && x<map_view_x0+map_canvas_store_twidth) ||
	   (x+map.xsize>=map_view_x0 && 
	    x+map.xsize<map_view_x0+map_canvas_store_twidth)));
}

/**************************************************************************
...
**************************************************************************/
int tile_visible_and_not_on_border_mapcanvas(int x, int y)
{
  return ((y>=map_view_y0+2 || (y >= map_view_y0 && map_view_y0 == 0))
	  && (y<map_view_y0+map_canvas_store_theight-2 ||
	      (y<map_view_y0+map_canvas_store_theight &&
	       map_view_y0 + map_canvas_store_theight-EXTRA_BOTTOM_ROW == map.ysize))
	  && ((x>=map_view_x0+2 && x<map_view_x0+map_canvas_store_twidth-2) ||
	      (x+map.xsize>=map_view_x0+2
	       && x+map.xsize<map_view_x0+map_canvas_store_twidth-2)));
}

/**************************************************************************
Animates punit's "smooth" move from (x0,y0) to (x0+dx,y0+dy).
Note: Works only for adjacent-square moves.
(Tiles need not be square.)
**************************************************************************/
void move_unit_map_canvas(struct unit *punit, int x0, int y0, int dx, int dy)
{
  static struct timer *anim_timer = NULL; 
  int dest_x, dest_y;

  /* only works for adjacent-square moves */
  if ((dx < -1) || (dx > 1) || (dy < -1) || (dy > 1) ||
      ((dx == 0) && (dy == 0))) {
    return;
  }

  dest_x = map_adjust_x(x0+dx);
  dest_y = map_adjust_y(y0+dy);

  if (player_can_see_unit(game.player_ptr, punit) &&
      (tile_visible_mapcanvas(x0, y0) ||
       tile_visible_mapcanvas(dest_x, dest_y))) {
    int i, steps;
    int start_x, start_y;
    int this_x, this_y;

    if (smooth_move_unit_steps < 2) {
      steps = 2;
    } else if (smooth_move_unit_steps >
	       MIN(NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT)) {
      steps = MIN(NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
    } else {
      steps = smooth_move_unit_steps;
    }

    get_canvas_xy(x0, y0, &start_x, & start_y);

    this_x = start_x;
    this_y = start_y;

    for (i = 1; i <= steps; i++) {
      anim_timer = renew_timer_start(anim_timer, TIMER_USER, TIMER_ACTIVE);

      XCopyArea(display, map_canvas_store, XtWindow(map_canvas), civ_gc,
		this_x, this_y, NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT,
		this_x, this_y);

      this_x = start_x + (dx * ((i * NORMAL_TILE_WIDTH) / steps));
      this_y = start_y + (dy * ((i * NORMAL_TILE_HEIGHT) / steps));

      XCopyArea(display, map_canvas_store, single_tile_pixmap, civ_gc,
		this_x, this_y, NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT,
		0, 0);
      put_unit_pixmap(punit, single_tile_pixmap, 0, 0);

      XCopyArea(display, single_tile_pixmap, XtWindow(map_canvas), civ_gc,
		0, 0, NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT,
		this_x, this_y);

      XSync(display, 0);
      if (i < steps) {
	usleep_since_timer_start(anim_timer, 10000);
      }
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void get_center_tile_mapcanvas(int *x, int *y)
{
  *x = map_adjust_x(map_view_x0+map_canvas_store_twidth/2);
  *y = map_adjust_y(map_view_y0+map_canvas_store_theight/2);
}

/**************************************************************************
...
**************************************************************************/
void center_tile_mapcanvas(int x, int y)
{
  int new_map_view_x0, new_map_view_y0;

  new_map_view_x0=map_adjust_x(x-map_canvas_store_twidth/2);
  new_map_view_y0=map_adjust_y(y-map_canvas_store_theight/2);
  if(new_map_view_y0>map.ysize+EXTRA_BOTTOM_ROW-map_canvas_store_theight)
     new_map_view_y0=
       map_adjust_y(map.ysize+EXTRA_BOTTOM_ROW-map_canvas_store_theight);

  map_view_x0=new_map_view_x0;
  map_view_y0=new_map_view_y0;

  update_map_canvas_visible();
  update_map_canvas_scrollbars();
  
  refresh_overview_viewrect();
  if (hover_state == HOVER_GOTO || hover_state == HOVER_PATROL)
    create_line_at_mouse_pos();
}

/**************************************************************************
...
**************************************************************************/
void overview_canvas_expose(Widget w, XEvent *event, Region exposed, 
			    void *client_data)
{
  Dimension height, width;
  
  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE) {
    if(radar_gfx_sprite)
      XCopyArea(display, radar_gfx_sprite->pixmap, XtWindow(overview_canvas),
                 civ_gc,
                 event->xexpose.x, event->xexpose.y,
                 event->xexpose.width, event->xexpose.height,
                 event->xexpose.x, event->xexpose.y);
    return;
  }

  XtVaGetValues(w, XtNheight, &height, XtNwidth, &width, NULL);
  
  refresh_overview_viewrect();
}


/**************************************************************************
...
**************************************************************************/
static void set_overview_tile_foreground_color(int x, int y)
{
  XSetForeground(display, fill_bg_gc,
		 colors_standard[overview_tile_color(x, y)]);
}


/**************************************************************************
...
**************************************************************************/
void refresh_overview_canvas(void)
{
  whole_map_iterate(x, y) {
    set_overview_tile_foreground_color(x, y);
    XFillRectangle(display, overview_canvas_store, fill_bg_gc, x * 2,
		   y * 2, 2, 2);
  } whole_map_iterate_end;

  XSetForeground(display, fill_bg_gc, 0);
}


/**************************************************************************
...
**************************************************************************/
void overview_update_tile(int x, int y)
{
  int pos = x + map.xsize/2 - (map_view_x0 + map_canvas_store_twidth/2);
  
  pos %= map.xsize;
  if (pos < 0)
    pos += map.xsize;
  
  set_overview_tile_foreground_color(x, y);
  XFillRectangle(display, overview_canvas_store, fill_bg_gc, x*2, y*2, 
                 2, 2);
  
  XFillRectangle(display, XtWindow(overview_canvas), fill_bg_gc, 
                 pos*2, y*2, 2, 2);
}

/**************************************************************************
...
**************************************************************************/
void refresh_overview_viewrect(void)
{
  int delta=map.xsize/2-(map_view_x0+map_canvas_store_twidth/2);

  if(delta>=0) {
    XCopyArea(display, overview_canvas_store, XtWindow(overview_canvas), 
	      civ_gc, 0, 0, 
	      overview_canvas_store_width-2*delta,
	      overview_canvas_store_height, 
	      2*delta, 0);
    XCopyArea(display, overview_canvas_store, XtWindow(overview_canvas), 
	      civ_gc, 
	      overview_canvas_store_width-2*delta, 0,
	      2*delta, overview_canvas_store_height, 
	      0, 0);
  }
  else {
    XCopyArea(display, overview_canvas_store, XtWindow(overview_canvas), 
	      civ_gc, 
	      -2*delta, 0, 
	      overview_canvas_store_width+2*delta,
	      overview_canvas_store_height, 
	      0, 0);

    XCopyArea(display, overview_canvas_store, XtWindow(overview_canvas), 
	      civ_gc, 
	      0, 0,
	      -2*delta, overview_canvas_store_height, 
	      overview_canvas_store_width+2*delta, 0);
  }

  XSetForeground(display, civ_gc, colors_standard[COLOR_STD_WHITE]);
  
  XDrawRectangle(display, XtWindow(overview_canvas), civ_gc, 
		 (overview_canvas_store_width-2*map_canvas_store_twidth)/2,
		 2*map_view_y0,
		 2*map_canvas_store_twidth, 2*map_canvas_store_theight-1);
}


/**************************************************************************
...
**************************************************************************/
void map_canvas_expose(Widget w, XEvent *event, Region exposed, 
		       void *client_data)
{
  Dimension width, height;
  int tile_width, tile_height;

  XtVaGetValues(w, XtNwidth, &width, XtNheight, &height, NULL);
  tile_width=(width+NORMAL_TILE_WIDTH-1)/NORMAL_TILE_WIDTH;
  tile_height=(height+NORMAL_TILE_HEIGHT-1)/NORMAL_TILE_HEIGHT;

  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE) {
    if(!intro_gfx_sprite)  load_intro_gfx();
    if(height!=scaled_intro_pixmap_height || width!=scaled_intro_pixmap_width) {
      if(scaled_intro_pixmap)
	XFreePixmap(display, scaled_intro_pixmap);

      scaled_intro_pixmap=x_scale_pixmap(intro_gfx_sprite->pixmap,
					 intro_gfx_sprite->width,
					 intro_gfx_sprite->height, 
					 width, height, root_window);
      scaled_intro_pixmap_width=width;
      scaled_intro_pixmap_height=height;
    }

    if(scaled_intro_pixmap)
       XCopyArea(display, scaled_intro_pixmap, XtWindow(map_canvas),
		 civ_gc,
		 event->xexpose.x, event->xexpose.y,
		 event->xexpose.width, event->xexpose.height,
		 event->xexpose.x, event->xexpose.y);

    if(map_canvas_store_twidth !=tile_width ||
       map_canvas_store_theight!=tile_height) { /* resized? */
      map_canvas_resize();
    }
    return;
  }
  if(scaled_intro_pixmap) {
    XFreePixmap(display, scaled_intro_pixmap);
    scaled_intro_pixmap=0; scaled_intro_pixmap_height=0;
  }

  if(map.xsize) { /* do we have a map at all */
    if(map_canvas_store_twidth !=tile_width ||
       map_canvas_store_theight!=tile_height) { /* resized? */
      map_canvas_resize();

      XFillRectangle(display, map_canvas_store, fill_bg_gc, 0, 0, 
		     NORMAL_TILE_WIDTH*map_canvas_store_twidth,
		     NORMAL_TILE_HEIGHT*map_canvas_store_theight);

      update_map_canvas_visible();

      update_map_canvas_scrollbars();
      refresh_overview_viewrect();
    } else {
      XCopyArea(display, map_canvas_store, XtWindow(map_canvas),
		civ_gc,
		event->xexpose.x, event->xexpose.y,
		event->xexpose.width, event->xexpose.height,
		event->xexpose.x, event->xexpose.y);
      show_city_descriptions();
    }
  }
  refresh_overview_canvas();
}

/**************************************************************************
...
**************************************************************************/
void map_canvas_resize(void)
{
  Dimension width, height;

  if (map_canvas_store)
    XFreePixmap(display, map_canvas_store);

  XtVaGetValues(map_canvas, XtNwidth, &width, XtNheight, &height, NULL);
  map_canvas_store_twidth=((width-1)/NORMAL_TILE_WIDTH)+1;
  map_canvas_store_theight=((height-1)/NORMAL_TILE_HEIGHT)+1;

  map_canvas_store=XCreatePixmap(display, XtWindow(map_canvas),
				 map_canvas_store_twidth*NORMAL_TILE_WIDTH,
				 map_canvas_store_theight*NORMAL_TILE_HEIGHT,
				 display_depth);
}

/**************************************************************************
...
**************************************************************************/
void update_map_canvas(int x, int y, int width, int height, 
		       int write_to_screen)
{
  int x_itr, y_itr;
  int canvas_x, canvas_y;

  for (y_itr=y; y_itr<y+height; y_itr++) {
    for (x_itr=x; x_itr<x+width; x_itr++) {
      int map_x = map_adjust_x(x_itr);
      int map_y = y_itr;
      get_canvas_xy(map_x, map_y, &canvas_x, &canvas_y);

      if (tile_visible_mapcanvas(map_x, map_y)) {
	pixmap_put_tile(map_canvas_store, map_x, map_y,
			canvas_x, canvas_y, 0);
      }
    }
  }

  get_canvas_xy(x, y, &canvas_x, &canvas_y);
  if (write_to_screen) {
    XCopyArea(display, map_canvas_store, XtWindow(map_canvas), 
	      civ_gc, 
	      canvas_x, canvas_y,
	      width*NORMAL_TILE_WIDTH, height*NORMAL_TILE_HEIGHT,
	      canvas_x, canvas_y);
  }
}

/**************************************************************************
 Update (only) the visible part of the map
**************************************************************************/
void update_map_canvas_visible(void)
{
  update_map_canvas(map_view_x0, map_view_y0,
		    map_canvas_store_twidth, map_canvas_store_theight, 1);
  show_city_descriptions();
}

/**************************************************************************
...
**************************************************************************/
void update_map_canvas_scrollbars(void)
{
  float shown_h=(float)map_canvas_store_twidth/(float)map.xsize;
  float top_h=(float)map_view_x0/(float)map.xsize;
  float shown_v=(float)map_canvas_store_theight/((float)map.ysize+EXTRA_BOTTOM_ROW);
  float top_v=(float)map_view_y0/((float)map.ysize+EXTRA_BOTTOM_ROW);

  XawScrollbarSetThumb(map_horizontal_scrollbar, top_h, shown_h);
  XawScrollbarSetThumb(map_vertical_scrollbar, top_v, shown_v);
}

/**************************************************************************
Update display of descriptions associated with cities on the main map.
**************************************************************************/
void update_city_descriptions(void)
{
  update_map_canvas_visible();
}

/**************************************************************************
Draw at x = center of string, y = top of string.
**************************************************************************/
static void draw_shadowed_string(XFontStruct * font, GC font_gc,
					  int x, int y, const char *string)
{
  Window wndw=XtWindow(map_canvas);
  int len=strlen(string);
  int wth=XTextWidth(font, string, len);
  int xs=x-wth/2;
  int ys=y+font->ascent;

  XSetForeground(display, font_gc, colors_standard[COLOR_STD_BLACK]);
  XDrawString(display, wndw, font_gc, xs+1, ys+1, string, len);

  XSetForeground(display, font_gc, colors_standard[COLOR_STD_WHITE]);
  XDrawString(display, wndw, font_gc, xs, ys, string, len);
}

/**************************************************************************
...
**************************************************************************/
static void show_city_descriptions(void)
{
  int x, y;

  if (!draw_city_names && !draw_city_productions)
    return;

  for (y = 0; y < map_canvas_store_theight; ++y) {
    for (x = 0; x < map_canvas_store_twidth; ++x) {
      int rx = map_view_x0 + x;
      int ry = map_view_y0 + y;
      struct city *pcity;

      if (!normalize_map_pos(&rx, &ry))
        continue;

      if((pcity=map_get_city(rx, ry))) {

	if (draw_city_names) {
	  draw_shadowed_string(main_font_struct, font_gc,
			       x*NORMAL_TILE_WIDTH+NORMAL_TILE_WIDTH/2,
			       (y+1)*NORMAL_TILE_HEIGHT,
			       pcity->name);
	}

	if (draw_city_productions && (pcity->owner==game.player_idx)) {
	  int turns, show_turns = 1;
	  struct unit_type *punit_type;
	  struct impr_type *pimpr_type;
	  char *name;
	  char buffer[512];

	  turns = city_turns_to_build(pcity, pcity->currently_building,
				      pcity->is_building_unit, TRUE);

	  if (pcity->is_building_unit) {
	    punit_type = get_unit_type(pcity->currently_building);
	    name = punit_type->name;
	  } else {
	    pimpr_type = get_improvement_type(pcity->currently_building);
	    name = pimpr_type->name;
	    show_turns = (pcity->currently_building != B_CAPITAL);
	  }
	  if (show_turns) {
	    my_snprintf(buffer, sizeof(buffer), "%s %d", name, turns);
	  } else {
	    sz_strlcpy(buffer, name);
	  }

	  draw_shadowed_string(prod_font_struct, prod_font_gc,
			       x*NORMAL_TILE_WIDTH+NORMAL_TILE_WIDTH/2,
			       (y+1)*NORMAL_TILE_HEIGHT +
			         (draw_city_names ?
				   main_font_struct->ascent +
				     main_font_struct->descent :
				   0),
			       buffer);
	}

      }
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void put_city_tile_output(Pixmap pm, int canvas_x, int canvas_y, 
			  int food, int shield, int trade)
{
  food = CLIP(0, food, NUM_TILES_DIGITS-1);
  trade = CLIP(0, trade, NUM_TILES_DIGITS-1);
  shield = CLIP(0, shield, NUM_TILES_DIGITS-1);
  
  pixmap_put_overlay_tile(pm, canvas_x, canvas_y, sprites.city.tile_foodnum[food]);
  pixmap_put_overlay_tile(pm, canvas_x, canvas_y, sprites.city.tile_shieldnum[shield]);
  pixmap_put_overlay_tile(pm, canvas_x, canvas_y, sprites.city.tile_tradenum[trade]);
}


/**************************************************************************
...
**************************************************************************/
void put_unit_pixmap(struct unit *punit, Pixmap pm,
		     int canvas_x, int canvas_y)
{
  struct Sprite *sprites[40];
  int solid_bg;
  int count = fill_unit_sprite_array(sprites, punit, &solid_bg);

  if (count) {
    int i = 0;

    if (solid_bg) {
      XSetForeground(display, fill_bg_gc,
		     colors_standard[player_color(unit_owner(punit))]);
      XFillRectangle(display, pm, fill_bg_gc, 
		     canvas_x, canvas_y,
		     NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
    } else {
      if (flags_are_transparent) {
	pixmap_put_overlay_tile(pm, canvas_x, canvas_y, sprites[0]);
      } else {
	XCopyArea(display, sprites[0]->pixmap, pm, civ_gc, 0, 0,
		  sprites[0]->width, sprites[0]->height, 
		  canvas_x, canvas_y);
      }
      i++;
    }

    for (; i<count; i++) {
      if (sprites[i])
	pixmap_put_overlay_tile(pm, canvas_x, canvas_y, sprites[i]);
    }
  }
}


/**************************************************************************
  FIXME: 
  For now only two food, one shield and two masks can be drawn per unit,
  the proper way to do this is probably something like what Civ II does.
  (One food/shield/mask drawn N times, possibly one top of itself. -- SKi 
**************************************************************************/
void put_unit_pixmap_city_overlays(struct unit *punit, Pixmap pm)
{
  int upkeep_food = CLIP(0, punit->upkeep_food, 2);
  int unhappy = CLIP(0, punit->unhappiness, 2);
 
  /* wipe the slate clean */
  XSetForeground(display, fill_bg_gc, colors_standard[COLOR_STD_WHITE]);
  XFillRectangle(display, pm, fill_bg_gc, 0, NORMAL_TILE_WIDTH, 
		 NORMAL_TILE_HEIGHT, NORMAL_TILE_HEIGHT+SMALL_TILE_HEIGHT);

  /* draw overlay pixmaps */
  if (punit->upkeep > 0)
    pixmap_put_overlay_tile(pm, 0, 1, sprites.upkeep.shield);
  if (upkeep_food > 0)
    pixmap_put_overlay_tile(pm, 0, 1, sprites.upkeep.food[upkeep_food-1]);
  if (unhappy > 0)
    pixmap_put_overlay_tile(pm, 0, 1, sprites.upkeep.unhappy[unhappy-1]);
}

/**************************************************************************
...
**************************************************************************/
void put_nuke_mushroom_pixmaps(int x, int y)
{
  int x_itr, y_itr;

  for (x_itr = 0; x_itr<3; x_itr++) {
    for (y_itr = 0; y_itr<3; y_itr++) {
      int x1 = x + x_itr -1;
      int y1 = y + y_itr -1;
      if (normalize_map_pos(&x1, &y1)) {
	int canvas_x, canvas_y;
	struct Sprite *mysprite = sprites.explode.nuke[y_itr][x_itr];

	get_canvas_xy(x1, y1, &canvas_x, &canvas_y);
	XCopyArea(display, map_canvas_store, single_tile_pixmap, civ_gc,
		  canvas_x, canvas_y, NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT,
		  0, 0);
	pixmap_put_overlay_tile(single_tile_pixmap, 0, 0, mysprite);
	XCopyArea(display, single_tile_pixmap, XtWindow(map_canvas), civ_gc,
		  0, 0, NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT,
		  canvas_x, canvas_y);
      }
    }
  }
  XSync(display, 0);
  sleep(1);

  update_map_canvas(x-1, y-1, 3, 3, 1);
}

/**************************************************************************
...
**************************************************************************/
void pixmap_put_black_tile(Pixmap pm, int canvas_x, int canvas_y)
{
  XSetForeground(display, fill_bg_gc, colors_standard[COLOR_STD_BLACK]);

  XFillRectangle(display, pm, fill_bg_gc,  
		 canvas_x, canvas_y,
		 NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
}
		     

/**************************************************************************
...
**************************************************************************/
void pixmap_frame_tile_red(Pixmap pm, int canvas_x, int canvas_y)
{
  XSetForeground(display, fill_bg_gc, colors_standard[COLOR_STD_RED]);

  XDrawRectangle(display, pm, fill_bg_gc,  
		 canvas_x, canvas_y,
		 NORMAL_TILE_WIDTH-1, NORMAL_TILE_HEIGHT-1);
  XDrawRectangle(display, pm, fill_bg_gc,  
		 canvas_x+1, canvas_y+1,
		 NORMAL_TILE_WIDTH-3, NORMAL_TILE_HEIGHT-3);
  XDrawRectangle(display, pm, fill_bg_gc,  
		 canvas_x+2, canvas_y+2,
		 NORMAL_TILE_WIDTH-5, NORMAL_TILE_HEIGHT-5);
}


/**************************************************************************
...
**************************************************************************/
void pixmap_put_tile(Pixmap pm, int x, int y, int canvas_x, int canvas_y, 
		     int citymode)
{
  struct Sprite *tile_sprs[80];
  int fill_bg;
  struct player *pplayer;

  if (normalize_map_pos(&x, &y) && tile_is_known(x, y)) {
    int count = fill_tile_sprite_array(tile_sprs, x, y, citymode, &fill_bg, &pplayer);
    int i = 0;

    if (fill_bg) {
      if (pplayer) {
        XSetForeground(display, fill_bg_gc,
		       colors_standard[player_color(pplayer)]);
      } else {
        XSetForeground(display, fill_bg_gc,
		       colors_standard[COLOR_STD_BACKGROUND]);
      }
      XFillRectangle(display, pm, fill_bg_gc, 
		     canvas_x, canvas_y, NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
    } else {
      /* first tile without mask */
      XCopyArea(display, tile_sprs[0]->pixmap, pm, 
		civ_gc, 0, 0,
		tile_sprs[0]->width, tile_sprs[0]->height, canvas_x, canvas_y);
      i++;
    }

    for (;i<count; i++) {
      if (tile_sprs[i]) {
        pixmap_put_overlay_tile(pm, canvas_x, canvas_y, tile_sprs[i]);
      }
    }

    if (draw_map_grid && !citymode) {
      /* left side... */
      XSetForeground(display, civ_gc, colors_standard[get_grid_color
						      (x, y, x - 1, y)]);
      XDrawLine(display, pm, civ_gc,
		canvas_x, canvas_y,
		canvas_x, canvas_y + NORMAL_TILE_HEIGHT);

      /* top side... */
      XSetForeground(display, civ_gc, colors_standard[get_grid_color
						      (x, y, x, y - 1)]);
      XDrawLine(display, pm, civ_gc,
		canvas_x, canvas_y,
		canvas_x + NORMAL_TILE_WIDTH, canvas_y);
    }
    if (draw_coastline && !draw_terrain) {
      enum tile_terrain_type t1 = map_get_terrain(x, y), t2;
      int x1 = x-1, y1 = y;
      XSetForeground(display, civ_gc, colors_standard[COLOR_STD_OCEAN]);
      if (normalize_map_pos(&x1, &y1)) {
	t2 = map_get_terrain(x1, y1);
	/* left side */
	if ((t1 == T_OCEAN) ^ (t2 == T_OCEAN))
	  XDrawLine(display, pm, civ_gc,
		    canvas_x, canvas_y,
		    canvas_x, canvas_y + NORMAL_TILE_HEIGHT);
      }
      /* top side */
      x1 = x; y1 = y-1;
      if (normalize_map_pos(&x1, &y1)) {
	t2 = map_get_terrain(x1, y1);
	if ((t1 == T_OCEAN) ^ (t2 == T_OCEAN))
	  XDrawLine(display, pm, civ_gc,
		    canvas_x, canvas_y,
		    canvas_x + NORMAL_TILE_WIDTH, canvas_y);
      }
    }
  } else {
    /* tile is unknown */
    pixmap_put_black_tile(pm, canvas_x, canvas_y);
  }

  if (!citymode) {
    /* put any goto lines on the tile. */
    if (is_real_tile(x, y)) {
      int dir;
      for (dir = 0; dir < 8; dir++) {
	if (get_drawn(x, y, dir)) {
	  put_line(map_canvas_store, x, y, dir);
	}
      }
    }

    /* Some goto lines overlap onto the tile... */
    if (NORMAL_TILE_WIDTH%2 == 0 || NORMAL_TILE_HEIGHT%2 == 0) {
      int line_x = x - 1;
      int line_y = y;
      if (normalize_map_pos(&line_x, &line_y)
	  && get_drawn(line_x, line_y, 2)) {
	/* it is really only one pixel in the top right corner */
	put_line(map_canvas_store, line_x, line_y, 2);
      }
    }
  }
}

  
/**************************************************************************
...
**************************************************************************/
static void pixmap_put_overlay_tile(Pixmap pixmap, int canvas_x, int canvas_y,
 				    struct Sprite *ssprite)
{
  if (!ssprite) return;
      
  XSetClipOrigin(display, civ_gc, canvas_x, canvas_y);
  XSetClipMask(display, civ_gc, ssprite->mask);
      
  XCopyArea(display, ssprite->pixmap, pixmap, 
	    civ_gc, 0, 0,
	    ssprite->width, ssprite->height, 
	    canvas_x, canvas_y);
  XSetClipMask(display, civ_gc, None); 
}

/**************************************************************************
 Draws a cross-hair overlay on a tile
**************************************************************************/
void put_cross_overlay_tile(int x,int y)
{
  int canvas_x, canvas_y;
  x=map_adjust_x(x);
  y=map_adjust_y(y);
  get_canvas_xy(x, y, &canvas_x, &canvas_y);

  if (tile_visible_mapcanvas(x, y)) {
    pixmap_put_overlay_tile(XtWindow(map_canvas), canvas_x, canvas_y,
			    sprites.user.attention);
  }
}


/**************************************************************************
 Shade the tiles around a city to indicate the location of workers
**************************************************************************/
void put_city_workers(struct city *pcity, int color)
{
  int canvas_x, canvas_y;
  static struct city *last_pcity = NULL;

  if (color == -1) {
    if (pcity != last_pcity)
      city_workers_color = (city_workers_color%3)+1;
    color = city_workers_color;
  }

  XSetForeground(display, fill_tile_gc, colors_standard[color]);
  get_canvas_xy(pcity->x, pcity->y, &canvas_x, &canvas_y);
  city_map_checked_iterate(pcity->x, pcity->y, i, j, x, y) {
    enum city_tile_type worked = get_worker_city(pcity, i, j);

    get_canvas_xy(x, y, &canvas_x, &canvas_y);
    if (!is_city_center(i, j)) {
      if (worked == C_TILE_EMPTY) {
	XSetStipple(display, fill_tile_gc, gray25);
      } else if (worked == C_TILE_WORKER) {
	XSetStipple(display, fill_tile_gc, gray50);
      } else
	continue;
      XCopyArea(display, map_canvas_store, XtWindow(map_canvas), civ_gc,
		canvas_x, canvas_y,
		NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT, canvas_x, canvas_y);
      XFillRectangle(display, XtWindow(map_canvas), fill_tile_gc,
		     canvas_x, canvas_y,
		     NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
    }
    if (worked == C_TILE_WORKER) {
      put_city_tile_output(XtWindow(map_canvas),
			   canvas_x, canvas_y,
			   city_get_food_tile(i, j, pcity),
			   city_get_shields_tile(i, j, pcity),
			   city_get_trade_tile(i, j, pcity));
    }
  } city_map_checked_iterate_end;

  last_pcity = pcity;
}


/**************************************************************************
...
**************************************************************************/
void scrollbar_jump_callback(Widget w, XtPointer client_data,
			     XtPointer percent_ptr)
{
  float percent=*(float*)percent_ptr;

  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE)
     return;

  if(w==map_horizontal_scrollbar)
    map_view_x0=percent*map.xsize;
  else {
    map_view_y0=percent*(map.ysize+EXTRA_BOTTOM_ROW);
    map_view_y0=(map_view_y0<0) ? 0 : map_view_y0;
    map_view_y0=
      (map_view_y0>map.ysize+EXTRA_BOTTOM_ROW-map_canvas_store_theight) ? 
	map.ysize+EXTRA_BOTTOM_ROW-map_canvas_store_theight :
	map_view_y0;
  }

  update_map_canvas_visible();
  /* The scrollbar tracks by itself, while calling the jumpProc,
     so there's no need to call update_map_canvas_scrollbars() here. */
  refresh_overview_viewrect();
}


/**************************************************************************
...
**************************************************************************/
void scrollbar_scroll_callback(Widget w, XtPointer client_data,
			     XtPointer position_val)
{
  int position=(int)position_val;


  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE)
     return;

  if(w==map_horizontal_scrollbar) {
    if(position>0) 
      map_view_x0++;
    else
      map_view_x0--;
  }
  else {
    if(position>0 && map_view_y0<map.ysize+EXTRA_BOTTOM_ROW-map_canvas_store_theight)
      map_view_y0++;
    else if(position<0 && map_view_y0>0)
      map_view_y0--;
  }

  map_view_x0=map_adjust_x(map_view_x0);
  map_view_y0=map_adjust_y(map_view_y0);


  update_map_canvas_visible();
  update_map_canvas_scrollbars();
  refresh_overview_viewrect();
}

/**************************************************************************
...
**************************************************************************/
static void put_line(Pixmap pm, int x, int y, int dir)
{
  int canvas_src_x, canvas_src_y, canvas_dest_x, canvas_dest_y;
  get_canvas_xy(x, y, &canvas_src_x, &canvas_src_y);
  canvas_src_x += NORMAL_TILE_WIDTH/2;
  canvas_src_y += NORMAL_TILE_HEIGHT/2;
  canvas_dest_x = canvas_src_x + (NORMAL_TILE_WIDTH * DIR_DX[dir])/2;
  canvas_dest_y = canvas_src_y + (NORMAL_TILE_WIDTH * DIR_DY[dir])/2;

  XSetForeground(display, civ_gc, colors_standard[COLOR_STD_CYAN]);

  XDrawLine(display, pm, civ_gc, canvas_src_x, canvas_src_y,
	    canvas_dest_x, canvas_dest_y);
}

/**************************************************************************
...
**************************************************************************/
void draw_segment(int src_x, int src_y, int dir)
{
  int dest_x, dest_y, is_real;

  is_real = MAPSTEP(dest_x, dest_y, src_x, src_y, dir);
  assert(is_real);

  /* A previous line already marks the place */
  if (get_drawn(src_x, src_y, dir)) {
    increment_drawn(src_x, src_y, dir);
    return;
  }

  if (tile_visible_mapcanvas(src_x, src_y)) {
    put_line(map_canvas_store, src_x, src_y, dir);
    put_line(XtWindow(map_canvas), src_x, src_y, dir);
  }

  if (tile_visible_mapcanvas(dest_x, dest_y)) {
    put_line(map_canvas_store, dest_x, dest_y, DIR_REVERSE(dir));
    put_line(XtWindow(map_canvas), dest_x, dest_y, DIR_REVERSE(dir));
  }

  increment_drawn(src_x, src_y, dir);
}

/**************************************************************************
This is somewhat inefficient, but I simply can't feel any performance
penalty so I will be lazy...
**************************************************************************/
void undraw_segment(int src_x, int src_y, int dir)
{
  int dest_x, dest_y, is_real;
  int drawn = get_drawn(src_x, src_y, dir);

  is_real = MAPSTEP(dest_x, dest_y, src_x, src_y, dir);
  assert(is_real);

  assert(drawn > 0);
  /* If we walk on a path twice it looks just like walking on it once. */
  if (drawn > 1) {
    decrement_drawn(src_x, src_y, dir);
    return;
  }

  decrement_drawn(src_x, src_y, dir);
  refresh_tile_mapcanvas(src_x, src_y, 1);
  refresh_tile_mapcanvas(dest_x, dest_y, 1);
  if (NORMAL_TILE_WIDTH%2 == 0 || NORMAL_TILE_HEIGHT%2 == 0) {
    int is_real;

    if (dir == DIR8_NORTHEAST) {
      /* Since the tile doesn't have a middle we draw an extra pixel
         on the adjacent tile when drawing in this direction. */
      dest_x = src_x + 1;
      dest_y = src_y;
      is_real = normalize_map_pos(&dest_x, &dest_y);
      assert(is_real);
      refresh_tile_mapcanvas(dest_x, dest_y, 1);
    } else if (dir == DIR8_SOUTHWEST) {	/* the same */
      dest_x = src_x;
      dest_y = src_y + 1;
      is_real = normalize_map_pos(&dest_x, &dest_y);
      assert(is_real);
      refresh_tile_mapcanvas(dest_x, dest_y, 1);
    }
  }
}
