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
                          optiondlg.c  -  description
                             -------------------
    begin                : Sun Aug 11 2002
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafa³ Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
/*#include <string.h>*/
/*#include <ctype.h>*/

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

#include "fcintl.h"

#include "gui_mem.h"

#include "clinet.h"

#include "graphics.h"
#include "unistring.h"
#include "gui_string.h"
#include "gui_id.h"
#include "gui_stuff.h"
#include "gui_zoom.h"
#include "gui_tilespec.h"
#include "gui_main.h"

#include "civclient.h"
#include "chatline.h"
#include "menu.h"
#include "control.h"
#include "mapctrl.h"
#include "mapview.h"
#include "wldlg.h"
#include "colors.h"

#include "optiondlg.h"
#include "options.h"

static struct GUI *pBeginOptionsWidgetList = NULL;
static struct GUI *pEndOptionsWidgetList = NULL;
static struct GUI *pBeginCoreOptionsWidgetList = NULL;
static struct GUI *pBeginMainOptionsWidgetList = NULL;

/**************************************************************************
  ...
**************************************************************************/
static void center_optiondlg(void)
{
  Sint16 newX = (Main.screen->w - pEndOptionsWidgetList->size.w) / 2;
  Sint16 newY = (Main.screen->h - pEndOptionsWidgetList->size.h) / 2;

  set_new_group_start_pos(pBeginOptionsWidgetList,
			  pEndOptionsWidgetList,
			  newX - pEndOptionsWidgetList->size.x,
			  newY - pEndOptionsWidgetList->size.y);

}

/**************************************************************************
  ...
**************************************************************************/
static int main_optiondlg_callback(struct GUI *pWidget)
{
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int sound_callback(struct GUI *pWidget)
{
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int work_lists_callback(struct GUI *pWidget)
{
  /* popdown window */
  popdown_window_group_dialog(pBeginOptionsWidgetList,
			      pEndOptionsWidgetList);

  /* clear flags */
  SDL_Client_Flags &=
      ~(CF_OPTION_MAIN | CF_OPTION_OPEN | CF_TOGGLED_FULLSCREEN);

  popup_worklists_report(game.player_ptr);

  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int change_mode_callback(struct GUI *pWidget)
{

  char __buf[50] = "";

  Uint32 tmp_flags = Main.screen->flags;
  struct GUI *pWindow =
      pBeginMainOptionsWidgetList->prev->prev->prev->prev;

  /* don't free this */
  SDL_Rect **pModes_Rect =
      SDL_ListModes(NULL, SDL_FULLSCREEN | Main.screen->flags);


  while (pWindow) {

    if (get_wstate(pWindow) == WS_DISABLED) {
      set_wstate(pWindow, WS_NORMAL);
      break;
    }

    pWindow = pWindow->prev;
  }

  set_wstate(pWidget, WS_DISABLED);


  if (SDL_Client_Flags & CF_TOGGLED_FULLSCREEN) {
    SDL_Client_Flags &= ~CF_TOGGLED_FULLSCREEN;
    tmp_flags ^= SDL_FULLSCREEN;
    tmp_flags ^= SDL_RESIZABLE;
  }

  set_video_mode(pModes_Rect[MAX_ID - pWidget->ID]->w,
		 pModes_Rect[MAX_ID - pWidget->ID]->h, tmp_flags);



  /* change setting label */
  if (Main.screen->flags & SDL_FULLSCREEN) {
    sprintf(__buf, _("Current Setup\nFullscreen %dx%d"),
	    Main.screen->w, Main.screen->h);
  } else {
    sprintf(__buf, _("Current Setup\n%dx%d"), Main.screen->w,
	    Main.screen->h);
  }

  FREE(pBeginMainOptionsWidgetList->prev->string16->text);
  pBeginMainOptionsWidgetList->prev->string16->text =
      convert_to_utf16(__buf);


  /* move units window to botton-right corrner */
  set_new_units_window_pos(get_widget_pointer_form_main_list
			   (ID_UNITS_WINDOW));

  /* move minimap window to botton-left corrner */
  set_new_mini_map_window_pos(get_widget_pointer_form_main_list
			      (ID_MINI_MAP_WINDOW));

  /* move cooling/warming icons to botton-right corrner */

  pWindow = get_widget_pointer_form_main_list(ID_WARMING_ICON);
  pWindow->size.x = Main.screen->w - 10 - (pWindow->size.w << 1);

  /* ID_COOLING_ICON */
  pWindow = pWindow->next;
  pWindow->size.x = Main.screen->w - 10 - pWindow->size.w;

  center_optiondlg();

  pWindow = pEndOptionsWidgetList;

  if (get_client_state() != CLIENT_GAME_RUNNING_STATE) {

    draw_intro_gfx();
    Redraw_Log_Window(2);

    refresh_widget_background(pWindow);

    redraw_group(pBeginOptionsWidgetList, pEndOptionsWidgetList, 0);

    refresh_fullscreen();

  } else {

    get_new_view_rectsize();

    /* with redrawing full map */
    center_on_something();

    refresh_widget_background(pWindow);

    redraw_group(pBeginOptionsWidgetList, pEndOptionsWidgetList, 0);

    refresh_rect(pWindow->size);

  }

  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int togle_fullscreen_callback(struct GUI *pWidget)
{
  int i = 0;
  struct GUI *pTmp = NULL;

  /* don't free this */
  SDL_Rect **pModes_Rect =
      SDL_ListModes(NULL, SDL_FULLSCREEN | Main.screen->flags);

  redraw_icon(pWidget);
  refresh_rect(pWidget->size);

  SDL_Client_Flags ^= CF_TOGGLED_FULLSCREEN;


  while (pModes_Rect[i]->w != Main.screen->w) {
    i++;
  }

  pTmp = get_widget_pointer_form_main_list(MAX_ID - i);

  if (get_wstate(pTmp) == WS_DISABLED) {
    set_wstate(pTmp, WS_NORMAL);
  } else {
    set_wstate(pTmp, WS_DISABLED);
  }

  redraw_ibutton(pTmp);
  refresh_rect(pTmp->size);

  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int video_callback(struct GUI *pWidget)
{
  int i = 0;
  char __buf[50] = "";
  Uint16 len = 0;
  Sint16 xxx;			/* tmp */
  SDL_Rect **pModes_Rect = NULL;
  struct GUI *pTmpGui = NULL, *pWindow = pEndOptionsWidgetList;
  Uint16 **pModes = get_list_modes(Main.screen->flags);

  if (!pModes) {
    return 0;
  }

  /* don't free this */
  pModes_Rect = SDL_ListModes(NULL, SDL_FULLSCREEN | Main.screen->flags);

  /* clear flag */
  SDL_Client_Flags &= ~CF_OPTION_MAIN;

  /* hide main widget group */
  hide_group(pBeginMainOptionsWidgetList,
	     pBeginCoreOptionsWidgetList->prev);

  /* create setting label */
  if (Main.screen->flags & SDL_FULLSCREEN) {
    sprintf(__buf, _("Current Setup\nFullscreen %dx%d"),
	    Main.screen->w, Main.screen->h);
  } else {
    sprintf(__buf, _("Current Setup\n%dx%d"), Main.screen->w,
	    Main.screen->h);
  }

  pTmpGui = create_iconlabel(NULL, create_str16_from_char(__buf, 10), 0);
  pTmpGui->string16->style |= TTF_STYLE_BOLD;
  pTmpGui->string16->forecol.r = 255;
  pTmpGui->string16->forecol.g = 255;
  /*pTmpGui->string16->forecol.b = 255; */

  /* set window width to 'pTmpGui' for center string */
  pTmpGui->size.w = pWindow->size.w;

  pTmpGui->size.x = pWindow->size.x;
  pTmpGui->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 6;

  add_to_gui_list(ID_OPTIONS_RESOLUTION_LABEL, pTmpGui);

  /* fullscreen mode label */
  pTmpGui =
      create_themelabel(create_filled_surface
			(150, 30, SDL_SWSURFACE, NULL),
			create_str16_from_char(_("Fullscreen Mode"),
					       10), 150, 30, 0);

  pTmpGui->string16->style |= (TTF_STYLE_BOLD | SF_CENTER_RIGHT);
  pTmpGui->string16->style &= ~SF_CENTER;
  /* pTmpGui->string16->forecol.r = 255;
     pTmpGui->string16->forecol.g = 255;
     pTmpGui->string16->forecol.b = 255; */


  xxx = pTmpGui->size.x = pWindow->size.x +
      ((pWindow->size.w - pTmpGui->size.w) / 2);
  pTmpGui->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 40;

  add_to_gui_list(ID_OPTIONS_FULLSCREEN_LABEL, pTmpGui);

  /* fullscreen check box */
  if (Main.screen->flags & SDL_FULLSCREEN) {
    pTmpGui = create_checkbox(TRUE, WF_DRAW_THEME_TRANSPARENT);
  } else {
    pTmpGui = create_checkbox(FALSE, WF_DRAW_THEME_TRANSPARENT);
  }

  pTmpGui->action = togle_fullscreen_callback;
  set_wstate(pTmpGui, WS_NORMAL);

  pTmpGui->size.x = xxx + 5;
  pTmpGui->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 45;

  add_to_gui_list(ID_OPTIONS_TOGGLE_FULLSCREEN_CHECKBOX, pTmpGui);

  while (pModes_Rect[i]->w == Main.screen->w) {
    i++;
  }

  /* create modes buttons */
  for (i = 0; pModes[i]; ++i) {

    pTmpGui = create_icon_button_from_unichar(NULL, pModes[i], 14, 0);

    if (len) {
      pTmpGui->size.w = len;
    } else {
      pTmpGui->size.w += 6;
      len = pTmpGui->size.w;
    }

    if (pModes_Rect[i]->w != Main.screen->w)
      set_wstate(pTmpGui, WS_NORMAL);

    pTmpGui->action = change_mode_callback;

    /* ugly hack */
    add_to_gui_list((MAX_ID - i), pTmpGui);
  }

  pBeginOptionsWidgetList = pTmpGui;

  /* set start positions */
  pTmpGui = pBeginMainOptionsWidgetList->prev->prev->prev->prev;
  pTmpGui->size.x =
      pWindow->size.x + ((pWindow->size.w - pTmpGui->size.w) / 2);
  pTmpGui->size.y = pWindow->size.y + 110;

  for (pTmpGui = pTmpGui->prev; pTmpGui; pTmpGui = pTmpGui->prev) {
    pTmpGui->size.x = pTmpGui->next->size.x;
    pTmpGui->size.y = pTmpGui->next->size.y + pTmpGui->next->size.h + 10;
  }


  FREE(pModes);

  redraw_group(pBeginOptionsWidgetList, pEndOptionsWidgetList, 0);
  refresh_rect(pWindow->size);

  return -1;
}

/* ===================================================================== */

/**************************************************************************
  ...
**************************************************************************/
static int sound_bell_at_new_turn_callback(struct GUI *pWidget)
{
  redraw_icon(pWidget);
  refresh_rect(pWidget->size);
  sound_bell_at_new_turn ^= 1;
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int smooth_move_units_callback(struct GUI *pWidget)
{
  redraw_icon(pWidget);
  refresh_rect(pWidget->size);
  if (smooth_move_units) {
    smooth_move_units = FALSE;
    set_wstate(pWidget->prev->prev, WS_DISABLED);
    redraw_edit(pWidget->prev->prev);
    refresh_rect(pWidget->prev->prev->size);
  } else {
    smooth_move_units = TRUE;
    set_wstate(pWidget->prev->prev, WS_NORMAL);
    redraw_edit(pWidget->prev->prev);
    refresh_rect(pWidget->prev->prev->size);
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int smooth_move_unit_steps_callback(struct GUI *pWidget)
{
  char *tmp = convert_to_chars(pWidget->string16->text);
  sscanf(tmp, "%d", &smooth_move_unit_steps);
  FREE(tmp);
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int do_combat_animation_callback(struct GUI *pWidget)
{
  redraw_icon(pWidget);
  refresh_rect(pWidget->size);
  do_combat_animation ^= 1;
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int auto_center_on_unit_callback(struct GUI *pWidget)
{
  redraw_icon(pWidget);
  refresh_rect(pWidget->size);
  auto_center_on_unit ^= 1;
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int auto_center_on_combat_callback(struct GUI *pWidget)
{
  redraw_icon(pWidget);
  refresh_rect(pWidget->size);
  auto_center_on_combat ^= 1;
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int wakeup_focus_callback(struct GUI *pWidget)
{
  redraw_icon(pWidget);
  refresh_rect(pWidget->size);
  wakeup_focus ^= 1;
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int center_when_popup_city_callback(struct GUI *pWidget)
{
  redraw_icon(pWidget);
  refresh_rect(pWidget->size);
  center_when_popup_city ^= 1;
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int auto_turn_done_callback(struct GUI *pWidget)
{
  redraw_icon(pWidget);
  refresh_rect(pWidget->size);
  auto_turn_done ^= 1;
  return -1;
}

/**************************************************************************
  popup local settings.
**************************************************************************/
static int local_setting_callback(struct GUI *pWidget)
{
  struct GUI *pTmpGui = NULL, *pWindow = pEndOptionsWidgetList;
  char __buf[3];

  /* clear flag */
  SDL_Client_Flags &= ~CF_OPTION_MAIN;

  /* hide main widget group */
  hide_group(pBeginMainOptionsWidgetList,
	     pBeginCoreOptionsWidgetList->prev);

  /* 'sound befor new turn' */
  /* check box */
  pTmpGui = create_checkbox(sound_bell_at_new_turn,
			    WF_DRAW_THEME_TRANSPARENT);

  pTmpGui->action = sound_bell_at_new_turn_callback;
  set_wstate(pTmpGui, WS_NORMAL);

  pTmpGui->size.x = pWindow->size.x + 15;
  pTmpGui->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 6;

  add_to_gui_list(ID_OPTIONS_LOCAL_SOUND_CHECKBOX, pTmpGui);

  /* 'sound befor new turn' label */
  pTmpGui = create_iconlabel_from_chars(NULL,
					_("D¼wiêk przed now± tur±"), 10,
					0);
  pTmpGui->string16->style |= TTF_STYLE_BOLD;
  pTmpGui->string16->forecol.r = 255;
  pTmpGui->string16->forecol.g = 255;
  /*pTmpGui->string16->forecol.b = 255; */

  pTmpGui->size.x = pWindow->size.x + 55;

  add_to_gui_list(ID_OPTIONS_LOCAL_SOUND_LABEL, pTmpGui);

  pTmpGui->size.y = pTmpGui->next->size.y +
      ((pTmpGui->next->size.h - pTmpGui->size.h) / 2);

  /* 'smooth unit moves' */
  /* check box */
  pTmpGui = create_checkbox(smooth_move_units, WF_DRAW_THEME_TRANSPARENT);

  pTmpGui->action = smooth_move_units_callback;
  set_wstate(pTmpGui, WS_NORMAL);

  pTmpGui->size.x = pWindow->size.x + 15;

  add_to_gui_list(ID_OPTIONS_LOCAL_MOVE_CHECKBOX, pTmpGui);
  pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + 4;

  /* label */
  pTmpGui = create_iconlabel_from_chars(NULL,
					_("P³ynne ruchy jednostek"), 10,
					0);
  pTmpGui->string16->style |= TTF_STYLE_BOLD;
  pTmpGui->string16->forecol.r = 255;
  pTmpGui->string16->forecol.g = 255;
  /*pTmpGui->string16->forecol.b = 255; */

  pTmpGui->size.x = pWindow->size.x + 55;

  add_to_gui_list(ID_OPTIONS_LOCAL_MOVE_LABEL, pTmpGui);

  pTmpGui->size.y = pTmpGui->next->size.y +
      ((pTmpGui->next->size.h - pTmpGui->size.h) / 2);

  /* 'smooth unit move steps' */

  /* edit */
  sprintf(__buf, "%d", smooth_move_unit_steps);
  pTmpGui = create_edit_from_chars(NULL, __buf, 11, 25, 0);

  pTmpGui->action = smooth_move_unit_steps_callback;

  if (smooth_move_units) {
    set_wstate(pTmpGui, WS_NORMAL);
  }

  pTmpGui->size.x = pWindow->size.x + 12;

  add_to_gui_list(ID_OPTIONS_LOCAL_MOVE_STEP_EDIT, pTmpGui);
  pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + 4;

  /* label */
  pTmpGui = create_iconlabel_from_chars(NULL,
					_("P³ynne kroki jednostek"), 10,
					0);
  pTmpGui->string16->style |= TTF_STYLE_BOLD;
  pTmpGui->string16->forecol.r = 255;
  pTmpGui->string16->forecol.g = 255;
  /*pTmpGui->string16->forecol.b = 255; */

  pTmpGui->size.x = pWindow->size.x + 55;

  add_to_gui_list(ID_OPTIONS_LOCAL_MOVE_STEP_LABEL, pTmpGui);

  pTmpGui->size.y = pTmpGui->next->size.y +
      (pTmpGui->next->size.h - pTmpGui->size.h) / 2;

  /* 'show combat anim' */

  /* check box */
  pTmpGui =
      create_checkbox(do_combat_animation, WF_DRAW_THEME_TRANSPARENT);

  pTmpGui->action = do_combat_animation_callback;
  set_wstate(pTmpGui, WS_NORMAL);

  pTmpGui->size.x = pWindow->size.x + 15;

  add_to_gui_list(ID_OPTIONS_LOCAL_COMBAT_CHECKBOX, pTmpGui);
  pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + 4;

  /* label */
  pTmpGui = create_iconlabel_from_chars(NULL, _("Animacjia Walki"), 10, 0);
  pTmpGui->string16->style |= TTF_STYLE_BOLD;
  pTmpGui->string16->forecol.r = 255;
  pTmpGui->string16->forecol.g = 255;
  /*pTmpGui->string16->forecol.b = 255; */

  pTmpGui->size.x = pWindow->size.x + 55;

  add_to_gui_list(ID_OPTIONS_LOCAL_COMBAT_LABEL, pTmpGui);

  pTmpGui->size.y = pTmpGui->next->size.y +
      (pTmpGui->next->size.h - pTmpGui->size.h) / 2;

  /* 'auto center on units' */
  /* check box */
  pTmpGui =
      create_checkbox(auto_center_on_unit, WF_DRAW_THEME_TRANSPARENT);

  pTmpGui->action = auto_center_on_unit_callback;
  set_wstate(pTmpGui, WS_NORMAL);

  pTmpGui->size.x = pWindow->size.x + 15;

  add_to_gui_list(ID_OPTIONS_LOCAL_ACENTER_CHECKBOX, pTmpGui);
  pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + 4;

  /* label */
  pTmpGui = create_iconlabel_from_chars(NULL,
					_("Automatyczne wy¶rodkowanie na "
					  "jednostke"), 10, 0);
  pTmpGui->string16->style |= TTF_STYLE_BOLD;
  pTmpGui->string16->forecol.r = 255;
  pTmpGui->string16->forecol.g = 255;
  /* pTmpGui->string16->forecol.b = 255; */

  pTmpGui->size.x = pWindow->size.x + 65;

  add_to_gui_list(ID_OPTIONS_LOCAL_ACENTER_LABEL, pTmpGui);

  pTmpGui->size.y = pTmpGui->next->size.y +
      ((pTmpGui->next->size.h - pTmpGui->size.h) / 2);

  /* 'auto center on combat' */
  /* check box */
  pTmpGui = create_checkbox(auto_center_on_combat,
			    WF_DRAW_THEME_TRANSPARENT);

  pTmpGui->action = auto_center_on_combat_callback;
  set_wstate(pTmpGui, WS_NORMAL);

  pTmpGui->size.x = pWindow->size.x + 15;

  add_to_gui_list(ID_OPTIONS_LOCAL_COMBAT_CENTER_CHECKBOX, pTmpGui);
  pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + 4;

  /* label */
  pTmpGui = create_iconlabel_from_chars(NULL,
					_("Automatyczne wy¶rodkowanie "
					  "na bitwe"), 10, 0);
  pTmpGui->string16->style |= TTF_STYLE_BOLD;
  pTmpGui->string16->forecol.r = 255;
  pTmpGui->string16->forecol.g = 255;
  /*pTmpGui->string16->forecol.b = 255; */

  pTmpGui->size.x = pWindow->size.x + 65;

  add_to_gui_list(ID_OPTIONS_LOCAL_COMBAT_CENTER_LABEL, pTmpGui);

  pTmpGui->size.y = pTmpGui->next->size.y +
      (pTmpGui->next->size.h - pTmpGui->size.h) / 2;

  /* 'wakeup focus' */

  /* check box */
  pTmpGui = create_checkbox(wakeup_focus, WF_DRAW_THEME_TRANSPARENT);

  pTmpGui->action = wakeup_focus_callback;
  set_wstate(pTmpGui, WS_NORMAL);

  pTmpGui->size.x = pWindow->size.x + 15;

  add_to_gui_list(ID_OPTIONS_LOCAL_ACTIVE_UNITS_CHECKBOX, pTmpGui);
  pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + 4;

  /* label */
  pTmpGui = create_iconlabel_from_chars(NULL,
					_
					("Przej¶cie do uakywnionej jednostki"),
					10, 0);
  pTmpGui->string16->style |= TTF_STYLE_BOLD;
  pTmpGui->string16->forecol.r = 255;
  pTmpGui->string16->forecol.g = 255;
  /* pTmpGui->string16->forecol.b = 255; */

  pTmpGui->size.x = pWindow->size.x + 65;

  add_to_gui_list(ID_OPTIONS_LOCAL_ACTIVE_UNITS_LABEL, pTmpGui);

  pTmpGui->size.y = pTmpGui->next->size.y +
      (pTmpGui->next->size.h - pTmpGui->size.h) / 2;

  /* 'center when popup city' */
  /* check box */
  pTmpGui = create_checkbox(center_when_popup_city,
			    WF_DRAW_THEME_TRANSPARENT);

  pTmpGui->action = center_when_popup_city_callback;
  set_wstate(pTmpGui, WS_NORMAL);

  pTmpGui->size.x = pWindow->size.x + 15;

  add_to_gui_list(ID_OPTIONS_LOCAL_CITY_CENTER_CHECKBOX, pTmpGui);
  pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + 4;

  /* label */
  pTmpGui = create_iconlabel_from_chars(NULL,
					_("Centrowanie mapy przy wej¶ciu "
					  "do miasta"), 10, 0);
  pTmpGui->string16->style |= TTF_STYLE_BOLD;
  pTmpGui->string16->forecol.r = 255;
  pTmpGui->string16->forecol.g = 255;
  /*pTmpGui->string16->forecol.b = 255; */

  pTmpGui->size.x = pWindow->size.x + 65;

  add_to_gui_list(ID_OPTIONS_LOCAL_CITY_CENTER_LABEL, pTmpGui);

  pTmpGui->size.y = pTmpGui->next->size.y +
      (pTmpGui->next->size.h - pTmpGui->size.h) / 2;

  /* 'auto turn done' */

  /* check box */
  pTmpGui = create_checkbox(auto_turn_done, WF_DRAW_THEME_TRANSPARENT);

  pTmpGui->action = auto_turn_done_callback;
  set_wstate(pTmpGui, WS_NORMAL);

  pTmpGui->size.x = pWindow->size.x + 15;

  add_to_gui_list(ID_OPTIONS_LOCAL_END_TURN_CHECKBOX, pTmpGui);
  pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + 4;

  /* label */
  pTmpGui = create_iconlabel_from_chars(NULL,
					_
					("Zakoñcz ture gdy wykona siê wszystkie ruchy"),
					10, 0);
  pTmpGui->string16->style |= TTF_STYLE_BOLD;
  pTmpGui->string16->forecol.r = 255;
  pTmpGui->string16->forecol.g = 255;
  /*pTmpGui->string16->forecol.b = 255; */

  pTmpGui->size.x = pWindow->size.x + 65;

  add_to_gui_list(ID_OPTIONS_LOCAL_END_TURN_LABEL, pTmpGui);

  pTmpGui->size.y = pTmpGui->next->size.y +
      (pTmpGui->next->size.h - pTmpGui->size.h) / 2;


  pBeginOptionsWidgetList = pTmpGui;
  redraw_group(pBeginOptionsWidgetList, pEndOptionsWidgetList, 0);
  refresh_rect(pWindow->size);

  return -1;
}

/* ===================================================================== */

/**************************************************************************
  ...
**************************************************************************/
static int map_grid_callback(struct GUI *pWidget)
{
  redraw_icon(pWidget);
  refresh_rect(pWidget->size);
  draw_map_grid ^= 1;
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int draw_city_names_callback(struct GUI *pWidget)
{
  redraw_icon(pWidget);
  refresh_rect(pWidget->size);
  draw_city_names ^= 1;
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int draw_city_productions_callback(struct GUI *pWidget)
{
  redraw_icon(pWidget);
  refresh_rect(pWidget->size);
  draw_city_productions ^= 1;
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int draw_terrain_callback(struct GUI *pWidget)
{
  redraw_icon(pWidget);
  refresh_rect(pWidget->size);
  draw_terrain ^= 1;
  draw_coastline = FALSE;
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int draw_coastline_callback(struct GUI *pWidget)
{
  redraw_icon(pWidget);
  refresh_rect(pWidget->size);
  draw_coastline ^= 1;
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int draw_specials_callback(struct GUI *pWidget)
{
  redraw_icon(pWidget);
  refresh_rect(pWidget->size);
  draw_specials ^= 1;
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int draw_pollution_callback(struct GUI *pWidget)
{
  redraw_icon(pWidget);
  refresh_rect(pWidget->size);
  draw_pollution ^= 1;
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int draw_cities_callback(struct GUI *pWidget)
{
  redraw_icon(pWidget);
  refresh_rect(pWidget->size);
  draw_cities ^= 1;
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int draw_units_callback(struct GUI *pWidget)
{
  redraw_icon(pWidget);
  refresh_rect(pWidget->size);
  draw_units ^= 1;
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int draw_fog_of_war_callback(struct GUI *pWidget)
{
  redraw_icon(pWidget);
  refresh_rect(pWidget->size);
  draw_fog_of_war ^= 1;
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int draw_roads_rails_callback(struct GUI *pWidget)
{
  redraw_icon(pWidget);
  refresh_rect(pWidget->size);
  draw_roads_rails ^= 1;
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int draw_irrigation_callback(struct GUI *pWidget)
{
  redraw_icon(pWidget);
  refresh_rect(pWidget->size);
  draw_irrigation ^= 1;
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int draw_mines_callback(struct GUI *pWidget)
{
  redraw_icon(pWidget);
  refresh_rect(pWidget->size);
  draw_mines ^= 1;
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int draw_fortress_airbase_callback(struct GUI *pWidget)
{
  redraw_icon(pWidget);
  refresh_rect(pWidget->size);
  draw_fortress_airbase ^= 1;
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int map_setting_callback(struct GUI *pWidget)
{
  struct GUI *pTmpGui = NULL, *pWindow = pEndOptionsWidgetList;

  /* clear flag */
  SDL_Client_Flags &= ~CF_OPTION_MAIN;

  /* hide main widget group */
  hide_group(pBeginMainOptionsWidgetList,
	     pBeginCoreOptionsWidgetList->prev);

  /* 'draw map gird' */
  /* check box */
  pTmpGui = create_checkbox(draw_map_grid, WF_DRAW_THEME_TRANSPARENT);

  pTmpGui->action = map_grid_callback;
  set_wstate(pTmpGui, WS_NORMAL);

  pTmpGui->size.x = pWindow->size.x + 15;
  pTmpGui->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 6;

  add_to_gui_list(ID_OPTIONS_MAP_GRID_CHECKBOX, pTmpGui);

  /* 'sound befor new turn' label */
  pTmpGui = create_iconlabel_from_chars(NULL, _("Map Grid"), 10, 0);
  pTmpGui->string16->style |= TTF_STYLE_BOLD;
  pTmpGui->string16->forecol.r = 255;
  pTmpGui->string16->forecol.g = 255;
  /* pTmpGui->string16->forecol.b = 255; */

  pTmpGui->size.x = pWindow->size.x + 55;

  add_to_gui_list(ID_OPTIONS_MAP_GRID_LABEL, pTmpGui);

  pTmpGui->size.y = pTmpGui->next->size.y +
      ((pTmpGui->next->size.h - pTmpGui->size.h) / 2);

  /* 'draw city names' */
  /* check box */
  pTmpGui = create_checkbox(draw_city_names, WF_DRAW_THEME_TRANSPARENT);

  pTmpGui->action = draw_city_names_callback;
  set_wstate(pTmpGui, WS_NORMAL);

  pTmpGui->size.x = pWindow->size.x + 15;

  add_to_gui_list(ID_OPTIONS_MAP_CITY_NAMES_CHECKBOX, pTmpGui);
  pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + 4;

  /* label */
  pTmpGui = create_iconlabel_from_chars(NULL, _("City Names"), 10, 0);
  pTmpGui->string16->style |= TTF_STYLE_BOLD;
  pTmpGui->string16->forecol.r = 255;
  pTmpGui->string16->forecol.g = 255;
  /* pTmpGui->string16->forecol.b = 255; */

  pTmpGui->size.x = pWindow->size.x + 55;

  add_to_gui_list(ID_OPTIONS_MAP_CITY_NAMES_LABEL, pTmpGui);

  pTmpGui->size.y = pTmpGui->next->size.y +
      ((pTmpGui->next->size.h - pTmpGui->size.h) / 2);

  /* 'draw city prod.' */
  /* check box */
  pTmpGui = create_checkbox(draw_city_productions,
			    WF_DRAW_THEME_TRANSPARENT);

  pTmpGui->action = draw_city_productions_callback;
  set_wstate(pTmpGui, WS_NORMAL);

  pTmpGui->size.x = pWindow->size.x + 15;

  add_to_gui_list(ID_OPTIONS_MAP_CITY_PROD_CHECKBOX, pTmpGui);
  pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + 4;

  /* label */
  pTmpGui =
      create_iconlabel_from_chars(NULL, _("City Production"), 10, 0);
  pTmpGui->string16->style |= TTF_STYLE_BOLD;
  pTmpGui->string16->forecol.r = 255;
  pTmpGui->string16->forecol.g = 255;
  /*pTmpGui->string16->forecol.b = 255; */

  pTmpGui->size.x = pWindow->size.x + 55;

  add_to_gui_list(ID_OPTIONS_MAP_CITY_NAMES_LABEL, pTmpGui);

  pTmpGui->size.y = pTmpGui->next->size.y +
      ((pTmpGui->next->size.h - pTmpGui->size.h) / 2);

  /* 'draw terrain' */
  /* check box */
  pTmpGui = create_checkbox(draw_terrain, WF_DRAW_THEME_TRANSPARENT);

  pTmpGui->action = draw_terrain_callback;
  set_wstate(pTmpGui, WS_NORMAL);

  pTmpGui->size.x = pWindow->size.x + 15;

  add_to_gui_list(ID_OPTIONS_MAP_CITY_PROD_CHECKBOX, pTmpGui);
  pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + 4;

  /* label */
  pTmpGui = create_iconlabel_from_chars(NULL, _("Terrain"), 10, 0);
  pTmpGui->string16->style |= TTF_STYLE_BOLD;
  pTmpGui->string16->forecol.r = 255;
  pTmpGui->string16->forecol.g = 255;
  /*pTmpGui->string16->forecol.b = 255; */

  pTmpGui->size.x = pWindow->size.x + 55;

  add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_LABEL, pTmpGui);

  pTmpGui->size.y = pTmpGui->next->size.y +
      (pTmpGui->next->size.h - pTmpGui->size.h) / 2;

  /* 'draw coast line' */

  /* check box */
  pTmpGui = create_checkbox(draw_coastline, WF_DRAW_THEME_TRANSPARENT);

  pTmpGui->action = draw_coastline_callback;

  if (!draw_terrain) {
    set_wstate(pTmpGui, WS_NORMAL);
  }

  pTmpGui->size.x = pWindow->size.x + 35;

  add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_COAST_CHECKBOX, pTmpGui);
  pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + 4;

  /* label */
  pTmpGui = create_iconlabel_from_chars(NULL, _("Coast outline"), 10, 0);
  pTmpGui->string16->style |= TTF_STYLE_BOLD;
  pTmpGui->string16->forecol.r = 255;
  pTmpGui->string16->forecol.g = 255;
  /*pTmpGui->string16->forecol.b = 255; */

  pTmpGui->size.x = pWindow->size.x + 75;

  add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_COAST_LABEL, pTmpGui);
  pTmpGui->size.y = pTmpGui->next->size.y +
      (pTmpGui->next->size.h - pTmpGui->size.h) / 2;

  /* 'draw specials' */

  /* check box */
  pTmpGui = create_checkbox(draw_specials, WF_DRAW_THEME_TRANSPARENT);

  pTmpGui->action = draw_specials_callback;
  set_wstate(pTmpGui, WS_NORMAL);

  pTmpGui->size.x = pWindow->size.x + 15;

  add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_SPEC_CHECKBOX, pTmpGui);
  pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + 4;

  /* label */
  pTmpGui =
      create_iconlabel_from_chars(NULL, _("Special Resources"), 10, 0);
  pTmpGui->string16->style |= TTF_STYLE_BOLD;
  pTmpGui->string16->forecol.r = 255;
  pTmpGui->string16->forecol.g = 255;
  /*pTmpGui->string16->forecol.b = 255; */

  pTmpGui->size.x = pWindow->size.x + 55;

  add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_SPEC_LABEL, pTmpGui);

  pTmpGui->size.y = pTmpGui->next->size.y +
      (pTmpGui->next->size.h - pTmpGui->size.h) / 2;

  /* 'draw pollutions' */

  /* check box */
  pTmpGui = create_checkbox(draw_pollution, WF_DRAW_THEME_TRANSPARENT);

  pTmpGui->action = draw_pollution_callback;
  set_wstate(pTmpGui, WS_NORMAL);

  pTmpGui->size.x = pWindow->size.x + 15;

  add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_POLL_CHECKBOX, pTmpGui);
  pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + 4;

  /* label */
  pTmpGui = create_iconlabel_from_chars(NULL, _("Pollution"), 10, 0);
  pTmpGui->string16->style |= TTF_STYLE_BOLD;
  pTmpGui->string16->forecol.r = 255;
  pTmpGui->string16->forecol.g = 255;
  /* pTmpGui->string16->forecol.b = 255; */

  pTmpGui->size.x = pWindow->size.x + 55;

  add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_POLL_LABEL, pTmpGui);

  pTmpGui->size.y = pTmpGui->next->size.y +
      (pTmpGui->next->size.h - pTmpGui->size.h) / 2;

  /* 'draw cities' */

  /* check box */
  pTmpGui = create_checkbox(draw_cities, WF_DRAW_THEME_TRANSPARENT);

  pTmpGui->action = draw_cities_callback;
  set_wstate(pTmpGui, WS_NORMAL);

  pTmpGui->size.x = pWindow->size.x + 15;

  add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_CITY_CHECKBOX, pTmpGui);
  pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + 4;

  /* label */
  pTmpGui = create_iconlabel_from_chars(NULL, _("Cities"), 10, 0);
  pTmpGui->string16->style |= TTF_STYLE_BOLD;
  pTmpGui->string16->forecol.r = 255;
  pTmpGui->string16->forecol.g = 255;
  /* pTmpGui->string16->forecol.b = 255; */

  pTmpGui->size.x = pWindow->size.x + 55;

  add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_CITY_LABEL, pTmpGui);

  pTmpGui->size.y = pTmpGui->next->size.y +
      (pTmpGui->next->size.h - pTmpGui->size.h) / 2;

  /* 'draw units' */

  /* check box */
  pTmpGui = create_checkbox(draw_units, WF_DRAW_THEME_TRANSPARENT);

  pTmpGui->action = draw_units_callback;
  set_wstate(pTmpGui, WS_NORMAL);

  pTmpGui->size.x = pWindow->size.x + 15;

  add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_UNITS_CHECKBOX, pTmpGui);
  pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + 3;

  /* label */
  pTmpGui = create_iconlabel_from_chars(NULL, _("Units"), 10, 0);
  pTmpGui->string16->style |= TTF_STYLE_BOLD;
  pTmpGui->string16->forecol.r = 255;
  pTmpGui->string16->forecol.g = 255;
  /*pTmpGui->string16->forecol.b = 255; */

  pTmpGui->size.x = pWindow->size.x + 55;

  add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_UNITS_LABEL, pTmpGui);

  pTmpGui->size.y = pTmpGui->next->size.y +
      (pTmpGui->next->size.h - pTmpGui->size.h) / 2;

  /* 'draw fog of war' */

  /* check box */
  pTmpGui = create_checkbox(draw_fog_of_war, WF_DRAW_THEME_TRANSPARENT);

  pTmpGui->action = draw_fog_of_war_callback;
  set_wstate(pTmpGui, WS_NORMAL);

  pTmpGui->size.x = pWindow->size.x + 15;

  add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_FOG_CHECKBOX, pTmpGui);
  pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + 3;

  /* label */
  pTmpGui = create_iconlabel_from_chars(NULL, _("Fog of War"), 10, 0);
  pTmpGui->string16->style |= TTF_STYLE_BOLD;
  pTmpGui->string16->forecol.r = 255;
  pTmpGui->string16->forecol.g = 255;
  /* pTmpGui->string16->forecol.b = 255; */

  pTmpGui->size.x = pWindow->size.x + 55;

  add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_FOG_LABEL, pTmpGui);

  pTmpGui->size.y = pTmpGui->next->size.y +
      (pTmpGui->next->size.h - pTmpGui->size.h) / 2;

  /* label inpr. */
  pTmpGui = create_iconlabel_from_chars(NULL, _("Infrastructure"), 10, 0);
  pTmpGui->string16->style |= TTF_STYLE_BOLD;
  pTmpGui->string16->forecol.r = 255;
  pTmpGui->string16->forecol.g = 255;
  /* pTmpGui->string16->forecol.b = 255; */

  pTmpGui->size.x = pWindow->size.x + 185;
  pTmpGui->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 6;

  add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_INPR_LABEL, pTmpGui);
#if 0
  pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + 10;
#endif

  /* 'draw road / rails' */
  /* check box */
  pTmpGui = create_checkbox(draw_roads_rails, WF_DRAW_THEME_TRANSPARENT);

  pTmpGui->action = draw_roads_rails_callback;
  set_wstate(pTmpGui, WS_NORMAL);

  pTmpGui->size.x = pWindow->size.x + 170;

  add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_RR_CHECKBOX, pTmpGui);
  pTmpGui->size.y = pTmpGui->next->size.y + pTmpGui->next->size.h + 4;

  /* label */
  pTmpGui = create_iconlabel_from_chars(NULL, _("Roads and Rails"), 10, 0);
  pTmpGui->string16->style |= TTF_STYLE_BOLD;
  pTmpGui->string16->forecol.r = 255;
  pTmpGui->string16->forecol.g = 255;
  /*pTmpGui->string16->forecol.b = 255; */

  pTmpGui->size.x = pWindow->size.x + 210;

  add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_RR_LABEL, pTmpGui);

  pTmpGui->size.y = pTmpGui->next->size.y +
      (pTmpGui->next->size.h - pTmpGui->size.h) / 2;

  /* 'draw irrigations' */
  /* check box */
  pTmpGui = create_checkbox(draw_irrigation, WF_DRAW_THEME_TRANSPARENT);

  pTmpGui->action = draw_irrigation_callback;
  set_wstate(pTmpGui, WS_NORMAL);

  pTmpGui->size.x = pWindow->size.x + 170;

  add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_IR_CHECKBOX, pTmpGui);
  pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + 4;

  /* label */
  pTmpGui = create_iconlabel_from_chars(NULL, _("Irrigation"), 10, 0);
  pTmpGui->string16->style |= TTF_STYLE_BOLD;
  pTmpGui->string16->forecol.r = 255;
  pTmpGui->string16->forecol.g = 255;
  /*pTmpGui->string16->forecol.b = 255; */

  pTmpGui->size.x = pWindow->size.x + 210;

  add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_IR_LABEL, pTmpGui);

  pTmpGui->size.y = pTmpGui->next->size.y +
      (pTmpGui->next->size.h - pTmpGui->size.h) / 2;

  /* 'draw mines' */

  /* check box */
  pTmpGui = create_checkbox(draw_mines, WF_DRAW_THEME_TRANSPARENT);

  pTmpGui->action = draw_mines_callback;
  set_wstate(pTmpGui, WS_NORMAL);

  pTmpGui->size.x = pWindow->size.x + 170;

  add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_M_CHECKBOX, pTmpGui);
  pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + 4;

  /* label */
  pTmpGui = create_iconlabel_from_chars(NULL, _("Mines"), 10, 0);
  pTmpGui->string16->style |= TTF_STYLE_BOLD;
  pTmpGui->string16->forecol.r = 255;
  pTmpGui->string16->forecol.g = 255;
  /* pTmpGui->string16->forecol.b = 255; */

  pTmpGui->size.x = pWindow->size.x + 210;

  add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_M_LABEL, pTmpGui);

  pTmpGui->size.y = pTmpGui->next->size.y +
      (pTmpGui->next->size.h - pTmpGui->size.h) / 2;

  /* 'draw fortress / air bases' */
  /* check box */
  pTmpGui = create_checkbox(draw_fortress_airbase,
			    WF_DRAW_THEME_TRANSPARENT);

  pTmpGui->action = draw_fortress_airbase_callback;
  set_wstate(pTmpGui, WS_NORMAL);

  pTmpGui->size.x = pWindow->size.x + 170;

  add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_FA_CHECKBOX, pTmpGui);
  pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + 4;

  /* label */
  pTmpGui = create_iconlabel_from_chars(NULL, _("Fortress and Airbase"),
					10, 0);
  pTmpGui->string16->style |= TTF_STYLE_BOLD;
  pTmpGui->string16->forecol.r = 255;
  pTmpGui->string16->forecol.g = 255;
  /*pTmpGui->string16->forecol.b = 255; */

  pTmpGui->size.x = pWindow->size.x + 210;

  add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_FA_LABEL, pTmpGui);

  pTmpGui->size.y = pTmpGui->next->size.y +
      (pTmpGui->next->size.h - pTmpGui->size.h) / 2;


  pBeginOptionsWidgetList = pTmpGui;

  /* redraw window group */
  redraw_group(pBeginOptionsWidgetList, pEndOptionsWidgetList, 0);
  refresh_rect(pWindow->size);

  return -1;
}

/* ===================================================================== */

/**************************************************************************
  ...
**************************************************************************/
static int disconnect_callback(struct GUI *pWidget)
{
  if (get_client_state() == CLIENT_PRE_GAME_STATE) {
    SDL_Rect area;
    struct GUI *pOptions_Button =
	get_widget_pointer_form_main_list(ID_CLIENT_OPTIONS);
    popdown_window_group_dialog(pBeginOptionsWidgetList,
				pEndOptionsWidgetList);
    SDL_Client_Flags &= ~(CF_OPTION_MAIN | CF_OPTION_OPEN |
			  CF_TOGGLED_FULLSCREEN);
    /* hide buton */
    area = pOptions_Button->size;
    SDL_BlitSurface(pOptions_Button->gfx, NULL, Main.screen, &area);
    refresh_rect(pOptions_Button->size);

    /* hide "waiting for game start" label */
    pOptions_Button = get_widget_pointer_form_main_list(ID_WAITING_LABEL);
    area = pOptions_Button->size;
    SDL_BlitSurface(pOptions_Button->gfx, NULL, Main.screen, &area);
    refresh_rect(pOptions_Button->size);
  }

  disconnect_from_server();
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int back_callback(struct GUI *pWidget)
{

  if (SDL_Client_Flags & CF_OPTION_MAIN) {
    struct GUI *pOptions_Button =
	get_widget_pointer_form_main_list(ID_CLIENT_OPTIONS);
    popdown_window_group_dialog(pBeginOptionsWidgetList,
				pEndOptionsWidgetList);
    SDL_Client_Flags &= ~(CF_OPTION_MAIN | CF_OPTION_OPEN |
			  CF_TOGGLED_FULLSCREEN);

    set_wstate(pOptions_Button, WS_NORMAL);
    redraw_icon(pOptions_Button);
    refresh_rect(pOptions_Button->size);
    update_menus();
    return -1;
  }

  del_group_of_widgets_from_gui_list(pBeginOptionsWidgetList,
				     pBeginMainOptionsWidgetList->prev);

  pBeginOptionsWidgetList = pBeginMainOptionsWidgetList;

  show_group(pBeginOptionsWidgetList, pBeginCoreOptionsWidgetList->prev);

  SDL_Client_Flags |= CF_OPTION_MAIN;
  SDL_Client_Flags &= ~CF_TOGGLED_FULLSCREEN;

  redraw_group(pBeginOptionsWidgetList, pEndOptionsWidgetList, 0);

  refresh_rect(pEndOptionsWidgetList->size);

  return -1;
}

/* ===================================================================== */
/* =================================== Public ========================== */
/* ===================================================================== */

/**************************************************************************
  ...
**************************************************************************/
void popup_optiondlg(void)
{
  struct GUI *pTmp_GUI = NULL;

  Uint16 longest = 0;

  Uint16 w = 300;
  Uint16 h = 300;

  Sint16 start_x = (Main.screen->w - w) / 2;
  Sint16 start_y = (Main.screen->h - h) / 2;


  SDL_Surface *pLogo = get_logo_gfx();

  /* create window widget */
  pTmp_GUI =
      create_window(create_str16_from_char(_("Options"), 12), w, h, 0);
  pTmp_GUI->string16->style |= TTF_STYLE_BOLD;
  pTmp_GUI->size.x = start_x;
  pTmp_GUI->size.y = start_y;
  pTmp_GUI->action = main_optiondlg_callback;

  if (resize_window(pTmp_GUI, pLogo, NULL, w, h)) {
    FREESURFACE(pLogo);
  }

  set_wstate(pTmp_GUI, WS_NORMAL);
  add_to_gui_list(ID_OPTIONS_WINDOW, pTmp_GUI);
  pEndOptionsWidgetList = pTmp_GUI;


  /* create exit button */
  pTmp_GUI = create_themeicon_button_from_chars(pTheme->CANCEL_Icon,
						_("Quit"), 12, 0);
  pTmp_GUI->size.x = start_x + w - pTmp_GUI->size.w - 10;
  pTmp_GUI->size.y = start_y + h - pTmp_GUI->size.h - 10;
  /* pTmp_GUI->action = exit_callback; */
  set_wstate(pTmp_GUI, WS_NORMAL);
  add_to_gui_list(ID_OPTIONS_EXIT_BUTTON, pTmp_GUI);

  /* create disconnection button */
  pTmp_GUI = create_themeicon_button_from_chars(pTheme->BACK_Icon,
						_("Disconnect"), 12, 0);
  pTmp_GUI->size.x = start_x + (w - pTmp_GUI->size.w) / 2;
  pTmp_GUI->size.y = start_y + h - pTmp_GUI->size.h - 10;
  pTmp_GUI->action = disconnect_callback;
  set_wstate(pTmp_GUI, WS_NORMAL);
  add_to_gui_list(ID_OPTIONS_DISC_BUTTON, pTmp_GUI);

  /* create back button */
  pTmp_GUI = create_themeicon_button_from_chars(pTheme->BACK_Icon,
						_("Back"), 12, 0);
  pTmp_GUI->size.x = start_x + 10;
  pTmp_GUI->size.y = start_y + h - pTmp_GUI->size.h - 10;
  pTmp_GUI->action = back_callback;
  set_wstate(pTmp_GUI, WS_NORMAL);
  add_to_gui_list(ID_OPTIONS_BACK_BUTTON, pTmp_GUI);
  pBeginCoreOptionsWidgetList = pTmp_GUI;

  /* ============================================================= */

  /* create video button widget */
  pTmp_GUI = create_icon_button_from_chars(NULL,
					   _("Video options"), 12, 0);
  pTmp_GUI->size.y = start_y + 60;
  pTmp_GUI->action = video_callback;
  set_wstate(pTmp_GUI, WS_NORMAL);
  pTmp_GUI->size.h += 4;

  longest = MAX(longest, pTmp_GUI->size.w);

  add_to_gui_list(ID_OPTIONS_VIDEO_BUTTON, pTmp_GUI);

  /* create sound button widget */
  pTmp_GUI = create_icon_button_from_chars(NULL,
					   _("Sound options"), 12, 0);
  pTmp_GUI->size.y = start_y + 90;
  pTmp_GUI->action = sound_callback;
  /* set_wstate( pTmp_GUI, WS_NORMAL ); */
  pTmp_GUI->size.h += 4;
  longest = MAX(longest, pTmp_GUI->size.w);

  add_to_gui_list(ID_OPTIONS_SOUND_BUTTON, pTmp_GUI);


  /* create local button widget */
  pTmp_GUI =
      create_icon_button_from_chars(NULL, _("Game options"), 12, 0);
  pTmp_GUI->size.y = start_y + 120;
  pTmp_GUI->action = local_setting_callback;
  set_wstate(pTmp_GUI, WS_NORMAL);
  pTmp_GUI->size.h += 4;
  longest = MAX(longest, pTmp_GUI->size.w);

  add_to_gui_list(ID_OPTIONS_LOCAL_BUTTON, pTmp_GUI);

  /* create map button widget */
  pTmp_GUI = create_icon_button_from_chars(NULL, _("Map options"), 12, 0);
  pTmp_GUI->size.y = start_y + 150;
  pTmp_GUI->action = map_setting_callback;
  set_wstate(pTmp_GUI, WS_NORMAL);
  pTmp_GUI->size.h += 4;
  longest = MAX(longest, pTmp_GUI->size.w);

  add_to_gui_list(ID_OPTIONS_MAP_BUTTON, pTmp_GUI);


  /* create work lists widget */
  pTmp_GUI = create_icon_button_from_chars(NULL, _("Worklists"), 12, 0);
  pTmp_GUI->size.y = start_y + 180;
  pTmp_GUI->action = work_lists_callback;
  /*set_wstate( pTmp_GUI, WS_NORMAL ); */

  pTmp_GUI->size.h += 4;
  longest = MAX(longest, pTmp_GUI->size.w);

  add_to_gui_list(ID_OPTIONS_WORKLIST_BUTTON, pTmp_GUI);

  pBeginOptionsWidgetList = pTmp_GUI;
  pBeginMainOptionsWidgetList = pTmp_GUI;

  /* seting witdth and stat x */
  do {

    pTmp_GUI->size.w = longest;
    pTmp_GUI->size.x = start_x + (w - pTmp_GUI->size.w) / 2;

    pTmp_GUI = pTmp_GUI->next;
  } while (pTmp_GUI != pBeginCoreOptionsWidgetList);

  /* draw window group */
  redraw_group(pBeginOptionsWidgetList, pEndOptionsWidgetList, 0);

  refresh_rect(pEndOptionsWidgetList->size);

  SDL_Client_Flags |= (CF_OPTION_MAIN | CF_OPTION_OPEN);
  SDL_Client_Flags &= ~CF_TOGGLED_FULLSCREEN;

  update_menus();
}

/**************************************************************************
  ...
**************************************************************************/
void podown_optiondlg(void)
{
  if (SDL_Client_Flags & CF_OPTION_OPEN) {
    popdown_window_group_dialog(pBeginOptionsWidgetList,
				pEndOptionsWidgetList);
    SDL_Client_Flags &= ~(CF_OPTION_MAIN | CF_OPTION_OPEN |
			  CF_TOGGLED_FULLSCREEN);
  }
}
