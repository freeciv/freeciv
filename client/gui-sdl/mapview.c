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

/* #define SDL_CVS */

#include "SDL.h"

/* utility */
#include "astring.h"
#include "fcintl.h"
#include "log.h"

/* common */
#include "game.h"
#include "goto.h"
#include "government.h"
#include "unitlist.h"

/* client */
#include "citydlg_common.h"
#include "civclient.h"
#include "climisc.h"
#include "overview_common.h"
#include "pages_g.h"
#include "text.h"

/* gui-sdl */
#include "colors.h"
#include "dialogs.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_mouse.h"
#include "gui_tilespec.h"
#include "mapctrl.h"
#include "sprite.h"
#include "themespec.h"
#include "widget.h"

#include "mapview.h"

extern SDL_Event *pFlush_User_Event;
extern SDL_Rect *pInfo_Area;

int overview_start_x = 0;
int overview_start_y = 0;

static enum {
  NORMAL = 0,
  BORDERS = 1,
  TEAMS
} overview_mode = NORMAL;

static struct canvas *overview_canvas;
static struct canvas *city_map_canvas;
static struct canvas *terrain_canvas;

/* ================================================================ */

void update_map_canvas_scrollbars_size()
{
  /* No scrollbars */
}

static bool is_flush_queued = FALSE;

/**************************************************************************
  Flush the given part of the buffer(s) to the screen.
**************************************************************************/
void flush_mapcanvas(int canvas_x , int canvas_y ,
		     int pixel_width , int pixel_height)
{
  SDL_Rect rect = {canvas_x, canvas_y, pixel_width, pixel_height};
  alphablit(mapview.store->surf, &rect, Main.map, &rect);
}

void flush_rect(SDL_Rect rect, bool force_flush)
{
  if (is_flush_queued && !force_flush) {
    sdl_dirty_rect(rect);
  } else {
    static SDL_Rect src, dst;
    
    if (correct_rect_region(&rect)) {
      static int i = 0;
      dst = rect;
      if (C_S_RUNNING == client_state()) {     
        flush_mapcanvas(dst.x, dst.y, dst.w, dst.h);
      }
      alphablit(Main.map, &rect, Main.screen, &dst);
      if (Main.guis) {
        while((i < Main.guis_count) && Main.guis[i]) {
          src = rect;
          screen_rect_to_layer_rect(Main.guis[i], &src);
          dst = rect;
          alphablit(Main.guis[i++]->surface, &src, Main.screen, &dst);
        }
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
  flush_dirty();
  redraw_selection_rectangle();
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
    
    if ((C_S_RUNNING == client_state()) &&
        (get_client_page() == PAGE_GAME)) {
      flush_mapcanvas(0, 0, Main.screen->w, Main.screen->h);
      refresh_overview();
    }
    alphablit(Main.map, NULL, Main.screen, NULL);
    if (Main.guis) {
      while((j < Main.guis_count) && Main.guis[j]) {
        SDL_Rect dst = Main.guis[j]->dest_rect;
        alphablit(Main.guis[j++]->surface, NULL, Main.screen, &dst);
      }
    }
    j = 0;

    draw_mouse_cursor();    
    
    /* flush main buffer to framebuffer */    
    SDL_UpdateRect(Main.screen, 0, 0, 0, 0);
  } else {
    static int i;
    static SDL_Rect src, dst;
    
    for(i = 0; i<Main.rects_count; i++) {
      
      dst = Main.rects[i];
      if (C_S_RUNNING == client_state()) {     
        flush_mapcanvas(dst.x, dst.y, dst.w, dst.h);
      }
      alphablit(Main.map, &Main.rects[i], Main.screen, &dst);
      if (Main.guis) {
        while((j < Main.guis_count) && Main.guis[j]) {
          src = Main.rects[i];
          screen_rect_to_layer_rect(Main.guis[j], &src);
          dst = Main.rects[i];
          alphablit(Main.guis[j++]->surface, &src, Main.screen, &dst);
        }
      }
      j = 0;
      
      /* restore widget info label if it overlaps with this area */
      dst = Main.rects[i];
      if (pInfo_Area && !(((dst.x + dst.w) < pInfo_Area->x)
                          || (dst.x > (pInfo_Area->x + pInfo_Area->w)) 
                          || ((dst.y + dst.h) < pInfo_Area->y) 
                          || (dst.y > (pInfo_Area->y + pInfo_Area->h)))) {
        redraw_widget_info_label(&dst);     
      }
      
    }

    draw_mouse_cursor();            
    
    /* flush main buffer to framebuffer */    
    SDL_UpdateRects(Main.screen, Main.rects_count, Main.rects);
  }
  Main.rects_count = 0;
}

/**************************************************************************
  This function is called when the map has changed.
**************************************************************************/
void gui_flush(void)
{
  if (C_S_RUNNING == client_state()) {
    refresh_overview();
  }    
}

/* ===================================================================== */

/**************************************************************************
  Set information for the indicator icons typically shown in the main
  client window.  The parameters tell which sprite to use for the
  indicator.
**************************************************************************/
void set_indicator_icons(struct sprite *bulb, struct sprite *sol,
			 struct sprite *flake, struct sprite *gov)
{
  struct widget *pBuf = NULL;
  char cBuf[128];
  
  pBuf = get_widget_pointer_form_main_list(ID_WARMING_ICON);
  FREESURFACE(pBuf->theme);
  pBuf->theme = adj_surf(GET_SURF(sol));
  widget_redraw(pBuf);
    
  pBuf = get_widget_pointer_form_main_list(ID_COOLING_ICON);
  FREESURFACE(pBuf->theme);
  pBuf->theme = adj_surf(GET_SURF(flake));
  widget_redraw(pBuf);
    
  pBuf = get_revolution_widget();
  set_new_icon2_theme(pBuf, adj_surf(GET_SURF(gov)), TRUE);    
  
  if (game.player_ptr) {
    my_snprintf(cBuf, sizeof(cBuf), "%s (%s)\n%s", _("Revolution"), "Shift+R",
                                    government_name_for_player(game.player_ptr));
  } else {
    my_snprintf(cBuf, sizeof(cBuf), "%s (%s)\n%s", _("Revolution"), "Shift+R",
                                    "None");
  }        
  copy_chars_to_string16(pBuf->string16, cBuf);
      
  widget_redraw(pBuf);
  widget_mark_dirty(pBuf);

  
  pBuf = get_tax_rates_widget();
  if(!pBuf->theme) {
    set_new_icon2_theme(pBuf, get_tax_surface(O_GOLD), TRUE);
  }
  
  pBuf = get_research_widget();
  
  if (!game.player_ptr) {
    /* TRANS: Research report action */
    my_snprintf(cBuf, sizeof(cBuf), "%s (%s)\n%s (%d/%d)", _("Research"), "F6",
                                    "None", 0, 0);
  } else if (get_player_research(game.player_ptr)->researching != A_UNSET) {  
    /* TRANS: Research report action */
    my_snprintf(cBuf, sizeof(cBuf), "%s (%s)\n%s (%d/%d)", _("Research"), "F6",
	      	advance_name_researching(game.player_ptr),
	      	get_player_research(game.player_ptr)->bulbs_researched,
  		total_bulbs_required(game.player_ptr));
  } else {
    /* TRANS: Research report action */
    my_snprintf(cBuf, sizeof(cBuf), "%s (%s)\n%s (%d/%d)", _("Research"), "F6",
	      	advance_name_researching(game.player_ptr),
	      	get_player_research(game.player_ptr)->bulbs_researched,
  		0);
  }

  copy_chars_to_string16(pBuf->string16, cBuf);
  
  set_new_icon2_theme(pBuf, adj_surf(GET_SURF(bulb)), TRUE);  
  
  widget_redraw(pBuf);
  widget_mark_dirty(pBuf);
  
}

/****************************************************************************
  Called when the map size changes. This may be used to change the
  size of the GUI element holding the overview canvas. The
  overview.width and overview.height are updated if this function is
  called.
****************************************************************************/
void overview_size_changed(void)
{
  map_canvas_resized(Main.screen->w, Main.screen->h);  	
  
  if (overview_canvas) {
    canvas_free(overview_canvas);
  }      
  overview_canvas = canvas_create(overview.width, overview.height);
  
  resize_minimap();
}

/**************************************************************************
  Typically an info box is provided to tell the player about the state
  of their civilization.  This function is called when the label is
  changed.
**************************************************************************/
void update_info_label(void)
{
  SDL_Color bg_color = {0, 0, 0, 80};

  SDL_Surface *pTmp = NULL;
  char buffer[512];
#ifdef SMALL_SCREEN
  SDL_Rect area = {0, 0, 0, 0};
#else
  SDL_Rect area = {0, 3, 0, 0};
#endif
  SDL_String16 *pText;

  if (get_client_page() != PAGE_GAME) {
    return;
  }
  
#ifdef SMALL_SCREEN
  pText = create_string16(NULL, 0, 8);
#else
  pText = create_string16(NULL, 0, 10);
#endif

  /* set text settings */
  pText->style |= TTF_STYLE_BOLD;
  pText->fgcol = *get_game_colorRGB(COLOR_THEME_MAPVIEW_INFO_TEXT);
  pText->bgcol = (SDL_Color) {0, 0, 0, 0};

  if (game.player_ptr) {
#ifdef SMALL_SCREEN
    my_snprintf(buffer, sizeof(buffer),
                _("%s Population: %s  Year: %s  "
                  "Gold %d "),
                nation_adjective_for_player(game.player_ptr),
                population_to_text(civ_population(game.player_ptr)),
                textyear(game.info.year),
                game.player_ptr->economic.gold);
#else
    my_snprintf(buffer, sizeof(buffer),
                _("%s Population: %s  Year: %s  "
                  "Gold %d Tax: %d Lux: %d Sci: %d "),
                nation_adjective_for_player(game.player_ptr),
                population_to_text(civ_population(game.player_ptr)),
                textyear(game.info.year),
                game.player_ptr->economic.gold,
                game.player_ptr->economic.tax,
                game.player_ptr->economic.luxury,
                game.player_ptr->economic.science);
#endif
    /* convert to unistr and create text surface */
    copy_chars_to_string16(pText, buffer);
    pTmp = create_text_surf_from_str16(pText);

    area.x = (Main.screen->w - pTmp->w) / 2 - adj_size(5);
    area.w = pTmp->w + adj_size(8);
      area.h = pTmp->h + adj_size(4);
  
    SDL_FillRect(Main.gui->surface, &area , map_rgba(Main.gui->surface->format, bg_color));
    
    /* Horizontal lines */
    putline(Main.gui->surface,
               area.x + 1, area.y, area.x + area.w - 2, area.y,
               get_game_colorRGB(COLOR_THEME_MAPVIEW_INFO_FRAME));
    putline(Main.gui->surface,
               area.x + 1, area.y + area.h - 1, area.x + area.w - 2, area.y + area.h - 1,
               get_game_colorRGB(COLOR_THEME_MAPVIEW_INFO_FRAME));
  
    /* vertical lines */
    putline(Main.gui->surface,
               area.x + area.w - 1, area.y + 1, area.x + area.w - 1, area.y + area.h - 2,
               get_game_colorRGB(COLOR_THEME_MAPVIEW_INFO_FRAME));
    putline(Main.gui->surface,
               area.x, area.y + 1, area.x, area.y + area.h - 2,
               get_game_colorRGB(COLOR_THEME_MAPVIEW_INFO_FRAME));
  
    /* blit text to screen */  
    blit_entire_src(pTmp, Main.gui->surface, area.x + adj_size(5), area.y + adj_size(2));
    
    sdl_dirty_rect(area);
    
    FREESURFACE(pTmp);
  }

  set_indicator_icons(client_research_sprite(),
		      client_warming_sprite(),
		      client_cooling_sprite(),
		      client_government_sprite());

  update_timeout_label();

  FREESTRING16(pText);
  
  queue_flush();
}

static int fucus_units_info_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    struct unit *pUnit = pWidget->data.unit;
    if (pUnit) {
      request_new_unit_activity(pUnit, ACTIVITY_IDLE);
      set_unit_focus(pUnit);
    }
  }
  return -1;
}

/****************************************************************************
  Return a (static) string with tile name describing terrain and specials.
  Same as tile_get_info_text() from client/text.c, but with an additional
  line break

  Examples:
    "Hills"
    "Hills (Coals)"
    "Hills (Coals)
     [Pollution]"
****************************************************************************/
static const char *gui_sdl_tile_get_info_text(const struct tile *ptile)
{
  static char s[256];
  bool first;

  sz_strlcpy(s, terrain_name_translation(ptile->terrain));
  if (tile_has_special(ptile, S_RIVER)) {
    sz_strlcat(s, "/");
    sz_strlcat(s, special_name_translation(S_RIVER));
  }

  if (tile_resource_is_valid(ptile)) {
    cat_snprintf(s, sizeof(s), " (%s)",
		 resource_name_translation(ptile->resource));
  }

  first = TRUE;
  if (tile_has_special(ptile, S_POLLUTION)) {
    if (first) {
      first = FALSE;
      sz_strlcat(s, "\n[");
    } else {
      sz_strlcat(s, "/");
    }
    sz_strlcat(s, special_name_translation(S_POLLUTION));
  }
  if (tile_has_special(ptile, S_FALLOUT)) {
    if (first) {
      first = FALSE;
      sz_strlcat(s, "\n[");
    } else {
      sz_strlcat(s, "/");
    }
    sz_strlcat(s, special_name_translation(S_FALLOUT));
  }
  if (!first) {
    sz_strlcat(s, "]");
  }

  return s;
}

/****************************************************************************
  Return the text body for the unit info shown in the info panel.
  Same as get_unit_info_label_text2() from client/text.c, but calls
  gui_sdl_tile_get_info_text() instead.

  FIXME: this should be renamed.
****************************************************************************/
static const char *gui_sdl_get_unit_info_label_text2(struct unit_list *punits)
{
  static struct astring str = ASTRING_INIT;
  int count;

  astr_clear(&str);

  if (!punits) {
    return "";
  }

  count = unit_list_size(punits);

  /* This text should always have the same number of lines.  Otherwise the
   * GUI widgets may be confused and try to resize themselves. */

  /* Line 1. Goto or activity text. */
  if (count > 0 && hover_state != HOVER_NONE) {
    int min, max;

    if (!goto_get_turns(&min, &max)) {
      /* TRANS: Impossible to reach goto target tile */
      astr_add_line(&str, Q_("?goto:Unreachable"), min);
    } else if (min == max) {
      astr_add_line(&str, _("Turns to target: %d"), max);
    } else {
      astr_add_line(&str, _("Turns to target: %d to %d"), min, max);
    }
  } else if (count == 1) {
    astr_add_line(&str, "%s",
      unit_activity_text(unit_list_get(punits, 0)));
  } else if (count > 1) {
    astr_add_line(&str, PL_("%d unit selected",
          "%d units selected",
          count),
      count);
  } else {
    astr_add_line(&str, _("No units selected."));
  }

  /* Lines 2, 3, and 4 vary. */
  if (count == 1) {
    struct unit *punit = unit_list_get(punits, 0);
    struct city *pcity =
  player_find_city_by_id(unit_owner(punit), punit->homecity);
    int infracount;
    bv_special infrastructure =
      get_tile_infrastructure_set(punit->tile, &infracount);

    astr_add_line(&str, "%s", gui_sdl_tile_get_info_text(punit->tile));
    if (infracount > 0) {
      astr_add_line(&str, "%s", get_infrastructure_text(infrastructure));
    } else {
      astr_add_line(&str, " ");
    }
    if (pcity) {
      astr_add_line(&str, "%s", city_name(pcity));
    } else {
      astr_add_line(&str, " ");
    }
  } else if (count > 1) {
    int mil = 0, nonmil = 0;
    int types_count[U_LAST], i;
    struct unit_type *top[3];

    memset(types_count, 0, sizeof(types_count));
    unit_list_iterate(punits, punit) {
      if (unit_has_type_flag(punit, F_NONMIL)) {
  nonmil++;
      } else {
  mil++;
      }
      types_count[unit_type(punit)->index]++;
    } unit_list_iterate_end;

    top[0] = top[1] = top[2] = NULL;
    unit_type_iterate(utype) {
      if (!top[2]
    || types_count[top[2]->index] < types_count[utype->index]) {
  top[2] = utype;

  if (!top[1]
      || types_count[top[1]->index] < types_count[top[2]->index]) {
    top[2] = top[1];
    top[1] = utype;

    if (!top[0]
        || types_count[top[0]->index] < types_count[utype->index]) {
      top[1] = top[0];
      top[0] = utype;
    }
  }
      }
    } unit_type_iterate_end;

    for (i = 0; i < 3; i++) {
      if (top[i] && types_count[top[i]->index] > 0) {
  if (utype_has_flag(top[i], F_NONMIL)) {
    nonmil -= types_count[top[i]->index];
  } else {
    mil -= types_count[top[i]->index];
  }
  astr_add_line(&str, "%d: %s",
          types_count[top[i]->index], utype_name_translation(top[i]));
      } else {
  astr_add_line(&str, " ");
      }
    }

    if (nonmil > 0 && mil > 0) {
      astr_add_line(&str, _("Others: %d civil; %d military"), nonmil, mil);
    } else if (nonmil > 0) {
      astr_add_line(&str, _("Others: %d civilian"), nonmil);
    } else if (mil > 0) {
      astr_add_line(&str, _("Others: %d military"), mil);
    } else {
      astr_add_line(&str, " ");
    }
  } else {
    astr_add_line(&str, " ");
    astr_add_line(&str, " ");
    astr_add_line(&str, " ");
  }

  /* Line 5. Debug text. */
#ifdef DEBUG
  if (count == 1) {
    astr_add_line(&str, "(Unit ID %d)", unit_list_get(punits, 0)->id);
  } else {
    astr_add_line(&str, " ");
  }
#endif

  return str.str;
}

/**************************************************************************
  Read Function Name :)
  FIXME: should use same method as client/text.c popup_info_text()
**************************************************************************/
void redraw_unit_info_label(struct unit_list *punitlist)
{
  struct widget *pInfo_Window;
  SDL_Rect src, area;
  SDL_Rect dest;
  SDL_Surface *pBuf_Surf, *pTmpSurf;
  SDL_String16 *pStr;
  struct canvas *destcanvas;
  struct unit *pUnit = unit_list_get(punitlist, 0);

  if (SDL_Client_Flags & CF_UNITINFO_SHOWN) {
    
    pInfo_Window = get_unit_info_window_widget();

    /* blit theme surface */
    widget_redraw(pInfo_Window);

    if (pUnit) {

      SDL_Surface *pName, *pVet_Name = NULL, *pInfo, *pInfo_II = NULL;
      int sy, y, width, height, n;
      bool right;
      char buffer[512];
      struct tile *pTile = pUnit->tile;

      /* get and draw unit name (with veteran status) */
      pStr = create_str16_from_char(unit_name_translation(pUnit), adj_font(12));
      pStr->style |= TTF_STYLE_BOLD;
      pStr->bgcol = (SDL_Color) {0, 0, 0, 0};
      pName = create_text_surf_from_str16(pStr);
      
      pStr->style &= ~TTF_STYLE_BOLD;
      
      if (pInfo_Window->size.w > 1.8 * 
           ((pInfo_Window->size.w - pInfo_Window->area.w) + DEFAULT_UNITS_W)) {
	width = pInfo_Window->area.w / 2;
	right = TRUE;
      } else {
	width = pInfo_Window->area.w;
	right = FALSE;
      }
      
      if(pUnit->veteran) {
	copy_chars_to_string16(pStr, _("veteran"));
        change_ptsize16(pStr, adj_font(10));
	pStr->fgcol = *get_game_colorRGB(COLOR_THEME_MAPVIEW_UNITINFO_VETERAN_TEXT);
        pVet_Name = create_text_surf_from_str16(pStr);
        pStr->fgcol = *get_game_colorRGB(COLOR_THEME_MAPVIEW_UNITINFO_TEXT);
      }

      /* get and draw other info (MP, terran, city, etc.) */
      change_ptsize16(pStr, adj_font(10));
      pStr->style |= SF_CENTER;

      copy_chars_to_string16(pStr, gui_sdl_get_unit_info_label_text2(punitlist));
      pInfo = create_text_surf_from_str16(pStr);
      
      if (pInfo_Window->size.h > 
          (DEFAULT_UNITS_H + (pInfo_Window->size.h - pInfo_Window->area.h)) || right) {
	int h = TTF_FontHeight(pInfo_Window->string16->font);
				     
	my_snprintf(buffer, sizeof(buffer),"%s",
				sdl_get_tile_defense_info_text(pTile));
	
        if (pInfo_Window->size.h > 
            2 * h + (DEFAULT_UNITS_H + (pInfo_Window->size.h - pInfo_Window->area.h))|| right) {
	  if (game.info.borders > 0 && !pTile->city) {
	    const char *diplo_nation_plural_adjectives[DS_LAST] =
                        {Q_("?nation:Neutral"), Q_("?nation:Hostile"),
     			"" /* unused, DS_CEASEFIRE*/,
     			Q_("?nation:Peaceful"), Q_("?nation:Friendly"), 
     			Q_("?nation:Mysterious")};
            if (tile_owner(pTile) == game.player_ptr) {
              cat_snprintf(buffer, sizeof(buffer), _("\nOur Territory"));
            } else {
	      if (tile_owner(pTile)) {
                if (game.player_ptr->diplstates[tile_owner(pTile)->player_no].type==DS_CEASEFIRE){
		  int turns = game.player_ptr->diplstates[tile_owner(pTile)->player_no].turns_left;
		  cat_snprintf(buffer, sizeof(buffer),
		  	PL_("\n%s territory (%d turn ceasefire)",
				"\n%s territory (%d turn ceasefire)", turns),
		 		nation_adjective_for_player(tile_owner(pTile)), turns);
                } else {
	          cat_snprintf(buffer, sizeof(buffer), _("\nTerritory of the %s %s"),
		    diplo_nation_plural_adjectives[
		  	game.player_ptr->diplstates[tile_owner(pTile)->player_no].type],
		    		nation_plural_for_player(tile_owner(pTile)));
                }
              } else { /* !tile_owner(pTile) */
                cat_snprintf(buffer, sizeof(buffer), _("\nUnclaimed territory"));
              }
	    }
          } /* game.info.borders > 0 && !pTile->city */
          
          if (pTile->city) {
            /* Look at city owner, not tile owner (the two should be the same, if
             * borders are in use). */
            struct player *pOwner = city_owner(pTile->city);
            bool citywall;
/*            bool barrack = FALSE, airport = FALSE, port = FALSE;*/
	    const char *diplo_city_adjectives[DS_LAST] =
    			{Q_("?city:Neutral"), Q_("?city:Hostile"),
     			  "" /*unused, DS_CEASEFIRE */, Q_("?city:Peaceful"),
			  Q_("?city:Friendly"), Q_("?city:Mysterious")};
			  
	    cat_snprintf(buffer, sizeof(buffer),
			 _("\nCity of %s"),
			 city_name(pTile->city));

	    citywall = pTile->city->client.walls;
                          
#if 0       
            /* This has hardcoded assumption that EFT_LAND_REGEN is always
             * provided by *building* named *Barracks*. Similar assumptions for
             * other effects. */     
	    if (pplayers_allied(game.player_ptr, pOwner)) {
	      barrack = (get_city_bonus(pTile->city, EFT_LAND_REGEN) > 0);
	      airport = (get_city_bonus(pTile->city, EFT_AIR_VETERAN) > 0);
	      port = (get_city_bonus(pTile->city, EFT_SEA_VETERAN) > 0);
	    }
	  
	    if (citywall || barrack || airport || port) {
	      cat_snprintf(buffer, sizeof(buffer), Q_("?blistbegin: with "));
	      if (barrack) {
                cat_snprintf(buffer, sizeof(buffer), _("Barracks"));
	        if (port || airport || citywall) {
	          cat_snprintf(buffer, sizeof(buffer), Q_("?blistmore:, "));
	        }
	      }
	      if (port) {
	        cat_snprintf(buffer, sizeof(buffer), _("Port"));
	        if (airport || citywall) {
	          cat_snprintf(buffer, sizeof(buffer), Q_("?blistmore:, "));
	        }
	      }
	      if (airport) {
	        cat_snprintf(buffer, sizeof(buffer), _("Airport"));
	        if (citywall) {
	          cat_snprintf(buffer, sizeof(buffer), Q_("?blistmore:, "));
	        }
	      }
	      if (citywall) {
	        cat_snprintf(buffer, sizeof(buffer), _("City Walls"));
              }

              cat_snprintf(buffer, sizeof(buffer), Q_("?blistend:"));
	    }
#endif
	    
	    if (pOwner && pOwner != game.player_ptr) {
              /* TRANS: (<nation>,<diplomatic_state>)" */
              cat_snprintf(buffer, sizeof(buffer), _("\n(%s,%s)"),
		  nation_adjective_for_player(pOwner),
		  diplo_city_adjectives[game.player_ptr->
				   diplstates[player_index(pOwner)].type]);
	    }
	    
	  }
        }
		
	if (pInfo_Window->size.h > 
            4 * h + (DEFAULT_UNITS_H + (pInfo_Window->size.h - pInfo_Window->area.h)) || right) {
          cat_snprintf(buffer, sizeof(buffer), _("\nFood/Prod/Trade: %s"),
				get_tile_output_text(pUnit->tile));
	}
	
	copy_chars_to_string16(pStr, buffer);
      
	pInfo_II = create_text_surf_smaller_that_w(pStr, width - BLOCKU_W - adj_size(4));
	
      }
      
      FREESTRING16(pStr);
      
      /* ------------------------------------------- */
      
      n = unit_list_size(pTile->units);
      y = 0;
      
      if (n > 1 && ((!right && pInfo_II
	 && (pInfo_Window->size.h - (DEFAULT_UNITS_H + (pInfo_Window->size.h - pInfo_Window->area.h)) - pInfo_II->h > 52))
         || (right && pInfo_Window->size.h - (DEFAULT_UNITS_H + (pInfo_Window->size.h - pInfo_Window->area.h)) > 52))) {
	height = (pInfo_Window->size.h - pInfo_Window->area.h) + DEFAULT_UNITS_H;
      } else {
	height = pInfo_Window->size.h;
        if (pInfo_Window->size.h > (DEFAULT_UNITS_H + (pInfo_Window->size.h - pInfo_Window->area.h))) {
	  y = (pInfo_Window->size.h - (DEFAULT_UNITS_H + (pInfo_Window->size.h - pInfo_Window->area.h)) -
	                 (!right && pInfo_II ? pInfo_II->h : 0)) / 2;
        }
      }
      
      sy = y + adj_size(3);
      area.y = pInfo_Window->area.y + sy;
      area.x = pInfo_Window->area.x + BLOCKU_W + (width - pName->w - BLOCKU_W) / 2;
      dest = area;
      alphablit(pName, NULL, pInfo_Window->dst->surface, &dest);
      sy += pName->h;
      if(pVet_Name) {
	area.y += pName->h - adj_size(3);
        area.x = pInfo_Window->area.x + BLOCKU_W + (width - pVet_Name->w - BLOCKU_W) / 2;
        alphablit(pVet_Name, NULL, pInfo_Window->dst->surface, &area);
	sy += pVet_Name->h - adj_size(3);
        FREESURFACE(pVet_Name);
      }
      FREESURFACE(pName);
      
      /* draw unit sprite */
      pTmpSurf = ResizeSurfaceBox(get_unittype_surface(unit_type(pUnit)),
                                  adj_size(80), adj_size(80), 1, TRUE, TRUE);
      pBuf_Surf = blend_surface(pTmpSurf, 32);
      FREESURFACE(pTmpSurf);
      src = (SDL_Rect){0, 0, pBuf_Surf->w, pBuf_Surf->h};

      area.x = pInfo_Window->area.x + BLOCKU_W - adj_size(4) + 
               ((width - BLOCKU_W + adj_size(3) - src.w)/2);
      area.y = pInfo_Window->size.y + sy + (DEFAULT_UNITS_H - sy - src.h)/2;
      alphablit(pBuf_Surf, &src, pInfo_Window->dst->surface, &area);
      FREESURFACE(pBuf_Surf);

      /* blit unit info text */
      area.x = pInfo_Window->area.x + BLOCKU_W - adj_size(4) + 
                 ((width - BLOCKU_W + adj_size(4) - pInfo->w)/2);
      area.y = pInfo_Window->size.y + sy + (DEFAULT_UNITS_H - sy - pInfo->h)/2;

      alphablit(pInfo, NULL, pInfo_Window->dst->surface, &area);
      FREESURFACE(pInfo);

      if (pInfo_II) {
        if (right) {
	  area.x = pInfo_Window->area.x + width + (width - pInfo_II->w) / 2;
	  area.y = pInfo_Window->area.y + (height - pInfo_II->h) / 2;
        } else {
	  area.y = pInfo_Window->area.y + DEFAULT_UNITS_H + y;
          area.x = pInfo_Window->area.x + BLOCKU_W + 
                   (width - BLOCKU_W - pInfo_II->w) / 2;
        }
      
        /* blit unit info text */
        alphablit(pInfo_II, NULL, pInfo_Window->dst->surface, &area);
              
        if (right) {
          sy = (DEFAULT_UNITS_H + (pInfo_Window->size.h - pInfo_Window->area.h));
        } else {
	  sy = area.y - pInfo_Window->size.y + pInfo_II->h;
        }
        FREESURFACE(pInfo_II);
      } else {
	sy = (DEFAULT_UNITS_H + (pInfo_Window->size.h - pInfo_Window->area.h));
      }
      
      if (n > 1 && (pInfo_Window->size.h - sy > 52)) {
	struct ADVANCED_DLG *pDlg = pInfo_Window->private_data.adv_dlg;
	struct widget *pBuf = NULL, *pEnd = NULL, *pDock;
	struct city *pHome_City;
        struct unit_type *pUType;
	int num_w, num_h;
	  
	if (pDlg->pEndActiveWidgetList && pDlg->pBeginActiveWidgetList) {
	  del_group(pDlg->pBeginActiveWidgetList, pDlg->pEndActiveWidgetList);
	}
	num_w = (pInfo_Window->area.w - BLOCKU_W) / 68;
	num_h = (pInfo_Window->area.h - sy) / 52;
	pDock = pInfo_Window;
	n = 0;
        unit_list_iterate(pTile->units, aunit) {
          if (aunit == pUnit) {
	    continue;
	  }
	    
	  pUType = unit_type(aunit);
          pHome_City = game_find_city_by_number(aunit->homecity);
          my_snprintf(buffer, sizeof(buffer), "%s (%d,%d,%d)%s\n%s\n(%d/%d)\n%s",
		utype_name_translation(pUType),
		pUType->attack_strength,
		pUType->defense_strength, pUType->move_rate / SINGLE_MOVE,
                (aunit->veteran ? _("\nveteran") : ""),
                unit_activity_text(aunit),
		aunit->hp, pUType->hp,
		pHome_City ? city_name(pHome_City) : _("None"));
      
	  pBuf_Surf = create_surf(tileset_full_tile_width(tileset),
	    				tileset_full_tile_height(tileset), SDL_SWSURFACE);

          destcanvas = canvas_create(tileset_full_tile_width(tileset), tileset_full_tile_height(tileset));
  
          put_unit(aunit, destcanvas, 0, 0);
          
          pTmpSurf = adj_surf(destcanvas->surf);
          
          alphablit(pTmpSurf, NULL, pBuf_Surf, NULL);
          
          FREESURFACE(pTmpSurf);
          canvas_free(destcanvas);

          if (pBuf_Surf->w > 64) {
            float zoom = 64.0 / pBuf_Surf->w;    
            SDL_Surface *pZoomed = zoomSurface(pBuf_Surf, zoom, zoom, 1);
            FREESURFACE(pBuf_Surf);
            pBuf_Surf = pZoomed;
          }
	    
	  pStr = create_str16_from_char(buffer, 10);
          pStr->style |= SF_CENTER;
    
          pBuf = create_icon2(pBuf_Surf, pInfo_Window->dst,
	             (WF_FREE_THEME | WF_RESTORE_BACKGROUND |
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
	pDlg->pActiveWidgetList = pDlg->pEndActiveWidgetList;
	  	  	  
	if (n > num_w * num_h) {
    
	  if (!pDlg->pScroll) {
            
            create_vertical_scrollbar(pDlg, num_w, num_h, FALSE, TRUE);
          
	  } else {
	    pDlg->pScroll->active = num_h;
            pDlg->pScroll->step = num_w;
            pDlg->pScroll->count = n;
	    show_scrollbar(pDlg->pScroll);
	  }
	    
	  /* create up button */
          pBuf = pDlg->pScroll->pUp_Left_Button;
          pBuf->size.x = pInfo_Window->area.x + pInfo_Window->area.w - pBuf->size.w;
          pBuf->size.y = pInfo_Window->area.y + sy +
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
          pInfo_Window->area.x + BLOCKU_W + adj_size(2),
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

      if (game.player_ptr) {
        char buf[256];
        my_snprintf(buf, sizeof(buf), "%s\n%s\n%s",
                    _("End of Turn"), _("Press"), _("Shift+Return"));
        pStr = create_str16_from_char(buf, adj_font(14));
        pStr->style = SF_CENTER;
        pStr->bgcol = (SDL_Color) {0, 0, 0, 0};
        pBuf_Surf = create_text_surf_from_str16(pStr);
        area.x = pInfo_Window->size.x + BLOCKU_W +
                          (pInfo_Window->size.w - BLOCKU_W - pBuf_Surf->w)/2;
        area.y = pInfo_Window->size.y + (pInfo_Window->size.h - pBuf_Surf->h)/2;
        alphablit(pBuf_Surf, NULL, pInfo_Window->dst->surface, &area);
        FREESURFACE(pBuf_Surf);
        FREESTRING16(pStr);
      }
    }

    /* draw buttons */
    redraw_group(pInfo_Window->private_data.adv_dlg->pBeginWidgetList,
	    	pInfo_Window->private_data.adv_dlg->pEndWidgetList->prev, 0);
    
    widget_mark_dirty(pInfo_Window);
  }
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
/* FIXME */
/*  update_unit_info_label(punit);*/
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
void update_unit_info_label(struct unit_list *punitlist)
{
  if (C_S_RUNNING != client_state()) {
    return;
  }
  
  /* draw unit info window */
  redraw_unit_info_label(punitlist);
  
  if (punitlist) {
    if(!is_anim_enabled()) {
      enable_focus_animation();
    }
    if (action_state == CURSOR_ACTION_WAIT) {
      update_mouse_cursor(CURSOR_WAIT);
    } else {
    switch (hover_state) {
    case HOVER_NONE:
      if (action_state == CURSOR_ACTION_SELECT) {
        update_mouse_cursor(CURSOR_SELECT); 
      } else if (action_state == CURSOR_ACTION_PARATROOPER) {
        update_mouse_cursor(CURSOR_PARADROP);  
      } else if (action_state == CURSOR_ACTION_NUKE) {
        update_mouse_cursor(CURSOR_NUKE);
      } else {
        update_mouse_cursor(CURSOR_DEFAULT);
      }  
      break;
    case HOVER_GOTO:
      if (action_state == CURSOR_ACTION_GOTO) {
        update_mouse_cursor(CURSOR_GOTO);
      } else if (action_state == CURSOR_ACTION_DEFAULT) {
        update_mouse_cursor(CURSOR_DEFAULT);
      } else if (action_state == CURSOR_ACTION_ATTACK) {
        update_mouse_cursor(CURSOR_ATTACK); 
      } else {
        update_mouse_cursor(CURSOR_INVALID);  
      }
      break;
    case HOVER_PATROL:
      if (action_state == CURSOR_ACTION_INVALID) {
        update_mouse_cursor(CURSOR_INVALID);
      } else {
        update_mouse_cursor(CURSOR_PATROL);
      }
      break;
    case HOVER_CONNECT:
      if (action_state == CURSOR_ACTION_INVALID) {
        update_mouse_cursor(CURSOR_INVALID);
      } else {
        update_mouse_cursor(CURSOR_GOTO);
      }
      break;
    case HOVER_NUKE:
      update_mouse_cursor(CURSOR_NUKE);
      break;
    case HOVER_PARADROP:
      update_mouse_cursor(CURSOR_PARADROP);
      break;
    }
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
  show_city_descriptions(0, 0, mapview.store_width, mapview.store_height);
  dirty_all();  
}

/* ===================================================================== */
/* =============================== Mini Map ============================ */
/* ===================================================================== */

/**************************************************************************
...
**************************************************************************/
void toggle_overview_mode(void)
{
  /* FIXME: has no effect anymore */
  if (overview_mode == BORDERS) {
    overview_mode = NORMAL;
  } else {
    overview_mode = BORDERS;
  }
}

/****************************************************************************
  Return a canvas that is the overview window.
****************************************************************************/
struct canvas *get_overview_window(void)
{
  return overview_canvas;  
}

/****************************************************************************
  Return the dimensions of the area (container widget; maximum size) for
  the overview.
****************************************************************************/
void get_overview_area_dimensions(int *width, int *height)
{
  /* calculate the dimensions in a way to always get a resulting
     overview with a height bigger than or equal to DEFAULT_OVERVIEW_H.
     First, the default dimensions are fed to the same formula that
     is used in overview_common.c. If the resulting height is
     smaller than DEFAULT_OVERVIEW_H, increase OVERVIEW_TILE_SIZE
     by 1 until the height condition is met.
  */
  
  int overview_tile_size_bak = OVERVIEW_TILE_SIZE;
  
  int xfact = MAP_IS_ISOMETRIC ? 2 : 1;
  int shift = (MAP_IS_ISOMETRIC && !topo_has_flag(TF_WRAPX)) ? -1 : 0;
  
  OVERVIEW_TILE_SIZE = MIN((DEFAULT_OVERVIEW_W - 1) / (map.xsize * xfact),
                           (DEFAULT_OVERVIEW_H - 1) / map.ysize) + 1;

  do {
    *height = OVERVIEW_TILE_HEIGHT * map.ysize;
    if (*height < DEFAULT_OVERVIEW_H) {
      OVERVIEW_TILE_SIZE++;
    }
  } while (*height < DEFAULT_OVERVIEW_H);

  *width = OVERVIEW_TILE_WIDTH * map.xsize + shift * OVERVIEW_TILE_SIZE; 
  
  OVERVIEW_TILE_SIZE = overview_tile_size_bak;
}

/**************************************************************************
  Refresh (update) the viewrect on the overview. This is the rectangle
  showing the area covered by the mapview.
**************************************************************************/
void refresh_overview(void)
{
  struct widget *pMMap;
  SDL_Rect overview_area;

  if (SDL_Client_Flags & CF_OVERVIEW_SHOWN) {
    pMMap = get_minimap_window_widget();
      
    overview_area = (SDL_Rect) {
      pMMap->area.x + overview_start_x, pMMap->area.x + overview_start_y, 
      overview_canvas->surf->w, overview_canvas->surf->h
    };
  
    alphablit(overview_canvas->surf, NULL, pMMap->dst->surface, &overview_area);
    
    widget_mark_dirty(pMMap);
  }
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
void put_cross_overlay_tile(struct tile *ptile)
{
  freelog(LOG_DEBUG, "MAPVIEW: put_cross_overlay_tile : PORT ME");
}

/**************************************************************************
 Area Selection
**************************************************************************/
void draw_selection_rectangle(int canvas_x, int canvas_y, int w, int h)
{
  /* PORTME */
  putframe(Main.map,
           canvas_x, canvas_y, canvas_x + w, canvas_y + h,
           get_game_colorRGB(COLOR_THEME_SELECTIONRECTANGLE));
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

/* =====================================================================
				City MAP
   ===================================================================== */

SDL_Surface *create_city_map(struct city *pCity)
{
  /* city map dimensions might have changed, so create a new canvas each time */

  if (city_map_canvas) {
    canvas_free(city_map_canvas);
  }

  city_map_canvas = canvas_create(get_citydlg_canvas_width(), 
                                  get_citydlg_canvas_height());

  city_dialog_redraw_map(pCity, city_map_canvas);  

  return city_map_canvas->surf;
}

SDL_Surface *get_terrain_surface(struct tile *ptile)
{
  /* tileset dimensions might have changed, so create a new canvas each time */
  
  if (terrain_canvas) {
    canvas_free(terrain_canvas);
  }
    
  terrain_canvas = canvas_create_with_alpha(tileset_full_tile_width(tileset),
                                            tileset_full_tile_height(tileset));

  put_terrain(ptile, terrain_canvas, 0, 0);
  
  return terrain_canvas->surf;
}
