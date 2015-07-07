/********************************************************************** 
 Freeciv - Copyright (C) 1996-2005 - Freeciv Development Team
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

#include <gtk/gtk.h>

/* utility */
#include "astring.h"
#include "support.h"

/* common */
#include "actions.h"
#include "game.h"
#include "traderoutes.h"
#include "movement.h"
#include "research.h"
#include "unit.h"
#include "unitlist.h"

/* client */
#include "dialogs_g.h"
#include "chatline.h"
#include "choice_dialog.h"
#include "client_main.h"
#include "climisc.h"
#include "connectdlg_common.h"
#include "control.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapview.h"
#include "packhand.h"

/* client/gui-gtk-2.0 */
#include "citydlg.h"
#include "dialogs.h"
#include "wldlg.h"

/* Locations for non action enabler controlled buttons. */
#define BUTTON_MOVE ACTION_MOVE
#define BUTTON_CANCEL BUTTON_MOVE + 1
#define BUTTON_COUNT BUTTON_MOVE + 2

#define BUTTON_NOT_THERE -1


static GtkWidget *act_sel_dialog;
static int action_button_map[BUTTON_COUNT];

static int actor_unit_id;
static int target_ids[ATK_COUNT];
static bool is_more_user_input_needed = FALSE;
static action_probability follow_up_act_probs[ACTION_COUNT];

static GtkWidget  *spy_tech_shell;

static GtkWidget  *spy_sabotage_shell;

/* A structure to hold parameters for actions inside the GUI in stead of
 * storing the needed data in a global variable. */
struct action_data {
  int actor_unit_id;
  int target_city_id;
  int target_unit_id;
  int target_tile_id;
  int value;
};

/****************************************************************
  Create a new action data structure that can be stored in the
  dialogs.
*****************************************************************/
static struct action_data *act_data(int actor_unit_id,
                                    int target_city_id,
                                    int target_unit_id,
                                    int target_tile_id,
                                    int value)
{
  struct action_data *data = fc_malloc(sizeof(*data));

  data->actor_unit_id = actor_unit_id;
  data->target_city_id = target_city_id;
  data->target_unit_id = target_unit_id;
  data->target_tile_id = target_tile_id;
  data->value = value;

  return data;
}

/**************************************************************************
  Move the queue of units that need user input forward unless the current
  unit are going to need more input.
**************************************************************************/
static void diplomat_queue_handle_primary(void)
{
  if (!is_more_user_input_needed) {
    choose_action_queue_next();
  }
}

/**************************************************************************
  Move the queue of diplomats that need user input forward since the
  current diplomat got the extra input that was required.
**************************************************************************/
static void diplomat_queue_handle_secondary(void)
{
  /* Stop waiting. Move on to the next queued diplomat. */
  is_more_user_input_needed = FALSE;
  diplomat_queue_handle_primary();
}

/****************************************************************
  User selected build city from the choice dialog
*****************************************************************/
static void found_city_callback(GtkWidget *w, gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  dsend_packet_city_name_suggestion_req(&client.conn,
                                        args->actor_unit_id);

  gtk_widget_destroy(act_sel_dialog);
  free(args);
}

/****************************************************************
  User selected join city from caravan dialog
*****************************************************************/
static void join_city_callback(GtkWidget *w, gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  if (NULL != game_unit_by_number(args->actor_unit_id)
      && NULL != game_city_by_number(args->target_city_id)) {
    request_do_action(ACTION_JOIN_CITY, args->actor_unit_id,
                      args->target_city_id, 0, "");
  }

  gtk_widget_destroy(act_sel_dialog);
  free(args);
}

/****************************************************************
  User selected enter market place from caravan dialog
*****************************************************************/
static void caravan_marketplace_callback(GtkWidget *w, gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  if (NULL != game_unit_by_number(args->actor_unit_id)
      && NULL != game_city_by_number(args->target_city_id)) {
    request_do_action(ACTION_MARKETPLACE, args->actor_unit_id,
                      args->target_city_id, 0, "");
  }

  gtk_widget_destroy(act_sel_dialog);
  free(args);
}

/****************************************************************
  User selected traderoute from caravan dialog
*****************************************************************/
static void caravan_establish_trade_callback(GtkWidget *w, gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  if (NULL != game_unit_by_number(args->actor_unit_id)
      && NULL != game_city_by_number(args->target_city_id)) {
    request_do_action(ACTION_TRADE_ROUTE, args->actor_unit_id,
                      args->target_city_id, 0, "");
  }

  gtk_widget_destroy(act_sel_dialog);
  free(args);
}

/****************************************************************
  User selected wonder building helping from caravan dialog
*****************************************************************/
static void caravan_help_build_wonder_callback(GtkWidget *w, gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  if (NULL != game_unit_by_number(args->actor_unit_id)
      && NULL != game_city_by_number(args->target_city_id)) {
    request_do_action(ACTION_HELP_WONDER, args->actor_unit_id,
                      args->target_city_id, 0, "");
  }

  gtk_widget_destroy(act_sel_dialog);
  free(args);
}

/**************************************************************************
  Returns a string with how many shields remains of the current production.
  This is useful as custom information on the help build wonder button.
**************************************************************************/
static const gchar *city_prod_remaining(struct city* destcity)
{
  if (destcity == NULL
      || city_owner(destcity) != client.conn.playing) {
    /* Can't give remaining production for a foreign or non existing
     * city. */
    return NULL;
  }

  return g_strdup_printf(_("%d remaining"),
                         impr_build_shield_cost(
                           destcity->production.value.building)
                         - destcity->shield_stock);
}

/**********************************************************************
  User responded to bribe dialog
**********************************************************************/
static void bribe_response(GtkWidget *w, gint response, gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  if (response == GTK_RESPONSE_YES) {
    request_do_action(ACTION_SPY_BRIBE_UNIT, args->actor_unit_id,
                      args->target_unit_id, 0, "");
  }

  gtk_widget_destroy(w);
  free(args);

  /* The user have answered the follow up question. Move on. */
  diplomat_queue_handle_secondary();
}

/****************************************************************
  Ask the server how much the bribe is
*****************************************************************/
static void diplomat_bribe_callback(GtkWidget *w, gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  if (NULL != game_unit_by_number(args->actor_unit_id)
      && NULL != game_unit_by_number(args->target_unit_id)) {
    request_action_details(ACTION_SPY_BRIBE_UNIT, args->actor_unit_id,
                           args->target_unit_id);
  }

  /* Wait for the server's reply before moving on to the next unit that
   * needs to know what action to take. */
  is_more_user_input_needed = TRUE;

  gtk_widget_destroy(act_sel_dialog);
  free(args);
}

/*************************************************************************
  Popup unit bribe dialog
**************************************************************************/
void popup_bribe_dialog(struct unit *actor, struct unit *punit, int cost)
{
  GtkWidget *shell;
  char buf[1024];

  fc_snprintf(buf, ARRAY_SIZE(buf), PL_("Treasury contains %d gold.",
                                        "Treasury contains %d gold.",
                                        client_player()->economic.gold),
              client_player()->economic.gold);

  if (cost <= client_player()->economic.gold) {
    shell = gtk_message_dialog_new(NULL, 0,
      GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
      /* TRANS: %s is pre-pluralised "Treasury contains %d gold." */
      PL_("Bribe unit for %d gold?\n%s",
          "Bribe unit for %d gold?\n%s", cost), cost, buf);
    gtk_window_set_title(GTK_WINDOW(shell), _("Bribe Enemy Unit"));
    setup_dialog(shell, toplevel);
  } else {
    shell = gtk_message_dialog_new(NULL, 0,
      GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
      /* TRANS: %s is pre-pluralised "Treasury contains %d gold." */
      PL_("Bribing the unit costs %d gold.\n%s",
          "Bribing the unit costs %d gold.\n%s", cost), cost, buf);
    gtk_window_set_title(GTK_WINDOW(shell), _("Traitors Demand Too Much!"));
    setup_dialog(shell, toplevel);
  }
  gtk_window_present(GTK_WINDOW(shell));
  
  g_signal_connect(shell, "response", G_CALLBACK(bribe_response),
                   act_data(actor->id, 0, punit->id, 0, cost));
}

/****************************************************************
  User selected sabotaging from choice dialog
*****************************************************************/
static void diplomat_sabotage_callback(GtkWidget *w, gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  if (NULL != game_unit_by_number(args->actor_unit_id)
      && NULL != game_city_by_number(args->target_city_id)) {
    request_do_action(ACTION_SPY_SABOTAGE_CITY, args->actor_unit_id,
                      args->target_city_id, B_LAST + 1, "");
  }

  gtk_widget_destroy(act_sel_dialog);
  free(args);
}

/****************************************************************
  User selected investigating from choice dialog
*****************************************************************/
static void diplomat_investigate_callback(GtkWidget *w, gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  if (NULL != game_city_by_number(args->target_city_id)
      && NULL != game_unit_by_number(args->actor_unit_id)) {
    request_do_action(ACTION_SPY_INVESTIGATE_CITY, args->actor_unit_id,
                      args->target_city_id, 0, "");
  }

  gtk_widget_destroy(act_sel_dialog);
  free(args);
}

/****************************************************************
  User selected unit sabotaging from choice dialog
*****************************************************************/
static void spy_sabotage_unit_callback(GtkWidget *w, gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  request_do_action(ACTION_SPY_SABOTAGE_UNIT, args->actor_unit_id,
                    args->target_unit_id, 0, "");

  gtk_widget_destroy(act_sel_dialog);
  free(args);
}

/****************************************************************
  User selected capture units from choice dialog
*****************************************************************/
static void capture_units_callback(GtkWidget *w, gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  request_do_action(ACTION_CAPTURE_UNITS, args->actor_unit_id,
                    args->target_tile_id, 0, "");

  gtk_widget_destroy(act_sel_dialog);
  free(args);
}

/****************************************************************
  User selected embassy establishing from choice dialog
*****************************************************************/
static void diplomat_embassy_callback(GtkWidget *w, gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  if (NULL != game_unit_by_number(args->actor_unit_id)
      && NULL != game_city_by_number(args->target_city_id)) {
    request_do_action(ACTION_ESTABLISH_EMBASSY, args->actor_unit_id,
                      args->target_city_id, 0, "");
  }

  gtk_widget_destroy(act_sel_dialog);
  free(args);
}

/****************************************************************
  User selected to steal gold from choice dialog
*****************************************************************/
static void spy_steal_gold_callback(GtkWidget *w, gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  if (NULL != game_unit_by_number(args->actor_unit_id)
      && NULL != game_city_by_number(args->target_city_id)) {
    request_do_action(ACTION_SPY_STEAL_GOLD, args->actor_unit_id,
                      args->target_city_id, 0, "");
  }

  gtk_widget_destroy(act_sel_dialog);
  free(args);
}

/****************************************************************
  User selected to steal maps from choice dialog
*****************************************************************/
static void spy_steal_maps_callback(GtkWidget *w, gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  if (NULL != game_unit_by_number(args->actor_unit_id)
      && NULL != game_city_by_number(args->target_city_id)) {
    request_do_action(ACTION_STEAL_MAPS, args->actor_unit_id,
                      args->target_city_id, 0, "");
  }

  gtk_widget_destroy(act_sel_dialog);
  free(args);
}

/****************************************************************
  User selected poisoning from choice dialog
*****************************************************************/
static void spy_poison_callback(GtkWidget *w, gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  if (NULL != game_unit_by_number(args->actor_unit_id)
      && NULL != game_city_by_number(args->target_city_id)) {
    request_do_action(ACTION_SPY_POISON, args->actor_unit_id,
                      args->target_city_id, 0, "");
  }

  gtk_widget_destroy(act_sel_dialog);
  free(args);
}

/****************************************************************
  User selected stealing from choice dialog
*****************************************************************/
static void diplomat_steal_callback(GtkWidget *w, gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  if (NULL != game_unit_by_number(args->actor_unit_id)
      && NULL != game_city_by_number(args->target_city_id)) {
    request_do_action(ACTION_SPY_STEAL_TECH, args->actor_unit_id,
                      args->target_city_id, A_UNSET, "");
  }

  gtk_widget_destroy(act_sel_dialog);
  free(args);
}

/****************************************************************
  User responded to steal advances dialog
*****************************************************************/
static void spy_advances_response(GtkWidget *w, gint response,
                                  gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  if (response == GTK_RESPONSE_ACCEPT && args->value > 0) {
    if (NULL != game_unit_by_number(args->actor_unit_id)
        && NULL != game_city_by_number(args->target_city_id)) {
      if (args->value == A_UNSET) {
        /* This is the untargeted version. */
        request_do_action(ACTION_SPY_STEAL_TECH,
                          args->actor_unit_id, args->target_city_id,
                          args->value, "");
      } else {
        /* This is the targeted version. */
        request_do_action(ACTION_SPY_TARGETED_STEAL_TECH,
                          args->actor_unit_id, args->target_city_id,
                          args->value, "");
      }
    }
  }

  gtk_widget_destroy(spy_tech_shell);
  spy_tech_shell = NULL;
  free(data);

  /* The user have answered the follow up question. Move on. */
  diplomat_queue_handle_secondary();
}

/****************************************************************
  User selected entry in steal advances dialog
*****************************************************************/
static void spy_advances_callback(GtkTreeSelection *select,
                                  gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  GtkTreeModel *model;
  GtkTreeIter it;

  if (gtk_tree_selection_get_selected(select, &model, &it)) {
    gtk_tree_model_get(model, &it, 1, &(args->value), -1);
    
    gtk_dialog_set_response_sensitive(GTK_DIALOG(spy_tech_shell),
      GTK_RESPONSE_ACCEPT, TRUE);
  } else {
    args->value = 0;
	  
    gtk_dialog_set_response_sensitive(GTK_DIALOG(spy_tech_shell),
      GTK_RESPONSE_ACCEPT, FALSE);
  }
}

/****************************************************************
  Create spy's tech stealing dialog
*****************************************************************/
static void create_advances_list(struct player *pplayer,
				 struct player *pvictim,
				 struct action_data *args)
{  
  GtkWidget *sw, *label, *vbox, *view;
  GtkListStore *store;
  GtkCellRenderer *rend;
  GtkTreeViewColumn *col;

  spy_tech_shell = gtk_dialog_new_with_buttons(_("Steal Technology"),
    NULL,
    0,
    GTK_STOCK_CANCEL,
    GTK_RESPONSE_CANCEL,
    _("_Steal"),
    GTK_RESPONSE_ACCEPT,
    NULL);
  setup_dialog(spy_tech_shell, toplevel);
  gtk_window_set_position(GTK_WINDOW(spy_tech_shell), GTK_WIN_POS_MOUSE);

  gtk_dialog_set_default_response(GTK_DIALOG(spy_tech_shell),
				  GTK_RESPONSE_ACCEPT);

  label = gtk_frame_new(_("Select Advance to Steal"));
  gtk_container_add(GTK_CONTAINER(GTK_DIALOG(spy_tech_shell)->vbox), label);

  vbox = gtk_vbox_new(FALSE, 6);
  gtk_container_add(GTK_CONTAINER(label), vbox);
      
  store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);

  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  g_object_unref(store);
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);

  rend = gtk_cell_renderer_text_new();
  col = gtk_tree_view_column_new_with_attributes(NULL, rend,
						 "text", 0, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

  label = g_object_new(GTK_TYPE_LABEL,
    "use-underline", TRUE,
    "mnemonic-widget", view,
    "label", _("_Advances:"),
    "xalign", 0.0,
    "yalign", 0.5,
    NULL);
  gtk_container_add(GTK_CONTAINER(vbox), label);
  
  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
				      GTK_SHADOW_ETCHED_IN);
  gtk_container_add(GTK_CONTAINER(sw), view);

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
    GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
  gtk_widget_set_size_request(sw, -1, 200);
  
  gtk_container_add(GTK_CONTAINER(vbox), sw);

  /* Now populate the list */
  if (pvictim) { /* you don't want to know what lag can do -- Syela */
    const struct research *presearch = research_get(pplayer);
    const struct research *vresearch = research_get(pvictim);
    GtkTreeIter it;
    GValue value = { 0, };

    advance_index_iterate(A_FIRST, i) {
      if (research_invention_state(vresearch, i) == TECH_KNOWN
          && (research_invention_state(presearch, i) == TECH_UNKNOWN
              || research_invention_state(presearch, i)
                 == TECH_PREREQS_KNOWN)) {
	gtk_list_store_append(store, &it);

	g_value_init(&value, G_TYPE_STRING);
        g_value_set_static_string(&value, research_advance_name_translation
                                              (presearch, i));
	gtk_list_store_set_value(store, &it, 0, &value);
	g_value_unset(&value);
	gtk_list_store_set(store, &it, 1, i, -1);
      }
    } advance_index_iterate_end;

    if (action_prob_possible(follow_up_act_probs[ACTION_SPY_STEAL_TECH])) {
      gtk_list_store_append(store, &it);

      g_value_init(&value, G_TYPE_STRING);
      {
        struct astring str = ASTRING_INIT;
        /* TRANS: %s is a unit name, e.g., Spy */
        astr_set(&str, _("At %s's Discretion"),
                 unit_name_translation(game_unit_by_number(
                                         args->actor_unit_id)));
        g_value_set_string(&value, astr_str(&str));
        astr_free(&str);
      }
      gtk_list_store_set_value(store, &it, 0, &value);
      g_value_unset(&value);
      gtk_list_store_set(store, &it, 1, A_UNSET, -1);
    }
  }

  gtk_dialog_set_response_sensitive(GTK_DIALOG(spy_tech_shell),
    GTK_RESPONSE_ACCEPT, FALSE);
  
  gtk_widget_show_all(GTK_DIALOG(spy_tech_shell)->vbox);

  g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)), "changed",
                   G_CALLBACK(spy_advances_callback), args);
  g_signal_connect(spy_tech_shell, "response",
                   G_CALLBACK(spy_advances_response), args);
  
  args->value = 0;

  gtk_tree_view_focus(GTK_TREE_VIEW(view));
}

/****************************************************************
  User has responded to spy's sabotage building dialog
*****************************************************************/
static void spy_improvements_response(GtkWidget *w, gint response, gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  if (response == GTK_RESPONSE_ACCEPT && args->value > -2) {
    if (NULL != game_unit_by_number(args->actor_unit_id)
        && NULL != game_city_by_number(args->target_city_id)) {
      if (args->value == B_LAST) {
        /* This is the untargeted version. */
        request_do_action(ACTION_SPY_SABOTAGE_CITY,
                          args->actor_unit_id,
                          args->target_city_id,
                          args->value + 1, "");
      } else {
        /* This is the targeted version. */
        request_do_action(ACTION_SPY_TARGETED_SABOTAGE_CITY,
                          args->actor_unit_id,
                          args->target_city_id,
                          args->value + 1, "");
      }
    }
  }

  gtk_widget_destroy(spy_sabotage_shell);
  spy_sabotage_shell = NULL;
  free(args);

  /* The user have answered the follow up question. Move on. */
  diplomat_queue_handle_secondary();
}

/****************************************************************
  User has selected new building from spy's sabotage dialog
*****************************************************************/
static void spy_improvements_callback(GtkTreeSelection *select, gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  GtkTreeModel *model;
  GtkTreeIter it;

  if (gtk_tree_selection_get_selected(select, &model, &it)) {
    gtk_tree_model_get(model, &it, 1, &(args->value), -1);
    
    gtk_dialog_set_response_sensitive(GTK_DIALOG(spy_sabotage_shell),
      GTK_RESPONSE_ACCEPT, TRUE);
  } else {
    args->value = -2;
	  
    gtk_dialog_set_response_sensitive(GTK_DIALOG(spy_sabotage_shell),
      GTK_RESPONSE_ACCEPT, FALSE);
  }
}

/****************************************************************
  Creates spy's building sabotaging dialog
*****************************************************************/
static void create_improvements_list(struct player *pplayer,
				     struct city *pcity,
				     struct action_data *args)
{  
  GtkWidget *sw, *label, *vbox, *view;
  GtkListStore *store;
  GtkCellRenderer *rend;
  GtkTreeViewColumn *col;
  GtkTreeIter it;
  
  spy_sabotage_shell = gtk_dialog_new_with_buttons(_("Sabotage Improvements"),
    NULL,
    0,
    GTK_STOCK_CANCEL,
    GTK_RESPONSE_CANCEL,
    _("_Sabotage"), 
    GTK_RESPONSE_ACCEPT,
    NULL);
  setup_dialog(spy_sabotage_shell, toplevel);
  gtk_window_set_position(GTK_WINDOW(spy_sabotage_shell), GTK_WIN_POS_MOUSE);

  gtk_dialog_set_default_response(GTK_DIALOG(spy_sabotage_shell),
				  GTK_RESPONSE_ACCEPT);

  label = gtk_frame_new(_("Select Improvement to Sabotage"));
  gtk_container_add(GTK_CONTAINER(GTK_DIALOG(spy_sabotage_shell)->vbox), label);

  vbox = gtk_vbox_new(FALSE, 6);
  gtk_container_add(GTK_CONTAINER(label), vbox);
      
  store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);

  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  g_object_unref(store);
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);

  rend = gtk_cell_renderer_text_new();
  col = gtk_tree_view_column_new_with_attributes(NULL, rend,
						 "text", 0, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

  label = g_object_new(GTK_TYPE_LABEL,
    "use-underline", TRUE,
    "mnemonic-widget", view,
    "label", _("_Improvements:"),
    "xalign", 0.0,
    "yalign", 0.5,
    NULL);
  gtk_container_add(GTK_CONTAINER(vbox), label);
  
  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
				      GTK_SHADOW_ETCHED_IN);
  gtk_container_add(GTK_CONTAINER(sw), view);

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
    GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
  gtk_widget_set_size_request(sw, -1, 200);
  
  gtk_container_add(GTK_CONTAINER(vbox), sw);

  /* Now populate the list */
  gtk_list_store_append(store, &it);
  gtk_list_store_set(store, &it, 0, _("City Production"), 1, -1, -1);

  city_built_iterate(pcity, pimprove) {
    if (pimprove->sabotage > 0) {
      gtk_list_store_append(store, &it);
      gtk_list_store_set(store, &it,
                         0, city_improvement_name_translation(pcity, pimprove),
                         1, improvement_number(pimprove),
                         -1);
    }  
  } city_built_iterate_end;

  if (action_prob_possible(
        follow_up_act_probs[ACTION_SPY_SABOTAGE_CITY])) {
    struct astring str = ASTRING_INIT;

    gtk_list_store_append(store, &it);

    /* TRANS: %s is a unit name, e.g., Spy */
    astr_set(&str, _("At %s's Discretion"),
             unit_name_translation(game_unit_by_number(args->actor_unit_id)));
    gtk_list_store_set(store, &it, 0, astr_str(&str), 1, B_LAST, -1);

    astr_free(&str);
  }

  gtk_dialog_set_response_sensitive(GTK_DIALOG(spy_sabotage_shell),
    GTK_RESPONSE_ACCEPT, FALSE);
  
  gtk_widget_show_all(GTK_DIALOG(spy_sabotage_shell)->vbox);

  g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)), "changed",
                   G_CALLBACK(spy_improvements_callback), args);
  g_signal_connect(spy_sabotage_shell, "response",
                   G_CALLBACK(spy_improvements_response), args);

  args->value = -2;
	  
  gtk_tree_view_focus(GTK_TREE_VIEW(view));
}

/****************************************************************
  Popup tech stealing dialog with list of possible techs
*****************************************************************/
static void spy_steal_popup(GtkWidget *w, gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  struct city *pvcity = game_city_by_number(args->target_city_id);
  struct player *pvictim = NULL;

  if (pvcity) {
    pvictim = city_owner(pvcity);
  }

/* it is concievable that pvcity will not be found, because something
has happened to the city during latency.  Therefore we must initialize
pvictim to NULL and account for !pvictim in create_advances_list. -- Syela */

  /* FIXME: Don't discard the second tech choice dialog. */
  if (!spy_tech_shell) {
    create_advances_list(client.conn.playing, pvictim, args);
    gtk_window_present(GTK_WINDOW(spy_tech_shell));
  } else {
    free(args);
  }

  /* Wait for the server's reply before moving on to the next unit that
   * needs to know what action to take. */
  is_more_user_input_needed = TRUE;

  gtk_widget_destroy(act_sel_dialog);
}

/****************************************************************
 Requests up-to-date list of improvements, the return of
 which will trigger the popup_sabotage_dialog() function.
*****************************************************************/
static void spy_request_sabotage_list(GtkWidget *w, gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  if (NULL != game_unit_by_number(args->actor_unit_id)
      && NULL != game_city_by_number(args->target_city_id)) {
    request_action_details(ACTION_SPY_TARGETED_SABOTAGE_CITY,
                           args->actor_unit_id,
                           args->target_city_id);
  }

  /* Wait for the server's reply before moving on to the next unit that
   * needs to know what action to take. */
  is_more_user_input_needed = TRUE;

  gtk_widget_destroy(act_sel_dialog);
  free(args);
}

/*************************************************************************
 Pops-up the Spy sabotage dialog, upon return of list of
 available improvements requested by the above function.
**************************************************************************/
void popup_sabotage_dialog(struct unit *actor, struct city *pcity)
{
  /* FIXME: Don't discard the second target choice dialog. */
  if (!spy_sabotage_shell) {
    create_improvements_list(client.conn.playing, pcity,
                             act_data(actor->id, pcity->id, 0, 0, 0));
    gtk_window_present(GTK_WINDOW(spy_sabotage_shell));
  }
}

/****************************************************************
...  Ask the server how much the revolt is going to cost us
*****************************************************************/
static void diplomat_incite_callback(GtkWidget *w, gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  if (NULL != game_unit_by_number(args->actor_unit_id)
      && NULL != game_city_by_number(args->target_city_id)) {
    request_action_details(ACTION_SPY_INCITE_CITY, args->actor_unit_id,
                           args->target_city_id);
  }

  /* Wait for the server's reply before moving on to the next unit that
   * needs to know what action to take. */
  is_more_user_input_needed = TRUE;

  gtk_widget_destroy(act_sel_dialog);
  free(args);
}

/************************************************************************
  User has responded to incite dialog
************************************************************************/
static void incite_response(GtkWidget *w, gint response, gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  if (response == GTK_RESPONSE_YES) {
    request_do_action(ACTION_SPY_INCITE_CITY, args->actor_unit_id,
                      args->target_city_id, 0, "");
  }

  gtk_widget_destroy(w);
  free(args);

  /* The user have answered the follow up question. Move on. */
  diplomat_queue_handle_secondary();
}

/*************************************************************************
Popup the yes/no dialog for inciting, since we know the cost now
**************************************************************************/
void popup_incite_dialog(struct unit *actor, struct city *pcity, int cost)
{
  GtkWidget *shell;
  char buf[1024];

  fc_snprintf(buf, ARRAY_SIZE(buf), PL_("Treasury contains %d gold.",
                                        "Treasury contains %d gold.",
                                        client_player()->economic.gold),
              client_player()->economic.gold);

  if (INCITE_IMPOSSIBLE_COST == cost) {
    shell = gtk_message_dialog_new(NULL,
      0,
      GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
      _("You can't incite a revolt in %s."),
      city_name(pcity));
    gtk_window_set_title(GTK_WINDOW(shell), _("City can't be incited!"));
  setup_dialog(shell, toplevel);
  } else if (cost <= client_player()->economic.gold) {
    shell = gtk_message_dialog_new(NULL, 0,
      GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
      /* TRANS: %s is pre-pluralised "Treasury contains %d gold." */
      PL_("Incite a revolt for %d gold?\n%s",
          "Incite a revolt for %d gold?\n%s", cost), cost, buf);
    gtk_window_set_title(GTK_WINDOW(shell), _("Incite a Revolt!"));
    setup_dialog(shell, toplevel);
  } else {
    shell = gtk_message_dialog_new(NULL,
      0,
      GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
      /* TRANS: %s is pre-pluralised "Treasury contains %d gold." */
      PL_("Inciting a revolt costs %d gold.\n%s",
          "Inciting a revolt costs %d gold.\n%s", cost), cost, buf);
    gtk_window_set_title(GTK_WINDOW(shell), _("Traitors Demand Too Much!"));
    setup_dialog(shell, toplevel);
  }
  gtk_window_present(GTK_WINDOW(shell));
  
  g_signal_connect(shell, "response", G_CALLBACK(incite_response),
                   act_data(actor->id, pcity->id, 0, 0, cost));
}


/**************************************************************************
  Callback from diplomat/spy dialog for "keep moving".
  (This should only occur when entering a tile that has an allied city or
  an allied unit.)
**************************************************************************/
static void diplomat_keep_moving_callback(GtkWidget *w, gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  struct unit *punit;
  struct tile *ptile;

  if ((punit = game_unit_by_number(args->actor_unit_id))
      && (ptile = index_to_tile(args->target_tile_id))
      && !same_pos(unit_tile(punit), ptile)) {
    request_do_action(ACTION_MOVE, args->actor_unit_id,
                      args->target_tile_id, 0, "");
  }

  gtk_widget_destroy(act_sel_dialog);
  free(args);
}

/****************************************************************
  Action selection dialog has been destroyed
*****************************************************************/
static void act_sel_destroy_callback(GtkWidget *w, gpointer data)
{
  act_sel_dialog = NULL;
  diplomat_queue_handle_primary();
}

/****************************************************************
  Action selection dialog has been canceled
*****************************************************************/
static void act_sel_cancel_callback(GtkWidget *w, gpointer data)
{
  gtk_widget_destroy(act_sel_dialog);
  free(data);
}

/****************************************************************
  Action selection dialog has been closed
*****************************************************************/
static void act_sel_close_callback(GtkWidget *w,
                                   gint response_id,
                                   gpointer data)
{
  gtk_widget_destroy(act_sel_dialog);
  free(data);
}

/* Mapping from an action to the function to call when its button is
 * pushed. */
static const GCallback af_map[ACTION_COUNT] = {
  /* Unit acting against a city target. */
  [ACTION_ESTABLISH_EMBASSY] = (GCallback)diplomat_embassy_callback,
  [ACTION_SPY_INVESTIGATE_CITY] = (GCallback)diplomat_investigate_callback,
  [ACTION_SPY_POISON] = (GCallback)spy_poison_callback,
  [ACTION_SPY_STEAL_GOLD] = (GCallback)spy_steal_gold_callback,
  [ACTION_STEAL_MAPS] = (GCallback)spy_steal_maps_callback,
  [ACTION_SPY_SABOTAGE_CITY] = (GCallback)diplomat_sabotage_callback,
  [ACTION_SPY_TARGETED_SABOTAGE_CITY] =
      (GCallback)spy_request_sabotage_list,
  [ACTION_SPY_STEAL_TECH] = (GCallback)diplomat_steal_callback,
  [ACTION_SPY_TARGETED_STEAL_TECH] = (GCallback)spy_steal_popup,
  [ACTION_SPY_INCITE_CITY] = (GCallback)diplomat_incite_callback,
  [ACTION_TRADE_ROUTE] = (GCallback)caravan_establish_trade_callback,
  [ACTION_MARKETPLACE] = (GCallback)caravan_marketplace_callback,
  [ACTION_HELP_WONDER] = (GCallback)caravan_help_build_wonder_callback,
  [ACTION_JOIN_CITY] = (GCallback)join_city_callback,

  /* Unit acting against a unit target. */
  [ACTION_SPY_BRIBE_UNIT] = (GCallback)diplomat_bribe_callback,
  [ACTION_SPY_SABOTAGE_UNIT] = (GCallback)spy_sabotage_unit_callback,

  /* Unit acting against all units at a tile. */
  [ACTION_CAPTURE_UNITS] = (GCallback)capture_units_callback,

  /* Unit acting against a tile. */
  [ACTION_FOUND_CITY] = (GCallback)found_city_callback,
};

/******************************************************************
  Show the user the action if it is enabled.
*******************************************************************/
static void action_entry(GtkWidget *shl,
                         int action_id,
                         const action_probability *action_probabilities,
                         const gchar *custom,
                         struct action_data *handler_args)
{
  const gchar *label;
  const gchar *tooltip;

  /* Don't show disabled actions. */
  if (!action_prob_possible(action_probabilities[action_id])) {
    return;
  }

  label = action_prepare_ui_name(action_id, "_",
                                 action_probabilities[action_id],
                                 custom);

  tooltip = action_get_tool_tip(action_id,
                                action_probabilities[action_id]);

  action_button_map[action_id] = choice_dialog_get_number_of_buttons(shl);
  choice_dialog_add(shl, label, af_map[action_id], handler_args, tooltip);
}

/******************************************************************
  Update an existing button.
*******************************************************************/
static void action_entry_update(GtkWidget *shl,
                                int action_id,
                                const action_probability *act_prob,
                                const gchar *custom,
                                struct action_data *handler_args)
{
  const gchar *label;
  const gchar *tooltip;

  /* An action that just became impossible has its button disabled.
   * An action that became possible again must be reenabled. */
  choice_dialog_button_set_sensitive(act_sel_dialog,
      action_button_map[action_id],
      action_prob_possible(act_prob[action_id]));

  /* The probability may have changed. */
  label = action_prepare_ui_name(action_id, "_",
                                 act_prob[action_id], custom);

  tooltip = action_get_tool_tip(action_id,
                                act_prob[action_id]);

  choice_dialog_button_set_label(act_sel_dialog,
                                 action_button_map[action_id],
                                 label);
  choice_dialog_button_set_tooltip(act_sel_dialog,
                                   action_button_map[action_id],
                                   tooltip);
}

/**************************************************************************
  Popup a dialog that allows the player to select what action a unit
  should take.
**************************************************************************/
void popup_action_selection(struct unit *actor_unit,
                            struct city *target_city,
                            struct unit *target_unit,
                            struct tile *target_tile,
                            const action_probability *act_probs)
{
  GtkWidget *shl;
  struct astring title = ASTRING_INIT, text = ASTRING_INIT;
  struct city *actor_homecity;

  int button_id;

  struct action_data *data =
      act_data(actor_unit->id,
               (target_city) ? target_city->id : 0,
               (target_unit) ? target_unit->id : 0,
               (target_tile) ? target_tile->index : 0,
               0);

  /* Could be caused by the server failing to reply to a request for more
   * information or a bug in the client code. */
  fc_assert_msg(!is_more_user_input_needed,
                "Diplomat queue problem. Is another diplomat window open?");

  /* No extra input is required as no action has been chosen yet. */
  is_more_user_input_needed = FALSE;

  /* No buttons are added yet. */
  for (button_id = 0; button_id < BUTTON_COUNT; button_id++) {
    action_button_map[button_id] = BUTTON_NOT_THERE;
  }

  actor_homecity = game_city_by_number(actor_unit->homecity);

  actor_unit_id = actor_unit->id;
  target_ids[ATK_CITY] = target_city ?
                         target_city->id :
                         IDENTITY_NUMBER_ZERO;
  target_ids[ATK_UNIT] = target_unit ?
                         target_unit->id :
                         IDENTITY_NUMBER_ZERO;
  target_ids[ATK_UNITS] = target_tile ?
                          tile_index(target_tile) :
                          IDENTITY_NUMBER_ZERO;
  target_ids[ATK_TILE] = target_tile ?
                         tile_index(target_tile) :
                         IDENTITY_NUMBER_ZERO;

  astr_set(&title,
           /* TRANS: %s is a unit name, e.g., Spy */
           _("Choose Your %s's Strategy"),
           unit_name_translation(actor_unit));

  if (target_city && actor_homecity) {
    astr_set(&text,
             _("Your %s from %s reaches the city of %s.\nWhat now?"),
             unit_name_translation(actor_unit),
             city_name(actor_homecity),
             city_name(target_city));
  } else if (target_city) {
    astr_set(&text,
             _("Your %s has arrived at %s.\nWhat is your command?"),
             unit_name_translation(actor_unit),
             city_name(target_city));
  } else if (target_unit) {
    astr_set(&text,
             /* TRANS: Your Spy is ready to act against Roman Freight. */
             _("Your %s is ready to act against %s %s."),
             unit_name_translation(actor_unit),
             nation_adjective_for_player(unit_owner(target_unit)),
             unit_name_translation(target_unit));
  } else {
    fc_assert_msg(target_unit || target_city || target_tile,
                  "No target specified.");
    astr_set(&text,
             /* TRANS: %s is a unit name, e.g., Diplomat, Spy */
             _("Your %s is waiting for your command."),
             unit_name_translation(actor_unit));
  }

  shl = choice_dialog_start(GTK_WINDOW(toplevel), astr_str(&title),
                            astr_str(&text));

  /* Spy/Diplomat acting against a city */

  action_iterate(act) {
    if (action_get_actor_kind(act) == AAK_UNIT
        && action_get_target_kind(act) == ATK_CITY) {
      action_entry(shl,
                   (enum gen_action)act,
                   act_probs,
                   act == ACTION_HELP_WONDER ?
                     city_prod_remaining(target_city) : NULL,
                   data);
    }
  } action_iterate_end;

  /* Spy/Diplomat acting against a unit */

  action_iterate(act) {
    if (action_get_actor_kind(act) == AAK_UNIT
        && action_get_target_kind(act) == ATK_UNIT) {
      action_entry(shl,
                   (enum gen_action)act,
                   act_probs,
                   NULL,
                   data);
    }
  } action_iterate_end;

  /* Spy/Diplomat acting against all units at a tile */

  action_iterate(act) {
    if (action_get_actor_kind(act) == AAK_UNIT
        && action_get_target_kind(act) == ATK_UNITS) {
      action_entry(shl,
                   (enum gen_action)act,
                   act_probs,
                   NULL,
                   data);
    }
  } action_iterate_end;

  /* Unit acting against a tile */

  action_iterate(act) {
    if (action_get_actor_kind(act) == AAK_UNIT
        && action_get_target_kind(act) == ATK_TILE) {
      action_entry(shl,
                   (enum gen_action)act,
                   act_probs,
                   NULL,
                   data);
    }
  } action_iterate_end;

  if (unit_can_move_to_tile(actor_unit, target_tile, FALSE)
      || (is_military_unit(actor_unit) || is_attack_unit(actor_unit))
      || (can_unit_bombard(actor_unit) && !is_ocean_tile(target_tile))) {
    action_button_map[BUTTON_MOVE] =
        choice_dialog_get_number_of_buttons(shl);
    choice_dialog_add(shl, _("_Keep moving"),
                      (GCallback)diplomat_keep_moving_callback,
                      data, NULL);
  }

  action_button_map[BUTTON_CANCEL] =
      choice_dialog_get_number_of_buttons(shl);
  choice_dialog_add(shl, GTK_STOCK_CANCEL,
                    (GCallback)act_sel_cancel_callback, data, NULL);

  choice_dialog_end(shl);

  act_sel_dialog = shl;

  choice_dialog_set_hide(shl, TRUE);
  g_signal_connect(shl, "destroy",
                   G_CALLBACK(act_sel_destroy_callback), NULL);
  g_signal_connect(shl, "delete_event",
                   G_CALLBACK(act_sel_close_callback), data);

  /* Give follow up questions access to action probabilities. */
  action_iterate(act) {
    follow_up_act_probs[act] = act_probs[act];
  } action_iterate_end;

  astr_free(&title);
  astr_free(&text);
}

/**************************************************************************
  Returns the id of the actor unit currently handled in action selection
  dialog when the action selection dialog is open.
  Returns IDENTITY_NUMBER_ZERO if no action selection dialog is open.
**************************************************************************/
int action_selection_actor_unit(void)
{
  if (act_sel_dialog == NULL) {
    return IDENTITY_NUMBER_ZERO;
  }
  return actor_unit_id;
}

/**************************************************************************
  Returns id of the target city of the actions currently handled in action
  selection dialog when the action selection dialog is open and it has a
  city target. Returns IDENTITY_NUMBER_ZERO if no action selection dialog
  is open or no city target is present in the action selection dialog.
**************************************************************************/
int action_selection_target_city(void)
{
  if (act_sel_dialog == NULL) {
    return IDENTITY_NUMBER_ZERO;
  }
  return target_ids[ATK_CITY];
}

/**************************************************************************
  Returns id of the target unit of the actions currently handled in action
  selection dialog when the action selection dialog is open and it has a
  unit target. Returns IDENTITY_NUMBER_ZERO if no action selection dialog
  is open or no unit target is present in the action selection dialog.
**************************************************************************/
int action_selection_target_unit(void)
{
  if (act_sel_dialog == NULL) {
    return IDENTITY_NUMBER_ZERO;
  }

  return target_ids[ATK_UNIT];
}

/**************************************************************************
  Updates the action selection dialog with new information.
**************************************************************************/
void action_selection_refresh(struct unit *actor_unit,
                              struct city *target_city,
                              struct unit *target_unit,
                              struct tile *target_tile,
                              const action_probability *act_prob)
{
  struct action_data *data;

  if (act_sel_dialog == NULL) {
    fc_assert_msg(act_sel_dialog != NULL,
                  "The action selection dialog should have been open");
    return;
  }

  if (actor_unit->id != action_selection_actor_unit()) {
    fc_assert_msg(actor_unit->id == action_selection_actor_unit(),
                  "The action selection dialog is for another actor unit.");
    return;
  }

  data = act_data(actor_unit->id,
                  (target_city) ? target_city->id : IDENTITY_NUMBER_ZERO,
                  (target_unit) ? target_unit->id : IDENTITY_NUMBER_ZERO,
                  (target_tile) ? target_tile->index : 0,
                  0);

  action_iterate(act) {
    const gchar *custom;

    if (action_get_actor_kind(act) != AAK_UNIT) {
      /* Not relevant. */
      continue;
    }

    if (action_prob_possible(act_prob[act])
        && act == ACTION_HELP_WONDER) {
      /* Add information about how far along the wonder is. */
      custom = city_prod_remaining(target_city);
    } else {
      custom = NULL;
    }

    if (BUTTON_NOT_THERE == action_button_map[act]) {
      /* Add the button (unless its probability is 0). */
      action_entry(act_sel_dialog, act, act_prob, custom, data);
    } else {
      /* Update the existing button. */
      action_entry_update(act_sel_dialog, act, act_prob, custom, data);
    }
  } action_iterate_end;

  if (BUTTON_NOT_THERE != action_button_map[BUTTON_CANCEL]) {
    /* Move the cancel button below the recently added button. */
    choice_dialog_button_move_to_the_end(act_sel_dialog,
        action_button_map[BUTTON_CANCEL]);

    /* DO NOT change action_button_map[BUTTON_CANCEL] or
     * the action_button_map[] for any other button to reflect the new
     * positions. A button keeps its choice dialog internal name when its
     * position changes. A button's id number is therefore based on when
     * it was added, not on its current position. */
  }

  choice_dialog_end(act_sel_dialog);
}

/****************************************************************
  Closes the diplomat dialog
****************************************************************/
void close_diplomat_dialog(void)
{
  if (act_sel_dialog != NULL) {
    gtk_widget_destroy(act_sel_dialog);
  }
}
