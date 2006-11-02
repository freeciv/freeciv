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

/* common */
#include "game.h"
#include "government.h"

/* gui-sdl */
#include "graphics.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_tilespec.h"
#include "gui_zoom.h"
#include "mapview.h"
#include "repodlgs.h"
#include "spaceshipdlg.h"
#include "widget.h"

#include "inteldlg.h"

struct intel_dialog {
  struct player *pplayer;
  struct ADVANCED_DLG *pdialog;
  int pos_x, pos_y;
};

#define SPECLIST_TAG dialog
#define SPECLIST_TYPE struct intel_dialog
#include "speclist.h"

#define dialog_list_iterate(dialoglist, pdialog) \
    TYPED_LIST_ITERATE(struct intel_dialog, dialoglist, pdialog)
#define dialog_list_iterate_end  LIST_ITERATE_END

static struct dialog_list *dialog_list;
static struct intel_dialog *create_intel_dialog(struct player *p);

/****************************************************************
...
*****************************************************************/
void intel_dialog_init()
{
  dialog_list = dialog_list_new();
}

/****************************************************************
...
*****************************************************************/
void intel_dialog_done()
{
  dialog_list_free(dialog_list);
}

/****************************************************************
...
*****************************************************************/
static struct intel_dialog *get_intel_dialog(struct player *pplayer)
{
  dialog_list_iterate(dialog_list, pdialog) {
    if (pdialog->pplayer == pplayer) {
      return pdialog;
    }
  } dialog_list_iterate_end;

  return NULL;
}

/****************************************************************
...
*****************************************************************/
static int intel_window_dlg_callback(struct widget *pWindow)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    struct intel_dialog *pSelectedDialog = get_intel_dialog(pWindow->data.player);
  
    move_window_group(pSelectedDialog->pdialog->pBeginWidgetList, pWindow);
  }
  return -1;
}

static int tech_callback(struct widget *pWidget)
{
  /* get tech help - PORT ME */
  return -1;
}

static int spaceship_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    struct player *pPlayer = pWidget->data.player;
    popdown_intel_dialog(pPlayer);
    popup_spaceship_dialog(pPlayer);
  }
  return -1;
}


static int exit_intel_dlg_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    popdown_intel_dialog(pWidget->data.player);
    flush_dirty();
  }
  return -1;
}

static struct intel_dialog *create_intel_dialog(struct player *pPlayer) {

  struct intel_dialog *pdialog = fc_calloc(1, sizeof(struct intel_dialog));

  pdialog->pplayer = pPlayer;
  
  pdialog->pdialog = fc_calloc(1, sizeof(struct ADVANCED_DLG));
      
  pdialog->pos_x = 0;
  pdialog->pos_y = 0;
    
  dialog_list_prepend(dialog_list, pdialog);  
  
  return pdialog;
}


/**************************************************************************
  Popup an intelligence dialog for the given player.
**************************************************************************/
void popup_intel_dialog(struct player *p)
{
  struct intel_dialog *pdialog;

  if (!(pdialog = get_intel_dialog(p))) {
    pdialog = create_intel_dialog(p);
  } else {
    /* bring existing dialog to front */
    sellect_window_group_dialog(pdialog->pdialog->pBeginWidgetList,
                                         pdialog->pdialog->pEndWidgetList);
  }

  update_intel_dialog(p);
}

/**************************************************************************
  Popdown an intelligence dialog for the given player.
**************************************************************************/
void popdown_intel_dialog(struct player *p)
{
  struct intel_dialog *pdialog = get_intel_dialog(p);
    
  if (pdialog) {
    popdown_window_group_dialog(pdialog->pdialog->pBeginWidgetList,
			                  pdialog->pdialog->pEndWidgetList);
      
    dialog_list_unlink(dialog_list, pdialog);
      
    FC_FREE(pdialog->pdialog->pScroll);
    FC_FREE(pdialog->pdialog);  
    FC_FREE(pdialog);
  }
}

/**************************************************************************
  Popdown all intelligence dialogs
**************************************************************************/
void popdown_intel_dialogs() {
  dialog_list_iterate(dialog_list, pdialog) {
    popdown_intel_dialog(pdialog->pplayer);
  } dialog_list_iterate_end;
}

/****************************************************************************
  Update the intelligence dialog for the given player.  This is called by
  the core client code when that player's information changes.
****************************************************************************/
void update_intel_dialog(struct player *p)
{
  struct intel_dialog *pdialog = get_intel_dialog(p);
      
  struct widget *pWindow = NULL, *pBuf = NULL, *pLast;
  SDL_Surface *pLogo = NULL;
  SDL_Surface *pText1, *pInfo, *pText2 = NULL;
  SDL_String16 *pStr;
  SDL_Rect dst;
  char cBuf[256];
  int i, n = 0, w = 0, h, count = 0, col;
  struct city *pCapital;
      
  if (pdialog) {

    /* save window position and delete old content */
    if (pdialog->pdialog->pEndWidgetList) {
      pdialog->pos_x = pdialog->pdialog->pEndWidgetList->size.x;
      pdialog->pos_y = pdialog->pdialog->pEndWidgetList->size.y;
        
      popdown_window_group_dialog(pdialog->pdialog->pBeginWidgetList,
                                            pdialog->pdialog->pEndWidgetList);
    }
        
    h = WINDOW_TITLE_HEIGHT + adj_size(3) + pTheme->FR_Bottom->h;
  
    pStr = create_str16_from_char(_("Foreign Intelligence Report") , adj_font(12));
    pStr->style |= TTF_STYLE_BOLD;
    
    pWindow = create_window(NULL, pStr, 1, 1, 0);
      
    pWindow->action = intel_window_dlg_callback;
    set_wstate(pWindow , FC_WS_NORMAL);
    pWindow->data.player = p;
    w = MAX(w , pWindow->size.w);
    
    add_to_gui_list(ID_WINDOW, pWindow);
    pdialog->pdialog->pEndWidgetList = pWindow;
    /* ---------- */
    /* exit button */
    pBuf = create_themeicon(pTheme->Small_CANCEL_Icon, pWindow->dst,
                                                  WF_RESTORE_BACKGROUND);
    w += pBuf->size.w + adj_size(10);
    pBuf->action = exit_intel_dlg_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
    pBuf->data.player = p;  
    pBuf->key = SDLK_ESCAPE;
    
    add_to_gui_list(ID_BUTTON, pBuf);
    /* ---------- */
    
    pLogo = GET_SURF(get_nation_flag_sprite(tileset, p->nation));
    pText1 = ZoomSurface(pLogo, 4.0 , 4.0, 1);
    pLogo = pText1;
          
    pBuf = create_icon2(pLogo, pWindow->dst,
          (WF_RESTORE_BACKGROUND|WF_WIDGET_HAS_INFO_LABEL|
                                          WF_FREE_STRING|WF_FREE_THEME));
    pBuf->action = spaceship_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
    pBuf->data.player = p;
    my_snprintf(cBuf, sizeof(cBuf),
                _("Intelligence Information about %s Spaceship"), 
                                          get_nation_name(p->nation));
    pBuf->string16 = create_str16_from_char(cBuf, adj_font(12));
          
    add_to_gui_list(ID_ICON, pBuf);
          
    /* ---------- */
    my_snprintf(cBuf, sizeof(cBuf),
                _("Intelligence Information for the %s Empire"), 
                                          get_nation_name(p->nation));
    
    pStr = create_str16_from_char(cBuf, adj_font(14));
    pStr->style |= TTF_STYLE_BOLD;
    pStr->bgcol = (SDL_Color) {0, 0, 0, 0};
    
    pText1 = create_text_surf_from_str16(pStr);
    w = MAX(w, pText1->w + adj_size(20));
    h += pText1->h + adj_size(20);
      
    /* ---------- */
    
    pCapital = find_palace(p);
    struct player_research* research = get_player_research(p);
    change_ptsize16(pStr, adj_font(10));
    pStr->style &= ~TTF_STYLE_BOLD;
    if (research->researching != A_NOINFO) {
      my_snprintf(cBuf, sizeof(cBuf),
        _("Ruler: %s %s  Government: %s\nCapital: %s  Gold: %d\nTax: %d%%"
          " Science: %d%% Luxury: %d%%\nResearching: %s(%d/%d)"),
        get_ruler_title(p->government, p->is_male, p->nation),
        p->name, get_government_name(p->government),
        (!pCapital) ? _("(Unknown)") : pCapital->name, p->economic.gold,
        p->economic.tax, p->economic.science, p->economic.luxury,
        get_tech_name(p, research->researching),
        research->bulbs_researched, total_bulbs_required(p));
    } else {
      my_snprintf(cBuf, sizeof(cBuf),
        _("Ruler: %s %s  Government: %s\nCapital: %s  Gold: %d\nTax: %d%%"
          " Science: %d%% Luxury: %d%%\nResearching: Unknown"),
        get_ruler_title(p->government, p->is_male, p->nation),
        p->name, get_government_name(p->government),
        (!pCapital) ? _("(Unknown)") : pCapital->name, p->economic.gold,
        p->economic.tax, p->economic.science, p->economic.luxury);
    }
    
    copy_chars_to_string16(pStr, cBuf);
    pInfo = create_text_surf_from_str16(pStr);
    w = MAX(w, pLogo->w + adj_size(10) + pInfo->w + adj_size(20));
    h += MAX(pLogo->h + adj_size(20), pInfo->h + adj_size(20));
      
    /* ---------- */
    col = w / (get_tech_icon(A_FIRST)->w + adj_size(4));
    n = 0;
    pLast = pBuf;
    for(i = A_FIRST; i<game.control.num_tech_types; i++) {
      if(get_invention(p, i) == TECH_KNOWN &&
        tech_is_available(game.player_ptr, i) &&
        get_invention(game.player_ptr, i) != TECH_KNOWN) {
        
        pBuf = create_icon2(get_tech_icon(i), pWindow->dst,
          (WF_RESTORE_BACKGROUND|WF_WIDGET_HAS_INFO_LABEL|WF_FREE_STRING));
        pBuf->action = tech_callback;
        set_wstate(pBuf, FC_WS_NORMAL);
  
        pBuf->string16 = create_str16_from_char(advances[i].name, adj_font(12));
          
        add_to_gui_list(ID_ICON, pBuf);
          
        if(n > ((2 * col) - 1)) {
          set_wflag(pBuf, WF_HIDDEN);
        }
        
        n++;	
      }
    }
    
    pdialog->pdialog->pBeginWidgetList = pBuf;
    
    if(n) {
      pdialog->pdialog->pBeginActiveWidgetList = pBuf;
      pdialog->pdialog->pEndActiveWidgetList = pLast->prev;
      if(n > 2 * col) {
        pdialog->pdialog->pActiveWidgetList = pLast->prev;
        count = create_vertical_scrollbar(pdialog->pdialog, col, 2, TRUE, TRUE);
        h += (2 * pBuf->size.h + adj_size(10));
      } else {
        count = 0;
        if(n > col) {
          h += pBuf->size.h;
        }
        h += (adj_size(10) + pBuf->size.h);
      }
      
      w = MAX(w, col * pBuf->size.w + count + pTheme->FR_Left->w + pTheme->FR_Right->w);
      
      my_snprintf(cBuf, sizeof(cBuf), _("Their techs that we don't have :"));
      copy_chars_to_string16(pStr, cBuf);
      pStr->style |= TTF_STYLE_BOLD;
      pText2 = create_text_surf_from_str16(pStr);
    }
    
    FREESTRING16(pStr);
    
    /* ------------------------ */  
    widget_set_position(pWindow,
      (pdialog->pos_x) ? (pdialog->pos_x) : ((Main.screen->w - w) / 2),
      (pdialog->pos_y) ? (pdialog->pos_y) : ((Main.screen->h - h) / 2));
    
    resize_window(pWindow, NULL, NULL, w, h);
    
    /* exit button */
    pBuf = pWindow->prev; 
    pBuf->size.x = pWindow->size.x + pWindow->size.w - pBuf->size.w - pTheme->FR_Right->w - 1;
    pBuf->size.y = pWindow->size.y + 1;
    
    dst.x = (pWindow->size.w - pText1->w) / 2;
    dst.y = WINDOW_TITLE_HEIGHT + adj_size(12);
    
    alphablit(pText1, NULL, pWindow->theme, &dst);
    dst.y += pText1->h + adj_size(10);
    FREESURFACE(pText1);
    
    /* space ship button */
    pBuf = pBuf->prev;
    dst.x = (pWindow->size.w - (pBuf->size.w + adj_size(10) + pInfo->w)) / 2;
    pBuf->size.x = pWindow->size.x + dst.x;
    pBuf->size.y = pWindow->size.y + dst.y;
    
    dst.x += pBuf->size.w + adj_size(10);  
    alphablit(pInfo, NULL, pWindow->theme, &dst);
    dst.y += pInfo->h + adj_size(10);
    FREESURFACE(pInfo);
        
    /* --------------------- */
      
    if(n) {
      
      dst.x = pTheme->FR_Left->w + adj_size(5);
      alphablit(pText2, NULL, pWindow->theme, &dst);
      dst.y += pText2->h + adj_size(2);
      FREESURFACE(pText2);
      
      setup_vertical_widgets_position(col,
          pWindow->size.x + pTheme->FR_Left->w,
          pWindow->size.y + dst.y,
            0, 0, pdialog->pdialog->pBeginActiveWidgetList,
                            pdialog->pdialog->pEndActiveWidgetList);
      
      if(pdialog->pdialog->pScroll) {
        setup_vertical_scrollbar_area(pdialog->pdialog->pScroll,
          pWindow->size.x + pWindow->size.w - pTheme->FR_Right->w,
          pWindow->size.y + dst.y,
          pWindow->size.h - (dst.y + pTheme->FR_Bottom->h + 1), TRUE);
      }
    }

    redraw_group(pdialog->pdialog->pBeginWidgetList, pdialog->pdialog->pEndWidgetList, 0);
    widget_mark_dirty(pWindow);
  
    flush_dirty();
    
  }
}
