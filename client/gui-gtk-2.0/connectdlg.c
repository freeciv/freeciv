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

enum {
  FIRST_PAGE,
  NEW_PAGE,
  LOAD_PAGE,
  NETWORK_PAGE
};

enum {
  LOGIN_PAGE, 
  METASERVER_PAGE,
  LAN_PAGE
};
  
static enum {
  LOGIN_TYPE, 
  NEW_PASSWORD_TYPE, 
  VERIFY_PASSWORD_TYPE,
  ENTER_PASSWORD_TYPE
} dialog_config;

static GtkWidget *imsg, *ilabel, *iinput, *ihost, *iport;
static GtkWidget *aifill, *aiskill, *savedlabel;
static GtkWidget *prevw, *nextw, *loadw;
static GtkListStore *storesaved;
static GtkListStore *storemeta;
static GtkListStore *storelan;

static GtkWidget *dialog, *uberbook, *book;

static int next_page;

const char *get_aiskill_setting(void);

/* meta Server */
static bool update_meta_dialog(void);
static void meta_list_callback(GtkTreeSelection *select, GtkTreeModel *model);
static void meta_update_callback(GtkWidget *w, gpointer data);

static int get_meta_list(char *errbuf, int n_errbuf);

/* LAN Servers */
static void lan_update_callback(GtkWidget *w, gpointer data);
static void lan_list_callback(GtkTreeSelection *select, GtkTreeModel *model);
static int get_lanservers(gpointer data);

static int num_lanservers_timer = 0;

/**************************************************************************
 really close and destroy the dialog.
**************************************************************************/
void really_close_connection_dialog(void)
{
  if (dialog) {
    gtk_widget_destroy(dialog);
    dialog = NULL;
    gtk_widget_set_sensitive(top_vbox, TRUE);
  }
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
 configure the dialog depending on what type of authentication request the
 server is making.
**************************************************************************/
void handle_authentication_req(enum authentication_type type, char *message)
{
  gtk_widget_grab_focus(iinput);
  gtk_entry_set_text(GTK_ENTRY(iinput), "");
  gtk_button_set_label(GTK_BUTTON(nextw), _("Next >"));
  gtk_widget_set_sensitive(nextw, TRUE);
  gtk_label_set_text(GTK_LABEL(imsg), message);

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
  gtk_label_set_text(GTK_LABEL(ilabel), _("Password:"));
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

  if (gtk_notebook_get_current_page(GTK_NOTEBOOK(book)) == METASERVER_PAGE ||
      gtk_notebook_get_current_page(GTK_NOTEBOOK(book)) == LAN_PAGE) {
    gtk_notebook_set_current_page(GTK_NOTEBOOK(book), LOGIN_PAGE);
    return;
  }

  switch (dialog_config) {
  case LOGIN_TYPE:
    sz_strlcpy(user_name, gtk_entry_get_text(GTK_ENTRY(iinput)));
    sz_strlcpy(server_host, gtk_entry_get_text(GTK_ENTRY(ihost)));
    server_port = atoi(gtk_entry_get_text(GTK_ENTRY(iport)));
  
    if (connect_to_server(user_name, server_host, server_port,
                          errbuf, sizeof(errbuf)) != -1) {
      really_close_connection_dialog();
    } else {
      append_output_window(errbuf);
    }

    break; 
  case NEW_PASSWORD_TYPE:
    sz_strlcpy(password, gtk_entry_get_text(GTK_ENTRY(iinput)));
    gtk_label_set_text(GTK_LABEL(imsg), _("Verify Password"));
    gtk_entry_set_text(GTK_ENTRY(iinput), "");
    gtk_widget_grab_focus(iinput);
    dialog_config = VERIFY_PASSWORD_TYPE;
    break;
  case VERIFY_PASSWORD_TYPE:
    sz_strlcpy(reply.password, gtk_entry_get_text(GTK_ENTRY(iinput)));
    if (strncmp(reply.password, password, MAX_LEN_NAME) == 0) {
      gtk_widget_set_sensitive(nextw, FALSE);
      memset(password, 0, MAX_LEN_NAME);
      password[0] = '\0';
      send_packet_authentication_reply(&aconnection, &reply);
    } else { 
      gtk_widget_grab_focus(iinput);
      gtk_entry_set_text(GTK_ENTRY(iinput), "");
      gtk_label_set_text(GTK_LABEL(imsg),
	  		 _("Passwords don't match, enter password."));
      dialog_config = NEW_PASSWORD_TYPE;
    }
    break;
  case ENTER_PASSWORD_TYPE:
    gtk_widget_set_sensitive(nextw, FALSE);
    sz_strlcpy(reply.password, gtk_entry_get_text(GTK_ENTRY(iinput)));
    send_packet_authentication_reply(&aconnection, &reply);
    break;
  default:
    assert(0);
  }
}

/**************************************************************************
...
**************************************************************************/
static bool update_meta_dialog(void)
{
  char errbuf[128];

  if(get_meta_list(errbuf, sizeof(errbuf))!=-1)  {
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
  update_meta_dialog();
}

/**************************************************************************
 This function updates the list of LAN servers every 100 ms for 5 seconds. 
**************************************************************************/
static int get_lanservers(gpointer data)
{
  struct server_list *server_list = get_lan_server_list();
  gchar *row[6];

  if (server_list != NULL) {
    gtk_list_store_clear(storelan);

    server_list_iterate(*server_list, pserver) {
      GtkTreeIter it;
      int i;

      row[0] = ntoh_str(pserver->name);
      row[1] = ntoh_str(pserver->port);
      row[2] = ntoh_str(pserver->version);
      row[3] = _(pserver->status);
      row[4] = ntoh_str(pserver->players);
      row[5] = ntoh_str(pserver->metastring);

      gtk_list_store_append(storelan, &it);
      gtk_list_store_set(storelan, &it,
		         0, row[0], 1, row[1], 2, row[2],
		         3, row[3], 4, row[4], 5, row[5], -1);

      for (i = 0; i < 6; i++) {
        if (i != 3) {
          g_free(row[i]);
        }
      }
    } server_list_iterate_end;
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
    gtk_list_store_clear(storelan);
    if (begin_lanserver_scan()) {
      get_lanservers_timer = gtk_timeout_add(100, get_lanservers, NULL);
    }
  }
}

/**************************************************************************
 This function is called when a row from the single player 
 saved list is selected.
**************************************************************************/
static void saved_list_callback(GtkTreeSelection *select, GtkTreeModel *dummy)
{
  GtkTreeIter it;
  char *name;

  if (!gtk_tree_selection_get_selected(select, NULL, &it)) {
    gtk_widget_set_sensitive(nextw, FALSE);
    return;
  }

  gtk_tree_model_get(GTK_TREE_MODEL(storesaved), &it, 0, &name, -1);

  sz_strlcpy(player_name, name);

  gtk_widget_set_sensitive(nextw, TRUE);
}

/**************************************************************************
...
**************************************************************************/
static void meta_list_callback(GtkTreeSelection *select, GtkTreeModel *dummy)
{
  GtkTreeIter it;
  char *name, *port;

  if (!gtk_tree_selection_get_selected(select, NULL, &it))
    return;

  gtk_tree_model_get(GTK_TREE_MODEL(storemeta), &it, 0, &name, 1, &port, -1);

  gtk_entry_set_text(GTK_ENTRY(ihost), name);
  gtk_entry_set_text(GTK_ENTRY(iport), port);
}

/**************************************************************************
...
**************************************************************************/
static void lan_list_callback(GtkTreeSelection *select, GtkTreeModel *dummy)
{
  GtkTreeIter it;
  char *name, *port;

  if (!gtk_tree_selection_get_selected(select, NULL, &it)) {
    return;
  }

  gtk_tree_model_get(GTK_TREE_MODEL(storelan), &it, 0, &name, 1, &port, -1);

  gtk_entry_set_text(GTK_ENTRY(ihost), name);
  gtk_entry_set_text(GTK_ENTRY(iport), port);
}

/**************************************************************************
  This function is called on a click in the single player saved list
***************************************************************************/
static gboolean saved_click_callback(GtkWidget *w, GdkEventButton *event, 
                                     gpointer data)
{
  if (event->type == GDK_2BUTTON_PRESS) {
    really_close_connection_dialog();
    send_start_saved_game();
  }

  return FALSE;
}

/**************************************************************************
...
***************************************************************************/
static gboolean meta_click_callback(GtkWidget *w, GdkEventButton *event, gpointer data)
{
  if (event->type==GDK_2BUTTON_PRESS) connect_callback(w, data);
  return FALSE;
}

/**************************************************************************
...
***************************************************************************/
static gboolean lan_click_callback(GtkWidget *w, GdkEventButton *event, gpointer data)
{
  if (event->type == GDK_2BUTTON_PRESS) {
    connect_callback(w, data);
  }
  return FALSE;
}

/**************************************************************************
...
**************************************************************************/
static void connect_destroy_callback(GtkWidget *w, gpointer data)
{
  dialog = NULL;
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
  this regenerates the player information from a game on the server.
**************************************************************************/
void handle_single_playerlist_reply(struct packet_single_playerlist_reply 
                                    *packet)
{
  int i;

  if (!dialog) {
    return;
  }

  gtk_list_store_clear(storesaved);

  if (packet->nplayers != 0) {
    game.nplayers = packet->nplayers;
  }

  /* we couldn't load the savegame, we could have gotten the name wrong, etc */
  if (packet->nplayers == 0
      || strcmp(current_filename, packet->load_filename) != 0) {
    gtk_label_set_label(GTK_LABEL(savedlabel), _("Couldn't load the savegame"));
    return;
  } else {
    char *buf = current_filename;
    
    buf = strrchr(current_filename, '/');
    gtk_label_set_label(GTK_LABEL(savedlabel), ++buf);
  }


  for (i = 0; i < packet->nplayers; i++) {
    GtkTreeIter iter;
    GdkPixbuf *flag;

    gtk_list_store_append(storesaved, &iter);
    gtk_list_store_set(storesaved, &iter, 
                       0, packet->name[i],
                       2, packet->nation_name[i],
                       3, packet->is_alive[i] ? _("Alive") : _("Dead"),
                       4, packet->is_ai[i] ? _("AI") : _("Human"), -1);

    /* set flag. */
    flag = get_flag(packet->nation_flag[i]);
    gtk_list_store_set(storesaved, &iter, 1, flag, -1);
    g_object_unref(flag);
  }
}

/**************************************************************************
 callback to load a game.
**************************************************************************/
static void load_file(GtkWidget *widget, gpointer user_data)
{
  GtkFileSelection *selector = (GtkFileSelection *)user_data;
  char message[MAX_LEN_MSG];

  if (current_filename) {
    free(current_filename);
  }

  current_filename = mystrdup(gtk_file_selection_get_filename(selector));

  gtk_label_set_label(GTK_LABEL(savedlabel), _("Loading..."));

  my_snprintf(message, MAX_LEN_MSG, "/load %s", current_filename);
  send_chat(message);
  send_packet_single_playerlist_req(&aconnection);
}

/**************************************************************************
 callback to save a game.
**************************************************************************/
static void save_file(GtkWidget *widget, gpointer user_data)
{ 
  GtkFileSelection *selector = (GtkFileSelection *)user_data;
  
  if (current_filename) {
    free(current_filename);
  }
  
  current_filename = mystrdup(gtk_file_selection_get_filename(selector));

  send_save_game(current_filename);
}

/**************************************************************************
 create a file selector for both the load and save commands
**************************************************************************/
GtkWidget *create_file_selection(char *title, bool is_save)
{
  GtkWidget *file_selector;
  
  /* Create the selector */
  file_selector = gtk_file_selection_new(title);
   
  if (current_filename) {
    gtk_file_selection_set_filename(GTK_FILE_SELECTION(file_selector),
                                    current_filename);
  }

  g_signal_connect(GTK_OBJECT(GTK_FILE_SELECTION(file_selector)->ok_button),
                   "clicked", 
                   (is_save) ? G_CALLBACK(save_file) : G_CALLBACK(load_file), 
                   (gpointer)file_selector);
                           
  /* Ensure that the dialog box is destroyed when the user clicks a button. */
   
  g_signal_connect_swapped(GTK_OBJECT(
                           GTK_FILE_SELECTION(file_selector)->ok_button),
                           "clicked", G_CALLBACK (gtk_widget_destroy), 
                           (gpointer) file_selector); 

  g_signal_connect_swapped(GTK_OBJECT(
                           GTK_FILE_SELECTION(file_selector)->cancel_button),
                           "clicked", G_CALLBACK (gtk_widget_destroy),
                           (gpointer) file_selector); 

  /* Display that dialog */
  gtk_widget_show(file_selector);

  return file_selector;
}

/**************************************************************************
  callback for load button.
**************************************************************************/
static void load_callback(GtkWidget *w, gpointer data)
{
  /* start a server if we haven't already started one */
  if (!is_server_running() && !client_start_server()) {
    return;
  }
 
  gtk_widget_set_sensitive(nextw, FALSE);

  /* swapped makes the filesel the second arg, and a filesel is always true */
  g_signal_connect_swapped(create_file_selection(_("Choose Savegame to Load"),
                                                 FALSE),
			   "destroy", G_CALLBACK(gtk_widget_set_sensitive), w);
  gtk_widget_set_sensitive(w, FALSE);
}

/****************************************************************
 handles both the uberbook and the network book
*****************************************************************/
static void switch_page_callback(GtkNotebook * notebook,
                                 GtkNotebookPage * page, gint page_num,
                                 gpointer data)
{
  if (notebook == GTK_NOTEBOOK(uberbook)) {
    gtk_widget_set_sensitive(nextw, TRUE);
    gtk_widget_set_sensitive(prevw, TRUE);
    gtk_widget_hide(loadw);

    switch(page_num) {
    case FIRST_PAGE:
      client_kill_server();
      gtk_widget_set_sensitive(prevw, FALSE);
      gtk_button_set_label(GTK_BUTTON(nextw), _("Next >"));
      break;
    case NEW_PAGE:
      break;
    case LOAD_PAGE:
      gtk_widget_set_sensitive(nextw, FALSE);
      gtk_widget_show(loadw);
      break;
    case NETWORK_PAGE:
      break;
    default:
      break;
    }
  } else { /* network book */
    switch(page_num) {
    case LOGIN_PAGE:
      gtk_button_set_label(GTK_BUTTON(nextw),
                      dialog_config == LOGIN_TYPE ? _("Connect") : _("Next >"));
      break;
    case METASERVER_PAGE:
    case LAN_PAGE:
      gtk_button_set_label(GTK_BUTTON(nextw), _("Select"));
      break;
    default:
      break;
    }
  }
}

/**************************************************************************
...
**************************************************************************/
static void response_callback(GtkWidget *w, gint response_id)
{
  if (response_id == GTK_RESPONSE_REJECT) {
    client_kill_server();
    gtk_main_quit();
  }
}

/**************************************************************************
...
**************************************************************************/
static void radio_command_callback(GtkToggleButton *w, gpointer data)
{
  if (gtk_toggle_button_get_active(w)) {
    next_page = GPOINTER_TO_INT(data);
  }
}

/**************************************************************** 
  get the ai skill command
*****************************************************************/ 
const char *get_aiskill_setting(void)
{
  int i;
  char buf[32];

  sz_strlcpy(buf, gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(aiskill)->entry)));

 for (i = 0; i < NUM_SKILL_LEVELS; i++) {
    if (strcmp(buf, _(skill_level_names[i])) == 0) {
      return skill_level_names[i];
    }
  }

  /* an error of some kind, should never get here. */ 
  assert(0);
  return /* TRANS: don't translate */ "normal";
}

/**************************************************************************
...
**************************************************************************/
static void prev_command_callback(GtkWidget *w, gpointer data)
{
  gtk_notebook_set_current_page(GTK_NOTEBOOK(uberbook), FIRST_PAGE);
}

/**************************************************************************
...
**************************************************************************/
static void next_command_callback(GtkWidget *w, gpointer data)
{
  const char *next_labels[4] = { "", N_("Start"), N_("Resume"), N_("Connect") };
  char buf[512];

  if (gtk_notebook_get_current_page(GTK_NOTEBOOK(uberbook)) == FIRST_PAGE) {
    if (next_page != NETWORK_PAGE && !is_server_running() && 
        !client_start_server()) {
      return;
    }

    gtk_widget_set_sensitive(prevw, TRUE);

    /* check which radio button is active and switch "book" to that page */   
    gtk_notebook_set_current_page(GTK_NOTEBOOK(uberbook), next_page);
    gtk_button_set_label(GTK_BUTTON(nextw), next_labels[next_page]);
    if (next_page == LOAD_PAGE) {
      load_callback(loadw, NULL);
    }
  } else {
    switch (gtk_notebook_get_current_page(GTK_NOTEBOOK(uberbook))) {
    case NEW_PAGE:
      my_snprintf(buf, sizeof(buf), "/%s", get_aiskill_setting());
      send_chat(buf);
      my_snprintf(buf, sizeof(buf), "/set aifill %d",
                  (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(aifill)));
      send_chat(buf);

      really_close_connection_dialog();
      send_chat("/start");
      break;
    case LOAD_PAGE:
      really_close_connection_dialog();
      send_start_saved_game();
      break;
    case NETWORK_PAGE:
      connect_callback(w, NULL);
      break;
    default:
      break;
    };
  }
}

/**************************************************************************
...
**************************************************************************/
void gui_server_connect(void)
{
  GtkWidget *label, *table, *scrolled, *listsaved, *listmeta, *listlan;
  GtkWidget *hbox, *vbox, *updatemeta, *updatelan;
  GtkWidget *radio, *button;
  int i;
  char buf[256];
  GtkCellRenderer *trenderer, *prenderer;
  GtkTreeSelection *selectionsaved, *selectionmeta, *selectionlan;
  GSList *group = NULL;
  GList *items = NULL;
  GtkAdjustment *adj;

  if (dialog) {
    return;
  }

  next_page = NEW_PAGE;
  dialog_config = LOGIN_TYPE;

  dialog = gtk_dialog_new_with_buttons(_(" Connect to Freeciv Server"),
                                       NULL,
                                       GTK_DIALOG_DESTROY_WITH_PARENT,
                                       NULL);

  if (dialogs_on_top) {
    gtk_window_set_transient_for(GTK_WINDOW(dialog),
				 GTK_WINDOW(toplevel));
  }
  g_signal_connect(dialog, "destroy",
		   G_CALLBACK(connect_destroy_callback), NULL);
  g_signal_connect(dialog, "response",
		   G_CALLBACK(response_callback), NULL);

  /* create the action area buttons */

  loadw = gtk_button_new_with_label(_("Load..."));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area), 
                     loadw, TRUE, TRUE, 0);
  g_signal_connect(loadw, "clicked", G_CALLBACK(load_callback), NULL);

  prevw = gtk_button_new_with_label(_("< Prev"));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area),
                     prevw, TRUE, TRUE, 0);
  gtk_widget_set_sensitive(prevw, FALSE);
  g_signal_connect(prevw, "clicked", G_CALLBACK(prev_command_callback), NULL);

  nextw = gtk_button_new_with_label(_("Next >"));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area),
                     nextw, TRUE, TRUE, 0);
  g_signal_connect(nextw, "clicked", G_CALLBACK(next_command_callback), NULL);

  gtk_dialog_add_button(GTK_DIALOG(dialog),
                        GTK_STOCK_QUIT, GTK_RESPONSE_REJECT);

  /* main body */

  uberbook = gtk_notebook_new();
  gtk_notebook_set_show_tabs(GTK_NOTEBOOK(uberbook), FALSE);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), uberbook, 
                     TRUE, TRUE, 0);
  
  /* first page of uber book */  

  vbox = gtk_vbox_new(FALSE, 2);
  gtk_notebook_append_page(GTK_NOTEBOOK(uberbook), vbox, NULL);

  radio = gtk_radio_button_new_with_label(group, _("Start New Game"));
  group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio));
  gtk_box_pack_start(GTK_BOX(vbox), radio, TRUE, FALSE, 2);

  g_signal_connect(radio, "toggled",G_CALLBACK(radio_command_callback),
                   GINT_TO_POINTER(NEW_PAGE));

  radio = gtk_radio_button_new_with_label(group, _("Load Saved Game"));
  group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio));
  gtk_box_pack_start(GTK_BOX(vbox), radio, TRUE, FALSE, 2);
  
  g_signal_connect(radio, "toggled",G_CALLBACK(radio_command_callback),
                   GINT_TO_POINTER(LOAD_PAGE));

  radio = gtk_radio_button_new_with_label(group, _("Connect to Network Game"));
  gtk_box_pack_start(GTK_BOX(vbox), radio, TRUE, FALSE, 2);

  g_signal_connect(radio, "toggled",G_CALLBACK(radio_command_callback),
                   GINT_TO_POINTER(NETWORK_PAGE));

  /* second page of uber book: new game */  

  vbox = gtk_vbox_new(FALSE, 2);
  gtk_notebook_append_page(GTK_NOTEBOOK(uberbook), vbox, NULL);

  label = gtk_label_new(_("Start New Game"));
  gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 2);

  hbox = gtk_hbox_new(FALSE, 2);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 20);

  table = gtk_table_new(2, 2, FALSE);
  gtk_table_set_col_spacings(GTK_TABLE(table), 15);
  gtk_box_pack_start(GTK_BOX(hbox), table, TRUE, TRUE, 20);

  label = g_object_new(GTK_TYPE_LABEL,
                       "label", _("Number of players:"),
                       "xalign", 0.0,
                       "yalign", 0.5,
                       NULL);
  gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 0, 1);

  adj = GTK_ADJUSTMENT(gtk_adjustment_new(1, 1, MAX_NUM_PLAYERS, 1, 1, 1));
  aifill = gtk_spin_button_new(adj, 1, 0);
  gtk_spin_button_set_update_policy(GTK_SPIN_BUTTON(aifill), 
                                    GTK_UPDATE_IF_VALID);
  gtk_table_attach_defaults(GTK_TABLE(table), aifill, 1, 2, 0, 1);

  label = g_object_new(GTK_TYPE_LABEL,
                       "label", _("AI skill level:"),
                       "xalign", 0.0,
                       "yalign", 0.5,
                       NULL);
  gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 1, 2);

  aiskill = gtk_combo_new();

  gtk_editable_set_editable(GTK_EDITABLE(GTK_COMBO(aiskill)->entry), FALSE);
  for (i = 0; i < NUM_SKILL_LEVELS; i++) {
    items = g_list_append(items, (char*)_(skill_level_names[i]));
  }
  gtk_combo_set_popdown_strings(GTK_COMBO(aiskill), items);
  gtk_table_attach_defaults(GTK_TABLE(table), aiskill, 1, 2, 1, 2);

  hbox = gtk_hbox_new(FALSE, 2);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 20);

  button = gtk_button_new_with_label(_("Change Server Options"));
  g_signal_connect_swapped(G_OBJECT(button), "clicked",
			   G_CALLBACK(send_report_request), 
			   (gpointer)REPORT_SERVER_OPTIONS2);
  gtk_box_pack_end(GTK_BOX(hbox), button, TRUE, TRUE, 20);


  /* third page of uber book: load game */  

  vbox = gtk_vbox_new(FALSE, 2);
  gtk_notebook_append_page(GTK_NOTEBOOK(uberbook), vbox, NULL);

  savedlabel = g_object_new(GTK_TYPE_LABEL,
                            "use-underline", TRUE,
                            "xalign", 0.0,
                            "yalign", 0.5,
                            NULL);
  gtk_box_pack_start(GTK_BOX(vbox), savedlabel, FALSE, FALSE, 2);
  
  label = g_object_new(GTK_TYPE_LABEL,
                       "use-underline", TRUE,
                       "label", _("These are the nations in the saved game, "
                                  "choose which to play:"),
                       "xalign", 0.0,
                       "yalign", 0.5,
                       NULL);
  gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 2);
  
  storesaved = gtk_list_store_new(5, G_TYPE_STRING, GDK_TYPE_PIXBUF,
                                     G_TYPE_STRING, G_TYPE_STRING,
                                     G_TYPE_STRING);
  
  listsaved = gtk_tree_view_new_with_model(GTK_TREE_MODEL(storesaved));
  selectionsaved = gtk_tree_view_get_selection(GTK_TREE_VIEW(listsaved));
  g_object_unref(storesaved);
  gtk_tree_view_columns_autosize(GTK_TREE_VIEW(listsaved));

  trenderer = gtk_cell_renderer_text_new();
  prenderer = gtk_cell_renderer_pixbuf_new();

  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(listsaved),
        -1, _("Name"), trenderer, "text", 0, NULL);
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(listsaved),
        -1, _("Flag"), prenderer, "pixbuf", 1, NULL);
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(listsaved),
        -1, _("Nation"), trenderer, "text", 2, NULL);
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(listsaved),
        -1, _("Status"), trenderer, "text", 3, NULL);
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(listsaved),
        -1, _("Type"), trenderer, "text", 4, NULL);

  gtk_tree_selection_set_mode(selectionsaved, GTK_SELECTION_SINGLE);

  scrolled = gtk_scrolled_window_new(NULL,NULL);
  gtk_container_add(GTK_CONTAINER(scrolled), listsaved);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
  gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

  /* fourth page of uber book: network notebook */  

  book = gtk_notebook_new();
  gtk_notebook_append_page(GTK_NOTEBOOK(uberbook), book, NULL);

  label=gtk_label_new(_("Freeciv Server Selection"));

  vbox=gtk_vbox_new(FALSE, 2);
  gtk_notebook_append_page (GTK_NOTEBOOK (book), vbox, label);

  table = gtk_table_new (4, 1, FALSE);
  gtk_table_set_row_spacings (GTK_TABLE (table), 2);
  gtk_table_set_col_spacings (GTK_TABLE (table), 12);
  gtk_container_set_border_width(GTK_CONTAINER(table), 6);
  gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, TRUE, 0);

  imsg = g_object_new(GTK_TYPE_LABEL,
                       "use-underline", TRUE,
                       "xalign", 0.0,
                       "yalign", 0.5,
                       NULL);
  gtk_table_attach(GTK_TABLE(table), imsg, 1, 2, 0, 1,
                   GTK_FILL, GTK_FILL, 0, 0);

  iinput = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(iinput), user_name);
  gtk_table_attach(GTK_TABLE(table), iinput, 1, 2, 1, 2,
		   GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

  ilabel = g_object_new(GTK_TYPE_LABEL,
		       "use-underline", TRUE,
		       "mnemonic-widget", iinput,
		       "label", _("_Login:"),
		       "xalign", 0.0,
		       "yalign", 0.5,
		       NULL);
  gtk_table_attach(GTK_TABLE(table), ilabel, 0, 1, 1, 2,
		   GTK_FILL, GTK_FILL, 0, 0);

  ihost=gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(ihost), server_host);
  gtk_table_attach(GTK_TABLE(table), ihost, 1, 2, 2, 3,
		   GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

  label = g_object_new(GTK_TYPE_LABEL,
		       "use-underline", TRUE,
		       "mnemonic-widget", ihost,
		       "label", _("_Host:"),
		       "xalign", 0.0,
		       "yalign", 0.5,
		       NULL);
  gtk_table_attach(GTK_TABLE(table), label, 0, 1, 2, 3,
		   GTK_FILL, GTK_FILL, 0, 0);

  my_snprintf(buf, sizeof(buf), "%d", server_port);

  iport=gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(iport), buf);
  gtk_table_attach(GTK_TABLE(table), iport, 1, 2, 3, 4,
		   GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

  label = g_object_new(GTK_TYPE_LABEL,
		       "use-underline", TRUE,
		       "mnemonic-widget", iport,
		       "label", _("_Port:"),
		       "xalign", 0.0,
		       "yalign", 0.5,
		       NULL);
  gtk_table_attach(GTK_TABLE(table), label, 0, 1, 3, 4,
		   GTK_FILL, GTK_FILL, 0, 0);

#if IS_BETA_VERSION
  {
    GtkWidget *label2;

    label2=gtk_label_new (beta_message());
    gtk_widget_modify_fg(label2, GTK_STATE_NORMAL,
	colors_standard[COLOR_STD_RED]);
    gtk_misc_set_alignment(GTK_MISC(label2), 0.5, 0.5);
    gtk_label_set_justify(GTK_LABEL(label2), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(vbox), label2, TRUE, TRUE, 0);
  }
#endif

  /* Create the Metaserver notebook page  */
  label = gtk_label_new(_("Metaserver"));

  vbox = gtk_vbox_new(FALSE, 2);
  gtk_notebook_append_page (GTK_NOTEBOOK (book), vbox, label);

  storemeta = gtk_list_store_new(6, G_TYPE_STRING, G_TYPE_STRING,
                                 G_TYPE_STRING, G_TYPE_STRING, 
                                 G_TYPE_STRING, G_TYPE_STRING);

  listmeta = gtk_tree_view_new_with_model(GTK_TREE_MODEL(storemeta));
  selectionmeta = gtk_tree_view_get_selection(GTK_TREE_VIEW(listmeta));
  g_object_unref(storemeta);
  gtk_tree_view_columns_autosize(GTK_TREE_VIEW(listmeta));

  trenderer = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(listmeta),
	-1, _("Server Name"), trenderer, "text", 0, NULL);
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(listmeta),
	-1, _("Port"), trenderer, "text", 1, NULL);
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(listmeta),
	-1, _("Version"), trenderer, "text", 2, NULL);
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(listmeta),
	-1, _("Status"), trenderer, "text", 3, NULL);
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(listmeta),
	-1, _("Players"), trenderer, "text", 4, NULL);
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(listmeta),
	-1, _("Comment"), trenderer, "text", 5, NULL);

  scrolled = gtk_scrolled_window_new(NULL,NULL);
  gtk_container_add(GTK_CONTAINER(scrolled), listmeta);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				 GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
  gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

  updatemeta = gtk_button_new_from_stock(GTK_STOCK_REFRESH);
  gtk_box_pack_start(GTK_BOX(vbox), updatemeta, FALSE, FALSE, 2);

  gtk_widget_show_all(GTK_DIALOG(dialog)->vbox);

  /* Create the Local Area Network notebook page   */
  label = gtk_label_new(_("Local Area Network"));

  vbox = gtk_vbox_new(FALSE, 2);
  gtk_notebook_append_page (GTK_NOTEBOOK (book), vbox, label);

  storelan = gtk_list_store_new(6, G_TYPE_STRING, G_TYPE_STRING,
                                G_TYPE_STRING, G_TYPE_STRING, 
                                G_TYPE_STRING, G_TYPE_STRING);

  listlan = gtk_tree_view_new_with_model(GTK_TREE_MODEL(storelan));
  selectionlan = gtk_tree_view_get_selection(GTK_TREE_VIEW(listlan));
  g_object_unref(storelan);
  gtk_tree_view_columns_autosize(GTK_TREE_VIEW(listlan));

  trenderer = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(listlan),
	-1, _("Server Name"), trenderer, "text", 0, NULL);
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(listlan),
	-1, _("Port"), trenderer, "text", 1, NULL);
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(listlan),
	-1, _("Version"), trenderer, "text", 2, NULL);
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(listlan),
	-1, _("Status"), trenderer, "text", 3, NULL);
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(listlan),
	-1, _("Players"), trenderer, "text", 4, NULL);
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(listlan),
	-1, _("Comment"), trenderer, "text", 5, NULL);

  scrolled = gtk_scrolled_window_new(NULL,NULL);
  gtk_container_add(GTK_CONTAINER(scrolled), listlan);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				 GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
  gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

  updatelan = gtk_button_new_from_stock(GTK_STOCK_REFRESH);
  gtk_box_pack_start(GTK_BOX(vbox), updatelan, FALSE, FALSE, 2);

  gtk_widget_show_all(GTK_DIALOG(dialog)->vbox);

  gtk_widget_hide(loadw);

  if (auto_connect) {
     gtk_widget_hide(dialog);
  } else {
     gtk_widget_show(dialog);
  }

  /* connect all the signals here, so that we can't send 
   * packets to the server until the dialog is up (which 
   * it may not be on very slow machines) */


  g_signal_connect(listsaved, "button_press_event",
		   G_CALLBACK(saved_click_callback), NULL);
  g_signal_connect(selectionsaved, "changed",
                   G_CALLBACK(saved_list_callback), NULL);

  g_signal_connect(listmeta, "button_press_event",
		   G_CALLBACK(meta_click_callback), NULL);
  g_signal_connect(selectionmeta, "changed",
                   G_CALLBACK(meta_list_callback), NULL);
  g_signal_connect(updatemeta, "clicked",
		   G_CALLBACK(meta_update_callback), NULL);

  g_signal_connect(listlan, "button_press_event",
                   G_CALLBACK(lan_click_callback), NULL);
  g_signal_connect(selectionlan, "changed",
                   G_CALLBACK(lan_list_callback), NULL);
  g_signal_connect(updatelan, "clicked",
                   G_CALLBACK(lan_update_callback), NULL);

  g_signal_connect(uberbook, "switch-page",
                   G_CALLBACK(switch_page_callback), NULL);
  g_signal_connect(book, "switch-page", G_CALLBACK(switch_page_callback), NULL);
  g_signal_connect(iinput, "activate", G_CALLBACK(connect_callback), NULL);
  g_signal_connect(ihost, "activate", G_CALLBACK(connect_callback), NULL);
  g_signal_connect(iport, "activate", G_CALLBACK(connect_callback), NULL);

  gtk_widget_set_size_request(dialog, 500, 250);
  gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER_ON_PARENT);
  gtk_window_present(GTK_WINDOW(dialog));
}

/**************************************************************************
  Get the list of servers from the metaserver
**************************************************************************/
static int get_meta_list(char *errbuf, int n_errbuf)
{
  struct server_list *server_list = create_server_list(errbuf, n_errbuf);
  gchar *row[6];

  if (!server_list) {
    return -1;
  }

  gtk_list_store_clear(storemeta);

  server_list_iterate(*server_list, pserver) {
    GtkTreeIter it;
    int i;

    row[0] = ntoh_str(pserver->name);
    row[1] = ntoh_str(pserver->port);
    row[2] = ntoh_str(pserver->version);
    row[3] = _(pserver->status);
    row[4] = ntoh_str(pserver->players);
    row[5] = ntoh_str(pserver->metastring);

    gtk_list_store_append(storemeta, &it);
    gtk_list_store_set(storemeta, &it,
		       0, row[0], 1, row[1], 2, row[2],
		       3, row[3], 4, row[4], 5, row[5], -1);

    for (i=0; i<6; i++) {
      if (i != 3) {
        g_free(row[i]);
      }
    }
  }
  server_list_iterate_end;

  delete_server_list(server_list);
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
