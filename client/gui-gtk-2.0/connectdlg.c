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
  CMD_OPTIONS, CMD_LOAD,
  CMD_PREV, CMD_NEXT,
  CMD_START, CMD_RESUME, CMD_CONNECT
};

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
  
static GtkWidget *options_cmd, *load_cmd;
static GtkWidget *prev_cmd, *next_cmd;
static GtkWidget *start_cmd, *resume_cmd, *connect_cmd;

static enum {
  LOGIN_TYPE, 
  NEW_PASSWORD_TYPE, 
  VERIFY_PASSWORD_TYPE,
  ENTER_PASSWORD_TYPE
} dialog_config;

static GtkWidget *imsg, *ilabel, *iinput, *ihost, *iport;
static GtkWidget *aifill, *aiskill;
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

static guint lan_timer;
static int num_lanservers_timer = 0;

/**************************************************************************
 really close and destroy the dialog.
**************************************************************************/
void really_close_connection_dialog(void)
{
  if (dialog) {
    gtk_widget_destroy(dialog);
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
  gtk_widget_set_sensitive(connect_cmd, TRUE);
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
  gtk_label_set_text_with_mnemonic(GTK_LABEL(ilabel), _("_Password:"));
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
      gtk_widget_set_sensitive(connect_cmd, FALSE);
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
    gtk_widget_set_sensitive(connect_cmd, FALSE);
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
static gboolean get_lanservers(gpointer data)
{
  struct server_list *server_list = get_lan_server_list();
  gchar *row[6];

  if (server_list != NULL) {
    gtk_list_store_clear(storelan);

    server_list_iterate(*server_list, pserver) {
      GtkTreeIter it;

      row[0] = pserver->name;
      row[1] = pserver->port;
      row[2] = pserver->version;
      row[3] = _(pserver->status);
      row[4] = pserver->players;
      row[5] = pserver->metastring;

      gtk_list_store_append(storelan, &it);
      gtk_list_store_set(storelan, &it,
		         0, row[0], 1, row[1], 2, row[2],
		         3, row[3], 4, row[4], 5, row[5], -1);
    } server_list_iterate_end;
  }

  num_lanservers_timer++;
  if (num_lanservers_timer == 50) {
    finish_lanserver_scan();
    num_lanservers_timer = 0;
    lan_timer = 0;
    return FALSE;
  }
  return TRUE;
}

/**************************************************************************
  This function updates the list widget with LAN servers.
**************************************************************************/
static void lan_update_callback(GtkWidget *w, gpointer data)
{
  if (num_lanservers_timer == 0) { 
    gtk_list_store_clear(storelan);

    if (begin_lanserver_scan()) {
      lan_timer = g_timeout_add(100, get_lanservers, NULL);
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
    gtk_widget_set_sensitive(resume_cmd, FALSE);
    return;
  }

  gtk_tree_model_get(GTK_TREE_MODEL(storesaved), &it, 0, &name, -1);

  sz_strlcpy(player_name, name);

  gtk_widget_set_sensitive(resume_cmd, TRUE);
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
  This function is called on a double-click in the single player saved list
***************************************************************************/
static void saved_click_callback(GtkTreeView *treeview,
                                 GtkTreePath *arg1,
                                 GtkTreeViewColumn *arg2,
                                 gpointer data)
{
  really_close_connection_dialog();
  send_start_saved_game();
}

/**************************************************************************
...
***************************************************************************/
static void meta_click_callback(GtkTreeView *treeview,
                                GtkTreePath *arg1,
                                GtkTreeViewColumn *arg2,
                                gpointer data)
{
  connect_callback(NULL, data);
}

/**************************************************************************
...
***************************************************************************/
static void lan_click_callback(GtkTreeView *treeview,
                               GtkTreePath *arg1,
                               GtkTreeViewColumn *arg2,
                               gpointer data)
{
  connect_callback(NULL, data);
}

/**************************************************************************
...
**************************************************************************/
static void connect_destroy_callback(GtkWidget *w, gpointer data)
{
  if (lan_timer != 0) {
    g_source_remove(lan_timer);
  }

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
  this regenerates the player information from a loaded game on the server.
**************************************************************************/
void handle_game_load(struct packet_game_load *packet)
{
  int i;

  if (!dialog) {
    return;
  }

  gtk_list_store_clear(storesaved);

  /* we couldn't load the savegame, we could have gotten the name wrong, etc */
  if (!packet->load_successful
      || strcmp(current_filename, packet->load_filename) != 0) {
    gtk_window_set_title(GTK_WINDOW(dialog), _("Couldn't load the savegame"));
    return;
  } else {
    char *buf = current_filename;
    
    buf = strrchr(current_filename, '/');
    if (buf) {
      /* First character after separator. */
      buf++;
    } else {
      /* No separator. */
      buf = current_filename;
    }
    gtk_window_set_title(GTK_WINDOW(dialog), buf);
  }

  game.nplayers = packet->nplayers;

  for (i = 0; i < packet->nplayers; i++) {
    GtkTreeIter iter;
    GdkPixbuf *flag;

    gtk_list_store_append(storesaved, &iter);
    gtk_list_store_set(storesaved, &iter, 
                       0, packet->name[i],
                       2, packet->nation_name[i],
                       3, packet->is_alive[i] ? _("Alive") : _("Dead"),
                       4, packet->is_ai[i] ? _("AI") : _("Human"), -1);

    /* set flag if we've got one to set. */
    if (strcmp(packet->nation_flag[i], "") != 0) {
      flag = get_flag(packet->nation_flag[i]);
      gtk_list_store_set(storesaved, &iter, 1, flag, -1);
      g_object_unref(flag);
    }
  }

  /* if nplayers is zero, we suppose it's a scenario */
  if (packet->load_successful && packet->nplayers == 0) {
    GtkTreeIter iter;
    char message[MAX_LEN_MSG];

    my_snprintf(message, sizeof(message), "/create %s", user_name);
    send_chat(message);
    my_snprintf(message, sizeof(message), "/ai %s", user_name);
    send_chat(message);
    my_snprintf(message, sizeof(message), "/take %s", user_name);
    send_chat(message);

    /* create a false entry */
    gtk_list_store_append(storesaved, &iter);
    gtk_list_store_set(storesaved, &iter,
                       0, user_name,
                       3, _("Alive"),
                       4, _("Human"), -1);
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

    if (current_filename) {
      free(current_filename);
    }

    filename = g_filename_to_utf8(
	gtk_file_selection_get_filename(GTK_FILE_SELECTION(w)),
	-1, NULL, NULL, NULL);
    current_filename = mystrdup(filename);

    if (is_save) {
      send_save_game(current_filename);
    } else {
      char message[MAX_LEN_MSG];

      gtk_window_set_title(GTK_WINDOW(dialog), _("Loading..."));

      my_snprintf(message, sizeof(message), "/load %s", current_filename);
      send_chat(message);
    }
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
   
  if (current_filename) {
    gtk_file_selection_set_filename(GTK_FILE_SELECTION(filesel),
	g_filename_from_utf8(current_filename, -1, NULL, NULL, NULL));
  }

  g_signal_connect(filesel, "response",
		   G_CALLBACK(filesel_response_callback), (gpointer)is_save);

  /* Display that dialog */
  gtk_window_present(GTK_WINDOW(filesel));

  return filesel;
}

/**************************************************************************
...
**************************************************************************/
static void filesel_destroy_callback(GtkWidget *w, gpointer data)
{
  gtk_widget_set_sensitive(load_cmd, TRUE);
}

/**************************************************************************
  callback for load button.
**************************************************************************/
static void load_callback(GtkWidget *w, gpointer data)
{
  /* start a server if we haven't already started one */
  if (is_server_running() || client_start_server()) {
    GtkWidget *filesel;

    gtk_widget_set_sensitive(resume_cmd, FALSE);
    gtk_widget_set_sensitive(load_cmd, FALSE);

    filesel = create_file_selection(_("Choose Saved Game to Load"), FALSE);

    g_signal_connect(filesel, "destroy",
		     G_CALLBACK(filesel_destroy_callback), NULL);
  }
}

/**************************************************************************
...
**************************************************************************/
static void reset_dialog(void)
{
  gtk_window_set_title(GTK_WINDOW(dialog), _("Welcome to Freeciv"));
  gtk_widget_hide(options_cmd);
  gtk_widget_hide(load_cmd);
  gtk_widget_hide(prev_cmd);
  gtk_widget_hide(next_cmd);
  gtk_widget_hide(start_cmd);
  gtk_widget_hide(resume_cmd);
  gtk_widget_hide(connect_cmd);
}

/**************************************************************************
...
**************************************************************************/
static void update_dialog(gint page_num)
{
  switch (page_num) {
  case FIRST_PAGE:
    reset_dialog();
    gtk_widget_show(next_cmd);
    break;
  case NEW_PAGE:
    gtk_window_set_title(GTK_WINDOW(dialog), _("Start New Game"));
    gtk_widget_show(options_cmd);
    gtk_widget_show(prev_cmd);
    gtk_widget_hide(next_cmd);
    gtk_widget_show(start_cmd);
    break;
  case LOAD_PAGE:
    gtk_window_set_title(GTK_WINDOW(dialog), _("Load Saved Game"));
    gtk_widget_show(load_cmd);
    gtk_widget_show(prev_cmd);
    gtk_widget_hide(next_cmd);
    gtk_widget_show(resume_cmd);
    break;
  case NETWORK_PAGE:
    gtk_window_set_title(GTK_WINDOW(dialog), _("Connect to Network Game"));
    gtk_widget_show(prev_cmd);
    gtk_widget_hide(next_cmd);
    gtk_widget_show(connect_cmd);
    break;
  }
}

/****************************************************************
 handles the uberbook
*****************************************************************/
static void switch_page_callback(GtkNotebook *notebook,
                                 GtkNotebookPage *page, gint page_num,
                                 gpointer data)
{
  update_dialog(page_num);
}

/**************************************************************************
...
**************************************************************************/
static void response_callback(GtkWidget *w, gint response_id)
{
  switch (response_id) {
  case CMD_OPTIONS:
    send_report_request(REPORT_SERVER_OPTIONS2);
    break;
  case CMD_LOAD:
    load_callback(load_cmd, NULL);
    break;
  case CMD_PREV:
    /* It will also kill internal server if there's one */
    disconnect_from_server();

    gtk_notebook_set_current_page(GTK_NOTEBOOK(uberbook), FIRST_PAGE);
    break;
  case CMD_NEXT:
    switch (next_page) {
    case NEW_PAGE:
      if (!is_server_running() && !client_start_server()) {
	return;
      }
      break;
    case LOAD_PAGE:
      load_callback(load_cmd, NULL);
      break;
    default:
      break;
    }

    /* check which radio button is active and switch "book" to that page */   
    gtk_notebook_set_current_page(GTK_NOTEBOOK(uberbook), next_page);
    break;
  case CMD_START:
    {
      char buf[512];

      my_snprintf(buf, sizeof(buf), "/%s", get_aiskill_setting());
      send_chat(buf);
      my_snprintf(buf, sizeof(buf), "/set aifill %d",
	  gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(aifill)));
      send_chat(buf);

      really_close_connection_dialog();
      send_chat("/start");
    }
    break;
  case CMD_RESUME:
    really_close_connection_dialog();
    send_start_saved_game();
    break;
  case CMD_CONNECT:
    connect_callback(connect_cmd, NULL);
    break;
  default:
    gtk_main_quit();
    break;
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

  if ((i = gtk_option_menu_get_history(GTK_OPTION_MENU(aiskill))) != -1) {
    return skill_level_names[i];
  } else {
    return /* TRANS: don't translate */ "normal";
  }
}

/**************************************************************************
...
**************************************************************************/
void gui_server_connect(void)
{
  GtkWidget *label, *table, *scrolled, *listsaved, *listmeta, *listlan;
  GtkWidget *hbox, *vbox, *updatemeta, *updatelan, *align, *menu, *item;
  GtkWidget *radio;
  int i;
  char buf[256];
  GtkCellRenderer *trenderer, *prenderer;
  GtkTreeSelection *selectionsaved, *selectionmeta, *selectionlan;
  GSList *group = NULL;
  GtkAdjustment *adj;

  if (dialog) {
    return;
  }

#ifdef CLIENT_CAN_LAUNCH_SERVER
  next_page = NEW_PAGE;
#else
  next_page = NETWORK_PAGE;
#endif
  dialog_config = LOGIN_TYPE;

  lan_timer = 0;

  dialog = gtk_dialog_new_with_buttons(NULL,
                                       NULL,
                                       GTK_DIALOG_DESTROY_WITH_PARENT,
                                       NULL);
  setup_dialog(dialog, toplevel);
  g_signal_connect(dialog, "destroy",
		   G_CALLBACK(connect_destroy_callback), NULL);
  g_signal_connect(dialog, "response",
		   G_CALLBACK(response_callback), NULL);

  /* create the action area buttons */
  options_cmd = gtk_stockbutton_new(GTK_STOCK_PREFERENCES, _("Game _Options"));
  gtk_dialog_add_action_widget(GTK_DIALOG(dialog), options_cmd, CMD_OPTIONS);
				      
  load_cmd = gtk_dialog_add_button(GTK_DIALOG(dialog),
      				   GTK_STOCK_OPEN, CMD_LOAD);

  prev_cmd = gtk_dialog_add_button(GTK_DIALOG(dialog),
      				   GTK_STOCK_GO_BACK, CMD_PREV);
  next_cmd = gtk_dialog_add_button(GTK_DIALOG(dialog),
      				   GTK_STOCK_GO_FORWARD, CMD_NEXT);
  
  start_cmd = gtk_dialog_add_button(GTK_DIALOG(dialog),
      				    _("_Start"), CMD_START);
  resume_cmd = gtk_dialog_add_button(GTK_DIALOG(dialog),
      				     _("_Resume"), CMD_RESUME);
  connect_cmd = gtk_dialog_add_button(GTK_DIALOG(dialog),
      				      _("_Connect"), CMD_CONNECT);

  gtk_dialog_add_button(GTK_DIALOG(dialog),
                    	GTK_STOCK_QUIT, GTK_RESPONSE_REJECT);
  reset_dialog();

  /* main body */
  uberbook = gtk_notebook_new();
  gtk_notebook_set_show_tabs(GTK_NOTEBOOK(uberbook), FALSE);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), uberbook, 
                     TRUE, TRUE, 0);
  

  /* first page of uber book */  
  align = gtk_alignment_new(0.5, 0.5, 0.0, 0.0);
  gtk_notebook_append_page(GTK_NOTEBOOK(uberbook), align, NULL);

  vbox = gtk_vbutton_box_new();
  gtk_container_add(GTK_CONTAINER(align), vbox);

#ifdef CLIENT_CAN_LAUNCH_SERVER
  radio = gtk_radio_button_new_with_mnemonic(group, _("_Start New Game"));
  group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio));
  gtk_container_add(GTK_CONTAINER(vbox), radio);

  g_signal_connect(radio, "toggled",G_CALLBACK(radio_command_callback),
                   GINT_TO_POINTER(NEW_PAGE));

  radio = gtk_radio_button_new_with_mnemonic(group, _("_Load Saved Game"));
  group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio));
  gtk_container_add(GTK_CONTAINER(vbox), radio);
  
  g_signal_connect(radio, "toggled",G_CALLBACK(radio_command_callback),
                   GINT_TO_POINTER(LOAD_PAGE));
#endif

  radio = gtk_radio_button_new_with_mnemonic(group,
      					     _("_Connect to Network Game"));
  gtk_container_add(GTK_CONTAINER(vbox), radio);

  g_signal_connect(radio, "toggled",G_CALLBACK(radio_command_callback),
                   GINT_TO_POINTER(NETWORK_PAGE));

  /* second page of uber book: new game */  
  vbox = gtk_vbox_new(FALSE, 2);
  gtk_container_set_border_width(GTK_CONTAINER(vbox), 6);
  gtk_notebook_append_page(GTK_NOTEBOOK(uberbook), vbox, NULL);

  hbox = gtk_hbox_new(FALSE, 12);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

  label = g_object_new(GTK_TYPE_LABEL,
                       "label", _("Number of players (Including AI):"),
                       "xalign", 0.0,
                       "yalign", 0.5,
                       NULL);
  gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);

  adj = GTK_ADJUSTMENT(gtk_adjustment_new(1, 1, MAX_NUM_PLAYERS, 1, 1, 1));
  aifill = gtk_spin_button_new(adj, 1, 0);
  gtk_spin_button_set_update_policy(GTK_SPIN_BUTTON(aifill), 
                                    GTK_UPDATE_IF_VALID);
  /* Default to aifill 5. */
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(aifill), 5);
  gtk_box_pack_start(GTK_BOX(hbox), aifill, FALSE, FALSE, 0);

  hbox = gtk_hbox_new(FALSE, 12);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

  label = g_object_new(GTK_TYPE_LABEL,
                       "label", _("AI skill level:"),
                       "xalign", 0.0,
                       "yalign", 0.5,
                       NULL);
  gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);

  aiskill = gtk_option_menu_new();
  menu = gtk_menu_new();
  for (i = 0; i < NUM_SKILL_LEVELS; i++) {
    item = gtk_menu_item_new_with_label(_(skill_level_names[i]));
    gtk_widget_show(item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
  }
  gtk_option_menu_set_menu(GTK_OPTION_MENU(aiskill), menu);
  gtk_box_pack_start(GTK_BOX(hbox), aiskill, FALSE, FALSE, 0);


  /* third page of uber book: load game */  
  vbox = gtk_vbox_new(FALSE, 2);
  gtk_container_set_border_width(GTK_CONTAINER(vbox), 6);
  gtk_notebook_append_page(GTK_NOTEBOOK(uberbook), vbox, NULL);

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

  label = g_object_new(GTK_TYPE_LABEL,
                       "use-underline", TRUE,
		       "mnemonic-widget", listsaved,
                       "label", _("Choose a _nation to play:"),
                       "xalign", 0.0,
                       "yalign", 0.5,
                       NULL);
  gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 2);
  
  scrolled = gtk_scrolled_window_new(NULL,NULL);
  gtk_container_add(GTK_CONTAINER(scrolled), listsaved);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled),
                                      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
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

  gtk_widget_show_all(uberbook);

  /* connect all the signals here, so that we can't send 
   * packets to the server until the dialog is up (which 
   * it may not be on very slow machines) */


  g_signal_connect(listsaved, "row_activated",
		   G_CALLBACK(saved_click_callback), NULL);
  g_signal_connect(selectionsaved, "changed",
                   G_CALLBACK(saved_list_callback), NULL);

  g_signal_connect(listmeta, "row_activated",
		   G_CALLBACK(meta_click_callback), NULL);
  g_signal_connect(selectionmeta, "changed",
                   G_CALLBACK(meta_list_callback), NULL);
  g_signal_connect(updatemeta, "clicked",
		   G_CALLBACK(meta_update_callback), NULL);

  g_signal_connect(listlan, "row_activated",
                   G_CALLBACK(lan_click_callback), NULL);
  g_signal_connect(selectionlan, "changed",
                   G_CALLBACK(lan_list_callback), NULL);
  g_signal_connect(updatelan, "clicked",
                   G_CALLBACK(lan_update_callback), NULL);

  g_signal_connect(uberbook, "switch-page",
                   G_CALLBACK(switch_page_callback), NULL);

  g_signal_connect(iinput, "activate", G_CALLBACK(connect_callback), NULL);
  g_signal_connect(ihost, "activate", G_CALLBACK(connect_callback), NULL);
  g_signal_connect(iport, "activate", G_CALLBACK(connect_callback), NULL);

  update_dialog(FIRST_PAGE);

  gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 250);
  gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER_ON_PARENT);

  if (!auto_connect) {
    gtk_window_present(GTK_WINDOW(dialog));
  }
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

    row[0] = pserver->name;
    row[1] = pserver->port;
    row[2] = pserver->version;
    row[3] = _(pserver->status);
    row[4] = pserver->players;
    row[5] = pserver->metastring;

    gtk_list_store_append(storemeta, &it);
    gtk_list_store_set(storemeta, &it,
		       0, row[0], 1, row[1], 2, row[2],
		       3, row[3], 4, row[4], 5, row[5], -1);
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
