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

/* utility */
#include "fcintl.h"

/* common */
#include "game.h"

/* gui-sdl */
#include "colors.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "gui_tilespec.h"
#include "mapctrl.h"
#include "mapview.h"

#include "finddlg.h"

/* ====================================================================== */
/* ============================= FIND CITY MENU ========================= */
/* ====================================================================== */
static struct ADVANCED_DLG  *pFind_City_Dlg = NULL;

static int find_city_window_dlg_callback(struct widget *pWindow)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    move_window_group(pFind_City_Dlg->pBeginWidgetList, pWindow);
  }
  return -1;
}

static int exit_find_city_dlg_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    int orginal_x = pWidget->data.cont->id0;
    int orginal_y = pWidget->data.cont->id1;
        
    popdown_find_dialog();
    
    center_tile_mapcanvas(map_pos_to_tile(orginal_x, orginal_y));
    
    flush_dirty();
  }
  return -1;
}

static int find_city_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    struct city *pCity = pWidget->data.city;
    if(pCity) {
      center_tile_mapcanvas(pCity->tile);
      if(Main.event.button.button == SDL_BUTTON_RIGHT) {
        popdown_find_dialog();
      }
      flush_dirty();
    }
  }
  return -1;
}

/**************************************************************************
  Popdown a dialog to ask for a city to find.
**************************************************************************/
void popdown_find_dialog(void)
{
  if (pFind_City_Dlg) {
    popdown_window_group_dialog(pFind_City_Dlg->pBeginWidgetList,
			pFind_City_Dlg->pEndWidgetList);
    FC_FREE(pFind_City_Dlg->pScroll);
    FC_FREE(pFind_City_Dlg);
    enable_and_redraw_find_city_button();
  }
}

/**************************************************************************
  Popup a dialog to ask for a city to find.
**************************************************************************/
void popup_find_dialog(void)
{
  struct widget *pWindow = NULL, *pBuf = NULL;
  SDL_Surface *pLogo = NULL;
  SDL_String16 *pStr;
  char cBuf[128]; 
  int i, n = 0, w = 0, h, /*owner = 0xffff,*/ units_h = 0;
  struct player *owner = NULL;
  struct tile *original;
  bool mouse = (Main.event.type == SDL_MOUSEBUTTONDOWN);
  
  /* check that there are any cities to find */
  h = 0;
  i = 0;    
  while(!h && i<game.info.nplayers) {
    h = city_list_size(game.players[i++].cities);
  }
  
  if (pFind_City_Dlg && !h) {
    return;
  }
     
  h = WINDOW_TITLE_HEIGHT + adj_size(3) + pTheme->FR_Bottom->h;
  
  original = canvas_pos_to_tile(Main.map->w/2, Main.map->h/2);
  
  pFind_City_Dlg = fc_calloc(1, sizeof(struct ADVANCED_DLG));
  
  pStr = create_str16_from_char(_("Find City") , adj_font(12));
  pStr->style |= TTF_STYLE_BOLD;
  
  pWindow = create_window(NULL, pStr, 1, 1, 0);
    
  pWindow->action = find_city_window_dlg_callback;
  set_wstate(pWindow , FC_WS_NORMAL);
  w = MAX(w , pWindow->size.w);
  
  add_to_gui_list(ID_TERRAIN_ADV_DLG_WINDOW, pWindow);
  pFind_City_Dlg->pEndWidgetList = pWindow;
  /* ---------- */
  /* exit button */
  pBuf = create_themeicon(pTheme->Small_CANCEL_Icon, pWindow->dst,
  			  	(WF_RESTORE_BACKGROUND|WF_FREE_DATA));
  w += pBuf->size.w + adj_size(10);
  pBuf->action = exit_find_city_dlg_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  pBuf->data.cont = fc_calloc(1, sizeof(struct CONTAINER));
  pBuf->data.cont->id0 = original->x;
  pBuf->data.cont->id1 = original->y;

  add_to_gui_list(ID_TERRAIN_ADV_DLG_EXIT_BUTTON, pBuf);
  /* ---------- */
  
  for(i=0; i<game.info.nplayers; i++) {
    city_list_iterate(game.players[i].cities, pCity) {
    
      my_snprintf(cBuf , sizeof(cBuf), "%s (%d)",pCity->name, pCity->size);
      
      pStr = create_str16_from_char(cBuf , adj_font(10));
      pStr->style |= (TTF_STYLE_BOLD|SF_CENTER);
   
      if(pCity->owner != owner) {
        pLogo = GET_SURF(get_nation_flag_sprite(tileset, 
			get_player(pCity->owner->player_no)->nation));
        pLogo = make_flag_surface_smaler(pLogo);
      }
      
      pBuf = create_iconlabel(pLogo, pWindow->dst, pStr, 
    	(WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE));
      
      if(pCity->owner != owner) {
        set_wflag(pBuf, WF_FREE_THEME);
        owner = pCity->owner;
      }
      
      pBuf->string16->style &= ~SF_CENTER;
      pBuf->string16->fgcol =
	    *(get_player_color(tileset, city_owner(pCity))->color);	  
      pBuf->string16->bgcol = (SDL_Color) {0, 0, 0, 0};
    
      pBuf->data.city = pCity;
  
      pBuf->action = find_city_callback;
      set_wstate(pBuf, FC_WS_NORMAL);
  
      add_to_gui_list(ID_LABEL , pBuf);
    
      w = MAX(w , pBuf->size.w);
      h += pBuf->size.h;
    
      if (n > 19)
      {
        set_wflag(pBuf , WF_HIDDEN);
      }
      
      n++;  
    } city_list_iterate_end;
  }
  pFind_City_Dlg->pBeginWidgetList = pBuf;
  pFind_City_Dlg->pBeginActiveWidgetList = pBuf;
  pFind_City_Dlg->pEndActiveWidgetList = pWindow->prev->prev;
  pFind_City_Dlg->pActiveWidgetList = pFind_City_Dlg->pEndActiveWidgetList;
  
  
  /* ---------- */
  if (n > 20)
  {
     
    units_h = create_vertical_scrollbar(pFind_City_Dlg, 1, 20, TRUE, TRUE);
    pFind_City_Dlg->pScroll->count = n;
    
    n = units_h;
    w += n;
    
    units_h = 20 * pBuf->size.h + WINDOW_TITLE_HEIGHT + adj_size(3) + pTheme->FR_Bottom->h;
    
  } else {
    units_h = h;
  }
        
  /* ---------- */
  
  w += (pTheme->FR_Left->w + pTheme->FR_Right->w);
  
  h = units_h;

  if(!mouse) {  
    pWindow->size.x = adj_size(10);
    pWindow->size.y = (Main.screen->h - h) / 2;
  } else {
    pWindow->size.x = ((Main.event.motion.x + w < Main.screen->w) ?
                     (Main.event.motion.x + adj_size(10)) : (Main.screen->w - w - adj_size(10)));
    pWindow->size.y = 
      ((Main.event.motion.y - (WINDOW_TITLE_HEIGHT + adj_size(2)) + h < Main.screen->h) ?
             (Main.event.motion.y - (WINDOW_TITLE_HEIGHT + adj_size(2))) :
             (Main.screen->h - h - adj_size(10)));
    
  }

  set_window_pos(pWindow, pWindow->size.x, pWindow->size.y);  
  
  resize_window(pWindow , NULL, NULL, w, h);
  
  w -= (pTheme->FR_Left->w + pTheme->FR_Right->w);
  
  if (pFind_City_Dlg->pScroll)
  {
    w -= n;
  }
  
  /* exit button */
  pBuf = pWindow->prev;
  
  pBuf->size.x = pWindow->size.x + pWindow->size.w - pBuf->size.w - pTheme->FR_Right->w - 1;
  pBuf->size.y = pWindow->size.y + 1;
  
  /* cities */
  pBuf = pBuf->prev;
  setup_vertical_widgets_position(1,
	pWindow->size.x + pTheme->FR_Left->w, pWindow->size.y + WINDOW_TITLE_HEIGHT + adj_size(2),
	w, 0, pFind_City_Dlg->pBeginActiveWidgetList, pBuf);
  
  if (pFind_City_Dlg->pScroll)
  {
    setup_vertical_scrollbar_area(pFind_City_Dlg->pScroll,
	pWindow->size.x + pWindow->size.w - pTheme->FR_Right->w,
    	pWindow->size.y + WINDOW_TITLE_HEIGHT + 1,
    	pWindow->size.h - (pTheme->FR_Bottom->h + WINDOW_TITLE_HEIGHT + 1), TRUE);
  }
  
  /* -------------------- */
  /* redraw */
  redraw_group(pFind_City_Dlg->pBeginWidgetList, pWindow, 0);
  sdl_dirty_rect(pWindow->size);
  
  flush_dirty();
}
