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
                          connectdlg.c  -  description
                             -------------------
    begin                : Mon Jul 1 2002
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafa³ Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

#include "fcintl.h"
#include "log.h"
#include "support.h"
#include "version.h"

#include "civclient.h"
#include "chatline.h"
#include "clinet.h"
#include "colors.h"
#include "dialogs.h"

#include "gui_mem.h"

#include "graphics.h"
#include "gui_string.h"
#include "gui_stuff.h"
#include "gui_tilespec.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_zoom.h"
#include "mapview.h"
#include "optiondlg.h"
#include "connectdlg.h"

static struct ADVANCED_DLG *pMeta_Severs = NULL;
static struct server_list *pServer_list = NULL;

static int connect_callback(struct GUI *pWidget);
static int convert_portnr_callback(struct GUI *pWidget);
static int convert_playername_callback(struct GUI *pWidget);
static int convert_servername_callback(struct GUI *pWidget);
static int popup_join_game_callback(struct GUI *pWidget);
/**************************************************************************
  ...
**************************************************************************/
static int connect_callback(struct GUI *pWidget)
{
  char errbuf[512];
  /*struct GUI *pTmp = NULL; */

  if (connect_to_server(player_name, server_host, server_port,
			errbuf, sizeof(errbuf)) != -1) {
			  
    /* clear dlg area */			  
    SDL_FillRect(Main.gui, (SDL_Rect *)pWidget->data, 0x0);
    add_refresh_rect(*((SDL_Rect *)pWidget->data));
    FREE(pWidget->data);
			  
    /* destroy connect dlg. widgets */
    del_widget_from_gui_list(pWidget);
    del_ID_from_gui_list(ID_META_SERVERS_BUTTON);
    del_ID_from_gui_list(ID_CANCEL_BUTTON);
    del_ID_from_gui_list(ID_PLAYER_NAME_EDIT);
    del_ID_from_gui_list(ID_SERVER_NAME_EDIT);
    del_ID_from_gui_list(ID_PORT_EDIT);

    /* setup Option Icon */
    pSellected_Widget =
	get_widget_pointer_form_main_list(ID_CLIENT_OPTIONS);
    pSellected_Widget->size.x = 5;
    pSellected_Widget->size.y = 5;
    set_wstate(pSellected_Widget, WS_NORMAL);
    clear_wflag(pSellected_Widget, WF_HIDDEN);
    real_redraw_icon( pSellected_Widget , Main.gui );

    if (get_client_state() != CLIENT_GAME_RUNNING_STATE) {
      flush_dirty();
    }


#ifdef UNUSED
    SDL_Delay(100);

    /* check game status */
    pTmp = get_widget_pointer_form_main_list(ID_WAITING_LABEL);

    if (pTmp) {			/* game not started */

      /* draw intro screen */
      draw_intro_gfx();

      /* set x,y to Options icon and draw to screen */
      draw_icon(pSellected_Widget, 5, 5);

      refresh_widget_background(pTmp);
      redraw_label(pTmp);

      refresh_fullscreen();
    }
#endif

    pSellected_Widget = NULL;

    return -1;
  } else {
    append_output_window(errbuf);

    /* button up */
    unsellect_widget_action();
    set_wstate(pWidget, WS_SELLECTED);
    pSellected_Widget = pWidget;
    redraw_tibutton(pWidget);
    flush_rect(pWidget->size);
  }
  return -1;
}
/* ======================================================== */

static int meta_severs_window_callback(struct GUI *pWindow)
{
  return -1;
}

static int exit_meta_severs_dlg_callback(struct GUI *pWidget)
{
  popdown_window_group_dialog(pMeta_Severs->pBeginWidgetList,
				  pMeta_Severs->pEndWidgetList, Main.gui);
  FREE(pMeta_Severs->pScroll);
  FREE(pMeta_Severs);
  flush_dirty();
  delete_server_list(pServer_list);
  popup_join_game_callback(NULL);
  return -1;
}

static int sellect_meta_severs_callback(struct GUI *pWidget)
{
  struct server *pServer = (struct server *)pWidget->data;
      
  sz_strlcpy(server_host, pServer->name);
  sscanf(pServer->port, "%d", &server_port);
  
  exit_meta_severs_dlg_callback(NULL);
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int up_meta_severs_callback(struct GUI *pButton)
{
  struct GUI *pBegin = up_scroll_widget_list(pButton->prev,
			pMeta_Severs->pScroll,
			pMeta_Severs->pActiveWidgetList,
			pMeta_Severs->pBeginActiveWidgetList,
			pMeta_Severs->pEndActiveWidgetList);

  if (pBegin) {
    pMeta_Severs->pActiveWidgetList = pBegin;
  }
  
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
static int down_meta_severs_callback(struct GUI *pButton)
{
  struct GUI *pBegin = down_scroll_widget_list(pButton->next,
			pMeta_Severs->pScroll,
			pMeta_Severs->pActiveWidgetList,
			pMeta_Severs->pBeginActiveWidgetList,
			pMeta_Severs->pEndActiveWidgetList);

  if (pBegin) {
    pMeta_Severs->pActiveWidgetList = pBegin;
  }

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
static int vscroll_meta_severs_callback(struct GUI *pVscrollBar)
{

  struct GUI *pBegin = vertic_scroll_widget_list(pVscrollBar,
			pMeta_Severs->pScroll,
			pMeta_Severs->pActiveWidgetList,
			pMeta_Severs->pBeginActiveWidgetList,
			pMeta_Severs->pEndActiveWidgetList);

  if (pBegin) {
    pMeta_Severs->pActiveWidgetList = pBegin;
  }
  
  unsellect_widget_action();
  set_wstate(pVscrollBar, WS_SELLECTED);
  pSellected_Widget = pVscrollBar;
  redraw_vert(pVscrollBar, Main.screen);
  flush_rect(pVscrollBar->size);

  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int meta_severs_callback(struct GUI *pWidget)
{
  char errbuf[128];
  char cBuf[512];
  int w = 0, h = 0, count = 0, meta_h;
  struct GUI *pBuf, *pWindow;
  SDL_Surface *pLogo;
  SDL_Color col = {255, 255, 255, 128};
  SDL_Rect area;
  
  if(pMeta_Severs) {
    return -1;
  }
  
  /* clear dlg area */			  
  SDL_FillRect(Main.gui, (SDL_Rect *)pWidget->data, 0x0);
  flush_rect(*((SDL_Rect *)pWidget->data));
  FREE(pWidget->data);
			  
  /* destroy connect dlg. widgets */
  del_widget_from_gui_list(pWidget);
  del_ID_from_gui_list(ID_CONNECT_BUTTON);
  del_ID_from_gui_list(ID_CANCEL_BUTTON);
  del_ID_from_gui_list(ID_PLAYER_NAME_EDIT);
  del_ID_from_gui_list(ID_SERVER_NAME_EDIT);
  del_ID_from_gui_list(ID_PORT_EDIT);
  
  pServer_list = create_server_list(errbuf, sizeof(errbuf));
  
  if(!pServer_list) {
    popup_join_game_callback(NULL);
    append_output_window(errbuf);
    return -1;
  }

  
  pMeta_Severs = MALLOC(sizeof(struct ADVANCED_DLG));
    
  pWindow = create_window(NULL, 10 , 10 , 0);
  pWindow->action = meta_severs_window_callback;
  set_wstate(pWindow, WS_NORMAL);
  clear_wflag(pWindow, WF_DRAW_FRAME_AROUND_WIDGET);
  
  add_to_gui_list(ID_WINDOW, pWindow);
  pMeta_Severs->pEndWidgetList = pWindow;
  /* ---------------------- */
  pBuf = create_themeicon_button_from_chars(pTheme->META_Icon,
					_("Refresh"), 14, 0);
					
  /*pBuf->action = refresh_meta_severs_callback;*/
  
  /*set_wstate(pMeta, WS_NORMAL);*/
  
  add_to_gui_list(ID_BUTTON, pBuf);
  /* ------------------------------ */
  pBuf = create_themeicon_button_from_chars(pTheme->CANCEL_Icon,
						     _("Cancel"), 14, 0);
  pBuf->action = exit_meta_severs_dlg_callback;
  set_wstate(pBuf, WS_NORMAL);
  add_to_gui_list(ID_BUTTON, pBuf);
  /* ------------------------------ */
  
  server_list_iterate(*pServer_list,pServer) {
    
    my_snprintf(cBuf, sizeof(cBuf), "%s Port %s Ver: %s %s %s %s\n%s",
    	pServer->name, pServer->port, pServer->version, _(pServer->status),
    		_("Players"), pServer->players, pServer->metastring);

    pBuf = create_iconlabel_from_chars(NULL, cBuf, 10,
		WF_DRAW_TEXT_LABEL_WITH_SPACE|WF_DRAW_THEME_TRANSPARENT);
    
    pBuf->action = sellect_meta_severs_callback;
    set_wstate(pBuf, WS_NORMAL);
    pBuf->data = (void *)pServer;
    w = MAX(w, pBuf->size.w);
    h = MAX(h, pBuf->size.h);
    
    add_to_gui_list(ID_BUTTON, pBuf);
    count++;
    
    if(count > 10) {
      set_wflag(pBuf, WF_HIDDEN);
    }
    
  }
  server_list_iterate_end;
  pMeta_Severs->pBeginWidgetList = pBuf;
  pMeta_Severs->pBeginActiveWidgetList = pBuf;
  pMeta_Severs->pEndActiveWidgetList = pWindow->prev->prev->prev;
  pMeta_Severs->pActiveWidgetList = pWindow->prev->prev->prev;
  
  if (count > 10) {
    meta_h = 10 * h;
       
    /* create up button */
    pBuf = create_themeicon_button(pTheme->UP_Icon, NULL, 0);
    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);

    pBuf->action = up_meta_severs_callback;
    set_wstate(pBuf, WS_NORMAL);

    add_to_gui_list(ID_BUTTON, pBuf);
      
    /* create vsrollbar */
    pBuf = create_vertical(pTheme->Vertic, 50, WF_DRAW_THEME_TRANSPARENT);
       
    set_wstate(pBuf, WS_NORMAL);
    pBuf->action = vscroll_meta_severs_callback;

    add_to_gui_list(ID_SCROLLBAR, pBuf);

    /* create down button */
    pBuf = create_themeicon_button(pTheme->DOWN_Icon, NULL, 0);
       
    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);

    pBuf->action = down_meta_severs_callback;
    set_wstate(pBuf, WS_NORMAL);

    add_to_gui_list(ID_BUTTON, pBuf);

    w += pBuf->size.w;
       
    pMeta_Severs->pScroll = MALLOC(sizeof(struct ScrollBar));
    pMeta_Severs->pScroll->active = 10;
    pMeta_Severs->pScroll->count = count;
    
    pMeta_Severs->pBeginWidgetList = pBuf;
    
  } else {
    meta_h = count * h;
  }
  
  w += 20;
  area.h = meta_h;
  
  meta_h += pMeta_Severs->pEndWidgetList->prev->size.h + 10 + 20;
  
  pWindow->size.x = (Main.gui->w - w) /2;
  pWindow->size.y = (Main.gui->h - meta_h) /2;
  
  pLogo = get_logo_gfx();
  if (resize_window( pWindow , pLogo , NULL , w , meta_h )) {
    FREESURFACE(pLogo);
  }
  
  w -= 20;
  
  area.w = w + 1;
  
  if(count > 10) {
    w -= pBuf->size.w;
  }
  
  /* refresh button */
  pBuf = pWindow->prev;
  
  pBuf->size.x = pWindow->size.x + 10;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 10;
  
  /* exit button */
  pBuf = pBuf->prev;
  
  pBuf->size.x = pWindow->size.x + pWindow->size.w - pBuf->size.w - 10;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 10;
  
  /* meta labels */
  pBuf = pBuf->prev;
  
  pBuf->size.x = pWindow->size.x + 10;
  pBuf->size.y = pWindow->size.y + 10;
  pBuf->size.w = w;
  pBuf->size.h = h;
  
  pBuf = pBuf->prev;
  while( pBuf )
  {
        
    pBuf->size.w = w;
    pBuf->size.h = h;
    pBuf->size.x = pBuf->next->size.x;
    pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
        
    if ( pBuf == pMeta_Severs->pBeginActiveWidgetList ) break;
    pBuf = pBuf->prev;  
   
  }
  
  if ( count > 10 )
  {
    /* up button */
    pBuf = pBuf->prev;
    
    pBuf->size.x = pWindow->size.x + pWindow->size.w - pBuf->size.w - 9;
    pBuf->size.y = pMeta_Severs->pEndActiveWidgetList->size.y;
    
    pMeta_Severs->pScroll->min = pBuf->size.y + pBuf->size.h;
    
    /* scrollbar */
    pBuf = pBuf->prev;
    
    pBuf->size.x = pBuf->next->size.x;
    pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
    
    /* down button */
    pBuf = pBuf->prev;
    
    pBuf->size.x = pWindow->size.x + pWindow->size.w - pBuf->size.w - 9;
    pBuf->size.y = pWindow->size.y + pWindow->size.h - 20 - pBuf->size.h -
                  pWindow->prev->size.h;
    
    
    pMeta_Severs->pScroll->max = pBuf->size.y;
    
    /* 
       scrollbar high
     */
    pBuf->next->size.h = scrollbar_size(pMeta_Severs->pScroll);
    
  }
  
  /* -------------------- */
  /* redraw */
  
  redraw_window(pWindow, Main.gui);
  
  area.x = pMeta_Severs->pEndActiveWidgetList->size.x;
  area.y = pMeta_Severs->pEndActiveWidgetList->size.y;
  
  SDL_FillRectAlpha(Main.gui, &area, &col);
  
  putframe(Main.gui, area.x - 1, area.y - 1, 
  		area.x + area.w , area.y + area.h , 0xff000000);
  
  redraw_group(pMeta_Severs->pBeginWidgetList, pWindow->prev, Main.gui,0);

  putframe(Main.gui, pWindow->size.x , pWindow->size.y , 
  		pWindow->size.x + pWindow->size.w - 1,
  		pWindow->size.y + pWindow->size.h - 1,
  		0xffffffff);
    
  flush_rect(pWindow->size);
  
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int convert_playername_callback(struct GUI *pWidget)
{
  char *tmp = convert_to_chars(pWidget->string16->text);
  sz_strlcpy(player_name, tmp);
  FREE(tmp);
  return -1;
}

/**************************************************************************
...
**************************************************************************/
static int convert_servername_callback(struct GUI *pWidget)
{
  char *tmp = convert_to_chars(pWidget->string16->text);
  sz_strlcpy(server_host, tmp);
  FREE(tmp);
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int convert_portnr_callback(struct GUI *pWidget)
{
  char *tmp = convert_to_chars(pWidget->string16->text);
  sscanf(tmp, "%d", &server_port);
  FREE(tmp);
  return -1;
}

static int cancel_connect_dlg_callback(struct GUI *pWidget)
{
  
  struct GUI *pEnd = pWidget->next->next->next->next->next;
    
  SDL_FillRect(Main.gui, (SDL_Rect *)pWidget->data, 0x0);
  
  FREE(pWidget->data);
  
  del_group_of_widgets_from_gui_list(pWidget,pEnd);
  
  gui_server_connect();
  return -1;
}

/*
  This function is one big tmp solution and should be full redrawed !!
*/
static int popup_join_game_callback(struct GUI *pWidget)
{
  char pCharPort[6];
  struct GUI *pUser = NULL, *pServer = NULL, *pPort = NULL;
  struct GUI *pConnect = NULL, *pMeta = NULL, *pCancel = NULL;
  SDL_String16 *pPlayer_name = NULL;
  SDL_String16 *pServer_name = NULL;
  SDL_String16 *pPort_nr = NULL;
  SDL_Color color = {255, 255, 255, 255};
  SDL_Surface *pLogo, *pTmp;
  SDL_Rect *area = MALLOC(sizeof(SDL_Rect));
  
  int start_x;			/* = 215; */
  int start_y;

  int start_button_y;
  int start_button_connect_x;	/* = 165; */
  int start_button_meta_x;	/* = 270; */
  int start_button_cancel_x;	/* = 415; */
  
  if(pWidget) {
    /* popdown start buttons */  
    SDL_FillRect(Main.gui, (SDL_Rect *)pWidget->data, 0x0);
  
    FREE(pWidget->data);
  
    del_group_of_widgets_from_gui_list(pWidget->prev->prev,
  					pWidget->next->next);
  }
  /* -------------------------- */

  pPlayer_name = create_str16_from_char(_("Player Name :"), 10);
  pPlayer_name->forecol = color;
  pServer_name = create_str16_from_char(_("Freeciv Serwer :"), 10);
  pServer_name->forecol = color;
  pPort_nr = create_str16_from_char(_("Port :"), 10);
  pPort_nr->forecol = color;
  
  disable(ID_CLIENT_OPTIONS);

  /* ====================== INIT =============================== */
  my_snprintf(pCharPort, sizeof(pCharPort), "%d", server_port);

  pConnect = create_themeicon_button_from_chars(pTheme->OK_Icon,
						     _("Connect"), 14, 0);

  pConnect->action = connect_callback;
  set_wstate(pConnect, WS_NORMAL);
  pConnect->data = (void *)area;
  
  add_to_gui_list(ID_CONNECT_BUTTON, pConnect);
  /* ------------------------------ */

  pMeta = create_themeicon_button_from_chars(pTheme->META_Icon,
					_("Metaserver"), 14, 0);
					
  pMeta->action = meta_severs_callback;
  pMeta->data = (void *)area;
  set_wstate(pMeta, WS_NORMAL);
  
  add_to_gui_list(ID_META_SERVERS_BUTTON, pMeta);
  /* ------------------------------ */
  
  pCancel = create_themeicon_button_from_chars(pTheme->CANCEL_Icon,
						     _("Cancel"), 14, 0);
  pCancel->action = cancel_connect_dlg_callback;
  pCancel->data = (void *)area;
  set_wstate(pCancel, WS_NORMAL);
  add_to_gui_list(ID_CANCEL_BUTTON, pCancel);
  
  /* ------------------------------ */

  pUser = create_edit_from_chars(NULL, player_name, 14, 210,
					 WF_DRAW_THEME_TRANSPARENT);
  pUser->action = convert_playername_callback;
  set_wstate(pUser, WS_NORMAL);
  add_to_gui_list(ID_PLAYER_NAME_EDIT, pUser );

  /* ------------------------------ */
  
  pServer = create_edit_from_chars(NULL, server_host, 14, 210,
					 WF_DRAW_THEME_TRANSPARENT);

  pServer->action = convert_servername_callback;
  set_wstate(pServer, WS_NORMAL);
  add_to_gui_list(ID_SERVER_NAME_EDIT, pServer);

  /* ------------------------------ */

  pPort = create_edit_from_chars(NULL, pCharPort, 14, 210,
					 WF_DRAW_THEME_TRANSPARENT);
					 
  pPort->action = convert_portnr_callback;
  set_wstate(pPort, WS_NORMAL);
  add_to_gui_list(ID_PORT_EDIT, pPort);
  

  /* ==================== Draw first time ===================== */
  
  pLogo = get_logo_gfx();
  pTmp = ResizeSurface(pLogo, 400, 250, 1);
  FREESURFACE(pLogo);
  
  area->x = (Main.gui->w - 400)/ 2;
  area->y = (Main.gui->h - 250)/ 2 + 40;
  SDL_BlitSurface(pTmp, NULL, Main.gui, area);
  FREESURFACE(pTmp);

  putframe(Main.gui, area->x, area->y, area->x + 399, area->y + 249, 0xffffffff);

  area->w = 400;
  area->h = 250;
  /* ---------------------------------------- */
  
  start_x = area->x + (area->w - pUser->size.w) / 2;
  start_y = area->y + 35;
  
  write_text16(Main.gui, start_x + 5, start_y - 13, pPlayer_name);
  draw_edit(pUser, start_x, start_y);
  write_text16(Main.gui, start_x + 5, start_y - 13 + 15 +
	       pUser->size.h, pServer_name);
  draw_edit(pServer, start_x, start_y + 15 + pUser->size.h);
  write_text16(Main.gui, start_x + 5, start_y - 13 + 30 +
	       pUser->size.h + pServer->size.h, pPort_nr);
  draw_edit(pPort, start_x,
	    start_y + 30 + pUser->size.h + pServer->size.h);
	    
  /* --------------------------------- */
  start_button_y = pPort->size.y + pPort->size.h + 80;
  
  start_button_meta_x = area->x + (area->w - pMeta->size.w) / 2;

  start_button_connect_x = start_button_meta_x - pConnect->size.w - 20;

  start_button_cancel_x = start_button_meta_x + pMeta->size.w + 20;

  /* ---------------------------- */
  draw_tibutton(pConnect, start_button_connect_x, start_button_y);

  draw_frame_around_widget(pConnect);

  draw_tibutton(pMeta, start_button_meta_x, start_button_y);

  draw_frame_around_widget(pMeta);

  draw_tibutton(pCancel, start_button_cancel_x, start_button_y);

  draw_frame_around_widget(pCancel);

  dirty_all();
  flush_dirty();

  /* ==================== Free Local Var ===================== */
  FREESTRING16(pPlayer_name);
  FREESTRING16(pServer_name);
  FREESTRING16(pPort_nr);
  
  return -1;
}

static int quit_callback(struct GUI *pWidget)
{
  struct GUI *pEnd = pWidget->next->next->next->next;
    
  /*SDL_FillRect(Main.gui, (SDL_Rect *)pWidget->data, 0x0);*/
  
  FREE(pWidget->data);
  
  del_group_of_widgets_from_gui_list(pWidget,pEnd);
  
  return 0;
}

static int popup_option_callback(struct GUI *pWidget)
{
  struct GUI *pEnd = pWidget->next->next->next;
    
  SDL_FillRect(Main.gui, (SDL_Rect *)pWidget->data, 0x0);
  
  flush_rect( *((SDL_Rect *)pWidget->data));
  FREE(pWidget->data);
  
  del_group_of_widgets_from_gui_list(pWidget->prev,pEnd);
  
  popup_optiondlg();
  
  return -1;
}


/**************************************************************************
  Provide an interface for connecting to a FreeCiv server.
**************************************************************************/
void gui_server_connect(void)
{
  int w = 0 , h = 0;
  struct GUI *pBuf = NULL, *pFirst , *pLast;
  SDL_Rect *pArea;
  SDL_Surface *pLogo, *pTmp;
  SDL_Color col = {255, 255, 255, 128};
  
  pFirst = pBuf =
	create_iconlabel_from_chars(NULL, _("Start New Game"), 14,
						  WF_DRAW_THEME_TRANSPARENT);
  
  /*pBuf->action = popup_start_new_game_callback;*/
  pBuf->string16->forecol.r = 128;
  pBuf->string16->forecol.g = 128;
  pBuf->string16->forecol.b = 128;
  
  w = MAX(w , pBuf->size.w);
  h = MAX(h , pBuf->size.h);
  add_to_gui_list(ID_START_NEW_GAME, pBuf);
  
  pBuf = create_iconlabel_from_chars(NULL, _("Load Game"), 14,
		WF_SELLECT_WITHOUT_BAR|WF_DRAW_THEME_TRANSPARENT);
  /*pBuf->action = popup_load_game_callback;*/
  pBuf->string16->forecol.r = 128;
  pBuf->string16->forecol.g = 128;
  pBuf->string16->forecol.b = 128;
  
  w = MAX(w , pBuf->size.w);
  h = MAX(h , pBuf->size.h);
  add_to_gui_list(ID_LOAD_GAME, pBuf);
  
  pBuf = create_iconlabel_from_chars(NULL, _("Join Game"), 14,
			WF_SELLECT_WITHOUT_BAR|WF_DRAW_THEME_TRANSPARENT);
  pBuf->action = popup_join_game_callback;
  
  
  set_wstate(pBuf, WS_NORMAL);
  w = MAX(w , pBuf->size.w);
  h = MAX(h , pBuf->size.h);
  add_to_gui_list(ID_JOIN_GAME, pBuf);
  
  pBuf = create_iconlabel_from_chars(NULL, _("Options"), 14,
			WF_SELLECT_WITHOUT_BAR|WF_DRAW_THEME_TRANSPARENT);
  pBuf->action = popup_option_callback;
  
  w = MAX(w , pBuf->size.w);
  h = MAX(h , pBuf->size.h);
  set_wstate(pBuf, WS_NORMAL);
  add_to_gui_list(ID_CLIENT_OPTIONS_BUTTON, pBuf);
  
  pLast = pBuf = create_iconlabel_from_chars(NULL, _("Quit"), 14,
			WF_SELLECT_WITHOUT_BAR|WF_DRAW_THEME_TRANSPARENT);
  pBuf->action = quit_callback;
  
  set_wstate(pBuf, WS_NORMAL);
  w = MAX(w , pBuf->size.w);
  h = MAX(h , pBuf->size.h);
  add_to_gui_list(ID_QUIT, pBuf);
  
  w+=30;
  h+=6;
  
  pFirst->size.x = (Main.gui->w - w) - 20;
  pFirst->size.y = (Main.gui->h - (h * 5)) -20;
  pFirst->size.w = w;
  pFirst->size.h = h;
  pBuf = pFirst->prev;
  
  pArea = MALLOC(sizeof(SDL_Rect));
  
  pArea->x = pFirst->size.x - FRAME_WH;
  pArea->y = pFirst->size.y - FRAME_WH;
  pArea->w = pFirst->size.w + DOUBLE_FRAME_WH;
  pArea->h = 5 * pFirst->size.h + DOUBLE_FRAME_WH;
  
  pFirst->data = (void *)pArea;
  
  while(pBuf)
  {
    pBuf->data = (void *)pArea;
    pBuf->size.w = w;
    pBuf->size.h = h;
    pBuf->size.x = pBuf->next->size.x;
    pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
    if(pBuf == pLast) break;
    pBuf = pBuf->prev;
  }
  
  draw_intro_gfx();
  
  pLogo = get_logo_gfx();
  pTmp = ResizeSurface(pLogo, pArea->w, pArea->h , 1);
  FREESURFACE(pLogo);
  
  blit_entire_src(pTmp, Main.gui, pArea->x , pArea->y );
  FREESURFACE(pLogo);
    
  SDL_FillRectAlpha(Main.gui, pArea, &col );
  
  redraw_group(pLast, pFirst, Main.gui, 0);
  
  draw_frame(Main.gui, pFirst->size.x - FRAME_WH, pFirst->size.y - FRAME_WH ,
  	w + DOUBLE_FRAME_WH, (h*5) + DOUBLE_FRAME_WH);


  dirty_all();
  flush_dirty();
  
}

#if 0
/**************************************************************************
  Make an attempt to autoconnect to the server.  
**************************************************************************/
static Uint32 try_to_autoconnect(Uint32 interval, void *parm)
{
  char errbuf[512];
  static int count = 0;
  static int warning_shown = 0;

  count++;

  if (count >= MAX_AUTOCONNECT_ATTEMPTS) {
    freelog(LOG_FATAL,
	    _("Failed to contact server \"%s\" at port "
	      "%d as \"%s\" after %d attempts"),
	    server_host, server_port, player_name, count);

    exit(EXIT_FAILURE);
  }

  switch (try_to_connect(player_name, errbuf, sizeof(errbuf))) {
  case 0:			/* Success! 
				   if (autoconnect_timer_id)
				   SDL_RemoveTimer(autoconnect_timer_id); */
    return 0;			/*  Tells GTK not to call this
				   function again */
  case ECONNREFUSED:		/* Server not available (yet) */
    if (!warning_shown) {
      freelog(LOG_NORMAL, _("Connection to server refused. "
			    "Please start the server."));
      append_output_window(_("Connection to server refused. "
			     "Please start the server."));
      warning_shown = 1;
    }
    return interval;		/*  Tells GTK to keep calling this function */
  default:			/* All other errors are fatal */
    freelog(LOG_FATAL,
	    _("Error contacting server \"%s\" at port %d "
	      "as \"%s\":\n %s\n"),
	    server_host, server_port, player_name, errbuf);
    exit(EXIT_FAILURE);		/* Suppresses a gcc warning */
  }
  return interval;
}
#endif

/**************************************************************************
  Make an attempt to autoconnect to the server.  
**************************************************************************/
bool try_to_autoconnect(void)
{
  char errbuf[512];
  static int count = 0;
  static int warning_shown = 0;

  count++;

  if (count >= MAX_AUTOCONNECT_ATTEMPTS) {
    freelog(LOG_FATAL,
	    _("Failed to contact server \"%s\" at port "
	      "%d as \"%s\" after %d attempts"),
	    server_host, server_port, player_name, count);

    exit(EXIT_FAILURE);
  }

  if(try_to_connect(player_name, errbuf, sizeof(errbuf))) {
    /* Server not available (yet) */
    if (!warning_shown) {
      freelog(LOG_NORMAL, _("Connection to server refused. "
			    "Please start the server."));
      append_output_window(_("Connection to server refused. "
			     "Please start the server."));
      warning_shown = 1;
    }
    return FALSE; /*  Tells Client to keep calling this function */
  }
  
  /* Success! */
  return TRUE;	/*  Tells Client not to call this function again */
}

/**************************************************************************
  Start trying to autoconnect to civserver.
  Calls get_server_address(), then arranges for
  autoconnect_callback(), which calls try_to_connect(), to be called
  roughly every AUTOCONNECT_INTERVAL milliseconds, until success,
  fatal error or user intervention.
**************************************************************************/
void server_autoconnect()
{
  char buf[512];

  my_snprintf(buf, sizeof(buf),
	      _("Auto-connecting to server \"%s\" at port %d "
		"as \"%s\" every %d.%d second(s) for %d times"),
	      server_host, server_port, player_name,
	      AUTOCONNECT_INTERVAL / 1000, AUTOCONNECT_INTERVAL % 1000,
	      MAX_AUTOCONNECT_ATTEMPTS);
  append_output_window(buf);

  if (get_server_address(server_host, server_port, buf, sizeof(buf)) < 0) {
    freelog(LOG_FATAL,
	    _("Error contacting server \"%s\" at port %d "
	      "as \"%s\":\n %s\n"),
	    server_host, server_port, player_name, buf);


    exit(EXIT_FAILURE);
  }
  if (!try_to_autoconnect()) {
    add_autoconnect_to_timer();
  }
}
