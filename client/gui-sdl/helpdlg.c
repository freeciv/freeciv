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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL/SDL.h>

#include "city.h"
#include "fcintl.h"
#include "log.h"
#include "game.h"
#include "genlist.h"
#include "government.h"

#include "gui_mem.h"

#include "shared.h"
#include "tech.h"
#include "unit.h"
#include "map.h"
#include "support.h"
#include "version.h"

#include "climisc.h"
#include "colors.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_string.h"
#include "gui_stuff.h"
#include "gui_zoom.h"
#include "helpdata.h"
#include "mapview.h"
#include "tilespec.h"
#include "gui_tilespec.h"
#include "repodlgs.h"

#include "helpdlg.h"




static struct ADVANCED_DLG *pHelpDlg = NULL;
  
struct TECHS_BUTTONS {
  struct GUI *pTargets[6], *pSub_Targets[6];
  struct GUI *pReq[2], *pSub_Req[4];
  struct GUI *pDock;
  bool show_tree;
  bool show_full_tree;
};

struct UNITS_BUTTONS {
  struct GUI *pObs;
  struct GUI *pReq;
  struct GUI *pDock;
};

static enum help {
  TECH,
  UNITS,
  IMPR,
  GOV,
  TERRAINS,
  TEXT,
  HELP_LAST
} current_help_dlg = HELP_LAST;

/* HACK: we use a static string for convenience. */
static char long_buffer[64000];

static int change_tech_callback(struct GUI *pWidget);
  
/**************************************************************************
  Popup the help dialog to get help on the given string topic.  Note that
  the toppic may appear in multiple sections of the help (it may be both
  an improvement and a unit, for example).

  The string will be untranslated.
**************************************************************************/
void popup_help_dialog_string(const char *item)
{
  popup_help_dialog_typed(_(item), HELP_ANY);
}

/**************************************************************************
  Popup the help dialog to display help on the given string topic from
  the given section.

  The string will be translated.
**************************************************************************/
void popup_help_dialog_typed(const char *item, enum help_page_type eHPT)
{
  freelog(LOG_DEBUG, "popup_help_dialog_typed : PORT ME");
}

/**************************************************************************
  Close the help dialog.
**************************************************************************/
void popdown_help_dialog(void)
{
  if (pHelpDlg)
  {
    popdown_window_group_dialog(pHelpDlg->pBeginWidgetList,
					   pHelpDlg->pEndWidgetList);
    FREE(pHelpDlg->pScroll);
    FREE(pHelpDlg);
    current_help_dlg = HELP_LAST;
  }
}

static int help_dlg_window_callback(struct GUI *pWindow)
{
  return -1;
}

static int exit_help_dlg_callback(struct GUI *pWidget)
{
  popdown_help_dialog();
  flush_dirty();
  return -1;
}

/* =============================================== */

static int change_gov_callback(struct GUI *pWidget)
{
  popup_gov_info(MAX_ID - pWidget->ID);
  return -1;
}

void popup_gov_info(int gov)
{ 
}


static int change_impr_callback(struct GUI *pWidget)
{
  popup_impr_info(MAX_ID - pWidget->ID);
  return -1;
}

static void redraw_impr_info_dlg(void)
{
  struct GUI *pWindow = pHelpDlg->pEndWidgetList;
  struct UNITS_BUTTONS *pStore = (struct UNITS_BUTTONS *)pWindow->data.ptr;
  SDL_Rect dst;
  SDL_Color color = {255, 255, 255, 64};
  
  redraw_group(pWindow->prev, pWindow, FALSE);
    
  dst.x = pStore->pDock->prev->size.x - 10;
  dst.y = pStore->pDock->prev->size.y - 10;
  dst.w = pWindow->size.w - (dst.x - pWindow->size.x) - 10; 
  dst.h = pWindow->size.h - (dst.y - pWindow->size.y) - 10; 
  SDL_FillRectAlpha(pWindow->dst, &dst, &color);
  putframe(pWindow->dst, dst.x , dst.y , dst.x + dst.w , dst.y + dst.h, 0xFF000000);
  
  /*------------------------------------- */
  redraw_group(pHelpDlg->pBeginWidgetList, pWindow->prev->prev, FALSE);
  flush_rect(pWindow->size);
}


void popup_impr_info(Impr_Type_id impr)
{ 
  struct GUI *pBuf, *pHelpText = NULL;
  struct GUI *pDock;
  struct GUI *pWindow;
  struct UNITS_BUTTONS *pStore;
  SDL_String16 *pStr;
  SDL_Surface *pSurf;
  int w, h, start_x, start_y;
  bool created, text = FALSE;
  int width = 0;
  struct impr_type *pImpr_type;
  char buffer[64000];
  
  if(current_help_dlg != IMPR)
  {
    popdown_help_dialog();
  }
  
  if (!pHelpDlg)
  {
    SDL_Surface *pText, *pBack, *pTmp;
    SDL_Rect dst;
    
    current_help_dlg = IMPR;
    created = TRUE;
    pHelpDlg = MALLOC(sizeof(struct ADVANCED_DLG));
    pStore = MALLOC(sizeof(struct UNITS_BUTTONS));
    
    pStr = create_str16_from_char(_("Help : Improvement"), 12);
    pStr->style |= TTF_STYLE_BOLD;

    pWindow = create_window(NULL, pStr, 400, 400, WF_FREE_DATA);
    pWindow->action = help_dlg_window_callback;
    set_wstate(pWindow , FC_WS_NORMAL);
    pWindow->data.ptr = (void *)pStore;
    add_to_gui_list(ID_WINDOW, pWindow);
    pHelpDlg->pEndWidgetList = pWindow;
    /* ------------------ */
    
    /* exit button */
    pBuf = create_themeicon(pTheme->Small_CANCEL_Icon, pWindow->dst,
  			  			WF_DRAW_THEME_TRANSPARENT);
  
    //w += pBuf->size.w + 10;
    pBuf->action = exit_help_dlg_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
    pBuf->key = SDLK_ESCAPE;
  
    add_to_gui_list(ID_BUTTON, pBuf);

    /* ------------------ */
    pDock = pBuf;
    
    pStr = create_string16(NULL, 0, 10);
    pStr->style |= (TTF_STYLE_BOLD | SF_CENTER);
    
    pTmp = create_surf(140, 40, SDL_SWSURFACE);
    pText = SDL_DisplayFormatAlpha(pTmp);
    FREESURFACE(pTmp);
    pTmp = pText;
    
    SDL_FillRect(pTmp, NULL,
	SDL_MapRGBA(pTmp->format, 255, 255, 255, 128));
    putframe(pTmp, 0,0, pTmp->w - 1, pTmp->h - 1, 0xFF000000);
    
    h = 0;
    impr_type_iterate(type)
    {
      pBack = SDL_DisplayFormatAlpha(pTmp);
      copy_chars_to_string16(pStr, get_improvement_name(type));
      pText = create_text_surf_smaller_that_w(pStr, 100 - 4);
      /* draw name tech text */ 
      dst.x = 40 + (pBack->w - pText->w - 40) / 2;
      dst.y = (pBack->h - pText->h) / 2;
      SDL_BlitSurface(pText, NULL, pBack, &dst);
      FREESURFACE(pText);
    
      /* draw tech icon */
      pText = GET_SURF(get_improvement_type(type)->sprite);
      dst.x = 5;
      dst.y = (pBack->h - pText->h) / 2;
      SDL_BlitSurface(pText, NULL, pBack, &dst);
      
      pBuf = create_icon2(pBack, pWindow->dst,
      		WF_FREE_THEME | WF_DRAW_THEME_TRANSPARENT);

      set_wstate(pBuf, FC_WS_NORMAL);
      pBuf->action = change_impr_callback;
      add_to_gui_list(MAX_ID - type, pBuf);
      
      if (++h > 10)
      {
        set_wflag(pBuf, WF_HIDDEN);
      }

    } impr_type_iterate_end;
    
    FREESURFACE(pTmp);
    
    pHelpDlg->pBeginActiveWidgetList = pBuf;
    pHelpDlg->pEndActiveWidgetList = pDock->prev;
    pHelpDlg->pBeginWidgetList = pBuf;/* IMPORTANT */
    
    if (h > 10) {
      pHelpDlg->pActiveWidgetList = pDock->prev;
      width = create_vertical_scrollbar(pHelpDlg, 1, 10, TRUE, TRUE);
    }
        
    
    /* toggle techs list button */
    pBuf = create_themeicon_button_from_chars(pTheme->UP_Icon,
	      pWindow->dst,  _("Improvements"), 10, 0);
    /*pBuf->action = toggle_full_tree_mode_in_help_dlg_callback;
   if (pStore->show_tree)
    {
      set_wstate(pBuf, FC_WS_NORMAL);
    }
*/    
    pBuf->size.w = 160;
    pBuf->size.h = 15;
    pBuf->string16->fgcol = *get_game_colorRGB(COLOR_STD_WHITE);
    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
  
    add_to_gui_list(ID_BUTTON, pBuf);
      
    pDock = pBuf;
    pStore->pDock = pDock;
  } else {
    created = FALSE;
    width = (pHelpDlg->pScroll ? pHelpDlg->pScroll->pUp_Left_Button->size.w: 0);
    pWindow = pHelpDlg->pEndWidgetList;
    pStore = (struct UNITS_BUTTONS *)pWindow->data.ptr;
    pDock = pStore->pDock;
    
    /* del. all usless widget */
    if (pDock  != pHelpDlg->pBeginWidgetList)
    {
      del_group_of_widgets_from_gui_list(pHelpDlg->pBeginWidgetList,
				       pDock->prev);
      pHelpDlg->pBeginWidgetList = pDock;
    }
  }
  
  pImpr_type = get_improvement_type(impr);
  
  pBuf= create_iconlabel_from_chars(
	  ZoomSurface(GET_SURF(pImpr_type->sprite), 3.0, 3.0, 1),
	  pWindow->dst, get_impr_name_ex(NULL, impr),
					      24, WF_FREE_THEME);

  pBuf->ID = ID_LABEL;
  DownAdd(pBuf, pDock);
  pDock = pBuf;
  
  if (impr != B_CAPITAL)
  {
    sprintf(buffer, "%s %d", N_("Cost:"), impr_build_shield_cost(impr));
    pBuf = create_iconlabel_from_chars(NULL,
		    pWindow->dst, buffer, 12, 0);
    pBuf->ID = ID_LABEL;
    DownAdd(pBuf, pDock);
    pDock = pBuf;
    if (!is_wonder(impr))
    {
      sprintf(buffer, "%s %d", N_("Upkeep:"), pImpr_type->upkeep);
      pBuf = create_iconlabel_from_chars(NULL,
		    pWindow->dst, buffer, 12, 0);
      pBuf->ID = ID_LABEL;
      DownAdd(pBuf, pDock);
      pDock = pBuf;
    }
  }
  pBuf = create_iconlabel_from_chars(NULL,
		    pWindow->dst, N_("Requirement:"), 12, 0);
  pBuf->ID = ID_LABEL;
  DownAdd(pBuf, pDock);
  pDock = pBuf;
  
  if(pImpr_type->tech_req==A_LAST || pImpr_type->tech_req==A_NONE)
  {
    pBuf = create_iconlabel_from_chars(NULL,
		    pWindow->dst, _("None"), 12, 0);
    pBuf->ID = ID_LABEL;
  } else {
    pBuf = create_iconlabel_from_chars(NULL, pWindow->dst,
	  advances[pImpr_type->tech_req].name, 12,
			  WF_DRAW_THEME_TRANSPARENT);
    pBuf->ID = MAX_ID - pImpr_type->tech_req;
    pBuf->string16->fgcol = *get_tech_color(pImpr_type->tech_req);
    pBuf->action = change_tech_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
  }
  DownAdd(pBuf, pDock);
  pDock = pBuf;
  pStore->pReq = pBuf;
  
  pBuf = create_iconlabel_from_chars(NULL,
		    pWindow->dst, N_("Obsolete by:"), 12, 0);
  pBuf->ID = ID_LABEL;
  DownAdd(pBuf, pDock);
  pDock = pBuf;
  
  if(pImpr_type->obsolete_by==A_LAST || pImpr_type->obsolete_by==A_NONE)
  {
    pBuf = create_iconlabel_from_chars(NULL,
		    pWindow->dst, _("None"), 12, 0);
    pBuf->ID = ID_LABEL;
  } else {
    pBuf = create_iconlabel_from_chars(NULL, pWindow->dst,
	      advances[pImpr_type->obsolete_by].name, 12,
			  WF_DRAW_THEME_TRANSPARENT);
    pBuf->ID = MAX_ID - pImpr_type->obsolete_by;
    pBuf->string16->fgcol = *get_tech_color(pImpr_type->obsolete_by);
    pBuf->action = change_tech_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
  }
  DownAdd(pBuf, pDock);
  pDock = pBuf;
  pStore->pObs = pBuf;
    
  start_x = (FRAME_WH + 1 + width + pHelpDlg->pEndActiveWidgetList->size.w + 20);
  
  buffer[0] = '\0';
  helptext_building(buffer, sizeof(buffer), impr, NULL);
  if (buffer[0] != '\0')
  {
    SDL_String16 *pStr = create_str16_from_char(buffer, 12);
    convert_string_to_const_surface_width(pStr,	640 - start_x - 20);
    pBuf = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    pBuf->ID = ID_LABEL;
    DownAdd(pBuf, pDock);
    pDock = pBuf;
    pHelpText = pBuf;
    text = TRUE;
  }
  
  pHelpDlg->pBeginWidgetList = pBuf;
  
  /* --------------------------------------------------------- */ 
  if (created)
  {
    w = 640;
    h = 480;
    pWindow->size.x = (Main.screen->w - w) / 2;
    pWindow->size.y = (Main.screen->h - h) / 2;
  
    /* alloca window theme and win background buffer */
    pSurf = get_logo_gfx();
    if (resize_window(pWindow, pSurf, NULL, w, h))
    {
      FREESURFACE(pSurf);
    }

    /* exit button */
    pBuf = pWindow->prev;
    pBuf->size.x = pWindow->size.x + pWindow->size.w-pBuf->size.w-FRAME_WH-1;
    pBuf->size.y = pWindow->size.y + 1;
  
    /* toggle button */
    pStore->pDock->size.x = pWindow->size.x + FRAME_WH;
    pStore->pDock->size.y = pWindow->size.y +  WINDOW_TILE_HIGH + 1;
    
    h = setup_vertical_widgets_position(1, pWindow->size.x + FRAME_WH + width,
		  pWindow->size.y + WINDOW_TILE_HIGH + 17, 0, 0,
		  pHelpDlg->pBeginActiveWidgetList,
  		  pHelpDlg->pEndActiveWidgetList);
    
    if (pHelpDlg->pScroll)
    {
      setup_vertical_scrollbar_area(pHelpDlg->pScroll,
	pWindow->size.x + FRAME_WH,
    	pWindow->size.y + WINDOW_TILE_HIGH + 17,
    	h, FALSE);
    }
  }
  
  /* unittype  icon and label */
  pBuf = pStore->pDock->prev;
  pBuf->size.x = pWindow->size.x + start_x;
  pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 20;
  start_y = pBuf->size.y + pBuf->size.h + 10;
  
  if (impr != B_CAPITAL)
  {
    pBuf = pBuf->prev;
    pBuf->size.x = pWindow->size.x + start_x;
    pBuf->size.y = start_y;
    if (!is_wonder(impr))
    {
      pBuf = pBuf->prev;
      pBuf->size.x = pBuf->next->size.x + pBuf->next->size.w + 20;
      pBuf->size.y = start_y;
    }
    start_y += pBuf->size.h;
  }
  
  pBuf = pStore->pReq->next;
  pBuf->size.x = pWindow->size.x + start_x;
  pBuf->size.y = start_y;
  
  pStore->pReq->size.x = pBuf->size.x + pBuf->size.w + 5;
  pStore->pReq->size.y = start_y;
  
  if (pStore->pObs)
  {  
    pBuf = pStore->pObs->next;
    pBuf->size.x = pStore->pReq->size.x + pStore->pReq->size.w + 10;
    pBuf->size.y = start_y;
  
    pStore->pObs->size.x = pBuf->size.x + pBuf->size.w + 5;
    pStore->pObs->size.y = start_y;
    start_y += pStore->pObs->size.h;
  }
  
  start_y += 30;
  if (text)
  {
    pHelpText->size.x = pWindow->size.x + start_x;
    pHelpText->size.y = start_y;
  }
    
  
  redraw_impr_info_dlg();
}

/* ============================================ */
static int change_unit_callback(struct GUI *pWidget)
{
  popup_unit_info(MAX_ID - pWidget->ID);
  return -1;
}

static void redraw_unit_info_dlg(void)
{
  struct GUI *pWindow = pHelpDlg->pEndWidgetList;
  struct UNITS_BUTTONS *pStore = (struct UNITS_BUTTONS *)pWindow->data.ptr;
  SDL_Rect dst;
  SDL_Color color = {255, 255, 255, 64};
  
  redraw_group(pWindow->prev, pWindow, FALSE);
    
  dst.x = pStore->pDock->prev->size.x - 10;
  dst.y = pStore->pDock->prev->size.y - 10;
  dst.w = pWindow->size.w - (dst.x - pWindow->size.x) - 10; 
  dst.h = pWindow->size.h - (dst.y - pWindow->size.y) - 10; 
  SDL_FillRectAlpha(pWindow->dst, &dst, &color);
  putframe(pWindow->dst, dst.x , dst.y , dst.x + dst.w , dst.y + dst.h, 0xFF000000);
  
  /*------------------------------------- */
  redraw_group(pHelpDlg->pBeginWidgetList, pWindow->prev->prev, FALSE);
  flush_rect(pWindow->size);
}


void popup_unit_info(Unit_Type_id type_id)
{ 
  struct GUI *pBuf;
  struct GUI *pDock;
  struct GUI *pWindow;
  struct UNITS_BUTTONS *pStore;
  SDL_String16 *pStr;
  SDL_Surface *pSurf;
  int w, h, start_x, start_y;
  bool created, text = FALSE;
  int width = 0;
  struct unit_type *pUnit;
  char *buffer = &long_buffer[0];
  
  if(current_help_dlg != UNITS)
  {
    popdown_help_dialog();
  }
  
  if (!pHelpDlg)
  {
    SDL_Surface *pText, *pBack, *pTmp;
    SDL_Rect dst;
    
    current_help_dlg = UNITS;
    created = TRUE;
    pHelpDlg = MALLOC(sizeof(struct ADVANCED_DLG));
    pStore = MALLOC(sizeof(struct UNITS_BUTTONS));
    
    pStr = create_str16_from_char(_("Help : Units"), 12);
    pStr->style |= TTF_STYLE_BOLD;

    pWindow = create_window(NULL, pStr, 400, 400, WF_FREE_DATA);
    pWindow->action = help_dlg_window_callback;
    set_wstate(pWindow , FC_WS_NORMAL);
    pWindow->data.ptr = (void *)pStore;
    add_to_gui_list(ID_WINDOW, pWindow);
    pHelpDlg->pEndWidgetList = pWindow;
    /* ------------------ */
    
    /* exit button */
    pBuf = create_themeicon(pTheme->Small_CANCEL_Icon, pWindow->dst,
  			  			WF_DRAW_THEME_TRANSPARENT);
  
    pBuf->action = exit_help_dlg_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
    pBuf->key = SDLK_ESCAPE;
  
    add_to_gui_list(ID_BUTTON, pBuf);

    /* ------------------ */
    pDock = pBuf;
    
    pStr = create_string16(NULL, 0, 10);
    pStr->style |= (TTF_STYLE_BOLD | SF_CENTER);
    
    pTmp = create_surf(135, 40, SDL_SWSURFACE);
    pText = SDL_DisplayFormatAlpha(pTmp);
    FREESURFACE(pTmp);
    pTmp = pText;
    
    SDL_FillRect(pTmp, NULL,
	SDL_MapRGBA(pTmp->format, 255, 255, 255, 128));
    putframe(pTmp, 0,0, pTmp->w - 1, pTmp->h - 1, 0xFF000000);
    
    h = 0;
    unit_type_iterate(type) {
      pUnit = get_unit_type(type);
	
      pBack = SDL_DisplayFormatAlpha(pTmp);
      
      copy_chars_to_string16(pStr, pUnit->name);
      pText = create_text_surf_smaller_that_w(pStr, 100 - 4);
      
      /* draw name tech text */ 
      dst.x = 35 + (pBack->w - pText->w - 35) / 2;
      dst.y = (pBack->h - pText->h) / 2;
      SDL_BlitSurface(pText, NULL, pBack, &dst);
      FREESURFACE(pText);
    
      /* draw tech icon */
      {
	float zoom = 25.0 / GET_SURF(pUnit->sprite)->h;
        pText = ZoomSurface(GET_SURF(pUnit->sprite), zoom, zoom, 1);
      }
      dst.x = (35 - pText->w) / 2;;
      dst.y = (pBack->h - pText->h) / 2;
      SDL_BlitSurface(pText, NULL, pBack, &dst);
      FREESURFACE(pText);

      pBuf = create_icon2(pBack, pWindow->dst,
      		WF_FREE_THEME | WF_DRAW_THEME_TRANSPARENT);

      set_wstate(pBuf, FC_WS_NORMAL);
      pBuf->action = change_unit_callback;
      add_to_gui_list(MAX_ID - type, pBuf);
      
      if (++h > 10)
      {
        set_wflag(pBuf, WF_HIDDEN);
      }

    } unit_type_iterate_end;
    
    FREESURFACE(pTmp);
    
    pHelpDlg->pBeginActiveWidgetList = pBuf;
    pHelpDlg->pEndActiveWidgetList = pDock->prev;
    pHelpDlg->pBeginWidgetList = pBuf;/* IMPORTANT */
    
    if (h > 10) {
      pHelpDlg->pActiveWidgetList = pDock->prev;
      width = create_vertical_scrollbar(pHelpDlg, 1, 10, TRUE, TRUE);
    }
        
    
    /* toggle techs list button */
    pBuf = create_themeicon_button_from_chars(pTheme->UP_Icon,
	      pWindow->dst,  _("Units"), 10, 0);
    /*pBuf->action = toggle_full_tree_mode_in_help_dlg_callback;
   if (pStore->show_tree)
    {
      set_wstate(pBuf, FC_WS_NORMAL);
    }
*/    
    pBuf->size.w = 160;
    pBuf->size.h = 15;
    pBuf->string16->fgcol = *get_game_colorRGB(COLOR_STD_WHITE);
    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
  
    add_to_gui_list(ID_BUTTON, pBuf);
      
    pDock = pBuf;
    pStore->pDock = pDock;
  } else {
    created = FALSE;
    width = (pHelpDlg->pScroll ? pHelpDlg->pScroll->pUp_Left_Button->size.w: 0);
    pWindow = pHelpDlg->pEndWidgetList;
    pStore = (struct UNITS_BUTTONS *)pWindow->data.ptr;
    pDock = pStore->pDock;
    
    /* del. all usless widget */
    if (pDock  != pHelpDlg->pBeginWidgetList)
    {
      del_group_of_widgets_from_gui_list(pHelpDlg->pBeginWidgetList,
				       pDock->prev);
      pHelpDlg->pBeginWidgetList = pDock;
    }
  }
  
  pUnit = get_unit_type(type_id);
  pBuf= create_iconlabel_from_chars(GET_SURF(pUnit->sprite),
		    pWindow->dst, pUnit->name, 24, 0);

  pBuf->ID = ID_LABEL;
  DownAdd(pBuf, pDock);
  pDock = pBuf;

  
  {
    char local[2048];
    
    my_snprintf(local, sizeof(local), "%s %d %s",
	      N_("Cost:"), unit_build_shield_cost(type_id),
	      PL_("shield", "shields", unit_build_shield_cost(type_id)));
  
    if(pUnit->pop_cost)
    {
      cat_snprintf(local, sizeof(local), " %d %s",
	  pUnit->pop_cost, PL_("citizen", "citizens", pUnit->pop_cost));
    }
  
    cat_snprintf(local, sizeof(local), "      %s",  N_("Upkeep:"));
        
    if(pUnit->shield_cost)
    {
      cat_snprintf(local, sizeof(local), " %d %s",
	  pUnit->shield_cost, PL_("shield", "shields", pUnit->shield_cost));
     }
    if(pUnit->food_cost)
    {
      cat_snprintf(local, sizeof(local), " %d %s",
	  pUnit->food_cost, PL_("food", "foods", pUnit->food_cost));
    }
    if(pUnit->gold_cost)
    {
      cat_snprintf(local, sizeof(local), " %d %s",
	  pUnit->gold_cost, PL_("gold", "golds", pUnit->gold_cost));
    }
    if(pUnit->happy_cost)
    {
      cat_snprintf(local, sizeof(local), " %d %s",
	  pUnit->happy_cost, PL_("citizen", "citizens", pUnit->happy_cost));
    }
 
    cat_snprintf(local, sizeof(local), "\n%s %d %s %d %s %d\n%s %d %s %d %s %d",
	      N_("Attack:"), pUnit->attack_strength,
	      N_("Defense:"), pUnit->defense_strength,
              N_("Move:"), pUnit->move_rate / SINGLE_MOVE,
              N_("Vision:"), pUnit->vision_range,
	      N_("FirePower:"), pUnit->firepower,
              N_("Hitpoints:"), pUnit->hp);
  
    pBuf = create_iconlabel_from_chars(NULL,
		    pWindow->dst, local, 12, 0);
    pBuf->ID = ID_LABEL;
    DownAdd(pBuf, pDock);
    pDock = pBuf;
  }
  pBuf = create_iconlabel_from_chars(NULL,
		    pWindow->dst, N_("Requirement:"), 12, 0);
  pBuf->ID = ID_LABEL;
  DownAdd(pBuf, pDock);
  pDock = pBuf;
  
  if(pUnit->tech_requirement==A_LAST || pUnit->tech_requirement==A_NONE)
  {
    pBuf = create_iconlabel_from_chars(NULL,
		    pWindow->dst, _("None"), 12, 0);
    pBuf->ID = ID_LABEL;
  } else {
    pBuf = create_iconlabel_from_chars(NULL, pWindow->dst,
	  advances[pUnit->tech_requirement].name, 12,
			  WF_DRAW_THEME_TRANSPARENT);
    pBuf->ID = MAX_ID - pUnit->tech_requirement;
    pBuf->string16->fgcol = *get_tech_color(pUnit->tech_requirement);
    pBuf->action = change_tech_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
  }
  DownAdd(pBuf, pDock);
  pDock = pBuf;
  pStore->pReq = pBuf;
  
  pBuf = create_iconlabel_from_chars(NULL,
		    pWindow->dst, N_("Obsolete by:"), 12, 0);
  pBuf->ID = ID_LABEL;
  DownAdd(pBuf, pDock);
  pDock = pBuf;
  
  if(pUnit->obsoleted_by==-1) {
    pBuf = create_iconlabel_from_chars(NULL,
		    pWindow->dst, _("None"), 12, 0);
    pBuf->ID = ID_LABEL;  
  } else {
    struct unit_type *utype = get_unit_type(pUnit->obsoleted_by);
    pBuf = create_iconlabel_from_chars(NULL, pWindow->dst,
	      utype->name, 12, WF_DRAW_THEME_TRANSPARENT);
    pBuf->string16->fgcol = *get_tech_color(utype->tech_requirement);
    pBuf->ID = MAX_ID - pUnit->obsoleted_by;
    pBuf->action = change_unit_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
  }
  DownAdd(pBuf, pDock);
  pDock = pBuf;
  pStore->pObs = pBuf;
 
  start_x = (FRAME_WH + 1 + width + pHelpDlg->pActiveWidgetList->size.w + 20);
  
  buffer[0] = '\0';
  helptext_unit(buffer, type_id, "");
  if (buffer[0] != '\0')
  {
    SDL_String16 *pStr = create_str16_from_char(buffer, 12);
    convert_string_to_const_surface_width(pStr,	640 - start_x - 20);
    pBuf = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    pBuf->ID = ID_LABEL;
    DownAdd(pBuf, pDock);
    pDock = pBuf;
    text = TRUE;
  }
  
  pHelpDlg->pBeginWidgetList = pBuf;
  
  /* --------------------------------------------------------- */ 
  if (created)
  {
    w = 640;
    h = 480;
    pWindow->size.x = (Main.screen->w - w) / 2;
    pWindow->size.y = (Main.screen->h - h) / 2;
  
    /* alloca window theme and win background buffer */
    pSurf = get_logo_gfx();
    if (resize_window(pWindow, pSurf, NULL, w, h))
    {
      FREESURFACE(pSurf);
    }

    /* exit button */
    pBuf = pWindow->prev;
    pBuf->size.x = pWindow->size.x + pWindow->size.w-pBuf->size.w-FRAME_WH-1;
    pBuf->size.y = pWindow->size.y + 1;
  
    /* toggle button */
    pStore->pDock->size.x = pWindow->size.x + FRAME_WH;
    pStore->pDock->size.y = pWindow->size.y +  WINDOW_TILE_HIGH + 1;
    
    h = setup_vertical_widgets_position(1, pWindow->size.x + FRAME_WH + width,
		  pWindow->size.y + WINDOW_TILE_HIGH + 17, 0, 0,
		  pHelpDlg->pBeginActiveWidgetList,
  		  pHelpDlg->pEndActiveWidgetList);
    
    if (pHelpDlg->pScroll)
    {
      setup_vertical_scrollbar_area(pHelpDlg->pScroll,
	pWindow->size.x + FRAME_WH,
    	pWindow->size.y + WINDOW_TILE_HIGH + 17,
    	h, FALSE);
    }
  }
  
  /* unittype  icon and label */
  pBuf = pStore->pDock->prev;
  pBuf->size.x = pWindow->size.x + start_x;
  pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 20;
  start_y = pBuf->size.y + pBuf->size.h + 10;
  
  pBuf = pBuf->prev;
  pBuf->size.x = pWindow->size.x + start_x;
  pBuf->size.y = start_y;
  start_y += pBuf->size.h;
    
  pBuf = pStore->pReq->next;
  pBuf->size.x = pWindow->size.x + start_x;
  pBuf->size.y = start_y;
  
  pStore->pReq->size.x = pBuf->size.x + pBuf->size.w + 5;
  pStore->pReq->size.y = start_y;
    
  pBuf = pStore->pObs->next;
  pBuf->size.x = pStore->pReq->size.x + pStore->pReq->size.w + 10;
  pBuf->size.y = start_y;
  
  pStore->pObs->size.x = pBuf->size.x + pBuf->size.w + 5;
  pStore->pObs->size.y = start_y;
  start_y += pStore->pObs->size.h + 20;
  
  if (text)
  {
    pBuf = pStore->pObs->prev;
    pBuf->size.x = pWindow->size.x + start_x;
    pBuf->size.y = start_y;
  }
    
  
  redraw_unit_info_dlg();
}
  
/* =============================================== */
/* ==================== Tech Tree ================ */
/* =============================================== */


static int change_tech_callback(struct GUI *pWidget)
{
  popup_tech_info(MAX_ID - pWidget->ID);
  return -1;
}

static int show_help_callback(struct GUI *pWidget)
{
  struct TECHS_BUTTONS *pStore = (struct TECHS_BUTTONS *)pHelpDlg->pEndWidgetList->data.ptr;
  pStore->show_tree = !pStore->show_tree;
  if (!pStore->show_tree)
  {
    pStore->show_full_tree = FALSE;
    pStore->pDock->gfx = pTheme->UP_Icon;
  }
  popup_tech_info(MAX_ID - pStore->pDock->prev->ID);
  return -1;
}

static void redraw_tech_info_dlg(void)
{
  struct GUI *pWindow = pHelpDlg->pEndWidgetList;
  struct TECHS_BUTTONS *pStore = (struct TECHS_BUTTONS *)pWindow->data.ptr;
  SDL_Surface *pText0, *pText1 = NULL;
  SDL_String16 *pStr;
  SDL_Rect dst;
  SDL_Color color = {255, 255, 255, 64};
  
  redraw_group(pWindow->prev, pWindow, FALSE);
    
  dst.x = pStore->pDock->prev->prev->size.x - 10;
  dst.y = pStore->pDock->prev->prev->size.y - 10;
  dst.w = pWindow->size.w - (dst.x - pWindow->size.x) - 10; 
  dst.h = pWindow->size.h - (dst.y - pWindow->size.y) - 10; 
  SDL_FillRectAlpha(pWindow->dst, &dst, &color);
  putframe(pWindow->dst, dst.x , dst.y , dst.x + dst.w , dst.y + dst.h, 0xFF000000);
  
  /* -------------------------- */
  pStr = create_str16_from_char(_("Allows"), 14);
  pStr->style |= TTF_STYLE_BOLD;
  
  pText0 = create_text_surf_from_str16(pStr);
  dst.x = pStore->pDock->prev->prev->size.x;
  if (pStore->pTargets[0])
  {
    dst.y = pStore->pTargets[0]->size.y - pText0->h;
  } else {
    dst.y = pStore->pDock->prev->prev->size.y 
	      + pStore->pDock->prev->prev->size.h + 10;
  }
  SDL_BlitSurface(pText0, NULL, pWindow->dst, &dst);
  FREESURFACE(pText0);

  if (pStore->pSub_Targets[0])
  {
    int i;
      
    change_ptsize16(pStr, 12);
      
    copy_chars_to_string16(pStr, _("( witch "));
    pText0 = create_text_surf_from_str16(pStr);
    
    copy_chars_to_string16(pStr, _(" )"));
    pText1 = create_text_surf_from_str16(pStr);
    i = 0;
    while(i < 6 && pStore->pSub_Targets[i])
    {
      dst.x = pStore->pSub_Targets[i]->size.x - pText0->w;
      dst.y = pStore->pSub_Targets[i]->size.y;
      SDL_BlitSurface(pText0, NULL, pWindow->dst, &dst);
      dst.x = pStore->pSub_Targets[i]->size.x + pStore->pSub_Targets[i]->size.w;
      SDL_BlitSurface(pText1, NULL, pWindow->dst, &dst);
      i++;
    }
      
    FREESURFACE(pText0);
    FREESURFACE(pText1);
  }
  FREESTRING16(pStr);
  
  redraw_group(pHelpDlg->pBeginWidgetList, pWindow->prev->prev, FALSE);
  flush_rect(pWindow->size);
}

static struct GUI * create_tech_info(Tech_Type_id tech, int width, struct GUI *pWindow, struct TECHS_BUTTONS *pStore)
{
  struct GUI *pBuf;
  struct GUI *pLast, *pBudynki;
  struct GUI *pDock = pStore->pDock;
  int i, targets_count,sub_targets_count, max_width = 0;
  int start_x, start_y, imp_count, unit_count, flags_count, gov_count;
  char *buffer = &long_buffer[0];
  
  start_x = (FRAME_WH + 1 + width + pHelpDlg->pActiveWidgetList->size.w + 20);
  
  pBuf = create_icon2(pTheme->Tech_Tree_Icon, pWindow->dst,
      		   WF_DRAW_THEME_TRANSPARENT);

  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->action = show_help_callback;
  pBuf->ID = MAX_ID - tech;
  DownAdd(pBuf, pDock);
  pDock = pBuf;
  
  /* ----------------------------------- */
  pBuf= create_iconlabel_from_chars(get_tech_icon(tech),
		    pWindow->dst, advances[tech].name, 24, 0);

  pBuf->ID = ID_LABEL;
  DownAdd(pBuf, pDock);
  pDock = pBuf;
  
  targets_count = 0;
  for(i=A_FIRST; i<game.num_tech_types; i++)
  {
    if ((targets_count<6)
      && (advances[i].req[0] == tech || advances[i].req[1] == tech))
    {
      pBuf= create_iconlabel_from_chars(NULL, pWindow->dst, advances[i].name, 12, WF_DRAW_THEME_TRANSPARENT);
      pBuf->string16->fgcol = *get_tech_color(i);
      max_width = MAX(max_width, pBuf->size.w);
      set_wstate(pBuf, FC_WS_NORMAL);
      pBuf->action = change_tech_callback;
      pBuf->ID = MAX_ID - i;
      DownAdd(pBuf, pDock);
      pDock = pBuf;
      pStore->pTargets[targets_count++] = pBuf;
    }
  }
  if (targets_count<6)
  {
    pStore->pTargets[targets_count] = NULL;
  }
  
  sub_targets_count = 0;
  if (targets_count)
  {
    int sub_tech;
    for(i = 0; i < targets_count; i++)
    {
      sub_tech = MAX_ID - pStore->pTargets[i]->ID;
      if (advances[sub_tech].req[0] == tech
	   && advances[sub_tech].req[1] != A_NONE)
      {
	sub_tech = advances[sub_tech].req[1];
      } else {
	if (advances[sub_tech].req[1] == tech
	     && advances[sub_tech].req[0] != A_NONE)
        {
	  sub_tech = advances[sub_tech].req[0];
	} else {
	  continue;
	}
      }
      pBuf= create_iconlabel_from_chars(NULL, pWindow->dst, advances[sub_tech].name, 12, WF_DRAW_THEME_TRANSPARENT);
      pBuf->string16->fgcol = *get_tech_color(sub_tech);
      set_wstate(pBuf, FC_WS_NORMAL);
      pBuf->action = change_tech_callback;
      pBuf->ID = MAX_ID - sub_tech;
      DownAdd(pBuf, pDock);
      pDock = pBuf;
      pStore->pSub_Targets[sub_targets_count++] = pBuf;
    }
  }
  if (sub_targets_count<6)
  {
    pStore->pSub_Targets[sub_targets_count] = NULL;
  }
  
  /* fill array with iprvm. icons */
  pBudynki = pBuf;
  
  gov_count = 0;
  government_iterate(gov) {
    if (gov->required_tech == tech)
    {
      pBuf = create_iconlabel_from_chars(GET_SURF(gov->sprite),
	      pWindow->dst, gov->name, 14,
	      WF_DRAW_THEME_TRANSPARENT|WF_SELLECT_WITHOUT_BAR);
      set_wstate(pBuf, FC_WS_NORMAL);
      pBuf->action = change_gov_callback;
      pBuf->ID = MAX_ID - gov->index;
      DownAdd(pBuf, pDock);
      pDock = pBuf;
      gov_count++;
    }
  } government_iterate_end;
  
  
  imp_count = 0;
  impr_type_iterate(imp) {
    struct impr_type *pImpr = get_improvement_type(imp);
    if (pImpr->tech_req == tech) {
      pBuf = create_iconlabel_from_chars(GET_SURF(pImpr->sprite),
	      pWindow->dst, get_improvement_name(imp), 14,
	      WF_DRAW_THEME_TRANSPARENT|WF_SELLECT_WITHOUT_BAR);
      set_wstate(pBuf, FC_WS_NORMAL);
      if (is_wonder(imp))
      {
	pBuf->string16->fgcol = *get_game_colorRGB(COLOR_STD_CITY_LUX);
      }
      pBuf->action = change_impr_callback;
      pBuf->ID = MAX_ID - imp;
      DownAdd(pBuf, pDock);
      pDock = pBuf;
      imp_count++;
    }
  } impr_type_iterate_end;
  
  unit_count = 0;
  unit_type_iterate(un) {
    struct unit_type *pUnit = get_unit_type(un);
    if (pUnit->tech_requirement == tech) {
      if (GET_SURF(pUnit->sprite)->w > 64)
      {
	float zoom = 64.0 / GET_SURF(pUnit->sprite)->w;
        pBuf = create_iconlabel_from_chars(ZoomSurface(GET_SURF(pUnit->sprite), zoom, zoom, 1),
	      pWindow->dst, pUnit->name, 14, 
	      (WF_FREE_THEME|WF_DRAW_THEME_TRANSPARENT|WF_SELLECT_WITHOUT_BAR));
      } else {
	pBuf = create_iconlabel_from_chars(GET_SURF(pUnit->sprite),
	      pWindow->dst, pUnit->name, 14,
	      (WF_DRAW_THEME_TRANSPARENT|WF_SELLECT_WITHOUT_BAR));
      }
      set_wstate(pBuf, FC_WS_NORMAL);
      pBuf->action = change_unit_callback;
      pBuf->ID = MAX_ID - un;
      DownAdd(pBuf, pDock);
      pDock = pBuf;
      unit_count++;
    }
  } unit_type_iterate_end;
  
  buffer[0] = '\0';
  helptext_tech(buffer, tech, "");
  if (buffer[0] != '\0')
  {
    SDL_String16 *pStr = create_str16_from_char(buffer, 12);
    convert_string_to_const_surface_width(pStr,	640 - start_x - 20);
    pBuf = create_iconlabel(NULL, pWindow->dst, pStr, 0);
    pBuf->ID = ID_LABEL;
    DownAdd(pBuf, pDock);
    pDock = pBuf;
    flags_count = 1;
  } else {
    flags_count = 0;
  }
  
  pLast = pBuf;
  /* --------------------------------------------- */
    
  /* tree button */
  pBuf = pStore->pDock->prev;
  pBuf->size.x = pWindow->size.x + pWindow->size.w - pBuf->size.w - 20;
  pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 20;
  
  /* Tech label */
  pBuf = pBuf->prev;
  pBuf->size.x = pWindow->size.x + start_x;
  pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 20;
  start_y = pBuf->size.y + pBuf->size.h + 30;
  
  if (targets_count)
  {
    int j, t0, t1;
    
    i = 0;
    j = 0;
    t1 = MAX_ID - pStore->pSub_Targets[j]->ID;
    while(i < 6 && pStore->pTargets[i])
    {
      pStore->pTargets[i]->size.x = pWindow->size.x + start_x;
      pStore->pTargets[i]->size.y = start_y;
      
      if(pStore->pSub_Targets[j])
      {
        t0 = MAX_ID - pStore->pTargets[i]->ID;
	t1 = MAX_ID - pStore->pSub_Targets[j]->ID;
        if (advances[t0].req[0] == t1 || advances[t0].req[1] == t1)
        {
	  pStore->pSub_Targets[j]->size.x = pWindow->size.x + start_x + max_width + 60;
          pStore->pSub_Targets[j]->size.y = pStore->pTargets[i]->size.y;
	  j++;
	}
      }
      
      start_y += pStore->pTargets[i]->size.h;
      i++;
    }
    
    start_y += 10;
  }
  pBuf = NULL;
  
  if (gov_count)
  {
    pBuf = pBudynki->prev;
    while(gov_count-- && pBuf)
    {
      pBuf->size.x = pWindow->size.x + start_x;
      pBuf->size.y = start_y;
      start_y += pBuf->size.h + 2;
      pBuf = pBuf->prev;
    }
  }
  
  if (imp_count)
  {
    if(!pBuf)
    {
      pBuf = pBudynki->prev;
    }
    while(imp_count-- && pBuf)
    {
      pBuf->size.x = pWindow->size.x + start_x;
      pBuf->size.y = start_y;
      start_y += pBuf->size.h + 2;
      pBuf = pBuf->prev;
    }
  }
  
  if (unit_count)
  {
    if(!pBuf)
    {
      pBuf = pBudynki->prev;
    }
    while(unit_count-- && pBuf)
    {
      pBuf->size.x = pWindow->size.x + start_x;
      pBuf->size.y = start_y;
      start_y += pBuf->size.h + 2;
      pBuf = pBuf->prev;
    }
  }
  
  if (flags_count)
  {
    if(!pBuf)
    {
      pBuf = pBudynki->prev;
    }
    while(flags_count-- && pBuf)
    {
      pBuf->size.x = pWindow->size.x + start_x;
      pBuf->size.y = start_y;
      start_y += pBuf->size.h + 2;
      pBuf = pBuf->prev;
    }
  }
  
  return pLast;
}


static void redraw_tech_tree_dlg(void)
{
  struct GUI *pWindow = pHelpDlg->pEndWidgetList;
  struct GUI *pSub0, *pSub1;
  struct TECHS_BUTTONS *pStore = (struct TECHS_BUTTONS *)pWindow->data.ptr;
  struct GUI *pTech = pStore->pDock->prev;
  int i,j, tech, count, step, mod;
  Uint32 color = 0xFFFFFFFF;
  SDL_Rect dst;
  SDL_Surface *pSurf;
  
  /* Redraw Window with exit button */ 
  redraw_group(pWindow->prev, pWindow, FALSE);
    
  pSurf = ResizeSurface(get_tech_icon(MAX_ID - pTech->ID), 420, 420, 1);
  SDL_SetAlpha(pSurf, SDL_SRCALPHA, 164);
  dst.x = pWindow->size.x + pWindow->size.w - pSurf->w - 50;
  dst.y = pWindow->size.y + (pWindow->size.h - pSurf->h) / 2;;
  SDL_BlitSurface(pSurf, NULL, pWindow->dst, &dst);
  FREESURFACE(pSurf);
  
  
  /* Draw Req arrows */
  i = 0;
  while(i < 4 && pStore->pSub_Req[i])
  {
    i++;
  }
  count = i;
  
  i = 0;
  while(i < 2 && pStore->pReq[i])
  {
    tech = MAX_ID - pStore->pReq[i]->ID;
    
    /*find Sub_Req's */
    if(i)
    {
      pSub0 = NULL;
      for(j=count - 1; j >= 0; j--)
      {
        if (MAX_ID - pStore->pSub_Req[j]->ID == advances[tech].req[0])
        {
	  pSub0 = pStore->pSub_Req[j];
	  break;
        }
      }
    
      pSub1 = NULL;
      for(j=count - 1; j >= 0; j--)
      {
        if (MAX_ID - pStore->pSub_Req[j]->ID == advances[tech].req[1])
        {
	  pSub1 = pStore->pSub_Req[j];
	  break;
        }
      }
    } else {
      pSub0 = NULL;
      for(j=0; j < 4 && pStore->pSub_Req[j]; j++)
      {
        if (MAX_ID - pStore->pSub_Req[j]->ID == advances[tech].req[0])
        {
	  pSub0 = pStore->pSub_Req[j];
	  break;
        }
      }
    
      pSub1 = NULL;
      for(j=0; j < 4 && pStore->pSub_Req[j]; j++)
      {
        if (MAX_ID - pStore->pSub_Req[j]->ID == advances[tech].req[1])
        {
	  pSub1 = pStore->pSub_Req[j];
	  break;
        }
      }
    }
    
    /* draw main Arrow */
    putline(pStore->pReq[i]->dst,
        pStore->pReq[i]->size.x + pStore->pReq[i]->size.w,
        pStore->pReq[i]->size.y + pStore->pReq[i]->size.h / 2,
        pTech->size.x,
        pStore->pReq[i]->size.y + pStore->pReq[i]->size.h / 2,
        color);
    
    /* Draw Sub_Req arrows */
    if (pSub0 || pSub1)
    {
      putline(pStore->pReq[i]->dst,
        pStore->pReq[i]->size.x - 10,
        pStore->pReq[i]->size.y + pStore->pReq[i]->size.h / 2,
        pStore->pReq[i]->size.x ,
        pStore->pReq[i]->size.y + pStore->pReq[i]->size.h / 2,
        color);
    }
    
    if(pSub0)
    {
      putline(pStore->pReq[i]->dst,
        pStore->pReq[i]->size.x - 10,
        pSub0->size.y + pSub0->size.h / 2,
        pStore->pReq[i]->size.x - 10 ,
        pStore->pReq[i]->size.y + pStore->pReq[i]->size.h / 2,
        color);
      putline(pStore->pReq[i]->dst,
        pSub0->size.x + pSub0->size.w,
        pSub0->size.y + pSub0->size.h / 2,
        pStore->pReq[i]->size.x - 10 ,
        pSub0->size.y + pSub0->size.h / 2,
        color);
    }
    
    if(pSub1)
    {
      putline(pStore->pReq[i]->dst,
        pStore->pReq[i]->size.x - 10,
        pSub1->size.y + pSub1->size.h / 2,
        pStore->pReq[i]->size.x - 10 ,
        pStore->pReq[i]->size.y + pStore->pReq[i]->size.h / 2,
        color);
      putline(pStore->pReq[i]->dst,
        pSub1->size.x + pSub1->size.w,
        pSub1->size.y + pSub1->size.h / 2,
        pStore->pReq[i]->size.x - 10 ,
        pSub1->size.y + pSub1->size.h / 2,
        color);
    }
    i++;
  }

  i = 0;
  while(i < 6 && pStore->pTargets[i])
  {
    i++;
  }
  count = i;
  
  if (count > 4)
  {
    mod = 3;
  } else {
    mod = 2;
  }
  
  for(i=0; i< count; i++)  
  {
    tech = MAX_ID - pStore->pTargets[i]->ID;
    step = pTech->size.h / (count + 1);
    
    switch((i % mod))
    {
      case 2:
	color = 0xFFFF0FF0;
      break;
      case 1:
	color = 0xFFFFF00F;
      break;
      default:
        color = 0xFFFFFFFF;
      break;
    }
    
    
    /*find Sub_Req's */
    if (advances[tech].req[0] == MAX_ID - pTech->ID)
    {
      pSub0 = pTech;
    } else {
      pSub0 = NULL;
      for(j=0; j < 6 && pStore->pSub_Targets[j]; j++)
      {
        if (MAX_ID - pStore->pSub_Targets[j]->ID == advances[tech].req[0])
        {
	  pSub0 = pStore->pSub_Targets[j];
	  break;
        }
      }
    }
    
    if (advances[tech].req[1] == MAX_ID - pTech->ID)
    {
      pSub1 = pTech;
    } else {
      pSub1 = NULL;
      for(j=0; j < 6 && pStore->pSub_Targets[j]; j++)
      {
        if (MAX_ID - pStore->pSub_Targets[j]->ID == advances[tech].req[1])
        {
	  pSub1 = pStore->pSub_Targets[j];
	  break;
        }
      }
    }
    
    /* Draw Sub_Targets arrows */
    if (pSub0 || pSub1)
    {
      putline(pStore->pTargets[i]->dst,
        pStore->pTargets[i]->size.x - ((i % mod) + 1) * 6,
        pStore->pTargets[i]->size.y + pStore->pTargets[i]->size.h / 2,
        pStore->pTargets[i]->size.x ,
        pStore->pTargets[i]->size.y + pStore->pTargets[i]->size.h / 2,
        color);
    }
    
    if(pSub0)
    {
      int y;
      if (pSub0 == pTech)
      {
	y = pSub0->size.y + step * (i + 1);
      } else {
	y = pSub0->size.y + pSub0->size.h / 2;
      }
      
      putline(pStore->pTargets[i]->dst,
        pStore->pTargets[i]->size.x - ((i % mod) + 1) * 6,
        y,
        pStore->pTargets[i]->size.x - ((i % mod) + 1) * 6,
        pStore->pTargets[i]->size.y + pStore->pTargets[i]->size.h / 2,
        color);
      putline(pStore->pTargets[i]->dst,
        pSub0->size.x + pSub0->size.w,
        y,
        pStore->pTargets[i]->size.x - ((i % mod) + 1) * 6,
        y,
        color);
    }
    
    if(pSub1)
    {
      int y;
      if (pSub1 == pTech)
      {
	y = pSub1->size.y + step * (i + 1);
      } else {
	y = pSub1->size.y + pSub1->size.h / 2;
      }
      putline(pStore->pTargets[i]->dst,
        pStore->pTargets[i]->size.x - ((i % mod) + 1) * 6,
        y,
        pStore->pTargets[i]->size.x - ((i % mod) + 1) * 6,
        pStore->pTargets[i]->size.y + pStore->pTargets[i]->size.h / 2,
        color);
      putline(pStore->pTargets[i]->dst,
        pSub1->size.x + pSub1->size.w,
        y,
        pStore->pTargets[i]->size.x - ((i % mod) + 1) * 6,
        y,
        color);
    }
  }
  
  /* Redraw rest */
  redraw_group(pHelpDlg->pBeginWidgetList, pWindow->prev->prev, FALSE);
  
  flush_rect(pWindow->size);
}

static int toggle_full_tree_mode_in_help_dlg_callback(struct GUI *pWidget)
{
  struct TECHS_BUTTONS *pStore = (struct TECHS_BUTTONS *)pHelpDlg->pEndWidgetList->data.ptr;
  if (pStore->show_full_tree)
  {
    pWidget->gfx = pTheme->UP_Icon;
  } else {
    pWidget->gfx = pTheme->DOWN_Icon;
  }
  pStore->show_full_tree = !pStore->show_full_tree;
  popup_tech_info(MAX_ID - pStore->pDock->prev->ID);
  return -1;
}


static struct GUI * create_tech_tree(Tech_Type_id tech, int width, struct GUI *pWindow, struct TECHS_BUTTONS *pStore)
{
  int i, w, h, req_count , targets_count, sub_req_count, sub_targets_count;
  struct GUI *pBuf;
  struct GUI *pTech;
  SDL_String16 *pStr;
  SDL_Surface *pSurf;
  struct GUI *pDock = pStore->pDock;
    
  pStr = create_string16(NULL, 0, 10);
  pStr->style |= (TTF_STYLE_BOLD | SF_CENTER);
  
  copy_chars_to_string16(pStr, advances[tech].name);
  pSurf = create_sellect_tech_icon(pStr, tech, FULL_MODE);
  pBuf = create_icon2(pSurf, pWindow->dst,
      		WF_FREE_THEME | WF_DRAW_THEME_TRANSPARENT);

  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->action = show_help_callback;
  pBuf->ID = MAX_ID - tech;
  DownAdd(pBuf, pDock);
  pTech = pBuf;
  pDock = pBuf;
  
  req_count  = 0;
  for(i = 0; i < 2; i++)
  {
    if (tech_exists(i) && advances[tech].req[i] != A_NONE)
    {
      copy_chars_to_string16(pStr, advances[advances[tech].req[i]].name);
      pSurf = create_sellect_tech_icon(pStr, advances[tech].req[i], SMALL_MODE);
      pBuf = create_icon2(pSurf, pWindow->dst,
      		WF_FREE_THEME | WF_DRAW_THEME_TRANSPARENT);
      set_wstate(pBuf, FC_WS_NORMAL);
      pBuf->action = change_tech_callback;
      pBuf->ID = MAX_ID - advances[tech].req[i];
      DownAdd(pBuf, pDock);
      pDock = pBuf;
      pStore->pReq[i] = pBuf;
      req_count++;
    } else {
      pStore->pReq[i] = NULL;
    }
  }
  
  sub_req_count = 0;

  if (pStore->show_full_tree && req_count)
  {
    int j, sub_tech;
    for(j = 0; j < req_count; j++)
    {
      sub_tech = MAX_ID - pStore->pReq[j]->ID;
      for(i = 0; i < 2; i++)
      {
        if (tech_exists(i) && advances[sub_tech].req[i] != A_NONE)
        {
          copy_chars_to_string16(pStr, advances[advances[sub_tech].req[i]].name);
          pSurf = create_sellect_tech_icon(pStr, advances[sub_tech].req[i], SMALL_MODE);
          pBuf = create_icon2(pSurf, pWindow->dst,
      		WF_FREE_THEME | WF_DRAW_THEME_TRANSPARENT);
          set_wstate(pBuf, FC_WS_NORMAL);
          pBuf->action = change_tech_callback;
          pBuf->ID = MAX_ID - advances[sub_tech].req[i];
          DownAdd(pBuf, pDock);
          pDock = pBuf;
          pStore->pSub_Req[sub_req_count++] = pBuf;
        }
      }
    }
  }

  if (sub_req_count < 4)
  {
    pStore->pSub_Req[sub_req_count] = NULL;
  }
  
  targets_count = 0;
  for(i=A_FIRST; i<game.num_tech_types; i++)
  {
    if ((targets_count<6)
      && (advances[i].req[0] == tech || advances[i].req[1] == tech))
    {
      copy_chars_to_string16(pStr, advances[i].name);
      pSurf = create_sellect_tech_icon(pStr, i, SMALL_MODE);
      pBuf = create_icon2(pSurf, pWindow->dst,
      		WF_FREE_THEME | WF_DRAW_THEME_TRANSPARENT);

      set_wstate(pBuf, FC_WS_NORMAL);
      pBuf->action = change_tech_callback;
      pBuf->ID = MAX_ID - i;
      DownAdd(pBuf, pDock);
      pDock = pBuf;
      pStore->pTargets[targets_count++] = pBuf;
    }
  }
  if (targets_count<6)
  {
    pStore->pTargets[targets_count] = NULL;
  }
  
  sub_targets_count = 0;
  if (targets_count)
  {
    int sub_tech;
    for(i = 0; i < targets_count; i++)
    {
      sub_tech = MAX_ID - pStore->pTargets[i]->ID;
      if (advances[sub_tech].req[0] == tech
	   && advances[sub_tech].req[1] != A_NONE)
      {
	sub_tech = advances[sub_tech].req[1];
      } else {
	if (advances[sub_tech].req[1] == tech
	     && advances[sub_tech].req[0] != A_NONE)
        {
	  sub_tech = advances[sub_tech].req[0];
	} else {
	  continue;
	}
      }
      
      copy_chars_to_string16(pStr, advances[sub_tech].name);
      pSurf = create_sellect_tech_icon(pStr, sub_tech, SMALL_MODE);
      pBuf = create_icon2(pSurf, pWindow->dst,
      	WF_FREE_THEME | WF_DRAW_THEME_TRANSPARENT);
      set_wstate(pBuf, FC_WS_NORMAL);
      pBuf->action = change_tech_callback;
      pBuf->ID = MAX_ID - sub_tech;
      DownAdd(pBuf, pDock);
      pDock = pBuf;
      pStore->pSub_Targets[sub_targets_count++] = pBuf;
    }
  }
  if (sub_targets_count<6)
  {
    pStore->pSub_Targets[sub_targets_count] = NULL;
  }
  
  FREESTRING16(pStr);

  /* ------------------------------------------ */
  if (sub_req_count)
  {
    w = (20 + pStore->pSub_Req[0]->size.w) * 2;
    w += (pWindow->size.w - (20 + pStore->pSub_Req[0]->size.w + w + pTech->size.w)) / 2;
  } else {
    if (req_count)
    {
      w = (FRAME_WH + 1 + width + pStore->pReq[0]->size.w * 2 + 20);
      w += (pWindow->size.w - ((20 + pStore->pReq[0]->size.w) + w + pTech->size.w)) / 2;
    } else {
      w = (pWindow->size.w - pTech->size.w) / 2;
    }
  }

  pTech->size.x = pWindow->size.x + w;
  pTech->size.y = pWindow->size.y + WINDOW_TILE_HIGH + (pWindow->size.h - pTech->size.h - WINDOW_TILE_HIGH) / 2;
    
  if(req_count)
  {
    h = (req_count == 1 ? pStore->pReq[0]->size.h : req_count * (pStore->pReq[0]->size.h + 80) - 80);
    h = pTech->size.y + (pTech->size.h - h) / 2;
    for(i =0; i <req_count; i++)
    {
      pStore->pReq[i]->size.x = pTech->size.x - 20 - pStore->pReq[i]->size.w;
      pStore->pReq[i]->size.y = h;
      h += (pStore->pReq[i]->size.h + 80);
    }
  }
  
  if(sub_req_count)
  {
    h = (sub_req_count == 1 ? pStore->pSub_Req[0]->size.h : sub_req_count * (pStore->pSub_Req[0]->size.h + 20) - 20);
    h = pTech->size.y + (pTech->size.h - h) / 2;
    for(i =0; i <sub_req_count; i++)
    {
      pStore->pSub_Req[i]->size.x = pTech->size.x - (20 + pStore->pSub_Req[i]->size.w) * 2;
      pStore->pSub_Req[i]->size.y = h;
      h += (pStore->pSub_Req[i]->size.h + 20);
    }
  }
  
  if(targets_count)
  {
    h = (targets_count == 1 ? pStore->pTargets[0]->size.h : targets_count * (pStore->pTargets[0]->size.h + 20) - 20);
    h = pTech->size.y + (pTech->size.h - h) / 2;
    for(i =0; i <targets_count; i++)
    {  
      pStore->pTargets[i]->size.x = pTech->size.x + pTech->size.w + 20;
      pStore->pTargets[i]->size.y = h;
      h += (pStore->pTargets[i]->size.h + 20);
    }
  }
  
  if(sub_targets_count)
  {
    if(sub_targets_count < 3)
    {
      pStore->pSub_Targets[0]->size.x = pTech->size.x + pTech->size.w - pStore->pSub_Targets[0]->size.w;
      pStore->pSub_Targets[0]->size.y = pTech->size.y - pStore->pSub_Targets[0]->size.h - 10;
      if (pStore->pSub_Targets[1])
      {
	pStore->pSub_Targets[1]->size.x = pTech->size.x + pTech->size.w - pStore->pSub_Targets[1]->size.w;
        pStore->pSub_Targets[1]->size.y = pTech->size.y + pTech->size.h + 10;
      }
    }
    else
    {
      if(sub_targets_count < 5)
      {
        for(i =0; i <MIN(sub_targets_count, 4); i++)
        {       
	  pStore->pSub_Targets[i]->size.x = pTech->size.x + pTech->size.w - pStore->pSub_Targets[i]->size.w;
	  if (i < 2)
	  {
            pStore->pSub_Targets[i]->size.y = pTech->size.y - (pStore->pSub_Targets[i]->size.h + 5) * ( 2 - i );
	  } else {
	    pStore->pSub_Targets[i]->size.y = pTech->size.y + pTech->size.h + 5  + (pStore->pSub_Targets[i]->size.h + 5) * ( i - 2 );
	  }
        }
      } else {
	h = (pStore->pSub_Targets[0]->size.h + 6);
	for(i =0; i <MIN(sub_targets_count, 6); i++)
        {
	  switch(i)
	  {
	    case 0:
	      pStore->pSub_Targets[i]->size.x = pTech->size.x + pTech->size.w - pStore->pSub_Targets[i]->size.w;
	      pStore->pSub_Targets[i]->size.y = pTech->size.y - h * 2;
	    break;
	    case 1:
	      pStore->pSub_Targets[i]->size.x = pTech->size.x + pTech->size.w - pStore->pSub_Targets[i]->size.w * 2 - 10;
	      pStore->pSub_Targets[i]->size.y = pTech->size.y - h - h / 2;
	    break;
	    case 2:
	      pStore->pSub_Targets[i]->size.x = pTech->size.x + pTech->size.w - pStore->pSub_Targets[i]->size.w;
	      pStore->pSub_Targets[i]->size.y = pTech->size.y - h;
	    break;
	    case 3:
	      pStore->pSub_Targets[i]->size.x = pTech->size.x + pTech->size.w - pStore->pSub_Targets[i]->size.w;
	      pStore->pSub_Targets[i]->size.y = pTech->size.y + pTech->size.h + 6;
	    break;
	    case 4:
	      pStore->pSub_Targets[i]->size.x = pTech->size.x + pTech->size.w - pStore->pSub_Targets[i]->size.w;
	      pStore->pSub_Targets[i]->size.y = pTech->size.y + pTech->size.h + 6 + h;
	    break;
	    default:
	      pStore->pSub_Targets[i]->size.x = pTech->size.x + pTech->size.w - pStore->pSub_Targets[i]->size.w * 2 - 10;
	      pStore->pSub_Targets[i]->size.y = pTech->size.y + pTech->size.h + 6 + h / 2 ;
	    break;
	  };
        }
      }
    }
  }
    
  return pBuf;
}

void popup_tech_info(Tech_Type_id tech)
{ 
  struct GUI *pBuf;
  struct GUI *pDock;
  struct GUI *pWindow;
  struct TECHS_BUTTONS *pStore;
  SDL_String16 *pStr;
  SDL_Surface *pSurf;
  int i, w, h;
  bool created;
  int width = 0;
  
  if(current_help_dlg != TECH)
  {
    popdown_help_dialog();
  }
  
  if (!pHelpDlg)
  {
    created = TRUE;
    current_help_dlg = TECH;
    pHelpDlg = MALLOC(sizeof(struct ADVANCED_DLG));
    pStore = MALLOC(sizeof(struct TECHS_BUTTONS));
      
    memset(pStore, 0, sizeof(struct TECHS_BUTTONS));
  
    pStore->show_tree = FALSE;
    pStore->show_full_tree = FALSE;
    
    pStr = create_str16_from_char(_("Help : Advances Tree"), 12);
    pStr->style |= TTF_STYLE_BOLD;

    pWindow = create_window(NULL, pStr, 400, 400, WF_FREE_DATA);
    pWindow->data.ptr = (void *)pStore;
    pWindow->action = help_dlg_window_callback;
    set_wstate(pWindow , FC_WS_NORMAL);
      
    add_to_gui_list(ID_WINDOW, pWindow);
    pHelpDlg->pEndWidgetList = pWindow;
    /* ------------------ */
    
    /* exit button */
    pBuf = create_themeicon(pTheme->Small_CANCEL_Icon, pWindow->dst,
  			  			WF_DRAW_THEME_TRANSPARENT);
  
    //w += pBuf->size.w + 10;
    pBuf->action = exit_help_dlg_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
    pBuf->key = SDLK_ESCAPE;
  
    add_to_gui_list(ID_BUTTON, pBuf);

    /* ------------------ */
    pDock = pBuf;
    pStr = create_string16(NULL, 0, 10);
    pStr->style |= (TTF_STYLE_BOLD | SF_CENTER);
    
    h = 0;
    for(i=A_FIRST; i<game.num_tech_types; i++)
    {
      if (tech_exists(i))
      {
        copy_chars_to_string16(pStr, advances[i].name);
        pSurf = create_sellect_tech_icon(pStr, i, SMALL_MODE);
        pBuf = create_icon2(pSurf, pWindow->dst,
      		WF_FREE_THEME | WF_DRAW_THEME_TRANSPARENT);

        set_wstate(pBuf, FC_WS_NORMAL);
        pBuf->action = change_tech_callback;
        add_to_gui_list(MAX_ID - i, pBuf);
      
        if (++h > 10)
        {
          set_wflag(pBuf, WF_HIDDEN);
        }
      }
    }
    
    FREESTRING16(pStr);  
    
    pHelpDlg->pBeginActiveWidgetList = pBuf;
    pHelpDlg->pEndActiveWidgetList = pDock->prev;
    pHelpDlg->pBeginWidgetList = pBuf;/* IMPORTANT */
    
    if (h > 10) {
      pHelpDlg->pActiveWidgetList = pDock->prev;
      width = create_vertical_scrollbar(pHelpDlg, 1, 10, TRUE, TRUE);
    }
        
    
    /* toggle techs list button */
    pBuf = create_themeicon_button_from_chars(pTheme->UP_Icon,
	      pWindow->dst,  _("Advances"), 10, 0);
    pBuf->action = toggle_full_tree_mode_in_help_dlg_callback;
    if (pStore->show_tree)
    {
      set_wstate(pBuf, FC_WS_NORMAL);
    }
    pBuf->size.w = 160;
    pBuf->size.h = 15;
    pBuf->string16->fgcol = *get_game_colorRGB(COLOR_STD_WHITE);
    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
    //pBuf->key = SDLK_ESCAPE;
  
    add_to_gui_list(ID_BUTTON, pBuf);
      
    pDock = pBuf;
    pStore->pDock = pDock;
  } else {
    created = FALSE;
    width = (pHelpDlg->pScroll ? pHelpDlg->pScroll->pUp_Left_Button->size.w: 0);
    pWindow = pHelpDlg->pEndWidgetList;
    pStore = (struct TECHS_BUTTONS *)pWindow->data.ptr;
    pDock = pStore->pDock;
    
    /* del. all usless widget */
    if (pDock  != pHelpDlg->pBeginWidgetList)
    {
      del_group_of_widgets_from_gui_list(pHelpDlg->pBeginWidgetList,
				       pDock->prev);
      pHelpDlg->pBeginWidgetList = pDock;
    }
  
    /* show/hide techs list */
    if (pStore->show_tree)
    {
      set_wstate(pDock, FC_WS_NORMAL);
    } else {
      set_wstate(pDock, FC_WS_DISABLED);
    }
    
    if (pStore->show_full_tree)
    {
      hide_group(pHelpDlg->pBeginActiveWidgetList,
		pHelpDlg->pEndActiveWidgetList);
      hide_scrollbar(pHelpDlg->pScroll);
    } else {
      int count = pHelpDlg->pScroll->active;
      pBuf = pHelpDlg->pActiveWidgetList;
      while(pBuf && count--)
      {
        pBuf = pBuf->prev;
      }
      pBuf = pBuf->next;
      show_group(pBuf, pHelpDlg->pActiveWidgetList);
      show_scrollbar(pHelpDlg->pScroll);
    }
  }
  
  /* --------------------------------------------------------- */ 
  if (created)
  {
    w = 640;
    h = 480;
    pWindow->size.x = (Main.screen->w - w) / 2;
    pWindow->size.y = (Main.screen->h - h) / 2;
  
    /* alloca window theme and win background buffer */
    pSurf = get_logo_gfx();
    if (resize_window(pWindow, pSurf, NULL, w, h))
    {
      FREESURFACE(pSurf);
    }

    /* exit button */
    pBuf = pWindow->prev;
    pBuf->size.x = pWindow->size.x + pWindow->size.w-pBuf->size.w-FRAME_WH-1;
    pBuf->size.y = pWindow->size.y + 1;
  
    /* toggle button */
    pStore->pDock->size.x = pWindow->size.x + FRAME_WH;
    pStore->pDock->size.y = pWindow->size.y +  WINDOW_TILE_HIGH + 1;
    
    h = setup_vertical_widgets_position(1, pWindow->size.x + FRAME_WH + width,
		  pWindow->size.y + WINDOW_TILE_HIGH + 17, 0, 0,
		  pHelpDlg->pBeginActiveWidgetList,
  		  pHelpDlg->pEndActiveWidgetList);
    
    if (pHelpDlg->pScroll)
    {
      setup_vertical_scrollbar_area(pHelpDlg->pScroll,
	pWindow->size.x + FRAME_WH,
    	pWindow->size.y + WINDOW_TILE_HIGH + 17,
    	h, FALSE);
    }
  }

  if (pStore->show_tree)
  {
    pHelpDlg->pBeginWidgetList = create_tech_tree(tech, width, pWindow, pStore);
    redraw_tech_tree_dlg();
  } else {
    pHelpDlg->pBeginWidgetList = create_tech_info(tech, width, pWindow, pStore);
    redraw_tech_info_dlg();
  }
}
