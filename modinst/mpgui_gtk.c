/**********************************************************************
< Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
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

#include <stdlib.h>

#include <gtk/gtk.h>

/* utility */
#include "fciconv.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "netintf.h"
#include "shared.h"

/* modinst */
#include "download.h"
#include "mpcmdline.h"

#include "modinst.h"

static GtkWidget *statusbar;
static GtkWidget *progressbar;
static GtkWidget *main_list;
static GtkListStore *main_store;
static GtkWidget *URL_input;
static gboolean downloading = FALSE;

struct fcmp_params fcmp = {
  .list_url = MODPACK_LIST_URL,
  .inst_prefix = NULL
};

static gboolean quit_dialog_callback(void);

#define ML_COL_NAME 0
#define ML_COL_VER  1
#define ML_COL_TYPE 2
#define ML_COL_LIC  3
#define ML_COL_URL  4

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
  Popups the quit dialog if 
****************************************************************/
static gboolean quit_dialog_callback(void)
{
  if (downloading) {
    /* Download in progress. Confirm quit from user. */
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

  } else {
    /* User loses no work by quitting, so let's not annoy him/her
     * with confirmation dialog. */
    modinst_quit();
  }

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

  errmsg = download_modpack(data, &fcmp, msg_callback, pbar_callback);

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
  GtkEntry *URL_in = data;
  const char *URL = gtk_entry_get_text(URL_in);

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
  Build main modpack list view
**************************************************************************/
static void setup_modpack_list(const char *name, const char *URL,
                               const char *version, const char *license,
                               enum modpack_type type)
{
  GtkTreeIter iter;
  const char *type_str;
  const char *lic_str;

  if (modpack_type_is_valid(type)) {
    type_str = _(modpack_type_name(type));
  } else {
    /* TRANS: Unknown modpack type */
    type_str = _("?");
  }

  if (license != NULL) {
    lic_str = license;
  } else {
    /* TRANS: License of modpack is not known */
    lic_str = Q_("?license:Unknown");
  }

  gtk_list_store_append(main_store, &iter);
  gtk_list_store_set(main_store, &iter,
                     ML_COL_NAME, name,
                     ML_COL_VER, version,
                     ML_COL_TYPE, type_str,
                     ML_COL_LIC, lic_str,
                     ML_COL_URL, URL,
                     -1);
}

/**************************************************************************
  Callback called when entry from main modpack list selected
**************************************************************************/
static void select_from_list(GtkTreeSelection *select, gpointer data)
{
  GtkTreeModel *model;
  GtkTreeIter it;
  const char *URL;

  if (!gtk_tree_selection_get_selected(select, &model, &it)) {
    return;
  }

  gtk_tree_model_get(model, &it, ML_COL_URL, &URL, -1);

  gtk_entry_set_text(GTK_ENTRY(URL_input), URL);
}

/**************************************************************************
  Build widgets
**************************************************************************/
static void modinst_setup_widgets(GtkWidget *toplevel)
{
  GtkWidget *mbox, *Ubox;
  GtkWidget *install_button, *install_label;
  GtkWidget *URL_label;
  GtkCellRenderer *renderer;
  GtkTreeSelection *selection;
  const char *errmsg;

  mbox = gtk_vbox_new(FALSE, 4);

  main_list = gtk_tree_view_new();
  renderer = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(main_list),
                                              ML_COL_NAME,      
                                              _("Name"), renderer, "text", 0,
                                              NULL);
  renderer = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(main_list),
                                              ML_COL_VER,
                                              _("Version"), renderer, "text", 1,
                                              NULL);
  renderer = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(main_list),
                                              ML_COL_TYPE,
                                              _("Type"), renderer, "text", 2,
                                              NULL);
  renderer = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(main_list),
                                              ML_COL_LIC,
                                              _("License"), renderer, "text", 3,
                                              NULL);
  renderer = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(main_list),
                                              ML_COL_URL,
                                              _("URL"), renderer, "text", 4,
                                              NULL);
  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(main_list));
  g_signal_connect(selection, "changed", G_CALLBACK(select_from_list), NULL);

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
  gtk_entry_set_text(GTK_ENTRY(URL_input), DEFAULT_URL_START);
  g_signal_connect(URL_input, "activate",
		   G_CALLBACK(URL_return), NULL);

  g_signal_connect(install_button, "clicked",
                   G_CALLBACK(install_clicked), URL_input);

  gtk_box_pack_start(GTK_BOX(Ubox), URL_label, TRUE, TRUE, 0);
  gtk_box_pack_end(GTK_BOX(Ubox), URL_input, TRUE, TRUE, 0);

  progressbar = gtk_progress_bar_new();

  statusbar = gtk_label_new(_("Select modpack to install"));

  gtk_box_pack_start(GTK_BOX(mbox), main_list, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(mbox), Ubox, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(mbox), install_button, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(mbox), progressbar, TRUE, TRUE, 0);
  gtk_box_pack_end(GTK_BOX(mbox), statusbar, TRUE, TRUE, 0);

  gtk_container_add(GTK_CONTAINER(toplevel), mbox);

  main_store = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING,
                                  G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
  errmsg = download_modpack_list(&fcmp, setup_modpack_list, msg_callback);
  gtk_tree_view_set_model(GTK_TREE_VIEW(main_list), GTK_TREE_MODEL(main_store));

  g_object_unref(main_store);

  if (errmsg != NULL) {
    gtk_label_set_text(GTK_LABEL(statusbar), errmsg);
  }
}

/**************************************************************************
  Entry point of the freeciv-modpack program
**************************************************************************/
int main(int argc, char *argv[])
{
  GtkWidget *toplevel;
  int loglevel = LOG_NORMAL;
  int ui_options;

  init_nls();
  init_character_encodings(FC_DEFAULT_DATA_ENCODING, FALSE);

  fc_init_network();

  g_thread_init(NULL);

  log_init(NULL, loglevel, NULL, NULL, -1);

  /* This modifies argv! */
  ui_options = fcmp_parse_cmdline(argc, argv);

  if (ui_options != -1) {

    /* Process GTK arguments */
    gtk_init(&ui_options, &argv);

    toplevel = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    gtk_widget_realize(toplevel);
    gtk_widget_set_name(toplevel, "Freeciv-modpack");
    gtk_window_set_title(GTK_WINDOW(toplevel),
                         _("Freeciv modpack installer (gtk)"));

    /* Keep the icon of the executable on Windows */
#ifndef WIN32_NATIVE
    {
      /* Unlike main client, this only works if installed. Ignore any
       * errors loading the icon. */
      GError *err;
      (void) gtk_window_set_icon_from_file(GTK_WINDOW(toplevel), MPICON_PATH,
                                           &err);
    }
#endif /* WIN32_NATIVE */

    g_signal_connect(toplevel, "delete_event",
                     G_CALLBACK(quit_dialog_callback), NULL);

    modinst_setup_widgets(toplevel);

    gtk_widget_show_all(toplevel);

    gtk_main();
  }

  log_close();

  return EXIT_SUCCESS;
}
