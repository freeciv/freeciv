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
#include "fcintl.h"
#include "log.h"

/* common */
#include "game.h"
#include "nation.h"

/* client */
#include "zoom.h"

/* gui-sdl2 */
#include "colors.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_tilespec.h"
#include "mapctrl.h"
#include "mapview.h"
#include "sprite.h"
#include "widget.h"

#include "finddlg.h"

/* ====================================================================== */
/* ============================= FIND CITY MENU ========================= */
/* ====================================================================== */
static struct advanced_dialog *find_city_dlg = NULL;

/**********************************************************************//**
  User interacted with find city dialog window.
**************************************************************************/
static int find_city_window_dlg_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    move_window_group(find_city_dlg->begin_widget_list, pwindow);
  }

  return -1;
}

/**********************************************************************//**
  Close find city dialog.
**************************************************************************/
static int exit_find_city_dlg_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    int orginal_x = pwidget->data.cont->id0;
    int orginal_y = pwidget->data.cont->id1;

    popdown_find_dialog();

    center_tile_mapcanvas(map_pos_to_tile(&(wld.map), orginal_x, orginal_y));

    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  User has selected city to find.
**************************************************************************/
static int find_city_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct city *pcity = pwidget->data.city;

    if (pcity) {
      center_tile_mapcanvas(pcity->tile);
      if (main_data.event.button.button == SDL_BUTTON_RIGHT) {
        popdown_find_dialog();
      }
      flush_dirty();
    }
  }

  return -1;
}

/**********************************************************************//**
  Popdown a dialog to ask for a city to find.
**************************************************************************/
void popdown_find_dialog(void)
{
  if (find_city_dlg) {
    popdown_window_group_dialog(find_city_dlg->begin_widget_list,
                                find_city_dlg->end_widget_list);
    FC_FREE(find_city_dlg->scroll);
    FC_FREE(find_city_dlg);
    enable_and_redraw_find_city_button();
  }
}

/**********************************************************************//**
  Popup a dialog to ask for a city to find.
**************************************************************************/
void popup_find_dialog(void)
{
  struct widget *pwindow = NULL, *buf = NULL;
  SDL_Surface *logo = NULL;
  utf8_str *pstr;
  char cbuf[128];
  int h = 0, n = 0, w = 0, units_h = 0;
  struct player *owner = NULL;
  struct tile *original;
  int window_x = 0, window_y = 0;
  bool mouse = (main_data.event.type == SDL_MOUSEBUTTONDOWN);
  SDL_Rect area;

  /* check that there are any cities to find */
  players_iterate(pplayer) {
    h = city_list_size(pplayer->cities);
    if (h > 0) {
      break;
    }
  } players_iterate_end;

  if (find_city_dlg && !h) {
    return;
  }

  original = canvas_pos_to_tile(main_data.map->w / 2, main_data.map->h / 2, map_zoom);

  find_city_dlg = fc_calloc(1, sizeof(struct advanced_dialog));

  pstr = create_utf8_from_char_fonto(_("Find City"), FONTO_ATTENTION);
  pstr->style |= TTF_STYLE_BOLD;

  pwindow = create_window_skeleton(NULL, pstr, 0);

  pwindow->action = find_city_window_dlg_callback;
  set_wstate(pwindow , FC_WS_NORMAL);

  add_to_gui_list(ID_TERRAIN_ADV_DLG_WINDOW, pwindow);
  find_city_dlg->end_widget_list = pwindow;

  area = pwindow->area;

  /* ---------- */
  /* Exit button */
  buf = create_themeicon(current_theme->small_cancel_icon, pwindow->dst,
                         WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND
                         | WF_FREE_DATA);
  buf->info_label = create_utf8_from_char_fonto(_("Close Dialog (Esc)"),
                                                FONTO_ATTENTION);
  area.w = MAX(area.w, buf->size.w + adj_size(10));
  buf->action = exit_find_city_dlg_callback;
  set_wstate(buf, FC_WS_NORMAL);
  buf->key = SDLK_ESCAPE;
  buf->data.cont = fc_calloc(1, sizeof(struct container));
  buf->data.cont->id0 = index_to_map_pos_x(tile_index(original));
  buf->data.cont->id1 = index_to_map_pos_y(tile_index(original));

  add_to_gui_list(ID_TERRAIN_ADV_DLG_EXIT_BUTTON, buf);
  /* ---------- */

  players_iterate(pplayer) {
    city_list_iterate(pplayer->cities, pcity) {
      fc_snprintf(cbuf , sizeof(cbuf), "%s (%d)", city_name_get(pcity),
                  city_size_get(pcity));

      pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
      pstr->style |= (TTF_STYLE_BOLD|SF_CENTER);

      if (!player_owns_city(owner, pcity)) {
        logo = get_nation_flag_surface(nation_of_player(city_owner(pcity)));
        logo = crop_visible_part_from_surface(logo);
      }

      buf = create_iconlabel(logo, pwindow->dst, pstr,
        (WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE));

      if (!player_owns_city(owner, pcity)) {
        set_wflag(buf, WF_FREE_THEME);
        owner = city_owner(pcity);
      }

      buf->string_utf8->style &= ~SF_CENTER;
      buf->string_utf8->fgcol = *(get_player_color(tileset, city_owner(pcity))->color);
      buf->string_utf8->bgcol = (SDL_Color) {0, 0, 0, 0};

      buf->data.city = pcity;

      buf->action = find_city_callback;
      set_wstate(buf, FC_WS_NORMAL);

      add_to_gui_list(ID_LABEL , buf);

      area.w = MAX(area.w, buf->size.w);
      area.h += buf->size.h;

      if (n > 19) {
        set_wflag(buf , WF_HIDDEN);
      }

      n++;
    } city_list_iterate_end;
  } players_iterate_end;

  find_city_dlg->begin_widget_list = buf;
  find_city_dlg->begin_active_widget_list = find_city_dlg->begin_widget_list;
  find_city_dlg->end_active_widget_list = pwindow->prev->prev;
  find_city_dlg->active_widget_list = find_city_dlg->end_active_widget_list;


  /* ---------- */
  if (n > 20) {
    units_h = create_vertical_scrollbar(find_city_dlg, 1, 20, TRUE, TRUE);
    find_city_dlg->scroll->count = n;

    n = units_h;
    area.w += n;

    units_h = 20 * buf->size.h + adj_size(2);

  } else {
    units_h = area.h;
  }

  /* ---------- */

  area.h = units_h;

  resize_window(pwindow , NULL, NULL,
                (pwindow->size.w - pwindow->area.w) + area.w,
                (pwindow->size.h - pwindow->area.h) + area.h);

  area = pwindow->area;

  if (!mouse) {
    window_x = adj_size(10);
    window_y = (main_window_height() - pwindow->size.h) / 2;
  } else {
    window_x = (main_data.event.motion.x + pwindow->size.w + adj_size(10) < main_window_width()) ?
                (main_data.event.motion.x + adj_size(10)) :
                (main_window_width() - pwindow->size.w - adj_size(10));
    window_y = (main_data.event.motion.y - adj_size(2) + pwindow->size.h < main_window_height()) ?
             (main_data.event.motion.y - adj_size(2)) :
             (main_window_height() - pwindow->size.h - adj_size(10));
  }

  widget_set_position(pwindow, window_x, window_y);

  w = area.w;

  if (find_city_dlg->scroll) {
    w -= n;
  }

  /* exit button */
  buf = pwindow->prev;

  buf->size.x = area.x + area.w - buf->size.w - 1;
  buf->size.y = pwindow->size.y + adj_size(2);

  /* cities */
  buf = buf->prev;
  setup_vertical_widgets_position(1, area.x, area.y, w, 0,
                                  find_city_dlg->begin_active_widget_list,
                                  buf);

  if (find_city_dlg->scroll) {
    setup_vertical_scrollbar_area(find_city_dlg->scroll,
                                  area.x + area.w, area.y,
                                  area.h, TRUE);
  }

  /* -------------------- */
  /* redraw */
  redraw_group(find_city_dlg->begin_widget_list, pwindow, 0);
  widget_mark_dirty(pwindow);

  flush_dirty();
}
