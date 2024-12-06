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
                          mapctrl.c  -  description
                             -------------------
    begin                : Thu Sep 05 2002
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
#include "unit.h"
#include "unitlist.h"

/* client */
#include "client_main.h"
#include "climisc.h"
#include "overview_common.h"
#include "pages_g.h"
#include "update_queue.h"
#include "zoom.h"

/* client/gui-sdl3 */
#include "citydlg.h"
#include "cityrep.h"
#include "colors.h"
#include "dialogs.h"
#include "finddlg.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_mouse.h"
#include "gui_tilespec.h"
#include "mapview.h"
#include "menu.h"
#include "messagewin.h"
#include "optiondlg.h"
#include "plrdlg.h"
#include "repodlgs.h"
#include "sprite.h"
#include "themespec.h"
#include "widget.h"
#include "wldlg.h"

#include "mapctrl.h"

#undef SCALE_MINIMAP
#undef SCALE_UNITINFO

extern int overview_start_x;
extern int overview_start_y;
extern bool is_unit_move_blocked;

static char *suggested_city_name = NULL;
static struct small_dialog *new_city_dlg = NULL;
extern struct widget *options_button;

#ifdef SCALE_MINIMAP
static struct small_dialog *scale_minimap_dlg = NULL;
static int popdown_scale_minmap_dlg_callback(struct widget *pwidget);
#endif /* SCALE_MINIMAP */

#ifdef SCALE_UNITINFO
static struct small_dialog *scale_unit_info_dlg = NULL;
static int info_width, info_height = 0, info_width_min, info_height_min;
static int popdown_scale_unitinfo_dlg_callback(struct widget *pwidget);
static void remake_unitinfo(int w, int h);
#endif /* SCALE_UNITINFO */

static struct advanced_dialog *minimap_dlg = NULL;
static struct advanced_dialog *unit_info_dlg = NULL;

int overview_w = 0;
int overview_h = 0;
int unitinfo_w = 0;
int unitinfo_h = 0;

bool draw_goto_patrol_lines = FALSE;
static struct widget *new_turn_button = NULL;
static struct widget *units_info_window = NULL;
static struct widget *minimap_window = NULL;
static struct widget *find_city_button = NULL;
static struct widget *revolution_button = NULL;
static struct widget *tax_button = NULL;
static struct widget *research_button = NULL;

static void enable_minimap_widgets(void);
static void disable_minimap_widgets(void);
static void enable_unitinfo_widgets(void);
static void disable_unitinfo_widgets(void);

/* ================================================================ */

/**********************************************************************//**
  User interacted with nations button.
**************************************************************************/
static int players_action_callback(struct widget *pwidget)
{
  set_wstate(pwidget, FC_WS_NORMAL);
  widget_redraw(pwidget);
  widget_mark_dirty(pwidget);
  if (main_data.event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    switch (main_data.event.button.button) {
#if 0
    case SDL_BUTTON_LEFT:

      break;
    case SDL_BUTTON_MIDDLE:

      break;
#endif /* 0 */
    case SDL_BUTTON_RIGHT:
      popup_players_nations_dialog();
      break;
    default:
      popup_players_dialog(true);
      break;
    }
  } else {
    popup_players_dialog(true);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with units button.
**************************************************************************/
static int units_action_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    set_wstate(pwidget, FC_WS_NORMAL);
    widget_redraw(pwidget);
    widget_mark_dirty(pwidget);
    units_report_dialog_popup(FALSE);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with cities button.
**************************************************************************/
static int cities_action_callback(struct widget *button)
{
  set_wstate(button, FC_WS_DISABLED);
  widget_redraw(button);
  widget_mark_dirty(button);
  if (main_data.event.type == SDL_EVENT_KEY_DOWN) {
    /* Ctrl-F shortcut */
    popup_find_dialog();
  } else if (main_data.event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    switch (main_data.event.button.button) {
#if 0
    case SDL_BUTTON_LEFT:

      break;
    case SDL_BUTTON_MIDDLE:

      break;
#endif /* 0 */
    case SDL_BUTTON_RIGHT:
      popup_find_dialog();
      break;
    default:
      city_report_dialog_popup(FALSE);
      break;
    }
  } else if (PRESSED_EVENT(main_data.event)) {
    city_report_dialog_popup(FALSE);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with Turn Done button.
**************************************************************************/
static int end_turn_callback(struct widget *button)
{
  if (PRESSED_EVENT(main_data.event)) {
    widget_redraw(button);
    widget_flush(button);
    disable_focus_animation();
    key_end_turn();
  }

  return -1;
}

/**********************************************************************//**
  User interacted with Revolution button.
**************************************************************************/
static int revolution_callback(struct widget *button)
{
  if (PRESSED_EVENT(main_data.event)) {
    set_wstate(button, FC_WS_DISABLED);
    widget_redraw(button);
    widget_mark_dirty(button);
    popup_government_dialog();
  }

  return -1;
}

/**********************************************************************//**
  User interacted with Research button.
**************************************************************************/
static int research_callback(struct widget *button)
{
  if (PRESSED_EVENT(main_data.event)) {
    science_report_dialog_popup(TRUE);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with Economy button.
**************************************************************************/
static int economy_callback(struct widget *button)
{
  if (PRESSED_EVENT(main_data.event)) {
    economy_report_dialog_popup(FALSE);
  }

  return -1;
}

/* ====================================== */

/**********************************************************************//**
  Show/Hide Units Info Window
**************************************************************************/
static int toggle_unit_info_window_callback(struct widget *icon_widget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct widget *buf = NULL;

    clear_surface(icon_widget->theme, NULL);
    alphablit(current_theme->map_icon, NULL, icon_widget->theme, NULL, 255);

    if (get_num_units_in_focus() > 0) {
      undraw_order_widgets();
    }

    if (sdl3_client_flags & CF_UNITINFO_SHOWN) {
      /* HIDE */
      SDL_Surface *buf_surf;
      SDL_Rect src, window_area;

      set_wstate(icon_widget, FC_WS_NORMAL);
      selected_widget = NULL;

      if (units_info_window->private_data.adv_dlg->end_active_widget_list) {
        del_group(units_info_window->private_data.adv_dlg->begin_active_widget_list,
                  units_info_window->private_data.adv_dlg->end_active_widget_list);
      }
      if (units_info_window->private_data.adv_dlg->scroll) {
        hide_scrollbar(units_info_window->private_data.adv_dlg->scroll);
      }

      /* clear area under old unit info window */
      widget_undraw(units_info_window);
      widget_mark_dirty(units_info_window);

      /* new button direction */
      alphablit(current_theme->l_arrow_icon, NULL, icon_widget->theme, NULL,
                255);

      copy_chars_to_utf8_str(icon_widget->info_label,
                             _("Show Unit Info Window"));

      sdl3_client_flags &= ~CF_UNITINFO_SHOWN;

      set_new_unitinfo_window_pos();

      /* blit part of map window */
      src.x = 0;
      src.y = 0;
      src.w = (units_info_window->size.w - units_info_window->area.w) + BLOCKU_W;
      src.h = units_info_window->theme->h;

      window_area = units_info_window->size;
      alphablit(units_info_window->theme, &src,
                units_info_window->dst->surface, &window_area, 255);

      /* blit right vertical frame */
      buf_surf = resize_surface(current_theme->fr_right,
                                current_theme->fr_right->w,
                                units_info_window->area.h, 1);

      window_area.y = units_info_window->area.y;
      window_area.x = units_info_window->area.x + units_info_window->area.w;
      alphablit(buf_surf, NULL,
                units_info_window->dst->surface, &window_area, 255);
      FREESURFACE(buf_surf);

      /* redraw widgets */

      /* ID_ECONOMY */
      buf = units_info_window->prev;
      widget_redraw(buf);

      /* ===== */
      /* ID_RESEARCH */
      buf = buf->prev;
      widget_redraw(buf);

      /* ===== */
      /* ID_REVOLUTION */
      buf = buf->prev;
      widget_redraw(buf);

      /* ===== */
      /* ID_TOGGLE_UNITS_WINDOW_BUTTON */
      buf = buf->prev;
      widget_redraw(buf);

#ifdef SCALE_UNITINFO
      popdown_scale_unitinfo_dlg_callback(NULL);
#endif
    } else {
      if (main_window_width() - units_info_window->size.w >=
                  minimap_window->dst->dest_rect.x + minimap_window->size.w) {

        set_wstate(icon_widget, FC_WS_NORMAL);
        selected_widget = NULL;

        /* SHOW */
        copy_chars_to_utf8_str(icon_widget->info_label,
                               _("Hide Unit Info Window"));

        alphablit(current_theme->r_arrow_icon, NULL, icon_widget->theme, NULL,
                  255);

        sdl3_client_flags |= CF_UNITINFO_SHOWN;

        set_new_unitinfo_window_pos();

        widget_mark_dirty(units_info_window);

        redraw_unit_info_label(get_units_in_focus());
      } else {
        alphablit(current_theme->l_arrow_icon, NULL, icon_widget->theme, NULL,
                  255);
        widget_redraw(icon_widget);
        widget_mark_dirty(icon_widget);
      }
    }

    if (get_num_units_in_focus() > 0) {
      update_order_widgets();
    }

    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  Show/Hide Mini Map
**************************************************************************/
static int toggle_map_window_callback(struct widget *map_button)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct unit *focus = head_of_units_in_focus();
    struct widget *pwidget;

    /* make new map icon */
    clear_surface(map_button->theme, NULL);
    alphablit(current_theme->map_icon, NULL, map_button->theme, NULL, 255);

    set_wstate(minimap_window, FC_WS_NORMAL);

    if (focus) {
      undraw_order_widgets();
    }

    if (sdl3_client_flags & CF_OVERVIEW_SHOWN) {
      /* Hide MiniMap */
      SDL_Surface *buf_surf;
      SDL_Rect src, map_area = minimap_window->size;

      set_wstate(map_button, FC_WS_NORMAL);
      selected_widget = NULL;

      copy_chars_to_utf8_str(map_button->info_label, _("Show Mini Map"));

      /* Make new map icon */
      alphablit(current_theme->r_arrow_icon, NULL, map_button->theme, NULL, 255);

      sdl3_client_flags &= ~CF_OVERVIEW_SHOWN;

      /* Clear area under old map window */
      widget_undraw(minimap_window);
      widget_mark_dirty(minimap_window);

      minimap_window->size.w = (minimap_window->size.w - minimap_window->area.w) + BLOCKM_W;
      minimap_window->area.w = BLOCKM_W;

      set_new_minimap_window_pos();

      /* blit part of map window */
      src.x = minimap_window->theme->w - BLOCKM_W - current_theme->fr_right->w;
      src.y = 0;
      src.w = BLOCKM_W + current_theme->fr_right->w;
      src.h = minimap_window->theme->h;

      alphablit(minimap_window->theme, &src,
                minimap_window->dst->surface, &map_area, 255);

      /* blit left vertical frame theme */
      buf_surf = resize_surface(current_theme->fr_left,
                                current_theme->fr_left->w,
                                minimap_window->area.h, 1);

      map_area.y += adj_size(2);
      alphablit(buf_surf, NULL, minimap_window->dst->surface, &map_area, 255);
      FREESURFACE(buf_surf);

      /* redraw widgets */
      /* ID_NEW_TURN */
      pwidget = minimap_window->prev;
      widget_redraw(pwidget);

      /* ID_PLAYERS */
      pwidget = pwidget->prev;
      widget_redraw(pwidget);

      /* ID_CITIES */
      pwidget = pwidget->prev;
      widget_redraw(pwidget);

      /* ID_UNITS */
      pwidget = pwidget->prev;
      widget_redraw(pwidget);

      /* ID_CHATLINE_TOGGLE_LOG_WINDOW_BUTTON */
      pwidget = pwidget->prev;
      widget_redraw(pwidget);

#ifdef GUI_SDL3_SMALL_SCREEN
      /* Options */
      pwidget = pwidget->prev;
      widget_redraw(pwidget);
#endif /* GUI_SDL3_SMALL_SCREEN */

      /* ID_TOGGLE_MAP_WINDOW_BUTTON */
      pwidget = pwidget->prev;
      widget_redraw(pwidget);

#ifdef SCALE_MINIMAP
      popdown_scale_minmap_dlg_callback(NULL);
#endif
    } else {
      if (((minimap_window->size.w - minimap_window->area.w) +
            overview_w + BLOCKM_W) <= units_info_window->dst->dest_rect.x) {

        set_wstate(map_button, FC_WS_NORMAL);
        selected_widget = NULL;

        /* show MiniMap */
        copy_chars_to_utf8_str(map_button->info_label, _("Hide Mini Map"));

        alphablit(current_theme->l_arrow_icon, NULL, map_button->theme, NULL,
                  255);
        sdl3_client_flags |= CF_OVERVIEW_SHOWN;

        minimap_window->size.w =
          (minimap_window->size.w - minimap_window->area.w) + overview_w + BLOCKM_W;
        minimap_window->area.w = overview_w + BLOCKM_W;

        set_new_minimap_window_pos();

        widget_redraw(minimap_window);
        redraw_minimap_window_buttons();
        refresh_overview();
      } else {
        alphablit(current_theme->r_arrow_icon, NULL, map_button->theme, NULL,
                  255);
        widget_redraw(map_button);
        widget_mark_dirty(map_button);
      }
    }

    if (focus) {
      update_order_widgets();
    }

    flush_dirty();
  }

  return -1;
}

/* ====================================================================== */

/**********************************************************************//**
  User interacted with messages toggling button.
**************************************************************************/
static int toggle_msg_window_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (meswin_dialog_is_open()) {
      meswin_dialog_popdown();
      copy_chars_to_utf8_str(pwidget->info_label, _("Show Messages (F9)"));
    } else {
      meswin_dialog_popup(TRUE);
      copy_chars_to_utf8_str(pwidget->info_label, _("Hide Messages (F9)"));
    }

    selected_widget = pwidget;
    set_wstate(pwidget, FC_WS_SELECTED);
    widget_redraw(pwidget);
    widget_mark_dirty(pwidget);

    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  Update the size of the minimap
**************************************************************************/
int resize_minimap(void)
{
  overview_w = gui_options.overview.width;
  overview_h = gui_options.overview.height;
  overview_start_x = (overview_w - gui_options.overview.width) / 2;
  overview_start_y = (overview_h - gui_options.overview.height) / 2;

  if (C_S_RUNNING == client_state()) {
    popdown_minimap_window();
    popup_minimap_window();
    refresh_overview();
    menus_update();
  }

  return 0;
}

#ifdef SCALE_MINIMAP
/* ============================================================== */

/**********************************************************************//**
  User interacted with minimap scaling dialog
**************************************************************************/
static int move_scale_minimap_dlg_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    move_window_group(scale_minimap_dlg->begin_widget_list, pwindow);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with minimap scaling dialog closing button.
**************************************************************************/
static int popdown_scale_minimap_dlg_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (scale_minimap_dlg) {
      popdown_window_group_dialog(scale_minimap_dlg->begin_widget_list,
                                  scale_minimap_dlg->end_widget_list);
      FC_FREE(scale_minimap_dlg);
      if (pwidget) {
        flush_dirty();
      }
    }
  }

  return -1;
}

/**********************************************************************//**
  User interacted with minimap width increase button.
**************************************************************************/
static int up_width_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    widget_redraw(pwidget);
    widget_mark_dirty(pwidget);
    if ((((OVERVIEW_TILE_WIDTH + 1) * map.xsize) +
         (minimap_window->size.w - minimap_window->area.w) + BLOCKM_W) <=
         units_info_window->dst->dest_rect.x) {
      char cbuf[4];

      fc_snprintf(cbuf, sizeof(cbuf), "%d", OVERVIEW_TILE_WIDTH);
      copy_chars_to_utf8_str(pwidget->next->string_utf8, cbuf);
      widget_redraw(pwidget->next);
      widget_mark_dirty(pwidget->next);

      calculate_overview_dimensions();
      resize_minimap();
    }
    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  User interacted with minimap width decrease button.
**************************************************************************/
static int down_width_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    widget_redraw(pwidget);
    widget_mark_dirty(pwidget);
    if (OVERVIEW_TILE_WIDTH > 1) {
      char cbuf[4];

      fc_snprintf(cbuf, sizeof(cbuf), "%d", OVERVIEW_TILE_WIDTH);
      copy_chars_to_utf8_str(pwidget->prev->string_utf8, cbuf);
      widget_redraw(pwidget->prev);
      widget_mark_dirty(pwidget->prev);

      resize_minimap();
    }
    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  User interacted with minimap height increase button.
**************************************************************************/
static int up_height_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    widget_redraw(pwidget);
    widget_mark_dirty(pwidget);
    if (main_data.screen->h -
      ((OVERVIEW_TILE_HEIGHT + 1) * map.ysize
       + (current_theme->fr_bottom->h * 2)) >= 40) {
      char cbuf[4];

      OVERVIEW_TILE_HEIGHT++;
      fc_snprintf(cbuf, sizeof(cbuf), "%d", OVERVIEW_TILE_HEIGHT);
      copy_chars_to_utf8_str(pwidget->next->string_utf8, cbuf);
      widget_redraw(pwidget->next);
      widget_mark_dirty(pwidget->next);
      resize_minimap();
    }
    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  User interacted with minimap height decrease button.
**************************************************************************/
static int down_height_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    widget_redraw(pwidget);
    widget_mark_dirty(pwidget);
    if (OVERVIEW_TILE_HEIGHT > 1) {
      char cbuf[4];

      OVERVIEW_TILE_HEIGHT--;
      fc_snprintf(cbuf, sizeof(cbuf), "%d", OVERVIEW_TILE_HEIGHT);
      copy_chars_to_utf8_str(pwidget->prev->string_utf8, cbuf);
      widget_redraw(pwidget->prev);
      widget_mark_dirty(pwidget->prev);

      resize_minimap();
    }
    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  Open minimap scaling dialog.
**************************************************************************/
static void popup_minimap_scale_dialog(void)
{
  SDL_Surface *text1, *text2;
  utf8_str *pstr = NULL;
  struct widget *pwindow = NULL;
  struct widget *buf = NULL;
  char cbuf[4];
  int window_x = 0, window_y = 0;
  SDL_Rect area;

  if (scale_minimap_dlg || !(sdl3_client_flags & CF_OVERVIEW_SHOWN)) {
    return;
  }

  scale_minimap_dlg = fc_calloc(1, sizeof(struct small_dialog));

  /* Create window */
  pstr = create_utf8_from_char_fonto(_("Scale Mini Map"), FONTO_ATTENTION);
  pstr->style |= TTF_STYLE_BOLD;
  pwindow = create_window_skeleton(NULL, pstr, 0);
  pwindow->action = move_scale_minimap_dlg_callback;
  set_wstate(pwindow, FC_WS_NORMAL);
  add_to_gui_list(ID_WINDOW, pwindow);
  scale_minimap_dlg->end_widget_list = pwindow;

  area = pwindow->area;

  /* ----------------- */
  pstr = create_utf8_from_char_fonto(_("Single Tile Width"), FONTO_ATTENTION);
  text1 = create_text_surf_from_utf8(pstr);
  area.w = MAX(area.w, text1->w + adj_size(30));

  copy_chars_to_utf8_str(pstr, _("Single Tile Height"));
  text2 = create_text_surf_from_utf8(pstr);
  area.w = MAX(area.w, text2->w + adj_size(30));
  FREEUTF8_STR(pstr);

  buf = create_themeicon_button(current_theme->l_arrow_icon, pwindow->dst,
                                NULL, 0);
  buf->action = down_width_callback;
  set_wstate(buf, FC_WS_NORMAL);
  add_to_gui_list(ID_BUTTON, buf);

  fc_snprintf(cbuf, sizeof(cbuf), "%d" , OVERVIEW_TILE_WIDTH);
  pstr = create_utf8_from_char_fonto(cbuf, FONTO_MAX);
  pstr->style |= (TTF_STYLE_BOLD|SF_CENTER);
  buf = create_iconlabel(NULL, pwindow->dst, pstr, WF_RESTORE_BACKGROUND);
  buf->size.w = MAX(adj_size(50), buf->size.w);
  area.h += buf->size.h + adj_size(5);
  add_to_gui_list(ID_LABEL, buf);

  buf = create_themeicon_button(current_theme->r_arrow_icon, pwindow->dst,
                                NULL, 0);
  buf->action = up_width_callback;
  set_wstate(buf, FC_WS_NORMAL);
  add_to_gui_list(ID_BUTTON, buf);

  /* ------------ */
  buf = create_themeicon_button(current_theme->l_arrow_icon, pwindow->dst,
                                NULL, 0);
  buf->action = down_height_callback;
  set_wstate(buf, FC_WS_NORMAL);
  add_to_gui_list(ID_BUTTON, buf);

  fc_snprintf(cbuf, sizeof(cbuf), "%d" , OVERVIEW_TILE_HEIGHT);
  pstr = create_utf8_from_char_fonto(cbuf, FONTO_MAX);
  pstr->style |= (TTF_STYLE_BOLD|SF_CENTER);
  buf = create_iconlabel(NULL, pwindow->dst, pstr, WF_RESTORE_BACKGROUND);
  buf->size.w = MAX(adj_size(50), buf->size.w);
  area.h += buf->size.h + adj_size(20);
  add_to_gui_list(ID_LABEL, buf);

  buf = create_themeicon_button(current_theme->r_arrow_icon, pwindow->dst,
                                NULL, 0);
  buf->action = up_height_callback;
  set_wstate(buf, FC_WS_NORMAL);
  add_to_gui_list(ID_BUTTON, buf);
  area.w = MAX(area.w , buf->size.w * 2 + buf->next->size.w + adj_size(20));

  /* ------------ */
  pstr = create_utf8_from_char_fonto(_("Exit"), FONTO_ATTENTION);
  buf = create_themeicon_button(current_theme->cancel_icon, pwindow->dst,
                                pstr, 0);
  buf->action = popdown_scale_minimap_dlg_callback;
  set_wstate(buf, FC_WS_NORMAL);
  scale_minimap_dlg->begin_widget_list = buf;
  add_to_gui_list(ID_BUTTON, buf);
  area.h += buf->size.h + adj_size(10);
  area.w = MAX(area.w, buf->size.w + adj_size(20));
  /* ------------ */

  area.h += adj_size(20);

  resize_window(pwindow, NULL, get_theme_color(COLOR_THEME_BACKGROUND),
                (pwindow->size.w - pwindow->area.w) + area.w,
                (pwindow->size.h - pwindow->area.h) + area.h);

  area = pwindow->area;

  if (main_data.event.motion.x + pwindow->size.w > main_window_width()) {
    if (main_data.event.motion.x - pwindow->size.w >= 0) {
      window_x = main_data.event.motion.x - pwindow->size.w;
    } else {
      window_x = (main_window_width() - pwindow->size. w) / 2;
    }
  } else {
    window_x = main_data.event.motion.x;
  }

  if (main_data.event.motion.y + pwindow->size.h >= main_data.screen->h) {
    if (main_data.event.motion.y - pwindow->size.h >= 0) {
      window_y = main_data.event.motion.y - pwindow->size.h;
    } else {
      window_y = (main_data.screen->h - pwindow->size.h) / 2;
    }
  } else {
    window_y = main_data.event.motion.y;
  }

  widget_set_position(pwindow, window_x, window_y);

  blit_entire_src(text1, pwindow->theme, 15, area.y + 1);
  FREESURFACE(text1);

  /* width label */
  buf = pwindow->prev->prev;
  buf->size.y = area.y + adj_size(16);
  buf->size.x = area.x + (area.w - buf->size.w) / 2;

  /* width left button */
  buf->next->size.y = buf->size.y + buf->size.h - buf->next->size.h;
  buf->next->size.x = buf->size.x - buf->next->size.w;

  /* width right button */
  buf->prev->size.y = buf->size.y + buf->size.h - buf->prev->size.h;
  buf->prev->size.x = buf->size.x + buf->size.w;

  /* height label */
  buf = buf->prev->prev->prev;
  buf->size.y = buf->next->next->next->size.y + buf->next->next->next->size.h + adj_size(20);
  buf->size.x = area.x + (area.w - buf->size.w) / 2;

  blit_entire_src(text2, pwindow->theme, adj_size(15), buf->size.y - text2->h - adj_size(2));
  FREESURFACE(text2);

  /* height left button */
  buf->next->size.y = buf->size.y + buf->size.h - buf->next->size.h;
  buf->next->size.x = buf->size.x - buf->next->size.w;

  /* height right button */
  buf->prev->size.y = buf->size.y + buf->size.h - buf->prev->size.h;
  buf->prev->size.x = buf->size.x + buf->size.w;

  /* exit button */
  buf = buf->prev->prev;
  buf->size.x = area.x + (area.w - buf->size.w) / 2;
  buf->size.y = area.y + area.h - buf->size.h - adj_size(7);

  /* -------------------- */
  redraw_group(scale_minimap_dlg->begin_widget_list, pwindow, 0);
  widget_flush(pwindow);
}
#endif /* SCALE_MINIMAP */

/* ==================================================================== */
#ifdef SCALE_UNITINFO

/**********************************************************************//**
  User interacted with unitinfo scaling dialog.
**************************************************************************/
static int move_scale_unitinfo_dlg_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    move_window_group(scale_unit_info_dlg->begin_widget_list, pwindow);
  }

  return -1;
}

/**********************************************************************//**
  Close unitinfo scaling dialog.
**************************************************************************/
static int popdown_scale_unitinfo_dlg_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (scale_unit_info_dlg) {
      popdown_window_group_dialog(scale_unit_info_dlg->begin_widget_list,
                                  scale_unit_info_dlg->end_widget_list);
      FC_FREE(scale_unit_info_dlg);
      if (pwidget) {
        flush_dirty();
      }
    }
  }

  return -1;
}

/**********************************************************************//**
  Rebuild unitinfo widget.
**************************************************************************/
static void remake_unitinfo(int w, int h)
{
  SDL_Color bg_color = {255, 255, 255, 128};
  SDL_Surface *surf;
  SDL_Rect area = {units_info_window->area.x + BLOCKU_W,
                   units_info_window->area.y, 0, 0};
  struct widget *pwidget = units_info_window;

  if (w < DEFAULT_UNITS_W - BLOCKU_W) {
    w = (units_info_window->size.w - units_info_window->area.w) + DEFAULT_UNITS_W;
  } else {
    w += (units_info_window->size.w - units_info_window->area.w) + BLOCKU_W;
  }

  if (h < DEFAULT_UNITS_H) {
    h = (units_info_window->size.h - units_info_window->area.h) + DEFAULT_UNITS_H;
  } else {
    h += (units_info_window->size.h - units_info_window->area.h);
  }

  /* Clear area under old map window */
  clear_surface(pwidget->dst->surface, &pwidget->size);
  widget_mark_dirty(pwidget);

  pwidget->size.w = w;
  pwidget->size.h = h;

  pwidget->size.x = main_window_width() - w;
  pwidget->size.y = main_window_height() - h;

  FREESURFACE(pwidget->theme);
  pwidget->theme = create_surf(w, h);

  draw_frame(pwidget->theme, 0, 0, pwidget->size.w, pwidget->size.h);

  surf = resize_surface(current_theme->Block, BLOCKU_W,
    pwidget->size.h - ((units_info_window->size.h - units_info_window->area.h)), 1);

  blit_entire_src(surf, pwidget->theme, units_info_window->area.x,
                  units_info_window->area.y);
  FREESURFACE(surf);

  area.w = w - BLOCKU_W - (units_info_window->size.w - units_info_window->area.w);
  area.h = h - (units_info_window->size.h - units_info_window->area.h);
  SDL_FillSurfaceRect(pwidget->theme, &area, map_rgba(pwidget->theme->format, bg_color));

  /* Economy button */
  pwidget = tax_button;
  FREESURFACE(pwidget->gfx);
  pwidget->size.x = pwidget->dst->surface->w - w + units_info_window->area.x
                                             + (BLOCKU_W - pwidget->size.w) / 2;
  pwidget->size.y = pwidget->dst->surface->h - h + pwidget->area.y + 2;

  /* Research button */
  pwidget = pwidget->prev;
  FREESURFACE(pwidget->gfx);
  pwidget->size.x = pwidget->dst->surface->w - w + units_info_window->area.x
                                             + (BLOCKU_W - pwidget->size.w) / 2;
  pwidget->size.y = pwidget->dst->surface->h - h + units_info_window->area.y +
                    pwidget->size.h + 2;

  /* Revolution button */
  pwidget = pwidget->prev;
  FREESURFACE(pwidget->gfx);
  pwidget->size.x = pwidget->dst->surface->w - w + units_info_window->area.x
                                             + (BLOCKU_W - pwidget->size.w) / 2;
  pwidget->size.y = pwidget->dst->surface->h - h + units_info_window->area.y +
                    pwidget->size.h * 2 + 2;

  /* Show/hide unit's window button */
  pwidget = pwidget->prev;
  FREESURFACE(pwidget->gfx);
  pwidget->size.x = pwidget->dst->surface->w - w + units_info_window->area.x
                                             + (BLOCKU_W - pwidget->size.w) / 2;
  pwidget->size.y = units_info_window->area.y + units_info_window->area.h -
                    pwidget->size.h - 2;

  unitinfo_w = w;
  unitinfo_h = h;
}

/**********************************************************************//**
  Resize unitinfo widget.
**************************************************************************/
int resize_unit_info(void)
{
  struct widget *info_window = get_unit_info_window_widget();
  int w = info_width * map.xsize;
  int h = info_height * map.ysize;
  int current_w = units_info_window->size.w - BLOCKU_W -
                  (info_window->size.w - info_window->area.w);
  int current_h = units_info_window->size.h -
                  (info_window->size.h - info_window->area.h);

  if ((((current_w > DEFAULT_UNITS_W - BLOCKU_W)
        || (w > DEFAULT_UNITS_W - BLOCKU_W)) && (current_w != w))
      || (((current_h > DEFAULT_UNITS_H) || (h > DEFAULT_UNITS_H)) && (current_h != h))) {
    remake_unitinfo(w, h);
  }

  if (C_S_RUNNING == client_state()) {
    menus_update();
  }
  update_unit_info_label(get_units_in_focus());

  return 0;
}

/**********************************************************************//**
  User interacted with unitinfo width increase button.
**************************************************************************/
static int up_info_width_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    widget_redraw(pwidget);
    widget_mark_dirty(pwidget);
    if (main_window_width() - ((info_width + 1) * map.xsize + BLOCKU_W +
         (minimap_window->size.w - minimap_window->area.w)) >=
         minimap_window->size.x + minimap_window->size.w) {
      info_width++;
      resize_unit_info();
    }
    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  User interacted with unitinfo width decrease button.
**************************************************************************/
static int down_info_width_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    widget_redraw(pwidget);
    widget_mark_dirty(pwidget);
    if (info_width > info_width_min) {
      info_width--;
      resize_unit_info();
    }
    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  User interacted with unitinfo height increase button.
**************************************************************************/
static int up_info_height_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    widget_redraw(pwidget);
    widget_mark_dirty(pwidget);
    if (main_data.screen->h - ((info_height + 1) * map.ysize +
        (units_info_window->size.h - units_info_window->area.h)) >= adj_size(40)) {
      info_height++;
      resize_unit_info();
    }
    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  User interacted with unitinfo height decrease button.
**************************************************************************/
static int down_info_height_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    widget_redraw(pwidget);
    widget_mark_dirty(pwidget);
    if (info_height > info_height_min) {
      info_height--;
      resize_unit_info();
    }
    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  Open unitinfo scaling dialog.
**************************************************************************/
static void popup_unitinfo_scale_dialog(void)
{

#ifndef SCALE_UNITINFO
  return;
#endif  /* SCALE_UNITINFO */

  SDL_Surface *text1, *text2;
  utf8_str *pstr = NULL;
  struct widget *pwindow = NULL;
  struct widget *buf = NULL;
  int window_x = 0, window_y = 0;
  SDL_Rect area;

  if (scale_unit_info_dlg || !(sdl3_client_flags & CF_UNITINFO_SHOWN)) {
    return;
  }

  scale_unit_info_dlg = fc_calloc(1, sizeof(struct small_dialog));

  /* Create window */
  pstr = create_utf8_from_char_fonto(_("Scale Unit Info"), FONTO_ATTENTION);
  pstr->style |= TTF_STYLE_BOLD;
  pwindow = create_window_skeleton(NULL, pstr, 0);
  pwindow->action = move_scale_unitinfo_dlg_callback;
  set_wstate(pwindow, FC_WS_NORMAL);
  add_to_gui_list(ID_WINDOW, pwindow);
  scale_unit_info_dlg->end_widget_list = pwindow;

  area = pwindow->area;

  pstr = create_utf8_from_char_fonto(_("Width"), FONTO_ATTENTION);
  text1 = create_text_surf_from_utf8(pstr);
  area.w = MAX(area.w, text1->w + adj_size(30));
  area.h += MAX(adj_size(20), text1->h + adj_size(4));
  copy_chars_to_utf8_str(pstr, _("Height"));
  text2 = create_text_surf_from_utf8(pstr);
  area.w = MAX(area.w, text2->w + adj_size(30));
  area.h += MAX(adj_size(20), text2->h + adj_size(4));
  FREEUTF8STR(pstr);

  /* ----------------- */
  buf = create_themeicon_button(current_theme->l_arrow_icon, pwindow->dst,
                                NULL, 0);
  buf->action = down_info_width_callback;
  set_wstate(buf, FC_WS_NORMAL);
  add_to_gui_list(ID_BUTTON, buf);
  area.h += buf->size.h;

  buf = create_themeicon_button(current_theme->r_arrow_icon, pwindow->dst,
                                NULL, 0);
  buf->action = up_info_width_callback;
  set_wstate(buf, FC_WS_NORMAL);
  add_to_gui_list(ID_BUTTON, buf);

  /* ------------ */
  buf = create_themeicon_button(current_theme->l_arrow_icon, pwindow->dst,
                                NULL, 0);
  buf->action = down_info_height_callback;
  set_wstate(buf, FC_WS_NORMAL);
  add_to_gui_list(ID_BUTTON, buf);
  area.h += buf->size.h + adj_size(10);

  buf = create_themeicon_button(current_theme->r_arrow_icon, pwindow->dst,
                                NULL, 0);
  buf->action = up_info_height_callback;
  set_wstate(buf, FC_WS_NORMAL);
  add_to_gui_list(ID_BUTTON, buf);
  area.w = MAX(area.w , buf->size.w * 2 + adj_size(20));

  /* ------------ */
  pstr = create_utf8_from_char_fonto(_("Exit"), FONTO_ATTENTION);
  buf = create_themeicon_button(current_theme->cancel_icon,
                                pwindow->dst, pstr, 0);
  buf->action = popdown_scale_unitinfo_dlg_callback;
  set_wstate(buf, FC_WS_NORMAL);
  scale_unit_info_dlg->begin_widget_list = buf;
  add_to_gui_list(ID_BUTTON, buf);
  area.h += buf->size.h + adj_size(10);
  area.w = MAX(area.w, buf->size.w + adj_size(20));

  resize_window(pwindow, NULL, get_theme_color(COLOR_THEME_BACKGROUND),
                (pwindow->size.w - pwindow->area.w) + area.w,
                (pwindow->size.h - pwindow->area.h) + area.h);

  area = pwindow->area;

  /* ------------ */

  if (main_data.event.motion.x + pwindow->size.w > main_window_width()) {
    if (main_data.event.motion.x - pwindow->size.w >= 0) {
      window_x = main_data.event.motion.x - pwindow->size.w;
    } else {
      window_x = (main_window_width() - pwindow->size.w) / 2;
    }
  } else {
    window_x = main_data.event.motion.x;
  }

  if (main_data.event.motion.y + pwindow->size.h >= main_data.screen->h) {
    if (main_data.event.motion.y - pwindow->size.h >= 0) {
      window_y = main_data.event.motion.y - pwindow->size.h;
    } else {
      window_y = (pwindow->dst->surface->h - pwindow->size.h) / 2;
    }
  } else {
    window_y = main_data.event.motion.y;
  }

  widget_set_position(pwindow, window_x, window_y);

  /* width left button */
  buf = pwindow->prev;
  buf->size.y = area.y + MAX(adj_size(20), text1->h + adj_size(4));
  buf->size.x = area.x + (area.w - buf->size.w * 2) / 2;
  blit_entire_src(text1, pwindow->theme, adj_size(15), buf->size.y
                                                        - area.y - text1->h - adj_size(2));
  FREESURFACE(text1);

  /* width right button */
  buf->prev->size.y = buf->size.y;
  buf->prev->size.x = buf->size.x + buf->size.w;

  /* height left button */
  buf = buf->prev->prev;
  buf->size.y = buf->next->next->size.y +
    buf->next->next->size.h + MAX(adj_size(20), text2->h + adj_size(4));
  buf->size.x = area.x + (area.w - buf->size.w * 2) / 2;

  blit_entire_src(text2, pwindow->theme, adj_size(15), buf->size.y - area.y - text2->h - adj_size(2));
  FREESURFACE(text2);

  /* height right button */
  buf->prev->size.y = buf->size.y;
  buf->prev->size.x = buf->size.x + buf->size.w;

  /* exit button */
  buf = buf->prev->prev;
  buf->size.x = area.x + (area.w - buf->size.w) / 2;
  buf->size.y = area.y + area.h - buf->size.h - adj_size(7);

  if (!info_height) {
    info_width_min = (DEFAULT_UNITS_W - BLOCKU_W) / map.xsize;
    if (info_width_min == 1) {
      info_width_min = 0;
    }
    info_height_min = DEFAULT_UNITS_H / map.ysize;
    if (!info_height_min) {
      info_height_min = 1;
    }
    info_width = info_width_min;
    info_height = info_height_min;
  }

  /* -------------------- */
  redraw_group(scale_unit_info_dlg->begin_widget_list, pwindow, 0);
  widget_flush(pwindow);
}
#endif /* SCALE_UNITINFO */

/* ==================================================================== */

/**********************************************************************//**
  User interacted with minimap window.
**************************************************************************/
static int minimap_window_callback(struct widget *pwidget)
{
  int mouse_x, mouse_y;

  switch (main_data.event.button.button) {
  case SDL_BUTTON_RIGHT:
    mouse_x = main_data.event.motion.x - minimap_window->dst->dest_rect.x -
      minimap_window->area.x - overview_start_x;
    mouse_y = main_data.event.motion.y - minimap_window->dst->dest_rect.y -
      minimap_window->area.y - overview_start_y;
    if ((sdl3_client_flags & CF_OVERVIEW_SHOWN)
        && (mouse_x >= 0) && (mouse_x < overview_w)
        && (mouse_y >= 0) && (mouse_y < overview_h)) {
      int map_x, map_y;

      overview_to_map_pos(&map_x, &map_y, mouse_x, mouse_y);
      center_tile_mapcanvas(map_pos_to_tile(&(wld.map), map_x, map_y));
    }

    break;
  case SDL_BUTTON_MIDDLE:
    /* FIXME: scaling needs to be fixed */
#ifdef SCALE_MINIMAP
    popup_minimap_scale_dialog();
#endif
    break;
  default:
    break;
  }

  return -1;
}

/**********************************************************************//**
  User interacted with unitinfo window.
**************************************************************************/
static int unit_info_window_callback(struct widget *pwidget)
{
  if (main_data.event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    switch (main_data.event.button.button) {
#if 0
    case SDL_BUTTON_LEFT:

      break;
#endif
    case SDL_BUTTON_MIDDLE:
      request_center_focus_unit();
      break;
    case SDL_BUTTON_RIGHT:
#ifdef SCALE_UNITINFO
      popup_unitinfo_scale_dialog();
#endif
      break;
    default:
      key_unit_wait();
      break;
    }
  } else if (PRESSED_EVENT(main_data.event)) {
    key_unit_wait();
  }

  return -1;
}

/* ============================== Public =============================== */

/**********************************************************************//**
  This Function is used when resize main_data.screen.
  We must set new Units Info Win. start position.
**************************************************************************/
void set_new_unitinfo_window_pos(void)
{
  struct widget *unit_window = units_info_window;
  struct widget *pwidget;
  SDL_Rect area;

  if (sdl3_client_flags & CF_UNITINFO_SHOWN) {
    widget_set_position(units_info_window,
                        main_window_width() - units_info_window->size.w,
                        main_window_height() - units_info_window->size.h);
  } else {
    widget_set_position(unit_window,
                        main_window_width() - BLOCKU_W - current_theme->fr_right->w,
                        main_window_height() - units_info_window->size.h);
  }

  area.x = units_info_window->area.x;
  area.y = units_info_window->area.y;
  area.w = BLOCKU_W;
  area.h = DEFAULT_UNITS_H;

  /* ID_ECONOMY */
  pwidget = tax_button;
  widget_set_area(pwidget, area);
  widget_set_position(pwidget,
                      area.x + (area.w - pwidget->size.w) / 2,
                      area.y + 2);

  /* ID_RESEARCH */
  pwidget = pwidget->prev;
  widget_set_area(pwidget, area);
  widget_set_position(pwidget,
                      area.x + (area.w - pwidget->size.w) / 2,
                      area.y + 2 + pwidget->size.h);

  /* ID_REVOLUTION */
  pwidget = pwidget->prev;
  widget_set_area(pwidget, area);
  widget_set_position(pwidget,
                      area.x + (area.w - pwidget->size.w) / 2,
                      area.y + 2 + (pwidget->size.h * 2));

  /* ID_TOGGLE_UNITS_WINDOW_BUTTON */
  pwidget = pwidget->prev;
  widget_set_area(pwidget, area);
  widget_set_position(pwidget,
                      area.x + (area.w - pwidget->size.w) / 2,
                      area.y + area.h - pwidget->size.h - 2);
}

/**********************************************************************//**
  This Function is used when resize main_data.screen.
  We must set new MiniMap start position.
**************************************************************************/
void set_new_minimap_window_pos(void)
{
  struct widget *pwidget;
  SDL_Rect area;

  widget_set_position(minimap_window, 0,
                      main_window_height() - minimap_window->size.h);

  area.x = minimap_window->size.w - current_theme->fr_right->w - BLOCKM_W;
  area.y = minimap_window->area.y;
  area.w = BLOCKM_W;
  area.h = minimap_window->area.h;

  /* ID_NEW_TURN */
  pwidget = minimap_window->prev;
  widget_set_area(pwidget, area);
  widget_set_position(pwidget,
                      area.x + adj_size(2) + pwidget->size.w,
                      area.y + 2);

  /* PLAYERS BUTTON */
  pwidget = pwidget->prev;
  widget_set_area(pwidget, area);
  widget_set_position(pwidget,
                      area.x + adj_size(2) + pwidget->size.w,
                      area.y + pwidget->size.h + 2);

  /* ID_FIND_CITY */
  pwidget = pwidget->prev;
  widget_set_area(pwidget, area);
  widget_set_position(pwidget,
                      area.x + adj_size(2) + pwidget->size.w,
                      area.y + pwidget->size.h * 2 + 2);

  /* UNITS BUTTON */
  pwidget = pwidget->prev;
  widget_set_area(pwidget, area);
  widget_set_position(pwidget,
                      area.x + adj_size(2),
                      area.y + 2);

  /* ID_CHATLINE_TOGGLE_LOG_WINDOW_BUTTON */
  pwidget = pwidget->prev;
  widget_set_area(pwidget, area);
  widget_set_position(pwidget,
                      area.x + adj_size(2),
                      area.y + pwidget->size.h + 2);

#ifdef GUI_SDL3_SMALL_SCREEN
  /* ID_TOGGLE_MAP_WINDOW_BUTTON */
  pwidget = pwidget->prev;
  widget_set_area(pwidget, area);
  widget_set_position(pwidget,
                      area.x + adj_size(2),
                      area.y + area.h - pwidget->size.h - 2);
#endif /* GUI_SDL3_SMALL_SCREEN */

  /* ID_TOGGLE_MAP_WINDOW_BUTTON */
  pwidget = pwidget->prev;
  widget_set_area(pwidget, area);
  widget_set_position(pwidget,
                      area.x + adj_size(2) + pwidget->size.w,
                      area.y + area.h - pwidget->size.h - 2);
}

/**********************************************************************//**
  Open unitinfo window.
**************************************************************************/
void popup_unitinfo_window(void)
{
  struct widget *pwidget, *pwindow;
  SDL_Surface *icon_theme = NULL;
  char buf[256];

  if (unit_info_dlg) {
    return;
  }

  unit_info_dlg = fc_calloc(1, sizeof(struct advanced_dialog));

  /* units_info_window */
  pwindow = create_window_skeleton(NULL, NULL, 0);

  resize_window(pwindow, NULL, NULL,
                (pwindow->size.w - pwindow->area.w) + DEFAULT_UNITS_W,
                (pwindow->size.h - pwindow->area.h) + DEFAULT_UNITS_H);

  draw_frame(pwindow->theme, 0, 0, pwindow->size.w, pwindow->size.h);

  unitinfo_w = pwindow->size.w;
  unitinfo_h = pwindow->size.h;

  icon_theme = resize_surface(current_theme->block, pwindow->area.w,
                              pwindow->area.h, 1);

  blit_entire_src(icon_theme, pwindow->theme, pwindow->area.x, pwindow->area.y);
  FREESURFACE(icon_theme);

  pwindow->action = unit_info_window_callback;

  add_to_gui_list(ID_UNITS_WINDOW, pwindow);

  units_info_window = pwindow;

  unit_info_dlg->end_widget_list = units_info_window;
  units_info_window->private_data.adv_dlg = unit_info_dlg;

  /* Economy button */
  pwidget = create_icon2(get_tax_surface(O_GOLD), units_info_window->dst,
                         WF_FREE_GFX | WF_WIDGET_HAS_INFO_LABEL
                         | WF_RESTORE_BACKGROUND | WF_FREE_THEME);

  fc_snprintf(buf, sizeof(buf), "%s (%s)", _("Economy"), "F5");
  pwidget->info_label = create_utf8_from_char_fonto(buf, FONTO_ATTENTION);
  pwidget->action = economy_callback;
  pwidget->key = SDLK_F5;

  add_to_gui_list(ID_ECONOMY, pwidget);

  tax_button = pwidget;

  /* research button */
  pwidget = create_icon2(adj_surf(GET_SURF(client_research_sprite())),
                         units_info_window->dst,
                         WF_FREE_GFX | WF_WIDGET_HAS_INFO_LABEL
                         | WF_RESTORE_BACKGROUND | WF_FREE_THEME);
  /* TRANS: Research report action */
  fc_snprintf(buf, sizeof(buf), "%s (%s)", _("Research"), "F6");
  pwidget->info_label = create_utf8_from_char_fonto(buf, FONTO_ATTENTION);
  pwidget->action = research_callback;
  pwidget->key = SDLK_F6;

  add_to_gui_list(ID_RESEARCH, pwidget);

  research_button = pwidget;

  /* Revolution button */
  pwidget = create_icon2(adj_surf(GET_SURF(client_government_sprite())),
                         units_info_window->dst,
                         WF_FREE_GFX | WF_WIDGET_HAS_INFO_LABEL
                         | WF_RESTORE_BACKGROUND | WF_FREE_THEME);
  fc_snprintf(buf, sizeof(buf), "%s (%s)", _("Revolution"), "Ctrl+Shift+G");
  pwidget->info_label = create_utf8_from_char_fonto(buf, FONTO_ATTENTION);
  pwidget->action = revolution_callback;
  pwidget->key = SDLK_G;
  pwidget->mod = SDL_KMOD_CTRL | SDL_KMOD_SHIFT;

  add_to_gui_list(ID_REVOLUTION, pwidget);

  revolution_button = pwidget;

  /* Show/hide unit's window button */

  /* Make UNITS Icon */
  icon_theme = create_surf(current_theme->map_icon->w,
                           current_theme->map_icon->h);
  alphablit(current_theme->map_icon, NULL, icon_theme, NULL, 255);
  alphablit(current_theme->r_arrow_icon, NULL, icon_theme, NULL, 255);

  pwidget = create_themeicon(icon_theme, units_info_window->dst,
                             WF_FREE_GFX | WF_FREE_THEME
                             | WF_RESTORE_BACKGROUND
                             | WF_WIDGET_HAS_INFO_LABEL);
  pwidget->info_label
    = create_utf8_from_char_fonto(_("Hide Unit Info Window"),
                                  FONTO_ATTENTION);

  pwidget->action = toggle_unit_info_window_callback;

  add_to_gui_list(ID_TOGGLE_UNITS_WINDOW_BUTTON, pwidget);

  unit_info_dlg->begin_widget_list = pwidget;

  sdl3_client_flags |= CF_UNITINFO_SHOWN;

  set_new_unitinfo_window_pos();

  widget_redraw(units_info_window);
}

/**********************************************************************//**
  Make unitinfo buttons visible.
**************************************************************************/
void show_unitinfo_window_buttons(void)
{
  struct widget *pwidget = get_unit_info_window_widget();

  /* economy button */
  pwidget = pwidget->prev;
  clear_wflag(pwidget, WF_HIDDEN);

  /* research button */
  pwidget = pwidget->prev;
  clear_wflag(pwidget, WF_HIDDEN);

  /* revolution button */
  pwidget = pwidget->prev;
  clear_wflag(pwidget, WF_HIDDEN);

  /* show/hide unit's window button */
  pwidget = pwidget->prev;
  clear_wflag(pwidget, WF_HIDDEN);
}

/**********************************************************************//**
  Make unitinfo buttons hidden.
**************************************************************************/
void hide_unitinfo_window_buttons(void)
{
  struct widget *pwidget = get_unit_info_window_widget();

  /* economy button */
  pwidget = pwidget->prev;
  set_wflag(pwidget, WF_HIDDEN);

  /* research button */
  pwidget = pwidget->prev;
  set_wflag(pwidget, WF_HIDDEN);

  /* revolution button */
  pwidget = pwidget->prev;
  set_wflag(pwidget, WF_HIDDEN);

  /* show/hide unit's window button */
  pwidget = pwidget->prev;
  set_wflag(pwidget, WF_HIDDEN);
}

/**********************************************************************//**
  Make unitinfo buttons disabled.
**************************************************************************/
void disable_unitinfo_window_buttons(void)
{
  struct widget *pwidget = get_unit_info_window_widget();

  /* economy button */
  pwidget = pwidget->prev;
  set_wstate(pwidget, FC_WS_DISABLED);

  /* research button */
  pwidget = pwidget->prev;
  set_wstate(pwidget, FC_WS_DISABLED);

  /* revolution button */
  pwidget = pwidget->prev;
  set_wstate(pwidget, FC_WS_DISABLED);
}

/**********************************************************************//**
  Close unitinfo window.
**************************************************************************/
void popdown_unitinfo_window(void)
{
  if (unit_info_dlg) {
    popdown_window_group_dialog(unit_info_dlg->begin_widget_list,
                                unit_info_dlg->end_widget_list);
    FC_FREE(unit_info_dlg);
    sdl3_client_flags &= ~CF_UNITINFO_SHOWN;
  }
}

/**********************************************************************//**
  Open minimap area.
**************************************************************************/
void popup_minimap_window(void)
{
  struct widget *pwidget, *pwindow;
  SDL_Surface *icon_theme = NULL;
  SDL_Color black = {0, 0, 0, 255};
  char buf[256];

  if (minimap_dlg) {
    return;
  }

  minimap_dlg = fc_calloc(1, sizeof(struct advanced_dialog));

  /* minimap_window */
  pwindow = create_window_skeleton(NULL, NULL, 0);

  resize_window(pwindow, NULL, &black,
                (pwindow->size.w - pwindow->area.w) + overview_w + BLOCKM_W,
                (pwindow->size.h - pwindow->area.h) + overview_h);

  draw_frame(pwindow->theme, 0, 0, pwindow->size.w, pwindow->size.h);

  icon_theme = resize_surface(current_theme->block, BLOCKM_W,
                              pwindow->area.h, 1);
  blit_entire_src(icon_theme, pwindow->theme,
                  pwindow->area.x + pwindow->area.w - icon_theme->w,
                  pwindow->area.y);
  FREESURFACE(icon_theme);

  pwindow->action = minimap_window_callback;

  add_to_gui_list(ID_MINI_MAP_WINDOW, pwindow);

  minimap_window = pwindow;
  minimap_dlg->end_widget_list = minimap_window;

  /* New turn button */
  pwidget = create_themeicon(current_theme->new_turn_icon, minimap_window->dst,
                             WF_WIDGET_HAS_INFO_LABEL
                             | WF_RESTORE_BACKGROUND);
  fc_snprintf(buf, sizeof(buf), "%s (%s)", _("Turn Done"), _("Shift+Return"));
  pwidget->info_label = create_utf8_from_char_fonto(buf, FONTO_ATTENTION);
  pwidget->action = end_turn_callback;
  pwidget->key = SDLK_RETURN;
  pwidget->mod = SDL_KMOD_SHIFT;

  add_to_gui_list(ID_NEW_TURN, pwidget);

  new_turn_button = pwidget;

  /* players button */
  pwidget = create_themeicon(current_theme->players_icon, minimap_window->dst,
                             WF_WIDGET_HAS_INFO_LABEL
                             | WF_RESTORE_BACKGROUND);
  /* TRANS: Nations report action */
  fc_snprintf(buf, sizeof(buf), "%s (%s)", _("Nations"), "F3");
  pwidget->info_label = create_utf8_from_char_fonto(buf, FONTO_ATTENTION);
  pwidget->action = players_action_callback;
  pwidget->key = SDLK_F3;

  add_to_gui_list(ID_PLAYERS, pwidget);

  /* Find city button */
  pwidget = create_themeicon(current_theme->find_city_icon, minimap_window->dst,
                             WF_WIDGET_HAS_INFO_LABEL
                             | WF_RESTORE_BACKGROUND);
  fc_snprintf(buf, sizeof(buf), "%s (%s)\n%s\n%s (%s)", _("Cities Report"),
                                "F4", _("or"), _("Find City"), "Ctrl+F");
  pwidget->info_label = create_utf8_from_char_fonto(buf, FONTO_ATTENTION);
  pwidget->info_label->style |= SF_CENTER;
  pwidget->action = cities_action_callback;
  pwidget->key = SDLK_F;
  pwidget->mod = SDL_KMOD_CTRL;

  add_to_gui_list(ID_CITIES, pwidget);

  find_city_button = pwidget;

  /* Units button */
  pwidget = create_themeicon(current_theme->units2_icon, minimap_window->dst,
                             WF_WIDGET_HAS_INFO_LABEL
                             | WF_RESTORE_BACKGROUND);
  fc_snprintf(buf, sizeof(buf), "%s (%s)", _("Units"), "F2");
  pwidget->info_label = create_utf8_from_char_fonto(buf, FONTO_ATTENTION);
  pwidget->action = units_action_callback;
  pwidget->key = SDLK_F2;

  add_to_gui_list(ID_UNITS, pwidget);

  /* Show/hide log window button */
  pwidget = create_themeicon(current_theme->log_icon, minimap_window->dst,
                             WF_WIDGET_HAS_INFO_LABEL
                             | WF_RESTORE_BACKGROUND);
  fc_snprintf(buf, sizeof(buf), "%s (%s)", _("Hide Messages"), "F9");
  pwidget->info_label = create_utf8_from_char_fonto(buf, FONTO_ATTENTION);
  pwidget->action = toggle_msg_window_callback;
  pwidget->key = SDLK_F9;

  add_to_gui_list(ID_CHATLINE_TOGGLE_LOG_WINDOW_BUTTON, pwidget);

#ifdef GUI_SDL3_SMALL_SCREEN
  /* Options button */
  options_button = create_themeicon(current_theme->options_icon,
                                    minimap_window->dst,
                                    WF_WIDGET_HAS_INFO_LABEL
                                    | WF_RESTORE_BACKGROUND);
  fc_snprintf(buf, sizeof(buf), "%s (%s)", _("Options"), "Esc");
  options_button->info_label
    = create_utf8_from_char_fonto(buf, FONTO_ATTENTION);

  options_button->action = optiondlg_callback;
  options_button->key = SDLK_ESCAPE;

  add_to_gui_list(ID_CLIENT_OPTIONS, options_button);
#endif /* GUI_SDL3_SMALL_SCREEN */

  /* Show/hide minimap button */

  /* Make Map Icon */
  icon_theme = create_surf(current_theme->map_icon->w,
                           current_theme->map_icon->h);
  alphablit(current_theme->map_icon, NULL, icon_theme, NULL, 255);
  alphablit(current_theme->l_arrow_icon, NULL, icon_theme, NULL, 255);

  pwidget = create_themeicon(icon_theme, minimap_window->dst,
                             WF_FREE_GFX | WF_FREE_THEME |
                             WF_WIDGET_HAS_INFO_LABEL
                             | WF_RESTORE_BACKGROUND);

  pwidget->info_label = create_utf8_from_char_fonto(_("Hide Mini Map"),
                                                    FONTO_ATTENTION);
  pwidget->action = toggle_map_window_callback;

  add_to_gui_list(ID_TOGGLE_MAP_WINDOW_BUTTON, pwidget);

  minimap_dlg->begin_widget_list = pwidget;

  sdl3_client_flags |= CF_OVERVIEW_SHOWN;

  set_new_minimap_window_pos();

  widget_redraw(minimap_window);
}

/**********************************************************************//**
  Make minimap window buttons visible.
**************************************************************************/
void show_minimap_window_buttons(void)
{
  struct widget *pwidget = get_minimap_window_widget();

  /* new turn button */
  pwidget = pwidget->prev;
  clear_wflag(pwidget, WF_HIDDEN);

  /* players button */
  pwidget = pwidget->prev;
  clear_wflag(pwidget, WF_HIDDEN);

  /* find city button */
  pwidget = pwidget->prev;
  clear_wflag(pwidget, WF_HIDDEN);

  /* units button */
  pwidget = pwidget->prev;
  clear_wflag(pwidget, WF_HIDDEN);

  /* Show/hide log window button */
  pwidget = pwidget->prev;
  clear_wflag(pwidget, WF_HIDDEN);

#ifdef GUI_SDL3_SMALL_SCREEN
  /* Options button */
  pwidget = pwidget->prev;
  clear_wflag(pwidget, WF_HIDDEN);
#endif /* GUI_SDL3_SMALL_SCREEN */

  /* show/hide minimap button */
  pwidget = pwidget->prev;
  clear_wflag(pwidget, WF_HIDDEN);
}

/**********************************************************************//**
  Make minimap window buttons hidden.
**************************************************************************/
void hide_minimap_window_buttons(void)
{
  struct widget *pwidget = get_minimap_window_widget();

  /* new turn button */
  pwidget = pwidget->prev;
  set_wflag(pwidget, WF_HIDDEN);

  /* players button */
  pwidget = pwidget->prev;
  set_wflag(pwidget, WF_HIDDEN);

  /* find city button */
  pwidget = pwidget->prev;
  set_wflag(pwidget, WF_HIDDEN);

  /* units button */
  pwidget = pwidget->prev;
  set_wflag(pwidget, WF_HIDDEN);

  /* Show/hide log window button */
  pwidget = pwidget->prev;
  set_wflag(pwidget, WF_HIDDEN);

#ifdef GUI_SDL3_SMALL_SCREEN
  /* Options button */
  pwidget = pwidget->prev;
  set_wflag(pwidget, WF_HIDDEN);
#endif /* GUI_SDL3_SMALL_SCREEN */

  /* show/hide minimap button */
  pwidget = pwidget->prev;
  set_wflag(pwidget, WF_HIDDEN);
}

/**********************************************************************//**
  Redraw minimap window buttons.
**************************************************************************/
void redraw_minimap_window_buttons(void)
{
  struct widget *pwidget = get_minimap_window_widget();

  /* new turn button */
  pwidget = pwidget->prev;
  widget_redraw(pwidget);

  /* players button */
  pwidget = pwidget->prev;
  widget_redraw(pwidget);

  /* find city button */
  pwidget = pwidget->prev;
  widget_redraw(pwidget);

  /* units button */
  pwidget = pwidget->prev;
  widget_redraw(pwidget);
  /* Show/hide log window button */
  pwidget = pwidget->prev;
  widget_redraw(pwidget);

#ifdef GUI_SDL3_SMALL_SCREEN
  /* Options button */
  pwidget = pwidget->prev;
  widget_redraw(pwidget);
#endif /* GUI_SDL3_SMALL_SCREEN */

  /* show/hide minimap button */
  pwidget = pwidget->prev;
  widget_redraw(pwidget);
}

/**********************************************************************//**
  Make minimap window buttons disabled.
**************************************************************************/
void disable_minimap_window_buttons(void)
{
  struct widget *pwidget = get_minimap_window_widget();

  /* new turn button */
  pwidget = pwidget->prev;
  set_wstate(pwidget, FC_WS_DISABLED);

  /* players button */
  pwidget = pwidget->prev;
  set_wstate(pwidget, FC_WS_DISABLED);

  /* find city button */
  pwidget = pwidget->prev;
  set_wstate(pwidget, FC_WS_DISABLED);

  /* units button */
  pwidget = pwidget->prev;
  set_wstate(pwidget, FC_WS_DISABLED);

  /* Show/hide log window button */
  pwidget = pwidget->prev;
  set_wstate(pwidget, FC_WS_DISABLED);

#ifdef GUI_SDL3_SMALL_SCREEN
  /* Options button */
  pwidget = pwidget->prev;
  set_wstate(pwidget, FC_WS_DISABLED);
#endif /* GUI_SDL3_SMALL_SCREEN */
}

/**********************************************************************//**
  Close minimap window.
**************************************************************************/
void popdown_minimap_window(void)
{
  if (minimap_dlg) {
    popdown_window_group_dialog(minimap_dlg->begin_widget_list,
                                minimap_dlg->end_widget_list);
    FC_FREE(minimap_dlg);
    sdl3_client_flags &= ~CF_OVERVIEW_SHOWN;
  }
}

/**********************************************************************//**
  Create and show game page.
**************************************************************************/
void show_game_page(void)
{
  struct widget *pwidget;
  SDL_Surface *icon_theme = NULL;

  if (sdl3_client_flags & CF_MAP_UNIT_W_CREATED) {
    return;
  }

  popup_minimap_window();
  popup_unitinfo_window();
  sdl3_client_flags |= CF_MAP_UNIT_W_CREATED;

#ifndef GUI_SDL3_SMALL_SCREEN
  init_options_button();
#endif

  /* Cooling icon */
  icon_theme = adj_surf(GET_SURF(client_cooling_sprite()));
  fc_assert(icon_theme != NULL);
  pwidget = create_iconlabel(icon_theme, main_data.gui, NULL, WF_FREE_THEME);

#ifdef GUI_SDL3_SMALL_SCREEN
  widget_set_position(pwidget,
                      pwidget->dst->surface->w - pwidget->size.w - adj_size(10),
                      0);
#else  /* GUI_SDL3_SMALL_SCREEN */
  widget_set_position(pwidget,
                      pwidget->dst->surface->w - pwidget->size.w - adj_size(10),
                      adj_size(10));
#endif /* GUI_SDL3_SMALL_SCREEN */

  add_to_gui_list(ID_COOLING_ICON, pwidget);

  /* Warming icon */
  icon_theme = adj_surf(GET_SURF(client_warming_sprite()));
  fc_assert(icon_theme != NULL);

  pwidget = create_iconlabel(icon_theme, main_data.gui, NULL, WF_FREE_THEME);

#ifdef GUI_SDL3_SMALL_SCREEN
  widget_set_position(pwidget,
                      pwidget->dst->surface->w - pwidget->size.w * 2 - adj_size(10),
                      0);
#else  /* GUI_SDL3_SMALL_SCREEN */
  widget_set_position(pwidget,
                      pwidget->dst->surface->w - pwidget->size.w * 2 - adj_size(10),
                      adj_size(10));
#endif /* GUI_SDL3_SMALL_SCREEN */

  add_to_gui_list(ID_WARMING_ICON, pwidget);

  /* create order buttons */
  create_units_order_widgets();

  /* enable options button and order widgets */
  enable_options_button();
  enable_order_buttons();
}

/**********************************************************************//**
  Close game page.
**************************************************************************/
void close_game_page(void)
{
  struct widget *pwidget;

  del_widget_from_gui_list(options_button);

  pwidget = get_widget_pointer_from_main_list(ID_COOLING_ICON);
  del_widget_from_gui_list(pwidget);

  pwidget = get_widget_pointer_from_main_list(ID_WARMING_ICON);
  del_widget_from_gui_list(pwidget);

  delete_units_order_widgets();

  popdown_minimap_window();
  popdown_unitinfo_window();
  sdl3_client_flags &= ~CF_MAP_UNIT_W_CREATED;
}

/**********************************************************************//**
  Make minimap window buttons disabled and redraw.
  TODO: Use disable_minimap_window_buttons() for disabling buttons.
**************************************************************************/
static void disable_minimap_widgets(void)
{
  struct widget *buf, *end;

  buf = get_minimap_window_widget();
  set_wstate(buf, FC_WS_DISABLED);

  /* new turn button */
  buf = buf->prev;
  end = buf;
  set_wstate(buf, FC_WS_DISABLED);

  /* players button */
  buf = buf->prev;
  set_wstate(buf, FC_WS_DISABLED);

  /* find city button */
  buf = buf->prev;
  set_wstate(buf, FC_WS_DISABLED);

  /* units button */
  buf = buf->prev;
  set_wstate(buf, FC_WS_DISABLED);

  /* Show/hide log window button */
  buf = buf->prev;
  set_wstate(buf, FC_WS_DISABLED);

#ifdef GUI_SDL3_SMALL_SCREEN
  /* Options button */
  buf = buf->prev;
  set_wstate(buf, FC_WS_DISABLED);
#endif /* GUI_SDL3_SMALL_SCREEN */

  /* show/hide minimap button */
  buf = buf->prev;
  set_wstate(buf, FC_WS_DISABLED);

  redraw_group(buf, end, TRUE);
}

/**********************************************************************//**
  Make unitinfo window buttons disabled and redraw.
  TODO: Use disable_unitinfo_window_buttons() for disabling buttons.
**************************************************************************/
static void disable_unitinfo_widgets(void)
{
  struct widget *buf = units_info_window->private_data.adv_dlg->begin_widget_list;
  struct widget *end = units_info_window->private_data.adv_dlg->end_widget_list;

  set_group_state(buf, end, FC_WS_DISABLED);
  end = end->prev;
  redraw_group(buf, end, TRUE);
}

/**********************************************************************//**
  Disable game page widgets.
**************************************************************************/
void disable_main_widgets(void)
{
  if (C_S_RUNNING == client_state()) {
    disable_minimap_widgets();
    disable_unitinfo_widgets();

    disable_options_button();

    disable_order_buttons();
  }
}

/**********************************************************************//**
  Make minimap window buttons enabled and redraw.
**************************************************************************/
static void enable_minimap_widgets(void)
{
  struct widget *buf, *end;

  if (can_client_issue_orders()) {
    buf = minimap_window;
    set_wstate(buf, FC_WS_NORMAL);

    /* new turn button */
    buf = buf->prev;
    end = buf;
    set_wstate(buf, FC_WS_NORMAL);

    /* players button */
    buf = buf->prev;
    set_wstate(buf, FC_WS_NORMAL);

    /* find city button */
    buf = buf->prev;
    set_wstate(buf, FC_WS_NORMAL);

    /* units button */
    buf = buf->prev;
    set_wstate(buf, FC_WS_NORMAL);

    /* Show/hide log window button */
    buf = buf->prev;
    set_wstate(buf, FC_WS_NORMAL);

#ifdef GUI_SDL3_SMALL_SCREEN
    /* Options button */
    buf = buf->prev;
    set_wstate(buf, FC_WS_NORMAL);
#endif /* GUI_SDL3_SMALL_SCREEN */

    /* show/hide minimap button */
    buf = buf->prev;
    set_wstate(buf, FC_WS_NORMAL);

    redraw_group(buf, end, TRUE);
  }
}

/**********************************************************************//**
  Make unitinfo window buttons enabled and redraw.
**************************************************************************/
static void enable_unitinfo_widgets(void)
{
  struct widget *buf, *end;

  if (can_client_issue_orders()) {
    buf = units_info_window->private_data.adv_dlg->begin_widget_list;
    end = units_info_window->private_data.adv_dlg->end_widget_list;

    set_group_state(buf, end, FC_WS_NORMAL);
    end = end->prev;
    redraw_group(buf, end, TRUE);
  }
}

/**********************************************************************//**
  Enable game page widgets.
**************************************************************************/
void enable_main_widgets(void)
{
  if (C_S_RUNNING == client_state()) {
    enable_minimap_widgets();
    enable_unitinfo_widgets();

    enable_options_button();

    enable_order_buttons();
  }
}

/**********************************************************************//**
  Get main unitinfo widget.
**************************************************************************/
struct widget *get_unit_info_window_widget(void)
{
  return units_info_window;
}

/**********************************************************************//**
  Get main minimap widget.
**************************************************************************/
struct widget *get_minimap_window_widget(void)
{
  return minimap_window;
}

/**********************************************************************//**
  Get main tax rates widget.
**************************************************************************/
struct widget *get_tax_rates_widget(void)
{
  return tax_button;
}

/**********************************************************************//**
  Get main research widget.
**************************************************************************/
struct widget *get_research_widget(void)
{
  return research_button;
}

/**********************************************************************//**
  Get main revolution widget.
**************************************************************************/
struct widget *get_revolution_widget(void)
{
  return revolution_button;
}

/**********************************************************************//**
  Make Find City button available.
**************************************************************************/
void enable_and_redraw_find_city_button(void)
{
  set_wstate(find_city_button, FC_WS_NORMAL);
  widget_redraw(find_city_button);
  widget_mark_dirty(find_city_button);
}

/**********************************************************************//**
  Make Revolution button available.
**************************************************************************/
void enable_and_redraw_revolution_button(void)
{
  set_wstate(revolution_button, FC_WS_NORMAL);
  widget_redraw(revolution_button);
  widget_mark_dirty(revolution_button);
}
/**********************************************************************//**
  Finger down handler
**************************************************************************/
void finger_down_on_map(struct finger_behavior *finger_behavior)
{
  if (C_S_RUNNING != client_state()) {
    return;
  }

  key_unit_goto();
  update_mouse_cursor(CURSOR_GOTO);
}

/**********************************************************************//**
  Finger up handler
**************************************************************************/
void finger_up_on_map(struct finger_behavior *finger_behavior)
{
  if (C_S_RUNNING != client_state()) {
    return;
  }
  update_mouse_cursor(CURSOR_DEFAULT);
  action_button_pressed(finger_behavior->event.x,
                        finger_behavior->event.y, SELECT_POPUP);
}

/**********************************************************************//**
  Mouse click handler
**************************************************************************/
void button_down_on_map(struct mouse_button_behavior *button_behavior)
{
  struct tile *ptile;

  if (C_S_RUNNING != client_state()) {
    return;
  }

  if (button_behavior->event->button == SDL_BUTTON_LEFT) {
    switch (button_behavior->hold_state) {
    case MB_HOLD_SHORT:
      break;
    case MB_HOLD_MEDIUM:
      /* switch to goto mode */
      key_unit_goto();
      update_mouse_cursor(CURSOR_GOTO);
      break;
    case MB_HOLD_LONG:
#ifdef UNDER_CE
      /* cancel goto mode and open context menu on Pocket PC since we have
       * only one 'mouse button' */
      key_cancel_action();
      draw_goto_patrol_lines = FALSE;
      update_mouse_cursor(CURSOR_DEFAULT);
      /* popup context menu */
      if ((ptile = canvas_pos_to_tile((int) button_behavior->event->x,
                                      (int) button_behavior->event->y,
                                      mouse_zoom))) {
        popup_advanced_terrain_dialog(ptile, button_behavior->event->x,
                                      button_behavior->event->y);
      }
#endif /* UNDER_CE */
      break;
    default:
      break;
    }
  } else if (button_behavior->event->button == SDL_BUTTON_MIDDLE) {
    switch (button_behavior->hold_state) {
    case MB_HOLD_SHORT:
      break;
    case MB_HOLD_MEDIUM:
      break;
    case MB_HOLD_LONG:
      break;
    default:
      break;
    }
  } else if (button_behavior->event->button == SDL_BUTTON_RIGHT) {
    switch (button_behavior->hold_state) {
    case MB_HOLD_SHORT:
      break;
    case MB_HOLD_MEDIUM:
      /* popup context menu */
      if ((ptile = canvas_pos_to_tile((int) button_behavior->event->x,
                                      (int) button_behavior->event->y,
                                      mouse_zoom))) {
        popup_advanced_terrain_dialog(ptile, button_behavior->event->x,
                                      button_behavior->event->y);
      }
      break;
    case MB_HOLD_LONG:
      break;
    default:
      break;
    }
  }
}

/**********************************************************************//**
  Use released mouse button over map.
**************************************************************************/
void button_up_on_map(struct mouse_button_behavior *button_behavior)
{
  struct tile *ptile;
  struct city *pcity;

  if (C_S_RUNNING != client_state()) {
    return;
  }

  draw_goto_patrol_lines = FALSE;

  if (button_behavior->event->button == SDL_BUTTON_LEFT) {
    switch (button_behavior->hold_state) {
    case MB_HOLD_SHORT:
      if (LSHIFT || LALT || LCTRL) {
        if ((ptile = canvas_pos_to_tile((int) button_behavior->event->x,
                                        (int) button_behavior->event->y,
                                        mouse_zoom))) {
          if (LSHIFT) {
            popup_advanced_terrain_dialog(ptile, button_behavior->event->x,
                                          button_behavior->event->y);
          } else {
            if (((pcity = tile_city(ptile)) != NULL)
                && (city_owner(pcity) == client.conn.playing)) {
              if (LCTRL) {
                popup_worklist_editor(pcity, NULL);
              } else {
                /* LALT - this work only with fullscreen mode */
                popup_hurry_production_dialog(pcity, NULL);
              }
            }
          }
        }
      } else {
        update_mouse_cursor(CURSOR_DEFAULT);
        action_button_pressed(button_behavior->event->x,
                              button_behavior->event->y, SELECT_POPUP);
      }
      break;
    case MB_HOLD_MEDIUM:
      /* finish goto */
      update_mouse_cursor(CURSOR_DEFAULT);
      action_button_pressed(button_behavior->event->x,
                            button_behavior->event->y, SELECT_POPUP);
      break;
    case MB_HOLD_LONG:
#ifndef UNDER_CE
      /* finish goto */
      update_mouse_cursor(CURSOR_DEFAULT);
      action_button_pressed(button_behavior->event->x,
                            button_behavior->event->y, SELECT_POPUP);
#endif /* UNDER_CE */
      break;
    default:
      break;
    }
  } else if (button_behavior->event->button == SDL_BUTTON_MIDDLE) {
    switch (button_behavior->hold_state) {
    case MB_HOLD_SHORT:
/*        break;*/
    case MB_HOLD_MEDIUM:
/*        break;*/
    case MB_HOLD_LONG:
/*        break;*/
    default:
      /* Popup context menu */
      if ((ptile = canvas_pos_to_tile((int) button_behavior->event->x,
                                      (int) button_behavior->event->y,
                                      mouse_zoom))) {
        popup_advanced_terrain_dialog(ptile, button_behavior->event->x,
                                      button_behavior->event->y);
      }
      break;
    }
  } else if (button_behavior->event->button == SDL_BUTTON_RIGHT) {
    switch (button_behavior->hold_state) {
    case MB_HOLD_SHORT:
      /* recenter map */
      recenter_button_pressed(button_behavior->event->x, button_behavior->event->y);
      flush_dirty();
      break;
    case MB_HOLD_MEDIUM:
      break;
    case MB_HOLD_LONG:
      break;
    default:
      break;
    }
  }
}

/**********************************************************************//**
  Toggle map drawing stuff.
**************************************************************************/
bool map_event_handler(SDL_KeyboardEvent *key)
{
  if (C_S_RUNNING == client_state()) {
    enum direction8 movedir = DIR8_MAGIC_MAX;

    switch (key->scancode) {

    case SDL_SCANCODE_KP_8:
      movedir = DIR8_NORTH;
      break;

    case SDL_SCANCODE_KP_9:
      movedir = DIR8_NORTHEAST;
      break;

    case SDL_SCANCODE_KP_6:
      movedir = DIR8_EAST;
      break;

    case SDL_SCANCODE_KP_3:
      movedir = DIR8_SOUTHEAST;
      break;

    case SDL_SCANCODE_KP_2:
      movedir = DIR8_SOUTH;
      break;

    case SDL_SCANCODE_KP_1:
      movedir = DIR8_SOUTHWEST;
      break;

    case SDL_SCANCODE_KP_4:
      movedir = DIR8_WEST;
      break;

    case SDL_SCANCODE_KP_7:
      movedir = DIR8_NORTHWEST;
      break;

    case SDL_SCANCODE_KP_5:
      key_recall_previous_focus_unit();
      return FALSE;

    default:
      switch (key->key) {

      /* Cancel action */
      case SDLK_ESCAPE:
        key_cancel_action();
        draw_goto_patrol_lines = FALSE;
        update_mouse_cursor(CURSOR_DEFAULT);
        return FALSE;

      case SDLK_UP:
        movedir = DIR8_NORTH;
        break;

      case SDLK_PAGEUP:
        movedir = DIR8_NORTHEAST;
        break;

      case SDLK_RIGHT:
        movedir = DIR8_EAST;
        break;

      case SDLK_PAGEDOWN:
        movedir = DIR8_SOUTHEAST;
        break;

      case SDLK_DOWN:
        movedir = DIR8_SOUTH;
        break;

      case SDLK_END:
        movedir = DIR8_SOUTHWEST;
        break;

      case SDLK_LEFT:
        movedir = DIR8_WEST;
        break;

      case SDLK_HOME:
        movedir = DIR8_NORTHWEST;
        break;

        /* *** Map view settings *** */

        /* Show city outlines - Ctrl+y */
      case SDLK_Y:
        if (LCTRL || RCTRL) {
          key_city_outlines_toggle();
        }
        return FALSE;

        /* Show map grid - Ctrl+g */
      case SDLK_G:
        if (LCTRL || RCTRL) {
          key_map_grid_toggle();
        }
        return FALSE;

        /* Show national borders - Ctrl+b */
      case SDLK_B:
        if (LCTRL || RCTRL) {
          key_map_borders_toggle();
        }
        return FALSE;

      case SDLK_N:
        /* Show native tiles - Ctrl+Shift+n */
        if ((LCTRL || RCTRL) && (LSHIFT || RSHIFT)) {
          key_map_native_toggle();
        } else if (LCTRL || RCTRL) {
          /* Show city names - Ctrl+n */
          key_city_names_toggle();
        }
        return FALSE;

        /* Show city growth Ctrl+o */
        /* Show pollution - Ctrl+Shift+o */
      case SDLK_O:
        if (LCTRL || RCTRL) {
          if (LSHIFT || RSHIFT) {
            key_pollution_toggle();
          } else {
            key_city_growth_toggle();
          }
        }
        return FALSE;

        /* Show bases - Ctrl+Shift+f */
      case SDLK_F:
        if ((LCTRL || RCTRL) && (LSHIFT || RSHIFT)) {
          request_toggle_bases();
        }
        return FALSE;

        /* Show city productions - Ctrl+p */
      case SDLK_P:
        if (LCTRL || RCTRL) {
          key_city_productions_toggle();
        }
        return FALSE;

        /* Show city trade routes - Ctrl+d */
      case SDLK_D:
        if (LCTRL || RCTRL) {
          key_city_trade_routes_toggle();
        }
        return FALSE;

        /* *** Some additional shortcuts that work in the SDL client only *** */

        /* Show terrain - Ctrl+Shift+t */
      case SDLK_T:
        if ((LCTRL || RCTRL) && (LSHIFT || RSHIFT)) {
          key_terrain_toggle();
        }
        return FALSE;

        /* (Show coastline) */

      /* (Show roads and rails) */

      /* Show irrigation - Ctrl+i */
      case SDLK_I:
        if (LCTRL || RCTRL) {
          key_irrigation_toggle();
        }
        return FALSE;

        /* Show mines - Ctrl+m */
      case SDLK_M:
        if (LCTRL || RCTRL) {
          key_mines_toggle();
        }
        return FALSE;

        /* Show resources - Ctrl+s */
      case SDLK_S:
        if (LCTRL || RCTRL) {
          key_resources_toggle();
        }
        return FALSE;

        /* Show huts - Ctrl+h */
      case SDLK_H:
        if (LCTRL || RCTRL) {
          key_huts_toggle();
        }
        return FALSE;

        /* Show cities - Ctrl+c */
      case SDLK_C:
        if (LCTRL || RCTRL) {
          request_toggle_cities();
        } else {
          request_center_focus_unit();
        }
        return FALSE;

        /* Show units - Ctrl+u */
      case SDLK_U:
        if (LCTRL || RCTRL) {
          key_units_toggle();
        }
        return FALSE;

        /* (Show focus unit) */

        /* Show city output - Ctrl+v */
      case SDLK_V:
        key_city_output_toggle();
        return FALSE;

        /* Show fog of war - Ctrl+w */
      case SDLK_W:
        if (LCTRL || RCTRL) {
          key_fog_of_war_toggle();
        }
        return FALSE;

      default:
        break;
      }
    }

    if (movedir != DIR8_MAGIC_MAX) {
      if (!is_unit_move_blocked) {
        key_unit_move(movedir);
      }
      return FALSE;
    }
  }

  return TRUE;
}

/**********************************************************************//**
  User interacted with the edit button of the new city dialog.
**************************************************************************/
static int newcity_name_edit_callback(struct widget *pedit)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (new_city_dlg->begin_widget_list->string_utf8->text == NULL) {
      /* empty input -> restore previous content */
      copy_chars_to_utf8_str(pedit->string_utf8, suggested_city_name);
      widget_redraw(pedit);
      widget_mark_dirty(pedit);
      flush_dirty();
    }
  }

  return -1;
}

/**********************************************************************//**
  User interacted with the Ok button of the new city dialog.
**************************************************************************/
static int newcity_ok_callback(struct widget *ok_button)
{
  if (PRESSED_EVENT(main_data.event)) {

    finish_city(ok_button->data.tile,
                new_city_dlg->begin_widget_list->string_utf8->text);

    popdown_window_group_dialog(new_city_dlg->begin_widget_list,
                                new_city_dlg->end_widget_list);
    FC_FREE(new_city_dlg);

    FC_FREE(suggested_city_name);

    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  User interacted with the Cancel button of the new city dialog.
**************************************************************************/
static int newcity_cancel_callback(struct widget *cancel_button)
{
  if (PRESSED_EVENT(main_data.event)) {
    popdown_window_group_dialog(new_city_dlg->begin_widget_list,
                                new_city_dlg->end_widget_list);

    cancel_city(cancel_button->data.tile);

    FC_FREE(new_city_dlg);

    FC_FREE(suggested_city_name);

    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  User interacted with the new city dialog.
**************************************************************************/
static int move_new_city_dlg_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    move_window_group(new_city_dlg->begin_widget_list, pwindow);
  }

  return -1;
}

/* ============================== Native =============================== */


/**********************************************************************//**
  Popup a dialog to ask for the name of a new city.  The given string
  should be used as a suggestion.
**************************************************************************/
void popup_newcity_dialog(struct unit *punit, const char *suggest_name)
{
  SDL_Surface *background;
  utf8_str *pstr = NULL;
  struct widget *label = NULL;
  struct widget *pwindow = NULL;
  struct widget *cancel_button = NULL;
  struct widget *ok_button;
  struct widget *pedit;
  SDL_Rect area;
  int suggestlen;

  if (new_city_dlg) {
    return;
  }

  suggestlen = strlen(suggest_name) + 1;
  suggested_city_name = fc_calloc(1, suggestlen);
  fc_strlcpy(suggested_city_name, suggest_name, suggestlen);

  new_city_dlg = fc_calloc(1, sizeof(struct small_dialog));

  /* Create window */
  pstr = create_utf8_from_char_fonto(action_id_name_translation(ACTION_FOUND_CITY),
                                     FONTO_ATTENTION);
  pstr->style |= TTF_STYLE_BOLD;
  pwindow = create_window_skeleton(NULL, pstr, 0);
  pwindow->action = move_new_city_dlg_callback;

  area = pwindow->area;

  /* Create ok button */
  ok_button =
    create_themeicon_button_from_chars_fonto(current_theme->small_ok_icon,
                                             pwindow->dst,
                                             _("OK"), FONTO_DEFAULT, 0);
  ok_button->action = newcity_ok_callback;
  ok_button->key = SDLK_RETURN;
  ok_button->data.tile = unit_tile(punit);

  area.h += ok_button->size.h;

  /* Create cancel button */
  cancel_button =
      create_themeicon_button_from_chars_fonto(current_theme->small_cancel_icon,
                                               pwindow->dst, _("Cancel"),
                                               FONTO_DEFAULT, 0);
  cancel_button->action = newcity_cancel_callback;
  cancel_button->key = SDLK_ESCAPE;
  cancel_button->data.tile = unit_tile(punit);

  /* Correct sizes */
  cancel_button->size.w += adj_size(5);
  ok_button->size.w = cancel_button->size.w;

  /* Create text label */
  pstr = create_utf8_from_char_fonto(_("What should we call our new city?"),
                                     FONTO_DEFAULT);
  pstr->style |= (TTF_STYLE_BOLD|SF_CENTER);
  pstr->fgcol = *get_theme_color(COLOR_THEME_NEWCITYDLG_TEXT);
  label = create_iconlabel(NULL, pwindow->dst, pstr,
                           WF_DRAW_TEXT_LABEL_WITH_SPACE);

  area.h += label->size.h;

  pedit = create_edit(NULL, pwindow->dst,
                      create_utf8_from_char_fonto(suggest_name, FONTO_ATTENTION),
     (ok_button->size.w + cancel_button->size.w + adj_size(15)),
                      WF_RESTORE_BACKGROUND);
  pedit->action = newcity_name_edit_callback;

  area.w = MAX(area.w, pedit->size.w + adj_size(20));
  area.h += pedit->size.h + adj_size(25);

  /* I make this hack to center label on window */
  if (label->size.w < area.w) {
    label->size.w = area.w;
  } else {
    area.w = MAX(pwindow->area.w, label->size.w + adj_size(10));
  }

  pedit->size.w = area.w - adj_size(20);

  /* Create window background */
  background = theme_get_background(active_theme, BACKGROUND_NEWCITYDLG);
  if (resize_window(pwindow, background, NULL,
                    (pwindow->size.w - pwindow->area.w) + area.w,
                    (pwindow->size.h - pwindow->area.h) + area.h)) {
    FREESURFACE(background);
  }

  area = pwindow->area;

  /* set start positions */
  widget_set_position(pwindow,
                      (main_window_width() - pwindow->size.w) / 2,
                      (main_window_height() - pwindow->size.h) / 2);

  ok_button->size.x = area.x + adj_size(10);
  ok_button->size.y = area.y + area.h - ok_button->size.h - adj_size(10);

  cancel_button->size.y = ok_button->size.y;
  cancel_button->size.x = area.x + area.w - cancel_button->size.w - adj_size(10);

  pedit->size.x = area.x + adj_size(10);
  pedit->size.y = area.y + adj_size(4) + label->size.h + adj_size(3);

  label->size.x = area.x + adj_size(3);
  label->size.y = area.y + adj_size(4);

  /* enable widgets */
  set_wstate(cancel_button, FC_WS_NORMAL);
  set_wstate(ok_button, FC_WS_NORMAL);
  set_wstate(pedit, FC_WS_NORMAL);
  set_wstate(pwindow, FC_WS_NORMAL);

  /* add widgets to main list */
  new_city_dlg->end_widget_list = pwindow;
  add_to_gui_list(ID_NEWCITY_NAME_WINDOW, pwindow);
  add_to_gui_list(ID_NEWCITY_NAME_LABEL, label);
  add_to_gui_list(ID_NEWCITY_NAME_CANCEL_BUTTON, cancel_button);
  add_to_gui_list(ID_NEWCITY_NAME_OK_BUTTON, ok_button);
  add_to_gui_list(ID_NEWCITY_NAME_EDIT, pedit);
  new_city_dlg->begin_widget_list = pedit;

  /* redraw */
  redraw_group(pedit, pwindow, 0);

  widget_flush(pwindow);
}

/**********************************************************************//**
  Close new city dialog.
**************************************************************************/
void popdown_newcity_dialog(void)
{
  if (new_city_dlg) {
    popdown_window_group_dialog(new_city_dlg->begin_widget_list,
                                new_city_dlg->end_widget_list);
    FC_FREE(new_city_dlg);
    flush_dirty();
  }
}

/**********************************************************************//**
  A turn done button should be provided for the player. This function
  is called to toggle it between active/inactive.
**************************************************************************/
void set_turn_done_button_state(bool state)
{
  if (PAGE_GAME == get_current_client_page()
      && !update_queue_is_switching_page()) {
    if (state) {
      set_wstate(new_turn_button, FC_WS_NORMAL);
    } else {
      set_wstate(new_turn_button, FC_WS_DISABLED);
    }
    widget_redraw(new_turn_button);
    widget_flush(new_turn_button);
    redraw_unit_info_label(get_units_in_focus());
  }
}

/**********************************************************************//**
  Draw a goto or patrol line at the current mouse position.
**************************************************************************/
void create_line_at_mouse_pos(void)
{
  float pos_x, pos_y;

  SDL_GetMouseState(&pos_x, &pos_y);
  update_line(pos_x, pos_y);
  draw_goto_patrol_lines = TRUE;
}

/**********************************************************************//**
  The Area Selection rectangle. Called by center_tile_mapcanvas() and
  when the mouse pointer moves.
**************************************************************************/
void update_rect_at_mouse_pos(void)
{
  /* PORTME */
}
