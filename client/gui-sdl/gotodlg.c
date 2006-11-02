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
#include "unitlist.h"

/* client */
#include "civclient.h"
#include "control.h"
#include "goto.h"

/* gui-sdl */
#include "colors.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_tilespec.h"
#include "gui_zoom.h"
#include "mapview.h"
#include "widget.h"

#include "gotodlg.h"

static struct ADVANCED_DLG *pGotoDlg = NULL;
static Uint32 all_players = 0;
static bool GOTO = TRUE;

static void update_goto_dialog(void);

static int goto_dialog_window_callback(struct widget *pWindow)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    move_window_group(pGotoDlg->pBeginWidgetList, pWindow);
  }
  return -1;
}

static int exit_goto_dialog_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    popdown_goto_airlift_dialog();
    flush_dirty();
  }
  return -1;
}

static int toggle_goto_nations_cities_dialog_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    all_players ^= (1u << (get_player(MAX_ID - pWidget->ID)->player_no));
    update_goto_dialog();
  }
  return -1;
}

static int goto_city_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    struct city *pDestcity = find_city_by_id(MAX_ID - pWidget->ID);
  
    if (pDestcity) {
      struct unit *pUnit = unit_list_get(get_units_in_focus(), 0);
      if (pUnit) {
        if(GOTO) {
          send_goto_tile(pUnit, pDestcity->tile);
        } else {
          request_unit_airlift(pUnit, pDestcity);
        }
      }
    }
    
    popdown_goto_airlift_dialog();
    flush_dirty();
  }
  return -1;
}

/**************************************************************************
...
**************************************************************************/
static void update_goto_dialog(void)
{
  struct widget *pBuf = NULL, *pAdd_Dock, *pLast;
  SDL_Surface *pLogo = NULL;
  SDL_String16 *pStr;
  char cBuf[128]; 
  int i, n = 0/*, owner = 0xffff*/;  
  struct player *owner = NULL;
  
  if(pGotoDlg->pEndActiveWidgetList) {
    pAdd_Dock = pGotoDlg->pEndActiveWidgetList->next;
    pGotoDlg->pBeginWidgetList = pAdd_Dock;
    del_group(pGotoDlg->pBeginActiveWidgetList, pGotoDlg->pEndActiveWidgetList);
    pGotoDlg->pActiveWidgetList = NULL;
  } else {
    pAdd_Dock = pGotoDlg->pBeginWidgetList;
  }
  
  pLast = pAdd_Dock;
  
  for(i = 0; i < game.info.nplayers; i++) {
    
    if (!TEST_BIT(all_players, game.players[i].player_no)) {
      continue;
    }

    city_list_iterate(game.players[i].cities, pCity) {
      
      /* FIXME: should use unit_can_airlift_to(). */
      if (!GOTO && !pCity->airlift) {
	continue;
      }
      
      my_snprintf(cBuf, sizeof(cBuf), "%s (%d)", pCity->name, pCity->size);
      
      pStr = create_str16_from_char(cBuf, 12);
      pStr->style |= TTF_STYLE_BOLD;
   
      if(pCity->owner != owner) {
        pLogo = GET_SURF(get_nation_flag_sprite(tileset, 
			get_player(pCity->owner->player_no)->nation));
        pLogo = make_flag_surface_smaler(pLogo);
      }
      
      pBuf = create_iconlabel(pLogo, pGotoDlg->pEndWidgetList->dst, pStr, 
    	(WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE));
    
      if(pCity->owner != owner) {
        set_wflag(pBuf, WF_FREE_THEME);
        owner = pCity->owner;
      }
      
      pBuf->string16->fgcol =
	    *(get_player_color(tileset, city_owner(pCity))->color);	  	  
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
	      pGotoDlg->pEndWidgetList->size.y + WINDOW_TITLE_HEIGHT + 1 +
				   pGotoDlg->pScroll->pUp_Left_Button->size.h;
      pGotoDlg->pScroll->pScrollBar->size.h = scrollbar_size(pGotoDlg->pScroll);
    } else {
      hide_scrollbar(pGotoDlg->pScroll);
    }
  
    setup_vertical_widgets_position(1,
	pGotoDlg->pEndWidgetList->size.x + pTheme->FR_Left->w + 1,
        pGotoDlg->pEndWidgetList->size.y + WINDOW_TITLE_HEIGHT + 1,
        pGotoDlg->pScroll->pUp_Left_Button->size.x -
          pGotoDlg->pEndWidgetList->size.x - pTheme->FR_Right->w - adj_size(2),
        0, pGotoDlg->pBeginActiveWidgetList, pGotoDlg->pEndActiveWidgetList);
        
  } else {
    hide_scrollbar(pGotoDlg->pScroll);
  }
    
  /* redraw */
  redraw_group(pGotoDlg->pBeginWidgetList, pGotoDlg->pEndWidgetList, 0);
  widget_flush(pGotoDlg->pEndWidgetList);
  
}


/**************************************************************************
  Popup a dialog to have the focus unit goto to a city.
**************************************************************************/
static void popup_goto_airlift_dialog(void)
{
  SDL_Color bg_color = {0, 0, 0, 96};
  
  struct widget *pBuf, *pWindow;
  SDL_String16 *pStr;
  SDL_Surface *pFlag, *pEnabled, *pDisabled;
  SDL_Rect dst;
  int w = 0, h, i, col, block_x;
  
  if (pGotoDlg) {
    return;
  }
  
  pGotoDlg = fc_calloc(1, sizeof(struct ADVANCED_DLG));
    
  pStr = create_str16_from_char(_("Select destination"), adj_font(12));
  pStr->style |= TTF_STYLE_BOLD;
  
  pWindow = create_window(NULL, pStr, 1, 1, 0);
  
  pWindow->action = goto_dialog_window_callback;
  set_wstate(pWindow, FC_WS_NORMAL);
  w = MAX(w, pWindow->size.w);
  
  add_to_gui_list(ID_WINDOW, pWindow);
  pGotoDlg->pEndWidgetList = pWindow;
  
  /* ---------- */
  /* create exit button */
  pBuf = create_themeicon(pTheme->Small_CANCEL_Icon, pWindow->dst,
  			  			WF_RESTORE_BACKGROUND);
  pBuf->action = exit_goto_dialog_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  w += (pBuf->size.w + adj_size(10));
  
  add_to_gui_list(ID_BUTTON, pBuf);
  
  col = 0;
  /* --------------------------------------------- */
  for(i = 0; i < game.info.nplayers; i++) {
    if(i != game.info.player_idx
      && pplayer_get_diplstate(
    		game.player_ptr, &game.players[i])->type == DS_NO_CONTACT) {
      continue;
    }
    
    pFlag = make_flag_surface_smaler(
    	GET_SURF(get_nation_flag_sprite(tileset, game.players[i].nation)));
  
    if (pFlag->w > 15 || pFlag->h > 15) {
      float zoom = (float)(MAX(pFlag->w, pFlag->h)) / 15;
      pEnabled = ZoomSurface(pFlag, zoom, zoom, 1);
      FREESURFACE(pFlag);
      pFlag = pEnabled;
    }
    
    pEnabled = create_icon_theme_surf(pFlag);
    SDL_FillRectAlpha(pFlag, NULL, &bg_color);
    pDisabled = create_icon_theme_surf(pFlag);
    FREESURFACE(pFlag);
    
    pBuf = create_checkbox(pWindow->dst,
      TEST_BIT(all_players, game.players[i].player_no),
    	(WF_FREE_STRING|WF_FREE_THEME|WF_RESTORE_BACKGROUND|WF_WIDGET_HAS_INFO_LABEL));
    set_new_checkbox_theme(pBuf, pEnabled, pDisabled);
    
    pBuf->string16 = create_str16_from_char(
    			game.players[i].nation->name, adj_font(12));
    pBuf->string16->style &= ~SF_CENTER;
    set_wstate(pBuf, FC_WS_NORMAL);
    
    pBuf->action = toggle_goto_nations_cities_dialog_callback;
    add_to_gui_list(MAX_ID - i, pBuf);
    col++;  
  }
    
  pGotoDlg->pBeginWidgetList = pBuf;
  
  pGotoDlg->pScroll = fc_calloc(1, sizeof(struct ScrollBar));
  pGotoDlg->pScroll->step = 1;
  pGotoDlg->pScroll->active = 17;
  create_vertical_scrollbar(pGotoDlg, 1, 17, TRUE, TRUE);
  hide_scrollbar(pGotoDlg->pScroll);
  
  w = MAX(w, adj_size(300));
  h = adj_size(300);

  widget_set_position(pWindow,
                      (Main.screen->w - w) / 2,
                      (Main.screen->h - h) / 2);
  
  resize_window(pWindow, NULL, NULL, w, h);
  
  col = (col + adj_size(10)) / adj_size(11);
  w = col * pBuf->size.w + (col - 1) * 5 + adj_size(10);
  h = pWindow->size.h - WINDOW_TITLE_HEIGHT - 1 - pTheme->FR_Bottom->h;
  
  pFlag = ResizeSurface(pTheme->Block, w, h, 1);
  
  block_x = dst.x = pWindow->theme->w - pTheme->FR_Right->w - pFlag->w;
  dst.y = WINDOW_TITLE_HEIGHT + 1;
  alphablit(pFlag, NULL, pWindow->theme, &dst);
  FREESURFACE(pFlag);

  /* exit button */
  pBuf = pWindow->prev;
  pBuf->size.x = pWindow->size.x + pWindow->size.w - pBuf->size.w - pTheme->FR_Right->w - 1;
  pBuf->size.y = pWindow->size.y + 1;

  /* nations buttons */
  pBuf = pBuf->prev;
  i = 0;
  w = pWindow->size.x + block_x + adj_size(5);
  h = pWindow->size.y + WINDOW_TITLE_HEIGHT + adj_size(6);
  while(pBuf) {
    pBuf->size.x = w;
    pBuf->size.y = h;
       
    if(!((i + 1) % col)) {
      h += pBuf->size.h + adj_size(5);
      w = pWindow->size.x + block_x + adj_size(5);
    } else {
      w += pBuf->size.w + adj_size(5);
    }
    
    if(pBuf == pGotoDlg->pBeginWidgetList) {
      break;
    }
    
    i++;
    pBuf = pBuf->prev;
  }

  setup_vertical_scrollbar_area(pGotoDlg->pScroll,
	pWindow->size.x + block_x,
  	pWindow->size.y + WINDOW_TITLE_HEIGHT + 1,
  	pWindow->size.h - (WINDOW_TITLE_HEIGHT + 1 + pTheme->FR_Bottom->h), TRUE);
    
  update_goto_dialog();
}


/**************************************************************************
  Popup a dialog to have the focus unit goto to a city.
**************************************************************************/
void popup_goto_dialog(void)
{
  if (!can_client_issue_orders() || !unit_list_get(get_units_in_focus(), 0)) {
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
  if (!can_client_issue_orders() || !unit_list_get(get_units_in_focus(), 0)) {
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
    FC_FREE(pGotoDlg->pScroll);
    FC_FREE(pGotoDlg);
  }
  GOTO = TRUE;
}
