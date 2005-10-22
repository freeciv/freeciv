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

#define single_tile_pixmap (mapview_canvas.single_tile->bitmap)

extern HBITMAP BITMAP2HBITMAP(BITMAP *bmp);

extern void do_mainwin_layout();

extern int seconds_to_turndone;   
void update_map_canvas_scrollbars_size(void);
void refresh_overview_viewrect_real(HDC hdcp);
static void draw_rates(HDC hdc);


/***************************************************************************
 ...
***************************************************************************/
struct canvas *canvas_create(int width, int height)
{
  struct canvas *result = fc_malloc(sizeof(*result));
  HDC hdc;
  hdc = GetDC(root_window);
  result->bitmap = CreateCompatibleBitmap(hdc, width, height);
  result->hdc = NULL;
  ReleaseDC(root_window, hdc);
  return result;
}

/***************************************************************************
  ...
***************************************************************************/
void canvas_free(struct canvas *store)
{
  DeleteObject(store->bitmap);
  free(store);
}

static struct canvas overview_canvas;

/****************************************************************************
  Return a canvas that is the overview window.
****************************************************************************/
struct canvas *get_overview_window(void)
{
  return &overview_canvas;
}

/***************************************************************************
   ...
***************************************************************************/
void canvas_copy(struct canvas *dest, struct canvas *src,
		 int src_x, int src_y, int dest_x, int dest_y,
		 int width, int height)
{
  HDC hdcsrc = NULL;
  HDC hdcdst = NULL;
  HBITMAP oldsrc = NULL;
  HBITMAP olddst = NULL;
  if (src->hdc) {
    hdcsrc = src->hdc;
  } else {
    hdcsrc = CreateCompatibleDC(NULL);
    oldsrc = SelectObject(hdcsrc, src->bitmap);
  }
  if (dest->hdc) {
    hdcdst = dest->hdc;
  } else if (dest->bitmap) {
    hdcdst = CreateCompatibleDC(NULL);
    olddst = SelectObject(hdcdst, dest->bitmap);
  } else {
    hdcdst = GetDC(root_window);
  }
  BitBlt(hdcdst, dest_x, dest_y, width, height, hdcsrc, src_x, src_y, SRCCOPY);
  if (!src->hdc) {
    SelectObject(hdcsrc, oldsrc);
    DeleteDC(hdcsrc);
  }
  if (!dest->hdc) {
    if (dest->bitmap) {
      SelectObject(hdcdst, olddst);
      DeleteDC(hdcdst);
    } else {
      ReleaseDC(root_window, hdcdst);
    }
  }
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
      intro_gfx = BITMAP2HBITMAP(&intro_gfx_sprite->img);
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
      set_hover_state(NULL, HOVER_NONE, ACTIVITY_LAST);
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
  redraw_selection_rectangle();
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
  char buffer[512], buffer2[32];
  enum color_std growth_color;
  RECT name_rect, growth_rect, prod_rect;
  int extra_width = 0, total_width, total_height;

  HDC hdc;
  HGDIOBJ old = NULL;

  name_rect.left   = 0;
  name_rect.right  = 0;
  name_rect.top    = 0;
  name_rect.bottom = 0;

  growth_rect.left   = 0;
  growth_rect.right  = 0;
  growth_rect.top    = 0;
  growth_rect.bottom = 0;

  prod_rect.left   = 0;
  prod_rect.right  = 0;
  prod_rect.top    = 0;
  prod_rect.bottom = 0;

  if (pcanvas->hdc) {
    hdc = pcanvas->hdc;
  } else if (pcanvas->bitmap) {
    hdc = CreateCompatibleDC(NULL);
    old = SelectObject(hdc, pcanvas->bitmap);
  } else {
    hdc = GetDC(root_window);
  }

  SetBkMode(hdc,TRANSPARENT);

  *width = *height = 0;

  canvas_x += NORMAL_TILE_WIDTH / 2;
  canvas_y += NORMAL_TILE_HEIGHT;

  if (draw_city_names) {
    RECT rc;

    rc.left   = 0;
    rc.right  = 0;
    rc.top    = 0;
    rc.bottom = 0;

    get_city_mapview_name_and_growth(pcity, buffer, sizeof(buffer),
				     buffer2, sizeof(buffer2), &growth_color);

    DrawText(hdc, buffer, strlen(buffer), &name_rect, DT_CALCRECT);

    if (buffer2[0] != '\0') {
      DrawText(hdc, buffer2, strlen(buffer2), &growth_rect, DT_CALCRECT);
      /* HACK: put a character's worth of space between the two strings. */
      DrawText(hdc, "M", 1, &rc, DT_CALCRECT);
      extra_width = rc.right;
    }
    total_width = name_rect.right + extra_width + growth_rect.right;
    total_height = MAX(name_rect.bottom, growth_rect.bottom);

    rc.left   = canvas_x - total_width / 2;
    rc.right  = rc.left + name_rect.right;
    rc.top    = canvas_y;
    rc.bottom = rc.top + total_height;

    SetTextColor(hdc, RGB(0, 0, 0));
    DrawText(hdc, buffer, strlen(buffer), &rc, DT_NOCLIP);
    
    rc.left++;
    rc.top--;
    rc.right++;
    rc.bottom--;

    SetTextColor(hdc, RGB(255, 255, 255));

    DrawText(hdc, pcity->name, strlen(pcity->name), &rc,
	     DT_NOCLIP | DT_CENTER);

    if (buffer2[0] != '\0') {
      rc.left   = canvas_x - total_width / 2 + name_rect.right + extra_width;
      rc.right  = rc.left + growth_rect.right;
      rc.top    = canvas_y + total_height - growth_rect.bottom;
      rc.bottom = rc.top + total_height;

      SetTextColor(hdc, RGB(0, 0, 0));
      DrawText(hdc, buffer2, strlen(buffer2), &rc, DT_NOCLIP);

      rc.left++;
      rc.top--;
      rc.right++;
      rc.bottom--;

      /* FIXME: use correct color */
      if (growth_color == COLOR_STD_WHITE) {
	SetTextColor(hdc, RGB(255, 255, 255));
      } else {	/* COLOR_STD_RED */
	SetTextColor(hdc, RGB(255, 0, 0));
      }
      DrawText(hdc, buffer2, strlen(buffer2), &rc, DT_NOCLIP);
    }
    canvas_y += total_height + 2;

    *width = MAX(*width, total_width);
    *height += total_height + 3;
  }

  if (draw_city_productions && pcity->owner == game.player_idx) {
    RECT rc;

    get_city_mapview_production(pcity, buffer, sizeof(buffer));
      
    DrawText(hdc, buffer, strlen(buffer), &rc, DT_CALCRECT);
    rc.left = canvas_x - 10;
    rc.right = rc.left + 20;
    rc.bottom -= rc.top;
    rc.top = canvas_y;
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

  if (!pcanvas->hdc) {
    if (pcanvas->bitmap) {
      SelectObject(hdc, old);
      DeleteDC(hdc);
    } else {
      ReleaseDC(root_window, hdc);
    }
  }
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
      overview_canvas.hdc = hdc;
      refresh_overview_canvas(/* hdc */);
      overview_canvas.hdc = NULL;
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
  HDC hdc;
  HBITMAP old = NULL; /*Remove warning*/

  /* FIXME: we don't want to have to recreate the hdc each time! */
  if (pcanvas->bitmap) {
    hdc = CreateCompatibleDC(pcanvas->hdc);
    old = SelectObject(hdc, pcanvas->bitmap);
  } else {
    hdc = pcanvas->hdc;
  }
  draw_sprite(sprite, hdc, canvas_x, canvas_y);
  if (pcanvas->bitmap) {
    SelectObject(hdc, old);
    DeleteDC(hdc);
  }
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
  HDC hdc;
  HBITMAP old = NULL;

  if (pcanvas->hdc == NULL) {
    hdc = CreateCompatibleDC(NULL);
    old = SelectObject(hdc, pcanvas->bitmap);
  } else {
    hdc = pcanvas->hdc;
  }

  pixmap_put_overlay_tile_draw(hdc, canvas_x, canvas_y,
			       psprite, fog);

  if (pcanvas->hdc == NULL) {
    SelectObject(hdc, old);
    DeleteDC(hdc);
  }
}

/**************************************************************************
  Draw a filled-in colored rectangle onto the mapview or citydialog canvas.
**************************************************************************/
void canvas_put_rectangle(struct canvas *pcanvas,
			  enum color_std color,
			  int canvas_x, int canvas_y, int width, int height)
{
  HDC hdc;
  HBITMAP old = NULL; /*Remove warning*/
  RECT rect;

  if (pcanvas->bitmap) {
    hdc = CreateCompatibleDC(pcanvas->hdc);
    old = SelectObject(hdc, pcanvas->bitmap);
  } else {
    hdc = pcanvas->hdc;
  }

  /* FillRect doesn't fill bottom and right edges, however canvas_x + width
   * and canvas_y + height are each 1 larger than necessary. */
  SetRect(&rect, canvas_x, canvas_y, canvas_x + width,
		 canvas_y + height);

  FillRect(hdc, &rect, brush_std[color]);

  if (pcanvas->bitmap) {
    SelectObject(hdc, old);
    DeleteDC(hdc);
  }
}
/****************************************************************************
  Fill the area covered by the sprite with the given color.
****************************************************************************/
void canvas_fill_sprite_area(struct canvas *pcanvas,
			     struct Sprite *psprite, enum color_std color,
			     int canvas_x, int canvas_y)
{
  /* FIXME: this may be inefficient */
  HDC hdc;
  HBITMAP old = NULL; /*Remove warning*/
  HPEN oldpen;
  HBRUSH oldbrush;
  POINT points[4];

  if (pcanvas->bitmap) {
    hdc = CreateCompatibleDC(pcanvas->hdc);
    old = SelectObject(hdc, pcanvas->bitmap);
  } else {
    hdc = pcanvas->hdc;
  }

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

  if (pcanvas->bitmap) {
    SelectObject(hdc, old);
    DeleteDC(hdc);
  }
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
  HDC hdc;
  HBITMAP old = NULL; /*Remove warning*/
  HPEN old_pen;

  if (pcanvas->hdc) {
    hdc = pcanvas->hdc;
  } else if (pcanvas->bitmap) {
    hdc = CreateCompatibleDC(pcanvas->hdc);
    old = SelectObject(hdc, pcanvas->bitmap);
  } else {
    hdc = GetDC(root_window);
  }

  /* FIXME: set line type (size). */
  old_pen = SelectObject(hdc, pen_std[color]);
  MoveToEx(hdc, start_x, start_y, NULL);
  LineTo(hdc, start_x + dx, start_y + dy);
  SelectObject(hdc, old_pen);

  if (!pcanvas->hdc) {
    if (pcanvas->bitmap) {
      SelectObject(hdc, old);
      DeleteDC(hdc);
    } else {
      ReleaseDC(root_window, hdc);
    }
  }

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
