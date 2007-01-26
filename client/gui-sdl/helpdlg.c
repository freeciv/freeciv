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

#include <SDL/SDL.h>

/* utility */
#include "fcintl.h"
#include "log.h"

/* common */
#include "game.h"
#include "government.h"

/* client */
#include "helpdata.h"

/* gui-sdl */
#include "colors.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_tilespec.h"
#include "mapview.h"
#include "repodlgs.h"
#include "sprite.h"
#include "themespec.h"
#include "widget.h"

#include "helpdlg.h"

static struct ADVANCED_DLG *pHelpDlg = NULL;
  
struct TECHS_BUTTONS {
  struct widget *pTargets[6], *pSub_Targets[6];
  struct widget *pReq[2], *pSub_Req[4];
  struct widget *pDock;
  bool show_tree;
  bool show_full_tree;
};

struct UNITS_BUTTONS {
  struct widget *pObs;
  struct widget *pReq;
  struct widget *pDock;
};

enum help_page_type current_help_dlg = HELP_LAST;

static const int bufsz = 8192;

static int change_tech_callback(struct widget *pWidget);
  
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
    FC_FREE(pHelpDlg->pScroll);
    FC_FREE(pHelpDlg);
    current_help_dlg = HELP_LAST;
  }
}

static int help_dlg_window_callback(struct widget *pWindow)
{
  return -1;
}

static int exit_help_dlg_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    popdown_help_dialog();
    flush_dirty();
  }
  return -1;
}

/* =============================================== */

static int change_gov_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    popup_gov_info(MAX_ID - pWidget->ID);
  }
  return -1;
}

void popup_gov_info(int gov)
{ 
}


static int change_impr_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    popup_impr_info(MAX_ID - pWidget->ID);
  }
  return -1;
}

static void redraw_impr_info_dlg(void)
{
  SDL_Color bg_color = {255, 255, 255, 64};

  struct widget *pWindow = pHelpDlg->pEndWidgetList;
  struct UNITS_BUTTONS *pStore = (struct UNITS_BUTTONS *)pWindow->data.ptr;
  SDL_Rect dst;

  redraw_group(pWindow->prev, pWindow, FALSE);

  dst.x = pStore->pDock->prev->size.x - adj_size(10);
  dst.y = pStore->pDock->prev->size.y - adj_size(10);
  dst.w = pWindow->size.w - (dst.x - pWindow->size.x) - adj_size(10);
  dst.h = pWindow->size.h - (dst.y - pWindow->size.y) - adj_size(10);

  SDL_FillRectAlpha(pWindow->dst->surface, &dst, &bg_color);
  putframe(pWindow->dst->surface, dst.x , dst.y , dst.x + dst.w , dst.y + dst.h,
    map_rgba(pWindow->dst->surface->format, *get_game_colorRGB(COLOR_THEME_HELPDLG_FRAME)));
  
  /*------------------------------------- */
  redraw_group(pHelpDlg->pBeginWidgetList, pWindow->prev->prev, FALSE);
  widget_flush(pWindow);
}


void popup_impr_info(Impr_type_id impr)
{ 
  SDL_Color bg_color = {255, 255, 255, 128};

  struct widget *pBuf, *pHelpText = NULL;
  struct widget *pDock;
  struct widget *pWindow;
  struct UNITS_BUTTONS *pStore;
  SDL_String16 *pStr;
  SDL_Surface *pSurf;
  int w, h, start_x, start_y;
  bool created, text = FALSE;
  int width = 0;
  struct impr_type *pImpr_type;
  char buffer[64000];
  
  if(current_help_dlg != HELP_IMPROVEMENT)
  {
    popdown_help_dialog();
  }
  
  if (!pHelpDlg)
  {
    SDL_Surface *pText, *pBack, *pTmp;
    SDL_Rect dst;
    
    current_help_dlg = HELP_IMPROVEMENT;
    created = TRUE;
    pHelpDlg = fc_calloc(1, sizeof(struct ADVANCED_DLG));
    pStore = fc_calloc(1, sizeof(struct UNITS_BUTTONS));
    
    pStr = create_str16_from_char(_("Help : Improvement"), adj_font(12));
    pStr->style |= TTF_STYLE_BOLD;

    pWindow = create_window(NULL, pStr, 1, 1, WF_FREE_DATA);
    pWindow->action = help_dlg_window_callback;
    set_wstate(pWindow , FC_WS_NORMAL);
    pWindow->data.ptr = (void *)pStore;
    add_to_gui_list(ID_WINDOW, pWindow);
    pHelpDlg->pEndWidgetList = pWindow;
    /* ------------------ */
    
    /* exit button */
    pBuf = create_themeicon(pTheme->Small_CANCEL_Icon, pWindow->dst,
  			  			WF_RESTORE_BACKGROUND);
  
    /*w += pBuf->size.w + 10;*/
    pBuf->action = exit_help_dlg_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
    pBuf->key = SDLK_ESCAPE;
  
    add_to_gui_list(ID_BUTTON, pBuf);

    /* ------------------ */
    pDock = pBuf;
    
    pStr = create_string16(NULL, 0, adj_font(10));
    pStr->style |= (TTF_STYLE_BOLD | SF_CENTER);
    
    pText = create_surf_alpha(adj_size(140), adj_size(40), SDL_SWSURFACE);
    pTmp = pText;
    
    SDL_FillRect(pTmp, NULL, map_rgba(pTmp->format, bg_color));
    putframe(pTmp, 0,0, pTmp->w - 1, pTmp->h - 1, map_rgba(pTmp->format, *get_game_colorRGB(COLOR_THEME_HELPDLG_FRAME)));
    
    h = 0;
    impr_type_iterate(type)
    {
      pBack = SDL_DisplayFormatAlpha(pTmp);
      copy_chars_to_string16(pStr, get_improvement_name(type));
      pText = create_text_surf_smaller_that_w(pStr, adj_size(100 - 4));
      /* draw name tech text */ 
      dst.x = adj_size(40) + (pBack->w - pText->w - adj_size(40)) / 2;
      dst.y = (pBack->h - pText->h) / 2;
      alphablit(pText, NULL, pBack, &dst);
      FREESURFACE(pText);
    
      /* draw tech icon */
      pText = get_building_surface(type);
      pText = zoomSurface(pText, DEFAULT_ZOOM * ((float)36 / pText->w), DEFAULT_ZOOM * ((float)36 / pText->w), 1);
      dst.x = adj_size(5);
      dst.y = (pBack->h - pText->h) / 2;
      alphablit(pText, NULL, pBack, &dst);
      FREESURFACE(pText);
      
      pBuf = create_icon2(pBack, pWindow->dst,
      		WF_FREE_THEME | WF_RESTORE_BACKGROUND);

      set_wstate(pBuf, FC_WS_NORMAL);
      pBuf->action = change_impr_callback;
      add_to_gui_list(MAX_ID - type, pBuf);
      
      if (++h > 10)
      {
        set_wflag(pBuf, WF_HIDDEN);
      }

    } impr_type_iterate_end;
    
    FREESURFACE(pTmp);
    
    pHelpDlg->pEndActiveWidgetList = pDock->prev;
    pHelpDlg->pBeginWidgetList = pBuf;/* IMPORTANT */
    pHelpDlg->pBeginActiveWidgetList = pHelpDlg->pBeginWidgetList;
    
    if (h > 10) {
      pHelpDlg->pActiveWidgetList = pHelpDlg->pEndActiveWidgetList;
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
    pBuf->size.w = adj_size(160);
    pBuf->size.h = adj_size(15);
    pBuf->string16->fgcol = *get_game_colorRGB(COLOR_THEME_HELPDLG_TEXT);
  
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
  
  pSurf = get_building_surface(impr);
  pBuf= create_iconlabel_from_chars(
	  zoomSurface(pSurf, DEFAULT_ZOOM * ((float)108 / pSurf->w), DEFAULT_ZOOM * ((float)108 / pSurf->w), 1),
	  pWindow->dst, get_impr_name_ex(NULL, impr),
          adj_font(24), WF_FREE_THEME);

  pBuf->ID = ID_LABEL;
  DownAdd(pBuf, pDock);
  pDock = pBuf;
  
  if (!impr_flag(impr, IF_GOLD))
  {
    sprintf(buffer, "%s %d", N_("Cost:"), impr_build_shield_cost(impr));
    pBuf = create_iconlabel_from_chars(NULL,
		    pWindow->dst, buffer, adj_font(12), 0);
    pBuf->ID = ID_LABEL;
    DownAdd(pBuf, pDock);
    pDock = pBuf;
    if (!is_wonder(impr))
    {
      sprintf(buffer, "%s %d", N_("Upkeep:"), pImpr_type->upkeep);
      pBuf = create_iconlabel_from_chars(NULL,
		    pWindow->dst, buffer, adj_font(12), 0);
      pBuf->ID = ID_LABEL;
      DownAdd(pBuf, pDock);
      pDock = pBuf;
    }
  }
  pBuf = create_iconlabel_from_chars(NULL,
		    pWindow->dst, N_("Requirement:"), adj_font(12), 0);
  pBuf->ID = ID_LABEL;
  DownAdd(pBuf, pDock);
  pDock = pBuf;
  
  if (!(requirement_vector_size(&pImpr_type->reqs) > 0)) {
    pBuf = create_iconlabel_from_chars(NULL,
		    pWindow->dst, _("None"), adj_font(12), 0);
    pBuf->ID = ID_LABEL;
  } else {
    /* FIXME: this should show ranges and all the MAX_NUM_REQS reqs. 
     * Currently it's limited to 1 req. Remember MAX_NUM_REQS is a compile-time
     * definition. */
    requirement_vector_iterate(&pImpr_type->reqs, preq) {
      pBuf = create_iconlabel_from_chars(NULL, pWindow->dst,
	            get_req_source_text(&preq->source, buffer, sizeof(buffer)),
                    adj_font(12), WF_RESTORE_BACKGROUND);
      pBuf->ID = MAX_ID - preq->source.value.tech;
      pBuf->string16->fgcol = *get_tech_color(preq->source.value.tech);
      pBuf->action = change_tech_callback;
      set_wstate(pBuf, FC_WS_NORMAL);
      break;
    } requirement_vector_iterate_end;	
  }
  DownAdd(pBuf, pDock);
  pDock = pBuf;
  pStore->pReq = pBuf;
  
  pBuf = create_iconlabel_from_chars(NULL,
		    pWindow->dst, N_("Obsolete by:"), adj_font(12), 0);
  pBuf->ID = ID_LABEL;
  DownAdd(pBuf, pDock);
  pDock = pBuf;
  
  if(pImpr_type->obsolete_by==A_LAST || pImpr_type->obsolete_by==A_NONE)
  {
    pBuf = create_iconlabel_from_chars(NULL,
		    pWindow->dst, _("None"), adj_font(12), 0);
    pBuf->ID = ID_LABEL;
  } else {
    pBuf = create_iconlabel_from_chars(NULL, pWindow->dst,
	      advances[pImpr_type->obsolete_by].name, adj_font(12),
			  WF_RESTORE_BACKGROUND);
    pBuf->ID = MAX_ID - pImpr_type->obsolete_by;
    pBuf->string16->fgcol = *get_tech_color(pImpr_type->obsolete_by);
    pBuf->action = change_tech_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
  }
  DownAdd(pBuf, pDock);
  pDock = pBuf;
  pStore->pObs = pBuf;
    
  start_x = (pTheme->FR_Left->w + 1 + width + pHelpDlg->pEndActiveWidgetList->size.w + adj_size(20));
  
  buffer[0] = '\0';
  helptext_building(buffer, sizeof(buffer), impr, NULL);
  if (buffer[0] != '\0')
  {
    SDL_String16 *pStr = create_str16_from_char(buffer, adj_font(12));
    convert_string_to_const_surface_width(pStr,	adj_size(640) - start_x - adj_size(20));
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
    w = adj_size(640);
    h = adj_size(480);

    widget_set_position(pWindow,
                        (Main.screen->w - w) / 2,
                        (Main.screen->h - h) / 2);
    
    /* alloca window theme and win background buffer */
    pSurf = theme_get_background(theme, BACKGROUND_HELPDLG);
    if (resize_window(pWindow, pSurf, NULL, w, h))
    {
      FREESURFACE(pSurf);
    }

    /* exit button */
    pBuf = pWindow->prev;
    pBuf->size.x = pWindow->size.x + pWindow->size.w - pBuf->size.w - pTheme->FR_Right->w - 1;
    pBuf->size.y = pWindow->size.y + 1;
  
    /* toggle button */
    pStore->pDock->size.x = pWindow->size.x + pTheme->FR_Left->w;
    pStore->pDock->size.y = pWindow->size.y +  WINDOW_TITLE_HEIGHT + 1;
    
    h = setup_vertical_widgets_position(1, pWindow->size.x + pTheme->FR_Left->w + width,
		  pWindow->size.y + WINDOW_TITLE_HEIGHT + adj_size(17), 0, 0,
		  pHelpDlg->pBeginActiveWidgetList,
  		  pHelpDlg->pEndActiveWidgetList);
    
    if (pHelpDlg->pScroll)
    {
      setup_vertical_scrollbar_area(pHelpDlg->pScroll,
	pWindow->size.x + pTheme->FR_Left->w,
    	pWindow->size.y + WINDOW_TITLE_HEIGHT + adj_size(17),
    	h, FALSE);
    }
  }
  
  /* unittype  icon and label */
  pBuf = pStore->pDock->prev;
  pBuf->size.x = pWindow->size.x + start_x;
  pBuf->size.y = pWindow->size.y + WINDOW_TITLE_HEIGHT + adj_size(20);
  start_y = pBuf->size.y + pBuf->size.h + adj_size(10);
  
  if (!impr_flag(impr, IF_GOLD))
  {
    pBuf = pBuf->prev;
    pBuf->size.x = pWindow->size.x + start_x;
    pBuf->size.y = start_y;
    if (!is_wonder(impr))
    {
      pBuf = pBuf->prev;
      pBuf->size.x = pBuf->next->size.x + pBuf->next->size.w + adj_size(20);
      pBuf->size.y = start_y;
    }
    start_y += pBuf->size.h;
  }
  
  pBuf = pStore->pReq->next;
  pBuf->size.x = pWindow->size.x + start_x;
  pBuf->size.y = start_y;
  
  pStore->pReq->size.x = pBuf->size.x + pBuf->size.w + adj_size(5);
  pStore->pReq->size.y = start_y;
  
  if (pStore->pObs)
  {  
    pBuf = pStore->pObs->next;
    pBuf->size.x = pStore->pReq->size.x + pStore->pReq->size.w + adj_size(10);
    pBuf->size.y = start_y;
  
    pStore->pObs->size.x = pBuf->size.x + pBuf->size.w + adj_size(5);
    pStore->pObs->size.y = start_y;
    start_y += pStore->pObs->size.h;
  }
  
  start_y += adj_size(30);
  if (text)
  {
    pHelpText->size.x = pWindow->size.x + start_x;
    pHelpText->size.y = start_y;
  }
    
  
  redraw_impr_info_dlg();
}

/* ============================================ */
static int change_unit_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    popup_unit_info(MAX_ID - pWidget->ID);
  }
  return -1;
}

static void redraw_unit_info_dlg(void)
{
  SDL_Color bg_color = {255, 255, 255, 64};
  
  struct widget *pWindow = pHelpDlg->pEndWidgetList;
  struct UNITS_BUTTONS *pStore = (struct UNITS_BUTTONS *)pWindow->data.ptr;
  SDL_Rect dst;
  
  redraw_group(pWindow->prev, pWindow, FALSE);
    
  dst.x = pStore->pDock->prev->size.x - adj_size(10);
  dst.y = pStore->pDock->prev->size.y - adj_size(10);
  dst.w = pWindow->size.w - (dst.x - pWindow->size.x) - adj_size(10); 
  dst.h = pWindow->size.h - (dst.y - pWindow->size.y) - adj_size(10); 

  SDL_FillRectAlpha(pWindow->dst->surface, &dst, &bg_color);
  putframe(pWindow->dst->surface, dst.x , dst.y , dst.x + dst.w , dst.y + dst.h,
    map_rgba(pWindow->dst->surface->format, *get_game_colorRGB(COLOR_THEME_HELPDLG_FRAME)));
  
  /*------------------------------------- */
  redraw_group(pHelpDlg->pBeginWidgetList, pWindow->prev->prev, FALSE);
  widget_flush(pWindow);
}


void popup_unit_info(Unit_type_id type_id)
{ 
  SDL_Color bg_color = {255, 255, 255, 128};
  
  struct widget *pBuf;
  struct widget *pDock;
  struct widget *pWindow;
  struct UNITS_BUTTONS *pStore;
  SDL_String16 *pStr;
  SDL_Surface *pSurf;
  int w, h, start_x, start_y;
  bool created, text = FALSE;
  int width = 0;
  struct unit_type *pUnit;
  char buffer[bufsz];
  
  if(current_help_dlg != HELP_UNIT)
  {
    popdown_help_dialog();
  }
  
  if (!pHelpDlg)
  {
    SDL_Surface *pText, *pBack, *pTmp;
    SDL_Rect dst;
    
    current_help_dlg = HELP_UNIT;
    created = TRUE;
    pHelpDlg = fc_calloc(1, sizeof(struct ADVANCED_DLG));
    pStore = fc_calloc(1, sizeof(struct UNITS_BUTTONS));
    
    pStr = create_str16_from_char(_("Help : Units"), adj_font(12));
    pStr->style |= TTF_STYLE_BOLD;

    pWindow = create_window(NULL, pStr, 1, 1, WF_FREE_DATA);
    pWindow->action = help_dlg_window_callback;
    set_wstate(pWindow , FC_WS_NORMAL);
    pWindow->data.ptr = (void *)pStore;
    add_to_gui_list(ID_WINDOW, pWindow);
    pHelpDlg->pEndWidgetList = pWindow;
    /* ------------------ */
    
    /* exit button */
    pBuf = create_themeicon(pTheme->Small_CANCEL_Icon, pWindow->dst,
  			  			WF_RESTORE_BACKGROUND);
  
    pBuf->action = exit_help_dlg_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
    pBuf->key = SDLK_ESCAPE;
  
    add_to_gui_list(ID_BUTTON, pBuf);

    /* ------------------ */
    pDock = pBuf;
    
    pStr = create_string16(NULL, 0, adj_font(10));
    pStr->style |= (TTF_STYLE_BOLD | SF_CENTER);
    
    pText = create_surf_alpha(adj_size(135), adj_size(40), SDL_SWSURFACE);
    pTmp = pText;
    
    SDL_FillRect(pTmp, NULL, map_rgba(pTmp->format, bg_color));
    putframe(pTmp, 0,0, pTmp->w - 1, pTmp->h - 1, map_rgba(pTmp->format, *get_game_colorRGB(COLOR_THEME_HELPDLG_FRAME)));
    
    h = 0;
    unit_type_iterate(type) {
      pUnit = type;
	
      pBack = SDL_DisplayFormatAlpha(pTmp);
      
      copy_chars_to_string16(pStr, pUnit->name);
      pText = create_text_surf_smaller_that_w(pStr, adj_size(100 - 4));
      
      /* draw name tech text */ 
      dst.x = adj_size(35) + (pBack->w - pText->w - adj_size(35)) / 2;
      dst.y = (pBack->h - pText->h) / 2;
      alphablit(pText, NULL, pBack, &dst);
      FREESURFACE(pText);
    
      /* draw tech icon */
      {
	float zoom = DEFAULT_ZOOM * (25.0 / get_unittype_surface(type)->h);
        pText = zoomSurface(get_unittype_surface(type), zoom, zoom, 1);
      }
      dst.x = (adj_size(35) - pText->w) / 2;;
      dst.y = (pBack->h - pText->h) / 2;
      alphablit(pText, NULL, pBack, &dst);
      FREESURFACE(pText);

      pBuf = create_icon2(pBack, pWindow->dst,
      		WF_FREE_THEME | WF_RESTORE_BACKGROUND);

      set_wstate(pBuf, FC_WS_NORMAL);
      pBuf->action = change_unit_callback;
      add_to_gui_list(MAX_ID - type->index, pBuf);
      
      if (++h > 10)
      {
        set_wflag(pBuf, WF_HIDDEN);
      }

    } unit_type_iterate_end;
    
    FREESURFACE(pTmp);

    pHelpDlg->pEndActiveWidgetList = pDock->prev;
    pHelpDlg->pBeginWidgetList = pBuf;/* IMPORTANT */
    pHelpDlg->pBeginActiveWidgetList = pHelpDlg->pBeginWidgetList;
    
    if (h > 10) {
      pHelpDlg->pActiveWidgetList = pHelpDlg->pEndActiveWidgetList;
      width = create_vertical_scrollbar(pHelpDlg, 1, 10, TRUE, TRUE);
    }
        
    
    /* toggle techs list button */
    pBuf = create_themeicon_button_from_chars(pTheme->UP_Icon,
	      pWindow->dst,  _("Units"), adj_font(10), 0);
    /*pBuf->action = toggle_full_tree_mode_in_help_dlg_callback;
   if (pStore->show_tree)
    {
      set_wstate(pBuf, FC_WS_NORMAL);
    }
*/    
    pBuf->size.w = adj_size(160);
    pBuf->size.h = adj_size(15);
    pBuf->string16->fgcol = *get_game_colorRGB(COLOR_THEME_HELPDLG_TEXT);
  
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
  pBuf= create_iconlabel_from_chars(
          adj_surf(get_unittype_surface(get_unit_type(type_id))),
          pWindow->dst, pUnit->name, adj_font(24), WF_FREE_THEME);

  pBuf->ID = ID_LABEL;
  DownAdd(pBuf, pDock);
  pDock = pBuf;

  
  {
    char local[2048];
    
    my_snprintf(local, sizeof(local), "%s %d %s",
	      N_("Cost:"), unit_build_shield_cost(get_unit_type(type_id)),
	      PL_("shield", "shields", unit_build_shield_cost(get_unit_type(type_id))));
  
    if(pUnit->pop_cost)
    {
      cat_snprintf(local, sizeof(local), " %d %s",
	  pUnit->pop_cost, PL_("citizen", "citizens", pUnit->pop_cost));
    }
  
    cat_snprintf(local, sizeof(local), "      %s",  N_("Upkeep:"));
        
    if(pUnit->upkeep[O_SHIELD])
    {
      cat_snprintf(local, sizeof(local), " %d %s",
	  pUnit->upkeep[O_SHIELD], PL_("shield", "shields", pUnit->upkeep[O_SHIELD]));
     }
    if(pUnit->upkeep[O_FOOD])
    {
      cat_snprintf(local, sizeof(local), " %d %s",
	  pUnit->upkeep[O_FOOD], PL_("food", "foods", pUnit->upkeep[O_FOOD]));
    }
    if(pUnit->upkeep[O_GOLD])
    {
      cat_snprintf(local, sizeof(local), " %d %s",
	  pUnit->upkeep[O_GOLD], PL_("gold", "golds", pUnit->upkeep[O_GOLD]));
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
              N_("Vision:"), pUnit->vision_radius_sq,
	      N_("FirePower:"), pUnit->firepower,
              N_("Hitpoints:"), pUnit->hp);
  
    pBuf = create_iconlabel_from_chars(NULL,
		    pWindow->dst, local, adj_font(12), 0);
    pBuf->ID = ID_LABEL;
    DownAdd(pBuf, pDock);
    pDock = pBuf;
  }
  pBuf = create_iconlabel_from_chars(NULL,
		    pWindow->dst, N_("Requirement:"), adj_font(12), 0);
  pBuf->ID = ID_LABEL;
  DownAdd(pBuf, pDock);
  pDock = pBuf;
  
  if(pUnit->tech_requirement==A_LAST || pUnit->tech_requirement==A_NONE)
  {
    pBuf = create_iconlabel_from_chars(NULL,
		    pWindow->dst, _("None"), adj_font(12), 0);
    pBuf->ID = ID_LABEL;
  } else {
    pBuf = create_iconlabel_from_chars(NULL, pWindow->dst,
	  advances[pUnit->tech_requirement].name, adj_font(12),
			  WF_RESTORE_BACKGROUND);
    pBuf->ID = MAX_ID - pUnit->tech_requirement;
    pBuf->string16->fgcol = *get_tech_color(pUnit->tech_requirement);
    pBuf->action = change_tech_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
  }
  DownAdd(pBuf, pDock);
  pDock = pBuf;
  pStore->pReq = pBuf;
  
  pBuf = create_iconlabel_from_chars(NULL,
		    pWindow->dst, N_("Obsolete by:"), adj_font(12), 0);
  pBuf->ID = ID_LABEL;
  DownAdd(pBuf, pDock);
  pDock = pBuf;
  
  if (pUnit->obsoleted_by == U_NOT_OBSOLETED) {
    pBuf = create_iconlabel_from_chars(NULL,
		    pWindow->dst, _("None"), adj_font(12), 0);
    pBuf->ID = ID_LABEL;  
  } else {
    struct unit_type *utype = pUnit->obsoleted_by;
    pBuf = create_iconlabel_from_chars(NULL, pWindow->dst,
	      utype->name, adj_font(12), WF_RESTORE_BACKGROUND);
    pBuf->string16->fgcol = *get_tech_color(utype->tech_requirement);
    pBuf->ID = MAX_ID - pUnit->obsoleted_by->index;
    pBuf->action = change_unit_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
  }
  DownAdd(pBuf, pDock);
  pDock = pBuf;
  pStore->pObs = pBuf;
 
  start_x = (pTheme->FR_Left->w + 1 + width + pHelpDlg->pActiveWidgetList->size.w + adj_size(20));
  
  buffer[0] = '\0';
  helptext_unit(buffer, get_unit_type(type_id), "");
  if (buffer[0] != '\0')
  {
    SDL_String16 *pStr = create_str16_from_char(buffer, adj_font(12));
    convert_string_to_const_surface_width(pStr,	adj_size(640) - start_x - adj_size(20));
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
    w = adj_size(640);
    h = adj_size(480);
    
    widget_set_position(pWindow,
                        (Main.screen->w - w) / 2,
                        (Main.screen->h - h) / 2);
    
    /* alloca window theme and win background buffer */
    pSurf = theme_get_background(theme, BACKGROUND_HELPDLG);
    if (resize_window(pWindow, pSurf, NULL, w, h))
    {
      FREESURFACE(pSurf);
    }

    /* exit button */
    pBuf = pWindow->prev;
    pBuf->size.x = pWindow->size.x + pWindow->size.w - pBuf->size.w - pTheme->FR_Right->w - 1;
    pBuf->size.y = pWindow->size.y + 1;
  
    /* toggle button */
    pStore->pDock->size.x = pWindow->size.x + pTheme->FR_Left->w;
    pStore->pDock->size.y = pWindow->size.y +  WINDOW_TITLE_HEIGHT + 1;
    
    h = setup_vertical_widgets_position(1, pWindow->size.x + pTheme->FR_Left->w + width,
		  pWindow->size.y + WINDOW_TITLE_HEIGHT + adj_size(17), 0, 0,
		  pHelpDlg->pBeginActiveWidgetList,
  		  pHelpDlg->pEndActiveWidgetList);
    
    if (pHelpDlg->pScroll)
    {
      setup_vertical_scrollbar_area(pHelpDlg->pScroll,
	pWindow->size.x + pTheme->FR_Left->w,
    	pWindow->size.y + WINDOW_TITLE_HEIGHT + adj_size(17),
    	h, FALSE);
    }
  }
  
  /* unittype  icon and label */
  pBuf = pStore->pDock->prev;
  pBuf->size.x = pWindow->size.x + start_x;
  pBuf->size.y = pWindow->size.y + WINDOW_TITLE_HEIGHT + adj_size(20);
  start_y = pBuf->size.y + pBuf->size.h + adj_size(10);
  
  pBuf = pBuf->prev;
  pBuf->size.x = pWindow->size.x + start_x;
  pBuf->size.y = start_y;
  start_y += pBuf->size.h;
    
  pBuf = pStore->pReq->next;
  pBuf->size.x = pWindow->size.x + start_x;
  pBuf->size.y = start_y;
  
  pStore->pReq->size.x = pBuf->size.x + pBuf->size.w + adj_size(5);
  pStore->pReq->size.y = start_y;
    
  pBuf = pStore->pObs->next;
  pBuf->size.x = pStore->pReq->size.x + pStore->pReq->size.w + adj_size(10);
  pBuf->size.y = start_y;
  
  pStore->pObs->size.x = pBuf->size.x + pBuf->size.w + adj_size(5);
  pStore->pObs->size.y = start_y;
  start_y += pStore->pObs->size.h + adj_size(20);
  
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


static int change_tech_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    popup_tech_info(MAX_ID - pWidget->ID);
  }
  return -1;
}

static int show_help_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    struct TECHS_BUTTONS *pStore = (struct TECHS_BUTTONS *)pHelpDlg->pEndWidgetList->data.ptr;
    pStore->show_tree = !pStore->show_tree;
    if (!pStore->show_tree)
    {
      pStore->show_full_tree = FALSE;
      pStore->pDock->theme2 = pTheme->UP_Icon;
    }
    popup_tech_info(MAX_ID - pStore->pDock->prev->ID);
  }
  return -1;
}

static void redraw_tech_info_dlg(void)
{
  SDL_Color bg_color = {255, 255, 255, 64};
  
  struct widget *pWindow = pHelpDlg->pEndWidgetList;
  struct TECHS_BUTTONS *pStore = (struct TECHS_BUTTONS *)pWindow->data.ptr;
  SDL_Surface *pText0, *pText1 = NULL;
  SDL_String16 *pStr;
  SDL_Rect dst;
  
  redraw_group(pWindow->prev, pWindow, FALSE);
    
  dst.x = pStore->pDock->prev->prev->size.x - adj_size(10);
  dst.y = pStore->pDock->prev->prev->size.y - adj_size(10);
  dst.w = pWindow->size.w - (dst.x - pWindow->size.x) - adj_size(10); 
  dst.h = pWindow->size.h - (dst.y - pWindow->size.y) - adj_size(10); 

  SDL_FillRectAlpha(pWindow->dst->surface, &dst, &bg_color);
  putframe(pWindow->dst->surface, dst.x , dst.y , dst.x + dst.w , dst.y + dst.h,
    map_rgba(pWindow->dst->surface->format, *get_game_colorRGB(COLOR_THEME_HELPDLG_FRAME)));
  
  /* -------------------------- */
  pStr = create_str16_from_char(_("Allows"), adj_font(14));
  pStr->style |= TTF_STYLE_BOLD;
  
  pText0 = create_text_surf_from_str16(pStr);
  dst.x = pStore->pDock->prev->prev->size.x;
  if (pStore->pTargets[0])
  {
    dst.y = pStore->pTargets[0]->size.y - pText0->h;
  } else {
    dst.y = pStore->pDock->prev->prev->size.y 
	      + pStore->pDock->prev->prev->size.h + adj_size(10);
  }

  alphablit(pText0, NULL, pWindow->dst->surface, &dst);
  FREESURFACE(pText0);

  if (pStore->pSub_Targets[0])
  {
    int i;
      
    change_ptsize16(pStr, adj_font(12));
      
    copy_chars_to_string16(pStr, _("( witch "));
    pText0 = create_text_surf_from_str16(pStr);
    
    copy_chars_to_string16(pStr, _(" )"));
    pText1 = create_text_surf_from_str16(pStr);
    i = 0;
    while(i < 6 && pStore->pSub_Targets[i])
    {
      dst.x = pStore->pSub_Targets[i]->size.x - pText0->w;
      dst.y = pStore->pSub_Targets[i]->size.y;

      alphablit(pText0, NULL, pWindow->dst->surface, &dst);
      dst.x = pStore->pSub_Targets[i]->size.x + pStore->pSub_Targets[i]->size.w;
      dst.y = pStore->pSub_Targets[i]->size.y;

      alphablit(pText1, NULL, pWindow->dst->surface, &dst);
      i++;
    }
      
    FREESURFACE(pText0);
    FREESURFACE(pText1);
  }
  FREESTRING16(pStr);
  
  redraw_group(pHelpDlg->pBeginWidgetList, pWindow->prev->prev, FALSE);
  widget_flush(pWindow);
}

static struct widget * create_tech_info(Tech_type_id tech, int width, struct widget *pWindow, struct TECHS_BUTTONS *pStore)
{
  struct widget *pBuf;
  struct widget *pLast, *pBudynki;
  struct widget *pDock = pStore->pDock;
  int i, targets_count,sub_targets_count, max_width = 0;
  int start_x, start_y, imp_count, unit_count, flags_count, gov_count;
  char buffer[bufsz];
  SDL_Surface *pSurf;
  
  start_x = (pTheme->FR_Left->w + 1 + width + pHelpDlg->pActiveWidgetList->size.w + adj_size(20));
  
  /* tech tree icon */
  pBuf = create_icon2(pTheme->Tech_Tree_Icon, pWindow->dst, WF_RESTORE_BACKGROUND);
  
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->action = show_help_callback;
  pBuf->ID = MAX_ID - tech;
  DownAdd(pBuf, pDock);
  pDock = pBuf;
  
  /* tech name (heading) */
  pBuf= create_iconlabel_from_chars(get_tech_icon(tech),
		    pWindow->dst, advances[tech].name, adj_font(24), WF_FREE_THEME);

  pBuf->ID = ID_LABEL;
  DownAdd(pBuf, pDock);
  pDock = pBuf;
  
  /* target techs */
  targets_count = 0;
  for(i=A_FIRST; i<game.control.num_tech_types; i++)
  {
    if ((targets_count < 6) &&
        (advances[i].req[0] == tech || advances[i].req[1] == tech))
    {
      pBuf= create_iconlabel_from_chars(NULL, pWindow->dst, advances[i].name,
                                    adj_font(12), WF_RESTORE_BACKGROUND);
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
  if (targets_count < 6) {
    pStore->pTargets[targets_count] = NULL;
  }
  
  sub_targets_count = 0;
  if (targets_count > 0)
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
      pBuf= create_iconlabel_from_chars(NULL, pWindow->dst,
           advances[sub_tech].name, adj_font(12), WF_RESTORE_BACKGROUND);
      pBuf->string16->fgcol = *get_tech_color(sub_tech);
      set_wstate(pBuf, FC_WS_NORMAL);
      pBuf->action = change_tech_callback;
      pBuf->ID = MAX_ID - sub_tech;
      DownAdd(pBuf, pDock);
      pDock = pBuf;
      pStore->pSub_Targets[sub_targets_count++] = pBuf;
    }
  }
  if (sub_targets_count < 6)
  {
    pStore->pSub_Targets[sub_targets_count] = NULL;
  }
  
  /* fill array with iprvm. icons */
  pBudynki = pBuf;
  
  /* target governments */
  gov_count = 0;
  government_iterate(gov) {
    requirement_vector_iterate(&(gov->reqs), preq) {
      if ((preq->source.type == REQ_TECH) && (preq->source.value.tech == tech)) {
                  
        pBuf = create_iconlabel_from_chars(adj_surf(get_government_surface(gov)),
                pWindow->dst, gov->name, adj_font(14),
                WF_RESTORE_BACKGROUND|WF_SELLECT_WITHOUT_BAR | WF_FREE_THEME);
        set_wstate(pBuf, FC_WS_NORMAL);
        pBuf->action = change_gov_callback;
        pBuf->ID = MAX_ID - gov->index;
        DownAdd(pBuf, pDock);
        pDock = pBuf;
        gov_count++;
      }
    } requirement_vector_iterate_end;		
  } government_iterate_end;
  
  /* target improvements */
  imp_count = 0;
  impr_type_iterate(imp) {
    /* FIXME: this should show ranges and all the MAX_NUM_REQS reqs. 
     * Currently it's limited to 1 req. Remember MAX_NUM_REQS is a compile-time
     * definition. */
    requirement_vector_iterate(&(get_improvement_type(imp)->reqs), preq) {
      if ((preq->source.type == REQ_TECH) && (preq->source.value.tech == tech)) {
        pSurf = get_building_surface(imp);
        pBuf = create_iconlabel_from_chars(
                zoomSurface(pSurf, DEFAULT_ZOOM * ((float)36 / pSurf->w), DEFAULT_ZOOM * ((float)36 / pSurf->w), 1),
                pWindow->dst, get_improvement_name(imp), adj_font(14),
                WF_RESTORE_BACKGROUND|WF_SELLECT_WITHOUT_BAR);
        set_wstate(pBuf, FC_WS_NORMAL);
        if (is_wonder(imp))
        {
               pBuf->string16->fgcol = *get_game_colorRGB(COLOR_THEME_CITYDLG_LUX);
        }
        pBuf->action = change_impr_callback;
        pBuf->ID = MAX_ID - imp;
        DownAdd(pBuf, pDock);
        pDock = pBuf;
        imp_count++;
      }
      
      break;
    } requirement_vector_iterate_end;	
  } impr_type_iterate_end;
  
  unit_count = 0;
  unit_type_iterate(un) {
    struct unit_type *pUnit = un;
    if (pUnit->tech_requirement == tech) {
      if (get_unittype_surface(un)->w > 64)
      {
	float zoom = DEFAULT_ZOOM * (64.0 / get_unittype_surface(un)->w);
        pBuf = create_iconlabel_from_chars(zoomSurface(get_unittype_surface(un), zoom, zoom, 1),
	      pWindow->dst, pUnit->name, adj_font(14),
	      (WF_FREE_THEME|WF_RESTORE_BACKGROUND|WF_SELLECT_WITHOUT_BAR));
      } else {
	pBuf = create_iconlabel_from_chars(adj_surf(get_unittype_surface(un)),
	      pWindow->dst, pUnit->name, adj_font(14),
	      (WF_RESTORE_BACKGROUND|WF_SELLECT_WITHOUT_BAR | WF_FREE_THEME));
      }
      set_wstate(pBuf, FC_WS_NORMAL);
      pBuf->action = change_unit_callback;
      pBuf->ID = MAX_ID - un->index;
      DownAdd(pBuf, pDock);
      pDock = pBuf;
      unit_count++;
    }
  } unit_type_iterate_end;
  
  buffer[0] = '\0';
  helptext_tech(buffer, sizeof(buffer), tech, "");
  if (buffer[0] != '\0')
  {
    SDL_String16 *pStr = create_str16_from_char(buffer, adj_font(12));
    convert_string_to_const_surface_width(pStr,	adj_size(640) - start_x - adj_size(20));
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
  pBuf->size.x = pWindow->size.x + pWindow->size.w - pBuf->size.w - adj_size(20);
  pBuf->size.y = pWindow->size.y + WINDOW_TITLE_HEIGHT + adj_size(20);
  
  /* Tech label */
  pBuf = pBuf->prev;
  pBuf->size.x = pWindow->size.x + start_x;
  pBuf->size.y = pWindow->size.y + WINDOW_TITLE_HEIGHT + adj_size(20);
  start_y = pBuf->size.y + pBuf->size.h + adj_size(30);
  
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
    
    start_y += adj_size(10);
  }
  pBuf = NULL;
  
  if (gov_count)
  {
    pBuf = pBudynki->prev;
    while(gov_count-- && pBuf)
    {
      pBuf->size.x = pWindow->size.x + start_x;
      pBuf->size.y = start_y;
      start_y += pBuf->size.h + adj_size(2);
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
      start_y += pBuf->size.h + adj_size(2);
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
      start_y += pBuf->size.h + adj_size(2);
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
      start_y += pBuf->size.h + adj_size(2);
      pBuf = pBuf->prev;
    }
  }
  
  return pLast;
}


static void redraw_tech_tree_dlg(void)
{
  SDL_Color line_color = *get_game_colorRGB(COLOR_THEME_HELPDLG_LINE);
  SDL_Color bg_color = {255, 255, 255, 64};

  struct widget *pWindow = pHelpDlg->pEndWidgetList;
  struct widget *pSub0, *pSub1;
  struct TECHS_BUTTONS *pStore = (struct TECHS_BUTTONS *)pWindow->data.ptr;
  struct widget *pTech = pStore->pDock->prev;
  int i,j, tech, count, step, mod;
  SDL_Rect dst;
  
  /* Redraw Window with exit button */ 
  redraw_group(pWindow->prev, pWindow, FALSE);
  
  dst.x = pWindow->size.x + pWindow->size.w - adj_size(459) - adj_size(10);
  dst.y = pWindow->size.y + WINDOW_TITLE_HEIGHT + adj_size(10);
  dst.w = pWindow->size.w - (dst.x - pWindow->size.x) - adj_size(10); 
  dst.h = pWindow->size.h - (dst.y - pWindow->size.y) - adj_size(10); 

  SDL_FillRectAlpha(pWindow->dst->surface, &dst, &bg_color);
  putframe(pWindow->dst->surface, dst.x , dst.y , dst.x + dst.w , dst.y + dst.h,
    map_rgba(pWindow->dst->surface->format, *get_game_colorRGB(COLOR_THEME_HELPDLG_FRAME)));
   
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
    putline(pStore->pReq[i]->dst->surface,
        pStore->pReq[i]->size.x + pStore->pReq[i]->size.w,
        pStore->pReq[i]->size.y + pStore->pReq[i]->size.h / 2,
        pTech->size.x,
        pStore->pReq[i]->size.y + pStore->pReq[i]->size.h / 2,
        map_rgba(pStore->pReq[i]->dst->surface->format, line_color));
    
    /* Draw Sub_Req arrows */
    if (pSub0 || pSub1)
    {
      putline(pStore->pReq[i]->dst->surface,
        pStore->pReq[i]->size.x - adj_size(10),
        pStore->pReq[i]->size.y + pStore->pReq[i]->size.h / 2,
        pStore->pReq[i]->size.x ,
        pStore->pReq[i]->size.y + pStore->pReq[i]->size.h / 2,
        map_rgba(pStore->pReq[i]->dst->surface->format, line_color));
    }
    
    if(pSub0)
    {
      putline(pStore->pReq[i]->dst->surface,
        pStore->pReq[i]->size.x - adj_size(10),
        pSub0->size.y + pSub0->size.h / 2,
        pStore->pReq[i]->size.x - adj_size(10),
        pStore->pReq[i]->size.y + pStore->pReq[i]->size.h / 2,
        map_rgba(pStore->pReq[i]->dst->surface->format, line_color));
      putline(pStore->pReq[i]->dst->surface,
        pSub0->size.x + pSub0->size.w,
        pSub0->size.y + pSub0->size.h / 2,
        pStore->pReq[i]->size.x - adj_size(10),
        pSub0->size.y + pSub0->size.h / 2,
        map_rgba(pStore->pReq[i]->dst->surface->format, line_color));
    }
    
    if(pSub1)
    {
      putline(pStore->pReq[i]->dst->surface,
        pStore->pReq[i]->size.x - adj_size(10),
        pSub1->size.y + pSub1->size.h / 2,
        pStore->pReq[i]->size.x - adj_size(10),
        pStore->pReq[i]->size.y + pStore->pReq[i]->size.h / 2,
        map_rgba(pStore->pReq[i]->dst->surface->format, line_color));
      putline(pStore->pReq[i]->dst->surface,
        pSub1->size.x + pSub1->size.w,
        pSub1->size.y + pSub1->size.h / 2,
        pStore->pReq[i]->size.x - adj_size(10),
        pSub1->size.y + pSub1->size.h / 2,
        map_rgba(pStore->pReq[i]->dst->surface->format, line_color));
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
        line_color = *get_game_colorRGB(COLOR_THEME_HELPDLG_LINE2);
      break;
      case 1:
        line_color = *get_game_colorRGB(COLOR_THEME_HELPDLG_LINE3);
      break;
      default:
        line_color = *get_game_colorRGB(COLOR_THEME_HELPDLG_LINE);
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
      putline(pStore->pTargets[i]->dst->surface,
        pStore->pTargets[i]->size.x - ((i % mod) + 1) * 6,
        pStore->pTargets[i]->size.y + pStore->pTargets[i]->size.h / 2,
        pStore->pTargets[i]->size.x ,
        pStore->pTargets[i]->size.y + pStore->pTargets[i]->size.h / 2,
        map_rgba(pStore->pTargets[i]->dst->surface->format, line_color));
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
      
      putline(pStore->pTargets[i]->dst->surface,
        pStore->pTargets[i]->size.x - ((i % mod) + 1) * 6,
        y,
        pStore->pTargets[i]->size.x - ((i % mod) + 1) * 6,
        pStore->pTargets[i]->size.y + pStore->pTargets[i]->size.h / 2,
        map_rgba(pStore->pTargets[i]->dst->surface->format, line_color));
      putline(pStore->pTargets[i]->dst->surface,
        pSub0->size.x + pSub0->size.w,
        y,
        pStore->pTargets[i]->size.x - ((i % mod) + 1) * 6,
        y,
        map_rgba(pStore->pTargets[i]->dst->surface->format, line_color));
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
      putline(pStore->pTargets[i]->dst->surface,
        pStore->pTargets[i]->size.x - ((i % mod) + 1) * 6,
        y,
        pStore->pTargets[i]->size.x - ((i % mod) + 1) * 6,
        pStore->pTargets[i]->size.y + pStore->pTargets[i]->size.h / 2,
        map_rgba(pStore->pTargets[i]->dst->surface->format, line_color));
      putline(pStore->pTargets[i]->dst->surface,
        pSub1->size.x + pSub1->size.w,
        y,
        pStore->pTargets[i]->size.x - ((i % mod) + 1) * 6,
        y,
        map_rgba(pStore->pTargets[i]->dst->surface->format, line_color));
    }
  }

  /* Redraw rest */
  redraw_group(pHelpDlg->pBeginWidgetList, pWindow->prev->prev, FALSE);
  
  widget_flush(pWindow);
}

static int toggle_full_tree_mode_in_help_dlg_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    struct TECHS_BUTTONS *pStore = (struct TECHS_BUTTONS *)pHelpDlg->pEndWidgetList->data.ptr;
    if (pStore->show_full_tree)
    {
      pWidget->theme2 = pTheme->UP_Icon;
    } else {
      pWidget->theme2 = pTheme->DOWN_Icon;
    }
    pStore->show_full_tree = !pStore->show_full_tree;
    popup_tech_info(MAX_ID - pStore->pDock->prev->ID);
  }
  return -1;
}


static struct widget * create_tech_tree(Tech_type_id tech, int width, struct widget *pWindow, struct TECHS_BUTTONS *pStore)
{
  int i, w, h, req_count , targets_count, sub_req_count, sub_targets_count;
  struct widget *pBuf;
  struct widget *pTech;
  SDL_String16 *pStr;
  SDL_Surface *pSurf;
  struct widget *pDock = pStore->pDock;
    
  pStr = create_string16(NULL, 0, adj_font(10));
  pStr->style |= (TTF_STYLE_BOLD | SF_CENTER);

  copy_chars_to_string16(pStr, advances[tech].name);
  pSurf = create_sellect_tech_icon(pStr, tech, FULL_MODE);
  pBuf = create_icon2(pSurf, pWindow->dst,
      		WF_FREE_THEME | WF_RESTORE_BACKGROUND);

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
      		WF_FREE_THEME | WF_RESTORE_BACKGROUND);
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
      		WF_FREE_THEME | WF_RESTORE_BACKGROUND);
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
  for(i=A_FIRST; i<game.control.num_tech_types; i++)
  {
    if ((targets_count<6)
      && (advances[i].req[0] == tech || advances[i].req[1] == tech))
    {
      copy_chars_to_string16(pStr, advances[i].name);
      pSurf = create_sellect_tech_icon(pStr, i, SMALL_MODE);
      pBuf = create_icon2(pSurf, pWindow->dst,
      		WF_FREE_THEME | WF_RESTORE_BACKGROUND);

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
      	WF_FREE_THEME | WF_RESTORE_BACKGROUND);
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
    w = (adj_size(20) + pStore->pSub_Req[0]->size.w) * 2;
    w += (pWindow->size.w - (20 + pStore->pSub_Req[0]->size.w + w + pTech->size.w)) / 2;
  } else {
    if (req_count)
    {
      w = (pTheme->FR_Left->w + 1 + width + pStore->pReq[0]->size.w * 2 + adj_size(20));
      w += (pWindow->size.w - ((adj_size(20) + pStore->pReq[0]->size.w) + w + pTech->size.w)) / 2;
    } else {
      w = (pWindow->size.w - pTech->size.w) / 2;
    }
  }

  pTech->size.x = pWindow->size.x + w;
  pTech->size.y = pWindow->size.y + WINDOW_TITLE_HEIGHT + (pWindow->size.h - pTech->size.h - WINDOW_TITLE_HEIGHT) / 2;

  if(req_count)
  {
    h = (req_count == 1 ? pStore->pReq[0]->size.h : 
        req_count * (pStore->pReq[0]->size.h + adj_size(80)) - adj_size(80));
    h = pTech->size.y + (pTech->size.h - h) / 2;
    for(i =0; i <req_count; i++)
    {
      pStore->pReq[i]->size.x = pTech->size.x - adj_size(20) - pStore->pReq[i]->size.w;
      pStore->pReq[i]->size.y = h;
      h += (pStore->pReq[i]->size.h + adj_size(80));
    }
  }
  
  if(sub_req_count)
  {
    h = (sub_req_count == 1 ? pStore->pSub_Req[0]->size.h :
     sub_req_count * (pStore->pSub_Req[0]->size.h + adj_size(20)) - adj_size(20));
    h = pTech->size.y + (pTech->size.h - h) / 2;
    for(i =0; i <sub_req_count; i++)
    {
      pStore->pSub_Req[i]->size.x = pTech->size.x - (adj_size(20) + pStore->pSub_Req[i]->size.w) * 2;
      pStore->pSub_Req[i]->size.y = h;
      h += (pStore->pSub_Req[i]->size.h + adj_size(20));
    }
  }
  
  if(targets_count)
  {
    h = (targets_count == 1 ? pStore->pTargets[0]->size.h :
     targets_count * (pStore->pTargets[0]->size.h + adj_size(20)) - adj_size(20));
    h = pTech->size.y + (pTech->size.h - h) / 2;
    for(i =0; i <targets_count; i++)
    {  
      pStore->pTargets[i]->size.x = pTech->size.x + pTech->size.w + adj_size(20);
      pStore->pTargets[i]->size.y = h;
      h += (pStore->pTargets[i]->size.h + adj_size(20));
    }
  }
  
  if(sub_targets_count)
  {
    if(sub_targets_count < 3)
    {
      pStore->pSub_Targets[0]->size.x = pTech->size.x + pTech->size.w - pStore->pSub_Targets[0]->size.w;
      pStore->pSub_Targets[0]->size.y = pTech->size.y - pStore->pSub_Targets[0]->size.h - adj_size(10);
      if (pStore->pSub_Targets[1])
      {
	pStore->pSub_Targets[1]->size.x = pTech->size.x + pTech->size.w - pStore->pSub_Targets[1]->size.w;
        pStore->pSub_Targets[1]->size.y = pTech->size.y + pTech->size.h + adj_size(10);
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
            pStore->pSub_Targets[i]->size.y = pTech->size.y - (pStore->pSub_Targets[i]->size.h + adj_size(5)) * ( 2 - i );
	  } else {
	    pStore->pSub_Targets[i]->size.y = pTech->size.y + pTech->size.h + adj_size(5)  + (pStore->pSub_Targets[i]->size.h + adj_size(5)) * ( i - 2 );
	  }
        }
      } else {
	h = (pStore->pSub_Targets[0]->size.h + adj_size(6));
	for(i =0; i <MIN(sub_targets_count, 6); i++)
        {
	  switch(i)
	  {
	    case 0:
	      pStore->pSub_Targets[i]->size.x = pTech->size.x + pTech->size.w - pStore->pSub_Targets[i]->size.w;
	      pStore->pSub_Targets[i]->size.y = pTech->size.y - h * 2;
	    break;
	    case 1:
	      pStore->pSub_Targets[i]->size.x = pTech->size.x + pTech->size.w - pStore->pSub_Targets[i]->size.w * 2 - adj_size(10);
	      pStore->pSub_Targets[i]->size.y = pTech->size.y - h - h / 2;
	    break;
	    case 2:
	      pStore->pSub_Targets[i]->size.x = pTech->size.x + pTech->size.w - pStore->pSub_Targets[i]->size.w;
	      pStore->pSub_Targets[i]->size.y = pTech->size.y - h;
	    break;
	    case 3:
	      pStore->pSub_Targets[i]->size.x = pTech->size.x + pTech->size.w - pStore->pSub_Targets[i]->size.w;
	      pStore->pSub_Targets[i]->size.y = pTech->size.y + pTech->size.h + adj_size(6);
	    break;
	    case 4:
	      pStore->pSub_Targets[i]->size.x = pTech->size.x + pTech->size.w - pStore->pSub_Targets[i]->size.w;
	      pStore->pSub_Targets[i]->size.y = pTech->size.y + pTech->size.h + adj_size(6) + h;
	    break;
	    default:
	      pStore->pSub_Targets[i]->size.x = pTech->size.x + pTech->size.w - pStore->pSub_Targets[i]->size.w * 2 - adj_size(10);
	      pStore->pSub_Targets[i]->size.y = pTech->size.y + pTech->size.h + adj_size(6) + h / 2 ;
	    break;
	  };
        }
      }
    }
  }
    
  return pBuf;
}

void popup_tech_info(Tech_type_id tech)
{ 
  struct widget *pBuf;
  struct widget *pDock;
  struct widget *pWindow;
  struct TECHS_BUTTONS *pStore;
  SDL_String16 *pStr;
  SDL_Surface *pSurf;
  int i, w, h;
  bool created;
  int width = 0;
  
  if(current_help_dlg != HELP_TECH)
  {
    popdown_help_dialog();
  }
  
  if (!pHelpDlg)
  {
    created = TRUE;
    current_help_dlg = HELP_TECH;
    pHelpDlg = fc_calloc(1, sizeof(struct ADVANCED_DLG));
    pStore = fc_calloc(1, sizeof(struct TECHS_BUTTONS));
      
    memset(pStore, 0, sizeof(struct TECHS_BUTTONS));
  
    pStore->show_tree = FALSE;
    pStore->show_full_tree = FALSE;
    
    pStr = create_str16_from_char(_("Help : Advances Tree"), adj_font(12));
    pStr->style |= TTF_STYLE_BOLD;

    pWindow = create_window(NULL, pStr, 1, 1, WF_FREE_DATA);
    pWindow->data.ptr = (void *)pStore;
    pWindow->action = help_dlg_window_callback;
    set_wstate(pWindow , FC_WS_NORMAL);
      
    add_to_gui_list(ID_WINDOW, pWindow);
    pHelpDlg->pEndWidgetList = pWindow;
    /* ------------------ */
    
    /* exit button */
    pBuf = create_themeicon(pTheme->Small_CANCEL_Icon, pWindow->dst,
  			  			WF_RESTORE_BACKGROUND);
  
    /*w += pBuf->size.w + 10;*/
    pBuf->action = exit_help_dlg_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
    pBuf->key = SDLK_ESCAPE;
  
    add_to_gui_list(ID_BUTTON, pBuf);

    /* ------------------ */
    pDock = pBuf;
    pStr = create_string16(NULL, 0, adj_font(10));
    pStr->style |= (TTF_STYLE_BOLD | SF_CENTER);
    
    h = 0;
    for(i=A_FIRST; i<game.control.num_tech_types; i++)
    {
      if (tech_exists(i))
      {
        copy_chars_to_string16(pStr, advances[i].name);
        pSurf = create_sellect_tech_icon(pStr, i, SMALL_MODE);
        pBuf = create_icon2(pSurf, pWindow->dst,
      		WF_FREE_THEME | WF_RESTORE_BACKGROUND);

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

    pHelpDlg->pEndActiveWidgetList = pDock->prev;
    pHelpDlg->pBeginWidgetList = pBuf;/* IMPORTANT */
    pHelpDlg->pBeginActiveWidgetList = pHelpDlg->pBeginWidgetList;
    
    if (h > 10) {
      pHelpDlg->pActiveWidgetList = pHelpDlg->pEndActiveWidgetList;
      width = create_vertical_scrollbar(pHelpDlg, 1, 10, TRUE, TRUE);
    }
        
    
    /* toggle techs list button */
    pBuf = create_themeicon_button_from_chars(pTheme->UP_Icon,
	      pWindow->dst,  _("Advances"), adj_font(10), 0);
    pBuf->action = toggle_full_tree_mode_in_help_dlg_callback;
    if (pStore->show_tree)
    {
      set_wstate(pBuf, FC_WS_NORMAL);
    }
    pBuf->size.w = adj_size(160);
    pBuf->size.h = adj_size(15);
    pBuf->string16->fgcol = *get_game_colorRGB(COLOR_THEME_HELPDLG_TEXT);
    /*pBuf->key = SDLK_ESCAPE;*/
  
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
    w = adj_size(640);
    h = adj_size(480);
    
    widget_set_position(pWindow,
                        (Main.screen->w - w) / 2,
                        (Main.screen->h - h) / 2);
    
    /* alloca window theme and win background buffer */
    pSurf = theme_get_background(theme, BACKGROUND_HELPDLG);
    if (resize_window(pWindow, pSurf, NULL, w, h))
    {
      FREESURFACE(pSurf);
    }

    /* exit button */
    pBuf = pWindow->prev;
    pBuf->size.x = pWindow->size.x + pWindow->size.w - pBuf->size.w - pTheme->FR_Right->w - 1;
    pBuf->size.y = pWindow->size.y + 1;
  
    /* toggle button */
    pStore->pDock->size.x = pWindow->size.x + pTheme->FR_Left->w;
    pStore->pDock->size.y = pWindow->size.y +  WINDOW_TITLE_HEIGHT + 1;
    
    h = setup_vertical_widgets_position(1, pWindow->size.x + pTheme->FR_Left->w + width,
		  pWindow->size.y + WINDOW_TITLE_HEIGHT + adj_size(17), 0, 0,
		  pHelpDlg->pBeginActiveWidgetList,
  		  pHelpDlg->pEndActiveWidgetList);
    
    if (pHelpDlg->pScroll)
    {
      setup_vertical_scrollbar_area(pHelpDlg->pScroll,
	pWindow->size.x + pTheme->FR_Left->w,
    	pWindow->size.y + WINDOW_TITLE_HEIGHT + adj_size(17),
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
