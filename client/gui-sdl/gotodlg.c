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

#include "fcintl.h"
#include "game.h"
#include "map.h"
#include "packets.h"
#include "player.h"
#include "support.h"
#include "unit.h"

#include "tilespec.h"
#include "clinet.h"
#include "civclient.h"
#include "control.h"
#include "dialogs.h"
#include "colors.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_string.h"
#include "gui_stuff.h"
#include "gui_id.h"
#include "gui_zoom.h"
#include "gui_tilespec.h"
#include "mapview.h"

#include "gotodlg.h"

static struct ADVANCED_DLG *pGotoDlg = NULL;
static Uint32 all_players = 0;
static bool GOTO = TRUE;

static void update_goto_dialog(void);

static int goto_dialog_window_callback(struct GUI *pWindow)
{
  return std_move_window_group_callback(pGotoDlg->pBeginWidgetList, pWindow);
}

static int exit_goto_dialog_callback(struct GUI *pWidget)
{
  popdown_goto_airlift_dialog();
  flush_dirty();
  return -1;
}

static int toggle_goto_nations_cities_dialog_callback(struct GUI *pWidget)
{
  all_players ^= (1u << (get_player(MAX_ID - pWidget->ID)->player_no));
  update_goto_dialog();
  return -1;
}

static int goto_city_callback(struct GUI *pWidget)
{
  struct city *pDestcity = find_city_by_id(MAX_ID - pWidget->ID);

  if (pDestcity) {
    struct unit *pUnit = get_unit_in_focus();
    if (pUnit) {
      if(GOTO) {
        send_goto_unit(pUnit, pDestcity->x, pDestcity->y);
      } else {
	request_unit_airlift(pUnit, pDestcity);
      }
    }
  }
  
  popdown_goto_airlift_dialog();
  flush_dirty();
  return -1;
}

/**************************************************************************
...
**************************************************************************/
static void update_goto_dialog(void)
{
  struct GUI *pBuf = NULL, *pAdd_Dock, *pLast;
  SDL_Surface *pLogo = NULL;
  SDL_String16 *pStr;
  char cBuf[128]; 
  int i, n = 0, owner = 0xffff;  
  
  if(pGotoDlg->pEndActiveWidgetList) {
    pAdd_Dock = pGotoDlg->pEndActiveWidgetList->next;
    pGotoDlg->pBeginWidgetList = pAdd_Dock;
    del_group(pGotoDlg->pBeginActiveWidgetList, pGotoDlg->pEndActiveWidgetList);
    pGotoDlg->pActiveWidgetList = NULL;
  } else {
    pAdd_Dock = pGotoDlg->pBeginWidgetList;
  }
  
  pLast = pAdd_Dock;
  
  for(i = 0; i < game.nplayers; i++) {
    
    if (!TEST_BIT(all_players, game.players[i].player_no)) {
      continue;
    }

    city_list_iterate(game.players[i].cities, pCity) {
      
      /* FIXME: should use unit_can_airlift_to(). */
      if (!GOTO && !pcity->airlift) {
	continue;
      }
      
      my_snprintf(cBuf, sizeof(cBuf), "%s (%d)", pCity->name, pCity->size);
      
      pStr = create_str16_from_char(cBuf, 12);
      pStr->style |= TTF_STYLE_BOLD;
   
      if(pCity->owner != owner) {
        pLogo = GET_SURF(get_nation_by_idx(
			get_player(pCity->owner)->nation)->flag_sprite);
        pLogo = make_flag_surface_smaler(pLogo);
      }
      
      pBuf = create_iconlabel(pLogo, pGotoDlg->pEndWidgetList->dst, pStr, 
    	(WF_DRAW_THEME_TRANSPARENT|WF_DRAW_TEXT_LABEL_WITH_SPACE));
    
      if(pCity->owner != owner) {
        set_wflag(pBuf, WF_FREE_THEME);
        owner = pCity->owner;
      }
      
      pBuf->string16->fgcol =
	    *(get_game_colorRGB(player_color(get_player(pCity->owner))));
      pBuf->string16->render = 3;
      pBuf->string16->bgcol.unused = 128;      
      pBuf->action = goto_city_callback;
      
      if(GOTO || pCity->airlift) {
        set_wstate(pBuf, FC_WS_NORMAL);
      }
      
      assert((MAX_ID - pCity->id) > 0);
      pBuf->ID = MAX_ID - pCity->id;
      
      DownAdd(pBuf, pAdd_Dock);
      pAdd_Dock = pBuf;
      
      if (n > 16)
      {
        set_wflag(pBuf, WF_HIDDEN);
      }
      
      n++;  
    } city_list_iterate_end;
  }

  if(n) {
    pGotoDlg->pBeginActiveWidgetList = pBuf;
    pGotoDlg->pBeginWidgetList = pBuf;
    pGotoDlg->pEndActiveWidgetList = pLast->prev;
    pGotoDlg->pActiveWidgetList = pLast->prev;
    pGotoDlg->pScroll->count = n;
    if (n > 17)
    {
      show_scrollbar(pGotoDlg->pScroll);
      pGotoDlg->pScroll->pScrollBar->size.y =
	      pGotoDlg->pEndWidgetList->size.y + WINDOW_TILE_HIGH + 1 +
				   pGotoDlg->pScroll->pUp_Left_Button->size.h;
      pGotoDlg->pScroll->pScrollBar->size.h = scrollbar_size(pGotoDlg->pScroll);
    } else {
      hide_scrollbar(pGotoDlg->pScroll);
    }
  
    setup_vertical_widgets_position(1,
	pGotoDlg->pEndWidgetList->size.x + FRAME_WH + 1,
        pGotoDlg->pEndWidgetList->size.y + WINDOW_TILE_HIGH + 1,
        pGotoDlg->pScroll->pUp_Left_Button->size.x -
		  	pGotoDlg->pEndWidgetList->size.x - FRAME_WH - 2,
        0, pGotoDlg->pBeginActiveWidgetList, pGotoDlg->pEndActiveWidgetList);
        
  } else {
    hide_scrollbar(pGotoDlg->pScroll);
  }
    
  /* redraw */
  redraw_group(pGotoDlg->pBeginWidgetList, pGotoDlg->pEndWidgetList, 0);
  flush_rect(pGotoDlg->pEndWidgetList->size);
  
}


/**************************************************************************
  Popup a dialog to have the focus unit goto to a city.
**************************************************************************/
static void popup_goto_airlift_dialog(void)
{
  struct GUI *pBuf, *pWindow;
  SDL_String16 *pStr;
  SDL_Surface *pFlag, *pEnabled, *pDisabled;
  SDL_Rect dst;
  SDL_Color color = {0, 0, 0, 96};
  int w = 0, h, i, col, block_x;
  
  if (pGotoDlg) {
    return;
  }
  
  pGotoDlg = MALLOC(sizeof(struct ADVANCED_DLG));
    
  pStr = create_str16_from_char(_("Select destination"), 12);
  pStr->style |= TTF_STYLE_BOLD;
  
  pWindow = create_window(NULL, pStr, 10, 10, WF_DRAW_THEME_TRANSPARENT);
  
  pWindow->action = goto_dialog_window_callback;
  set_wstate(pWindow, FC_WS_NORMAL);
  w = MAX(w, pWindow->size.w);
  
  add_to_gui_list(ID_WINDOW, pWindow);
  pGotoDlg->pEndWidgetList = pWindow;
  
  /* ---------- */
  /* create exit button */
  pBuf = create_themeicon(pTheme->Small_CANCEL_Icon, pWindow->dst,
  			  			WF_DRAW_THEME_TRANSPARENT);
  pBuf->action = exit_goto_dialog_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  w += (pBuf->size.w + 10);
  
  add_to_gui_list(ID_BUTTON, pBuf);
  
  col = 0;
  /* --------------------------------------------- */
  for(i = 0; i < game.nplayers; i++) {
    if(i != game.player_idx
      && pplayer_get_diplstate(
    		game.player_ptr, &game.players[i])->type == DS_NO_CONTACT) {
      continue;
    }
    
    pFlag = make_flag_surface_smaler(
    	GET_SURF(get_nation_by_idx(game.players[i].nation)->flag_sprite));
  
    if (pFlag->w > 15 || pFlag->h > 15) {
      float zoom = (float)(MAX(pFlag->w, pFlag->h)) / 15;
      pEnabled = ZoomSurface(pFlag, zoom, zoom, 1);
      FREESURFACE(pFlag);
      pFlag = pEnabled;
    }
    
    pEnabled = create_icon_theme_surf(pFlag);
    SDL_FillRectAlpha(pFlag, NULL, &color);
    pDisabled = create_icon_theme_surf(pFlag);
    FREESURFACE(pFlag);
    
    pBuf = create_checkbox(pWindow->dst,
      TEST_BIT(all_players, game.players[i].player_no),
    	(WF_FREE_STRING|WF_FREE_THEME|WF_DRAW_THEME_TRANSPARENT|WF_WIDGET_HAS_INFO_LABEL));
    set_new_checkbox_theme(pBuf, pEnabled, pDisabled);
    
    pBuf->string16 = create_str16_from_char(
    			get_nation_by_idx(game.players[i].nation)->name, 12);
    pBuf->string16->style &= ~SF_CENTER;
    set_wstate(pBuf, FC_WS_NORMAL);
    
    pBuf->action = toggle_goto_nations_cities_dialog_callback;
    add_to_gui_list(MAX_ID - i, pBuf);
    col++;  
  }
    
  pGotoDlg->pBeginWidgetList = pBuf;
  
  pGotoDlg->pScroll = MALLOC(sizeof(struct ScrollBar));
  pGotoDlg->pScroll->step = 1;
  pGotoDlg->pScroll->active = 17;
  create_vertical_scrollbar(pGotoDlg, 1, 17, TRUE, TRUE);
  hide_scrollbar(pGotoDlg->pScroll);
  
  w = MAX(w, 300);
  h = 300;
  
  pWindow->size.x = (Main.screen->w - w) / 2;
  pWindow->size.y = (Main.screen->h - h) / 2;
  
  resize_window(pWindow, NULL, NULL, w, h);
  
  col = (col + 10) / 11;
  w = col * pBuf->size.w + (col - 1) * 5 + 10;
  h = pWindow->size.h - WINDOW_TILE_HIGH - 1 - FRAME_WH;
  
  pFlag = ResizeSurface(pTheme->Block, w, h, 1);
  
  block_x = dst.x = pWindow->theme->w - FRAME_WH - pFlag->w;
  dst.y = WINDOW_TILE_HIGH + 1;
  SDL_SetAlpha(pFlag, 0x0, 0x0);
  SDL_BlitSurface(pFlag, NULL, pWindow->theme, &dst);
  FREESURFACE(pFlag);

  /* exit button */
  pBuf = pWindow->prev;
  pBuf->size.x = pWindow->size.x + pWindow->size.w - pBuf->size.w - FRAME_WH - 1;
  pBuf->size.y = pWindow->size.y + 1;

  /* nations buttons */
  pBuf = pBuf->prev;
  i = 0;
  w = pWindow->size.x + block_x + 5;
  h = pWindow->size.y + WINDOW_TILE_HIGH + 6;
  while(pBuf) {
    pBuf->size.x = w;
    pBuf->size.y = h;
       
    if(!((i + 1) % col)) {
      h += pBuf->size.h + 5;
      w = pWindow->size.x + block_x + 5;
    } else {
      w += pBuf->size.w + 5;
    }
    
    if(pBuf == pGotoDlg->pBeginWidgetList) {
      break;
    }
    
    i++;
    pBuf = pBuf->prev;
  }

  setup_vertical_scrollbar_area(pGotoDlg->pScroll,
	pWindow->size.x + block_x,
  	pWindow->size.y + WINDOW_TILE_HIGH + 1,
  	pWindow->size.h - (WINDOW_TILE_HIGH + 1 + FRAME_WH), TRUE);
    
  update_goto_dialog();
}


/**************************************************************************
  Popup a dialog to have the focus unit goto to a city.
**************************************************************************/
void popup_goto_dialog(void)
{
  if (!can_client_issue_orders() || !get_unit_in_focus()) {
    return;
  }
  all_players = (1u << (game.player_ptr->player_no));
  popup_goto_airlift_dialog();
}

/**************************************************************************
  Popup a dialog to have the focus unit airlift to a city.
**************************************************************************/
void popup_airlift_dialog(void)
{
  if (!can_client_issue_orders() || !get_unit_in_focus()) {
    return;
  }
  all_players = (1u << (game.player_ptr->player_no));
  GOTO = FALSE;
  popup_goto_airlift_dialog();
}

/**************************************************************************
  Popdown goto/airlift to a city dialog.
**************************************************************************/
void popdown_goto_airlift_dialog(void)
{
 if(pGotoDlg) {
    popdown_window_group_dialog(pGotoDlg->pBeginWidgetList,
					    pGotoDlg->pEndWidgetList);
    FREE(pGotoDlg->pScroll);
    FREE(pGotoDlg);
  }
  GOTO = TRUE;
}
