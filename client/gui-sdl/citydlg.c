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
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL/SDL.h>

#include "city.h"
#include "fcintl.h"
#include "game.h"
#include "map.h"
#include "improvement.h"
#include "log.h"

#include "gui_mem.h"

#include "packets.h"
#include "player.h"
#include "shared.h"
#include "support.h"

#include "civclient.h"
#include "cityrep.h"
#include "cma_fe.h"
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
#include "climisc.h"
#include "climap.h"
#include "wldlg.h"
#include "gui_tilespec.h"
#include "text.h"

#include "optiondlg.h"
#include "menu.h"

/*#include "cityicon.ico"*/

#include "citydlg.h"

/* ============================================================= */
#define SCALLED_TILE_WIDTH	48
#define SCALLED_TILE_HEIGHT	24

#define NUM_UNITS_SHOWN		 3

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
  struct ADVANCED_DLG *pImprv;
  
  /* Penel group list */
  struct ADVANCED_DLG *pPanel;
    
  /* Menu imprv. dlg. */
  struct GUI *pBeginCityMenuWidgetList;
  struct GUI *pEndCityMenuWidgetList;

  /* shortcuts */
  struct GUI *pAdd_Point;
  struct GUI *pBuy_Button;
  struct GUI *pResource_Map;
  struct GUI *pCity_Name_Edit;

  SDL_Rect specs_area[3];	/* active area of specialist
				   0 - elvis
				   1 - taxman
				   2 - scientists
				   change when pressed on this area */
  bool specs[3];
  
  bool lock;
} *pCityDlg = NULL;

static struct SMALL_DLG *pHurry_Prod_Dlg = NULL;

static void popdown_hurry_production_dialog(void);
static void disable_city_dlg_widgets(void);
static void redraw_city_dialog(struct city *pCity);
static void rebuild_imprm_list(struct city *pCity);
static void rebuild_citydlg_title_str(struct GUI *pWindow, struct city *pCity);

/* ======================================================================= */

/**************************************************************************
  Destroy City Menu Dlg but not undraw.
**************************************************************************/
static void del_city_menu_dlg(bool enable)
{
  if (pCityDlg->pEndCityMenuWidgetList) {
    del_group_of_widgets_from_gui_list(pCityDlg->pBeginCityMenuWidgetList,
				       pCityDlg->pEndCityMenuWidgetList);
    pCityDlg->pEndCityMenuWidgetList = NULL;
  }
  if (enable) {
    /* enable city dlg */
    enable_city_dlg_widgets();
  }
}

/**************************************************************************
  Destroy City Dlg but not undraw.
**************************************************************************/
static void del_city_dialog(void)
{
  if (pCityDlg) {

    if (pCityDlg->pImprv->pEndWidgetList) {
      del_group_of_widgets_from_gui_list(pCityDlg->pImprv->pBeginWidgetList,
					 pCityDlg->pImprv->pEndWidgetList);
    }
    FREE(pCityDlg->pImprv->pScroll);
    FREE(pCityDlg->pImprv);

    if (pCityDlg->pPanel) {
      del_group_of_widgets_from_gui_list(pCityDlg->pPanel->pBeginWidgetList,
					 pCityDlg->pPanel->pEndWidgetList);
      FREE(pCityDlg->pPanel->pScroll);
      FREE(pCityDlg->pPanel);
    }
        
    if (pHurry_Prod_Dlg)
    {
      del_group_of_widgets_from_gui_list(pHurry_Prod_Dlg->pBeginWidgetList,
			      		 pHurry_Prod_Dlg->pEndWidgetList);

      FREE(pHurry_Prod_Dlg);
    }
    
    free_city_units_lists();
    del_city_menu_dlg(FALSE);
    del_group_of_widgets_from_gui_list(pCityDlg->pBeginCityWidgetList,
				       pCityDlg->pEndCityWidgetList);
    FREE(pCityDlg);
  }
}

/**************************************************************************
  Main Citu Dlg. window callback.
  Here was implemented change specialist ( Elvis, Taxman, Scientist ) code. 
**************************************************************************/
static int city_dlg_callback(struct GUI *pWindow)
{  
  if (!cma_is_city_under_agent(pCityDlg->pCity, NULL)
     && city_owner(pCityDlg->pCity) == game.player_ptr) {
       
    /* check elvis area */
    if (pCityDlg->specs[0]
       && is_in_rect_area(Main.event.motion.x, Main.event.motion.y,
					pCityDlg->specs_area[0])) {
      city_change_specialist(pCityDlg->pCity, SP_ELVIS, SP_TAXMAN);
      return -1;
    }

    /* check TAXMANs area */
    if (pCityDlg->specs[1]
       && is_in_rect_area(Main.event.motion.x, Main.event.motion.y,
					pCityDlg->specs_area[1])) {
      city_change_specialist(pCityDlg->pCity, SP_TAXMAN, SP_SCIENTIST);
      return -1;
    }

    /* check SCIENTISTs area */
    if (pCityDlg->specs[2]
       && is_in_rect_area(Main.event.motion.x, Main.event.motion.y,
					pCityDlg->specs_area[2])) {
      city_change_specialist(pCityDlg->pCity, SP_SCIENTIST, SP_ELVIS);
      return -1;
    }
    
  }
  
  if(!pCityDlg->lock &&
    	sellect_window_group_dialog(pCityDlg->pBeginCityWidgetList, pWindow)) {
    flush_rect(pWindow->size);
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
  lock_buffer(pButton->dst);
  popdown_window_group_dialog(pCityDlg->pBeginCityMenuWidgetList,
			      pCityDlg->pEndCityMenuWidgetList);
  pCityDlg->pEndCityMenuWidgetList = NULL;
  unlock_buffer();
  
  /* enable city dlg */
  enable_city_dlg_widgets();
  flush_dirty();
  return -1;
}

/**************************************************************************
  activate unit and del unit order dlg. widget group.
  update screen is unused becouse common code call here 
  redraw_city_dialog(pCityDlg->pCity);
**************************************************************************/
static int activate_units_orders_city_dlg_callback(struct GUI *pButton)
{
  struct unit *pUnit = pButton->data.unit;

  del_city_menu_dlg(TRUE);
  if(pUnit) {
    set_unit_focus(pUnit);
  }
  return -1;
}

/**************************************************************************
  activate unit and popdow city dlg. + center on unit.
**************************************************************************/
static int activate_and_exit_units_orders_city_dlg_callback(struct GUI *pButton)
{
  struct unit *pUnit = pButton->data.unit;

  if(pUnit) {
    SDL_Surface *pDest = pCityDlg->pEndCityWidgetList->dst;
    if (pDest == Main.gui) {
      blit_entire_src(pCityDlg->pEndCityWidgetList->gfx,
			    pDest, pCityDlg->pEndCityWidgetList->size.x,
				    pCityDlg->pEndCityWidgetList->size.y);
    } else {
      lock_buffer(pDest);
      remove_locked_buffer();
    }
    del_city_dialog();
    center_tile_mapcanvas(pUnit->x, pUnit->y);
    set_unit_focus(pUnit);
    flush_dirty();
  }

  return -1;
}

/**************************************************************************
  sentry unit and del unit order dlg. widget group.
  update screen is unused becouse common code call here 
  redraw_city_dialog(pCityDlg->pCity);
**************************************************************************/
static int sentry_units_orders_city_dlg_callback(struct GUI *pButton)
{
  struct unit *pUnit = pButton->data.unit;

  del_city_menu_dlg(TRUE);
  if(pUnit) {
    request_unit_sentry(pUnit);
  }
  return -1;
}

/**************************************************************************
  fortify unit and del unit order dlg. widget group.
  update screen is unused becouse common code call here 
  redraw_city_dialog(pCityDlg->pCity);
**************************************************************************/
static int fortify_units_orders_city_dlg_callback(struct GUI *pButton)
{
  struct unit *pUnit = pButton->data.unit;

  del_city_menu_dlg(TRUE);
  if(pUnit) {
    request_unit_fortify(pUnit);
  }
  return -1;
}

/**************************************************************************
  disband unit and del unit order dlg. widget group.
  update screen is unused becouse common code call here 
  redraw_city_dialog(pCityDlg->pCity);
**************************************************************************/
static int disband_units_orders_city_dlg_callback(struct GUI *pButton)
{
  struct unit *pUnit = pButton->data.unit;

  free_city_units_lists();
  del_city_menu_dlg(TRUE);

  /* ugly hack becouse this free unit widget list*/
  /* FIX ME: add remove from list support */
  pCityDlg->state = INFO_PAGE;

  if(pUnit) {
    request_unit_disband(pUnit);
  }

  return -1;
}

/**************************************************************************
  homecity unit and del unit order dlg. widget group.
  update screen is unused becouse common code call here 
  redraw_city_dialog(pCityDlg->pCity);
**************************************************************************/
static int homecity_units_orders_city_dlg_callback(struct GUI *pButton)
{
  struct unit *pUnit = pButton->data.unit;

  del_city_menu_dlg(TRUE);
  if(pUnit) {
    request_unit_change_homecity(pUnit);
  }

  return -1;
}

/**************************************************************************
  upgrade unit and del unit order dlg. widget group.
  update screen is unused becouse common code call here 
  redraw_city_dialog(pCityDlg->pCity);
**************************************************************************/
static int upgrade_units_orders_city_dlg_callback(struct GUI *pButton)
{
  struct unit *pUnit = pButton->data.unit;
    
  lock_buffer(pButton->dst);
  popdown_window_group_dialog(pCityDlg->pBeginCityMenuWidgetList,
			      pCityDlg->pEndCityMenuWidgetList);
  pCityDlg->pEndCityMenuWidgetList = NULL;
  unlock_buffer();
  popup_unit_upgrade_dlg(pUnit, TRUE);
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

  pUnit = player_find_unit_by_id(game.player_ptr, MAX_ID - pButton->ID);
  
  if(!pUnit || !can_client_issue_orders()) {
    return -1;
  }
  
  if(Main.event.button.button == SDL_BUTTON_RIGHT) {
    SDL_Surface *pDest = pWindow->dst;
    
    if (pDest == Main.gui) {
      blit_entire_src(pWindow->gfx, pDest, pWindow->size.x, pWindow->size.y);
    } else {
      lock_buffer(pDest);
      remove_locked_buffer();
    }
    del_city_dialog();
    center_tile_mapcanvas(pUnit->x, pUnit->y);
    set_unit_focus(pUnit);
    flush_dirty();
    return -1;
  }
    
  /* Disable city dlg */
  unsellect_widget_action();
  disable_city_dlg_widgets();

  ww = 0;
  hh = 0;
  i = 0;
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
  set_wstate(pWindow, FC_WS_NORMAL);
  add_to_gui_list(ID_REVOLUTION_DLG_WINDOW, pWindow);
  pCityDlg->pEndCityMenuWidgetList = pWindow;
  /* ----- */

  my_snprintf(cBuf, sizeof(cBuf), "%s", unit_description(pUnit));
  pStr = create_str16_from_char(cBuf, 12);
  pStr->style |= (TTF_STYLE_BOLD|SF_CENTER);
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
  pBuf->data = pButton->data;
  set_wstate(pBuf, FC_WS_NORMAL);
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
  pBuf->data = pButton->data;
  set_wstate(pBuf, FC_WS_NORMAL);
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
    pBuf->data = pButton->data;
    pBuf->action = sentry_units_orders_city_dlg_callback;
    if (pUnit->activity != ACTIVITY_SENTRY
	&& can_unit_do_activity(pUnit, ACTIVITY_SENTRY)) {
      set_wstate(pBuf, FC_WS_NORMAL);
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
    pBuf->data = pButton->data;
    pBuf->action = fortify_units_orders_city_dlg_callback;
    if (pUnit->activity != ACTIVITY_FORTIFYING
	&& can_unit_do_activity(pUnit, ACTIVITY_FORTIFYING)) {
      set_wstate(pBuf, FC_WS_NORMAL);
    }
    add_to_gui_list(pButton->ID, pBuf);
  }
  /* ----- */
  
  /* Disband unit */
  pBuf = create_icon_button_from_chars(NULL, pWindow->dst,
				  _("Disband unit"), 12, 0);
  i++;
  ww = MAX(ww, pBuf->size.w);
  hh = MAX(hh, pBuf->size.h);
  clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
  pBuf->data = pButton->data;
  pBuf->action = disband_units_orders_city_dlg_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  add_to_gui_list(pButton->ID, pBuf);
  /* ----- */

  if (pCityDlg->state == ARMY_PAGE) {
    if (pUnit->homecity != pCityDlg->pCity->id) {
      /* Make new Homecity */
      pBuf = create_icon_button_from_chars(NULL, pWindow->dst, 
    					_("Make new homecity"), 12, 0);
      i++;
      ww = MAX(ww, pBuf->size.w);
      hh = MAX(hh, pBuf->size.h);
      clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
      pBuf->data = pButton->data;
      pBuf->action = homecity_units_orders_city_dlg_callback;
      set_wstate(pBuf, FC_WS_NORMAL);
      add_to_gui_list(pButton->ID, pBuf);
    }
    /* ----- */
    
    if (can_upgrade_unittype(game.player_ptr, pUnit->type) != -1) {
      /* Upgrade unit */
      pBuf = create_icon_button_from_chars(NULL, pWindow->dst,
					    _("Upgrade unit"), 12, 0);
      i++;
      ww = MAX(ww, pBuf->size.w);
      hh = MAX(hh, pBuf->size.h);
      clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
      pBuf->data = pButton->data;
      pBuf->action = upgrade_units_orders_city_dlg_callback;
      set_wstate(pBuf, FC_WS_NORMAL);
      add_to_gui_list(pButton->ID, pBuf);
    }
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
  set_wstate(pBuf, FC_WS_NORMAL);
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
  
  /* label */
  pBuf = pWindow->prev;
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
  create unit icon with support icons.
**************************************************************************/
static SDL_Surface *create_unit_surface(struct unit *pUnit, bool support)
{
  int i, step;
  SDL_Rect dest;
  SDL_Surface *pSurf =
  	create_surf(UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT, SDL_SWSURFACE);

  put_unit_pixmap_draw(pUnit, pSurf, 0, 3);

  if (pSurf->w > 64) {
    float zoom = 64.0 / pSurf->w;
    SDL_Surface *pZoomed = ZoomSurface(pSurf, zoom, zoom, 1);
    FREESURFACE(pSurf);
    pSurf = pZoomed;
  }
  
  if (support) {
    i = pUnit->upkeep + pUnit->upkeep_food +
	pUnit->upkeep_gold + pUnit->unhappiness;

    if (i * pIcons->pFood->w > pSurf->w / 2) {
      step = (pSurf->w / 2 - pIcons->pFood->w) / (i - 1);
    } else {
      step = pIcons->pFood->w;
    }

    dest.y = pSurf->h - pIcons->pFood->h - 2;
    dest.x = pSurf->w / 8;

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
  struct GUI *pBuf = NULL;
  struct GUI *pEnd = NULL;
  struct GUI *pWindow = pCityDlg->pEndCityWidgetList;
  struct city *pHome_City;
  struct unit_type *pUType;
  SDL_Surface *pSurf;
  SDL_String16 *pStr;
  char cBuf[256];
  
  i = 0;

  unit_list_iterate(*pList, pUnit) {
        
    pUType = get_unit_type(pUnit->type);
    pHome_City = find_city_by_id(pUnit->homecity);
    my_snprintf(cBuf, sizeof(cBuf), "%s (%d,%d,%d)%s\n%s\n(%d/%d)\n%s",
		pUType->name, pUType->attack_strength,
		pUType->defense_strength, pUType->move_rate / SINGLE_MOVE,
                (pUnit->veteran ? _("\nveteran") : ""),
                unit_activity_text(pUnit),
		pUnit->hp, pUType->hp,
		pHome_City ? pHome_City->name : _("None"));
    
    if (pCityDlg->state == SUPPORTED_UNITS_PAGE) {
      int pcity_near_dist;
      struct city *pNear_City = get_nearest_city(pUnit, &pcity_near_dist);

      sz_strlcat(cBuf, "\n");
      sz_strlcat(cBuf, get_nearest_city_text(pNear_City, pcity_near_dist));
      pSurf = create_unit_surface(pUnit, 1);
    } else {
      pSurf = create_unit_surface(pUnit, 0);
    }
        
    pStr = create_str16_from_char(cBuf, 10);
    pStr->style |= SF_CENTER;
    
    pBuf = create_icon2(pSurf, pWindow->dst,
	(WF_FREE_THEME | WF_DRAW_THEME_TRANSPARENT | WF_WIDGET_HAS_INFO_LABEL));
	
    pBuf->string16 = pStr;
    pBuf->data.unit = pUnit;
    add_to_gui_list(MAX_ID - pUnit->id, pBuf);

    if (!pEnd) {
      pEnd = pBuf;
    }
    
    if (++i > NUM_UNITS_SHOWN * NUM_UNITS_SHOWN) {
      set_wflag(pBuf, WF_HIDDEN);
    }
  
    if (pCityDlg->pCity->owner == game.player_idx) {    
      set_wstate(pBuf, FC_WS_NORMAL);
    }
    
    pBuf->action = units_orders_city_dlg_callback;

  } unit_list_iterate_end;
  
  pCityDlg->pPanel = MALLOC(sizeof(struct ADVANCED_DLG));
  pCityDlg->pPanel->pEndWidgetList = pEnd;
  pCityDlg->pPanel->pEndActiveWidgetList = pEnd;
  pCityDlg->pPanel->pBeginWidgetList = pBuf;
  pCityDlg->pPanel->pBeginActiveWidgetList = pBuf;
  pCityDlg->pPanel->pActiveWidgetList = pEnd;
  
  setup_vertical_widgets_position(NUM_UNITS_SHOWN,
	pWindow->size.x + 7,
	pWindow->size.y + WINDOW_TILE_HIGH + 40,
	  0, 0, pCityDlg->pPanel->pBeginActiveWidgetList,
			  pCityDlg->pPanel->pEndActiveWidgetList);
  
  if (i > NUM_UNITS_SHOWN * NUM_UNITS_SHOWN) {
    
    pCityDlg->pPanel->pScroll = MALLOC(sizeof(struct ScrollBar));
    pCityDlg->pPanel->pScroll->active = NUM_UNITS_SHOWN;
    pCityDlg->pPanel->pScroll->step = NUM_UNITS_SHOWN;
    pCityDlg->pPanel->pScroll->count = i;
    
    create_vertical_scrollbar(pCityDlg->pPanel,
	NUM_UNITS_SHOWN, NUM_UNITS_SHOWN, FALSE, TRUE);
    
    /* create up button */
    pBuf = pCityDlg->pPanel->pScroll->pUp_Left_Button;
    pBuf->size.x = pWindow->size.x + 6;
    pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 20;
    pBuf->size.w = 103;
        
    /* create down button */
    pBuf = pCityDlg->pPanel->pScroll->pDown_Right_Button;
    pBuf->size.x = pWindow->size.x + 6 + 104;
    pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 20;
    pBuf->size.w = 103;
    
  }
    
}

/**************************************************************************
  free city present/supported units panel list.
**************************************************************************/
void free_city_units_lists(void)
{
  if (pCityDlg && pCityDlg->pPanel) {
    del_group_of_widgets_from_gui_list(pCityDlg->pPanel->pBeginWidgetList,
					 pCityDlg->pPanel->pEndWidgetList);
    FREE(pCityDlg->pPanel->pScroll);
    FREE(pCityDlg->pPanel);
  }
}

/**************************************************************************
  change to present units panel.
**************************************************************************/
static int army_city_dlg_callback(struct GUI *pButton)
{
  
  if (pCityDlg->state != ARMY_PAGE) {
    free_city_units_lists();
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
    free_city_units_lists();
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
    free_city_units_lists();
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
    free_city_units_lists();
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
    pWidget->gfx = get_citizen_surface(CITIZEN_TAXMAN, 0);
    pWidget->ID = MAX_ID - 0x40;
    redraw_ibutton(pWidget);
    flush_rect(pWidget->size);
    break;
  case 0x40:
    new &= 0x1f;
    pWidget->gfx = get_citizen_surface(CITIZEN_ELVIS, 0);
    pWidget->ID = MAX_ID - 0x60;
    redraw_ibutton(pWidget);
    flush_rect(pWidget->size);
    break;
  case 0x60:
    new |= 0x20;
    pWidget->gfx = get_citizen_surface(CITIZEN_SCIENTIST, 0);
    pWidget->ID = MAX_ID - 0x20;
    redraw_ibutton(pWidget);
    flush_rect(pWidget->size);
    break;
  }

  dsend_packet_city_options_req(&aconnection, pCityDlg->pCity->id, new);

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

  my_snprintf(cBuf, sizeof(cBuf), "%s\n%s" , _("Auto attack vs"), _("land units"));
  pStr = create_str16_from_char(cBuf, 10);
  pStr->fgcol = *get_game_colorRGB(COLOR_STD_WHITE);
  pStr->style |= TTF_STYLE_BOLD;
  pBuf =
      create_textcheckbox(pWindow->dst, pCity->city_options & 0x01, pStr,
			  WF_DRAW_THEME_TRANSPARENT);
  pBuf->size.x = pWindow->size.x + 10;
  pBuf->size.y = pWindow->size.y + 40;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->action = misc_panel_city_dlg_callback;
  add_to_gui_list(MAX_ID - 1, pBuf);
  pCityDlg->pPanel = MALLOC(sizeof(struct ADVANCED_DLG));
  pCityDlg->pPanel->pEndWidgetList = pBuf;
  /* ---- */
  
  my_snprintf(cBuf, sizeof(cBuf), "%s\n%s" , _("Auto attack vs"), _("sea units"));
  pStr = create_str16_from_char(cBuf, 10);
  pStr->style |= TTF_STYLE_BOLD;
  pStr->fgcol = *get_game_colorRGB(COLOR_STD_WHITE);
  pBuf =
      create_textcheckbox(pWindow->dst, pCity->city_options & 0x02, pStr,
			  WF_DRAW_THEME_TRANSPARENT);
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->action = misc_panel_city_dlg_callback;
  add_to_gui_list(MAX_ID - 2, pBuf);
  pBuf->size.x = pBuf->next->size.x;
  pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
  /* ----- */
  
  my_snprintf(cBuf, sizeof(cBuf), "%s\n%s" , _("Auto attack vs"), _("heli units"));
  pStr = create_str16_from_char(cBuf, 10);
  pStr->style |= TTF_STYLE_BOLD;
  pStr->fgcol = *get_game_colorRGB(COLOR_STD_WHITE);
  pBuf =
      create_textcheckbox(pWindow->dst, pCity->city_options & 0x04, pStr,
			  WF_DRAW_THEME_TRANSPARENT);
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->action = misc_panel_city_dlg_callback;
  add_to_gui_list(MAX_ID - 4, pBuf);
  pBuf->size.x = pBuf->next->size.x;
  pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
  /* ----- */
  
  my_snprintf(cBuf, sizeof(cBuf), "%s\n%s" , _("Auto attack vs"), _("air units"));
  pStr = create_str16_from_char(cBuf, 10);
  pStr->style |= TTF_STYLE_BOLD;
  pStr->fgcol = *get_game_colorRGB(COLOR_STD_WHITE);

  pBuf =
      create_textcheckbox(pWindow->dst, pCity->city_options & 0x08, pStr,
			  WF_DRAW_THEME_TRANSPARENT);
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->action = misc_panel_city_dlg_callback;
  add_to_gui_list(MAX_ID - 8, pBuf);
  pBuf->size.x = pBuf->next->size.x;
  pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
  /* ----- */
  
  my_snprintf(cBuf, sizeof(cBuf),
	      _("Disband if build\nsettler at size 1"));
  pStr = create_str16_from_char(cBuf, 10);
  pStr->style |= TTF_STYLE_BOLD;
  pStr->fgcol = *get_game_colorRGB(COLOR_STD_WHITE);

  pBuf =
      create_textcheckbox(pWindow->dst, pCity->city_options & 0x10, pStr,
			  WF_DRAW_THEME_TRANSPARENT);
  set_wstate(pBuf, FC_WS_NORMAL);
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
    pSurf = get_citizen_surface(CITIZEN_SCIENTIST, 0);
    pBuf = create_icon_button(pSurf, pWindow->dst, pStr, WF_ICON_CENTER_RIGHT);
    add_to_gui_list(MAX_ID - 0x20, pBuf);
  } else {
    if (pCity->city_options & 0x40) {
      pSurf = get_citizen_surface(CITIZEN_TAXMAN, 0);
      pBuf = create_icon_button(pSurf, pWindow->dst,
				      pStr, WF_ICON_CENTER_RIGHT);
      add_to_gui_list(MAX_ID - 0x40, pBuf);
    } else {
      pSurf = get_citizen_surface(CITIZEN_ELVIS, 0);
      pBuf = create_icon_button(pSurf, pWindow->dst,
				pStr, WF_ICON_CENTER_RIGHT);
      add_to_gui_list(MAX_ID - 0x60, pBuf);
    }
  }

  pBuf->size.w = 199;
  pBuf->action = misc_panel_city_dlg_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);

  pBuf->size.x = pBuf->next->size.x;
  pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h + 5;
  pCityDlg->pPanel->pBeginWidgetList = pBuf;
}

/**************************************************************************
  change to city options panel.
**************************************************************************/
static int options_city_dlg_callback(struct GUI *pButton)
{

  if (pCityDlg->state != MISC_PAGE) {
    free_city_units_lists();
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
  ...
**************************************************************************/
static int cma_city_dlg_callback(struct GUI *pButton)
{
  disable_city_dlg_widgets();
  popup_city_cma_dialog(pCityDlg->pCity);
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
  city_buy_production(pButton->data.city);
  
  if (pCityDlg)
  {
    del_group_of_widgets_from_gui_list(pHurry_Prod_Dlg->pBeginWidgetList,
			      		pHurry_Prod_Dlg->pEndWidgetList);
    FREE(pHurry_Prod_Dlg);
    /* enable city dlg */
    enable_city_dlg_widgets();
    unlock_buffer();
    set_wstate(pCityDlg->pBuy_Button, FC_WS_DISABLED);
  } else {
    popdown_hurry_production_dialog();
  }
    
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
  return std_move_window_group_callback(pHurry_Prod_Dlg->pBeginWidgetList,
								pWindow);
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
  SDL_Surface *pText;
  SDL_Rect dst;
  
  if (pHurry_Prod_Dlg) {
    return;
  }
  
  pHurry_Prod_Dlg = MALLOC(sizeof(struct SMALL_DLG));
  
  if (pCity->is_building_unit) {
    name = get_unit_type(pCity->currently_building)->name;
  } else {
    name = get_impr_name_ex(pCity, pCity->currently_building);
  }

  value = city_buy_cost(pCity);
  if(!pCity->did_buy) {
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
  } else {
    my_snprintf(cBuf, sizeof(cBuf),
		_("Sorry, You have already bought here in this turn"));
  }

  hh = WINDOW_TILE_HIGH + 2;
  pStr = create_str16_from_char(_("Buy It?"), 12);
  pStr->style |= TTF_STYLE_BOLD;
  pWindow = create_window(pDest, pStr, 100, 100, 0);
  pWindow->action = hurry_production_window_callback;
  set_wstate(pWindow, FC_WS_NORMAL);
  ww = pWindow->size.w;
  pHurry_Prod_Dlg->pEndWidgetList = pWindow;
  add_to_gui_list(ID_WINDOW, pWindow);
  /* ============================================================= */
  
  /* label */
  pStr = create_str16_from_char(cBuf, 10);
  pStr->style |= (TTF_STYLE_BOLD|SF_CENTER);
  pStr->fgcol.r = 255;
  pStr->fgcol.g = 255;
  /* pStr->fgcol.b = 255; */

  pText = create_text_surf_from_str16(pStr);
  FREESTRING16(pStr);
  ww = MAX(ww , pText->w);
  hh += pText->h + 5;

  pBuf = create_themeicon_button_from_chars(pTheme->CANCEL_Icon,
			    pWindow->dst, _("No"), 12, 0);

  clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
  pBuf->action = cancel_buy_prod_city_dlg_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  hh += pBuf->size.h;

  add_to_gui_list(ID_BUTTON, pBuf);

  if (!pCity->did_buy && game.player_ptr->economic.gold >= value) {
    pBuf = create_themeicon_button_from_chars(pTheme->OK_Icon, pWindow->dst,
					      _("Yes"), 12, 0);

    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
    pBuf->action = ok_buy_prod_city_dlg_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
    pBuf->data.city = pCity;
    pBuf->key = SDLK_RETURN;
    add_to_gui_list(ID_BUTTON, pBuf);
    pBuf->size.w = MAX(pBuf->next->size.w, pBuf->size.w);
    pBuf->next->size.w = pBuf->size.w;
    ww = MAX(ww , 2 * pBuf->size.w + 20);
  }
  
  pHurry_Prod_Dlg->pBeginWidgetList = pBuf;
  
  /* setup window size and start position */
  ww += 10;

  pBuf = pWindow->prev;
  
  if (city_dialog_is_open(pCity))
  {
    pWindow->size.x = pCityDlg->pBuy_Button->size.x;
    pWindow->size.y = pCityDlg->pBuy_Button->size.y - (hh + FRAME_WH + 5);
  } else {
    if(is_city_report_open()) {
      assert(pSellected_Widget != NULL);
      if (pSellected_Widget->size.x + NORMAL_TILE_WIDTH +
	 		ww + DOUBLE_FRAME_WH > pWindow->dst->w)
      {
        pWindow->size.x = pSellected_Widget->size.x - ww - DOUBLE_FRAME_WH;
      } else {
        pWindow->size.x = pSellected_Widget->size.x + NORMAL_TILE_WIDTH;
      }
    
      pWindow->size.y = pSellected_Widget->size.y +
      		(pSellected_Widget->size.h - (hh + FRAME_WH + 5)) / 2;
      if (pWindow->size.y + (hh + FRAME_WH + 5) > pWindow->dst->h)
      {
	pWindow->size.y = pWindow->dst->h - (hh + FRAME_WH + 5) - 1;
      } else {
        if (pWindow->size.y < 0) {
	  pWindow->size.y = 0;
	}
      }
    } else {
      put_window_near_map_tile(pWindow,
  		ww + DOUBLE_FRAME_WH, hh + FRAME_WH + 5, pCity->x , pCity->y);
    }
    
  }

  resize_window(pWindow, NULL,
		get_game_colorRGB(COLOR_STD_BACKGROUND_BROWN),
		ww + DOUBLE_FRAME_WH, hh + FRAME_WH + 5);

  /* setup rest of widgets */
  /* label */
  dst.x = FRAME_WH + (pWindow->size.w - DOUBLE_FRAME_WH - pText->w) / 2;
  dst.y = WINDOW_TILE_HIGH + 2;
  SDL_BlitSurface(pText, NULL, pWindow->theme, &dst);
  dst.y += pText->h + 5;
  FREESURFACE(pText);
  
  /* no */
  pBuf = pWindow->prev;
  pBuf->size.y = pWindow->size.y + dst.y;
  
  if (!pCity->did_buy && game.player_ptr->economic.gold >= value) {
    /* yes */
    pBuf = pBuf->prev;
    pBuf->size.x = pWindow->size.x +
	    (pWindow->size.w - DOUBLE_FRAME_WH - (2 * pBuf->size.w + 20)) / 2;
    pBuf->size.y = pWindow->size.y + dst.y;
    
    /* no */
    pBuf->next->size.x = pBuf->size.x + pBuf->size.w + 20;
  } else {
    /* no */
    pBuf->size.x = pWindow->size.x +
		    pWindow->size.w - FRAME_WH - pBuf->size.w - 10;
  }
  /* ================================================== */
  /* redraw */
  redraw_group(pHurry_Prod_Dlg->pBeginWidgetList, pWindow, 0);
  sdl_dirty_rect(pWindow->size);
  flush_dirty();
}

/* =======================================================================*/
/* ========================== CHANGE PRODUCTION ==========================*/
/* =======================================================================*/

/**************************************************************************
  Popup the change production dialog.
**************************************************************************/
static int change_prod_dlg_callback(struct GUI *pButton)
{
  redraw_icon(pButton);
  flush_rect(pButton->size);

  disable_city_dlg_widgets();
  popup_worklist_editor(pCityDlg->pCity, &pCityDlg->pCity->worklist);
  return -1;
}

/* =======================================================================*/
/* =========================== SELL IMPROVMENTS ==========================*/
/* =======================================================================*/

/**************************************************************************
  Popdown Sell Imprv. Dlg. and exit without sell.
**************************************************************************/
static int sell_imprvm_dlg_cancel_callback(struct GUI *pCancel_Button)
{
  lock_buffer(pCancel_Button->dst);
  popdown_window_group_dialog(pCityDlg->pBeginCityMenuWidgetList,
			      pCityDlg->pEndCityMenuWidgetList);
  unlock_buffer();
  pCityDlg->pEndCityMenuWidgetList = NULL;
  enable_city_dlg_widgets();
  flush_dirty();
  return -1;
}

/**************************************************************************
  Popdown Sell Imprv. Dlg. and exit with sell.
**************************************************************************/
static int sell_imprvm_dlg_ok_callback(struct GUI *pOK_Button)
{
  struct GUI *pTmp = (struct GUI *)pOK_Button->data.ptr;

  city_sell_improvement(pCityDlg->pCity, MAX_ID - 3000 - pTmp->ID);
  
  /* popdown, we don't redraw and flush becouse this is make by redraw city dlg.
     when response from server come */
  del_group_of_widgets_from_gui_list(pCityDlg->pBeginCityMenuWidgetList,
				     pCityDlg->pEndCityMenuWidgetList);

  pCityDlg->pEndCityMenuWidgetList = NULL;

  /* del imprv from widget list */
  del_widget_from_vertical_scroll_widget_list(pCityDlg->pImprv, pTmp);
  
  enable_city_dlg_widgets();

  if (pCityDlg->pImprv->pEndWidgetList) {
    set_group_state(pCityDlg->pImprv->pBeginActiveWidgetList,
		    pCityDlg->pImprv->pEndActiveWidgetList, FC_WS_DISABLED);
  }

  return -1;
}

/**************************************************************************
  Popup Sell Imprvm. Dlg.
**************************************************************************/
static int sell_imprvm_dlg_callback(struct GUI *pImpr)
{
  struct SDL_String16 *pStr = NULL;
  struct GUI *pLabel = pImpr;
  struct GUI *pWindow = NULL;
  struct GUI *pCancel_Button = NULL;
  struct GUI *pOK_Button = NULL;
  char cBuf[80];
  int ww;
  int id;

  unsellect_widget_action();
  disable_city_dlg_widgets();

  /* create ok button */
  pOK_Button = create_themeicon_button_from_chars(
  		pTheme->Small_OK_Icon, pImpr->dst, _("Sell"), 10,  0);

  pOK_Button->data.ptr = (void *)pLabel;
  clear_wflag(pOK_Button, WF_DRAW_FRAME_AROUND_WIDGET);

  /* create cancel button */
  pCancel_Button =
      create_themeicon_button_from_chars(pTheme->Small_CANCEL_Icon,
      			pImpr->dst, _("Cancel"), 10, 0);

  clear_wflag(pCancel_Button, WF_DRAW_FRAME_AROUND_WIDGET);

  id = MAX_ID - 3000 - pImpr->ID;

  my_snprintf(cBuf, sizeof(cBuf), _("Sell %s for %d gold?"),
	      get_impr_name_ex(pCityDlg->pCity, id),
	      impr_sell_gold(id));


  /* create text label */
  pStr = create_str16_from_char(cBuf, 10);
  pStr->style |= (TTF_STYLE_BOLD|SF_CENTER);
  pStr->fgcol.r = 255;
  pStr->fgcol.g = 255;
  /*pStr->fgcol.b = 255; */
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
  set_wstate(pCancel_Button, FC_WS_NORMAL);
  set_wstate(pOK_Button, FC_WS_NORMAL);
  /*set_wstate( pWindow, FC_WS_NORMAL ); */

  /* add widgets to main list */
  pCityDlg->pEndCityMenuWidgetList = pWindow;
  add_to_gui_list(ID_WINDOW, pWindow);
  add_to_gui_list(ID_LABEL, pLabel);
  add_to_gui_list(ID_BUTTON, pCancel_Button);
  add_to_gui_list(ID_BUTTON, pOK_Button);
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
void enable_city_dlg_widgets(void)
{
  if (pCityDlg) {
    set_group_state(pCityDlg->pBeginCityWidgetList,
		  pCityDlg->pEndCityWidgetList->prev, FC_WS_NORMAL);
  
    if (pCityDlg->pImprv->pEndWidgetList) {
      if (pCityDlg->pImprv->pScroll) {
        set_wstate(pCityDlg->pImprv->pScroll->pScrollBar, FC_WS_NORMAL);	/* vscroll */
        set_wstate(pCityDlg->pImprv->pScroll->pUp_Left_Button, FC_WS_NORMAL); /* up */
        set_wstate(pCityDlg->pImprv->pScroll->pDown_Right_Button, FC_WS_NORMAL); /* down */
      }

      if (pCityDlg->pCity->did_sell) {
        set_group_state(pCityDlg->pImprv->pBeginActiveWidgetList,
		      pCityDlg->pImprv->pEndActiveWidgetList, FC_WS_DISABLED);
      } else {
        struct GUI *pTmpWidget = pCityDlg->pImprv->pEndActiveWidgetList;

        while (TRUE) {
	  if (get_improvement_type(MAX_ID - 3000 - pTmpWidget->ID)->is_wonder) {
	    set_wstate(pTmpWidget, FC_WS_DISABLED);
	  } else {
	    set_wstate(pTmpWidget, FC_WS_NORMAL);
	  }

	  if (pTmpWidget == pCityDlg->pImprv->pBeginActiveWidgetList) {
	    break;
	  }

	  pTmpWidget = pTmpWidget->prev;

        }				/* while */
      }
    }
  
    if (pCityDlg->pCity->did_buy && pCityDlg->pBuy_Button) {
      set_wstate(pCityDlg->pBuy_Button, FC_WS_DISABLED);
    }

    if (pCityDlg->pPanel) {
      set_group_state(pCityDlg->pPanel->pBeginWidgetList,
		    pCityDlg->pPanel->pEndWidgetList, FC_WS_NORMAL);
    }

    if (cma_is_city_under_agent(pCityDlg->pCity, NULL)) {
      set_wstate(pCityDlg->pResource_Map, FC_WS_DISABLED);
    }
  
    pCityDlg->lock = FALSE;
  }
}

/**************************************************************************
  ...
**************************************************************************/
static void disable_city_dlg_widgets(void)
{
  if (pCityDlg->pPanel) {
    set_group_state(pCityDlg->pPanel->pBeginWidgetList,
		    pCityDlg->pPanel->pEndWidgetList, FC_WS_DISABLED);
  }


  if (pCityDlg->pImprv->pEndWidgetList) {
    set_group_state(pCityDlg->pImprv->pBeginWidgetList,
		    pCityDlg->pImprv->pEndWidgetList, FC_WS_DISABLED);
  }

  set_group_state(pCityDlg->pBeginCityWidgetList,
		  pCityDlg->pEndCityWidgetList->prev, FC_WS_DISABLED);
  pCityDlg->lock = TRUE;
}
/* ======================================================================== */

//#define NO_ISO

#ifdef NO_ISO
/**************************************************************************
This converts a city coordinate position to citymap canvas coordinates
(either isometric or overhead).  It should be in cityview.c instead.
**************************************************************************/
static bool sdl_city_to_canvas_pos(int *canvas_x, int *canvas_y, int city_x, int city_y)
{
  if (is_isometric) {
    /*
     * The top-left corner is in the center of tile (-2, 2).  However,
     * we're looking for the top-left corner of the tile, so we
     * subtract off half a tile in each direction.  For a more
     * rigorous example, see map_pos_to_canvas_pos().
     */
    int iso_x = (city_x - city_y) - (-4);
    int iso_y = (city_x + city_y) - (0);

    *canvas_x = (iso_x - 1) * SCALLED_TILE_WIDTH / 2;
    *canvas_y = (iso_y - 1) * SCALLED_TILE_HEIGHT / 2;
  } else {
    *canvas_x = city_x * SCALLED_TILE_WIDTH;
    *canvas_y = city_y * SCALLED_TILE_HEIGHT;
  }

  if (!is_valid_city_coords(city_x, city_y)) {
    assert(FALSE);
    return FALSE;
  }
  return TRUE;
}

/**************************************************************************
This converts a citymap canvas position to a city coordinate position
(either isometric or overhead).  It should be in cityview.c instead.
**************************************************************************/
static bool sdl_canvas_to_city_pos(int *city_x, int *city_y, int canvas_x, int canvas_y)
{
  int orig_canvas_x = canvas_x, orig_canvas_y = canvas_y;

  if (is_isometric) {
    const int W = SCALLED_TILE_WIDTH, H = SCALLED_TILE_HEIGHT;

    /* Shift the tile right so the top corner of tile (-2,2) is at
       canvas position (0,0). */
    canvas_y += H / 2;

    /* Perform a pi/4 rotation, with scaling.  See canvas_pos_to_map_pos
       for a full explanation. */
    *city_x = DIVIDE(canvas_x * H + canvas_y * W, W * H);
    *city_y = DIVIDE(canvas_y * W - canvas_x * H, W * H);

    /* Add on the offset of the top-left corner to get the final
     * coordinates (like in canvas_to_map_pos). */
    *city_x -= 2;
    *city_y += 2;
  } else {
    *city_x = canvas_x / SCALLED_TILE_WIDTH;
    *city_y = canvas_y / SCALLED_TILE_HEIGHT;
  }
  freelog(LOG_DEBUG, "canvas_to_city_pos(pos=(%d,%d))=(%d,%d)",
	  orig_canvas_x, orig_canvas_y, *city_x, *city_y);

  return is_valid_city_coords(*city_x, *city_y);
}
#else

/**************************************************************************
  city resource map: calculate screen position to city map position.

  col = ( map_x - X0 ) / W + ( map_y - Y0 ) / H;
  row = ( map_y - Y0 ) / H - ( map_x - X0 ) / W;

  map_x = mouse_x_pos_on_screen - resource_map_x_pos_on_screen;
  map_y = mouse_y_pos_on_screen - resource_map_y_pos_on_screen;
  W - resource_tile_width (zoomed) = 48;
  H - resource_tile_hight (zoomed) = 24;
  X0 - x_pos of first tile (0,0) + W / 2 on resource map = 1.5 * W + W / 2 = 72;
  Y0 - y_pos of first tile (0,0) resource map = 0;
**************************************************************************/
static bool get_citymap_cr(Sint16 map_x, Sint16 map_y, int *pCol, int *pRow)
{
  float a = (float) (map_x) / SCALLED_TILE_WIDTH;
  float b = (float) (map_y) / SCALLED_TILE_HEIGHT;
  /* 2.0 come from 2 * SCALLED_TILE_WIDTH in "a"
    parm ( map_x - 2 * SCALLED_TILE_WIDTH ) */
  float result = a + b - 2.0;
  if (result < 0) {
    /* correct negative numbers in (0 ... -1) to show as -1 tile (and more) */
    *pCol = (result - 1.0);
  } else {
    *pCol = result;
  }

  result = (b - a) + 2.0;
  if (result < 0) {
    /* correct negative numbers in (0 ... -1) to show as -1 tile (and more) */
    *pRow = (result - 1.0);
  } else {
    *pRow = result;
  }
  
  freelog(LOG_DEBUG, "get_citymap_cr(pos=(%d,%d))=(%d,%d)",
	  map_x, map_y, *pCol, *pRow);
  
  return is_valid_city_coords(*pCol, *pRow);
}
#endif

SDL_Surface * get_scaled_city_map(struct city *pCity)
{
  SDL_Surface *pBuf = create_city_map(pCity);
  if (pBuf->w > 192 || pBuf->h > 134)
  {
    float zoom = (pBuf->w > 192 ? 192.0 / pBuf->w : 134.0 / pBuf->h);
    SDL_Surface *pRet = ZoomSurface(pBuf, zoom, zoom, 1);
    FREESURFACE(pBuf);
    return pRet;
  } 
  return pBuf;
}

/**************************************************************************
  city resource map: event callback
**************************************************************************/
static int resource_map_city_dlg_callback(struct GUI *pMap)
{
  int col, row;
#ifndef NO_ISO
  if (get_citymap_cr(Main.event.motion.x - pMap->size.x,
		     Main.event.motion.y - pMap->size.y, &col, &row)) {
    city_toggle_worker(pCityDlg->pCity, col, row);
  }
#else
  if (sdl_canvas_to_city_pos(&col, &row, Main.event.motion.x - pMap->size.x,
		     Main.event.motion.y - pMap->size.y)) {
		       
    city_toggle_worker(pCityDlg->pCity, col, row);
  }
#endif
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static void fill_tile_resorce_surf(SDL_Surface * pTile,
				   const struct city *pCity,
				   Uint16 city_col, Uint16 city_row)
{
  int i, step;
  SDL_Rect dest;
  int food = city_get_food_tile(city_col, city_row, pCity);
  int shield = city_get_shields_tile(city_col, city_row, pCity);
  int trade = city_get_trade_tile(city_col, city_row, pCity);
  
  step = food + shield + trade;
  if(step) {
    dest.y = (SCALLED_TILE_HEIGHT - pIcons->pFood->h) / 2;
    dest.x = 10;
    step = (SCALLED_TILE_WIDTH - 2 * dest.x) / step;
  

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
  } else {
    dest.x = (SCALLED_TILE_WIDTH - pIcons->pFace->w) / 2;
    dest.y = (SCALLED_TILE_HEIGHT - pIcons->pFace->h) / 2;
    SDL_BlitSurface(pIcons->pFace, NULL, pTile, &dest);
  }
}

/**************************************************************************
  Refresh (update) the city resource map
**************************************************************************/
void refresh_city_resource_map(SDL_Surface *pDest, int x, int y,
			       const struct city *pCity,
			       bool (*worker_check) (const struct city *,
						     int, int))
{
#ifndef NO_ISO
  register int col, row;
  SDL_Rect dest;
  int sx, sy, row0, real_col = pCity->x, real_row = pCity->y;
  int x0 = x + SCALLED_TILE_WIDTH + SCALLED_TILE_WIDTH / 2;
  int y0 = y;
  
  SDL_Surface *pTile = create_surf(SCALLED_TILE_WIDTH,
				   SCALLED_TILE_HEIGHT, SDL_SWSURFACE);

  SDL_SetColorKey(pTile, SDL_SRCCOLORKEY, 0x0);

  real_col -= 2;
  real_row -= 2;
  correction_map_pos((int *) &real_col, (int *) &real_row);
  row0 = real_row;

  /* draw loop */
  for (col = 0; col < CITY_MAP_SIZE; col++) {
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
	if (worker_check(pCity, col, row)) {
	  fill_tile_resorce_surf(pTile, pCity, col, row);
	  SDL_BlitSurface(pTile, NULL, pDest, &dest);
	  /* clear pTile */
	  SDL_FillRect(pTile, NULL, 0x0);
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
#else
  
  int city_x, city_y;
  int map_x, map_y, canvas_x, canvas_y;
  SDL_Rect dest;  
  SDL_Surface *pTile = create_surf(SCALLED_TILE_WIDTH,
				   SCALLED_TILE_HEIGHT, SDL_SWSURFACE);

  /* We have to draw the tiles in a particular order, so its best
     to avoid using any iterator macro. */
  for (city_x = 0; city_x<CITY_MAP_SIZE; city_x++)
  {
    for (city_y = 0; city_y<CITY_MAP_SIZE; city_y++)
    {
      if (is_valid_city_coords(city_x, city_y)
	&& city_map_to_map(&map_x, &map_y, pCity, city_x, city_y)
	&& tile_get_known(map_x, map_y)
	&& sdl_city_to_canvas_pos(&canvas_x, &canvas_y, city_x, city_y))
      {
        dest.x = canvas_x;
	dest.y = canvas_y;
	if (worker_check(pCity, map_x, map_y))
	{
	  fill_tile_resorce_surf(pTile, pCity, map_x, map_y);
	  SDL_BlitSurface(pTile, NULL, pDest, &dest);
	  /* clear pTile */
	  SDL_FillRect(pTile, NULL, 0x0);
	}
      }
    }
  }
  
  FREESURFACE(pTile);
#endif    
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
  free_city_units_lists();
  /* refresh resource map */
  FREESURFACE(pCityDlg->pResource_Map->theme);
  pCityDlg->pResource_Map->theme = get_scaled_city_map(pCityDlg->pCity);
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
    SDL_Client_Flags |= CF_CHANGED_CITY_NAME;
    city_rename(pCityDlg->pCity, tmp);
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
  if (pCityDlg->pCity_Name_Edit) {
    char name[MAX_LEN_NAME];
    
    convertcopy_to_chars(name, MAX_LEN_NAME,
			    pCityDlg->pCity_Name_Edit->string16->text);
    if ((strcmp(pCity->name, name) != 0)
      || (SDL_Client_Flags & CF_CHANGED_CITY_NAME)) {
      copy_chars_to_string16(pCityDlg->pCity_Name_Edit->string16, pCity->name);
      rebuild_citydlg_title_str(pCityDlg->pEndCityWidgetList, pCity);
      SDL_Client_Flags &= ~CF_CHANGED_CITY_NAME;
    }
  }
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
  pStr->fgcol.r = 238;
  pStr->fgcol.g = 156;
  pStr->fgcol.b = 7;
  pStr->style |= TTF_STYLE_BOLD;

  pSurf = create_text_surf_from_str16(pStr);

  dest.x = pCityWindow->size.x + 5 + (207 - pSurf->w) / 2;
  dest.y = pCityWindow->size.y + WINDOW_TILE_HIGH + 6;

  SDL_BlitSurface(pSurf, NULL, pCityWindow->dst, &dest);

  FREESURFACE(pSurf);
  FREESTRING16(pStr);

  if (!pCityDlg->pPanel) {
    create_city_options_widget_list(pCity);
  }
  redraw_group(pCityDlg->pPanel->pBeginWidgetList,
		 pCityDlg->pPanel->pEndWidgetList, 0);
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
  pStr->fgcol.r = 238;
  pStr->fgcol.g = 156;
  pStr->fgcol.b = 7;
  pStr->style |= TTF_STYLE_BOLD;

  pSurf = create_text_surf_from_str16(pStr);

  dest.x = pCityWindow->size.x + 5 + (207 - pSurf->w) / 2;
  dest.y = pCityWindow->size.y + WINDOW_TILE_HIGH + 6;

  SDL_BlitSurface(pSurf, NULL, pCityWindow->dst, &dest);

  FREESURFACE(pSurf);
  FREESTRING16(pStr);

  if (pCityDlg->pPanel) {
    if (size) {
      redraw_group(pCityDlg->pPanel->pBeginWidgetList,
		   pCityDlg->pPanel->pEndWidgetList, 0);
    } else {
      del_group_of_widgets_from_gui_list(pCityDlg->pPanel->pBeginWidgetList,
					 pCityDlg->pPanel->pEndWidgetList);
      FREE(pCityDlg->pPanel->pScroll);
      FREE(pCityDlg->pPanel);
    }
  } else {
    if (size) {
      create_present_supported_units_widget_list(pList);
      redraw_group(pCityDlg->pPanel->pBeginWidgetList,
		   pCityDlg->pPanel->pEndWidgetList, 0);
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
  pStr->fgcol.r = 238;
  pStr->fgcol.g = 156;
  pStr->fgcol.b = 7;
  pStr->style |= TTF_STYLE_BOLD;

  pSurf = create_text_surf_from_str16(pStr);

  dest.x = pCityWindow->size.x + 5 + (207 - pSurf->w) / 2;
  dest.y = pCityWindow->size.y + WINDOW_TILE_HIGH + 6;

  SDL_BlitSurface(pSurf, NULL, pCityWindow->dst, &dest);

  FREESURFACE(pSurf);
  FREESTRING16(pStr);

  if (pCityDlg->pPanel) {
    if (size) {
      redraw_group(pCityDlg->pPanel->pBeginWidgetList,
		   pCityDlg->pPanel->pEndWidgetList, 0);
    } else {
      del_group_of_widgets_from_gui_list(pCityDlg->pPanel->pBeginWidgetList,
					 pCityDlg->pPanel->pEndWidgetList);
      FREE(pCityDlg->pPanel->pScroll);
      FREE(pCityDlg->pPanel);
    }
  } else {
    if (size) {
      create_present_supported_units_widget_list(pList);
      redraw_group(pCityDlg->pPanel->pBeginWidgetList,
		   pCityDlg->pPanel->pEndWidgetList, 0);
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
  SDL_Rect dest;

  my_snprintf(cBuf, sizeof(cBuf), _("Info Panel"));
  pStr = create_str16_from_char(cBuf, 10);
  pStr->fgcol.r = 238;
  pStr->fgcol.g = 156;
  pStr->fgcol.b = 7;
  pStr->style |= TTF_STYLE_BOLD;

  pSurf = create_text_surf_from_str16(pStr);
  
  dest.x = pCityWindow->size.x + 5 + (207 - pSurf->w) / 2;
  dest.y = pCityWindow->size.y + WINDOW_TILE_HIGH + 6;
      
  SDL_BlitSurface(pSurf, NULL, pCityWindow->dst, &dest);

  dest.x = pCityWindow->size.x + 10;
  dest.y += pSurf->h + 1;

  FREESURFACE(pSurf);

  change_ptsize16(pStr, 12);
  pStr->fgcol.r = 220;
  pStr->fgcol.g = 186;
  pStr->fgcol.b = 60;

  if (pCity->pollution) {
    my_snprintf(cBuf, sizeof(cBuf), _("Pollution : %d"),
		pCity->pollution);

    copy_chars_to_string16(pStr, cBuf);
    
    pSurf = create_text_surf_from_str16(pStr);

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

    copy_chars_to_string16(pStr, cBuf);

    pSurf = create_text_surf_from_str16(pStr);

    SDL_BlitSurface(pSurf, NULL, pCityWindow->dst, &dest);

    dest.y += pSurf->h + 3;

    FREESURFACE(pSurf);
  }

  my_snprintf(cBuf, sizeof(cBuf), _("Trade routes : "));

  copy_chars_to_string16(pStr, cBuf);

  pSurf = create_text_surf_from_str16(pStr);

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


      copy_chars_to_string16(pStr, cBuf);

      pSurf = create_text_surf_from_str16(pStr);
      
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

    copy_chars_to_string16(pStr, cBuf);
    pSurf = create_text_surf_from_str16(pStr);
    SDL_BlitSurface(pSurf, NULL, pCityWindow->dst, &dest);

    dest.x += pSurf->w + 3;
    dest.y += 4;
    SDL_BlitSurface(pIcons->pTrade, NULL, pCityWindow->dst, &dest);

    FREESURFACE(pSurf);
  } else {
    my_snprintf(cBuf, sizeof(cBuf), _("none"));

    copy_chars_to_string16(pStr, cBuf);

    pSurf = create_text_surf_from_str16(pStr);

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
  SDL_Rect dest;

  my_snprintf(cBuf, sizeof(cBuf), _("Happiness panel"));

  pStr = create_str16_from_char(cBuf, 10);
  pStr->fgcol.r = 238;
  pStr->fgcol.g = 156;
  pStr->fgcol.b = 7;
  pStr->style |= TTF_STYLE_BOLD;

  pSurf = create_text_surf_from_str16(pStr);

  dest.x = pCityWindow->size.x + 5 + (207 - pSurf->w) / 2;
  dest.y = pCityWindow->size.y + WINDOW_TILE_HIGH + 6;
  SDL_BlitSurface(pSurf, NULL, pCityWindow->dst, &dest);
  
  dest.x = pCityWindow->size.x + 10;
  dest.y += pSurf->h + 1;

  FREESURFACE(pSurf);
  FREESTRING16(pStr);

  count = (pCity->ppl_happy[4] + pCity->ppl_content[4]
	   + pCity->ppl_unhappy[4] + pCity->ppl_angry[4]
	   + pCity->specialists[SP_ELVIS] + pCity->specialists[SP_SCIENTIST]
	   + pCity->specialists[SP_TAXMAN]);

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

      if (pCity->specialists[SP_ELVIS]) {
	for (i = 0; i < pCity->specialists[SP_ELVIS]; i++) {
	  SDL_BlitSurface(pIcons->pSpec_Lux, NULL, pCityWindow->dst, &dest);
	  dest.x += step;
	}
      }

      if (pCity->specialists[SP_TAXMAN]) {
	for (i = 0; i < pCity->specialists[SP_TAXMAN]; i++) {
	  SDL_BlitSurface(pIcons->pSpec_Tax, NULL, pCityWindow->dst, &dest);
	  dest.x += step;
	}
      }

      if (pCity->specialists[SP_SCIENTIST]) {
	for (i = 0; i < pCity->specialists[SP_SCIENTIST]; i++) {
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

  if ((city_unhappy(pCity) || city_celebrating(pCity) || city_happy(pCity) ||
      cma_is_city_under_agent(pCity, NULL))
      ^ ((SDL_Client_Flags & CF_CITY_STATUS_SPECIAL) == CF_CITY_STATUS_SPECIAL)) {
    /* city status was changed : NORMAL <-> DISORDER, HAPPY, CELEBR. */

    SDL_Client_Flags ^= CF_CITY_STATUS_SPECIAL;

    /* upd. resource map */
    FREESURFACE(pCityDlg->pResource_Map->theme);
    pCityDlg->pResource_Map->theme = get_scaled_city_map(pCity);

    /* upd. window title */
    rebuild_citydlg_title_str(pCityDlg->pEndCityWidgetList, pCity);
	
    rebuild_focus_anim_frames();
  }

  /* redraw city dlg */
  redraw_group(pCityDlg->pBeginCityWidgetList,
	       			pCityDlg->pEndCityWidgetList, 0);
  
  /* is_worker_here(struct city *, int, int) - is function pointer */
  refresh_city_resource_map(pCityDlg->pResource_Map->dst,
			    pCityDlg->pResource_Map->size.x,
			    pCityDlg->pResource_Map->size.y,
  			    pCity, is_worker_here);

  /* ================================================================= */
  my_snprintf(cBuf, sizeof(cBuf), _("City map"));

  pStr = create_str16_from_char(cBuf, 11);
  pStr->fgcol = *get_game_colorRGB(COLOR_STD_CITY_GOLD);
  pStr->style |= TTF_STYLE_BOLD;

  pBuf = create_text_surf_from_str16(pStr);

  dest.x = pWindow->size.x + 222 + (115 - pBuf->w) / 2;
  dest.y = pWindow->size.y + 69 + (15 - pBuf->h) / 2;

  SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

  FREESURFACE(pBuf);

  my_snprintf(cBuf, sizeof(cBuf), _("Citizens"));

  copy_chars_to_string16(pStr, cBuf);
  pStr->fgcol = *get_game_colorRGB(COLOR_STD_CITY_LUX);
  
  pBuf = create_text_surf_from_str16(pStr);

  dest.x = pWindow->size.x + 354 + (147 - pBuf->w) / 2;
  dest.y = pWindow->size.y + 67 + (13 - pBuf->h) / 2;

  SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

  FREESURFACE(pBuf);

  my_snprintf(cBuf, sizeof(cBuf), _("City Improvements"));

  copy_chars_to_string16(pStr, cBuf);
  pStr->fgcol = *get_game_colorRGB(COLOR_STD_CITY_GOLD);
  
  pBuf = create_text_surf_from_str16(pStr);

  dest.x = pWindow->size.x + 517 + (115 - pBuf->w) / 2;
  dest.y = pWindow->size.y + 69 + (15 - pBuf->h) / 2;

  SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

  FREESURFACE(pBuf);
  /* ================================================================= */
  /* food label */
  my_snprintf(cBuf, sizeof(cBuf), _("Food : %d per turn"),
	      pCity->food_prod);

  copy_chars_to_string16(pStr, cBuf);
  pStr->fgcol = *get_game_colorRGB(COLOR_STD_GROUND);

  pBuf = create_text_surf_from_str16(pStr);

  dest.x = pWindow->size.x + 200;
  dest.y = pWindow->size.y + 228 + (16 - pBuf->h) / 2;

  SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

  FREESURFACE(pBuf);

  /* draw food income */
  dest.y = pWindow->size.y + 246 + (16 - pIcons->pBIG_Food->h) / 2;
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

  copy_chars_to_string16(pStr, cBuf);
  pStr->fgcol = *get_game_colorRGB(COLOR_STD_CITY_FOOD_SURPLUS);
  
  pBuf = create_text_surf_from_str16(pStr);

  dest.x = pWindow->size.x + 440 - pBuf->w;
  dest.y = pWindow->size.y + 228 + (16 - pBuf->h) / 2;

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
    dest.y = pWindow->size.y + 246 + (16 - pBuf->h) / 2;

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

  copy_chars_to_string16(pStr, cBuf);
  pStr->fgcol = *get_game_colorRGB(COLOR_STD_CITY_PROD);
  
  pBuf = create_text_surf_from_str16(pStr);

  dest.x = pWindow->size.x + 200;
  dest.y = pWindow->size.y + 263 + (15 - pBuf->h) / 2;

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

    dest.y = pWindow->size.y + 281 + (16 - pBuf->h) / 2;
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
	  pCity->shield_prod + pCity->shield_waste - pCity->shield_surplus);

  copy_chars_to_string16(pStr, cBuf);
  pStr->fgcol = *get_game_colorRGB(COLOR_STD_CITY_SUPPORT);
  
  pBuf = create_text_surf_from_str16(pStr);

  dest.x = pWindow->size.x + 440 - pBuf->w;
  dest.y = pWindow->size.y + 263 + (15 - pBuf->h) / 2;

  SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

  FREESURFACE(pBuf);

  /* draw support shields */
  if (pCity->shield_prod - pCity->shield_surplus) {
    dest.x = pWindow->size.x + 423;
    dest.y =
	pWindow->size.y + 281 + (16 - pIcons->pBIG_Shield->h) / 2;
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

  copy_chars_to_string16(pStr, cBuf);
  pStr->fgcol = *get_game_colorRGB(COLOR_STD_CITY_TRADE);
  
  pBuf = create_text_surf_from_str16(pStr);
  
  dest.x = pWindow->size.x + 200;
  dest.y = pWindow->size.y + 298 + (15 - pBuf->h) / 2;

  SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

  FREESURFACE(pBuf);

  /* draw total (trade - corruption) */
  if (pCity->trade_prod) {
    dest.y =
	pWindow->size.y + 316 + (16 - pIcons->pBIG_Trade->h) / 2;
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

  copy_chars_to_string16(pStr, cBuf);
  pStr->fgcol.r = 0;
  pStr->fgcol.g = 0;
  pStr->fgcol.b = 0;

  pBuf = create_text_surf_from_str16(pStr);
  
  dest.x = pWindow->size.x + 440 - pBuf->w;
  dest.y = pWindow->size.y + 298 + (15 - pBuf->h) / 2;

  SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

  FREESURFACE(pBuf);

  /* draw corruption */
  if (pCity->corruption) {
    dest.x = pWindow->size.x + 423;
    dest.y =
	pWindow->size.y + 316 + (16 - pIcons->pBIG_Trade->h) / 2;

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
	      city_gold_surplus(pCity, pcity->tax_total), pCity->tax_total);

  copy_chars_to_string16(pStr, cBuf);
  pStr->fgcol = *get_game_colorRGB(COLOR_STD_CITY_GOLD);
  
  pBuf = create_text_surf_from_str16(pStr);

  dest.x = pWindow->size.x + 200;
  dest.y = pWindow->size.y + 342 + (15 - pBuf->h) / 2;

  SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

  FREESURFACE(pBuf);

  /* draw coins */
  count = city_gold_surplus(pCity, pcity->tax_total);
  if (count) {

    if (count > 0) {
      pBuf = pIcons->pBIG_Coin;
    } else {
      count *= -1;
      pBuf = pIcons->pBIG_Coin_Corr;
    }

    dest.y = pWindow->size.y + 359 + (16 - pBuf->h) / 2;
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
	      city_gold_surplus(pCity, pcity->tax_total));

  copy_chars_to_string16(pStr, cBuf);
  pStr->fgcol = *get_game_colorRGB(COLOR_STD_CITY_UNKEEP);
  
  pBuf = create_text_surf_from_str16(pStr);

  dest.x = pWindow->size.x + 440 - pBuf->w;
  dest.y = pWindow->size.y + 342 + (15 - pBuf->h) / 2;

  SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

  FREESURFACE(pBuf);

  /* draw upkeep */
  count = city_gold_surplus(pCity, pcity->tax_total);
  if (pCity->tax_total - count) {

    dest.x = pWindow->size.x + 423;
    dest.y = pWindow->size.y + 359
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

  copy_chars_to_string16(pStr, cBuf);
  pStr->fgcol = *get_game_colorRGB(COLOR_STD_CITY_SCIENCE);
  
  pBuf = create_text_surf_from_str16(pStr);

  dest.x = pWindow->size.x + 200;
  dest.y = pWindow->size.y + 376 + (15 - pBuf->h) / 2;

  SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

  FREESURFACE(pBuf);

  /* draw colb */
  count = pCity->science_total;
  if (count) {

    dest.y =
	pWindow->size.y + 394 + (16 - pIcons->pBIG_Colb->h) / 2;
    dest.x = pWindow->size.x + 203;

    if ((pIcons->pBIG_Colb->w * count) > 235) {
      step = (235 - pIcons->pBIG_Colb->w) / (count - 1);
      if (!step) {
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

  copy_chars_to_string16(pStr, cBuf);
  pStr->fgcol = *get_game_colorRGB(COLOR_STD_CITY_LUX);
  
  pBuf = create_text_surf_from_str16(pStr);

  dest.x = pWindow->size.x + 200;
  dest.y = pWindow->size.y + 412 + (15 - pBuf->h) / 2;

  SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

  FREESURFACE(pBuf);

  /* draw luxury */
  if (pCity->luxury_total) {

    dest.y =
	pWindow->size.y + 429 + (16 - pIcons->pBIG_Luxury->h) / 2;
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

  copy_chars_to_string16(pStr, cBuf);
  pStr->fgcol = *get_game_colorRGB(COLOR_STD_GROUND);

  pBuf = create_text_surf_from_str16(pStr);

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
    copy_chars_to_string16(pStr, _("Stock"));
    pBuf = create_text_surf_from_str16(pStr);

    dest.x = pWindow->size.x + 461 + (76 - pBuf->w) / 2;
    dest.y = pWindow->size.y + 258 - pBuf->h - 1;

    SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

    FREESURFACE(pBuf);

    /* granary label */
    copy_chars_to_string16(pStr, _("Granary"));
    pBuf = create_text_surf_from_str16(pStr);

    dest.x = pWindow->size.x + 549 + (76 - pBuf->w) / 2;
    /*dest.y = pWindow->size.y + 258 - pBuf->h - 1; */

    SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

    FREESURFACE(pBuf);

    /* draw bcgd granary */
    dest.x = pWindow->size.x + 462;
    dest.y = pWindow->size.y + 260;
    dest.w = 70 + 4;
    dest.h = i + 4;
    SDL_FillRectAlpha(pWindow->dst, &dest, &color);

    putframe(pWindow->dst, dest.x - 1, dest.y - 1,
		dest.x + dest.w, dest.y + dest.h, 0xff000000);
		
    /* draw bcgd stocks*/
    dest.x = pWindow->size.x + 550;
    dest.y = pWindow->size.y + 260;
    SDL_FillRectAlpha(pWindow->dst, &dest, &color);

    putframe(pWindow->dst, dest.x - 1, dest.y - 1,
	dest.x + dest.w, dest.y + dest.h, 0xff000000);

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
    copy_chars_to_string16(pStr, _("Stock"));
    pBuf = create_text_surf_from_str16(pStr);

    dest.x = pWindow->size.x + 461 + (144 - pBuf->w) / 2;
    dest.y = pWindow->size.y + 258 - pBuf->h - 1;
    SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);
    FREESURFACE(pBuf);
    
    /* food stock */

    /* draw bcgd */
    dest.x = pWindow->size.x + 462;
    dest.y = pWindow->size.y + 260;
    dest.w = 144;
    dest.h = i + 4;
    SDL_FillRectAlpha(pWindow->dst, &dest, &color);

    putframe(pWindow->dst, dest.x - 1, dest.y - 1,
			dest.x + dest.w, dest.y + dest.h, 0xff000000);

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
    cost = unit_build_shield_cost(pCity->currently_building);
    count = cost / 10;
        
    copy_chars_to_string16(pStr, pUnit->name);
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

      if (pCityDlg->pBuy_Button
	 && get_wstate(pCityDlg->pBuy_Button) != FC_WS_DISABLED) {
	set_wstate(pCityDlg->pBuy_Button, FC_WS_DISABLED);
	redraw_widget(pCityDlg->pBuy_Button);
      }

      /* You can't see capitalization progres */
      count = 0;

    } else {

      if (!pCity->did_buy && pCityDlg->pBuy_Button
	 && (get_wstate(pCityDlg->pBuy_Button) == FC_WS_DISABLED)) {
	set_wstate(pCityDlg->pBuy_Button, FC_WS_NORMAL);
	redraw_widget(pCityDlg->pBuy_Button);
      }

      cost = impr_build_shield_cost(pCity->currently_building);
      count = cost / 10;
      
    }

    copy_chars_to_string16(pStr, pImpr->name);
    pBuf = GET_SURF(pImpr->sprite);

    /* blit impr icon */
    dest.x = pWindow->size.x + 6 + (185 - pBuf->w) / 2;
    dest.y = pWindow->size.y + 230;
    SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);

    dest.y += (pBuf->h + 2);

    pBuf = create_text_surf_from_str16(pStr);

    dest.x = pWindow->size.x + 6 + (185 - pBuf->w) / 2;
  }

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
    
    /* draw sheild stock background */
    dest.x = pWindow->size.x + 28;
    dest.y = pWindow->size.y + 270;
    dest.w = 144;
    dest.h = i + 4;
    SDL_FillRectAlpha(pWindow->dst, &dest, &color);
    putframe(pWindow->dst, dest.x - 1, dest.y - 1,
		dest.x + dest.w, dest.y + dest.h, 0xff000000);
    /* draw production progres text */
    dest.y = pWindow->size.y + 270 + dest.h + 1;
    
    if (pCity->shield_stock < cost) {
      count = city_turns_to_build(pCity,
    	pCity->currently_building, pCity->is_building_unit, TRUE);
      if (count == 999) {
        my_snprintf(cBuf, sizeof(cBuf), "(%d/%d) %s!",
		  		pCity->shield_stock, cost,  _("blocked"));
      } else {
        my_snprintf(cBuf, sizeof(cBuf), "(%d/%d) %d %s",
	    pCity->shield_stock, cost, count, PL_("turn", "turns", count));
     }
   } else {
     my_snprintf(cBuf, sizeof(cBuf), "(%d/%d) %s!",
		    		pCity->shield_stock, cost, _("finished"));
   }

    copy_chars_to_string16(pStr, cBuf);
    pStr->fgcol = *get_game_colorRGB(COLOR_STD_CITY_LUX);
    
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
  count = (pCity->ppl_happy[4] + pCity->ppl_content[4]
	   + pCity->ppl_unhappy[4] + pCity->ppl_angry[4]
	   + pCity->specialists[SP_ELVIS] + pCity->specialists[SP_SCIENTIST]
	   + pCity->specialists[SP_TAXMAN]);

  pBuf = get_citizen_surface(CITIZEN_ELVIS, 0);
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
      pBuf = get_citizen_surface(CITIZEN_HAPPY, i);
      SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);
      dest.x += step;
    }
  }

  if (pCity->ppl_content[4]) {
    for (i = 0; i < pCity->ppl_content[4]; i++) {
      pBuf = get_citizen_surface(CITIZEN_CONTENT, i);
      SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);
      dest.x += step;
    }
  }

  if (pCity->ppl_unhappy[4]) {
    for (i = 0; i < pCity->ppl_unhappy[4]; i++) {
      pBuf = get_citizen_surface(CITIZEN_UNHAPPY, i);
      SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);
      dest.x += step;
    }
  }

  if (pCity->ppl_angry[4]) {
    for (i = 0; i < pCity->ppl_angry[4]; i++) {
      pBuf = get_citizen_surface(CITIZEN_ANGRY, i);
      SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);
      dest.x += step;
    }
  }
    
  pCityDlg->specs[0] = FALSE;
  pCityDlg->specs[1] = FALSE;
  pCityDlg->specs[2] = FALSE;
  
  if (pCity->specialists[SP_ELVIS]) {
    pBuf = get_citizen_surface(CITIZEN_ELVIS, 0);
    pCityDlg->specs_area[0].x = dest.x;
    pCityDlg->specs_area[0].y = dest.y;
    pCityDlg->specs_area[0].w = pBuf->w;
    pCityDlg->specs_area[0].h = pBuf->h;
    for (i = 0; i < pCity->specialists[SP_ELVIS]; i++) {
      SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);
      dest.x += step;
      pCityDlg->specs_area[0].w += step;
    }
    pCityDlg->specs_area[0].w -= step;
    pCityDlg->specs[0] = TRUE;
  }

  if (pCity->specialists[SP_TAXMAN]) {
    pBuf = get_citizen_surface(CITIZEN_TAXMAN, 0);
    pCityDlg->specs_area[1].x = dest.x;
    pCityDlg->specs_area[1].y = dest.y;
    pCityDlg->specs_area[1].w = pBuf->w;
    pCityDlg->specs_area[1].h = pBuf->h;
    for (i = 0; i < pCity->specialists[SP_TAXMAN]; i++) {
      SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);
      dest.x += step;
      pCityDlg->specs_area[1].w += step;
    }
    pCityDlg->specs_area[1].w -= step;
    pCityDlg->specs[1] = TRUE;
  }

  if (pCity->specialists[SP_SCIENTIST]) {
    pBuf = get_citizen_surface(CITIZEN_SCIENTIST, 0);
    pCityDlg->specs_area[2].x = dest.x;
    pCityDlg->specs_area[2].y = dest.y;
    pCityDlg->specs_area[2].w = pBuf->w;
    pCityDlg->specs_area[2].h = pBuf->h;
    for (i = 0; i < pCity->specialists[SP_SCIENTIST]; i++) {
      SDL_BlitSurface(pBuf, NULL, pWindow->dst, &dest);
      dest.x += step;
      pCityDlg->specs_area[2].w += step;
    }
    pCityDlg->specs_area[2].w -= step;
    pCityDlg->specs[2] = TRUE;
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
  int count = 0;
  struct GUI *pWindow = pCityDlg->pEndCityWidgetList;
  struct GUI *pAdd_Dock, *pBuf, *pLast;
  SDL_Surface *pLogo = NULL;
  SDL_String16 *pStr = NULL;
  struct impr_type *pImpr = NULL;
  struct player *pOwner = city_owner(pCity);
    
  if(!pCityDlg->pImprv) {
    pCityDlg->pImprv = MALLOC(sizeof(struct ADVANCED_DLG));
  }
  
  /* free old list */
  if (pCityDlg->pImprv->pEndWidgetList) {
    del_group_of_widgets_from_gui_list(pCityDlg->pImprv->pBeginWidgetList,
				       pCityDlg->pImprv->pEndWidgetList);
    pCityDlg->pImprv->pEndWidgetList = NULL;
    pCityDlg->pImprv->pBeginWidgetList = NULL;
    pCityDlg->pImprv->pActiveWidgetList = NULL;
    FREE(pCityDlg->pImprv->pScroll);
  } 
    
  pAdd_Dock = pCityDlg->pAdd_Point;
  pBuf = pLast = pAdd_Dock;
  
  /* allock new */
  built_impr_iterate(pCity, imp) {

    pImpr = get_improvement_type(imp);

    pStr = create_str16_from_char(get_impr_name_ex(pCity, imp), 12);
    pStr->fgcol = *get_game_colorRGB(COLOR_STD_WHITE);
    pStr->bgcol.unused = 128;/* 50% transp */

    pStr->style |= TTF_STYLE_BOLD;

    pLogo = ZoomSurface(GET_SURF(pImpr->sprite), 0.6, 0.6 , 1);
    
    pBuf = create_iconlabel(pLogo, pWindow->dst, pStr,
			 (WF_FREE_THEME | WF_DRAW_THEME_TRANSPARENT));

    pBuf->size.x = pWindow->size.x + 428;
    pBuf->size.y = pWindow->size.y + 91 + count * pBuf->size.h;
    
    pBuf->size.w = 182;
    pBuf->action = sell_imprvm_dlg_callback;

    if (!pCityDlg->pCity->did_sell
        && !pImpr->is_wonder && (pOwner == game.player_ptr)) {
      set_wstate(pBuf, FC_WS_NORMAL);
    }

    pBuf->ID = MAX_ID - imp - 3000;
    DownAdd(pBuf, pAdd_Dock);
    pAdd_Dock = pBuf;
        
    count++;

    if (count > 8) {
      set_wflag(pBuf, WF_HIDDEN);
    }

  } built_impr_iterate_end;

  if (count) {
    pCityDlg->pImprv->pEndWidgetList = pLast->prev;
    pCityDlg->pImprv->pEndActiveWidgetList = pLast->prev;
    pCityDlg->pImprv->pBeginWidgetList = pBuf;
    pCityDlg->pImprv->pBeginActiveWidgetList = pBuf;

    if (count > 8) {
      pCityDlg->pImprv->pActiveWidgetList =
		    pCityDlg->pImprv->pEndActiveWidgetList;
      pCityDlg->pImprv->pScroll = MALLOC(sizeof(struct ScrollBar));
      pCityDlg->pImprv->pScroll->step = 1;  
      pCityDlg->pImprv->pScroll->active = 8;
      pCityDlg->pImprv->pScroll->count = count;
    
      create_vertical_scrollbar(pCityDlg->pImprv, 1, 8, TRUE, TRUE);
    
      setup_vertical_scrollbar_area(pCityDlg->pImprv->pScroll,
	pWindow->size.x + 629, pWindow->size.y + 90, 130, TRUE);
    }
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

  if (cma_is_city_under_agent(pCity, NULL)) {
    mystrlcat(cBuf, _(" - under CMA control."), sizeof(cBuf));
  }
  
  copy_chars_to_string16(pWindow->string16, cBuf);
}


/* ========================= Public ================================== */

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
  struct player *pOwner = city_owner(pCity);
    
  if (pCityDlg) {
    return;
  }

  update_menus();

  pCityDlg = MALLOC(sizeof(struct city_dialog));
  pCityDlg->pCity = pCity;
  
  pStr = create_string16(NULL, 0, 12);
  pStr->style |= TTF_STYLE_BOLD;
  pWindow = create_window(get_locked_buffer(), pStr, 640, 480, 0);
  unlock_buffer();
  
  rebuild_citydlg_title_str(pWindow, pCity);

  pWindow->size.x = (pWindow->dst->w - 640) / 2;
  pWindow->size.y = (pWindow->dst->h - 480) / 2;
  pWindow->size.w = 640;
  pWindow->size.h = 480;
  pWindow->action = city_dlg_callback;
  set_wstate(pWindow, FC_WS_NORMAL);

  /* create window background */
  pLogo = get_logo_gfx();
  if (resize_window(pWindow, pLogo, NULL, pWindow->size.w, pWindow->size.h)) {
    FREESURFACE(pLogo);
  }
    
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
  pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 9;
  pBuf->key = SDLK_ESCAPE;
  set_wstate(pBuf, FC_WS_NORMAL);
  add_to_gui_list(ID_CITY_DLG_EXIT_BUTTON, pBuf);
  
  /* Buttons */
  pBuf = create_themeicon(pTheme->INFO_Icon, pWindow->dst,
			  (WF_WIDGET_HAS_INFO_LABEL |
			   WF_DRAW_THEME_TRANSPARENT));
  pBuf->string16 = create_str16_from_char(_("Information panel"), 12);
  pBuf->action = info_city_dlg_callback;
  pBuf->size.x =
      pWindow->size.x + pWindow->size.w - 2 * pBuf->size.w - 5 - 10;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 9;
  set_wstate(pBuf, FC_WS_NORMAL);
  add_to_gui_list(ID_CITY_DLG_INFO_BUTTON, pBuf);
  /* -------- */
  
  pBuf = create_themeicon(pTheme->Happy_Icon, pWindow->dst,
			  (WF_WIDGET_HAS_INFO_LABEL |
			   WF_DRAW_THEME_TRANSPARENT));
  pBuf->string16 = create_str16_from_char(_("Happiness panel"), 12);
  pBuf->action = happy_city_dlg_callback;
  pBuf->size.x =
      pWindow->size.x + pWindow->size.w - 3 * pBuf->size.w - 10 - 10;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 9;
  set_wstate(pBuf, FC_WS_NORMAL);
  add_to_gui_list(ID_CITY_DLG_HAPPY_BUTTON, pBuf);
  /* -------- */
  
  pBuf = create_themeicon(pTheme->Army_Icon, pWindow->dst,
			  (WF_WIDGET_HAS_INFO_LABEL |
			   WF_DRAW_THEME_TRANSPARENT));
  pBuf->string16 = create_str16_from_char(_("Garrison panel"), 12);
  pBuf->action = army_city_dlg_callback;
  pBuf->size.x =
      pWindow->size.x + pWindow->size.w - 4 * pBuf->size.w - 10 - 10;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 9;
  set_wstate(pBuf, FC_WS_NORMAL);
  add_to_gui_list(ID_CITY_DLG_ARMY_BUTTON, pBuf);
  /* -------- */
  
  pBuf = create_themeicon(pTheme->Support_Icon, pWindow->dst,
			  (WF_WIDGET_HAS_INFO_LABEL |
			   WF_DRAW_THEME_TRANSPARENT));
  pBuf->string16 = create_str16_from_char(_("Maintenance panel"), 12);
  pBuf->action = supported_unit_city_dlg_callback;
  pBuf->size.x =
      pWindow->size.x + pWindow->size.w - 5 * pBuf->size.w - 10 - 10;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 9;
  set_wstate(pBuf, FC_WS_NORMAL);
  add_to_gui_list(ID_CITY_DLG_SUPPORT_BUTTON, pBuf);
  
  pCityDlg->pAdd_Point = pBuf;
  pCityDlg->pBeginCityWidgetList = pBuf;
  /* ===================================================== */
  rebuild_imprm_list(pCity);
  /* ===================================================== */
  
  pLogo = get_scaled_city_map(pCity);
  pBuf = create_themelabel(pLogo, pWindow->dst, NULL, pLogo->w, pLogo->h, 0);

  pCityDlg->pResource_Map = pBuf;

  pBuf->action = resource_map_city_dlg_callback;
  if (!cma_is_city_under_agent(pCity, NULL) && (pOwner == game.player_ptr)) {
    set_wstate(pBuf, FC_WS_NORMAL);
  }
  pBuf->size.x =
      pWindow->size.x + (pWindow->size.w - pBuf->size.w) / 2 - 1;
  pBuf->size.y = pWindow->size.y + 87 + (134 - pBuf->size.h) / 2;
  add_to_gui_list(ID_CITY_DLG_RESOURCE_MAP, pBuf);  
  /* -------- */
  
  if (pOwner == game.player_ptr) {
    pBuf = create_themeicon(pTheme->Options_Icon, pWindow->dst,
			  (WF_WIDGET_HAS_INFO_LABEL |
			   WF_DRAW_THEME_TRANSPARENT));
    pBuf->string16 = create_str16_from_char(_("Options panel"), 12);
    pBuf->action = options_city_dlg_callback;
    pBuf->size.x =
      pWindow->size.x + pWindow->size.w - 6 * pBuf->size.w - 10 - 10;
    pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 9;
    set_wstate(pBuf, FC_WS_NORMAL);
    add_to_gui_list(ID_CITY_DLG_OPTIONS_BUTTON, pBuf);
    /* -------- */
  
    pBuf = create_themeicon(pTheme->PROD_Icon, pWindow->dst,
			  (WF_WIDGET_HAS_INFO_LABEL |
			   WF_DRAW_THEME_TRANSPARENT));
    pBuf->string16 = create_str16_from_char(_("Change Production"), 12);
    pBuf->action = change_prod_dlg_callback;
    pBuf->size.x = pWindow->size.x + 10;
    pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 9;
    set_wstate(pBuf, FC_WS_NORMAL);
    pBuf->key = SDLK_c;
    add_to_gui_list(ID_CITY_DLG_CHANGE_PROD_BUTTON, pBuf);
    /* -------- */
  
    pBuf = create_themeicon(pTheme->Buy_PROD_Icon, pWindow->dst,
			  (WF_WIDGET_HAS_INFO_LABEL |
			   WF_DRAW_THEME_TRANSPARENT));
    pBuf->string16 = create_str16_from_char(_("Hurry production"), 12);
    pBuf->action = buy_prod_city_dlg_callback;
    pBuf->size.x = pWindow->size.x + 10 + (pBuf->size.w + 2);
    pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 9;
    pCityDlg->pBuy_Button = pBuf;
    pBuf->key = SDLK_h;
    if (!pCity->did_buy) {
      set_wstate(pBuf, FC_WS_NORMAL);
    }
    add_to_gui_list(ID_CITY_DLG_PROD_BUY_BUTTON, pBuf);
    /* -------- */
  
    pBuf = create_themeicon(pTheme->CMA_Icon, pWindow->dst,
			  (WF_WIDGET_HAS_INFO_LABEL |
			   WF_DRAW_THEME_TRANSPARENT));
    pBuf->string16 = create_str16_from_char(_("Citizen Management Agent"), 12);
    pBuf->action = cma_city_dlg_callback;
    pBuf->key = SDLK_a;
    pBuf->size.x = pWindow->size.x + 10 + (pBuf->size.w + 2) * 2;
    pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 9;
    set_wstate(pBuf, FC_WS_NORMAL);
    add_to_gui_list(ID_CITY_DLG_CMA_BUTTON, pBuf);
  
  
    /* -------- */
    pBuf = create_themeicon(pTheme->L_ARROW_Icon, pWindow->dst,
			  (WF_WIDGET_HAS_INFO_LABEL |
			   WF_DRAW_THEME_TRANSPARENT));

    pBuf->string16 = create_str16_from_char(_("Prev city"), 12);
    pBuf->action = next_prev_city_dlg_callback;
    pBuf->size.x = pWindow->size.x + 220 - pBuf->size.w - 5;
    pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 2;
    set_wstate(pBuf, FC_WS_NORMAL);
    pBuf->key = SDLK_LEFT;
    pBuf->mod = KMOD_LSHIFT;
    add_to_gui_list(ID_CITY_DLG_PREV_BUTTON, pBuf);
    /* -------- */
    
    pBuf = create_themeicon(pTheme->R_ARROW_Icon, pWindow->dst,
			  (WF_WIDGET_HAS_INFO_LABEL |
			   WF_DRAW_THEME_TRANSPARENT));
    pBuf->string16 = create_str16_from_char(_("Next city"), 12);
    pBuf->action = next_prev_city_dlg_callback;
    pBuf->size.x = pWindow->size.x + 420 + 5;
    pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 2;
    set_wstate(pBuf, FC_WS_NORMAL);
    pBuf->key = SDLK_RIGHT;
    pBuf->mod = KMOD_LSHIFT;
    add_to_gui_list(ID_CITY_DLG_NEXT_BUTTON, pBuf);
    /* -------- */
    
    pBuf = create_edit_from_chars(NULL, pWindow->dst, pCity->name,
				10, 200, WF_DRAW_THEME_TRANSPARENT);
    pBuf->action = new_name_city_dlg_callback;
    pBuf->size.x = pWindow->size.x + (pWindow->size.w - pBuf->size.w) / 2;
    pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 5;
    set_wstate(pBuf, FC_WS_NORMAL);
  
    pCityDlg->pCity_Name_Edit = pBuf;
    add_to_gui_list(ID_CITY_DLG_NAME_EDIT, pBuf);
  } else {
    pCityDlg->pCity_Name_Edit = NULL;
    pCityDlg->pBuy_Button = NULL;
  }
  
  pCityDlg->pBeginCityWidgetList = pBuf;
  
  /* check if Citizen Icons style was loaded */
  cs = get_city_style(pCity);

  if (cs != pIcons->style) {
    reload_citizens_icons(cs);
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

/**************************************************************************
  Close the dialog for the given city.
**************************************************************************/
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
  Close all cities dialogs.
**************************************************************************/
void popdown_all_city_dialogs(void)
{
    
  if (pCityDlg) {

    if (pCityDlg->pImprv->pEndWidgetList) {
      del_group_of_widgets_from_gui_list(pCityDlg->pImprv->pBeginWidgetList,
					 pCityDlg->pImprv->pEndWidgetList);
    }
    FREE(pCityDlg->pImprv->pScroll);
    FREE(pCityDlg->pImprv);

    if (pCityDlg->pPanel) {
      del_group_of_widgets_from_gui_list(pCityDlg->pPanel->pBeginWidgetList,
					 pCityDlg->pPanel->pEndWidgetList);
      FREE(pCityDlg->pPanel->pScroll);
      FREE(pCityDlg->pPanel);
    }
    
    if (pHurry_Prod_Dlg)
    {
      del_group_of_widgets_from_gui_list(pHurry_Prod_Dlg->pBeginWidgetList,
			      		 pHurry_Prod_Dlg->pEndWidgetList);

      FREE( pHurry_Prod_Dlg );
    }
    
    free_city_units_lists();
    del_city_menu_dlg(FALSE);
    popdown_window_group_dialog(pCityDlg->pBeginCityWidgetList,
				pCityDlg->pEndCityWidgetList);
    FREE(pCityDlg);
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
    free_city_units_lists();
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
