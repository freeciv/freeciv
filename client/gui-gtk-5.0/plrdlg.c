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
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

/* utility */
#include "astring.h"
#include "fcintl.h"
#include "support.h"

/* common */
#include "diptreaty.h"
#include "packets.h"
#include "nation.h"
#include "player.h"

/* client */
#include "client_main.h"
#include "climisc.h"
#include "connectdlg_common.h"
#include "tilespec.h"
#include "colors.h"
#include "graphics.h"
#include "options.h"
#include "text.h"

/* client/gui-gtk-4.0 */
#include "chatline.h"
#include "dialogs.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "inteldlg.h"
#include "spaceshipdlg.h"
#include "colors.h"
#include "graphics.h"

#include "plrdlg.h"

struct gui_dialog *players_dialog_shell;
static GtkWidget *players_list;
static GtkTreeSelection *players_selection;
static GSimpleAction *players_int_command;
static GSimpleAction *players_meet_command;
static GSimpleAction *players_war_command;
static GSimpleAction *players_vision_command;
static GSimpleAction *players_sship_command;

static GtkListStore *players_dialog_store;
#define PLR_DLG_COL_STYLE       (0 + num_player_dlg_columns)
#define PLR_DLG_COL_WEIGHT      (1 + num_player_dlg_columns)
#define PLR_DLG_COL_ID          (2 + num_player_dlg_columns)
#define PLR_DLG_COL_TOOLTIP     (3 + num_player_dlg_columns)

#define PLR_DLG_COL_TOTAL       (PLR_DLG_COL_TOOLTIP + 1)

static void create_players_dialog(void);
static void players_meet_callback(GSimpleAction *action, GVariant *parameter,
                                  gpointer data);
static void players_war_callback(GSimpleAction *action, GVariant *parameter,
                                 gpointer data);
static void players_vision_callback(GSimpleAction *action, GVariant *parameter,
                                    gpointer data);
static void players_intel_callback(GSimpleAction *action, GVariant *parameter,
                                   gpointer data);
static void players_intel_wonder_callback(GSimpleAction *action,
                                          GVariant *parameter, gpointer data);
static void players_sship_callback(GSimpleAction *action, GVariant *parameter,
                                   gpointer data);
static void players_ai_toggle_callback(GSimpleAction *action, GVariant *parameter,
                                       gpointer data);
static void players_ai_skill_callback(GSimpleAction *action, GVariant *parameter,
                                      gpointer data);

static void update_views(void);

static GMenu *display_menu;

/**********************************************************************//**
  Popup the dialog 10% inside the main-window, and optionally raise it.
**************************************************************************/
void popup_players_dialog(bool raise)
{
  if (!players_dialog_shell) {
    create_players_dialog();
  }
  gui_dialog_present(players_dialog_shell);
  if (raise) {
    gui_dialog_raise(players_dialog_shell);
  }
}

/**********************************************************************//**
  Closes the players dialog.
**************************************************************************/
void popdown_players_dialog(void)
{
  if (players_dialog_shell) {
    gui_dialog_destroy(players_dialog_shell);
  }
}

/**********************************************************************//**
  Create a small colored square representing the player color, for use
  in player lists.
  May return NULL if the player has no color yet.
**************************************************************************/
GdkPixbuf *create_player_icon(const struct player *plr)
{
  int width = 20;
  int height = 20;
  GdkPixbuf *tmp;
  cairo_surface_t *surface;
  struct color *color;
  cairo_t *cr;

  if (!player_has_color(plr)) {
    return NULL;
  }

  surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);

  cr = cairo_create(surface);

  color = get_color(tileset, COLOR_PLAYER_COLOR_BACKGROUND);
  gdk_cairo_set_source_rgba(cr, &color->color);
  cairo_rectangle(cr, 0, 0, width, height);
  cairo_fill(cr);

  color = get_player_color(tileset, plr);
  gdk_cairo_set_source_rgba(cr, &color->color);
  cairo_rectangle(cr, 1, 1, width - 2, height - 2);
  cairo_fill(cr);

  cairo_destroy(cr);
  tmp = surface_get_pixbuf(surface, width, height);
  cairo_surface_destroy(surface);

  return tmp;
}

/**********************************************************************//**
  Refresh player menu
**************************************************************************/
static void update_players_menu(void)
{
  GtkTreeModel *model;
  GtkTreeIter it;

  if (gtk_tree_selection_get_selected(players_selection, &model, &it)) {
    struct player *plr;
    gint plrno;

    gtk_tree_model_get(model, &it, PLR_DLG_COL_ID, &plrno, -1);
    plr = player_by_number(plrno);

    if (plr->spaceship.state != SSHIP_NONE) {
      g_simple_action_set_enabled(players_sship_command, TRUE);
    } else {
      g_simple_action_set_enabled(players_sship_command, FALSE);
    }

    if (NULL != client.conn.playing) {
      /* We keep button sensitive in case of DIPL_SENATE_BLOCKING, so that player
       * can request server side to check requirements of those effects with omniscience */
      g_simple_action_set_enabled(players_war_command,
                                  can_client_issue_orders()
                                  && pplayer_can_cancel_treaty(client_player(),
                                                               player_by_number(plrno))
                                  != DIPL_ERROR);
    } else {
      g_simple_action_set_enabled(players_war_command, FALSE);
    }

    g_simple_action_set_enabled(players_vision_command,
                                can_client_issue_orders()
                                && gives_shared_vision(client.conn.playing, plr)
                                && !players_on_same_team(client.conn.playing, plr));

    g_simple_action_set_enabled(players_meet_command, can_meet_with_player(plr));
    g_simple_action_set_enabled(players_int_command, can_intel_with_player(plr));
    return;
  }

  g_simple_action_set_enabled(players_meet_command, FALSE);
  g_simple_action_set_enabled(players_int_command, FALSE);
}

/**********************************************************************//**
  Something selected from player menu
**************************************************************************/
static void selection_callback(GtkTreeSelection *selection, gpointer data)
{
  update_players_menu();
}

/**********************************************************************//**
  Left button pressed on player list
**************************************************************************/
static gboolean left_button_press_callback(GtkGestureClick *gesture, int n_press,
                                           double x, double y)
{
  GtkTreeView *view
    = GTK_TREE_VIEW(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)));

  if (n_press == 2) {
    GtkTreePath *path;

    gtk_tree_view_get_cursor(view, &path, NULL);
    if (path) {
      GtkTreeModel *model = gtk_tree_view_get_model(view);
      GtkTreeIter it;
      gint id;
      struct player *plr;

      gtk_tree_model_get_iter(model, &it, path);
      gtk_tree_path_free(path);

      gtk_tree_model_get(model, &it, PLR_DLG_COL_ID, &id, -1);
      plr = player_by_number(id);

      if (can_intel_with_player(plr)) {
        popup_intel_dialog(plr);
      }
    }
  }

  return FALSE;
}

/**********************************************************************//**
  Right button pressed on player list
**************************************************************************/
static gboolean right_button_press_callback(GtkGestureClick *gesture, int n_press,
                                            double x, double y)
{
  GtkTreeView *view
    = GTK_TREE_VIEW(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)));

  if (n_press == 2) {
    GtkTreePath *path;

    gtk_tree_view_get_cursor(view, &path, NULL);
    if (path) {
      GtkTreeModel *model = gtk_tree_view_get_model(view);
      GtkTreeIter it;
      gint id;
      struct player *plr;

      gtk_tree_model_get_iter(model, &it, path);
      gtk_tree_path_free(path);

      gtk_tree_model_get(model, &it, PLR_DLG_COL_ID, &id, -1);
      plr = player_by_number(id);

      if (can_meet_with_player(plr)) {
        dsend_packet_diplomacy_init_meeting_req(&client.conn, id);
      }
    }
  }

  return FALSE;
}

/**********************************************************************//**
  Sorting function for plr dlg.
**************************************************************************/
static gint plrdlg_sort_func(GtkTreeModel *model,
                             GtkTreeIter *a, GtkTreeIter *b, gpointer data)
{
  GValue value = { 0, };
  struct player *player1;
  struct player *player2;
  gint n;

  n = GPOINTER_TO_INT(data);

  gtk_tree_model_get_value(model, a, PLR_DLG_COL_ID, &value);
  player1 = player_by_number(g_value_get_int(&value));
  g_value_unset(&value);

  gtk_tree_model_get_value(model, b, PLR_DLG_COL_ID, &value);
  player2 = player_by_number(g_value_get_int(&value));
  g_value_unset(&value);

  return player_dlg_columns[n].sort_func(player1, player2);
}

/**********************************************************************//**
  Create a player dialog store.
**************************************************************************/
static GtkListStore *players_dialog_store_new(void)
{
  GtkListStore *store;
  GType model_types[num_player_dlg_columns + PLR_DLG_COL_TOTAL];
  int i;

  for (i = 0; i < num_player_dlg_columns; i++) {
    switch (player_dlg_columns[i].type) {
    case COL_FLAG:
      model_types[i] = GDK_TYPE_PIXBUF;
      break;
    case COL_COLOR:
      model_types[i] = GDK_TYPE_PIXBUF;
      break;
    case COL_BOOLEAN:
      model_types[i] = G_TYPE_BOOLEAN;
      break;
    case COL_TEXT:
    case COL_RIGHT_TEXT:
      model_types[i] = G_TYPE_STRING;
      break;
    }
  }

  /* Special (invisible rows) - Text style, weight and player id */
  model_types[i++] = G_TYPE_INT;        /* PLR_DLG_COL_STYLE. */
  model_types[i++] = G_TYPE_INT;        /* PLR_DLG_COL_WEIGHT. */
  model_types[i++] = G_TYPE_INT;        /* PLR_DLG_COL_ID. */
  model_types[i++] = G_TYPE_STRING;     /* PLR_DLG_COL_TOOLTIP */

  store = gtk_list_store_newv(i, model_types);

  /* Set sort order */
  for (i = 0; i < num_player_dlg_columns; i++) {
    if (player_dlg_columns[i].sort_func != NULL) {
        gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(store), i,
                                        plrdlg_sort_func, GINT_TO_POINTER(i),
                                        NULL);
    }
  }

  return store;
}

/************************************************************************//**
  Create up-to-date menu item for the plrdlg display menu.
  Caller need to g_object_unref() returned item.
****************************************************************************/
static GMenuItem *create_plrdlg_display_menu_item(int pos)
{
  GMenuItem *item;
  char act_name[50];
  struct player_dlg_column *pcol;

  pcol = &player_dlg_columns[pos];

  fc_snprintf(act_name, sizeof(act_name), "win.display%d(%s)",
              pos, pcol->show ? "true" : "false");
  item = g_menu_item_new(pcol->title, NULL);
  g_menu_item_set_detailed_action(item, act_name);

  return item;
}

/************************************************************************//**
  Create up-to-date menu item for "Dead Players" menu entry.
  Caller need to g_object_unref() returned item.
****************************************************************************/
static GMenuItem *create_dead_players_menu_item(void)
{
  GMenuItem *item;
  char act_name[50];

  /* TODO: Should this be gui-specific option as there's no universal
   *       player dlg setup? */
  fc_snprintf(act_name, sizeof(act_name), "win.show_dead(%s)",
              gui_options.player_dlg_show_dead_players ? "true" : "false");
  item = g_menu_item_new(Q_("?show:Dead Players"), NULL);
  g_menu_item_set_detailed_action(item, act_name);

  return item;
}

/**********************************************************************//**
  Toggled column visibility
**************************************************************************/
static void toggle_view(GSimpleAction *act, GVariant *value, gpointer data)
{
  int idx = GPOINTER_TO_INT(data);
  struct player_dlg_column *pcol = &player_dlg_columns[idx];

  pcol->show ^= 1;
  update_views();

  /* The menu has no 'playername' in the beginning, so menu index is one smaller
   * then column index. */
  g_menu_remove(display_menu, idx - 1);

  menu_item_insert_unref(display_menu, idx - 1, create_plrdlg_display_menu_item(idx));
}

/**********************************************************************//**
  Called whenever player toggles the 'Show/Dead Players' menu item
**************************************************************************/
static void toggle_dead_players(GSimpleAction *act, GVariant *value,
                                gpointer data)
{
  int idx = GPOINTER_TO_INT(data);

  gui_options.player_dlg_show_dead_players ^= 1;
  real_players_dialog_update(NULL);

  /* The menu has no 'playername' in the beginning, so menu index is one smaller
   * then column index - applies also to this even though this is not
   * a column. */
  g_menu_remove(display_menu, idx - 1);

  menu_item_insert_unref(display_menu, idx - 1,
                         create_dead_players_menu_item());
}

/**********************************************************************//**
  Create and return the "diplomacy" menu for the player report. This menu
  contains diplomacy actions the current player can use on other nations.
**************************************************************************/
static GMenu *create_diplomacy_menu(GActionGroup *group)
{
  GMenu *menu;
  GSimpleAction *act;

  menu = g_menu_new();

  act = g_simple_action_new("meet", NULL);
  g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
  g_signal_connect(act, "activate", G_CALLBACK(players_meet_callback), NULL);
  menu_item_append_unref(menu, g_menu_item_new(_("_Meet"), "win.meet"));
  players_meet_command = act;

  act = g_simple_action_new("cancel_treaty", NULL);
  g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
  g_signal_connect(act, "activate", G_CALLBACK(players_war_callback), NULL);
  menu_item_append_unref(menu, g_menu_item_new(_("Cancel _Treaty"),
                                               "win.cancel_treaty"));
  players_war_command = act;

  act = g_simple_action_new("withdraw_vision", NULL);
  g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
  g_signal_connect(act, "activate", G_CALLBACK(players_vision_callback), NULL);
  menu_item_append_unref(menu, g_menu_item_new(_("_Withdraw Vision"),
                                               "win.withdraw_vision"));
  players_vision_command = act;

  return menu;
}

/**********************************************************************//**
  Create and return the "intelligence" menu. The items in this menu are
  used by the player to see more detailed information about other nations.
**************************************************************************/
static GMenu *create_intelligence_menu(GActionGroup *group)
{
  GMenu *menu;
  GSimpleAction *act;

  menu = g_menu_new();

  act = g_simple_action_new("report", NULL);
  g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
  g_signal_connect(act, "activate", G_CALLBACK(players_intel_callback), NULL);
  menu_item_append_unref(menu, g_menu_item_new(_("_Report"), "win.report"));
  players_int_command = act;

  act = g_simple_action_new("wonders", NULL);
  g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
  g_signal_connect(act, "activate", G_CALLBACK(players_intel_wonder_callback), NULL);
  menu_item_append_unref(menu, g_menu_item_new(_("_Wonders"), "win.wonders"));

  act = g_simple_action_new("spaceship", NULL);
  g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
  g_signal_connect(act, "activate", G_CALLBACK(players_sship_callback), NULL);
  menu_item_append_unref(menu, g_menu_item_new(_("_Spaceship"), "win.spaceship"));
  players_sship_command = act;

  return menu;
}

/**********************************************************************//**
  Create 'show' menu for player dialog
**************************************************************************/
static GMenu *create_show_menu(GActionGroup *group)
{
  GVariantType *bvart = g_variant_type_new("b");
  int i;
  GSimpleAction *act;
  GVariant *var;

  display_menu = g_menu_new();

  /* Index starting at one (1) here to force playername to always be shown */
  for (i = 1; i < num_player_dlg_columns - 1; i++) {
    char act_name[50];

    var = g_variant_new("b", TRUE);
    fc_snprintf(act_name, sizeof(act_name), "display%d", i);
    act = g_simple_action_new_stateful(act_name, bvart, var);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
    g_signal_connect(act, "change-state", G_CALLBACK(toggle_view), GINT_TO_POINTER(i));

    menu_item_insert_unref(display_menu, i, create_plrdlg_display_menu_item(i));
  }

  var = g_variant_new("b", TRUE);
  act = g_simple_action_new_stateful("show_dead", bvart, var);
  g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));

  menu_item_insert_unref(display_menu, i, create_dead_players_menu_item());
  g_signal_connect(act, "change-state", G_CALLBACK(toggle_dead_players),
                   GINT_TO_POINTER(i));

  g_variant_type_free(bvart);

  return display_menu;
}

/**********************************************************************//**
  Create and return the "AI" menu, to adjust difficulty levels
  of players.
**************************************************************************/
static GMenu *create_ai_menu(GActionGroup *group)
{
  GMenu *menu;
  GSimpleAction *act;
  enum ai_level level;

  menu = g_menu_new();

  act = g_simple_action_new("ai_toggle", NULL);
  g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
  g_signal_connect(act, "activate",
                   G_CALLBACK(players_ai_toggle_callback), NULL);

  menu_item_append_unref(menu, g_menu_item_new(_("_Toggle AI Mode"),
                                               "win.ai_toggle"));

  for (level = 0; level < AI_LEVEL_COUNT; level++) {
    if (is_settable_ai_level(level)) {
      const char *level_name = ai_level_translated_name(level);
      char act_name[50];

      fc_snprintf(act_name, sizeof(act_name), "ai_level%d", level);
      act = g_simple_action_new(act_name, NULL);
      g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
      g_signal_connect(act, "activate",
                       G_CALLBACK(players_ai_skill_callback),
                       GUINT_TO_POINTER(level));

      fc_snprintf(act_name, sizeof(act_name), "win.ai_level%d", level);
      menu_item_append_unref(menu, g_menu_item_new(level_name, act_name));
    }
  }

  return menu;
}

/**********************************************************************//**
  Create all of player dialog
**************************************************************************/
void create_players_dialog(void)
{
  int i;
  GtkWidget *sep, *sw;
  GtkWidget *aux_menu;
  GMenu *topmenu, *submenu;
  GActionGroup *group;
  GtkWidget *vgrid;
  GtkEventController *left_controller;
  GtkEventController *right_controller;
  GtkGesture *gesture;
  int grid_row = 0;

  gui_dialog_new(&players_dialog_shell, GTK_NOTEBOOK(top_notebook), NULL,
                 TRUE);
  /* TRANS: Nations report title */
  gui_dialog_set_title(players_dialog_shell, _("Nations"));

  gui_dialog_add_button(players_dialog_shell, "window-close", _("_Close"),
                        GTK_RESPONSE_CLOSE);

  gui_dialog_set_default_size(players_dialog_shell, -1, 270);

  players_dialog_store = players_dialog_store_new();

  players_list = gtk_tree_view_new_with_model(GTK_TREE_MODEL
                                              (players_dialog_store));
  gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(players_list),
                                   PLR_DLG_COL_TOOLTIP);
  gtk_widget_set_hexpand(players_list, TRUE);
  gtk_widget_set_vexpand(players_list, TRUE);
  g_object_unref(players_dialog_store);
  gtk_widget_set_name(players_list, "small_font");

  players_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(players_list));
  g_signal_connect(players_selection, "changed",
      G_CALLBACK(selection_callback), NULL);
  gesture = gtk_gesture_click_new();
  left_controller = GTK_EVENT_CONTROLLER(gesture);
  g_signal_connect(left_controller, "pressed",
                   G_CALLBACK(left_button_press_callback), NULL);
  gtk_widget_add_controller(players_list, left_controller);
  gesture = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 3);
  right_controller = GTK_EVENT_CONTROLLER(gesture);
  g_signal_connect(right_controller, "pressed",
                   G_CALLBACK(right_button_press_callback), NULL);
  gtk_widget_add_controller(players_list, right_controller);

  for (i = 0; i < num_player_dlg_columns; i++) {
    struct player_dlg_column *pcol;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *col;

    pcol = &player_dlg_columns[i];
    col = NULL;

    switch (pcol->type) {
    case COL_FLAG:
      renderer = gtk_cell_renderer_pixbuf_new();

      col = gtk_tree_view_column_new_with_attributes(pcol->title,
         renderer, "pixbuf", i, NULL);
      break;
    case COL_BOOLEAN:
      renderer = gtk_cell_renderer_toggle_new();

      col = gtk_tree_view_column_new_with_attributes(pcol->title, renderer,
        "active", i, NULL);
      break;
    case COL_COLOR:
      renderer = gtk_cell_renderer_pixbuf_new();

      col = gtk_tree_view_column_new_with_attributes(pcol->title, renderer,
             "pixbuf", i, NULL);
      break;
    case COL_TEXT:
      renderer = gtk_cell_renderer_text_new();
      g_object_set(renderer, "style-set", TRUE, "weight-set", TRUE, NULL);

      col = gtk_tree_view_column_new_with_attributes(pcol->title, renderer,
                                                     "text", i,
                                                     "style", PLR_DLG_COL_STYLE,
                                                     "weight", PLR_DLG_COL_WEIGHT,
                                                     NULL);
      gtk_tree_view_column_set_sort_column_id(col, i);
      break;
    case COL_RIGHT_TEXT:
      renderer = gtk_cell_renderer_text_new();
      g_object_set(renderer, "style-set", TRUE, "weight-set", TRUE, NULL);

      col = gtk_tree_view_column_new_with_attributes(pcol->title, renderer,
                                                     "text", i,
                                                     "style", PLR_DLG_COL_STYLE,
                                                     "weight", PLR_DLG_COL_WEIGHT,
                                                     NULL);
      gtk_tree_view_column_set_sort_column_id(col, i);
      g_object_set(renderer, "xalign", 1.0, NULL);
      gtk_tree_view_column_set_alignment(col, 1.0);
      break;
    }

    if (col) {
      gtk_tree_view_append_column(GTK_TREE_VIEW(players_list), col);
    }
  }

  gtk_tree_view_set_search_column(GTK_TREE_VIEW(players_list),
                                  player_dlg_default_sort_column());

  sw = gtk_scrolled_window_new();
  gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(sw), TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), players_list);

  gui_dialog_add_content_widget(players_dialog_shell, sw);

  vgrid = gtk_grid_new();
  gtk_orientable_set_orientation(GTK_ORIENTABLE(vgrid),
                                 GTK_ORIENTATION_VERTICAL);

  sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_grid_attach(GTK_GRID(vgrid), sep, 0, grid_row++, 1, 1);

  aux_menu = aux_menu_new();
  gtk_grid_attach(GTK_GRID(vgrid), aux_menu, 0, grid_row++, 1, 1);

  gui_dialog_add_action_widget(players_dialog_shell, vgrid);

  group = G_ACTION_GROUP(g_simple_action_group_new());
  topmenu = g_menu_new();

  submenu = create_diplomacy_menu(group);
  submenu_append_unref(topmenu, _("Di_plomacy"), G_MENU_MODEL(submenu));

  submenu = create_intelligence_menu(group);
  submenu_append_unref(topmenu, _("_Intelligence"), G_MENU_MODEL(submenu));

  submenu = create_show_menu(group);
  submenu_append_unref(topmenu, _("_Display"), G_MENU_MODEL(submenu));

  submenu = create_ai_menu(group);
  submenu_append_unref(topmenu, _("_AI"), G_MENU_MODEL(submenu));

  gtk_widget_insert_action_group(aux_menu, "win", group);
  gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(aux_menu), G_MENU_MODEL(topmenu));

  gui_dialog_show_all(players_dialog_shell);

  real_players_dialog_update(NULL);

  gtk_tree_view_focus(GTK_TREE_VIEW(players_list));
}


/**************************************************************************
...
**************************************************************************/
#define MIN_DIMENSION 5

/**********************************************************************//**
  Builds the flag pixmap. May return NULL if there is not enough memory.
  You must call g_object_unref on the returned pixbuf when it is no
  longer needed.
**************************************************************************/
GdkPixbuf *get_flag(const struct nation_type *nation)
{
  int x0, y0, x1, y1, w, h;
  GdkPixbuf *im;
  struct sprite *flag;

  flag = get_nation_flag_sprite(tileset, nation);

  /* Calculate the bounding box ... */
  sprite_get_bounding_box(flag, &x0, &y0, &x1, &y1);

  fc_assert_ret_val(x0 != -1, NULL);
  fc_assert_ret_val(y0 != -1, NULL);
  fc_assert_ret_val(x1 != -1, NULL);
  fc_assert_ret_val(y1 != -1, NULL);

  w = (x1 - x0) + 1;
  h = (y1 - y0) + 1;

  /* If the flag is smaller then 5 x 5, something is wrong */
  fc_assert_ret_val(w >= MIN_DIMENSION && h >= MIN_DIMENSION, NULL);

  /* Croping */
  im = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, w, h);
  if (im != NULL) {
    GdkPixbuf *pixbuf = sprite_get_pixbuf(flag);

    gdk_pixbuf_copy_area(pixbuf, x0, y0, w, h,
                         im, 0, 0);
    g_object_unref(G_OBJECT(pixbuf));
  }

  /* Finally store the scaled flag pixbuf in the static flags array */
  return im;
}

/**********************************************************************//**
  Fills the player list with the information for 'pplayer' at the row
  given by 'it'.
**************************************************************************/
static void fill_row(GtkListStore *store, GtkTreeIter *it,
                     const struct player *pplayer)
{
  struct player_dlg_column *pcol;
  GdkPixbuf *pixbuf;
  int style = PANGO_STYLE_NORMAL, weight = PANGO_WEIGHT_NORMAL;
  int k;

  for (k = 0; k < num_player_dlg_columns; k++) {
    pcol = &player_dlg_columns[k];
    switch (pcol->type) {
    case COL_TEXT:
    case COL_RIGHT_TEXT:
      gtk_list_store_set(store, it, k, pcol->func(pplayer), -1);
      break;
    case COL_FLAG:
      pixbuf = get_flag(nation_of_player(pplayer));
      if (pixbuf != NULL) {
        gtk_list_store_set(store, it, k, pixbuf, -1);
        g_object_unref(pixbuf);
      }
      break;
    case COL_COLOR:
      pixbuf = create_player_icon(pplayer);
      if (pixbuf != NULL) {
        gtk_list_store_set(store, it, k, pixbuf, -1);
        g_object_unref(pixbuf);
      }
      break;
    case COL_BOOLEAN:
      gtk_list_store_set(store, it, k, pcol->bool_func(pplayer), -1);
      break;
    }
  }

  gtk_list_store_set(store, it, PLR_DLG_COL_TOOLTIP,
                     score_tooltip(pplayer, pplayer->score.game), -1);

   /* Now add some eye candy ... */
  if (client_has_player()) {
    switch (player_diplstate_get(client_player(), pplayer)->type) {
    case DS_WAR:
      weight = PANGO_WEIGHT_NORMAL;
      style = PANGO_STYLE_ITALIC;
      break;
    case DS_ALLIANCE:
    case DS_TEAM:
      weight = PANGO_WEIGHT_BOLD;
      style = PANGO_STYLE_NORMAL;
      break;
    case DS_ARMISTICE:
    case DS_CEASEFIRE:
    case DS_PEACE:
    case DS_NO_CONTACT:
      weight = PANGO_WEIGHT_NORMAL;
      style = PANGO_STYLE_NORMAL;
      break;
    case DS_LAST:
      break;
    }
  }

  gtk_list_store_set(store, it,
                     PLR_DLG_COL_STYLE, style,
                     PLR_DLG_COL_WEIGHT, weight,
                     PLR_DLG_COL_ID, player_number(pplayer),
                     -1);
}

/**********************************************************************//**
  Return TRUE if the player should be shown in the player list.
**************************************************************************/
static bool player_should_be_shown(const struct player *pplayer)
{
  return NULL != pplayer && (gui_options.player_dlg_show_dead_players
                             || pplayer->is_alive)
         && (!is_barbarian(pplayer));
}

/**********************************************************************//**
  Clear and refill the entire player list.
**************************************************************************/
void real_players_dialog_update(void *unused)
{
  GtkTreeModel *model;
  GtkTreeIter iter;
  int selected;

  if (NULL == players_dialog_shell) {
    return;
  }

  /* Save the selection. */
  if (gtk_tree_selection_get_selected(players_selection, &model, &iter)) {
    gtk_tree_model_get(model, &iter, PLR_DLG_COL_ID, &selected, -1);
  } else {
    selected = -1;
  }

  gtk_list_store_clear(players_dialog_store);
  players_iterate(pplayer) {
    if (!player_should_be_shown(pplayer)) {
      continue;
    }
    gtk_list_store_append(players_dialog_store, &iter);
    fill_row(players_dialog_store, &iter, pplayer);
    if (player_number(pplayer) == selected) {
      /* Restore the selection. */
      gtk_tree_selection_select_iter(players_selection, &iter);
    }
  } players_iterate_end;

  update_views();
}

/**********************************************************************//**
  Callback for diplomatic meetings button. This button is enabled iff
  we can meet with the other player.
**************************************************************************/
void players_meet_callback(GSimpleAction *action, GVariant *parameter,
                           gpointer data)
{
  GtkTreeModel *model;
  GtkTreeIter it;

  if (gtk_tree_selection_get_selected(players_selection, &model, &it)) {
    gint plrno;

    gtk_tree_model_get(model, &it, PLR_DLG_COL_ID, &plrno, -1);

    dsend_packet_diplomacy_init_meeting_req(&client.conn, plrno);
  }
}

/**********************************************************************//**
  Confirm pact/treaty cancellation.
  Frees strings passed in.
**************************************************************************/
static void confirm_cancel_pact(enum clause_type clause, int plrno,
                                char *title, char *question)
{
  GtkWidget *shell;

  shell = gtk_message_dialog_new(NULL, 0,
                                 GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
                                 "%s", question);
  gtk_window_set_title(GTK_WINDOW(shell), title);
  setup_dialog(shell, toplevel);
  gtk_dialog_set_default_response(GTK_DIALOG(shell), GTK_RESPONSE_NO);

  if (blocking_dialog(shell) == GTK_RESPONSE_YES) {
    dsend_packet_diplomacy_cancel_pact(&client.conn, plrno, clause);
  }
  gtk_window_destroy(GTK_WINDOW(shell));
  FC_FREE(title);
  FC_FREE(question);
}

/**********************************************************************//**
  Pact cancellation requested
**************************************************************************/
void players_war_callback(GSimpleAction *action, GVariant *parameter,
                          gpointer data)
{
  GtkTreeModel *model;
  GtkTreeIter it;

  if (gtk_tree_selection_get_selected(players_selection, &model, &it)) {
    struct astring title = ASTRING_INIT, question = ASTRING_INIT;
    gint plrno;
    struct player *aplayer;
    enum diplstate_type oldstate, newstate;

    gtk_tree_model_get(model, &it, PLR_DLG_COL_ID, &plrno, -1);
    aplayer = player_by_number(plrno);
    fc_assert_ret(aplayer != NULL);

    oldstate = player_diplstate_get(client_player(), aplayer)->type;
    newstate = cancel_pact_result(oldstate);

    /* TRANS: %s is a diplomatic state: "Cancel Cease-fire" */
    astr_set(&title, _("Cancel %s"), diplstate_type_translated_name(oldstate));

    if (newstate == DS_WAR) {
      astr_set(&question, _("Really declare war on the %s?"),
               nation_plural_for_player(aplayer));
    } else {
      /* TRANS: "Cancel Belgian Alliance? ... will be Armistice." */
      astr_set(&question, _("Cancel %s %s? New diplomatic state will be %s."),
               nation_adjective_for_player(aplayer),
               diplstate_type_translated_name(oldstate),
               diplstate_type_translated_name(newstate));
    }

    /* Can be any pact clause */
    confirm_cancel_pact(CLAUSE_CEASEFIRE, plrno,
                        astr_to_str(&title), astr_to_str(&question));
  }
}

/**********************************************************************//**
  Withdrawing shared vision
**************************************************************************/
void players_vision_callback(GSimpleAction *action, GVariant *parameter,
                             gpointer data)
{
  GtkTreeModel *model;
  GtkTreeIter it;

  if (gtk_tree_selection_get_selected(players_selection, &model, &it)) {
    struct astring question = ASTRING_INIT;
    gint plrno;
    struct player *aplayer;

    gtk_tree_model_get(model, &it, PLR_DLG_COL_ID, &plrno, -1);
    aplayer = player_by_number(plrno);
    fc_assert_ret(aplayer != NULL);

    /* TRANS: "...from the Belgians?" */
    astr_set(&question, _("Withdraw shared vision from the %s?"),
             nation_plural_for_player(aplayer));

    confirm_cancel_pact(CLAUSE_VISION, plrno,
                        fc_strdup(_("Withdraw Shared Vision")),
                        astr_to_str(&question));
  }
}

/**********************************************************************//**
  Intelligence report query
**************************************************************************/
void players_intel_callback(GSimpleAction *action, GVariant *parameter,
                            gpointer data)
{
  GtkTreeModel *model;
  GtkTreeIter it;

  if (gtk_tree_selection_get_selected(players_selection, &model, &it)) {
    gint plrno;

    gtk_tree_model_get(model, &it, PLR_DLG_COL_ID, &plrno, -1);

    if (can_intel_with_player(player_by_number(plrno))) {
      popup_intel_dialog(player_by_number(plrno));
    }
  }
}

/**********************************************************************//**
  Wonders list report query
**************************************************************************/
void players_intel_wonder_callback(GSimpleAction *action, GVariant *parameter,
                                   gpointer data)
{
  GtkTreeModel *model;
  GtkTreeIter it;

  if (gtk_tree_selection_get_selected(players_selection, &model, &it)) {
    gint plrno;

    gtk_tree_model_get(model, &it, PLR_DLG_COL_ID, &plrno, -1);

    popup_intel_wonder_dialog(player_by_number(plrno));
  }
}

/**********************************************************************//**
  Spaceship query callback
**************************************************************************/
void players_sship_callback(GSimpleAction *action, GVariant *parameter,
                            gpointer data)
{
  GtkTreeModel *model;
  GtkTreeIter it;

  if (gtk_tree_selection_get_selected(players_selection, &model, &it)) {
    gint plrno;

    gtk_tree_model_get(model, &it, PLR_DLG_COL_ID, &plrno, -1);
    popup_spaceship_dialog(player_by_number(plrno));
  }
}

/**********************************************************************//**
  AI toggle callback.
**************************************************************************/
static void players_ai_toggle_callback(GSimpleAction *action,
                                       GVariant *parameter,
                                       gpointer data)
{
  GtkTreeModel *model;
  GtkTreeIter it;

  if (gtk_tree_selection_get_selected(players_selection, &model, &it)) {
    gint plrno;

    gtk_tree_model_get(model, &it, PLR_DLG_COL_ID, &plrno, -1);

    send_chat_printf("/aitoggle \"%s\"", player_name(player_by_number(plrno)));
  }
}

/**********************************************************************//**
  AI skill level setting callback.
**************************************************************************/
static void players_ai_skill_callback(GSimpleAction *action,
                                      GVariant *parameter,
                                      gpointer data)
{
  GtkTreeModel *model;
  GtkTreeIter it;

  if (gtk_tree_selection_get_selected(players_selection, &model, &it)) {
    gint plrno;

    gtk_tree_model_get(model, &it, PLR_DLG_COL_ID, &plrno, -1);

    send_chat_printf("/%s %s",
                     ai_level_cmd(GPOINTER_TO_UINT(data)),
                     player_name(player_by_number(plrno)));
  }
}

/**********************************************************************//**
  Refresh players dialog views.
**************************************************************************/
static void update_views(void)
{
  int i;

  for (i = 0; i < num_player_dlg_columns; i++) {
    GtkTreeViewColumn *col;

    col = gtk_tree_view_get_column(GTK_TREE_VIEW(players_list), i);
    gtk_tree_view_column_set_visible(col, player_dlg_columns[i].show);
  }
}
