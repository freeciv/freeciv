/***********************************************************************
 Freeciv - Copyright (C) 1996-2004 - The Freeciv Team
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

/* utility */
#include "fcintl.h"
#include "log.h"

/* common */
#include "fc_types.h"
#include "version.h"

/* client */
#include "connectdlg_common.h"
#include "pages_g.h"

/* gui-sdl2 */
#include "chatline.h"
#include "colors.h"
#include "connectdlg.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_tilespec.h"
#include "mapctrl.h"
#include "mapview.h"
#include "menu.h"
#include "messagewin.h"
#include "optiondlg.h"
#include "themespec.h"
#include "widget.h"


static enum client_pages old_page = PAGE_MAIN;

/**************************************************************************
                                  MAIN PAGE
**************************************************************************/
static struct small_dialog *start_menu = NULL;

static void popdown_start_menu(void);

/**********************************************************************//**
  User interacted with the Start New Game button.
**************************************************************************/
static int start_new_game_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popdown_start_menu();
    if (!is_server_running()) {
      client_start_server();

      /* Saved settings are sent in client/options.c
       * resend_desired_settable_options() */
    }
  }

  return -1;
}

/**********************************************************************//**
  User interacted with the Load Game button.
**************************************************************************/
static int load_game_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    set_client_page(PAGE_LOAD);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with the Join Game button.
**************************************************************************/
static int join_game_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    set_client_page(PAGE_NETWORK);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with the Join Pubserver button - open connection dialog.
**************************************************************************/
static int servers_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    bool lan_scan = (pwidget->id != ID_JOIN_META_GAME);

    popdown_start_menu();
    popup_connection_dialog(lan_scan);
  }

  return -1;
}

/**********************************************************************//**
  User interacted with the Options button - open options dialog.
**************************************************************************/
static int options_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    queue_flush();
    popdown_start_menu();
    popup_optiondlg();
  }

  return -1;
}

/**********************************************************************//**
  User interacted with the Quit button.
**************************************************************************/
static int quit_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popdown_start_menu();
  }

  return 0;/* exit from main game loop */
}

/**********************************************************************//**
  Update view to show main page.
**************************************************************************/
static void show_main_page(void)
{
  SDL_Color bg_color = {255, 255, 255, 96};
  SDL_Color line_color = {128, 128, 128, 255};
  int count = 0;
  struct widget *pwidget = NULL, *pwindow = NULL;
  SDL_Surface *background;
  int h = 0;
  SDL_Rect area;
  char verbuf[200];
  const char *rev_ver;

  /* create dialog */
  start_menu = fc_calloc(1, sizeof(struct small_dialog));

  pwindow = create_window_skeleton(NULL, NULL, 0);
  add_to_gui_list(ID_WINDOW, pwindow);
  start_menu->end_widget_list = pwindow;

  area = pwindow->area;

  /* Freeciv version */
  /* TRANS: Freeciv 3.2.0 */
  fc_snprintf(verbuf, sizeof(verbuf), _("Freeciv %s"), VERSION_STRING);
  pwidget = create_iconlabel_from_chars_fonto(NULL, pwindow->dst, verbuf,
                                              FONTO_ATTENTION,
                                        (WF_SELECT_WITHOUT_BAR|WF_RESTORE_BACKGROUND|WF_FREE_DATA));

  pwidget->string_utf8->style |= SF_CENTER | TTF_STYLE_BOLD;

  area.w = MAX(area.w, pwidget->size.w);
  h = MAX(h, pwidget->size.h);
  count++;

  add_to_gui_list(ID_LABEL, pwidget);

  /* TRANS: gui-sdl2 client */
  fc_snprintf(verbuf, sizeof(verbuf), _("%s client"), client_string);
  pwidget = create_iconlabel_from_chars_fonto(NULL, pwindow->dst, verbuf,
                                              FONTO_ATTENTION,
                                        (WF_SELECT_WITHOUT_BAR|WF_RESTORE_BACKGROUND|WF_FREE_DATA));

  pwidget->string_utf8->style |= SF_CENTER | TTF_STYLE_BOLD;

  area.w = MAX(area.w, pwidget->size.w);
  h = MAX(h, pwidget->size.h);
  count++;

  add_to_gui_list(ID_LABEL, pwidget);

  /* Start New Game */
  pwidget = create_iconlabel_from_chars_fonto(NULL, pwindow->dst,
                                              _("Start New Game"),
                                              FONTO_HEADING,
                   (WF_SELECT_WITHOUT_BAR|WF_RESTORE_BACKGROUND|WF_FREE_DATA));

  pwidget->action = start_new_game_callback;
  pwidget->string_utf8->style |= SF_CENTER;
  set_wstate(pwidget, FC_WS_NORMAL);

  area.w = MAX(area.w, pwidget->size.w);
  h = MAX(h, pwidget->size.h);
  count++;

  add_to_gui_list(ID_START_NEW_GAME, pwidget);

  /* Load Game */
  pwidget = create_iconlabel_from_chars_fonto(NULL, pwindow->dst,
                                              _("Load Game"),
                                              FONTO_HEADING,
                               (WF_SELECT_WITHOUT_BAR|WF_RESTORE_BACKGROUND));

  pwidget->action = load_game_callback;
  pwidget->string_utf8->style |= SF_CENTER;
  set_wstate(pwidget, FC_WS_NORMAL);

  add_to_gui_list(ID_LOAD_GAME, pwidget);

  area.w = MAX(area.w, pwidget->size.w);
  h = MAX(h, pwidget->size.h);
  count++;

  /* Join Game */
  pwidget = create_iconlabel_from_chars_fonto(NULL, pwindow->dst,
                                              _("Join Game"),
                                              FONTO_HEADING,
                                 WF_SELECT_WITHOUT_BAR|WF_RESTORE_BACKGROUND);

  pwidget->action = join_game_callback;
  pwidget->string_utf8->style |= SF_CENTER;
  set_wstate(pwidget, FC_WS_NORMAL);

  add_to_gui_list(ID_JOIN_GAME, pwidget);

  area.w = MAX(area.w, pwidget->size.w);
  h = MAX(h, pwidget->size.h);
  count++;

  /* Join Pubserver */
  pwidget = create_iconlabel_from_chars_fonto(NULL, pwindow->dst,
                                              _("Join Pubserver"),
                                              FONTO_HEADING,
                                WF_SELECT_WITHOUT_BAR|WF_RESTORE_BACKGROUND);

  pwidget->action = servers_callback;
  pwidget->string_utf8->style |= SF_CENTER;
  set_wstate(pwidget, FC_WS_NORMAL);

  add_to_gui_list(ID_JOIN_META_GAME, pwidget);

  area.w = MAX(area.w, pwidget->size.w);
  h = MAX(h, pwidget->size.h);
  count++;

  /* Join LAN Server */
  pwidget = create_iconlabel_from_chars_fonto(NULL, pwindow->dst,
                                              _("Join LAN Server"),
                                              FONTO_HEADING,
                                 WF_SELECT_WITHOUT_BAR|WF_RESTORE_BACKGROUND);

  pwidget->action = servers_callback;
  pwidget->string_utf8->style |= SF_CENTER;
  set_wstate(pwidget, FC_WS_NORMAL);

  add_to_gui_list(ID_JOIN_GAME, pwidget);

  area.w = MAX(area.w, pwidget->size.w);
  h = MAX(h, pwidget->size.h);
  count++;

  /* Options */
  pwidget = create_iconlabel_from_chars_fonto(NULL, pwindow->dst,
                                              _("Options"),
                                              FONTO_HEADING,
                                 WF_SELECT_WITHOUT_BAR|WF_RESTORE_BACKGROUND);

  pwidget->action = options_callback;
  pwidget->string_utf8->style |= SF_CENTER;
  set_wstate(pwidget, FC_WS_NORMAL);

  add_to_gui_list(ID_CLIENT_OPTIONS_BUTTON, pwidget);

  area.w = MAX(area.w, pwidget->size.w);
  h = MAX(h, pwidget->size.h);
  count++;

  /* Quit */
  pwidget = create_iconlabel_from_chars_fonto(NULL, pwindow->dst,
                                              _("Quit"),
                                              FONTO_HEADING,
                                 WF_SELECT_WITHOUT_BAR|WF_RESTORE_BACKGROUND);

  pwidget->action = quit_callback;
  pwidget->string_utf8->style |= SF_CENTER;
  pwidget->key = SDLK_ESCAPE;
  set_wstate(pwidget, FC_WS_NORMAL);
  add_to_gui_list(ID_QUIT, pwidget);

  area.w = MAX(area.w, pwidget->size.w);
  h = MAX(h, pwidget->size.h);
  count++;

  start_menu->begin_widget_list = pwidget;

  /* ------*/

  area.w += adj_size(30);
  h += adj_size(6);

  area.h = MAX(area.h, h * count);

  /* ------*/

  background = theme_get_background(active_theme, BACKGROUND_STARTMENU);
  if (resize_window(pwindow, background, NULL,
                    (pwindow->size.w - pwindow->area.w) + area.w,
                    (pwindow->size.h - pwindow->area.h) + area.h)) {
    FREESURFACE(background);
  }

  area = pwindow->area;

  group_set_area(pwidget, pwindow->prev, area);

  setup_vertical_widgets_position(1, area.x, area.y, area.w, h, pwidget, pwindow->prev);

  area.h = h * 2;
  fill_rect_alpha(pwindow->theme, &area, &bg_color);

  widget_set_position(pwindow,
                      (main_window_width() - pwindow->size.w) - adj_size(20),
                      (main_window_height() - pwindow->size.h) - adj_size(20));

  draw_intro_gfx();

  redraw_group(start_menu->begin_widget_list, start_menu->end_widget_list, FALSE);

  create_line(pwindow->dst->surface,
              area.x, area.y + (h * 2 - 1),
              area.x + area.w - 1, area.y + (h * 2 - 1),
              &line_color);

  set_output_window_text(_("SDL2-client welcomes you..."));

#if IS_BETA_VERSION || IS_DEVEL_VERSION
  set_output_window_text(unstable_message());
#endif /* IS_BETA_VERSION || IS_DEVEL_VERSION */

  rev_ver = fc_git_revision();
  if (rev_ver != NULL) {
    char buffer[512];

    fc_snprintf(buffer, sizeof(buffer), _("Commit: %s"), rev_ver);
    set_output_window_text(buffer);
  }

  chat_welcome_message(FALSE);

  meswin_dialog_popup(TRUE);

  flush_all();
}

/**********************************************************************//**
  Close start menu
**************************************************************************/
static void popdown_start_menu(void)
{
  if (start_menu) {
    popdown_window_group_dialog(start_menu->begin_widget_list,
                                start_menu->end_widget_list);
    FC_FREE(start_menu);
    flush_dirty();
  }
}

/**************************************************************************
                             PUBLIC FUNCTIONS
**************************************************************************/

/**********************************************************************//**
  Sets the "page" that the client should show.  See documentation in
  pages_g.h.
**************************************************************************/
void real_set_client_page(enum client_pages page)
{
  switch (old_page) {
  case PAGE_MAIN:
    popdown_start_menu();
    break;
  case PAGE_LOAD:
    popdown_load_game_dialog();
    break;
  case PAGE_NETWORK:
    close_connection_dialog();
    break;
  case PAGE_START:
    popdown_conn_list_dialog();
    break;
  case PAGE_GAME:
    close_game_page();
    break;
  default:
    break;
  }

  old_page = page;

  switch (page) {
  case PAGE_MAIN:
    show_main_page();
    break;
  case PAGE_LOAD:
    client_start_server();
    break;
  case PAGE_NETWORK:
    popup_join_game_dialog();
    break;
  case PAGE_START:
    conn_list_dialog_update();
    break;
  case PAGE_GAME:
    show_game_page();
    enable_main_widgets();
    update_info_label();
    unit_focus_update();
    update_unit_info_label(get_units_in_focus());
    update_turn_done_button_state();
    refresh_overview();
    menus_update();
    break;
  default:
    break;
  }
}

/**********************************************************************//**
  Set the list of available rulesets.  The default ruleset should be
  "default", and if the user changes this then set_ruleset() should be
  called.
**************************************************************************/
void set_rulesets(int num_rulesets, char **rulesets)
{
  /* PORTME */
}

/**********************************************************************//**
  Returns current client page
**************************************************************************/
enum client_pages get_current_client_page(void)
{
  return old_page;
}

/**********************************************************************//**
  Update the start page.
**************************************************************************/
void update_start_page(void)
{
  conn_list_dialog_update();
}
