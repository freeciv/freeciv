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
                          repodlgs.c  -  description
                             -------------------
    begin                : Nov 15 2002
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL/SDL.h>

#include "gui_mem.h"

#include "fcintl.h"
#include "log.h"
#include "game.h"
#include "government.h"
#include "packets.h"
#include "shared.h"
#include "support.h"

#include "cityrep.h"
#include "civclient.h"
#include "clinet.h"
#include "dialogs.h"
#include "graphics.h"
#include "colors.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_string.h"
#include "gui_zoom.h"
#include "gui_stuff.h"
#include "helpdlg.h"
#include "optiondlg.h"
#include "gui_tilespec.h"
#include "tilespec.h"

#include "mapview.h"
#include "mapctrl.h"
#include "repodlgs_common.h"
#include "repodlgs.h"


static SDL_Surface *pNone_Tech_Icon;
static SDL_Surface *pFuture_Tech_Icon;

/* ===================================================================== */
/* ======================== Active Units Report ======================== */
/* ===================================================================== */
static struct ADVANCED_DLG *pUnitsDlg = NULL;
static struct SMALL_DLG *pUnits_Upg_Dlg = NULL;
struct units_entry {
  int active_count;
  int upkeep_shield;
  int upkeep_food;
  int upkeep_gold;
  int building_count;
  int soonest_completions;
};


static void get_units_report_data(struct units_entry *entries, 
  					struct units_entry *total)
{
  int time_to_build;
  memset(entries, '\0', U_LAST * sizeof(struct units_entry));
  memset(total, '\0', sizeof(struct units_entry));
  for(time_to_build = 0; time_to_build < U_LAST; time_to_build++) {
    entries[time_to_build].soonest_completions = FC_INFINITY;
  }
  unit_list_iterate(game.player_ptr->units, pUnit) {
    (entries[pUnit->type].active_count)++;
    (total->active_count)++;
    if (pUnit->homecity) {
      entries[pUnit->type].upkeep_shield += pUnit->upkeep;
      total->upkeep_shield += pUnit->upkeep;
      entries[pUnit->type].upkeep_food += pUnit->upkeep_food;
      total->upkeep_food += pUnit->upkeep_food;
      entries[pUnit->type].upkeep_gold += pUnit->upkeep_gold;
      total->upkeep_gold += pUnit->upkeep_gold;
    }
  } unit_list_iterate_end;
    
  city_list_iterate(game.player_ptr->cities, pCity) {
    if (pCity->is_building_unit &&
      (unit_type_exists(pCity->currently_building))) {
        (entries[pCity->currently_building].building_count)++;
	(total->building_count)++;
        entries[pCity->currently_building].soonest_completions =
		MIN(entries[pCity->currently_building].soonest_completions,
			city_turns_to_build(pCity,
				pCity->currently_building, TRUE, TRUE));
    }
  } city_list_iterate_end;
}


static int units_dialog_callback(struct GUI *pWindow)
{
  return std_move_window_group_callback(
  			pUnitsDlg->pBeginWidgetList, pWindow);
}

/* --------------------------------------------------------------- */
static int ok_upgrade_unit_window_callback(struct GUI *pWidget)
{
  int ut1 = MAX_ID - pWidget->ID;
  
  /* popdown sell dlg */
  lock_buffer(pWidget->dst);
  popdown_window_group_dialog(pUnits_Upg_Dlg->pBeginWidgetList,
			      pUnits_Upg_Dlg->pEndWidgetList);
  unlock_buffer();
  FREE(pUnits_Upg_Dlg);
   
  dsend_packet_unit_type_upgrade(&aconnection, ut1);
    
  rebuild_focus_anim_frames();

  return -1;
}

static int upgrade_unit_window_callback(struct GUI *pWindow)
{
  return std_move_window_group_callback(
  			pUnits_Upg_Dlg->pBeginWidgetList, pWindow);
}

static int cancel_upgrade_unit_callback(struct GUI *pWidget)
{
  if (pUnits_Upg_Dlg) {
    lock_buffer(pWidget->dst);
    popdown_window_group_dialog(pUnits_Upg_Dlg->pBeginWidgetList,
			      pUnits_Upg_Dlg->pEndWidgetList);
    unlock_buffer();
    FREE(pUnits_Upg_Dlg);
    flush_dirty();
  }
  return -1;
}

static int popup_upgrade_unit_callback(struct GUI *pWidget)
{
  int ut1, ut2;
  int value, hh, ww = 0;
  char cBuf[128];
  struct GUI *pBuf = NULL, *pWindow;
  SDL_String16 *pStr;
  SDL_Surface *pText, *pDest = pWidget->dst;
  SDL_Rect dst;
  
  ut1 = MAX_ID - pWidget->ID;
  
  if (pUnits_Upg_Dlg || !unit_type_exists(ut1)) {
    return 1;
  }
  
  set_wstate(pWidget, FC_WS_NORMAL);
  pSellected_Widget = NULL;
  redraw_label(pWidget);
  sdl_dirty_rect(pWidget->size);
  
  pUnits_Upg_Dlg = MALLOC(sizeof(struct SMALL_DLG));

  ut2 = can_upgrade_unittype(game.player_ptr, ut1);
  value = unit_upgrade_price(game.player_ptr, ut1, ut2);
  
  my_snprintf(cBuf, sizeof(cBuf),
    	_("Upgrade as many %s to %s as possible for %d gold each?\n"
	  "Treasury contains %d gold."),
	unit_types[ut1].name, unit_types[ut2].name,
	value, game.player_ptr->economic.gold);
 
  
  hh = WINDOW_TILE_HIGH + 1;
  pStr = create_str16_from_char(_("Upgrade Obsolete Units"), 12);
  pStr->style |= TTF_STYLE_BOLD;

  pWindow = create_window(pDest, pStr, 100, 100, 0);

  pWindow->action = upgrade_unit_window_callback;
  set_wstate(pWindow, FC_WS_NORMAL);

  pUnits_Upg_Dlg->pEndWidgetList = pWindow;

  add_to_gui_list(ID_WINDOW, pWindow);

  /* ============================================================= */
  
  /* create text label */
  pStr = create_str16_from_char(cBuf, 10);
  pStr->style |= (TTF_STYLE_BOLD|SF_CENTER);
  pStr->fgcol.r = 255;
  pStr->fgcol.g = 255;
  /*pStr->forecol.b = 255; */
  
  pText = create_text_surf_from_str16(pStr);
  FREESTRING16(pStr);
  
  hh += (pText->h + 10);
  ww = MAX(ww , pText->w + 20);
  
  /* cancel button */
  pBuf = create_themeicon_button_from_chars(pTheme->CANCEL_Icon,
			    pWindow->dst, _("No"), 12, 0);

  clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
  pBuf->action = cancel_upgrade_unit_callback;
  set_wstate(pBuf, FC_WS_NORMAL);

  hh += (pBuf->size.h + 20);
  
  add_to_gui_list(ID_BUTTON, pBuf);
  
  if (game.player_ptr->economic.gold >= value) {
    pBuf = create_themeicon_button_from_chars(pTheme->OK_Icon, pWindow->dst,
					      _("Yes"), 12, 0);
        
    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
    pBuf->action = ok_upgrade_unit_window_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
        
    add_to_gui_list(pWidget->ID, pBuf);
    pBuf->size.w = MAX(pBuf->size.w, pBuf->next->size.w);
    pBuf->next->size.w = pBuf->size.w;
    ww = MAX(ww, 30 + pBuf->size.w * 2);
  } else {
    ww = MAX(ww, pBuf->size.w + 20);
  }
  /* ============================================ */
  
  pUnits_Upg_Dlg->pBeginWidgetList = pBuf;
  
  pWindow->size.x = pUnitsDlg->pEndWidgetList->size.x +
  		(pUnitsDlg->pEndWidgetList->size.w - ww) / 2;
  pWindow->size.y = pUnitsDlg->pEndWidgetList->size.y +
		(pUnitsDlg->pEndWidgetList->size.h - hh) / 2;
  
  resize_window(pWindow, NULL,
		get_game_colorRGB(COLOR_STD_BACKGROUND_BROWN),
		ww + DOUBLE_FRAME_WH, hh);
  
  /* setup rest of widgets */
  /* label */
  dst.x = FRAME_WH + (ww - DOUBLE_FRAME_WH - pText->w) / 2;
  dst.y = WINDOW_TILE_HIGH + 11;
  SDL_BlitSurface(pText, NULL, pWindow->theme, &dst);
  FREESURFACE(pText);
   
  /* cancel button */
  pBuf = pWindow->prev;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 10;
  
  if (game.player_ptr->economic.gold >= value) {
    /* sell button */
    pBuf = pBuf->prev;
    pBuf->size.x = pWindow->size.x + (ww - (2 * pBuf->size.w + 10)) / 2;
    pBuf->size.y = pBuf->next->size.y;
    
    /* cancel button */
    pBuf->next->size.x = pBuf->size.x + pBuf->size.w + 10;
  } else {
    /* x position of cancel button */
    pBuf->size.x = pWindow->size.x +
			    pWindow->size.w - FRAME_WH - pBuf->size.w - 10;
  }
  
  
  /* ================================================== */
  /* redraw */
  redraw_group(pUnits_Upg_Dlg->pBeginWidgetList, pWindow, 0);
    
  sdl_dirty_rect(pWindow->size);
  flush_dirty();
  return -1;
}

static int exit_units_dlg_callback(struct GUI *pWidget)
{
  if (pUnitsDlg) {
    if (pUnits_Upg_Dlg) {
       del_group_of_widgets_from_gui_list(pUnits_Upg_Dlg->pBeginWidgetList,
			      pUnits_Upg_Dlg->pEndWidgetList);
       FREE(pUnits_Upg_Dlg); 
    }
    popdown_window_group_dialog(pUnitsDlg->pBeginWidgetList,
				      pUnitsDlg->pEndWidgetList);
    FREE(pUnitsDlg->pScroll);
    FREE(pUnitsDlg);
    flush_dirty();
  }
  return -1;
}


/**************************************************************************
  rebuild the units report.
**************************************************************************/
static void real_activeunits_report_dialog_update(struct units_entry *units, 
  						struct units_entry *total)
{
  struct GUI *pBuf = NULL;
  struct GUI *pWindow , *pLast;
  SDL_String16 *pStr;
  SDL_Surface *pText1, *pText2, *pText3 , *pText4, *pText5, *pLogo;
  int w = 0 , count , h = 0, ww, hh = 0, name_w = 0;
  char cBuf[64];
  SDL_Color color = {255,255,255,128};
  SDL_Color sellect = {255,255,0,255};
  SDL_Rect dst;
  bool upgrade = FALSE;
  struct unit_type *pUnit;
    
  if(pUnitsDlg) {
    popdown_window_group_dialog(pUnitsDlg->pBeginWidgetList,
			      			pUnitsDlg->pEndWidgetList);
  } else {
    pUnitsDlg = MALLOC(sizeof(struct ADVANCED_DLG));  
  }
  
  my_snprintf(cBuf, sizeof(cBuf), _("active"));
  pStr = create_str16_from_char(cBuf, 10);
  pStr->style |= SF_CENTER;
  pText1 = create_text_surf_from_str16(pStr);
    
  my_snprintf(cBuf, sizeof(cBuf), _("under\nconstruction"));
  copy_chars_to_string16(pStr, cBuf);
  pText2 = create_text_surf_from_str16(pStr);
    
  my_snprintf(cBuf, sizeof(cBuf), _("soonest\ncompletion"));
  copy_chars_to_string16(pStr, cBuf);
  pText5 = create_text_surf_from_str16(pStr);
    
  my_snprintf(cBuf, sizeof(cBuf), _("Total"));
  copy_chars_to_string16(pStr, cBuf);
  pText3 = create_text_surf_from_str16(pStr);
    
  my_snprintf(cBuf, sizeof(cBuf), _("Units"));
  copy_chars_to_string16(pStr, cBuf);
  pText4 = create_text_surf_from_str16(pStr);
  name_w = pText4->w;
  FREESTRING16(pStr);
  
  /* --------------- */
  pStr = create_str16_from_char(_("Units Report"), 12);
  pStr->style |= TTF_STYLE_BOLD;

  pWindow = create_window(NULL, pStr, 40, 30, 0);
  pUnitsDlg->pEndWidgetList = pWindow;
  w = MAX(w, pWindow->size.w);
  set_wstate(pWindow, FC_WS_NORMAL);
  pWindow->action = units_dialog_callback;
  
  add_to_gui_list(ID_UNITS_DIALOG_WINDOW, pWindow);
  
  /* ------------------------- */
  /* exit button */
  pBuf = create_themeicon(pTheme->Small_CANCEL_Icon, pWindow->dst,
  			  			WF_DRAW_THEME_TRANSPARENT);
  pBuf->action = exit_units_dlg_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  
  add_to_gui_list(ID_BUTTON, pBuf);
  /* ------------------------- */
  /* totals */
  my_snprintf(cBuf, sizeof(cBuf), "%d", total->active_count);
	
  pStr = create_str16_from_char(cBuf, 10);
  pStr->style |= (TTF_STYLE_BOLD|SF_CENTER);
	
  pBuf = create_iconlabel(NULL, pWindow->dst, pStr,
					WF_DRAW_THEME_TRANSPARENT);
 	
  h += pBuf->size.h;
  pBuf->size.w = pText1->w + 6;
  add_to_gui_list(ID_LABEL, pBuf);
  /* ---------------------------------------------- */
  my_snprintf(cBuf, sizeof(cBuf), "%d", total->upkeep_shield);
	
  pStr = create_str16_from_char(cBuf, 10);
  pStr->style |= (TTF_STYLE_BOLD|SF_CENTER);
	
  pBuf = create_iconlabel(NULL, pWindow->dst, pStr, WF_DRAW_THEME_TRANSPARENT);
	
  pBuf->size.w = pText1->w;
  add_to_gui_list(ID_LABEL, pBuf);
  /* ---------------------------------------------- */	
  my_snprintf(cBuf, sizeof(cBuf), "%d", total->upkeep_food);
	
  pStr = create_str16_from_char(cBuf, 10);
  pStr->style |= (TTF_STYLE_BOLD|SF_CENTER);
	
  pBuf = create_iconlabel(NULL, pWindow->dst, pStr, WF_DRAW_THEME_TRANSPARENT);
	
  pBuf->size.w = pText1->w;
  add_to_gui_list(ID_LABEL, pBuf);
  /* ---------------------------------------------- */	
  my_snprintf(cBuf, sizeof(cBuf), "%d", total->upkeep_gold);
	
  pStr = create_str16_from_char(cBuf, 10);
  pStr->style |= (TTF_STYLE_BOLD|SF_CENTER);
	
  pBuf = create_iconlabel(NULL, pWindow->dst, pStr, WF_DRAW_THEME_TRANSPARENT);
	
  pBuf->size.w = pText1->w;
  add_to_gui_list(ID_LABEL, pBuf);
  /* ---------------------------------------------- */	
  my_snprintf(cBuf, sizeof(cBuf), "%d", total->building_count);
	
  pStr = create_str16_from_char(cBuf, 10);
  pStr->style |= (TTF_STYLE_BOLD|SF_CENTER);

  pBuf = create_iconlabel(NULL, pWindow->dst, pStr,
					WF_DRAW_THEME_TRANSPARENT);
	
  pBuf->size.w = pText2->w + 6;
  add_to_gui_list(ID_LABEL, pBuf);
  
  /* ------------------------- */
  pLast = pBuf;
  count = 0; 
  unit_type_iterate(i) {
    if ((units[i].active_count > 0) || (units[i].building_count > 0)) {
      upgrade = (can_upgrade_unittype(game.player_ptr, i) != -1);
      pUnit = get_unit_type(i);
	
      /* ----------- */
      pBuf = create_iconlabel(GET_SURF(pUnit->sprite), pWindow->dst, NULL,
			WF_DRAW_THEME_TRANSPARENT);
      if(count > 72) {
	set_wflag(pBuf, WF_HIDDEN);
      }
      hh = pBuf->size.h;
      add_to_gui_list(MAX_ID - i, pBuf);
      
      /* ----------- */
      pStr = create_str16_from_char(pUnit->name, 12);
      pStr->style |= (TTF_STYLE_BOLD|SF_CENTER);
      pBuf = create_iconlabel(NULL, pWindow->dst, pStr,
			(WF_DRAW_THEME_TRANSPARENT|WF_SELLECT_WITHOUT_BAR));
      if(upgrade) {
	pBuf->string16->fgcol = sellect;
	pBuf->action = popup_upgrade_unit_callback;
	set_wstate(pBuf, FC_WS_NORMAL);
      } else {
        pBuf->string16->fgcol = color;
      }
      pBuf->string16->style &= ~SF_CENTER;
      if(count > 72) {
	set_wflag(pBuf , WF_HIDDEN);
      }
      hh = MAX(hh, pBuf->size.h);
      name_w = MAX(pBuf->size.w, name_w);
      add_to_gui_list(MAX_ID - i, pBuf);
      
      /* ----------- */	
      my_snprintf(cBuf, sizeof(cBuf), "%d", units[i].active_count);
      pStr = create_str16_from_char(cBuf, 10);
      pStr->style |= SF_CENTER;
      pBuf = create_iconlabel(NULL, pWindow->dst, pStr,
					WF_DRAW_THEME_TRANSPARENT);
      if(count > 72) {
	set_wflag(pBuf, WF_HIDDEN);
      }
      hh = MAX(hh, pBuf->size.h);
      pBuf->size.w = pText1->w + 6;
      add_to_gui_list(MAX_ID - i, pBuf);
      
      /* ----------- */	
      my_snprintf(cBuf, sizeof(cBuf), "%d", units[i].upkeep_shield);
      pStr = create_str16_from_char(cBuf, 10);
      pStr->style |= SF_CENTER;
      pBuf = create_iconlabel(NULL, pWindow->dst, pStr,
      						WF_DRAW_THEME_TRANSPARENT);
      if(count > 72) {
	set_wflag(pBuf, WF_HIDDEN);
      }
      hh = MAX(hh, pBuf->size.h);
      pBuf->size.w = pText1->w;
      add_to_gui_list(MAX_ID - i, pBuf);
	
      /* ----------- */
      my_snprintf(cBuf, sizeof(cBuf), "%d", units[i].upkeep_food);
      pStr = create_str16_from_char(cBuf, 10);
      pStr->style |= SF_CENTER;
      pBuf = create_iconlabel(NULL, pWindow->dst, pStr,
						WF_DRAW_THEME_TRANSPARENT);
      if(count > 72) {
	set_wflag(pBuf, WF_HIDDEN);
      }
	
      hh = MAX(hh, pBuf->size.h);
      pBuf->size.w = pText1->w;
      add_to_gui_list(MAX_ID - i, pBuf);

      /* ----------- */
      my_snprintf(cBuf, sizeof(cBuf), "%d", units[i].upkeep_gold);
      pStr = create_str16_from_char(cBuf, 10);
      pStr->style |= SF_CENTER;
      pBuf = create_iconlabel(NULL, pWindow->dst, pStr,
						WF_DRAW_THEME_TRANSPARENT);
      if(count > 72) {
	set_wflag(pBuf, WF_HIDDEN);
      }
	
      hh = MAX(hh, pBuf->size.h);
      pBuf->size.w = pText1->w;
      add_to_gui_list(MAX_ID - i, pBuf);      
      /* ----------- */
      if(units[i].building_count > 0) {
	my_snprintf(cBuf, sizeof(cBuf), "%d", units[i].building_count);
      } else {
	my_snprintf(cBuf, sizeof(cBuf), "--");
      }
      pStr = create_str16_from_char(cBuf, 10);
      pStr->style |= SF_CENTER;
      pBuf = create_iconlabel(NULL, pWindow->dst, pStr,
					WF_DRAW_THEME_TRANSPARENT);
      if(count > 72) {
	set_wflag(pBuf, WF_HIDDEN);
      }
      hh = MAX(hh, pBuf->size.h);
      pBuf->size.w = pText2->w + 6;
      add_to_gui_list(MAX_ID - i, pBuf);
      
      /* ----------- */
      if(units[i].building_count > 0) {
	my_snprintf(cBuf, sizeof(cBuf), "%d %s", units[i].soonest_completions,
			PL_("turn", "turns", units[i].soonest_completions));
      } else {
	my_snprintf(cBuf, sizeof(cBuf), "--");
      }
	
      pStr = create_str16_from_char(cBuf, 10);
      pStr->style |= SF_CENTER;
      pBuf = create_iconlabel(NULL, pWindow->dst, pStr,
					WF_DRAW_THEME_TRANSPARENT);
	
      if(count > 72) {
	set_wflag(pBuf, WF_HIDDEN);
      }
      hh = MAX(hh, pBuf->size.h);
      pBuf->size.w = pText5->w + 6;
      add_to_gui_list(MAX_ID - i, pBuf);

      
      count += 8;
      h += (hh/2);
    }
  } unit_type_iterate_end;
    
  pUnitsDlg->pBeginWidgetList = pBuf;
  w = (UNIT_TILE_WIDTH * 2 + name_w + 15) +
		(4 * pText1->w + 46) + (pText2->w + 16) + (pText5->w + 6) + 2;
  if(count) {
    pUnitsDlg->pBeginActiveWidgetList = pBuf;
    pUnitsDlg->pEndActiveWidgetList = pLast->prev;
    if(count > 80) {
      pUnitsDlg->pActiveWidgetList = pUnitsDlg->pEndActiveWidgetList;
      if(pUnitsDlg->pScroll) {
	pUnitsDlg->pScroll->count = count;
      }
      ww = create_vertical_scrollbar(pUnitsDlg, 8, 10, TRUE, TRUE);
      w += ww;
      h = (hh + 9 * (hh/2) + 10) + WINDOW_TILE_HIGH + 1 + FRAME_WH;
    } else {
      h += WINDOW_TILE_HIGH + 1 + FRAME_WH + hh/2;
    }
  } else {
    h = WINDOW_TILE_HIGH + 1 + FRAME_WH + 50;
  }
  
  h += pText1->h + 10;
  w += DOUBLE_FRAME_WH + 2;
  pWindow->size.x = (Main.screen->w - w) / 2;
  pWindow->size.y = (Main.screen->h - h) / 2;
    
  pLogo = get_logo_gfx();
  resize_window(pWindow, pLogo,	NULL, w, h);
  FREESURFACE(pLogo);

  pLogo = SDL_DisplayFormat(pWindow->theme);
  FREESURFACE(pWindow->theme);
  pWindow->theme = pLogo;
  pLogo = NULL;
  
  ww -= DOUBLE_FRAME_WH;
  color.unused = 136;
  
  /* exit button */
  pBuf = pWindow->prev;
  pBuf->size.x = pWindow->size.x + pWindow->size.w - pBuf->size.w - FRAME_WH - 1;
  pBuf->size.y = pWindow->size.y + 1;
  
  /* totals background and label */
  dst.x = FRAME_WH + 2;
  dst.y = h - ( pText3->h + 2 ) - 2 - FRAME_WH;
  dst.w = name_w + UNIT_TILE_WIDTH * 2 + 5;
  dst.h = pText3->h + 2;
  SDL_FillRectAlpha(pWindow->theme, &dst, &color);
  
  putframe(pWindow->theme, dst.x , dst.y,
			  dst.x + dst.w, dst.y + dst.h - 1, 0xFF000000);
  
  dst.y += 1;
  dst.x += ((name_w + UNIT_TILE_WIDTH * 2 + 5) - pText3->w) / 2;
  SDL_BlitSurface(pText3, NULL, pWindow->theme, &dst);
  FREESURFACE(pText3);
  
  /* total active widget */
  pBuf = pBuf->prev;
  pBuf->size.x = pWindow->size.x + FRAME_WH + name_w +
			  UNIT_TILE_WIDTH * 2 + 17;
  pBuf->size.y = pWindow->size.y + dst.y;
  
  /* total shields cost widget */
  pBuf = pBuf->prev;
  pBuf->size.x = pBuf->next->size.x + pBuf->next->size.w + 10;
  pBuf->size.y = pWindow->size.y + dst.y;
  
  /* total food cost widget */
  pBuf = pBuf->prev;
  pBuf->size.x = pBuf->next->size.x + pBuf->next->size.w + 10;
  pBuf->size.y = pWindow->size.y + dst.y;
  
  /* total gold cost widget */
  pBuf = pBuf->prev;
  pBuf->size.x = pBuf->next->size.x + pBuf->next->size.w + 10;
  pBuf->size.y = pWindow->size.y + dst.y;
  
  /* total building count widget */
  pBuf = pBuf->prev;
  pBuf->size.x = pBuf->next->size.x + pBuf->next->size.w + 10;
  pBuf->size.y = pWindow->size.y + dst.y;
  
  /* units background and labels */
  dst.x = FRAME_WH + 2;
  dst.y = WINDOW_TILE_HIGH + 2;
  dst.w = name_w + UNIT_TILE_WIDTH * 2 + 5;
  dst.h = pText4->h + 2;
  SDL_FillRectAlpha(pWindow->theme, &dst, &color);
  
  putframe(pWindow->theme, dst.x , dst.y,
			  dst.x + dst.w, dst.y + dst.h - 1, 0xFF000000);
  
  dst.y += 1;
  dst.x += ((name_w + UNIT_TILE_WIDTH * 2 + 5)- pText4->w) / 2;
  SDL_BlitSurface(pText4, NULL, pWindow->theme, &dst);
  FREESURFACE(pText4);
  
  /* active count background and label */  
  dst.x = FRAME_WH + 2 + name_w + UNIT_TILE_WIDTH * 2 + 15;
  dst.y = WINDOW_TILE_HIGH + 2;
  dst.w = pText1->w + 6;
  dst.h = h - WINDOW_TILE_HIGH - 2 - FRAME_WH - 2;
  SDL_FillRectAlpha(pWindow->theme, &dst, &color);
    
  putframe(pWindow->theme, dst.x , dst.y,
			  dst.x + dst.w, dst.y + dst.h - 1, 0xFF000000);
    
  dst.x += 3;
  SDL_BlitSurface(pText1, NULL, pWindow->theme, &dst);
  ww = pText1->w;
  hh = pText1->h;
  FREESURFACE(pText1);
  
  /* shields cost background and label */
  dst.x += (ww + 13);
  w = dst.x;
  dst.w = ww;
  dst.h = h - WINDOW_TILE_HIGH - 2 - FRAME_WH - 2;
  SDL_FillRectAlpha(pWindow->theme, &dst, &color);
  
  putframe(pWindow->theme, dst.x , dst.y,
			  dst.x + dst.w, dst.y + dst.h - 1, 0xFF000000);
  
  dst.y = WINDOW_TILE_HIGH + 4;
  dst.x += ((ww - pIcons->pBIG_Shield->w) / 2);
  SDL_BlitSurface(pIcons->pBIG_Shield, NULL, pWindow->theme, &dst);
  
  /* food cost background and label */
  dst.x = w + ww + 10;
  w = dst.x;
  dst.y = WINDOW_TILE_HIGH + 2;
  dst.w = ww;
  dst.h = h - WINDOW_TILE_HIGH - 2 - FRAME_WH - 2;
  SDL_FillRectAlpha(pWindow->theme, &dst, &color);
  
  putframe(pWindow->theme, dst.x , dst.y,
			  dst.x + dst.w, dst.y + dst.h - 1, 0xFF000000);
  
  dst.y = WINDOW_TILE_HIGH + 4;
  dst.x += ((ww - pIcons->pBIG_Food->w) / 2);
  SDL_BlitSurface(pIcons->pBIG_Food, NULL, pWindow->theme, &dst);
  
  /* gold cost background and label */
  dst.x = w + ww + 10;
  w = dst.x;
  dst.y = WINDOW_TILE_HIGH + 2;
  dst.w = ww;
  dst.h = h - WINDOW_TILE_HIGH - 2 - FRAME_WH - 2;
  SDL_FillRectAlpha(pWindow->theme, &dst, &color);
  
  putframe(pWindow->theme, dst.x , dst.y,
			  dst.x + dst.w, dst.y + dst.h - 1, 0xFF000000);
  
  dst.y = WINDOW_TILE_HIGH + 4;
  dst.x += ((ww - pIcons->pBIG_Coin->w) / 2);
  SDL_BlitSurface(pIcons->pBIG_Coin, NULL, pWindow->theme, &dst);
  
  /* building count background and label */
  dst.x = w + ww + 10;
  dst.y = WINDOW_TILE_HIGH + 2;
  dst.w = pText2->w + 6;
  ww = pText2->w + 6;
  w = dst.x;
  dst.h = h - WINDOW_TILE_HIGH - 2 - FRAME_WH - 2;
  SDL_FillRectAlpha(pWindow->theme, &dst, &color);
  
  putframe(pWindow->theme, dst.x , dst.y,
			  dst.x + dst.w, dst.y + dst.h - 1, 0xFF000000);
			  
  dst.x += 3;
  SDL_BlitSurface(pText2, NULL, pWindow->theme, &dst);
  FREESURFACE(pText2);
   
  /* building count background and label */
  dst.x = w + ww + 10;
  dst.y = WINDOW_TILE_HIGH + 2;
  dst.w = pText5->w + 6;
  dst.h = h - WINDOW_TILE_HIGH - 2 - FRAME_WH - 2;
  SDL_FillRectAlpha(pWindow->theme, &dst, &color);
  
  putframe(pWindow->theme, dst.x , dst.y,
			  dst.x + dst.w, dst.y + dst.h - 1, 0xFF000000);
			  
  dst.x += 3;
  SDL_BlitSurface(pText5, NULL, pWindow->theme, &dst);
  FREESURFACE(pText5);
  
  if(count) {
    int start_x = pWindow->size.x + FRAME_WH + 2;
    int start_y = pWindow->size.y + WINDOW_TILE_HIGH + 2 + hh + 2;
    int mod = 0;
    
    pBuf = pBuf->prev;
    while(TRUE)
    {
      pBuf->size.x = start_x + (mod ? UNIT_TILE_WIDTH : 0);
      pBuf->size.y = start_y;
      hh = pBuf->size.h;
      mod ^= 1;
      
      pBuf = pBuf->prev;
      pBuf->size.w = name_w;
      pBuf->size.x = start_x + UNIT_TILE_WIDTH * 2 + 5;
      pBuf->size.y = start_y + (hh - pBuf->size.h) / 2;
      
      pBuf = pBuf->prev;
      pBuf->size.x = pBuf->next->size.x + pBuf->next->size.w + 10;
      pBuf->size.y = start_y + (hh - pBuf->size.h) / 2;
      
      pBuf = pBuf->prev;
      pBuf->size.x = pBuf->next->size.x + pBuf->next->size.w + 10;
      pBuf->size.y = start_y + (hh - pBuf->size.h) / 2;
      
      pBuf = pBuf->prev;
      pBuf->size.x = pBuf->next->size.x + pBuf->next->size.w + 10;
      pBuf->size.y = start_y + (hh - pBuf->size.h) / 2;
      
      pBuf = pBuf->prev;
      pBuf->size.x = pBuf->next->size.x + pBuf->next->size.w + 10;
      pBuf->size.y = start_y + (hh - pBuf->size.h) / 2;
      
      pBuf = pBuf->prev;
      pBuf->size.x = pBuf->next->size.x + pBuf->next->size.w + 10;
      pBuf->size.y = start_y + (hh - pBuf->size.h) / 2;
      
      pBuf = pBuf->prev;
      pBuf->size.x = pBuf->next->size.x + pBuf->next->size.w + 10;
      pBuf->size.y = start_y + (hh - pBuf->size.h) / 2;

      start_y += (hh>>1);
      if(pBuf == pUnitsDlg->pBeginActiveWidgetList) {
	break;
      }
      pBuf = pBuf->prev;
    }
    
    if (pUnitsDlg->pScroll)
    {
      setup_vertical_scrollbar_area(pUnitsDlg->pScroll,
	  pWindow->size.x + pWindow->size.w - FRAME_WH,
    	  pWindow->size.y + WINDOW_TILE_HIGH + 1,
    	  pWindow->size.h - (WINDOW_TILE_HIGH + 1 + FRAME_WH + 1), TRUE);      
    }
    
  }
  /* ----------------------------------- */
  redraw_group(pUnitsDlg->pBeginWidgetList, pWindow, 0);
  sdl_dirty_rect(pWindow->size);
    
  flush_dirty();  
}

/**************************************************************************
  update the units report.
**************************************************************************/
void activeunits_report_dialog_update(void)
{
  if(pUnitsDlg && !is_report_dialogs_frozen()) {
    struct units_entry units[U_LAST];
    struct units_entry units_total;
    struct GUI *pWidget, *pBuf;
    bool is_in_list = FALSE;
    char cBuf[32];
    SDL_Color sellect = {255,255,0,255};
    bool upgrade;  
    
    get_units_report_data(units, &units_total);

    /* find if there are new units entry (if not then rebuild all) */
    pWidget = pUnitsDlg->pEndActiveWidgetList;/* icon of first unit */
    unit_type_iterate(i) {
      if ((units[i].active_count > 0) || (units[i].building_count > 0)) {
        is_in_list = FALSE;
        pBuf = pWidget;
        while(pBuf) {
	  if(i == MAX_ID - pBuf->ID) {
	    is_in_list = TRUE;
	    pWidget = pBuf;
	    break;
	  }
	  if(pBuf->prev->prev->prev->prev->prev->prev ==
			  pUnitsDlg->pBeginActiveWidgetList) {
	    break;
	  }
	  pBuf = pBuf->prev->prev->prev->prev->prev->prev->prev;/* add 7 widgets */
        }
        if(!is_in_list) {
	  real_activeunits_report_dialog_update(units, &units_total);
          return;
        }
      }
    } unit_type_iterate_end;
  
  
    /* update list */
    pWidget = pUnitsDlg->pEndActiveWidgetList;
    unit_type_iterate(i) {
      pBuf = pWidget;
      if ((units[i].active_count > 0) || (units[i].building_count > 0)) {
        if (i == MAX_ID - pBuf->ID) {
UPD:	  upgrade = (can_upgrade_unittype(game.player_ptr, i) != -1);
	  pBuf = pBuf->prev;
	  if(upgrade) {
	    pBuf->string16->fgcol = sellect;
	    pBuf->action = popup_upgrade_unit_callback;
	    set_wstate(pBuf, FC_WS_NORMAL);
          }
	
	  my_snprintf(cBuf, sizeof(cBuf), "%d", units[i].active_count);
	  pBuf = pBuf->prev;
	  copy_chars_to_string16(pBuf->string16, cBuf);
	  	
          my_snprintf(cBuf, sizeof(cBuf), "%d", units[i].upkeep_shield);
	  pBuf = pBuf->prev;
	  copy_chars_to_string16(pBuf->string16, cBuf);
	
          my_snprintf(cBuf, sizeof(cBuf), "%d", units[i].upkeep_food);
	  pBuf = pBuf->prev;
	  copy_chars_to_string16(pBuf->string16, cBuf);
	
	  if(units[i].building_count > 0) {
	    my_snprintf(cBuf, sizeof(cBuf), "%d", units[i].building_count);
          } else {
	    my_snprintf(cBuf, sizeof(cBuf), "--");
          }
	  pBuf = pBuf->prev;
	  copy_chars_to_string16(pBuf->string16, cBuf);
	
          if(units[i].building_count > 0) {
	    my_snprintf(cBuf, sizeof(cBuf), "%d %s", units[i].soonest_completions,
			PL_("turn", "turns", units[i].soonest_completions));
          } else {
	    my_snprintf(cBuf, sizeof(cBuf), "--");
          }
	  pBuf = pBuf->prev;
	  copy_chars_to_string16(pBuf->string16, cBuf);
	
	  pWidget = pBuf->prev;
          } else {
            pBuf = pWidget->next;
            do {
	      del_widget_from_vertical_scroll_widget_list(pUnitsDlg, pBuf->prev);
	    } while(i != MAX_ID - pBuf->prev->ID &&
			pBuf->prev != pUnitsDlg->pBeginActiveWidgetList);
	    if(pBuf->prev == pUnitsDlg->pBeginActiveWidgetList) {
	      del_widget_from_vertical_scroll_widget_list(pUnitsDlg, pBuf->prev);
	      pWidget = pBuf->prev;
	    } else {
	      pBuf = pBuf->prev;
	      goto UPD;
	    }
          }
        } else {
          if(pBuf && pBuf->next != pUnitsDlg->pBeginActiveWidgetList) {
            if (i < MAX_ID - pBuf->ID) {
	      continue;
            } else {
              pBuf = pBuf->next;
              do {
	        del_widget_from_vertical_scroll_widget_list(pUnitsDlg,
							pBuf->prev);
              } while(i == MAX_ID - pBuf->prev->ID &&
			pBuf->prev != pUnitsDlg->pBeginActiveWidgetList);
              if(pBuf->prev == pUnitsDlg->pBeginActiveWidgetList) {
	        del_widget_from_vertical_scroll_widget_list(pUnitsDlg,
							pBuf->prev);
              }
	      pWidget = pBuf->prev;
            }
          }
        }
      } unit_type_iterate_end;
      /* -------------------------------------- */
      /* total active */
      pBuf = pUnitsDlg->pEndWidgetList->prev->prev;
    my_snprintf(cBuf, sizeof(cBuf), "%d", units_total.active_count);
    copy_chars_to_string16(pBuf->string16, cBuf);
  
    /* total shields cost */
    pBuf = pBuf->prev;
    my_snprintf(cBuf, sizeof(cBuf), "%d", units_total.upkeep_shield);
    copy_chars_to_string16(pBuf->string16, cBuf);
  
    /* total food cost widget */
    pBuf = pBuf->prev;
    my_snprintf(cBuf, sizeof(cBuf), "%d", units_total.upkeep_food);
    copy_chars_to_string16(pBuf->string16, cBuf);
  
    /* total building count */
    pBuf = pBuf->prev;
    my_snprintf(cBuf, sizeof(cBuf), "%d", units_total.building_count);
    copy_chars_to_string16(pBuf->string16, cBuf);

    /* -------------------------------------- */
    redraw_group(pUnitsDlg->pBeginWidgetList, pUnitsDlg->pEndWidgetList, 0);
    sdl_dirty_rect(pUnitsDlg->pEndWidgetList->size);
    
    flush_dirty();
  }
}

/**************************************************************************
  Popup (or raise) the units report (F2).  It may or may not be modal.
**************************************************************************/
void popup_activeunits_report_dialog(bool make_modal)
{
  struct units_entry units[U_LAST];
  struct units_entry units_total;
    
  if(pUnitsDlg) {
    return;
  }

  get_units_report_data(units, &units_total);
  real_activeunits_report_dialog_update(units, &units_total);
}

/**************************************************************************
  Popdown the units report.
**************************************************************************/
void popdown_activeunits_report_dialog(void)
{
  if (pUnitsDlg) {
    if (pUnits_Upg_Dlg) {
       del_group_of_widgets_from_gui_list(pUnits_Upg_Dlg->pBeginWidgetList,
			      pUnits_Upg_Dlg->pEndWidgetList);
       FREE(pUnits_Upg_Dlg); 
    }
    popdown_window_group_dialog(pUnitsDlg->pBeginWidgetList,
				      pUnitsDlg->pEndWidgetList);
    FREE(pUnitsDlg->pScroll);
    FREE(pUnitsDlg);
  }
}
/* ===================================================================== */
/* ======================== Economy Report ============================= */
/* ===================================================================== */
static struct ADVANCED_DLG *pEconomyDlg = NULL;
static struct SMALL_DLG *pEconomy_Sell_Dlg = NULL;
  
struct rates_move {
  int min, max, tax, x, gov_max;
  int *src_rate, *dst_rate;
  struct GUI *pHoriz_Src, *pHoriz_Dst;
  struct GUI *pLabel_Src, *pLabel_Dst;
};

static int economy_dialog_callback(struct GUI *pWindow)
{
  return std_move_window_group_callback(pEconomyDlg->pBeginWidgetList, pWindow);
}

static int exit_economy_dialog_callback(struct GUI *pWidget)
{
  if(pEconomyDlg) {
    if (pEconomy_Sell_Dlg) {
       del_group_of_widgets_from_gui_list(pEconomy_Sell_Dlg->pBeginWidgetList,
			      pEconomy_Sell_Dlg->pEndWidgetList);
       FREE(pEconomy_Sell_Dlg); 
    }
    popdown_window_group_dialog(pEconomyDlg->pBeginWidgetList,
					    pEconomyDlg->pEndWidgetList);
    FREE(pEconomyDlg->pScroll);
    FREE(pEconomyDlg);
    set_wstate(get_tax_rates_widget(), FC_WS_NORMAL);
    redraw_icon2(get_tax_rates_widget());
    sdl_dirty_rect(get_tax_rates_widget()->size);
    flush_dirty();
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int toggle_block_callback(struct GUI *pCheckBox)
{
  switch (pCheckBox->ID) {
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
static Uint16 report_scroll_mouse_button_up(SDL_MouseButtonEvent *pButtonEvent,
						void *pData)
{
  return (Uint16)ID_SCROLLBAR;
}

/**************************************************************************
  ...
**************************************************************************/
static Uint16 report_scroll_mouse_motion_handler(
			SDL_MouseMotionEvent *pMotionEvent, void *pData)
{
  struct rates_move *pMotion = (struct rates_move *)pData;
  struct GUI *pTax_Label = pEconomyDlg->pEndWidgetList->prev->prev;
  struct GUI *pBuf = NULL;
  char cBuf[8];
  int dir, inc, x, *buf_rate = NULL;
  
  if ((abs(pMotionEvent->x - pMotion->x) > 7) &&
     (pMotionEvent->x >= pMotion->min) && (pMotionEvent->x <= pMotion->max)) {
	
	/* set up directions */
    if (pMotionEvent->xrel > 0) {
      dir = 15;
      inc = 10;
    } else {
      dir = -15;
      inc = -10;
    }
	
    /* make checks */
    x = pMotion->pHoriz_Src->size.x;
    if (((x + dir) <= pMotion->max) && ((x + dir) >= pMotion->min)) {
      /* src in range */  
      if(pMotion->pHoriz_Dst) {
        x = pMotion->pHoriz_Dst->size.x;
	if (((x + (-1 * dir)) > pMotion->max) || ((x + (-1 * dir)) < pMotion->min)) {
	  /* dst out of range */
	  if(pMotion->tax + (-1 * inc) <= pMotion->gov_max &&
	    pMotion->tax + (-1 * inc) >= 0) {
	    /* tax in range */
	    pBuf = pMotion->pHoriz_Dst;
	    pMotion->pHoriz_Dst = NULL;
	    buf_rate = pMotion->dst_rate;
	    pMotion->dst_rate = &pMotion->tax;
	    pMotion->pLabel_Dst = pTax_Label;
	  } else {
	    pMotion->x = pMotion->pHoriz_Src->size.x;
	    return ID_ERROR;
	  }		  
	}
      } else {
        if(pMotion->tax + (-1 * inc) > pMotion->gov_max ||
	  pMotion->tax + (-1 * inc) < 0) {
	  pMotion->x = pMotion->pHoriz_Src->size.x;
	  return ID_ERROR;
	}
      }

      /* undraw scrollbars */    
      blit_entire_src(pMotion->pHoriz_Src->gfx, pMotion->pHoriz_Src->dst,
		pMotion->pHoriz_Src->size.x, pMotion->pHoriz_Src->size.y);
      sdl_dirty_rect(pMotion->pHoriz_Src->size);
	    
      if(pMotion->pHoriz_Dst) {
        blit_entire_src(pMotion->pHoriz_Dst->gfx, pMotion->pHoriz_Dst->dst,
		pMotion->pHoriz_Dst->size.x, pMotion->pHoriz_Dst->size.y);
        sdl_dirty_rect(pMotion->pHoriz_Dst->size);
      }
	  
      pMotion->pHoriz_Src->size.x += dir;
      if(pMotion->pHoriz_Dst) {
        pMotion->pHoriz_Dst->size.x -= dir;
      }

      *pMotion->src_rate += inc;
      *pMotion->dst_rate -= inc;
	  	  
      my_snprintf(cBuf, sizeof(cBuf), "%d%%", *pMotion->src_rate);
      copy_chars_to_string16(pMotion->pLabel_Src->string16, cBuf);
      my_snprintf(cBuf, sizeof(cBuf), "%d%%", *pMotion->dst_rate);
      copy_chars_to_string16(pMotion->pLabel_Dst->string16, cBuf);
      		      
      /* redraw label */
      redraw_label(pMotion->pLabel_Src);
      sdl_dirty_rect(pMotion->pLabel_Src->size);

      redraw_label(pMotion->pLabel_Dst);
      sdl_dirty_rect(pMotion->pLabel_Dst->size);

      /* redraw scroolbar */
      refresh_widget_background(pMotion->pHoriz_Src);
      redraw_horiz(pMotion->pHoriz_Src);
      sdl_dirty_rect(pMotion->pHoriz_Src->size);
	  
      if(pMotion->pHoriz_Dst) {
        refresh_widget_background(pMotion->pHoriz_Dst);
        redraw_horiz(pMotion->pHoriz_Dst);
        sdl_dirty_rect(pMotion->pHoriz_Dst->size);
      }

      flush_dirty();

      if (pBuf) {
        pMotion->pHoriz_Dst = pBuf;
        pMotion->pLabel_Dst = pMotion->pHoriz_Dst->prev;
        pMotion->dst_rate = buf_rate;
        pBuf = NULL;
      }

      pMotion->x = pMotion->pHoriz_Src->size.x;
    }
  }				/* if */
  
  return ID_ERROR;
}


/**************************************************************************
  ...
**************************************************************************/
static int horiz_taxrate_callback(struct GUI *pHoriz_Src)
{
  struct rates_move pMotion;
  
  pMotion.pHoriz_Src = pHoriz_Src;
  pMotion.pLabel_Src = pHoriz_Src->prev;
  
  switch (pHoriz_Src->ID) {
    case ID_CHANGE_TAXRATE_DLG_LUX_SCROLLBAR:
      if (SDL_Client_Flags & CF_CHANGE_TAXRATE_LUX_BLOCK) {
        goto END;
      }
      pMotion.src_rate = (int *)pHoriz_Src->data.ptr;
      pMotion.pHoriz_Dst = pHoriz_Src->prev->prev->prev;	/* sci */
      pMotion.dst_rate = (int *)pMotion.pHoriz_Dst->data.ptr;
      pMotion.tax = 100 - *pMotion.src_rate - *pMotion.dst_rate;
      if ((SDL_Client_Flags & CF_CHANGE_TAXRATE_SCI_BLOCK)) {
        if (pMotion.tax <= get_gov_pplayer(game.player_ptr)->max_rate) {
	  pMotion.pHoriz_Dst = NULL;	/* tax */
	  pMotion.dst_rate = &pMotion.tax;
        } else {
	  goto END;	/* all blocked */
        }
      }

    break;

    case ID_CHANGE_TAXRATE_DLG_SCI_SCROLLBAR:
      if ((SDL_Client_Flags & CF_CHANGE_TAXRATE_SCI_BLOCK)) {
        goto END;
      }
      pMotion.src_rate = (int *)pHoriz_Src->data.ptr;
      pMotion.pHoriz_Dst = pHoriz_Src->next->next->next;	/* lux */
      pMotion.dst_rate = (int *)pMotion.pHoriz_Dst->data.ptr;
      pMotion.tax = 100 - *pMotion.src_rate - *pMotion.dst_rate;
      if (SDL_Client_Flags & CF_CHANGE_TAXRATE_LUX_BLOCK) {
        if (pMotion.tax <= get_gov_pplayer(game.player_ptr)->max_rate) {
	  /* tax */
	  pMotion.pHoriz_Dst = NULL;
	  pMotion.dst_rate = &pMotion.tax;
        } else {
	  goto END;	/* all blocked */
        }
      }

    break;

    default:
      return -1;
  }

  if(pMotion.pHoriz_Dst) {
    pMotion.pLabel_Dst = pMotion.pHoriz_Dst->prev;
  } else {
    /* tax label */
    pMotion.pLabel_Dst = pEconomyDlg->pEndWidgetList->prev->prev;
  }

  pMotion.min = pHoriz_Src->next->size.x + pHoriz_Src->next->size.w + 2;
  pMotion.gov_max = get_gov_pplayer(game.player_ptr)->max_rate;
  pMotion.max = pMotion.min + pMotion.gov_max * 1.5;
  pMotion.x = pHoriz_Src->size.x;
  
  MOVE_STEP_Y = 0;
  /* Filter mouse motion events */
  SDL_SetEventFilter(FilterMouseMotionEvents);
  gui_event_loop((void *)(&pMotion), NULL, NULL, NULL, NULL,
		  report_scroll_mouse_button_up,
  			report_scroll_mouse_motion_handler);
  /* Turn off Filter mouse motion events */
  SDL_SetEventFilter(NULL);
  MOVE_STEP_Y = DEFAULT_MOVE_STEP;
  
END:
  unsellect_widget_action();
  pSellected_Widget = pHoriz_Src;
  set_wstate(pHoriz_Src, FC_WS_SELLECTED);
  redraw_horiz(pHoriz_Src);
  flush_rect(pHoriz_Src->size);

  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int apply_taxrates_callback(struct GUI *pButton)
{
  struct GUI *pBuf;
  int science, luxury, tax;

  if (get_client_state() != CLIENT_GAME_RUNNING_STATE) {
    return -1;
  }

  /* Science Scrollbar */
  pBuf = pButton->next->next;
  science = *(int *)pBuf->data.ptr;
    
  /* Luxuries Scrollbar */
  pBuf = pBuf->next->next->next;
  luxury = *(int *)pBuf->data.ptr;
  
  /* Tax */
  tax = 100 - luxury - science;
  
  if(tax != game.player_ptr->economic.tax ||
    science != game.player_ptr->economic.science ||
    luxury != game.player_ptr->economic.luxury) {
    dsend_packet_player_rates(&aconnection, tax, luxury, science);
  }

  redraw_tibutton(pButton);
  flush_rect(pButton->size);

  return -1;
}

static void enable_economy_dlg(void)
{
  /* lux lock */
  struct GUI *pBuf = pEconomyDlg->pEndWidgetList->prev->prev->prev->prev->prev->prev;
  set_wstate(pBuf, FC_WS_NORMAL);
  
  /* lux scrollbar */
  pBuf = pBuf->prev;
  set_wstate(pBuf, FC_WS_NORMAL);
  
  /* sci lock */
  pBuf = pBuf->prev->prev;
  set_wstate(pBuf, FC_WS_NORMAL);
  
  /* sci scrollbar */
  pBuf = pBuf->prev;
  set_wstate(pBuf, FC_WS_NORMAL);
  
  /* update button */
  pBuf = pBuf->prev->prev;
  set_wstate(pBuf, FC_WS_NORMAL);
  
  /* cancel button */
  pBuf = pBuf->prev;
  set_wstate(pBuf, FC_WS_NORMAL);
  
  set_group_state(pEconomyDlg->pBeginActiveWidgetList,
			pEconomyDlg->pEndActiveWidgetList, FC_WS_NORMAL);
  if(pEconomyDlg->pScroll && pEconomyDlg->pActiveWidgetList) {
    set_wstate(pEconomyDlg->pScroll->pUp_Left_Button, FC_WS_NORMAL);
    set_wstate(pEconomyDlg->pScroll->pDown_Right_Button, FC_WS_NORMAL);
    set_wstate(pEconomyDlg->pScroll->pScrollBar, FC_WS_NORMAL);
  }
}

static void disable_economy_dlg(void)
{
  /* lux lock */
  struct GUI *pBuf = pEconomyDlg->pEndWidgetList->prev->prev->prev->prev->prev->prev;
  set_wstate(pBuf, FC_WS_DISABLED);
  
  /* lux scrollbar */
  pBuf = pBuf->prev;
  set_wstate(pBuf, FC_WS_DISABLED);
  
  /* sci lock */
  pBuf = pBuf->prev->prev;
  set_wstate(pBuf, FC_WS_DISABLED);
  
  /* sci scrollbar */
  pBuf = pBuf->prev;
  set_wstate(pBuf, FC_WS_DISABLED);
  
  /* update button */
  pBuf = pBuf->prev->prev;
  set_wstate(pBuf, FC_WS_DISABLED);
  
  /* cancel button */
  pBuf = pBuf->prev;
  set_wstate(pBuf, FC_WS_DISABLED);
  
  set_group_state(pEconomyDlg->pBeginActiveWidgetList,
			pEconomyDlg->pEndActiveWidgetList, FC_WS_DISABLED);
  if(pEconomyDlg->pScroll && pEconomyDlg->pActiveWidgetList) {
    set_wstate(pEconomyDlg->pScroll->pUp_Left_Button, FC_WS_DISABLED);
    set_wstate(pEconomyDlg->pScroll->pDown_Right_Button, FC_WS_DISABLED);
    set_wstate(pEconomyDlg->pScroll->pScrollBar, FC_WS_DISABLED);
  }
  
}

/* --------------------------------------------------------------- */
static int ok_sell_impv_callback(struct GUI *pWidget)
{
  int imp, total_count, count = 0;
  struct GUI *pImpr = (struct GUI *)pWidget->data.ptr;
    
  imp = pImpr->data.cont->id0;
  total_count = pImpr->data.cont->id1;
  
  /* popdown sell dlg */
  del_group_of_widgets_from_gui_list(pEconomy_Sell_Dlg->pBeginWidgetList,
			      pEconomy_Sell_Dlg->pEndWidgetList);
  FREE(pEconomy_Sell_Dlg);
  enable_economy_dlg();
  
  /* send sell */
  city_list_iterate(game.player_ptr->cities, pCity) {
    if(!pCity->did_sell && city_got_building(pCity, imp)){
	count++;

	city_sell_improvement(pCity, imp);
      
    }
  } city_list_iterate_end;
  
  if(count == total_count) {
    del_widget_from_vertical_scroll_widget_list(pEconomyDlg, pImpr);
  }
  
  return -1;
}

static int sell_impv_window_callback(struct GUI *pWindow)
{
  return std_move_window_group_callback(
  			pEconomy_Sell_Dlg->pBeginWidgetList, pWindow);
}

static int cancel_sell_impv_callback(struct GUI *pWidget)
{
  if (pEconomy_Sell_Dlg) {
    lock_buffer(pWidget->dst);
    popdown_window_group_dialog(pEconomy_Sell_Dlg->pBeginWidgetList,
			      pEconomy_Sell_Dlg->pEndWidgetList);
    unlock_buffer();
    FREE(pEconomy_Sell_Dlg);
    enable_economy_dlg();
    flush_dirty();
  }
  return -1;
}


static int popup_sell_impv_callback(struct GUI *pWidget)
{
  
  int imp, total_count ,count = 0, gold = 0;
  int value, hh, ww = 0;
  char cBuf[128];
  struct GUI *pBuf = NULL, *pWindow;
  SDL_String16 *pStr;
  SDL_Surface *pText, *pDest = pWidget->dst;
  SDL_Rect dst;
  
  if (pEconomy_Sell_Dlg) {
    return 1;
  }
  
  set_wstate(pWidget, FC_WS_NORMAL);
  pSellected_Widget = NULL;
  redraw_icon2(pWidget);
  sdl_dirty_rect(pWidget->size);
  
  pEconomy_Sell_Dlg = MALLOC(sizeof(struct SMALL_DLG));

  imp = pWidget->data.cont->id0;
  total_count = pWidget->data.cont->id1;
  value = impr_sell_gold(imp);
  
  city_list_iterate(game.player_ptr->cities, pCity) {
    if(!pCity->did_sell && city_got_building(pCity, imp)) {
	count++;
        gold += value;
    }
  } city_list_iterate_end;
  
  if(count > 0) {
    my_snprintf(cBuf, sizeof(cBuf),
    _("We have %d of %s\n(total value is : %d)\n"
    	"We can sell %d of them for %d gold"),
	    total_count, get_improvement_name(imp),
			    total_count * value, count, gold); 
  } else {
    my_snprintf(cBuf, sizeof(cBuf),
	_("We can't sell any %s in this turn"), get_improvement_name(imp)); 
  }
  
  
  hh = WINDOW_TILE_HIGH + 1;
  pStr = create_str16_from_char(_("Sell It?"), 12);
  pStr->style |= TTF_STYLE_BOLD;

  pWindow = create_window(pDest, pStr, 100, 100, 0);

  pWindow->action = sell_impv_window_callback;
  set_wstate(pWindow, FC_WS_NORMAL);

  pEconomy_Sell_Dlg->pEndWidgetList = pWindow;

  add_to_gui_list(ID_WINDOW, pWindow);

  /* ============================================================= */
  
  /* create text label */
  pStr = create_str16_from_char(cBuf, 10);
  pStr->style |= (TTF_STYLE_BOLD|SF_CENTER);
  pStr->fgcol.r = 255;
  pStr->fgcol.g = 255;
  /*pStr->fgcol.b = 255; */
  
  pText = create_text_surf_from_str16(pStr);
  FREESTRING16(pStr);
  
  hh += (pText->h + 10);
  ww = MAX(ww , pText->w + 20);
  
  /* cancel button */
  pBuf = create_themeicon_button_from_chars(pTheme->CANCEL_Icon,
			    pWindow->dst, _("No"), 12, 0);

  clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
  pBuf->action = cancel_sell_impv_callback;
  set_wstate(pBuf, FC_WS_NORMAL);

  hh += (pBuf->size.h + 20);
  
  add_to_gui_list(ID_BUTTON, pBuf);
  
  if (count > 0) {
    pBuf = create_themeicon_button_from_chars(pTheme->OK_Icon, pWindow->dst,
					      "Sell", 12, 0);
        
    clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
    pBuf->action = ok_sell_impv_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
    pBuf->data.ptr = (void *)pWidget;
    
    add_to_gui_list(ID_BUTTON, pBuf);
    pBuf->size.w = MAX(pBuf->size.w, pBuf->next->size.w);
    pBuf->next->size.w = pBuf->size.w;
    ww = MAX(ww, 30 + pBuf->size.w * 2);
  } else {
    ww = MAX(ww, pBuf->size.w + 20);
  }
  /* ============================================ */
  
  pEconomy_Sell_Dlg->pBeginWidgetList = pBuf;
  
  pWindow->size.x = pEconomyDlg->pEndWidgetList->size.x +
  		(pEconomyDlg->pEndWidgetList->size.w - ww) / 2;
  pWindow->size.y = pEconomyDlg->pEndWidgetList->size.y +
		(pEconomyDlg->pEndWidgetList->size.h - hh) / 2;
  
  resize_window(pWindow, NULL,
		get_game_colorRGB(COLOR_STD_BACKGROUND_BROWN),
		ww + DOUBLE_FRAME_WH, hh);
  
  /* setup rest of widgets */
  /* label */
  dst.x = FRAME_WH + (ww - DOUBLE_FRAME_WH - pText->w) / 2;
  dst.y = WINDOW_TILE_HIGH + 11;
  SDL_BlitSurface(pText, NULL, pWindow->theme, &dst);
  FREESURFACE(pText);
   
  /* cancel button */
  pBuf = pWindow->prev;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 10;
  
  if (count > 0) {
    /* sell button */
    pBuf = pBuf->prev;
    pBuf->size.x = pWindow->size.x + (ww - (2 * pBuf->size.w + 10)) / 2;
    pBuf->size.y = pBuf->next->size.y;
    
    /* cancel button */
    pBuf->next->size.x = pBuf->size.x + pBuf->size.w + 10;
  } else {
    /* x position of cancel button */
    pBuf->size.x = pWindow->size.x +
			    pWindow->size.w - FRAME_WH - pBuf->size.w - 10;
  }
  
  
  /* ================================================== */
  /* redraw */
  redraw_group(pEconomy_Sell_Dlg->pBeginWidgetList, pWindow, 0);
  disable_economy_dlg();
  
  sdl_dirty_rect(pWindow->size);
  flush_dirty();
  return -1;
}


/**************************************************************************
  Update the economy report.
**************************************************************************/
void economy_report_dialog_update(void)
{
  if(pEconomyDlg && !is_report_dialogs_frozen()) {
    struct GUI *pBuf = pEconomyDlg->pEndWidgetList;
    int tax, total, entries_used = 0;
    char cBuf[128];
    struct improvement_entry entries[B_LAST];
    
    get_economy_report_data(entries, &entries_used, &total, &tax);
  
    /* tresure */
    pBuf = pBuf->prev;
    my_snprintf(cBuf, sizeof(cBuf), "%d", game.player_ptr->economic.gold);
    copy_chars_to_string16(pBuf->string16, cBuf);
    remake_label_size(pBuf);
  
    /* Icome */
    pBuf = pBuf->prev->prev;
    my_snprintf(cBuf, sizeof(cBuf), "%d", tax);
    copy_chars_to_string16(pBuf->string16, cBuf);
    remake_label_size(pBuf);
  
    /* Cost */
    pBuf = pBuf->prev;
    my_snprintf(cBuf, sizeof(cBuf), "%d", total);
    copy_chars_to_string16(pBuf->string16, cBuf);
    remake_label_size(pBuf);
  
    /* Netto */
    pBuf = pBuf->prev;
    my_snprintf(cBuf, sizeof(cBuf), "%d", tax - total);
    copy_chars_to_string16(pBuf->string16, cBuf);
    remake_label_size(pBuf);
    if(tax - total < 0) {
      pBuf->string16->fgcol.r = 255;
    } else {
      pBuf->string16->fgcol.r = 0;
    }
  
  
    /* ---------------- */
    redraw_group(pEconomyDlg->pBeginWidgetList, pEconomyDlg->pEndWidgetList, 0);
    flush_rect(pEconomyDlg->pEndWidgetList->size);
  }
}

/**************************************************************************
  Popdown the economy report.
**************************************************************************/
void popdown_economy_report_dialog(void)
{
  if(pEconomyDlg) {
    if (pEconomy_Sell_Dlg) {
       del_group_of_widgets_from_gui_list(pEconomy_Sell_Dlg->pBeginWidgetList,
			      pEconomy_Sell_Dlg->pEndWidgetList);
       FREE(pEconomy_Sell_Dlg); 
    }    
    popdown_window_group_dialog(pEconomyDlg->pBeginWidgetList,
					    pEconomyDlg->pEndWidgetList);
    FREE(pEconomyDlg->pScroll);
    FREE(pEconomyDlg);
    set_wstate(get_tax_rates_widget(), FC_WS_NORMAL);
    redraw_icon2(get_tax_rates_widget());
    sdl_dirty_rect(get_tax_rates_widget()->size);
  }
}

#define TARGETS_ROW	2
#define TARGETS_COL	4

/**************************************************************************
  Popup (or raise) the economy report (F5).  It may or may not be modal.
**************************************************************************/
void popup_economy_report_dialog(bool make_modal)
{
  struct GUI *pBuf = get_tax_rates_widget();
  struct GUI *pWindow , *pLast;
  SDL_String16 *pStr;
  SDL_Surface *pSurf, *pText_Name, *pText, *pZoom, *pText2;
  SDL_Surface *pMain;
  int i, w = 0 , count , h, w2 = 0, w3;
  int tax, total, entries_used = 0;
  char cBuf[128];
  struct improvement_entry entries[B_LAST];
  SDL_Color color = {255,255,255,128};
  SDL_Rect dst;
  struct government *pGov = get_gov_pplayer(game.player_ptr);
    
  if(pEconomyDlg) {
    return;
  }
  
  set_wstate(pBuf, FC_WS_DISABLED);
  redraw_icon2(pBuf);
  sdl_dirty_rect(pBuf->size);
  
  pEconomyDlg = MALLOC(sizeof(struct ADVANCED_DLG));
  
  get_economy_report_data(entries, &entries_used, &total, &tax);
  
  /* --------------- */
  pStr = create_str16_from_char(_("Economy Report"), 12);
  pStr->style |= TTF_STYLE_BOLD;

  pWindow = create_window(NULL, pStr, 40, 30, 0);
  pEconomyDlg->pEndWidgetList = pWindow;
  h = WINDOW_TILE_HIGH + 1 + FRAME_WH;
  set_wstate(pWindow, FC_WS_NORMAL);
  pWindow->action = economy_dialog_callback;
  
  add_to_gui_list(ID_ECONOMY_DIALOG_WINDOW, pWindow);
  
  /* ------------------------- */
  /* Total Treasury */
  my_snprintf(cBuf, sizeof(cBuf), "%d", game.player_ptr->economic.gold);

  pStr = create_str16_from_char(cBuf, 12);
  pStr->style |= (TTF_STYLE_BOLD|SF_CENTER);

  pBuf = create_iconlabel(pIcons->pBIG_Coin, pWindow->dst, pStr,
  			(WF_DRAW_THEME_TRANSPARENT|WF_ICON_CENTER_RIGHT));
  
  w = MAX(w, pBuf->size.w);
  h += pBuf->size.h;
  add_to_gui_list(ID_LABEL, pBuf);

  /* Tax Rate */
  /* it is important to leave 1 space at ending of this string */
  my_snprintf(cBuf, sizeof(cBuf), "%d%% " , game.player_ptr->economic.tax);
  pStr = create_str16_from_char(cBuf, 12);
  pStr->style |= (TTF_STYLE_BOLD|SF_CENTER);
  
  pBuf = create_iconlabel(NULL, pWindow->dst, pStr, WF_DRAW_THEME_TRANSPARENT);
    
  h += pBuf->size.h;
  add_to_gui_list(ID_LABEL, pBuf);
  w = MAX(w, pBuf->size.w + pBuf->next->size.w);
  
  /* Total Icome Label */
  my_snprintf(cBuf, sizeof(cBuf), "%d", tax);
  pStr = create_str16_from_char(cBuf, 12);
  pStr->style |= TTF_STYLE_BOLD;
  
  pBuf = create_iconlabel(NULL, pWindow->dst, pStr, WF_DRAW_THEME_TRANSPARENT);
    
  w = MAX(w, pBuf->size.w);
  h += pBuf->size.h;
  add_to_gui_list(ID_LABEL, pBuf);
  
  /* Total Cost Label */
  my_snprintf(cBuf, sizeof(cBuf), "%d", total);
  pStr = create_str16_from_char(cBuf, 12);
  pStr->style |= TTF_STYLE_BOLD;
  
  pBuf = create_iconlabel(NULL, pWindow->dst, pStr, WF_DRAW_THEME_TRANSPARENT);
    
  w = MAX(w, pBuf->size.w);
  h += pBuf->size.h;
  add_to_gui_list(ID_LABEL, pBuf);
  
  /* Net Icome */
  my_snprintf(cBuf, sizeof(cBuf), "%d", tax - total);
  pStr = create_str16_from_char(cBuf, 12);
  pStr->style |= (TTF_STYLE_BOLD|SF_CENTER);
  
  if(tax - total < 0) {
    pStr->fgcol.r = 255;
  }
  
  pBuf = create_iconlabel(NULL, pWindow->dst, pStr, WF_DRAW_THEME_TRANSPARENT);
			  
  w = MAX(w, pBuf->size.w);
  h += pBuf->size.h;
  add_to_gui_list(ID_LABEL, pBuf);
    
  /* ------------------------- */
  /* lux rate */
  
  my_snprintf(cBuf, sizeof(cBuf), _("Lock"));
  pStr = create_str16_from_char(cBuf, 10);
  pStr->style |= TTF_STYLE_BOLD;

  pBuf = create_checkbox(pWindow->dst, 
      		(SDL_Client_Flags & CF_CHANGE_TAXRATE_LUX_BLOCK),
      		(WF_DRAW_THEME_TRANSPARENT|WF_WIDGET_HAS_INFO_LABEL));

  set_new_checkbox_theme(pBuf, pTheme->LOCK_Icon, pTheme->UNLOCK_Icon);
  w2 = 10 + pBuf->size.w;  
  pBuf->string16 = pStr;
  pBuf->action = toggle_block_callback;
  set_wstate(pBuf, FC_WS_NORMAL);

  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_LUX_BLOCK_CHECKBOX, pBuf);
  
  /* ---- */
  pBuf = create_horizontal(pTheme->Horiz, pWindow->dst, 30,
			(WF_FREE_DATA | WF_DRAW_THEME_TRANSPARENT));

  pBuf->action = horiz_taxrate_callback;
  pBuf->data.ptr = MALLOC(sizeof(int));
  *(int *)pBuf->data.ptr = game.player_ptr->economic.luxury;
  w2 += 184;
  set_wstate(pBuf, FC_WS_NORMAL);

  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_LUX_SCROLLBAR, pBuf);
  /* ---- */
  
  /* it is important to leave 1 space at ending of this string */
  my_snprintf(cBuf, sizeof(cBuf), "%d%% ", game.player_ptr->economic.luxury);
  pStr = create_str16_from_char(cBuf, 11);
  pStr->style |= TTF_STYLE_BOLD;

  pBuf = create_iconlabel(pIcons->pBIG_Luxury, pWindow->dst, pStr,
					      WF_DRAW_THEME_TRANSPARENT);
  w2 += (5 + pBuf->size.w + 10);
  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_LUX_LABEL, pBuf);
  /* ------------------------- */
  /* science rate */
  
  my_snprintf(cBuf, sizeof(cBuf), _("Lock"));
  pStr = create_str16_from_char(cBuf, 10);
  pStr->style |= TTF_STYLE_BOLD;

  pBuf = create_checkbox(pWindow->dst,
	      (SDL_Client_Flags & CF_CHANGE_TAXRATE_SCI_BLOCK),
      		(WF_DRAW_THEME_TRANSPARENT|WF_WIDGET_HAS_INFO_LABEL));

  set_new_checkbox_theme(pBuf, pTheme->LOCK_Icon, pTheme->UNLOCK_Icon);
    
  pBuf->string16 = pStr;
  pBuf->action = toggle_block_callback;
  set_wstate(pBuf, FC_WS_NORMAL);

  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_SCI_BLOCK_CHECKBOX, pBuf);
  /* ---- */
  
  pBuf = create_horizontal(pTheme->Horiz, pWindow->dst, 30,
				(WF_FREE_DATA | WF_DRAW_THEME_TRANSPARENT));

  pBuf->action = horiz_taxrate_callback;
  pBuf->data.ptr = MALLOC(sizeof(int));
  *(int *)pBuf->data.ptr = game.player_ptr->economic.science;
  
  set_wstate(pBuf, FC_WS_NORMAL);

  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_SCI_SCROLLBAR, pBuf);
  /* ---- */
  
  /* it is important to leave 1 space at ending of this string */
  my_snprintf(cBuf, sizeof(cBuf), "%d%% ", game.player_ptr->economic.science);
  pStr = create_str16_from_char(cBuf, 11);
  pStr->style |= TTF_STYLE_BOLD;

  pBuf = create_iconlabel(pIcons->pBIG_Colb, pWindow->dst, pStr,
					      WF_DRAW_THEME_TRANSPARENT);

  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_SCI_LABEL, pBuf);
  /* ---- */
  
  my_snprintf(cBuf, sizeof(cBuf), _("Update"));
  pStr = create_str16_from_char(cBuf, 12);
  pBuf = create_themeicon_button(pTheme->Small_OK_Icon, pWindow->dst, pStr,
  			  			WF_DRAW_THEME_TRANSPARENT);
  pBuf->action = apply_taxrates_callback;
  clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
  set_wstate(pBuf, FC_WS_NORMAL);

  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_OK_BUTTON, pBuf);
  
  /* ---- */
  
  my_snprintf(cBuf, sizeof(cBuf), _("Cancel"));
  pStr = create_str16_from_char(cBuf, 12);
  pBuf = create_themeicon_button(pTheme->Small_CANCEL_Icon, pWindow->dst, pStr,
  			  			WF_DRAW_THEME_TRANSPARENT);
  clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
  pBuf->action = exit_economy_dialog_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  
  add_to_gui_list(ID_CHANGE_TAXRATE_DLG_CANCEL_BUTTON, pBuf);
  pBuf->size.w = MAX(pBuf->size.w , pBuf->next->size.w);
  pBuf->next->size.w = pBuf->size.w;
  
  w2 = MAX(w2 , 10 + 2 * pBuf->size.w + 10 + 10);
  h += 5;
  /* ------------------------- */
  pLast = pBuf;
  if(entries_used) {
    
    /* Create Imprv Background Icon */
    pMain = create_surf(116, 116, SDL_SWSURFACE);
    pSurf = SDL_DisplayFormatAlpha(pMain);
    SDL_FillRect(pSurf, NULL, SDL_MapRGBA(pSurf->format, color.r,
					    color.g, color.b, color.unused));
    putframe(pSurf, 0, 0, pSurf->w - 1, pSurf->h - 1, 0xFF000000);
    FREESURFACE(pMain);
    pMain = pSurf;
    pSurf = NULL;
    
    pStr = create_string16(NULL, 0, 10);
    pStr->style |= (SF_CENTER|TTF_STYLE_BOLD);
    pStr->render = 3;
    pStr->bgcol = color;
  
    for (i = 0; i < entries_used; i++) {
      struct improvement_entry *p = &entries[i];
	
      pSurf = crop_rect_from_surface(pMain, NULL);
      
      my_snprintf(cBuf, sizeof(cBuf), "%s", get_improvement_name(p->type));
      
      copy_chars_to_string16(pStr, cBuf);
      pStr->style |= TTF_STYLE_BOLD;
      pText_Name = create_text_surf_smaller_that_w(pStr, pSurf->w - 4);
      SDL_SetAlpha(pText_Name, 0x0, 0x0);
            
      my_snprintf(cBuf, sizeof(cBuf), "%s %d\n%s %d",
			_("Built"), p->count, _("U Total"),p->total_cost);
      copy_chars_to_string16(pStr, cBuf);
      pStr->style &= ~TTF_STYLE_BOLD;
  
      pText = create_text_surf_from_str16(pStr);
      SDL_SetAlpha(pText, 0x0, 0x0);

      
      /*-----------------*/
  
      pZoom = ZoomSurface(
      		GET_SURF(get_improvement_type(p->type)->sprite), 1.5, 1.5, 1);
      dst.x = (pSurf->w - pZoom->w)/2;
      dst.y = (pSurf->h/2 - pZoom->h)/2;
      SDL_BlitSurface(pZoom, NULL, pSurf, &dst);
      dst.y += pZoom->h;
      FREESURFACE(pZoom);
  
      dst.x = (pSurf->w - pText_Name->w)/2;
      dst.y += ((pSurf->h - dst.y) -
	      (pText_Name->h + (pIcons->pBIG_Coin->h + 2) + pText->h))/2;
      SDL_BlitSurface(pText_Name, NULL, pSurf, &dst);
      
      dst.y += pText_Name->h;
      if(p->cost) {
	dst.x = (pSurf->w - p->cost * (pIcons->pBIG_Coin->w + 1))/2;
        for(count = 0; count < p->cost; count++) {
	  SDL_BlitSurface(pIcons->pBIG_Coin, NULL, pSurf, &dst);
	  dst.x += pIcons->pBIG_Coin->w + 1;
        }
      } else {
	if(p->type == B_PALACE) {
	  copy_chars_to_string16(pStr, _("Nation"));
	} else {
	  copy_chars_to_string16(pStr, _("Wonder"));
	}
        /*pStr->style &= ~TTF_STYLE_BOLD;*/
  
        pZoom = create_text_surf_from_str16(pStr);
        SDL_SetAlpha(pZoom, 0x0, 0x0);
	
	dst.x = (pSurf->w - pZoom->w)/2;
	SDL_BlitSurface(pZoom, NULL, pSurf, &dst);
	FREESURFACE(pZoom);
      }
      
      dst.y += (pIcons->pBIG_Coin->h + 2);
      dst.x = (pSurf->w - pText->w)/2;
      SDL_BlitSurface(pText, NULL, pSurf, &dst);
  
      FREESURFACE(pText);
      FREESURFACE(pText_Name);
            
      pBuf = create_icon2(pSurf, pWindow->dst,
    		(WF_DRAW_THEME_TRANSPARENT|WF_FREE_THEME|WF_FREE_DATA));
      
      set_wstate(pBuf, FC_WS_NORMAL);
      
      pBuf->data.cont = MALLOC(sizeof(struct CONTAINER));
      pBuf->data.cont->id0 = p->type;
      pBuf->data.cont->id1 = p->count;
      pBuf->action = popup_sell_impv_callback;
      
      add_to_gui_list(MAX_ID - i, pBuf);
      
      if(i > (TARGETS_ROW * TARGETS_COL - 1)) {
        set_wflag(pBuf, WF_HIDDEN);
      }
      
    }
  
    FREESTRING16(pStr);
    FREESURFACE(pMain);
    
    pEconomyDlg->pEndActiveWidgetList = pLast->prev;
    pEconomyDlg->pBeginActiveWidgetList = pBuf;
    pEconomyDlg->pBeginWidgetList = pBuf;
    
    if(entries_used > TARGETS_ROW * TARGETS_COL) {
      pEconomyDlg->pActiveWidgetList = pEconomyDlg->pEndActiveWidgetList;
      count = create_vertical_scrollbar(pEconomyDlg,
		    		TARGETS_COL, TARGETS_ROW, TRUE, TRUE);
      h += (TARGETS_ROW * pBuf->size.h + 10);
    } else {
      count = 0;
      if(entries_used > TARGETS_COL) {
	h += pBuf->size.h;
      }
      h += (10 + pBuf->size.h);
    }
    count = TARGETS_COL * pBuf->size.w + count + DOUBLE_FRAME_WH;  
  } else {
    pEconomyDlg->pBeginWidgetList = pBuf;
    h += 10;
    count = 0;
  }
  
  /* tresure */
  my_snprintf(cBuf, sizeof(cBuf), _("Treasury: "));
  pStr = create_str16_from_char(cBuf, 12);
  pStr->style |= TTF_STYLE_BOLD;
  pText = create_text_surf_from_str16(pStr);
  w3 = pText->w;
  
  /* tax rate label */
  my_snprintf(cBuf, sizeof(cBuf), _("Tax Rate: "));
  copy_chars_to_string16(pStr, cBuf);
  pText_Name = create_text_surf_from_str16(pStr);
  w3 = MAX(w3, pText_Name->w);
  /* total icome */
  my_snprintf(cBuf, sizeof(cBuf), _("Total Income: "));
  copy_chars_to_string16(pStr, cBuf);
  pSurf = create_text_surf_from_str16(pStr);
  w3 = MAX(w3, pSurf->w);
  /* total cost */
  my_snprintf(cBuf, sizeof(cBuf), _("Total Cost: "));
  copy_chars_to_string16(pStr, cBuf);
  pZoom = create_text_surf_from_str16(pStr);

  /* net icome */
  my_snprintf(cBuf, sizeof(cBuf), _("Net Income: "));
  copy_chars_to_string16(pStr, cBuf);
  pText2 = create_text_surf_from_str16(pStr);
  w3 = MAX(w3, pText2->w);
  
  w = MAX(FRAME_WH + 10 + w3 + w + w2 + FRAME_WH, count);
  
  pWindow->size.x = (Main.screen->w - w) / 2;
  pWindow->size.y = (Main.screen->h - h) / 2;
    
  pMain = get_logo_gfx();
  if(resize_window(pWindow, pMain, NULL, w, h)) {
    FREESURFACE(pMain);
  }
  
  pMain = SDL_DisplayFormat(pWindow->theme);
  FREESURFACE(pWindow->theme);
  pWindow->theme = pMain;
  pMain = NULL;
      
  pBuf = pWindow->prev;
  pBuf->size.x = pWindow->size.x + FRAME_WH + 10 + pText->w;
  pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 1 + 5;
  h = pBuf->size.h;
  w = pBuf->size.w + pText->w;
      
  pBuf = pBuf->prev;
  pBuf->size.x = pWindow->size.x + FRAME_WH + 10 + pText_Name->w;
  pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
  h += pBuf->size.h;
  w = MAX(w, pBuf->size.w + pText_Name->w);
      
  pBuf = pBuf->prev;
  pBuf->size.x = pWindow->size.x + FRAME_WH + 10 + pSurf->w;
  pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
  h += pBuf->size.h;
  w = MAX(w, pBuf->size.w + pSurf->w);
   
  pBuf = pBuf->prev;
  pBuf->size.x = pWindow->size.x + FRAME_WH + 10 + pZoom->w;
  pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
  h += pBuf->size.h;
  w = MAX(w, pBuf->size.w + pZoom->w);
      
  pBuf = pBuf->prev;
  pBuf->size.x = pWindow->size.x + FRAME_WH + 10 + pText2->w;
  pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
  h += pBuf->size.h;
  w = MAX(w, pBuf->size.w + pText2->w);
  
  /* Backgrounds */
  dst.x = FRAME_WH;
  dst.y = WINDOW_TILE_HIGH + 1;
  dst.w = pWindow->size.w - DOUBLE_FRAME_WH;
  dst.h = h + 15;
  h = dst.y + dst.h;
  
  color.unused = 136;
  SDL_FillRectAlpha(pWindow->theme, &dst, &color);
  
  putframe(pWindow->theme, dst.x, dst.y,
			  dst.x + dst.w - 1, dst.y + dst.h - 1, 0xFF000000);
  
  /* draw statical strings */
  dst.x = FRAME_WH + 10;
  dst.y = WINDOW_TILE_HIGH + 1 + 5;
  SDL_BlitSurface(pText, NULL, pWindow->theme, &dst);
  dst.y += pText->h;
  FREESURFACE(pText);

  SDL_BlitSurface(pText_Name, NULL, pWindow->theme, &dst);
  dst.y += pText_Name->h;
  FREESURFACE(pText_Name);

  SDL_BlitSurface(pSurf, NULL, pWindow->theme, &dst);
  dst.y += pSurf->h;
  FREESURFACE(pSurf);

  SDL_BlitSurface(pZoom, NULL, pWindow->theme, &dst);
  dst.y += pZoom->h;
  FREESURFACE(pZoom);
  
  SDL_BlitSurface(pText2, NULL, pWindow->theme, &dst);
  dst.y += pText2->h;
  FREESURFACE(pText2);

  /* gov and taxrate */
  my_snprintf(cBuf, sizeof(cBuf), _("%s max rate : %d%%"),
	      				pGov->name, pGov->max_rate);
  copy_chars_to_string16(pStr, cBuf);
  pMain = create_text_surf_from_str16(pStr);
  FREESTRING16(pStr);
  dst.y = WINDOW_TILE_HIGH + 1 + 5;
  dst.x = FRAME_WH + 10 + w +
	(pWindow->size.w - (w + DOUBLE_FRAME_WH + 10) - pMain->w) / 2;
	
  SDL_BlitSurface(pMain, NULL, pWindow->theme, &dst);
  dst.y += (pMain->h + 1);
  FREESURFACE(pMain);
  
  /* Luxuries Horizontal Scrollbar Background */
  dst.x = FRAME_WH + 10 + w +
	(pWindow->size.w - (w + DOUBLE_FRAME_WH + 10) - 184) / 2;
  dst.w = 184;
  dst.h = pTheme->Horiz->h - 2;
  
  color.unused = 64;
  SDL_FillRectAlpha(pWindow->theme, &dst, &color);
  
  putframe(pWindow->theme, dst.x, dst.y,
			  dst.x + dst.w - 1, dst.y + dst.h - 1, 0xFF000000);
  
  /* lock icon */
  pBuf = pBuf->prev;
  pBuf->size.x = pWindow->size.x + dst.x - pBuf->size.w;
  pBuf->size.y = pWindow->size.y + dst.y - 2;
  
  /* lux scrollbar */
  pBuf = pBuf->prev;
  pBuf->size.x = pWindow->size.x + dst.x + 2
		  + (game.player_ptr->economic.luxury * 3) / 2;
  pBuf->size.y = pWindow->size.y + dst.y -1;
  
  /* lux rate */
  pBuf = pBuf->prev;
  pBuf->size.x = pWindow->size.x + dst.x + dst.w + 5;
  pBuf->size.y = pWindow->size.y + dst.y + 1;
  
  
  /* Science Horizontal Scrollbar Background */
  dst.y += pTheme->Horiz->h + 1;
  SDL_FillRectAlpha(pWindow->theme, &dst, &color);
  
  putframe(pWindow->theme, dst.x, dst.y,
			  dst.x + dst.w - 1, dst.y + dst.h - 1, 0xFF000000);
  
  /* science lock icon */
  pBuf = pBuf->prev;
  pBuf->size.x = pWindow->size.x + dst.x - pBuf->size.w;
  pBuf->size.y = pWindow->size.y + dst.y - 2;
  
  /* science scrollbar */
  pBuf = pBuf->prev;
  pBuf->size.x = pWindow->size.x + dst.x + 2
		  + (game.player_ptr->economic.science * 3) / 2;
  pBuf->size.y = pWindow->size.y + dst.y -1;
  
  /* science rate */
  pBuf = pBuf->prev;
  pBuf->size.x = pWindow->size.x + dst.x + dst.w + 5;
  pBuf->size.y = pWindow->size.y + dst.y + 1;

  /* update */
  pBuf = pBuf->prev;
  pBuf->size.x = pWindow->size.x + FRAME_WH + 10 + w +
	(pWindow->size.w - (w + DOUBLE_FRAME_WH + 10)
					- (2 * pBuf->size.w + 10)) / 2;
  pBuf->size.y = pWindow->size.y + dst.y + dst.h + 3;
    
  /* cancel */
  pBuf = pBuf->prev;
  pBuf->size.x = pBuf->next->size.x + pBuf->next->size.w + 10;
  pBuf->size.y = pWindow->size.y + dst.y + dst.h + 3;  
  /* ------------------------------- */
  
  if(entries_used) {
    setup_vertical_widgets_position(TARGETS_COL,
	pWindow->size.x + FRAME_WH,
	pWindow->size.y + h,
	  0, 0, pEconomyDlg->pBeginActiveWidgetList,
			  pEconomyDlg->pEndActiveWidgetList);
    if(pEconomyDlg->pScroll) {
      setup_vertical_scrollbar_area(pEconomyDlg->pScroll,
	pWindow->size.x + pWindow->size.w - FRAME_WH,
    	pWindow->size.y + h,
    	pWindow->size.h - (h + FRAME_WH + 1), TRUE);
    }
  }
  
  /* ------------------------ */
  redraw_group(pEconomyDlg->pBeginWidgetList, pWindow, 0);
  sdl_dirty_rect(pWindow->size);
  flush_dirty();
  
}

/* ===================================================================== */
/* ======================== Science Report ============================= */
/* ===================================================================== */
static struct SMALL_DLG *pScienceDlg = NULL;

static struct ADVANCED_DLG *pChangeTechDlg = NULL;

void setup_auxiliary_tech_icons(void)
{
  SDL_Surface *pSurf;
  SDL_String16 *pStr = create_str16_from_char(_("None"), 10);
  
  /* create "None" icon */
  pNone_Tech_Icon = create_surf(50, 50, SDL_SWSURFACE);
  SDL_FillRect(pNone_Tech_Icon, NULL,
	  SDL_MapRGB(pNone_Tech_Icon->format, 255 , 255 , 255));
  putframe(pNone_Tech_Icon, 0 , 0,
	  pNone_Tech_Icon->w - 1, pNone_Tech_Icon->h - 1 , 0x0);
  pFuture_Tech_Icon = SDL_DisplayFormat(pNone_Tech_Icon);
  
  pStr->style |= (TTF_STYLE_BOLD | SF_CENTER);
  
  pSurf = create_text_surf_from_str16(pStr);
    
  blit_entire_src(pSurf, pNone_Tech_Icon ,
	  (50 - pSurf->w) / 2 , (50 - pSurf->h) / 2);
  
  FREESURFACE(pSurf);
  copy_chars_to_string16(pStr, _("FT"));
  pSurf = create_text_surf_from_str16(pStr);
  blit_entire_src(pSurf, pFuture_Tech_Icon,
	  (50 - pSurf->w) / 2 , (50 - pSurf->h) / 2);
  
  
  FREESURFACE(pSurf);
  FREESTRING16(pStr);
    
}

void free_auxiliary_tech_icons(void)
{
  FREESURFACE(pNone_Tech_Icon);
  FREESURFACE(pFuture_Tech_Icon);
}

SDL_Surface * get_tech_icon(Tech_Type_id tech)
{
  switch(tech)
  {
    case A_NONE:
    case A_UNSET:
    case A_NOINFO:
    case A_LAST:
      return pNone_Tech_Icon;
    case A_FUTURE:
      return pFuture_Tech_Icon;
    default:
      return GET_SURF(advances[tech].sprite);
  }
  return NULL;
}

SDL_Color * get_tech_color(Tech_Type_id tech_id)
{
  if (tech_is_available(game.player_ptr, tech_id))
  {
    switch (get_invention(game.player_ptr, tech_id))
    {
      case TECH_UNKNOWN:
        return get_game_colorRGB(COLOR_STD_WHITE);
      case TECH_KNOWN:
        return get_game_colorRGB(COLOR_STD_GROUND);
      case TECH_REACHABLE:
        return get_game_colorRGB(COLOR_STD_YELLOW);
      default:
        return get_game_colorRGB(COLOR_STD_RED);
    }
  }
  return get_game_colorRGB(COLOR_STD_RED);
}

SDL_Surface * create_sellect_tech_icon(SDL_String16 *pStr, Tech_Type_id tech_id, enum tech_info_mode mode)
{
  struct impr_type *pImpr = NULL;
  struct unit_type *pUnit = NULL;
  SDL_Surface *pSurf, *pText, *pTmp;
  SDL_Surface *Surf_Array[10], **pBuf_Array;
  SDL_Rect dst;
  SDL_Color color;
  int w, h;
  
  color = *get_tech_color(tech_id);
  switch (mode)
  {
    case SMALL_MODE:
      h = 40;
      w = 135;
      break;
    case MED_MODE:
      color = *get_game_colorRGB(COLOR_STD_WHITE);
    default:
      h = 200;
      w = 100;
    break;
  }

  pText = create_text_surf_smaller_that_w(pStr, 100 - 4);  
  
  pTmp = create_surf(w, h, SDL_SWSURFACE);
  pSurf = SDL_DisplayFormatAlpha(pTmp);
  FREESURFACE(pTmp);
  
  if (game.player_ptr->research.researching == tech_id)
  {
    color.unused = 180;
  } else {
    color.unused = 128;
  }

  SDL_FillRect(pSurf, NULL,
	SDL_MapRGBA(pSurf->format, color.r, color.g, color.b, color.unused));
  putframe(pSurf, 0,0, pSurf->w - 1, pSurf->h - 1, 0xFF000000);
  
  pTmp = get_tech_icon(tech_id);
  
  if (mode == SMALL_MODE)
  {
    /* draw name tech text */ 
    dst.x = 35 + (pSurf->w - pText->w - 35) / 2;
    dst.y = (pSurf->h - pText->h) / 2;
    SDL_BlitSurface(pText, NULL, pSurf, &dst);
    FREESURFACE(pText);
    
    /* draw tech icon */
    pText = ResizeSurface(pTmp, 25, 25, 1);
    dst.x = (35 - pText->w) / 2;;
    dst.y = (pSurf->h - pText->h) / 2;
    SDL_BlitSurface(pText, NULL, pSurf, &dst);
    FREESURFACE(pText);
    
  } else {
  
    /* draw name tech text */ 
    dst.x = (pSurf->w - pText->w) / 2;
    dst.y = 20;
    SDL_BlitSurface(pText, NULL, pSurf, &dst);
    dst.y += pText->h + 10;
    FREESURFACE(pText);
    
    /* draw tech icon */
    dst.x = (pSurf->w - pTmp->w) / 2;
    SDL_BlitSurface(pTmp, NULL, pSurf, &dst);
    dst.y += pTmp->w + 10;

    /* fill array with iprvm. icons */
    w = 0;
    impr_type_iterate(imp) {
      pImpr = get_improvement_type(imp);
      if (pImpr->tech_req == tech_id) {
        Surf_Array[w++] = GET_SURF(pImpr->sprite);
      }
    } impr_type_iterate_end;

    if (w) {
      if (w >= 2) {
        dst.x = (pSurf->w - 2 * Surf_Array[0]->w) / 2;
      } else {
        dst.x = (pSurf->w - Surf_Array[0]->w) / 2;
      }

      /* draw iprvm. icons */
      pBuf_Array = Surf_Array;
      h = 0;
      while (w) {
        SDL_BlitSurface(*pBuf_Array, NULL, pSurf, &dst);
        dst.x += (*pBuf_Array)->w;
        w--;
        h++;
        if (!(h % 2)) {
          if (w >= 2) {
            dst.x = (pSurf->w - 2 * (*pBuf_Array)->w) / 2;
          } else {
            dst.x = (pSurf->w - (*pBuf_Array)->w) / 2;
          }
          dst.y += (*pBuf_Array)->h;
          h = 0;
        }	/* h == 2 */
        pBuf_Array++;
      }	/* while */
      dst.y += Surf_Array[0]->h + 5;
    } /* if (w) */
  /* -------------------------------------------------------- */
    w = 0;
    unit_type_iterate(un) {
      pUnit = get_unit_type(un);
      if (pUnit->tech_requirement == tech_id) {
        Surf_Array[w++] = GET_SURF(pUnit->sprite);
      }
    } unit_type_iterate_end;

    if (w) {
      if (w < 2) {
        /* w == 1 */
        if (Surf_Array[0]->w > 64) {
	  float zoom = 64.0 / Surf_Array[0]->w;
	  SDL_Surface *pZoomed = ZoomSurface(Surf_Array[0], zoom, zoom, 1);
	
	  dst.x = (pSurf->w - pZoomed->w) / 2;
	  SDL_BlitSurface(pZoomed, NULL, pSurf, &dst);
	  FREESURFACE(pZoomed);
        } else {
          dst.x = (pSurf->w - Surf_Array[0]->w) / 2;
          SDL_BlitSurface(Surf_Array[0], NULL, pSurf, &dst);
        }
      } else {
        float zoom;
      
        if (w > 2) {
	  zoom = 38.0 / Surf_Array[0]->w;
        } else {
	  zoom = 45.0 / Surf_Array[0]->w;
        }
        dst.x = (pSurf->w - (Surf_Array[0]->w * 2) * zoom - 2) / 2;
        pBuf_Array = Surf_Array;
        h = 0;
        while (w) {
	  SDL_Surface *pZoomed = ZoomSurface((*pBuf_Array), zoom, zoom, 1);
	
	  SDL_SetColorKey(pZoomed, SDL_SRCCOLORKEY, get_first_pixel(pZoomed));
          SDL_BlitSurface(pZoomed, NULL, pSurf, &dst);
          dst.x += pZoomed->w + 2;
          w--;
          h++;
          if (!(h % 2)) {
            if (w >= 2) {
	      dst.x = (pSurf->w - 2 * pZoomed->w - 2 ) / 2;
            } else {
	      dst.x = (pSurf->w - pZoomed->w) / 2;
            }
	    dst.y += pZoomed->h + 2;
            h = 0;
          }	/* h == 2 */
          pBuf_Array++;
	  FREESURFACE(pZoomed);
        }	/* while */
      } /* w > 1 */
    }/* if (w) */
  }
  
  return pSurf;
}

/**************************************************************************
  enable science dialog group ( without window )
**************************************************************************/
static void enable_science_dialog(void)
{
  set_group_state(pScienceDlg->pBeginWidgetList,
		     pScienceDlg->pEndWidgetList->prev, FC_WS_NORMAL);
}

/**************************************************************************
  disable science dialog group ( without window )
**************************************************************************/
static void disable_science_dialog(void)
{
  set_group_state(pScienceDlg->pBeginWidgetList,
		     pScienceDlg->pEndWidgetList->prev, FC_WS_DISABLED);
}

/**************************************************************************
  Update the science report.
**************************************************************************/
void science_dialog_update(void)
{
  if(pScienceDlg && !is_report_dialogs_frozen()) {
    char cBuf[128];
    SDL_String16 *pStr;
    SDL_Surface *pSurf, *pColb_Surface = pIcons->pBIG_Colb;
    int step, i, cost = total_bulbs_required(game.player_ptr);
    SDL_Rect dest, src;
    SDL_Color color;
    struct impr_type *pImpr;
    struct unit_type *pUnit;
    int turns_to_advance, turns_to_next_tech, steps;
    int curent_output = 0;
          
    struct GUI *pWindow = pScienceDlg->pEndWidgetList;
    color = *get_game_colorRGB(COLOR_STD_WHITE);
      
    pWindow->prev->theme = get_tech_icon(game.player_ptr->research.researching);
    pWindow->prev->prev->theme = get_tech_icon(game.player_ptr->ai.tech_goal);
    
    /* redraw Window */
    redraw_group(pWindow, pWindow, 0);
  
    putframe(pWindow->dst, pWindow->size.x, pWindow->size.y,
	  	pWindow->size.x + pWindow->size.w - 1,
		  	pWindow->size.y + pWindow->size.h - 1, 0xffffffff);
  
    redraw_group(pScienceDlg->pBeginWidgetList, pWindow->prev, 0);
    /* ------------------------------------- */

    city_list_iterate(game.player_ptr->cities, pCity) {
      curent_output += pCity->science_total;
    } city_list_iterate_end;

    if (curent_output <= 0) {
      my_snprintf(cBuf, sizeof(cBuf),
		_("Current output : 0\nResearch speed : "
		  "none\nNext's advance time : never"));
    } else {
      char cBiernikujemy[64];
      turns_to_advance = (cost + curent_output - 1) / curent_output;
      turns_to_next_tech =
	    (cost - game.player_ptr->research.bulbs_researched +
		    curent_output - 1) / curent_output;
      
      my_snprintf(cBiernikujemy, sizeof(cBiernikujemy),
                 PL_("Next advance in %d turn",
                     "Next advance in %d turns",
                     turns_to_next_tech), turns_to_next_tech);
      my_snprintf(cBuf, sizeof(cBuf),
		_("Current output : %d per turn\nResearch speed "
		  ": %d %s/advance\n%s"),
	  	  curent_output, turns_to_advance,
		  PL_("turn", "turns", turns_to_advance), cBiernikujemy);
    }

    pStr = create_str16_from_char(cBuf, 12);
    pStr->style |= SF_CENTER;
    pStr->fgcol = color;
  
    pSurf = create_text_surf_from_str16(pStr);
      
    dest.x = pWindow->size.x + (pWindow->size.w - pSurf->w) / 2;
    dest.y = pWindow->size.y + WINDOW_TILE_HIGH + 2;
    SDL_BlitSurface(pSurf, NULL, pWindow->dst, &dest);

    dest.y += pSurf->h + 2;
    FREESURFACE(pSurf);

    /* ------------------------------------- */
    dest.x = pWindow->prev->size.x;
    /* separator */
    putline(pWindow->dst, dest.x, dest.y, dest.x + 365, dest.y, 0xff000000);

    dest.y += 6;
    /* ------------------------------------- */

    my_snprintf(cBuf, sizeof(cBuf), "%s (%d/%d)",
	      get_tech_name(game.player_ptr,
			    game.player_ptr->research.researching),
	      game.player_ptr->research.bulbs_researched, cost);

    copy_chars_to_string16(pStr, cBuf);
    
    pSurf = create_text_surf_from_str16(pStr);
    
    dest.x = pWindow->prev->size.x + pWindow->prev->size.w + 10;
    SDL_BlitSurface(pSurf, NULL, pWindow->dst, &dest);

    dest.y += pSurf->h;
    FREESURFACE(pSurf);

    dest.w = cost * pColb_Surface->w;
    step = pColb_Surface->w;
    if (dest.w > 300) {
      dest.w = 300;
      step = (300 - pColb_Surface->w) / (cost - 1);

      if (step == 0) {
        step = 1;
      }

    }

    dest.h = pColb_Surface->h + 4;
    color.unused = 136;
    SDL_FillRectAlpha(pWindow->dst, &dest, &color);
  
    putframe(pWindow->dst, dest.x - 1, dest.y - 1, dest.x + dest.w,
  	dest.y + dest.h, 0xff000000);
  
    if (cost > 286)
    {
      cost =
        286.0 * ((float) game.player_ptr->research.bulbs_researched / cost);
    }
    else
    {
      cost =
        (float)cost * ((float)game.player_ptr->research.bulbs_researched/cost);
    }
  
    dest.y += 2;
    for (i = 0; i < cost; i++) {
      SDL_BlitSurface(pColb_Surface, NULL, pWindow->dst, &dest);
      dest.x += step;
    }

    /* ----------------------- */

    dest.y += dest.h + 4;
    dest.x = pWindow->prev->size.x + pWindow->prev->size.w + 10;

    impr_type_iterate(imp) {
      pImpr = get_improvement_type(imp);
      if (pImpr->tech_req == game.player_ptr->research.researching) {
        SDL_BlitSurface(GET_SURF(pImpr->sprite), NULL, pWindow->dst, &dest);
        dest.x += GET_SURF(pImpr->sprite)->w + 1;
      }
    } impr_type_iterate_end;

    dest.x += 5;

    unit_type_iterate(un) {
      pUnit = get_unit_type(un);
      if (pUnit->tech_requirement == game.player_ptr->research.researching) {
	if (GET_SURF(pUnit->sprite)->w > 64) {
	  float zoom = 64.0 / GET_SURF(pUnit->sprite)->w;
	  SDL_Surface *pZoomed =
	  	ZoomSurface(GET_SURF(pUnit->sprite), zoom, zoom, 1);
	  src = get_smaller_surface_rect(pZoomed);
	  SDL_BlitSurface(pZoomed, &src, pWindow->dst, &dest);
	  FREESURFACE(pZoomed);
          dest.x += src.w + 2;
	} else {
          src = get_smaller_surface_rect(GET_SURF(pUnit->sprite));
          SDL_BlitSurface(GET_SURF(pUnit->sprite), &src, pWindow->dst, &dest);
          dest.x += src.w + 2;
	}
      }
    } unit_type_iterate_end;
  
    /* -------------------------------- */
    /* draw separator line */
    dest.x = pWindow->prev->size.x;
    dest.y = pWindow->prev->size.y + pWindow->prev->size.h + 35;

    putline(pWindow->dst, dest.x, dest.y, dest.x + 365, dest.y, 0xff000000);
    dest.y += 10;
    /* -------------------------------- */
    /* Goals */
    if (game.player_ptr->ai.tech_goal != A_UNSET)
    {
      steps =
        num_unknown_techs_for_goal(game.player_ptr,
				 game.player_ptr->ai.tech_goal);
      my_snprintf(cBuf, sizeof(cBuf), "%s ( %d %s )",
	      get_tech_name(game.player_ptr,
			    game.player_ptr->ai.tech_goal), steps,
	      PL_("step", "steps", steps));

      copy_chars_to_string16(pStr, cBuf);

      pSurf = create_text_surf_from_str16(pStr);
      
      dest.x = pWindow->prev->size.x + pWindow->prev->size.w + 10;
      SDL_BlitSurface(pSurf, NULL, pWindow->dst, &dest);

      dest.y += pSurf->h + 4;
      FREESURFACE(pSurf);

      impr_type_iterate(imp) {
        pImpr = get_improvement_type(imp);
        if (pImpr->tech_req == game.player_ptr->ai.tech_goal) {
          SDL_BlitSurface(GET_SURF(pImpr->sprite), NULL, pWindow->dst, &dest);
          dest.x += GET_SURF(pImpr->sprite)->w + 1;
        }
      } impr_type_iterate_end;

      dest.x += 5;

      unit_type_iterate(un) {
        pUnit = get_unit_type(un);
        if (pUnit->tech_requirement == game.player_ptr->ai.tech_goal) {
	  if (GET_SURF(pUnit->sprite)->w > 64) {
	    float zoom = 64.0 / GET_SURF(pUnit->sprite)->w;
	    SDL_Surface *pZoomed =
	  	ZoomSurface(GET_SURF(pUnit->sprite), zoom, zoom, 1);
	    src = get_smaller_surface_rect(pZoomed);
	    SDL_BlitSurface(pZoomed, &src, pWindow->dst, &dest);
	    FREESURFACE(pZoomed);
            dest.x += src.w + 2;
	  } else {
            src = get_smaller_surface_rect(GET_SURF(pUnit->sprite));
            SDL_BlitSurface(GET_SURF(pUnit->sprite), &src, pWindow->dst, &dest);
            dest.x += src.w + 2;
	  }
        }
      } unit_type_iterate_end;
    }
  
    /* -------------------------------- */
    sdl_dirty_rect(pWindow->size);
    flush_dirty();
  
    FREESTRING16(pStr);
  }
}

/**************************************************************************
  ...
**************************************************************************/
static int popdown_science_dialog(struct GUI *pButton)
{
  if(pScienceDlg) {
    popdown_window_group_dialog(pScienceDlg->pBeginWidgetList,
				  pScienceDlg->pEndWidgetList);
    FREE(pScienceDlg);
    set_wstate(get_research_widget(), FC_WS_NORMAL);
    redraw_icon2(get_research_widget());
    sdl_dirty_rect(get_research_widget()->size);
    flush_dirty();
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int exit_change_tech_dlg_callback(struct GUI *pWidget)
{
  if (pChangeTechDlg) {
    popdown_window_group_dialog(pChangeTechDlg->pBeginWidgetList, 
  				pChangeTechDlg->pEndWidgetList);
    FREE(pChangeTechDlg->pScroll);
    FREE(pChangeTechDlg);
    enable_science_dialog();
    if (pWidget) {
      flush_dirty();
    }
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int change_research_callback(struct GUI *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_RIGHT)
  {
    popup_tech_info((MAX_ID - pWidget->ID));
  } else {
    dsend_packet_player_research(&aconnection, (MAX_ID - pWidget->ID));
    exit_change_tech_dlg_callback(NULL);
  }
  return -1;
}

/**************************************************************************
  This function is used by change research and change goals dlgs.
**************************************************************************/
static int change_research_goal_dialog_callback(struct GUI *pWindow)
{
  if(sellect_window_group_dialog(pChangeTechDlg->pBeginWidgetList, pWindow)) {
      flush_rect(pWindow->size);
  }      
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int change_research(struct GUI *pWidget)
{
  struct GUI *pBuf = NULL;
  struct GUI *pWindow;
  SDL_String16 *pStr;
  SDL_Surface *pSurf;
  int max_col, max_row, col, i, count = 0, w = 0, h;

  set_wstate(pWidget, FC_WS_NORMAL);
  pSellected_Widget = NULL;
  redraw_icon2(pWidget);
  flush_rect(pWidget->size);
    
  if (is_future_tech(game.player_ptr->research.researching)) {
    return -1;
  }
    
  for (i = A_FIRST; i < game.num_tech_types; i++) {
    if (!tech_is_available(game.player_ptr, i)
       || get_invention(game.player_ptr, i) != TECH_REACHABLE) {
      continue;
    }
    count++;
  }
  
  if (count < 2) {
    return -1;
  }
  
  pChangeTechDlg = MALLOC(sizeof(struct ADVANCED_DLG));
  
  pStr = create_str16_from_char(_("What should we focus on now?"), 12);
  pStr->style |= TTF_STYLE_BOLD;

  pWindow = create_window(NULL, pStr, 40, 30, 0);
  pChangeTechDlg->pEndWidgetList = pWindow;
  w = MAX(w, pWindow->size.w);
  set_wstate(pWindow, FC_WS_NORMAL);
  pWindow->action = change_research_goal_dialog_callback;
  
  add_to_gui_list(ID_SCIENCE_DLG_CHANGE_REASARCH_WINDOW, pWindow);
  /* ------------------------- */
    /* exit button */
  pBuf = create_themeicon(pTheme->Small_CANCEL_Icon, pWindow->dst,
  			  			WF_DRAW_THEME_TRANSPARENT);
  w += pBuf->size.w + 10;
  pBuf->action = exit_change_tech_dlg_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  
  add_to_gui_list(ID_TERRAIN_ADV_DLG_EXIT_BUTTON, pBuf);

  /* ------------------------- */
  /* max col - 104 is sellect tech widget width */
  max_col = (Main.screen->w - DOUBLE_FRAME_WH - 2) / 104;
  /* max row - 204 is sellect tech widget height */
  max_row = (Main.screen->h - (WINDOW_TILE_HIGH + 1 + 2 + FRAME_WH)) / 204;
  
  /* make space on screen for scrollbar */
  if (max_col * max_row < count) {
    max_col--;
  }

  if (count < max_col + 1) {
    col = count;
  } else {
    if (count < max_col + 3) {
      col = max_col - 2;
    } else {
      if (count < max_col + 5) {
        col = max_col - 1;
      } else {
        col = max_col;
      }
    }
  }
  
  pStr = create_string16(NULL, 0, 10);
  pStr->style |= (TTF_STYLE_BOLD | SF_CENTER);
  
  count = 0;
  h = col * max_row;
  for (i = A_FIRST; i < game.num_tech_types; i++) {
    if (!tech_is_available(game.player_ptr, i)
       || get_invention(game.player_ptr, i) != TECH_REACHABLE) {
      continue;
    }
    
    count++;  
    copy_chars_to_string16(pStr, advances[i].name);
    pSurf = create_sellect_tech_icon(pStr, i, MED_MODE);
    pBuf = create_icon2(pSurf, pWindow->dst,
      		WF_FREE_THEME | WF_DRAW_THEME_TRANSPARENT);

    set_wstate(pBuf, FC_WS_NORMAL);
    pBuf->action = change_research_callback;

    add_to_gui_list(MAX_ID - i, pBuf);
    
    if (count > h) {
      set_wflag(pBuf, WF_HIDDEN);
    }
    
  }
  
  FREESTRING16(pStr);
  pChangeTechDlg->pBeginWidgetList = pBuf;
  pChangeTechDlg->pBeginActiveWidgetList = pBuf;
  pChangeTechDlg->pEndActiveWidgetList = pWindow->prev->prev;
  
  /* -------------------------------------------------------------- */
  
  i = 0;
  if (count > col) {
    count = (count + (col - 1)) / col;
    if (count > max_row) {
      pChangeTechDlg->pActiveWidgetList = pWindow->prev->prev;
      count = max_row;
      i = create_vertical_scrollbar(pChangeTechDlg, col, count, TRUE, TRUE);  
    }
  } else {
    count = 1;
  }

  w = MAX(w, (col * pBuf->size.w + 2 + DOUBLE_FRAME_WH + i));
  h = WINDOW_TILE_HIGH + 1 + count * pBuf->size.h + 2 + FRAME_WH;
  pWindow->size.x = (Main.screen->w - w) / 2;
  pWindow->size.y = (Main.screen->h - h) / 2;
  disable_science_dialog();
  
  /* alloca window theme and win background buffer */
  pSurf = get_logo_gfx();
  resize_window(pWindow, pSurf, NULL, w, h);
  FREESURFACE(pSurf);

    /* exit button */
  pBuf = pWindow->prev;
  pBuf->size.x = pWindow->size.x + pWindow->size.w-pBuf->size.w-FRAME_WH-1;
  pBuf->size.y = pWindow->size.y + 1;
  
  setup_vertical_widgets_position(col, pWindow->size.x + FRAME_WH + 1,
		  pWindow->size.y + WINDOW_TILE_HIGH + 1, 0, 0,
		  pChangeTechDlg->pBeginActiveWidgetList,
  		  pChangeTechDlg->pEndActiveWidgetList);
    
  if(pChangeTechDlg->pScroll) {
    setup_vertical_scrollbar_area(pChangeTechDlg->pScroll,
	pWindow->size.x + pWindow->size.w - FRAME_WH,
    	pWindow->size.y + WINDOW_TILE_HIGH + 1,
    	pWindow->size.h - (FRAME_WH + WINDOW_TILE_HIGH + 1), TRUE);
  }

  redraw_group(pChangeTechDlg->pBeginWidgetList, pWindow, FALSE);

  flush_rect(pWindow->size);
  
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int change_research_goal_callback(struct GUI *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_RIGHT)
  {
    popup_tech_info((MAX_ID - pWidget->ID));
  } else {
    dsend_packet_player_tech_goal(&aconnection, (MAX_ID - pWidget->ID));

    exit_change_tech_dlg_callback(NULL);
    
   /* Following is to make the menu go back to the current goal;
   * there may be a better way to do this?  --dwp */
    science_dialog_update();
  } 
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int change_research_goal(struct GUI *pWidget)
{
  struct GUI *pBuf = NULL;
  struct GUI *pWindow;
  SDL_String16 *pStr;
  SDL_Surface *pSurf;
  char cBuf[128];
  int max_col, max_row, col, i, count = 0, w = 0, h , num;

  set_wstate(pWidget, FC_WS_NORMAL);
  pSellected_Widget = NULL;
  redraw_icon2(pWidget);
  flush_rect(pWidget->size);
      
  /* collect all techs which are reachable in under 11 steps
   * hist will hold afterwards the techid of the current choice
   */
  for (i = A_FIRST; i < game.num_tech_types; i++) {
    if (tech_is_available(game.player_ptr, i)
        && get_invention(game.player_ptr, i) != TECH_KNOWN
        && advances[i].req[0] != A_LAST && advances[i].req[1] != A_LAST
	&& (num_unknown_techs_for_goal(game.player_ptr, i) < 11
	    || i == game.player_ptr->ai.tech_goal)) {
      count++;
    }
  }
  
  if (count < 1) {
    return -1;
  }
  
  pChangeTechDlg = MALLOC(sizeof(struct ADVANCED_DLG));
  
  pStr = create_str16_from_char(_("Sellect target :"), 12);
  pStr->style |= TTF_STYLE_BOLD;

  pWindow = create_window(NULL, pStr, 40, 30, 0);
  pChangeTechDlg->pEndWidgetList = pWindow;
  w = MAX(w, pWindow->size.w);
  set_wstate(pWindow, FC_WS_NORMAL);
  pWindow->action = change_research_goal_dialog_callback;
  
  add_to_gui_list(ID_SCIENCE_DLG_CHANGE_GOAL_WINDOW, pWindow);
  /* ------------------------- */
    /* exit button */
  pBuf = create_themeicon(pTheme->Small_CANCEL_Icon, pWindow->dst,
  			  			WF_DRAW_THEME_TRANSPARENT);
  w += pBuf->size.w + 10;
  pBuf->action = exit_change_tech_dlg_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  
  add_to_gui_list(ID_SCIENCE_DLG_CHANGE_GOAL_CANCEL_BUTTON, pBuf);

  /* ------------------------- */
  /* max col - 104 is goal tech widget width */
  max_col = (Main.screen->w - DOUBLE_FRAME_WH - 2) / 104;
  /* max row - 204 is goal tech widget height */
  max_row = (Main.screen->h - (WINDOW_TILE_HIGH + 1 + 2 + FRAME_WH)) / 204;
  
  /* make space on screen for scrollbar */
  if (max_col * max_row < count) {
    max_col--;
  }
  
  if (count < max_col + 1) {
    col = count;
  } else {
    if (count < max_col + 3) {
      col = max_col - 2;
    } else {
      if (count < max_col + 5) {
        col = max_col - 1;
      } else {
        col = max_col;
      }
    }
  }
  
  pStr = create_string16(NULL, 0, 10);
  pStr->style |= (TTF_STYLE_BOLD | SF_CENTER);
  
  /* collect all techs which are reachable in under 11 steps
   * hist will hold afterwards the techid of the current choice
   */
  count = 0;
  h = col * max_row;
  for (i = A_FIRST; i < game.num_tech_types; i++) {
    if (tech_is_available(game.player_ptr, i)
        && get_invention(game.player_ptr, i) != TECH_KNOWN
        && advances[i].req[0] != A_LAST && advances[i].req[1] != A_LAST
	&& ((num = num_unknown_techs_for_goal(game.player_ptr, i)) < 11
	    || i == game.player_ptr->ai.tech_goal)) {
    
      count++;
      my_snprintf(cBuf, sizeof(cBuf), "%s\n%d %s", advances[i].name, num,
	  					PL_("step", "steps", num));
      copy_chars_to_string16(pStr, cBuf);
      pSurf = create_sellect_tech_icon(pStr, i, FULL_MODE);
      pBuf = create_icon2(pSurf, pWindow->dst,
      		WF_FREE_THEME | WF_DRAW_THEME_TRANSPARENT);

      set_wstate(pBuf, FC_WS_NORMAL);
      pBuf->action = change_research_goal_callback;

      add_to_gui_list(MAX_ID - i, pBuf);
    
      if (count > h) {
        set_wflag(pBuf, WF_HIDDEN);
      }
    }
  }
  
  FREESTRING16(pStr);
  pChangeTechDlg->pBeginWidgetList = pBuf;
  pChangeTechDlg->pBeginActiveWidgetList = pBuf;
  pChangeTechDlg->pEndActiveWidgetList = pWindow->prev->prev;
  
  /* -------------------------------------------------------------- */
  
  i = 0;
  if (count > col) {
    count = (count + (col-1)) / col;
    if (count > max_row) {
      pChangeTechDlg->pActiveWidgetList = pWindow->prev->prev;
      count = max_row;
      i = create_vertical_scrollbar(pChangeTechDlg, col, count, TRUE, TRUE);  
    }
  } else {
    count = 1;
  }

  w = MAX(w, (col * pBuf->size.w + 2 + DOUBLE_FRAME_WH + i));
  h = WINDOW_TILE_HIGH + 1 + count * pBuf->size.h + 2 + FRAME_WH;
  pWindow->size.x = (Main.screen->w - w) / 2;
  pWindow->size.y = (Main.screen->h - h) / 2;
  disable_science_dialog();
  
  /* alloca window theme and win background buffer */
  pSurf = get_logo_gfx();
  resize_window(pWindow, pSurf, NULL, w, h);
  FREESURFACE(pSurf);

    /* exit button */
  pBuf = pWindow->prev;
  pBuf->size.x = pWindow->size.x + pWindow->size.w-pBuf->size.w-FRAME_WH-1;
  pBuf->size.y = pWindow->size.y + 1;
  
  setup_vertical_widgets_position(col, pWindow->size.x + FRAME_WH + 1,
		  pWindow->size.y + WINDOW_TILE_HIGH + 1, 0, 0,
		  pChangeTechDlg->pBeginActiveWidgetList,
  		  pChangeTechDlg->pEndActiveWidgetList);
    
  if(pChangeTechDlg->pScroll) {
    setup_vertical_scrollbar_area(pChangeTechDlg->pScroll,
	pWindow->size.x + pWindow->size.w - FRAME_WH,
    	pWindow->size.y + WINDOW_TILE_HIGH + 1,
    	pWindow->size.h - (FRAME_WH + WINDOW_TILE_HIGH + 1), TRUE);
  }

  redraw_group(pChangeTechDlg->pBeginWidgetList, pWindow, FALSE);

  flush_rect(pWindow->size);
  
  return -1;
}

static int science_dialog_callback(struct GUI *pWindow)
{
  if (!pChangeTechDlg &&
	    move_window_group_dialog(pScienceDlg->pBeginWidgetList, pWindow)) {
    sellect_window_group_dialog(pScienceDlg->pBeginWidgetList, pWindow);
    science_dialog_update();
  } else {
    if(!pChangeTechDlg &&
      	sellect_window_group_dialog(pScienceDlg->pBeginWidgetList, pWindow)) {
      flush_rect(pWindow->size);
    }      
  }
  return -1;
}

/**************************************************************************
  Popup (or raise) the science report(F6).  It may or may not be modal.
**************************************************************************/
void popup_science_dialog(bool make_modal)
{
  struct GUI *pBuf = get_research_widget(), *pWindow = NULL;
  SDL_String16 *pStr;
  SDL_Surface *pLogo;
  int count, i;
  
  if (pScienceDlg) {
    return;
  }

  set_wstate(pBuf, FC_WS_DISABLED);
  redraw_icon2(pBuf);
  sdl_dirty_rect(pBuf->size);
  
  pScienceDlg = MALLOC(sizeof(struct SMALL_DLG));
    
  pStr = create_str16_from_char(_("Science"), 12);
  pStr->style |= TTF_STYLE_BOLD;
  
  pWindow = create_window(NULL, pStr, 400, 260, 0);
  pScienceDlg->pEndWidgetList = pWindow;

  clear_wflag(pWindow, WF_DRAW_FRAME_AROUND_WIDGET);
  pWindow->action = science_dialog_callback;
  pWindow->size.x = (Main.screen->w - 400) / 2;
  pWindow->size.y = (Main.screen->h - 260) / 2;
  pWindow->size.w = 400;
  pWindow->size.h = 260;
  set_wstate(pWindow, FC_WS_NORMAL);
  
  pLogo = get_logo_gfx();
  pWindow->theme = ResizeSurface(pLogo, pWindow->size.w, pWindow->size.h, 1);
  FREESURFACE(pLogo);
    
  refresh_widget_background(pWindow);

  add_to_gui_list(ID_SCIENCE_DLG_WINDOW, pWindow);
  /* ------ */

  count = 0;
  for (i = A_FIRST; i < game.num_tech_types; i++) {
    if (tech_is_available(game.player_ptr, i)
        && get_invention(game.player_ptr, i) != TECH_KNOWN
        && advances[i].req[0] != A_LAST && advances[i].req[1] != A_LAST) {
	count++;	  
    }
  }

  if(game.player_ptr->research.researching != A_FUTURE) {
    pLogo = GET_SURF(advances[game.player_ptr->research.researching].sprite);
  } else {
    /* "Future Tech" icon */
    pLogo = pFuture_Tech_Icon;
  }

  pBuf = create_icon2(pLogo, pWindow->dst, WF_DRAW_THEME_TRANSPARENT);

  pBuf->action = change_research;
  if(count) {
    set_wstate(pBuf, FC_WS_NORMAL);
  }
  
  pBuf->size.x = pWindow->size.x + 16;
  pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 60;

  add_to_gui_list(ID_SCIENCE_DLG_CHANGE_REASARCH_BUTTON, pBuf);

  /* ------ */
  if (game.player_ptr->ai.tech_goal != A_UNSET)
  {
    pLogo = GET_SURF(advances[game.player_ptr->ai.tech_goal].sprite);
  } else {
    /* "None" icon */
    pLogo = pNone_Tech_Icon;
  }
  
  pBuf = create_icon2(pLogo, pWindow->dst, WF_DRAW_THEME_TRANSPARENT);
  pBuf->action = change_research_goal;
  if(count) {
    set_wstate(pBuf, FC_WS_NORMAL);
  }

  pBuf->size.x = pWindow->size.x + 16;
  pBuf->size.y =
      pWindow->size.y + WINDOW_TILE_HIGH + 60 + pBuf->size.h + 45;

  add_to_gui_list(ID_SCIENCE_DLG_CHANGE_GOAL_BUTTON, pBuf);

  /* ------ */
  /* exit button */
  pBuf = create_themeicon(pTheme->Small_CANCEL_Icon, pWindow->dst,
  			  			WF_DRAW_THEME_TRANSPARENT);
  
  pBuf->action = popdown_science_dialog;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  
  add_to_gui_list(ID_SCIENCE_CANCEL_DLG_BUTTON, pBuf);
  
  pBuf->size.x = pWindow->size.x + pWindow->size.w-pBuf->size.w-FRAME_WH-1;
  pBuf->size.y = pWindow->size.y + 1;
    
  /* ======================== */
  pScienceDlg->pBeginWidgetList = pBuf;

  science_dialog_update();
}

/**************************************************************************
  Popdow all the science reports (report, chnge tech, change goals).
**************************************************************************/
void popdown_all_science_dialogs(void)
{
  if(pChangeTechDlg) {
    lock_buffer(pChangeTechDlg->pEndWidgetList->dst);
    popdown_window_group_dialog(pChangeTechDlg->pBeginWidgetList,
				  pChangeTechDlg->pEndWidgetList);
    unlock_buffer();
    FREE(pChangeTechDlg->pScroll);
    FREE(pChangeTechDlg);
  }
  if(pScienceDlg) {
    popdown_window_group_dialog(pScienceDlg->pBeginWidgetList,
				  pScienceDlg->pEndWidgetList);
    FREE(pScienceDlg);
    set_wstate(get_research_widget(), FC_WS_NORMAL);
    redraw_icon2(get_research_widget());
    sdl_dirty_rect(get_research_widget()->size);
  }  
}
  
/**************************************************************************
  Update all report dialogs.
**************************************************************************/
void update_report_dialogs(void)
{
  economy_report_dialog_update();
  activeunits_report_dialog_update();
  if(pScienceDlg) {
    science_dialog_update();
  }
}

/* ===================================================================== */
/* ======================== Endgame Report ============================= */
/* ===================================================================== */

/****************************************************************
  Show a dialog with player statistics at endgame.
  TODO: Display all statistics in packet_endgame_report.
*****************************************************************/
void popup_endgame_report_dialog(struct packet_endgame_report *packet)
{
  char buffer[150 * MAX_NUM_PLAYERS];
  int i;
 
  buffer[0] = '\0';
  for (i = 0; i < packet->nscores; i++) {
    cat_snprintf(buffer, sizeof(buffer),
                 PL_("%2d: The %s ruler %s scored %d point\n",
                     "%2d: The %s ruler %s scored %d points\n",
                     packet->score[i]),
                 i + 1,
                 get_nation_name(get_player(packet->id[i])->nation),
                 get_player(packet->id[i])->name,
                 packet->score[i]);
  }
  popup_notify_dialog(_("Final Report:"),
                      _("The Greatest Civilizations in the world."),
                      buffer);
}

/*************************************************************************
  Server options dialog
*************************************************************************/
void popup_settable_options_dialog(void)
{
  /* PORT ME */
}
