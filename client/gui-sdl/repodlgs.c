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
#include "gui_stuff.h"
#include "helpdlg.h"
#include "optiondlg.h"
#include "citydlg.h"

#include "repodlgs_common.h"
#include "repodlgs.h"

#ifdef UNUSED
/**************************************************************************
  ...
**************************************************************************/
void report_update_delay_set(bool delay)
{
  freelog(LOG_DEBUG, _("report_update_delay_set"));
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
static struct GUI *pBeginScienceDlg;
static struct GUI *pEndScienceDlg = NULL;

static struct GUI *pBeginChangeTechDlg;
static struct GUI *pBeginActiveChangeTechDlg;
static struct GUI *pEndChangeTechDlg = NULL;
struct ScrollBar *pChangeTechScroll = NULL;

/**************************************************************************
  Update the science report.
**************************************************************************/
void science_dialog_update(void)
{
  char cBuf[120];
  SDL_String16 *pStr;
  int cost = total_bulbs_required(game.player_ptr);
  SDL_Surface *pSurf, *pColb_Surface = get_colb_surface();
  int step, i;
  SDL_Rect dest, src;
  SDL_Color color = *get_game_colorRGB(COLOR_STD_WHITE);
  struct impr_type *pImpr;
  struct unit_type *pUnit;
  int turns_to_advance, turns_to_next_tech, steps;
  int curent_output = 0;
  div_t result;

  color.unused = 128;
#if 0
  if (pEndScienceDlg->gfx) {
    dest.x = pEndScienceDlg->size.x;
    dest.y = pEndScienceDlg->size.y;
    SDL_BlitSurface(pEndScienceDlg->gfx, NULL, Main.screen, &dest);
  }
#endif

#if 0
  pEndScienceDlg->prev->theme =
      (SDL_Surface *) advances[game.player_ptr->research.researching].
      sprite;
  pEndScienceDlg->prev->prev->theme =
      (SDL_Surface *) advances[game.player_ptr->ai.tech_goal].sprite;
#else
  pEndScienceDlg->prev->theme = NULL;
  pEndScienceDlg->prev->prev->theme = NULL;
#endif

  redraw_group(pBeginScienceDlg, pEndScienceDlg, 0);
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
  pStr->style |= (TTF_STYLE_BOLD | SF_CENTER);

  pSurf = create_text_surf_from_str16(pStr);

  FREE(pStr->text);

  dest.x = pEndScienceDlg->size.x + (pEndScienceDlg->size.w - pSurf->w) / 2;
  dest.y = pEndScienceDlg->size.y + WINDOW_TILE_HIGH + 2;
  SDL_BlitSurface(pSurf, NULL, Main.screen, &dest);

  dest.y += pSurf->h + 2;
  FREESURFACE(pSurf);


  /* ------------------------------------- */
  dest.x = pEndScienceDlg->prev->size.x;

  putline(Main.screen, dest.x, dest.y, dest.x + 360, dest.y, 0x0);

  dest.y += 6;
  /* ------------------------------------- */

  my_snprintf(cBuf, sizeof(cBuf), "%s (%d/%d)",
	      get_tech_name(game.player_ptr,
			    game.player_ptr->research.researching),
	      game.player_ptr->research.bulbs_researched, cost);

  pStr->text = convert_to_utf16(cBuf);

  pSurf = create_text_surf_from_str16(pStr);

  FREE(pStr->text);

  dest.x =
      pEndScienceDlg->prev->size.x + pEndScienceDlg->prev->size.w + 10;
  SDL_BlitSurface(pSurf, NULL, Main.screen, &dest);

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

  SDL_FillRectAlpha(Main.screen, &dest, &color);

  putframe(Main.screen, dest.x - 1, dest.y - 1, dest.x + dest.w,
	   dest.y + dest.h, 0x0);

  cost =
      286.0 * ((float) game.player_ptr->research.bulbs_researched / cost);

  dest.y += 2;
  for (i = 0; i < cost; i++) {
    SDL_BlitSurface(pColb_Surface, NULL, Main.screen, &dest);
    dest.x += step;
  }

  /* ----------------------- */

  dest.y += dest.h + 4;
  dest.x =
      pEndScienceDlg->prev->size.x + pEndScienceDlg->prev->size.w + 10;

  impr_type_iterate(imp)
      pImpr = get_improvement_type(imp);
  if (pImpr->tech_req == game.player_ptr->research.researching) {
    SDL_BlitSurface((SDL_Surface *) pImpr->sprite, NULL, Main.screen,
		    &dest);
    dest.x += ((SDL_Surface *) pImpr->sprite)->w + 1;
  } impr_type_iterate_end;

  dest.x += 5;

  unit_type_iterate(un)
      pUnit = get_unit_type(un);
  if (pUnit->tech_requirement == game.player_ptr->research.researching) {
    src = get_smaller_surface_rect((SDL_Surface *) pUnit->sprite);
    SDL_BlitSurface((SDL_Surface *) pUnit->sprite, &src, Main.screen,
		    &dest);
    dest.x += src.w + 2;
  } unit_type_iterate_end;


  /* -------------------------------- */
  /* draw separator line */
  dest.x = pEndScienceDlg->prev->size.x;
  dest.y =
      pEndScienceDlg->prev->size.y + pEndScienceDlg->prev->size.h + 10;

  putline(Main.screen, dest.x, dest.y, dest.x + 365, dest.y, 0x0);
  dest.y += 10;
  /* -------------------------------- */
  /* Goals */

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

  dest.x =
      pEndScienceDlg->prev->size.x + pEndScienceDlg->prev->size.w + 10;
  SDL_BlitSurface(pSurf, NULL, Main.screen, &dest);

  dest.y += pSurf->h + 4;
  FREESURFACE(pSurf);

  impr_type_iterate(imp) {
    pImpr = get_improvement_type(imp);
    if (pImpr->tech_req == game.player_ptr->ai.tech_goal) {
      SDL_BlitSurface((SDL_Surface *) pImpr->sprite, NULL, Main.screen,
		      &dest);
      dest.x += ((SDL_Surface *) pImpr->sprite)->w + 1;
    }
  } impr_type_iterate_end;

  dest.x += 5;

  unit_type_iterate(un) {
    pUnit = get_unit_type(un);
    if (pUnit->tech_requirement == game.player_ptr->ai.tech_goal) {
      src = get_smaller_surface_rect((SDL_Surface *) pUnit->sprite);
      SDL_BlitSurface((SDL_Surface *) pUnit->sprite, &src, Main.screen,
		      &dest);
      dest.x += src.w + 2;
    }
  } unit_type_iterate_end;
  /* -------------------------------- */
  dest.x = pEndScienceDlg->size.x + FRAME_WH;
  dest.y =
      pEndScienceDlg->size.y + pEndScienceDlg->size.h -
      pBeginScienceDlg->size.h - FRAME_WH - 1;

  putline(Main.screen, dest.x, dest.y,
	  dest.x + pEndScienceDlg->size.w - DOUBLE_FRAME_WH, dest.y, 0x0);
  /* -------------------------------- */
  refresh_rect(pEndScienceDlg->size);

  FREESTRING16(pStr);
}

/**************************************************************************
  ...
**************************************************************************/
static int popdown_science_dialog(struct GUI *pButton)
{

  popdown_window_group_dialog(pBeginScienceDlg, pEndScienceDlg);
  pEndScienceDlg = NULL;

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

  popdown_window_group_dialog(pBeginChangeTechDlg, pEndChangeTechDlg);
  pEndChangeTechDlg = NULL;
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int change_research(struct GUI *pWidget)
{
  char cBuf[100], *pTmp;
  struct impr_type *pImpr;
  struct unit_type *pUnit;
  struct GUI *pBuf = NULL; /* FIXME: possibly uninitialized */
  struct GUI *pWindow;
  SDL_String16 *pStr;
  SDL_Surface *pSurf, *pText;
  SDL_Surface *Surf_Array[5], **pBuf_Array;
  SDL_Rect src, dst;
  int i, count = 0, w, h;

  redraw_icon2(pWidget);
  refresh_rect(pWidget->size);

  pStr =
      create_str16_from_char(_
			     ("What should we focus on now?"),
			     12);
  pStr->style |= TTF_STYLE_BOLD;

  pWindow = create_window(pStr, 40, 30, 0);
  pEndChangeTechDlg = pWindow;

  set_wstate(pWindow, WS_NORMAL);

  add_to_gui_list(ID_SCIENCE_DLG_CHANGE_REASARCH_WINDOW, pWindow);

  pStr = create_string16(NULL, 10);
  pStr->style |= (TTF_STYLE_BOLD | SF_CENTER);

  if (game.player_ptr->research.researching != A_NONE) {
    for (i = A_FIRST; i < game.num_tech_types; i++) {
      if (get_invention(game.player_ptr, i) != TECH_REACHABLE) {
	continue;
      }

      pSurf = SDL_CreateRGBSurface(SDL_SWSURFACE, 100, 200,
				   32, 0x000000ff, 0x0000ff00, 0x00ff0000,
				   0xff000000);

      SDL_FillRect(pSurf, NULL,
		   SDL_MapRGBA(pSurf->format, 255, 255, 255, 128));

      pStr->text = convert_to_utf16(advances[i].name);

      pText = create_text_surf_from_str16(pStr);

      if (pText->w >= 100) {
	FREE(pStr->text);
	FREESURFACE(pText);

	pTmp = cBuf;
	my_snprintf(cBuf, sizeof(cBuf), advances[i].name);

	while (*pTmp != 0 && *pTmp != 32) {
	  /* Inefficient and dangerous! */
	  pTmp++;
	}

	if (*pTmp == 32) {
	  *pTmp = 10;
	}

	pStr->text = convert_to_utf16(cBuf);
	pText = create_text_surf_from_str16(pStr);
      }

      FREE(pStr->text);

      /* draw name tech text */
      dst.x = (100 - pText->w) / 2;
      dst.y = 25;
      SDL_BlitSurface(pText, NULL, pSurf, &dst);

      dst.y += pText->h + 10;

      FREESURFACE(pText);

      /* draw tech icon */
#if 0
      pText = (SDL_Surface *) advances[i].sprite;
#else
      pText = NULL;		/* ? */
#endif
      dst.x = (100 - pText->w) / 2;
      SDL_BlitSurface(pText, NULL, pSurf, &dst);

      dst.y += pText->w + 10;

      /* fill array with iprvm. icons */
      w = 0;
      impr_type_iterate(imp) {
	pImpr = get_improvement_type(imp);
	if (pImpr->tech_req == i) {
	  Surf_Array[w++] = (SDL_Surface *) pImpr->sprite;
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
	  }			/* h == 2 */
	  pBuf_Array++;
	}			/* while */
	dst.y += Surf_Array[0]->h + 10;
      }
      /* w */
      w = 0;
      unit_type_iterate(un) {
	pUnit = get_unit_type(un);
	if (pUnit->tech_requirement == i) {
	  Surf_Array[w++] = (SDL_Surface *) pUnit->sprite;
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
	  }			/* h == 2 */
	  pBuf_Array++;
	}			/* while */

      }
      /* ------------------------------ */
      pBuf =
	  create_icon2(pSurf, WF_FREE_THEME | WF_DRAW_THEME_TRANSPARENT);

      set_wstate(pBuf, WS_NORMAL);
      pBuf->action = change_research_callback;

      add_to_gui_list(MAX_ID - i, pBuf);
      count++;
    }
  }

  pBeginChangeTechDlg = pBuf;

  if (count > 4) {
    count = (count + 3) / 4;
    w = 408 + DOUBLE_FRAME_WH;
    h = WINDOW_TILE_HIGH + 1 + count * 202 + FRAME_WH;
  } else {
    w = count * 104 + DOUBLE_FRAME_WH;
    h = WINDOW_TILE_HIGH + 1 + 202 + FRAME_WH;
  }

  pWindow->size.x = (Main.screen->w - w) / 2;
  pWindow->size.y = (Main.screen->h - h) / 2;

  pSurf = get_logo_gfx();
#if 0
  resize_window(pWindow, NULL,
		get_game_colorRGB(COLOR_STD_BACKGROUND_BROWN), w, h);
#endif
  resize_window(pWindow, pSurf, NULL, w, h);
  FREESURFACE(pSurf);

  pBuf = pWindow->prev;
  pBuf->size.x = pWindow->size.x + FRAME_WH;
  pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 1;
  pBuf = pBuf->prev;

  w = 1;
  while (1) {
    pBuf->size.x = pBuf->next->size.x + pBuf->next->size.w - 2;
    pBuf->size.y = pBuf->next->size.y;
    w++;

    if (w == 5) {
      w = 0;
      pBuf->size.x = pWindow->size.x + FRAME_WH;
      pBuf->size.y += pBuf->size.h - 2;
    }

    if (pBuf == pBeginChangeTechDlg) {
      break;
    }
    pBuf = pBuf->prev;
  }

  set_wstate(pWidget, WS_NORMAL);
  pSellected_Widget = NULL;

  redraw_icon2(pWidget);

  redraw_group(pBeginChangeTechDlg, pEndChangeTechDlg, 0);

  refresh_rect(pWindow->size);

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

  popdown_window_group_dialog(pBeginChangeTechDlg, pEndChangeTechDlg);
  pEndChangeTechDlg = NULL;

  if (pChangeTechScroll) {
    FREE(pChangeTechScroll);
  }

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
  struct GUI *pBegin = up_scroll_widget_list(NULL,
					     pChangeTechScroll,
					     pBeginActiveChangeTechDlg,
					     pBeginChangeTechDlg->next->
					     next,
					     pEndChangeTechDlg->prev);


  if (pBegin) {
    pBeginActiveChangeTechDlg = pBegin;
  }

  unsellect_widget_action();
  pSellected_Widget = pWidget;
  set_wstate(pWidget, WS_SELLECTED);
  redraw_icon(pWidget);
  refresh_rect(pWidget->size);
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int change_research_goal_down_callback(struct GUI *pWidget)
{
  struct GUI *pBegin = down_scroll_widget_list(NULL,
					       pChangeTechScroll,
					       pBeginActiveChangeTechDlg,
					       pBeginChangeTechDlg->next->
					       next,
					       pEndChangeTechDlg->prev);


  if (pBegin) {
    pBeginActiveChangeTechDlg = pBegin;
  }

  unsellect_widget_action();
  pSellected_Widget = pWidget;
  set_wstate(pWidget, WS_SELLECTED);
  redraw_icon(pWidget);
  refresh_rect(pWidget->size);
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
  refresh_rect(pWidget->size);

  pStr = create_str16_from_char(_("Wybie¿ cel :"), 12);
  pStr->style |= TTF_STYLE_BOLD;

  pWindow = create_window(pStr, 40, 30, 0);
  pEndChangeTechDlg = pWindow;

  set_wstate(pWindow, WS_NORMAL);

  add_to_gui_list(ID_SCIENCE_DLG_CHANGE_GOAL_WINDOW, pWindow);


  h = WINDOW_TILE_HIGH + 1 + FRAME_WH;

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

      pBuf = create_iconlabel(NULL, pStr, WF_DRAW_THEME_TRANSPARENT);

      pBuf->size.h += 2;

      h += pBuf->size.h;

      if (w < pBuf->size.w) {
	w = pBuf->size.w;
      }

      set_wstate(pBuf, WS_NORMAL);
      pBuf->action = change_research_goal_callback;

      add_to_gui_list(MAX_ID - i, pBuf);
      count++;
    }
  }

  pBeginChangeTechDlg = pBuf;

  w += 10;

  if (h > Main.screen->h) {

    h = pBuf->size.h;

    pBuf = create_themeicon(pTheme->UP_Icon, 0);

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

    pBuf = create_themeicon(pTheme->DOWN_Icon, 0);

    pBuf->size.w = w;
    pBuf->size.x = pWidget->size.x + FRAME_WH;
    pBuf->size.y =
	pWindow->size.y + pWindow->size.h - FRAME_WH - pBuf->size.h;
    pBuf->action = change_research_goal_down_callback;
    set_wstate(pBuf, WS_NORMAL);

    add_to_gui_list(ID_SCIENCE_DLG_CHANGE_GOAL_DOWN_BUTTON, pBuf);

    pBeginChangeTechDlg = pBuf;

    pChangeTechScroll = MALLOC(sizeof(struct ScrollBar));
    pChangeTechScroll->active = max;
    pChangeTechScroll->count = count;
    pChangeTechScroll->min = h;
    pChangeTechScroll->max = pBuf->size.y;
  } else {
    pWindow->size.x = pWidget->size.x;
    pWindow->size.y = (Main.screen->h - h) / 2;

    resize_window(pWindow, NULL,
		  get_game_colorRGB(COLOR_STD_BACKGROUND_BROWN),
		  w + DOUBLE_FRAME_WH, h);

    max = count;
    h = WINDOW_TILE_HIGH + 1;
  }

  pBeginActiveChangeTechDlg = pBuf = pEndChangeTechDlg->prev;
  pBuf->size.x = pWindow->size.x + FRAME_WH;
  pBuf->size.y = pWindow->size.y + h;
  pBuf->size.w = w;
  pBuf = pBuf->prev;

  h = 2;
  while (1) {
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

  set_wstate(pWidget, WS_NORMAL);
  pSellected_Widget = NULL;

  redraw_icon2(pWidget);

  redraw_group(pBeginChangeTechDlg, pEndChangeTechDlg, 0);

  refresh_rect(pWindow->size);

  return -1;
}

/**************************************************************************
  Popup (or raise) the science report(F6).  It may or may not be modal.
**************************************************************************/
void popup_science_dialog(bool make_modal)
{
  struct GUI *pBuf, *pWindow;
  SDL_String16 *pStr;
  /*SDL_Surface *pLogo; */

  if (pEndScienceDlg) {
    return;
  }

  pStr = create_str16_from_char(_("Science"), 12);
  pStr->style |= TTF_STYLE_BOLD;

  pWindow = create_window(pStr, 400, 300, 0);
  pEndScienceDlg = pWindow;

  pWindow->size.x = (Main.screen->w - 400) / 2;
  pWindow->size.y = (Main.screen->h - 240) / 2;

  resize_window(pWindow, NULL,
		get_game_colorRGB(COLOR_STD_BACKGROUND_BROWN), 400, 240);
  /*pLogo = get_logo_gfx();
     if ( resize_window( pWindow , pLogo , NULL , 400, 240 ) )
     {
     FREESURFACE( pLogo );
     } */

  add_to_gui_list(ID_SCIENCE_DLG_WINDOW, pWindow);
  /* ------ */

#if 0
  pBuf = create_icon2((SDL_Surface *)
		      advances[game.player_ptr->research.researching].
		      sprite, WF_DRAW_THEME_TRANSPARENT);
#else
  pBuf = NULL;			/* ? */
#endif

  pBuf->action = change_research;
  set_wstate(pBuf, WS_NORMAL);

  pBuf->size.x = pWindow->size.x + 16;
  pBuf->size.y = pWindow->size.y + WINDOW_TILE_HIGH + 60;

  add_to_gui_list(ID_SCIENCE_DLG_CHANGE_REASARCH_BUTTON, pBuf);

  /* ------ */

#if 0
  pBuf =
      create_icon2((SDL_Surface *) advances[game.player_ptr->ai.tech_goal].
		   sprite, WF_DRAW_THEME_TRANSPARENT);
#else
  pBuf = NULL;			/* ? */
#endif

  pBuf->action = change_research_goal;
  set_wstate(pBuf, WS_NORMAL);

  pBuf->size.x = pWindow->size.x + 16;
  pBuf->size.y =
      pWindow->size.y + WINDOW_TILE_HIGH + 60 + pBuf->size.h + 20;

  add_to_gui_list(ID_SCIENCE_DLG_CHANGE_GOAL_BUTTON, pBuf);

  /* ------ */
  pStr = create_str16_from_char(_("Close"), 12);

  pBuf = create_icon_button(NULL, pStr, 0);

  pBuf->action = popdown_science_dialog;
  set_wstate(pBuf, WS_NORMAL);
  clear_wflag(pBuf, WF_DRAW_FRAME_AROUND_WIDGET);

  add_to_gui_list(ID_SCIENCE_CANCEL_DLG_BUTTON, pBuf);

  pBuf->size.h += 6;
  pBuf->size.x = pWindow->size.x + 3;
  pBuf->size.y = pWindow->size.y + pWindow->size.h - pBuf->size.h - 3;
  pBuf->size.w = pWindow->size.w - 6;

  /* ======================== */

  pBeginScienceDlg = pBuf;

  science_dialog_update();
}
