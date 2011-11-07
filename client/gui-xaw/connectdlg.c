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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/SimpleMenu.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/AsciiText.h>  
#include <X11/Xaw/List.h>
#include <X11/Xaw/Viewport.h>

/* common & utility */
#include "fcintl.h"
#include "log.h"
#include "mem.h"          /* fc_strdup() */
#include "support.h"
#include "version.h"

/* client */
#include "client_main.h"
#include "clinet.h"       /* connect_to_server() */
#include "packhand.h"
#include "servers.h"

#include "chatline.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "pages.h"

#include "connectdlg_common.h"
#include "connectdlg.h"


enum connection_state {
  LOGIN_TYPE,
  NEW_PASSWORD_TYPE,
  ENTER_PASSWORD_TYPE,
  WAITING_TYPE
};

static enum connection_state connection_status;


static Widget connectdlg_shell;
static Widget connectdlg_form;
#if IS_BETA_VERSION
static Widget connectdlg_beta_label;
#endif
static Widget connectdlg_select_label;
static Widget connectdlg_host_label;
static Widget connectdlg_host_text;
static Widget connectdlg_port_label;
static Widget connectdlg_port_text;
static Widget connectdlg_login_label;
static Widget connectdlg_login_text;
static Widget connectdlg_password_label;
static Widget connectdlg_password_text;
static Widget connectdlg_verify_label;
static Widget connectdlg_verify_text;
static Widget connectdlg_message_label;
static Widget connectdlg_connect_button;
static Widget connectdlg_lan_button;
static Widget connectdlg_meta_button;
static Widget connectdlg_quit_button;

void connectdlg_connect_callback(Widget w, XtPointer client_data,
                                 XtPointer call_data);
void connectdlg_lan_callback(Widget w, XtPointer client_data,
                             XtPointer call_data);
void connectdlg_meta_callback(Widget w, XtPointer client_data,
                              XtPointer call_data);
void connectdlg_quit_callback(Widget w, XtPointer client_data,
                              XtPointer call_data);

/****************************************************************/

/* Serverlist */
static Widget connectdlg_serverlist_shell;

static Widget connectdlg_serverlist_form;
static Widget connectdlg_serverlist_legend_label;
static Widget connectdlg_serverlist_viewport;
static Widget connectdlg_serverlist_list;
static Widget connectdlg_serverlist_update_button;
static Widget connectdlg_serverlist_close_button;

static struct server_scan *lan_scan, *meta_scan;

void connectdlg_serverlist_popup(void);
void server_scan_error(struct server_scan *scan, const char *message);
void connectdlg_serverlist_update_callback(Widget w, XtPointer client_data,
                                           XtPointer call_data);
void connectdlg_serverlist_close_callback(Widget w, XtPointer client_data,
                                          XtPointer call_data);
void connectdlg_serverlist_list_callback(Widget w, XtPointer client_data,
                                         XtPointer call_data);

/* FIXME: Replace magic 64 with proper constant. */
static char *servers_list[64]={NULL};

static void server_list_timer(XtPointer client_data, XtIntervalId * id);
static int get_server_list(char **list, char *errbuf, int n_errbuf);

static bool lan_mode;  /* true for LAN mode, false when Meta mode */
static int num_lanservers_timer = 0;


/****************************************************************************
  Aligns widths of connect dialog labels and texts.
****************************************************************************/
static void connectdlg_align_labels(void)
{
  Dimension width, s_width, t_width, max_width = 0;

#if IS_BETA_VERSION
  XtVaGetValues(connectdlg_beta_label, XtNwidth, &width, NULL);
  max_width = width;
#endif

  XtVaGetValues(connectdlg_select_label, XtNwidth, &width, NULL);
  if (width > max_width) {
    max_width = width;
  }
  XtVaGetValues(connectdlg_message_label, XtNwidth, &width, NULL);
  if (width > max_width) {
    max_width = width;
  }

  /* FIXME: Replace 7 with proper XtN* variable value */
  XtVaGetValues(connectdlg_host_label, XtNwidth, &width, NULL);
  XtVaGetValues(connectdlg_host_text, XtNwidth, &t_width, NULL);
  s_width = width;
  if (width + 7 + t_width > max_width) {
    max_width = width + 7 + t_width;
  }
  XtVaGetValues(connectdlg_port_label, XtNwidth, &width, NULL);
  XtVaGetValues(connectdlg_port_text, XtNwidth, &t_width, NULL);
  if (width > s_width) {
    s_width = width;
  }
  if (width + 7 + t_width > max_width) {
    max_width = width + 7 + t_width;
  }
  XtVaGetValues(connectdlg_login_label, XtNwidth, &width, NULL);
  XtVaGetValues(connectdlg_login_text, XtNwidth, &t_width, NULL);
  if (width > s_width) {
    s_width = width;
  }
  if (width + 7 + t_width > max_width) {
    max_width = width + 7 + t_width;
  }
  XtVaGetValues(connectdlg_password_label, XtNwidth, &width, NULL);
  XtVaGetValues(connectdlg_password_text, XtNwidth, &t_width, NULL);
  if (width > s_width) {
    s_width = width;
  }
  if (width + 7 + t_width > max_width) {
    max_width = width + 7 + t_width;
  }
  XtVaGetValues(connectdlg_verify_label, XtNwidth, &width, NULL);
  XtVaGetValues(connectdlg_verify_text, XtNwidth, &t_width, NULL);
  if (width > s_width) {
    s_width = width;
  }
  if (width + 7 + t_width > max_width) {
    max_width = width + 7 + t_width;
  }

#if IS_BETA_VERSION
  XtVaSetValues(connectdlg_beta_label, XtNwidth, max_width, NULL);
#endif
  XtVaSetValues(connectdlg_select_label, XtNwidth, max_width, NULL);
  XtVaSetValues(connectdlg_message_label, XtNwidth, max_width, NULL);
  XtVaSetValues(connectdlg_message_label, XtNresizable, False, NULL);

  XtVaSetValues(connectdlg_host_label, XtNwidth, s_width, NULL);
  XtVaSetValues(connectdlg_port_label, XtNwidth, s_width, NULL);
  XtVaSetValues(connectdlg_login_label, XtNwidth, s_width, NULL);
  XtVaSetValues(connectdlg_password_label, XtNwidth, s_width, NULL);
  XtVaSetValues(connectdlg_verify_label, XtNwidth, s_width, NULL);

  XtVaSetValues(connectdlg_host_text, XtNresizable, True, NULL);
  XtVaSetValues(connectdlg_host_text, XtNwidth, max_width - s_width - 7,
                NULL);
  XtVaSetValues(connectdlg_host_text, XtNresizable, False, NULL);
  XtVaSetValues(connectdlg_port_text, XtNresizable, True, NULL);
  XtVaSetValues(connectdlg_port_text, XtNwidth, max_width - s_width - 7,
                NULL);
  XtVaSetValues(connectdlg_port_text, XtNresizable, False, NULL);
  XtVaSetValues(connectdlg_login_text, XtNresizable, True, NULL);
  XtVaSetValues(connectdlg_login_text, XtNwidth, max_width - s_width - 7,
                NULL);
  XtVaSetValues(connectdlg_login_text, XtNresizable, False, NULL);
  XtVaSetValues(connectdlg_password_text, XtNresizable, True, NULL);
  XtVaSetValues(connectdlg_password_text, XtNwidth, max_width - s_width - 7,
                NULL);
  XtVaSetValues(connectdlg_password_text, XtNresizable, False, NULL);
  XtVaSetValues(connectdlg_verify_text, XtNresizable, True, NULL);
  XtVaSetValues(connectdlg_verify_text, XtNwidth, max_width - s_width - 7,
                NULL);
  XtVaSetValues(connectdlg_verify_text, XtNresizable, False, NULL);
}

/**************************************************************************
  Creates connect dialog.
**************************************************************************/
static void connectdlg_create(void)
{
  char buf[64];

  if (connectdlg_shell) {
    return;
  }

  connectdlg_shell =
    I_IN(I_T(XtCreatePopupShell("connectdialog", topLevelShellWidgetClass,
                                toplevel, NULL, 0)));
  connectdlg_form =
    XtVaCreateManagedWidget("connectform", formWidgetClass,
                            connectdlg_shell, NULL);

#if IS_BETA_VERSION
  connectdlg_beta_label =
    I_L(XtVaCreateManagedWidget("connbetalabel", labelWidgetClass,
                                connectdlg_form,
                                XtNresizable, True,
                                XtNjustify, XtJustifyCenter,
                                XtNlabel, beta_message(),
                                NULL));
#endif

  connectdlg_select_label =
    I_L(XtVaCreateManagedWidget("connselectlabel", labelWidgetClass,
                                connectdlg_form,
                                XtNresizable, True,
#if IS_BETA_VERSION
                                XtNfromVert, connectdlg_beta_label,
#endif
                                NULL));

  connectdlg_host_label =
    I_L(XtVaCreateManagedWidget("connhostlabel", labelWidgetClass,
                                connectdlg_form,
                                XtNresizable, True,
                                NULL));
  connectdlg_host_text =
    XtVaCreateManagedWidget("connhosttext", asciiTextWidgetClass,
                            connectdlg_form,
                            XtNeditType, XawtextEdit,
                            XtNstring, server_host,
                            NULL);
  connectdlg_port_label =
    I_L(XtVaCreateManagedWidget("connportlabel", labelWidgetClass,
                                connectdlg_form,
                                XtNresizable, True,
                                NULL));
  fc_snprintf(buf, sizeof(buf), "%d", server_port);
  connectdlg_port_text =
    XtVaCreateManagedWidget("connporttext", asciiTextWidgetClass,
                            connectdlg_form,
                            XtNeditType, XawtextEdit,
                            XtNstring, buf,
                            NULL);
  connectdlg_login_label =
    I_L(XtVaCreateManagedWidget("connloginlabel", labelWidgetClass,
                                connectdlg_form,
                                XtNresizable, True,
                                NULL));
  connectdlg_login_text =
    XtVaCreateManagedWidget("connlogintext", asciiTextWidgetClass,
                            connectdlg_form,
                            XtNeditType, XawtextEdit,
                            XtNstring, user_name,
                            NULL);
  connectdlg_password_label =
    I_L(XtVaCreateManagedWidget("connpasswordlabel", labelWidgetClass,
                                connectdlg_form,
                                XtNresizable, True,
                                NULL));
  connectdlg_password_text =
    XtVaCreateManagedWidget("connpasswordtext", asciiTextWidgetClass,
                            connectdlg_form,
                            XtNecho, False,
                            XtNsensitive, False,
                            NULL);
  connectdlg_verify_label =
    I_L(XtVaCreateManagedWidget("connverifylabel", labelWidgetClass,
                                connectdlg_form,
                                XtNresizable, True,
                                NULL));
  connectdlg_verify_text =
    XtVaCreateManagedWidget("connverifytext", asciiTextWidgetClass,
                            connectdlg_form,
                            XtNecho, False,
                            XtNsensitive, False,
                            NULL);
  connectdlg_message_label =
    I_L(XtVaCreateManagedWidget("connmessagelabel", labelWidgetClass,
                                connectdlg_form,
                                XtNresizable, True,
                                NULL));

  connectdlg_connect_button =
    I_L(XtVaCreateManagedWidget("connconnectbutton", commandWidgetClass,
                            connectdlg_form,
                            XtNlabel, _("Connect"),
                            NULL));
  connectdlg_lan_button =
    I_L(XtVaCreateManagedWidget("connlanbutton", commandWidgetClass,
                            connectdlg_form,
                            XtNlabel, _("LAN Servers"),
                            NULL));
  connectdlg_meta_button =
    I_L(XtVaCreateManagedWidget("connmetabutton", commandWidgetClass,
                            connectdlg_form,
                            XtNlabel, _("Metaserver"),
                            NULL));
  connectdlg_quit_button =
    I_L(XtVaCreateManagedWidget("connquitbutton", commandWidgetClass,
                            connectdlg_form,
                            XtNlabel, _("Quit"),
                            NULL));

  XtAddCallback(connectdlg_connect_button, XtNcallback,
                connectdlg_connect_callback, NULL);
  XtAddCallback(connectdlg_lan_button, XtNcallback,
                connectdlg_lan_callback, NULL);
  XtAddCallback(connectdlg_meta_button, XtNcallback,
                connectdlg_meta_callback, NULL);
  XtAddCallback(connectdlg_quit_button, XtNcallback,
                connectdlg_quit_callback, NULL);

  XtRealizeWidget(connectdlg_shell);
}

/**************************************************************************
  Popdowns connect dialog.
**************************************************************************/
static void connectdlg_popdown(void)
{
  if (lan_scan) {
    server_scan_finish(lan_scan);
    lan_scan = NULL;
  }
  if (meta_scan) {
    server_scan_finish(meta_scan);
    meta_scan = NULL;
  }
  if (connectdlg_shell) {
    XtPopdown(connectdlg_shell);
  }
}

/**************************************************************************
  Destroys connect dialog.
**************************************************************************/
static void connectdlg_destroy(void)
{
  if (connectdlg_shell) {
    connectdlg_popdown();
    XtDestroyWidget(connectdlg_shell);
    connectdlg_shell = NULL;
  }
}

/**************************************************************************
  Popups connect dialog.
**************************************************************************/
static void connectdlg_popup(void)
{
  if (!connectdlg_shell) {
    connectdlg_create();
  }

  connectdlg_align_labels();
  xaw_set_relative_position(toplevel, connectdlg_shell, 20, 20);
  XtPopup(connectdlg_shell, XtGrabNone);
}

/****************************************************************************
  Callback for Connect button.
****************************************************************************/
void connectdlg_connect_callback(Widget w, XtPointer client_data,
                              XtPointer call_data)
{
  XtPointer pxp;
  char errbuf[512];
  struct packet_authentication_reply reply;

  switch (connection_status) {
  case LOGIN_TYPE:
    XtVaGetValues(connectdlg_host_text, XtNstring, &pxp, NULL);
    sz_strlcpy(server_host, (char *)pxp);
    XtVaGetValues(connectdlg_port_text, XtNstring, &pxp, NULL);
    sscanf((char *)pxp, "%d", &server_port);
    XtVaGetValues(connectdlg_login_text, XtNstring, &pxp, NULL);
    sz_strlcpy(user_name, (char *)pxp);
    if (connect_to_server(user_name, server_host, server_port,
                          errbuf, sizeof(errbuf)) != -1) {
      popup_start_page();
      connectdlg_destroy();
      XtSetSensitive(toplevel, True);
      return;
    } else {
      XtVaSetValues(connectdlg_message_label, XtNlabel, errbuf, NULL);
      output_window_append(ftc_client, errbuf);
    }
    break;
  case NEW_PASSWORD_TYPE:
    XtVaGetValues(connectdlg_password_text, XtNstring, &pxp, NULL);
    sz_strlcpy(password, (char *)pxp);
    XtVaGetValues(connectdlg_verify_text, XtNstring, &pxp, NULL);
    sz_strlcpy(reply.password, (char *)pxp);
    if (strncmp(reply.password, password, MAX_LEN_NAME) == 0) {
      password[0] = '\0';
      send_packet_authentication_reply(&client.conn, &reply);
      XtVaSetValues(connectdlg_message_label, XtNlabel, "", NULL);
      XtVaSetValues(connectdlg_password_text, XtNsensitive, False, NULL);
      XtVaSetValues(connectdlg_verify_text, XtNsensitive, False, NULL);
    } else {
      XtVaSetValues(connectdlg_password_text, XtNstring, "", NULL);
      XtVaSetValues(connectdlg_verify_text, XtNstring, "", NULL);
      XtVaSetValues(connectdlg_message_label, XtNlabel,
                    _("Passwords don't match, enter password."), NULL);
      output_window_append(ftc_client,
                           _("Passwords don't match, enter password."));
    }
    break;
  case ENTER_PASSWORD_TYPE:
    XtVaGetValues(connectdlg_verify_text, XtNstring, &pxp, NULL);
    sz_strlcpy(reply.password, (char *)pxp);
    send_packet_authentication_reply(&client.conn, &reply);

    XtVaSetValues(connectdlg_message_label, XtNlabel, "", NULL);
    XtVaSetValues(connectdlg_password_text, XtNsensitive, False, NULL);
    XtVaSetValues(connectdlg_verify_text, XtNsensitive, False, NULL);
    break;
  case WAITING_TYPE:
    break;
  }
}

/****************************************************************************
  Callback fir LAN Servers button.
****************************************************************************/
void connectdlg_lan_callback(Widget w, XtPointer client_data,
                          XtPointer call_data)
{
  lan_mode = true;
  if (!lan_scan) {
    lan_scan = server_scan_begin(SERVER_SCAN_LOCAL, server_scan_error);
  }
  connectdlg_serverlist_popup();
}

/****************************************************************************
  Callback for Metaserver button.
****************************************************************************/
void connectdlg_meta_callback(Widget w, XtPointer client_data,
                           XtPointer call_data)
{
  lan_mode = false;
  if (!meta_scan) {
    meta_scan = server_scan_begin(SERVER_SCAN_GLOBAL, server_scan_error);
  }
  connectdlg_serverlist_popup();
}

/****************************************************************************
  Callback for Quit button.
****************************************************************************/
void connectdlg_quit_callback(Widget w, XtPointer client_data,
                           XtPointer call_data)
{
  connectdlg_destroy();
  xaw_ui_exit();
}

/**************************************************************************
 configure the dialog depending on what type of authentication request the
 server is making.
**************************************************************************/
void handle_authentication_req(enum authentication_type type,
                               const char *message)
{
  XtVaSetValues(connectdlg_message_label, XtNlabel, message, NULL);

  switch (type) {
  case AUTH_NEWUSER_FIRST:
  case AUTH_NEWUSER_RETRY:
    XtVaSetValues(connectdlg_password_text, XtNsensitive, True, NULL);
    XtVaSetValues(connectdlg_verify_text, XtNsensitive, True, NULL);
    connection_status = NEW_PASSWORD_TYPE;
    break;
  case AUTH_LOGIN_FIRST:
    /* if we magically have a password already present in 'password'
     * then, use that and skip the password entry dialog */
    if (password[0] != '\0') {
      struct packet_authentication_reply reply;

      sz_strlcpy(reply.password, password);
      send_packet_authentication_reply(&client.conn, &reply);
      return;
    } else {
      XtVaSetValues(connectdlg_password_text, XtNsensitive, True, NULL);
      XtVaSetValues(connectdlg_verify_text, XtNsensitive, False, NULL);
      connection_status = ENTER_PASSWORD_TYPE;
    }
    break;
  case AUTH_LOGIN_RETRY:
    XtVaSetValues(connectdlg_password_text, XtNsensitive, True, NULL);
    XtVaSetValues(connectdlg_verify_text, XtNsensitive, False, NULL);
    connection_status = ENTER_PASSWORD_TYPE;
    break;
  default:
    log_error("Unsupported authentication type %d: %s.", type, message);
    break;
  }
}

/****************************************************************
...
*****************************************************************/
void gui_server_connect(void)
{
  connection_status = LOGIN_TYPE;

  XtSetSensitive(turn_done_button, FALSE);
/*  XtSetSensitive(toplevel, FALSE);*/

  if (auto_connect) {
    /* FIXME */
  } else {
    connectdlg_popup();
/*
    XtPopup(connectdlg_shell, XtGrabNone);
    XtSetKeyboardFocus(toplevel, connectdlg_shell);
*/
  }
}

/**************************************************************************
  this regenerates the player information from a loaded game on the server.
  currently a stub. TODO
**************************************************************************/
void handle_game_load(bool load_successful, const char *filename)
{ 
  /* PORTME */
}

/****************************************************************
...
*****************************************************************/
void connectdlg_key_connect(Widget w)
{
  x_simulate_button_click(connectdlg_connect_button);
}

/****************************************************************************
                            SERVERS LIST DIALOG
****************************************************************************/

/****************************************************************************
  Callback function for when there's an error in the server scan.
****************************************************************************/
void server_scan_error(struct server_scan *scan, const char *message)
{
  output_window_append(ftc_client, message);
  log_normal("%s", message);
  switch (server_scan_get_type(scan)) {
  case SERVER_SCAN_LOCAL:
    server_scan_finish(lan_scan);
    lan_scan = NULL;
    break;
  case SERVER_SCAN_GLOBAL:
    server_scan_finish(meta_scan);
    meta_scan = NULL;
    break;
  case SERVER_SCAN_LAST:
    break;
  }
}

/****************************************************************************
  This function updates the list of servers after the server dialog has been
  displayed. LAN servers updated every 250 ms for 5 seconds, metaserver
  updated every 500 ms for 2 seconds.
****************************************************************************/
static void server_list_timer(XtPointer meta_list, XtIntervalId * id)
{
  char errbuf[128];

  if (!connectdlg_serverlist_shell) {
    return;
  }

  if (get_server_list(servers_list, errbuf, sizeof(errbuf)) != -1)  {
    XawListChange(meta_list, servers_list, 0, 0, True);
  }
/*
  else if (!lan_mode) {
    output_window_append(ftc_client, errbuf);
  }
*/
  num_lanservers_timer++;

  if (lan_mode) {
    if (num_lanservers_timer == 20) {
      server_scan_finish(lan_scan);
      lan_scan = NULL;
      num_lanservers_timer = 0;
      return;
    }
    (void)XtAppAddTimeOut(app_context, 250, server_list_timer,
                          (XtPointer)meta_list);
  } else {
    if (num_lanservers_timer == 4) {
      server_scan_finish(meta_scan);
      meta_scan = NULL;
      num_lanservers_timer = 0;
      return;
    }
    (void)XtAppAddTimeOut(app_context, 500, server_list_timer,
                          (XtPointer)meta_list);
  }
}

/****************************************************************************
  Creates Servers List dialog.
****************************************************************************/
static void connectdlg_serverlist_create(void)
{
  connectdlg_serverlist_shell =
    I_IN(I_T(XtCreatePopupShell("serverlistdialog",
                                topLevelShellWidgetClass,
                                toplevel, NULL, 0)));

  connectdlg_serverlist_form =
    XtVaCreateManagedWidget("serverlistform", formWidgetClass,
                            connectdlg_serverlist_shell, NULL);
  connectdlg_serverlist_legend_label =
    I_L(XtVaCreateManagedWidget("legendlabel", labelWidgetClass,
                                connectdlg_serverlist_form,
                                XtNresizable, True,
                                NULL));
  connectdlg_serverlist_viewport =
    XtVaCreateManagedWidget("viewport", viewportWidgetClass,
                            connectdlg_serverlist_form, NULL);
  connectdlg_serverlist_list =
    XtVaCreateManagedWidget("serverlist", listWidgetClass,
                            connectdlg_serverlist_viewport, NULL);
  connectdlg_serverlist_update_button =
    XtVaCreateManagedWidget("updatebutton", commandWidgetClass,
                            connectdlg_serverlist_form,
                            XtNlabel, _("Update"),
                            NULL);
  connectdlg_serverlist_close_button =
    XtVaCreateManagedWidget("closebutton", commandWidgetClass,
                            connectdlg_serverlist_form,
                            XtNlabel, _("Close"),
                            NULL);

  XtAddCallback(connectdlg_serverlist_update_button, XtNcallback,
                connectdlg_serverlist_update_callback, NULL);
  XtAddCallback(connectdlg_serverlist_close_button, XtNcallback,
                connectdlg_serverlist_close_callback, NULL);
  XtAddCallback(connectdlg_serverlist_list, XtNcallback,
                connectdlg_serverlist_list_callback, NULL);

  (void)XtAppAddTimeOut(app_context, 1, server_list_timer,
                        (XtPointer)connectdlg_serverlist_list);

  XtRealizeWidget(connectdlg_serverlist_shell);
}

/****************************************************************************
  Popups Servers List dialog.
****************************************************************************/
void connectdlg_serverlist_popup(void)
{
  if (!connectdlg_serverlist_shell) {
    connectdlg_serverlist_create();
  }

  xaw_set_relative_position(toplevel, connectdlg_serverlist_shell, 15, 15);
  XtPopup(connectdlg_serverlist_shell, XtGrabNone);
}

/****************************************************************************
  Destroys Servers List dialog.
****************************************************************************/
static void connectdlg_serverlist_destroy(void)
{
  int i;

  if (lan_scan) {
    server_scan_finish(lan_scan);
    lan_scan = NULL;
  }
  if (meta_scan) {
    server_scan_finish(meta_scan);
    meta_scan = NULL;
  }
  /* FIXME: Replace magic 64 with proper constant. */
  for (i = 0; i < 64; i++) {
    if (servers_list[i]) {
      free(servers_list[i]);
      servers_list[i] = NULL;
    }
  }
  if (connectdlg_serverlist_shell) {
    XtDestroyWidget(connectdlg_serverlist_shell);
    connectdlg_serverlist_shell = NULL;
  }
}

/****************************************************************************
  Callback for Update button.
****************************************************************************/
void connectdlg_serverlist_update_callback(Widget w, XtPointer client_data,
                                           XtPointer call_data)
{
  if (lan_mode) {
    if (!lan_scan) {
      lan_scan = server_scan_begin(SERVER_SCAN_LOCAL, server_scan_error);
    }
    if (num_lanservers_timer == 0) {
      server_list_timer(connectdlg_serverlist_list, NULL);
    }
  } else {
    if (!meta_scan) {
      meta_scan = server_scan_begin(SERVER_SCAN_GLOBAL, server_scan_error);
    }
    if (num_lanservers_timer == 0) {
      server_list_timer(connectdlg_serverlist_list, NULL);
    }
  }
}

/****************************************************************************
  Callback for Close button.
****************************************************************************/
void connectdlg_serverlist_close_callback(Widget w, XtPointer client_data,
                                          XtPointer call_data)
{
  connectdlg_serverlist_destroy();
}

/****************************************************************************
  Callback for choosing server from serverlist.
****************************************************************************/
void connectdlg_serverlist_list_callback(Widget w, XtPointer client_data,
                                         XtPointer call_data)
{
  XawListReturnStruct *ret = XawListShowCurrent(w);
  char name[64], port[16];

  sscanf(ret->string, "%s %s\n", name, port);
  XtVaSetValues(connectdlg_host_text, XtNstring, name, NULL);
  XtVaSetValues(connectdlg_port_text, XtNstring, port, NULL);
}

/**************************************************************************
  Get the list of servers from the metaserver or LAN servers
  depending on lan_mode.
**************************************************************************/
static int get_server_list(char **list, char *errbuf, int n_errbuf)
{
  char line[256];
  struct srv_list *srvrs = NULL;
  enum server_scan_status scan_stat;

  if (lan_mode) {
    if (lan_scan) {
      server_scan_poll(lan_scan);
      srvrs = server_scan_get_list(lan_scan);
      fc_allocate_mutex(&srvrs->mutex);
      if (srvrs->servers == NULL) {
        int retval;

	if (num_lanservers_timer == 0) {
          if (*list) {
            free(*list);
          }
	  *list = fc_strdup(" ");
	  retval = 0;
	} else {
	  retval = -1;
	}
        fc_release_mutex(&srvrs->mutex);

        return retval;
      }
    } else {
      return -1;
    }
  } else {
    if (meta_scan) {
      scan_stat = server_scan_poll(meta_scan);
      if (scan_stat >= SCAN_STATUS_PARTIAL) {
        srvrs = server_scan_get_list(meta_scan);
        fc_allocate_mutex(&srvrs->mutex);
        if (!srvrs->servers) {
          fc_release_mutex(&srvrs->mutex);
          if (num_lanservers_timer == 0) {
            if (*list) {
              free(*list);
            }
            *list = fc_strdup(" ");
            return 0;
          } else {
            return -1;
          }
        }
      } else {
        return -1;
      }
    } else {
      return -1;
    }
  }

  server_list_iterate(srvrs->servers, pserver) {
    if (pserver == NULL) {
      continue;
    }
    fc_snprintf(line, sizeof(line), "%-35s %-5d %-11s %-11s %2d   %s",
		pserver->host, pserver->port, pserver->version,
		_(pserver->state), pserver->nplayers, pserver->message);
    if (*list) {
      free(*list);
    }
    *list = fc_strdup(line);
    list++;
  } server_list_iterate_end;

  fc_release_mutex(&srvrs->mutex);

/*
  if (!lan_mode) {
    delete_server_list(server_list);
  } 
*/
  *list = NULL;

  return 0;
}

/**************************************************************************
  Really closes and destroys the dialog.
**************************************************************************/
void really_close_connection_dialog(void)
{
  /* PORTME */
}

/**************************************************************************
  Closes and destroys the dialog.
**************************************************************************/
void close_connection_dialog()
{
  connectdlg_serverlist_destroy();
  connectdlg_destroy();
}

