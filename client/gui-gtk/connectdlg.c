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
#include <string.h>

#include <gtk/gtk.h>

#include "fcintl.h"
#include "log.h"
#include "packets.h"
#include "support.h"
#include "version.h"

#include "civclient.h"
#include "clinet.h"
#include "packhand.h"

#include "chatline.h"
#include "colors.h"
#include "connectdlg_g.h"
#include "dialogs.h"
#include "gui_main.h"
#include "gui_stuff.h"

#include "connectdlg.h"

enum { 
  LOGIN_PAGE, 
  METASERVER_PAGE 
};

static enum { 
  LOGIN_TYPE, 
  NEW_PASSWORD_TYPE, 
  VERIFY_PASSWORD_TYPE, 
  ENTER_PASSWORD_TYPE 
} dialog_config;

static GtkWidget *imsg, *ilabel, *iinput, *ihost, *iport;
static GtkWidget *connw, *quitw;
static GtkWidget *meta_list;	/* Meta Server */
static GtkWidget *lan_list;     /* LAN Servers */

static GtkWidget *dialog, *book;
static int sort_column;

/* meta Server */
static bool update_meta_dialog(GtkWidget *meta_list);
static void meta_list_callback(GtkWidget *w, gint row, gint column);
static void meta_update_callback(GtkWidget *w, gpointer data);

static int get_meta_list(GtkWidget *list, char *errbuf, int n_errbuf);

/* LAN Servers */
static void lan_update_callback(GtkWidget *w, gpointer data);
static void lan_list_callback(GtkWidget *w, gint row, gint column);
static int get_lanservers(gpointer data);

static int num_lanservers_timer = 0;

#define DEFAULT_SORT_COLUMN 0	/* default sort column  (server)  */

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
  if (dialog) {
    gtk_widget_destroy(dialog);
    dialog = NULL;
    gtk_widget_set_sensitive(top_vbox, TRUE);
  }
}

/**************************************************************************
 configure the dialog depending on what type of authentication request the
 server is making.
**************************************************************************/
void handle_authentication_req(enum authentication_type type, char *message)
{
  gtk_widget_grab_focus(iinput);
  gtk_entry_set_text(GTK_ENTRY(iinput), "");
  gtk_set_label(GTK_BUTTON(connw)->child, _("Next"));
  gtk_widget_set_sensitive(connw, TRUE);
  gtk_set_label(imsg, message);

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

  gtk_widget_show(dialog);
  gtk_entry_set_visibility(GTK_ENTRY(iinput), FALSE);
  gtk_set_label(ilabel, _("Password:"));
}

/**************************************************************************
 if on the metaserver page, switch page to the login page (with new server
 and port). if on the login page, send connect and/or authentication 
 requests to the server.
**************************************************************************/
static void connect_callback(GtkWidget *w, gpointer data)
{
  char errbuf [512];
  struct packet_authentication_reply reply;

  if (gtk_notebook_get_current_page(GTK_NOTEBOOK(book)) == METASERVER_PAGE) {
    gtk_notebook_set_page(GTK_NOTEBOOK(book), LOGIN_PAGE);
    return;
  }

  switch (dialog_config) {
  case LOGIN_TYPE:
    sz_strlcpy(user_name, gtk_entry_get_text(GTK_ENTRY(iinput)));
    sz_strlcpy(server_host, gtk_entry_get_text(GTK_ENTRY(ihost)));
    sscanf(gtk_entry_get_text(GTK_ENTRY(iport)), "%d", &server_port);
  
    if (connect_to_server(user_name, server_host, server_port,
                          errbuf, sizeof(errbuf)) != -1) {
    } else {
      append_output_window(errbuf);
    }

    break;
  case NEW_PASSWORD_TYPE:
    sz_strlcpy(password, gtk_entry_get_text(GTK_ENTRY(iinput)));
    gtk_set_label(imsg, _("Verify Password"));
    gtk_entry_set_text(GTK_ENTRY(iinput), "");
    gtk_widget_grab_focus(iinput);
    dialog_config = VERIFY_PASSWORD_TYPE;
    break;
  case VERIFY_PASSWORD_TYPE:
    sz_strlcpy(reply.password, gtk_entry_get_text(GTK_ENTRY(iinput)));
    if (strncmp(reply.password, password, MAX_LEN_NAME) == 0) {
      gtk_widget_set_sensitive(connw, FALSE);
      memset(password, 0, MAX_LEN_NAME);
      password[0] = '\0';
      send_packet_authentication_reply(&aconnection, &reply);
    } else {
      gtk_widget_grab_focus(iinput);
      gtk_entry_set_text(GTK_ENTRY(iinput), "");
      gtk_set_label(imsg, _("Passwords don't match, enter password."));
      dialog_config = NEW_PASSWORD_TYPE;
    }
    break;
  case ENTER_PASSWORD_TYPE:
    gtk_widget_set_sensitive(connw, FALSE);
    sz_strlcpy(reply.password, gtk_entry_get_text(GTK_ENTRY(iinput)));
    send_packet_authentication_reply(&aconnection, &reply);
    break;
  default:
    assert(0);
  }
}

/**************************************************************************
 Sort the list of servers
**************************************************************************/
static void sort_servers_callback(GtkButton * button, gpointer * data)
{
  sort_column = GPOINTER_TO_INT(data);
  if (GTK_CLIST(data)->sort_type == GTK_SORT_ASCENDING) {
    gtk_clist_set_sort_type(GTK_CLIST(data), GTK_SORT_DESCENDING);
  } else {
    gtk_clist_set_sort_type(GTK_CLIST(data), GTK_SORT_ASCENDING);
  }
  gtk_clist_set_sort_column(GTK_CLIST(data), sort_column);
  gtk_clist_sort(GTK_CLIST(data));
}

/**************************************************************************
...
**************************************************************************/
static bool update_meta_dialog(GtkWidget *meta_list)
{
  char errbuf[128];

  if(get_meta_list(meta_list, errbuf, sizeof(errbuf))!=-1)  {
    return TRUE;
  } else {
    append_output_window(errbuf);
    return FALSE;
  }
}

/**************************************************************************
...
**************************************************************************/
static void meta_update_callback(GtkWidget *w, gpointer data)
{
  update_meta_dialog(GTK_WIDGET(data));
}

/**************************************************************************
 This function updates the list of LAN servers every 100 ms for 5 seconds.
**************************************************************************/
static gint get_lanservers(gpointer data)
{
  int i;
  char *row[6];
  char buf[6][64];
  struct server_list *server_list = get_lan_server_list();

  if (server_list != NULL) {
    gtk_clist_freeze(GTK_CLIST(lan_list));
    gtk_clist_clear(GTK_CLIST(lan_list));

    for (i = 0; i < 6; i++) {
      row[i] = buf[i];
    }
    server_list_iterate(*server_list,pserver) {
      sz_strlcpy(buf[0], pserver->host);
      sz_strlcpy(buf[1], pserver->port);
      sz_strlcpy(buf[2], pserver->version);
      sz_strlcpy(buf[3], _(pserver->state));
      sz_strlcpy(buf[4], pserver->nplayers);
      sz_strlcpy(buf[5], pserver->message);

      gtk_clist_append(GTK_CLIST(lan_list), row);
    } server_list_iterate_end;

    gtk_clist_thaw(GTK_CLIST(lan_list));
    gtk_clist_set_sort_type(GTK_CLIST(lan_list), GTK_SORT_ASCENDING);
    gtk_clist_set_sort_column(GTK_CLIST(lan_list), sort_column);
    gtk_clist_sort(GTK_CLIST(lan_list));
  }

  num_lanservers_timer++;
  if (num_lanservers_timer == 50) {
    finish_lanserver_scan();
    num_lanservers_timer = 0;
    return 0;
  }
  return 1;
}

/**************************************************************************
  This function updates the list widget with LAN servers.
**************************************************************************/
static void lan_update_callback(GtkWidget *w, gpointer data)
{
  int get_lanservers_timer = 0;

  if (num_lanservers_timer == 0) {
    gtk_clist_clear(GTK_CLIST(lan_list));
    if (begin_lanserver_scan()) {
      get_lanservers_timer = gtk_timeout_add(100, get_lanservers, NULL);
    }
  }
}

/**************************************************************************
...
**************************************************************************/
static void meta_list_callback(GtkWidget *w, gint row, gint column)
{
  gchar *name, *port;

  gtk_clist_get_text(GTK_CLIST(w), row, 0, &name);
  gtk_entry_set_text(GTK_ENTRY(ihost), name);
  gtk_clist_get_text(GTK_CLIST(w), row, 1, &port);
  gtk_entry_set_text(GTK_ENTRY(iport), port);
}

/**************************************************************************
  This function sets the host and port field in the server selection.
**************************************************************************/
static void lan_list_callback(GtkWidget *w, gint row, gint column)
{
  gchar *name, *port;

  gtk_clist_get_text(GTK_CLIST(w), row, 0, &name);
  gtk_entry_set_text(GTK_ENTRY(ihost), name);
  gtk_clist_get_text(GTK_CLIST(w), row, 1, &port);
  gtk_entry_set_text(GTK_ENTRY(iport), port);
}

/**************************************************************************
...
***************************************************************************/
static void meta_click_callback(GtkWidget *w, GdkEventButton *event, gpointer data)
{
  if (event->type==GDK_2BUTTON_PRESS) connect_callback(w, data);
}

/**************************************************************************
...
***************************************************************************/
static void lan_click_callback(GtkWidget *w, GdkEventButton *event, gpointer data)
{
  if (event->type == GDK_2BUTTON_PRESS) {
    connect_callback(w, data);
  }
}

/**************************************************************************
...
**************************************************************************/
static gint connect_deleted_callback(GtkWidget *w, GdkEvent *ev, gpointer data)
{
  gtk_main_quit();
  return FALSE;
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
 change the connect button label on switching.
*****************************************************************/
static void switch_page_callback(GtkNotebook * notebook,
                                 GtkNotebookPage * page, gint page_num,
                                 gpointer data)
{
  if (page_num == LOGIN_PAGE) {
    gtk_set_label(GTK_BUTTON(connw)->child, 
                  dialog_config == LOGIN_TYPE ? _("Connect") : _("Next"));
  } else {
    gtk_set_label(GTK_BUTTON(connw)->child, _("Select")); 
  }
}

/**************************************************************************
...
**************************************************************************/
void gui_server_connect(void)
{
  GtkWidget *label, *table, *scrolled, *vbox, *update_meta, *update_lan;
  static const char *titles_[6]= {N_("Server Name"), N_("Port"), N_("Version"),
				  N_("Status"), N_("Players"), N_("Comment")};
  static char **titles;
  char buf [256];
  int i;

  if (dialog) {
    return;
  }

  dialog_config = LOGIN_TYPE;

  if (!titles) titles = intl_slist(6, titles_);

  gtk_widget_set_sensitive(turn_done_button, FALSE);
  gtk_widget_set_sensitive(top_vbox, FALSE);

  dialog=gtk_dialog_new();
  gtk_signal_connect(GTK_OBJECT(dialog),"delete_event",
	GTK_SIGNAL_FUNC(connect_deleted_callback), NULL);
  
  gtk_window_set_title(GTK_WINDOW(dialog), _(" Connect to Freeciv Server"));

  book = gtk_notebook_new ();
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), book, TRUE, TRUE, 0);


  label=gtk_label_new(_("Freeciv Server Selection"));

  vbox=gtk_vbox_new(FALSE, 2);
  gtk_notebook_append_page (GTK_NOTEBOOK (book), vbox, label);

  table = gtk_table_new (5, 2, FALSE);
  gtk_table_set_row_spacings (GTK_TABLE (table), 2);
  gtk_table_set_col_spacings (GTK_TABLE (table), 5);
  gtk_container_border_width (GTK_CONTAINER (table), 5);
  gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, TRUE, 0);

  imsg = gtk_label_new(NULL);
  gtk_table_attach_defaults(GTK_TABLE (table), imsg, 1, 2, 0, 1);
  gtk_label_set_line_wrap(GTK_LABEL(imsg), TRUE);
  gtk_misc_set_alignment(GTK_MISC(imsg), 0.0, 0.5);

  ilabel = gtk_label_new(_("Login:"));
  gtk_table_attach (GTK_TABLE (table), ilabel, 0, 1, 1, 2, 0, 0, 0, 0);
  gtk_misc_set_alignment (GTK_MISC (ilabel), 0.0, 0.5);

  iinput=gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(iinput), user_name);
  gtk_table_attach_defaults (GTK_TABLE (table), iinput, 1, 2, 1, 2);

  label=gtk_label_new(_("Host:"));
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 2, 3, 0, 0, 0, 0);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);

  ihost=gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(ihost), server_host);
  gtk_table_attach_defaults (GTK_TABLE (table), ihost, 1, 2, 2, 3);

  label=gtk_label_new(_("Port:"));
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 3, 4, 0, 0, 0, 0);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);

  my_snprintf(buf, sizeof(buf), "%d", server_port);

  iport=gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(iport), buf);
  gtk_table_attach_defaults (GTK_TABLE (table), iport, 1, 2, 3, 4);

#if IS_BETA_VERSION
  {
    GtkWidget *label2;
    GtkStyle *style;

    label2 = gtk_label_new(beta_message());

    if (!(style = gtk_rc_get_style(label2))) {
      style = label2->style;
    }
    style = gtk_style_copy(style);

    style->fg[GTK_STATE_NORMAL] = *colors_standard[COLOR_STD_RED];
    gtk_widget_set_style(label2, style);
    gtk_table_attach_defaults(GTK_TABLE (table), label2, 0, 2, 4, 5);
  }
#endif

  label=gtk_label_new(_("Metaserver"));

  vbox=gtk_vbox_new(FALSE, 2);
  gtk_notebook_append_page (GTK_NOTEBOOK (book), vbox, label);

  meta_list = gtk_clist_new_with_titles(6, titles);

  for (i = 0; i < 6; i++) {
    gtk_clist_set_column_auto_resize(GTK_CLIST(meta_list), i, TRUE);
  }

  scrolled=gtk_scrolled_window_new(NULL,NULL);
  gtk_container_add(GTK_CONTAINER(scrolled), meta_list);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				 GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
  gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

  update_meta = gtk_button_new_with_label(_("Update"));
  gtk_box_pack_start(GTK_BOX(vbox), update_meta, FALSE, FALSE, 2);


  label = gtk_label_new(_("Local Area Network"));

  vbox = gtk_vbox_new(FALSE, 2);
  gtk_notebook_append_page (GTK_NOTEBOOK (book), vbox, label);

  lan_list = gtk_clist_new_with_titles(6, titles);

  for (i = 0; i < 6; i++) {
    gtk_clist_set_column_auto_resize(GTK_CLIST(lan_list), i, TRUE);
  }

  scrolled = gtk_scrolled_window_new(NULL,NULL);
  gtk_container_add(GTK_CONTAINER(scrolled), lan_list);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				 GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
  gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

  update_lan = gtk_button_new_with_label(_("Update"));
  gtk_box_pack_start(GTK_BOX(vbox), update_lan, FALSE, FALSE, 2);

  connw = gtk_button_new_with_label(_("Connect"));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area), connw,
	TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(connw, GTK_CAN_DEFAULT);
  gtk_widget_grab_default(connw);

  quitw=gtk_button_new_with_label(_("Quit"));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area), quitw,
	TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(quitw, GTK_CAN_DEFAULT);

  gtk_widget_grab_focus (iinput);

  /*  default sort column  */
  gtk_clist_set_sort_column(GTK_CLIST(meta_list), DEFAULT_SORT_COLUMN);
  gtk_clist_set_sort_column(GTK_CLIST(lan_list), DEFAULT_SORT_COLUMN);

  gtk_widget_show_all(GTK_DIALOG(dialog)->vbox);
  gtk_widget_show_all(GTK_DIALOG(dialog)->action_area);

  gtk_widget_set_usize(dialog, 450, 250);
  gtk_set_relative_position(toplevel, dialog, 50, 50);

  if (auto_connect) {
     gtk_widget_hide(dialog);
  } else {
     gtk_widget_show(dialog);
  }

  /* connect all the signals here, so that we can't send 
   * packets to the server until the dialog is up (which 
   * it may not be on very slow machines) */

  gtk_signal_connect(GTK_OBJECT(book), "switch-page",
                     GTK_SIGNAL_FUNC(switch_page_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(iinput), "activate",
        	      GTK_SIGNAL_FUNC(connect_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(ihost), "activate",
        	      GTK_SIGNAL_FUNC(connect_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(iport), "activate",
        	      GTK_SIGNAL_FUNC(connect_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(connw), "clicked",
        	      GTK_SIGNAL_FUNC(connect_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(quitw), "clicked",
        	      GTK_SIGNAL_FUNC(gtk_main_quit), NULL);

  gtk_signal_connect(GTK_OBJECT(meta_list), "select_row",
		     GTK_SIGNAL_FUNC(meta_list_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(meta_list), "button_press_event",
		     GTK_SIGNAL_FUNC(meta_click_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(update_meta), "clicked",
		     GTK_SIGNAL_FUNC(meta_update_callback),
		     (gpointer) meta_list);
  gtk_signal_connect(GTK_OBJECT(lan_list), "select_row",
                     GTK_SIGNAL_FUNC(lan_list_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(lan_list), "button_press_event",
                     GTK_SIGNAL_FUNC(lan_click_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(update_lan), "clicked",
                     GTK_SIGNAL_FUNC(lan_update_callback),
                     (gpointer) lan_list);


  /*  all columns are clickable  */
  for (i = 0; i <6 ; i++) {
    gtk_signal_connect(GTK_OBJECT(GTK_CLIST(meta_list)->column[i].button),
		       "clicked", GTK_SIGNAL_FUNC(sort_servers_callback),
		       GINT_TO_POINTER(meta_list));
    gtk_signal_connect(GTK_OBJECT(GTK_CLIST(lan_list)->column[i].button),
                       "clicked", GTK_SIGNAL_FUNC(sort_servers_callback),
                       GINT_TO_POINTER(lan_list));

  }
}

/**************************************************************************
  Get the list of servers from the metaserver
**************************************************************************/
static int get_meta_list(GtkWidget *list, char *errbuf, int n_errbuf)
{
  int i;
  char *row[6];
  char  buf[6][64];
  struct server_list *server_list = create_server_list(errbuf, n_errbuf);
  if(!server_list) return -1;

  gtk_clist_freeze(GTK_CLIST(meta_list));
  gtk_clist_clear(GTK_CLIST(meta_list));

  for (i=0; i<6; i++)
    row[i]=buf[i];

  server_list_iterate(*server_list,pserver) {
    sz_strlcpy(buf[0], pserver->host);
    sz_strlcpy(buf[1], pserver->port);
    sz_strlcpy(buf[2], pserver->version);
    sz_strlcpy(buf[3], _(pserver->state));
    sz_strlcpy(buf[4], pserver->nplayers);
    sz_strlcpy(buf[5], pserver->message);

    gtk_clist_append(GTK_CLIST(meta_list), row);
  }
  server_list_iterate_end;

  delete_server_list(server_list);
  gtk_clist_thaw(GTK_CLIST(meta_list));

  /* sort the list */
  gtk_clist_set_sort_type(GTK_CLIST(meta_list), GTK_SORT_ASCENDING);
  gtk_clist_set_sort_column(GTK_CLIST(meta_list), sort_column);
  gtk_clist_sort(GTK_CLIST(meta_list));
     
  return 0;
}

/**************************************************************************
  Make an attempt to autoconnect to the server.
  (server_autoconnect() gets GTK to call this function every so often.)
**************************************************************************/
static int try_to_autoconnect(gpointer data)
{
  char errbuf[512];
  static int count = 0;
  static int warning_shown = 0;

  count++;

  if (count >= MAX_AUTOCONNECT_ATTEMPTS) {
    freelog(LOG_FATAL,
	    _("Failed to contact server \"%s\" at port "
	      "%d as \"%s\" after %d attempts"),
	    server_host, server_port, user_name, count);
    exit(EXIT_FAILURE);
  }

  switch (try_to_connect(user_name, errbuf, sizeof(errbuf))) {
  case 0:			/* Success! */
    return FALSE;		/*  Tells GTK not to call this
				   function again */
#ifndef WIN32_NATIVE
  /* See PR#4042 for more info on issues with try_to_connect() and errno. */
  case ECONNREFUSED:		/* Server not available (yet) */
    if (!warning_shown) {
      freelog(LOG_NORMAL, _("Connection to server refused. "
			    "Please start the server."));
      append_output_window(_("Connection to server refused. "
			     "Please start the server."));
      warning_shown = 1;
    }
    return TRUE;		/*  Tells GTK to keep calling this function */
#endif
  default:			/* All other errors are fatal */
    freelog(LOG_FATAL,
	    _("Error contacting server \"%s\" at port %d "
	      "as \"%s\":\n %s\n"),
	    server_host, server_port, user_name, errbuf);
    gtk_exit(EXIT_FAILURE);
    exit(EXIT_FAILURE);			/* Suppresses a gcc warning */
  }
}

/**************************************************************************
  Start trying to autoconnect to civserver.  Calls
  get_server_address(), then arranges for try_to_autoconnect(), which
  calls try_to_connect(), to be called roughly every
  AUTOCONNECT_INTERVAL milliseconds, until success, fatal error or
  user intervention.  (Doesn't use widgets, but is GTK-specific
  because it calls gtk_timeout_add().)
**************************************************************************/
void server_autoconnect()
{
  char buf[512];

  my_snprintf(buf, sizeof(buf),
	      _("Auto-connecting to server \"%s\" at port %d "
		"as \"%s\" every %d.%d second(s) for %d times"),
	      server_host, server_port, user_name,
	      AUTOCONNECT_INTERVAL / 1000,AUTOCONNECT_INTERVAL % 1000, 
	      MAX_AUTOCONNECT_ATTEMPTS);
  append_output_window(buf);

  if (get_server_address(server_host, server_port, buf, sizeof(buf)) < 0) {
    freelog(LOG_FATAL,
	    _("Error contacting server \"%s\" at port %d "
	      "as \"%s\":\n %s\n"),
	    server_host, server_port, user_name, buf);
    gtk_exit(EXIT_FAILURE);
  }
  if (try_to_autoconnect(NULL)) {
    gtk_timeout_add(AUTOCONNECT_INTERVAL, try_to_autoconnect, NULL);
  }
}
