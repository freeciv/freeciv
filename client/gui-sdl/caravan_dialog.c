/********************************************************************** 
 Freeciv - Copyright (C) 1996-2006 - Freeciv Development Team
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

/* utility */
#include "fcintl.h"

/* client */
#include "civclient.h"
#include "clinet.h"
#include "control.h"

/* gui-sdl */
#include "gui_id.h"
#include "graphics.h"
#include "gui_stuff.h"
#include "mapview.h"
#include "gui_tilespec.h"

#include "dialogs.h"

extern bool is_unit_move_blocked;

void popdown_caravan_dialog(void);

static int caravan_city_id;
static int caravan_unit_id;

/* ====================================================================== */
/* ============================ CARAVAN DIALOG ========================== */
/* ====================================================================== */
static struct SMALL_DLG *pCaravan_Dlg = NULL;

static int caravan_dlg_window_callback(struct widget *pWindow)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    move_window_group(pCaravan_Dlg->pBeginWidgetList, pWindow);
  }
  return -1;
}

/****************************************************************
...
*****************************************************************/
static int caravan_establish_trade_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    dsend_packet_unit_establish_trade(&aconnection, pWidget->data.cont->id0);
    
    popdown_caravan_dialog();
  }
  return -1;
}


/****************************************************************
...
*****************************************************************/
static int caravan_help_build_wonder_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    dsend_packet_unit_help_build_wonder(&aconnection, pWidget->data.cont->id0);
    
    popdown_caravan_dialog();  
  }
  return -1;
}

static int exit_caravan_dlg_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    popdown_caravan_dialog();
    process_caravan_arrival(NULL);
  }
  return -1;
}
  
void popdown_caravan_dialog(void)
{
  if (pCaravan_Dlg) {
    is_unit_move_blocked = FALSE;
    popdown_window_group_dialog(pCaravan_Dlg->pBeginWidgetList,
				pCaravan_Dlg->pEndWidgetList);
    FC_FREE(pCaravan_Dlg);
    flush_dirty();
  }
}

/**************************************************************************
  Popup a dialog giving a player choices when their caravan arrives at
  a city (other than its home city).  Example:
    - Establish traderoute.
    - Help build wonder.
    - Keep moving.
**************************************************************************/
void popup_caravan_dialog(struct unit *pUnit,
			  struct city *pHomecity, struct city *pDestcity)
{
  struct widget *pWindow = NULL, *pBuf = NULL;
  SDL_String16 *pStr;
  int w = 0, h;
  struct CONTAINER *pCont;
  char cBuf[128];
  
  if (pCaravan_Dlg) {
    return;
  }

  caravan_unit_id=pUnit->id;
  caravan_city_id=pDestcity->id;
  
  pCont = fc_calloc(1, sizeof(struct CONTAINER));
  pCont->id0 = pUnit->id;
  pCont->id1 = pDestcity->id;
  
  pCaravan_Dlg = fc_calloc(1, sizeof(struct SMALL_DLG));
  is_unit_move_blocked = TRUE;
  h = WINDOW_TITLE_HEIGHT + adj_size(3) + pTheme->FR_Bottom->h;
      
  my_snprintf(cBuf, sizeof(cBuf), _("Your caravan has arrived at %s"),
							  pDestcity->name);
  
  /* window */
  pStr = create_str16_from_char(cBuf, adj_font(12));
  pStr->style |= TTF_STYLE_BOLD;
  
  pWindow = create_window(NULL, pStr, adj_size(10), adj_size(10), 0);
    
  pWindow->action = caravan_dlg_window_callback;
  set_wstate(pWindow, FC_WS_NORMAL);
  w = MAX(w, pWindow->size.w);
  
  add_to_gui_list(ID_CARAVAN_DLG_WINDOW, pWindow);
  pCaravan_Dlg->pEndWidgetList = pWindow;
    
  /* ---------- */
  if (can_cities_trade(pHomecity, pDestcity))
  {
    int revenue = get_caravan_enter_city_trade_bonus(pHomecity, pDestcity);
    
    if (can_establish_trade_route(pHomecity, pDestcity)) {
      my_snprintf(cBuf, sizeof(cBuf),
      		_("Establish Traderoute with %s ( %d R&G + %d trade )"),
      		pHomecity->name, revenue,
      			trade_between_cities(pHomecity, pDestcity));
    } else {
      revenue = (revenue + 2) / 3;
      my_snprintf(cBuf, sizeof(cBuf),
		_("Enter Marketplace ( %d R&G bonus )"), revenue);
    }
    
    create_active_iconlabel(pBuf, pWindow->dst, pStr,
	    cBuf, caravan_establish_trade_callback);
    pBuf->data.cont = pCont;
    set_wstate(pBuf, FC_WS_NORMAL);
  
    add_to_gui_list(ID_LABEL, pBuf);
    
    w = MAX(w, pBuf->size.w);
    h += pBuf->size.h;
  }
  
  /* ---------- */
  if (unit_can_help_build_wonder(pUnit, pDestcity)) {
        
    create_active_iconlabel(pBuf, pWindow->dst, pStr,
	_("Help build Wonder"), caravan_help_build_wonder_callback);
    
    pBuf->data.cont = pCont;
    set_wstate(pBuf, FC_WS_NORMAL);
  
    add_to_gui_list(ID_LABEL, pBuf);
    
    w = MAX(w, pBuf->size.w);
    h += pBuf->size.h;
  }
  /* ---------- */
  
  create_active_iconlabel(pBuf, pWindow->dst, pStr,
	    _("Keep moving"), exit_caravan_dlg_callback);
  
  pBuf->data.cont = pCont;
  set_wstate(pBuf, FC_WS_NORMAL);
  set_wflag(pBuf, WF_FREE_DATA);
  pBuf->key = SDLK_ESCAPE;
  
  add_to_gui_list(ID_LABEL, pBuf);
    
  w = MAX(w, pBuf->size.w);
  h += pBuf->size.h;
  /* ---------- */
  pCaravan_Dlg->pBeginWidgetList = pBuf;
  
  /* setup window size and start position */
  
  pWindow->size.w = pTheme->FR_Left->w + w + pTheme->FR_Right->w;
  pWindow->size.h = h;
  
  auto_center_on_focus_unit();
  put_window_near_map_tile(pWindow,
  		pTheme->FR_Left->w + w + pTheme->FR_Right->w, h, pUnit->tile);
  resize_window(pWindow, NULL, NULL, pWindow->size.w, h);
  
  /* setup widget size and start position */
    
  pBuf = pWindow->prev;
  setup_vertical_widgets_position(1,
	pWindow->size.x + pTheme->FR_Left->w,
  	pWindow->size.y + WINDOW_TITLE_HEIGHT + adj_size(2), w, 0,
	pCaravan_Dlg->pBeginWidgetList, pBuf);
  /* --------------------- */
  /* redraw */
  redraw_group(pCaravan_Dlg->pBeginWidgetList, pWindow, 0);

  flush_rect(pWindow->size, FALSE);
}

/**************************************************************************
  Is there currently a caravan dialog open?  This is important if there
  can be only one such dialog at a time; otherwise return FALSE.
**************************************************************************/
bool caravan_dialog_is_open(int *unit_id, int *city_id)
{
  if (unit_id) {
    *unit_id = caravan_unit_id;
  }
  if (city_id) {
    *city_id = caravan_city_id;
  }

  return pCaravan_Dlg != NULL;
}

/****************************************************************
  Updates caravan dialog
****************************************************************/
void caravan_dialog_update(void)
{
  /* PORT ME */
}
