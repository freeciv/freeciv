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

#include <windows.h>
#include <windowsx.h>

#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "government.h"         /* government_graphic() */
#include "map.h"
#include "mem.h"
#include "player.h"
#include "rand.h"
#include "support.h"
#include "timing.h"
#include "version.h"

#include "citydlg.h" 
#include "civclient.h"
#include "climap.h"
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

static struct Sprite *indicator_sprite[3];

HDC overviewstoredc;
HBITMAP overviewstorebitmap;
HBITMAP mapstorebitmap;

static HBITMAP intro_gfx;

static HBITMAP single_tile_pixmap;

extern HBITMAP BITMAP2HBITMAP(BITMAP *bmp);

extern void do_mainwin_layout();

extern int seconds_to_turndone;   
void update_map_canvas_scrollbars_size(void);
void refresh_overview_viewrect_real(HDC hdcp);
void put_one_tile_full(HDC hdc, int x, int y,
			      int canvas_x, int canvas_y, int citymode);
static void pixmap_put_tile_iso(HDC hdc, int x, int y,
                                int canvas_x, int canvas_y,
                                int citymode,
                                int offset_x, int offset_y, int offset_y_unit,
                                int width, int height, int height_unit,
                                enum draw_type draw);
static void really_draw_segment(HDC mapstoredc,int src_x, int src_y, int dir,
				bool write_to_screen, bool force);
static void dither_tile(HDC hdc, struct Sprite **dither,
                        int canvas_x, int canvas_y,
                        int offset_x, int offset_y,
                        int width, int height, int fog);
static void put_line(HDC hdc, int x, int y, 
		     int dir, bool write_to_screen);
static void draw_rates(HDC hdc);


/**************************************************************************

**************************************************************************/
void map_resize()
{
  HBITMAP newbmp;
  HDC hdc;
  hdc=GetDC(map_window);
  newbmp=CreateCompatibleBitmap(hdc,map_win_width,map_win_height);
  if (mapstorebitmap) DeleteObject(mapstorebitmap);
  mapstorebitmap=newbmp;
  ReleaseDC(map_window,hdc);

  if (!mapview_canvas.store) {
    mapview_canvas.store = fc_malloc(sizeof(*mapview_canvas.store));
  }
  mapview_canvas.store->hdc = NULL;
  mapview_canvas.store->bitmap = mapstorebitmap;
  
  map_view_width=(map_win_width+NORMAL_TILE_WIDTH-1)/NORMAL_TILE_WIDTH;
  map_view_height=(map_win_height+NORMAL_TILE_HEIGHT-1)/NORMAL_TILE_HEIGHT; 
  update_map_canvas_scrollbars_size();
  if (can_client_change_view() && map_exists()) {
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
  if (!can_client_change_view()) {
    if (!intro_gfx_sprite) {
      load_intro_gfx();
    }
    if (!intro_gfx) {
      intro_gfx = BITMAP2HBITMAP(&intro_gfx_sprite->bmp);
    }
    introgfxdc = CreateCompatibleDC(hdc);
    bmsave = SelectObject(introgfxdc, intro_gfx);
    StretchBlt(hdc, 0, 0, map_win_width, map_win_height,
	       introgfxdc, 0, 0,
	       intro_gfx_sprite->width,
	       intro_gfx_sprite->height,
	       SRCCOPY);
    SelectObject(introgfxdc, bmsave);
    DeleteDC(introgfxdc);
  } else {
    HBITMAP old;
    HDC mapstoredc;
    mapstoredc = CreateCompatibleDC(NULL);
    old = SelectObject(mapstoredc, mapstorebitmap);
    BitBlt(hdc, 0, 0, map_win_width, map_win_height,
	   mapstoredc, 0, 0, SRCCOPY);
    SelectObject(mapstoredc, old);
    DeleteDC(mapstoredc);
  }
} 

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
void pixmap_frame_tile_red(HDC hdc, int canvas_x, int canvas_y)
{
  HPEN old;
  old=SelectObject(hdc,pen_std[COLOR_STD_RED]);
  if (is_isometric) {
    MoveToEx(hdc, canvas_x+NORMAL_TILE_WIDTH/2-1, canvas_y, NULL);
    LineTo(hdc, canvas_x+NORMAL_TILE_WIDTH-1, canvas_y+NORMAL_TILE_HEIGHT/2-1);
    LineTo(hdc, canvas_x+NORMAL_TILE_WIDTH/2-1,
	   canvas_y+NORMAL_TILE_HEIGHT-1);
    LineTo(hdc, canvas_x, canvas_y+NORMAL_TILE_HEIGHT/2-1);
    LineTo(hdc, canvas_x+NORMAL_TILE_WIDTH/2-1, canvas_y);
  } else {
    mydrawrect(hdc,canvas_x,canvas_y,
	       NORMAL_TILE_WIDTH-1,NORMAL_TILE_HEIGHT-1);
  }  
  SelectObject(hdc,old);
}

/**************************************************************************
hack to ensure that mapstorebitmap is usable. 
On win95/win98 mapstorebitmap becomes somehow invalid. 
**************************************************************************/
void check_mapstore()
{
  static int n=0;
  HDC hdc;
  BITMAP bmp;
  if (GetObject(mapstorebitmap,sizeof(BITMAP),&bmp)==0) {
    DeleteObject(mapstorebitmap);
    hdc=GetDC(map_window);
    mapstorebitmap=CreateCompatibleBitmap(hdc,map_win_width,map_win_height);
    update_map_canvas_visible();
    n++;
    assert(n<5);
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
static void draw_rates(HDC hdc)
{
  int d;
  d=0;
  for(;d<(game.player_ptr->economic.luxury)/10;d++)
    draw_sprite(get_citizen_sprite(CITIZEN_ELVIS, d, NULL), hdc,
		SMALL_TILE_WIDTH*d,taxinfoline_y);/* elvis tile */
  for(;d<(game.player_ptr->economic.science+game.player_ptr->economic.luxury)/10;d++)
    draw_sprite(get_citizen_sprite(CITIZEN_SCIENTIST, d, NULL), hdc,
		SMALL_TILE_WIDTH*d,taxinfoline_y); /* scientist tile */    
  for(;d<10;d++)
    draw_sprite(get_citizen_sprite(CITIZEN_TAXMAN, d, NULL), hdc,
		SMALL_TILE_WIDTH*d,taxinfoline_y); /* taxman tile */  
}

/**************************************************************************

**************************************************************************/
void
update_info_label(void)
{
  char buffer2[512];
  char buffer[512];
  HDC hdc;
  my_snprintf(buffer, sizeof(buffer),
	      _("Population: %s\nYear: %s\nGold: %d\nTax: %d Lux: %d Sci: %d"),
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
  set_indicator_icons(client_research_sprite(),
		      client_warming_sprite(),
                      client_cooling_sprite(),
                      game.player_ptr->government);
  
  hdc=GetDC(root_window);
  draw_rates(hdc);
  ReleaseDC(root_window,hdc);
  update_timeout_label();    
  
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
  char buffer[512];
  
  if (game.timeout <= 0)
    sz_strlcpy(buffer, Q_("?timeout:off"));
  else
    format_duration(buffer, sizeof(buffer), seconds_to_turndone);
  SetWindowText(timeout_label,buffer);
}

/**************************************************************************

**************************************************************************/
void update_turn_done_button(bool do_restore)
{
  static bool flip = FALSE;

  if (!get_turn_done_button_state()) {
    return;
  }

  if (do_restore) {
    flip = FALSE;
    Button_SetState(turndone_button, 0);
  } else {
    Button_SetState(turndone_button, flip);
    flip = !flip;
  }
}

/**************************************************************************

**************************************************************************/
void
set_indicator_icons(int bulb, int sol, int flake, int gov)
{
  int i;
  HDC hdc;
  
  bulb = CLIP(0, bulb, NUM_TILES_PROGRESS-1);
  sol = CLIP(0, sol, NUM_TILES_PROGRESS-1);
  flake = CLIP(0, flake, NUM_TILES_PROGRESS-1);     
  indicator_sprite[0]=sprites.bulb[bulb];
  indicator_sprite[1]=sprites.warming[sol];
  indicator_sprite[2]=sprites.cooling[flake];
  if (game.government_count==0) {
    /* not sure what to do here */
    indicator_sprite[3] = get_citizen_sprite(CITIZEN_UNHAPPY, 0, NULL);
  } else {
    indicator_sprite[3] = get_government(gov)->sprite;    
  }
  hdc=GetDC(root_window);
  for(i=0;i<4;i++)
    draw_sprite(indicator_sprite[i],hdc,i*SMALL_TILE_WIDTH,indicator_y); 
  ReleaseDC(root_window,hdc);
}

/**************************************************************************

**************************************************************************/
void
set_overview_dimensions(int x, int y)
{
  HDC hdc;
  HBITMAP newbit;
  set_overview_win_dim(OVERVIEW_TILE_WIDTH * x,OVERVIEW_TILE_HEIGHT * y);
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
  int overview_x, overview_y;

  map_to_overview_pos(&overview_x, &overview_y, x, y);
 
  rc.left = OVERVIEW_TILE_WIDTH * x;
  rc.right = rc.left + OVERVIEW_TILE_WIDTH;
  rc.top = OVERVIEW_TILE_HEIGHT * y;
  rc.bottom = rc.top + OVERVIEW_TILE_HEIGHT;
  FillRect(overviewstoredc,&rc,brush_std[overview_tile_color(x, y)]);

  hdc=GetDC(root_window);
  rc.left = overview_x + overview_win_x;
  rc.top = overview_y + overview_win_y;
  rc.right = rc.left + OVERVIEW_TILE_WIDTH;
  rc.bottom = rc.top + OVERVIEW_TILE_HEIGHT;
  FillRect(hdc,&rc,brush_std[overview_tile_color(x,y)]);
  ReleaseDC(root_window,hdc);
}

/**************************************************************************
  Flush the given part of the canvas buffer (if there is one) to the
  screen.
**************************************************************************/
void flush_mapcanvas(int canvas_x, int canvas_y,
		     int pixel_width, int pixel_height)
{
  HDC hdcwin = GetDC(map_window);
  HDC mapstoredc = CreateCompatibleDC(NULL);
  HBITMAP old = SelectObject(mapstoredc, mapstorebitmap);
  BitBlt(hdcwin, canvas_x, canvas_y,
	 pixel_width, pixel_height,
	 mapstoredc,
	 canvas_x, canvas_y,
	 SRCCOPY);
  ReleaseDC(map_window, hdcwin);
  SelectObject(mapstoredc, old);
  DeleteDC(mapstoredc);
}

#define MAX_DIRTY_RECTS 20
static int num_dirty_rects = 0;
static struct {
  int x, y, w, h;
} dirty_rects[MAX_DIRTY_RECTS];
bool is_flush_queued = FALSE;

/**************************************************************************
  A callback invoked as a result of a timer event, this function simply
  flushes the mapview canvas.
**************************************************************************/
static VOID CALLBACK unqueue_flush(HWND hwnd, UINT uMsg, UINT idEvent,
				   DWORD dwTime)
{
  flush_dirty();
  is_flush_queued = FALSE;
}

/**************************************************************************
  Called when a region is marked dirty, this function queues a flush event
  to be handled later.  The flush may end up being done by freeciv before
  then, in which case it will be a wasted call.
**************************************************************************/
static void queue_flush(void)
{
  if (!is_flush_queued) {
    SetTimer(root_window, 4, 0, unqueue_flush);
    is_flush_queued = TRUE;
  }
}

/**************************************************************************
  Mark the rectangular region as 'dirty' so that we know to flush it
  later.
**************************************************************************/
void dirty_rect(int canvas_x, int canvas_y,
		int pixel_width, int pixel_height)
{
  if (num_dirty_rects < MAX_DIRTY_RECTS) {
    dirty_rects[num_dirty_rects].x = canvas_x;
    dirty_rects[num_dirty_rects].y = canvas_y;
    dirty_rects[num_dirty_rects].w = pixel_width;
    dirty_rects[num_dirty_rects].h = pixel_height;
    num_dirty_rects++;
    queue_flush();
  }
}

/**************************************************************************
  Mark the entire screen area as "dirty" so that we can flush it later.
**************************************************************************/
void dirty_all(void)
{
  num_dirty_rects = MAX_DIRTY_RECTS;
  queue_flush();
}

/**************************************************************************
  Flush all regions that have been previously marked as dirty.  See
  dirty_rect and dirty_all.  This function is generally called after we've
  processed a batch of drawing operations.
**************************************************************************/
void flush_dirty(void)
{
  if (num_dirty_rects == MAX_DIRTY_RECTS) {
    flush_mapcanvas(0, 0, map_win_width, map_win_height);
  } else {
    int i;

    for (i = 0; i < num_dirty_rects; i++) {
      flush_mapcanvas(dirty_rects[i].x, dirty_rects[i].y,
		      dirty_rects[i].w, dirty_rects[i].h);
    }
  }
  num_dirty_rects = 0;
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
  If necessary, clear the city descriptions out of the buffer.
**************************************************************************/
void prepare_show_city_descriptions(void)
{
  /* Nothing to do */
}

/**************************************************************************

**************************************************************************/
void show_city_desc(struct city *pcity, int canvas_x, int canvas_y)
{
  char buffer[500];
  int y_offset;
  HDC hdc;
  HBITMAP old;

  /* TODO: hdc should be stored statically */
  hdc = CreateCompatibleDC(NULL);
  old = SelectObject(hdc, mapstorebitmap);
  SetBkMode(hdc,TRANSPARENT);

  y_offset = canvas_y + NORMAL_TILE_HEIGHT;
  if (draw_city_names && pcity->name) {
    RECT rc;

    /* FIXME: draw city growth as well, using
     * get_city_mapview_name_and_growth() */

    DrawText(hdc, pcity->name, strlen(pcity->name), &rc, DT_CALCRECT);
    rc.left = canvas_x + NORMAL_TILE_WIDTH / 2 - 10;
    rc.right = rc.left + 20;
    rc.bottom -= rc.top;
    rc.top = y_offset;
    rc.bottom += rc.top;
    SetTextColor(hdc, RGB(0, 0, 0));
    DrawText(hdc, pcity->name, strlen(pcity->name), &rc,
	     DT_NOCLIP | DT_CENTER);
    rc.left++;
    rc.top--;
    rc.right++;
    rc.bottom--;
    SetTextColor(hdc, RGB(255, 255, 255));
    DrawText(hdc, pcity->name, strlen(pcity->name), &rc,
	     DT_NOCLIP | DT_CENTER);
    y_offset = rc.bottom + 2;
  }

  if (draw_city_productions && pcity->owner == game.player_idx) {
    RECT rc;

    get_city_mapview_production(pcity, buffer, sizeof(buffer));
      
    DrawText(hdc, buffer, strlen(buffer), &rc, DT_CALCRECT);
    rc.left = canvas_x + NORMAL_TILE_WIDTH / 2 - 10;
    rc.right = rc.left + 20;
    rc.bottom -= rc.top;
    rc.top = y_offset;
    rc.bottom += rc.top; 
    SetTextColor(hdc, RGB(0, 0, 0));
    DrawText(hdc, buffer, strlen(buffer), &rc, DT_NOCLIP | DT_CENTER);
    rc.left++;
    rc.top--;
    rc.right++;
    rc.bottom--;
    SetTextColor(hdc, RGB(255, 255, 255));
    DrawText(hdc, buffer, strlen(buffer), &rc, DT_NOCLIP | DT_CENTER);
  }

  SelectObject(hdc, old);
  DeleteDC(hdc);
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
    hdc=GetDC(map_window);
    draw_sprite(sprites.user.attention,hdc,canvas_x,canvas_y);
    ReleaseDC(map_window,hdc);
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
  Draw a single frame of animation.  This function needs to clear the old
  image and draw the new one.  It must flush output to the display.
**************************************************************************/
void draw_unit_animation_frame(struct unit *punit,
			       bool first_frame, bool last_frame,
			       int old_canvas_x, int old_canvas_y,
			       int new_canvas_x, int new_canvas_y)
{
  static HDC mapstoredc, hdc, hdcwin;
  static HBITMAP old, oldbmp;

  /* Create extra backing store.  This should be done statically. */
  if (first_frame) {
    mapstoredc = CreateCompatibleDC(NULL);
    old = SelectObject(mapstoredc, mapstorebitmap);
    hdc = CreateCompatibleDC(NULL);
    oldbmp = SelectObject(hdc, single_tile_pixmap);
    hdcwin = GetDC(map_window);
  }

  /* Clear old sprite. */
  BitBlt(hdcwin, old_canvas_x, old_canvas_y, UNIT_TILE_WIDTH,
	 UNIT_TILE_HEIGHT, mapstoredc, old_canvas_x, old_canvas_y, SRCCOPY);

  /* Draw the new sprite. */
  BitBlt(hdc, 0, 0, UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT, mapstoredc,
	 new_canvas_x, new_canvas_y, SRCCOPY);
  put_unit_pixmap(punit, hdc, 0, 0);

  /* Write to screen. */
  BitBlt(hdcwin, new_canvas_x, new_canvas_y, UNIT_TILE_WIDTH,
	 UNIT_TILE_HEIGHT, hdc, 0, 0, SRCCOPY);

  /* Flush. */
  GdiFlush();

  if (last_frame) {
    SelectObject(hdc, oldbmp);
    DeleteDC(hdc);
    ReleaseDC(map_window, hdcwin);
    SelectObject(mapstoredc, old);
    DeleteDC(mapstoredc);
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
  HDC mapstoredc;
  HDC hdc,hdcwin;
  HBITMAP oldbmp,old_mapbmp;
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
    
    refresh_tile_mapcanvas(punit0->x, punit0->y, TRUE);
    refresh_tile_mapcanvas(punit1->x, punit1->y, TRUE);
    GdiFlush();
    
    usleep_since_timer_start(anim_timer, 10000);
    
  } while (punit0->hp > hp0 || punit1->hp > hp1);

  mapstoredc=CreateCompatibleDC(NULL);
  old_mapbmp=SelectObject(mapstoredc,mapstorebitmap);
  hdc=CreateCompatibleDC(NULL);
  hdcwin=GetDC(map_window);
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
      BitBlt(hdcwin,canvas_x,canvas_y,
	     NORMAL_TILE_WIDTH,NORMAL_TILE_HEIGHT,
	     hdc,0,0,SRCCOPY);
    } else {
      struct canvas_store store = {NULL, single_tile_pixmap};

      put_one_tile(&store, losing_unit->x, losing_unit->y, 0, 0, FALSE);
      put_unit_pixmap(losing_unit, hdc, 0, 0);
      draw_sprite(sprites.explode.unit[i],hdc,NORMAL_TILE_WIDTH/4,0);
      BitBlt(hdcwin,canvas_x,canvas_y,
	     NORMAL_TILE_WIDTH,NORMAL_TILE_HEIGHT,
	     hdc,0,0,SRCCOPY);
    }
    GdiFlush();
    usleep_since_timer_start(anim_timer, 20000);
  }
  
  SelectObject(hdc,oldbmp);
  DeleteDC(hdc);
  ReleaseDC(map_window,hdcwin);
  SelectObject(mapstoredc,old_mapbmp);
  DeleteDC(mapstoredc);
  set_units_in_combat(NULL, NULL);
  refresh_tile_mapcanvas(punit0->x, punit0->y, TRUE);
  refresh_tile_mapcanvas(punit1->x, punit1->y, TRUE);
  
}

/**************************************************************************

**************************************************************************/
void
put_nuke_mushroom_pixmaps(int x, int y)
{
  
  HDC hdc;
  hdc=GetDC(map_window);
  if (is_isometric) {
    int canvas_x, canvas_y;
    struct Sprite *mysprite = sprites.explode.iso_nuke;  
    get_canvas_xy(x, y, &canvas_x, &canvas_y);
    canvas_x += NORMAL_TILE_WIDTH/2 - mysprite->width/2;
    canvas_y += NORMAL_TILE_HEIGHT/2 - mysprite->height/2;
    draw_sprite(mysprite,hdc,canvas_x,canvas_y);
    GdiFlush();
    ReleaseDC(map_window,hdc);
    Sleep(1000);
    update_map_canvas_visible();
  } else {
    int x_itr, y_itr;
    int canvas_x, canvas_y;
    
    for (y_itr=0; y_itr<3; y_itr++) {
      for (x_itr=0; x_itr<3; x_itr++) {
        struct Sprite *mysprite = sprites.explode.nuke[y_itr][x_itr];
        get_canvas_xy(x + x_itr - 1, y + y_itr - 1, &canvas_x, &canvas_y);
	draw_sprite(mysprite,hdc,canvas_x,canvas_y);
      }
    }
    GdiFlush();
    ReleaseDC(map_window,hdc);
    Sleep(1000); 
    update_map_canvas(x-1, y-1, 3, 3, TRUE);
  }
}

/**************************************************************************

**************************************************************************/
void
refresh_overview_canvas(void)
{
  HDC hdc=GetDC(root_window);
  int overview_x, overview_y;
  RECT rc;

  whole_map_iterate(x, y) {
    rc.left = OVERVIEW_TILE_WIDTH * x;
    rc.right = rc.left + OVERVIEW_TILE_WIDTH;
    rc.top = OVERVIEW_TILE_HEIGHT * y;
    rc.bottom = rc.top + OVERVIEW_TILE_HEIGHT;
    FillRect(overviewstoredc, &rc, brush_std[overview_tile_color(x, y)]);

    map_to_overview_pos(&overview_x, &overview_y, x, y);

    rc.left = overview_win_x + overview_x;
    rc.right = rc.left + OVERVIEW_TILE_WIDTH;
    rc.top = overview_win_y + overview_y;
    rc.bottom = rc.top + OVERVIEW_TILE_HEIGHT;
    FillRect(hdc, &rc, brush_std[overview_tile_color(x, y)]);
  } whole_map_iterate_end;

  ReleaseDC(root_window,hdc);
}

/**************************************************************************

**************************************************************************/
void
refresh_overview_viewrect_real(HDC hdcp)
{
  int map_overview_x0 = get_overview_x0();
  int x0 = OVERVIEW_TILE_WIDTH * map_overview_x0;
  int x1 = OVERVIEW_TILE_WIDTH * (map.xsize - map_overview_x0);
  int dy = OVERVIEW_TILE_HEIGHT * map.ysize;
  int gui_x, gui_y;
  HDC hdc = hdcp;
  HPEN oldpen;

  if (!hdc) {
    hdc = GetDC(root_window);
  }

  /* Copy the part of the overview to the right of map_overview_x0. */
  BitBlt(hdc, overview_win_x, overview_win_y, x1, dy, x0, 0);

  /* Copy the part of the overview to the left of map_overview_x0. */
  BitBlt(hdc, overview_win_x + x1, overview_win_y, x0, dy, 0, 0);

  /* Find the origin of the mapview, in overview (gui) coordinates. */
  map_to_overview_pos(&gui_x, &gui_y,
		      mapview_canvas.map_x0, mapview_canvas.map_y0);

  oldpen = SelectObject(hdc, pen_std[COLOR_STD_WHITE]);
  if (is_isometric) {
    int Wx = gui_x;
    int Wy = gui_y;
    int Nx = Wx + OVERVIEW_TILE_WIDTH * map_view_width;
    int Ny = Wy - OVERVIEW_TILE_HEIGHT * map_view_width;
    int Sx = Wx + OVERVIEW_TILE_WIDTH * map_view_height;
    int Sy = Wy + OVERVIEW_TILE_HEIGHT * map_view_height;
    int Ex = Nx + OVERVIEW_TILE_WIDTH * map_view_height;
    int Ey = Ny + OVERVIEW_TILE_HEIGHT * map_view_height;
    
    freelog(LOG_DEBUG, "wx,wy: %d,%d nx,ny:%d,%x ex,ey:%d,%d, sx,sy:%d,%d",
            Wx, Wy, Nx, Ny, Ex, Ey, Sx, Sy);
    MoveToEx(hdc,Wx+overview_win_x,Wy+overview_win_y,NULL);
    LineTo(hdc,Nx+overview_win_x,Ny+overview_win_y);
    LineTo(hdc,Ex+overview_win_x,Ey+overview_win_y);
    LineTo(hdc,Sx+overview_win_x,Sy+overview_win_y);
    LineTo(hdc,Wx+overview_win_x,Wy+overview_win_y);
  } else {
    mydrawrect(hdc, overview_win_x + gui_x, overview_win_y + gui_y,
	       OVERVIEW_TILE_WIDTH * map_view_width,
	       OVERVIEW_TILE_HEIGHT * map_view_height);
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
  HDC hdctest;
  HBITMAP old;
  HBITMAP bmp;
  int i;
  if (!can_client_change_view()) {
      if (!radar_gfx_sprite) {
	load_intro_gfx();
      }
      if (radar_gfx_sprite) {
	char s[64];
	int h;
	RECT rc;
	draw_sprite(radar_gfx_sprite,hdc,overview_win_x,overview_win_y);
	SetBkMode(hdc,TRANSPARENT);
	my_snprintf(s, sizeof(s), "%d.%d.%d%s",
		    MAJOR_VERSION, MINOR_VERSION,
		    PATCH_VERSION, VERSION_LABEL);
	DrawText(hdc, word_version(), strlen(word_version()), &rc, DT_CALCRECT);
	h=rc.bottom-rc.top;
	rc.left = overview_win_x;
	rc.right = overview_win_y + overview_win_width;
	rc.bottom = overview_win_y + overview_win_height - h - 2; 
	rc.top = rc.bottom - h;
	SetTextColor(hdc, RGB(0,0,0));
	DrawText(hdc, word_version(), strlen(word_version()), &rc, DT_CENTER);
	rc.top+=h;
	rc.bottom+=h;
	DrawText(hdc, s, strlen(s), &rc, DT_CENTER);
	rc.left++;
	rc.right++;
	rc.top--;
	rc.bottom--;
	SetTextColor(hdc, RGB(255,255,255));
	DrawText(hdc, s, strlen(s), &rc, DT_CENTER);
	rc.top-=h;
	rc.bottom-=h;
	DrawText(hdc, word_version(), strlen(word_version()), &rc, DT_CENTER);
      }
    }
  else
    {
      hdctest=CreateCompatibleDC(NULL);
      old=NULL;
      bmp=NULL;
      for(i=0;i<4;i++)
	if (indicator_sprite[i]) {
	  bmp=BITMAP2HBITMAP(&indicator_sprite[i]->bmp);
	  if (!old)
	    old=SelectObject(hdctest,bmp);
	  else
	    DeleteObject(SelectObject(hdctest,bmp));
	  BitBlt(hdc,i*SMALL_TILE_WIDTH,indicator_y,
		 SMALL_TILE_WIDTH,SMALL_TILE_HEIGHT,
		 hdctest,0,0,SRCCOPY);
	}
      SelectObject(hdctest,old);
      if (bmp)
	DeleteObject(bmp);
      DeleteDC(hdctest);
      draw_rates(hdc);
      refresh_overview_viewrect_real(hdc);
    }
}

/**************************************************************************

**************************************************************************/
void map_handle_hscroll(int pos)
{
  if (!can_client_change_view()) {
    return;
  }
  map_view_x=pos;
  update_map_canvas_visible();
  refresh_overview_viewrect();                                                
}

/**************************************************************************

**************************************************************************/
void map_handle_vscroll(int pos)
{
  if (!can_client_change_view()) {
    return;
  }
  map_view_y=pos;
  map_view_y=(map_view_y<0)?0:map_view_y;
  map_view_y= (map_view_y>map.ysize+EXTRA_BOTTOM_ROW-map_view_height) ?
        map.ysize+EXTRA_BOTTOM_ROW-map_view_height :
        map_view_y;
  update_map_canvas_visible();
  refresh_overview_viewrect();
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
static void really_draw_segment(HDC mapstoredc,int src_x, int src_y, int dir,
                                bool write_to_screen, bool force)
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
    hdc=GetDC(map_window);
    
    old=SelectObject(hdc, pen_std[COLOR_STD_CYAN]);
    
    MoveToEx(hdc,canvas_start_x,canvas_start_y,NULL);
    LineTo(hdc,canvas_end_x,canvas_end_y);
    SelectObject(hdc,old);
    ReleaseDC(map_window,hdc);
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
  Draw some or all of a tile onto the mapview canvas.
**************************************************************************/
void gui_map_put_tile_iso(int map_x, int map_y,
			  int canvas_x, int canvas_y,
			  int offset_x, int offset_y, int offset_y_unit,
			  int width, int height, int height_unit,
			  enum draw_type draw)
{
  HDC hdc;
  HBITMAP old;

  /* FIXME: we don't want to have to recreate the hdc each time! */
  hdc = CreateCompatibleDC(NULL);
  old = SelectObject(hdc, mapstorebitmap);

  pixmap_put_tile_iso(hdc, map_x, map_y, canvas_x, canvas_y, 0,
		      offset_x, offset_y, offset_y_unit,
		      width, height, height_unit,
		      draw);

  SelectObject(hdc, old);
  DeleteDC(hdc);
}

/**************************************************************************
  Draw some or all of a sprite onto the mapview or citydialog canvas.
**************************************************************************/
void gui_put_sprite(struct canvas_store *pcanvas_store,
		    int canvas_x, int canvas_y,
		    struct Sprite *sprite,
		    int offset_x, int offset_y, int width, int height)
{
  HDC hdc;
  HBITMAP old;

  /* FIXME: we don't want to have to recreate the hdc each time! */
  hdc = CreateCompatibleDC(pcanvas_store->hdc);
  old = SelectObject(hdc, pcanvas_store->bitmap);

  pixmap_put_overlay_tile_draw(hdc, canvas_x, canvas_y,
			       sprite, offset_x, offset_y,
			       width, height, 0);

  SelectObject(hdc, old);
  DeleteDC(hdc);
}

/**************************************************************************
  Draw a full sprite onto the mapview or citydialog canvas.
**************************************************************************/
void gui_put_sprite_full(struct canvas_store *pcanvas_store,
			 int canvas_x, int canvas_y,
			 struct Sprite *sprite)
{
  gui_put_sprite(pcanvas_store, canvas_x, canvas_y, sprite,
		 0, 0, sprite->width, sprite->height);
}

/**************************************************************************
  Draw a filled-in colored rectangle onto the mapview or citydialog canvas.
**************************************************************************/
void gui_put_rectangle(struct canvas_store *pcanvas_store,
		       enum color_std color,
		       int canvas_x, int canvas_y, int width, int height)
{
  HDC hdc = CreateCompatibleDC(pcanvas_store->hdc);
  HBRUSH old = SelectObject(hdc, brush_std[color]);

  mydrawrect(hdc, canvas_x, canvas_y, width, height);

  SelectObject(hdc, old);
  DeleteDC(hdc);
}

/**************************************************************************
  Draw a 1-pixel-width colored line onto the mapview or citydialog canvas.
**************************************************************************/
void gui_put_line(struct canvas_store *pcanvas_store, enum color_std color,
		  int start_x, int start_y, int dx, int dy)
{
  HDC hdc = CreateCompatibleDC(pcanvas_store->hdc);
  HPEN old = SelectObject(hdc, pen_std[color]);

  MoveToEx(hdc, start_x, start_y, NULL);
  LineTo(hdc, start_x + dx, start_y + dy);

  SelectObject(hdc, old);
  DeleteDC(hdc);
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
    if (is_ocean(map_get_terrain(x, y))) { /* coasts */
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
      if (draw & D_M_R && (is_ocean(t1) ^ is_ocean(t2))) {
	MoveToEx(hdc,canvas_x+NORMAL_TILE_WIDTH/2,canvas_y,NULL);
	LineTo(hdc,canvas_x+NORMAL_TILE_WIDTH,
	       canvas_y+NORMAL_TILE_HEIGHT/2);
      }
    }
    x1=x-1; 
    y1=y;
    if (normalize_map_pos(&x1, &y1)) {
      t2 = map_get_terrain(x1, y1);
      if (draw & D_M_L && (is_ocean(t1) ^ is_ocean(t2))) {
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
  
  if (contains_special(special, S_AIRBASE) && draw_fortress_airbase)
    pixmap_put_overlay_tile_draw(hdc,
                                 canvas_x, canvas_y-NORMAL_TILE_HEIGHT/2,
                                 sprites.tx.airbase,
                                 offset_x, offset_y_unit,
                                 width, height_unit, fog);
  if (contains_special(special, S_FALLOUT) && draw_pollution)
    pixmap_put_overlay_tile_draw(hdc,
                                 canvas_x, canvas_y,
                                 sprites.tx.fallout,
                                 offset_x, offset_y,
                                 width, height, fog);
  if (contains_special(special, S_POLLUTION) && draw_pollution)
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
  
  if (contains_special(special, S_FORTRESS) && draw_fortress_airbase)
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
		     int dir, bool write_to_screen)
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
  MoveToEx(hdc,canvas_src_x,
	   canvas_src_y,
	   NULL);
  LineTo(hdc,canvas_dest_x,
	 canvas_dest_y);
  SelectObject(hdc,old);
}


/**************************************************************************
...
**************************************************************************/
void draw_segment(int src_x, int src_y, int dir)
{
  HDC hdc;

  assert(get_drawn(src_x, src_y, dir) > 0);

  if (is_isometric) {
    HDC mapstoredc = CreateCompatibleDC(NULL);
    HBITMAP old = SelectObject(mapstoredc, mapstorebitmap);

    really_draw_segment(mapstoredc, src_x, src_y, dir, TRUE, FALSE);
    SelectObject(mapstoredc, old);
    DeleteDC(mapstoredc);
  } else {
    int dest_x, dest_y, is_real;
    HBITMAP old;
    HDC mapstoredc;
    is_real = MAPSTEP(dest_x, dest_y, src_x, src_y, dir);
    assert(is_real);

    mapstoredc=CreateCompatibleDC(NULL);
    old=SelectObject(mapstoredc,mapstorebitmap);
    hdc=GetDC(map_window);
    if (tile_visible_mapcanvas(src_x, src_y)) {
      put_line(mapstoredc, src_x, src_y, dir,0);
      put_line(hdc, src_x, src_y, dir,1);
    }
    if (tile_visible_mapcanvas(dest_x, dest_y)) {
      put_line(mapstoredc, dest_x, dest_y, DIR_REVERSE(dir),0);
      put_line(hdc, dest_x, dest_y, DIR_REVERSE(dir),1);
    }
    ReleaseDC(map_window,hdc);
    SelectObject(mapstoredc,old);
    DeleteDC(mapstoredc);
  }
}

/**************************************************************************
  This function is called when the tileset is changed.
**************************************************************************/
void tileset_changed(void)
{
  /* PORTME */
  /* Here you should do any necessary redraws (for instance, the city
   * dialogs usually need to be resized).
   */
  
  indicator_sprite[0] = NULL;
  indicator_sprite[1] = NULL;
  indicator_sprite[2] = NULL;
  citydlg_tileset_change();
}
