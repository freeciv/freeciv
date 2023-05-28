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

/**********************************************************************
                          messagewin.c  -  description
                             -------------------
    begin                : Feb 2 2003
    copyright            : (C) 2003 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
 **********************************************************************/

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
#include "fcintl.h"
#include "log.h"

/* client */
#include "options.h"

/* gui-sdl2 */
#include "citydlg.h"
#include "colors.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_tilespec.h"
#include "mapview.h"
#include "themespec.h"
#include "utf8string.h"
#include "widget.h"

#include "messagewin.h"


#ifdef GUI_SDL2_SMALL_SCREEN
#define N_MSG_VIEW               3    /* max before scrolling happens */
#else
#define N_MSG_VIEW		 6
#endif

/* 0 -> use theme default */
#define PTSIZE_LOG_FONT		0

static struct advanced_dialog *msg_dlg = NULL;

/**********************************************************************//**
  Called from default clicks on a message.
**************************************************************************/
static int msg_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    int message_index = *(int*)pwidget->data.ptr;

    pwidget->string_utf8->fgcol = *get_theme_color(COLOR_THEME_MESWIN_ACTIVE_TEXT2);
    unselect_widget_action();

    meswin_double_click(message_index);
    meswin_set_visited_state(message_index, TRUE);
  }

  return -1;
}

/**********************************************************************//**
  Called from default clicks on a messages window.
**************************************************************************/
static int move_msg_window_callback(struct widget *pwindow)
{
  if (PRESSED_EVENT(main_data.event)) {
    move_window_group(msg_dlg->begin_widget_list, pwindow);
  }

  return -1;
}

/* ======================================================================
                                Public
   ====================================================================== */

/**********************************************************************//**
  Really update message window.
**************************************************************************/
void real_meswin_dialog_update(void *unused)
{
  int msg_count;
  int current_count;
  const struct message *msg = NULL;
  struct widget *buf = NULL, *pwindow = NULL;
  utf8_str *pstr = NULL;
  SDL_Rect area = {0, 0, 0, 0};
  bool create;
  int label_width;

  if (msg_dlg == NULL) {
    meswin_dialog_popup(TRUE);
  }

  msg_count = meswin_get_num_messages();
  current_count = msg_dlg->scroll->count;

  if (current_count > 0) {
    undraw_group(msg_dlg->begin_active_widget_list, msg_dlg->end_active_widget_list);
    del_group_of_widgets_from_gui_list(msg_dlg->begin_active_widget_list,
                                       msg_dlg->end_active_widget_list);
    msg_dlg->begin_active_widget_list = NULL;
    msg_dlg->end_active_widget_list = NULL;
    msg_dlg->active_widget_list = NULL;
    /* hide scrollbar */
    hide_scrollbar(msg_dlg->scroll);
    msg_dlg->scroll->count = 0;
    current_count = 0;
  }
  create = (current_count == 0);

  pwindow = msg_dlg->end_widget_list;

  area = pwindow->area;

  label_width = area.w - msg_dlg->scroll->up_left_button->size.w - adj_size(3);

  if (msg_count > 0) {
    for (; current_count < msg_count; current_count++) {
      msg = meswin_get_message(current_count);
      pstr = create_utf8_from_char(msg->descr, PTSIZE_LOG_FONT);

      if (convert_utf8_str_to_const_surface_width(pstr, label_width - adj_size(10))) {
        /* string must be divided to fit into the given area */
        utf8_str *pstr2;
        char **utf8_texts = create_new_line_utf8strs(pstr->text);
        int count = 0;

        while (utf8_texts[count]) {
          pstr2 = create_utf8_str(utf8_texts[count],
                                  strlen(utf8_texts[count]) + 1, PTSIZE_LOG_FONT);

          buf = create_iconlabel(NULL, pwindow->dst, pstr2,
                   (WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE|WF_FREE_DATA));

          /* this block is duplicated in the "else" branch */
          {
            buf->string_utf8->bgcol = (SDL_Color) {0, 0, 0, 0};

            buf->size.w = label_width;
            buf->data.ptr = fc_calloc(1, sizeof(int));
            *(int*)buf->data.ptr = current_count;
            buf->action = msg_callback;
            if (msg->tile) {
              set_wstate(buf, FC_WS_NORMAL);
              if (msg->visited) {
                buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_MESWIN_ACTIVE_TEXT2);
              } else {
                buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_MESWIN_ACTIVE_TEXT);
              }
            }

            buf->id = ID_LABEL;

            widget_set_area(buf, area);

            /* add to widget list */
            if (create) {
              add_widget_to_vertical_scroll_widget_list(msg_dlg, buf, pwindow, FALSE,
                                                        area.x, area.y);
              create = FALSE;
            } else {
              add_widget_to_vertical_scroll_widget_list(msg_dlg, buf,
                                                        msg_dlg->begin_active_widget_list,
                                                        FALSE, area.x, area.y);
            }
          }
          count++;
        } /* while */
        FREEUTF8STR(pstr);
      } else {
        buf = create_iconlabel(NULL, pwindow->dst, pstr,
                  (WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE|WF_FREE_DATA));

        /* duplicated block */
        {
          buf->string_utf8->bgcol = (SDL_Color) {0, 0, 0, 0};

          buf->size.w = label_width;
          buf->data.ptr = fc_calloc(1, sizeof(int));
          *(int*)buf->data.ptr = current_count;
          buf->action = msg_callback;
          if (msg->tile) {
            set_wstate(buf, FC_WS_NORMAL);
            if (msg->visited) {
              buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_MESWIN_ACTIVE_TEXT2);
            } else {
              buf->string_utf8->fgcol = *get_theme_color(COLOR_THEME_MESWIN_ACTIVE_TEXT);
            }
          }

          buf->id = ID_LABEL;

          widget_set_area(buf, area);

          /* add to widget list */
          if (create) {
            add_widget_to_vertical_scroll_widget_list(msg_dlg, buf,
                                                      pwindow, FALSE,
                                                      area.x, area.y);
            create = FALSE;
          } else {
            add_widget_to_vertical_scroll_widget_list(msg_dlg, buf,
                                                      msg_dlg->begin_active_widget_list,
                                                      FALSE, area.x, area.y);
          }
        }
      } /* if */
    } /* for */
  } /* if */

  redraw_group(msg_dlg->begin_widget_list, pwindow, 0);
  widget_flush(pwindow);
}

/**********************************************************************//**
  Popup (or raise) the message dialog; typically triggered by 'F9'.
**************************************************************************/
void meswin_dialog_popup(bool raise)
{
  utf8_str *pstr;
  struct widget *pwindow = NULL;
  SDL_Surface *background;
  SDL_Rect area;
  SDL_Rect size;

  if (msg_dlg) {
    return;
  }

  msg_dlg = fc_calloc(1, sizeof(struct advanced_dialog));

  /* Create window */
  pstr = create_utf8_from_char_fonto(_("Messages"), FONTO_ATTENTION);
  pstr->style = TTF_STYLE_BOLD;

  pwindow = create_window_skeleton(NULL, pstr, 0);

  pwindow->action = move_msg_window_callback;
  set_wstate(pwindow, FC_WS_NORMAL);
  add_to_gui_list(ID_CHATLINE_WINDOW, pwindow);

  msg_dlg->end_widget_list = pwindow;
  msg_dlg->begin_widget_list = pwindow;

  /* create scrollbar */
  create_vertical_scrollbar(msg_dlg, 1, N_MSG_VIEW, TRUE, TRUE);

  pstr = create_utf8_from_char("sample text", PTSIZE_LOG_FONT);

  /* define content area */
  area.w = (adj_size(520) - (pwindow->size.w - pwindow->area.w));
  utf8_str_size(pstr, &size);
  area.h = (N_MSG_VIEW + 1) * size.h;

  FREEUTF8STR(pstr);

  /* create window background */
  background = theme_get_background(active_theme, BACKGROUND_MESSAGEWIN);
  if (resize_window(pwindow, background, NULL,
                    (pwindow->size.w - pwindow->area.w) + area.w,
                    (pwindow->size.h - pwindow->area.h) + area.h)) {
    FREESURFACE(background);
  }

  area = pwindow->area;

  setup_vertical_scrollbar_area(msg_dlg->scroll,
                                area.x + area.w, area.y,
                                area.h, TRUE);

  hide_scrollbar(msg_dlg->scroll);

  widget_set_position(pwindow, (main_window_width() - pwindow->size.w)/2, adj_size(25));

  widget_redraw(pwindow);

  real_meswin_dialog_update(NULL);
}

/**********************************************************************//**
  Popdown the messages dialog; called by popdown_all_game_dialogs()
**************************************************************************/
void meswin_dialog_popdown(void)
{
  if (msg_dlg) {
    popdown_window_group_dialog(msg_dlg->begin_widget_list,
                                msg_dlg->end_widget_list);
    FC_FREE(msg_dlg->scroll);
    FC_FREE(msg_dlg);
  }
}

/**********************************************************************//**
  Return whether the message dialog is open.
**************************************************************************/
bool meswin_dialog_is_open(void)
{
  return (msg_dlg != NULL);
}
