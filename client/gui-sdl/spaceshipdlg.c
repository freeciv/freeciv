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

#include <SDL/SDL.h>

#include "fcintl.h"
#include "game.h"
#include "genlist.h"
#include "map.h"

#include "gui_mem.h"

#include "packets.h"
#include "player.h"
#include "shared.h"
#include "support.h"

#include "clinet.h"
#include "colors.h"
#include "dialogs.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_id.h"
#include "gui_string.h"
#include "gui_stuff.h"
#include "gui_tilespec.h"
#include "gui_zoom.h"

#include "helpdlg.h"
#include "inputdlg.h"
#include "mapctrl.h"
#include "mapview.h"
#include "repodlgs.h"
#include "spaceship.h"
#include "tilespec.h"
#include "climisc.h"

#include "spaceshipdlg.h"

#define SPECLIST_TAG dialog
#define SPECLIST_TYPE struct SMALL_DLG
#include "speclist.h"

#define dialog_list_iterate(dialoglist, pdialog) \
    TYPED_LIST_ITERATE(struct SMALL_DLG, dialoglist, pdialog)
#define dialog_list_iterate_end  LIST_ITERATE_END
    
static struct dialog_list dialog_list;
static bool dialog_list_has_been_initialised = FALSE;

/****************************************************************
...
*****************************************************************/
static struct SMALL_DLG *get_spaceship_dialog(struct player *pplayer)
{
  if (!dialog_list_has_been_initialised) {
    dialog_list_init(&dialog_list);
    dialog_list_has_been_initialised = TRUE;
  }

  dialog_list_iterate(dialog_list, pDialog) {
    if (pDialog->pEndWidgetList->data.player == pplayer) {
      return pDialog;
    }
  } dialog_list_iterate_end;

  return NULL;
}

static int space_dialog_window_callback(struct GUI *pWindow)
{
  return std_move_window_group_callback(
  	pWindow->private_data.small_dlg->pBeginWidgetList, pWindow);
}

static int exit_space_dialog_callback(struct GUI *pWidget)
{
  popdown_spaceship_dialog(pWidget->data.player);
  flush_dirty();
  return -1;
}

static int launch_spaceship_callback(struct  GUI *pWidget)
{
  send_packet_spaceship_launch(&aconnection);
  return -1;
}

/**************************************************************************
  Refresh (update) the spaceship dialog for the given player.
**************************************************************************/
void refresh_spaceship_dialog(struct player *pPlayer)
{
  struct SMALL_DLG *pSpaceShp;
  struct GUI *pBuf;
    
  if(!(pSpaceShp = get_spaceship_dialog(pPlayer)))
    return;
  
  /* launch button */
  pBuf = pSpaceShp->pEndWidgetList->prev->prev;
  if(game.spacerace
     && pPlayer->player_no == game.player_idx
     && pPlayer->spaceship.state == SSHIP_STARTED
     && pPlayer->spaceship.success_rate > 0.0) {
    set_wstate(pBuf, FC_WS_NORMAL);
  }
  
  /* update text info */
  pBuf = pBuf->prev;
  copy_chars_to_string16(pBuf->string16,
				get_spaceship_descr(&pPlayer->spaceship));
    
  /* ------------------------------------------ */
  
    /* redraw */
  redraw_group(pSpaceShp->pBeginWidgetList, pSpaceShp->pEndWidgetList, 0);
  sdl_dirty_rect(pSpaceShp->pEndWidgetList->size);
  
  flush_dirty();
}

/**************************************************************************
  Popup (or raise) the spaceship dialog for the given player.
**************************************************************************/
void popup_spaceship_dialog(struct player *pPlayer)
{
  struct SMALL_DLG *pSpaceShp;

  if(!(pSpaceShp = get_spaceship_dialog(pPlayer))) {
    struct GUI *pBuf, *pWindow;
    SDL_String16 *pStr;
    char cBuf[128];
    int w = 0, h = 0;
  
    pSpaceShp = MALLOC(sizeof(struct SMALL_DLG));
    
    my_snprintf(cBuf, sizeof(cBuf), _("%s's SpaceShip"),
				    get_nation_name(pPlayer->nation));
    pStr = create_str16_from_char(cBuf, 12);
    pStr->style |= TTF_STYLE_BOLD;
  
    pWindow = create_window(NULL, pStr, 10, 10, WF_DRAW_THEME_TRANSPARENT);
  
    pWindow->action = space_dialog_window_callback;
    set_wstate(pWindow, FC_WS_NORMAL);
    w = MAX(w, pWindow->size.w);
    h = WINDOW_TILE_HIGH + 1;
    pWindow->data.player = pPlayer;
    pWindow->private_data.small_dlg = pSpaceShp;
    add_to_gui_list(ID_WINDOW, pWindow);
    pSpaceShp->pEndWidgetList = pWindow;
  
    /* ---------- */
    /* create exit button */
    pBuf = create_themeicon(pTheme->Small_CANCEL_Icon, pWindow->dst,
  			  			WF_DRAW_THEME_TRANSPARENT);
    pBuf->data.player = pPlayer;
    pBuf->action = exit_space_dialog_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
    pBuf->key = SDLK_ESCAPE;
    w += (pBuf->size.w + 10);
  
    add_to_gui_list(ID_BUTTON, pBuf);
  
    pBuf = create_themeicon_button_from_chars(pTheme->OK_Icon, pWindow->dst,
					      _("Launch"), 12, 0);
        
    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
    pBuf->action = launch_spaceship_callback;
    w = MAX(w, pBuf->size.w);
    h += pBuf->size.h + 20;
    add_to_gui_list(ID_BUTTON, pBuf);
    
    pStr = create_str16_from_char(get_spaceship_descr(NULL), 12);
    pStr->render = 3;
    pStr->bgcol.unused = 128;
    pBuf = create_iconlabel(NULL, pWindow->dst, pStr, WF_DRAW_THEME_TRANSPARENT);
    w = MAX(w, pBuf->size.w);
    h += pBuf->size.h + 20;
    add_to_gui_list(ID_LABEL, pBuf);

    pSpaceShp->pBeginWidgetList = pBuf;
    /* -------------------------------------------------------- */
  
    w = MAX(w, 300);
      
    pWindow->size.x = (Main.screen->w - w) / 2;
    pWindow->size.y = (Main.screen->h - h) / 2;
  
    resize_window(pWindow, NULL, NULL, w, h);
     
    /* exit button */
    pBuf = pWindow->prev;
    pBuf->size.x = pWindow->size.x + pWindow->size.w - pBuf->size.w - FRAME_WH - 1;
    pBuf->size.y = pWindow->size.y + 1;

    /* launch button */
    pBuf = pBuf->prev;
    pBuf->size.x = pWindow->size.x + (pWindow->size.w  - pBuf->size.w) / 2;
    pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 10;
  
    /* info label */
    pBuf = pBuf->prev;
    pBuf->size.x = pWindow->size.x + (pWindow->size.w - pBuf->size.w) / 2;
    pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 1 + 10;

    dialog_list_insert(&dialog_list, pSpaceShp);
    
    refresh_spaceship_dialog(pPlayer);
  } else {
    if (sellect_window_group_dialog(pSpaceShp->pBeginWidgetList,
				   pSpaceShp->pEndWidgetList)) {
      flush_rect(pSpaceShp->pEndWidgetList->size);
    }
  }
  
}

/**************************************************************************
  Close the spaceship dialog for the given player.
**************************************************************************/
void popdown_spaceship_dialog(struct player *pPlayer)
{
  struct SMALL_DLG *pSpaceShp;

  if((pSpaceShp = get_spaceship_dialog(pPlayer))) {
    popdown_window_group_dialog(pSpaceShp->pBeginWidgetList,
					    pSpaceShp->pEndWidgetList);
    dialog_list_unlink(&dialog_list, pSpaceShp);
    FREE(pSpaceShp);
  }
  
}
