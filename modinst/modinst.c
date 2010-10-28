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

#include <stdlib.h>

#include <gtk/gtk.h>

/* utility */
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "shared.h"

/* modinst */
#include "download.h"

#define EXAMPLE_URL "http://www.cazfi.net/freeciv/modinst/2.3/ancients.modpack"

static GtkWidget *statusbar;
static GtkWidget *progressbar;
static gboolean downloading = FALSE;

static gboolean quit_dialog_callback(void);

/****************************************************************
  freeciv-modpack quit
****************************************************************/
static void modinst_quit(void)
{
  free_nls();

  exit(EXIT_SUCCESS);
}

/****************************************************************
  This is the response callback for the dialog with the message:
  Are you sure you want to quit?
****************************************************************/
static void quit_dialog_response(GtkWidget *dialog, gint response)
{
  gtk_widget_destroy(dialog);
  if (response == GTK_RESPONSE_YES) {
    modinst_quit();
  }
}

/****************************************************************
  Popups the quit dialog.
****************************************************************/
static gboolean quit_dialog_callback(void)
{
  static GtkWidget *dialog;

  if (!dialog) {
    dialog = gtk_message_dialog_new(NULL,
	0,
	GTK_MESSAGE_WARNING,
	GTK_BUTTONS_YES_NO,
	_("Are you sure you want to quit?"));

    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_MOUSE);

    g_signal_connect(dialog, "response", 
	G_CALLBACK(quit_dialog_response), NULL);
    g_signal_connect(dialog, "destroy",
	G_CALLBACK(gtk_widget_destroyed), &dialog);
  }

  gtk_window_present(GTK_WINDOW(dialog));

  /* Stop emission of event. */
  return TRUE;
}

/**************************************************************************
  Progress indications from downloader
**************************************************************************/
static void msg_callback(const char *msg)
{
  gtk_label_set_text(GTK_LABEL(statusbar), msg);
}

/**************************************************************************
  Progress indications from downloader
**************************************************************************/
static void pbar_callback(const double fraction)
{
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progressbar), fraction);
}

/**************************************************************************
  Entry point for downloader thread
**************************************************************************/
static gpointer download_thread(gpointer data)
{
  const char *errmsg;

  errmsg = download_modpack(data, msg_callback, pbar_callback);

  if (errmsg == NULL) {
    gtk_label_set_text(GTK_LABEL(statusbar), _("Ready"));
  } else {
    gtk_label_set_text(GTK_LABEL(statusbar), errmsg);
  }

  free(data);

  downloading = FALSE;

  return NULL;
}

/**************************************************************************
  Download modpack, display error message dialogs
**************************************************************************/
static void gui_download_modpack(const char *URL)
{
  GThread *downloader;
  char *URLbuf;

  if (downloading) {
    gtk_label_set_text(GTK_LABEL(statusbar),
                       _("Another download already active"));
    return;
  }

  downloading = TRUE;

  URLbuf = fc_malloc(strlen(URL) + 1);

  strcpy(URLbuf, URL);

  downloader = g_thread_create(download_thread, URLbuf, FALSE, NULL);
  if (downloader == NULL) {
    gtk_label_set_text(GTK_LABEL(statusbar),
                       _("Failed to start downloader"));
    free(URLbuf);
  }
}

/**************************************************************************
  Install modpack button clicked
**************************************************************************/
static void install_clicked(GtkWidget *w, gpointer data)
{
  GtkEntry *URL_input = data;
  const char *URL = gtk_entry_get_text(URL_input);

  gui_download_modpack(URL);
}

/**************************************************************************
  URL entered
**************************************************************************/
static void URL_return(GtkEntry *w, gpointer data)
{
  const char *URL;

  URL = gtk_entry_get_text(w);
  gui_download_modpack(URL);
}

/**************************************************************************
  Build widgets
**************************************************************************/
static void modinst_setup_widgets(GtkWidget *toplevel)
{
  GtkWidget *mbox, *Ubox;
  GtkWidget *install_button, *install_label;
  GtkWidget *URL_label, *URL_input;

  mbox = gtk_vbox_new(FALSE, 4);

  install_button = gtk_button_new();
  install_label = gtk_label_new(_("Install modpack"));
  gtk_label_set_mnemonic_widget(GTK_LABEL(install_label), install_button);
  g_object_set_data(G_OBJECT(install_button), "label", install_label);
  gtk_container_add(GTK_CONTAINER(install_button), install_label);

  Ubox = gtk_hbox_new(FALSE, 4);
  URL_label = gtk_label_new_with_mnemonic(_("Modpack URL"));

  URL_input = gtk_entry_new();
  gtk_entry_set_width_chars(GTK_ENTRY(URL_input),
                            strlen(EXAMPLE_URL));
  g_signal_connect(URL_input, "activate",
		   G_CALLBACK(URL_return), NULL);

  g_signal_connect(install_button, "clicked",
                   G_CALLBACK(install_clicked), URL_input);

  gtk_box_pack_start(GTK_BOX(Ubox), URL_label, TRUE, TRUE, 0);
  gtk_box_pack_end(GTK_BOX(Ubox), URL_input, TRUE, TRUE, 0);

  progressbar = gtk_progress_bar_new();

  statusbar = gtk_label_new(_("Select modpack to install"));

  gtk_box_pack_start(GTK_BOX(mbox), Ubox, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(mbox), install_button, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(mbox), progressbar, TRUE, TRUE, 0);
  gtk_box_pack_end(GTK_BOX(mbox), statusbar, TRUE, TRUE, 0);

  gtk_container_add(GTK_CONTAINER(toplevel), mbox);
}

/**************************************************************************
  Entry point of the freeciv-modpack program
**************************************************************************/
int main(int argc, char *argv[])
{
  GtkWidget *toplevel;
  int loglevel = LOG_NORMAL;

  init_nls();

  /* Process GTK arguments */
  gtk_init(&argc, &argv);

  log_init(NULL, loglevel, NULL, NULL, -1);

  toplevel = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  gtk_widget_realize(toplevel);
  gtk_widget_set_name(toplevel, "Freeciv-modpack");

  g_signal_connect(toplevel, "delete_event",
      G_CALLBACK(quit_dialog_callback), NULL);

  modinst_setup_widgets(toplevel);

  gtk_widget_show_all(toplevel);

  gtk_main();

  return EXIT_SUCCESS;
}
