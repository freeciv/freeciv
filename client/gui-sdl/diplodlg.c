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

#include <stdlib.h>
#include <SDL/SDL.h>

#include "fcintl.h"

#include "game.h"

#include "gui_mem.h"
#include "player.h"
#include "shared.h"
#include "support.h"

#include "chatline.h"
#include "climisc.h"
#include "clinet.h"
#include "civclient.h"
#include "diptreaty.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_id.h"
#include "gui_tilespec.h"
#include "gui_string.h"
#include "gui_stuff.h"
#include "mapview.h"
#include "colors.h"

#include "diplodlg.h"

#define MAX_NUM_CLAUSES 64

static struct ADVANCED_DLG *pWants = NULL;
static struct ADVANCED_DLG *pOffers = NULL;
static struct ADVANCED_DLG *pClauses_Dlg = NULL;

static void popdown_diplomacy_dialog(void);
static void popdown_sdip_dialog(void);

/**************************************************************************
  Update a player's acceptance status of a treaty (traditionally shown
  with the thumbs-up/thumbs-down sprite).
**************************************************************************/
void handle_diplomacy_accept_treaty(int counterpart, bool I_accepted,
				    bool other_accepted)
{
  struct GUI *pLabel;
  SDL_Surface *pThm;
  SDL_Rect src = {0, 0, 0, 0};
  
  /* updates your own acceptance status */
  pLabel = pClauses_Dlg->pEndWidgetList->prev;

  pLabel->private_data.cbox->state = I_accepted;  
  if (pLabel->private_data.cbox->state) {
    pThm = pLabel->private_data.cbox->pTRUE_Theme;
  } else {
    pThm = pLabel->private_data.cbox->pFALSE_Theme;
  }
      
  src.w = pThm->w / 4;
  src.h = pThm->h;
    
  SDL_SetAlpha(pThm, 0x0, 0x0);
  SDL_BlitSurface(pThm, &src, pLabel->theme, NULL);
  SDL_SetAlpha(pThm, SDL_SRCALPHA, 255);
  
  redraw_widget(pLabel);
  flush_rect(pLabel->size);
  
  /* updates other player's acceptance status */
  pLabel = pClauses_Dlg->pEndWidgetList->prev->prev;
  
  pLabel->private_data.cbox->state = other_accepted;  
  if (pLabel->private_data.cbox->state) {
    pThm = pLabel->private_data.cbox->pTRUE_Theme;
  } else {
    pThm = pLabel->private_data.cbox->pFALSE_Theme;
  }
      
  src.w = pThm->w / 4;
  src.h = pThm->h;
    
  SDL_SetAlpha(pThm, 0x0, 0x0);
  SDL_BlitSurface(pThm, &src, pLabel->theme, NULL);
  SDL_SetAlpha(pThm, SDL_SRCALPHA, 255);
  
  redraw_widget(pLabel);
  flush_rect(pLabel->size);
}

/**************************************************************************
  Update the diplomacy dialog when the meeting is canceled (the dialog
  should be closed).
**************************************************************************/
void handle_diplomacy_cancel_meeting(int counterpart, int initiated_from)
{
  popdown_diplomacy_dialog();
  flush_dirty();
}
/* ----------------------------------------------------------------------- */

static int remove_clause_callback(struct GUI *pWidget)
{
  dsend_packet_diplomacy_remove_clause_req(&aconnection,
					   pClauses_Dlg->pEndWidgetList->
					   data.cont->id1,
					   pWidget->data.cont->id0,
					   (enum clause_type) pWidget->data.
					   cont->id1,
					   pWidget->data.cont->value);
  return -1;
}

/**************************************************************************
  Update the diplomacy dialog by adding a clause.
**************************************************************************/
void handle_diplomacy_create_clause(int counterpart, int giver,
				    enum clause_type type, int value)
{
  SDL_String16 *pStr;
  struct GUI *pBuf, *pWindow = pClauses_Dlg->pEndWidgetList;
  char cBuf[64];
  struct Clause Cl;
  bool redraw_all, scroll = pClauses_Dlg->pActiveWidgetList == NULL;
  int len = pClauses_Dlg->pScroll->pUp_Left_Button->size.w;
     
  Cl.type = type;
  Cl.from = &game.players[giver];
  Cl.value = value;
  
  client_diplomacy_clause_string(cBuf, sizeof(cBuf), &Cl);
  
  /* find existing gold clause and update value */
  if(type == CLAUSE_GOLD && pClauses_Dlg->pScroll->count) {
    pBuf = pClauses_Dlg->pEndActiveWidgetList;
    while(pBuf) {
      
      if(pBuf->data.cont->id0 == giver &&
	pBuf->data.cont->id1 == (int)type) {
	if(pBuf->data.cont->value != value) {
	  pBuf->data.cont->value = value;
	  copy_chars_to_string16(pBuf->string16, cBuf);
	  if(redraw_widget(pBuf) == 0) {
	    flush_rect(pBuf->size);
	  }
	}
	return;
      }
      
      if(pBuf == pClauses_Dlg->pBeginActiveWidgetList) {
	break;
      }
      pBuf = pBuf->prev;
    }
  }
  
  pStr = create_str16_from_char(cBuf, 12);
  pBuf = create_iconlabel(NULL, pWindow->dst, pStr,
   (WF_FREE_DATA|WF_DRAW_TEXT_LABEL_WITH_SPACE|WF_DRAW_THEME_TRANSPARENT));
      
  if(giver != game.player_idx) {
     pBuf->string16->style |= SF_CENTER_RIGHT;  
  }

  pBuf->data.cont = MALLOC(sizeof(struct CONTAINER));
  pBuf->data.cont->id0 = giver;
  pBuf->data.cont->id1 = (int)type;
  pBuf->data.cont->value = value;
    
  pBuf->action = remove_clause_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  
  pBuf->size.w = pWindow->size.w - 24 - (scroll ? 0 : len);
  
  redraw_all = add_widget_to_vertical_scroll_widget_list(pClauses_Dlg,
		pBuf, pClauses_Dlg->pBeginWidgetList,
		FALSE,
		pWindow->size.x + 12,
                pClauses_Dlg->pScroll->pUp_Left_Button->size.y + 2);
  
  /* find if there was scrollbar shown */
  if(scroll && pClauses_Dlg->pActiveWidgetList != NULL) {
    pBuf = pClauses_Dlg->pEndActiveWidgetList->next;
    do {
      pBuf = pBuf->prev;
      pBuf->size.w -= len;
      FREESURFACE(pBuf->gfx);
    } while(pBuf != pClauses_Dlg->pBeginActiveWidgetList);
  }
  
  /* redraw */
  if(redraw_all) {
    redraw_group(pClauses_Dlg->pBeginWidgetList, pWindow, 0);
    flush_rect(pWindow->size);
  } else {
    redraw_widget(pBuf);
    flush_rect(pBuf->size);
  }
  
}

/**************************************************************************
  Update the diplomacy dialog by removing a clause.
**************************************************************************/
void handle_diplomacy_remove_clause(int counterpart, int giver,
				    enum clause_type type, int value)
{
  struct GUI *pBuf;
  SDL_Rect src = {0, 0, 0, 0};
  bool scroll, redraw = TRUE;
  
  assert(pClauses_Dlg->pScroll->count > 0);
   
  /* find widget with clause */
  pBuf = pClauses_Dlg->pEndActiveWidgetList->next;
  do {
    pBuf = pBuf->prev;
  } while(!(pBuf->data.cont->id0 == giver &&
            pBuf->data.cont->id1 == (int)type &&
            pBuf->data.cont->value == value) &&
  		pBuf != pClauses_Dlg->pBeginActiveWidgetList);
  
  if(!(pBuf->data.cont->id0 == giver &&
            pBuf->data.cont->id1 == (int)type &&
            pBuf->data.cont->value == value)) {
     return;
  }
    
  scroll = pClauses_Dlg->pActiveWidgetList != NULL;
  del_widget_from_vertical_scroll_widget_list(pClauses_Dlg, pBuf);

  /* find if there was scrollbar hide */
  if(scroll && pClauses_Dlg->pActiveWidgetList == NULL) {
    int len = pClauses_Dlg->pScroll->pUp_Left_Button->size.w;
    pBuf = pClauses_Dlg->pEndActiveWidgetList->next;
    do {
      pBuf = pBuf->prev;
      pBuf->size.w += len;
      FREESURFACE(pBuf->gfx);
    } while(pBuf != pClauses_Dlg->pBeginActiveWidgetList);
    
    redraw = FALSE;
  }
    
  /* update state icons */
  pBuf = pClauses_Dlg->pEndWidgetList->prev;
  if(pBuf->private_data.cbox->state) {
    pBuf->private_data.cbox->state = FALSE;
    src.w = pBuf->private_data.cbox->pFALSE_Theme->w / 4;
    src.h = pBuf->private_data.cbox->pFALSE_Theme->h;
    
    SDL_SetAlpha(pBuf->private_data.cbox->pFALSE_Theme, 0x0, 0x0);
    SDL_BlitSurface(pBuf->private_data.cbox->pFALSE_Theme, &src, pBuf->theme, NULL);
    SDL_SetAlpha(pBuf->private_data.cbox->pFALSE_Theme, SDL_SRCALPHA, 255);
    
    if(redraw) {
      redraw_widget(pBuf);
      sdl_dirty_rect(pBuf->size);
    }
  }
  
  /*
  pBuf = pBuf->prev;
  if(pBuf->private_data.cbox->state) {
    pBuf->private_data.cbox->state = FALSE;
    src.w = pBuf->private_data.cbox->pFALSE_Theme->w / 4;
    src.h = pBuf->private_data.cbox->pFALSE_Theme->h;
    
    SDL_SetAlpha(pBuf->private_data.cbox->pFALSE_Theme, 0x0, 0x0);
    SDL_BlitSurface(pBuf->private_data.cbox->pFALSE_Theme, &src, pBuf->theme, NULL);
    SDL_SetAlpha(pBuf->private_data.cbox->pFALSE_Theme, SDL_SRCALPHA, 255);
  
    if(redraw) {
      redraw_widget(pBuf);
      sdl_dirty_rect(pBuf->size);
    }
  }
   */
  if(redraw) {
    redraw_group(pClauses_Dlg->pBeginActiveWidgetList,
    				pClauses_Dlg->pEndActiveWidgetList, TRUE);
  } else {
    redraw_group(pClauses_Dlg->pBeginWidgetList,
    				pClauses_Dlg->pEndWidgetList, FALSE);
    sdl_dirty_rect(pClauses_Dlg->pEndWidgetList->size);
  }
  
  
  flush_dirty();
}

/* ================================================================= */

static int cancel_meeting_callback(struct GUI *pWidget)
{
  dsend_packet_diplomacy_cancel_meeting_req(&aconnection,
					    pWidget->data.cont->id1);
  return -1;
}

static int accept_treaty_callback(struct GUI *pWidget)
{
  dsend_packet_diplomacy_accept_treaty_req(&aconnection,
					   pWidget->data.cont->id1);
  return -1;
}

/* ------------------------------------------------------------------------ */

static int pact_callback(struct GUI *pWidget)
{
  int clause_type;
  
  switch(MAX_ID - pWidget->ID) {
    case 2:
      clause_type = CLAUSE_CEASEFIRE;
    break;
    case 1:
      clause_type = CLAUSE_PEACE;
    break;
    default:
      clause_type = CLAUSE_ALLIANCE;
    break;
  }
  
  dsend_packet_diplomacy_create_clause_req(&aconnection,
					   pClauses_Dlg->pEndWidgetList->
					   data.cont->id1,
					   pWidget->data.cont->id0,
					   clause_type, 0);
  
  return -1;
}

static int vision_callback(struct GUI *pWidget)
{
  dsend_packet_diplomacy_create_clause_req(&aconnection,
					   pClauses_Dlg->pEndWidgetList->
					   data.cont->id1,
					   pWidget->data.cont->id0,
					   CLAUSE_VISION, 0);
  return -1;
}

static int maps_callback(struct GUI *pWidget)
{
  int clause_type;
  
  switch(MAX_ID - pWidget->ID) {
    case 1:
      clause_type = CLAUSE_MAP;
    break;
    default:
      clause_type = CLAUSE_SEAMAP;
    break;
  }

  dsend_packet_diplomacy_create_clause_req(&aconnection,
					   pClauses_Dlg->pEndWidgetList->
					   data.cont->id1,
					   pWidget->data.cont->id0,
					   clause_type, 0);
  return -1;
}

static int techs_callback(struct GUI *pWidget)
{
  dsend_packet_diplomacy_create_clause_req(&aconnection,
					   pClauses_Dlg->pEndWidgetList->
					   data.cont->id1,
					   pWidget->data.cont->id0,
					   CLAUSE_ADVANCE,
					   (MAX_ID - pWidget->ID));
  
  return -1;
}

static int gold_callback(struct GUI *pWidget)
{
  int amount;
  
  if(pWidget->string16->text) {
    char cBuf[16];
    
    convertcopy_to_chars(cBuf, sizeof(cBuf), pWidget->string16->text);
    sscanf(cBuf, "%d", &amount);
    
    if(amount > pWidget->data.cont->value) {
      /* max player gold */
      amount = pWidget->data.cont->value;
    }
    
  } else {
    amount = 0;
  }
  
  if (amount > 0) {
    dsend_packet_diplomacy_create_clause_req(&aconnection,
					     pClauses_Dlg->pEndWidgetList->
					     data.cont->id1,
					     pWidget->data.cont->id0,
					     CLAUSE_GOLD, amount);
    
  } else {
    append_output_window(_("Game: Invalid amount of gold specified."));
  }
  
  if(amount || !pWidget->string16->text) {
    copy_chars_to_string16(pWidget->string16, "0");
    redraw_widget(pWidget);
    flush_rect(pWidget->size);
  }
  
  return -1;
}


static int cities_callback(struct GUI *pWidget)
{
  dsend_packet_diplomacy_create_clause_req(&aconnection,
					   pClauses_Dlg->pEndWidgetList->
					   data.cont->id1,
					   pWidget->data.cont->id0,
					   CLAUSE_CITY,
					   (MAX_ID - pWidget->ID));
  
  return -1;
}


static int dipomatic_window_callback(struct GUI *pWindow)
{
  return -1;
}

static struct ADVANCED_DLG * popup_diplomatic_objects(struct player *pPlayer0,
  				struct player *pPlayer1,
  				struct GUI *pMainWindow, bool L_R)
{
  struct ADVANCED_DLG *pDlg = MALLOC(sizeof(struct ADVANCED_DLG));
  struct CONTAINER *pCont = MALLOC(sizeof(struct CONTAINER));
  int hh, ww = 0, width, height, count = 0, scroll_w = 0;
  char cBuf[128];
  struct GUI *pBuf = NULL, *pWindow;
  SDL_String16 *pStr;
  SDL_Color color = {255,255,255,255};
  SDL_Color color_t = {255,255,128,255};
  
  enum diplstate_type type =
		  pplayer_get_diplstate(pPlayer0, pPlayer1)->type;
  
  pCont->id0 = pPlayer0->player_no;
  pCont->id1 = pPlayer1->player_no;
  
  hh = WINDOW_TILE_HIGH + 2;
  pStr = create_str16_from_char(get_nation_name(pPlayer0->nation), 12);
  pStr->style |= TTF_STYLE_BOLD;

  pWindow = create_window(pMainWindow->dst, pStr, 100, 100, WF_FREE_DATA);

  pWindow->action = dipomatic_window_callback;
  set_wstate(pWindow, FC_WS_NORMAL);
  
  pDlg->pEndWidgetList = pWindow;
  pWindow->data.cont = pCont;
  add_to_gui_list(ID_WINDOW, pWindow);

  /* ============================================================= */
  width = 0;
  height = 0;
  
  /* Pacts. */
  if (game.player_ptr == pPlayer0 && type != DS_ALLIANCE) {
    
    pBuf = create_iconlabel_from_chars(NULL, pWindow->dst,
			  _("Pacts"), 12, WF_DRAW_THEME_TRANSPARENT);
    pBuf->string16->fgcol = color_t;
    width = pBuf->size.w;
    height = pBuf->size.h;
    add_to_gui_list(ID_LABEL, pBuf);
    count++;
    
    /*if(type == DS_WAR || type == DS_NEUTRAL) {*/
    if(type != DS_CEASEFIRE) {
      my_snprintf(cBuf, sizeof(cBuf), "  %s", Q_("?diplomatic_state:Cease-fire"));
      pBuf = create_iconlabel_from_chars(NULL, pWindow->dst,
	cBuf, 12, (WF_DRAW_THEME_TRANSPARENT|WF_DRAW_TEXT_LABEL_WITH_SPACE));
      pBuf->string16->fgcol = color;
      width = MAX(width, pBuf->size.w);
      height = MAX(height, pBuf->size.h);
      pBuf->action = pact_callback;
      pBuf->data.cont = pCont;
      set_wstate(pBuf, FC_WS_NORMAL);
      add_to_gui_list(MAX_ID - 2, pBuf);
      count++;
    }
    
    if(type != DS_PEACE) {
      my_snprintf(cBuf, sizeof(cBuf), "  %s", Q_("?diplomatic_state:Peace"));
  
      pBuf = create_iconlabel_from_chars(NULL, pWindow->dst,
	cBuf, 12, (WF_DRAW_THEME_TRANSPARENT|WF_DRAW_TEXT_LABEL_WITH_SPACE));
      pBuf->string16->fgcol = color;
      width = MAX(width, pBuf->size.w);
      height = MAX(height, pBuf->size.h);
      pBuf->action = pact_callback;
      pBuf->data.cont = pCont;
      set_wstate(pBuf, FC_WS_NORMAL);
      add_to_gui_list(MAX_ID - 1, pBuf);
      count++;
    }
    
    if(pplayer_can_ally(pPlayer0, pPlayer1)) {
      my_snprintf(cBuf, sizeof(cBuf), "  %s", Q_("?diplomatic_state:Alliance"));
      
      pBuf = create_iconlabel_from_chars(NULL, pWindow->dst,
	cBuf, 12, (WF_DRAW_THEME_TRANSPARENT|WF_DRAW_TEXT_LABEL_WITH_SPACE));
      pBuf->string16->fgcol = color;
      width = MAX(width, pBuf->size.w);
      height = MAX(height, pBuf->size.h);
      pBuf->action = pact_callback;
      pBuf->data.cont = pCont;
      set_wstate(pBuf, FC_WS_NORMAL);
      add_to_gui_list(MAX_ID, pBuf);
      count++;
    }
    
  }
  
  /* ---------------------------- */
  if (!gives_shared_vision(pPlayer0, pPlayer1)) {
    
    pBuf = create_iconlabel_from_chars(NULL, pWindow->dst,
	_("Give shared vision"), 12,
    		(WF_DRAW_THEME_TRANSPARENT|WF_DRAW_TEXT_LABEL_WITH_SPACE));
    pBuf->string16->fgcol = color;
    width = MAX(width, pBuf->size.w);
    height = MAX(height, pBuf->size.h);
    pBuf->action = vision_callback;
    pBuf->data.cont = pCont;
    set_wstate(pBuf, FC_WS_NORMAL);
    add_to_gui_list(ID_LABEL, pBuf);
    count++;
    
    /* ---------------------------- */
    /* you can't give maps if you give shared vision */
    pBuf = create_iconlabel_from_chars(NULL, pWindow->dst,
		_("Maps"), 12, WF_DRAW_THEME_TRANSPARENT);
    pBuf->string16->fgcol = color_t;
    width = MAX(width, pBuf->size.w);
    height = MAX(height, pBuf->size.h);
    add_to_gui_list(ID_LABEL, pBuf);
    count++;
    
    /* ----- */
    my_snprintf(cBuf, sizeof(cBuf), "  %s", _("World map"));
  
    pBuf = create_iconlabel_from_chars(NULL, pWindow->dst,
	cBuf, 12, (WF_DRAW_THEME_TRANSPARENT|WF_DRAW_TEXT_LABEL_WITH_SPACE));
    pBuf->string16->fgcol = color;
    width = MAX(width, pBuf->size.w);
    height = MAX(height, pBuf->size.h);
    pBuf->action = maps_callback;
    pBuf->data.cont = pCont;
    set_wstate(pBuf, FC_WS_NORMAL);
    add_to_gui_list(MAX_ID - 1, pBuf);
    count++;
    
    /* ----- */
    my_snprintf(cBuf, sizeof(cBuf), "  %s", _("Sea map"));
  
    pBuf = create_iconlabel_from_chars(NULL, pWindow->dst,
	cBuf, 12, (WF_DRAW_THEME_TRANSPARENT|WF_DRAW_TEXT_LABEL_WITH_SPACE));
    pBuf->string16->fgcol = color;
    width = MAX(width, pBuf->size.w);
    height = MAX(height, pBuf->size.h);
    pBuf->action = maps_callback;
    pBuf->data.cont = pCont;
    set_wstate(pBuf, FC_WS_NORMAL);
    add_to_gui_list(MAX_ID, pBuf);
    count++;
  }
  /* ---------------------------- */
  if(pPlayer0->economic.gold > 0) {
    pCont->value = pPlayer0->economic.gold;
    
    my_snprintf(cBuf, sizeof(cBuf), _("Gold(max %d)"), pPlayer0->economic.gold);
    pBuf = create_iconlabel_from_chars(NULL, pWindow->dst,
			  cBuf, 12, WF_DRAW_THEME_TRANSPARENT);
    pBuf->string16->fgcol = color_t;
    width = MAX(width, pBuf->size.w);
    height = MAX(height, pBuf->size.h);
    add_to_gui_list(ID_LABEL, pBuf);
    count++;
    
    pBuf = create_edit(NULL, pWindow->dst,
    	create_str16_from_char("0", 10), 0,
    		(WF_DRAW_THEME_TRANSPARENT|WF_FREE_STRING));
    pBuf->data.cont = pCont;
    pBuf->action = gold_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
    width = MAX(width, pBuf->size.w);
    height = MAX(height, pBuf->size.h);
    add_to_gui_list(ID_LABEL, pBuf);
    count++;
  }
  /* ---------------------------- */
  
  /* Advances */
  {
    bool flag = FALSE;
    int i;
    
    for (i = 1; i < game.num_tech_types; i++) {
      if (get_invention(pPlayer0, i) == TECH_KNOWN &&
         tech_is_available(pPlayer1, i) &&
	(get_invention(pPlayer1, i) == TECH_UNKNOWN || 
	 get_invention(pPlayer1, i) == TECH_REACHABLE)) {
	     
	     pBuf = create_iconlabel_from_chars(NULL, pWindow->dst,
		_("Advances"), 12, WF_DRAW_THEME_TRANSPARENT);
             pBuf->string16->fgcol = color_t;
	     width = MAX(width, pBuf->size.w);
	     height = MAX(height, pBuf->size.h);
             add_to_gui_list(ID_LABEL, pBuf);
	     count++;
	     
	     my_snprintf(cBuf, sizeof(cBuf), "  %s", advances[i].name);
  
             pBuf = create_iconlabel_from_chars(NULL, pWindow->dst, cBuf, 12,
	         (WF_DRAW_THEME_TRANSPARENT|WF_DRAW_TEXT_LABEL_WITH_SPACE));
             pBuf->string16->fgcol = color;
	     width = MAX(width, pBuf->size.w);
	     height = MAX(height, pBuf->size.h);
	     pBuf->action = techs_callback;
	     set_wstate(pBuf, FC_WS_NORMAL);
	     pBuf->data.cont = pCont;
             add_to_gui_list(MAX_ID - i, pBuf);
	     count++;	
	     flag = TRUE;
	     i++;
	     break;
      }
    }
    
    if(flag) {
      for (; i < game.num_tech_types; i++) {
	if (get_invention(pPlayer0, i) == TECH_KNOWN &&
	   tech_is_available(pPlayer1, i) &&
	  (get_invention(pPlayer1, i) == TECH_UNKNOWN || 
	   get_invention(pPlayer1, i) == TECH_REACHABLE)) {
	     
	     my_snprintf(cBuf, sizeof(cBuf), "  %s", advances[i].name);
  
             pBuf = create_iconlabel_from_chars(NULL, pWindow->dst, cBuf, 12,
	         (WF_DRAW_THEME_TRANSPARENT|WF_DRAW_TEXT_LABEL_WITH_SPACE));
             pBuf->string16->fgcol = color;
	     width = MAX(width, pBuf->size.w);
	     height = MAX(height, pBuf->size.h);
	     pBuf->action = techs_callback;
	     set_wstate(pBuf, FC_WS_NORMAL);
             pBuf->data.cont = pCont;
             add_to_gui_list(MAX_ID - i, pBuf);
	     count++;
	}
      }
    }
    
  }  /* Advances */
  
  /* Cities */
  /****************************************************************
  Creates a sorted list of pPlayer0's cities, excluding the capital and
  any cities not visible to pPlayer1.  This means that you can only trade 
  cities visible to requesting player.  

			      - Kris Bubendorfer
  *****************************************************************/
  {
    int i = 0, j = 0, n = city_list_size(&pPlayer0->cities);
    struct city **city_list_ptrs;

    if (n > 0) {
      city_list_ptrs = MALLOC(sizeof(struct city *) * n);
      city_list_iterate(pPlayer0->cities, pCity) {
        if (!is_capital(pCity)) {
	  city_list_ptrs[i] = pCity;
	  i++;
        }
      } city_list_iterate_end;	
    } else {
      city_list_ptrs = NULL;
    }
    

    if(i > 0) {
      
      pBuf = create_iconlabel_from_chars(NULL, pWindow->dst,
		_("Cities"), 12, WF_DRAW_THEME_TRANSPARENT);
      pBuf->string16->fgcol = color_t;
      pBuf->string16->style &= ~SF_CENTER;
      width = MAX(width, pBuf->size.w);
      height = MAX(height, pBuf->size.h);
      add_to_gui_list(ID_LABEL, pBuf);
      count++;
      
      qsort(city_list_ptrs, i, sizeof(struct city *), city_name_compare);
        
      for (j = 0; j < i; j++) {
	my_snprintf(cBuf, sizeof(cBuf), "  %s", city_list_ptrs[j]->name);
  
        pBuf = create_iconlabel_from_chars(NULL, pWindow->dst, cBuf, 12,
	     (WF_DRAW_THEME_TRANSPARENT|WF_DRAW_TEXT_LABEL_WITH_SPACE));
        pBuf->string16->fgcol = color;
	width = MAX(width, pBuf->size.w);
	height = MAX(height, pBuf->size.h);
        pBuf->data.cont = pCont;
	pBuf->action = cities_callback;
	set_wstate(pBuf, FC_WS_NORMAL);
	/* MAX_ID is unigned short type range and city id must be in this range */
	assert(city_list_ptrs[j]->id <= MAX_ID);
        add_to_gui_list(MAX_ID - city_list_ptrs[j]->id, pBuf);
	count++;      
      }
      
      FREE(city_list_ptrs);
    }
  } /* Cities */
  
  pDlg->pBeginWidgetList = pBuf;
  pDlg->pBeginActiveWidgetList = pBuf;
  pDlg->pEndActiveWidgetList = pWindow->prev;
  pDlg->pScroll = NULL;
  
  hh = (Main.screen->h - 100 - WINDOW_TILE_HIGH - 4 - FRAME_WH);
  ww = hh < (count * height);
  
  if(ww) {
    pDlg->pActiveWidgetList = pWindow->prev;
    count = hh / height;
    scroll_w = create_vertical_scrollbar(pDlg, 1, count, TRUE, TRUE);
    pBuf = pWindow;
    /* hide not seen widgets */
    do {
      pBuf = pBuf->prev;
      if(count) {
	count--;
      } else {
	set_wflag(pBuf, WF_HIDDEN);
      }
    } while(pBuf != pDlg->pBeginActiveWidgetList);
  }
  
  ww = MAX(width + DOUBLE_FRAME_WH + 4 + scroll_w, 150);
  hh = Main.screen->h - 100;
  
  if(L_R) {
    pWindow->size.x = pMainWindow->size.x + pMainWindow->size.w + 20;
  } else {
    pWindow->size.x = pMainWindow->size.x - 20 - ww;
  }
  pWindow->size.y = (Main.screen->h - hh) / 2;
  
  resize_window(pWindow, NULL,
		get_game_colorRGB(COLOR_STD_BACKGROUND_BROWN), ww, hh);
  
  setup_vertical_widgets_position(1,
     pWindow->size.x + FRAME_WH + 2, pWindow->size.y + WINDOW_TILE_HIGH + 2,
	width, height, pDlg->pBeginActiveWidgetList, pDlg->pEndActiveWidgetList);
  
  if(pDlg->pScroll) {
    setup_vertical_scrollbar_area(pDlg->pScroll,
	pWindow->size.x + pWindow->size.w - FRAME_WH,
    	pWindow->size.y + WINDOW_TILE_HIGH + 1,
    	pWindow->size.h - WINDOW_TILE_HIGH - FRAME_WH - 1, TRUE);
  }
  
  return pDlg;
}

/**************************************************************************
  Handle the start of a diplomacy meeting - usually by poping up a
  diplomacy dialog.
**************************************************************************/
void handle_diplomacy_init_meeting(int counterpart, int initiated_from)
{
  if(!pClauses_Dlg) {
    struct player *pPlayer0 = &game.players[game.player_idx];
    struct player *pPlayer1 = &game.players[counterpart];
    struct CONTAINER *pCont = MALLOC(sizeof(struct CONTAINER));
    int hh, ww = 0;
    char cBuf[128];
    struct GUI *pBuf = NULL, *pWindow;
    SDL_String16 *pStr;
    SDL_Rect dst;
    SDL_Color color = {255,255,255,255};
    
    pClauses_Dlg = MALLOC(sizeof(struct ADVANCED_DLG));
    
    /*if(game.player_idx != pPlayer0->player_no) {
      pPlayer0 = pPlayer1;
      pPlayer1 = game.player_idx;
    }*/
        
    pCont->id0 = pPlayer0->player_no;
    pCont->id1 = pPlayer1->player_no;
    
    my_snprintf(cBuf, sizeof(cBuf), _("Diplomacy meeting"));
    
    hh = WINDOW_TILE_HIGH + 2;
    pStr = create_str16_from_char(cBuf, 12);
    pStr->style |= TTF_STYLE_BOLD;

    pWindow = create_window(NULL, pStr, 100, 100, 0);

    pWindow->action = dipomatic_window_callback;
    set_wstate(pWindow, FC_WS_NORMAL);
    pWindow->data.cont = pCont;
    pClauses_Dlg->pEndWidgetList = pWindow;

    add_to_gui_list(ID_WINDOW, pWindow);

    /* ============================================================= */
  
    pStr = create_str16_from_char(get_nation_name(pPlayer0->nation), 12);
    pStr->style |= (TTF_STYLE_BOLD|SF_CENTER);
    pStr->fgcol = color;
    
    pBuf = create_iconlabel(
    	create_icon_from_theme(pTheme->CANCEL_PACT_Icon, 0),
		pWindow->dst, pStr,
		(WF_ICON_ABOVE_TEXT|WF_FREE_PRIVATE_DATA|WF_FREE_THEME|
						WF_DRAW_THEME_TRANSPARENT));
						
    pBuf->private_data.cbox = MALLOC(sizeof(struct CHECKBOX));
    pBuf->private_data.cbox->state = FALSE;
    pBuf->private_data.cbox->pTRUE_Theme = pTheme->OK_PACT_Icon;
    pBuf->private_data.cbox->pFALSE_Theme = pTheme->CANCEL_PACT_Icon;
    
    add_to_gui_list(ID_ICON, pBuf);
    
    pStr = create_str16_from_char(get_nation_name(pPlayer1->nation), 12);
    pStr->style |= (TTF_STYLE_BOLD|SF_CENTER);
    pStr->fgcol = color;
    
    pBuf = create_iconlabel(
    	create_icon_from_theme(pTheme->CANCEL_PACT_Icon, 0),
		pWindow->dst, pStr,
		(WF_ICON_ABOVE_TEXT|WF_FREE_PRIVATE_DATA|WF_FREE_THEME|
    						WF_DRAW_THEME_TRANSPARENT));
    pBuf->private_data.cbox = MALLOC(sizeof(struct CHECKBOX));
    pBuf->private_data.cbox->state = FALSE;
    pBuf->private_data.cbox->pTRUE_Theme = pTheme->OK_PACT_Icon;
    pBuf->private_data.cbox->pFALSE_Theme = pTheme->CANCEL_PACT_Icon;
    add_to_gui_list(ID_ICON, pBuf);
    /* ============================================================= */
    
    pBuf = create_themeicon(pTheme->CANCEL_PACT_Icon, pWindow->dst, 
    	(WF_WIDGET_HAS_INFO_LABEL|WF_FREE_STRING|WF_DRAW_THEME_TRANSPARENT));
	
    pBuf->string16 = create_str16_from_char(_("Cancel meeting"), 12);
    
    pBuf->action = cancel_meeting_callback;
    pBuf->data.cont = pCont;
    set_wstate(pBuf, FC_WS_NORMAL);
    
    add_to_gui_list(ID_ICON, pBuf);
    
    pBuf = create_themeicon(pTheme->OK_PACT_Icon, pWindow->dst, 
    	(WF_FREE_DATA|WF_WIDGET_HAS_INFO_LABEL|
				WF_FREE_STRING|WF_DRAW_THEME_TRANSPARENT));
	
    pBuf->string16 = create_str16_from_char(_("Accept treaty"), 12);
    
    pBuf->action = accept_treaty_callback;
    pBuf->data.cont = pCont;
    set_wstate(pBuf, FC_WS_NORMAL);
    
    add_to_gui_list(ID_ICON, pBuf);
    /* ============================================================= */
    
    pClauses_Dlg->pBeginWidgetList = pBuf;
    
    create_vertical_scrollbar(pClauses_Dlg, 1, 7, TRUE, TRUE);
    hide_scrollbar(pClauses_Dlg->pScroll);
    
    /* ============================================================= */
    ww = 250;
    hh = 300;
    
    pWindow->size.x = (Main.screen->w - ww) / 2;
    pWindow->size.y = (Main.screen->h - hh) / 2;
  
    resize_window(pWindow, NULL,
		get_game_colorRGB(COLOR_STD_BACKGROUND_BROWN), ww, hh);

    pBuf = pWindow->prev;
    pBuf->size.x = pWindow->size.x + 20;
    pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 10;
    
    dst.y = WINDOW_TILE_HIGH + 10 + pBuf->size.h + 10;
    dst.x = 10;
    dst.w = pWindow->size.w - 20;
        
    pBuf = pBuf->prev;
    pBuf->size.x = pWindow->size.x + pWindow->size.w - pBuf->size.w - 20;
    pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 10;
    
    pBuf = pBuf->prev;
    pBuf->size.x = pWindow->size.x + (pWindow->size.w - (2 * pBuf->size.w + 40)) / 2;
    pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.w - 20;
    
    pBuf = pBuf->prev;
    pBuf->size.x = pBuf->next->size.x + pBuf->next->size.w + 40;
    pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.w - 20;
    
    dst.h = (pWindow->size.h - pBuf->size.w - 30) - dst.y;
    /* ============================================================= */
    
    color.unused = 136;
    SDL_FillRectAlpha(pWindow->theme, &dst, &color);
    
    /* ============================================================= */
    setup_vertical_scrollbar_area(pClauses_Dlg->pScroll,
	pWindow->size.x + dst.x + dst.w,
    	pWindow->size.y + dst.y,
    	dst.h, TRUE);
    /* ============================================================= */
    pOffers = popup_diplomatic_objects(pPlayer0, pPlayer1, pWindow, FALSE);
    
    pWants = popup_diplomatic_objects(pPlayer1, pPlayer0, pWindow, TRUE);
    /* ============================================================= */
    /* redraw */
    redraw_group(pClauses_Dlg->pBeginWidgetList, pWindow, 0);
    sdl_dirty_rect(pWindow->size);
    
    redraw_group(pOffers->pBeginWidgetList, pOffers->pEndWidgetList, 0);
    sdl_dirty_rect(pOffers->pEndWidgetList->size);
    
    redraw_group(pWants->pBeginWidgetList, pWants->pEndWidgetList, 0);
    sdl_dirty_rect(pWants->pEndWidgetList->size);
    
    flush_dirty();
  }
}


/**************************************************************************
  ...
**************************************************************************/
static void popdown_diplomacy_dialog(void)
{
  if(pClauses_Dlg) {
    lock_buffer(pClauses_Dlg->pEndWidgetList->dst);
    popdown_window_group_dialog(pOffers->pBeginWidgetList,
			      pOffers->pEndWidgetList);
    FREE(pOffers->pScroll);
    FREE(pOffers);
    
    popdown_window_group_dialog(pWants->pBeginWidgetList,
			      pWants->pEndWidgetList);
    FREE(pWants->pScroll);
    FREE(pWants);
    
    unlock_buffer();
    popdown_window_group_dialog(pClauses_Dlg->pBeginWidgetList,
			      pClauses_Dlg->pEndWidgetList);
    FREE(pClauses_Dlg->pScroll);
    FREE(pClauses_Dlg);
  }
}

/**************************************************************************
  Close all open diplomacy dialogs, for when client disconnects from game.
**************************************************************************/
void close_all_diplomacy_dialogs(void)
{
  popdown_sdip_dialog();
  popdown_diplomacy_dialog();
}

/* ================================================================= */
/* ========================== Small Diplomat Dialog ================ */
/* ================================================================= */
static struct SMALL_DLG *pSDip_Dlg = NULL;

static void popdown_sdip_dialog(void)
{
  if (pSDip_Dlg) {
    popdown_window_group_dialog(pSDip_Dlg->pBeginWidgetList,
			      pSDip_Dlg->pEndWidgetList);
    FREE(pSDip_Dlg);
  }
}

static int sdip_window_callback(struct GUI *pWindow)
{
  return std_move_window_group_callback(pSDip_Dlg->pBeginWidgetList,
								pWindow);
}

static int withdraw_vision_dlg_callback(struct GUI *pWidget)
{
  popdown_sdip_dialog();

  dsend_packet_diplomacy_cancel_pact(&aconnection,
				     pWidget->data.player->player_no,
				     CLAUSE_VISION);
  
  flush_dirty();
  return -1;
}

static int cancel_pact_dlg_callback(struct GUI *pWidget)
{
  popdown_sdip_dialog();

  dsend_packet_diplomacy_cancel_pact(&aconnection,
				     pWidget->data.player->player_no,
				     CLAUSE_CEASEFIRE);
  
  flush_dirty();
  return -1;
}

static int call_meeting_dlg_callback(struct GUI *pWidget)
{
  popdown_sdip_dialog();
  
  dsend_packet_diplomacy_init_meeting_req(&aconnection,
					  pWidget->data.player->player_no);
  
  flush_dirty();
  return -1;
}


static int cancel_sdip_dlg_callback(struct GUI *pWidget)
{
  popdown_sdip_dialog();
  flush_dirty();
  return -1;
}

static void popup_war_dialog(struct player *pPlayer)
{
  int hh, ww = 0;
  char cBuf[128];
  struct GUI *pBuf = NULL, *pWindow;
  SDL_String16 *pStr;
  SDL_Surface *pText;
  SDL_Rect dst;
  
  if (pSDip_Dlg) {
    return;
  }
  
  pSDip_Dlg = MALLOC(sizeof(struct SMALL_DLG));
      
    
  my_snprintf(cBuf, sizeof(cBuf),
       _("%s incident !"), get_nation_name(pPlayer->nation));
  hh = WINDOW_TILE_HIGH + 2;
  pStr = create_str16_from_char(cBuf, 12);
  pStr->style |= TTF_STYLE_BOLD;

  pWindow = create_window(NULL, pStr, 100, 100, 0);

  pWindow->action = sdip_window_callback;
  set_wstate(pWindow, FC_WS_NORMAL);
  ww = pWindow->size.w;
  pSDip_Dlg->pEndWidgetList = pWindow;

  add_to_gui_list(ID_WINDOW, pWindow);

  /* ============================================================= */
  /* label */
  my_snprintf(cBuf, sizeof(cBuf), _("Shall we declare WAR on them?"));
  
  pStr = create_str16_from_char(cBuf, 14);
  pStr->style |= (TTF_STYLE_BOLD|SF_CENTER);
  pStr->fgcol.r = 255;
  pStr->fgcol.g = 255;
  /* pStr->fgcol.b = 255; */

  pText = create_text_surf_from_str16(pStr);
  FREESTRING16(pStr);
  ww = MAX(ww, pText->w);
  hh += pText->h + 10;


  pBuf = create_themeicon_button_from_chars(pTheme->CANCEL_Icon,
			    pWindow->dst, _("No"), 12, 0);

  clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
  pBuf->action = cancel_sdip_dlg_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  hh += pBuf->size.h;

  add_to_gui_list(ID_BUTTON, pBuf);

  pBuf = create_themeicon_button_from_chars(pTheme->OK_Icon, pWindow->dst,
					      _("Yes"), 12, 0);

  clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);
  pBuf->action = cancel_pact_dlg_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->data.player = pPlayer;
  pBuf->key = SDLK_RETURN;
  add_to_gui_list(ID_BUTTON, pBuf);
  pBuf->size.w = MAX(pBuf->next->size.w, pBuf->size.w);
  pBuf->next->size.w = pBuf->size.w;
  ww = MAX(ww , 2 * pBuf->size.w + 20);
    
  pSDip_Dlg->pBeginWidgetList = pBuf;
  
  /* setup window size and start position */
  ww += 10;

  pBuf = pWindow->prev;
  
  ww += DOUBLE_FRAME_WH;
  hh += FRAME_WH + 5;
  
  pWindow->size.x = (Main.screen->w - ww) / 2;
  pWindow->size.y = (Main.screen->h - hh) / 2;
  
  resize_window(pWindow, NULL,
		get_game_colorRGB(COLOR_STD_BACKGROUND_BROWN), ww, hh);

  /* setup rest of widgets */
  /* label */
  dst.x = FRAME_WH + (pWindow->size.w - DOUBLE_FRAME_WH - pText->w) / 2;
  dst.y = WINDOW_TILE_HIGH + 5;
  SDL_BlitSurface(pText, NULL, pWindow->theme, &dst);
  dst.y += pText->h + 5;
  FREESURFACE(pText);
  
  /* no */
  pBuf = pWindow->prev;
  pBuf->size.y = pWindow->size.y + dst.y;
  
  /* yes */
  pBuf = pBuf->prev;
  pBuf->size.x = pWindow->size.x +
	    (pWindow->size.w - DOUBLE_FRAME_WH - (2 * pBuf->size.w + 20)) / 2;
  pBuf->size.y = pWindow->size.y + dst.y;
    
  /* no */
  pBuf->next->size.x = pBuf->size.x + pBuf->size.w + 20;
  
  /* ================================================== */
  /* redraw */
  redraw_group(pSDip_Dlg->pBeginWidgetList, pWindow, 0);
  sdl_dirty_rect(pWindow->size);
  flush_dirty();
}

/* ===================================================================== */

void popup_diplomacy_dialog(struct player *pPlayer)
{
  enum diplstate_type type =
		  pplayer_get_diplstate(game.player_ptr, pPlayer)->type;
  
  if(!can_meet_with_player(pPlayer)) {
    if(type == DS_WAR || pPlayer == game.player_ptr) {
      flush_dirty();
      return;
    } else {
      popup_war_dialog(pPlayer);
      return;
    }
  } else {
    int hh, ww = 0, button_w = 0, button_h = 0;
    char cBuf[128];
    struct GUI *pBuf = NULL, *pWindow;
    SDL_String16 *pStr;
    SDL_Surface *pText;
    SDL_Rect dst;
    SDL_Color color = {255, 255, 255, 255};
    bool shared = FALSE;
    
    if (pSDip_Dlg) {
      return;
    }
  
    pSDip_Dlg = MALLOC(sizeof(struct SMALL_DLG));
          
    my_snprintf(cBuf, sizeof(cBuf),  _("Foreign Minister"));
    hh = WINDOW_TILE_HIGH + 2;
    pStr = create_str16_from_char(cBuf, 12);
    pStr->style |= TTF_STYLE_BOLD;

    pWindow = create_window(NULL, pStr, 100, 100, 0);

    pWindow->action = sdip_window_callback;
    set_wstate(pWindow, FC_WS_NORMAL);
    ww = pWindow->size.w;
    pSDip_Dlg->pEndWidgetList = pWindow;

    add_to_gui_list(ID_WINDOW, pWindow);

    /* ============================================================= */
    /* label */
    my_snprintf(cBuf, sizeof(cBuf), _("Sir!, %s ambassador has arrived\n"
    		"What are your wishes?"), get_nation_name(pPlayer->nation));
  
    pStr = create_str16_from_char(cBuf, 14);
    pStr->style |= (TTF_STYLE_BOLD|SF_CENTER);
    pStr->fgcol.r = 255;
    pStr->fgcol.g = 255;
    /* pStr->fgcol.b = 255; */

    pText = create_text_surf_from_str16(pStr);
    FREESTRING16(pStr);
    ww = MAX(ww , pText->w);
    hh += pText->h + 15;
          
    if(type != DS_WAR && can_client_issue_orders()) {
      
      if(type == DS_NEUTRAL) {
	my_snprintf(cBuf, sizeof(cBuf), _("Declare WAR"));
      } else {
	my_snprintf(cBuf, sizeof(cBuf), _("Cancel Treaty"));
      }
      
      /* cancel treaty */
      pBuf = create_themeicon_button_from_chars(pTheme->UNITS2_Icon,
			pWindow->dst, cBuf, 12, 0);

      pBuf->action = cancel_pact_dlg_callback;
      set_wstate(pBuf, FC_WS_NORMAL);
      pBuf->string16->fgcol = color;
      pBuf->data.player = pPlayer;
      pBuf->key = SDLK_c;
      add_to_gui_list(ID_BUTTON, pBuf);
      pBuf->size.w = MAX(pBuf->next->size.w, pBuf->size.w);
      pBuf->next->size.w = pBuf->size.w;
      button_w = MAX(button_w , pBuf->size.w);
      button_h = MAX(button_h , pBuf->size.h);
    
      shared = gives_shared_vision(game.player_ptr, pPlayer);
      
      if(shared) {
        /* shared vision */
        pBuf = create_themeicon_button_from_chars(pTheme->UNITS2_Icon, pWindow->dst,
					      _("Withdraw vision"), 12, 0);

        pBuf->action = withdraw_vision_dlg_callback;
        set_wstate(pBuf, FC_WS_NORMAL);
        pBuf->data.player = pPlayer;
        pBuf->key = SDLK_w;
	pBuf->string16->fgcol = color;
        add_to_gui_list(ID_BUTTON, pBuf);
        pBuf->size.w = MAX(pBuf->next->size.w, pBuf->size.w);
        pBuf->next->size.w = pBuf->size.w;
        button_w = MAX(button_w , pBuf->size.w);
        button_h = MAX(button_h , pBuf->size.h);
      }
    }
    
    /* meet */
    pBuf = create_themeicon_button_from_chars(pTheme->PLAYERS_Icon, pWindow->dst,
					      _("Call Diplomatic Meeting"), 12, 0);

    pBuf->action = call_meeting_dlg_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
    pBuf->data.player = pPlayer;
    pBuf->key = SDLK_m;
    pBuf->string16->fgcol = color;
    add_to_gui_list(ID_BUTTON, pBuf);
    pBuf->size.w = MAX(pBuf->next->size.w, pBuf->size.w);
    pBuf->next->size.w = pBuf->size.w;
    button_w = MAX(button_w , pBuf->size.w);
    button_h = MAX(button_h , pBuf->size.h);

    pBuf = create_themeicon_button_from_chars(pTheme->CANCEL_Icon,
			    pWindow->dst, _("Send him back"), 12, 0);

    pBuf->action = cancel_sdip_dlg_callback;
    set_wstate(pBuf, FC_WS_NORMAL);
    pBuf->string16->fgcol = color;
    pBuf->key = SDLK_ESCAPE;
    button_w = MAX(button_w , pBuf->size.w);
    button_h = MAX(button_h , pBuf->size.h);
    
    button_h += 4;
    ww = MAX(ww, button_w + 20);
    
    if(type != DS_WAR) {
      if(shared) {
	hh += 4 * (button_h + 10);
      } else {
        hh += 3 * (button_h + 10);
      }
    } else {
      hh += 2 * (button_h + 10);
    }
    
    add_to_gui_list(ID_BUTTON, pBuf);


    pSDip_Dlg->pBeginWidgetList = pBuf;
  
    /* setup window size and start position */
    ww += 10;

    pBuf = pWindow->prev;
  
    ww += DOUBLE_FRAME_WH;
    hh += FRAME_WH + 5;
   
    pWindow->size.x = (Main.screen->w - ww) / 2;
    pWindow->size.y = (Main.screen->h - hh) / 2;
  
    resize_window(pWindow, NULL,
		get_game_colorRGB(COLOR_STD_BACKGROUND_BROWN), ww, hh);

    /* setup rest of widgets */
    /* label */
    dst.x = FRAME_WH + (pWindow->size.w - DOUBLE_FRAME_WH - pText->w) / 2;
    dst.y = WINDOW_TILE_HIGH + 5;
    SDL_BlitSurface(pText, NULL, pWindow->theme, &dst);
    dst.y += pText->h + 15;
    FREESURFACE(pText);
         
    pBuf = pWindow;
  
    if(type != DS_WAR) {
      /* cancel treaty */
      pBuf = pBuf->prev;
      pBuf->size.w = button_w;
      pBuf->size.h = button_h;
      pBuf->size.x = pWindow->size.x +
	    (pWindow->size.w - DOUBLE_FRAME_WH - (pBuf->size.w)) / 2;
      pBuf->size.y = pWindow->size.y + dst.y;
      
      if(shared) {
	/* vision */
        pBuf = pBuf->prev;
        pBuf->size.w = button_w;
        pBuf->size.h = button_h;
        pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h + 10;
        pBuf->size.x = pBuf->next->size.x;
      }
      
      /* meet */
      pBuf = pBuf->prev;
      pBuf->size.w = button_w;
      pBuf->size.h = button_h;
      pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h + 10;
      pBuf->size.x = pBuf->next->size.x;
            
    } else {
    
      /* meet */
      pBuf = pBuf->prev;
      pBuf->size.w = button_w;
      pBuf->size.h = button_h;
      pBuf->size.x = pWindow->size.x +
	    (pWindow->size.w - DOUBLE_FRAME_WH - (pBuf->size.w)) / 2;
      pBuf->size.y = pWindow->size.y + dst.y;
      
    }
    
    /* cancel */
    pBuf = pBuf->prev;
    pBuf->size.w = button_w;
    pBuf->size.h = button_h;
    pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h + 10;
    pBuf->size.x = pBuf->next->size.x;
  
    /* ================================================== */
    /* redraw */
    redraw_group(pSDip_Dlg->pBeginWidgetList, pWindow, 0);
    sdl_dirty_rect(pWindow->size);
  
    flush_dirty();
  }
}
