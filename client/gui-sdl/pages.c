/********************************************************************** 
 Freeciv - Copyright (C) 1996-2004 - The Freeciv Team
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
#include "log.h"

/* gui-sdl */
#include "chatline.h"
#include "colors.h"
#include "connectdlg.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_stuff.h"
#include "mapctrl.h"
#include "mapview.h"
#include "messagewin.h"
#include "optiondlg.h"
#include "themecolors.h"

#include "pages.h"

static enum client_pages old_page = PAGE_MAIN;

/**************************************************************************
                                  MAIN PAGE
**************************************************************************/
static struct SMALL_DLG *pStartMenu = NULL;

static void popdown_start_menu(void);

/**************************************************************************
  ...
**************************************************************************/
static int join_game_callback(struct GUI *pWidget)
{
  popdown_start_menu();  
  set_client_page(PAGE_NETWORK);
  popup_join_game_dialog(pWidget);
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int servers_callback(struct GUI *pWidget)
{
  popdown_start_menu();  
  popup_connection_dialog(pWidget);
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int options_callback(struct GUI *pWidget)
{
  queue_flush();
  popdown_start_menu();
  popup_optiondlg();
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int quit_callback(struct GUI *pWidget)
{
  popdown_start_menu();
  return 0;/* exit from main game loop */
}

/**************************************************************************
  ...
**************************************************************************/
static void show_main_page()
{
  SDL_Color bg_color = {255, 255, 255, 136};
  
  int w = 0 , h = 0, count = 0;
  struct GUI *pWidget = NULL, *pFirst;
  SDL_Rect *pArea;
  SDL_Surface *pLogo, *pTmp;
    
  /* create dialog */
  pStartMenu = fc_calloc(1, sizeof(struct SMALL_DLG));
    
  /* Start New Game */
  pWidget =
	create_iconlabel_from_chars(NULL, Main.gui, _("Start New Game"), adj_font(14),
	(WF_SELLECT_WITHOUT_BAR|WF_DRAW_THEME_TRANSPARENT|WF_FREE_DATA));
  
  /*pBuf->action = popup_start_new_game_callback;*/
  pWidget->string16->style |= SF_CENTER;
  pWidget->string16->fgcol = *get_game_colorRGB(COLOR_THEME_WIDGET_DISABLED_TEXT);
  
  w = MAX(w, pWidget->size.w);
  h = MAX(h, pWidget->size.h);
  count++;
  
  add_to_gui_list(ID_START_NEW_GAME, pWidget);
  
  pFirst = pWidget;
  pStartMenu->pEndWidgetList = pFirst;
  
  /* Load Game */  
  pWidget = create_iconlabel_from_chars(NULL, Main.gui, _("Load Game"), adj_font(14),
		(WF_SELLECT_WITHOUT_BAR|WF_DRAW_THEME_TRANSPARENT));
  /*pWidget->action = popup_load_game_callback;*/
  pWidget->string16->style |= SF_CENTER;
  pWidget->string16->fgcol = *get_game_colorRGB(COLOR_THEME_WIDGET_DISABLED_TEXT);
  
  add_to_gui_list(ID_LOAD_GAME, pWidget);
  
  w = MAX(w, pWidget->size.w);
  h = MAX(h, pWidget->size.h);
  count++;
  
  /* Join Game */
  pWidget = create_iconlabel_from_chars(NULL, Main.gui, _("Join Game"), adj_font(14),
			WF_SELLECT_WITHOUT_BAR|WF_DRAW_THEME_TRANSPARENT);
  pWidget->action = join_game_callback;
  pWidget->string16->style |= SF_CENTER;  
  set_wstate(pWidget, FC_WS_NORMAL);
  
  add_to_gui_list(ID_JOIN_GAME, pWidget);
  
  w = MAX(w, pWidget->size.w);
  h = MAX(h, pWidget->size.h);
  count++;
    
  /* Join Pubserver */  
  pWidget = create_iconlabel_from_chars(NULL, Main.gui, _("Join Pubserver"), adj_font(14),
			WF_SELLECT_WITHOUT_BAR|WF_DRAW_THEME_TRANSPARENT);
  pWidget->action = servers_callback;
  pWidget->string16->style |= SF_CENTER;  
  set_wstate(pWidget, FC_WS_NORMAL);
  
  add_to_gui_list(ID_JOIN_META_GAME, pWidget);
  
  w = MAX(w, pWidget->size.w);
  h = MAX(h, pWidget->size.h);
  count++;
  
  /* Join LAN Server */  
  pWidget = create_iconlabel_from_chars(NULL, Main.gui, _("Join LAN Server"), adj_font(14),
			WF_SELLECT_WITHOUT_BAR|WF_DRAW_THEME_TRANSPARENT);
  pWidget->action = servers_callback;
  pWidget->string16->style |= SF_CENTER;  
  set_wstate(pWidget, FC_WS_NORMAL);
  
  add_to_gui_list(ID_JOIN_GAME, pWidget);
  
  w = MAX(w, pWidget->size.w);
  h = MAX(h, pWidget->size.h);
  count++;
  
  /* Options */  
  pWidget = create_iconlabel_from_chars(NULL, Main.gui, _("Options"), adj_font(14),
			WF_SELLECT_WITHOUT_BAR|WF_DRAW_THEME_TRANSPARENT);
  pWidget->action = options_callback;
  pWidget->string16->style |= SF_CENTER;
  set_wstate(pWidget, FC_WS_NORMAL);
  
  add_to_gui_list(ID_CLIENT_OPTIONS_BUTTON, pWidget);
  
  w = MAX(w, pWidget->size.w);
  h = MAX(h, pWidget->size.h);
  count++;
  
  /* Quit */  
  pWidget = create_iconlabel_from_chars(NULL, Main.gui, _("Quit"), adj_font(14),
			WF_SELLECT_WITHOUT_BAR|WF_DRAW_THEME_TRANSPARENT);
  pWidget->action = quit_callback;
  pWidget->string16->style |= SF_CENTER;
  pWidget->key = SDLK_ESCAPE;
  set_wstate(pWidget, FC_WS_NORMAL);
  add_to_gui_list(ID_QUIT, pWidget);
  
  w = MAX(w, pWidget->size.w);
  h = MAX(h, pWidget->size.h);
  count++;
  

  pStartMenu->pBeginWidgetList = pWidget;

  /* ------*/

  w+=adj_size(30);
  h+=adj_size(6);
   
  setup_vertical_widgets_position(1,
	(pFirst->dst->w - w) - adj_size(20), (pFirst->dst->h - (h * count)) - adj_size(20),
		w, h, pWidget, pFirst);
		
  pArea = fc_calloc(1, sizeof(SDL_Rect));

  pArea->x = pFirst->size.x - FRAME_WH;
  pArea->y = pFirst->size.y - FRAME_WH;
  pArea->w = pFirst->size.w + DOUBLE_FRAME_WH;
  pArea->h = count * pFirst->size.h + DOUBLE_FRAME_WH;

  pFirst->data.ptr = (void *)pArea;
  
  draw_intro_gfx();
  flush_all();
  
  pLogo = get_logo_gfx();
  pTmp = ResizeSurface(pLogo, pArea->w, pArea->h , 1);
  FREESURFACE(pLogo);
  
  blit_entire_src(pTmp, pFirst->dst, pArea->x , pArea->y);
  FREESURFACE(pTmp);
  
  SDL_FillRectAlpha(pFirst->dst, pArea, &bg_color);
      
  redraw_group(pWidget, pFirst, 0);
  
  draw_frame(pFirst->dst, pArea->x, pArea->y, pArea->w, pArea->h);

  set_output_window_text(_("SDLClient welcomes you..."));

  set_output_window_text(_("Freeciv is free software and you are welcome "
			   "to distribute copies of "
			   "it under certain conditions;"));
  set_output_window_text(_("See the \"Copying\" item on the Help"
			   " menu."));
  set_output_window_text(_("Now.. Go give'em hell!"));
  
  popup_meswin_dialog(true);  

  flush_all();
}

static void popdown_start_menu()
{
  if(pStartMenu) {
    clear_surface(pStartMenu->pEndWidgetList->dst,
    			(SDL_Rect *)pStartMenu->pEndWidgetList->data.ptr);
    
    sdl_dirty_rect(*((SDL_Rect *)pStartMenu->pEndWidgetList->data.ptr));
  
    del_group_of_widgets_from_gui_list(pStartMenu->pBeginWidgetList,
    						pStartMenu->pEndWidgetList);
    FC_FREE(pStartMenu);
  }
}

/**************************************************************************
                             PUBLIC FUNCTIONS
**************************************************************************/

/**************************************************************************
  Sets the "page" that the client should show.  See documentation in
  pages_g.h.
**************************************************************************/
void set_client_page(enum client_pages page)
{

  switch (old_page) {
    case PAGE_GAME:
      disable_main_widgets();
      break;
    default: 
      break;
  }
  
  switch (page) {
    case PAGE_MAIN:
      show_main_page();
      break;
    case PAGE_GAME:
      enable_main_widgets();
    default:
      break;
  }  
  
  old_page = page;
}

/****************************************************************************
  Set the list of available rulesets.  The default ruleset should be
  "default", and if the user changes this then set_ruleset() should be
  called.
****************************************************************************/
void gui_set_rulesets(int num_rulesets, char **rulesets)
{
  /* PORTME */
}

/**************************************************************************
  Returns current client page
**************************************************************************/
enum client_pages get_client_page(void)
{
  return old_page;
}

/**************************************************************************
  update the start page.
**************************************************************************/
void update_start_page(void)
{
  /* PORTME*/    
}
