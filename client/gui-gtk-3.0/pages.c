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

#include <stdio.h>
#include <time.h>

#include <sys/stat.h>

#include <gtk/gtk.h>

#ifdef GGZ_GTK
#  include <ggz-gtk.h>
#endif

/* utility */
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "shared.h"
#include "support.h"

/* common */
#include "dataio.h"
#include "game.h"
#include "mapimg.h"
#include "version.h"

/* client */
#include "client_main.h"
#include "climisc.h"
#include "clinet.h"
#include "connectdlg_common.h"
#include "ggzclient.h"
#include "packhand.h"
#include "servers.h"
#include "update_queue.h"

/* client/gui-gtk-3.0 */
#include "chatline.h"
#include "connectdlg.h"
#include "dialogs.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapview.h"
#include "optiondlg.h"
#include "plrdlg.h"             /* get_flag() */
#include "repodlgs.h"
#include "voteinfo_bar.h"

#include "pages.h"


static GtkWidget *scenario_description;
static GtkWidget *scenario_filename;

static GtkListStore *load_store, *scenario_store, *meta_store, *lan_store; 

static GtkListStore *server_playerlist_store;
static GtkWidget *server_playerlist_view;

static GtkTreeSelection *load_selection, *scenario_selection;
static GtkTreeSelection *meta_selection, *lan_selection;

/* This is the current page. Invalid value at start, to be sure that it won't
 * be catch throught a switch() statement. */
static enum client_pages current_page = -1;

static guint meta_scan_timer, lan_scan_timer;
static struct server_scan *meta_scan, *lan_scan;

static GtkWidget *statusbar, *statusbar_frame;
static GQueue *statusbar_queue;
static guint statusbar_timer = 0;

static GtkWidget *ruleset_combo;

static void connection_state_reset(void);

/**************************************************************************
  spawn a server, if there isn't one, using the default settings.
**************************************************************************/
static void start_new_game_callback(GtkWidget *w, gpointer data)
{
  if (is_server_running() || client_start_server()) {
    /* saved settings are sent in client/options.c load_settable_options() */
  }
}

/**************************************************************************
  go to the scenario page, spawning a server,
**************************************************************************/
static void start_scenario_callback(GtkWidget *w, gpointer data)
{
  set_client_page(PAGE_SCENARIO);
  client_start_server();
}

/**************************************************************************
  go to the load page, spawning a server.
**************************************************************************/
static void load_saved_game_callback(GtkWidget *w, gpointer data)
{
  set_client_page(PAGE_LOAD);
  client_start_server();
}

/****************************************************************************
  Reset the connection status and switch to network page.
****************************************************************************/
static void connect_network_game_callback(GtkWidget *w, gpointer data)
{
  connection_state_reset();
  set_client_page(PAGE_NETWORK);
}

/**************************************************************************
  cancel, by terminating the connection and going back to main page.
**************************************************************************/
static void main_callback(GtkWidget *w, gpointer data)
{
  enum client_pages page = in_ggz ? PAGE_GGZ : PAGE_MAIN;

  if (client.conn.used) {
    disconnect_from_server();
  }
  if (page != get_client_page()) {
    set_client_page(page);
  }
}

/**************************************************************************
  this is called whenever the intro graphic needs a graphics refresh.
**************************************************************************/
static gboolean intro_expose(GtkWidget *w, GdkEventExpose *ev)
{
  static PangoLayout *layout;
  static int width, height;

  if (!layout) {
    char msgbuf[128];

    layout = pango_layout_new(gdk_pango_context_get());
    pango_layout_set_font_description(layout,
         pango_font_description_from_string("Sans Bold 10"));

    fc_snprintf(msgbuf, sizeof(msgbuf), "%s%s",
                word_version(), VERSION_STRING);
    pango_layout_set_text(layout, msgbuf, -1);

    pango_layout_get_pixel_size(layout, &width, &height);
  }
 
  gtk_draw_shadowed_string(w->window,
      w->style->black_gc,
      w->style->white_gc,
      w->allocation.x + w->allocation.width - width - 4,
      w->allocation.y + w->allocation.height - height - 4,
      layout);
  return TRUE;
}

#ifdef GGZ_GTK
/****************************************************************************
  Callback to raise the login dialog when the gaming zone login button is
  clicked.
****************************************************************************/
static void ggz_login(void)
{
  set_client_page(PAGE_GGZ);
}
#endif /* GGZ_GTK */

/**************************************************************************
  create the main page.
**************************************************************************/
GtkWidget *create_main_page(void)
{
  GtkWidget *widget, *align, *vbox, *frame, *image, *button, *table;
  GtkSizeGroup *size;

  size = gtk_size_group_new(GTK_SIZE_GROUP_BOTH);

  vbox = gtk_vbox_new(FALSE, 0);
  widget = vbox;

  align = gtk_alignment_new(0.5, 0.0, 0.0, 0.0);
  gtk_container_set_border_width(GTK_CONTAINER(align), 18);
  gtk_box_pack_start(GTK_BOX(vbox), align, FALSE, FALSE, 0);

  frame = gtk_frame_new(NULL);
  gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_OUT);
  gtk_container_add(GTK_CONTAINER(align), frame);

  image = gtk_image_new_from_file(tileset_main_intro_filename(tileset));
  g_signal_connect_after(image, "expose_event",
                         G_CALLBACK(intro_expose), NULL);
  gtk_container_add(GTK_CONTAINER(frame), image);

#if IS_BETA_VERSION
  {
    GtkWidget *label;

    label = gtk_label_new(beta_message());
    gtk_widget_set_name(label, "beta_label");
    gtk_misc_set_alignment(GTK_MISC(label), 0.5, 0.5);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
  }
#endif /* IS_BETA_VERSION */

  table = gtk_table_new(3, 2, TRUE);
  align = gtk_alignment_new(0.5, 0.5, 0.0, 0.0);
  gtk_box_pack_start(GTK_BOX(vbox), align, TRUE, TRUE, 12);

  gtk_table_set_row_spacings(GTK_TABLE(table), 8);
  gtk_table_set_col_spacings(GTK_TABLE(table), 18);
  gtk_container_add(GTK_CONTAINER(align), table);

  button = gtk_button_new_with_mnemonic(_("Start _New Game"));
  gtk_size_group_add_widget(size, button);
  gtk_table_attach_defaults(GTK_TABLE(table), button, 0, 1, 0, 1);
  g_signal_connect(button, "clicked",
                   G_CALLBACK(start_new_game_callback), NULL);

  button = gtk_button_new_with_mnemonic(_("Start _Scenario Game"));
  gtk_size_group_add_widget(size, button);
  gtk_table_attach_defaults(GTK_TABLE(table), button, 0, 1, 1, 2);
  g_signal_connect(button, "clicked",
                   G_CALLBACK(start_scenario_callback), NULL);

  button = gtk_button_new_with_mnemonic(_("_Load Saved Game"));
  gtk_size_group_add_widget(size, button);
  gtk_table_attach_defaults(GTK_TABLE(table), button, 0, 1, 2, 3);
  g_signal_connect(button, "clicked",
                   G_CALLBACK(load_saved_game_callback), NULL);

  button = gtk_button_new_with_mnemonic(_("C_onnect to Network Game"));
  gtk_size_group_add_widget(size, button);
  gtk_table_attach_defaults(GTK_TABLE(table), button, 1, 2, 0, 1);
  g_signal_connect(button, "clicked",
                   G_CALLBACK(connect_network_game_callback), NULL);

#ifdef GGZ_GTK
  button = gtk_button_new_with_mnemonic(_("Connect to Gaming _Zone"));
  gtk_size_group_add_widget(size, button);
  gtk_table_attach_defaults(GTK_TABLE(table), button, 1, 2, 1, 2);
  g_signal_connect(button, "clicked", ggz_login, NULL);
#endif /* GGZ_GTK */

  button = gtk_button_new_from_stock(GTK_STOCK_QUIT);
  gtk_size_group_add_widget(size, button);
  gtk_table_attach_defaults(GTK_TABLE(table), button, 1, 2, 2, 3);
  g_signal_connect(button, "clicked",
                   G_CALLBACK(gtk_main_quit), NULL);

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

/****************************************************************************
  Create a new file list store.
****************************************************************************/
static inline GtkListStore *save_dialog_store_new(void)
{
  return gtk_list_store_new(SD_COL_NUM,
                            G_TYPE_STRING,      /* SD_COL_PRETTY_NAME */
                            G_TYPE_STRING);     /* SD_COL_FULL_PATH */
}

/****************************************************************************
  Fill a file list store with 'files'.
****************************************************************************/
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

/****************************************************************************
  Update a save dialog.
****************************************************************************/
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

/**************************************************************************
  Callback for save_dialog_file_chooser_new().
**************************************************************************/
static void save_dialog_file_chooser_callback(GtkWidget *widget,
                                              gint response, gpointer data)
{
  if (response == GTK_RESPONSE_OK) {
    save_dialog_action_fn_t action = data;
    gchar *filename = g_filename_to_utf8(gtk_file_chooser_get_filename
                                         (GTK_FILE_CHOOSER(widget)),
                                         -1, NULL, NULL, NULL);

    if (NULL != filename) {
      action(filename);
      g_free(filename);
    }
  }
  gtk_widget_destroy(widget);
}

/****************************************************************************
  Create a file chooser for both the load and save commands.
****************************************************************************/
static void save_dialog_file_chooser_popup(const char *title,
                                           GtkFileChooserAction action,
                                           save_dialog_action_fn_t cb)
{
  GtkWidget *filechoose;

  /* Create the chooser */
  filechoose = gtk_file_chooser_dialog_new(title, GTK_WINDOW(toplevel), action,
      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
      action == GTK_FILE_CHOOSER_ACTION_SAVE ? GTK_STOCK_SAVE : GTK_STOCK_OPEN,
      GTK_RESPONSE_OK, NULL);
  setup_dialog(filechoose, toplevel);
  gtk_window_set_position(GTK_WINDOW(filechoose), GTK_WIN_POS_MOUSE);

  g_signal_connect(filechoose, "response",
                   G_CALLBACK(save_dialog_file_chooser_callback), cb);

  /* Display that dialog */
  gtk_window_present(GTK_WINDOW(filechoose));
}

/****************************************************************************
  Handle save dialog response.
****************************************************************************/
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
      const char *text = gtk_entry_get_text(pdialog->entry);
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
  gtk_widget_destroy(GTK_WIDGET(pdialog->shell));
}

/****************************************************************************
  Handle save list double click.
****************************************************************************/
static void save_dialog_row_callback(GtkTreeView *tree_view,
                                     GtkTreePath *path,
                                     GtkTreeViewColumn *column,
                                     gpointer data)
{
  save_dialog_response_callback(NULL, SD_RES_SAVE, data);
}

/**************************************************************************
  Handle save filename entry activation.
**************************************************************************/
static void save_dialog_entry_callback(GtkEntry *entry, gpointer data)
{
  save_dialog_response_callback(NULL, SD_RES_SAVE, data);
}

/**************************************************************************
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
  gtk_entry_set_text(pdialog->entry, filename);
}

/****************************************************************************
  Create a new save dialog.
****************************************************************************/
static GtkWidget *save_dialog_new(const char *title, const char *savelabel,
                                  const char *savefilelabel,
                                  save_dialog_action_fn_t action,
                                  save_dialog_files_fn_t files)
{
  GtkWidget *shell, *sbox, *sw, *label, *view, *entry;
  GtkBox *vbox;
  GtkListStore *store;
  GtkCellRenderer *rend;
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
                                      GTK_STOCK_DELETE, SD_RES_DELETE,
                                      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                      GTK_STOCK_SAVE, SD_RES_SAVE,
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
  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  g_object_unref(store);
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);
  g_signal_connect(view, "row-activated",
                   G_CALLBACK(save_dialog_row_callback), pdialog);
  pdialog->tree_view = GTK_TREE_VIEW(view);

  sbox = gtk_vbox_new(FALSE, 2);
  gtk_box_pack_start(vbox, sbox, TRUE, TRUE, 0);

  label = g_object_new(GTK_TYPE_LABEL,
                       "use-underline", TRUE,
                       "mnemonic-widget", view,
                       "label", savelabel,
                       "xalign", 0.0,
                       "yalign", 0.5,
                       NULL);
  gtk_box_pack_start(GTK_BOX(sbox), label, FALSE, FALSE, 0);

  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
                                      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request(sw, 300, 300);
  gtk_container_add(GTK_CONTAINER(sw), view);
  gtk_box_pack_start(GTK_BOX(sbox), sw, TRUE, TRUE, 0);

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
  g_signal_connect(entry, "activate",
                   G_CALLBACK(save_dialog_entry_callback), pdialog);
  pdialog->entry = GTK_ENTRY(entry);

  sbox = gtk_vbox_new(FALSE, 2);

  label = g_object_new(GTK_TYPE_LABEL,
                       "use-underline", TRUE,
                       "mnemonic-widget", entry,
                       "label", savefilelabel,
                       "xalign", 0.0,
                       "yalign", 0.5,
                       NULL);
  gtk_box_pack_start(GTK_BOX(sbox), label, FALSE, FALSE, 0);

  gtk_box_pack_start(GTK_BOX(sbox), entry, FALSE, FALSE, 0);
  gtk_box_pack_start(vbox, sbox, FALSE, FALSE, 12);

  save_dialog_update(pdialog);
  gtk_window_set_focus(GTK_WINDOW(shell), entry);
  gtk_widget_show_all(GTK_WIDGET(vbox));
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

/**************************************************************************
  update a server list.
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

  host = gtk_entry_get_text(GTK_ENTRY(network_host));
  portstr = gtk_entry_get_text(GTK_ENTRY(network_port));
  port = atoi(portstr);

  server_list_iterate(list, pserver) {
    char buf[20];

    if (pserver->humans >= 0) {
      fc_snprintf(buf, sizeof(buf), "%d", pserver->humans);
    } else {
      strncpy(buf, _("Unknown"), sizeof(buf));
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

/**************************************************************************
  Free the server scans.
**************************************************************************/
static void destroy_server_scans(void)
{
  if (meta_scan) {
    server_scan_finish(meta_scan);
    meta_scan = NULL;
  }
  if (meta_scan_timer != 0) {
    g_source_remove(meta_scan_timer);
    meta_scan_timer = 0;
  }
  if (lan_scan) {
    server_scan_finish(lan_scan);
    lan_scan = NULL;
  }
  if (lan_scan_timer != 0) {
    g_source_remove(lan_scan_timer);
    lan_scan_timer = 0;
  }
}

/**************************************************************************
  This function updates the list of servers every so often.
**************************************************************************/
static gboolean check_server_scan(gpointer data)
{
  struct server_scan *scan = data;
  const struct server_list *servers;
  enum server_scan_status stat;

  if (!scan) {
    return FALSE;
  }

  stat = server_scan_poll(scan);
  if (stat >= SCAN_STATUS_PARTIAL) {
    enum server_scan_type type;
    type = server_scan_get_type(scan);
    servers = server_scan_get_list(scan);
    update_server_list(type, servers);
  }

  if (stat == SCAN_STATUS_ERROR || stat == SCAN_STATUS_DONE) {
    return FALSE;
  }
  return TRUE;
}

/**************************************************************************
  Callback function for when there's an error in the server scan.
**************************************************************************/
static void server_scan_error(struct server_scan *scan,
			      const char *message)
{
  output_window_append(ftc_client, message);
  log_error("%s", message);

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

/**************************************************************************
  Stop and restart the metaserver and lan server scans.
**************************************************************************/
static void update_network_lists(void)
{
  destroy_server_scans();

  meta_scan = server_scan_begin(SERVER_SCAN_GLOBAL, server_scan_error);
  meta_scan_timer = g_timeout_add(200, check_server_scan, meta_scan);

  lan_scan = server_scan_begin(SERVER_SCAN_LOCAL, server_scan_error);
  lan_scan_timer = g_timeout_add(500, check_server_scan, lan_scan);
}

/**************************************************************************
  network connection state defines.
**************************************************************************/
enum connection_state {
  LOGIN_TYPE, 
  NEW_PASSWORD_TYPE, 
  ENTER_PASSWORD_TYPE,
  WAITING_TYPE
};

static enum connection_state connection_status;

/**************************************************************************
  update statusbar label text.
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

/**************************************************************************
  clear statusbar queue.
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

/**************************************************************************
  queue statusbar label text change.
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

/**************************************************************************
  create statusbar.
**************************************************************************/
GtkWidget *create_statusbar(void)
{
  statusbar_frame = gtk_frame_new(NULL);
  gtk_frame_set_shadow_type(GTK_FRAME(statusbar_frame), GTK_SHADOW_IN);

  statusbar = gtk_label_new("");
  gtk_misc_set_padding(GTK_MISC(statusbar), 2, 2);
  gtk_container_add(GTK_CONTAINER(statusbar_frame), statusbar);

  statusbar_queue = g_queue_new();
  statusbar_timer = g_timeout_add(2000, update_network_statusbar, NULL);

  return statusbar_frame;
}

/**************************************************************************
  update network page connection state.
**************************************************************************/
static void set_connection_state(enum connection_state state)
{
  switch (state) {
  case LOGIN_TYPE:
    append_network_statusbar("", FALSE);

    gtk_entry_set_text(GTK_ENTRY(network_password), "");
    gtk_entry_set_text(GTK_ENTRY(network_confirm_password), "");

    gtk_widget_set_sensitive(network_host, TRUE);
    gtk_widget_set_sensitive(network_port, TRUE);
    gtk_widget_set_sensitive(network_login, TRUE);
    gtk_widget_set_sensitive(network_password_label, FALSE);
    gtk_widget_set_sensitive(network_password, FALSE);
    gtk_widget_set_sensitive(network_confirm_password_label, FALSE);
    gtk_widget_set_sensitive(network_confirm_password, FALSE);
    break;
  case NEW_PASSWORD_TYPE:
    set_client_page(PAGE_NETWORK);
    gtk_entry_set_text(GTK_ENTRY(network_password), "");
    gtk_entry_set_text(GTK_ENTRY(network_confirm_password), "");

    gtk_widget_set_sensitive(network_host, FALSE);
    gtk_widget_set_sensitive(network_port, FALSE);
    gtk_widget_set_sensitive(network_login, FALSE);
    gtk_widget_set_sensitive(network_password_label, TRUE);
    gtk_widget_set_sensitive(network_password, TRUE);
    gtk_widget_set_sensitive(network_confirm_password_label, TRUE);
    gtk_widget_set_sensitive(network_confirm_password, TRUE);

    gtk_widget_grab_focus(network_password);
    break;
  case ENTER_PASSWORD_TYPE:
    set_client_page(PAGE_NETWORK);
    gtk_entry_set_text(GTK_ENTRY(network_password), "");
    gtk_entry_set_text(GTK_ENTRY(network_confirm_password), "");

    gtk_widget_set_sensitive(network_host, FALSE);
    gtk_widget_set_sensitive(network_port, FALSE);
    gtk_widget_set_sensitive(network_login, FALSE);
    gtk_widget_set_sensitive(network_password_label, TRUE);
    gtk_widget_set_sensitive(network_password, TRUE);
    gtk_widget_set_sensitive(network_confirm_password_label, FALSE);
    gtk_widget_set_sensitive(network_confirm_password, FALSE);

    gtk_widget_grab_focus(network_password);
    break;
  case WAITING_TYPE:
    append_network_statusbar("", TRUE);

    gtk_widget_set_sensitive(network_login, FALSE);
    gtk_widget_set_sensitive(network_password_label, FALSE);
    gtk_widget_set_sensitive(network_password, FALSE);
    gtk_widget_set_sensitive(network_confirm_password_label, FALSE);
    gtk_widget_set_sensitive(network_confirm_password, FALSE);
    break;
  }

  connection_status = state;
}

/****************************************************************************
  Reset the connection state.
****************************************************************************/
static void connection_state_reset(void)
{
  set_connection_state(LOGIN_TYPE);
}

/**************************************************************************
 configure the dialog depending on what type of authentication request the
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
    /* if we magically have a password already present in 'password'
     * then, use that and skip the password entry dialog */
    if (password[0] != '\0') {
      struct packet_authentication_reply reply;

      sz_strlcpy(reply.password, password);
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

/**************************************************************************
 if on the network page, switch page to the login page (with new server
 and port). if on the login page, send connect and/or authentication 
 requests to the server.
**************************************************************************/
static void connect_callback(GtkWidget *w, gpointer data)
{
  char errbuf [512];
  struct packet_authentication_reply reply;

  switch (connection_status) {
  case LOGIN_TYPE:
    sz_strlcpy(user_name, gtk_entry_get_text(GTK_ENTRY(network_login)));
    sz_strlcpy(server_host, gtk_entry_get_text(GTK_ENTRY(network_host)));
    server_port = atoi(gtk_entry_get_text(GTK_ENTRY(network_port)));
  
    if (connect_to_server(user_name, server_host, server_port,
                          errbuf, sizeof(errbuf)) != -1) {
    } else {
      append_network_statusbar(errbuf, TRUE);

      output_window_append(ftc_client, errbuf);
    }
    return; 
  case NEW_PASSWORD_TYPE:
    if (w != network_password) {
      sz_strlcpy(password,
	  gtk_entry_get_text(GTK_ENTRY(network_password)));
      sz_strlcpy(reply.password,
	  gtk_entry_get_text(GTK_ENTRY(network_confirm_password)));
      if (strncmp(reply.password, password, MAX_LEN_NAME) == 0) {
	password[0] = '\0';
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
	gtk_entry_get_text(GTK_ENTRY(network_password)));
    send_packet_authentication_reply(&client.conn, &reply);

    set_connection_state(WAITING_TYPE);
    return;
  case WAITING_TYPE:
    return;
  }

  log_error("Unsupported connection status: %d", connection_status);
}

/**************************************************************************
  connect on list item double-click.
***************************************************************************/
static void network_activate_callback(GtkTreeView *view,
                      		      GtkTreePath *arg1,
				      GtkTreeViewColumn *arg2,
				      gpointer data)
{
  connect_callback(NULL, data);
}

/**************************************************************************
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

/**************************************************************************
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
    const struct server_list *servers;

    servers = server_scan_get_list(meta_scan);
    path = gtk_tree_model_get_path(model, &it);
    if (servers && path) {
      gint pos = gtk_tree_path_get_indices(path)[0];
      pserver = server_list_get(servers, pos);
    }
    gtk_tree_path_free(path);
  }
  update_server_playerlist(pserver);

  gtk_tree_model_get(model, &it, 0, &host, 1, &port, -1);

  gtk_entry_set_text(GTK_ENTRY(network_host), host);
  fc_snprintf(portstr, sizeof(portstr), "%d", port);
  gtk_entry_set_text(GTK_ENTRY(network_port), portstr);
}

/**************************************************************************
  update the network page.
**************************************************************************/
static void update_network_page(void)
{
  char buf[256];

  gtk_tree_selection_unselect_all(lan_selection);
  gtk_tree_selection_unselect_all(meta_selection);

  gtk_entry_set_text(GTK_ENTRY(network_login), user_name);
  gtk_entry_set_text(GTK_ENTRY(network_host), server_host);
  fc_snprintf(buf, sizeof(buf), "%d", server_port);
  gtk_entry_set_text(GTK_ENTRY(network_port), buf);
}

/**************************************************************************
  create the network page.
**************************************************************************/
GtkWidget *create_network_page(void)
{
  GtkWidget *box, *sbox, *bbox, *hbox, *notebook;
  GtkWidget *button, *label, *view, *sw, *table;
  GtkTreeSelection *selection;
  GtkListStore *store;

  box = gtk_vbox_new(FALSE, 0);
  gtk_container_set_border_width(GTK_CONTAINER(box), 4);

  notebook = gtk_notebook_new();
  gtk_container_add(GTK_CONTAINER(box), notebook);

  /* LAN pane. */
  lan_store = gtk_list_store_new(7, G_TYPE_STRING, /* host */
                                 G_TYPE_INT,       /* port */
                                 G_TYPE_STRING,    /* version */
                                 G_TYPE_STRING,    /* state */
                                 G_TYPE_INT,       /* nplayers */
                                 G_TYPE_STRING,    /* humans */
                                 G_TYPE_STRING);   /* message */

  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(lan_store));
  g_object_unref(lan_store);
  gtk_tree_view_columns_autosize(GTK_TREE_VIEW(view));

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  lan_selection = selection;
  gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
  g_signal_connect(view, "focus",
      		   G_CALLBACK(gtk_true), NULL);
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

  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_set_border_width(GTK_CONTAINER(sw), 4);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
				      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(sw), view);
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
  g_object_unref(meta_store);
  gtk_tree_view_columns_autosize(GTK_TREE_VIEW(view));

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  meta_selection = selection;
  gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
  g_signal_connect(view, "focus",
      		   G_CALLBACK(gtk_true), NULL);
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

  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_set_border_width(GTK_CONTAINER(sw), 4);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
				      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(sw), view);
  if (gui_gtk3_metaserver_tab_first) {
    gtk_notebook_prepend_page(GTK_NOTEBOOK(notebook), sw, label);
  } else {
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), sw, label);
  }

  /* Bottom part of the page, outside the inner notebook. */
  sbox = gtk_vbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box), sbox, FALSE, FALSE, 0);

  hbox = gtk_hbox_new(FALSE, 12);
  gtk_box_pack_start(GTK_BOX(sbox), hbox, FALSE, FALSE, 8);

  table = gtk_table_new(6, 2, FALSE);
  gtk_table_set_row_spacings(GTK_TABLE(table), 2);
  gtk_table_set_col_spacings(GTK_TABLE(table), 12);
  gtk_table_set_row_spacing(GTK_TABLE(table), 2, 12);
  gtk_box_pack_start(GTK_BOX(hbox), table, FALSE, FALSE, 0);

  network_host = gtk_entry_new();
  g_signal_connect(network_host, "activate",
      G_CALLBACK(connect_callback), NULL);
  gtk_table_attach(GTK_TABLE(table), network_host, 1, 2, 0, 1,
		   GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

  label = g_object_new(GTK_TYPE_LABEL,
		       "use-underline", TRUE,
		       "mnemonic-widget", network_host,
		       "label", _("_Host:"),
		       "xalign", 0.0,
		       "yalign", 0.5,
		       NULL);
  network_host_label = label;
  gtk_table_attach(GTK_TABLE(table), label, 0, 1, 0, 1,
		   GTK_FILL, GTK_FILL, 0, 0);

  network_port = gtk_entry_new();
  g_signal_connect(network_port, "activate",
      G_CALLBACK(connect_callback), NULL);
  gtk_table_attach(GTK_TABLE(table), network_port, 1, 2, 1, 2,
		   GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

  label = g_object_new(GTK_TYPE_LABEL,
		       "use-underline", TRUE,
		       "mnemonic-widget", network_port,
		       "label", _("_Port:"),
		       "xalign", 0.0,
		       "yalign", 0.5,
		       NULL);
  network_port_label = label;
  gtk_table_attach(GTK_TABLE(table), label, 0, 1, 1, 2,
		   GTK_FILL, GTK_FILL, 0, 0);

  network_login = gtk_entry_new();
  g_signal_connect(network_login, "activate",
      G_CALLBACK(connect_callback), NULL);
  gtk_table_attach(GTK_TABLE(table), network_login, 1, 2, 3, 4,
		   GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

  label = g_object_new(GTK_TYPE_LABEL,
		       "use-underline", TRUE,
		       "mnemonic-widget", network_login,
		       "label", _("_Login:"),
		       "xalign", 0.0,
		       "yalign", 0.5,
		       NULL);
  network_login_label = label;
  gtk_table_attach(GTK_TABLE(table), label, 0, 1, 3, 4,
		   GTK_FILL, GTK_FILL, 0, 0);

  network_password = gtk_entry_new();
  g_signal_connect(network_password, "activate",
      G_CALLBACK(connect_callback), NULL);
  gtk_entry_set_visibility(GTK_ENTRY(network_password), FALSE);
  gtk_table_attach(GTK_TABLE(table), network_password, 1, 2, 4, 5,
		   GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

  label = g_object_new(GTK_TYPE_LABEL,
		       "use-underline", TRUE,
		       "mnemonic-widget", network_password,
		       "label", _("Pass_word:"),
		       "xalign", 0.0,
		       "yalign", 0.5,
		       NULL);
  network_password_label = label;
  gtk_table_attach(GTK_TABLE(table), label, 0, 1, 4, 5,
		   GTK_FILL, GTK_FILL, 0, 0);

  network_confirm_password = gtk_entry_new();
  g_signal_connect(network_confirm_password, "activate",
      G_CALLBACK(connect_callback), NULL);
  gtk_entry_set_visibility(GTK_ENTRY(network_confirm_password), FALSE);
  gtk_table_attach(GTK_TABLE(table), network_confirm_password, 1, 2, 5, 6,
		   GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

  label = g_object_new(GTK_TYPE_LABEL,
		       "use-underline", TRUE,
		       "mnemonic-widget", network_confirm_password,
		       "label", _("Conf_irm Password:"),
		       "xalign", 0.0,
		       "yalign", 0.5,
		       NULL);
  network_confirm_password_label = label;
  gtk_table_attach(GTK_TABLE(table), label, 0, 1, 5, 6,
		   GTK_FILL, GTK_FILL, 0, 0);

  /* Server player list. */
  store = gtk_list_store_new(4, G_TYPE_STRING,
                             G_TYPE_STRING,
                             G_TYPE_STRING,
                             G_TYPE_STRING);
  server_playerlist_store = store;

  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  add_treeview_column(view, _("Name"), G_TYPE_STRING, 0);
  add_treeview_column(view, _("Type"), G_TYPE_STRING, 1);
  add_treeview_column(view, _("Host"), G_TYPE_STRING, 2);
  add_treeview_column(view, _("Nation"), G_TYPE_STRING, 3);
  server_playerlist_view = view;

  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
				      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(sw), view);
  gtk_box_pack_start(GTK_BOX(hbox), sw, TRUE, TRUE, 0);


  bbox = gtk_hbutton_box_new();
  gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_END);
  gtk_box_set_spacing(GTK_BOX(bbox), 12);
  gtk_box_pack_start(GTK_BOX(sbox), bbox, FALSE, FALSE, 2);

  button = gtk_button_new_from_stock(GTK_STOCK_REFRESH);
  gtk_container_add(GTK_CONTAINER(bbox), button);
  gtk_button_box_set_child_secondary(GTK_BUTTON_BOX(bbox), button, TRUE);
  g_signal_connect(button, "clicked",
      G_CALLBACK(update_network_lists), NULL);

  button = gtk_button_new_from_stock(GTK_STOCK_CANCEL);
  gtk_container_add(GTK_CONTAINER(bbox), button);
  g_signal_connect(button, "clicked",
      G_CALLBACK(main_callback), NULL);

  button = gtk_button_new_with_mnemonic(_("C_onnect"));
  gtk_container_add(GTK_CONTAINER(bbox), button);
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
static GtkWidget *start_aifill_spin;


/* NB: Must match creation arugments in connection_list_store_new(). */
enum connection_list_columns {
  CL_COL_PLAYER_NUMBER = 0,
  CL_COL_USER_NAME,
  CL_COL_READY_STATE,
  CL_COL_PLAYER_NAME,
  CL_COL_FLAG,
  CL_COL_NATION,
  CL_COL_TEAM,
  CL_COL_GGZ_RECORD,
  CL_COL_GGZ_RATING,
  CL_COL_CONN_ID,
  CL_COL_STYLE,
  CL_COL_WEIGHT,
  CL_COL_COLLAPSED,

  CL_NUM_COLUMNS
};

/****************************************************************************
  Create a new tree store for connection list.
****************************************************************************/
static inline GtkTreeStore *connection_list_store_new(void)
{
  return gtk_tree_store_new(CL_NUM_COLUMNS,
                            G_TYPE_INT,         /* CL_COL_PLAYER_NUMBER */
                            G_TYPE_STRING,      /* CL_COL_USER_NAME */
                            G_TYPE_BOOLEAN,     /* CL_COL_READY_STATE */
                            G_TYPE_STRING,      /* CL_COL_PLAYER_NAME */
                            GDK_TYPE_PIXBUF,    /* CL_COL_FLAG */
                            G_TYPE_STRING,      /* CL_COL_NATION */
                            G_TYPE_STRING,      /* CL_COL_TEAM */
                            G_TYPE_STRING,      /* CL_COL_GGZ_RECORD */
                            G_TYPE_STRING,      /* CL_COL_GGZ_RATING */
                            G_TYPE_INT,         /* CL_COL_CONN_ID */
                            G_TYPE_INT,         /* CL_COL_STYLE */
                            G_TYPE_INT,         /* CL_COL_WEIGHT */
                            G_TYPE_BOOLEAN);    /* CL_COL_COLLAPSED */
}

/****************************************************************************
  Maybe toggle AI of the player if the client could take the player. This
  function shouldn't be used directly, see in client_take_player().
****************************************************************************/
static void client_aitoggle_player(void *data)
{
  struct player *pplayer = player_by_number(FC_PTR_TO_INT(data));

  if (NULL != pplayer
      && pplayer == client_player()
      && pplayer->ai_controlled) {
    send_chat("/away");
  }
}

/****************************************************************************
  Send the /take command by chat and toggle AI if needed (after that).
****************************************************************************/
static void client_take_player(struct player *pplayer)
{
  int request_id = send_chat_printf("/take \"%s\"", player_name(pplayer));
  void *data = FC_INT_TO_PTR(player_number(pplayer));

  update_queue_connect_processing_finished(request_id,
                                           client_aitoggle_player, data);
}

/****************************************************************************
  Connect the object to the player and the connection.
****************************************************************************/
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

/****************************************************************************
  Extract the player and the connection set with object_put(). Returns TRUE
  if at least one of them isn't NULL.
****************************************************************************/
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

/**************************************************************************
  request the game options dialog.
**************************************************************************/
static void game_options_callback(GtkWidget *w, gpointer data)
{
  option_dialog_popup(_("Game Settings"), server_optset);
}

/**************************************************************************
  AI skill setting callback.
**************************************************************************/
static void ai_skill_callback(GtkWidget *w, gpointer data)
{
  enum ai_level level;
  enum ai_level *levels = (enum ai_level *)data;
  const char *name;
  int i;

  i = gtk_combo_box_get_active(GTK_COMBO_BOX(w));

  if (i == -1) {
    level = AI_LEVEL_DEFAULT;
  } else {
    level = levels[i];
  }

  name = ai_level_cmd(level);

  send_chat_printf("/%s", name);
}

/* HACK: sometimes when creating the ruleset combo the value is set without
 * the user's control.  In this case we don't want to do a /read. */
static bool no_ruleset_callback = FALSE;

/**************************************************************************
  Ruleset name has been given
**************************************************************************/
static void ruleset_selected(const char *name)
{
  if (name && name[0] != '\0' && !no_ruleset_callback) {
    set_ruleset(name);
  }
}

/**************************************************************************
  Ruleset selection callback. Note that this gets also called when ever
  user types to entry box. In that case we don't want to set_ruleset()
  after each letter.
**************************************************************************/
static void ruleset_entry_changed(GtkWidget *w, gpointer data)
{
  const char *name = NULL;

  if (gtk_combo_box_get_active(GTK_COMBO_BOX(ruleset_combo)) != -1) {
    name = 
      gtk_entry_get_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(ruleset_combo))));
  }

  ruleset_selected(name);
}

/**************************************************************************
  Ruleset name has been typed in completely
**************************************************************************/
static void ruleset_enter(GtkWidget *w, gpointer data)
{
  const char *name = NULL;

  name =
      gtk_entry_get_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(ruleset_combo))));

  ruleset_selected(name);
}

/**************************************************************************
  AI fill setting callback.
**************************************************************************/
static bool send_new_aifill_to_server = TRUE;
static void ai_fill_callback(GtkWidget *w, gpointer data)
{
  if (send_new_aifill_to_server) {
    send_chat_printf("/set aifill %d",
                     gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w)));
  }
}

/**************************************************************************
  Update the start page.
**************************************************************************/
void update_start_page(void)
{
  bool old = send_new_aifill_to_server;

  send_new_aifill_to_server = FALSE;
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(start_aifill_spin),
                            game.info.aifill);
  send_new_aifill_to_server = old;
  conn_list_dialog_update();
}

/****************************************************************************
  Callback for when a team is chosen from the conn menu.
****************************************************************************/
static void conn_menu_team_chosen(GObject *object, gpointer data)
{
  struct player *pplayer;
  struct team_slot *tslot = data;

  if (object_extract(object, &pplayer, NULL)
      && NULL != tslot
      && team_slot_index(tslot) != team_number(pplayer->team)) {
    send_chat_printf("/team \"%s\" \"%s\"",
                     player_name(pplayer),
                     team_slot_rule_name(tslot));
  }
}

/****************************************************************************
  Callback for when the "ready" entry is chosen from the conn menu.
****************************************************************************/
static void conn_menu_ready_chosen(GObject *object, gpointer data)
{
  struct player *pplayer;

  if (object_extract(object, &pplayer, NULL)) {
    dsend_packet_player_ready(&client.conn,
                              player_number(pplayer), !pplayer->is_ready);
  }
}

/****************************************************************************
  Callback for when the pick-nation entry is chosen from the conn menu.
****************************************************************************/
static void conn_menu_nation_chosen(GObject *object, gpointer data)
{
  struct player *pplayer;

  if (object_extract(object, &pplayer, NULL)) {
    popup_races_dialog(pplayer);
  }
}

/****************************************************************************
  Miscellaneous callback for the conn menu that allows an arbitrary command
  (/observe, /remove, /hard, etc.) to be run on the player.
****************************************************************************/
static void conn_menu_player_command(GObject *object, gpointer data)
{
  struct player *pplayer;

  if (object_extract(object, &pplayer, NULL)) {
    send_chat_printf("/%s \"%s\"",
                     (char *) g_object_get_data(G_OBJECT(data), "command"),
                     player_name(pplayer));
  }
}

/****************************************************************************
  Take command in the conn menu.
****************************************************************************/
static void conn_menu_player_take(GObject *object, gpointer data)
{
  struct player *pplayer;

  if (object_extract(object, &pplayer, NULL)) {
    client_take_player(pplayer);
  }
}

/****************************************************************************
  Miscellaneous callback for the conn menu that allows an arbitrary command
  (/cmdlevel, /cut, etc.) to be run on the connection.
****************************************************************************/
static void conn_menu_connection_command(GObject *object, gpointer data)
{
  struct connection *pconn;

  if (object_extract(object, NULL, &pconn)) {
    send_chat_printf("/%s \"%s\"",
                     (char *) g_object_get_data(G_OBJECT(data), "command"),
                     pconn->username);
  }
}

/**************************************************************************
 Show details about a user in the Connected Users dialog in a popup.
**************************************************************************/
static void show_conn_popup(struct player *pplayer, struct connection *pconn)
{
  GtkWidget *popup;
  char buf[1024] = "";

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
  g_signal_connect(popup, "response", G_CALLBACK(gtk_widget_destroy), NULL);
  gtk_window_present(GTK_WINDOW(popup));
}

/****************************************************************************
  Callback for when the "info" entry is chosen from the conn menu.
****************************************************************************/
static void conn_menu_info_chosen(GObject *object, gpointer data)
{
  struct player *pplayer;
  struct connection *pconn;

  if (object_extract(object, &pplayer, &pconn)) {
    show_conn_popup(pplayer, pconn);
  }
}

/****************************************************************************
  Called when you click on a player; this function pops up a menu
  to allow changing the team.
****************************************************************************/
static GtkWidget *create_conn_menu(struct player *pplayer,
                                   struct connection *pconn)
{
  GtkWidget *menu;
  GtkWidget *item;
  char buf[128];

  menu = gtk_menu_new();
  object_put(G_OBJECT(menu), pplayer, pconn);

  fc_snprintf(buf, sizeof(buf), _("%s info"),
              pconn ? pconn->username : player_name(pplayer));
  item = gtk_menu_item_new_with_label(buf);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
  g_signal_connect_swapped(item, "activate",
                           G_CALLBACK(conn_menu_info_chosen), menu);

  if (NULL != pplayer) {
    item = gtk_menu_item_new_with_label(_("Toggle player ready"));
    gtk_widget_set_sensitive(item, !pplayer->ai_controlled);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    g_signal_connect_swapped(item, "activate",
                             G_CALLBACK(conn_menu_ready_chosen), menu);

    item = gtk_menu_item_new_with_label(_("Pick nation"));
    gtk_widget_set_sensitive(item,
                             can_conn_edit_players_nation(&client.conn,
                                                          pplayer));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    g_signal_connect_swapped(item, "activate",
                             G_CALLBACK(conn_menu_nation_chosen), menu);

    item = gtk_menu_item_new_with_label(_("Observe this player"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    g_object_set_data_full(G_OBJECT(item), "command", g_strdup("observe"),
                           (GDestroyNotify) g_free);
    g_signal_connect_swapped(item, "activate",
                             G_CALLBACK(conn_menu_player_command), menu);

    item = gtk_menu_item_new_with_label(_("Take this player"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    g_signal_connect_swapped(item, "activate",
                             G_CALLBACK(conn_menu_player_take), menu);
  }

  if (ALLOW_CTRL <= client.conn.access_level && NULL != pconn
      && (pconn->id != client.conn.id || NULL != pplayer)) {
    item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    if (pconn->id != client.conn.id) {
      item = gtk_menu_item_new_with_label(_("Cut connection"));
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
      g_object_set_data_full(G_OBJECT(item), "command", g_strdup("cut"),
                             (GDestroyNotify) g_free);
      g_signal_connect_swapped(item, "activate",
                               G_CALLBACK(conn_menu_connection_command),
                               menu);
    }
  }

  if (ALLOW_CTRL <= client.conn.access_level && NULL != pplayer) {
    item = gtk_menu_item_new_with_label(_("Aitoggle player"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    g_object_set_data_full(G_OBJECT(item), "command", g_strdup("aitoggle"),
                           (GDestroyNotify) g_free);
    g_signal_connect_swapped(item, "activate",
                             G_CALLBACK(conn_menu_player_command), menu);

    if (pplayer != client.conn.playing && game.info.is_new_game) {
      item = gtk_menu_item_new_with_label(_("Remove player"));
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
      g_object_set_data_full(G_OBJECT(item), "command", g_strdup("remove"),
                             (GDestroyNotify) g_free);
      g_signal_connect_swapped(item, "activate",
                               G_CALLBACK(conn_menu_player_command), menu);
    }
  }

  if (ALLOW_ADMIN <= client.conn.access_level && NULL != pconn
      && pconn->id != client.conn.id) {
    enum cmdlevel level;

    /* No item for hack access; that would be a serious security hole. */
    for (level = cmdlevel_min(); level < client.conn.access_level; level++) {
      /* TRANS: Give access level to a connection. */
      fc_snprintf(buf, sizeof(buf), _("Give %s access"),
                  cmdlevel_name(level));
      item = gtk_menu_item_new_with_label(buf);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
      g_object_set_data_full(G_OBJECT(item), "command",
                             g_strdup_printf("cmdlevel %s",
                                             cmdlevel_name(level)),
                             (GDestroyNotify) g_free);
      g_signal_connect_swapped(item, "activate",
                               G_CALLBACK(conn_menu_connection_command),
                               menu);
    }
  }

  if (ALLOW_CTRL <= client.conn.access_level
      && NULL != pplayer && pplayer->ai_controlled) {
    enum ai_level level;

    item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    for (level = 0; level < AI_LEVEL_LAST; level++) {
      if (is_settable_ai_level(level)) {
        const char *level_name = ai_level_name(level);
        const char *level_cmd = ai_level_cmd(level);

        item = gtk_menu_item_new_with_label(level_name);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        g_object_set_data_full(G_OBJECT(item), "command",
                               g_strdup(level_cmd), (GDestroyNotify) g_free);
        g_signal_connect_swapped(item, "activate",
                                 G_CALLBACK(conn_menu_player_command), menu);
      }
    }
  }

  if (pplayer && game.info.is_new_game) {
    const int count = pplayer->team
                      ? player_list_size(team_members(pplayer->team)) : 0;
    bool need_empty_team = (count != 1);

    item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    /* Can't use team_iterate here since it skips empty teams. */
    team_slots_iterate(tslot) {
      if (!team_slot_is_used(tslot)) {
        if (!need_empty_team) {
          continue;
        }
        need_empty_team = FALSE;
      }

      /* TRANS: e.g., "Put on Team 5" */
      fc_snprintf(buf, sizeof(buf), _("Put on %s"),
                  team_slot_name_translation(tslot));
      item = gtk_menu_item_new_with_label(buf);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
      object_put(G_OBJECT(item), pplayer, NULL);
      g_signal_connect(item, "activate", G_CALLBACK(conn_menu_team_chosen),
                       tslot);
    } team_slots_iterate_end;
  }

  gtk_widget_show_all(menu);

  return menu;
}

/****************************************************************************
  Unselect a tree path.
****************************************************************************/
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

/****************************************************************************
  Called on a button event on the pregame player list.
****************************************************************************/
static gboolean connection_list_event(GtkWidget *widget,
                                      GdkEventButton *event,
                                      gpointer data)
{
  GtkTreeView *tree = GTK_TREE_VIEW(widget);
  GtkTreePath *path = NULL;
  GtkTreeSelection *selection = gtk_tree_view_get_selection(tree);
  gboolean ret = FALSE;

  if ((1 != event->button && 3 != event->button)
      || GDK_BUTTON_PRESS != event->type
      || !gtk_tree_view_get_path_at_pos(tree,
                                        event->x, event->y,
                                        &path, NULL, NULL, NULL)) {
    return FALSE;
  }

  if (1 == event->button) {
    if (gtk_tree_selection_path_is_selected(selection, path)) {
      /* Need to delay to avoid problem with the expander. */
      g_idle_add(delayed_unselect_path, path);
      return FALSE;     /* Return now, don't free the path. */
    }
  } else if (3 == event->button) {
    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    GtkTreeIter iter;
    GtkWidget *menu;
    int player_no, conn_id;
    struct player *pplayer;
    struct connection *pconn;

    if (!gtk_tree_selection_path_is_selected(selection, path)) {
      gtk_tree_selection_select_path(selection, path);
    }
    gtk_tree_model_get_iter(model, &iter, path);

    gtk_tree_model_get(model, &iter, CL_COL_PLAYER_NUMBER, &player_no, -1);
    pplayer = player_by_number(player_no);

    gtk_tree_model_get(model, &iter, CL_COL_CONN_ID, &conn_id, -1);
    pconn = conn_by_number(conn_id);

    menu = create_conn_menu(pplayer, pconn);
    gtk_menu_popup(GTK_MENU(menu), NULL, NULL,
                   NULL, NULL, event->button, event->time);
    ret = TRUE;
  }

  gtk_tree_path_free(path);
  return ret;
}

/****************************************************************************
  Mark a row as collapsed or expanded.
****************************************************************************/
static void connection_list_row_callback(GtkTreeView *tree_view,
                                         GtkTreeIter *iter,
                                         GtkTreePath *path,
                                         gpointer data)
{
  GtkTreeStore *store = GTK_TREE_STORE(gtk_tree_view_get_model(tree_view));

  gtk_tree_store_set(store, iter,
                     CL_COL_COLLAPSED, GPOINTER_TO_INT(data), -1);
}

/****************************************************************************
  Returns TRUE if a row is selected in the connection/player list. Fills
  the not null data.
****************************************************************************/
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

/****************************************************************************
  Returns TRUE if a row is selected in the connection/player list. Fills
  the not null data.
****************************************************************************/
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

/**************************************************************************
  'ready_button' clicked callback.
**************************************************************************/
static void ready_button_callback(GtkWidget *w, gpointer data)
{
  if (can_client_control()) {
    dsend_packet_player_ready(&client.conn,
                              player_number(client_player()),
                              !client_player()->is_ready);
  }
}

/**************************************************************************
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

/**************************************************************************
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

/****************************************************************************
  Update the buttons of the start page.
****************************************************************************/
static void update_start_page_buttons(void)
{
  char buf[2 * MAX_LEN_NAME];
  const char *text;
  struct player *selected_plr;
  bool row_selected = conn_list_selection(&selected_plr, NULL);
  bool sensitive;

  /*** Ready button. ***/
  if (can_client_control()) {
    sensitive = TRUE;
    if (client_player()->is_ready) {
      text = _("Not _ready");
    } else {
      int num_unready = 0;

      players_iterate(pplayer) {
        if (!pplayer->ai_controlled && !pplayer->is_ready) {
          num_unready++;
        }
      } players_iterate_end;

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
    sensitive = FALSE;
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

/****************************************************************************
  Search a player iterator in the model. Begin the iteration at 'start' or
  at the start of the model if 'start' is set to NULL.
****************************************************************************/
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

/****************************************************************************
  Search a connection iterator in the model. Begin the iteration at 'start'
  or at the start of the model if 'start' is set to NULL.
****************************************************************************/
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

/****************************************************************************
  Update the connected users list at pregame state.
****************************************************************************/
void real_conn_list_dialog_update(void)
{
  if (connection_list_view != NULL) {
    GObject *view;
    GtkTreeViewColumn *col;
    bool visible;

    view = G_OBJECT(connection_list_view);
    visible = (with_ggz || in_ggz);

    col = g_object_get_data(view, "record_col");
    if (col != NULL) {
      gtk_tree_view_column_set_visible(col, visible);
    }
    col = g_object_get_data(view, "rating_col");
    if (col != NULL) {
      gtk_tree_view_column_set_visible(col, visible);
    }
  }

  if (client_state() == C_S_PREPARING
      && get_client_page() == PAGE_START
      && connection_list_store != NULL) {
    GtkTreeStore *store = connection_list_store;
    GtkTreeModel *model = GTK_TREE_MODEL(store);
    GtkTreePath *path;
    GtkTreeIter child, prev_child, *pprev_child;
    GtkTreeIter parent, prev_parent, *pprev_parent = NULL;
    GdkPixbuf *pixbuf;
    gboolean collapsed;
    struct player *pselected_player;
    struct connection *pselected_conn;
    bool is_ready;
    const char *nation, *plr_name, *team;
    char user_name[MAX_LEN_NAME + 8], rating_text[128], record_text[128];
    int rating, wins, losses, ties, forfeits;
    enum cmdlevel access_level;
    int conn_id;

    /* Save the selected connection. */
    (void) conn_list_selection(&pselected_player, &pselected_conn);

    /* Insert players into the connection list. */
    players_iterate(pplayer) {
      conn_id = -1;
      access_level = ALLOW_NONE;
      pixbuf = pplayer->nation ? get_flag(pplayer->nation) : NULL;

      conn_list_iterate(pplayer->connections, pconn) {
        if (pconn->playing == pplayer && !pconn->observer) {
          conn_id = pconn->id;
          access_level = pconn->access_level;
          break;
        }
      } conn_list_iterate_end;

      if (pplayer->ai_controlled && !pplayer->was_created
          && !pplayer->is_connected) {
        /* TRANS: "<Novice AI>" */
        fc_snprintf(user_name, sizeof(user_name), _("<%s AI>"),
                    ai_level_name(pplayer->ai_common.skill_level));
      } else {
        sz_strlcpy(user_name, pplayer->username);
        if (access_level > ALLOW_BASIC) {
          sz_strlcat(user_name, "*");
        }
      }

      is_ready = pplayer->ai_controlled ? TRUE : pplayer->is_ready;

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

      rating_text[0] = '\0';
      if ((in_ggz || with_ggz)
          && !pplayer->ai_controlled
          && user_get_rating(pplayer->username, &rating)) {
        fc_snprintf(rating_text, sizeof(rating_text), "%d", rating);
      }

      record_text[0] = '\0';
      if ((in_ggz || with_ggz)
          && !pplayer->ai_controlled
          && user_get_record(pplayer->username,
                             &wins, &losses, &ties, &forfeits)) {
        if (forfeits == 0 && ties == 0) {
          fc_snprintf(record_text, sizeof(record_text), "%d-%d",
                      wins, losses);
        } else if (forfeits == 0) {
          fc_snprintf(record_text, sizeof(record_text), "%d-%d-%d",
                      wins, losses, ties);
        } else {
          fc_snprintf(record_text, sizeof(record_text), "%d-%d-%d-%d",
                      wins, losses, ties, forfeits);
        }
      }

      if (model_get_player_iter(model, &parent, pprev_parent, pplayer)) {
        gtk_tree_store_move_after(store, &parent, pprev_parent);
      } else {
        gtk_tree_store_insert_after(store, &parent, NULL, pprev_parent);
      }

      gtk_tree_store_set(store, &parent,
                         CL_COL_PLAYER_NUMBER, player_number(pplayer),
                         CL_COL_USER_NAME, user_name,
                         CL_COL_READY_STATE, is_ready,
                         CL_COL_PLAYER_NAME, plr_name,
                         CL_COL_FLAG, pixbuf,
                         CL_COL_NATION, nation,
                         CL_COL_TEAM, team,
                         CL_COL_GGZ_RECORD, record_text,
                         CL_COL_GGZ_RATING, rating_text,
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
      if (pixbuf) {
        g_object_unref(pixbuf);
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

/**************************************************************************
  Helper function for adding columns to a tree view. If 'key' is not NULL
  then the added column is added to the object data of the treeview using
  g_object_set_data under 'key'.
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

/**************************************************************************
  create start page.
**************************************************************************/
GtkWidget *create_start_page(void)
{
  GtkWidget *box, *sbox, *table, *align, *vbox;
  GtkWidget *view, *sw, *text, *toolkit_view, *button, *spin, *ai_lvl_combobox;
  GtkWidget *label;
  GtkWidget *rs_entry;
  GtkTreeSelection *selection;
  enum ai_level level;
  /* There's less than AI_LEVEL_LAST entries as not all levels have
     entries (that's the whole point of this array: index != value),
     but this is set safely to the max */
  static enum ai_level levels[AI_LEVEL_LAST];
  int i = 0;

  box = gtk_vbox_new(FALSE, 8);
  gtk_container_set_border_width(GTK_CONTAINER(box), 4);

  sbox = gtk_hbox_new(FALSE, 12);
  gtk_box_pack_start(GTK_BOX(box), sbox, FALSE, FALSE, 0);

  align = gtk_alignment_new(0.5, 0.5, 0.0, 0.0);
  gtk_container_set_border_width(GTK_CONTAINER(align), 12);
  gtk_box_pack_start(GTK_BOX(sbox), align, FALSE, FALSE, 0);

  vbox = gtk_vbox_new(FALSE, 2);
  gtk_container_add(GTK_CONTAINER(align), vbox);

  table = gtk_table_new(2, 3, FALSE);
  start_options_table = table;
  gtk_table_set_row_spacings(GTK_TABLE(table), 2);
  gtk_table_set_col_spacings(GTK_TABLE(table), 12);
  gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, FALSE, 0);

  spin = gtk_spin_button_new_with_range(1, MAX_NUM_PLAYERS, 1);
  start_aifill_spin = spin;
  gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 0);
  gtk_spin_button_set_update_policy(GTK_SPIN_BUTTON(spin), 
                                    GTK_UPDATE_IF_VALID);
  g_signal_connect_after(spin, "value_changed",
                         G_CALLBACK(ai_fill_callback), NULL);

  gtk_table_attach_defaults(GTK_TABLE(table), spin, 1, 2, 0, 1);

  label = g_object_new(GTK_TYPE_LABEL,
		       "use-underline", TRUE,
		       "mnemonic-widget", spin,
                       "label", _("Number of _Players (including AI):"),
                       "xalign", 0.0,
                       "yalign", 0.5,
                       NULL);
  gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 0, 1);

  ai_lvl_combobox = gtk_combo_box_text_new();

  for (level = 0; level < AI_LEVEL_LAST; level++) {
    if (is_settable_ai_level(level)) {
      const char *level_name = ai_level_name(level);

      gtk_combo_box_text_insert_text(GTK_COMBO_BOX_TEXT(ai_lvl_combobox), i, level_name);
      levels[i] = level;
      i++;
    }
  }
  gtk_combo_box_set_active(GTK_COMBO_BOX(ai_lvl_combobox), 0);
  g_signal_connect(ai_lvl_combobox, "changed",
                   G_CALLBACK(ai_skill_callback), levels);

  gtk_table_attach_defaults(GTK_TABLE(table), ai_lvl_combobox, 1, 2, 1, 2);

  label = g_object_new(GTK_TYPE_LABEL,
		       "use-underline", TRUE,
		       "mnemonic-widget", ai_lvl_combobox,
                       "label", _("AI Skill _Level:"),
                       "xalign", 0.0,
                       "yalign", 0.5,
                       NULL);
  gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 1, 2);

  {
    GtkListStore *model = gtk_list_store_new(1, G_TYPE_STRING);
    ruleset_combo = gtk_combo_box_new_with_model_and_entry(GTK_TREE_MODEL(model));
    gtk_combo_box_set_entry_text_column(GTK_COMBO_BOX(ruleset_combo), 0);
    g_object_unref(G_OBJECT(model));
  }

  g_signal_connect(G_OBJECT(ruleset_combo), "changed",
                   G_CALLBACK(ruleset_entry_changed), NULL);
  
  rs_entry = gtk_bin_get_child(GTK_BIN(ruleset_combo));
  g_signal_connect(GTK_ENTRY(rs_entry), "activate",
                   G_CALLBACK(ruleset_enter),
                   NULL);

  gtk_table_attach_defaults(GTK_TABLE(table), ruleset_combo, 1, 2, 2, 3);

  label = g_object_new(GTK_TYPE_LABEL,
		       "use-underline", TRUE,
		       "mnemonic-widget", GTK_COMBO_BOX(ruleset_combo),
                       "label", _("Ruleset _Version:"),
                       "xalign", 0.0,
                       "yalign", 0.5,
                       NULL);
  gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 2, 3);

  align = gtk_alignment_new(0.5, 0.5, 0.0, 0.0);
  button = gtk_stockbutton_new(GTK_STOCK_PREFERENCES,
      _("_More Game Options..."));
  g_signal_connect(button, "clicked",
      G_CALLBACK(game_options_callback), NULL);
  gtk_container_add(GTK_CONTAINER(align), button);
  gtk_box_pack_start(GTK_BOX(vbox), align, FALSE, FALSE, 8);

  connection_list_store = connection_list_store_new();
  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(connection_list_store));
  g_object_unref(G_OBJECT(connection_list_store));
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), TRUE);
  connection_list_view = GTK_TREE_VIEW(view);

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
  g_signal_connect(selection, "changed",
                   G_CALLBACK(update_start_page_buttons), NULL);

  add_tree_col(view, G_TYPE_STRING, _("Name"),
               CL_COL_USER_NAME, NULL);
  add_tree_col(view, G_TYPE_STRING, _("Record"),
               CL_COL_GGZ_RECORD, "record_col");
  add_tree_col(view, G_TYPE_STRING, _("Rating"),
               CL_COL_GGZ_RATING, "rating_col");
  add_tree_col(view, G_TYPE_BOOLEAN, _("Ready"),
               CL_COL_READY_STATE, NULL);
  add_tree_col(view, G_TYPE_STRING, Q_("?player:Leader"),
               CL_COL_PLAYER_NAME, NULL);
  add_tree_col(view, GDK_TYPE_PIXBUF, _("Flag"),
               CL_COL_FLAG, NULL);
  add_tree_col(view, G_TYPE_STRING, _("Nation"),
               CL_COL_NATION, NULL);
  add_tree_col(view, G_TYPE_STRING, _("Team"),
               CL_COL_TEAM, NULL);

  g_signal_connect(view, "button-press-event",
                   G_CALLBACK(connection_list_event), NULL);
  g_signal_connect(view, "row-collapsed",
                   G_CALLBACK(connection_list_row_callback),
                   GINT_TO_POINTER(TRUE));
  g_signal_connect(view, "row-expanded",
                   G_CALLBACK(connection_list_row_callback),
                   GINT_TO_POINTER(FALSE));

  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
				      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_ALWAYS);
  gtk_widget_set_size_request(sw, -1, 200);
  gtk_container_add(GTK_CONTAINER(sw), view);
  gtk_box_pack_start(GTK_BOX(sbox), sw, TRUE, TRUE, 0);


  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
				      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_ALWAYS);
  gtk_box_pack_start(GTK_BOX(box), sw, TRUE, TRUE, 0);

  text = gtk_text_view_new_with_buffer(message_buffer);
  start_message_area = text;
  gtk_widget_set_name(text, "chatline");
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD);
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text), 5);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
  gtk_container_add(GTK_CONTAINER(sw), text);


  /* Vote widgets. */
  if (pregame_votebar == NULL) {
    pregame_votebar = voteinfo_bar_new(TRUE);
  }
  gtk_box_pack_start(GTK_BOX(box), pregame_votebar, FALSE, FALSE, 0);
  

  toolkit_view = inputline_toolkit_view_new();
  gtk_box_pack_start(GTK_BOX(box), toolkit_view, FALSE, FALSE, 0);

  button = gtk_button_new_from_stock(GTK_STOCK_CANCEL);
  inputline_toolkit_view_append_button(toolkit_view, button);
  g_signal_connect(button, "clicked", G_CALLBACK(main_callback), NULL);

  nation_button = gtk_stockbutton_new(GTK_STOCK_PROPERTIES,
                                      _("Pick _Nation"));
  inputline_toolkit_view_append_button(toolkit_view, nation_button);
  g_signal_connect(nation_button, "clicked",
                   G_CALLBACK(nation_button_callback), NULL);

  observe_button = gtk_stockbutton_new(GTK_STOCK_ZOOM_IN, _("_Observe"));
  inputline_toolkit_view_append_button(toolkit_view, observe_button);
  g_signal_connect(observe_button, "clicked",
                   G_CALLBACK(observe_button_callback), NULL);

  ready_button = gtk_stockbutton_new(GTK_STOCK_EXECUTE, _("_Ready"));
  inputline_toolkit_view_append_button(toolkit_view, ready_button);
  g_signal_connect(ready_button, "clicked",
                   G_CALLBACK(ready_button_callback), NULL);

  return box;
}


/**************************************************************************
  this regenerates the player information from a loaded game on the server.
**************************************************************************/
void handle_game_load(bool load_successful, const char *filename)
{
  if (load_successful) {
    set_client_page(PAGE_START);

    if (game.info.is_new_game) {
      /* It's pregame. Create a player and connect to him */
      send_chat("/take -");
    }
  }
}

/**************************************************************************
  Load a savegame/scenario.
**************************************************************************/
static void load_filename(const char *filename)
{
  send_chat_printf("/load %s", filename);
}

/**************************************************************************
  loads the currently selected game.
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

/**************************************************************************
  call the default GTK+ requester for saved game loading.
**************************************************************************/
static void load_browse_callback(GtkWidget *w, gpointer data)
{
  save_dialog_file_chooser_popup(_("Choose Saved Game to Load"),
                                 GTK_FILE_CHOOSER_ACTION_OPEN,
                                 load_filename);
}

/**************************************************************************
 update the load page.
**************************************************************************/
static void update_load_page(void)
{
  /* Search for user saved games. */
  struct fileinfo_list *files = fileinfolist_infix(get_save_dirs(),
                                                   ".sav", FALSE);

  save_dialog_store_update(load_store, files);
  fileinfo_list_destroy(files);
}

/**************************************************************************
  create the load page.
**************************************************************************/
GtkWidget *create_load_page(void)
{
  GtkWidget *align, *box, *sbox, *bbox;

  GtkWidget *button, *label, *view, *sw;
  GtkCellRenderer *rend;

  box = gtk_vbox_new(FALSE, 18);
  gtk_container_set_border_width(GTK_CONTAINER(box), 4);

  align = gtk_alignment_new(0.5, 0.5, 0.0, 1.0);
  gtk_box_pack_start(GTK_BOX(box), align, TRUE, TRUE, 0);

  load_store = save_dialog_store_new();
  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(load_store));
  g_object_unref(load_store);

  rend = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
      -1, NULL, rend, "text", 0, NULL);

  load_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);

  gtk_tree_selection_set_mode(load_selection, GTK_SELECTION_SINGLE);

  g_signal_connect(view, "row-activated",
                   G_CALLBACK(load_callback), NULL);
  
  sbox = gtk_vbox_new(FALSE, 2);
  gtk_container_add(GTK_CONTAINER(align), sbox);

  label = g_object_new(GTK_TYPE_LABEL,
    "use-underline", TRUE,
    "mnemonic-widget", view,
    "label", _("Choose Saved Game to _Load:"),
    "xalign", 0.0,
    "yalign", 0.5,
    NULL);
  gtk_box_pack_start(GTK_BOX(sbox), label, FALSE, FALSE, 0);

  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
				      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC,
  				 GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request(sw, 300, -1);
  gtk_container_add(GTK_CONTAINER(sw), view);
  gtk_box_pack_start(GTK_BOX(sbox), sw, TRUE, TRUE, 0);

  bbox = gtk_hbutton_box_new();
  gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_END);
  gtk_box_set_spacing(GTK_BOX(bbox), 12);
  gtk_box_pack_start(GTK_BOX(box), bbox, FALSE, FALSE, 0);

  button = gtk_button_new_with_mnemonic(_("_Browse..."));
  gtk_container_add(GTK_CONTAINER(bbox), button);
  gtk_button_box_set_child_secondary(GTK_BUTTON_BOX(bbox), button, TRUE);
  g_signal_connect(button, "clicked",
      G_CALLBACK(load_browse_callback), NULL);

  button = gtk_button_new_from_stock(GTK_STOCK_CANCEL);
  gtk_container_add(GTK_CONTAINER(bbox), button);
  g_signal_connect(button, "clicked",
      G_CALLBACK(main_callback), NULL);

  button = gtk_button_new_from_stock(GTK_STOCK_OK);
  gtk_container_add(GTK_CONTAINER(bbox), button);
  g_signal_connect(button, "clicked",
      G_CALLBACK(load_callback), NULL);

  return box;
}


/**************************************************************************
  Updates the info for the currently selected scenario.
**************************************************************************/
static void scenario_list_callback(void)
{
  GtkTreeIter it;
  GtkTextBuffer *buffer;
  char *description;
  char *filename;

  if (gtk_tree_selection_get_selected(scenario_selection, NULL, &it)) {
    gtk_tree_model_get(GTK_TREE_MODEL(scenario_store), &it,
		       2, &description, -1);
    gtk_tree_model_get(GTK_TREE_MODEL(scenario_store), &it,
		       1, &filename, -1);
    filename = skip_to_basename(filename);
  } else {
    description = "";
    filename = "";
  }

  buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(scenario_description));
  gtk_text_buffer_set_text(buffer, description, -1);
  gtk_label_set_text(GTK_LABEL(scenario_filename), filename);
}

/**************************************************************************
  loads the currently selected scenario.
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

/**************************************************************************
  call the default GTK+ requester for scenario loading.
**************************************************************************/
static void scenario_browse_callback(GtkWidget *w, gpointer data)
{
  save_dialog_file_chooser_popup(_("Choose a Scenario"),
                                 GTK_FILE_CHOOSER_ACTION_OPEN,
                                 load_filename);
}

/**************************************************************************
  update the scenario page.
**************************************************************************/
static void update_scenario_page(void)
{
  struct fileinfo_list *files;

  gtk_list_store_clear(scenario_store);

  /* search for scenario files. */
  files = fileinfolist_infix(get_scenario_dirs(), ".sav", TRUE);
  fileinfo_list_iterate(files, pfile) {
    GtkTreeIter it;
    struct section_file *sf;

    gtk_list_store_append(scenario_store, &it);

    if ((sf = secfile_load_section(pfile->fullname, "scenario", TRUE))) {
      const char *sname, *sdescription;

      sname = secfile_lookup_str_default(sf, NULL, "scenario.name");
      sdescription = secfile_lookup_str_default(sf, NULL,
                                                "scenario.description");
      gtk_list_store_set(scenario_store, &it,
                         0, sname && strlen(sname) ? Q_(sname) : pfile->name,
                         1, pfile->fullname,
                         2, (NULL != sdescription && '\0' != sdescription[0]
                             ? Q_(sdescription) : ""),
                         -1);
      secfile_destroy(sf);
    } else {
      log_error("Error loading '%s':\n%s", pfile->fullname, secfile_error());
      gtk_list_store_set(scenario_store, &it,
			 0, pfile->name,
			 1, pfile->fullname,
			 2, "",
			-1);
    }
  } fileinfo_list_iterate_end;

  fileinfo_list_destroy(files);
}

/**************************************************************************
  create the scenario page.
**************************************************************************/
GtkWidget *create_scenario_page(void)
{
  GtkWidget *vbox, *hbox, *sbox, *bbox, *filenamebox, *descbox;

  GtkWidget *align, *button, *label, *view, *sw, *text;
  GtkCellRenderer *rend;

  vbox = gtk_vbox_new(FALSE, 18);
  gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);

  scenario_store = gtk_list_store_new(3, G_TYPE_STRING,
					 G_TYPE_STRING,
					 G_TYPE_STRING);
  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(scenario_store));
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

  sbox = gtk_vbox_new(FALSE, 2);
  gtk_box_pack_start(GTK_BOX(vbox), sbox, TRUE, TRUE, 0);

  label = g_object_new(GTK_TYPE_LABEL,
    "use-underline", TRUE,
    "mnemonic-widget", view,
    "label", _("Choose a _Scenario:"),
    "xalign", 0.0,
    "yalign", 0.5,
    NULL);
  gtk_box_pack_start(GTK_BOX(sbox), label, FALSE, FALSE, 0);

  hbox = gtk_hbox_new(TRUE, 12);
  gtk_box_pack_start(GTK_BOX(sbox), hbox, TRUE, TRUE, 0);

  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
				      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC,
  				 GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(sw), view);
  gtk_box_pack_start(GTK_BOX(hbox), sw, TRUE, TRUE, 0);

  align = gtk_alignment_new(0.5, 0.0, 1.0, 0.5);
  gtk_box_pack_start(GTK_BOX(hbox), align, TRUE, TRUE, 0);

  text = gtk_text_view_new();
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD);
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text), 2);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
  scenario_description = text;

  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
				      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC,
  				 GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(sw), text);

  text = gtk_label_new(_("Filename:"));
  scenario_filename = gtk_label_new("");
  gtk_misc_set_alignment(GTK_MISC(scenario_filename), 0.0, 0.5);
  gtk_label_set_selectable(GTK_LABEL(scenario_filename), TRUE);

  filenamebox = gtk_hbox_new(FALSE, 12);

  gtk_box_pack_start(GTK_BOX(filenamebox), text, FALSE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(filenamebox), scenario_filename, FALSE, TRUE, 0);

  descbox = gtk_vbox_new(FALSE, 6);
  gtk_box_pack_start(GTK_BOX(descbox), sw, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(descbox), filenamebox, FALSE, FALSE, 5);
  gtk_container_add(GTK_CONTAINER(align), descbox);

  bbox = gtk_hbutton_box_new();
  gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_END);
  gtk_box_set_spacing(GTK_BOX(bbox), 12);
  gtk_box_pack_start(GTK_BOX(vbox), bbox, FALSE, FALSE, 0);

  button = gtk_button_new_with_mnemonic(_("_Browse..."));
  gtk_container_add(GTK_CONTAINER(bbox), button);
  gtk_button_box_set_child_secondary(GTK_BUTTON_BOX(bbox), button, TRUE);
  g_signal_connect(button, "clicked",
      G_CALLBACK(scenario_browse_callback), NULL);

  button = gtk_button_new_from_stock(GTK_STOCK_CANCEL);
  gtk_container_add(GTK_CONTAINER(bbox), button);
  g_signal_connect(button, "clicked",
      G_CALLBACK(main_callback), NULL);

  button = gtk_button_new_from_stock(GTK_STOCK_OK);
  gtk_container_add(GTK_CONTAINER(bbox), button);
  g_signal_connect(button, "clicked",
      G_CALLBACK(scenario_callback), NULL);

  return vbox;
}


/**************************************************************************
  Returns current client page
**************************************************************************/
enum client_pages get_current_client_page(void)
{
  return current_page;
}

/**************************************************************************
  Changes the current page.  The action is delayed.
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
  case PAGE_NETWORK:
    destroy_server_scans();
    break;
  case PAGE_GAME:
    enable_menus(FALSE);
    gtk_window_unmaximize(GTK_WINDOW(toplevel));
    break;
  default:
    break;
  }

  switch (new_page) {
  case PAGE_MAIN:
  case PAGE_GGZ:
    break;
  case PAGE_START:
    if (is_server_running()) {
      if (game.info.is_new_game) {
        gtk_widget_show(start_options_table);
      } else {
        gtk_widget_hide(start_options_table);
      }
      update_start_page();
    } else {
      gtk_widget_hide(start_options_table);
    }
    voteinfo_gui_update();
    overview_size_changed();
    conn_list_dialog_update();
    break;
  case PAGE_GAME:
    reset_unit_table();
    enable_menus(TRUE);
    gtk_window_maximize(GTK_WINDOW(toplevel));
    voteinfo_gui_update();
    mapview_freeze();
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

  /* hide/show statusbar. */
  if (new_page == PAGE_START || new_page == PAGE_GAME) {
    clear_network_statusbar();
    gtk_widget_hide(statusbar_frame);
  } else {
    gtk_widget_show(statusbar_frame);
  }

  gtk_notebook_set_current_page(GTK_NOTEBOOK(toplevel_tabs), new_page);

  /* Update the GUI. */
  while (gtk_events_pending()) {
    gtk_main_iteration();
  }

  switch (new_page) {
  case PAGE_MAIN:
    break;
  case PAGE_START:
    chatline_scroll_to_bottom(FALSE);
    inputline_grab_focus();
    break;
  case PAGE_GGZ:
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

/****************************************************************************
  Save game 'save_dialog_files_fn_t' implementation.
****************************************************************************/
static struct fileinfo_list *save_dialog_savegame_list(void)
{
  return fileinfolist_infix(get_save_dirs(), ".sav", FALSE);
}

/****************************************************************************
  Save game dialog.
****************************************************************************/
void save_game_dialog_popup(void)
{
  static GtkWidget *shell = NULL;

  if (NULL != shell) {
    return;
  }

  shell = save_dialog_new(_("Save Game"), _("Saved _Games:"),
                          _("Save _Filename:"), send_save_game,
                          save_dialog_savegame_list);
  g_signal_connect(shell, "destroy", G_CALLBACK(gtk_widget_destroyed),
                   &shell);
  gtk_window_present(GTK_WINDOW(shell));
}

/****************************************************************************
  Save scenario 'save_dialog_action_fn_t' implementation.
****************************************************************************/
static void save_dialog_save_scenario(const char *filename)
{
  dsend_packet_save_scenario(&client.conn, filename);
}

/****************************************************************************
  Save scenario 'save_dialog_files_fn_t' implementation.
****************************************************************************/
static struct fileinfo_list *save_dialog_scenario_list(void)
{
  return fileinfolist_infix(get_scenario_dirs(), ".sav", FALSE);
}

/****************************************************************************
  Save scenario dialog.
****************************************************************************/
void save_scenario_dialog_popup(void)
{
  static GtkWidget *shell = NULL;

  if (NULL != shell) {
    return;
  }

  shell = save_dialog_new(_("Save Scenario"), _("Saved Sce_narios:"),
                          _("Save Sc_enario:"), save_dialog_save_scenario,
                          save_dialog_scenario_list);
  g_signal_connect(shell, "destroy", G_CALLBACK(gtk_widget_destroyed),
                   &shell);
  gtk_window_present(GTK_WINDOW(shell));
}

/****************************************************************************
  Save mapimg 'save_dialog_files_fn_t' implementation. If possible, only the
  current directory is used. As fallback, the files in the save directories
  are listed.
****************************************************************************/
static struct fileinfo_list *save_dialog_mapimg_list(void)
{
  return fileinfolist_infix(get_save_dirs(), ".map", FALSE);
}

/****************************************************************************
  Save scenario dialog.
****************************************************************************/
void save_mapimg_dialog_popup(void)
{
  static GtkWidget *shell = NULL;

  if (NULL != shell) {
    return;
  }

  shell = save_dialog_new(_("Save Map Image"), _("Saved Map _Images:"),
                          _("Save _Map Images:"), mapimg_client_save,
                          save_dialog_mapimg_list);
  g_signal_connect(shell, "destroy", G_CALLBACK(gtk_widget_destroyed),
                   &shell);
  gtk_window_present(GTK_WINDOW(shell));
}

/****************************************************************************
  Save map image. On error popup a message window for the user.
****************************************************************************/
void mapimg_client_save(const char *filename)
{
  if (!mapimg_client_createmap(filename)) {
    char msg[512];

    fc_snprintf(msg, sizeof(msg), "(%s)", mapimg_error());
    popup_notify_dialog("Error", "Error Creating the Map Image!", msg);
  }
}


/****************************************************************************
  Set the list of available rulesets.  The default ruleset should be
  "default", and if the user changes this then set_ruleset() should be
  called.
****************************************************************************/
void gui_set_rulesets(int num_rulesets, char **rulesets)
{
  int i;
  int def_idx = -1;
  GtkListStore *model =
      GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(ruleset_combo)));

  gtk_list_store_clear(model);
  for (i = 0; i < num_rulesets; i++){
    GtkTreeIter iter; /* unused */
    gtk_list_store_insert_with_values(model, &iter, i, 0, rulesets[i], -1);
    if (!strcmp("default", rulesets[i])) {
      def_idx = i;
    }
  }

  no_ruleset_callback = TRUE;

  /* HACK: server should tell us the current ruleset. */
  gtk_combo_box_set_active(GTK_COMBO_BOX(ruleset_combo), def_idx);

  no_ruleset_callback = FALSE;
}
