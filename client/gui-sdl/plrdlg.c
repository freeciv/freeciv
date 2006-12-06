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

/* client */
#include "civclient.h"
#include "climisc.h"

/* gui-sdl */
#include "colors.h"
#include "diplodlg.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_tilespec.h"
#include "gui_zoom.h"
#include "inteldlg.h"
#include "mapview.h"
#include "sprite.h"
#include "themespec.h"
#include "widget.h"

#include "plrdlg.h"

#ifndef M_PI
#define M_PI		3.14159265358979323846	/* pi */
#endif
#ifndef M_PI_2
#define M_PI_2		1.57079632679489661923	/* pi/2 */
#endif

static struct SMALL_DLG  *pPlayers_Dlg = NULL;

static int exit_players_dlg_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    popdown_players_dialog();
    flush_dirty();
  }
  return -1;
}

static int player_callback(struct widget *pWidget)
{
  struct player *pPlayer = pWidget->data.player;
  switch(Main.event.button.button) {
#if 0    
      case SDL_BUTTON_LEFT:
      
      break;
      case SDL_BUTTON_MIDDLE:
  
      break;
#endif    
    case SDL_BUTTON_RIGHT:
      if (can_intel_with_player(pPlayer)) {
	popdown_players_dialog();
        popup_intel_dialog(pPlayer);
	return -1;
      }
    break;
    default:
      popdown_players_dialog();
      popup_diplomacy_dialog(pPlayer);
      return -1;
    break;
  }
    
  return -1;
}

static int players_window_dlg_callback(struct widget *pWindow)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    if (move_window_group_dialog(pPlayers_Dlg->pBeginWidgetList, pWindow)) {
      sellect_window_group_dialog(pPlayers_Dlg->pBeginWidgetList, pWindow);
      update_players_dialog();
    } else {
      if(sellect_window_group_dialog(pPlayers_Dlg->pBeginWidgetList, pWindow)) {
        widget_flush(pWindow);
      }      
    }
  }
  return -1;
}

static int toggle_draw_war_status_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    /* exit button -> neutral -> war -> casefire -> peace -> alliance */
    struct widget *pPlayer = pPlayers_Dlg->pEndWidgetList->prev->prev->prev->prev->prev->prev;
    SDL_Client_Flags ^= CF_DRAW_PLAYERS_WAR_STATUS;
    do{
      pPlayer = pPlayer->prev;
      FREESURFACE(pPlayer->gfx);
    } while(pPlayer != pPlayers_Dlg->pBeginWidgetList);
    update_players_dialog();
  }
  return -1;
}

static int toggle_draw_ceasefire_status_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    /* exit button -> neutral -> war -> casefire -> peace -> alliance */
    struct widget *pPlayer = pPlayers_Dlg->pEndWidgetList->prev->prev->prev->prev->prev->prev;
    SDL_Client_Flags ^= CF_DRAW_PLAYERS_CEASEFIRE_STATUS;
    do{
      pPlayer = pPlayer->prev;
      FREESURFACE(pPlayer->gfx);
    } while(pPlayer != pPlayers_Dlg->pBeginWidgetList);
    update_players_dialog();
  }
  return -1;
}

static int toggle_draw_pease_status_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    /* exit button -> neutral -> war -> casefire -> peace -> alliance */
    struct widget *pPlayer = pPlayers_Dlg->pEndWidgetList->prev->prev->prev->prev->prev->prev;
    SDL_Client_Flags ^= CF_DRAW_PLAYERS_PEACE_STATUS;
    do{
      pPlayer = pPlayer->prev;
      FREESURFACE(pPlayer->gfx);
    } while(pPlayer != pPlayers_Dlg->pBeginWidgetList);
    update_players_dialog();
  }
  return -1;
}

static int toggle_draw_alliance_status_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    /* exit button -> neutral -> war -> casefire -> peace -> alliance */
    struct widget *pPlayer = pPlayers_Dlg->pEndWidgetList->prev->prev->prev->prev->prev->prev;
    SDL_Client_Flags ^= CF_DRAW_PLAYERS_ALLIANCE_STATUS;
    do{
      pPlayer = pPlayer->prev;
      FREESURFACE(pPlayer->gfx);
    } while(pPlayer != pPlayers_Dlg->pBeginWidgetList);
    update_players_dialog();
  }
  return -1;
}

static int toggle_draw_neutral_status_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    /* exit button -> neutral -> war -> casefire -> peace -> alliance */
    struct widget *pPlayer = pPlayers_Dlg->pEndWidgetList->prev->prev->prev->prev->prev->prev;
    
    SDL_Client_Flags ^= CF_DRAW_PLAYERS_NEUTRAL_STATUS;
    do{
      pPlayer = pPlayer->prev;
      FREESURFACE(pPlayer->gfx);
    } while(pPlayer != pPlayers_Dlg->pBeginWidgetList);
    update_players_dialog();
  }
  return -1;
}


static bool have_diplomat_info_about(struct player *pPlayer)
{
  return (pPlayer == game.player_ptr ||
  	(pPlayer != game.player_ptr
	  && player_has_embassy(game.player_ptr, pPlayer)));
}

/**************************************************************************
  Update all information in the player list dialog.
**************************************************************************/
void update_players_dialog(void)
{
  if(pPlayers_Dlg) {
    struct widget *pPlayer0, *pPlayer1;
    struct player *pPlayer;
    char cBuf[128], *state, *team;
    int idle;
    SDL_Rect dst0, dst1;
          
    /* redraw window */
    widget_redraw(pPlayers_Dlg->pEndWidgetList);
    
    /* exit button -> neutral -> war -> casefire -> peace -> alliance */
    pPlayer0 = pPlayers_Dlg->pEndWidgetList->prev->prev->prev->prev->prev->prev;
    do{
      pPlayer0 = pPlayer0->prev;
      pPlayer1 = pPlayer0;
      pPlayer = pPlayer0->data.player;
      
      /* the team */
      if (pPlayer->team) {
        team = _(team_get_name(pPlayer->team));
      } else {
        team = _("none");
      }
            
      if(pPlayer->is_alive) {
        if (pPlayer->ai.control) {
	  state = _("AI");
        } else {
          if (pPlayer->is_connected) {
            if (pPlayer->phase_done) {
              state = _("done");
            } else {
              state = _("moving");
            }
          } else {
            state = _("disconnected");
          }
        }
      } else {
        state = _("R.I.P");
      }
  
      /* text for idleness */
      if (pPlayer->nturns_idle > 3) {
        idle = pPlayer->nturns_idle - 1;
      } else {
        idle = 0;
      }
      
      my_snprintf(cBuf, sizeof(cBuf), _("Name : %s\nNation : %s\nTeam : %s\n"
      					"Embassy :%s\n"
    					"State : %s\nIdle : %d %s"),
                   pPlayer->name, get_nation_name(pPlayer->nation),
		   team, get_embassy_status(game.player_ptr, pPlayer),
		   state, idle, PL_("turn", "turns", idle));
      
      copy_chars_to_string16(pPlayer0->string16, cBuf);
          
      /* now add some eye candy ... */
      if(pPlayer1 != pPlayers_Dlg->pBeginWidgetList) {
        dst0.x = pPlayer0->size.x + pPlayer0->size.w / 2;
        dst0.y = pPlayer0->size.y + pPlayer0->size.h / 2;

        do{
          pPlayer1 = pPlayer1->prev;
	  if (have_diplomat_info_about(pPlayer) ||
	     have_diplomat_info_about(pPlayer1->data.player)) {
            dst1.x = pPlayer1->size.x + pPlayer1->size.w / 2;
            dst1.y = pPlayer1->size.y + pPlayer1->size.h / 2;
               
            switch (pplayer_get_diplstate(pPlayer, pPlayer1->data.player)->type) {
	      case DS_ARMISTICE:
	        if(SDL_Client_Flags & CF_DRAW_PLAYERS_NEUTRAL_STATUS) {
	          putline(pPlayer1->dst->surface, dst0.x, dst0.y, dst1.x, dst1.y,
                    map_rgba(pPlayer1->dst->surface->format, *get_game_colorRGB(COLOR_THEME_PLRDLG_ARMISTICE)));
	        }
	      break;
              case DS_WAR:
	        if(SDL_Client_Flags & CF_DRAW_PLAYERS_WAR_STATUS) {
	          putline(pPlayer1->dst->surface, dst0.x, dst0.y, dst1.x, dst1.y,
                         map_rgba(pPlayer1->dst->surface->format, *get_game_colorRGB(COLOR_THEME_PLRDLG_WAR)));		  
	        }
              break;
	      case DS_CEASEFIRE:
	        if (SDL_Client_Flags & CF_DRAW_PLAYERS_CEASEFIRE_STATUS) {
	          putline(pPlayer1->dst->surface, dst0.x, dst0.y, dst1.x, dst1.y,
	    		map_rgba(pPlayer1->dst->surface->format, *get_game_colorRGB(COLOR_THEME_PLRDLG_CEASEFIRE)));
	        }
              break;
              case DS_PEACE:
	        if (SDL_Client_Flags & CF_DRAW_PLAYERS_PEACE_STATUS) {
	          putline(pPlayer1->dst->surface, dst0.x, dst0.y, dst1.x, dst1.y,
	    		map_rgba(pPlayer1->dst->surface->format, *get_game_colorRGB(COLOR_THEME_PLRDLG_PEACE)));
	        }
              break;
	      case DS_ALLIANCE:
	        if (SDL_Client_Flags & CF_DRAW_PLAYERS_ALLIANCE_STATUS) {
	          putline(pPlayer1->dst->surface, dst0.x, dst0.y, dst1.x, dst1.y,
	    		map_rgba(pPlayer1->dst->surface->format, *get_game_colorRGB(COLOR_THEME_PLRDLG_ALLIANCE)));
	        }
              break;
              default:
	        /* no contact */
              break;
	    }  
	  }
	  
        } while(pPlayer1 != pPlayers_Dlg->pBeginWidgetList);
      }
      
    } while(pPlayer0 != pPlayers_Dlg->pBeginWidgetList);
    
    /* -------------------- */
    /* redraw */
    redraw_group(pPlayers_Dlg->pBeginWidgetList,
    			pPlayers_Dlg->pEndWidgetList->prev, 0);
    widget_mark_dirty(pPlayers_Dlg->pEndWidgetList);
  
    flush_dirty();
  }
}

/**************************************************************************
  Popup (or raise) the player list dialog.
**************************************************************************/
void popup_players_dialog(bool raise)
{
  struct widget *pWindow = NULL, *pBuf = NULL;
  SDL_Surface *pLogo = NULL, *pZoomed = NULL;
  SDL_String16 *pStr;
  SDL_Rect dst;
  int i, n, w = 0, h;
  double a, b, r;
  struct player *pPlayer;
  
  if (pPlayers_Dlg) {
    return;
  }
  
  n = 0;
  for(i=0; i<game.info.nplayers; i++) {
    if(is_barbarian(get_player(i))) {
      continue;
    }
    n++;
  }

  if(n < 2) {
    return;
  }
    
  pPlayers_Dlg = fc_calloc(1, sizeof(struct SMALL_DLG));
  
  pStr = create_str16_from_char(_("Players"), adj_font(12));
  pStr->style |= TTF_STYLE_BOLD;
  
  pWindow = create_window(NULL, pStr, 1, 1, 0);
    
  pWindow->action = players_window_dlg_callback;
  set_wstate(pWindow, FC_WS_NORMAL);
    
  add_to_gui_list(ID_WINDOW, pWindow);
  pPlayers_Dlg->pEndWidgetList = pWindow;
  /* ---------- */
  /* exit button */
  pBuf = create_themeicon(pTheme->Small_CANCEL_Icon, pWindow->dst,
  			  			WF_RESTORE_BACKGROUND);
  pBuf->action = exit_players_dlg_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  
  add_to_gui_list(ID_BUTTON, pBuf);
  /* ---------- */
  
  for(i = 0; i<DS_LAST; i++) {
    switch (i) {
      case DS_ARMISTICE:
	pBuf = create_checkbox(pWindow->dst,
		(SDL_Client_Flags & CF_DRAW_PLAYERS_NEUTRAL_STATUS),
      						WF_RESTORE_BACKGROUND);
	pBuf->action = toggle_draw_neutral_status_callback;
	pBuf->key = SDLK_n;
      break;
      case DS_WAR:
	pBuf = create_checkbox(pWindow->dst,
		(SDL_Client_Flags & CF_DRAW_PLAYERS_WAR_STATUS),
      						WF_RESTORE_BACKGROUND);
	pBuf->action = toggle_draw_war_status_callback;
	pBuf->key = SDLK_w;
      break;
      case DS_CEASEFIRE:
	pBuf = create_checkbox(pWindow->dst,
		(SDL_Client_Flags & CF_DRAW_PLAYERS_CEASEFIRE_STATUS),
      						WF_RESTORE_BACKGROUND);
	pBuf->action = toggle_draw_ceasefire_status_callback;
	pBuf->key = SDLK_c;
      break;
      case DS_PEACE:
	pBuf = create_checkbox(pWindow->dst,
		(SDL_Client_Flags & CF_DRAW_PLAYERS_PEACE_STATUS),
      						WF_RESTORE_BACKGROUND);
	pBuf->action = toggle_draw_pease_status_callback;
	pBuf->key = SDLK_p;
      break;
      case DS_ALLIANCE:
	pBuf = create_checkbox(pWindow->dst,
		(SDL_Client_Flags & CF_DRAW_PLAYERS_ALLIANCE_STATUS),
      						WF_RESTORE_BACKGROUND);
	pBuf->action = toggle_draw_alliance_status_callback;
	pBuf->key = SDLK_a;
      break;
      default:
	 /* no contact */
	 continue;
      break;
    }
    set_wstate(pBuf, FC_WS_NORMAL);
    add_to_gui_list(ID_CHECKBOX, pBuf);
  } 
  /* ---------- */
  
  for(i=0; i<game.info.nplayers; i++) {
    pPlayer = get_player(i);
      
    if(is_barbarian(pPlayer)) {
      continue;
    }
                
    pStr = create_string16(NULL, 0, adj_font(10));
    pStr->style |= (TTF_STYLE_BOLD|SF_CENTER);
   
    pLogo = get_nation_flag_surface(pPlayer->nation);
    pZoomed = ZoomSurface(pLogo, DEFAULT_ZOOM * (3.0 - n * 0.05), DEFAULT_ZOOM * (3.0 - n * 0.05), 1);
            
    pBuf = create_icon2(pZoomed, pWindow->dst,
    	(WF_RESTORE_BACKGROUND|WF_WIDGET_HAS_INFO_LABEL|WF_FREE_THEME));
    
    pBuf->string16 = pStr;
      
    if(!pPlayer->is_alive) {
      pStr = create_str16_from_char("R.I.P" , adj_font(10));
      pStr->style |= TTF_STYLE_BOLD;
      pStr->fgcol = *get_game_colorRGB(COLOR_THEME_PLRDLG_TEXT);
      pLogo = create_text_surf_from_str16(pStr);
      FREESTRING16(pStr);
	
      dst.x = (pZoomed->w - pLogo->w) / 2;
      dst.y = (pZoomed->h - pLogo->h) / 2;
      alphablit(pLogo, NULL, pZoomed, &dst);
      FREESURFACE(pLogo);
    }
     
    if(pPlayer->is_alive) {
      set_wstate(pBuf, FC_WS_NORMAL);
    }
    
    pBuf->data.player = pPlayer;
  
    pBuf->action = player_callback;
    
    add_to_gui_list(ID_LABEL, pBuf);
    
  }
  
  pPlayers_Dlg->pBeginWidgetList = pBuf;

  w = adj_size(500);
  h = adj_size(400);
  r = MIN(w,h);
  r -= ((MAX(pBuf->size.w, pBuf->size.h) * 2) + WINDOW_TITLE_HEIGHT + pTheme->FR_Bottom->h);
  r /= 2;
  a = (2.0 * M_PI) / n;

  widget_set_position(pWindow,
                      (Main.screen->w - w) / 2,
                      (Main.screen->h - h) / 2);
  
  resize_window(pWindow, NULL, NULL, w, h);
  
  putframe(pWindow->theme, 0, 0, w - 1, h - 1, map_rgba(pWindow->theme->format, *get_game_colorRGB(COLOR_THEME_PLRDLG_FRAME)));
  
  /* exit button */
  pBuf = pWindow->prev;
  
  pBuf->size.x = pWindow->size.x + pWindow->size.w - pBuf->size.w - pTheme->FR_Right->w - 1;
  pBuf->size.y = pWindow->size.y + 1;
    
  n = WINDOW_TITLE_HEIGHT + adj_size(4);
  pStr = create_string16(NULL, 0, adj_font(10));
  pStr->style |= TTF_STYLE_BOLD;
  pStr->bgcol = (SDL_Color) {0, 0, 0, 0};
  
  for(i = 0; i<DS_LAST; i++) {
      switch (i) {
	case DS_ARMISTICE:
	  pStr->fgcol = *get_game_colorRGB(COLOR_THEME_PLRDLG_ARMISTICE);
	break;
        case DS_WAR:
	  pStr->fgcol = *get_game_colorRGB(COLOR_THEME_PLRDLG_WAR);
	break;
	case DS_CEASEFIRE:
	  pStr->fgcol = *get_game_colorRGB(COLOR_THEME_PLRDLG_CEASEFIRE);
	break;
        case DS_PEACE:
	  pStr->fgcol = *get_game_colorRGB(COLOR_THEME_PLRDLG_PEACE);
        break;
	case DS_ALLIANCE:
	  pStr->fgcol = *get_game_colorRGB(COLOR_THEME_PLRDLG_ALLIANCE);
	break;
        default:
	   /* no contact */
	   continue;
        break;
      }
      
      copy_chars_to_string16(pStr, diplstate_text(i));
      pLogo = create_text_surf_from_str16(pStr);
  
      pBuf = pBuf->prev;
      h = MAX(pBuf->size.h, pLogo->h);
      pBuf->size.x = pWindow->size.x + adj_size(5);
      pBuf->size.y = pWindow->size.y + n + (h - pBuf->size.h) / 2;
      
      dst.x = adj_size(5) + pBuf->size.w + adj_size(6);
      dst.y = n + (h - pLogo->h) / 2;
      alphablit(pLogo, NULL, pWindow->theme, &dst);
      n += h;
      FREESURFACE(pLogo);
  }
  FREESTRING16(pStr);
     
  /* first player shield */
  pBuf = pBuf->prev;
  pBuf->size.x = pWindow->size.x + pWindow->size.w / 2 - pBuf->size.w / 2;
  pBuf->size.y = pWindow->size.y + pWindow->size.h / 2 - r;
  
  n = 1;
  if(pBuf != pPlayers_Dlg->pBeginWidgetList) {
    do{
      pBuf = pBuf->prev;
      b = M_PI_2 + n * a;
      pBuf->size.x = pWindow->size.x + pWindow->size.w / 2 - r * cos(b) - pBuf->size.w / 2;
      pBuf->size.y = pWindow->size.y + pWindow->size.h / 2 - r * sin(b);
      n++;
    } while(pBuf != pPlayers_Dlg->pBeginWidgetList);
  }
  
  update_players_dialog();
  
}

/**************************************************************************
  Popdownown the player list dialog.
**************************************************************************/
void popdown_players_dialog(void)
{
  if (pPlayers_Dlg) {
    popdown_window_group_dialog(pPlayers_Dlg->pBeginWidgetList,
			pPlayers_Dlg->pEndWidgetList);
    FC_FREE(pPlayers_Dlg);
  }
}


/* ============================== SHORT =============================== */
static struct ADVANCED_DLG  *pShort_Players_Dlg = NULL;

static int players_nations_window_dlg_callback(struct widget *pWindow)
{
  return -1;
}

static int exit_players_nations_dlg_callback(struct widget *pWidget)
{
  if (Main.event.button.button == SDL_BUTTON_LEFT) {
    popdown_players_nations_dialog();
    flush_dirty();
  }
  return -1;
}

static int player_nation_callback(struct widget *pWidget)
{
  struct player *pPlayer = pWidget->data.player;
  popdown_players_nations_dialog();
  switch(Main.event.button.button) {
#if 0    
      case SDL_BUTTON_LEFT:
      
      break;
      case SDL_BUTTON_MIDDLE:
  
      break;
#endif    
    case SDL_BUTTON_RIGHT:
      if (can_intel_with_player(pPlayer)) {
        popup_intel_dialog(pPlayer);
      } else {
	flush_dirty();
      }
    break;
    default:
      if(pPlayer->player_no != game.info.player_idx) {
        popup_diplomacy_dialog(pPlayer);
      }
    break;
  }
  
  return -1;
}

/**************************************************************************
  Popup (or raise) the short player list dialog version.
**************************************************************************/
void popup_players_nations_dialog(void)
{
  struct widget *pWindow = NULL, *pBuf = NULL;
  SDL_Surface *pLogo = NULL;
  SDL_String16 *pStr;
  char cBuf[128], *state;
  int i, n = 0, w = 0, h, units_h = 0;
  struct player *pPlayer;
  const struct player_diplstate *pDS;
  
  if (pShort_Players_Dlg) {
    return;
  }
     
  h = WINDOW_TITLE_HEIGHT + adj_size(3) + pTheme->FR_Bottom->h;
      
  pShort_Players_Dlg = fc_calloc(1, sizeof(struct ADVANCED_DLG));
  
  pStr = create_str16_from_char(_("Nations") , adj_font(12));
  pStr->style |= TTF_STYLE_BOLD;
  
  pWindow = create_window(NULL, pStr, 1, 1, 0);
    
  pWindow->action = players_nations_window_dlg_callback;
  set_wstate(pWindow, FC_WS_NORMAL);
  w = MAX(w, pWindow->size.w);
  
  add_to_gui_list(ID_WINDOW, pWindow);
  pShort_Players_Dlg->pEndWidgetList = pWindow;
  /* ---------- */
  /* exit button */
  pBuf = create_themeicon(pTheme->Small_CANCEL_Icon, pWindow->dst,
  			  			WF_RESTORE_BACKGROUND);
  w += pBuf->size.w + adj_size(10);
  pBuf->action = exit_players_nations_dlg_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  
  add_to_gui_list(ID_BUTTON, pBuf);
  /* ---------- */
  
  for(i=0; i<game.info.nplayers; i++) {
    if(i != game.info.player_idx) {
      pPlayer = get_player(i);
      
      if(!pPlayer->is_alive || is_barbarian(pPlayer)) {
        continue;
      }
      
      pDS = pplayer_get_diplstate(game.player_ptr, pPlayer);
            
      if(pPlayer->ai.control) {
	state = _("AI");
      } else {
        if (pPlayer->is_connected) {
          if (pPlayer->phase_done) {
      	    state = _("done");
          } else {
      	    state = _("moving");
          }
        } else {
          state = _("disconnected");
        }
      }
     
      if(pDS->type == DS_CEASEFIRE) {
	my_snprintf(cBuf, sizeof(cBuf), "%s(%s) - %d %s",
                get_nation_name(pPlayer->nation), state,
		pDS->turns_left, PL_("turn", "turns", pDS->turns_left));
      } else {
	my_snprintf(cBuf, sizeof(cBuf), "%s(%s)",
                           get_nation_name(pPlayer->nation), state);
      }
      
      pStr = create_str16_from_char(cBuf, adj_font(10));
      pStr->style |= TTF_STYLE_BOLD;
   
      pLogo = get_nation_flag_surface(pPlayer->nation);
      
      pBuf = create_iconlabel(pLogo, pWindow->dst, pStr, 
    	(/*WF_FREE_THEME|*/WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE));
                      
      /* now add some eye candy ... */
      switch (pDS->type) {
	case DS_ARMISTICE:
	  pBuf->string16->fgcol = *get_game_colorRGB(COLOR_THEME_PLRDLG_ARMISTICE);
	  set_wstate(pBuf, FC_WS_NORMAL);
        break;
        case DS_WAR:
	  if(can_meet_with_player(pPlayer) || can_intel_with_player(pPlayer)) {
            set_wstate(pBuf, FC_WS_NORMAL);
	    pBuf->string16->fgcol = *get_game_colorRGB(COLOR_THEME_PLRDLG_WAR);
          } else {
	    pBuf->string16->fgcol = *(get_game_colorRGB(COLOR_THEME_PLRDLG_WAR_RESTRICTED));
	  }
        break;
	case DS_CEASEFIRE:
	  pBuf->string16->fgcol = *get_game_colorRGB(COLOR_THEME_PLRDLG_CEASEFIRE);
	  set_wstate(pBuf, FC_WS_NORMAL);
        break;
        case DS_PEACE:
	  pBuf->string16->fgcol = *get_game_colorRGB(COLOR_THEME_PLRDLG_PEACE);
	  set_wstate(pBuf, FC_WS_NORMAL);
        break;
	case DS_ALLIANCE:
	  pBuf->string16->fgcol = *get_game_colorRGB(COLOR_THEME_PLRDLG_ALLIANCE);
	  set_wstate(pBuf, FC_WS_NORMAL);
        break;
	case DS_NO_CONTACT:
	  pBuf->string16->fgcol = *(get_game_colorRGB(COLOR_THEME_WIDGET_DISABLED_TEXT));
	break;
        default:
	  set_wstate(pBuf, FC_WS_NORMAL);
        break;
      }
      
      pBuf->string16->bgcol = (SDL_Color) {0, 0, 0, 0};
    
      pBuf->data.player = pPlayer;
  
      pBuf->action = player_nation_callback;
            
  
      add_to_gui_list(ID_LABEL, pBuf);
    
      w = MAX(w, pBuf->size.w);
      h += pBuf->size.h;
    
      if (n > 19)
      {
        set_wflag(pBuf, WF_HIDDEN);
      }
      
      n++;  
    }
  }
  pShort_Players_Dlg->pBeginWidgetList = pBuf;
  pShort_Players_Dlg->pBeginActiveWidgetList = pShort_Players_Dlg->pBeginWidgetList;
  pShort_Players_Dlg->pEndActiveWidgetList = pWindow->prev->prev;
  pShort_Players_Dlg->pActiveWidgetList = pShort_Players_Dlg->pEndActiveWidgetList;
  
  
  /* ---------- */
  if (n > 20)
  {
     
    units_h = create_vertical_scrollbar(pShort_Players_Dlg, 1, 20, TRUE, TRUE);
    pShort_Players_Dlg->pScroll->count = n;
    
    n = units_h;
    w += n;
    
    units_h = 20 * pBuf->size.h + WINDOW_TITLE_HEIGHT + adj_size(3) + pTheme->FR_Bottom->h;
    
  } else {
    units_h = h;
  }
        
  /* ---------- */
  
  w += (pTheme->FR_Left->w + pTheme->FR_Right->w);
  
  h = units_h;

  widget_set_position(pWindow,
    ((Main.event.motion.x + w < Main.screen->w) ?
      (Main.event.motion.x + adj_size(10)) : (Main.screen->w - w - adj_size(10))),
    ((Main.event.motion.y - (WINDOW_TITLE_HEIGHT + adj_size(2)) + h < Main.screen->h) ?
      (Main.event.motion.y - (WINDOW_TITLE_HEIGHT + adj_size(2))) :
      (Main.screen->h - h - adj_size(10))));
  
  resize_window(pWindow, NULL, NULL, w, h);
  
  w -= (pTheme->FR_Left->w + pTheme->FR_Right->w);
  
  if (pShort_Players_Dlg->pScroll)
  {
    w -= n;
  }
  
  /* exit button */
  pBuf = pWindow->prev;
  pBuf->size.x = pWindow->size.x + pWindow->size.w - pBuf->size.w - pTheme->FR_Right->w - 1;
  pBuf->size.y = pWindow->size.y + 1;
  
  /* cities */
  pBuf = pBuf->prev;
  setup_vertical_widgets_position(1,
	pWindow->size.x + pTheme->FR_Left->w, pWindow->size.y + WINDOW_TITLE_HEIGHT + adj_size(2),
	w, 0, pShort_Players_Dlg->pBeginActiveWidgetList, pBuf);
  
  if (pShort_Players_Dlg->pScroll)
  {
    setup_vertical_scrollbar_area(pShort_Players_Dlg->pScroll,
	pWindow->size.x + pWindow->size.w - pTheme->FR_Right->w,
    	pWindow->size.y + WINDOW_TITLE_HEIGHT + 1,
    	pWindow->size.h - (pTheme->FR_Bottom->h + WINDOW_TITLE_HEIGHT + 1), TRUE);
  }
  
  /* -------------------- */
  /* redraw */
  redraw_group(pShort_Players_Dlg->pBeginWidgetList, pWindow, 0);
  widget_mark_dirty(pWindow);
  
  flush_dirty();
}

/**************************************************************************
  Popdown the short player list dialog version.
**************************************************************************/
void popdown_players_nations_dialog(void)
{
  if (pShort_Players_Dlg) {
    popdown_window_group_dialog(pShort_Players_Dlg->pBeginWidgetList,
			pShort_Players_Dlg->pEndWidgetList);
    FC_FREE(pShort_Players_Dlg->pScroll);
    FC_FREE(pShort_Players_Dlg);
  }
}
