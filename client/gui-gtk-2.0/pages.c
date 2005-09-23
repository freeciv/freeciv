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

#include <stdio.h>
#include <time.h>

#include <sys/stat.h>

#include <gtk/gtk.h>

#include "dataio.h"
#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "mem.h"
#include "shared.h"
#include "support.h"
#include "version.h"

#include "chatline.h"
#include "civclient.h"
#include "clinet.h"
#include "connectdlg.h"
#include "connectdlg_common.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "packhand.h"
#include "pages.h"


GtkWidget *start_message_area;
GtkListStore *conn_model;       

static GtkWidget *start_options_table;

static GtkListStore *load_store, *scenario_store,
  *nation_store, *meta_store, *lan_store; 

static GtkTreeSelection *load_selection, *scenario_selection,
  *nation_selection, *meta_selection, *lan_selection;

static enum client_pages old_page;

static void set_page_callback(GtkWidget *w, gpointer data);
static void update_nation_page(struct packet_game_load *packet);

static guint lan_timer = 0;
static int num_lanservers_timer = 0;

static GtkWidget *statusbar, *statusbar_frame;
static GQueue *statusbar_queue;
static guint statusbar_timer = 0;


/**************************************************************************
  spawn a server, if there isn't one, using the default settings.
**************************************************************************/
static void start_new_game_callback(GtkWidget *w, gpointer data)
{
  if (is_server_running() || client_start_server()) {
    char buf[512];

    /* Send new game defaults. */
    send_chat("/set aifill 5");

    my_snprintf(buf, sizeof(buf), "/%s", skill_level_names[0]);
    send_chat(buf);
  }
}

/**************************************************************************
  go to the scenario page, spawning a server,
**************************************************************************/
static void start_scenario_callback(GtkWidget *w, gpointer data)
{
  set_client_page(PAGE_SCENARIO);

  start_new_game_callback(NULL, NULL);
}

/**************************************************************************
  go to the load page, spawning a server.
**************************************************************************/
static void load_saved_game_callback(GtkWidget *w, gpointer data)
{
  set_client_page(PAGE_LOAD);

  start_new_game_callback(NULL, NULL);
}

/**************************************************************************
  cancel, by terminating the connection and going back to main page.
**************************************************************************/
static void main_callback(GtkWidget *w, gpointer data)
{
  if (aconnection.used) {
    disconnect_from_server();
  } else {
    set_client_page(PAGE_MAIN);
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
    pango_layout_set_font_description(layout, main_font);

    my_snprintf(msgbuf, sizeof(msgbuf), "%s%s",
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

/**************************************************************************
  create the main page.
**************************************************************************/
GtkWidget *create_main_page(void)
{
  GtkWidget *align, *box, *sbox, *bbox, *frame, *image;

  GtkWidget *button;
  GtkSizeGroup *size;

  size = gtk_size_group_new(GTK_SIZE_GROUP_BOTH);

  box = gtk_vbox_new(FALSE, 6);
  gtk_container_set_border_width(GTK_CONTAINER(box), 4);

  align = gtk_alignment_new(0.5, 0.0, 0.0, 0.0);
  gtk_container_set_border_width(GTK_CONTAINER(align), 18);
  gtk_box_pack_start(GTK_BOX(box), align, FALSE, FALSE, 0);

  frame = gtk_frame_new(NULL);
  gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_OUT);
  gtk_container_add(GTK_CONTAINER(align), frame);

  image = gtk_image_new_from_file(main_intro_filename);
  g_signal_connect_after(image, "expose_event",
      G_CALLBACK(intro_expose), NULL);
  gtk_container_add(GTK_CONTAINER(frame), image);

  align = gtk_alignment_new(0.5, 0.0, 0.0, 0.0);
  gtk_box_pack_start(GTK_BOX(box), align, FALSE, FALSE, 0);

  sbox = gtk_vbox_new(FALSE, 18);
  gtk_container_add(GTK_CONTAINER(align), sbox);

  bbox = gtk_vbox_new(FALSE, 6);
  gtk_container_add(GTK_CONTAINER(sbox), bbox);

  button = gtk_button_new_with_mnemonic(_("Start _New Game"));
  gtk_size_group_add_widget(size, button);
  gtk_container_add(GTK_CONTAINER(bbox), button);
  g_signal_connect(button, "clicked",
      G_CALLBACK(start_new_game_callback), NULL);

  button = gtk_button_new_with_mnemonic(_("Start _Scenario Game"));
  gtk_size_group_add_widget(size, button);
  gtk_container_add(GTK_CONTAINER(bbox), button);
  g_signal_connect(button, "clicked",
      G_CALLBACK(start_scenario_callback), NULL);

  button = gtk_button_new_with_mnemonic(_("_Load Saved Game"));
  gtk_size_group_add_widget(size, button);
  gtk_container_add(GTK_CONTAINER(bbox), button);
  g_signal_connect(button, "clicked",
      G_CALLBACK(load_saved_game_callback), NULL);

  bbox = gtk_vbox_new(FALSE, 6);
  gtk_container_add(GTK_CONTAINER(sbox), bbox);

  button = gtk_button_new_with_mnemonic(_("C_onnect to Network Game"));
  gtk_size_group_add_widget(size, button);
  gtk_container_add(GTK_CONTAINER(bbox), button);
  g_signal_connect(button, "clicked",
      G_CALLBACK(set_page_callback), GUINT_TO_POINTER(PAGE_NETWORK));

  bbox = gtk_vbox_new(FALSE, 6);
  gtk_container_add(GTK_CONTAINER(sbox), bbox);

  button = gtk_button_new_from_stock(GTK_STOCK_QUIT);
  gtk_size_group_add_widget(size, button);
  gtk_container_add(GTK_CONTAINER(bbox), button);
  g_signal_connect(button, "clicked",
      G_CALLBACK(gtk_main_quit), NULL);

#if IS_BETA_VERSION
  {
    GtkWidget *label;

    label = gtk_label_new(beta_message());
    gtk_widget_set_name(label, "beta label");
    gtk_misc_set_alignment(GTK_MISC(label), 0.5, 0.5);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    gtk_container_add(GTK_CONTAINER(sbox), label);
  }
#endif

  return box;
}


/**************************************************************************
                                 NETWORK PAGE
**************************************************************************/
static GtkWidget *network_login_label, *network_login;
static GtkWidget *network_host_label, *network_host;
static GtkWidget *network_port_label, *network_port;
static GtkWidget *network_password_label, *network_password;
static GtkWidget *network_confirm_password_label, *network_confirm_password;

/**************************************************************************
  update a server list.
**************************************************************************/
static void update_server_list(GtkTreeSelection *selection,
			       GtkListStore *store, struct server_list *list)
{
  const gchar *host, *port;

  host = gtk_entry_get_text(GTK_ENTRY(network_host));
  port = gtk_entry_get_text(GTK_ENTRY(network_port));

  gtk_list_store_clear(store);

  if (!list) {
    return;
  }

  server_list_iterate(*list, pserver) {
    GtkTreeIter it;
    gchar *row[6];

    gtk_list_store_append(store, &it);

    row[0] = pserver->host;
    row[1] = pserver->port;
    row[2] = pserver->version;
    row[3] = _(pserver->state);
    row[4] = pserver->nplayers;
    row[5] = pserver->message;

    gtk_list_store_set(store, &it,
	0, row[0], 1, row[1], 2, row[2],
	3, row[3], 4, row[4], 5, row[5], -1);

    if (strcmp(host, pserver->host) == 0 && strcmp(port, pserver->port) == 0) {
      gtk_tree_selection_select_iter(selection, &it);
    }
  } server_list_iterate_end;
}

/**************************************************************************
  get the list of servers from the metaserver.
**************************************************************************/
static bool get_meta_list(char *errbuf, int n_errbuf)
{
  struct server_list *server_list = create_server_list(errbuf, n_errbuf);

  update_server_list(meta_selection, meta_store, server_list);

  if (server_list) {
    delete_server_list(server_list);
    return TRUE;
  } else {
    return FALSE;
  }
}

/**************************************************************************
  this function frees the list of LAN servers on timeout destruction. 
**************************************************************************/
static void get_lan_destroy(gpointer data)
{
  finish_lanserver_scan();
  num_lanservers_timer = 0;
  lan_timer = 0;
}

/**************************************************************************
  this function updates the list of LAN servers every 1000 ms for 5 secs.
**************************************************************************/
static gboolean get_lan_list(gpointer data)
{
  struct server_list *server_list = get_lan_server_list();

  update_server_list(lan_selection, lan_store, server_list);
  num_lanservers_timer++;
  if (num_lanservers_timer == 5) {
    return FALSE;
  } else {
    return TRUE;
  }
}

/**************************************************************************
  update the metaserver and lan server lists.
**************************************************************************/
static void update_network_lists(void)
{
  char errbuf[128];

  if (!get_meta_list(errbuf, sizeof(errbuf)))  {
    append_output_window(errbuf);
  }

  if (lan_timer == 0) { 
    if (begin_lanserver_scan()) {
      lan_timer = g_timeout_add_full(G_PRIORITY_DEFAULT, 1000,
	  get_lan_list, NULL, get_lan_destroy);
    }
  }
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
  if (GTK_WIDGET_VISIBLE(statusbar_frame)) {
    if (force) {
      clear_network_statusbar();
      gtk_label_set_text(GTK_LABEL(statusbar), text);
    } else {
      g_queue_push_tail(statusbar_queue, mystrdup(text));
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

/**************************************************************************
 configure the dialog depending on what type of authentication request the
 server is making.
**************************************************************************/
void handle_authentication_req(enum authentication_type type, char *message)
{
  append_network_statusbar(message, TRUE);

  switch (type) {
  case AUTH_NEWUSER_FIRST:
  case AUTH_NEWUSER_RETRY:
    set_connection_state(NEW_PASSWORD_TYPE);
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
      set_connection_state(ENTER_PASSWORD_TYPE);
    }
    break;
  case AUTH_LOGIN_RETRY:
    set_connection_state(ENTER_PASSWORD_TYPE);
    break;
  default:
    assert(0);
  }
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

      append_output_window(errbuf);
    }
    break; 
  case NEW_PASSWORD_TYPE:
    if (w != network_password) {
      sz_strlcpy(password,
	  gtk_entry_get_text(GTK_ENTRY(network_password)));
      sz_strlcpy(reply.password,
	  gtk_entry_get_text(GTK_ENTRY(network_confirm_password)));
      if (strncmp(reply.password, password, MAX_LEN_NAME) == 0) {
	password[0] = '\0';
	send_packet_authentication_reply(&aconnection, &reply);

	set_connection_state(WAITING_TYPE);
      } else { 
	append_network_statusbar(_("Passwords don't match, enter password."),
	    TRUE);

	set_connection_state(NEW_PASSWORD_TYPE);
      }
    }
    break;
  case ENTER_PASSWORD_TYPE:
    sz_strlcpy(reply.password,
	gtk_entry_get_text(GTK_ENTRY(network_password)));
    send_packet_authentication_reply(&aconnection, &reply);

    set_connection_state(WAITING_TYPE);
    break;
  case WAITING_TYPE:
    break;
  default:
    assert(0);
  }
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
  sets the host, port of the selected server.
**************************************************************************/
static void network_list_callback(GtkTreeSelection *select, gpointer data)
{
  GtkTreeModel *model;
  GtkTreeIter it;
  char *host, *port;

  if (!gtk_tree_selection_get_selected(select, &model, &it)) {
    return;
  }

  /* only one server can be selected in either list. */
  if (select == meta_selection) {
    gtk_tree_selection_unselect_all(lan_selection);
  } else {
    gtk_tree_selection_unselect_all(meta_selection);
  }

  gtk_tree_model_get(model, &it, 0, &host, 1, &port, -1);

  gtk_entry_set_text(GTK_ENTRY(network_host), host);
  gtk_entry_set_text(GTK_ENTRY(network_port), port);
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
  my_snprintf(buf, sizeof(buf), "%d", server_port);
  gtk_entry_set_text(GTK_ENTRY(network_port), buf);

  set_connection_state(LOGIN_TYPE);
}

/**************************************************************************
  create the network page.
**************************************************************************/
GtkWidget *create_network_page(void)
{
  GtkWidget *salign, *box, *sbox, *bbox, *notebook;

  GtkWidget *button, *label, *view, *sw;
  GtkCellRenderer *rend;
  GtkTreeSelection *selection;

  GtkWidget *table;

  static const char *titles[] = {
    N_("Server Name"),
    N_("Port"),
    N_("Version"),
    N_("Status"),
    N_("Players"),
    N_("Comment")
  };
  static bool titles_done;

  int i;

  intl_slist(ARRAY_SIZE(titles), titles, &titles_done);

  box = gtk_vbox_new(FALSE, 0);
  gtk_container_set_border_width(GTK_CONTAINER(box), 4);

  notebook = gtk_notebook_new();
  gtk_container_add(GTK_CONTAINER(box), notebook);

  /* LAN pane. */
  lan_store = gtk_list_store_new(6, G_TYPE_STRING, G_TYPE_STRING,
      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(lan_store));
  g_object_unref(lan_store);
  gtk_tree_view_columns_autosize(GTK_TREE_VIEW(view));

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  lan_selection = selection;
  gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
  g_signal_connect(view, "focus",
      		   G_CALLBACK(gtk_true), NULL);
  g_signal_connect(view, "row_activated",
		   G_CALLBACK(network_activate_callback), NULL);
  g_signal_connect(selection, "changed",
                   G_CALLBACK(network_list_callback), NULL);

  rend = gtk_cell_renderer_text_new();
  for (i = 0; i < ARRAY_SIZE(titles); i++) {
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
	-1, titles[i], rend, "text", i, NULL);
  }

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
  meta_store = gtk_list_store_new(6, G_TYPE_STRING, G_TYPE_STRING,
      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(meta_store));
  g_object_unref(meta_store);
  gtk_tree_view_columns_autosize(GTK_TREE_VIEW(view));

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  meta_selection = selection;
  gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
  g_signal_connect(view, "focus",
      		   G_CALLBACK(gtk_true), NULL);
  g_signal_connect(view, "row_activated",
		   G_CALLBACK(network_activate_callback), NULL);
  g_signal_connect(selection, "changed",
                   G_CALLBACK(network_list_callback), NULL);

  rend = gtk_cell_renderer_text_new();
  for (i = 0; i < ARRAY_SIZE(titles); i++) {
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
	-1, titles[i], rend, "text", i, NULL);
  }

  label = gtk_label_new_with_mnemonic(_("Internet _Metaserver"));

  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_set_border_width(GTK_CONTAINER(sw), 4);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
				      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(sw), view);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), sw, label);

  /* Bottom part of the page, outside the inner notebook. */
  sbox = gtk_vbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box), sbox, FALSE, FALSE, 0);

  salign = gtk_alignment_new(0.5, 0.5, 0.0, 0.0);
  gtk_box_pack_start(GTK_BOX(sbox), salign, FALSE, FALSE, 18);

  table = gtk_table_new(6, 2, FALSE);
  gtk_table_set_row_spacings(GTK_TABLE(table), 2);
  gtk_table_set_col_spacings(GTK_TABLE(table), 12);
  gtk_table_set_row_spacing(GTK_TABLE(table), 2, 12);
  gtk_widget_set_size_request(table, 400, -1);
  gtk_container_add(GTK_CONTAINER(salign), table);

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

  button = gtk_button_new_from_stock(GTK_STOCK_OK);
  gtk_container_add(GTK_CONTAINER(bbox), button);
  g_signal_connect(button, "clicked",
      G_CALLBACK(connect_callback), NULL);

  return box;
}


/**************************************************************************
                                  START PAGE
**************************************************************************/
static GtkWidget *start_aifill_spin;

/**************************************************************************
  request the game options dialog.
**************************************************************************/
static void game_options_callback(GtkWidget *w, gpointer data)
{
  send_report_request(REPORT_SERVER_OPTIONS2);
}

/**************************************************************************
  AI skill setting callback.
**************************************************************************/
static void ai_skill_callback(GtkWidget *w, gpointer data)
{
  const char *name;
  char buf[512];

  name = skill_level_names[GPOINTER_TO_UINT(data)];

  my_snprintf(buf, sizeof(buf), "/%s", name);
  send_chat(buf);
}

/**************************************************************************
  AI fill setting callback.
**************************************************************************/
static void ai_fill_callback(GtkWidget *w, gpointer data)
{
  char buf[512];

  my_snprintf(buf, sizeof(buf), "/set aifill %d",
      gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w)));
  send_chat(buf);
}

/**************************************************************************
  start game callback.
**************************************************************************/
static void start_start_callback(GtkWidget *w, gpointer data)
{
  really_close_connection_dialog();
  send_chat("/start");
}

/**************************************************************************
  update the start page.
**************************************************************************/
static void update_start_page(void)
{
  /* Default to aifill 5. */
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(start_aifill_spin), 5);
}

/**************************************************************************
  create start page.
**************************************************************************/
GtkWidget *create_start_page(void)
{
  GtkWidget *box, *sbox, *bbox, *table, *align, *vbox;

  GtkWidget *view, *sw, *text, *entry, *button, *spin, *option;
  GtkWidget *label, *menu, *item;
  GtkCellRenderer *rend;

  int i;

  box = gtk_vbox_new(FALSE, 8);
  gtk_container_set_border_width(GTK_CONTAINER(box), 4);

  sbox = gtk_hbox_new(FALSE, 12);
  gtk_box_pack_start(GTK_BOX(box), sbox, FALSE, FALSE, 0);

  align = gtk_alignment_new(0.5, 0.5, 0.0, 0.0);
  gtk_container_set_border_width(GTK_CONTAINER(align), 12);
  gtk_box_pack_start(GTK_BOX(sbox), align, FALSE, FALSE, 0);

  vbox = gtk_vbox_new(FALSE, 2);
  gtk_container_add(GTK_CONTAINER(align), vbox);

  table = gtk_table_new(2, 2, FALSE);
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
                       "label", _("_Number of Players (including AI):"),
                       "xalign", 0.0,
                       "yalign", 0.5,
                       NULL);
  gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 0, 1);

  option = gtk_option_menu_new();

  menu = gtk_menu_new();
  for (i = 0; i < NUM_SKILL_LEVELS; i++) {
    item = gtk_menu_item_new_with_label(_(skill_level_names[i]));
    g_signal_connect(item, "activate",
	G_CALLBACK(ai_skill_callback), GUINT_TO_POINTER(i));

    gtk_widget_show(item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
  }
  gtk_option_menu_set_menu(GTK_OPTION_MENU(option), menu);
  gtk_table_attach_defaults(GTK_TABLE(table), option, 1, 2, 1, 2);

  label = g_object_new(GTK_TYPE_LABEL,
		       "use-underline", TRUE,
		       "mnemonic-widget", option,
                       "label", _("_AI Skill Level:"),
                       "xalign", 0.0,
                       "yalign", 0.5,
                       NULL);
  gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 1, 2);

  align = gtk_alignment_new(0.5, 0.5, 0.0, 0.0);
  button = gtk_stockbutton_new(GTK_STOCK_PREFERENCES,
      _("More Game _Options..."));
  g_signal_connect(button, "clicked",
      G_CALLBACK(game_options_callback), NULL);
  gtk_container_add(GTK_CONTAINER(align), button);
  gtk_box_pack_start(GTK_BOX(vbox), align, FALSE, FALSE, 8);


  conn_model = gtk_list_store_new(1, G_TYPE_STRING); 

  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(conn_model));
  g_object_unref(conn_model);
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);

  rend = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
	-1, NULL, rend, "text", 0, NULL);

  g_signal_connect(view, "button-press-event",
		   G_CALLBACK(show_conn_popup), NULL);

  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
				      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				 GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
  gtk_widget_set_size_request(sw, -1, 200);
  gtk_container_add(GTK_CONTAINER(sw), view);
  gtk_box_pack_start(GTK_BOX(sbox), sw, TRUE, TRUE, 0);


  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
				      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				 GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
  gtk_box_pack_start(GTK_BOX(box), sw, TRUE, TRUE, 0);

  text = gtk_text_view_new_with_buffer(message_buffer);
  start_message_area = text;
  gtk_widget_set_name(text, "chatline");
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD);
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text), 5);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
  gtk_container_add(GTK_CONTAINER(sw), text);


  sbox = gtk_hbox_new(FALSE, 12);
  gtk_box_pack_start(GTK_BOX(box), sbox, FALSE, FALSE, 0);

  entry = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(sbox), entry, TRUE, TRUE, 0);
  g_signal_connect(entry, "key_press_event",
      G_CALLBACK(inputline_handler), NULL);
  g_signal_connect(entry, "activate",
      G_CALLBACK(inputline_return), NULL);

  bbox = gtk_hbutton_box_new();
  gtk_box_set_spacing(GTK_BOX(bbox), 12);
  gtk_box_pack_start(GTK_BOX(sbox), bbox, FALSE, FALSE, 0);

  button = gtk_button_new_from_stock(GTK_STOCK_CANCEL);
  gtk_container_add(GTK_CONTAINER(bbox), button);
  g_signal_connect(button, "clicked",
      G_CALLBACK(main_callback), NULL);

  button = gtk_stockbutton_new(GTK_STOCK_EXECUTE, _("_Start"));
  g_signal_connect(button, "clicked",
      G_CALLBACK(start_start_callback), NULL);
  gtk_container_add(GTK_CONTAINER(bbox), button);

  return box;
}


/**************************************************************************
  this regenerates the player information from a loaded game on the server.
**************************************************************************/
void handle_game_load(struct packet_game_load *packet)
{
  if (!packet->load_successful) {
  } else {
    update_nation_page(packet);

    set_client_page(PAGE_NATION);
  }
}

/**************************************************************************
  loads the currently selected game.
**************************************************************************/
static void load_callback(void)
{
  GtkTreeIter it;
  char *filename;

  if (!gtk_tree_selection_get_selected(load_selection, NULL, &it)) {
    return;
  }

  gtk_tree_model_get(GTK_TREE_MODEL(load_store), &it, 1, &filename, -1);

  if (is_server_running()) {
    char message[MAX_LEN_MSG];

    my_snprintf(message, sizeof(message), "/load %s", filename);
    send_chat(message);
  }
}

/**************************************************************************
  call the default GTK+ requester for saved game loading.
**************************************************************************/
static void load_browse_callback(GtkWidget *w, gpointer data)
{
  create_file_selection(_("Choose Saved Game to Load"), FALSE);
}

/**************************************************************************
  update the saved games list store.
**************************************************************************/
static void update_saves_store(GtkListStore *store)
{
  struct datafile_list files;

  gtk_list_store_clear(store);

  /* search for user saved games. */
  files = datafilelist_infix("saves", ".sav", FALSE);
  datafile_list_iterate(files, pfile) {
    GtkTreeIter it;

    gtk_list_store_append(store, &it);
    gtk_list_store_set(store, &it,
	0, pfile->name, 1, pfile->fullname, -1);

    free(pfile->name);
    free(pfile->fullname);
    free(pfile);
  } datafile_list_iterate_end;

  datafile_list_unlink_all(&files);

  files = datafilelist_infix(NULL, ".sav", FALSE);
  datafile_list_iterate(files, pfile) {
    GtkTreeIter it;

    gtk_list_store_append(store, &it);
    gtk_list_store_set(store, &it,
	0, pfile->name, 1, pfile->fullname, -1);

    free(pfile->name);
    free(pfile->fullname);
    free(pfile);
  } datafile_list_iterate_end;

  datafile_list_unlink_all(&files);
}

/**************************************************************************
 update the load page.
**************************************************************************/
static void update_load_page(void)
{
  update_saves_store(load_store);
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

  load_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(load_store));
  g_object_unref(load_store);

  rend = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
      -1, NULL, rend, "text", 0, NULL);

  load_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);

  gtk_tree_selection_set_mode(load_selection, GTK_SELECTION_SINGLE);

  g_signal_connect(view, "row_activated",
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

  if (is_server_running()) {
    char message[MAX_LEN_MSG];

    my_snprintf(message, sizeof(message), "/load %s", filename);
    send_chat(message);
  }
}

/**************************************************************************
  call the default GTK+ requester for scenario loading.
**************************************************************************/
static void scenario_browse_callback(GtkWidget *w, gpointer data)
{
  create_file_selection(_("Choose a Scenario"), FALSE);
}

/**************************************************************************
  update the scenario page.
**************************************************************************/
static void update_scenario_page(void)
{
  struct datafile_list files;

  gtk_list_store_clear(scenario_store);

  /* search for scenario files. */
  files = datafilelist_infix("scenario", ".sav", TRUE);
  datafile_list_iterate(files, pfile) {
    GtkTreeIter it;

    gtk_list_store_append(scenario_store, &it);
    gtk_list_store_set(scenario_store, &it,
	0, pfile->name, 1, pfile->fullname, -1);

    free(pfile->name);
    free(pfile->fullname);
    free(pfile);
  } datafile_list_iterate_end;

  datafile_list_unlink_all(&files);
}

/**************************************************************************
  create the scenario page.
**************************************************************************/
GtkWidget *create_scenario_page(void)
{
  GtkWidget *align, *box, *sbox, *bbox;

  GtkWidget *button, *label, *view, *sw;
  GtkCellRenderer *rend;

  box = gtk_vbox_new(FALSE, 18);
  gtk_container_set_border_width(GTK_CONTAINER(box), 4);

  align = gtk_alignment_new(0.5, 0.5, 0.0, 1.0);
  gtk_box_pack_start(GTK_BOX(box), align, TRUE, TRUE, 0);

  scenario_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(scenario_store));
  g_object_unref(scenario_store);

  rend = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
      -1, NULL, rend, "text", 0, NULL);

  scenario_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);

  gtk_tree_selection_set_mode(scenario_selection, GTK_SELECTION_SINGLE);

  g_signal_connect(view, "row_activated",
                   G_CALLBACK(scenario_callback), NULL);
  
  sbox = gtk_vbox_new(FALSE, 2);
  gtk_container_add(GTK_CONTAINER(align), sbox);

  label = g_object_new(GTK_TYPE_LABEL,
    "use-underline", TRUE,
    "mnemonic-widget", view,
    "label", _("Choose a _Scenario:"),
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
      G_CALLBACK(scenario_browse_callback), NULL);

  button = gtk_button_new_from_stock(GTK_STOCK_CANCEL);
  gtk_container_add(GTK_CONTAINER(bbox), button);
  g_signal_connect(button, "clicked",
      G_CALLBACK(main_callback), NULL);

  button = gtk_button_new_from_stock(GTK_STOCK_OK);
  gtk_container_add(GTK_CONTAINER(bbox), button);
  g_signal_connect(button, "clicked",
      G_CALLBACK(scenario_callback), NULL);

  return box;
}


/**************************************************************************
  change the player name to that of the nation's leader and start the game.
**************************************************************************/
static void nation_start_callback(void)
{
  GtkTreeIter it;
  char *name;

  if (!gtk_tree_selection_get_selected(nation_selection, NULL, &it)) {
    return;
  }

  gtk_tree_model_get(GTK_TREE_MODEL(nation_store), &it, 0, &name, -1);
  sz_strlcpy(player_name, name);

  send_start_saved_game();
}

#define MIN_DIMENSION 5
/**************************************************************************
 FIXME: this is somewhat duplicated in plrdlg.c, 
        should be somewhere else and non-static
**************************************************************************/
static GdkPixbuf *get_flag(char *flag_str)
{
  int x0, y0, x1, y1, w, h;
  GdkPixbuf *im;
  SPRITE *flag;

  flag = load_sprite(flag_str);

  if (!flag) {
    return NULL;
  }

  /* calculate the bounding box ... */
  sprite_get_bounding_box(flag, &x0, &y0, &x1, &y1);

  assert(x0 != -1);
  assert(y0 != -1);
  assert(x1 != -1);
  assert(y1 != -1);

  w = (x1 - x0) + 1;
  h = (y1 - y0) + 1;

  /* if the flag is smaller then 5 x 5, something is wrong */
  assert(w >= MIN_DIMENSION && h >= MIN_DIMENSION);

  /* get the pixbuf and crop*/
  im = gdk_pixbuf_get_from_drawable(NULL, flag->pixmap,
                                    gdk_colormap_get_system(),
                                    x0, y0, 0, 0, w, h);

  unload_sprite(flag_str);

  /* and finaly store the scaled flag pixbuf in the static flags array */
  return im;
}

/**************************************************************************
...
**************************************************************************/
static void update_nation_page(struct packet_game_load *packet)
{
  int i;

  game.nplayers = packet->nplayers;

  gtk_list_store_clear(nation_store);

  for (i = 0; i < packet->nplayers; i++) {
    GtkTreeIter iter;
    GdkPixbuf *flag;

    gtk_list_store_append(nation_store, &iter);
    gtk_list_store_set(nation_store, &iter, 
	0, packet->name[i],
	2, packet->nation_name[i],
	3, packet->is_alive[i] ? _("Alive") : _("Dead"),
	4, packet->is_ai[i] ? _("AI") : _("Human"), -1);

    /* set flag if we've got one to set. */
    if (strcmp(packet->nation_flag[i], "") != 0) {
      flag = get_flag(packet->nation_flag[i]);
      gtk_list_store_set(nation_store, &iter, 1, flag, -1);
      g_object_unref(flag);
    }
  }

  /* if nplayers is zero, we suppose it's a scenario */
  if (packet->nplayers == 0) {
    GtkTreeIter iter;
    char message[MAX_LEN_MSG];

    my_snprintf(message, sizeof(message), "/create %s", user_name);
    send_chat(message);
    my_snprintf(message, sizeof(message), "/ai %s", user_name);
    send_chat(message);
    my_snprintf(message, sizeof(message), "/take \"%s\"", user_name);
    send_chat(message);

    /* create a false entry */
    gtk_list_store_append(nation_store, &iter);
    gtk_list_store_set(nation_store, &iter,
	0, user_name,
	3, _("Alive"),
	4, _("Human"), -1);
  }
}

/**************************************************************************
  create the nation page.
**************************************************************************/
GtkWidget *create_nation_page(void)
{
  GtkWidget *box, *sbox, *label, *view, *sw, *bbox, *button;
  GtkCellRenderer *trenderer, *prenderer;

  box = gtk_vbox_new(FALSE, 18);
  gtk_container_set_border_width(GTK_CONTAINER(box), 4);

  sbox = gtk_vbox_new(FALSE, 2);
  gtk_container_add(GTK_CONTAINER(box), sbox);

  nation_store = gtk_list_store_new(5, G_TYPE_STRING, GDK_TYPE_PIXBUF,
      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
  
  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(nation_store));
  nation_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  g_object_unref(nation_store);
  gtk_tree_view_columns_autosize(GTK_TREE_VIEW(view));

  trenderer = gtk_cell_renderer_text_new();
  prenderer = gtk_cell_renderer_pixbuf_new();

  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
        -1, _("Name"), trenderer, "text", 0, NULL);
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
        -1, _("Flag"), prenderer, "pixbuf", 1, NULL);
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
        -1, _("Nation"), trenderer, "text", 2, NULL);
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
        -1, _("Status"), trenderer, "text", 3, NULL);
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
        -1, _("Type"), trenderer, "text", 4, NULL);

  gtk_tree_selection_set_mode(nation_selection, GTK_SELECTION_SINGLE);

  g_signal_connect(view, "row_activated",
                   G_CALLBACK(nation_start_callback), NULL);

  label = g_object_new(GTK_TYPE_LABEL,
                       "use-underline", TRUE,
		       "mnemonic-widget", view,
                       "label", _("Choose a _nation to play:"),
                       "xalign", 0.0,
                       "yalign", 0.5,
                       NULL);
  gtk_box_pack_start(GTK_BOX(sbox), label, FALSE, FALSE, 2);
  
  sw = gtk_scrolled_window_new(NULL,NULL);
  gtk_container_add(GTK_CONTAINER(sw), view);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
                                      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start(GTK_BOX(sbox), sw, TRUE, TRUE, 0);

  bbox = gtk_hbutton_box_new();
  gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_END);
  gtk_box_set_spacing(GTK_BOX(bbox), 12);
  gtk_box_pack_start(GTK_BOX(box), bbox, FALSE, FALSE, 0);

  button = gtk_stockbutton_new(GTK_STOCK_PREFERENCES,
      _("Game _Options..."));
  gtk_container_add(GTK_CONTAINER(bbox), button);
  gtk_button_box_set_child_secondary(GTK_BUTTON_BOX(bbox), button, TRUE);
  g_signal_connect(button, "clicked",
      G_CALLBACK(game_options_callback), NULL);

  button = gtk_button_new_from_stock(GTK_STOCK_CANCEL);
  gtk_container_add(GTK_CONTAINER(bbox), button);
  g_signal_connect(button, "clicked",
      G_CALLBACK(main_callback), NULL);

  button = gtk_stockbutton_new(GTK_STOCK_EXECUTE, _("_Start"));
  g_signal_connect(button, "clicked",
      G_CALLBACK(nation_start_callback), NULL);
  gtk_container_add(GTK_CONTAINER(bbox), button);

  return box;
}


/**************************************************************************
  changes the current page.
  this is basically a state machine. jumps actions are hardcoded.
**************************************************************************/
void set_client_page(enum client_pages page)
{
  enum client_pages new_page;

  new_page = page;

  /* If the page remains the same, don't do anything. */
  if (old_page == new_page) {
    return;
  }

  switch (old_page) {
  /* We aren't interested in showing a start page on game load or scenario. */
  case PAGE_SCENARIO:
  case PAGE_LOAD:
    if (new_page == PAGE_START) {
      return;
    }
    break;
  case PAGE_NETWORK:
    if (lan_timer != 0) {
      g_source_remove(lan_timer);
    }
    break;
  case PAGE_GAME:
    enable_menus(FALSE);
    break;
  default:
    break;
  }

  switch (new_page) {
  case PAGE_MAIN:
    break;
  case PAGE_START:
    if (is_server_running()) {
      gtk_widget_show(start_options_table);
      update_start_page();
    } else {
      gtk_widget_hide(start_options_table);
    }
    break;
  case PAGE_NATION:
    break;
  case PAGE_GAME:
    enable_menus(TRUE);
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
  case PAGE_START:
    break;
  case PAGE_NATION:
    gtk_tree_view_focus(gtk_tree_selection_get_tree_view(nation_selection));
    break;
  case PAGE_LOAD:
    gtk_tree_view_focus(gtk_tree_selection_get_tree_view(load_selection));
    break;
  case PAGE_SCENARIO:
    gtk_tree_view_focus(gtk_tree_selection_get_tree_view(scenario_selection));
    break;
  case PAGE_GAME:
    break;
  case PAGE_NETWORK:
    update_network_lists();
    gtk_widget_grab_focus(network_login);
    gtk_editable_set_position(GTK_EDITABLE(network_login), 0);
    break;
  }

  old_page = page;
}


/**************************************************************************
...
**************************************************************************/
static void set_page_callback(GtkWidget *w, gpointer data)
{
  set_client_page(GPOINTER_TO_UINT(data));
}

/**************************************************************************
                                SAVE GAME DIALOG
**************************************************************************/
static GtkWidget *save_dialog_shell, *save_entry;
static GtkListStore *save_store;
static GtkTreeSelection *save_selection;

enum {
  SAVE_BROWSE,
  SAVE_DELETE,
  SAVE_SAVE
};

/**************************************************************************
  update save dialog.
**************************************************************************/
static void update_save_dialog(void)
{
  update_saves_store(save_store);
}

/**************************************************************************
  handle save dialog response.
**************************************************************************/
static void save_response_callback(GtkWidget *w, gint arg)
{
  switch (arg) {
  case SAVE_BROWSE:
    create_file_selection(_("Select Location to Save"), TRUE);
    break;
  case SAVE_DELETE:
    {
      char *filename;
      GtkTreeIter it;

      if (!gtk_tree_selection_get_selected(save_selection, NULL, &it)) {
	return;
      }

      gtk_tree_model_get(GTK_TREE_MODEL(save_store), &it, 1, &filename, -1);
      remove(filename);
      update_save_dialog();
    }
    return;
  case SAVE_SAVE:
    {
      const char *text;
      char *filename;

      text = gtk_entry_get_text(GTK_ENTRY(save_entry));
      filename = g_filename_from_utf8(text, -1, NULL, NULL, NULL);
      send_save_game(filename);
      g_free(filename);
    }
    break;
  default:
    break;
  }
  gtk_widget_destroy(save_dialog_shell);
}

/**************************************************************************
  handle save list double click.
**************************************************************************/
static void save_row_callback(void)
{
  save_response_callback(NULL, SAVE_SAVE);
}

/**************************************************************************
  handle save filename entry activation.
**************************************************************************/
static void save_entry_callback(GtkEntry *w, gpointer data)
{
  save_response_callback(NULL, SAVE_SAVE);
}

/**************************************************************************
  handle save list selection change.
**************************************************************************/
static void save_list_callback(GtkTreeSelection *select, gpointer data)
{
  GtkTreeModel *model;
  GtkTreeIter it;
  char *name;

  if (!gtk_tree_selection_get_selected(select, &model, &it)) {
    gtk_dialog_set_response_sensitive(GTK_DIALOG(save_dialog_shell),
	SAVE_DELETE, FALSE);
    return;
  }

  gtk_dialog_set_response_sensitive(GTK_DIALOG(save_dialog_shell),
      SAVE_DELETE, TRUE);

  gtk_tree_model_get(model, &it, 0, &name, -1);
  gtk_entry_set_text(GTK_ENTRY(save_entry), name);
}

/**************************************************************************
  create save dialog.
**************************************************************************/
static void create_save_dialog(void)
{
  GtkWidget *shell;

  GtkWidget *sbox, *sw;

  GtkWidget *label, *view, *entry;
  GtkCellRenderer *rend;
  GtkTreeSelection *selection;


  shell = gtk_dialog_new_with_buttons(_("Save Game"),
      NULL,
      0,
      _("_Browse..."),
      SAVE_BROWSE,
      GTK_STOCK_DELETE,
      SAVE_DELETE,
      GTK_STOCK_CANCEL,
      GTK_RESPONSE_CANCEL,
      GTK_STOCK_SAVE,
      SAVE_SAVE,
      NULL);
  gtk_dialog_set_default_response(GTK_DIALOG(shell), GTK_RESPONSE_CANCEL);
  save_dialog_shell = shell;
  setup_dialog(shell, toplevel);

  save_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(save_store));
  g_object_unref(save_store);

  rend = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
      -1, NULL, rend, "text", 0, NULL);

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  save_selection = selection;
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);

  gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);

  g_signal_connect(view, "row_activated",
                   G_CALLBACK(save_row_callback), NULL);
  g_signal_connect(selection, "changed",
                   G_CALLBACK(save_list_callback), NULL);

  sbox = gtk_vbox_new(FALSE, 2);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(shell)->vbox), sbox, TRUE, TRUE, 0);

  label = g_object_new(GTK_TYPE_LABEL,
    "use-underline", TRUE,
    "mnemonic-widget", view,
    "label", _("Saved _Games:"),
    "xalign", 0.0,
    "yalign", 0.5,
    NULL);
  gtk_box_pack_start(GTK_BOX(sbox), label, FALSE, FALSE, 0);

  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
				      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC,
  				 GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request(sw, 300, 300);
  gtk_container_add(GTK_CONTAINER(sw), view);
  gtk_box_pack_start(GTK_BOX(sbox), sw, TRUE, TRUE, 0);


  sbox = gtk_vbox_new(FALSE, 2);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(shell)->vbox), sbox, FALSE, FALSE, 12);

  entry = gtk_entry_new();
  save_entry = entry;
  g_signal_connect(entry, "activate",
      G_CALLBACK(save_entry_callback), NULL);

  label = g_object_new(GTK_TYPE_LABEL,
    "use-underline", TRUE,
    "mnemonic-widget", entry,
    "label", _("Save _Filename:"),
    "xalign", 0.0,
    "yalign", 0.5,
    NULL);
  gtk_box_pack_start(GTK_BOX(sbox), label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(sbox), entry, FALSE, FALSE, 0);


  g_signal_connect(shell, "response",
      G_CALLBACK(save_response_callback), NULL);
  g_signal_connect(shell, "destroy",
      G_CALLBACK(gtk_widget_destroyed), &save_dialog_shell);

  gtk_dialog_set_response_sensitive(GTK_DIALOG(save_dialog_shell),
      SAVE_DELETE, FALSE);

  gtk_window_set_focus(GTK_WINDOW(shell), entry);

  gtk_widget_show_all(GTK_DIALOG(shell)->vbox);
}

/**************************************************************************
  popup save dialog.
**************************************************************************/
void popup_save_dialog(void)
{
  if (!save_dialog_shell) {
    create_save_dialog();
  }
  update_save_dialog();
 
  gtk_window_present(GTK_WINDOW(save_dialog_shell));
}

