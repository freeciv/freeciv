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
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
 **********************************************************************/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <SDL/SDL.h>

/* utility */
#include "fcintl.h"
#include "log.h"

/* common */
#include "unit.h"
#include "unitlist.h"

/* client */
#include "civclient.h"
#include "climisc.h"
#include "clinet.h"
#include "overview_common.h"

/* gui-sdl */
#include "citydlg.h"
#include "cityrep.h"
#include "colors.h"
#include "dialogs.h"
#include "finddlg.h"
#include "graphics.h"
#include "gui_iconv.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_mouse.h"
#include "gui_stuff.h"
#include "gui_tilespec.h"
#include "mapview.h"
#include "menu.h"
#include "messagewin.h"
#include "optiondlg.h"
#include "plrdlg.h"
#include "repodlgs.h"
#include "themecolors.h"
#include "wldlg.h"

#include "mapctrl.h"

#undef SCALE_MINIMAP

extern int OVERVIEW_START_X;
extern int OVERVIEW_START_Y;
extern bool is_unit_move_blocked;

static char *pSuggestedCityName = NULL;
static struct SMALL_DLG *pNewCity_Dlg = NULL;
#ifdef SCALE_MINIMAP
static struct SMALL_DLG *pScall_MiniMap_Dlg = NULL;
#endif
static struct SMALL_DLG *pScall_UnitInfo_Dlg = NULL;
static struct ADVANCED_DLG *pUnitInfo_Dlg = NULL;

int MINI_MAP_W = DEFAULT_MINI_MAP_W;
int MINI_MAP_H = DEFAULT_MINI_MAP_H;
int UNITS_W = DEFAULT_UNITS_W;
int UNITS_H = DEFAULT_UNITS_H;

static int INFO_WIDTH, INFO_HEIGHT = 0, INFO_WIDTH_MIN, INFO_HEIGHT_MIN;

bool draw_goto_patrol_lines = FALSE;
static struct widget *pNew_Turn_Button = NULL;
static struct widget *pUnits_Info_Window = NULL;
static struct widget *pMiniMap_Window = NULL;
static struct widget *pFind_City_Button = NULL;
static struct widget *pRevolution_Button = NULL;
static struct widget *pTax_Button = NULL;
static struct widget *pResearch_Button = NULL;

static int popdown_scale_unitinfo_dlg_callback(struct widget *pWidget);
static void Remake_UnitInfo(int w, int h);

/* ================================================================ */

static int players_action_callback(struct widget *pWidget)
{
  set_wstate(pWidget, FC_WS_NORMAL);
  redraw_icon(pWidget);
  sdl_dirty_rect(pWidget->size);
  if (Main.event.type == SDL_MOUSEBUTTONDOWN) {
    switch(Main.event.button.button) {
#if 0    
      case SDL_BUTTON_LEFT:
      
      break;
      case SDL_BUTTON_MIDDLE:
  
      break;
#endif    
      case SDL_BUTTON_RIGHT:
        popup_players_nations_dialog();
      break;
      default:
        popup_players_dialog(true);
      break;
    }
  } else {
    popup_players_dialog(true);
  }
  return -1;
}


static int units_action_callback(struct widget *pWidget)
{
  set_wstate(pWidget, FC_WS_NORMAL);
  redraw_icon(pWidget);
  sdl_dirty_rect(pWidget->size);
  popup_activeunits_report_dialog(FALSE);
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int cities_action_callback(struct widget *pButton)
{
  set_wstate(pButton, FC_WS_DISABLED);
  redraw_icon(pButton);
  sdl_dirty_rect(pButton->size);
  if (Main.event.type == SDL_MOUSEBUTTONDOWN) {
    switch(Main.event.button.button) {
#if 0      
      case SDL_BUTTON_LEFT:
        
      break;
      case SDL_BUTTON_MIDDLE:
  
      break;
#endif      
      case SDL_BUTTON_RIGHT:
        popup_find_dialog();
      break;
      default:
        popup_city_report_dialog(FALSE);
      break;
    }
  } else {
    popup_find_dialog();
  }
    
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int end_turn_callback(struct widget *pButton)
{
  redraw_icon(pButton);
  flush_rect(pButton->size, FALSE);
  disable_focus_animation();
  key_end_turn();
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int revolution_callback(struct widget *pButton)
{
  set_wstate(pButton, FC_WS_DISABLED);
  redraw_icon2(pButton);
  sdl_dirty_rect(pButton->size);
  popup_revolution_dialog();
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int research_callback(struct widget *pButton)
{
  popup_science_dialog(TRUE);
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int economy_callback(struct widget *pButton)
{
  popup_economy_report_dialog(FALSE);
  return -1;
}

/* ====================================== */

/**************************************************************************
  Show/Hide Units Info Window
**************************************************************************/
static int toggle_unit_info_window_callback(struct widget *pIcon_Widget)
{
  struct unit *pFocus = unit_list_get(get_units_in_focus(), 0);
  struct widget *pBuf = NULL;

  clear_surface(pIcon_Widget->theme, NULL);
  alphablit(pTheme->MAP_Icon, NULL, pIcon_Widget->theme, NULL);

  if (pFocus) {
    undraw_order_widgets();
  }
  
  if (SDL_Client_Flags & CF_UNIT_INFO_SHOW) {
    /* HIDE */
    SDL_Surface *pBuf_Surf;
    SDL_Rect src, window_area;

    set_wstate(pIcon_Widget, FC_WS_NORMAL);
    pSellected_Widget = NULL;
  
    if (pUnits_Info_Window->private_data.adv_dlg->pEndActiveWidgetList) {
      del_group(pUnits_Info_Window->private_data.adv_dlg->pBeginActiveWidgetList,
	    	pUnits_Info_Window->private_data.adv_dlg->pEndActiveWidgetList);
    }
    if (pUnits_Info_Window->private_data.adv_dlg->pScroll) {
      hide_scrollbar(pUnits_Info_Window->private_data.adv_dlg->pScroll);
    }
    
    /* clear area under old unit info window */
    window_area = pUnits_Info_Window->size;
    fix_rect(pUnits_Info_Window->dst, &window_area);
    clear_surface(pUnits_Info_Window->dst, &window_area);
    
    /* new button direction */
    alphablit(pTheme->L_ARROW_Icon, NULL, pIcon_Widget->theme, NULL);

    sdl_dirty_rect(pUnits_Info_Window->size);
    copy_chars_to_string16(pIcon_Widget->string16, _("Show Unit Info Window"));
        
    SDL_Client_Flags &= ~CF_UNIT_INFO_SHOW;

    set_new_units_window_pos();

    /* blit part of map window */
    src.x = 0;
    src.y = 0;
    src.w = HIDDEN_UNITS_W;
    src.h = pUnits_Info_Window->theme->h;
      
    alphablit(pUnits_Info_Window->theme, &src , pUnits_Info_Window->dst, &window_area);

    /* blit right vertical frame */
    pBuf_Surf = ResizeSurface(pTheme->FR_Vert, pTheme->FR_Vert->w,
		pUnits_Info_Window->size.h - DOUBLE_FRAME_WH + adj_size(2), 1);

    window_area.y += adj_size(2);
    window_area.x = Main.gui->w - FRAME_WH;
    fix_rect(pUnits_Info_Window->dst, &window_area);
    alphablit(pBuf_Surf, NULL , pUnits_Info_Window->dst, &window_area);
    FREESURFACE(pBuf_Surf);

    /* redraw widgets */
    
    /* ID_ECONOMY */
    pBuf = pUnits_Info_Window->prev;
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
    
    popdown_scale_unitinfo_dlg_callback(NULL);
    
  } else {
    if (Main.gui->w - pUnits_Info_Window->size.w >=
		pMiniMap_Window->size.x + pMiniMap_Window->size.w) {

      set_wstate(pIcon_Widget, FC_WS_NORMAL);
      pSellected_Widget = NULL;
		  
      /* SHOW */
      copy_chars_to_string16(pIcon_Widget->string16, _("Hide Unit Info Window"));
       
      alphablit(pTheme->R_ARROW_Icon, NULL, pIcon_Widget->theme, NULL);

      SDL_Client_Flags |= CF_UNIT_INFO_SHOW;

      set_new_units_window_pos();

      sdl_dirty_rect(pUnits_Info_Window->size);
    
      redraw_unit_info_label(pFocus);
    } else {
      alphablit(pTheme->L_ARROW_Icon, NULL, pIcon_Widget->theme, NULL);
      real_redraw_icon(pIcon_Widget);
      sdl_dirty_rect(pIcon_Widget->size);
    }
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
static int toggle_map_window_callback(struct widget *pMap_Button)
{
  struct unit *pFocus = unit_list_get(get_units_in_focus(), 0);
  struct widget *pMap = pMiniMap_Window;
    
  /* make new map icon */
  clear_surface(pMap_Button->theme, NULL);
  alphablit(pTheme->MAP_Icon, NULL, pMap_Button->theme, NULL);

  set_wstate(pMap, FC_WS_NORMAL);

  if (pFocus) {
    undraw_order_widgets();
  }
  
  if (SDL_Client_Flags & CF_MINI_MAP_SHOW) {
    /* Hide MiniMap */
    SDL_Surface *pBuf_Surf;
    SDL_Rect src, map_area = pMap->size;

    set_wstate(pMap_Button, FC_WS_NORMAL);
    pSellected_Widget = NULL;
    
    sdl_dirty_rect(pMap->size);
    copy_chars_to_string16(pMap_Button->string16, _("Show MiniMap"));
        
    /* make new map icon */
    alphablit(pTheme->R_ARROW_Icon, NULL, pMap_Button->theme, NULL);

    SDL_Client_Flags &= ~CF_MINI_MAP_SHOW;
    
    /* clear area under old map window */
    fix_rect(pMap->dst, &map_area);
    clear_surface(pMap->dst, &map_area);
        
    pMap->size.w = HIDDEN_MINI_MAP_W;

    set_new_mini_map_window_pos();
    
    /* blit part of map window */
    src.x = pMap->theme->w - HIDDEN_MINI_MAP_W - FRAME_WH;
    src.y = 0;
    src.w = HIDDEN_MINI_MAP_W;
    src.h = pMap->theme->h;
      
    alphablit(pMap->theme, &src , pMap->dst, &map_area);
  
    /* blit left vertical frame theme */
    pBuf_Surf = ResizeSurface(pTheme->FR_Vert, pTheme->FR_Vert->w,
				pMap->size.h - DOUBLE_FRAME_WH + adj_size(2), 1);

    map_area.y += adj_size(2);
    alphablit(pBuf_Surf, NULL , pMap->dst, &map_area);
    FREESURFACE(pBuf_Surf);
  
    /* redraw widgets */  
    /* ID_NEW_TURN */
    pMap = pMap->prev;
    real_redraw_icon(pMap);

    /* ID_PLAYERS */
    pMap = pMap->prev;
    real_redraw_icon(pMap);

    /* ID_CITIES */
    pMap = pMap->prev;
    real_redraw_icon(pMap);

    /* ID_UNITS */
    pMap = pMap->prev;
    if ((get_wflags(pMap) & WF_HIDDEN) != WF_HIDDEN) {
      real_redraw_icon(pMap);
    }
    
    /* ID_CHATLINE_TOGGLE_LOG_WINDOW_BUTTON */
    pMap = pMap->prev;
    if ((get_wflags(pMap) & WF_HIDDEN) != WF_HIDDEN) {
      real_redraw_icon(pMap);
    }

    /* Toggle Minimap mode */
    pMap = pMap->prev;
    if ((get_wflags(pMap) & WF_HIDDEN) != WF_HIDDEN) {
      real_redraw_icon(pMap);
    }
    
    #ifdef SMALL_SCREEN
    /* options */
    pMap = pMap->prev;
    if ((get_wflags(pMap) & WF_HIDDEN) != WF_HIDDEN) {
      real_redraw_icon(pMap);
    }
    #endif
    
    /* ID_TOGGLE_MAP_WINDOW_BUTTON */
    pMap = pMap->prev;
    real_redraw_icon(pMap);

#ifdef SCALE_MINIMAP
    popdown_scale_minmap_dlg_callback(NULL);
#endif

  } else {
    if (MINI_MAP_W <= pUnits_Info_Window->size.x) {
      
      set_wstate(pMap_Button, FC_WS_NORMAL);
      pSellected_Widget = NULL;
      
      /* show MiniMap */
      copy_chars_to_string16(pMap_Button->string16, _("Hide MiniMap"));
        
      alphablit(pTheme->L_ARROW_Icon, NULL, pMap_Button->theme, NULL);
      SDL_Client_Flags |= CF_MINI_MAP_SHOW;
      pMap->size.w = MINI_MAP_W;
      set_new_mini_map_window_pos();
    
      refresh_overview(); /* Is a full refresh needed? */
      sdl_dirty_rect(pMap->size);
    } else {
      alphablit(pTheme->R_ARROW_Icon, NULL, pMap_Button->theme, NULL);
      real_redraw_icon(pMap_Button);
      sdl_dirty_rect(pMap_Button->size);
    }
  }

  if (pFocus) {
    update_order_widget();
  }

  flush_dirty();
  return -1;
}

/* ====================================================================== */


static int togle_minimap_mode(struct widget *pWidget)
{
  if (pWidget) {
    pSellected_Widget = pWidget;
    set_wstate(pWidget, FC_WS_SELLECTED);
  }
  toggle_overview_mode();
  refresh_overview();
  flush_dirty();
  return -1;
}

static int togle_msg_window(struct widget *pWidget)
{
  
  if (is_meswin_open()) {
    popdown_meswin_dialog();
    copy_chars_to_string16(pWidget->string16, _("Show Log (F10)"));
  } else {
    popup_meswin_dialog(true);
    copy_chars_to_string16(pWidget->string16, _("Hide Log (F10)"));
  }

  pSellected_Widget = pWidget;
  set_wstate(pWidget, FC_WS_SELLECTED);
  real_redraw_icon(pWidget);
  sdl_dirty_rect(pWidget->size);
  
  flush_dirty();
  return -1;
}

int resize_minimap(void)
{
  int w = OVERVIEW_TILE_WIDTH * map.xsize;
  int h = OVERVIEW_TILE_HEIGHT * map.ysize;
  int current_w = pMiniMap_Window->size.w - BLOCKM_W - DOUBLE_FRAME_WH;
  int current_h = pMiniMap_Window->size.h - DOUBLE_FRAME_WH;
  
  if ((((current_w > DEFAULT_MINI_MAP_W - BLOCKM_W - DOUBLE_FRAME_WH)
   || (w > DEFAULT_MINI_MAP_W - BLOCKM_W - DOUBLE_FRAME_WH)) && (current_w != w)) ||
    (((current_h > DEFAULT_MINI_MAP_H - DOUBLE_FRAME_WH)
   || (h > DEFAULT_MINI_MAP_H - DOUBLE_FRAME_WH)) && (current_h != h))) {
    Remake_MiniMap(w, h);
  }
  center_minimap_on_minimap_window();
  refresh_overview();
  update_menus();
  
  return 0;
}

#ifdef SCALE_MINIMAP
/* ============================================================== */
static int move_scale_minmap_dlg_callback(struct widget *pWindow)
{
  return std_move_window_group_callback(pScall_MiniMap_Dlg->pBeginWidgetList,
								pWindow);
}

static int popdown_scale_minmap_dlg_callback(struct widget *pWidget)
{
  if (pScall_MiniMap_Dlg) {
    popdown_window_group_dialog(pScall_MiniMap_Dlg->pBeginWidgetList,
    				pScall_MiniMap_Dlg->pEndWidgetList);
    FC_FREE(pScall_MiniMap_Dlg);
    if (pWidget) {
      flush_dirty();
    }
  }
  return -1;
}

static int up_width_callback(struct widget *pWidget)
{
  redraw_widget(pWidget);
  sdl_dirty_rect(pWidget->size);
  if (((OVERVIEW_TILE_WIDTH + 1) * map.xsize + BLOCKM_W + DOUBLE_FRAME_WH) <=
					pUnits_Info_Window->size.x) {
    char cBuf[4];
    my_snprintf(cBuf, sizeof(cBuf), "%d", OVERVIEW_TILE_WIDTH);
    copy_chars_to_string16(pWidget->next->string16, cBuf);
    redraw_label(pWidget->next);
    sdl_dirty_rect(pWidget->next->size);
    
    resize_minimap();
  }
  flush_dirty();
  return -1;
}

static int down_width_callback(struct widget *pWidget)
{
  redraw_widget(pWidget);
  sdl_dirty_rect(pWidget->size);
  if (OVERVIEW_TILE_WIDTH > 1) {
    char cBuf[4];
    
    my_snprintf(cBuf, sizeof(cBuf), "%d", OVERVIEW_TILE_WIDTH);
    copy_chars_to_string16(pWidget->prev->string16, cBuf);
    redraw_label(pWidget->prev);
    sdl_dirty_rect(pWidget->prev->size);
    
    resize_minimap();
  }
  flush_dirty();
  return -1;
}

static int up_height_callback(struct widget *pWidget)
{
  redraw_widget(pWidget);
  sdl_dirty_rect(pWidget->size);
  if (Main.gui->h -
    ((OVERVIEW_TILE_HEIGHT + 1) * map.ysize + DOUBLE_FRAME_WH) >= 40) {
    char cBuf[4];
      
    OVERVIEW_TILE_HEIGHT++;
    my_snprintf(cBuf, sizeof(cBuf), "%d", OVERVIEW_TILE_HEIGHT);
    copy_chars_to_string16(pWidget->next->string16, cBuf);
    redraw_label(pWidget->next);
    sdl_dirty_rect(pWidget->next->size);
    resize_minimap();
  }
  flush_dirty();
  return -1;
}

static int down_height_callback(struct widget *pWidget)
{
  redraw_widget(pWidget);
  sdl_dirty_rect(pWidget->size);
  if (OVERVIEW_TILE_HEIGHT > 1) {
    char cBuf[4];
    
    OVERVIEW_TILE_HEIGHT--;
    my_snprintf(cBuf, sizeof(cBuf), "%d", OVERVIEW_TILE_HEIGHT);
    copy_chars_to_string16(pWidget->prev->string16, cBuf);
    redraw_label(pWidget->prev);
    sdl_dirty_rect(pWidget->prev->size);
    
    resize_minimap();
  }
  flush_dirty();
  return -1;
}

static void popup_minimap_scale_dialog(void)
{
  SDL_Surface *pText1, *pText2;
  SDL_String16 *pStr = NULL;
  struct widget *pWindow = NULL;
  struct widget *pBuf = NULL;
  char cBuf[4];
  int h = WINDOW_TILE_HIGH + FRAME_WH + 1, w = 0;
  
  if (pScall_MiniMap_Dlg || !(SDL_Client_Flags & CF_MINI_MAP_SHOW)) {
    return;
  }
  
  pStr = create_str16_from_char(_("Single Tile Width"), adj_font(12));
  pText1 = create_text_surf_from_str16(pStr);
  w = MAX(w, pText1->w + adj_size(30));
    
  copy_chars_to_string16(pStr, _("Single Tile Height"));
  pText2 = create_text_surf_from_str16(pStr);
  w = MAX(w, pText2->w + adj_size(30));
  FREESTRING16(pStr);
  
  pScall_MiniMap_Dlg = fc_calloc(1, sizeof(struct SMALL_DLG));
    
  /* create window */
  pStr = create_str16_from_char(_("Scale Minimap"), adj_font(12));
  pStr->style |= TTF_STYLE_BOLD;
  pWindow = create_window(NULL, pStr, adj_size(10), adj_size(10), 0);
  pWindow->action = move_scale_minmap_dlg_callback;
  set_wstate(pWindow, FC_WS_NORMAL);
  w = MAX(w, pWindow->size.w);
  add_to_gui_list(ID_WINDOW, pWindow);
  pScall_MiniMap_Dlg->pEndWidgetList = pWindow;
  
  /* ----------------- */
  pBuf = create_themeicon_button(pTheme->L_ARROW_Icon, pWindow->dst, NULL, 0);
  pBuf->action = down_width_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
  add_to_gui_list(ID_BUTTON, pBuf);
  
  my_snprintf(cBuf, sizeof(cBuf), "%d" , OVERVIEW_TILE_WIDTH);
  pStr = create_str16_from_char(cBuf, adj_font(24));
  pStr->style |= (TTF_STYLE_BOLD|SF_CENTER);
  pBuf = create_iconlabel(NULL, pWindow->dst, pStr, WF_DRAW_THEME_TRANSPARENT);
  pBuf->size.w = MAX(adj_size(50), pBuf->size.w);
  h += pBuf->size.h + adj_size(5);
  add_to_gui_list(ID_LABEL, pBuf);
  
  pBuf = create_themeicon_button(pTheme->R_ARROW_Icon, pWindow->dst, NULL, 0);
  pBuf->action = up_width_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
  add_to_gui_list(ID_BUTTON, pBuf);
  
  
  /* ------------ */
  pBuf = create_themeicon_button(pTheme->L_ARROW_Icon, pWindow->dst, NULL, 0);
  pBuf->action = down_height_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
  add_to_gui_list(ID_BUTTON, pBuf);
  
  my_snprintf(cBuf, sizeof(cBuf), "%d" , OVERVIEW_TILE_HEIGHT);
  pStr = create_str16_from_char(cBuf, adj_font(24));
  pStr->style |= (TTF_STYLE_BOLD|SF_CENTER);
  pBuf = create_iconlabel(NULL, pWindow->dst, pStr, WF_DRAW_THEME_TRANSPARENT);
  pBuf->size.w = MAX(adj_size(50), pBuf->size.w);
  h += pBuf->size.h + adj_size(20);
  add_to_gui_list(ID_LABEL, pBuf);
  
  pBuf = create_themeicon_button(pTheme->R_ARROW_Icon, pWindow->dst, NULL, 0);
  pBuf->action = up_height_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
  add_to_gui_list(ID_BUTTON, pBuf);
  w = MAX(w , pBuf->size.w * 2 + pBuf->next->size.w + adj_size(20));
  
  /* ------------ */
  pStr = create_str16_from_char(_("Exit"), adj_font(12));
  pBuf = create_themeicon_button(pTheme->CANCEL_Icon,
						  pWindow->dst, pStr, 0);
  pBuf->action = popdown_scale_minmap_dlg_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pScall_MiniMap_Dlg->pBeginWidgetList = pBuf;
  add_to_gui_list(ID_BUTTON, pBuf);
  h += pBuf->size.h + adj_size(10);
  w = MAX(w, pBuf->size.w + adj_size(20));
  /* ------------ */
  
  h += adj_size(20); 
  
  if (Main.event.motion.x + w > pWindow->dst->w)
  {
    if (Main.event.motion.x - w >= 0)
    {
      pWindow->size.x = Main.event.motion.x - w;
    }
    else
    {
      pWindow->size.x = (pWindow->dst->w - w) / 2;
    }
  }
  else
  {
    pWindow->size.x = Main.event.motion.x;
  }
    
  if (Main.event.motion.y + h >= pWindow->dst->h)
  {
    if (Main.event.motion.y - h >= 0)
    {
      pWindow->size.y = Main.event.motion.y - h;
    }
    else
    {
      pWindow->size.y = (pWindow->dst->h - h) / 2;
    }
  }
  else
  {
    pWindow->size.y = Main.event.motion.y;
  }

  set_window_pos(pWindow, pWindow->size.x, pWindow->size.y);  

  resize_window(pWindow, NULL,
		get_game_colorRGB(COLOR_STD_BACKGROUND_BROWN), w, h);

  blit_entire_src(pText1, pWindow->theme, 15, WINDOW_TILE_HIGH + adj_size(5));
  FREESURFACE(pText1);
  
  /* width label */
  pBuf = pWindow->prev->prev;
  pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + adj_size(20);
  pBuf->size.x = pWindow->size.x + (pWindow->size.w - pBuf->size.w) / 2;
  
  /* width left button */
  pBuf->next->size.y = pBuf->size.y + pBuf->size.h - pBuf->next->size.h;
  pBuf->next->size.x = pBuf->size.x - pBuf->next->size.w;
  
  /* width right button */
  pBuf->prev->size.y = pBuf->size.y + pBuf->size.h - pBuf->prev->size.h;
  pBuf->prev->size.x = pBuf->size.x + pBuf->size.w;
  
  /* height label */
  pBuf = pBuf->prev->prev->prev;
  pBuf->size.y = pBuf->next->next->next->size.y + pBuf->next->next->next->size.h + adj_size(20);
  pBuf->size.x = pWindow->size.x + (pWindow->size.w - pBuf->size.w) / 2;
  
  blit_entire_src(pText2, pWindow->theme, adj_size(15), pBuf->size.y - pWindow->size.y - pText2->h - adj_size(2));
  FREESURFACE(pText2);
    
  /* height left button */
  pBuf->next->size.y = pBuf->size.y + pBuf->size.h - pBuf->next->size.h;
  pBuf->next->size.x = pBuf->size.x - pBuf->next->size.w;
  
  /* height right button */
  pBuf->prev->size.y = pBuf->size.y + pBuf->size.h - pBuf->prev->size.h;
  pBuf->prev->size.x = pBuf->size.x + pBuf->size.w;
  
  /* exit button */
  pBuf = pBuf->prev->prev;
  pBuf->size.x = pWindow->size.x + (pWindow->size.w - pBuf->size.w) / 2;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - adj_size(10);
  
  /* -------------------- */
  redraw_group(pScall_MiniMap_Dlg->pBeginWidgetList, pWindow, 0);
  flush_rect(pWindow->size, FALSE);
  
}
#endif

/* ==================================================================== */

static int move_scale_unitinfo_dlg_callback(struct widget *pWindow)
{
  return std_move_window_group_callback(pScall_UnitInfo_Dlg->pBeginWidgetList,
								pWindow);
}

static int popdown_scale_unitinfo_dlg_callback(struct widget *pWidget)
{
  if(pScall_UnitInfo_Dlg) {
    popdown_window_group_dialog(pScall_UnitInfo_Dlg->pBeginWidgetList,
    				pScall_UnitInfo_Dlg->pEndWidgetList);
    FC_FREE(pScall_UnitInfo_Dlg);
    if(pWidget) {
      flush_dirty();
    }
  }
  return -1;
}

int resize_unit_info(void)
{
  int w = INFO_WIDTH * map.xsize;
  int h = INFO_HEIGHT * map.ysize;
  int current_w = pUnits_Info_Window->size.w - BLOCKU_W - DOUBLE_FRAME_WH;
  int current_h = pUnits_Info_Window->size.h - DOUBLE_FRAME_WH;
  
  if ((((current_w > DEFAULT_UNITS_W - BLOCKU_W - DOUBLE_FRAME_WH)
   || (w > DEFAULT_UNITS_W - BLOCKU_W - DOUBLE_FRAME_WH)) && (current_w != w)) ||
    (((current_h > DEFAULT_UNITS_H - DOUBLE_FRAME_WH)
   || (h > DEFAULT_UNITS_H - DOUBLE_FRAME_WH)) && (current_h != h))) {
    Remake_UnitInfo(w, h);
  }
  
  update_menus();
  update_unit_info_label(get_units_in_focus());
      
  return 0;
}

static int up_info_width_callback(struct widget *pWidget)
{
  redraw_widget(pWidget);
  sdl_dirty_rect(pWidget->size);
  if (Main.gui->w -
    ((INFO_WIDTH + 1) * map.xsize + BLOCKU_W + DOUBLE_FRAME_WH) >=
		pMiniMap_Window->size.x + pMiniMap_Window->size.w) {
    INFO_WIDTH++;
    resize_unit_info();
  }
  flush_dirty();
  return -1;
}

static int down_info_width_callback(struct widget *pWidget)
{
  redraw_widget(pWidget);
  sdl_dirty_rect(pWidget->size);
  if(INFO_WIDTH > INFO_WIDTH_MIN) {
    INFO_WIDTH--;
    resize_unit_info();
  }
  flush_dirty();
  return -1;
}

static int up_info_height_callback(struct widget *pWidget)
{
  redraw_widget(pWidget);
  sdl_dirty_rect(pWidget->size);
  if(Main.gui->h -
    ((INFO_HEIGHT + 1) * map.ysize + DOUBLE_FRAME_WH) >= 40) {
    INFO_HEIGHT++;
    resize_unit_info();
  }
  flush_dirty();
  return -1;
}

static int down_info_height_callback(struct widget *pWidget)
{
  redraw_widget(pWidget);
  sdl_dirty_rect(pWidget->size);
  if(INFO_HEIGHT > INFO_HEIGHT_MIN) {
    INFO_HEIGHT--;    
    resize_unit_info();
  }
  flush_dirty();
  return -1;
}

static void popup_unitinfo_scale_dialog(void)
{
  SDL_Surface *pText1, *pText2;
  SDL_String16 *pStr = NULL;
  struct widget *pWindow = NULL;
  struct widget *pBuf = NULL;
  int h = WINDOW_TILE_HIGH + FRAME_WH + 1, w = 0;
  
  if(pScall_UnitInfo_Dlg || !(SDL_Client_Flags & CF_UNIT_INFO_SHOW)) {
    return;
  }
  
  pStr = create_str16_from_char(_("Width"), adj_font(12));
  pText1 = create_text_surf_from_str16(pStr);
  w = MAX(w, pText1->w + adj_size(30));
  h += MAX(adj_size(20), pText1->h + adj_size(4));
  copy_chars_to_string16(pStr, _("Height"));
  pText2 = create_text_surf_from_str16(pStr);
  w = MAX(w, pText2->w + adj_size(30));
  h += MAX(adj_size(20), pText2->h + adj_size(4));
  FREESTRING16(pStr);
  
  pScall_UnitInfo_Dlg = fc_calloc(1, sizeof(struct SMALL_DLG));
    
  /* create window */
  pStr = create_str16_from_char(_("Scale Unit Info"), adj_font(12));
  pStr->style |= TTF_STYLE_BOLD;
  pWindow = create_window(NULL, pStr, adj_size(10), adj_size(10), 0);
  pWindow->action = move_scale_unitinfo_dlg_callback;
  set_wstate(pWindow, FC_WS_NORMAL);
  w = MAX(w, pWindow->size.w);
  add_to_gui_list(ID_WINDOW, pWindow);
  pScall_UnitInfo_Dlg->pEndWidgetList = pWindow;
  
  /* ----------------- */
  pBuf = create_themeicon_button(pTheme->L_ARROW_Icon, pWindow->dst, NULL, 0);
  pBuf->action = down_info_width_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
  add_to_gui_list(ID_BUTTON, pBuf);
  h += pBuf->size.h;  
  
  pBuf = create_themeicon_button(pTheme->R_ARROW_Icon, pWindow->dst, NULL, 0);
  pBuf->action = up_info_width_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
  add_to_gui_list(ID_BUTTON, pBuf);
    
  /* ------------ */
  pBuf = create_themeicon_button(pTheme->L_ARROW_Icon, pWindow->dst, NULL, 0);
  pBuf->action = down_info_height_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
  add_to_gui_list(ID_BUTTON, pBuf);
  h += pBuf->size.h + adj_size(10);
  
  pBuf = create_themeicon_button(pTheme->R_ARROW_Icon, pWindow->dst, NULL, 0);
  pBuf->action = up_info_height_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
  add_to_gui_list(ID_BUTTON, pBuf);
  w = MAX(w , pBuf->size.w * 2 + adj_size(20));
    
  /* ------------ */
  pStr = create_str16_from_char(_("Exit"), adj_font(12));
  pBuf = create_themeicon_button(pTheme->CANCEL_Icon,
						  pWindow->dst, pStr, 0);
  pBuf->action = popdown_scale_unitinfo_dlg_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pScall_UnitInfo_Dlg->pBeginWidgetList = pBuf;
  add_to_gui_list(ID_BUTTON, pBuf);
  h += pBuf->size.h + adj_size(10);
  w = MAX(w, pBuf->size.w + adj_size(20));
  /* ------------ */
  
  if (Main.event.motion.x + w > pWindow->dst->w)
  {
    if (Main.event.motion.x - w >= 0)
    {
      pWindow->size.x = Main.event.motion.x - w;
    }
    else
    {
      pWindow->size.x = (pWindow->dst->w - w) / 2;
    }
  }
  else
  {
    pWindow->size.x = Main.event.motion.x;
  }
    
  if (Main.event.motion.y + h >= pWindow->dst->h)
  {
    if (Main.event.motion.y - h >= 0)
    {
      pWindow->size.y = Main.event.motion.y - h;
    }
    else
    {
      pWindow->size.y = (pWindow->dst->h - h) / 2;
    }
  }
  else
  {
    pWindow->size.y = Main.event.motion.y;
  }
  
  set_window_pos(pWindow, pWindow->size.x, pWindow->size.y);
  
  resize_window(pWindow, NULL,
		get_game_colorRGB(COLOR_THEME_BACKGROUND), w, h);
    
  /* width left button */
  pBuf = pWindow->prev;
  pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + MAX(adj_size(20), pText1->h + adj_size(4));
  pBuf->size.x = pWindow->size.x + (pWindow->size.w - pBuf->size.w * 2) / 2;
  blit_entire_src(pText1, pWindow->theme, adj_size(15), pBuf->size.y
					  - pWindow->size.y - pText1->h - adj_size(2));
  FREESURFACE(pText1);
  
  /* width right button */
  pBuf->prev->size.y = pBuf->size.y;
  pBuf->prev->size.x = pBuf->size.x + pBuf->size.w;
  
  /* height left button */
  pBuf = pBuf->prev->prev;
  pBuf->size.y = pBuf->next->next->size.y +
  			pBuf->next->next->size.h + MAX(adj_size(20), pText2->h + adj_size(4));
  pBuf->size.x = pWindow->size.x + (pWindow->size.w - pBuf->size.w * 2) / 2;
  
  blit_entire_src(pText2, pWindow->theme, adj_size(15), pBuf->size.y - pWindow->size.y - pText2->h - adj_size(2));
  FREESURFACE(pText2);
    
  /* height right button */
  pBuf->prev->size.y = pBuf->size.y;
  pBuf->prev->size.x = pBuf->size.x + pBuf->size.w;
  
  /* exit button */
  pBuf = pBuf->prev->prev;
  pBuf->size.x = pWindow->size.x + (pWindow->size.w - pBuf->size.w) / 2;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - adj_size(10);
    
  if (!INFO_HEIGHT) {
    INFO_WIDTH_MIN = (DEFAULT_UNITS_W - BLOCKU_W - DOUBLE_FRAME_WH) / map.xsize;
    if (INFO_WIDTH_MIN == 1) {
      INFO_WIDTH_MIN = 0;
    }
    INFO_HEIGHT_MIN = (DEFAULT_UNITS_H - DOUBLE_FRAME_WH) / map.ysize;
    if (!INFO_HEIGHT_MIN) {
      INFO_HEIGHT_MIN = 1;
    }  
    INFO_WIDTH = INFO_WIDTH_MIN;
    INFO_HEIGHT = INFO_HEIGHT_MIN;
  }
  
  /* -------------------- */
  redraw_group(pScall_UnitInfo_Dlg->pBeginWidgetList, pWindow, 0);
  flush_rect(pWindow->size, FALSE);
  
}


/* ==================================================================== */
static int minimap_window_callback(struct widget *pWidget)
{
  switch(Main.event.button.button) {
    case SDL_BUTTON_RIGHT:    
    /* FIXME: scaling needs to be fixed */
#ifdef SCALE_MINIMAP
      popup_minimap_scale_dialog();
#endif    
    break;
    default:
      if ((SDL_Client_Flags & CF_MINI_MAP_SHOW) &&  
         (Main.event.motion.x >= OVERVIEW_START_X) &&
         (Main.event.motion.x <
	   OVERVIEW_START_X + OVERVIEW_TILE_WIDTH * map.xsize) &&
         (Main.event.motion.y >=
	   Main.gui->h - pMiniMap_Window->size.h + OVERVIEW_START_Y) &&
         (Main.event.motion.y <
	   Main.gui->h - pMiniMap_Window->size.h + OVERVIEW_START_Y +
			  OVERVIEW_TILE_HEIGHT * map.ysize)) {
                              
        int map_x, map_y;
                            
        overview_to_map_pos(&map_x, &map_y, 
                   Main.event.motion.x - OVERVIEW_START_X, Main.event.motion.y -
                     (Main.gui->h - pMiniMap_Window->size.h + OVERVIEW_START_Y));
                              
        center_tile_mapcanvas(map_pos_to_tile(map_x, map_y));
                              
      }

    break;
  }
  return -1;
}

static int unit_info_window_callback(struct widget *pWidget)
{
  switch(Main.event.button.button) {
#if 0    
    case SDL_BUTTON_LEFT:
      
    break;
#endif
    case SDL_BUTTON_MIDDLE:
      request_center_focus_unit();
    break;
    case SDL_BUTTON_RIGHT:
      popup_unitinfo_scale_dialog();
    break;
    default:
      key_unit_wait();
    break;
  }
  
  return -1;
}

/* ============================== Public =============================== */

/**************************************************************************
  This Function is used when resize Main.screen.
  We must set new Units Info Win. start position.
**************************************************************************/
void set_new_units_window_pos(void)
{
  struct widget *pUnit_Window = pUnits_Info_Window;
  struct widget *pWidget;

  if (SDL_Client_Flags & CF_UNIT_INFO_SHOW) {
    pUnit_Window->size.x = Main.screen->w - pUnit_Window->size.w;
  } else {
    pUnit_Window->size.x = Main.screen->w - BLOCKU_W - DOUBLE_FRAME_WH;
  }

  pUnit_Window->size.y = Main.screen->h - pUnit_Window->size.h;
  pUnit_Window->dst = Main.gui;
  
  /* ID_ECONOMY */
  pWidget = pTax_Button;
  pWidget->size.x = pUnit_Window->size.x + FRAME_WH
                                             + (BLOCKU_W - pWidget->size.w)/2;                                 

  pWidget->size.y = Main.screen->h - UNITS_H + FRAME_WH + adj_size(2);
  pWidget->dst = Main.gui;
  
  /* ID_RESEARCH */
  pWidget = pWidget->prev;
  pWidget->size.x = pUnit_Window->size.x + FRAME_WH
                                             + (BLOCKU_W - pWidget->size.w)/2;    
  pWidget->size.y = Main.screen->h - UNITS_H + FRAME_WH + pWidget->size.h + adj_size(2);
  pWidget->dst = Main.gui;
  
  /* ID_REVOLUTION */
  pWidget = pWidget->prev;
  pWidget->size.x = pUnit_Window->size.x + FRAME_WH
                                             + (BLOCKU_W - pWidget->size.w)/2;    
  pWidget->size.y = Main.screen->h - UNITS_H + FRAME_WH +
      						(pWidget->size.h << 1) + adj_size(2);
  pWidget->dst = Main.gui;
  
  /* ID_TOGGLE_UNITS_WINDOW_BUTTON */
  pWidget = pWidget->prev;
  pWidget->size.x = pUnit_Window->size.x + FRAME_WH
                                             + (BLOCKU_W - pWidget->size.w)/2;    
  pWidget->size.y = Main.screen->h - FRAME_WH - pWidget->size.h - adj_size(2);
  pWidget->dst = Main.gui;
}

/**************************************************************************
  This Function is used when resize Main.screen.
  We must set new MiniMap start position.
**************************************************************************/
void set_new_mini_map_window_pos(void)
{
  int new_x;
  struct widget *pMM_Window = pMiniMap_Window;
    
  if (SDL_Client_Flags & CF_MINI_MAP_SHOW) {
    new_x = pMM_Window->size.w - BLOCKM_W + adj_size(1);
  } else {
    new_x = FRAME_WH;
  }

  pMM_Window->size.y = Main.screen->h - pMM_Window->size.h;
  pMM_Window->dst = Main.gui;
  
  /* ID_NEW_TURN */
  pMM_Window = pMM_Window->prev;
  pMM_Window->size.x = new_x + pMM_Window->size.w;
  pMM_Window->size.y = Main.screen->h - MINI_MAP_H + FRAME_WH + adj_size(2);
  pMM_Window->dst = Main.gui;
  
  /* PLAYERS BUTTON */
  pMM_Window = pMM_Window->prev;
  pMM_Window->size.x = new_x + pMM_Window->size.w;
  pMM_Window->size.y = Main.screen->h - MINI_MAP_H + FRAME_WH + adj_size(2) +
      						pMM_Window->size.h;
  pMM_Window->dst = Main.gui;
  
  /* ID_FIND_CITY */
  pMM_Window = pMM_Window->prev;
  pMM_Window->size.x = new_x + pMM_Window->size.w;
  pMM_Window->size.y = Main.screen->h - MINI_MAP_H + FRAME_WH + adj_size(2) +
      						pMM_Window->size.h * 2;
  pMM_Window->dst = Main.gui;
  
  
  /* UNITS BUTTON */
  pMM_Window = pMM_Window->prev;
  pMM_Window->size.x = new_x;
  pMM_Window->size.y = Main.screen->h - MINI_MAP_H + FRAME_WH + adj_size(2);
  pMM_Window->dst = Main.gui;
  
  
  /* ID_CHATLINE_TOGGLE_LOG_WINDOW_BUTTON */
  pMM_Window = pMM_Window->prev;
  pMM_Window->size.x = new_x;
  pMM_Window->size.y = Main.screen->h - MINI_MAP_H + FRAME_WH + adj_size(2) +
      						pMM_Window->size.h;
 
  /* Toggle minimap mode */
  pMM_Window = pMM_Window->prev;
  pMM_Window->size.x = new_x;
  pMM_Window->size.y = Main.screen->h - MINI_MAP_H + FRAME_WH + adj_size(2) +
      						pMM_Window->size.h * 2;
						
  #ifdef SMALL_SCREEN
  /* ID_TOGGLE_MAP_WINDOW_BUTTON */
  pMM_Window = pMM_Window->prev;
  pMM_Window->size.x = new_x;
  pMM_Window->size.y = Main.screen->h - FRAME_WH - pMM_Window->size.h - adj_size(2);
  pMM_Window->dst = Main.gui;
  #endif
						
  /* ID_TOGGLE_MAP_WINDOW_BUTTON */
  pMM_Window = pMM_Window->prev;
  pMM_Window->size.x = new_x + pMM_Window->size.w;
  pMM_Window->size.y = Main.screen->h - FRAME_WH - pMM_Window->size.h - adj_size(2);
  pMM_Window->dst = Main.gui;
}

void Remake_MiniMap(int w, int h)
{
  SDL_Surface *pSurf;
  struct widget *pWidget = pMiniMap_Window;
  SDL_Rect dest;
    
  w += BLOCKM_W + DOUBLE_FRAME_WH;
  
  if(h < DEFAULT_MINI_MAP_H - DOUBLE_FRAME_WH) {
    h = DEFAULT_MINI_MAP_H;
  } else {
    h += DOUBLE_FRAME_WH;
  }
  
  if(pWidget->size.w > w || pWidget->size.h > h) {
    /* clear area under old map window */
    dest = pWidget->size;
    fix_rect(pWidget->dst, &dest);
    clear_surface(pWidget->dst, &dest);
    sdl_dirty_rect(pWidget->size);
  }
  
  pWidget->size.y = Main.gui->h - h;
  pWidget->size.w = w;
  pWidget->size.h = h;
  
  FREESURFACE(pWidget->theme);
  
  pWidget->theme = create_surf_alpha(w, h, SDL_SWSURFACE);  
     
  draw_frame(pWidget->theme, 0, 0, pWidget->size.w, pWidget->size.h);
  
  pSurf = ResizeSurface(pTheme->Block, BLOCKM_W,
				pWidget->size.h - DOUBLE_FRAME_WH, 1);
  
  blit_entire_src(pSurf, pWidget->theme,
			pWidget->size.w - FRAME_WH - pSurf->w, FRAME_WH);
  FREESURFACE(pSurf);  
  
  /* new turn button */
  pWidget = pWidget->prev;
  FREESURFACE(pWidget->gfx);
  pWidget->size.x = w - BLOCKM_W + pWidget->size.w + adj_size(1);
  pWidget->size.y = pWidget->dst->h - h + FRAME_WH + adj_size(2);
  
  /* players */
  pWidget = pWidget->prev;
  FREESURFACE(pWidget->gfx);
  pWidget->size.x = w - BLOCKM_W + pWidget->size.w + adj_size(1);
  pWidget->size.y = pWidget->dst->h - h + FRAME_WH + adj_size(2) + pWidget->size.h;
  
  /* find city */
  pWidget = pWidget->prev;
  FREESURFACE(pWidget->gfx);
  pWidget->size.x = w - BLOCKM_W + pWidget->size.w + adj_size(1);
  pWidget->size.y = pWidget->dst->h - h + FRAME_WH + adj_size(2) + pWidget->size.h * 2;

  /* units */
  pWidget = pWidget->prev;
  FREESURFACE(pWidget->gfx);
  pWidget->size.x = w - BLOCKM_W + adj_size(1);
  pWidget->size.y = pWidget->dst->h - h + FRAME_WH + adj_size(2);

  /* show/hide log */
  pWidget = pWidget->prev;
  FREESURFACE(pWidget->gfx);
  pWidget->size.x = w - BLOCKM_W + adj_size(1);
  pWidget->size.y = pWidget->dst->h - h + FRAME_WH + adj_size(2) + pWidget->size.h;
  
  /* toggle minimap mode */
  pWidget = pWidget->prev;
  FREESURFACE(pWidget->gfx);
  pWidget->size.x = w - BLOCKM_W + adj_size(1);
  pWidget->size.y = pWidget->dst->h - h + FRAME_WH + adj_size(2) + pWidget->size.h * 2;
  
  #ifdef SMALL_SCREEN
  /* options */
  pWidget = pWidget->prev;
  FREESURFACE(pWidget->gfx);
  pWidget->size.x = w - BLOCKM_W;
  pWidget->size.y = pWidget->dst->h - FRAME_WH - pWidget->size.h - adj_size(2);
  #endif
  
  /* hide/show mini map */
  pWidget = pWidget->prev;
  FREESURFACE(pWidget->gfx);
  pWidget->size.x = w - BLOCKM_W + pWidget->size.w + adj_size(1);
  pWidget->size.y = pWidget->dst->h - FRAME_WH - pWidget->size.h - adj_size(2);
  
  MINI_MAP_W = w;
  MINI_MAP_H = h;
}

static void Remake_UnitInfo(int w, int h)
{
  SDL_Color bg_color = {255, 255, 255, 128};
  
  SDL_Surface *pSurf;
  SDL_Rect area = {FRAME_WH + BLOCKU_W, FRAME_WH , 0, 0};
  SDL_Rect dest;
  struct widget *pWidget = pUnits_Info_Window;

  if(w < DEFAULT_UNITS_W - BLOCKU_W - DOUBLE_FRAME_WH) {
    w = DEFAULT_UNITS_W;
  } else {
    w += BLOCKU_W + DOUBLE_FRAME_WH;
  }
  
  if(h < DEFAULT_UNITS_H - DOUBLE_FRAME_WH) {
    h = DEFAULT_UNITS_H;
  } else {
    h += DOUBLE_FRAME_WH;
  }
  
  /* clear area under old map window */
  dest = pWidget->size;
  fix_rect(pWidget->dst, &dest);
  clear_surface(pWidget->dst, &dest);
  sdl_dirty_rect(pWidget->size);
    
  pWidget->size.w = w;
  pWidget->size.h = h;
  
  pWidget->size.x = Main.gui->w - w;
  pWidget->size.y = Main.gui->h - h;
  
  FREESURFACE(pWidget->theme);
  pWidget->theme = create_surf_alpha(w, h, SDL_SWSURFACE);
     
  draw_frame(pWidget->theme, 0, 0, pWidget->size.w, pWidget->size.h);
  
  pSurf = ResizeSurface(pTheme->Block, BLOCKU_W,
					pWidget->size.h - DOUBLE_FRAME_WH, 1);
  
  blit_entire_src(pSurf, pWidget->theme, FRAME_WH, FRAME_WH);
  FREESURFACE(pSurf);
  
  area.w = w - BLOCKU_W - DOUBLE_FRAME_WH;
  area.h = h - DOUBLE_FRAME_WH;
  SDL_FillRect(pWidget->theme, &area, map_rgba(pWidget->theme->format, bg_color));
  
  /* economy button */
  pWidget = pTax_Button;
  FREESURFACE(pWidget->gfx);
  pWidget->size.x = pWidget->dst->w - w + FRAME_WH
                                             + (BLOCKU_W - pWidget->size.w)/2;  
  pWidget->size.y = pWidget->dst->h - h + FRAME_WH + 2;
  
  /* research button */
  pWidget = pWidget->prev;
  FREESURFACE(pWidget->gfx);
  pWidget->size.x = pWidget->dst->w - w + FRAME_WH
                                             + (BLOCKU_W - pWidget->size.w)/2;  
  pWidget->size.y = pWidget->dst->h - h + FRAME_WH + pWidget->size.h + 2;
  
  /* revolution button */
  pWidget = pWidget->prev;
  FREESURFACE(pWidget->gfx);
  pWidget->size.x = pWidget->dst->w - w + FRAME_WH
                                             + (BLOCKU_W - pWidget->size.w)/2;
  pWidget->size.y = pWidget->dst->h - h + FRAME_WH + pWidget->size.h * 2 + 2;
  
  /* show/hide unit's window button */
  pWidget = pWidget->prev;
  FREESURFACE(pWidget->gfx);
  pWidget->size.x = pWidget->dst->w - w + FRAME_WH
                                             + (BLOCKU_W - pWidget->size.w)/2;  
  pWidget->size.y = pWidget->dst->h - FRAME_WH - pWidget->size.h - 2;
  
  UNITS_W = w;
  UNITS_H = h;
  
}

/**************************************************************************
  Init MiniMap window and Unit's Info Window.
**************************************************************************/
void Init_MapView(void)
{
  struct widget *pWidget, *pWindow;

#if 0  
  SDL_Rect unit_info_area = {FRAME_WH + BLOCKU_W, FRAME_WH ,
		             UNITS_W - BLOCKU_W - DOUBLE_FRAME_WH,
    		    UNITS_H - DOUBLE_FRAME_WH};
#endif
                    
  SDL_Surface *pIcon_theme = NULL;
		    
  /* =================== Units Window ======================= */
  pUnitInfo_Dlg = fc_calloc(1, sizeof(struct ADVANCED_DLG));

  /* pUnits_Info_Window */
  pWindow = create_window(Main.gui, create_string16(NULL, 0, 12),
    			UNITS_W, UNITS_H, WF_DRAW_THEME_TRANSPARENT);

  pWindow->size.x = Main.screen->w - UNITS_W;
  pWindow->size.y = Main.screen->h - UNITS_H;
  set_window_pos(pWindow, pWindow->size.x, pWindow->size.y);
  
  pWindow->theme = create_surf_alpha(UNITS_W, UNITS_H, SDL_SWSURFACE);
     
  draw_frame(pWindow->theme, 0, 0, pWindow->size.w, pWindow->size.h);
  
  pIcon_theme = ResizeSurface(pTheme->Block, /*BLOCKU_W*/pWindow->size.w,
 	   		     pWindow->size.h - DOUBLE_FRAME_WH, 1);
  
  blit_entire_src(pIcon_theme, pWindow->theme, FRAME_WH, FRAME_WH);
  FREESURFACE(pIcon_theme);
 
#if 0  
  SDL_FillRect(pWindow->theme, &unit_info_area,
          SDL_MapRGBA(pWindow->theme->format, 255, 255, 255, 128));
#endif
  
  pWindow->string16->style |= (SF_CENTER);
  pWindow->string16->bgcol = (SDL_Color) {0, 0, 0, 0};
  
  pWindow->action = unit_info_window_callback;
  set_wstate(pWindow, FC_WS_DISABLED);
  add_to_gui_list(ID_UNITS_WINDOW, pWindow);

  pUnits_Info_Window = pWindow;
  
  pUnitInfo_Dlg->pEndWidgetList = pUnits_Info_Window;
  pUnits_Info_Window->private_data.adv_dlg = pUnitInfo_Dlg;

  /* economy button */
  pWidget = create_icon2(NULL, pUnits_Info_Window->dst, WF_FREE_GFX
                      | WF_WIDGET_HAS_INFO_LABEL | WF_DRAW_THEME_TRANSPARENT);

  pWidget->string16 = create_str16_from_char(_("Economy (F5)"), adj_font(12));
  
  #ifdef SMALL_SCREEN
  pWidget->size.w = 8;
  pWidget->size.h = 10;
  #else
  pWidget->size.w = 18;
  pWidget->size.h = 24;
  #endif

  pWidget->size.x = Main.screen->w - UNITS_W + FRAME_WH
                                            + (BLOCKU_W - pWidget->size.w)/2;
  pWidget->size.y = Main.screen->h - UNITS_H + FRAME_WH + 2;

  pWidget->action = economy_callback;
  pWidget->key = SDLK_F5;
  set_wstate(pWidget, FC_WS_DISABLED);  
  add_to_gui_list(ID_ECONOMY, pWidget);
  
  pTax_Button = pWidget; 

  /* research button */
  pWidget = create_icon2(NULL, pUnits_Info_Window->dst, WF_FREE_GFX
		       | WF_WIDGET_HAS_INFO_LABEL | WF_DRAW_THEME_TRANSPARENT);
  pWidget->string16 = create_str16_from_char(_("Research (F6)"), adj_font(12));

  #ifdef SMALL_SCREEN
  pWidget->size.w = 8;
  pWidget->size.h = 10;
  #else
  pWidget->size.w = 18;
  pWidget->size.h = 24;
  #endif

  pWidget->size.x = Main.screen->w - UNITS_W + FRAME_WH
                                             + (BLOCKU_W - pWidget->size.w)/2;
  pWidget->size.y = Main.screen->h - UNITS_H + FRAME_WH + pWidget->size.h + 2;

  pWidget->action = research_callback;
  pWidget->key = SDLK_F6;
  set_wstate(pWidget, FC_WS_DISABLED);
  add_to_gui_list(ID_RESEARCH, pWidget);

  pResearch_Button = pWidget;

  /* revolution button */
  pWidget = create_icon2(NULL, pUnits_Info_Window->dst, (WF_FREE_GFX
			| WF_WIDGET_HAS_INFO_LABEL| WF_DRAW_THEME_TRANSPARENT));
  pWidget->string16 = create_str16_from_char(_("Revolution (Shift + R)"), adj_font(12));

  #ifdef SMALL_SCREEN
  pWidget->size.w = 8;
  pWidget->size.h = 10;
  #else
  pWidget->size.w = 18;
  pWidget->size.h = 24;
  #endif

  pWidget->size.x = Main.screen->w - UNITS_W + FRAME_WH
                                             + (BLOCKU_W - pWidget->size.w)/2;   
  pWidget->size.y =
      Main.screen->h - UNITS_H + FRAME_WH + (pWidget->size.h << 1) + 2;

  pWidget->action = revolution_callback;
  pWidget->key = SDLK_r;
  pWidget->mod = KMOD_SHIFT;
  set_wstate(pWidget, FC_WS_DISABLED);
  add_to_gui_list(ID_REVOLUTION, pWidget);

  pRevolution_Button = pWidget;
  
  /* show/hide unit's window button */

  /* make UNITS Icon */
  pIcon_theme = create_surf_alpha(pTheme->MAP_Icon->w,
			    pTheme->MAP_Icon->h, SDL_SWSURFACE);
  alphablit(pTheme->MAP_Icon, NULL, pIcon_theme, NULL);
  alphablit(pTheme->R_ARROW_Icon, NULL, pIcon_theme, NULL);

  pWidget = create_themeicon(pIcon_theme, pUnits_Info_Window->dst,
			  WF_FREE_GFX | WF_FREE_THEME |
		WF_DRAW_THEME_TRANSPARENT | WF_WIDGET_HAS_INFO_LABEL);

  pWidget->string16 = create_str16_from_char(_("Hide Unit Info Window"), adj_font(12));
  pWidget->size.x = Main.screen->w - UNITS_W + FRAME_WH
                                             + (BLOCKU_W - pWidget->size.w)/2;  
  pWidget->size.y = Main.screen->h - FRAME_WH - pWidget->size.h - 2;

  pWidget->action = toggle_unit_info_window_callback;
  set_wstate(pWidget, FC_WS_DISABLED);  
  add_to_gui_list(ID_TOGGLE_UNITS_WINDOW_BUTTON, pWidget);
  
  pUnitInfo_Dlg->pBeginWidgetList = pWidget;
     
  /* ========================= Mini map ========================== */

  /* pMiniMap_Window */
  pWindow = create_window(Main.gui, NULL, MINI_MAP_W, MINI_MAP_H, 0);
  pWindow->size.x = 0;
  pWindow->size.y = pWindow->dst->h - MINI_MAP_H;
  
  set_window_pos(pWindow, pWindow->size.x, pWindow->size.y);
  
  pWindow->theme = create_surf_alpha(MINI_MAP_W, MINI_MAP_H, SDL_SWSURFACE);
  
  draw_frame(pWindow->theme, 0, 0, pWindow->size.w, pWindow->size.h);
  
  pIcon_theme = ResizeSurface(pTheme->Block, BLOCKM_W,
					pWindow->size.h - DOUBLE_FRAME_WH, 1);
  blit_entire_src(pIcon_theme, pWindow->theme,
			pWindow->size.w - FRAME_WH - pIcon_theme->w, FRAME_WH);
  FREESURFACE(pIcon_theme);  
  
  pWindow->action = minimap_window_callback;
  set_wstate(pWindow, FC_WS_DISABLED);
  add_to_gui_list(ID_MINI_MAP_WINDOW, pWindow);
  
  pMiniMap_Window = pWindow;

  /* new turn button */
  pWidget = create_themeicon(pTheme->NEW_TURN_Icon, pMiniMap_Window->dst,
			  WF_WIDGET_HAS_INFO_LABEL);

  pWidget->string16 = create_str16_from_char(_("End Turn (Enter)"), adj_font(12));

  pWidget->size.x = MINI_MAP_W - BLOCKM_W + pWidget->size.w;
  pWidget->size.y = pWidget->dst->h - MINI_MAP_H + FRAME_WH + 2;
  
  pWidget->action = end_turn_callback;
  pWidget->key = SDLK_RETURN;
  pWidget->mod = KMOD_LCTRL;

  set_wstate(pWidget, FC_WS_DISABLED);
  add_to_gui_list(ID_NEW_TURN, pWidget);

  pNew_Turn_Button = pWidget;

  /* players button */
  pWidget = create_themeicon(pTheme->PLAYERS_Icon, pMiniMap_Window->dst,
						  WF_WIDGET_HAS_INFO_LABEL);
  pWidget->string16 = create_str16_from_char(_("Players (F3)"), adj_font(12));
  pWidget->size.x = MINI_MAP_W - BLOCKM_W + pWidget->size.w;
  pWidget->size.y = pWidget->dst->h - MINI_MAP_H + FRAME_WH + pWidget->size.h + 2;

  pWidget->action = players_action_callback;
  pWidget->key = SDLK_F3;
  set_wstate(pWidget, FC_WS_DISABLED);  

  add_to_gui_list(ID_PLAYERS, pWidget);

  /* find city button */
  pWidget = create_themeicon(pTheme->FindCity_Icon, pMiniMap_Window->dst,
						  WF_WIDGET_HAS_INFO_LABEL);
  pWidget->string16 = create_str16_from_char(
  		_("Cities Report (F1)\nor\nFind City (Shift + F)"), adj_font(12));
  pWidget->string16->style |= SF_CENTER;
  pWidget->size.x = MINI_MAP_W - BLOCKM_W + pWidget->size.w;
  pWidget->size.y = pWidget->dst->h - MINI_MAP_H + FRAME_WH + pWidget->size.h * 2 + 2;

  pWidget->action = cities_action_callback;
  pWidget->key = SDLK_f;
  pWidget->mod = KMOD_SHIFT;
  set_wstate(pWidget, FC_WS_DISABLED);

  add_to_gui_list(ID_CITIES, pWidget);
  
  pFind_City_Button = pWidget;

  /* units button */
  pWidget = create_themeicon(pTheme->UNITS2_Icon, pMiniMap_Window->dst,
						  WF_WIDGET_HAS_INFO_LABEL);
  pWidget->string16 = create_str16_from_char(_("Units (F2)"), adj_font(12));
  pWidget->size.x = MINI_MAP_W - BLOCKM_W;
  pWidget->size.y = pWidget->dst->h - MINI_MAP_H + FRAME_WH + 2;
  
  pWidget->action = units_action_callback;
  pWidget->key = SDLK_F2;
  set_wstate(pWidget, FC_WS_DISABLED);  
  
  add_to_gui_list(ID_UNITS, pWidget);

  /* show/hide log window button */
  pWidget = create_themeicon(pTheme->LOG_Icon, pMiniMap_Window->dst,
						  WF_WIDGET_HAS_INFO_LABEL);
  pWidget->string16 = create_str16_from_char(_("Hide Log (F10)"), adj_font(12));
  pWidget->size.x = MINI_MAP_W - BLOCKM_W;
  pWidget->size.y = pWidget->dst->h - MINI_MAP_H + FRAME_WH + pWidget->size.h + 2;
  pWidget->action = togle_msg_window;
  pWidget->key = SDLK_F10;
  set_wstate(pWidget, FC_WS_DISABLED);  

  add_to_gui_list(ID_CHATLINE_TOGGLE_LOG_WINDOW_BUTTON, pWidget);

  /* toggle minimap mode button */
  pWidget = create_themeicon(pTheme->BORDERS_Icon, pMiniMap_Window->dst,
						  WF_WIDGET_HAS_INFO_LABEL);
  pWidget->string16 = create_str16_from_char(
                         _("Toggle Minimap Mode (Shift + \\)"), adj_font(12));
  pWidget->size.x = MINI_MAP_W - BLOCKM_W;
  pWidget->size.y = pWidget->dst->h - MINI_MAP_H + FRAME_WH + pWidget->size.h * 2 + 2;
  pWidget->action = togle_minimap_mode;
  pWidget->key = SDLK_BACKSLASH;
  pWidget->mod = KMOD_SHIFT;
  set_wstate(pWidget, FC_WS_DISABLED);  
  
  add_to_gui_list(ID_TOGGLE_MINIMAP_MODE, pWidget);

  #ifdef SMALL_SCREEN
  /* options button */
  pOptions_Button = create_themeicon(pTheme->Options_Icon, pMiniMap_Window->dst,
				       (WF_WIDGET_HAS_INFO_LABEL));
  pOptions_Button->string16 = create_str16_from_char(_("Options"), adj_font(12));
  pOptions_Button->size.x = MINI_MAP_W - BLOCKM_W;
  pOptions_Button->size.y = pWidget->dst->h - FRAME_WH - pWidget->size.h - 2;
  pOptions_Button->action = optiondlg_callback;  
  pOptions_Button->key = SDLK_TAB;
  set_wstate(pOptions_Button, FC_WS_DISABLED);
  add_to_gui_list(ID_CLIENT_OPTIONS, pOptions_Button);
  #endif

  /* show/hide minimap button */

  /* make Map Icon */
  pIcon_theme =
      create_surf_alpha(pTheme->MAP_Icon->w, pTheme->MAP_Icon->h, SDL_SWSURFACE);
  alphablit(pTheme->MAP_Icon, NULL, pIcon_theme, NULL);
  alphablit(pTheme->L_ARROW_Icon, NULL, pIcon_theme, NULL);

  pWidget = create_themeicon(pIcon_theme, pMiniMap_Window->dst,
			  WF_FREE_GFX | WF_FREE_THEME |
		          WF_WIDGET_HAS_INFO_LABEL);

  pWidget->string16 = create_str16_from_char(_("Hide MiniMap"), adj_font(12));
  pWidget->size.x = MINI_MAP_W - BLOCKM_W + pWidget->size.w;
  pWidget->size.y = pWidget->dst->h - FRAME_WH - pWidget->size.h - 2;

  pWidget->action = toggle_map_window_callback;
  set_wstate(pWidget, FC_WS_DISABLED);  

  add_to_gui_list(ID_TOGGLE_MAP_WINDOW_BUTTON, pWidget);

  /* ========================= Cooling/Warming ========================== */

  /* cooling icon */
  pIcon_theme = adj_surf(GET_SURF(client_cooling_sprite()));
  assert(pIcon_theme != NULL);
  pWidget = create_iconlabel(pIcon_theme, Main.gui, NULL, 0);

  pWidget->size.x = pWidget->dst->w - pWidget->size.w - adj_size(10);
  pWidget->size.y = adj_size(10);

  add_to_gui_list(ID_COOLING_ICON, pWidget);

  /* warming icon */
  pIcon_theme = adj_surf(GET_SURF(client_warming_sprite()));
  assert(pIcon_theme != NULL);

  pWidget = create_iconlabel(pIcon_theme, Main.gui, NULL, 0);

  pWidget->size.x = pWidget->dst->w - pWidget->size.w * 2 - adj_size(10);
  pWidget->size.y = adj_size(10);

  add_to_gui_list(ID_WARMING_ICON, pWidget);
  
  /* ================================ */

  SDL_Client_Flags |= (CF_MAP_UNIT_W_CREATED | CF_UNIT_INFO_SHOW |
							  CF_MINI_MAP_SHOW);
}

void reset_main_widget_dest_buffer(void)
{
  		    
  /* =================== Units Window ======================= */
  struct widget *pBuf = pUnits_Info_Window;
    
  while (pBuf) {
    pBuf->dst = Main.gui;
    if (pBuf == pUnits_Info_Window->private_data.adv_dlg->pBeginWidgetList) {
      break;
    }
    pBuf = pBuf->prev;
  }
  
  /* ========================= Mini map ========================== */

  pBuf = pMiniMap_Window;
  pBuf->dst = Main.gui;

  /* new turn button */
  pBuf = pBuf->prev;
  pBuf->dst = Main.gui;
  
  /* players button */
  pBuf = pBuf->prev;
  pBuf->dst = Main.gui;
  
  /* find city button */
  pBuf = pBuf->prev;
  pBuf->dst = Main.gui;
  
  /* units button */
  pBuf = pBuf->prev;
  pBuf->dst = Main.gui;

  /* show/hide log window button */
  pBuf = pBuf->prev;
  pBuf->dst = Main.gui;

  /* toggle minimap mode button */
  pBuf = pBuf->prev;
  pBuf->dst = Main.gui;

  #ifdef SMALL_SCREEN
  /* options button */
  pBuf = pBuf->prev;
  pBuf->dst = Main.gui;
  #endif

  /* show/hide minimap button */
  pBuf = pBuf->prev;
  pBuf->dst = Main.gui;

  /* ========================= Cooling/Warming ========================== */

  /* cooling icon */
  pBuf = pBuf->prev;
  pBuf->dst = Main.gui;

  /* warming icon */
  pBuf = pBuf->prev;
  pBuf->dst = Main.gui;
  
}

void disable_main_widgets(void)
{
  if (get_client_state() == CLIENT_GAME_RUNNING_STATE) {
    struct widget *pEnd = pUnits_Info_Window->private_data.adv_dlg->pEndWidgetList;
    struct widget *pBuf = pUnits_Info_Window->private_data.adv_dlg->pBeginWidgetList;
        
    /* =================== Units Window ======================= */
    set_group_state(pBuf, pEnd, FC_WS_DISABLED);    
    pEnd = pEnd->prev;
    redraw_group(pBuf, pEnd, TRUE);
    /* ========================= Mini map ========================== */

    pBuf = pMiniMap_Window;
    set_wstate(pBuf, FC_WS_DISABLED);

    /* new turn button */
    pBuf = pBuf->prev;
    pEnd = pBuf;
    set_wstate(pBuf, FC_WS_DISABLED);
  
    /* players button */
    pBuf = pBuf->prev;
    set_wstate(pBuf, FC_WS_DISABLED);
  
    /* find city button */
    pBuf = pBuf->prev;
    set_wstate(pBuf, FC_WS_DISABLED);
  
    /* units button */
    pBuf = pBuf->prev;
    set_wstate(pBuf, FC_WS_DISABLED);

    /* show/hide log window button */
    pBuf = pBuf->prev;
    set_wstate(pBuf, FC_WS_DISABLED);

    /* toggle minimap mode button */
    pBuf = pBuf->prev;
    set_wstate(pBuf, FC_WS_DISABLED);

    #ifdef SMALL_SCREEN
    /* options button */
    pBuf = pBuf->prev;
    set_wstate(pBuf, FC_WS_DISABLED);
    #endif

    /* show/hide minimap button */
    pBuf = pBuf->prev;
    set_wstate(pBuf, FC_WS_DISABLED);
  
    redraw_group(pBuf, pEnd, TRUE);
    disable_order_buttons();
  }
}

void enable_main_widgets(void)
{
  if (get_client_state() == CLIENT_GAME_RUNNING_STATE) {
    struct widget *pEnd = pUnits_Info_Window->private_data.adv_dlg->pEndWidgetList;
    struct widget *pBuf = pUnits_Info_Window->private_data.adv_dlg->pBeginWidgetList;
        
    /* =================== Units Window ======================= */
    set_group_state(pBuf, pEnd, FC_WS_NORMAL);
    pEnd = pEnd->prev;
    redraw_group(pBuf, pEnd, TRUE);
    /* ========================= Mini map ========================== */

    pBuf = pMiniMap_Window;
    set_wstate(pBuf, FC_WS_NORMAL);

    /* new turn button */
    pBuf = pBuf->prev;
    pEnd = pBuf;
    set_wstate(pBuf, FC_WS_NORMAL);
  
    /* players button */
    pBuf = pBuf->prev;
    set_wstate(pBuf, FC_WS_NORMAL);
  
    /* find city button */
    pBuf = pBuf->prev;
    set_wstate(pBuf, FC_WS_NORMAL);
  
    /* units button */
    pBuf = pBuf->prev;
    set_wstate(pBuf, FC_WS_NORMAL);

    /* show/hide log window button */
    pBuf = pBuf->prev;
    set_wstate(pBuf, FC_WS_NORMAL);

    /* toggle minimap mode button */
    pBuf = pBuf->prev;
    set_wstate(pBuf, FC_WS_NORMAL);

    #ifdef SMALL_SCREEN
    /* options button */
    pBuf = pBuf->prev;
    set_wstate(pBuf, FC_WS_NORMAL);
    #endif

    /* show/hide minimap button */
    pBuf = pBuf->prev;
    set_wstate(pBuf, FC_WS_NORMAL);
  
    redraw_group(pBuf, pEnd, TRUE);
    
    enable_order_buttons();
    
  }
}

struct widget * get_unit_info_window_widget(void)
{
  return pUnits_Info_Window;
}

struct widget * get_minimap_window_widget(void)
{
  return pMiniMap_Window;
}

struct widget * get_tax_rates_widget(void)
{
  return pTax_Button;
}

struct widget * get_research_widget(void)
{
  return pResearch_Button;
}

struct widget * get_revolution_widget(void)
{
  return pRevolution_Button;
}

void enable_and_redraw_find_city_button(void)
{
  set_wstate(pFind_City_Button, FC_WS_NORMAL);
  redraw_icon(pFind_City_Button);
  sdl_dirty_rect(pFind_City_Button->size);
}

void enable_and_redraw_revolution_button(void)
{
  set_wstate(pRevolution_Button, FC_WS_NORMAL);
  redraw_icon2(pRevolution_Button);
  sdl_dirty_rect(pRevolution_Button->size);
}

/**************************************************************************
  mouse click handler
**************************************************************************/
void button_down_on_map(struct mouse_button_behavior *button_behavior)
{
  struct tile *ptile;
  
  if (get_client_state() != CLIENT_GAME_RUNNING_STATE) {
    return;
  }
  
  if (button_behavior->event->button == SDL_BUTTON_LEFT) {
    switch(button_behavior->hold_state) {
      case MB_HOLD_SHORT:
        break;
      case MB_HOLD_MEDIUM:
        /* switch to goto mode */
        key_unit_goto();
        update_mouse_cursor(CURSOR_GOTO);
        break;
      case MB_HOLD_LONG:
#ifdef UNDER_CE
        /* cancel goto mode and open context menu on Pocket PC since we have
         * only one 'mouse button' */
        key_cancel_action();
        draw_goto_patrol_lines = FALSE;
        update_mouse_cursor(CURSOR_DEFAULT);      
        /* popup context menu */
        if ((ptile = canvas_pos_to_tile((int) button_behavior->event->x,
                                        (int) button_behavior->event->y))) {
          popup_advanced_terrain_dialog(ptile, button_behavior->event->x, 
                                               button_behavior->event->y);
        }
#endif
        break;
      default:
        break;
    }
  } else if (button_behavior->event->button == SDL_BUTTON_MIDDLE) {
    switch(button_behavior->hold_state) {
      case MB_HOLD_SHORT:
        break;      
      case MB_HOLD_MEDIUM:
        break;
      case MB_HOLD_LONG:
        break;
      default:
        break;
    }
  } else if (button_behavior->event->button == SDL_BUTTON_RIGHT) {
    switch (button_behavior->hold_state) {
      case MB_HOLD_SHORT:
        break;
      case MB_HOLD_MEDIUM:      
        /* popup context menu */
        if ((ptile = canvas_pos_to_tile((int) button_behavior->event->x,
                                        (int) button_behavior->event->y))) {
          popup_advanced_terrain_dialog(ptile, button_behavior->event->x,
                                               button_behavior->event->y);
        }
        break;
      case MB_HOLD_LONG:
        break;
      default:
        break;
    }
  }
    
}

void button_up_on_map(struct mouse_button_behavior *button_behavior)
{
  struct tile *ptile;
  struct city *pCity;
    
  if (get_client_state() != CLIENT_GAME_RUNNING_STATE) {
    return;
  }
  
  draw_goto_patrol_lines = FALSE;
  
  if (button_behavior->event->button == SDL_BUTTON_LEFT) {
    switch(button_behavior->hold_state) {
      case MB_HOLD_SHORT:
        if(LSHIFT || LALT || LCTRL) {
          if ((ptile = canvas_pos_to_tile((int) button_behavior->event->x,
                                          (int) button_behavior->event->y))) {
            if(LSHIFT) {
              popup_advanced_terrain_dialog(ptile, button_behavior->event->x,
                                                   button_behavior->event->y);
            } else {
              if(((pCity = ptile->city) != NULL) &&
                (pCity->owner == game.player_ptr)) {
                if(LCTRL) {
                  popup_worklist_editor(pCity, &(pCity->worklist));
                } else {
                  /* LALT - this work only with fullscreen mode */
                  popup_hurry_production_dialog(pCity, NULL);
                }
              }
            }		      
          }
        } else {
          update_mouse_cursor(CURSOR_DEFAULT);
          action_button_pressed(button_behavior->event->x,
                                     button_behavior->event->y, SELECT_POPUP);
        }
        break;
      case MB_HOLD_MEDIUM:
        /* finish goto */
        update_mouse_cursor(CURSOR_DEFAULT);
        action_button_pressed(button_behavior->event->x,
                                     button_behavior->event->y, SELECT_POPUP);
        break;
      case MB_HOLD_LONG:
#ifndef UNDER_CE
        /* finish goto */
        update_mouse_cursor(CURSOR_DEFAULT);
        action_button_pressed(button_behavior->event->x,
                                     button_behavior->event->y, SELECT_POPUP);
#endif
        break;
      default:
        break;
    }
  } else if (button_behavior->event->button == SDL_BUTTON_MIDDLE) {
    switch(button_behavior->hold_state) {
      case MB_HOLD_SHORT:
/*        break;*/
      case MB_HOLD_MEDIUM:
/*        break;*/
      case MB_HOLD_LONG:
/*        break;*/
      default:
        /* popup context menu */
        if ((ptile = canvas_pos_to_tile((int) button_behavior->event->x,
                                        (int) button_behavior->event->y))) {
          popup_advanced_terrain_dialog(ptile, button_behavior->event->x,
                                               button_behavior->event->y);
        }
        break;
    }
  } else if (button_behavior->event->button == SDL_BUTTON_RIGHT) {
    switch (button_behavior->hold_state) {
      case MB_HOLD_SHORT:
        /* recenter map */
        recenter_button_pressed(button_behavior->event->x, button_behavior->event->y);
        flush_dirty();
        break;
      case MB_HOLD_MEDIUM:      
        break;
      case MB_HOLD_LONG:
        break;
      default:
        break;
    }
  }
  
}
  
/**************************************************************************
  Toggle map drawing stuff.
**************************************************************************/
bool map_event_handler(SDL_keysym Key)
{
  if (get_client_state() == CLIENT_GAME_RUNNING_STATE) {
    switch (Key.sym) {
      
    case SDLK_ESCAPE:
      key_cancel_action();
      draw_goto_patrol_lines = FALSE;
      update_mouse_cursor(CURSOR_DEFAULT);
    return FALSE;

    case SDLK_UP:
    case SDLK_KP8:
      if(!is_unit_move_blocked) {
	key_unit_move(DIR8_NORTH);
      }
    return FALSE;

    case SDLK_PAGEUP:
    case SDLK_KP9:
      if(!is_unit_move_blocked) {
        key_unit_move(DIR8_NORTHEAST);
      }
    return FALSE;

    case SDLK_RIGHT:
    case SDLK_KP6:
      if(!is_unit_move_blocked) {
        key_unit_move(DIR8_EAST);
      }
    return FALSE;

    case SDLK_PAGEDOWN:
    case SDLK_KP3:
      if(!is_unit_move_blocked) {
        key_unit_move(DIR8_SOUTHEAST);
      }
    return FALSE;

    case SDLK_DOWN:
    case SDLK_KP2:
      if(!is_unit_move_blocked) {
        key_unit_move(DIR8_SOUTH);
      }
    return FALSE;

    case SDLK_END:
    case SDLK_KP1:
      if(!is_unit_move_blocked) {
        key_unit_move(DIR8_SOUTHWEST);
      }
    return FALSE;

    case SDLK_LEFT:
    case SDLK_KP4:
      if(!is_unit_move_blocked) {
        key_unit_move(DIR8_WEST);
      }
    return FALSE;

    case SDLK_HOME:
    case SDLK_KP7:
      if(!is_unit_move_blocked) {
        key_unit_move(DIR8_NORTHWEST);
      }
    return FALSE;

    case SDLK_KP5:
      key_recall_previous_focus_unit();
    return FALSE;
      
    case SDLK_g:
      if(LCTRL || RCTRL) {
        request_toggle_map_grid();
      }
      return FALSE;

    case SDLK_b:
      if(LCTRL || RCTRL) {
        request_toggle_map_borders();
      }
      return FALSE;

    case SDLK_n:
      if ((LCTRL || RCTRL) && can_client_change_view()) {
        draw_city_names ^= 1;
        if(draw_city_names||draw_city_productions) {
          update_city_descriptions();
        }
        dirty_all();
      }
      return FALSE;

    case SDLK_p:
      if ((LCTRL || RCTRL) && can_client_change_view()) {
        draw_city_productions ^= 1;
        if(draw_city_names||draw_city_productions) {
          update_city_descriptions();
        }
        dirty_all();
      }
      return FALSE;

    case SDLK_t:
      if (LCTRL || RCTRL) {
        request_toggle_terrain();
      }
      return FALSE;

    case SDLK_r:
      if (LCTRL || RCTRL) {
        request_toggle_roads_rails();
      }
      return FALSE;

    case SDLK_i:
      if (LCTRL || RCTRL) {
        request_toggle_irrigation();
      }
      return FALSE;

    case SDLK_m:
      if (LCTRL || RCTRL) {
        request_toggle_mines();
      }
      return FALSE;

    case SDLK_f:
      if (LCTRL || RCTRL) {
        request_toggle_fortress_airbase();
      }
      return FALSE;

    case SDLK_s:
      if (LCTRL || RCTRL) {
        request_toggle_specials();
      }
      return FALSE;

    case SDLK_o:
      if (LCTRL || RCTRL) {
        request_toggle_pollution();
      }
      return FALSE;

    case SDLK_c:
      if (LCTRL || RCTRL) {
        request_toggle_cities();
      } else {
	 request_center_focus_unit();
      }
      return FALSE;

    case SDLK_u:
      if (LCTRL || RCTRL) {
        request_toggle_units();
      }
      return FALSE;

    case SDLK_w:
      if (LCTRL || RCTRL) {
        request_toggle_fog_of_war();
      }
      return FALSE;

    case SDLK_BACKSLASH:
      if (LSHIFT || RSHIFT) {
        togle_minimap_mode(NULL);
      }
      return FALSE;
            
    default:
      break;
    }
  }

  return TRUE;
}

/**************************************************************************
  ...
**************************************************************************/
static int newcity_ok_edit_callback(struct widget *pEdit) {
  char *input =
	  convert_to_chars(pNewCity_Dlg->pBeginWidgetList->string16->text);

  if (input) {
    FC_FREE(input);
  } else {
    /* empty input -> restore previous content */
    copy_chars_to_string16(pEdit->string16, pSuggestedCityName);
    redraw_edit(pEdit);
    sdl_dirty_rect(pEdit->size);
    flush_dirty();
  }
  
  return -1;
}
/**************************************************************************
  ...
**************************************************************************/
static int newcity_ok_callback(struct widget *pOk_Button)
{
  char *input =
	  convert_to_chars(pNewCity_Dlg->pBeginWidgetList->string16->text);
  
  dsend_packet_unit_build_city(&aconnection, pOk_Button->data.unit->id,
		               input);
  FC_FREE(input);

  popdown_window_group_dialog(pNewCity_Dlg->pBeginWidgetList,
			      pNewCity_Dlg->pEndWidgetList);
  FC_FREE(pNewCity_Dlg);
  
  FC_FREE(pSuggestedCityName);
  
  flush_dirty();
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int newcity_cancel_callback(struct widget *pCancel_Button)
{
  popdown_window_group_dialog(pNewCity_Dlg->pBeginWidgetList,
			      pNewCity_Dlg->pEndWidgetList);
  FC_FREE(pNewCity_Dlg);
  
  FC_FREE(pSuggestedCityName);  
  
  flush_dirty();
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int move_new_city_dlg_callback(struct widget *pWindow)
{
  return std_move_window_group_callback(pNewCity_Dlg->pBeginWidgetList, pWindow);
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
  struct widget *pLabel = NULL;
  struct widget *pWindow = NULL;
  struct widget *pCancel_Button = NULL;
  struct widget *pOK_Button;
  struct widget *pEdit;

  if(pNewCity_Dlg) {
    return;
  }

  pSuggestedCityName = fc_calloc(1, strlen(pSuggestname) + 1);
  mystrlcpy(pSuggestedCityName, pSuggestname, strlen(pSuggestname) + 1);
  
  pNewCity_Dlg = fc_calloc(1, sizeof(struct SMALL_DLG));
    
  /* create ok button */
  pOK_Button =
    create_themeicon_button_from_chars(pTheme->Small_OK_Icon, Main.gui,
					  _("OK"), adj_font(10), 0);
  pOK_Button->action = newcity_ok_callback;
  pOK_Button->key = SDLK_RETURN;  
  pOK_Button->data.unit = pUnit;  
  
  /* create cancel button */
  pCancel_Button =
      create_themeicon_button_from_chars(pTheme->Small_CANCEL_Icon,
  			Main.gui, _("Cancel"), adj_font(10), 0);
  pCancel_Button->action = newcity_cancel_callback;
  pCancel_Button->key = SDLK_ESCAPE;  

  /* correct sizes */
  pCancel_Button->size.w += adj_size(5);
  pOK_Button->size.w = pCancel_Button->size.w;
  
  /* create text label */
  pStr = create_str16_from_char(_("What should we call our new city?"), adj_font(10));
  pStr->style |= (TTF_STYLE_BOLD|SF_CENTER);
  pStr->fgcol = *get_game_colorRGB(COLOR_THEME_NEWCITYDLG_TEXT);
  pLabel = create_iconlabel(NULL, Main.gui, pStr, WF_DRAW_TEXT_LABEL_WITH_SPACE);
  
  pEdit = create_edit(NULL, Main.gui, create_str16_from_char(pSuggestname, adj_font(12)),
			(pOK_Button->size.w + pCancel_Button->size.w + adj_size(15)), WF_DRAW_THEME_TRANSPARENT);
  pEdit->action = newcity_ok_edit_callback;

  /* create window */
  pStr = create_str16_from_char(_("Build New City"), adj_font(12));
  pStr->style |= TTF_STYLE_BOLD;
  pWindow = create_window(Main.gui, pStr, pEdit->size.w + adj_size(20), pEdit->size.h +
			  pOK_Button->size.h + pLabel->size.h +
			  WINDOW_TILE_HIGH + adj_size(25), 0);
  pWindow->action = move_new_city_dlg_callback;

  /* I make this hack to center label on window */
  if (pLabel->size.w < pWindow->size.w)
  {
    pLabel->size.w = pWindow->size.w;
  } else { 
    pWindow->size.w = pLabel->size.w + adj_size(10);
  }
  
  pEdit->size.w = pWindow->size.w - adj_size(20);
  
  /* set start positions */
  pWindow->size.x = (Main.screen->w - pWindow->size.w) / 2;
  pWindow->size.y = (Main.screen->h - pWindow->size.h) / 2;
  set_window_pos(pWindow, pWindow->size.x, pWindow->size.y);
  
  set_window_pos(pWindow, pWindow->size.x, pWindow->size.y);

  pOK_Button->size.x = pWindow->size.x + adj_size(10);
  pOK_Button->size.y =
      pWindow->size.y + pWindow->size.h - pOK_Button->size.h - adj_size(10);


  pCancel_Button->size.y = pOK_Button->size.y;
  pCancel_Button->size.x = pWindow->size.x + pWindow->size.w -
      pCancel_Button->size.w - adj_size(10);

  pEdit->size.x = pWindow->size.x + adj_size(10);
  pEdit->size.y =
      pWindow->size.y + WINDOW_TILE_HIGH + adj_size(5) + pLabel->size.h + adj_size(3);

  pLabel->size.x = pWindow->size.x + FRAME_WH;
  pLabel->size.y = pWindow->size.y + WINDOW_TILE_HIGH + adj_size(5);

  /* create window background */
  pLogo = get_logo_gfx();
  if (resize_window
      (pWindow, pLogo, NULL, pWindow->size.w, pWindow->size.h)) {
    FREESURFACE(pLogo);
  }

  /* enable widgets */
  set_wstate(pCancel_Button, FC_WS_NORMAL);
  set_wstate(pOK_Button, FC_WS_NORMAL);
  set_wstate(pEdit, FC_WS_NORMAL);
  set_wstate(pWindow, FC_WS_NORMAL);

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

  flush_rect(pWindow->size, FALSE);
}

/**************************************************************************
  ...
**************************************************************************/
void popdown_newcity_dialog(void)
{
  if(pNewCity_Dlg) {
    popdown_window_group_dialog(pNewCity_Dlg->pBeginWidgetList,
			      pNewCity_Dlg->pEndWidgetList);
    FC_FREE(pNewCity_Dlg);
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
      set_wstate(pNew_Turn_Button, FC_WS_NORMAL);
    } else {
      set_wstate(pNew_Turn_Button, FC_WS_DISABLED);
    }
    redraw_icon(pNew_Turn_Button);
    flush_rect(pNew_Turn_Button->size, FALSE);
  }
}

/**************************************************************************
  Draw a goto or patrol line at the current mouse position.
**************************************************************************/
void create_line_at_mouse_pos(void)
{
  int pos_x, pos_y;
    
  SDL_GetMouseState(&pos_x, &pos_y);
  update_line(pos_x, pos_y);
  draw_goto_patrol_lines = TRUE;
}

/**************************************************************************
 The Area Selection rectangle. Called by center_tile_mapcanvas() and
 when the mouse pointer moves.
**************************************************************************/
void update_rect_at_mouse_pos(void)
{
  /* PORTME */
}
