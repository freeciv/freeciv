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

/**********************************************************************
                          mapview.c  -  description
                             -------------------
    begin                : Aug 10 2002
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
 *********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <SDL/SDL.h>

/* #define SDL_CVS */

#include "gui_mem.h"

#include "fcintl.h"
#include "game.h"
#include "government.h"		/* government_graphic() */
#include "log.h"
#include "map.h"
#include "player.h"
#include "rand.h"
#include "support.h"

#include "civclient.h"
#include "climisc.h"
#include "climap.h"
#include "colors.h"
#include "control.h"
#include "goto.h"

#include "chatline.h"

#include "graphics.h"
#include "gui_string.h"
#include "gui_stuff.h"
#include "gui_id.h"
#include "gui_zoom.h"
#include "gui_main.h"
#include "gui_tilespec.h"

#include "menu.h"
#include "dialogs.h" /*sdl_map_get_tile_info_text(...)*/
#include "mapctrl.h"
#include "options.h"
#include "tilespec.h"
#include "citydlg.h"
#include "cma_fe.h"
#include "gui_dither.h"
#include "timing.h"
#include "mapview.h"

extern SDL_Event *pFlush_User_Event;
extern SDL_Cursor **pAnimCursor;
extern bool do_cursor_animation;

/* These values are stored in the mapview_canvas struct now. */
#define map_view_x0 mapview_canvas.map_x0
#define map_view_y0 mapview_canvas.map_y0

static Uint32 Amask;
static int HALF_NORMAL_TILE_HEIGHT, HALF_NORMAL_TILE_WIDTH;

static SDL_Surface *pTmpSurface;

static SDL_Surface *pBlinkSurfaceA;
static SDL_Surface *pBlinkSurfaceB;

static SDL_Surface *pMapGrid[3][2];
static SDL_Surface ***pMapBorders = NULL;
static bool UPDATE_OVERVIEW_MAP = FALSE;
int OVERVIEW_START_X;
int OVERVIEW_START_Y;

SDL_Surface *pDitherMask;

static SDL_Surface *pOcean_Tile;
static SDL_Surface *pOcean_Foged_Tile;
static SDL_Surface *pDithers[MAX_NUM_TERRAINS][4];

static enum {
  NORMAL = 0,
  BORDERS = 1,
  TEAMS
} overview_mode = NORMAL;


static void init_dither_tiles(void);
static void free_dither_tiles(void);
static void init_borders_tiles(void);
static void free_borders_tiles(void);
static void fill_dither_buffers(SDL_Surface **pDitherBufs, int x, int y,
					Terrain_type_id terrain);


static void draw_map_cell(SDL_Surface * pDest, Sint16 map_x, Sint16 map_y,
			  Uint16 map_col, Uint16 map_wier, int citymode);

/* ================================================================ */


/**************************************************************************
  Draw some or all of a tile onto the mapview canvas.
**************************************************************************/
void gui_map_put_tile_iso(int map_x, int map_y,
			  int canvas_x, int canvas_y,
			  int offset_x, int offset_y, int offset_y_unit,
			  int width, int height, int height_unit,
			  enum draw_type draw)
{
  if (draw == D_MB_LR || draw == D_FULL) {  
    draw_map_cell(Main.map, canvas_x, canvas_y, (Uint16)map_x, (Uint16)map_y, 0);
  } else {
    static SDL_Rect dest;
        
    dest.x = canvas_x + offset_x;
    dest.y = canvas_y - HALF_NORMAL_TILE_HEIGHT + offset_y_unit;
    dest.w = width;
    dest.h = height_unit;
    SDL_SetClipRect(Main.map, &dest);
  
    draw_map_cell(Main.map, canvas_x, canvas_y, (Uint16)map_x, (Uint16)map_y, 0);
    
    /* clear ClipRect */
    SDL_SetClipRect(Main.map, NULL);

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
  SDL_Rect src = {offset_x, offset_y, width, height};
  SDL_Rect dst = {canvas_x + offset_x, canvas_y + offset_y, 0, 0};
  SDL_BlitSurface(GET_SURF(sprite), &src, pcanvas->map, &dst);
}

/**************************************************************************
  Draw a full sprite onto the mapview or citydialog canvas.
**************************************************************************/
void canvas_put_sprite_full(struct canvas *pcanvas,
			 int canvas_x, int canvas_y,
			 struct Sprite *sprite)
{
  SDL_Rect dst = {canvas_x, canvas_y, 0, 0};
  SDL_BlitSurface(GET_SURF(sprite), NULL, pcanvas->map, &dst);
}

/**************************************************************************
  Draw a filled-in colored rectangle onto the mapview or citydialog canvas.
**************************************************************************/
void canvas_put_rectangle(struct canvas *pcanvas,
		       enum color_std color,
		       int canvas_x, int canvas_y, int width, int height)
{
  SDL_Rect dst = {canvas_x, canvas_y, width, height};
  SDL_FillRect(pcanvas->map, &dst,
	    get_game_color(color, pcanvas->map));
}

/**************************************************************************
  Draw a 1-pixel-width colored line onto the mapview or citydialog canvas.
**************************************************************************/
void canvas_put_line(struct canvas *pcanvas, enum color_std color,
		  enum line_type ltype, int start_x, int start_y,
		  int dx, int dy)
{
  putline(pcanvas->map, start_x, start_y, start_x + dx, start_y + dy,
				get_game_color(color, pcanvas->map));
}

static bool is_flush_queued = FALSE;

/**************************************************************************
  Flush the given part of the buffer(s) to the screen.
**************************************************************************/
void flush_mapcanvas(int canvas_x , int canvas_y ,
		     int pixel_width , int pixel_height)
{
  if(is_flush_queued) {
    dirty_rect(canvas_x, canvas_y, pixel_width, pixel_height);
  } else {
    SDL_Rect src, dst = {canvas_x, canvas_y, pixel_width, pixel_height};

    if (correct_rect_region(&dst)) {
      int i = 0;
      src = dst;
      SDL_BlitSurface(Main.map, &src, Main.screen, &dst);
      dst = src;
      if(draw_city_names||draw_city_productions) {
        SDL_BlitSurface(Main.text, &src, Main.screen, &dst);
        dst = src;
      }
      SDL_BlitSurface(Main.gui, &src, Main.screen, &dst);
      while(Main.guis && Main.guis[i] && i < Main.guis_count) {
        dst = src;
        SDL_BlitSurface(Main.guis[i++], &src, Main.screen, &dst);
      }
      /* flush main buffer to framebuffer */
      SDL_UpdateRect(Main.screen, dst.x, dst.y, dst.w, dst.h);  
    }
  }
}

void flush_rect(SDL_Rect rect)
{
  if(is_flush_queued) {
    sdl_dirty_rect(rect);
  } else {
    static SDL_Rect dst;
    if (correct_rect_region(&rect)) {
      static int i = 0;
      dst = rect;
      SDL_BlitSurface(Main.map, &rect, Main.screen, &dst);
      dst = rect;
      if(draw_city_names||draw_city_productions) {
        SDL_BlitSurface(Main.text, &rect, Main.screen, &dst);
        dst = rect;
      }
      SDL_BlitSurface(Main.gui, &rect, Main.screen, &dst);
      while(Main.guis && Main.guis[i] && i < Main.guis_count) {
        dst = rect;
        SDL_BlitSurface(Main.guis[i++], &rect, Main.screen, &dst);
      }
      i = 0;
      /* flush main buffer to framebuffer */
      SDL_UpdateRect(Main.screen, rect.x, rect.y, rect.w, rect.h);
    }
  }
}

/**************************************************************************
  A callback invoked as a result of a FLUSH event, this function simply
  flushes the mapview canvas.
**************************************************************************/
void unqueue_flush(void)
{
  if(UPDATE_OVERVIEW_MAP) {
    refresh_overview_viewrect();
    UPDATE_OVERVIEW_MAP = FALSE;
  }
  flush_dirty();
  is_flush_queued = FALSE;
}

/**************************************************************************
  Called when a region is marked dirty, this function queues a flush event
  to be handled later by SDL.  The flush may end up being done
  by freeciv before then, in which case it will be a wasted call.
**************************************************************************/
void queue_flush(void)
{
  if (!is_flush_queued) {
    if (SDL_PushEvent(pFlush_User_Event) == 0) {
      is_flush_queued = TRUE;
    } else {
      /* We don't want to set is_flush_queued in this case, since then
       * the flush code would simply stop working.  But this means the
       * below message may be repeated many times. */
      freelog(LOG_ERROR,
	      _("The SDL event buffer is full; you may see drawing errors\n"
		"as a result.  If you see this message often, please\n"
		"report it at %s. Thank you."), BUG_URL);
    }
  }
}

/**************************************************************************
  Save Flush area used by "end" flush.
**************************************************************************/
void dirty_rect(int canvas_x, int canvas_y,
		     int pixel_width, int pixel_height)
{
  SDL_Rect Rect = {canvas_x, canvas_y, pixel_width, pixel_height};
  if ((Main.rects_count < RECT_LIMIT) && correct_rect_region(&Rect)) {
    Main.rects[Main.rects_count++] = Rect;
    queue_flush();
  }
}

/**************************************************************************
  Save Flush rect used by "end" flush.
**************************************************************************/
void sdl_dirty_rect(SDL_Rect Rect)
{
  if ((Main.rects_count < RECT_LIMIT) && correct_rect_region(&Rect)) {
    Main.rects[Main.rects_count++] = Rect;
  }
}

/**************************************************************************
  Sellect entire screen area to "end" flush and block "save" new areas.
**************************************************************************/
void dirty_all(void)
{
  Main.rects_count = RECT_LIMIT;
  queue_flush();
}

/**************************************************************************
  flush entire screen.
**************************************************************************/
void flush_all(void)
{
  is_flush_queued = FALSE;
  Main.rects_count = RECT_LIMIT;
  flush_dirty();
}

/**************************************************************************
  Make "end" Flush "saved" parts/areas of the buffer(s) to the screen.
  Function is called in handle_procesing_finished and handle_thaw_hint
**************************************************************************/
void flush_dirty(void)
{
  static int j = 0;
  if(!Main.rects_count) {
    return;
  }
  if(Main.rects_count >= RECT_LIMIT) {
    SDL_BlitSurface(Main.map, NULL, Main.screen, NULL);
    if(draw_city_names||draw_city_productions) {
      SDL_BlitSurface(Main.text, NULL, Main.screen, NULL);
    }
    SDL_BlitSurface(Main.gui, NULL, Main.screen, NULL);
    while(Main.guis && Main.guis[j] && j < Main.guis_count) {
      SDL_BlitSurface(Main.guis[j++], NULL, Main.screen, NULL);
    }
    j = 0;
    /* flush main buffer to framebuffer */    
    SDL_UpdateRect(Main.screen, 0, 0, 0, 0);
  } else {
    static int i;
    static SDL_Rect dst;
    for(i = 0; i<Main.rects_count; i++) {
      dst = Main.rects[i];
      SDL_BlitSurface(Main.map, &Main.rects[i], Main.screen, &dst);
      dst = Main.rects[i];
      if(draw_city_names||draw_city_productions) {
        SDL_BlitSurface(Main.text, &Main.rects[i], Main.screen, &dst);
        dst = Main.rects[i];
      }
      SDL_BlitSurface(Main.gui, &Main.rects[i], Main.screen, &dst);
      while(Main.guis && Main.guis[j] && j < Main.guis_count) {
        dst = Main.rects[i];
        SDL_BlitSurface(Main.guis[j++], &Main.rects[i], Main.screen, &dst);
      }
      j = 0;
    }
    /* flush main buffer to framebuffer */    
    SDL_UpdateRects(Main.screen, Main.rects_count, Main.rects);
  }
  Main.rects_count = 0;

}

/* ===================================================================== */

/**************************************************************************
  Set information for the indicator icons typically shown in the main
  client window.  The parameters tell which sprite to use for the
  indicator.
**************************************************************************/
void set_indicator_icons(int bulb, int sol, int flake, int gov)
{
  struct GUI *pBuf = NULL;
  char cBuf[128];
  
  bulb = CLIP(0, bulb, NUM_TILES_PROGRESS - 1);
  sol = CLIP(0, sol, NUM_TILES_PROGRESS - 1);
  flake = CLIP(0, flake, NUM_TILES_PROGRESS - 1);

  pBuf = get_widget_pointer_form_main_list(ID_WARMING_ICON);
  pBuf->theme = GET_SURF(sprites.warming[sol]);
  redraw_label(pBuf);
    
  pBuf = get_widget_pointer_form_main_list(ID_COOLING_ICON);
  pBuf->theme = GET_SURF(sprites.cooling[flake]);
  redraw_label(pBuf);
    
  putframe(pBuf->dst, pBuf->size.x - pBuf->size.w - 1,
	   pBuf->size.y - 1,
	   pBuf->size.x + pBuf->size.w,
	   pBuf->size.y + pBuf->size.h,
	   SDL_MapRGB(pBuf->dst->format, 255, 255, 255));
	   
  dirty_rect(pBuf->size.x - pBuf->size.w - 1, pBuf->size.y - 1,
	      2 * pBuf->size.w + 2, 2 * pBuf->size.h + 2);

  if (SDL_Client_Flags & CF_REVOLUTION) {
    struct Sprite *sprite = NULL;
    if (game.government_count == 0) {
      /* HACK: the UNHAPPY citizen is used for the government
       * when we don't know any better. */
      struct citizen_type c = {.type = CITIZEN_UNHAPPY};

      sprite = get_citizen_sprite(c, 0, NULL);
    } else {
      sprite = get_government(gov)->sprite;
    }

    pBuf = get_revolution_widget();
    set_new_icon2_theme(pBuf, GET_SURF(sprite), FALSE);
    
    my_snprintf(cBuf, sizeof(cBuf), _("Revolution (Shift + R)\n%s"),
    				get_gov_pplayer(game.player_ptr)->name);
    copy_chars_to_string16(pBuf->string16, cBuf);
        
    redraw_widget(pBuf);
    sdl_dirty_rect(pBuf->size);
    SDL_Client_Flags &= ~CF_REVOLUTION;
  }

  pBuf = get_tax_rates_widget();
  if(!pBuf->theme) {
    /* create economy icon */
    int i;
    SDL_Surface *pIcon = create_surf(16, 19, SDL_SWSURFACE);
    Uint32 color = SDL_MapRGB(pIcon->format, 255, 255, 0);
    SDL_Rect dst;
    for(i = 0; i < 16; i+=3) {
      putline(pIcon, i, 0, i, pIcon->h - 1, color);
    }
    for(i = 0; i < 19; i+=3) {
      putline(pIcon, 0, i, pIcon->w - 1, i, color);
    }
    
    dst.x = (pIcon->w - pIcons->pBIG_Trade->w) / 2;
    dst.y = (pIcon->h - pIcons->pBIG_Trade->h) / 2;
    SDL_BlitSurface(pIcons->pBIG_Trade, NULL, pIcon, &dst);
    
    SDL_SetColorKey(pIcon, SDL_SRCCOLORKEY|SDL_RLEACCEL, 0x0);
    set_new_icon2_theme(pBuf,pIcon, FALSE);
  }
  
  pBuf = get_research_widget();
  
  my_snprintf(cBuf, sizeof(cBuf), _("Research (F6)\n%s (%d/%d)"),
	      	get_tech_name(game.player_ptr,
			    game.player_ptr->research.researching),
	      	game.player_ptr->research.bulbs_researched,
  		total_bulbs_required(game.player_ptr));

  copy_chars_to_string16(pBuf->string16, cBuf);
  
  set_new_icon2_theme(pBuf, GET_SURF(sprites.bulb[bulb]), FALSE);
  redraw_widget(pBuf);
  sdl_dirty_rect(pBuf->size);
  
}

/**************************************************************************
  Typically an info box is provided to tell the player about the state
  of their civilization.  This function is called when the label is
  changed.
**************************************************************************/
void update_info_label(void)
{
  SDL_Surface *pTmp = NULL;
  SDL_Rect area = {0, 3, 0, 0};
  Uint32 color = 0x0;/* black */
  SDL_Color col = {0, 0, 0, 80};
  char buffer[512];
  SDL_String16 *pText = create_string16(NULL, 0, 10);

  /* set text settings */
  pText->style |= TTF_STYLE_BOLD;
  pText->fgcol.r = 255;
  pText->fgcol.g = 255;
  pText->fgcol.b = 255;


  my_snprintf(buffer, sizeof(buffer),
	      _("%s Population: %s  Year: %s  "
		"Gold %d Tax: %d Lux: %d Sci: %d "),
	      get_nation_name(game.player_ptr->nation),
	      population_to_text(civ_population(game.player_ptr)),
	      textyear(game.year),
	      game.player_ptr->economic.gold,
	      game.player_ptr->economic.tax,
	      game.player_ptr->economic.luxury,
	      game.player_ptr->economic.science);
  
  pText->render = 3;
  pText->bgcol = col;
  
  /* convert to unistr and create text surface */
  copy_chars_to_string16(pText, buffer);
  pTmp = create_text_surf_from_str16(pText);

  area.x = (Main.gui->w - pTmp->w) / 2 - 5;
  area.w = pTmp->w + 8;
  area.h = pTmp->h + 4;

  color = SDL_MapRGBA(Main.gui->format, col.r, col.g, col.b, col.unused);
  SDL_FillRect(Main.gui, &area , color);
  
  color = SDL_MapRGB(Main.gui->format, col.r, col.g, col.b);
  
  /* Horizontal lines */
  putline(Main.gui, area.x + 1, area.y,
		  area.x + area.w - 2, area.y , color);
  putline(Main.gui, area.x + 1, area.y + area.h - 1,
		  area.x + area.w - 2, area.y + area.h - 1, color);

  /* vertical lines */
  putline(Main.gui, area.x + area.w - 1, area.y + 1 ,
  	area.x + area.w - 1, area.y + area.h - 2, color);
  putline(Main.gui, area.x, area.y + 1, area.x,
	  area.y + area.h - 2, color);

  /* turn off alpha calculate */
  SDL_SetAlpha(pTmp, 0x0, 0x0);

  /* blit text to screen */  
  blit_entire_src(pTmp, Main.gui, area.x + 5, area.y + 2);
  
  sdl_dirty_rect(area);
  
  FREESURFACE(pTmp);

  set_indicator_icons(client_research_sprite(),
		      client_warming_sprite(),
		      client_cooling_sprite(),
		      game.player_ptr->government);

  update_timeout_label();

  FREESTRING16(pText);
  
  queue_flush();
}

static int fucus_units_info_callback(struct GUI *pWidget)
{
  struct unit *pUnit = pWidget->data.unit;
  if (pUnit) {
    request_new_unit_activity(pUnit, ACTIVITY_IDLE);
    set_unit_focus(pUnit);
  }
  return -1;
}

/**************************************************************************
  Read Function Name :)
**************************************************************************/
void redraw_unit_info_label(struct unit *pUnit)
{
  struct GUI *pInfo_Window = get_unit_info_window_widget();
  SDL_Rect src, area = {pInfo_Window->size.x, pInfo_Window->size.y, 0, 0};
  SDL_Surface *pBuf_Surf;
  SDL_String16 *pStr;
  
  if (SDL_Client_Flags & CF_UNIT_INFO_SHOW) {   
    /* Unit Window is Show */

    /* blit theme surface */
    SDL_BlitSurface(pInfo_Window->theme, NULL, pInfo_Window->dst, &area);
    
    if (pUnit) {
      SDL_Surface *pName, *pVet_Name = NULL, *pInfo, *pInfo_II = NULL;
      int sy, y, sx, width, height, n;
      bool right;
      char buffer[512];
      struct city *pCity = player_find_city_by_id(game.player_ptr,
						  pUnit->homecity);
      struct tile *pTile = map_get_tile(pUnit->x, pUnit->y);
      int infrastructure = get_tile_infrastructure_set(pTile);

      pStr = pInfo_Window->string16;
      
      change_ptsize16(pStr, 12);

      /* get and draw unit name (with veteran status) */
      copy_chars_to_string16(pStr, unit_type(pUnit)->name);
            
      pStr->style |= TTF_STYLE_BOLD;
      pName = create_text_surf_from_str16(pStr);
      SDL_SetAlpha(pName, 0x0 , 0x0);
      pStr->style &= ~TTF_STYLE_BOLD;
      
      if (pInfo_Window->size.w > 1.8 * DEFAULT_UNITS_W) {
	width = pInfo_Window->size.w / 2;
	right = TRUE;
      } else {
	width = pInfo_Window->size.w;
	right = FALSE;
      }
      
      if(pUnit->veteran) {
	copy_chars_to_string16(pStr, _("veteran"));
        change_ptsize16(pStr, 10);
	pStr->fgcol.b = 255;
        pVet_Name = create_text_surf_from_str16(pStr);
	SDL_SetAlpha(pVet_Name, 0x0, 0x0);
        pStr->fgcol.b = 0;
      }

      /* get and draw other info (MP, terran, city, etc.) */
      change_ptsize16(pStr, 10);

      my_snprintf(buffer, sizeof(buffer), "%s\n%s\n%s%s%s",
		  (hover_unit == pUnit->id) ? _("Select destination") :
		  unit_activity_text(pUnit),
		  sdl_map_get_tile_info_text(pTile),
		  infrastructure ?
		  map_get_infrastructure_text(infrastructure) : "",
		  infrastructure ? "\n" : "", pCity ? pCity->name : _("NONE"));

      copy_chars_to_string16(pStr, buffer);
      pInfo = create_text_surf_from_str16(pStr);
      SDL_SetAlpha(pInfo, 0x0, 0x0);
      
      if (pInfo_Window->size.h > DEFAULT_UNITS_H || right) {
	int h = TTF_FontHeight(pInfo_Window->string16->font);
				     
	my_snprintf(buffer, sizeof(buffer),"%s",
				sdl_get_tile_defense_info_text(pTile));
	
        if (pInfo_Window->size.h > 2 * h + DEFAULT_UNITS_H || right) {
	  if (game.borders > 0 && !pTile->city) {
	    const char *diplo_nation_plural_adjectives[DS_LAST] =
                        {Q_("?nation:Neutral"), Q_("?nation:Hostile"),
     			"" /* unused, DS_CEASEFIRE*/,
     			Q_("?nation:Peaceful"), Q_("?nation:Friendly"), 
     			Q_("?nation:Mysterious")};
            if (pTile->owner == game.player_ptr) {
              cat_snprintf(buffer, sizeof(buffer), _("\nOur Territory"));
            } else {
	      if (pTile->owner) {
                if (game.player_ptr->diplstates[pTile->owner->player_no].type==DS_CEASEFIRE){
		  int turns = game.player_ptr->diplstates[pTile->owner->player_no].turns_left;
		  cat_snprintf(buffer, sizeof(buffer),
		  	PL_("\n%s territory (%d turn ceasefire)",
				"\n%s territory (%d turn ceasefire)", turns),
		 		get_nation_name(pTile->owner->nation), turns);
                } else {
	          cat_snprintf(buffer, sizeof(buffer), _("\nTerritory of the %s %s"),
		    diplo_nation_plural_adjectives[
		  	game.player_ptr->diplstates[pTile->owner->player_no].type],
		    		get_nation_name_plural(pTile->owner->nation));
                }
              } else { /* !pTile->owner */
                cat_snprintf(buffer, sizeof(buffer), _("\nUnclaimed territory"));
              }
	    }
          } /* game.borders > 0 && !pTile->city */
	  
          if (pTile->city) {
            /* Look at city owner, not tile owner (the two should be the same, if
             * borders are in use). */
            struct player *pOwner = city_owner(pTile->city);
            bool citywall, barrack = FALSE, airport = FALSE, port = FALSE;
	    const char *diplo_city_adjectives[DS_LAST] =
    			{Q_("?city:Neutral"), Q_("?city:Hostile"),
     			  "" /*unused, DS_CEASEFIRE */, Q_("?city:Peaceful"),
			  Q_("?city:Friendly"), Q_("?city:Mysterious")};
			  
	    cat_snprintf(buffer, sizeof(buffer), _("\nCity of %s"), pTile->city->name);
            	  
	    citywall = city_got_citywalls(pTile->city);
	    if (pplayers_allied(game.player_ptr, pOwner)) {
	      barrack = (city_affected_by_wonder(pTile->city, B_SUNTZU) ||
	  		city_got_building(pTile->city, B_BARRACKS) || 
	  		city_got_building(pTile->city, B_BARRACKS2) ||
	  		city_got_building(pTile->city, B_BARRACKS3));
	      airport = city_got_effect(pTile->city, B_AIRPORT);
	      port = city_got_effect(pTile->city, B_PORT);
	    }
	  
	    if (citywall || barrack || airport || port) {
	      cat_snprintf(buffer, sizeof(buffer), _(" with "));
	      if (barrack) {
                cat_snprintf(buffer, sizeof(buffer), _("Barracks"));
	        if (port || airport || citywall) {
	          cat_snprintf(buffer, sizeof(buffer), ", ");
	        }
	      }
	      if (port) {
	        cat_snprintf(buffer, sizeof(buffer), _("Port"));
	        if (airport || citywall) {
	          cat_snprintf(buffer, sizeof(buffer), ", ");
	        }
	      }
	      if (airport) {
	        cat_snprintf(buffer, sizeof(buffer), _("Airport"));
	        if (citywall) {
	          cat_snprintf(buffer, sizeof(buffer), ", ");
	        }
	      }
	      if (citywall) {
	        cat_snprintf(buffer, sizeof(buffer), _("City Walls"));
              }
	    }
	    
	    if (pOwner && pOwner != game.player_ptr) {
              /* TRANS: (<nation>,<diplomatic_state>)" */
              cat_snprintf(buffer, sizeof(buffer), _("\n(%s,%s)"),
		  get_nation_name(pOwner->nation),
		  diplo_city_adjectives[game.player_ptr->
				   diplstates[pOwner->player_no].type]);
	    }
	    
	  }
        }
		
	if (pInfo_Window->size.h > 4 * h + DEFAULT_UNITS_H || right) {
          cat_snprintf(buffer, sizeof(buffer), _("\nFood/Prod/Trade: %s"),
				map_get_tile_fpt_text(pUnit->x, pUnit->y));
	}
	
	copy_chars_to_string16(pStr, buffer);
      
	pInfo_II = create_text_surf_smaller_that_w(pStr, width - BLOCK_W - 10);
	SDL_SetAlpha(pInfo_II, 0x0, 0x0);
	
      }
      /* ------------------------------------------- */
      
      n = unit_list_size(&pTile->units);
      y = 0;
      
      if (n > 1 && ((!right && pInfo_II
	 && (pInfo_Window->size.h - DEFAULT_UNITS_H - pInfo_II->h > 52))
         || (right && pInfo_Window->size.h - DEFAULT_UNITS_H > 52))) {
	height = DEFAULT_UNITS_H;
      } else {
	height = pInfo_Window->size.h;
        if (pInfo_Window->size.h > DEFAULT_UNITS_H) {
	  y = (pInfo_Window->size.h - DEFAULT_UNITS_H -
	                 (!right && pInfo_II ? pInfo_II->h : 0)) / 2;
        }
      }
      
      sy = y + DOUBLE_FRAME_WH;
      area.y = pInfo_Window->size.y + sy;
      area.x = pInfo_Window->size.x + FRAME_WH + BLOCK_W +
			    (width - pName->w - BLOCK_W - DOUBLE_FRAME_WH) / 2;
            
      SDL_BlitSurface(pName, NULL, pInfo_Window->dst, &area);
      sy += pName->h;
      if(pVet_Name) {
	area.y += pName->h - 3;
        area.x = pInfo_Window->size.x + FRAME_WH + BLOCK_W +
		(width - pVet_Name->w - BLOCK_W - DOUBLE_FRAME_WH) / 2;
        SDL_BlitSurface(pVet_Name, NULL, pInfo_Window->dst, &area);
	sy += pVet_Name->h - 3;
        FREESURFACE(pVet_Name);
      }
      FREESURFACE(pName);
      
      /* draw unit sprite */
      pBuf_Surf = GET_SURF(unit_type(pUnit)->sprite);
      src = get_smaller_surface_rect(pBuf_Surf);
      sx = FRAME_WH + BLOCK_W + 3 +
			      (width / 2 - src.w - 3 - BLOCK_W - FRAME_WH) / 2;
                  
      area.x = pInfo_Window->size.x + sx + src.w +
      		(width - (sx + src.w) - FRAME_WH - pInfo->w) / 2;
      
      area.y = pInfo_Window->size.y + sy +
	      (DEFAULT_UNITS_H - (sy - y) - FRAME_WH - pInfo->h) / 2;
            
      /* blit unit info text */
      SDL_BlitSurface(pInfo, NULL, pInfo_Window->dst, &area);
      FREESURFACE(pInfo);
      
      area.x = pInfo_Window->size.x + sx;
      area.y = pInfo_Window->size.y + y +
      		(DEFAULT_UNITS_H - DOUBLE_FRAME_WH - src.h) / 2;
      SDL_BlitSurface(pBuf_Surf, &src, pInfo_Window->dst, &area);
      
      
      if (pInfo_II) {
        if (right) {
	  area.x = pInfo_Window->size.x + width +
      				(width - pInfo_II->w) / 2;
	  area.y = pInfo_Window->size.y + FRAME_WH +
	  		(height - FRAME_WH - pInfo_II->h) / 2;
        } else {
	  area.y = pInfo_Window->size.y + DEFAULT_UNITS_H + y;
          area.x = pInfo_Window->size.x + BLOCK_W +
      		(width - BLOCK_W - pInfo_II->w) / 2;
        }
      
        /* blit unit info text */
        SDL_BlitSurface(pInfo_II, NULL, pInfo_Window->dst, &area);
              
        if (right) {
          sy = DEFAULT_UNITS_H;
        } else {
	  sy = area.y - pInfo_Window->size.y + pInfo_II->h;
        }
        FREESURFACE(pInfo_II);
      } else {
	sy = DEFAULT_UNITS_H;
      }
      
      if (n > 1 && (pInfo_Window->size.h - sy > 52)) {
	struct ADVANCED_DLG *pDlg = pInfo_Window->private_data.adv_dlg;
	struct GUI *pBuf = NULL, *pEnd = NULL, *pDock;
	struct city *pHome_City;
        struct unit_type *pUType;
	int num_w, num_h;
	  
	if (pDlg->pEndActiveWidgetList && pDlg->pBeginActiveWidgetList) {
	  del_group(pDlg->pBeginActiveWidgetList, pDlg->pEndActiveWidgetList);
	}
	num_w = (pInfo_Window->size.w - BLOCK_W - DOUBLE_FRAME_WH) / 68;
	num_h = (pInfo_Window->size.h - sy - FRAME_WH) / 52;
	pDock = pInfo_Window;
	n = 0;
        unit_list_iterate(pTile->units, aunit) {
          if (aunit == pUnit) {
	    continue;
	  }
	    
	  pUType = get_unit_type(aunit->type);
          pHome_City = find_city_by_id(aunit->homecity);
          my_snprintf(buffer, sizeof(buffer), "%s (%d,%d,%d)%s\n%s\n(%d/%d)\n%s",
		pUType->name, pUType->attack_strength,
		pUType->defense_strength, pUType->move_rate / SINGLE_MOVE,
                (aunit->veteran ? _("\nveteran") : ""),
                unit_activity_text(aunit),
		aunit->hp, pUType->hp,
		pHome_City ? pHome_City->name : _("None"));
      
	  pBuf_Surf = create_surf(UNIT_TILE_WIDTH,
	    				UNIT_TILE_HEIGHT, SDL_SWSURFACE);

          put_unit_pixmap_draw(aunit, pBuf_Surf, 0, 0);

          if (pBuf_Surf->w > 64) {
            float zoom = 64.0 / pBuf_Surf->w;    
            SDL_Surface *pZoomed = ZoomSurface(pBuf_Surf, zoom, zoom, 1);
            FREESURFACE(pBuf_Surf);
            pBuf_Surf = pZoomed;
          }
          SDL_SetColorKey(pBuf_Surf, SDL_SRCCOLORKEY | SDL_RLEACCEL, 
  				get_first_pixel(pBuf_Surf));
	    
	  pStr = create_str16_from_char(buffer, 10);
          pStr->style |= SF_CENTER;
    
          pBuf = create_icon2(pBuf_Surf, pInfo_Window->dst,
	             (WF_FREE_THEME | WF_DRAW_THEME_TRANSPARENT |
						    WF_WIDGET_HAS_INFO_LABEL));
	
    	  pBuf->string16 = pStr;
          pBuf->data.unit = aunit;
	  pBuf->ID = ID_ICON;
	  DownAdd(pBuf, pDock);
          pDock = pBuf;

          if (!pEnd) {
            pEnd = pBuf;
          }
    
          if (++n > num_w * num_h) {
             set_wflag(pBuf, WF_HIDDEN);
          }
  
          if (unit_owner(aunit) == game.player_ptr) {    
            set_wstate(pBuf, FC_WS_NORMAL);
          }
    
          pBuf->action = fucus_units_info_callback;
    
	} unit_list_iterate_end;	  
	    
	pDlg->pBeginActiveWidgetList = pBuf;
	pDlg->pEndActiveWidgetList = pEnd;
	pDlg->pActiveWidgetList = pEnd;
	  	  	  
	if (n > num_w * num_h) {
    
	  if (!pDlg->pScroll) {
	      
            pDlg->pScroll = MALLOC(sizeof(struct ScrollBar));
            pDlg->pScroll->active = num_h;
            pDlg->pScroll->step = num_w;
            pDlg->pScroll->count = n;
	      
            create_vertical_scrollbar(pDlg, num_w, num_h, FALSE, TRUE);
          
	  } else {
	    pDlg->pScroll->active = num_h;
            pDlg->pScroll->step = num_w;
            pDlg->pScroll->count = n;
	    show_scrollbar(pDlg->pScroll);
	  }
	    
	  /* create up button */
          pBuf = pDlg->pScroll->pUp_Left_Button;
          pBuf->size.x = pInfo_Window->size.x +
			      pInfo_Window->size.w - FRAME_WH - pBuf->size.w;
          pBuf->size.y = pInfo_Window->size.y + sy +
	    			(pInfo_Window->size.h - sy - num_h * 52) / 2;
          pBuf->size.h = (num_h * 52) / 2;
        
          /* create down button */
          pBuf = pDlg->pScroll->pDown_Right_Button;
          pBuf->size.x = pDlg->pScroll->pUp_Left_Button->size.x;
          pBuf->size.y = pDlg->pScroll->pUp_Left_Button->size.y +
	      			pDlg->pScroll->pUp_Left_Button->size.h;
          pBuf->size.h = pDlg->pScroll->pUp_Left_Button->size.h;
	    	    
        } else {
	  if (pDlg->pScroll) {
	    hide_scrollbar(pDlg->pScroll);
	  }
	  num_h = (n + num_w - 1) / num_w;
	}
	  
	setup_vertical_widgets_position(num_w,
			pInfo_Window->size.x + FRAME_WH + BLOCK_W + 2,
			pInfo_Window->size.y + sy +
	  			(pInfo_Window->size.h - sy - num_h * 52) / 2,
	  		0, 0, pDlg->pBeginActiveWidgetList,
			  pDlg->pEndActiveWidgetList);
	  	  
      } else {
	if (pInfo_Window->private_data.adv_dlg->pEndActiveWidgetList) {
	  del_group(pInfo_Window->private_data.adv_dlg->pBeginActiveWidgetList,
	    		pInfo_Window->private_data.adv_dlg->pEndActiveWidgetList);
	}
	if (pInfo_Window->private_data.adv_dlg->pScroll) {
	  hide_scrollbar(pInfo_Window->private_data.adv_dlg->pScroll);
	}
      }
    
    } else { /* pUnit */
      if (pInfo_Window->private_data.adv_dlg->pEndActiveWidgetList) {
	del_group(pInfo_Window->private_data.adv_dlg->pBeginActiveWidgetList,
	    		pInfo_Window->private_data.adv_dlg->pEndActiveWidgetList);
      }
      if (pInfo_Window->private_data.adv_dlg->pScroll) {
	hide_scrollbar(pInfo_Window->private_data.adv_dlg->pScroll);
      }
      change_ptsize16(pInfo_Window->string16, 14);
      copy_chars_to_string16(pInfo_Window->string16,
      					_("End of Turn\n(Press Enter)"));
      pBuf_Surf = create_text_surf_from_str16(pInfo_Window->string16);
      SDL_SetAlpha(pBuf_Surf, 0x0, 0x0);
      area.x = pInfo_Window->size.x + BLOCK_W +
      			(pInfo_Window->size.w - BLOCK_W - pBuf_Surf->w)/2;
      area.y = pInfo_Window->size.y + (pInfo_Window->size.h - pBuf_Surf->h)/2;
      SDL_BlitSurface(pBuf_Surf, NULL, pInfo_Window->dst, &area);
      FREESURFACE(pBuf_Surf);
    }

    redraw_group(pInfo_Window->private_data.adv_dlg->pBeginWidgetList,
	    	pInfo_Window->private_data.adv_dlg->pEndWidgetList->prev, 0);
    
  } else {
#if 0    
    /* draw hidden */
    area.x = Main.screen->w - pBuf_Surf->w - FRAME_WH;
    area.y = Main.screen->h - pBuf_Surf->h - FRAME_WH;
    
    SDL_BlitSurface(pInfo_Window->theme, NULL, pInfo_Window->dst, &area);
#endif    
  }
  
  sdl_dirty_rect(get_unit_info_window_widget()->size);
}

static bool is_anim_enabled(void)
{
  return (SDL_Client_Flags & CF_FOCUS_ANIMATION) == CF_FOCUS_ANIMATION;
}

/**************************************************************************
  Set one of the unit icons in the information area based on punit.
  NULL will be pased to clear the icon. idx==-1 will be passed to
  indicate this is the active unit, or idx in [0..num_units_below-1] for
  secondary (inactive) units on the same tile.
**************************************************************************/
void set_unit_icon(int idx, struct unit *punit)
{
/* Balast */
}

/**************************************************************************
  Most clients use an arrow (e.g., sprites.right_arrow) to indicate when
  the units_below will not fit. This function is called to activate and
  deactivate the arrow.
**************************************************************************/
void set_unit_icons_more_arrow(bool onoff)
{
/* Balast */
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
void update_unit_info_label(struct unit *pUnit)
{
  static bool mutant = FALSE;
   
  if (get_client_state() != CLIENT_GAME_RUNNING_STATE) {
    return;
  }
  
  /* draw unit info window */
  redraw_unit_info_label(pUnit);
  
  if (pUnit) {

    if(!is_anim_enabled()) {
      enable_focus_animation();
    }
    if (hover_unit != pUnit->id) {
      set_hover_state(NULL, HOVER_NONE);
    }
    switch (hover_state) {
    case HOVER_NONE:
      if (mutant) {
	SDL_SetCursor(pStd_Cursor);
	pAnimCursor = NULL;
	mutant = FALSE;
      }
      break;
    case HOVER_PATROL:
      if (pAnim->Cursors.Patrol) {
	if (do_cursor_animation && pAnim->Cursors.Patrol[1]) {
	  pAnimCursor = pAnim->Cursors.Patrol;
	} else {
	  SDL_SetCursor(pAnim->Cursors.Patrol[0]);
	}
      } else {
        SDL_SetCursor(pPatrol_Cursor);
      }
      mutant = TRUE;
      break;
    case HOVER_GOTO:
      if (pAnim->Cursors.Goto) {
	if (do_cursor_animation && pAnim->Cursors.Goto[1]) {
	  pAnimCursor = pAnim->Cursors.Goto;
	} else {
	  SDL_SetCursor(pAnim->Cursors.Goto[0]);
	}
      } else {
        SDL_SetCursor(pGoto_Cursor);
      }
      mutant = TRUE;
      break;
    case HOVER_CONNECT:
      if (pAnim->Cursors.Connect) {
	if (do_cursor_animation && pAnim->Cursors.Connect[1]) {
	  pAnimCursor = pAnim->Cursors.Connect;
	} else {
	  SDL_SetCursor(pAnim->Cursors.Connect[0]);
	}
      } else {
        SDL_SetCursor(pGoto_Cursor);
      }
      mutant = TRUE;
      break;
    case HOVER_NUKE:
      if (pAnim->Cursors.Nuke) {
	if (do_cursor_animation && pAnim->Cursors.Nuke[1]) {
	  pAnimCursor = pAnim->Cursors.Nuke;
	} else {
	  SDL_SetCursor(pAnim->Cursors.Nuke[0]);
	}
      } else {
        SDL_SetCursor(pNuke_Cursor);
      }
      mutant = TRUE;
      break;
    case HOVER_PARADROP:
      if (pAnim->Cursors.Paradrop) {
	if (do_cursor_animation && pAnim->Cursors.Paradrop[1]) {
	  pAnimCursor = pAnim->Cursors.Paradrop;
	} else {
	  SDL_SetCursor(pAnim->Cursors.Paradrop[0]);
	}
      } else {
        SDL_SetCursor(pDrop_Cursor);
      }
      mutant = TRUE;
      break;
    }
  } else {
    disable_focus_animation();
  }
}

void update_timeout_label(void)
{
  freelog(LOG_DEBUG, "MAPVIEW: update_timeout_label : PORT ME");
}

void update_turn_done_button(bool do_restore)
{
  freelog(LOG_DEBUG, "MAPVIEW: update_turn_done_button : PORT ME");
}

/* ===================================================================== */
/* ========================== City Descriptions ======================== */
/* ===================================================================== */

/**************************************************************************
  Update (refresh) all of the city descriptions on the mapview.
**************************************************************************/
void update_city_descriptions(void)
{
  /* redraw buffer */
  show_city_descriptions();
  dirty_all();  
}

/**************************************************************************
  Clear the city descriptions out of the buffer.
**************************************************************************/
void prepare_show_city_descriptions(void)
{
  SDL_FillRect(Main.text, NULL, 0x0);
}

/**************************************************************************
  Draw a description for the given city onto the surface.
**************************************************************************/
static void put_city_desc_on_surface(SDL_Surface *pDest,
				     struct city *pCity,
				     int canvas_x, int canvas_y)
{
  static char buffer[128];
  SDL_Surface *pCity_Size = NULL, *pCity_Name = NULL, *pCity_Prod = NULL;
  int togrow;
  SDL_String16 *pText = NULL;
  SDL_Rect dst, clear_area = {0, 0, 0, 0};	
  SDL_Color color_size = *(get_game_colorRGB(
			  player_color(get_player(pCity->owner))));
  SDL_Color color_bg = {0, 0, 0, 80};
  Uint32 frame_color;
  
  color_size.unused = 128;
  
  pText = create_string16(NULL, 0, 10);
  pText->style |= (TTF_STYLE_BOLD|SF_CENTER);
  pText->fgcol = *(get_game_colorRGB(COLOR_STD_WHITE));
  
  if (SDL_Client_Flags & CF_CIV3_CITY_TEXT_STYLE)
  {
    pText->render = 3;
    pText->bgcol = color_bg;
  }
   
  canvas_y += NORMAL_TILE_HEIGHT;

  if (draw_city_names) {
    if (draw_city_growth && pCity->owner == game.player_idx) {
      togrow = city_turns_to_grow(pCity);
      switch (togrow) {
      case 0:
	my_snprintf(buffer, sizeof(buffer), "%s: #", pCity->name);
	break;
      case FC_INFINITY:
	my_snprintf(buffer, sizeof(buffer), "%s: --", pCity->name);
	break;
      default:
	my_snprintf(buffer, sizeof(buffer), "%s: %d", pCity->name, togrow);
	break;
      }
    } else {
      /* Force certain behavior below. */
      togrow = 0;
      my_snprintf(buffer, sizeof(buffer), "%s\n%s", pCity->name , 
	    get_nation_name(get_player(pCity->owner)->nation));
    }

    if (togrow < 0) {
      /* set RED */
      pText->fgcol.g = 0;
      pText->fgcol.b = 0;
    }
    
    copy_chars_to_string16(pText, buffer);
    pCity_Name = create_text_surf_from_str16(pText);

    if (togrow < 0) {
      /* set white */
      pText->fgcol.g = 255;
      pText->fgcol.b = 255;
    }
  }

  /* City Production */
  if (draw_city_productions && pCity->owner == game.player_idx) {
    /* set text color */
    if (pCity->is_building_unit) {
      pText->fgcol.r = 255;
      pText->fgcol.g = 255;
      pText->fgcol.b = 0;
    } else {
      if (get_improvement_type(pCity->currently_building)->is_wonder) {
	pText->fgcol.r = 0xe2;
	pText->fgcol.g = 0xc2;
	pText->fgcol.b = 0x1f;
      } else {
	pText->fgcol.r = 255;
	pText->fgcol.g = 255;
	pText->fgcol.b = 255;
      }
    }

    get_city_mapview_production(pCity, buffer, sizeof(buffer));

    copy_chars_to_string16(pText, buffer);
    pCity_Prod = create_text_surf_from_str16(pText);
    
  }
  else
  {
    if (!pCity_Name && (SDL_Client_Flags & CF_CIV3_CITY_TEXT_STYLE))
    {
      /* Civ3 style don't draw player flag and size
         outside city description and that is reason I made this hack */
      my_snprintf(buffer, sizeof(buffer), "%s", 
	    get_nation_name(get_player(pCity->owner)->nation));
	    
      copy_chars_to_string16(pText, buffer);
      pCity_Prod = create_text_surf_from_str16(pText);    
    }
  }
  
  
  if (SDL_Client_Flags & CF_CIV3_CITY_TEXT_STYLE)
  {
  
    /* city size */
    if (pCity_Name || pCity_Prod) {
      my_snprintf(buffer , sizeof(buffer), "%d", pCity->size);
      copy_chars_to_string16(pText, buffer);
      pText->style |= TTF_STYLE_BOLD;
      pText->fgcol.r = 255;
      pText->fgcol.g = 255;
      pText->fgcol.b = 255;
      pText->bgcol = color_size;
      pCity_Size = create_text_surf_from_str16(pText);
	    
      clear_area.y = canvas_y;    
    }
                
    if (pCity_Name)
    {
      clear_area.w = pCity_Name->w + 6;
      clear_area.h = pCity_Name->h;
    }
  
    if (pCity_Prod)
    {
      clear_area.h += pCity_Prod->h;
      clear_area.w = MAX(clear_area.w , pCity_Prod->w + 6);
    }
  
    clear_area.h += 2;
    if (pCity_Name && pCity_Prod) {
      clear_area.h += 1;
    }
    
    if (pCity_Size)
    {
      /* this isn't a error */	    
      clear_area.w += pCity_Size->h + 1;
      clear_area.h = MAX(clear_area.h , pCity_Size->h + 2);
    }
    
    if (clear_area.w)
    {
      clear_area.x = canvas_x + (NORMAL_TILE_WIDTH - clear_area.w) / 2;
    }
  
    if (pCity_Size)
    {
        
      frame_color = SDL_MapRGB(pDest->format, color_size.r ,color_size.g,
						      color_size.b);
  
      /* solid citi size background */
      dst = clear_area;	    
      SDL_FillRect(pDest, &dst,
      		SDL_MapRGBA(pDest->format, color_bg.r, color_bg.g,
					color_bg.b, color_bg.unused));
      /* solid text background */
      dst.w = pCity_Size->h + 1;	    
      SDL_FillRect(pDest, &dst ,
	      SDL_MapRGBA(pDest->format, color_size.r, color_size.g,
					color_size.b, color_size.unused));
					
      /* blit city size number */
      SDL_SetAlpha(pCity_Size, 0x0, 0x0);
      
      /* this isn't error */
      dst.x = clear_area.x + (pCity_Size->h - pCity_Size->w) / 2;
      dst.y = canvas_y + (clear_area.h - pCity_Size->h) / 2;
      SDL_BlitSurface(pCity_Size, NULL, pDest, &dst);

      /* Draw Frame */
      /* Horizontal lines */
      putline(pDest, clear_area.x , clear_area.y - 1,
			  clear_area.x + clear_area.w,
                          clear_area.y - 1, frame_color);
      putline(pDest, clear_area.x, clear_area.y + clear_area.h,
			  clear_area.x + clear_area.w,
                          clear_area.y + clear_area.h, frame_color);

      if (pCity_Name && pCity_Prod) {
        putline(pDest, clear_area.x + 1 + pCity_Size->h,
			clear_area.y + pCity_Name->h,
			clear_area.x + clear_area.w,
                        clear_area.y + pCity_Name->h, frame_color);
      }

      /* vertical lines */
      putline(pDest, clear_area.x + clear_area.w, clear_area.y,
			  clear_area.x + clear_area.w,
                          clear_area.y + clear_area.h, frame_color);
      putline(pDest, clear_area.x - 1, clear_area.y, clear_area.x - 1,
                          clear_area.y + clear_area.h, frame_color);
			  

      putline(pDest, clear_area.x + 1 + pCity_Size->h, clear_area.y,
			  clear_area.x + 1 + pCity_Size->h,
                          clear_area.y + clear_area.h, frame_color);
    }
    
    if (pCity_Name)
    {
      SDL_SetAlpha(pCity_Name, 0x0, 0x0);
      dst.x = clear_area.x + pCity_Size->h + 2 +
		(clear_area.w - (pCity_Size->h + 2) - pCity_Name->w) / 2;
      if (pCity_Prod) {
	dst.y = canvas_y;
      } else {
        dst.y = canvas_y + (clear_area.h - pCity_Name->h) / 2;
      }
      SDL_BlitSurface(pCity_Name, NULL, pDest, &dst);
      canvas_y += pCity_Name->h;
    }
  
    if (pCity_Prod)
    {
      SDL_SetAlpha(pCity_Prod, 0x0, 0x0);
      dst.x = clear_area.x + pCity_Size->h + 2 +
		(clear_area.w - (pCity_Size->h + 2) - pCity_Prod->w) / 2;
      if (pCity_Name) {
	dst.y = canvas_y + 1;
      } else {
        dst.y = canvas_y + (clear_area.h - pCity_Prod->h) / 2;
      }
      SDL_BlitSurface(pCity_Prod, NULL, pDest, &dst);
    } 
  }
  else
  {
    if (pCity_Name)
    {
      SDL_SetAlpha(pCity_Name, 0x0, 0x0);
      dst.x = canvas_x + (NORMAL_TILE_WIDTH - pCity_Name->w) / 2;
      dst.y = canvas_y;
      SDL_BlitSurface(pCity_Name, NULL, pDest, &dst);
      canvas_y += pCity_Name->h;
    }
  
    if (pCity_Prod)
    {
      SDL_SetAlpha(pCity_Prod, 0x0, 0x0);
      dst.x = canvas_x + (NORMAL_TILE_WIDTH - pCity_Prod->w) / 2;
      dst.y = canvas_y;
      SDL_BlitSurface(pCity_Prod, NULL, pDest, &dst);
    }
    
  }
  
  FREESURFACE(pCity_Prod);
  FREESURFACE(pCity_Name);
  FREESURFACE(pCity_Size);
  FREESTRING16(pText);
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
void show_city_desc(struct city *pcity, int canvas_x, int canvas_y)
{
  put_city_desc_on_surface(Main.text, pcity,
                     canvas_x, canvas_y - NORMAL_TILE_HEIGHT / 6 );
}

/* ===================================================================== */
/* =============================== Mini Map ============================ */
/* ===================================================================== */

/**************************************************************************
...
**************************************************************************/
void center_minimap_on_minimap_window(void)
{
  struct GUI *pMMap = get_minimap_window_widget();
  OVERVIEW_START_X = FRAME_WH +
	  (pMMap->size.w - 30 - DOUBLE_FRAME_WH -
				  OVERVIEW_TILE_WIDTH * map.xsize) / 2;
  OVERVIEW_START_Y = FRAME_WH + (pMMap->size.h - DOUBLE_FRAME_WH -
			  OVERVIEW_TILE_HEIGHT * map.ysize) / 2;
}

/**************************************************************************
  Set the dimensions for the map overview, in map units (tiles).
  Typically each tile will be a 2x2 rectangle, although this may vary.
**************************************************************************/
void set_overview_dimensions(int w, int h)
{
  struct GUI *pMMap = get_minimap_window_widget();
  int width = pMMap->size.w - 30 - DOUBLE_FRAME_WH;
  int height = pMMap->size.h - DOUBLE_FRAME_WH;
  
  freelog(LOG_DEBUG,
    "MAPVIEW: 'set_overview_dimensions' call with x = %d  y = %d", w, h);

  if(w > width || h > height) {
    Remake_MiniMap(w, h);
    width = pMMap->size.w - 30 - DOUBLE_FRAME_WH;
    height = pMMap->size.h - DOUBLE_FRAME_WH;
  }
  
  OVERVIEW_TILE_WIDTH = MAX(1, width / w);
  OVERVIEW_TILE_HEIGHT = MAX(1, height / h);
  center_minimap_on_minimap_window();
  
  enable(ID_TOGGLE_UNITS_WINDOW_BUTTON);
  enable(ID_REVOLUTION);
  enable(ID_ECONOMY);
  enable(ID_RESEARCH);
  
  /* New Turn */
  pMMap = pMMap->prev;
    
  /* enable PLAYERS BUTTON */
  pMMap = pMMap->prev;
  set_wstate(pMMap, FC_WS_NORMAL);
    
  /* enable ID_FIND_CITY */
  pMMap = pMMap->prev;
  set_wstate(pMMap, FC_WS_NORMAL);
  
  /* enable UNITS BUTTON */
  pMMap = pMMap->prev;
  set_wstate(pMMap, FC_WS_NORMAL);
  
  /* enable ID_CHATLINE_TOGGLE_LOG_WINDOW_BUTTON */
  pMMap = pMMap->prev;
  set_wstate(pMMap, FC_WS_NORMAL);
  
  /* enable toggle minimap mode */
  pMMap = pMMap->prev;
  set_wstate(pMMap, FC_WS_NORMAL);
  
  /* enable ID_TOGGLE_MAP_WINDOW_BUTTON */
  pMMap = pMMap->prev;
  set_wstate(pMMap, FC_WS_NORMAL);
  
  /* del init string */
  pMMap = get_widget_pointer_form_main_list(ID_WAITING_LABEL);

  if (pMMap) {
    del_widget_from_gui_list(pMMap);
  }

  /* this is here becouse I need function that is call after game start */
  if(is_isometric) {  
    free_dither_tiles();
    init_dither_tiles();
    free_borders_tiles();
    init_borders_tiles();
  }
  
  draw_city_names = TRUE;
  
}

void toggle_overview_mode(void)
{
  if (overview_mode == BORDERS) {
    overview_mode = NORMAL;
  } else {
    overview_mode = BORDERS;
  }
}

/**************************************************************************
...
**************************************************************************/
static SDL_Color sdl_overview_tile_color(int x, int y)
{
  SDL_Color color;
  struct tile *pTile=map_get_tile(x, y);
      
  if ((enum known_type)pTile->known == TILE_UNKNOWN) {
    return *(get_game_colorRGB(COLOR_STD_BLACK));
  } else {
    struct city *pCity = pTile->city;
    if (pCity) {
      if(pCity->owner == game.player_idx) {
        return *(get_game_colorRGB(COLOR_STD_WHITE));
      } else {
        return *(get_game_colorRGB(COLOR_STD_CYAN));
      }
    } else {
      struct unit *pUnit = find_visible_unit(pTile);
      if (pUnit) {
        if(pUnit->owner == game.player_idx) {
	  return *(get_game_colorRGB(COLOR_STD_YELLOW));
	} else {
	  return *(get_game_colorRGB(COLOR_STD_RED));
	}
      } else {
        switch (overview_mode) {
          case BORDERS:
	    if (pTile->owner) {
	      if (is_ocean(pTile->terrain)) {
                if ((enum known_type)pTile->known == TILE_KNOWN_FOGGED
	           && draw_fog_of_war) {
	          return *(get_game_colorRGB(COLOR_STD_RACE4));
                } else {
		  SDL_Color color_fog = *(get_game_colorRGB(COLOR_STD_RACE0 +
		  				pTile->owner->player_no));
		  
		  color = *(get_game_colorRGB(COLOR_STD_OCEAN));
		  
		  ALPHA_BLEND(color_fog.r, color_fog.g, color_fog.b, 64,
			         color.r, color.g, color.b);
	          return color;
                }
              } else {
                if ((enum known_type)pTile->known == TILE_KNOWN_FOGGED
	           && draw_fog_of_war) {
		  color = *(get_game_colorRGB(COLOR_STD_RACE0 +
		  				pTile->owner->player_no));
		  
		  ALPHA_BLEND(0, 0, 0, 64,
			         color.r, color.g, color.b);
	          return color;
                } else {
	          return *(get_game_colorRGB(COLOR_STD_RACE0 +
		  				pTile->owner->player_no));
                }
              } 
	    } else {
	      goto STD;
	    }
          break;
          case TEAMS:
	    goto STD;
          break;
          default:
	  {
STD:        if (is_ocean(pTile->terrain)) {
	      if ((enum known_type)pTile->known == TILE_KNOWN_FOGGED
	         && draw_fog_of_war) {
	        return *(get_game_colorRGB(COLOR_STD_RACE4));
              } else {
	        return *(get_game_colorRGB(COLOR_STD_OCEAN));
              }
            } else {
	      if ((enum known_type)pTile->known == TILE_KNOWN_FOGGED
	          && draw_fog_of_war) {
	        return *(get_game_colorRGB(COLOR_STD_GROUND_FOGED));
              } else {
	        return *(get_game_colorRGB(COLOR_STD_GROUND));
              }
            }
	  }
        }	
      }
    }
  }
    
  return color;
}

/**************************************************************************
  Update the tile for the given map position on the overview.
**************************************************************************/
void overview_update_tile(int x, int y)
{
  struct GUI *pMMap = get_minimap_window_widget();
  SDL_Rect cell_size = {OVERVIEW_TILE_WIDTH * x + OVERVIEW_START_X,
			OVERVIEW_TILE_HEIGHT * y + OVERVIEW_START_Y,
			OVERVIEW_TILE_WIDTH, OVERVIEW_TILE_HEIGHT};
  SDL_Color color = sdl_overview_tile_color(x, y);

  freelog(LOG_DEBUG, "MAPVIEW: overview_update_tile (x = %d y = %d )", x, y);
			 
  SDL_FillRect(pMMap->theme, &cell_size,
	    SDL_MapRGBA(pMMap->theme->format, color.r,color.g,
    					color.b,color.unused));
  UPDATE_OVERVIEW_MAP = TRUE;
}

/**************************************************************************
  Refresh (update) the entire map overview.
**************************************************************************/
void refresh_overview_canvas(void)
{
  struct GUI *pMMap = get_minimap_window_widget();
  SDL_Rect map_area = {FRAME_WH, FRAME_WH,
			pMMap->size.w - 30 - DOUBLE_FRAME_WH,
    			pMMap->size.h - DOUBLE_FRAME_WH}; 
  SDL_Rect cell_size = {OVERVIEW_START_X, OVERVIEW_START_Y,
			OVERVIEW_TILE_WIDTH, OVERVIEW_TILE_HEIGHT};
  SDL_Color std_color;
  Uint16 col = 0, row = 0;
  			
  /* clear map area */
  SDL_FillRect(pMMap->theme, &map_area, 
  	SDL_MapRGB(pMMap->theme->format, 0x0, 0x0, 0x0));
  
  while (TRUE) { /* mini map draw loop */
    std_color = sdl_overview_tile_color(col, row); 
    SDL_FillRect(pMMap->theme, &cell_size,
	    SDL_MapRGBA(pMMap->theme->format, std_color.r,std_color.g,
    					std_color.b,std_color.unused));

    cell_size.x += OVERVIEW_TILE_WIDTH;
    col++;

    if (col == map.xsize) {
      cell_size.y += OVERVIEW_TILE_HEIGHT;
      cell_size.x = OVERVIEW_START_X;
      row++;
      if (row == map.ysize) {
	break;
      }
      col = 0;
    }

  } /* end mini map draw loop */
}

/**************************************************************************
  draw line on mini map.
  this algoritm is far from optim.
**************************************************************************/
static void putline_on_mini_map(SDL_Rect * pMapArea, Sint16 x0, Sint16 y0,
		Sint16 x1, Sint16 y1, Uint32 color, SDL_Surface *pDest)
{
  register float m;
  register int x, y;
  int xloop, w = (x1 - x0);


  m = (float) (y1 - y0) / w;

  if (x0 < 0) {
    x0 += pMapArea->w;
  } else if (x0 >= pMapArea->w) {
    x0 -= pMapArea->w;
  }

  if (x1 < 0) {
    x1 += pMapArea->w;
  } else if (x1 >= pMapArea->w) {
    x1 -= pMapArea->w;
  }

  if (y0 < 0) {
    y0 += pMapArea->h;
  } else if (y0 >= pMapArea->h) {
    y0 -= pMapArea->h;
  }

  lock_surf(pDest);

  for (xloop = 0; xloop < w; xloop++) {

    x = x0 + xloop;

    y = m * (x - x0) + y0;

    if (x >= pMapArea->w) {
      x -= pMapArea->w;
    }

    if (y < 0) {
      y += pMapArea->h;
    } else if (y >= pMapArea->h) {
      y -= pMapArea->h;
    }

    x += pMapArea->x;
    y += pMapArea->y;

    putpixel(pDest, x, y, color);

    x -= pMapArea->x;

  }

  unlock_surf(pDest);
}

/**************************************************************************
  Refresh (update) the viewrect on the overview. This is the rectangle
  showing the area covered by the mapview.
**************************************************************************/
void refresh_overview_viewrect(void)
{

  int Nx, Ny, Ex, Ey, Wx, Wy, Sx, Sy, map_w, map_h;
  Uint32 color;
  struct GUI *pMMap, *pBuf;
  SDL_Rect map_area;

  if (get_client_state() != CLIENT_GAME_RUNNING_STATE) {
    return;
  }
  
  freelog(LOG_DEBUG, "MAPVIEW: refresh_overview_viewrect");
  
  pMMap = get_minimap_window_widget();
    
  map_area.x = 0;
  map_area.y = pMMap->dst->h - pMMap->theme->h;
    
  if (SDL_Client_Flags & CF_MINI_MAP_SHOW) {

    SDL_BlitSurface(pMMap->theme, NULL, pMMap->dst, &map_area);
    
    map_area.x += OVERVIEW_START_X;
    map_area.y += OVERVIEW_START_Y;
    map_area.w = MIN(pMMap->size.w - 30 - DOUBLE_FRAME_WH,
					    OVERVIEW_TILE_WIDTH * map.xsize);
    map_area.h = MIN(pMMap->size.h - DOUBLE_FRAME_WH,
					    OVERVIEW_TILE_HEIGHT * map.ysize);
    
    if(OVERVIEW_START_X != FRAME_WH || OVERVIEW_START_Y != FRAME_WH) {
      putframe(pMMap->dst , map_area.x - 1, map_area.y - 1,
			map_area.x + map_area.w + 1,
    			map_area.y + map_area.h + 1, 0xFFFFFFFF);
    }
    
    /* The x's and y's are in overview coordinates. */
    map_w = mapview_canvas.tile_width;
    map_h = mapview_canvas.tile_height;
    
#if 0
    /* take from Main Map */
    if (!canvas_to_map_pos(&Wx, &Wy, 0, 0)) {
      nearest_real_pos(&Wx, &Wy);
    }
#endif
    
    Wx = OVERVIEW_TILE_WIDTH * map_view_x0;
    Wy = OVERVIEW_TILE_HEIGHT * map_view_y0;

    Nx = Wx + OVERVIEW_TILE_WIDTH * map_w;
    Ny = Wy - OVERVIEW_TILE_HEIGHT * map_w;
    
    Sx = Wx + OVERVIEW_TILE_WIDTH * map_h;
    Sy = Wy + OVERVIEW_TILE_HEIGHT * map_h;
    Ex = Nx + OVERVIEW_TILE_WIDTH * map_h;
    Ey = Ny + OVERVIEW_TILE_HEIGHT * map_h;

    color = 0xFFFFFFFF;
    
    putline_on_mini_map(&map_area, Nx, Ny, Ex, Ey, color, pMMap->dst);

    putline_on_mini_map(&map_area, Sx, Sy, Ex, Ey, color, pMMap->dst);

    putline_on_mini_map(&map_area, Wx, Wy, Sx, Sy, color, pMMap->dst);

    putline_on_mini_map(&map_area, Wx, Wy, Nx, Ny, color, pMMap->dst);

    freelog(LOG_DEBUG, "wx,wy: %d,%d nx,ny:%d,%x ex,ey:%d,%d, sx,sy:%d,%d",
	    Wx, Wy, Nx, Ny, Ex, Ey, Sx, Sy);
    /* ===== */


    /* redraw widgets */

    /* ID_NEW_TURN */
    pBuf = pMMap->prev;
    if (!pBuf->gfx) {
      pBuf->gfx = crop_rect_from_surface(pBuf->dst, &pBuf->size);
    }

    real_redraw_icon(pBuf);

    /* ===== */
    /* ID_PLAYERS */
    pBuf = pBuf->prev;

    if (!pBuf->gfx) {
      pBuf->gfx = crop_rect_from_surface(pBuf->dst, &pBuf->size);
    }

    real_redraw_icon(pBuf);

    /* ===== */
    /* ID_CITIES */
    pBuf = pBuf->prev;

    if (!pBuf->gfx) {
      pBuf->gfx = crop_rect_from_surface(pBuf->dst, &pBuf->size);
    }

    real_redraw_icon(pBuf);

    /* ===== */
    /* ID_UNITS */
    pBuf = pBuf->prev;
    if((get_wflags(pBuf) & WF_HIDDEN) != WF_HIDDEN) {
      if (!pBuf->gfx) {
        pBuf->gfx = crop_rect_from_surface(pBuf->dst, &pBuf->size);
      }

      real_redraw_icon(pBuf);
    }
    
    /* ===== */
    /* ID_CHATLINE_TOGGLE_LOG_WINDOW_BUTTON */
    pBuf = pBuf->prev;
    if((get_wflags(pBuf) & WF_HIDDEN) != WF_HIDDEN) {
      if (!pBuf->gfx) {
        pBuf->gfx = crop_rect_from_surface(pBuf->dst, &pBuf->size);
      }

      real_redraw_icon(pBuf);
    }
    
    /* ===== */
    
    /* Toggle minimap mode */
    pBuf = pBuf->prev;
    if((get_wflags(pBuf) & WF_HIDDEN) != WF_HIDDEN) {
      if (!pBuf->gfx) {
        pBuf->gfx = crop_rect_from_surface(pBuf->dst, &pBuf->size);
      }

      real_redraw_icon(pBuf);
    }
    
    /* ===== */
    /* ID_TOGGLE_MAP_WINDOW_BUTTON */
    pBuf = pBuf->prev;

    if (!pBuf->gfx && pBuf->theme) {
      pBuf->gfx = crop_rect_from_surface(pBuf->dst, &pBuf->size);
    }

    real_redraw_icon(pBuf);

    sdl_dirty_rect(pMMap->size);
    
  } else {/* map hiden */

#if 0
    src.y = 0;
    src.h = pMMap->theme->h;
    src.x = pMMap->theme->w - pMMap->size.w;
    src.w = pMMap->size.w;
    map_area.w = pMMap->theme->w;
    map_area.h = pMMap->theme->h;
    
    /* clear area under old map window */
    SDL_FillRect(pMMap->dst, &map_area , 0x0);
    
    /*map_area.x = 0;
    map_area.y = Main.gui->h - pMMap->size.h;*/
    SDL_BlitSurface(pMMap->theme, &src, pMMap->dst, &map_area);
    
    
    SDL_Surface *pBuf_Surf = 
    	ResizeSurface(pTheme->FR_Vert, pTheme->FR_Vert->w,
				pMMap->size.h - DOUBLE_FRAME_WH + 2, 1);

    map_area.y += 2;
    SDL_BlitSurface(pBuf_Surf, NULL , pMMap->dst, &map_area);
    FREESURFACE(pBuf_Surf);

    /* ID_NEW_TURN */
    real_redraw_icon(pBuf);

    /* ID_CHATLINE_TOGGLE_LOG_WINDOW_BUTTON */
    pBuf = pBuf->prev;
    real_redraw_icon(pBuf);

    /* ID_FIND_CITY */
    pBuf = pBuf->prev;
    real_redraw_icon(pBuf);

    /* ID_TOGGLE_MAP_WINDOW_BUTTON */
    pBuf = pBuf->prev;
    real_redraw_icon(pBuf);


    /*add_refresh_rect(pMMap->size);*/
#endif
  }
  
}

/* ===================================================================== */
/* ===================================================================== */
/* ===================================================================== */


/**************************************************************************
  Draw City's surfaces to 'pDest' on position 'map_x' , 'map_y'.
**************************************************************************/
static void put_city_pixmap_draw(struct city *pCity, SDL_Surface *pDest,
				 Sint16 map_x, Sint16 map_y)
{
  static struct drawn_sprite pSprites[10];
  static SDL_Rect src, des;
  static int i, count;
    
  if(!pCity) {
    return;
  }
    
  count = fill_city_sprite_array_iso(pSprites, pCity);
  
  if (!(SDL_Client_Flags & CF_CIV3_CITY_TEXT_STYLE))
  {
    src = get_smaller_surface_rect(GET_SURF(pSprites[0].sprite));
    des.x = map_x + (NORMAL_TILE_WIDTH - src.w) / 2;
    des.y = map_y + NORMAL_TILE_HEIGHT / 4;
      
    /* blit flag/shield */
    SDL_BlitSurface(GET_SURF(pSprites[0].sprite), &src, pDest, &des);
  }
  
  if (pCity->owner == game.player_idx && cma_is_city_under_agent(pCity, NULL)) {
    draw_icon_from_theme(pTheme->CMA_Icon, 0, pDest,
    		map_x + (NORMAL_TILE_WIDTH - pTheme->CMA_Icon->w / 2) / 4,
    		map_y + NORMAL_TILE_HEIGHT - pTheme->CMA_Icon->h);
  }
  
  des.x = map_x;
  des.y = map_y;
  src = des;
  for (i = 1; i < count; i++) {
    if (pSprites[i].sprite) {
#if 0      
      if(GET_SURF(pSprites[i].sprite)->w - NORMAL_TILE_WIDTH > 0) {
	des.x -= ((GET_SURF(pSprites[i].sprite)->w - NORMAL_TILE_WIDTH) >> 1);
      }
#endif      
      SDL_BlitSurface(GET_SURF(pSprites[i].sprite), NULL, pDest, &des);
      des = src;	    
    }
  }
    
}

/**************************************************************************
  Draw Unit's surfaces to 'pDest' on position 'map_x' , 'map_y'.
**************************************************************************/
void put_unit_pixmap_draw(struct unit *pUnit, SDL_Surface *pDest,
			  Sint16 map_x, Sint16 map_y)
{
  static struct drawn_sprite pSprites[10];
  static SDL_Rect copy, des;
  static SDL_Rect src_hp = {0,0,0,0};
  static SDL_Rect src_flag = {0,0,0,0};
  static int count, i;
  bool solid_bg;

  if(!pUnit) {
    return;
  }
  
  des.x = map_x;
  des.y = map_y;
  
  count = fill_unit_sprite_array(pSprites, pUnit, &solid_bg);

  des.x += NORMAL_TILE_WIDTH / 4;

  /* blit hp bar */
  if (!src_hp.x)
  {
    src_hp = get_smaller_surface_rect(GET_SURF(pSprites[count - 1].sprite));
  }
  copy = des;
  SDL_BlitSurface(GET_SURF(pSprites[count - 1].sprite), &src_hp, pDest, &des);

  des = copy;
  des.y += src_hp.h;
  des.x++;
  
  /* blit flag/shield */
  if (!src_flag.x)
  {
    src_flag = get_smaller_surface_rect(GET_SURF(pSprites[0].sprite));
  }
  
  SDL_BlitSurface(GET_SURF(pSprites[0].sprite), &src_flag, pDest, &des);

  des.x = map_x;
  des.y = map_y;
  copy = des;    
  for (i = 1; i < count - 1; i++) {
    if (pSprites[i].sprite) {
      SDL_BlitSurface(GET_SURF(pSprites[i].sprite), NULL, pDest, &des);
      des = copy;	    
    }
  }
  
  /* draw current occupy status */
  if (pUnit->owner == game.player_idx
      && get_transporter_occupancy(pUnit) > 0) {
    
    des.y += NORMAL_TILE_HEIGHT / 2;
    copy = des;
    
    if (pUnit->occupy >= 10) {
      SDL_BlitSurface(GET_SURF(sprites.city.size_tens[pUnit->occupy / 10]),
	      NULL, pDest, &des);
      des = copy;
    }

    SDL_BlitSurface(GET_SURF(sprites.city.size[pUnit->occupy % 10]),
		    NULL, pDest, &des);

  }
  
}

/**************************************************************************
...
**************************************************************************/
static bool is_full_ocean(Terrain_type_id t, int x, int y)
{
  
  if (is_ocean(t))
  {
    Terrain_type_id ter;
    adjc_iterate(x, y, x1, y1) {
      ter = map_get_terrain(x1, y1);
      if (!is_ocean(ter) && ter != T_UNKNOWN) {
        return FALSE;
      }
    } adjc_iterate_end;
    return TRUE;
  }
  
  return FALSE;
}

/**************************************************************************
  This function draw 1x1 [map_col]x[map_row] map cell to 'pDest' surface
  on map_x , map_y position.
  Main Draw Map Function... speed of this function is critical !
**************************************************************************/
static void draw_map_cell(SDL_Surface *pDest, Sint16 map_x, Sint16 map_y,
			  Uint16 map_col, Uint16 map_row, int citymode)
{
  static struct drawn_sprite pTile_sprs[80];
  static struct Sprite *pCoasts[4] = {NULL, NULL, NULL, NULL};
  static SDL_Surface *pDitherBufs[4];
  static SDL_Surface *pBufSurface = NULL;
  static SDL_Rect dst, des;
  static struct tile *pTile = NULL;
  static struct city *pCity = NULL;
  static struct unit *pUnit = NULL, *pFocus = NULL;
  static enum tile_special_type special;
  static Terrain_type_id terrain;
  static int count, i;
  static bool fog, full_ocean, solid_bg;

  count =
  	fill_tile_sprite_array_iso(pTile_sprs, pCoasts, NULL, map_col,
				 map_row, citymode, &solid_bg);

  if (count == -1) { /* tile is unknown */
    des.x = map_x;
    des.y = map_y;
    SDL_BlitSurface(GET_SURF(sprites.black_tile), NULL, pDest, &des);
    return;
  }
  
  i = 0;
  pTile = map_get_tile(map_col, map_row);
  pCity = pTile->city;  
  special = pTile->special;
  terrain = pTile->terrain;
  fog = (draw_fog_of_war && (enum known_type)pTile->known == TILE_KNOWN_FOGGED);
  pUnit = get_drawable_unit(map_col, map_row, citymode);
  pFocus = get_unit_in_focus();
  full_ocean = is_full_ocean(terrain, map_col, map_row);

  if (!full_ocean && !is_ocean(terrain) && (SDL_Client_Flags & CF_DRAW_MAP_DITHER))
  { 
    fill_dither_buffers(pDitherBufs, map_col, map_row, terrain);
  }
  
  if (fog && !full_ocean) {
    des.x = 0;
    des.y = HALF_NORMAL_TILE_HEIGHT;
    pBufSurface = pTmpSurface;
  } else {
    des.x = map_x;
    des.y = map_y;
    pBufSurface = pDest;
  }

  dst = des;
  
  if (is_ocean(terrain)) {
    if (full_ocean) {
      if (fog) {
	SDL_BlitSurface(pOcean_Foged_Tile, NULL, pBufSurface, &des);
      } else {
	SDL_BlitSurface(pOcean_Tile, NULL, pBufSurface, &des);
      }
    } else {  /* coasts */
      /* top */
      des.x += NORMAL_TILE_WIDTH / 4;
      SDL_BlitSurface(GET_SURF(pCoasts[0]), NULL, pBufSurface, &des);
      des = dst;
      
      /* bottom */
      des.x += NORMAL_TILE_WIDTH / 4;
      des.y += HALF_NORMAL_TILE_HEIGHT;
      SDL_BlitSurface(GET_SURF(pCoasts[1]), NULL, pBufSurface, &des);
      des = dst;
      
      /* left */
      des.y += NORMAL_TILE_HEIGHT / 4;
      SDL_BlitSurface(GET_SURF(pCoasts[2]), NULL, pBufSurface, &des);
      des = dst;
      
      /* right */
      des.y += NORMAL_TILE_HEIGHT / 4;
      des.x += HALF_NORMAL_TILE_WIDTH;
      SDL_BlitSurface(GET_SURF(pCoasts[3]), NULL, pBufSurface, &des);
    }
  } else {
    SDL_BlitSurface(GET_SURF(pTile_sprs[0].sprite), NULL, pBufSurface, &des);
    i++;
  }

  des = dst;
  /*** Dither base terrain ***/

  if (!full_ocean && !is_ocean(terrain) && (SDL_Client_Flags & CF_DRAW_MAP_DITHER))
  {
    /* north */
    if (pDitherBufs[0]) {
      des.x += HALF_NORMAL_TILE_WIDTH;
      SDL_BlitSurface(pDitherBufs[0], NULL, pBufSurface, &des);
      des = dst;
    }
    /* south */
    if (pDitherBufs[1]) {
      des.y += HALF_NORMAL_TILE_HEIGHT;
      SDL_BlitSurface(pDitherBufs[1], NULL, pBufSurface, &des);
      des = dst;
    }
    /* east */
    if (pDitherBufs[2]) {
      des.y += HALF_NORMAL_TILE_HEIGHT;
      des.x += HALF_NORMAL_TILE_WIDTH;
      SDL_BlitSurface(pDitherBufs[2], NULL, pBufSurface, &des);
      des = dst;
    }
    /* west */
    if (pDitherBufs[3]) {
      SDL_BlitSurface(pDitherBufs[3], NULL, pBufSurface, &des);
      des = dst;
    }
  }  

    /*** Rest of terrain and specials ***/
  if (draw_terrain) {
    for (; i < count; i++) {
      if (pTile_sprs[i].sprite) {
        if (GET_SURF(pTile_sprs[i].sprite)->w - NORMAL_TILE_WIDTH > 0
	   || GET_SURF(pTile_sprs[i].sprite)->h - NORMAL_TILE_HEIGHT > 0) {
	  des.x -= ((GET_SURF(pTile_sprs[i].sprite)->w - NORMAL_TILE_WIDTH) / 2); /* center */
          /* this allow drawing of civ3 bigger tiles */
	  des.y -= (GET_SURF(pTile_sprs[i].sprite)->h - NORMAL_TILE_HEIGHT);
	  SDL_BlitSurface(GET_SURF(pTile_sprs[i].sprite), NULL, pBufSurface, &des);
        } else {
	  SDL_BlitSurface(GET_SURF(pTile_sprs[i].sprite), NULL, pBufSurface, &des);
        }
        des = dst;
      } else {
        freelog(LOG_ERROR, _("sprite is NULL"));
      }
    }
  }

    /*** Map grid ***/
  if (draw_map_grid) {
    int color1 = 0, color2 = 0, x = map_col, y = map_row;
    if((SDL_Client_Flags & CF_DRAW_CITY_GRID) == CF_DRAW_CITY_GRID) {
      enum city_tile_type city_tile_type = C_TILE_EMPTY,
      		city_tile_type1 = C_TILE_EMPTY, city_tile_type2 = C_TILE_EMPTY;
      struct city *dummy_pcity;
      bool is_in_city_radius =
            player_in_city_radius(game.player_ptr, map_col, map_row);
      bool pos1_is_in_city_radius = FALSE;
      bool pos2_is_in_city_radius = FALSE;

      if((SDL_Client_Flags & CF_DRAW_CITY_WORKER_GRID) == CF_DRAW_CITY_WORKER_GRID) {
        get_worker_on_map_position(x, y, &city_tile_type, &dummy_pcity);
      }
      
      x--;
      if (is_real_map_pos(x, y)) {
        normalize_map_pos(&x, &y);
        /*assert(is_tiles_adjacent(map_col, map_row, x, y));*/

        if ((enum known_type)(map_get_tile(x, y))->known != TILE_UNKNOWN) {
          pos1_is_in_city_radius = player_in_city_radius(game.player_ptr, x, y);
	  if((SDL_Client_Flags & CF_DRAW_CITY_WORKER_GRID) == CF_DRAW_CITY_WORKER_GRID) {
            get_worker_on_map_position(x, y, &city_tile_type1, &dummy_pcity);
	  }
	}
      } else {
        city_tile_type1 = C_TILE_UNAVAILABLE;
      }

      x = map_col;
      y = map_row - 1;
      if (is_real_map_pos(x, y)) {
        normalize_map_pos(&x, &y);
        /*assert(is_tiles_adjacent(map_col, map_row, x, y));*/

        if ((enum known_type)(map_get_tile(x, y))->known != TILE_UNKNOWN) {
          pos2_is_in_city_radius = player_in_city_radius(game.player_ptr, x, y);
	  if((SDL_Client_Flags & CF_DRAW_CITY_WORKER_GRID) == CF_DRAW_CITY_WORKER_GRID) {
            get_worker_on_map_position(x, y, &city_tile_type2, &dummy_pcity);
	  }
	}
      } else {
        city_tile_type2 = C_TILE_UNAVAILABLE;
      }
            
      if (is_in_city_radius || pos1_is_in_city_radius) {
        if (city_tile_type == C_TILE_WORKER || city_tile_type1 == C_TILE_WORKER) {
          color1 = 2;
        } else {
          color1 = 1;
        }
      }
      if (is_in_city_radius || pos2_is_in_city_radius) {	
	if (city_tile_type == C_TILE_WORKER || city_tile_type2 == C_TILE_WORKER) {
          color2 = 2;
        } else {
          color2 = 1;
        }
      }
    } 
    /* we draw buffers surfaces with 1 lines on top of the tile;
    the buttom lines will be drawn by the tiles underneath. */
    SDL_BlitSurface(pMapGrid[color1][0], NULL, pBufSurface, &des);
    des = dst;
    des.x += HALF_NORMAL_TILE_WIDTH;
    SDL_BlitSurface(pMapGrid[color2][1], NULL, pBufSurface, &des);
    des = dst;
  }

  /* Draw national borders */
  if (draw_borders && (game.borders != 0)) {
    struct tile *pBorder_Tile;
    struct player *this_owner = pTile->owner;
    int x1, y1;
  
    /* left side */
    if (this_owner && MAPSTEP(x1, y1, map_col, map_row, DIR8_WEST)
       && (pBorder_Tile = map_get_tile(x1, y1))
       && (this_owner != pBorder_Tile->owner)
       && pBorder_Tile->known) {
      SDL_BlitSurface(pMapBorders[this_owner->player_no][0], NULL, pBufSurface, &des);
      des = dst;
    }
    /* top side */
    if (this_owner && MAPSTEP(x1, y1, map_col, map_row, DIR8_NORTH)
       && (pBorder_Tile = map_get_tile(x1, y1))
       && (this_owner != pBorder_Tile->owner)
       && pBorder_Tile->known) {
      SDL_BlitSurface(pMapBorders[this_owner->player_no][1], NULL, pBufSurface, &des);
      des = dst;
    }
    /* right side */
    if (this_owner && MAPSTEP(x1, y1, map_col, map_row, DIR8_EAST)
       && (pBorder_Tile = map_get_tile(x1, y1))
       && (this_owner != pBorder_Tile->owner)
       && pBorder_Tile->known) {
      SDL_BlitSurface(pMapBorders[this_owner->player_no][2], NULL, pBufSurface, &des);
      des = dst;
    }
    /* bottom side */
    if (this_owner && MAPSTEP(x1, y1, map_col, map_row, DIR8_SOUTH)
       && (pBorder_Tile = map_get_tile(x1, y1))
       && (this_owner != pBorder_Tile->owner)
       && pBorder_Tile->known) {
      SDL_BlitSurface(pMapBorders[this_owner->player_no][3], NULL, pBufSurface, &des);
      des = dst;
    }
  }
  
  /*** City and various terrain improvements ***/
  if (sdl_contains_special(special, S_FORTRESS) && draw_fortress_airbase) {
    SDL_BlitSurface(GET_SURF(sprites.tx.fortress_back),
					    NULL, pBufSurface, &des);
    des = dst;
  }

  if (pCity && draw_cities) {
    put_city_pixmap_draw(pCity, pBufSurface,
			 des.x, des.y - HALF_NORMAL_TILE_HEIGHT);
  }

  if (sdl_contains_special(special, S_AIRBASE) && draw_fortress_airbase) {
    SDL_BlitSurface(GET_SURF(sprites.tx.airbase), NULL, pBufSurface, &des);
    des = dst;
  }

  if (sdl_contains_special(special, S_FALLOUT) && draw_pollution) {
    SDL_BlitSurface(GET_SURF(sprites.tx.fallout), NULL, pBufSurface, &des);
    des = dst;
  }

  if (sdl_contains_special(special, S_POLLUTION) && draw_pollution) {
    SDL_BlitSurface(GET_SURF(sprites.tx.pollution), NULL, pBufSurface, &des);
    des = dst;	  
  }

    /*** city size ***/
  /* Not fogged as it would be unreadable */
  if ((!(SDL_Client_Flags & CF_CIV3_CITY_TEXT_STYLE)
       || (!draw_city_productions && !draw_city_names)) &&
				     pCity && draw_cities) {
    if (pCity->size >= 10) {
      SDL_BlitSurface(GET_SURF(sprites.city.size_tens[pCity->size / 10]),
	      NULL, pBufSurface, &des);
      des = dst;	    
    }

    SDL_BlitSurface(GET_SURF(sprites.city.size[pCity->size % 10]),
		    NULL, pBufSurface, &des);
    des = dst;    
  }
  

    /*** Unit ***/
  if (pUnit && (draw_units || (pUnit == pFocus && draw_focus_unit))) {
    put_unit_pixmap_draw(pUnit, pBufSurface, des.x,
			 des.y - HALF_NORMAL_TILE_HEIGHT);

    if (!pCity && unit_list_size(&(pTile->units)) > 1) {
      des.y -= HALF_NORMAL_TILE_HEIGHT;
      SDL_BlitSurface(GET_SURF(sprites.unit.stack), NULL, pBufSurface, &des);
      des = dst;
    }
  }

  if (sdl_contains_special(special, S_FORTRESS) && draw_fortress_airbase) {
    SDL_BlitSurface(GET_SURF(sprites.tx.fortress), NULL, pBufSurface, &des);
    des = dst;	  
  }


  if (fog && !full_ocean)  {
    
    SDL_FillRectAlpha(pBufSurface, NULL,
			    get_game_colorRGB(COLOR_STD_FOG_OF_WAR));
    
    des.x = map_x;
    des.y = map_y - HALF_NORMAL_TILE_HEIGHT;
    SDL_BlitSurface(pBufSurface, NULL, pDest, &des);
      
    /* clear pBufSurface */
    /* if BytesPerPixel == 4 and Amask == 0 ( 24 bit coding ) alpha blit 
     * functions Set A = 255 in all pixels and then pixel val 
     * ( r=0, g=0, b=0) != 0. Real val of this pixel is 0xff000000/0x000000ff.*/
    if (pBufSurface->format->BytesPerPixel == 4
	&& !pBufSurface->format->Amask) {
      SDL_FillRect(pBufSurface, NULL, Amask);
    } else {
      SDL_FillRect(pBufSurface, NULL, 0x0);
    }

  }
  
}

static int frame = 0;
static bool reset_anim = FALSE;

/**************************************************************************
  Force rebuild sellecting unit animation frames
**************************************************************************/
void rebuild_focus_anim_frames(void)
{
  reset_anim = TRUE;
}

/**************************************************************************
  Draw sellecting unit animation
**************************************************************************/
void real_blink_active_unit(void)
{
  static int oldCol = 0, oldRow = 0, canvas_x, canvas_y, next_x, next_y;
  static struct unit *pPrevUnit = NULL, *pUnit = NULL;
  static SDL_Rect area, backup;
  static struct city *pCity;
  
  if (draw_units && (pUnit = get_unit_in_focus())) {
    if(tile_to_canvas_pos(&canvas_x, &canvas_y, pUnit->x, pUnit->y)) {
      area.x = canvas_x;
      area.y = canvas_y - HALF_NORMAL_TILE_HEIGHT;
      backup = area;
      if (!reset_anim
	 && pUnit == pPrevUnit && pUnit->type == pPrevUnit->type
         && pUnit->x == oldCol && pUnit->y == oldRow) {
        /* blit clear area */    
        SDL_BlitSurface(pBlinkSurfaceA, NULL, Main.map, &area);
	area = backup;
        /* blit frame of animation */
        area.y += HALF_NORMAL_TILE_HEIGHT;
        SDL_BlitSurface(pAnim->Focus[frame++], NULL, Main.map, &area);
        area = backup;
        /* blit unit graphic */
        SDL_BlitSurface(pBlinkSurfaceB, NULL, Main.map, &area);
     
        flush_mapcanvas(area.x, area.y, UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT);
          
      } else {
	
	if (reset_anim) {
	  bool un = draw_units, unf = draw_focus_unit;
	  
	  draw_units = FALSE;
	  draw_focus_unit = FALSE;
	  
	  draw_map_cell(Main.map, canvas_x, canvas_y, pUnit->x, pUnit->y, 0);
	  
	  draw_units = un;
	  draw_focus_unit = unf;
	}
	
        /* refresh clear area */
        area.w = UNIT_TILE_WIDTH;
        area.h = UNIT_TILE_HEIGHT;
        SDL_BlitSurface(Main.map, &area, pBlinkSurfaceA, NULL);
        area = backup;
	
        /* create unit graphic */
        SDL_FillRect(pBlinkSurfaceB, NULL, 0x0);
        put_unit_pixmap_draw(pUnit, pBlinkSurfaceB, 0, 0);
	  
	/* left unit */
	next_x = pUnit->x;
	next_y = pUnit->y + 1;
	if(normalize_map_pos(&next_x, &next_y)) {
	  if(draw_cities && (pCity = map_get_city(next_x, next_y))) {
	    put_city_pixmap_draw(pCity, pBlinkSurfaceB,
			  -HALF_NORMAL_TILE_WIDTH, HALF_NORMAL_TILE_HEIGHT);
	  } else {
	    put_unit_pixmap_draw(get_drawable_unit(next_x, next_y, 0),
				  pBlinkSurfaceB, -HALF_NORMAL_TILE_WIDTH,
						  HALF_NORMAL_TILE_HEIGHT);
	  }
	}
	  
	/* right unit */
	next_x = pUnit->x + 1;
	next_y = pUnit->y;
	if(normalize_map_pos(&next_x, &next_y)) {
	  if((pCity = map_get_city(next_x, next_y)) && draw_cities) {
	    put_city_pixmap_draw(pCity, pBlinkSurfaceB,
			  HALF_NORMAL_TILE_WIDTH, HALF_NORMAL_TILE_HEIGHT);
	  } else {
	    put_unit_pixmap_draw(get_drawable_unit(next_x, next_y, 0),
				  pBlinkSurfaceB, HALF_NORMAL_TILE_WIDTH,
						  HALF_NORMAL_TILE_HEIGHT);
	    if(!pCity &&
	       unit_list_size(&map_get_tile(next_x, next_y)->units) > 1) {
	       blit_entire_src(GET_SURF(sprites.unit.stack), pBlinkSurfaceB,
	    		HALF_NORMAL_TILE_WIDTH, HALF_NORMAL_TILE_HEIGHT);
	    }
	  }
	}
	  
	/* botton unit */
	next_x = pUnit->x + 1;
	next_y = pUnit->y + 1;
	if(normalize_map_pos(&next_x, &next_y)) {
	  if(draw_cities && (pCity = map_get_city(next_x, next_y))) {
	    put_city_pixmap_draw(pCity, pBlinkSurfaceB, 0, NORMAL_TILE_HEIGHT);
	  } else {
	    put_unit_pixmap_draw(get_drawable_unit(next_x, next_y, 0),
				  pBlinkSurfaceB, 0, NORMAL_TILE_HEIGHT);
	  }
	}
         
        /* draw focus animation frame */
        area.y += HALF_NORMAL_TILE_HEIGHT;
        SDL_BlitSurface(pAnim->Focus[frame++], NULL, Main.map, &area);
        area = backup;
		
        /* draw unit graphic */
        SDL_BlitSurface(pBlinkSurfaceB, NULL, Main.map, &area);
      
        flush_rect(area);
      
        pPrevUnit = pUnit;
        oldCol = pUnit->x;
        oldRow = pUnit->y;
	reset_anim = FALSE;
      }
    }
  }
  
  if(!(frame < pAnim->num_tiles_focused_unit)) {
    frame = 0;
  }
    
}

/**************************************************************************
 This function is called to decrease a unit's HP smoothly in battle when
 combat_animation is turned on.
**************************************************************************/
void decrease_unit_hp_smooth(struct unit *punit0, int hp0,
			     struct unit *punit1, int hp1)
{

  static struct timer *anim_timer = NULL;
  struct unit *losing_unit = (hp0 == 0 ? punit0 : punit1);
  int i, canvas_x, canvas_y;
  SDL_Rect src =
      { 0, 0, NORMAL_TILE_WIDTH, 1.5 * NORMAL_TILE_HEIGHT }, dest;

  set_units_in_combat(punit0, punit1);

  do {
    anim_timer = renew_timer_start(anim_timer, TIMER_USER, TIMER_ACTIVE);

    if (punit0->hp > hp0
	&& myrand((punit0->hp - hp0) + (punit1->hp - hp1)) <
	punit0->hp - hp0) {
      punit0->hp--;
    } else {
      if (punit1->hp > hp1) {
	punit1->hp--;
      } else {
	punit0->hp--;
      }
    }

    refresh_tile_mapcanvas(punit0->x, punit0->y, FALSE);
    refresh_tile_mapcanvas(punit1->x, punit1->y, FALSE);
    
    flush_dirty();

    usleep_since_timer_start(anim_timer, 10000);

  } while (punit0->hp > hp0 || punit1->hp > hp1);


  if (num_tiles_explode_unit &&
      tile_to_canvas_pos(&canvas_x, &canvas_y, losing_unit->x, losing_unit->y)) {
    /* copy screen area */
    src.x = canvas_x;
    src.y = canvas_y;
    SDL_BlitSurface(Main.map, &src, pTmpSurface, NULL);

    dest.y = canvas_y;
    for (i = 0; i < num_tiles_explode_unit; i++) {

      anim_timer = renew_timer_start(anim_timer, TIMER_USER, TIMER_ACTIVE);

      /* blit explosion */
      dest.x = canvas_x + NORMAL_TILE_WIDTH / 4;

      SDL_BlitSurface(GET_SURF(sprites.explode.unit[i]), 
      						NULL, Main.map, &dest);
      
      flush_rect(dest);

      usleep_since_timer_start(anim_timer, 40000);

      /* clear explosion */
      dest.x = canvas_x;
      SDL_BlitSurface(pTmpSurface, NULL, Main.map, &dest);
    }
  }

  sdl_dirty_rect(dest);

  /* clear pTmpSurface */
  if ((pTmpSurface->format->BytesPerPixel == 4) &&
      (!pTmpSurface->format->Amask)) {
    SDL_FillRect(pTmpSurface, NULL, Amask);
  } else {
    SDL_FillRect(pTmpSurface, NULL, 0x0);
  }

  set_units_in_combat(NULL, NULL);
  
  refresh_tile_mapcanvas(punit0->x, punit0->y, FALSE);
  refresh_tile_mapcanvas(punit1->x, punit1->y, FALSE);
  rebuild_focus_anim_frames();
  flush_dirty();
}

/**************************************************************************
  Update (refresh) the locations of the mapview scrollbars (if it uses
  them).
**************************************************************************/
void update_map_canvas_scrollbars(void)
{
  /* No scrollbars. */
}

/**************************************************************************
  Draw a cross-hair overlay on a tile.
**************************************************************************/
void put_cross_overlay_tile(int x, int y)
{
  freelog(LOG_DEBUG, "MAPVIEW: put_cross_overlay_tile : PORT ME");
}

/**************************************************************************
  Draw in information about city workers on the mapview in the given
  color.
**************************************************************************/
void put_city_workers(struct city *pcity, int color)
{
  freelog(LOG_DEBUG, "MAPVIEW: put_city_workers : PORT ME");
}

/**************************************************************************
  Draw a nuke mushroom cloud at the given tile.
**************************************************************************/
void put_nuke_mushroom_pixmaps(int x, int y)
{
  int canvas_x, canvas_y;
      
  if (pAnim->num_tiles_explode_nuke &&
     tile_to_canvas_pos(&canvas_x, &canvas_y, x, y)) {
    struct Sprite *pNuke;
    SDL_Surface *pStore;
    struct timer *anim_timer = NULL;
    SDL_Rect src, dst;
    char tag[32];
    int i;
    
    my_snprintf(tag , sizeof(tag), "explode.iso_nuke_0");
    pNuke = load_sprite(tag);
    assert(pNuke != NULL);   
    /* copy screen area */
    src.w = GET_SURF(pNuke)->w;
    src.h = GET_SURF(pNuke)->h;
    src.x = canvas_x + (NORMAL_TILE_WIDTH - src.w) / 2;
    src.y = canvas_y + (NORMAL_TILE_HEIGHT - src.h) / 2 - 24;/* hard coded y position !! */
    dst = src;
       
    pStore = create_surf(src.w, src.h, SDL_SWSURFACE);
    SDL_BlitSurface(Main.map, &src, pStore, NULL);
    src = dst;
       
    /* draw nuke explosion animations */
    for (i = 0; i < pAnim->num_tiles_explode_nuke; i++) {

      if(!pNuke) {
       my_snprintf(tag , sizeof(tag), "explode.iso_nuke_%d", i);
        pNuke = load_sprite(tag);
      }
      assert(pNuke != NULL);
      
      SDL_SetColorKey(GET_SURF(pNuke), SDL_SRCCOLORKEY,
			    		get_first_pixel(GET_SURF(pNuke)));
      
      anim_timer = renew_timer_start(anim_timer, TIMER_USER, TIMER_ACTIVE);

      /* blit explosion */
      SDL_BlitSurface(GET_SURF(pNuke), NULL, Main.map, &dst);
      
      flush_rect(dst);
      dst = src;
      
      usleep_since_timer_start(anim_timer, 40000);

      /* clear explosion */
      SDL_BlitSurface(pStore, NULL, Main.map, &dst);
      dst = src;
      
      unload_sprite(tag);
      pNuke = NULL;
    }
    
    FREESURFACE(pStore);
    finish_loading_sprites();
  }
  
}

/**************************************************************************
  Draw a segment (e.g., a goto line) from the given tile in the given
  direction.
**************************************************************************/
void draw_segment(int src_x, int src_y, int dir)
{
  int dest_x, dest_y, is_real;
  int canvas_start_x, canvas_start_y;
  int canvas_end_x, canvas_end_y;
  Uint32 color = get_game_color(COLOR_STD_CYAN, Main.map);
  
  is_real = MAPSTEP(dest_x, dest_y, src_x, src_y, dir);
  assert(is_real);
  
  /* Find middle of tiles. y-1 to not undraw the the middle pixel of a
     horizontal line when we refresh the tile below-between. */
  tile_to_canvas_pos(&canvas_start_x, &canvas_start_y, src_x, src_y);
  tile_to_canvas_pos(&canvas_end_x, &canvas_end_y, dest_x, dest_y);
  canvas_start_x += HALF_NORMAL_TILE_WIDTH;
  canvas_start_y += HALF_NORMAL_TILE_HEIGHT - 1;
  canvas_end_x += HALF_NORMAL_TILE_WIDTH;
  canvas_end_y += HALF_NORMAL_TILE_HEIGHT - 1;
    
  dest_x = abs(canvas_end_x - canvas_start_x);
  dest_y = abs(canvas_end_y - canvas_start_y);
  
  /* somewhat hackish way of solving the problem where draw from a tile on
     one side of the screen out of the screen, and the tile we draw to is
     found to be on the other side of the screen. */
  if (dest_x > NORMAL_TILE_WIDTH || dest_y > NORMAL_TILE_HEIGHT) {
    return;
  }

  /* draw it! */
  putline(Main.map,
  	canvas_start_x,	canvas_start_y,	canvas_end_x, canvas_end_y, color);
  
  dirty_rect(MIN(canvas_start_x, canvas_end_x),
		  MIN(canvas_start_y, canvas_end_y),
  			dest_x ? dest_x : 1, dest_y ? dest_y : 1);
  
  
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
   * dialogs usually need to be resized). */
  
  free_dither_tiles();
  init_dither_tiles();
  
}

/* =====================================================================
				City MAP
   ===================================================================== */

/**************************************************************************
  normalize_map_pos + (y) corrections.  This function must go to drink!
**************************************************************************/
int correction_map_pos(int *pCol, int *pRow)
{
  int iRet = 0;

  /* correction ... pCol and pRow must be in map :) */
  if (*pCol < 0) {
    *pCol += map.xsize;
    iRet = 1;
  } else {
    if (*pCol >= map.xsize) {
      *pCol -= map.xsize;
      iRet = 1;
    }
  }

  if (*pRow < 0) {
    *pRow += map.ysize;
    iRet = 1;
  } else if (*pRow >= map.ysize) {
    *pRow -= map.ysize;
    iRet = 1;
  }

  return iRet;
}


/**************************************************************************
  ...
**************************************************************************/
static SDL_Surface * create_iso_city_map(struct city *pCity)
{
#if 0
  register Uint16 col = 0, row;
  SDL_Rect dest;
  int draw_units_backup = draw_units;
  int draw_map_grid_backup = draw_map_grid;
  int sx, sy, row0, real_col = pCity->x, real_row = pCity->y;

  Sint16 x0 = 3 * NORMAL_TILE_WIDTH / 2;
  Sint16 y0 = 0;
  SDL_Surface *pTile = NULL;
  SDL_Surface *pDest = create_surf(get_citydlg_canvas_width(),
				   get_citydlg_canvas_height()
				  + HALF_NORMAL_TILE_HEIGHT, SDL_SWSURFACE);
  real_col -= 2;
  real_row -= 2;
  correction_map_pos((int *) &real_col, (int *) &real_row);
  row0 = real_row;

  /* turn off drawing units and map_grid */
  draw_units = 0;
  draw_map_grid = 0;

  /* draw loop */
  for (; col < CITY_MAP_SIZE; col++) {
    for (row = 0; row < CITY_MAP_SIZE; row++) {
#if 1
       sx = x0 + (col - row) * HALF_NORMAL_TILE_WIDTH;
       sy = y0 + (row + col) * HALF_NORMAL_TILE_HEIGHT;
        
#else
      /* ugly hack for speed.  Assumes:
       *   NORMAL_TILE_WIDTH == 64
       *   NORMAL_TILE_HEIGHT == 32  */
      sx = x0 + ((col - row) << 5);
      sy = y0 + ((row + col) << 4);
#endif
      if (sx > -NORMAL_TILE_WIDTH && sx < pDest->w + NORMAL_TILE_WIDTH
	  && sy > -NORMAL_TILE_HEIGHT
	  && sy < pDest->h + NORMAL_TILE_HEIGHT) {
	dest.x = sx;
	dest.y = sy;
	if ((!col && !row) ||
	    (!col && row == CITY_MAP_SIZE - 1) ||
	    (!row && col == CITY_MAP_SIZE - 1) ||
	    (col == CITY_MAP_SIZE - 1 && row == CITY_MAP_SIZE - 1)) {
	  /* draw black corners */

	  SDL_BlitSurface(GET_SURF(sprites.black_tile),
			  NULL, pDest, &dest);
	} else {
	  /* draw map cell */
	  draw_map_cell(pDest, dest.x, dest.y, real_col, real_row, 1);
	  if (get_worker_city(pCity, col, row) == C_TILE_UNAVAILABLE &&
	      map_get_terrain(real_col, real_row) != T_UNKNOWN) {
	    if (!pTile) {
	      
	      /* fill pTile with Mask Color */
	      SDL_FillRect(pTmpSurface, NULL,
			   SDL_MapRGB(pTmpSurface->format, 255, 0, 255));
	      
	      /* make mask */
	      SDL_BlitSurface(GET_SURF(sprites.black_tile),
			      NULL, pTmpSurface, NULL);

	      SDL_SetColorKey(pTmpSurface, SDL_SRCCOLORKEY,
			      getpixel(pTmpSurface, HALF_NORMAL_TILE_WIDTH,
				       HALF_NORMAL_TILE_HEIGHT));
	      /* create pTile */
	      pTile = create_surf(NORMAL_TILE_WIDTH,
				  NORMAL_TILE_HEIGHT, SDL_SWSURFACE);
	      
	      /* fill pTile with RED */
	      SDL_FillRect(pTile, NULL,
			   SDL_MapRGBA(pTile->format, 255, 0, 0, 96));

	      /* blit mask */
	      SDL_BlitSurface(pTmpSurface, NULL, pTile, NULL);

	      SDL_SetColorKey(pTile, SDL_SRCCOLORKEY | SDL_RLEACCEL,
			      get_first_pixel(pTile));

	      SDL_SetAlpha(pTile, SDL_SRCALPHA, 96);

	      /* clear pTmpSurface */
	      if (pTmpSurface->format->BytesPerPixel == 4 &&
		  !pTmpSurface->format->Amask) {
		SDL_SetColorKey(pTmpSurface, SDL_SRCCOLORKEY, Amask);
		SDL_FillRect(pTmpSurface, NULL, Amask);
	      } else {
		SDL_SetColorKey(pTmpSurface, SDL_SRCCOLORKEY, 0x0);
		SDL_FillRect(pTmpSurface, NULL, 0);
	      }
	    }
	    SDL_BlitSurface(pTile, NULL, pDest, &dest);
	  }
	}

      }
      if (++real_row >= map.ysize) {
        real_row = 0;
      }      
    }
    real_row = row0;
    if (++real_col >= map.xsize) {
      real_col = 0;
    }
  }

  draw_units = draw_units_backup;
  draw_map_grid = draw_map_grid_backup;

  FREESURFACE(pTile);

#endif  
  
  int city_x, city_y;
  int map_x, map_y, canvas_x, canvas_y;
  int draw_units_backup = draw_units;
  SDL_Surface *pTile = NULL;
  SDL_Surface *pDest = create_surf(get_citydlg_canvas_width(),
		get_citydlg_canvas_height()
	    + HALF_NORMAL_TILE_HEIGHT, SDL_SWSURFACE);

    
  /* turn off drawing units */
  draw_units = 0;
  
  /* We have to draw the tiles in a particular order, so its best
     to avoid using any iterator macro. */
  for (city_x = 0; city_x<CITY_MAP_SIZE; city_x++)
  {
    for (city_y = 0; city_y<CITY_MAP_SIZE; city_y++) {
        if (is_valid_city_coords(city_x, city_y)
	  && city_map_to_map(&map_x, &map_y, pCity, city_x, city_y)
	  && tile_get_known(map_x, map_y)
	  && city_to_canvas_pos(&canvas_x, &canvas_y, city_x, city_y)) {
	  draw_map_cell(pDest, canvas_x,
		  canvas_y + HALF_NORMAL_TILE_HEIGHT, map_x, map_y, 1);
	
	if (get_worker_city(pCity, city_x, city_y) == C_TILE_UNAVAILABLE)
	{
	    
	    SDL_Rect dest = {canvas_x, canvas_y + HALF_NORMAL_TILE_HEIGHT, NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT};
	    if (!pTile) {
	      
	      /* fill pTile with Mask Color */
	      SDL_FillRect(pTmpSurface, NULL,
			   SDL_MapRGB(pTmpSurface->format, 255, 0, 255));
	      
	      /* make mask */
	      SDL_BlitSurface(GET_SURF(sprites.black_tile),
			      NULL, pTmpSurface, NULL);

	      SDL_SetColorKey(pTmpSurface, SDL_SRCCOLORKEY,
			      getpixel(pTmpSurface, HALF_NORMAL_TILE_WIDTH,
				       HALF_NORMAL_TILE_HEIGHT));
	      /* create pTile */
	      pTile = create_surf(NORMAL_TILE_WIDTH,
				  NORMAL_TILE_HEIGHT, SDL_SWSURFACE);
	      
	      /* fill pTile with RED */
	      SDL_FillRect(pTile, NULL,
			   SDL_MapRGBA(pTile->format, 255, 0, 0, 96));

	      /* blit mask */
	      SDL_BlitSurface(pTmpSurface, NULL, pTile, NULL);

	      SDL_SetColorKey(pTile, SDL_SRCCOLORKEY | SDL_RLEACCEL,
			      get_first_pixel(pTile));

	      SDL_SetAlpha(pTile, SDL_SRCALPHA, 96);

	      /* clear pTmpSurface */
	      if (pTmpSurface->format->BytesPerPixel == 4 &&
		  !pTmpSurface->format->Amask) {
		SDL_SetColorKey(pTmpSurface, SDL_SRCCOLORKEY, Amask);
		SDL_FillRect(pTmpSurface, NULL, Amask);
	      } else {
		SDL_SetColorKey(pTmpSurface, SDL_SRCCOLORKEY, 0x0);
		SDL_FillRect(pTmpSurface, NULL, 0);
	      }
	    }
	    SDL_BlitSurface(pTile, NULL, pDest, &dest);
	} 
      }
    }
  }
  draw_units = draw_units_backup;
  FREESURFACE(pTile);
  
  return pDest;
}

/**************************************************************************
  ...
**************************************************************************/
static SDL_Surface * create_noiso_city_map(struct city *pCity)
{
  
  int city_x, city_y;
  int map_x, map_y, canvas_x, canvas_y;
  int draw_units_backup = draw_units;
  struct canvas store;
  SDL_Surface *pDest = create_surf(get_citydlg_canvas_width(),
				   get_citydlg_canvas_height(), SDL_SWSURFACE);

  store.map = pDest;
  
  /* turn off drawing units */
  draw_units = 0;
  
  /* We have to draw the tiles in a particular order, so its best
     to avoid using any iterator macro. */
  for (city_x = 0; city_x<CITY_MAP_SIZE; city_x++)
  {
    for (city_y = 0; city_y<CITY_MAP_SIZE; city_y++) {
        if (is_valid_city_coords(city_x, city_y)
	  && city_map_to_map(&map_x, &map_y, pCity, city_x, city_y)
	  && tile_get_known(map_x, map_y)
	  && city_to_canvas_pos(&canvas_x, &canvas_y, city_x, city_y)) {
	put_one_tile(&store, map_x, map_y,  canvas_x, canvas_y, 1);
	if (get_worker_city(pCity, city_x, city_y) == C_TILE_UNAVAILABLE)
	{
	    SDL_Color used = {255, 0, 0, 96};
	    SDL_Rect dest = {canvas_x, canvas_y, NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT};
	    SDL_FillRectAlpha(pDest, &dest, &used);
	} 
      }
    }
  }
  draw_units = draw_units_backup;
  
  return pDest;
}



SDL_Surface * create_city_map(struct city *pCity)
{
  
    if (is_isometric)
    {
      return create_iso_city_map(pCity);
    }
    
    return create_noiso_city_map(pCity);
}

/**************************************************************************
  ...
**************************************************************************/
SDL_Surface * get_terrain_surface(int x , int y)
{
  SDL_Surface *pSurf = create_surf(UNIT_TILE_WIDTH,
			           UNIT_TILE_HEIGHT,
			           SDL_SWSURFACE);
  bool fg = draw_fog_of_war,
     ci = draw_cities ,
     tr = draw_terrain ,
     rr = draw_roads_rails,
     ir = draw_irrigation,
     un = draw_units,
     gr = draw_map_grid,
     pl = draw_pollution ,
     fa = draw_fortress_airbase,
     mi = draw_mines,
     bo = draw_borders;
  
  draw_fog_of_war = FALSE;
  draw_cities = FALSE;
  draw_terrain = TRUE;
  draw_roads_rails = FALSE;
  draw_irrigation = FALSE;
  draw_units = FALSE;
  draw_map_grid = FALSE;
  draw_pollution = FALSE;
  draw_fortress_airbase = FALSE;
  draw_mines = FALSE;
  draw_borders = FALSE;
  
  SDL_Client_Flags &= ~CF_DRAW_MAP_DITHER;
  
  draw_map_cell(pSurf, 0, NORMAL_TILE_HEIGHT / 2, x , y , 0);
    
  SDL_SetColorKey(pSurf , SDL_SRCCOLORKEY|SDL_RLEACCEL , 0x0);
  
  SDL_Client_Flags |= CF_DRAW_MAP_DITHER;
  
  draw_fog_of_war = fg;
  draw_cities = ci;
  draw_terrain = tr;
  draw_roads_rails = rr;
  draw_irrigation = ir;
  draw_units = un;
  draw_map_grid = gr;
  draw_pollution = pl;
  draw_fortress_airbase = fa;
  draw_mines = mi;
  draw_borders = bo;
  
  return pSurf;
}

/* =====================================================================
				Init Functions
   ===================================================================== */

/**************************************************************************
  ...
**************************************************************************/
static SDL_Surface * create_ocean_tile(void)
{
  SDL_Surface *pOcean = create_surf(NORMAL_TILE_WIDTH ,
			  	     NORMAL_TILE_HEIGHT ,
			    	     SDL_SWSURFACE);
  SDL_Rect des = { 0 , 0 , 0, 0 };
  SDL_Surface *pBuf[4];
  
  pBuf[0] = GET_SURF(sprites.tx.coast_cape_iso[0][0]);
  pBuf[1] = GET_SURF(sprites.tx.coast_cape_iso[0][1]);
  pBuf[2] = GET_SURF(sprites.tx.coast_cape_iso[0][2]);
  pBuf[3] = GET_SURF(sprites.tx.coast_cape_iso[0][3]);
  
  /* top */
  des.y = 0;
  des.x = NORMAL_TILE_WIDTH / 4;
  SDL_BlitSurface(pBuf[0], NULL, pOcean, &des);

  /* bottom */
  des.y = NORMAL_TILE_HEIGHT / 2;
  SDL_BlitSurface(pBuf[1], NULL, pOcean, &des);

  /* left */
  des.x = 0;
  des.y = NORMAL_TILE_HEIGHT / 4;
  SDL_BlitSurface(pBuf[2], NULL, pOcean, &des);

  /* right */
  des.x = NORMAL_TILE_WIDTH / 2;
  SDL_BlitSurface(pBuf[3], NULL, pOcean, &des);
  
  SDL_SetColorKey(pOcean, SDL_SRCCOLORKEY|SDL_RLEACCEL, 0x0);
  
  return pOcean;
}

/**************************************************************************
  ...
**************************************************************************/
static void fill_dither_buffers(SDL_Surface **pDitherBufs, int x, int y,
					Terrain_type_id terrain)
{
  Terrain_type_id __ter[8];
  enum direction8 dir;
  int x1, y1;
  
  /* Loop over all adjacent tiles.  We should have an iterator for this. */
  for (dir = 0; dir < 8; dir++) {
  
    if (MAPSTEP(x1, y1, x, y, dir)) {
      __ter[dir] = map_get_terrain(x1, y1);
      
      /* hacking away the river here... */
      if (is_isometric && __ter[dir] == T_RIVER) {
	__ter[dir] = T_GRASSLAND;
      }
    } else {
      /* We draw the edges of the map as if the same terrain just
       * continued off the edge of the map. */
      
      __ter[dir] = terrain;
    }
  }
  
  /* north */
  pDitherBufs[0] = pDithers[__ter[DIR8_NORTH]][0];
  
  /* south */
  pDitherBufs[1] = pDithers[__ter[DIR8_SOUTH]][1];
  
  /* east */
  pDitherBufs[2] = pDithers[__ter[DIR8_EAST]][2];
  
  /* weast */
  pDitherBufs[3] = pDithers[__ter[DIR8_WEST]][3];
}

/**************************************************************************
  ...
**************************************************************************/
static void init_dither_tiles(void)
{
  Terrain_type_id terrain;
  int i , w = NORMAL_TILE_WIDTH / 2, h = NORMAL_TILE_HEIGHT / 2;
  SDL_Rect src = { 0 , 0 , w , h };
  SDL_Surface *pTerrain_Surface, *pBuf = create_surf(NORMAL_TILE_WIDTH,
  					NORMAL_TILE_HEIGHT, SDL_SWSURFACE);
  Uint32 color = SDL_MapRGB(pBuf->format, 255, 0, 255);
  SDL_FillRect(pBuf, NULL, color);
  SDL_SetColorKey(pBuf, SDL_SRCCOLORKEY, color);
  
  for (terrain = T_FIRST; terrain < T_COUNT; terrain++) {
    
    if (terrain == T_RIVER || is_ocean(terrain) || terrain == T_UNKNOWN)
    {
      for(i = 0; i < 4; i++)
      {
        pDithers[terrain][i] = NULL;
      }
      continue;
    }
      
    pTerrain_Surface = GET_SURF(get_tile_type(terrain)->sprite[0]);
    
    for( i = 0; i < 4; i++ )
    {
      pDithers[terrain][i] = create_surf(w, h , SDL_SWSURFACE);
      SDL_FillRect(pDithers[terrain][i], NULL, color);
    }
  
    /* north */
    dither_north(pTerrain_Surface, pDitherMask, pBuf);
    
    /* south */
    dither_south(pTerrain_Surface, pDitherMask, pBuf);
    
    /* east */
    dither_east(pTerrain_Surface, pDitherMask, pBuf);

    /* west */
    dither_west(pTerrain_Surface, pDitherMask, pBuf);
    
    /* north */
    src.x = w;
    SDL_BlitSurface(pBuf, &src, pDithers[terrain][0], NULL);
    
    /* south */
    src.x = 0;
    src.y = h;
    SDL_BlitSurface(pBuf, &src, pDithers[terrain][1], NULL);
    
    /* east */
    src.x = w;
    src.y = h;
    SDL_BlitSurface(pBuf, &src, pDithers[terrain][2], NULL);
    
    /* west */
    src.x = 0;
    src.y = 0;
    SDL_BlitSurface(pBuf, &src, pDithers[terrain][3], NULL);
    
    SDL_FillRect(pBuf, NULL, color);
    
    for(i = 0; i < 4; i++)
    {
      SDL_SetColorKey(pDithers[terrain][i] , SDL_SRCCOLORKEY|SDL_RLEACCEL, color);
    }
    
  }
  
  FREESURFACE(pBuf);
}

/**************************************************************************
  ...
**************************************************************************/
static void free_dither_tiles(void)
{
  Terrain_type_id terrain;
  int i;
    
  for (terrain = T_FIRST; terrain < T_COUNT; terrain++) {
    for(i = 0; i < 4; i++)
    {
      FREESURFACE(pDithers[terrain][i]);
    }
  }
  
}

/**************************************************************************
  ...
**************************************************************************/
static void clear_dither_tiles(void)
{
  Terrain_type_id terrain;
  int i;

  for (terrain = T_FIRST; terrain < T_COUNT; terrain++) {
    for(i = 0; i < 4; i++)
    {
      pDithers[terrain][i] = NULL;
    }
  }
}
/* ================================================================ */

/**************************************************************************
  ...
**************************************************************************/
static void init_borders_tiles(void)
{
  int i;
  SDL_Color *color;
  
  pMapBorders = CALLOC(game.nplayers + 1, sizeof(SDL_Surface **));
  for(i=0; i<game.nplayers; i++) {
    
    color = get_game_colorRGB(COLOR_STD_RACE0 + i);
    color->unused = 192;
    
    pMapBorders[i] = CALLOC(4, sizeof(SDL_Surface *));
    
    pMapBorders[i][0] = SDL_DisplayFormat(pTheme->NWEST_BORDER_Icon);
    SDL_FillRectAlpha(pMapBorders[i][0], NULL, color);
    SDL_SetColorKey(pMapBorders[i][0], SDL_SRCCOLORKEY|SDL_RLEACCEL,
    					get_first_pixel(pMapBorders[i][0]));
    
    pMapBorders[i][1] = SDL_DisplayFormat(pTheme->NNORTH_BORDER_Icon);
    SDL_FillRectAlpha(pMapBorders[i][1], NULL, color);
    SDL_SetColorKey(pMapBorders[i][1], SDL_SRCCOLORKEY|SDL_RLEACCEL,
    					get_first_pixel(pMapBorders[i][1]));
    
    pMapBorders[i][2] = SDL_DisplayFormat(pTheme->NEAST_BORDER_Icon);
    SDL_FillRectAlpha(pMapBorders[i][2], NULL, color);
    SDL_SetColorKey(pMapBorders[i][2], SDL_SRCCOLORKEY|SDL_RLEACCEL,
					get_first_pixel(pMapBorders[i][2]));
        
    pMapBorders[i][3] = SDL_DisplayFormat(pTheme->NSOUTH_BORDER_Icon);
    SDL_FillRectAlpha(pMapBorders[i][3], NULL, color);
    SDL_SetColorKey(pMapBorders[i][3], SDL_SRCCOLORKEY|SDL_RLEACCEL,
    					get_first_pixel(pMapBorders[i][3]));
            
    color->unused = 255;
  }
  
}

/**************************************************************************
  ...
**************************************************************************/
static void free_borders_tiles(void)
{
  if(pMapBorders) {
    int i = 0;
    while(pMapBorders[i]) {
      FREESURFACE(pMapBorders[i][0]);
      FREESURFACE(pMapBorders[i][1]);
      FREESURFACE(pMapBorders[i][2]);
      FREESURFACE(pMapBorders[i][3]);
      FREE(pMapBorders[i]);
      i++;
    }
    FREE(pMapBorders);
  }
}

/**************************************************************************
  ...
**************************************************************************/
void tmp_map_surfaces_init(void)
{

  HALF_NORMAL_TILE_HEIGHT = NORMAL_TILE_HEIGHT / 2;
  HALF_NORMAL_TILE_WIDTH = NORMAL_TILE_WIDTH / 2;
  
  #if SDL_BYTEORDER == SDL_BIG_ENDIAN
    Amask = 0x000000ff;
  #else
    Amask = 0xff000000;
  #endif
  
  clear_dither_tiles();
  
  if(is_isometric) {
    
    /* =========================== */
    pTmpSurface = create_surf(UNIT_TILE_WIDTH,
			    UNIT_TILE_HEIGHT,
			    SDL_SWSURFACE);
    SDL_SetAlpha(pTmpSurface, 0x0, 0x0);

    /* if BytesPerPixel == 4 and Amask == 0 ( 24 bit coding ) alpha blit
     * functions; Set A = 255 in all pixels and then pixel val
     *  ( r=0, g=0, b=0) != 0.  Real val this pixel is 0xff000000/0x000000ff. */
    if (pTmpSurface->format->BytesPerPixel == 4
      && !pTmpSurface->format->Amask) {
      SDL_SetColorKey(pTmpSurface, SDL_SRCCOLORKEY, Amask);
    } else {
      SDL_SetColorKey(pTmpSurface, SDL_SRCCOLORKEY, 0x0);
    }
    
    /* ===================================================== */
    
    pOcean_Tile = create_ocean_tile();
  
    pOcean_Foged_Tile = create_surf(NORMAL_TILE_WIDTH ,
			    NORMAL_TILE_HEIGHT ,
			    SDL_SWSURFACE);
  
    SDL_BlitSurface(pOcean_Tile , NULL , pOcean_Foged_Tile, NULL);
    SDL_FillRectAlpha(pOcean_Foged_Tile, NULL,
			    get_game_colorRGB(COLOR_STD_FOG_OF_WAR));
    SDL_SetColorKey(pOcean_Foged_Tile , SDL_SRCCOLORKEY|SDL_RLEACCEL,
			  get_first_pixel(pOcean_Foged_Tile));
    
    /* =========================================================== */
    /* create black grid */
    pMapGrid[0][0] = create_surf(NORMAL_TILE_WIDTH / 2,
		  		NORMAL_TILE_HEIGHT / 2, SDL_SWSURFACE);
    pMapGrid[0][1] = create_surf(NORMAL_TILE_WIDTH / 2,
  				NORMAL_TILE_HEIGHT / 2, SDL_SWSURFACE);
    putline(pMapGrid[0][0], 0, pMapGrid[0][0]->h, pMapGrid[0][0]->w, 0, 64);
    putline(pMapGrid[0][1], 0, 0, pMapGrid[0][1]->w, pMapGrid[0][1]->h, 64);
    SDL_SetColorKey(pMapGrid[0][0] , SDL_SRCCOLORKEY|SDL_RLEACCEL, 0x0);
    SDL_SetColorKey(pMapGrid[0][1] , SDL_SRCCOLORKEY|SDL_RLEACCEL, 0x0);
  
    /* create white grid */
    pMapGrid[1][0] = create_surf(NORMAL_TILE_WIDTH / 2,
				NORMAL_TILE_HEIGHT / 2, SDL_SWSURFACE);
    pMapGrid[1][1] = create_surf(NORMAL_TILE_WIDTH / 2,
  				NORMAL_TILE_HEIGHT / 2, SDL_SWSURFACE);
    putline(pMapGrid[1][0], 0, pMapGrid[1][0]->h, pMapGrid[1][0]->w, 0, 0xffffffff);
    putline(pMapGrid[1][1], 0, 0, pMapGrid[1][1]->w, pMapGrid[1][1]->h, 0xffffffff);
    SDL_SetColorKey(pMapGrid[1][0] , SDL_SRCCOLORKEY|SDL_RLEACCEL, 0x0);
    SDL_SetColorKey(pMapGrid[1][1] , SDL_SRCCOLORKEY|SDL_RLEACCEL, 0x0);
  
    /* create red grid */
    pMapGrid[2][0] = create_surf(NORMAL_TILE_WIDTH / 2,
  				NORMAL_TILE_HEIGHT / 2, SDL_SWSURFACE);
    pMapGrid[2][1] = create_surf(NORMAL_TILE_WIDTH / 2,
  				NORMAL_TILE_HEIGHT / 2, SDL_SWSURFACE);
    putline(pMapGrid[2][0], 0, pMapGrid[2][0]->h, pMapGrid[2][0]->w, 0,
				SDL_MapRGB(pMapGrid[2][0]->format, 255, 0,0));
    putline(pMapGrid[2][1], 0, 0, pMapGrid[2][1]->w, pMapGrid[2][1]->h,
  				SDL_MapRGB(pMapGrid[2][1]->format, 255, 0,0));
    SDL_SetColorKey(pMapGrid[2][0] , SDL_SRCCOLORKEY|SDL_RLEACCEL, 0x0);
    SDL_SetColorKey(pMapGrid[2][1] , SDL_SRCCOLORKEY|SDL_RLEACCEL, 0x0);
  
    pBlinkSurfaceA = 
      create_surf(UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT, SDL_SWSURFACE);
  
    pBlinkSurfaceB =
      create_surf(UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT, SDL_SWSURFACE);
      
#ifdef SDL_CVS
    SDL_SetColorKey(pBlinkSurfaceB , SDL_SRCCOLORKEY|SDL_RLEACCEL, 0x0);
#else  
    SDL_SetColorKey(pBlinkSurfaceB , SDL_SRCCOLORKEY, 0x0);
#endif
  
  } else {
    pTmpSurface = NULL;
    pOcean_Tile = NULL;
    pOcean_Foged_Tile = NULL;
    pBlinkSurfaceA = NULL;
    pBlinkSurfaceB = NULL;
    pMapGrid[0][0] = NULL;
    pMapGrid[0][1] = NULL;
    pMapGrid[1][0] = NULL;
    pMapGrid[1][1] = NULL;
    pMapGrid[2][0] = NULL;
    pMapGrid[2][1] = NULL;
  }

}
