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
                          messagewin.c  -  description
                             -------------------
    begin                : Feb 2 2003
    copyright            : (C) 2003 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
 **********************************************************************/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <SDL/SDL.h>

/* utility */
#include "fcintl.h"

/* client */
#include "options.h"

/* gui-sdl */
#include "citydlg.h"
#include "colors.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "gui_tilespec.h"
#include "mapview.h"
#include "themecolors.h"

#include "messagewin.h"


#ifdef SMALL_SCREEN
#define N_MSG_VIEW               3    /* max before scrolling happens */
#else
#define N_MSG_VIEW		 6          
#endif

#define PTSIZE_LOG_FONT		adj_font(10)

static struct ADVANCED_DLG *pMsg_Dlg = NULL;

/**************************************************************************
 Called from default clicks on a message.
**************************************************************************/
static int msg_callback(struct widget *pWidget)
{

  struct message *pMsg = (struct message *)pWidget->data.ptr;
    
  pWidget->string16->fgcol.r += 128;
  unsellect_widget_action();
  
  if (pMsg->city_ok
      && is_city_event(pMsg->event)) {
	
    struct city *pCity = tile_get_city(pMsg->tile);

    if (center_when_popup_city) {
      center_tile_mapcanvas(pMsg->tile);
    }

    if (pCity) {
      /* If the event was the city being destroyed, pcity will be NULL
       * and we'd better not try to pop it up.  In this case, it would
       * be better if the popup button weren't highlighted at all, but
       * that's OK. */
      popup_city_dialog(pCity);
    }
    
    if (center_when_popup_city || pCity) {
      flush_dirty();
    }
	
  } else if (pMsg->location_ok) {
    center_tile_mapcanvas(pMsg->tile);
    flush_dirty();
  }

  return -1;
}

/**************************************************************************
 Called from default clicks on a messages window.
**************************************************************************/
static int move_msg_window_callback(struct widget *pWindow)
{
  return std_move_window_group_callback(pMsg_Dlg->pBeginWidgetList, pWindow);
}

/* ======================================================================
				Public
   ====================================================================== */

/**************************************************************************
 ...
**************************************************************************/
void real_update_meswin_dialog(void)
{
  int msg_count = get_num_messages();
  int i = pMsg_Dlg->pScroll->count;
    
  struct message *pMsg = NULL;
  struct widget *pBuf = NULL, *pWindow = pMsg_Dlg->pEndWidgetList;
  SDL_String16 *pStr = NULL;

  bool create;
  int w = pWindow->size.w - pTheme->FR_Vert->w - (pTheme->FR_Vert->w * 2) -
			  pMsg_Dlg->pScroll->pUp_Left_Button->size.w;
  
  if (i && msg_count <= i) {
    del_group_of_widgets_from_gui_list(pMsg_Dlg->pBeginActiveWidgetList,
					pMsg_Dlg->pEndActiveWidgetList);
    pMsg_Dlg->pBeginActiveWidgetList = NULL;
    pMsg_Dlg->pEndActiveWidgetList = NULL;
    pMsg_Dlg->pActiveWidgetList = NULL;
    /* hide scrollbar */
    hide_scrollbar(pMsg_Dlg->pScroll);
    pMsg_Dlg->pScroll->count = 0;
    i = 0;
  }
  create = (i == 0);

  if (msg_count) {
    for(; i<msg_count; i++)
    {
      pMsg = get_message(i);
      pStr = create_str16_from_char(pMsg->descr , PTSIZE_LOG_FONT);
      	
      pBuf = create_iconlabel(NULL, pWindow->dst, pStr, 
    		(WF_DRAW_THEME_TRANSPARENT|WF_DRAW_TEXT_LABEL_WITH_SPACE));
    
      pBuf->string16->bgcol = (SDL_Color) {0, 0, 0, 0};
      
      pBuf->size.w = w;
      pBuf->data.ptr = (void *)pMsg;	
      pBuf->action = msg_callback;
      if(pMsg->tile) {
        set_wstate(pBuf, FC_WS_NORMAL);
	pBuf->string16->fgcol = *get_game_colorRGB(COLOR_THEME_MESWIN_ACTIVE_TEXT);
      }
      
      pBuf->ID = ID_LABEL;
      
      /* add to widget list */
      if(create) {
        add_widget_to_vertical_scroll_widget_list(pMsg_Dlg,
				pBuf, pWindow, FALSE,
				pWindow->size.x + pTheme->FR_Vert->w,
		      		pWindow->size.y + WINDOW_TITLE_HEIGHT + adj_size(2));
	 create = FALSE;
      } else {
	add_widget_to_vertical_scroll_widget_list(pMsg_Dlg,
				pBuf, pMsg_Dlg->pBeginActiveWidgetList, FALSE,
				pWindow->size.x + pTheme->FR_Vert->w,
		      		pWindow->size.y + WINDOW_TITLE_HEIGHT + adj_size(2));
      }
      
      
    } /* for */
  } /* if */

  redraw_group(pMsg_Dlg->pBeginWidgetList, pWindow, 0);
  flush_rect(pWindow->size, FALSE);
}

/**************************************************************************
  Popup (or raise) the message dialog; typically triggered by 'F10'.
**************************************************************************/
void popup_meswin_dialog(bool raise)
{
  SDL_Color bg_color = {255 , 255, 255, 128};
  SDL_Color title_bg_color = {255 , 255, 255, 200};

  SDL_String16 *pStr = create_str16_from_char("M", PTSIZE_LOG_FONT);
  Sint16 start_x = (Main.screen->w - adj_size(520)) / 2;
  Sint16 start_y = adj_size(25);
  Uint16 w = adj_size(520);
  Uint16 h = adj_size(8) + WINDOW_TITLE_HEIGHT + N_MSG_VIEW * str16size(pStr).h;
  int len, i = 0;
  struct message *pMsg = NULL;
  struct widget *pWindow = NULL, *pBuf = NULL;
  int msg_count = get_num_messages();
  SDL_Surface *pSurf;
  SDL_Rect area;
  
  FREESTRING16(pStr);
  
  if(pMsg_Dlg) {
    return;
  }
  
  pMsg_Dlg = fc_calloc(1, sizeof(struct ADVANCED_DLG));

  /* create window */
  pWindow = create_window(NULL, NULL, w, h, 0);

  pWindow->size.x = start_x;
  pWindow->size.y = start_y;
  set_window_pos(pWindow, pWindow->size.x, pWindow->size.y);

  pWindow->theme = create_surf_alpha(w, h, SDL_SWSURFACE);
  
  SDL_FillRect(pWindow->theme , NULL, map_rgba(pWindow->theme->format, bg_color));
  
  area.x = 0;
  area.y = 0;
  area.w = w;
  area.h = WINDOW_TITLE_HEIGHT;
  
  /* fill title bar */
  SDL_FillRect(pWindow->theme, &area, map_rgba(pWindow->theme->format, title_bg_color));
  
  putline(pWindow->theme, 0, WINDOW_TITLE_HEIGHT, w - 1, WINDOW_TITLE_HEIGHT,
                                map_rgba(pWindow->theme->format, *get_game_colorRGB(COLOR_THEME_MESWIN_FRAME)));

  /* create static text on window */
  pStr = create_str16_from_char(_("Log"), adj_font(12));
  pStr->style = TTF_STYLE_BOLD;
  pStr->bgcol = (SDL_Color) {0, 0, 0, 0};
    
  pSurf = create_text_surf_from_str16(pStr);
  area.x += adj_size(10);
  area.y += ((WINDOW_TITLE_HEIGHT - pSurf->h) / 2);
  alphablit(pSurf, NULL, pWindow->theme, &area);
  FREESURFACE(pSurf);
  FREESTRING16(pStr);
  
  putframe(pWindow->theme, 0, 0, w - 1, h - 1,
    map_rgba(pWindow->theme->format, *get_game_colorRGB(COLOR_THEME_MESWIN_FRAME)));
  
  clear_wflag(pWindow, WF_DRAW_FRAME_AROUND_WIDGET);
  pWindow->action = move_msg_window_callback;
  set_wstate(pWindow, FC_WS_NORMAL);
  add_to_gui_list(ID_CHATLINE_WINDOW, pWindow);
  pMsg_Dlg->pEndWidgetList = pWindow;
  
  /* ------------------------------- */
  
  if (msg_count) {
    for(i=0; i<msg_count; i++)
    {
      pMsg = get_message(i);
      pStr = create_str16_from_char(pMsg->descr , PTSIZE_LOG_FONT);
      	
      pBuf = create_iconlabel(NULL, pWindow->dst, pStr, 
    		(WF_DRAW_THEME_TRANSPARENT|WF_DRAW_TEXT_LABEL_WITH_SPACE));
    
      pBuf->string16->bgcol = (SDL_Color) {0, 0, 0, 0};
      pBuf->size.x = start_x;
      pBuf->size.w = w;
      pBuf->data.ptr = (void *)pMsg;	
      pBuf->action = msg_callback;
      if(pMsg->tile) {
        set_wstate(pBuf, FC_WS_NORMAL);
	pBuf->string16->fgcol = *get_game_colorRGB(COLOR_THEME_MESWIN_ACTIVE_TEXT2);
      }
      
      if(i>N_MSG_VIEW-1) {
        set_wflag(pBuf, WF_HIDDEN);
      }
      
      add_to_gui_list(ID_LABEL, pBuf);
    }
    pMsg_Dlg->pEndActiveWidgetList = pWindow->prev;
    pMsg_Dlg->pBeginActiveWidgetList = pBuf;
    pMsg_Dlg->pBeginWidgetList = pBuf;
    
  } else {
    pMsg_Dlg->pBeginWidgetList = pWindow;
  }
  
  len = create_vertical_scrollbar(pMsg_Dlg, 1, N_MSG_VIEW, TRUE, TRUE);
  setup_vertical_scrollbar_area(pMsg_Dlg->pScroll,
		start_x + w - 1, start_y + 1, h - adj_size(2), TRUE);
  
  if(i>N_MSG_VIEW-1) {
    /* find pActiveWidgetList to draw last seen part of list */
    /* pBuf her has pointer to last created widget */
    pBuf = pMsg_Dlg->pBeginActiveWidgetList;
    for(i = 0; i < N_MSG_VIEW; i++) {
      clear_wflag(pBuf, WF_HIDDEN);
      pBuf = pBuf->next;
    }
    pMsg_Dlg->pActiveWidgetList = pBuf->prev;
    /* hide others (not-seen) widgets */
    while(pBuf != pMsg_Dlg->pEndActiveWidgetList->next) {
      set_wflag(pBuf, WF_HIDDEN);
      pBuf = pBuf->next;
    }
    /* set new scrollbar position */
    pMsg_Dlg->pScroll->pScrollBar->size.y = pMsg_Dlg->pScroll->max -
				    pMsg_Dlg->pScroll->pScrollBar->size.h;
  } else {
    hide_scrollbar(pMsg_Dlg->pScroll);
  }
    
  len = w - pTheme->FR_Vert->w - (pTheme->FR_Vert->w * 2) - len;
  		
  /* ------------------------------------- */
  
  if (msg_count) {
    /* find if scrollbar is active */
    if(pMsg_Dlg->pActiveWidgetList) {
      pBuf = pMsg_Dlg->pActiveWidgetList;
    } else {
      pBuf = pMsg_Dlg->pEndActiveWidgetList;
    }
    
    setup_vertical_widgets_position(1,
	start_x + pTheme->FR_Vert->w, start_y + WINDOW_TITLE_HEIGHT + adj_size(2), len, 0,
	pMsg_Dlg->pBeginActiveWidgetList, pBuf);
  }

  redraw_group(pMsg_Dlg->pBeginWidgetList,
		  pMsg_Dlg->pEndWidgetList, 0);
  flush_all();
  
}

/**************************************************************************
  Popdown the messages dialog; called by void popdown_all_game_dialogs(void)
**************************************************************************/
void popdown_meswin_dialog(void)
{
  if(pMsg_Dlg) {
    popdown_window_group_dialog(pMsg_Dlg->pBeginWidgetList,
				  pMsg_Dlg->pEndWidgetList);
    FC_FREE(pMsg_Dlg->pScroll);
    FC_FREE(pMsg_Dlg);
  }
  
}

/**************************************************************************
  Return whether the message dialog is open.
**************************************************************************/
bool is_meswin_open(void)
{
  return (pMsg_Dlg != NULL);
}
