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
#include "control.h" /* get_unit_in_focus() */
#include "graphics.h"
#include "gui_stuff.h"
#include "mapctrl.h"
#include "options.h"
#include "tilespec.h"      
#include "goto.h"
#include "gui_main.h"
#include "mapview.h"
#include "text.h"

extern HCURSOR goto_cursor;
extern HCURSOR drop_cursor;
extern HCURSOR nuke_cursor;
extern HCURSOR patrol_cursor;

static struct Sprite *indicator_sprite[3];

static HBITMAP intro_gfx;

extern HBITMAP BITMAP2HBITMAP(BITMAP *bmp);

extern void do_mainwin_layout();

extern int seconds_to_turndone;   
void update_map_canvas_scrollbars_size(void);
void refresh_overview_viewrect_real(HDC hdcp);
static void draw_rates(HDC hdc);

/***************************************************************************
 ...
***************************************************************************/
static HDC canvas_get_hdc(struct canvas *pcanvas)
{
  if (!pcanvas->hdc) {
    switch(pcanvas->type) {
      case CANVAS_BITMAP:
	pcanvas->hdc = CreateCompatibleDC(NULL);
	pcanvas->tmp = SelectObject(pcanvas->hdc, pcanvas->bmp);
	break;
      case CANVAS_WINDOW:
	pcanvas->hdc = GetDC(pcanvas->wnd);
	pcanvas->tmp = 1; /* non-null */
	break;
      default:
	break;
    }
  }

  return pcanvas->hdc;
}

/***************************************************************************
 ...
***************************************************************************/
static void canvas_release_hdc(struct canvas *pcanvas)
{
  if (pcanvas->tmp) {
    switch(pcanvas->type) {
      case CANVAS_BITMAP:
	SelectObject(pcanvas->hdc, pcanvas->tmp);
	DeleteDC(pcanvas->hdc);
	break;
      case CANVAS_WINDOW:
	ReleaseDC(pcanvas->wnd, pcanvas->hdc);
	break;
      default:
	break;
    }
    pcanvas->hdc = NULL;
    pcanvas->tmp = NULL;
  }
}

/***************************************************************************
 ...
***************************************************************************/
struct canvas *canvas_create(int width, int height)
{
  struct canvas *result = fc_malloc(sizeof(*result));
  HDC hdc;
  hdc = GetDC(root_window);
  result->type = CANVAS_BITMAP;
  result->hdc = NULL;
  result->bmp = CreateCompatibleBitmap(hdc, width, height);
  result->wnd = NULL;
  result->tmp = NULL;
  ReleaseDC(root_window, hdc);
  return result;
}

/***************************************************************************
  ...
***************************************************************************/
void canvas_free(struct canvas *store)
{
  DeleteObject(store->bmp);
  free(store);
}

static struct canvas overview_canvas;
static struct canvas real_mapview_canvas; /* conflict with mapview_canvas */

/****************************************************************************
  Return a canvas that is the overview window.
****************************************************************************/
struct canvas *get_overview_window(void)
{
  overview_canvas.type = CANVAS_WINDOW;
  overview_canvas.hdc = NULL;
  overview_canvas.bmp = NULL;
  overview_canvas.wnd = root_window;
  overview_canvas.tmp = NULL;
  return &overview_canvas;
}

/****************************************************************************
  Return a canvas that is the mapview window.
****************************************************************************/
struct canvas *get_mapview_window(void)
{
  real_mapview_canvas.type = CANVAS_WINDOW;
  real_mapview_canvas.hdc = NULL;
  real_mapview_canvas.bmp = NULL;
  real_mapview_canvas.wnd = map_window;
  real_mapview_canvas.tmp = NULL;
  return &real_mapview_canvas;
}

/***************************************************************************
   ...
***************************************************************************/
void canvas_copy(struct canvas *dst, struct canvas *src,
		 int src_x, int src_y, int dst_x, int dst_y,
		 int width, int height)
{
  HDC hdcsrc = canvas_get_hdc(src);
  HDC hdcdst = canvas_get_hdc(dst);

  BitBlt(hdcdst, dst_x, dst_y, width, height, hdcsrc, src_x, src_y, SRCCOPY);

  canvas_release_hdc(src);
  canvas_release_hdc(dst);
}

/**************************************************************************

**************************************************************************/
void map_expose(int x, int y, int width, int height)
{
  HBITMAP bmsave;
  HDC introgfxdc;
  HDC hdc;

  if (!can_client_change_view()) {
    if (!intro_gfx_sprite) {
      load_intro_gfx();
    }
    if (!intro_gfx) {
      intro_gfx = BITMAP2HBITMAP(&intro_gfx_sprite->img);
    }
    hdc = GetDC(map_window);
    introgfxdc = CreateCompatibleDC(hdc);
    bmsave = SelectObject(introgfxdc, intro_gfx);
    StretchBlt(hdc, 0, 0, map_win_width, map_win_height,
	       introgfxdc, 0, 0,
	       intro_gfx_sprite->width,
	       intro_gfx_sprite->height,
	       SRCCOPY);
    SelectObject(introgfxdc, bmsave);
    DeleteDC(introgfxdc);
    ReleaseDC(map_window, hdc);
  } else {
    canvas_copy(get_mapview_window(), mapview_canvas.store, x, y, x, y,
		width, height);
  }
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
static void draw_rates(HDC hdc)
{
  int d;
  d=0;
  for(;d<(game.player_ptr->economic.luxury)/10;d++)
    draw_sprite(sprites.tax_luxury, hdc,
		SMALL_TILE_WIDTH*d,taxinfoline_y);/* elvis tile */
  for(;d<(game.player_ptr->economic.science+game.player_ptr->economic.luxury)/10;d++)
    draw_sprite(sprites.tax_science, hdc,
		SMALL_TILE_WIDTH*d,taxinfoline_y); /* scientist tile */    
  for(;d<10;d++)
    draw_sprite(sprites.tax_gold, hdc,
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
  SetWindowText(unit_info_frame, get_unit_info_label_text1(punit));
  SetWindowText(unit_info_label, get_unit_info_label_text2(punit));

  if(punit) {
    if (hover_unit != punit->id)
      set_hover_state(NULL, HOVER_NONE, ACTIVITY_LAST, ORDER_LAST);
    switch (hover_state) {
      case HOVER_NONE:
	SetCursor (LoadCursor(NULL, IDC_ARROW));
	break;
      case HOVER_PATROL:
	SetCursor (patrol_cursor);
	break;
      case HOVER_GOTO:
      case HOVER_CONNECT:
	SetCursor (goto_cursor);
	break;
      case HOVER_NUKE:
	SetCursor (nuke_cursor);
	break;
      case HOVER_PARADROP:
	SetCursor (drop_cursor);
	break;
    }
  } else {
    SetCursor (LoadCursor(NULL, IDC_ARROW));
  }

    do_mainwin_layout();
}

/**************************************************************************

**************************************************************************/
void update_timeout_label(void)
{
  SetWindowText(timeout_label, get_timeout_label_text());
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
    /* HACK: the UNHAPPY citizen is used for the government
     * when we don't know any better. */
    struct citizen_type c = {.type = CITIZEN_UNHAPPY};

    indicator_sprite[3] = get_citizen_sprite(c, 0, NULL);
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
map_size_changed(void)
{
  set_overview_win_dim(OVERVIEW_TILE_WIDTH * map.xsize,OVERVIEW_TILE_HEIGHT * map.ysize);
}

/**************************************************************************
  Flush the given part of the canvas buffer (if there is one) to the
  screen.
**************************************************************************/
void flush_mapcanvas(int canvas_x, int canvas_y,
		     int pixel_width, int pixel_height)
{
  canvas_copy(get_mapview_window(), mapview_canvas.store, canvas_x, canvas_y,
	      canvas_x, canvas_y, pixel_width, pixel_height);
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

/****************************************************************************
  Do any necessary synchronization to make sure the screen is up-to-date.
  The canvas should have already been flushed to screen via flush_dirty -
  all this function does is make sure the hardware has caught up.
****************************************************************************/
void gui_flush(void)
{
  GdiFlush();
}

/**************************************************************************

**************************************************************************/
void update_map_canvas_scrollbars_size(void)
{
  int xmin, ymin, xmax, ymax, xsize, ysize;

  get_mapview_scroll_window(&xmin, &ymin, &xmax, &ymax, &xsize, &ysize);
  ScrollBar_SetRange(map_scroll_h, xmin, xmax, TRUE);
  ScrollBar_SetRange(map_scroll_v, ymin, ymax, TRUE);
}

/**************************************************************************

**************************************************************************/
void
update_map_canvas_scrollbars(void)
{
  int scroll_x, scroll_y;

  get_mapview_scroll_pos(&scroll_x, &scroll_y);
  ScrollBar_SetPos(map_scroll_h, scroll_x, TRUE);
  ScrollBar_SetPos(map_scroll_v, scroll_y, TRUE);
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
  char buffer[500];
  int y_offset;
  HDC hdc;

  hdc = canvas_get_hdc(pcanvas);
  SetBkMode(hdc,TRANSPARENT);

  *width = *height = 0;

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

    *width = rc.right - rc.left + 1;
    *height = rc.bottom - rc.top + 2;

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

    *width = MAX(*width, rc.right - rc.left + 1);
    *height += rc.bottom - rc.top + 1;
  }

  canvas_release_hdc(pcanvas);
}

/**************************************************************************

**************************************************************************/
void
put_cross_overlay_tile(struct tile *ptile)
{
  HDC hdc;
  int canvas_x, canvas_y;
  tile_to_canvas_pos(&canvas_x, &canvas_y, ptile);
  if (tile_visible_mapcanvas(ptile)) {
    hdc=GetDC(map_window);
    draw_sprite(sprites.user.attention,hdc,canvas_x,canvas_y);
    ReleaseDC(map_window,hdc);
  }
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
	  bmp=BITMAP2HBITMAP(&indicator_sprite[i]->img);
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
      refresh_overview_canvas();
    }
}

/**************************************************************************

**************************************************************************/
void map_handle_hscroll(int pos)
{
  int scroll_x, scroll_y;

  if (!can_client_change_view()) {
    return;
  }

  get_mapview_scroll_pos(&scroll_x, &scroll_y);
  set_mapview_scroll_pos(pos, scroll_y);
}

/**************************************************************************

**************************************************************************/
void map_handle_vscroll(int pos)
{
  int scroll_x, scroll_y;

  if (!can_client_change_view()) {
    return;
  }

  get_mapview_scroll_pos(&scroll_x, &scroll_y);
  set_mapview_scroll_pos(scroll_x, pos);
}

/**************************************************************************
Only used for isometric view.
**************************************************************************/
static void pixmap_put_overlay_tile_draw(HDC hdc,
                                         int canvas_x, int canvas_y,
                                         struct Sprite *ssprite,
                                         bool fog)
{
  if (!ssprite)
    return;

  if (fog && better_fog && !ssprite->has_fog) {
    fog_sprite(ssprite);
    if (!ssprite->has_fog) {
      freelog(LOG_NORMAL,
	      _("Better fog will only work in truecolor.  Disabling it"));
      better_fog = FALSE;
    }
  }

  if (fog && better_fog) {
    draw_sprite_fog(ssprite, hdc, canvas_x, canvas_y);
    return;
  }

  draw_sprite(ssprite, hdc, canvas_x, canvas_y);

  if (fog) {
    draw_fog(ssprite, hdc, canvas_x, canvas_y);
  }
  
}

/**************************************************************************
  Draw some or all of a sprite onto the mapview or citydialog canvas.
**************************************************************************/
void canvas_put_sprite(struct canvas *pcanvas,
		       int canvas_x, int canvas_y,
		       struct Sprite *sprite,
		       int offset_x, int offset_y, int width, int height)
{
  HDC hdc = canvas_get_hdc(pcanvas);

  draw_sprite(sprite, hdc, canvas_x, canvas_y);

  canvas_release_hdc(pcanvas);
}

/**************************************************************************
  Draw a full sprite onto the mapview or citydialog canvas.
**************************************************************************/
void canvas_put_sprite_full(struct canvas *pcanvas,
			    int canvas_x, int canvas_y,
			    struct Sprite *sprite)
{
  canvas_put_sprite(pcanvas, canvas_x, canvas_y, sprite,
		    0, 0, sprite->width, sprite->height);
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
  HDC hdc = canvas_get_hdc(pcanvas);

  pixmap_put_overlay_tile_draw(hdc, canvas_x, canvas_y,
			       psprite, fog);

  canvas_release_hdc(pcanvas);
}

/**************************************************************************
  Draw a filled-in colored rectangle onto the mapview or citydialog canvas.
**************************************************************************/
void canvas_put_rectangle(struct canvas *pcanvas,
			  enum color_std color,
			  int canvas_x, int canvas_y, int width, int height)
{
  HDC hdc = canvas_get_hdc(pcanvas);
  RECT rect;

  /* FillRect doesn't fill bottom and right edges, however canvas_x + width
   * and canvas_y + height are each 1 larger than necessary. */
  SetRect(&rect, canvas_x, canvas_y, canvas_x + width,
		 canvas_y + height);

  FillRect(hdc, &rect, brush_std[color]);

  canvas_release_hdc(pcanvas);
}

/****************************************************************************
  Fill the area covered by the sprite with the given color.
****************************************************************************/
void canvas_fill_sprite_area(struct canvas *pcanvas,
			     struct Sprite *psprite, enum color_std color,
			     int canvas_x, int canvas_y)
{
  /* FIXME: this may be inefficient */
  HDC hdc = canvas_get_hdc(pcanvas);
  HPEN oldpen;
  HBRUSH oldbrush;
  POINT points[4];

  /* FIXME: give a real implementation of this function. */
  assert(psprite == sprites.black_tile);

  points[0].x = canvas_x + NORMAL_TILE_WIDTH / 2;
  points[0].y = canvas_y;
  points[1].x = canvas_x;
  points[1].y = canvas_y + NORMAL_TILE_HEIGHT / 2;
  points[2].x = canvas_x + NORMAL_TILE_WIDTH / 2;
  points[2].y = canvas_y + NORMAL_TILE_HEIGHT;
  points[3].x = canvas_x + NORMAL_TILE_WIDTH;
  points[3].y = canvas_y + NORMAL_TILE_HEIGHT / 2;
  oldpen = SelectObject(hdc, pen_std[color]); 
  oldbrush = SelectObject(hdc, brush_std[color]);
  Polygon(hdc, points, 4);
  SelectObject(hdc, oldpen);
  SelectObject(hdc, oldbrush);

  canvas_release_hdc(pcanvas);
}

/****************************************************************************
  Fill the area covered by the sprite with the given color.
****************************************************************************/
void canvas_fog_sprite_area(struct canvas *pcanvas, struct Sprite *psprite,
			    int canvas_x, int canvas_y)
{
  /* PORTME */
}

/**************************************************************************
  Draw a 1-pixel-width colored line onto the mapview or citydialog canvas.
**************************************************************************/
void canvas_put_line(struct canvas *pcanvas, enum color_std color,
		     enum line_type ltype, int start_x, int start_y,
		     int dx, int dy)
{
  HDC hdc = canvas_get_hdc(pcanvas);
  HPEN old_pen;

  /* FIXME: set line type (size). */
  old_pen = SelectObject(hdc, pen_std[color]);
  MoveToEx(hdc, start_x, start_y, NULL);
  LineTo(hdc, start_x + dx, start_y + dy);
  SelectObject(hdc, old_pen);

  canvas_release_hdc(pcanvas);
}

/**************************************************************************
 Area Selection
**************************************************************************/
void draw_selection_rectangle(int canvas_x, int canvas_y, int w, int h)
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
   * dialogs usually need to be resized).
   */
  
  indicator_sprite[0] = NULL;
  indicator_sprite[1] = NULL;
  indicator_sprite[2] = NULL;
  init_fog_bmp();
  map_canvas_resized(mapview_canvas.width, mapview_canvas.height);
  citydlg_tileset_change();
}
