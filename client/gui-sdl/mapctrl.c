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
#include <SDL/SDL_ttf.h>

#include "fcintl.h"
#include "log.h"
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
#include "menu.h"
#include "tilespec.h"
#include "cma_core.h"

#include "mapctrl.h"

struct city *city_workers_display = NULL;

/* New City Dialog */
static struct GUI *pBeginNewCityWidgetList;
static struct GUI *pEndNewCityWidgetList;
static int newcity_unit_id;


#define MINI_MAP_W		196
#define MINI_MAP_H		106

#define HIDDEN_MINI_MAP_W	36

#define UNITS_W			180
#define UNITS_H			106

/**************************************************************************
  ...
**************************************************************************/
static int find_city_callback(struct GUI *pButton)
{
  redraw_icon(pButton);
  refresh_rect(pButton->size);
  popup_find_dialog();
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int end_turn_callback(struct GUI *pButton)
{
  redraw_ibutton(pButton);
  refresh_rect(pButton->size);
  key_end_turn();
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int revolution_callback(struct GUI *pButton)
{
  redraw_icon(pButton);
  refresh_rect(pButton->size);
  popup_revolution_dialog();
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int research_callback(struct GUI *pButton)
{
  redraw_icon(pButton);
  refresh_rect(pButton->size);
  popup_science_dialog(TRUE);
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int economy_callback(struct GUI *pButton)
{
  redraw_icon(pButton);
  refresh_rect(pButton->size);
  popup_taxrate_dialog();
  return -1;
}

/* ====================================== */

/**************************************************************************
  Show/Hide Units Info Window
**************************************************************************/
static int toggle_unit_info_window_callback(struct GUI *pIcon_Widget)
{
  struct unit *pFocus = get_unit_in_focus();
  struct GUI *pBuf = get_widget_pointer_form_main_list(ID_UNITS_WINDOW);

  SDL_BlitSurface(pTheme->UNITS_Icon, NULL, pIcon_Widget->theme, NULL);

  /* draw background */
  if (pBuf->gfx) {
    blit_entire_src(pBuf->gfx, Main.screen, pBuf->size.x, pBuf->size.y);
  }

  if (SDL_Client_Flags & CF_UNIT_INFO_SHOW) {
    /* HIDE */

    SDL_BlitSurface(pTheme->L_ARROW_Icon, NULL, pIcon_Widget->theme, NULL);

    add_refresh_rect(pBuf->size);

    SDL_Client_Flags &= ~CF_UNIT_INFO_SHOW;

    set_new_units_window_pos(pBuf);

  } else {
    /* SHOW */

    SDL_BlitSurface(pTheme->R_ARROW_Icon, NULL, pIcon_Widget->theme, NULL);

    SDL_Client_Flags |= CF_UNIT_INFO_SHOW;

    set_new_units_window_pos(pBuf);

    add_refresh_rect(pBuf->size);
  }

  if (pFocus) {
    undraw_order_widgets();
  }

  refresh_widget_background(pBuf);
  redraw_unit_info_label(pFocus, pBuf);

  if (pFocus) {
    update_order_widget();
  }

  refresh_rects();

  return -1;
}

/**************************************************************************
  Show/Hide Mini Map
**************************************************************************/
static int toggle_map_window_callback(struct GUI *pMap_Button)
{
  struct unit *pFocus = get_unit_in_focus();
  struct GUI *pMap = get_widget_pointer_form_main_list(ID_MINI_MAP_WINDOW);

  /* make new map icon */
  SDL_BlitSurface(pTheme->MAP_Icon, NULL, pMap_Button->theme, NULL);

  set_wstate(pMap, WS_NORMAL);

  /* clear area */
  if (pMap->gfx) {
    blit_entire_src(pMap->gfx, Main.screen, pMap->size.x, pMap->size.y);
    add_refresh_rect(pMap->size);
  }

  if (SDL_Client_Flags & CF_MINI_MAP_SHOW) {
    /* Hide MiniMap */

    /* make new map icon */
    SDL_BlitSurface(pTheme->R_ARROW_Icon, NULL, pMap_Button->theme, NULL);

    SDL_Client_Flags &= ~CF_MINI_MAP_SHOW;

    pMap->size.w = HIDDEN_MINI_MAP_W;

    set_new_mini_map_window_pos(pMap);

  } else {
    /* show MiniMap */
    SDL_BlitSurface(pTheme->L_ARROW_Icon, NULL, pMap_Button->theme, NULL);
    SDL_Client_Flags |= CF_MINI_MAP_SHOW;
    pMap->size.w = MINI_MAP_W;
    set_new_mini_map_window_pos(pMap);
  }

  if (pFocus) {
    undraw_order_widgets();
  }

  /* allocate new background area */
  FREESURFACE(pMap->gfx);
  pMap->gfx = crop_rect_from_screen(pMap->size);

  refresh_overview_viewrect();

  if (pFocus) {
    update_order_widget();
  }

  refresh_rects();

  return -1;
}

/* =================== New City Dialog ========================= */

/**************************************************************************
  ...
**************************************************************************/
static int newcity_ok_callback(struct GUI *pOk_Button)
{
  struct packet_unit_request req;
  char *input = convert_to_chars(pBeginNewCityWidgetList->string16->text);

  req.unit_id = newcity_unit_id;
  sz_strlcpy(req.name, input);
  send_packet_unit_request(&aconnection, &req, PACKET_UNIT_BUILD_CITY);
  FREE(input);

  popdown_window_group_dialog(pBeginNewCityWidgetList,
			      pEndNewCityWidgetList);
  pEndNewCityWidgetList = NULL;
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int newcity_cancel_callback(struct GUI *pCancel_Button)
{
  popdown_window_group_dialog(pBeginNewCityWidgetList,
			      pEndNewCityWidgetList);
  pEndNewCityWidgetList = NULL;
  newcity_unit_id = 0;
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int move_new_city_dlg_callback(struct GUI *pWindow)
{
  if (move_window_group_dialog(pBeginNewCityWidgetList, pWindow)) {
    refresh_rect(pWindow->size);
  }
  return -1;
}

/* ============================== Public =============================== */

/**************************************************************************
  This Function is used when resize Main.screen.
  We must set new Units Info Win. start position.
  !!! pUnit_Window must be :
  pUnit_Window = get_widget_pointer_form_main_list( ID_UNITS_WINDOW );
**************************************************************************/
void set_new_units_window_pos(struct GUI *pUnit_Window)
{
  int new_x;

  if (SDL_Client_Flags & CF_UNIT_INFO_SHOW) {
    pUnit_Window->size.x = Main.screen->w - pUnit_Window->size.w;
  } else {
    pUnit_Window->size.x = Main.screen->w - 30 - DOUBLE_FRAME_WH;
  }

  pUnit_Window->size.y = Main.screen->h - pUnit_Window->size.h;

  new_x = pUnit_Window->size.x + FRAME_WH + 2;

  /* ID_ECONOMY */
  pUnit_Window = pUnit_Window->prev;
  pUnit_Window->size.x = new_x + 3;
  pUnit_Window->size.y = Main.screen->h - UNITS_H + FRAME_WH + 2;

  /* ID_RESEARCH */
  pUnit_Window = pUnit_Window->prev;
  pUnit_Window->size.x = new_x + 3;
  pUnit_Window->size.y = Main.screen->h - UNITS_H + FRAME_WH + 2 +
      pUnit_Window->size.h;

  /* ID_REVOLUTION */
  pUnit_Window = pUnit_Window->prev;
  pUnit_Window->size.x = new_x + 3;
  pUnit_Window->size.y = Main.screen->h - UNITS_H + FRAME_WH + 2 +
      (pUnit_Window->size.h << 1);

  /* ID_TOGGLE_UNITS_WINDOW_BUTTON */
  pUnit_Window = pUnit_Window->prev;
  pUnit_Window->size.x = new_x;
  pUnit_Window->size.y =
      Main.screen->h - FRAME_WH - pUnit_Window->size.h - 2;
}

/**************************************************************************
  This Function is used when resize Main.screen.
  We must set new MiniMap start position.
  !!! pMiniMap_Window must be :
  pMiniMap_Window = get_widget_pointer_form_main_list( ID_MINI_MAP_WINDOW )
**************************************************************************/
void set_new_mini_map_window_pos(struct GUI *pMiniMap_Window)
{
  int new_x;

  if (SDL_Client_Flags & CF_MINI_MAP_SHOW) {
    new_x = 166;
  } else {
    new_x = FRAME_WH + 3;
  }

  pMiniMap_Window->size.y = Main.screen->h - pMiniMap_Window->size.h;

  /* ID_NEW_TURN */
  pMiniMap_Window = pMiniMap_Window->prev;
  pMiniMap_Window->size.x = new_x;
  pMiniMap_Window->size.y = Main.screen->h - MINI_MAP_H + FRAME_WH + 2;

  /* ID_CHATLINE_TOGGLE_LOG_WINDOW_BUTTON */
  pMiniMap_Window = pMiniMap_Window->prev;
  pMiniMap_Window->size.x = new_x;
  pMiniMap_Window->size.y = Main.screen->h - MINI_MAP_H + FRAME_WH + 2 +
      pMiniMap_Window->size.h;

  /* ID_FIND_CITY */
  pMiniMap_Window = pMiniMap_Window->prev;
  pMiniMap_Window->size.x = new_x;
  pMiniMap_Window->size.y = Main.screen->h - MINI_MAP_H + FRAME_WH + 2 +
      (pMiniMap_Window->size.h << 1);

  /* ID_TOGGLE_MAP_WINDOW_BUTTON */
  pMiniMap_Window = pMiniMap_Window->prev;
  pMiniMap_Window->size.x = new_x;
  pMiniMap_Window->size.y = Main.screen->h - FRAME_WH -
      pMiniMap_Window->size.h - 2;
}

/**************************************************************************
  Init MiniMap window and Unit's Info Window.
**************************************************************************/
void Init_MapView(void)
{
  /* =================== Units Window ======================= */
  SDL_Surface *pIcon_theme = NULL;
  struct GUI *pBuf = create_window(create_string16(NULL, 12), UNITS_W,
				   UNITS_H, WF_DRAW_THEME_TRANSPARENT);

  pBuf->size.x = Main.screen->w - UNITS_W;
  pBuf->size.y = Main.screen->h - UNITS_H;
  resize_window(pBuf, NULL, NULL, UNITS_W, UNITS_H);
  FREESURFACE(pBuf->gfx);
  pBuf->string16->style |= SF_CENTER;

  add_to_gui_list(ID_UNITS_WINDOW, pBuf);

  /* economy button */
  pBuf = create_themeicon(NULL, WF_FREE_GFX | WF_FREE_THEME);

  pBuf->size.w = 19;
  pBuf->size.h = 24;

  pBuf->size.x = Main.screen->w - UNITS_W + FRAME_WH + 5;
  pBuf->size.y = Main.screen->h - UNITS_H + FRAME_WH + 2;

  pBuf->action = economy_callback;
  pBuf->key = SDLK_t;
  pBuf->mod = KMOD_SHIFT;

  add_to_gui_list(ID_ECONOMY, pBuf);

  /* research button */
  pBuf = create_themeicon(NULL, WF_FREE_GFX | WF_FREE_THEME);

  pBuf->size.w = 19;
  pBuf->size.h = 24;

  pBuf->size.x = Main.screen->w - UNITS_W + FRAME_WH + 5;
  pBuf->size.y = Main.screen->h - UNITS_H + FRAME_WH + 2 + pBuf->size.h;

  pBuf->action = research_callback;
  pBuf->key = SDLK_F6;

  add_to_gui_list(ID_RESEARCH, pBuf);

  /* revolution button */
  pBuf = create_themeicon(NULL, WF_FREE_GFX | WF_FREE_THEME);

  pBuf->size.w = 19;
  pBuf->size.h = 24;

  pBuf->size.x = Main.screen->w - UNITS_W + FRAME_WH + 5;
  pBuf->size.y =
      Main.screen->h - UNITS_H + FRAME_WH + 2 + (pBuf->size.h << 1);

  pBuf->action = revolution_callback;
  pBuf->key = SDLK_r;
  pBuf->mod = KMOD_SHIFT;

  add_to_gui_list(ID_REVOLUTION, pBuf);

  /* show/hide unit's window button */

  /* make UNITS Icon */
  pIcon_theme = create_surf(pTheme->UNITS_Icon->w,
			    pTheme->UNITS_Icon->h, SDL_SWSURFACE);
  SDL_BlitSurface(pTheme->UNITS_Icon, NULL, pIcon_theme, NULL);
  SDL_BlitSurface(pTheme->L_ARROW_Icon, NULL, pIcon_theme, NULL);
  SDL_SetColorKey(pIcon_theme, SDL_SRCCOLORKEY, 0x0);

  SDL_BlitSurface(pTheme->R_ARROW_Icon, NULL, pIcon_theme, NULL);

  pBuf = create_themeicon(pIcon_theme,
			  WF_FREE_GFX | WF_FREE_THEME |
			  WF_DRAW_THEME_TRANSPARENT);

  pBuf->size.x = Main.screen->w - UNITS_W + FRAME_WH + 2;
  pBuf->size.y = Main.screen->h - FRAME_WH - pBuf->size.h - 2;

  pBuf->action = toggle_unit_info_window_callback;
  add_to_gui_list(ID_TOGGLE_UNITS_WINDOW_BUTTON, pBuf);

  /* ========================= Mini map ========================== */

  pBuf = create_window(NULL, MINI_MAP_W, MINI_MAP_H, 0);
  pBuf->size.x = 0;
  pBuf->size.y = Main.screen->h - MINI_MAP_H;
  add_to_gui_list(ID_MINI_MAP_WINDOW, pBuf);

  /* new turn button */
  pBuf = create_themeicon(pTheme->NEW_TURN_Icon,
			  WF_WIDGET_HAS_INFO_LABEL |
			  WF_DRAW_THEME_TRANSPARENT);

  pBuf->string16 = create_str16_from_char(_("End Turn (enter)"), 12);

  pBuf->action = end_turn_callback;
  pBuf->key = SDLK_RETURN;

  pBuf->size.x = 166;
  pBuf->size.y = Main.screen->h - MINI_MAP_H + FRAME_WH + 2;

  /*set_wstate( pBuf , WS_NORMAL ); */

  add_to_gui_list(ID_NEW_TURN, pBuf);

  /* show/hide log window button */
  /* this button was created in "Init_Log_Window(...)" function */
  pBuf =
      get_widget_pointer_form_main_list
      (ID_CHATLINE_TOGGLE_LOG_WINDOW_BUTTON);
  move_widget_to_front_of_gui_list(pBuf);

  pBuf->size.x = 166;
  pBuf->size.y = Main.screen->h - MINI_MAP_H + FRAME_WH + 2 + pBuf->size.h;

  /* find city button */
  pBuf = create_themeicon(pTheme->FindCity_Icon, 0);

  pBuf->size.x = 166;
  pBuf->size.y =
      Main.screen->h - MINI_MAP_H + FRAME_WH + 2 + pBuf->size.h * 2;

  pBuf->action = find_city_callback;
  pBuf->key = SDLK_f;
  pBuf->mod = KMOD_SHIFT;

  add_to_gui_list(ID_FIND_CITY, pBuf);

  /* show/hide minimap button */

  /* make Map Icon */
  pIcon_theme =
      create_surf(pTheme->MAP_Icon->w, pTheme->MAP_Icon->h, SDL_SWSURFACE);
  SDL_BlitSurface(pTheme->MAP_Icon, NULL, pIcon_theme, NULL);
  SDL_BlitSurface(pTheme->L_ARROW_Icon, NULL, pIcon_theme, NULL);
  SDL_SetColorKey(pIcon_theme, SDL_SRCCOLORKEY, 0x0);

  pBuf = create_themeicon(pIcon_theme,
			  WF_FREE_GFX | WF_FREE_THEME |
			  WF_DRAW_THEME_TRANSPARENT);

  pBuf->size.x = 166;
  pBuf->size.y = Main.screen->h - FRAME_WH - pBuf->size.h - 2;

  pBuf->action = toggle_map_window_callback;
  add_to_gui_list(ID_TOGGLE_MAP_WINDOW_BUTTON, pBuf);

  /* ========================= Cooling/Warming ========================== */

  /* cooling icon */
  pBuf = create_iconlabel((SDL_Surface *) sprites.cooling[0], NULL, 0);

  pBuf->size.x = Main.screen->w - 10 - pBuf->size.w;
  pBuf->size.y = 10;

  add_to_gui_list(ID_COOLING_ICON, pBuf);

  /* warming icon */
  pBuf = create_iconlabel((SDL_Surface *) sprites.warming[0], NULL, 0);

  pBuf->size.x = Main.screen->w - 10 - pBuf->size.w * 2;
  pBuf->size.y = 10;

  add_to_gui_list(ID_WARMING_ICON, pBuf);

  /* ================================ */
  tmp_map_surfaces_init();
  get_new_view_rectsize();

  SDL_Client_Flags |= (CF_REVOLUTION | CF_MAP_UNIT_W_CREATED |
		       CF_UNIT_INFO_SHOW | CF_MINI_MAP_SHOW);
}

/**************************************************************************
  FIXME: active sentry units and turn off automations !
**************************************************************************/
void button_down_on_map(SDL_MouseButtonEvent * pButtonEvent)
{
  int col, row;

  if (get_client_state() != CLIENT_GAME_RUNNING_STATE)
    return;

#if 0
  if (ev->button == 1 && (ev->state & GDK_SHIFT_MASK)) {
    adjust_workers(w, ev);
    return TRUE;
  }
#endif
  get_mcell_cr((int) pButtonEvent->x, (int) pButtonEvent->y, &col, &row);

  if (pButtonEvent->button == SDL_BUTTON_LEFT) {
    do_map_click(col, row);

#if 0
    gtk_widget_grab_focus(turn_done_button);
  } else if ((ev->button == 2) || (ev->state & GDK_CONTROL_MASK)) {
    popit(ev, xtile, ytile);
#endif
  } else {
    center_tile_mapcanvas(col, row);
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
      draw_map_grid ^= TRUE;
      redraw_map_visible();
      return 0;

    case SDLK_n:
      draw_city_names ^= TRUE;
      redraw_map_visible();
      return 0;

    case SDLK_p:
      draw_city_productions ^= TRUE;
      redraw_map_visible();
      return 0;

    case SDLK_t:
      draw_terrain ^= TRUE;
      redraw_map_visible();
      return 0;

    case SDLK_r:
      draw_roads_rails ^= TRUE;
      redraw_map_visible();
      return 0;

    case SDLK_i:
      draw_irrigation ^= TRUE;
      redraw_map_visible();
      return 0;

    case SDLK_m:
      draw_mines ^= TRUE;
      redraw_map_visible();
      return 0;

    case SDLK_f:
      draw_fortress_airbase ^= TRUE;
      redraw_map_visible();
      return 0;

    case SDLK_s:
      draw_specials ^= TRUE;
      redraw_map_visible();
      return 0;

    case SDLK_o:
      draw_pollution ^= TRUE;
      redraw_map_visible();
      return 0;

    case SDLK_c:
      draw_cities ^= TRUE;
      redraw_map_visible();
      return 0;

    case SDLK_u:
      draw_units ^= TRUE;
      redraw_map_visible();
      return 0;

    case SDLK_w:
      draw_fog_of_war ^= TRUE;
      redraw_map_visible();
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
  struct SDL_String16 *pStr = NULL;
  struct GUI *pLabel = NULL;
  struct GUI *pWindow = NULL;
  struct GUI *pCancel_Button = NULL;

  /* create edit */
  struct GUI *pEdit = create_edit(NULL,
				  create_str16_from_char(pSuggestname, 12),
				  180, WF_DRAW_THEME_TRANSPARENT);

  /* create ok button */
  SDL_Surface *pLogo = ZoomSurface(pTheme->OK_Icon, 0.7, 0.7, 1);
  struct GUI *pOK_Button = create_themeicon_button_from_chars(pLogo,
							      _("OK"), 10,
							      WF_FREE_GFX);
  SDL_SetColorKey(pLogo, SDL_SRCCOLORKEY, getpixel(pLogo, 0, 0));


  /* create cancel button */
  pLogo = ZoomSurface(pTheme->CANCEL_Icon, 0.7, 0.7, 1);
  pCancel_Button =
      create_themeicon_button_from_chars(pLogo, _("Cancel"), 10,
					 WF_FREE_GFX);
  SDL_SetColorKey(pLogo, SDL_SRCCOLORKEY, getpixel(pLogo, 0, 0));

  /* create text label */
  pStr = create_str16_from_char(_("What should we call our new city?"), 10);
  pStr->style |= TTF_STYLE_BOLD;
  pStr->forecol.r = 255;
  pStr->forecol.g = 255;
  /* pStr->forecol.b = 255; */
  pLabel = create_iconlabel(NULL, pStr, 0);

  /* create window */
  pStr = create_str16_from_char(_("Build New City"), 12);
  pStr->style |= TTF_STYLE_BOLD;
  pWindow = create_window(pStr, pEdit->size.w + 20, pEdit->size.h +
			  pOK_Button->size.h + pLabel->size.h +
			  WINDOW_TILE_HIGH + 25, 0);


  /* set actions */
  pWindow->action = move_new_city_dlg_callback;
  pCancel_Button->action = newcity_cancel_callback;
  pOK_Button->action = newcity_ok_callback;

  /* set keys */
  pOK_Button->key = SDLK_RETURN;

  /* correct sizes */
  pCancel_Button->size.w += 5;
  /*pOK_Button->size.w += 10; */
  pOK_Button->size.w = pCancel_Button->size.w;

  /* I make this hack to center label on window */
  pLabel->size.w = pWindow->size.w;

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

  pLabel->size.x = pWindow->size.x;
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
  pEndNewCityWidgetList = pWindow;
  add_to_gui_list(ID_NEWCITY_NAME_WINDOW, pWindow);
  add_to_gui_list(ID_NEWCITY_NAME_LABEL, pLabel);
  add_to_gui_list(ID_NEWCITY_NAME_CANCEL_BUTTON, pCancel_Button);
  add_to_gui_list(ID_NEWCITY_NAME_OK_BUTTON, pOK_Button);
  add_to_gui_list(ID_NEWCITY_NAME_EDIT, pEdit);
  pBeginNewCityWidgetList = pEdit;


  newcity_unit_id = pUnit->id;

  /* redraw */
  redraw_group(pBeginNewCityWidgetList, pEndNewCityWidgetList, 0);

  refresh_rect(pWindow->size);
}

/**************************************************************************
  ...
**************************************************************************/
void popdown_newcity_dialog(void)
{

  popdown_window_group_dialog(pBeginNewCityWidgetList,
			      pEndNewCityWidgetList);
  pEndNewCityWidgetList = NULL;
  newcity_unit_id = 0;

}

/**************************************************************************
  A turn done button should be provided for the player.  This function
  is called to toggle it between active/inactive.
**************************************************************************/
void set_turn_done_button_state(bool state)
{
  struct GUI *pTmp = get_widget_pointer_form_main_list(ID_NEW_TURN);

  if (state) {
    set_wstate(pTmp, WS_NORMAL);
  } else {
    set_wstate(pTmp, WS_DISABLED);
  }

  if ((get_client_state() == CLIENT_GAME_RUNNING_STATE) &&
      (!detect_rect_colisions(get_citydlg_rect(), &(pTmp->size)))) {
    redraw_icon(pTmp);
    refresh_rect(pTmp->size);
  }
}

/**************************************************************************
  Draw a goto or patrol line at the current mouse position.
**************************************************************************/
void create_line_at_mouse_pos(void)
{
  freelog(LOG_DEBUG, "create_line_at_mouse_pos : PORT ME");
}
