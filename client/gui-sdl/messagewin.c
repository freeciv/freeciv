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
    copyright            : (C) 2003 by Rafa³ Bursig
    email                : Rafa³ Bursig <bursig@poczta.fm>
 **********************************************************************/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

#include "events.h"
#include "fcintl.h"

#include "game.h"
#include "map.h"

#include "gui_mem.h"

#include "packets.h"
#include "player.h"

#include "chatline.h"
#include "citydlg.h"
#include "clinet.h"
#include "colors.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_string.h"
#include "gui_id.h"
#include "gui_stuff.h"
#include "gui_tilespec.h"
#include "mapview.h"
#include "options.h"

#include "messagewin.h"


#define N_MSG_VIEW 7		/* max before scrolling happens */
#define PTSIZE_LOG_FONT		10

static struct ADVANCED_DLG *pMsg_Dlg = NULL;

static void redraw_meswin_dialog(void);

#ifdef UNUSED
/**************************************************************************
  Turn off updating of message window
**************************************************************************/
void meswin_update_delay_on(void)
{
  freelog(LOG_DEBUG, "meswin_update_delay_on : PORT ME");
}

/**************************************************************************
  Turn on updating of message window
**************************************************************************/
void meswin_update_delay_off(void)
{
  /* dissconect_from_server call this */
  freelog(LOG_DEBUG, "meswin_update_delay_off : PORT ME");
}
#endif

/**************************************************************************
 Called from default clicks on a message.
**************************************************************************/
static int msg_callback(struct GUI *pWidget)
{

  struct message *pMsg = (struct message *)pWidget->data;
    
  pWidget->string16->forecol.b = 128;
  unsellect_widget_action();
  
  if (pMsg->city_ok
      && is_city_event(pMsg->event)) {
	
    int x = pMsg->x;
    int y = pMsg->y;
    struct city *pCity = map_get_city(x, y);

    if (center_when_popup_city) {
      center_tile_mapcanvas(x, y);
    }

    if (pCity) {
      /* If the event was the city being destroyed, pcity will be NULL
       * and we'd better not try to pop it up.  In this case, it would
       * be better if the popup button weren't highlighted at all, but
       * that's OK. */
      popup_city_dialog(pCity, FALSE);
    }
    
    if (center_when_popup_city || pCity) {
      flush_dirty();
    }
	
  } else if (pMsg->location_ok) {
    center_tile_mapcanvas(pMsg->x, pMsg->y);
    flush_dirty();
  }

  return -1;
}

static int move_msg_window_callback(struct GUI *pWindow)
{
  /*
  if (move_window_group_dialog(pMsg_Dlg->pBeginWidgetList , pWindow , Main.gui)) {
    flush_rect(pWindow->size);
  }
  */
  return -1;
}

static int up_msg_callback(struct GUI *pWidget)
{
  up_advanced_dlg(pMsg_Dlg, pWidget->prev->prev);
  
  unsellect_widget_action();
  pSellected_Widget = pWidget;
  set_wstate(pWidget, WS_SELLECTED);
  real_redraw_tibutton(pWidget, Main.gui);
  flush_rect(pWidget->size);
  return -1;
}

static int down_msg_callback(struct GUI *pWidget)
{
  down_advanced_dlg(pMsg_Dlg, pWidget->prev);
  unsellect_widget_action();
  pSellected_Widget = pWidget;
  set_wstate(pWidget, WS_SELLECTED);
  real_redraw_tibutton(pWidget, Main.gui);
  flush_rect(pWidget->size);
  return -1;
}

static int vscroll_msg_callback(struct GUI *pWidget)
{
  vscroll_advanced_dlg(pMsg_Dlg, pWidget);
  unsellect_widget_action();
  set_wstate(pWidget, WS_SELLECTED);
  pSellected_Widget = pWidget;
  redraw_vert(pWidget, Main.gui);
  flush_rect(pWidget->size);
  return -1;
}

static int togle_msg_window(struct GUI *pWidget)
{
  struct GUI *pWindow = pMsg_Dlg->pEndWidgetList;
    
  if (get_wflags(pWindow) & WF_HIDDEN) {

    clear_wflag(pWindow, WF_HIDDEN);
    
    if(pMsg_Dlg->pScroll->count > N_MSG_VIEW) {
      clear_wflag(pWindow->prev, WF_HIDDEN);
      clear_wflag(pWindow->prev->prev, WF_HIDDEN);
      clear_wflag(pWindow->prev->prev->prev, WF_HIDDEN);
    }

    refresh_widget_background(pWindow, Main.gui);

    show_group(pMsg_Dlg->pBeginActiveWidgetList,
		pMsg_Dlg->pEndActiveWidgetList);

  } else {    

    hide_group(pMsg_Dlg->pBeginWidgetList, pWindow);

  }

  /* redraw window */
  redraw_meswin_dialog();
  
  if(pWidget) {
    pSellected_Widget = pWidget;
    set_wstate(pWidget, WS_SELLECTED);
    real_redraw_icon(pWidget, Main.gui);
    flush_rect(pWidget->size);
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static void Init_Msg_Window(Sint16 start_x, Sint16 start_y, Uint16 w, Uint16 h)
{
  struct GUI *pWindow = NULL, *pWidget;
#if 1    
  SDL_Surface *pBuf;
  SDL_Rect area;
  SDL_Color color = { 255 , 255, 255, 128 };
#endif  
  SDL_String16 *pStr;
  int min, max;
  
  pMsg_Dlg = MALLOC(sizeof(struct ADVANCED_DLG));
  
#if 0

  pStr = create_str16_from_char(_("Log"), 12);
  pStr->style = TTF_STYLE_BOLD;

  /* create window */
  pWindow = create_window(pStr, w, h, WF_DRAW_THEME_TRANSPARENT);

  pWindow->size.x = start_x;
  pWindow->size.y = start_y;

  resize_window(pWindow, NULL, NULL, w, h);
#else  

  /* create window */
  pWindow = create_window(NULL, w, h, WF_DRAW_THEME_TRANSPARENT);

  pWindow->size.x = start_x;
  pWindow->size.y = start_y;

  pBuf = create_surf(w, h, SDL_SWSURFACE);
  pWindow->theme = SDL_DisplayFormatAlpha(pBuf);
  FREESURFACE(pBuf);
  
  SDL_FillRect(pWindow->theme , NULL, 
    SDL_MapRGBA(pWindow->theme->format , 255, 255, 255, 128 ));
  
  area.x = 0;
  area.y = 0;
  area.w = w;
  area.h = WINDOW_TILE_HIGH;
  SDL_FillRectAlpha(pWindow->theme, &area, &color);
  
  putline(pWindow->theme, 0, WINDOW_TILE_HIGH, w - 1, WINDOW_TILE_HIGH, 0xFF000000);
  
  pStr = create_str16_from_char(_("Log"), 12);
  pStr->style = TTF_STYLE_BOLD;
  pStr->render = 3;
  SDL_GetRGBA(get_first_pixel(pWindow->theme), pWindow->theme->format,
    &pStr->backcol.r, &pStr->backcol.g, &pStr->backcol.b, &pStr->backcol.unused);
    
  pBuf = create_text_surf_from_str16(pStr);
  area.x += 10;
  area.y += ((WINDOW_TILE_HIGH - pBuf->h) / 2);
  SDL_SetAlpha(pBuf, 0x0, 0x0);
  SDL_BlitSurface(pBuf, NULL, pWindow->theme, &area);
  FREESURFACE(pBuf);
  FREESTRING16(pStr);
  
  /*draw_frame(pWindow->theme, 0 , 0, w - 3, h - 3);*/
    
  SDL_SetAlpha(pWindow->theme, 0x0 , 0x0);
#endif
  pWindow->action = move_msg_window_callback;
  set_wstate(pWindow, WS_NORMAL);
  set_wflag(pWindow, WF_HIDDEN);
  add_to_gui_list(ID_CHATLINE_WINDOW, pWindow);
  pMsg_Dlg->pEndWidgetList = pWindow;
  
  /* ------------------------------- */
  /* create up button */
  pWidget = create_themeicon_button(pTheme->UP_Icon, NULL, 0);
  pWidget->size.x = start_x + w - pWidget->size.w - 3;
  pWidget->size.y = start_y + FRAME_WH;
  pWidget->action = up_msg_callback;
  set_wstate(pWidget, WS_NORMAL);
  min = start_y + pWidget->size.h + FRAME_WH;
  set_wflag(pWidget, WF_HIDDEN);
  clear_wflag(pWidget, WF_DRAW_FRAME_AROUND_WIDGET);
  add_to_gui_list(ID_CHATLINE_UP_BUTTON, pWidget);
  /* ----------------------- */
  
  /* create down button */
  pWidget = create_themeicon_button(pTheme->DOWN_Icon, NULL, 0);
  pWidget->size.x = start_x + w - pWidget->size.w - FRAME_WH;
  pWidget->size.y = max = start_y + h - pWidget->size.h - FRAME_WH;
  pWidget->action = down_msg_callback;
  set_wstate(pWidget, WS_NORMAL);
  set_wflag(pWidget, WF_HIDDEN);
  clear_wflag(pWidget, WF_DRAW_FRAME_AROUND_WIDGET);
  add_to_gui_list(ID_CHATLINE_DOWN_BUTTON, pWidget);
  /* ----------------------- */
  
  /* create vsrollbar */
  pWidget =
      create_vertical(pTheme->Vertic, max - min, 0);
  pWidget->size.x = start_x + w - pWidget->size.w - 1;
  pWidget->size.y = min;

  pWidget->action = vscroll_msg_callback;
  
  set_wstate(pWidget, WS_NORMAL);
  
  set_wflag(pWidget, WF_HIDDEN);
  
  add_to_gui_list(ID_CHATLINE_VSCROLLBAR, pWidget);
  
  pMsg_Dlg->pScroll = MALLOC(sizeof(struct ScrollBar));
  pMsg_Dlg->pScroll->max = max;
  pMsg_Dlg->pScroll->min = min;
  pMsg_Dlg->pScroll->count = 0;
  pMsg_Dlg->pScroll->active = N_MSG_VIEW;

  /* ------------------------------------- */
  
  pMsg_Dlg->pBeginWidgetList = pWidget;
  pMsg_Dlg->pBeginActiveWidgetList = pWidget;
  pMsg_Dlg->pEndActiveWidgetList = NULL;
  pMsg_Dlg->pActiveWidgetList = NULL;
  /* ================================================ */

  pWidget = create_themeicon(pTheme->LOG_Icon, 0);
  pWidget->action = togle_msg_window;
  pWidget->key = SDLK_BACKQUOTE;
  add_to_gui_list(ID_CHATLINE_TOGGLE_LOG_WINDOW_BUTTON, pWidget);

}

static void redraw_meswin_dialog(void)
{
  SDL_Rect dst = pMsg_Dlg->pEndWidgetList->size;
  if (get_wflags(pMsg_Dlg->pEndWidgetList) & WF_HIDDEN)
  {
    /* clear */
    SDL_BlitSurface(pMsg_Dlg->pEndWidgetList->gfx, NULL , Main.gui, &dst);
  } else {
    /* redraw */
    redraw_group(pMsg_Dlg->pBeginWidgetList,
		  pMsg_Dlg->pEndWidgetList, Main.gui,0);  
  }
  
  flush_rect(pMsg_Dlg->pEndWidgetList->size);
}

/**************************************************************************
  Do the work of updating (populating) the message dialog.
**************************************************************************/
void real_update_meswin_dialog(void)
{
  int msg_count = get_num_messages();
  int start_x = pMsg_Dlg->pEndWidgetList->size.x + FRAME_WH;
  int i = pMsg_Dlg->pScroll->count;
  struct message *pMsg = NULL;
  struct GUI *pBuf = NULL;
  struct GUI *pMsg_Active = pMsg_Dlg->pBeginActiveWidgetList;
  struct GUI *pMsg_Active_Last = pMsg_Active;
  SDL_String16 *pStr = NULL;
  SDL_Color color = { 255 , 255, 255, 128 };
  bool show = (get_wflags(pMsg_Dlg->pEndWidgetList) & WF_HIDDEN) == 0;
  
  if (i && msg_count <= i) {
    del_group_of_widgets_from_gui_list(pMsg_Dlg->pBeginActiveWidgetList,
					pMsg_Dlg->pEndActiveWidgetList);
    pMsg_Dlg->pBeginActiveWidgetList = pMsg_Dlg->pEndWidgetList->prev->prev->prev;
    pMsg_Dlg->pBeginWidgetList = pMsg_Dlg->pEndWidgetList->prev->prev->prev;
    pMsg_Active = pMsg_Dlg->pBeginActiveWidgetList;
    pMsg_Active_Last = pMsg_Active;
    pMsg_Dlg->pEndActiveWidgetList = NULL;
    /* hide scrollbar */
    set_wflag(pMsg_Dlg->pEndWidgetList->prev, WF_HIDDEN);
    set_wflag(pMsg_Dlg->pEndWidgetList->prev->prev, WF_HIDDEN);
    set_wflag(pMsg_Dlg->pEndWidgetList->prev->prev->prev, WF_HIDDEN);
    pMsg_Dlg->pScroll->count = 0;
    i = 0;
  }
  
  if ( msg_count ) {  
    for(; i<msg_count; i++)
    {
      pMsg = get_message(i);
      pStr = create_str16_from_char(pMsg->descr , 10);
      	
      pBuf = create_iconlabel(NULL, pStr, 
    		(WF_DRAW_THEME_TRANSPARENT|WF_DRAW_TEXT_LABEL_WITH_SPACE));
    
      pBuf->string16->style &= ~SF_CENTER;
      pBuf->string16->backcol = color;
      pBuf->string16->render = 3;
      pBuf->size.x = start_x;
      pBuf->data = (void *)pMsg;	
      pBuf->action = msg_callback;
      if(pMsg->x != -1) {
        set_wstate(pBuf, WS_NORMAL);
	pBuf->string16->forecol.r = 0;
	pBuf->string16->forecol.g = 255;
	pBuf->string16->forecol.b = 0;
      }
      set_wflag(pBuf, WF_HIDDEN);
    
      pBuf->ID = ID_LABEL;
    
      /* add to main widget list in Msg. Dlg. window group */
      pBuf->next = pMsg_Active;
      pBuf->prev = pMsg_Active->prev;
      if (pMsg_Active->prev) {
        pMsg_Active->prev->next = pBuf;
      }
      pMsg_Active->prev = pBuf;
      pMsg_Active = pBuf;
      
      pMsg_Dlg->pScroll->count++;
    
      if(!pMsg_Dlg->pEndActiveWidgetList)
      {
        pMsg_Dlg->pEndActiveWidgetList = pBuf;
        pMsg_Dlg->pActiveWidgetList = pBuf;
      }
    
    }
    pMsg_Dlg->pBeginActiveWidgetList = pBuf;
    pMsg_Dlg->pBeginWidgetList = pBuf;
  
    if(pMsg_Dlg->pScroll->count > N_MSG_VIEW) {
      i = N_MSG_VIEW;
      while(i)
      {
        pMsg_Active = pBuf->next;
        while(pMsg_Active != pMsg_Active_Last)
        {
          pMsg_Active = pMsg_Active->next;
        }
        set_wflag(pMsg_Active, WF_HIDDEN);
        pBuf->size.y = pMsg_Active->size.y;
        pBuf->gfx = pMsg_Active->gfx;
	pMsg_Active->gfx = NULL;
	if(show) {
          clear_wflag(pBuf, WF_HIDDEN);
	}
        pBuf = pBuf->next;
	pMsg_Active_Last = pMsg_Active_Last->next;
        i--;
      }
      pMsg_Dlg->pActiveWidgetList = pBuf->prev;
      if(show) {
        /* show up buton */
        clear_wflag(pMsg_Dlg->pEndWidgetList->prev, WF_HIDDEN);
        /* show down buton */
        clear_wflag(pMsg_Dlg->pEndWidgetList->prev->prev, WF_HIDDEN);
        /* show scrollbar */
        clear_wflag(pMsg_Dlg->pEndWidgetList->prev->prev->prev, WF_HIDDEN);
      }
      /* set new scrollbar high */ 
      pMsg_Dlg->pEndWidgetList->prev->prev->prev->size.h = 
      	scrollbar_size(pMsg_Dlg->pScroll);
      /* set new scrollbar start y pos. */ 
      pMsg_Dlg->pEndWidgetList->prev->prev->prev->size.y = 
      	pMsg_Dlg->pEndWidgetList->size.y /* window y pos */
      	+ pMsg_Dlg->pEndWidgetList->size.h /* window high */
      		- pMsg_Dlg->pEndWidgetList->prev->prev->size.h /* down button high */
      			- pMsg_Dlg->pEndWidgetList->prev->prev->prev->size.h /* scrollbar high */
      			- FRAME_WH;
    } else {
      if(pMsg_Dlg->pScroll->count == 1) {
        pBuf->size.y = pMsg_Dlg->pEndWidgetList->size.y + WINDOW_TILE_HIGH + 2;
	if(show) {
          clear_wflag(pBuf, WF_HIDDEN);
	}
      } else {
        pMsg_Active = pBuf->next;
	while(pMsg_Active != pMsg_Active_Last)
        {
          pMsg_Active = pMsg_Active->next;
        }
	if(pMsg_Active == pMsg_Dlg->pEndWidgetList->prev->prev->prev) {
	  /* first element */
	  pMsg_Active = pMsg_Active->prev;
	  pMsg_Active->size.y = pMsg_Dlg->pEndWidgetList->size.y
					  + WINDOW_TILE_HIGH + 2;
	  if(show) {
            clear_wflag(pMsg_Active, WF_HIDDEN);
	  }  
	}
	pMsg_Active = pMsg_Active->prev;
        while(TRUE)
        {
	  pMsg_Active->size.y = pMsg_Active->next->size.y +
						pMsg_Active->next->size.h;
	  if(show) {
	    clear_wflag(pMsg_Active, WF_HIDDEN);
	  }
	  if(pMsg_Active == pBuf) break;
	  pMsg_Active = pMsg_Active->prev;
        }
      }
    }
  }
  redraw_meswin_dialog();
  
}

/**************************************************************************
  Popup (or raise) the message dialog; typically triggered by '~'.
**************************************************************************/
void popup_meswin_dialog(void)
{
   
  if(!pMsg_Dlg) {
    Init_Msg_Window((Main.gui->w - 520) / 2, 25 , 520, 124);
  }
  
  if (get_wflags(pMsg_Dlg->pEndWidgetList) & WF_HIDDEN)
  {
    clear_wflag(pMsg_Dlg->pEndWidgetList, WF_HIDDEN);
  }
  
  refresh_widget_background(pMsg_Dlg->pEndWidgetList, Main.gui);
  
  real_update_meswin_dialog();
  
}

/**************************************************************************
  Return whether the message dialog is open.
**************************************************************************/
bool is_meswin_open(void)
{
  return (pMsg_Dlg != NULL) &&
	  !(get_wflags(pMsg_Dlg->pEndWidgetList) & WF_HIDDEN);
}

/**************************************************************************
  center message dialog after screen resize (redraw if show).
**************************************************************************/
void center_meswin_dialog(void)
{
  int dx; 
  struct GUI *pBuf;
  if(!pMsg_Dlg) {
    return;
  }
  
  dx = pMsg_Dlg->pEndWidgetList->size.x;
  pMsg_Dlg->pEndWidgetList->size.x = (Main.gui->w - 
  					pMsg_Dlg->pEndWidgetList->size.w) / 2;
  dx = pMsg_Dlg->pEndWidgetList->size.x - dx;
  
  pBuf = pMsg_Dlg->pEndWidgetList->prev;
  
  while(TRUE) {
    pBuf->size.x += dx;
    if(pBuf == pMsg_Dlg->pBeginWidgetList) break;
    pBuf = pBuf->prev;  
  }
  
/*  refresh_widget_background(pMsg_Dlg->pEndWidgetList, Main.gui); */
  
  if (!(get_wflags(pMsg_Dlg->pEndWidgetList) & WF_HIDDEN))
  {
    redraw_meswin_dialog();
  }
  
}
