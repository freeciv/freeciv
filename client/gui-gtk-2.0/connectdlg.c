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
#include "chatline.h"
#include "clinet.h"
#include "colors.h"
#include "connectdlg_common.h"
#include "dialogs.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "options.h"
#include "packhand.h"
#include "tilespec.h"

#include "connectdlg.h"


/**************************************************************************
 really close and destroy the dialog.
**************************************************************************/
void really_close_connection_dialog(void)
{
}

/**************************************************************************
 close and destroy the dialog but only if we don't have a local
 server running (that we started).
**************************************************************************/
void close_connection_dialog() 
{   
  if (!is_server_running()) {
    really_close_connection_dialog();
  }
}

/**************************************************************************
...
**************************************************************************/
static void filesel_response_callback(GtkWidget *w, gint id, gpointer data)
{
  if (id == GTK_RESPONSE_OK) {
    gchar *filename;
    bool is_save = (bool)data;

    filename = g_filename_to_utf8(
	gtk_file_selection_get_filename(GTK_FILE_SELECTION(w)),
	-1, NULL, NULL, NULL);

    if (is_save) {
      send_save_game(filename);
    } else {
      char message[MAX_LEN_MSG];

      my_snprintf(message, sizeof(message), "/load %s", filename);
      send_chat(message);
    }

    g_free(filename);
  }

  gtk_widget_destroy(w);
}


/**************************************************************************
 create a file selector for both the load and save commands
**************************************************************************/
GtkWidget *create_file_selection(const char *title, bool is_save)
{
  GtkWidget *filesel;
  
  /* Create the selector */
  filesel = gtk_file_selection_new(title);
  setup_dialog(filesel, toplevel);
  gtk_window_set_position(GTK_WINDOW(filesel), GTK_WIN_POS_MOUSE);

  g_signal_connect(filesel, "response",
		   G_CALLBACK(filesel_response_callback), (gpointer)is_save);

  /* Display that dialog */
  gtk_window_present(GTK_WINDOW(filesel));

  return filesel;
}

/**************************************************************************
...
**************************************************************************/
void gui_server_connect(void)
{
}

/**************************************************************************
  Make an attempt to autoconnect to the server.
  (server_autoconnect() gets GTK to call this function every so often.)
**************************************************************************/
static int try_to_autoconnect(gpointer data)
{
  char errbuf[512];
  static int count = 0;
#ifndef WIN32_NATIVE
  static int warning_shown = 0;
#endif

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
    exit(EXIT_FAILURE);
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
		"as \"%s\" every %f second(s) for %d times"),
	      server_host, server_port, user_name,
	      0.001 * AUTOCONNECT_INTERVAL,
	      MAX_AUTOCONNECT_ATTEMPTS);
  append_output_window(buf);

  if (get_server_address(server_host, server_port, buf, sizeof(buf)) < 0) {
    freelog(LOG_FATAL,
	    _("Error contacting server \"%s\" at port %d "
	      "as \"%s\":\n %s\n"),
	    server_host, server_port, user_name, buf);
    exit(EXIT_FAILURE);
  }
  if (try_to_autoconnect(NULL)) {
    gtk_timeout_add(AUTOCONNECT_INTERVAL, try_to_autoconnect, NULL);
  }
}
