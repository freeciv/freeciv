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

#include <stdlib.h>
#include <SDL/SDL.h>

#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "player.h"
#include "shared.h"
#include "support.h"

#include "colors.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_tilespec.h"
#include "gui_zoom.h"
#include "gui_id.h"
#include "gui_string.h"
#include "gui_stuff.h"
#include "mapview.h"
#include "spaceshipdlg.h"

#include "inteldlg.h"

static struct ADVANCED_DLG  *pIntel_Dlg = NULL;

static int intel_window_dlg_callback(struct GUI *pWindow)
{
  return std_move_window_group_callback(pIntel_Dlg->pBeginWidgetList, pWindow);
}

static int tech_callback(struct GUI *pWidget)
{
  /* get tech help - PORT ME */
  return -1;
}

static int spaceship_callback(struct GUI *pWidget)
{
  struct player *pPlayer = pWidget->data.player;
  popdown_intel_dialog();
  popup_spaceship_dialog(pPlayer);
  return -1;
}


static int exit_intel_dlg_callback(struct GUI *pWidget)
{
  popdown_intel_dialog();
  flush_dirty();
  return -1;
}

/**************************************************************************
  Popup an intelligence dialog for the given player.
**************************************************************************/
void popup_intel_dialog(struct player *pPlayer)
{
  struct GUI *pWindow = NULL, *pBuf = NULL, *pLast;
  SDL_Surface *pLogo = NULL;
  SDL_Surface *pText1, *pInfo, *pText2 = NULL;
  SDL_String16 *pStr;
  SDL_Rect dst;
  char cBuf[256];
  int i, n = 0, w = 0, h, count = 0, col;
  struct city *pCapital;
    
  if (pIntel_Dlg) {
    return;
  }
     
  h = WINDOW_TILE_HIGH + 3 + FRAME_WH;
      
  pIntel_Dlg = MALLOC(sizeof(struct ADVANCED_DLG));
  
  pStr = create_str16_from_char(_("Foreign Intelligence Report") , 12);
  pStr->style |= TTF_STYLE_BOLD;
  
  pWindow = create_window(NULL, pStr, 10, 10, WF_DRAW_THEME_TRANSPARENT);
    
  pWindow->action = intel_window_dlg_callback;
  set_wstate(pWindow , FC_WS_NORMAL);
  w = MAX(w , pWindow->size.w);
  
  add_to_gui_list(ID_WINDOW, pWindow);
  pIntel_Dlg->pEndWidgetList = pWindow;
  /* ---------- */
  /* exit button */
  pBuf = create_themeicon(pTheme->Small_CANCEL_Icon, pWindow->dst,
  			  			WF_DRAW_THEME_TRANSPARENT);
  w += pBuf->size.w + 10;
  pBuf->action = exit_intel_dlg_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  
  add_to_gui_list(ID_BUTTON, pBuf);
  /* ---------- */
  
  pLogo = GET_SURF(get_nation_by_idx(pPlayer->nation)->flag_sprite);
  pLogo = make_flag_surface_smaler(pLogo);
  
  pText1 = ZoomSurface(pLogo, 4.0 , 4.0, 1);
  FREESURFACE(pLogo);
  pLogo = pText1;
  SDL_SetColorKey(pLogo,
        SDL_SRCCOLORKEY|SDL_RLEACCEL, getpixel(pLogo, pLogo->w - 1, pLogo->h - 1));
	
  pBuf = create_icon2(pLogo, pWindow->dst,
	(WF_DRAW_THEME_TRANSPARENT|WF_WIDGET_HAS_INFO_LABEL|
					WF_FREE_STRING|WF_FREE_THEME));
  pBuf->action = spaceship_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->data.player = pPlayer;
  my_snprintf(cBuf, sizeof(cBuf),
	      _("Intelligence Information about %s Spaceship"), 
	      				get_nation_name(pPlayer->nation));
  pBuf->string16 = create_str16_from_char(cBuf, 12);
	
  add_to_gui_list(ID_ICON, pBuf);
	
  /* ---------- */
  my_snprintf(cBuf, sizeof(cBuf),
	      _("Intelligence Information for the %s Empire"), 
	      				get_nation_name(pPlayer->nation));
  
  pStr = create_str16_from_char(cBuf, 14);
  pStr->style |= TTF_STYLE_BOLD;
  pStr->render = 3;
  pStr->bgcol.unused = 128;
  
  pText1 = create_text_surf_from_str16(pStr);
  SDL_SetAlpha(pText1, 0x0, 0x0);
  w = MAX(w, pText1->w + 20);
  h += pText1->h + 20;
    
  /* ---------- */
  
  pCapital = find_palace(pPlayer);
  change_ptsize16(pStr, 10);
  pStr->style &= ~TTF_STYLE_BOLD;
  my_snprintf(cBuf, sizeof(cBuf),
    _("Ruler: %s %s  Government: %s\nCapital: %s  Gold: %d\nTax: %d%%"
      " Science: %d%% Luxury: %d%%\nResearching: %s(%d/%d)"),
    get_ruler_title(pPlayer->government, pPlayer->is_male, pPlayer->nation),
    pPlayer->name, get_government_name(pPlayer->government),
    (!pCapital) ? _("(Unknown)") : pCapital->name, pPlayer->economic.gold,
    pPlayer->economic.tax, pPlayer->economic.science, pPlayer->economic.luxury,
    get_tech_name(pPlayer, pPlayer->research.researching),
    pPlayer->research.bulbs_researched, total_bulbs_required(pPlayer));
  
  copy_chars_to_string16(pStr, cBuf);
  pInfo = create_text_surf_from_str16(pStr);
  SDL_SetAlpha(pInfo, 0x0, 0x0);
  w = MAX(w, pLogo->w + 10 + pInfo->w + 20);
  h += MAX(pLogo->h + 20, pInfo->h + 20);
    
  /* ---------- */
  col = w / (GET_SURF(advances[A_FIRST].sprite)->w + 4);
  n = 0;
  pLast = pBuf;
  for(i = A_FIRST; i<game.num_tech_types; i++) {
    if(get_invention(pPlayer, i) == TECH_KNOWN &&
      tech_is_available(game.player_ptr, i) &&
      get_invention(game.player_ptr, i) != TECH_KNOWN) {
      
      pBuf = create_icon2(GET_SURF(advances[i].sprite), pWindow->dst,
	(WF_DRAW_THEME_TRANSPARENT|WF_WIDGET_HAS_INFO_LABEL|WF_FREE_STRING));
      pBuf->action = tech_callback;
      set_wstate(pBuf, FC_WS_NORMAL);

      pBuf->string16 = create_str16_from_char(advances[i].name, 12);
	
      add_to_gui_list(ID_ICON, pBuf);
	
      if(n > ((2 * col) - 1)) {
	set_wflag(pBuf, WF_HIDDEN);
      }
      
      n++;	
    }
  }
  
  pIntel_Dlg->pBeginWidgetList = pBuf;
  
  if(n) {
    pIntel_Dlg->pBeginActiveWidgetList = pBuf;
    pIntel_Dlg->pEndActiveWidgetList = pLast->prev;
    if(n > 2 * col) {
      pIntel_Dlg->pActiveWidgetList = pLast->prev;
      count = create_vertical_scrollbar(pIntel_Dlg, col, 2, TRUE, TRUE);
      h += (2 * pBuf->size.h + 10);
    } else {
      count = 0;
      if(n > col) {
	h += pBuf->size.h;
      }
      h += (10 + pBuf->size.h);
    }
    
    w = MAX(w, col * pBuf->size.w + count + DOUBLE_FRAME_WH);
    
    my_snprintf(cBuf, sizeof(cBuf), _("Their techs that we don't have :"));
    copy_chars_to_string16(pStr, cBuf);
    pStr->style |= TTF_STYLE_BOLD;
    pText2 = create_text_surf_from_str16(pStr);
    SDL_SetAlpha(pText2, 0x0, 0x0);    
  }
  
  FREESTRING16(pStr);
  
  /* ------------------------ */  
  pWindow->size.x = (Main.screen->w - w) / 2;
  pWindow->size.y = (Main.screen->h - h) / 2;
  
  resize_window(pWindow, NULL, NULL, w, h);
  
  /* exit button */
  pBuf = pWindow->prev; 
  pBuf->size.x = pWindow->size.x + pWindow->size.w - pBuf->size.w - FRAME_WH - 1;
  pBuf->size.y = pWindow->size.y + 1;
  
  dst.x = (pWindow->size.w - pText1->w) / 2;
  dst.y = WINDOW_TILE_HIGH + 12;
  
  SDL_BlitSurface(pText1, NULL, pWindow->theme, &dst);
  dst.y += pText1->h + 10;
  FREESURFACE(pText1);
  
  /* space ship button */
  pBuf = pBuf->prev;
  dst.x = (pWindow->size.w - (pBuf->size.w + 10 + pInfo->w)) / 2;
  pBuf->size.x = pWindow->size.x + dst.x;
  pBuf->size.y = pWindow->size.y + dst.y;
  
  dst.x += pBuf->size.w + 10;  
  SDL_BlitSurface(pInfo, NULL, pWindow->theme, &dst);
  dst.y += pInfo->h + 10;
  FREESURFACE(pInfo);
      
  /* --------------------- */
    
  if(n) {
    
    dst.x = FRAME_WH + 5;
    SDL_BlitSurface(pText2, NULL, pWindow->theme, &dst);
    dst.y += pText2->h + 2;
    FREESURFACE(pText2);
    
    setup_vertical_widgets_position(col,
	pWindow->size.x + FRAME_WH,
	pWindow->size.y + dst.y,
	  0, 0, pIntel_Dlg->pBeginActiveWidgetList,
			  pIntel_Dlg->pEndActiveWidgetList);
    
    if(pIntel_Dlg->pScroll) {
      setup_vertical_scrollbar_area(pIntel_Dlg->pScroll,
	pWindow->size.x + pWindow->size.w - FRAME_WH,
    	pWindow->size.y + dst.y,
    	pWindow->size.h - (dst.y + FRAME_WH + 1), TRUE);
    }
  }
    
  /* --------------------- */
  /* redraw */
  redraw_group(pIntel_Dlg->pBeginWidgetList, pWindow, 0);
  sdl_dirty_rect(pWindow->size);
  
  flush_dirty();
}

/**************************************************************************
  Popdown an intelligence dialog for the given player.
**************************************************************************/
void popdown_intel_dialog(void)
{
  if (pIntel_Dlg) {
    popdown_window_group_dialog(pIntel_Dlg->pBeginWidgetList,
			pIntel_Dlg->pEndWidgetList);
    FREE(pIntel_Dlg->pScroll);
    FREE(pIntel_Dlg);
  }
}

/****************************************************************************
  Update the intelligence dialog for the given player.  This is called by
  the core client code when that player's information changes.
****************************************************************************/
void update_intel_dialog(struct player *p)
{
  /* PORTME */
}
