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
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafa³ Bursig <bursig@poczta.fm>
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

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

#include "mapview.h"
#include "mapctrl.h"
#include "repodlgs_common.h"
#include "repodlgs.h"


static SDL_Surface *pNone_Tech_Icon;

/* ============================================================= */

void setup_auxiliary_tech_icons(void)
{
  SDL_Surface *pSurf;
  SDL_String16 *pStr = create_str16_from_char(_("None"), 10);
  
  /* create "None" icon */
  pNone_Tech_Icon = create_surf( 50 , 50 , SDL_SWSURFACE );
  SDL_FillRect(pNone_Tech_Icon, NULL,
	  SDL_MapRGB(pNone_Tech_Icon->format, 255 , 255 , 255));
  putframe(pNone_Tech_Icon, 0 , 0,
	  pNone_Tech_Icon->w - 1, pNone_Tech_Icon->h - 1 , 0x0);
  
  pStr->style |= (TTF_STYLE_BOLD | SF_CENTER);
  
  pSurf = create_text_surf_from_str16(pStr);
    
  blit_entire_src(pSurf, pNone_Tech_Icon ,
	  (50 - pSurf->w) / 2 , (50 - pSurf->h) / 2);
    
  FREESURFACE(pSurf);
  FREESTRING16(pStr);
}

void free_auxiliary_tech_icons(void)
{
  FREESURFACE(pNone_Tech_Icon);
}

SDL_Surface * create_sellect_tech_icon(SDL_String16 *pStr, int tech_id)
{
  char cBuf[128], *pTmp = NULL;
  struct impr_type *pImpr = NULL;
  struct unit_type *pUnit = NULL;
  SDL_Surface *pSurf, *pText;
  SDL_Surface *Surf_Array[5], **pBuf_Array;
  SDL_Rect src, dst;
  int w, h;
  
  pSurf = create_surf(100 , 200 , SDL_SWSURFACE);
  pText = SDL_DisplayFormatAlpha(pSurf);
  FREESURFACE(pSurf);
  pSurf = pText;
      
  if (game.player_ptr->research.researching == tech_id)
  {
    SDL_FillRect(pSurf, NULL,
		   SDL_MapRGBA(pSurf->format, 255, 255, 255, 180));
  } else {
    SDL_FillRect(pSurf, NULL,
		   SDL_MapRGBA(pSurf->format, 255, 255, 255, 128));
  }

  if (!pStr->text) {
    pStr->text = convert_to_utf16(advances[tech_id].name);
  }

  pText = create_text_surf_from_str16(pStr);

  if (pText->w >= 100) {
    FREE(pStr->text);
    FREESURFACE(pText);

    pTmp = cBuf;
    my_snprintf(cBuf, sizeof(cBuf), advances[tech_id].name);

    while (*pTmp != 0 && *pTmp != 32) {
      /* Inefficient and dangerous! */
     /* Make Better */
     pTmp++;
    }

    if (*pTmp == 32) {
	*pTmp = 10;/*"\n"*/
    }

    pStr->text = convert_to_utf16(cBuf);
    pText = create_text_surf_from_str16(pStr);
  }/* pText->w >= 100 */

  FREE(pStr->text);

  /* draw name tech text */
  dst.x = (100 - pText->w) / 2;
  dst.y = 25;
  SDL_BlitSurface(pText, NULL, pSurf, &dst);

  dst.y += pText->h + 10;

  FREESURFACE(pText);

  /* draw tech icon */
  pText = GET_SURF(advances[tech_id].sprite);
  dst.x = (100 - pText->w) / 2;
  SDL_BlitSurface(pText, NULL, pSurf, &dst);

  dst.y += pText->w + 10;

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
      dst.x = (100 - 2 * Surf_Array[0]->w) / 2;
    } else {
      dst.x = (100 - Surf_Array[0]->w) / 2;
    }

    /* draw iprvm. icons */
    pBuf_Array = Surf_Array;
    h = 0;
    while (w) {
      SDL_BlitSurface(*pBuf_Array, NULL, pSurf, &dst);
      dst.x += (*pBuf_Array)->w;
      w--;
      h++;
      if (h == 2) {
        if (w >= 2) {
          dst.x = (100 - 2 * (*pBuf_Array)->w) / 2;
        } else {
          dst.x = (100 - (*pBuf_Array)->w) / 2;
        }
        dst.y += (*pBuf_Array)->h;
        h = 0;
      }	/* h == 2 */
      pBuf_Array++;
    }	/* while */
    dst.y += Surf_Array[0]->h + 10;
  } /* if (w) */
  
  w = 0;
  unit_type_iterate(un) {
    pUnit = get_unit_type(un);
    if (pUnit->tech_requirement == tech_id) {
      Surf_Array[w++] = GET_SURF(pUnit->sprite);
    }
  } unit_type_iterate_end;

  if (w) {
    src = get_smaller_surface_rect(Surf_Array[0]);
    if (w >= 2) {
      dst.x = (100 - 2 * src.w) / 2;
    } else {
      dst.x = (100 - src.w) / 2;
    }

    pBuf_Array = Surf_Array;
    h = 0;
    while (w) {
      src = get_smaller_surface_rect(*pBuf_Array);
      SDL_BlitSurface(*pBuf_Array, &src, pSurf, &dst);
      dst.x += src.w + 2;
      w--;
      h++;
      if (h == 2) {
        if (w >= 2) {
          dst.x = (100 - 2 * src.w) / 2;
        } else {
          dst.x = (100 - src.w) / 2;
        }
        dst.y += src.h + 2;
        h = 0;
      }	/* h == 2 */
      pBuf_Array++;
    }	/* while */
  }/* if (w) */
  
  
  return pSurf;
}

#if 0
/**************************************************************************
  ...
**************************************************************************/
void report_update_delay_set(bool delay)
{
  freelog(LOG_DEBUG, "report_update_delay_set");
}
#endif

/**************************************************************************
  ...
**************************************************************************/
char *get_report_title(char *report_name)
{
  char *tmp = "abra cadabra";
  freelog(LOG_DEBUG, "get_report_title : PORT ME");
  return tmp;
}

/**************************************************************************
  Update all report dialogs.
**************************************************************************/
void update_report_dialogs(void)
{
  freelog(LOG_DEBUG, "update_report_dialogs : PORT ME");
}

/**************************************************************************
  Update the economy report.
**************************************************************************/
void economy_report_dialog_update(void)
{
  freelog(LOG_DEBUG, "economy_report_dialog_update : PORT ME");
}

/**************************************************************************
  Popup (or raise) the economy report (F5).  It may or may not be modal.
**************************************************************************/
void popup_economy_report_dialog(bool make_modal)
{
  freelog(LOG_DEBUG, "popup_economy_report_dialog : PORT ME");
}

/**************************************************************************
  Update the units report.
**************************************************************************/
void activeunits_report_dialog_update(void)
{
  freelog(LOG_DEBUG, "activeunits_report_dialog_update : PORT ME");
}

/**************************************************************************
  Popup (or raise) the units report (F2).  It may or may not be modal.
**************************************************************************/
void popup_activeunits_report_dialog(bool make_modal)
{
  freelog(LOG_DEBUG, "popup_activeunits_report_dialog : PORT ME");
}

/*************************************************************************/
/*************************************************************************/
/*************************************************************************/
static struct SMALL_DLG *pScienceDlg = NULL;

static struct ADVANCED_DLG *pChangeTechDlg = NULL;

/**************************************************************************
  enable science dialog group ( without window )
**************************************************************************/
static void enable_science_dialog(void)
{
  set_group_state(pScienceDlg->pBeginWidgetList,
		     pScienceDlg->pEndWidgetList->prev, WS_NORMAL);
}

/**************************************************************************
  disable science dialog group ( without window )
**************************************************************************/
static void disable_science_dialog(void)
{
  set_group_state(pScienceDlg->pBeginWidgetList,
		     pScienceDlg->pEndWidgetList->prev, WS_DISABLED);
}

/**************************************************************************
  Update the science report.
**************************************************************************/
void science_dialog_update(void)
{
  char cBuf[120];
  SDL_String16 *pStr;
  int cost;
  SDL_Surface *pSurf, *pColb_Surface = pIcons->pBIG_Colb;
  int step, i;
  SDL_Rect dest, src;
  SDL_Color color;
  struct impr_type *pImpr;
  struct unit_type *pUnit;
  int turns_to_advance, turns_to_next_tech, steps;
  int curent_output = 0;
  div_t result;
  struct GUI *pWindow;
  color.unused = 128;

  if(!pScienceDlg) {
    return;
  }
  
  pWindow = pScienceDlg->pEndWidgetList;
  color = *get_game_colorRGB(COLOR_STD_WHITE);
  cost = total_bulbs_required(game.player_ptr);
  
  pWindow->prev->theme =
    GET_SURF(advances[game.player_ptr->research.researching].sprite);
  
  if ( game.player_ptr->ai.tech_goal != A_UNSET )
  {
    pWindow->prev->prev->theme =
      GET_SURF(advances[game.player_ptr->ai.tech_goal].sprite);
  } else {
    /* add "None" icon */
    pWindow->prev->prev->theme = pNone_Tech_Icon;
  }
  
  redraw_group(pScienceDlg->pBeginWidgetList, pWindow, 0);
  
  putframe(pWindow->dst, pWindow->size.x, pWindow->size.y,
	  	pWindow->size.x + pWindow->size.w - 1,
		  	pWindow->size.y + pWindow->size.h - 1, 0xffffffff);
  
  /* ------------------------------------- */

  city_list_iterate(game.player_ptr->cities, pCity) {
    curent_output += pCity->science_total;
  } city_list_iterate_end;

  if (curent_output <= 0) {
    my_snprintf(cBuf, sizeof(cBuf),
		_("Current output : 0\nResearch speed : "
		  "none\nNext's advance time : never"));
  } else {
    result = div(cost, curent_output);
    turns_to_advance = result.quot;
    if (result.rem) {
      turns_to_advance++;
    }
    result =
	div(cost - game.player_ptr->research.bulbs_researched - 1,
	    curent_output);
    turns_to_next_tech = result.quot;
    if (result.rem) {
      turns_to_next_tech++;
    }
    my_snprintf(cBuf, sizeof(cBuf),
		_("Current output : %d per turn\nResearch speed "
		  ": %d %s/advance\nNext advance in: "
		  "%d %s"),
		curent_output, turns_to_advance,
		PL_("turn", "turns", turns_to_advance),
		turns_to_next_tech, PL_("turn", "turns", turns_to_next_tech));
  }

  pStr = create_str16_from_char(cBuf, 12);
  pStr->style |= SF_CENTER;
  pStr->forecol = color;
  
  pSurf = create_text_surf_from_str16(pStr);
  
  FREE(pStr->text);

  dest.x = pWindow->size.x + (pWindow->size.w - pSurf->w) / 2;
  dest.y = pWindow->size.y + WINDOW_TILE_HIGH + 2;
  SDL_BlitSurface(pSurf, NULL, pWindow->dst, &dest);

  dest.y += pSurf->h + 2;
  FREESURFACE(pSurf);


  /* ------------------------------------- */
  dest.x = pWindow->prev->size.x;

  putline(pWindow->dst, dest.x, dest.y, dest.x + 365, dest.y, 0xff000000);

  dest.y += 6;
  /* ------------------------------------- */

  my_snprintf(cBuf, sizeof(cBuf), "%s (%d/%d)",
	      get_tech_name(game.player_ptr,
			    game.player_ptr->research.researching),
	      game.player_ptr->research.bulbs_researched, cost);

  pStr->text = convert_to_utf16(cBuf);

  pSurf = create_text_surf_from_str16(pStr);
  FREE(pStr->text);

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
  
  SDL_FillRectAlpha(pWindow->dst, &dest, &color);
  
  putframe(pWindow->dst, dest.x - 1, dest.y - 1, dest.x + dest.w,
  	dest.y + dest.h, 0xff000000);
  
  if ( cost > 286 )
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

  impr_type_iterate(imp)
      pImpr = get_improvement_type(imp);
  if (pImpr->tech_req == game.player_ptr->research.researching) {
    SDL_BlitSurface(GET_SURF(pImpr->sprite), NULL, pWindow->dst, &dest);
    dest.x += GET_SURF(pImpr->sprite)->w + 1;
  } impr_type_iterate_end;

  dest.x += 5;

  unit_type_iterate(un)
      pUnit = get_unit_type(un);
  if (pUnit->tech_requirement == game.player_ptr->research.researching) {
    src = get_smaller_surface_rect(GET_SURF(pUnit->sprite));
    SDL_BlitSurface(GET_SURF(pUnit->sprite), &src, pWindow->dst, &dest);
    dest.x += src.w + 2;
  } unit_type_iterate_end;
  
  /* -------------------------------- */
  /* draw separator line */
  dest.x = pWindow->prev->size.x;
  dest.y = pWindow->prev->size.y + pWindow->prev->size.h + 20;

  putline(pWindow->dst, dest.x, dest.y, dest.x + 365, dest.y, 0xff000000);
  dest.y += 10;
  /* -------------------------------- */
  /* Goals */
  if ( game.player_ptr->ai.tech_goal != A_UNSET )
  {
    steps =
      num_unknown_techs_for_goal(game.player_ptr,
				 game.player_ptr->ai.tech_goal);
    my_snprintf(cBuf, sizeof(cBuf), "%s ( %d %s )",
	      get_tech_name(game.player_ptr,
			    game.player_ptr->ai.tech_goal), steps,
	      PL_("step", "steps", steps));

    pStr->text = convert_to_utf16(cBuf);

    pSurf = create_text_surf_from_str16(pStr);
    FREE(pStr->text);

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
        src = get_smaller_surface_rect(GET_SURF(pUnit->sprite));
        SDL_BlitSurface(GET_SURF(pUnit->sprite), &src, pWindow->dst, &dest);
        dest.x += src.w + 2;
      }
    } unit_type_iterate_end;
  }
  
  /* -------------------------------- */
  sdl_dirty_rect(pWindow->size);
  flush_dirty();
  
  FREESTRING16(pStr);
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
    set_wstate(get_research_widget(), WS_NORMAL);
    redraw_icon2(get_research_widget());
    sdl_dirty_rect(get_research_widget()->size);
    flush_dirty();
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int change_research_callback(struct GUI *pWidget)
{
  struct packet_player_request packet;
  packet.tech = MAX_ID - pWidget->ID;
  send_packet_player_request(&aconnection, &packet,
			     PACKET_PLAYER_RESEARCH);

  lock_buffer(pWidget->dst);
  popdown_window_group_dialog(pChangeTechDlg->pBeginWidgetList,
				pChangeTechDlg->pEndWidgetList);
  unlock_buffer();
  enable_science_dialog();
  FREE(pChangeTechDlg);
  flush_dirty();
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int exit_change_research_callback(struct GUI *pWidget)
{
  lock_buffer(pWidget->dst);
  popdown_window_group_dialog(pChangeTechDlg->pBeginWidgetList, 
  				pChangeTechDlg->pEndWidgetList);
  unlock_buffer();
  enable_science_dialog();
  FREE(pChangeTechDlg);
  flush_dirty();
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int change_research_goal_dialog_callback(struct GUI *pWindow)
{
  if(sellect_window_group_dialog(pScienceDlg->pBeginWidgetList, pWindow)) {
      flush_rect(pWindow->size);
  }      
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int change_research(struct GUI *pWidget)
{
  struct GUI *pBuf = NULL; /* FIXME: possibly uninitialized */
  struct GUI *pWindow;
  SDL_String16 *pStr;
  SDL_Surface *pSurf;
  int i, count = 0, w = 0, h;

  redraw_icon2(pWidget);
  flush_rect(pWidget->size);
    
  pChangeTechDlg = MALLOC(sizeof(struct ADVANCED_DLG));
  
  pStr = create_str16_from_char(_("What should we focus on now?"), 12);
  pStr->style |= TTF_STYLE_BOLD;

  pWindow = create_window(pWidget->dst, pStr, 40, 30, 0);
  pChangeTechDlg->pEndWidgetList = pWindow;
  w = MAX(w, pWindow->size.w);
  set_wstate(pWindow, WS_NORMAL);
  pWindow->action = change_research_goal_dialog_callback;
  
  add_to_gui_list(ID_SCIENCE_DLG_CHANGE_REASARCH_WINDOW, pWindow);
  /* ------------------------- */
    /* exit button */
  pBuf = create_themeicon(ResizeSurface(pTheme->CANCEL_Icon,
			  pTheme->CANCEL_Icon->w - 4,
			  pTheme->CANCEL_Icon->h - 4, 1), pWindow->dst,
  			  (WF_FREE_THEME|WF_DRAW_THEME_TRANSPARENT));
  SDL_SetColorKey( pBuf->theme ,
	  SDL_SRCCOLORKEY|SDL_RLEACCEL , get_first_pixel(pBuf->theme));
  
  w += pBuf->size.w + 10;
  pBuf->action = exit_change_research_callback;
  set_wstate(pBuf, WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  
  add_to_gui_list(ID_TERRAIN_ADV_DLG_EXIT_BUTTON, pBuf);

  /* ------------------------- */
  pStr = create_string16(NULL, 10);
  pStr->style |= (TTF_STYLE_BOLD | SF_CENTER);

  if (!is_future_tech(game.player_ptr->research.researching)) {
    for (i = A_FIRST; i < game.num_tech_types; i++) {
      if (get_invention(game.player_ptr, i) != TECH_REACHABLE) {
	continue;
      }
      
      pSurf = create_sellect_tech_icon(pStr, i);
      pBuf = create_icon2(pSurf, pWindow->dst,
      		WF_FREE_THEME | WF_DRAW_THEME_TRANSPARENT);

      set_wstate(pBuf, WS_NORMAL);
      pBuf->action = change_research_callback;

      add_to_gui_list(MAX_ID - i, pBuf);
      count++;
    }
  }

  pChangeTechDlg->pBeginWidgetList = pBuf;

  if (count > 10)
  {
    i = 6;
    if (count > 12)
    {
      freelog( LOG_FATAL , "If you see this msg please contact me on bursig@poczta.fm and give me this : Tech12 Err");
    }
  }
  else
  {
    if (count > 8)
    {
      i = 5;
    }
    else
    {
      if (count > 6)
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
    w = MAX( w, (i * 102 + 2 + DOUBLE_FRAME_WH));
    h = WINDOW_TILE_HIGH + 1 + count * 202 + 2 + FRAME_WH;
  } else {
    w = MAX( w , (count * 102 + 2 + DOUBLE_FRAME_WH));
    h = WINDOW_TILE_HIGH + 1 + 202 + 2 + FRAME_WH;
  }

  pWindow->size.x = (Main.screen->w - w) / 2;
  pWindow->size.y = (Main.screen->h - h) / 2;

  /* redraw change button before window take background buffer */
  set_wstate(pWidget, WS_NORMAL);
  pSellected_Widget = NULL;
  redraw_icon2(pWidget);
  
  disable_science_dialog();
  
  /* alloca window theme and win background buffer */
  pSurf = get_logo_gfx();
  resize_window(pWindow, pSurf, NULL, w, h);
  FREESURFACE(pSurf);

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

    if (pBuf == pChangeTechDlg->pBeginWidgetList) {
      break;
    }
    pBuf = pBuf->prev;
  }

  redraw_group(pChangeTechDlg->pBeginWidgetList, pWindow, 0);

  flush_rect(pWindow->size);

  FREESTRING16(pStr);
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int change_research_goal_callback(struct GUI *pWidget)
{
  struct packet_player_request packet;
  packet.tech = MAX_ID - pWidget->ID;
  send_packet_player_request(&aconnection, &packet,
			     PACKET_PLAYER_TECH_GOAL);

  lock_buffer(pWidget->dst);
  popdown_window_group_dialog(pChangeTechDlg->pBeginWidgetList,
				  pChangeTechDlg->pEndWidgetList);
  FREE(pChangeTechDlg->pScroll);
  FREE(pChangeTechDlg);
  enable_science_dialog();
  unlock_buffer();
  flush_dirty();
    
  /* Following is to make the menu go back to the current goal;
   * there may be a better way to do this?  --dwp */
  science_dialog_update();
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int change_research_goal_up_callback(struct GUI *pWidget)
{
  up_advanced_dlg(pChangeTechDlg, NULL);
  
  unsellect_widget_action();
  pSellected_Widget = pWidget;
  set_wstate(pWidget, WS_SELLECTED);
  redraw_icon(pWidget);
  flush_rect(pWidget->size);
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int change_research_goal_down_callback(struct GUI *pWidget)
{
  down_advanced_dlg(pChangeTechDlg, NULL);
  
  unsellect_widget_action();
  pSellected_Widget = pWidget;
  set_wstate(pWidget, WS_SELLECTED);
  redraw_icon(pWidget);
  flush_rect(pWidget->size);
  return -1;
}

static int popdown_change_goal(struct GUI *pWidget)
{
  lock_buffer(pWidget->dst);
  popdown_window_group_dialog(pChangeTechDlg->pBeginWidgetList,
				  pChangeTechDlg->pEndWidgetList);
  unlock_buffer();
  FREE(pChangeTechDlg->pScroll);
  FREE(pChangeTechDlg);
  enable_science_dialog();
  flush_dirty();
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int change_research_goal(struct GUI *pWidget)
{
  struct GUI *pBuf = NULL; /* FIXME: possibly uninitialized */
  struct GUI *pWindow;
  SDL_String16 *pStr;
  int i, max, count = 0, w = 0, h;

  redraw_icon2(pWidget);
  flush_rect(pWidget->size);
  disable_science_dialog();
  
  pChangeTechDlg = MALLOC(sizeof(struct ADVANCED_DLG));
  
  pStr = create_str16_from_char(_("Sellect target :"), 12);
  pStr->style |= TTF_STYLE_BOLD;

  pWindow = create_window(pWidget->dst, pStr, 40, 30, 0);
  pChangeTechDlg->pEndWidgetList = pWindow;
  clear_wflag(pWindow, WF_DRAW_FRAME_AROUND_WIDGET);
  set_wstate(pWindow, WS_NORMAL);
  pWindow->action = change_research_goal_dialog_callback;
  w = MAX(w, pWindow->size.w);
  add_to_gui_list(ID_SCIENCE_DLG_CHANGE_GOAL_WINDOW, pWindow);


  h = WINDOW_TILE_HIGH + 1 + FRAME_WH;
  /* ------------------------- */
  /* exit button */
  pBuf = create_themeicon(ResizeSurface(pTheme->CANCEL_Icon,
			  pTheme->CANCEL_Icon->w - 4,
			  pTheme->CANCEL_Icon->h - 4, 1), pWindow->dst,
  			  (WF_FREE_THEME|WF_DRAW_THEME_TRANSPARENT));
  SDL_SetColorKey( pBuf->theme ,
	  SDL_SRCCOLORKEY|SDL_RLEACCEL , get_first_pixel(pBuf->theme));
  
  w += pBuf->size.w + 10;
  pBuf->action = popdown_change_goal;
  set_wstate(pBuf, WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  
  add_to_gui_list(ID_SCIENCE_DLG_CHANGE_GOAL_CANCEL_BUTTON, pBuf);
  /* ------------------------------------ */
  /* collect all techs which are reachable in under 11 steps
   * hist will hold afterwards the techid of the current choice
   */
  count = 0;
  for (i = A_FIRST; i < game.num_tech_types; i++) {
    if (get_invention(game.player_ptr, i) != TECH_KNOWN &&
	advances[i].req[0] != A_LAST && advances[i].req[1] != A_LAST &&
	num_unknown_techs_for_goal(game.player_ptr, i) < 11) {

      pStr = create_str16_from_char(advances[i].name, 10);
      pStr->style |= TTF_STYLE_BOLD;
      pStr->backcol.unused = 128;

      pBuf = create_iconlabel(NULL, pWindow->dst, pStr, 
	  	(WF_DRAW_THEME_TRANSPARENT|WF_DRAW_TEXT_LABEL_WITH_SPACE));

      pBuf->size.h += 2;

      h += pBuf->size.h;
      w = MAX(w, pBuf->size.w);
      
      set_wstate(pBuf, WS_NORMAL);
      pBuf->action = change_research_goal_callback;

      add_to_gui_list(MAX_ID - i, pBuf);
      count++;
    }
  }

  pChangeTechDlg->pBeginActiveWidgetList = pBuf;

  w += 10;

  if (h > Main.screen->h) {

    h = pBuf->size.h;

    pBuf = create_themeicon(pTheme->UP_Icon, pWindow->dst, 0);

    pBuf->size.w = w;
    pBuf->size.x = pWidget->size.x + FRAME_WH;
    pBuf->action = change_research_goal_up_callback;
    set_wstate(pBuf, WS_NORMAL);

    add_to_gui_list(ID_SCIENCE_DLG_CHANGE_GOAL_UP_BUTTON, pBuf);
    /* --------------------------------- */

    max =
	(Main.screen->h - WINDOW_TILE_HIGH - 1 - FRAME_WH -
	 (pBuf->size.h * 2)) / h;
    h = WINDOW_TILE_HIGH + 1 + FRAME_WH + (pBuf->size.h * 2) + max * h;

    pWindow->size.x = pWidget->size.x;
    pWindow->size.y = (Main.screen->h - h) / 2;

    resize_window(pWindow, NULL,
		  get_game_colorRGB(COLOR_STD_BACKGROUND_BROWN),
		  w + DOUBLE_FRAME_WH, h);

    h = WINDOW_TILE_HIGH + 1 + pBuf->size.h;

    /* --------------------------------- */
    pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 1;

    pBuf = create_themeicon(pTheme->DOWN_Icon, pWindow->dst, 0);

    pBuf->size.w = w;
    pBuf->size.x = pWidget->size.x + FRAME_WH;
    pBuf->size.y =
	pWindow->size.y + pWindow->size.h - FRAME_WH - pBuf->size.h;
    pBuf->action = change_research_goal_down_callback;
    set_wstate(pBuf, WS_NORMAL);

    add_to_gui_list(ID_SCIENCE_DLG_CHANGE_GOAL_DOWN_BUTTON, pBuf);

    

    pChangeTechDlg->pScroll = MALLOC(sizeof(struct ScrollBar));
    pChangeTechDlg->pScroll->active = max;
    pChangeTechDlg->pScroll->count = count;
    pChangeTechDlg->pScroll->min = h;
    pChangeTechDlg->pScroll->max = pBuf->size.y;
  } else {
    pWindow->size.x = pWidget->size.x;
    pWindow->size.y = (Main.screen->h - h) / 2;

    resize_window(pWindow, NULL,
		  get_game_colorRGB(COLOR_STD_BACKGROUND_BROWN),
		  w + DOUBLE_FRAME_WH, h);

    max = count;
    h = WINDOW_TILE_HIGH + 1;
  }
  
  pChangeTechDlg->pBeginWidgetList = pBuf;
  
  /* exit button */
  pBuf = pWindow->prev;
  pBuf->size.x = pWindow->size.x + pWindow->size.w-pBuf->size.w-FRAME_WH-1;
  pBuf->size.y = pWindow->size.y;
  pBuf = pBuf->prev;
  
  pChangeTechDlg->pEndActiveWidgetList = pBuf;
  pChangeTechDlg->pActiveWidgetList = pBuf;
  pBuf->size.x = pWindow->size.x + FRAME_WH;
  pBuf->size.y = pWindow->size.y + h;
  pBuf->size.w = w;
  pBuf = pBuf->prev;

  h = 2;
  while (pBuf) {
    pBuf->size.x = pBuf->next->size.x;
    pBuf->size.y = pBuf->next->size.y + pBuf->next->size.h;
    pBuf->size.w = w;

    if (h == count) {
      break;
    }

    if (h > max) {
      set_wflag(pBuf, WF_HIDDEN);
    }

    h++;
    pBuf = pBuf->prev;
  }

  pSellected_Widget = NULL;
  
  redraw_group(pChangeTechDlg->pBeginWidgetList, pWindow, 0);

  putframe(pWindow->dst, pWindow->size.x, pWindow->size.y,
	  pWindow->size.x + pWindow->size.w - 1,
		  pWindow->size.y + pWindow->size.h - 1,  0xff000000);
  
  flush_rect(pWindow->size);

  return -1;
}

static int science_dialog_callback(struct GUI *pWindow)
{
  if (move_window_group_dialog(pScienceDlg->pBeginWidgetList, pWindow)) {
    sellect_window_group_dialog(pScienceDlg->pBeginWidgetList, pWindow);
    science_dialog_update();
  } else {
    if(sellect_window_group_dialog(pScienceDlg->pBeginWidgetList, pWindow)) {
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

  if (pScienceDlg) {
    return;
  }

  set_wstate(pBuf, WS_DISABLED);
  redraw_icon2(pBuf);
  sdl_dirty_rect(pBuf->size);
  
  pScienceDlg = MALLOC(sizeof(struct SMALL_DLG));
    
  pStr = create_str16_from_char(_("Science"), 12);
  pStr->style |= TTF_STYLE_BOLD;
  
  pWindow = create_window(NULL, pStr, 400, 225, 0);
  pScienceDlg->pEndWidgetList = pWindow;

  clear_wflag(pWindow, WF_DRAW_FRAME_AROUND_WIDGET);
  pWindow->action = science_dialog_callback;
  pWindow->size.x = (Main.screen->w - 400) / 2;
  pWindow->size.y = (Main.screen->h - 225) / 2;
  pWindow->size.w = 400;
  pWindow->size.h = 225;
  set_wstate(pWindow, WS_NORMAL);
  
  pLogo = get_logo_gfx();
  pWindow->theme = ResizeSurface(pLogo, pWindow->size.w, pWindow->size.h, 1);
  FREESURFACE(pLogo);
    
  refresh_widget_background(pWindow);

  add_to_gui_list(ID_SCIENCE_DLG_WINDOW, pWindow);
  /* ------ */

  pBuf = create_icon2(GET_SURF(advances[game.player_ptr->
					research.researching].sprite),
		      pWindow->dst, WF_DRAW_THEME_TRANSPARENT);

  pBuf->action = change_research;
  set_wstate(pBuf, WS_NORMAL);

  pBuf->size.x = pWindow->size.x + 16;
  pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 60;

  add_to_gui_list(ID_SCIENCE_DLG_CHANGE_REASARCH_BUTTON, pBuf);

  /* ------ */
  if ( game.player_ptr->ai.tech_goal != A_UNSET )
  {
    pBuf = create_icon2(GET_SURF(advances[game.player_ptr->
					ai.tech_goal].sprite),
		      pWindow->dst, WF_DRAW_THEME_TRANSPARENT);
  } else {
    /* "None" icon */
    pBuf = create_icon2(pNone_Tech_Icon,
    				pWindow->dst, WF_DRAW_THEME_TRANSPARENT);
  }
  
  pBuf->action = change_research_goal;
  set_wstate(pBuf, WS_NORMAL);

  pBuf->size.x = pWindow->size.x + 16;
  pBuf->size.y =
      pWindow->size.y + WINDOW_TILE_HIGH + 60 + pBuf->size.h + 25;

  add_to_gui_list(ID_SCIENCE_DLG_CHANGE_GOAL_BUTTON, pBuf);

  /* ------ */
  /* exit button */
  pBuf = create_themeicon(ResizeSurface(pTheme->CANCEL_Icon,
			  pTheme->CANCEL_Icon->w - 4,
			  pTheme->CANCEL_Icon->h - 4, 1), pWindow->dst,
  			  (WF_FREE_THEME|WF_DRAW_THEME_TRANSPARENT));
  SDL_SetColorKey( pBuf->theme ,
	  SDL_SRCCOLORKEY|SDL_RLEACCEL , get_first_pixel(pBuf->theme));
  
  pBuf->action = popdown_science_dialog;
  set_wstate(pBuf, WS_NORMAL);
  pBuf->key = SDLK_ESCAPE;
  
  add_to_gui_list(ID_SCIENCE_CANCEL_DLG_BUTTON, pBuf);
  
  pBuf->size.x = pWindow->size.x + pWindow->size.w-pBuf->size.w-FRAME_WH-1;
  pBuf->size.y = pWindow->size.y;
    

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
    set_wstate(get_research_widget(), WS_NORMAL);
    redraw_icon2(get_research_widget());
    sdl_dirty_rect(get_research_widget()->size);
  }  
}
  
