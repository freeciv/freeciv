/***********************************************************************
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
#include "specialist.h"
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
#include "text.h"

/* client/gui-gtk-5.0 */
#include "citydlg.h"
#include "dialogs.h"
#include "unitselextradlg.h"
#include "unitselunitdlg.h"
#include "wldlg.h"

/* Locations for non action enabler controlled buttons. */
#define BUTTON_NEW_UNIT_TGT (ACTION_COUNT + 1)
#define BUTTON_NEW_EXTRA_TGT (BUTTON_NEW_UNIT_TGT + 1)
#define BUTTON_LOCATION (BUTTON_NEW_EXTRA_TGT + 1)
#define BUTTON_WAIT (BUTTON_LOCATION + 1)
#define BUTTON_CANCEL (BUTTON_WAIT + 1)
#define BUTTON_COUNT (BUTTON_CANCEL + 1)

#define BUTTON_NOT_THERE -1


static GtkWidget *act_sel_dialog;
static int action_button_map[BUTTON_COUNT];

static int actor_unit_id;
static int target_ids[ATK_COUNT];
static int target_extra_id;
static bool is_more_user_input_needed = FALSE;
static bool did_not_decide = FALSE;
static bool action_selection_restart = FALSE;

static GtkWidget  *spy_tech_shell;

static GtkWidget  *spy_sabotage_shell;

/* A structure to hold parameters for actions inside the GUI instead of
 * storing the needed data in a global variable. */
struct action_data {
  action_id act_id;
  int actor_unit_id;
  int target_city_id;
  int target_unit_id;
  int target_tile_id;
  int target_building_id;
  int target_tech_id;
  int target_extra_id;
  int target_specialist_id;
};

/* TODO: Maybe this should be in the dialog itself? */
static struct action_data *act_sel_dialog_data;

#define FC_TYPE_ACTION_ROW (fc_action_row_get_type())

G_DECLARE_FINAL_TYPE(FcActionRow, fc_action_row, FC, ACTION_ROW, GObject)

struct _FcActionRow
{
  GObject parent_instance;

  char *name;
  int id;
};

struct _FcActionRowClass
{
  GObjectClass parent_class;
};

G_DEFINE_TYPE(FcActionRow, fc_action_row, G_TYPE_OBJECT)

/**********************************************************************//**
  Finalizing method for FcActionRow
**************************************************************************/
static void fc_action_row_finalize(GObject *gobject)
{
  FcActionRow *row = FC_ACTION_ROW(gobject);

  free(row->name);
  row->name = nullptr;

  G_OBJECT_CLASS(fc_action_row_parent_class)->finalize(gobject);
}

/**********************************************************************//**
  Initialization method for FcActionRow class
**************************************************************************/
static void
fc_action_row_class_init(FcActionRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->finalize = fc_action_row_finalize;
}

/**********************************************************************//**
  Initialization method for FcActionRow
**************************************************************************/
static void
fc_action_row_init(FcActionRow *self)
{
  self->name = nullptr;
}

/**********************************************************************//**
  FcActionRow creation method
**************************************************************************/
static FcActionRow *fc_action_row_new(void)
{
  FcActionRow *result;

  result = g_object_new(FC_TYPE_ACTION_ROW, nullptr);

  return result;
}

/**********************************************************************//**
  Create a new action data structure that can be stored in the
  dialogs.
**************************************************************************/
static struct action_data *act_data(action_id act_id,
                                    int actor_id,
                                    int target_city_id,
                                    int target_unit_id,
                                    int target_tile_id,
                                    int target_building_id,
                                    int target_tech_id,
                                    int target_specialist_id,
                                    int tgt_extra_id)
{
  struct action_data *data = fc_malloc(sizeof(*data));

  data->act_id = act_id;
  data->actor_unit_id = actor_id;
  data->target_city_id = target_city_id;
  data->target_unit_id = target_unit_id;
  data->target_tile_id = target_tile_id;
  data->target_building_id = target_building_id;
  data->target_tech_id = target_tech_id;
  data->target_specialist_id = target_specialist_id;
  data->target_extra_id = tgt_extra_id;

  return data;
}

/**********************************************************************//**
  Move the queue of units that need user input forward unless the current
  unit is going to need more input.
**************************************************************************/
static void diplomat_queue_handle_primary(void)
{
  if (!is_more_user_input_needed) {
    /* The client isn't waiting for information for any unanswered follow
     * up questions. */

    struct unit *actor_unit;

    if ((actor_unit = game_unit_by_number(actor_unit_id))) {
      /* The action selection dialog wasn't closed because the actor unit
       * was lost. */

      /* The probabilities didn't just disappear, right? */
      fc_assert_action(actor_unit->client.act_prob_cache,
                       client_unit_init_act_prob_cache(actor_unit));

      FC_FREE(actor_unit->client.act_prob_cache);
    }

    if (action_selection_restart) {
      /* The action selection dialog was closed but only so it can be
       * redrawn with fresh data. */

      action_selection_restart = FALSE;
    } else {
      /* The action selection process is over, at least for now. */
      action_selection_no_longer_in_progress(actor_unit_id);
    }

    if (did_not_decide) {
      /* The action selection dialog was closed but the player didn't
       * decide what the unit should do. */

      /* Reset so the next action selection dialog does the right thing. */
      did_not_decide = FALSE;
    } else {
      /* An action, or no action at all, was selected. */
      action_decision_clear_want(actor_unit_id);
      action_selection_next_in_focus(actor_unit_id);
    }
  }
}

/**********************************************************************//**
  Move the queue of units that need user input forward since the
  current unit doesn't require the extra input any more.
**************************************************************************/
static void diplomat_queue_handle_secondary(void)
{
  /* Stop waiting. Move on to the next queued unit. */
  is_more_user_input_needed = FALSE;
  diplomat_queue_handle_primary();
}

/**********************************************************************//**
  Let the non shared client code know that the action selection process
  no longer is in progress for the specified unit.

  This allows the client to clean up any client specific assumptions.
**************************************************************************/
void action_selection_no_longer_in_progress_gui_specific(int actor_id)
{
  /* Stop assuming the answer to a follow up question will arrive. */
  is_more_user_input_needed = FALSE;
}

/**********************************************************************//**
  Get the non targeted version of an action so it, if enabled, can appear
  in the target selection dialog.
**************************************************************************/
static action_id get_non_targeted_action_id(action_id tgt_action_id)
{
  /* Don't add an action mapping here unless the non targeted version is
   * selectable in the targeted version's target selection dialog. */
  switch ((enum gen_action)tgt_action_id) {
  case ACTION_SPY_TARGETED_SABOTAGE_CITY:
    return ACTION_SPY_SABOTAGE_CITY;
  case ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC:
    return ACTION_SPY_SABOTAGE_CITY_ESC;
  case ACTION_SPY_TARGETED_STEAL_TECH:
    return ACTION_SPY_STEAL_TECH;
  case ACTION_SPY_TARGETED_STEAL_TECH_ESC:
    return ACTION_SPY_STEAL_TECH_ESC;
  default:
    /* No non targeted version found. */
    return ACTION_NONE;
  }
}

/**********************************************************************//**
  Get the production targeted version of an action so it, if enabled, can
  appear in the target selection dialog.
**************************************************************************/
static action_id get_production_targeted_action_id(action_id tgt_action_id)
{
  /* Don't add an action mapping here unless the non targeted version is
   * selectable in the targeted version's target selection dialog. */
  switch ((enum gen_action)tgt_action_id) {
  case ACTION_SPY_TARGETED_SABOTAGE_CITY:
    return ACTION_SPY_SABOTAGE_CITY_PRODUCTION;
  case ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC:
    return ACTION_SPY_SABOTAGE_CITY_PRODUCTION_ESC;
  case ACTION_STRIKE_BUILDING:
    return ACTION_STRIKE_PRODUCTION;
  default:
    /* No non targeted version found. */
    return ACTION_NONE;
  }
}

/**********************************************************************//**
  User selected an action from the choice dialog and the action has no
  special needs.
**************************************************************************/
static void simple_action_callback(GtkWidget *w, gpointer data)
{
  int actor_id, target_id, sub_target;
  struct action *paction;

  struct action_data *args = act_sel_dialog_data;

  bool failed = FALSE;

  /* Data */
  args->act_id = GPOINTER_TO_INT(data);
  paction = action_by_number(args->act_id);

  /* Actor */
  fc_assert(action_get_actor_kind(paction) == AAK_UNIT);
  actor_id = args->actor_unit_id;
  if (NULL == game_unit_by_number(actor_id)) {
    /* Probably dead. */
    failed = TRUE;
  }

  /* Target */
  target_id = IDENTITY_NUMBER_ZERO;
  switch (action_get_target_kind(paction)) {
  case ATK_CITY:
    target_id = args->target_city_id;
    if (NULL == game_city_by_number(target_id)) {
      /* Probably destroyed. */
      failed = TRUE;
    }
    break;
  case ATK_UNIT:
    target_id = args->target_unit_id;
    if (NULL == game_unit_by_number(target_id)) {
      /* Probably dead. */
      failed = TRUE;
    }
    break;
  case ATK_STACK:
  case ATK_TILE:
  case ATK_EXTRAS:
    target_id = args->target_tile_id;
    if (NULL == index_to_tile(&(wld.map), target_id)) {
      /* TODO: Should this be possible at all? If not: add assertion. */
      failed = TRUE;
    }
    break;
  case ATK_SELF:
    target_id = IDENTITY_NUMBER_ZERO;
    break;
  case ATK_COUNT:
    fc_assert(action_get_target_kind(paction) != ATK_COUNT);
    failed = TRUE;
  }

  /* Sub target. */
  sub_target = NO_TARGET;
  if (paction->target_complexity != ACT_TGT_COMPL_SIMPLE) {
    switch (action_get_sub_target_kind(paction)) {
    case ASTK_BUILDING:
      sub_target = args->target_building_id;
      if (NULL == improvement_by_number(sub_target)) {
        /* Did the ruleset change? */
        failed = TRUE;
      }
      break;
    case ASTK_TECH:
      sub_target = args->target_tech_id;
      if (NULL == valid_advance_by_number(sub_target)) {
        /* Did the ruleset change? */
        failed = TRUE;
      }
      break;
    case ASTK_EXTRA:
    case ASTK_EXTRA_NOT_THERE:
      /* TODO: Validate if the extra is there? */
      sub_target = args->target_extra_id;
      if (NULL == extra_by_number(sub_target)) {
        /* Did the ruleset change? */
        failed = TRUE;
      }
      break;
    case ASTK_SPECIALIST:
      sub_target = args->target_specialist_id;
      if (nullptr == specialist_by_number(sub_target)) {
        /* Did the ruleset change? */
        failed = TRUE;
      }
      break;
    case ASTK_NONE:
    case ASTK_COUNT:
      /* Shouldn't happen. */
      fc_assert(action_get_sub_target_kind(paction) != ASTK_NONE);
      failed = TRUE;
      break;
    }
  }

  /* Send request. */
  if (!failed) {
    request_do_action(paction->id, actor_id, target_id, sub_target, "");
  }

  /* Clean up. */
  choice_dialog_destroy(act_sel_dialog);
  /* No follow up questions. */
  act_sel_dialog_data = NULL;
  FC_FREE(args);
}

/**********************************************************************//**
  User selected an action from the choice dialog that needs details from
  the server.
**************************************************************************/
static void request_action_details_callback(GtkWidget *w, gpointer data)
{
  int actor_id, target_id;
  struct action *paction;

  struct action_data *args = act_sel_dialog_data;

  bool failed = FALSE;

  /* Data */
  args->act_id = GPOINTER_TO_INT(data);
  paction = action_by_number(args->act_id);

  /* Actor */
  fc_assert(action_get_actor_kind(paction) == AAK_UNIT);
  actor_id = args->actor_unit_id;
  if (NULL == game_unit_by_number(actor_id)) {
    /* Probably dead. */
    failed = TRUE;
  }

  /* Target */
  target_id = IDENTITY_NUMBER_ZERO;
  switch (action_get_target_kind(paction)) {
  case ATK_CITY:
    target_id = args->target_city_id;
    if (NULL == game_city_by_number(target_id)) {
      /* Probably destroyed. */
      failed = TRUE;
    }
    break;
  case ATK_UNIT:
    target_id = args->target_unit_id;
    if (NULL == game_unit_by_number(target_id)) {
      /* Probably dead. */
      failed = TRUE;
    }
    break;
  case ATK_STACK:
  case ATK_TILE:
  case ATK_EXTRAS:
    target_id = args->target_tile_id;
    if (NULL == index_to_tile(&(wld.map), target_id)) {
      /* TODO: Should this be possible at all? If not: add assertion. */
      failed = TRUE;
    }
    break;
  case ATK_SELF:
    target_id = IDENTITY_NUMBER_ZERO;
    break;
  case ATK_COUNT:
    fc_assert(action_get_target_kind(paction) != ATK_COUNT);
    failed = TRUE;
  }

  /* Send request. */
  if (!failed) {
    request_action_details(paction->id, actor_id, target_id);
  }

  /* Wait for the server's reply before moving on to the next unit that
   * needs to know what action to take. */
  is_more_user_input_needed = TRUE;

  /* Clean up. */
  choice_dialog_destroy(act_sel_dialog);
  /* No client side follow up questions. */
  act_sel_dialog_data = NULL;
  FC_FREE(args);
}

/**********************************************************************//**
  User selected build city from the choice dialog
**************************************************************************/
static void found_city_callback(GtkWidget *w, gpointer data)
{
  struct action_data *args = act_sel_dialog_data;

  dsend_packet_city_name_suggestion_req(&client.conn,
                                        args->actor_unit_id);

  choice_dialog_destroy(act_sel_dialog);
  free(args);
}

/**********************************************************************//**
  User selected "Upgrade Unit" from choice dialog.
**************************************************************************/
static void upgrade_callback(GtkWidget *w, gpointer data)
{
  struct unit *punit;
  struct action_data *args = act_sel_dialog_data;

  if ((punit = game_unit_by_number(args->actor_unit_id))
      && NULL != game_city_by_number(args->target_city_id)) {
    struct unit_list *as_list;

    as_list = unit_list_new();
    unit_list_append(as_list, punit);
    popup_upgrade_dialog(as_list);
    unit_list_destroy(as_list);
  }

  choice_dialog_destroy(act_sel_dialog);
  free(args);
}

/**********************************************************************//**
  User responded to bribe unit dialog
**************************************************************************/
static void bribe_unit_response(GtkWidget *w, gint response, gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  if (response == GTK_RESPONSE_YES) {
    request_do_action(args->act_id, args->actor_unit_id,
                      args->target_unit_id, 0, "");
  }

  gtk_window_destroy(GTK_WINDOW(w));
  free(args);

  /* The user have answered the follow up question. Move on. */
  diplomat_queue_handle_secondary();
}

/**********************************************************************//**
  User responded to bribe stack dialog
**************************************************************************/
static void bribe_stack_response(GtkWidget *w, gint response, gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  if (response == GTK_RESPONSE_YES) {
    request_do_action(args->act_id, args->actor_unit_id,
                      args->target_tile_id, 0, "");
  }

  gtk_window_destroy(GTK_WINDOW(w));
  free(args);

  /* The user have answered the follow up question. Move on. */
  diplomat_queue_handle_secondary();
}

/**********************************************************************//**
  Popup unit bribe dialog
**************************************************************************/
void popup_bribe_unit_dialog(struct unit *actor, struct unit *punit, int cost,
                             const struct action *paction)
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

  g_signal_connect(shell, "response", G_CALLBACK(bribe_unit_response),
                   act_data(paction->id, actor->id,
                            0, punit->id, 0,
                            0, 0, 0, 0));
}

/**********************************************************************//**
  Popup stack bribe dialog
**************************************************************************/
void popup_bribe_stack_dialog(struct unit *actor, struct tile *ptile, int cost,
                             const struct action *paction)
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
      PL_("Bribe unit stack for %d gold?\n%s",
          "Bribe unit stack for %d gold?\n%s", cost), cost, buf);
    gtk_window_set_title(GTK_WINDOW(shell), _("Bribe Enemy Stack"));
    setup_dialog(shell, toplevel);
  } else {
    shell = gtk_message_dialog_new(NULL, 0,
      GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
      /* TRANS: %s is pre-pluralised "Treasury contains %d gold." */
      PL_("Bribing units costs %d gold.\n%s",
          "Bribing units costs %d gold.\n%s", cost), cost, buf);
    gtk_window_set_title(GTK_WINDOW(shell), _("Traitors Demand Too Much!"));
    setup_dialog(shell, toplevel);
  }
  gtk_window_present(GTK_WINDOW(shell));

  g_signal_connect(shell, "response", G_CALLBACK(bribe_stack_response),
                   act_data(paction->id, actor->id,
                            0, 0, ptile->index,
                            0, 0, 0, 0));
}

/**********************************************************************//**
  User responded to steal advances dialog
**************************************************************************/
static void spy_advances_response(GtkWidget *w, gint response,
                                  gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  if (response == GTK_RESPONSE_ACCEPT && args->target_tech_id > 0) {
    if (NULL != game_unit_by_number(args->actor_unit_id)
        && NULL != game_city_by_number(args->target_city_id)) {
      if (args->target_tech_id == A_UNSET) {
        /* This is the untargeted version. */
        request_do_action(get_non_targeted_action_id(args->act_id),
                          args->actor_unit_id, args->target_city_id,
                          args->target_tech_id, "");
      } else {
        /* This is the targeted version. */
        request_do_action(args->act_id,
                          args->actor_unit_id, args->target_city_id,
                          args->target_tech_id, "");
      }
    }
  }

  gtk_window_destroy(GTK_WINDOW(spy_tech_shell));
  spy_tech_shell = NULL;
  free(data);

  /* The user have answered the follow up question. Move on. */
  diplomat_queue_handle_secondary();
}

/**********************************************************************//**
  User selected entry in steal advances dialog
**************************************************************************/
static void spy_advances_callback(GtkSelectionModel *self,
                                  guint position,
                                  guint n_items,
                                  gpointer data)
{
  struct action_data *args = (struct action_data *)data;
  FcActionRow *row = gtk_single_selection_get_selected_item(
                                    GTK_SINGLE_SELECTION(self));

  if (row != NULL) {
    args->target_tech_id = row->id;

    gtk_dialog_set_response_sensitive(GTK_DIALOG(spy_tech_shell),
      GTK_RESPONSE_ACCEPT, TRUE);
  } else {
    args->target_tech_id = 0;

    gtk_dialog_set_response_sensitive(GTK_DIALOG(spy_tech_shell),
      GTK_RESPONSE_ACCEPT, FALSE);
  }
}

/**********************************************************************//**
  Action table cell bind function
**************************************************************************/
static void action_factory_bind(GtkSignalListItemFactory *self,
                                GtkListItem *list_item,
                                gpointer user_data)
{
  FcActionRow *row;

  row = gtk_list_item_get_item(list_item);

  gtk_label_set_text(GTK_LABEL(gtk_list_item_get_child(list_item)),
                     row->name);
}

/**********************************************************************//**
  Action table cell setup function
**************************************************************************/
static void action_factory_setup(GtkSignalListItemFactory *self,
                                 GtkListItem *list_item,
                                 gpointer user_data)
{
  gtk_list_item_set_child(list_item, gtk_label_new(""));
}

/**********************************************************************//**
  Create spy's tech stealing dialog
**************************************************************************/
static void create_advances_list(struct player *pplayer,
                                 struct player *pvictim,
                                 struct action_data *args)
{
  GtkWidget *frame, *label, *vgrid;
  GListStore *store;
  GtkWidget *list;
  GtkColumnViewColumn *column;
  GtkListItemFactory *factory;
  GtkSingleSelection *selection;

  struct unit *actor_unit = game_unit_by_number(args->actor_unit_id);

  spy_tech_shell = gtk_dialog_new_with_buttons(_("Steal Technology"),
                                               NULL, 0,
                                               _("_Cancel"), GTK_RESPONSE_CANCEL,
                                               _("_Steal"), GTK_RESPONSE_ACCEPT,
                                               NULL);
  setup_dialog(spy_tech_shell, toplevel);

  gtk_dialog_set_default_response(GTK_DIALOG(spy_tech_shell),
                                  GTK_RESPONSE_ACCEPT);

  frame = gtk_frame_new(_("Select Advance to Steal"));
  gtk_box_append(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(spy_tech_shell))), frame);

  vgrid = gtk_grid_new();
  gtk_orientable_set_orientation(GTK_ORIENTABLE(vgrid),
                                 GTK_ORIENTATION_VERTICAL);
  gtk_grid_set_row_spacing(GTK_GRID(vgrid), 6);
  gtk_frame_set_child(GTK_FRAME(frame), vgrid);

  store = g_list_store_new(FC_TYPE_ACTION_ROW);

  selection = gtk_single_selection_new(G_LIST_MODEL(store));
  list = gtk_column_view_new(GTK_SELECTION_MODEL(selection));

  factory = gtk_signal_list_item_factory_new();
  g_signal_connect(factory, "bind", G_CALLBACK(action_factory_bind),
                   nullptr);
  g_signal_connect(factory, "setup", G_CALLBACK(action_factory_setup),
                   nullptr);

  column = gtk_column_view_column_new(_("Tech"), factory);
  gtk_column_view_append_column(GTK_COLUMN_VIEW(list), column);

  label = g_object_new(GTK_TYPE_LABEL,
    "use-underline", TRUE,
    "mnemonic-widget", list,
    "label", _("_Advances:"),
    "xalign", 0.0,
    "yalign", 0.5,
    NULL);
  gtk_grid_attach(GTK_GRID(vgrid), label, 0, 0, 1, 1);

  gtk_grid_attach(GTK_GRID(vgrid), list, 0, 1, 1, 1);

  /* Now populate the list */
  if (pvictim) { /* You don't want to know what lag can do -- Syela */
    const struct research *presearch = research_get(pplayer);
    const struct research *vresearch = research_get(pvictim);
    GValue value = { 0, };

    advance_index_iterate(A_FIRST, i) {
      if (research_invention_gettable(presearch, i,
                                      game.info.tech_steal_allow_holes)
          && research_invention_state(vresearch, i) == TECH_KNOWN
          && research_invention_state(presearch, i) != TECH_KNOWN) {
        FcActionRow *row = fc_action_row_new();

        row->name = fc_strdup(research_advance_name_translation(presearch, i));
        row->id = i;

        g_list_store_append(store, row);
        g_object_unref(row);
      }
    } advance_index_iterate_end;

    if (action_prob_possible(actor_unit->client.act_prob_cache[
                             get_non_targeted_action_id(args->act_id)])) {
      FcActionRow *row = fc_action_row_new();

      {
        struct astring str = ASTRING_INIT;

        /* TRANS: %s is a unit name, e.g., Spy */
        astr_set(&str, _("At %s's Discretion"),
                 unit_name_translation(actor_unit));
        g_value_set_string(&value, astr_str(&str));

        row->name = fc_strdup(astr_str(&str));

        astr_free(&str);
      }

      row->id = A_UNSET;

      g_list_store_append(store, row);
      g_object_unref(row);
    }
  }

  gtk_dialog_set_response_sensitive(GTK_DIALOG(spy_tech_shell),
    GTK_RESPONSE_ACCEPT, FALSE);

  gtk_widget_set_visible(gtk_dialog_get_content_area(GTK_DIALOG(spy_tech_shell)),
                         TRUE);

  g_signal_connect(selection, "selection-changed",
                   G_CALLBACK(spy_advances_callback), args);
  g_signal_connect(spy_tech_shell, "response",
                   G_CALLBACK(spy_advances_response), args);

  args->target_tech_id = 0;
}

/**********************************************************************//**
  User has responded to spy's sabotage building dialog
**************************************************************************/
static void spy_improvements_response(GtkWidget *w, gint response, gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  if (response == GTK_RESPONSE_ACCEPT && args->target_building_id > -2) {
    if (NULL != game_unit_by_number(args->actor_unit_id)
        && NULL != game_city_by_number(args->target_city_id)) {
      if (args->target_building_id == B_LAST) {
        /* This is the untargeted version. */
        request_do_action(get_non_targeted_action_id(args->act_id),
                          args->actor_unit_id,
                          args->target_city_id,
                          args->target_building_id, "");
      } else if (args->target_building_id == -1) {
        /* This is the city production version. */
        request_do_action(get_production_targeted_action_id(args->act_id),
                          args->actor_unit_id,
                          args->target_city_id,
                          args->target_building_id, "");
      } else {
        /* This is the targeted version. */
        request_do_action(args->act_id,
                          args->actor_unit_id,
                          args->target_city_id,
                          args->target_building_id, "");
      }
    }
  }

  gtk_window_destroy(GTK_WINDOW(spy_sabotage_shell));
  spy_sabotage_shell = NULL;
  free(args);

  /* The user have answered the follow up question. Move on. */
  diplomat_queue_handle_secondary();
}

/**********************************************************************//**
  User has selected new building from spy's sabotage dialog
**************************************************************************/
static void spy_improvements_callback(GtkSelectionModel *self,
                                      guint position,
                                      guint n_items,
                                      gpointer data)
{
  struct action_data *args = (struct action_data *)data;
  FcActionRow *row = gtk_single_selection_get_selected_item(
                                    GTK_SINGLE_SELECTION(self));

  if (row != NULL) {
    args->target_building_id = row->id;

    gtk_dialog_set_response_sensitive(GTK_DIALOG(spy_sabotage_shell),
                                      GTK_RESPONSE_ACCEPT, TRUE);
  } else {
    args->target_building_id = -2;

    gtk_dialog_set_response_sensitive(GTK_DIALOG(spy_sabotage_shell),
                                      GTK_RESPONSE_ACCEPT, FALSE);
  }
}

/**********************************************************************//**
  Creates spy's building sabotaging dialog
**************************************************************************/
static void create_improvements_list(struct player *pplayer,
                                     struct city *pcity,
                                     struct action_data *args)
{
  GtkWidget *frame, *label, *vgrid;
  GListStore *store;
  GtkWidget *list;
  GtkColumnViewColumn *column;
  GtkListItemFactory *factory;
  GtkSingleSelection *selection;

  struct unit *actor_unit = game_unit_by_number(args->actor_unit_id);

  spy_sabotage_shell = gtk_dialog_new_with_buttons(_("Sabotage Improvements"),
                                                   NULL, 0,
                                                   _("_Cancel"), GTK_RESPONSE_CANCEL,
                                                   _("_Sabotage"), GTK_RESPONSE_ACCEPT,
                                                   NULL);
  setup_dialog(spy_sabotage_shell, toplevel);

  gtk_dialog_set_default_response(GTK_DIALOG(spy_sabotage_shell),
                                  GTK_RESPONSE_ACCEPT);

  frame = gtk_frame_new(_("Select Improvement to Sabotage"));
  gtk_box_append(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(spy_sabotage_shell))), frame);

  vgrid = gtk_grid_new();
  gtk_orientable_set_orientation(GTK_ORIENTABLE(vgrid),
                                 GTK_ORIENTATION_VERTICAL);
  gtk_grid_set_row_spacing(GTK_GRID(vgrid), 6);
  gtk_frame_set_child(GTK_FRAME(frame), vgrid);

  store = g_list_store_new(FC_TYPE_ACTION_ROW);

  selection = gtk_single_selection_new(G_LIST_MODEL(store));
  list = gtk_column_view_new(GTK_SELECTION_MODEL(selection));

  factory = gtk_signal_list_item_factory_new();
  g_signal_connect(factory, "bind", G_CALLBACK(action_factory_bind),
                   nullptr);
  g_signal_connect(factory, "setup", G_CALLBACK(action_factory_setup),
                   nullptr);

  column = gtk_column_view_column_new(_("Improvement"), factory);
  gtk_column_view_append_column(GTK_COLUMN_VIEW(list), column);

  label = g_object_new(GTK_TYPE_LABEL,
    "use-underline", TRUE,
    "mnemonic-widget", list,
    "label", _("_Improvements:"),
    "xalign", 0.0,
    "yalign", 0.5,
    NULL);
  gtk_grid_attach(GTK_GRID(vgrid), label, 0, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(vgrid), list, 0, 1, 1, 1);

  /* Now populate the list */
  if (action_prob_possible(actor_unit->client.act_prob_cache[
                           get_production_targeted_action_id(
                               args->act_id)])) {
    FcActionRow *row = fc_action_row_new();

    row->name = fc_strdup(_("City Production"));
    row->id = -1;

    g_list_store_append(store, row);
    g_object_unref(row);
  }

  city_built_iterate(pcity, pimprove) {
    if (pimprove->sabotage > 0) {
      FcActionRow *row = fc_action_row_new();

      row->name = fc_strdup(city_improvement_name_translation(pcity, pimprove));
      row->id = improvement_number(pimprove);

      g_list_store_append(store, row);
      g_object_unref(row);
    }
  } city_built_iterate_end;

  if (action_prob_possible(actor_unit->client.act_prob_cache[
                           get_non_targeted_action_id(args->act_id)])) {
    struct astring str = ASTRING_INIT;
    FcActionRow *row = fc_action_row_new();

    /* TRANS: %s is a unit name, e.g., Spy */
    astr_set(&str, _("At %s's Discretion"),
             unit_name_translation(actor_unit));

    row->name = fc_strdup(astr_str(&str));
    row->id = B_LAST;

    g_list_store_append(store, row);
    g_object_unref(row);

    astr_free(&str);
  }

  gtk_dialog_set_response_sensitive(GTK_DIALOG(spy_sabotage_shell),
                                    GTK_RESPONSE_ACCEPT, FALSE);

  gtk_widget_set_visible(gtk_dialog_get_content_area(GTK_DIALOG(spy_sabotage_shell)),
                         TRUE);

  g_signal_connect(selection, "selection-changed",
                   G_CALLBACK(spy_improvements_callback), args);
  g_signal_connect(spy_sabotage_shell, "response",
                   G_CALLBACK(spy_improvements_response), args);

  args->target_building_id = -2;
}

/**********************************************************************//**
  Popup tech stealing dialog with list of possible techs
**************************************************************************/
static void spy_steal_popup_shared(GtkWidget *w, gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  args->act_id = args->act_id;

  struct city *pvcity = game_city_by_number(args->target_city_id);
  struct player *pvictim = NULL;

  if (pvcity) {
    pvictim = city_owner(pvcity);
  }

/* It is concievable that pvcity will not be found, because something
   has happened to the city during latency. Therefore we must initialize
   pvictim to NULL and account for !pvictim in create_advances_list. -- Syela */

  /* FIXME: Don't discard the second tech choice dialog. */
  if (!spy_tech_shell) {
    create_advances_list(client.conn.playing, pvictim, args);
    gtk_window_present(GTK_WINDOW(spy_tech_shell));
  } else {
    free(args);
  }

  /* Wait for the player's reply before moving on to the next unit that
   * needs to know what action to take. */
  is_more_user_input_needed = TRUE;

  choice_dialog_destroy(act_sel_dialog);
}

/**********************************************************************//**
  Popup tech stealing dialog with list of possible techs for
  "Targeted Steal Tech"
**************************************************************************/
static void spy_steal_popup(GtkWidget *w, gpointer data)
{
  act_sel_dialog_data->act_id = ACTION_SPY_TARGETED_STEAL_TECH;
  spy_steal_popup_shared(w, act_sel_dialog_data);
}

/**********************************************************************//**
  Popup tech stealing dialog with list of possible techs for
  "Targeted Steal Tech Escape Expected"
**************************************************************************/
static void spy_steal_esc_popup(GtkWidget *w, gpointer data)
{
  act_sel_dialog_data->act_id = ACTION_SPY_TARGETED_STEAL_TECH_ESC;
  spy_steal_popup_shared(w, act_sel_dialog_data);
}

/**********************************************************************//**
  Pops-up the Spy sabotage dialog, upon return of list of
  available improvements requested by the above function.
**************************************************************************/
void popup_sabotage_dialog(struct unit *actor, struct city *pcity,
                           const struct action *paction)
{
  /* FIXME: Don't discard the second target choice dialog. */
  if (!spy_sabotage_shell) {
    create_improvements_list(client.conn.playing, pcity,
                             act_data(paction->id,
                                      actor->id, pcity->id, 0, 0,
                                      0, 0, 0, 0));
    gtk_window_present(GTK_WINDOW(spy_sabotage_shell));
  }
}

/**********************************************************************//**
  User has responded to incite dialog
**************************************************************************/
static void incite_response(GtkWidget *w, gint response, gpointer data)
{
  struct action_data *args = (struct action_data *)data;

  if (response == GTK_RESPONSE_YES) {
    request_do_action(args->act_id, args->actor_unit_id,
                      args->target_city_id, 0, "");
  }

  gtk_window_destroy(GTK_WINDOW(w));
  free(args);

  /* The user have answered the follow up question. Move on. */
  diplomat_queue_handle_secondary();
}

/**********************************************************************//**
  Popup the yes/no dialog for inciting, since we know the cost now
**************************************************************************/
void popup_incite_dialog(struct unit *actor, struct city *pcity, int cost,
                         const struct action *paction)
{
  GtkWidget *shell;
  char buf[1024];

  fc_snprintf(buf, ARRAY_SIZE(buf), PL_("Treasury contains %d gold.",
                                        "Treasury contains %d gold.",
                                        client_player()->economic.gold),
              client_player()->economic.gold);

  if (INCITE_IMPOSSIBLE_COST == cost) {
    shell = gtk_message_dialog_new(NULL, 0,
                                   GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
                                   _("You can't incite a revolt in %s."),
                                   city_name_get(pcity));
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
                   act_data(paction->id, actor->id,
                            pcity->id, 0, 0,
                            0, 0, 0, 0));
}

/**********************************************************************//**
  Callback from the unit target selection dialog.
**************************************************************************/
static void tgt_unit_change_callback(GtkWidget *dlg, gint arg)
{
  int au_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(dlg), "actor"));

  if (arg == GTK_RESPONSE_YES) {
    struct unit *actor = game_unit_by_number(au_id);

    if (actor != NULL) {
      int tgt_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(dlg),
                                                     "target"));
      struct unit *tgt_unit = game_unit_by_number(tgt_id);
      struct tile *tgt_tile = g_object_get_data(G_OBJECT(dlg), "tile");

      if (tgt_unit == NULL) {
        /* Make the action dialog pop up again. */
        dsend_packet_unit_get_actions(&client.conn,
                                      actor->id,
                                      /* Let the server choose the target
                                       * unit. */
                                      IDENTITY_NUMBER_ZERO,
                                      tgt_tile->index,
                                      action_selection_target_extra(),
                                      REQEST_PLAYER_INITIATED);
      } else {
        dsend_packet_unit_get_actions(&client.conn,
                                      actor->id,
                                      tgt_id,
                                      tgt_tile->index,
                                      action_selection_target_extra(),
                                      REQEST_PLAYER_INITIATED);
      }
    }
  } else {
    /* Dialog canceled. This ends the action selection process. */
    action_selection_no_longer_in_progress(au_id);
  }

  gtk_window_destroy(GTK_WINDOW(dlg));
}

/**********************************************************************//**
  Callback from action selection dialog for "Change unit target".
**************************************************************************/
static void act_sel_new_unit_tgt_callback(GtkWidget *w, gpointer data)
{
  struct action_data *args = act_sel_dialog_data;

  struct unit *punit;
  struct unit *tunit;
  struct tile *ptile;

  if ((punit = game_unit_by_number(args->actor_unit_id))
      && (ptile = index_to_tile(&(wld.map), args->target_tile_id))
      && (tunit = game_unit_by_number(args->target_unit_id))) {
    select_tgt_unit(punit, ptile, ptile->units, tunit,
                    _("Target unit selection"),
                    _("Looking for target unit:"),
                    _("Units at tile:"),
                    _("Select"),
                    G_CALLBACK(tgt_unit_change_callback));
  }

  did_not_decide = TRUE;
  action_selection_restart = TRUE;
  choice_dialog_destroy(act_sel_dialog);
  free(args);
}

/**********************************************************************//**
  Callback from the extra target selection dialog.
**************************************************************************/
static void tgt_extra_change_callback(GtkWidget *dlg, gint arg)
{
  int au_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(dlg), "actor"));

  if (arg == GTK_RESPONSE_YES) {
    struct unit *actor = game_unit_by_number(au_id);

    if (actor != NULL) {
      int tgt_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(dlg),
                                                     "target"));
      struct extra_type *tgt_extra = extra_by_number(tgt_id);
      struct tile *tgt_tile = g_object_get_data(G_OBJECT(dlg), "tile");

      if (tgt_extra == NULL) {
        /* Make the action dialog pop up again. */
        dsend_packet_unit_get_actions(&client.conn,
                                      actor->id,
                                      action_selection_target_unit(),
                                      tgt_tile->index,
                                      /* Let the server choose the target
                                       * extra. */
                                      action_selection_target_extra(),
                                      REQEST_PLAYER_INITIATED);
      } else {
        dsend_packet_unit_get_actions(&client.conn,
                                      actor->id,
                                      action_selection_target_unit(),
                                      tgt_tile->index,
                                      tgt_id,
                                      REQEST_PLAYER_INITIATED);
      }
    }
  } else {
    /* Dialog canceled. This ends the action selection process. */
    action_selection_no_longer_in_progress(au_id);
  }

  gtk_window_destroy(GTK_WINDOW(dlg));
}

/**********************************************************************//**
  Callback from action selection dialog for "Change extra target".
**************************************************************************/
static void act_sel_new_extra_tgt_callback(GtkWidget *w, gpointer data)
{
  struct action_data *args = act_sel_dialog_data;

  struct unit *act_unit;
  struct extra_type *tgt_extra;
  struct tile *tgt_tile;

  if ((act_unit = game_unit_by_number(args->actor_unit_id))
      && (tgt_tile = index_to_tile(&(wld.map), args->target_tile_id))
      && (tgt_extra = extra_by_number(args->target_extra_id))) {
    bv_extras potential_targets;

    /* Start with the extras at the tile */
    potential_targets = *tile_extras(tgt_tile);

    extra_type_re_active_iterate(pextra) {
      if (BV_ISSET(potential_targets, extra_number(pextra))) {
        /* This extra is at the tile. Can anything be done to it? */
        if (!utype_can_remove_extra(unit_type_get(act_unit),
                                    pextra)) {
          BV_CLR(potential_targets, extra_number(pextra));
        }
      } else {
        /* This extra isn't at the tile yet. Can it be created? */
        if (utype_can_create_extra(unit_type_get(act_unit),
                                   pextra)) {
          BV_SET(potential_targets, extra_number(pextra));
        }
      }
    } extra_type_re_active_iterate_end;

    select_tgt_extra(act_unit, tgt_tile, potential_targets, tgt_extra,
                     /* TRANS: GTK action selection dialog extra target
                      * selection dialog title. */
                     _("Target extra selection"),
                     /* TRANS: GTK action selection dialog extra target
                      * selection dialog actor unit label. */
                     _("Looking for target extra:"),
                     /* TRANS: GTK action selection dialog extra target
                      * selection dialog extra list label. */
                     _("Extra targets:"),
                     _("Select"),
                     G_CALLBACK(tgt_extra_change_callback));
  }

  did_not_decide = TRUE;
  action_selection_restart = TRUE;
  choice_dialog_destroy(act_sel_dialog);
  free(args);
}

/**********************************************************************//**
  Callback from action selection dialog for "Show Location".
**************************************************************************/
static void act_sel_location_callback(GtkWidget *w, gpointer data)
{
  struct action_data *args = act_sel_dialog_data;

  struct unit *punit;

  if ((punit = game_unit_by_number(args->actor_unit_id))) {
    center_tile_mapcanvas(unit_tile(punit));
  }
}

/**********************************************************************//**
  Delay selection of what action to take.
**************************************************************************/
static void act_sel_wait_callback(GtkWidget *w, gpointer data)
{
  struct action_data *args = act_sel_dialog_data;

  key_unit_wait();

  /* The dialog was destroyed when key_unit_wait() resulted in
   * action_selection_close() being called. */

  free(args);
}

/**********************************************************************//**
  Action selection dialog has been destroyed
**************************************************************************/
static void act_sel_destroy_callback(GtkWidget *w, gpointer data)
{
  act_sel_dialog = NULL;
  diplomat_queue_handle_primary();
}

/**********************************************************************//**
  Action selection dialog has been canceled
**************************************************************************/
static void act_sel_cancel_callback(GtkWidget *w, gpointer data)
{
  choice_dialog_destroy(act_sel_dialog);
  free(act_sel_dialog_data);
}

/**********************************************************************//**
  Action selection dialog has been closed
**************************************************************************/
static void act_sel_close_callback(GtkWidget *w, gpointer data)
{
  choice_dialog_destroy(act_sel_dialog);
  free(act_sel_dialog_data);
}

/* Mapping from an action to the function to call when its button is
 * pushed. */
static const GCallback af_map[ACTION_COUNT] = {
  /* Unit acting against a city target. */
  [ACTION_SPY_TARGETED_SABOTAGE_CITY] =
      (GCallback)request_action_details_callback,
  [ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC] =
      (GCallback)request_action_details_callback,
  [ACTION_SPY_TARGETED_STEAL_TECH] = (GCallback)spy_steal_popup,
  [ACTION_SPY_TARGETED_STEAL_TECH_ESC] = (GCallback)spy_steal_esc_popup,
  [ACTION_SPY_INCITE_CITY] = (GCallback)request_action_details_callback,
  [ACTION_SPY_INCITE_CITY_ESC] = (GCallback)request_action_details_callback,
  [ACTION_UPGRADE_UNIT] = (GCallback)upgrade_callback,
  [ACTION_STRIKE_BUILDING] = (GCallback)request_action_details_callback,

  /* Unit acting against a unit target. */
  [ACTION_SPY_BRIBE_UNIT] = (GCallback)request_action_details_callback,

  /* Unit acting against all units at a tile. */
  [ACTION_SPY_BRIBE_STACK] = (GCallback)request_action_details_callback,

  /* Unit acting against a tile. */
  [ACTION_FOUND_CITY] = (GCallback)found_city_callback,

  /* Unit acting with no target except itself. */
  /* No special callback functions needed for any self targeted actions. */
};

/**********************************************************************//**
  Show the user the action if it is enabled.
**************************************************************************/
static void action_entry(GtkWidget *shl,
                         action_id act_id,
                         const struct act_prob *act_probs,
                         const char *custom,
                         action_id act_num)
{
  const gchar *label;
  const gchar *tooltip;
  GCallback cb;

  if (af_map[act_id] == NULL) {
    /* No special call back function needed for this action. */
    cb = (GCallback)simple_action_callback;
  } else {
    /* Special action specific callback function specified. */
    cb = af_map[act_id];
  }

  /* Don't show disabled actions. */
  if (!action_prob_possible(act_probs[act_id])) {
    return;
  }

  label = action_prepare_ui_name(act_id, "_",
                                 act_probs[act_id],
                                 custom);

  tooltip = act_sel_action_tool_tip(action_by_number(act_id),
                                    act_probs[act_id]);

  action_button_map[act_id] = choice_dialog_get_number_of_buttons(shl);
  choice_dialog_add(shl, label, cb, GINT_TO_POINTER(act_num),
                    FALSE, tooltip);
}

/**********************************************************************//**
  Update an existing button.
**************************************************************************/
static void action_entry_update(GtkWidget *shl,
                                action_id act_id,
                                const struct act_prob *act_probs,
                                const char *custom,
                                action_id act_num)
{
  const gchar *label;
  const gchar *tooltip;

  /* An action that just became impossible has its button disabled.
   * An action that became possible again must be re-enabled. */
  choice_dialog_button_set_sensitive(act_sel_dialog,
      action_button_map[act_id],
      action_prob_possible(act_probs[act_id]));

  /* The probability may have changed. */
  label = action_prepare_ui_name(act_id, "_",
                                 act_probs[act_id], custom);

  tooltip = act_sel_action_tool_tip(action_by_number(act_id),
                                    act_probs[act_id]);

  choice_dialog_button_set_label(act_sel_dialog,
                                 action_button_map[act_id],
                                 label);
  choice_dialog_button_set_tooltip(act_sel_dialog,
                                   action_button_map[act_id],
                                   tooltip);
}

/**********************************************************************//**
  Popup a dialog that allows the player to select what action a unit
  should take.
**************************************************************************/
void popup_action_selection(struct unit *actor_unit,
                            struct city *target_city,
                            struct unit *target_unit,
                            struct tile *target_tile,
                            struct extra_type *target_extra,
                            const struct act_prob *act_probs)
{
  GtkWidget *shl;
  struct astring title = ASTRING_INIT, text = ASTRING_INIT;
  struct city *actor_homecity;

  int button_id;

  act_sel_dialog_data =
      act_data(ACTION_ANY, /* Not decided yet */
               actor_unit->id,
               (target_city) ? target_city->id : IDENTITY_NUMBER_ZERO,
               (target_unit) ? target_unit->id : IDENTITY_NUMBER_ZERO,
               (target_tile) ? target_tile->index : TILE_INDEX_NONE,
               /* No target_building or target_tech supplied. (Dec 2019) */
               B_LAST, A_UNSET, -1,
               target_extra ? target_extra->id : EXTRA_NONE);

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
  target_ids[ATK_SELF] = actor_unit_id;
  target_ids[ATK_CITY] = target_city ?
                         target_city->id :
                         IDENTITY_NUMBER_ZERO;
  target_ids[ATK_UNIT] = target_unit ?
                         target_unit->id :
                         IDENTITY_NUMBER_ZERO;
  target_ids[ATK_STACK] = target_tile ?
                          tile_index(target_tile) :
                          TILE_INDEX_NONE;
  target_ids[ATK_TILE] = target_tile ?
                         tile_index(target_tile) :
                         TILE_INDEX_NONE;
  target_ids[ATK_EXTRAS] = target_tile ?
                           tile_index(target_tile) :
                           TILE_INDEX_NONE;
  target_extra_id      = target_extra ?
                         extra_number(target_extra) :
                         EXTRA_NONE;

  astr_set(&title,
           /* TRANS: %s is a unit name, e.g., Spy */
           _("Choose Your %s's Strategy"),
           unit_name_translation(actor_unit));

  if (target_city && actor_homecity) {
    astr_set(&text,
             _("Your %s from %s reaches the city of %s.\nWhat now?"),
             unit_name_translation(actor_unit),
             city_name_get(actor_homecity),
             city_name_get(target_city));
  } else if (target_city) {
    astr_set(&text,
             _("Your %s has arrived at %s.\nWhat is your command?"),
             unit_name_translation(actor_unit),
             city_name_get(target_city));
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

  /* Unit acting against a city */

  action_iterate(act) {
    if (action_id_get_actor_kind(act) == AAK_UNIT
        && action_id_get_target_kind(act) == ATK_CITY) {
      action_entry(shl, act, act_probs,
                   get_act_sel_action_custom_text(action_by_number(act),
                                                  act_probs[act],
                                                  actor_unit,
                                                  target_city),
                   act);
    }
  } action_iterate_end;

  /* Unit acting against another unit */

  action_iterate(act) {
    if (action_id_get_actor_kind(act) == AAK_UNIT
        && action_id_get_target_kind(act) == ATK_UNIT) {
      action_entry(shl, act, act_probs,
                   get_act_sel_action_custom_text(action_by_number(act),
                                                  act_probs[act],
                                                  actor_unit,
                                                  target_city),
                   act);
    }
  } action_iterate_end;

  /* Unit acting against all units at a tile */

  action_iterate(act) {
    if (action_id_get_actor_kind(act) == AAK_UNIT
        && action_id_get_target_kind(act) == ATK_STACK) {
      action_entry(shl, act, act_probs,
                   get_act_sel_action_custom_text(action_by_number(act),
                                                  act_probs[act],
                                                  actor_unit,
                                                  target_city),
                   act);
    }
  } action_iterate_end;

  /* Unit acting against a tile */

  action_iterate(act) {
    if (action_id_get_actor_kind(act) == AAK_UNIT
        && action_id_get_target_kind(act) == ATK_TILE) {
      action_entry(shl, act, act_probs,
                   get_act_sel_action_custom_text(action_by_number(act),
                                                  act_probs[act],
                                                  actor_unit,
                                                  target_city),
                   act);
    }
  } action_iterate_end;

  /* Unit acting against a tile's extras */

  action_iterate(act) {
    if (action_id_get_actor_kind(act) == AAK_UNIT
        && action_id_get_target_kind(act) == ATK_EXTRAS) {
      action_entry(shl, act, act_probs,
                   get_act_sel_action_custom_text(action_by_number(act),
                                                  act_probs[act],
                                                  actor_unit,
                                                  target_city),
                   act);
    }
  } action_iterate_end;

  /* Unit acting against itself. */

  action_iterate(act) {
    if (action_id_get_actor_kind(act) == AAK_UNIT
        && action_id_get_target_kind(act) == ATK_SELF) {
      action_entry(shl, act, act_probs,
                   get_act_sel_action_custom_text(action_by_number(act),
                                                  act_probs[act],
                                                  actor_unit,
                                                  target_city),
                   act);
    }
  } action_iterate_end;

  if (target_unit != NULL
      && unit_list_size(target_tile->units) > 1) {
    action_button_map[BUTTON_NEW_UNIT_TGT]
      = choice_dialog_get_number_of_buttons(shl);
    choice_dialog_add(shl, _("Change unit target"),
                      (GCallback)act_sel_new_unit_tgt_callback,
                      GINT_TO_POINTER(ACTION_NONE), TRUE, NULL);
  }

  if (target_extra != NULL) {
    action_button_map[BUTTON_NEW_EXTRA_TGT]
      = choice_dialog_get_number_of_buttons(shl);
    choice_dialog_add(shl, _("Change extra target"),
                      (GCallback)act_sel_new_extra_tgt_callback,
                      GINT_TO_POINTER(ACTION_NONE), TRUE, NULL);
  }

  action_button_map[BUTTON_LOCATION]
    = choice_dialog_get_number_of_buttons(shl);
  choice_dialog_add(shl, _("Show Location"),
                    (GCallback)act_sel_location_callback,
                    GINT_TO_POINTER(ACTION_NONE),
                    TRUE, NULL);

  action_button_map[BUTTON_WAIT]
    = choice_dialog_get_number_of_buttons(shl);
  choice_dialog_add(shl, _("_Wait"),
                    (GCallback)act_sel_wait_callback,
                    GINT_TO_POINTER(ACTION_NONE),
                    TRUE, NULL);

  action_button_map[BUTTON_CANCEL]
    = choice_dialog_get_number_of_buttons(shl);
  choice_dialog_add(shl, _("_Cancel"),
                    (GCallback)act_sel_cancel_callback,
                    GINT_TO_POINTER(ACTION_NONE),
                    FALSE, NULL);

  choice_dialog_end(shl);

  act_sel_dialog = shl;

  choice_dialog_set_hide(shl, TRUE);
  g_signal_connect(shl, "destroy",
                   G_CALLBACK(act_sel_destroy_callback), NULL);
  g_signal_connect(shl, "close-request",
                   G_CALLBACK(act_sel_close_callback),
                   GINT_TO_POINTER(ACTION_NONE));

  /* Give follow up questions access to action probabilities. */
  client_unit_init_act_prob_cache(actor_unit);
  action_iterate(act) {
    actor_unit->client.act_prob_cache[act] = act_probs[act];
  } action_iterate_end;

  astr_free(&title);
  astr_free(&text);
}

/**********************************************************************//**
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

/**********************************************************************//**
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

/**********************************************************************//**
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

/**********************************************************************//**
  Returns id of the target tile of the actions currently handled in action
  selection dialog when the action selection dialog is open and it has a
  tile target. Returns TILE_INDEX_NONE if no action selection dialog is
  open.
**************************************************************************/
int action_selection_target_tile(void)
{
  if (act_sel_dialog == NULL) {
    return TILE_INDEX_NONE;
  }

  return target_ids[ATK_TILE];
}

/**********************************************************************//**
  Returns id of the target extra of the actions currently handled in action
  selection dialog when the action selection dialog is open and it has an
  extra target. Returns EXTRA_NONE if no action selection dialog is open
  or no extra target is present in the action selection dialog.
**************************************************************************/
int action_selection_target_extra(void)
{
  if (act_sel_dialog == NULL) {
    return EXTRA_NONE;
  }

  return target_extra_id;
}

/**********************************************************************//**
  Updates the action selection dialog with new information.
**************************************************************************/
void action_selection_refresh(struct unit *actor_unit,
                              struct city *target_city,
                              struct unit *target_unit,
                              struct tile *target_tile,
                              struct extra_type *target_extra,
                              const struct act_prob *act_probs)
{
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

  /* A new target may have appeared. */
  if (target_city) {
    act_sel_dialog_data->target_city_id = target_city->id;
  }
  if (target_unit) {
    act_sel_dialog_data->target_unit_id = target_unit->id;
  }
  if (target_tile) {
    act_sel_dialog_data->target_tile_id = target_tile->index;
  }
  /* No target_building or target_tech supplied. (Dec 2019) */
  if (target_extra) {
    act_sel_dialog_data->target_extra_id = target_extra->id;
  }

  action_iterate(act) {
    const char *custom;

    if (action_id_get_actor_kind(act) != AAK_UNIT) {
      /* Not relevant. */
      continue;
    }

    custom = get_act_sel_action_custom_text(action_by_number(act),
                                            act_probs[act],
                                            actor_unit,
                                            target_city);

    if (BUTTON_NOT_THERE == action_button_map[act]) {
      /* Add the button (unless its probability is 0). */
      action_entry(act_sel_dialog, act, act_probs, custom, act);
    } else {
      /* Update the existing button. */
      action_entry_update(act_sel_dialog, act, act_probs, custom, act);
    }
  } action_iterate_end;

  /* DO NOT change the action_button_map[] for any button to reflect its
   * new position. A button keeps its choice dialog internal name when its
   * position changes. A button's id number is therefore based on when
   * it was added, not on its current position. */

  if (BUTTON_NOT_THERE != action_button_map[BUTTON_WAIT]) {
    /* Move the wait button below the recently added button. */
    choice_dialog_button_move_to_the_end(act_sel_dialog,
        action_button_map[BUTTON_WAIT]);
  }

  if (BUTTON_NOT_THERE != action_button_map[BUTTON_CANCEL]) {
    /* Move the cancel button below the recently added button. */
    choice_dialog_button_move_to_the_end(act_sel_dialog,
        action_button_map[BUTTON_CANCEL]);
  }

  choice_dialog_end(act_sel_dialog);
}

/**********************************************************************//**
  Closes the action selection dialog
**************************************************************************/
void action_selection_close(void)
{
  if (act_sel_dialog != NULL) {
    did_not_decide = TRUE;
    choice_dialog_destroy(act_sel_dialog);
  }
}
