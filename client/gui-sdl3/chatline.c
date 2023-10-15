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
                          chatline.c  -  description
                             -------------------
    begin                : Sun Jun 30 2002
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
#include "game.h"
#include "packets.h"

/* client */
#include "client_main.h"
#include "clinet.h"
#include "connectdlg_common.h"
#include "ratesdlg_g.h"
#include "update_queue.h"

/* gui-sdl3 */
#include "colors.h"
#include "dialogs.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_tilespec.h"
#include "mapview.h"
#include "messagewin.h"
#include "themespec.h"
#include "utf8string.h"
#include "widget.h"

#include "chatline.h"

struct CONNLIST {
  struct advanced_dialog *users_dlg;
  struct advanced_dialog *chat_dlg;
  struct widget *begin_widget_list;
  struct widget *end_widget_list;
  struct widget *start_button;
  struct widget *select_nation_button;
  struct widget *load_game_button;
  struct widget *configure;
  struct widget *back_button;
  struct widget *pedit;
  int text_width;
  int active;
} *conn_dlg = NULL;

static void popup_conn_list_dialog(void);
static void add_to_chat_list(char *msg, size_t n_alloc);

/**************************************************************************
                                  LOAD GAME
**************************************************************************/

struct advanced_dialog *load_dialog;

/**********************************************************************//**
  User event to load game dialog window.
**************************************************************************/
static int move_load_game_dlg_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    move_window_group(load_dialog->begin_widget_list, pwindow);
  }

  return -1;
}

/**********************************************************************//**
  Close load game dialog
**************************************************************************/
void popdown_load_game_dialog(void)
{
  if (load_dialog) {
    popdown_window_group_dialog(load_dialog->begin_widget_list,
                                load_dialog->end_widget_list);
    FC_FREE(load_dialog->scroll);
    FC_FREE(load_dialog);

    /* enable buttons */
    set_wstate(conn_dlg->back_button, FC_WS_NORMAL);
    widget_redraw(conn_dlg->back_button);
    widget_mark_dirty(conn_dlg->back_button);
    set_wstate(conn_dlg->load_game_button, FC_WS_NORMAL);
    widget_redraw(conn_dlg->load_game_button);
    widget_mark_dirty(conn_dlg->load_game_button);
    set_wstate(conn_dlg->start_button, FC_WS_NORMAL);
    widget_redraw(conn_dlg->start_button);
    widget_mark_dirty(conn_dlg->start_button);

    flush_dirty();
  }
}

/**********************************************************************//**
  User clicked load game dialog close-button.
**************************************************************************/
static int exit_load_dlg_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (get_client_page() == PAGE_LOAD) {
      set_client_page(PAGE_START);
    } else {
      popdown_load_game_dialog();
    }
  }

  return -1;
}

/**********************************************************************//**
  User selected file to load.
**************************************************************************/
static int load_selected_game_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    char *filename = (char*)pwidget->data.ptr;

    if (is_server_running()) {
      send_chat_printf("/load %s", filename);

      if (get_client_page() == PAGE_LOAD) {
        set_client_page(PAGE_START);
      } else if (get_client_page() == PAGE_START) {
        popdown_load_game_dialog();
      }
    } else {
      set_client_page(PAGE_MAIN);
    }
  }

  return -1;
}

/**********************************************************************//**
  Open load game dialog
**************************************************************************/
static void popup_load_game_dialog(void)
{
  struct widget *pwindow;
  struct widget *close_button;
  struct widget *filename_label = NULL;
  struct widget *first_label = NULL;
  struct widget *last_label = NULL;
  struct widget *next_label = NULL;
  utf8_str *title, *filename;
  SDL_Rect area;
  struct fileinfo_list *files;
  int count = 0;
  int scrollbar_width = 0;
  int max_label_width = 0;

  if (load_dialog) {
    return;
  }

  /* Disable buttons */
  set_wstate(conn_dlg->back_button, FC_WS_DISABLED);
  widget_redraw(conn_dlg->back_button);
  widget_mark_dirty(conn_dlg->back_button);
  set_wstate(conn_dlg->load_game_button, FC_WS_DISABLED);
  widget_redraw(conn_dlg->load_game_button);
  widget_mark_dirty(conn_dlg->load_game_button);
  set_wstate(conn_dlg->select_nation_button, FC_WS_DISABLED);
  widget_redraw(conn_dlg->select_nation_button);
  widget_mark_dirty(conn_dlg->select_nation_button);
  set_wstate(conn_dlg->start_button, FC_WS_DISABLED);
  widget_redraw(conn_dlg->start_button);
  widget_mark_dirty(conn_dlg->start_button);

  /* Create dialog */
  load_dialog = fc_calloc(1, sizeof(struct advanced_dialog));

  title = create_utf8_from_char_fonto(_("Choose Saved Game to Load"),
                                      FONTO_ATTENTION);
  title->style |= TTF_STYLE_BOLD;

  pwindow = create_window_skeleton(NULL, title, 0);
  pwindow->action = move_load_game_dlg_callback;
  set_wstate(pwindow, FC_WS_NORMAL);

  add_to_gui_list(ID_WINDOW, pwindow);

  load_dialog->end_widget_list = pwindow;

  area = pwindow->area;

  /* Close button */
  close_button = create_themeicon(current_theme->small_cancel_icon,
                                  pwindow->dst,
                                  WF_WIDGET_HAS_INFO_LABEL
                                  | WF_RESTORE_BACKGROUND);
  close_button->info_label
    = create_utf8_from_char_fonto(_("Close Dialog (Esc)"), FONTO_ATTENTION);
  close_button->action = exit_load_dlg_callback;
  set_wstate(close_button, FC_WS_NORMAL);
  close_button->key = SDLK_ESCAPE;

  add_to_gui_list(ID_BUTTON, close_button);

  area.w += close_button->size.w;

  load_dialog->begin_widget_list = close_button;

  /* Create scrollbar */
  scrollbar_width = create_vertical_scrollbar(load_dialog, 1, 20, TRUE, TRUE);
  hide_scrollbar(load_dialog->scroll);

  /* Search for user saved games. */
  files = fileinfolist_infix(get_save_dirs(), ".sav", FALSE);
  fileinfo_list_iterate(files, pfile) {
    count++;

    filename = create_utf8_from_char_fonto(pfile->name, FONTO_ATTENTION_PLUS);
    filename->style |= SF_CENTER;
    filename_label = create_iconlabel(NULL, pwindow->dst, filename,
      (WF_FREE_DATA | WF_SELECT_WITHOUT_BAR | WF_RESTORE_BACKGROUND));

    /* Store filename */
    filename_label->data.ptr = fc_calloc(1, strlen(pfile->fullname) + 1);
    fc_strlcpy((char*)filename_label->data.ptr, pfile->fullname,
               strlen(pfile->fullname) + 1);

    filename_label->action = load_selected_game_callback;

    set_wstate(filename_label, FC_WS_NORMAL);

    /* FIXME: This was supposed to be add_widget_to_vertical_scroll_widget_list(), but
     * add_widget_to_vertical_scroll_widget_list() needs the scrollbar area to be defined
     * for updating the scrollbar position, but the area is not known yet (depends on
     * maximum label width) */
    add_to_gui_list(ID_LABEL, filename_label);

    if (count == 1) {
      first_label = filename_label;
    }

    max_label_width = MAX(max_label_width, filename_label->size.w);
  } fileinfo_list_iterate_end;
  fileinfo_list_destroy(files);

  last_label = filename_label;

  area.w = MAX(area.w, max_label_width + scrollbar_width + 1);

  if (count > 0) {
    area.h = (load_dialog->scroll->active * filename_label->size.h) + adj_size(5);
  }

  resize_window(pwindow, theme_get_background(active_theme,
                                              BACKGROUND_LOADGAMEDLG),
                NULL,
                (pwindow->size.w - pwindow->area.w) + area.w,
                (pwindow->size.h - pwindow->area.h) + area.h);

  area = pwindow->area;

  setup_vertical_scrollbar_area(load_dialog->scroll,
                                area.x + area.w - 1,
                                area.y + 1,
                                area.h - adj_size(2), TRUE);

  /* Add filename labels to list */
  filename_label = first_label;
  while (filename_label) {
    filename_label->size.w = area.w - scrollbar_width - 3;

    next_label = filename_label->prev;

    del_widget_pointer_from_gui_list(filename_label);
    if (filename_label == first_label) {
      add_widget_to_vertical_scroll_widget_list(load_dialog,
          filename_label, close_button,
          FALSE,
          area.x + 1,
          area.y + adj_size(2));
    } else {
      add_widget_to_vertical_scroll_widget_list(load_dialog,
          filename_label,
          load_dialog->begin_active_widget_list,
          FALSE,
          area.x + 1,
          area.y + adj_size(2));
    }

    if (filename_label == last_label) {
      break;
    }

    filename_label = next_label;
  }

  widget_set_position(pwindow,
                      (main_window_width() - pwindow->size.w) / 2,
                      (main_window_height() - pwindow->size.h) / 2);

  widget_set_position(close_button,
                      area.x + area.w - close_button->size.w - 1,
                      pwindow->size.y + adj_size(2));

  /* FIXME: The scrollbar already got a background saved in
   * add_widget_to_vertical_scroll_widget_list(), but the window
   * is not drawn yet, so this saved background is wrong.
   * Deleting it here as a workaround. */
  FREESURFACE(load_dialog->scroll->pscroll_bar->gfx);

  redraw_group(load_dialog->begin_widget_list, pwindow, 1);
  flush_dirty();
}

/**********************************************************************//**
  Sent msg/command from input dlg to server
**************************************************************************/
static int inputline_return_callback(struct widget *pwidget)
{
  if (pwidget->string_utf8->text != NULL
      && pwidget->string_utf8->text[0] != '\0') {
    send_chat(pwidget->string_utf8->text);

    output_window_append(ftc_any, pwidget->string_utf8->text);
  }

  return -1;
}

/**********************************************************************//**
  This function is main chat/command client input.
**************************************************************************/
void popup_input_line(void)
{
  struct widget *input_edit;

  input_edit = create_edit_from_chars_fonto(NULL, main_data.gui, "",
                                            FONTO_ATTENTION,
                                            adj_size(400), 0);

  input_edit->size.x = (main_window_width() - input_edit->size.w) / 2;
  input_edit->size.y = (main_window_height() - input_edit->size.h) / 2;

  if (edit(input_edit) != ED_ESC) {
    inputline_return_callback(input_edit);
  }

  widget_undraw(input_edit);
  widget_mark_dirty(input_edit);
  FREEWIDGET(input_edit);

  flush_dirty();
}

/**********************************************************************//**
  Appends the string to the chat output window. The string should be
  inserted on its own line, although it will have no newline.
**************************************************************************/
void real_output_window_append(const char *astring,
                               const struct text_tag_list *tags,
                               int conn_id)
{
  /* Currently this is a wrapper to the message subsystem. */
  if (conn_dlg) {
    size_t n = strlen(astring);
    char *buffer = fc_strdup(astring);

    add_to_chat_list(buffer, n);
  } else {
    meswin_add(astring, tags, NULL, E_CHAT_MSG,
               game.info.turn, game.info.phase);
  }
}

/**********************************************************************//**
  Get the text of the output window, and call write_chatline_content() to
  log it.
**************************************************************************/
void log_output_window(void)
{
  /* TODO */
}

/**********************************************************************//**
  Clear all text from the output window.
**************************************************************************/
void clear_output_window(void)
{
  /* TODO */
}

/* ====================================================================== */

/**********************************************************************//**
  User did something to connection list dialog.
**************************************************************************/
static int conn_dlg_callback(struct widget *pwindow)
{
  return -1;
}

/**********************************************************************//**
  User selected to get back from connection list dialog.
**************************************************************************/
static int disconnect_conn_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popdown_conn_list_dialog();
    flush_dirty();
    disconnect_from_server(TRUE);
  }

  return -1;
}

/**********************************************************************//**
  Handle chat messages when connection dialog open.
**************************************************************************/
static void add_to_chat_list(char *msg, size_t n_alloc)
{
  utf8_str *pstr;
  struct widget *buf, *pwindow = conn_dlg->end_widget_list;

  fc_assert_ret(msg != NULL);
  fc_assert_ret(n_alloc != 0);

  pstr = create_utf8_str_fonto(msg, n_alloc, FONTO_ATTENTION);

  if (convert_utf8_str_to_const_surface_width(pstr,
                                              conn_dlg->text_width - adj_size(5))) {
    utf8_str *pstr2;
    int count = 0;
    char **utf8_texts = create_new_line_utf8strs(pstr->text);

    while (utf8_texts[count] != NULL) {
      pstr2 = create_utf8_str_fonto(utf8_texts[count],
                                    strlen(utf8_texts[count]) + 1,
                                    FONTO_ATTENTION);
      pstr2->bgcol = (SDL_Color) {0, 0, 0, 0};
      buf = create_themelabel2(NULL, pwindow->dst,
                               pstr2, conn_dlg->text_width, 0,
                               (WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE));

      buf->size.w = conn_dlg->text_width;
      add_widget_to_vertical_scroll_widget_list(conn_dlg->chat_dlg, buf,
                        conn_dlg->chat_dlg->begin_active_widget_list, FALSE,
                        pwindow->size.x + adj_size(10 + 60 + 10),
                        pwindow->size.y + adj_size(14));
      count++;
    }
    redraw_group(conn_dlg->chat_dlg->begin_widget_list,
                 conn_dlg->chat_dlg->end_widget_list, TRUE);
    FREEUTF8STR(pstr);
  } else {
    pstr->bgcol = (SDL_Color) {0, 0, 0, 0};
    buf = create_themelabel2(NULL, pwindow->dst,
                             pstr, conn_dlg->text_width, 0,
                             (WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE));

    buf->size.w = conn_dlg->text_width;

    if (add_widget_to_vertical_scroll_widget_list(conn_dlg->chat_dlg, buf,
                        conn_dlg->chat_dlg->begin_active_widget_list, FALSE,
                        pwindow->size.x + adj_size(10 + 60 + 10),
                        pwindow->size.y + adj_size(14))) {
      redraw_group(conn_dlg->chat_dlg->begin_widget_list,
                   conn_dlg->chat_dlg->end_widget_list, TRUE);
    } else {
      widget_redraw(buf);
      widget_mark_dirty(buf);
    }
  }

  flush_dirty();
}

/**********************************************************************//**
  User interacted with connection dialog input field.
**************************************************************************/
static int input_edit_conn_callback(struct widget *pwidget)
{
  if (pwidget->string_utf8->text != NULL) {
    if (pwidget->string_utf8->text[0] != '\0') {
      send_chat(pwidget->string_utf8->text);
    }

    free(pwidget->string_utf8->text);
    pwidget->string_utf8->text = fc_malloc(1);
    pwidget->string_utf8->text[0] = '\0';
    pwidget->string_utf8->n_alloc = 0;
  }

  return -1;
}

/**********************************************************************//**
   User interacted with Start Game button.
**************************************************************************/
static int start_game_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    send_chat("/start");
  }

  return -1;
}

/**********************************************************************//**
  User interacted with Select Nation button.
**************************************************************************/
static int select_nation_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    popup_races_dialog(client.conn.playing);
  }

  return -1;
}

/* not implemented yet */
#if 0
/**********************************************************************//**
  User interacted with Server Settings button.
**************************************************************************/
static int server_config_callback(struct widget *pwidget)
{
  return -1;
}
#endif /* 0 */

/**********************************************************************//**
  User interacted with Load Game button.
**************************************************************************/
static int load_game_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    /* set_wstate(conn_dlg->load_game_button, FC_WS_NORMAL);
     * widget_redraw(conn_dlg->load_game_button);
     * flush_dirty(); */
    popup_load_game_dialog();
  }

  return -1;
}

/**********************************************************************//**
  Update the connected users list at pregame state.
**************************************************************************/
void real_conn_list_dialog_update(void *unused)
{
  if (C_S_PREPARING == client_state()) {
    if (conn_dlg) {
      struct widget *buf = NULL, *pwindow = conn_dlg->end_widget_list;
      utf8_str *pstr = create_utf8_str_fonto(NULL, 0, FONTO_ATTENTION);
      bool create;

      pstr->bgcol = (SDL_Color) {0, 0, 0, 0};

      if (conn_dlg->users_dlg) {
        del_group(conn_dlg->users_dlg->begin_active_widget_list,
                  conn_dlg->users_dlg->end_active_widget_list);
        conn_dlg->users_dlg->active_widget_list = NULL;
        conn_dlg->users_dlg->begin_widget_list =
          conn_dlg->users_dlg->scroll->pscroll_bar;
        conn_dlg->users_dlg->scroll->count = 0;
      } else {
        conn_dlg->users_dlg = fc_calloc(1, sizeof(struct advanced_dialog));
        conn_dlg->users_dlg->end_widget_list = conn_dlg->begin_widget_list;
        conn_dlg->users_dlg->begin_widget_list = conn_dlg->begin_widget_list;

        create_vertical_scrollbar(conn_dlg->users_dlg, 1,
                                  conn_dlg->active, TRUE, TRUE);
        conn_dlg->users_dlg->end_widget_list =
          conn_dlg->users_dlg->end_widget_list->prev;
        setup_vertical_scrollbar_area(conn_dlg->users_dlg->scroll,
          pwindow->size.x + pwindow->size.w - adj_size(30),
          pwindow->size.y + adj_size(14),
          pwindow->size.h - adj_size(44) - adj_size(40), FALSE);
      }

      hide_scrollbar(conn_dlg->users_dlg->scroll);
      create = TRUE;
      conn_list_iterate(game.est_connections, pconn) {
        copy_chars_to_utf8_str(pstr, pconn->username);

        buf = create_themelabel2(NULL, pwindow->dst, pstr, adj_size(100), 0,
                (WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE));
        clear_wflag(buf, WF_FREE_STRING);

        buf->id = ID_LABEL;

        /* add to widget list */
        if (create) {
          add_widget_to_vertical_scroll_widget_list(conn_dlg->users_dlg,
            buf, conn_dlg->users_dlg->begin_widget_list, FALSE,
            pwindow->area.x + pwindow->area.w - adj_size(130),
            pwindow->size.y + adj_size(14));
          create = FALSE;
        } else {
	  add_widget_to_vertical_scroll_widget_list(conn_dlg->users_dlg,
                buf, conn_dlg->users_dlg->begin_active_widget_list, FALSE,
                pwindow->area.x + pwindow->area.w - adj_size(130),
                pwindow->size.y + adj_size(14));
        }
      } conn_list_iterate_end;

      conn_dlg->begin_widget_list = conn_dlg->users_dlg->begin_widget_list;
      FREEUTF8STR(pstr);

/* FIXME: implement the server settings dialog and then reactivate this part */
#if 0
      if (ALLOW_CTRL == client.conn.access_level
          || ALLOW_HACK == client.conn.access_level) {
        set_wstate(conn_dlg->configure, FC_WS_NORMAL);
      } else {
        set_wstate(conn_dlg->configure, FC_WS_DISABLED);
      }
#endif /* 0 */

      /* redraw */
      redraw_group(conn_dlg->begin_widget_list, conn_dlg->end_widget_list, 0);

      widget_flush(conn_dlg->end_widget_list);
    } else {
      popup_conn_list_dialog();
    }

    /* PAGE_LOAD -> the server was started from the main page to load a game */
    if (get_client_page() == PAGE_LOAD) {
      popup_load_game_dialog();
    }
  } else {
    if (popdown_conn_list_dialog()) {
      flush_dirty();
    }
  }
}

/**********************************************************************//**
  Open connection list dialog
**************************************************************************/
static void popup_conn_list_dialog(void)
{
  SDL_Color window_bg_color = {255, 255, 255, 96};

  struct widget *pwindow = NULL, *buf = NULL, *label = NULL;
  struct widget* back_button = NULL;
  struct widget *start_game_button = NULL;
  struct widget *select_nation_button = NULL;
/*  struct widget *server_settings_button = NULL; */
  utf8_str *pstr = NULL;
  int n;
  SDL_Rect area;
  SDL_Surface *surf;

  if (conn_dlg || !client.conn.established) {
    return;
  }

  meswin_dialog_popdown();

  conn_dlg = fc_calloc(1, sizeof(struct CONNLIST));

  pwindow = create_window_skeleton(NULL, NULL, 0);
  pwindow->action = conn_dlg_callback;
  set_wstate(pwindow, FC_WS_NORMAL);
  clear_wflag(pwindow, WF_DRAW_FRAME_AROUND_WIDGET);

  conn_dlg->end_widget_list = pwindow;
  add_to_gui_list(ID_WINDOW, pwindow);

  widget_set_position(pwindow, 0, 0);

  /* Create window background */
  surf = theme_get_background(active_theme, BACKGROUND_CONNLISTDLG);
  if (resize_window(pwindow, surf, NULL, main_window_width(),
                    main_window_height())) {
    FREESURFACE(surf);
  }

  conn_dlg->text_width
    = pwindow->size.w - adj_size(130) - adj_size(20) - adj_size(20);

  /* Chat area background */
  area.x = adj_size(10);
  area.y = adj_size(14);
  area.w = conn_dlg->text_width + adj_size(20);
  area.h = pwindow->size.h - adj_size(44) - adj_size(40);
  fill_rect_alpha(pwindow->theme, &area, &window_bg_color);

  create_frame(pwindow->theme,
               area.x - 1, area.y - 1, area.w + 1, area.h + 1,
               get_theme_color(COLOR_THEME_CONNLISTDLG_FRAME));

  /* User list background */
  area.x = pwindow->size.w - adj_size(130);
  area.y = adj_size(14);
  area.w = adj_size(120);
  area.h = pwindow->size.h - adj_size(44) - adj_size(40);
  fill_rect_alpha(pwindow->theme, &area, &window_bg_color);

  create_frame(pwindow->theme,
               area.x - 1, area.y - 1, area.w + 1, area.h + 1,
               get_theme_color(COLOR_THEME_CONNLISTDLG_FRAME));

  draw_frame(pwindow->theme, 0, 0, pwindow->theme->w, pwindow->theme->h);

  /* -------------------------------- */

  /* Chat area */

  conn_dlg->chat_dlg = fc_calloc(1, sizeof(struct advanced_dialog));

  n = conn_list_size(game.est_connections);

  {
    char cbuf[256];

    fc_snprintf(cbuf, sizeof(cbuf), _("Total users logged in : %d"), n);
    pstr = create_utf8_from_char_fonto(cbuf, FONTO_ATTENTION);
  }

  pstr->bgcol = (SDL_Color) {0, 0, 0, 0};

  label = create_themelabel2(NULL, pwindow->dst,
                             pstr, conn_dlg->text_width, 0,
                             (WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE));

  widget_set_position(label, adj_size(10), adj_size(14));

  add_to_gui_list(ID_LABEL, label);

  conn_dlg->chat_dlg->begin_widget_list = label;
  conn_dlg->chat_dlg->end_widget_list = label;
  conn_dlg->chat_dlg->begin_active_widget_list = conn_dlg->chat_dlg->begin_widget_list;
  conn_dlg->chat_dlg->end_active_widget_list = conn_dlg->chat_dlg->end_widget_list;

  n = (pwindow->size.h - adj_size(44) - adj_size(40)) / label->size.h;
  conn_dlg->active = n;

  create_vertical_scrollbar(conn_dlg->chat_dlg, 1,
                            conn_dlg->active, TRUE, TRUE);

  setup_vertical_scrollbar_area(conn_dlg->chat_dlg->scroll,
                adj_size(10) + conn_dlg->text_width + 1,
                adj_size(14), pwindow->size.h - adj_size(44) - adj_size(40), FALSE);
  hide_scrollbar(conn_dlg->chat_dlg->scroll);

  /* -------------------------------- */

  /* Input field */

  buf = create_edit_from_chars_fonto(NULL, pwindow->dst, "",
                                     FONTO_ATTENTION,
                                     pwindow->size.w - adj_size(10) - adj_size(10),
                                     (WF_RESTORE_BACKGROUND|WF_EDIT_LOOP));

  buf->size.x = adj_size(10);
  buf->size.y = pwindow->size.h - adj_size(40) - adj_size(5) - buf->size.h;
  buf->action = input_edit_conn_callback;
  set_wstate(buf, FC_WS_NORMAL);
  conn_dlg->pedit = buf;
  add_to_gui_list(ID_EDIT, buf);

  /* Buttons */

  buf = create_themeicon_button_from_chars_fonto(current_theme->back_icon,
                                                 pwindow->dst,
                                                 _("Back"), FONTO_ATTENTION, 0);
  buf->size.x = adj_size(10);
  buf->size.y = pwindow->size.h - adj_size(10) - buf->size.h;
  conn_dlg->back_button = buf;
  buf->action = disconnect_conn_callback;
  set_wstate(buf, FC_WS_NORMAL);
  buf->key = SDLK_ESCAPE;
  add_to_gui_list(ID_BUTTON, buf);
  back_button = buf;

  buf = create_themeicon_button_from_chars_fonto(current_theme->ok_icon,
                                                 pwindow->dst,
                                                 _("Start"),
                                                 FONTO_ATTENTION, 0);
  buf->size.x = pwindow->size.w - adj_size(10) - buf->size.w;
  buf->size.y = back_button->size.y;
  conn_dlg->start_button = buf;
  buf->action = start_game_callback;
  set_wstate(buf, FC_WS_NORMAL);
  add_to_gui_list(ID_BUTTON, buf);
  start_game_button = buf;

  buf = create_themeicon_button_from_chars_fonto(NULL, pwindow->dst,
                                                 _("Pick Nation"),
                                                 FONTO_ATTENTION, 0);
  buf->size.h = start_game_button->size.h;
  buf->size.x = start_game_button->size.x - adj_size(10) - buf->size.w;
  buf->size.y = start_game_button->size.y;
  conn_dlg->select_nation_button = buf;
  buf->action = select_nation_callback;
  set_wstate(buf, FC_WS_NORMAL);
  add_to_gui_list(ID_BUTTON, buf);
  select_nation_button = buf;

  buf = create_themeicon_button_from_chars_fonto(NULL, pwindow->dst,
                                                 _("Load Game"),
                                                 FONTO_ATTENTION, 0);
  buf->size.h = select_nation_button->size.h;
  buf->size.x = select_nation_button->size.x - adj_size(10) - buf->size.w;
  buf->size.y = select_nation_button->size.y;
  conn_dlg->load_game_button = buf;
  buf->action = load_game_callback;
  set_wstate(buf, FC_WS_NORMAL);
  add_to_gui_list(ID_BUTTON, buf);

  /* Not implemented yet */
#if 0
  buf = create_themeicon_button_from_chars_fonto(NULL, pwindow->dst,
                                                 _("Server Settings"),
                                                 FONTO_ATTENTION, 0);
  buf->size.h = select_nation_button->size.h;
  buf->size.x = select_nation_button->size.x - adj_size(10) - buf->size.w;
  buf->size.y = select_nation_button->size.y;
  conn_dlg->configure = buf;
  buf->action = server_config_callback;
  set_wstate(buf, FC_WS_DISABLED);
  add_to_gui_list(ID_BUTTON, buf);
  server_settings_button = buf;
#endif /* 0 */

  /* Not implemented yet */
#if 0
  buf = create_themeicon_button_from_chars_fonto(NULL, pwindow->dst->surface,
                                                 "?", FONTO_ATTENTION, 0);
  buf->size.y = pwindow->size.y + pwindow->size.h - (buf->size.h + 7);
  buf->size.x = pwindow->size.x + pwindow->size.w - (buf->size.w + 10) - 5;

  buf->action = client_config_callback;
  set_wstate(buf, FC_WS_NORMAL);
  add_to_gui_list(ID_BUTTON, buf);
#endif /* 0 */

  conn_dlg->begin_widget_list = buf;
  /* ------------------------------------------------------------ */

  conn_list_dialog_update();
}

/**********************************************************************//**
  Close connection list dialog.
**************************************************************************/
bool popdown_conn_list_dialog(void)
{
  if (conn_dlg) {
    if (get_wstate(conn_dlg->pedit) == FC_WS_PRESSED) {
      force_exit_from_event_loop();
    }

    popdown_window_group_dialog(conn_dlg->begin_widget_list,
                                conn_dlg->end_widget_list);
    if (conn_dlg->users_dlg) {
      FC_FREE(conn_dlg->users_dlg->scroll);
      FC_FREE(conn_dlg->users_dlg);
    }

    if (conn_dlg->chat_dlg) {
      FC_FREE(conn_dlg->chat_dlg->scroll);
      FC_FREE(conn_dlg->chat_dlg);
    }

    FC_FREE(conn_dlg);

    return TRUE;
  }

  return FALSE;
}

/**********************************************************************//**
  Got version message from metaserver thread.
**************************************************************************/
void version_message(const char *vertext)
{
  output_window_append(ftc_client, vertext);
}
