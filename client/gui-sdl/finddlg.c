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
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

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
  int orginal_x = ((struct map_position *)pWidget->data)->x;
  int orginal_y = ((struct map_position *)pWidget->data)->y;
      
  popdown_find_dialog();
  
  center_tile_mapcanvas(orginal_x, orginal_y);
  
  flush_dirty();
  return -1;
}

static int find_city_callback(struct GUI *pWidget)
{
  struct city *pCity = (struct city *)pWidget->data;
  if(pCity) {
    center_tile_mapcanvas(pCity->x, pCity->y);
    flush_dirty();
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int up_find_city_dlg_callback(struct GUI *pButton)
{
  up_advanced_dlg(pFind_City_Dlg, pButton->prev);
  
  unsellect_widget_action();
  pSellected_Widget = pButton;
  set_wstate(pButton, WS_SELLECTED);
  redraw_tibutton(pButton);
  flush_rect(pButton->size);
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int down_find_city_dlg_callback(struct GUI *pButton)
{
  down_advanced_dlg(pFind_City_Dlg, pButton->next);
  
  unsellect_widget_action();
  pSellected_Widget = pButton;
  set_wstate(pButton, WS_SELLECTED);
  redraw_tibutton(pButton);
  flush_rect(pButton->size);
  return -1;
}

/**************************************************************************
  FIXME : fix main funct : vertic_scroll_widget_list(...)
**************************************************************************/
static int vscroll_find_city_dlg_callback(struct GUI *pVscrollBar)
{
  vscroll_advanced_dlg(pFind_City_Dlg, pVscrollBar);
  
  unsellect_widget_action();
  set_wstate(pVscrollBar, WS_SELLECTED);
  pSellected_Widget = pVscrollBar;
  redraw_vert(pVscrollBar);
  flush_rect(pVscrollBar->size);
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
  int i, n = 0, w = 0, h , owner = 0xffff,units_h = 0, orginal_x , orginal_y ;
  
  if (pFind_City_Dlg) {
    return;
  }
  
  h = WINDOW_TILE_HIGH + 3 + FRAME_WH;
  
  get_map_xy(Main.map->w/2 , Main.map->h/2 , &orginal_x , &orginal_y);
  
  pFind_City_Dlg = MALLOC(sizeof(struct ADVANCED_DLG));
  
  pStr = create_str16_from_char(_("Find City") , 12);
  pStr->style |= TTF_STYLE_BOLD;
  
  pWindow = create_window(NULL, pStr, 10, 10, WF_DRAW_THEME_TRANSPARENT);
    
  pWindow->action = find_city_window_dlg_callback;
  set_wstate(pWindow , WS_NORMAL);
  w = MAX(w , pWindow->size.w);
  
  add_to_gui_list(ID_TERRAIN_ADV_DLG_WINDOW, pWindow);
  pFind_City_Dlg->pEndWidgetList = pWindow;
  /* ---------- */
  /* exit button */
  pBuf = create_themeicon(ResizeSurface(pTheme->CANCEL_Icon,
			  pTheme->CANCEL_Icon->w - 4,
			  pTheme->CANCEL_Icon->h - 4, 1), pWindow->dst,
  		(WF_FREE_THEME|WF_DRAW_THEME_TRANSPARENT|WF_FREE_DATA));
  SDL_SetColorKey(pBuf->theme ,
	  SDL_SRCCOLORKEY|SDL_RLEACCEL , get_first_pixel(pBuf->theme));
  
  w += pBuf->size.w + 10;
  pBuf->action = exit_find_city_dlg_callback;
  set_wstate(pBuf, WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  pBuf->data = (void *)MALLOC(sizeof(struct map_position));
  ((struct map_position *)pBuf->data)->x = orginal_x;
  ((struct map_position *)pBuf->data)->y = orginal_y;

  add_to_gui_list(ID_TERRAIN_ADV_DLG_EXIT_BUTTON, pBuf);
  /* ---------- */
  
  for(i=0; i<game.nplayers; i++) {
    city_list_iterate(game.players[i].cities, pCity)
    
      my_snprintf(cBuf , sizeof(cBuf), "%s (%d)",pCity->name, pCity->size);
      
      pStr = create_str16_from_char(cBuf , 10);
      pStr->style |= TTF_STYLE_BOLD;
   
      if(pCity->owner != owner ) {
        pLogo = GET_SURF(get_nation_by_idx(
			get_player(pCity->owner)->nation)->flag_sprite);
        pLogo = make_flag_surface_smaler(pLogo);
        owner = pCity->owner;
      }
      pBuf = create_iconlabel(pLogo, pWindow->dst, pStr, 
    	(WF_DRAW_THEME_TRANSPARENT|WF_DRAW_TEXT_LABEL_WITH_SPACE));
    
      pBuf->string16->style &= ~SF_CENTER;
      pBuf->string16->forecol =
	    *(get_game_colorRGB(player_color(get_player(pCity->owner))));
      pBuf->string16->render = 3;
      pBuf->string16->backcol.unused = 128;
    
      pBuf->data = (void *)pCity;
  
      pBuf->action = find_city_callback;
      set_wstate(pBuf , WS_NORMAL);
  
      add_to_gui_list(ID_LABEL , pBuf);
    
      w = MAX(w , pBuf->size.w);
      h += pBuf->size.h;
    
      if (n > 19)
      {
        set_wflag(pBuf , WF_HIDDEN);
      }
      
      n++;  
    city_list_iterate_end;
  }
  
  pFind_City_Dlg->pBeginActiveWidgetList = pBuf;
  pFind_City_Dlg->pEndActiveWidgetList = pWindow->prev->prev;
  pFind_City_Dlg->pActiveWidgetList = pFind_City_Dlg->pEndActiveWidgetList;
  
  
  /* ---------- */
  if (n > 20)
  {
    units_h = 20 * pBuf->size.h + WINDOW_TILE_HIGH + 3 + FRAME_WH;
       
    /* create up button */
    pBuf = create_themeicon_button(pTheme->UP_Icon, pWindow->dst, NULL, 0);
    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);

    pBuf->action = up_find_city_dlg_callback;
    set_wstate(pBuf, WS_NORMAL);

    add_to_gui_list(ID_UNIT_SELLECT_DLG_UP_BUTTON, pBuf);
      
    /* create vsrollbar */
    pBuf = create_vertical(pTheme->Vertic, pWindow->dst,
				    50, WF_DRAW_THEME_TRANSPARENT);
       
    set_wstate(pBuf, WS_NORMAL);
    pBuf->action = vscroll_find_city_dlg_callback;

    add_to_gui_list(ID_UNIT_SELLECT_DLG_VSCROLLBAR, pBuf);

    /* create down button */
    pBuf = create_themeicon_button(pTheme->DOWN_Icon, pWindow->dst, NULL, 0);
      
    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);

    pBuf->action = down_find_city_dlg_callback;
    set_wstate(pBuf, WS_NORMAL);

    add_to_gui_list(ID_UNIT_SELLECT_DLG_DOWN_BUTTON, pBuf);

    w += pBuf->size.w;
       
    pFind_City_Dlg->pScroll = MALLOC(sizeof(struct ScrollBar));
    pFind_City_Dlg->pScroll->active = 20;
    pFind_City_Dlg->pScroll->count = n;
  } else {
    units_h = h;
  }
        
  /* ---------- */
  
  pFind_City_Dlg->pBeginWidgetList = pBuf;
  
  
  w += DOUBLE_FRAME_WH;
  
  h = units_h;
  
  pWindow->size.x = 10;
  pWindow->size.y = (pWindow->dst->h - h) / 2;
#if 0  
  pLogo = get_logo_gfx();  
  if(resize_window( pWindow , pLogo , NULL , w , h )) {
    FREESURFACE(pLogo);
  }
  SDL_SetAlpha(pWindow->theme, 0x0, 0x0);
#endif
/*
  resize_window(pWindow , NULL,
	  get_game_colorRGB(COLOR_STD_BACKGROUND_BROWN), w, h);
*/  
  resize_window(pWindow , NULL, NULL, w, h);
  
  w -= DOUBLE_FRAME_WH;
  
  if (n > 20)
  {
    w -= pBuf->size.w;
  }
  
  
  /* exit button */
  pBuf = pWindow->prev;
  
  pBuf->size.x = pWindow->size.x + pWindow->size.w-pBuf->size.w-FRAME_WH-1;
  pBuf->size.y = pWindow->size.y;
  
  /* terrain info */
  pBuf = pBuf->prev;
  
  pBuf->size.x = pWindow->size.x + FRAME_WH;
  pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 2;
  pBuf->size.w = w;
  
  h = pBuf->size.h;
    
  pBuf = pBuf->prev;
  while(pBuf)
  {
    pBuf->size.w = w;
    pBuf->size.x = pBuf->next->size.x;
    pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
    if (pBuf == pFind_City_Dlg->pBeginActiveWidgetList) break;
    pBuf = pBuf->prev;  
  }
  
  if (n > 20)
  {
    /* up button */
    pBuf = pBuf->prev;
    
    pBuf->size.x = pWindow->size.x + pWindow->size.w - pBuf->size.w - FRAME_WH;
    pBuf->size.y = pFind_City_Dlg->pEndActiveWidgetList->size.y;
    
    pFind_City_Dlg->pScroll->min = pBuf->size.y + pBuf->size.h;
    
    /* scrollbar */
    pBuf = pBuf->prev;
    
    pBuf->size.x = pBuf->next->size.x;
    pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
    
    /* down button */
    pBuf = pBuf->prev;
    
    pBuf->size.x = pWindow->size.x + pWindow->size.w - pBuf->size.w - FRAME_WH;
    pBuf->size.y = pWindow->size.y + pWindow->size.h - FRAME_WH - pBuf->size.h;
    
    pFind_City_Dlg->pScroll->max = pBuf->size.y;
    /* 
       scrollbar high
       10 - units. seen in window.
     */
    pBuf->next->size.h = scrollbar_size(pFind_City_Dlg->pScroll);
  }
  
  /* -------------------- */
  /* redraw */
  redraw_group(pFind_City_Dlg->pBeginWidgetList, pWindow, 0);
  sdl_dirty_rect(pWindow->size);
  
  flush_dirty();
}
