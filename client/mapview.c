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
#include <stdio.h>
#include <stdlib.h>

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Scrollbar.h>

#include <mapview.h>
#include <map.h>
#include <graphics.h>
#include <log.h>
#include <colors.h>
#include <unit.h>
#include <game.h>
#include <civclient.h>
#include <xstuff.h>
#include <mapctrl.h>
#include <canvas.h>
#include <pixcomm.h>

#define CORNER_TILES     0

#define NUKE_TILE0        1*20+17
#define NUKE_TILE1        1*20+18
#define NUKE_TILE2        1*20+19
#define NUKE_TILE3        2*20+17
#define NUKE_TILE4        2*20+18
#define NUKE_TILE5        2*20+19
#define NUKE_TILE6        3*20+17
#define NUKE_TILE7        3*20+18
#define NUKE_TILE8        3*20+19

#define GRASSLAND_TILES  1*20
#define DESERT_TILES     2*20
#define ARCTIC_TILES     3*20
#define JUNGLE_TILES     4*20
#define PLAINS_TILES     5*20
#define SWAMP_TILES      6*20
#define TUNDRA_TILES     7*20
#define ROAD_TILES       8*20

#define RIVER_TILES      9*20
#define OUTLET_TILES     9*20+16
#define OCEAN_TILES      10*20
#define HILLS_TILES      11*20
#define FOREST_TILES     11*20+4
#define MOUNTAINS_TILES  11*20+8
#define DENMARK_TILES    11*20+12

#define SPECIAL_TILES    12*20
#define S_TILE           12*20+13
#define G_TILE           12*20+14
#define M_TILE           12*20+15
#define P_TILE           12*20+16
#define R_TILE           12*20+17
#define I_TILE           12*20+18
#define F_TILE           12*20+19

#define RAIL_TILES       13*20
#define FLAG_TILES       14*20
#define CROSS_TILE       14*20+17
#define AUTO_TILE        14*20+18
#define PLUS_TILE        14*20+19

/*
#define UNIT_TILES       15*20
The tiles for the units are now stored in units.xpm
*/
extern int UNIT_TILES;

#define IRRIGATION_TILE  15*20+8
#define HILLMINE_TILE    15*20+9
#define DESERTMINE_TILE  15*20+10
#define POLLUTION_TILE   15*20+11
#define CITY_TILE        15*20+12
#define CITY_WALLS_TILE  15*20+13
#define HUT_TILE         15*20+14
#define FORTRESS_TILE    15*20+15

#define BORDER_TILES     16*20

#define NUMBER_TILES     17*20
#define NUMBER_MSD_TILES 17*20+9

#define SHIELD_NUMBERS   18*20
#define TRADE_NUMBERS    18*20+10

#define HP_BAR_TILES     19*20

#define CITY_FLASH_TILE  19*20+14
#define CITY_FOOD_TILES  19*20+15
#define CITY_MASK_TILES  19*20+17
#define CITY_SHIELD_TILE 19*20+19

#define FOOD_NUMBERS     20*20

#define BULB_TILES       21*20
#define GOVERNMENT_TILES 21*20+8
#define SUN_TILES        21*20+14
#define PEOPLE_TILES     21*20+22
#define RIGHT_ARROW_TILE 21*20+30

#define THUMB_TILES      21*20+31

int terrain_to_tile_map[13]= {
  ARCTIC_TILES, DESERT_TILES, FOREST_TILES, GRASSLAND_TILES,
  HILLS_TILES, JUNGLE_TILES, MOUNTAINS_TILES, OCEAN_TILES,
  PLAINS_TILES, RIVER_TILES, SWAMP_TILES, TUNDRA_TILES
};

extern Display	*display;
extern GC civ_gc, font_gc;
extern GC fill_bg_gc;
extern XFontStruct *main_font_struct;
extern Window root_window;

extern Widget map_canvas, info_command, turn_done_button;
extern Widget map_vertical_scrollbar, map_horizontal_scrollbar;
extern Widget overview_canvas, main_form, left_column_form;
extern Widget bulb_label, sun_label, government_label, timeout_label;
extern Widget unit_info_label;
extern Widget unit_pix_canvas, unit_below_canvas[4];
extern Widget more_arrow_label;
extern int display_depth;
extern Pixmap single_tile_pixmap;
/*extern Pixmap unit_below_pixmap[4];*/

extern int seconds_to_turndone;

extern int use_solid_color_behind_units;
extern int flags_are_transparent;

extern struct Sprite *intro_gfx_sprite;
extern struct Sprite *radar_gfx_sprite;

extern Pixmap map_canvas_store;
extern int map_canvas_store_twidth, map_canvas_store_theight;
extern Pixmap overview_canvas_store;
extern int overview_canvas_store_width, overview_canvas_store_height;

/* contains the x0, y0 coordinates of the upper left corner block */
int map_view_x0, map_view_y0;

/* used by map_canvas expose func */ 
int force_full_repaint;

extern int goto_state;

void pixmap_put_overlay_tile(Pixmap pixmap, int x, int y, int tileno);
void show_city_names(void);

/* the intro picture is held in this pixmap, which is scaled to
   the screen size */
Pixmap scaled_intro_pixmap;
int scaled_intro_pixmap_width, scaled_intro_pixmap_height;

/**************************************************************************
...
**************************************************************************/
void decrease_unit_hp_smooth(struct unit *punit0, int hp0, 
			     struct unit *punit1, int hp1)
{
  struct timeval tv;

  tv.tv_sec=0;
  tv.tv_usec=100;
  
  set_unit_focus_no_center(punit0);
  set_unit_focus_no_center(punit1);
  
  do {
    refresh_tile_mapcanvas(punit0->x, punit0->y, 1);
    refresh_tile_mapcanvas(punit1->x, punit1->y, 1);

    select(0, NULL, NULL, NULL, &tv);
    
    if(punit0->hp>hp0)
      punit0->hp--;
    if(punit1->hp>hp1)
      punit1->hp--;
  } while(punit0->hp>hp0 || punit1->hp>hp1);
  
  refresh_tile_mapcanvas(punit0->x, punit0->y, 1);
  refresh_tile_mapcanvas(punit1->x, punit1->y, 1);
}



/**************************************************************************
...
**************************************************************************/
void blink_active_unit(void)
{
  static int is_shown;
  struct unit *punit;
  
  if((punit=get_unit_in_focus())) {
    struct tile *ptile;
    ptile=map_get_tile(punit->x, punit->y);

    if(is_shown) {
      struct unit_list units;
      units=ptile->units;
      unit_list_init(&ptile->units);
      refresh_tile_mapcanvas(punit->x, punit->y, 1);
      ptile->units=units;
    }
    else {
      /* make sure that the blinking unit is always on the top */      
      unit_list_unlink(&ptile->units, punit);
      unit_list_insert(&ptile->units, punit);
      refresh_tile_mapcanvas(punit->x, punit->y, 1);
    }
      
    is_shown=!is_shown;
  }
}


/**************************************************************************
...
**************************************************************************/
void set_overview_dimensions(int x, int y)
{
  XtVaSetValues(overview_canvas, XtNwidth, 2*x, XtNheight, 2*y, NULL);
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

  sprintf(buffer, "%d", seconds_to_turndone);
  xaw_set_label(timeout_label, buffer);
}


/**************************************************************************
...
**************************************************************************/
void update_info_label(void)
{
  char buffer[512];
  
  sprintf(buffer, "%s People\nYear: %s\nGold: %d\nTax:%d Lux:%d Sci:%d",
	  int_to_text(civ_population(game.player_ptr)),
	  textyear(game.year),
	  game.player_ptr->economic.gold,
	  game.player_ptr->economic.tax,
	  game.player_ptr->economic.luxury,
	  game.player_ptr->economic.science);
  xaw_set_label(info_command, buffer);
  if (game.heating>7) game.heating=7;
  set_bulb_sol_government(8*game.player_ptr->research.researched/
			 (research_time(game.player_ptr)+1),
			 game.heating, 
			 game.player_ptr->government);

  update_timeout_label();
}


/**************************************************************************
...
**************************************************************************/
void update_unit_info_label(struct unit *punit)
{
  if(punit) {
    char buffer[512];
    struct city *pcity;
    pcity=city_list_find_id(&game.player_ptr->cities, punit->homecity);
    sprintf(buffer, "%s %s\n%s\n%s\n%s", 
	    get_unit_type(punit->type)->name,
	    (punit->veteran) ? "(veteran)" : "",
	    (goto_state==punit->id) ? 
	    "Select destination" : unit_activity_text(punit), 
	    map_get_tile_info_text(punit->x, punit->y),
	    pcity ? pcity->name : "");

    xaw_set_label(unit_info_label, buffer);
  }
  else
    xaw_set_label(unit_info_label, "");

  update_unit_pix_label(punit);
}

/**************************************************************************
...
**************************************************************************/
void update_unit_pix_label(struct unit *punit)
{
  int i;
  /* what initialises these statics? */
  static enum unit_activity uactivity = ACTIVITY_UNKNOWN;
  static int utemplate = U_LAST;
  static int unit_ids[4];
  static int showing_arrow=0;
  struct genlist_iterator myiter;
  
  if(punit) {
    if(punit->type!=utemplate || punit->activity!=uactivity) {
      put_unit_pixmap(punit, XawPixcommPixmap(unit_pix_canvas), 0, 0);
      xaw_expose_now(unit_pix_canvas);
      utemplate=punit->type;
      uactivity=punit->activity;
    }
    genlist_iterator_init(&myiter, 
			  &(map_get_tile(punit->x, punit->y)->units.list), 0);
    
    for(i=0; i<4 && ITERATOR_PTR(myiter); i++) {
      int id;
      id=ITERATOR_PTR(myiter) ? ((struct unit *)ITERATOR_PTR(myiter))->id : 0;
      if(id==punit->id) {
	ITERATOR_NEXT(myiter);
	i--;
	continue;
      }
      
      /* IS THIS INTENTIONAL?? - mjd */
      if(1 || unit_ids[i]!=id) {
	if(id) {
	  put_unit_pixmap((struct unit *)ITERATOR_PTR(myiter),
			  XawPixcommPixmap(unit_below_canvas[i]),
			  0, 0);
	  xaw_expose_now(unit_below_canvas[i]);
	}
	else
	  XawPixcommClear(unit_below_canvas[i]);
	  
	unit_ids[i]=id;
      }
      ITERATOR_NEXT(myiter);
    }

    
    for(; i<4; i++) {
      XawPixcommClear(unit_below_canvas[i]);
      unit_ids[i]=0;
    }

    
    if(ITERATOR_PTR(myiter)) {
      if(!showing_arrow) {
	xaw_set_bitmap(more_arrow_label, 
		       get_tile_sprite(RIGHT_ARROW_TILE)->pixmap);
	showing_arrow=1;
      }
    }
    else {
      if(showing_arrow) {
	xaw_set_bitmap(more_arrow_label, None);
	showing_arrow=0;
      }
    }
  }
  else {
    XawPixcommClear(unit_pix_canvas);
    utemplate=U_LAST;
    uactivity=ACTIVITY_UNKNOWN;
    for(i=0; i<4; i++) {
      XawPixcommClear(unit_below_canvas[i]);
      unit_ids[i]=0;
    }
    xaw_set_bitmap(more_arrow_label, None);
    showing_arrow=0;
  }
}


/**************************************************************************
...
**************************************************************************/
Pixmap get_thumb_pixmap(int onoff)
{
  return get_tile_sprite(THUMB_TILES+!onoff)->pixmap;
}

/**************************************************************************
...
**************************************************************************/
Pixmap get_citizen_pixmap(int frame)
{
  return get_tile_sprite(PEOPLE_TILES+frame)->pixmap;
}


/**************************************************************************
...
**************************************************************************/
void set_bulb_sol_government(int bulb, int sol, int government)
{
  if (bulb <0) bulb = 0;
  xaw_set_bitmap(bulb_label, get_tile_sprite(BULB_TILES+bulb)->pixmap);

  xaw_set_bitmap(sun_label, get_tile_sprite(SUN_TILES+sol)->pixmap);
  
  xaw_set_bitmap(government_label, 
		 get_tile_sprite(GOVERNMENT_TILES+government)->pixmap);
}

/**************************************************************************
...
**************************************************************************/
int map_canvas_adjust_x(int x)
{
  if(map_view_x0+map_canvas_store_twidth<=map.xsize)
     return x-map_view_x0;
  else if(x>=map_view_x0)
     return x-map_view_x0;
  else if(x<map_adjust_x(map_view_x0+map_canvas_store_twidth))
     return x+map.xsize-map_view_x0;

  return -1;
}

/**************************************************************************
...
**************************************************************************/
int map_canvas_adjust_y(int y)
{
  return y-map_view_y0;
}

/**************************************************************************
...
**************************************************************************/
void refresh_tile_mapcanvas(int x, int y, int write_to_screen)
{
  x=map_adjust_x(x);
  y=map_adjust_y(y);

  if(tile_visible_mapcanvas(x, y)) {
    update_map_canvas(map_canvas_adjust_x(x), 
		      map_canvas_adjust_y(y), 1, 1, write_to_screen);
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
  return (y>=map_view_y0+1 && y<map_view_y0+map_canvas_store_theight-1 &&
	  ((x>=map_view_x0+1 && x<map_view_x0+map_canvas_store_twidth-1) ||
	   (x+map.xsize>=map_view_x0+1 && 
	    x+map.xsize<map_view_x0+map_canvas_store_twidth-1)));
}

/**************************************************************************
...
**************************************************************************/
void move_unit_map_canvas(struct unit *punit, int x0, int y0, int dx, int dy)
{
  int i;
  int dest_x, dest_y;

  dest_x=map_adjust_x(x0+dx);
  dest_y=map_adjust_y(y0+dy);

  if(player_can_see_unit(game.player_ptr, punit) && (
     tile_visible_mapcanvas(x0, y0) || 
     tile_visible_mapcanvas(dest_x, dest_y))) {
    int x, y;

    put_unit_pixmap(punit, single_tile_pixmap, 0, 0);

    if(x0>=map_view_x0)
      x=(x0-map_view_x0)*NORMAL_TILE_WIDTH;
    else
      x=(map.xsize-map_view_x0+x0)*NORMAL_TILE_WIDTH;
    
    y=(y0-map_view_y0)*NORMAL_TILE_HEIGHT;

    for(i=0; i<NORMAL_TILE_WIDTH; i++) {
      if(dy>0)
	XCopyArea(display, map_canvas_store, XtWindow(map_canvas),
		  civ_gc, x, y, NORMAL_TILE_WIDTH, 1, x, y);
      else if(dy<0)
	XCopyArea(display, map_canvas_store, XtWindow(map_canvas),
		  civ_gc, x, y+NORMAL_TILE_HEIGHT-1, NORMAL_TILE_WIDTH, 1,
		  x, y+NORMAL_TILE_HEIGHT-1);
      
      if(dx>0)
	XCopyArea(display, map_canvas_store, XtWindow(map_canvas),
		  civ_gc, x, y, 1, NORMAL_TILE_HEIGHT, x, y);
      else if(dx<0)
	XCopyArea(display, map_canvas_store, XtWindow(map_canvas),
		  civ_gc, x+NORMAL_TILE_WIDTH-1, y, 1, NORMAL_TILE_HEIGHT,
		  x+NORMAL_TILE_WIDTH-1, y);

      x+=dx; y+=dy;
      
      XCopyArea(display, single_tile_pixmap, XtWindow(map_canvas),
		civ_gc, 0, 0, NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT, x, y);
      XSync(display, 0);
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void get_center_tile_mapcanvas(int *x, int *y)
{
  *x=map_adjust_x(map_view_x0+map_canvas_store_twidth/2);
  *y=map_adjust_y(map_view_y0+map_canvas_store_theight/2);
}

/**************************************************************************
...
**************************************************************************/
void center_tile_mapcanvas(int x, int y)
{
  int new_map_view_x0, new_map_view_y0;

  new_map_view_x0=map_adjust_x(x-map_canvas_store_twidth/2);
  new_map_view_y0=map_adjust_y(y-map_canvas_store_theight/2);
  if(new_map_view_y0>map.ysize-map_canvas_store_theight)
     new_map_view_y0=map_adjust_y(map.ysize-map_canvas_store_theight);

  map_view_x0=new_map_view_x0;
  map_view_y0=new_map_view_y0;

  update_map_canvas(0, 0, map_canvas_store_twidth,map_canvas_store_theight, 1);
  update_map_canvas_scrollbars();
  
  refresh_overview_viewrect();
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
void set_overview_tile_foreground_color(int x, int y)
{
  struct tile *ptile=map_get_tile(x, y);
  struct unit *punit;
  struct city *pcity;
  
  if(!ptile->known)
    XSetForeground(display, fill_bg_gc, colors_standard[COLOR_STD_BLACK]);
  else if((punit=unit_list_get(&ptile->units, 0))) {
    if(player_can_see_unit(game.player_ptr, punit)) {
      if(punit->owner==game.player_idx)
	XSetForeground(display, fill_bg_gc, colors_standard[COLOR_STD_YELLOW]);
      else
	XSetForeground(display, fill_bg_gc, colors_standard[COLOR_STD_RED]);
    }
  }
  else if((pcity=map_get_city(x, y))) {
    if(pcity->owner==game.player_idx)
      XSetForeground(display, fill_bg_gc, colors_standard[COLOR_STD_WHITE]);
    else
      XSetForeground(display, fill_bg_gc, colors_standard[COLOR_STD_CYAN]);
  }
  else if(ptile->terrain==T_OCEAN)
    XSetForeground(display, fill_bg_gc, colors_standard[COLOR_STD_OCEAN]);
  else
    XSetForeground(display, fill_bg_gc, colors_standard[COLOR_STD_GROUND]);
}



/**************************************************************************
...
**************************************************************************/
void refresh_overview_canvas(void)
{
  int x, y;
  
  for(y=0; y<map.ysize; y++)
    for(x=0; x<map.xsize; x++) {
      set_overview_tile_foreground_color(x, y);
      XFillRectangle(display, overview_canvas_store, fill_bg_gc, x*2, y*2, 
		     2, 2);
    }

  XSetForeground(display, fill_bg_gc, 0);
}


/**************************************************************************
...
**************************************************************************/
void overview_update_tile(int x, int y)
{
  int pos=x+map.xsize/2-(map_view_x0+map_canvas_store_twidth/2);
  
  if(pos>=map.xsize)
    pos-=map.xsize;
  
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
  Dimension height, width;
  int tile_width, tile_height;

  XtVaGetValues(w, XtNheight, &height, XtNwidth, &width, NULL);

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
    return;
  }
  if(scaled_intro_pixmap) {
    XFreePixmap(display, scaled_intro_pixmap);
    scaled_intro_pixmap=0; scaled_intro_pixmap_height=0;
  };
  
  if(map.xsize) { /* do we have a map at all */
    int tile_width=(width+NORMAL_TILE_WIDTH-1)/NORMAL_TILE_WIDTH;
    int tile_height=(height+NORMAL_TILE_HEIGHT-1)/NORMAL_TILE_HEIGHT;

    if(map_canvas_store_twidth !=tile_width ||
       map_canvas_store_theight!=tile_height) { /* resized? */
      
      XFreePixmap(display, map_canvas_store);
      
      map_canvas_store_twidth=tile_width;
      map_canvas_store_theight=tile_height;

      
      map_canvas_store=XCreatePixmap(display, XtWindow(map_canvas), 
				     tile_width*NORMAL_TILE_WIDTH,
				     tile_height*NORMAL_TILE_WIDTH,
 				     display_depth);
				     
      XFillRectangle(display, map_canvas_store, fill_bg_gc, 0, 0, 
		     NORMAL_TILE_WIDTH*map_canvas_store_twidth,
		     NORMAL_TILE_HEIGHT*map_canvas_store_theight);
      
      update_map_canvas(0, 0, map_canvas_store_twidth,
			map_canvas_store_theight, 1);
      
      update_map_canvas_scrollbars();
      refresh_overview_viewrect();

    }
    else {
      XCopyArea(display, map_canvas_store, XtWindow(map_canvas),
		civ_gc,
		event->xexpose.x, event->xexpose.y,
		event->xexpose.width, event->xexpose.height,
		event->xexpose.x, event->xexpose.y);
    }
  }
  refresh_overview_canvas();
}

/**************************************************************************
...
**************************************************************************/
void update_map_canvas(int tile_x, int tile_y, int width, int height, 
		       int write_to_screen)
{
  int x, y;

  for(y=tile_y; y<tile_y+height; y++)
    for(x=tile_x; x<tile_x+width; x++)
      pixmap_put_tile(map_canvas_store, x, y, 
		      (map_view_x0+x)%map.xsize, map_view_y0+y, 0);

  if(write_to_screen) {
    XCopyArea(display, map_canvas_store, XtWindow(map_canvas), 
	      civ_gc, 
	      tile_x*NORMAL_TILE_WIDTH, 
	      tile_y*NORMAL_TILE_HEIGHT, 
	      width*NORMAL_TILE_WIDTH,
	      height*NORMAL_TILE_WIDTH,
	      tile_x*NORMAL_TILE_WIDTH, 
	      tile_y*NORMAL_TILE_HEIGHT);

    if(width==map_canvas_store_twidth && height==map_canvas_store_theight) {
      show_city_names();
    }
    
  }
}

/**************************************************************************
...
**************************************************************************/
void update_map_canvas_scrollbars(void)
{
  float shown_h=(float)map_canvas_store_twidth/(float)map.xsize;
  float top_h=(float)map_view_x0/(float)map.xsize;
  float shown_v=(float)map_canvas_store_theight/(float)map.ysize;
  float top_v=(float)map_view_y0/(float)map.ysize;

  my_XawScrollbarSetThumb(map_horizontal_scrollbar, top_h, shown_h);
  my_XawScrollbarSetThumb(map_vertical_scrollbar, top_v, shown_v);
}


/**************************************************************************
...
**************************************************************************/
void show_city_names(void)
{
  int x, y;
  
  for(y=0; y<map_canvas_store_theight; ++y) { 
    int ry=map_view_y0+y;
    for(x=0; x<map_canvas_store_twidth; ++x) { 
      int rx=(map_view_x0+x)%map.xsize;
      struct city *pcity;
      if((pcity=map_get_city(rx, ry))) {
	int w=XTextWidth(main_font_struct, pcity->name, strlen(pcity->name));
	
	XDrawString(display, XtWindow(map_canvas), font_gc, 
		    x*NORMAL_TILE_WIDTH+NORMAL_TILE_WIDTH/2-w/2, 
		    y*NORMAL_TILE_HEIGHT+3*NORMAL_TILE_HEIGHT/2,
		    pcity->name, strlen(pcity->name));
      }
    }
  }
}



/**************************************************************************
...
**************************************************************************/
void put_city_pixmap(struct city *pcity, Pixmap pm, int xtile, int ytile)
{
  struct Sprite *mysprite;

  if(use_solid_color_behind_units) {
    XSetForeground(display, fill_bg_gc, colors_standard[COLOR_STD_RACE0+
                   game.players[pcity->owner].race]);
    XFillRectangle(display, pm, fill_bg_gc, 
		   xtile*NORMAL_TILE_WIDTH, ytile*NORMAL_TILE_HEIGHT, 
		   NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
  }
  else {
    mysprite=get_tile_sprite(game.players[pcity->owner].race+FLAG_TILES);
    XCopyArea(display, mysprite->pixmap, pm, civ_gc, 0, 0,
	      mysprite->width, mysprite->height, 
	      xtile*NORMAL_TILE_WIDTH, ytile*NORMAL_TILE_HEIGHT);
  }
  
  pixmap_put_overlay_tile(pm, xtile, ytile, CITY_TILE+
			  city_got_citywalls(pcity));
  
  if(pcity->size>=10)
    pixmap_put_overlay_tile(pm, xtile, ytile, NUMBER_MSD_TILES+
			    pcity->size/10);
  pixmap_put_overlay_tile(pm, xtile, ytile, NUMBER_TILES+pcity->size%10);

  if(city_unhappy(pcity))
    pixmap_put_overlay_tile(pm, xtile, ytile, CITY_FLASH_TILE);
  

}


/**************************************************************************
...
**************************************************************************/
void put_city_tile_output(Pixmap pm, int x, int y, 
			  int food, int shield, int trade)
{
  
  pixmap_put_overlay_tile(pm, x, y,  FOOD_NUMBERS+food);
  pixmap_put_overlay_tile(pm, x, y, SHIELD_NUMBERS+shield);
  pixmap_put_overlay_tile(pm, x, y,  TRADE_NUMBERS+trade);
}


/**************************************************************************
...
**************************************************************************/
void put_unit_pixmap(struct unit *punit, Pixmap pm, int xtile, int ytile)
{
  struct Sprite *mysprite;

  if(use_solid_color_behind_units) {
    XSetForeground(display, fill_bg_gc, colors_standard[COLOR_STD_RACE0+
		   game.players[punit->owner].race]);
    XFillRectangle(display, pm, fill_bg_gc, 
		   xtile*NORMAL_TILE_WIDTH, ytile*NORMAL_TILE_HEIGHT, 
		   NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
  }
  else {
    if(flags_are_transparent) {
      pixmap_put_overlay_tile(pm, xtile, ytile, 
			      game.players[punit->owner].race+FLAG_TILES);
    } else {
      mysprite=get_tile_sprite(game.players[punit->owner].race+FLAG_TILES);
      XCopyArea(display, mysprite->pixmap, pm, civ_gc, 0, 0,
		mysprite->width, mysprite->height, 
		xtile*NORMAL_TILE_WIDTH, ytile*NORMAL_TILE_HEIGHT);
    };
  }
    
  pixmap_put_overlay_tile(pm, xtile, ytile, get_unit_type(punit->type)->graphics+UNIT_TILES);

  if(punit->activity!=ACTIVITY_IDLE) {
    int tileno=0;
    switch(punit->activity) {
    case ACTIVITY_MINE:
      tileno=M_TILE;
      break;
    case ACTIVITY_POLLUTION:
     case ACTIVITY_PILLAGE:
      tileno=P_TILE;
      break;
    case ACTIVITY_ROAD:
    case ACTIVITY_RAILROAD:
      tileno=R_TILE;
      break;
    case ACTIVITY_IRRIGATE:
      tileno=I_TILE;
      break;
    case ACTIVITY_FORTIFY:
    case ACTIVITY_FORTRESS:
      tileno=F_TILE;
      break;
    case ACTIVITY_SENTRY:
      tileno=S_TILE;
      break;
     case ACTIVITY_GOTO:
      tileno=G_TILE;
      break;
     default:
      break;
    }

    pixmap_put_overlay_tile(pm, xtile, ytile, tileno);
  }

  if(punit->ai.control)
    pixmap_put_overlay_tile(pm, xtile, ytile, AUTO_TILE);

  pixmap_put_overlay_tile(pm, xtile, ytile, HP_BAR_TILES+
			  (11*(get_unit_type(punit->type)->hp-punit->hp))/
			  (get_unit_type(punit->type)->hp));
}


/**************************************************************************
...
**************************************************************************/
void put_unit_pixmap_city_overlays(struct unit *punit, Pixmap pm, 
				   int unhappiness, int upkeep)
{
  XSetForeground(display, fill_bg_gc, colors_standard[COLOR_STD_WHITE]);
  XFillRectangle(display, pm, fill_bg_gc, 0, NORMAL_TILE_WIDTH, 
		 NORMAL_TILE_HEIGHT, NORMAL_TILE_HEIGHT+SMALL_TILE_HEIGHT);

  if(upkeep) {
    if(unit_flag(punit->type, F_SETTLERS)) {
      pixmap_put_overlay_tile(pm, 0, 1, CITY_FOOD_TILES+upkeep-1);
    }
    else
      pixmap_put_overlay_tile(pm, 0, 1, CITY_SHIELD_TILE);
  }
  
  if(unhappiness)
    pixmap_put_overlay_tile(pm, 0, 1, CITY_MASK_TILES+unhappiness-1);
}


/**************************************************************************
...
**************************************************************************/
void put_nuke_mushroom_pixmaps(int abs_x0, int abs_y0)
{
  int i, x, y;
  static int nuke_tiles[9]= {
    NUKE_TILE0, NUKE_TILE1, NUKE_TILE2,
    NUKE_TILE3, NUKE_TILE4, NUKE_TILE5,
    NUKE_TILE6, NUKE_TILE7, NUKE_TILE8,
  };
  
  
  for(i=0, y=0; y<3; y++)
    for(x=0; x<3; x++) {
     struct Sprite *mysprite=get_tile_sprite(nuke_tiles[i++]);
      XCopyArea(display, mysprite->pixmap, XtWindow(map_canvas), 
		civ_gc, 
		0, 0, 
		NORMAL_TILE_WIDTH,
		NORMAL_TILE_HEIGHT,
		(x-1+abs_x0-map_view_x0)*NORMAL_TILE_WIDTH, 
		(y-1+abs_y0-map_view_y0)*NORMAL_TILE_HEIGHT);
    }

  XSync(display, 0);
  sleep(1);

  update_map_canvas(abs_x0-map_view_x0-1, abs_y0-map_view_y0-1,
		    3, 3, 1);
}

/**************************************************************************
...
**************************************************************************/
void pixmap_put_black_tile(Pixmap pm, int x, int y)
{
  XSetForeground(display, fill_bg_gc, colors_standard[COLOR_STD_BLACK]);

  XFillRectangle(display, pm, fill_bg_gc,  
		 x*NORMAL_TILE_WIDTH, y*NORMAL_TILE_HEIGHT,
		 NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
}
		     

/**************************************************************************
...
**************************************************************************/
void pixmap_frame_tile_red(Pixmap pm, int x, int y)
{
  XSetForeground(display, fill_bg_gc, colors_standard[COLOR_STD_RED]);

  XDrawRectangle(display, pm, fill_bg_gc,  
		 x*NORMAL_TILE_WIDTH, y*NORMAL_TILE_HEIGHT,
		 NORMAL_TILE_WIDTH-1, NORMAL_TILE_HEIGHT-1);
  XDrawRectangle(display, pm, fill_bg_gc,  
		 x*NORMAL_TILE_WIDTH+1, y*NORMAL_TILE_HEIGHT+1,
		 NORMAL_TILE_WIDTH-3, NORMAL_TILE_HEIGHT-3);
  XDrawRectangle(display, pm, fill_bg_gc,  
		 x*NORMAL_TILE_WIDTH+2, y*NORMAL_TILE_HEIGHT+2,
		 NORMAL_TILE_WIDTH-5, NORMAL_TILE_HEIGHT-5);
}



/**************************************************************************
...
**************************************************************************/
void pixmap_put_tile(Pixmap pm, int x, int y, int abs_x0, int abs_y0, 
		     int citymode)
{
  int ttype, ttype_north, ttype_south, ttype_east, ttype_west;
  int ttype_north_east, ttype_south_east, ttype_south_west, ttype_north_west;
  int tspecial, tspecial_north, tspecial_south, tspecial_east, tspecial_west;

  int tileno,n;
  struct tile *ptile;
  struct Sprite *mysprite;
  struct unit *punit;
  struct city *pcity;

  ptile=map_get_tile(abs_x0, abs_y0);
  punit=get_unit_in_focus();
  
  if(abs_y0>=map.ysize || ptile->known<TILE_KNOWN) {
    XSetForeground(display, fill_bg_gc, colors_standard[COLOR_STD_BLACK]);

    XFillRectangle(display, pm, fill_bg_gc,  
		   x*NORMAL_TILE_WIDTH, y*NORMAL_TILE_HEIGHT,
		   NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
    return;
  }

  if((pcity=map_get_city(abs_x0, abs_y0)) &&
     (citymode || !punit || punit->x!=abs_x0 || punit->y!=abs_y0 ||
      unit_list_size(&ptile->units)==0)) {
      put_city_pixmap(pcity, pm, x, y);
      return;
  }
  
  if(!flags_are_transparent)
    if((n=unit_list_size(&ptile->units))>0)
      if(!citymode || unit_list_get(&ptile->units, 0)->owner!=game.player_idx) {
	if(player_can_see_unit(game.player_ptr, unit_list_get(&ptile->units, 0))) {
	  put_unit_pixmap(unit_list_get(&ptile->units, 0), pm, x, y);
	  if(n>1)  pixmap_put_overlay_tile(pm, x, y, PLUS_TILE);
	  return;
	}
      }
  
  ttype=map_get_terrain(abs_x0, abs_y0);

  ttype_north=map_get_terrain(abs_x0, abs_y0-1);
  ttype_south=map_get_terrain(abs_x0, abs_y0+1);
  ttype_west=map_get_terrain(abs_x0-1, abs_y0);
  ttype_east=map_get_terrain(abs_x0+1, abs_y0);
  ttype_north_east=map_get_terrain(abs_x0+1, abs_y0-1);
  ttype_south_east=map_get_terrain(abs_x0+1, abs_y0+1);
  ttype_south_west=map_get_terrain(abs_x0-1, abs_y0+1);
  ttype_north_west=map_get_terrain(abs_x0-1, abs_y0-1);
  tspecial=map_get_special(abs_x0, abs_y0);


  tileno=terrain_to_tile_map[ttype];

  switch(ttype) {
  case T_OCEAN:
    tileno+=(ttype_north!=T_OCEAN) ? 1 : 0;
    tileno+=(ttype_east!=T_OCEAN)  ? 2 : 0;
    tileno+=(ttype_south!=T_OCEAN) ? 4 : 0;
    tileno+=(ttype_west!=T_OCEAN)  ? 8 : 0;
    break;

  case T_RIVER:
    tileno=RIVER_TILES;
    tileno+=(ttype_north==T_RIVER || ttype_north==T_OCEAN) ? 1 : 0;
    tileno+=(ttype_east==T_RIVER  || ttype_east== T_OCEAN) ? 2 : 0;
    tileno+=(ttype_south==T_RIVER || ttype_south==T_OCEAN) ? 4 : 0;
    tileno+=(ttype_west==T_RIVER  || ttype_west== T_OCEAN) ? 8 : 0;
    break;

  case T_MOUNTAINS:
  case T_HILLS:
  case T_FOREST:
    if(ttype_west==ttype && ttype_east==ttype)
      tileno+=2;
    else if(ttype_west==ttype)
      tileno+=3;
    else if(ttype_east==ttype)
      tileno++;
    break;

  case T_GRASSLAND:
  case T_DESERT:
  case T_ARCTIC:
  case T_JUNGLE:
  case T_PLAINS:
  case T_SWAMP:
  case T_TUNDRA:
    tileno+=(ttype_north!=ttype) ? 1 : 0;
    tileno+=(ttype_east!=ttype)  ? 2 : 0;
    tileno+=(ttype_south!=ttype) ? 4 : 0;
    tileno+=(ttype_west!=ttype)  ? 8 : 0;
    break;


  }

  if(map.is_earth && abs_x0>=34 && abs_x0<=36 && abs_y0>=12 && abs_y0<=13) 
    tileno=(abs_y0-12)*3+abs_x0-34+DENMARK_TILES;

  mysprite=get_tile_sprite(tileno);
  
  XCopyArea(display, mysprite->pixmap, pm, 
	    civ_gc, 0, 0,
	    mysprite->width, mysprite->height, 
	    x*NORMAL_TILE_WIDTH, y*NORMAL_TILE_HEIGHT);


  if(ttype==T_OCEAN) {
    tileno=CORNER_TILES-1;
      if((ttype_north==T_OCEAN && ttype_east==T_OCEAN && 
	  ttype_north_east!=T_OCEAN))
	tileno+=1;
      if((ttype_east==T_OCEAN && ttype_south==T_OCEAN && 
	  ttype_south_east!=T_OCEAN))
	tileno+=2;
      if((ttype_south==T_OCEAN && ttype_west==T_OCEAN && 
	  ttype_south_west!=T_OCEAN))
	tileno+=4;
      if((ttype_north==T_OCEAN && ttype_west==T_OCEAN && 
	  ttype_north_west!=T_OCEAN))
	tileno+=8;

    if(tileno!=CORNER_TILES-1)
      pixmap_put_overlay_tile(pm, x, y, tileno);

    if(ttype_north==T_RIVER)
      pixmap_put_overlay_tile(pm, x, y, OUTLET_TILES+0);
    if(ttype_west==T_RIVER)
      pixmap_put_overlay_tile(pm, x, y, OUTLET_TILES+1);
    if(ttype_south==T_RIVER)
      pixmap_put_overlay_tile(pm, x, y, OUTLET_TILES+2);
    if(ttype_east==T_RIVER)
      pixmap_put_overlay_tile(pm, x, y, OUTLET_TILES+3);

  }  

  if(tspecial & S_IRRIGATION)
    pixmap_put_overlay_tile(pm, x, y, IRRIGATION_TILE);

  if(tspecial & S_MINE) {
    if(ttype==T_HILLS || ttype==T_MOUNTAINS)
      pixmap_put_overlay_tile(pm, x, y, HILLMINE_TILE);
    else /* desert */
      pixmap_put_overlay_tile(pm, x, y, DESERTMINE_TILE);
  }

  if((tspecial & S_ROAD) || (tspecial & S_RAILROAD)) {
    tspecial_north=map_get_special(abs_x0, abs_y0-1);
    tspecial_east=map_get_special(abs_x0+1, abs_y0);
    tspecial_south=map_get_special(abs_x0, abs_y0+1);
    tspecial_west=map_get_special(abs_x0-1, abs_y0);

    if(tspecial & S_ROAD) {
      tileno=ROAD_TILES;
      tileno+=(tspecial_north&S_ROAD) ? 1 : 0;
      tileno+=(tspecial_east&S_ROAD)  ? 2 : 0;
      tileno+=(tspecial_south&S_ROAD) ? 4 : 0;
      tileno+=(tspecial_west&S_ROAD)  ? 8 : 0;
      pixmap_put_overlay_tile(pm, x, y, tileno);
    }

    if(tspecial & S_RAILROAD) {
      tileno=RAIL_TILES;
      tileno+=(tspecial_north&S_RAILROAD) ? 1 : 0;
      tileno+=(tspecial_east&S_RAILROAD)  ? 2 : 0;
      tileno+=(tspecial_south&S_RAILROAD) ? 4 : 0;
      tileno+=(tspecial_west&S_RAILROAD)  ? 8 : 0;
      pixmap_put_overlay_tile(pm, x, y, tileno);
    }

  }

  if(tspecial & S_SPECIAL)
     pixmap_put_overlay_tile(pm, x, y, SPECIAL_TILES+ttype);

  if(tspecial & S_HUT)
    pixmap_put_overlay_tile(pm, x, y, HUT_TILE);
    
  if(tspecial & S_FORTRESS)
    pixmap_put_overlay_tile(pm, x, y, FORTRESS_TILE);

  if(tspecial & S_POLLUTION)
    pixmap_put_overlay_tile(pm, x, y, POLLUTION_TILE);

  if(!citymode) {
    tileno=BORDER_TILES;
    if(tile_is_known(abs_x0, abs_y0-1)==TILE_UNKNOWN)
      tileno+=1;
    if(tile_is_known(abs_x0+1, abs_y0)==TILE_UNKNOWN)
      tileno+=2;
    if(tile_is_known(abs_x0, abs_y0+1)==TILE_UNKNOWN)
      tileno+=4;
    if(tile_is_known(abs_x0-1, abs_y0)==TILE_UNKNOWN)
      tileno+=8;
    if(tileno!=BORDER_TILES)
      pixmap_put_overlay_tile(pm, x, y, tileno);
  }

  if(flags_are_transparent)
    if((n=unit_list_size(&ptile->units))>0)
      if(!citymode || unit_list_get(&ptile->units, 0)->owner!=game.player_idx) {
	if(player_can_see_unit(game.player_ptr, unit_list_get(&ptile->units, 0))) {
	  put_unit_pixmap(unit_list_get(&ptile->units, 0), pm, x, y);
	  if(n>1)  pixmap_put_overlay_tile(pm, x, y, PLUS_TILE);
	}
      }

}


/**************************************************************************
...
**************************************************************************/
void pixmap_put_overlay_tile(Pixmap pixmap, int x, int y, int tileno)
{
  struct Sprite *ssprite=get_tile_sprite(tileno);
      
  XSetClipOrigin(display, civ_gc, x*NORMAL_TILE_WIDTH, y*NORMAL_TILE_HEIGHT);
  XSetClipMask(display, civ_gc, ssprite->mask);
      
  XCopyArea(display, ssprite->pixmap, pixmap, 
	    civ_gc, 0, 0,
	    ssprite->width, ssprite->height, 
	    x*NORMAL_TILE_WIDTH, y*NORMAL_TILE_HEIGHT);
  XSetClipMask(display, civ_gc, None); 
}

/**************************************************************************
 Draws a cross-hair overlay on a tile
**************************************************************************/
void put_cross_overlay_tile(int x,int y)
{
  x=map_adjust_x(x);
  y=map_adjust_y(y);

  if(tile_visible_mapcanvas(x, y)) {
    pixmap_put_overlay_tile(XtWindow(map_canvas),map_canvas_adjust_x(x),
                            map_canvas_adjust_y(y),CROSS_TILE);
  }
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
    map_view_y0=percent*map.ysize;
    map_view_y0=(map_view_y0<0) ? 0 : map_view_y0;
    map_view_y0=(map_view_y0>map.ysize-map_canvas_store_theight) ? 
       map.ysize-map_canvas_store_theight : map_view_y0;
  }

  update_map_canvas(0, 0, map_canvas_store_twidth, map_canvas_store_theight, 1);
  update_map_canvas_scrollbars();
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
    if(position>0 && map_view_y0<map.ysize-map_canvas_store_theight)
      map_view_y0++;
    else if(position<0 && map_view_y0>0)
      map_view_y0--;
  }

  map_view_x0=map_adjust_x(map_view_x0);
  map_view_y0=map_adjust_y(map_view_y0);


  update_map_canvas(0, 0, map_canvas_store_twidth, map_canvas_store_theight, 1);
  update_map_canvas_scrollbars();
  refresh_overview_viewrect();
}



/**************************************************************************
couldn't get the usual XawScrollbarSetThumb to work. tried everything.
someone please tell me why - pu
**************************************************************************/
void my_XawScrollbarSetThumb(Widget w, float top, float shown)
{
  Arg arglist[2];

  if(sizeof(float)>sizeof(XtArgVal)) {
    XtSetArg(arglist[0], XtCTopOfThumb, &top);
    XtSetArg(arglist[1], XtNshown, &shown);
   }
  else {
    XtArgVal *l_top=(XtArgVal*)&top;
    XtArgVal *l_shown=(XtArgVal*)&shown; 
    XtSetArg(arglist[0], XtNtopOfThumb, *l_top);
    XtSetArg(arglist[1], XtNshown, *l_shown);
   }
  XtSetValues(w, arglist, 2);
}
