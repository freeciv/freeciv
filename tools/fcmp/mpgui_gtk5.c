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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <stdlib.h>

#include <gtk/gtk.h>

/* utility */
#include "fc_cmdline.h"
#include "fciconv.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"

/* common */
#include "version.h"

/* modinst */
#include "download.h"
#include "mpcmdline.h"
#include "mpdb.h"

#include "modinst.h"

static GtkWidget *toplevel;
static GtkWidget *statusbar;
static GtkWidget *progressbar;
static GtkWidget *main_list;
static GListStore *main_store;
static GtkWidget *URL_input;
static GtkAlertDialog *quit_dialog;
static gboolean downloading = FALSE;

struct fcmp_params fcmp = {
  .list_url = MODPACK_LIST_URL,
  .inst_prefix = nullptr,
  .autoinstall = nullptr
};

static GtkApplication *fcmp_app;

static gboolean quit_dialog_callback(void);

#define ML_COL_NAME    0
#define ML_COL_VER     1
#define ML_COL_INST    2
#define ML_COL_TYPE    3
#define ML_COL_SUBTYPE 4
#define ML_COL_LIC     5
#define ML_COL_URL     6

#define ML_COL_COUNT   7

#define ML_TYPE        7
#define ML_NOTES       8
#define ML_STORE_SIZE  9

#define FC_TYPE_MPROW (fc_mprow_get_type())

G_DECLARE_FINAL_TYPE(FcMPRow, fc_mprow, FC, MPROW, GObject)

struct _FcMPRow
{
  GObject parent_instance;

  const char *name;
  const char *ver;
  const char *inst;
  const char *type;
  const char *subtype;
  const char *lic;
  char *URL;
  const char *notes;

  int type_int;
};

struct _FcMPRowClass
{
  GObjectClass parent_class;
};

G_DEFINE_TYPE(FcMPRow, fc_mprow, G_TYPE_OBJECT)

/**********************************************************************//**
  Finalizing method for FcMPRow class
**************************************************************************/
static void fc_mprow_finalize(GObject *gobject)
{
  FcMPRow *row = FC_MPROW(gobject);

  free(row->URL);
  row->URL = nullptr;

  G_OBJECT_CLASS(fc_mprow_parent_class)->finalize(gobject);
}

/**********************************************************************//**
  Initialization method for FcMPRow class
**************************************************************************/
static void
fc_mprow_class_init(FcMPRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->finalize = fc_mprow_finalize;
}

/**********************************************************************//**
  Initialization method for FcMPRow
**************************************************************************/
static void
fc_mprow_init(FcMPRow *self)
{
  self->URL = nullptr;
}

/**********************************************************************//**
  FcMPRow creation method
**************************************************************************/
static FcMPRow *fc_mprow_new(void)
{
  FcMPRow *result;

  result = g_object_new(FC_TYPE_MPROW, nullptr);

  return result;
}

/**********************************************************************//**
  freeciv-modpack quit
**************************************************************************/
static void modinst_quit(void)
{
  g_application_quit(G_APPLICATION(fcmp_app));
}

/**********************************************************************//**
  This is the response callback for the dialog with the message:
  Are you sure you want to quit?
**************************************************************************/
static void quit_dialog_response(GObject *dialog, GAsyncResult *result,
                                 gpointer data)
{
  int button = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(dialog),
                                              result, nullptr);

  if (button == 0) {
    modinst_quit();
  }

  quit_dialog = nullptr;
}

/**********************************************************************//**
  Popups the quit dialog if download in progress
**************************************************************************/
static gboolean quit_dialog_callback(void)
{
  if (downloading) {
    /* Download in progress. Confirm quit from user. */

    if (quit_dialog == nullptr) {
      const char *buttons[] = { _("Quit"), _("Cancel"), nullptr };

      quit_dialog = gtk_alert_dialog_new(_("Modpack installation in progress.\n"
                                           "Are you sure you want to quit?"));

      gtk_alert_dialog_set_buttons(GTK_ALERT_DIALOG(quit_dialog), buttons);

      gtk_alert_dialog_choose(GTK_ALERT_DIALOG(quit_dialog),
                              GTK_WINDOW(toplevel), nullptr,
                              quit_dialog_response, nullptr);
    }

  } else {
    /* User loses no work by quitting, so let's not annoy them
     * with confirmation dialog. */
    modinst_quit();
  }

  /* Stop emission of event. */
  return TRUE;
}

/**********************************************************************//**
  Progress indications from downloader
**************************************************************************/
static void msg_callback(const char *msg)
{
  log_verbose("%s", msg);
  gtk_label_set_text(GTK_LABEL(statusbar), msg);
}

/**********************************************************************//**
  Progress indications from downloader
**************************************************************************/
static void pbar_callback(int downloaded, int max)
{
  /* Control file already downloaded */
  double fraction = (double) downloaded / (max);

  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progressbar), fraction);
}

struct msg_data {
  char *msg;
};

/**********************************************************************//**
  Main thread handling of message sent from downloader thread.
**************************************************************************/
static gboolean msg_main_thread(gpointer user_data)
{
  struct msg_data *data = (struct msg_data *)user_data;

  msg_callback(data->msg);

  FC_FREE(data->msg);
  FC_FREE(data);

  return G_SOURCE_REMOVE;
}

/**********************************************************************//**
  Downloader thread message callback.
**************************************************************************/
static void msg_dl_thread(const char *msg)
{
  struct msg_data *data = fc_malloc(sizeof(*data));

  data->msg = fc_strdup(msg);

  g_idle_add(msg_main_thread, data);
}

struct pbar_data {
  int current;
  int max;
};

/**********************************************************************//**
  Main thread handling of progressbar update sent from downloader thread.
**************************************************************************/
static gboolean pbar_main_thread(gpointer user_data)
{
  struct pbar_data *data = (struct pbar_data *)user_data;

  pbar_callback(data->current, data->max);

  FC_FREE(data);

  return G_SOURCE_REMOVE;
}

/**********************************************************************//**
  Downloader thread progress bar callback.
**************************************************************************/
static void pbar_dl_thread(int current, int max)
{
  struct pbar_data *data = fc_malloc(sizeof(*data));

  data->current = current;
  data->max = max;

  g_idle_add(pbar_main_thread, data);
}

/**********************************************************************//**
  Main thread handling of versionlist update requested by downloader thread
**************************************************************************/
static gboolean versionlist_update_main_thread(gpointer user_data)
{
#if 0
  GtkTreeIter iter;

  if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(main_store), &iter)) {
    do {
      const char *name_str;
      int type_int;
      const char *new_inst;
      enum modpack_type type;

      gtk_tree_model_get(GTK_TREE_MODEL(main_store), &iter,
                         ML_COL_NAME, &name_str,
                         ML_TYPE, &type_int,
                         -1);

     type = type_int;

     new_inst = mpdb_installed_version(name_str, type);

     if (new_inst == nullptr) {
       new_inst = _("Not installed");
     }

     gtk_list_store_set(main_store, &iter,
                        ML_COL_INST, new_inst,
                        -1);

    } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(main_store), &iter));
  }
#endif

  return G_SOURCE_REMOVE;
}

/**********************************************************************//**
  Downloader thread requests versionlist update.
**************************************************************************/
static void versionlist_update_dl_thread(void)
{
  g_idle_add(versionlist_update_main_thread, nullptr);
}

/**********************************************************************//**
  Entry point for downloader thread
**************************************************************************/
static gpointer download_thread(gpointer data)
{
  const char *errmsg;

  errmsg = download_modpack(data, &fcmp, msg_dl_thread, pbar_dl_thread);

  if (errmsg == nullptr) {
    msg_dl_thread(_("Ready"));
  } else {
    msg_dl_thread(errmsg);
  }

  free(data);

  versionlist_update_dl_thread();

  downloading = FALSE;

  return nullptr;
}

/**********************************************************************//**
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

  downloader = g_thread_new("Downloader", download_thread, URLbuf);
  if (downloader == nullptr) {
    gtk_label_set_text(GTK_LABEL(statusbar),
                       _("Failed to start downloader"));
    free(URLbuf);
  } else {
    g_thread_unref(downloader);
  }
}

/**********************************************************************//**
  Install modpack button clicked
**************************************************************************/
static void install_clicked(GtkWidget *w, gpointer data)
{
  GtkEntry *URL_in = data;
  GtkEntryBuffer *buffer = gtk_entry_get_buffer(URL_in);
  const char *URL = gtk_entry_buffer_get_text(buffer);

  gui_download_modpack(URL);
}

/**********************************************************************//**
  URL entered
**************************************************************************/
static void URL_return(GtkEntry *w, gpointer data)
{
  const char *URL;
  GtkEntryBuffer *buffer = gtk_entry_get_buffer(w);

  URL = gtk_entry_buffer_get_text(buffer);
  gui_download_modpack(URL);
}

/**********************************************************************//**
  Callback for getting main list row tooltip
**************************************************************************/
static gboolean query_main_list_tooltip_cb(GtkWidget *widget,
                                           gint x, gint y,
                                           gboolean keyboard_tip,
                                           GtkTooltip *tooltip,
                                           gpointer data)
{
  GtkWidget *child = gtk_widget_get_first_child(gtk_widget_get_next_sibling(gtk_widget_get_first_child(widget)));
  int row_number = -1; /* 0 after header */
  int curr_y = 0;

  while (GTK_IS_WIDGET(child)) {
    curr_y += gtk_widget_get_height(GTK_WIDGET(child));

    if (curr_y > y) {
      FcMPRow *row = g_list_model_get_item(G_LIST_MODEL(main_store), row_number);

      if (row != nullptr) {
        log_debug("Tooltip row %d. Notes: %s", row_number,
                  row->notes == nullptr ? "-" : row->notes);

        if (row->notes != nullptr) {
          gtk_tooltip_set_markup(tooltip, row->notes);

          return TRUE;
        }
      }

      return FALSE;
    }

    row_number++;

    child = gtk_widget_get_next_sibling(child);
  }

  return FALSE;
}

/**********************************************************************//**
  Build main modpack list view
**************************************************************************/
static void setup_modpack_list(const char *name, const char *URL,
                               const char *version, const char *license,
                               enum modpack_type type, const char *subtype,
                               const char *notes)
{
  FcMPRow *row;

  row = fc_mprow_new();

  if (modpack_type_is_valid(type)) {
    row->type = _(modpack_type_name(type));
  } else {
    /* TRANS: Unknown modpack type */
    row->type = _("?");
  }

  if (license != nullptr) {
    row->lic = license;
  } else {
    /* TRANS: License of modpack is not known */
    row->lic = Q_("?license:Unknown");
  }

  row->inst = mpdb_installed_version(name, type);
  if (row->inst == nullptr) {
    row->inst = _("Not installed");
  }

  row->name = name;
  row->ver = version;
  row->subtype = subtype;
  row->URL = fc_strdup(URL);
  row->notes = notes;

  row->type_int = type;

  g_list_store_append(main_store, row);
  g_object_unref(row);
}

/**********************************************************************//**
  Callback called when entry from main modpack list selected
**************************************************************************/
static void selection_change(GtkSelectionModel *model,
                             guint position, guint n_items,
                             gpointer user_data)
{
  GtkSingleSelection *selection = GTK_SINGLE_SELECTION(model);
  int row_number = gtk_single_selection_get_selected(selection);
  GListModel *lmodel = gtk_single_selection_get_model(selection);
  FcMPRow *row = g_list_model_get_item(lmodel, row_number);
  GtkEntryBuffer *buffer;

  log_debug("Selected row: %d. URL: %s", row_number, row->URL);

  buffer = gtk_entry_get_buffer(GTK_ENTRY(URL_input));
  gtk_entry_buffer_set_text(buffer, row->URL, -1);
}

/**********************************************************************//**
  Table cell bind function
**************************************************************************/
static void factory_bind(GtkSignalListItemFactory *self,
                         GtkListItem *list_item,
                         gpointer user_data)
{
  FcMPRow *row;
  const char *str = "-";

  row = gtk_list_item_get_item(list_item);

  switch (GPOINTER_TO_INT(user_data)) {
  case ML_COL_NAME:
    str = row->name;
    break;
  case ML_COL_VER:
    str = row->ver;
    break;
  case ML_COL_INST:
    str = row->inst;
    break;
  case ML_COL_TYPE:
    str = row->type;
    break;
  case ML_COL_SUBTYPE:
    str = row->subtype;
    break;
  case ML_COL_LIC:
    str = row->lic;
    break;
  case ML_COL_URL:
    str = row->URL;
    break;
  }

  gtk_label_set_text(GTK_LABEL(gtk_list_item_get_child(list_item)),
                     str);
}

/**********************************************************************//**
  Table cell setup function
**************************************************************************/
static void factory_setup(GtkSignalListItemFactory *self,
                          GtkListItem *list_item,
                          gpointer user_data)
{
  gtk_list_item_set_child(list_item, gtk_label_new(""));
}

/**********************************************************************//**
  Build widgets
**************************************************************************/
static void modinst_setup_widgets(void)
{
  GtkWidget *mbox, *Ubox;
  GtkWidget *version_label;
  GtkWidget *install_button;
  GtkWidget *URL_label;
  GtkColumnViewColumn *column;
  GtkListItemFactory *factory;
  GtkSingleSelection *selection;
  const char *errmsg;
  char verbuf[2048];
  const char *rev_ver;

  mbox = gtk_grid_new();
  gtk_orientable_set_orientation(GTK_ORIENTABLE(mbox),
                                 GTK_ORIENTATION_VERTICAL);
  gtk_grid_set_row_spacing(GTK_GRID(mbox), 4);

  rev_ver = fc_git_revision();

  if (rev_ver == nullptr) {
    fc_snprintf(verbuf, sizeof(verbuf), "%s%s", word_version(), VERSION_STRING);
  } else {
    fc_snprintf(verbuf, sizeof(verbuf), _("%s%s\ncommit: %s"),
                word_version(), VERSION_STRING, rev_ver);
  }

  version_label = gtk_label_new(verbuf);

  main_store = g_list_store_new(FC_TYPE_MPROW);
  selection = gtk_single_selection_new(G_LIST_MODEL(main_store));
  g_signal_connect(selection, "selection-changed", G_CALLBACK(selection_change),
                   nullptr);

  main_list = gtk_column_view_new(GTK_SELECTION_MODEL(selection));

  factory = gtk_signal_list_item_factory_new();
  g_signal_connect(factory, "bind", G_CALLBACK(factory_bind),
                   GINT_TO_POINTER(ML_COL_NAME));
  g_signal_connect(factory, "setup", G_CALLBACK(factory_setup), nullptr);

  column = gtk_column_view_column_new(_("Name"), factory);
  gtk_column_view_append_column(GTK_COLUMN_VIEW(main_list), column);

  factory = gtk_signal_list_item_factory_new();
  g_signal_connect(factory, "bind", G_CALLBACK(factory_bind),
                   GINT_TO_POINTER(ML_COL_VER));
  g_signal_connect(factory, "setup", G_CALLBACK(factory_setup), nullptr);

  column = gtk_column_view_column_new(_("Version"), factory);
  gtk_column_view_append_column(GTK_COLUMN_VIEW(main_list), column);

  factory = gtk_signal_list_item_factory_new();
  g_signal_connect(factory, "bind", G_CALLBACK(factory_bind),
                   GINT_TO_POINTER(ML_COL_INST));
  g_signal_connect(factory, "setup", G_CALLBACK(factory_setup), nullptr);

  column = gtk_column_view_column_new(_("Installed"), factory);
  gtk_column_view_append_column(GTK_COLUMN_VIEW(main_list), column);

  factory = gtk_signal_list_item_factory_new();
  g_signal_connect(factory, "bind", G_CALLBACK(factory_bind),
                   GINT_TO_POINTER(ML_COL_TYPE));
  g_signal_connect(factory, "setup", G_CALLBACK(factory_setup), nullptr);

  column = gtk_column_view_column_new(Q_("?modpack:Type"), factory);
  gtk_column_view_append_column(GTK_COLUMN_VIEW(main_list), column);

  factory = gtk_signal_list_item_factory_new();
  g_signal_connect(factory, "bind", G_CALLBACK(factory_bind),
                   GINT_TO_POINTER(ML_COL_SUBTYPE));
  g_signal_connect(factory, "setup", G_CALLBACK(factory_setup), nullptr);

  column = gtk_column_view_column_new(_("Subtype"), factory);
  gtk_column_view_append_column(GTK_COLUMN_VIEW(main_list), column);

  factory = gtk_signal_list_item_factory_new();
  g_signal_connect(factory, "bind", G_CALLBACK(factory_bind),
                   GINT_TO_POINTER(ML_COL_LIC));
  g_signal_connect(factory, "setup", G_CALLBACK(factory_setup), nullptr);

  column = gtk_column_view_column_new(_("License"), factory);
  gtk_column_view_append_column(GTK_COLUMN_VIEW(main_list), column);

  factory = gtk_signal_list_item_factory_new();
  g_signal_connect(factory, "bind", G_CALLBACK(factory_bind),
                   GINT_TO_POINTER(ML_COL_URL));
  g_signal_connect(factory, "setup", G_CALLBACK(factory_setup), nullptr);

  column = gtk_column_view_column_new(_("URL"), factory);
  gtk_column_view_append_column(GTK_COLUMN_VIEW(main_list), column);

  install_button = gtk_button_new();
  gtk_button_set_label(GTK_BUTTON(install_button), _("Install modpack"));

  Ubox = gtk_grid_new();
  gtk_widget_set_halign(Ubox, GTK_ALIGN_CENTER);
  gtk_grid_set_column_spacing(GTK_GRID(Ubox), 4);
  URL_label = gtk_label_new_with_mnemonic(_("Modpack URL"));

  URL_input = gtk_entry_new();
  gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(URL_input)),
                            DEFAULT_URL_START, -1);
  g_signal_connect(URL_input, "activate",
		   G_CALLBACK(URL_return), nullptr);

  g_signal_connect(install_button, "clicked",
                   G_CALLBACK(install_clicked), URL_input);

  gtk_grid_attach(GTK_GRID(Ubox), URL_label, 0, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(Ubox), URL_input, 0, 1, 1, 1);

  progressbar = gtk_progress_bar_new();

  statusbar = gtk_label_new(_("Select modpack to install"));

  gtk_widget_set_hexpand(main_list, TRUE);
  gtk_widget_set_vexpand(main_list, TRUE);

  gtk_grid_attach(GTK_GRID(mbox), version_label, 0, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(mbox), main_list, 0, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(mbox), Ubox, 0, 2, 1, 1);
  gtk_grid_attach(GTK_GRID(mbox), install_button, 0, 3, 1, 1);
  gtk_grid_attach(GTK_GRID(mbox), progressbar, 0, 4, 1, 1);
  gtk_grid_attach(GTK_GRID(mbox), statusbar, 0, 5, 1, 1);

  gtk_window_set_child(GTK_WINDOW(toplevel), mbox);

  errmsg = download_modpack_list(&fcmp, setup_modpack_list, msg_callback);

  g_object_set(main_list, "has-tooltip", TRUE, nullptr);
  g_signal_connect(main_list, "query-tooltip",
                   G_CALLBACK(query_main_list_tooltip_cb), nullptr);

  if (errmsg != nullptr) {
    gtk_label_set_text(GTK_LABEL(statusbar), errmsg);
  }
}

/**********************************************************************//**
  Run the gui
**************************************************************************/
static void activate_gui(GtkApplication *app, gpointer data)
{
  quit_dialog = nullptr;

  toplevel = gtk_application_window_new(app);

  gtk_widget_realize(toplevel);
  gtk_widget_set_name(toplevel, "Freeciv-modpack");
  gtk_window_set_title(GTK_WINDOW(toplevel),
                       _("Freeciv modpack installer (gtk4x)"));

#if 0
    /* Keep the icon of the executable on Windows */
#ifndef FREECIV_MSWINDOWS
  {
    /* Unlike main client, this only works if installed. Ignore any
     * errors loading the icon. */
    GError *err;

    (void) gtk_window_set_icon_from_file(GTK_WINDOW(toplevel),
                                         MPICON_PATH, &err);
  }
#endif /* FREECIV_MSWINDOWS */
#endif /* 0 */

  g_signal_connect(toplevel, "close_request",
                   G_CALLBACK(quit_dialog_callback), nullptr);

  modinst_setup_widgets();

  gtk_widget_set_visible(toplevel, TRUE);

  if (fcmp.autoinstall != nullptr) {
    gui_download_modpack(fcmp.autoinstall);
  }
}

/**********************************************************************//**
  Entry point of the freeciv-modpack program
**************************************************************************/
int main(int argc, char *argv[])
{
  int ui_options;

  fcmp_init();

  /* This modifies argv! */
  ui_options = fcmp_parse_cmdline(argc, argv);

  if (ui_options != -1) {
    int i;

    for (i = 1; i <= ui_options; i++) {
      if (is_option("--help", argv[i])) {
        /* TRANS: No full stop after the URL, could cause confusion. */
        fc_fprintf(stderr, _("Report bugs at %s\n"), BUG_URL);

        ui_options = -1;
      }
    }
  }

  if (ui_options != -1) {
    load_install_info_lists(&fcmp);

    if (gtk_init_check()) {
      fcmp_app = gtk_application_new(nullptr, 0);
      g_signal_connect(fcmp_app, "activate",
                       G_CALLBACK(activate_gui), nullptr);
      g_application_run(G_APPLICATION(fcmp_app), 0, nullptr);

      g_object_unref(fcmp_app);
    } else {
      log_fatal(_("Failed to open graphical mode."));
    }

    close_mpdbs();
  }

  fcmp_deinit();
  cmdline_option_values_free();

  return EXIT_SUCCESS;
}
