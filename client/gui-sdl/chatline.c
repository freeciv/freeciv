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
                          chatline.c  -  description
                             -------------------
    begin                : Sun Jun 30 2002
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL/SDL.h>

#include "fcintl.h"

#include "gui_mem.h"

#include "packets.h"
#include "support.h"

#include "climisc.h"
#include "clinet.h"
#include "civclient.h"

#include "colors.h"
#include "graphics.h"
#include "unistring.h"
#include "gui_string.h"
#include "gui_id.h"
#include "gui_stuff.h"
#include "gui_zoom.h"
#include "gui_main.h"
#include "gui_tilespec.h"
#include "mapview.h"
#include "messagewin.h"
#include "chatline.h"

#include "connectdlg.h"

#define PTSIZE_LOG_FONT 10

struct CONNLIST {
  struct ADVANCED_DLG *pUsers_Dlg;
  struct ADVANCED_DLG *pChat_Dlg;
  struct GUI *pBeginWidgetList;
  struct GUI *pEndWidgetList;
  struct GUI *pStart;
  struct GUI *pConfigure;
  struct GUI *pEdit;
  int text_width;
  int active;
} *pConnDlg = NULL;

static bool popdown_conn_list_dialog(void);
static void popup_conn_list_dialog(void);
static void add_to_chat_list(Uint16 *pUniStr, size_t n_alloc);

/**************************************************************************
  Sent msg/command from imput dlg to server
**************************************************************************/
static int inputline_return_callback(struct GUI *pWidget)
{
  char *theinput = NULL;

  if (!pWidget->string16->text) {
    return -1;
  }

  theinput = convert_to_chars(pWidget->string16->text);

  if (theinput && *theinput) {
    send_chat(theinput);

    real_append_output_window(theinput);
    FREE(theinput);
  }
  
  return -1;
}

/**************************************************************************
  This function is main chat/command client input.
**************************************************************************/
void popup_input_line(void)
{
  int w = 400;
  int h = 30;
  struct GUI *pInput_Edit;
    
  pInput_Edit = create_edit_from_unichars(NULL, NULL, NULL, 0, 18, w, 0);
  lock_buffer(pInput_Edit->dst);/* always on top */
  
  pInput_Edit->size.x = (Main.screen->w - w) / 2;
  
  if (h > pInput_Edit->size.h) {
    pInput_Edit->size.h = h;
  }

  pInput_Edit->size.y = Main.screen->h - 2 * pInput_Edit->size.h;
  
  
  if(edit(pInput_Edit) != ED_ESC) {
    inputline_return_callback(pInput_Edit);
  }
  
  sdl_dirty_rect(pInput_Edit->size);
  FREEWIDGET(pInput_Edit);
  remove_locked_buffer();
  
  flush_dirty();
}

/**************************************************************************
  Appends the string to the chat output window.  The string should be
  inserted on its own line, although it will have no newline.
**************************************************************************/
void real_append_output_window(const char *astring, int conn_id)
{
  /* Currently this is a wrapper to the message subsystem. */
  if (pConnDlg) {
    Uint16 *pUniStr;
    size_t n = strlen(astring);
    
    n += 1;
    n *= 2;
    pUniStr = MALLOC(n);
    convertcopy_to_utf16(pUniStr, n, astring);
    add_to_chat_list(pUniStr, n);
  } else {
    char message[MAX_LEN_MSG];
    my_snprintf(message , MAX_LEN_MSG, "%s" , astring);
    
    add_notify_window(message, -1, -1, E_NOEVENT);
  }
}

/**************************************************************************
  Get the text of the output window, and call write_chatline_content() to
  log it.
**************************************************************************/
void log_output_window(void)
{
  /* TODO */
}

/**************************************************************************
  Clear all text from the output window.
**************************************************************************/
void clear_output_window(void)
{
  /* TODO */
}

/* ====================================================================== */

/**************************************************************************
  ...
**************************************************************************/
static int conn_dlg_callback(struct GUI *pWindow)
{
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int disconnect_conn_callback(struct GUI *pWidget)
{
  popdown_conn_list_dialog();
  flush_dirty();
  disconnect_from_server();
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static void add_to_chat_list(Uint16 *pUniStr, size_t n_alloc)
{
  SDL_String16 *pStr;
  struct GUI *pBuf, *pWindow = pConnDlg->pEndWidgetList;
  SDL_Color bg = {0, 0, 0, 0};
  
  assert(pUniStr != NULL);
  assert(n_alloc != 0);
  
  pStr = create_string16(pUniStr, n_alloc, 12);
   
  if (convert_string_to_const_surface_width(pStr, pConnDlg->text_width - 5)) {
    SDL_String16 *pStr2;
    int count = 0;
    Uint16 **UniTexts = create_new_line_unistrings(pStr->text);
    
    while (UniTexts[count]) {
      pStr2 = create_string16(UniTexts[count],
      					unistrlen(UniTexts[count]) + 1, 12);
      pStr2->render = 3;
      pStr2->bgcol = bg;
      pBuf = create_themelabel2(NULL, pWindow->dst,
  		pStr2, pConnDlg->text_width, 0,
		 (WF_DRAW_THEME_TRANSPARENT|WF_DRAW_TEXT_LABEL_WITH_SPACE));
      
      pBuf->size.w = pConnDlg->text_width;
      add_widget_to_vertical_scroll_widget_list(pConnDlg->pChat_Dlg, pBuf,
			pConnDlg->pChat_Dlg->pBeginActiveWidgetList, FALSE,
			pWindow->size.x + 10 + 60 + 10,
		      	pWindow->size.y + 14);
      count++;
    }
    redraw_group(pConnDlg->pChat_Dlg->pBeginWidgetList,
    			pConnDlg->pChat_Dlg->pEndWidgetList, TRUE);
    FREESTRING16(pStr);
  } else {
    pStr->render = 3;
    pStr->bgcol = bg;
    pBuf = create_themelabel2(NULL, pWindow->dst,
  		pStr, pConnDlg->text_width, 0,
		 (WF_DRAW_THEME_TRANSPARENT|WF_DRAW_TEXT_LABEL_WITH_SPACE));
    
    pBuf->size.w = pConnDlg->text_width;
  
    if (add_widget_to_vertical_scroll_widget_list(pConnDlg->pChat_Dlg, pBuf,
			pConnDlg->pChat_Dlg->pBeginActiveWidgetList, FALSE,
			pWindow->size.x + 10 + 60 + 10,
		      	pWindow->size.y + 14)) {
      redraw_group(pConnDlg->pChat_Dlg->pBeginWidgetList,
    			pConnDlg->pChat_Dlg->pEndWidgetList, TRUE);
    } else {
      redraw_widget(pBuf);
      sdl_dirty_rect(pBuf->size);
    }
  }
  
  flush_dirty();
  
}

/**************************************************************************
  ...
**************************************************************************/
static int input_edit_conn_callback(struct GUI *pWidget)
{
 
  if (pWidget->string16->text) {
    char theinput[256];
    
    convertcopy_to_chars(theinput, sizeof(theinput), pWidget->string16->text);
  
  
    if (*theinput != '\0') {
      send_chat(theinput);
      /*real_append_output_window(theinput);*/
    }
    
    FREE(pWidget->string16->text);
    pWidget->string16->n_alloc = 0;
  }
  return -1;
}

/**************************************************************************
 ...
**************************************************************************/
static int start_game_callback(struct GUI *pWidget)
{
  send_chat("/start");
  return -1;
}

/**************************************************************************
 ...
**************************************************************************/
static int server_config_callback(struct GUI *pWidget)
{

  return -1;
}

/**************************************************************************
 Update the connected users list at pregame state.
**************************************************************************/
void update_conn_list_dialog(void)
{
  
  if (get_client_state() == CLIENT_PRE_GAME_STATE) {
    if (pConnDlg) {
      SDL_Color bg = {0, 0, 0, 0};
      struct GUI *pBuf = NULL, *pWindow = pConnDlg->pEndWidgetList;
      SDL_String16 *pStr = create_string16(NULL, 0, 12);
      bool create;
      
      pStr->render = 3;
      pStr->bgcol = bg;
    
      if (pConnDlg->pUsers_Dlg) {
        del_group(pConnDlg->pUsers_Dlg->pBeginActiveWidgetList,
				pConnDlg->pUsers_Dlg->pEndActiveWidgetList);
        pConnDlg->pUsers_Dlg->pActiveWidgetList = NULL;
        pConnDlg->pUsers_Dlg->pBeginWidgetList =
      				pConnDlg->pUsers_Dlg->pScroll->pScrollBar;
        pConnDlg->pUsers_Dlg->pScroll->count = 0;
      } else {
        pConnDlg->pUsers_Dlg = MALLOC(sizeof(struct ADVANCED_DLG));
        pConnDlg->pUsers_Dlg->pEndWidgetList = pConnDlg->pBeginWidgetList;
        pConnDlg->pUsers_Dlg->pBeginWidgetList = pConnDlg->pBeginWidgetList;
      
        pConnDlg->pUsers_Dlg->pScroll = MALLOC(sizeof(struct ScrollBar));
        pConnDlg->pUsers_Dlg->pScroll->count = 0;
        create_vertical_scrollbar(pConnDlg->pUsers_Dlg, 1,
					pConnDlg->active, TRUE, TRUE);	
        pConnDlg->pUsers_Dlg->pEndWidgetList =
				pConnDlg->pUsers_Dlg->pEndWidgetList->prev;
        setup_vertical_scrollbar_area(pConnDlg->pUsers_Dlg->pScroll,
			pWindow->size.x + pWindow->size.w - 29 - FRAME_WH,
        		pWindow->size.y + 14, pWindow->size.h - 44, FALSE);
      }
    
      hide_scrollbar(pConnDlg->pUsers_Dlg->pScroll);
      create = TRUE;
      conn_list_iterate(game.est_connections, pconn) {
      
        copy_chars_to_string16(pStr, pconn->username);
      
        pBuf = create_themelabel2(NULL, pWindow->dst, pStr, 100, 0,
		(WF_DRAW_THEME_TRANSPARENT|WF_DRAW_TEXT_LABEL_WITH_SPACE));
        clear_wflag(pBuf, WF_FREE_STRING);
      
        pBuf->ID = ID_LABEL;
      
        /* add to widget list */
        if(create) {
          add_widget_to_vertical_scroll_widget_list(pConnDlg->pUsers_Dlg,
			pBuf, pConnDlg->pUsers_Dlg->pBeginWidgetList, FALSE,
			pWindow->size.x + pWindow->size.w - 130 - FRAME_WH,
		      		pWindow->size.y + 14);
	  create = FALSE;
        } else {
	  add_widget_to_vertical_scroll_widget_list(pConnDlg->pUsers_Dlg,
		pBuf, pConnDlg->pUsers_Dlg->pBeginActiveWidgetList, FALSE,
		pWindow->size.x + pWindow->size.w - 130 - FRAME_WH,
	      		pWindow->size.y + 14);
        }
            
      } conn_list_iterate_end;
  
      pConnDlg->pBeginWidgetList = pConnDlg->pUsers_Dlg->pBeginWidgetList;
      FREESTRING16(pStr);

      if (aconnection.access_level == ALLOW_CTRL
         || aconnection.access_level == ALLOW_HACK) {
        set_wstate(pConnDlg->pStart, FC_WS_NORMAL);
	set_wstate(pConnDlg->pConfigure, FC_WS_NORMAL);
      } else {
        set_wstate(pConnDlg->pStart, FC_WS_DISABLED);
	set_wstate(pConnDlg->pConfigure, FC_WS_DISABLED);
      }
          
      /* redraw */
      redraw_group(pConnDlg->pBeginWidgetList, pConnDlg->pEndWidgetList, 0);

      flush_rect(pConnDlg->pEndWidgetList->size);
    } else {
      popup_conn_list_dialog();
    }
  } else {
    if (popdown_conn_list_dialog()) {
      flush_dirty();
    }
  }
}

/**************************************************************************
  ...
**************************************************************************/
static void popup_conn_list_dialog(void)
{
  struct GUI *pWindow = NULL, *pBuf = NULL;
  SDL_String16 *pStr = NULL;
  int n;
    
  if (pConnDlg || !aconnection.established) {
    return;
  }
  
  popdown_meswin_dialog();
  
  pConnDlg = MALLOC(sizeof(struct CONNLIST));
    
  pWindow = create_window(NULL, NULL, 10, 10, 0);
  pWindow->action = conn_dlg_callback;
  set_wstate(pWindow, FC_WS_NORMAL);
  clear_wflag(pWindow, WF_DRAW_FRAME_AROUND_WIDGET);
  
  pConnDlg->pEndWidgetList = pWindow;
  add_to_gui_list(ID_WINDOW, pWindow);
  
  pWindow->size.x = 0;
  pWindow->size.y = 0;
  
  /* create window background */
  {
    SDL_Rect area;
    SDL_Color color = {255, 255, 255, 96};
    SDL_Surface *pSurf = get_logo_gfx();
    
    if (resize_window(pWindow, pSurf, NULL, Main.screen->w, Main.screen->h)) {
      FREESURFACE(pSurf);
    }
        
    n = pWindow->size.w - 130 - FRAME_WH - (10 + 60 + 10 + 30);
    pConnDlg->text_width = n;
    
    /* draw lists backgrounds */
    area.x = 10 + 60 + 10;
    area.y = 14;
    area.w = n + 20;
    area.h = pWindow->size.h - 44;
    SDL_FillRectAlpha(pWindow->theme, &area, &color);
    putframe(pWindow->theme, area.x - 1, area.y - 1, area.x + area.w,
  					area.y + area.h, 0xFFFFFFFF);
    
    area.x = pWindow->size.w - 130 - FRAME_WH;
    area.y = 14;
    area.w = 120;
    area.h = pWindow->size.h - 44;
    SDL_FillRectAlpha(pWindow->theme, &area, &color);
    putframe(pWindow->theme, area.x - 1, area.y - 1, area.x + area.w,
  					area.y + area.h, 0xFFFFFFFF);
    
    draw_frame(pWindow->theme, 0, 0, pWindow->theme->w, pWindow->theme->h);
  }
    
  /* -------------------------------- */
  
  pConnDlg->pChat_Dlg = MALLOC(sizeof(struct ADVANCED_DLG));
    
  n = conn_list_size(&game.est_connections);
  
  {  
    char cBuf[256];   
    my_snprintf(cBuf, sizeof(cBuf), _("Total users logged in : %d"), n);
    pStr = create_str16_from_char(cBuf, 12);
  }
  
  pStr->render = 3;
  pStr->bgcol.r = 0;
  pStr->bgcol.g = 0;
  pStr->bgcol.b = 0;
  pStr->bgcol.unused = 0;
  
  pBuf = create_themelabel2(NULL, pWindow->dst,
  		pStr, pConnDlg->text_width, 0,
		 (WF_DRAW_THEME_TRANSPARENT|WF_DRAW_TEXT_LABEL_WITH_SPACE));
        
  pBuf->size.x = pWindow->size.x + 10 + 60 + 10;
  pBuf->size.y = pWindow->size.y + 14;
  pBuf->size.w = pConnDlg->text_width;
  
  add_to_gui_list(ID_LABEL, pBuf);
      
  pConnDlg->pChat_Dlg->pBeginWidgetList = pBuf;
  pConnDlg->pChat_Dlg->pEndWidgetList = pBuf;
  pConnDlg->pChat_Dlg->pBeginActiveWidgetList = pBuf;
  pConnDlg->pChat_Dlg->pEndActiveWidgetList = pBuf;
  
  pConnDlg->pChat_Dlg->pScroll = MALLOC(sizeof(struct ScrollBar));
  pConnDlg->pChat_Dlg->pScroll->count = 1;
  
  n = (pWindow->size.h - 44) / pBuf->size.h;
  pConnDlg->active = n;
  
  create_vertical_scrollbar(pConnDlg->pChat_Dlg, 1,
  					pConnDlg->active, TRUE, TRUE);	
      
  setup_vertical_scrollbar_area(pConnDlg->pChat_Dlg->pScroll,
  		pWindow->size.x + 10 + 60 + 10 + pConnDlg->text_width + 1,
		pWindow->size.y + 14, pWindow->size.h - 44, FALSE);
  hide_scrollbar(pConnDlg->pChat_Dlg->pScroll);  
  /* -------------------------------- */
  
  pBuf = create_themeicon_button_from_chars(NULL, pWindow->dst,
  				_("Start\nGame"), 12, 0);
  pBuf->size.w = 60;
  pBuf->size.h = 60;
  pBuf->size.x = pWindow->size.x + 10;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - 4 * (pBuf->size.h + 10);
  pConnDlg->pStart = pBuf;
  pBuf->action = start_game_callback;
  add_to_gui_list(ID_BUTTON, pBuf);
  
  pBuf = create_themeicon_button_from_chars(NULL, pWindow->dst,
  				_("Server\nSettings"), 12, 0);
  pBuf->size.w = 60;
  pBuf->size.h = 60;
  pBuf->size.x = pWindow->size.x + 10;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - 3 * (pBuf->size.h + 10);
  pConnDlg->pConfigure = pBuf;
  pBuf->action = server_config_callback;
  add_to_gui_list(ID_BUTTON, pBuf);
  
  pBuf = create_themeicon_button_from_chars(NULL, pWindow->dst,
  				_("Tabs"), 12, 0);
  pBuf->size.w = 60;
  pBuf->size.h = 60;
  pBuf->size.x = pWindow->size.x + 10;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - 2 * (pBuf->size.h + 10);
  /*pBuf->action = client_config_callback;
  set_wstate(pBuf, FC_WS_NORMAL);*/
  add_to_gui_list(ID_BUTTON, pBuf);
  
  pBuf = create_themeicon_button_from_chars(NULL, pWindow->dst,
  				_("Quit"), 12, 0);
  pBuf->size.w = 60;
  pBuf->size.h = 60;
  pBuf->size.x = pWindow->size.x + 10;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - (pBuf->size.h + 10);
  pBuf->action = disconnect_conn_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  add_to_gui_list(ID_BUTTON, pBuf);

  pBuf = create_edit_from_unichars(NULL, pWindow->dst,
  		NULL, 0, 12, pConnDlg->text_width + 120,
			(WF_DRAW_THEME_TRANSPARENT|WF_EDIT_LOOP));
    
  pBuf->size.x = pWindow->size.x + 10 + 60 + 10;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - (pBuf->size.h + 5);
  pBuf->action = input_edit_conn_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pConnDlg->pEdit = pBuf;
  add_to_gui_list(ID_EDIT, pBuf);
  
  pBuf = create_themeicon_button_from_chars(NULL, pWindow->dst,
  				"?", 12, 0);
  pBuf->size.y = pWindow->size.y + pWindow->size.h - (pBuf->size.h + 7); 
  pBuf->size.x = pWindow->size.x + pWindow->size.w - (pBuf->size.w + 10) - 5;
  
  /*pBuf->action = client_config_callback;
  set_wstate(pBuf, FC_WS_NORMAL);*/
  add_to_gui_list(ID_BUTTON, pBuf);
    
  pConnDlg->pBeginWidgetList = pBuf;
  /* ------------------------------------------------------------ */
  
  update_conn_list_dialog();
}

/**************************************************************************
  ...
**************************************************************************/
static bool popdown_conn_list_dialog(void)
{
  if (pConnDlg) {
    
    if (get_wstate(pConnDlg->pEdit) == FC_WS_PRESSED) {
      force_exit_from_event_loop();
    }
    
    popdown_window_group_dialog(pConnDlg->pBeginWidgetList,
			                pConnDlg->pEndWidgetList);
    if (pConnDlg->pUsers_Dlg) {
      FREE(pConnDlg->pUsers_Dlg->pScroll);
      FREE(pConnDlg->pUsers_Dlg);
    }
    
    if (pConnDlg->pChat_Dlg) {
      FREE(pConnDlg->pChat_Dlg->pScroll);
      FREE(pConnDlg->pChat_Dlg);
    }
    
    FREE(pConnDlg);
    return TRUE;
  }
  
  return FALSE;
}
