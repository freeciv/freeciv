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
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "fcintl.h"
#include "game.h"
#include "packets.h"
#include "nation.h"
#include "player.h"
#include "support.h"

#include "chatline.h"
#include "climisc.h"
#include "clinet.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "inteldlg.h"
#include "spaceshipdlg.h"
#include "colors.h"
#include "graphics.h"
#include "options.h"

#include "plrdlg.h"

static GtkWidget *players_dialog_shell;
static GtkWidget *players_list;
static GtkTreeSelection *players_selection;
static GtkWidget *players_int_command;
static GtkWidget *players_meet_command;
static GtkWidget *players_war_command;
static GtkWidget *players_vision_command;
static GtkWidget *players_sship_command;
static GtkListStore *store;

static void create_players_dialog(void);
static void players_meet_callback(GtkMenuItem *item, gpointer data);
static void players_war_callback(GtkMenuItem *item, gpointer data);
static void players_vision_callback(GtkMenuItem *item, gpointer data);
static void players_intel_callback(GtkMenuItem *item, gpointer data);
static void players_sship_callback(GtkMenuItem *item, gpointer data);

#define NUM_COLUMNS 12                /* number of columns in total */
#define DEF_SORT_COLUMN 2             /* default sort column (2 = nation) */
#define COLOR_COLUMN (NUM_COLUMNS)    /* color column */
#define PLRNO_COLUMN (NUM_COLUMNS+1)  /* plrno column */

/**************************************************************************
popup the dialog 10% inside the main-window 
**************************************************************************/
void popup_players_dialog(void)
{
  if(!players_dialog_shell){
    create_players_dialog();
    gtk_window_set_position(GTK_WINDOW(players_dialog_shell),GTK_WIN_POS_MOUSE);
  }
  gtk_window_present(GTK_WINDOW(players_dialog_shell));
}

/****************************************************************
 Closes the players dialog.
*****************************************************************/
void popdown_players_dialog(void)
{
  if (players_dialog_shell) {
    gtk_widget_destroy(players_dialog_shell);
  }
}

/**************************************************************************
...
**************************************************************************/
static void players_destroy_callback(GtkObject *object, gpointer data)
{
  players_dialog_shell = NULL;
}

/**************************************************************************
...
**************************************************************************/
static void update_players_menu(void)
{
  GtkTreeModel *model;
  GtkTreeIter it;

  if (gtk_tree_selection_get_selected(players_selection, &model, &it)) {
    struct player *plr;
    gint plrno;

    gtk_tree_model_get(model, &it, PLRNO_COLUMN, &plrno, -1);
    plr = &game.players[plrno];
  
    if (plr->spaceship.state != SSHIP_NONE) {
      gtk_widget_set_sensitive(players_sship_command, TRUE);
    } else {
      gtk_widget_set_sensitive(players_sship_command, FALSE);
    }

    switch (pplayer_get_diplstate(game.player_ptr, get_player(plrno))->type) {
    case DS_WAR:
    case DS_NO_CONTACT:
      gtk_widget_set_sensitive(players_war_command, FALSE);
      break;
    default:
      gtk_widget_set_sensitive(players_war_command, (game.player_idx != plrno));
    }

    gtk_widget_set_sensitive(players_vision_command,
      gives_shared_vision(game.player_ptr, plr));

    if (plr->is_alive
        && plr != game.player_ptr
        && player_has_embassy(game.player_ptr, plr)) {
      gtk_widget_set_sensitive(players_meet_command, plr->is_connected);
      gtk_widget_set_sensitive(players_int_command, TRUE);
      return;
    }
  }

  gtk_widget_set_sensitive(players_meet_command, FALSE);
  gtk_widget_set_sensitive(players_int_command, FALSE);
}

/**************************************************************************
...
**************************************************************************/
static void selection_callback(GtkTreeSelection *selection, gpointer data)
{
  update_players_menu();
}

/**************************************************************************
...
**************************************************************************/
void create_players_dialog(void)
{
  static char *titles[NUM_COLUMNS] = {
    N_("Name"),
    N_("Flag"),
    N_("Nation"),
    N_("Team"),
    N_("AI"),
    N_("Embassy"),
    N_("Dipl.State"),
    N_("Vision"),
    N_("Reputation"),
    N_("State"),
    N_("Host"),
    N_("Idle Turns")
  };
  static bool titles_done;

  static GType model_types[NUM_COLUMNS+2] = {
    G_TYPE_STRING,
    G_TYPE_NONE,
    G_TYPE_STRING,
    G_TYPE_STRING,
    G_TYPE_BOOLEAN,
    G_TYPE_STRING,
    G_TYPE_STRING,
    G_TYPE_STRING,
    G_TYPE_STRING,
    G_TYPE_STRING,
    G_TYPE_STRING,
    G_TYPE_INT,
    G_TYPE_NONE,
    G_TYPE_INT
  };

  int i;
  GtkAccelGroup *accel = gtk_accel_group_new();
  GtkWidget *sep, *sw;
  GtkWidget *menubar, *menu, *item;

  model_types[1] = GDK_TYPE_PIXBUF;
  model_types[COLOR_COLUMN] = GDK_TYPE_COLOR;

  intl_slist(ARRAY_SIZE(titles), titles, &titles_done);

  players_dialog_shell = gtk_dialog_new_with_buttons(_("Players"),
    NULL,
    0,
    GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
    NULL);
  if (dialogs_on_top) {
    gtk_window_set_transient_for(GTK_WINDOW(players_dialog_shell),
				 GTK_WINDOW(toplevel));
  }
  gtk_window_set_type_hint(GTK_WINDOW(players_dialog_shell),
			   GDK_WINDOW_TYPE_HINT_NORMAL);
  gtk_window_set_default_size(GTK_WINDOW(players_dialog_shell), -1, 270);

  g_signal_connect(players_dialog_shell, "destroy",
    G_CALLBACK(players_destroy_callback), NULL);
  g_signal_connect_swapped(players_dialog_shell, "response",
    G_CALLBACK(gtk_widget_destroy), GTK_OBJECT(players_dialog_shell));

  store = gtk_list_store_newv(ARRAY_SIZE(model_types), model_types);

  players_list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  g_object_unref(store);

  players_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(players_list));
  g_signal_connect(players_selection, "changed",
        G_CALLBACK(selection_callback), NULL);

  for (i = 0; i < NUM_COLUMNS; i++) {
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *col;

    if (model_types[i] == GDK_TYPE_PIXBUF) {
      renderer = gtk_cell_renderer_pixbuf_new();

      col = gtk_tree_view_column_new_with_attributes(titles[i], renderer,
        "pixbuf", i, NULL);
    } else if (model_types[i] == G_TYPE_BOOLEAN) {
      renderer = gtk_cell_renderer_toggle_new();

      col = gtk_tree_view_column_new_with_attributes(titles[i], renderer,
        "active", i, NULL);
    } else {
      renderer = gtk_cell_renderer_text_new();
      g_object_set(renderer,
	"weight", "bold",
	NULL);

      col = gtk_tree_view_column_new_with_attributes(titles[i], renderer,
        "text", i, "foreground-gdk", COLOR_COLUMN, NULL);
      gtk_tree_view_column_set_sort_column_id(col, i);
    }

    gtk_tree_view_append_column(GTK_TREE_VIEW(players_list), col);
  }

  gtk_tree_view_set_search_column(GTK_TREE_VIEW(players_list), DEF_SORT_COLUMN);

  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
				      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
		                 GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
  gtk_container_add(GTK_CONTAINER(sw), players_list);

  menubar = gtk_menu_bar_new();
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(players_dialog_shell)->vbox), menubar,
		     FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(players_dialog_shell)->vbox), sw,
		     TRUE, TRUE, 0);

  item = gtk_menu_item_new_with_mnemonic(_("_Player"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menubar), item);

  menu = gtk_menu_new();
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
  gtk_menu_set_accel_group(GTK_MENU(menu), accel);

  players_int_command = gtk_menu_item_new_with_mnemonic(_("_Intelligence"));
  gtk_widget_set_sensitive(players_int_command, FALSE);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), players_int_command);

  sep = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);

  players_meet_command = gtk_menu_item_new_with_mnemonic(_("_Meet"));
  gtk_widget_set_sensitive(players_meet_command, FALSE);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), players_meet_command);

  players_war_command = gtk_menu_item_new_with_mnemonic(_("_Cancel Treaty"));
  gtk_widget_set_sensitive(players_war_command, FALSE);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), players_war_command);

  players_vision_command=gtk_menu_item_new_with_mnemonic(_("_Withdraw vision"));
  gtk_widget_set_sensitive(players_vision_command, FALSE);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), players_vision_command);

  sep = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);

  players_sship_command = gtk_menu_item_new_with_mnemonic(_("_Spaceship"));
  gtk_widget_set_sensitive(players_sship_command, FALSE);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), players_sship_command);

  gtk_widget_add_accelerator(players_int_command,
    "activate", accel, GDK_I, 0, GTK_ACCEL_VISIBLE);
  gtk_widget_add_accelerator(players_meet_command,
    "activate", accel, GDK_M, 0, GTK_ACCEL_VISIBLE);
  gtk_widget_add_accelerator(players_war_command,
    "activate", accel, GDK_C, 0, GTK_ACCEL_VISIBLE);
  gtk_widget_add_accelerator(players_vision_command,
    "activate", accel, GDK_W, 0, GTK_ACCEL_VISIBLE);
  gtk_widget_add_accelerator(players_sship_command,
    "activate", accel, GDK_S, 0, GTK_ACCEL_VISIBLE);

  gtk_window_add_accel_group(GTK_WINDOW(players_dialog_shell), accel);
  gtk_widget_show_all(GTK_DIALOG(players_dialog_shell)->vbox);

  g_signal_connect(players_meet_command, "activate",
    G_CALLBACK(players_meet_callback), NULL);
  g_signal_connect(players_war_command, "activate",
    G_CALLBACK(players_war_callback), NULL);
  g_signal_connect(players_vision_command, "activate",
    G_CALLBACK(players_vision_callback), NULL);
  g_signal_connect(players_int_command, "activate",
    G_CALLBACK(players_intel_callback), NULL);
  g_signal_connect(players_sship_command, "activate",
    G_CALLBACK(players_sship_callback), NULL);

  gtk_list_store_clear(store);
  update_players_dialog();

  gtk_dialog_set_default_response(GTK_DIALOG(players_dialog_shell),
    GTK_RESPONSE_CLOSE);
}


/**************************************************************************
...
**************************************************************************/
#define MIN_DIMENSION 5

/* 
 * Builds the flag pixmap.
 */
static GdkPixbuf *get_flag(struct nation_type *nation)
{
  int x0, y0, x1, y1, w, h;
  GdkPixbuf *im;
  SPRITE *flag;

  flag = nation->flag_sprite;

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

  /* croping */
  im = gdk_pixbuf_get_from_drawable(NULL,
				    flag->pixmap,
				    gdk_colormap_get_system(),
				    x0, y0,
				    0, 0,
				    w, h);

  /* and finaly store the scaled flag pixbuf in the static flags array */
  return im;
}


/* 
 * Builds the text for the cells of a row in the player report. If
 * update is TRUE, only the changable entries are build.
 */
static void build_row(GtkTreeIter *it, int i)
{
  static char dsbuf[32];
  gchar *team, *state;
  const struct player_diplstate *pds;
  gint idle;
  struct player *plr = get_player(i);
  GdkPixbuf *flag;
  GdkColor *state_col;
  GValue value = { 0, };

  /* the team */
  if (plr->team != TEAM_NONE) {
    team = team_get_by_id(plr->team)->name;
  } else {
    team = "";
  }

  gtk_list_store_set(store, it,
    0, (gchar *)plr->name,   	      	      /* the playername */
    2, (gchar *)get_nation_name(plr->nation), /* the nation */
    3, (gchar *)team,
    PLRNO_COLUMN, (gint)i,    	      	      /* the playerid */
    -1);

  /* text for diplstate type and turns -- not applicable if this is me */
  if (i == game.player_idx) {
    strcpy(dsbuf, "-");
  } else {
    pds = pplayer_get_diplstate(game.player_ptr, plr);
    if (pds->type == DS_CEASEFIRE) {
      my_snprintf(dsbuf, sizeof(dsbuf), "%s (%d)",
		  diplstate_text(pds->type), pds->turns_left);
    } else {
      my_snprintf(dsbuf, sizeof(dsbuf), "%s", diplstate_text(pds->type));
    }
  }

  /* text for state */
  if (plr->is_alive) {
    if (plr->is_connected) {
      if (plr->turn_done) {
      	state = _("done");
      } else {
      	state = _("moving");
      }
    } else {
      state = "";
    }
  } else {
    state = _("R.I.P");
  }

  /* text for idleness */
  if (plr->nturns_idle > 3) {
    idle = plr->nturns_idle - 1;
  } else {
    idle = 0;
  }

  /* assemble the whole lot */
  g_value_init(&value, G_TYPE_STRING);
  g_value_set_static_string(&value, state);
  gtk_list_store_set_value(store, it, 9, &value);
  g_value_unset(&value);

  gtk_list_store_set(store, it,
     4, (gboolean)plr->ai.control,
     5, (gchar *)get_embassy_status(game.player_ptr, plr),
     6, (gchar *)dsbuf,
     7, (gchar *)get_vision_status(game.player_ptr, plr),
     8, (gchar *)reputation_text(plr->reputation),
    10, (gchar *)player_addr_hack(plr),   	      	    /* Fixme */
    11, (gint)idle,
    -1);

   /* set flag. */
   flag = get_flag(get_nation_by_plr(plr));
   gtk_list_store_set(store, it, 1, flag, -1);
   g_object_unref(flag);

   /* now add some eye candy ... */
   switch (pplayer_get_diplstate(game.player_ptr, plr)->type) {
   case DS_WAR:
     state_col = colors_standard[COLOR_STD_RED];
     break;
   case DS_ALLIANCE:
     state_col = colors_standard[COLOR_STD_GROUND];
     break;
   default:
     state_col = colors_standard[COLOR_STD_BLACK];
   }
   gtk_list_store_set(store, it, COLOR_COLUMN, state_col, -1);
}


/**************************************************************************
...
**************************************************************************/
void update_players_dialog(void)
{
  if (players_dialog_shell && !is_plrdlg_frozen()) {
    gboolean exists[MAX_NUM_PLAYERS];
    gint i;
    ITree it, it_next;

    for (i = 0; i < MAX_NUM_PLAYERS; i++) {
      exists[i] = FALSE;
    }

    itree_begin(GTK_TREE_MODEL(store), &it);
    while (!itree_end(&it)) {
      gint plrno;

      it_next = it;
      itree_next(&it_next);

      itree_get(&it, PLRNO_COLUMN, &plrno, -1);

      /*
       * The nation already had a row in the player report. In that
       * case we just update the row.
       */
      if (plrno >= 0 && plrno < game.nplayers) {
      	exists[plrno] = TRUE;

        build_row(&it.it, plrno);
      } else {
      	gtk_list_store_remove(store, &it.it);
      }

      it = it_next;
    }

    for (i = 0; i < game.nplayers; i++) {
      GtkTreeIter iter;

      /* skip barbarians */
      if (is_barbarian(&game.players[i])) {
	continue;
      }

      if (!exists[i]) {
	/* 
	 * A nation is not in the player report yet. This happens when
	 * the report is just opened and after a split.
	 */
      	gtk_list_store_append(store, &iter);

        build_row(&iter, i);
      }
    }

    update_players_menu();
  }
}

/**************************************************************************
...
**************************************************************************/
void players_meet_callback(GtkMenuItem *item, gpointer data)
{
  GtkTreeModel *model;
  GtkTreeIter it;
  gint plrno;

  if (!gtk_tree_selection_get_selected(players_selection, &model, &it))
    return;
  gtk_tree_model_get(model, &it, PLRNO_COLUMN, &plrno, -1);

  if(player_has_embassy(game.player_ptr, &game.players[plrno])) {
    struct packet_diplomacy_info pa;
  
    pa.plrno0=game.player_idx;
    pa.plrno1=plrno;
    pa.plrno_from=pa.plrno0;
    send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_INIT_MEETING,
        		       &pa);
  }
  else {
    append_output_window(_("Game: You need an embassy to "
			   "establish a diplomatic meeting."));
  }
}

/**************************************************************************
...
**************************************************************************/
void players_war_callback(GtkMenuItem *item, gpointer data)
{
  GtkTreeModel *model;
  GtkTreeIter it;

  if (gtk_tree_selection_get_selected(players_selection, &model, &it)) {
    struct packet_generic_integer pa;    
    gint plrno;

    gtk_tree_model_get(model, &it, PLRNO_COLUMN, &plrno, -1);

    pa.value = plrno;
    send_packet_generic_integer(&aconnection, PACKET_PLAYER_CANCEL_PACT,
				&pa);
  }
}

/**************************************************************************
...
**************************************************************************/
void players_vision_callback(GtkMenuItem *item, gpointer data)
{
  GtkTreeModel *model;
  GtkTreeIter it;

  if (gtk_tree_selection_get_selected(players_selection, &model, &it)) {
    struct packet_generic_integer pa;    
    gint plrno;

    gtk_tree_model_get(model, &it, PLRNO_COLUMN, &plrno, -1);

    pa.value = plrno;
    send_packet_generic_integer(&aconnection, PACKET_PLAYER_REMOVE_VISION,
				&pa);
  }
}

/**************************************************************************
...
**************************************************************************/
void players_intel_callback(GtkMenuItem *item, gpointer data)
{
  GtkTreeModel *model;
  GtkTreeIter it;
  gint plrno;

  if (!gtk_tree_selection_get_selected(players_selection, &model, &it))
    return;
  gtk_tree_model_get(model, &it, PLRNO_COLUMN, &plrno, -1);

  if(player_has_embassy(game.player_ptr, &game.players[plrno]))
    popup_intel_dialog(&game.players[plrno]);
}

/**************************************************************************
...
**************************************************************************/
void players_sship_callback(GtkMenuItem *item, gpointer data)
{
  GtkTreeModel *model;
  GtkTreeIter it;
  gint plrno;

  if (!gtk_tree_selection_get_selected(players_selection, &model, &it))
    return;
  gtk_tree_model_get(model, &it, PLRNO_COLUMN, &plrno, -1);

  popup_spaceship_dialog(&game.players[plrno]);
}
