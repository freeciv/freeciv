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
                          citydlg.c  -  description
                             -------------------
    begin                : Wed Sep 04 2002
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafa³ Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

#include "city.h"
#include "fcintl.h"
#include "game.h"
#include "map.h"
#include "improvement.h"

#include "gui_mem.h"

#include "packets.h"
#include "player.h"
#include "shared.h"
#include "support.h"

#include "cityrep.h"
#include "cma_fe.h"
#include "cma_fec.h"
#include "colors.h"
#include "control.h"
#include "clinet.h"
#include "dialogs.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_id.h"
#include "gui_string.h"
#include "gui_stuff.h"
#include "gui_zoom.h"
#include "happiness.h"
#include "helpdlg.h"
#include "inputdlg.h"
#include "mapctrl.h"
#include "mapview.h"
#include "options.h"
#include "repodlgs.h"
#include "tilespec.h"
#include "wldlg.h"
#include "gui_tilespec.h"

#include "optiondlg.h"
#include "menu.h"

/*#include "cityicon.ico"*/

#include "citydlg.h"

#if 0
/* get 'struct dialog_list' and related functions: */
struct city_dialog;

#define SPECLIST_TAG dialog
#define SPECLIST_TYPE struct city_dialog
#include "speclist.h"

#define SPECLIST_TAG dialog
#define SPECLIST_TYPE struct city_dialog
#include "speclist_c.h"

#define dialog_list_iterate(dialoglist, pdialog) \
    TYPED_LIST_ITERATE(struct city_dialog, dialoglist, pdialog)
#define dialog_list_iterate_end  LIST_ITERATE_END

static int NUM_UNITS_SHOWN;
static int MAX_UNIT_ROWS;
static int MINI_NUM_UNITS;
#endif

/* ============================================================= */
#define SCALLED_TILE_WIDTH	48
#define SCALLED_TILE_HEIGHT	24
#define NUM_UNITS_SHOWN		 3

extern char *pDataPath;

static struct city_dialog {
  struct city *pCity;

  enum {
    INFO_PAGE = 0,
    HAPPINESS_PAGE = 1,
    ARMY_PAGE,
    SUPPORTED_UNITS_PAGE,
    MISC_PAGE
  } state;

  /* main window group list */
  struct GUI *pBeginCityWidgetList;
  struct GUI *pEndCityWidgetList;

  /* Imprvm. vscrollbar */
  struct GUI *pBeginCityImprWidgetList;
  struct GUI *pBeginActiveCityImprWidgetList;
  struct GUI *pEndCityImprWidgetList;
  struct ScrollBar *pVscroll;
  
  /* penel group list */
  struct GUI *pBeginCityPanelWidgetList;
  struct GUI *pBeginActiveCityPanelWidgetList;
  struct GUI *pEndCityPanelWidgetList;
  struct ScrollBar *pPanelVscroll;

  /* Menu imprv. dlg. */
  struct GUI *pBeginCityMenuWidgetList;
  struct GUI *pEndCityMenuWidgetList;

  /* shortcuts */
  struct GUI *pBuy_Button;
  struct GUI *pResource_Map;
  struct GUI *pCity_Name_Edit;

  SDL_Rect *specs_area[3];	/* active area of specialist
				   0 - elvis
				   1 - taxman
				   2 - scientists
				   change when pressed on this area */


} *pCityDlg = NULL;

static struct ADVANCED_DLG *pChange_Prod_Dlg = NULL;

static struct SMALL_DLG *pHurry_Prod_Dlg = NULL;

static void popdown_change_production_dialog(void);
static void popdown_hurry_production_dialog(void);
static void refresh_city_resource_map(struct GUI *pMap,
				      struct city *pCity);
static void enable_city_dlg_widgets(void);
static void disable_city_dlg_widgets(void);
static void redraw_city_dialog(struct city *pCity);
static void rebuild_imprm_list(struct city *pCity);
static void rebuild_citydlg_title_str(struct GUI *pWindow,
				      struct city *pCity);

/* ======================================================================= */


/**************************************************************************
  Destroy City Dlg but not undraw.
**************************************************************************/
static void del_city_dialog(void)
{
  if (pCityDlg) {

    if (pCityDlg->pEndCityImprWidgetList) {
      del_group_of_widgets_from_gui_list(pCityDlg->
					 pBeginCityImprWidgetList,
					 pCityDlg->pEndCityImprWidgetList);

      FREE(pCityDlg->pVscroll);
    }

    if (pChange_Prod_Dlg) {
      del_group_of_widgets_from_gui_list(pChange_Prod_Dlg->pBeginWidgetList,
				     pChange_Prod_Dlg->pEndWidgetList);
  
      FREE(pChange_Prod_Dlg->pScroll);
      FREE( pChange_Prod_Dlg );
    }

    if (pHurry_Prod_Dlg)
    {
      del_group_of_widgets_from_gui_list(pHurry_Prod_Dlg->pBeginWidgetList,
			      		 pHurry_Prod_Dlg->pEndWidgetList);

      FREE( pHurry_Prod_Dlg );
    }
    
    if (pCityDlg->pEndCityPanelWidgetList) {
      del_group_of_widgets_from_gui_list(pCityDlg->
					 pBeginCityPanelWidgetList,
					 pCityDlg->
					 pEndCityPanelWidgetList);

      FREE(pCityDlg->pPanelVscroll);
    }

    if (pCityDlg->pEndCityMenuWidgetList) {
      del_group_of_widgets_from_gui_list(pCityDlg->
					 pBeginCityMenuWidgetList,
					 pCityDlg->pEndCityMenuWidgetList);
    }


    del_group_of_widgets_from_gui_list(pCityDlg->pBeginCityWidgetList,
				       pCityDlg->pEndCityWidgetList);

    FREE(pCityDlg->specs_area[0]);
    FREE(pCityDlg->specs_area[1]);
    FREE(pCityDlg->specs_area[2]);
    FREE(pCityDlg->pVscroll);
    FREE(pCityDlg);

    SDL_Client_Flags &= ~CF_CITY_DIALOG_IS_OPEN;
  }
}

/**************************************************************************
  Main Citu Dlg. window callback.
  Here was implemented change specialist ( Elvis, Taxman, Scientist ) code. 
**************************************************************************/
static int city_dlg_callback(struct GUI *pWindow)
{
  enum specialist_type type;

  /* check elvis area */
  if (pCityDlg->specs_area[0]) {
    if ((Main.event.motion.x >= pCityDlg->specs_area[0]->x) &&
	(Main.event.motion.x <=
	 pCityDlg->specs_area[0]->x + pCityDlg->specs_area[0]->w)
	&& (Main.event.motion.y >= pCityDlg->specs_area[0]->y)
	&& (Main.event.motion.y <=
	    pCityDlg->specs_area[0]->y + pCityDlg->specs_area[0]->h)) {
      type = SP_ELVIS;
      goto SEND;
    }
  }

  /* check TAXMANs area */
  if (pCityDlg->specs_area[1]) {
    if ((Main.event.motion.x >= pCityDlg->specs_area[1]->x) &&
	(Main.event.motion.x <=
	 pCityDlg->specs_area[1]->x + pCityDlg->specs_area[1]->w)
	&& (Main.event.motion.y >= pCityDlg->specs_area[1]->y)
	&& (Main.event.motion.y <=
	    pCityDlg->specs_area[1]->y + pCityDlg->specs_area[1]->h)) {
      type = SP_TAXMAN;
      goto SEND;
    }
  }

  /* check SCIENTISTs area */
  if (pCityDlg->specs_area[2]) {
    if ((Main.event.motion.x >= pCityDlg->specs_area[2]->x) &&
	(Main.event.motion.x <=
	 pCityDlg->specs_area[2]->x + pCityDlg->specs_area[2]->w)
	&& (Main.event.motion.y >= pCityDlg->specs_area[2]->y)
	&& (Main.event.motion.y <=
	    pCityDlg->specs_area[2]->y + pCityDlg->specs_area[2]->h)) {
      type = SP_SCIENTIST;
      goto SEND;
    }
  }

#if 0  
  if (move_window_group_dialog(pCityDlg->pBeginCityWidgetList, pWindow)) {
    sellect_window_group_dialog(pCityDlg->pBeginCityWidgetList, pWindow);
    redraw_city_dialog(pCityDlg->pCity);
    flush_dirty();
  } else {
    if(sellect_window_group_dialog(pCityDlg->pBeginCityWidgetList, pWindow)) {
      flush_rect(pWindow->size);
    }      
  }
#endif
  return -1;

SEND:
  {
    struct packet_city_request packet;
    packet.city_id = pCityDlg->pCity->id;
    packet.specialist_from = type;

    switch (type) {
    case SP_ELVIS:
      packet.specialist_to = SP_TAXMAN;
      break;
    case SP_TAXMAN:
      packet.specialist_to = SP_SCIENTIST;
      break;
    case SP_SCIENTIST:
      packet.specialist_to = SP_ELVIS;
      break;
    }

    send_packet_city_request(&aconnection, &packet,
			     PACKET_CITY_CHANGE_SPECIALIST);
  }
  return -1;
}

/* ===================================================================== */
/* ========================== Units Orders Menu ======================== */
/* ===================================================================== */

/**************************************************************************
  Popdown unit city orders menu.
**************************************************************************/
static int cancel_units_orders_city_dlg_callback(struct GUI *pButton)
{
  popdown_window_group_dialog(pCityDlg->pBeginCityMenuWidgetList,
			      pCityDlg->pEndCityMenuWidgetList);

  pCityDlg->pEndCityMenuWidgetList = NULL;

  /* enable city dlg */
  enable_city_dlg_widgets();
  
  flush_dirty();
  
  return -1;
}

/**************************************************************************
  activate unit and del unit order dlg. widget group.
  update screen is unused becous common code call here 
  redraw_city_dialog(pCityDlg->pCity);
**************************************************************************/
static int activate_units_orders_city_dlg_callback(struct GUI *pButton)
{
  struct unit *pUnit;

  if ((pUnit =
       player_find_unit_by_id(game.player_ptr, MAX_ID - pButton->ID))) {
    set_unit_focus(pUnit);
  }

  del_group_of_widgets_from_gui_list(pCityDlg->pBeginCityMenuWidgetList,
				     pCityDlg->pEndCityMenuWidgetList);

  pCityDlg->pEndCityMenuWidgetList = NULL;

  /* enable city dlg */
  enable_city_dlg_widgets();

  return -1;
}

/**************************************************************************
  activate unit and popdow city dlg. + center on unit.
**************************************************************************/
static int activate_and_exit_units_orders_city_dlg_callback(struct GUI *pButton)
{
  struct unit *pUnit;

  if ((pUnit =
       player_find_unit_by_id(game.player_ptr, MAX_ID - pButton->ID))) {

    /* clear city area */	 
    blit_entire_src(pCityDlg->pEndCityWidgetList->gfx,
			 pCityDlg->pEndCityWidgetList->dst, 
    				pCityDlg->pEndCityWidgetList->size.x,
				    pCityDlg->pEndCityWidgetList->size.y);
	 
    del_city_dialog();
	 
    center_tile_mapcanvas(pUnit->x, pUnit->y);

    set_unit_focus(pUnit);
    flush_dirty();
  }

  return -1;
}

/**************************************************************************
  sentry unit and del unit order dlg. widget group.
  update screen is unused becous common code call here 
  redraw_city_dialog(pCityDlg->pCity);
**************************************************************************/
static int sentry_units_orders_city_dlg_callback(struct GUI *pButton)
{
  struct unit *pUnit;
  Unit_Type_id id = MAX_ID - pButton->ID;

  del_group_of_widgets_from_gui_list(pCityDlg->pBeginCityMenuWidgetList,
				     pCityDlg->pEndCityMenuWidgetList);

  pCityDlg->pEndCityMenuWidgetList = NULL;

  /* enable city dlg */
  enable_city_dlg_widgets();

  if ((pUnit = player_find_unit_by_id(game.player_ptr, id))) {
    request_unit_sentry(pUnit);
  }

  return -1;
}

/**************************************************************************
  fortify unit and del unit order dlg. widget group.
  update screen is unused becous common code call here 
  redraw_city_dialog(pCityDlg->pCity);
**************************************************************************/
static int fortify_units_orders_city_dlg_callback(struct GUI *pButton)
{
  struct unit *pUnit;
  Unit_Type_id id = MAX_ID - pButton->ID;

  del_group_of_widgets_from_gui_list(pCityDlg->pBeginCityMenuWidgetList,
				     pCityDlg->pEndCityMenuWidgetList);

  pCityDlg->pEndCityMenuWidgetList = NULL;

  /* enable city dlg */
  enable_city_dlg_widgets();

  if ((pUnit = player_find_unit_by_id(game.player_ptr, id))) {
    request_unit_fortify(pUnit);
  }

  return -1;
}

/**************************************************************************
  disband unit and del unit order dlg. widget group.
  update screen is unused becous common code call here 
  redraw_city_dialog(pCityDlg->pCity);
**************************************************************************/
static int disband_units_orders_city_dlg_callback(struct GUI *pButton)
{
  struct unit *pUnit;
  Unit_Type_id id = MAX_ID - pButton->ID;

  del_group_of_widgets_from_gui_list(pCityDlg->pBeginCityPanelWidgetList,
				     pCityDlg->pEndCityPanelWidgetList);
  pCityDlg->pEndCityPanelWidgetList = NULL;
  FREE(pCityDlg->pPanelVscroll);

  del_group_of_widgets_from_gui_list(pCityDlg->pBeginCityMenuWidgetList,
				     pCityDlg->pEndCityMenuWidgetList);

  pCityDlg->pEndCityMenuWidgetList = NULL;

  /* enable city dlg */
  enable_city_dlg_widgets();

  /* ugly hack becouse this free unit widget list*/
  pCityDlg->state = INFO_PAGE;

  if ((pUnit = player_find_unit_by_id(game.player_ptr, id))) {
    request_unit_disband(pUnit);
  }

  return -1;
}

/**************************************************************************
  homecity unit and del unit order dlg. widget group.
  update screen is unused becous common code call here 
  redraw_city_dialog(pCityDlg->pCity);
**************************************************************************/
static int homecity_units_orders_city_dlg_callback(struct GUI *pButton)
{
  struct unit *pUnit;
  Unit_Type_id id = MAX_ID - pButton->ID;

  del_group_of_widgets_from_gui_list(pCityDlg->pBeginCityMenuWidgetList,
				     pCityDlg->pEndCityMenuWidgetList);

  pCityDlg->pEndCityMenuWidgetList = NULL;

  /* enable city dlg */
  enable_city_dlg_widgets();

  if ((pUnit = player_find_unit_by_id(game.player_ptr, id))) {
    request_unit_change_homecity(pUnit);
  }

  return -1;
}

/**************************************************************************
  upgrade unit and del unit order dlg. widget group.
  update screen is unused becous common code call here 
  redraw_city_dialog(pCityDlg->pCity);
**************************************************************************/
static int upgrade_units_orders_city_dlg_callback(struct GUI *pButton)
{
  /* Port Me */
  return -1;
}

/**************************************************************************
  Main unit order dlg. callback.
**************************************************************************/
static int units_orders_dlg_callback(struct GUI *pButton)
{
  return -1;
}

/**************************************************************************
  popup units orders menu.
**************************************************************************/
static int units_orders_city_dlg_callback(struct GUI *pButton)
{

  SDL_String16 *pStr;
  char cBuf[80];
  struct GUI *pBuf, *pWindow = pCityDlg->pEndCityWidgetList;
  struct unit *pUnit;
  struct unit_type *pUType;
  Uint16 ww, i, hh;

  /* Disable city dlg */
  disable_city_dlg_widgets();

  ww = 0;
  hh = 0;
  i = 0;

  pUnit = player_find_unit_by_id(game.player_ptr, MAX_ID - pButton->ID);
  pUType = get_unit_type(pUnit->type);

  /* ----- */
  my_snprintf(cBuf, sizeof(cBuf), "%s :", _("Unit Commands"));

  pStr = create_str16_from_char(cBuf, 12);
  pStr->style |= TTF_STYLE_BOLD;

  pWindow = create_window(pWindow->dst, pStr, 1, 1, 0);

  pWindow->size.x = pButton->size.x;
  pWindow->size.y = pButton->size.y;
  ww = MAX(ww, pWindow->size.w);
  
  pWindow->action = units_orders_dlg_callback;
  set_wstate(pWindow, WS_NORMAL);

  add_to_gui_list(ID_REVOLUTION_DLG_WINDOW, pWindow);
  pCityDlg->pEndCityMenuWidgetList = pWindow;
  /* ----- */

  my_snprintf(cBuf, sizeof(cBuf), "%s", unit_description(pUnit));

  pStr = create_str16_from_char(cBuf, 12);
  pStr->style |= TTF_STYLE_BOLD;

  pBuf = create_iconlabel(GET_SURF(get_unit_type(pUnit->type)->sprite),
			  pWindow->dst, pStr, 0);

  ww = MAX(ww, pBuf->size.w);

  add_to_gui_list(ID_LABEL, pBuf);
  /* ----- */
  /* Activate unit */
  pBuf =
      create_icon_button_from_chars(NULL, pWindow->dst,
					      _("Activate unit"), 12, 0);

  i++;
  ww = MAX(ww, pBuf->size.w);
  hh = MAX(hh, pBuf->size.h);
  
  clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);

  pBuf->action = activate_units_orders_city_dlg_callback;
  set_wstate(pBuf, WS_NORMAL);

  add_to_gui_list(pButton->ID, pBuf);

  /* ----- */
  /* Activate unit, close dlg. */
  pBuf = create_icon_button_from_chars(NULL, pWindow->dst,
		  _("Activate unit, close dialog"),  12, 0);

  i++;
  ww = MAX(ww, pBuf->size.w);
  hh = MAX(hh, pBuf->size.h);
  
  clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
  pBuf->action = activate_and_exit_units_orders_city_dlg_callback;
  set_wstate(pBuf, WS_NORMAL);

  add_to_gui_list(pButton->ID, pBuf);
  /* ----- */
  if (pCityDlg->state == ARMY_PAGE) {
    /* Sentry unit */
    pBuf = create_icon_button_from_chars(NULL, pWindow->dst, 
    					_("Sentry unit"), 12, 0);

    i++;
    ww = MAX(ww, pBuf->size.w);
    hh = MAX(hh, pBuf->size.h);
    
    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);

    pBuf->action = sentry_units_orders_city_dlg_callback;

    if (pUnit->activity != ACTIVITY_SENTRY
	&& can_unit_do_activity(pUnit, ACTIVITY_SENTRY)) {
      set_wstate(pBuf, WS_NORMAL);
    }

    add_to_gui_list(pButton->ID, pBuf);
    /* ----- */
    /* Fortify unit */
    pBuf = create_icon_button_from_chars(NULL, pWindow->dst,
					    _("Fortify unit"), 12, 0);

    i++;
    ww = MAX(ww, pBuf->size.w);
    hh = MAX(hh, pBuf->size.h);
    
    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);

    pBuf->action = fortify_units_orders_city_dlg_callback;

    if (pUnit->activity != ACTIVITY_FORTIFYING
	&& can_unit_do_activity(pUnit, ACTIVITY_FORTIFYING)) {
      set_wstate(pBuf, WS_NORMAL);
    }

    add_to_gui_list(pButton->ID, pBuf);
  }

  /* ----- */
  /* Disband unit */
  pBuf =
      create_icon_button_from_chars(NULL, pWindow->dst,
				  _("Disband unit"), 12, 0);

  i++;
  ww = MAX(ww, pBuf->size.w);
  hh = MAX(hh, pBuf->size.h);
  
  clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);

  pBuf->action = disband_units_orders_city_dlg_callback;
  set_wstate(pBuf, WS_NORMAL);

  add_to_gui_list(pButton->ID, pBuf);
  /* ----- */

  if (pCityDlg->state == ARMY_PAGE) {
    /* Make new Homecity */
    pBuf = create_icon_button_from_chars(NULL, pWindow->dst, 
    					_("Make new homecity"), 12, 0);

    i++;
    ww = MAX(ww, pBuf->size.w);
    hh = MAX(hh, pBuf->size.h);
    
    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);

    pBuf->action = homecity_units_orders_city_dlg_callback;

    if (pUnit->homecity != pCityDlg->pCity->id) {
      set_wstate(pBuf, WS_NORMAL);
    }

    add_to_gui_list(pButton->ID, pBuf);
    /* ----- */
    /* Upgrade unit */
    pBuf = create_icon_button_from_chars(NULL, pWindow->dst,
					    _("Upgrade unit"), 12, 0);
    i++;
    ww = MAX(ww, pBuf->size.w);
    hh = MAX(hh, pBuf->size.h);
    
    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);

    pBuf->action = upgrade_units_orders_city_dlg_callback;

    if (can_upgrade_unittype(game.player_ptr, pUnit->type) != -1) {
      set_wstate(pBuf, WS_NORMAL);
    }

    add_to_gui_list(pButton->ID, pBuf);
  }

  /* ----- */
  /* Cancel */
  pBuf = create_icon_button_from_chars(NULL, pWindow->dst,
  						_("Cancel"), 12, 0);
  i++;
  ww = MAX(ww, pBuf->size.w);
  hh = MAX(hh, pBuf->size.h);

  pBuf->key = SDLK_ESCAPE;
  pBuf->action = cancel_units_orders_city_dlg_callback;
  set_wstate(pBuf, WS_NORMAL);
  clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);

  add_to_gui_list(pButton->ID, pBuf);

  pCityDlg->pBeginCityMenuWidgetList = pBuf;

  /* ================================================== */
  unsellect_widget_action();
  /* ================================================== */

  ww += DOUBLE_FRAME_WH + 10;
  hh += 4;
  pWindow->size.x += FRAME_WH;
  pWindow->size.y += WINDOW_TILE_HIGH + 2;

  /* create window background */

  resize_window(pWindow, NULL,
		get_game_colorRGB(COLOR_STD_BACKGROUND_BROWN), ww,
		WINDOW_TILE_HIGH + 2 + FRAME_WH + (i * hh) +
		pWindow->prev->size.h + 5);

  pBuf = pWindow->prev;

  /* label */
  pBuf->size.w = ww - DOUBLE_FRAME_WH;
  pBuf->size.x = pWindow->size.x + FRAME_WH;
  pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 2;
  pBuf = pBuf->prev;

  /* first button */
  pBuf->size.w = ww - DOUBLE_FRAME_WH;
  pBuf->size.h = hh;
  pBuf->size.x = pWindow->size.x + FRAME_WH;
  pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h + 5;
  pBuf = pBuf->prev;

  while (pBuf) {
    pBuf->size.w = ww - DOUBLE_FRAME_WH;
    pBuf->size.h = hh;
    pBuf->size.x = pBuf->next->size.x;
    pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
    if (pBuf == pCityDlg->pBeginCityMenuWidgetList) {
      break;
    }
    pBuf = pBuf->prev;
  }

  /* ================================================== */
  /* redraw */
  redraw_group(pCityDlg->pBeginCityMenuWidgetList, pWindow, 0);
  
  flush_rect(pWindow->size);
  return -1;
}

/* ======================================================================= */
/* ======================= City Dlg. Panels ============================== */
/* ======================================================================= */

/**************************************************************************
  up scrollbar unit list button callback
**************************************************************************/
static int up_army_city_dlg_callback(struct GUI *pButton)
{
  struct GUI *pBegin = up_scroll_widget_list(NULL,
					     pCityDlg->pPanelVscroll,
					     pCityDlg->
					     pBeginActiveCityPanelWidgetList,
					     pCityDlg->
					     pBeginCityPanelWidgetList->
					     next->next,
					     pCityDlg->
					     pEndCityPanelWidgetList);

  if (pBegin) {
    pCityDlg->pBeginActiveCityPanelWidgetList = pBegin;
  }

  unsellect_widget_action();
  pSellected_Widget = pButton;
  set_wstate(pButton, WS_SELLECTED);
  redraw_tibutton(pButton);
  flush_rect(pButton->size);

  return -1;
}

/**************************************************************************
  down scrollbar unit list button callback
**************************************************************************/
static int down_army_city_dlg_callback(struct GUI *pButton)
{
  struct GUI *pBegin = down_scroll_widget_list(NULL,
					       pCityDlg->pPanelVscroll,
					       pCityDlg->
					       pBeginActiveCityPanelWidgetList,
					       pCityDlg->
					       pBeginCityPanelWidgetList->
					       next->next,
					       pCityDlg->
					       pEndCityPanelWidgetList);

  if (pBegin) {
    pCityDlg->pBeginActiveCityPanelWidgetList = pBegin;
  }

  unsellect_widget_action();
  pSellected_Widget = pButton;
  set_wstate(pButton, WS_SELLECTED);
  redraw_tibutton(pButton);
  flush_rect(pButton->size);

  return -1;
}

/**************************************************************************
  create unit icon with support icons.
**************************************************************************/
static SDL_Surface *create_unit_surface(struct unit *pUnit, bool support)
{
  int i, step;
  SDL_Rect dest;
  SDL_Surface *pSurf = create_surf(NORMAL_TILE_WIDTH,
				   3 * NORMAL_TILE_HEIGHT / 2,
				   SDL_SWSURFACE);

  put_unit_pixmap_draw(pUnit, pSurf, 0, 3);

  if (support) {
    i = pUnit->upkeep + pUnit->upkeep_food +
	pUnit->upkeep_gold + pUnit->unhappiness;

    if (i * pIcons->pFood->w > NORMAL_TILE_WIDTH / 2) {
      step = (NORMAL_TILE_WIDTH / 2 - pIcons->pFood->w) / (i - 1);
    } else {
      step = pIcons->pFood->w;
    }

    dest.y = 3 * NORMAL_TILE_HEIGHT / 2 - pIcons->pFood->h - 2;
    dest.x = NORMAL_TILE_WIDTH / 8;

    for (i = 0; i < pUnit->upkeep; i++) {
      SDL_BlitSurface(pIcons->pShield, NULL, pSurf, &dest);
      dest.x += step;
    }

    for (i = 0; i < pUnit->upkeep_food; i++) {
      SDL_BlitSurface(pIcons->pFood, NULL, pSurf, &dest);
      dest.x += step;
    }

    for (i = 0; i < pUnit->upkeep_gold; i++) {
      SDL_BlitSurface(pIcons->pCoin, NULL, pSurf, &dest);
      dest.x += step;
    }

    for (i = 0; i < pUnit->unhappiness; i++) {
      SDL_BlitSurface(pIcons->pFace, NULL, pSurf, &dest);
      dest.x += step;
    }

  }

  SDL_SetColorKey(pSurf, SDL_SRCCOLORKEY | SDL_RLEACCEL, 
  				get_first_pixel(pSurf));

  return pSurf;
}

/**************************************************************************
  create present/supported units widget list
  207 pixels is panel width in city dlg.
  220 - max y position pixel position belong to panel area.
**************************************************************************/
static void create_present_supported_units_widget_list(struct unit_list *pList)
{
  int i;
  struct GUI *pBuf = NULL; /* FIXME: possibly uninitialized */
  struct GUI *pEnd = NULL;
  struct GUI *pWindow = pCityDlg->pEndCityWidgetList;
  struct genlist_iterator myiter;
  struct unit *pUnit;
  struct city *pHome_City;
  struct unit_type *pUType;
  SDL_Surface *pSurf;
  SDL_String16 *pStr;
  char cBuf[80];

  genlist_iterator_init(&myiter, &(pList->list), 0);

  i = 0;

  while (ITERATOR_PTR(myiter)) {
    pUnit = (struct unit *) ITERATOR_PTR(myiter);
    pUType = get_unit_type(pUnit->type);
    pHome_City = find_city_by_id(pUnit->homecity);
    
    if (pCityDlg->state == SUPPORTED_UNITS_PAGE) {
      pSurf = create_unit_surface(pUnit, 1);
    } else {
      pSurf = create_unit_surface(pUnit, 0);
    }

    my_snprintf(cBuf, sizeof(cBuf), "%s\n(%d,%d,%d)\n(%d/%d) %s",
		pUType->name, pUType->attack_strength,
		pUType->defense_strength, pUType->move_rate / 3,
		pUnit->hp, pUType->hp,
		pHome_City ? pHome_City->name : _("None"));

    pStr = create_str16_from_char(cBuf, 10);
    pStr->style |= TTF_STYLE_BOLD;
    pStr->backcol.unused = 128;/* 50% transparent */

    pBuf = create_iconlabel(pSurf, pWindow->dst, pStr,
			    (WF_FREE_THEME | WF_DRAW_THEME_TRANSPARENT));

    add_to_gui_list(MAX_ID - pUnit->id, pBuf);

    if (!pEnd) {
      pEnd = pBuf;
      pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 20 + 18;
    } else {
      pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
    }

    pBuf->size.x = pWindow->size.x + 7;

    if (++i > NUM_UNITS_SHOWN) {
      set_wflag(pBuf, WF_HIDDEN);
    }

    pBuf->size.w = 205;

    set_wstate(pBuf, WS_NORMAL);
    pBuf->action = units_orders_city_dlg_callback;

    ITERATOR_NEXT(myiter);
  }

  pCityDlg->pBeginActiveCityPanelWidgetList = pEnd;

  pCityDlg->pEndCityPanelWidgetList = pEnd;

  pCityDlg->pBeginCityPanelWidgetList = pBuf;


  if (i > NUM_UNITS_SHOWN) {
    int tmp, high;
    /* create up button */
    pBuf = create_themeicon_button(pTheme->UP_Icon, pWindow->dst, NULL, 0);

    pBuf->size.x = pWindow->size.x + 6;
    pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 19;

    pBuf->size.w = 207;

    tmp = pBuf->size.h;

    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);

    pBuf->action = up_army_city_dlg_callback;
    set_wstate(pBuf, WS_NORMAL);

    add_to_gui_list(ID_CHANGE_PROD_DLG_UP_BUTTON, pBuf);

    /* create down button */
    pBuf = create_themeicon_button(pTheme->DOWN_Icon, pWindow->dst, NULL, 0);

    pBuf->size.x = pWindow->size.x + 6;
    pBuf->size.y = pWindow->size.y + 220 - pBuf->size.h;

    high = pBuf->size.y;

    pBuf->size.w = 207;

    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);

    pBuf->action = down_army_city_dlg_callback;
    set_wstate(pBuf, WS_NORMAL);

    add_to_gui_list(ID_CHANGE_PROD_DLG_DOWN_BUTTON, pBuf);


    pCityDlg->pPanelVscroll = MALLOC(sizeof(struct ScrollBar));
    pCityDlg->pPanelVscroll->active = NUM_UNITS_SHOWN;
    pCityDlg->pPanelVscroll->count = i;
    pCityDlg->pPanelVscroll->min = pBuf->size.y;
    pCityDlg->pPanelVscroll->max = high;

    pCityDlg->pBeginCityPanelWidgetList = pBuf;
  }
}

/**************************************************************************
  change to present units panel.
**************************************************************************/
static int army_city_dlg_callback(struct GUI *pButton)
{
  
  if (pCityDlg->state != ARMY_PAGE) {

    if (pCityDlg->pEndCityPanelWidgetList) {	/* SUPPORTED_UNITS_PAGE */
      del_group_of_widgets_from_gui_list(pCityDlg->
					 pBeginCityPanelWidgetList,
					 pCityDlg->
					 pEndCityPanelWidgetList);
      pCityDlg->pEndCityPanelWidgetList = NULL;
      FREE(pCityDlg->pPanelVscroll);
    }

    pCityDlg->state = ARMY_PAGE;

    redraw_city_dialog(pCityDlg->pCity);
    flush_dirty();
  } else {
    redraw_icon(pButton);
    flush_rect(pButton->size);
  }

  return -1;
}

/**************************************************************************
  change to supported units panel.
**************************************************************************/
static int supported_unit_city_dlg_callback(struct GUI *pButton)
{
  if (pCityDlg->state != SUPPORTED_UNITS_PAGE) {

    if (pCityDlg->pEndCityPanelWidgetList) {	/* ARMY_PAGE */
      del_group_of_widgets_from_gui_list(pCityDlg->
					 pBeginCityPanelWidgetList,
					 pCityDlg->
					 pEndCityPanelWidgetList);
      pCityDlg->pEndCityPanelWidgetList = NULL;
      FREE(pCityDlg->pPanelVscroll);
    }

    pCityDlg->state = SUPPORTED_UNITS_PAGE;

    redraw_city_dialog(pCityDlg->pCity);
    flush_dirty();
  } else {
    redraw_icon(pButton);
    flush_rect(pButton->size);
  }

  return -1;
}

/* ---------------------- */

/**************************************************************************
  change to info panel.
**************************************************************************/
static int info_city_dlg_callback(struct GUI *pButton)
{
  if (pCityDlg->state != INFO_PAGE) {
    if (pCityDlg->pEndCityPanelWidgetList) {
      /* ARMY_PAGE || SUPPORTED_UNITS_PAGE */
      del_group_of_widgets_from_gui_list(pCityDlg->
					 pBeginCityPanelWidgetList,
					 pCityDlg->
					 pEndCityPanelWidgetList);
      pCityDlg->pEndCityPanelWidgetList = NULL;
      FREE(pCityDlg->pPanelVscroll);
    }

    pCityDlg->state = INFO_PAGE;
    redraw_city_dialog(pCityDlg->pCity);
    flush_dirty();
  } else {
    redraw_icon(pButton);
    flush_rect(pButton->size);
  }

  return -1;
}

/* ---------------------- */
/**************************************************************************
  change to happines panel.
**************************************************************************/
static int happy_city_dlg_callback(struct GUI *pButton)
{
  if (pCityDlg->state != HAPPINESS_PAGE) {
    if (pCityDlg->pEndCityPanelWidgetList) {
      /* ARMY_PAGE || SUPPORTED_UNITS_PAGE */
      del_group_of_widgets_from_gui_list(pCityDlg->
					 pBeginCityPanelWidgetList,
					 pCityDlg->
					 pEndCityPanelWidgetList);
      pCityDlg->pEndCityPanelWidgetList = NULL;
      FREE(pCityDlg->pPanelVscroll);
    }

    pCityDlg->state = HAPPINESS_PAGE;
    redraw_city_dialog(pCityDlg->pCity);
    flush_dirty();
  } else {
    redraw_icon(pButton);
    flush_rect(pButton->size);
  }

  return -1;
}

/**************************************************************************
  city option callback
**************************************************************************/
static int misc_panel_city_dlg_callback(struct GUI *pWidget)
{
  struct packet_generic_values packet;
  int new = pCityDlg->pCity->city_options & 0xff;

  switch (MAX_ID - pWidget->ID) {
  case 0x01:
    new ^= 0x01;
    break;
  case 0x02:
    new ^= 0x02;
    break;
  case 0x04:
    new ^= 0x04;
    break;
  case 0x08:
    new ^= 0x08;
    break;
  case 0x10:
    new ^= 0x10;
    break;
  case 0x20:
    new ^= 0x20;
    new ^= 0x40;
    pWidget->gfx = GET_SURF(get_citizen_sprite(CITIZEN_TAXMAN, 0,
					       pCityDlg->pCity));
    pWidget->ID = MAX_ID - 0x40;
    redraw_ibutton(pWidget);
    flush_rect(pWidget->size);
    break;
  case 0x40:
    new &= 0x1f;
    pWidget->gfx = GET_SURF(get_citizen_sprite(CITIZEN_ELVIS, 0,
					       pCityDlg->pCity));
    pWidget->ID = MAX_ID - 0x60;
    redraw_ibutton(pWidget);
    flush_rect(pWidget->size);
    break;
  case 0x60:
    new |= 0x20;
    pWidget->gfx = GET_SURF(get_citizen_sprite(CITIZEN_SCIENTIST, 0,
					       pCityDlg->pCity));
    pWidget->ID = MAX_ID - 0x20;
    redraw_ibutton(pWidget);
    flush_rect(pWidget->size);
    break;
  }

  packet.value1 = pCityDlg->pCity->id;
  packet.value2 = new;
  send_packet_generic_values(&aconnection, PACKET_CITY_OPTIONS, &packet);

  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static void create_city_options_widget_list(struct city *pCity)
{
  struct GUI *pBuf, *pWindow = pCityDlg->pEndCityWidgetList;
  SDL_Surface *pSurf;
  SDL_String16 *pStr;
  char cBuf[80];

  my_snprintf(cBuf, sizeof(cBuf), _("Auto-attack\nvs land units"));

  pStr = create_str16_from_char(cBuf, 10);
  pStr->forecol = *get_game_colorRGB(COLOR_STD_WHITE);
  pStr->style |= TTF_STYLE_BOLD;


  pBuf =
      create_textcheckbox(pWindow->dst, pCity->city_options & 0x01, pStr,
			  WF_DRAW_THEME_TRANSPARENT);

  pBuf->size.x = pWindow->size.x + 10;
  pBuf->size.y = pWindow->size.y + 40;
  set_wstate(pBuf, WS_NORMAL);
  pBuf->action = misc_panel_city_dlg_callback;

  add_to_gui_list(MAX_ID - 1, pBuf);

  pCityDlg->pBeginActiveCityPanelWidgetList = pBuf;

  pCityDlg->pEndCityPanelWidgetList = pBuf;

  /* ---- */
  my_snprintf(cBuf, sizeof(cBuf), _("Auto-attack\nvs sea units"));

  pStr = create_str16_from_char(cBuf, 10);
  pStr->style |= TTF_STYLE_BOLD;
  pStr->forecol = *get_game_colorRGB(COLOR_STD_WHITE);

  pBuf =
      create_textcheckbox(pWindow->dst, pCity->city_options & 0x02, pStr,
			  WF_DRAW_THEME_TRANSPARENT);

  set_wstate(pBuf, WS_NORMAL);
  pBuf->action = misc_panel_city_dlg_callback;

  add_to_gui_list(MAX_ID - 2, pBuf);

  pBuf->size.x = pBuf->next->size.x;
  pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;

  /* ----- */
  my_snprintf(cBuf, sizeof(cBuf), _("Auto-attack\nvs air units"));

  pStr = create_str16_from_char(cBuf, 10);
  pStr->style |= TTF_STYLE_BOLD;
  pStr->forecol = *get_game_colorRGB(COLOR_STD_WHITE);

  pBuf =
      create_textcheckbox(pWindow->dst, pCity->city_options & 0x04, pStr,
			  WF_DRAW_THEME_TRANSPARENT);

  set_wstate(pBuf, WS_NORMAL);
  pBuf->action = misc_panel_city_dlg_callback;

  add_to_gui_list(MAX_ID - 4, pBuf);

  pBuf->size.x = pBuf->next->size.x;
  pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;

  /* ----- */
  my_snprintf(cBuf, sizeof(cBuf), _("Auto-attack\nvs air units"));

  pStr = create_str16_from_char(cBuf, 10);
  pStr->style |= TTF_STYLE_BOLD;
  pStr->forecol = *get_game_colorRGB(COLOR_STD_WHITE);

  pBuf =
      create_textcheckbox(pWindow->dst, pCity->city_options & 0x08, pStr,
			  WF_DRAW_THEME_TRANSPARENT);

  set_wstate(pBuf, WS_NORMAL);
  pBuf->action = misc_panel_city_dlg_callback;

  add_to_gui_list(MAX_ID - 8, pBuf);

  pBuf->size.x = pBuf->next->size.x;
  pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;

  /* ----- */
  my_snprintf(cBuf, sizeof(cBuf),
	      _("Disband if build\nsettler at size 1"));

  pStr = create_str16_from_char(cBuf, 10);
  pStr->style |= TTF_STYLE_BOLD;
  pStr->forecol = *get_game_colorRGB(COLOR_STD_WHITE);

  pBuf =
      create_textcheckbox(pWindow->dst, pCity->city_options & 0x10, pStr,
			  WF_DRAW_THEME_TRANSPARENT);

  set_wstate(pBuf, WS_NORMAL);
  pBuf->action = misc_panel_city_dlg_callback;

  add_to_gui_list(MAX_ID - 0x10, pBuf);

  pBuf->size.x = pBuf->next->size.x;
  pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
  /* ----- */
  my_snprintf(cBuf, sizeof(cBuf), "%s :", _("New citizens are"));

  pStr = create_str16_from_char(cBuf, 10);
  pStr->style |= SF_CENTER;
  change_ptsize16(pStr, 13);

  if (pCity->city_options & 0x20) {
    pSurf = GET_SURF(get_citizen_sprite(CITIZEN_SCIENTIST, 0,
					pCityDlg->pCity));
    pBuf = create_icon_button(pSurf, pWindow->dst, pStr, WF_ICON_CENTER_RIGHT);
    add_to_gui_list(MAX_ID - 0x20, pBuf);
  } else {
    if (pCity->city_options & 0x40) {
      pSurf = GET_SURF(get_citizen_sprite(CITIZEN_TAXMAN, 0,
					  pCityDlg->pCity));
      pBuf = create_icon_button(pSurf, pWindow->dst,
				      pStr, WF_ICON_CENTER_RIGHT);
      add_to_gui_list(MAX_ID - 0x40, pBuf);
    } else {
      pSurf = GET_SURF(get_citizen_sprite(CITIZEN_ELVIS, 0,
					  pCityDlg->pCity));
      pBuf = create_icon_button(pSurf, pWindow->dst,
				pStr, WF_ICON_CENTER_RIGHT);
      add_to_gui_list(MAX_ID - 0x60, pBuf);
    }
  }

  pBuf->size.w = 199;

  pBuf->action = misc_panel_city_dlg_callback;
  set_wstate(pBuf, WS_NORMAL);
  clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);

  pBuf->size.x = pBuf->next->size.x;
  pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h + 5;

  pCityDlg->pBeginCityPanelWidgetList = pBuf;
}

/**************************************************************************
  change to city options panel.
**************************************************************************/
static int options_city_dlg_callback(struct GUI *pButton)
{

  if (pCityDlg->state != MISC_PAGE) {
    if (pCityDlg->pEndCityPanelWidgetList) {
      /* ARMY_PAGE || SUPPORTED_UNITS_PAGE */
      del_group_of_widgets_from_gui_list(pCityDlg->
					 pBeginCityPanelWidgetList,
					 pCityDlg->
					 pEndCityPanelWidgetList);
      pCityDlg->pEndCityPanelWidgetList = NULL;
      FREE(pCityDlg->pPanelVscroll);
    }

    pCityDlg->state = MISC_PAGE;

    redraw_city_dialog(pCityDlg->pCity);
    flush_dirty();
  } else {
    redraw_icon(pButton);
    flush_rect(pButton->size);
  }

  return -1;
}

/* ======================================================================= */

/**************************************************************************
  PORTME
**************************************************************************/
static int work_list_city_dlg_callback(struct GUI *pButton)
{
  return -1;
}

/**************************************************************************
  PORTME
**************************************************************************/
static int cma_city_dlg_callback(struct GUI *pButton)
{
  return -1;
}

/**************************************************************************
  Exit city dialog.
**************************************************************************/
static int exit_city_dlg_callback(struct GUI *pButton)
{
  popdown_city_dialog(pCityDlg->pCity);
  return -1;
}

/* ======================================================================= */
/* ======================== Buy Production Dlg. ========================== */
/* ======================================================================= */

/**************************************************************************
  popdown buy productions dlg.
**************************************************************************/
static int cancel_buy_prod_city_dlg_callback(struct GUI *pButton)
{
  popdown_hurry_production_dialog();
  
  if (pCityDlg)
  {
    /* enable city dlg */
    enable_city_dlg_widgets();
    unlock_buffer();
  }
  
  return -1;
}

/**************************************************************************
  buy productions.
**************************************************************************/
static int ok_buy_prod_city_dlg_callback(struct GUI *pButton)
{
  struct packet_city_request packet;

  packet.city_id = ((struct city *)pButton->data)->id;
  
  if (pCityDlg)
  {
    del_group_of_widgets_from_gui_list(pHurry_Prod_Dlg->pBeginWidgetList,
			      		pHurry_Prod_Dlg->pEndWidgetList);

    FREE( pHurry_Prod_Dlg );

    /* enable city dlg */
    enable_city_dlg_widgets();
    unlock_buffer();
    set_wstate(pCityDlg->pBuy_Button, WS_DISABLED);
  }
  else
  {
    popdown_hurry_production_dialog();
  }
    
  send_packet_city_request(&aconnection, &packet, PACKET_CITY_BUY);

  return -1;
}

/**************************************************************************
  popup buy productions dlg.
**************************************************************************/
static int buy_prod_city_dlg_callback(struct GUI *pButton)
{
  
  redraw_icon(pButton);
  flush_rect(pButton->size);

  disable_city_dlg_widgets();
  lock_buffer(pButton->dst);
  popup_hurry_production_dialog(pCityDlg->pCity, pButton->dst);
  
  return -1;
}

/**************************************************************************
  popup buy productions dlg.
**************************************************************************/
static void popdown_hurry_production_dialog(void)
{
  if (pHurry_Prod_Dlg) {
    popdown_window_group_dialog(pHurry_Prod_Dlg->pBeginWidgetList,
			      pHurry_Prod_Dlg->pEndWidgetList);
    FREE(pHurry_Prod_Dlg);
    flush_dirty();
  }
}

/**************************************************************************
  main hurry productions dlg. callback
**************************************************************************/
static int hurry_production_window_callback(struct GUI *pWindow)
{
  return -1;
}

/**************************************************************************
  popup buy productions dlg.
**************************************************************************/
void popup_hurry_production_dialog(struct city *pCity, SDL_Surface *pDest)
{

  int value, hh, ww = 0;
  const char *name;
  char cBuf[512];
  struct GUI *pBuf = NULL, *pWindow;
  SDL_String16 *pStr;

  if (pHurry_Prod_Dlg) {
    return;
  }
  
  pHurry_Prod_Dlg = MALLOC( sizeof(struct SMALL_DLG));
  
  if (pCity->is_building_unit) {
    name = get_unit_type(pCity->currently_building)->name;
  } else {
    name = get_impr_name_ex(pCity, pCity->currently_building);
  }

  value = city_buy_cost(pCity);

  if (game.player_ptr->economic.gold >= value) {
    my_snprintf(cBuf, sizeof(cBuf),
		_("Buy %s for %d gold?\n"
		  "Treasury contains %d gold."),
		name, value, game.player_ptr->economic.gold);


  } else {
    my_snprintf(cBuf, sizeof(cBuf),
		_("%s costs %d gold.\n"
		  "Treasury contains %d gold."),
		name, value, game.player_ptr->economic.gold);
  }

  hh = WINDOW_TILE_HIGH + 2;
  pStr = create_str16_from_char(_("Buy It?"), 12);
  pStr->style |= TTF_STYLE_BOLD;

  pWindow = create_window(pDest, pStr, 100, 100, 0);

  pWindow->action = hurry_production_window_callback;
  set_wstate(pWindow, WS_NORMAL);

  pHurry_Prod_Dlg->pEndWidgetList = pWindow;

  add_to_gui_list(ID_CHANGE_PROD_DLG_WINDOW, pWindow);

  /* ============================================================= */

  /* create text label */
  pStr = create_str16_from_char(cBuf, 10);
  pStr->style |= TTF_STYLE_BOLD;
  pStr->forecol.r = 255;
  pStr->forecol.g = 255;
  /*pStr->forecol.b = 255; */
  pBuf = create_iconlabel(NULL, pWindow->dst, pStr, 0);

  hh += pBuf->size.h;
  ww = MAX(ww , pBuf->size.w);
  
  add_to_gui_list(ID_REVOLUTION_DLG_LABEL, pBuf);

  if (game.player_ptr->economic.gold >= value) {
    pBuf = create_themeicon_button_from_chars(pTheme->OK_Icon, pWindow->dst,
					      _("Yes"), 12, 0);

    hh += pBuf->size.h;
    ww = MAX(ww, pBuf->size.w);

    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
    pBuf->action = ok_buy_prod_city_dlg_callback;
    set_wstate(pBuf, WS_NORMAL);
    pBuf->data = (void *)pCity;
    
    add_to_gui_list(ID_REVOLUTION_DLG_OK_BUTTON, pBuf);

    pBuf = create_themeicon_button_from_chars(pTheme->CANCEL_Icon,
			    pWindow->dst, _("No"), 12, 0);

    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
    pBuf->action = cancel_buy_prod_city_dlg_callback;
    set_wstate(pBuf, WS_NORMAL);

    hh += pBuf->size.h;
    ww = MAX(ww, pBuf->size.w);

    add_to_gui_list(ID_REVOLUTION_DLG_CANCEL_BUTTON, pBuf);
  } else {
    pBuf = create_themeicon_button_from_chars(pTheme->CANCEL_Icon,
					pWindow->dst, _("Darn"), 12, 0);

    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
    pBuf->action = cancel_buy_prod_city_dlg_callback;
    set_wstate(pBuf, WS_NORMAL);

    hh += pBuf->size.h;
    ww = MAX(ww , pBuf->size.w);

    add_to_gui_list(ID_REVOLUTION_DLG_CANCEL_BUTTON, pBuf);
  }

  pHurry_Prod_Dlg->pBeginWidgetList = pBuf;
  
  /* setup window size and start position */
  ww += 10;

  pBuf = pWindow->prev;
  
  if (city_dialog_is_open(pCity))
  {
    pWindow->size.x = pCityDlg->pBuy_Button->size.x;
    pWindow->size.y = pCityDlg->pBuy_Button->size.y - (hh + FRAME_WH + 5);
  }
  else
  {
    int canvas_x , canvas_y;
    get_canvas_xy(pCity->x , pCity->y , &canvas_x , &canvas_y);
    if (canvas_x + NORMAL_TILE_WIDTH + ww > pWindow->dst->w)
    {
      if (canvas_x - ww >= 0)
      {
        pWindow->size.x = canvas_x - ww;
      }
      else
      {
        pWindow->size.x = (pWindow->dst->w - ww) / 2;
      }
    }
    else
    {
      pWindow->size.x = canvas_x + NORMAL_TILE_WIDTH;
    }
    
    if (canvas_y - NORMAL_TILE_HEIGHT + hh > pWindow->dst->h)
    {
      if ( canvas_y + NORMAL_TILE_HEIGHT - hh >= 0 )
      {
        pWindow->size.y = canvas_y + NORMAL_TILE_HEIGHT - hh;
      }
      else
      {
        pWindow->size.y = (pWindow->dst->h - hh) / 2;
      }
    }
    else
    {
      pWindow->size.y = canvas_y - NORMAL_TILE_HEIGHT;
    }
  }

  resize_window(pWindow, NULL,
		get_game_colorRGB(COLOR_STD_BACKGROUND_BROWN),
		ww + DOUBLE_FRAME_WH, hh + FRAME_WH + 5);

  /* setup rest of widgets */
  /* label */
  pBuf->size.x = pWindow->size.x + FRAME_WH;
  pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 2;
  pBuf->size.w = ww;

  pBuf = pBuf->prev;
  pBuf->size.x = pWindow->size.x + FRAME_WH;
  pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h + 5;
  pBuf->size.w = ww;

  if (pBuf != pHurry_Prod_Dlg->pBeginWidgetList) {
    pBuf = pBuf->prev;
    pBuf->size.x = pWindow->size.x + FRAME_WH;
    pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
    pBuf->size.w = ww;
  }
  /* ================================================== */
  /* redraw */
  redraw_group(pHurry_Prod_Dlg->pBeginWidgetList, pWindow, 0);

  flush_rect(pWindow->size);
}

/* =======================================================================*/
/* ========================== CHANGE PRODUCTION ==========================*/
/* =======================================================================*/

/**************************************************************************
 Change production dialog: Scroll up prod. list.
**************************************************************************/
static int up_change_prod_dlg_callback(struct GUI *pButton)
{
  up_advanced_dlg(pChange_Prod_Dlg, pButton->prev->prev);
  
  unsellect_widget_action();
  pSellected_Widget = pButton;
  set_wstate(pButton, WS_SELLECTED);
  redraw_tibutton(pButton);
  flush_rect(pButton->size);
  return -1;
}

/**************************************************************************
  Change production dialog: scroll down production list
**************************************************************************/
static int down_change_prod_dlg_callback(struct GUI *pButton)
{
  down_advanced_dlg(pChange_Prod_Dlg, pButton->prev);
  
  unsellect_widget_action();
  pSellected_Widget = pButton;
  set_wstate(pButton, WS_SELLECTED);
  redraw_tibutton(pButton);
  flush_rect(pButton->size);
  return -1;
}

/**************************************************************************
  Change production dialog: vertical scrollbar callback
**************************************************************************/
static int vertic_change_prod_dlg_callback(struct GUI *pButton)
{
  vscroll_advanced_dlg(pChange_Prod_Dlg, pButton);
  
  unsellect_widget_action();
  pSellected_Widget = pButton;
  set_wstate(pButton, WS_SELLECTED);
  redraw_vert(pButton);
  flush_rect(pButton->size);
  return -1;
}

/**************************************************************************
  Change production dialog: cancel the dialog.
**************************************************************************/
static int exit_change_prod_dlg_callback(struct GUI *pButton)
{
  
  popdown_change_production_dialog();
  
  if (pCityDlg)
  {
    /* enable city dlg */
    enable_city_dlg_widgets();
    unlock_buffer();
  }
  
  return -1;
}

/**************************************************************************
  Change production dialog: accept changes.
**************************************************************************/
static int change_prod_callback(struct GUI *pLabel)
{

  struct packet_city_request packet;
  struct city *pCity = (struct city *)pLabel->data;
  
  if (pLabel->ID > (MAX_ID - 1000)) {
    packet.city_id = pCity->id;
    packet.build_id = (Unit_Type_id) (MAX_ID - pLabel->ID);
    packet.is_build_id_unit_id = 1;
  } else {
    packet.city_id = pCity->id;
    packet.build_id = (Impr_Type_id) (MAX_ID - 1000 - pLabel->ID);
    packet.is_build_id_unit_id = 0;
  }

  if (city_dialog_is_open(pCity))
  {
    SDL_Client_Flags |= CF_CHANGED_PROD;

    del_group_of_widgets_from_gui_list(pChange_Prod_Dlg->pBeginWidgetList,
				     pChange_Prod_Dlg->pEndWidgetList);
  
    FREE(pChange_Prod_Dlg->pScroll);
    FREE(pChange_Prod_Dlg);
    
    /* enable city dlg. */
    enable_city_dlg_widgets();
    unlock_buffer();
  }
  else
  {
    popdown_change_production_dialog();
  }

  send_packet_city_request(&aconnection, &packet, PACKET_CITY_CHANGE);

  return -1;
}

/**************************************************************************
  Change production dialog: popdown dlg.
**************************************************************************/
static void popdown_change_production_dialog(void)
{
  if (pChange_Prod_Dlg) {
    popdown_window_group_dialog(pChange_Prod_Dlg->pBeginWidgetList,
			      pChange_Prod_Dlg->pEndWidgetList);
  
    FREE(pChange_Prod_Dlg->pScroll);
    FREE( pChange_Prod_Dlg );
    flush_dirty();
  }
}

/**************************************************************************
  Change production dialog: main dlg. callback.
**************************************************************************/
static int change_prod_dlg_window_callback(struct GUI *pWindow)
{
  return -1;
}

/**************************************************************************
  Popup the change production dialog.
**************************************************************************/
void popup_change_production_dialog(struct city *pCity, SDL_Surface *pDest)
{
  struct GUI *pWindow = NULL, *pBuf = NULL;
  SDL_Surface *pLogo = NULL, *pTemp_Surf = NULL;
  SDL_String16 *pStr = NULL;
  int turns, count , button_high = 0, button_width = 0;
  int width = 0, window_high = 0, high = 0;
  char cBuf[80];
  
  if (pChange_Prod_Dlg) {
    return;
  }
    
  pChange_Prod_Dlg = MALLOC(sizeof(struct ADVANCED_DLG));
  
  /* create window widget */
  pStr = create_str16_from_char(_("Change Production"), 12);
  pStr->style |= TTF_STYLE_BOLD;

  pWindow = create_window(pDest, pStr, 100, 100, 0);

  pWindow->action = change_prod_dlg_window_callback;
  set_wstate(pWindow, WS_NORMAL);
  
  add_to_gui_list(ID_CHANGE_PROD_DLG_WINDOW, pWindow);
  pChange_Prod_Dlg->pEndWidgetList = pWindow;
  /* ============================================================= */
  
  /* create exit button */
  pBuf = create_themeicon(ResizeSurface(pTheme->CANCEL_Icon ,
			  pTheme->CANCEL_Icon->w - 4,
			  pTheme->CANCEL_Icon->h - 4, 1) , pWindow->dst,
  			  (WF_WIDGET_HAS_INFO_LABEL|WF_FREE_THEME|
  						WF_DRAW_THEME_TRANSPARENT));
  SDL_SetColorKey(pBuf->theme ,
	  SDL_SRCCOLORKEY|SDL_RLEACCEL , get_first_pixel(pBuf->theme));
 
  pBuf->string16 = create_str16_from_char(_("Cancel"), 12);

  pBuf->action = exit_change_prod_dlg_callback;
  pBuf->key = SDLK_ESCAPE;
  set_wstate(pBuf, WS_NORMAL);

  add_to_gui_list(ID_CITY_DLG_EXIT_BUTTON, pBuf);
  
  /* create imprv. and units widgets */
  count = 0;
  pBuf = NULL;
  impr_type_iterate(imp) {
    if (can_build_improvement(pCity, imp)) {
      struct impr_type *pImpr = get_improvement_type(imp);
      char *state;

      if (imp == B_CAPITAL) {
	my_snprintf(cBuf, sizeof(cBuf), _("%s\n %d gold per turn"),
		    pImpr->name, MAX(0, pCity->shield_surplus));
      } else {
	turns = city_turns_to_build(pCity, imp, FALSE, TRUE);

	if (is_wonder(imp)) {
	  if (wonder_obsolete(imp)) {
	    state = _("Obsolete");
	  } else if (game.global_wonders[imp] != 0) {
	    state = _("Built");
	  } else {
	    state = _("Wonder");
	  }
	} else {
	  state = NULL;
	}

	if (turns == FC_INFINITY) {
	  my_snprintf(cBuf, sizeof(cBuf), _("%s%s\n%d/%d shields - %s"),
		      pImpr->name, state,
		      pCity->shield_stock, pImpr->build_cost, _("never"));
	} else if (state) {
	  my_snprintf(cBuf, sizeof(cBuf), _("%s (%s)\n%d/%d shields - %d %s"),
		      pImpr->name, state,
		      pCity->shield_stock, pImpr->build_cost,
		      turns, PL_("turn", "turns", turns));
	} else {
	  my_snprintf(cBuf, sizeof(cBuf), _("%s\n%d/%d shields - %d %s"),
		      pImpr->name,
		      pCity->shield_stock, pImpr->build_cost,
		      turns, PL_("turn", "turns", turns));
	}
      }

      pStr = create_str16_from_char(cBuf, 10);
      pStr->backcol.unused = 128;

#if 0
      pStr->forecol = get_game_color(0);
      pStr->style |= TTF_STYLE_BOLD;
#endif

      pBuf = create_iconlabel(GET_SURF(pImpr->sprite), pWindow->dst, pStr,
		WF_DRAW_THEME_TRANSPARENT|WF_DRAW_TEXT_LABEL_WITH_SPACE);

       pBuf->action = change_prod_callback;
      set_wstate(pBuf, WS_NORMAL);
      pBuf->data = (void *)pCity;
      add_to_gui_list(MAX_ID - 1000 - imp, pBuf);

      count++;

      if (count > 12) {
	set_wflag(pBuf, WF_HIDDEN);
      }

      width = MAX(width, pBuf->size.w);
      high = MAX(high, pBuf->size.h);
    }
  } impr_type_iterate_end;
  /* ============================================================= */

  unit_type_iterate(un) {
    if (can_build_unit(pCity, un)) {
      struct unit_type *pUnit = get_unit_type(un);

      turns = city_turns_to_build(pCity, un, TRUE, TRUE);

      if (turns == FC_INFINITY) {
	my_snprintf(cBuf, sizeof(cBuf),
		    _("%s (%d/%d/%d)\n%d/%d shields - never"),
		    pUnit->name, pUnit->attack_strength,
		    pUnit->defense_strength, pUnit->move_rate / 3,
		    pCity->shield_stock, pUnit->build_cost);
      } else {
	my_snprintf(cBuf, sizeof(cBuf),
		    _("%s (%d/%d/%d)\n%d/%d shields - %d %s"),
		    pUnit->name, pUnit->attack_strength,
		    pUnit->defense_strength, pUnit->move_rate / 3,
		    pCity->shield_stock, pUnit->build_cost, 
		    turns, PL_("turn", "turns", turns));
      }

      pStr = create_str16_from_char(cBuf, 10);
      pStr->backcol.unused = 128;
#if 0
      pStr->forecol = get_game_color(0);
      pStr->style |= TTF_STYLE_BOLD;
#endif
      
      pTemp_Surf = make_flag_surface_smaler(GET_SURF(pUnit->sprite));

      if ( pTemp_Surf->h > 26 )
      {
        pLogo = ZoomSurface(pTemp_Surf, 26.0 / pTemp_Surf->h,
		      26.0 / pTemp_Surf->h, 1);
        FREESURFACE(pTemp_Surf);
	
        SDL_SetColorKey(pLogo, SDL_SRCCOLORKEY|SDL_RLEACCEL,
						get_first_pixel(pLogo));
      
        pBuf = create_iconlabel(pLogo, pWindow->dst, pStr,
	  (WF_FREE_THEME|
      		WF_DRAW_THEME_TRANSPARENT|WF_DRAW_TEXT_LABEL_WITH_SPACE));
      }
      else
      {
	pBuf = create_iconlabel(pTemp_Surf, pWindow->dst, pStr,
	  (WF_DRAW_THEME_TRANSPARENT|WF_DRAW_TEXT_LABEL_WITH_SPACE));
      }

      pBuf->data = (void *)pCity;

      pBuf->action = change_prod_callback;
      set_wstate(pBuf, WS_NORMAL);

      add_to_gui_list(MAX_ID - un, pBuf);

      count++;

      if (count > 12) {
	set_wflag(pBuf, WF_HIDDEN);
      }

      width = MAX(width, pBuf->size.w);
      high = MAX(high, pBuf->size.h);
      
    }
  } unit_type_iterate_end;
  /* ============================================================= */
  
  pChange_Prod_Dlg->pBeginActiveWidgetList = pBuf;
  pChange_Prod_Dlg->pBeginWidgetList = pBuf;

  
  if (count > 12) {
    /* create up button */
    pBuf = create_themeicon_button(pTheme->UP_Icon, pWindow->dst, NULL, 0);
    
    button_high = pBuf->size.h;
    button_width = pBuf->size.w;
    
    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
    pBuf->action = up_change_prod_dlg_callback;
    set_wstate(pBuf, WS_NORMAL);

    add_to_gui_list(ID_CHANGE_PROD_DLG_UP_BUTTON, pBuf);


    /* create down button */
    pBuf = create_themeicon_button(pTheme->DOWN_Icon, pWindow->dst, NULL, 0);
    
    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
    pBuf->action = down_change_prod_dlg_callback;
    set_wstate(pBuf, WS_NORMAL);

    add_to_gui_list(ID_CHANGE_PROD_DLG_DOWN_BUTTON, pBuf);

    /* create vsrollbar */
    pBuf = create_vertical(pTheme->Vertic, pWindow->dst, 
    					50, WF_DRAW_THEME_TRANSPARENT);
    
    set_wstate(pBuf, WS_NORMAL);
    pBuf->action = vertic_change_prod_dlg_callback;

    add_to_gui_list(ID_CHANGE_PROD_DLG_VSCROLLBAR, pBuf);

    pChange_Prod_Dlg->pScroll = MALLOC(sizeof(struct ScrollBar));
    pChange_Prod_Dlg->pScroll->active = 12;
    pChange_Prod_Dlg->pScroll->count = count;
    
    pChange_Prod_Dlg->pBeginWidgetList = pBuf;
  }
  /* ============================================================= */
  
  /* setup window size and start position */

  if (count > 12) {
    window_high = (high * 12) + (3 + FRAME_WH +
					    WINDOW_TILE_HIGH + button_high * 2);
  } else {
    window_high = (high * count) + (7 + WINDOW_TILE_HIGH);
  }
  
  width += button_width + DOUBLE_FRAME_WH;
  
  if (pCityDlg)
  {
    pWindow->size.x = (pCityDlg->pEndCityWidgetList->size.x + 6);
    pWindow->size.y = (pCityDlg->pEndCityWidgetList->size.y +
		     pCityDlg->pEndCityWidgetList->size.h - 36 - window_high);
  }
  else
  {
    int canvas_x , canvas_y;
    get_canvas_xy(pCity->x , pCity->y , &canvas_x , &canvas_y);
    
    if (canvas_x + NORMAL_TILE_WIDTH + width > pWindow->dst->w)
    {
      if ( canvas_x - width >= 0 )
      {
        pWindow->size.x = canvas_x - width;
      }
      else
      {
        pWindow->size.x = (pWindow->dst->w - width) / 2;
      }
    }
    else
    {
      pWindow->size.x = canvas_x + NORMAL_TILE_WIDTH;
    }
    
    if (canvas_y - NORMAL_TILE_HEIGHT + window_high > pWindow->dst->h)
    {
      if ( canvas_y + NORMAL_TILE_HEIGHT - window_high >= 0 )
      {
        pWindow->size.y = canvas_y + NORMAL_TILE_HEIGHT - window_high;
      }
      else
      {
        pWindow->size.y = (pWindow->dst->h - window_high) / 2;
      }
    }
    else
    {
      pWindow->size.y = canvas_y - NORMAL_TILE_HEIGHT;
    }
    
  }

  /* create window background */

  resize_window(pWindow, NULL,
	get_game_colorRGB(COLOR_STD_BACKGROUND_BROWN), width, window_high);

  width -= (button_width + DOUBLE_FRAME_WH);

  /* setup widgets start position */
  
  pBuf = pWindow;
  pBuf = pBuf->prev;
  
  /* exit button */
  
  pBuf->size.x = pWindow->size.x + pWindow->size.w-pBuf->size.w-FRAME_WH-1;
  pBuf->size.y = pWindow->size.y;
  
  /* imprv. and units widgets */
  turns = count;
  pBuf = pBuf->prev;
  pBuf->size.x = pWindow->size.x + FRAME_WH;
  pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 2;
  
  if (count > 12) {
    pBuf->size.y += button_high;
  }
  
  pBuf->size.w = width;
  pBuf->size.h = high;
  pBuf->string16->backcol = *(get_game_colorRGB(COLOR_STD_BACKGROUND_BROWN));
  pBuf = convert_iconlabel_to_themeiconlabel2(pBuf);
  
  count--;
  pBuf = pBuf->prev;
  while (count) {
    pBuf->size.x = pWindow->size.x + FRAME_WH;
    pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
    pBuf->size.w = width;
    pBuf->size.h = high;
    pBuf->string16->backcol = *(get_game_colorRGB(COLOR_STD_BACKGROUND_BROWN));
    pBuf = convert_iconlabel_to_themeiconlabel2(pBuf);
    pBuf = pBuf->prev;
    count--;
  }
  
  if (turns > 12) {
    
    /* up button */
    pBuf = pChange_Prod_Dlg->pBeginActiveWidgetList->prev;

    pBuf->size.x = pWindow->size.x + FRAME_WH;
    pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 1;

    pBuf->size.w = width + pBuf->size.w;

    pChange_Prod_Dlg->pScroll->min = pBuf->size.y + pBuf->size.h;
  
    /* down button */
    pBuf = pBuf->prev;

    pBuf->size.x = pWindow->size.x + FRAME_WH;
    pBuf->size.y =
	pWindow->size.y + pWindow->size.h - pBuf->size.h - FRAME_WH;


    pBuf->size.w = width + pBuf->size.w ;
    
    pChange_Prod_Dlg->pScroll->max = pBuf->size.y;
    
    /* vsrollbar */
    pBuf = pBuf->prev;
    
    pBuf->size.x =
	pWindow->size.x + pWindow->size.w - FRAME_WH - pBuf->size.w + 2;
    pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + button_high + 1;

    /* 
       imprv. scrollbar size.
     */
    pBuf->size.h = scrollbar_size(pChange_Prod_Dlg->pScroll);
  }

  pChange_Prod_Dlg->pActiveWidgetList = pWindow->prev->prev;
  pChange_Prod_Dlg->pEndActiveWidgetList = pChange_Prod_Dlg->pActiveWidgetList;
  /* ------------------------------------------------- */
  /* redraw change prod. dlg */
  redraw_group(pChange_Prod_Dlg->pBeginWidgetList, pWindow, 0);

  flush_rect(pWindow->size);
}

/**************************************************************************
  Popup the change production dialog.
**************************************************************************/
static int change_prod_dlg_callback(struct GUI *pButton)
{
  
  redraw_icon(pButton);
  flush_rect(pButton->size);

  disable_city_dlg_widgets();
  lock_buffer(pButton->dst);
  
  popup_change_production_dialog(pCityDlg->pCity, pButton->dst);
  return -1;
}

/**************************************************************************
  Scrolling improvement list: scroll down.
**************************************************************************/
static int down_city_dlg_building_callback(struct GUI *pButton)
{
  struct GUI *pBegin;

  pBegin = down_scroll_widget_list(pButton->prev,
				   pCityDlg->pVscroll,
				   pCityDlg->
				   pBeginActiveCityImprWidgetList,
				   pCityDlg->
				   pBeginCityImprWidgetList->
				   next->next->next,
				   pCityDlg->pEndCityImprWidgetList);


  if (pBegin) {
    pCityDlg->pBeginActiveCityImprWidgetList = pBegin;
  }

  unsellect_widget_action();
  pSellected_Widget = pButton;
  set_wstate(pButton, WS_SELLECTED);
  redraw_tibutton(pButton);
  flush_rect(pButton->size);

  return -1;
}


/**************************************************************************
  Scroll up the building list.
**************************************************************************/
static int up_city_dlg_building_callback(struct GUI *pButton)
{

  struct GUI *pBegin = up_scroll_widget_list(pButton->prev->prev,
					     pCityDlg->pVscroll,
					     pCityDlg->
					     pBeginActiveCityImprWidgetList,
					     pCityDlg->
					     pBeginCityImprWidgetList->
					     next->next->next,
					     pCityDlg->
					     pEndCityImprWidgetList);


  if (pBegin) {
    pCityDlg->pBeginActiveCityImprWidgetList = pBegin;
  }

  unsellect_widget_action();
  pSellected_Widget = pButton;
  set_wstate(pButton, WS_SELLECTED);
  redraw_tibutton(pButton);
  flush_rect(pButton->size);

  return -1;
}

/**************************************************************************
  FIXME: fix main scroll function
**************************************************************************/
static int vscroll_city_dlg_building_callback(struct GUI *pVscrollBar)
{

  struct GUI *pBegin = vertic_scroll_widget_list(pVscrollBar,
						 pCityDlg->pVscroll,
						 pCityDlg->
						 pBeginActiveCityImprWidgetList,
						 pCityDlg->
						 pBeginCityImprWidgetList->
						 next->next->next,
						 pCityDlg->
						 pEndCityImprWidgetList);


  if (pBegin) {
    pCityDlg->pBeginActiveCityImprWidgetList = pBegin;
  }

  unsellect_widget_action();
  set_wstate(pVscrollBar, WS_SELLECTED);
  pSellected_Widget = pVscrollBar;
  redraw_vert(pVscrollBar);
  flush_rect(pVscrollBar->size);

  return -1;
}

/**************************************************************************
  Sell improvement dialog.
**************************************************************************/
static void del_imprv_from_imprv_list(struct GUI *pImprv)
{
  int count = 0;
  struct GUI *pBuf = pImprv;
  struct GUI *pEnd = NULL;

  /* if begin == end -> size = 1 */
  if (pCityDlg->pBeginCityImprWidgetList ==
      pCityDlg->pEndCityImprWidgetList) {
    pCityDlg->pBeginCityImprWidgetList = NULL;
    pCityDlg->pBeginActiveCityImprWidgetList = NULL;
    pCityDlg->pEndCityImprWidgetList = NULL;
    del_widget_from_gui_list(pImprv);
    return;
  }

  if (pCityDlg->pVscroll) {	/* exist scrollbar -> total count > 10 */

    /* find numbers of labels to mod. */
    while (pBuf != pCityDlg->pBeginActiveCityImprWidgetList) {
      count++;
      pBuf = pBuf->next;
    }

    /* if ( pCityDlg->pEndCityImprWidgetList !=
       pCityDlg->pBeginActiveCityImprWidgetList ) */
    if (pBuf != pCityDlg->pEndCityImprWidgetList) {	/* start from first seen label */
      pBuf = pBuf->next;
      clear_wflag(pBuf, WF_HIDDEN);

      count++;

      while (count) {
	pBuf->gfx = pBuf->prev->gfx;
	pBuf->prev->gfx = NULL;
	pBuf->size.y = pBuf->prev->size.y;
	pBuf = pBuf->prev;
	count--;
      }

    } else {			/* start from last seen label */

      /* find last */
      int active = 10;		/* active */
      while (active) {
	pBuf = pBuf->prev;
	active--;
      }

      pEnd = pBuf->next;
      count = 0;
      pBuf = pImprv;

      /* find numbers of labels to mod. */
      while (pBuf != pEnd->prev) {
	count++;
	pBuf = pBuf->prev;
      }

      clear_wflag(pBuf, WF_HIDDEN);
      while (count) {
	pBuf->gfx = pBuf->next->gfx;
	pBuf->next->gfx = NULL;
	pBuf->size.y = pBuf->next->size.y;

	pBuf = pBuf->next;
	count--;
      }

    }

  } else { /* no scrollbar */
    pBuf = pImprv;

    /* goto down list */
    while (pBuf != pCityDlg->pBeginCityImprWidgetList) {
      pBuf = pBuf->prev;
    }

    FREESURFACE(pBuf->gfx);

    while (pBuf != pImprv) {
      pBuf->gfx = pBuf->next->gfx;
      pBuf->next->gfx = NULL;
      pBuf->size.y = pBuf->next->size.y;

      pBuf = pBuf->next;
    }

    if (pImprv == pCityDlg->pBeginCityImprWidgetList) {
      pCityDlg->pBeginCityImprWidgetList = pImprv->next;
    }

  }

  if (pImprv == pCityDlg->pEndCityImprWidgetList) {

    if (pCityDlg->pBeginActiveCityImprWidgetList ==
	pCityDlg->pEndCityImprWidgetList) {
      pCityDlg->pBeginActiveCityImprWidgetList = pImprv->prev;
    }

    pCityDlg->pEndCityImprWidgetList = pImprv->prev;

  }


  if (pCityDlg->pBeginActiveCityImprWidgetList !=
      pCityDlg->pEndCityImprWidgetList) {
    pCityDlg->pBeginActiveCityImprWidgetList =
	pCityDlg->pBeginActiveCityImprWidgetList->next;
  }

  del_widget_from_gui_list(pImprv);
}

/**************************************************************************
  ...
**************************************************************************/
static int sell_imprvm_dlg_cancel_callback(struct GUI *pCancel_Button)
{
  popdown_window_group_dialog(pCityDlg->pBeginCityMenuWidgetList,
			      pCityDlg->pEndCityMenuWidgetList);
  pCityDlg->pEndCityMenuWidgetList = NULL;
  enable_city_dlg_widgets();
  flush_dirty();
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int sell_imprvm_dlg_ok_callback(struct GUI *pOK_Button)
{

  struct packet_city_request packet;
  struct GUI *pTmp = NULL;

  packet.city_id = pCityDlg->pCity->id;
  packet.build_id = MAX_ID - 4000 - pOK_Button->ID;

  del_group_of_widgets_from_gui_list(pCityDlg->pBeginCityMenuWidgetList,
				     pCityDlg->pEndCityMenuWidgetList);

  pCityDlg->pEndCityMenuWidgetList = NULL;

  /* find impr. in list */
  pTmp = get_widget_pointer_form_main_list(pOK_Button->ID + 1000);

  del_imprv_from_imprv_list(pTmp);

  if (pCityDlg->pVscroll) {
    pCityDlg->pVscroll->count--;

    pTmp = pCityDlg->pBeginCityImprWidgetList;

    if (pCityDlg->pVscroll->count == pCityDlg->pVscroll->active) {

      del_widget_from_gui_list(pTmp->next->next);	/* up button */
      del_widget_from_gui_list(pTmp->next);	/* down button */
      del_widget_from_gui_list(pTmp);	/* vscrollbar */
      FREE(pCityDlg->pVscroll);

    } else {
      SDL_BlitSurface(pTmp->gfx, NULL, pTmp->dst, &pTmp->size);
      FREESURFACE(pTmp->gfx);

      pTmp->size.h = ((float) 10 / pCityDlg->pVscroll->count) * 94;
    }

    if (pCityDlg->pVscroll) {
      pTmp = pCityDlg->pBeginCityImprWidgetList->next->next->next;
    } else {
      pTmp = pCityDlg->pBeginCityImprWidgetList;
    }

  } else {
    pTmp = pCityDlg->pBeginCityImprWidgetList;
  }

  send_packet_city_request(&aconnection, &packet, PACKET_CITY_SELL);

  enable_city_dlg_widgets();

  if (pCityDlg->pEndCityImprWidgetList) {
    set_group_state(pTmp, pCityDlg->pEndCityImprWidgetList, WS_DISABLED);
  }

  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int sell_imprvm_dlg_callback(struct GUI *pImpr)
{
  SDL_Surface *pLogo;
  struct SDL_String16 *pStr = NULL;
  struct GUI *pLabel = NULL;
  struct GUI *pWindow = NULL;
  struct GUI *pCancel_Button = NULL;
  struct GUI *pOK_Button = NULL;
  char cBuf[80];
  int ww;
  int id;

  unsellect_widget_action();
  disable_city_dlg_widgets();

  /* create ok button */
  pLogo = ZoomSurface(pTheme->OK_Icon, 0.7, 0.7, 1);

  SDL_SetColorKey(pLogo, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  get_first_pixel(pLogo));

  pOK_Button = create_themeicon_button_from_chars(pLogo, pImpr->dst,
				_("Sell"), 10,  WF_FREE_GFX);

  clear_wflag(pOK_Button, WF_DRAW_FRAME_AROUND_WIDGET);

  /* create cancel button */
  pLogo = ZoomSurface(pTheme->CANCEL_Icon, 0.7, 0.7, 1);

  SDL_SetColorKey(pLogo, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  get_first_pixel(pLogo));

  pCancel_Button =
      create_themeicon_button_from_chars(pLogo, pImpr->dst, _("Cancel"), 10,
					 WF_FREE_GFX);

  clear_wflag(pCancel_Button, WF_DRAW_FRAME_AROUND_WIDGET);

  id = MAX_ID - 3000 - pImpr->ID;

  my_snprintf(cBuf, sizeof(cBuf), _("Sell %s for %d gold?"),
	      get_impr_name_ex(pCityDlg->pCity, id),
	      improvement_value(id));


  /* create text label */
  pStr = create_str16_from_char(cBuf, 10);
  pStr->style |= TTF_STYLE_BOLD;
  pStr->forecol.r = 255;
  pStr->forecol.g = 255;
  /*pStr->forecol.b = 255; */
  pLabel = create_iconlabel(NULL, pImpr->dst, pStr, 0);

  /* create window */
  pStr = create_str16_from_char(_("Sell It?"), 12);
  pStr->style |= TTF_STYLE_BOLD;

  /* correct sizes */
  pOK_Button->size.w += 10;
  pCancel_Button->size.w = pOK_Button->size.w;

  if ((pOK_Button->size.w + pCancel_Button->size.w + 30) >
      pLabel->size.w + 20) {
    ww = pOK_Button->size.w + pCancel_Button->size.w + 30;
  } else {
    ww = pLabel->size.w + 20;
  }

  pWindow = create_window(pImpr->dst, pStr, ww,
	pOK_Button->size.h + pLabel->size.h + WINDOW_TILE_HIGH + 25, 0);

  /* set actions */
  /*pWindow->action = move_revolution_dlg_callback; */
  pCancel_Button->action = sell_imprvm_dlg_cancel_callback;
  pOK_Button->action = sell_imprvm_dlg_ok_callback;

  /* set keys */
  pOK_Button->key = SDLK_RETURN;
  pCancel_Button->key = SDLK_ESCAPE;

  /* I make this hack to center label on window */
  pLabel->size.w = pWindow->size.w;

  /* set start positions */
  pWindow->size.x = (pWindow->dst->w - pWindow->size.w) / 2;
  pWindow->size.y = (pWindow->dst->h - pWindow->size.h) / 2 + 10;


  pOK_Button->size.x = pWindow->size.x + 10;
  pOK_Button->size.y = pWindow->size.y + pWindow->size.h -
      pOK_Button->size.h - 10;


  pCancel_Button->size.y = pOK_Button->size.y;
  pCancel_Button->size.x = pWindow->size.x + pWindow->size.w -
      pCancel_Button->size.w - 10;

  pLabel->size.x = pWindow->size.x;
  pLabel->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 5;

  /* create window background */
  resize_window(pWindow, NULL,
		get_game_colorRGB(COLOR_STD_BACKGROUND_BROWN),
		pWindow->size.w, pWindow->size.h);

  /* enable widgets */
  set_wstate(pCancel_Button, WS_NORMAL);
  set_wstate(pOK_Button, WS_NORMAL);
  /*set_wstate( pWindow, WS_NORMAL ); */

  /* add widgets to main list */
  pCityDlg->pEndCityMenuWidgetList = pWindow;
  add_to_gui_list(ID_REVOLUTION_DLG_WINDOW, pWindow);
  add_to_gui_list(ID_REVOLUTION_DLG_LABEL, pLabel);
  add_to_gui_list(ID_REVOLUTION_DLG_CANCEL_BUTTON, pCancel_Button);
  add_to_gui_list(pImpr->ID - 1000, pOK_Button);
  pCityDlg->pBeginCityMenuWidgetList = pOK_Button;

  /* redraw */
  redraw_group(pCityDlg->pBeginCityMenuWidgetList,
	       pCityDlg->pEndCityMenuWidgetList, 0);

  flush_rect(pWindow->size);

  return -1;
}
/* ====================================================================== */

/**************************************************************************
  ...
**************************************************************************/
static void enable_city_dlg_widgets(void)
{

  if (pCityDlg->pEndCityImprWidgetList) {
    struct GUI *pBegin = pCityDlg->pBeginCityImprWidgetList;
    if (pCityDlg->pVscroll) {
      set_wstate(pBegin, WS_NORMAL);	/* vscroll */
      pBegin = pBegin->next;
      set_wstate(pBegin, WS_NORMAL);	/* up */
      pBegin = pBegin->next;
      set_wstate(pBegin, WS_NORMAL);	/* down */
      pBegin = pBegin->next;
    }

    if (pCityDlg->pCity->did_sell) {
      set_group_state(pBegin, pCityDlg->pEndCityImprWidgetList,
		      WS_DISABLED);
    } else {
      struct GUI *pTmpWidget = pCityDlg->pEndCityImprWidgetList;

      while (TRUE) {
	if (get_improvement_type(MAX_ID - 3000 - pTmpWidget->ID)->
	    is_wonder) {
	  set_wstate(pTmpWidget, WS_DISABLED);
	} else {
	  set_wstate(pTmpWidget, WS_NORMAL);
	}

	if (pTmpWidget == pBegin) {
	  break;
	}

	pTmpWidget = pTmpWidget->prev;

      }				/* while */
    }
  }

  if (pCityDlg->pCity->did_buy) {
    set_wstate(pCityDlg->pBuy_Button, WS_DISABLED);
  }

  if (pCityDlg->pEndCityPanelWidgetList) {
    set_group_state(pCityDlg->pBeginCityPanelWidgetList,
		    pCityDlg->pEndCityPanelWidgetList, WS_NORMAL);
  }

  set_group_state(pCityDlg->pBeginCityWidgetList,
		  pCityDlg->pEndCityWidgetList->prev, WS_NORMAL);

}

/**************************************************************************
  ...
**************************************************************************/
static void disable_city_dlg_widgets(void)
{
  if (pCityDlg->pEndCityPanelWidgetList) {
    set_group_state(pCityDlg->pBeginCityPanelWidgetList,
		    pCityDlg->pEndCityPanelWidgetList, WS_DISABLED);
  }


  if (pCityDlg->pEndCityImprWidgetList) {
    set_group_state(pCityDlg->pBeginCityImprWidgetList,
		    pCityDlg->pEndCityImprWidgetList, WS_DISABLED);
  }

  set_group_state(pCityDlg->pBeginCityWidgetList,
		  pCityDlg->pEndCityWidgetList->prev, WS_DISABLED);
}
/* ======================================================================== */

/**************************************************************************
  city resource map: calculate screen position to city map position.
**************************************************************************/
static bool get_citymap_cr(Sint16 map_x, Sint16 map_y, int *pCol,
			   int *pRow)
{
  float result;
  float a =
      (float) (map_x - SCALLED_TILE_WIDTH * 2) / SCALLED_TILE_WIDTH;
  float b =
      (float) (map_y + SCALLED_TILE_HEIGHT / 2) / SCALLED_TILE_HEIGHT;

  result = a + b;
  if (result < 0) {
    *pCol = result - 1;
  } else {
    *pCol = result;
  }

  result = b - a;
  if (result < 0) {
    *pRow = result - 1;
  } else {
    *pRow = result;
  }

  return is_valid_city_coords(*pCol, *pRow);
}

/**************************************************************************
  city resource map: event callback
**************************************************************************/
static int resource_map_city_dlg_callback(struct GUI *pMap)
{
  int col, row;

  if (get_citymap_cr(Main.event.motion.x - pMap->size.x,
		     Main.event.motion.y - pMap->size.y, &col, &row)) {
    struct packet_city_request packet;

    packet.city_id = pCityDlg->pCity->id;
    packet.worker_x = col;
    packet.worker_y = row;

    if (pCityDlg->pCity->city_map[col][row] == C_TILE_WORKER) {
      send_packet_city_request(&aconnection, &packet,
			       PACKET_CITY_MAKE_SPECIALIST);
    } else if (pCityDlg->pCity->city_map[col][row] == C_TILE_EMPTY) {
      send_packet_city_request(&aconnection, &packet,
			       PACKET_CITY_MAKE_WORKER);
    }
  }

  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static void fill_tile_resorce_surf(SDL_Surface * pTile, struct city *pCity,
				   Uint16 city_col, Uint16 city_row)
{
  int i, step;
  SDL_Rect dest;
  int food = city_get_food_tile(city_col, city_row, pCity);
  int shield = city_get_shields_tile(city_col, city_row, pCity);
  int trade = city_get_trade_tile(city_col, city_row, pCity);

  dest.y = (SCALLED_TILE_HEIGHT - pIcons->pFood->h) / 2;
  dest.x = 10;
  step = (SCALLED_TILE_WIDTH - 2 * dest.x) / (food + shield + trade);

  for (i = 0; i < food; i++) {
    SDL_BlitSurface(pIcons->pFood, NULL, pTile, &dest);
    dest.x += step;
  }

  for (i = 0; i < shield; i++) {
    SDL_BlitSurface(pIcons->pShield, NULL, pTile, &dest);
    dest.x += step;
  }

  for (i = 0; i < trade; i++) {
    SDL_BlitSurface(pIcons->pTrade, NULL, pTile, &dest);
    dest.x += step;
  }
}

/**************************************************************************
  Refresh (update) the city resource map
**************************************************************************/
static void refresh_city_resource_map(struct GUI *pMap, struct city *pCity)
{
  register Uint16 col = 0, row;
  SDL_Rect dest;
  int sx, sy, row0, real_col = pCity->x, real_row = pCity->y;

  Sint16 x0 =
      pMap->size.x + SCALLED_TILE_WIDTH + SCALLED_TILE_WIDTH / 2;
  Sint16 y0 = pMap->size.y - SCALLED_TILE_HEIGHT / 2;

  SDL_Surface *pTile = create_surf(SCALLED_TILE_WIDTH,
				   SCALLED_TILE_HEIGHT, SDL_SWSURFACE);

  SDL_SetColorKey(pTile, SDL_SRCCOLORKEY, 0x0);

  real_col -= 2;
  real_row -= 2;
  correction_map_pos((int *) &real_col, (int *) &real_row);
  row0 = real_row;

  /* draw loop */
  for (; col < CITY_MAP_SIZE; col++) {
    for (row = 0; row < CITY_MAP_SIZE; row++) {
      /* calculate start pixel position and check if it belong to 'pDest' */
      sx = x0 + (col - row) * (SCALLED_TILE_WIDTH / 2);
      sy = y0 + (row + col) * (SCALLED_TILE_HEIGHT / 2);

      if (!((!col && !row) ||
	    (!col && (row == CITY_MAP_SIZE - 1)) ||
	    (!row && (col == CITY_MAP_SIZE - 1)) ||
	    ((col == CITY_MAP_SIZE - 1) && (row == CITY_MAP_SIZE - 1))
	  )
	  ) {
	dest.x = sx;
	dest.y = sy;
	if (is_worker_here(pCity, col, row)) {
	  fill_tile_resorce_surf(pTile, pCity, col, row);

	  SDL_BlitSurface(pTile, NULL, pMap->dst, &dest);

	  /* clear pTile */
	  SDL_FillRect(pTile, NULL, 0);
	}
      }

      /* inc row with correct */
      if (++real_row >= map.ysize) {
	real_row = 0;
      }

    }
    real_row = row0;
    /* inc col with correct */
    if (++real_col >= map.xsize) {
      real_col = 0;
    }

  }

  FREESURFACE(pTile);
}

/* ====================================================================== */

/************************************************************************
  Helper for switch_city_callback.
*************************************************************************/
static int city_comp_by_turn_founded(const void *a, const void *b)
{
  struct city *pcity1 = *((struct city **) a);
  struct city *pcity2 = *((struct city **) b);

  return pcity1->turn_founded - pcity2->turn_founded;
}

/**************************************************************************
  Callback for next/prev city button
**************************************************************************/
static int next_prev_city_dlg_callback(struct GUI *pButton)
{
  int i, dir, non_open_size, size =
      city_list_size(&game.player_ptr->cities);
  struct city **array;
  SDL_Surface *pSurf;

  assert(size >= 1);
  assert(pCityDlg->pCity->owner == game.player_idx);

  if (size == 1) {
    return -1;
  }

  /* dir = 1 will advance to the city, dir = -1 will get previous */
  if (pButton->ID == ID_CITY_DLG_NEXT_BUTTON) {
    dir = 1;
  } else {
    if (pButton->ID == ID_CITY_DLG_PREV_BUTTON) {
      dir = -1;
    } else {
      assert(0);
      dir = 1;
    }
  }

  array = MALLOC(size * sizeof(struct city *));

  non_open_size = 0;
  for (i = 0; i < size; i++) {
    array[non_open_size++] = city_list_get(&game.player_ptr->cities, i);
  }

  assert(non_open_size > 0);

  if (non_open_size == 1) {
    FREE(array);
    return -1;
  }

  qsort(array, non_open_size, sizeof(struct city *),
	city_comp_by_turn_founded);

  for (i = 0; i < non_open_size; i++) {
    if (pCityDlg->pCity == array[i]) {
      break;
    }
  }

  assert(i < non_open_size);

  pCityDlg->pCity = array[(i + dir + non_open_size) % non_open_size];

  FREE(array);

  /* free panel widgets */
  if (pCityDlg->pEndCityPanelWidgetList) {
    del_group_of_widgets_from_gui_list(pCityDlg->pBeginCityPanelWidgetList,
				       pCityDlg->pEndCityPanelWidgetList);
    pCityDlg->pEndCityPanelWidgetList = NULL;
    FREE(pCityDlg->pPanelVscroll);
  }

  /* refresh resource map */
  pSurf = create_city_map(pCityDlg->pCity);
  FREESURFACE(pCityDlg->pResource_Map->theme);
  pCityDlg->pResource_Map->theme = ResizeSurface(pSurf, 192, 96, 1);
  FREESURFACE(pSurf);

  rebuild_imprm_list(pCityDlg->pCity);

  /* redraw */
  redraw_city_dialog(pCityDlg->pCity);
  flush_dirty();
  return -1;
}

/**************************************************************************
  Rename city name: 
**************************************************************************/
static int new_name_city_dlg_callback(struct GUI *pEdit)
{
  char *tmp = convert_to_chars(pEdit->string16->text);

  if(!tmp) {
    return -1;
  }
  
  if(strcmp(tmp, pCityDlg->pCity->name)) {
    struct packet_city_request packet;
    packet.city_id = pCityDlg->pCity->id;
    sz_strlcpy(packet.name, tmp);
    SDL_Client_Flags |= CF_CHANGED_CITY_NAME;
    send_packet_city_request(&aconnection, &packet, PACKET_CITY_RENAME);
  }
  
  FREE(tmp);
  return -1;
}

/* ======================================================================= */
/* ======================== Redrawing City Dlg. ========================== */
/* ======================================================================= */

/**************************************************************************
  Refresh (update) the city names for the dialog
**************************************************************************/
static void refresh_city_names(struct city *pCity)
{
  char *tmp = convert_to_chars(pCityDlg->pCity_Name_Edit->string16->text);

  if ((strcmp(pCity->name, tmp) != 0)
      || (SDL_Client_Flags & CF_CHANGED_CITY_NAME)) {
    FREE(pCityDlg->pCity_Name_Edit->string16->text);
    pCityDlg->pCity_Name_Edit->string16->text =
	convert_to_utf16(pCity->name);
    rebuild_citydlg_title_str(pCityDlg->pEndCityWidgetList, pCity);
    SDL_Client_Flags &= ~CF_CHANGED_CITY_NAME;
  }

  FREE(tmp);
}

/**************************************************************************
  Redraw city option panel
  207 = max panel width
**************************************************************************/
static void redraw_misc_city_dialog(struct GUI *pCityWindow,
				    struct city *pCity)
{
  char cBuf[60];
  SDL_String16 *pStr;
  SDL_Surface *pSurf;
  SDL_Rect dest;

  my_snprintf(cBuf, sizeof(cBuf), _("Options panel"));

  pStr = create_str16_from_char(cBuf, 10);
  pStr->forecol.r = 238;
  pStr->forecol.g = 156;
  pStr->forecol.b = 7;
  pStr->style |= TTF_STYLE_BOLD;

  pSurf = create_text_surf_from_str16(pStr);

  dest.x = pCityWindow->size.x + 5 + (207 - pSurf->w) / 2;
  dest.y = pCityWindow->size.y + WINDOW_TILE_HIGH + 5;

  SDL_BlitSurface(pSurf, NULL, pCityWindow->dst, &dest);

  FREESURFACE(pSurf);
  FREESTRING16(pStr);

  if (pCityDlg->pEndCityPanelWidgetList) {
    redraw_group(pCityDlg->pBeginCityPanelWidgetList,
		 pCityDlg->pEndCityPanelWidgetList, 0);
  } else {
    create_city_options_widget_list(pCity);
    redraw_group(pCityDlg->pBeginCityPanelWidgetList,
		 pCityDlg->pEndCityPanelWidgetList, 0);
  }
}

/**************************************************************************
  Redraw supported unit panel
  207 = max panel width
**************************************************************************/
static void redraw_supported_units_city_dialog(struct GUI *pCityWindow,
					       struct city *pCity)
{
  char cBuf[60];
  SDL_String16 *pStr;
  SDL_Surface *pSurf;
  SDL_Rect dest;
  struct unit_list *pList;
  int size;

  if (pCityDlg->pCity->owner != game.player_idx) {
    pList = &(pCityDlg->pCity->info_units_supported);
  } else {
    pList = &(pCityDlg->pCity->units_supported);
  }

  size = unit_list_size(pList);

  my_snprintf(cBuf, sizeof(cBuf), _("Unit maintenance panel (%d %s)"),
	      size, PL_("unit", "units", size));

  pStr = create_str16_from_char(cBuf, 10);
  pStr->forecol.r = 238;
  pStr->forecol.g = 156;
  pStr->forecol.b = 7;
  pStr->style |= TTF_STYLE_BOLD;

  pSurf = create_text_surf_from_str16(pStr);

  dest.x = pCityWindow->size.x + 5 + (207 - pSurf->w) / 2;
  dest.y = pCityWindow->size.y + WINDOW_TILE_HIGH + 5;

  SDL_BlitSurface(pSurf, NULL, pCityWindow->dst, &dest);

  FREESURFACE(pSurf);
  FREESTRING16(pStr);

  if (pCityDlg->pEndCityPanelWidgetList) {
    if (size) {
      redraw_group(pCityDlg->pBeginCityPanelWidgetList,
		   pCityDlg->pEndCityPanelWidgetList, 0);
    } else {
      del_group_of_widgets_from_gui_list(pCityDlg->
					 pBeginCityPanelWidgetList,
					 pCityDlg->
					 pEndCityPanelWidgetList);
      pCityDlg->pEndCityPanelWidgetList = NULL;
      FREE(pCityDlg->pPanelVscroll);
    }

  } else {
    if (size) {
      create_present_supported_units_widget_list(pList);
      redraw_group(pCityDlg->pBeginCityPanelWidgetList,
		   pCityDlg->pEndCityPanelWidgetList, 0);
    }
  }
}

/**************************************************************************
  Redraw garrison panel
  207 = max panel width
**************************************************************************/
static void redraw_army_city_dialog(struct GUI *pCityWindow,
				    struct city *pCity)
{
  char cBuf[60];
  SDL_String16 *pStr;
  SDL_Surface *pSurf;
  SDL_Rect dest;
  struct unit_list *pList;

  int size;

  if (pCityDlg->pCity->owner != game.player_idx) {
    pList = &(pCityDlg->pCity->info_units_present);
  } else {
    pList = &(map_get_tile(pCityDlg->pCity->x, pCityDlg->pCity->y)->units);
  }

  size = unit_list_size(pList);

  my_snprintf(cBuf, sizeof(cBuf), _("Garrison Panel (%d %s)"),
	      size, PL_("unit", "units", size));

  pStr = create_str16_from_char(cBuf, 10);
  pStr->forecol.r = 238;
  pStr->forecol.g = 156;
  pStr->forecol.b = 7;
  pStr->style |= TTF_STYLE_BOLD;

  pSurf = create_text_surf_from_str16(pStr);

  dest.x = pCityWindow->size.x + 5 + (207 - pSurf->w) / 2;
  dest.y = pCityWindow->size.y + WINDOW_TILE_HIGH + 5;

  SDL_BlitSurface(pSurf, NULL, pCityWindow->dst, &dest);

  FREESURFACE(pSurf);
  FREESTRING16(pStr);

  if (pCityDlg->pEndCityPanelWidgetList) {
    if (size) {
      redraw_group(pCityDlg->pBeginCityPanelWidgetList,
		   pCityDlg->pEndCityPanelWidgetList, 0);
    } else {
      del_group_of_widgets_from_gui_list(pCityDlg->
					 pBeginCityPanelWidgetList,
					 pCityDlg->
					 pEndCityPanelWidgetList);
      pCityDlg->pEndCityPanelWidgetList = NULL;
      FREE(pCityDlg->pPanelVscroll);
    }
  } else {
    if (size) {

      create_present_supported_units_widget_list(pList);

      redraw_group(pCityDlg->pBeginCityPanelWidgetList,
		   pCityDlg->pEndCityPanelWidgetList, 0);

    }
  }
}

/**************************************************************************
  Redraw Info panel
  207 = max panel width
**************************************************************************/
static void redraw_info_city_dialog(struct GUI *pCityWindow,
				    struct city *pCity)
{
  char cBuf[30];
  struct city *pTradeCity = NULL;
  int step, i, xx;
  SDL_String16 *pStr = NULL;
  SDL_Surface *pSurf = NULL;
  SDL_Rect dest =
      { (pCityWindow->size.x + 10), (pCityWindow->size.y + 25), 0, 0 };

  my_snprintf(cBuf, sizeof(cBuf), _("Info Panel"));
  pStr = create_str16_from_char(cBuf, 10);
  pStr->forecol.r = 238;
  pStr->forecol.g = 156;
  pStr->forecol.b = 7;
  pStr->style |= TTF_STYLE_BOLD;

  pSurf = create_text_surf_from_str16(pStr);

  FREE(pStr->text);

  dest.x = pCityWindow->size.x + 5 + (207 - pSurf->w) / 2;

  SDL_BlitSurface(pSurf, NULL, pCityWindow->dst, &dest);

  dest.x = pCityWindow->size.x + 10;
  dest.y += pSurf->h + 1;

  FREESURFACE(pSurf);

  change_ptsize16(pStr, 12);
  pStr->forecol.r = 220;
  pStr->forecol.g = 186;
  pStr->forecol.b = 60;

  if (pCity->pollution) {
    my_snprintf(cBuf, sizeof(cBuf), _("Pollution : %d"),
		pCity->pollution);

    pStr->text = convert_to_utf16(cBuf);

    pSurf = create_text_surf_from_str16(pStr);

    FREE(pStr->text);

    SDL_BlitSurface(pSurf, NULL, pCityWindow->dst, &dest);

    dest.y += pSurf->h + 3;

    FREESURFACE(pSurf);

    if (((pIcons->pPollution->w + 1) * pCity->pollution) > 187) {
      step = (187 - pIcons->pPollution->w) / (pCity->pollution - 1);
    } else {
      step = pIcons->pPollution->w + 1;
    }

    for (i = 0; i < pCity->pollution; i++) {
      SDL_BlitSurface(pIcons->pPollution, NULL, pCityWindow->dst, &dest);
      dest.x += step;
    }

    dest.x = pCityWindow->size.x + 10;
    dest.y += pIcons->pPollution->h + 30;

  } else {
    my_snprintf(cBuf, sizeof(cBuf), _("Pollution : none"));

    pStr->text = convert_to_utf16(cBuf);

    pSurf = create_text_surf_from_str16(pStr);

    FREE(pStr->text);

    SDL_BlitSurface(pSurf, NULL, pCityWindow->dst, &dest);

    dest.y += pSurf->h + 3;

    FREESURFACE(pSurf);
  }

  my_snprintf(cBuf, sizeof(cBuf), _("Trade routes : "));

  pStr->text = convert_to_utf16(cBuf);

  pSurf = create_text_surf_from_str16(pStr);

  FREE(pStr->text);

  SDL_BlitSurface(pSurf, NULL, pCityWindow->dst, &dest);

  xx = dest.x + pSurf->w;
  dest.y += pSurf->h + 3;

  FREESURFACE(pSurf);

  step = 0;
  dest.x = pCityWindow->size.x + 10;

  for (i = 0; i < NUM_TRADEROUTES; i++) {
    if (pCity->trade[i]) {
      step += pCity->trade_value[i];

      if ((pTradeCity = find_city_by_id(pCity->trade[i]))) {
	my_snprintf(cBuf, sizeof(cBuf), "%s : +%d", pTradeCity->name,
		    pCity->trade_value[i]);
      } else {
	my_snprintf(cBuf, sizeof(cBuf), "%s : +%d", _("Unknown"),
		    pCity->trade_value[i]);
      }


      pStr->text = convert_to_utf16(cBuf);

      pSurf = create_text_surf_from_str16(pStr);

      FREE(pStr->text);

      SDL_BlitSurface(pSurf, NULL, pCityWindow->dst, &dest);

      /* blit trade icon */
      dest.x += pSurf->w + 3;
      dest.y += 4;
      SDL_BlitSurface(pIcons->pTrade, NULL, pCityWindow->dst, &dest);
      dest.x = pCityWindow->size.x + 10;
      dest.y -= 4;

      dest.y += pSurf->h;

      FREESURFACE(pSurf);
    }
  }

  if (step) {
    my_snprintf(cBuf, sizeof(cBuf), _("Trade : +%d"), step);

    pStr->text = convert_to_utf16(cBuf);
    pSurf = create_text_surf_from_str16(pStr);
    SDL_BlitSurface(pSurf, NULL, pCityWindow->dst, &dest);

    dest.x += pSurf->w + 3;
    dest.y += 4;
    SDL_BlitSurface(pIcons->pTrade, NULL, pCityWindow->dst, &dest);

    FREESURFACE(pSurf);
  } else {
    my_snprintf(cBuf, sizeof(cBuf), _("none"));

    pStr->text = convert_to_utf16(cBuf);

    pSurf = create_text_surf_from_str16(pStr);

    FREE(pStr->text);

    dest.x = xx;
    dest.y -= pSurf->h + 3;
    SDL_BlitSurface(pSurf, NULL, pCityWindow->dst, &dest);

    FREESURFACE(pSurf);
  }


  FREESTRING16(pStr);
}

/**************************************************************************
  Redraw (refresh/update) the happiness info for the dialog
  207 - max panel width
  180 - max citizens icons area width
**************************************************************************/
static void redraw_happyness_city_dialog(const struct GUI *pCityWindow,
					 struct city *pCity)
{
  char cBuf[30];
  int step, i, j, count;
  SDL_Surface *pTmp1, *pTmp2, *pTmp3, *pTmp4;
  SDL_String16 *pStr = NULL;
  SDL_Surface *pSurf = NULL;
  SDL_Rect dest =
      { (pCityWindow->size.x + 10), (pCityWindow->size.y + 25), 0, 0 };

  my_snprintf(cBuf, sizeof(cBuf), _("Happiness panel"));

  pStr = create_str16_from_char(cBuf, 10);
  pStr->forecol.r = 238;
  pStr->forecol.g = 156;
  pStr->forecol.b = 7;
  pStr->style |= TTF_STYLE_BOLD;

  pSurf = create_text_surf_from_str16(pStr);

  dest.x = pCityWindow->size.x + 5 + (207 - pSurf->w) / 2;

  SDL_BlitSurface(pSurf, NULL, pCityWindow->dst, &dest);

  dest.x = pCityWindow->size.x + 10;
  dest.y += pSurf->h + 1;

  FREESURFACE(pSurf);
  FREESTRING16(pStr);

  count = pCity->ppl_happy[4] + pCity->ppl_content[4] +
      pCity->ppl_unhappy[4] + pCity->ppl_angry[4] +
      pCity->ppl_elvis + pCity->ppl_scientist + pCity->ppl_taxman;

  if (count * pIcons->pMale_Happy->w > 180) {
    step = (180 - pIcons->pMale_Happy->w) / (count - 1);
  } else {
    step = pIcons->pMale_Happy->w;
  }

  for (j = 0; j < 5; j++) {
    if (j == 0 || pCity->ppl_happy[j - 1] != pCity->ppl_happy[j]
	|| pCity->ppl_content[j - 1] != pCity->ppl_content[j]
	|| pCity->ppl_unhappy[j - 1] != pCity->ppl_unhappy[j]
	|| pCity->ppl_angry[j - 1] != pCity->ppl_angry[j]) {

      if (j != 0) {
	putline(pCityWindow->dst, dest.x, dest.y, dest.x + 195,
					dest.y, 0xff000000);
	dest.y += 5;
      }

      if (pCity->ppl_happy[j]) {
	pSurf = pIcons->pMale_Happy;
	for (i = 0; i < pCity->ppl_happy[j]; i++) {
	  SDL_BlitSurface(pSurf, NULL, pCityWindow->dst, &dest);
	  dest.x += step;
	  if (pSurf == pIcons->pMale_Happy) {
	    pSurf = pIcons->pFemale_Happy;
	  } else {
	    pSurf = pIcons->pMale_Happy;
	  }
	}
      }

      if (pCity->ppl_content[j]) {
	pSurf = pIcons->pMale_Content;
	for (i = 0; i < pCity->ppl_content[j]; i++) {
	  SDL_BlitSurface(pSurf, NULL, pCityWindow->dst, &dest);
	  dest.x += step;
	  if (pSurf == pIcons->pMale_Content) {
	    pSurf = pIcons->pFemale_Content;
	  } else {
	    pSurf = pIcons->pMale_Content;
	  }
	}
      }

      if (pCity->ppl_unhappy[j]) {
	pSurf = pIcons->pMale_Unhappy;
	for (i = 0; i < pCity->ppl_unhappy[j]; i++) {
	  SDL_BlitSurface(pSurf, NULL, pCityWindow->dst, &dest);
	  dest.x += step;
	  if (pSurf == pIcons->pMale_Unhappy) {
	    pSurf = pIcons->pFemale_Unhappy;
	  } else {
	    pSurf = pIcons->pMale_Unhappy;
	  }
	}
      }

      if (pCity->ppl_angry[j]) {
	pSurf = pIcons->pMale_Angry;
	for (i = 0; i < pCity->ppl_angry[j]; i++) {
	  SDL_BlitSurface(pSurf, NULL, pCityWindow->dst, &dest);
	  dest.x += step;
	  if (pSurf == pIcons->pMale_Angry) {
	    pSurf = pIcons->pFemale_Angry;
	  } else {
	    pSurf = pIcons->pMale_Angry;
	  }
	}
      }

      if (pCity->ppl_elvis) {
	for (i = 0; i < pCity->ppl_elvis; i++) {
	  SDL_BlitSurface(pIcons->pSpec_Lux, NULL, pCityWindow->dst, &dest);
	  dest.x += step;
	}
      }

      if (pCity->ppl_taxman) {
	for (i = 0; i < pCity->ppl_taxman; i++) {
	  SDL_BlitSurface(pIcons->pSpec_Tax, NULL, pCityWindow->dst, &dest);
	  dest.x += step;
	}
      }

      if (pCity->ppl_scientist) {
	for (i = 0; i < pCity->ppl_scientist; i++) {
	  SDL_BlitSurface(pIcons->pSpec_Sci, NULL, pCityWindow->dst, &dest);
	  dest.x += step;
	}
      }

      if (j == 1) { /* luxury effect */
	dest.x =
	    pCityWindow->size.x + 212 - pIcons->pBIG_Luxury->w - 2;
	count = dest.y;
	dest.y += (pIcons->pMale_Happy->h -
		   pIcons->pBIG_Luxury->h) / 2;
	SDL_BlitSurface(pIcons->pBIG_Luxury, NULL, pCityWindow->dst, &dest);
	dest.y = count;
      }

      if (j == 2) { /* improvments effects */
	pSurf = NULL;
	count = 0;

	if (city_got_building(pCity, B_TEMPLE)) {
	  pTmp1 =
	    ZoomSurface(GET_SURF(get_improvement_type(B_TEMPLE)->sprite),
			0.5, 0.5, 1);
	  count += (pTmp1->h + 1);
	  pSurf = pTmp1;
	} else {
	  pTmp1 = NULL;
	}

	if (city_got_building(pCity, B_COLOSSEUM)) {
	  pTmp2 =
	    ZoomSurface(GET_SURF(get_improvement_type(B_COLOSSEUM)->sprite),
			0.5, 0.5, 1);
	  count += (pTmp2->h + 1);
	  if (!pSurf) {
	    pSurf = pTmp2;
	  }
	} else {
	  pTmp2 = NULL;
	}

	if (city_got_building(pCity, B_CATHEDRAL) ||
	    city_affected_by_wonder(pCity, B_MICHELANGELO)) {
	  pTmp3 =
	    ZoomSurface(GET_SURF(get_improvement_type(B_CATHEDRAL)->sprite),
			0.5, 0.5, 1);
	  count += (pTmp3->h + 1);
	  if (!pSurf) {
	    pSurf = pTmp3;
	  }
	} else {
	  pTmp3 = NULL;
	}


	dest.x = pCityWindow->size.x + 212 - pSurf->w - 2;
	i = dest.y;
	dest.y += (pIcons->pMale_Happy->h - count) / 2;


	if (pTmp1) { /* Temple */
	  SDL_BlitSurface(pTmp1, NULL, pCityWindow->dst, &dest);
	  dest.y += (pTmp1->h + 1);
	}

	if (pTmp2) { /* Colosseum */
	  SDL_BlitSurface(pTmp2, NULL, pCityWindow->dst, &dest);
	  dest.y += (pTmp2->h + 1);
	}

	if (pTmp3) { /* Cathedral */
	  SDL_BlitSurface(pTmp3, NULL, pCityWindow->dst, &dest);
	  /*dest.y += (pTmp3->h + 1); */
	}


	FREESURFACE(pTmp1);
	FREESURFACE(pTmp2);
	FREESURFACE(pTmp3);
	dest.y = i;
      }

      if (j == 3) { /* police effect */
	dest.x = pCityWindow->size.x + 212 - pIcons->pPolice->w - 5;
	i = dest.y;
	dest.y +=
	    (pIcons->pMale_Happy->h - pIcons->pPolice->h) / 2;
	SDL_BlitSurface(pIcons->pPolice, NULL, pCityWindow->dst, &dest);
	dest.y = i;
      }

      if (j == 4) { /* wonders effect */
	count = 0;

	if (city_affected_by_wonder(pCity, B_CURE)) {
	  pTmp1 =
	    ZoomSurface(GET_SURF(get_improvement_type(B_CURE)->sprite),
			0.5, 0.5, 1);
	  count += (pTmp1->h + 1);
	  pSurf = pTmp1;
	} else {
	  pTmp1 = NULL;
	}

	if (city_affected_by_wonder(pCity, B_SHAKESPEARE)) {
	  pTmp2 = ZoomSurface(
	  	GET_SURF(get_improvement_type(B_SHAKESPEARE)->sprite),
			      0.5, 0.5, 1);
	  count += (pTmp2->h + 1);
	  if (!pSurf) {
	    pSurf = pTmp2;
	  }
	} else {
	  pTmp2 = NULL;
	}

	if (city_affected_by_wonder(pCity, B_BACH)) {
	  pTmp3 =
	    ZoomSurface(GET_SURF(get_improvement_type(B_BACH)->sprite),
			0.5, 0.5, 1);
	  count += (pTmp3->h + 1);
	  if (!pSurf) {
	    pSurf = pTmp3;
	  }
	} else {
	  pTmp3 = NULL;
	}

	if (city_affected_by_wonder(pCity, B_HANGING)) {
	  pTmp4 =
	    ZoomSurface(GET_SURF(get_improvement_type(B_HANGING)->sprite),
			0.5, 0.5, 1);
	  count += (pTmp4->h + 1);
	  if (!pSurf) {
	    pSurf = pTmp4;
	  }
	} else {
	  pTmp4 = NULL;
	}

	dest.x = pCityWindow->size.x + 212 - pSurf->w - 2;
	i = dest.y;
	dest.y += (pIcons->pMale_Happy->h - count) / 2;


	if (pTmp1) { /* Cure of Cancer */
	  SDL_BlitSurface(pTmp1, NULL, pCityWindow->dst, &dest);
	  dest.y += (pTmp1->h + 1);
	}

	if (pTmp2) { /* Shakespeare Theater */
	  SDL_BlitSurface(pTmp2, NULL, pCityWindow->dst, &dest);
	  dest.y += (pTmp2->h + 1);
	}

	if (pTmp3) { /* J. S. Bach ... */
	  SDL_BlitSurface(pTmp3, NULL, pCityWindow->dst, &dest);
	  dest.y += (pTmp3->h + 1);
	}

	if (pTmp4) { /* Hanging Gardens */
	  SDL_BlitSurface(pTmp4, NULL, pCityWindow->dst, &dest);
	  /*dest.y += (pTmp4->h + 1); */
	}

	FREESURFACE(pTmp1);
	FREESURFACE(pTmp2);
	FREESURFACE(pTmp3);
	FREESURFACE(pTmp4);
	dest.y = i;
      }

      dest.x = pCityWindow->size.x + 10;
      dest.y += pIcons->pMale_Happy->h + 5;

    }
  }
}

/**************************************************************************
  Redraw the dialog.
**************************************************************************/
static void redraw_city_dialog(struct city *pCity)
{
  char cBuf[40];
  int i, step, count, limit;
  int cost = 0; /* FIXME: possibly uninitialized */
  SDL_Rect dest, src;
  struct GUI *pWindow = pCityDlg->pEndCityWidgetList;
  SDL_Surface *pBuf = NULL;
  SDL_String16 *pStr = NULL;
  SDL_Color color = *get_game_colorRGB(COLOR_STD_GROUND);

  color.unused = 64; /* 25% transparecy */

  refresh_city_names(pCity);

  if ((city_unhappy(pCity) || city_celebrating(pCity) || city_happy(pCity))
      ^ ((SDL_Client_Flags & CF_CITY_STATUS_SPECIAL) > 0)) {
    /* city status was changed : NORMAL <-> DISORDER, HAPPY, CELEBR. */

    SDL_Client_Flags ^= CF_CITY_STATUS_SPECIAL;

    /* upd. resource map */
    pBuf = create_city_map(pCity);
    FREESURFACE(pCityDlg->pResource_Map->theme);
    pCityDlg->pResource_Map->theme = ResizeSurface(pBuf, 192, 96, 1);
    FREESURFACE(pBuf);

    /* upd. window title */
    rebuild_citydlg_title_str(pCityDlg->pEndCityWidgetList, pCity);
  }

  /* redraw city dlg */
  redraw_group(pCityDlg->pBeginCityWidgetList,
	       pCityDlg->pEndCityWidgetList, 0);
/*
  if (pCityDlg->pEndCityImprWidgetList) {
    redraw_group(pCityDlg->pBeginCityImprWidgetList,
		 pCityDlg->pEndCityImprWidgetList, 0);
  }
*/
  refresh_city_resource_map(get_widget_pointer_form_main_list
			    (ID_CITY_DLG_RESOURCE_MAP), pCity);

  /* ================================================================= */
  my_snprintf(cBuf, sizeof(cBuf), _("City map"));

  pStr = create_str16_from_char(cBuf, 11);
  pStr->forecol.r = 220;
  pStr->forecol.g = 186;
  pStr->forecol.b = 60;
  pStr->style |= TTF_STYLE_BOLD;

  pBuf = create_text_surf_from_str16(pStr);

  FREE(pStr->text);

  dest.x = pWindow->size.x + 222 + (115 - pBuf->w) / 2;
  dest.y = pWindow->size.y + 69 + (15 - pBuf->h) / 2;

  SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

  FREESURFACE(pBuf);

  my_snprintf(cBuf, sizeof(cBuf), _("Citizens"));

  pStr->text = convert_to_utf16(cBuf);
  pStr->forecol.r = 238;
  pStr->forecol.g = 156;
  pStr->forecol.b = 7;

  pBuf = create_text_surf_from_str16(pStr);

  FREE(pStr->text);

  dest.x = pWindow->size.x + 354 + (147 - pBuf->w) / 2;
  dest.y = pWindow->size.y + 67 + (13 - pBuf->h) / 2;

  SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

  FREESURFACE(pBuf);

  my_snprintf(cBuf, sizeof(cBuf), _("City Improvements"));

  pStr->text = convert_to_utf16(cBuf);
  pStr->forecol.r = 220;
  pStr->forecol.g = 186;
  pStr->forecol.b = 60;

  pBuf = create_text_surf_from_str16(pStr);

  FREE(pStr->text);

  dest.x = pWindow->size.x + 517 + (115 - pBuf->w) / 2;
  dest.y = pWindow->size.y + 69 + (15 - pBuf->h) / 2;

  SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

  FREESURFACE(pBuf);
  /* ================================================================= */
  /* food label */
  my_snprintf(cBuf, sizeof(cBuf), _("Food : %d per turn"),
	      pCity->food_prod);

  pStr->text = convert_to_utf16(cBuf);
  pStr->forecol = *get_game_colorRGB(COLOR_STD_GROUND);

  pBuf = create_text_surf_from_str16(pStr);

  FREE(pStr->text);

  dest.x = pWindow->size.x + 200;
  dest.y = pWindow->size.y + 227 + (16 - pBuf->h) / 2;

  SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

  FREESURFACE(pBuf);

  /* draw food income */
  dest.y = pWindow->size.y + 245 + (16 - pIcons->pBIG_Food->h) / 2;
  dest.x = pWindow->size.x + 203;

  if (pCity->food_surplus >= 0) {
    count = pCity->food_prod - pCity->food_surplus;
  } else {
    count = pCity->food_prod;
  }

  if (((pIcons->pBIG_Food->w + 1) * count) > 200) {
    step = (200 - pIcons->pBIG_Food->w) / (count - 1);
  } else {
    step = pIcons->pBIG_Food->w + 1;
  }

  for (i = 0; i < count; i++) {
    SDL_BlitSurface(pIcons->pBIG_Food, NULL, pWindow->dst, &dest);
    dest.x += step;
  }

  my_snprintf(cBuf, sizeof(cBuf), Q_("?food:Surplus : %d"),
	      pCity->food_surplus);

  pStr->text = convert_to_utf16(cBuf);
  pStr->forecol.r = 52;
  pStr->forecol.g = 122;
  pStr->forecol.b = 20;

  pBuf = create_text_surf_from_str16(pStr);

  FREE(pStr->text);

  dest.x = pWindow->size.x + 440 - pBuf->w;
  dest.y = pWindow->size.y + 227 + (16 - pBuf->h) / 2;

  SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

  FREESURFACE(pBuf);

  /* draw surplus of food */
  if (pCity->food_surplus) {

    if (pCity->food_surplus > 0) {
      count = pCity->food_surplus;
      pBuf = pIcons->pBIG_Food;
    } else {
      count = -1 * pCity->food_surplus;
      pBuf = pIcons->pBIG_Food_Corr;
    }

    dest.x = pWindow->size.x + 423;
    dest.y = pWindow->size.y + 245 + (16 - pBuf->h) / 2;

    /*if ( ((pBuf->w + 1) * count ) > 30 ) */
    if (count > 2) {
      if (count < 18) {
	step = (30 - pBuf->w) / (count - 1);
      } else {
	step = 1;
	count = 17;
      }
    } else {
      step = pBuf->w + 1;
    }

    for (i = 0; i < count; i++) {
      SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);
      dest.x -= step;
    }
  }
  /* ================================================================= */
  /* productions label */
  my_snprintf(cBuf, sizeof(cBuf), _("Production : %d (%d) per turn"),
	      pCity->shield_surplus ,
		  pCity->shield_prod + pCity->shield_waste);

  pStr->text = convert_to_utf16(cBuf);
  pStr->forecol.r = 124;
  pStr->forecol.g = 159;
  pStr->forecol.b = 221;

  pBuf = create_text_surf_from_str16(pStr);

  FREE(pStr->text);

  dest.x = pWindow->size.x + 200;
  dest.y = pWindow->size.y + 262 + (15 - pBuf->h) / 2;

  SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

  FREESURFACE(pBuf);

  /* draw productions schields */
  if (pCity->shield_surplus) {

    if (pCity->shield_surplus > 0) {
      count = pCity->shield_surplus + pCity->shield_waste;
      pBuf = pIcons->pBIG_Shield;
    } else {
      count = -1 * pCity->shield_surplus;
      pBuf = pIcons->pBIG_Shield_Corr;
    }

    dest.y = pWindow->size.y + 280 + (16 - pBuf->h) / 2;
    dest.x = pWindow->size.x + 203;

    if ((pBuf->w * count) > 200) {
      step = (200 - pBuf->w) / (count - 1);
    } else {
      step = pBuf->w;
    }

    for (i = 0; i < count; i++) {
      SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);
      dest.x += step;
      if(i > pCity->shield_surplus) {
	pBuf = pIcons->pBIG_Shield_Corr;
      }
    }
  }

  /* support shields label */
  my_snprintf(cBuf, sizeof(cBuf), Q_("?production:Support : %d"),
	      pCity->shield_prod - pCity->shield_surplus);

  pStr->text = convert_to_utf16(cBuf);
  pStr->forecol.r = 12;
  pStr->forecol.g = 18;
  pStr->forecol.b = 108;

  pBuf = create_text_surf_from_str16(pStr);

  FREE(pStr->text);

  dest.x = pWindow->size.x + 440 - pBuf->w;
  dest.y = pWindow->size.y + 262 + (15 - pBuf->h) / 2;

  SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

  FREESURFACE(pBuf);

  /* draw support shields */
  if (pCity->shield_prod - pCity->shield_surplus) {
    dest.x = pWindow->size.x + 423;
    dest.y =
	pWindow->size.y + 280 + (16 - pIcons->pBIG_Shield->h) / 2;
    if ((pIcons->pBIG_Shield->w + 1) * (pCity->shield_prod -
					    pCity->shield_surplus) > 30) {
      step =
	  (30 - pIcons->pBIG_Food->w) / (pCity->shield_prod -
					     pCity->shield_surplus - 1);
    } else {
      step = pIcons->pBIG_Shield->w + 1;
    }

    for (i = 0; i < (pCity->shield_prod - pCity->shield_surplus); i++) {
      SDL_BlitSurface(pIcons->pBIG_Shield, NULL, pWindow->dst, &dest);
      dest.x -= step;
    }
  }
  /* ================================================================= */

  /* trade label */
  my_snprintf(cBuf, sizeof(cBuf), _("Trade : %d per turn"),
	      pCity->trade_prod);

  pStr->text = convert_to_utf16(cBuf);
  pStr->forecol.r = 226;
  pStr->forecol.g = 82;
  pStr->forecol.b = 13;

  pBuf = create_text_surf_from_str16(pStr);

  FREE(pStr->text);

  dest.x = pWindow->size.x + 200;
  dest.y = pWindow->size.y + 297 + (15 - pBuf->h) / 2;

  SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

  FREESURFACE(pBuf);

  /* draw total (trade - corruption) */
  if (pCity->trade_prod) {
    dest.y =
	pWindow->size.y + 315 + (16 - pIcons->pBIG_Trade->h) / 2;
    dest.x = pWindow->size.x + 203;

    if (((pIcons->pBIG_Trade->w + 1) * pCity->trade_prod) > 200) {
      step = (200 - pIcons->pBIG_Trade->w) / (pCity->trade_prod - 1);
    } else {
      step = pIcons->pBIG_Trade->w + 1;
    }

    for (i = 0; i < pCity->trade_prod; i++) {
      SDL_BlitSurface(pIcons->pBIG_Trade, NULL, pWindow->dst, &dest);
      dest.x += step;
    }
  }

  /* corruption label */
  my_snprintf(cBuf, sizeof(cBuf), _("Corruption : %d"), pCity->corruption);

  pStr->text = convert_to_utf16(cBuf);
  pStr->forecol.r = 0;
  pStr->forecol.g = 0;
  pStr->forecol.b = 0;

  pBuf = create_text_surf_from_str16(pStr);

  FREE(pStr->text);

  dest.x = pWindow->size.x + 440 - pBuf->w;
  dest.y = pWindow->size.y + 297 + (15 - pBuf->h) / 2;

  SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

  FREESURFACE(pBuf);

  /* draw corruption */
  if (pCity->corruption) {
    dest.x = pWindow->size.x + 423;
    dest.y =
	pWindow->size.y + 315 + (16 - pIcons->pBIG_Trade->h) / 2;

    if (((pIcons->pBIG_Trade_Corr->w + 1) * pCity->corruption) > 30) {
      step =
	  (30 - pIcons->pBIG_Trade_Corr->w) / (pCity->corruption - 1);
    } else {
      step = pIcons->pBIG_Trade_Corr->w + 1;
    }

    for (i = 0; i < pCity->corruption; i++) {
      SDL_BlitSurface(pIcons->pBIG_Trade_Corr, NULL, pWindow->dst,
		      &dest);
      dest.x -= step;
    }

  }
  /* ================================================================= */
  /* gold label */
  my_snprintf(cBuf, sizeof(cBuf), _("Gold: %d (%d) per turn"),
	      city_gold_surplus(pCity), pCity->tax_total);

  pStr->text = convert_to_utf16(cBuf);
  /*pStr->forecol.r = 12;
     pStr->forecol.g = 18;
     pStr->forecol.b = 108; */

  pBuf = create_text_surf_from_str16(pStr);

  FREE(pStr->text);

  dest.x = pWindow->size.x + 200;
  dest.y = pWindow->size.y + 341 + (15 - pBuf->h) / 2;

  SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

  FREESURFACE(pBuf);

  /* draw coins */
  count = city_gold_surplus(pCity);
  if (count) {

    if (count > 0) {
      pBuf = pIcons->pBIG_Coin;
    } else {
      count *= -1;
      pBuf = pIcons->pBIG_Coin_Corr;
    }

    dest.y = pWindow->size.y + 358 + (16 - pBuf->h) / 2;
    dest.x = pWindow->size.x + 203;

    if ((pBuf->w * count) > 110) {
      step = (110 - pBuf->w) / (count - 1);
      if (!step) {
	step = 1;
	count = 97;
      }
    } else {
      step = pBuf->w;
    }

    for (i = 0; i < count; i++) {
      SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);
      dest.x += step;
    }

  }

  /* upkeep label */
  my_snprintf(cBuf, sizeof(cBuf), _("Upkeep : %d"), pCity->tax_total -
	      city_gold_surplus(pCity));

  pStr->text = convert_to_utf16(cBuf);
  pStr->forecol.r = 12;
  pStr->forecol.g = 18;
  pStr->forecol.b = 108;

  pBuf = create_text_surf_from_str16(pStr);

  FREE(pStr->text);

  dest.x = pWindow->size.x + 440 - pBuf->w;
  dest.y = pWindow->size.y + 341 + (15 - pBuf->h) / 2;

  SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

  FREESURFACE(pBuf);

  /* draw upkeep */
  count = city_gold_surplus(pCity);
  if (pCity->tax_total - count) {

    dest.x = pWindow->size.x + 423;
    dest.y = pWindow->size.y + 358
      + (16 - pIcons->pBIG_Coin_UpKeep->h) / 2;

    if (((pIcons->pBIG_Coin_UpKeep->w + 1) *
	 (pCity->tax_total - count)) > 110) {
      step = (110 - pIcons->pBIG_Coin_UpKeep->w) /
	  (pCity->tax_total - count - 1);
    } else {
      step = pIcons->pBIG_Coin_UpKeep->w + 1;
    }

    for (i = 0; i < (pCity->tax_total - count); i++) {
      SDL_BlitSurface(pIcons->pBIG_Coin_UpKeep, NULL, pWindow->dst,
		      &dest);
      dest.x -= step;
    }
  }
  /* ================================================================= */
  /* science label */
  my_snprintf(cBuf, sizeof(cBuf), _("Science: %d per turn"),
	      pCity->science_total);

  pStr->text = convert_to_utf16(cBuf);
  pStr->forecol.r = 12;
  pStr->forecol.g = 18;
  pStr->forecol.b = 108;

  pBuf = create_text_surf_from_str16(pStr);

  FREE(pStr->text);

  dest.x = pWindow->size.x + 200;
  dest.y = pWindow->size.y + 375 + (15 - pBuf->h) / 2;

  SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

  FREESURFACE(pBuf);

  /* draw colb */
  if (pCity->science_total) {

    dest.y =
	pWindow->size.y + 393 + (16 - pIcons->pBIG_Colb->h) / 2;
    dest.x = pWindow->size.x + 203;

    if ((pIcons->pBIG_Colb->w * pCity->science_total) > 235) {
      step = (235 - pIcons->pBIG_Colb->w) / (pCity->science_total - 1);
      if (step) {
	count = pCity->science_total;
      } else {
	step = 1;
	count = 222;
      }
    } else {
      step = pIcons->pBIG_Colb->w;
    }

    for (i = 0; i < count; i++) {
      SDL_BlitSurface(pIcons->pBIG_Colb, NULL, pWindow->dst, &dest);
      dest.x += step;
    }
  }
  /* ================================================================= */
  /* luxury label */
  my_snprintf(cBuf, sizeof(cBuf), _("Luxury: %d per turn"),
	      pCity->luxury_total);

  pStr->text = convert_to_utf16(cBuf);
  pStr->forecol.r = 12;
  pStr->forecol.g = 18;
  pStr->forecol.b = 108;

  pBuf = create_text_surf_from_str16(pStr);

  FREE(pStr->text);

  dest.x = pWindow->size.x + 200;
  dest.y = pWindow->size.y + 411 + (15 - pBuf->h) / 2;

  SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

  FREESURFACE(pBuf);

  /* draw luxury */
  if (pCity->luxury_total) {

    dest.y =
	pWindow->size.y + 428 + (16 - pIcons->pBIG_Luxury->h) / 2;
    dest.x = pWindow->size.x + 203;

    if ((pIcons->pBIG_Luxury->w * pCity->luxury_total) > 235) {
      step =
	  (235 - pIcons->pBIG_Luxury->w) / (pCity->luxury_total - 1);
    } else {
      step = pIcons->pBIG_Luxury->w;
    }

    for (i = 0; i < pCity->luxury_total; i++) {
      SDL_BlitSurface(pIcons->pBIG_Luxury, NULL, pWindow->dst, &dest);
      dest.x += step;
    }
  }
  /* ================================================================= */
  /* turns to grow label */
  count = city_turns_to_grow(pCity);
  if (count == 0) {
    my_snprintf(cBuf, sizeof(cBuf), _("City growth : blocked"));
  } else if (count == FC_INFINITY) {
    my_snprintf(cBuf, sizeof(cBuf), _("City growth : never"));
  } else if (count < 0) {
    /* turns until famine */
    my_snprintf(cBuf, sizeof(cBuf),
		_("City shrinks : %d %s"), abs(count),
		PL_("turn", "turns", abs(count)));
  } else {
    my_snprintf(cBuf, sizeof(cBuf),
		_("City growth : %d %s"), count,
		PL_("turn", "turns", count));
  }

  pStr->text = convert_to_utf16(cBuf);
  pStr->forecol = *get_game_colorRGB(COLOR_STD_GROUND);

  pBuf = create_text_surf_from_str16(pStr);

  FREE(pStr->text);

  dest.x = pWindow->size.x + 445 + (192 - pBuf->w) / 2;
  dest.y = pWindow->size.y + 227;

  SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

  FREESURFACE(pBuf);


  count = (city_granary_size(pCity->size)) / 10;

  if (count > 12) {
    step = (168 - pIcons->pBIG_Food->h) / (11 + count - 12);
    i = (count - 1) * step + 14;
    count = 12;
  } else {
    step = pIcons->pBIG_Food->h;
    i = count * step;
  }

  /* food stock */
  if (city_got_building(pCity, B_GRANARY)
      || city_affected_by_wonder(pCity, B_PYRAMIDS)) {
    /* with granary */
    /* stocks label */
    pStr->text = convert_to_utf16(_("Stock"));

    pBuf = create_text_surf_from_str16(pStr);

    FREE(pStr->text);

    dest.x = pWindow->size.x + 461 + (76 - pBuf->w) / 2;
    dest.y = pWindow->size.y + 258 - pBuf->h - 1;

    SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

    FREESURFACE(pBuf);

    /* granary label */
    pStr->text = convert_to_utf16(_("Granary"));

    pBuf = create_text_surf_from_str16(pStr);

    FREE(pStr->text);

    dest.x = pWindow->size.x + 549 + (76 - pBuf->w) / 2;
    /*dest.y = pWindow->size.y + 258 - pBuf->h - 1; */

    SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

    FREESURFACE(pBuf);


    pBuf = create_filled_surface(70 + 4, i + 4, SDL_SWSURFACE, &color);
    
    /* draw bcgd granary */
    dest.x = pWindow->size.x + 462;
    dest.y = pWindow->size.y + 260;
    SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

    putframe(pWindow->dst, dest.x - 1, dest.y - 1,
	     dest.x + pBuf->w, dest.y + pBuf->h, 0xff000000);

    /* draw bcgd stocks*/
    dest.x = pWindow->size.x + 550;
    dest.y = pWindow->size.y + 260;
    SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

    putframe(pWindow->dst, dest.x - 1, dest.y - 1,
	     dest.x + pBuf->w, dest.y + pBuf->h, 0xff000000);

    FREESURFACE(pBuf);

    /* draw stocks icons */
    cost = city_granary_size(pCity->size);
    if (pCity->food_stock + pCity->food_surplus > cost) {
      count = cost;
    } else {
      if(pCity->food_surplus < 0) {
        count = pCity->food_stock;
      } else {
	count = pCity->food_stock + pCity->food_surplus;
      }
    }
    cost /= 2;
    
    if(pCity->food_surplus < 0) {
      limit = pCity->food_stock + pCity->food_surplus;
      if(limit < 0) {
	limit = 0;
      }
    } else {
      limit = 0xffff;
    }
    
    dest.x += 2;
    dest.y += 2;
    i = 0;
    pBuf = pIcons->pBIG_Food;
    while (count && cost) {
      SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);
      dest.x += pBuf->w;
      count--;
      cost--;
      i++;
      if (dest.x > pWindow->size.x + 620) {
	dest.x = pWindow->size.x + 552;
	dest.y += step;
      }
      if(i > limit - 1) {
	pBuf = pIcons->pBIG_Food_Corr;
      } else {
        if(i > pCity->food_stock - 1)
        {
	  pBuf = pIcons->pBIG_Food_Surplus;
        }
      }
    }
    /* draw granary icons */
    dest.x = pWindow->size.x + 462 + 2;
    dest.y = pWindow->size.y + 260 + 2;
        
    while (count) {
      SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);
      dest.x += pBuf->w;
      count--;
      i++;
      if (dest.x > pWindow->size.x + 532) {
	dest.x = pWindow->size.x + 464;
	dest.y += step;
      }
      if(i > limit - 1) {
	pBuf = pIcons->pBIG_Food_Corr;
      } else {
        if(i > pCity->food_stock - 1)
        {
	  pBuf = pIcons->pBIG_Food_Surplus;
        }
      }
    }
    
  } else {
    /* without granary */
    /* stocks label */
    pStr->text = convert_to_utf16(_("Stock"));

    pBuf = create_text_surf_from_str16(pStr);

    FREE(pStr->text);

    dest.x = pWindow->size.x + 461 + (144 - pBuf->w) / 2;
    dest.y = pWindow->size.y + 258 - pBuf->h - 1;

    SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

    FREESURFACE(pBuf);

    pBuf = create_filled_surface(144, i + 4, SDL_SWSURFACE, &color);

    /* food stock */

    /* draw bcgd */
    dest.x = pWindow->size.x + 462;
    dest.y = pWindow->size.y + 260;
    SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

    putframe(pWindow->dst, dest.x - 1, dest.y - 1,
	     dest.x + pBuf->w, dest.y + pBuf->h, 0xff000000);

    FREESURFACE(pBuf);

    /* draw icons */
    cost = city_granary_size(pCity->size);
    if (pCity->food_stock + pCity->food_surplus > cost) {
      count = cost;
    } else {
      if(pCity->food_surplus < 0) {
        count = pCity->food_stock;
      } else {
	count = pCity->food_stock + pCity->food_surplus;
      }
    }
        
    if(pCity->food_surplus < 0) {
      limit = pCity->food_stock + pCity->food_surplus;
      if(limit < 0) {
	limit = 0;
      }
    } else {
      limit = 0xffff;
    }
        
    dest.x += 2;
    dest.y += 2;
    i = 0;
    pBuf = pIcons->pBIG_Food;
    while (count) {
      SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);
      dest.x += pBuf->w;
      count--;
      i++;
      if (dest.x > pWindow->size.x + 602) {
	dest.x = pWindow->size.x + 464;
	dest.y += step;
      }
      if(i > limit - 1) {
	pBuf = pIcons->pBIG_Food_Corr;
      } else {
        if(i > pCity->food_stock - 1)
        {
	  pBuf = pIcons->pBIG_Food_Surplus;
        }
      }
    }
  }
  /* ================================================================= */

  /* draw productions shields progress */
  if (pCity->is_building_unit) {
    struct unit_type *pUnit = get_unit_type(pCity->currently_building);
    cost = pUnit->build_cost;
    count = cost / 10;
        
    pStr->text = convert_to_utf16(pUnit->name);

    src = get_smaller_surface_rect(GET_SURF(pUnit->sprite));

    pBuf = create_text_surf_from_str16(pStr);

    dest.x = pWindow->size.x + 6 + (185 - (pBuf->w + src.w + 5)) / 2;
    dest.y = pWindow->size.y + 233;

    /* blit unit icon */
    SDL_BlitSurface(GET_SURF(pUnit->sprite), &src, pWindow->dst, &dest);

    dest.y += (src.h - pBuf->h) / 2;
    dest.x += src.w + 5;

  } else {
    struct impr_type *pImpr =
	get_improvement_type(pCity->currently_building);

    if (pCity->currently_building == B_CAPITAL) {

      if (get_wstate(pCityDlg->pBuy_Button) != WS_DISABLED) {
	set_wstate(pCityDlg->pBuy_Button, WS_DISABLED);
	redraw_widget(pCityDlg->pBuy_Button);
      }

      /* You can't see capitalization progres */
      count = 0;

    } else {

      if (!pCity->did_buy
	  && (get_wstate(pCityDlg->pBuy_Button) == WS_DISABLED)) {
	set_wstate(pCityDlg->pBuy_Button, WS_NORMAL);
	redraw_widget(pCityDlg->pBuy_Button);
      }

      cost = pImpr->build_cost;
      count = cost / 10;
      
    }

    pStr->text = convert_to_utf16(pImpr->name);
    pBuf = GET_SURF(pImpr->sprite);

    /* blit impr icon */
    dest.x = pWindow->size.x + 6 + (185 - pBuf->w) / 2;
    dest.y = pWindow->size.y + 230;
    SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

    dest.y += (pBuf->h + 2);

    pBuf = create_text_surf_from_str16(pStr);

    dest.x = pWindow->size.x + 6 + (185 - pBuf->w) / 2;
  }

  FREE(pStr->text);

  /* blit unit/impr name */
  SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

  FREESURFACE(pBuf);
  
  if (count) {
    if (count > 11) {
      step = (154 - pIcons->pBIG_Shield->h) / (10 + count - 11);
      
      if(!step) step = 1;
      
      i = (step * (count - 1)) + pIcons->pBIG_Shield->h;
    } else {
      step = pIcons->pBIG_Shield->h;
      i = count * step;
    }

    pBuf = create_filled_surface(142, i + 4, SDL_SWSURFACE, &color);

    /* draw sheild stock background */
    dest.x = pWindow->size.x + 29;
    dest.y = pWindow->size.y + 270;
    SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);
    putframe(pWindow->dst, dest.x - 1, dest.y - 1,
	     dest.x + pBuf->w, dest.y + pBuf->h, 0xff000000);

    /* draw production progres text */
    dest.y = pWindow->size.y + 270 + pBuf->h + 1;

    FREESURFACE(pBuf);
    if (pCity->shield_surplus) {
      div_t result =
	  div((cost - pCity->shield_stock), pCity->shield_surplus);

      if (result.rem) {
	count = result.quot + 1;
      } else {
	count = result.quot;
      }

      if (count > 0) {
	my_snprintf(cBuf, sizeof(cBuf), _("(%d/%d) will finish in %d %s"),
		    pCity->shield_stock, cost, count,
		    PL_("turn", "turns", count));
      } else {
	my_snprintf(cBuf, sizeof(cBuf), _("(%d/%d) finished!"),
		    pCity->shield_stock, cost);
      }
    } else {
      my_snprintf(cBuf, sizeof(cBuf), _("(%d/%d) blocked!"),
		  pCity->shield_stock, cost);
    }


    pStr->text = convert_to_utf16(cBuf);
    pStr->forecol.r = 238;
    pStr->forecol.g = 156;
    pStr->forecol.b = 7;

    pBuf = create_text_surf_from_str16(pStr);

    dest.x = pWindow->size.x + 6 + (185 - pBuf->w) / 2;

    SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

    FREESTRING16(pStr);
    FREESURFACE(pBuf);
    
    /* draw sheild stock */
    if (pCity->shield_stock + pCity->shield_surplus <= cost) {
      count = pCity->shield_stock + pCity->shield_surplus;
    } else {
      count = cost;
    }
    dest.x = pWindow->size.x + 29 + 2;
    dest.y = pWindow->size.y + 270 + 2;
    i = 0;
    
    pBuf = pIcons->pBIG_Shield;
    while (count) {
      SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);
      dest.x += pBuf->w;
      count--;
      if (dest.x > pWindow->size.x + 170) {
	dest.x = pWindow->size.x + 31;
	dest.y += step;
      }
      i++;
      if(i > pCity->shield_stock - 1) {
	pBuf = pIcons->pBIG_Shield_Surplus;
      }
    }
    
    
  }


  /* count != 0 */
  /* ==================================================== */
  /* Draw Citizens */
  count = pCity->ppl_happy[4] + pCity->ppl_content[4] +
      pCity->ppl_unhappy[4] + pCity->ppl_angry[4] +
      pCity->ppl_elvis + pCity->ppl_scientist + pCity->ppl_taxman;

  pBuf = GET_SURF(get_citizen_sprite(CITIZEN_ELVIS, 0, pCity));
  if (count > 13) {
    step = (400 - pBuf->w) / (12 + count - 13);
  } else {
    step = pBuf->w;
  }

  dest.y =
      pWindow->size.y + 26 + (42 - pBuf->h) / 2;
  dest.x = pWindow->size.x + 227;

  if (pCity->ppl_happy[4]) {
    for (i = 0; i < pCity->ppl_happy[4]; i++) {
      pBuf = GET_SURF(get_citizen_sprite(CITIZEN_HAPPY, i, pCity));
      SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);
      dest.x += step;
    }
  }

  if (pCity->ppl_content[4]) {
    for (i = 0; i < pCity->ppl_content[4]; i++) {
      pBuf = GET_SURF(get_citizen_sprite(CITIZEN_CONTENT, i, pCity));
      SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);
      dest.x += step;
    }
  }

  if (pCity->ppl_unhappy[4]) {
    for (i = 0; i < pCity->ppl_unhappy[4]; i++) {
      pBuf = GET_SURF(get_citizen_sprite(CITIZEN_UNHAPPY, i, pCity));
      SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);
      dest.x += step;
    }
  }

  if (pCity->ppl_angry[4]) {
    for (i = 0; i < pCity->ppl_angry[4]; i++) {
      pBuf = GET_SURF(get_citizen_sprite(CITIZEN_ANGRY, i, pCity));
      SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);
      dest.x += step;
    }
  }

  FREE(pCityDlg->specs_area[0]);
  FREE(pCityDlg->specs_area[1]);
  FREE(pCityDlg->specs_area[2]);

  if (pCity->ppl_elvis) {
    pBuf = GET_SURF(get_citizen_sprite(CITIZEN_ELVIS, 0, pCity));
    pCityDlg->specs_area[0] = MALLOC(sizeof(SDL_Rect));
    pCityDlg->specs_area[0]->x = dest.x;
    pCityDlg->specs_area[0]->y = dest.y;
    pCityDlg->specs_area[0]->w = pBuf->w;
    pCityDlg->specs_area[0]->h = pBuf->h;
    for (i = 0; i < pCity->ppl_elvis; i++) {
      SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);
      dest.x += step;
      pCityDlg->specs_area[0]->w += step;
    }
    pCityDlg->specs_area[0]->w -= step;
  }

  if (pCity->ppl_taxman) {
    pBuf = GET_SURF(get_citizen_sprite(CITIZEN_TAXMAN, 0, pCity));
    pCityDlg->specs_area[1] = MALLOC(sizeof(SDL_Rect));
    pCityDlg->specs_area[1]->x = dest.x;
    pCityDlg->specs_area[1]->y = dest.y;
    pCityDlg->specs_area[1]->w = pBuf->w;
    pCityDlg->specs_area[1]->h = pBuf->h;
    for (i = 0; i < pCity->ppl_taxman; i++) {
      SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);
      dest.x += step;
      pCityDlg->specs_area[1]->w += step;
    }
    pCityDlg->specs_area[1]->w -= step;
  }

  if (pCity->ppl_scientist) {
    pBuf = GET_SURF(get_citizen_sprite(CITIZEN_SCIENTIST, 0, pCity));
    pCityDlg->specs_area[2] = MALLOC(sizeof(SDL_Rect));
    pCityDlg->specs_area[2]->x = dest.x;
    pCityDlg->specs_area[2]->y = dest.y;
    pCityDlg->specs_area[2]->w = pBuf->w;
    pCityDlg->specs_area[2]->h = pBuf->h;
    for (i = 0; i < pCity->ppl_scientist; i++) {
      SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);
      dest.x += step;
      pCityDlg->specs_area[2]->w += step;
    }
    pCityDlg->specs_area[2]->w -= step;
  }

  /* ==================================================== */


  switch (pCityDlg->state) {
  case INFO_PAGE:
    redraw_info_city_dialog(pWindow, pCity);
    break;

  case HAPPINESS_PAGE:
    redraw_happyness_city_dialog(pWindow, pCity);
    break;

  case ARMY_PAGE:
    redraw_army_city_dialog(pWindow, pCity);
    break;

  case SUPPORTED_UNITS_PAGE:
    redraw_supported_units_city_dialog(pWindow, pCity);
    break;

  case MISC_PAGE:
    redraw_misc_city_dialog(pWindow, pCity);
    break;

  default:
    break;

  }
  
  sdl_dirty_rect(pWindow->size);
}

/* ============================================================== */

/**************************************************************************
  ...
**************************************************************************/
static void rebuild_imprm_list(struct city *pCity)
{
  int high, turns = 0;
  struct GUI *pWindow = pCityDlg->pEndCityWidgetList;
  struct GUI *pBegin = pCityDlg->pEndCityImprWidgetList;
  struct GUI *pBuf = NULL;
  SDL_Surface *pLogo = NULL;
  SDL_String16 *pStr = NULL;
  struct impr_type *pImpr = NULL;
  bool created = pBegin != NULL;
  
  /* free old list */
  if (pBegin) {
    pBegin = pBegin->next;
    del_group_of_widgets_from_gui_list(pCityDlg->pBeginCityImprWidgetList,
				       pCityDlg->pEndCityImprWidgetList);
    pCityDlg->pEndCityImprWidgetList = NULL;
    FREE(pCityDlg->pVscroll);
  }

  /* allock new */
  built_impr_iterate(pCity, imp) {

    pImpr = get_improvement_type(imp);

    pStr = create_str16_from_char(get_impr_name_ex(pCity, imp), 11);
    pStr->forecol = *get_game_colorRGB(COLOR_STD_WHITE);
    pStr->backcol.unused = 128;/* 50% transp */

    pStr->style |= TTF_STYLE_BOLD;

    pLogo = ResizeSurface(GET_SURF(pImpr->sprite), 18, 11, 1);

    pBuf =
	create_iconlabel(pLogo, pWindow->dst, pStr,
			 (WF_FREE_THEME | WF_DRAW_THEME_TRANSPARENT));

    pBuf->size.x = pWindow->size.x + 428;
    pBuf->size.y = pWindow->size.y + 88 + turns * 13;

    pBuf->size.w = 182;
    pBuf->string16->style &= ~SF_CENTER;
    pBuf->action = sell_imprvm_dlg_callback;

    if (!pImpr->is_wonder) {
      set_wstate(pBuf, WS_NORMAL);
    }

    if(created) {
      pBuf->ID = MAX_ID - imp - 3000;
      pBuf->prev = pBegin->prev;
      pBegin->prev->next = pBuf;
      pBuf->next = pBegin;
      pBegin->prev = pBuf;
      pBegin = pBuf;
    } else {
      add_to_gui_list(MAX_ID - imp - 3000, pBuf);
    }
    
    turns++;

    if (turns > 10) {
      set_wflag(pBuf, WF_HIDDEN);
    }

    if (!pCityDlg->pEndCityImprWidgetList) {
      pCityDlg->pEndCityImprWidgetList = pBuf;
    }

  } built_impr_iterate_end;

  if (turns > 10) {
    /* create up button */
    pBuf = create_themeicon_button(pTheme->UP_Icon, pWindow->dst, NULL, 0);


    pBuf->size.x = pWindow->size.x + 629 - pBuf->size.w;
    pBuf->size.y = pWindow->size.y + 89;
    high = pBuf->size.h;
    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);

    pBuf->action = up_city_dlg_building_callback;
    set_wstate(pBuf, WS_NORMAL);

    if(created) {
      pBuf->ID = ID_CITY_DLG_BUILDING_UP_BUTTON;
      pBuf->prev = pBegin->prev;
      pBegin->prev->next = pBuf;
      pBuf->next = pBegin;
      pBegin->prev = pBuf;
      pBegin = pBuf;
    } else {
      add_to_gui_list(ID_CITY_DLG_BUILDING_UP_BUTTON, pBuf);
    }
    /*pLog->min = start_y + pLog->up_button->size.h + 3; */

    /* create down button */
    pBuf = create_themeicon_button(pTheme->DOWN_Icon, pWindow->dst, NULL, 0);

    pBuf->size.x = pWindow->size.x + 629 - pBuf->size.w;
    pBuf->size.y = pWindow->size.y + 219 - pBuf->size.h;

    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);

    pBuf->action = down_city_dlg_building_callback;
    set_wstate(pBuf, WS_NORMAL);

    if(created) {
      pBuf->ID = ID_CITY_DLG_BUILDING_DOWN_BUTTON;
      pBuf->prev = pBegin->prev;
      pBegin->prev->next = pBuf;
      pBuf->next = pBegin;
      pBegin->prev = pBuf;
      pBegin = pBuf;
    } else {
      add_to_gui_list(ID_CITY_DLG_BUILDING_DOWN_BUTTON, pBuf);
    }

    /* create vsrollbar */
    pBuf = create_vertical(pTheme->Vertic, pWindow->dst,
					    50, WF_DRAW_THEME_TRANSPARENT);

    pBuf->size.x = pWindow->size.x + 631 - pBuf->size.w;
    pBuf->size.y = pWindow->size.y + 89 + high;

    /* 
       impv. scrollbar size
       10 - impr. sean in window.
       94 - max size of scroolbar active area.
     */
    pBuf->size.h = ((float) 10 / turns) * 94;
    set_wstate(pBuf, WS_NORMAL);

    pBuf->action = vscroll_city_dlg_building_callback;

    if(created) {
      pBuf->ID = ID_CITY_DLG_BUILDING_VSCROLLBAR;
      pBuf->prev = pBegin->prev;
      pBegin->prev->next = pBuf;
      pBuf->next = pBegin;
      pBegin->prev = pBuf;
      pBegin = pBuf;
    } else {
      add_to_gui_list(ID_CITY_DLG_BUILDING_VSCROLLBAR, pBuf);
    }


    pCityDlg->pVscroll = MALLOC(sizeof(struct ScrollBar));
    pCityDlg->pVscroll->active = 10;
    pCityDlg->pVscroll->count = turns;
    pCityDlg->pVscroll->min = pWindow->size.y + 89 + high;
    pCityDlg->pVscroll->max = pWindow->size.y + 219 - high;
  }

  if (pBuf) {
    pCityDlg->pBeginActiveCityImprWidgetList =
	pCityDlg->pEndCityImprWidgetList;
    pCityDlg->pBeginCityImprWidgetList = pBuf;
  }
}

/**************************************************************************
  ...
**************************************************************************/
static void rebuild_citydlg_title_str(struct GUI *pWindow,
				      struct city *pCity)
{
  char cBuf[512];

  my_snprintf(cBuf, sizeof(cBuf),
	      _("City of %s (Population %s citizens)"), pCity->name,
	      population_to_text(city_population(pCity)));

  if (city_unhappy(pCity)) {
    mystrlcat(cBuf, _(" - DISORDER"), sizeof(cBuf));
  } else {
    if (city_celebrating(pCity)) {
      mystrlcat(cBuf, _(" - celebrating"), sizeof(cBuf));
    } else {
      if (city_happy(pCity)) {
	mystrlcat(cBuf, _(" - happy"), sizeof(cBuf));
      }
    }
  }

  FREE(pWindow->string16->text);
  pWindow->string16->text = convert_to_utf16(cBuf);
}


/* ========================= Public ================================== */


/**************************************************************************
  ...
**************************************************************************/
void refresh_city_dlg_background(void)
{
  refresh_widget_background(pCityDlg->pEndCityWidgetList);
}

/**************************************************************************
  ...
**************************************************************************/
void undraw_city_dialog(void)
{
  SDL_Rect dst = pCityDlg->pEndCityWidgetList->size;
  SDL_BlitSurface(pCityDlg->pEndCityWidgetList->gfx, NULL,
			  pCityDlg->pEndCityWidgetList->dst, &dst);
}

/**************************************************************************
  get city dlg. position and size.
**************************************************************************/
SDL_Rect *get_citydlg_rect(void)
{
  if (pCityDlg) {
    return &(pCityDlg->pEndCityWidgetList->size);
  }
  return NULL;
}

/* ========================= NATIVE ================================== */

/**************************************************************************
  Pop up (or bring to the front) a dialog for the given city.  It may or
  may not be modal.
**************************************************************************/
void popup_city_dialog(struct city *pCity, bool make_modal)
{
  struct GUI *pWindow = NULL, *pBuf = NULL;
  SDL_Surface *pLogo = NULL;
  SDL_String16 *pStr = NULL;
  int cs;

  if (pCityDlg) {
    return;
  }

  SDL_Client_Flags |= CF_CITY_DIALOG_IS_OPEN;

  update_menus();

  pCityDlg = MALLOC(sizeof(struct city_dialog));

  pCityDlg->pCity = pCity;

  pStr = create_string16(NULL, 12);
  pStr->style |= TTF_STYLE_BOLD;

  pWindow = create_window(get_locked_buffer(), pStr, 640, 480, 0);
  unlock_buffer();
  
  rebuild_citydlg_title_str(pWindow, pCity);

  pWindow->size.x = (pWindow->dst->w - 640) / 2;
  pWindow->size.y = (pWindow->dst->h - 480) / 2;
  pWindow->size.w = 640;
  pWindow->size.h = 480;
  pWindow->action = city_dlg_callback;
  set_wstate(pWindow, WS_NORMAL);

  /* create window background */
  pLogo = get_logo_gfx();

  if (resize_window(pWindow, pLogo, NULL, pWindow->size.w, pWindow->size.h)) {
    FREESURFACE(pLogo);
  }

  if (pWindow->dst->format->Amask) {
    pLogo = SDL_DisplayFormatAlpha(pWindow->theme);
  } else {
    pLogo = SDL_DisplayFormat(pWindow->theme);
  }
  FREESURFACE(pWindow->theme);
  pWindow->theme = pLogo;
  
  pLogo = get_city_gfx();
  SDL_BlitSurface(pLogo, NULL, pWindow->theme, NULL);
  FREESURFACE(pLogo);

  SDL_SetAlpha(pWindow->theme, 0x0, 0x0);

  pCityDlg->pEndCityWidgetList = pWindow;
  add_to_gui_list(ID_CITY_DLG_WINDOW, pWindow);

  /* ============================================================= */

  pBuf = create_themeicon(pTheme->CANCEL_Icon, pWindow->dst,
			  (WF_WIDGET_HAS_INFO_LABEL |
			   WF_DRAW_THEME_TRANSPARENT));

  pBuf->string16 = create_str16_from_char(_("Cancel"), 12);

  pBuf->action = exit_city_dlg_callback;

  pBuf->size.x = pWindow->size.x + pWindow->size.w - pBuf->size.w - 10;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 10;
  pBuf->key = SDLK_ESCAPE;
  set_wstate(pBuf, WS_NORMAL);

  add_to_gui_list(ID_CITY_DLG_EXIT_BUTTON, pBuf);
  /* ---------------------------------- */

  pLogo = create_city_map(pCity);
  pBuf =
      create_themelabel(ResizeSurface(pLogo, 192, 96, 1),
		      pWindow->dst, NULL, 192, 96, 0);
  FREESURFACE(pLogo);

  pCityDlg->pResource_Map = pBuf;

  pBuf->action = resource_map_city_dlg_callback;
  set_wstate(pBuf, WS_NORMAL);

  pBuf->size.x =
      pWindow->size.x + (pWindow->size.w - pBuf->size.w) / 2 - 1;
  pBuf->size.y = pWindow->size.y + 87 + (134 - pBuf->size.h) / 2;

  add_to_gui_list(ID_CITY_DLG_RESOURCE_MAP, pBuf);

  /* Buttons */
  pBuf = create_themeicon(pTheme->INFO_Icon, pWindow->dst,
			  (WF_WIDGET_HAS_INFO_LABEL |
			   WF_DRAW_THEME_TRANSPARENT));

  pBuf->string16 = create_str16_from_char(_("Information panel"), 12);

  pBuf->action = info_city_dlg_callback;

  pBuf->size.x =
      pWindow->size.x + pWindow->size.w - 2 * pBuf->size.w - 5 - 10;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 10;

  set_wstate(pBuf, WS_NORMAL);

  add_to_gui_list(ID_CITY_DLG_INFO_BUTTON, pBuf);
  /* -------- */
  pBuf = create_themeicon(pTheme->Happy_Icon, pWindow->dst,
			  (WF_WIDGET_HAS_INFO_LABEL |
			   WF_DRAW_THEME_TRANSPARENT));

  pBuf->string16 = create_str16_from_char(_("Happiness panel"), 12);

  pBuf->action = happy_city_dlg_callback;

  pBuf->size.x =
      pWindow->size.x + pWindow->size.w - 3 * pBuf->size.w - 10 - 10;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 10;

  set_wstate(pBuf, WS_NORMAL);

  add_to_gui_list(ID_CITY_DLG_HAPPY_BUTTON, pBuf);
  /* -------- */
  pBuf = create_themeicon(pTheme->Army_Icon, pWindow->dst,
			  (WF_WIDGET_HAS_INFO_LABEL |
			   WF_DRAW_THEME_TRANSPARENT));

  pBuf->string16 = create_str16_from_char(_("Garrison panel"), 12);

  pBuf->action = army_city_dlg_callback;

  pBuf->size.x =
      pWindow->size.x + pWindow->size.w - 4 * pBuf->size.w - 10 - 10;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 10;

  set_wstate(pBuf, WS_NORMAL);

  add_to_gui_list(ID_CITY_DLG_ARMY_BUTTON, pBuf);
  /* -------- */
  pBuf = create_themeicon(pTheme->Support_Icon, pWindow->dst,
			  (WF_WIDGET_HAS_INFO_LABEL |
			   WF_DRAW_THEME_TRANSPARENT));

  pBuf->string16 = create_str16_from_char(_("Maintenance panel"), 12);

  pBuf->action = supported_unit_city_dlg_callback;

  pBuf->size.x =
      pWindow->size.x + pWindow->size.w - 5 * pBuf->size.w - 10 - 10;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 10;

  set_wstate(pBuf, WS_NORMAL);

  add_to_gui_list(ID_CITY_DLG_SUPPORT_BUTTON, pBuf);
  /* -------- */
  pBuf = create_themeicon(pTheme->Options_Icon, pWindow->dst,
			  (WF_WIDGET_HAS_INFO_LABEL |
			   WF_DRAW_THEME_TRANSPARENT));

  pBuf->string16 = create_str16_from_char(_("Options panel"), 12);

  pBuf->action = options_city_dlg_callback;

  pBuf->size.x =
      pWindow->size.x + pWindow->size.w - 6 * pBuf->size.w - 10 - 10;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 10;

  set_wstate(pBuf, WS_NORMAL);

  add_to_gui_list(ID_CITY_DLG_OPTIONS_BUTTON, pBuf);
  /* -------- */
  pBuf = create_themeicon(pTheme->PROD_Icon, pWindow->dst,
			  (WF_WIDGET_HAS_INFO_LABEL |
			   WF_DRAW_THEME_TRANSPARENT));

  pBuf->string16 = create_str16_from_char(_("Change Production"), 12);

  pBuf->action = change_prod_dlg_callback;

  pBuf->size.x = pWindow->size.x + 10;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 10;

  set_wstate(pBuf, WS_NORMAL);

  add_to_gui_list(ID_CITY_DLG_CHANGE_PROD_BUTTON, pBuf);
  /* -------- */
  pBuf = create_themeicon(pTheme->Buy_PROD_Icon, pWindow->dst,
			  (WF_WIDGET_HAS_INFO_LABEL |
			   WF_DRAW_THEME_TRANSPARENT));

  pBuf->string16 = create_str16_from_char(_("Hurry production"), 12);

  pBuf->action = buy_prod_city_dlg_callback;

  pBuf->size.x = pWindow->size.x + 10 + (pBuf->size.w + 2);
  pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 10;

  pCityDlg->pBuy_Button = pBuf;

  if (!pCity->did_buy) {
    set_wstate(pBuf, WS_NORMAL);
  }

  add_to_gui_list(ID_CITY_DLG_PROD_BUY_BUTTON, pBuf);
  /* -------- */
  pBuf = create_themeicon(pTheme->QPROD_Icon, pWindow->dst,
			  (WF_WIDGET_HAS_INFO_LABEL |
			   WF_DRAW_THEME_TRANSPARENT));

  pBuf->string16 = create_str16_from_char(_("Worklist"), 12);

  pBuf->action = work_list_city_dlg_callback;

  pBuf->size.x = pWindow->size.x + 10 + 2 * (pBuf->size.w + 2);
  pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 10;
/*	
	set_wstate( pBuf , WS_NORMAL );
*/
  add_to_gui_list(ID_CITY_DLG_PROD_Q_BUTTON, pBuf);
  /* -------- */
  pBuf = create_themeicon(pTheme->CMA_Icon, pWindow->dst,
			  (WF_WIDGET_HAS_INFO_LABEL |
			   WF_DRAW_THEME_TRANSPARENT));

  pBuf->string16 = create_str16_from_char(_("Citizen Management Agent"), 12);

  pBuf->action = cma_city_dlg_callback;

  pBuf->size.x = pWindow->size.x + 10 + (pBuf->size.w + 2) * 3;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 10;
/*	
	set_wstate( pBuf , WS_NORMAL );
*/
  add_to_gui_list(ID_CITY_DLG_CMA_BUTTON, pBuf);
  
  pCityDlg->pBeginCityWidgetList = pBuf;
  /* ===================================================== */

  rebuild_imprm_list(pCity);

  /* ===================================================== */
  /* -------- */
  pBuf = create_themeicon(pTheme->L_ARROW_Icon, pWindow->dst,
			  (WF_WIDGET_HAS_INFO_LABEL |
			   WF_DRAW_THEME_TRANSPARENT));

  pBuf->string16 = create_str16_from_char(_("Prev city"), 12);

  pBuf->action = next_prev_city_dlg_callback;

  pBuf->size.x = pWindow->size.x + 220 - pBuf->size.w - 5;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 3;

  set_wstate(pBuf, WS_NORMAL);

  add_to_gui_list(ID_CITY_DLG_PREV_BUTTON, pBuf);
  /* -------- */
  pBuf = create_themeicon(pTheme->R_ARROW_Icon, pWindow->dst,
			  (WF_WIDGET_HAS_INFO_LABEL |
			   WF_DRAW_THEME_TRANSPARENT));

  pBuf->string16 = create_str16_from_char(_("Next city"), 12);

  pBuf->action = next_prev_city_dlg_callback;

  pBuf->size.x = pWindow->size.x + 420 + 5;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 3;

  set_wstate(pBuf, WS_NORMAL);

  add_to_gui_list(ID_CITY_DLG_NEXT_BUTTON, pBuf);
  /* -------- */
  pBuf = create_edit_from_chars(NULL, pWindow->dst, pCity->name,
				10, 200, WF_DRAW_THEME_TRANSPARENT);

  pBuf->action = new_name_city_dlg_callback;

  pBuf->size.x = pWindow->size.x + (pWindow->size.w - pBuf->size.w) / 2;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 6;

  set_wstate(pBuf, WS_NORMAL);
  pCityDlg->pCity_Name_Edit = pBuf;

  add_to_gui_list(ID_CITY_DLG_NAME_EDIT, pBuf);


  pCityDlg->pBeginCityWidgetList = pBuf;
  /* check if Citizen Icon style was loaded */

  cs = get_city_style(pCity);

  if (cs != pIcons->style) {
    reload_citizens_icons( cs , pCity );
  }

  /* ===================================================== */
  if ((city_unhappy(pCity) || city_celebrating(pCity)
       || city_happy(pCity))) {
    SDL_Client_Flags |= CF_CITY_STATUS_SPECIAL;
  }
  /* ===================================================== */

  redraw_city_dialog(pCity);
  flush_dirty();
}

void popdown_city_dialog(struct city *pCity)
{
  if (city_dialog_is_open(pCity)) {
    SDL_Surface *pDest = pCityDlg->pEndCityWidgetList->dst;
    if (pDest == Main.gui) {
      blit_entire_src(pCityDlg->pEndCityWidgetList->gfx,
			    pDest, pCityDlg->pEndCityWidgetList->size.x,
				    pCityDlg->pEndCityWidgetList->size.y);
    } else {
      lock_buffer(pDest);
      remove_locked_buffer();
    }
    
    sdl_dirty_rect(pCityDlg->pEndCityWidgetList->size);
    
    del_city_dialog();
	  
    SDL_Client_Flags &= ~CF_CITY_STATUS_SPECIAL;
    update_menus();
    flush_dirty();
  }
}

/**************************************************************************
  Close the dialog for the given city.
**************************************************************************/
void popdown_all_city_dialogs(void)
{
    
  if (pCityDlg) {

    if (pCityDlg->pEndCityImprWidgetList) {
      del_group_of_widgets_from_gui_list(pCityDlg->
					 pBeginCityImprWidgetList,
					 pCityDlg->pEndCityImprWidgetList);

      FREE(pCityDlg->pVscroll);
    }

    if (pChange_Prod_Dlg) {
      del_group_of_widgets_from_gui_list(pChange_Prod_Dlg->pBeginWidgetList,
				     pChange_Prod_Dlg->pEndWidgetList);
  
      FREE(pChange_Prod_Dlg->pScroll);
      FREE( pChange_Prod_Dlg );
    }

    if (pHurry_Prod_Dlg)
    {
      del_group_of_widgets_from_gui_list(pHurry_Prod_Dlg->pBeginWidgetList,
			      		 pHurry_Prod_Dlg->pEndWidgetList);

      FREE( pHurry_Prod_Dlg );
    }
    
    if (pCityDlg->pEndCityPanelWidgetList) {
      del_group_of_widgets_from_gui_list(pCityDlg->
					 pBeginCityPanelWidgetList,
					 pCityDlg->
					 pEndCityPanelWidgetList);

      FREE(pCityDlg->pPanelVscroll);
    }

    if (pCityDlg->pEndCityMenuWidgetList) {
      del_group_of_widgets_from_gui_list(pCityDlg->
					 pBeginCityMenuWidgetList,
					 pCityDlg->pEndCityMenuWidgetList);
    }

    popdown_window_group_dialog(pCityDlg->pBeginCityWidgetList,
				pCityDlg->pEndCityWidgetList);
    FREE(pCityDlg->specs_area[0]);
    FREE(pCityDlg->specs_area[1]);
    FREE(pCityDlg->specs_area[2]);
    FREE(pCityDlg->pVscroll);
    FREE(pCityDlg);
    SDL_Client_Flags &= ~CF_CITY_DIALOG_IS_OPEN;
    SDL_Client_Flags &= ~CF_CITY_STATUS_SPECIAL;
  }
  flush_dirty();
}

/**************************************************************************
  Refresh (update) all data for the given city's dialog.
**************************************************************************/
void refresh_city_dialog(struct city *pCity)
{
  if (city_dialog_is_open(pCity)) {
    redraw_city_dialog(pCityDlg->pCity);
    flush_dirty();
  }
}

/**************************************************************************
  Update city dialogs when the given unit's status changes.  This
  typically means updating both the unit's home city (if any) and the
  city in which it is present (if any).
**************************************************************************/
void refresh_unit_city_dialogs(struct unit *pUnit)
{

  struct city *pCity_sup = find_city_by_id(pUnit->homecity);
  struct city *pCity_pre = map_get_city(pUnit->x, pUnit->y);

  if (pCityDlg && ((pCityDlg->pCity == pCity_sup)
		   || (pCityDlg->pCity == pCity_pre))) {
    if (pCityDlg->pEndCityPanelWidgetList) {
      del_group_of_widgets_from_gui_list(pCityDlg->
					 pBeginCityPanelWidgetList,
					 pCityDlg->
					 pEndCityPanelWidgetList);


      pCityDlg->pEndCityPanelWidgetList = NULL;
      FREE(pCityDlg->pPanelVscroll);
    }

    redraw_city_dialog(pCityDlg->pCity);
    flush_dirty();
  }

}

/**************************************************************************
  Return whether the dialog for the given city is open.
**************************************************************************/
bool city_dialog_is_open(struct city *pCity)
{
  return (pCityDlg && (pCityDlg->pCity == pCity));
}
