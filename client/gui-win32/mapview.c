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
#include <assert.h>

#include <windows.h>
#include <windowsx.h>

#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "government.h"         /* government_graphic() */
#include "map.h"
#include "player.h"
#include "rand.h"
#include "support.h"
#include "timing.h"
 
#include "civclient.h"
#include "climisc.h"
#include "colors.h"
#include "control.h" /* set_unit_focus_no_center and get_unit_in_focus */
#include "graphics.h"
#include "gui_stuff.h"
#include "mapctrl.h"
#include "options.h"
#include "tilespec.h"      
#include "goto.h"
#include "gui_main.h"
#include "mapview.h"

enum draw_part {
  D_T_L = 1, D_T_R = 2, D_M_L = 4, D_M_R = 8, D_B_L = 16, D_B_R = 32
};

/* Format: D_[TMB]+_[LR]+.
   The drawing algorithm don't take all possible combinations into account,
   but only those that are rectangles.
*/
/* Some usefull definitions: */
enum draw_type {
  D_FULL = D_T_L | D_T_R | D_M_L | D_M_R | D_B_L | D_B_R,
  D_B_LR = D_B_L | D_B_R,
  D_MB_L = D_M_L | D_B_L,
  D_MB_R = D_M_R | D_B_R,
  D_TM_L = D_T_L | D_M_L,
  D_TM_R = D_T_R | D_M_R,
  D_T_LR = D_T_L | D_T_R,
  D_TMB_L = D_T_L | D_M_L | D_B_L,
  D_TMB_R = D_T_R | D_M_R | D_B_R,
  D_M_LR = D_M_L | D_M_R,
  D_MB_LR = D_M_L | D_M_R | D_B_L | D_B_R
};



HDC mapstoredc;
HDC overviewstoredc;
HBITMAP overviewstorebitmap;
HBITMAP mapstorebitmap;

static HBITMAP intro_gfx;
static HBITMAP indicatorbmp;
static HBITMAP single_tile_pixmap;
static HDC indicatordc;

extern HBITMAP BITMAP2HBITMAP(BITMAP *bmp);

extern void do_mainwin_layout();
int map_view_x;
int map_view_y;
int map_view_width;
int map_view_height;
static HRGN cliprgn;
extern int seconds_to_turndone;   
void update_map_canvas_scrollbars_size(void);
static void show_city_descriptions(HDC hdc);     
void refresh_overview_viewrect_real(HDC hdcp);
void set_overview_win_dim(int w,int h);
static int get_canvas_xy(int map_x, int map_y, int *canvas_x, int *canvas_y);
static void put_one_tile(int x, int y, enum draw_type draw);
void put_one_tile_full(HDC hdc, int x, int y,
			      int canvas_x, int canvas_y, int citymode);
static void pixmap_put_tile_iso(HDC hdc, int x, int y,
                                int canvas_x, int canvas_y,
                                int citymode,
                                int offset_x, int offset_y, int offset_y_unit,
                                int width, int height, int height_unit,
                                enum draw_type draw);
static void really_draw_segment(int src_x, int src_y, int dir,
                                    int write_to_screen, int force);
static void dither_tile(HDC hdc, struct Sprite **dither,
                        int canvas_x, int canvas_y,
                        int offset_x, int offset_y,
                        int width, int height, int fog);
static void put_line(HDC hdc, int x, int y, 
		     int dir,int write_to_screen);

/**************************************************************************

**************************************************************************/
void map_resize()
{
  HBITMAP newbmp;
  HDC hdc;
  hdc=GetDC(root_window);
  newbmp=CreateCompatibleBitmap(hdc,map_win_width,map_win_height);
  SelectObject(mapstoredc,newbmp);
  if (mapstorebitmap) DeleteObject(mapstorebitmap);
  mapstorebitmap=newbmp;
  ReleaseDC(root_window,hdc);
  if (cliprgn)
    DeleteObject(cliprgn);
  cliprgn=NULL;
  map_view_width=(map_win_width+NORMAL_TILE_WIDTH-1)/NORMAL_TILE_WIDTH;
  map_view_height=(map_win_height+NORMAL_TILE_HEIGHT-1)/NORMAL_TILE_HEIGHT; 
  update_map_canvas_scrollbars_size();
  if ((get_client_state()==CLIENT_GAME_RUNNING_STATE)&&(map.xsize))
    {
      update_map_canvas_visible();
      update_map_canvas_scrollbars();
      refresh_overview_viewrect_real(NULL);
      refresh_overview_canvas();
    }
}

/**************************************************************************

**************************************************************************/
void init_map_win()
{
  HDC hdc;
  hdc=GetDC(root_window);
  mapstoredc=CreateCompatibleDC(hdc);
  overviewstoredc=CreateCompatibleDC(hdc);

  single_tile_pixmap=CreateCompatibleBitmap(hdc,
					    UNIT_TILE_WIDTH,
					    UNIT_TILE_HEIGHT);
  ReleaseDC(root_window,hdc);
  mapstorebitmap=NULL;
  overviewstorebitmap=NULL;
  map_view_x=0;
  map_view_y=0;
  overview_win_width=160;
  overview_win_height=100;
}

/**************************************************************************

**************************************************************************/
void map_expose(HDC hdc)
{
  HBITMAP bmsave;
  HDC introgfxdc;
  if (get_client_state()!=CLIENT_GAME_RUNNING_STATE)
    {
      if (!intro_gfx_sprite) load_intro_gfx();
      if (!intro_gfx) intro_gfx=BITMAP2HBITMAP(&intro_gfx_sprite->bmp);
      introgfxdc=CreateCompatibleDC(hdc);
      bmsave=SelectObject(introgfxdc,intro_gfx);
      StretchBlt(hdc,map_win_x,map_win_y,map_win_width,map_win_height,
		 introgfxdc,0,0,
		 intro_gfx_sprite->width,
		 intro_gfx_sprite->height,
		 SRCCOPY);
      SelectObject(introgfxdc,bmsave);
      DeleteDC(introgfxdc);
    }
  else
    {
      bmsave=SelectObject(indicatordc,indicatorbmp);
      BitBlt(hdc,0,taxinfoline_y,10*SMALL_TILE_WIDTH,2*SMALL_TILE_HEIGHT,
	     indicatordc,0,0,SRCCOPY);
      SelectObject(indicatordc,bmsave);
      BitBlt(hdc,map_win_x,map_win_y,map_win_width,map_win_height,mapstoredc,0,0,SRCCOPY);
      show_city_descriptions(hdc);
    }
}
/* put a unit at pos x and y (not xtile, ytile !!) */ 

/**************************************************************************

**************************************************************************/
void put_unit_pixmap(struct unit *punit, HDC hdc,int canvas_x,int canvas_y)
{
  int solid_bg;
  if (is_isometric) {
    struct Sprite *sprites[40];        
    int count = fill_unit_sprite_array(sprites, punit,&solid_bg);        
    int i;
  
    assert(!solid_bg);
    for (i=0; i<count; i++) {
      if (sprites[i]) {
	draw_sprite(sprites[i],hdc,canvas_x,canvas_y);
      }
    }
    
  } else {
    struct Sprite *sprites[40];
    int count = fill_unit_sprite_array(sprites, punit, &solid_bg);
    if (count) {
      int i=0;
      if (solid_bg) {
	RECT rc;
	rc.left=canvas_x;
	rc.top=canvas_y;
	rc.right=rc.left+UNIT_TILE_WIDTH;
	rc.bottom=rc.top+UNIT_TILE_HEIGHT;
	FillRect(hdc,&rc,brush_std[player_color(unit_owner(punit))]);
      } else {
	
	draw_sprite(sprites[0],hdc,canvas_x,canvas_y);
	
	i++;
      }
      for(;i<count;i++) {
	if (sprites[i])
	  draw_sprite(sprites[i],hdc,canvas_x,canvas_y);
      }
    }
  }
}
/**************************************************************************

**************************************************************************/
void put_unit_city_overlays(struct unit *punit, HDC hdc, int x, int y)
{
  int upkeep_food = CLIP(0, punit->upkeep_food, 2);
  int unhappy = CLIP(0, punit->unhappiness, 2);   
  
  if (punit->upkeep > 0)
    draw_sprite(sprites.upkeep.shield,hdc,x,y+NORMAL_TILE_HEIGHT);
  if (upkeep_food > 0)
    draw_sprite(sprites.upkeep.food[upkeep_food-1],hdc,x,y+NORMAL_TILE_HEIGHT);
  if (unhappy > 0)
    draw_sprite(sprites.upkeep.unhappy[unhappy-1],hdc,x,y+NORMAL_TILE_HEIGHT);
}

/**************************************************************************

**************************************************************************/
void pixmap_frame_tile_red(HDC hdc,int x, int y)
{
  HPEN old;
  old=SelectObject(hdc,pen_std[COLOR_STD_RED]);
  mydrawrect(hdc,x*NORMAL_TILE_WIDTH,y*NORMAL_TILE_HEIGHT,
	     NORMAL_TILE_WIDTH-1,NORMAL_TILE_HEIGHT-1);
  SelectObject(hdc,old);
}

/**************************************************************************

**************************************************************************/
void pixmap_put_tile(HDC hdc, int x, int y,
                     int canvas_x, int canvas_y, int citymode)
{
  struct Sprite *tile_sprs[80];
  int fill_bg;
  struct player *pplayer;

  if (normalize_map_pos(&x, &y) && tile_get_known(x, y)) {
    int count = fill_tile_sprite_array(tile_sprs, x, y, citymode, &fill_bg, &pplayer);


    int i = 0;
    
    if (fill_bg) {
      HBRUSH old;
    
      if (pplayer) {
	old=SelectObject(hdc,brush_std[player_color(pplayer)]);
      } else {
	old=SelectObject(hdc,brush_std[COLOR_STD_BACKGROUND]);
      }
      mydrawrect(hdc,canvas_x,canvas_y,NORMAL_TILE_WIDTH,NORMAL_TILE_HEIGHT);
      SelectObject(hdc,old);
    } else {
      /* first tile without mask */
      draw_sprite(tile_sprs[0],hdc,canvas_x,canvas_y);
      i++;
    }

    for (;i<count; i++) {
      if (tile_sprs[i]) {
        draw_sprite(tile_sprs[i],hdc,canvas_x,canvas_y);
      }
    }
    
    if (draw_map_grid && !citymode) {
      HPEN old;
      int here_in_radius =
        player_in_city_radius(game.player_ptr, x, y);
      /* left side... */
      if ((map_get_tile(x-1, y))->known &&
          (here_in_radius ||
           player_in_city_radius(game.player_ptr, x-1, y))) {
	old=SelectObject(hdc,pen_std[COLOR_STD_WHITE]);
      } else {
	old=SelectObject(hdc,pen_std[COLOR_STD_BLACK]);
      }
      MoveToEx(hdc,canvas_x,canvas_y,NULL);
      LineTo(hdc,canvas_x,canvas_y+NORMAL_TILE_HEIGHT);
      old=SelectObject(hdc,old);
      /* top side... */
      if((map_get_tile(x, y-1))->known &&
         (here_in_radius ||
          player_in_city_radius(game.player_ptr, x, y-1))) {
        old=SelectObject(hdc,pen_std[COLOR_STD_WHITE]);
      } else {
	old=SelectObject(hdc,pen_std[COLOR_STD_BLACK]);
      }
      MoveToEx(hdc,canvas_x,canvas_y,NULL);
      LineTo(hdc,canvas_x+NORMAL_TILE_WIDTH,canvas_y);
      old=SelectObject(hdc,old);
    }
    if (draw_coastline && !draw_terrain) {
      HPEN old;
      enum tile_terrain_type t1 = map_get_terrain(x, y), t2;
      int x1 = x-1, y1 = y;
      old=SelectObject(hdc,pen_std[COLOR_STD_OCEAN]);
      if (normalize_map_pos(&x1, &y1)) {
        t2 = map_get_terrain(x1, y1);
        /* left side */
        if ((t1 == T_OCEAN) ^ (t2 == T_OCEAN)) {
	  MoveToEx(hdc,canvas_x,canvas_y,NULL);
	  LineTo(hdc,canvas_x,canvas_y+NORMAL_TILE_HEIGHT);
          
	}
      }
      /* top side */
      x1 = x; y1 = y-1;
      if (normalize_map_pos(&x1, &y1)) {
        t2 = map_get_terrain(x1, y1);
        if ((t1 == T_OCEAN) ^ (t2 == T_OCEAN)) {
	  MoveToEx(hdc,canvas_x,canvas_y,NULL);
	  LineTo(hdc,canvas_x+NORMAL_TILE_WIDTH,canvas_y);
	}
      }
      SelectObject(hdc,old);
    }
  } else {
    /* tile is unknown */
    BitBlt(hdc,canvas_x,canvas_y,NORMAL_TILE_WIDTH,NORMAL_TILE_HEIGHT,
	   NULL,0,0,BLACKNESS);
  }

  if (!citymode) {
    /* put any goto lines on the tile. */
    if (is_real_tile(x, y)) {
      int dir;
      for (dir = 0; dir < 8; dir++) {
        if (get_drawn(x, y, dir)) {
          put_line(hdc, x, y, dir,0);
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
        put_line(hdc, line_x, line_y, 2,0);
      }
    }
  }
}

/**************************************************************************

**************************************************************************/
void put_city_tile_output(HDC hdc, int canvas_x, int canvas_y,
			  int food, int shield, int trade)
{
  food = CLIP(0, food, NUM_TILES_DIGITS-1);
  trade = CLIP(0, trade, NUM_TILES_DIGITS-1);
  shield = CLIP(0, shield, NUM_TILES_DIGITS-1);  
  if (is_isometric) {
    canvas_x += NORMAL_TILE_WIDTH/3;
    canvas_y -= NORMAL_TILE_HEIGHT/3;
  }

  draw_sprite(sprites.city.tile_foodnum[food],hdc,canvas_x,canvas_y);
  draw_sprite(sprites.city.tile_shieldnum[shield],hdc,canvas_x,canvas_y);
  draw_sprite(sprites.city.tile_tradenum[trade],hdc,canvas_x,canvas_y); 

}

/**************************************************************************

**************************************************************************/
SPRITE *get_citizen_sprite(int frame)
{
  frame = CLIP(0, frame, NUM_TILES_CITIZEN-1);
  return sprites.citizen[frame];
}           

/**************************************************************************

**************************************************************************/
int
map_canvas_adjust_x(int x)
{
  if (map_view_x+map_view_width<=map.xsize)
    return x-map_view_x;
  else if (x>=map_view_x)
    return x-map_view_x;
  else if (x<map_adjust_x(map_view_x+map_view_width))
    return x+map.xsize-map_view_x;
  return -1;
}


/**************************************************************************

**************************************************************************/
int
map_canvas_adjust_y(int y)
{
  return y-map_view_y;
}

/**************************************************************************

**************************************************************************/
int
tile_visible_mapcanvas(int x, int y)
{
  if (is_isometric)
    {
      int dummy_x, dummy_y;
      return get_canvas_xy(x,y,&dummy_x,&dummy_y);
    }
  else
    {
      return (y>=map_view_y && y<map_view_y+map_view_height &&
	      ((x>=map_view_x && x<map_view_x+map_view_width) ||
	       (x+map.xsize>=map_view_x &&
		x+map.xsize<map_view_x+map_view_width)));   
    }    
  
}

/**************************************************************************

**************************************************************************/
int
tile_visible_and_not_on_border_mapcanvas(int x, int y)
{
  return ((y>=map_view_y+2 || (y >= map_view_y && map_view_y == 0))
          && (y<map_view_y+map_view_height-2 ||
              (y<map_view_y+map_view_height &&
               map_view_y + map_view_height-EXTRA_BOTTOM_ROW == map.ysize))
          && ((x>=map_view_x+2 && x<map_view_x+map_view_width-2) ||
              (x+map.xsize>=map_view_x+2
               && x+map.xsize<map_view_x+map_view_width-2)));
}


/**************************************************************************

**************************************************************************/
void
update_info_label(void)
{
  char buffer2[512];
  RECT rc;
  char buffer[512];
  int  d;
  HBITMAP old;
  HDC hdc;
  my_snprintf(buffer, sizeof(buffer),
	      _("Population: %s\nYear: %s\nGold %d\nTax: %d Lux: %d Sci: %d"),
		population_to_text(civ_population(game.player_ptr)),
		textyear( game.year ),
		game.player_ptr->economic.gold,
		game.player_ptr->economic.tax,
		game.player_ptr->economic.luxury,
		game.player_ptr->economic.science );      
  my_snprintf(buffer2,sizeof(buffer2),
	      "%s\n%s",get_nation_name(game.player_ptr->nation),buffer);
  SetWindowText(infolabel_win,buffer2);
  do_mainwin_layout();
  if (!indicatorbmp)
    {
      hdc=GetDC(root_window);
      if (!indicatordc)
	indicatordc=CreateCompatibleDC(hdc);
      
      indicatorbmp=CreateCompatibleBitmap(hdc,
					  10*SMALL_TILE_WIDTH,
					  2*SMALL_TILE_HEIGHT);
      ReleaseDC(root_window,hdc);
    }
  old=SelectObject(indicatordc,indicatorbmp);
  BitBlt(indicatordc,0,0,10*SMALL_TILE_WIDTH,2*SMALL_TILE_HEIGHT,NULL,0,0,
	 WHITENESS);
  set_indicator_icons(client_research_sprite(),
		      client_warming_sprite(),
                      client_cooling_sprite(),
                      game.player_ptr->government);    
  d=0;
  for(;d<(game.player_ptr->economic.luxury)/10;d++)
    draw_sprite(get_citizen_sprite(0), indicatordc,
		SMALL_TILE_WIDTH*d,0);/* elvis tile */
  for(;d<(game.player_ptr->economic.science+game.player_ptr->economic.luxury)/10;d++)
    draw_sprite(get_citizen_sprite(1), indicatordc,
		SMALL_TILE_WIDTH*d,0); /* scientist tile */    
  for(;d<10;d++)
    draw_sprite(get_citizen_sprite(2), indicatordc,
		SMALL_TILE_WIDTH*d,0); /* taxman tile */          
  update_timeout_label();    
  SelectObject(indicatordc,old);
  rc.left=0;
  rc.top=taxinfoline_y;
  rc.right=rc.left+10*SMALL_TILE_WIDTH;
  rc.bottom=rc.top+2*SMALL_TILE_HEIGHT;
  InvalidateRect(root_window,&rc,FALSE);
}

/**************************************************************************

**************************************************************************/
void
update_unit_info_label(struct unit *punit)
{
    if(punit) {
    char buffer[512];
    char buffer2[512];
    
    struct city *pcity;
    pcity=player_find_city_by_id(game.player_ptr, punit->homecity);
 
    my_snprintf(buffer, sizeof(buffer), "%s %s",
            unit_type(punit)->name,
            (punit->veteran) ? _("(veteran)") : "" );
    /* FIXME */         
    my_snprintf(buffer2, sizeof(buffer2), "%s\n%s\n%s\n%s",buffer,
		unit_activity_text(punit),
		map_get_tile_info_text(punit->x, punit->y),
		pcity ? pcity->name : "");     
    SetWindowText(unitinfo_win,buffer2);
    }
    else
      {
	SetWindowText(unitinfo_win,"");
	
      }
    do_mainwin_layout();
}

/**************************************************************************

**************************************************************************/
void
update_timeout_label(void)
{
	/* PORTME */
}

/**************************************************************************

**************************************************************************/
void
update_turn_done_button(int do_restore)
{
	/* PORTME */
}

/**************************************************************************

**************************************************************************/
void
set_indicator_icons(int bulb, int sol, int flake, int gov)
{

  struct Sprite *gov_sprite;
  if (!indicatordc) return;
  bulb = CLIP(0, bulb, NUM_TILES_PROGRESS-1);
  sol = CLIP(0, sol, NUM_TILES_PROGRESS-1);
  flake = CLIP(0, flake, NUM_TILES_PROGRESS-1);     

  draw_sprite(sprites.bulb[bulb],indicatordc,0,SMALL_TILE_HEIGHT);
  draw_sprite(sprites.warming[sol],indicatordc,
	      SMALL_TILE_WIDTH,
	      SMALL_TILE_HEIGHT);
  draw_sprite(sprites.cooling[flake],indicatordc,
	      2*SMALL_TILE_WIDTH,SMALL_TILE_HEIGHT);
  if (game.government_count==0) {
    /* not sure what to do here */
    gov_sprite = sprites.citizen[7];
  } else {
    gov_sprite = get_government(gov)->sprite;    
  }
  draw_sprite(gov_sprite,indicatordc,3*SMALL_TILE_WIDTH,SMALL_TILE_HEIGHT);
  
}

/**************************************************************************

**************************************************************************/
void
set_overview_dimensions(int x, int y)
{
  HDC hdc;
  HBITMAP newbit;
  set_overview_win_dim(2*x,2*y);
  hdc=GetDC(root_window);
  newbit=CreateCompatibleBitmap(hdc,
				overview_win_width,
				overview_win_height);
  ReleaseDC(root_window,hdc);
  SelectObject(overviewstoredc,newbit);
  if (overviewstorebitmap)
    DeleteObject(overviewstorebitmap);
  overviewstorebitmap=newbit;
  BitBlt(overviewstoredc,0,0,overview_win_width,overview_win_height,
	 NULL,0,0,BLACKNESS);
  update_map_canvas_scrollbars_size();
}

/**************************************************************************

**************************************************************************/
void
overview_update_tile(int x, int y)
{
  HDC hdc;
  RECT rc;
  int screen_width=is_isometric?map_view_width+map_view_height:map_view_width;
  int pos = x + map.xsize/2 - (map_view_x + screen_width/2);
  
  pos %= map.xsize;
  if (pos < 0)
    pos += map.xsize;
  
 
  rc.left=x*2;
  rc.right=rc.left+2;
  rc.top=y*2;
  rc.bottom=rc.top+2;
  FillRect(overviewstoredc,&rc,brush_std[overview_tile_color(x, y)]);
  hdc=GetDC(root_window);
  rc.left=pos*2+overview_win_x;
  rc.top=y*2+overview_win_y;
  rc.right=rc.left+2;
  rc.bottom=rc.top+2;
  FillRect(hdc,&rc,brush_std[overview_tile_color(x,y)]);
  ReleaseDC(root_window,hdc);
}

/**************************************************************************
Centers the mapview around (x, y).

This function is almost identical between all GUI's.
**************************************************************************/
void center_tile_mapcanvas(int x, int y)
{
  base_center_tile_mapcanvas(x, y, &map_view_x, &map_view_y,
			     map_view_width, map_view_height);

  update_map_canvas_visible();
  update_map_canvas_scrollbars();

  refresh_overview_viewrect_real(NULL);
}

/**************************************************************************

**************************************************************************/
void
get_center_tile_mapcanvas(int *x, int *y)
{
  *x=map_adjust_x(map_view_x+map_view_width/2);
  *y=map_adjust_y(map_view_y+map_view_height/2);    
}

/**************************************************************************

**************************************************************************/
void
update_map_canvas(int x, int y, int width, int height,
                       int write_to_screen)
{
  if (!is_isometric) {
    int x_itr, y_itr;
    int canvas_x,canvas_y;
    int old_x;
    old_x=-1;
    for(y_itr=y; y_itr<y+height; y_itr++)
      for(x_itr=x; x_itr<x+width; x_itr++) {
	int map_x = map_adjust_x(x_itr);
	int map_y = y_itr; /* not adjusted;, we want to draw black tiles */
	get_canvas_xy(map_x,map_y,&canvas_x,&canvas_y);
	if (tile_visible_mapcanvas(map_x,map_y))
	  {
	    old_x=map_x;
	    pixmap_put_tile(mapstoredc, map_x, map_y,
			    canvas_x,canvas_y, 0);
	  }
      }
    if (write_to_screen)
      {
	int scr_x,scr_y;
	HDC whdc;
	whdc=GetDC(root_window);
	get_canvas_xy(x,y,&scr_x,&scr_y);
	BitBlt(whdc,map_win_x+scr_x,
	       map_win_y+scr_y,
	       NORMAL_TILE_WIDTH*width,NORMAL_TILE_HEIGHT*height,mapstoredc,
	       scr_x,
	       scr_y,
	       SRCCOPY);
	show_city_descriptions(whdc);
	ReleaseDC(root_window,whdc);
      }
  } else {
    int i,x_itr,y_itr;
    
    put_one_tile(x-1,y-1,D_B_LR);
    for(i=0;i<height-1;i++) {
      int x1 = x - 1;
      int y1 = y + i;
      put_one_tile(x1, y1, D_MB_LR);
      
    }
    put_one_tile(x-1, y+height-1, D_TMB_R); /* last tile left side. */
    
    for (i=0; i<width-1; i++) { /* top side */
      int x1 = x + i;
      int y1 = y - 1;
      put_one_tile(x1, y1, D_MB_LR);
    }
    if (width > 1) /* last tile top side. */
      put_one_tile(x+width-1, y-1, D_TMB_L);
    else
      put_one_tile(x+width-1, y-1, D_MB_L);
    /*** Now draw the tiles to be refreshed, from the top down to get the
         overlapping areas correct ***/
    for (x_itr = x; x_itr < x+width; x_itr++) {
      for (y_itr = y; y_itr < y+height; y_itr++) {
	put_one_tile(x_itr, y_itr, D_FULL);
      }
    }
    
      /*** Then draw the tiles underneath to refresh the parts of them that
	   overlap onto the area just drawn ***/
    put_one_tile(x, y+height, D_TM_R);  /* bottom side */
    for (i=1; i<width; i++) {
      int x1 = x + i;
      int y1 = y + height;
      put_one_tile(x1, y1, D_TM_R);
      put_one_tile(x1, y1, D_T_L);
    }
    
    put_one_tile(x+width, y, D_TM_L); /* right side */
    for (i=1; i < height; i++) {
      int x1 = x + width;
      int y1 = y + i;
      put_one_tile(x1, y1, D_TM_L);
      put_one_tile(x1, y1, D_T_R);
    }
    /*** Draw the goto line on top of the whole thing. Done last as
	 we want it completely on top. ***/
    /* Drawing is cheap; we just draw all the lines.
       Perhaps this should be optimized, though... */
    for (x_itr = x-1; x_itr <= x+width; x_itr++) {
      for (y_itr = y-1; y_itr <= y+height; y_itr++) {
	int x1 = x_itr;
	int y1 = y_itr;
	if (normalize_map_pos(&x1, &y1)) {
	  adjc_dir_iterate(x1, y1, x2, y2, dir) {
	    if (get_drawn(x1, y1, dir)) {
	      really_draw_segment(x1, y1, dir, 0, 1);
	    }
	  } adjc_dir_iterate_end;
	}
      } 
    }
    
    if (write_to_screen) {
      HDC hdc;
      int canvas_start_x, canvas_start_y;
      get_canvas_xy(x, y, &canvas_start_x, &canvas_start_y); /* top left corner */
      /* top left corner in isometric view */
      canvas_start_x -= height * NORMAL_TILE_WIDTH/2;
      
      /* because of where get_canvas_xy() sets canvas_x */
      canvas_start_x += NORMAL_TILE_WIDTH/2;
      
      /* And because units fill a little extra */
      canvas_start_y -= NORMAL_TILE_HEIGHT/2;
      
      /* here we draw a rectangle that includes the updated tiles. */
      hdc=GetDC(root_window);
      BitBlt(hdc,canvas_start_x+map_win_x,canvas_start_y+map_win_y,
	     (height + width) * NORMAL_TILE_WIDTH/2,
	     (height + width) * NORMAL_TILE_HEIGHT/2 + NORMAL_TILE_HEIGHT/2,
	     mapstoredc,
	     canvas_start_x,canvas_start_y,SRCCOPY);
      show_city_descriptions(hdc);
      ReleaseDC(root_window,hdc);
    }


  }
}

/**************************************************************************

**************************************************************************/
void
update_map_canvas_visible(void)
{
  if (is_isometric)
    {
      int width,height;
      width=height=map_view_width+map_view_height;
      update_map_canvas(map_view_x,
			map_view_y-map_view_width,
			width,height,1); 
      /*
	update_map_canvas(0,0,map.xsize,map.ysize,1); */
    }
  else
    {
      update_map_canvas(map_view_x,map_view_y,
			map_view_width,map_view_height, 1);
    }
}

/**************************************************************************

**************************************************************************/
void update_map_canvas_scrollbars_size(void)
{
  ScrollBar_SetRange(map_scroll_h,0,map.xsize,TRUE);
  ScrollBar_SetRange(map_scroll_v,0,map.ysize+EXTRA_BOTTOM_ROW,TRUE);
}

/**************************************************************************

**************************************************************************/
void
update_map_canvas_scrollbars(void)
{
  ScrollBar_SetPos(map_scroll_h,map_view_x,TRUE);
  ScrollBar_SetPos(map_scroll_v,map_view_y,TRUE);
}

/**************************************************************************

**************************************************************************/
void
update_city_descriptions(void)
{
  update_map_canvas_visible();   
      
}

/**************************************************************************

**************************************************************************/
static void show_desc_at_tile(HDC hdc,int x, int y)
{
  char buffer[500];
  int y_offset;
  int canvas_x,canvas_y;
  struct city *pcity;
  if ((pcity = map_get_city(x, y))) {
    get_canvas_xy(x, y, &canvas_x, &canvas_y);
    y_offset=canvas_y+map_win_y+NORMAL_TILE_HEIGHT;
    if ((draw_city_names)&&(pcity->name)) {
      RECT rc;
      DrawText(hdc,pcity->name,strlen(pcity->name),&rc,DT_CALCRECT);
      rc.left=canvas_x+NORMAL_TILE_WIDTH/2-10+map_win_x;
      rc.right=rc.left+20;
      rc.bottom-=rc.top;
      rc.top=y_offset;
      rc.bottom+=rc.top;
      SetTextColor(hdc,RGB(0,0,0));
      DrawText(hdc,pcity->name,strlen(pcity->name),&rc,
	       DT_NOCLIP | DT_CENTER);
      rc.left++;
      rc.top--;
      rc.right++;
      rc.bottom--;
      SetTextColor(hdc,RGB(255,255,255));
      DrawText(hdc,pcity->name,strlen(pcity->name),&rc,
	       DT_NOCLIP | DT_CENTER);
      y_offset=rc.bottom+2;
    }      
    if (draw_city_productions && (pcity->owner==game.player_idx)) {
      int turns;
      RECT rc;
      struct unit_type *punit_type;
      struct impr_type *pimprovement_type;
      
      turns = city_turns_to_build(pcity, pcity->currently_building,
				  pcity->is_building_unit,TRUE);
      
      if (pcity->is_building_unit) {
	punit_type = get_unit_type(pcity->currently_building);
	my_snprintf(buffer, sizeof(buffer), "%s %d",
		    punit_type->name, turns);
      } else {
	pimprovement_type =
	  get_improvement_type(pcity->currently_building);
	if (pcity->currently_building == B_CAPITAL) {
	  sz_strlcpy(buffer, pimprovement_type->name);
	} else {
	  my_snprintf(buffer, sizeof(buffer), "%s %d",
		      pimprovement_type->name, turns);
	}
      }
      
      DrawText(hdc,buffer,strlen(buffer),&rc,DT_CALCRECT);
      rc.left=canvas_x+NORMAL_TILE_WIDTH/2-10+map_win_x;
      rc.right=rc.left+20;
      rc.bottom-=rc.top;
      rc.top=y_offset;
      rc.bottom+=rc.top; 
      SetTextColor(hdc,RGB(0,0,0));
      DrawText(hdc,buffer,strlen(buffer),&rc,
	       DT_NOCLIP | DT_CENTER);
      rc.left++;
      rc.top--;
      rc.right++;
      rc.bottom--;
      SetTextColor(hdc,RGB(255,255,255));
      DrawText(hdc,buffer,strlen(buffer),&rc,
	       DT_NOCLIP | DT_CENTER);
    }
  }
  
}

/**************************************************************************

**************************************************************************/
static void show_city_descriptions(HDC hdc)
{
  if (!draw_city_names && !draw_city_productions)
    return;
  SetBkMode(hdc,TRANSPARENT);
  if (!cliprgn)
    {
      RECT rc;
      rc.top=map_win_y;
      rc.left=map_win_x;
      rc.right=rc.left+map_win_width;
      rc.bottom=rc.top+map_win_height;
      cliprgn=CreateRectRgnIndirect(&rc);
    }
  SelectClipRgn(hdc,cliprgn);
  if (is_isometric ) {
    int x, y;
    int w, h;
    
    for (h=-1; h<map_view_height*2; h++) {
      int x_base = map_view_x + h/2 + (h != -1 ? h%2 : 0);
      int y_base = map_view_y + h/2 + (h == -1 ? -1 : 0);
      for (w=0; w<=map_view_width; w++) {
        x = (x_base + w);
        y = y_base - w;
        if (normalize_map_pos(&x, &y)) {
          show_desc_at_tile(hdc, x, y);
        }
      }
    }
  } else { /* is_isometric */
    int x1, y1;
    for (x1 = 0; x1 < map_view_width; x1++) {
      int x = map_view_x + x1;
      for (y1 = 0; y1 < map_view_width; y1++) {
        int y = map_view_y + y1;

        if (normalize_map_pos(&x, &y)) {
          show_desc_at_tile(hdc, x, y);
        }
      }
    }
  }
  

  SelectClipRgn(hdc,NULL);
}

/**************************************************************************

**************************************************************************/
void
put_cross_overlay_tile(int x,int y)
{
  HDC hdc;
  int canvas_x, canvas_y;
  get_canvas_xy(x, y, &canvas_x, &canvas_y);
  if (tile_visible_mapcanvas(x, y)) {
    hdc=GetDC(root_window);
    draw_sprite(sprites.user.attention,hdc,canvas_x,canvas_y);
    ReleaseDC(root_window,hdc);
  }
}

/**************************************************************************

**************************************************************************/
void
put_city_workers(struct city *pcity, int color)
{
	/* PORTME */
}

/**************************************************************************

**************************************************************************/
void
move_unit_map_canvas(struct unit *punit, int x0, int y0, int dx, int dy)
{

  static struct timer *anim_timer = NULL; 
  int dest_x, dest_y;
  
  
  /* only works for adjacent-square moves */
  if ((dx < -1) || (dx > 1) || (dy < -1) || (dy > 1) ||
      ((dx == 0) && (dy == 0))) {
    return;
  }
  
  if (punit == get_unit_in_focus() && hover_state != HOVER_NONE) {
    set_hover_state(NULL, HOVER_NONE);
    update_unit_info_label(punit);
  }
  
  dest_x = map_adjust_x(x0+dx);
  dest_y = map_adjust_y(y0+dy);
  
  if (player_can_see_unit(game.player_ptr, punit) &&
      (tile_visible_mapcanvas(x0, y0) ||
       tile_visible_mapcanvas(dest_x, dest_y))) {
    int i, steps;
    int start_x, start_y;
    int this_x, this_y;
    int canvas_dx, canvas_dy;
    HDC hdc,hdcwin;
    HBITMAP oldbmp;
    hdc=CreateCompatibleDC(mapstoredc);
    oldbmp=SelectObject(hdc,single_tile_pixmap);
    hdcwin=GetDC(root_window);
    if (is_isometric) {
      if (dx == 0) {
        canvas_dx = -NORMAL_TILE_WIDTH/2 * dy;
        canvas_dy = NORMAL_TILE_HEIGHT/2 * dy;
      } else if (dy == 0) {
        canvas_dx = NORMAL_TILE_WIDTH/2 * dx;
        canvas_dy = NORMAL_TILE_HEIGHT/2 * dx;
      } else {
        if (dx > 0) {
          if (dy > 0) {
            canvas_dx = 0;
            canvas_dy = NORMAL_TILE_HEIGHT;
          } else { /* dy < 0 */
            canvas_dx = NORMAL_TILE_WIDTH;
            canvas_dy = 0;
          }
        } else { /* dx < 0 */
          if (dy > 0) {
            canvas_dx = -NORMAL_TILE_WIDTH;
            canvas_dy = 0;
          } else { /* dy < 0 */
            canvas_dx = 0;
            canvas_dy = -NORMAL_TILE_HEIGHT;
          }
        }
      }
    } else {
      canvas_dx = NORMAL_TILE_WIDTH * dx;
      canvas_dy = NORMAL_TILE_HEIGHT * dy;
    }
    
    if (smooth_move_unit_steps < 2) {
      steps = 2;
    } else if (smooth_move_unit_steps > MAX(abs(canvas_dx), abs(canvas_dy))) {
      steps = MAX(abs(canvas_dx), abs(canvas_dy));
    } else {
      steps = smooth_move_unit_steps;
    }
    
    get_canvas_xy(x0, y0, &start_x, &start_y);
    if (is_isometric) {
      start_y -= NORMAL_TILE_HEIGHT/2;
    }
    
    this_x = start_x;
    this_y = start_y;
    
    for (i = 1; i <= steps; i++) {
      anim_timer = renew_timer_start(anim_timer, TIMER_USER, TIMER_ACTIVE);
      /* FIXME: We need to draw units on tiles below the moving unit on top. */
      BitBlt(hdcwin,this_x+map_win_x,this_y+map_win_y,UNIT_TILE_WIDTH,
	     UNIT_TILE_HEIGHT,mapstoredc,this_x,this_y,SRCCOPY);
      this_x = start_x + ((i * canvas_dx)/steps);
      this_y = start_y + ((i * canvas_dy)/steps);
      BitBlt(hdc,0,0,UNIT_TILE_WIDTH,UNIT_TILE_HEIGHT,mapstoredc,
	     this_x,this_y,SRCCOPY);
      put_unit_pixmap(punit, hdc, 0, 0);
      BitBlt(hdcwin,this_x+map_win_x,this_y+map_win_y,UNIT_TILE_WIDTH,
	     UNIT_TILE_HEIGHT,hdc,0,0,SRCCOPY);
      
      GdiFlush();
      if (i < steps) {
	
	usleep_since_timer_start(anim_timer, 10000);
      }
    }
    SelectObject(hdc,oldbmp);
    DeleteDC(hdc);
    ReleaseDC(root_window,hdcwin);
  }
}

/**************************************************************************
 This function is called to decrease a unit's HP smoothly in battle
 when combat_animation is turned on.
**************************************************************************/
void
decrease_unit_hp_smooth(struct unit *punit0, int hp0,
                             struct unit *punit1, int hp1)
{
  HDC hdc,hdcwin;
  HBITMAP oldbmp;
  static struct timer *anim_timer = NULL; 
  struct unit *losing_unit = (hp0 == 0 ? punit0 : punit1);
  int i;
  
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
    GdiFlush();
    
    usleep_since_timer_start(anim_timer, 10000);
    
  } while (punit0->hp > hp0 || punit1->hp > hp1);

  hdc=CreateCompatibleDC(NULL);
  hdcwin=GetDC(root_window);
  oldbmp=SelectObject(hdc,single_tile_pixmap);
  for (i = 0; i < num_tiles_explode_unit; i++) {
    int canvas_x, canvas_y;
    get_canvas_xy(losing_unit->x, losing_unit->y, &canvas_x, &canvas_y);
    anim_timer = renew_timer_start(anim_timer, TIMER_USER, TIMER_ACTIVE);
    if (is_isometric) {
      /* We first draw the explosion onto the unit and draw draw the
         complete thing onto the map canvas window. This avoids flickering. */
      BitBlt(hdc,0,0,NORMAL_TILE_WIDTH,NORMAL_TILE_HEIGHT,
	     mapstoredc,canvas_x,canvas_y,SRCCOPY);
      draw_sprite(sprites.explode.unit[i],hdc,NORMAL_TILE_WIDTH/4,0);
      BitBlt(hdcwin,canvas_x+map_win_x,canvas_y+map_win_y,
	     NORMAL_TILE_WIDTH,NORMAL_TILE_HEIGHT,
	     hdc,0,0,SRCCOPY);
    } else {
      pixmap_put_tile(hdc, losing_unit->x, losing_unit->y,
                      0, 0, 0);
      put_unit_pixmap(losing_unit, hdc, 0, 0);
      draw_sprite(sprites.explode.unit[i],hdc,NORMAL_TILE_WIDTH/4,0);
      BitBlt(hdcwin,map_win_x+canvas_x,map_win_y+canvas_y,
	     NORMAL_TILE_WIDTH,NORMAL_TILE_HEIGHT,
	     hdc,0,0,SRCCOPY);
    }
    GdiFlush();
    usleep_since_timer_start(anim_timer, 20000);
  }
  
  SelectObject(hdc,oldbmp);
  DeleteDC(hdc);
  ReleaseDC(root_window,hdcwin);
  set_units_in_combat(NULL, NULL);
  refresh_tile_mapcanvas(punit0->x, punit0->y, 1);
  refresh_tile_mapcanvas(punit1->x, punit1->y, 1);
  
}

/**************************************************************************

**************************************************************************/
void
put_nuke_mushroom_pixmaps(int abs_x0, int abs_y0)
{
  int x, y;
  HDC hdc;
  hdc=GetDC(root_window);
  for(y=0; y<3; y++) {
    for(x=0; x<3; x++) {
      int map_x = map_canvas_adjust_x(x-1+abs_x0)*NORMAL_TILE_WIDTH;
      int map_y = map_canvas_adjust_y(y-1+abs_y0)*NORMAL_TILE_HEIGHT;
      struct Sprite *mysprite = sprites.explode.nuke[y][x];
      if (mysprite)
	draw_sprite(mysprite,hdc,map_win_x+map_x,map_win_y+map_y);
    }
  }
  
  ReleaseDC(root_window,hdc);
  Sleep(1000);
 
  update_map_canvas(abs_x0-1,
                    abs_y0-1,
                    3, 3, 1);          
}

/**************************************************************************

**************************************************************************/
void
refresh_overview_canvas(void)
{
  HDC hdc=GetDC(root_window);
  int pos;
  RECT rc;

  whole_map_iterate(x, y) {
    rc.left = x * 2;
    rc.right = rc.left + 2;
    rc.top = y * 2;
    rc.bottom = rc.top + 2;
    FillRect(overviewstoredc, &rc, brush_std[overview_tile_color(x, y)]);
    pos = x + map.xsize / 2 - (map_view_x + map_win_width / 2);

    pos %= map.xsize;
    if (pos < 0)
      pos += map.xsize;
    rc.left = overview_win_x + pos * 2;
    rc.right = rc.left + 2;
    rc.top = overview_win_y + y * 2;
    rc.bottom = rc.top + 2;
    FillRect(hdc, &rc, brush_std[overview_tile_color(x, y)]);
  } whole_map_iterate_end;

  ReleaseDC(root_window,hdc);
}

/**************************************************************************

**************************************************************************/
void
refresh_overview_viewrect_real(HDC hdcp)
{
  int screen_width=is_isometric?map_view_width+map_view_height:map_view_width;
  int delta=map.xsize/2-(map_view_x+screen_width/2);   
  HDC hdc;
  HPEN oldpen;
  hdc=hdcp;
  if (!hdc)
    hdc=GetDC(root_window);
  
  if (delta>=0)
    {
      BitBlt(hdc,overview_win_x+2*delta,overview_win_y,
	     overview_win_width-2*delta,overview_win_height,
	     overviewstoredc,0,0,SRCCOPY);
      BitBlt(hdc,overview_win_x,overview_win_y,2*delta,overview_win_height,
	     overviewstoredc,overview_win_width-2*delta,0,SRCCOPY);
    }
  else
    {
      BitBlt(hdc,overview_win_x,overview_win_y,overview_win_width+2*delta,
	     overview_win_height,overviewstoredc,-2*delta,0,SRCCOPY);
      BitBlt(hdc,overview_win_x+overview_win_width+2*delta,overview_win_y,
	     -2*delta,overview_win_height,overviewstoredc,
	     0,0,SRCCOPY);
    }
  oldpen=SelectObject(hdc,pen_std[COLOR_STD_WHITE]);
  if (is_isometric) {
    int Wx = overview_win_width/2 - screen_width /* *2/2 */;
    int Wy = map_view_y * 2;
    int Nx = Wx + 2 * map_view_width;
    int Ny = Wy - 2 * map_view_width;
    int Sx = Wx + 2 * map_view_height;
    int Sy = Wy + 2 * map_view_height;
    int Ex = Nx + 2 * map_view_height;
    int Ey = Ny + 2 * map_view_height;
    
    freelog(LOG_DEBUG, "wx,wy: %d,%d nx,ny:%d,%x ex,ey:%d,%d, sx,sy:%d,%d",
            Wx, Wy, Nx, Ny, Ex, Ey, Sx, Sy);
    MoveToEx(hdc,Wx+overview_win_x,Wy+overview_win_y,NULL);
    LineTo(hdc,Nx+overview_win_x,Ny+overview_win_y);
    LineTo(hdc,Ex+overview_win_x,Ey+overview_win_y);
    LineTo(hdc,Sx+overview_win_x,Sy+overview_win_y);
    LineTo(hdc,Wx+overview_win_x,Wy+overview_win_y);
  } else {
    mydrawrect(hdc,
	       (overview_win_width-2*map_view_width)/2+overview_win_x,
	       2*map_view_y+overview_win_y,
	       2*map_view_width,
	       2*map_view_height);
  }
  SelectObject(hdc,oldpen);
  if (!hdcp)
    ReleaseDC(root_window,hdc);
}

/**************************************************************************

**************************************************************************/
void refresh_overview_viewrect(void)
{
  refresh_overview_viewrect_real(NULL);
}

/**************************************************************************

**************************************************************************/
void overview_expose(HDC hdc)
{
  if (get_client_state()!=CLIENT_GAME_RUNNING_STATE)
    {
      if (radar_gfx_sprite)
	{
	  draw_sprite(radar_gfx_sprite,hdc,overview_win_x,overview_win_y);
	}
    }
  else
    {
      refresh_overview_viewrect_real(hdc);
    }
}

/**************************************************************************

**************************************************************************/
void map_handle_hscroll(int pos)
{
  if (get_client_state()!=CLIENT_GAME_RUNNING_STATE)
    return;
  map_view_x=pos;
  update_map_canvas_visible();
  refresh_overview_viewrect();                                                
}

/**************************************************************************

**************************************************************************/
void map_handle_vscroll(int pos)
{
  if (get_client_state()!=CLIENT_GAME_RUNNING_STATE)
    return;
  map_view_y=pos;
  map_view_y=(map_view_y<0)?0:map_view_y;
  map_view_y= (map_view_y>map.ysize+EXTRA_BOTTOM_ROW-map_view_height) ?
        map.ysize+EXTRA_BOTTOM_ROW-map_view_height :
        map_view_y;
  update_map_canvas_visible();
  refresh_overview_viewrect();
}

/**************************************************************************
Finds the pixel coordinates of a tile.  Beside setting the results in
canvas_x,canvas_y it returns whether the tile is inside the visible
map.

This function is almost identical between all GUI's.
**************************************************************************/
static int get_canvas_xy(int map_x, int map_y, int *canvas_x,
			 int *canvas_y)
{
  return map_pos_to_canvas_pos(map_x, map_y, canvas_x, canvas_y,
			       map_view_x, map_view_y, map_win_width,
			       map_win_height);
}


/**************************************************************************
Finds the map coordinates corresponding to pixel coordinates.

This function is almost identical between all GUI's.
**************************************************************************/
void get_map_xy(int canvas_x, int canvas_y, int *map_x, int *map_y)
{
  canvas_pos_to_map_pos(canvas_x, canvas_y, map_x, map_y, map_view_x,
			map_view_y);
}

/**************************************************************************
Only used for isometric view.
**************************************************************************/
static void pixmap_put_black_tile_iso(HDC hdc,
                                      int canvas_x, int canvas_y,
                                      int offset_x, int offset_y,
                                      int width, int height)
{
  draw_sprite_part(sprites.black_tile,hdc,canvas_x+offset_x,canvas_y+offset_y,
		   width,height,
		   offset_x,offset_x);
}

/**************************************************************************

**************************************************************************/
static void dither_tile(HDC hdc, struct Sprite **dither,
                        int canvas_x, int canvas_y,
                        int offset_x, int offset_y,
                        int width, int height, int fog)
{
  if (!width || !height)
    return;
  
  assert(offset_x == 0 || offset_x == NORMAL_TILE_WIDTH/2);
  assert(offset_y == 0 || offset_y == NORMAL_TILE_HEIGHT/2);
  assert(width == NORMAL_TILE_WIDTH || width == NORMAL_TILE_WIDTH/2);
  assert(height == NORMAL_TILE_HEIGHT || height == NORMAL_TILE_HEIGHT/2);
  
  /* north */
  if (dither[0]
      && (offset_x != 0 || width == NORMAL_TILE_WIDTH)
      && (offset_y == 0)) {
    draw_sprite_part_with_mask(dither[0],sprites.dither_tile,hdc,
			       canvas_x + NORMAL_TILE_WIDTH/2, canvas_y,
			       NORMAL_TILE_WIDTH/2, NORMAL_TILE_HEIGHT/2,
			       NORMAL_TILE_WIDTH/2, 0);
  }

  /* south */
  if (dither[1] && offset_x == 0
      && (offset_y == NORMAL_TILE_HEIGHT/2 || height == NORMAL_TILE_HEIGHT)) {
    draw_sprite_part_with_mask(dither[1],sprites.dither_tile,hdc,
			       canvas_x,
			       canvas_y + NORMAL_TILE_HEIGHT/2,
			       NORMAL_TILE_WIDTH/2, NORMAL_TILE_HEIGHT/2,
			       0, NORMAL_TILE_HEIGHT/2);
  }

  /* east */
  if (dither[2]
      && (offset_x != 0 || width == NORMAL_TILE_WIDTH)
      && (offset_y != 0 || height == NORMAL_TILE_HEIGHT)) {
    draw_sprite_part_with_mask(dither[2],sprites.dither_tile,hdc,
			       canvas_x + NORMAL_TILE_WIDTH/2,
			       canvas_y + NORMAL_TILE_HEIGHT/2,
			       NORMAL_TILE_WIDTH/2, NORMAL_TILE_HEIGHT/2,
			       NORMAL_TILE_WIDTH/2, NORMAL_TILE_HEIGHT/2);
  }

  /* west */
  if (dither[3] && offset_x == 0 && offset_y == 0) {
    draw_sprite_part_with_mask(dither[3],sprites.dither_tile,hdc,
			       canvas_x,
			       canvas_y,
			       NORMAL_TILE_WIDTH/2, NORMAL_TILE_HEIGHT/2,
			       0,0);
  }


  if (fog) {
    draw_fog_part(hdc,canvas_x+offset_x, canvas_y+offset_y,
		  MIN(width, MAX(0, NORMAL_TILE_WIDTH-offset_x)),
		  MIN(height, MAX(0, NORMAL_TILE_HEIGHT-offset_y)),
		  offset_x,offset_y);
  }

}

/**************************************************************************
draw a line from src_x,src_y -> dest_x,dest_y on both map_canvas and
map_canvas_store
FIXME: We currently always draw the line.
Only used for isometric view.
**************************************************************************/
static void really_draw_segment(int src_x, int src_y, int dir,
                                int write_to_screen, int force)
{
  HPEN old;
  int dest_x, dest_y, is_real;
  int canvas_start_x, canvas_start_y;
  int canvas_end_x, canvas_end_y;

  is_real = MAPSTEP(dest_x, dest_y, src_x, src_y, dir);
  assert(is_real);

  /* Find middle of tiles. y-1 to not undraw the the middle pixel of a
     horizontal line when we refresh the tile below-between. */
  get_canvas_xy(src_x, src_y, &canvas_start_x, &canvas_start_y);
  get_canvas_xy(dest_x, dest_y, &canvas_end_x, &canvas_end_y);
  canvas_start_x += NORMAL_TILE_WIDTH/2;
  canvas_start_y += NORMAL_TILE_HEIGHT/2-1;
  canvas_end_x += NORMAL_TILE_WIDTH/2;
  canvas_end_y += NORMAL_TILE_HEIGHT/2-1;

  /* somewhat hackish way of solving the problem where draw from a tile on
     one side of the screen out of the screen, and the tile we draw to is
     found to be on the other side of the screen. */
  if (abs(canvas_end_x - canvas_start_x) > NORMAL_TILE_WIDTH
      || abs(canvas_end_y - canvas_start_y) > NORMAL_TILE_HEIGHT)
    return;
 
  old=SelectObject(mapstoredc, pen_std[COLOR_STD_CYAN]);
  /* draw it! */
  MoveToEx(mapstoredc,canvas_start_x,canvas_start_y,NULL);
  LineTo(mapstoredc,canvas_end_x,canvas_end_y);
  SelectObject(mapstoredc,old);
  if (write_to_screen) {
    HDC hdc;
    hdc=GetDC(root_window);
    
    old=SelectObject(hdc, pen_std[COLOR_STD_CYAN]);
    
    MoveToEx(hdc,map_win_x+canvas_start_x,map_win_y+canvas_start_y,NULL);
    LineTo(hdc,map_win_x+canvas_end_x,map_win_y+canvas_end_y);
    SelectObject(hdc,old);
    ReleaseDC(root_window,hdc);
  }
  return;
}



/**************************************************************************
Only used for isometric view.
**************************************************************************/
static void pixmap_put_overlay_tile_draw(HDC hdc,
                                         int canvas_x, int canvas_y,
                                         struct Sprite *ssprite,
                                         int offset_x, int offset_y,
                                         int width, int height,
                                         int fog)
{
  if (!ssprite || !width || !height)
    return;
  
  draw_sprite_part(ssprite,hdc,canvas_x+offset_x,canvas_y+offset_y,
		   MIN(width, MAX(0,ssprite->width-offset_x)),
		   MIN(height, MAX(0,ssprite->height-offset_y)),
		   offset_x,offset_y);

  if (fog) {
    draw_fog_part(hdc,canvas_x+offset_x,canvas_y+offset_y,
		  MIN(width, MAX(0,ssprite->width-offset_x)),
		  MIN(height, MAX(0,ssprite->height-offset_y)),
		  offset_x,offset_y); 
    
  }
  
}

/**************************************************************************
Only used for isometric view.
**************************************************************************/
void put_one_tile_full(HDC hdc, int x, int y,
                       int canvas_x, int canvas_y, int citymode)
{
  pixmap_put_tile_iso(hdc, x, y, canvas_x, canvas_y, citymode,
                      0, 0, 0,
                      NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT, UNIT_TILE_HEIGHT,
                      D_FULL);
}



/**************************************************************************
Only used for isometric view.
**************************************************************************/
static void put_one_tile(int x, int y, enum draw_type draw)
{
  int canvas_x, canvas_y;
  int height, width, height_unit;
  int offset_x, offset_y, offset_y_unit;
  
  if (!tile_visible_mapcanvas(x, y)) {
    freelog(LOG_DEBUG, "dropping %d,%d", x, y);
    return;
  }
  freelog(LOG_DEBUG, "putting %d,%d draw %x", x, y, draw);
  
  width = (draw & D_TMB_L) && (draw & D_TMB_R) ? NORMAL_TILE_WIDTH : NORMAL_TILE_WIDTH/2;
  if (!(draw & D_TMB_L))
    offset_x = NORMAL_TILE_WIDTH/2;
  else
    offset_x = 0;
  
  height = 0;
  if (draw & D_M_LR) height += NORMAL_TILE_HEIGHT/2;
  if (draw & D_B_LR) height += NORMAL_TILE_HEIGHT/2;
  if (draw & D_T_LR)
    height_unit = height + NORMAL_TILE_HEIGHT/2;
  else
    height_unit = height;

  offset_y = (draw & D_M_LR) ? 0 : NORMAL_TILE_HEIGHT/2;
  if (!(draw & D_T_LR))
    offset_y_unit = (draw & D_M_LR) ? NORMAL_TILE_HEIGHT/2 : NORMAL_TILE_HEIGHT;
  else
    offset_y_unit = 0;

  /* returns whether the tile is visible. */
  if (get_canvas_xy(x, y, &canvas_x, &canvas_y)) {
    
    if (normalize_map_pos(&x, &y)) {
    
      pixmap_put_tile_iso(mapstoredc, x, y, canvas_x, canvas_y, 0,
                          offset_x, offset_y, offset_y_unit,
                          width, height, height_unit,
                          draw);
    } else {
      pixmap_put_black_tile_iso(mapstoredc, canvas_x, canvas_y,
                                offset_x, offset_y,
                                width, height);
    }
  }
}


/**************************************************************************
Only used for isometric view.
**************************************************************************/
static void put_unit_pixmap_draw(struct unit *punit, HDC hdc,
                                 int canvas_x, int canvas_y,
                                 int offset_x, int offset_y_unit,
                                 int width, int height_unit)
{
  struct Sprite *sprites[40];
  int dummy;
  int count = fill_unit_sprite_array(sprites, punit, &dummy);
  int i;

  for (i=0; i<count; i++) {
    if (sprites[i]) {
      pixmap_put_overlay_tile_draw(hdc, canvas_x, canvas_y, sprites[i],
                                   offset_x, offset_y_unit,
                                   width, height_unit, 0);
    }
  }
}


/**************************************************************************
Only used for isometric view.
**************************************************************************/
static void put_city_pixmap_draw(struct city *pcity,HDC hdc,
                                 int canvas_x, int canvas_y,
                                 int offset_x, int offset_y_unit,
                                 int width, int height_unit,
                                 int fog)
{
  struct Sprite *sprites[80];
  int count = fill_city_sprite_array_iso(sprites, pcity);
  int i;

  for (i=0; i<count; i++) {
    if (sprites[i]) {
      pixmap_put_overlay_tile_draw(hdc, canvas_x, canvas_y, sprites[i],
                                   offset_x, offset_y_unit,
                                   width, height_unit,
                                   fog);
    }
  }
}


/**************************************************************************
Only used for isometric view.
**************************************************************************/
static void pixmap_put_tile_iso(HDC hdc, int x, int y,
                                int canvas_x, int canvas_y,
                                int citymode,
                                int offset_x, int offset_y, int offset_y_unit,
                                int width, int height, int height_unit,
                                enum draw_type draw)
{
  struct Sprite *tile_sprs[80];
  struct Sprite *coasts[4];
  struct Sprite *dither[4];
  struct city *pcity;
  struct unit *punit, *pfocus;
  enum tile_special_type special;
  int count, i = 0;
  int fog;
  int solid_bg;
  int is_real;

  if (!width || !(height || height_unit))
    return;

  count = fill_tile_sprite_array_iso(tile_sprs, coasts, dither,
                                     x, y, citymode, &solid_bg);

  if (count == -1) { /* tile is unknown */
    pixmap_put_black_tile_iso(hdc, canvas_x, canvas_y,
                              offset_x, offset_y, width, height);
    return;
  }
  is_real = normalize_map_pos(&x, &y);
  assert(is_real);
  fog = tile_get_known(x, y) == TILE_KNOWN_FOGGED && draw_fog_of_war;
  pcity = map_get_city(x, y);
  punit = get_drawable_unit(x, y, citymode);
  pfocus = get_unit_in_focus();
  special = map_get_special(x, y);

  if (solid_bg) {
    HPEN oldpen;
    HBRUSH oldbrush;
    POINT points[4];
    points[0].x=canvas_x+NORMAL_TILE_WIDTH/2;
    points[0].y=canvas_y;
    points[1].x=canvas_x;
    points[1].y=canvas_y+NORMAL_TILE_HEIGHT/2;
    points[2].x=canvas_x+NORMAL_TILE_WIDTH/2;
    points[2].y=canvas_y+NORMAL_TILE_HEIGHT;
    points[3].x=canvas_x+NORMAL_TILE_WIDTH;
    points[3].y=canvas_y+NORMAL_TILE_HEIGHT/2;
    oldpen=SelectObject(hdc,pen_std[COLOR_STD_BACKGROUND]); 
    oldbrush=SelectObject(hdc,brush_std[COLOR_STD_BACKGROUND]);
    Polygon(hdc,points,4);
    SelectObject(hdc,oldpen);
    SelectObject(hdc,oldbrush);
  }
  if (draw_terrain) {
    if (map_get_terrain(x, y) == T_OCEAN) { /* coasts */
      int dx, dy;
      /* top */
      dx = offset_x-NORMAL_TILE_WIDTH/4;
      pixmap_put_overlay_tile_draw(hdc, canvas_x + NORMAL_TILE_WIDTH/4,
                                   canvas_y, coasts[0],
                                   MAX(0, dx),
                                   offset_y,
                                   MAX(0, width-MAX(0, -dx)),
                                   height,
                                   fog);
      /* bottom */
      dx = offset_x-NORMAL_TILE_WIDTH/4;
      dy = offset_y-NORMAL_TILE_HEIGHT/2;
      pixmap_put_overlay_tile_draw(hdc, canvas_x + NORMAL_TILE_WIDTH/4,
                                   canvas_y + NORMAL_TILE_HEIGHT/2, coasts[1],
                                   MAX(0, dx),
                                   MAX(0, dy),
                                   MAX(0, width-MAX(0, -dx)),
                                   MAX(0, height-MAX(0, -dy)),
                                   fog);
      /* left */
      dy = offset_y-NORMAL_TILE_HEIGHT/4;
      pixmap_put_overlay_tile_draw(hdc, canvas_x,
                                   canvas_y + NORMAL_TILE_HEIGHT/4, coasts[2],
                                   offset_x,
                                   MAX(0, dy),
                                   width,
                                   MAX(0, height-MAX(0, -dy)),
                                   fog);
      /* right */
      dx = offset_x-NORMAL_TILE_WIDTH/2;
      dy = offset_y-NORMAL_TILE_HEIGHT/4;
      pixmap_put_overlay_tile_draw(hdc, canvas_x + NORMAL_TILE_WIDTH/2,
                                   canvas_y + NORMAL_TILE_HEIGHT/4, coasts[3],
                                   MAX(0, dx),
                                   MAX(0, dy),
                                   MAX(0, width-MAX(0, -dx)),
                                   MAX(0, height-MAX(0, -dy)),
                                   fog);

    } else {
      pixmap_put_overlay_tile_draw(hdc, canvas_x, canvas_y, tile_sprs[0],
                                   offset_x, offset_y, width, height, fog);
      i++;
    }
    if (draw_terrain) {
      dither_tile(hdc, dither, canvas_x, canvas_y,
                  offset_x, offset_y, width, height, fog);
      if (fog)
	draw_fog_part(hdc,canvas_x+offset_x,canvas_y+offset_y,
		      width,height,offset_x,offset_y);
    }
  }
  
  /*** Rest of terrain and specials ***/
  for (; i<count; i++) {
    if (tile_sprs[i])
      pixmap_put_overlay_tile_draw(hdc, canvas_x, canvas_y, tile_sprs[i],
                                   offset_x, offset_y, width, height, fog);
    else
      freelog(LOG_ERROR, "sprite is NULL");
  }
  /*** Map grid ***/
  if (draw_map_grid) {
    HPEN old;
    /* we draw the 2 lines on top of the tile; the buttom lines will be
       drawn by the tiles underneath. */
    old=SelectObject(hdc,pen_std[COLOR_STD_BLACK]);
    if (draw & D_M_R)
      {
	MoveToEx(hdc,canvas_x+NORMAL_TILE_WIDTH/2,canvas_y,NULL);
	LineTo(hdc,canvas_x+NORMAL_TILE_WIDTH,
	       canvas_y+NORMAL_TILE_HEIGHT/2);
      }
    if (draw & D_M_L)
      {
	MoveToEx(hdc,canvas_x,canvas_y+NORMAL_TILE_HEIGHT/2,NULL);
	LineTo(hdc,canvas_x+NORMAL_TILE_WIDTH/2,canvas_y);
      }
    SelectObject(hdc,old);
  }
  if (draw_coastline && !draw_terrain) {
    enum tile_terrain_type t1 = map_get_terrain(x, y), t2;
    int x1, y1;
    HPEN old;
    old=SelectObject(hdc,pen_std[COLOR_STD_OCEAN]);
    x1=x;
    y1=y-1;
    if (normalize_map_pos(&x1,&y1)) { 
      t2=map_get_terrain(x1,y1);
      if (draw & D_M_R && ((t1==T_OCEAN) ^ (t2==T_OCEAN))) {
	MoveToEx(hdc,canvas_x+NORMAL_TILE_WIDTH/2,canvas_y,NULL);
	LineTo(hdc,canvas_x+NORMAL_TILE_WIDTH,
	       canvas_y+NORMAL_TILE_HEIGHT/2);
      }
    }
    x1=x-1; 
    y1=y;
    if (normalize_map_pos(&x1, &y1)) {
      t2 = map_get_terrain(x1, y1);
      if (draw & D_M_L && ((t1 == T_OCEAN) ^ (t2 == T_OCEAN))){
	MoveToEx(hdc,canvas_x,canvas_y+NORMAL_TILE_HEIGHT/2,NULL);
	LineTo(hdc,canvas_x+NORMAL_TILE_WIDTH/2,canvas_y); 
      }
    }
  }
  
  /*** City and various terrain improvements ***/
  if (pcity && draw_cities) {
    put_city_pixmap_draw(pcity, hdc,
			 canvas_x, canvas_y - NORMAL_TILE_HEIGHT/2,
                         offset_x, offset_y_unit,
                         width, height_unit, fog);
  }
  
  if (special & S_AIRBASE && draw_fortress_airbase)
    pixmap_put_overlay_tile_draw(hdc,
                                 canvas_x, canvas_y-NORMAL_TILE_HEIGHT/2,
                                 sprites.tx.airbase,
                                 offset_x, offset_y_unit,
                                 width, height_unit, fog);
  if (special & S_FALLOUT && draw_pollution)
    pixmap_put_overlay_tile_draw(hdc,
                                 canvas_x, canvas_y,
                                 sprites.tx.fallout,
                                 offset_x, offset_y,
                                 width, height, fog);
  if (special & S_POLLUTION && draw_pollution)
    pixmap_put_overlay_tile_draw(hdc,
                                 canvas_x, canvas_y,
                                 sprites.tx.pollution,
                                 offset_x, offset_y,
                                 width, height, fog);
  
  /*** city size ***/
  /* Not fogged as it would be unreadable */
  if (pcity && draw_cities) {
    if (pcity->size>=10)
      pixmap_put_overlay_tile_draw(hdc, 
				   canvas_x, canvas_y-NORMAL_TILE_HEIGHT/2,
                                   sprites.city.size_tens[pcity->size/10],
                                   offset_x, offset_y_unit,
				   width, height_unit, 0);

    pixmap_put_overlay_tile_draw(hdc, canvas_x, canvas_y-NORMAL_TILE_HEIGHT/2,
				 sprites.city.size[pcity->size%10],
                                 offset_x, offset_y_unit,
                                 width, height_unit, 0);  
  }

    /*** Unit ***/
  if (punit && (draw_units || (punit == pfocus && draw_focus_unit))) {
    put_unit_pixmap_draw(punit, hdc,
                         canvas_x, canvas_y - NORMAL_TILE_HEIGHT/2,
                         offset_x, offset_y_unit,
                         width, height_unit);
    if (!pcity && unit_list_size(&map_get_tile(x, y)->units) > 1)
      pixmap_put_overlay_tile_draw(hdc,
                                   canvas_x, canvas_y-NORMAL_TILE_HEIGHT/2,
                                   sprites.unit.stack,
                                   offset_x, offset_y_unit,
                                   width, height_unit, fog);
  }
  
  if (special & S_FORTRESS && draw_fortress_airbase)
    pixmap_put_overlay_tile_draw(hdc,
                                 canvas_x, canvas_y-NORMAL_TILE_HEIGHT/2,
                                 sprites.tx.fortress,
                                 offset_x, offset_y_unit,
                                 width, height_unit, fog);
  
}



/**************************************************************************
Not used in isometric view.
**************************************************************************/
static void put_line(HDC hdc, int x, int y, 
		     int dir,int write_to_screen)
{
  HPEN old;
  int canvas_src_x, canvas_src_y, canvas_dest_x, canvas_dest_y;
  get_canvas_xy(x, y, &canvas_src_x, &canvas_src_y);
  canvas_src_x += NORMAL_TILE_WIDTH/2;
  canvas_src_y += NORMAL_TILE_HEIGHT/2;
  DIRSTEP(canvas_dest_x, canvas_dest_y, dir);
  canvas_dest_x = canvas_src_x + (NORMAL_TILE_WIDTH * canvas_dest_x) / 2;
  canvas_dest_y = canvas_src_y + (NORMAL_TILE_WIDTH * canvas_dest_y) / 2;
 
  old=SelectObject(hdc,pen_std[COLOR_STD_CYAN]);
  MoveToEx(hdc,canvas_src_x+(write_to_screen?map_win_x:0),
	   canvas_src_y+(write_to_screen?map_win_y:0),
	   NULL);
  LineTo(hdc,canvas_dest_x+(write_to_screen?map_win_x:0),
	   canvas_dest_y+(write_to_screen?map_win_y:0));
  SelectObject(hdc,old);
}


/**************************************************************************
...
**************************************************************************/
void draw_segment(int src_x, int src_y, int dir)
{
  HDC hdc;
  if (is_isometric) {
    increment_drawn(src_x, src_y, dir);
    if (get_drawn(src_x, src_y, dir) > 1) {
      return;
    } else {
      really_draw_segment(src_x, src_y, dir, 1, 0);
    }
  } else {
    int dest_x, dest_y, is_real;

    is_real = MAPSTEP(dest_x, dest_y, src_x, src_y, dir);
    assert(is_real);

    /* A previous line already marks the place */
    if (get_drawn(src_x, src_y, dir)) {
      increment_drawn(src_x, src_y, dir);
      return;
    }
    
    hdc=GetDC(root_window);
    if (tile_visible_mapcanvas(src_x, src_y)) {
      put_line(mapstoredc, src_x, src_y, dir,0);
      put_line(hdc, src_x, src_y, dir,1);
    }
    if (tile_visible_mapcanvas(dest_x, dest_y)) {
      put_line(mapstoredc, dest_x, dest_y, DIR_REVERSE(dir),0);
      put_line(hdc, dest_x, dest_y, DIR_REVERSE(dir),1);
    }
    ReleaseDC(root_window,hdc);
    increment_drawn(src_x, src_y, dir);
  }
}


/**************************************************************************
remove the line from src_x,src_y -> dest_x,dest_y on both map_canvas and
map_canvas_store.
**************************************************************************/
void undraw_segment(int src_x, int src_y, int dir)
{
  int dest_x, dest_y, is_real;

  is_real = MAPSTEP(dest_x, dest_y, src_x, src_y, dir);
  assert(is_real);

  if (is_isometric) {
    assert(get_drawn(src_x, src_y, dir));
    decrement_drawn(src_x, src_y, dir);

    /* somewhat inefficient */
    if (!get_drawn(src_x, src_y, dir)) {
      update_map_canvas(MIN(src_x, dest_x), MIN(src_y, dest_y),
                        src_x == dest_x ? 1 : 2,
                        src_y == dest_y ? 1 : 2,
                        1);
    }
  } else {
    int drawn = get_drawn(src_x, src_y, dir);

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
      if (dir == DIR8_NORTHEAST) { /* Since the tle doesn't have a middle we draw an extra pi
xel
                         on the adjacent tile when drawing in this direction. */
        dest_x = src_x + 1;
        dest_y = src_y;
        is_real = normalize_map_pos(&dest_x, &dest_y);
	assert(is_real);
        refresh_tile_mapcanvas(dest_x, dest_y, 1);
      } else if (dir == DIR8_SOUTHWEST) { /* the same */
        dest_x = src_x;
        dest_y = src_y + 1;
        is_real = normalize_map_pos(&dest_x, &dest_y);
	assert(is_real);
        refresh_tile_mapcanvas(dest_x, dest_y, 1);
      }
    }
  }
}
