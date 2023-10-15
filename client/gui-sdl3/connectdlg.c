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
                          connectdlg.c  -  description
                             -------------------
    begin                : Mon Jul 1 2002
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <stdlib.h>

/* SDL3 */
#include <SDL3/SDL.h>

/* utility */
#include "fcintl.h"
#include "log.h"

/* client */
#include "client_main.h"
#include "clinet.h"        /* connect_to_server() */
#include "packhand.h"
#include "servers.h"
#include "update_queue.h"

/* gui-sdl3 */
#include "chatline.h"
#include "colors.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_main.h"
#include "gui_tilespec.h"
#include "mapview.h"
#include "messagewin.h"
#include "optiondlg.h"
#include "themespec.h"
#include "widget.h"

#include "connectdlg.h"

static struct server_list *server_list = NULL;
static struct server_scan *server_scan = NULL;

static struct advanced_dialog *meta_server = NULL;
static struct small_dialog *connect_dlg = NULL;

static int connect_callback(struct widget *pwidget);
static int convert_portnr_callback(struct widget *pwidget);
static int convert_playername_callback(struct widget *pwidget);
static int convert_servername_callback(struct widget *pwidget);
static void popup_new_user_passwd_dialog(const char *message);

/*
  THESE FUNCTIONS ARE ONE BIG TMP SOLUTION AND SHOULD BE FULLY REWRITTEN !!
*/

/**********************************************************************//**
  Provide a packet handler for packet_game_load
**************************************************************************/
void handle_game_load(bool load_successful, const char *filename)
{
  /* PORTME */
}

/**********************************************************************//**
  User interacted with connect -widget
**************************************************************************/
static int connect_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    char errbuf[512];

    if (connect_to_server(user_name, server_host, server_port,
                          errbuf, sizeof(errbuf)) != -1) {
    } else {
      output_window_append(ftc_any, errbuf);

      /* button up */
      unselect_widget_action();
      set_wstate(pwidget, FC_WS_SELECTED);
      selected_widget = pwidget;
      widget_redraw(pwidget);
      widget_flush(pwidget);
    }
  }

  return -1;
}
/* ======================================================== */


/**********************************************************************//**
  User interacted with server list window.
**************************************************************************/
static int meta_server_window_callback(struct widget *pwindow)
{
  return -1;
}

/**********************************************************************//**
  Close servers dialog.
**************************************************************************/
static int exit_meta_server_dlg_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    queue_flush();

    server_scan_finish(server_scan);
    server_scan = NULL;
    server_list = NULL;

    set_client_page(PAGE_NETWORK);
    meswin_dialog_popup(TRUE);
  }

  return -1;
}

/**********************************************************************//**
  Server selected from dialog.
**************************************************************************/
static int select_meta_servers_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct server *server = (struct server *)pwidget->data.ptr;

    sz_strlcpy(server_host, server->host);
    server_port = server->port;

    exit_meta_server_dlg_callback(NULL);
  }

  return -1;
}

/**********************************************************************//**
  Callback function for when there's an error in the server scan.
**************************************************************************/
static void server_scan_error(struct server_scan *scan,
                              const char *message)
{
  output_window_append(ftc_client, message);
  log_normal("%s", message);

  switch (server_scan_get_type(scan)) {
  case SERVER_SCAN_LOCAL:
    server_scan_finish(server_scan);
    server_scan = NULL;
    server_list = NULL;
    break;
  case SERVER_SCAN_GLOBAL:
    server_scan_finish(server_scan);
    server_scan = NULL;
    server_list = NULL;
    break;
  case SERVER_SCAN_LAST:
    break;
  }
}

/**********************************************************************//**
  Wrapper on server_scan_begin() and server_scan_get_list()
  functions.

  WARNING !: use "server_scan_finish()" to free server list.
**************************************************************************/
static struct srv_list *sdl_create_server_list(bool lan)
{
  struct srv_list *list = NULL;
  int i;

  if (lan) {
    server_scan = server_scan_begin(SERVER_SCAN_LOCAL, server_scan_error);
  } else {
    server_scan = server_scan_begin(SERVER_SCAN_GLOBAL, server_scan_error);
  }

  if (!server_scan) {
    return NULL;
  }

  SDL_Delay(5000);

  for (i = 0; i < 100; i++) {
    server_scan_poll(server_scan);
    list = server_scan_get_list(server_scan);
    if (list) {
      break;
    }
    SDL_Delay(100);
  }

  return list;
}

/**********************************************************************//**
  Open connection dialog for either meta or lan scan.
**************************************************************************/
void popup_connection_dialog(bool lan_scan)
{
  SDL_Color bg_color = {255, 255, 255, 128};
  char cbuf[512];
  int w = 0, h = 0, count = 0, meta_h;
  struct widget *new_widget, *pwindow, *label_window;
  utf8_str *pstr;
  SDL_Surface *logo;
  SDL_Rect area, area2;
  struct srv_list *srvrs;

  queue_flush();
  close_connection_dialog();
  meswin_dialog_popdown();

  /* Text Label */
  label_window = create_window_skeleton(NULL, NULL, 0);
  add_to_gui_list(ID_WINDOW, label_window);

  area = label_window->area;

  fc_snprintf(cbuf, sizeof(cbuf), _("Creating Server List..."));
  pstr = create_utf8_from_char_fonto(cbuf, FONTO_BIG);
  pstr->style = TTF_STYLE_BOLD;
  pstr->bgcol = (SDL_Color) {0, 0, 0, 0};
  new_widget = create_iconlabel(NULL, label_window->dst, pstr,
                (WF_RESTORE_BACKGROUND | WF_DRAW_TEXT_LABEL_WITH_SPACE));
  add_to_gui_list(ID_LABEL, new_widget);

  area.w = MAX(area.w, new_widget->size.w + (adj_size(60) -
                       (label_window->size.w - label_window->area.w)));
  area.h += new_widget->size.h + (adj_size(30) -
            (label_window->size.w - label_window->area.w));

  resize_window(label_window, NULL, &bg_color,
                (label_window->size.w - label_window->area.w) + area.w,
                (label_window->size.h - label_window->area.h) + area.h);

  area = label_window->area;

  widget_set_position(label_window,
                      (main_window_width() - label_window->size.w) / 2,
                      (main_window_height() - label_window->size.h) / 2);

  widget_set_area(new_widget, area);
  widget_set_position(new_widget,
                      area.x + (area.w - new_widget->size.w) / 2,
                      area.y + (area.h - new_widget->size.h) / 2);

  redraw_group(new_widget, label_window, TRUE);
  flush_dirty();

  /* Create server list */
  srvrs = sdl_create_server_list(lan_scan);

  /* Copy list */
  server_list = server_list_new();
  fc_mutex_allocate(&srvrs->mutex);
  server_list_iterate(srvrs->servers, pserver) {
    server_list_append(server_list, pserver);
  } server_list_iterate_end;
  fc_mutex_release(&srvrs->mutex);

  /* Clear label */
  popdown_window_group_dialog(new_widget, label_window);

  meswin_dialog_popup(TRUE);

  if (!server_list) {
    if (lan_scan) {
      output_window_append(ftc_client, _("No LAN servers found"));
    } else {
      output_window_append(ftc_client, _("No public servers found"));
    }
    set_client_page(PAGE_NETWORK);
    return;
  }

  /* Server list window */
  meta_server = fc_calloc(1, sizeof(struct advanced_dialog));

  pwindow = create_window_skeleton(NULL, NULL, 0);
  pwindow->action = meta_server_window_callback;
  set_wstate(pwindow, FC_WS_NORMAL);
  clear_wflag(pwindow, WF_DRAW_FRAME_AROUND_WIDGET);
  if (lan_scan) {
    add_to_gui_list(ID_LAN_SERVERS_WINDOW, pwindow);
  } else {
    add_to_gui_list(ID_META_SERVERS_WINDOW, pwindow);
  }
  meta_server->end_widget_list = pwindow;

  /* Cancel button */
  new_widget
    = create_themeicon_button_from_chars_fonto(current_theme->cancel_icon,
                                               pwindow->dst, _("Cancel"),
                                               FONTO_HEADING, 0);
  new_widget->action = exit_meta_server_dlg_callback;
  set_wstate(new_widget, FC_WS_NORMAL);
  add_to_gui_list(ID_BUTTON, new_widget);

  /* Servers */
  server_list_iterate(server_list, server) {

    /* TRANS: "host.example.com Port 5556 Ver: 3.1.0 Running Players 3\n
     * [server message]" */
    fc_snprintf(cbuf, sizeof(cbuf), _("%s Port %d Ver: %s %s %s %d\n%s"),
                server->host, server->port, server->version, _(server->state),
                Q_("?header:Players"), server->nplayers, server->message);

    new_widget = create_iconlabel_from_chars_fonto(NULL, pwindow->dst, cbuf,
                                                   FONTO_DEFAULT,
                     WF_FREE_STRING|WF_DRAW_TEXT_LABEL_WITH_SPACE|WF_RESTORE_BACKGROUND);

    new_widget->string_utf8->style |= SF_CENTER;
    new_widget->string_utf8->bgcol = (SDL_Color) {0, 0, 0, 0};

    new_widget->action = select_meta_servers_callback;
    set_wstate(new_widget, FC_WS_NORMAL);
    new_widget->data.ptr = (void *)server;

    add_to_gui_list(ID_BUTTON, new_widget);

    w = MAX(w, new_widget->size.w);
    h = MAX(h, new_widget->size.h);
    count++;

    if (count > 10) {
      set_wflag(new_widget, WF_HIDDEN);
    }

  } server_list_iterate_end;

  if (!count) {
    if (lan_scan) {
      output_window_append(ftc_client, _("No LAN servers found"));
    } else {
      output_window_append(ftc_client, _("No public servers found"));
    }
    set_client_page(PAGE_NETWORK);
    return;
  }

  meta_server->begin_widget_list = new_widget;
  meta_server->begin_active_widget_list = meta_server->begin_widget_list;
  meta_server->end_active_widget_list = meta_server->end_widget_list->prev->prev;
  meta_server->active_widget_list = meta_server->end_active_widget_list;

  if (count > 10) {
    meta_h = 10 * h;

    count = create_vertical_scrollbar(meta_server, 1, 10, TRUE, TRUE);
    w += count;
  } else {
    meta_h = count * h;
  }

  w += adj_size(20);
  area2.h = meta_h;

  meta_h += meta_server->end_widget_list->prev->size.h + adj_size(10) + adj_size(20);

  logo = theme_get_background(active_theme, BACKGROUND_CONNECTDLG);
  if (resize_window(pwindow, logo, NULL, w, meta_h)) {
    FREESURFACE(logo);
  }

  area = pwindow->area;

  widget_set_position(pwindow,
                      (main_window_width() - w) / 2,
                      (main_window_height() - meta_h) / 2);

  w -= adj_size(20);

  area2.w = w + 1;

  if (meta_server->scroll) {
    w -= count;
  }

  /* Exit button */
  new_widget = pwindow->prev;
  new_widget->size.x = area.x + area.w - new_widget->size.w - adj_size(10);
  new_widget->size.y = area.y + area.h - new_widget->size.h - adj_size(10);

  /* Meta labels */
  new_widget = new_widget->prev;

  new_widget->size.x = area.x + adj_size(10);
  new_widget->size.y = area.y + adj_size(10);
  new_widget->size.w = w;
  new_widget->size.h = h;
  new_widget = convert_iconlabel_to_themeiconlabel2(new_widget);

  new_widget = new_widget->prev;

  while (new_widget != NULL) {
    new_widget->size.w = w;
    new_widget->size.h = h;
    new_widget->size.x = new_widget->next->size.x;
    new_widget->size.y = new_widget->next->size.y + new_widget->next->size.h;
    new_widget = convert_iconlabel_to_themeiconlabel2(new_widget);

    if (new_widget == meta_server->begin_active_widget_list) {
      break;
    }
    new_widget = new_widget->prev;
  }

  if (meta_server->scroll) {
    setup_vertical_scrollbar_area(meta_server->scroll,
                                  area.x + area.w - adj_size(6),
                                  meta_server->end_active_widget_list->size.y,
                                  area.h - adj_size(24) - pwindow->prev->size.h, TRUE);
  }

  /* -------------------- */
  /* Redraw */

  widget_redraw(pwindow);

  area2.x = meta_server->end_active_widget_list->size.x;
  area2.y = meta_server->end_active_widget_list->size.y;

  fill_rect_alpha(pwindow->dst->surface, &area2, &bg_color);

  create_frame(pwindow->dst->surface,
               area2.x - 1, area2.y - 1, area2.w, area2.h,
               get_theme_color(COLOR_THEME_CONNECTDLG_INNERFRAME));

  redraw_group(meta_server->begin_widget_list, pwindow->prev, 0);

  create_frame(pwindow->dst->surface,
               pwindow->size.x, pwindow->size.y,
               area.w - 1, area.h - 1,
               get_theme_color(COLOR_THEME_CONNECTDLG_FRAME));

  widget_flush(pwindow);
}

/**********************************************************************//**
  User interacted with playername widget.
**************************************************************************/
static int convert_playername_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (pwidget->string_utf8->text != NULL) {
      sz_strlcpy(user_name, pwidget->string_utf8->text);
    } else {
      /* empty input -> restore previous content */
      copy_chars_to_utf8_str(pwidget->string_utf8, user_name);
      widget_redraw(pwidget);
      widget_mark_dirty(pwidget);
      flush_dirty();
    }
  }

  return -1;
}

/**********************************************************************//**
  User interacted with servername widget.
**************************************************************************/
static int convert_servername_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (pwidget->string_utf8->text != NULL) {
      sz_strlcpy(server_host, pwidget->string_utf8->text);
    } else {
      /* empty input -> restore previous content */
      copy_chars_to_utf8_str(pwidget->string_utf8, server_host);
      widget_redraw(pwidget);
      widget_mark_dirty(pwidget);
      flush_dirty();
    }
  }

  return -1;
}

/**********************************************************************//**
  User interacted with port number widget.
**************************************************************************/
static int convert_portnr_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    char port_str[6];

    if (pwidget->string_utf8->text != NULL) {
      sscanf(pwidget->string_utf8->text, "%d", &server_port);
    } else {
      /* empty input -> restore previous content */
      fc_snprintf(port_str, sizeof(port_str), "%d", server_port);
      copy_chars_to_utf8_str(pwidget->string_utf8, port_str);
      widget_redraw(pwidget);
      widget_mark_dirty(pwidget);
      flush_dirty();
    }
  }

  return -1;
}

/**********************************************************************//**
  User interacted with cancel -button
**************************************************************************/
static int cancel_connect_dlg_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    close_connection_dialog();
    set_client_page(PAGE_MAIN);
  }

  return -1;
}

/**********************************************************************//**
  Open dialog for joining to game.
**************************************************************************/
void popup_join_game_dialog(void)
{
  char port_str[6];
  struct widget *buf, *pwindow;
  utf8_str *plrname = NULL;
  utf8_str *srvname = NULL;
  utf8_str *port_nr = NULL;
  SDL_Surface *logo;
  SDL_Rect area;
  int start_x;
  int pos_y;
  int dialog_w, dialog_h;

  queue_flush();
  close_connection_dialog();

  connect_dlg = fc_calloc(1, sizeof(struct small_dialog));

  /* Window */
  pwindow = create_window_skeleton(NULL, NULL, 0);
  add_to_gui_list(ID_WINDOW, pwindow);
  connect_dlg->end_widget_list = pwindow;

  area = pwindow->area;

  /* Player name label */
  plrname = create_utf8_from_char_fonto(_("Player Name :"), FONTO_DEFAULT);
  plrname->fgcol = *get_theme_color(COLOR_THEME_JOINGAMEDLG_TEXT);
  buf = create_iconlabel(NULL, pwindow->dst, plrname,
          (WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE));
  add_to_gui_list(ID_LABEL, buf);
  area.h += buf->size.h + adj_size(20);

  /* Player name edit */
  buf = create_edit_from_chars_fonto(NULL, pwindow->dst, user_name,
                                     FONTO_HEADING,
                                     adj_size(210),
                                     (WF_RESTORE_BACKGROUND|WF_FREE_DATA));
  buf->action = convert_playername_callback;
  set_wstate(buf, FC_WS_NORMAL);
  add_to_gui_list(ID_PLAYER_NAME_EDIT, buf);
  area.h += buf->size.h + adj_size(5);

  /* Server name label */
  srvname = create_utf8_from_char_fonto(_("Freeciv Server :"), FONTO_DEFAULT);
  srvname->fgcol = *get_theme_color(COLOR_THEME_JOINGAMEDLG_TEXT);
  buf = create_iconlabel(NULL, pwindow->dst, srvname,
          (WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE));
  add_to_gui_list(ID_LABEL, buf);
  area.h += buf->size.h + adj_size(5);

  /* Server name edit */
  buf = create_edit_from_chars_fonto(NULL, pwindow->dst, server_host,
                                     FONTO_HEADING,
                                     adj_size(210), WF_RESTORE_BACKGROUND);

  buf->action = convert_servername_callback;
  set_wstate(buf, FC_WS_NORMAL);
  add_to_gui_list(ID_SERVER_NAME_EDIT, buf);
  area.h += buf->size.h + adj_size(5);

  /* Port label */
  port_nr = create_utf8_from_char_fonto(_("Port :"), FONTO_DEFAULT);
  port_nr->fgcol = *get_theme_color(COLOR_THEME_JOINGAMEDLG_TEXT);
  buf = create_iconlabel(NULL, pwindow->dst, port_nr,
          (WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE));
  add_to_gui_list(ID_LABEL, buf);
  area.h += buf->size.h + adj_size(5);

  /* Port edit */
  fc_snprintf(port_str, sizeof(port_str), "%d", server_port);

  buf = create_edit_from_chars_fonto(NULL, pwindow->dst, port_str,
                                     FONTO_HEADING,
                                     adj_size(210),
                                     WF_RESTORE_BACKGROUND);

  buf->action = convert_portnr_callback;
  set_wstate(buf, FC_WS_NORMAL);
  add_to_gui_list(ID_PORT_EDIT, buf);
  area.h += buf->size.h + adj_size(20);

  /* Connect button */
  buf = create_themeicon_button_from_chars_fonto(current_theme->ok_icon,
                                                 pwindow->dst,
                                                 _("Connect"),
                                                 FONTO_HEADING, 0);
  buf->action = connect_callback;
  set_wstate(buf, FC_WS_NORMAL);
  buf->key = SDLK_RETURN;
  add_to_gui_list(ID_CONNECT_BUTTON, buf);

  /* Cancel button */
  buf = create_themeicon_button_from_chars_fonto(current_theme->cancel_icon,
                                                 pwindow->dst, _("Cancel"),
                                                 FONTO_HEADING, 0);
  buf->action = cancel_connect_dlg_callback;
  set_wstate(buf, FC_WS_NORMAL);
  buf->key = SDLK_ESCAPE;
  add_to_gui_list(ID_CANCEL_BUTTON, buf);
  buf->size.w = MAX(buf->size.w, buf->next->size.w);
  buf->next->size.w = buf->size.w;
  area.h += buf->size.h + adj_size(10);
  /* ------------------------------ */

  connect_dlg->begin_widget_list = buf;

  dialog_w = MAX(adj_size(40) + buf->size.w * 2, adj_size(210)) + adj_size(80);

#ifdef GUI_SDL3_SMALL_SCREEN
  dialog_h = area.h + (pwindow->size.h - pwindow->area.h);
#else
  dialog_h = area.h + (pwindow->size.h - pwindow->area.h);
#endif

  logo = theme_get_background(active_theme, BACKGROUND_JOINGAMEDLG);
  if (resize_window(pwindow, logo, NULL, dialog_w, dialog_h)) {
    FREESURFACE(logo);
  }

  area = pwindow->area;

  widget_set_position(pwindow,
                      (main_window_width() - pwindow->size.w) / 2,
                      (main_window_height() - pwindow->size.h) / 2 + adj_size(40));

  /* player name label */
  buf = connect_dlg->end_widget_list->prev;

  start_x = area.x + (area.w - buf->prev->size.w) / 2;
  pos_y = area.y + adj_size(20);

  buf->size.x = start_x + adj_size(5);
  buf->size.y = pos_y;

  pos_y += buf->size.h;

  buf = buf->prev;
  buf->size.x = start_x;
  buf->size.y = pos_y;

  pos_y += buf->size.h + adj_size(5);

  /* server name label */
  buf = buf->prev;
  buf->size.x = start_x + adj_size(5);
  buf->size.y = pos_y;

  pos_y += buf->size.h;

  /* server name edit */
  buf = buf->prev;
  buf->size.x = start_x;
  buf->size.y = pos_y;

  pos_y += buf->size.h + adj_size(5);

  /* port label */
  buf = buf->prev;
  buf->size.x = start_x + adj_size(5);
  buf->size.y = pos_y;

  pos_y += buf->size.h;

  /* port edit */
  buf = buf->prev;
  buf->size.x = start_x;
  buf->size.y = pos_y;

  pos_y += buf->size.h + adj_size(20);

  /* connect button */
  buf = buf->prev;
  buf->size.x = area.x + (dialog_w - (adj_size(40) + buf->size.w * 2)) / 2;
  buf->size.y = pos_y;

  /* cancel button */
  buf = buf->prev;
  buf->size.x = buf->next->size.x + buf->size.w + adj_size(40);
  buf->size.y = pos_y;

  redraw_group(connect_dlg->begin_widget_list, connect_dlg->end_widget_list,
               FALSE);

  flush_all();
}

/**********************************************************************//**
  User interacted with password widget
**************************************************************************/
static int convert_passwd_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (pwidget->string_utf8->text != NULL) {
      fc_snprintf(fc_password, MAX_LEN_NAME, "%s", pwidget->string_utf8->text);
    }
  }

  return -1;
}

/**********************************************************************//**
  User interacted with "Next" -button after entering password.
**************************************************************************/
static int send_passwd_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    struct packet_authentication_reply reply;

    sz_strlcpy(reply.password, fc_password);

    memset(fc_password, 0, MAX_LEN_NAME);
    fc_password[0] = '\0';

    set_wstate(pwidget, FC_WS_DISABLED);

    widget_redraw(pwidget);

    widget_mark_dirty(pwidget);

    flush_dirty();

    send_packet_authentication_reply(&client.conn, &reply);
  }

  return -1;
}

/**********************************************************************//**
  Close password dialog with no password given.
**************************************************************************/
static int cancel_passwd_callback(struct widget *pwidget)
{
  memset(fc_password, 0, MAX_LEN_NAME);
  fc_password[0] = '\0';
  disconnect_from_server(TRUE);

  return cancel_connect_dlg_callback(pwidget);
}

/**********************************************************************//**
  Open password dialog.
**************************************************************************/
static void popup_user_passwd_dialog(const char *message)
{
  struct widget *buf, *pwindow;
  utf8_str *label_str = NULL;
  SDL_Surface *background;
  int start_x, start_y;
  int start_button_y;
  SDL_Rect area;

  queue_flush();
  close_connection_dialog();

  connect_dlg = fc_calloc(1, sizeof(struct small_dialog));

  pwindow = create_window_skeleton(NULL, NULL, 0);
  add_to_gui_list(ID_WINDOW, pwindow);
  connect_dlg->end_widget_list = pwindow;

  area = pwindow->area;

  /* Text label */
  label_str = create_utf8_from_char_fonto(message, FONTO_ATTENTION);
  label_str->fgcol = *get_theme_color(COLOR_THEME_USERPASSWDDLG_TEXT);
  buf = create_iconlabel(NULL, pwindow->dst, label_str,
                          (WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE));
  add_to_gui_list(ID_LABEL, buf);
  area.h += adj_size(10) + buf->size.h + adj_size(5);

  /* Password edit */
  buf = create_edit(NULL, pwindow->dst,
                    create_utf8_str_fonto(NULL, 0, FONTO_BIG),
                    adj_size(210),
                    (WF_PASSWD_EDIT|WF_RESTORE_BACKGROUND|WF_FREE_DATA));
  buf->action = convert_passwd_callback;
  set_wstate(buf, FC_WS_NORMAL);
  add_to_gui_list(ID_EDIT, buf);
  area.h += buf->size.h + adj_size(10);

  /* Next button */
  buf = create_themeicon_button_from_chars_fonto(current_theme->ok_icon,
                                                 pwindow->dst,
                                                 _("Next"),
                                                 FONTO_HEADING, 0);
  buf->action = send_passwd_callback;
  set_wstate(buf, FC_WS_NORMAL);
  buf->key = SDLK_RETURN;
  add_to_gui_list(ID_BUTTON, buf);

  /* Cancel button */
  buf = create_themeicon_button_from_chars_fonto(current_theme->cancel_icon,
                                                 pwindow->dst,
                                                 _("Cancel"),
                                                 FONTO_HEADING, 0);
  buf->action = cancel_passwd_callback;
  set_wstate(buf, FC_WS_NORMAL);
  buf->key = SDLK_ESCAPE;
  add_to_gui_list(ID_CANCEL_BUTTON, buf);
  buf->size.w = MAX(buf->size.w, buf->next->size.w);
  buf->next->size.w = buf->size.w;
  area.h += buf->size.h + adj_size(10);

  /* ------------------------------ */

  connect_dlg->begin_widget_list = buf;

  area.w = MAX(area.w, buf->size.w * 2 + adj_size(40));
  area.w = MAX(area.w, adj_size(210) - (pwindow->size.w - pwindow->area.w));
  area.w = MAX(area.w, pwindow->prev->size.w);
  area.w += adj_size(80);

  background = theme_get_background(active_theme, BACKGROUND_USERPASSWDDLG);
  if (resize_window(pwindow, background, NULL,
                    (pwindow->size.w - pwindow->area.w) + area.w,
                    (pwindow->size.h - pwindow->area.h) + area.h)) {
    FREESURFACE(background);
  }

  area = pwindow->area;

  widget_set_position(pwindow,
                      (main_window_width() - pwindow->size.w) / 2,
                      (main_window_height() - pwindow->size.h) / 2);

  /* Text label */
  buf = connect_dlg->end_widget_list->prev;

  start_x = area.x + (area.w - buf->size.w) / 2;
  start_y = area.y + adj_size(10);

  widget_set_area(buf, area);
  widget_set_position(buf, start_x, start_y);

  start_y += buf->size.h + adj_size(5);

  /* Password edit */
  buf = buf->prev;
  start_x = area.x + (area.w - buf->size.w) / 2;

  widget_set_area(buf, area);
  widget_set_position(buf, start_x, start_y);

  /* --------------------------------- */
  start_button_y = buf->size.y + buf->size.h + adj_size(10);

  /* connect button */
  buf = buf->prev;
  widget_set_area(buf, area);
  widget_set_position(buf,
                      area.x + (area.w - (adj_size(40) + buf->size.w * 2)) / 2,
                      start_button_y);

  /* cancel button */
  buf = buf->prev;
  widget_set_area(buf, area);
  widget_set_position(buf,
                      buf->next->size.x + buf->size.w + adj_size(40),
                      start_button_y);

  redraw_group(connect_dlg->begin_widget_list, connect_dlg->end_widget_list,
               FALSE);

  flush_all();
}

/**********************************************************************//**
  New Password
**************************************************************************/
static int convert_first_passwd_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (pwidget->string_utf8->text != NULL) {
      fc_snprintf(fc_password, MAX_LEN_NAME, "%s", pwidget->string_utf8->text);
      set_wstate(pwidget->prev, FC_WS_NORMAL);
      widget_redraw(pwidget->prev);
      widget_flush(pwidget->prev);
    }
  }

  return -1;
}

/**********************************************************************//**
  Verify Password
**************************************************************************/
static int convert_second_passwd_callback(struct widget *pwidget)
{
  if (PRESSED_EVENT(main_data.event)) {
    if (pwidget->string_utf8->text != NULL
        && !fc_strncmp(fc_password, pwidget->string_utf8->text,
                       MAX_LEN_NAME)) {
      set_wstate(pwidget->prev, FC_WS_NORMAL); /* Next button */
      widget_redraw(pwidget->prev);
      widget_flush(pwidget->prev);
    } else {
      memset(fc_password, 0, MAX_LEN_NAME);
      fc_password[0] = '\0';

      FC_FREE(pwidget->next->string_utf8->text);/* First edit */
      FC_FREE(pwidget->string_utf8->text); /* Second edit */

      popup_new_user_passwd_dialog(_("Passwords don't match, enter password."));
    }
  }

  return -1;
}

/**********************************************************************//**
  Open dialog for new password.
**************************************************************************/
static void popup_new_user_passwd_dialog(const char *message)
{
  struct widget *buf, *pwindow;
  utf8_str *label_str = NULL;
  SDL_Surface *background;
  int start_x, start_y;
  int start_button_y;
  SDL_Rect area;

  queue_flush();
  close_connection_dialog();

  connect_dlg = fc_calloc(1, sizeof(struct small_dialog));

  pwindow = create_window_skeleton(NULL, NULL, 0);
  add_to_gui_list(ID_WINDOW, pwindow);
  connect_dlg->end_widget_list = pwindow;

  area = pwindow->area;

  /* Text label */
  label_str = create_utf8_from_char_fonto(message, FONTO_ATTENTION);
  label_str->fgcol = *get_theme_color(COLOR_THEME_USERPASSWDDLG_TEXT);
  buf = create_iconlabel(NULL, pwindow->dst, label_str,
                          (WF_RESTORE_BACKGROUND|WF_DRAW_TEXT_LABEL_WITH_SPACE));
  add_to_gui_list(ID_LABEL, buf);
  area.h += adj_size(10) + buf->size.h + adj_size(5);

  /* Password edit */
  buf = create_edit(NULL, pwindow->dst,
                    create_utf8_str_fonto(NULL, 0, FONTO_BIG),
                    adj_size(210),
                    (WF_PASSWD_EDIT|WF_RESTORE_BACKGROUND|WF_FREE_DATA));
  buf->action = convert_first_passwd_callback;
  set_wstate(buf, FC_WS_NORMAL);
  add_to_gui_list(ID_EDIT, buf);
  area.h += buf->size.h + adj_size(5);

  /* Second password edit */
  buf = create_edit(NULL, pwindow->dst,
                    create_utf8_str_fonto(NULL, 0, FONTO_BIG),
                    adj_size(210),
                    (WF_PASSWD_EDIT|WF_RESTORE_BACKGROUND|WF_FREE_DATA));
  buf->action = convert_second_passwd_callback;
  add_to_gui_list(ID_EDIT, buf);
  area.h += buf->size.h + adj_size(10);

  /* Next button */
  buf = create_themeicon_button_from_chars_fonto(current_theme->ok_icon,
                                                 pwindow->dst,
                                                 _("Next"),
                                                 FONTO_HEADING, 0);
  buf->action = send_passwd_callback;
  buf->key = SDLK_RETURN;
  add_to_gui_list(ID_BUTTON, buf);

  /* Cancel button */
  buf = create_themeicon_button_from_chars_fonto(current_theme->cancel_icon,
                                                 pwindow->dst, _("Cancel"),
                                                 FONTO_HEADING, 0);
  buf->action = cancel_passwd_callback;
  set_wstate(buf, FC_WS_NORMAL);
  buf->key = SDLK_ESCAPE;
  add_to_gui_list(ID_CANCEL_BUTTON, buf);
  buf->size.w = MAX(buf->size.w, buf->next->size.w);
  buf->next->size.w = buf->size.w;
  area.h += buf->size.h + adj_size(10);

  /* ------------------------------ */

  connect_dlg->begin_widget_list = buf;

  area.w = buf->size.w * 2 + adj_size(40);
  area.w = MAX(area.w, adj_size(210) - (pwindow->size.w - pwindow->area.w));
  area.w = MAX(area.w, pwindow->prev->size.w);
  area.w += adj_size(80);

  background = theme_get_background(active_theme, BACKGROUND_USERPASSWDDLG);
  if (resize_window(pwindow, background, NULL,
                    (pwindow->size.w - pwindow->area.w) + area.w,
                    (pwindow->size.h - pwindow->area.h) + area.h)) {
    FREESURFACE(background);
  }

  area = pwindow->area;

  widget_set_position(pwindow,
                      (main_window_width() - pwindow->size.w) / 2,
                      (main_window_height() - pwindow->size.h) / 2);

  /* text label */
  buf = connect_dlg->end_widget_list->prev;

  start_x = area.x + (area.w - buf->size.w) / 2;
  start_y = area.y + adj_size(10);

  widget_set_area(buf, area);
  widget_set_position(buf, start_x, start_y);

  start_y += buf->size.h + adj_size(5);

  /* passwd edit */
  buf = buf->prev;
  start_x = area.x + (area.w - buf->size.w) / 2;

  widget_set_area(buf, area);
  widget_set_position(buf, start_x, start_y);

  start_y += buf->size.h + adj_size(5);

  /* retype passwd */
  buf = buf->prev;
  widget_set_area(buf, area);
  widget_set_position(buf, start_x, start_y);

  start_button_y = buf->size.y + buf->size.h + adj_size(10);

  /* connect button */
  buf = buf->prev;
  widget_set_area(buf, area);
  widget_set_position(buf,
                      area.x + (area.w - (adj_size(40) + buf->size.w * 2)) / 2,
                      start_button_y);

  /* cancel button */
  buf = buf->prev;
  widget_set_area(buf, area);
  widget_set_position(buf,
                      buf->next->size.x + buf->size.w + adj_size(40),
                      start_button_y);

  redraw_group(connect_dlg->begin_widget_list, connect_dlg->end_widget_list,
               FALSE);

  flush_all();
}

/* ======================================================================== */

/**********************************************************************//**
  Close and destroy the dialog.
**************************************************************************/
void close_connection_dialog(void)
{
  if (connect_dlg) {
    popdown_window_group_dialog(connect_dlg->begin_widget_list,
                                connect_dlg->end_widget_list);
    FC_FREE(connect_dlg);
  }
  if (meta_server) {
    popdown_window_group_dialog(meta_server->begin_widget_list,
                                meta_server->end_widget_list);
    FC_FREE(meta_server->scroll);
    FC_FREE(meta_server);

    if (server_list) {
      server_scan_finish(server_scan);
      server_scan = NULL;
      server_list = NULL;
    }
  }
}

/**********************************************************************//**
  Popup passwd dialog depending on what type of authentication request the
  server is making.
**************************************************************************/
void handle_authentication_req(enum authentication_type type,
                               const char *message)
{
  switch (type) {
  case AUTH_NEWUSER_FIRST:
  case AUTH_NEWUSER_RETRY:
    popup_new_user_passwd_dialog(message);
    return;
  case AUTH_LOGIN_FIRST:
    /* if we magically have a password already present in 'fc_password'
     * then, use that and skip the password entry dialog */
    if (fc_password[0] != '\0') {
      struct packet_authentication_reply reply;

      sz_strlcpy(reply.password, fc_password);
      send_packet_authentication_reply(&client.conn, &reply);

      return;
    } else {
      popup_user_passwd_dialog(message);
    }

    return;
  case AUTH_LOGIN_RETRY:
    popup_user_passwd_dialog(message);

    return;
  }

  log_error("Not supported authentication type %d: %s.", type, message);
}

/**********************************************************************//**
  Provide an interface for connecting to a Freeciv server.
  sdl3-client does it as popup main start menu which != connecting dlg.
**************************************************************************/
void server_connect(void)
{
}
