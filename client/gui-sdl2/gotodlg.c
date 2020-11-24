/***********************************************************************
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
#include <fc_config.h>
#endif

#include <stdlib.h>

/* SDL2 */
#ifdef SDL2_PLAIN_INCLUDE
#include <SDL.h>
#else  /* SDL2_PLAIN_INCLUDE */
#include <SDL2/SDL.h>
#endif /* SDL2_PLAIN_INCLUDE */

/* utility */
#include "bitvector.h"
#include "fcintl.h"
#include "log.h"

/* common */
#include "game.h"
#include "fc_types.h" /* bv_player */
#include "unitlist.h"

/* client */
#include "client_main.h"
#include "control.h"
#include "goto.h"

/* gui-sdl2 */
#include "colors.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_tilespec.h"
#include "mapview.h"
#include "sprite.h"
#include "widget.h"

#include "gotodlg.h"

static struct advanced_dialog *pGotoDlg = NULL;
bv_player all_players;
static bool GOTO = TRUE;

static void update_goto_dialog(void);

/**********************************************************************//**
  User interacted with goto dialog window.
**************************************************************************/
static int goto_dialog_window_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    move_window_group(pGotoDlg->begin_widget_list, pwindow);
  }

  return -1;
}

/**********************************************************************//**
  Close goto dialog.
**************************************************************************/
static int exit_goto_dialog_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popdown_goto_airlift_dialog();
    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  Toggle whether player cities are listed as possible destinations.
**************************************************************************/
static int toggle_goto_nations_cities_dialog_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    int plr_id = player_index(player_by_number(MAX_ID - pwidget->ID));

    if (BV_ISSET(all_players, plr_id)) {
      BV_CLR(all_players, plr_id);
    } else {
      BV_SET(all_players, plr_id);
    }
    update_goto_dialog();
  }

  return -1;
}

/**********************************************************************//**
  User has selected city for unit to go to.
**************************************************************************/
static int goto_city_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct city *pdestcity = game_city_by_number(MAX_ID - pwidget->ID);
  
    if (pdestcity) {
      struct unit *punit = head_of_units_in_focus();

      if (punit) {
        if (GOTO) {
          send_goto_tile(punit, pdestcity->tile);
        } else {
          request_unit_airlift(punit, pdestcity);
        }
      }
    }

    popdown_goto_airlift_dialog();
    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  Refresh goto dialog.
**************************************************************************/
static void update_goto_dialog(void)
{
  struct widget *buf = NULL, *add_dock, *pLast;
  SDL_Surface *logo = NULL;
  utf8_str *pstr;
  char cBuf[128];
  int n = 0;
  struct player *owner = NULL;

  if (pGotoDlg->end_active_widget_list) {
    add_dock = pGotoDlg->end_active_widget_list->next;
    pGotoDlg->begin_widget_list = add_dock;
    del_group(pGotoDlg->begin_active_widget_list, pGotoDlg->end_active_widget_list);
    pGotoDlg->active_widget_list = NULL;
  } else {
    add_dock = pGotoDlg->begin_widget_list;
  }

  pLast = add_dock;

  players_iterate(pplayer) {
    if (!BV_ISSET(all_players, player_index(pplayer))) {
      continue;
    }

    city_list_iterate(pplayer->cities, pCity) {

      /* FIXME: should use unit_can_airlift_to(). */
      if (!GOTO && !pCity->airlift) {
	continue;
      }

      fc_snprintf(cBuf, sizeof(cBuf), "%s (%d)", city_name_get(pCity),
                  city_size_get(pCity));

      pstr = create_utf8_from_char(cBuf, adj_font(12));
      pstr->style |= TTF_STYLE_BOLD;

      if (!player_owns_city(owner, pCity)) {
        logo = get_nation_flag_surface(nation_of_player(city_owner(pCity)));
        logo = crop_visible_part_from_surface(logo);
      }

      buf = create_iconlabel(logo, pGotoDlg->end_widget_list->dst, pstr,
    	(WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE));

      if (!player_owns_city(owner, pCity)) {
        set_wflag(buf, WF_FREE_THEME);
        owner = city_owner(pCity);
      }

      buf->string_utf8->fgcol =
	    *(get_player_color(tileset, city_owner(pCity))->color);
      buf->action = goto_city_callback;

      if (GOTO || pCity->airlift) {
        set_wstate(buf, FC_WS_NORMAL);
      }

      fc_assert((MAX_ID - pCity->id) > 0);
      buf->ID = MAX_ID - pCity->id;

      widget_add_as_prev(buf, add_dock);
      add_dock = buf;

      if (n > (pGotoDlg->scroll->active - 1)) {
        set_wflag(buf, WF_HIDDEN);
      }

      n++;
    } city_list_iterate_end;
  } players_iterate_end;

  if (n > 0) {
    pGotoDlg->begin_widget_list = buf;

    pGotoDlg->begin_active_widget_list = pGotoDlg->begin_widget_list;
    pGotoDlg->end_active_widget_list = pLast->prev;
    pGotoDlg->active_widget_list = pGotoDlg->end_active_widget_list;
    pGotoDlg->scroll->count = n;

    if (n > pGotoDlg->scroll->active) {
      show_scrollbar(pGotoDlg->scroll);
      pGotoDlg->scroll->pscroll_bar->size.y = pGotoDlg->end_widget_list->area.y +
        pGotoDlg->scroll->pUp_Left_Button->size.h;
      pGotoDlg->scroll->pscroll_bar->size.h = scrollbar_size(pGotoDlg->scroll);
    } else {
      hide_scrollbar(pGotoDlg->scroll);
    }

    setup_vertical_widgets_position(1,
                                    pGotoDlg->end_widget_list->area.x,
                                    pGotoDlg->end_widget_list->area.y,
                                    pGotoDlg->scroll->pUp_Left_Button->size.x -
                                    pGotoDlg->end_widget_list->area.x - adj_size(2),
                                    0, pGotoDlg->begin_active_widget_list,
                                    pGotoDlg->end_active_widget_list);

  } else {
    hide_scrollbar(pGotoDlg->scroll);
  }

  /* redraw */
  redraw_group(pGotoDlg->begin_widget_list, pGotoDlg->end_widget_list, 0);
  widget_flush(pGotoDlg->end_widget_list);
}

/**********************************************************************//**
  Popup a dialog to have the focus unit goto to a city.
**************************************************************************/
static void popup_goto_airlift_dialog(void)
{
  SDL_Color bg_color = {0, 0, 0, 96};
  struct widget *buf, *pwindow;
  utf8_str *pstr;
  SDL_Surface *pFlag, *pEnabled, *pDisabled;
  SDL_Rect dst;
  int i, col, block_x, x, y;
  SDL_Rect area;

  if (pGotoDlg) {
    return;
  }

  pGotoDlg = fc_calloc(1, sizeof(struct advanced_dialog));

  pstr = create_utf8_from_char(_("Select destination"), adj_font(12));
  pstr->style |= TTF_STYLE_BOLD;

  pwindow = create_window_skeleton(NULL, pstr, 0);

  pwindow->action = goto_dialog_window_callback;
  set_wstate(pwindow, FC_WS_NORMAL);

  add_to_gui_list(ID_WINDOW, pwindow);
  pGotoDlg->end_widget_list = pwindow;

  area = pwindow->area;

  /* ---------- */
  /* create exit button */
  buf = create_themeicon(current_theme->Small_CANCEL_Icon, pwindow->dst,
                          WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  buf->info_label = create_utf8_from_char(_("Close Dialog (Esc)"),
                                           adj_font(12));
  buf->action = exit_goto_dialog_callback;
  set_wstate(buf, FC_WS_NORMAL);
  buf->key = SDLK_ESCAPE;
  area.w = MAX(area.w, buf->size.w) + adj_size(10);

  add_to_gui_list(ID_BUTTON, buf);

  col = 0;
  /* --------------------------------------------- */
  players_iterate(pplayer) {
    if (pplayer != client.conn.playing
        && DS_NO_CONTACT == player_diplstate_get(client.conn.playing, pplayer)->type) {
      continue;
    }

    pFlag = ResizeSurfaceBox(get_nation_flag_surface(pplayer->nation),
                             adj_size(30), adj_size(30), 1, TRUE, FALSE);

    pEnabled = create_icon_theme_surf(pFlag);
    fill_rect_alpha(pFlag, NULL, &bg_color);
    pDisabled = create_icon_theme_surf(pFlag);
    FREESURFACE(pFlag);

    buf = create_checkbox(pwindow->dst,
                           BV_ISSET(all_players, player_index(pplayer)),
                           WF_FREE_THEME | WF_RESTORE_BACKGROUND
                           | WF_WIDGET_HAS_INFO_LABEL);
    set_new_checkbox_theme(buf, pEnabled, pDisabled);

    buf->info_label =
        create_utf8_from_char(nation_adjective_for_player(pplayer),
                              adj_font(12));
    buf->info_label->style &= ~SF_CENTER;
    set_wstate(buf, FC_WS_NORMAL);

    buf->action = toggle_goto_nations_cities_dialog_callback;
    add_to_gui_list(MAX_ID - player_number(pplayer), buf);
    col++;
  } players_iterate_end;

  pGotoDlg->begin_widget_list = buf;

  create_vertical_scrollbar(pGotoDlg, 1, adj_size(320) / adj_size(30), TRUE, TRUE);
  hide_scrollbar(pGotoDlg->scroll);

  area.w = MAX(area.w, adj_size(300));
  area.h = adj_size(320);

  resize_window(pwindow, NULL, NULL,
                (pwindow->size.w - pwindow->area.w) + area.w,
                (pwindow->size.h - pwindow->area.h) + area.h);

  /* background */
  col = (col + 15) / 16; /* number of flag columns */

  pFlag = ResizeSurface(current_theme->Block,
                        (col * buf->size.w + (col - 1) * adj_size(5) + adj_size(10)),
                        area.h, 1);

  block_x = dst.x = area.x + area.w - pFlag->w;
  dst.y = area.y;
  alphablit(pFlag, NULL, pwindow->theme, &dst, 255);
  FREESURFACE(pFlag);

  widget_set_position(pwindow,
                      (main_window_width() - pwindow->size.w) / 2,
                      (main_window_height() - pwindow->size.h) / 2);

  /* exit button */
  buf = pwindow->prev;
  buf->size.x = area.x + area.w - buf->size.w - 1;
  buf->size.y = pwindow->size.y + adj_size(2);

  /* nations buttons */
  buf = buf->prev;
  i = 0;
  x = block_x + adj_size(5);
  y = area.y + adj_size(1);
  while (buf) {
    buf->size.x = x;
    buf->size.y = y;

    if (!((i + 1) % col)) {
      x = block_x + adj_size(5);
      y += buf->size.h + adj_size(1);
    } else {
      x += buf->size.w + adj_size(5);
    }

    if (buf == pGotoDlg->begin_widget_list) {
      break;
    }

    i++;
    buf = buf->prev;
  }

  setup_vertical_scrollbar_area(pGotoDlg->scroll,
	                        block_x, area.y,
  	                        area.h, TRUE);

  update_goto_dialog();
}


/**********************************************************************//**
  Popup a dialog to have the focus unit goto to a city.
**************************************************************************/
void popup_goto_dialog(void)
{
  if (!can_client_issue_orders() || 0 == get_num_units_in_focus()) {
    return;
  }

  BV_CLR_ALL(all_players);
  BV_SET(all_players, player_index(client.conn.playing));
  /* FIXME: Should we include allies in all_players */
  popup_goto_airlift_dialog();
}

/**********************************************************************//**
  Popup a dialog to have the focus unit airlift to a city.
**************************************************************************/
void popup_airlift_dialog(void)
{
  if (!can_client_issue_orders() || 0 == get_num_units_in_focus()) {
    return;
  }

  BV_CLR_ALL(all_players);
  BV_SET(all_players, player_index(client.conn.playing));
  /* FIXME: Should we include allies in all_players */
  GOTO = FALSE;
  popup_goto_airlift_dialog();
}

/**********************************************************************//**
  Popdown goto/airlift to a city dialog.
**************************************************************************/
void popdown_goto_airlift_dialog(void)
{
  if (pGotoDlg) {
    popdown_window_group_dialog(pGotoDlg->begin_widget_list,
                                pGotoDlg->end_widget_list);
    FC_FREE(pGotoDlg->scroll);
    FC_FREE(pGotoDlg);
  }
  GOTO = TRUE;
}
