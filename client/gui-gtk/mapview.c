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

#include <stdio.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <gtk/gtk.h>

#include <gdk_imlib.h>

#include "fcintl.h"
#include "game.h"
#include "government.h"		/* government_graphic() */
#include "map.h"
#include "shared.h"		/* myusleep() */

#include "civclient.h"
#include "colors.h"
#include "control.h" /* set_unit_focus_no_center and get_unit_in_focus */
#include "graphics.h"
#include "gui_stuff.h"
#include "mapctrl.h"
#include "tilespec.h"

#include "mapview.h"

extern int		seconds_to_turndone;

/* adjusted depending on tile size: */
int num_units_below = MAX_NUM_UNITS_BELOW;

extern int goto_state;

extern GtkWidget *	main_frame_civ_name;
extern GtkWidget *	main_label_info;
extern int		use_solid_color_behind_units;
extern int		flags_are_transparent;
extern int		ai_manual_turn_done;
extern int		draw_diagonal_roads;
extern int		draw_map_grid;
extern GtkWidget *	econ_label			[10];
extern GtkWidget *	bulb_label;
extern GtkWidget *	sun_label;
extern GtkWidget *	government_label;
extern GtkWidget *	timeout_label;
extern GtkWidget *	turn_done_button;

extern GtkWidget *	unit_info_frame, *unit_info_label;

extern GtkWidget *	unit_pixmap;
extern GtkWidget *	unit_below_pixmap		[MAX_NUM_UNITS_BELOW];
extern GtkWidget *	more_arrow_pixmap;

extern GtkWidget *	map_canvas;			/* GtkDrawingArea */
extern GtkWidget *	overview_canvas;		/* GtkDrawingArea */

extern GtkWidget *	map_vertical_scrollbar, *map_horizontal_scrollbar;

/* this pixmap acts as a backing store for the map_canvas widget */
extern GdkPixmap *	map_canvas_store;
extern int		map_canvas_store_twidth, map_canvas_store_theight;

/* this pixmap acts as a backing store for the overview_canvas widget */
extern GdkPixmap *	overview_canvas_store;
extern int		overview_canvas_store_width, overview_canvas_store_height;

/* this pixmap is used when moving units etc */
extern GdkPixmap *	single_tile_pixmap;
extern int		single_tile_pixmap_width, single_tile_pixmap_height;

extern GdkPixmap *gray50,*gray25;
extern int city_workers_color;

/* contains the x0, y0 coordinates of the upper left corner block */
int map_view_x0, map_view_y0;

/* used by map_canvas expose func */ 
int force_full_repaint;


extern GtkWidget *	toplevel;
extern GdkWindow *	root_window;

extern GdkGC *		civ_gc;
extern GdkGC *		fill_bg_gc;
extern GdkGC *		fill_tile_gc;

extern GdkGC *		mask_fg_gc;
extern GdkGC *		mask_bg_gc;

extern GdkFont *	main_font;


static void pixmap_put_overlay_tile(GdkPixmap *pixmap,
				    int x, int y, struct Sprite *ssprite);
static void put_overlay_tile_gpixmap(GtkPixcomm *pixmap,
				     int x, int y, struct Sprite *ssprite);
static void show_city_names(void);


/* the intro picture is held in this pixmap, which is scaled to
   the screen size */
GdkPixmap *scaled_intro_pixmap;
int scaled_intro_pixmap_width, scaled_intro_pixmap_height;

extern SPRITE *intro_gfx_sprite;
extern SPRITE *radar_gfx_sprite;
extern GdkCursor *goto_cursor;

GtkObject *		map_hadj, *map_vadj;



/**************************************************************************
...
**************************************************************************/
void decrease_unit_hp_smooth(struct unit *punit0, int hp0, 
			     struct unit *punit1, int hp1)
{
  set_unit_focus_no_center(punit0);
  set_unit_focus_no_center(punit1);
  
  do {
    refresh_tile_mapcanvas(punit0->x, punit0->y, 1);
    refresh_tile_mapcanvas(punit1->x, punit1->y, 1);
    myusleep(100);

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
  gtk_widget_set_usize(overview_canvas, 2*x, 2*y);

  overview_canvas_store_width=2*x;
  overview_canvas_store_height=2*y;

  if(overview_canvas_store)
    gdk_pixmap_unref(overview_canvas_store);
  
  overview_canvas_store	= gdk_pixmap_new(root_window,
			  overview_canvas_store_width,
			  overview_canvas_store_height, -1);
}


/**************************************************************************
...
**************************************************************************/
void update_turn_done_button(int do_restore)
{
   static int flip;
   GdkGC      *fore, *back;
 
   if(game.player_ptr->ai.control && !ai_manual_turn_done)
     return;
   if((do_restore && flip) || !do_restore)
   { 
	
	fore = turn_done_button->style->bg_gc[GTK_STATE_NORMAL];
	back = turn_done_button->style->light_gc[GTK_STATE_NORMAL];

	turn_done_button->style->bg_gc[GTK_STATE_NORMAL] = back;
	turn_done_button->style->light_gc[GTK_STATE_NORMAL] = fore;

	gtk_expose_now(turn_done_button);

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
  gtk_set_label(timeout_label, buffer);
}

/**************************************************************************
...
**************************************************************************/
void update_info_label( void )
{
  char buffer	[512];
  int  d;

  gtk_frame_set_label( GTK_FRAME( main_frame_civ_name ), get_nation_name(game.player_ptr->nation) );

  sprintf( buffer, _("Population: %s\nYear: %s\nGold %d\nTax: %d Lux: %d Sci: %d"),
  	  int_to_text( civ_population( game.player_ptr ) ),
  	  textyear( game.year ),
  	  game.player_ptr->economic.gold,
  	  game.player_ptr->economic.tax,
  	  game.player_ptr->economic.luxury,
  	  game.player_ptr->economic.science );

  gtk_set_label(main_label_info, buffer);

  if (game.heating>7) game.heating=7;
  set_bulb_sol_government(8*game.player_ptr->research.researched/
                         (research_time(game.player_ptr)+1),
                         game.heating, 
                         game.player_ptr->government);


  d=0;
  for(;d<(game.player_ptr->economic.luxury)/10;d++)
    gtk_pixmap_set(GTK_PIXMAP(econ_label[d]),
    get_citizen_pixmap(0), NULL ); /* elvis tile */
 
  for(;d<(game.player_ptr->economic.science+game.player_ptr->economic.luxury)/10;d++)
    gtk_pixmap_set(GTK_PIXMAP(econ_label[d]),
    get_citizen_pixmap(1), NULL ); /* scientist tile */
 
   for(;d<10;d++)
    gtk_pixmap_set(GTK_PIXMAP(econ_label[d]),
    get_citizen_pixmap(2), NULL ); /* taxman tile */
 
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

    sprintf(buffer, "%s %s", 
            get_unit_type(punit->type)->name,
            (punit->veteran) ? _("(veteran)") : "" );
    gtk_frame_set_label( GTK_FRAME(unit_info_frame), buffer);

    sprintf(buffer, "%s\n%s\n%s", 
            (goto_state==punit->id) ? 
            _("Select destination") : unit_activity_text(punit), 
            map_get_tile_info_text(punit->x, punit->y),
            pcity ? pcity->name : "");
    gtk_set_label( unit_info_label, buffer);

    if (goto_cursor != NULL) {
      if (goto_state==punit->id)
       gdk_window_set_cursor (root_window, goto_cursor);
      else
       gdk_window_set_cursor (root_window, 0);
    }
  }
  else
  {
    gtk_frame_set_label( GTK_FRAME(unit_info_frame),"");
    gtk_set_label(unit_info_label,"\n\n");
  }
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
  static int unit_ids[MAX_NUM_UNITS_BELOW];
  static int showing_arrow=0;
  struct genlist_iterator myiter;
  
  if(punit) {
    if(punit->type!=utemplate || punit->activity!=uactivity) {
      gtk_pixcomm_clear(GTK_PIXCOMM(unit_pixmap), FALSE); /* STG */
      put_unit_gpixmap(punit, GTK_PIXCOMM(unit_pixmap), 0, 0);
      utemplate=punit->type;
      uactivity=punit->activity;
    }
    genlist_iterator_init(&myiter, 
			  &(map_get_tile(punit->x, punit->y)->units.list), 0);
    
    for(i=0; i<num_units_below && ITERATOR_PTR(myiter); i++) {
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
	  gtk_pixcomm_clear(GTK_PIXCOMM(unit_below_pixmap[i]), FALSE); /* STG */
	  put_unit_gpixmap((struct unit *)ITERATOR_PTR(myiter),
			  GTK_PIXCOMM(unit_below_pixmap[i]),
			  0, 0);
	}
	else
	  gtk_pixcomm_clear(GTK_PIXCOMM(unit_below_pixmap[i]), TRUE);

	  
	unit_ids[i]=id;
      }
      ITERATOR_NEXT(myiter);
    }

    
    for(; i<num_units_below; i++) {
      gtk_pixcomm_clear(GTK_PIXCOMM(unit_below_pixmap[i]), TRUE);
      unit_ids[i]=0;
    }

    
    if(ITERATOR_PTR(myiter)) {
      if(!showing_arrow) {
	gtk_widget_show(more_arrow_pixmap);
	showing_arrow=1;
      }
    }
    else {
      if(showing_arrow) {
	gtk_widget_hide(more_arrow_pixmap);
	showing_arrow=0;
      }
    }
  }
  else {
    gtk_pixcomm_clear(GTK_PIXCOMM(unit_pixmap), TRUE);
    utemplate=U_LAST;
    uactivity=ACTIVITY_UNKNOWN;
    for(i=0; i<num_units_below; i++) {
      gtk_pixcomm_clear(GTK_PIXCOMM(unit_below_pixmap[i]), TRUE);
      unit_ids[i]=0;
    }
    gtk_widget_hide(more_arrow_pixmap);
    showing_arrow=0;
  }
}

/**************************************************************************
...
**************************************************************************/
GdkPixmap *get_thumb_pixmap(int onoff)
{
  return sprites.treaty_thumb[!!onoff]->pixmap;
}

/**************************************************************************
...
**************************************************************************/
GdkPixmap *get_citizen_pixmap(int frame)
{
  frame = CLIP(0, frame, NUM_TILES_CITIZEN-1);
  return sprites.citizen[frame]->pixmap;
}


/**************************************************************************
...
**************************************************************************/
SPRITE *get_citizen_sprite(int frame)
{
  frame = CLIP(0, frame, NUM_TILES_CITIZEN-1);
  return sprites.citizen[frame];
}


/**************************************************************************
...
**************************************************************************/
void set_bulb_sol_government(int bulb, int sol, int government)
{
  struct Sprite *gov_sprite;
  
  bulb = CLIP(0, bulb, NUM_TILES_PROGRESS-1);
  sol  = CLIP(0, sol, NUM_TILES_PROGRESS-1);

  gtk_pixmap_set(GTK_PIXMAP(bulb_label), sprites.bulb[bulb]->pixmap, NULL);

  gtk_pixmap_set(GTK_PIXMAP(sun_label), sprites.warming[sol]->pixmap, NULL);
  
  if (game.government_count==0) {
    /* not sure what to do here */
    gov_sprite = sprites.citizen[7]; 
  } else {
    gov_sprite = get_government(government)->sprite;
  }
  gtk_pixmap_set(GTK_PIXMAP(government_label), gov_sprite->pixmap, NULL);
}


/**************************************************************************
...
**************************************************************************/
static struct Sprite *get_city_nation_flag_sprite(struct city *pcity)
{
  return get_nation_by_plr(&game.players[pcity->owner])->flag_sprite;
}

/**************************************************************************
...
**************************************************************************/
static struct Sprite *get_unit_nation_flag_sprite(struct unit *punit)
{
  return get_nation_by_plr(&game.players[punit->owner])->flag_sprite;
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
  int dest_x, dest_y;
  int i;

  dest_x=map_adjust_x(x0+dx);
  dest_y=map_adjust_y(y0+dy);

  if(player_can_see_unit(game.player_ptr, punit) && (
     tile_visible_mapcanvas(x0, y0) || 
     tile_visible_mapcanvas(dest_x, dest_y))) {
    int x, y;

    gdk_draw_rectangle(single_tile_pixmap, civ_gc, TRUE, 0, 0, -1, -1);
    put_unit_pixmap(punit, single_tile_pixmap, 0, 0);

    if(x0>=map_view_x0)
      x=(x0-map_view_x0)*NORMAL_TILE_WIDTH;
    else
      x=(map.xsize-map_view_x0+x0)*NORMAL_TILE_WIDTH;

    y=(y0-map_view_y0)*NORMAL_TILE_HEIGHT;

    for(i=0; i<NORMAL_TILE_WIDTH; i++) {
      if(dy>0)
	gdk_draw_pixmap( map_canvas->window, civ_gc, map_canvas_store,
		  x, y, x, y, NORMAL_TILE_WIDTH, 1 );
      else if(dy<0)
	gdk_draw_pixmap( map_canvas->window, civ_gc, map_canvas_store,
		  x, y+NORMAL_TILE_HEIGHT-1, x, y+NORMAL_TILE_HEIGHT-1,
		  NORMAL_TILE_WIDTH, 1 );
      
      if(dx>0)
	gdk_draw_pixmap( map_canvas->window, civ_gc, map_canvas_store,
		  x, y, x, y, 1, NORMAL_TILE_HEIGHT );
      else if(dx<0)
	gdk_draw_pixmap( map_canvas->window, civ_gc, map_canvas_store,
		  x+NORMAL_TILE_WIDTH-1, y, x+NORMAL_TILE_WIDTH-1, y, 1,
		  NORMAL_TILE_HEIGHT );

      x+=dx; y+=dy;
      
      gdk_draw_pixmap( map_canvas->window, civ_gc, single_tile_pixmap,
		  0, 0, x, y, NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT );

      gdk_flush ();
/*      poll(0, 0, 1);*/	/* this is an evil hack */
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
gint overview_canvas_expose( GtkWidget *widget, GdkEventExpose *event )
{
  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE) {
    if(radar_gfx_sprite)
      gdk_draw_pixmap( overview_canvas->window, civ_gc, radar_gfx_sprite->pixmap,
		event->area.x, event->area.y, event->area.x, event->area.y,
		event->area.width, event->area.height );
    return TRUE;
  }
  
  refresh_overview_viewrect();
  return TRUE;
}


/**************************************************************************
...
**************************************************************************/
static void set_overview_tile_foreground_color(int x, int y)
{
  struct tile *ptile=map_get_tile(x, y);
  struct unit *punit;
  struct city *pcity;
  
  if(!ptile->known)
    gdk_gc_set_foreground (fill_bg_gc, colors_standard[COLOR_STD_BLACK]);
  else if((punit=player_find_visible_unit(game.player_ptr, ptile))) {
    if(punit->owner==game.player_idx)
      gdk_gc_set_foreground (fill_bg_gc, colors_standard[COLOR_STD_YELLOW]);
    else
      gdk_gc_set_foreground (fill_bg_gc, colors_standard[COLOR_STD_RED]);
  }
  else if((pcity=map_get_city(x, y))) {
    if(pcity->owner==game.player_idx)
      gdk_gc_set_foreground (fill_bg_gc, colors_standard[COLOR_STD_WHITE]);
    else
      gdk_gc_set_foreground (fill_bg_gc, colors_standard[COLOR_STD_CYAN]);
  }
  else if(ptile->terrain==T_OCEAN)
    gdk_gc_set_foreground (fill_bg_gc, colors_standard[COLOR_STD_OCEAN]);
  else
    gdk_gc_set_foreground (fill_bg_gc, colors_standard[COLOR_STD_GROUND]);
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
      gdk_draw_rectangle( overview_canvas_store, fill_bg_gc, TRUE, x*2, y*2,
			2, 2 );
    }

  gdk_gc_set_foreground( fill_bg_gc, colors_standard[COLOR_STD_BLACK] );
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
  gdk_draw_rectangle( overview_canvas_store, fill_bg_gc, TRUE, x*2, y*2,
		 2, 2 );
  
  gdk_draw_rectangle( overview_canvas->window, fill_bg_gc, TRUE, pos*2, y*2,
		 2, 2 );
}

/**************************************************************************
...
**************************************************************************/
void refresh_overview_viewrect(void)
{
  int delta=map.xsize/2-(map_view_x0+map_canvas_store_twidth/2);

  if(delta>=0) {
    gdk_draw_pixmap( overview_canvas->window, civ_gc, overview_canvas_store,
		0, 0, 2*delta, 0,
		overview_canvas_store_width-2*delta,
		overview_canvas_store_height );
    gdk_draw_pixmap( overview_canvas->window, civ_gc, overview_canvas_store,
		overview_canvas_store_width-2*delta, 0,
		0, 0,
		2*delta, overview_canvas_store_height );
  }
  else {
    gdk_draw_pixmap( overview_canvas->window, civ_gc, overview_canvas_store,
		-2*delta, 0,
		0, 0,
		overview_canvas_store_width+2*delta,
		overview_canvas_store_height );

    gdk_draw_pixmap( overview_canvas->window, civ_gc, overview_canvas_store,
		0, 0,
		overview_canvas_store_width+2*delta, 0,
		-2*delta, overview_canvas_store_height );
  }

  gdk_gc_set_foreground( civ_gc, colors_standard[COLOR_STD_WHITE] );
  
  gdk_draw_rectangle( overview_canvas->window, civ_gc, FALSE,
		(overview_canvas_store_width-2*map_canvas_store_twidth)/2,
		2*map_view_y0,
		2*map_canvas_store_twidth, 2*map_canvas_store_theight-1 );
}


/**************************************************************************
...
**************************************************************************/
gint map_canvas_expose( GtkWidget *widget, GdkEventExpose *event )
{
  gint height, width;
  int tile_width, tile_height;
  gboolean map_resized;

  gdk_window_get_size( widget->window, &width, &height );

  tile_width=(width+NORMAL_TILE_WIDTH-1)/NORMAL_TILE_WIDTH;
  tile_height=(height+NORMAL_TILE_HEIGHT-1)/NORMAL_TILE_HEIGHT;
  
  map_resized=FALSE;
  if(map_canvas_store_twidth !=tile_width ||
     map_canvas_store_theight!=tile_height) { /* resized? */
    gdk_pixmap_unref(map_canvas_store);
  
    map_canvas_store_twidth=tile_width;
    map_canvas_store_theight=tile_height;
/*
    gtk_drawing_area_size(GTK_DRAWING_AREA(map_canvas),
  		    map_canvas_store_twidth,
  		    map_canvas_store_theight);
*/
    map_canvas_store= gdk_pixmap_new( map_canvas->window,
  		    tile_width*NORMAL_TILE_WIDTH,
  		    tile_height*NORMAL_TILE_HEIGHT,
  		    -1 );
  				 
    gdk_draw_rectangle( map_canvas_store, fill_bg_gc, TRUE,
  		    0, 0,
  		    NORMAL_TILE_WIDTH*map_canvas_store_twidth,
  		    NORMAL_TILE_HEIGHT*map_canvas_store_theight );
    update_map_canvas_scrollbars_size();
    map_resized=TRUE;
  }

  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE) {
    if(!intro_gfx_sprite)  load_intro_gfx();
    if(height!=scaled_intro_pixmap_height || width!=scaled_intro_pixmap_width) {
      if(scaled_intro_pixmap)
	gdk_imlib_free_pixmap(scaled_intro_pixmap);
	
      scaled_intro_pixmap=gtk_scale_pixmap(intro_gfx_sprite->pixmap,
					   intro_gfx_sprite->width,
					   intro_gfx_sprite->height,
					   width, height);
      scaled_intro_pixmap_width=width;
      scaled_intro_pixmap_height=height;
    }
    
    if(scaled_intro_pixmap)
	gdk_draw_pixmap( map_canvas->window, civ_gc, scaled_intro_pixmap,
		event->area.x, event->area.y, event->area.x, event->area.y,
		event->area.width, event->area.height );
  }
  else
  {
    if(scaled_intro_pixmap) {
      gdk_imlib_free_pixmap(scaled_intro_pixmap);
      scaled_intro_pixmap=0; scaled_intro_pixmap_height=0;
    }

    if(map.xsize) { /* do we have a map at all */
      if(map_resized) {
	update_map_canvas(0, 0, map_canvas_store_twidth,
			map_canvas_store_theight, 1);

	update_map_canvas_scrollbars();

    	refresh_overview_viewrect();
      }
      else {
	gdk_draw_pixmap( map_canvas->window, civ_gc, map_canvas_store,
		event->area.x, event->area.y, event->area.x, event->area.y,
		event->area.width, event->area.height );
      }
    }
    refresh_overview_canvas();
  }
  return TRUE;
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

  if(draw_map_grid) {	/* draw some grid lines... */
    int length, limit;
  
    gdk_gc_set_foreground(fill_bg_gc, colors_standard[COLOR_STD_BLACK]);

    length=map_canvas_store_twidth*NORMAL_TILE_WIDTH;
    limit=(tile_y+height)*NORMAL_TILE_HEIGHT;

    for(y=tile_y*NORMAL_TILE_HEIGHT; y<limit; y+=NORMAL_TILE_HEIGHT)
      gdk_draw_line(map_canvas_store, fill_bg_gc, 0, y, length, y);

    length=map_canvas_store_theight*NORMAL_TILE_HEIGHT;
    limit=(tile_x+width)*NORMAL_TILE_WIDTH;

    for(x=tile_x*NORMAL_TILE_WIDTH; x<limit; x+=NORMAL_TILE_WIDTH)
      gdk_draw_line(map_canvas_store, fill_bg_gc, x, 0, x, length);
  }

  if(write_to_screen) {
    gdk_draw_pixmap( map_canvas->window, civ_gc, map_canvas_store,
		tile_x*NORMAL_TILE_WIDTH, 
		tile_y*NORMAL_TILE_HEIGHT,
		tile_x*NORMAL_TILE_WIDTH, 
		tile_y*NORMAL_TILE_HEIGHT,
		width*NORMAL_TILE_WIDTH,
		height*NORMAL_TILE_HEIGHT );

    if(width==map_canvas_store_twidth && height==map_canvas_store_theight) {
      show_city_names();
    }
    
  }
}

/**************************************************************************
 Update (only) the visible part of the map
**************************************************************************/
void update_map_canvas_visible(void)
{
  update_map_canvas(0,0, map_canvas_store_twidth,map_canvas_store_theight, 1);
}

/**************************************************************************
...
**************************************************************************/
void update_map_canvas_scrollbars(void)
{
  gtk_adjustment_set_value(GTK_ADJUSTMENT(map_hadj), map_view_x0);
  gtk_adjustment_set_value(GTK_ADJUSTMENT(map_vadj), map_view_y0);
}

/**************************************************************************
...
**************************************************************************/
void update_map_canvas_scrollbars_size(void)
{
  map_hadj=gtk_adjustment_new(-1, 0, map.xsize, 1,
	   map_canvas_store_twidth, map_canvas_store_twidth);
  map_vadj=gtk_adjustment_new(-1, 0, map.ysize, 1,
	   map_canvas_store_theight, map_canvas_store_theight);
  gtk_range_set_adjustment(GTK_RANGE(map_horizontal_scrollbar),
	GTK_ADJUSTMENT(map_hadj));
  gtk_range_set_adjustment(GTK_RANGE(map_vertical_scrollbar),
	GTK_ADJUSTMENT(map_vadj));

  gtk_signal_connect(GTK_OBJECT(map_hadj), "value_changed",
	GTK_SIGNAL_FUNC(scrollbar_jump_callback), (gpointer)TRUE);
  gtk_signal_connect(GTK_OBJECT(map_vadj), "value_changed",
	GTK_SIGNAL_FUNC(scrollbar_jump_callback), (gpointer)FALSE);
}

/**************************************************************************
...
**************************************************************************/
static void show_city_names(void)
{
  int x, y;
  
  for(y=0; y<map_canvas_store_theight; ++y) { 
    int ry=map_view_y0+y;
    for(x=0; x<map_canvas_store_twidth; ++x) { 
      int rx=(map_view_x0+x)%map.xsize;
      struct city *pcity;
      if((pcity=map_get_city(rx, ry))) {
	int w=gdk_string_width(main_font, pcity->name);
						  /*FIXME*/	
	gdk_draw_string( map_canvas->window, main_font,
		    toplevel->style->black_gc,
		    x*NORMAL_TILE_WIDTH+NORMAL_TILE_WIDTH/2-w/2+1,
		    y*NORMAL_TILE_HEIGHT+3*NORMAL_TILE_HEIGHT/2+1,
		    pcity->name );
	gdk_draw_string( map_canvas->window, main_font,
		    toplevel->style->white_gc,
		    x*NORMAL_TILE_WIDTH+NORMAL_TILE_WIDTH/2-w/2, 
		    y*NORMAL_TILE_HEIGHT+3*NORMAL_TILE_HEIGHT/2,
		    pcity->name );
      }
    }
  }
}



/**************************************************************************
...
**************************************************************************/
void put_city_pixmap(struct city *pcity, GdkPixmap *pm, int xtile, int ytile)
{
  SPRITE *mysprite;

  if(use_solid_color_behind_units) {
    gdk_gc_set_foreground( fill_bg_gc, colors_standard[COLOR_STD_RACE0+
					game.players[pcity->owner].nation] );
    gdk_draw_rectangle( pm, fill_bg_gc, TRUE,
		    xtile*NORMAL_TILE_WIDTH, ytile*NORMAL_TILE_HEIGHT, 
		    NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT );
  }
  else if(!flags_are_transparent) {	/* observe transparency here, too! */
    mysprite=get_city_nation_flag_sprite(pcity);
    gdk_draw_pixmap( pm, civ_gc, mysprite->pixmap,
		0, 0,
		xtile*NORMAL_TILE_WIDTH, ytile*NORMAL_TILE_HEIGHT,
		mysprite->width, mysprite->height );
  } else {
    pixmap_put_overlay_tile(pm, xtile, ytile, get_city_nation_flag_sprite(pcity));
  }
  

  pixmap_put_overlay_tile(pm, xtile, ytile,
			  (city_got_citywalls(pcity)
			   ? sprites.tx.city_walls
			   : sprites.tx.city));

  if(genlist_size(&((map_get_tile(pcity->x, pcity->y))->units.list)) > 0)
    pixmap_put_overlay_tile(pm, xtile, ytile, sprites.city.occupied);

  if(pcity->size>=10) {
    pixmap_put_overlay_tile(pm, xtile, ytile,
			    sprites.city.size_tens[pcity->size/10]);
  }
  pixmap_put_overlay_tile(pm, xtile, ytile, sprites.city.size[pcity->size%10]);

  if(map_get_special(pcity->x, pcity->y) & S_POLLUTION)
    pixmap_put_overlay_tile(pm, xtile, ytile, sprites.tx.pollution);

  if(city_unhappy(pcity))
    pixmap_put_overlay_tile(pm, xtile, ytile, sprites.city.disorder);

}


/**************************************************************************
...
**************************************************************************/
void put_city_tile_output(GdkDrawable *pm, int x, int y, 
			  int food, int shield, int trade)
{
  food = CLIP(0, food, NUM_TILES_DIGITS-1);
  trade = CLIP(0, trade, NUM_TILES_DIGITS-1);
  shield = CLIP(0, shield, NUM_TILES_DIGITS-1);
  
  pixmap_put_overlay_tile(pm, x, y, sprites.city.tile_foodnum[food]);
  pixmap_put_overlay_tile(pm, x, y, sprites.city.tile_shieldnum[shield]);
  pixmap_put_overlay_tile(pm, x, y, sprites.city.tile_tradenum[trade]);
}


/**************************************************************************
...
**************************************************************************/
void put_unit_pixmap(struct unit *punit, GdkPixmap *pm, int xtile, int ytile)
{
  SPRITE *mysprite;
  int ihp;

  if(use_solid_color_behind_units) {
    gdk_gc_set_foreground( fill_bg_gc, colors_standard[COLOR_STD_RACE0+
					game.players[punit->owner].nation] );
    gdk_draw_rectangle( pm, fill_bg_gc, TRUE,
		    xtile*NORMAL_TILE_WIDTH, ytile*NORMAL_TILE_HEIGHT, 
		    NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT );
  }
  else {
    if(flags_are_transparent) {
      pixmap_put_overlay_tile(pm, xtile, ytile, 
			      get_unit_nation_flag_sprite(punit));
    } else {
      mysprite=get_unit_nation_flag_sprite(punit);
      gdk_draw_pixmap( pm, civ_gc, mysprite->pixmap,
		0, 0,
		xtile*NORMAL_TILE_WIDTH, ytile*NORMAL_TILE_HEIGHT,
		mysprite->width, mysprite->height );
    };
  }
    
  pixmap_put_overlay_tile(pm, xtile, ytile,
			  get_unit_type(punit->type)->sprite);

  if(punit->activity!=ACTIVITY_IDLE) {
    struct Sprite *s = NULL;
    switch(punit->activity) {
    case ACTIVITY_MINE:
      s = sprites.unit.mine;
      break;
    case ACTIVITY_POLLUTION:
      s = sprites.unit.pollution;
      break;
    case ACTIVITY_PILLAGE:
      s = sprites.unit.pillage;
      break;
    case ACTIVITY_ROAD:
    case ACTIVITY_RAILROAD:
      s = sprites.unit.road;
      break;
    case ACTIVITY_IRRIGATE:
      s = sprites.unit.irrigate;
      break;
    case ACTIVITY_EXPLORE:
      s = sprites.unit.auto_explore;
      break;
    case ACTIVITY_FORTIFY:
      s = sprites.unit.fortify;
      break;
    case ACTIVITY_FORTRESS:
      s = sprites.unit.fortress;
      break;
    case ACTIVITY_SENTRY:
      s = sprites.unit.sentry;
      break;
    case ACTIVITY_GOTO:
      s = sprites.unit.go_to;
      break;
    case ACTIVITY_TRANSFORM:
      s = sprites.unit.transform;
    default:
      break;
    }

    pixmap_put_overlay_tile(pm, xtile, ytile, s);
  }

  if(punit->ai.control) {
    if(is_military_unit(punit)) {
      pixmap_put_overlay_tile(pm, xtile, ytile, sprites.unit.auto_attack);
    } else {
      pixmap_put_overlay_tile(pm, xtile, ytile, sprites.unit.auto_settler);
    }
  }

  ihp = ((NUM_TILES_HP_BAR-1)*punit->hp) / get_unit_type(punit->type)->hp;
  ihp = CLIP(0, ihp, NUM_TILES_HP_BAR-1);
  pixmap_put_overlay_tile(pm, xtile, ytile, sprites.unit.hp_bar[ihp]);
}


/**************************************************************************
...
**************************************************************************/
void put_unit_gpixmap(struct unit *punit, GtkPixcomm *p, int xtile, int ytile)
{
  int ihp;
  
  if(use_solid_color_behind_units) {
    gtk_pixcomm_fill(p, colors_standard[COLOR_STD_RACE0+
			game.players[punit->owner].nation], FALSE);
  }
  else {
    put_overlay_tile_gpixmap(p, xtile, ytile,
			     get_unit_nation_flag_sprite(punit));
  }
    
  put_overlay_tile_gpixmap(p, xtile, ytile,
			   get_unit_type(punit->type)->sprite);

  if(punit->activity!=ACTIVITY_IDLE) {
    struct Sprite *s = NULL;
    switch(punit->activity) {
    case ACTIVITY_MINE:
      s = sprites.unit.mine;
      break;
    case ACTIVITY_POLLUTION:
      s = sprites.unit.pollution;
      break;
    case ACTIVITY_PILLAGE:
      s = sprites.unit.pillage;
      break;
    case ACTIVITY_ROAD:
    case ACTIVITY_RAILROAD:
      s = sprites.unit.road;
      break;
    case ACTIVITY_IRRIGATE:
      s = sprites.unit.irrigate;
      break;
    case ACTIVITY_EXPLORE:
      s = sprites.unit.auto_explore;
      break;
    case ACTIVITY_FORTIFY:
      s = sprites.unit.fortify;
      break;
    case ACTIVITY_FORTRESS:
      s = sprites.unit.fortress;
      break;
    case ACTIVITY_SENTRY:
      s = sprites.unit.sentry;
      break;
    case ACTIVITY_GOTO:
      s = sprites.unit.go_to;
      break;
    case ACTIVITY_TRANSFORM:
      s = sprites.unit.transform;
    default:
      break;
    }

    put_overlay_tile_gpixmap(p, xtile, ytile, s);
  }

  if(punit->ai.control) {
    if(is_military_unit(punit)) {
      put_overlay_tile_gpixmap(p, xtile, ytile, sprites.unit.auto_attack);
    } else {
      put_overlay_tile_gpixmap(p, xtile, ytile, sprites.unit.auto_settler);
    }
  }

  ihp = ((NUM_TILES_HP_BAR-1)*punit->hp) / get_unit_type(punit->type)->hp;
  ihp = CLIP(0, ihp, NUM_TILES_HP_BAR-1);
  put_overlay_tile_gpixmap(p, xtile, ytile, sprites.unit.hp_bar[ihp]);
  
  gtk_pixcomm_changed(GTK_PIXCOMM(p));
}


/**************************************************************************
  ...
  FIXME:
  For now only two food, one shield and two masks can be drawn per unit,
  the proper way to do this is probably something like what Civ II does.
  (One food/shield/mask drawn N times, possibly one top of itself. -- SKi 
**************************************************************************/
void put_unit_gpixmap_city_overlays(struct unit *punit, GtkPixcomm *p)
{
  int upkeep_food = CLIP(0, punit->upkeep_food, 2);
  int unhappy = CLIP(0, punit->unhappiness, 2);
 
  /* draw overlay pixmaps */
  if (punit->upkeep > 0)
    put_overlay_tile_gpixmap(p, 0, 1, sprites.upkeep.shield);
  if (upkeep_food > 0)
    put_overlay_tile_gpixmap(p, 0, 1, sprites.upkeep.food[upkeep_food-1]);
  if (unhappy > 0)
    put_overlay_tile_gpixmap(p, 0, 1, sprites.upkeep.unhappy[unhappy-1]);
}


/**************************************************************************
...
**************************************************************************/
void put_nuke_mushroom_pixmaps(int abs_x0, int abs_y0)
{
  int x, y;
  
  for(y=0; y<3; y++)
    for(x=0; x<3; x++) {
      struct Sprite *mysprite = sprites.explode.nuke[y][x];

      gdk_draw_pixmap( map_canvas->window, civ_gc, mysprite->pixmap,
		0, 0,
		map_canvas_adjust_x(x-1+abs_x0)*NORMAL_TILE_WIDTH, 
		map_canvas_adjust_y(y-1+abs_y0)*NORMAL_TILE_HEIGHT,
		NORMAL_TILE_WIDTH,
		NORMAL_TILE_HEIGHT );
    }

  gdk_flush ();
  sleep(1);

  update_map_canvas(map_canvas_adjust_x(abs_x0-1), 
                    map_canvas_adjust_y(abs_y0-1),
		    3, 3, 1);
}

/**************************************************************************
...
**************************************************************************/
void pixmap_put_black_tile(GdkDrawable *pm, int x, int y)
{
  gdk_gc_set_foreground( fill_bg_gc, colors_standard[COLOR_STD_BLACK] );
  gdk_draw_rectangle( pm, fill_bg_gc, TRUE,
		    x*NORMAL_TILE_WIDTH, y*NORMAL_TILE_HEIGHT,
		    NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT );
}
		     

/**************************************************************************
...
**************************************************************************/
void pixmap_frame_tile_red(GdkDrawable *pm, int x, int y)
{
  gdk_gc_set_foreground( fill_bg_gc, colors_standard[COLOR_STD_RED] );

  gdk_draw_rectangle( pm, fill_bg_gc, FALSE,
		    x*NORMAL_TILE_WIDTH, y*NORMAL_TILE_HEIGHT,
		    NORMAL_TILE_WIDTH-1, NORMAL_TILE_HEIGHT-1 );
/*
  gdk_draw_rectangle( pm, fill_bg_gc, TRUE,
		    x*NORMAL_TILE_WIDTH, y*NORMAL_TILE_HEIGHT,
		    NORMAL_TILE_WIDTH-1, NORMAL_TILE_HEIGHT-1 );
  gdk_draw_rectangle( pm, fill_bg_gc, TRUE,
		    x*NORMAL_TILE_WIDTH+1, y*NORMAL_TILE_HEIGHT+1,
		    NORMAL_TILE_WIDTH-3, NORMAL_TILE_HEIGHT-3 );
  gdk_draw_rectangle( pm, fill_bg_gc, TRUE,
		    x*NORMAL_TILE_WIDTH+2, y*NORMAL_TILE_HEIGHT+2,
		    NORMAL_TILE_WIDTH-5, NORMAL_TILE_HEIGHT-5 );
*/
}



/**************************************************************************
...
**************************************************************************/
void pixmap_put_tile(GdkDrawable *pm, int x, int y, int abs_x0, int abs_y0, 
		     int citymode)
{
  int ttype, ttype_north, ttype_south, ttype_east, ttype_west;
  int ttype_north_east, ttype_south_east, ttype_south_west, ttype_north_west;
  int tspecial, tspecial_north, tspecial_south, tspecial_east, tspecial_west;
  int tspecial_north_east, tspecial_south_east, tspecial_south_west, tspecial_north_west;
  int rail_card_tileno=0, rail_semi_tileno=0, road_card_tileno=0, road_semi_tileno=0;
  int rail_card_count=0, rail_semi_count=0, road_card_count=0, road_semi_count=0;

  int tileno;
  struct tile *ptile;
  struct Sprite *mysprite;
  struct unit *punit;
  struct city *pcity;
  int den_y=map.ysize*.24;

  ptile=map_get_tile(abs_x0, abs_y0);
  punit=get_unit_in_focus();
  
  if(abs_y0>=map.ysize || ptile->known<TILE_KNOWN) {
    gdk_gc_set_foreground(fill_bg_gc, colors_standard[COLOR_STD_BLACK]);
    gdk_draw_rectangle(pm, fill_bg_gc, TRUE,
		    x*NORMAL_TILE_WIDTH, y*NORMAL_TILE_HEIGHT,
		    NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
    return;
  }

  if(!flags_are_transparent) {
    /* non-transparent flags -> just draw city or unit. */
    if((pcity=map_get_city(abs_x0, abs_y0))
       && (citymode || !(punit=get_unit_in_focus())
	   || punit->x!=abs_x0 || punit->y!=abs_y0
	   || (unit_list_size(&ptile->units)==0))) {
        /* above, unit_list_size==0 happens when focus unit is blinking --dwp */ 
	put_city_pixmap(pcity, pm, x, y);
	return;
    }

    if((punit=player_find_visible_unit(game.player_ptr, ptile))) {
      if(!citymode || punit->owner!=game.player_idx) {
       put_unit_pixmap(punit, pm, x, y);
       if(unit_list_size(&ptile->units)>1)
	 pixmap_put_overlay_tile(pm, x, y, sprites.unit.stack);
       return;
      }
    }
  }
    
  ttype=map_get_terrain(abs_x0, abs_y0);
  ttype_east=map_get_terrain(abs_x0+1, abs_y0);
  ttype_west=map_get_terrain(abs_x0-1, abs_y0);

  /* make north and south pole seamless: */
  if(abs_y0==0) {
    ttype_north=ttype;
    ttype_north_east=ttype_east;
    ttype_north_west=ttype_west;
  } else {
    ttype_north=map_get_terrain(abs_x0, abs_y0-1);
    ttype_north_east=map_get_terrain(abs_x0+1, abs_y0-1);
    ttype_north_west=map_get_terrain(abs_x0-1, abs_y0-1);
  }
  if(abs_y0==map.ysize-1) {
    ttype_south=ttype;
    ttype_south_east=ttype_east;
    ttype_south_west=ttype_west;
  } else {
    ttype_south=map_get_terrain(abs_x0, abs_y0+1);
    ttype_south_east=map_get_terrain(abs_x0+1, abs_y0+1);
    ttype_south_west=map_get_terrain(abs_x0-1, abs_y0+1);
  }

  /* map_get_specials() returns S_NO_SPECIAL past poles anyway */
  tspecial=map_get_special(abs_x0, abs_y0);
  tspecial_north=map_get_special(abs_x0, abs_y0-1);
  tspecial_east=map_get_special(abs_x0+1, abs_y0);
  tspecial_south=map_get_special(abs_x0, abs_y0+1);
  tspecial_west=map_get_special(abs_x0-1, abs_y0);
  tspecial_north_east=map_get_special(abs_x0+1, abs_y0-1);
  tspecial_south_east=map_get_special(abs_x0+1, abs_y0+1);
  tspecial_south_west=map_get_special(abs_x0-1, abs_y0+1);
  tspecial_north_west=map_get_special(abs_x0-1, abs_y0-1);

  if(map.is_earth &&
     abs_x0>=34 && abs_x0<=36 && abs_y0>=den_y && abs_y0<=den_y+1) {
    mysprite = sprites.tx.denmark[abs_y0-den_y][abs_x0-34];
  } else {
    tileno = INDEX_NSEW((ttype_north==ttype),
			(ttype_south==ttype),
			(ttype_east==ttype),	
			(ttype_west==ttype));
    if(ttype==T_RIVER) {
      tileno |= INDEX_NSEW((ttype_north==T_OCEAN),
			   (ttype_south==T_OCEAN),
			   (ttype_east==T_OCEAN),
			   (ttype_west==T_OCEAN));
    }
    mysprite = get_tile_type(ttype)->sprite[tileno];
  }

  if (mysprite) {
    gdk_draw_pixmap(pm, civ_gc, mysprite->pixmap,
		    0, 0,
		    x*NORMAL_TILE_WIDTH, y*NORMAL_TILE_HEIGHT,
		    mysprite->width, mysprite->height);
  }

  if(ttype==T_OCEAN) {
    tileno = INDEX_NSEW((ttype_north==T_OCEAN && ttype_east==T_OCEAN && 
			 ttype_north_east!=T_OCEAN),
			(ttype_south==T_OCEAN && ttype_west==T_OCEAN && 
			 ttype_south_west!=T_OCEAN),
			(ttype_east==T_OCEAN && ttype_south==T_OCEAN && 
			 ttype_south_east!=T_OCEAN),
			(ttype_north==T_OCEAN && ttype_west==T_OCEAN && 
			 ttype_north_west!=T_OCEAN));
    if(tileno!=0) 
      pixmap_put_overlay_tile(pm, x, y, sprites.tx.coast_cape[tileno]);

    if(tspecial_north&S_RIVER || ttype_north==T_RIVER)
      pixmap_put_overlay_tile(pm, x, y, sprites.tx.river_outlet[DIR_NORTH]);
    if(tspecial_west&S_RIVER || ttype_west==T_RIVER)
      pixmap_put_overlay_tile(pm, x, y, sprites.tx.river_outlet[DIR_WEST]);
    if(tspecial_south&S_RIVER || ttype_south==T_RIVER)
      pixmap_put_overlay_tile(pm, x, y, sprites.tx.river_outlet[DIR_SOUTH]);
    if(tspecial_east&S_RIVER || ttype_east==T_RIVER)
      pixmap_put_overlay_tile(pm, x, y, sprites.tx.river_outlet[DIR_EAST]);
  }

  if (tspecial&S_RIVER) {
    tileno = INDEX_NSEW((tspecial_north&S_RIVER || ttype_north==T_OCEAN),
			(tspecial_south&S_RIVER || ttype_south==T_OCEAN),
			(tspecial_east&S_RIVER  || ttype_east==T_OCEAN),
			(tspecial_west&S_RIVER  || ttype_west== T_OCEAN));
    pixmap_put_overlay_tile(pm, x, y, sprites.tx.spec_river[tileno]);
  }

  if(tspecial & S_IRRIGATION) {
    if(tspecial & S_FARMLAND)
      pixmap_put_overlay_tile(pm, x, y, sprites.tx.farmland);
    else
      pixmap_put_overlay_tile(pm, x, y, sprites.tx.irrigation);
  }

  if((tspecial & S_ROAD) || (tspecial & S_RAILROAD)) {
    int n, s, e, w;
    
    n = !!(tspecial_north&S_RAILROAD);
    s = !!(tspecial_south&S_RAILROAD);
    e = !!(tspecial_east&S_RAILROAD);
    w = !!(tspecial_west&S_RAILROAD);
    rail_card_count = n + s + e + w;
    rail_card_tileno = INDEX_NSEW(n,s,e,w);
    
    n = !!(tspecial_north&S_ROAD);
    s = !!(tspecial_south&S_ROAD);
    e = !!(tspecial_east&S_ROAD);
    w = !!(tspecial_west&S_ROAD);
    road_card_count = n + s + e + w;
    road_card_tileno = INDEX_NSEW(n,s,e,w);
    
    n = !!(tspecial_north_east&S_RAILROAD);
    s = !!(tspecial_south_west&S_RAILROAD);
    e = !!(tspecial_south_east&S_RAILROAD);
    w = !!(tspecial_north_west&S_RAILROAD);
    rail_semi_count = n + s + e + w;
    rail_semi_tileno = INDEX_NSEW(n,s,e,w);
    
    n = !!(tspecial_north_east&S_ROAD);
    s = !!(tspecial_south_west&S_ROAD);
    e = !!(tspecial_south_east&S_ROAD);
    w = !!(tspecial_north_west&S_ROAD);
    road_semi_count = n + s + e + w;
    road_semi_tileno = INDEX_NSEW(n,s,e,w);

    if(tspecial & S_RAILROAD) {
      road_card_tileno&=~rail_card_tileno;
      road_semi_tileno&=~rail_semi_tileno;
    } else if(tspecial & S_ROAD) {
      rail_card_tileno&=~road_card_tileno;
      rail_semi_tileno&=~road_semi_tileno;
    }

    if(road_semi_count > road_card_count) {
      if(road_card_tileno!=0) {
	pixmap_put_overlay_tile(pm, x, y,
				sprites.road.cardinal[road_card_tileno]);
      }
      if(road_semi_tileno!=0 && draw_diagonal_roads) {
	pixmap_put_overlay_tile(pm, x, y,
				sprites.road.diagonal[road_semi_tileno]);
      }
    } else {
      if(road_semi_tileno!=0 && draw_diagonal_roads) {
	pixmap_put_overlay_tile(pm, x, y,
				sprites.road.diagonal[road_semi_tileno]);
      }
      if(road_card_tileno!=0) {
	pixmap_put_overlay_tile(pm, x, y, 
				sprites.road.cardinal[road_card_tileno]);
      }
    }

    if(rail_semi_count > rail_card_count) {
      if(rail_card_tileno!=0) {
	pixmap_put_overlay_tile(pm, x, y,
				sprites.rail.cardinal[rail_card_tileno]);
      }
      if(rail_semi_tileno!=0 && draw_diagonal_roads) {
	pixmap_put_overlay_tile(pm, x, y,
				sprites.rail.diagonal[rail_semi_tileno]);
      }
    } else {
      if(rail_semi_tileno!=0 && draw_diagonal_roads) {
	pixmap_put_overlay_tile(pm, x, y,
				sprites.rail.diagonal[rail_semi_tileno]);
      }
      if(rail_card_tileno!=0) {
	pixmap_put_overlay_tile(pm, x, y,
				sprites.rail.cardinal[rail_card_tileno]);
      }
    }
  }

  if(tspecial & S_SPECIAL_1)
    pixmap_put_overlay_tile(pm, x, y, tile_types[ttype].special[0].sprite);
  else if(tspecial & S_SPECIAL_2)
    pixmap_put_overlay_tile(pm, x, y, tile_types[ttype].special[1].sprite);

  if(tspecial & S_MINE) {
    if(ttype==T_HILLS || ttype==T_MOUNTAINS)
      pixmap_put_overlay_tile(pm, x, y, sprites.tx.mine);
    else /* desert */
      pixmap_put_overlay_tile(pm, x, y, sprites.tx.oil_mine);
  }

  if (tspecial & S_RAILROAD) {
    int adjacent = rail_card_tileno;
    if (draw_diagonal_roads)
      adjacent |= rail_semi_tileno;
    if (!adjacent)
      pixmap_put_overlay_tile(pm, x, y, sprites.rail.isolated);
  }
  else if (tspecial & S_ROAD) {
    int adjacent = (rail_card_tileno | road_card_tileno);
    if (draw_diagonal_roads)
      adjacent |= (rail_semi_tileno | road_semi_tileno);
    if (!adjacent)
      pixmap_put_overlay_tile(pm, x, y, sprites.road.isolated);
  }

  if(tspecial & S_HUT)
    pixmap_put_overlay_tile(pm, x, y, sprites.tx.village);
    
  if(tspecial & S_FORTRESS)
    pixmap_put_overlay_tile(pm, x, y, sprites.tx.fortress);

  if(tspecial & S_POLLUTION)
    pixmap_put_overlay_tile(pm, x, y, sprites.tx.pollution);

  if(!citymode) {
    tileno = INDEX_NSEW((tile_is_known(abs_x0, abs_y0-1)==TILE_UNKNOWN),
			(tile_is_known(abs_x0, abs_y0+1)==TILE_UNKNOWN),
			(tile_is_known(abs_x0+1, abs_y0)==TILE_UNKNOWN),
			(tile_is_known(abs_x0-1, abs_y0)==TILE_UNKNOWN));
    if (tileno) 
      pixmap_put_overlay_tile(pm, x, y, sprites.tx.darkness[tileno]);
  }

  if(flags_are_transparent) {  /* transparent flags -> draw city or unit last */
    if((pcity=map_get_city(abs_x0, abs_y0))) {
      put_city_pixmap(pcity, pm, x, y);
    }

    if((punit=player_find_visible_unit(game.player_ptr, ptile))) {
      if(pcity && punit!=get_unit_in_focus()) return;
      if(!citymode || punit->owner!=game.player_idx) {
       put_unit_pixmap(punit, pm, x, y);
       if(unit_list_size(&ptile->units)>1)  
	 pixmap_put_overlay_tile(pm, x, y, sprites.unit.stack);
      }
    }
  }
}


/**************************************************************************
...
**************************************************************************/
static void pixmap_put_overlay_tile(GdkDrawable *pixmap,
				    int x, int y, struct Sprite *ssprite)
{
  if (!ssprite)
    return;
      
  gdk_gc_set_clip_origin( civ_gc, x*NORMAL_TILE_WIDTH, y*NORMAL_TILE_HEIGHT );
  gdk_gc_set_clip_mask( civ_gc, ssprite->mask );

  gdk_draw_pixmap( pixmap, civ_gc, ssprite->pixmap,
	    0, 0,
	    x*NORMAL_TILE_WIDTH, y*NORMAL_TILE_HEIGHT,
	    ssprite->width, ssprite->height );
  gdk_gc_set_clip_mask( civ_gc, NULL );
}

/**************************************************************************
...
**************************************************************************/
static void put_overlay_tile_gpixmap(GtkPixcomm *p, int x, int y,
				     struct Sprite *ssprite)
{
  if (!ssprite)
    return;

  gtk_pixcomm_copyto (p, ssprite, x*NORMAL_TILE_WIDTH, y*NORMAL_TILE_HEIGHT,
		FALSE);
}

/**************************************************************************
 Draws a cross-hair overlay on a tile
**************************************************************************/
void put_cross_overlay_tile(int x,int y)
{
  x=map_adjust_x(x);
  y=map_adjust_y(y);

  if(tile_visible_mapcanvas(x, y)) {
    pixmap_put_overlay_tile(map_canvas->window,map_canvas_adjust_x(x),
			    map_canvas_adjust_y(y), sprites.user.attention);
  }
}


/**************************************************************************
 Shade the tiles around a city to indicate the location of workers
**************************************************************************/
void put_city_workers(struct city *pcity, int color)
{
  int x,y;
  int i,j;
  static struct city *last_pcity=NULL;

  if(color==-1) {
    if(pcity!=last_pcity)  city_workers_color = (city_workers_color%3)+1;
    color=city_workers_color;
  }

  gdk_gc_set_foreground(fill_tile_gc, colors_standard[color]);
  x=map_canvas_adjust_x(pcity->x); y=map_canvas_adjust_y(pcity->y);
  city_map_iterate(i, j)  {
    enum city_tile_type t=get_worker_city(pcity, i, j);
    enum city_tile_type last_t=-1;
    if(i==2 && j==2) continue;
    if(t==C_TILE_EMPTY) {
      if(last_t!=t) gdk_gc_set_stipple(fill_tile_gc,gray25);
    } else if(t==C_TILE_WORKER) {
      if(last_t!=t) gdk_gc_set_stipple(fill_tile_gc,gray50);
    } else continue;
    last_t=t;
    gdk_draw_pixmap(map_canvas->window, civ_gc, map_canvas_store,
	     (x+i-2)*NORMAL_TILE_WIDTH, (y+j-2)*NORMAL_TILE_HEIGHT, 
	     (x+i-2)*NORMAL_TILE_WIDTH, (y+j-2)*NORMAL_TILE_HEIGHT,
	     NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
    gdk_draw_rectangle(map_canvas->window, fill_tile_gc, TRUE,
		  (x+i-2)*NORMAL_TILE_WIDTH, (y+j-2)*NORMAL_TILE_HEIGHT,
		  NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
  }

  last_pcity=pcity;
}


/**************************************************************************
...
**************************************************************************/
void scrollbar_jump_callback(GtkAdjustment *adj, gpointer hscrollbar)
{
  gfloat percent=adj->value;

  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE)
     return;

  if(hscrollbar)
    map_view_x0=percent;
  else {
    map_view_y0=percent;
    map_view_y0=(map_view_y0<0) ? 0 : map_view_y0;
    map_view_y0=(map_view_y0>map.ysize-map_canvas_store_theight) ? 
       map.ysize-map_canvas_store_theight : map_view_y0;
  }

  update_map_canvas(0, 0, map_canvas_store_twidth, map_canvas_store_theight, 1);
  refresh_overview_viewrect();
}
