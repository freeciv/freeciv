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

/***************************************************************************
                          dialogs.c  -  description
                             -------------------
    begin                : Wed Jul 24 2002
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafa³ Bursig <bursig@poczta.fm>
***************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

#include "climap.h" /* for tile_get_known() */
#include "fcintl.h"
#include "log.h"
#include "game.h"
#include "government.h"
#include "map.h"

#include "gui_mem.h"

#include "packets.h"
#include "player.h"
#include "rand.h"
#include "support.h"

#include "chatline.h"
#include "civclient.h"
#include "clinet.h"
#include "control.h"

#include "colors.h"

#include "citydlg_g.h"
#include "citydlg.h"
#include "graphics.h"
#include "unistring.h"
#include "gui_string.h"
#include "gui_stuff.h"
#include "gui_zoom.h"
#include "gui_id.h"
#include "gui_tilespec.h"
#include "gui_main.h"
#include "repodlgs.h"

#include "mapview.h"
#include "mapctrl.h"
#include "options.h"
#include "optiondlg.h"
#include "tilespec.h"

#include "dialogs.h"

#define create_active_iconlabel( pBuf , pStr , pString , pData, pCallback ) \
do { 						\
  pStr = create_str16_from_char( pString , 10);	\
  pStr->style |= TTF_STYLE_BOLD;		\
  pBuf = create_iconlabel( NULL, pStr , 	\
    	     (WF_DRAW_THEME_TRANSPARENT|WF_DRAW_TEXT_LABEL_WITH_SPACE)); \
  pBuf->string16->style &= ~SF_CENTER;		\
  pBuf->string16->backcol.unused = 128;	\
  pBuf->string16->render = 3;			\
  pBuf->data = (void *)pData;			\
  pBuf->action = pCallback;			\
} while(0)


static void popdown_unit_select_dialog(void);
static void popdown_advanced_terrain_dialog(void);
static void popdown_terrain_info_dialog(void);
static void popdown_caravan_dialog(void);
static void popdown_diplomat_dialog(void);
static void popdown_pillage_dialog(void);
static void popdown_incite_dialog(void);
static void popdown_connect_dialog(void);
static void popdown_bribe_dialog(void);

/********************************************************************** 
  This function is called when the client disconnects or the game is
  over.  It should close all dialog windows for that game.
***********************************************************************/
void popdown_all_game_dialogs(void)
{
  popdown_caravan_dialog();  
  popdown_unit_select_dialog();
  popdown_advanced_terrain_dialog();
  popdown_terrain_info_dialog();
  popdown_newcity_dialog();
  podown_optiondlg();
  popdown_diplomat_dialog();
  popdown_pillage_dialog();
  popdown_incite_dialog();
  popdown_connect_dialog();
  popdown_bribe_dialog();
  
  /* clear city text buffer */
  SDL_FillRect(Main.text, NULL, 0x0);
  
  /* clear gui buffer */
  SDL_FillRect(Main.gui, NULL, 0x0);
}

/* ======================================================================= */



/**************************************************************************
  Popup a dialog to display information about an event that has a
  specific location.  The user should be given the option to goto that
  location.
**************************************************************************/
void popup_notify_goto_dialog(char *headline, char *lines, int x, int y)
{
  freelog(LOG_NORMAL, "popup_notify_goto_dialog : PORT ME\n \
  			a: %s\nb: %s",headline, lines );
}

/**************************************************************************
  Popup a generic dialog to display some generic information.
**************************************************************************/
void popup_notify_dialog(char *caption, char *headline, char *lines)
{
  freelog(LOG_NORMAL, "popup_notify_dialog : PORT ME\n \
  		a: %s\nb: %s", caption, headline);
}


/* =======================================================================*/
/* ======================== UNIT SELECTION DIALOG ========================*/
/* =======================================================================*/
static struct ADVANCED_DLG *pUnit_Select_Dlg = NULL;

/**************************************************************************
...
**************************************************************************/
static int unit_select_window_callback( struct GUI *pWindow )
{
  /*
  if (move_window_group_dialog(pUnit_Select_Dlg->pBeginWidgetList,
						    pWindow , Main.gui)) {
    flush_rect(pWindow->size);
  }
  */
  return -1;
}

/**************************************************************************
...
**************************************************************************/
static int exit_unit_select_callback( struct GUI *pWidget )
{
  popdown_unit_select_dialog();
  return -1;
}

/**************************************************************************
...
**************************************************************************/
static int unit_select_callback( struct GUI *pWidget )
{
  struct unit *pUnit = player_find_unit_by_id(game.player_ptr,
                                   MAX_ID - pWidget->ID);

  popdown_unit_select_dialog();
  
  if (pUnit) {
    request_new_unit_activity(pUnit, ACTIVITY_IDLE);
    set_unit_focus(pUnit);
  }

  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int up_unit_select_dlg_callback(struct GUI *pButton)
{
  up_advanced_dlg(pUnit_Select_Dlg, NULL);
  
  unsellect_widget_action();
  pSellected_Widget = pButton;
  set_wstate(pButton, WS_SELLECTED);
  redraw_tibutton(pButton);
  flush_rect(pButton->size);
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int down_unit_select_dlg_callback(struct GUI *pButton)
{
  down_advanced_dlg(pUnit_Select_Dlg, NULL);
  
  unsellect_widget_action();
  pSellected_Widget = pButton;
  set_wstate(pButton, WS_SELLECTED);
  redraw_tibutton(pButton);
  flush_rect(pButton->size);
  return -1;
}

/**************************************************************************
  Popdown a dialog window to select units on a particular tile.
**************************************************************************/
static void popdown_unit_select_dialog(void)
{
  if (pUnit_Select_Dlg) {
    popdown_window_group_dialog(pUnit_Select_Dlg->pBeginWidgetList,
			pUnit_Select_Dlg->pEndWidgetList, Main.gui);
				   
    FREE(pUnit_Select_Dlg->pScroll);
    FREE(pUnit_Select_Dlg);
    flush_dirty();
  }
}

/**************************************************************************
  Popup a dialog window to select units on a particular tile.
**************************************************************************/
void popup_unit_select_dialog(struct tile *ptile)
{
  struct GUI *pBuf = NULL, *pWindow;
  SDL_String16 *pStr;
  struct unit *pUnit;
  struct unit_type *pUnitType;
  char cBuf[255];  
  int i , w = 0, h , n , canvas_x = 0, canvas_y = 0, high = 0;
  
  if (pUnit_Select_Dlg) {
    return;
  }
    
  pUnit_Select_Dlg = MALLOC(sizeof(struct ADVANCED_DLG));
  
  n = unit_list_size(&ptile->units);
  
  my_snprintf(cBuf , sizeof(cBuf),"%s (%d)", _("Unit selection") , n);
  pStr = create_str16_from_char(cBuf , 12);
  pStr->style |= TTF_STYLE_BOLD;
  
  pWindow = create_window( pStr , 10 , 10 , WF_DRAW_THEME_TRANSPARENT );
  
  pWindow->action = unit_select_window_callback;
  set_wstate(pWindow , WS_NORMAL);
  w = MAX(w , pWindow->size.w);
  
  add_to_gui_list(ID_UNIT_SELLECT_DLG_WINDOW, pWindow);
  pUnit_Select_Dlg->pEndWidgetList = pWindow;
  
  /* ---------- */
  h = WINDOW_TILE_HIGH + 1 + FRAME_WH;
  
  for(i=0; i<n; i++) {
    pUnit = unit_list_get(&ptile->units, i);
    pUnitType = unit_type(pUnit);
    
    if (!canvas_x && !canvas_y)
    {
      get_canvas_xy( pUnit->x , pUnit->y , &canvas_x , &canvas_y );
    }
    
    my_snprintf(cBuf , sizeof(cBuf), _("Contact %s (%d / %d) %s(%d,%d,%d) %s"),
            pUnit->veteran ? _("Veteran") : "" ,
            pUnit->hp, pUnitType->hp,
            pUnitType->name,
            pUnitType->attack_strength,
            pUnitType->defense_strength,
            (pUnitType->move_rate / 3),
	    unit_activity_text(pUnit));
    
    
    create_active_iconlabel(pBuf , pStr , cBuf , NULL, unit_select_callback);
            
    add_to_gui_list(MAX_ID - pUnit->id , pBuf);
    
    w = MAX(w, pBuf->size.w);
    h += pBuf->size.h;
    
    if (i > 14)
    {
      set_wflag(pBuf , WF_HIDDEN);
    }
    else
    {
      set_wstate(pBuf , WS_NORMAL);
    }
    
  }
  pUnit_Select_Dlg->pBeginActiveWidgetList = pBuf;
  pUnit_Select_Dlg->pEndActiveWidgetList = pWindow->prev;
  pUnit_Select_Dlg->pActiveWidgetList = pWindow->prev;
  
  w += DOUBLE_FRAME_WH;
  if ( n > 15 )
  {
    int tmp;
    
    /* create up button */
    pBuf = create_themeicon_button(pTheme->UP_Icon, NULL, 0);

    /* ------- window ------- */
    h = WINDOW_TILE_HIGH + 1 + 2 * pBuf->size.h +
	    15 * pWindow->prev->size.h + FRAME_WH;
    
    if (canvas_x + NORMAL_TILE_WIDTH + w > Main.screen->w)
    {
      if (canvas_x - w >= 0)
      {
        pWindow->size.x = canvas_x - w;
      }
      else
      {
	pWindow->size.x = (Main.screen->w - w) / 2;
      }
    }
    else
    {
      pWindow->size.x = canvas_x + NORMAL_TILE_WIDTH;
    }
    
    if (canvas_y - NORMAL_TILE_HEIGHT + h > Main.screen->h)
    {
      if (canvas_y + NORMAL_TILE_HEIGHT - h >= 0)
      {
        pWindow->size.y = canvas_y + NORMAL_TILE_HEIGHT - h;
      }
      else
      {
	pWindow->size.y = (Main.screen->h - h) / 2;
      }
    }
    else
    {
      pWindow->size.y = canvas_y - NORMAL_TILE_HEIGHT;
    }
        
    resize_window(pWindow , NULL , NULL , w , h);
    /* ---------------------- */   
    
    pBuf->size.x = pWindow->size.x + FRAME_WH;
    pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 1;

    pBuf->size.w = w - DOUBLE_FRAME_WH;

    high = pBuf->size.h;

    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);

    pBuf->action = up_unit_select_dlg_callback;
    set_wstate(pBuf, WS_NORMAL);

    add_to_gui_list(ID_UNIT_SELLECT_DLG_UP_BUTTON, pBuf);


    /* create down button */
    pBuf = create_themeicon_button(pTheme->DOWN_Icon, NULL, 0);

    pBuf->size.x = pWindow->size.x + FRAME_WH;
    pBuf->size.y = pWindow->size.y + h - FRAME_WH - pBuf->size.h;

    tmp = pBuf->size.y;

    pBuf->size.w = w - DOUBLE_FRAME_WH;

    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);

    pBuf->action = down_unit_select_dlg_callback;
    set_wstate(pBuf, WS_NORMAL);

    add_to_gui_list(ID_UNIT_SELLECT_DLG_DOWN_BUTTON, pBuf);

    pUnit_Select_Dlg->pScroll = MALLOC(sizeof(struct ScrollBar));
    pUnit_Select_Dlg->pScroll->active = 15;
    pUnit_Select_Dlg->pScroll->count = n;
    pUnit_Select_Dlg->pScroll->min = pBuf->size.y;
    pUnit_Select_Dlg->pScroll->max = tmp;
   
  }
  else
  {
    
    if (canvas_x + NORMAL_TILE_WIDTH + w > Main.gui->w)
    {
      if (canvas_x - w >= 0)
      {
        pWindow->size.x = canvas_x - w;
      }
      else
      {
	pWindow->size.x = ( Main.gui->w - w ) / 2;
      }
    }
    else
    {
      pWindow->size.x = canvas_x + NORMAL_TILE_WIDTH;
    }
    
    if (canvas_y - NORMAL_TILE_HEIGHT + h > Main.gui->h)
    {
      if (canvas_y + NORMAL_TILE_HEIGHT - h >= 0)
      {
        pWindow->size.y = canvas_y + NORMAL_TILE_HEIGHT - h;
      }
      else
      {
	pWindow->size.y = (Main.gui->h - h) / 2;
      }
    }
    else
    {
      pWindow->size.y = canvas_y - NORMAL_TILE_HEIGHT;
    }
    
    resize_window(pWindow , NULL , NULL , w , h);
    
  }

  w-= DOUBLE_FRAME_WH;
  pBuf = pWindow->prev;
  pBuf->size.w = w;
  pBuf->size.x = pWindow->size.x + FRAME_WH;
  pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 1 + high;
  pBuf = pBuf->prev;
  i = 1;
  while(pBuf)
  {
    pBuf->size.w = w;
    pBuf->size.x = pBuf->next->size.x;
    pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
    if (pBuf == pUnit_Select_Dlg->pBeginActiveWidgetList || i > 14 ) break;
    i++;  
    pBuf = pBuf->prev;  
  }
  
  /* create exit button */
  pBuf = create_themeicon(ResizeSurface(pTheme->CANCEL_Icon ,
			  pTheme->CANCEL_Icon->w - 4,
			  pTheme->CANCEL_Icon->h - 4, 1) ,
  			  (WF_FREE_THEME|WF_DRAW_THEME_TRANSPARENT));
  SDL_SetColorKey(pBuf->theme ,
	  SDL_SRCCOLORKEY|SDL_RLEACCEL , get_first_pixel(pBuf->theme));
  pBuf->size.x = pWindow->size.x + pWindow->size.w-pBuf->size.w-FRAME_WH-1;
  pBuf->size.y = pWindow->size.y;
  
  pBuf->action = exit_unit_select_callback;
  set_wstate(pBuf, WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  
  add_to_gui_list(ID_UNIT_SELLECT_DLG_EXIT_BUTTON, pBuf);
  pUnit_Select_Dlg->pBeginWidgetList = pBuf;
  /* ==================================================== */
  /* redraw */
  redraw_group(pUnit_Select_Dlg->pBeginWidgetList, pWindow, Main.gui,0);

  flush_rect(pWindow->size);
}

/* ====================================================================== */
/* ============================ TERRAIN INFO ============================ */
/* ====================================================================== */
static struct SMALL_DLG *pTerrain_Info_Dlg = NULL;


/**************************************************************************
  Popdown terrain information dialog.
**************************************************************************/
static int terrain_info_window_dlg_callback( struct GUI *pWindow )
{
  if (move_window_group_dialog(pTerrain_Info_Dlg->pBeginWidgetList,
						    pWindow, Main.gui)) {
    flush_rect(pWindow->size);
  }
  return -1;
}

/**************************************************************************
  Popdown terrain information dialog.
**************************************************************************/
static void popdown_terrain_info_dialog(void)
{
  if (pTerrain_Info_Dlg) {
    popdown_window_group_dialog(pTerrain_Info_Dlg->pBeginWidgetList,
				pTerrain_Info_Dlg->pEndWidgetList, Main.gui);			   
    FREE(pTerrain_Info_Dlg);
    flush_dirty();
  }
}

/**************************************************************************
  Popdown terrain information dialog.
**************************************************************************/
static int exit_terrain_info_dialog( struct GUI *pButton )
{
  popdown_terrain_info_dialog();
  return -1;
}

/***************************************************************
  Return a (static) string with terrain name;
  eg: "Hills"
  eg: "Hills (Coals)"
  eg: "Hills (Coals) [Pollution]"
***************************************************************/
const char *sdl_map_get_tile_info_text(int x, int y)
{
  static char s[64];
  struct tile *ptile=map_get_tile(x, y);
  bool first;

  my_snprintf(s, sizeof(s), "%s", tile_types[ptile->terrain].terrain_name);
  if (tile_has_special(ptile, S_RIVER)) {
    sz_strlcat(s, "/");
    sz_strlcat(s, get_special_name(S_RIVER));
  }

  first = TRUE;
  if (tile_has_special(ptile, S_SPECIAL_1)) {
    if (first) {
      first = FALSE;
      sz_strlcat(s, " (");
    } else {
      sz_strlcat(s, "/");
    }
    sz_strlcat(s, tile_types[ptile->terrain].special_1_name);
  }
  if (tile_has_special(ptile, S_SPECIAL_2)) {
    if (first) {
      first = FALSE;
      sz_strlcat(s, " (");
    } else {
      sz_strlcat(s, "/");
    }
    sz_strlcat(s, tile_types[ptile->terrain].special_2_name);
  }
  if (!first) {
    sz_strlcat(s, ")");
  }

  first = TRUE;
  if (tile_has_special(ptile, S_POLLUTION)) {
    if (first) {
      first = FALSE;
      sz_strlcat(s, "\n[");
    } else {
      sz_strlcat(s, "/");
    }
    sz_strlcat(s, get_special_name(S_POLLUTION));
  }
  if (tile_has_special(ptile, S_FALLOUT)) {
    if (first) {
      first = FALSE;
      sz_strlcat(s, " [");
    } else {
      sz_strlcat(s, "/");
    }
    sz_strlcat(s, get_special_name(S_FALLOUT));
  }
  if (!first) {
    sz_strlcat(s, "]");
  }
  
  return s;
}


/**************************************************************************
  Popup terrain information dialog.
**************************************************************************/
static void popup_terrain_info_dialog(struct tile *pTile , int x , int y)
{
  SDL_Surface *pSurf;
  struct GUI *pBuf , *pWindow;
  char cBuf[255];  
  int canvas_x , canvas_y;
  
  if (pTerrain_Info_Dlg) {
    return;
  }
  
  get_canvas_xy(x , y , &canvas_x , &canvas_y);
    
  pSurf = get_terrain_surface(x , y);
  pTerrain_Info_Dlg = MALLOC(sizeof(struct SMALL_DLG));
    
  /* ----------- */  
  my_snprintf(cBuf, sizeof(cBuf), "%s [%d,%d]", _("Terrain Info"), x , y);
  
  pWindow = create_window(create_str16_from_char(cBuf , 12) , 10 , 10 , 0);
  pWindow->string16->style |= TTF_STYLE_BOLD;
  
  pWindow->action = terrain_info_window_dlg_callback;
  set_wstate( pWindow , WS_NORMAL );
  
  add_to_gui_list(ID_TERRAIN_INFO_DLG_WINDOW, pWindow);
  pTerrain_Info_Dlg->pEndWidgetList = pWindow;
  /* ---------- */
  
  if(tile_get_known(x, y) >= TILE_KNOWN_FOGGED) {
  
    my_snprintf(cBuf, sizeof(cBuf), _("Terrain: %s\nFood/Prod/Trade: %s\n"),
		sdl_map_get_tile_info_text(x, y),
		map_get_tile_fpt_text(x, y) );
  
  
    if (tile_has_special(pTile, S_HUT))
    { 
      sz_strlcat(cBuf, _("Minor Tribe Village"));
    }
    else
    {
      if (get_tile_infrastructure_set(pTile))
      {
        sz_strlcat(cBuf, _("Infrastructure: "));
        sz_strlcat(cBuf, map_get_infrastructure_text(pTile->special));
      }
    }
  }
  else
  {
    my_snprintf(cBuf, sizeof(cBuf), _("Terrain : UNKNOWN"));
  }
  
  pBuf = create_iconlabel(pSurf ,
	  create_str16_from_char(cBuf, 12), 
    		WF_FREE_THEME);
  
  pBuf->size.h += NORMAL_TILE_HEIGHT / 2;
  
  add_to_gui_list(ID_LABEL , pBuf);
  
  /* ------ window ---------- */
  pWindow->size.w = pBuf->size.w + 20;
  pWindow->size.h = pBuf->size.h + WINDOW_TILE_HIGH + 1 + FRAME_WH;
						    
  if (canvas_x + NORMAL_TILE_WIDTH + pWindow->size.w > Main.screen->w)
  {
    pWindow->size.x = canvas_x - pWindow->size.w;
  }
  else
  {
    pWindow->size.x = canvas_x + NORMAL_TILE_WIDTH;
  }
  
  if (canvas_y - NORMAL_TILE_HEIGHT + pWindow->size.h > Main.screen->h)
  {
    if (canvas_y - pWindow->size.h + NORMAL_TILE_HEIGHT > Main.screen->h)
    {
       pWindow->size.y = (Main.screen->h - pWindow->size.h) / 2;
    }
    else
    {
       pWindow->size.y = canvas_y - pWindow->size.h + NORMAL_TILE_HEIGHT;
    }
  }
  else
  {
    if (canvas_y - NORMAL_TILE_HEIGHT < 0)
    {
      if (canvas_y < 0)
      {
	pWindow->size.y = canvas_y + NORMAL_TILE_HEIGHT;
      }
      else
      {
	pWindow->size.y = canvas_y;
      }        
    }
    else
    {
       pWindow->size.y = canvas_y - NORMAL_TILE_HEIGHT;
    }
  }
  
  resize_window(pWindow , NULL,
	  get_game_colorRGB(COLOR_STD_BACKGROUND_BROWN) ,
				  pWindow->size.w , pWindow->size.h);
  
  /* ------------------------ */
  
  pBuf->size.x = pWindow->size.x + 10;
  pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 1;
  
  pBuf = create_themeicon(ResizeSurface( pTheme->CANCEL_Icon ,
			  pTheme->CANCEL_Icon->w - 4,
			  pTheme->CANCEL_Icon->h - 4, 1) ,
  			  (WF_FREE_THEME|WF_DRAW_THEME_TRANSPARENT));
  SDL_SetColorKey( pBuf->theme ,
	  SDL_SRCCOLORKEY|SDL_RLEACCEL , get_first_pixel(pBuf->theme));
  
  pBuf->size.x = pWindow->size.x + pWindow->size.w-pBuf->size.w-FRAME_WH-1;
  pBuf->size.y = pWindow->size.y;
  
  pBuf->action = exit_terrain_info_dialog;
  set_wstate(pBuf, WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  
  add_to_gui_list(ID_TERRAIN_INFO_DLG_EXIT_BUTTON, pBuf);
    
  pTerrain_Info_Dlg->pBeginWidgetList = pBuf;
   
  
  /* --------------------------------- */
  /* redraw */
  redraw_group(pTerrain_Info_Dlg->pBeginWidgetList, pWindow, Main.gui,0);

  flush_rect(pWindow->size);
}
/* ====================================================================== */
/* ========================= ADVANCED_TERRAIN_MENU ====================== */
/* ====================================================================== */
static struct ADVANCED_DLG  *pAdvanced_Terrain_Dlg = NULL;

/**************************************************************************
  Popdown a generic dialog to display some generic information about
  terrain : tile, units , cities, etc.
**************************************************************************/
static void popdown_advanced_terrain_dialog(void)
{
  if (pAdvanced_Terrain_Dlg) {
    popdown_window_group_dialog(pAdvanced_Terrain_Dlg->pBeginWidgetList,
			pAdvanced_Terrain_Dlg->pEndWidgetList, Main.gui);
				   
    FREE(pAdvanced_Terrain_Dlg->pScroll);
    FREE(pAdvanced_Terrain_Dlg);
    flush_dirty();
  }
}

/**************************************************************************
  ...
**************************************************************************/
static int advanced_terrain_window_dlg_callback(struct GUI *pWindow)
{
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int exit_advanced_terrain_dlg_callback(struct GUI *pWidget)
{
  popdown_advanced_terrain_dialog();
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int terrain_info_callback(struct GUI *pWidget)
{
  int x = ((struct map_position *)pWidget->data)->x;
  int y = ((struct map_position *)pWidget->data)->y;
    
  popdown_advanced_terrain_dialog();

  popup_terrain_info_dialog(map_get_tile(x , y) , x , y);
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int zoom_to_city_callback(struct GUI *pWidget)
{
  struct city *pCity = (struct city *)pWidget->data;
    
  popdown_advanced_terrain_dialog();

  popup_city_dialog(pCity, 0);
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int change_production_callback(struct GUI *pWidget)
{
  struct city *pCity = (struct city *)pWidget->data;
    
  popdown_advanced_terrain_dialog();

  popup_change_production_dialog(pCity);
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int hurry_production_callback(struct GUI *pWidget)
{
  struct city *pCity = (struct city *)pWidget->data;
    
  popdown_advanced_terrain_dialog();

  popup_hurry_production_dialog(pCity);
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int cma_callback(struct GUI *pWidget)
{
  /* struct city *pCity = (struct city *)pWidget->data; */ 
    
  popdown_advanced_terrain_dialog();

  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int change_work_list_callback(struct GUI *pWidget)
{
  /* struct city *pCity = (struct city *)pWidget->data; */
    
  popdown_advanced_terrain_dialog();

  return -1;
}

/**************************************************************************
...
**************************************************************************/
static int adv_unit_select_callback( struct GUI *pWidget )
{
  struct unit *pUnit = player_find_unit_by_id(game.player_ptr,
                                   MAX_ID - pWidget->ID);

  popdown_advanced_terrain_dialog();
  
  if (pUnit) {
    request_new_unit_activity(pUnit, ACTIVITY_IDLE);
    set_unit_focus(pUnit);
  }

  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int up_adv_unit_select_dlg_callback(struct GUI *pButton)
{
  up_advanced_dlg(pAdvanced_Terrain_Dlg, pButton->prev);
  
  unsellect_widget_action();
  pSellected_Widget = pButton;
  set_wstate(pButton, WS_SELLECTED);
  redraw_tibutton(pButton);
  flush_rect(pButton->size);
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int down_adv_unit_select_dlg_callback(struct GUI *pButton)
{
  up_advanced_dlg(pAdvanced_Terrain_Dlg, pButton->next);
  
  unsellect_widget_action();
  pSellected_Widget = pButton;
  set_wstate(pButton, WS_SELLECTED);
  redraw_tibutton(pButton);
  flush_rect(pButton->size);
  return -1;
}

/**************************************************************************
  FIXME : fix main funct : vertic_scroll_widget_list(...)
**************************************************************************/
static int vscroll_adv_unit_select_dlg_callback(struct GUI *pVscrollBar)
{
  vscroll_advanced_dlg(pAdvanced_Terrain_Dlg, pVscrollBar);
    
  unsellect_widget_action();
  set_wstate(pVscrollBar, WS_SELLECTED);
  pSellected_Widget = pVscrollBar;
  redraw_vert(pVscrollBar, Main.screen);
  flush_rect(pVscrollBar->size);
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int connect_here_callback(struct GUI *pWidget)
{
  int x = ((struct map_position *)pWidget->data)->x;
  int y = ((struct map_position *)pWidget->data)->y;
    
  popdown_advanced_terrain_dialog();
  
  /* may not work */
  popup_unit_connect_dialog(get_unit_in_focus(), x, y);
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int goto_here_callback(struct GUI *pWidget)
{
  int x = ((struct map_position *)pWidget->data)->x;
  int y = ((struct map_position *)pWidget->data)->y;
    
  popdown_advanced_terrain_dialog();
  
  /* may not work */
  send_goto_unit(get_unit_in_focus(), x, y);
  return -1;
}


/**************************************************************************
  ...
**************************************************************************/
static int patrol_here_callback(struct GUI *pWidget)
{
  /*int x = ((struct map_position *)pWidget->data)->x;
  int y = ((struct map_position *)pWidget->data)->y;*/
  struct unit *pUnit = get_unit_in_focus();
    
  popdown_advanced_terrain_dialog();
  
  if(pUnit) {
    if (is_air_unit(pUnit)) {
      append_output_window(_("Game: Sorry, airunit patrol not yet implemented."));
    } else {
      /*send_patrol_route(pUnit);*/
    }
  }
  
  return -1;
}

/**************************************************************************
  Popup a generic dialog to display some generic information about
  terrain : tile, units , cities, etc.
**************************************************************************/
void popup_advanced_terrain_dialog(int x , int y)
{
  struct GUI *pWindow = NULL, *pBuf = NULL;
  struct tile *pTile;
  struct city *pCity;
  struct unit *pFocus_Unit;
  SDL_String16 *pStr;
  SDL_Rect area;
  struct map_position *pPos;
  char cBuf[255]; 
  int n , w = 0, h , units_h = 0, canvas_x , canvas_y ;
  
  if (pAdvanced_Terrain_Dlg) {
    return;
  }
  
  pTile = map_get_tile(x, y);
  pCity = pTile->city;
  n = unit_list_size(&pTile->units);
  pFocus_Unit = get_unit_in_focus();
  
  if (!n && !pCity && !pFocus_Unit)
  {
    popup_terrain_info_dialog(pTile , x , y);
    return;
  }
  
  h = WINDOW_TILE_HIGH + 3 + FRAME_WH;
  
  get_canvas_xy( x , y , &canvas_x , &canvas_y );
  
  pAdvanced_Terrain_Dlg = MALLOC(sizeof(struct ADVANCED_DLG));
  
  pPos = MALLOC(sizeof(struct map_position));
  pPos->x = x;
  pPos->y = y;
  
  pStr = create_str16_from_char(_("Advanced Menu") , 12);
  pStr->style |= TTF_STYLE_BOLD;
  
  pWindow = create_window(pStr , 10, 10, WF_DRAW_THEME_TRANSPARENT);
    
  pWindow->action = advanced_terrain_window_dlg_callback;
  set_wstate(pWindow , WS_NORMAL);
  w = MAX(w , pWindow->size.w);
  
  add_to_gui_list(ID_TERRAIN_ADV_DLG_WINDOW, pWindow);
  pAdvanced_Terrain_Dlg->pEndWidgetList = pWindow;
  /* ---------- */
  /* exit button */
  pBuf = create_themeicon(ResizeSurface(pTheme->CANCEL_Icon ,
			  pTheme->CANCEL_Icon->w - 4,
			  pTheme->CANCEL_Icon->h - 4, 1) ,
  			  (WF_FREE_THEME|WF_DRAW_THEME_TRANSPARENT));
  SDL_SetColorKey(pBuf->theme,
	  SDL_SRCCOLORKEY|SDL_RLEACCEL , get_first_pixel(pBuf->theme));
  
  w += pBuf->size.w + 10;
  pBuf->action = exit_advanced_terrain_dlg_callback;
  set_wstate(pBuf, WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  
  add_to_gui_list(ID_TERRAIN_ADV_DLG_EXIT_BUTTON, pBuf);
  /* ---------- */
  
  pStr = create_str16_from_char(_("Terrain Info") , 10);
  pStr->style |= TTF_STYLE_BOLD;
   
  pBuf = create_iconlabel(NULL, pStr , 
    (WF_DRAW_THEME_TRANSPARENT|WF_DRAW_TEXT_LABEL_WITH_SPACE|WF_FREE_DATA));
    
  pBuf->string16->style &= ~SF_CENTER;
  pBuf->string16->render = 3;
  pBuf->string16->backcol.unused = 128;
    
  pBuf->data = (void *)pPos;
  
  pBuf->action = terrain_info_callback;
  set_wstate(pBuf , WS_NORMAL);
  
  add_to_gui_list(ID_LABEL , pBuf);
    
  w = MAX(w , pBuf->size.w);
  h += pBuf->size.h;

  /* ---------- */  
  if ( pCity && pCity->owner == game.player_idx)
  {
    /* separator */
    pBuf = create_iconlabel(NULL, NULL, WF_FREE_THEME);
    
    add_to_gui_list(ID_SEPARATOR , pBuf);
    h += pBuf->next->size.h;
    /* ------------------ */
    
    my_snprintf(cBuf, sizeof(cBuf), _("Zoom to : %s"), pCity->name );
    
    create_active_iconlabel( pBuf, pStr, cBuf, pCity, zoom_to_city_callback);
    
    set_wstate(pBuf , WS_NORMAL);
  
    add_to_gui_list(ID_LABEL , pBuf);
    
    w = MAX(w , pBuf->size.w);
    h += pBuf->size.h;
    /* ----------- */
    
    create_active_iconlabel(pBuf, pStr,
	    _("Change Production"), pCity, change_production_callback);
	    
    set_wstate(pBuf , WS_NORMAL);
  
    add_to_gui_list(ID_LABEL , pBuf);
    
    w = MAX(w , pBuf->size.w);
    h += pBuf->size.h;
    /* -------------- */
    
    create_active_iconlabel(pBuf, pStr,
	    _("Hurry production"), pCity, hurry_production_callback);
    
    set_wstate(pBuf , WS_NORMAL);
  
    add_to_gui_list(ID_LABEL , pBuf);
    
    w = MAX(w , pBuf->size.w);
    h += pBuf->size.h;
    /* ----------- */
    
    create_active_iconlabel(pBuf, pStr,
	    _("Change WorkList") , pCity, change_work_list_callback);
        
    pBuf->string16->forecol = *(get_game_colorRGB(COLOR_STD_DISABLED));
    
    /* set_wstate( pBuf , WS_NORMAL ); */
  
    add_to_gui_list( ID_LABEL , pBuf );
    
    w = MAX( w , pBuf->size.w );  
    h += pBuf->size.h;
    
    /* ----------- */

    create_active_iconlabel( pBuf, pStr,
	    _("Change C.M.A Settings") , pCity, cma_callback);

    pBuf->string16->forecol = *(get_game_colorRGB(COLOR_STD_DISABLED));
    
    /* set_wstate( pBuf , WS_NORMAL ); */
  
    add_to_gui_list(ID_LABEL , pBuf);
    
    w = MAX(w , pBuf->size.w);
    h += pBuf->size.h;
    
  }
  /* ---------- */
  
  if(pFocus_Unit && (pFocus_Unit->x != x || pFocus_Unit->y != y)) {
    /* separator */
    pBuf = create_iconlabel( NULL, NULL, WF_FREE_THEME);
    
    add_to_gui_list(ID_SEPARATOR , pBuf);
    h += pBuf->next->size.h;
    /* ------------------ */
        
    create_active_iconlabel(pBuf, pStr, _("Goto here"), pPos,
						    goto_here_callback);
    
    set_wstate(pBuf , WS_NORMAL);
        
    add_to_gui_list(MAX_ID - 1000 - pFocus_Unit->id, pBuf);
    
    w = MAX(w , pBuf->size.w);
    h += pBuf->size.h;
    /* ----------- */
    
    create_active_iconlabel(pBuf, pStr, _("Patrol here"), pPos,
						    patrol_here_callback);
    
    /*set_wstate(pBuf , WS_NORMAL);*/
    pBuf->string16->forecol = *(get_game_colorRGB(COLOR_STD_DISABLED));
    
    add_to_gui_list(MAX_ID - 1000 - pFocus_Unit->id, pBuf);
    
    w = MAX(w , pBuf->size.w);
    h += pBuf->size.h;
    /* ----------- */
    
    if(unit_flag(pFocus_Unit, F_SETTLERS)) {
      create_active_iconlabel(pBuf, pStr, _("Connect here"), pPos,
						    connect_here_callback);
    
      set_wstate(pBuf, WS_NORMAL);
  
      add_to_gui_list(ID_LABEL, pBuf);
    
      w = MAX(w , pBuf->size.w);
      h += pBuf->size.h;
      
    }
  }
  
  /* ---------- */
  if (n)
  {
    int i;
    struct unit *pUnit;
    struct unit_type *pUnitType;
    units_h = 0;  
    /* separator */
    pBuf = create_iconlabel(NULL, NULL, WF_FREE_THEME);
    
    add_to_gui_list(ID_SEPARATOR , pBuf);
    h += pBuf->next->size.h;
    /* ---------- */
    
    if (n > 1)
    {
      for(i=0; i<n; i++) {
        pUnit = unit_list_get(&pTile->units, i);
        pUnitType = unit_type(pUnit);
    
        my_snprintf(cBuf , sizeof(cBuf),
            _("Activate %s (%d / %d) %s(%d,%d,%d) %s"),
            pUnit->veteran ? _("Veteran") : "" ,
            pUnit->hp, pUnitType->hp,
            pUnitType->name,
            pUnitType->attack_strength,
            pUnitType->defense_strength,
            (pUnitType->move_rate / 3),
	    unit_activity_text(pUnit));
    
	create_active_iconlabel( pBuf, pStr,
	       cBuf,  NULL, adv_unit_select_callback);
        
        set_wstate(pBuf , WS_NORMAL);
	
        add_to_gui_list(MAX_ID - pUnit->id , pBuf);
    
        w = MAX(w , pBuf->size.w);
        units_h += pBuf->size.h;
    
    	if (!pAdvanced_Terrain_Dlg->pEndActiveWidgetList)
	{
	  pAdvanced_Terrain_Dlg->pEndActiveWidgetList = pBuf;
	}
    
        if (i > 9)
        {
          set_wflag(pBuf , WF_HIDDEN);
        }
        
      }
      pAdvanced_Terrain_Dlg->pBeginActiveWidgetList = pBuf;
      pAdvanced_Terrain_Dlg->pActiveWidgetList =
			      pAdvanced_Terrain_Dlg->pEndActiveWidgetList;
      
      if(n > 10)
      {
        units_h = 10 * pBuf->size.h;
       
        /* create up button */
        pBuf = create_themeicon_button(pTheme->UP_Icon, NULL, 0);
        clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);

        pBuf->action = up_adv_unit_select_dlg_callback;
        set_wstate(pBuf, WS_NORMAL);

        add_to_gui_list(ID_UNIT_SELLECT_DLG_UP_BUTTON, pBuf);
      
        /* create vsrollbar */
        pBuf = create_vertical(pTheme->Vertic, 50, WF_DRAW_THEME_TRANSPARENT);
       
        set_wstate(pBuf, WS_NORMAL);
        pBuf->action = vscroll_adv_unit_select_dlg_callback;

        add_to_gui_list(ID_UNIT_SELLECT_DLG_VSCROLLBAR, pBuf);

        /* create down button */
        pBuf = create_themeicon_button(pTheme->DOWN_Icon, NULL, 0);
       
        clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);

        pBuf->action = down_adv_unit_select_dlg_callback;
        set_wstate(pBuf, WS_NORMAL);

        add_to_gui_list(ID_UNIT_SELLECT_DLG_DOWN_BUTTON, pBuf);

        w += pBuf->size.w;
       
        pAdvanced_Terrain_Dlg->pScroll = MALLOC(sizeof(struct ScrollBar));
        pAdvanced_Terrain_Dlg->pScroll->active = 10;
        pAdvanced_Terrain_Dlg->pScroll->count = n;
      }
      
    }
    else
    {
      /* one unit - give orders */
      pUnit = unit_list_get(&pTile->units, 0);
      pUnitType = unit_type(pUnit);
      
      if (pCity)
      {
        my_snprintf(cBuf , sizeof(cBuf),
            _("Activate %s (%d / %d) %s(%d,%d,%d) %s"),
            pUnit->veteran ? _("Veteran") : "" ,
            pUnit->hp, pUnitType->hp,
            pUnitType->name,
            pUnitType->attack_strength,
            pUnitType->defense_strength,
            (pUnitType->move_rate / 3),
	    unit_activity_text(pUnit));
    
	create_active_iconlabel(pBuf, pStr,
	    		cBuf , NULL , adv_unit_select_callback);
	
        set_wstate(pBuf , WS_NORMAL);
	
        add_to_gui_list(MAX_ID - pUnit->id , pBuf);
    
        w = MAX(w , pBuf->size.w);
        units_h += pBuf->size.h;
      }
      /* ---------------- */
      my_snprintf(cBuf , sizeof(cBuf),
            _("View Civiliopedia entry for %s"), pUnitType->name );
    
      create_active_iconlabel(pBuf, pStr,
	    		cBuf , pUnitType , NULL);
      
      pBuf->string16->forecol = *(get_game_colorRGB(COLOR_STD_DISABLED));
      
      /* set_wstate( pBuf , WS_NORMAL ); */
      
      add_to_gui_list(ID_LABEL , pBuf);
    
      w = MAX(w , pBuf->size.w);
      units_h += pBuf->size.h;
      /* ---------------- */  
    }
    
  }
  /* ---------- */
  
  pAdvanced_Terrain_Dlg->pBeginWidgetList = pBuf;
  
  
  w += DOUBLE_FRAME_WH;
  
  h += units_h;
  
  
  if (canvas_x + NORMAL_TILE_WIDTH + w > Main.gui->w)
  {
    if (canvas_x - w >= 0)
    {
      pWindow->size.x = canvas_x - w;
    }
    else
    {
      pWindow->size.x = (Main.gui->w - w) / 2;
    }
  }
  else
  {
    pWindow->size.x = canvas_x + NORMAL_TILE_WIDTH;
  }
    
  if (canvas_y - NORMAL_TILE_HEIGHT + h > Main.gui->h)
  {
    if (canvas_y + NORMAL_TILE_HEIGHT - h >= 0)
    {
      pWindow->size.y = canvas_y + NORMAL_TILE_HEIGHT - h;
    }
    else
    {
      pWindow->size.y = (Main.gui->h - h) / 2;
    }
  }
  else
  {
    pWindow->size.y = canvas_y - NORMAL_TILE_HEIGHT;
  }
        
  resize_window(pWindow , NULL , NULL , w , h);
  
  w -= DOUBLE_FRAME_WH;
  
  if (n > 10)
  {
    units_h = pBuf->size.w;
  }
  else
  {
    units_h = 0;
  }
  
  /* exit button */
  pBuf = pWindow->prev;
  
  pBuf->size.x = pWindow->size.x + pWindow->size.w-pBuf->size.w-FRAME_WH-1;
  pBuf->size.y = pWindow->size.y;
  
  /* terrain info */
  pBuf = pBuf->prev;
  
  pBuf->size.x = pWindow->size.x + FRAME_WH;
  pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 2;
  pBuf->size.w = w;
  h = pBuf->size.h;
  
  area.x = 10;
  area.h = 2;
  
  pBuf = pBuf->prev;
  while(pBuf)
  {
    
    if (pBuf == pAdvanced_Terrain_Dlg->pEndActiveWidgetList)
    {
      w -= units_h;
    }
    
    pBuf->size.w = w;
    pBuf->size.x = pBuf->next->size.x;
    pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
    
    if (pBuf->ID == ID_SEPARATOR)
    {
      FREESURFACE(pBuf->theme);
      pBuf->size.h = h;
      pBuf->theme = create_surf(w , h , SDL_SWSURFACE);
    
      area.y = pBuf->size.h / 2 - 1;
      area.w = pBuf->size.w - 20;
      
      SDL_FillRect(pBuf->theme , &area, 64);
      SDL_SetColorKey(pBuf->theme , SDL_SRCCOLORKEY|SDL_RLEACCEL , 0x0);
    }
    
    if (pBuf == pAdvanced_Terrain_Dlg->pBeginWidgetList || 
        pBuf == pAdvanced_Terrain_Dlg->pBeginActiveWidgetList) break;
    pBuf = pBuf->prev;
  }
  
  if (n > 10)
  {
    /* up button */
    pBuf = pBuf->prev;
    
    pBuf->size.x = pWindow->size.x + pWindow->size.w - pBuf->size.w - FRAME_WH;
    pBuf->size.y = pAdvanced_Terrain_Dlg->pEndActiveWidgetList->size.y;
    
    pAdvanced_Terrain_Dlg->pScroll->min = pBuf->size.y + pBuf->size.h;
    
    /* scrollbar */
    pBuf = pBuf->prev;
    
    pBuf->size.x = pBuf->next->size.x;
    pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
    
    /* down button */
    pBuf = pBuf->prev;
    
    pBuf->size.x = pWindow->size.x + pWindow->size.w - pBuf->size.w - FRAME_WH;
    pBuf->size.y = pWindow->size.y + pWindow->size.h - FRAME_WH - pBuf->size.h;
    
    pAdvanced_Terrain_Dlg->pScroll->max = pBuf->size.y;
    
    /* 
       units scrollbar high
     */
    pBuf->next->size.h = scrollbar_size(pAdvanced_Terrain_Dlg->pScroll);
    
  }
  
  /* -------------------- */
  /* redraw */
  redraw_group(pAdvanced_Terrain_Dlg->pBeginWidgetList, pWindow, Main.gui,0);

  flush_rect(pWindow->size);
}

/* ====================================================================== */
/* ============================ CARAVAN DIALOG ========================== */
/* ====================================================================== */
static struct SMALL_DLG *pCaravan_Dlg = NULL;

static int caravan_dlg_window_callback( struct GUI *pWindow )
{
  return -1;
}

/****************************************************************
...
*****************************************************************/
static int caravan_establish_trade_callback(struct GUI *pWidget)
{
  struct packet_unit_request req;
  req.unit_id = MAX_ID - pWidget->ID;
  req.city_id = ((struct city *)pWidget->data)->id;
  req.name[0]='\0';
  popdown_caravan_dialog();
  send_packet_unit_request(&aconnection, &req, PACKET_UNIT_ESTABLISH_TRADE);
  return -1;
}


/****************************************************************
...
*****************************************************************/
static int caravan_help_build_wonder_callback(struct GUI *pWidget)
{
  struct packet_unit_request req;
  req.unit_id = MAX_ID - pWidget->ID;
  req.city_id = ((struct city *)pWidget->data)->id;
  req.name[0]='\0';
  popdown_caravan_dialog();
  send_packet_unit_request(&aconnection, &req, PACKET_UNIT_HELP_BUILD_WONDER);
  return -1;
}

static int exit_caravan_dlg_callback(struct GUI *pWidget)
{
  popdown_caravan_dialog();
  process_caravan_arrival(NULL);
  return -1;
}
  
static void popdown_caravan_dialog(void)
{
  if (pCaravan_Dlg) {
    popdown_window_group_dialog(pCaravan_Dlg->pBeginWidgetList,
				pCaravan_Dlg->pEndWidgetList, Main.gui);
    FREE(pCaravan_Dlg);
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
  struct GUI *pWindow = NULL, *pBuf = NULL;
  SDL_String16 *pStr;
  int w = 0, h , canvas_x , canvas_y;
  
  if (pCaravan_Dlg) {
    return;
  }

  pCaravan_Dlg = MALLOC(sizeof(struct SMALL_DLG));
  
  h = WINDOW_TILE_HIGH + 3 + FRAME_WH;
  
  get_canvas_xy(pUnit->x , pUnit->y , &canvas_x , &canvas_y);
  
  /* window */
  pStr = create_str16_from_char(_("Your Caravan Has Arrived") , 12);
  pStr->style |= TTF_STYLE_BOLD;
  
  pWindow = create_window(pStr , 10 , 10 , WF_DRAW_THEME_TRANSPARENT);
    
  pWindow->action = caravan_dlg_window_callback;
  set_wstate(pWindow , WS_NORMAL);
  w = MAX(w , pWindow->size.w);
  
  add_to_gui_list(ID_CARAVAN_DLG_WINDOW, pWindow);
  pCaravan_Dlg->pEndWidgetList = pWindow;
    
  /* ---------- */
  if (can_establish_trade_route(pHomecity, pDestcity))
  {
    create_active_iconlabel( pBuf , pStr ,
	    _("Establish Traderoute") , pDestcity,
			    caravan_establish_trade_callback );
    
    set_wstate(pBuf , WS_NORMAL);
  
    add_to_gui_list(MAX_ID - pUnit->id , pBuf);
    
    w = MAX(w , pBuf->size.w);
    h += pBuf->size.h;
  }
  
  /* ---------- */
  if (unit_can_help_build_wonder(pUnit, pDestcity)) {
        
    create_active_iconlabel(pBuf , pStr ,
	_("Help build Wonder"), pDestcity, caravan_help_build_wonder_callback);
    
    set_wstate(pBuf , WS_NORMAL);
  
    add_to_gui_list(MAX_ID - pUnit->id , pBuf);
    
    w = MAX(w , pBuf->size.w);
    h += pBuf->size.h;
  }
  /* ---------- */
  
  create_active_iconlabel(pBuf , pStr ,
	    _("Keep moving") , pDestcity, exit_caravan_dlg_callback);
			    
  set_wstate(pBuf , WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  
  add_to_gui_list(MAX_ID - pUnit->id , pBuf);
    
  w = MAX(w , pBuf->size.w);
  h += pBuf->size.h;
  /* ---------- */
  pCaravan_Dlg->pBeginWidgetList = pBuf;
  
  /* setup window size and start position */
  
  pWindow->size.w = w + DOUBLE_FRAME_WH;
  pWindow->size.h = h;
  
  if (canvas_x + NORMAL_TILE_WIDTH + (w + DOUBLE_FRAME_WH) > Main.screen->w)
  {
    if (canvas_x - w >= 0)
    {
      pWindow->size.x = canvas_x - (w + DOUBLE_FRAME_WH);
    }
    else
    {
      pWindow->size.x = (Main.screen->w - (w + DOUBLE_FRAME_WH)) / 2;
    }
  }
  else
  {
    pWindow->size.x = canvas_x + NORMAL_TILE_WIDTH;
  }
    
  if (canvas_y - NORMAL_TILE_HEIGHT + h > Main.screen->h)
  {
    if (canvas_y + NORMAL_TILE_HEIGHT - h >= 0)
    {
      pWindow->size.y = canvas_y + NORMAL_TILE_HEIGHT - h;
    }
    else
    {
      pWindow->size.y = (Main.screen->h - h) / 2;
    }
  }
  else
  {
    pWindow->size.y = canvas_y - NORMAL_TILE_HEIGHT;
  }
        
  resize_window(pWindow , NULL , NULL , pWindow->size.w , h);
  
  /* setup widget size and start position */
    
  pBuf = pWindow->prev;
    
  pBuf->size.x = pWindow->size.x + FRAME_WH;
  pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 2;
  pBuf->size.w = w;
  
  pBuf = pBuf->prev;
  while(pBuf)
  {
    pBuf->size.w = w;
    pBuf->size.x = pBuf->next->size.x;
    pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
    if (pBuf == pCaravan_Dlg->pBeginWidgetList) break;
    pBuf = pBuf->prev;
  }
  /* --------------------- */
  /* redraw */
  redraw_group(pCaravan_Dlg->pBeginWidgetList, pWindow, Main.gui,0);

  flush_rect(pWindow->size);
}

/**************************************************************************
  Is there currently a caravan dialog open?  This is important if there
  can be only one such dialog at a time; otherwise return FALSE.
**************************************************************************/
bool caravan_dialog_is_open(void)
{
  return pCaravan_Dlg != NULL;
}

/* ====================================================================== */
/* ============================ DIPLOMAT DIALOG ========================= */
/* ====================================================================== */
static struct SMALL_DLG *pDiplomat_Dlg = NULL;

/****************************************************************
...
*****************************************************************/
static int diplomat_dlg_window_callback( struct GUI *pWindow )
{
  return -1;
}

/****************************************************************
...
*****************************************************************/
static int diplomat_embassy_callback( struct GUI *pWidget )
{
  struct city *pCity = ((struct city *)pWidget->data);
  int id = MAX_ID - pWidget->ID;
  
  popdown_diplomat_dialog();
  if(pCity && find_unit_by_id(id)) { 
    struct packet_diplomat_action req;

    req.action_type = DIPLOMAT_EMBASSY;
    req.diplomat_id = id;
    req.target_id = pCity->id;

    send_packet_diplomat_action(&aconnection, &req);
  }

  process_diplomat_arrival(NULL, 0);
  return -1;
}

/****************************************************************
...
*****************************************************************/
static int diplomat_investigate_callback( struct GUI *pWidget )
{
  struct city *pCity = ((struct city *)pWidget->data);
  int id = MAX_ID - pWidget->ID;

  popdown_diplomat_dialog();
  if(pCity && find_unit_by_id(id)) { 
    struct packet_diplomat_action req;

    req.action_type = DIPLOMAT_INVESTIGATE;
    req.diplomat_id = id;
    req.target_id = pCity->id;

    send_packet_diplomat_action(&aconnection, &req);
  }

  process_diplomat_arrival(NULL, 0);
  return -1;
}

/****************************************************************
...
*****************************************************************/
static int spy_poison_callback( struct GUI *pWidget )
{
  struct city *pCity = ((struct city *)pWidget->data);
  int id = MAX_ID - pWidget->ID;

  popdown_diplomat_dialog();
  if(pCity && find_unit_by_id(id)) { 
    struct packet_diplomat_action req;

    req.action_type = SPY_POISON;
    req.diplomat_id = id;
    req.target_id = pCity->id;

    send_packet_diplomat_action(&aconnection, &req);
  }

  process_diplomat_arrival(NULL, 0);
  return -1;
}

/****************************************************************
 Requests up-to-date list of improvements, the return of
 which will trigger the popup_sabotage_dialog() function.
*****************************************************************/
static int spy_sabotage_request(struct GUI *pWidget)
{
  struct city *pCity = ((struct city *)pWidget->data);
  int id = MAX_ID - pWidget->ID;
  
  popdown_diplomat_dialog();
  
  if(pCity && find_unit_by_id(id)) {
    struct packet_diplomat_action req;

    req.action_type = SPY_GET_SABOTAGE_LIST;
    req.diplomat_id = id;
    req.target_id = pCity->id;

    send_packet_diplomat_action(&aconnection, &req);
  }
  return -1;
}

/****************************************************************
...
*****************************************************************/
static int diplomat_sabotage_callback( struct GUI *pWidget )
{
  struct city *pCity = ((struct city *)pWidget->data);
  int id = MAX_ID - pWidget->ID;
  
  popdown_diplomat_dialog();
  if(pCity && find_unit_by_id(id)) { 
    struct packet_diplomat_action req;

    req.action_type = DIPLOMAT_SABOTAGE;
    req.diplomat_id = id;
    req.target_id = pCity->id;
    req.value = -1;

    send_packet_diplomat_action(&aconnection, &req);
  }

  process_diplomat_arrival(NULL, 0);
  return -1;
}
/* --------------------------------------------------------- */

static int spy_steal_dlg_window_callback(struct GUI *pWidget)
{
  return -1;
}

static int exit_spy_steal_dlg_callback(struct GUI *pWidget)
{
  popdown_diplomat_dialog();
  process_diplomat_arrival(NULL, 0);
  return -1;  
}

static int spy_steal_callback(struct GUI *pWidget)
{
  int steal_advance = MAX_ID - pWidget->ID;
  int diplomat_target_id = ((struct map_position *)pWidget->data)->x;
  int diplomat_id = ((struct map_position *)pWidget->data)->y;
    
  popdown_diplomat_dialog();
  
  if(find_unit_by_id(diplomat_id) && 
     find_city_by_id(diplomat_target_id)) { 
    struct packet_diplomat_action req;
    
    req.action_type = DIPLOMAT_STEAL;
    req.value = steal_advance;
    req.diplomat_id = diplomat_id;
    req.target_id = diplomat_target_id;

    send_packet_diplomat_action(&aconnection, &req);
  }

  process_diplomat_arrival(NULL, 0);
  return -1;
}

/****************************************************************
...
*****************************************************************/
static int spy_steal_popup(struct GUI *pWidget)
{
  struct city *pVcity = ((struct city *)pWidget->data);
  int id = MAX_ID - pWidget->ID;
  struct player *pVictim = NULL;
  void *wdata;  
  struct GUI *pBuf = NULL; /* FIXME: possibly uninitialized */
  struct GUI *pWindow;
  SDL_String16 *pStr;
  SDL_Surface *pSurf;
  int i, count, w = 0, h;
  
  popdown_diplomat_dialog();
  
  if(pVcity)
  {
    pVictim = city_owner(pVcity);
    /*get_canvas_xy( pVcity->x , pVcity->y , &canvas_x , &canvas_y );*/
  }
  
  if (pDiplomat_Dlg || !pVictim) {
    return 1;
  }
  
  count = 0;
  for(i=A_FIRST; i<game.num_tech_types; i++) {
    if(get_invention(pVictim, i)==TECH_KNOWN && 
     (get_invention(game.player_ptr, i)==TECH_UNKNOWN || 
      get_invention(game.player_ptr, i)==TECH_REACHABLE)) {
	w = i;
	count++;
      }
  }
  
  if(!count) {
    return 1;
  }
  
  if ( count == 1 )
  {
    /* if there is only 1 tech to steal then 
       send steal order to it */
    struct packet_diplomat_action req;
    
    req.action_type = DIPLOMAT_STEAL;
    req.value = w;
    req.diplomat_id = id;
    req.target_id = pVcity->id;

    send_packet_diplomat_action(&aconnection, &req);
    return -1;
  }
  
  w = 0;
  
  /* this is hack becouse I need struct that carry 2 int */
  wdata = (void *)MALLOC(sizeof(struct map_position));
  ((struct map_position *)wdata)->x = pVcity->id;
  ((struct map_position *)wdata)->y = id;/* spy id */
  
  count = 0;
  
  pDiplomat_Dlg = MALLOC(sizeof(struct SMALL_DLG));
  
  h = WINDOW_TILE_HIGH + 3 + FRAME_WH;
  
  pStr = create_str16_from_char(_("Select Advance to Steal"), 12);
  pStr->style |= TTF_STYLE_BOLD;

  pWindow = create_window(pStr, 10, 10, 0);
  
  pWindow->action = spy_steal_dlg_window_callback;
  set_wstate( pWindow , WS_NORMAL );
  w = MAX(0, pWindow->size.w + 8);
  
  add_to_gui_list(ID_CARAVAN_DLG_WINDOW, pWindow);
  pDiplomat_Dlg->pEndWidgetList = pWindow;
  /* ------------------ */
  
  /* exit button */
  pBuf = create_themeicon(ResizeSurface( pTheme->CANCEL_Icon ,
			  pTheme->CANCEL_Icon->w - 4,
			  pTheme->CANCEL_Icon->h - 4, 1) ,
  			  (WF_FREE_THEME|WF_DRAW_THEME_TRANSPARENT));
  SDL_SetColorKey( pBuf->theme ,
	  SDL_SRCCOLORKEY|SDL_RLEACCEL , get_first_pixel(pBuf->theme));
  
  w += pBuf->size.w + 10;
  pBuf->action = exit_spy_steal_dlg_callback;
  set_wstate(pBuf, WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  
  add_to_gui_list(ID_TERRAIN_ADV_DLG_EXIT_BUTTON, pBuf);
  /* ------------------ */
  
  pStr = create_string16(NULL, 10);
  pStr->style |= (TTF_STYLE_BOLD | SF_CENTER);  
  
  count = 0;
  for(i=A_FIRST; i<game.num_tech_types; i++) {
    if(get_invention(pVictim, i)==TECH_KNOWN && 
     (get_invention(game.player_ptr, i)==TECH_UNKNOWN || 
      get_invention(game.player_ptr, i)==TECH_REACHABLE)) {

      pSurf = create_sellect_tech_icon( pStr, i );
	
      /* ------------------------------ */
      pBuf = create_icon2(pSurf,(WF_FREE_THEME | WF_DRAW_THEME_TRANSPARENT));

      set_wstate(pBuf, WS_NORMAL);
      pBuf->action = spy_steal_callback;
      pBuf->data = wdata;
	
      add_to_gui_list(MAX_ID - i, pBuf);
      count++;
    }
  }
  

  if(count > 0) {
    /* get spy tech */
    i = unit_type(find_unit_by_id(id))->tech_requirement;
    pStr->text= convert_to_utf16(_("At Spy's Discretion"));
      
    pSurf = create_sellect_tech_icon( pStr, i );
	
    /* ------------------------------ */
    pBuf = create_icon2(pSurf, 
    	(WF_FREE_THEME | WF_DRAW_THEME_TRANSPARENT| WF_FREE_DATA));
    set_wstate(pBuf, WS_NORMAL);
    pBuf->action = spy_steal_callback;
    pBuf->data = wdata;
    
    add_to_gui_list(MAX_ID - game.num_tech_types, pBuf);
    count++;
  }

  pDiplomat_Dlg->pBeginWidgetList = pBuf;

  if ( count > 10 )
  {
    i = 6;
    if ( count > 12 ) {
      freelog( LOG_FATAL , "If you see this msg please contact me on bursig@poczta.fm and give me this : Tech12 Err");
    }
  }
  else
  {
    if ( count > 8 )
    {
      i = 5;
    }
    else
    {
      if ( count > 6 )
      {
        i = 4;
      }
      else
      {
	i = count;
      }
    }
  }
  
  if (count > i) {
    count = (count + (i-1)) / i;
    w = MAX( w , (i * 102 + 2 + DOUBLE_FRAME_WH));
    h += count * 202;
  } else {
    w = MAX(w , (count * 102 + 2 + DOUBLE_FRAME_WH));
    h += 202;
  }

  pWindow->size.x = (Main.screen->w - w) / 2;
  pWindow->size.y = (Main.screen->h - h) / 2;

  pSurf = get_logo_gfx();
  if(resize_window(pWindow, pSurf, NULL, w, h)) {
    FREESURFACE(pSurf);
  }

  /* exit button */
  pBuf = pWindow->prev;
  pBuf->size.x = pWindow->size.x + pWindow->size.w-pBuf->size.w-FRAME_WH-1;
  pBuf->size.y = pWindow->size.y;
  
  /* techs widgets*/
  pBuf = pBuf->prev;
  pBuf->size.x = pWindow->size.x + FRAME_WH;
  pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 1;
  pBuf = pBuf->prev;

  w = 0;
  while (pBuf) {
    pBuf->size.x = pBuf->next->size.x + pBuf->next->size.w - 2;
    pBuf->size.y = pBuf->next->size.y;
    w++;

    if (w == i) {
      w = 0;
      pBuf->size.x = pWindow->size.x + FRAME_WH;
      pBuf->size.y += pBuf->size.h - 2;
    }

    if (pBuf == pDiplomat_Dlg->pBeginWidgetList) {
      break;
    }
    pBuf = pBuf->prev;
  }

  /* ----------------------- */
  redraw_group(pDiplomat_Dlg->pBeginWidgetList, pWindow, Main.gui,0);

  flush_rect(pWindow->size);

  FREESTRING16(pStr);
  
  return -1;
}

/****************************************************************
...
*****************************************************************/
static int diplomat_steal_callback(struct GUI *pWidget)
{
  struct city *pCity = ((struct city *)pWidget->data);
  int id = MAX_ID - pWidget->ID;
  
  popdown_diplomat_dialog();
  if(pCity && find_unit_by_id(id)) { 
    struct packet_diplomat_action req;

    req.action_type = DIPLOMAT_STEAL;
    req.diplomat_id = id;
    req.target_id = pCity->id;
    req.value=0;

    send_packet_diplomat_action(&aconnection, &req);
  }

  process_diplomat_arrival(NULL, 0);
  return -1;
}

/****************************************************************
...  Ask the server how much the revolt is going to cost us
*****************************************************************/
static int diplomat_incite_callback(struct GUI *pWidget)
{
  struct city *pCity = ((struct city *)pWidget->data);
  int id = MAX_ID - pWidget->ID;
  
  popdown_diplomat_dialog();
  if(pCity && find_unit_by_id(id)) { 
    struct packet_generic_integer packet;
    packet.value = pCity->id;
    send_packet_generic_integer(&aconnection, PACKET_INCITE_INQ, &packet);
  }
  
  return -1;
}

/****************************************************************
  Callback from diplomat/spy dialog for "keep moving".
  (This should only occur when entering allied city.)
*****************************************************************/
static int diplomat_keep_moving_callback(struct GUI *pWidget)
{
  struct unit *pUnit = find_unit_by_id(MAX_ID - pWidget->ID);
  struct city *pCity = ((struct city *)pWidget->data);
  
  popdown_diplomat_dialog();
  
  if(pUnit && pCity && !same_pos(pUnit->x, pUnit->y, pCity->x, pCity->y)) {
    struct packet_diplomat_action req;
    req.action_type = DIPLOMAT_MOVE;
    req.diplomat_id = pUnit->id;
    req.target_id = pCity->id;
    send_packet_diplomat_action(&aconnection, &req);
  }
  process_diplomat_arrival(NULL, 0);
  
  return -1;
}

/****************************************************************
...  Ask the server how much the bribe is
*****************************************************************/
static int diplomat_bribe_callback(struct GUI *pWidget)
{
  struct packet_generic_integer packet;
  struct unit *pTunit = ((struct unit *)pWidget->data);
  int id = MAX_ID - pWidget->ID;
  
  popdown_diplomat_dialog();
  
  if(find_unit_by_id(id) && pTunit ) { 
    packet.value = pTunit->id;
    send_packet_generic_integer(&aconnection, PACKET_INCITE_INQ, &packet);
  }
   
  return -1;
}

/****************************************************************
...
*****************************************************************/
static int spy_sabotage_unit_callback(struct GUI *pWidget)
{
  
  struct packet_diplomat_action req;
  
  req.action_type = SPY_SABOTAGE_UNIT;
  req.diplomat_id = MAX_ID - pWidget->ID;
  req.target_id = ((struct unit *)pWidget->data)->id;
  
  popdown_diplomat_dialog();
  
  send_packet_diplomat_action(&aconnection, &req);
  
  return -1;
}

/****************************************************************
...
*****************************************************************/
static int diplomat_close_callback(struct GUI *pWidget)
{
  popdown_diplomat_dialog();
  process_diplomat_arrival(NULL, 0);
  return -1;
}

/**************************************************************************
  Popdown a dialog giving a diplomatic unit some options when moving into
  the target tile.
**************************************************************************/
static void popdown_diplomat_dialog(void)
{
  if (pDiplomat_Dlg) {
    popdown_window_group_dialog(pDiplomat_Dlg->pBeginWidgetList,
				pDiplomat_Dlg->pEndWidgetList, Main.gui);
    FREE( pDiplomat_Dlg );
    flush_dirty();
  }
}

/**************************************************************************
  Popup a dialog giving a diplomatic unit some options when moving into
  the target tile.
**************************************************************************/
void popup_diplomat_dialog(struct unit *pUnit, int target_x, int target_y)
{
  struct GUI *pWindow = NULL, *pBuf = NULL;
  SDL_String16 *pStr;
  struct city *pCity;
  struct unit *pTunit;
  int w = 0, h , canvas_x , canvas_y;
  bool spy;
  
  if (pDiplomat_Dlg) {
    return;
  }

  pCity = map_get_city(target_x, target_y);
  spy = unit_flag(pUnit, F_SPY);
  
  pDiplomat_Dlg = MALLOC(sizeof(struct SMALL_DLG));
  
  h = WINDOW_TILE_HIGH + 3 + FRAME_WH;
  
  get_canvas_xy(pUnit->x , pUnit->y , &canvas_x , &canvas_y);
  
  /* window */
  if (pCity)
  {
    if(spy) {
      pStr = create_str16_from_char(_("Choose Your Spy's Strategy") , 12);
    }
    else
    {
      pStr = create_str16_from_char(_("Choose Your Diplomat's Strategy"), 12);
    }
  }
  else
  {
    pStr = create_str16_from_char(_("Subvert Enemy Unit"), 12);
  }
  
  pStr->style |= TTF_STYLE_BOLD;
  
  pWindow = create_window(pStr , 10 , 10 , WF_DRAW_THEME_TRANSPARENT);
    
  pWindow->action = diplomat_dlg_window_callback;
  set_wstate(pWindow , WS_NORMAL);
  w = MAX(w , pWindow->size.w + 8);
  
  add_to_gui_list(ID_CARAVAN_DLG_WINDOW, pWindow);
  pDiplomat_Dlg->pEndWidgetList = pWindow;
    
  /* ---------- */
  if((pCity))
  {
    /* Spy/Diplomat acting against a city */
    
    /* -------------- */
    if (diplomat_can_do_action(pUnit, DIPLOMAT_EMBASSY, target_x, target_y))
    {
	
      create_active_iconlabel(pBuf , pStr ,
	    _("Establish Embassy") , pCity , diplomat_embassy_callback);
	
      set_wstate(pBuf , WS_NORMAL);
  
      add_to_gui_list(MAX_ID - pUnit->id , pBuf);
    
      w = MAX(w , pBuf->size.w);
      h += pBuf->size.h;
    }
  
    /* ---------- */
    if (diplomat_can_do_action(pUnit, DIPLOMAT_INVESTIGATE, target_x, target_y)) {
    
      create_active_iconlabel(pBuf, pStr,
	    spy ? _("Investigate City (free)") : _("Investigate City"), pCity,
			    diplomat_investigate_callback );
    
      set_wstate(pBuf , WS_NORMAL);
  
      add_to_gui_list(MAX_ID - pUnit->id , pBuf);
    
      w = MAX(w , pBuf->size.w);
      h += pBuf->size.h;
    }
  
    /* ---------- */
    if (spy && diplomat_can_do_action(pUnit, SPY_POISON, target_x, target_y)) {
    
      create_active_iconlabel(pBuf , pStr,
	    _("Poison City") , pCity, spy_poison_callback);
    
      set_wstate(pBuf , WS_NORMAL);
  
      add_to_gui_list(MAX_ID - pUnit->id , pBuf);
    
      w = MAX(w , pBuf->size.w);
      h += pBuf->size.h;
    }    
    /* ---------- */
    if (diplomat_can_do_action(pUnit, DIPLOMAT_SABOTAGE, target_x, target_y)) {
    
      create_active_iconlabel(pBuf , pStr ,
	    _("Sabotage City"), pCity ,
		spy ? spy_sabotage_request : diplomat_sabotage_callback);
    
      set_wstate(pBuf , WS_NORMAL);
  
      add_to_gui_list(MAX_ID - pUnit->id , pBuf);
    
      w = MAX(w , pBuf->size.w);
      h += pBuf->size.h;
    }
  
    /* ---------- */
    if (diplomat_can_do_action(pUnit, DIPLOMAT_STEAL, target_x, target_y)) {
    
      create_active_iconlabel( pBuf , pStr ,
	    _("Steal Technology") , pCity ,
		spy ? spy_steal_popup : diplomat_steal_callback );
    
      set_wstate(pBuf , WS_NORMAL);
  
      add_to_gui_list(MAX_ID - pUnit->id , pBuf);
    
      w = MAX(w , pBuf->size.w);
      h += pBuf->size.h;
    }
      
    /* ---------- */
    if (diplomat_can_do_action(pUnit, DIPLOMAT_INCITE, target_x, target_y)) {
    
      create_active_iconlabel( pBuf , pStr ,
	    _("Incite a Revolt") , pCity , diplomat_incite_callback );
    
      set_wstate(pBuf , WS_NORMAL);
  
      add_to_gui_list(MAX_ID - pUnit->id , pBuf);
    
      w = MAX(w , pBuf->size.w);
      h += pBuf->size.h;
    }
      
    /* ---------- */
    if (diplomat_can_do_action(pUnit, DIPLOMAT_MOVE, target_x, target_y)) {
    
      create_active_iconlabel(pBuf , pStr ,
	    _("Keep moving") , pCity , diplomat_keep_moving_callback);
    
      set_wstate(pBuf , WS_NORMAL);
  
      add_to_gui_list(MAX_ID - pUnit->id , pBuf);
    
      w = MAX(w , pBuf->size.w);
      h += pBuf->size.h;
    }
  }
  else
  {
    if((pTunit=unit_list_get(&map_get_tile(target_x, target_y)->units, 0))){
       /* Spy/Diplomat acting against a unit */ 
      /* ---------- */
      if (diplomat_can_do_action(pUnit, DIPLOMAT_BRIBE, target_x, target_y)) {
    
        create_active_iconlabel(pBuf , pStr ,
	    _("Bribe Enemy Unit") , pTunit , diplomat_bribe_callback);
    
        set_wstate(pBuf , WS_NORMAL);
  
        add_to_gui_list(MAX_ID - pUnit->id , pBuf);
    
        w = MAX(w , pBuf->size.w);
        h += pBuf->size.h;
      }
      
      /* ---------- */
      if (diplomat_can_do_action(pUnit, SPY_SABOTAGE_UNIT, target_x, target_y)) {
    
        create_active_iconlabel( pBuf , pStr ,
	    _("Sabotage Enemy Unit") , pTunit , spy_sabotage_unit_callback );
    
        set_wstate(pBuf , WS_NORMAL);
  
        add_to_gui_list(MAX_ID - pUnit->id , pBuf);
    
        w = MAX(w , pBuf->size.w);
        h += pBuf->size.h;
      }
    }
  }
  /* ---------- */
  
  create_active_iconlabel(pBuf , pStr ,
	    _("Cancel") , NULL , diplomat_close_callback);
    
  set_wstate(pBuf , WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  
  add_to_gui_list(ID_LABEL , pBuf);
    
  w = MAX(w , pBuf->size.w);
  h += pBuf->size.h;
  /* ---------- */
  pDiplomat_Dlg->pBeginWidgetList = pBuf;
  
  /* setup window size and start position */
  
  pWindow->size.w = w + DOUBLE_FRAME_WH;
  pWindow->size.h = h;
  
  if (canvas_x + NORMAL_TILE_WIDTH + (w + DOUBLE_FRAME_WH) > Main.gui->w)
  {
    if (canvas_x - w >= 0)
    {
      pWindow->size.x = canvas_x - (w + DOUBLE_FRAME_WH);
    }
    else
    {
      pWindow->size.x = (Main.screen->w - (w + DOUBLE_FRAME_WH)) / 2;
    }
  }
  else
  {
    pWindow->size.x = canvas_x + NORMAL_TILE_WIDTH;
  }
    
  if ( canvas_y - NORMAL_TILE_HEIGHT + h > Main.screen->h )
  {
    if ( canvas_y + NORMAL_TILE_HEIGHT - h >= 0 )
    {
      pWindow->size.y = canvas_y + NORMAL_TILE_HEIGHT - h;
    }
    else
    {
      pWindow->size.y = (Main.screen->h - h) / 2;
    }
  }
  else
  {
    pWindow->size.y = canvas_y - NORMAL_TILE_HEIGHT;
  }
        
  resize_window(pWindow , NULL , NULL , pWindow->size.w , h);
  
  /* setup widget size and start position */
    
  pBuf = pWindow->prev;
    
  pBuf->size.x = pWindow->size.x + FRAME_WH;
  pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 2;
  pBuf->size.w = w;
    
  pBuf = pBuf->prev;
  while(pBuf)
  {
    pBuf->size.w = w;
    pBuf->size.x = pBuf->next->size.x;
    pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
    if (pBuf == pDiplomat_Dlg->pBeginWidgetList) break;
    pBuf = pBuf->prev;
  }
  /* --------------------- */
  /* redraw */
  redraw_group(pDiplomat_Dlg->pBeginWidgetList, pWindow, Main.gui, 0);

  flush_rect(pWindow->size);
  
}

/**************************************************************************
  Return whether a diplomat dialog is open.  This is important if there
  can be only one such dialog at a time; otherwise return FALSE.
**************************************************************************/
bool diplomat_dialog_is_open(void)
{
  return pDiplomat_Dlg != NULL;
}

/* ====================================================================== */
/* ============================ SABOTAGE DIALOG ========================= */
/* ====================================================================== */
/* Sabotage Dlg use ADVANCED_TERRAIN_DLG struct and funct*/

static int sabotage_impr_callback(struct GUI *pWidget)
{
  int sabotage_improvement = MAX_ID - pWidget->ID;
  int diplomat_target_id = ((struct map_position *)pWidget->data)->x;
  int diplomat_id = ((struct map_position *)pWidget->data)->y;
    
  popdown_advanced_terrain_dialog();
  
  if(sabotage_improvement == 1000)
  {
    sabotage_improvement = -1;
  }
  
  if(find_unit_by_id(diplomat_id) && 
     find_city_by_id(diplomat_target_id)) { 
    struct packet_diplomat_action req;
    
    req.action_type=DIPLOMAT_SABOTAGE;
    req.value=sabotage_improvement+1;
    req.diplomat_id=diplomat_id;
    req.target_id=diplomat_target_id;

    send_packet_diplomat_action(&aconnection, &req);
  }

  process_diplomat_arrival(NULL, 0);
  return -1;
}

/****************************************************************
 Pops-up the Spy sabotage dialog, upon return of list of
 available improvements requested by the above function.
*****************************************************************/
void popup_sabotage_dialog(struct city *pCity)
{
  struct GUI *pWindow = NULL, *pBuf = NULL , *pLast = NULL;
  void *wdata;
  struct unit *pUnit = get_unit_in_focus();
  SDL_String16 *pStr;
  SDL_Rect area;
  int n , w = 0, h , imp_h = 0, canvas_x , canvas_y ;
  
  if (pAdvanced_Terrain_Dlg || !pUnit || !unit_flag(pUnit, F_SPY)) {
    return;
  }
  
  h = WINDOW_TILE_HIGH + 3 + FRAME_WH;
  
  get_canvas_xy(pUnit->x , pUnit->y , &canvas_x , &canvas_y);
  
  pAdvanced_Terrain_Dlg = MALLOC(sizeof(struct ADVANCED_DLG));
  
  /* this is hack becouse I need struct that carry 2 int */
  wdata = (void *)MALLOC(sizeof(struct map_position));
  ((struct map_position *)wdata)->x = pCity->id;
  ((struct map_position *)wdata)->y = pUnit->id;/* spy id */
  
  
  pStr = create_str16_from_char(_("Select Improvement to Sabotage") , 12);
  pStr->style |= TTF_STYLE_BOLD;
  
  pWindow = create_window(pStr , 10 , 10 , WF_DRAW_THEME_TRANSPARENT);
    
  pWindow->action = advanced_terrain_window_dlg_callback;
  set_wstate(pWindow , WS_NORMAL);
  w = MAX(w , pWindow->size.w);
  
  add_to_gui_list(ID_TERRAIN_ADV_DLG_WINDOW, pWindow);
  pAdvanced_Terrain_Dlg->pEndWidgetList = pWindow;
  /* ---------- */
  /* exit button */
  pBuf = create_themeicon(ResizeSurface( pTheme->CANCEL_Icon ,
			  pTheme->CANCEL_Icon->w - 4,
			  pTheme->CANCEL_Icon->h - 4, 1) ,
  			  (WF_FREE_THEME|WF_DRAW_THEME_TRANSPARENT));
  SDL_SetColorKey(pBuf->theme ,
	  SDL_SRCCOLORKEY|SDL_RLEACCEL , get_first_pixel(pBuf->theme));
  
  w += pBuf->size.w + 10;
  pBuf->action = exit_advanced_terrain_dlg_callback;
  set_wstate(pBuf, WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  
  add_to_gui_list(ID_TERRAIN_ADV_DLG_EXIT_BUTTON, pBuf);
  /* ---------- */
  
  create_active_iconlabel(pBuf, pStr,
	    _("City Production"), wdata, sabotage_impr_callback);
	    
  set_wstate(pBuf , WS_NORMAL);
  
  add_to_gui_list(MAX_ID - 1000 , pBuf);
    
  w = MAX(w , pBuf->size.w);
  h += pBuf->size.h;

  /* separator */
  pBuf = create_iconlabel(NULL, NULL, WF_FREE_THEME);
    
  add_to_gui_list(ID_SEPARATOR , pBuf);
  h += pBuf->next->size.h;
  /* ------------------ */
  n = 0;
  built_impr_iterate(pCity, imp) {
    if(imp != B_PALACE && !is_wonder(imp)) {
      
      create_active_iconlabel(pBuf, pStr,
	      (char *) get_impr_name_ex(pCity, imp), wdata,
				      sabotage_impr_callback);
    
      set_wstate(pBuf , WS_NORMAL);
  
      add_to_gui_list(MAX_ID - imp , pBuf);
    
      w = MAX(w , pBuf->size.w);
      imp_h += pBuf->size.h;
      
      if (!pAdvanced_Terrain_Dlg->pEndActiveWidgetList)
      {
	pAdvanced_Terrain_Dlg->pEndActiveWidgetList = pBuf;
      }
    
      if (imp > 9)
      {
        set_wflag(pBuf , WF_HIDDEN);
      }
      
      n++;    
      /* ----------- */
    }  
  } built_impr_iterate_end;
  pAdvanced_Terrain_Dlg->pBeginActiveWidgetList = pBuf;
  
  if(n) {
    /* separator */
    pBuf = create_iconlabel(NULL, NULL, WF_FREE_THEME);
    
    add_to_gui_list(ID_SEPARATOR , pBuf);
    h += pBuf->next->size.h;
  /* ------------------ */
  }
  
  create_active_iconlabel(pBuf, pStr, _("At Spy's Discretion"),
			  wdata, sabotage_impr_callback);
    
  set_wstate(pBuf , WS_NORMAL);
  
  add_to_gui_list(MAX_ID - B_LAST , pBuf);
    
  w = MAX(w , pBuf->size.w);
  h += pBuf->size.h;
  /* ----------- */
  pLast = pBuf;
  pAdvanced_Terrain_Dlg->pActiveWidgetList =
			      pAdvanced_Terrain_Dlg->pEndActiveWidgetList;
  
  /* ---------- */
  if (n > 10)
  {
    imp_h = 10 * pBuf->size.h;
       
    /* create up button */
    pBuf = create_themeicon_button(pTheme->UP_Icon, NULL, 0);
    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);

    pBuf->action = up_adv_unit_select_dlg_callback;
    set_wstate(pBuf, WS_NORMAL);

    add_to_gui_list(ID_UNIT_SELLECT_DLG_UP_BUTTON, pBuf);
      
    /* create vsrollbar */
    pBuf = create_vertical(pTheme->Vertic, 50, WF_DRAW_THEME_TRANSPARENT);
       
    set_wstate(pBuf, WS_NORMAL);
    pBuf->action = vscroll_adv_unit_select_dlg_callback;

    add_to_gui_list(ID_UNIT_SELLECT_DLG_VSCROLLBAR, pBuf);

    /* create down button */
    pBuf = create_themeicon_button(pTheme->DOWN_Icon, NULL, 0);
       
    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);

    pBuf->action = down_adv_unit_select_dlg_callback;
    set_wstate(pBuf, WS_NORMAL);
    add_to_gui_list(ID_UNIT_SELLECT_DLG_DOWN_BUTTON, pBuf);

    w += pBuf->size.w;
       
    pAdvanced_Terrain_Dlg->pScroll = MALLOC(sizeof(struct ScrollBar));
    pAdvanced_Terrain_Dlg->pScroll->active = 10;
    pAdvanced_Terrain_Dlg->pScroll->count = n;
   
  }
  /* ---------- */
  
  pAdvanced_Terrain_Dlg->pBeginWidgetList = pBuf;
  
  
  w += DOUBLE_FRAME_WH;
  
  h += imp_h;
  
  
  if (canvas_x + NORMAL_TILE_WIDTH + w > Main.gui->w)
  {
    if (canvas_x - w >= 0)
    {
      pWindow->size.x = canvas_x - w;
    }
    else
    {
      pWindow->size.x = (Main.gui->w - w) / 2;
    }
  }
  else
  {
    pWindow->size.x = canvas_x + NORMAL_TILE_WIDTH;
  }
    
  if (canvas_y - NORMAL_TILE_HEIGHT + h > Main.gui->h)
  {
    if (canvas_y + NORMAL_TILE_HEIGHT - h >= 0)
    {
      pWindow->size.y = canvas_y + NORMAL_TILE_HEIGHT - h;
    }
    else
    {
      pWindow->size.y = (Main.screen->h - h) / 2;
    }
  }
  else
  {
    pWindow->size.y = canvas_y - NORMAL_TILE_HEIGHT;
  }
        
  resize_window(pWindow , NULL , NULL , w , h);
  
  w -= DOUBLE_FRAME_WH;
  
  if (n > 10)
  {
    imp_h = pBuf->size.w;
  }
  else
  {
    imp_h = 0;
  }
  
  /* exit button */
  pBuf = pWindow->prev;
  
  pBuf->size.x = pWindow->size.x + pWindow->size.w-pBuf->size.w-FRAME_WH-1;
  pBuf->size.y = pWindow->size.y;
  
  /* Production sabotage */
  pBuf = pBuf->prev;
  
  pBuf->size.x = pWindow->size.x + FRAME_WH;
  pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 2;
  pBuf->size.w = w;
  h = pBuf->size.h;
  
  area.x = 10;
  area.h = 2;
  
  pBuf = pBuf->prev;
  while(pBuf)
  {
    
    if (pBuf == pAdvanced_Terrain_Dlg->pEndActiveWidgetList)
    {
      w -= imp_h;
    }
    
    pBuf->size.w = w;
    pBuf->size.x = pBuf->next->size.x;
    pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
    
    if (pBuf->ID == ID_SEPARATOR)
    {
      FREESURFACE(pBuf->theme);
      pBuf->size.h = h;
      pBuf->theme = create_surf(w , h , SDL_SWSURFACE);
    
      area.y = pBuf->size.h / 2 - 1;
      area.w = pBuf->size.w - 20;
      
      SDL_FillRect(pBuf->theme , &area, 64);
      SDL_SetColorKey(pBuf->theme , SDL_SRCCOLORKEY|SDL_RLEACCEL , 0x0);
    }
    
    if (pBuf == pLast) break;
    pBuf = pBuf->prev;  
   
  }
  
  if (n > 10)
  {
    /* up button */
    pBuf = pBuf->prev;
    
    pBuf->size.x = pWindow->size.x + pWindow->size.w - pBuf->size.w - FRAME_WH;
    pBuf->size.y = pAdvanced_Terrain_Dlg->pEndActiveWidgetList->size.y;
    
    pAdvanced_Terrain_Dlg->pScroll->min = pBuf->size.y + pBuf->size.h;
    
    /* scrollbar */
    pBuf = pBuf->prev;
    
    pBuf->size.x = pBuf->next->size.x;
    pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
    
    /* down button */
    pBuf = pBuf->prev;
    
    pBuf->size.x = pWindow->size.x + pWindow->size.w - pBuf->size.w - FRAME_WH;
    pBuf->size.y = pWindow->size.y + pWindow->size.h - FRAME_WH - pBuf->size.h;
    
    pAdvanced_Terrain_Dlg->pScroll->max = pBuf->size.y;
    
    /* 
       units scrollbar high
     */
    pBuf->next->size.h = scrollbar_size(pAdvanced_Terrain_Dlg->pScroll);
    
  }
  
  /* -------------------- */
  /* redraw */
  redraw_group(pAdvanced_Terrain_Dlg->pBeginWidgetList, pWindow, Main.gui,0);

  flush_rect(pWindow->size);
  
}

/* ====================================================================== */
/* ============================== INCITE DIALOG ========================= */
/* ====================================================================== */
static struct SMALL_DLG *pIncite_Dlg = NULL;

/****************************************************************
...
*****************************************************************/
static int incite_dlg_window_callback(struct GUI *pWindow )
{
  return -1;
}

/****************************************************************
...
*****************************************************************/
static int diplomat_incite_yes_callback(struct GUI *pWidget)
{
  struct packet_diplomat_action req;
    
  req.action_type=DIPLOMAT_INCITE;
  req.diplomat_id = MAX_ID - pWidget->ID;
  req.target_id = ((struct city *)pWidget->data)->id;
  
  popdown_incite_dialog();
  
  send_packet_diplomat_action(&aconnection, &req);
  return -1;
}

/****************************************************************
...
*****************************************************************/
static int exit_incite_dlg_callback(struct GUI *pWidget)
{
  popdown_incite_dialog();
  process_diplomat_arrival(NULL, 0);
  return -1;
}

/**************************************************************************
  Popdown a window asking a diplomatic unit if it wishes to incite the
  given enemy city.
**************************************************************************/
static void popdown_incite_dialog(void)
{
  if (pIncite_Dlg) {
    
    popdown_window_group_dialog(pIncite_Dlg->pBeginWidgetList,
				pIncite_Dlg->pEndWidgetList, Main.gui);
    FREE(pIncite_Dlg);
    flush_dirty();
  }
}

/**************************************************************************
  Popup a window asking a diplomatic unit if it wishes to incite the
  given enemy city.
**************************************************************************/
void popup_incite_dialog(struct city *pCity)
{
  struct GUI *pWindow = NULL, *pBuf = NULL;
  SDL_String16 *pStr;
  struct unit *pUnit;
  char cBuf[255]; 
  int w = 0, h , canvas_x , canvas_y;
  bool exit = FALSE;
  
  if (pIncite_Dlg) {
    return;
  }
  
  /* ugly hack */
  pUnit = get_unit_in_focus();
  
  if ( !pUnit || !unit_flag(pUnit,F_SPY)) return;
  
  pIncite_Dlg = MALLOC(sizeof(struct SMALL_DLG));
  
  h = WINDOW_TILE_HIGH + 3 + FRAME_WH;
  
  get_canvas_xy(pCity->x , pCity->y , &canvas_x , &canvas_y);
  
  /* window */
  pStr = create_str16_from_char(_("Incite a Revolt!"), 12);
    
  pStr->style |= TTF_STYLE_BOLD;
  
  pWindow = create_window(pStr , 10 , 10 , WF_DRAW_THEME_TRANSPARENT);
    
  pWindow->action = incite_dlg_window_callback;
  set_wstate(pWindow , WS_NORMAL);
  w = MAX(w , pWindow->size.w + 8);
  
  add_to_gui_list(ID_INCITE_DLG_WINDOW, pWindow);
  pIncite_Dlg->pEndWidgetList = pWindow;
  
  if (pCity->incite_revolt_cost == INCITE_IMPOSSIBLE_COST) {
    
    /* exit button */
    pBuf = create_themeicon(ResizeSurface(pTheme->CANCEL_Icon ,
			  pTheme->CANCEL_Icon->w - 4,
			  pTheme->CANCEL_Icon->h - 4, 1) ,
  			  (WF_FREE_THEME|WF_DRAW_THEME_TRANSPARENT));
    SDL_SetColorKey(pBuf->theme,
	  SDL_SRCCOLORKEY|SDL_RLEACCEL, get_first_pixel(pBuf->theme));
  
    w += pBuf->size.w + 10;
    pBuf->action = exit_incite_dlg_callback;
    set_wstate(pBuf, WS_NORMAL);
    pBuf->key = SDLK_ESCAPE;
  
    add_to_gui_list(ID_INCITE_DLG_EXIT_BUTTON, pBuf);
    exit = TRUE;
    /* --------------- */
    
    my_snprintf(cBuf, sizeof(cBuf), _("You can't incite a revolt in %s."),
		pCity->name);

    create_active_iconlabel(pBuf , pStr , cBuf , NULL , NULL);
        
    add_to_gui_list(ID_LABEL , pBuf);
    
    w = MAX(w , pBuf->size.w);
    h += pBuf->size.h;
    /*------------*/
    create_active_iconlabel(pBuf , pStr ,
	    _("City can't be incited!"), NULL , NULL);
        
    add_to_gui_list(ID_LABEL , pBuf);
    
    w = MAX(w , pBuf->size.w);
    h += pBuf->size.h;
    
  } else if (game.player_ptr->economic.gold >= pCity->incite_revolt_cost) {
    my_snprintf(cBuf, sizeof(cBuf),
		_("Incite a revolt for %d gold?\nTreasury contains %d gold."), 
		pCity->incite_revolt_cost, game.player_ptr->economic.gold);
    
    create_active_iconlabel( pBuf , pStr , cBuf , NULL , NULL );
        
  
    add_to_gui_list(ID_LABEL , pBuf);
    
    w = MAX(w , pBuf->size.w);
    h += pBuf->size.h;
    
    /*------------*/
    create_active_iconlabel(pBuf , pStr ,
	    _("Yes") , pCity , diplomat_incite_yes_callback);
    
    set_wstate(pBuf , WS_NORMAL);
  
    add_to_gui_list(MAX_ID - pUnit->id , pBuf);
    
    w = MAX(w , pBuf->size.w);
    h += pBuf->size.h;
    /* ------- */
    create_active_iconlabel(pBuf , pStr ,
	    _("No") , NULL , exit_incite_dlg_callback);
    
    set_wstate(pBuf , WS_NORMAL);
    pBuf->key = SDLK_ESCAPE;
    
    add_to_gui_list(ID_LABEL , pBuf);
    
    w = MAX(w , pBuf->size.w);
    h += pBuf->size.h;
    
  } else {
    /* exit button */
    pBuf = create_themeicon(ResizeSurface( pTheme->CANCEL_Icon ,
			  pTheme->CANCEL_Icon->w - 4,
			  pTheme->CANCEL_Icon->h - 4, 1) ,
  			  (WF_FREE_THEME|WF_DRAW_THEME_TRANSPARENT));
    SDL_SetColorKey(pBuf->theme ,
	  SDL_SRCCOLORKEY|SDL_RLEACCEL , get_first_pixel(pBuf->theme));
  
    w += pBuf->size.w + 10;
    pBuf->action = exit_incite_dlg_callback;
    set_wstate(pBuf, WS_NORMAL);
    pBuf->key = SDLK_ESCAPE;
  
    add_to_gui_list(ID_INCITE_DLG_EXIT_BUTTON, pBuf);
    exit = TRUE;
    /* --------------- */

    my_snprintf(cBuf, sizeof(cBuf),
		_("Inciting a revolt costs %d gold.\n"
		  "Treasury contains %d gold."), 
		pCity->incite_revolt_cost, game.player_ptr->economic.gold);
    
    create_active_iconlabel(pBuf , pStr , cBuf , NULL , NULL);
        
  
    add_to_gui_list(ID_LABEL , pBuf);
    
    w = MAX(w , pBuf->size.w);
    h += pBuf->size.h;
    
    /*------------*/
    create_active_iconlabel(pBuf , pStr ,
	    _("Traitors Demand Too Much!"), NULL , NULL);
        
    add_to_gui_list(ID_LABEL , pBuf);
    
    w = MAX(w , pBuf->size.w);
    h += pBuf->size.h;
  }
  pIncite_Dlg->pBeginWidgetList = pBuf;
  
  /* setup window size and start position */
  
  pWindow->size.w = w + DOUBLE_FRAME_WH;
  pWindow->size.h = h;
  
  if (canvas_x + NORMAL_TILE_WIDTH + (w + DOUBLE_FRAME_WH) > Main.screen->w)
  {
    if (canvas_x - w >= 0)
    {
      pWindow->size.x = canvas_x - (w + DOUBLE_FRAME_WH);
    }
    else
    {
      pWindow->size.x = (Main.screen->w - (w + DOUBLE_FRAME_WH)) / 2;
    }
  }
  else
  {
    pWindow->size.x = canvas_x + NORMAL_TILE_WIDTH;
  }
    
  if (canvas_y - NORMAL_TILE_HEIGHT + h > Main.screen->h)
  {
    if (canvas_y + NORMAL_TILE_HEIGHT - h >= 0)
    {
      pWindow->size.y = canvas_y + NORMAL_TILE_HEIGHT - h;
    }
    else
    {
      pWindow->size.y = (Main.screen->h - h) / 2;
    }
  }
  else
  {
    pWindow->size.y = canvas_y - NORMAL_TILE_HEIGHT;
  }
        
  resize_window(pWindow , NULL , NULL , pWindow->size.w , h);
  
  /* setup widget size and start position */
  pBuf = pWindow;
  
  if (exit)
  {/* exit button */
    pBuf = pBuf->prev;
    pBuf->size.x = pWindow->size.x + pWindow->size.w-pBuf->size.w-FRAME_WH-1;
    pBuf->size.y = pWindow->size.y;
  }
  
  pBuf = pBuf->prev;
    
  pBuf->size.x = pWindow->size.x + FRAME_WH;
  pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 2;
  pBuf->size.w = w;
    
  pBuf = pBuf->prev;
  while(pBuf)
  {
    pBuf->size.w = w;
    pBuf->size.x = pBuf->next->size.x;
    pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
    if (pBuf == pIncite_Dlg->pBeginWidgetList) break;
    pBuf = pBuf->prev;
  }
  /* --------------------- */
  /* redraw */
  redraw_group(pIncite_Dlg->pBeginWidgetList, pWindow, Main.gui,0);

  flush_rect(pWindow->size);
  
}

/* ====================================================================== */
/* ============================ BRIBE DIALOG ========================== */
/* ====================================================================== */
static struct SMALL_DLG *pBribe_Dlg = NULL;

static int bribe_dlg_window_callback( struct GUI *pWindow )
{
  return -1;
}

static int diplomat_bribe_yes_callback(struct GUI *pWidget)
{
  struct packet_diplomat_action req;

  req.action_type = DIPLOMAT_BRIBE;
  req.diplomat_id = MAX_ID - pWidget->ID;
  req.target_id = ((struct unit *)pWidget->data)->id;

  popdown_bribe_dialog();
  
  send_packet_diplomat_action(&aconnection, &req);
  return -1;
}

static int exit_bribe_dlg_callback( struct GUI *pWidget )
{
  popdown_bribe_dialog();
  return -1;
}

/**************************************************************************
  Popdown a dialog asking a diplomatic unit if it wishes to bribe the
  given enemy unit.
**************************************************************************/
static void popdown_bribe_dialog(void)
{
  if (pBribe_Dlg) {
    popdown_window_group_dialog(pBribe_Dlg->pBeginWidgetList,
				pBribe_Dlg->pEndWidgetList, Main.gui);
    FREE(pBribe_Dlg);
    flush_dirty();
  }
}

/**************************************************************************
  Popup a dialog asking a diplomatic unit if it wishes to bribe the
  given enemy unit.
**************************************************************************/
void popup_bribe_dialog(struct unit *pUnit)
{
  struct GUI *pWindow = NULL, *pBuf = NULL;
  SDL_String16 *pStr;
  struct unit *pDiplomatUnit;
  char cBuf[255]; 
  int w = 0, h , canvas_x , canvas_y;
  bool exit = FALSE;
  
  if (pBribe_Dlg) {
    return;
  }
    
  /* ugly hack */
  pDiplomatUnit = get_unit_in_focus();
  
  if (!pDiplomatUnit || !(unit_flag(pDiplomatUnit, F_SPY) ||
		    unit_flag(pDiplomatUnit, F_DIPLOMAT))) {
    return;
  }
    
  pBribe_Dlg = MALLOC(sizeof(struct SMALL_DLG));
  
  h = WINDOW_TILE_HIGH + 3 + FRAME_WH;
      
  get_canvas_xy(pDiplomatUnit->x, pDiplomatUnit->y, &canvas_x, &canvas_y);
  
  /* window */
  pStr = create_str16_from_char(_("Bribe Enemy Unit"), 12);
    
  pStr->style |= TTF_STYLE_BOLD;
  
  pWindow = create_window(pStr , 10 , 10 , WF_DRAW_THEME_TRANSPARENT);
    
  pWindow->action = bribe_dlg_window_callback;
  set_wstate(pWindow , WS_NORMAL);
  w = MAX(w , pWindow->size.w + 8);
  
  add_to_gui_list(ID_BRIBE_DLG_WINDOW, pWindow);
  pBribe_Dlg->pEndWidgetList = pWindow;
  
  if(game.player_ptr->economic.gold >= pUnit->bribe_cost) {
    my_snprintf(cBuf, sizeof(cBuf),
		_("Bribe unit for %d gold?\nTreasury contains %d gold."), 
		pUnit->bribe_cost, game.player_ptr->economic.gold);
    
    create_active_iconlabel(pBuf , pStr , cBuf , NULL , NULL);
  
    add_to_gui_list(ID_LABEL , pBuf);
    
    w = MAX(w , pBuf->size.w);
    h += pBuf->size.h;
    
    /*------------*/
    create_active_iconlabel(pBuf , pStr,
	    _("Yes") , pUnit , diplomat_bribe_yes_callback);
    
    set_wstate(pBuf , WS_NORMAL);
  
    add_to_gui_list(MAX_ID - pDiplomatUnit->id , pBuf);
    
    w = MAX(w , pBuf->size.w);
    h += pBuf->size.h;
    /* ------- */
    create_active_iconlabel(pBuf , pStr,
	    _("No") , NULL , exit_bribe_dlg_callback);
    
    set_wstate(pBuf , WS_NORMAL);
    pBuf->key = SDLK_ESCAPE;
    
    add_to_gui_list(ID_LABEL , pBuf);
    
    w = MAX(w , pBuf->size.w);
    h += pBuf->size.h;
    
  } else {
    /* exit button */
    pBuf = create_themeicon(ResizeSurface( pTheme->CANCEL_Icon ,
			  pTheme->CANCEL_Icon->w - 4,
			  pTheme->CANCEL_Icon->h - 4, 1) ,
  			  (WF_FREE_THEME|WF_DRAW_THEME_TRANSPARENT));
    SDL_SetColorKey(pBuf->theme,
	  SDL_SRCCOLORKEY|SDL_RLEACCEL , get_first_pixel(pBuf->theme));
  
    w += pBuf->size.w + 10;
    pBuf->action = exit_bribe_dlg_callback;
    set_wstate(pBuf, WS_NORMAL);
    pBuf->key = SDLK_ESCAPE;
  
    add_to_gui_list(ID_BRIBE_DLG_EXIT_BUTTON, pBuf);
    exit = TRUE;
    /* --------------- */

    my_snprintf(cBuf, sizeof(cBuf),
		_("Bribing the unit costs %d gold.\n"
		  "Treasury contains %d gold."), 
		pUnit->bribe_cost, game.player_ptr->economic.gold);
    
    create_active_iconlabel(pBuf , pStr , cBuf , NULL , NULL);
  
    add_to_gui_list(ID_LABEL , pBuf);
    
    w = MAX(w , pBuf->size.w);
    h += pBuf->size.h;
    
    /*------------*/
    create_active_iconlabel(pBuf , pStr,
	    _("Traitors Demand Too Much!"), NULL , NULL);
        
    add_to_gui_list(ID_LABEL , pBuf);
    
    w = MAX(w , pBuf->size.w);
    h += pBuf->size.h;
  }
  pBribe_Dlg->pBeginWidgetList = pBuf;
  
  /* setup window size and start position */
  
  pWindow->size.w = w + DOUBLE_FRAME_WH;
  pWindow->size.h = h;
  
  if (canvas_x + NORMAL_TILE_WIDTH + (w + DOUBLE_FRAME_WH) > Main.screen->w)
  {
    if (canvas_x - w >= 0)
    {
      pWindow->size.x = canvas_x - (w + DOUBLE_FRAME_WH);
    }
    else
    {
      pWindow->size.x = (Main.screen->w - (w + DOUBLE_FRAME_WH)) / 2;
    }
  }
  else
  {
    pWindow->size.x = canvas_x + NORMAL_TILE_WIDTH;
  }
    
  if (canvas_y - NORMAL_TILE_HEIGHT + h > Main.screen->h)
  {
    if (canvas_y + NORMAL_TILE_HEIGHT - h >= 0)
    {
      pWindow->size.y = canvas_y + NORMAL_TILE_HEIGHT - h;
    }
    else
    {
      pWindow->size.y = (Main.screen->h - h) / 2;
    }
  }
  else
  {
    pWindow->size.y = canvas_y - NORMAL_TILE_HEIGHT;
  }
        
  resize_window(pWindow , NULL , NULL , pWindow->size.w , h);
  
  /* setup widget size and start position */
  pBuf = pWindow;
  
  if (exit)
  {/* exit button */
    pBuf = pBuf->prev;
    pBuf->size.x = pWindow->size.x + pWindow->size.w-pBuf->size.w-FRAME_WH-1;
    pBuf->size.y = pWindow->size.y;
  }
  
  pBuf = pBuf->prev;
    
  pBuf->size.x = pWindow->size.x + FRAME_WH;
  pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 2;
  pBuf->size.w = w;
    
  pBuf = pBuf->prev;
  while(pBuf)
  {
    pBuf->size.w = w;
    pBuf->size.x = pBuf->next->size.x;
    pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
    if (pBuf == pBribe_Dlg->pBeginWidgetList) break;
    pBuf = pBuf->prev;
  }
  /* --------------------- */
  /* redraw */
  redraw_group(pBribe_Dlg->pBeginWidgetList, pWindow, Main.gui, 0);

  flush_rect(pWindow->size);
}

/* ====================================================================== */
/* ============================ PILLAGE DIALOG ========================== */
/* ====================================================================== */
static struct SMALL_DLG *pPillage_Dlg = NULL;

static int pillage_window_callback(struct GUI *pWindow)
{
  return -1;
}

static int pillage_callback(struct GUI *pWidget)
{
  
  struct unit *pUnit = ((struct unit *)pWidget->data);
  enum tile_special_type what = MAX_ID - pWidget->ID;
  
  popdown_pillage_dialog();
  
  if (pUnit) 
  {
    request_new_unit_activity_targeted(pUnit, ACTIVITY_PILLAGE, what);
  }
  
  return -1;
}

static int exit_pillage_dlg_callback(struct GUI *pWidget)
{
  popdown_pillage_dialog();
  return -1;
}

/**************************************************************************
  Popdown a dialog asking the unit which improvement they would like to
  pillage.
**************************************************************************/
static void popdown_pillage_dialog(void)
{
  if (pPillage_Dlg) {
    popdown_window_group_dialog(pPillage_Dlg->pBeginWidgetList,
				pPillage_Dlg->pEndWidgetList, Main.gui);
    FREE(pPillage_Dlg);
    flush_dirty();
  }
}

/**************************************************************************
  Popup a dialog asking the unit which improvement they would like to
  pillage.
**************************************************************************/
void popup_pillage_dialog(struct unit *pUnit,
			  enum tile_special_type may_pillage)
{
  struct GUI *pWindow = NULL, *pBuf = NULL;
  SDL_String16 *pStr;
  enum tile_special_type what;
  int w = 0, h , canvas_x , canvas_y;
  
  if (pPillage_Dlg) {
    return;
  }

  pPillage_Dlg = MALLOC(sizeof(struct SMALL_DLG));
  
  h = WINDOW_TILE_HIGH + 3 + FRAME_WH;
  
  get_canvas_xy(pUnit->x , pUnit->y , &canvas_x , &canvas_y);
  
  /* window */
  pStr = create_str16_from_char(_("What To Pillage") , 12);
  pStr->style |= TTF_STYLE_BOLD;
  
  pWindow = create_window(pStr , 10, 10, WF_DRAW_THEME_TRANSPARENT);
    
  pWindow->action = pillage_window_callback;
  set_wstate(pWindow , WS_NORMAL);
  w = MAX(w , pWindow->size.w);
  
  add_to_gui_list(ID_PILLAGE_DLG_WINDOW, pWindow);
  pPillage_Dlg->pEndWidgetList = pWindow;
    
  /* ---------- */
  /* exit button */
  pBuf = create_themeicon(ResizeSurface(pTheme->CANCEL_Icon ,
			  pTheme->CANCEL_Icon->w - 4,
			  pTheme->CANCEL_Icon->h - 4, 1) ,
  			  (WF_FREE_THEME|WF_DRAW_THEME_TRANSPARENT));
  SDL_SetColorKey(pBuf->theme ,
	  SDL_SRCCOLORKEY|SDL_RLEACCEL , get_first_pixel(pBuf->theme));
  
  w += pBuf->size.w + 10;
  pBuf->action = exit_pillage_dlg_callback;
  set_wstate(pBuf, WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  
  add_to_gui_list(ID_PILLAGE_DLG_EXIT_BUTTON, pBuf);
  /* ---------- */
  
  while (may_pillage != S_NO_SPECIAL) {
    what = get_preferred_pillage(may_pillage);
    
    create_active_iconlabel(pBuf , pStr ,
	    get_special_name(what) , pUnit , pillage_callback);
    
    set_wstate(pBuf , WS_NORMAL);
  
    add_to_gui_list(MAX_ID - what , pBuf);
    
    w = MAX(w , pBuf->size.w);
    h += pBuf->size.h;
        
    may_pillage &= (~(what | map_get_infrastructure_prerequisite(what)));
  }
  pPillage_Dlg->pBeginWidgetList = pBuf;
  
  /* setup window size and start position */
  
  pWindow->size.w = w + DOUBLE_FRAME_WH;
  pWindow->size.h = h;
  
  if (canvas_x + NORMAL_TILE_WIDTH + (w + DOUBLE_FRAME_WH) > Main.screen->w)
  {
    if (canvas_x - w >= 0)
    {
      pWindow->size.x = canvas_x - (w + DOUBLE_FRAME_WH);
    }
    else
    {
      pWindow->size.x = (Main.screen->w - (w + DOUBLE_FRAME_WH)) / 2;
    }
  }
  else
  {
    pWindow->size.x = canvas_x + NORMAL_TILE_WIDTH;
  }
    
  if (canvas_y - NORMAL_TILE_HEIGHT + h > Main.screen->h)
  {
    if (canvas_y + NORMAL_TILE_HEIGHT - h >= 0)
    {
      pWindow->size.y = canvas_y + NORMAL_TILE_HEIGHT - h;
    }
    else
    {
      pWindow->size.y = (Main.screen->h - h) / 2;
    }
  }
  else
  {
    pWindow->size.y = canvas_y - NORMAL_TILE_HEIGHT;
  }
        
  resize_window(pWindow , NULL , NULL , pWindow->size.w , h);
  
  /* setup widget size and start position */

  /* exit button */  
  pBuf = pWindow->prev;
  
  pBuf->size.x = pWindow->size.x + pWindow->size.w-pBuf->size.w-FRAME_WH-1;
  pBuf->size.y = pWindow->size.y;

  /* first special to pillage */
  pBuf = pBuf->prev;
  
  pBuf->size.x = pWindow->size.x + FRAME_WH;
  pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 2;
  pBuf->size.w = w;
    
  pBuf = pBuf->prev;
  while(pBuf)
  {
    pBuf->size.w = w;
    pBuf->size.x = pBuf->next->size.x;
    pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
    if (pBuf == pPillage_Dlg->pBeginWidgetList) break;
    pBuf = pBuf->prev;
  }
  /* --------------------- */
  /* redraw */
  redraw_group(pPillage_Dlg->pBeginWidgetList, pWindow, Main.gui, 0);

  flush_rect(pWindow->size);
}

/* ======================================================================= */
/* =========================== CONNECT DIALOG ============================ */
/* ======================================================================= */
static struct SMALL_DLG *pConnect_Dlg = NULL;

/****************************************************************
 ...
*****************************************************************/
static int connect_window_callback(struct GUI *pWindow)
{
  return -1;
}

/****************************************************************
 ...
*****************************************************************/
static int unit_connect_callback(struct GUI *pWidget)
{
  enum unit_activity act = MAX_ID - pWidget->ID;
  int x = ((struct map_position *)pWidget->data)->x;
  int y = ((struct map_position *)pWidget->data)->y;
  
  /* may not work */  
  struct unit *pUnit = get_unit_in_focus();
    
  popdown_connect_dialog();
  flush_dirty();
  
  if (pUnit && unit_flag(pUnit, F_SETTLERS)) {
    struct packet_unit_connect req;

    req.activity_type = act;
    req.unit_id = pUnit->id;
    req.dest_x = x;
    req.dest_y = y;
    send_packet_unit_connect(&aconnection, &req);
  }
  return -1;
}

/****************************************************************
 ...
*****************************************************************/
static int exit_connect_dlg_callback(struct GUI *pWidget)
{
  /* may not work */  
  struct unit *pUnit = get_unit_in_focus();

  popdown_connect_dialog();
  
  if (pUnit) {
    update_unit_info_label(pUnit);
  }
  
  flush_dirty();
  return -1;
}

/**************************************************************************
  Popdown a dialog asking the unit how they want to "connect" their
  location to the destination.
**************************************************************************/
static void popdown_connect_dialog(void)
{
  if (pConnect_Dlg) {
    popdown_window_group_dialog(pConnect_Dlg->pBeginWidgetList,
				pConnect_Dlg->pEndWidgetList, Main.gui);
    FREE(pConnect_Dlg);
  }
}

/**************************************************************************
  Popup a dialog asking the unit how they want to "connect" their
  location to the destination.
**************************************************************************/
void popup_unit_connect_dialog(struct unit *pUnit, int dest_x, int dest_y)
{
  struct GUI *pWindow = NULL, *pBuf = NULL;
  SDL_String16 *pStr;
  enum unit_activity activity;
  int w = 0, h , canvas_x , canvas_y;
  
  if (pPillage_Dlg) {
    return;
  }

  pConnect_Dlg = MALLOC(sizeof(struct SMALL_DLG));
  
  h = WINDOW_TILE_HIGH + 3 + FRAME_WH;
  
  /*get_canvas_xy( pUnit->x , pUnit->y , &canvas_x , &canvas_y );*/
  canvas_x = Main.event.motion.x;
  canvas_y = Main.event.motion.y;
  
  /* window */
  pStr = create_str16_from_char(_("Connect") , 12);
  pStr->style |= TTF_STYLE_BOLD;
  
  pWindow = create_window( pStr , 10 , 10 , WF_DRAW_THEME_TRANSPARENT );
    
  pWindow->action = connect_window_callback;
  set_wstate(pWindow , WS_NORMAL);
  w = MAX(w , pWindow->size.w);
  
  add_to_gui_list(ID_CONNECT_DLG_WINDOW, pWindow);
  pConnect_Dlg->pEndWidgetList = pWindow;
    
  /* ---------- */
  /* exit button */
  pBuf = create_themeicon(ResizeSurface( pTheme->CANCEL_Icon ,
			  pTheme->CANCEL_Icon->w - 4,
			  pTheme->CANCEL_Icon->h - 4, 1) ,
  			  (WF_FREE_THEME|WF_DRAW_THEME_TRANSPARENT));
  SDL_SetColorKey( pBuf->theme ,
	  SDL_SRCCOLORKEY|SDL_RLEACCEL , get_first_pixel(pBuf->theme));
  
  w += pBuf->size.w + 10;
  pBuf->action = exit_connect_dlg_callback;
  set_wstate(pBuf, WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  
  add_to_gui_list(ID_CONNECT_DLG_EXIT_BUTTON, pBuf);
  /* ---------- */
  
  create_active_iconlabel(pBuf , pStr,
	    _("Choose unit activity:"), NULL , NULL);
        
  add_to_gui_list(ID_LABEL , pBuf);
    
  w = MAX(w , pBuf->size.w);
  h += pBuf->size.h;
  /* ---------- */
  
  for (activity = ACTIVITY_IDLE + 1; activity < ACTIVITY_LAST; activity++) {
    if (!can_unit_do_connect(pUnit, activity)) {
      continue;
    }

    create_active_iconlabel(pBuf , pStr ,
	    get_activity_text(activity) , NULL , unit_connect_callback);
    
    pBuf->data = (void *)MALLOC(sizeof(struct map_position));
    ((struct map_position *)pBuf->data)->x = dest_x;
    ((struct map_position *)pBuf->data)->y = dest_y;
    
    set_wstate(pBuf , WS_NORMAL);
    set_wflag(pBuf , WF_FREE_DATA);
    
    add_to_gui_list(MAX_ID - activity , pBuf);
    
    w = MAX( w , pBuf->size.w );  
    h += pBuf->size.h;
  }
  pConnect_Dlg->pBeginWidgetList = pBuf;
  
  /* setup window size and start position */
  
  pWindow->size.w = w + DOUBLE_FRAME_WH;
  pWindow->size.h = h;
  
  if (canvas_x + NORMAL_TILE_WIDTH + (w + DOUBLE_FRAME_WH) > Main.screen->w)
  {
    if (canvas_x - w >= 0)
    {
      pWindow->size.x = canvas_x - (w + DOUBLE_FRAME_WH);
    }
    else
    {
      pWindow->size.x = ( Main.screen->w - (w + DOUBLE_FRAME_WH) ) / 2;
    }
  }
  else
  {
    pWindow->size.x = canvas_x + NORMAL_TILE_WIDTH;
  }
    
  if (canvas_y - NORMAL_TILE_HEIGHT + h > Main.screen->h)
  {
    if (canvas_y + NORMAL_TILE_HEIGHT - h >= 0)
    {
      pWindow->size.y = canvas_y + NORMAL_TILE_HEIGHT - h;
    }
    else
    {
      pWindow->size.y = (Main.screen->h - h) / 2;
    }
  }
  else
  {
    pWindow->size.y = canvas_y - NORMAL_TILE_HEIGHT;
  }
        
  resize_window(pWindow , NULL , NULL , pWindow->size.w , h);
  
  /* setup widget size and start position */

  /* exit button */  
  pBuf = pWindow->prev;
  
  pBuf->size.x = pWindow->size.x + pWindow->size.w-pBuf->size.w-FRAME_WH-1;
  pBuf->size.y = pWindow->size.y;

  /* first special to pillage */
  pBuf = pBuf->prev;
  
  pBuf->size.x = pWindow->size.x + FRAME_WH;
  pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 2;
  pBuf->size.w = w;
    
  pBuf = pBuf->prev;
  while(pBuf)
  {
    pBuf->size.w = w;
    pBuf->size.x = pBuf->next->size.x;
    pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
    if (pBuf == pConnect_Dlg->pBeginWidgetList) break;
    pBuf = pBuf->prev;
  }
  /* --------------------- */
  /* redraw */
  redraw_group(pConnect_Dlg->pBeginWidgetList, pWindow, Main.gui,0);

  flush_rect(pWindow->size);

}

/**************************************************************************
                                   Tax Rates
**************************************************************************/

static struct GUI *pBeginTaxRateDlgWidgetList = NULL;
static struct GUI *pEndTaxRateDlgWidgetList = NULL;

/**************************************************************************
  ...
**************************************************************************/
static int popdown_taxrate_dialog_callback(struct GUI *pButton)
{
  popdown_window_group_dialog(pBeginTaxRateDlgWidgetList,
			      pEndTaxRateDlgWidgetList, Main.gui);
  pEndTaxRateDlgWidgetList = NULL;
  flush_dirty();
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int move_taxrate_dlg_callback(struct GUI *pWindow)
{
  if (move_window_group_dialog(pBeginTaxRateDlgWidgetList, pWindow,
							    Main.gui)) {
    flush_rect(pWindow->size);
  }
  return (-1);
}

/**************************************************************************
  ...
**************************************************************************/
static int toggle_block_callback(struct GUI *pCheckBox)
{
  switch (pCheckBox->ID) {
  case ID_CHANGE_TAXRATE_DLG_TAX_BLOCK_CHECKBOX:
    SDL_Client_Flags ^= CF_CHANGE_TAXRATE_TAX_BLOCK;
    return -1;

  case ID_CHANGE_TAXRATE_DLG_LUX_BLOCK_CHECKBOX:
    SDL_Client_Flags ^= CF_CHANGE_TAXRATE_LUX_BLOCK;
    return -1;

  case ID_CHANGE_TAXRATE_DLG_SCI_BLOCK_CHECKBOX:
    SDL_Client_Flags ^= CF_CHANGE_TAXRATE_SCI_BLOCK;
    return -1;

  default:
    return -1;
  }

  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int horiz_taxrate_callback(struct GUI *pHoriz_Src)
{
  struct GUI *pLabel_Src =
      pHoriz_Src->next->next, *pLabel_Dst, *pHoriz_Dst;
  struct GUI *pTmp, *pBuf = NULL;
  Uint16 pDigit[10], *pStr;
  char cBuf[5];
  int dir, x, min, ret = 1, max =
      get_gov_pplayer(game.player_ptr)->max_rate;

  switch (pHoriz_Src->ID) {
  case ID_CHANGE_TAXRATE_DLG_TAX_SCROLLBAR:
    if (!(SDL_Client_Flags & CF_CHANGE_TAXRATE_LUX_BLOCK)) {
      pHoriz_Dst = pHoriz_Src->prev->prev->prev;	/* lux */
      if (!(SDL_Client_Flags & CF_CHANGE_TAXRATE_SCI_BLOCK)) {
	pTmp = pHoriz_Src->prev->prev->prev->prev->prev->prev;	/* sci */
      } else {
	pTmp = NULL;
      }
    } else {
      if (!(SDL_Client_Flags & CF_CHANGE_TAXRATE_SCI_BLOCK)) {
	/* sci */
	pHoriz_Dst = pHoriz_Src->prev->prev->prev->prev->prev->prev;
	pTmp = NULL;
      } else {
	return -1;		/* all blocked */
      }
    }

    break;

  case ID_CHANGE_TAXRATE_DLG_LUX_SCROLLBAR:
    if (!(SDL_Client_Flags & CF_CHANGE_TAXRATE_SCI_BLOCK)) {
      pHoriz_Dst = pHoriz_Src->prev->prev->prev;	/* sci */
      if (!(SDL_Client_Flags & CF_CHANGE_TAXRATE_TAX_BLOCK)) {
	pTmp = pHoriz_Src->next->next->next;	/* tax */
      } else {
	pTmp = NULL;
      }
    } else {
      if (!(SDL_Client_Flags & CF_CHANGE_TAXRATE_TAX_BLOCK)) {
	pHoriz_Dst = pHoriz_Src->next->next->next;	/* tax */
	pTmp = NULL;
      } else {
	return -1;		/* all blocked */
      }
    }

    break;

  case ID_CHANGE_TAXRATE_DLG_SCI_SCROLLBAR:
    if (!(SDL_Client_Flags & CF_CHANGE_TAXRATE_LUX_BLOCK)) {
      pHoriz_Dst = pHoriz_Src->next->next->next;	/* lux */
      if (!(SDL_Client_Flags & CF_CHANGE_TAXRATE_TAX_BLOCK)) {
	pTmp = pHoriz_Src->next->next->next->next->next->next;	/* tax */
      } else {
	pTmp = NULL;
      }
    } else {
      if (!(SDL_Client_Flags & CF_CHANGE_TAXRATE_TAX_BLOCK)) {
	/* tax */
	pHoriz_Dst = pHoriz_Src->next->next->next->next->next->next;
	pTmp = NULL;
      } else {
	return -1;		/* all blocked */
      }
    }

    break;

  default:
    return -1;
  }

  pLabel_Dst = pHoriz_Dst->next->next;

  min = pLabel_Src->size.x + 2;
  max = pLabel_Src->size.x + 2 + max * 3;
  x = pHoriz_Src->size.x;

  while (ret) {
    SDL_WaitEvent(&Main.event);
    switch (Main.event.type) {
    case SDL_MOUSEBUTTONUP:
      ret = 0;
      break;
    case SDL_MOUSEMOTION:

      if ((abs(Main.event.motion.x - x) > 15) &&
	  (pHoriz_Src->size.x >= min) &&
	  (pHoriz_Src->size.x <= max) &&
	  (Main.event.motion.x >= min) && (Main.event.motion.x <= max)) {

	if (Main.event.motion.xrel > 0) {
	  dir = 30;
	} else {
	  dir = -30;
	}

	x = pHoriz_Src->size.x;

	if (((x + dir) <= max) && ((x + dir) >= min)) {
	  x = pHoriz_Dst->size.x;
	  if (((x + (-1 * dir)) > max) || ((x + (-1 * dir)) < min)) {
	    if ((pTmp) &&
		(((pTmp->size.x + (-1 * dir)) <= max) &&
		 ((pTmp->size.x + (-1 * dir)) >= min))) {
	      pBuf = pHoriz_Dst;
	      pHoriz_Dst = pTmp;
	      pLabel_Dst = pHoriz_Dst->next->next;
	    } else {
	      goto END;
	    }
	  }

	  pHoriz_Src->size.x += dir;
	  pHoriz_Dst->size.x -= dir;

	  my_snprintf(cBuf, sizeof(cBuf), "%d%%",
		      (pHoriz_Src->size.x - pLabel_Src->size.x) / 3);

	  convertcopy_to_utf16(pDigit, cBuf);
	  pStr = pLabel_Src->string16->text;

	  pStr++;

	  while (*pStr != 32) {
	    pStr++;
	  }

	  pStr++;

	  unistrcpy(pStr, pDigit);

	  my_snprintf(cBuf, sizeof(cBuf), "%d%%",
		      (pHoriz_Dst->size.x - pLabel_Dst->size.x) / 3);
	  convertcopy_to_utf16(pDigit, cBuf);

	  pStr = pLabel_Dst->string16->text;

	  pStr++;

	  while (*pStr != 32) {
	    pStr++;
	  }

	  pStr++;

	  unistrcpy(pStr, pDigit);

	  /* redraw label */
	  redraw_label(pLabel_Src, Main.gui);
	  sdl_dirty_rect(pLabel_Src->size);

	  redraw_label(pLabel_Dst , Main.gui);
	  sdl_dirty_rect(pLabel_Dst->size);

	  /* redraw scroolbar */
	  redraw_horiz(pHoriz_Src, Main.gui);
	  redraw_horiz(pHoriz_Dst, Main.gui);

	  flush_dirty();

	  if (pBuf) {
	    pHoriz_Dst = pBuf;
	    pLabel_Dst = pHoriz_Dst->next->next;
	    pBuf = NULL;
	  }

	END:
	  x = pHoriz_Src->size.x;
	}
      }				/* if */
      break;
    default:
      break;
    }				/* switch */
  }				/* while */

  unsellect_widget_action();
  pSellected_Widget = pHoriz_Src;
  set_wstate(pHoriz_Src, WS_SELLECTED);
  redraw_horiz(pHoriz_Src, Main.gui);
  flush_rect(pHoriz_Src->size);

  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int apply_taxrate_dialog_callback(struct GUI *pButton)
{
  int x;
  struct GUI *pBuf;
  struct packet_player_request packet;

  if (get_client_state() != CLIENT_GAME_RUNNING_STATE) {
    return -1;
  }

  /* Tax label */
  pBuf = pEndTaxRateDlgWidgetList->prev->prev;
  x = pBuf->size.x;
  /* Tax HScrollBar */
  pBuf = pBuf->prev->prev;
  packet.tax = (pBuf->size.x - x) / 3;

  /* Lux Label */
  pBuf = pBuf->prev;
  x = pBuf->size.x;
  /* lux HScrollBar */
  pBuf = pBuf->prev->prev;
  packet.luxury = (pBuf->size.x - x) / 3;

  /* Sci Label */
  pBuf = pBuf->prev;
  x = pBuf->size.x;
  /* Sci HScrollBar */
  pBuf = pBuf->prev->prev;
  packet.science = (pBuf->size.x - x) / 3;

  send_packet_player_request(&aconnection, &packet, PACKET_PLAYER_RATES);

  popdown_window_group_dialog(pBeginTaxRateDlgWidgetList,
			      pEndTaxRateDlgWidgetList, Main.gui);
  pEndTaxRateDlgWidgetList = NULL;
  flush_dirty();
  return -1;
}


/**************************************************************************
  ...
**************************************************************************/
void popup_taxrate_dialog(void)
{
  struct GUI *pBuf, *pWindow;
  SDL_String16 *pStr;
  char cBuf[80];
  Sint16 y;
  struct government *pGov = get_gov_pplayer(game.player_ptr);
  SDL_Surface *pSurf = create_bcgnd_surf(pTheme->Edit,
					 SDL_TRUE, 2, 334,
					 pTheme->Horiz->h - 3);

  pStr = create_str16_from_char(_("Select tax, luxury and science rates"), 12);
  pStr->style |= TTF_STYLE_BOLD;

  pWindow = create_window(pStr, 440, 230, 0);

  pWindow->size.x = (Main.gui->w - 430) / 2;
  pWindow->size.y = (Main.gui->h - 230) / 2;

  resize_window(pWindow, NULL,
		get_game_colorRGB(COLOR_STD_BACKGROUND_BROWN), 430, 230);

  pWindow->action = move_taxrate_dlg_callback;
  set_wstate(pWindow, WS_NORMAL);

  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_WINDDOW, pWindow);
  pEndTaxRateDlgWidgetList = pWindow;
  /* ---------- */
  my_snprintf(cBuf, sizeof(cBuf), _("%s max rate : %d%%"),
	      pGov->name, pGov->max_rate);

  pStr = create_str16_from_char(cBuf, 12);
  pStr->style |= TTF_STYLE_BOLD;

  pBuf = create_iconlabel(NULL, pStr, 0);

  pBuf->size.x = pWindow->size.x;
  pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 2;
  pBuf->size.w = pWindow->size.w;	/* auto center on window */

  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_GOVERMENT_LABEL, pBuf);
  /* ============================================================== */
  /* it is important to leave 1 space at ending of this string */
  my_snprintf(cBuf, sizeof(cBuf), "%s : %d%%",
	      _("Tax Rate"), game.player_ptr->economic.tax);

  pStr = create_str16_from_char(cBuf, 11);
  pStr->style |= TTF_STYLE_BOLD;

  pBuf =
      create_iconlabel(pSurf, pStr,
		       (WF_DRAW_THEME_TRANSPARENT | WF_ICON_UNDER_TEXT));
  pStr->style &= ~SF_CENTER;

  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_TAX_LABEL, pBuf);

  pBuf->size.x = pWindow->size.x + 10;
  pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h + 10;
  y = pBuf->size.y + pBuf->size.h;

  /* ---------- */

  my_snprintf(cBuf, sizeof(cBuf), _("Lock"));
  pStr = create_str16_from_char(cBuf, 10);
  pStr->style |= TTF_STYLE_BOLD;

  pBuf =
      create_textcheckbox((SDL_Client_Flags & CF_CHANGE_TAXRATE_TAX_BLOCK),
			  pStr, WF_DRAW_THEME_TRANSPARENT);

  pBuf->action = toggle_block_callback;
  set_wstate(pBuf, WS_NORMAL);

  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_TAX_BLOCK_CHECKBOX, pBuf);

  pBuf->size.x = pBuf->next->size.x + pBuf->next->size.w + 10;
  pBuf->size.y =
      pBuf->next->size.y + pBuf->next->size.h - pBuf->size.h + 2;

  /* ---------- */

  pBuf = create_horizontal(pTheme->Horiz, 30, 0);

  pBuf->action = horiz_taxrate_callback;
  set_wstate(pBuf, WS_NORMAL);

  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_TAX_SCROLLBAR, pBuf);

  pBuf->size.x =
      pWindow->size.x + 12 + (game.player_ptr->economic.tax * 3);
  pBuf->size.y = y - pBuf->size.h + 2;
  /* ------------------ */
  /* it is important to leave 1 space at ending of this string */
  my_snprintf(cBuf, sizeof(cBuf), " %s : %d%%",
	      _("Luxury"), game.player_ptr->economic.luxury);

  pStr = create_str16_from_char(cBuf, 11);
  pStr->style |= TTF_STYLE_BOLD;

  pBuf =
      create_iconlabel(pSurf, pStr,
		       (WF_DRAW_THEME_TRANSPARENT | WF_ICON_UNDER_TEXT));
  pStr->style &= ~SF_CENTER;

  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_LUX_LABEL, pBuf);

  pBuf->size.x = pWindow->size.x + 10;
  pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h + 10;
  y = pBuf->size.y + pBuf->size.h;

  /* ---------- */

  my_snprintf(cBuf, sizeof(cBuf), _("Lock"));
  pStr = create_str16_from_char(cBuf, 10);
  pStr->style |= TTF_STYLE_BOLD;

  pBuf =
      create_textcheckbox((SDL_Client_Flags & CF_CHANGE_TAXRATE_LUX_BLOCK),
			  pStr, WF_DRAW_THEME_TRANSPARENT);

  pBuf->action = toggle_block_callback;
  set_wstate(pBuf, WS_NORMAL);

  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_LUX_BLOCK_CHECKBOX, pBuf);

  pBuf->size.x = pBuf->next->size.x + pBuf->next->size.w + 10;
  pBuf->size.y =
      pBuf->next->size.y + pBuf->next->size.h - pBuf->size.h + 2;

  /* ---------- */

  pBuf = create_horizontal(pTheme->Horiz, 30, 0);

  pBuf->action = horiz_taxrate_callback;
  set_wstate(pBuf, WS_NORMAL);

  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_LUX_SCROLLBAR, pBuf);

  pBuf->size.x =
      pWindow->size.x + 12 + (game.player_ptr->economic.luxury * 3);
  pBuf->size.y = y - pBuf->size.h + 2;

  /* --------------------- */
  /* it is important to leave 1 space at ending of this string */
  my_snprintf(cBuf, sizeof(cBuf), " %s : %d%%",
	      _("Science"), game.player_ptr->economic.science);

  pStr = create_str16_from_char(cBuf, 11);
  pStr->style |= TTF_STYLE_BOLD;

  pBuf =
      create_iconlabel(pSurf, pStr,
		       (WF_DRAW_THEME_TRANSPARENT | WF_ICON_UNDER_TEXT));
  pStr->style &= ~SF_CENTER;

  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_SCI_LABEL, pBuf);

  pBuf->size.x = pWindow->size.x + 10;
  pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h + 10;
  y = pBuf->size.y + pBuf->size.h;

  /* ---------- */

  my_snprintf(cBuf, sizeof(cBuf), _("Lock"));
  pStr = create_str16_from_char(cBuf, 10);
  pStr->style |= TTF_STYLE_BOLD;

  pBuf =
      create_textcheckbox((SDL_Client_Flags & CF_CHANGE_TAXRATE_SCI_BLOCK),
			  pStr, WF_DRAW_THEME_TRANSPARENT);

  pBuf->action = toggle_block_callback;
  set_wstate(pBuf, WS_NORMAL);

  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_SCI_BLOCK_CHECKBOX, pBuf);

  pBuf->size.x = pBuf->next->size.x + pBuf->next->size.w + 10;
  pBuf->size.y =
      pBuf->next->size.y + pBuf->next->size.h - pBuf->size.h + 2;

  /* ---------- */

  pBuf = create_horizontal(pTheme->Horiz, 30, 0);

  pBuf->action = horiz_taxrate_callback;
  set_wstate(pBuf, WS_NORMAL);

  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_SCI_SCROLLBAR, pBuf);

  pBuf->size.x =
      pWindow->size.x + 12 + (game.player_ptr->economic.science * 3);
  pBuf->size.y = y - pBuf->size.h + 2;

  /* ====================================================== */
  my_snprintf(cBuf, sizeof(cBuf), _("Update"));
  pStr = create_str16_from_char(cBuf, 12);

  pBuf = create_themeicon_button(pTheme->OK_Icon, pStr, 0);

  pBuf->action = apply_taxrate_dialog_callback;
  set_wstate(pBuf, WS_NORMAL);

  pBuf->size.x = pWindow->size.x + 10;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 10;

  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_OK_BUTTON, pBuf);
  /* ------------- */
  my_snprintf(cBuf, sizeof(cBuf), _("Cancel"));
  pStr = create_str16_from_char(cBuf, 12);

  pBuf = create_themeicon_button(pTheme->CANCEL_Icon, pStr, 0);

  pBuf->size.x = pWindow->size.x + pWindow->size.w - pBuf->size.w - 10;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 10;

  pBuf->action = popdown_taxrate_dialog_callback;
  set_wstate(pBuf, WS_NORMAL);

  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_CANCEL_BUTTON, pBuf);

  /* =========================================== */
  pBeginTaxRateDlgWidgetList = pBuf;

  /* redraw */
  redraw_group(pBuf, pWindow, Main.gui,0);

  flush_rect(pWindow->size);
}

/**************************************************************************
                                  Revolutions
**************************************************************************/
static struct SMALL_DLG *pRevolutionDlg = NULL;

/**************************************************************************
  ...
**************************************************************************/
static int revolution_dlg_ok_callback(struct GUI *pButton)
{
  struct packet_player_request packet;

  send_packet_player_request(&aconnection, &packet,
			     PACKET_PLAYER_REVOLUTION);

  popdown_window_group_dialog(pRevolutionDlg->pBeginWidgetList,
			      pRevolutionDlg->pEndWidgetList, Main.gui);
  FREE(pRevolutionDlg);
  SDL_Client_Flags |= CF_REVOLUTION;
  flush_dirty();
  return (-1);
}

/**************************************************************************
  ...
**************************************************************************/
static int revolution_dlg_cancel_callback(struct GUI *pCancel_Button)
{
  popdown_window_group_dialog(pRevolutionDlg->pBeginWidgetList,
			      pRevolutionDlg->pEndWidgetList, Main.gui);
  FREE(pRevolutionDlg);
  flush_dirty();
  return (-1);
}

/**************************************************************************
  ...
**************************************************************************/
static int move_revolution_dlg_callback(struct GUI *pWindow)
{
  if (move_window_group_dialog(pRevolutionDlg->pBeginWidgetList,
				    pWindow, Main.gui)) {
    flush_rect(pWindow->size);
  }
  return (-1);
}


/* ==================== Public ========================= */

/**************************************************************************
  Popup a dialog asking if the player wants to start a revolution.
**************************************************************************/
void popup_revolution_dialog(void)
{
  SDL_Surface *pLogo;
  struct SDL_String16 *pStr = NULL;
  struct GUI *pLabel = NULL;
  struct GUI *pWindow = NULL;
  struct GUI *pCancel_Button = NULL;
  struct GUI *pOK_Button = NULL;
  int ww;

  if(pRevolutionDlg) {
    return;
  }
  
  pRevolutionDlg = MALLOC(sizeof(struct SMALL_DLG));
    
  /* create ok button */
  pLogo = ZoomSurface(pTheme->OK_Icon, 0.7, 0.7, 1);
  SDL_SetColorKey(pLogo, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  get_first_pixel(pLogo));
  pOK_Button =
      create_themeicon_button_from_chars(pLogo, _("Revolution!"), 10,
					 WF_FREE_GFX);

  /* create cancel button */
  pLogo = ZoomSurface(pTheme->CANCEL_Icon, 0.7, 0.7, 1);
  SDL_SetColorKey(pLogo, SDL_SRCCOLORKEY | SDL_RLEACCEL,
		  get_first_pixel(pLogo));
  pCancel_Button =
      create_themeicon_button_from_chars(pLogo, _("Cancel"), 10,
					 WF_FREE_GFX);
  /* correct sizes */
  pCancel_Button->size.w += 6;

  /* create text label */
  pStr = create_str16_from_char(_("You say you wanna revolution?"), 10);
  pStr->style |= TTF_STYLE_BOLD;
  pStr->forecol.r = 255;
  pStr->forecol.g = 255;
  /* pStr->forecol.b = 255; */
  pLabel = create_iconlabel(NULL, pStr, 0);

  /* create window */
  pStr = create_str16_from_char(_("REVOLUTION!"), 12);
  pStr->style |= TTF_STYLE_BOLD;
  if ((pOK_Button->size.w + pCancel_Button->size.w + 30) >
      pLabel->size.w + 20) {
    ww = pOK_Button->size.w + pCancel_Button->size.w + 30;
  } else {
    ww = pLabel->size.w + 20;
  }

  pWindow = create_window(pStr, ww, pOK_Button->size.h + pLabel->size.h +
			  WINDOW_TILE_HIGH + 25, 0);

  /* set actions */
  pWindow->action = move_revolution_dlg_callback;
  pCancel_Button->action = revolution_dlg_cancel_callback;
  pOK_Button->action = revolution_dlg_ok_callback;

  /* set keys */
  pOK_Button->key = SDLK_RETURN;
  
  /* I make this hack to center label on window */
  pLabel->size.w = pWindow->size.w;

  /* set start positions */
  pWindow->size.x = (Main.screen->w - pWindow->size.w) / 2;
  pWindow->size.y = (Main.screen->h - pWindow->size.h) / 2;

  pOK_Button->size.x = pWindow->size.x + 10;
  pOK_Button->size.y = pWindow->size.y + pWindow->size.h -
      pOK_Button->size.h - 10;

  pCancel_Button->size.y = pOK_Button->size.y;
  pCancel_Button->size.x = pWindow->size.x + pWindow->size.w -
      pCancel_Button->size.w - 10;
  pLabel->size.x = pWindow->size.x;
  pLabel->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 5;

  /* create window background */
  pLogo = get_logo_gfx();
  if (resize_window
      (pWindow, pLogo, NULL, pWindow->size.w, pWindow->size.h)) {
    FREESURFACE(pLogo);
  }
  
  SDL_SetAlpha(pWindow->theme, 0x0, 0x0);
  /* enable widgets */
  set_wstate(pCancel_Button, WS_NORMAL);
  set_wstate(pOK_Button, WS_NORMAL);
  set_wstate(pWindow, WS_NORMAL);

  /* add widgets to main list */
  pRevolutionDlg->pEndWidgetList = pWindow;
  add_to_gui_list(ID_REVOLUTION_DLG_WINDOW, pWindow);
  add_to_gui_list(ID_REVOLUTION_DLG_LABEL, pLabel);
  add_to_gui_list(ID_REVOLUTION_DLG_CANCEL_BUTTON, pCancel_Button);
  add_to_gui_list(ID_REVOLUTION_DLG_OK_BUTTON, pOK_Button);
  pRevolutionDlg->pBeginWidgetList = pOK_Button;

  /* redraw */
  redraw_group(pOK_Button, pWindow, Main.gui, 0);
  flush_rect(pWindow->size);
}

/**************************************************************************
  Close the revolution dialog.
**************************************************************************/
void popdown_revolution_dialog(void)
{
  if(pRevolutionDlg) {
    popdown_window_group_dialog(pRevolutionDlg->pBeginWidgetList,
			      pRevolutionDlg->pEndWidgetList, Main.gui);
    FREE(pRevolutionDlg);
    flush_dirty();
  }
}

/**************************************************************************
                           Sellect Goverment Type
**************************************************************************/
static struct SMALL_DLG *pGov_Dlg = NULL;

/**************************************************************************
  ...
**************************************************************************/
static int government_dlg_callback(struct GUI *pGov_Button)
{
  struct packet_player_request packet;
  packet.government = MAX_ID - pGov_Button->ID;
  send_packet_player_request(&aconnection, &packet,
			     PACKET_PLAYER_GOVERNMENT);
  popdown_window_group_dialog(pGov_Dlg->pBeginWidgetList,
			      pGov_Dlg->pEndWidgetList, Main.gui);
  FREE(pGov_Dlg);
  SDL_Client_Flags |= CF_REVOLUTION;
  flush_dirty();
  return (-1);
}

/**************************************************************************
  ...
**************************************************************************/
static int move_government_dlg_callback(struct GUI *pWindow)
{
  if (move_window_group_dialog(pGov_Dlg->pBeginWidgetList, pWindow, Main.gui)) {
    flush_rect(pWindow->size);
  }
  return (-1);
}

/**************************************************************************
  Public -

  Popup a dialog asking the player what government to switch to (this
  happens after a revolution completes).
**************************************************************************/
void popup_government_dialog(void)
{
  SDL_Surface *pLogo = NULL;
  struct SDL_String16 *pStr = NULL;
  struct GUI *pGov_Button = NULL;
  struct GUI *pWindow = NULL;
  struct government *pGov = NULL;
  int i, j;
  Uint16 max_w = 0, max_h = 0;

  if (pGov_Dlg) {
    return;
  }

  assert(game.government_when_anarchy >= 0
	 && game.government_when_anarchy < game.government_count);

  pGov_Dlg = MALLOC(sizeof(struct SMALL_DLG));
  
  /* create window */
  pStr = create_str16_from_char(_("Choose Your New Government"), 12);
  pStr->style |= TTF_STYLE_BOLD;
  /* this win. size is temp. */
  pWindow = create_window(pStr, 10, 30, 0);
  pWindow->action = move_government_dlg_callback;
  pGov_Dlg->pEndWidgetList = pWindow;
  add_to_gui_list(ID_GOVERNMENT_DLG_WINDOW, pWindow);

  /* create gov. buttons */
  j = 0;
  for (i = 0; i < game.government_count; i++) {

    if (i == game.government_when_anarchy) {
      continue;
    }

    if (can_change_to_government(game.player_ptr, i)) {

      pGov = &governments[i];
      pStr = create_str16_from_char(pGov->name, 12);
      pGov_Button =
	  create_icon_button(GET_SURF(pGov->sprite), pStr, 0);
      pGov_Button->action = government_dlg_callback;

      max_w = MAX(max_w, pGov_Button->size.w);
      max_h = MAX(max_h, pGov_Button->size.h);
      
      /* ugly hack */
      add_to_gui_list((MAX_ID - i), pGov_Button);
      j++;

    }
  }

  pGov_Dlg->pBeginWidgetList = pGov_Button;

  max_w += 10;
  max_h += 4;

  /* set window start positions */
  pWindow->size.x = (Main.screen->w - pWindow->size.w) / 2;
  pWindow->size.y = (Main.screen->h - pWindow->size.h) / 2;

  /* create window background */
  pLogo = get_logo_gfx();
  if (resize_window(pWindow, pLogo, NULL, max_w + 20,
		    j * (max_h + 10) + WINDOW_TILE_HIGH + 6)) {
    FREESURFACE(pLogo);
  }
  
  pWindow->size.w = max_w + 20;
  pWindow->size.h = j * (max_h + 10) + WINDOW_TILE_HIGH + 6;
  
  /* set buttons start positions and size */
  j = 1;
  while (pGov_Button != pGov_Dlg->pEndWidgetList) {
    pGov_Button->size.w = max_w;
    pGov_Button->size.h = max_h;
    pGov_Button->size.x = pWindow->size.x + 10;
    pGov_Button->size.y = pWindow->size.y + pWindow->size.h -
	(j++) * (max_h + 10);
    set_wstate(pGov_Button, WS_NORMAL);

    pGov_Button = pGov_Button->next;
  }

  set_wstate(pWindow, WS_NORMAL);

  /* redraw */
  redraw_group(pGov_Dlg->pBeginWidgetList, pWindow, Main.gui, 0);

  flush_rect(pWindow->size);
}

/**************************************************************************
                                Nation Wizard
**************************************************************************/
static struct Nation_Window {

  struct GUI *pBeginWidgetList;
  struct GUI *pEndWidgetList;/* window */
  struct GUI *pWidgetListSeparator;

  SDL_String16 *nation_name_str16;

  /* title strings */
  Uint16 *title_str;

  /* SEX strings */
  Uint16 *male_str;
  Uint16 *female_str;

  /* leaders names */
  Uint16 **leaders;
  Uint8 max_leaders;
  
  Uint8 nation_city_style;	/* sellected city style */

  Uint8 selected_leader;	/* if not unique -> sellected leader */

  Uint8 leader_sex;		/* sellected leader sex */
  
  Sint8 nation;			/* sellected nation */

} *pNations;

static int nations_dialog_callback(struct GUI *pWindow);

static int nation_button_callback(struct GUI *pNation);

static int back_callback(struct GUI *pBack_Button);
static int start_callback(struct GUI *pStart_Button);
static int disconnect_callback(struct GUI *pButton);

static int next_name_callback(struct GUI *pNext_Button);
static int prev_name_callback(struct GUI *pPrev_Button);
static int change_sex_callback(struct GUI *pSex);

static int get_leader_sex(Nation_Type_id nation, Uint8 leader);
static void select_random_leader(Nation_Type_id nation);

static void create_city_styles_widgets(void);
static int create_nations_buttons(int *width , int *hight);

static void create_nations_dialog(void);
static void destroy_nations_dialog(void);
static void redraw_nations_dialog(void);

/**************************************************************************
  ...
**************************************************************************/
static int nations_dialog_callback(struct GUI *pWindow)
{
  return -1;
}

/**************************************************************************
   ...
**************************************************************************/
static int nation_button_callback(struct GUI *pNationButton)
{
  Uint16 *pTmp_str = NULL;
  
  pNations->nation = MAX_ID - pNationButton->ID;
  pNations->nation_city_style =
      get_nation_city_style(MAX_ID - pNationButton->ID);

  pNations->nation_name_str16->text = pNationButton->string16->text;

  disable(MAX_ID - 1000 - pNations->nation_city_style);
  
  hide_group(pNations->pWidgetListSeparator , pNations->pEndWidgetList->prev);
  show_group(pNations->pBeginWidgetList , pNations->pWidgetListSeparator);
  
  pTmp_str = pNations->pEndWidgetList->string16->text;
  pNations->pEndWidgetList->string16->text = pNations->title_str;
  pNations->title_str = pTmp_str;

  select_random_leader(pNations->nation);

  if (pNations->max_leaders > 1) {
    if (pNations->selected_leader) {
      set_wstate(pNations->pBeginWidgetList->next, WS_NORMAL);
    }

    if ((pNations->max_leaders - 1) != pNations->selected_leader) {
      set_wstate(pNations->pBeginWidgetList->next->next, WS_NORMAL);
    }
  }

  redraw_nations_dialog();

  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int start_callback(struct GUI *pStart_Button)
{
  struct packet_alloc_nation packet;
  char *pStr = convert_to_chars(pNations->pBeginWidgetList
  					->next->next->next->string16->text);

  /* perform a minimum of sanity test on the name */
  packet.nation_no = pNations->nation;	/*selected; */
  packet.is_male = pNations->leader_sex;	/*selected_sex; */
  packet.city_style = pNations->nation_city_style;

  sz_strlcpy(packet.name, pStr);
  FREE(pStr);

  if (!is_sane_name(packet.name)) {
    append_output_window(_("You must type a legal name."));
    pSellected_Widget = pStart_Button;
    set_wstate(pStart_Button, WS_SELLECTED);
    redraw_tibutton(pStart_Button);
    flush_rect(pStart_Button->size);
    return (-1);
  }

  /*packet.name[0] = toupper( packet.name[0] ); */

  send_packet_alloc_nation(&aconnection, &packet);

  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int back_callback(struct GUI *pBack_Button)
{
  Uint16 *pTmp_str = NULL;
  int i;
  
  enable(MAX_ID - 1000 - pNations->nation_city_style);
  
  pNations->nation = -1;
  pNations->nation_name_str16->text = NULL;

  for (i = 0; i < pNations->max_leaders; i++) {
    FREE(pNations->leaders[i]);
  }
  FREE(pNations->leaders);

  pNations->max_leaders = 0;

  hide_group(pNations->pBeginWidgetList , pNations->pWidgetListSeparator);
  show_group(pNations->pWidgetListSeparator , pNations->pEndWidgetList->prev);
  
  pTmp_str = pNations->pEndWidgetList->string16->text;
  pNations->pEndWidgetList->string16->text = pNations->title_str;
  pNations->title_str = pTmp_str;
  
  set_wstate(pNations->pBeginWidgetList->next, WS_DISABLED);
  set_wstate(pNations->pBeginWidgetList->next->next, WS_DISABLED);

  redraw_nations_dialog();

  return (-1);
}

/**************************************************************************
  ...
**************************************************************************/
static int next_name_callback(struct GUI *pNext)
{
  struct GUI *pEdit = pNations->pBeginWidgetList->next->next->next;
  struct GUI *pPrev = pNations->pBeginWidgetList->next;
    
  pNations->selected_leader++;
  
  /* change leadaer sex */
  pNations->leader_sex = get_leader_sex(pNations->nation,
					pNations->selected_leader);

  if (pNations->leader_sex) {
    pNations->pBeginWidgetList->string16->text = pNations->male_str;
  } else {
    pNations->pBeginWidgetList->string16->text = pNations->female_str;
  }

  /* change leadaer name */
  pEdit->string16->text =
      pNations->leaders[pNations->selected_leader];

  if ((pNations->max_leaders - 1) == pNations->selected_leader) {
    set_wstate(pNext, WS_DISABLED);
  }

  if (get_wstate(pPrev) == WS_DISABLED) {
    set_wstate(pPrev, WS_NORMAL);
  }

  if (!(get_wstate(pNext) == WS_DISABLED)) {
    pSellected_Widget = pNext;
    set_wstate(pNext, WS_SELLECTED);
  }

  redraw_edit(pEdit, Main.gui);
  redraw_tibutton(pPrev);
  redraw_tibutton(pNext);
  dirty_rect(pEdit->size.x - pPrev->size.w, pEdit->size.y,
		 pEdit->size.w + pPrev->size.w + pNext->size.w,
		 pEdit->size.h);
  
  redraw_ibutton(pNations->pBeginWidgetList);
  sdl_dirty_rect(pNations->pBeginWidgetList->size);
  
  flush_dirty();
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int prev_name_callback(struct GUI *pPrev)
{
  struct GUI *pEdit = pNations->pBeginWidgetList->next->next->next;
  struct GUI *pNext = pNations->pBeginWidgetList->next->next;
    
  pNations->selected_leader--;

  /* change leadaer sex */
  pNations->leader_sex = get_leader_sex(pNations->nation,
					pNations->selected_leader);

  if (pNations->leader_sex) {
    pNations->pBeginWidgetList->string16->text = pNations->male_str;
  } else {
    pNations->pBeginWidgetList->string16->text = pNations->female_str;
  }

  /* change leadaer name */
  pEdit->string16->text =
      pNations->leaders[pNations->selected_leader];

  if (!pNations->selected_leader) {
    set_wstate(pPrev, WS_DISABLED);
  }

  if (get_wstate(pNext) == WS_DISABLED) {
    set_wstate(pNext, WS_NORMAL);
  }

  if (!(get_wstate(pPrev) == WS_DISABLED)) {
    pSellected_Widget = pPrev;
    set_wstate(pPrev, WS_SELLECTED);
  }

  redraw_edit(pEdit, Main.gui);
  redraw_tibutton(pPrev);
  redraw_tibutton(pNext);
  dirty_rect(pEdit->size.x - pPrev->size.w, pEdit->size.y,
		 pEdit->size.w + pPrev->size.w + pNext->size.w,
		 pEdit->size.h);
  
  redraw_ibutton(pNations->pBeginWidgetList);
  sdl_dirty_rect(pNations->pBeginWidgetList->size);
  
  flush_dirty();
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int change_sex_callback(struct GUI *pSex)
{

  if (pNations->leader_sex) {
    pNations->leader_sex = 0;
    pSex->string16->text = pNations->female_str;
  } else {
    pNations->leader_sex = 1;
    pSex->string16->text = pNations->male_str;
  }

  pSellected_Widget = pSex;
  set_wstate(pSex, WS_SELLECTED);

  redraw_ibutton(pSex);
  flush_rect(pSex->size);

  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int disconnect_callback(struct GUI *pButton)
{
  destroy_nations_dialog();
  disconnect_from_server();
  return -1;
}

static int city_style_callback(struct GUI *pWidget)
{
  struct GUI *pGUI = get_widget_pointer_form_main_list(MAX_ID - 1000 -
					    pNations->nation_city_style);
  set_wstate(pGUI , WS_NORMAL);
  redraw_icon2(pGUI);
  sdl_dirty_rect(pGUI->size);
  
  set_wstate(pWidget , WS_DISABLED);
  redraw_icon2(pWidget);
  sdl_dirty_rect(pWidget->size);
  
  pNations->nation_city_style = MAX_ID - 1000 - pWidget->ID;
  
  flush_dirty();
  pSellected_Widget = NULL;
  return -1;
}

/* =========================================================== */

/**************************************************************************
  ...
**************************************************************************/
static int get_leader_sex(Nation_Type_id nation, Uint8 leader)
{
  int dim;
  struct leader *leaders = get_nation_leaders(nation, &dim);
  return leaders[leader].is_male;
}

/**************************************************************************
  Selectes a leader and the appropriate sex. Updates the gui elements
  and the selected_* variables.
**************************************************************************/
static void select_random_leader(Nation_Type_id nation)
{
  int j, dim;
  struct leader *leaders;
  struct GUI *pChange_Sex_Button = pNations->pBeginWidgetList;
  struct GUI *pName_edit = pNations->pBeginWidgetList->next->next->next;
    
  leaders = get_nation_leaders(nation, &dim);
  pNations->max_leaders = dim;
  pNations->leaders = CALLOC(pNations->max_leaders, sizeof(Uint16 *));

  for (j = 0; j < pNations->max_leaders; j++) {
    pNations->leaders[j] = convert_to_utf16(leaders[j].name);
  }

  pNations->selected_leader = myrand(pNations->max_leaders);
  pName_edit->string16->text =
      pNations->leaders[pNations->selected_leader];

  /* initialize leader sex */
  pNations->leader_sex = leaders[pNations->selected_leader].is_male;

  if (pNations->leader_sex) {
    pChange_Sex_Button->string16->text = pNations->male_str;
  } else {
    pChange_Sex_Button->string16->text = pNations->female_str;
  }
}

/**************************************************************************
  ...
**************************************************************************/
static void create_city_styles_widgets(void)
{
  int i = 0;
  Uint16 len = 0;
  struct GUI *pEnd = NULL, *pWidget;
  Sint16 start_y = pNations->pEndWidgetList->size.y +
			  pNations->pEndWidgetList->size.h - 130;
  
  while (i < game.styles_count) {
    if (city_styles[i].techreq == A_NONE) {
      pWidget = create_icon2(GET_SURF(sprites.city.tile[i][2]),
					      WF_DRAW_THEME_TRANSPARENT);
      pEnd = pWidget;
      
      pWidget->action = city_style_callback;
      set_wstate(pWidget, WS_NORMAL);
      
      len = pWidget->size.w;
      add_to_gui_list(MAX_ID - 1000 - i, pWidget);
      set_wflag(pWidget, WF_HIDDEN);
      i++;
      break;
    }
    i++;
  }

  len += 3;

  for (i = i; (i < game.styles_count && i < 64); i++) {
    if (city_styles[i].techreq == A_NONE) {
      pWidget = create_icon2(GET_SURF(sprites.city.tile[i][2]),
					      WF_DRAW_THEME_TRANSPARENT);
      pWidget->action = city_style_callback;
      set_wstate(pWidget, WS_NORMAL);
      len += (pWidget->size.w + 3);
      set_wflag(pWidget, WF_HIDDEN);
      add_to_gui_list(MAX_ID - 1000 - i, pWidget);
    }
  }
  
  pEnd->size.x = pNations->pEndWidgetList->size.x +
		  (pNations->pEndWidgetList->size.w - len) / 2;
  pEnd->size.y = start_y;
  pEnd = pEnd->prev;
  
  while(pEnd) {
    pEnd->size.x = pEnd->next->size.x + pEnd->next->size.w + 3;
    pEnd->size.y = start_y;
    pEnd = pEnd->prev;
  }
  
}

/**************************************************************************
  ...
**************************************************************************/
static int create_nations_buttons(int *width , int *hight)
{
  int i , start_x , x , y;
  int w = 0, h = 0;
  SDL_Surface *pTmp_flag = NULL, *pTmp_flag_zoomed = NULL;
  struct GUI *pBeginNationButtons = NULL , *pWidget;

  /* init list */
  pTmp_flag = GET_SURF(get_nation_by_idx(0)->flag_sprite);
  pTmp_flag = make_flag_surface_smaler(pTmp_flag);
  pTmp_flag_zoomed = ZoomSurface(pTmp_flag, 0.5, 0.5, 0);
  FREESURFACE(pTmp_flag);
  pBeginNationButtons = pWidget =
	create_icon_button(pTmp_flag_zoomed,
		create_str16_from_char(get_nation_name_plural(0),11), 0);
  
  pWidget->size.h += 4;
  set_wflag(pWidget, WF_FREE_GFX);
  set_wstate(pWidget, WS_NORMAL);
  clear_wflag(pWidget, WF_DRAW_FRAME_AROUND_WIDGET);
  pWidget->action = nation_button_callback;

  w = pWidget->size.w;
  h = pWidget->size.h;

  add_to_gui_list(MAX_ID, pWidget);

  /* fill list */
  for (i = 1; i < game.playable_nation_count; i++) {
    pTmp_flag = GET_SURF(get_nation_by_idx(i)->flag_sprite);

    pTmp_flag = make_flag_surface_smaler(pTmp_flag);
    pTmp_flag_zoomed = ZoomSurface(pTmp_flag, 0.5, 0.5, 0);
    FREESURFACE(pTmp_flag);

    pWidget = create_icon_button(pTmp_flag_zoomed,
		  create_str16_from_char(get_nation_name_plural(i), 11), 0);

    pWidget->size.h += 4;
    set_wflag(pWidget, WF_FREE_GFX);
    clear_wflag(pWidget, WF_DRAW_FRAME_AROUND_WIDGET);
    set_wstate(pWidget, WS_NORMAL);
    
    pWidget->action = nation_button_callback;

    w = MAX(w, pWidget->size.w);
    h = MAX(h, pWidget->size.h);
    
    add_to_gui_list(MAX_ID - i, pWidget);

    pTmp_flag = NULL;
  }

  /* set window w, h and start position */
  *width = 20 + 4 * w;
  
  if ( i & 0x01 ) {
    *hight = 65 + ((i + 1) / 4) * h;
  } else {
    *hight = 65 + (i / 4) * h;
  }
  
  pNations->pEndWidgetList->size.x = (Main.gui->w - *width) /2;
  pNations->pEndWidgetList->size.y = (Main.gui->h - *hight) /2;
  
  x = start_x = pNations->pEndWidgetList->size.x + 10;
  y = pNations->pEndWidgetList->size.y + 25;
  
  pWidget = pBeginNationButtons;
  i = 1;
  /* correct size and set start positions*/
  while (pWidget) {

    pWidget->size.w = w;
    pWidget->size.h = h;

    pWidget->size.x = x;
    pWidget->size.y = y;
    x += w;

    if (x > start_x + w * 3) {
      x = start_x;
      y += h;
      i++;
    }
    
    pWidget = pWidget->prev;
  }
    
  return i;
}


/**************************************************************************
  ...
**************************************************************************/
static void create_nations_dialog(void)
{
  SDL_Surface *pBuf = NULL;
  struct GUI *pWindow, *pWidget;
  int w = 0;
  int h = 0;

  pNations = MALLOC(sizeof(struct Nation_Window));

  pNations->nation = -1;
  
  pNations->title_str =
      convert_to_utf16(_("Nation Wizzard : Nation Settings"));

  pNations->nation_name_str16 = create_string16(NULL, 24);
  pNations->nation_name_str16->forecol =
      *get_game_colorRGB(COLOR_STD_WHITE);

  /* create window widget */
  pWindow =
     create_window(create_str16_from_char
	(_("Nation Wizzard : What nation will you be?"), 12), w, h, 0);
  pWindow->string16->style |= TTF_STYLE_BOLD;
  
  pWindow->action = nations_dialog_callback;
  set_wstate(pWindow, WS_NORMAL);

  pNations->pEndWidgetList = pWindow;
  add_to_gui_list(ID_NATION_WIZARD_WINDOW, pWindow);
  /* ---------------------------- */
  
  /* Create Nations Buttons and set window start pos*/
  create_nations_buttons(&w , &h);
  
  pBuf = get_logo_gfx();
  if (resize_window(pWindow, pBuf, NULL, w, h)) {
    FREESURFACE(pBuf);
  }
  
  SDL_SetAlpha(pWindow->theme, 0x0, 0x0);
  
  /* create disconnection button */
  pWidget =
      create_themeicon_button_from_chars(pTheme->BACK_Icon,
					 _("Disconnect"), 12, 0);
  pWidget->action = disconnect_callback;
  set_wstate(pWidget, WS_NORMAL);
  
  pWidget->size.x = pWindow->size.x + pWindow->size.w - pWidget->size.w - 10;
  pWidget->size.y = pWindow->size.y + pWindow->size.h - pWidget->size.h - 7;
  
  add_to_gui_list(ID_NATION_WIZARD_DISCONNECT_BUTTON, pWidget);
  
  pNations->pWidgetListSeparator = pWidget;  

  /* create back button */
  pWidget =
      create_themeicon_button_from_chars(pTheme->BACK_Icon, _("Back"), 12, 0);
  pWidget->action = back_callback;

  pWidget->size.x = pWindow->size.x + 10;
  pWidget->size.y = pWindow->size.y + pWindow->size.h - pWidget->size.h - 7;
  set_wflag(pWidget, WF_HIDDEN);
  set_wstate(pWidget, WS_NORMAL);
  add_to_gui_list(ID_NATION_WIZARD_BACK_BUTTON, pWidget);

  /* create start button */
  pWidget =
      create_themeicon_button_from_chars(pTheme->FORWARD_Icon,
					 _("Start"), 12, 0);
  pWidget->action = start_callback;

  pWidget->size.w += 60;
  pWidget->size.x = pWindow->size.x + (pWindow->size.w - pWidget->size.w) / 2;
  pWidget->size.y = pWindow->size.y + pWindow->size.h - pWidget->size.h - 10;
  set_wflag(pWidget, WF_HIDDEN);
  set_wstate(pWidget, WS_NORMAL);
  add_to_gui_list(ID_NATION_WIZARD_START_BUTTON, pWidget);

  /* ------- city style toggles ------- */

  create_city_styles_widgets();

  /* create leader name edit */
  pWidget = create_edit_from_unichars(NULL, NULL, 16, 200, 0);
  pWidget->size.h = 30;

  pWidget->size.x = pWindow->size.x + (pWindow->size.w - pWidget->size.w) / 2;
  pWidget->size.y = pWindow->size.y + 120;		  
  set_wflag(pWidget, WF_HIDDEN);
  set_wstate(pWidget, WS_NORMAL);
  add_to_gui_list(ID_NATION_WIZARD_LEADER_NAME_EDIT, pWidget);

  /* create next leader name button */
  pWidget = create_themeicon_button(pTheme->R_ARROW_Icon, NULL, 0);
  pWidget->action = next_name_callback;
  set_wflag(pWidget, WF_HIDDEN);
  clear_wflag(pWidget, WF_DRAW_FRAME_AROUND_WIDGET);
  add_to_gui_list(ID_NATION_WIZARD_NEXT_LEADER_NAME_BUTTON, pWidget);
  pWidget->size.h = pWidget->next->size.h;
  pWidget->size.x = pWidget->next->size.x + pWidget->next->size.w;
  pWidget->size.y = pWidget->next->size.y;

  /* create prev leader name button */
  pWidget = create_themeicon_button(pTheme->L_ARROW_Icon, NULL, 0);
  pWidget->action = prev_name_callback;
  set_wflag(pWidget, WF_HIDDEN);
  clear_wflag(pWidget, WF_DRAW_FRAME_AROUND_WIDGET);
  add_to_gui_list(ID_NATION_WIZARD_PREV_LEADER_NAME_BUTTON, pWidget);
  pWidget->size.h = pWidget->next->size.h;
  pWidget->size.x = pWidget->next->next->size.x - pWidget->size.w;
  pWidget->size.y = pWidget->next->size.y;

  /* change sex button */
  pWidget = create_icon_button_from_unichar(NULL, NULL, 14, 0);
  pWidget->action = change_sex_callback;
  pWidget->size.w = 100;
  pWidget->size.h = 22;
  set_wflag(pWidget, WF_HIDDEN);
  set_wstate(pWidget, WS_NORMAL);
  pWidget->size.y = pWindow->size.y + 180;
  pWidget->size.x = pWindow->size.x + (pWindow->size.w - pWidget->size.w) / 2;
  
  /* add to main widget list */

  add_to_gui_list(ID_NATION_WIZARD_CHANGE_SEX_BUTTON, pWidget);

  pNations->pBeginWidgetList = pWidget;
  
  pNations->male_str = convert_to_utf16(_("Male"));
  pNations->female_str = convert_to_utf16(_("Female"));

}

/**************************************************************************
  ...
**************************************************************************/
static void destroy_nations_dialog(void)
{
  int i;

  /* Free widgets */
  pNations->nation_name_str16->text = NULL;
  
  /* change sex button */
  pNations->pBeginWidgetList->string16->text = NULL;
  
  /* leader name edit */
  pNations->pBeginWidgetList->next->next->next->string16->text = NULL;
  
  popdown_window_group_dialog(pNations->pBeginWidgetList,
			  pNations->pEndWidgetList, Main.gui);
  
  /* Free pNations */
  FREE(pNations->male_str);
  FREE(pNations->female_str);

  if (pNations->leaders) {
    for (i = 0; i < pNations->max_leaders; i++)
      FREE(pNations->leaders[i]);
    FREE(pNations->leaders);
  }

  FREESTRING16(pNations->nation_name_str16);
  FREE(pNations->title_str);

  FREE(pNations);
}

/**************************************************************************
...
**************************************************************************/
static void redraw_nations_dialog(void)
{
  SDL_Surface *pTmp = NULL;
  struct GUI *pWindow = pNations->pEndWidgetList;
  
  redraw_window(pWindow, Main.gui);
  
  putframe(Main.gui, pWindow->size.x , pWindow->size.y ,
  		pWindow->size.x + pWindow->size.w - 1,
		pWindow->size.y + pWindow->size.h - 1, 0xffffffff);
  
  if (pNations->nation != -1) {
    SDL_Rect dst;
    redraw_group(pNations->pBeginWidgetList ,
		    pNations->pWidgetListSeparator, Main.gui, 0);
  
    
    pTmp = GET_SURF(get_nation_by_idx(pNations->nation)->flag_sprite);
    pTmp = ZoomSurface(pTmp, 3.0, 3.0, 1);
    SDL_SetColorKey(pTmp, SDL_SRCCOLORKEY, get_first_pixel(pTmp));

    dst.x = pWindow->size.x + 40;
    dst.y = pWindow->size.y + 20;
    SDL_BlitSurface(pTmp, NULL, Main.gui, &dst);
    
    FREESURFACE(pTmp);

    pTmp = create_text_surf_from_str16(pNations->nation_name_str16);
    
    dst.x = pWindow->size.x + (pWindow->size.w - pTmp->w) / 2;
    dst.y = pWindow->size.y + 50;
    SDL_BlitSurface(pTmp, NULL, Main.gui, &dst);
    
    FREESURFACE(pTmp);

    draw_frame(Main.gui, pNations->pBeginWidgetList->next->size.x - FRAME_WH,
	       pNations->pBeginWidgetList->next->size.y - FRAME_WH,
	       pNations->pBeginWidgetList->next->size.w +
	       pNations->pBeginWidgetList->next->next->size.w +
	       pNations->pBeginWidgetList->next->next->next->size.w +
	       DOUBLE_FRAME_WH,
	       pNations->pBeginWidgetList->next->size.h + DOUBLE_FRAME_WH);

  } else {
    redraw_group(pNations->pWidgetListSeparator, pWindow->prev, Main.gui, 0);
  }
  
  flush_rect(pWindow->size);
}

/**************************************************************************
  Popup the nation selection dialog.
**************************************************************************/
void popup_races_dialog(void)
{
  create_nations_dialog();
  redraw_nations_dialog();
}

/**************************************************************************
  Close the nation selection dialog.  This should allow the user to
  (at least) select a unit to activate.
**************************************************************************/
void popdown_races_dialog(void)
{
  if (pNations) {
    destroy_nations_dialog();
  }
}

/**************************************************************************
  In the nation selection dialog, make already-taken nations unavailable.
  This information is contained in the packet_nations_used packet.
**************************************************************************/
void races_toggles_set_sensitive(struct packet_nations_used *packet)
{
  int i, nation , back = 0;
  struct GUI *pNat;

  if(packet->num_nations_used) {
  
    freelog(LOG_DEBUG, "%d nations used:", packet->num_nations_used);
    for (i = 0; i < packet->num_nations_used; i++) {
      nation = packet->nations_used[i];

      freelog(LOG_DEBUG,"  [%d]: %d = %s", i, nation, get_nation_name(nation));

      pNat = get_widget_pointer_form_main_list(MAX_ID - nation);
      set_wstate(pNat , WS_DISABLED);
    
      if (nation == pNations->nation) {
        back = 1;
      }
    }
  
    if (back) {
      back_callback(NULL);
    } else {
      redraw_nations_dialog();
    }
  }
}
