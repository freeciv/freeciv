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

#include <stdlib.h>

#include <SDL/SDL.h>

/* utility */
#include "fcintl.h"
#include "log.h"

/* client */
#include "civclient.h"
#include "clinet.h"
#include "packhand.h"
#include "servers.h"

/* gui-sdl */
#include "chatline.h"
#include "graphics.h"
#include "gui_iconv.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "gui_tilespec.h"
#include "mapview.h"
#include "messagewin.h"
#include "optiondlg.h"
#include "pages.h"

#include "connectdlg.h"

static struct server_list *pServer_list = NULL;
static struct server_scan *pServer_scan = NULL; 
    
static struct ADVANCED_DLG *pMeta_Severs = NULL;
static struct SMALL_DLG *pConnectDlg = NULL;

static int connect_callback(struct GUI *pWidget);
static int convert_portnr_callback(struct GUI *pWidget);
static int convert_playername_callback(struct GUI *pWidget);
static int convert_servername_callback(struct GUI *pWidget);

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
    real_update_meswin_dialog();

    /* button up */
    unsellect_widget_action();
    set_wstate(pWidget, FC_WS_SELLECTED);
    pSellected_Widget = pWidget;
    redraw_tibutton(pWidget);
    flush_rect(pWidget->size, FALSE);
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
    
  server_scan_finish(pServer_scan);
  pServer_scan = NULL;
  pServer_list = NULL;
    
  popup_join_game_dialog(NULL);
  popup_meswin_dialog(true);
    
  return -1;
}


/**************************************************************************
  ...
**************************************************************************/
static int sellect_meta_severs_callback(struct GUI *pWidget)
{
  struct server *pServer = (struct server *)pWidget->data.ptr;
      
  sz_strlcpy(server_host, pServer->host);
  sscanf(pServer->port, "%d", &server_port);
  
  exit_meta_severs_dlg_callback(NULL);
  return -1;
}

/**************************************************************************
  Callback function for when there's an error in the server scan.
**************************************************************************/
static void server_scan_error(struct server_scan *scan,
			      const char *message)
{
  append_output_window(message);
  freelog(LOG_NORMAL, "%s", message);

  switch (server_scan_get_type(scan)) {
  case SERVER_SCAN_LOCAL:
    server_scan_finish(pServer_scan);
    pServer_scan = NULL;
    pServer_list = NULL;
    break;
  case SERVER_SCAN_GLOBAL:
    server_scan_finish(pServer_scan);
    pServer_scan = NULL;
    pServer_list = NULL;
    break;
  case SERVER_SCAN_LAST:
    break;
  }
}

/**************************************************************************
  SDL wraper on create_server_list(...) function witch add
  same functionality for LAN server dettection.
  WARING !: for LAN scan use "finish_lanserver_scan()" to free server list.
**************************************************************************/
static struct server_list *sdl_create_server_list(bool lan)
{
  struct server_list *server_list = NULL;
    
  if (lan) {
    pServer_scan = server_scan_begin(SERVER_SCAN_LOCAL, server_scan_error);      
    } else {
    pServer_scan = server_scan_begin(SERVER_SCAN_GLOBAL, server_scan_error);      
    }

  assert(pServer_scan);
  
  SDL_Delay(5000);
    
  int i;
  for (i = 0; i < 100; i++) {
    server_list = server_scan_get_servers(pServer_scan);
    if (server_list) {
      break;
    }
    SDL_Delay(100);
  }
  
  return server_list;
}


void popup_connection_dialog(struct GUI *pWidget)
{
  char cBuf[512];
  int w = 0, h = 0, count = 0, meta_h;
  struct GUI *pNewWidget, *pWindow;
  SDL_String16 *pStr;
  SDL_Surface *pLogo , *pDest = pWidget->dst;
  SDL_Color col = {255, 255, 255, 128};
  SDL_Rect area;
  bool lan_scan = (pWidget->ID != ID_JOIN_META_GAME);
  
  queue_flush();
  close_connection_dialog();
  popdown_meswin_dialog();
  
  my_snprintf(cBuf, sizeof(cBuf), _("Creating Server List..."));
  pStr = create_str16_from_char(cBuf, adj_font(16));
  pStr->style = TTF_STYLE_BOLD;
  pStr->bgcol = (SDL_Color) {0, 0, 0, 0};
  pLogo = create_text_surf_from_str16(pStr);
    
  area.w = pLogo->w + adj_size(60);
  area.h = pLogo->h + adj_size(30);
  area.x = (pDest->w - area.w) / 2;
  area.y = (pDest->h - area.h) / 2;
  SDL_FillRect(pDest, &area,
  		SDL_MapRGBA(pDest->format, col.r, col.g,col.b, col.unused));
  
  putframe(pDest, area.x, area.y, area.x + area.w - 1,
 		   area.y + area.h - 1, SDL_MapRGB(pDest->format, 0, 0, 0));
  
  sdl_dirty_rect(area);
  
  area.x += adj_size(30);
  area.y += adj_size(15);
  alphablit(pLogo, NULL, pDest, &area);
  unqueue_flush();
  
  FREESURFACE(pLogo);
  FREESTRING16(pStr);

  pServer_list = sdl_create_server_list(lan_scan);

  popup_meswin_dialog(true);        
  
  if(!pServer_list) {
    if (lan_scan) {
      append_output_window("No LAN servers found"); 
    } else {
      append_output_window("No public servers found"); 
    }        
    real_update_meswin_dialog();
    popup_join_game_dialog(NULL);
    return;
  }
  
  pMeta_Severs = fc_calloc(1, sizeof(struct ADVANCED_DLG));
    
  pWindow = create_window(pDest, NULL, adj_size(10), adj_size(10), 0);
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
  
  pNewWidget = create_themeicon_button_from_chars(pTheme->CANCEL_Icon, pWindow->dst,
						     _("Cancel"), adj_font(14), 0);
  pNewWidget->action = exit_meta_severs_dlg_callback;
  set_wstate(pNewWidget, FC_WS_NORMAL);
  add_to_gui_list(ID_BUTTON, pNewWidget);
  /* ------------------------------ */
  
  server_list_iterate(pServer_list, pServer) {

    my_snprintf(cBuf, sizeof(cBuf), "%s Port %s Ver: %s %s %s %s\n%s",
    	pServer->host, pServer->port, pServer->version, _(pServer->state),
    		_("Players"), pServer->nplayers, pServer->message);

    pNewWidget = create_iconlabel_from_chars(NULL, pWindow->dst, cBuf, adj_font(10),
	WF_FREE_STRING|WF_DRAW_TEXT_LABEL_WITH_SPACE|WF_DRAW_THEME_TRANSPARENT);
    
    pNewWidget->string16->style |= SF_CENTER;
    pNewWidget->string16->bgcol = (SDL_Color) {0, 0, 0, 0};
    
    pNewWidget->action = sellect_meta_severs_callback;
    set_wstate(pNewWidget, FC_WS_NORMAL);
    pNewWidget->data.ptr = (void *)pServer;
    
    add_to_gui_list(ID_BUTTON, pNewWidget);
    
    w = MAX(w, pNewWidget->size.w);
    h = MAX(h, pNewWidget->size.h);
    count++;
    
    if(count > 10) {
      set_wflag(pNewWidget, WF_HIDDEN);
    }
    
  } server_list_iterate_end;

  if(!count) {
    if (lan_scan) {
      append_output_window("No LAN servers found"); 
    } else {
      append_output_window("No public servers found"); 
    }        
    real_update_meswin_dialog();
    popup_join_game_dialog(NULL);
    return;
  }
  
  pMeta_Severs->pBeginWidgetList = pNewWidget;
  pMeta_Severs->pBeginActiveWidgetList = pNewWidget;
  pMeta_Severs->pEndActiveWidgetList = pWindow->prev->prev;
  pMeta_Severs->pActiveWidgetList = pWindow->prev->prev;
    
  if (count > 10) {
    meta_h = 10 * h;
  
    count = create_vertical_scrollbar(pMeta_Severs, 1, 10, TRUE, TRUE);
    w += count;
  } else {
    meta_h = count * h;
  }
  
  w += adj_size(20);
  area.h = meta_h;
  
  meta_h += pMeta_Severs->pEndWidgetList->prev->size.h + adj_size(10) + adj_size(20);
  
  pWindow->size.x = (pWindow->dst->w - w) /2;
  pWindow->size.y = (pWindow->dst->h - meta_h) /2;
  
  pLogo = get_logo_gfx();
  if (resize_window(pWindow , pLogo , NULL , w , meta_h)) {
    FREESURFACE(pLogo);
  }
  
  w -= adj_size(20);
  
  area.w = w + 1;
  
  if(pMeta_Severs->pScroll) {
    w -= count;
  }
  
  /* exit button */
  pNewWidget = pWindow->prev;
  pNewWidget->size.x = pWindow->size.x + pWindow->size.w - pNewWidget->size.w - adj_size(10);
  pNewWidget->size.y = pWindow->size.y + pWindow->size.h - pNewWidget->size.h - adj_size(10);
  
  /* meta labels */
  pNewWidget = pNewWidget->prev;
  
  pNewWidget->size.x = pWindow->size.x + adj_size(10);
  pNewWidget->size.y = pWindow->size.y + adj_size(10);
  pNewWidget->size.w = w;
  pNewWidget->size.h = h;
  pNewWidget = convert_iconlabel_to_themeiconlabel2(pNewWidget);
  
  pNewWidget = pNewWidget->prev;
  while(pNewWidget)
  {
        
    pNewWidget->size.w = w;
    pNewWidget->size.h = h;
    pNewWidget->size.x = pNewWidget->next->size.x;
    pNewWidget->size.y = pNewWidget->next->size.y + pNewWidget->next->size.h;
    pNewWidget = convert_iconlabel_to_themeiconlabel2(pNewWidget);
    
    if (pNewWidget == pMeta_Severs->pBeginActiveWidgetList) {
      break;
    }
    pNewWidget = pNewWidget->prev;  
  }
  
  if(pMeta_Severs->pScroll) {
    setup_vertical_scrollbar_area(pMeta_Severs->pScroll,
	pWindow->size.x + pWindow->size.w - adj_size(9),
	pMeta_Severs->pEndActiveWidgetList->size.y,
	pWindow->size.h - adj_size(30) - pWindow->prev->size.h, TRUE);
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
    
  flush_rect(pWindow->size, FALSE);
}

/**************************************************************************
  ...
**************************************************************************/
static int convert_playername_callback(struct GUI *pWidget)
{
  char *tmp = convert_to_chars(pWidget->string16->text);
  
  if (tmp) {  
    sz_strlcpy(user_name, tmp);
    FC_FREE(tmp);
  } else {
    /* empty input -> restore previous content */
    copy_chars_to_string16(pWidget->string16, user_name);
    redraw_edit(pWidget);
    sdl_dirty_rect(pWidget->size);
    flush_dirty();
  }
  
  return -1;
}

/**************************************************************************
...
**************************************************************************/
static int convert_servername_callback(struct GUI *pWidget)
{
  char *tmp = convert_to_chars(pWidget->string16->text);
  
  if (tmp) {
    sz_strlcpy(server_host, tmp);
    FC_FREE(tmp);
  } else {
    /* empty input -> restore previous content */
    copy_chars_to_string16(pWidget->string16, server_host);
    redraw_edit(pWidget);
    sdl_dirty_rect(pWidget->size);
    flush_dirty();
  }
  
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int convert_portnr_callback(struct GUI *pWidget)
{
  char pCharPort[6];
  char *tmp = convert_to_chars(pWidget->string16->text);
  
  if (tmp) {
    sscanf(tmp, "%d", &server_port);
    FC_FREE(tmp);
  } else {
    /* empty input -> restore previous content */
    my_snprintf(pCharPort, sizeof(pCharPort), "%d", server_port);
    copy_chars_to_string16(pWidget->string16, pCharPort);
    redraw_edit(pWidget);
    sdl_dirty_rect(pWidget->size);
    flush_dirty();
  }
  
  return -1;
}

static int cancel_connect_dlg_callback(struct GUI *pWidget)
{
  close_connection_dialog();
  set_client_page(PAGE_MAIN);
  return -1;
}


void popup_join_game_dialog(struct GUI *pWidget)
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
  int pos_y;
  int dialog_w, dialog_h;
  
     
  if(pWidget) {
    pDest = pWidget->dst;
  } else {
    pDest = Main.gui;
  }
  
  queue_flush();
  close_connection_dialog();
  
  pConnectDlg = fc_calloc(1, sizeof(struct SMALL_DLG));
  area = fc_calloc(1, sizeof(SDL_Rect)); 
    
  /* -------------------------- */

  pPlayer_name = create_str16_from_char(_("Player Name :"), adj_font(10));
  pPlayer_name->fgcol = color;
  pServer_name = create_str16_from_char(_("Freeciv Server :"), adj_font(10));
  pServer_name->fgcol = color;
  pPort_nr = create_str16_from_char(_("Port :"), adj_font(10));
  pPort_nr->fgcol = color;
  
  /* ====================== INIT =============================== */
  pBuf = create_edit_from_chars(NULL, pDest, user_name, adj_font(14), adj_size(210),
				(WF_DRAW_THEME_TRANSPARENT|WF_FREE_DATA));
  pBuf->action = convert_playername_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  add_to_gui_list(ID_PLAYER_NAME_EDIT, pBuf);
  
  pBuf->data.ptr = (void *)area;
  pConnectDlg->pEndWidgetList = pBuf;

  /* ------------------------------ */
  pBuf = create_edit_from_chars(NULL, pDest, server_host, adj_font(14), adj_size(210),
					 WF_DRAW_THEME_TRANSPARENT);

  pBuf->action = convert_servername_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  add_to_gui_list(ID_SERVER_NAME_EDIT, pBuf);

  /* ------------------------------ */
  my_snprintf(pCharPort, sizeof(pCharPort), "%d", server_port);
  
  pBuf = create_edit_from_chars(NULL, pDest, pCharPort, adj_font(14), adj_size(210),
					 WF_DRAW_THEME_TRANSPARENT);

  pBuf->action = convert_portnr_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  add_to_gui_list(ID_PORT_EDIT, pBuf);
  
  /* ------------------------------ */
  
  pBuf = create_themeicon_button_from_chars(pTheme->OK_Icon, pDest,
					      _("Connect"), adj_font(14), 0);
  pBuf->action = connect_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_RETURN;
  add_to_gui_list(ID_CONNECT_BUTTON, pBuf);
  
  /* ------------------------------ */
  
  pBuf = create_themeicon_button_from_chars(pTheme->CANCEL_Icon, pDest,
					       _("Cancel"), adj_font(14), 0);
  pBuf->action = cancel_connect_dlg_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  add_to_gui_list(ID_CANCEL_BUTTON, pBuf);
  pBuf->size.w = MAX(pBuf->size.w, pBuf->next->size.w);
  pBuf->next->size.w = pBuf->size.w;
  
  /* ------------------------------ */
  
  pConnectDlg->pBeginWidgetList = pBuf;
  
  /* ==================== Draw first time ===================== */
  dialog_w = adj_size(40) + pBuf->size.w * 2;
  dialog_w = MAX(dialog_w, adj_size(210));
  dialog_w += adj_size(80);
  
  #ifdef SMALL_SCREEN
  dialog_h = 110;
  #else
  dialog_h = 200;
  #endif

  pLogo = get_logo_gfx();
  pTmp = ResizeSurface(pLogo, dialog_w, dialog_h, 1);
  FREESURFACE(pLogo);

  area->x = (pDest->w - dialog_w)/ 2;
  area->y = (pDest->h - dialog_h)/ 2 + adj_size(40);
  alphablit(pTmp, NULL, pDest, area);
  FREESURFACE(pTmp);

  putframe(pDest, area->x, area->y, area->x + dialog_w - 1,
  					area->y + dialog_h - 1, 0xffffffff);

  area->w = dialog_w;
  area->h = dialog_h;
  /* ---------------------------------------- */
  
  /* user edit */
  pBuf = pConnectDlg->pEndWidgetList;
  
  start_x = area->x + (area->w - pBuf->size.w) / 2;
  start_y = area->y + adj_size(20);
  pos_y = start_y;
  
  write_text16(pDest, start_x + adj_size(5),
               pos_y,
               pPlayer_name);
  pos_y += str16height(pPlayer_name);

  draw_edit(pBuf, start_x, pos_y);
  pos_y += pBuf->size.h;
  /* -------- */
  
  pos_y += adj_size(5);
  
  /* server name */
  pBuf = pBuf->prev;
	       
  write_text16(pDest, start_x + adj_size(5), pos_y, pServer_name);
  pos_y += str16height(pServer_name);
                  
  draw_edit(pBuf, start_x, pos_y);
  pos_y += pBuf->size.h;
  /* -------- */

  pos_y += adj_size(5);

  /* port */
  pBuf = pBuf->prev;
	       
  write_text16(pDest, start_x + adj_size(5), pos_y, pPort_nr);
  pos_y += str16height(pPort_nr);
	       
  draw_edit(pBuf, start_x, pos_y);
  pos_y += pBuf->size.h;
  /* -------- */

  pos_y += adj_size(20);

  /* --------------------------------- */

  /* connect button */
  pBuf = pBuf->prev;
  draw_tibutton(pBuf, area->x + (dialog_w - (adj_size(40) + pBuf->size.w * 2)) / 2,
  						pos_y);
  draw_frame_around_widget(pBuf);
  
  /* cancel button */
  pBuf = pBuf->prev;
  draw_tibutton(pBuf, pBuf->next->size.x + pBuf->size.w + adj_size(40), pos_y);
  draw_frame_around_widget(pBuf);

  flush_all();

  /* ==================== Free Local Var ===================== */
  FREESTRING16(pPlayer_name);
  FREESTRING16(pServer_name);
  FREESTRING16(pPort_nr);
}

/**************************************************************************
  ...
**************************************************************************/
static int convert_passwd_callback(struct GUI *pWidget)
{
  char *tmp = convert_to_chars(pWidget->string16->text);
  if(tmp) {
    my_snprintf(password, MAX_LEN_NAME, "%s", tmp);
    FC_FREE(tmp);
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
  
  pConnectDlg = fc_calloc(1, sizeof(struct SMALL_DLG));
  area = fc_calloc(1, sizeof(SDL_Rect)); 
    
  /* -------------------------- */

  pStr = create_str16_from_char(pMessage, adj_font(12));
  pStr->fgcol = color_white;
    
  pText = create_text_surf_from_str16(pStr);
  
  /* ====================== INIT =============================== */
  change_ptsize16(pStr, adj_font(16));
  pStr->fgcol = color_black;
  FC_FREE(pStr->text);
  
  pBuf = create_edit(NULL, pDest, pStr, adj_size(210),
		(WF_PASSWD_EDIT|WF_DRAW_THEME_TRANSPARENT|WF_FREE_DATA));
  pBuf->action = convert_passwd_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  add_to_gui_list(ID_EDIT, pBuf);
  pBuf->data.ptr = (void *)area;
  pConnectDlg->pEndWidgetList = pBuf;
  /* ------------------------------ */
      
  pBuf = create_themeicon_button_from_chars(pTheme->OK_Icon, pDest,
						     _("Next"), adj_font(14), 0);
  pBuf->action = send_passwd_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_RETURN;
  add_to_gui_list(ID_BUTTON, pBuf);
  
  /* ------------------------------ */
  
  pBuf = create_themeicon_button_from_chars(pTheme->CANCEL_Icon, pDest,
						     _("Cancel"), adj_font(14), 0);
  pBuf->action = cancel_connect_dlg_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  add_to_gui_list(ID_CANCEL_BUTTON, pBuf);
  pBuf->size.w = MAX(pBuf->size.w, pBuf->next->size.w);
  pBuf->next->size.w = pBuf->size.w;
  
  /* ------------------------------ */
  
  pConnectDlg->pBeginWidgetList = pBuf;
  /* ==================== Draw first time ===================== */
  dialog_w = adj_size(40) + pBuf->size.w * 2;
  dialog_w = MAX(dialog_w, adj_size(210));
  dialog_w = MAX(dialog_w, pText->w);
  dialog_w += adj_size(80);
  dialog_h = adj_size(170);

  pLogo = get_logo_gfx();
  pTmp = ResizeSurface(pLogo, dialog_w, dialog_h, 1);
  FREESURFACE(pLogo);

  area->x = (pDest->w - dialog_w)/ 2;
  area->y = (pDest->h - dialog_h)/ 2 + adj_size(40);
  alphablit(pTmp, NULL, pDest, area);
  FREESURFACE(pTmp);

  putframe(pDest, area->x, area->y, area->x + dialog_w - 1,
  					area->y + dialog_h - 1, 0xffffffff);

  area->w = dialog_w;
  area->h = dialog_h;
  /* ---------------------------------------- */
  /* passwd edit */
  pBuf = pConnectDlg->pEndWidgetList;
  start_x = area->x + (area->w - pText->w) / 2;
  start_y = area->y + adj_size(50);
  blit_entire_src(pText, pDest, start_x, start_y);
  start_y += pText->h + adj_size(5);
  FREESURFACE(pText);
  start_x = area->x + (area->w - pBuf->size.w) / 2;
  draw_edit(pBuf, start_x, start_y);
  
  /* --------------------------------- */
  start_button_y = pBuf->size.y + pBuf->size.h + adj_size(25);

  /* connect button */
  pBuf = pBuf->prev;
  draw_tibutton(pBuf, area->x + (dialog_w - (adj_size(40) + pBuf->size.w * 2)) / 2,
  						start_button_y);
  draw_frame_around_widget(pBuf);
  
  /* cancel button */
  pBuf = pBuf->prev;
  draw_tibutton(pBuf, pBuf->next->size.x + pBuf->size.w + adj_size(40), start_button_y);
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
    FC_FREE(tmp);
    set_wstate(pWidget->prev, FC_WS_NORMAL);
    redraw_edit(pWidget->prev);
    flush_rect(pWidget->prev->size, FALSE);
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
    flush_rect(pWidget->prev->size, FALSE);
  } else {
    memset(password, 0, MAX_LEN_NAME);
    password[0] = '\0';
    
    FC_FREE(pWidget->next->string16->text);/* first edit */
    FC_FREE(pWidget->string16->text); /* secound edit */
    
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
  
  pConnectDlg = fc_calloc(1, sizeof(struct SMALL_DLG));
  area = fc_calloc(1, sizeof(SDL_Rect)); 
    
  /* -------------------------- */

  pStr = create_str16_from_char(pMessage, adj_font(12));
  pStr->fgcol = color_white;
    
  pText = create_text_surf_from_str16(pStr);
  
  /* ====================== INIT =============================== */
  change_ptsize16(pStr, adj_font(16));
  pStr->fgcol = color_black;
  
  FC_FREE(pStr->text);
  pStr->n_alloc = 0;
  
  pBuf = create_edit(NULL, pDest, pStr, adj_size(210),
		(WF_PASSWD_EDIT|WF_DRAW_THEME_TRANSPARENT|WF_FREE_DATA));
  pBuf->action = convert_first_passwd_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  add_to_gui_list(ID_EDIT, pBuf);
  pBuf->data.ptr = (void *)area;
  pConnectDlg->pEndWidgetList = pBuf;
  /* ------------------------------ */
  
  pBuf = create_edit(NULL, pDest, create_string16(NULL, 0, adj_font(16)) , adj_size(210),
		(WF_PASSWD_EDIT|WF_DRAW_THEME_TRANSPARENT|WF_FREE_DATA));
  pBuf->action = convert_secound_passwd_callback;
  add_to_gui_list(ID_EDIT, pBuf);
  
  /* ------------------------------ */    
  pBuf = create_themeicon_button_from_chars(pTheme->OK_Icon, pDest,
						     _("Next"), adj_font(14), 0);
  pBuf->action = send_passwd_callback;
  pBuf->key = SDLK_RETURN;  
  add_to_gui_list(ID_BUTTON, pBuf);
  
  /* ------------------------------ */
  
  pBuf = create_themeicon_button_from_chars(pTheme->CANCEL_Icon, pDest,
						     _("Cancel"), adj_font(14), 0);
  pBuf->action = cancel_connect_dlg_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  add_to_gui_list(ID_CANCEL_BUTTON, pBuf);
  pBuf->size.w = MAX(pBuf->size.w, pBuf->next->size.w);
  pBuf->next->size.w = pBuf->size.w;
  
  /* ------------------------------ */
  
  pConnectDlg->pBeginWidgetList = pBuf;
  /* ==================== Draw first time ===================== */
  dialog_w = adj_size(40) + pBuf->size.w * 2;
  dialog_w = MAX(dialog_w, adj_size(210));
  dialog_w = MAX(dialog_w, pText->w);
  dialog_w += adj_size(80);
  dialog_h = adj_size(180);

  pLogo = get_logo_gfx();
  pTmp = ResizeSurface(pLogo, dialog_w, dialog_h, 1);
  FREESURFACE(pLogo);

  area->x = (pDest->w - dialog_w)/ 2;
  area->y = (pDest->h - dialog_h)/ 2 + adj_size(40);
  alphablit(pTmp, NULL, pDest, area);
  FREESURFACE(pTmp);

  putframe(pDest, area->x, area->y, area->x + dialog_w - 1,
  					area->y + dialog_h - 1, 0xffffffff);

  area->w = dialog_w;
  area->h = dialog_h;
  /* ---------------------------------------- */
  /* passwd edit */
  pBuf = pConnectDlg->pEndWidgetList;
  start_x = area->x + (area->w - pText->w) / 2;
  start_y = area->y + adj_size(30);
  blit_entire_src(pText, pDest, start_x, start_y);
  start_y += pText->h + adj_size(5);
  FREESURFACE(pText);
  
  start_x = area->x + (area->w - pBuf->size.w) / 2;
  draw_edit(pBuf, start_x, start_y);
  start_y += pBuf->size.h + adj_size(5);
  
  /* retype passwd */
  pBuf = pBuf->prev;
  draw_edit(pBuf, start_x, start_y);
  
  /* --------------------------------- */
  start_button_y = pBuf->size.y + pBuf->size.h + adj_size(25);

  /* connect button */
  pBuf = pBuf->prev;
  draw_tibutton(pBuf, area->x + (dialog_w - (adj_size(40) + pBuf->size.w * 2)) / 2,
  						start_button_y);
  draw_frame_around_widget(pBuf);
  
  /* cancel button */
  pBuf = pBuf->prev;
  draw_tibutton(pBuf, pBuf->next->size.x + pBuf->size.w + adj_size(40), start_button_y);
  draw_frame_around_widget(pBuf);

  flush_all();
}

/* ======================================================================== */

/**************************************************************************
 close and destroy the dialog.
**************************************************************************/
void close_connection_dialog(void)
{
  if(pConnectDlg) {
    SDL_FillRect(pConnectDlg->pEndWidgetList->dst,
    			(SDL_Rect *)pConnectDlg->pEndWidgetList->data.ptr, 0x0);
  
    sdl_dirty_rect(*((SDL_Rect *)pConnectDlg->pEndWidgetList->data.ptr));
    
  
    del_group_of_widgets_from_gui_list(pConnectDlg->pBeginWidgetList,
    						pConnectDlg->pEndWidgetList);
    FC_FREE(pConnectDlg);
  }
  if(pMeta_Severs) {
    popdown_window_group_dialog(pMeta_Severs->pBeginWidgetList,
				  pMeta_Severs->pEndWidgetList);
    FC_FREE(pMeta_Severs->pScroll);
    FC_FREE(pMeta_Severs);
    if(pServer_list) {
      server_scan_finish(pServer_scan);
      pServer_scan = NULL;
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
}
