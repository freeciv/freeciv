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

/* common */
#include "fc_types.h"
#include "game.h"

/* client */
#include "civclient.h"
#include "climisc.h"
#include "clinet.h"

/* gui-sdl */
#include "colors.h"
#include "connectdlg.h"
#include "dialogs.h"
#include "graphics.h"
#include "gui_iconv.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "gui_tilespec.h"
#include "mapctrl.h"
#include "mapview.h"
#include "menu.h"
#include "pages.h"
#include "themespec.h"
#include "wldlg.h"

#include "optiondlg.h"

static struct OPT_DLG {
  struct widget *pBeginOptionsWidgetList;
  struct widget *pEndOptionsWidgetList;
  struct widget *pBeginCoreOptionsWidgetList;
  struct widget *pBeginMainOptionsWidgetList;
  struct ADVANCED_DLG *pADlg;
} *pOption_Dlg = NULL;

struct widget *pOptions_Button = NULL;
static struct widget *pEdited_WorkList_Name = NULL;
extern bool do_cursor_animation;
extern bool use_color_cursors;

/**************************************************************************
  ...
**************************************************************************/
static void center_optiondlg(void)
{
  Sint16 newX, newY;
  
  newX = (Main.screen->w - pOption_Dlg->pEndOptionsWidgetList->size.w) / 2;
  newY = (Main.screen->h - pOption_Dlg->pEndOptionsWidgetList->size.h) / 2;

  set_new_group_start_pos(pOption_Dlg->pBeginOptionsWidgetList,
			  pOption_Dlg->pEndOptionsWidgetList,
			  newX - pOption_Dlg->pEndOptionsWidgetList->size.x,
			  newY - pOption_Dlg->pEndOptionsWidgetList->size.y);
}

/**************************************************************************
  ...
**************************************************************************/
static int main_optiondlg_callback(struct widget *pWindow)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    move_window_group(pOption_Dlg->pBeginOptionsWidgetList, pWindow);
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int sound_callback(struct widget *pWidget)
{
  return -1;
}

/**************************************************************************
			Global WorkLists Handler
**************************************************************************/

/**************************************************************************
  ...
**************************************************************************/
static int edit_worklist_callback(struct widget *pWidget)
{  
  switch(Main.event.button.button) {
    case SDL_BUTTON_LEFT:
      pEdited_WorkList_Name = pWidget;
      popup_worklist_editor(NULL, &client.worklists[MAX_ID - pWidget->ID]);
    break;
    case SDL_BUTTON_MIDDLE:
      /* nothing */
    break;
    case SDL_BUTTON_RIGHT:
    {
      int i = MAX_ID - pWidget->ID;
      bool scroll = (pOption_Dlg->pADlg->pActiveWidgetList != NULL);
      
      for(; i < MAX_NUM_WORKLISTS; i++) {
	if (!client.worklists[i].is_valid) {
      	  break;
	}
	if (i + 1 < MAX_NUM_WORKLISTS &&
	    client.worklists[i + 1].is_valid) {
	  copy_worklist(&client.worklists[i],
			  &client.worklists[i + 1]);
	} else {
	  client.worklists[i].is_valid = FALSE;
	  strcpy(client.worklists[i].name, "\n");
	}
      
      }
    
      del_widget_from_vertical_scroll_widget_list(pOption_Dlg->pADlg, pWidget);
      
      /* find if there was scrollbar hide */
      if(scroll && pOption_Dlg->pADlg->pActiveWidgetList == NULL) {
        int len = pOption_Dlg->pADlg->pScroll->pUp_Left_Button->size.w;
	pWidget = pOption_Dlg->pADlg->pEndActiveWidgetList->next;
        do {
          pWidget = pWidget->prev;
          pWidget->size.w += len;
          FREESURFACE(pWidget->gfx);
        } while(pWidget != pOption_Dlg->pADlg->pBeginActiveWidgetList);
      }   
      
      /* find if that was no empty list */
      for (i = 0; i < MAX_NUM_WORKLISTS; i++)
        if (!client.worklists[i].is_valid)
          break;

      /* No more worklist slots free. */
      if (i < MAX_NUM_WORKLISTS &&
	(get_wstate(pOption_Dlg->pADlg->pBeginActiveWidgetList) == FC_WS_DISABLED)) {
        set_wstate(pOption_Dlg->pADlg->pBeginActiveWidgetList, FC_WS_NORMAL);
        pOption_Dlg->pADlg->pBeginActiveWidgetList->string16->fgcol =
	                  *get_game_colorRGB(COLOR_THEME_OPTIONDLG_WORKLISTLIST_TEXT);
      }
      
      redraw_group(pOption_Dlg->pBeginOptionsWidgetList,
  				pOption_Dlg->pEndOptionsWidgetList, 0);
      sdl_dirty_rect(pOption_Dlg->pEndOptionsWidgetList->size);
      flush_dirty();
    }
    break;
    default:
    	abort();
    break;
  }
  return -1;  
}


/**************************************************************************
  ...
**************************************************************************/
static int add_new_worklist_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    struct widget *pNew_WorkList_Widget = NULL;
    struct widget *pWindow = pOption_Dlg->pEndOptionsWidgetList;
    bool scroll = pOption_Dlg->pADlg->pActiveWidgetList == NULL;
    bool redraw_all = FALSE;
    int j;
  
    set_wstate(pWidget, FC_WS_NORMAL);
    pSellected_Widget = NULL;
    
    /* Find the next free worklist for this player */
  
    for (j = 0; j < MAX_NUM_WORKLISTS; j++)
      if (!client.worklists[j].is_valid)
        break;
  
    /* No more worklist slots free.  (!!!Maybe we should tell the user?) */
    if (j == MAX_NUM_WORKLISTS) {
      return -2;
    }
    
    /* Validate this slot. */
    init_worklist(&client.worklists[j]);
    client.worklists[j].is_valid = TRUE;
    strcpy(client.worklists[j].name, _("empty worklist"));
    
    /* create list element */
    pNew_WorkList_Widget = create_iconlabel_from_chars(NULL, pWidget->dst, 
                  client.worklists[j].name, adj_font(12), WF_RESTORE_BACKGROUND);
    pNew_WorkList_Widget->ID = MAX_ID - j;
    pNew_WorkList_Widget->string16->style |= SF_CENTER;
    set_wstate(pNew_WorkList_Widget, FC_WS_NORMAL);
    pNew_WorkList_Widget->size.w = pWidget->size.w;
    pNew_WorkList_Widget->action = edit_worklist_callback;
    
    /* add to widget list */
    redraw_all = add_widget_to_vertical_scroll_widget_list(pOption_Dlg->pADlg,
                                        pNew_WorkList_Widget,
                                        pWidget, TRUE,
                                          pWindow->size.x + adj_size(20),
                        pWindow->size.y + WINDOW_TITLE_HEIGHT + 1 + adj_size(20));
  
    /* find if there was scrollbar shown */
    if(scroll && pOption_Dlg->pADlg->pActiveWidgetList != NULL) {
      int len = pOption_Dlg->pADlg->pScroll->pUp_Left_Button->size.w;
      pWindow = pOption_Dlg->pADlg->pEndActiveWidgetList->next;
      do {
        pWindow = pWindow->prev;
        pWindow->size.w -= len;
        FREESURFACE(pWindow->gfx);
      } while(pWindow != pOption_Dlg->pADlg->pBeginActiveWidgetList);
    }
    
    /* find if that was last empty list */
    for (j = 0; j < MAX_NUM_WORKLISTS; j++)
      if (!client.worklists[j].is_valid)
        break;
  
    /* No more worklist slots free. */
    if (j == MAX_NUM_WORKLISTS) {
      set_wstate(pWidget, FC_WS_DISABLED);
      pWidget->string16->fgcol = *(get_game_colorRGB(COLOR_THEME_WIDGET_DISABLED_TEXT));
    }
    
    
    if(redraw_all) {
      redraw_group(pOption_Dlg->pBeginOptionsWidgetList,
                                  pOption_Dlg->pEndOptionsWidgetList, 0);
      sdl_dirty_rect(pOption_Dlg->pEndOptionsWidgetList->size);
    } else {
      /* redraw only new widget and dock widget */
      if (!pWidget->gfx) {
        refresh_widget_background(pWidget);
      }
      redraw_widget(pWidget);
      sdl_dirty_rect(pWidget->size);
      if (!pNew_WorkList_Widget->gfx) {
        refresh_widget_background(pNew_WorkList_Widget);
      }
      redraw_widget(pNew_WorkList_Widget);
      sdl_dirty_rect(pNew_WorkList_Widget->size);
    }
    flush_dirty();
  }
  return -1;
}


/**************************************************************************
 * The Worklist Report part of Options dialog shows all the global
 * worklists that the player has defined.  There can be at most
 * MAX_NUM_WORKLISTS global worklists.
**************************************************************************/
static int work_lists_callback(struct widget *pWidget)
{
  SDL_Color bg_color = {255, 255, 255, 128};

  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    struct widget *pBuf = NULL, *pWindow = pOption_Dlg->pEndOptionsWidgetList;
    int i , count = 0, len;
    SDL_Rect area = {pWindow->size.x + adj_size(15),
                          pWindow->size.y + WINDOW_TITLE_HEIGHT + 1 + adj_size(15),
                          pWindow->size.w - adj_size(30),
                          pWindow->size.h - WINDOW_TITLE_HEIGHT - adj_size(2) - adj_size(30)};
    
    /* clear flag */
    SDL_Client_Flags &= ~CF_OPTION_MAIN;
  
    /* hide main widget group */
    hide_group(pOption_Dlg->pBeginMainOptionsWidgetList,
               pOption_Dlg->pBeginCoreOptionsWidgetList->prev);
    /* ----------------------------- */
    /* create white background */		
    pBuf = create_iconlabel(create_surf_alpha(area.w, area.h - 30, SDL_SWSURFACE),
                          pWindow->dst, NULL, WF_FREE_THEME);
    pBuf->size = area;
    SDL_FillRect(pBuf->theme, NULL, map_rgba(pBuf->theme->format, bg_color));
    putframe(pBuf->theme, 0, 0, pBuf->theme->w - 1, pBuf->theme->h - 1,
      map_rgba(pBuf->theme->format, *get_game_colorRGB(COLOR_THEME_OPTIONDLG_WORKLISTLIST_FRAME)));
    add_to_gui_list(ID_LABEL, pBuf);
    
    /* ----------------------------- */
    for (i = 0; i < MAX_NUM_WORKLISTS; i++) {
      if (client.worklists[i].is_valid) {
        pBuf = create_iconlabel_from_chars(NULL, pWindow->dst, 
                  client.worklists[i].name, adj_font(12),
                                                WF_RESTORE_BACKGROUND);
        set_wstate(pBuf, FC_WS_NORMAL);
        add_to_gui_list(MAX_ID - i, pBuf);
        pBuf->action = edit_worklist_callback;
        pBuf->string16->style |= SF_CENTER;
        count++;
      
        if(count>13) {
          set_wflag(pBuf, WF_HIDDEN);
        }
      }
    }
    
    if(count < MAX_NUM_WORKLISTS) {
      pBuf = create_iconlabel_from_chars(NULL, pWindow->dst, 
                  _("Add new worklist"), adj_font(12), WF_RESTORE_BACKGROUND);
      set_wstate(pBuf, FC_WS_NORMAL);
      add_to_gui_list(ID_ADD_NEW_WORKLIST, pBuf);
      pBuf->action = add_new_worklist_callback;
      pBuf->string16->style |= SF_CENTER;
      count++;
      
      if(count>13) {
        set_wflag(pBuf, WF_HIDDEN);
      }
    }
    /* ----------------------------- */
    
    pOption_Dlg->pADlg = fc_calloc(1, sizeof(struct ADVANCED_DLG));
    
    pOption_Dlg->pADlg->pEndWidgetList = pOption_Dlg->pEndOptionsWidgetList;   
    
    pOption_Dlg->pADlg->pEndActiveWidgetList =
                    pOption_Dlg->pBeginMainOptionsWidgetList->prev->prev;
    
    pOption_Dlg->pADlg->pBeginActiveWidgetList = pBuf;
    pOption_Dlg->pADlg->pBeginWidgetList = pBuf;
    
    
    pOption_Dlg->pADlg->pScroll = fc_calloc(1, sizeof(struct ScrollBar));
    pOption_Dlg->pADlg->pScroll->count = count;
    pOption_Dlg->pADlg->pScroll->active = 13;
    pOption_Dlg->pADlg->pScroll->step = 1;
    
    len = create_vertical_scrollbar(pOption_Dlg->pADlg,
                    1, 13, TRUE, TRUE);
    setup_vertical_scrollbar_area(pOption_Dlg->pADlg->pScroll,
          area.x + area.w - 1, area.y + 1, area.h - adj_size(32), TRUE);
    
    if(count>13) {
      pOption_Dlg->pADlg->pActiveWidgetList =
                                  pOption_Dlg->pADlg->pEndActiveWidgetList;
    } else {
      hide_scrollbar(pOption_Dlg->pADlg->pScroll);
      len = 0;
    }
    /* ----------------------------- */
    
    setup_vertical_widgets_position(1, pWindow->size.x + adj_size(20),
          pWindow->size.y + WINDOW_TITLE_HEIGHT + 1 + adj_size(20),
          area.w - adj_size(10) - len, 0,
          pOption_Dlg->pADlg->pBeginActiveWidgetList,
          pOption_Dlg->pADlg->pEndActiveWidgetList);
   
    pOption_Dlg->pBeginOptionsWidgetList = pOption_Dlg->pADlg->pBeginWidgetList;
    /* ----------------------------- */
    
    redraw_group(pOption_Dlg->pBeginOptionsWidgetList,
                                  pOption_Dlg->pEndOptionsWidgetList, 0);
    flush_rect(pWindow->size, FALSE);
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int change_mode_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {  
    char cBuf[50] = "";
    int mode;
    Uint32 tmp_flags = Main.screen->flags;
    struct widget *pWindow =
        pOption_Dlg->pBeginMainOptionsWidgetList->prev->prev->prev->prev;

    /* don't free this */
    SDL_Rect **pModes_Rect =
        SDL_ListModes(NULL, SDL_FULLSCREEN | Main.screen->flags);
  
    mode = 0;
    while (pWindow) {
  
      if (get_wstate(pWindow) == FC_WS_DISABLED) {
        if (pModes_Rect[mode]) {
          set_wstate(pWindow, FC_WS_NORMAL);
        }
        break;
      }
      mode++;
      pWindow = pWindow->prev;
    }
  
    set_wstate(pWidget, FC_WS_DISABLED);
  
    if (gui_sdl_fullscreen != (Main.screen->flags & SDL_FULLSCREEN)) {
      tmp_flags ^= SDL_FULLSCREEN;
      tmp_flags ^= SDL_RESIZABLE;
    }
  
    mode = MAX_ID - pWidget->ID;
    
    if (pModes_Rect[mode])
    {
      set_video_mode(pModes_Rect[mode]->w, pModes_Rect[mode]->h, tmp_flags);
    } else {
      set_video_mode(640, 480, tmp_flags);
    }
  
    gui_sdl_screen_width = Main.screen->w;
    gui_sdl_screen_height = Main.screen->h;

    /* change setting label */
    if (Main.screen->flags & SDL_FULLSCREEN) {
      my_snprintf(cBuf, sizeof(cBuf), _("Current Setup\nFullscreen %dx%d"),
              Main.screen->w, Main.screen->h);
    } else {
      my_snprintf(cBuf, sizeof(cBuf), _("Current Setup\n%dx%d"),
              Main.screen->w, Main.screen->h);
    }
    copy_chars_to_string16(
          pOption_Dlg->pBeginMainOptionsWidgetList->prev->string16, cBuf);
      
    pOptions_Button->dst = Main.gui;
    /* move cooling/warming icons to botton-right corrner */
  
    pWindow = get_widget_pointer_form_main_list(ID_WARMING_ICON);
    pWindow->dst = Main.gui;
    pWindow->size.x = Main.gui->w - 10 - (pWindow->size.w << 1);
  
    /* ID_COOLING_ICON */
    pWindow = pWindow->next;
    pWindow->dst = Main.gui;
    pWindow->size.x = Main.gui->w - 10 - pWindow->size.w;
  
    center_optiondlg();/* alloc new dest buffers */
    reset_main_widget_dest_buffer();
    
    /* move units window to botton-right corrner */
    set_new_units_window_pos();
    /* move minimap window to botton-left corrner */
    set_new_mini_map_window_pos();
    set_new_order_widgets_dest_buffers();
    
    if (get_client_state() == CLIENT_GAME_RUNNING_STATE) {
      map_canvas_resized(Main.screen->w, Main.screen->h); 
    }      
  
    /* Options Dlg Window */
    pWindow = pOption_Dlg->pEndOptionsWidgetList;
    set_window_pos(pWindow, pWindow->size.x, pWindow->size.y);
    
    if (get_client_state() != CLIENT_GAME_RUNNING_STATE) {
      draw_intro_gfx();
      refresh_widget_background(pWindow);
      redraw_group(pOption_Dlg->pBeginOptionsWidgetList, 
                                  pOption_Dlg->pEndOptionsWidgetList, 0);
    } else {
      
      update_info_label();
      update_unit_info_label(get_units_in_focus());
      center_on_something();/* with redrawing full map */
      update_order_widget();
      redraw_group(pOption_Dlg->pBeginOptionsWidgetList,
                              pOption_Dlg->pEndOptionsWidgetList, 0);
  
    }
    
    flush_all();
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
#if defined UNDER_CE && defined SMALL_SCREEN

/* under Windows CE with 320x240 resolution there's no need to switch resolutions,
   but to switch between window mode and full screen mode, because in full screen
   mode you can't access the software keyboard, but in window mode you have a
   disturbing title bar, so you would switch to window mode if you need the
   keyboard and go back to full screen if you've finished typing
*/

static int toggle_fullscreen_callback(struct widget *pWidget)
{ 
  if (Main.event.button.button == SDL_BUTTON_LEFT) {  
    gui_sdl_fullscreen = !gui_sdl_fullscreen;
    
    if (gui_sdl_fullscreen) {
      set_video_mode(320, 240, SDL_SWSURFACE | SDL_ANYFORMAT | SDL_FULLSCREEN);
    } else {
      set_video_mode(320, 240, SDL_SWSURFACE | SDL_ANYFORMAT);
    }
  
    flush_all();
  }
  return -1;
}
#else
static int toggle_fullscreen_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {  
    int i = 0;
    struct widget *pTmp = NULL;

    /* don't free this */
    SDL_Rect **pModes_Rect =
        SDL_ListModes(NULL, SDL_FULLSCREEN | Main.screen->flags);
  
    redraw_icon(pWidget);
    flush_rect(pWidget->size, FALSE);
  
    gui_sdl_fullscreen = !gui_sdl_fullscreen;
  
    while (pModes_Rect[i] && pModes_Rect[i]->w != Main.screen->w) {
      i++;
    }
  
    if (pModes_Rect[i])
    {
      pTmp = get_widget_pointer_form_main_list(MAX_ID - i);
  
      if (get_wstate(pTmp) == FC_WS_DISABLED) {
        set_wstate(pTmp, FC_WS_NORMAL);
      } else {
        set_wstate(pTmp, FC_WS_DISABLED);
      }
  
      redraw_ibutton(pTmp);
      
      if (!pModes_Rect[i+1] && pTmp->prev)
      {
        sdl_dirty_rect(pTmp->size);
        if (get_checkbox_state(pWidget)) {
          set_wstate(pTmp->prev, FC_WS_DISABLED);
        } else {
          set_wstate(pTmp->prev, FC_WS_NORMAL);
        }
        redraw_ibutton(pTmp->prev);
        sdl_dirty_rect(pTmp->prev->size);
        flush_dirty();
      } else {
        flush_rect(pTmp->size, FALSE);
      }
    } else {
      
      pTmp = get_widget_pointer_form_main_list(MAX_ID - i);
  
      if (get_checkbox_state(pWidget)||(Main.screen->w == 640)) {
        set_wstate(pTmp, FC_WS_DISABLED);
      } else {
        set_wstate(pTmp, FC_WS_NORMAL);
      }
  
      redraw_ibutton(pTmp);
      flush_rect(pTmp->size, FALSE);
    }
  }  
  return -1;
}
#endif
/**************************************************************************
  ...
**************************************************************************/
static int video_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {  
    int i = 0;
    char cBuf[64] = "";
    Uint16 len = 0, count = 0;
    Sint16 xxx;	/* tmp */
    SDL_String16 *pStr;
    struct widget *pTmpGui = NULL, *pWindow = pOption_Dlg->pEndOptionsWidgetList;
    
#if !defined UNDER_CE || !defined SMALL_SCREEN
    /* don't free this */
    SDL_Rect **pModes_Rect = 
                  SDL_ListModes(NULL, SDL_FULLSCREEN | Main.screen->flags);  
      
    /* Check is there are any modes available */
    if (!pModes_Rect) {
      freelog(LOG_DEBUG, _("No modes available!"));
      return 0;
    }
    
    /* Check if or resolution is restricted */
    if (pModes_Rect == (SDL_Rect **) - 1) {
      freelog(LOG_DEBUG, _("All resolutions available."));
      return 0;
      /* fix ME */
    }
#endif
    
    /* clear flag */
    SDL_Client_Flags &= ~CF_OPTION_MAIN;
  
    /* hide main widget group */
    hide_group(pOption_Dlg->pBeginMainOptionsWidgetList,
               pOption_Dlg->pBeginCoreOptionsWidgetList->prev);
  
    /* create setting label */
    if (Main.screen->flags & SDL_FULLSCREEN) {
      my_snprintf(cBuf, sizeof(cBuf),_("Current Setup\nFullscreen %dx%d"),
              Main.screen->w, Main.screen->h);
    } else {
      my_snprintf(cBuf, sizeof(cBuf),_("Current Setup\n%dx%d"), Main.screen->w,
              Main.screen->h);
    }
  
    pTmpGui = create_iconlabel(NULL, pWindow->dst,
                          create_str16_from_char(cBuf, adj_font(10)), 0);
    pTmpGui->string16->style |= (TTF_STYLE_BOLD|SF_CENTER);
    pTmpGui->string16->fgcol = *get_game_colorRGB(COLOR_THEME_CHECKBOX_LABEL_TEXT);
  
    /* set window width to 'pTmpGui' for center string */
    pTmpGui->size.w = pWindow->size.w;
  
    pTmpGui->size.x = pWindow->size.x;
    pTmpGui->size.y = pWindow->size.y + WINDOW_TITLE_HEIGHT + adj_size(6);
  
    add_to_gui_list(ID_OPTIONS_RESOLUTION_LABEL, pTmpGui);
  
    pStr = create_str16_from_char(_("Fullscreen Mode"), adj_font(10));
    pStr->style |= (TTF_STYLE_BOLD|SF_CENTER_RIGHT);
    
    /* gui_sdl_fullscreen mode label */
    pTmpGui = create_themelabel(create_filled_surface(adj_size(150), adj_size(30),
                SDL_SWSURFACE, NULL, TRUE),
                pWindow->dst, pStr, adj_size(150), adj_size(30), 0);
                          
    xxx = pTmpGui->size.x = pWindow->size.x +
        ((pWindow->size.w - pTmpGui->size.w) / 2);
    pTmpGui->size.y = pWindow->size.y + WINDOW_TITLE_HEIGHT + adj_size(40);
  
    add_to_gui_list(ID_OPTIONS_FULLSCREEN_LABEL, pTmpGui);
  
    /* gui_sdl_fullscreen check box */
    pTmpGui = create_checkbox(pWindow->dst,
                  ((Main.screen->flags & SDL_FULLSCREEN) == SDL_FULLSCREEN),
                                                  WF_RESTORE_BACKGROUND);
    
    pTmpGui->action = toggle_fullscreen_callback;
    set_wstate(pTmpGui, FC_WS_NORMAL);
  
    pTmpGui->size.x = xxx + adj_size(5);
    pTmpGui->size.y = pWindow->size.y + WINDOW_TITLE_HEIGHT + adj_size(45);
  
    add_to_gui_list(ID_OPTIONS_TOGGLE_FULLSCREEN_CHECKBOX, pTmpGui);
    /* ------------------------- */
    
#if !defined UNDER_CE || !defined SMALL_SCREEN
  
    /* create modes buttons */
    for (i = 0; pModes_Rect[i]; i++) {
      if (i && ((pModes_Rect[i]->w == pModes_Rect[i - 1]->w)
        || ((pModes_Rect[i]->w < 640 && pModes_Rect[i]->h < 480)))) {
        continue;
      }
    
      my_snprintf(cBuf, sizeof(cBuf), "%dx%d",
                                  pModes_Rect[i]->w, pModes_Rect[i]->h);
      pTmpGui = create_icon_button_from_chars(NULL, pWindow->dst, cBuf, adj_font(14), 0);
    
      if (len) {
        pTmpGui->size.w = len;
      } else {
        pTmpGui->size.w += adj_size(6);
        len = pTmpGui->size.w;
      }
  
      if (pModes_Rect[i]->w != Main.screen->w) {
        set_wstate(pTmpGui, FC_WS_NORMAL);
      }
      
      count++;
      pTmpGui->action = change_mode_callback;
  
      /* ugly hack */
      add_to_gui_list((MAX_ID - i), pTmpGui);
    } /* for */
  
    /* when only one resolution is avilable (bigger that 640x480)
       then this allow secound (640x480) window mode */
    if ((i == 1) && (pModes_Rect[0]->w > 640))
    {
      pTmpGui = create_icon_button_from_chars(NULL,
                                          pWindow->dst, "640x480", adj_font(14), 0);
      
      if (len) {
        pTmpGui->size.w = len;
      } else {
        pTmpGui->size.w += 6;
        len = pTmpGui->size.w;
      }
      
      if(!(Main.screen->flags & SDL_FULLSCREEN)&&(Main.screen->w != 640))
      {
        set_wstate(pTmpGui, FC_WS_NORMAL);
      }
      
      count++;
      pTmpGui->action = change_mode_callback;
  
      /* ugly hack */
      add_to_gui_list((MAX_ID - 1), pTmpGui);
    }
#endif
    
    /* ------------------------- */
    pOption_Dlg->pBeginOptionsWidgetList = pTmpGui;
#if !defined UNDER_CE || !defined SMALL_SCREEN
    if(count % 5) {
      count /= 5;
      count++;
    } else {
      count /= 5;
    }
    
    /* set start positions */
    pTmpGui = pOption_Dlg->pBeginMainOptionsWidgetList->prev->prev->prev->prev;
    
    pTmpGui->size.x =
        pWindow->size.x +
            (pWindow->size.w - count * (pTmpGui->size.w + adj_size(10)) - adj_size(10)) / 2;
    pTmpGui->size.y = pWindow->size.y + adj_size(110);
    
    count = 0;
    for (pTmpGui = pTmpGui->prev; pTmpGui; pTmpGui = pTmpGui->prev) {
      if(count < 4) {
        pTmpGui->size.x = pTmpGui->next->size.x;
        pTmpGui->size.y = pTmpGui->next->size.y + pTmpGui->next->size.h + adj_size(10);
        count++;
      } else {
        pTmpGui->size.x = pTmpGui->next->size.x + pTmpGui->size.w + adj_size(10);
        pTmpGui->size.y = pWindow->size.y + adj_size(110);
        count = 0;
      }
    }
#endif
    redraw_group(pOption_Dlg->pBeginOptionsWidgetList,
                            pOption_Dlg->pEndOptionsWidgetList, 0);
    flush_rect(pWindow->size, FALSE);
  }
  return -1;
}

/* ===================================================================== */

/**************************************************************************
  ...
**************************************************************************/
static int sound_bell_at_new_turn_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    redraw_icon(pWidget);
    flush_rect(pWidget->size, FALSE);
    sound_bell_at_new_turn ^= 1;
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int smooth_move_unit_msec_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {  
    char *tmp = convert_to_chars(pWidget->string16->text);
    sscanf(tmp, "%d", &smooth_move_unit_msec);
    FC_FREE(tmp);
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int do_combat_animation_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    redraw_icon(pWidget);
    flush_rect(pWidget->size, FALSE);
    do_combat_animation ^= 1;
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int do_focus_animation_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    redraw_icon(pWidget);
    flush_rect(pWidget->size, FALSE);
    do_focus_animation ^= 1;
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int do_cursor_animation_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    redraw_icon(pWidget);
    flush_rect(pWidget->size, FALSE);
    do_cursor_animation ^= 1;
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int use_color_cursors_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    redraw_icon(pWidget);
    flush_rect(pWidget->size, FALSE);
    use_color_cursors ^= 1;
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int auto_center_on_unit_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    redraw_icon(pWidget);
    flush_rect(pWidget->size, FALSE);
    auto_center_on_unit ^= 1;
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int auto_center_on_combat_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    redraw_icon(pWidget);
    flush_rect(pWidget->size, FALSE);
    auto_center_on_combat ^= 1;
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int wakeup_focus_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    redraw_icon(pWidget);
    flush_rect(pWidget->size, FALSE);
    wakeup_focus ^= 1;
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int popup_new_cities_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    redraw_icon(pWidget);
    flush_rect(pWidget->size, FALSE);
    popup_new_cities ^= 1;
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int ask_city_names_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    redraw_icon(pWidget);
    flush_rect(pWidget->size, FALSE);
    ask_city_name ^= 1;
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int auto_turn_done_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    redraw_icon(pWidget);
    flush_rect(pWidget->size, FALSE);
    auto_turn_done ^= 1;
  }
  return -1;
}

/**************************************************************************
  popup local settings.
**************************************************************************/
static int local_setting_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    SDL_Color text_color = *get_game_colorRGB(COLOR_THEME_CHECKBOX_LABEL_TEXT);
    SDL_String16 *pStr = NULL;
    struct widget *pTmpGui = NULL, *pWindow = pOption_Dlg->pEndOptionsWidgetList;
    char cBuf[3];
    
    /* clear flag */
    SDL_Client_Flags &= ~CF_OPTION_MAIN;
  
    /* hide main widget group */
    hide_group(pOption_Dlg->pBeginMainOptionsWidgetList,
               pOption_Dlg->pBeginCoreOptionsWidgetList->prev);
  
    /* 'sound befor new turn' */
    /* check box */
    pTmpGui = create_checkbox(pWindow->dst, sound_bell_at_new_turn,
                              WF_RESTORE_BACKGROUND);
  
    pTmpGui->action = sound_bell_at_new_turn_callback;
    set_wstate(pTmpGui, FC_WS_NORMAL);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(15);
    pTmpGui->size.y = pWindow->size.y + WINDOW_TITLE_HEIGHT + adj_size(6);
  
    add_to_gui_list(ID_OPTIONS_LOCAL_SOUND_CHECKBOX, pTmpGui);
  
    /* 'sound befor new turn' label */
    pStr = create_str16_from_char(_("Sound bell at new turn"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    
    pTmpGui->size.x = pWindow->size.x + adj_size(55);
  
    add_to_gui_list(ID_OPTIONS_LOCAL_SOUND_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        ((pTmpGui->next->size.h - pTmpGui->size.h) / 2);
  
    /* 'smooth unit move msec' */
  
    /* edit */
    my_snprintf(cBuf, sizeof(cBuf), "%d", smooth_move_unit_msec);
    pTmpGui = create_edit_from_chars(NULL, pWindow->dst, cBuf, adj_font(11), adj_size(25),
                                            WF_RESTORE_BACKGROUND);
    pTmpGui->action = smooth_move_unit_msec_callback;
    set_wstate(pTmpGui, FC_WS_NORMAL);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(12);
  
    add_to_gui_list(ID_OPTIONS_LOCAL_MOVE_STEP_EDIT, pTmpGui);
    pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + adj_size(4);
  
    /* label */
    pStr = create_str16_from_char(_("Smooth unit move steps"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    
    pTmpGui->size.x = pWindow->size.x + adj_size(55);
  
    add_to_gui_list(ID_OPTIONS_LOCAL_MOVE_STEP_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        (pTmpGui->next->size.h - pTmpGui->size.h) / 2;
  
    /* 'show combat anim' */
  
    /* check box */
    pTmpGui = create_checkbox(pWindow->dst,
                          do_combat_animation, WF_RESTORE_BACKGROUND);
  
    pTmpGui->action = do_combat_animation_callback;
    set_wstate(pTmpGui, FC_WS_NORMAL);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(15);
  
    add_to_gui_list(ID_OPTIONS_LOCAL_COMBAT_CHECKBOX, pTmpGui);
    pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + adj_size(4);
  
    /* label */
    pStr = create_str16_from_char(_("Show combat animation"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    
    pTmpGui->size.x = pWindow->size.x + adj_size(55);
  
    add_to_gui_list(ID_OPTIONS_LOCAL_COMBAT_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        (pTmpGui->next->size.h - pTmpGui->size.h) / 2;
  
    /* 'show focus anim' */
  
    /* check box */
    pTmpGui = create_checkbox(pWindow->dst,
                          do_focus_animation, WF_RESTORE_BACKGROUND);
  
    pTmpGui->action = do_focus_animation_callback;
    set_wstate(pTmpGui, FC_WS_NORMAL);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(15);
  
    add_to_gui_list(ID_CHECKBOX, pTmpGui);
    pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + adj_size(4);
  
    /* label */
    pStr = create_str16_from_char(_("Show focus animation"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    
    pTmpGui->size.x = pWindow->size.x + adj_size(55);
  
    add_to_gui_list(ID_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        (pTmpGui->next->size.h - pTmpGui->size.h) / 2;
  
    /* 'show cursors anim' */
  
    /* check box */
    pTmpGui = create_checkbox(pWindow->dst,
                          do_cursor_animation, WF_RESTORE_BACKGROUND);
  
    pTmpGui->action = do_cursor_animation_callback;
    set_wstate(pTmpGui, FC_WS_NORMAL);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(15);
  
    add_to_gui_list(ID_CHECKBOX, pTmpGui);
    pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + adj_size(4);
  
    /* label */
    pStr = create_str16_from_char(_("Show cursors animation"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    
    pTmpGui->size.x = pWindow->size.x + adj_size(55);
  
    add_to_gui_list(ID_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        (pTmpGui->next->size.h - pTmpGui->size.h) / 2;
  
    /* 'use color cursors' */
  
    /* check box */
    pTmpGui = create_checkbox(pWindow->dst,
                          use_color_cursors, WF_RESTORE_BACKGROUND);
  
    pTmpGui->action = use_color_cursors_callback;
    set_wstate(pTmpGui, FC_WS_NORMAL);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(15);
  
    add_to_gui_list(ID_CHECKBOX, pTmpGui);
    pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + adj_size(4);
  
    /* label */
    pStr = create_str16_from_char(_("Use color cursors"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    
    pTmpGui->size.x = pWindow->size.x + adj_size(55);
  
    add_to_gui_list(ID_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        (pTmpGui->next->size.h - pTmpGui->size.h) / 2;
  
    /* 'auto center on units' */
    /* check box */
    pTmpGui = create_checkbox(pWindow->dst,
                  auto_center_on_unit, WF_RESTORE_BACKGROUND);
  
    pTmpGui->action = auto_center_on_unit_callback;
    set_wstate(pTmpGui, FC_WS_NORMAL);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(15);
  
    add_to_gui_list(ID_OPTIONS_LOCAL_ACENTER_CHECKBOX, pTmpGui);
    pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + adj_size(4);
  
    /* label */
    pStr = create_str16_from_char(_("Auto Center on Units"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    
    pTmpGui->size.x = pWindow->size.x + adj_size(55);
  
    add_to_gui_list(ID_OPTIONS_LOCAL_ACENTER_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        ((pTmpGui->next->size.h - pTmpGui->size.h) / 2);
  
    /* 'auto center on combat' */
    /* check box */
    pTmpGui = create_checkbox(pWindow->dst, auto_center_on_combat,
                              WF_RESTORE_BACKGROUND);
  
    pTmpGui->action = auto_center_on_combat_callback;
    set_wstate(pTmpGui, FC_WS_NORMAL);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(15);
  
    add_to_gui_list(ID_OPTIONS_LOCAL_COMBAT_CENTER_CHECKBOX, pTmpGui);
    pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + adj_size(4);
  
    /* label */
    pStr = create_str16_from_char(_("Auto Center on Combat"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    
    pTmpGui->size.x = pWindow->size.x + adj_size(55);
  
    add_to_gui_list(ID_OPTIONS_LOCAL_COMBAT_CENTER_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        (pTmpGui->next->size.h - pTmpGui->size.h) / 2;
  
    /* 'wakeup focus' */
  
    /* check box */
    pTmpGui = create_checkbox(pWindow->dst,
                  wakeup_focus, WF_RESTORE_BACKGROUND);
  
    pTmpGui->action = wakeup_focus_callback;
    set_wstate(pTmpGui, FC_WS_NORMAL);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(15);
  
    add_to_gui_list(ID_OPTIONS_LOCAL_ACTIVE_UNITS_CHECKBOX, pTmpGui);
    pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + adj_size(4);
  
    /* label */
    pStr = create_str16_from_char(_("Focus on Awakened Units"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    
    pTmpGui->size.x = pWindow->size.x + adj_size(55);
  
    add_to_gui_list(ID_OPTIONS_LOCAL_ACTIVE_UNITS_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        (pTmpGui->next->size.h - pTmpGui->size.h) / 2;
  
    /* 'popup new city window' */
    /* check box */
    pTmpGui = create_checkbox(pWindow->dst, popup_new_cities,
                              WF_RESTORE_BACKGROUND);
  
    pTmpGui->action = popup_new_cities_callback;
    set_wstate(pTmpGui, FC_WS_NORMAL);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(15);
  
    add_to_gui_list(ID_OPTIONS_LOCAL_CITY_CENTER_CHECKBOX, pTmpGui);
    pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + adj_size(4);
  
    /* label */
    pStr = create_str16_from_char(_("Pop up city dialog for new cities"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(55);
  
    add_to_gui_list(ID_OPTIONS_LOCAL_CITY_CENTER_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        (pTmpGui->next->size.h - pTmpGui->size.h) / 2;
  
    /* 'popup new city window' */
    /* check box */
    pTmpGui = create_checkbox(pWindow->dst, ask_city_name,
                              WF_RESTORE_BACKGROUND);
  
    pTmpGui->action = ask_city_names_callback;
    set_wstate(pTmpGui, FC_WS_NORMAL);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(15);
  
    add_to_gui_list(ID_OPTIONS_LOCAL_CITY_CENTER_CHECKBOX, pTmpGui);
    pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + adj_size(4);
  
    /* label */
    pStr = create_str16_from_char(_("Prompt for city names"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    
    pTmpGui->size.x = pWindow->size.x + adj_size(55);
  
    add_to_gui_list(ID_OPTIONS_LOCAL_CITY_CENTER_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        (pTmpGui->next->size.h - pTmpGui->size.h) / 2;
    /* 'auto turn done' */
  
    /* check box */
    pTmpGui = create_checkbox(pWindow->dst,
                  auto_turn_done, WF_RESTORE_BACKGROUND);
  
    pTmpGui->action = auto_turn_done_callback;
    set_wstate(pTmpGui, FC_WS_NORMAL);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(15);
  
    add_to_gui_list(ID_OPTIONS_LOCAL_END_TURN_CHECKBOX, pTmpGui);
    pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + adj_size(4);
  
    /* label */
    pStr = create_str16_from_char(_("End Turn when done moving"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    
    pTmpGui->size.x = pWindow->size.x + adj_size(55);
  
    add_to_gui_list(ID_OPTIONS_LOCAL_END_TURN_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        (pTmpGui->next->size.h - pTmpGui->size.h) / 2;
    /* ------------------------- */
  
    pOption_Dlg->pBeginOptionsWidgetList = pTmpGui;
    redraw_group(pOption_Dlg->pBeginOptionsWidgetList,
                                    pOption_Dlg->pEndOptionsWidgetList, 0);
    flush_rect(pWindow->size, FALSE);
  }
  return -1;
}

/* ===================================================================== */

/**************************************************************************
  ...
**************************************************************************/
static int draw_city_names_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    redraw_icon(pWidget);
    flush_rect(pWidget->size, FALSE);
    draw_city_names ^= 1;
    update_map_canvas_visible();
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int draw_city_productions_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    redraw_icon(pWidget);
    flush_rect(pWidget->size, FALSE);
    draw_city_productions ^= 1;
    update_map_canvas_visible();
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int borders_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    redraw_icon(pWidget);
    flush_rect(pWidget->size, FALSE);
    draw_borders ^= 1;
    update_map_canvas_visible();
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int draw_terrain_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    redraw_icon(pWidget);
    sdl_dirty_rect(pWidget->size);
    draw_terrain ^= 1;
    update_map_canvas_visible();
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int map_grid_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    redraw_icon(pWidget);
    sdl_dirty_rect(pWidget->size);
    draw_map_grid ^= 1;
    
    if (draw_map_grid) {
      set_wstate(pWidget->prev->prev, FC_WS_NORMAL);
    } else {
      set_wstate(pWidget->prev->prev, FC_WS_DISABLED);
    }
    redraw_icon(pWidget->prev->prev);
    sdl_dirty_rect(pWidget->prev->prev->size);
    
    if (draw_map_grid
        && (SDL_Client_Flags & CF_DRAW_CITY_GRID) == CF_DRAW_CITY_GRID) {
      set_wstate(pWidget->prev->prev->prev->prev, FC_WS_NORMAL);
    } else {
      set_wstate(pWidget->prev->prev->prev->prev, FC_WS_DISABLED);
    }
    redraw_icon(pWidget->prev->prev->prev->prev);
    sdl_dirty_rect(pWidget->prev->prev->prev->prev->size);
    
    flush_dirty();
    update_map_canvas_visible();
  }  
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int draw_city_map_grid_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    redraw_icon(pWidget);
    sdl_dirty_rect(pWidget->size);
    SDL_Client_Flags ^= CF_DRAW_CITY_GRID;
    if((SDL_Client_Flags & CF_DRAW_CITY_GRID) == CF_DRAW_CITY_GRID) {
      set_wstate(pWidget->prev->prev, FC_WS_NORMAL);
    } else {
      set_wstate(pWidget->prev->prev, FC_WS_DISABLED);
    }
    redraw_icon(pWidget->prev->prev);
    sdl_dirty_rect(pWidget->prev->prev->size);
    
    flush_dirty();
    update_map_canvas_visible();
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int draw_city_worker_map_grid_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    redraw_icon(pWidget);
    flush_rect(pWidget->size, FALSE);
    SDL_Client_Flags ^= CF_DRAW_CITY_WORKER_GRID;
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int draw_specials_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    redraw_icon(pWidget);
    flush_rect(pWidget->size, FALSE);
    draw_specials ^= 1;
    update_map_canvas_visible();
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int draw_pollution_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    redraw_icon(pWidget);
    flush_rect(pWidget->size, FALSE);
    draw_pollution ^= 1;
    update_map_canvas_visible();
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int draw_cities_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    redraw_icon(pWidget);
    flush_rect(pWidget->size, FALSE);
    draw_cities ^= 1;
    update_map_canvas_visible();
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int draw_units_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    redraw_icon(pWidget);
    flush_rect(pWidget->size, FALSE);
    draw_units ^= 1;
    update_map_canvas_visible();
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int draw_fog_of_war_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    redraw_icon(pWidget);
    flush_rect(pWidget->size, FALSE);
    draw_fog_of_war ^= 1;
    update_map_canvas_visible();
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int draw_roads_rails_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    redraw_icon(pWidget);
    flush_rect(pWidget->size, FALSE);
    draw_roads_rails ^= 1;
    update_map_canvas_visible();
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int draw_irrigation_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    redraw_icon(pWidget);
    flush_rect(pWidget->size, FALSE);
    draw_irrigation ^= 1;
    update_map_canvas_visible();
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int draw_mines_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    redraw_icon(pWidget);
    flush_rect(pWidget->size, FALSE);
    draw_mines ^= 1;
    update_map_canvas_visible();
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int draw_fortress_airbase_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    redraw_icon(pWidget);
    flush_rect(pWidget->size, FALSE);
    draw_fortress_airbase ^= 1;
    update_map_canvas_visible();
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int map_setting_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    SDL_Color text_color = *get_game_colorRGB(COLOR_THEME_CHECKBOX_LABEL_TEXT);
    SDL_String16 *pStr = NULL;
    struct widget *pTmpGui = NULL, *pWindow = pOption_Dlg->pEndOptionsWidgetList;
  
    /* clear flag */
    SDL_Client_Flags &= ~CF_OPTION_MAIN;
  
    /* hide main widget group */
    hide_group(pOption_Dlg->pBeginMainOptionsWidgetList,
               pOption_Dlg->pBeginCoreOptionsWidgetList->prev);
   
    /* 'draw city names' */
    /* check box */
    pTmpGui = create_checkbox(pWindow->dst,
                    draw_city_names, WF_RESTORE_BACKGROUND);
  
    pTmpGui->action = draw_city_names_callback;
    set_wstate(pTmpGui, FC_WS_NORMAL);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(15);
    pTmpGui->size.y = pWindow->size.y + WINDOW_TITLE_HEIGHT + adj_size(6);
    
    add_to_gui_list(ID_OPTIONS_MAP_CITY_NAMES_CHECKBOX, pTmpGui);
    
    /* label */
    pStr = create_str16_from_char(_("City Names"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    
    pTmpGui->size.x = pWindow->size.x + adj_size(55);
  
    add_to_gui_list(ID_OPTIONS_MAP_CITY_NAMES_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        ((pTmpGui->next->size.h - pTmpGui->size.h) / 2);
  
    /* 'draw city prod.' */
    /* check box */
    pTmpGui = create_checkbox(pWindow->dst, draw_city_productions,
                              WF_RESTORE_BACKGROUND);
  
    pTmpGui->action = draw_city_productions_callback;
    set_wstate(pTmpGui, FC_WS_NORMAL);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(15);
  
    add_to_gui_list(ID_OPTIONS_MAP_CITY_PROD_CHECKBOX, pTmpGui);
    pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + adj_size(4);
  
    /* label */
    pStr = create_str16_from_char(_("City Production"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    
    pTmpGui->size.x = pWindow->size.x + adj_size(55);
  
    add_to_gui_list(ID_OPTIONS_MAP_CITY_NAMES_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        ((pTmpGui->next->size.h - pTmpGui->size.h) / 2);
  
    /* 'draw borders' */
    /* check box */
    pTmpGui = create_checkbox(pWindow->dst, draw_borders,
                              WF_RESTORE_BACKGROUND);
  
    pTmpGui->action = borders_callback;
    set_wstate(pTmpGui, FC_WS_NORMAL);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(15);
  
    add_to_gui_list(ID_OPTIONS_MAP_BORDERS_CHECKBOX, pTmpGui);
    pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + adj_size(4);
  
    /* label */
    pStr = create_str16_from_char(_("National Borders"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    
    pTmpGui->size.x = pWindow->size.x + adj_size(55);
  
    add_to_gui_list(ID_OPTIONS_MAP_BORDERS_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        ((pTmpGui->next->size.h - pTmpGui->size.h) / 2);
  
    /* 'draw terrain' */
    /* check box */
    pTmpGui = create_checkbox(pWindow->dst,
                          draw_terrain, WF_RESTORE_BACKGROUND);
  
    pTmpGui->action = draw_terrain_callback;
    set_wstate(pTmpGui, FC_WS_NORMAL);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(15);
  
    add_to_gui_list(ID_OPTIONS_MAP_CITY_PROD_CHECKBOX, pTmpGui);
    pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + adj_size(4);
  
    /* label */
    pStr = create_str16_from_char(_("Terrain"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    
    pTmpGui->size.x = pWindow->size.x + adj_size(55);
  
    add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        (pTmpGui->next->size.h - pTmpGui->size.h) / 2;
        
    /* 'draw map gird' */
    /* check box */
    pTmpGui = create_checkbox(pWindow->dst,
                    draw_map_grid, WF_RESTORE_BACKGROUND);
  
    pTmpGui->action = map_grid_callback;
    set_wstate(pTmpGui, FC_WS_NORMAL);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(15);
    
    add_to_gui_list(ID_OPTIONS_MAP_GRID_CHECKBOX, pTmpGui);
    pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + adj_size(4);
    
    /* 'sound befor new turn' label */
    pStr = create_str16_from_char(_("Map Grid"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    
    pTmpGui->size.x = pWindow->size.x + adj_size(55);
  
    add_to_gui_list(ID_OPTIONS_MAP_GRID_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        ((pTmpGui->next->size.h - pTmpGui->size.h) / 2);
    
    /* Draw City Grids */
    /* check box */
    pTmpGui = create_checkbox(pWindow->dst,
          ((SDL_Client_Flags & CF_DRAW_CITY_GRID) == CF_DRAW_CITY_GRID),
                              WF_RESTORE_BACKGROUND);
  
    pTmpGui->action = draw_city_map_grid_callback;
    if (draw_map_grid) {
      set_wstate(pTmpGui, FC_WS_NORMAL);
    }
    pTmpGui->size.x = pWindow->size.x + adj_size(35);
  
    add_to_gui_list(ID_CHECKBOX, pTmpGui);
    pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + adj_size(4);
  
    /* label */
    pStr = create_str16_from_char(_("Draw city map grid"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    
    pTmpGui->size.x = pWindow->size.x + adj_size(75);
  
    add_to_gui_list(ID_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        (pTmpGui->next->size.h - pTmpGui->size.h) / 2;
        
    /* Draw City Workers Grids */
    /* check box */
    pTmpGui = create_checkbox(pWindow->dst,
          ((SDL_Client_Flags & CF_DRAW_CITY_WORKER_GRID) == CF_DRAW_CITY_WORKER_GRID),
                              WF_RESTORE_BACKGROUND);
  
    pTmpGui->action = draw_city_worker_map_grid_callback;
    if(draw_map_grid
      && (SDL_Client_Flags & CF_DRAW_CITY_GRID) == CF_DRAW_CITY_GRID) {
      set_wstate(pTmpGui, FC_WS_NORMAL);
    }
  
    pTmpGui->size.x = pWindow->size.x + adj_size(35);
  
    add_to_gui_list(ID_CHECKBOX, pTmpGui);
    pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + adj_size(4);
  
    /* label */
    pStr = create_str16_from_char(_("Draw city worker map grid"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    
    pTmpGui->size.x = pWindow->size.x + adj_size(75);
  
    add_to_gui_list(ID_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        (pTmpGui->next->size.h - pTmpGui->size.h) / 2;
    
    /* 'draw specials' */
  
    /* check box */
    pTmpGui = create_checkbox(pWindow->dst,
                          draw_specials, WF_RESTORE_BACKGROUND);
  
    pTmpGui->action = draw_specials_callback;
    set_wstate(pTmpGui, FC_WS_NORMAL);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(15);
  
    add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_SPEC_CHECKBOX, pTmpGui);
    pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + adj_size(4);
  
    /* label */
    pStr = create_str16_from_char(_("Special Resources"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    
    pTmpGui->size.x = pWindow->size.x + adj_size(55);
  
    add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_SPEC_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        (pTmpGui->next->size.h - pTmpGui->size.h) / 2;
  
    /* 'draw pollutions' */
  
    /* check box */
    pTmpGui = create_checkbox(pWindow->dst,
                            draw_pollution, WF_RESTORE_BACKGROUND);
  
    pTmpGui->action = draw_pollution_callback;
    set_wstate(pTmpGui, FC_WS_NORMAL);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(15);
  
    add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_POLL_CHECKBOX, pTmpGui);
    pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + adj_size(4);
  
    /* label */
    pStr = create_str16_from_char(_("Pollution"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    
    pTmpGui->size.x = pWindow->size.x + adj_size(55);
  
    add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_POLL_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        (pTmpGui->next->size.h - pTmpGui->size.h) / 2;
  
    /* 'draw cities' */
  
    /* check box */
    pTmpGui = create_checkbox(pWindow->dst, 
                                  draw_cities, WF_RESTORE_BACKGROUND);
  
    pTmpGui->action = draw_cities_callback;
    set_wstate(pTmpGui, FC_WS_NORMAL);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(15);
  
    add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_CITY_CHECKBOX, pTmpGui);
    pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + adj_size(4);
  
    /* label */
    pStr = create_str16_from_char(_("Cities"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    
    pTmpGui->size.x = pWindow->size.x + adj_size(55);
  
    add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_CITY_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        (pTmpGui->next->size.h - pTmpGui->size.h) / 2;
  
    /* 'draw units' */
  
    /* check box */
    pTmpGui = create_checkbox(pWindow->dst,
                          draw_units, WF_RESTORE_BACKGROUND);
  
    pTmpGui->action = draw_units_callback;
    set_wstate(pTmpGui, FC_WS_NORMAL);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(15);
  
    add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_UNITS_CHECKBOX, pTmpGui);
    pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + adj_size(3);
  
    /* label */
    pStr = create_str16_from_char(_("Units"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    
    pTmpGui->size.x = pWindow->size.x + adj_size(55);
  
    add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_UNITS_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        (pTmpGui->next->size.h - pTmpGui->size.h) / 2;
  
    /* 'draw fog of war' */
  
    /* check box */
    pTmpGui = create_checkbox(pWindow->dst,
                          draw_fog_of_war, WF_RESTORE_BACKGROUND);
  
    pTmpGui->action = draw_fog_of_war_callback;
    set_wstate(pTmpGui, FC_WS_NORMAL);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(15);
  
    add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_FOG_CHECKBOX, pTmpGui);
    pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + adj_size(3);
  
    /* label */
    pStr = create_str16_from_char(_("Fog of War"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    
    pTmpGui->size.x = pWindow->size.x + adj_size(55);
  
    add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_FOG_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        (pTmpGui->next->size.h - pTmpGui->size.h) / 2;
    
    /* 'draw road / rails' */
    /* check box */
    pTmpGui = create_checkbox(pWindow->dst,
                          draw_roads_rails, WF_RESTORE_BACKGROUND);
  
    pTmpGui->action = draw_roads_rails_callback;
    set_wstate(pTmpGui, FC_WS_NORMAL);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(170);
    pTmpGui->size.y = pWindow->size.y + WINDOW_TITLE_HEIGHT + adj_size(6);
    
    add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_RR_CHECKBOX, pTmpGui);
    
    /* label */
    pStr = create_str16_from_char(_("Roads and Rails"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    
    pTmpGui->size.x = pWindow->size.x + adj_size(210);
  
    add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_RR_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        (pTmpGui->next->size.h - pTmpGui->size.h) / 2;
  
    /* 'draw irrigations' */
    /* check box */
    pTmpGui = create_checkbox(pWindow->dst, 
                          draw_irrigation, WF_RESTORE_BACKGROUND);
  
    pTmpGui->action = draw_irrigation_callback;
    set_wstate(pTmpGui, FC_WS_NORMAL);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(170);
  
    add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_IR_CHECKBOX, pTmpGui);
    pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + adj_size(4);
  
    /* label */
    pStr = create_str16_from_char(_("Irrigation"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    
    pTmpGui->size.x = pWindow->size.x + adj_size(210);
  
    add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_IR_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        (pTmpGui->next->size.h - pTmpGui->size.h) / 2;
  
    /* 'draw mines' */
  
    /* check box */
    pTmpGui = create_checkbox(pWindow->dst,
                          draw_mines, WF_RESTORE_BACKGROUND);
  
    pTmpGui->action = draw_mines_callback;
    set_wstate(pTmpGui, FC_WS_NORMAL);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(170);
  
    add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_M_CHECKBOX, pTmpGui);
    pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + adj_size(4);
  
    /* label */
    pStr = create_str16_from_char(_("Mines"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(210);
  
    add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_M_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        (pTmpGui->next->size.h - pTmpGui->size.h) / 2;
  
    /* 'draw fortress / air bases' */
    /* check box */
    pTmpGui = create_checkbox(pWindow->dst, draw_fortress_airbase,
                              WF_RESTORE_BACKGROUND);
  
    pTmpGui->action = draw_fortress_airbase_callback;
    set_wstate(pTmpGui, FC_WS_NORMAL);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(170);
  
    add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_FA_CHECKBOX, pTmpGui);
    pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + adj_size(4);
  
    /* label */
    pStr = create_str16_from_char(_("Fortress and Airbase"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    
    pTmpGui->size.x = pWindow->size.x + adj_size(210);
  
    add_to_gui_list(ID_OPTIONS_MAP_TERRAIN_FA_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        (pTmpGui->next->size.h - pTmpGui->size.h) / 2;
  
  #if 0
    /* Civ3 / Classic CITY Text Style */
    /* check box */
    pTmpGui = create_checkbox(pWindow->dst,
          ((SDL_Client_Flags & CF_CIV3_CITY_TEXT_STYLE) == CF_CIV3_CITY_TEXT_STYLE),
                              WF_RESTORE_BACKGROUND);
  
    pTmpGui->action = draw_civ3_city_text_style_callback;
    set_wstate(pTmpGui, FC_WS_NORMAL);
  
    pTmpGui->size.x = pWindow->size.x + adj_size(170);
  
    add_to_gui_list(ID_OPTIONS_MAP_CITY_CIV3_TEXT_STYLE_CHECKBOX, pTmpGui);
    pTmpGui->size.y = pTmpGui->next->next->size.y + pTmpGui->size.h + adj_size(4);
  
    /* label */
    pStr = create_str16_from_char(_("Civ3 city text style"), adj_font(10));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->fgcol = text_color;
    pTmpGui = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    
    pTmpGui->size.x = pWindow->size.x + adj_size(210);
  
    add_to_gui_list(ID_OPTIONS_MAP_CITY_CIV3_TEXT_STYLE_LABEL, pTmpGui);
  
    pTmpGui->size.y = pTmpGui->next->size.y +
        (pTmpGui->next->size.h - pTmpGui->size.h) / 2;
  #endif      
    /* ================================================== */
    
    pOption_Dlg->pBeginOptionsWidgetList = pTmpGui;
  
    /* redraw window group */
    redraw_group(pOption_Dlg->pBeginOptionsWidgetList,
                            pOption_Dlg->pEndOptionsWidgetList, 0);
    flush_rect(pWindow->size, FALSE);
  }
  return -1;
}

/* ===================================================================== */

/**************************************************************************
  ...
**************************************************************************/
static int disconnect_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    SDL_Rect dest;
    
    popdown_optiondlg();
    
    if (get_client_state() == CLIENT_PRE_GAME_STATE) {
      /* undraw buton */
      dest = pOptions_Button->size;
      fix_rect(pOptions_Button->dst, &dest);
      clear_surface(pOptions_Button->dst, &dest);
      sdl_dirty_rect(pOptions_Button->size);
      
  #if 0
      /* hide "waiting for game start" label */
      pOptions_Button = get_widget_pointer_form_main_list(ID_WAITING_LABEL);
      area = pOptions_Button->size;
      alphablit(pOptions_Button->gfx, NULL, pOptions_Button->dst, &area);
      sdl_dirty_rect(pOptions_Button->size);
  #endif    
        
      flush_dirty();
    }
    
    set_wstate(pOptions_Button, FC_WS_NORMAL);
    disconnect_from_server();
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int back_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    if(pOption_Dlg->pADlg) {
      FC_FREE(pOption_Dlg->pADlg->pScroll);
      FC_FREE(pOption_Dlg->pADlg);
    }
    
    if (SDL_Client_Flags & CF_OPTION_MAIN) {
      popdown_optiondlg();
      if(aconnection.established) {
        set_wstate(pOptions_Button, FC_WS_NORMAL);
        redraw_icon(pOptions_Button);
        sdl_dirty_rect(pOptions_Button->size);
        flush_dirty();
      } else {
        set_client_page(PAGE_MAIN);
      }
      return -1;
    }
  
    del_group_of_widgets_from_gui_list(pOption_Dlg->pBeginOptionsWidgetList,
                          pOption_Dlg->pBeginMainOptionsWidgetList->prev);
  
    pOption_Dlg->pBeginOptionsWidgetList =
                            pOption_Dlg->pBeginMainOptionsWidgetList;
  
    show_group(pOption_Dlg->pBeginOptionsWidgetList,
                            pOption_Dlg->pBeginCoreOptionsWidgetList->prev);
  
    SDL_Client_Flags |= CF_OPTION_MAIN;
    
    redraw_group(pOption_Dlg->pBeginOptionsWidgetList,
                            pOption_Dlg->pEndOptionsWidgetList, 0);
  
    flush_rect(pOption_Dlg->pEndOptionsWidgetList->size, FALSE);
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
int optiondlg_callback(struct widget *pButton)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    SDL_Rect dest;
    
    set_wstate(pButton, FC_WS_DISABLED);
    dest = pButton->size;
    fix_rect(pButton->dst, &dest);
    clear_surface(pButton->dst, &dest);
    real_redraw_icon(pButton);
    flush_rect(pButton->size, FALSE);
  
    popup_optiondlg();
  }
  return -1;
}

/* ===================================================================== */
/* =================================== Public ========================== */
/* ===================================================================== */

void init_options_button(void)
{
  pOptions_Button = create_themeicon(pTheme->Options_Icon, Main.gui,
				       (WF_WIDGET_HAS_INFO_LABEL |
					WF_RESTORE_BACKGROUND));
  pOptions_Button->size.x = adj_size(5);
  pOptions_Button->size.y = adj_size(5);
  pOptions_Button->action = optiondlg_callback;
  pOptions_Button->string16 = create_str16_from_char(_("Options"), adj_font(12));
  pOptions_Button->key = SDLK_TAB;
  set_wstate(pOptions_Button, FC_WS_NORMAL);
  set_wflag(pOptions_Button, WF_HIDDEN);
  
  #ifndef SMALL_SCREEN
  add_to_gui_list(ID_CLIENT_OPTIONS, pOptions_Button);
  #endif
}

static int exit_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    popdown_optiondlg();
    force_exit_from_event_loop();
  }
  return 0;
}

/**************************************************************************
  ...
**************************************************************************/
void popup_optiondlg(void)
{
  struct widget *pTmp_GUI, *pWindow;
  struct widget *pQuit, *pDisconnect = NULL, *pBack;
  SDL_String16 *pStr;
  SDL_Surface *pLogo;
  int longest = 0, w, h, start_x, start_y;
  
  if(pOption_Dlg) {
    return;
  }
  
  popdown_all_game_dialogs();
  flush_dirty();
  
  pOption_Dlg = fc_calloc(1, sizeof(struct OPT_DLG));
  pOption_Dlg->pADlg = NULL;
  pLogo = theme_get_background(theme, BACKGROUND_OPTIONDLG);
  
  /* create window widget */
  pStr = create_str16_from_char(_("Options"), adj_font(12));
  pStr->style |= TTF_STYLE_BOLD;
  
  pWindow = create_window(NULL, pStr, adj_size(10), adj_size(10), 0);
  pWindow->action = main_optiondlg_callback;
  
  set_wstate(pWindow, FC_WS_NORMAL);
  add_to_gui_list(ID_OPTIONS_WINDOW, pWindow);
  pOption_Dlg->pEndOptionsWidgetList = pWindow;

  w = 0;
  /* create exit button */
  pQuit = create_themeicon_button_from_chars(pTheme->CANCEL_Icon,
				pWindow->dst, _("Quit"), adj_font(12), 0);
  w += adj_size(10) + pQuit->size.w;
  pQuit->action = exit_callback;
  pQuit->key = SDLK_q;
  set_wstate(pQuit, FC_WS_NORMAL);
  add_to_gui_list(ID_OPTIONS_EXIT_BUTTON, pQuit);

  /* create disconnection button */
  if(aconnection.established) {
    pDisconnect = create_themeicon_button_from_chars(pTheme->BACK_Icon,
				pWindow->dst, _("Disconnect"), adj_font(12), 0);
    w += adj_size(10) + pDisconnect->size.w + adj_size(10);
    pDisconnect->action = disconnect_callback;
    set_wstate(pDisconnect, FC_WS_NORMAL);
    add_to_gui_list(ID_OPTIONS_DISC_BUTTON, pDisconnect);
  }
  
  /* create back button */
  pBack = create_themeicon_button_from_chars(pTheme->BACK_Icon,
				pWindow->dst, _("Back"), adj_font(12), 0);
  w += pBack->size.w + adj_size(10);
  pBack->action = back_callback;
  pBack->key = SDLK_ESCAPE;
  set_wstate(pBack, FC_WS_NORMAL);
  add_to_gui_list(ID_OPTIONS_BACK_BUTTON, pBack);
  pOption_Dlg->pBeginCoreOptionsWidgetList = pBack;
  /* ------------------------------------------------------ */
  
  w = MAX(w, adj_size(360));
  h = adj_size(350);
  
  start_x = (Main.screen->w - w) / 2;
  start_y = (Main.screen->h - h) / 2;
  pWindow->size.x = start_x;
  pWindow->size.y = start_y;
  
  if (resize_window(pWindow, pLogo, NULL, w, h)) {
    FREESURFACE(pLogo);
  }
      
  if(aconnection.established) {
    pDisconnect->size.x = start_x + (w - pDisconnect->size.w) / 2;
    pDisconnect->size.y = start_y + h - pDisconnect->size.h - adj_size(10);
    
    pBack->size.x = pDisconnect->size.x - adj_size(10) - pBack->size.w;
    pBack->size.y = start_y + h - pBack->size.h - adj_size(10);
    
    pQuit->size.x = pDisconnect->size.x + pDisconnect->size.w + adj_size(10);
    pQuit->size.y = start_y + h - pQuit->size.h - adj_size(10);
  } else {
    pBack->size.x = start_x + adj_size(10);
    pBack->size.y = start_y + h - pBack->size.h - adj_size(10);
    
    pQuit->size.x = start_x + w - pQuit->size.w - adj_size(10);
    pQuit->size.y = start_y + h - pQuit->size.h - adj_size(10);
  }
      
  /* ============================================================= */

  /* create video button widget */
  pTmp_GUI = create_icon_button_from_chars(NULL,
			pWindow->dst, _("Video options"), adj_font(12), 0);
  pTmp_GUI->size.y = start_y + adj_size(60);
  pTmp_GUI->action = video_callback;
  set_wstate(pTmp_GUI, FC_WS_NORMAL);
  pTmp_GUI->size.h += adj_size(4);

  longest = MAX(longest, pTmp_GUI->size.w);

  add_to_gui_list(ID_OPTIONS_VIDEO_BUTTON, pTmp_GUI);

  /* create sound button widget */
  pTmp_GUI = create_icon_button_from_chars(NULL,
				pWindow->dst, _("Sound options"), adj_font(12), 0);
  pTmp_GUI->size.y = start_y + adj_size(90);
  pTmp_GUI->action = sound_callback;
  /* set_wstate( pTmp_GUI, FC_WS_NORMAL ); */
  pTmp_GUI->size.h += adj_size(4);
  longest = MAX(longest, pTmp_GUI->size.w);

  add_to_gui_list(ID_OPTIONS_SOUND_BUTTON, pTmp_GUI);


  /* create local button widget */
  pTmp_GUI =
      create_icon_button_from_chars(NULL, pWindow->dst,
				      _("Game options"), adj_font(12), 0);
  pTmp_GUI->size.y = start_y + adj_size(120);
  pTmp_GUI->action = local_setting_callback;
  set_wstate(pTmp_GUI, FC_WS_NORMAL);
  pTmp_GUI->size.h += adj_size(4);
  longest = MAX(longest, pTmp_GUI->size.w);

  add_to_gui_list(ID_OPTIONS_LOCAL_BUTTON, pTmp_GUI);

  /* create map button widget */
  pTmp_GUI = create_icon_button_from_chars(NULL,
				  pWindow->dst, _("Map options"), adj_font(12), 0);
  pTmp_GUI->size.y = start_y + adj_size(150);
  pTmp_GUI->action = map_setting_callback;
  set_wstate(pTmp_GUI, FC_WS_NORMAL);
  pTmp_GUI->size.h += adj_size(4);
  longest = MAX(longest, pTmp_GUI->size.w);

  add_to_gui_list(ID_OPTIONS_MAP_BUTTON, pTmp_GUI);


  /* create work lists widget */
  pTmp_GUI = create_icon_button_from_chars(NULL, 
  				pWindow->dst, _("Worklists"), adj_font(12), 0);
  pTmp_GUI->size.y = start_y + adj_size(180);
  pTmp_GUI->action = work_lists_callback;
  
  if (get_client_state() == CLIENT_GAME_RUNNING_STATE) {
    set_wstate(pTmp_GUI, FC_WS_NORMAL);
  }

  pTmp_GUI->size.h += adj_size(4);
  longest = MAX(longest, pTmp_GUI->size.w);

  add_to_gui_list(ID_OPTIONS_WORKLIST_BUTTON, pTmp_GUI);

  pOption_Dlg->pBeginOptionsWidgetList = pTmp_GUI;
  pOption_Dlg->pBeginMainOptionsWidgetList = pTmp_GUI;

  /* seting witdth and stat x */
  do {
    pTmp_GUI->size.w = longest;
    pTmp_GUI->size.x = start_x + (w - pTmp_GUI->size.w) / 2;

    pTmp_GUI = pTmp_GUI->next;
  } while (pTmp_GUI != pOption_Dlg->pBeginCoreOptionsWidgetList);

  set_window_pos(pWindow, pWindow->size.x, pWindow->size.y);
  
  /* draw window group */
  redraw_group(pOption_Dlg->pBeginOptionsWidgetList, pWindow, 0);

  sdl_dirty_rect(pWindow->size);

  SDL_Client_Flags |= (CF_OPTION_MAIN | CF_OPTION_OPEN);
  
  gui_sdl_fullscreen = Main.screen->flags & SDL_FULLSCREEN;
  
  disable_main_widgets();
  
  flush_dirty();
}

/**************************************************************************
  ...
**************************************************************************/
void popdown_optiondlg(void)
{
  if (pOption_Dlg) {
    popdown_window_group_dialog(pOption_Dlg->pBeginOptionsWidgetList,
				pOption_Dlg->pEndOptionsWidgetList);
				
    SDL_Client_Flags &= ~(CF_OPTION_MAIN | CF_OPTION_OPEN);
			  
    gui_sdl_fullscreen = Main.screen->flags & SDL_FULLSCREEN;
    
    FC_FREE(pOption_Dlg);
    enable_main_widgets();
  }
}


/**************************************************************************
  If the Options Dlg is open, force Worklist List contents to be updated.
  This function is call by exiting worklist editor to update changed
  worklist name in global worklist report ( Options Dlg )
**************************************************************************/
void update_worklist_report_dialog(void)
{
  if(pOption_Dlg) {
    
    /* this is no NULL when inside worklist editors */
    if(pEdited_WorkList_Name) {
      copy_chars_to_string16(pEdited_WorkList_Name->string16,
        client.worklists[MAX_ID - pEdited_WorkList_Name->ID].name);
      pEdited_WorkList_Name = NULL;
    }
  
    redraw_group(pOption_Dlg->pBeginOptionsWidgetList,
  				pOption_Dlg->pEndOptionsWidgetList, 0);
    sdl_dirty_rect(pOption_Dlg->pEndOptionsWidgetList->size);
  }
}
