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
                          dialogs.c  -  description
                             -------------------
    begin                : Wed Jul 24 2002
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* SDL2 */
#ifdef SDL2_PLAIN_INCLUDE
#include <SDL.h>
#else  /* SDL2_PLAIN_INCLUDE */
#include <SDL2/SDL.h>
#endif /* SDL2_PLAIN_INCLUDE */

/* utility */
#include "astring.h"
#include "bitvector.h"
#include "fcintl.h"
#include "log.h"
#include "rand.h"

/* common */
#include "combat.h"
#include "game.h"
#include "government.h"
#include "movement.h"
#include "sex.h"
#include "unitlist.h"

/* client */
#include "client_main.h"
#include "climap.h"   /* For client_tile_get_known() */
#include "goto.h"
#include "helpdata.h" /* For helptext_nation() */
#include "packhand.h"
#include "ratesdlg_g.h"
#include "text.h"
#include "zoom.h"

/* gui-sdl2 */
#include "chatline.h"
#include "citydlg.h"
#include "cityrep.h"
#include "cma_fe.h"
#include "colors.h"
#include "finddlg.h"
#include "gotodlg.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_tilespec.h"
#include "helpdlg.h"
#include "inteldlg.h"
#include "mapctrl.h"
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

#include "dialogs.h"

struct player *races_player;

extern bool is_unit_move_blocked;
extern void popdown_diplomat_dialog(void);
extern void popdown_incite_dialog(void);
extern void popdown_bribe_dialog(void);

void popdown_advanced_terrain_dialog(void);
int advanced_terrain_window_dlg_callback(struct widget *pwindow);
int exit_advanced_terrain_dlg_callback(struct widget *pwidget);

static char *leader_name = NULL;

static void unit_select_dialog_popdown(void);
static void popdown_terrain_info_dialog(void);
static void popdown_pillage_dialog(void);
static void popdown_connect_dialog(void);
static void popdown_unit_upgrade_dlg(void);
static void popdown_unit_disband_dlg(void);

/**********************************************************************//**
  Place window near given tile on screen.
**************************************************************************/
void put_window_near_map_tile(struct widget *pwindow,
                              int window_width, int window_height,
                              struct tile *ptile)
{
  float canvas_x, canvas_y;
  int window_x = 0, window_y = 0;

  if (tile_to_canvas_pos(&canvas_x, &canvas_y, map_zoom, ptile)) {
    if (canvas_x + tileset_tile_width(tileset) + window_width >= main_window_width()) {
      if (canvas_x - window_width < 0) {
        window_x = (main_window_width() - window_width) / 2;
      } else {
        window_x = canvas_x - window_width;
      }
    } else {
      window_x = canvas_x + tileset_tile_width(tileset);
    }

    canvas_y += (tileset_tile_height(tileset) - window_height) / 2;
    if (canvas_y + window_height >= main_window_height()) {
      window_y = main_window_height() - window_height - 1;
    } else {
      if (canvas_y < 0) {
        window_y = 0;
      } else {
        window_y = canvas_y;
      }
    }
  } else {
    window_x = (main_window_width() - window_width) / 2;
    window_y = (main_window_height() - window_height) / 2;
  }

  widget_set_position(pwindow, window_x, window_y);
}

/**********************************************************************//**
  This function is called when the client disconnects or the game is
  over. It should close all dialog windows for that game.
**************************************************************************/
void popdown_all_game_dialogs(void)
{
  unit_select_dialog_popdown();
  popdown_advanced_terrain_dialog();
  popdown_terrain_info_dialog();
  popdown_newcity_dialog();
  popdown_optiondlg(TRUE);
  undraw_order_widgets();
  popdown_diplomat_dialog();
  popdown_pillage_dialog();
  popdown_incite_dialog();
  popdown_connect_dialog();
  popdown_bribe_dialog();
  popdown_find_dialog();
  science_report_dialogs_popdown_all();
  meswin_dialog_popdown();
  popdown_worklist_editor();
  popdown_all_city_dialogs();
  city_report_dialog_popdown();
  economy_report_dialog_popdown();
  units_report_dialog_popdown();
  popdown_intel_dialogs();
  popdown_players_nations_dialog();
  popdown_players_dialog();
  popdown_goto_airlift_dialog();
  popdown_unit_upgrade_dlg();
  popdown_unit_disband_dlg();
  popdown_help_dialog();
  popdown_notify_goto_dialog();

  /* clear gui buffer */
  if (C_S_PREPARING == client_state()) {
    clear_surface(main_data.gui->surface, NULL);
  }
}

/* ======================================================================= */

/**********************************************************************//**
  Find my unit's (focus) chance of success at attacking/defending the
  given enemy unit. Return FALSE if the values cannot be determined (e.g., no
  units given).
**************************************************************************/
static bool sdl_get_chance_to_win(int *att_chance, int *def_chance,
                                  struct unit *enemy_unit,
                                  struct unit *my_unit)
{
  if (!my_unit || !enemy_unit) {
    return FALSE;
  }

  /* Chance to win when active unit is attacking the selected unit */
  *att_chance = unit_win_chance(&(wld.map), my_unit,
                                enemy_unit, NULL) * 100;

  /* Chance to win when selected unit is attacking the active unit */
  *def_chance = (1.0 - unit_win_chance(&(wld.map), enemy_unit,
                                       my_unit, NULL)) * 100;

  return TRUE;
}

/**************************************************************************
  Notify goto dialog.
**************************************************************************/
struct notify_goto_data {
  char *headline;
  char *lines;
  struct tile *ptile;
};

#define SPECLIST_TAG notify_goto
#define SPECLIST_TYPE struct notify_goto_data
#include "speclist.h"

struct notify_goto_dialog {
  struct widget *window;
  struct widget *close_button;
  struct widget *label;
  struct notify_goto_list *datas;
};

static struct notify_goto_dialog *notify_goto_dialog = NULL;

static void notify_goto_dialog_advance(struct notify_goto_dialog *pdialog);

/**********************************************************************//**
  Create a notify goto data.
**************************************************************************/
static struct notify_goto_data *notify_goto_data_new(const char *headline,
                                                     const char *lines,
                                                     struct tile *ptile)
{
  struct notify_goto_data *pdata = fc_malloc(sizeof(*pdata));

  pdata->headline = fc_strdup(headline);
  pdata->lines = fc_strdup(lines);
  pdata->ptile = ptile;

  return pdata;
}

/**********************************************************************//**
  Destroy a notify goto data.
**************************************************************************/
static void notify_goto_data_destroy(struct notify_goto_data *pdata)
{
  free(pdata->headline);
  free(pdata->lines);
}

/**********************************************************************//**
  Move the notify dialog.
**************************************************************************/
static int notify_goto_dialog_callback(struct widget *widget)
{
  struct notify_goto_dialog *pdialog =
      (struct notify_goto_dialog *) widget->data.ptr;

  if (PRESSED_EVENT(main_data.event)) {
    move_window_group(pdialog->label, pdialog->window);
  }

  return -1;
}

/**********************************************************************//**
  Close the notify dialog.
**************************************************************************/
static int notify_goto_dialog_close_callback(struct widget *widget)
{
  struct notify_goto_dialog *pdialog =
      (struct notify_goto_dialog *) widget->data.ptr;

  if (PRESSED_EVENT(main_data.event)) {
    notify_goto_dialog_advance(pdialog);
  }

  return -1;
}

/**********************************************************************//**
  Goto callback.
**************************************************************************/
static int notify_goto_dialog_goto_callback(struct widget *widget)
{
  struct notify_goto_dialog *pdialog =
      (struct notify_goto_dialog *) widget->data.ptr;
  const struct notify_goto_data *pdata = notify_goto_list_get(pdialog->datas,
                                                              0);

  if (PRESSED_EVENT(main_data.event)) {
    if (NULL != pdata->ptile) {
      center_tile_mapcanvas(pdata->ptile);
    }
  } else if (main_data.event.button.button == SDL_BUTTON_RIGHT) {
     struct city *pcity;

     if (NULL != pdata->ptile && (pcity = tile_city(pdata->ptile))) {
       popup_city_dialog(pcity);
    }
  }

  return -1;
}

/**********************************************************************//**
  Create a notify dialog.
**************************************************************************/
static struct notify_goto_dialog *notify_goto_dialog_new(void)
{
  struct notify_goto_dialog *pdialog = fc_malloc(sizeof(*pdialog));
  utf8_str *str;

  /* Window. */
  str = create_utf8_from_char_fonto("", FONTO_ATTENTION);
  str->style |= TTF_STYLE_BOLD;

  pdialog->window = create_window_skeleton(NULL, str, 0);
  pdialog->window->action = notify_goto_dialog_callback;
  pdialog->window->data.ptr = pdialog;
  set_wstate(pdialog->window, FC_WS_NORMAL);
  add_to_gui_list(ID_WINDOW, pdialog->window);

  /* Close button. */
  pdialog->close_button = create_themeicon(current_theme->small_cancel_icon,
                                           pdialog->window->dst,
                                           WF_WIDGET_HAS_INFO_LABEL
                                           | WF_RESTORE_BACKGROUND);
  pdialog->close_button->info_label
    = create_utf8_from_char_fonto(_("Close Dialog (Esc)"), FONTO_ATTENTION);
  pdialog->close_button->action = notify_goto_dialog_close_callback;
  pdialog->close_button->data.ptr = pdialog;
  set_wstate(pdialog->close_button, FC_WS_NORMAL);
  pdialog->close_button->key = SDLK_ESCAPE;
  add_to_gui_list(ID_BUTTON, pdialog->close_button);

  pdialog->label = NULL;

  /* Data list. */
  pdialog->datas = notify_goto_list_new_full(notify_goto_data_destroy);

  return pdialog;
}

/**********************************************************************//**
  Destroy a notify dialog.
**************************************************************************/
static void notify_goto_dialog_destroy(struct notify_goto_dialog *pdialog)
{
  widget_undraw(pdialog->window);
  widget_mark_dirty(pdialog->window);
  remove_gui_layer(pdialog->window->dst);

  del_widget_pointer_from_gui_list(pdialog->window);
  del_widget_pointer_from_gui_list(pdialog->close_button);
  if (NULL != pdialog->label) {
    del_widget_pointer_from_gui_list(pdialog->label);
  }

  notify_goto_list_destroy(pdialog->datas);
  free(pdialog);
}

/**********************************************************************//**
  Update a notify dialog.
**************************************************************************/
static void notify_goto_dialog_update(struct notify_goto_dialog *pdialog)
{
  const struct notify_goto_data *pdata = notify_goto_list_get(pdialog->datas,
                                                              0);

  if (NULL == pdata) {
    return;
  }

  widget_undraw(pdialog->window);
  widget_mark_dirty(pdialog->window);

  copy_chars_to_utf8_str(pdialog->window->string_utf8, pdata->headline);
  if (NULL != pdialog->label) {
    del_widget_pointer_from_gui_list(pdialog->label);
  }
  pdialog->label = create_iconlabel_from_chars_fonto(NULL, pdialog->window->dst,
                                                     pdata->lines,
                                                     FONTO_ATTENTION,
                                                     WF_RESTORE_BACKGROUND);
  pdialog->label->action = notify_goto_dialog_goto_callback;
  pdialog->label->data.ptr = pdialog;
  set_wstate(pdialog->label, FC_WS_NORMAL);
  add_to_gui_list(ID_LABEL, pdialog->label);

  resize_window(pdialog->window, NULL, NULL,
                adj_size(pdialog->label->size.w + 40),
                adj_size(pdialog->label->size.h + 60));
  widget_set_position(pdialog->window,
                      (main_window_width() - pdialog->window->size.w) / 2,
                      (main_window_height() - pdialog->window->size.h) / 2);
  widget_set_position(pdialog->close_button, pdialog->window->size.w
                      - pdialog->close_button->size.w - 1,
                      pdialog->window->size.y + adj_size(2));
  widget_set_position(pdialog->label, adj_size(20), adj_size(40));

  widget_redraw(pdialog->window);
  widget_redraw(pdialog->close_button);
  widget_redraw(pdialog->label);
  widget_mark_dirty(pdialog->window);
  flush_all();
}

/**********************************************************************//**
  Update a notify dialog.
**************************************************************************/
static void notify_goto_dialog_advance(struct notify_goto_dialog *pdialog)
{
  if (1 < notify_goto_list_size(pdialog->datas)) {
    notify_goto_list_remove(pdialog->datas,
                            notify_goto_list_get(pdialog->datas, 0));
    notify_goto_dialog_update(pdialog);
  } else {
    notify_goto_dialog_destroy(pdialog);
    if (pdialog == notify_goto_dialog) {
      notify_goto_dialog = NULL;
    }
  }
}

/**********************************************************************//**
  Popup a dialog to display information about an event that has a
  specific location. The user should be given the option to goto that
  location.
**************************************************************************/
void popup_notify_goto_dialog(const char *headline, const char *lines,
                              const struct text_tag_list *tags,
                              struct tile *ptile)
{
  if (NULL == notify_goto_dialog) {
    notify_goto_dialog = notify_goto_dialog_new();
  }

  fc_assert(NULL != notify_goto_dialog);

  notify_goto_list_prepend(notify_goto_dialog->datas,
                           notify_goto_data_new(headline, lines, ptile));
  notify_goto_dialog_update(notify_goto_dialog);
}

/**********************************************************************//**
  Popdown the notify goto dialog.
**************************************************************************/
void popdown_notify_goto_dialog(void)
{
  if (NULL != notify_goto_dialog) {
    notify_goto_dialog_destroy(notify_goto_dialog);
    notify_goto_dialog = NULL;
  }
}

/**********************************************************************//**
  Popup a dialog to display connection message from server.
**************************************************************************/
void popup_connect_msg(const char *headline, const char *message)
{
  log_error("popup_connect_msg() PORT ME");
}

/* ----------------------------------------------------------------------- */
struct advanced_dialog *notify_dlg = NULL;

/**********************************************************************//**
  User interacted with generic notify dialog.
**************************************************************************/
static int notify_dialog_window_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    move_window_group(notify_dlg->begin_widget_list, pwindow);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with notify dialog close button.
**************************************************************************/
static int exit_notify_dialog_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (notify_dlg) {
      popdown_window_group_dialog(notify_dlg->begin_widget_list,
                                  notify_dlg->end_widget_list);
      FC_FREE(notify_dlg->scroll);
      FC_FREE(notify_dlg);
      flush_dirty();
    }
  }

  return -1;
}

/**********************************************************************//**
  Popup a generic dialog to display some generic information.
**************************************************************************/
void popup_notify_dialog(const char *caption, const char *headline,
                         const char *lines)
{
  struct widget *buf, *pwindow;
  utf8_str *pstr;
  SDL_Surface *headline_surf, *lines_surf;
  SDL_Rect dst;
  SDL_Rect area;

  if (notify_dlg) {
    return;
  }

  notify_dlg = fc_calloc(1, sizeof(struct advanced_dialog));

  pstr = create_utf8_from_char_fonto(caption, FONTO_ATTENTION);
  pstr->style |= TTF_STYLE_BOLD;

  pwindow = create_window_skeleton(NULL, pstr, 0);

  pwindow->action = notify_dialog_window_callback;
  set_wstate(pwindow, FC_WS_NORMAL);

  add_to_gui_list(ID_WINDOW, pwindow);
  notify_dlg->end_widget_list = pwindow;

  area = pwindow->area;

  /* ---------- */
  /* Create exit button */
  buf = create_themeicon(current_theme->small_cancel_icon, pwindow->dst,
                         WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  buf->info_label = create_utf8_from_char_fonto(_("Close Dialog (Esc)"),
                                                FONTO_ATTENTION);
  buf->action = exit_notify_dialog_callback;
  set_wstate(buf, FC_WS_NORMAL);
  buf->key = SDLK_ESCAPE;
  area.w += (buf->size.w + adj_size(10));

  add_to_gui_list(ID_BUTTON, buf);
  notify_dlg->begin_widget_list = buf;

  pstr = create_utf8_from_char_fonto(headline, FONTO_BIG);
  pstr->style |= TTF_STYLE_BOLD;

  headline_surf = create_text_surf_from_utf8(pstr);

  if (lines && *lines != '\0') {
    change_fonto_utf8(pstr, FONTO_ATTENTION);
    pstr->style &= ~TTF_STYLE_BOLD;
    copy_chars_to_utf8_str(pstr, lines);
    lines_surf = create_text_surf_from_utf8(pstr);
  } else {
    lines_surf = NULL;
  }

  FREEUTF8STR(pstr);

  area.w = MAX(area.w, headline_surf->w);
  if (lines_surf) {
    area.w = MAX(area.w, lines_surf->w);
  }
  area.w += adj_size(60);
  area.h = MAX(area.h, adj_size(10) + headline_surf->h + adj_size(10));
  if (lines_surf) {
    area.h += lines_surf->h + adj_size(10);
  }

  resize_window(pwindow, NULL, get_theme_color(COLOR_THEME_BACKGROUND),
                (pwindow->size.w - pwindow->area.w) + area.w,
                (pwindow->size.h - pwindow->area.h) + area.h);

  area = pwindow->area;

  widget_set_position(pwindow,
                      (main_window_width() - pwindow->size.w) / 2,
                      (main_window_height() - pwindow->size.h) / 2);

  dst.x = area.x + (area.w - headline_surf->w) / 2;
  dst.y = area.y + adj_size(10);

  alphablit(headline_surf, NULL, pwindow->theme, &dst, 255);
  if (lines_surf) {
    dst.y += headline_surf->h + adj_size(10);
    if (headline_surf->w < lines_surf->w) {
      dst.x = area.x + (area.w - lines_surf->w) / 2;
    }

    alphablit(lines_surf, NULL, pwindow->theme, &dst, 255);
  }

  FREESURFACE(headline_surf);
  FREESURFACE(lines_surf);

  /* Exit button */
  buf = pwindow->prev;
  buf->size.x = area.x + area.w - buf->size.w - 1;
  buf->size.y = pwindow->size.y + adj_size(2);

  /* Redraw */
  redraw_group(notify_dlg->begin_widget_list, pwindow, 0);
  widget_flush(pwindow);
}

/* =======================================================================*/
/* ========================= UNIT UPGRADE DIALOG =========================*/
/* =======================================================================*/
static struct small_dialog *unit_upgrade_dlg = NULL;

/**********************************************************************//**
  User interacted with upgrade unit widget.
**************************************************************************/
static int upgrade_unit_window_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    move_window_group(unit_upgrade_dlg->begin_widget_list, pwindow);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with upgrade unit dialog cancel -button
**************************************************************************/
static int cancel_upgrade_unit_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popdown_unit_upgrade_dlg();
    /* enable city dlg */
    enable_city_dlg_widgets();
    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  User interacted with unit upgrade dialog "Upgrade" -button.
**************************************************************************/
static int ok_upgrade_unit_window_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct unit *punit = pwidget->data.unit;

    popdown_unit_upgrade_dlg();
    /* enable city dlg */
    enable_city_dlg_widgets();
    free_city_units_lists();
    request_unit_upgrade(punit);
    flush_dirty();
  }

  return -1;
}

/***********************************************************************//**
  Popup dialog for upgrade units
***************************************************************************/
void popup_upgrade_dialog(struct unit_list *punits)
{
  /* PORTME: is support for more than one unit in punits required? */

  /* Assume only one unit for now. */
  fc_assert_msg(unit_list_size(punits) <= 1,
                "SDL2 popup_upgrade_dialog() handles max 1 unit.");

  popup_unit_upgrade_dlg(unit_list_get(punits, 0), FALSE);
}

/**********************************************************************//**
  Open unit upgrade dialog.
**************************************************************************/
void popup_unit_upgrade_dlg(struct unit *punit, bool city)
{
  char cbuf[128];
  struct widget *buf = NULL, *pwindow;
  utf8_str *pstr;
  SDL_Surface *text;
  SDL_Rect dst;
  int window_x = 0, window_y = 0;
  enum unit_upgrade_result unit_upgrade_result;
  SDL_Rect area;

  if (unit_upgrade_dlg) {
    /* just in case */
    flush_dirty();
    return;
  }

  unit_upgrade_dlg = fc_calloc(1, sizeof(struct small_dialog));

  unit_upgrade_result = unit_upgrade_info(&(wld.map), punit, cbuf, sizeof(cbuf));

  pstr = create_utf8_from_char_fonto(_("Upgrade Obsolete Units"),
                                     FONTO_ATTENTION);
  pstr->style |= TTF_STYLE_BOLD;

  pwindow = create_window_skeleton(NULL, pstr, 0);

  pwindow->action = upgrade_unit_window_callback;
  set_wstate(pwindow, FC_WS_NORMAL);

  unit_upgrade_dlg->end_widget_list = pwindow;

  add_to_gui_list(ID_WINDOW, pwindow);

  area = pwindow->area;

  /* ============================================================= */

  /* Create text label */
  pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  pstr->style |= (TTF_STYLE_BOLD|SF_CENTER);
  pstr->fgcol = *get_theme_color(COLOR_THEME_UNITUPGRADE_TEXT);

  text = create_text_surf_from_utf8(pstr);
  FREEUTF8STR(pstr);

  area.w = MAX(area.w, text->w + adj_size(20));
  area.h += (text->h + adj_size(10));

  /* Cancel button */
  buf = create_themeicon_button_from_chars_fonto(current_theme->cancel_icon,
                                                 pwindow->dst, _("Cancel"),
                                                 FONTO_ATTENTION, 0);

  buf->action = cancel_upgrade_unit_callback;
  set_wstate(buf, FC_WS_NORMAL);

  area.h += (buf->size.h + adj_size(20));

  add_to_gui_list(ID_BUTTON, buf);

  if (UU_OK == unit_upgrade_result) {
    buf = create_themeicon_button_from_chars_fonto(current_theme->ok_icon,
                                                   pwindow->dst,
                                                   _("Upgrade"),
                                                   FONTO_ATTENTION, 0);

    buf->action = ok_upgrade_unit_window_callback;
    set_wstate(buf, FC_WS_NORMAL);
    buf->data.unit = punit;
    add_to_gui_list(ID_BUTTON, buf);
    buf->size.w = MAX(buf->size.w, buf->next->size.w);
    buf->next->size.w = buf->size.w;
    area.w = MAX(area.w, adj_size(30) + buf->size.w * 2);
  } else {
    area.w = MAX(area.w, buf->size.w + adj_size(20));
  }
  /* ============================================ */

  unit_upgrade_dlg->begin_widget_list = buf;

  resize_window(pwindow, NULL, get_theme_color(COLOR_THEME_BACKGROUND),
                (pwindow->size.w - pwindow->area.w) + area.w,
                (pwindow->size.h - pwindow->area.h) + area.h);

  area = pwindow->area;

  if (city) {
    window_x = main_data.event.motion.x;
    window_y = main_data.event.motion.y;
  } else {
    put_window_near_map_tile(pwindow, pwindow->size.w, pwindow->size.h,
                             unit_tile(punit));
  }

  widget_set_position(pwindow, window_x, window_y);

  /* setup rest of widgets */
  /* label */
  dst.x = area.x + (area.w - text->w) / 2;
  dst.y = area.y + adj_size(10);
  alphablit(text, NULL, pwindow->theme, &dst, 255);
  FREESURFACE(text);

  /* cancel button */
  buf = pwindow->prev;
  buf->size.y = area.y + area.h - buf->size.h - adj_size(7);

  if (UU_OK == unit_upgrade_result) {
    /* upgrade button */
    buf = buf->prev;
    buf->size.x = area.x + (area.w - (2 * buf->size.w + adj_size(10))) / 2;
    buf->size.y = buf->next->size.y;

    /* cancel button */
    buf->next->size.x = buf->size.x + buf->size.w + adj_size(10);
  } else {
    /* x position of cancel button */
    buf->size.x = area.x + area.w - buf->size.w - adj_size(10);
  }

  /* ================================================== */
  /* redraw */
  redraw_group(unit_upgrade_dlg->begin_widget_list, pwindow, 0);

  widget_mark_dirty(pwindow);
  flush_dirty();
}

/**********************************************************************//**
  Close unit upgrade dialog.
**************************************************************************/
static void popdown_unit_upgrade_dlg(void)
{
  if (unit_upgrade_dlg) {
    popdown_window_group_dialog(unit_upgrade_dlg->begin_widget_list,
                                unit_upgrade_dlg->end_widget_list);
    FC_FREE(unit_upgrade_dlg);
  }
}

/* =======================================================================*/
/* ========================= UNIT DISBAND DIALOG =========================*/
/* =======================================================================*/
static struct small_dialog *unit_disband_dlg = NULL;

/**********************************************************************//**
  User interacted with disband unit widget.
**************************************************************************/
static int disband_unit_window_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    move_window_group(unit_disband_dlg->begin_widget_list, pwindow);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with disband unit dialog cancel -button
**************************************************************************/
static int cancel_disband_unit_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popdown_unit_disband_dlg();
    /* enable city dlg */
    enable_city_dlg_widgets();
    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  User interacted with unit disband dialog "Disband" -button.
**************************************************************************/
static int ok_disband_unit_window_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct unit *punit = pwidget->data.unit;

    popdown_unit_disband_dlg();
    /* enable city dlg */
    enable_city_dlg_widgets();
    free_city_units_lists();
    request_unit_disband(punit);
    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  Open unit disband dialog.
**************************************************************************/
void popup_unit_disband_dlg(struct unit *punit, bool city)
{
  char cbuf[128];
  struct widget *buf = NULL, *pwindow;
  utf8_str *pstr;
  SDL_Surface *text;
  SDL_Rect dst;
  int window_x = 0, window_y = 0;
  bool unit_disband_result;
  SDL_Rect area;

  if (unit_disband_dlg) {
    /* just in case */
    flush_dirty();
    return;
  }

  unit_disband_dlg = fc_calloc(1, sizeof(struct small_dialog));

  {
    struct unit_list *punits = unit_list_new();

    unit_list_append(punits, punit);
    unit_disband_result = get_units_disband_info(cbuf, sizeof(cbuf), punits);
    unit_list_destroy(punits);
  }

  pstr = create_utf8_from_char_fonto(_("Disband Units"), FONTO_ATTENTION);
  pstr->style |= TTF_STYLE_BOLD;

  pwindow = create_window_skeleton(NULL, pstr, 0);

  pwindow->action = disband_unit_window_callback;
  set_wstate(pwindow, FC_WS_NORMAL);

  unit_disband_dlg->end_widget_list = pwindow;

  add_to_gui_list(ID_WINDOW, pwindow);

  area = pwindow->area;

  /* ============================================================= */

  /* Create text label */
  pstr = create_utf8_from_char_fonto(cbuf, FONTO_DEFAULT);
  pstr->style |= (TTF_STYLE_BOLD|SF_CENTER);
  pstr->fgcol = *get_theme_color(COLOR_THEME_UNITDISBAND_TEXT);

  text = create_text_surf_from_utf8(pstr);
  FREEUTF8STR(pstr);

  area.w = MAX(area.w, text->w + adj_size(20));
  area.h += (text->h + adj_size(10));

  /* Cancel button */
  buf = create_themeicon_button_from_chars_fonto(current_theme->cancel_icon,
                                                 pwindow->dst, _("Cancel"),
                                                 FONTO_ATTENTION, 0);

  buf->action = cancel_disband_unit_callback;
  set_wstate(buf, FC_WS_NORMAL);

  area.h += (buf->size.h + adj_size(20));

  add_to_gui_list(ID_BUTTON, buf);

  if (unit_disband_result) {
    buf = create_themeicon_button_from_chars_fonto(current_theme->ok_icon,
                                                   pwindow->dst,
                                                   _("Disband"),
                                                   FONTO_ATTENTION, 0);

    buf->action = ok_disband_unit_window_callback;
    set_wstate(buf, FC_WS_NORMAL);
    buf->data.unit = punit;
    add_to_gui_list(ID_BUTTON, buf);
    buf->size.w = MAX(buf->size.w, buf->next->size.w);
    buf->next->size.w = buf->size.w;
    area.w = MAX(area.w, adj_size(30) + buf->size.w * 2);
  } else {
    area.w = MAX(area.w, buf->size.w + adj_size(20));
  }
  /* ============================================ */

  unit_disband_dlg->begin_widget_list = buf;

  resize_window(pwindow, NULL, get_theme_color(COLOR_THEME_BACKGROUND),
                (pwindow->size.w - pwindow->area.w) + area.w,
                (pwindow->size.h - pwindow->area.h) + area.h);

  area = pwindow->area;

  if (city) {
    window_x = main_data.event.motion.x;
    window_y = main_data.event.motion.y;
  } else {
    put_window_near_map_tile(pwindow, pwindow->size.w, pwindow->size.h,
                             unit_tile(punit));
  }

  widget_set_position(pwindow, window_x, window_y);

  /* setup rest of widgets */
  /* label */
  dst.x = area.x + (area.w - text->w) / 2;
  dst.y = area.y + adj_size(10);
  alphablit(text, NULL, pwindow->theme, &dst, 255);
  FREESURFACE(text);

  /* cancel button */
  buf = pwindow->prev;
  buf->size.y = area.y + area.h - buf->size.h - adj_size(7);

  if (unit_disband_result) {
    /* disband button */
    buf = buf->prev;
    buf->size.x = area.x + (area.w - (2 * buf->size.w + adj_size(10))) / 2;
    buf->size.y = buf->next->size.y;

    /* cancel button */
    buf->next->size.x = buf->size.x + buf->size.w + adj_size(10);
  } else {
    /* x position of cancel button */
    buf->size.x = area.x + area.w - buf->size.w - adj_size(10);
  }

  /* ================================================== */
  /* redraw */
  redraw_group(unit_disband_dlg->begin_widget_list, pwindow, 0);

  widget_mark_dirty(pwindow);
  flush_dirty();
}

/**********************************************************************//**
  Close unit disband dialog.
**************************************************************************/
static void popdown_unit_disband_dlg(void)
{
  if (unit_disband_dlg) {
    popdown_window_group_dialog(unit_disband_dlg->begin_widget_list,
                                unit_disband_dlg->end_widget_list);
    FC_FREE(unit_disband_dlg);
  }
}

/* =======================================================================*/
/* ======================== UNIT SELECTION DIALOG ========================*/
/* =======================================================================*/
static struct advanced_dialog *unit_select_dlg = NULL;

/**********************************************************************//**
  User interacted with unit selection window.
**************************************************************************/
static int unit_select_window_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    move_window_group(unit_select_dlg->begin_widget_list, pwindow);
  }

  return -1;
}

/**********************************************************************//**
  User requested unit select window to be closed.
**************************************************************************/
static int exit_unit_select_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    unit_select_dialog_popdown();
    is_unit_move_blocked = FALSE;
  }

  return -1;
}

/**********************************************************************//**
  User selected unit from unit select window.
**************************************************************************/
static int unit_select_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct unit *punit =
      player_unit_by_number(client_player(), MAX_ID - pwidget->id);

    unit_select_dialog_popdown();
    if (punit) {
      request_new_unit_activity(punit, ACTIVITY_IDLE);
      unit_focus_set(punit);
    }
  }

  return -1;
}

/**********************************************************************//**
  Popdown a dialog window to select units on a particular tile.
**************************************************************************/
static void unit_select_dialog_popdown(void)
{
  if (unit_select_dlg) {
    is_unit_move_blocked = FALSE;
    popdown_window_group_dialog(unit_select_dlg->begin_widget_list,
                                unit_select_dlg->end_widget_list);

    FC_FREE(unit_select_dlg->scroll);
    FC_FREE(unit_select_dlg);
    flush_dirty();
  }
}

/**********************************************************************//**
  Popup a dialog window to select units on a particular tile.
**************************************************************************/
void unit_select_dialog_popup(struct tile *ptile)
{
  struct widget *buf = NULL, *pwindow;
  utf8_str *pstr;
  struct unit *punit = NULL, *focus = head_of_units_in_focus();
  const struct unit_type *punittype;
  char cbuf[255];
  int i, w = 0, n;
  SDL_Rect area;

#define NUM_SEEN 20

  n = unit_list_size(ptile->units);

  if (n == 0 || unit_select_dlg) {
    return;
  }

  is_unit_move_blocked = TRUE;
  unit_select_dlg = fc_calloc(1, sizeof(struct advanced_dialog));

  fc_snprintf(cbuf, sizeof(cbuf), "%s (%d)", _("Unit selection"), n);
  pstr = create_utf8_from_char_fonto(cbuf, FONTO_ATTENTION);
  pstr->style |= TTF_STYLE_BOLD;

  pwindow = create_window_skeleton(NULL, pstr, 0);

  pwindow->action = unit_select_window_callback;
  set_wstate(pwindow, FC_WS_NORMAL);

  add_to_gui_list(ID_UNIT_SELECT_DLG_WINDOW, pwindow);
  unit_select_dlg->end_widget_list = pwindow;

  area = pwindow->area;

  /* ---------- */
  /* Create exit button */
  buf = create_themeicon(current_theme->small_cancel_icon, pwindow->dst,
                         WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  buf->info_label = create_utf8_from_char_fonto(_("Close Dialog (Esc)"),
                                                FONTO_ATTENTION);
  buf->action = exit_unit_select_callback;
  set_wstate(buf, FC_WS_NORMAL);
  buf->key = SDLK_ESCAPE;
  area.w += (buf->size.w + adj_size(10));

  add_to_gui_list(ID_UNIT_SELECT_DLG_EXIT_BUTTON, buf);

  /* ---------- */

  for (i = 0; i < n; i++) {
    const char *vetname;

    punit = unit_list_get(ptile->units, i);
    punittype = unit_type_get(punit);
    vetname = utype_veteran_name_translation(punittype, punit->veteran);

    if (unit_owner(punit) == client.conn.playing) {
      struct astring addition = ASTRING_INIT;

      unit_activity_astr(punit, &addition);
      fc_snprintf(cbuf , sizeof(cbuf), _("Contact %s (%d / %d) %s(%d,%d,%s) %s"),
                  (vetname != NULL ? vetname : ""),
                  punit->hp, punittype->hp,
                  utype_name_translation(punittype),
                  punittype->attack_strength,
                  punittype->defense_strength,
                  move_points_text(punittype->move_rate, FALSE),
                  astr_str(&addition));
      astr_free(&addition);
    } else {
      int att_chance, def_chance;

      fc_snprintf(cbuf , sizeof(cbuf), _("%s %s %s(A:%d D:%d M:%s FP:%d) HP:%d%%"),
                  nation_adjective_for_player(unit_owner(punit)),
                  (vetname != NULL ? vetname : ""),
                  utype_name_translation(punittype),
                  punittype->attack_strength,
                  punittype->defense_strength,
                  move_points_text(punittype->move_rate, FALSE),
                  punittype->firepower,
                  (punit->hp * 100 / punittype->hp + 9) / 10);

      /* Calculate chance to win */
      if (sdl_get_chance_to_win(&att_chance, &def_chance, punit, focus)) {
        /* TRANS: "CtW" = "Chance to Win"; preserve leading space */
        cat_snprintf(cbuf, sizeof(cbuf), _(" CtW: Att:%d%% Def:%d%%"),
                     att_chance, def_chance);
      }
    }

    create_active_iconlabel(buf, pwindow->dst, pstr, cbuf,
                            unit_select_callback);

    add_to_gui_list(MAX_ID - punit->id , buf);

    area.w = MAX(area.w, buf->size.w);
    area.h += buf->size.h;
    if (unit_owner(punit) == client.conn.playing) {
      set_wstate(buf, FC_WS_NORMAL);
    }

    if (i > NUM_SEEN - 1) {
      set_wflag(buf , WF_HIDDEN);
    }
  }

  unit_select_dlg->begin_widget_list = buf;
  unit_select_dlg->begin_active_widget_list = unit_select_dlg->begin_widget_list;
  unit_select_dlg->end_active_widget_list = pwindow->prev->prev;
  unit_select_dlg->active_widget_list = unit_select_dlg->end_active_widget_list;

  area.w += adj_size(2);
  if (n > NUM_SEEN) {
    n = create_vertical_scrollbar(unit_select_dlg, 1, NUM_SEEN, TRUE, TRUE);
    area.w += n;

    /* ------- window ------- */
    area.h = NUM_SEEN * pwindow->prev->prev->size.h;
  }

  resize_window(pwindow, NULL, NULL,
                (pwindow->size.w - pwindow->area.w) + area.w,
                (pwindow->size.h - pwindow->area.h) + area.h);

  area = pwindow->area;

  put_window_near_map_tile(pwindow, pwindow->size.w, pwindow->size.h,
                           ptile);

  w = area.w;

  if (unit_select_dlg->scroll) {
    w -= n;
  }

  /* Exit button */
  buf = pwindow->prev;
  buf->size.x = area.x + area.w - buf->size.w - 1;
  buf->size.y = pwindow->size.y + adj_size(2);
  buf = buf->prev;

  setup_vertical_widgets_position(1, area.x + 1, area.y, w, 0,
                                  unit_select_dlg->begin_active_widget_list,
                                  buf);

  if (unit_select_dlg->scroll) {
    setup_vertical_scrollbar_area(unit_select_dlg->scroll,
                                  area.x + area.w, area.y,
                                  area.h, TRUE);
  }

  /* ==================================================== */
  /* Redraw */
  redraw_group(unit_select_dlg->begin_widget_list, pwindow, 0);

  widget_flush(pwindow);
}

/**********************************************************************//**
  Update the dialog window to select units on a particular tile.
**************************************************************************/
void unit_select_dialog_update_real(void *unused)
{
  /* PORTME */
}

/* ====================================================================== */
/* ============================ TERRAIN INFO ============================ */
/* ====================================================================== */
static struct small_dialog *terrain_info_dlg = NULL;


/**********************************************************************//**
  Popdown terrain information dialog.
**************************************************************************/
static int terrain_info_window_dlg_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    move_window_group(terrain_info_dlg->begin_widget_list, pwindow);
  }

  return -1;
}

/**********************************************************************//**
  Popdown terrain information dialog.
**************************************************************************/
static void popdown_terrain_info_dialog(void)
{
  if (terrain_info_dlg) {
    popdown_window_group_dialog(terrain_info_dlg->begin_widget_list,
                                terrain_info_dlg->end_widget_list);
    FC_FREE(terrain_info_dlg);
    flush_dirty();
  }
}

/**********************************************************************//**
  Popdown terrain information dialog.
**************************************************************************/
static int exit_terrain_info_dialog_callback(struct widget *button)
{
  if (PRESSED_EVENT(main_data.event)) {
    popdown_terrain_info_dialog();
  }

  return -1;
}

/**********************************************************************//**
  Return a (static) string with terrain defense bonus.
  This does not include bonuses some units may get out of bases.
**************************************************************************/
const char *sdl_get_tile_defense_info_text(struct tile *ptile)
{
  static char buffer[64];
  int bonus = (tile_terrain(ptile)->defense_bonus - 10) * 10;

  extra_type_iterate(pextra) {
    if (tile_has_extra(ptile, pextra)
        && pextra->category == ECAT_NATURAL) {
      bonus += pextra->defense_bonus;
    }
  } extra_type_iterate_end;

  fc_snprintf(buffer, sizeof(buffer), _("Terrain Defense Bonus: +%d%% "), bonus);

  return buffer;
}

/**********************************************************************//**
  Popup terrain information dialog.
**************************************************************************/
static void popup_terrain_info_dialog(SDL_Surface *pdest, struct tile *ptile)
{
  SDL_Surface *surf;
  struct widget *buf, *pwindow;
  utf8_str *pstr;
  char cbuf[256];
  SDL_Rect area;

  if (terrain_info_dlg) {
    flush_dirty();
    return;
  }

  surf = get_terrain_surface(ptile);
  terrain_info_dlg = fc_calloc(1, sizeof(struct small_dialog));

  /* ----------- */
  fc_snprintf(cbuf, sizeof(cbuf), "%s [%d,%d]", _("Terrain Info"),
              TILE_XY(ptile));

  pwindow
    = create_window_skeleton(NULL,
                             create_utf8_from_char_fonto(cbuf, FONTO_ATTENTION),
                             0);
  pwindow->string_utf8->style |= TTF_STYLE_BOLD;

  pwindow->action = terrain_info_window_dlg_callback;
  set_wstate(pwindow, FC_WS_NORMAL);

  add_to_gui_list(ID_TERRAIN_INFO_DLG_WINDOW, pwindow);
  terrain_info_dlg->end_widget_list = pwindow;

  area = pwindow->area;

  /* ---------- */
  pstr = create_utf8_from_char_fonto(popup_info_text(ptile),
                                     FONTO_ATTENTION);
  pstr->style |= SF_CENTER;
  buf = create_iconlabel(surf, pwindow->dst, pstr, 0);

  buf->size.h += tileset_tile_height(tileset) / 2;

  add_to_gui_list(ID_LABEL, buf);

  /* ------ window ---------- */
  area.w = MAX(area.w, buf->size.w + adj_size(20));
  area.h = MAX(area.h, buf->size.h);

  resize_window(pwindow, NULL, get_theme_color(COLOR_THEME_BACKGROUND),
                (pwindow->size.w - pwindow->area.w) + area.w,
                (pwindow->size.h - pwindow->area.h) + area.h);

  area = pwindow->area;

  put_window_near_map_tile(pwindow, pwindow->size.w, pwindow->size.h, ptile);

  /* ------------------------ */

  buf->size.x = area.x + adj_size(10);
  buf->size.y = area.y;

  /* Exit icon */
  buf = create_themeicon(current_theme->small_cancel_icon, pwindow->dst,
                         WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  buf->info_label = create_utf8_from_char_fonto(_("Close Dialog (Esc)"),
                                                FONTO_ATTENTION);
  buf->size.x = area.x + area.w - buf->size.w - 1;
  buf->size.y = pwindow->size.y + adj_size(2);
  buf->action = exit_terrain_info_dialog_callback;
  set_wstate(buf, FC_WS_NORMAL);
  buf->key = SDLK_ESCAPE;

  add_to_gui_list(ID_TERRAIN_INFO_DLG_EXIT_BUTTON, buf);

  terrain_info_dlg->begin_widget_list = buf;
  /* --------------------------------- */
  /* redraw */
  redraw_group(terrain_info_dlg->begin_widget_list, pwindow, 0);
  widget_mark_dirty(pwindow);
  flush_dirty();
}

/* ====================================================================== */
/* ========================= ADVANCED_TERRAIN_MENU ====================== */
/* ====================================================================== */
struct advanced_dialog *advanced_terrain_dlg = NULL;

/**********************************************************************//**
  Popdown a generic dialog to display some generic information about
  terrain : tile, units , cities, etc.
**************************************************************************/
void popdown_advanced_terrain_dialog(void)
{
  if (advanced_terrain_dlg) {
    is_unit_move_blocked = FALSE;
    popdown_window_group_dialog(advanced_terrain_dlg->begin_widget_list,
                                advanced_terrain_dlg->end_widget_list);

    FC_FREE(advanced_terrain_dlg->scroll);
    FC_FREE(advanced_terrain_dlg);
  }
}

/**********************************************************************//**
  User selected "Advanced Menu"
**************************************************************************/
int advanced_terrain_window_dlg_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    move_window_group(advanced_terrain_dlg->begin_widget_list, pwindow);
  }

  return -1;
}

/**********************************************************************//**
  User requested closing of advanced terrain dialog.
**************************************************************************/
int exit_advanced_terrain_dlg_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popdown_advanced_terrain_dialog();
    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  User requested terrain info.
**************************************************************************/
static int terrain_info_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    int x = pwidget->data.cont->id0;
    int y = pwidget->data.cont->id1;

    popdown_advanced_terrain_dialog();

    popup_terrain_info_dialog(NULL, map_pos_to_tile(&(wld.map), x , y));
  }

  return -1;
}

/**********************************************************************//**
  User requested zoom to city.
**************************************************************************/
static int zoom_to_city_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct city *pcity = pwidget->data.city;

    popdown_advanced_terrain_dialog();

    popup_city_dialog(pcity);
  }

  return -1;
}

/**********************************************************************//**
  User requested production change.
**************************************************************************/
static int change_production_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct city *pcity = pwidget->data.city;

    popdown_advanced_terrain_dialog();
    popup_worklist_editor(pcity, NULL);
  }

  return -1;
}

/**********************************************************************//**
  User requested hurry production.
**************************************************************************/
static int hurry_production_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct city *pcity = pwidget->data.city;

    popdown_advanced_terrain_dialog();

    popup_hurry_production_dialog(pcity, NULL);
  }

  return -1;
}

/**********************************************************************//**
  User requested opening of cma settings.
**************************************************************************/
static int cma_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct city *pcity = pwidget->data.city;

    popdown_advanced_terrain_dialog();
    popup_city_cma_dialog(pcity);
  }

  return -1;
}

/**********************************************************************//**
  User selected unit.
**************************************************************************/
static int adv_unit_select_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct unit *punit = pwidget->data.unit;

    popdown_advanced_terrain_dialog();

    if (punit) {
      request_new_unit_activity(punit, ACTIVITY_IDLE);
      unit_focus_set(punit);
    }
  }

  return -1;
}

/**********************************************************************//**
  User selected all units from tile.
**************************************************************************/
static int adv_unit_select_all_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct unit *punit = pwidget->data.unit;

    popdown_advanced_terrain_dialog();

    if (punit) {
      activate_all_units(unit_tile(punit));
    }
  }

  return -1;
}

/**********************************************************************//**
  Sentry unit widget contains.
**************************************************************************/
static int adv_unit_sentry_idle_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct unit *punit = pwidget->data.unit;

    popdown_advanced_terrain_dialog();

    if (punit) {
      struct tile *ptile = unit_tile(punit);

      unit_list_iterate(ptile->units, other_unit) {
        if (unit_owner(other_unit) == client.conn.playing
            && ACTIVITY_IDLE == other_unit->activity
            && other_unit->ssa_controller == SSA_NONE
            && can_unit_do_activity_client(other_unit, ACTIVITY_SENTRY)) {
          request_new_unit_activity(other_unit, ACTIVITY_SENTRY);
        }
      } unit_list_iterate_end;
    }
  }

  return -1;
}

/**********************************************************************//**
  Initiate goto to selected tile.
**************************************************************************/
static int goto_here_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    int x = pwidget->data.cont->id0;
    int y = pwidget->data.cont->id1;

    popdown_advanced_terrain_dialog();

    /* may not work */
    send_goto_tile(head_of_units_in_focus(),
                   map_pos_to_tile(&(wld.map), x, y));
  }

  return -1;
}

/**********************************************************************//**
  Initiate patrol to selected tile.
**************************************************************************/
static int patrol_here_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    int x = pwidget->data.cont->id0;
    int y = pwidget->data.cont->id1;
    struct tile *ptile;

    ptile = map_pos_to_tile(&(wld.map), x, y);

    if (ptile != NULL) {
      struct unit_list *punits = get_units_in_focus();

      set_hover_state(punits, HOVER_PATROL, ACTIVITY_LAST, NULL,
                      NO_TARGET, NO_TARGET, ACTION_NONE, ORDER_LAST);
      update_unit_info_label(punits);
      enter_goto_state(punits);
      do_unit_patrol_to(ptile);
      exit_goto_state();
    }

    popdown_advanced_terrain_dialog();
  }

  return -1;
}

/**********************************************************************//**
  Initiate paradrop to selected tile.
**************************************************************************/
static int paradrop_here_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    int x = pwidget->data.cont->id0;
    int y = pwidget->data.cont->id1;
    struct tile *ptile;

    ptile = map_pos_to_tile(&(wld.map), x, y);

    if (ptile != NULL) {
      struct unit_list *punits = get_units_in_focus();

      set_hover_state(punits, HOVER_PARADROP, ACTIVITY_LAST, NULL,
                      NO_TARGET, NO_TARGET, ACTION_NONE, ORDER_LAST);
      update_unit_info_label(punits);

      unit_list_iterate(punits, punit) {
        do_unit_paradrop_to(punit, ptile);
      } unit_list_iterate_end;

      clear_hover_state();
      exit_goto_state();
    }

    popdown_advanced_terrain_dialog();
  }

  return -1;
}

/**********************************************************************//**
  Show help about unit type.
**************************************************************************/
static int unit_help_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    Unit_type_id unit_id = MAX_ID - pwidget->id;

    popdown_advanced_terrain_dialog();
    popup_unit_info(unit_id);
  }

  return -1;
}

/**********************************************************************//**
  Popup a generic dialog to display some generic information about
  terrain : tile, units , cities, etc.
**************************************************************************/
void popup_advanced_terrain_dialog(struct tile *ptile,
                                   Uint16 pos_x, Uint16 pos_y)
{
  struct widget *pwindow = NULL, *buf = NULL;
  struct city *pcity;
  struct unit *focus_unit;
  utf8_str *pstr;
  SDL_Rect area2;
  struct container *cont;
  char cbuf[255];
  int n, w = 0, h, units_h = 0;
  SDL_Rect area;

  if (advanced_terrain_dlg) {
    return;
  }

  pcity = tile_city(ptile);
  n = unit_list_size(ptile->units);
  focus_unit = head_of_units_in_focus();

  if (!n && !pcity && !focus_unit) {
    popup_terrain_info_dialog(NULL, ptile);

    return;
  }

  area.h = adj_size(2);
  is_unit_move_blocked = TRUE;

  advanced_terrain_dlg = fc_calloc(1, sizeof(struct advanced_dialog));

  cont = fc_calloc(1, sizeof(struct container));
  cont->id0 = index_to_map_pos_x(tile_index(ptile));
  cont->id1 = index_to_map_pos_y(tile_index(ptile));

  pstr = create_utf8_from_char_fonto(_("Advanced Menu"), FONTO_ATTENTION);
  pstr->style |= TTF_STYLE_BOLD;

  pwindow = create_window_skeleton(NULL, pstr, 0);

  pwindow->action = advanced_terrain_window_dlg_callback;
  set_wstate(pwindow , FC_WS_NORMAL);

  add_to_gui_list(ID_TERRAIN_ADV_DLG_WINDOW, pwindow);
  advanced_terrain_dlg->end_widget_list = pwindow;

  area = pwindow->area;

  /* ---------- */
  /* Exit button */
  buf = create_themeicon(current_theme->small_cancel_icon, pwindow->dst,
                         WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  buf->info_label = create_utf8_from_char_fonto(_("Close Dialog (Esc)"),
                                                FONTO_ATTENTION);
  area.w += buf->size.w + adj_size(10);
  buf->action = exit_advanced_terrain_dlg_callback;
  set_wstate(buf, FC_WS_NORMAL);
  buf->key = SDLK_ESCAPE;

  add_to_gui_list(ID_TERRAIN_ADV_DLG_EXIT_BUTTON, buf);
  /* ---------- */

  pstr = create_utf8_from_char_fonto(_("Terrain Info"), FONTO_DEFAULT);
  pstr->style |= TTF_STYLE_BOLD;

  buf = create_iconlabel(NULL, pwindow->dst, pstr,
    (WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE|WF_FREE_DATA));

  buf->string_utf8->bgcol = (SDL_Color) {0, 0, 0, 0};

  buf->data.cont = cont;

  buf->action = terrain_info_callback;
  set_wstate(buf, FC_WS_NORMAL);

  add_to_gui_list(ID_LABEL, buf);

  area.w = MAX(area.w, buf->size.w);
  area.h += buf->size.h;

  /* ---------- */
  if (pcity && city_owner(pcity) == client.conn.playing) {
    /* Separator */
    buf = create_iconlabel(NULL, pwindow->dst, NULL, WF_FREE_THEME);

    add_to_gui_list(ID_SEPARATOR, buf);
    area.h += buf->next->size.h;
    /* ------------------ */

    fc_snprintf(cbuf, sizeof(cbuf), _("Zoom to : %s"), city_name_get(pcity));

    create_active_iconlabel(buf, pwindow->dst,
                            pstr, cbuf, zoom_to_city_callback);
    buf->data.city = pcity;
    set_wstate(buf, FC_WS_NORMAL);

    add_to_gui_list(ID_LABEL, buf);

    area.w = MAX(area.w, buf->size.w);
    area.h += buf->size.h;
    /* ----------- */

    create_active_iconlabel(buf, pwindow->dst, pstr,
                            _("Change Production"), change_production_callback);

    buf->data.city = pcity;
    set_wstate(buf, FC_WS_NORMAL);

    add_to_gui_list(ID_LABEL, buf);

    area.w = MAX(area.w, buf->size.w);
    area.h += buf->size.h;
    /* -------------- */

    create_active_iconlabel(buf, pwindow->dst, pstr,
                            _("Hurry production"), hurry_production_callback);

    buf->data.city = pcity;
    set_wstate(buf, FC_WS_NORMAL);

    add_to_gui_list(ID_LABEL, buf);

    area.w = MAX(area.w, buf->size.w);
    area.h += buf->size.h;
    /* ----------- */

    create_active_iconlabel(buf, pwindow->dst, pstr,
                            _("Change City Governor settings"), cma_callback);

    buf->data.city = pcity;
    set_wstate(buf, FC_WS_NORMAL);

    add_to_gui_list(ID_LABEL, buf);

    area.w = MAX(area.w, buf->size.w);
    area.h += buf->size.h;
  }
  /* ---------- */

  if (focus_unit
      && (tile_index(unit_tile(focus_unit)) != tile_index(ptile))) {
    /* Separator */
    buf = create_iconlabel(NULL, pwindow->dst, NULL, WF_FREE_THEME);

    add_to_gui_list(ID_SEPARATOR, buf);
    area.h += buf->next->size.h;
    /* ------------------ */

    create_active_iconlabel(buf, pwindow->dst, pstr, _("Goto here"),
                            goto_here_callback);
    buf->data.cont = cont;
    set_wstate(buf, FC_WS_NORMAL);

    add_to_gui_list(MAX_ID - 1000 - focus_unit->id, buf);

    area.w = MAX(area.w, buf->size.w);
    area.h += buf->size.h;
    /* ----------- */

    create_active_iconlabel(buf, pwindow->dst, pstr, _("Patrol here"),
                            patrol_here_callback);
    buf->data.cont = cont;
    set_wstate(buf, FC_WS_NORMAL);

    add_to_gui_list(MAX_ID - 1000 - focus_unit->id, buf);

    area.w = MAX(area.w, buf->size.w);
    area.h += buf->size.h;
    /* ----------- */

#if 0 /* FIXME: specific connect buttons */
    if (unit_has_type_flag(focus_unit, UTYF_WORKERS)) {
      create_active_iconlabel(buf, pwindow->dst->surface, pstr, _("Connect here"),
                              connect_here_callback);
      buf->data.cont = cont;
      set_wstate(buf, FC_WS_NORMAL);

      add_to_gui_list(ID_LABEL, buf);

      area.w = MAX(area.w, buf->size.w);
      area.h += buf->size.h;
    }
#endif /* 0 */

    /* FIXME: This logic seems to try to mirror do_paradrop() why? */
    if (can_unit_paradrop(&(wld.map), focus_unit) && client_tile_get_known(ptile)
        && !(((pcity && pplayers_non_attack(client.conn.playing, city_owner(pcity)))
              || is_non_attack_unit_tile(ptile, client.conn.playing)))
        && (unit_type_get(focus_unit)->paratroopers_range >=
            real_map_distance(unit_tile(focus_unit), ptile))) {

      create_active_iconlabel(buf, pwindow->dst, pstr, _("Paradrop here"),
                              paradrop_here_callback);
      buf->data.cont = cont;
      set_wstate(buf, FC_WS_NORMAL);

      add_to_gui_list(ID_LABEL, buf);

      area.w = MAX(area.w, buf->size.w);
      area.h += buf->size.h;
    }

  }
  advanced_terrain_dlg->begin_widget_list = buf;

  /* ---------- */
  if (n) {
    int i;
    struct unit *punit;
    const struct unit_type *punittype = NULL;

    units_h = 0;

    /* Separator */
    buf = create_iconlabel(NULL, pwindow->dst, NULL, WF_FREE_THEME);

    add_to_gui_list(ID_SEPARATOR, buf);
    area.h += buf->next->size.h;
    /* ---------- */
    if (n > 1) {
      struct unit *defender, *attacker;
      struct widget *last = buf;
      bool reset = FALSE;
      int my_units = 0;
      const char *vetname;

      #define ADV_NUM_SEEN  15

      defender = (focus_unit ? get_defender(&(wld.map), focus_unit, ptile, NULL)
                  : NULL);
      attacker = (focus_unit ? get_attacker(&(wld.map), focus_unit, ptile)
                  : NULL);
      for (i = 0; i < n; i++) {
        punit = unit_list_get(ptile->units, i);
        if (punit == focus_unit) {
          continue;
        }
        punittype = unit_type_get(punit);
        vetname = utype_veteran_name_translation(punittype, punit->veteran);

        if (unit_owner(punit) == client.conn.playing) {
          struct astring addition = ASTRING_INIT;

          unit_activity_astr(punit, &addition);
          fc_snprintf(cbuf, sizeof(cbuf),
                      _("Activate %s (%d / %d) %s (%d,%d,%s) %s"),
                      (vetname != NULL ? vetname : ""),
                      punit->hp, punittype->hp,
                      utype_name_translation(punittype),
                      punittype->attack_strength,
                      punittype->defense_strength,
                      move_points_text(punittype->move_rate, FALSE),
                      astr_str(&addition));
          astr_free(&addition);

          create_active_iconlabel(buf, pwindow->dst, pstr,
                                  cbuf, adv_unit_select_callback);
          buf->data.unit = punit;
          set_wstate(buf, FC_WS_NORMAL);
          add_to_gui_list(ID_LABEL, buf);
          my_units++;
        } else {
          int att_chance, def_chance;

          fc_snprintf(cbuf, sizeof(cbuf), _("%s %s %s (A:%d D:%d M:%s FP:%d) HP:%d%%"),
                      nation_adjective_for_player(unit_owner(punit)),
                      (vetname != NULL ? vetname : ""),
                      utype_name_translation(punittype),
                      punittype->attack_strength,
                      punittype->defense_strength,
                      move_points_text(punittype->move_rate, FALSE),
                      punittype->firepower,
                      ((punit->hp * 100) / punittype->hp));

          /* Calculate chance to win */
          if (sdl_get_chance_to_win(&att_chance, &def_chance, punit,
                                    focus_unit)) {
            /* TRANS: "CtW" = "Chance to Win"; preserve leading space */
            cat_snprintf(cbuf, sizeof(cbuf), _(" CtW: Att:%d%% Def:%d%%"),
                         att_chance, def_chance);
          }

          if (attacker && attacker == punit) {
            pstr->fgcol = *(get_game_color(COLOR_OVERVIEW_ENEMY_UNIT));
            reset = TRUE;
          } else {
            if (defender && defender == punit) {
              pstr->fgcol = *(get_game_color(COLOR_OVERVIEW_MY_UNIT));
              reset = TRUE;
            }
          }

          create_active_iconlabel(buf, pwindow->dst, pstr, cbuf, NULL);

          if (reset) {
            pstr->fgcol = *get_theme_color(COLOR_THEME_ADVANCEDTERRAINDLG_TEXT);
            reset = FALSE;
          }

          add_to_gui_list(ID_LABEL, buf);
        }

        area.w = MAX(area.w, buf->size.w);
        units_h += buf->size.h;

        if (i > ADV_NUM_SEEN - 1) {
          set_wflag(buf, WF_HIDDEN);
        }
      }

      advanced_terrain_dlg->end_active_widget_list = last->prev;
      advanced_terrain_dlg->active_widget_list = advanced_terrain_dlg->end_active_widget_list;
      advanced_terrain_dlg->begin_widget_list = buf;
      advanced_terrain_dlg->begin_active_widget_list = advanced_terrain_dlg->begin_widget_list;

      if (n > ADV_NUM_SEEN) {
        units_h = ADV_NUM_SEEN * buf->size.h;
        n = create_vertical_scrollbar(advanced_terrain_dlg,
                                      1, ADV_NUM_SEEN, TRUE, TRUE);
        area.w += n;
      }

      if (my_units > 1) {
        fc_snprintf(cbuf, sizeof(cbuf), "%s (%d)", _("Ready all"), my_units);
        create_active_iconlabel(buf, pwindow->dst, pstr,
                                cbuf, adv_unit_select_all_callback);
        buf->data.unit = advanced_terrain_dlg->end_active_widget_list->data.unit;
        set_wstate(buf, FC_WS_NORMAL);
        buf->id = ID_LABEL;
        widget_add_as_prev(buf, last);
        area.h += buf->size.h;

        fc_snprintf(cbuf, sizeof(cbuf), "%s (%d)", _("Sentry idle"), my_units);
        create_active_iconlabel(buf, pwindow->dst, pstr,
                                cbuf, adv_unit_sentry_idle_callback);
        buf->data.unit = advanced_terrain_dlg->end_active_widget_list->data.unit;
        set_wstate(buf, FC_WS_NORMAL);
        buf->id = ID_LABEL;
        widget_add_as_prev(buf, last->prev);
        area.h += buf->size.h;

        /* separator */
        buf = create_iconlabel(NULL, pwindow->dst, NULL, WF_FREE_THEME);
        buf->id = ID_SEPARATOR;
        widget_add_as_prev(buf, last->prev->prev);
        area.h += buf->next->size.h;
      }
#undef ADV_NUM_SEEN
    } else { /* n == 1 */
      /* one unit - give orders */
      punit = unit_list_get(ptile->units, 0);
      punittype = unit_type_get(punit);
      if (punit != focus_unit) {
        const char *vetname;

        vetname = utype_veteran_name_translation(punittype, punit->veteran);
        if ((pcity && city_owner(pcity) == client.conn.playing)
            || (unit_owner(punit) == client.conn.playing)) {
          struct astring addition = ASTRING_INIT;

          unit_activity_astr(punit, &addition);
          fc_snprintf(cbuf, sizeof(cbuf),
                      _("Activate %s (%d / %d) %s (%d,%d,%s) %s"),
                      (vetname != NULL ? vetname : ""),
                      punit->hp, punittype->hp,
                      utype_name_translation(punittype),
                      punittype->attack_strength,
                      punittype->defense_strength,
                      move_points_text(punittype->move_rate, FALSE),
                      astr_str(&addition));
          astr_free(&addition);

          create_active_iconlabel(buf, pwindow->dst, pstr,
                                  cbuf, adv_unit_select_callback);
          buf->data.unit = punit;
          set_wstate(buf, FC_WS_NORMAL);

          add_to_gui_list(ID_LABEL, buf);

          area.w = MAX(area.w, buf->size.w);
          units_h += buf->size.h;
          /* ---------------- */
          /* Separator */
          buf = create_iconlabel(NULL, pwindow->dst, NULL, WF_FREE_THEME);

          add_to_gui_list(ID_SEPARATOR, buf);
          area.h += buf->next->size.h;
        } else {
          int att_chance, def_chance;

          fc_snprintf(cbuf, sizeof(cbuf), _("%s %s %s (A:%d D:%d M:%s FP:%d) HP:%d%%"),
                      nation_adjective_for_player(unit_owner(punit)),
                      (vetname != NULL ? vetname : ""),
                      utype_name_translation(punittype),
                      punittype->attack_strength,
                      punittype->defense_strength,
                      move_points_text(punittype->move_rate, FALSE),
                      punittype->firepower,
                      ((punit->hp * 100) / punittype->hp));

          /* Calculate chance to win */
          if (sdl_get_chance_to_win(&att_chance, &def_chance, punit,
                                    focus_unit)) {
            /* TRANS: "CtW" = "Chance to Win"; preserve leading space */
            cat_snprintf(cbuf, sizeof(cbuf), _(" CtW: Att:%d%% Def:%d%%"),
                         att_chance, def_chance);
          }
          create_active_iconlabel(buf, pwindow->dst, pstr, cbuf, NULL);
          add_to_gui_list(ID_LABEL, buf);
          area.w = MAX(area.w, buf->size.w);
          units_h += buf->size.h;
          /* ---------------- */

          /* Separator */
          buf = create_iconlabel(NULL, pwindow->dst, NULL, WF_FREE_THEME);

          add_to_gui_list(ID_SEPARATOR, buf);
          area.h += buf->next->size.h;
        }
      }

      /* ---------------- */
      fc_snprintf(cbuf, sizeof(cbuf),
            _("Look up \"%s\" in the Help Browser"),
            utype_name_translation(punittype));
      create_active_iconlabel(buf, pwindow->dst, pstr,
                              cbuf, unit_help_callback);
      set_wstate(buf , FC_WS_NORMAL);
      add_to_gui_list(MAX_ID - utype_number(punittype), buf);

      area.w = MAX(area.w, buf->size.w);
      units_h += buf->size.h;
      /* ---------------- */
      advanced_terrain_dlg->begin_widget_list = buf;
    }

  }
  /* ---------- */

  area.w += adj_size(2);
  area.h += units_h;

  resize_window(pwindow, NULL, NULL,
                (pwindow->size.w - pwindow->area.w) + area.w,
                (pwindow->size.h - pwindow->area.h) + area.h);

  area = pwindow->area;

  widget_set_position(pwindow, pos_x, pos_y);

  w = area.w - adj_size(2);

  if (advanced_terrain_dlg->scroll) {
    units_h = n;
  } else {
    units_h = 0;
  }

  /* Exit button */
  buf = pwindow->prev;

  buf->size.x = area.x + area.w - buf->size.w - 1;
  buf->size.y = pwindow->size.y + adj_size(2);

  /* Terrain info */
  buf = buf->prev;

  buf->size.x = area.x + 1;
  buf->size.y = area.y + 1;
  buf->size.w = w;
  h = buf->size.h;

  area2.x = adj_size(10);
  area2.h = adj_size(2);

  buf = buf->prev;
  while (buf) {
    if (buf == advanced_terrain_dlg->end_active_widget_list) {
      w -= units_h;
    }

    buf->size.w = w;
    buf->size.x = buf->next->size.x;
    buf->size.y = buf->next->size.y + buf->next->size.h;

    if (buf->id == ID_SEPARATOR) {
      FREESURFACE(buf->theme);
      buf->size.h = h;
      buf->theme = create_surf(w, h, SDL_SWSURFACE);

      area2.y = buf->size.h / 2 - 1;
      area2.w = buf->size.w - adj_size(20);

      SDL_FillRect(buf->theme, &area2, map_rgba(buf->theme->format,
                                      *get_theme_color(COLOR_THEME_ADVANCEDTERRAINDLG_TEXT)));
    }

    if (buf == advanced_terrain_dlg->begin_widget_list
        || buf == advanced_terrain_dlg->begin_active_widget_list) {
      break;
    }
    buf = buf->prev;
  }

  if (advanced_terrain_dlg->scroll) {
    setup_vertical_scrollbar_area(advanced_terrain_dlg->scroll,
        area.x + area.w,
        advanced_terrain_dlg->end_active_widget_list->size.y,
        area.y - advanced_terrain_dlg->end_active_widget_list->size.y + area.h,
        TRUE);
  }

  /* -------------------- */
  /* Redraw */
  redraw_group(advanced_terrain_dlg->begin_widget_list, pwindow, 0);

  widget_flush(pwindow);
}

/* ====================================================================== */
/* ============================ PILLAGE DIALOG ========================== */
/* ====================================================================== */
static struct small_dialog *pillage_dlg = NULL;

/**********************************************************************//**
  User interacted with pillage dialog.
**************************************************************************/
static int pillage_window_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    move_window_group(pillage_dlg->begin_widget_list, pwindow);
  }

  return -1;
}

/**********************************************************************//**
  User selected what to pillage.
**************************************************************************/
static int pillage_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct unit *punit = pwidget->data.unit;
    int what = MAX_ID - pwidget->id;

    popdown_pillage_dialog();

    if (punit) {
      struct extra_type *target = extra_by_number(what);

      request_new_unit_activity_targeted(punit, ACTIVITY_PILLAGE, target);
    }
  }

  return -1;
}

/**********************************************************************//**
  User requested closing of pillage dialog.
**************************************************************************/
static int exit_pillage_dlg_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popdown_pillage_dialog();
  }

  return -1;
}

/**********************************************************************//**
  Popdown a dialog asking the unit which improvement they would like to
  pillage.
**************************************************************************/
static void popdown_pillage_dialog(void)
{
  if (pillage_dlg) {
    is_unit_move_blocked = FALSE;
    popdown_window_group_dialog(pillage_dlg->begin_widget_list,
                                pillage_dlg->end_widget_list);
    FC_FREE(pillage_dlg);
    flush_dirty();
  }
}

/**********************************************************************//**
  Popup a dialog asking the unit which improvement they would like to
  pillage.
**************************************************************************/
void popup_pillage_dialog(struct unit *punit, bv_extras extras)
{
  struct widget *pwindow = NULL, *buf = NULL;
  utf8_str *pstr;
  SDL_Rect area;
  struct extra_type *tgt;

  if (pillage_dlg) {
    return;
  }

  is_unit_move_blocked = TRUE;
  pillage_dlg = fc_calloc(1, sizeof(struct small_dialog));

  /* Window */
  pstr = create_utf8_from_char_fonto(_("What To Pillage"), FONTO_ATTENTION);
  pstr->style |= TTF_STYLE_BOLD;

  pwindow = create_window_skeleton(NULL, pstr, 0);

  pwindow->action = pillage_window_callback;
  set_wstate(pwindow, FC_WS_NORMAL);

  add_to_gui_list(ID_PILLAGE_DLG_WINDOW, pwindow);
  pillage_dlg->end_widget_list = pwindow;

  area = pwindow->area;

  area.h = MAX(area.h, adj_size(2));

  /* ---------- */
  /* Exit button */
  buf = create_themeicon(current_theme->small_cancel_icon, pwindow->dst,
                         WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
  buf->info_label = create_utf8_from_char_fonto(_("Close Dialog (Esc)"),
                                                FONTO_ATTENTION);
  area.w += buf->size.w + adj_size(10);
  buf->action = exit_pillage_dlg_callback;
  set_wstate(buf, FC_WS_NORMAL);
  buf->key = SDLK_ESCAPE;

  add_to_gui_list(ID_PILLAGE_DLG_EXIT_BUTTON, buf);
  /* ---------- */

  while ((tgt = get_preferred_pillage(extras))) {
    const char *name = NULL;
    int what;

    BV_CLR(extras, extra_index(tgt));
    name = extra_name_translation(tgt);
    what = extra_index(tgt);

    fc_assert(name != NULL);

    create_active_iconlabel(buf, pwindow->dst, pstr,
                            (char *) name, pillage_callback);

    buf->data.unit = punit;
    set_wstate(buf, FC_WS_NORMAL);

    add_to_gui_list(MAX_ID - what, buf);

    area.w = MAX(area.w, buf->size.w);
    area.h += buf->size.h;
  }
  pillage_dlg->begin_widget_list = buf;

  /* setup window size and start position */

  resize_window(pwindow, NULL, NULL,
                (pwindow->size.w - pwindow->area.w) + area.w,
                (pwindow->size.h - pwindow->area.h) + area.h);

  area = pwindow->area;

  put_window_near_map_tile(pwindow, pwindow->size.w, pwindow->size.h,
                           unit_tile(punit));

  /* Setup widget size and start position */

  /* Exit button */
  buf = pwindow->prev;
  buf->size.x = area.x + area.w - buf->size.w - 1;
  buf->size.y = pwindow->size.y + adj_size(2);

  /* First special to pillage */
  buf = buf->prev;
  setup_vertical_widgets_position(1,
                                  area.x, area.y + 1, area.w, 0,
                                  pillage_dlg->begin_widget_list, buf);

  /* --------------------- */
  /* Redraw */
  redraw_group(pillage_dlg->begin_widget_list, pwindow, 0);

  widget_flush(pwindow);
}

/* ======================================================================= */
/* =========================== CONNECT DIALOG ============================ */
/* ======================================================================= */
static struct small_dialog *connect_dlg = NULL;

/**********************************************************************//**
  Popdown a dialog asking the unit how they want to "connect" their
  location to the destination.
**************************************************************************/
static void popdown_connect_dialog(void)
{
  if (connect_dlg) {
    is_unit_move_blocked = FALSE;
    popdown_window_group_dialog(connect_dlg->begin_widget_list,
                                connect_dlg->end_widget_list);
    FC_FREE(connect_dlg);
  }
}

/* ==================== Public ========================= */

/**************************************************************************
                           Select Government Type
**************************************************************************/
static struct small_dialog *gov_dlg = NULL;

/**********************************************************************//**
  Close the government dialog.
**************************************************************************/
static void popdown_government_dialog(void)
{
  if (gov_dlg) {
    popdown_window_group_dialog(gov_dlg->begin_widget_list,
                                gov_dlg->end_widget_list);
    FC_FREE(gov_dlg);
    enable_and_redraw_revolution_button();
  }
}

/**********************************************************************//**
  User selected government button.
**************************************************************************/
static int government_dlg_callback(struct widget *gov_button)
{
  if (PRESSED_EVENT(main_data.event)) {
    set_government_choice(government_by_number(MAX_ID - gov_button->id));

    popdown_government_dialog();
  }

  return -1;
}

/**********************************************************************//**
  User requested move of government dialog.
**************************************************************************/
static int move_government_dlg_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    move_window_group(gov_dlg->begin_widget_list, pwindow);
  }

  return -1;
}

/**********************************************************************//**
  Popup a dialog asking the player what government to switch to.
**************************************************************************/
void popup_government_dialog(void)
{
  struct utf8_str *pstr;
  struct widget *gov_button = NULL;
  struct widget *pwindow;
  int j;
  Uint16 max_w = 0, max_h = 0;
  SDL_Rect area;

  if (gov_dlg != NULL) {
    return;
  }

  gov_dlg = fc_calloc(1, sizeof(struct small_dialog));

  /* Create window */
  pstr = create_utf8_from_char_fonto(_("Choose Your New Government"),
                                     FONTO_ATTENTION);
  pstr->style |= TTF_STYLE_BOLD;
  /* This win. size is temp. */
  pwindow = create_window_skeleton(NULL, pstr, 0);
  pwindow->action = move_government_dlg_callback;
  add_to_gui_list(ID_GOVERNMENT_DLG_WINDOW, pwindow);

  gov_dlg->end_widget_list = pwindow;

  area = pwindow->area;

  /* Create gov. buttons */
  j = 0;
  governments_iterate(gov) {
    if (gov == game.government_during_revolution) {
      continue;
    }

    if (can_change_to_government(client.conn.playing, gov)) {
      pstr = create_utf8_from_char_fonto(government_name_translation(gov),
                                         FONTO_ATTENTION);
      gov_button
        = create_icon_button(get_government_surface(gov), pwindow->dst, pstr, 0);
      gov_button->action = government_dlg_callback;

      max_w = MAX(max_w, gov_button->size.w);
      max_h = MAX(max_h, gov_button->size.h);

      /* Ugly hack */
      add_to_gui_list((MAX_ID - government_number(gov)), gov_button);
      j++;

    }
  } governments_iterate_end;

  if (gov_button == NULL) {
    /* No governments to switch to.
     * TODO: Provide close button for the dialog. */
    gov_dlg->begin_widget_list = gov_dlg->end_widget_list;
  } else {
    SDL_Surface *logo;

    gov_dlg->begin_widget_list = gov_button;

    max_w += adj_size(10);
    max_h += adj_size(4);

    area.w = MAX(area.w, max_w + adj_size(20));
    area.h = MAX(area.h, j * (max_h + adj_size(10)) + adj_size(5));

    /* Create window background */
    logo = theme_get_background(active_theme, BACKGROUND_CHOOSEGOVERNMENTDLG);
    if (resize_window(pwindow, logo, NULL,
                      (pwindow->size.w - pwindow->area.w) + area.w,
                      (pwindow->size.h - pwindow->area.h) + area.h)) {
      FREESURFACE(logo);
    }

    area = pwindow->area;

    /* Set window start positions */
    widget_set_position(pwindow,
                        (main_window_width() - pwindow->size.w) / 2,
                        (main_window_height() - pwindow->size.h) / 2);

    /* Set buttons start positions and size */
    j = 1;
    while (gov_button != gov_dlg->end_widget_list) {
      gov_button->size.w = max_w;
      gov_button->size.h = max_h;
      gov_button->size.x = area.x + adj_size(10);
      gov_button->size.y = area.y + area.h - (j++) * (max_h + adj_size(10));
      set_wstate(gov_button, FC_WS_NORMAL);

      gov_button = gov_button->next;
    }
  }

  set_wstate(pwindow, FC_WS_NORMAL);

  /* Redraw */
  redraw_group(gov_dlg->begin_widget_list, pwindow, 0);

  widget_flush(pwindow);
}

/**************************************************************************
                                Nation Wizard
**************************************************************************/
static struct advanced_dialog *nation_dlg = NULL;
static struct small_dialog *help_dlg = NULL;

struct nation_info {
  unsigned char nation_style;      /* selected style */
  unsigned char selected_leader;   /* if not unique -> selected leader */
  Nation_type_id nation;           /* selected nation */
  bool leader_sex;                 /* selected leader sex */
  struct nation_set *set;
  struct widget *change_sex;
  struct widget *name_edit;
  struct widget *name_next;
  struct widget *name_prev;
  struct widget *pset_name;
  struct widget *pset_next;
  struct widget *pset_prev;
};

static int next_set_callback(struct widget *next_button);
static int prev_set_callback(struct widget *prev_button);
static int nations_dialog_callback(struct widget *pwindow);
static int nation_button_callback(struct widget *pnation);
static int races_dialog_ok_callback(struct widget *start_button);
static int races_dialog_cancel_callback(struct widget *button);
static int next_name_callback(struct widget *next_button);
static int prev_name_callback(struct widget *prev_button);
static int change_sex_callback(struct widget *sex);
static void select_random_leader(Nation_type_id nation);
static void change_nation_label(void);

/**********************************************************************//**
  User interacted with nations dialog.
**************************************************************************/
static int nations_dialog_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (select_window_group_dialog(nation_dlg->begin_widget_list, pwindow)) {
      widget_flush(pwindow);
    }
  }

  return -1;
}

/**********************************************************************//**
  User accepted nation.
**************************************************************************/
static int races_dialog_ok_callback(struct widget *start_button)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct nation_info *setup = (struct nation_info *)(nation_dlg->end_widget_list->data.ptr);
    char *pstr = setup->name_edit->string_utf8->text;

    /* perform a minimum of sanity test on the name */
    if (strlen(pstr) == 0) {
      output_window_append(ftc_client, _("You must type a legal name."));
      selected_widget = start_button;
      set_wstate(start_button, FC_WS_SELECTED);
      widget_redraw(start_button);
      widget_flush(start_button);

      return -1;
    }

    dsend_packet_nation_select_req(&client.conn, player_number(races_player),
                                   setup->nation,
                                   setup->leader_sex, pstr,
                                   setup->nation_style);

    popdown_races_dialog();
    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  User requested leader gender change.
**************************************************************************/
static int change_sex_callback(struct widget *sex)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct nation_info *setup = (struct nation_info *)(nation_dlg->end_widget_list->data.ptr);

    if (setup->leader_sex) {
      copy_chars_to_utf8_str(setup->change_sex->string_utf8,
                             sex_name_translation(SEX_FEMALE));
    } else {
      copy_chars_to_utf8_str(setup->change_sex->string_utf8,
                             sex_name_translation(SEX_MALE));
    }
    setup->leader_sex = !setup->leader_sex;

    if (sex) {
      selected_widget = sex;
      set_wstate(sex, FC_WS_SELECTED);

      widget_redraw(sex);
      widget_flush(sex);
    }
  }

  return -1;
}

/**********************************************************************//**
  User requested next leader name.
**************************************************************************/
static int next_name_callback(struct widget *next)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct nation_info *setup = (struct nation_info *)(nation_dlg->end_widget_list->data.ptr);
    const struct nation_leader_list *leaders =
        nation_leaders(nation_by_number(setup->nation));
    const struct nation_leader *pleader;

    setup->selected_leader++;
    pleader = nation_leader_list_get(leaders, setup->selected_leader);

    /* change leader sex */
    if (setup->leader_sex != nation_leader_is_male(pleader)) {
      change_sex_callback(NULL);
    }

    /* change leader name */
    copy_chars_to_utf8_str(setup->name_edit->string_utf8,
                           nation_leader_name(pleader));

    FC_FREE(leader_name);
    leader_name = fc_strdup(nation_leader_name(pleader));

    if (nation_leader_list_size(leaders) - 1 == setup->selected_leader) {
      set_wstate(setup->name_next, FC_WS_DISABLED);
    }

    if (get_wstate(setup->name_prev) == FC_WS_DISABLED) {
      set_wstate(setup->name_prev, FC_WS_NORMAL);
    }

    if (!(get_wstate(setup->name_next) == FC_WS_DISABLED)) {
      selected_widget = setup->name_next;
      set_wstate(setup->name_next, FC_WS_SELECTED);
    }

    widget_redraw(setup->name_edit);
    widget_redraw(setup->name_prev);
    widget_redraw(setup->name_next);
    widget_mark_dirty(setup->name_edit);
    widget_mark_dirty(setup->name_prev);
    widget_mark_dirty(setup->name_next);

    widget_redraw(setup->change_sex);
    widget_mark_dirty(setup->change_sex);

    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  User requested previous leader name.
**************************************************************************/
static int prev_name_callback(struct widget *prev)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct nation_info *setup = (struct nation_info *)(nation_dlg->end_widget_list->data.ptr);
    const struct nation_leader_list *leaders =
        nation_leaders(nation_by_number(setup->nation));
    const struct nation_leader *pleader;

    setup->selected_leader--;
    pleader = nation_leader_list_get(leaders, setup->selected_leader);

    /* change leader sex */
    if (setup->leader_sex != nation_leader_is_male(pleader)) {
      change_sex_callback(NULL);
    }

    /* change leader name */
    copy_chars_to_utf8_str(setup->name_edit->string_utf8,
                           nation_leader_name(pleader));

    FC_FREE(leader_name);
    leader_name = fc_strdup(nation_leader_name(pleader));

    if (!setup->selected_leader) {
      set_wstate(setup->name_prev, FC_WS_DISABLED);
    }

    if (get_wstate(setup->name_next) == FC_WS_DISABLED) {
      set_wstate(setup->name_next, FC_WS_NORMAL);
    }

    if (!(get_wstate(setup->name_prev) == FC_WS_DISABLED)) {
      selected_widget = setup->name_prev;
      set_wstate(setup->name_prev, FC_WS_SELECTED);
    }

    widget_redraw(setup->name_edit);
    widget_redraw(setup->name_prev);
    widget_redraw(setup->name_next);
    widget_mark_dirty(setup->name_edit);
    widget_mark_dirty(setup->name_prev);
    widget_mark_dirty(setup->name_next);

    widget_redraw(setup->change_sex);
    widget_mark_dirty(setup->change_sex);

    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  User requested next nationset
**************************************************************************/
static int next_set_callback(struct widget *next_button)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct nation_info *setup = (struct nation_info *)(nation_dlg->end_widget_list->data.ptr);
    struct option *poption = optset_option_by_name(server_optset, "nationset");

    fc_assert(setup->set != NULL
              && nation_set_index(setup->set) < nation_set_count() - 1);

    setup->set = nation_set_by_number(nation_set_index(setup->set) + 1);

    option_str_set(poption, nation_set_rule_name(setup->set));
  }

  return -1;
}

/**********************************************************************//**
  User requested prev nationset
**************************************************************************/
static int prev_set_callback(struct widget *prev_button)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct nation_info *setup = (struct nation_info *)(nation_dlg->end_widget_list->data.ptr);
    struct option *poption = optset_option_by_name(server_optset, "nationset");

    fc_assert(setup->set != NULL && nation_set_index(setup->set) > 0);

    setup->set = nation_set_by_number(nation_set_index(setup->set) - 1);

    option_str_set(poption, nation_set_rule_name(setup->set));
  }

  return -1;
}

/**********************************************************************//**
  User cancelled nations dialog.
**************************************************************************/
static int races_dialog_cancel_callback(struct widget *button)
{
  if (PRESSED_EVENT(main_data.event)) {
    popdown_races_dialog();
    flush_dirty();
  }

  return -1;
}

/**********************************************************************//**
  User interacted with style widget.
**************************************************************************/
static int style_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct nation_info *setup = (struct nation_info *)(nation_dlg->end_widget_list->data.ptr);
    struct widget *gui = get_widget_pointer_from_main_list(MAX_ID - 1000 -
                                                           setup->nation_style);

    set_wstate(gui, FC_WS_NORMAL);
    widget_redraw(gui);
    widget_mark_dirty(gui);

    set_wstate(pwidget, FC_WS_DISABLED);
    widget_redraw(pwidget);
    widget_mark_dirty(pwidget);

    setup->nation_style = MAX_ID - 1000 - pwidget->id;

    flush_dirty();
    selected_widget = NULL;
  }

  return -1;
}

/**********************************************************************//**
  User interacted with help dialog.
**************************************************************************/
static int help_dlg_callback(struct widget *pwindow)
{
  return -1;
}

/**********************************************************************//**
  User requested closing of help dialog.
**************************************************************************/
static int cancel_help_dlg_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (help_dlg) {
      popdown_window_group_dialog(help_dlg->begin_widget_list,
                                  help_dlg->end_widget_list);
      FC_FREE(help_dlg);
      if (pwidget) {
        flush_dirty();
      }
    }
  }

  return -1;
}

/**********************************************************************//**
  User selected nation.
**************************************************************************/
static int nation_button_callback(struct widget *nation_button)
{
  set_wstate(nation_button, FC_WS_SELECTED);
  selected_widget = nation_button;

  if (PRESSED_EVENT(main_data.event)) {
    struct nation_info *setup = (struct nation_info *)(nation_dlg->end_widget_list->data.ptr);

    if (setup->nation == MAX_ID - nation_button->id) {
      widget_redraw(nation_button);
      widget_flush(nation_button);

      return -1;
    }

    setup->nation = MAX_ID - nation_button->id;

    change_nation_label();

    enable(MAX_ID - 1000 - setup->nation_style);
    setup->nation_style = style_number(style_of_nation(nation_by_number(setup->nation)));
    disable(MAX_ID - 1000 - setup->nation_style);

    select_random_leader(setup->nation);

    redraw_group(nation_dlg->begin_widget_list, nation_dlg->end_widget_list, 0);
    widget_flush(nation_dlg->end_widget_list);
  } else {
    /* Pop up nation description */
    struct widget *pwindow, *ok_button;
    utf8_str *pstr;
    SDL_Surface *text;
    SDL_Rect area, area2;
    struct nation_type *pnation = nation_by_number(MAX_ID - nation_button->id);

    widget_redraw(nation_button);
    widget_mark_dirty(nation_button);

    if (!help_dlg) {
      help_dlg = fc_calloc(1, sizeof(struct small_dialog));

      pstr = create_utf8_from_char_fonto(nation_plural_translation(pnation),
                                         FONTO_ATTENTION);
      pstr->style |= TTF_STYLE_BOLD;

      pwindow = create_window_skeleton(NULL, pstr, 0);
      pwindow->action = help_dlg_callback;

      set_wstate(pwindow, FC_WS_NORMAL);

      help_dlg->end_widget_list = pwindow;
      add_to_gui_list(ID_WINDOW, pwindow);

      ok_button
        = create_themeicon_button_from_chars_fonto(current_theme->ok_icon,
                                                   pwindow->dst, _("OK"),
                                                   FONTO_HEADING, 0);
      ok_button->action = cancel_help_dlg_callback;
      set_wstate(ok_button, FC_WS_NORMAL);
      ok_button->key = SDLK_ESCAPE;
      add_to_gui_list(ID_BUTTON, ok_button);
      help_dlg->begin_widget_list = ok_button;
    } else {
      pwindow = help_dlg->end_widget_list;
      ok_button = help_dlg->begin_widget_list;

      /* Undraw window */
      widget_undraw(pwindow);
      widget_mark_dirty(pwindow);
    }

    area = pwindow->area;

    {
      char info[4096];

      helptext_nation(info, sizeof(info), pnation, NULL);
      pstr = create_utf8_from_char_fonto(info, FONTO_ATTENTION);
    }

    pstr->fgcol = *get_theme_color(COLOR_THEME_NATIONDLG_LEGEND);
    text = create_text_surf_smaller_than_w(pstr, main_window_width() - adj_size(20));

    FREEUTF8STR(pstr);

    /* create window background */
    area.w = MAX(area.w, text->w + adj_size(20));
    area.w = MAX(area.w, ok_button->size.w + adj_size(20));
    area.h = MAX(area.h, adj_size(9) + text->h
                         + adj_size(10) + ok_button->size.h + adj_size(10));

    resize_window(pwindow, NULL, get_theme_color(COLOR_THEME_BACKGROUND),
                  (pwindow->size.w - pwindow->area.w) + area.w,
                  (pwindow->size.h - pwindow->area.h) + area.h);

    widget_set_position(pwindow,
                        (main_window_width() - pwindow->size.w) / 2,
                        (main_window_height() - pwindow->size.h) / 2);

    area2.x = area.x + adj_size(7);
    area2.y = area.y + adj_size(6);
    alphablit(text, NULL, pwindow->theme, &area2, 255);
    FREESURFACE(text);

    ok_button->size.x = area.x + (area.w - ok_button->size.w) / 2;
    ok_button->size.y = area.y + area.h - ok_button->size.h - adj_size(10);

    /* redraw */
    redraw_group(ok_button, pwindow, 0);

    widget_mark_dirty(pwindow);

    flush_dirty();

  }

  return -1;
}

/**********************************************************************//**
  User interacted with leader name edit widget.
**************************************************************************/
static int leader_name_edit_callback(struct widget *pedit)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (pedit->string_utf8->text != NULL) {
      /* empty input -> restore previous content */
      copy_chars_to_utf8_str(pedit->string_utf8, leader_name);
      widget_redraw(pedit);
      widget_mark_dirty(pedit);
      flush_dirty();
    }
  }

  return -1;
}

/* =========================================================== */

/**********************************************************************//**
  Update nation label.
**************************************************************************/
static void change_nation_label(void)
{
  SDL_Surface *tmp_surf, *tmp_surf_zoomed;
  struct widget *pwindow = nation_dlg->end_widget_list;
  struct nation_info *setup = (struct nation_info *)(pwindow->data.ptr);
  struct widget *label = setup->name_edit->next;
  struct nation_type *pnation = nation_by_number(setup->nation);

  tmp_surf = get_nation_flag_surface(pnation);
  tmp_surf_zoomed = zoomSurface(tmp_surf, DEFAULT_ZOOM * 1.0, DEFAULT_ZOOM * 1.0, 1);

  FREESURFACE(label->theme);
  label->theme = tmp_surf_zoomed;

  copy_chars_to_utf8_str(label->string_utf8, nation_plural_translation(pnation));

  remake_label_size(label);

  label->size.x = pwindow->size.x + pwindow->size.w / 2 +
    (pwindow->size.w / 2 - label->size.w) / 2;

}

/**********************************************************************//**
  Selects a leader and the appropriate sex. Updates the gui elements
  and the selected_* variables.
**************************************************************************/
static void select_random_leader(Nation_type_id nation)
{
  struct nation_info *setup = (struct nation_info *)(nation_dlg->end_widget_list->data.ptr);
  const struct nation_leader_list *leaders =
      nation_leaders(nation_by_number(setup->nation));
  const struct nation_leader *pleader;

  setup->selected_leader = fc_rand(nation_leader_list_size(leaders));
  pleader = nation_leader_list_get(leaders, setup->selected_leader);
  copy_chars_to_utf8_str(setup->name_edit->string_utf8,
                         nation_leader_name(pleader));

  FC_FREE(leader_name);
  leader_name = fc_strdup(nation_leader_name(pleader));

  /* initialize leader sex */
  setup->leader_sex = nation_leader_is_male(pleader);

  if (setup->leader_sex) {
    copy_chars_to_utf8_str(setup->change_sex->string_utf8,
                           sex_name_translation(SEX_MALE));
  } else {
    copy_chars_to_utf8_str(setup->change_sex->string_utf8,
                           sex_name_translation(SEX_FEMALE));
  }

  /* disable navigation buttons */
  set_wstate(setup->name_prev, FC_WS_DISABLED);
  set_wstate(setup->name_next, FC_WS_DISABLED);

  if (1 < nation_leader_list_size(leaders)) {
    /* if selected leader is not the first leader, enable "previous leader" button */
    if (setup->selected_leader > 0) {
      set_wstate(setup->name_prev, FC_WS_NORMAL);
    }

    /* if selected leader is not the last leader, enable "next leader" button */
    if (setup->selected_leader < (nation_leader_list_size(leaders) - 1)) {
      set_wstate(setup->name_next, FC_WS_NORMAL);
    }
  }
}

/**********************************************************************//**
   Count available playable nations.
**************************************************************************/
static int get_playable_nation_count(void)
{
  int playable_nation_count = 0;

  nations_iterate(pnation) {
    if (pnation->is_playable && !pnation->player
        && is_nation_pickable(pnation))
      playable_nation_count++;
  } nations_iterate_end;

  return playable_nation_count;
}

/**********************************************************************//**
  Popup the nation selection dialog.
**************************************************************************/
void popup_races_dialog(struct player *pplayer)
{
  SDL_Color bg_color = {255,255,255,128};

  struct widget *pwindow, *pwidget = NULL, *buf, *last_City_Style;
  utf8_str *pstr;
  int len = 0;
  int w = adj_size(10), h = adj_size(10);
  SDL_Surface *tmp_surf, *tmp_surf_zoomed = NULL;
  SDL_Surface *main_bg, *text_name;
  SDL_Rect dst;
  float zoom;
  struct nation_info *setup;
  SDL_Rect area;
  int i;
  struct nation_type *pnat;
  struct widget *nationsets = NULL;
  int natinfo_y, natinfo_h;

#define TARGETS_ROW 5
#define TARGETS_COL 1

  if (nation_dlg) {
    return;
  }

  races_player = pplayer;

  nation_dlg = fc_calloc(1, sizeof(struct advanced_dialog));

  /* Create window widget */
  pstr = create_utf8_from_char_fonto(_("What nation will you be?"),
                                     FONTO_ATTENTION);
  pstr->style |= TTF_STYLE_BOLD;

  pwindow = create_window(NULL, pstr, w, h, WF_FREE_DATA);
  pwindow->action = nations_dialog_callback;
  set_wstate(pwindow, FC_WS_NORMAL);
  setup = fc_calloc(1, sizeof(struct nation_info));
  pwindow->data.ptr = (void *)setup;

  nation_dlg->end_widget_list = pwindow;
  add_to_gui_list(ID_NATION_WIZARD_WINDOW, pwindow);
  /* --------------------------------------------------------- */
  /* Create nations list */

  /* Create Imprv Background Icon */
  main_bg = create_surf(adj_size(96*2), adj_size(64), SDL_SWSURFACE);

  SDL_FillRect(main_bg, NULL, map_rgba(main_bg->format, bg_color));

  create_frame(main_bg,
               0, 0, main_bg->w - 1, main_bg->h - 1,
               get_theme_color(COLOR_THEME_NATIONDLG_FRAME));

  pstr = create_utf8_str_fonto(NULL, 0, FONTO_ATTENTION);
  pstr->style |= (SF_CENTER|TTF_STYLE_BOLD);
  pstr->bgcol = (SDL_Color) {0, 0, 0, 0};

  /* Fill list */

  nations_iterate(pnation) {
    if (!is_nation_playable(pnation) || !is_nation_pickable(pnation)) {
      continue;
    }

    tmp_surf_zoomed = adj_surf(get_nation_flag_surface(pnation));

    tmp_surf = crop_rect_from_surface(main_bg, NULL);

    copy_chars_to_utf8_str(pstr, nation_plural_translation(pnation));
    change_fonto_utf8(pstr, FONTO_ATTENTION);
    text_name
      = create_text_surf_smaller_than_w(pstr, tmp_surf->w - adj_size(4));

    dst.x = (tmp_surf->w - tmp_surf_zoomed->w) / 2;
    len = tmp_surf_zoomed->h +
      adj_size(10) + text_name->h;
    dst.y = (tmp_surf->h - len) / 2;
    alphablit(tmp_surf_zoomed, NULL, tmp_surf, &dst, 255);
    dst.y += (tmp_surf_zoomed->h + adj_size(10));
    FREESURFACE(tmp_surf_zoomed);

    dst.x = (tmp_surf->w - text_name->w) / 2;
    alphablit(text_name, NULL, tmp_surf, &dst, 255);
    dst.y += text_name->h;
    FREESURFACE(text_name);

    pwidget = create_icon2(tmp_surf, pwindow->dst,
                           (WF_RESTORE_BACKGROUND|WF_FREE_THEME));

    set_wstate(pwidget, FC_WS_NORMAL);

    pwidget->action = nation_button_callback;

    w = MAX(w, pwidget->size.w);
    h = MAX(h, pwidget->size.h);

    add_to_gui_list(MAX_ID - nation_index(pnation), pwidget);

    if (nation_index(pnation) > (TARGETS_ROW * TARGETS_COL - 1)) {
      set_wflag(pwidget, WF_HIDDEN);
    }

  } nations_iterate_end;

  FREESURFACE(main_bg);

  nation_dlg->end_active_widget_list = pwindow->prev;
  nation_dlg->begin_widget_list = pwidget;
  nation_dlg->begin_active_widget_list = nation_dlg->begin_widget_list;

  if (get_playable_nation_count() > TARGETS_ROW * TARGETS_COL) {
    nation_dlg->active_widget_list = nation_dlg->end_active_widget_list;
    create_vertical_scrollbar(nation_dlg,
                              TARGETS_COL, TARGETS_ROW, TRUE, TRUE);
  }

  /* ----------------------------------------------------------------- */

  /* Nation set selection */
  if (nation_set_count() > 1) {
    utf8_str *natset_str;
    struct option *poption;

    natset_str = create_utf8_from_char_fonto(_("Nation set"),
                                             FONTO_ATTENTION);
    change_fonto_utf8(natset_str, FONTO_MAX);
    nationsets = create_iconlabel(NULL, pwindow->dst, natset_str, 0);
    add_to_gui_list(ID_LABEL, nationsets);

    /* Create nation set name label */
    poption = optset_option_by_name(server_optset, "nationset");
    setup->set = nation_set_by_setting_value(option_str_get(poption));

    natset_str
      = create_utf8_from_char_fonto(nation_set_name_translation(setup->set),
                                    FONTO_ATTENTION);
    change_fonto_utf8(natset_str, FONTO_MAX);

    pwidget = create_iconlabel(NULL, pwindow->dst, natset_str, 0);

    add_to_gui_list(ID_LABEL, pwidget);
    setup->pset_name = pwidget;

    /* Create next nationset button */
    pwidget = create_themeicon_button(current_theme->r_arrow_icon,
                                      pwindow->dst, NULL, 0);
    pwidget->action = next_set_callback;
    if (nation_set_index(setup->set) < nation_set_count() - 1) {
      set_wstate(pwidget, FC_WS_NORMAL);
    }
    add_to_gui_list(ID_NATION_NEXT_NATIONSET_BUTTON, pwidget);
    pwidget->size.h = pwidget->next->size.h;
    setup->pset_next = pwidget;

    /* Create prev nationset button */
    pwidget = create_themeicon_button(current_theme->l_arrow_icon,
                                      pwindow->dst, NULL, 0);
    pwidget->action = prev_set_callback;
    if (nation_set_index(setup->set) > 0) {
      set_wstate(pwidget, FC_WS_NORMAL);
    }
    add_to_gui_list(ID_NATION_PREV_NATIONSET_BUTTON, pwidget);
    pwidget->size.h = pwidget->next->size.h;
    setup->pset_prev = pwidget;
  }

  /* Nation name */
  setup->nation = fc_rand(get_playable_nation_count());
  pnat = nation_by_number(setup->nation);
  setup->nation_style = style_number(style_of_nation(pnat));

  copy_chars_to_utf8_str(pstr, nation_plural_translation(pnat));
  change_fonto_utf8(pstr, FONTO_MAX);
  pstr->render = 2;
  pstr->fgcol = *get_theme_color(COLOR_THEME_NATIONDLG_TEXT);

  tmp_surf_zoomed = adj_surf(get_nation_flag_surface(pnat));

  pwidget = create_iconlabel(tmp_surf_zoomed, pwindow->dst, pstr,
                             (WF_ICON_ABOVE_TEXT|WF_ICON_CENTER|WF_FREE_GFX));
  if (nationsets == NULL) {
    buf = pwidget;
  } else {
    buf = nationsets;
  }

  add_to_gui_list(ID_LABEL, pwidget);

  /* Create leader name edit */
  pwidget = create_edit_from_chars_fonto(NULL, pwindow->dst,
                                         NULL, FONTO_BIG,
                                         adj_size(200), 0);
  pwidget->size.h = adj_size(24);

  set_wstate(pwidget, FC_WS_NORMAL);
  pwidget->action = leader_name_edit_callback;
  add_to_gui_list(ID_NATION_WIZARD_LEADER_NAME_EDIT, pwidget);
  setup->name_edit = pwidget;

  /* Create next leader name button */
  pwidget = create_themeicon_button(current_theme->r_arrow_icon,
                                    pwindow->dst, NULL, 0);
  pwidget->action = next_name_callback;
  add_to_gui_list(ID_NATION_WIZARD_NEXT_LEADER_NAME_BUTTON, pwidget);
  pwidget->size.h = pwidget->next->size.h;
  setup->name_next = pwidget;

  /* Create prev leader name button */
  pwidget = create_themeicon_button(current_theme->l_arrow_icon,
                                    pwindow->dst, NULL, 0);
  pwidget->action = prev_name_callback;
  add_to_gui_list(ID_NATION_WIZARD_PREV_LEADER_NAME_BUTTON, pwidget);
  pwidget->size.h = pwidget->next->size.h;
  setup->name_prev = pwidget;

  /* Change sex button */
  pwidget = create_icon_button_from_chars_fonto(NULL, pwindow->dst,
                                                sex_name_translation(SEX_MALE),
                                                FONTO_HEADING, 0);
  pwidget->action = change_sex_callback;
  pwidget->size.w = adj_size(100);
  pwidget->size.h = adj_size(22);
  set_wstate(pwidget, FC_WS_NORMAL);
  setup->change_sex = pwidget;

  /* Add to main widget list */
  add_to_gui_list(ID_NATION_WIZARD_CHANGE_SEX_BUTTON, pwidget);

  /* ---------------------------------------------------------- */
  zoom = DEFAULT_ZOOM * 1.0;

  len = 0;
  styles_iterate(pstyle) {
    int sn = basic_city_style_for_style(pstyle);

    tmp_surf = get_sample_city_surface(sn);

    if (tmp_surf->w > 48) {
      zoom = DEFAULT_ZOOM * (48.0 / tmp_surf->w);
    }

    tmp_surf_zoomed = zoomSurface(get_sample_city_surface(sn), zoom, zoom, 0);

    pwidget = create_icon2(tmp_surf_zoomed, pwindow->dst,
                           WF_RESTORE_BACKGROUND);
    pwidget->action = style_callback;
    if (sn != setup->nation_style) {
      set_wstate(pwidget, FC_WS_NORMAL);
    }
    len += pwidget->size.w;
    add_to_gui_list(MAX_ID - 1000 - sn, pwidget);
  } styles_iterate_end;

  last_City_Style = pwidget;
  /* ---------------------------------------------------------- */

  /* Create Cancel button */
  pwidget
    = create_themeicon_button_from_chars_fonto(current_theme->cancel_icon,
                                               pwindow->dst, _("Cancel"),
                                               FONTO_ATTENTION, 0);
  pwidget->action = races_dialog_cancel_callback;
  set_wstate(pwidget, FC_WS_NORMAL);

  add_to_gui_list(ID_NATION_WIZARD_DISCONNECT_BUTTON, pwidget);

  /* Create OK button */
  pwidget
    = create_themeicon_button_from_chars_fonto(current_theme->ok_icon,
                                               pwindow->dst,
                                               _("OK"), FONTO_ATTENTION, 0);
  pwidget->action = races_dialog_ok_callback;

  set_wstate(pwidget, FC_WS_NORMAL);
  add_to_gui_list(ID_NATION_WIZARD_START_BUTTON, pwidget);
  pwidget->size.w = MAX(pwidget->size.w, pwidget->next->size.w);
  pwidget->next->size.w = pwidget->size.w;

  nation_dlg->begin_widget_list = pwidget;
  /* ---------------------------------------------------------- */

  main_bg = theme_get_background(active_theme, BACKGROUND_NATIONDLG);
  if (resize_window(pwindow, main_bg, NULL, adj_size(640), adj_size(480))) {
    FREESURFACE(main_bg);
  }

  area = pwindow->area;

  widget_set_position(pwindow,
                      (main_window_width() - pwindow->size.w) / 2,
                      (main_window_height() - pwindow->size.h) / 2);

  /* Nations */

  h = nation_dlg->end_active_widget_list->size.h * TARGETS_ROW;
  i = (area.h - adj_size(43) - h) / 2;
  setup_vertical_widgets_position(TARGETS_COL,
                                  area.x + adj_size(10),
                                  area.y + i - adj_size(4),
                                  0, 0, nation_dlg->begin_active_widget_list,
                                  nation_dlg->end_active_widget_list);

  if (nation_dlg->scroll) {
    SDL_Rect area2;

    w = nation_dlg->end_active_widget_list->size.w * TARGETS_COL;
    setup_vertical_scrollbar_area(nation_dlg->scroll,
                                  area.x + w + adj_size(12),
                                  area.y + i - adj_size(4), h, FALSE);

    area2.x = area.x + w + adj_size(11);
    area2.y = area.y + i - adj_size(4);
    area2.w = nation_dlg->scroll->up_left_button->size.w + adj_size(2);
    area2.h = h;
    fill_rect_alpha(pwindow->theme, &area2, &bg_color);

    create_frame(pwindow->theme,
                 area2.x, area2.y - 1, area2.w, area2.h + 1,
                 get_theme_color(COLOR_THEME_NATIONDLG_FRAME));
  }

  if (nationsets != NULL) {
    /* Nationsets header */
    buf->size.x = area.x + area.w / 2 + (area.w / 2 - buf->size.w) / 2;
    buf->size.y = area.y + adj_size(46);

    natinfo_y = buf->size.y;
    natinfo_h = area.h -= buf->size.y;

    /* Nationset name */
    buf = buf->prev;
    buf->size.x = area.x + area.w / 2 + (area.w / 2 - buf->size.w) / 2;
    buf->size.y = natinfo_y + adj_size(46);

    natinfo_y += adj_size(46);
    natinfo_h -= adj_size(46);

    /* Next Nationset Button */
    buf = buf->prev;
    buf->size.x = buf->next->size.x + buf->next->size.w;
    buf->size.y = buf->next->size.y;

    /* Prev Nationset Button */
    buf = buf->prev;
    buf->size.x = buf->next->next->size.x - buf->size.w;
    buf->size.y = buf->next->size.y;

    buf = buf->prev;
  } else {
    natinfo_y = area.y;
    natinfo_h = area.h;
  }

  /* Selected Nation Name */
  buf->size.x = area.x + area.w / 2 + (area.w / 2 - buf->size.w) / 2;
  buf->size.y = natinfo_y + adj_size(46);

  /* Leader Name Edit */
  buf = buf->prev;
  buf->size.x = area.x + area.w / 2 + (area.w / 2 - buf->size.w) / 2;
  buf->size.y = natinfo_y + (natinfo_h - buf->size.h) / 2 - adj_size(30);

  /* Next Leader Name Button */
  buf = buf->prev;
  buf->size.x = buf->next->size.x + buf->next->size.w;
  buf->size.y = buf->next->size.y;

  /* Prev Leader Name Button */
  buf = buf->prev;
  buf->size.x = buf->next->next->size.x - buf->size.w;
  buf->size.y = buf->next->size.y;

  /* Change Leader Sex Button */
  buf = buf->prev;
  buf->size.x = area.x + area.w / 2 + (area.w / 2 - buf->size.w) / 2;
  buf->size.y = buf->next->size.y + buf->next->size.h + adj_size(20);

  /* First Style Button */
  buf = buf->prev;
  buf->size.x = area.x + area.w / 2 + (area.w / 2 - len) / 2 - adj_size(20);
  buf->size.y = buf->next->size.y + buf->next->size.h + adj_size(20);

  /* Rest Style Buttons */
  while (buf != last_City_Style) {
    buf = buf->prev;
    buf->size.x = buf->next->size.x + buf->next->size.w + adj_size(3);
    buf->size.y = buf->next->size.y;
  }

  create_line(pwindow->theme,
              area.x,
              natinfo_y + natinfo_h - adj_size(7) - buf->prev->size.h - adj_size(10),
              area.w - 1,
              natinfo_y + natinfo_h - adj_size(7) - buf->prev->size.h - adj_size(10),
              get_theme_color(COLOR_THEME_NATIONDLG_FRAME));

  /* Disconnect Button */
  buf = buf->prev;
  buf->size.x = area.x + adj_size(10);
  buf->size.y = natinfo_y + natinfo_h - adj_size(7) - buf->size.h;

  /* Start Button */
  buf = buf->prev;
  buf->size.x = area.w - adj_size(10) - buf->size.w;
  buf->size.y = buf->next->size.y;

  /* -------------------------------------------------------------------- */

  select_random_leader(setup->nation);

  redraw_group(nation_dlg->begin_widget_list, pwindow, 0);

  widget_flush(pwindow);
}

/**********************************************************************//**
  Close the nation selection dialog. This should allow the user to
  (at least) select a unit to activate.
**************************************************************************/
void popdown_races_dialog(void)
{
  if (nation_dlg) {
    popdown_window_group_dialog(nation_dlg->begin_widget_list,
                                nation_dlg->end_widget_list);

    cancel_help_dlg_callback(NULL);

    FC_FREE(leader_name);

    FC_FREE(nation_dlg->scroll);
    FC_FREE(nation_dlg);
  }
}

/**********************************************************************//**
  The server has changed the set of selectable nations.
**************************************************************************/
void races_update_pickable(bool nationset_change)
{
  /* If this is because of nationset change, update will take
   * place later when the new option value is received */
  if (nation_dlg != NULL && !nationset_change) {
    popdown_races_dialog();
    popup_races_dialog(client.conn.playing);
  }
}

/**********************************************************************//**
  Nationset selection update
**************************************************************************/
void nationset_changed(void)
{
  if (nation_dlg != NULL) {
    popdown_races_dialog();
    popup_races_dialog(client.conn.playing);
  }
}

/**********************************************************************//**
  In the nation selection dialog, make already-taken nations unavailable.
  This information is contained in the packet_nations_used packet.
**************************************************************************/
void races_toggles_set_sensitive(void)
{
  struct nation_info *setup;
  bool change = FALSE;
  struct widget *nat;

  if (!nation_dlg) {
    return;
  }

  setup = (struct nation_info *)(nation_dlg->end_widget_list->data.ptr);

  nations_iterate(nation) {
    if (!is_nation_pickable(nation) || nation->player) {
      log_debug("  [%d]: %d = %s", nation_index(nation),
                (!is_nation_pickable(nation) || nation->player),
                nation_rule_name(nation));

      nat = get_widget_pointer_from_main_list(MAX_ID - nation_index(nation));
      set_wstate(nat, FC_WS_DISABLED);

      if (nation_index(nation) == setup->nation) {
        change = TRUE;
      }
    }
  } nations_iterate_end;

  if (change) {
    do {
      setup->nation = fc_rand(get_playable_nation_count());
      nat = get_widget_pointer_from_main_list(MAX_ID - setup->nation);
    } while (get_wstate(nat) == FC_WS_DISABLED);

    if (get_wstate(setup->name_edit) == FC_WS_PRESSED) {
      force_exit_from_event_loop();
      set_wstate(setup->name_edit, FC_WS_NORMAL);
    }
    change_nation_label();
    enable(MAX_ID - 1000 - setup->nation_style);
    setup->nation_style = style_number(style_of_nation(nation_by_number(setup->nation)));
    disable(MAX_ID - 1000 - setup->nation_style);
    select_random_leader(setup->nation);
  }
  redraw_group(nation_dlg->begin_widget_list, nation_dlg->end_widget_list, 0);
  widget_flush(nation_dlg->end_widget_list);
}

/**********************************************************************//**
  Ruleset (modpack) has suggested loading certain tileset. Confirm from
  user and load.
**************************************************************************/
void popup_tileset_suggestion_dialog(void)
{
}

/**********************************************************************//**
  Ruleset (modpack) has suggested loading certain soundset. Confirm from
  user and load.
**************************************************************************/
void popup_soundset_suggestion_dialog(void)
{
}

/**********************************************************************//**
  Ruleset (modpack) has suggested loading certain musicset. Confirm from
  user and load.
**************************************************************************/
void popup_musicset_suggestion_dialog(void)
{
}

/**********************************************************************//**
  Tileset (modpack) has suggested loading certain theme. Confirm from
  user and load.
**************************************************************************/
bool popup_theme_suggestion_dialog(const char *theme_name)
{
  /* Don't load */
  return FALSE;
}

/**********************************************************************//**
  Player has gained a new tech.
**************************************************************************/
void show_tech_gained_dialog(Tech_type_id tech)
{
  /* PORTME */
}

/**********************************************************************//**
  Show tileset error dialog.
**************************************************************************/
void show_tileset_error(bool fatal, const char *tset_name, const char *msg)
{
  /* PORTME */
}

/**********************************************************************//**
  Give a warning when user is about to edit scenario with manually
  set properties.
**************************************************************************/
bool handmade_scenario_warning(void)
{
  /* Just tell the client common code to handle this. */
  return FALSE;
}

/**********************************************************************//**
  Update multipliers (policies) dialog.
**************************************************************************/
void real_multipliers_dialog_update(void *unused)
{
  /* PORTME */
}

/**********************************************************************//**
  Unit wants to get into some transport on given tile.
**************************************************************************/
bool request_transport(struct unit *pcargo, struct tile *ptile)
{
  return FALSE; /* Unit was not handled here. */
}

/**********************************************************************//**
  Popup detailed information about battle or save information for
  some kind of statistics
**************************************************************************/
void popup_combat_info(int attacker_unit_id, int defender_unit_id,
                       int attacker_hp, int defender_hp,
                       bool make_att_veteran, bool make_def_veteran)
{
}

/**********************************************************************//**
  Common code wants confirmation for an action.
**************************************************************************/
void request_action_confirmation(const char *expl,
                                 struct act_confirmation_data *data)
{
  /* TODO: Implement. Currently just pass everything as confirmed */
  action_confirmation(data, TRUE);
}

struct advanced_dialog *advanced_image_popup = NULL;

/**********************************************************************//**
  User interacted with image popup dialog.
**************************************************************************/
static int image_popup_window_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    move_window_group(advanced_image_popup->begin_widget_list, pwindow);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with image popup dialog close button.
**************************************************************************/
static int exit_image_popup_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (advanced_image_popup != NULL) {
      popdown_window_group_dialog(advanced_image_popup->begin_widget_list,
                                  advanced_image_popup->end_widget_list);
      FC_FREE(advanced_image_popup->scroll);
      FC_FREE(advanced_image_popup);
      flush_dirty();
    }
  }

  return -1;
}

/**********************************************************************//**
  Popup image window
**************************************************************************/
void popup_image(const char *tag)
{
  if (advanced_image_popup == NULL) {
    struct sprite *spr = load_popup_sprite(tag);

    if (spr != NULL) {
      struct widget *win = create_window_skeleton(NULL, NULL, 0);
      struct widget *buf;
      SDL_Surface *surf = copy_surface(GET_SURF(spr));
      SDL_Rect dst;
      SDL_Rect area;
      int width, height;

      get_sprite_dimensions(spr, &width, &height);
      advanced_image_popup = fc_calloc(1, sizeof(struct advanced_dialog));

      win->action = image_popup_window_callback;
      set_wstate(win, FC_WS_NORMAL);

      add_to_gui_list(ID_WINDOW, win);
      advanced_image_popup->end_widget_list = win;

      /* Create exit button */
      buf = create_themeicon(current_theme->small_cancel_icon, win->dst,
                             WF_WIDGET_HAS_INFO_LABEL | WF_RESTORE_BACKGROUND);
      buf->info_label = create_utf8_from_char_fonto(_("Close Dialog (Esc)"),
                                                    FONTO_ATTENTION);
      buf->action = exit_image_popup_callback;
      set_wstate(buf, FC_WS_NORMAL);
      buf->key = SDLK_ESCAPE;

      add_to_gui_list(ID_BUTTON, buf);
      advanced_image_popup->begin_widget_list = buf;

      resize_window(win, NULL, get_theme_color(COLOR_THEME_BACKGROUND),
                    win->size.w - win->area.w + width,
                    win->size.h - win->area.h + buf->area.h + height);

      widget_set_position(win,
                          (main_window_width() - win->size.w) / 2,
                          (main_window_height() - win->size.h) / 2);

      area = win->area;
      dst.x = area.x;
      dst.y = area.y + buf->size.y;
      alphablit(surf, NULL, win->theme, &dst, 255);

      /* Redraw */
      redraw_group(advanced_image_popup->begin_widget_list, win, 0);
      widget_flush(win);

      unload_popup_sprite(tag);
    } else {
      log_error(_("No image for tag \"%s\", requested by the server."), tag);
    }
  }
}
