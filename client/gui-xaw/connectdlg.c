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
#include <config.h>
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


#include "fcintl.h"
#include "log.h"
#include "mem.h"     /* mystrdup() */
#include "support.h"
#include "version.h"

#include "civclient.h"
#include "chatline.h"
#include "clinet.h"
#include "connectdlg_g.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "packhand.h"

#include "connectdlg.h"

static enum {
  LOGIN_TYPE,
  NEW_PASSWORD_TYPE,
  VERIFY_PASSWORD_TYPE,
  ENTER_PASSWORD_TYPE
} dialog_config;

/****************************************************************/

static Widget textsrc, shell;
static Widget imsg, ilabel, iinput, ihost, iport;
static Widget connw, metaw, lanw, quitw;

void server_address_ok_callback(Widget w, XtPointer client_data, 
				XtPointer call_data);
void quit_callback(Widget w, XtPointer client_data, XtPointer call_data);
void connect_callback(Widget w, XtPointer client_data, XtPointer call_data);
void connect_meta_callback(Widget w, XtPointer client_data, XtPointer call_data);
void connect_lan_callback(Widget w, XtPointer client_data, XtPointer call_data);

/****************************************************************/

/* Meta Server */
static Widget meta_dialog_shell=0;
static char *server_list[64]={NULL};

void create_meta_dialog(Widget caller);
int  update_meta_dialog(Widget meta_list);
void meta_list_callback(Widget w, XtPointer client_data, XtPointer call_data);
void meta_list_destroy(Widget w, XtPointer client_data, XtPointer call_data);
void meta_update_callback(Widget w, XtPointer client_data, XtPointer call_data);
void meta_close_callback(Widget w, XtPointer client_data, XtPointer call_data);

void connect_lan_callback(Widget w, XtPointer client_data, XtPointer call_data);

static void server_list_timer(XtPointer client_data, XtIntervalId * id);
static int get_server_list(char **list, char *errbuf, int n_errbuf);

static bool lan_mode;  /* true for LAN mode, false when Meta mode */
static int num_lanservers_timer = 0;

/**************************************************************************
 really close and destroy the dialog.
**************************************************************************/
void really_close_connection_dialog(void)
{
  /* PORTME */
}

/**************************************************************************
 close and destroy the dialog.
**************************************************************************/
void close_connection_dialog()
{
  if (shell) {
    XtDestroyWidget(shell);
    shell = 0;
  }

  if(meta_dialog_shell) {
    XtDestroyWidget(meta_dialog_shell);
    meta_dialog_shell = 0;
  }
}

/****************************************************************
 FIXME: should be used to give some feedback on entering the password.
 I couldn't get it to work (and I thought the GTK API reference was bad) -mck
*****************************************************************/
static void password_callback(Widget w, XtPointer client_data, 
                              XtPointer call_data) 
{
 /* FIXME */
}

/**************************************************************************
 configure the dialog depending on what type of authentication request the
 server is making.
**************************************************************************/
void handle_authentication_req(enum authentication_type type, char *message)
{
  XtVaSetValues(iinput, XtNstring, "", NULL);
  XtVaSetValues(connw, XtNlabel, _("Next"), NULL);
  XtSetSensitive(connw, TRUE);
  XtVaSetValues(imsg, XtNlabel, message, NULL);

  switch (type) {
  case AUTH_NEWUSER_FIRST:
    dialog_config = NEW_PASSWORD_TYPE;
    break;
  case AUTH_NEWUSER_RETRY:
    dialog_config = NEW_PASSWORD_TYPE;
    break;
  case AUTH_LOGIN_FIRST:
    /* if we magically have a password already present in 'password'
     * then, use that and skip the password entry dialog */
    if (password[0] != '\0') {
      struct packet_authentication_reply reply;

      sz_strlcpy(reply.password, password);
      send_packet_authentication_reply(&aconnection, &reply);
      return;
    } else {
      dialog_config = ENTER_PASSWORD_TYPE;
    }
    break;
  case AUTH_LOGIN_RETRY:
    dialog_config = ENTER_PASSWORD_TYPE;
    break;
  default:
    assert(0);
  }

  XtPopup(shell, XtGrabNone);
  XtSetKeyboardFocus(toplevel, shell);
  XtVaSetValues(ilabel, XtNlabel, _("Pass"), NULL);
  XtVaSetValues(iinput, XtNecho, FALSE, NULL);
}

/****************************************************************
...
*****************************************************************/
void gui_server_connect(void)
{
  Widget form;
  char buf[512];

  if (shell) {
    return;
  }

  dialog_config = LOGIN_TYPE;

  XtSetSensitive(turn_done_button, FALSE);
  XtSetSensitive(toplevel, FALSE);

  I_T(shell=XtCreatePopupShell("connectdialog", transientShellWidgetClass,
			       toplevel, NULL, 0));

  form=XtVaCreateManagedWidget("cform", formWidgetClass, shell, NULL);

  I_LW(XtVaCreateManagedWidget("cheadline", labelWidgetClass, form, NULL));

  I_L(imsg = XtVaCreateManagedWidget("cmsgl", labelWidgetClass, form, NULL));

  I_L(ilabel = XtVaCreateManagedWidget("cnamel", labelWidgetClass, form, NULL));
  iinput = XtVaCreateManagedWidget("cnamei", asciiTextWidgetClass, form, 
                                   XtNstring, user_name, NULL);
  textsrc = XawTextGetSource(iinput);

  I_L(XtVaCreateManagedWidget("chostl", labelWidgetClass, form, NULL));
  ihost=XtVaCreateManagedWidget("chosti", asciiTextWidgetClass, form, 
			  XtNstring, server_host, NULL);

  my_snprintf(buf, sizeof(buf), "%d", server_port);
  
  I_L(XtVaCreateManagedWidget("cportl", labelWidgetClass, form, NULL));
  iport=XtVaCreateManagedWidget("cporti", asciiTextWidgetClass, form, 
			  XtNstring, buf, NULL);

  I_L(connw=XtVaCreateManagedWidget("cconnectc", commandWidgetClass,
				    form, NULL));
  I_L(metaw=XtVaCreateManagedWidget("cmetac", commandWidgetClass, form, NULL));
  I_L(lanw=XtVaCreateManagedWidget("clanc", commandWidgetClass, form, NULL));
  I_L(quitw=XtVaCreateManagedWidget("cquitc", commandWidgetClass, form, NULL));

#if IS_BETA_VERSION
  XtVaCreateManagedWidget("cbetaline", labelWidgetClass, form,
			  XtNlabel, beta_message(),
			  NULL);
#endif

  XtAddCallback(textsrc, XtNcallback, password_callback, NULL);
  XtAddCallback(connw, XtNcallback, connect_callback, NULL);
  XtAddCallback(quitw, XtNcallback, quit_callback, NULL);
  XtAddCallback(metaw, XtNcallback, connect_meta_callback, (XtPointer)shell);
  XtAddCallback(lanw, XtNcallback, connect_lan_callback, (XtPointer)shell);

  xaw_set_relative_position(toplevel, shell, 30, 0);

  if (auto_connect) {
    XtPopdown(shell);
  } else {
    XtPopup(shell, XtGrabNone);
    XtSetKeyboardFocus(toplevel, shell);
  }
}

/**************************************************************************
  this regenerates the player information from a loaded game on the server.
  currently a stub. TODO
**************************************************************************/
void handle_game_load(struct packet_game_load *packet)
{ 
  /* PORTME */
}

/****************************************************************
...
*****************************************************************/
void connectdlg_key_connect(Widget w)
{
  x_simulate_button_click(connw);
}

/****************************************************************
...
*****************************************************************/
void quit_callback(Widget w, XtPointer client_data, XtPointer call_data)
{
  xaw_ui_exit();
}

/****************************************************************
 send connect and/or authentication requests to the server.
*****************************************************************/
void connect_callback(Widget w, XtPointer client_data, 
		      XtPointer call_data) 
{
  XtPointer dp;
  char errbuf[512];
  struct packet_authentication_reply reply;

  switch (dialog_config) {
  case LOGIN_TYPE:
    XtVaGetValues(iinput, XtNstring, &dp, NULL);
    sz_strlcpy(user_name, (char*)dp);
    XtVaGetValues(ihost, XtNstring, &dp, NULL);
    sz_strlcpy(server_host, (char*)dp);
    XtVaGetValues(iport, XtNstring, &dp, NULL);
    sscanf((char*)dp, "%d", &server_port);
  
    if (connect_to_server(user_name, server_host, server_port,
                          errbuf, sizeof(errbuf)) != -1) {
      if (meta_dialog_shell) {
        XtDestroyWidget(meta_dialog_shell);
        meta_dialog_shell=0;
      }

      XtSetSensitive(toplevel, True);
      return;
    } else {
      append_output_window(errbuf);
    }
  case NEW_PASSWORD_TYPE:
    XtVaGetValues(iinput, XtNstring, &dp, NULL);
    sz_strlcpy(password, (char*)dp);
    XtVaSetValues(imsg, XtNlabel, _("Verify Password"), NULL);
    XtVaSetValues(iinput, XtNstring, "", NULL);
    dialog_config = VERIFY_PASSWORD_TYPE;
    break;
  case VERIFY_PASSWORD_TYPE:
    XtVaGetValues(iinput, XtNstring, &dp, NULL);
    sz_strlcpy(reply.password, (char*)dp);
    if (strncmp(reply.password, password, MAX_LEN_NAME) == 0) {
      XtSetSensitive(connw, FALSE);
      memset(password, 0, MAX_LEN_NAME);
      password[0] = '\0';
      send_packet_authentication_reply(&aconnection, &reply);
    } else {
      XtVaSetValues(iinput, XtNstring, "", NULL);
      XtVaSetValues(imsg, XtNlabel, 
                    _("Passwords don't match, enter password."), NULL);
      dialog_config = NEW_PASSWORD_TYPE;
    }

    break;
  case ENTER_PASSWORD_TYPE:
    XtSetSensitive(connw, FALSE);
    XtVaGetValues(iinput, XtNstring, &dp, NULL);
    sz_strlcpy(reply.password, (char*)dp);
    send_packet_authentication_reply(&aconnection, &reply);
    break;
  default:
    assert(0);
  }
}

/****************************************************************
...
*****************************************************************/
void connect_meta_callback(Widget w, XtPointer client_data,
                           XtPointer call_data)
{
  lan_mode = false;
  if (meta_dialog_shell) {
    /* Metaserver window already poped up */
    return;
  }
  create_meta_dialog((Widget)client_data);
}

/****************************************************************
...
*****************************************************************/
void connect_lan_callback(Widget w, XtPointer client_data,
                          XtPointer call_data)
{
  lan_mode = true;
  if (meta_dialog_shell) {
    /* Metaserver window already poped up */
    return;
  }
  if (begin_lanserver_scan()) {
    create_meta_dialog((Widget)client_data);
  }
}

/**************************************************************************
  This function updates the list of servers after the server dialog has been
  displayed. LAN servers updated every 100 ms for 5 seconds, metaserver
  updated only once.
**************************************************************************/
static void server_list_timer(XtPointer meta_list, XtIntervalId * id)
{
  char errbuf[128];

  if (!meta_dialog_shell) {
    return;
  }

  if (get_server_list(server_list, errbuf, sizeof(errbuf))!=-1)  {
    XawListChange(meta_list, server_list, 0, 0, True);
  } else if (!lan_mode) {
    append_output_window(errbuf);
  }
  num_lanservers_timer++;

  if (lan_mode) {
    if (num_lanservers_timer == 50 && lan_mode) {
      finish_lanserver_scan();
      num_lanservers_timer = 0;
      return;
    }
    (void)XtAppAddTimeOut(app_context, 100, server_list_timer,
                          (XtPointer)meta_list);
  } else {
    num_lanservers_timer = 0;
  }
}

/****************************************************************
...
*****************************************************************/
void create_meta_dialog(Widget caller)
{
  Widget shell, form, label, list, update, close, viewport;

  I_T(shell=XtCreatePopupShell("metadialog", transientShellWidgetClass,
			       toplevel, NULL, 0));
  meta_dialog_shell=shell;

  form=XtVaCreateManagedWidget("metaform", formWidgetClass, shell, NULL);

  I_L(label=XtVaCreateManagedWidget("legend", labelWidgetClass, form, NULL));
  viewport = XtVaCreateManagedWidget("metaviewport",
                                     viewportWidgetClass, form, NULL);
  list = XtVaCreateManagedWidget("metalist", listWidgetClass, viewport,
                                 NULL);
  I_L(update=XtVaCreateManagedWidget("update", commandWidgetClass,
				     form, NULL));
  I_L(close=XtVaCreateManagedWidget("closecommand", commandWidgetClass,
				    form, NULL));
  
  XtAddCallback(list, XtNcallback, meta_list_callback, NULL);
  XtAddCallback(list, XtNdestroyCallback, meta_list_destroy, NULL);
  XtAddCallback(update, XtNcallback, meta_update_callback, (XtPointer)list);
  XtAddCallback(close, XtNcallback, meta_close_callback, NULL);

  (void)XtAppAddTimeOut(app_context, 1, server_list_timer, (XtPointer)list);

  /* XtRealizeWidget(shell); */

  XtVaSetValues(shell, XtNwidth, 700, NULL);
  XtVaSetValues(viewport, XtNwidth, 700, NULL);
  XtVaSetValues(viewport, XtNheight, 350, NULL);
  XtVaSetValues(list, XtNwidth, 700, NULL);
  XtVaSetValues(label, XtNwidth, 700, NULL);

  xaw_set_relative_position(caller, shell, 0, 90);
  XtPopup(shell, XtGrabNone);

  XtSetKeyboardFocus(toplevel, shell);
}

/****************************************************************
...
*****************************************************************/
void meta_update_callback(Widget w, XtPointer client_data, XtPointer call_data)
{
  if (num_lanservers_timer == 0) {
    if (lan_mode) {
      if (begin_lanserver_scan()) {
        server_list_timer((Widget)client_data, NULL);
      }
    } else {
      server_list_timer((Widget)client_data, NULL);
    }
  }
}

/****************************************************************
...
*****************************************************************/
void meta_close_callback(Widget w, XtPointer client_data, XtPointer call_data)
{
  XtDestroyWidget(meta_dialog_shell);
  meta_dialog_shell=0;
}

/****************************************************************
...
*****************************************************************/
void meta_list_callback(Widget w, XtPointer client_data, XtPointer call_data)
{
  XawListReturnStruct *ret=XawListShowCurrent(w);
  char name[64], port[16];

  sscanf(ret->string,"%s %s\n",name,port);
  XtVaSetValues(ihost, XtNstring, name, NULL);
  XtVaSetValues(iport, XtNstring, port, NULL);
}

/****************************************************************
...
*****************************************************************/
void meta_list_destroy(Widget w, XtPointer client_data, XtPointer call_data)
{
  int i;

  for (i = 0; server_list[i]; i++) {
    free(server_list[i]);
    server_list[i]=NULL;
  }
  if (num_lanservers_timer != 0) {    
    finish_lanserver_scan();
  }
}

/**************************************************************************
  Get the list of servers from the metaserver or LAN servers
  depending on lan_mode.
**************************************************************************/
static int get_server_list(char **list, char *errbuf, int n_errbuf)
{
  char line[256];
  struct server_list *server_list = NULL;

  if (lan_mode) {
    server_list = get_lan_server_list();
    if (server_list == NULL) {
      if (num_lanservers_timer == 0) {
        *list = mystrdup(" ");;
        return 0;
      } else {
        return -1;
      }
    }
  } else {
    server_list = create_server_list(errbuf, n_errbuf); 
    if (!server_list) {
      return -1;
    }
  }

  server_list_iterate(*server_list,pserver) {
    if (pserver == NULL) continue;
    my_snprintf(line, sizeof(line), "%-35s %-5s %-11s %-11s %2s   %s",
		pserver->host, pserver->port, pserver->version,
		_(pserver->state), pserver->nplayers, pserver->message);
    if (*list) free(*list);
    *list=mystrdup(line);
    list++;
  } server_list_iterate_end;

  if (!lan_mode) {
    delete_server_list(server_list);
  } 
  *list=NULL;
  return 0;
}

/**************************************************************************
  Make an attempt to autoconnect to the server.  If the server isn't
  there yet, get the Xt kit to call this routine again about
  AUTOCONNECT_INTERVAL milliseconds.  If anything else goes wrong, log
  a fatal error.
**************************************************************************/
static void try_to_autoconnect(XtPointer data, XtIntervalId * id)
{
  char errbuf[512];
  static int count = 0;

  count++;

  if (count >= MAX_AUTOCONNECT_ATTEMPTS) {
    freelog(LOG_FATAL,
	    _("Failed to contact server \"%s\" at port "
	      "%d as \"%s\" after %d attempts"),
	    server_host, server_port, user_name, count);
    exit(EXIT_FAILURE);
  }

  switch (try_to_connect(user_name, errbuf, sizeof(errbuf))) {
    /* Success! */
  case 0:
    return;

    /* Server not available (yet) - wait & retry */
  case ECONNREFUSED:
    XtAppAddTimeOut(app_context,
		    AUTOCONNECT_INTERVAL, try_to_autoconnect, NULL);
    break;

    /* All other errors are fatal */
  default:
    freelog(LOG_FATAL,
	    _("Error contacting server \"%s\" at port %d "
	      "as \"%s\":\n %s\n"),
	    server_host, server_port, user_name, errbuf);
    exit(EXIT_FAILURE);
  }
}

/**************************************************************************
  Start trying to autoconnect to civserver.
  Calls get_server_address() then try_to_autoconnect().
**************************************************************************/
void server_autoconnect()
{
  char buf[512];
  int outcome;

  my_snprintf(buf, sizeof(buf),
	      _("Auto-connecting to server \"%s\" at port %d "
		"as \"%s\" every %d.%d second(s) for %d times"),
	      server_host, server_port, user_name,
	      AUTOCONNECT_INTERVAL / 1000,AUTOCONNECT_INTERVAL % 1000, 
	      MAX_AUTOCONNECT_ATTEMPTS);
  append_output_window(buf);
  outcome = get_server_address(server_host, server_port, buf, sizeof(buf));
  if (outcome < 0) {
    freelog(LOG_FATAL,
	    _("Error contacting server \"%s\" at port %d "
	      "as \"%s\":\n %s\n"),
	    server_host, server_port, user_name, buf);
    exit(EXIT_FAILURE);
  }

  try_to_autoconnect(NULL, NULL);
  XtSetSensitive(toplevel, True);
}
