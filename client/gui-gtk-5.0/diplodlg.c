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

#include <gtk/gtk.h>

/* utility */
#include "log.h"
#include "mem.h"
#include "shared.h"
#include "support.h"

/* common */
#include "diptreaty.h"
#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "map.h"
#include "nation.h"
#include "packets.h"
#include "player.h"
#include "research.h"

/* client */
#include "chatline.h"
#include "client_main.h"
#include "climisc.h"
#include "options.h"

/* client/gui-gtk-5.0 */
#include "diplodlg.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapview.h"
#include "plrdlg.h"

struct Diplomacy_dialog {
  struct treaty *treaty;
  struct gui_dialog *dialog;

  GtkWidget *pic0;
  GtkWidget *pic1;

  GtkListStore *store;
};

struct Diplomacy_notebook {
  struct gui_dialog *dialog;
  GtkWidget *notebook;
};

struct city_deal {
  int giver;
  int receiver;
  int id;
};

#define SPECLIST_TAG dialog
#define SPECLIST_TYPE struct Diplomacy_dialog
#include "speclist.h"

#define dialog_list_iterate(dialoglist, pdialog) \
    TYPED_LIST_ITERATE(struct Diplomacy_dialog, dialoglist, pdialog)
#define dialog_list_iterate_end  LIST_ITERATE_END

static struct dialog_list *dialog_list;
static struct Diplomacy_notebook *dipl_main;

static struct Diplomacy_dialog *create_diplomacy_dialog(struct treaty *ptreaty,
                                                        struct player *plr0,
                                                        struct player *plr1);

static struct Diplomacy_dialog *find_diplomacy_dialog(struct player *they);
static void popup_diplomacy_dialog(struct treaty *ptreaty, struct player *they,
                                   struct player *initiator);
static void diplomacy_dialog_map_callback(GSimpleAction *action,
                                          GVariant *parameter,
                                          gpointer data);
static void diplomacy_dialog_seamap_callback(GSimpleAction *action,
                                             GVariant *parameter,
                                             gpointer data);

static void diplomacy_dialog_tech_callback(GSimpleAction *action,
                                           GVariant *parameter,
                                           gpointer data);
static void diplomacy_dialog_city_callback(GSimpleAction *action,
                                           GVariant *parameter,
                                           gpointer data);
static void diplomacy_dialog_vision_callback(GSimpleAction *action,
                                             GVariant *parameter,
                                             gpointer data);
static void diplomacy_dialog_embassy_callback(GSimpleAction *action,
                                              GVariant *parameter,
                                              gpointer data);
static void diplomacy_dialog_shared_tiles_callback(GSimpleAction *action,
                                                   GVariant *parameter,
                                                   gpointer data);
static void diplomacy_dialog_ceasefire_callback(GSimpleAction *action,
                                                GVariant *parameter,
                                                gpointer data);
static void diplomacy_dialog_peace_callback(GSimpleAction *action,
                                            GVariant *parameter,
                                            gpointer data);
static void diplomacy_dialog_alliance_callback(GSimpleAction *action,
                                               GVariant *parameter,
                                               gpointer data);

static void close_diplomacy_dialog(struct Diplomacy_dialog *pdialog);
static void update_diplomacy_dialog(struct Diplomacy_dialog *pdialog);
static void diplo_dialog_returnkey(GtkWidget *w, gpointer data);

static struct Diplomacy_notebook *diplomacy_main_create(void);
static void diplomacy_main_destroy(void);
static void diplomacy_main_response(struct gui_dialog *dlg, int response,
                                    gpointer data);

#define RESPONSE_CANCEL_MEETING 100
#define RESPONSE_CANCEL_MEETING_ALL 101


#define FC_TYPE_CLAUSE_ROW (fc_clause_row_get_type())

G_DECLARE_FINAL_TYPE(FcClauseRow, fc_clause_row, FC, CLAUSE_ROW, GObject)

struct _FcClauseRow
{
  GObject parent_instance;

  char *clause;
};

struct _FcClauseClass
{
  GObjectClass parent_class;
};

G_DEFINE_TYPE(FcClauseRow, fc_clause_row, G_TYPE_OBJECT)

/**********************************************************************//**
  Initialization method for FcClauseRow class
**************************************************************************/
static void
fc_clause_row_class_init(FcClauseRowClass *klass)
{
}

/**********************************************************************//**
  Initialization method for FcClauseRow
**************************************************************************/
static void
fc_clause_row_init(FcClauseRow *self)
{
}

/**********************************************************************//**
  FcClauseRow creation method
**************************************************************************/
#if 0
static FcClauseRow *fc_clause_row_new(void)
{
  FcClauseRow *result;

  result = g_object_new(FC_TYPE_CLAUSE_ROW, nullptr);

  return result;
}
#endif

/************************************************************************//**
  Server tells us that either party has accepted treaty
****************************************************************************/
void gui_recv_accept_treaty(struct treaty *ptreaty, struct player *they)
{
  struct Diplomacy_dialog *pdialog = find_diplomacy_dialog(they);

  if (!pdialog) {
    return;
  }

  fc_assert(pdialog->treaty == ptreaty);

  update_diplomacy_dialog(pdialog);
  gui_dialog_alert(pdialog->dialog);
}

/************************************************************************//**
  Someone is initiating meeting with us.
****************************************************************************/
void gui_init_meeting(struct treaty *ptreaty, struct player *they,
                      struct player *initiator)
{
  popup_diplomacy_dialog(ptreaty, they, initiator);
}

/************************************************************************//**
  Meeting has been cancelled.
****************************************************************************/
void gui_recv_cancel_meeting(struct treaty *ptreaty, struct player *they,
                             struct player *initiator)
{
  struct Diplomacy_dialog *pdialog = find_diplomacy_dialog(they);

  if (!pdialog) {
    return;
  }

  fc_assert(pdialog->treaty == ptreaty);

  close_diplomacy_dialog(pdialog);
}

/**********************************************************************//**
  Prepare to clause creation or removal.
**************************************************************************/
void gui_prepare_clause_updt(struct treaty *ptreaty, struct player *they)
{
  /* Not needed */
}

/************************************************************************//**
  Added clause to the meeting
****************************************************************************/
void gui_recv_create_clause(struct treaty *ptreaty, struct player *they)
{
  struct Diplomacy_dialog *pdialog = find_diplomacy_dialog(they);

  if (!pdialog) {
    return;
  }

  fc_assert(pdialog->treaty == ptreaty);

  update_diplomacy_dialog(pdialog);
  gui_dialog_alert(pdialog->dialog);
}

/************************************************************************//**
  Removed clause from meeting.
****************************************************************************/
void gui_recv_remove_clause(struct treaty *ptreaty, struct player *they)
{
  struct Diplomacy_dialog *pdialog = find_diplomacy_dialog(they);

  if (!pdialog) {
    return;
  }

  fc_assert(pdialog->treaty == ptreaty);

  update_diplomacy_dialog(pdialog);
  gui_dialog_alert(pdialog->dialog);
}

/************************************************************************//**
  Popup the dialog 10% inside the main-window
****************************************************************************/
static void popup_diplomacy_dialog(struct treaty *ptreaty, struct player *they,
                                   struct player *initiator)
{
  struct Diplomacy_dialog *pdialog = find_diplomacy_dialog(they);

  if (!is_human(client_player())) {
    return; /* Don't show if we are not human controlled. */
  }

  if (!pdialog) {
    pdialog = create_diplomacy_dialog(ptreaty, client_player(), they);
  }

  gui_dialog_present(pdialog->dialog);
  /* We initiated the meeting - Make the tab active */
  if (initiator == client_player()) {
    /* We have to raise the diplomacy meeting tab as well as the selected
     * meeting. */
    fc_assert_ret(dipl_main != NULL);
    gui_dialog_raise(dipl_main->dialog);
    gui_dialog_raise(pdialog->dialog);

    if (players_dialog_shell != NULL) {
      gui_dialog_set_return_dialog(pdialog->dialog, players_dialog_shell);
    }
  }
}

/************************************************************************//**
  Utility for g_list_sort(). See below.
****************************************************************************/
static gint sort_advance_names(gconstpointer a, gconstpointer b)
{
  const struct advance *padvance1 = (const struct advance *) a;
  const struct advance *padvance2 = (const struct advance *) b;

  return fc_strcoll(advance_name_translation(padvance1),
                    advance_name_translation(padvance2));
}

/************************************************************************//**
  Popup menu about adding clauses
****************************************************************************/
static GMenu *create_clause_menu(GActionGroup *group,
                                 struct Diplomacy_dialog *pdialog,
                                 struct player *partner, bool them)
{
  GMenu *topmenu, *submenu;
  GSimpleAction *act;
  bool any_map = FALSE;
  char act_plr_part[20];
  char act_name[60];
  struct player *pgiver, *pother;

  if (them) {
    fc_strlcpy(act_plr_part, "_them", sizeof(act_plr_part));
    pgiver = partner;
    pother = client_player();
  } else {
    fc_strlcpy(act_plr_part, "_us", sizeof(act_plr_part));
    pgiver = client_player();
    pother = partner;
  }

  topmenu = g_menu_new();

  /* Maps. */
  if (clause_enabled(CLAUSE_MAP)) {
    submenu = g_menu_new();

    fc_snprintf(act_name, sizeof(act_name), "worldmap%s", act_plr_part);
    act = g_simple_action_new(act_name, NULL);
    g_object_set_data(G_OBJECT(act), "plr", pgiver);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
    g_signal_connect(act, "activate", G_CALLBACK(diplomacy_dialog_map_callback),
                     pdialog);

    fc_snprintf(act_name, sizeof(act_name), "win.worldmap%s", act_plr_part);
    menu_item_append_unref(submenu, g_menu_item_new(_("World-map"), act_name));

    any_map = TRUE;
  }

  if (clause_enabled(CLAUSE_SEAMAP)) {
    if (!any_map) {
      submenu = g_menu_new();
    }

    fc_snprintf(act_name, sizeof(act_name), "seamap%s", act_plr_part);
    act = g_simple_action_new(act_name, NULL);
    g_object_set_data(G_OBJECT(act), "plr", pgiver);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
    g_signal_connect(act, "activate", G_CALLBACK(diplomacy_dialog_seamap_callback),
                     pdialog);

    fc_snprintf(act_name, sizeof(act_name), "win.seamap%s", act_plr_part);
    menu_item_append_unref(submenu, g_menu_item_new(_("Sea-map"), act_name));

    any_map = TRUE;
  }

  if (any_map) {
    submenu_append_unref(topmenu, _("_Maps"), G_MENU_MODEL(submenu));
  }

  /* Trading: advances */
  if (clause_enabled(CLAUSE_ADVANCE)) {
    const struct research *gresearch = research_get(pgiver);
    const struct research *oresearch = research_get(pother);
    GList *sorting_list = NULL;
    bool team_embassy = team_has_embassy(pgiver->team, pother);
    int i;

    submenu = g_menu_new();

    advance_iterate(padvance) {
      Tech_type_id tech = advance_number(padvance);

      if (research_invention_state(gresearch, tech) == TECH_KNOWN
          && (!team_embassy /* We don't know what the other could actually receive */
              || research_invention_gettable(oresearch, tech,
                                             game.info.tech_trade_allow_holes))
          && (research_invention_state(oresearch, tech) == TECH_UNKNOWN
              || research_invention_state(oresearch, tech)
                 == TECH_PREREQS_KNOWN)) {
        sorting_list = g_list_prepend(sorting_list, padvance);
      }
    } advance_iterate_end;

    if (NULL != sorting_list) {
      GList *list_item;
      const struct advance *padvance;

      sorting_list = g_list_sort(sorting_list, sort_advance_names);

      /* TRANS: All technologies menu item in the diplomatic dialog. */
      fc_snprintf(act_name, sizeof(act_name), "advance%sall", act_plr_part);
      act = g_simple_action_new(act_name, NULL);

      g_object_set_data(G_OBJECT(act), "player_from",
                        GINT_TO_POINTER(player_number(pgiver)));
      g_object_set_data(G_OBJECT(act), "player_to",
                        GINT_TO_POINTER(player_number(pother)));
      g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
      g_signal_connect(act, "activate",
                       G_CALLBACK(diplomacy_dialog_tech_callback),
                       GINT_TO_POINTER(A_LAST));

      fc_snprintf(act_name, sizeof(act_name), "win.advance%sall", act_plr_part);
      menu_item_append_unref(submenu, g_menu_item_new(_("All advances"), act_name));

      for (list_item = sorting_list, i = 0; NULL != list_item;
           list_item = g_list_next(list_item), i++) {

        fc_snprintf(act_name, sizeof(act_name), "advance%s%d",
                    act_plr_part, i);
        act = g_simple_action_new(act_name, NULL);

        padvance = (const struct advance *) list_item->data;
        g_object_set_data(G_OBJECT(act), "player_from",
                          GINT_TO_POINTER(player_number(pgiver)));
        g_object_set_data(G_OBJECT(act), "player_to",
                          GINT_TO_POINTER(player_number(pother)));
        g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
        g_signal_connect(act, "activate",
                         G_CALLBACK(diplomacy_dialog_tech_callback),
                         GINT_TO_POINTER(advance_number(padvance)));

        fc_snprintf(act_name, sizeof(act_name), "win.advance%s%d",
                    act_plr_part, i);
        menu_item_append_unref(submenu,
                               g_menu_item_new(advance_name_translation(padvance),
                                               act_name));
      }

      g_list_free(sorting_list);
    }

    submenu_append_unref(topmenu, _("_Advances"), G_MENU_MODEL(submenu));
  }

  /* Trading: cities. */

  /****************************************************************
  Creates a sorted list of plr0's cities, excluding the capital and
  any cities not visible to plr1. This means that you can only trade
  cities visible to requesting player.

                              - Kris Bubendorfer
  *****************************************************************/
  if (clause_enabled(CLAUSE_CITY)) {
    int i = 0;
    int n = city_list_size(pgiver->cities);

    submenu = g_menu_new();

    if (n > 0) {
      struct city **city_list_ptrs;

      city_list_ptrs = fc_malloc(sizeof(struct city *) * n);

      city_list_iterate(pgiver->cities, pcity) {
        if (!is_capital(pcity)) {
          city_list_ptrs[i] = pcity;
          i++;
        }
      } city_list_iterate_end;

      if (i > 0) { /* Cities other than capitals */
        int j;

        qsort(city_list_ptrs, i, sizeof(struct city *), city_name_compare);

        for (j = 0; j < i; j++) {
          struct city_deal *deal = fc_malloc(sizeof(struct city_deal));

          fc_snprintf(act_name, sizeof(act_name), "city%s%d", act_plr_part, i);
          act = g_simple_action_new(act_name, NULL);

          deal->giver = player_number(pgiver);
          deal->receiver = player_number(pother);
          deal->id = city_list_ptrs[j]->id;

          g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
          g_signal_connect(act, "activate",
                           G_CALLBACK(diplomacy_dialog_city_callback),
                           (gpointer)deal);

          fc_snprintf(act_name, sizeof(act_name), "win.city%s%d",
                      act_plr_part, i);
          menu_item_append_unref(submenu,
                                 g_menu_item_new(city_name_get(city_list_ptrs[j]),
                                                 act_name));
        }
      }

      free(city_list_ptrs);
    }

    submenu_append_unref(topmenu, _("_Cities"), G_MENU_MODEL(submenu));
  }

  /* Give shared vision. */
  if (clause_enabled(CLAUSE_VISION)) {
    fc_snprintf(act_name, sizeof(act_name), "vision%s", act_plr_part);
    act = g_simple_action_new(act_name, NULL);
    g_object_set_data(G_OBJECT(act), "plr", pgiver);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
    g_signal_connect(act, "activate", G_CALLBACK(diplomacy_dialog_vision_callback),
                     pdialog);

    fc_snprintf(act_name, sizeof(act_name), "win.vision%s", act_plr_part);
    menu_item_append_unref(topmenu, g_menu_item_new(_("_Give shared vision"), act_name));

    g_simple_action_set_enabled(G_SIMPLE_ACTION(act),
                                !gives_shared_vision(pgiver, pother));
  }

  /* Give embassy. */
  if (clause_enabled(CLAUSE_EMBASSY)) {
    fc_snprintf(act_name, sizeof(act_name), "embassy%s", act_plr_part);
    act = g_simple_action_new(act_name, NULL);
    g_object_set_data(G_OBJECT(act), "plr", pgiver);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
    g_signal_connect(act, "activate", G_CALLBACK(diplomacy_dialog_embassy_callback),
                     pdialog);

    fc_snprintf(act_name, sizeof(act_name), "win.embassy%s", act_plr_part);
    menu_item_append_unref(topmenu, g_menu_item_new(_("Give _embassy"), act_name));

    /* Don't take in account the embassy effects. */
    g_simple_action_set_enabled(G_SIMPLE_ACTION(act),
                                !player_has_real_embassy(pother, pgiver));
  }

  /* Shared tiles */
  if (clause_enabled(CLAUSE_SHARED_TILES)) {
    fc_snprintf(act_name, sizeof(act_name), "tiles%s", act_plr_part);
    act = g_simple_action_new(act_name, NULL);
    g_object_set_data(G_OBJECT(act), "plr", pgiver);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
    g_signal_connect(act, "activate", G_CALLBACK(diplomacy_dialog_shared_tiles_callback),
                     pdialog);

    fc_snprintf(act_name, sizeof(act_name), "win.tiles%s", act_plr_part);
    menu_item_append_unref(topmenu, g_menu_item_new(_("_Share tiles"), act_name));
  }

  /* Pacts. */
  if (pgiver == pdialog->treaty->plr0) {
    enum diplstate_type ds;
    int pact_clauses = 0;

    ds = player_diplstate_get(pgiver, pother)->type;

    submenu = g_menu_new();

    if (clause_enabled(CLAUSE_CEASEFIRE)) {
      fc_snprintf(act_name, sizeof(act_name), "ceasefire%s", act_plr_part);
      act = g_simple_action_new(act_name, NULL);
      g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
      g_signal_connect(act, "activate",
                       G_CALLBACK(diplomacy_dialog_ceasefire_callback), pdialog);

      fc_snprintf(act_name, sizeof(act_name), "win.ceasefire%s", act_plr_part);
      menu_item_append_unref(submenu,
                             g_menu_item_new(Q_("?diplomatic_state:Cease-fire"),
                                             act_name));

      g_simple_action_set_enabled(G_SIMPLE_ACTION(act),
                                  ds != DS_CEASEFIRE && ds != DS_TEAM);
      pact_clauses++;
    }

    if (clause_enabled(CLAUSE_PEACE)) {
      fc_snprintf(act_name, sizeof(act_name), "peace%s", act_plr_part);
      act = g_simple_action_new(act_name, NULL);
      g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
      g_signal_connect(act, "activate",
                       G_CALLBACK(diplomacy_dialog_peace_callback), pdialog);

      fc_snprintf(act_name, sizeof(act_name), "win.peace%s", act_plr_part);
      menu_item_append_unref(submenu, g_menu_item_new(Q_("?diplomatic_state:Peace"),
                                                      act_name));

      g_simple_action_set_enabled(G_SIMPLE_ACTION(act),
                                  ds != DS_PEACE && ds != DS_TEAM);
      pact_clauses++;
    }

    if (clause_enabled(CLAUSE_ALLIANCE)) {
      fc_snprintf(act_name, sizeof(act_name), "alliance%s", act_plr_part);
      act = g_simple_action_new(act_name, NULL);
      g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
      g_signal_connect(act, "activate",
                       G_CALLBACK(diplomacy_dialog_alliance_callback), pdialog);

      fc_snprintf(act_name, sizeof(act_name), "win.alliance%s", act_plr_part);
      menu_item_append_unref(submenu,
                             g_menu_item_new(Q_("?diplomatic_state:Alliance"),
                                             act_name));

      g_simple_action_set_enabled(G_SIMPLE_ACTION(act),
                                  ds != DS_ALLIANCE && ds != DS_TEAM);
      pact_clauses++;
    }

    if (pact_clauses > 0) {
      submenu_append_unref(topmenu, _("_Pacts"), G_MENU_MODEL(submenu));
    } else {
      g_object_unref(submenu);
    }
  }

  return topmenu;
}

/************************************************************************//**
  Some clause activated
****************************************************************************/
static void row_callback(GtkTreeView *view, GtkTreePath *path,
                         GtkTreeViewColumn *col, gpointer data)
{
  struct Diplomacy_dialog *pdialog = (struct Diplomacy_dialog *)data;
  gint i;
  gint *index;

  index = gtk_tree_path_get_indices(path);

  i = 0;
  clause_list_iterate(pdialog->treaty->clauses, pclause) {
    if (i == index[0]) {
      dsend_packet_diplomacy_remove_clause_req(&client.conn,
                                               player_number(pdialog->treaty->plr1),
                                               player_number(pclause->from),
                                               pclause->type,
                                               pclause->value);
      return;
    }
    i++;
  } clause_list_iterate_end;
}

/************************************************************************//**
  Create the main tab for diplomatic meetings.
****************************************************************************/
static struct Diplomacy_notebook *diplomacy_main_create(void)
{
  /* Collect all meetings in one main tab. */
  if (!dipl_main) {
    GtkWidget *dipl_sw;

    dipl_main = fc_malloc(sizeof(*dipl_main));
    gui_dialog_new(&(dipl_main->dialog), GTK_NOTEBOOK(top_notebook),
                  dipl_main->dialog, TRUE);
    dipl_main->notebook = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(dipl_main->notebook),
                             GTK_POS_RIGHT);
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(dipl_main->notebook), TRUE);

    dipl_sw = gtk_scrolled_window_new();
    gtk_widget_set_margin_bottom(dipl_sw, 2);
    gtk_widget_set_margin_end(dipl_sw, 2);
    gtk_widget_set_margin_start(dipl_sw, 2);
    gtk_widget_set_margin_top(dipl_sw, 2);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(dipl_sw),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(dipl_sw),
                                  dipl_main->notebook);

    /* Buttons */
    gui_dialog_add_button(dipl_main->dialog, NULL,
                          _("Cancel _all meetings"),
                          RESPONSE_CANCEL_MEETING_ALL);

    /* Responses for _all_ meetings. */
    gui_dialog_response_set_callback(dipl_main->dialog,
                                     diplomacy_main_response);

    gui_dialog_add_content_widget(dipl_main->dialog, dipl_sw);

    gui_dialog_show_all(dipl_main->dialog);
    gui_dialog_present(dipl_main->dialog);
  }

  return dipl_main;
}

/************************************************************************//**
  Destroy main diplomacy dialog.
****************************************************************************/
static void diplomacy_main_destroy(void)
{
  if (dipl_main->dialog) {
    gui_dialog_destroy(dipl_main->dialog);
  }
  free(dipl_main);
  dipl_main = NULL;
}

/************************************************************************//**
  User has responded to whole diplomacy dialog (main tab).
****************************************************************************/
static void diplomacy_main_response(struct gui_dialog *dlg, int response,
                                    gpointer data)
{
  if (!dipl_main) {
    return;
  }

  switch (response) {
  default:
    log_error("unhandled response in %s: %d", __FUNCTION__, response);
    fc__fallthrough; /* No break. */
  case GTK_RESPONSE_DELETE_EVENT:   /* GTK: delete the widget. */
  case RESPONSE_CANCEL_MEETING_ALL: /* Cancel all meetings. */
    dialog_list_iterate(dialog_list, adialog) {
      /* This will do a round trip to the server and close the dialog in the
       * client. Closing the last dialog will also close the main tab.*/
      dsend_packet_diplomacy_cancel_meeting_req(&client.conn,
                                                player_number(
                                                  adialog->treaty->plr1));
    } dialog_list_iterate_end;
    break;
  }
}

/************************************************************************//**
  Destroy diplomacy dialog
****************************************************************************/
static void diplomacy_destroy(struct Diplomacy_dialog *pdialog)
{
  if (NULL != pdialog->dialog) {
    /* pdialog->dialog may be NULL if the tab has been destroyed
     * by an other way. */
    gui_dialog_destroy(pdialog->dialog);
    pdialog->dialog = NULL;
  }
  dialog_list_remove(dialog_list, pdialog);

  if (dialog_list) {
    /* Diplomatic meetings in one main tab. */
    if (dialog_list_size(dialog_list) > 0) {
      if (dipl_main && dipl_main->dialog) {
        gchar *buf;

        buf = g_strdup_printf(_("Diplomacy [%d]"), dialog_list_size(dialog_list));
        gui_dialog_set_title(dipl_main->dialog, buf);
        g_free(buf);
      }
    } else if (dipl_main) {
      /* No meeting left - destroy main tab. */
      diplomacy_main_destroy();
    }
  }

  /* Last sub-tab must not be freed before diplomacy_main_destroy() call. */
  free(pdialog);
}

/************************************************************************//**
  User has responded to whole diplomacy dialog (one meeting).
****************************************************************************/
static void diplomacy_response(struct gui_dialog *dlg, int response,
                               gpointer data)
{
  struct Diplomacy_dialog *pdialog = NULL;

  fc_assert_ret(data);
  pdialog = (struct Diplomacy_dialog *)data;

  switch (response) {
  case GTK_RESPONSE_ACCEPT:         /* Accept treaty. */
    dsend_packet_diplomacy_accept_treaty_req(&client.conn,
                                             player_number(
                                               pdialog->treaty->plr1));
    break;

  default:
    log_error("unhandled response in %s: %d", __FUNCTION__, response);
    fc__fallthrough; /* No break. */
  case GTK_RESPONSE_DELETE_EVENT:   /* GTK: delete the widget. */
  case GTK_RESPONSE_CANCEL:         /* GTK: cancel button. */
  case RESPONSE_CANCEL_MEETING:     /* Cancel meetings. */
    dsend_packet_diplomacy_cancel_meeting_req(&client.conn,
                                              player_number(
                                                pdialog->treaty->plr1));
    break;
  }
}

/************************************************************************//**
  Setups diplomacy dialog widgets.
****************************************************************************/
static struct Diplomacy_dialog *create_diplomacy_dialog(struct treaty *ptreaty,
                                                        struct player *plr0,
                                                        struct player *plr1)
{
  struct Diplomacy_notebook *dipl_dialog;
  GtkWidget *vbox, *hgrid, *table, *mainbox;
  GtkWidget *label, *sw, *view, *pic, *spin;
  GtkWidget *aux_menu, *notebook;
  struct sprite *flag_spr;
  GtkListStore *store;
  GtkCellRenderer *rend;
  int i;
  struct Diplomacy_dialog *pdialog;
  char plr_buf[4 * MAX_LEN_NAME];
  gchar *buf;
  int grid_col = 0;
  int main_row = 0;
  GActionGroup *group;
  GMenu *menu;

  pdialog = fc_malloc(sizeof(*pdialog));

  dialog_list_prepend(dialog_list, pdialog);
  pdialog->treaty = ptreaty;

  /* Get main diplomacy tab. */
  dipl_dialog = diplomacy_main_create();

  buf = g_strdup_printf(_("Diplomacy [%d]"), dialog_list_size(dialog_list));
  gui_dialog_set_title(dipl_dialog->dialog, buf);
  g_free(buf);

  notebook = dipl_dialog->notebook;

  gui_dialog_new(&(pdialog->dialog), GTK_NOTEBOOK(notebook), pdialog, FALSE);

  /* Buttons */
  gui_dialog_add_button(pdialog->dialog, NULL,
                        _("Accept treaty"), GTK_RESPONSE_ACCEPT);
  gui_dialog_add_button(pdialog->dialog, NULL,
                        _("Cancel meeting"), RESPONSE_CANCEL_MEETING);

  /* Responses for one meeting. */
  gui_dialog_response_set_callback(pdialog->dialog, diplomacy_response);

  /* Label for the new meeting. */
  buf = g_strdup_printf("%s", nation_plural_for_player(plr1));
  gui_dialog_set_title(pdialog->dialog, buf);

  /* Sort meeting tabs alphabetically by the tab label. */
  for (i = 0; i < gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook)); i++) {
    GtkWidget *prev_page
      = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), i);
    struct gui_dialog *prev_dialog
      = g_object_get_data(G_OBJECT(prev_page), "gui-dialog-data");
    const char *prev_label
      = gtk_label_get_text(GTK_LABEL(prev_dialog->v.tab.label));

    if (fc_strcasecmp(buf, prev_label) < 0) {
      gtk_notebook_reorder_child(GTK_NOTEBOOK(notebook),
                                 pdialog->dialog->grid, i);
      break;
    }
  }
  g_free(buf);

  /* Us. */
  vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_widget_set_margin_start(vbox, 2);
  gtk_widget_set_margin_end(vbox, 2);
  gtk_widget_set_margin_top(vbox, 2);
  gtk_widget_set_margin_bottom(vbox, 2);
  gui_dialog_add_content_widget(pdialog->dialog, vbox);

  /* Our nation. */
  label = gtk_label_new(NULL);
  gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  buf = g_strdup_printf("<span size=\"large\"><u>%s</u></span>",
                        nation_plural_for_player(plr0));
  gtk_label_set_markup(GTK_LABEL(label), buf);
  g_free(buf);
  gtk_box_append(GTK_BOX(vbox), label);

  hgrid = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(hgrid), 5);
  gtk_box_append(GTK_BOX(vbox), hgrid);

  /* Our flag */
  flag_spr = get_nation_flag_sprite(tileset, nation_of_player(plr0));

  pic = gtk_picture_new();
  picture_set_from_surface(GTK_PICTURE(pic), flag_spr->surface);
  gtk_grid_attach(GTK_GRID(hgrid), pic, grid_col++, 0, 1, 1);

  /* Our name. */
  label = gtk_label_new(NULL);
  gtk_widget_set_hexpand(label, TRUE);
  gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  buf = g_strdup_printf("<span size=\"large\" weight=\"bold\">%s</span>",
                        ruler_title_for_player(plr0, plr_buf, sizeof(plr_buf)));
  gtk_label_set_markup(GTK_LABEL(label), buf);
  g_free(buf);
  gtk_grid_attach(GTK_GRID(hgrid), label, grid_col++, 0, 1, 1);

  pdialog->pic0 = gtk_picture_new();
  gtk_grid_attach(GTK_GRID(hgrid), pdialog->pic0, grid_col++, 0, 1, 1);

  /* Menu for clauses: we. */
  aux_menu = aux_menu_new();
  group = G_ACTION_GROUP(g_simple_action_group_new());

  menu = create_clause_menu(group, pdialog, plr1, FALSE);
  gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(aux_menu), G_MENU_MODEL(menu));

  /* Main table for clauses and (if activated) gold trading: we. */
  table = gtk_grid_new();
  gtk_widget_set_halign(table, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(table, GTK_ALIGN_CENTER);
  gtk_grid_set_column_spacing(GTK_GRID(table), 16);
  gtk_box_append(GTK_BOX(vbox), table);

  if (clause_enabled(CLAUSE_GOLD)) {
    spin = gtk_spin_button_new_with_range(0.0, plr0->economic.gold + 0.1,
                                          1.0);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 0);
    gtk_editable_set_width_chars(GTK_EDITABLE(spin), 16);
    gtk_grid_attach(GTK_GRID(table), spin, 1, 0, 1, 1);
    g_object_set_data(G_OBJECT(spin), "plr", plr0);
    g_signal_connect_after(spin, "value-changed",
                           G_CALLBACK(diplo_dialog_returnkey), pdialog);

    label = g_object_new(GTK_TYPE_LABEL, "use-underline", TRUE,
                         "mnemonic-widget", spin, "label", _("Gold:"),
                         "xalign", 0.0, "yalign", 0.5, NULL);
    gtk_grid_attach(GTK_GRID(table), label, 0, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(table), aux_menu, 2, 0, 1, 1);
  } else {
    gtk_grid_attach(GTK_GRID(table), aux_menu, 0, 0, 1, 1);
  }
  gtk_widget_insert_action_group(aux_menu, "win", group);

  /* Them. */
  vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_widget_set_margin_start(vbox, 2);
  gtk_widget_set_margin_end(vbox, 2);
  gtk_widget_set_margin_top(vbox, 2);
  gtk_widget_set_margin_bottom(vbox, 2);
  gui_dialog_add_content_widget(pdialog->dialog, vbox);

  /* Their nation. */
  label = gtk_label_new(NULL);
  gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  buf = g_strdup_printf("<span size=\"large\"><u>%s</u></span>",
                        nation_plural_for_player(plr1));
  gtk_label_set_markup(GTK_LABEL(label), buf);
  g_free(buf);
  gtk_box_append(GTK_BOX(vbox), label);

  hgrid = gtk_grid_new();
  grid_col = 0;
  gtk_grid_set_column_spacing(GTK_GRID(hgrid), 5);
  gtk_box_append(GTK_BOX(vbox), hgrid);

  /* Their flag */
  flag_spr = get_nation_flag_sprite(tileset, nation_of_player(plr1));

  pic = gtk_picture_new();
  picture_set_from_surface(GTK_PICTURE(pic), flag_spr->surface);
  gtk_grid_attach(GTK_GRID(hgrid), pic, grid_col++, 0, 1, 1);

  /* Their name. */
  label = gtk_label_new(NULL);
  gtk_widget_set_hexpand(label, TRUE);
  gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  buf = g_strdup_printf("<span size=\"large\" weight=\"bold\">%s</span>",
                        title_for_player(plr1, plr_buf, sizeof(plr_buf)));
  gtk_label_set_markup(GTK_LABEL(label), buf);
  g_free(buf);
  gtk_grid_attach(GTK_GRID(hgrid), label, grid_col++, 0, 1, 1);

  pdialog->pic1 = gtk_picture_new();
  gtk_grid_attach(GTK_GRID(hgrid), pdialog->pic1, grid_col++, 0, 1, 1);

  /* Menu for clauses: they. */
  aux_menu = aux_menu_new();
  group = G_ACTION_GROUP(g_simple_action_group_new());

  menu = create_clause_menu(group, pdialog, plr1, TRUE);
  gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(aux_menu), G_MENU_MODEL(menu));

  /* Main table for clauses and (if activated) gold trading: they. */
  table = gtk_grid_new();
  gtk_widget_set_halign(table, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(table, GTK_ALIGN_CENTER);
  gtk_grid_set_column_spacing(GTK_GRID(table), 16);
  gtk_box_append(GTK_BOX(vbox), table);

  if (clause_enabled(CLAUSE_GOLD)) {
    spin = gtk_spin_button_new_with_range(0.0, plr1->economic.gold + 0.1,
                                          1.0);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 0);
    gtk_editable_set_width_chars(GTK_EDITABLE(spin), 16);
    gtk_grid_attach(GTK_GRID(table), spin, 1, 0, 1, 1);
    g_object_set_data(G_OBJECT(spin), "plr", plr1);
    g_signal_connect_after(spin, "value-changed",
                           G_CALLBACK(diplo_dialog_returnkey), pdialog);

    label = g_object_new(GTK_TYPE_LABEL, "use-underline", TRUE,
                         "mnemonic-widget", spin, "label", _("Gold:"),
                         "xalign", 0.0, "yalign", 0.5, NULL);
    gtk_grid_attach(GTK_GRID(table), label, 0, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(table), aux_menu, 2, 0, 1, 1);
  } else {
    gtk_grid_attach(GTK_GRID(table), aux_menu, 0, 0, 1, 1);
  }
  gtk_widget_insert_action_group(aux_menu, "win", group);

  /* Clauses. */
  mainbox = gtk_grid_new();
  gtk_orientable_set_orientation(GTK_ORIENTABLE(mainbox),
                                 GTK_ORIENTATION_VERTICAL);
  gui_dialog_add_content_widget(pdialog->dialog, mainbox);

  store = gtk_list_store_new(1, G_TYPE_STRING);
  pdialog->store = store;

  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  gtk_widget_set_hexpand(view, TRUE);
  gtk_widget_set_vexpand(view, TRUE);
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);
  g_object_unref(store);
  gtk_widget_set_size_request(view, 320, 100);

  rend = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1, NULL,
    rend, "text", 0, NULL);

  sw = gtk_scrolled_window_new();
  gtk_widget_set_margin_bottom(sw, 2);
  gtk_widget_set_margin_end(sw, 2);
  gtk_widget_set_margin_start(sw, 2);
  gtk_widget_set_margin_top(sw, 2);
  gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(sw), TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), view);

  label = g_object_new(GTK_TYPE_LABEL,
    "use-underline", TRUE,
    "mnemonic-widget", view,
    "label", _("C_lauses:"),
    "xalign", 0.0,
    "yalign", 0.5,
    NULL);

  vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

  gtk_grid_attach(GTK_GRID(mainbox), vbox, 0, main_row++, 1, 1);
  gtk_box_append(GTK_BOX(vbox), label);
  gtk_box_append(GTK_BOX(vbox), sw);

  gtk_widget_set_visible(mainbox, TRUE);

  g_signal_connect(view, "row_activated", G_CALLBACK(row_callback), pdialog);

  update_diplomacy_dialog(pdialog);
  gui_dialog_show_all(pdialog->dialog);

  return pdialog;
}

/************************************************************************//**
  Update diplomacy dialog
****************************************************************************/
static void update_diplomacy_dialog(struct Diplomacy_dialog *pdialog)
{
  GtkListStore *store;
  GtkTreeIter it;
  bool blank = TRUE;
  GdkPixbuf *pixbuf;

  store = pdialog->store;

  gtk_list_store_clear(store);
  clause_list_iterate(pdialog->treaty->clauses, pclause) {
    char buf[128];

    client_diplomacy_clause_string(buf, sizeof(buf), pclause);

    gtk_list_store_append(store, &it);
    gtk_list_store_set(store, &it, 0, buf, -1);
    blank = FALSE;
  } clause_list_iterate_end;

  if (blank) {
    gtk_list_store_append(store, &it);
    gtk_list_store_set(store, &it, 0,
                       _("--- This treaty is blank. "
                         "Please add some clauses. ---"), -1);
  }

  pixbuf = get_thumb_pixbuf(pdialog->treaty->accept0);
  gtk_picture_set_pixbuf(GTK_PICTURE(pdialog->pic0), pixbuf);
  g_object_unref(G_OBJECT(pixbuf));
  pixbuf = get_thumb_pixbuf(pdialog->treaty->accept1);
  gtk_picture_set_pixbuf(GTK_PICTURE(pdialog->pic1), pixbuf);
  g_object_unref(G_OBJECT(pixbuf));
}

/************************************************************************//**
  Callback for the diplomatic dialog: give tech.
****************************************************************************/
static void diplomacy_dialog_tech_callback(GSimpleAction *action,
                                           GVariant *parameter,
                                           gpointer data)
{
  int giver, dest, other, tech;

  giver = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "player_from"));
  dest = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "player_to"));
  tech = GPOINTER_TO_INT(data);
  if (player_by_number(giver) == client_player()) {
    other = dest;
  } else {
    other = giver;
  }

  if (A_LAST == tech) {
    /* All techs. */
    struct player *pgiver = player_by_number(giver);
    struct player *pdest = player_by_number(dest);
    const struct research *dresearch, *gresearch;

    fc_assert_ret(NULL != pgiver);
    fc_assert_ret(NULL != pdest);

    dresearch = research_get(pdest);
    gresearch = research_get(pgiver);
    advance_iterate(padvance) {
      Tech_type_id i = advance_number(padvance);

      if (research_invention_state(gresearch, i) == TECH_KNOWN
          && research_invention_gettable(dresearch, i,
                                         game.info.tech_trade_allow_holes)
          && (research_invention_state(dresearch, i) == TECH_UNKNOWN
              || research_invention_state(dresearch, i)
                 == TECH_PREREQS_KNOWN)) {
        dsend_packet_diplomacy_create_clause_req(&client.conn, other, giver,
                                                 CLAUSE_ADVANCE, i);
      }
    } advance_iterate_end;
  } else {
    /* Only one tech. */
    dsend_packet_diplomacy_create_clause_req(&client.conn, other, giver,
                                             CLAUSE_ADVANCE, tech);
  }
}

/************************************************************************//**
  Callback for trading cities
                              - Kris Bubendorfer
****************************************************************************/
static void diplomacy_dialog_city_callback(GSimpleAction *action,
                                           GVariant *parameter,
                                           gpointer data)
{
  struct city_deal *deal_data = (struct city_deal *)data;
  int other;

  if (player_by_number(deal_data->giver) == client.conn.playing) {
    other = deal_data->receiver;
  } else {
    other = deal_data->giver;
  }

  dsend_packet_diplomacy_create_clause_req(&client.conn, other, deal_data->giver,
                                           CLAUSE_CITY, deal_data->id);

  free(deal_data);
}

/************************************************************************//**
  Map menu item activated
****************************************************************************/
static void diplomacy_dialog_map_callback(GSimpleAction *action, GVariant *parameter,
                                          gpointer data)
{
  struct Diplomacy_dialog *pdialog = (struct Diplomacy_dialog *)data;
  struct player *pgiver;

  pgiver = (struct player *)g_object_get_data(G_OBJECT(action), "plr");

  dsend_packet_diplomacy_create_clause_req(&client.conn,
                                           player_number(pdialog->treaty->plr1),
                                           player_number(pgiver), CLAUSE_MAP, 0);
}

/************************************************************************//**
  Seamap menu item activated
****************************************************************************/
static void diplomacy_dialog_seamap_callback(GSimpleAction *action, GVariant *parameter,
                                             gpointer data)
{
  struct Diplomacy_dialog *pdialog = (struct Diplomacy_dialog *)data;
  struct player *pgiver;

  pgiver = (struct player *)g_object_get_data(G_OBJECT(action), "plr");

  dsend_packet_diplomacy_create_clause_req(&client.conn,
                                           player_number(pdialog->treaty->plr1),
                                           player_number(pgiver), CLAUSE_SEAMAP,
                                           0);
}

/************************************************************************//**
  Adding pact clause
****************************************************************************/
static void diplomacy_dialog_add_pact_clause(gpointer data, int type)
{
  struct Diplomacy_dialog *pdialog = (struct Diplomacy_dialog *)data;

  dsend_packet_diplomacy_create_clause_req(&client.conn,
                                           player_number(pdialog->treaty->plr1),
                                           player_number(pdialog->treaty->plr0),
                                           type, 0);
}

/************************************************************************//**
  Ceasefire pact menu item activated
****************************************************************************/
static void diplomacy_dialog_ceasefire_callback(GSimpleAction *action,
                                                GVariant *parameter,
                                                gpointer data)
{
  diplomacy_dialog_add_pact_clause(data, CLAUSE_CEASEFIRE);
}

/************************************************************************//**
  Peace pact menu item activated
****************************************************************************/
static void diplomacy_dialog_peace_callback(GSimpleAction *action,
                                            GVariant *parameter,
                                            gpointer data)
{
  diplomacy_dialog_add_pact_clause(data, CLAUSE_PEACE);
}

/************************************************************************//**
  Alliance pact menu item activated
****************************************************************************/
static void diplomacy_dialog_alliance_callback(GSimpleAction *action,
                                               GVariant *parameter,
                                               gpointer data)
{
  diplomacy_dialog_add_pact_clause(data, CLAUSE_ALLIANCE);
}

/************************************************************************//**
  Shared vision menu item activated
****************************************************************************/
static void diplomacy_dialog_vision_callback(GSimpleAction *action,
                                             GVariant *parameter,
                                             gpointer data)
{
  struct Diplomacy_dialog *pdialog = (struct Diplomacy_dialog *) data;
  struct player *pgiver;

  pgiver = (struct player *)g_object_get_data(G_OBJECT(action), "plr");

  dsend_packet_diplomacy_create_clause_req(&client.conn,
                                           player_number(pdialog->treaty->plr1),
                                           player_number(pgiver), CLAUSE_VISION,
                                           0);
}

/************************************************************************//**
  Embassy menu item activated
****************************************************************************/
static void diplomacy_dialog_embassy_callback(GSimpleAction *action,
                                              GVariant *parameter,
                                              gpointer data)
{
  struct Diplomacy_dialog *pdialog = (struct Diplomacy_dialog *) data;
  struct player *pgiver;

  pgiver = (struct player *)g_object_get_data(G_OBJECT(action), "plr");

  dsend_packet_diplomacy_create_clause_req(&client.conn,
                                           player_number(pdialog->treaty->plr1),
                                           player_number(pgiver), CLAUSE_EMBASSY,
                                           0);
}

/************************************************************************//**
  Shared tiles menu item activated
****************************************************************************/
static void diplomacy_dialog_shared_tiles_callback(GSimpleAction *action,
                                                   GVariant *parameter,
                                                   gpointer data)
{
  struct Diplomacy_dialog *pdialog = (struct Diplomacy_dialog *) data;
  struct player *pgiver;

  pgiver = (struct player *)g_object_get_data(G_OBJECT(action), "plr");

  dsend_packet_diplomacy_create_clause_req(&client.conn,
                                           player_number(pdialog->treaty->plr1),
                                           player_number(pgiver),
                                           CLAUSE_SHARED_TILES,
                                           0);
}

/************************************************************************//**
  Close diplomacy dialog
****************************************************************************/
void close_diplomacy_dialog(struct Diplomacy_dialog *pdialog)
{
  diplomacy_destroy(pdialog);
}

/************************************************************************//**
  Initialize diplomacy dialog
****************************************************************************/
void diplomacy_dialog_init(void)
{
  dialog_list = dialog_list_new();
  dipl_main = NULL;
}

/************************************************************************//**
  Free resources allocated for diplomacy dialog
****************************************************************************/
void diplomacy_dialog_done(void)
{
  dialog_list_destroy(dialog_list);
}

/************************************************************************//**
  Find diplomacy dialog between player and other player
****************************************************************************/
static struct Diplomacy_dialog *find_diplomacy_dialog(struct player *they)
{
  struct player *plr0 = client.conn.playing;

  dialog_list_iterate(dialog_list, pdialog) {
    if ((pdialog->treaty->plr0 == plr0 && pdialog->treaty->plr1 == they)
        || (pdialog->treaty->plr0 == they && pdialog->treaty->plr1 == plr0)) {
      return pdialog;
    }
  } dialog_list_iterate_end;

  return NULL;
}

/************************************************************************//**
  User hit enter after entering gold amount
****************************************************************************/
static void diplo_dialog_returnkey(GtkWidget *w, gpointer data)
{
  struct Diplomacy_dialog *pdialog = (struct Diplomacy_dialog *) data;
  struct player *pgiver =
      (struct player *) g_object_get_data(G_OBJECT(w), "plr");
  int amount = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w));

  if (amount >= 0 && amount <= pgiver->economic.gold) {
    dsend_packet_diplomacy_create_clause_req(&client.conn,
                                             player_number(pdialog->treaty->plr1),
                                             player_number(pgiver),
                                             CLAUSE_GOLD, amount);
  } else {
    output_window_append(ftc_client, _("Invalid amount of gold specified."));
  }
}

/************************************************************************//**
  Close all dialogs, for when client disconnects from game.
****************************************************************************/
void close_all_diplomacy_dialogs(void)
{
  while (dialog_list_size(dialog_list) > 0) {
    close_diplomacy_dialog(dialog_list_get(dialog_list, 0));
  }
}
