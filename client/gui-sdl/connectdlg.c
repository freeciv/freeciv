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
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL/SDL.h>

#include "fcintl.h"
#include "log.h"
#include "support.h"
/*#include "version.h"*/

#include "civclient.h"
#include "chatline.h"
#include "clinet.h"
#include "packhand.h"

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
#include "messagewin.h"
#include "connectdlg.h"

#include "colors.h"


static struct server_list *pServer_list = NULL;
static struct ADVANCED_DLG *pMeta_Severs = NULL;
static struct SMALL_DLG *pStartMenu = NULL;
  
static int connect_callback(struct GUI *pWidget);
static int convert_portnr_callback(struct GUI *pWidget);
static int convert_playername_callback(struct GUI *pWidget);
static int convert_servername_callback(struct GUI *pWidget);
static int popup_join_game_callback(struct GUI *pWidget);

/*
  THOSE FUNCTIONS ARE ONE BIG TMP SOLUTION AND SHOULD BE FULL REWRITEN !!
*/

/**************************************************************************
 really close and destroy the dialog.
**************************************************************************/
void really_close_connection_dialog(void)
{
  /* PORTME */
}

/**************************************************************************
 provide a packet handler for packet_game_load
**************************************************************************/
void handle_game_load(struct packet_game_load *packet)
{ 
  /* PORTME */
}

/**************************************************************************
  ...
**************************************************************************/
static int connect_callback(struct GUI *pWidget)
{
  char errbuf[512];

  if (connect_to_server(user_name, server_host, server_port,
			errbuf, sizeof(errbuf)) != -1) {
  } else {
    append_output_window(errbuf);

    /* button up */
    unsellect_widget_action();
    set_wstate(pWidget, FC_WS_SELLECTED);
    pSellected_Widget = pWidget;
    redraw_tibutton(pWidget);
    flush_rect(pWidget->size);
  }
  return -1;
}
/* ======================================================== */


/**************************************************************************
  ...
**************************************************************************/
static int meta_severs_window_callback(struct GUI *pWindow)
{
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int exit_meta_severs_dlg_callback(struct GUI *pWidget)
{
  queue_flush();
  if (pMeta_Severs->pEndWidgetList->ID == ID_META_SERVERS_WINDOW)
  {
    delete_server_list(pServer_list);
  } else {
    /* lan list */
    finish_lanserver_scan();
  }
  pServer_list = NULL;
  popup_join_game_callback(NULL);
  popup_meswin_dialog();
  return -1;
}


/**************************************************************************
  ...
**************************************************************************/
static int sellect_meta_severs_callback(struct GUI *pWidget)
{
  struct server *pServer = (struct server *)pWidget->data.ptr;
      
  sz_strlcpy(server_host, pServer->name);
  sscanf(pServer->port, "%d", &server_port);
  
  exit_meta_severs_dlg_callback(NULL);
  return -1;
}

/**************************************************************************
  SDL wraper on create_server_list(...) function witch add
  same functionality for LAN server dettection.
  WARING !: for LAN scan use "finish_lanserver_scan()" to free server list.
**************************************************************************/
static struct server_list *sdl_create_server_list(char *errbuf, int n_errbuf, bool lan)
{
  if (lan)
  {
    struct server_list *pList = NULL;
    if (begin_lanserver_scan())
    {
      int scan_count = 0;
      /* 5 sec scan on LAN network */
      while (scan_count++ < 50)
      {
        pList = get_lan_server_list();
        SDL_Delay(100);
      }
      if(!pList || (pList && !server_list_size(pList)))
      {
        my_snprintf(errbuf, n_errbuf, "No LAN Server Found");
        finish_lanserver_scan();
        pList = NULL;
      }
    } else {
      my_snprintf(errbuf, n_errbuf, "LAN network scan error");
    }
    return pList;
  }
  return create_server_list(errbuf, n_errbuf);
}


/**************************************************************************
  ...
**************************************************************************/
static int severs_callback(struct GUI *pWidget)
{
  char errbuf[128];
  char cBuf[512];
  int w = 0, h = 0, count = 0, meta_h;
  struct GUI *pBuf, *pWindow;
  SDL_String16 *pStr;
  SDL_Surface *pLogo , *pDest = pWidget->dst;
  SDL_Color col = {255, 255, 255, 128};
  SDL_Rect area;
  bool lan_scan = (pWidget->ID != ID_JOIN_META_GAME);
  
  queue_flush();
  close_connection_dialog();
  popdown_meswin_dialog();
  
  my_snprintf(cBuf, sizeof(cBuf), _("Creating Server List..."));
  pStr = create_str16_from_char(cBuf, 16);
  pStr->style = TTF_STYLE_BOLD;
  pStr->render = 3;
  pStr->bgcol = col;
  pLogo = create_text_surf_from_str16(pStr);
  SDL_SetAlpha(pLogo, 0x0, 0x0);
    
  area.w = pLogo->w + 60;
  area.h = pLogo->h + 30;
  area.x = (pDest->w - area.w) / 2;
  area.y = (pDest->h - area.h) / 2;
  SDL_FillRect(pDest, &area,
  		SDL_MapRGBA(pDest->format, col.r, col.g,col.b, col.unused));
  
  putframe(pDest, area.x, area.y, area.x + area.w - 1,
				area.y + area.h - 1, 0xFF000000);
  
  sdl_dirty_rect(area);
  
  area.x += 30;
  area.y += 15;
  SDL_BlitSurface(pLogo, NULL, pDest, &area);
  unqueue_flush();
  
  FREESURFACE(pLogo);
  FREESTRING16(pStr);
  
  pServer_list = sdl_create_server_list(errbuf, sizeof(errbuf), lan_scan);
  
  if(!pServer_list) {
    popup_join_game_callback(NULL);
    append_output_window(errbuf);
    return -1;
  }
  
  pMeta_Severs = MALLOC(sizeof(struct ADVANCED_DLG));
    
  pWindow = create_window(pDest, NULL, 10, 10, 0);
  pWindow->action = meta_severs_window_callback;
  set_wstate(pWindow, FC_WS_NORMAL);
  clear_wflag(pWindow, WF_DRAW_FRAME_AROUND_WIDGET);
  if (lan_scan)
  {
    add_to_gui_list(ID_LAN_SERVERS_WINDOW, pWindow);
  } else {
    add_to_gui_list(ID_META_SERVERS_WINDOW, pWindow);
  }
  pMeta_Severs->pEndWidgetList = pWindow;
  /* ------------------------------ */
  pBuf = create_themeicon_button_from_chars(pTheme->CANCEL_Icon, pWindow->dst,
						     _("Cancel"), 14, 0);
  pBuf->action = exit_meta_severs_dlg_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  add_to_gui_list(ID_BUTTON, pBuf);
  /* ------------------------------ */
  
  server_list_iterate(*pServer_list,pServer) {
    
    my_snprintf(cBuf, sizeof(cBuf), "%s Port %s Ver: %s %s %s %s\n%s",
    	pServer->name, pServer->port, pServer->version, _(pServer->status),
    		_("Players"), pServer->players, pServer->metastring);

    pBuf = create_iconlabel_from_chars(NULL, pWindow->dst, cBuf, 10,
	WF_FREE_STRING|WF_DRAW_TEXT_LABEL_WITH_SPACE|WF_DRAW_THEME_TRANSPARENT);
    
    pBuf->string16->style |= SF_CENTER;
    pBuf->string16->render = 3;
    pBuf->string16->bgcol.r = 0;
    pBuf->string16->bgcol.g = 0;
    pBuf->string16->bgcol.b = 0;
    pBuf->string16->bgcol.unused = 0;
    
    pBuf->action = sellect_meta_severs_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
    pBuf->data.ptr = (void *)pServer;
    w = MAX(w, pBuf->size.w);
    h = MAX(h, pBuf->size.h);
    
    add_to_gui_list(ID_BUTTON, pBuf);
    count++;
    
    if(count > 10) {
      set_wflag(pBuf, WF_HIDDEN);
    }
    
  } server_list_iterate_end;
  
  pMeta_Severs->pBeginWidgetList = pBuf;
  pMeta_Severs->pBeginActiveWidgetList = pBuf;
  pMeta_Severs->pEndActiveWidgetList = pWindow->prev->prev;
  pMeta_Severs->pActiveWidgetList = pWindow->prev->prev;
    
  if (count > 10) {
    meta_h = 10 * h;
  
    count = create_vertical_scrollbar(pMeta_Severs, 1, 10, TRUE, TRUE);
    w += count;
  } else {
    meta_h = count * h;
  }
  
  w += 20;
  area.h = meta_h;
  
  meta_h += pMeta_Severs->pEndWidgetList->prev->size.h + 10 + 20;
  
  pWindow->size.x = (pWindow->dst->w - w) /2;
  pWindow->size.y = (pWindow->dst->h - meta_h) /2;
  
  pLogo = get_logo_gfx();
  if (resize_window(pWindow , pLogo , NULL , w , meta_h)) {
    FREESURFACE(pLogo);
  }
  SDL_SetAlpha(pWindow->theme, 0x0, 0x0);
  w -= 20;
  
  area.w = w + 1;
  
  if(pMeta_Severs->pScroll) {
    w -= count;
  }
  
  /* exit button */
  pBuf = pWindow->prev;
  pBuf->size.x = pWindow->size.x + pWindow->size.w - pBuf->size.w - 10;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 10;
  
  /* meta labels */
  pBuf = pBuf->prev;
  
  pBuf->size.x = pWindow->size.x + 10;
  pBuf->size.y = pWindow->size.y + 10;
  pBuf->size.w = w;
  pBuf->size.h = h;
  pBuf = convert_iconlabel_to_themeiconlabel2(pBuf);
  
  pBuf = pBuf->prev;
  while(pBuf)
  {
        
    pBuf->size.w = w;
    pBuf->size.h = h;
    pBuf->size.x = pBuf->next->size.x;
    pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
    pBuf = convert_iconlabel_to_themeiconlabel2(pBuf);
    
    if (pBuf == pMeta_Severs->pBeginActiveWidgetList) {
      break;
    }
    pBuf = pBuf->prev;  
  }
  
  if(pMeta_Severs->pScroll) {
    setup_vertical_scrollbar_area(pMeta_Severs->pScroll,
	pWindow->size.x + pWindow->size.w - 9,
	pMeta_Severs->pEndActiveWidgetList->size.y,
	pWindow->size.h - 30 - pWindow->prev->size.h, TRUE);
  }
  
  /* -------------------- */
  /* redraw */
  
  redraw_window(pWindow);
  
  area.x = pMeta_Severs->pEndActiveWidgetList->size.x;
  area.y = pMeta_Severs->pEndActiveWidgetList->size.y;
  
  SDL_FillRectAlpha(pWindow->dst, &area, &col);
  
  putframe(pWindow->dst, area.x - 1, area.y - 1, 
  		area.x + area.w , area.y + area.h , 0xff000000);
  
  redraw_group(pMeta_Severs->pBeginWidgetList, pWindow->prev, 0);

  putframe(pWindow->dst, pWindow->size.x , pWindow->size.y , 
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
  sz_strlcpy(user_name, tmp);
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
  close_connection_dialog();
  gui_server_connect();
  return -1;
}


static int popup_join_game_callback(struct GUI *pWidget)
{
  char pCharPort[6];
  struct GUI *pBuf;
  SDL_String16 *pPlayer_name = NULL;
  SDL_String16 *pServer_name = NULL;
  SDL_String16 *pPort_nr = NULL;
  SDL_Color color = {255, 255, 255, 255};
  SDL_Surface *pLogo, *pTmp, *pDest;
  SDL_Rect *area;
  
  int start_x, start_y;
  int dialog_w, dialog_h;
  int start_button_y;
  
     
  if(pWidget) {
    pDest = pWidget->dst;
  } else {
    pDest = Main.gui;
  }
  
  queue_flush();
  close_connection_dialog();
  
  pStartMenu = MALLOC(sizeof(struct SMALL_DLG));
  area = MALLOC(sizeof(SDL_Rect)); 
    
  /* -------------------------- */

  pPlayer_name = create_str16_from_char(_("Player Name :"), 10);
  pPlayer_name->fgcol = color;
  pServer_name = create_str16_from_char(_("Freeciv Server :"), 10);
  pServer_name->fgcol = color;
  pPort_nr = create_str16_from_char(_("Port :"), 10);
  pPort_nr->fgcol = color;
  
  /* ====================== INIT =============================== */
  pBuf = create_edit_from_chars(NULL, pDest, user_name, 14, 210,
				(WF_DRAW_THEME_TRANSPARENT|WF_FREE_DATA));
  pBuf->action = convert_playername_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  add_to_gui_list(ID_PLAYER_NAME_EDIT, pBuf);
  pBuf->data.ptr = (void *)area;
  pStartMenu->pEndWidgetList = pBuf;
  /* ------------------------------ */

  pBuf = create_edit_from_chars(NULL, pDest, server_host, 14, 210,
					 WF_DRAW_THEME_TRANSPARENT);

  pBuf->action = convert_servername_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  add_to_gui_list(ID_SERVER_NAME_EDIT, pBuf);

  /* ------------------------------ */
  my_snprintf(pCharPort, sizeof(pCharPort), "%d", server_port);
  
  pBuf = create_edit_from_chars(NULL, pDest, pCharPort, 14, 210,
					 WF_DRAW_THEME_TRANSPARENT);

  pBuf->action = convert_portnr_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  add_to_gui_list(ID_PORT_EDIT, pBuf);
  
  /* ------------------------------ */
  
  pBuf = create_themeicon_button_from_chars(pTheme->OK_Icon, pDest,
						     _("Connect"), 14, 0);
  pBuf->action = connect_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_RETURN;
  add_to_gui_list(ID_CONNECT_BUTTON, pBuf);
  
  /* ------------------------------ */
  
  pBuf = create_themeicon_button_from_chars(pTheme->CANCEL_Icon, pDest,
						     _("Cancel"), 14, 0);
  pBuf->action = cancel_connect_dlg_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  add_to_gui_list(ID_CANCEL_BUTTON, pBuf);
  pBuf->size.w = MAX(pBuf->size.w, pBuf->next->size.w);
  pBuf->next->size.w = pBuf->size.w;
  
  /* ------------------------------ */
  
  pStartMenu->pBeginWidgetList = pBuf;
  /* ==================== Draw first time ===================== */
  dialog_w = 40 + pBuf->size.w * 2;
  dialog_w = MAX(dialog_w, 210);
  dialog_w += 80;
  dialog_h = 200;

  pLogo = get_logo_gfx();
  pTmp = ResizeSurface(pLogo, dialog_w, dialog_h, 1);
  FREESURFACE(pLogo);

  area->x = (pDest->w - dialog_w)/ 2;
  area->y = (pDest->h - dialog_h)/ 2 + 40;
  SDL_SetAlpha(pTmp, 0x0, 0x0);
  SDL_BlitSurface(pTmp, NULL, pDest, area);
  FREESURFACE(pTmp);

  putframe(pDest, area->x, area->y, area->x + dialog_w - 1,
  					area->y + dialog_h - 1, 0xffffffff);

  area->w = dialog_w;
  area->h = dialog_h;
  /* ---------------------------------------- */
  /* user edit */
  pBuf = pStartMenu->pEndWidgetList;
  start_x = area->x + (area->w - pBuf->size.w) / 2;
  start_y = area->y + 35;
  write_text16(pDest, start_x + 5, start_y - 13, pPlayer_name);
  draw_edit(pBuf, start_x, start_y);
  
  /* server name */
  pBuf = pBuf->prev;
  write_text16(pDest, start_x + 5, start_y - 13 + 15 +
	       				pBuf->next->size.h, pServer_name);
	       
  draw_edit(pBuf, start_x, start_y + 15 + pBuf->next->size.h);

  /* port */
  pBuf = pBuf->prev;
  write_text16(pDest, start_x + 5, start_y - 13 + 30 +
	       pBuf->next->next->size.h + pBuf->next->size.h, pPort_nr);
	       
  draw_edit(pBuf, start_x,
	 start_y + 30 + pBuf->next->next->size.h + pBuf->next->size.h);

  /* --------------------------------- */
  start_button_y = pBuf->size.y + pBuf->size.h + 25;

  /* connect button */
  pBuf = pBuf->prev;
  draw_tibutton(pBuf, area->x + (dialog_w - (40 + pBuf->size.w * 2)) / 2,
  						start_button_y);
  draw_frame_around_widget(pBuf);
  
  /* cancel button */
  pBuf = pBuf->prev;
  draw_tibutton(pBuf, pBuf->next->size.x + pBuf->size.w + 40, start_button_y);
  draw_frame_around_widget(pBuf);

  flush_all();

  /* ==================== Free Local Var ===================== */
  FREESTRING16(pPlayer_name);
  FREESTRING16(pServer_name);
  FREESTRING16(pPort_nr);
  
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int convert_passwd_callback(struct GUI *pWidget)
{
  char *tmp = convert_to_chars(pWidget->string16->text);
  if(tmp) {
    my_snprintf(password, MAX_LEN_NAME, "%s", tmp);
    FREE(tmp);
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int send_passwd_callback(struct GUI *pWidget)
{
  struct packet_authentication_reply reply;
    
  sz_strlcpy(reply.password, password);
  
  memset(password, 0, MAX_LEN_NAME);
  password[0] = '\0';
  
  set_wstate(pWidget, FC_WS_DISABLED);
  set_wstate(pWidget->prev, FC_WS_DISABLED);
  
  redraw_tibutton(pWidget);
  redraw_tibutton(pWidget->prev);
  
  sdl_dirty_rect(pWidget->size);
  sdl_dirty_rect(pWidget->prev->size);
  
  flush_dirty();
  
  send_packet_authentication_reply(&aconnection, &reply);
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static void popup_user_passwd_dialog(char *pMessage)
{
  struct GUI *pBuf;
  SDL_String16 *pStr = NULL;
  SDL_Color color_white = {255, 255, 255, 255};
  SDL_Color color_black = {0, 0, 0, 255};
  SDL_Surface *pLogo, *pTmp, *pDest, *pText;
  SDL_Rect *area;
  
  int start_x, start_y;
  int dialog_w, dialog_h;
  int start_button_y;
        
  pDest = Main.gui;
    
  queue_flush();
  close_connection_dialog();
  
  pStartMenu = MALLOC(sizeof(struct SMALL_DLG));
  area = MALLOC(sizeof(SDL_Rect)); 
    
  /* -------------------------- */

  pStr = create_str16_from_char(pMessage, 12);
  pStr->fgcol = color_white;
    
  pText = create_text_surf_from_str16(pStr);
  
  /* ====================== INIT =============================== */
  change_ptsize16(pStr, 16);
  pStr->fgcol = color_black;
  FREE(pStr->text);
  
  pBuf = create_edit(NULL, pDest, pStr, 210,
		(WF_PASSWD_EDIT|WF_DRAW_THEME_TRANSPARENT|WF_FREE_DATA));
  pBuf->action = convert_passwd_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  add_to_gui_list(ID_EDIT, pBuf);
  pBuf->data.ptr = (void *)area;
  pStartMenu->pEndWidgetList = pBuf;
  /* ------------------------------ */
      
  pBuf = create_themeicon_button_from_chars(pTheme->OK_Icon, pDest,
						     _("Next"), 14, 0);
  pBuf->action = send_passwd_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_RETURN;
  add_to_gui_list(ID_BUTTON, pBuf);
  
  /* ------------------------------ */
  
  pBuf = create_themeicon_button_from_chars(pTheme->CANCEL_Icon, pDest,
						     _("Cancel"), 14, 0);
  pBuf->action = cancel_connect_dlg_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  add_to_gui_list(ID_CANCEL_BUTTON, pBuf);
  pBuf->size.w = MAX(pBuf->size.w, pBuf->next->size.w);
  pBuf->next->size.w = pBuf->size.w;
  
  /* ------------------------------ */
  
  pStartMenu->pBeginWidgetList = pBuf;
  /* ==================== Draw first time ===================== */
  dialog_w = 40 + pBuf->size.w * 2;
  dialog_w = MAX(dialog_w, 210);
  dialog_w = MAX(dialog_w, pText->w);
  dialog_w += 80;
  dialog_h = 170;

  pLogo = get_logo_gfx();
  pTmp = ResizeSurface(pLogo, dialog_w, dialog_h, 1);
  FREESURFACE(pLogo);

  area->x = (pDest->w - dialog_w)/ 2;
  area->y = (pDest->h - dialog_h)/ 2 + 40;
  SDL_SetAlpha(pTmp, 0x0, 0x0);
  SDL_BlitSurface(pTmp, NULL, pDest, area);
  FREESURFACE(pTmp);

  putframe(pDest, area->x, area->y, area->x + dialog_w - 1,
  					area->y + dialog_h - 1, 0xffffffff);

  area->w = dialog_w;
  area->h = dialog_h;
  /* ---------------------------------------- */
  /* passwd edit */
  pBuf = pStartMenu->pEndWidgetList;
  start_x = area->x + (area->w - pText->w) / 2;
  start_y = area->y + 50;
  blit_entire_src(pText, pDest, start_x, start_y);
  start_y += pText->h + 5;
  FREESURFACE(pText);
  start_x = area->x + (area->w - pBuf->size.w) / 2;
  draw_edit(pBuf, start_x, start_y);
  
  /* --------------------------------- */
  start_button_y = pBuf->size.y + pBuf->size.h + 25;

  /* connect button */
  pBuf = pBuf->prev;
  draw_tibutton(pBuf, area->x + (dialog_w - (40 + pBuf->size.w * 2)) / 2,
  						start_button_y);
  draw_frame_around_widget(pBuf);
  
  /* cancel button */
  pBuf = pBuf->prev;
  draw_tibutton(pBuf, pBuf->next->size.x + pBuf->size.w + 40, start_button_y);
  draw_frame_around_widget(pBuf);

  flush_all();
}


/**************************************************************************
  New Password
**************************************************************************/
static int convert_first_passwd_callback(struct GUI *pWidget)
{
  char *tmp = convert_to_chars(pWidget->string16->text);
  
  if(tmp) {
    my_snprintf(password, MAX_LEN_NAME, "%s", tmp);
    FREE(tmp);
    set_wstate(pWidget->prev, FC_WS_NORMAL);
    redraw_edit(pWidget->prev);
    flush_rect(pWidget->prev->size);
  }
  return -1;
}

/**************************************************************************
  Verify Password
**************************************************************************/
static int convert_secound_passwd_callback(struct GUI *pWidget)
{
  char *tmp = convert_to_chars(pWidget->string16->text);
  
  if (tmp && strncmp(password, tmp, MAX_LEN_NAME) == 0) {
    set_wstate(pWidget->prev, FC_WS_NORMAL); /* next button */
    redraw_tibutton(pWidget->prev);
    flush_rect(pWidget->prev->size);
  } else {
    memset(password, 0, MAX_LEN_NAME);
    password[0] = '\0';
    
    FREE(pWidget->next->string16->text);/* first edit */
    FREE(pWidget->string16->text); /* secound edit */
    
    set_wstate(pWidget, FC_WS_DISABLED);
    
    redraw_edit(pWidget);
    redraw_edit(pWidget->next);
  
    sdl_dirty_rect(pWidget->size);
    sdl_dirty_rect(pWidget->next->size);
  
    flush_dirty();
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static void popup_new_user_passwd_dialog(char *pMessage)
{
  struct GUI *pBuf;
  SDL_String16 *pStr = NULL;
  SDL_Color color_white = {255, 255, 255, 255};
  SDL_Color color_black = {0, 0, 0, 255};
  SDL_Surface *pLogo, *pTmp, *pDest, *pText;
  SDL_Rect *area;
  
  int start_x, start_y;
  int dialog_w, dialog_h;
  int start_button_y;
        
  pDest = Main.gui;
    
  queue_flush();
  close_connection_dialog();
  
  pStartMenu = MALLOC(sizeof(struct SMALL_DLG));
  area = MALLOC(sizeof(SDL_Rect)); 
    
  /* -------------------------- */

  pStr = create_str16_from_char(pMessage, 12);
  pStr->fgcol = color_white;
    
  pText = create_text_surf_from_str16(pStr);
  
  /* ====================== INIT =============================== */
  change_ptsize16(pStr, 16);
  pStr->fgcol = color_black;
  
  FREE(pStr->text);
  pStr->n_alloc = 0;
  
  pBuf = create_edit(NULL, pDest, pStr, 210,
		(WF_PASSWD_EDIT|WF_DRAW_THEME_TRANSPARENT|WF_FREE_DATA));
  pBuf->action = convert_first_passwd_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  add_to_gui_list(ID_EDIT, pBuf);
  pBuf->data.ptr = (void *)area;
  pStartMenu->pEndWidgetList = pBuf;
  /* ------------------------------ */
  
  pBuf = create_edit(NULL, pDest, create_string16(NULL, 0, 16) , 210,
		(WF_PASSWD_EDIT|WF_DRAW_THEME_TRANSPARENT|WF_FREE_DATA));
  pBuf->action = convert_secound_passwd_callback;
  add_to_gui_list(ID_EDIT, pBuf);
  
  /* ------------------------------ */    
  pBuf = create_themeicon_button_from_chars(pTheme->OK_Icon, pDest,
						     _("Next"), 14, 0);
  pBuf->action = send_passwd_callback;
  pBuf->key = SDLK_RETURN;  
  add_to_gui_list(ID_BUTTON, pBuf);
  
  /* ------------------------------ */
  
  pBuf = create_themeicon_button_from_chars(pTheme->CANCEL_Icon, pDest,
						     _("Cancel"), 14, 0);
  pBuf->action = cancel_connect_dlg_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  add_to_gui_list(ID_CANCEL_BUTTON, pBuf);
  pBuf->size.w = MAX(pBuf->size.w, pBuf->next->size.w);
  pBuf->next->size.w = pBuf->size.w;
  
  /* ------------------------------ */
  
  pStartMenu->pBeginWidgetList = pBuf;
  /* ==================== Draw first time ===================== */
  dialog_w = 40 + pBuf->size.w * 2;
  dialog_w = MAX(dialog_w, 210);
  dialog_w = MAX(dialog_w, pText->w);
  dialog_w += 80;
  dialog_h = 180;

  pLogo = get_logo_gfx();
  pTmp = ResizeSurface(pLogo, dialog_w, dialog_h, 1);
  FREESURFACE(pLogo);

  area->x = (pDest->w - dialog_w)/ 2;
  area->y = (pDest->h - dialog_h)/ 2 + 40;
  SDL_SetAlpha(pTmp, 0x0, 0x0);
  SDL_BlitSurface(pTmp, NULL, pDest, area);
  FREESURFACE(pTmp);

  putframe(pDest, area->x, area->y, area->x + dialog_w - 1,
  					area->y + dialog_h - 1, 0xffffffff);

  area->w = dialog_w;
  area->h = dialog_h;
  /* ---------------------------------------- */
  /* passwd edit */
  pBuf = pStartMenu->pEndWidgetList;
  start_x = area->x + (area->w - pText->w) / 2;
  start_y = area->y + 30;
  blit_entire_src(pText, pDest, start_x, start_y);
  start_y += pText->h + 5;
  FREESURFACE(pText);
  
  start_x = area->x + (area->w - pBuf->size.w) / 2;
  draw_edit(pBuf, start_x, start_y);
  start_y += pBuf->size.h + 5;
  
  /* retype passwd */
  pBuf = pBuf->prev;
  draw_edit(pBuf, start_x, start_y);
  
  /* --------------------------------- */
  start_button_y = pBuf->size.y + pBuf->size.h + 25;

  /* connect button */
  pBuf = pBuf->prev;
  draw_tibutton(pBuf, area->x + (dialog_w - (40 + pBuf->size.w * 2)) / 2,
  						start_button_y);
  draw_frame_around_widget(pBuf);
  
  /* cancel button */
  pBuf = pBuf->prev;
  draw_tibutton(pBuf, pBuf->next->size.x + pBuf->size.w + 40, start_button_y);
  draw_frame_around_widget(pBuf);

  flush_all();
}

/**************************************************************************
  ...
**************************************************************************/
static int quit_callback(struct GUI *pWidget)
{
  close_connection_dialog();
  return 0;/* exit from main game loop */
}

/**************************************************************************
  ...
**************************************************************************/
static int popup_option_callback(struct GUI *pWidget)
{
  queue_flush();
  close_connection_dialog();
  popup_optiondlg();
  return -1;
}

/* ======================================================================== */

/**************************************************************************
 close and destroy the dialog.
**************************************************************************/
void close_connection_dialog(void)
{
  if(pStartMenu) {
    SDL_FillRect(pStartMenu->pEndWidgetList->dst,
    			(SDL_Rect *)pStartMenu->pEndWidgetList->data.ptr, 0x0);
  
    sdl_dirty_rect(*((SDL_Rect *)pStartMenu->pEndWidgetList->data.ptr));
    
  
    del_group_of_widgets_from_gui_list(pStartMenu->pBeginWidgetList,
    						pStartMenu->pEndWidgetList);
    FREE(pStartMenu);
  }
  if(pMeta_Severs) {
    popdown_window_group_dialog(pMeta_Severs->pBeginWidgetList,
				  pMeta_Severs->pEndWidgetList);
    FREE(pMeta_Severs->pScroll);
    FREE(pMeta_Severs);
    if(pServer_list) {
      delete_server_list(pServer_list);
      pServer_list = NULL;
    }
  }
}

/**************************************************************************
 popup passwd dialog depending on what type of authentication request the
 server is making.
**************************************************************************/
void handle_authentication_req(enum authentication_type type, char *message)
{
  switch (type) {
    case AUTH_NEWUSER_FIRST:
    case AUTH_NEWUSER_RETRY:
      popup_new_user_passwd_dialog(message);
    break;
    case AUTH_LOGIN_FIRST:
    /* if we magically have a password already present in 'password'
     * then, use that and skip the password entry dialog */
    if (password[0] != '\0') {
      struct packet_authentication_reply reply;

      sz_strlcpy(reply.password, password);
      send_packet_authentication_reply(&aconnection, &reply);
      return;
    } else {
      popup_user_passwd_dialog(message);
    }
    break;
    case AUTH_LOGIN_RETRY:
      popup_user_passwd_dialog(message);
    break;
    default:
      assert(0);
  }

}

/**************************************************************************
  Provide an interface for connecting to a FreeCiv server.
  SDLClient use it as popup main start menu which != connecting dlg.
**************************************************************************/
void gui_server_connect(void)
{
  int w = 0 , h = 0, count = 0;
  struct GUI *pBuf = NULL, *pFirst;
  SDL_Rect *pArea;
  SDL_Surface *pLogo, *pTmp;
  SDL_Color col = {255, 255, 255, 136};

  if(pStartMenu) {
    /* error */
  }
  
  /* create dialog */
  pStartMenu = MALLOC(sizeof(struct SMALL_DLG));
    
  pFirst =
	create_iconlabel_from_chars(NULL, Main.gui, _("Start New Game"), 14,
	(WF_SELLECT_WITHOUT_BAR|WF_DRAW_THEME_TRANSPARENT|WF_FREE_DATA));
  
  /*pBuf->action = popup_start_new_game_callback;*/
  pFirst->string16->style |= SF_CENTER;
  pFirst->string16->fgcol.r = 128;
  pFirst->string16->fgcol.g = 128;
  pFirst->string16->fgcol.b = 128;
  
  w = MAX(w, pFirst->size.w);
  h = MAX(h, pFirst->size.h);
  count++;
  add_to_gui_list(ID_START_NEW_GAME, pFirst);
  pStartMenu->pEndWidgetList = pFirst;
  
  pBuf = create_iconlabel_from_chars(NULL, Main.gui, _("Load Game"), 14,
		(WF_SELLECT_WITHOUT_BAR|WF_DRAW_THEME_TRANSPARENT));
  /*pBuf->action = popup_load_game_callback;*/
  pBuf->string16->style |= SF_CENTER;
  pBuf->string16->fgcol.r = 128;
  pBuf->string16->fgcol.g = 128;
  pBuf->string16->fgcol.b = 128;
  
  w = MAX(w, pBuf->size.w);
  h = MAX(h, pBuf->size.h);
  count++;
  add_to_gui_list(ID_LOAD_GAME, pBuf);
  
  pBuf = create_iconlabel_from_chars(NULL, Main.gui, _("Join Game"), 14,
			WF_SELLECT_WITHOUT_BAR|WF_DRAW_THEME_TRANSPARENT);
  pBuf->action = popup_join_game_callback;
  pBuf->string16->style |= SF_CENTER;  
  set_wstate(pBuf, FC_WS_NORMAL);
  w = MAX(w, pBuf->size.w);
  h = MAX(h, pBuf->size.h);
  count++;
  add_to_gui_list(ID_JOIN_GAME, pBuf);
    
  pBuf = create_iconlabel_from_chars(NULL, Main.gui, _("Join Pubserver"), 14,
			WF_SELLECT_WITHOUT_BAR|WF_DRAW_THEME_TRANSPARENT);
  pBuf->action = severs_callback;
  pBuf->string16->style |= SF_CENTER;  
  set_wstate(pBuf, FC_WS_NORMAL);
  w = MAX(w, pBuf->size.w);
  h = MAX(h, pBuf->size.h);
  count++;
  add_to_gui_list(ID_JOIN_META_GAME, pBuf);
  
  pBuf = create_iconlabel_from_chars(NULL, Main.gui, _("Join LAN Server"), 14,
			WF_SELLECT_WITHOUT_BAR|WF_DRAW_THEME_TRANSPARENT);
  pBuf->action = severs_callback;
  pBuf->string16->style |= SF_CENTER;  
  set_wstate(pBuf, FC_WS_NORMAL);
  w = MAX(w, pBuf->size.w);
  h = MAX(h, pBuf->size.h);
  count++;
  add_to_gui_list(ID_JOIN_GAME, pBuf);
  
  pBuf = create_iconlabel_from_chars(NULL, Main.gui, _("Options"), 14,
			WF_SELLECT_WITHOUT_BAR|WF_DRAW_THEME_TRANSPARENT);
  pBuf->action = popup_option_callback;
  pBuf->string16->style |= SF_CENTER;
  w = MAX(w, pBuf->size.w);
  h = MAX(h, pBuf->size.h);
  count++;
  set_wstate(pBuf, FC_WS_NORMAL);
  add_to_gui_list(ID_CLIENT_OPTIONS_BUTTON, pBuf);
  
  pBuf = create_iconlabel_from_chars(NULL, Main.gui, _("Quit"), 14,
			WF_SELLECT_WITHOUT_BAR|WF_DRAW_THEME_TRANSPARENT);
  pBuf->action = quit_callback;
  pBuf->string16->style |= SF_CENTER;
  pBuf->key = SDLK_ESCAPE;
  set_wstate(pBuf, FC_WS_NORMAL);
  w = MAX(w, pBuf->size.w);
  h = MAX(h, pBuf->size.h);
  count++;
  add_to_gui_list(ID_QUIT, pBuf);
  pStartMenu->pBeginWidgetList = pBuf;
  
  w+=30;
  h+=6;
   
  setup_vertical_widgets_position(1,
	(pFirst->dst->w - w) - 20, (pFirst->dst->h - (h * count)) - 20,
		w, h, pBuf, pFirst);
		
  pArea = MALLOC(sizeof(SDL_Rect));
  
  pArea->x = pFirst->size.x - FRAME_WH;
  pArea->y = pFirst->size.y - FRAME_WH;
  pArea->w = pFirst->size.w + DOUBLE_FRAME_WH;
  pArea->h = count * pFirst->size.h + DOUBLE_FRAME_WH;
  
  pFirst->data.ptr = (void *)pArea;
  
  draw_intro_gfx();
  
  pLogo = get_logo_gfx();
  pTmp = ResizeSurface(pLogo, pArea->w, pArea->h , 1);
  FREESURFACE(pLogo);
  
  SDL_SetAlpha(pTmp, 0x0, 0x0);
  blit_entire_src(pTmp, pFirst->dst, pArea->x , pArea->y);
  FREESURFACE(pTmp);
  
  SDL_FillRectAlpha(pFirst->dst, pArea, &col);
      
  redraw_group(pBuf, pFirst, 0);
  
  draw_frame(pFirst->dst, pFirst->size.x - FRAME_WH, pFirst->size.y - FRAME_WH ,
  	w + DOUBLE_FRAME_WH, (h*count) + DOUBLE_FRAME_WH);

  set_output_window_text(_("SDLClient welcomes you..."));

  set_output_window_text(_("Freeciv is free software and you are welcome "
			   "to distribute copies of "
			   "it under certain conditions;"));
  set_output_window_text(_("See the \"Copying\" item on the Help"
			   " menu."));
  set_output_window_text(_("Now.. Go give'em hell!"));
  
  flush_all();
}

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
	    server_host, server_port, user_name, count);

    exit(EXIT_FAILURE);
  }

  if(try_to_connect(user_name, errbuf, sizeof(errbuf))) {
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
	      server_host, server_port, user_name,
	      AUTOCONNECT_INTERVAL / 1000, AUTOCONNECT_INTERVAL % 1000,
	      MAX_AUTOCONNECT_ATTEMPTS);
  append_output_window(buf);

  if (get_server_address(server_host, server_port, buf, sizeof(buf)) < 0) {
    freelog(LOG_FATAL,
	    _("Error contacting server \"%s\" at port %d "
	      "as \"%s\":\n %s\n"),
	    server_host, server_port, user_name, buf);


    exit(EXIT_FAILURE);
  }
  if (!try_to_autoconnect()) {
    add_autoconnect_to_timer();
  }
}
