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
                          mapctrl.c  -  description
                             -------------------
    begin                : Thu Sep 05 2002
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafa³ Bursig <bursig@poczta.fm>
 **********************************************************************/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>

#include <SDL/SDL.h>

#include "fcintl.h"
#include "game.h"
#include "map.h"
#include "player.h"
#include "support.h"
#include "unit.h"

#include "gui_mem.h"

#include "graphics.h"
#include "gui_string.h"
#include "gui_stuff.h"
#include "gui_id.h"
#include "gui_zoom.h"
#include "gui_main.h"
#include "gui_tilespec.h"

#include "chatline.h"
#include "citydlg.h"
#include "civclient.h"
#include "clinet.h"
#include "climisc.h"
#include "colors.h"
#include "control.h"
#include "dialogs.h"
#include "goto.h"
#include "options.h"

#include "repodlgs.h"
#include "finddlg.h"

#include "inputdlg.h"
#include "mapview.h"
#include "messagewin.h"
#include "menu.h"
#include "tilespec.h"
#include "cma_core.h"

#include "mapctrl.h"

/* New City Dialog */
static struct SMALL_DLG *pNewCity_Dlg = NULL;
  
#define MINI_MAP_W		196
#define MINI_MAP_H		106

#define HIDDEN_MINI_MAP_W	36

#define UNITS_W			196
#define UNITS_H			106

#define HIDDEN_UNITS_W		36

bool draw_goto_patrol_lines = FALSE;
static struct GUI *pNew_Turn_Button = NULL;
static struct GUI *pUnits_Info_Window = NULL;
static struct GUI *pMiniMap_Window = NULL;
static struct GUI *pFind_City_Button = NULL;
static struct GUI *pRevolution_Button = NULL;
static struct GUI *pTax_Button = NULL;
static struct GUI *pResearch_Button = NULL;
  
/**************************************************************************
  ...
**************************************************************************/
static int find_city_callback(struct GUI *pButton)
{
  set_wstate(pButton, WS_DISABLED);
  redraw_icon(pButton);
  sdl_dirty_rect(pButton->size);
  popup_find_dialog();
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int end_turn_callback(struct GUI *pButton)
{
  redraw_icon(pButton);
  flush_rect(pButton->size);
  disable_focus_animation();
  key_end_turn();
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int revolution_callback(struct GUI *pButton)
{
  set_wstate(pButton, WS_DISABLED);
  redraw_icon2(pButton);
  sdl_dirty_rect(pButton->size);
  popup_revolution_dialog();
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int research_callback(struct GUI *pButton)
{
  popup_science_dialog(TRUE);
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int economy_callback(struct GUI *pButton)
{
  popup_economy_report_dialog(FALSE);
  return -1;
}

/* ====================================== */

/**************************************************************************
  Show/Hide Units Info Window
**************************************************************************/
static int toggle_unit_info_window_callback(struct GUI *pIcon_Widget)
{
  struct unit *pFocus = get_unit_in_focus();
  struct GUI *pBuf = pUnits_Info_Window;

  SDL_BlitSurface(pTheme->UNITS_Icon, NULL, pIcon_Widget->theme, NULL);

  if (pFocus) {
    undraw_order_widgets();
  }
  
  if (SDL_Client_Flags & CF_UNIT_INFO_SHOW) {
    /* HIDE */
    SDL_Surface *pBuf_Surf;
    SDL_Rect src, window_area;
    
    /* clear area under old map window */
    SDL_FillRect(pBuf->dst, &pBuf->size , 0x0);
    
    /* new button direction */
    SDL_BlitSurface(pTheme->L_ARROW_Icon, NULL, pIcon_Widget->theme, NULL);

    sdl_dirty_rect(pBuf->size);

    FREE(pIcon_Widget->string16->text);
    pIcon_Widget->string16->text = convert_to_utf16(_("Show Unit Info Window"));
    
    SDL_Client_Flags &= ~CF_UNIT_INFO_SHOW;

    set_new_units_window_pos();

    window_area = pBuf->size;
    /* blit part of map window */
    src.x = 0;
    src.y = 0;
    src.w = HIDDEN_UNITS_W;
    src.h = pBuf->theme->h;
      
    SDL_BlitSurface(pBuf->theme, &src , pBuf->dst, &window_area);
  
    /* blit right vertical frame */
    pBuf_Surf = ResizeSurface(pTheme->FR_Vert, pTheme->FR_Vert->w,
				pBuf->size.h - DOUBLE_FRAME_WH + 2, 1);

    window_area.y += 2;
    window_area.x = Main.gui->w - FRAME_WH;
    SDL_BlitSurface(pBuf_Surf, NULL , pBuf->dst, &window_area);
    FREESURFACE(pBuf_Surf);

    /* redraw widgets */
    
    /* ID_ECONOMY */
    pBuf = pBuf->prev;
    real_redraw_icon2(pBuf);

    /* ===== */
    /* ID_RESEARCH */
    pBuf = pBuf->prev;
    real_redraw_icon2(pBuf);
    
    /* ===== */
    /* ID_REVOLUTION */
    pBuf = pBuf->prev;
    real_redraw_icon2(pBuf);

    /* ===== */
    /* ID_TOGGLE_UNITS_WINDOW_BUTTON */
    pBuf = pBuf->prev;
    real_redraw_icon(pBuf);
    
  } else {
    /* SHOW */

    FREE(pIcon_Widget->string16->text);
    pIcon_Widget->string16->text = convert_to_utf16(_("Hide Unit Info Window"));
    
    SDL_BlitSurface(pTheme->R_ARROW_Icon, NULL, pIcon_Widget->theme, NULL);

    SDL_Client_Flags |= CF_UNIT_INFO_SHOW;

    set_new_units_window_pos();

    sdl_dirty_rect(pBuf->size);
    
    redraw_unit_info_label(pFocus);
  }
  
  if (pFocus) {
    update_order_widget();
  }

  flush_dirty();
  
  return -1;
}

/**************************************************************************
  Show/Hide Mini Map
**************************************************************************/
static int toggle_map_window_callback(struct GUI *pMap_Button)
{
  struct unit *pFocus = get_unit_in_focus();
  struct GUI *pMap = pMiniMap_Window;
    
  /* make new map icon */
  SDL_BlitSurface(pTheme->MAP_Icon, NULL, pMap_Button->theme, NULL);

  set_wstate(pMap, WS_NORMAL);

  if (pFocus) {
    undraw_order_widgets();
  }
  
  if (SDL_Client_Flags & CF_MINI_MAP_SHOW) {
    /* Hide MiniMap */
    SDL_Surface *pBuf_Surf;
    SDL_Rect src, map_area = pMap->size;

    sdl_dirty_rect(pMap->size);
    
    FREE(pMap_Button->string16->text);
    pMap_Button->string16->text = convert_to_utf16(_("Show MiniMap"));
    
    /* make new map icon */
    SDL_BlitSurface(pTheme->R_ARROW_Icon, NULL, pMap_Button->theme, NULL);

    SDL_Client_Flags &= ~CF_MINI_MAP_SHOW;
    
    /* clear area under old map window */
    SDL_FillRect(pMap->dst, &map_area , 0x0);
        
    pMap->size.w = HIDDEN_MINI_MAP_W;

    set_new_mini_map_window_pos();
    
    /* blit part of map window */
    src.x = pMap->theme->w - HIDDEN_MINI_MAP_W;
    src.y = 0;
    src.w = HIDDEN_MINI_MAP_W;
    src.h = pMap->theme->h;
      
    SDL_BlitSurface(pMap->theme, &src , pMap->dst, &map_area);
  
    /* blit left vertical frame theme */
    pBuf_Surf = ResizeSurface(pTheme->FR_Vert, pTheme->FR_Vert->w,
				pMap->size.h - DOUBLE_FRAME_WH + 2, 1);

    map_area.y += 2;
    SDL_BlitSurface(pBuf_Surf, NULL , pMap->dst, &map_area);
    FREESURFACE(pBuf_Surf);
  
    /* redraw widgets */  
    /* ID_NEW_TURN */
    pMap = pMap->prev;
    real_redraw_icon(pMap);

    /* ID_CHATLINE_TOGGLE_LOG_WINDOW_BUTTON */
    pMap = pMap->prev;
    real_redraw_icon(pMap);

    /* ID_FIND_CITY */
    pMap = pMap->prev;
    real_redraw_icon(pMap);

    /* ID_TOGGLE_MAP_WINDOW_BUTTON */
    pMap = pMap->prev;
    real_redraw_icon(pMap);
  
  } else {
    /* show MiniMap */
    FREE(pMap_Button->string16->text);
    pMap_Button->string16->text = convert_to_utf16(_("Hide MiniMap"));
    
    SDL_BlitSurface(pTheme->L_ARROW_Icon, NULL, pMap_Button->theme, NULL);
    SDL_Client_Flags |= CF_MINI_MAP_SHOW;
    pMap->size.w = MINI_MAP_W;
    set_new_mini_map_window_pos();
    
    refresh_overview_viewrect();
    sdl_dirty_rect(pMap->size);
  }

  if (pFocus) {
    update_order_widget();
  }

  flush_dirty();
  return -1;
}

/* =================== New City Dialog ========================= */

/**************************************************************************
  ...
**************************************************************************/
static int newcity_ok_callback(struct GUI *pOk_Button)
{
  struct packet_unit_request req;
  char *input =
	  convert_to_chars(pNewCity_Dlg->pBeginWidgetList->string16->text);

  req.unit_id = ((struct unit *)pOk_Button->data)->id;
  sz_strlcpy(req.name, input);
  send_packet_unit_request(&aconnection, &req, PACKET_UNIT_BUILD_CITY);
  FREE(input);

  popdown_window_group_dialog(pNewCity_Dlg->pBeginWidgetList,
			      pNewCity_Dlg->pEndWidgetList);
  FREE(pNewCity_Dlg);
  flush_dirty();
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int newcity_cancel_callback(struct GUI *pCancel_Button)
{
  popdown_window_group_dialog(pNewCity_Dlg->pBeginWidgetList,
			      pNewCity_Dlg->pEndWidgetList);
  FREE(pNewCity_Dlg);
  flush_dirty();
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int move_new_city_dlg_callback(struct GUI *pWindow)
{
  return std_move_window_group_callback(pNewCity_Dlg->pBeginWidgetList, pWindow);
}


static int togle_msg_window(struct GUI *pWidget)
{
  
  if(is_meswin_open()) {
    popdown_meswin_dialog();
    FREE(pWidget->string16->text);
    pWidget->string16->text = convert_to_utf16(_("Show Log (F10)"));
  } else {
    popup_meswin_dialog();
    FREE(pWidget->string16->text);
    pWidget->string16->text = convert_to_utf16(_("Hide Log (F10)"));
  }

  pSellected_Widget = pWidget;
  set_wstate(pWidget, WS_SELLECTED);
  real_redraw_icon(pWidget);
  sdl_dirty_rect(pWidget->size);
  
  flush_dirty();
  return -1;
}


/* ============================== Public =============================== */

/**************************************************************************
  This Function is used when resize Main.screen.
  We must set new Units Info Win. start position.
**************************************************************************/
void set_new_units_window_pos(void)
{
  int new_x;
  struct GUI *pUnit_Window = pUnits_Info_Window;
    
  if (SDL_Client_Flags & CF_UNIT_INFO_SHOW) {
    pUnit_Window->size.x = Main.screen->w - pUnit_Window->size.w;
  } else {
    pUnit_Window->size.x = Main.screen->w - 30 - DOUBLE_FRAME_WH;
  }

  pUnit_Window->size.y = Main.screen->h - pUnit_Window->size.h;
  new_x = pUnit_Window->size.x + FRAME_WH + 2;
  pUnit_Window->dst = Main.gui;
  
  /* ID_ECONOMY */
  pUnit_Window = pUnit_Window->prev;
  pUnit_Window->size.x = new_x + 3;
  pUnit_Window->size.y = Main.screen->h - UNITS_H + FRAME_WH + 2;
  pUnit_Window->dst = Main.gui;
  
  /* ID_RESEARCH */
  pUnit_Window = pUnit_Window->prev;
  pUnit_Window->size.x = new_x + 3;
  pUnit_Window->size.y = Main.screen->h - UNITS_H + FRAME_WH + 2 +
  						    pUnit_Window->size.h;
  pUnit_Window->dst = Main.gui;
  
  /* ID_REVOLUTION */
  pUnit_Window = pUnit_Window->prev;
  pUnit_Window->size.x = new_x + 3;
  pUnit_Window->size.y = Main.screen->h - UNITS_H + FRAME_WH + 2 +
      						(pUnit_Window->size.h << 1);
  pUnit_Window->dst = Main.gui;
  
  /* ID_TOGGLE_UNITS_WINDOW_BUTTON */
  pUnit_Window = pUnit_Window->prev;
  pUnit_Window->size.x = new_x;
  pUnit_Window->size.y = Main.screen->h - FRAME_WH - pUnit_Window->size.h - 2;
  pUnit_Window->dst = Main.gui;
}

/**************************************************************************
  This Function is used when resize Main.screen.
  We must set new MiniMap start position.
**************************************************************************/
void set_new_mini_map_window_pos(void)
{
  int new_x;
  struct GUI *pMM_Window = pMiniMap_Window;
    
  if (SDL_Client_Flags & CF_MINI_MAP_SHOW) {
    new_x = 166;
  } else {
    new_x = FRAME_WH + 3;
  }

  pMM_Window->size.y = Main.screen->h - pMM_Window->size.h;
  pMM_Window->dst = Main.gui;
  
  /* ID_NEW_TURN */
  pMM_Window = pMM_Window->prev;
  pMM_Window->size.x = new_x;
  pMM_Window->size.y = Main.screen->h - MINI_MAP_H + FRAME_WH + 2;
  pMM_Window->dst = Main.gui;
  
  /* ID_CHATLINE_TOGGLE_LOG_WINDOW_BUTTON */
  pMM_Window = pMM_Window->prev;
  pMM_Window->size.x = new_x;
  pMM_Window->size.y = Main.screen->h - MINI_MAP_H + FRAME_WH + 2 +
      						pMM_Window->size.h;
  pMM_Window->dst = Main.gui;
  
  /* ID_FIND_CITY */
  pMM_Window = pMM_Window->prev;
  pMM_Window->size.x = new_x;
  pMM_Window->size.y = Main.screen->h - MINI_MAP_H + FRAME_WH + 2 +
      						(pMM_Window->size.h << 1);
  pMM_Window->dst = Main.gui;
  
  /* ID_TOGGLE_MAP_WINDOW_BUTTON */
  pMM_Window = pMM_Window->prev;
  pMM_Window->size.x = new_x;
  pMM_Window->size.y = Main.screen->h - FRAME_WH - pMM_Window->size.h - 2;
  pMM_Window->dst = Main.gui;
}

/**************************************************************************
  Init MiniMap window and Unit's Info Window.
**************************************************************************/
void Init_MapView(void)
{
  SDL_Rect area = {FRAME_WH + 30, FRAME_WH ,
		    UNITS_W - 30 - DOUBLE_FRAME_WH,
    		    UNITS_H - DOUBLE_FRAME_WH};
  SDL_Surface *pIcon_theme = NULL;
		    
  /* =================== Units Window ======================= */
  struct GUI *pBuf = create_window(Main.gui, create_string16(NULL, 12), UNITS_W,
				   UNITS_H, WF_DRAW_THEME_TRANSPARENT);

  pBuf->size.x = Main.screen->w - UNITS_W;
  pBuf->size.y = Main.screen->h - UNITS_H;
  
  pIcon_theme = create_surf(UNITS_W , UNITS_H , SDL_SWSURFACE);
  pBuf->theme = SDL_DisplayFormatAlpha(pIcon_theme);
  FREESURFACE(pIcon_theme);
     
  draw_frame(pBuf->theme, 0, 0, pBuf->size.w, pBuf->size.h);
  
  pIcon_theme = ResizeSurface(pTheme->Block, 30,
					pBuf->size.h - DOUBLE_FRAME_WH, 1);
  
  blit_entire_src(pIcon_theme , pBuf->theme , FRAME_WH , FRAME_WH);
  FREESURFACE(pIcon_theme);
  
  SDL_FillRect(pBuf->theme, &area,
		  SDL_MapRGBA(pBuf->theme->format, 255, 255, 255, 128));
  
  SDL_SetAlpha(pBuf->theme , 0x0 , 0x0);/* turn off alpha chanel */
  
  pBuf->string16->style |= (SF_CENTER);
  pBuf->string16->render = 3;
  
  pBuf->string16->backcol.r = 255;
  pBuf->string16->backcol.g = 255;
  pBuf->string16->backcol.b = 255;
  pBuf->string16->backcol.unused = 128;
  
  add_to_gui_list(ID_UNITS_WINDOW, pBuf);
  pUnits_Info_Window = pBuf;

  /* economy button */
  pBuf = create_icon2(NULL, Main.gui,
		WF_FREE_GFX | WF_FREE_THEME | WF_WIDGET_HAS_INFO_LABEL);
  pBuf->string16 = create_str16_from_char(_("Economy (F5)"), 12);
  
  pBuf->size.x = Main.screen->w - UNITS_W + FRAME_WH + 5;
  pBuf->size.y = Main.screen->h - UNITS_H + FRAME_WH + 2;

  pBuf->action = economy_callback;
  pBuf->key = SDLK_F5;
  
  add_to_gui_list(ID_ECONOMY, pBuf);
  pTax_Button = pBuf;

  /* research button */
  pBuf = create_icon2(NULL, Main.gui, WF_FREE_GFX | WF_FREE_THEME
						  | WF_WIDGET_HAS_INFO_LABEL);
  pBuf->string16 = create_str16_from_char(_("Research (F6)"), 12);
  pBuf->size.w = 19;
  pBuf->size.h = 24;

  pBuf->size.x = Main.screen->w - UNITS_W + FRAME_WH + 5;
  pBuf->size.y = Main.screen->h - UNITS_H + FRAME_WH + 2 + pBuf->size.h;

  pBuf->action = research_callback;
  pBuf->key = SDLK_F6;

  add_to_gui_list(ID_RESEARCH, pBuf);
  pResearch_Button = pBuf;

  /* revolution button */
  pBuf = create_icon2(NULL, Main.gui, WF_FREE_GFX | WF_FREE_THEME
						  | WF_WIDGET_HAS_INFO_LABEL);
  pBuf->string16 = create_str16_from_char(_("Revolution (Shift + R)"), 12);
  pBuf->size.w = 19;
  pBuf->size.h = 24;

  pBuf->size.x = Main.screen->w - UNITS_W + FRAME_WH + 5;
  pBuf->size.y =
      Main.screen->h - UNITS_H + FRAME_WH + 2 + (pBuf->size.h << 1);

  pBuf->action = revolution_callback;
  pBuf->key = SDLK_r;
  pBuf->mod = KMOD_SHIFT;

  add_to_gui_list(ID_REVOLUTION, pBuf);
  pRevolution_Button = pBuf;
  
  /* show/hide unit's window button */

  /* make UNITS Icon */
  pIcon_theme = create_surf(pTheme->UNITS_Icon->w,
			    pTheme->UNITS_Icon->h, SDL_SWSURFACE);
  SDL_BlitSurface(pTheme->UNITS_Icon, NULL, pIcon_theme, NULL);
  SDL_BlitSurface(pTheme->L_ARROW_Icon, NULL, pIcon_theme, NULL);
  SDL_SetColorKey(pIcon_theme, SDL_SRCCOLORKEY, 0x0);

  SDL_BlitSurface(pTheme->R_ARROW_Icon, NULL, pIcon_theme, NULL);

  pBuf = create_themeicon(pIcon_theme, Main.gui,
			  WF_FREE_GFX | WF_FREE_THEME |
		WF_DRAW_THEME_TRANSPARENT | WF_WIDGET_HAS_INFO_LABEL);

  pBuf->string16 = create_str16_from_char(_("Hide Unit Info Window"), 12);
  pBuf->size.x = Main.screen->w - UNITS_W + FRAME_WH + 2;
  pBuf->size.y = Main.screen->h - FRAME_WH - pBuf->size.h - 2;

  pBuf->action = toggle_unit_info_window_callback;
  add_to_gui_list(ID_TOGGLE_UNITS_WINDOW_BUTTON, pBuf);

  /* ========================= Mini map ========================== */

  pBuf = create_window(Main.gui, NULL, MINI_MAP_W, MINI_MAP_H, 0);
  pBuf->size.x = 0;
  pBuf->size.y = pBuf->dst->h - MINI_MAP_H;
  
  pIcon_theme = create_surf(MINI_MAP_W , MINI_MAP_H , SDL_SWSURFACE);
  pBuf->theme = SDL_DisplayFormatAlpha(pIcon_theme);
  FREESURFACE(pIcon_theme);
     
  draw_frame(pBuf->theme, 0, 0, pBuf->size.w, pBuf->size.h);
  
  pIcon_theme = ResizeSurface(pTheme->Block, 30,
					pBuf->size.h - DOUBLE_FRAME_WH, 1);
  
  blit_entire_src(pIcon_theme , pBuf->theme ,
			pBuf->size.w - FRAME_WH - pIcon_theme->w, FRAME_WH);
  FREESURFACE(pIcon_theme);  
  
  SDL_SetAlpha(pBuf->theme , 0x0 , 0x0);
  
  add_to_gui_list(ID_MINI_MAP_WINDOW, pBuf);
  pMiniMap_Window = pBuf;

  /* new turn button */
  pBuf = create_themeicon(pTheme->NEW_TURN_Icon, Main.gui,
			  WF_WIDGET_HAS_INFO_LABEL |
			  WF_DRAW_THEME_TRANSPARENT);

  pBuf->string16 = create_str16_from_char(_("End Turn (Enter)"), 12);

  pBuf->action = end_turn_callback;
  pBuf->key = SDLK_RETURN;
  pBuf->mod = KMOD_LSHIFT;
  
  pBuf->size.x = 166;
  pBuf->size.y = pBuf->dst->h - MINI_MAP_H + FRAME_WH + 2;
  pNew_Turn_Button = pBuf;

  add_to_gui_list(ID_NEW_TURN, pBuf);

  /* show/hide log window button */
  pBuf = create_themeicon(pTheme->LOG_Icon, Main.gui,
						  WF_WIDGET_HAS_INFO_LABEL);
  pBuf->string16 = create_str16_from_char(_("Hide Log (F10)"), 12);
  pBuf->action = togle_msg_window;
  pBuf->key = SDLK_F10;
  add_to_gui_list(ID_CHATLINE_TOGGLE_LOG_WINDOW_BUTTON, pBuf);


  pBuf->size.x = 166;
  pBuf->size.y = pBuf->dst->h - MINI_MAP_H + FRAME_WH + 2 + pBuf->size.h;

  /* find city button */
  pBuf = create_themeicon(pTheme->FindCity_Icon, Main.gui,
						  WF_WIDGET_HAS_INFO_LABEL);
  pBuf->string16 = create_str16_from_char(_("Find City (Shift + F)"), 12);
  
  pBuf->size.x = 166;
  pBuf->size.y = pBuf->dst->h - MINI_MAP_H + FRAME_WH + 2 + pBuf->size.h * 2;

  pBuf->action = find_city_callback;
  pBuf->key = SDLK_f;
  pBuf->mod = KMOD_SHIFT;

  add_to_gui_list(ID_FIND_CITY, pBuf);
  pFind_City_Button = pBuf;
  
  /* show/hide minimap button */

  /* make Map Icon */
  pIcon_theme =
      create_surf(pTheme->MAP_Icon->w, pTheme->MAP_Icon->h, SDL_SWSURFACE);
  SDL_BlitSurface(pTheme->MAP_Icon, NULL, pIcon_theme, NULL);
  SDL_BlitSurface(pTheme->L_ARROW_Icon, NULL, pIcon_theme, NULL);
  SDL_SetColorKey(pIcon_theme, SDL_SRCCOLORKEY, 0x0);

  pBuf = create_themeicon(pIcon_theme, Main.gui,
			  WF_FREE_GFX | WF_FREE_THEME |
		WF_DRAW_THEME_TRANSPARENT | WF_WIDGET_HAS_INFO_LABEL);

  pBuf->string16 = create_str16_from_char(_("Hide MiniMap"), 12);
  pBuf->size.x = 166;
  pBuf->size.y = pBuf->dst->h - FRAME_WH - pBuf->size.h - 2;

  pBuf->action = toggle_map_window_callback;
  add_to_gui_list(ID_TOGGLE_MAP_WINDOW_BUTTON, pBuf);

  /* ========================= Cooling/Warming ========================== */

  /* cooling icon */
  pIcon_theme = GET_SURF(sprites.cooling[0]);
  assert(pIcon_theme != NULL);
  pBuf = create_iconlabel(pIcon_theme, Main.gui, NULL, 0);

  pBuf->size.x = pBuf->dst->w - 10 - pBuf->size.w;
  pBuf->size.y = 10;

  add_to_gui_list(ID_COOLING_ICON, pBuf);

  /* warming icon */
  pIcon_theme = GET_SURF(sprites.warming[0]);
  assert(pIcon_theme != NULL);
  pBuf = create_iconlabel(pIcon_theme, Main.gui, NULL, 0);

  pBuf->size.x = pBuf->dst->w - 10 - pBuf->size.w * 2;
  pBuf->size.y = 10;

  add_to_gui_list(ID_WARMING_ICON, pBuf);

  /* ================================ */
  
  tmp_map_surfaces_init();

  SDL_Client_Flags |= (CF_REVOLUTION | CF_MAP_UNIT_W_CREATED | 
  	CF_CIV3_CITY_TEXT_STYLE | CF_UNIT_INFO_SHOW |
		CF_MINI_MAP_SHOW | CF_DRAW_MAP_DITHER);
}

struct GUI * get_unit_info_window_widget(void)
{
  return pUnits_Info_Window;
}

struct GUI * get_minimap_window_widget(void)
{
  return pMiniMap_Window;
}

struct GUI * get_tax_rates_widget(void)
{
  return pTax_Button;
}

struct GUI * get_research_widget(void)
{
  return pResearch_Button;
}

struct GUI * get_revolution_widget(void)
{
  return pRevolution_Button;
}

void enable_and_redraw_find_city_button(void)
{
  set_wstate(pFind_City_Button, WS_NORMAL);
  redraw_icon(pFind_City_Button);
  sdl_dirty_rect(pFind_City_Button->size);
}

void enable_and_redraw_revolution_button(void)
{
  set_wstate(pRevolution_Button, WS_NORMAL);
  redraw_icon2(pRevolution_Button);
  sdl_dirty_rect(pRevolution_Button->size);
}

/**************************************************************************
  mouse click handler
**************************************************************************/
void button_down_on_map(SDL_MouseButtonEvent * pButtonEvent)
{
  int col, row;

  if (get_client_state() != CLIENT_GAME_RUNNING_STATE) {
    return;
  }
  
#if 0
  if (ev->button == 1 && (ev->state & GDK_SHIFT_MASK)) {
    adjust_workers(w, ev);
    return TRUE;
  }
#endif
  
  draw_goto_patrol_lines = FALSE;
  
  if (!LSHIFT && pButtonEvent->button == SDL_BUTTON_LEFT) {
    action_button_pressed(pButtonEvent->x, pButtonEvent->y);
  } else {
    if (pButtonEvent->button == SDL_BUTTON_MIDDLE ||
      (LSHIFT && pButtonEvent->button == SDL_BUTTON_LEFT)) {
      if (canvas_to_map_pos(&col, &row,
			    (int) pButtonEvent->x, (int) pButtonEvent->y)) {
        popup_advanced_terrain_dialog(col, row);
      }
    } else {
      recenter_button_pressed(pButtonEvent->x, pButtonEvent->y);
      flush_dirty();
    }
  }
}

/**************************************************************************
  Toggle map drawing stuff.
**************************************************************************/
int map_event_handler(SDL_keysym Key)
{
  if (get_client_state() == CLIENT_GAME_RUNNING_STATE &&
      !(SDL_Client_Flags & CF_OPTION_OPEN) &&
      (Key.mod & KMOD_LCTRL || Key.mod & KMOD_RCTRL)) {
    switch (Key.sym) {
    case SDLK_g:
      rebuild_focus_anim_frames();
      request_toggle_map_grid();
      return 0;

    case SDLK_n:
      if (!can_client_change_view()) {
    	return 0;
      }

      draw_city_names ^= 1;
      if(draw_city_names||draw_city_productions) {
        show_city_descriptions();
      } else {
	prepare_show_city_descriptions();
      }
      dirty_all();
      return 0;

    case SDLK_p:
      if (!can_client_change_view()) {
    	return 0;
      }

      draw_city_productions ^= 1;
      if(draw_city_names||draw_city_productions) {
        show_city_descriptions();
      } else {
	prepare_show_city_descriptions();
      }
      dirty_all();
      return 0;

    case SDLK_t:
      rebuild_focus_anim_frames();
      request_toggle_terrain();
      return 0;

    case SDLK_r:
      rebuild_focus_anim_frames();
      request_toggle_roads_rails();
      return 0;

    case SDLK_i:
      rebuild_focus_anim_frames();
      request_toggle_irrigation();
      return 0;

    case SDLK_m:
      rebuild_focus_anim_frames();
      request_toggle_mines();
      return 0;

    case SDLK_f:
      rebuild_focus_anim_frames();
      request_toggle_fortress_airbase();
      return 0;

    case SDLK_s:
      rebuild_focus_anim_frames();
      request_toggle_specials();
      return 0;

    case SDLK_o:
      rebuild_focus_anim_frames();
      request_toggle_pollution();
      return 0;

    case SDLK_c:
      rebuild_focus_anim_frames();
      request_toggle_cities();
      return 0;

    case SDLK_u:
      rebuild_focus_anim_frames();
      request_toggle_units();
      return 0;

    case SDLK_w:
      rebuild_focus_anim_frames();
      request_toggle_fog_of_war();
      return 0;

    default:
      break;
    }
  }

  return 1;
}


/* ============================== Native =============================== */

  
/**************************************************************************
  Popup a dialog to ask for the name of a new city.  The given string
  should be used as a suggestion.
**************************************************************************/
void popup_newcity_dialog(struct unit *pUnit, char *pSuggestname)
{
  SDL_Surface *pLogo;
  struct SDL_String16 *pStr = NULL;
  struct GUI *pLabel = NULL;
  struct GUI *pWindow = NULL;
  struct GUI *pCancel_Button = NULL;
  struct GUI *pOK_Button;
  struct GUI *pEdit;

  if(pNewCity_Dlg) {
    return;
  }
  
  pNewCity_Dlg = MALLOC(sizeof(struct SMALL_DLG));
    
  /* create ok button */
  pLogo = ZoomSurface(pTheme->OK_Icon, 0.7, 0.7, 1);
  pOK_Button =
    create_themeicon_button_from_chars(pLogo, Main.gui,
					  _("OK"), 10, WF_FREE_GFX);
  SDL_SetColorKey(pLogo, SDL_SRCCOLORKEY, get_first_pixel(pLogo));


  /* create cancel button */
  pLogo = ZoomSurface(pTheme->CANCEL_Icon, 0.7, 0.7, 1);
  pCancel_Button =
      create_themeicon_button_from_chars(pLogo, Main.gui, _("Cancel"), 10,
					 WF_FREE_GFX);
  SDL_SetColorKey(pLogo, SDL_SRCCOLORKEY, get_first_pixel(pLogo));

  /* create text label */
  pStr = create_str16_from_char(_("What should we call our new city?"), 10);
  pStr->style |= TTF_STYLE_BOLD;
  pStr->forecol.r = 255;
  pStr->forecol.g = 255;
  /* pStr->forecol.b = 255; */
  pLabel = create_iconlabel(NULL, Main.gui, pStr, WF_DRAW_TEXT_LABEL_WITH_SPACE);
  
  
  pEdit = create_edit(NULL, Main.gui, create_str16_from_char(pSuggestname, 12),
			180, WF_DRAW_THEME_TRANSPARENT);
  
  /* create window */
  pStr = create_str16_from_char(_("Build New City"), 12);
  pStr->style |= TTF_STYLE_BOLD;
  pWindow = create_window(Main.gui, pStr, pEdit->size.w + 20, pEdit->size.h +
			  pOK_Button->size.h + pLabel->size.h +
			  WINDOW_TILE_HIGH + 25, 0);


  /* I make this hack to center label on window */
  if (pLabel->size.w < pWindow->size.w)
  {
    pLabel->size.w = pWindow->size.w;
  } else { 
    pWindow->size.w = pLabel->size.w + 10;
  }
  
  pEdit->size.w = pWindow->size.w - 20;
  
  /* set actions */
  pWindow->action = move_new_city_dlg_callback;
  pCancel_Button->action = newcity_cancel_callback;
  pOK_Button->action = newcity_ok_callback;

  /* set keys */
  pOK_Button->key = SDLK_RETURN;

  pCancel_Button->key = SDLK_ESCAPE;
  
  
  pOK_Button->data = (void *)pUnit;
  
  /* correct sizes */
  pCancel_Button->size.w += 5;
  /*pOK_Button->size.w += 10; */
  pOK_Button->size.w = pCancel_Button->size.w;
  
  /* set start positions */
  pWindow->size.x = (Main.screen->w - pWindow->size.w) / 2;
  pWindow->size.y = (Main.screen->h - pWindow->size.h) / 2;


  pOK_Button->size.x = pWindow->size.x + 10;
  pOK_Button->size.y =
      pWindow->size.y + pWindow->size.h - pOK_Button->size.h - 10;


  pCancel_Button->size.y = pOK_Button->size.y;
  pCancel_Button->size.x = pWindow->size.x + pWindow->size.w -
      pCancel_Button->size.w - 10;

  pEdit->size.x = pWindow->size.x + 10;
  pEdit->size.y =
      pWindow->size.y + WINDOW_TILE_HIGH + 5 + pLabel->size.h + 3;

  pLabel->size.x = pWindow->size.x + FRAME_WH;
  pLabel->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 5;

  /* create window background */
  pLogo = get_logo_gfx();
  if (resize_window
      (pWindow, pLogo, NULL, pWindow->size.w, pWindow->size.h)) {
    FREESURFACE(pLogo);
  }

  /* enable widgets */
  set_wstate(pCancel_Button, WS_NORMAL);
  set_wstate(pOK_Button, WS_NORMAL);
  set_wstate(pEdit, WS_NORMAL);
  set_wstate(pWindow, WS_NORMAL);

  /* add widgets to main list */
  pNewCity_Dlg->pEndWidgetList = pWindow;
  add_to_gui_list(ID_NEWCITY_NAME_WINDOW, pWindow);
  add_to_gui_list(ID_NEWCITY_NAME_LABEL, pLabel);
  add_to_gui_list(ID_NEWCITY_NAME_CANCEL_BUTTON, pCancel_Button);
  add_to_gui_list(ID_NEWCITY_NAME_OK_BUTTON, pOK_Button);
  add_to_gui_list(ID_NEWCITY_NAME_EDIT, pEdit);
  pNewCity_Dlg->pBeginWidgetList = pEdit;

  /* redraw */
  redraw_group(pEdit, pWindow, 0);

  flush_rect(pWindow->size);
}

/**************************************************************************
  ...
**************************************************************************/
void popdown_newcity_dialog(void)
{
  if(pNewCity_Dlg) {
    popdown_window_group_dialog(pNewCity_Dlg->pBeginWidgetList,
			      pNewCity_Dlg->pEndWidgetList);
    FREE(pNewCity_Dlg);
    flush_dirty();
  }
}

/**************************************************************************
  A turn done button should be provided for the player.  This function
  is called to toggle it between active/inactive.
**************************************************************************/
void set_turn_done_button_state(bool state)
{
  if (get_client_state() == CLIENT_GAME_RUNNING_STATE) {
    if (state) {
      set_wstate(pNew_Turn_Button, WS_NORMAL);
    } else {
      set_wstate(pNew_Turn_Button, WS_DISABLED);
    }
    redraw_icon(pNew_Turn_Button);
    flush_rect(pNew_Turn_Button->size);
  }
}

/**************************************************************************
  Draw a goto or patrol line at the current mouse position.
**************************************************************************/
void create_line_at_mouse_pos(void)
{
  update_line(Main.event.motion.x, Main.event.motion.y);
  draw_goto_patrol_lines = TRUE;
}
