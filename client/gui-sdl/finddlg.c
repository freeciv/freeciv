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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL/SDL.h>

#include "support.h"
#include "fcintl.h"

#include "game.h"
#include "player.h"
#include "map.h"

#include "gui_main.h"
#include "graphics.h"
#include "gui_zoom.h"
#include "gui_tilespec.h"

#include "gui_string.h"
#include "gui_stuff.h"
#include "gui_id.h"
#include "colors.h"

#include "mapview.h"
#include "mapctrl.h"
#include "dialogs.h"
#include "tilespec.h"

#include "finddlg.h"

/* ====================================================================== */
/* ============================= FIND CITY MENU ========================= */
/* ====================================================================== */
static struct ADVANCED_DLG  *pFind_City_Dlg = NULL;

static int find_city_window_dlg_callback(struct GUI *pWindow)
{
  return std_move_window_group_callback(pFind_City_Dlg->pBeginWidgetList,
								  pWindow);
}

static int exit_find_city_dlg_callback(struct GUI *pWidget)
{
  int orginal_x = pWidget->data.cont->id0;
  int orginal_y = pWidget->data.cont->id1;
      
  popdown_find_dialog();
  
  center_tile_mapcanvas(orginal_x, orginal_y);
  
  flush_dirty();
  return -1;
}

static int find_city_callback(struct GUI *pWidget)
{
  struct city *pCity = pWidget->data.city;
  if(pCity) {
    center_tile_mapcanvas(pCity->x, pCity->y);
    if(Main.event.button.button == SDL_BUTTON_RIGHT) {
      popdown_find_dialog();
    }
    flush_dirty();
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
    FREE(pFind_City_Dlg->pScroll);
    FREE(pFind_City_Dlg);
    enable_and_redraw_find_city_button();
  }
}

/**************************************************************************
  Popup a dialog to ask for a city to find.
**************************************************************************/
void popup_find_dialog(void)
{
  struct GUI *pWindow = NULL, *pBuf = NULL;
  SDL_Surface *pLogo = NULL;
  SDL_String16 *pStr;
  char cBuf[128]; 
  int i, n = 0, w = 0, h, owner = 0xffff, units_h = 0, orginal_x, orginal_y;
  bool mouse = (Main.event.type == SDL_MOUSEBUTTONDOWN);
  
  /* check that there are any cities to find */
  h = 0;
  i = 0;    
  while(!h && i<game.nplayers) {
    h = city_list_size(&game.players[i++].cities);
  }
  
  if (pFind_City_Dlg && !h) {
    return;
  }
     
  h = WINDOW_TILE_HIGH + 3 + FRAME_WH;
  
  canvas_to_map_pos(&orginal_x, &orginal_y, Main.map->w/2, Main.map->h/2);
  
  pFind_City_Dlg = MALLOC(sizeof(struct ADVANCED_DLG));
  
  pStr = create_str16_from_char(_("Find City") , 12);
  pStr->style |= TTF_STYLE_BOLD;
  
  pWindow = create_window(NULL, pStr, 10, 10, WF_DRAW_THEME_TRANSPARENT);
    
  pWindow->action = find_city_window_dlg_callback;
  set_wstate(pWindow , FC_WS_NORMAL);
  w = MAX(w , pWindow->size.w);
  
  add_to_gui_list(ID_TERRAIN_ADV_DLG_WINDOW, pWindow);
  pFind_City_Dlg->pEndWidgetList = pWindow;
  /* ---------- */
  /* exit button */
  pBuf = create_themeicon(pTheme->Small_CANCEL_Icon, pWindow->dst,
  			  	(WF_DRAW_THEME_TRANSPARENT|WF_FREE_DATA));
  w += pBuf->size.w + 10;
  pBuf->action = exit_find_city_dlg_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  pBuf->data.cont = MALLOC(sizeof(struct CONTAINER));
  pBuf->data.cont->id0 = orginal_x;
  pBuf->data.cont->id1 = orginal_y;

  add_to_gui_list(ID_TERRAIN_ADV_DLG_EXIT_BUTTON, pBuf);
  /* ---------- */
  
  for(i=0; i<game.nplayers; i++) {
    city_list_iterate(game.players[i].cities, pCity) {
    
      my_snprintf(cBuf , sizeof(cBuf), "%s (%d)",pCity->name, pCity->size);
      
      pStr = create_str16_from_char(cBuf , 10);
      pStr->style |= (TTF_STYLE_BOLD|SF_CENTER);
   
      if(pCity->owner != owner) {
        pLogo = GET_SURF(get_nation_by_idx(
			get_player(pCity->owner)->nation)->flag_sprite);
        pLogo = make_flag_surface_smaler(pLogo);
      }
      
      pBuf = create_iconlabel(pLogo, pWindow->dst, pStr, 
    	(WF_DRAW_THEME_TRANSPARENT|WF_DRAW_TEXT_LABEL_WITH_SPACE));
      
      if(pCity->owner != owner) {
        set_wflag(pBuf, WF_FREE_THEME);
        owner = pCity->owner;
      }
      
      pBuf->string16->style &= ~SF_CENTER;
      pBuf->string16->fgcol =
	    *(get_game_colorRGB(player_color(get_player(pCity->owner))));
      pBuf->string16->render = 3;
      pBuf->string16->bgcol.unused = 128;
    
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
    
    units_h = 20 * pBuf->size.h + WINDOW_TILE_HIGH + 3 + FRAME_WH;
    
  } else {
    units_h = h;
  }
        
  /* ---------- */
  
  w += DOUBLE_FRAME_WH;
  
  h = units_h;

  if(!mouse) {  
    pWindow->size.x = 10;
    pWindow->size.y = (pWindow->dst->h - h) / 2;
  } else {
    pWindow->size.x = ((Main.event.motion.x + w < pWindow->dst->w) ?
                     (Main.event.motion.x + 10) : (pWindow->dst->w - w - 10));
    pWindow->size.y = 
      ((Main.event.motion.y - (WINDOW_TILE_HIGH + 2) + h < pWindow->dst->h) ?
             (Main.event.motion.y - (WINDOW_TILE_HIGH + 2)) :
             (pWindow->dst->h - h - 10));
    
  }
  
  resize_window(pWindow , NULL, NULL, w, h);
  
  w -= DOUBLE_FRAME_WH;
  
  if (pFind_City_Dlg->pScroll)
  {
    w -= n;
  }
  
  /* exit button */
  pBuf = pWindow->prev;
  
  pBuf->size.x = pWindow->size.x + pWindow->size.w-pBuf->size.w-FRAME_WH-1;
  pBuf->size.y = pWindow->size.y + 1;
  
  /* cities */
  pBuf = pBuf->prev;
  setup_vertical_widgets_position(1,
	pWindow->size.x + FRAME_WH, pWindow->size.y + WINDOW_TILE_HIGH + 2,
	w, 0, pFind_City_Dlg->pBeginActiveWidgetList, pBuf);
  
  if (pFind_City_Dlg->pScroll)
  {
    setup_vertical_scrollbar_area(pFind_City_Dlg->pScroll,
	pWindow->size.x + pWindow->size.w - FRAME_WH,
    	pWindow->size.y + WINDOW_TILE_HIGH + 1,
    	pWindow->size.h - (FRAME_WH + WINDOW_TILE_HIGH + 1), TRUE);
  }
  
  /* -------------------- */
  /* redraw */
  redraw_group(pFind_City_Dlg->pBeginWidgetList, pWindow, 0);
  sdl_dirty_rect(pWindow->size);
  
  flush_dirty();
}
