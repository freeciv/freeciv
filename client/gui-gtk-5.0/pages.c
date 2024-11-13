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

#include <stdio.h>
#include <time.h>

#include <sys/stat.h>

#include <gtk/gtk.h>

/* utility */
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "shared.h"
#include "support.h"

/* common */
#include "chat.h"
#include "dataio.h"
#include "game.h"
#include "mapimg.h"
#include "version.h"

/* client */
#include "client_main.h"
#include "climisc.h"
#include "clinet.h"
#include "connectdlg_common.h"
#include "packhand.h"
#include "servers.h"
#include "update_queue.h"

/* client/gui-gtk-5.0 */
#include "chatline.h"
#include "connectdlg.h"
#include "dialogs.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapview.h"
#include "menu.h"
#include "optiondlg.h"
#include "plrdlg.h"             /* get_flag() */
#include "repodlgs.h"
#include "voteinfo_bar.h"

#include "pages.h"


static GtkWidget *scenario_description;
static GtkWidget *scenario_authors;
static GtkWidget *scenario_filename;
static GtkWidget *scenario_version;

static GtkListStore *load_store, *scenario_store, *meta_store, *lan_store;

static GtkListStore *server_playerlist_store;
static GtkWidget *server_playerlist_view;

static GtkTreeSelection *load_selection, *scenario_selection;
static GtkTreeSelection *meta_selection, *lan_selection;

/* This is the current page. Invalid value at start, to be sure that it won't
 * be caught through a switch() statement. */
static enum client_pages current_page = -1;

struct server_scan_timer_data
{
  struct server_scan *scan;
  guint timer;
};

static struct server_scan_timer_data meta_scan = { NULL, 0 };
static struct server_scan_timer_data lan_scan = { NULL, 0 };

static GtkWidget *statusbar, *statusbar_frame;
static GQueue *statusbar_queue;
static guint statusbar_timer = 0;

static GtkWidget *ruleset_combo;

static GtkWidget *conn_popover = NULL;

static bool holding_srv_list_mutex = FALSE;

static void connection_state_reset(void);

#define FC_TYPE_HOST_ROW (fc_host_row_get_type())

G_DECLARE_FINAL_TYPE(FcHostRow, fc_host_row, FC, HOST_ROW, GObject)

struct _FcHostRow
{
  GObject parent_instance;

  char *host;
  int port;
  char *version;
  char *state;
  int nplayers;
  char *humans;
  char *message;
};

struct _FcHostClass
{
  GObjectClass parent_class;
};

G_DEFINE_TYPE(FcHostRow, fc_host_row, G_TYPE_OBJECT)

enum host_rows {
  host_row_host,
  host_row_port,
  host_row_version,
  host_row_state,
  host_row_nplayers,
  host_row_humans,
  host_row_message
};


#define FC_TYPE_PLR_ROW (fc_plr_row_get_type())

G_DECLARE_FINAL_TYPE(FcPlrRow, fc_plr_row, FC, PLR_ROW, GObject)

struct _FcPlrRow
{
  GObject parent_instance;

  char *name;
  char *type;
  char *host;
  char *nation;
};

struct _FcPlrClass
{
  GObjectClass parent_class;
};

G_DEFINE_TYPE(FcPlrRow, fc_plr_row, G_TYPE_OBJECT)

enum plr_rows {
  plr_row_name,
  plr_row_type,
  plr_row_host,
  plr_row_nation
};


#define FC_TYPE_SCEN_ROW (fc_scen_row_get_type())

G_DECLARE_FINAL_TYPE(FcScenRow, fc_scen_row, FC, SCEN_ROW, GObject)

struct _FcScenRow
{
  GObject parent_instance;

  char *name;
  char *desc;
  char *authors;
  char *filename;
  int ver;
};

struct _FcScenClass
{
  GObjectClass parent_class;
};

G_DEFINE_TYPE(FcScenRow, fc_scen_row, G_TYPE_OBJECT)


#define FC_TYPE_SAVE_ROW (fc_save_row_get_type())

G_DECLARE_FINAL_TYPE(FcSaveRow, fc_save_row, FC, SAVE_ROW, GObject)

struct _FcSaveRow
{
  GObject parent_instance;

  char *pretty_name;
  char *full_path;
};

struct _FcSaveClass
{
  GObjectClass parent_class;
};

G_DEFINE_TYPE(FcSaveRow, fc_save_row, G_TYPE_OBJECT)

/**********************************************************************//**
  Initialization method for FcHostRow class
**************************************************************************/
static void
fc_host_row_class_init(FcHostRowClass *klass)
{
}

/**********************************************************************//**
  Initialization method for FcHostRow
**************************************************************************/
static void
fc_host_row_init(FcHostRow *self)
{
}

/**********************************************************************//**
  FcHostRow creation method
**************************************************************************/
#if 0
static FcHostRow *fc_host_row_new(void)
{
  FcHostRow *result;

  result = g_object_new(FC_TYPE_HOST_ROW, nullptr);

  return result;
}
#endif

/**********************************************************************//**
  Initialization method for FcPlrRow class
**************************************************************************/
static void
fc_plr_row_class_init(FcPlrRowClass *klass)
{
}

/**********************************************************************//**
  Initialization method for FcPlrRow
**************************************************************************/
static void
fc_plr_row_init(FcPlrRow *self)
{
}

/**********************************************************************//**
  FcPlrRow creation method
**************************************************************************/
#if 0
static FcPlrRow *fc_plr_row_new(void)
{
  FcPlrRow *result;

  result = g_object_new(FC_TYPE_PLR_ROW, nullptr);

  return result;
}
#endif

/**********************************************************************//**
  Initialization method for FcScenRow class
**************************************************************************/
static void
fc_scen_row_class_init(FcScenRowClass *klass)
{
}

/**********************************************************************//**
  Initialization method for FcScenRow
**************************************************************************/
static void
fc_scen_row_init(FcScenRow *self)
{
}

/**********************************************************************//**
  FcScenRow creation method
**************************************************************************/
#if 0
static FcScenRow *fc_scen_row_new(void)
{
  FcScenRow *result;

  result = g_object_new(FC_TYPE_SCEN_ROW, nullptr);

  return result;
}
#endif

/**********************************************************************//**
  Initialization method for FcSaveRow class
**************************************************************************/
static void
fc_save_row_class_init(FcSaveRowClass *klass)
{
}

/**********************************************************************//**
  Initialization method for FcSaveRow
**************************************************************************/
static void
fc_save_row_init(FcSaveRow *self)
{
}

/**********************************************************************//**
  FcSaveRow creation method
**************************************************************************/
#if 0
static FcSaveRow *fc_save_row_new(void)
{
  FcSaveRow *result;

  result = g_object_new(FC_TYPE_SAVE_ROW, nullptr);

  return result;
}
#endif

/**********************************************************************//**
  Spawn a server, if there isn't one, using the default settings.
**************************************************************************/
static void start_new_game_callback(GtkWidget *w, gpointer data)
{
  if (!is_server_running()) {
    client_start_server();

    /* Saved settings are sent in client/options.c
     * resend_desired_settable_options() */
  }
}

/**********************************************************************//**
  Go to the scenario page, spawning a server.
**************************************************************************/
static void start_scenario_callback(GtkWidget *w, gpointer data)
{
  output_window_append(ftc_client, _("Compiling scenario list."));
  client_start_server_and_set_page(PAGE_SCENARIO);
}

/**********************************************************************//**
  Go to the load page, spawning a server.
**************************************************************************/
static void load_saved_game_callback(GtkWidget *w, gpointer data)
{
  client_start_server_and_set_page(PAGE_LOAD);
}

/**********************************************************************//**
  Reset the connection status and switch to network page.
**************************************************************************/
static void connect_network_game_callback(GtkWidget *w, gpointer data)
{
  connection_state_reset();
  set_client_page(PAGE_NETWORK);
}

/**********************************************************************//**
  Callback to open settings dialog.
**************************************************************************/
static void open_settings(void)
{
  option_dialog_popup(_("Set local options"), client_optset);
}

/**********************************************************************//**
  Cancel, by terminating the connection and going back to main page.
**************************************************************************/
static void main_callback(GtkWidget *w, gpointer data)
{
  enum client_pages page = PAGE_MAIN;

  if (client.conn.used) {
    disconnect_from_server(TRUE);
  }
  if (page != get_client_page()) {
    set_client_page(page);
  }
}

/**********************************************************************//**
  This is called whenever the intro graphic needs a graphics refresh.
**************************************************************************/
static void intro_expose(GtkDrawingArea *w, cairo_t *cr,
                         int width, int height, gpointer data)
{
  static PangoLayout *layout;
  PangoFontDescription* desc;
  static int pwidth, pheight;
  static bool left = FALSE;
  GtkAllocation allocation;
  struct sprite *intro = (struct sprite *)data;

  cairo_set_source_surface(cr, intro->surface, 0, 0);
  cairo_paint(cr);

  if (!layout) {
    char msgbuf[128];
    const char *rev_ver;

    layout = pango_layout_new(gtk_widget_create_pango_context(GTK_WIDGET(w)));
    desc = pango_font_description_from_string("Sans Bold 10");
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);

    rev_ver = fc_git_revision();

    if (rev_ver == NULL) {
      /* TRANS: "version 2.6.0, gui-gtk-3.22 client" */
      fc_snprintf(msgbuf, sizeof(msgbuf), _("%s%s, %s client"),
                  word_version(), VERSION_STRING, client_string);
    } else {
      /* TRANS: "version 2.6.0
       *         commit: [modified] <git commit id>
       *         gui-gtk-3.22 client" */
      fc_snprintf(msgbuf, sizeof(msgbuf), _("%s%s\ncommit: %s\n%s client"),
                  word_version(), VERSION_STRING, rev_ver, client_string);
      left = TRUE;
    }
    pango_layout_set_text(layout, msgbuf, -1);

    pango_layout_get_pixel_size(layout, &pwidth, &pheight);
  }
  gtk_widget_get_allocation(GTK_WIDGET(w), &allocation);

  cairo_set_source_rgb(cr, 0, 0, 0);
  cairo_move_to(cr, left ? 4 : allocation.width - pwidth - 3,
                allocation.height - pheight - 3);
  pango_cairo_show_layout(cr, layout);

  cairo_set_source_rgb(cr, 1, 1, 1);
  cairo_move_to(cr, left ? 3 : allocation.width - pwidth - 4,
                 allocation.height - pheight - 4);
  pango_cairo_show_layout(cr, layout);
}

/**********************************************************************//**
  This is called when main page is getting destroyed.
**************************************************************************/
static void intro_free(GtkWidget *w, gpointer *data)
{
  struct sprite *intro = (struct sprite *)data;

  free_sprite(intro);
}

/**********************************************************************//**
  Create the main page.
**************************************************************************/
GtkWidget *create_main_page(void)
{
  GtkWidget *widget, *vgrid, *frame, *darea, *button, *table;
  GtkSizeGroup *size;
  struct sprite *intro_in, *intro;
  int width, height;
  int sh;
  int space_needed;
  int grid_row = 0;

  size = gtk_size_group_new(GTK_SIZE_GROUP_BOTH);

  vgrid = gtk_grid_new();
  gtk_orientable_set_orientation(GTK_ORIENTABLE(vgrid),
                                 GTK_ORIENTATION_VERTICAL);
  widget = vgrid;

  frame = gtk_frame_new(NULL);
  gtk_widget_set_margin_bottom(frame, 18);
  gtk_widget_set_margin_end(frame, 18);
  gtk_widget_set_margin_start(frame, 18);
  gtk_widget_set_margin_top(frame, 18);
  gtk_widget_set_halign(frame, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(vgrid), frame, 0, grid_row++, 1, 1);

  intro_in = load_gfxfile(tileset_main_intro_filename(tileset), FALSE);
  get_sprite_dimensions(intro_in, &width, &height);
  sh = screen_height();

  if (sh <= 0) {
    /* Assume some minimum height */
    sh = 600;
  }

  space_needed = 250;
#if IS_BETA_VERSION || IS_DEVEL_VERSION
  /* Alpha or Beta notice takes extra space */
  space_needed += 50;
#endif

  if (sh - height < space_needed) {
    float scale;

    if (sh < (space_needed + 0.2 * height)) {
      /* Screen is simply too small, use minimum scale */
      scale = 0.2;
    } else  {
      scale = (double)(sh - space_needed) / height;
    }
    height *= scale;
    width *= scale;
    intro = sprite_scale(intro_in, width, height);
    free_sprite(intro_in);
  } else {
    intro = intro_in;
  }
  darea = gtk_drawing_area_new();
  gtk_widget_set_size_request(darea, width, height);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(darea),
                                 intro_expose, intro, NULL);
  g_signal_connect(widget, "destroy",
                   G_CALLBACK(intro_free), intro);
  gtk_frame_set_child(GTK_FRAME(frame), darea);

#if IS_BETA_VERSION || IS_DEVEL_VERSION
  {
    GtkWidget *label;

    label = gtk_label_new(unstable_message());
    gtk_widget_set_name(label, "beta_label");
    gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    gtk_grid_attach(GTK_GRID(vgrid), label, 0, grid_row++, 1, 1);
  }
#endif /* IS_BETA_VERSION || IS_DEVEL_VERSION */

  table = gtk_grid_new();
  gtk_widget_set_margin_bottom(table, 12);
  gtk_widget_set_margin_end(table, 12);
  gtk_widget_set_margin_start(table, 12);
  gtk_widget_set_margin_top(table, 12);
  gtk_widget_set_hexpand(table, TRUE);
  gtk_widget_set_vexpand(table, TRUE);
  gtk_widget_set_halign(table, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(table, GTK_ALIGN_CENTER);

  gtk_grid_set_row_spacing(GTK_GRID(table), 8);
  gtk_grid_set_column_spacing(GTK_GRID(table), 18);
  gtk_grid_attach(GTK_GRID(vgrid), table, 0, grid_row++, 1, 1);

  button = gtk_button_new_with_mnemonic(_("Start _New Game"));
  gtk_size_group_add_widget(size, button);
  gtk_grid_attach(GTK_GRID(table), button, 0, 0, 1, 1);
  gtk_widget_set_tooltip_text(button,
     _("Launches local server, and connects to it for a single-player game."));
  g_signal_connect(button, "clicked",
                   G_CALLBACK(start_new_game_callback), NULL);

  button = gtk_button_new_with_mnemonic(_("Start _Scenario Game"));
  gtk_size_group_add_widget(size, button);
  gtk_grid_attach(GTK_GRID(table), button, 0, 1, 1, 1);
  gtk_widget_set_tooltip_text(button,
     _("Loads one of the scenarios for a single-player game. "
       "Tutorial is one of the scenarios."));
  g_signal_connect(button, "clicked",
                   G_CALLBACK(start_scenario_callback), NULL);

  button = gtk_button_new_with_mnemonic(_("_Load Saved Game"));
  gtk_size_group_add_widget(size, button);
  gtk_grid_attach(GTK_GRID(table), button, 0, 2, 1, 1);
  gtk_widget_set_tooltip_text(button,
     _("Continues previously saved single-player game."));
  g_signal_connect(button, "clicked",
                   G_CALLBACK(load_saved_game_callback), NULL);

  button = gtk_button_new_with_mnemonic(_("C_onnect to Network Game"));
  gtk_size_group_add_widget(size, button);
  gtk_grid_attach(GTK_GRID(table), button, 1, 0, 1, 1);
  gtk_widget_set_tooltip_text(button,
     _("Connects to outside server. "
       "Sometimes you want to launch a separate server even for local games."));
  g_signal_connect(button, "clicked",
                   G_CALLBACK(connect_network_game_callback), NULL);

  button = gtk_button_new_with_mnemonic(_("Client Settings"));
  gtk_size_group_add_widget(size, button);
  gtk_grid_attach(GTK_GRID(table), button, 1, 1, 1, 1);
  gtk_widget_set_tooltip_text(button,
                              _("Adjusting client-side options."));
  g_signal_connect(button, "clicked", open_settings, NULL);

  button = icon_label_button_new("application-exit", _("E_xit"));
  gtk_size_group_add_widget(size, button);
  g_object_unref(size);
  gtk_grid_attach(GTK_GRID(table), button, 1, 2, 1, 1);
  gtk_widget_set_tooltip_text(button,
                              _("Gives you a break from playing freeciv."));
  g_signal_connect(button, "clicked",
                   G_CALLBACK(quit_gtk_main), NULL);

  return widget;
}

/****************************************************************************
                            GENERIC SAVE DIALOG
****************************************************************************/
typedef void (*save_dialog_action_fn_t) (const char *filename);
typedef struct fileinfo_list * (*save_dialog_files_fn_t) (void);

struct save_dialog {
  GtkDialog *shell;
  GtkTreeView *tree_view;
  GtkEntry *entry;
  save_dialog_action_fn_t action;
  save_dialog_files_fn_t files;
};

enum save_dialog_columns {
  SD_COL_PRETTY_NAME = 0,
  SD_COL_FULL_PATH,

  SD_COL_NUM
};

enum save_dialog_response {
  SD_RES_BROWSE,
  SD_RES_DELETE,
  SD_RES_SAVE
};

/**********************************************************************//**
  Create a new file list store.
**************************************************************************/
static inline GtkListStore *save_dialog_store_new(void)
{
  return gtk_list_store_new(SD_COL_NUM,
                            G_TYPE_STRING,      /* SD_COL_PRETTY_NAME */
                            G_TYPE_STRING);     /* SD_COL_FULL_PATH */
}

/**********************************************************************//**
  Fill a file list store with 'files'.
**************************************************************************/
static void save_dialog_store_update(GtkListStore *store,
                                     const struct fileinfo_list *files)
{
  GtkTreeIter iter;

  gtk_list_store_clear(store);
  fileinfo_list_iterate(files, pfile) {
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter,
                       SD_COL_PRETTY_NAME, pfile->name,
                       SD_COL_FULL_PATH, pfile->fullname,
                       -1);
  } fileinfo_list_iterate_end;
}

/**********************************************************************//**
  Update a save dialog.
**************************************************************************/
static void save_dialog_update(struct save_dialog *pdialog)
{
  struct fileinfo_list *files;

  fc_assert_ret(NULL != pdialog);

  /* Update the store. */
  files = pdialog->files();
  save_dialog_store_update(GTK_LIST_STORE
                           (gtk_tree_view_get_model(pdialog->tree_view)),
                           files);
  fileinfo_list_destroy(files);
}

/**********************************************************************//**
  Callback for save_dialog_file_chooser_new().
**************************************************************************/
static void save_dialog_file_chooser_callback(GtkWidget *widget,
                                              gint response, gpointer data)
{
  if (response == GTK_RESPONSE_OK) {
    save_dialog_action_fn_t action = data;
    GFile *file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(widget));

    if (file != NULL) {
      gchar *filename = g_file_get_parse_name(file);

      if (NULL != filename) {
        action(filename);
        g_free(filename);
      }

      g_object_unref(file);
    }
  }

  gtk_window_destroy(GTK_WINDOW(widget));
}

/**********************************************************************//**
  Create a file chooser for both the load and save commands.
**************************************************************************/
static void save_dialog_file_chooser_popup(const char *title,
                                           GtkFileChooserAction action,
                                           save_dialog_action_fn_t cb)
{
  GtkWidget *filechoose;

  /* Create the chooser */
  filechoose = gtk_file_chooser_dialog_new(title, GTK_WINDOW(toplevel), action,
                                           _("_Cancel"), GTK_RESPONSE_CANCEL,
                                           (action == GTK_FILE_CHOOSER_ACTION_SAVE) ?
                                           _("_Save") : _("_Open"),
                                           GTK_RESPONSE_OK, NULL);
  setup_dialog(filechoose, toplevel);

  g_signal_connect(filechoose, "response",
                   G_CALLBACK(save_dialog_file_chooser_callback), cb);

  /* Display that dialog */
  gtk_window_present(GTK_WINDOW(filechoose));
}

/**********************************************************************//**
  Handle save dialog response.
**************************************************************************/
static void save_dialog_response_callback(GtkWidget *w, gint response,
                                          gpointer data)
{
  struct save_dialog *pdialog = data;

  switch (response) {
  case SD_RES_BROWSE:
    save_dialog_file_chooser_popup(_("Select Location to Save"),
                                   GTK_FILE_CHOOSER_ACTION_SAVE,
                                   pdialog->action);
    break;
  case SD_RES_DELETE:
    {
      GtkTreeSelection *selection;
      GtkTreeModel *model;
      GtkTreeIter iter;
      const gchar *full_path;

      selection = gtk_tree_view_get_selection(pdialog->tree_view);
      if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        return;
      }

      gtk_tree_model_get(model, &iter, SD_COL_FULL_PATH, &full_path, -1);
      fc_remove(full_path);
      save_dialog_update(pdialog);
    }
    return;
  case SD_RES_SAVE:
    {
      const char *text = gtk_entry_buffer_get_text(gtk_entry_get_buffer(pdialog->entry));
      gchar *filename = g_filename_from_utf8(text, -1, NULL, NULL, NULL);

      if (NULL == filename) {
        return;
      }
      pdialog->action(filename);
      g_free(filename);
    }
    break;
  default:
    break;
  }

  gtk_window_destroy(GTK_WINDOW(pdialog->shell));
}

/**********************************************************************//**
  Handle save list double click.
**************************************************************************/
static void save_dialog_row_callback(GtkTreeView *tree_view,
                                     GtkTreePath *path,
                                     GtkTreeViewColumn *column,
                                     gpointer data)
{
  save_dialog_response_callback(NULL, SD_RES_SAVE, data);
}

/**********************************************************************//**
  Handle save filename entry activation.
**************************************************************************/
static void save_dialog_entry_callback(GtkEntry *entry, gpointer data)
{
  save_dialog_response_callback(NULL, SD_RES_SAVE, data);
}

/**********************************************************************//**
  Handle the save list selection changes.
**************************************************************************/
static void save_dialog_list_callback(GtkTreeSelection *selection,
                                      gpointer data)
{
  struct save_dialog *pdialog = data;
  GtkTreeModel *model;
  GtkTreeIter iter;
  const gchar *filename;

  if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
    gtk_dialog_set_response_sensitive(pdialog->shell, SD_RES_DELETE, FALSE);
    return;
  }

  gtk_dialog_set_response_sensitive(pdialog->shell, SD_RES_DELETE, TRUE);
  gtk_tree_model_get(model, &iter, SD_COL_PRETTY_NAME, &filename, -1);
  gtk_entry_buffer_set_text(gtk_entry_get_buffer(pdialog->entry), filename, -1);
}

/**********************************************************************//**
  Save table cell bind function
**************************************************************************/
static void save_factory_bind(GtkSignalListItemFactory *self,
                              GtkListItem *list_item,
                              gpointer user_data)
{
  FcSaveRow *row;
  GtkWidget *child = gtk_list_item_get_child(list_item);

  row = gtk_list_item_get_item(list_item);

  gtk_label_set_text(GTK_LABEL(child), row->pretty_name);
}

/**********************************************************************//**
  Save table cell setup function
**************************************************************************/
static void save_factory_setup(GtkSignalListItemFactory *self,
                               GtkListItem *list_item,
                               gpointer user_data)
{
  gtk_list_item_set_child(list_item, gtk_label_new(""));
}

/**********************************************************************//**
  Create a new save dialog.
**************************************************************************/
static GtkWidget *save_dialog_new(const char *title, const char *savelabel,
                                  const char *savefilelabel,
                                  save_dialog_action_fn_t action,
                                  save_dialog_files_fn_t files)
{
  GtkWidget *shell, *sbox, *sw, *label, *view, *entry;
  GtkBox *vbox;
  GtkListStore *store;
  GtkCellRenderer *rend;
  GtkListItemFactory *factory;
  GtkTreeSelection *selection;
  struct save_dialog *pdialog;

  fc_assert_ret_val(NULL != action, NULL);
  fc_assert_ret_val(NULL != files, NULL);

  /* Save dialog structure. */
  pdialog = fc_malloc(sizeof(*pdialog));
  pdialog->action = action;
  pdialog->files = files;

  /* Shell. */
  shell = gtk_dialog_new_with_buttons(title, NULL, 0,
                                      _("_Browse..."), SD_RES_BROWSE,
                                      _("_Delete"), SD_RES_DELETE,
                                      _("_Cancel"), GTK_RESPONSE_CANCEL,
                                      _("_Save"), SD_RES_SAVE,
                                      NULL);
  g_object_set_data_full(G_OBJECT(shell), "save_dialog", pdialog,
                         (GDestroyNotify) free);
  gtk_dialog_set_default_response(GTK_DIALOG(shell), GTK_RESPONSE_CANCEL);
  gtk_dialog_set_response_sensitive(GTK_DIALOG(shell), SD_RES_DELETE, FALSE);
  setup_dialog(shell, toplevel);
  g_signal_connect(shell, "response",
                   G_CALLBACK(save_dialog_response_callback), pdialog);
  pdialog->shell = GTK_DIALOG(shell);
  vbox = GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(shell)));

  /* Tree view. */
  store = save_dialog_store_new();

  factory = gtk_signal_list_item_factory_new();
  g_signal_connect(factory, "bind", G_CALLBACK(save_factory_bind),
                   nullptr);
  g_signal_connect(factory, "setup", G_CALLBACK(save_factory_setup),
                   nullptr);

  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  gtk_widget_set_hexpand(view, TRUE);
  gtk_widget_set_vexpand(view, TRUE);
  g_object_unref(store);
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);
  g_signal_connect(view, "row-activated",
                   G_CALLBACK(save_dialog_row_callback), pdialog);
  pdialog->tree_view = GTK_TREE_VIEW(view);

  sbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_box_append(vbox, sbox);

  label = g_object_new(GTK_TYPE_LABEL,
                       "use-underline", TRUE,
                       "mnemonic-widget", view,
                       "label", savelabel,
                       "xalign", 0.0,
                       "yalign", 0.5,
                       NULL);
  gtk_box_append(GTK_BOX(sbox), label);

  sw = gtk_scrolled_window_new();
  gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(sw), 300);
  gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(sw), 300);
  gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(sw), TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), view);
  gtk_box_append(GTK_BOX(sbox), sw);

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
  g_signal_connect(selection, "changed",
                   G_CALLBACK(save_dialog_list_callback), pdialog);

  rend = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
                                              -1, NULL, rend, "text",
                                              SD_COL_PRETTY_NAME, NULL);

  /* Entry. */
  entry = gtk_entry_new();
  gtk_widget_set_hexpand(entry, TRUE);
  g_signal_connect(entry, "activate",
                   G_CALLBACK(save_dialog_entry_callback), pdialog);
  pdialog->entry = GTK_ENTRY(entry);

  sbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_margin_bottom(sbox, 12);
  gtk_widget_set_margin_end(sbox, 12);
  gtk_widget_set_margin_start(sbox, 12);
  gtk_widget_set_margin_top(sbox, 12);

  label = g_object_new(GTK_TYPE_LABEL,
                       "use-underline", TRUE,
                       "mnemonic-widget", entry,
                       "label", savefilelabel,
                       "xalign", 0.0,
                       "yalign", 0.5,
                       NULL);
  gtk_box_append(GTK_BOX(sbox), label);

  gtk_box_append(GTK_BOX(sbox), entry);
  gtk_box_append(vbox, sbox);

  save_dialog_update(pdialog);
  gtk_window_set_focus(GTK_WINDOW(shell), entry);
  gtk_widget_set_visible(GTK_WIDGET(vbox), TRUE);

  return shell;
}

/****************************************************************************
                                 NETWORK PAGE
****************************************************************************/
static GtkWidget *network_login_label, *network_login;
static GtkWidget *network_host_label, *network_host;
static GtkWidget *network_port_label, *network_port;
static GtkWidget *network_password_label, *network_password;
static GtkWidget *network_confirm_password_label, *network_confirm_password;

/**********************************************************************//**
  Update a server list.
**************************************************************************/
static void update_server_list(enum server_scan_type sstype,
                               const struct server_list *list)
{
  GtkTreeSelection *sel = NULL;
  GtkTreeView *view;
  GtkTreeIter it;
  GtkListStore *store;
  const gchar *host, *portstr;
  int port;

  switch (sstype) {
  case SERVER_SCAN_LOCAL:
    sel = lan_selection;
    break;
  case SERVER_SCAN_GLOBAL:
    sel = meta_selection;
    break;
  default:
    break;
  }

  if (!sel) {
    return;
  }

  view = gtk_tree_selection_get_tree_view(sel);
  store = GTK_LIST_STORE(gtk_tree_view_get_model(view));
  gtk_list_store_clear(store);

  if (!list) {
    return;
  }

  host = gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(network_host)));
  portstr = gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(network_port)));
  port = atoi(portstr);

  server_list_iterate(list, pserver) {
    char buf[35];

    if (pserver->humans >= 0) {
      fc_snprintf(buf, sizeof(buf), "%d", pserver->humans);
    } else {
      sz_strlcpy(buf, _("Unknown"));
    }
    gtk_list_store_append(store, &it);
    gtk_list_store_set(store, &it,
                       0, pserver->host,
                       1, pserver->port,
                       2, pserver->version,
                       3, _(pserver->state),
                       4, pserver->nplayers,
                       5, buf,
                       6, pserver->message,
                       -1);
    if (strcmp(host, pserver->host) == 0 && port == pserver->port) {
      gtk_tree_selection_select_iter(sel, &it);
    }
  } server_list_iterate_end;
}

/**********************************************************************//**
  Free the server scans.
**************************************************************************/
void destroy_server_scans(void)
{
  if (meta_scan.scan) {
    server_scan_finish(meta_scan.scan);
    meta_scan.scan = NULL;
  }
  if (meta_scan.timer != 0) {
    g_source_remove(meta_scan.timer);
    meta_scan.timer = 0;
  }
  if (lan_scan.scan) {
    server_scan_finish(lan_scan.scan);
    lan_scan.scan = NULL;
  }
  if (lan_scan.timer != 0) {
    g_source_remove(lan_scan.timer);
    lan_scan.timer = 0;
  }
}

/**********************************************************************//**
  This function updates the list of servers every so often.
**************************************************************************/
static gboolean check_server_scan(gpointer data)
{
  struct server_scan_timer_data *scan_data = data;
  struct server_scan *scan = scan_data->scan;
  enum server_scan_status stat;

  if (!scan) {
    return FALSE;
  }

  stat = server_scan_poll(scan);
  if (stat >= SCAN_STATUS_PARTIAL) {
    enum server_scan_type type;
    struct srv_list *srvrs;

    type = server_scan_get_type(scan);
    srvrs = server_scan_get_list(scan);
    fc_mutex_allocate(&srvrs->mutex);
    holding_srv_list_mutex = TRUE;
    update_server_list(type, srvrs->servers);
    holding_srv_list_mutex = FALSE;
    fc_mutex_release(&srvrs->mutex);
  }

  if (stat == SCAN_STATUS_ERROR || stat == SCAN_STATUS_DONE) {
    scan_data->timer = 0;
    return FALSE;
  }
  return TRUE;
}

/**********************************************************************//**
  Callback function for when there's an error in the server scan.
**************************************************************************/
static void server_scan_error(struct server_scan *scan,
                              const char *message)
{
  output_window_append(ftc_client, message);
  log_error("%s", message);

  /* Main thread will finalize the scan later (or even concurrently) -
   * do not do anything here to cause double free or raze condition. */
}

/**********************************************************************//**
  Stop and restart the metaserver and lan server scans.
**************************************************************************/
static void update_network_lists(void)
{
  destroy_server_scans();

  meta_scan.scan = server_scan_begin(SERVER_SCAN_GLOBAL, server_scan_error);
  meta_scan.timer = g_timeout_add(200, check_server_scan, &meta_scan);

  lan_scan.scan = server_scan_begin(SERVER_SCAN_LOCAL, server_scan_error);
  lan_scan.timer = g_timeout_add(500, check_server_scan, &lan_scan);
}

/**************************************************************************
  Network connection state defines.
**************************************************************************/
enum connection_state {
  LOGIN_TYPE,
  NEW_PASSWORD_TYPE,
  ENTER_PASSWORD_TYPE,
  WAITING_TYPE
};

static enum connection_state connection_status;

/**********************************************************************//**
  Update statusbar label text.
**************************************************************************/
static gboolean update_network_statusbar(gpointer data)
{
  if (!g_queue_is_empty(statusbar_queue)) {
    char *txt;

    txt = g_queue_pop_head(statusbar_queue);
    gtk_label_set_text(GTK_LABEL(statusbar), txt);
    free(txt);
  }

  return TRUE;
}

/**********************************************************************//**
  Clear statusbar queue.
**************************************************************************/
static void clear_network_statusbar(void)
{
  while (!g_queue_is_empty(statusbar_queue)) {
    char *txt;

    txt = g_queue_pop_head(statusbar_queue);
    free(txt);
  }

  gtk_label_set_text(GTK_LABEL(statusbar), "");
}

/**********************************************************************//**
  Queue statusbar label text change.
**************************************************************************/
void append_network_statusbar(const char *text, bool force)
{
  if (gtk_widget_get_visible(statusbar_frame)) {
    if (force) {
      clear_network_statusbar();
      gtk_label_set_text(GTK_LABEL(statusbar), text);
    } else {
      g_queue_push_tail(statusbar_queue, fc_strdup(text));
    }
  }
}

/**********************************************************************//**
  Create statusbar.
**************************************************************************/
GtkWidget *create_statusbar(void)
{
  statusbar_frame = gtk_frame_new(NULL);

  statusbar = gtk_label_new("");
  gtk_widget_set_margin_start(statusbar, 2);
  gtk_widget_set_margin_end(statusbar, 2);
  gtk_widget_set_margin_top(statusbar, 2);
  gtk_widget_set_margin_bottom(statusbar, 2);
  gtk_frame_set_child(GTK_FRAME(statusbar_frame), statusbar);

  statusbar_queue = g_queue_new();
  statusbar_timer = g_timeout_add(2000, update_network_statusbar, NULL);

  return statusbar_frame;
}

/**********************************************************************//**
  Update network page connection state.
**************************************************************************/
static void set_connection_state(enum connection_state state)
{
  switch (state) {
  case LOGIN_TYPE:
    append_network_statusbar("", FALSE);

    gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(network_password)),
                              "", -1);
    gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(network_confirm_password)),
                              "", -1);

    gtk_widget_set_sensitive(network_host, TRUE);
    gtk_widget_set_sensitive(network_port, TRUE);
    gtk_widget_set_sensitive(network_login, TRUE);
    gtk_widget_set_sensitive(network_password_label, FALSE);
    gtk_label_set_markup_with_mnemonic(GTK_LABEL(network_password_label), _("Pass_word:"));
    gtk_widget_set_sensitive(network_password, FALSE);
    gtk_widget_set_sensitive(network_confirm_password_label, FALSE);
    gtk_widget_set_sensitive(network_confirm_password, FALSE);
    break;
  case NEW_PASSWORD_TYPE:
    set_client_page(PAGE_NETWORK);
    gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(network_password)), "", -1);
    gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(network_confirm_password)),
                              "", -1);

    gtk_widget_set_sensitive(network_host, FALSE);
    gtk_widget_set_sensitive(network_port, FALSE);
    gtk_widget_set_sensitive(network_login, FALSE);
    gtk_widget_set_sensitive(network_password_label, TRUE);
    gtk_label_set_markup_with_mnemonic(GTK_LABEL(network_password_label), _("New Pass_word:"));
    gtk_widget_set_sensitive(network_password, TRUE);
    gtk_widget_set_sensitive(network_confirm_password_label, TRUE);
    gtk_widget_set_sensitive(network_confirm_password, TRUE);

    gtk_widget_grab_focus(network_password);
    break;
  case ENTER_PASSWORD_TYPE:
    set_client_page(PAGE_NETWORK);
    gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(network_password)), "", -1);
    gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(network_confirm_password)),
                              "", -1);

    gtk_widget_set_sensitive(network_host, FALSE);
    gtk_widget_set_sensitive(network_port, FALSE);
    gtk_widget_set_sensitive(network_login, FALSE);
    gtk_widget_set_sensitive(network_password_label, TRUE);
    gtk_label_set_markup_with_mnemonic(GTK_LABEL(network_password_label), _("Pass_word:"));
    gtk_widget_set_sensitive(network_password, TRUE);
    gtk_widget_set_sensitive(network_confirm_password_label, FALSE);
    gtk_widget_set_sensitive(network_confirm_password, FALSE);

    gtk_widget_grab_focus(network_password);
    break;
  case WAITING_TYPE:
    append_network_statusbar("", TRUE);

    gtk_widget_set_sensitive(network_login, FALSE);
    gtk_widget_set_sensitive(network_password_label, FALSE);
    gtk_label_set_markup_with_mnemonic(GTK_LABEL(network_password_label), _("Pass_word:"));
    gtk_widget_set_sensitive(network_password, FALSE);
    gtk_widget_set_sensitive(network_confirm_password_label, FALSE);
    gtk_widget_set_sensitive(network_confirm_password, FALSE);
    break;
  }

  connection_status = state;
}

/**********************************************************************//**
  Reset the connection state.
**************************************************************************/
static void connection_state_reset(void)
{
  set_connection_state(LOGIN_TYPE);
}

/**********************************************************************//**
  Configure the dialog depending on what type of authentication request the
  server is making.
**************************************************************************/
void handle_authentication_req(enum authentication_type type,
                               const char *message)
{
  append_network_statusbar(message, TRUE);

  switch (type) {
  case AUTH_NEWUSER_FIRST:
  case AUTH_NEWUSER_RETRY:
    set_connection_state(NEW_PASSWORD_TYPE);
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
      set_connection_state(ENTER_PASSWORD_TYPE);
    }
    return;
  case AUTH_LOGIN_RETRY:
    set_connection_state(ENTER_PASSWORD_TYPE);
    return;
  }

  log_error("Unsupported authentication type %d: %s.", type, message);
}

/**********************************************************************//**
  If on the network page, switch page to the login page (with new server
  and port). if on the login page, send connect and/or authentication
  requests to the server.
**************************************************************************/
static void connect_callback(GtkWidget *w, gpointer data)
{
  char errbuf [512];
  struct packet_authentication_reply reply;

  switch (connection_status) {
  case LOGIN_TYPE:
    sz_strlcpy(user_name,
               gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(network_login))));
    sz_strlcpy(server_host,
               gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(network_host))));
    server_port = atoi(gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(network_port))));

    if (connect_to_server(user_name, server_host, server_port,
                          errbuf, sizeof(errbuf)) != -1) {
    } else {
      append_network_statusbar(errbuf, TRUE);

      output_window_append(ftc_client, errbuf);
    }
    return;
  case NEW_PASSWORD_TYPE:
    if (w != network_password) {
      sz_strlcpy(fc_password,
          gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(network_password))));
      sz_strlcpy(reply.password,
          gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(network_confirm_password))));
      if (!fc_strncmp(reply.password, fc_password, MAX_LEN_NAME)) {
        fc_password[0] = '\0';
        send_packet_authentication_reply(&client.conn, &reply);

        set_connection_state(WAITING_TYPE);
      } else {
        append_network_statusbar(_("Passwords don't match, enter password."),
                                 TRUE);

        set_connection_state(NEW_PASSWORD_TYPE);
      }
    }
    return;
  case ENTER_PASSWORD_TYPE:
    sz_strlcpy(reply.password,
               gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(network_password))));
    send_packet_authentication_reply(&client.conn, &reply);

    set_connection_state(WAITING_TYPE);
    return;
  case WAITING_TYPE:
    return;
  }

  log_error("Unsupported connection status: %d", connection_status);
}

/**********************************************************************//**
  Connect on list item double-click.
**************************************************************************/
static void network_activate_callback(GtkTreeView *view,
                                      GtkTreePath *arg1,
                                      GtkTreeViewColumn *arg2,
                                      gpointer data)
{
  connect_callback(NULL, data);
}

/**********************************************************************//**
  Fills the server player list with the players in the given server, or
  clears it if there is no player data.
**************************************************************************/
static void update_server_playerlist(const struct server *pserver)
{
  GtkListStore *store;
  GtkTreeIter iter;
  int n, i;

  store = server_playerlist_store;
  fc_assert_ret(store != NULL);

  gtk_list_store_clear(store);
  if (!pserver || !pserver->players) {
    return;
  }

  n = pserver->nplayers;
  for (i = 0; i < n; i++) {
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter,
                       0, pserver->players[i].name,
                       1, pserver->players[i].type,
                       2, pserver->players[i].host,
                       3, pserver->players[i].nation,
                       -1);
  }
}

/**********************************************************************//**
  Sets the host, port and player list of the selected server.
**************************************************************************/
static void network_list_callback(GtkTreeSelection *select, gpointer data)
{
  GtkTreeModel *model;
  GtkTreeIter it;
  const char *host;
  int port;
  char portstr[32];
  const struct server *pserver = NULL;

  if (!gtk_tree_selection_get_selected(select, &model, &it)) {
    return;
  }

  if (select == meta_selection) {
    GtkTreePath *path;
    struct srv_list *srvrs;

    srvrs = server_scan_get_list(meta_scan.scan);
    path = gtk_tree_model_get_path(model, &it);
    if (!holding_srv_list_mutex) {
      /* We are not yet inside mutex protected block */
      fc_mutex_allocate(&srvrs->mutex);
    }
    if (srvrs->servers && path) {
      gint pos = gtk_tree_path_get_indices(path)[0];

      pserver = server_list_get(srvrs->servers, pos);
    }
    if (!holding_srv_list_mutex) {
      /* We are not yet inside mutex protected block */
      fc_mutex_release(&srvrs->mutex);
    }
    gtk_tree_path_free(path);
  }
  update_server_playerlist(pserver);

  gtk_tree_model_get(model, &it, 0, &host, 1, &port, -1);

  gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(network_host)), host, -1);
  fc_snprintf(portstr, sizeof(portstr), "%d", port);
  gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(network_port)), portstr, -1);
}

/**********************************************************************//**
  Update the network page.
**************************************************************************/
static void update_network_page(void)
{
  char buf[256];

  gtk_tree_selection_unselect_all(lan_selection);
  gtk_tree_selection_unselect_all(meta_selection);

  gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(network_login)), user_name, -1);
  gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(network_host)), server_host, -1);
  fc_snprintf(buf, sizeof(buf), "%d", server_port);
  gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(network_port)), buf, -1);
}

/**********************************************************************//**
  Host table cell bind function
**************************************************************************/
static void host_factory_bind(GtkSignalListItemFactory *self,
                              GtkListItem *list_item,
                              gpointer user_data)
{
  FcHostRow *row;
  GtkWidget *child = gtk_list_item_get_child(list_item);
  char buf[512];

  row = gtk_list_item_get_item(list_item);

  switch (GPOINTER_TO_INT(user_data)) {
  case host_row_host:
    gtk_label_set_text(GTK_LABEL(child), row->host);
    break;
  case host_row_port:
    fc_snprintf(buf, sizeof(buf), "%d", row->port);
    gtk_label_set_text(GTK_LABEL(child), buf);
    break;
  case host_row_version:
    gtk_label_set_text(GTK_LABEL(child), row->version);
    break;
  case host_row_state:
    gtk_label_set_text(GTK_LABEL(child), row->state);
    break;
  case host_row_nplayers:
    fc_snprintf(buf, sizeof(buf), "%d", row->nplayers);
    gtk_label_set_text(GTK_LABEL(child), buf);
    break;
  case host_row_humans:
    gtk_label_set_text(GTK_LABEL(child), row->humans);
    break;
  case host_row_message:
    gtk_label_set_text(GTK_LABEL(child), row->message);
    break;
  }
}

/**********************************************************************//**
  Host table cell setup function
**************************************************************************/
static void host_factory_setup(GtkSignalListItemFactory *self,
                               GtkListItem *list_item,
                               gpointer user_data)
{
  gtk_list_item_set_child(list_item, gtk_label_new(""));
}

/**********************************************************************//**
  Player table cell bind function
**************************************************************************/
static void plr_factory_bind(GtkSignalListItemFactory *self,
                             GtkListItem *list_item,
                             gpointer user_data)
{
  FcPlrRow *row;
  GtkWidget *child = gtk_list_item_get_child(list_item);

  row = gtk_list_item_get_item(list_item);

  switch (GPOINTER_TO_INT(user_data)) {
  case plr_row_name:
    gtk_label_set_text(GTK_LABEL(child), row->name);
    break;
  case plr_row_type:
    gtk_label_set_text(GTK_LABEL(child), row->type);
    break;
  case plr_row_host:
    gtk_label_set_text(GTK_LABEL(child), row->host);
    break;
  case plr_row_nation:
    gtk_label_set_text(GTK_LABEL(child), row->nation);
    break;
  }
}

/**********************************************************************//**
  Player table cell setup function
**************************************************************************/
static void plr_factory_setup(GtkSignalListItemFactory *self,
                              GtkListItem *list_item,
                              gpointer user_data)
{
  gtk_list_item_set_child(list_item, gtk_label_new(""));
}

/**********************************************************************//**
  Create the network page.
**************************************************************************/
GtkWidget *create_network_page(void)
{
  GtkWidget *box, *sbox, *bbox, *hbox, *notebook;
  GtkWidget *button, *label, *view, *sw, *table;
  GtkTreeSelection *selection;
  GtkListStore *store;
  GtkEventController *controller;
  GtkListItemFactory *factory;

  box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_margin_start(box, 4);
  gtk_widget_set_margin_end(box, 4);
  gtk_widget_set_margin_top(box, 4);
  gtk_widget_set_margin_bottom(box, 4);

  notebook = gtk_notebook_new();
  gtk_box_append(GTK_BOX(box), notebook);

  /* LAN pane. */
  lan_store = gtk_list_store_new(7, G_TYPE_STRING, /* host */
                                 G_TYPE_INT,       /* port */
                                 G_TYPE_STRING,    /* version */
                                 G_TYPE_STRING,    /* state */
                                 G_TYPE_INT,       /* nplayers */
                                 G_TYPE_STRING,    /* humans */
                                 G_TYPE_STRING);   /* message */

  factory = gtk_signal_list_item_factory_new();
  g_signal_connect(factory, "bind", G_CALLBACK(host_factory_bind),
                   GINT_TO_POINTER(host_row_host));
  g_signal_connect(factory, "setup", G_CALLBACK(host_factory_setup),
                   nullptr);

  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(lan_store));
  gtk_widget_set_hexpand(view, TRUE);
  gtk_widget_set_vexpand(view, TRUE);
  g_object_unref(lan_store);
  gtk_tree_view_columns_autosize(GTK_TREE_VIEW(view));

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  lan_selection = selection;
  gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);

  controller = gtk_event_controller_focus_new();
  g_signal_connect(controller, "enter",
                   G_CALLBACK(terminate_signal_processing), NULL);
  gtk_widget_add_controller(view, controller);

  g_signal_connect(view, "row-activated",
                   G_CALLBACK(network_activate_callback), NULL);
  g_signal_connect(selection, "changed",
                   G_CALLBACK(network_list_callback), NULL);

  add_treeview_column(view, _("Server Name"), G_TYPE_STRING, 0);
  add_treeview_column(view, _("Port"), G_TYPE_INT, 1);
  add_treeview_column(view, _("Version"), G_TYPE_STRING, 2);
  add_treeview_column(view, _("Status"), G_TYPE_STRING, 3);
  add_treeview_column(view, _("Players"), G_TYPE_INT, 4);
  add_treeview_column(view, _("Humans"), G_TYPE_STRING, 5);
  add_treeview_column(view, _("Comment"), G_TYPE_STRING, 6);

  label = gtk_label_new_with_mnemonic(_("Local _Area Network"));

  sw = gtk_scrolled_window_new();
  gtk_widget_set_margin_start(sw, 4);
  gtk_widget_set_margin_end(sw, 4);
  gtk_widget_set_margin_top(sw, 4);
  gtk_widget_set_margin_bottom(sw, 4);
  gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(sw), TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), view);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), sw, label);


  /* Metaserver pane. */
  meta_store = gtk_list_store_new(7, G_TYPE_STRING, /* host */
                                  G_TYPE_INT,       /* port */
                                  G_TYPE_STRING,    /* version */
                                  G_TYPE_STRING,    /* state */
                                  G_TYPE_INT,       /* nplayers */
                                  G_TYPE_STRING,    /* humans */
                                  G_TYPE_STRING);   /* message */

  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(meta_store));
  gtk_widget_set_hexpand(view, TRUE);
  gtk_widget_set_vexpand(view, TRUE);
  g_object_unref(meta_store);
  gtk_tree_view_columns_autosize(GTK_TREE_VIEW(view));

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  meta_selection = selection;
  gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);

  controller = gtk_event_controller_focus_new();
  g_signal_connect(controller, "enter",
                   G_CALLBACK(terminate_signal_processing), NULL);
  gtk_widget_add_controller(view, controller);

  g_signal_connect(view, "row-activated",
                   G_CALLBACK(network_activate_callback), NULL);
  g_signal_connect(selection, "changed",
                   G_CALLBACK(network_list_callback), NULL);

  add_treeview_column(view, _("Server Name"), G_TYPE_STRING, 0);
  add_treeview_column(view, _("Port"), G_TYPE_INT, 1);
  add_treeview_column(view, _("Version"), G_TYPE_STRING, 2);
  add_treeview_column(view, _("Status"), G_TYPE_STRING, 3);
  add_treeview_column(view, _("Players"), G_TYPE_INT, 4);
  add_treeview_column(view, _("Humans"), G_TYPE_STRING, 5);
  add_treeview_column(view, _("Comment"), G_TYPE_STRING, 6);

  label = gtk_label_new_with_mnemonic(_("Internet _Metaserver"));

  sw = gtk_scrolled_window_new();
  gtk_widget_set_margin_start(sw, 4);
  gtk_widget_set_margin_end(sw, 4);
  gtk_widget_set_margin_top(sw, 4);
  gtk_widget_set_margin_bottom(sw, 4);
  gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(sw), TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), view);
  if (GUI_GTK_OPTION(metaserver_tab_first)) {
    gtk_notebook_prepend_page(GTK_NOTEBOOK(notebook), sw, label);
  } else {
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), sw, label);
  }

  /* Bottom part of the page, outside the inner notebook. */
  sbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_box_append(GTK_BOX(box), sbox);

  hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_margin_bottom(hbox, 8);
  gtk_widget_set_margin_end(hbox, 8);
  gtk_widget_set_margin_start(hbox, 8);
  gtk_widget_set_margin_top(hbox, 8);
  gtk_box_append(GTK_BOX(sbox), hbox);

  table = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(table), 2);
  gtk_grid_set_column_spacing(GTK_GRID(table), 12);
  gtk_box_append(GTK_BOX(hbox), table);

  network_host = gtk_entry_new();
  g_signal_connect(network_host, "activate",
      G_CALLBACK(connect_callback), NULL);
  gtk_grid_attach(GTK_GRID(table), network_host, 1, 0, 1, 1);

  label = g_object_new(GTK_TYPE_LABEL,
                       "use-underline", TRUE,
                       "mnemonic-widget", network_host,
                       "label", _("_Host:"),
                       "xalign", 0.0,
                       "yalign", 0.5,
                       NULL);
  network_host_label = label;
  gtk_grid_attach(GTK_GRID(table), label, 0, 0, 1, 1);

  network_port = gtk_entry_new();
  g_signal_connect(network_port, "activate",
      G_CALLBACK(connect_callback), NULL);
  gtk_grid_attach(GTK_GRID(table), network_port, 1, 1, 1, 1);

  label = g_object_new(GTK_TYPE_LABEL,
                       "use-underline", TRUE,
                       "mnemonic-widget", network_port,
                       "label", _("_Port:"),
                       "xalign", 0.0,
                       "yalign", 0.5,
                       NULL);
  network_port_label = label;
  gtk_grid_attach(GTK_GRID(table), label, 0, 1, 1, 1);

  network_login = gtk_entry_new();
  gtk_widget_set_margin_top(network_login, 10);
  g_signal_connect(network_login, "activate",
      G_CALLBACK(connect_callback), NULL);
  gtk_grid_attach(GTK_GRID(table), network_login, 1, 3, 1, 1);

  label = g_object_new(GTK_TYPE_LABEL,
                       "use-underline", TRUE,
                       "mnemonic-widget", network_login,
                       "label", _("_Login:"),
                       "xalign", 0.0,
                       "yalign", 0.5,
                       NULL);
  gtk_widget_set_margin_top(label, 10);
  network_login_label = label;
  gtk_grid_attach(GTK_GRID(table), label, 0, 3, 1, 1);

  network_password = gtk_entry_new();
  g_signal_connect(network_password, "activate",
      G_CALLBACK(connect_callback), NULL);
  gtk_entry_set_visibility(GTK_ENTRY(network_password), FALSE);
  gtk_grid_attach(GTK_GRID(table), network_password, 1, 4, 1, 1);

  label = g_object_new(GTK_TYPE_LABEL,
                       "use-underline", TRUE,
                       "mnemonic-widget", network_password,
                       "label", _("Pass_word:"),
                       "xalign", 0.0,
                       "yalign", 0.5,
                       NULL);
  network_password_label = label;
  gtk_grid_attach(GTK_GRID(table), label, 0, 4, 1, 1);

  network_confirm_password = gtk_entry_new();
  g_signal_connect(network_confirm_password, "activate",
      G_CALLBACK(connect_callback), NULL);
  gtk_entry_set_visibility(GTK_ENTRY(network_confirm_password), FALSE);
  gtk_grid_attach(GTK_GRID(table), network_confirm_password, 1, 5, 1, 1);

  label = g_object_new(GTK_TYPE_LABEL,
                       "use-underline", TRUE,
                       "mnemonic-widget", network_confirm_password,
                       "label", _("Conf_irm Password:"),
                       "xalign", 0.0,
                       "yalign", 0.5,
                       NULL);
  network_confirm_password_label = label;
  gtk_grid_attach(GTK_GRID(table), label, 0, 5, 1, 1);

  /* Server player list. */
  store = gtk_list_store_new(4, G_TYPE_STRING,
                             G_TYPE_STRING,
                             G_TYPE_STRING,
                             G_TYPE_STRING);
  server_playerlist_store = store;

  factory = gtk_signal_list_item_factory_new();
  g_signal_connect(factory, "bind", G_CALLBACK(plr_factory_bind),
                   GINT_TO_POINTER(plr_row_name));
  g_signal_connect(factory, "setup", G_CALLBACK(plr_factory_setup),
                   nullptr);

  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  gtk_widget_set_hexpand(view, TRUE);
  add_treeview_column(view, _("Name"), G_TYPE_STRING, 0);
  add_treeview_column(view, _("Type"), G_TYPE_STRING, 1);
  add_treeview_column(view, _("Host"), G_TYPE_STRING, 2);
  add_treeview_column(view, _("Nation"), G_TYPE_STRING, 3);
  server_playerlist_view = view;

  sw = gtk_scrolled_window_new();
  gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(sw), TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), view);
  gtk_box_append(GTK_BOX(hbox), sw);

  bbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  gtk_widget_set_margin_bottom(bbox, 2);
  gtk_widget_set_margin_end(bbox, 2);
  gtk_widget_set_margin_start(bbox, 2);
  gtk_widget_set_margin_top(bbox, 2);
  gtk_box_set_spacing(GTK_BOX(bbox), 12);
  gtk_box_append(GTK_BOX(sbox), bbox);

  button = gtk_button_new_from_icon_name("view-refresh");
  gtk_box_append(GTK_BOX(bbox), button);
  g_signal_connect(button, "clicked",
                   G_CALLBACK(update_network_lists), NULL);

  button = gtk_button_new_with_mnemonic(_("_Cancel"));
  gtk_box_append(GTK_BOX(bbox), button);
  g_signal_connect(button, "clicked",
                   G_CALLBACK(main_callback), NULL);

  button = gtk_button_new_with_mnemonic(_("C_onnect"));
  gtk_box_append(GTK_BOX(bbox), button);
  g_signal_connect(button, "clicked",
                   G_CALLBACK(connect_callback), NULL);

  return box;
}

/****************************************************************************
                                  START PAGE
****************************************************************************/
GtkWidget *start_message_area;

static GtkWidget *start_options_table;
static GtkWidget *observe_button, *ready_button, *nation_button;
static GtkTreeStore *connection_list_store;
static GtkTreeView *connection_list_view;
static GtkWidget *start_aifill_spin = NULL;
static GtkWidget *ai_lvl_combobox = NULL;


/* NB: Must match creation arguments in connection_list_store_new(). */
enum connection_list_columns {
  CL_COL_PLAYER_NUMBER = 0,
  CL_COL_USER_NAME,
  CL_COL_READY_STATE,
  CL_COL_PLAYER_NAME,
  CL_COL_FLAG,
  CL_COL_COLOR,
  CL_COL_NATION,
  CL_COL_TEAM,
  CL_COL_CONN_ID,
  CL_COL_STYLE,
  CL_COL_WEIGHT,
  CL_COL_COLLAPSED,

  CL_NUM_COLUMNS
};

/**********************************************************************//**
  Create a new tree store for connection list.
**************************************************************************/
static inline GtkTreeStore *connection_list_store_new(void)
{
  return gtk_tree_store_new(CL_NUM_COLUMNS,
                            G_TYPE_INT,         /* CL_COL_PLAYER_NUMBER */
                            G_TYPE_STRING,      /* CL_COL_USER_NAME */
                            G_TYPE_BOOLEAN,     /* CL_COL_READY_STATE */
                            G_TYPE_STRING,      /* CL_COL_PLAYER_NAME */
                            GDK_TYPE_PIXBUF,    /* CL_COL_FLAG */
                            GDK_TYPE_PIXBUF,    /* CL_COL_COLOR */
                            G_TYPE_STRING,      /* CL_COL_NATION */
                            G_TYPE_STRING,      /* CL_COL_TEAM */
                            G_TYPE_INT,         /* CL_COL_CONN_ID */
                            G_TYPE_INT,         /* CL_COL_STYLE */
                            G_TYPE_INT,         /* CL_COL_WEIGHT */
                            G_TYPE_BOOLEAN);    /* CL_COL_COLLAPSED */
}

/**********************************************************************//**
  Maybe toggle AI of the player if the client could take the player. This
  function shouldn't be used directly, see in client_take_player().
**************************************************************************/
static void client_aitoggle_player(void *data)
{
  struct player *pplayer = player_by_number(FC_PTR_TO_INT(data));

  if (NULL != pplayer
      && pplayer == client_player()
      && !is_human(pplayer)) {
    send_chat("/away");
  }
}

/**********************************************************************//**
  Send the /take command by chat and toggle AI if needed (after that).
**************************************************************************/
static void client_take_player(struct player *pplayer)
{
  int request_id = send_chat_printf("/take \"%s\"", player_name(pplayer));
  void *data = FC_INT_TO_PTR(player_number(pplayer));

  update_queue_connect_processing_finished(request_id,
                                           client_aitoggle_player, data);
}

/**********************************************************************//**
  Connect the object to the player and the connection.
**************************************************************************/
static void object_put(GObject *object, struct player *pplayer,
                       struct connection *pconn)
{
  /* Note that passing -1 to GINT_TO_POINTER() is buggy with some versions
   * of gcc. player_slot_count() is not a valid player number. 0 is not
   * a valid connection id (see comment in server/sernet.c:
   * makeup_connection_name()). */
  g_object_set_data(object, "player_id",
                    GINT_TO_POINTER(NULL != pplayer
                                    ? player_number(pplayer)
                                    : player_slot_count()));
  g_object_set_data(object, "connection_id",
                    GINT_TO_POINTER(NULL != pconn ? pconn->id : 0));
}

/**********************************************************************//**
  Extract the player and the connection set with object_put(). Returns TRUE
  if at least one of them isn't NULL.
**************************************************************************/
static bool object_extract(GObject *object, struct player **ppplayer,
                           struct connection **ppconn)
{
  bool ret = FALSE;
  int id;

  if (NULL != ppplayer) {
    id = GPOINTER_TO_INT(g_object_get_data(object, "player_id"));
    *ppplayer = player_by_number(id);
    if (NULL != *ppplayer) {
      ret = TRUE;
    }
  }
  if (NULL != ppconn) {
    id = GPOINTER_TO_INT(g_object_get_data(object, "connection_id"));
    *ppconn = conn_by_number(id);
    if (NULL != *ppconn) {
      ret = TRUE;
    }
  }

  return ret;
}

/**********************************************************************//**
  Request the game options dialog.
**************************************************************************/
static void game_options_callback(GtkWidget *w, gpointer data)
{
  option_dialog_popup(_("Game Settings"), server_optset);
}

/**********************************************************************//**
  AI skill setting callback.
**************************************************************************/
static void ai_skill_callback(GtkWidget *w, gpointer data)
{
  enum ai_level *levels = (enum ai_level *)data;
  const char *name;
  int i;

  i = gtk_combo_box_get_active(GTK_COMBO_BOX(w));

  if (i != -1) {
    enum ai_level level = levels[i];

    /* Suppress changes provoked by server rather than local user */
    if (server_ai_level() != level) {
      name = ai_level_cmd(level);
      send_chat_printf("/%s", name);
    }
  }
}

/* HACK: sometimes when creating the ruleset combo the value is set without
 * the user's control. In this case we don't want to do a /read. */
static bool no_ruleset_callback = FALSE;

/**********************************************************************//**
  Ruleset name has been given
**************************************************************************/
static void ruleset_selected(const char *name)
{
  if (name && name[0] != '\0' && !no_ruleset_callback) {
    set_ruleset(name);
  }
}

/**********************************************************************//**
  Ruleset selection callback. Note that this gets also called when ever
  user types to entry box. In that case we don't want to set_ruleset()
  after each letter.
**************************************************************************/
static void ruleset_entry_changed(GtkWidget *w, gpointer data)
{
  const char *name = NULL;

  name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(ruleset_combo));

  if (name != NULL) {
    ruleset_selected(name);
  }
}

/**********************************************************************//**
  User changed AI fill setting.
**************************************************************************/
static bool send_new_aifill_to_server = TRUE;
static void ai_fill_changed_by_user(GtkWidget *w, gpointer data)
{
  if (send_new_aifill_to_server) {
    option_int_set(optset_option_by_name(server_optset, "aifill"),
                   gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w)));
  }
}

/**********************************************************************//**
  Server changed AI fill setting.
**************************************************************************/
void ai_fill_changed_by_server(int aifill)
{
  if (start_aifill_spin) {
    bool old = send_new_aifill_to_server;
    /* Suppress callback from this change to avoid a loop. */
    send_new_aifill_to_server = FALSE;
    /* HACK: this GUI control doesn't have quite the same semantics as the
     * server 'aifill' option, in that it claims to represent the minimum
     * number of players _including humans_. The GUI control has a minimum
     * value of 1, so aifill == 0 will not be represented correctly.
     * But there's generally exactly one human player because the control
     * only shows up for a locally spawned server, so we more or less
     * get away with this. */
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(start_aifill_spin), aifill);
    send_new_aifill_to_server = old;
  }
}

/**********************************************************************//**
  Update the start page.
**************************************************************************/
void update_start_page(void)
{
  conn_list_dialog_update();
}

/**********************************************************************//**
  Close connection menu.
**************************************************************************/
static void close_conn_menu_popover(void)
{
  if (conn_popover != NULL){
    gtk_widget_unparent(conn_popover);
    g_object_unref(conn_popover);

    conn_popover = NULL;
  }
}

/**********************************************************************//**
  Callback for when a team is chosen from the conn menu.
**************************************************************************/
static void conn_menu_team_chosen(GSimpleAction *action, GVariant *parameter,
                                  gpointer data)
{
  struct player *pplayer;
  struct team_slot *tslot = g_object_get_data(G_OBJECT(action), "slot");
  GMenu *menu = data;

  if (object_extract(G_OBJECT(menu), &pplayer, NULL)
      && NULL != tslot
      && team_slot_index(tslot) != team_number(pplayer->team)) {
    send_chat_printf("/team \"%s\" \"%s\"",
                     player_name(pplayer),
                     team_slot_rule_name(tslot));
  }

  close_conn_menu_popover();
}

/**********************************************************************//**
  Callback for when the "ready" entry is chosen from the conn menu.
**************************************************************************/
static void conn_menu_ready_chosen(GSimpleAction *action, GVariant *parameter,
                                   gpointer data)
{
  struct player *pplayer;
  GMenu *menu = data;

  if (object_extract(G_OBJECT(menu), &pplayer, NULL)) {
    dsend_packet_player_ready(&client.conn,
                              player_number(pplayer), !pplayer->is_ready);
  }

  close_conn_menu_popover();
}

/**********************************************************************//**
  Callback for when the pick-nation entry is chosen from the conn menu.
**************************************************************************/
static void conn_menu_nation_chosen(GSimpleAction *action, GVariant *parameter,
                                    gpointer data)
{
  struct player *pplayer;
  GMenu *menu = data;

  if (object_extract(G_OBJECT(menu), &pplayer, NULL)) {
    popup_races_dialog(pplayer);
  }

  close_conn_menu_popover();
}

/**********************************************************************//**
  Miscellaneous callback for the conn menu that allows an arbitrary command
  (/observe, /remove, /hard, etc.) to be run on the player.
**************************************************************************/
static void conn_menu_player_command(GSimpleAction *action, GVariant *parameter,
                                     gpointer data)
{
  struct player *pplayer;
  GMenu *menu = data;

  if (object_extract(G_OBJECT(menu), &pplayer, NULL)) {
    send_chat_printf("/%s \"%s\"",
                     (char *) g_object_get_data(G_OBJECT(action), "command"),
                     player_name(pplayer));
  }

  close_conn_menu_popover();
}

/**********************************************************************//**
  Take command in the conn menu.
**************************************************************************/
static void conn_menu_player_take(GSimpleAction *action, GVariant *parameter,
                                  gpointer data)
{
  struct player *pplayer;
  GMenu *menu = data;

  if (object_extract(G_OBJECT(menu), &pplayer, NULL)) {
    client_take_player(pplayer);
  }

  close_conn_menu_popover();
}

/**********************************************************************//**
  Miscellaneous callback for the conn menu that allows an arbitrary command
  (/cmdlevel, /cut, etc.) to be run on the connection.
**************************************************************************/
static void conn_menu_connection_command(GSimpleAction *action,
                                         GVariant *parameter, gpointer data)
{
  struct connection *pconn;
  GMenu *menu = data;

  if (object_extract(G_OBJECT(menu), NULL, &pconn)) {
    send_chat_printf(SERVER_COMMAND_PREFIX_STR "%s \"%s\"",
                     (char *) g_object_get_data(G_OBJECT(action), "command"),
                     pconn->username);
  }

  close_conn_menu_popover();
}

/**********************************************************************//**
  Show details about a user in the Connected Users dialog in a popup.
**************************************************************************/
static void show_conn_popup(struct player *pplayer, struct connection *pconn)
{
  GtkWidget *popup;
  char buf[4096] = "";

  if (pconn) {
    cat_snprintf(buf, sizeof(buf), _("Connection name: %s"),
                 pconn->username);
  } else {
    cat_snprintf(buf, sizeof(buf), _("Player name: %s"),
                 player_name(pplayer));
  }
  cat_snprintf(buf, sizeof(buf), "\n");
  if (pconn) {
    cat_snprintf(buf, sizeof(buf), _("Host: %s"), pconn->addr);
  }
  cat_snprintf(buf, sizeof(buf), "\n");

  /* Show popup. */
  popup = gtk_message_dialog_new(NULL, 0,
                                 GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
                                 "%s", buf);
  gtk_window_set_title(GTK_WINDOW(popup), _("Player/conn info"));
  setup_dialog(popup, toplevel);
  g_signal_connect(popup, "response", G_CALLBACK(gtk_window_destroy), NULL);
  gtk_window_present(GTK_WINDOW(popup));
}

/**********************************************************************//**
  Callback for when the "info" entry is chosen from the conn menu.
**************************************************************************/
static void conn_menu_info_chosen(GSimpleAction *action, GVariant *parameter,
                                  gpointer data)
{
  struct player *pplayer;
  struct connection *pconn;
  GMenu *menu = data;

  if (object_extract(G_OBJECT(menu), &pplayer, &pconn)) {
    show_conn_popup(pplayer, pconn);
  }

  close_conn_menu_popover();
}

/**********************************************************************//**
  Called when you click on a player; this function pops up a menu
  to allow changing the team.
**************************************************************************/
static GtkWidget *create_conn_menu(struct player *pplayer,
                                   struct connection *pconn)
{
  GMenu *menu;
  gchar *buf;
  GSimpleAction *act;
  GActionGroup *group;

  group = G_ACTION_GROUP(g_simple_action_group_new());

  menu = g_menu_new();

  object_put(G_OBJECT(menu), pplayer, pconn);

  buf = g_strdup_printf(_("%s info"),
                        pconn ? pconn->username : player_name(pplayer));

  act = g_simple_action_new("info", NULL);
  g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
  g_signal_connect(act, "activate", G_CALLBACK(conn_menu_info_chosen), menu);
  menu_item_append_unref(menu, g_menu_item_new(buf, "win.info"));

  if (NULL != pplayer) {
    act = g_simple_action_new("toggle_ready", NULL);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
    g_signal_connect(act, "activate", G_CALLBACK(conn_menu_ready_chosen), menu);
    g_simple_action_set_enabled(G_SIMPLE_ACTION(act), is_human(pplayer));
    menu_item_append_unref(menu, g_menu_item_new(_("Toggle player ready"),
                                                 "win.toggle_ready"));

    act = g_simple_action_new("pick_nation", NULL);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
    g_signal_connect(act, "activate", G_CALLBACK(conn_menu_nation_chosen), menu);
    g_simple_action_set_enabled(G_SIMPLE_ACTION(act),
                                can_conn_edit_players_nation(&client.conn,
                                                             pplayer));
    menu_item_append_unref(menu, g_menu_item_new(_("Pick nation"), "win.pick_nation"));

    act = g_simple_action_new("observe", NULL);
    g_object_set_data_full(G_OBJECT(act), "command", g_strdup("observe"),
                           (GDestroyNotify) g_free);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
    g_signal_connect(act, "activate", G_CALLBACK(conn_menu_player_command), menu);
    menu_item_append_unref(menu, g_menu_item_new(_("Observe this player"),
                                                 "win.observe"));

    act = g_simple_action_new("take_plr", NULL);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
    g_signal_connect(act, "activate", G_CALLBACK(conn_menu_player_take), menu);
    menu_item_append_unref(menu, g_menu_item_new(_("Take this player"),
                                                 "win.take_plr"));
  }

  if (ALLOW_CTRL <= client.conn.access_level && NULL != pconn
      && pconn->id != client.conn.id) {
    act = g_simple_action_new("cut_conn", NULL);
    g_object_set_data_full(G_OBJECT(act), "command", g_strdup("cut"),
                           (GDestroyNotify) g_free);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
    g_signal_connect(act, "activate", G_CALLBACK(conn_menu_connection_command), menu);
    menu_item_append_unref(menu, g_menu_item_new(_("Cut connection"), "win.cut_conn"));
  }

  if (ALLOW_CTRL <= client.conn.access_level && NULL != pplayer) {
    act = g_simple_action_new("aitoggle", NULL);
    g_object_set_data_full(G_OBJECT(act), "command", g_strdup("aitoggle"),
                           (GDestroyNotify) g_free);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
    g_signal_connect(act, "activate", G_CALLBACK(conn_menu_connection_command), menu);
    menu_item_append_unref(menu, g_menu_item_new(_("Aitoggle player"), "win.aitoggle"));

    if (pplayer != client.conn.playing && game.info.is_new_game) {
      act = g_simple_action_new("remove", NULL);
      g_object_set_data_full(G_OBJECT(act), "command", g_strdup("remove"),
                             (GDestroyNotify) g_free);
      g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
      g_signal_connect(act, "activate", G_CALLBACK(conn_menu_connection_command), menu);
      menu_item_append_unref(menu, g_menu_item_new(_("Remove player"), "win.remove"));
    }
  }

  if (ALLOW_ADMIN <= client.conn.access_level && NULL != pconn
      && pconn->id != client.conn.id) {
    enum cmdlevel level;

    /* No item for hack access; that would be a serious security hole. */
    for (level = cmdlevel_min(); level < client.conn.access_level; level++) {
      char actbuf[128];

      buf = g_strdup_printf(_("Give %s access"), cmdlevel_name(level));
      fc_snprintf(actbuf, sizeof(actbuf), "cmdlevel_%d", level);

      act = g_simple_action_new(actbuf, NULL);
      g_object_set_data_full(G_OBJECT(act), "command",
                             g_strdup_printf("cmdlevel %s",
                                             cmdlevel_name(level)),
                             (GDestroyNotify) g_free);
      g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
      g_signal_connect(act, "activate", G_CALLBACK(conn_menu_connection_command), menu);
      fc_snprintf(actbuf, sizeof(actbuf), "win.cmdlevel_%d", level);
      menu_item_append_unref(menu, g_menu_item_new(buf, actbuf));
      g_free(buf);
    }
  }

  if (ALLOW_CTRL <= client.conn.access_level
      && NULL != pplayer && is_ai(pplayer)) {
    enum ai_level level;

    for (level = 0; level < AI_LEVEL_COUNT; level++) {
      if (is_settable_ai_level(level)) {
        char actbuf[128];

        buf = g_strdup_printf(_("Difficulty: %s"), ai_level_translated_name(level));
        fc_snprintf(actbuf, sizeof(actbuf), "ailevel_%d", level);

        act = g_simple_action_new(actbuf, NULL);
        g_object_set_data_full(G_OBJECT(act), "command",
                               g_strdup(ai_level_cmd(level)),
                               (GDestroyNotify) g_free);
        g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
        g_signal_connect(act, "activate", G_CALLBACK(conn_menu_player_command), menu);
        fc_snprintf(actbuf, sizeof(actbuf), "win.ailevel_%d", level);
        menu_item_append_unref(menu, g_menu_item_new(buf, actbuf));
        g_free(buf);
      }
    }
  }

  if (pplayer != NULL /* && game.info.is_new_game */) {
    const int count = pplayer->team
      ? player_list_size(team_members(pplayer->team)) : 0;
    bool need_empty_team = (count != 1);

    /* Can't use team_iterate here since it skips empty teams. */
    team_slots_iterate(tslot) {
      char actbuf[128];
      int id;

      if (!team_slot_is_used(tslot)) {
        if (!need_empty_team) {
          continue;
        }
        need_empty_team = FALSE;
      }

      id = team_number(team_slot_get_team(tslot));

      /* TRANS: e.g., "Put on Team 5" */
      buf = g_strdup_printf(_("Put on %s"),
                            team_slot_name_translation(tslot));
      fc_snprintf(actbuf, sizeof(actbuf), "team_%d", id);

      act = g_simple_action_new(actbuf, NULL);
      g_object_set_data(G_OBJECT(act), "slot", tslot);
      g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
      g_signal_connect(act, "activate", G_CALLBACK(conn_menu_team_chosen), menu);
      fc_snprintf(actbuf, sizeof(actbuf), "win.team_%d", id);
      menu_item_append_unref(menu, g_menu_item_new(buf, actbuf));
      g_free(buf);
    } team_slots_iterate_end;
  }

  close_conn_menu_popover();
  conn_popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
  g_object_ref(conn_popover);
  gtk_widget_insert_action_group(conn_popover, "win", group);

  return conn_popover;
}

/**********************************************************************//**
  Unselect a tree path.
**************************************************************************/
static gboolean delayed_unselect_path(gpointer data)
{
  if (NULL != connection_list_view) {
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(connection_list_view);
    GtkTreePath *path = data;

    gtk_tree_selection_unselect_path(selection, path);
    gtk_tree_path_free(path);
  }
  return FALSE;
}

/**********************************************************************//**
  Called on a left button click on the pregame player list.
**************************************************************************/
static gboolean connect_list_left_button(GtkGestureClick *gesture,
                                         int n_press,
                                         double x, double y, gpointer data)
{
  GtkEventController *controller = GTK_EVENT_CONTROLLER(gesture);
  GtkTreeView *tree = GTK_TREE_VIEW(gtk_event_controller_get_widget(controller));
  GtkTreePath *path = NULL;
  GtkTreeSelection *selection = gtk_tree_view_get_selection(tree);

  if (!gtk_tree_view_get_path_at_pos(tree,
                                     x, y,
                                     &path, NULL, NULL, NULL)) {
    return FALSE;
  }

  if (gtk_tree_selection_path_is_selected(selection, path)) {
    /* Need to delay to avoid problem with the expander. */
    g_idle_add(delayed_unselect_path, path);
    return FALSE;     /* Return now, don't free the path. */
  }

  gtk_tree_path_free(path);

  return FALSE;
}

/**********************************************************************//**
  Called on a right button click on the pregame player list.
**************************************************************************/
static gboolean connect_list_right_button(GtkGestureClick *gesture,
                                          int n_press,
                                          double x, double y, gpointer data)
{
  GtkEventController *controller = GTK_EVENT_CONTROLLER(gesture);
  GtkWidget *tree = gtk_event_controller_get_widget(controller);
  GtkWidget *parent = data;
  GtkTreePath *path = NULL;
  GtkWidget *menu;
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GtkTreeIter iter;
  int player_no, conn_id;
  struct player *pplayer;
  struct connection *pconn;
  int bx, by;
  GdkRectangle rect = { .x = x, .y = y, .width = 1, .height = 1};

  gtk_tree_view_convert_widget_to_bin_window_coords(GTK_TREE_VIEW(tree), x, y, &bx, &by);

  if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(tree), bx, by,
                                     &path, NULL, NULL, NULL)) {
    return FALSE;
  }

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));

  if (!gtk_tree_selection_path_is_selected(selection, path)) {
    gtk_tree_selection_select_path(selection, path);
  }
  gtk_tree_model_get_iter(model, &iter, path);

  gtk_tree_model_get(model, &iter, CL_COL_PLAYER_NUMBER, &player_no, -1);
  pplayer = player_by_number(player_no);

  gtk_tree_model_get(model, &iter, CL_COL_CONN_ID, &conn_id, -1);
  pconn = conn_by_number(conn_id);

  menu = create_conn_menu(pplayer, pconn);
  gtk_widget_set_parent(menu, parent); /* Gtk bug prevents tree view parenting */
  gtk_popover_set_pointing_to(GTK_POPOVER(menu), &rect);

  gtk_popover_popup(GTK_POPOVER(menu));

  gtk_tree_path_free(path);

  return TRUE;
}

/**********************************************************************//**
  Mark a row as collapsed or expanded.
**************************************************************************/
static void connection_list_row_callback(GtkTreeView *tree_view,
                                         GtkTreeIter *iter,
                                         GtkTreePath *path,
                                         gpointer data)
{
  GtkTreeStore *store = GTK_TREE_STORE(gtk_tree_view_get_model(tree_view));

  gtk_tree_store_set(store, iter,
                     CL_COL_COLLAPSED, GPOINTER_TO_INT(data), -1);
}

/**********************************************************************//**
  Returns TRUE if a row is selected in the connection/player list. Fills
  the not null data.
**************************************************************************/
static bool conn_list_selection(struct player **ppplayer,
                                struct connection **ppconn)
{
  if (NULL != connection_list_view) {
    GtkTreeIter iter;
    GtkTreeModel *model;
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(connection_list_view);

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
      int id;

      if (NULL != ppplayer) {
        gtk_tree_model_get(model, &iter, CL_COL_PLAYER_NUMBER, &id, -1);
        *ppplayer = player_by_number(id);
      }
      if (NULL != ppconn) {
        gtk_tree_model_get(model, &iter, CL_COL_CONN_ID, &id, -1);
        *ppconn = conn_by_number(id);
      }
      return TRUE;
    }
  }

  if (NULL != ppplayer) {
    *ppplayer = NULL;
  }
  if (NULL != ppconn) {
    *ppconn = NULL;
  }
  return FALSE;
}

/**********************************************************************//**
  Selects connection's row in the connection/player list.
**************************************************************************/
static void conn_list_select_conn(struct connection *pconn)
{
  GtkTreeModel *model;
  GtkTreeIter parent, child, *iter = NULL;
  GtkTreeSelection *selection;
  gboolean valid;
  const int search_id = pconn->id;
  int id;

  if (NULL == connection_list_view) {
    return;
  }

  model = gtk_tree_view_get_model(connection_list_view);
  selection = gtk_tree_view_get_selection(connection_list_view);

  /* Main iteration. */
  valid = gtk_tree_model_get_iter_first(model, &parent);
  while (valid && NULL == iter) {
    gtk_tree_model_get(model, &parent, CL_COL_CONN_ID, &id, -1);
    if (search_id == id) {
      iter = &parent;
      break;
    }

    /* Node children iteration. */
    valid = gtk_tree_model_iter_children(model, &child, &parent);
    while (valid && NULL == iter) {
      gtk_tree_model_get(model, &child, CL_COL_CONN_ID, &id, -1);
      if (search_id == id) {
        iter = &child;
        break;
      }
      valid = gtk_tree_model_iter_next(model, &child);
    }

    valid = gtk_tree_model_iter_next(model, &parent);
  }

  /* Select iterator. */
  if (NULL != iter) {
    gtk_tree_selection_select_iter(selection, iter);
  } else {
    log_error("%s(): connection %s not found.",
              __FUNCTION__, conn_description(pconn));
  }
}

/**********************************************************************//**
  'ready_button' clicked callback.
**************************************************************************/
static void ready_button_callback(GtkWidget *w, gpointer data)
{
  if (can_client_control()) {
    dsend_packet_player_ready(&client.conn,
                              player_number(client_player()),
                              !client_player()->is_ready);
  } else {
    dsend_packet_player_ready(&client.conn, 0, TRUE);
  }
}

/**********************************************************************//**
  'nation_button' clicked callback.
**************************************************************************/
static void nation_button_callback(GtkWidget *w, gpointer data)
{
  struct player *selected_plr;
  bool row_selected = conn_list_selection(&selected_plr, NULL);

  if (row_selected && NULL != selected_plr) {
    /* "Take <player_name>" */
    client_take_player(selected_plr);
  } else if (can_client_control()) {
    /* "Pick Nation" */
    popup_races_dialog(client_player());
  } else {
    /* "Take a Player" */
    send_chat("/take -");
  }
}

/**********************************************************************//**
  'observe_button' clicked callback.
**************************************************************************/
static void observe_button_callback(GtkWidget *w, gpointer data)
{
  struct player *selected_plr;
  bool row_selected = conn_list_selection(&selected_plr, NULL);

  if (row_selected && NULL != selected_plr) {
    /* "Observe <player_name>" */
    send_chat_printf("/observe \"%s\"", player_name(selected_plr));
  } else if (!client_is_global_observer()) {
    /* "Observe" */
    send_chat("/observe");
  } else {
    /* "Do not observe" */
    send_chat("/detach");
  }
}

/**********************************************************************//**
  Update the buttons of the start page.
**************************************************************************/
static void update_start_page_buttons(void)
{
  char buf[2 * MAX_LEN_NAME];
  const char *text;
  struct player *selected_plr;
  bool row_selected = conn_list_selection(&selected_plr, NULL);
  bool sensitive;

  /*** Ready button. ***/
  if (can_client_control()) {
    sensitive = client_player()->is_alive;
    if (client_player()->is_ready) {
      text = _("Not _ready");
    } else {
      int num_unready = 0;

      players_iterate_alive(pplayer) {
        if (is_human(pplayer) && !pplayer->is_ready) {
          num_unready++;
        }
      } players_iterate_alive_end;

      if (num_unready > 1) {
        text = _("_Ready");
      } else {
        /* We are the last unready player so clicking here will
         * immediately start the game. */
        text = _("_Start");
      }
    }
  } else {
    text = _("_Start");
    if (can_client_access_hack()) {
      sensitive = TRUE;
      players_iterate_alive(plr) {
        if (is_human(plr)) {
          /* There's human controlled player(s) in game, so it's their
           * job to start the game. */
          sensitive = FALSE;
          break;
        }
      } players_iterate_alive_end;
    } else {
      sensitive = FALSE;
    }
  }
  gtk_stockbutton_set_label(ready_button, text);
  gtk_widget_set_sensitive(ready_button, sensitive);

  /*** Nation button. ***/
  if (row_selected && NULL != selected_plr) {
    fc_snprintf(buf, sizeof(buf), _("_Take %s"), player_name(selected_plr));
    text = buf;
    sensitive = (client_is_observer() || selected_plr != client_player());
  } else if (can_client_control()) {
    text = _("Pick _Nation");
    sensitive = game.info.is_new_game;
  } else {
    text = _("_Take a Player");
    sensitive = game.info.is_new_game;
  }
  gtk_stockbutton_set_label(nation_button, text);
  gtk_widget_set_sensitive(nation_button, sensitive);

  /*** Observe button. ***/
  if (row_selected && NULL != selected_plr) {
    fc_snprintf(buf, sizeof(buf), _("_Observe %s"),
                player_name(selected_plr));
    text = buf;
    sensitive = (!client_is_observer() || selected_plr != client_player());
  } else if (!client_is_global_observer()) {
    text = _("_Observe");
    sensitive = TRUE;
  } else {
    text = _("Do not _observe");
    sensitive = TRUE;
  }
  gtk_stockbutton_set_label(observe_button, text);
  gtk_widget_set_sensitive(observe_button, sensitive);
}

/**********************************************************************//**
  Search a player iterator in the model. Begin the iteration at 'start' or
  at the start of the model if 'start' is set to NULL.
**************************************************************************/
static bool model_get_player_iter(GtkTreeModel *model,
                                  GtkTreeIter *iter,
                                  GtkTreeIter *start,
                                  const struct player *pplayer)
{
  const int search_id = player_number(pplayer);
  int id;

  if (NULL != start) {
    *iter = *start;
    if (!gtk_tree_model_iter_next(model, iter)) {
      return FALSE;
    }
  } else if (!gtk_tree_model_get_iter_first(model, iter)) {
    return FALSE;
  }

  do {
    gtk_tree_model_get(model, iter, CL_COL_PLAYER_NUMBER, &id, -1);
    if (id == search_id) {
      return TRUE;
    }
  } while (gtk_tree_model_iter_next(model, iter));

  return FALSE;
}

/**********************************************************************//**
  Search a connection iterator in the model. Begin the iteration at 'start'
  or at the start of the model if 'start' is set to NULL.
**************************************************************************/
static bool model_get_conn_iter(GtkTreeModel *model, GtkTreeIter *iter,
                                GtkTreeIter *parent, GtkTreeIter *start,
                                const struct connection *pconn)
{
  const int search_id = pconn->id;
  int id;

  if (NULL != start) {
    *iter = *start;
    if (!gtk_tree_model_iter_next(model, iter)) {
      return FALSE;
    }
  } else if (!gtk_tree_model_iter_children(model, iter, parent)) {
    return FALSE;
  }

  do {
    gtk_tree_model_get(model, iter, CL_COL_CONN_ID, &id, -1);
    if (id == search_id) {
      return TRUE;
    }
  } while (gtk_tree_model_iter_next(model, iter));

  return FALSE;
}

/**********************************************************************//**
  Update the connected users list at pregame state.
**************************************************************************/
void real_conn_list_dialog_update(void *unused)
{
  if (client_state() == C_S_PREPARING
      && get_client_page() == PAGE_START
      && connection_list_store != NULL) {
    GtkTreeStore *store = connection_list_store;
    GtkTreeModel *model = GTK_TREE_MODEL(store);
    GtkTreePath *path;
    GtkTreeIter child, prev_child, *pprev_child;
    GtkTreeIter parent, prev_parent, *pprev_parent = NULL;
    GdkPixbuf *flag, *color;
    gboolean collapsed;
    struct player *pselected_player;
    struct connection *pselected_conn;
    bool is_ready;
    const char *nation, *plr_name, *team;
    char name[MAX_LEN_NAME + 8];
    enum cmdlevel access_level;
    int conn_id;

    /* Refresh the AI skill level control */
    if (ai_lvl_combobox) {
      enum ai_level new_level = server_ai_level(), level;
      int i = 0;

      for (level = 0; level < AI_LEVEL_COUNT; level++) {
        if (is_settable_ai_level(level)) {
          if (level == new_level) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(ai_lvl_combobox), i);
            break;
          }
          i++;
        }
      }
      if (level == AI_LEVEL_COUNT) {
        /* Probably ai_level_invalid() */
        gtk_combo_box_set_active(GTK_COMBO_BOX(ai_lvl_combobox), -1);
      }
    }

    /* Save the selected connection. */
    (void) conn_list_selection(&pselected_player, &pselected_conn);

    /* Insert players into the connection list. */
    players_iterate(pplayer) {
      if (!player_has_flag(pplayer, PLRF_SCENARIO_RESERVED)) {
        conn_id = -1;
        access_level = ALLOW_NONE;
        flag = pplayer->nation ? get_flag(pplayer->nation) : NULL;
        color = create_player_icon(pplayer);

        conn_list_iterate(pplayer->connections, pconn) {
          if (pconn->playing == pplayer && !pconn->observer) {
            conn_id = pconn->id;
            access_level = pconn->access_level;
            break;
          }
        } conn_list_iterate_end;

        if (is_ai(pplayer) && !pplayer->was_created
            && !pplayer->is_connected) {
          /* TRANS: "<Novice AI>" */
          fc_snprintf(name, sizeof(name), _("<%s AI>"),
                      ai_level_translated_name(pplayer->ai_common.skill_level));
        } else {
          sz_strlcpy(name, pplayer->username);
          if (access_level > ALLOW_BASIC) {
            sz_strlcat(name, "*");
          }
        }

        is_ready = !is_human(pplayer) ? TRUE : pplayer->is_ready;

        if (pplayer->nation == NO_NATION_SELECTED) {
          nation = _("Random");
          if (pplayer->was_created) {
            plr_name = player_name(pplayer);
          } else {
            plr_name = "";
          }
        } else {
          nation = nation_adjective_for_player(pplayer);
          plr_name = player_name(pplayer);
        }

        team = pplayer->team ? team_name_translation(pplayer->team) : "";

        if (model_get_player_iter(model, &parent, pprev_parent, pplayer)) {
          gtk_tree_store_move_after(store, &parent, pprev_parent);
        } else {
          gtk_tree_store_insert_after(store, &parent, NULL, pprev_parent);
        }

        gtk_tree_store_set(store, &parent,
                           CL_COL_PLAYER_NUMBER, player_number(pplayer),
                           CL_COL_USER_NAME, name,
                           CL_COL_READY_STATE, is_ready,
                           CL_COL_PLAYER_NAME, plr_name,
                           CL_COL_FLAG, flag,
                           CL_COL_COLOR, color,
                           CL_COL_NATION, nation,
                           CL_COL_TEAM, team,
                           CL_COL_CONN_ID, conn_id,
                           CL_COL_STYLE, PANGO_STYLE_NORMAL,
                           CL_COL_WEIGHT, PANGO_WEIGHT_BOLD,
                           -1);

        /* Insert observers of this player as child nodes. */
        pprev_child = NULL;
        conn_list_iterate(pplayer->connections, pconn) {
          if (pconn->id == conn_id) {
            continue;
          }
          if (model_get_conn_iter(model, &child, &parent,
                                  pprev_child, pconn)) {
            gtk_tree_store_move_after(store, &child, pprev_child);
          } else {
            gtk_tree_store_insert_after(store, &child, &parent, pprev_child);
          }

          gtk_tree_store_set(store, &child,
                             CL_COL_PLAYER_NUMBER, -1,
                             CL_COL_USER_NAME, pconn->username,
                             CL_COL_TEAM, _("Observer"),
                             CL_COL_CONN_ID, pconn->id,
                             CL_COL_STYLE, PANGO_STYLE_NORMAL,
                             CL_COL_WEIGHT, PANGO_WEIGHT_NORMAL,
                             -1);

          prev_child = child;
          pprev_child = &prev_child;
        } conn_list_iterate_end;

        /* Expand node? */
        if (NULL != pprev_child) {
          gtk_tree_model_get(model, &parent, CL_COL_COLLAPSED, &collapsed, -1);
          if (!collapsed) {
            path = gtk_tree_model_get_path(model, &parent);
            gtk_tree_view_expand_row(GTK_TREE_VIEW(connection_list_view),
                                     path, FALSE);
            gtk_tree_path_free(path);
          }
        }

        /* Remove trailing rows. */
        if (NULL != pprev_child) {
          child = prev_child;
          if (gtk_tree_model_iter_next(model, &child)) {
            while (gtk_tree_store_remove(store, &child)) {
              /* Do nothing more. */
            }
          }
        } else if (gtk_tree_model_iter_children(model, &child, &parent)) {
          while (gtk_tree_store_remove(store, &child)) {
            /* Do nothing more. */
          }
        }

        prev_parent = parent;
        pprev_parent = &prev_parent;
        if (flag) {
          g_object_unref(flag);
        }
        if (color) {
          g_object_unref(color);
        }
      }
    } players_iterate_end;

    /* Finally, insert global observers... */
    conn_list_iterate(game.est_connections, pconn) {
      if (NULL != pconn->playing || !pconn->observer) {
        continue;
      }

      if (model_get_conn_iter(model, &parent, NULL, pprev_parent, pconn)) {
        gtk_tree_store_move_after(store, &parent, pprev_parent);
      } else {
        gtk_tree_store_insert_after(store, &parent, NULL, pprev_parent);
      }

      gtk_tree_store_set(store, &parent,
                         CL_COL_PLAYER_NUMBER, -1,
                         CL_COL_USER_NAME, pconn->username,
                         CL_COL_TEAM, _("Observer"),
                         CL_COL_CONN_ID, pconn->id,
                         CL_COL_STYLE, PANGO_STYLE_NORMAL,
                         CL_COL_WEIGHT, PANGO_WEIGHT_NORMAL,
                         -1);

      prev_parent = parent;
      pprev_parent = &prev_parent;
    } conn_list_iterate_end;

    /* ...and detached connections. */
    conn_list_iterate(game.est_connections, pconn) {
      if (NULL != pconn->playing || pconn->observer) {
        continue;
      }

      if (model_get_conn_iter(model, &parent, NULL, pprev_parent, pconn)) {
        gtk_tree_store_move_after(store, &parent, pprev_parent);
      } else {
        gtk_tree_store_insert_after(store, &parent, NULL, pprev_parent);
      }

      gtk_tree_store_set(store, &parent,
                         CL_COL_PLAYER_NUMBER, -1,
                         CL_COL_USER_NAME, pconn->username,
                         CL_COL_TEAM, _("Detached"),
                         CL_COL_CONN_ID, pconn->id,
                         CL_COL_STYLE, PANGO_STYLE_ITALIC,
                         CL_COL_WEIGHT, PANGO_WEIGHT_NORMAL,
                         -1);

      prev_parent = parent;
      pprev_parent = &prev_parent;
    } conn_list_iterate_end;

    /* Remove trailing rows. */
    if (NULL != pprev_parent) {
      parent = prev_parent;
      if (gtk_tree_model_iter_next(model, &parent)) {
        while (gtk_tree_store_remove(store, &parent)) {
          /* Do nothing more. */
        }
      }
    } else {
      gtk_tree_store_clear(store);
    }

    /* If we were selecting a single connection, let's try to reselect it. */
    if (NULL == pselected_player && NULL != pselected_conn) {
      conn_list_select_conn(pselected_conn);
    }
  }

  update_start_page_buttons();
}

/**********************************************************************//**
  Helper function for adding columns to a tree view. If 'key' is not NULL
  then the added column is added to the object data of the treeview using
  g_object_set_data() under 'key'.
**************************************************************************/
static void add_tree_col(GtkWidget *treeview, GType gtype,
                         const char *title, int colnum, const char *key)
{
  GtkCellRenderer *rend;
  GtkTreeViewColumn *col;

  if (gtype == G_TYPE_BOOLEAN) {
    rend = gtk_cell_renderer_toggle_new();
    col = gtk_tree_view_column_new_with_attributes(title, rend,
                                                   "active", colnum, NULL);
  } else if (gtype == GDK_TYPE_PIXBUF) {
    rend = gtk_cell_renderer_pixbuf_new();
    col = gtk_tree_view_column_new_with_attributes(title, rend,
                                                   "pixbuf", colnum, NULL);
  } else {
    rend = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes(title, rend,
                                                   "text", colnum,
                                                   "style", CL_COL_STYLE,
                                                   "weight", CL_COL_WEIGHT,
                                                   NULL);
  }

  gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
  gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), col);

  if (key != NULL) {
    g_object_set_data(G_OBJECT(treeview), key, col);
  }
}

/**********************************************************************//**
  Create start page.
**************************************************************************/
GtkWidget *create_start_page(void)
{
  GtkWidget *box, *sbox, *table, *vgrid;
  GtkWidget *view, *sw, *text, *toolkit_view, *button, *spin;
  GtkWidget *label;
  GtkTreeSelection *selection;
  enum ai_level level;
  /* There's less than AI_LEVEL_COUNT entries as not all levels have
     entries (that's the whole point of this array: index != value),
     but this is set safely to the max */
  static enum ai_level levels[AI_LEVEL_COUNT];
  int i = 0;
  int box_row = 0;
  int sbox_col = 0;
  int grid_row = 0;
  GtkGesture *gesture;
  GtkEventController *controller;

  box = gtk_grid_new();
  gtk_orientable_set_orientation(GTK_ORIENTABLE(box),
                                 GTK_ORIENTATION_VERTICAL);
  gtk_grid_set_row_spacing(GTK_GRID(box), 8);
  gtk_widget_set_margin_start(box, 4);
  gtk_widget_set_margin_end(box, 4);
  gtk_widget_set_margin_top(box, 4);
  gtk_widget_set_margin_bottom(box, 4);

  sbox = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(sbox), 12);
  gtk_grid_attach(GTK_GRID(box), sbox, 0, box_row++, 1, 1);

  vgrid = gtk_grid_new();
  gtk_widget_set_margin_bottom(vgrid, 12);
  gtk_widget_set_margin_end(vgrid, 12);
  gtk_widget_set_margin_start(vgrid, 12);
  gtk_widget_set_margin_top(vgrid, 12);
  gtk_orientable_set_orientation(GTK_ORIENTABLE(vgrid),
                                 GTK_ORIENTATION_VERTICAL);
  gtk_grid_set_row_spacing(GTK_GRID(vgrid), 2);
  gtk_widget_set_halign(vgrid, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(vgrid, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(sbox), vgrid, sbox_col++, 0, 1, 1);

  table = gtk_grid_new();
  start_options_table = table;
  gtk_grid_set_row_spacing(GTK_GRID(table), 2);
  gtk_grid_set_column_spacing(GTK_GRID(table), 12);
  gtk_grid_attach(GTK_GRID(vgrid), table, 0, grid_row++, 1, 1);

  spin = gtk_spin_button_new_with_range(1, MAX_NUM_PLAYERS, 1);
  start_aifill_spin = spin;
  gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 0);
  gtk_spin_button_set_update_policy(GTK_SPIN_BUTTON(spin),
                                    GTK_UPDATE_IF_VALID);
  if (server_optset != NULL) {
    struct option *paifill = optset_option_by_name(server_optset, "aifill");

    if (paifill) {
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin),
                                option_int_get(paifill));
    } /* else it'll be updated later */
  }
  g_signal_connect_after(spin, "value_changed",
                         G_CALLBACK(ai_fill_changed_by_user), NULL);

  gtk_grid_attach(GTK_GRID(table), spin, 1, 0, 1, 1);

  label = g_object_new(GTK_TYPE_LABEL,
                       "use-underline", TRUE,
                       "mnemonic-widget", spin,
                       /* TRANS: Keep individual lines short */
                       "label", _("Number of _Players\n(including AI):"),
                       "xalign", 0.0,
                       "yalign", 0.5,
                       NULL);
  gtk_grid_attach(GTK_GRID(table), label, 0, 0, 1, 1);

  ai_lvl_combobox = gtk_combo_box_text_new();

  for (level = 0; level < AI_LEVEL_COUNT; level++) {
    if (is_settable_ai_level(level)) {
      const char *level_name = ai_level_translated_name(level);

      gtk_combo_box_text_insert_text(GTK_COMBO_BOX_TEXT(ai_lvl_combobox), i, level_name);
      levels[i] = level;
      i++;
    }
  }
  gtk_combo_box_set_active(GTK_COMBO_BOX(ai_lvl_combobox), -1);
  g_signal_connect(ai_lvl_combobox, "changed",
                   G_CALLBACK(ai_skill_callback), levels);

  gtk_grid_attach(GTK_GRID(table), ai_lvl_combobox, 1, 1, 1, 1);

  label = g_object_new(GTK_TYPE_LABEL,
                       "use-underline", TRUE,
                       "mnemonic-widget", ai_lvl_combobox,
                       "label", _("AI Skill _Level:"),
                       "xalign", 0.0,
                       "yalign", 0.5,
                       NULL);
  gtk_grid_attach(GTK_GRID(table), label, 0, 1, 1, 1);

  ruleset_combo = gtk_combo_box_text_new();
  g_signal_connect(G_OBJECT(ruleset_combo), "changed",
                   G_CALLBACK(ruleset_entry_changed), NULL);

  gtk_grid_attach(GTK_GRID(table), ruleset_combo, 1, 2, 1, 1);

  label = g_object_new(GTK_TYPE_LABEL,
                       "use-underline", TRUE,
                       "mnemonic-widget", GTK_COMBO_BOX_TEXT(ruleset_combo),
                       "label", _("Ruleset:"),
                       "xalign", 0.0,
                       "yalign", 0.5,
                       NULL);
  gtk_grid_attach(GTK_GRID(table), label, 0, 2, 1, 1);

  button = icon_label_button_new("preferences-system",
                                 _("_More Game Options..."));
  gtk_widget_set_margin_bottom(button, 8);
  gtk_widget_set_margin_end(button, 8);
  gtk_widget_set_margin_start(button, 8);
  gtk_widget_set_margin_top(button, 8);
  gtk_widget_set_halign(button, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
  g_signal_connect(button, "clicked",
      G_CALLBACK(game_options_callback), NULL);
  gtk_grid_attach(GTK_GRID(vgrid), button, 0, grid_row++, 1, 1);

  connection_list_store = connection_list_store_new();
  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(connection_list_store));
  gtk_widget_set_hexpand(view, TRUE);
  g_object_unref(G_OBJECT(connection_list_store));
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), TRUE);
  connection_list_view = GTK_TREE_VIEW(view);

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
  g_signal_connect(selection, "changed",
                   G_CALLBACK(update_start_page_buttons), NULL);

  add_tree_col(view, G_TYPE_STRING, _("Name"),
               CL_COL_USER_NAME, NULL);
  add_tree_col(view, G_TYPE_BOOLEAN, _("Ready"),
               CL_COL_READY_STATE, NULL);
  add_tree_col(view, G_TYPE_STRING, Q_("?player:Leader"),
               CL_COL_PLAYER_NAME, NULL);
  add_tree_col(view, GDK_TYPE_PIXBUF, _("Flag"),
               CL_COL_FLAG, NULL);
  add_tree_col(view, GDK_TYPE_PIXBUF, _("Border"),
               CL_COL_COLOR, NULL);
  add_tree_col(view, G_TYPE_STRING, _("Nation"),
               CL_COL_NATION, NULL);
  add_tree_col(view, G_TYPE_STRING, _("Team"),
               CL_COL_TEAM, NULL);

  controller = GTK_EVENT_CONTROLLER(gtk_gesture_click_new());
  g_signal_connect(controller, "pressed",
                   G_CALLBACK(connect_list_left_button), NULL);
  gtk_widget_add_controller(view, controller);

  g_signal_connect(view, "row-collapsed",
                   G_CALLBACK(connection_list_row_callback),
                   GINT_TO_POINTER(TRUE));
  g_signal_connect(view, "row-expanded",
                   G_CALLBACK(connection_list_row_callback),
                   GINT_TO_POINTER(FALSE));

  sw = gtk_scrolled_window_new();
  gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(sw), TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_ALWAYS);
  gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(sw), 200);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), view);

  /* This is for a workaround against gtk bug that we can't parent
   * popup to the tree view - we have to parent it all the way to sw */
  gesture = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 3);
  controller = GTK_EVENT_CONTROLLER(gesture);
  g_signal_connect(controller, "pressed",
                   G_CALLBACK(connect_list_right_button), sw);
  gtk_widget_add_controller(view, controller);

  gtk_grid_attach(GTK_GRID(sbox), sw, sbox_col++, 0, 1, 1);

  sw = gtk_scrolled_window_new();
  gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(sw), TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_ALWAYS);
  gtk_grid_attach(GTK_GRID(box), sw, 0, box_row++, 1, 1);

  text = gtk_text_view_new_with_buffer(message_buffer);
  gtk_widget_set_hexpand(text, TRUE);
  gtk_widget_set_vexpand(text, TRUE);
  start_message_area = text;
  gtk_widget_set_name(text, "chatline");
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD);
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text), 5);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), text);


  /* Vote widgets. */
  if (pregame_votebar == NULL) {
    pregame_votebar = voteinfo_bar_new(TRUE);
  }
  gtk_grid_attach(GTK_GRID(box), pregame_votebar, 0, box_row++, 1, 1);

  toolkit_view = inputline_toolkit_view_new();
  gtk_grid_attach(GTK_GRID(box), toolkit_view, 0, box_row++, 1, 1);

  button = gtk_button_new_with_mnemonic(_("_Cancel"));
  inputline_toolkit_view_append_button(toolkit_view, button);
  g_signal_connect(button, "clicked", G_CALLBACK(main_callback), NULL);

  nation_button = icon_label_button_new("document-properties",
                                        _("Pick _Nation"));
  inputline_toolkit_view_append_button(toolkit_view, nation_button);
  g_signal_connect(nation_button, "clicked",
                   G_CALLBACK(nation_button_callback), NULL);

  observe_button = icon_label_button_new("zoom-in", _("_Observe"));
  inputline_toolkit_view_append_button(toolkit_view, observe_button);
  g_signal_connect(observe_button, "clicked",
                   G_CALLBACK(observe_button_callback), NULL);

  ready_button = icon_label_button_new("system-run", _("_Ready"));
  inputline_toolkit_view_append_button(toolkit_view, ready_button);
  g_signal_connect(ready_button, "clicked",
                   G_CALLBACK(ready_button_callback), NULL);

  return box;
}


/**********************************************************************//**
  This regenerates the player information from a loaded game on the server.
**************************************************************************/
void handle_game_load(bool load_successful, const char *filename)
{
  if (load_successful) {
    set_client_page(PAGE_START);

    if (game.info.is_new_game) {
      /* It's pregame. Create a player and connect to it */
      send_chat("/take -");
    }
  }
}

/**********************************************************************//**
  Load a savegame/scenario.
**************************************************************************/
static void load_filename(const char *filename)
{
  send_chat_printf("/load %s", filename);
}

/**********************************************************************//**
  Loads the currently selected game.
**************************************************************************/
static void load_callback(void)
{
  GtkTreeIter it;
  const gchar *filename;

  if (!gtk_tree_selection_get_selected(load_selection, NULL, &it)) {
    return;
  }

  gtk_tree_model_get(GTK_TREE_MODEL(load_store), &it,
                     SD_COL_FULL_PATH, &filename, -1);
  load_filename(filename);
}

/**********************************************************************//**
  Call the default GTK requester for saved game loading.
**************************************************************************/
static void load_browse_callback(GtkWidget *w, gpointer data)
{
  save_dialog_file_chooser_popup(_("Choose Saved Game to Load"),
                                 GTK_FILE_CHOOSER_ACTION_OPEN,
                                 load_filename);
}

/**********************************************************************//**
  Update the load page.
**************************************************************************/
static void update_load_page(void)
{
  /* Search for user saved games. */
  struct fileinfo_list *files = fileinfolist_infix(get_save_dirs(),
                                                   ".sav", FALSE);

  save_dialog_store_update(load_store, files);
  fileinfo_list_destroy(files);
}

/**********************************************************************//**
  Create the load page.
**************************************************************************/
GtkWidget *create_load_page(void)
{
  GtkWidget *box, *sbox, *bbox;

  GtkWidget *button, *label, *view, *sw;
  GtkCellRenderer *rend;
  int box_row = 0;
  int sbox_row = 0;

  box = gtk_grid_new();
  gtk_orientable_set_orientation(GTK_ORIENTABLE(box),
                                 GTK_ORIENTATION_VERTICAL);
  gtk_grid_set_row_spacing(GTK_GRID(box), 18);
  gtk_widget_set_margin_start(box, 4);
  gtk_widget_set_margin_end(box, 4);
  gtk_widget_set_margin_top(box, 4);
  gtk_widget_set_margin_bottom(box, 4);

  load_store = save_dialog_store_new();
  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(load_store));
  gtk_widget_set_vexpand(view, TRUE);
  g_object_unref(load_store);

  rend = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
      -1, NULL, rend, "text", 0, NULL);

  load_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);

  gtk_tree_selection_set_mode(load_selection, GTK_SELECTION_SINGLE);

  g_signal_connect(view, "row-activated",
                   G_CALLBACK(load_callback), NULL);

  sbox = gtk_grid_new();
  gtk_widget_set_halign(sbox, GTK_ALIGN_CENTER);
  gtk_orientable_set_orientation(GTK_ORIENTABLE(sbox),
                                 GTK_ORIENTATION_VERTICAL);
  gtk_grid_set_row_spacing(GTK_GRID(sbox), 2);
  gtk_grid_attach(GTK_GRID(box), sbox, 0, box_row++, 1, 1);

  label = g_object_new(GTK_TYPE_LABEL,
    "use-underline", TRUE,
    "mnemonic-widget", view,
    "label", _("Choose Saved Game to _Load:"),
    "xalign", 0.0,
    "yalign", 0.5,
    NULL);
  gtk_grid_attach(GTK_GRID(sbox), label, 0, sbox_row++, 1, 1);

  sw = gtk_scrolled_window_new();
  gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(sw), 300);
  gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(sw), TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), view);
  gtk_grid_attach(GTK_GRID(sbox), sw, 0, sbox_row++, 1, 1);

  bbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  gtk_widget_set_hexpand(bbox, TRUE);
  gtk_box_set_spacing(GTK_BOX(bbox), 12);
  gtk_grid_attach(GTK_GRID(box), bbox, 0, box_row++, 1, 1);

  button = gtk_button_new_with_mnemonic(_("_Browse..."));
  gtk_box_append(GTK_BOX(bbox), button);
  g_signal_connect(button, "clicked",
                   G_CALLBACK(load_browse_callback), NULL);

  button = gtk_button_new_with_mnemonic(_("_Cancel"));
  gtk_box_append(GTK_BOX(bbox), button);
  g_signal_connect(button, "clicked",
                   G_CALLBACK(main_callback), NULL);

  button = gtk_button_new_with_mnemonic(_("_OK"));
  gtk_box_append(GTK_BOX(bbox), button);
  g_signal_connect(button, "clicked",
                   G_CALLBACK(load_callback), NULL);

  return box;
}

/**********************************************************************//**
  Updates the info for the currently selected scenario.
**************************************************************************/
static void scenario_list_callback(void)
{
  GtkTreeIter it;
  GtkTextBuffer *buffer;
  char *description;
  char *authors;
  char *filename;
  int ver;
  char vername[50];

  if (gtk_tree_selection_get_selected(scenario_selection, NULL, &it)) {
    gtk_tree_model_get(GTK_TREE_MODEL(scenario_store), &it,
                       2, &description, -1);
    gtk_tree_model_get(GTK_TREE_MODEL(scenario_store), &it,
                       3, &authors, -1);
    gtk_tree_model_get(GTK_TREE_MODEL(scenario_store), &it,
                       1, &filename, -1);
    gtk_tree_model_get(GTK_TREE_MODEL(scenario_store), &it,
                       4, &ver, -1);
    filename = skip_to_basename(filename);
    if (ver > 0) {
      int maj;
      int min;

      maj = ver / 1000000;
      ver %= 1000000;
      min = ver / 10000;
      ver %= 10000;
      if (ver >= 9000) {
        /* Development version, have '+' */
        fc_snprintf(vername, sizeof(vername), "%d.%d+", maj, min);
      } else {
        fc_snprintf(vername, sizeof(vername), "%d.%d", maj, min);
      }
    } else {
      /* TRANS: Old scenario format version */
      fc_snprintf(vername, sizeof(vername), _("pre-2.6"));
    }
  } else {
    description = "";
    authors = "";
    filename = "";
    vername[0] = '\0';
  }

  buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(scenario_description));
  gtk_text_buffer_set_text(buffer, description, -1);
  buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(scenario_authors));
  gtk_text_buffer_set_text(buffer, authors, -1);
  gtk_label_set_text(GTK_LABEL(scenario_filename), filename);
  gtk_label_set_text(GTK_LABEL(scenario_version), vername);
}

/**********************************************************************//**
  Loads the currently selected scenario.
**************************************************************************/
static void scenario_callback(void)
{
  GtkTreeIter it;
  char *filename;

  if (!gtk_tree_selection_get_selected(scenario_selection, NULL, &it)) {
    return;
  }

  gtk_tree_model_get(GTK_TREE_MODEL(scenario_store), &it, 1, &filename, -1);
  load_filename(filename);
}

/**********************************************************************//**
  Call the default GTK requester for scenario loading.
**************************************************************************/
static void scenario_browse_callback(GtkWidget *w, gpointer data)
{
  save_dialog_file_chooser_popup(_("Choose a Scenario"),
                                 GTK_FILE_CHOOSER_ACTION_OPEN,
                                 load_filename);
}

/**********************************************************************//**
  Update the scenario page.
**************************************************************************/
static void update_scenario_page(void)
{
  struct fileinfo_list *files;

  gtk_list_store_clear(scenario_store);

  /* search for scenario files. */
  files = fileinfolist_infix(get_scenario_dirs(), ".sav", TRUE);
  fileinfo_list_iterate(files, pfile) {
    struct section_file *sf;

    if ((sf = secfile_load_section(pfile->fullname, "scenario", TRUE))
        && secfile_lookup_bool_default(sf, TRUE, "scenario.is_scenario")) {
      const char *sname, *sdescription, *sauthors;
      int fcver;
      int fcdev;
      int current_ver = MAJOR_VERSION * 1000000 + MINOR_VERSION * 10000;
      int current_dev;

      current_dev = current_ver;
      if (PATCH_VERSION >= 90) {
        /* Patch level matters on development versions */
        current_dev += PATCH_VERSION * 100;
      }

      fcver = secfile_lookup_int_default(sf, 0, "scenario.game_version");
      if (fcver < 30000) {
        /* Pre-3.0 versions stored version number without emergency version
         * part in the end. To get comparable version number stored,
         * multiply by 100. */
        fcver *= 100;
      }
      if (fcver % 10000 >= 9000) {
        fcdev = fcver - (fcver % 100);   /* Emergency version does not count. */
      } else {
        fcdev = fcver - (fcver % 10000); /* Patch version does not count. */
      }
      sname = secfile_lookup_str_default(sf, NULL, "scenario.name");
      sdescription = secfile_lookup_str_default(sf, NULL,
                                                "scenario.description");
      sauthors = secfile_lookup_str_default(sf, NULL, "scenario.authors");
      log_debug("scenario file: %s from %s", sname, pfile->fullname);

      /* Ignore scenarios for newer freeciv versions than we are. */
      if (fcdev <= current_dev) {
        bool add_new = TRUE;

        if (sname != NULL) {
          GtkTreeIter it;
          bool valid;

          valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(scenario_store), &it);
          while (valid) {
            char *oname;

            gtk_tree_model_get(GTK_TREE_MODEL(scenario_store), &it,
                               0, &oname, -1);

            if (!strcmp(sname, oname)) {
              /* Already listed scenario has the same name as the one we just found */
              int existing;

              gtk_tree_model_get(GTK_TREE_MODEL(scenario_store), &it,
                                 4, &existing, -1);
              log_debug("Duplicate %s (%d vs %d)", sname, existing, fcver);

              if (existing > fcver) {
                /* Already listed one has higher version number */
                add_new = FALSE;
              } else if (existing < fcver) {
                /* New one has higher version number */
                add_new = FALSE;

                gtk_list_store_set(scenario_store, &it,
                                   0, sname && strlen(sname) ? Q_(sname) : pfile->name,
                                   1, pfile->fullname,
                                   2, (NULL != sdescription && '\0' != sdescription[0]
                                       ? Q_(sdescription) : ""),
                                   3, (NULL != sauthors && sauthors[0] != '\0'
                                       ? Q_(sauthors) : ""),
                                   4, fcver,
                                   -1);
              } else {
                /* Same version number -> list both */
              }
            }
            valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(scenario_store), &it);
          }
        }

        if (add_new) {
          GtkTreeIter it;

          gtk_list_store_append(scenario_store, &it);
          gtk_list_store_set(scenario_store, &it,
                             0, sname && strlen(sname) ? Q_(sname) : pfile->name,
                             1, pfile->fullname,
                             2, (NULL != sdescription && '\0' != sdescription[0]
                                 ? Q_(sdescription) : ""),
                             3, (NULL != sauthors && sauthors[0] != '\0'
                                 ? Q_(sauthors) : ""),
                             4, fcver,
                             -1);
        }
      }

      secfile_destroy(sf);
    }
  } fileinfo_list_iterate_end;

  fileinfo_list_destroy(files);
}

/**********************************************************************//**
  Scenario table cell bind function
**************************************************************************/
static void scenario_factory_bind(GtkSignalListItemFactory *self,
                                  GtkListItem *list_item,
                                  gpointer user_data)
{
  FcScenRow *row;
  GtkWidget *child = gtk_list_item_get_child(list_item);

  row = gtk_list_item_get_item(list_item);

  gtk_label_set_text(GTK_LABEL(child), row->name);
}

/**********************************************************************//**
  Scenario table cell setup function
**************************************************************************/
static void scenario_factory_setup(GtkSignalListItemFactory *self,
                                   GtkListItem *list_item,
                                   gpointer user_data)
{
  gtk_list_item_set_child(list_item, gtk_label_new(""));
}

/**********************************************************************//**
  Create the scenario page.
**************************************************************************/
GtkWidget *create_scenario_page(void)
{
  GtkWidget *vgrid, *hbox, *sbox, *bbox, *filenamebox, *descbox;
  GtkWidget *versionbox, *vertext;
  GtkWidget *button, *label, *view, *sw, *swa, *text;
  GtkCellRenderer *rend;
  GtkListItemFactory *factory;
  int grid_row = 0;
  int filenamecol = 0;
  int vercol = 0;
  int descrow = 0;

  vgrid = gtk_grid_new();
  gtk_orientable_set_orientation(GTK_ORIENTABLE(vgrid),
                                 GTK_ORIENTATION_VERTICAL);
  gtk_grid_set_row_spacing(GTK_GRID(vgrid), 18);
  gtk_widget_set_margin_start(vgrid, 4);
  gtk_widget_set_margin_end(vgrid, 4);
  gtk_widget_set_margin_top(vgrid, 4);
  gtk_widget_set_margin_bottom(vgrid, 4);

  scenario_store = gtk_list_store_new(5, G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_INT);

  factory = gtk_signal_list_item_factory_new();
  g_signal_connect(factory, "bind", G_CALLBACK(scenario_factory_bind),
                   nullptr);
  g_signal_connect(factory, "setup", G_CALLBACK(scenario_factory_setup),
                   nullptr);

  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(scenario_store));
  gtk_widget_set_hexpand(view, TRUE);
  gtk_widget_set_vexpand(view, TRUE);
  g_object_unref(scenario_store);

  rend = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
      -1, NULL, rend, "text", 0, NULL);

  scenario_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  g_signal_connect(scenario_selection, "changed",
                   G_CALLBACK(scenario_list_callback), NULL);
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);

  gtk_tree_selection_set_mode(scenario_selection, GTK_SELECTION_SINGLE);

  g_signal_connect(view, "row-activated",
                   G_CALLBACK(scenario_callback), NULL);

  label = g_object_new(GTK_TYPE_LABEL,
    "use-underline", TRUE,
    "mnemonic-widget", view,
    "label", _("Choose a _Scenario:"),
    "xalign", 0.0,
    "yalign", 0.5,
    NULL);
  gtk_grid_attach(GTK_GRID(vgrid), label, 0, grid_row++, 1, 1);

  sbox = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(sbox), 12);
  gtk_grid_set_row_homogeneous(GTK_GRID(sbox), TRUE);
  gtk_orientable_set_orientation(GTK_ORIENTABLE(sbox),
                                 GTK_ORIENTATION_VERTICAL);
  gtk_grid_set_row_spacing(GTK_GRID(sbox), 2);
  gtk_grid_attach(GTK_GRID(vgrid), sbox, 0, grid_row++, 1, 1);

  hbox = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(hbox), TRUE);
  gtk_grid_set_column_spacing(GTK_GRID(hbox), 12);

  sw = gtk_scrolled_window_new();
  gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(sw), TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), view);
  gtk_grid_attach(GTK_GRID(sbox), sw, 0, 0, 1, 2);

  text = gtk_text_view_new();
  gtk_widget_set_hexpand(text, TRUE);
  gtk_widget_set_vexpand(text, TRUE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD);
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text), 2);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
  scenario_description = text;

  sw = gtk_scrolled_window_new();
  gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(sw), TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), text);

  text = gtk_text_view_new();
  gtk_widget_set_hexpand(text, TRUE);
  gtk_widget_set_vexpand(text, TRUE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD);
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text), 2);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
  scenario_authors = text;

  swa = gtk_scrolled_window_new();
  gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(swa), TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(swa), GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(swa), text);

  text = gtk_label_new(_("Filename:"));
  scenario_filename = gtk_label_new("");
  gtk_widget_set_halign(scenario_filename, GTK_ALIGN_START);
  gtk_widget_set_valign(scenario_filename, GTK_ALIGN_CENTER);
  gtk_label_set_selectable(GTK_LABEL(scenario_filename), TRUE);

  filenamebox = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(hbox), 12);
  gtk_widget_set_margin_bottom(filenamebox, 5);
  gtk_widget_set_margin_end(filenamebox, 5);
  gtk_widget_set_margin_start(filenamebox, 5);
  gtk_widget_set_margin_top(filenamebox, 5);

  gtk_grid_attach(GTK_GRID(filenamebox), text, filenamecol++, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(filenamebox), scenario_filename,
                  filenamecol++, 0, 1, 1);

  /* TRANS: Scenario format version */
  vertext = gtk_label_new(_("Format:"));
  scenario_version = gtk_label_new("");
  gtk_widget_set_halign(scenario_version, GTK_ALIGN_START);
  gtk_widget_set_valign(scenario_version, GTK_ALIGN_CENTER);
  gtk_label_set_selectable(GTK_LABEL(scenario_version), TRUE);

  versionbox = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(hbox), 12);
  gtk_widget_set_margin_bottom(versionbox, 5);
  gtk_widget_set_margin_end(versionbox, 5);
  gtk_widget_set_margin_start(versionbox, 5);
  gtk_widget_set_margin_top(versionbox, 5);

  gtk_grid_attach(GTK_GRID(versionbox), vertext, vercol++, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(versionbox), scenario_version,
                  vercol++, 0, 1, 1);

  descbox = gtk_grid_new();
  gtk_orientable_set_orientation(GTK_ORIENTABLE(descbox),
                                 GTK_ORIENTATION_VERTICAL);
  gtk_grid_set_row_spacing(GTK_GRID(descbox), 6);
  gtk_grid_attach(GTK_GRID(descbox), sw, 0, descrow++, 1, 1);
  gtk_grid_attach(GTK_GRID(descbox), swa, 0, descrow++, 1, 1);
  gtk_grid_attach(GTK_GRID(descbox), filenamebox,
                  0, descrow++, 1, 1);
  gtk_grid_attach(GTK_GRID(descbox), versionbox,
                  0, descrow++, 1, 1);
  gtk_grid_attach(GTK_GRID(sbox), descbox, 1, 0, 1, 2);

  bbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  gtk_box_set_spacing(GTK_BOX(bbox), 12);
  gtk_grid_attach(GTK_GRID(vgrid), bbox, 0, grid_row++, 1, 1);

  button = gtk_button_new_with_mnemonic(_("_Browse..."));
  gtk_box_append(GTK_BOX(bbox), button);
  g_signal_connect(button, "clicked",
      G_CALLBACK(scenario_browse_callback), NULL);

  button = gtk_button_new_with_mnemonic(_("_Cancel"));
  gtk_box_append(GTK_BOX(bbox), button);
  g_signal_connect(button, "clicked",
                   G_CALLBACK(main_callback), NULL);

  button = gtk_button_new_with_mnemonic(_("_OK"));
  gtk_box_append(GTK_BOX(bbox), button);
  g_signal_connect(button, "clicked",
                   G_CALLBACK(scenario_callback), NULL);

  return vgrid;
}

/**********************************************************************//**
  Returns current client page
**************************************************************************/
enum client_pages get_current_client_page(void)
{
  return current_page;
}

/**********************************************************************//**
  Changes the current page. The action is delayed.
**************************************************************************/
void real_set_client_page(enum client_pages new_page)
{
  /* Don't use current_page directly here because maybe it could be modified
   * before we reach the end of this function. */
  enum client_pages old_page = current_page;

  /* If the page remains the same, don't do anything. */
  if (old_page == new_page) {
    return;
  }

  log_debug("Switching client page from %s to %s.",
            -1 == old_page ? "(no page)" : client_pages_name(old_page),
            client_pages_name(new_page));

  current_page = new_page;

  switch (old_page) {
  case PAGE_SCENARIO:
  case PAGE_LOAD:
    break;
  case PAGE_GAME:
    {
      struct video_mode *vmode = resolution_request_get();

      enable_menus(FALSE);
      if (vmode == NULL) {
        gtk_window_unmaximize(GTK_WINDOW(toplevel));
      }
    }
    break;
  default:
    break;
  }

  switch (new_page) {
  case PAGE_MAIN:
  case PAGE_START:
    if (is_server_running()) {
      gtk_widget_set_visible(start_options_table, game.info.is_new_game);
      update_start_page();
    } else {
      gtk_widget_set_visible(start_options_table, FALSE);
    }
    voteinfo_gui_update();
    overview_size_changed();
    conn_list_dialog_update();
    break;
  case PAGE_GAME:
    {
      struct video_mode *vmode = resolution_request_get();

      reset_unit_table();
      enable_menus(TRUE);
      if (vmode == NULL) {
        gtk_window_maximize(GTK_WINDOW(toplevel));
      }
      voteinfo_gui_update();
      mapview_freeze();
    }
    break;
  case PAGE_LOAD:
    update_load_page();
    break;
  case PAGE_SCENARIO:
    update_scenario_page();
    break;
  case PAGE_NETWORK:
    update_network_page();
    break;
  }

  /* Hide/show statusbar. */
  if (new_page == PAGE_START || new_page == PAGE_GAME) {
    clear_network_statusbar();
    gtk_widget_set_visible(statusbar_frame, FALSE);
  } else {
    gtk_widget_set_visible(statusbar_frame, TRUE);
  }

  gtk_notebook_set_current_page(GTK_NOTEBOOK(toplevel_tabs), new_page);

  /* Update the GUI. */
  while (g_main_context_pending(NULL)) {
    g_main_context_iteration(NULL, FALSE);
  }

  switch (new_page) {
  case PAGE_MAIN:
    break;
  case PAGE_START:
    chatline_scroll_to_bottom(FALSE);
    inputline_grab_focus();
    break;
  case PAGE_LOAD:
    gtk_tree_view_focus(gtk_tree_selection_get_tree_view(load_selection));
    break;
  case PAGE_SCENARIO:
    gtk_tree_view_focus(gtk_tree_selection_get_tree_view(scenario_selection));
    break;
  case PAGE_GAME:
    chatline_scroll_to_bottom(FALSE);
    refresh_chat_buttons();
    center_on_something();
    mapview_thaw();
    add_idle_callback(main_message_area_resize, NULL);
    add_idle_callback(animation_idle_cb, NULL);
    break;
  case PAGE_NETWORK:
    update_network_lists();
    gtk_widget_grab_focus(network_login);
    gtk_editable_set_position(GTK_EDITABLE(network_login), 0);
    set_connection_state(connection_status);
    break;
  }
}

/****************************************************************************
                            SAVE GAME DIALOGs
****************************************************************************/

/**********************************************************************//**
  Save game 'save_dialog_files_fn_t' implementation.
**************************************************************************/
static struct fileinfo_list *save_dialog_savegame_list(void)
{
  return fileinfolist_infix(get_save_dirs(), ".sav", FALSE);
}

/**********************************************************************//**
  Save game dialog.
**************************************************************************/
void save_game_dialog_popup(void)
{
  static GtkWidget *shell = NULL;

  if (NULL != shell) {
    return;
  }

  shell = save_dialog_new(_("Save Game"), _("Saved _Games:"),
                          _("Save _Filename:"), send_save_game,
                          save_dialog_savegame_list);
  g_signal_connect(shell, "destroy", G_CALLBACK(widget_destroyed),
                   &shell);
  gtk_window_present(GTK_WINDOW(shell));
}

/**********************************************************************//**
  Save scenario 'save_dialog_action_fn_t' implementation.
**************************************************************************/
static void save_dialog_save_scenario(const char *filename)
{
  dsend_packet_save_scenario(&client.conn, filename);
}

/**********************************************************************//**
  Save scenario 'save_dialog_files_fn_t' implementation.
**************************************************************************/
static struct fileinfo_list *save_dialog_scenario_list(void)
{
  return fileinfolist_infix(get_scenario_dirs(), ".sav", FALSE);
}

/**********************************************************************//**
  Save scenario dialog.
**************************************************************************/
void save_scenario_dialog_popup(void)
{
  static GtkWidget *shell = NULL;

  if (NULL != shell) {
    return;
  }

  shell = save_dialog_new(_("Save Scenario"), _("Saved Sce_narios:"),
                          _("Save Sc_enario:"), save_dialog_save_scenario,
                          save_dialog_scenario_list);
  g_signal_connect(shell, "destroy", G_CALLBACK(widget_destroyed),
                   &shell);
  gtk_window_present(GTK_WINDOW(shell));
}

/**********************************************************************//**
  Save mapimg 'save_dialog_files_fn_t' implementation. If possible, only the
  current directory is used. As fallback, the files in the save directories
  are listed.
**************************************************************************/
static struct fileinfo_list *save_dialog_mapimg_list(void)
{
  return fileinfolist_infix(get_save_dirs(), ".map", FALSE);
}

/**********************************************************************//**
  Save scenario dialog.
**************************************************************************/
void save_mapimg_dialog_popup(void)
{
  static GtkWidget *shell = NULL;

  if (NULL != shell) {
    return;
  }

  shell = save_dialog_new(_("Save Map Image"), _("Saved Map _Images:"),
                          _("Save _Map Images:"), mapimg_client_save,
                          save_dialog_mapimg_list);
  g_signal_connect(shell, "destroy", G_CALLBACK(widget_destroyed),
                   &shell);
  gtk_window_present(GTK_WINDOW(shell));
}

/**********************************************************************//**
  Save map image. On error popup a message window for the user.
**************************************************************************/
void mapimg_client_save(const char *filename)
{
  if (!mapimg_client_createmap(filename)) {
    char msg[512];

    fc_snprintf(msg, sizeof(msg), "(%s)", mapimg_error());
    popup_notify_dialog(_("Error"),
                        _("Error Creating the Map Image!"), msg);
  }
}

/**********************************************************************//**
  Set the list of available rulesets. The default ruleset should be
  "default", and if the user changes this then set_ruleset() should be
  called.
**************************************************************************/
void set_rulesets(int num_rulesets, char **rulesets)
{
  int i;
  int def_idx = -1;

  gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(ruleset_combo));
  for (i = 0; i < num_rulesets; i++) {

    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ruleset_combo), rulesets[i]);
    if (!strcmp("default", rulesets[i])) {
      def_idx = i;
    }
  }

  no_ruleset_callback = TRUE;

  /* HACK: Server should tell us the current ruleset. */
  gtk_combo_box_set_active(GTK_COMBO_BOX(ruleset_combo), def_idx);

  no_ruleset_callback = FALSE;
}
