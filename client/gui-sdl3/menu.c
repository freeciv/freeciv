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

/***********************************************************************
                          menu.c  -  description
                             -------------------
    begin                : Wed Sep 04 2002
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* SDL3 */
#include <SDL3/SDL.h>

/* utility */
#include "fcintl.h"
#include "log.h"

/* common */
#include "clientutils.h"
#include "game.h"
#include "road.h"
#include "traderoutes.h"
#include "unitlist.h"

/* client */
#include "client_main.h" /* client_state */
#include "climisc.h"
#include "control.h"
#include "pages_g.h"

/* gui-sdl3 */
#include "dialogs.h"
#include "gotodlg.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_tilespec.h"
#include "mapctrl.h"
#include "mapview.h"
#include "widget.h"

#include "menu.h"

extern struct widget *options_button;

static struct widget *begin_order_widget_list;
static struct widget *end_order_widget_list;

static struct widget *order_clean_button;
static struct widget *order_airbase_button;
static struct widget *order_fortress_button;
static struct widget *order_build_add_to_city_button;
static struct widget *order_mine_button;
static struct widget *order_irrigation_button;
static struct widget *order_cultivate_button;
static struct widget *order_plant_button;
static struct widget *order_road_button;
static struct widget *order_transform_button;
static struct widget *order_trade_button;

#define local_show(id)                                                \
  clear_wflag(get_widget_pointer_from_id(begin_order_widget_list, id, SCAN_FORWARD), \
              WF_HIDDEN)

#define local_hide(id)                                             \
  set_wflag(get_widget_pointer_from_id(begin_order_widget_list, id, SCAN_FORWARD), \
            WF_HIDDEN )


/**********************************************************************//**
  User interacted with some unit order widget.
**************************************************************************/
static int unit_order_callback(struct widget *order_widget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct unit *head_unit = head_of_units_in_focus();

    set_wstate(order_widget, FC_WS_SELECTED);
    selected_widget = order_widget;

    if (!head_unit) {
      return -1;
    }

    switch (order_widget->id) {
    case ID_UNIT_ORDER_BUILD_CITY:
      /* Enable the button for adding to a city in all cases, so we
         get an eventual error message from the server if we try. */
      key_unit_build_city();
      break;
    case ID_UNIT_ORDER_BUILD_WONDER:
      key_unit_build_wonder();
      break;
    case ID_UNIT_ORDER_ROAD:
      key_unit_road();
      break;
    case ID_UNIT_ORDER_TRADE_ROUTE:
      key_unit_trade_route();
      break;
    case ID_UNIT_ORDER_IRRIGATE:
      key_unit_irrigate();
      break;
    case ID_UNIT_ORDER_CULTIVATE:
      key_unit_cultivate();
      break;
    case ID_UNIT_ORDER_MINE:
      key_unit_mine();
      break;
    case ID_UNIT_ORDER_PLANT:
      key_unit_plant();
      break;
    case ID_UNIT_ORDER_TRANSFORM:
      key_unit_transform();
      break;
    case ID_UNIT_ORDER_FORTRESS:
      key_unit_fortress();
      break;
    case ID_UNIT_ORDER_FORTIFY:
      key_unit_fortify();
      break;
    case ID_UNIT_ORDER_AIRBASE:
      key_unit_airbase();
      break;
    case ID_UNIT_ORDER_CLEAN:
      key_unit_clean();
      break;
    case ID_UNIT_ORDER_PARADROP:
      key_unit_paradrop();
      break;
    case ID_UNIT_ORDER_SENTRY:
      key_unit_sentry();
      break;
    case ID_UNIT_ORDER_PILLAGE:
      key_unit_pillage();
      break;
    case ID_UNIT_ORDER_HOMECITY:
      key_unit_homecity();
      break;
    case ID_UNIT_ORDER_UNLOAD_TRANSPORTER:
      key_unit_unload_all();
      break;
    case ID_UNIT_ORDER_BOARD:
      unit_list_iterate(get_units_in_focus(), punit) {
        request_unit_load(punit, NULL, unit_tile(punit));
      } unit_list_iterate_end;
      break;
    case ID_UNIT_ORDER_DEBOARD:
      unit_list_iterate(get_units_in_focus(), punit) {
        request_unit_unload(punit);
      } unit_list_iterate_end;
      break;
    case ID_UNIT_ORDER_WAKEUP_OTHERS:
      key_unit_wakeup_others();
      break;
    case ID_UNIT_ORDER_AUTO_WORKER:
      unit_list_iterate(get_units_in_focus(), punit) {
        request_unit_autoworker(punit);
      } unit_list_iterate_end;
      break;
    case ID_UNIT_ORDER_AUTO_EXPLORE:
      key_unit_auto_explore();
      break;
    case ID_UNIT_ORDER_CONNECT_IRRIGATE:
      {
        struct extra_type_list *extras = extra_type_list_by_cause(EC_IRRIGATION);

        if (extra_type_list_size(extras) > 0) {
          struct extra_type *pextra;

          pextra = extra_type_list_get(extra_type_list_by_cause(EC_IRRIGATION), 0);

          key_unit_connect(ACTIVITY_IRRIGATE, pextra);
        }
      }
      break;
    case ID_UNIT_ORDER_CONNECT_ROAD:
      {
        struct road_type *proad = road_by_gui_type(ROAD_GUI_ROAD);

        if (proad != NULL) {
          struct extra_type *tgt;

          tgt = road_extra_get(proad);

          key_unit_connect(ACTIVITY_GEN_ROAD, tgt);
        }
      }
      break;
    case ID_UNIT_ORDER_CONNECT_RAILROAD:
      {
        struct road_type *prail = road_by_gui_type(ROAD_GUI_RAILROAD);

        if (prail != NULL) {
          struct extra_type *tgt;

          tgt = road_extra_get(prail);

          key_unit_connect(ACTIVITY_GEN_ROAD, tgt);
        }
      }
      break;
    case ID_UNIT_ORDER_PATROL:
      key_unit_patrol();
      break;
    case ID_UNIT_ORDER_GOTO:
      key_unit_goto();
      break;
    case ID_UNIT_ORDER_GOTO_CITY:
      popup_goto_dialog();
      break;
    case ID_UNIT_ORDER_AIRLIFT:
      popup_airlift_dialog();
      break;
    case ID_UNIT_ORDER_RETURN:
      unit_list_iterate(get_units_in_focus(), punit) {
        request_unit_return(punit);
      } unit_list_iterate_end;
      break;
    case ID_UNIT_ORDER_UPGRADE:
      popup_unit_upgrade_dlg(head_unit, FALSE);
      break;
    case ID_UNIT_ORDER_CONVERT:
      key_unit_convert();
      break;
    case ID_UNIT_ORDER_DISBAND:
      popup_unit_disband_dlg(head_unit, FALSE);
      break;
    case ID_UNIT_ORDER_DIPLOMAT_DLG:
      key_unit_action_select_tgt();
      break;
    case ID_UNIT_ORDER_NUKE:
      request_unit_goto(ORDER_PERFORM_ACTION, ACTION_NUKE, -1);
      break;
    case ID_UNIT_ORDER_WAIT:
      key_unit_wait();
      flush_dirty();
      break;
    case ID_UNIT_ORDER_DONE:
      key_unit_done();
      flush_dirty();
      break;

    default:
      break;
    }
  }

  return -1;
}

/**********************************************************************//**
   Refresh order widgets.
**************************************************************************/
static Uint16 redraw_order_widgets(void)
{
  Uint16 count = 0;
  struct widget *tmp_widget = begin_order_widget_list;

  while (TRUE) {
    if (!(get_wflags(tmp_widget) & WF_HIDDEN)) {
      if (get_wflags(tmp_widget) & WF_RESTORE_BACKGROUND) {
        refresh_widget_background(tmp_widget);
      }
      widget_redraw(tmp_widget);
      widget_mark_dirty(tmp_widget);
      count++;
    }

    if (tmp_widget == end_order_widget_list) {
      break;
    }

    tmp_widget = tmp_widget->next;
  }

  return count;
}

/**********************************************************************//**
   Reposition order widgets as they fit.
**************************************************************************/
static void set_new_order_widget_start_pos(void)
{
  struct widget *minimap = get_minimap_window_widget();
  struct widget *info_wind = get_unit_info_window_widget();
  struct widget *tmp_widget = begin_order_widget_list;
  Sint16 sx, sy, xx, yy = 0;
  int count = 0, lines = 1, w = 0, count_on_line;

  xx = minimap->dst->dest_rect.x + minimap->size.w + adj_size(10);
  w = (info_wind->dst->dest_rect.x - adj_size(10)) - xx;

  if (w < (tmp_widget->size.w + adj_size(10)) * 2) {
    if (minimap->size.h == info_wind->size.h) {
      xx = 0;
      w = main_window_width();
      yy = info_wind->size.h;
    } else {
      if (minimap->size.h > info_wind->size.h) {
        w = main_window_width() - xx - adj_size(20);
        if (w < (tmp_widget->size.w + adj_size(10)) * 2) {
          xx = 0;
          w = minimap->size.w;
          yy = minimap->size.h;
        } else {
          yy = info_wind->size.h;
        }
      } else {
        w = info_wind->dst->dest_rect.x - adj_size(20);
        if (w < (tmp_widget->size.w + adj_size(10)) * 2) {
          xx = info_wind->dst->dest_rect.x;
          w = info_wind->size.w;
          yy = info_wind->size.h;
        } else {
          xx = adj_size(10);
          yy = minimap->size.h;
        }
      }
    }
  }

  count_on_line = w / (tmp_widget->size.w + adj_size(5));

  /* find how many to reposition */
  while (TRUE) {
    if (!(get_wflags(tmp_widget) & WF_HIDDEN)) {
      count++;
    }

    if (tmp_widget == end_order_widget_list) {
      break;
    }

    tmp_widget = tmp_widget->next;
  }

  tmp_widget = begin_order_widget_list;

  if (count - count_on_line > 0) {
    lines = (count + (count_on_line - 1)) / count_on_line;

    count = count_on_line - ((count_on_line * lines) - count);
  }

  sx = xx + (w - count * (tmp_widget->size.w + adj_size(5))) / 2;

  sy = tmp_widget->dst->surface->h - yy - lines * (tmp_widget->size.h + adj_size(5));

  while (TRUE) {
    if (!(get_wflags(tmp_widget) & WF_HIDDEN)) {
      tmp_widget->size.x = sx;
      tmp_widget->size.y = sy;

      count--;
      sx += (tmp_widget->size.w + adj_size(5));
      if (!count) {
        count = count_on_line;
        lines--;

        sx = xx + (w - count * (tmp_widget->size.w + adj_size(5))) / 2;

        sy = tmp_widget->dst->surface->h - yy - lines * (tmp_widget->size.h + adj_size(5));
      }
    }

    if (tmp_widget == end_order_widget_list) {
      break;
    }

    tmp_widget = tmp_widget->next;
  }
}

/* ================================ PUBLIC ================================ */

/**********************************************************************//**
   Create units order widgets.
**************************************************************************/
void create_units_order_widgets(void)
{
  struct widget *buf = NULL;
  char cbuf[128];
  struct road_type *proad;
  struct road_type *prail;

  /* No orders */
  fc_snprintf(cbuf, sizeof(cbuf), "%s (%s)", _("No Orders"),
              /* TRANS: "Space" refers to the space bar on a keyboard. */
              _("Space"));
  buf = create_themeicon(current_theme->o_done_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_SPACE;
  add_to_gui_list(ID_UNIT_ORDER_DONE, buf);
  /* --------- */

  end_order_widget_list = buf;

  /* Wait */
  fc_snprintf(cbuf, sizeof(cbuf), "%s (%s)", _("Wait"), "W");
  buf = create_themeicon(current_theme->o_wait_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_W;
  add_to_gui_list(ID_UNIT_ORDER_WAIT, buf);
  /* --------- */

  /* Explode Nuclear */
  fc_snprintf(cbuf, sizeof(cbuf), "%s (%s)",
              action_id_name_translation(ACTION_NUKE), "Shift+N");
  buf = create_themeicon(current_theme->o_nuke_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_N;
  buf->mod = SDL_KMOD_SHIFT;
  add_to_gui_list(ID_UNIT_ORDER_NUKE, buf);
  /* --------- */

  /* Act against the specified tile. */
  /* TRANS: Button to bring up the action selection dialog. */
  fc_snprintf(cbuf, sizeof(cbuf), "%s (%s)", _("Do..."), "D");
  buf = create_themeicon(current_theme->o_spy_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_D;
  add_to_gui_list(ID_UNIT_ORDER_DIPLOMAT_DLG, buf);
  /* --------- */

  /* Disband */
  fc_snprintf(cbuf, sizeof(cbuf), "%s (%s)", _("Disband Unit"), "Shift+D");
  buf = create_themeicon(current_theme->o_disband_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_D;
  buf->mod = SDL_KMOD_SHIFT;
  add_to_gui_list(ID_UNIT_ORDER_DISBAND, buf);
  /* --------- */

  /* Upgrade */
  fc_snprintf(cbuf, sizeof(cbuf), "%s (%s)", _("Upgrade Unit"), "Shift+U");
  buf = create_themeicon(current_theme->order_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_U;
  buf->mod = SDL_KMOD_SHIFT;
  add_to_gui_list(ID_UNIT_ORDER_UPGRADE, buf);
  /* --------- */

  /* Convert */
  fc_snprintf(cbuf, sizeof(cbuf), "%s (%s)", _("Convert Unit"), "Shift+O");
  buf = create_themeicon(current_theme->order_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_O;
  buf->mod = SDL_KMOD_SHIFT;
  add_to_gui_list(ID_UNIT_ORDER_CONVERT, buf);
  /* --------- */

  /* Return to nearest city */
  fc_snprintf(cbuf, sizeof(cbuf), "%s (%s)",
              _("Return to Nearest City"), "Shift+G");
  buf = create_themeicon(current_theme->o_return_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_G;
  buf->mod = SDL_KMOD_SHIFT;
  add_to_gui_list(ID_UNIT_ORDER_RETURN, buf);
  /* --------- */

  /* Goto City */
  fc_snprintf(cbuf, sizeof(cbuf), "%s (%s)", _("Go to City"), "T");
  buf = create_themeicon(current_theme->o_goto_city_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_T;
  add_to_gui_list(ID_UNIT_ORDER_GOTO_CITY, buf);
  /* --------- */

  /* Airlift */
  fc_snprintf(cbuf, sizeof(cbuf), "%s (%s)", _("Airlift to City"), "T");
  buf = create_themeicon(current_theme->order_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_T;
  add_to_gui_list(ID_UNIT_ORDER_AIRLIFT, buf);
  /* --------- */

  /* Goto location */
  fc_snprintf(cbuf, sizeof(cbuf), "%s (%s)", _("Go to Tile"), "G");
  buf = create_themeicon(current_theme->o_goto_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_G;
  add_to_gui_list(ID_UNIT_ORDER_GOTO, buf);
  /* --------- */

  /* Patrol */
  fc_snprintf(cbuf, sizeof(cbuf), "%s (%s)", _("Patrol"), "Q");
  buf = create_themeicon(current_theme->o_patrol_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_Q;
  add_to_gui_list(ID_UNIT_ORDER_PATROL, buf);
  /* --------- */

  /* Connect irrigation */
  fc_snprintf(cbuf, sizeof(cbuf), "%s (%s)",
              _("Connect With Irrigation"), "Ctrl+I");
  buf = create_themeicon(current_theme->o_autoconnect_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_I;
  buf->mod = SDL_KMOD_CTRL;
  add_to_gui_list(ID_UNIT_ORDER_CONNECT_IRRIGATE, buf);
  /* --------- */

  /* Connect road */
  proad = road_by_gui_type(ROAD_GUI_ROAD);

  if (proad != NULL) {
    fc_snprintf(cbuf, sizeof(cbuf),
                _("Connect With %s (%s)"),
                extra_name_translation(road_extra_get(proad)),
                "Ctrl+R");
    buf = create_themeicon(current_theme->o_autoconnect_icon, main_data.gui,
                           WF_HIDDEN | WF_RESTORE_BACKGROUND
                           | WF_WIDGET_HAS_INFO_LABEL);
    set_wstate(buf, FC_WS_NORMAL);
    buf->action = unit_order_callback;
    buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
    buf->key = SDLK_R;
    buf->mod = SDL_KMOD_CTRL;
    add_to_gui_list(ID_UNIT_ORDER_CONNECT_ROAD, buf);
  }
  /* --------- */

  /* Connect railroad */
  prail = road_by_gui_type(ROAD_GUI_RAILROAD);
  if (prail != NULL) {
    fc_snprintf(cbuf, sizeof(cbuf),
                _("Connect With %s (%s)"),
                extra_name_translation(road_extra_get(prail)),
                "Ctrl+L");
    buf = create_themeicon(current_theme->o_autoconnect_icon, main_data.gui,
                           WF_HIDDEN | WF_RESTORE_BACKGROUND
                           | WF_WIDGET_HAS_INFO_LABEL);
    set_wstate(buf, FC_WS_NORMAL);
    buf->action = unit_order_callback;
    buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
    buf->key = SDLK_L;
    buf->mod = SDL_KMOD_CTRL;
    add_to_gui_list(ID_UNIT_ORDER_CONNECT_RAILROAD, buf);
  }
  /* --------- */

  /* Auto Explore */
  fc_snprintf(cbuf, sizeof(cbuf), "%s (%s)", _("Auto Explore"), "X");
  buf = create_themeicon(current_theme->o_autoexp_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_X;
  add_to_gui_list(ID_UNIT_ORDER_AUTO_EXPLORE, buf);
  /* --------- */

  /* Auto Worker */
  fc_snprintf(cbuf, sizeof(cbuf), "%s (%s)", _("Auto Worker"), "A");

  buf = create_themeicon(current_theme->o_autowork_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_A;
  add_to_gui_list(ID_UNIT_ORDER_AUTO_WORKER, buf);
  /* --------- */

  /* Wake Up Others */
  fc_snprintf(cbuf, sizeof(cbuf), "%s (%s)",
              _("Unsentry All On Tile"), "Shift+S");
  buf = create_themeicon(current_theme->o_wakeup_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_S;
  buf->mod = SDL_KMOD_SHIFT;
  add_to_gui_list(ID_UNIT_ORDER_WAKEUP_OTHERS, buf);
  /* --------- */

  /* Unload */
  fc_snprintf(cbuf, sizeof(cbuf), "%s (%s)",
              _("Unload All From Transporter"), "Shift+T");
  buf = create_themeicon(current_theme->o_unload_icon, main_data.gui,
                          WF_HIDDEN | WF_RESTORE_BACKGROUND
                          | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->mod = SDL_KMOD_SHIFT;
  buf->key = SDLK_T;
  add_to_gui_list(ID_UNIT_ORDER_UNLOAD_TRANSPORTER, buf);
  /* --------- */

  /* Board */
  fc_snprintf(cbuf, sizeof(cbuf), "%s (%s)", _("Load Unit"), "L");
  buf = create_themeicon(current_theme->o_load_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_L;
  add_to_gui_list(ID_UNIT_ORDER_BOARD, buf);
  /* --------- */

  /* Deboard */
  fc_snprintf(cbuf, sizeof(cbuf), "%s (%s)", _("Unload Unit"), "U");
  buf = create_themeicon(current_theme->o_unload_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_U;
  add_to_gui_list(ID_UNIT_ORDER_DEBOARD, buf);
  /* --------- */

  /* Find Homecity */
  fc_snprintf(cbuf, sizeof(cbuf), "%s (%s)",
              action_id_name_translation(ACTION_HOME_CITY), "H");
  buf = create_themeicon(current_theme->o_homecity_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_H;
  add_to_gui_list(ID_UNIT_ORDER_HOMECITY, buf);
  /* --------- */

  /* Pillage */
  fc_snprintf(cbuf, sizeof(cbuf), "%s (%s)", _("Pillage"), "Shift+P");
  buf = create_themeicon(current_theme->o_pillage_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_P;
  buf->mod = SDL_KMOD_SHIFT;
  add_to_gui_list(ID_UNIT_ORDER_PILLAGE, buf);
  /* --------- */

  /* Sentry */
  fc_snprintf(cbuf, sizeof(cbuf), "%s (%s)", _("Sentry Unit"), "S");
  buf = create_themeicon(current_theme->o_sentry_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_S;
  add_to_gui_list(ID_UNIT_ORDER_SENTRY, buf);
  /* --------- */

  /* Paradrop */
  fc_snprintf(cbuf, sizeof(cbuf), "%s (%s)",
              action_id_name_translation(ACTION_PARADROP), "J");
  buf = create_themeicon(current_theme->o_paradrop_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label =  create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_J;
  add_to_gui_list(ID_UNIT_ORDER_PARADROP, buf);
  /* --------- */

  /* Generic Clean */
  /* Label will be replaced by real_menus_update() before it's seen */
  fc_snprintf(cbuf, sizeof(cbuf), "placeholder");
  buf = create_themeicon(current_theme->o_clean_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_P;
  order_clean_button = buf;
  add_to_gui_list(ID_UNIT_ORDER_CLEAN, buf);
  /* --------- */

  /* Build Airbase */
  /* Label will be replaced by real_menus_update() before it's seen */
  fc_snprintf(cbuf, sizeof(cbuf), "placeholder");
  buf = create_themeicon(current_theme->o_airbase_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_E;
  buf->mod = SDL_KMOD_SHIFT;
  order_airbase_button = buf;
  add_to_gui_list(ID_UNIT_ORDER_AIRBASE, buf);
  /* --------- */

  /* Fortify */
  fc_snprintf(cbuf, sizeof(cbuf), "%s (%s)", _("Fortify Unit"), "F");
  buf = create_themeicon(current_theme->o_fortify_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_F;
  add_to_gui_list(ID_UNIT_ORDER_FORTIFY, buf);
  /* --------- */

  /* Build Fortress */
  /* Label will be replaced by real_menus_update() before it's seen */
  fc_snprintf(cbuf, sizeof(cbuf), "placeholder");
  buf = create_themeicon(current_theme->o_fortress_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_F;
  buf->mod = SDL_KMOD_SHIFT;
  order_fortress_button = buf;
  add_to_gui_list(ID_UNIT_ORDER_FORTRESS, buf);
  /* --------- */

  /* Transform Tile */
  /* Label will be replaced by real_menus_update() before it's seen */
  fc_snprintf(cbuf, sizeof(cbuf), "placeholder");
  buf = create_themeicon(current_theme->o_transform_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_O;
  order_transform_button = buf;
  add_to_gui_list(ID_UNIT_ORDER_TRANSFORM, buf);
  /* --------- */

  /* Build Mine */
  /* Label will be replaced by real_menus_update() before it's seen */
  fc_snprintf(cbuf, sizeof(cbuf), "placeholder");
  buf = create_themeicon(current_theme->o_mine_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_M;
  add_to_gui_list(ID_UNIT_ORDER_MINE, buf);

  order_mine_button = buf;
  /* --------- */

  /* Build Irrigation */
  /* Label will be replaced by real_menus_update() before it's seen */
  fc_snprintf(cbuf, sizeof(cbuf), "placeholder");
  buf = create_themeicon(current_theme->o_irrigation_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->key = SDLK_I;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  add_to_gui_list(ID_UNIT_ORDER_IRRIGATE, buf);

  order_irrigation_button = buf;
  /* --------- */

  /* Cultivate */
  /* Label will be replaced by real_menus_update() before it's seen */
  fc_snprintf(cbuf, sizeof(cbuf), "placeholder");
  buf = create_themeicon(current_theme->o_cultivate_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->key = SDLK_I;
  buf->mod = SDL_KMOD_SHIFT;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  add_to_gui_list(ID_UNIT_ORDER_CULTIVATE, buf);

  order_cultivate_button = buf;
  /* --------- */

  /* Plant */
  /* Label will be replaced by real_menus_update() before it's seen */
  fc_snprintf(cbuf, sizeof(cbuf), "placeholder");
  buf = create_themeicon(current_theme->o_plant_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->key = SDLK_M;
  buf->mod = SDL_KMOD_SHIFT;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  add_to_gui_list(ID_UNIT_ORDER_PLANT, buf);

  order_plant_button = buf;
  /* --------- */

  /* Establish Trade route */
  /* Label will be replaced by real_menus_update() before it's seen */
  fc_snprintf(cbuf, sizeof(cbuf), "placeholder");
  buf = create_themeicon(current_theme->o_trade_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_R;
  add_to_gui_list(ID_UNIT_ORDER_TRADE_ROUTE, buf);

  order_trade_button = buf;
  /* --------- */

  /* Build (Rail-)Road */
  /* Label will be replaced by real_menus_update() before it's seen */
  fc_snprintf(cbuf, sizeof(cbuf), "placeholder");
  buf = create_themeicon(current_theme->o_road_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_R;
  add_to_gui_list(ID_UNIT_ORDER_ROAD, buf);

  order_road_button = buf;
  /* --------- */

  /* Help Build Wonder */
  fc_snprintf(cbuf, sizeof(cbuf), "%s (%s)",
              action_id_name_translation(ACTION_HELP_WONDER), "B");
  buf = create_themeicon(current_theme->o_wonder_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_B;
  add_to_gui_list(ID_UNIT_ORDER_BUILD_WONDER, buf);
  /* --------- */

  /* Add to City / Build New City */
  /* Label will be replaced by real_menus_update() before it's seen */
  fc_snprintf(cbuf, sizeof(cbuf), "placeholder");
  buf = create_themeicon(current_theme->o_build_city_icon, main_data.gui,
                         WF_HIDDEN | WF_RESTORE_BACKGROUND
                         | WF_WIDGET_HAS_INFO_LABEL);
  set_wstate(buf, FC_WS_NORMAL);
  buf->action = unit_order_callback;
  buf->info_label = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  buf->key = SDLK_B;
  add_to_gui_list(ID_UNIT_ORDER_BUILD_CITY, buf);

  order_build_add_to_city_button = buf;
  /* --------- */

  begin_order_widget_list = buf;

  sdl3_client_flags |= CF_ORDERS_WIDGETS_CREATED;
}

/**********************************************************************//**
   Free resources allocated for unit order widgets.
**************************************************************************/
void delete_units_order_widgets(void)
{
  del_group(begin_order_widget_list, end_order_widget_list);

  begin_order_widget_list = NULL;
  end_order_widget_list = NULL;
  sdl3_client_flags &= ~CF_ORDERS_WIDGETS_CREATED;
}

/**********************************************************************//**
   Draw order widgets to their currently correct place.
**************************************************************************/
void update_order_widgets(void)
{
  set_new_order_widget_start_pos();
  redraw_order_widgets();
}

/**********************************************************************//**
   Clear unit order widgets from view.
**************************************************************************/
void undraw_order_widgets(void)
{
  struct widget *tmp_widget = begin_order_widget_list;

  if (tmp_widget == NULL) {
    return;
  }

  while (TRUE) {
    if (!(get_wflags(tmp_widget) & WF_HIDDEN) && (tmp_widget->gfx)) {
      widget_undraw(tmp_widget);
      widget_mark_dirty(tmp_widget);
    }

    if (tmp_widget == end_order_widget_list) {
      break;
    }

    tmp_widget = tmp_widget->next;
  }
}

/* ============================== Native =============================== */

/**********************************************************************//**
  Initialize menus (sensitivity, name, etc.) based on the
  current state and current ruleset, etc.  Call menus_update().
**************************************************************************/
void real_menus_init(void)
{
  /* PORTME */
}

/**********************************************************************//**
  Update all of the menus (sensitivity, etc.) based on the current state.
**************************************************************************/
void real_menus_update(void)
{
  static Uint16 counter = 0;
  struct unit_list *punits = NULL;
  struct unit *punit = NULL;
  static char cbuf[128];

  if ((C_S_RUNNING != client_state())
      || (get_client_page() != PAGE_GAME)) {

    sdl3_client_flags |= CF_GAME_JUST_STARTED;

    if (sdl3_client_flags & CF_MAP_UNIT_W_CREATED) {
      set_wflag(options_button, WF_HIDDEN);
      hide_minimap_window_buttons();
      hide_unitinfo_window_buttons();
    }

    if (sdl3_client_flags & CF_ORDERS_WIDGETS_CREATED) {
      hide_group(begin_order_widget_list, end_order_widget_list);
    }

  } else {
    /* Running state */
    if (sdl3_client_flags & CF_MAP_UNIT_W_CREATED) {
      /* Show options button */
      clear_wflag(options_button, WF_HIDDEN);
      widget_redraw(options_button);
      widget_mark_dirty(options_button);
    }

    if (NULL == client.conn.playing) {
      /* Global observer */
      if (sdl3_client_flags & CF_MAP_UNIT_W_CREATED) {
        /* Show minimap buttons and unitinfo buttons */
        show_minimap_window_buttons();
        show_unitinfo_window_buttons();
        /* Disable minimap buttons and unitinfo buttons */
        disable_minimap_window_buttons();
        disable_unitinfo_window_buttons();
      }

      return;

    } else {
      /* running state with player */

      if (get_wstate(end_order_widget_list) == FC_WS_DISABLED) {
        enable_group(begin_order_widget_list, end_order_widget_list);
      }

      if (counter) {
        undraw_order_widgets();
      }

      if (sdl3_client_flags & CF_GAME_JUST_STARTED) {
        sdl3_client_flags &= ~CF_GAME_JUST_STARTED;

        /* Show minimap buttons and unitinfo buttons */
        show_minimap_window_buttons();
        show_unitinfo_window_buttons();

        counter = 0;
      }
    }

    punits = get_units_in_focus();
    punit = unit_list_get(punits, 0);

    if (punit && punit->ssa_controller == SSA_NONE) {
      struct city *homecity;
      int time;
      struct tile *ptile = unit_tile(punit);
      struct city *pcity = tile_city(ptile);
      struct terrain *pterrain = tile_terrain(ptile);
      struct base_type *pbase;
      struct extra_type *pextra;

      if (!counter) {
        local_show(ID_UNIT_ORDER_GOTO);
        local_show(ID_UNIT_ORDER_DISBAND);

        local_show(ID_UNIT_ORDER_WAIT);
        local_show(ID_UNIT_ORDER_DONE);
      }

      /* Enable the button for adding to a city in all cases, so we
       * get an eventual error message from the server if we try. */

      if (unit_can_add_or_build_city(&(wld.map), punit)) {
        if (pcity != NULL) {
          fc_snprintf(cbuf, sizeof(cbuf), "%s (%s)",
                      action_id_name_translation(ACTION_JOIN_CITY), "B");
        } else {
          fc_snprintf(cbuf, sizeof(cbuf), "%s (%s)",
                      action_id_name_translation(ACTION_FOUND_CITY), "B");
        }
        copy_chars_to_utf8_str(order_build_add_to_city_button->info_label,
                               cbuf);
        clear_wflag(order_build_add_to_city_button, WF_HIDDEN);
      } else {
        set_wflag(order_build_add_to_city_button, WF_HIDDEN);
      }

      if (unit_can_help_build_wonder_here(&(wld.map), punit)) {
        local_show(ID_UNIT_ORDER_BUILD_WONDER);
      } else {
        local_hide(ID_UNIT_ORDER_BUILD_WONDER);
      }

      pextra = next_extra_for_tile(ptile, EC_ROAD, unit_owner(punit), punit);
      if (pextra != NULL
          && can_unit_do_activity_targeted_client(punit, ACTIVITY_GEN_ROAD, pextra)) {
        struct road_type *proad = extra_road_get(pextra);
        enum road_compat compat = road_compat_special(proad);

        time = turns_to_activity_done(ptile, ACTIVITY_GEN_ROAD, pextra, punit);

        /* TRANS: "Build Railroad (R) 3 turns" */
        fc_snprintf(cbuf, sizeof(cbuf), _("Build %s (%s) %d %s"),
                    extra_name_translation(pextra),
                    "R", time,
                    PL_("turn", "turns", time));

        if (compat == ROCO_RAILROAD) {
          order_road_button->theme = current_theme->o_railroad_icon;
        } else {
          order_road_button->theme = current_theme->o_road_icon;
        }
        copy_chars_to_utf8_str(order_road_button->info_label, cbuf);
        clear_wflag(order_road_button, WF_HIDDEN);
      } else {
        set_wflag(order_road_button, WF_HIDDEN);
      }

      /* unit_can_est_trade_route_here(punit) */
      if (pcity && utype_can_do_action(unit_type_get(punit),
                                       ACTION_TRADE_ROUTE)
          && (homecity = game_city_by_number(punit->homecity))
          && can_cities_trade(homecity, pcity)) {
        int revenue = get_caravan_enter_city_trade_bonus(homecity, pcity,
                                                         unit_type_get(punit),
                                                         punit->carrying,
                                                         TRUE);
        struct goods_type *pgood = unit_current_goods(punit, homecity);

        if (can_establish_trade_route(homecity, pcity, pgood->replace_priority)) {
          fc_snprintf(cbuf, sizeof(cbuf),
                      _("%s With %s ( %d one time bonus + %d trade ) (R)"),
                      action_id_name_translation(ACTION_TRADE_ROUTE),
                      city_name_get(homecity),
                      revenue,
                      trade_base_between_cities(homecity, pcity));
        } else {
          revenue = (revenue + 2) / 3;
          fc_snprintf(cbuf, sizeof(cbuf),
                      _("%s Of %s ( %d one time bonus ) (R)"),
                      action_id_name_translation(ACTION_MARKETPLACE),
                      city_name_get(homecity),
                      revenue);
        }
        copy_chars_to_utf8_str(order_trade_button->info_label, cbuf);
        clear_wflag(order_trade_button, WF_HIDDEN);
      } else {
        set_wflag(order_trade_button, WF_HIDDEN);
      }

      pextra = next_extra_for_tile(ptile, EC_IRRIGATION,
                                   unit_owner(punit), punit);
      if (pextra != NULL
          && can_unit_do_activity_targeted_client(punit, ACTIVITY_IRRIGATE, pextra)) {
        time = turns_to_activity_done(ptile, ACTIVITY_IRRIGATE,
                                      pextra, punit);
        /* TRANS: "Build Irrigation (I) 5 turns" */
        fc_snprintf(cbuf, sizeof(cbuf), _("Build %s (%s) %d %s"),
                    extra_name_translation(pextra), "I", time,
                    PL_("turn", "turns", time));
        order_irrigation_button->theme = current_theme->o_irrigation_icon;

        copy_chars_to_utf8_str(order_irrigation_button->info_label, cbuf);
        clear_wflag(order_irrigation_button, WF_HIDDEN);
      } else {
        set_wflag(order_irrigation_button, WF_HIDDEN);
      }

      pextra = next_extra_for_tile(ptile, EC_MINE,
                                   unit_owner(punit), punit);
      if (pextra != NULL
          && can_unit_do_activity_targeted_client(punit, ACTIVITY_MINE, pextra)) {
        time = turns_to_activity_done(ptile, ACTIVITY_MINE, pextra, punit);
        /* TRANS: "Build Mine (M) 5 turns" */
        fc_snprintf(cbuf, sizeof(cbuf), _("Build %s (%s) %d %s"),
                    extra_name_translation(pextra), "M", time,
                    PL_("turn", "turns", time));
        order_mine_button->theme = current_theme->o_mine_icon;

        copy_chars_to_utf8_str(order_mine_button->info_label, cbuf);
        clear_wflag(order_mine_button, WF_HIDDEN);
      } else {
        set_wflag(order_mine_button, WF_HIDDEN);
      }

      if (can_unit_do_activity_client(punit, ACTIVITY_CULTIVATE)) {
        /* Activity always results in terrain change */
        time = turns_to_activity_done(ptile, ACTIVITY_CULTIVATE, NULL, punit);
        fc_snprintf(cbuf, sizeof(cbuf), "%s %s (%s) %d %s",
                    _("Cultivate to"),
                    terrain_name_translation(pterrain->cultivate_result),
                    "Shift+I", time, PL_("turn", "turns", time));
        copy_chars_to_utf8_str(order_cultivate_button->info_label, cbuf);
        clear_wflag(order_cultivate_button, WF_HIDDEN);
      } else {
        set_wflag(order_cultivate_button, WF_HIDDEN);
      }

      if (can_unit_do_activity_client(punit, ACTIVITY_PLANT)) {
        /* Activity always results in terrain change */
        time = turns_to_activity_done(ptile, ACTIVITY_PLANT, NULL, punit);
        fc_snprintf(cbuf, sizeof(cbuf), "%s %s (%s) %d %s",
                    _("Plant to"),
                    terrain_name_translation(pterrain->plant_result),
                    "Shift+M", time, PL_("turn", "turns", time));
        copy_chars_to_utf8_str(order_plant_button->info_label, cbuf);
        clear_wflag(order_plant_button, WF_HIDDEN);
      } else {
        set_wflag(order_plant_button, WF_HIDDEN);
      }

      if (can_unit_do_activity_client(punit, ACTIVITY_TRANSFORM)) {
        /* Activity always results in terrain change */
        time = turns_to_activity_done(ptile, ACTIVITY_TRANSFORM, NULL, punit);
        fc_snprintf(cbuf, sizeof(cbuf), "%s %s (%s) %d %s",
                    _("Transform to"),
                    terrain_name_translation(pterrain->transform_result),
                    "O", time, PL_("turn", "turns", time));
        copy_chars_to_utf8_str(order_transform_button->info_label, cbuf);
        clear_wflag(order_transform_button, WF_HIDDEN);
      } else {
        set_wflag(order_transform_button, WF_HIDDEN);
      }

      pbase = get_base_by_gui_type(BASE_GUI_FORTRESS, punit, ptile);
      if (pbase != NULL) {
        struct extra_type *base_extra = base_extra_get(pbase);

        time = turns_to_activity_done(ptile, ACTIVITY_BASE, base_extra, punit);
        /* TRANS: "Build Fortress (Shift+F) 5 turns" */
        fc_snprintf(cbuf, sizeof(cbuf), _("Build %s (%s) %d %s"),
                    extra_name_translation(base_extra), "Shift+F", time,
                    PL_("turn", "turns", time));
        copy_chars_to_utf8_str(order_fortress_button->info_label, cbuf);
        clear_wflag(order_fortress_button, WF_HIDDEN);
      } else {
        set_wflag(order_fortress_button, WF_HIDDEN);
      }

      if (can_unit_do_activity_client(punit, ACTIVITY_FORTIFYING)) {
        local_show(ID_UNIT_ORDER_FORTIFY);
      } else {
        local_hide(ID_UNIT_ORDER_FORTIFY);
      }

      pbase = get_base_by_gui_type(BASE_GUI_AIRBASE, punit, ptile);
      if (pbase != NULL) {
        struct extra_type *base_extra = base_extra_get(pbase);

        time = turns_to_activity_done(ptile, ACTIVITY_BASE, base_extra, punit);
        /* TRANS: "Build Airbase (Shift+E) 5 turns" */
        fc_snprintf(cbuf, sizeof(cbuf), _("Build %s (%s) %d %s"),
                    extra_name_translation(base_extra), "Shift+E", time,
                    PL_("turn", "turns", time));
        copy_chars_to_utf8_str(order_airbase_button->info_label, cbuf);
        clear_wflag(order_airbase_button, WF_HIDDEN);
      } else {
        set_wflag(order_airbase_button, WF_HIDDEN);
      }

      if (can_unit_paradrop(&(wld.map), punit)) {
        local_show(ID_UNIT_ORDER_PARADROP);
      } else {
        local_hide(ID_UNIT_ORDER_PARADROP);
      }

      pextra = prev_cleanable_in_tile(ptile, unit_owner(punit), punit);

      if (pextra != NULL
          && can_unit_do_activity_targeted_client(punit, ACTIVITY_CLEAN,
                                                  pextra)) {
        time = turns_to_activity_done(ptile, ACTIVITY_CLEAN,
                                      pextra, punit);
        /* TRANS: "Clean Pollution (P) 3 turns" */
        fc_snprintf(cbuf, sizeof(cbuf), _("Clean %s (%s) %d %s"),
                    extra_name_translation(pextra), "P", time,
                    PL_("turn", "turns", time));
        copy_chars_to_utf8_str(order_clean_button->info_label, cbuf);
        clear_wflag(order_clean_button, WF_HIDDEN);
      } else {
        set_wflag(order_clean_button, WF_HIDDEN);
      }

      if (can_unit_do_activity_client(punit, ACTIVITY_SENTRY)) {
        local_show(ID_UNIT_ORDER_SENTRY);
      } else {
        local_hide(ID_UNIT_ORDER_SENTRY);
      }

      if (can_unit_do_activity_client(punit, ACTIVITY_PILLAGE)) {
        local_show(ID_UNIT_ORDER_PILLAGE);
      } else {
        local_hide(ID_UNIT_ORDER_PILLAGE);
      }

      if (pcity != NULL && can_unit_change_homecity(&(wld.map), punit)
          && pcity->id != punit->homecity) {
        local_show(ID_UNIT_ORDER_HOMECITY);
      } else {
        local_hide(ID_UNIT_ORDER_HOMECITY);
      }

      if (punit->client.occupied) {
        local_show(ID_UNIT_ORDER_UNLOAD_TRANSPORTER);
      } else {
        local_hide(ID_UNIT_ORDER_UNLOAD_TRANSPORTER);
      }

      if (units_can_load(punits)) {
        local_show(ID_UNIT_ORDER_BOARD);
      } else {
        local_hide(ID_UNIT_ORDER_BOARD);
      }

      if (units_can_unload(&(wld.map), punits)) {
        local_show(ID_UNIT_ORDER_DEBOARD);
      } else {
        local_hide(ID_UNIT_ORDER_DEBOARD);
      }

      if (is_unit_activity_on_tile(ACTIVITY_SENTRY, unit_tile(punit))) {
        local_show(ID_UNIT_ORDER_WAKEUP_OTHERS);
      } else {
        local_hide(ID_UNIT_ORDER_WAKEUP_OTHERS);
      }

      if (can_unit_do_autoworker(punit)) {
        local_show(ID_UNIT_ORDER_AUTO_WORKER);
      } else {
        local_hide(ID_UNIT_ORDER_AUTO_WORKER);
      }

      if (can_unit_do_activity_client(punit, ACTIVITY_EXPLORE)) {
        local_show(ID_UNIT_ORDER_AUTO_EXPLORE);
      } else {
        local_hide(ID_UNIT_ORDER_AUTO_EXPLORE);
      }

      {
        bool conn_possible = FALSE;
        struct extra_type_list *extras;

        extras = extra_type_list_by_cause(EC_IRRIGATION);

        if (extra_type_list_size(extras) > 0) {
          struct extra_type *tgt;

          tgt = extra_type_list_get(extras, 0);
          conn_possible = can_units_do_connect(punits, ACTIVITY_IRRIGATE, tgt);
        }

        if (conn_possible) {
          local_show(ID_UNIT_ORDER_CONNECT_IRRIGATE);
        } else {
          local_hide(ID_UNIT_ORDER_CONNECT_IRRIGATE);
        }
      }

      {
        struct road_type *proad = road_by_gui_type(ROAD_GUI_ROAD);
        bool road_conn_possible;

        if (proad != NULL) {
          struct extra_type *tgt;

          tgt = road_extra_get(proad);

          road_conn_possible = can_unit_do_connect(punit, ACTIVITY_GEN_ROAD, tgt);
        } else {
          road_conn_possible = FALSE;
        }

        if (road_conn_possible) {
          local_show(ID_UNIT_ORDER_CONNECT_ROAD);
        } else {
          local_hide(ID_UNIT_ORDER_CONNECT_ROAD);
        }
      }

      {
        struct road_type *proad = road_by_gui_type(ROAD_GUI_RAILROAD);
        bool road_conn_possible;

        if (proad != NULL) {
          struct extra_type *tgt;

          tgt = road_extra_get(proad);

          road_conn_possible = can_unit_do_connect(punit, ACTIVITY_GEN_ROAD, tgt);
        } else {
          road_conn_possible = FALSE;
        }

        if (road_conn_possible) {
          local_show(ID_UNIT_ORDER_CONNECT_RAILROAD);
        } else {
          local_hide(ID_UNIT_ORDER_CONNECT_RAILROAD);
        }
      }

     if (unit_can_do_action(punit, ACTION_ANY)) {
       local_show(ID_UNIT_ORDER_DIPLOMAT_DLG);
      } else {
       local_hide(ID_UNIT_ORDER_DIPLOMAT_DLG);
      }

      if (unit_can_do_action(punit, ACTION_NUKE)) {
        local_show(ID_UNIT_ORDER_NUKE);
      } else {
        local_hide(ID_UNIT_ORDER_NUKE);
      }

      if (pcity && pcity->airlift) {
        local_show(ID_UNIT_ORDER_AIRLIFT);
        hide(ID_UNIT_ORDER_GOTO_CITY);
      } else {
        local_show(ID_UNIT_ORDER_GOTO_CITY);
        local_hide(ID_UNIT_ORDER_AIRLIFT);
      }

      if (pcity && can_upgrade_unittype(client.conn.playing,
                                        unit_type_get(punit))) {
        local_show(ID_UNIT_ORDER_UPGRADE);
      } else {
        local_hide(ID_UNIT_ORDER_UPGRADE);
      }

      if (unit_can_convert(&(wld.map), punit)) {
        local_show(ID_UNIT_ORDER_CONVERT);
      } else {
        local_hide(ID_UNIT_ORDER_CONVERT);
      }

      set_new_order_widget_start_pos();
      counter = redraw_order_widgets();

    } else {
      if (counter) {
        hide_group(begin_order_widget_list, end_order_widget_list);
      }

      counter = 0;
    }
  }
}

/**********************************************************************//**
  Disable all unit order buttons.
**************************************************************************/
void disable_order_buttons(void)
{
  undraw_order_widgets();
  disable_group(begin_order_widget_list, end_order_widget_list);
  redraw_order_widgets();
}

/**********************************************************************//**
  Enable all unit order buttons.
**************************************************************************/
void enable_order_buttons(void)
{
  if (can_client_issue_orders()) {
    undraw_order_widgets();
    enable_group(begin_order_widget_list, end_order_widget_list);
    redraw_order_widgets();
  }
}
