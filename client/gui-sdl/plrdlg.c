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
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL/SDL.h>

#include "fcintl.h"

#include "game.h"
#include "packets.h"
#include "nation.h"
#include "player.h"
#include "support.h"

#include "chatline.h"
#include "civclient.h"
#include "climisc.h"
#include "clinet.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_id.h"
#include "gui_tilespec.h"
#include "gui_string.h"
#include "gui_stuff.h"
#include "gui_zoom.h"
#include "inteldlg.h"
#include "spaceshipdlg.h"
#include "colors.h"
#include "mapview.h"
#include "diplodlg.h"
#include "plrdlg.h"

#ifndef M_PI
#define M_PI		3.14159265358979323846	/* pi */
#endif
#ifndef M_PI_2
#define M_PI_2		1.57079632679489661923	/* pi/2 */
#endif

static struct SMALL_DLG  *pPlayers_Dlg = NULL;

static int exit_players_dlg_callback(struct GUI *pWidget)
{
  popdown_players_dialog();
  flush_dirty();
  return -1;
}

static int player_callback(struct GUI *pWidget)
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

static int players_window_dlg_callback(struct GUI *pWindow)
{
  if (move_window_group_dialog(pPlayers_Dlg->pBeginWidgetList, pWindow)) {
    sellect_window_group_dialog(pPlayers_Dlg->pBeginWidgetList, pWindow);
    update_players_dialog();
  } else {
    if(sellect_window_group_dialog(pPlayers_Dlg->pBeginWidgetList, pWindow)) {
      flush_rect(pWindow->size);
    }      
  }
  return -1;
}

static int toggle_draw_war_status_callback(struct GUI *pWidget)
{
  /* exit button -> neutral -> war -> casefire -> peace -> alliance */
  struct GUI *pPlayer = pPlayers_Dlg->pEndWidgetList->prev->prev->prev->prev->prev->prev;
  SDL_Client_Flags ^= CF_DRAW_PLAYERS_WAR_STATUS;
  do{
    pPlayer = pPlayer->prev;
    FREESURFACE(pPlayer->gfx);
  } while(pPlayer != pPlayers_Dlg->pBeginWidgetList);
  update_players_dialog();
  return -1;
}

static int toggle_draw_ceasefire_status_callback(struct GUI *pWidget)
{
  /* exit button -> neutral -> war -> casefire -> peace -> alliance */
  struct GUI *pPlayer = pPlayers_Dlg->pEndWidgetList->prev->prev->prev->prev->prev->prev;
  SDL_Client_Flags ^= CF_DRAW_PLAYERS_CEASEFIRE_STATUS;
  do{
    pPlayer = pPlayer->prev;
    FREESURFACE(pPlayer->gfx);
  } while(pPlayer != pPlayers_Dlg->pBeginWidgetList);
  update_players_dialog();
  return -1;
}

static int toggle_draw_pease_status_callback(struct GUI *pWidget)
{
  /* exit button -> neutral -> war -> casefire -> peace -> alliance */
  struct GUI *pPlayer = pPlayers_Dlg->pEndWidgetList->prev->prev->prev->prev->prev->prev;
  SDL_Client_Flags ^= CF_DRAW_PLAYERS_PEACE_STATUS;
  do{
    pPlayer = pPlayer->prev;
    FREESURFACE(pPlayer->gfx);
  } while(pPlayer != pPlayers_Dlg->pBeginWidgetList);
  update_players_dialog();
  return -1;
}

static int toggle_draw_alliance_status_callback(struct GUI *pWidget)
{
  /* exit button -> neutral -> war -> casefire -> peace -> alliance */
  struct GUI *pPlayer = pPlayers_Dlg->pEndWidgetList->prev->prev->prev->prev->prev->prev;
  SDL_Client_Flags ^= CF_DRAW_PLAYERS_ALLIANCE_STATUS;
  do{
    pPlayer = pPlayer->prev;
    FREESURFACE(pPlayer->gfx);
  } while(pPlayer != pPlayers_Dlg->pBeginWidgetList);
  update_players_dialog();
  return -1;
}

static int toggle_draw_neutral_status_callback(struct GUI *pWidget)
{
  /* exit button -> neutral -> war -> casefire -> peace -> alliance */
  struct GUI *pPlayer = pPlayers_Dlg->pEndWidgetList->prev->prev->prev->prev->prev->prev;
  
  SDL_Client_Flags ^= CF_DRAW_PLAYERS_NEUTRAL_STATUS;
  do{
    pPlayer = pPlayer->prev;
    FREESURFACE(pPlayer->gfx);
  } while(pPlayer != pPlayers_Dlg->pBeginWidgetList);
  update_players_dialog();
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
    struct GUI *pPlayer0, *pPlayer1;
    struct player *pPlayer;
    char cBuf[128], *state, *team;
    int idle, x0, y0, x1, y1;
          
    /* redraw window */
    redraw_widget(pPlayers_Dlg->pEndWidgetList);
    
    /* exit button -> neutral -> war -> casefire -> peace -> alliance */
    pPlayer0 = pPlayers_Dlg->pEndWidgetList->prev->prev->prev->prev->prev->prev;
    do{
      pPlayer0 = pPlayer0->prev;
      pPlayer1 = pPlayer0;
      pPlayer = pPlayer0->data.player;
      
      /* the team */
      if (pPlayer->team != TEAM_NONE) {
        team = team_get_by_id(pPlayer->team)->name;
      } else {
        team = _("none");
      }
            
      if(pPlayer->is_alive) {
        if (pPlayer->ai.control) {
	  state = _("AI");
        } else {
          if (pPlayer->is_connected) {
            if (pPlayer->turn_done) {
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
      					"Reputation : %s\nEmbassy :%s\n"
    					"State : %s\nIdle : %d %s"),
                   pPlayer->name, get_nation_name(pPlayer->nation),
		   team, reputation_text(pPlayer->reputation),
                   get_embassy_status(game.player_ptr, pPlayer),
		   state, idle, PL_("turn", "turns", idle));
      
      copy_chars_to_string16(pPlayer0->string16, cBuf);
          
      /* now add some eye candy ... */
      if(pPlayer1 != pPlayers_Dlg->pBeginWidgetList) {
	x0 = pPlayer0->size.x + pPlayer0->size.w / 2;
        y0 = pPlayer0->size.y + pPlayer0->size.h / 2;
        do{
          pPlayer1 = pPlayer1->prev;
	  if (have_diplomat_info_about(pPlayer) ||
	     have_diplomat_info_about(pPlayer1->data.player)) {
            x1 = pPlayer1->size.x + pPlayer1->size.w / 2;
            y1 = pPlayer1->size.y + pPlayer1->size.h / 2;
            switch (pplayer_get_diplstate(pPlayer, pPlayer1->data.player)->type) {
	      case DS_NEUTRAL:
	        if(SDL_Client_Flags & CF_DRAW_PLAYERS_NEUTRAL_STATUS) {
	          putline(pPlayer1->dst, x0, y0, x1, y1, 0xFF000000);
	        }
	      break;
              case DS_WAR:
	        if(SDL_Client_Flags & CF_DRAW_PLAYERS_WAR_STATUS) {
	          putline(pPlayer1->dst, x0, y0, x1, y1,
	    		get_game_color(COLOR_STD_RED, pPlayer1->dst));
	        }
              break;
	      case DS_CEASEFIRE:
	        if (SDL_Client_Flags & CF_DRAW_PLAYERS_CEASEFIRE_STATUS) {
	          putline(pPlayer1->dst, x0, y0, x1, y1,
	    		get_game_color(COLOR_STD_YELLOW, pPlayer1->dst));
	        }
              break;
              case DS_PEACE:
	        if (SDL_Client_Flags & CF_DRAW_PLAYERS_PEACE_STATUS) {
	          putline(pPlayer1->dst, x0, y0, x1, y1,
	    		get_game_color(COLOR_STD_GROUND, pPlayer1->dst));
	        }
              break;
	      case DS_ALLIANCE:
	        if (SDL_Client_Flags & CF_DRAW_PLAYERS_ALLIANCE_STATUS) {
	          putline(pPlayer1->dst, x0, y0, x1, y1,
	    		get_game_color(COLOR_STD_CITY_GOLD, pPlayer1->dst));
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
    sdl_dirty_rect(pPlayers_Dlg->pEndWidgetList->size);
  
    flush_dirty();
  }
}

/**************************************************************************
  Popup (or raise) the player list dialog.
**************************************************************************/
void popup_players_dialog(void)
{
  struct GUI *pWindow = NULL, *pBuf = NULL;
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
  for(i=0; i<game.nplayers; i++) {
    if(is_barbarian(get_player(i))) {
      continue;
    }
    n++;
  }

  if(n < 2) {
    return;
  }
    
  pPlayers_Dlg = MALLOC(sizeof(struct SMALL_DLG));
  
  pStr = create_str16_from_char(_("Players"), 12);
  pStr->style |= TTF_STYLE_BOLD;
  
  pWindow = create_window(NULL, pStr, 10, 10, WF_DRAW_THEME_TRANSPARENT);
    
  pWindow->action = players_window_dlg_callback;
  set_wstate(pWindow, FC_WS_NORMAL);
    
  add_to_gui_list(ID_WINDOW, pWindow);
  pPlayers_Dlg->pEndWidgetList = pWindow;
  /* ---------- */
  /* exit button */
  pBuf = create_themeicon(pTheme->Small_CANCEL_Icon, pWindow->dst,
  			  			WF_DRAW_THEME_TRANSPARENT);
  pBuf->action = exit_players_dlg_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  
  add_to_gui_list(ID_BUTTON, pBuf);
  /* ---------- */
  
  for(i = 0; i<DS_LAST; i++) {
    switch (i) {
      case DS_NEUTRAL:
	pBuf = create_checkbox(pWindow->dst,
		(SDL_Client_Flags & CF_DRAW_PLAYERS_NEUTRAL_STATUS),
      						WF_DRAW_THEME_TRANSPARENT);
	pBuf->action = toggle_draw_neutral_status_callback;
	pBuf->key = SDLK_n;
      break;
      case DS_WAR:
	pBuf = create_checkbox(pWindow->dst,
		(SDL_Client_Flags & CF_DRAW_PLAYERS_WAR_STATUS),
      						WF_DRAW_THEME_TRANSPARENT);
	pBuf->action = toggle_draw_war_status_callback;
	pBuf->key = SDLK_w;
      break;
      case DS_CEASEFIRE:
	pBuf = create_checkbox(pWindow->dst,
		(SDL_Client_Flags & CF_DRAW_PLAYERS_CEASEFIRE_STATUS),
      						WF_DRAW_THEME_TRANSPARENT);
	pBuf->action = toggle_draw_ceasefire_status_callback;
	pBuf->key = SDLK_c;
      break;
      case DS_PEACE:
	pBuf = create_checkbox(pWindow->dst,
		(SDL_Client_Flags & CF_DRAW_PLAYERS_PEACE_STATUS),
      						WF_DRAW_THEME_TRANSPARENT);
	pBuf->action = toggle_draw_pease_status_callback;
	pBuf->key = SDLK_p;
      break;
      case DS_ALLIANCE:
	pBuf = create_checkbox(pWindow->dst,
		(SDL_Client_Flags & CF_DRAW_PLAYERS_ALLIANCE_STATUS),
      						WF_DRAW_THEME_TRANSPARENT);
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
  
  for(i=0; i<game.nplayers; i++) {
    pPlayer = get_player(i);
      
    if(is_barbarian(pPlayer)) {
      continue;
    }
                
    pStr = create_string16(NULL, 0, 10);
    pStr->style |= (TTF_STYLE_BOLD|SF_CENTER);
   
    pLogo = GET_SURF(get_nation_by_idx(pPlayer->nation)->flag_sprite);
    pLogo = make_flag_surface_smaler(pLogo);
    pZoomed = ZoomSurface(pLogo, 3.0 - n * 0.05, 3.0 - n * 0.05 , 1);
    SDL_SetColorKey(pZoomed, SDL_SRCCOLORKEY|SDL_RLEACCEL,
    			getpixel(pZoomed, pZoomed->w - 1, pZoomed->h - 1));
    FREESURFACE(pLogo);
            
    pBuf = create_icon2(pZoomed, pWindow->dst,
    	(WF_DRAW_THEME_TRANSPARENT|WF_WIDGET_HAS_INFO_LABEL|WF_FREE_THEME));
    
    pBuf->string16 = pStr;
      
    if(!pPlayer->is_alive) {
      pStr = create_str16_from_char("R.I.P" , 10);
      pStr->style |= TTF_STYLE_BOLD;
      pStr->fgcol = *(get_game_colorRGB(COLOR_STD_WHITE));
      pLogo = create_text_surf_from_str16(pStr);
      FREESTRING16(pStr);
	
      dst.x = (pZoomed->w - pLogo->w) / 2;
      dst.y = (pZoomed->h - pLogo->h) / 2;
      SDL_BlitSurface(pLogo, NULL, pZoomed, &dst);
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

  w = 500;
  h = 400;
  r = MIN(w,h);
  r -= ((MAX(pBuf->size.w, pBuf->size.h) * 2) + WINDOW_TILE_HIGH + FRAME_WH);
  r /= 2;
  a = (2.0 * M_PI) / n;
   
  pWindow->size.x = (Main.screen->w - w) / 2;
  pWindow->size.y = (Main.screen->h - h) / 2;
  
  resize_window(pWindow, NULL, NULL, w, h);
  
  putframe(pWindow->theme, 0, 0, w - 1, h - 1, 0xFFFFFFFF);
  
  /* exit button */
  pBuf = pWindow->prev;
  
  pBuf->size.x = pWindow->size.x + pWindow->size.w-pBuf->size.w-FRAME_WH-1;
  pBuf->size.y = pWindow->size.y + 1;
    
  n = WINDOW_TILE_HIGH + 4;
  pStr = create_string16(NULL, 0, 10);
  pStr->style |= TTF_STYLE_BOLD;
  pStr->render = 3;
  pStr->bgcol.unused = 128;
  for(i = 0; i<DS_LAST; i++) {
      switch (i) {
	case DS_NEUTRAL:
	  pStr->fgcol = *(get_game_colorRGB(COLOR_STD_BLACK));
	break;
        case DS_WAR:
	  pStr->fgcol = *(get_game_colorRGB(COLOR_STD_RED));
	break;
	case DS_CEASEFIRE:
	  pStr->fgcol = *(get_game_colorRGB(COLOR_STD_YELLOW));
	break;
        case DS_PEACE:
	  pStr->fgcol = *(get_game_colorRGB(COLOR_STD_GROUND));
        break;
	case DS_ALLIANCE:
	  pStr->fgcol = *(get_game_colorRGB(COLOR_STD_CITY_GOLD));
	break;
        default:
	   /* no contact */
	   continue;
        break;
      }
      
      copy_chars_to_string16(pStr, diplstate_text(i));
      pLogo = create_text_surf_from_str16(pStr);
      SDL_SetAlpha(pLogo, 0x0, 0x0);
  
      pBuf = pBuf->prev;
      h = MAX(pBuf->size.h, pLogo->h);
      pBuf->size.x = pWindow->size.x + 5;
      pBuf->size.y = pWindow->size.y + n + (h - pBuf->size.h) / 2;
      
      dst.x = 5 + pBuf->size.w + 6;
      dst.y = n + (h - pLogo->h) / 2;
      SDL_BlitSurface(pLogo, NULL, pWindow->theme, &dst);
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
    FREE(pPlayers_Dlg);
  }
}


/* ============================== SHORT =============================== */
static struct ADVANCED_DLG  *pShort_Players_Dlg = NULL;

static int players_nations_window_dlg_callback(struct GUI *pWindow)
{
  return -1;
}

static int exit_players_nations_dlg_callback(struct GUI *pWidget)
{
  popdown_players_nations_dialog();
  flush_dirty();
  return -1;
}

static int player_nation_callback(struct GUI *pWidget)
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
      if(pPlayer->player_no != game.player_idx) {
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
  struct GUI *pWindow = NULL, *pBuf = NULL;
  SDL_Surface *pLogo = NULL;
  SDL_String16 *pStr;
  char cBuf[128], *state;
  int i, n = 0, w = 0, h, units_h = 0;
  struct player *pPlayer;
  const struct player_diplstate *pDS;
  
  if (pShort_Players_Dlg) {
    return;
  }
     
  h = WINDOW_TILE_HIGH + 3 + FRAME_WH;
      
  pShort_Players_Dlg = MALLOC(sizeof(struct ADVANCED_DLG));
  
  pStr = create_str16_from_char(_("Nations") , 12);
  pStr->style |= TTF_STYLE_BOLD;
  
  pWindow = create_window(NULL, pStr, 10, 10, WF_DRAW_THEME_TRANSPARENT);
    
  pWindow->action = players_nations_window_dlg_callback;
  set_wstate(pWindow, FC_WS_NORMAL);
  w = MAX(w, pWindow->size.w);
  
  add_to_gui_list(ID_WINDOW, pWindow);
  pShort_Players_Dlg->pEndWidgetList = pWindow;
  /* ---------- */
  /* exit button */
  pBuf = create_themeicon(pTheme->Small_CANCEL_Icon, pWindow->dst,
  			  			WF_DRAW_THEME_TRANSPARENT);
  w += pBuf->size.w + 10;
  pBuf->action = exit_players_nations_dlg_callback;
  set_wstate(pBuf, FC_WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  
  add_to_gui_list(ID_BUTTON, pBuf);
  /* ---------- */
  
  for(i=0; i<game.nplayers; i++) {
    if(i != game.player_idx) {
      pPlayer = get_player(i);
      
      if(!pPlayer->is_alive || is_barbarian(pPlayer)) {
        continue;
      }
      
      pDS = pplayer_get_diplstate(game.player_ptr, pPlayer);
            
      if(pPlayer->ai.control) {
	state = _("AI");
      } else {
        if (pPlayer->is_connected) {
          if (pPlayer->turn_done) {
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
      
      pStr = create_str16_from_char(cBuf, 10);
      pStr->style |= TTF_STYLE_BOLD;
   
      pLogo = GET_SURF(get_nation_by_idx(pPlayer->nation)->flag_sprite);
      pLogo = make_flag_surface_smaler(pLogo);
      
      pBuf = create_iconlabel(pLogo, pWindow->dst, pStr, 
    	(WF_FREE_THEME|WF_DRAW_THEME_TRANSPARENT|WF_DRAW_TEXT_LABEL_WITH_SPACE));
                      
      /* now add some eye candy ... */
      switch (pDS->type) {
        case DS_WAR:
	  if(can_meet_with_player(pPlayer) || can_intel_with_player(pPlayer)) {
            set_wstate(pBuf, FC_WS_NORMAL);
	    pBuf->string16->fgcol = *(get_game_colorRGB(COLOR_STD_RED));
          } else {
	    pBuf->string16->fgcol = *(get_game_colorRGB(COLOR_STD_RED_DISABLED));
	  }
        break;
	case DS_CEASEFIRE:
	  pBuf->string16->fgcol = *(get_game_colorRGB(COLOR_STD_YELLOW));
	  set_wstate(pBuf, FC_WS_NORMAL);
        break;
        case DS_PEACE:
	  pBuf->string16->fgcol = *(get_game_colorRGB(COLOR_STD_GROUND));
	  set_wstate(pBuf, FC_WS_NORMAL);
        break;
	case DS_ALLIANCE:
	  pBuf->string16->fgcol = *(get_game_colorRGB(COLOR_STD_CITY_GOLD));
	  set_wstate(pBuf, FC_WS_NORMAL);
        break;
	case DS_NO_CONTACT:
	  pBuf->string16->fgcol = *(get_game_colorRGB(COLOR_STD_DISABLED));
	break;
        default:
	  set_wstate(pBuf, FC_WS_NORMAL);
        break;
      }
      
      pBuf->string16->render = 3;
      pBuf->string16->bgcol.unused = 128;
    
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
  pShort_Players_Dlg->pBeginActiveWidgetList = pBuf;
  pShort_Players_Dlg->pEndActiveWidgetList = pWindow->prev->prev;
  pShort_Players_Dlg->pActiveWidgetList = pShort_Players_Dlg->pEndActiveWidgetList;
  
  
  /* ---------- */
  if (n > 20)
  {
     
    units_h = create_vertical_scrollbar(pShort_Players_Dlg, 1, 20, TRUE, TRUE);
    pShort_Players_Dlg->pScroll->count = n;
    
    n = units_h;
    w += n;
    
    units_h = 20 * pBuf->size.h + WINDOW_TILE_HIGH + 3 + FRAME_WH;
    
  } else {
    units_h = h;
  }
        
  /* ---------- */
  
  w += DOUBLE_FRAME_WH;
  
  h = units_h;

  pWindow->size.x = ((Main.event.motion.x + w < pWindow->dst->w) ?
                     (Main.event.motion.x + 10) : (pWindow->dst->w - w - 10));
  pWindow->size.y = 
      ((Main.event.motion.y - (WINDOW_TILE_HIGH + 2) + h < pWindow->dst->h) ?
             (Main.event.motion.y - (WINDOW_TILE_HIGH + 2)) :
             (pWindow->dst->h - h - 10));
      
  resize_window(pWindow, NULL, NULL, w, h);
  
  w -= DOUBLE_FRAME_WH;
  
  if (pShort_Players_Dlg->pScroll)
  {
    w -= n;
  }
  
  /* exit button */
  pBuf = pWindow->prev;
  pBuf->size.x = pWindow->size.x + pWindow->size.w-pBuf->size.w-FRAME_WH-1;
  pBuf->size.y = pWindow->size.y + 1;
  
  /* cities */
  pBuf = pBuf->prev;
  setup_vertical_widgets_position(1,
	pWindow->size.x + FRAME_WH, pWindow->size.y + WINDOW_TILE_HIGH + 2,
	w, 0, pShort_Players_Dlg->pBeginActiveWidgetList, pBuf);
  
  if (pShort_Players_Dlg->pScroll)
  {
    setup_vertical_scrollbar_area(pShort_Players_Dlg->pScroll,
	pWindow->size.x + pWindow->size.w - FRAME_WH,
    	pWindow->size.y + WINDOW_TILE_HIGH + 1,
    	pWindow->size.h - (FRAME_WH + WINDOW_TILE_HIGH + 1), TRUE);
  }
  
  /* -------------------- */
  /* redraw */
  redraw_group(pShort_Players_Dlg->pBeginWidgetList, pWindow, 0);
  sdl_dirty_rect(pWindow->size);
  
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
    FREE(pShort_Players_Dlg->pScroll);
    FREE(pShort_Players_Dlg);
  }
}
