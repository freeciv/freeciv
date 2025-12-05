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

// Qt
#include <QActionGroup>
#include <QApplication>
#include <QFileDialog>
#include <QMainWindow>
#include <QMessageBox>
#include <QScrollArea>
#include <QStandardPaths>
#include <QVBoxLayout>

// utility
#include "string_vector.h"

// common
#include "game.h"
#include "government.h"
#include "goto.h"
#include "name_translation.h"
#include "road.h"
#include "specialist.h"
#include "unit.h"

// client
#include "connectdlg_common.h"
#include "control.h"
#include "helpdata.h"

// gui-qt
#include "fc_client.h"
#include "chatline.h"
#include "cityrep.h"
#include "dialogs.h"
#include "gotodlg.h"
#include "gui_main.h"
#include "hudwidget.h"
#include "mapctrl.h"
#include "messagedlg.h"
#include "plrdlg.h"
#include "ratesdlg.h"
#include "repodlgs.h"
#include "shortcuts.h"
#include "spaceshipdlg.h"
#include "sprite.h"

#include "menu.h"

static void enable_interface(bool enable);

/**********************************************************************//**
  Constructor for trade city used to trade calculation
**************************************************************************/
trade_city::trade_city(struct city *pcity)
{
  city = pcity;
  tile = nullptr;
  trade_num = 0;
  poss_trade_num = 0;
}

/**********************************************************************//**
  Constructor for trade calculator
**************************************************************************/
trade_generator::trade_generator()
{
  hover_city = false;
}

/**********************************************************************//**
  Adds all cities to trade generator
**************************************************************************/
void trade_generator::add_all_cities()
{
  int i, s;
  struct city *pcity;
  struct player *pplayer = client_player();

  clear_trade_planing();
  s = city_list_size(pplayer->cities);
  if (s == 0) {
    return;
  }
  for (i = 0; i < s; i++) {
    pcity = city_list_get(pplayer->cities, i);
    add_city(pcity);
  }
}

/**********************************************************************//**
  Clears generated routes, virtual cities, cities
**************************************************************************/
void trade_generator::clear_trade_planing()
{
  struct city *pcity;
  trade_city *tc;

  foreach(pcity, virtual_cities) {
    destroy_city_virtual(pcity);
  }
  virtual_cities.clear();
  foreach(tc, cities) {
    delete tc;
  }
  cities.clear();
  lines.clear();
  gui()->mapview_wdg->repaint();
}

/**********************************************************************//**
  Adds single city to trade generator
**************************************************************************/
void trade_generator::add_city(struct city *pcity)
{
  trade_city *tc = new trade_city(pcity);

  cities.append(tc);
  gui()->infotab->chtwdg->append(QString(_("Adding city %1 to trade planning"))
                                 .arg(tc->city->name));
}

/**********************************************************************//**
  Adds/removes tile to trade generator
**************************************************************************/
void trade_generator::add_tile(struct tile *ptile)
{
  struct city *pcity;
  trade_city *tc;

  pcity = tile_city(ptile);

  foreach (tc, cities) {
    if (pcity != nullptr) {
      if (tc->city == pcity) {
        remove_city(pcity);
        return;
      }
    }
    if (tc->city->tile == ptile) {
      remove_virtual_city(ptile);
      return;
    }
  }

  if (pcity != nullptr) {
    add_city(pcity);
    return;
  }

  pcity = create_city_virtual(client_player(), ptile, "Virtual");
  add_city(pcity);
  virtual_cities.append(pcity);
}

/**********************************************************************//**
  Removes single city from trade generator
**************************************************************************/
void trade_generator::remove_city(struct city* pcity)
{
  trade_city *tc;

  foreach (tc, cities) {
    if (tc->city->tile == pcity->tile) {
      cities.removeAll(tc);
      gui()->infotab->chtwdg->append(
        QString(_("Removing city %1 from trade planning"))
        .arg(tc->city->name));
      return;
    }
  }
}

/**********************************************************************//**
  Removes virtual city from trade generator
**************************************************************************/
void trade_generator::remove_virtual_city(tile *ptile)
{
  struct city *c;
  trade_city *tc;

  foreach (c, virtual_cities) {
    if (c->tile == ptile) {
        virtual_cities.removeAll(c);
        gui()->infotab->chtwdg->append(
          QString(_("Removing city %1 from trade planning")).arg(c->name));
    }
  }

  foreach (tc, cities) {
    if (tc->city->tile == ptile) {
        cities.removeAll(tc);
        return;
    }
  }
}

/**********************************************************************//**
  Inner foreach() loop of trade_generator::calculate()
  Implemented as separate function to avoid shadow warnings about
  internal variables of foreach() inside foreach()
**************************************************************************/
void trade_generator::calculate_inner(trade_city *tc)
{
  trade_city *ttc;

  foreach (ttc, cities) {
    if (!have_cities_trade_route(tc->city, ttc->city)
        && can_establish_trade_route(tc->city, ttc->city, GOODS_HIGH_PRIORITY)) {
      tc->poss_trade_num++;
      tc->pos_cities.append(ttc->city);
    }
    tc->over_max = tc->trade_num + tc->poss_trade_num
      - max_trade_routes(tc->city);
  }
}

/**********************************************************************//**
  Finds trade routes to establish
**************************************************************************/
void trade_generator::calculate()
{
  trade_city *tc;
  int i;
  bool tdone;
  int count = cities.size();
  int *cities_ids = (int *)fc_malloc(sizeof(int) * count);
  trade_city **cities_order = (trade_city **)fc_malloc(sizeof(trade_city *) * count);

  for (i = 0; i < 100; i++) {
    int n;

    tdone = true;

    for (n = 0; n < count; n++) {
      cities_ids[n] = n;
      cities_order[n] = cities[n];
    }
    array_shuffle(cities_ids, count);

    cities.clear();
    for (n = 0; n < count; n++) {
      cities.append(cities_order[cities_ids[n]]);
    }

    lines.clear();
    foreach (tc, cities) {
      tc->pos_cities.clear();
      tc->new_tr_cities.clear();
      tc->curr_tr_cities.clear();
    }
    foreach (tc, cities) {
      tc->trade_num = city_num_trade_routes(tc->city);
      tc->poss_trade_num = 0;
      tc->pos_cities.clear();
      tc->new_tr_cities.clear();
      tc->curr_tr_cities.clear();
      tc->done = false;
      calculate_inner(tc);
    }

    find_certain_routes();
    discard();
    find_certain_routes();

    foreach (tc, cities) {
      if (!tc->done) {
        tdone = false;
      }
    }
    if (tdone) {
      break;
    }
  }
  foreach (tc, cities) {
    if (!tc->done) {
      char text[1024];
      fc_snprintf(text, sizeof(text),
                  PL_("City %s - 1 free trade route.",
                      "City %s - %d free trade routes.",
                      max_trade_routes(tc->city) - tc->trade_num),
                  city_link(tc->city),
                  max_trade_routes(tc->city) - tc->trade_num);
      output_window_append(ftc_client, text);
    }
  }

  free(cities_ids);
  free(cities_order);

  gui()->mapview_wdg->repaint();
}

/**********************************************************************//**
  Finds highest number of trade routes over maximum for all cities,
  skips given city
**************************************************************************/
int trade_generator::find_over_max(struct city *pcity = nullptr)
{
  trade_city *tc;
  int max = 0;

  foreach (tc, cities) {
    if (pcity != tc->city) {
      max = qMax(max, tc->over_max);
    }
  }
  return max;
}

/**********************************************************************//**
  Finds city with highest trade routes possible
**************************************************************************/
trade_city* trade_generator::find_most_free()
{
  trade_city *tc;
  trade_city *rc = nullptr;
  int max = 0;

  foreach (tc, cities) {
    if (max < tc->over_max) {
      max = tc->over_max;
      rc = tc;
    }
  }
  return rc;
}

/**********************************************************************//**
  Drops all possible trade routes.
**************************************************************************/
void trade_generator::discard()
{
  trade_city *tc;
  int j = 5;

  for (int i = j; i > -j; i--) {
    while ((tc = find_most_free())) {
      if (!discard_one(tc)) {
        if (!discard_any(tc, i)) {
          break;
        }
      }
    }
  }
}

/**********************************************************************//**
  Drops trade routes between given cities
**************************************************************************/
void trade_generator::discard_trade(trade_city* tc, trade_city* ttc)
{
  tc->pos_cities.removeOne(ttc->city);
  ttc->pos_cities.removeOne(tc->city);
  tc->poss_trade_num--;
  ttc->poss_trade_num--;
  tc->over_max--;
  ttc->over_max--;
  check_if_done(tc, ttc);
}

/**********************************************************************//**
  Drops one trade route for given city if possible
**************************************************************************/
bool trade_generator::discard_one(trade_city *tc)
{
  int best = 0;
  int current_candidate = 0;
  int best_id;
  trade_city *ttc;

  for (int i = cities.size() - 1 ; i >= 0; i--) {
    ttc = cities.at(i);
    current_candidate = ttc->over_max;
    if (current_candidate > best) {
      best_id = i;
    }
  }
  if (best == 0) {
    return false;
  }

  ttc = cities.at(best_id);
  discard_trade(tc, ttc);
  return true;
}

/**********************************************************************//**
  Drops all trade routes for given city
**************************************************************************/
bool trade_generator::discard_any(trade_city* tc, int freeroutes)
{
  trade_city *ttc;

  for (int i = cities.size() - 1 ; i >= 0; i--) {
    ttc = cities.at(i);
    if (tc->pos_cities.contains(ttc->city)
        && ttc->pos_cities.contains(tc->city)
        && ttc->over_max > freeroutes) {
      discard_trade(tc, ttc);
      return true;
    }
  }
  return false;
}

/**********************************************************************//**
  Inner foreach() loop of trade_generator::find_certain_routes()
  Implemented as separate function to avoid shadow warnings about
  internal variables of foreach() inside foreach()
**************************************************************************/
void trade_generator::find_certain_routes_inner(trade_city *tc)
{
  trade_city *ttc;

  foreach (ttc, cities) {
    if (ttc->done || ttc->over_max > 0
        || tc == ttc || tc->done || tc->over_max > 0) {
      continue;
    }
    if (tc->pos_cities.contains(ttc->city)
        && ttc->pos_cities.contains(tc->city)) {
      struct qtiles gilles;

      tc->pos_cities.removeOne(ttc->city);
      ttc->pos_cities.removeOne(tc->city);
      tc->poss_trade_num--;
      ttc->poss_trade_num--;
      tc->new_tr_cities.append(ttc->city);
      ttc->new_tr_cities.append(ttc->city);
      tc->trade_num++;
      ttc->trade_num++;
      tc->over_max--;
      ttc->over_max--;
      check_if_done(tc, ttc);
      gilles.t1 = tc->city->tile;
      gilles.t2 = ttc->city->tile;
      gilles.autocaravan = nullptr;
      lines.append(gilles);
    }
  }
}

/**********************************************************************//**
  Adds routes for cities which can only have maximum possible trade routes
**************************************************************************/
void trade_generator::find_certain_routes()
{
  trade_city *tc;

  foreach (tc, cities) {
    if (tc->done || tc->over_max > 0) {
      continue;
    }
    find_certain_routes_inner(tc);
  }
}

/**********************************************************************//**
  Marks cities with full trade routes to finish searching
**************************************************************************/
void trade_generator::check_if_done(trade_city* tc1, trade_city* tc2)
{
  if (tc1->trade_num == max_trade_routes(tc1->city)) {
    tc1->done = true;
  }
  if (tc2->trade_num == max_trade_routes(tc2->city)) {
    tc2->done = true;
  }
}

/**********************************************************************//**
  Constructor for units used in delayed orders
**************************************************************************/
qfc_units_list::qfc_units_list()
{
}

/**********************************************************************//**
  Adds given unit to list
**************************************************************************/
void qfc_units_list::add(qfc_delayed_unit_item* fui)
{
  unit_list.append(fui);
}

/**********************************************************************//**
  Clears list of units
**************************************************************************/
void qfc_units_list::clear()
{
  unit_list.clear();
}

/**********************************************************************//**
  Initialize menus (sensitivity, name, etc.) based on the
  current state and current ruleset, etc.  Call menus_update().
**************************************************************************/
void real_menus_init(void)
{
  if (!game.client.ruleset_ready) {
    return;
  }
  gui()->menu_bar->clear();
  gui()->menu_bar->setup_menus();

  gov_menu::create_all();

  // A new ruleset may have been loaded.
  go_act_menu::reset_all();
}

/**********************************************************************//**
  Update all of the menus (sensitivity, name, etc.) based on the
  current state.
**************************************************************************/
void real_menus_update(void)
{
  if (C_S_RUNNING <= client_state()) {
    gui()->menuBar()->setVisible(true);
    if (!is_waiting_turn_change()) {
      gui()->menu_bar->menus_sensitive();
      gui()->menu_bar->update_airlift_menu();
      gui()->menu_bar->update_roads_menu();
      gui()->menu_bar->update_bases_menu();
      gov_menu::update_all();
      go_act_menu::update_all();
      gui()->unitinfo_wdg->update_actions(nullptr);
    }
  } else {
    gui()->menuBar()->setVisible(false);
  }
}

/**********************************************************************//**
  Return the text for the tile, changed by the activity.
  Should only be called for irrigation, mining, or transformation, and
  only when the activity changes the base terrain type.
**************************************************************************/
static const char *get_tile_change_menu_text(struct tile *ptile,
                                             enum unit_activity activity)
{
  struct tile *newtile = tile_virtual_new(ptile);
  const char *text;

  tile_apply_activity(newtile, activity, nullptr);
  text = tile_get_info_text(newtile, FALSE, 0);
  tile_virtual_destroy(newtile);

  return text;
}

/**********************************************************************//**
  Creates a new government menu.
**************************************************************************/
gov_menu::gov_menu(QWidget* parent) :
  QMenu(_("Government"), parent)
{
  // Register ourselves to get updates for free.
  instances << this;
  setAttribute(Qt::WA_TranslucentBackground);
}

/**********************************************************************//**
  Destructor.
**************************************************************************/
gov_menu::~gov_menu()
{
  qDeleteAll(actions);
  instances.remove(this);
}

/**********************************************************************//**
  Creates the menu once the government list is known.
**************************************************************************/
void gov_menu::create() {
  QAction *action;
  struct government *gov, *revol_gov;
  int gov_count, i;

  // Clear any content
  foreach(action, QWidget::actions()) {
    removeAction(action);
    action->deleteLater();
  }
  actions.clear();

  gov_count = government_count();
  actions.reserve(gov_count + 1);
  action = addAction(_("Revolution..."));
  action->setShortcut(QKeySequence(tr("ctrl+shift+g")));
  connect(action, &QAction::triggered, this, &gov_menu::revolution);
  actions.append(action);

  addSeparator();

  // Add an action for each government. There is no icon yet.
  revol_gov = game.government_during_revolution;
  for (i = 0; i < gov_count; ++i) {
    gov = government_by_number(i);
    if (gov != revol_gov) { // Skip revolution government
      // Defeat keyboard shortcut mnemonics
      action = addAction(QString(government_name_translation(gov))
                         .replace("&", "&&"));
      // We need to keep track of the gov <-> action mapping to be able to
      // set enabled/disabled depending on available govs.
      actions.append(action);
      QObject::connect(action, &QAction::triggered, [this,i]() {
        change_gov(i);
      });
    }
  }
}

/**********************************************************************//**
  Updates the menu to take gov availability into account.
**************************************************************************/
void gov_menu::update()
{
  struct government *gov, *revol_gov;
  struct sprite *sprite;
  int gov_count, i, j;

  gov_count = government_count();
  revol_gov = game.government_during_revolution;
  for (i = 0, j = 0; i < gov_count; i++) {
    gov = government_by_number(i);
    if (gov != revol_gov) { // Skip revolution government
      sprite = get_government_sprite(tileset, gov);
      if (sprite != nullptr) {
        actions[j + 1]->setIcon(QIcon(*(sprite->pm)));
      }
      actions[j + 1]->setEnabled(
        can_change_to_government(client_player(), gov));
      j++;
    } else {
      actions[0]->setEnabled(!client_is_observer()
                             && untargeted_revolution_allowed());
    }
  }
}

/**********************************************************************//**
  Shows the dialog asking for confirmation before starting a revolution.
**************************************************************************/
void gov_menu::revolution()
{
  popup_revolution_dialog();
}

/**********************************************************************//**
  Shows the dialog asking for confirmation before starting a revolution.
**************************************************************************/
void gov_menu::change_gov(int target_gov)
{
  popup_revolution_dialog(government_by_number(target_gov));
}

/**************************************************************************
  Keeps track of all gov_menu instances.
**************************************************************************/
QSet<gov_menu *> gov_menu::instances = QSet<gov_menu *>();

/**********************************************************************//**
  Updates all gov_menu instances.
**************************************************************************/
void gov_menu::create_all()
{
  foreach (gov_menu *m, instances) {
    m->create();
  }
}

/**********************************************************************//**
  Updates all gov_menu instances.
**************************************************************************/
void gov_menu::update_all()
{
  foreach (gov_menu *m, instances) {
    m->update();
  }
}

/**********************************************************************//**
  Instantiate a new goto and act sub menu.
**************************************************************************/
go_act_menu::go_act_menu(QWidget *parent)
  : QMenu(_("Go to and..."), parent)
{
  // Will need auto updates etc.
  instances << this;
}

/**********************************************************************//**
  Destructor.
**************************************************************************/
go_act_menu::~go_act_menu()
{
  // Updates are no longer needed.
  instances.remove(this);
}

/**********************************************************************//**
  Reset the goto and act menu so it will be recreated.
**************************************************************************/
void go_act_menu::reset()
{
  // Clear menu item to action ID mapping.
  items.clear();

  // Remove the menu items
  clear();
}

/**********************************************************************//**
  Fill the new goto and act sub menu with menu items.
**************************************************************************/
void go_act_menu::create()
{
  QAction *item;
  int tgt_kind_group;

  // Group goto and perform action menu items by target kind.
  for (tgt_kind_group = 0; tgt_kind_group < ATK_COUNT; tgt_kind_group++) {
    action_noninternal_iterate(act_id) {
      struct action *paction = action_by_number(act_id);
      QString action_name = (QString(action_name_translation(paction))
                             .replace("&", "&&"));

      if (action_id_get_actor_kind(act_id) != AAK_UNIT) {
        // This action isn't performed by an unit.
        continue;
      }

      if (action_id_get_target_kind(act_id) != tgt_kind_group) {
        // Wrong group.
        continue;
      }

      if (action_id_has_complex_target(act_id)) {
        QMenu *sub_target_menu = addMenu(action_name);
        items.insert(sub_target_menu->menuAction(), act_id);

#define CREATE_SUB_ITEM(_menu_, _act_id_, _sub_tgt_id_, _sub_tgt_name_)   \
  {                                                                       \
    QAction *_sub_item_ = _menu_->addAction(_sub_tgt_name_);              \
    int _sub_target_id_ = _sub_tgt_id_;                                   \
    QObject::connect(_sub_item_, &QAction::triggered,                     \
                     [this, _act_id_, _sub_target_id_]() {                \
      start_go_act(_act_id_, _sub_target_id_);                            \
    });                                                                   \
  }

        switch (action_get_sub_target_kind(paction)) {
        case ASTK_BUILDING:
          improvement_iterate(pimpr) {
            CREATE_SUB_ITEM(sub_target_menu, act_id,
                            improvement_number(pimpr),
                            improvement_name_translation(pimpr));
          } improvement_iterate_end;
          break;
        case ASTK_TECH:
          advance_iterate(ptech) {
            CREATE_SUB_ITEM(sub_target_menu, act_id,
                            advance_number(ptech),
                            advance_name_translation(ptech));
          } advance_iterate_end;
          break;
        case ASTK_EXTRA:
        case ASTK_EXTRA_NOT_THERE:
          extra_type_iterate(pextra) {
            if (!actres_creates_extra(paction->result, pextra)
                && !actres_removes_extra(paction->result, pextra)) {
              // Not relevant
              continue;
            }

            CREATE_SUB_ITEM(sub_target_menu, act_id,
                            extra_number(pextra),
                            extra_name_translation(pextra));
          } extra_type_iterate_end;
          break;
        case ASTK_SPECIALIST:
          specialist_type_iterate(spc) {
            CREATE_SUB_ITEM(sub_target_menu, act_id, spc,
                            specialist_plural_translation(specialist_by_number(spc)));
          } specialist_type_iterate_end;
          break;
        case ASTK_NONE:
          // Should not be here.
          fc_assert(action_get_sub_target_kind(paction) != ASTK_NONE);
          break;
        case ASTK_COUNT:
          // Should not exits
          fc_assert(action_get_sub_target_kind(paction) != ASTK_COUNT);
          break;
        }
        continue;
      }

#define ADD_OLD_SHORTCUT(wanted_action_id, sc_id)                         \
  if (act_id == wanted_action_id) {                                    \
    item->setShortcut(QKeySequence(shortcut_to_string(                    \
                      fc_shortcuts::sc()->get_shortcut(sc_id))));         \
  }

      /* Create and add the menu item. It will be hidden or shown based on
       * unit type.  */
      item = addAction(action_name);
      items.insert(item, act_id);

      /* Add the keyboard shortcuts for "Go to and..." menu items that
       * existed independently before the "Go to and..." menu arrived. */
      ADD_OLD_SHORTCUT(ACTION_FOUND_CITY, SC_GOBUILDCITY);
      ADD_OLD_SHORTCUT(ACTION_JOIN_CITY, SC_GOJOINCITY);
      ADD_OLD_SHORTCUT(ACTION_NUKE, SC_NUKE);

      QObject::connect(item, &QAction::triggered, [this,act_id]() {
        start_go_act(act_id, NO_TARGET);
      });
    } action_noninternal_iterate_end;
  }
}

/**********************************************************************//**
  Update the goto and act menu based on the selected unit(s)
**************************************************************************/
void go_act_menu::update()
{
  bool can_do_something = false;

  if (!actions_are_ready()) {
    // Nothing to do.
    return;
  }

  if (items.isEmpty()) {
    // The goto and act menu needs menu items.
    create();
  }

  /* Enable a menu item if it is theoretically possible that one of the
   * selected units can perform it. Checking if the action can be performed
   * at the current tile is pointless since it should be performed at the
   * target tile. */
  foreach(QAction *item, items.keys()) {
    if (units_can_do_action(get_units_in_focus(),
                            items.value(item), TRUE)) {
      item->setVisible(true);
      can_do_something = true;
    } else {
      item->setVisible(false);
    }
  }

  if (can_do_something) {
    // At least one menu item is enabled for one of the selected units.
    setEnabled(true);
  } else {
    // No menu item is enabled any of the selected units.
    setEnabled(false);
  }
}

/**********************************************************************//**
  Activate the goto system
**************************************************************************/
void go_act_menu::start_go_act(int act_id, int sub_tgt_id)
{
  request_unit_goto(ORDER_PERFORM_ACTION, act_id, sub_tgt_id);
}

/**************************************************************************
  Store all goto and act menu items so they can be updated etc
**************************************************************************/
QSet<go_act_menu *> go_act_menu::instances;

/**********************************************************************//**
  Reset all goto and act menu instances.
**************************************************************************/
void go_act_menu::reset_all()
{
  foreach (go_act_menu *m, instances) {
    m->reset();
  }
}

/**********************************************************************//**
  Update all goto and act menu instances
**************************************************************************/
void go_act_menu::update_all()
{
  foreach (go_act_menu *m, instances) {
    m->update();
  }
}

/**********************************************************************//**
  Predicts last unit position
**************************************************************************/
struct tile *mr_menu::find_last_unit_pos(unit *punit, int pos)
{
  qfc_delayed_unit_item *fui;
  struct tile *ptile = nullptr;
  struct unit *zunit;
  struct unit *qunit;

  int i = 0;
  qunit = punit;
  foreach (fui, units_list.unit_list) {
    zunit = unit_list_find(client_player()->units, fui->id);
    i++;
    if (i >= pos) {
      punit = qunit;
      return ptile;
    }
    if (zunit == nullptr) {
      continue;
    }

    if (punit == zunit) {  // Unit found
      /* Unit was ordered to attack city so it might stay in
         front of that city */
      if (is_non_allied_city_tile(fui->ptile, unit_owner(punit))) {
        ptile = tile_before_end_path(punit, fui->ptile);
        if (ptile == nullptr) {
          ptile = fui->ptile;
        }
      } else {
        ptile = fui->ptile;
      }
      // Unit found in transport
    } else if (unit_contained_in(punit, zunit)) {
      ptile = fui->ptile;
    }
  }
  return nullptr;
}

/**********************************************************************//**
  Constructor for global menubar in gameview
**************************************************************************/
mr_menu::mr_menu() : QMenuBar()
{
}

/**********************************************************************//**
  Initializes menu system, and add custom enum(munit) for most of options
  Notice that if you set option for QAction->setChecked(option) it will
  check/uncheck automatically without any intervention
**************************************************************************/
void mr_menu::setup_menus()
{
  QAction *act;
  QMenu *sub_menu;
  QMenu *main_menu;
  QList<QMenu*> menus;
  int i;

  delayed_order = false;
  airlift_type_id = 0;
  quick_airlifting = false;

  // Game Menu
  main_menu = this->addMenu(_("Game"));

#ifdef __APPLE__
  // On Mac, Qt would try to move menu entry named just "Options" to
  // application menu, but as this is submenu and not an action
  // 1) It would fail to appear in the destination
  // 2) It's impossible to override the behavior with QAction::menuRule()
  // We add an invisible character for the string comparison to fail.
  sub_menu = main_menu->addMenu(QString("\u200B") + _("Options"));
#else  // __APPLE__
  sub_menu = main_menu->addMenu(_("Options"));
#endif // __APPLE__

  act = sub_menu->addAction(_("Set local options"));
  connect(act, &QAction::triggered, this, &mr_menu::local_options);
  act = sub_menu->addAction(_("Server Options"));
  connect(act, &QAction::triggered, this, &mr_menu::server_options);
  act = sub_menu->addAction(_("Messages"));
  connect(act, &QAction::triggered, this, &mr_menu::messages_options);
  act = sub_menu->addAction(_("Shortcuts"));
  connect(act, &QAction::triggered, this, &mr_menu::shortcut_options);
  act = sub_menu->addAction(_("Load another tileset"));
  connect(act, &QAction::triggered, this, &mr_menu::tileset_custom_load);
  act = sub_menu->addAction(_("Save Options Now"));
  act->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
  connect(act, &QAction::triggered, this, &mr_menu::save_options_now);
  act = sub_menu->addAction(_("Save Options on Exit"));
  act->setCheckable(true);
  act->setChecked(gui_options.save_options_on_exit);
  main_menu->addSeparator();
  act = main_menu->addAction(_("Save Game"));
  act->setShortcut(QKeySequence(tr("Ctrl+s")));
  act->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
  menu_list.insert(SAVE, act);
  connect(act, &QAction::triggered, this, &mr_menu::save_game);
  act = main_menu->addAction(_("Save Game As..."));
  menu_list.insert(SAVE, act);
  act->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
  connect(act, &QAction::triggered, this, &mr_menu::save_game_as);
  act = main_menu->addAction(_("Save Map to Image"));
  connect(act, &QAction::triggered, this, &mr_menu::save_image);
  main_menu->addSeparator();
  act = main_menu->addAction(_("Volume Up"));
  act->setShortcut(QKeySequence(tr(">")));
  connect(act, &QAction::triggered, this, &mr_menu::volume_up);
  act = main_menu->addAction(_("Volume Down"));
  act->setShortcut(QKeySequence(tr("<")));
  connect(act, &QAction::triggered, this, &mr_menu::volume_down);
  main_menu->addSeparator();
  act = main_menu->addAction(_("Leave game"));
  act->setIcon(style()->standardIcon(QStyle::SP_DialogDiscardButton));
  connect(act, &QAction::triggered, this, &mr_menu::back_to_menu);
  act = main_menu->addAction(_("Quit"));
  act->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
  connect(act, &QAction::triggered, this, &mr_menu::quit_game);

  // View Menu
  main_menu = this->addMenu(Q_("?verb:View"));
  act = main_menu->addAction(_("Center View"));
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_CENTER_VIEW))));
  connect(act, &QAction::triggered, this, &mr_menu::slot_center_view);
  main_menu->addSeparator();
  act = main_menu->addAction(_("Fullscreen"));
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_FULLSCREEN))));
  act->setCheckable(true);
  act->setChecked(gui_options.gui_qt_fullscreen);
  connect(act, &QAction::triggered, this, &mr_menu::slot_fullscreen);
  main_menu->addSeparator();
  minimap_status = main_menu->addAction(_("Minimap"));
  minimap_status->setCheckable(true);
  minimap_status->setShortcut(QKeySequence(shortcut_to_string(
                             fc_shortcuts::sc()->get_shortcut(SC_MINIMAP))));
  minimap_status->setChecked(true);
  connect(minimap_status, &QAction::triggered, this,
          &mr_menu::slot_minimap_view);
  osd_status = main_menu->addAction(_("Show new turn information"));
  osd_status->setCheckable(true);
  osd_status->setChecked(gui()->qt_settings.show_new_turn_text);
  connect(osd_status, &QAction::triggered, this,
          &mr_menu::slot_show_new_turn_text);
  btlog_status = main_menu->addAction(_("Show combat detailed information"));
  btlog_status->setCheckable(true);
  btlog_status->setChecked(gui()->qt_settings.show_battle_log);
  connect(btlog_status, &QAction::triggered, this, &mr_menu::slot_battlelog);
  lock_status = main_menu->addAction(_("Lock interface"));
  lock_status->setCheckable(true);
  lock_status->setShortcut(QKeySequence(shortcut_to_string(
                           fc_shortcuts::sc()->get_shortcut(SC_IFACE_LOCK))));
  lock_status->setChecked(false);
  connect(lock_status, &QAction::triggered, this, &mr_menu::slot_lock);
  connect(minimap_status, &QAction::triggered, this, &mr_menu::slot_lock);
  main_menu->addSeparator();
  act = main_menu->addAction(_("Zoom in"));
  act->setShortcut(QKeySequence(shortcut_to_string(
                          fc_shortcuts::sc()->get_shortcut(SC_ZOOM_IN))));
  connect(act, &QAction::triggered, this, &mr_menu::zoom_in);
  act = main_menu->addAction(_("Zoom default"));
  act->setShortcut(QKeySequence(shortcut_to_string(
                          fc_shortcuts::sc()->get_shortcut(SC_ZOOM_RESET))));
  connect(act, &QAction::triggered, this, &mr_menu::zoom_reset);
  act = main_menu->addAction(_("Zoom out"));
  act->setShortcut(QKeySequence(shortcut_to_string(
                          fc_shortcuts::sc()->get_shortcut(SC_ZOOM_OUT))));
  connect(act, &QAction::triggered, this, &mr_menu::zoom_out);
  scale_fonts_status = main_menu->addAction(_("Scale fonts"));
  connect(scale_fonts_status, &QAction::triggered, this,
          &mr_menu::zoom_scale_fonts);
  scale_fonts_status->setCheckable(true);
  scale_fonts_status->setChecked(true);
  main_menu->addSeparator();
  act = main_menu->addAction(_("City Outlines"));
  act->setShortcut(QKeySequence(tr("ctrl+y")));
  act->setCheckable(true);
  act->setChecked(gui_options.draw_city_outlines);
  connect(act, &QAction::triggered, this, &mr_menu::slot_city_outlines);
  act = main_menu->addAction(_("City Output"));
  act->setCheckable(true);
  act->setChecked(gui_options.draw_city_output);
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_CITY_OUTPUT))));
  connect(act, &QAction::triggered, this, &mr_menu::slot_city_output);
  act = main_menu->addAction(_("Map Grid"));
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_MAP_GRID))));
  act->setCheckable(true);
  act->setChecked(gui_options.draw_map_grid);
  connect(act, &QAction::triggered, this, &mr_menu::slot_map_grid);
  act = main_menu->addAction(_("National Borders"));
  act->setCheckable(true);
  act->setChecked(gui_options.draw_borders);
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_NAT_BORDERS))));
  connect(act, &QAction::triggered, this, &mr_menu::slot_borders);
  act = main_menu->addAction(_("Native Tiles"));
  act->setCheckable(true);
  act->setChecked(gui_options.draw_native);
  act->setShortcut(QKeySequence(tr("ctrl+shift+n")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_native_tiles);
  act = main_menu->addAction(_("City Full Bar"));
  act->setCheckable(true);
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_SHOW_FULLBAR))));
  act->setChecked(gui_options.draw_full_citybar);
  connect(act, &QAction::triggered, this, &mr_menu::slot_fullbar);
  act = main_menu->addAction(_("City Names"));
  act->setCheckable(true);
  act->setChecked(gui_options.draw_city_names);
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_CITY_NAMES))));
  connect(act, &QAction::triggered, this, &mr_menu::slot_city_names);
  act = main_menu->addAction(_("City Growth"));
  act->setCheckable(true);
  act->setChecked(gui_options.draw_city_growth);
  act->setShortcut(QKeySequence(tr("ctrl+o")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_city_growth);
  act = main_menu->addAction(_("City Production"));
  act->setCheckable(true);
  act->setChecked(gui_options.draw_city_productions);
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_CITY_PROD))));
  connect(act, &QAction::triggered, this, &mr_menu::slot_city_production);
  act = main_menu->addAction(_("City Buy Cost"));
  act->setCheckable(true);
  act->setChecked(gui_options.draw_city_buycost);
  connect(act, &QAction::triggered, this, &mr_menu::slot_city_buycost);
  act = main_menu->addAction(_("City Trade Routes"));
  act->setCheckable(true);
  act->setChecked(gui_options.draw_city_trade_routes);
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_TRADE_ROUTES))));
  connect(act, &QAction::triggered, this, &mr_menu::slot_city_trade_routes);
  act = main_menu->addAction(_("Unit Stack Size"));
  act->setCheckable(true);
  act->setChecked(gui_options.draw_unit_stack_size);
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_STACK_SIZE))));
  connect(act, &QAction::triggered, this, &mr_menu::slot_stack_size);

  // Select Menu
  main_menu = this->addMenu(_("Select"));
  act = main_menu->addAction(_("Single Unit (Unselect Others)"));
  act->setShortcut(QKeySequence(tr("z")));
  menu_list.insert(STANDARD, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_select_one);
  act = main_menu->addAction(_("All On Tile"));
  act->setShortcut(QKeySequence(tr("v")));
  menu_list.insert(STANDARD, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_select_all_tile);
  main_menu->addSeparator();
  act = main_menu->addAction(_("Same Type on Tile"));
  act->setShortcut(QKeySequence(tr("shift+v")));
  menu_list.insert(STANDARD, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_select_same_tile);
  act = main_menu->addAction(_("Same Type on Continent"));
  act->setShortcut(QKeySequence(tr("shift+c")));
  menu_list.insert(STANDARD, act);
  connect(act, &QAction::triggered, this,
          &mr_menu::slot_select_same_continent);
  act = main_menu->addAction(_("Same Type Everywhere"));
  act->setShortcut(QKeySequence(tr("shift+x")));
  menu_list.insert(STANDARD, act);
  connect(act, &QAction::triggered, this,
          &mr_menu::slot_select_same_everywhere);
  main_menu->addSeparator();
  act = main_menu->addAction(_("Wait"));
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_WAIT))));
  menu_list.insert(STANDARD, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_wait);
  act = main_menu->addAction(_("Done"));
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_DONE_MOVING))));
  menu_list.insert(STANDARD, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_done_moving);

  act = main_menu->addAction(_("Advanced unit selection"));
  act->setShortcut(QKeySequence(tr("ctrl+e")));
  menu_list.insert(NOT_4_OBS, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_unit_filter);

  // Unit Menu
  main_menu = this->addMenu(_("Unit"));
  act = main_menu->addAction(_("Go to Tile"));
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_GOTO))));
  menu_list.insert(STANDARD, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_unit_goto);

  // The goto and act sub menu is handled as a separate object.
  main_menu->addMenu(new go_act_menu(this));

  act = main_menu->addAction(_("Go to Nearest City"));
  act->setShortcut(QKeySequence(tr("shift+g")));
  menu_list.insert(GOTO_CITY, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_return_to_city);
  act = main_menu->addAction(_("Go to/Airlift to City..."));
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_GOTOAIRLIFT))));
  menu_list.insert(AIRLIFT, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_airlift);
  main_menu->addSeparator();
  act = main_menu->addAction(_("Auto Explore"));
  menu_list.insert(EXPLORE, act);
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_AUTOEXPLORE))));
  connect(act, &QAction::triggered, this, &mr_menu::slot_unit_explore);
  act = main_menu->addAction(_("Patrol"));
  menu_list.insert(STANDARD, act);
  act->setEnabled(false);
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_PATROL))));
  connect(act, &QAction::triggered, this, &mr_menu::slot_patrol);
  main_menu->addSeparator();
  act = main_menu->addAction(_("Sentry"));
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_SENTRY))));
  menu_list.insert(SENTRY, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_unit_sentry);
  act = main_menu->addAction(_("Unsentry All On Tile"));
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_UNSENTRY_TILE))));
  menu_list.insert(WAKEUP, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_unsentry);
  main_menu->addSeparator();
  act = main_menu->addAction(_("Load"));
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_BOARD))));
  menu_list.insert(BOARD, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_board);
  act = main_menu->addAction(_("Unload"));
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_DEBOARD))));
  menu_list.insert(DEBOARD, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_deboard);
  act = main_menu->addAction(_("Unload All From Transporter"));
  act->setShortcut(QKeySequence(tr("shift+t")));
  menu_list.insert(TRANSPORTER, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_unload_all);
  main_menu->addSeparator();

  // Defeat keyboard shortcut mnemonics
  act = main_menu->addAction(QString(action_id_name_translation(ACTION_HOME_CITY))
                             .replace("&", "&&"));
  menu_list.insert(HOMECITY, act);
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_SETHOME))));
  connect(act, &QAction::triggered, this, &mr_menu::slot_set_home);
  act = main_menu->addAction(_("Upgrade"));
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_UPGRADE_UNIT))));
  menu_list.insert(UPGRADE, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_upgrade);
  act = main_menu->addAction(_("Convert"));
  act->setShortcut(QKeySequence(tr("shift+o")));
  menu_list.insert(CONVERT, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_convert);
  act = main_menu->addAction(_("Disband"));
  act->setShortcut(QKeySequence(tr("shift+d")));
  menu_list.insert(DISBAND, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_disband);

  // Combat Menu
  main_menu = this->addMenu(_("Combat"));
  act = main_menu->addAction(_("Fortify Unit"));
  menu_list.insert(FORTIFY, act);
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_FORTIFY))));
  connect(act, &QAction::triggered, this, &mr_menu::slot_unit_fortify);
  act = main_menu->addAction(QString(Q_(terrain_control.gui_type_base0))
                             .replace("&", "&&"));
  menu_list.insert(FORTRESS, act);
  act->setShortcut(QKeySequence(tr("shift+f")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_unit_fortress);
  act = main_menu->addAction(QString(Q_(terrain_control.gui_type_base1))
                             .replace("&", "&&"));
  menu_list.insert(AIRBASE, act);
  act->setShortcut(QKeySequence(tr("shift+e")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_unit_airbase);
  bases_menu = main_menu->addMenu(_("Build Base"));
  main_menu->addSeparator();
  act = main_menu->addAction(_("Paradrop"));
  menu_list.insert(PARADROP, act);
  act->setShortcut(QKeySequence(shortcut_to_string(
                  fc_shortcuts::sc()->get_shortcut(SC_PARADROP))));
  connect(act, &QAction::triggered, this, &mr_menu::slot_paradrop);
  act = main_menu->addAction(_("Pillage"));
  menu_list.insert(PILLAGE, act);
  act->setShortcut(QKeySequence(tr("shift+p")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_pillage);
  // TRANS: Menu item to bring up the action selection dialog.
  act = main_menu->addAction(_("Do..."));
  menu_list.insert(ORDER_DIPLOMAT_DLG, act);
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_DO))));
  connect(act, &QAction::triggered, this, &mr_menu::slot_action);

  // Work Menu
  main_menu = this->addMenu(_("Work"));
  act = main_menu->addAction(QString(action_id_name_translation(ACTION_FOUND_CITY))
                             .replace("&", "&&"));
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_BUILDCITY))));
  menu_list.insert(BUILD, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_build_city);
  act = main_menu->addAction(_("Auto Worker"));
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_AUTOMATE))));
  menu_list.insert(AUTOWORKER, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_auto_worker);
  main_menu->addSeparator();
  act = main_menu->addAction(_("Build Road"));
  menu_list.insert(ROAD, act);
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_BUILDROAD))));
  connect(act, &QAction::triggered, this, &mr_menu::slot_build_road);
  roads_menu = main_menu->addMenu(_("Build Path"));
  act = main_menu->addAction(_("Build Irrigation"));
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_BUILDIRRIGATION))));
  menu_list.insert(IRRIGATION, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_build_irrigation);
  act = main_menu->addAction(_("Cultivate"));
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_CULTIVATE))));
  menu_list.insert(CULTIVATE, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_cultivate);
  act = main_menu->addAction(_("Build Mine"));
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_BUILDMINE))));
  menu_list.insert(MINE, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_build_mine);
  act = main_menu->addAction(_("Plant"));
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_PLANT))));
  menu_list.insert(PLANT, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_plant);
  main_menu->addSeparator();
  act = main_menu->addAction(_("Connect With Road"));
  act->setShortcut(QKeySequence(tr("ctrl+r")));
  menu_list.insert(CONNECT_ROAD, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_conn_road);
  act = main_menu->addAction(_("Connect With Railroad"));
  menu_list.insert(CONNECT_RAIL, act);
  act->setShortcut(QKeySequence(tr("ctrl+l")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_conn_rail);
  act = main_menu->addAction(_("Connect With Maglev"));
  menu_list.insert(CONNECT_MAGLEV, act);
  act->setShortcut(QKeySequence(tr("ctrl+m")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_conn_maglev);
  act = main_menu->addAction(_("Connect With Irrigation"));
  menu_list.insert(CONNECT_IRRIGATION, act);
  act->setShortcut(QKeySequence(tr("ctrl+i")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_conn_irrigation);
  main_menu->addSeparator();
  act = main_menu->addAction(_("Transform Terrain"));
  menu_list.insert(TRANSFORM, act);
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_TRANSFORM))));
  connect(act, &QAction::triggered, this, &mr_menu::slot_transform);
  act = main_menu->addAction(_("Clean"));
  menu_list.insert(CLEAN, act);
  act->setShortcut(QKeySequence(shortcut_to_string(
                   fc_shortcuts::sc()->get_shortcut(SC_CLEAN))));
  connect(act, &QAction::triggered, this, &mr_menu::slot_clean);
  act = main_menu->addAction(QString(action_id_name_translation(ACTION_HELP_WONDER))
                             .replace("&", "&&"));
  act->setShortcut(QKeySequence(tr("b")));
  menu_list.insert(BUILD_WONDER, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_build_city);
  act = main_menu->addAction(QString(action_id_name_translation(ACTION_TRADE_ROUTE))
                             .replace("&", "&&"));
  act->setShortcut(QKeySequence(tr("r")));
  menu_list.insert(ORDER_TRADE_ROUTE, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_build_road);

  // Multiplayer Menu
  multiplayer_menu = this->addMenu(_("Multiplayer"));
  act = multiplayer_menu->addAction(_("Delayed Goto"));
  act->setShortcut(QKeySequence(tr("z")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_delayed_goto);
  act = multiplayer_menu->addAction(_("Delayed Orders Execute"));
  act->setShortcut(QKeySequence(tr("ctrl+z")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_execute_orders);
  act = multiplayer_menu->addAction(_("Clear Orders"));
  act->setShortcut(QKeySequence(tr("ctrl+shift+c")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_orders_clear);
  act = multiplayer_menu->addAction(_("Add all cities to trade planning"));
  connect(act, &QAction::triggered, this, &mr_menu::slot_trade_add_all);
  act = multiplayer_menu->addAction(_("Calculate trade planning"));
  connect(act, &QAction::triggered, this, &mr_menu::slot_calculate);
  act = multiplayer_menu->addAction(_("Add/Remove City"));
  act->setShortcut(QKeySequence(tr("ctrl+t")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_trade_city);
  act = multiplayer_menu->addAction(_("Clear Trade Planning"));
  connect(act, &QAction::triggered, this, &mr_menu::slot_clear_trade);
  act = multiplayer_menu->addAction(_("Automatic caravan"));
  menu_list.insert(AUTOTRADEROUTE, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_autocaravan);
  act->setShortcut(QKeySequence(tr("ctrl+j")));
  act = multiplayer_menu->addAction(_("Set/Unset rally point"));
  act->setShortcut(QKeySequence(tr("shift+ctrl+r")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_rally);
  act = multiplayer_menu->addAction(_("Quick Airlift"));
  act->setShortcut(QKeySequence(tr("shift+ctrl+y")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_quickairlift);
  airlift_type = new QActionGroup(this);
  airlift_menu = multiplayer_menu->addMenu(_("Unit type for quickairlifting"));

  // Default diplo
  action_vs_city = new QActionGroup(this);
  action_vs_unit = new QActionGroup(this);
  action_unit_menu = multiplayer_menu->addMenu(_("Default action vs unit"));

  act = action_unit_menu->addAction(_("Ask"));
  act->setCheckable(true);
  act->setChecked(true);
  act->setData(-1);
  action_vs_unit->addAction(act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_action_vs_unit);

  act = action_unit_menu->addAction(_("Bribe Unit"));
  act->setCheckable(true);
  act->setChecked(false);
  act->setData(ACTION_SPY_BRIBE_UNIT);
  action_vs_unit->addAction(act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_action_vs_unit);

  act = action_unit_menu->addAction(_("Sabotage"));
  act->setCheckable(true);
  act->setChecked(false);
  act->setData(ACTION_SPY_SABOTAGE_UNIT);
  action_vs_unit->addAction(act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_action_vs_unit);

  act = action_unit_menu->addAction(_("Sabotage Unit Escape"));
  act->setCheckable(true);
  act->setChecked(false);
  act->setData(ACTION_SPY_SABOTAGE_UNIT_ESC);
  action_vs_unit->addAction(act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_action_vs_unit);

  action_city_menu = multiplayer_menu->addMenu(_("Default action vs city"));
  act = action_city_menu->addAction(_("Ask"));
  act->setCheckable(true);
  act->setChecked(true);
  act->setData(-1);
  action_vs_city->addAction(act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_action_vs_city);

  act = action_city_menu->addAction(_("Investigate city"));
  act->setCheckable(true);
  act->setChecked(false);
  act->setData(ACTION_SPY_INVESTIGATE_CITY);
  action_vs_city->addAction(act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_action_vs_city);

  act = action_city_menu->addAction(_("Investigate city (spends the unit)"));
  act->setCheckable(true);
  act->setChecked(false);
  act->setData(ACTION_INV_CITY_SPEND);
  action_vs_city->addAction(act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_action_vs_city);

  act = action_city_menu->addAction(_("Establish embassy"));
  act->setCheckable(true);
  act->setChecked(false);
  act->setData(ACTION_ESTABLISH_EMBASSY);
  action_vs_city->addAction(act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_action_vs_city);

  act = action_city_menu->addAction(_("Become Ambassador"));
  act->setCheckable(true);
  act->setChecked(false);
  act->setData(ACTION_ESTABLISH_EMBASSY_STAY);
  action_vs_city->addAction(act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_action_vs_city);

  act = action_city_menu->addAction(_("Steal technology"));
  act->setCheckable(true);
  act->setChecked(false);
  act->setData(ACTION_SPY_STEAL_TECH);
  action_vs_city->addAction(act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_action_vs_city);

  act = action_city_menu->addAction(_("Steal technology and escape"));
  act->setCheckable(true);
  act->setChecked(false);
  act->setData(ACTION_SPY_STEAL_TECH_ESC);
  action_vs_city->addAction(act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_action_vs_city);

  act = action_city_menu->addAction(_("Incite a revolt"));
  act->setCheckable(true);
  act->setChecked(false);
  act->setData(ACTION_SPY_INCITE_CITY);
  action_vs_city->addAction(act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_action_vs_city);

  act = action_city_menu->addAction(_("Incite a Revolt and Escape"));
  act->setCheckable(true);
  act->setChecked(false);
  act->setData(ACTION_SPY_INCITE_CITY_ESC);
  action_vs_city->addAction(act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_action_vs_city);

  act = action_city_menu->addAction(_("Poison city"));
  act->setCheckable(true);
  act->setChecked(false);
  act->setData(ACTION_SPY_POISON);
  action_vs_city->addAction(act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_action_vs_city);

  act = action_city_menu->addAction(_("Poison City Escape"));
  act->setCheckable(true);
  act->setChecked(false);
  act->setData(ACTION_SPY_POISON_ESC);
  action_vs_city->addAction(act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_action_vs_city);

  // Civilization menu
  main_menu = this->addMenu(_("Civilization"));
  act = main_menu->addAction(_("Tax Rates..."));
  act->setShortcut(QKeySequence(tr("ctrl+t")));
  menu_list.insert(NOT_4_OBS, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_popup_tax_rates);
  main_menu->addSeparator();

  act = main_menu->addAction(_("Policies..."));
  menu_list.insert(MULTIPLIERS, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_popup_mult_rates);
  main_menu->addSeparator();

  main_menu->addMenu(new class gov_menu(this));
  main_menu->addSeparator();

  act = main_menu->addAction(Q_("?noun:View"));
  act->setShortcut(QKeySequence(tr("F1")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_show_map);

  act = main_menu->addAction(_("Units"));
  act->setShortcut(QKeySequence(tr("F2")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_show_units_report);

  act = main_menu->addAction(_("Nations"));
  act->setShortcut(QKeySequence(tr("F3")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_show_nations);

  act = main_menu->addAction(_("Cities"));
  act->setShortcut(QKeySequence(tr("F4")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_show_cities);

  act = main_menu->addAction(_("Economy"));
  act->setShortcut(QKeySequence(tr("F5")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_show_eco_report);

  act = main_menu->addAction(_("Research"));
  act->setShortcut(QKeySequence(tr("F6")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_show_research_tab);

  act = main_menu->addAction(_("Wonders of the World"));
  act->setShortcut(QKeySequence(tr("F7")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_traveler);

  act = main_menu->addAction(_("Top Cities"));
  act->setShortcut(QKeySequence(tr("F8")));
  menu_list.insert(TOP_CITIES, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_top_cities);

  act = main_menu->addAction(_("Demographics"));
  act->setShortcut(QKeySequence(tr("F11")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_demographics);

  act = main_menu->addAction(_("Spaceship"));
  act->setShortcut(QKeySequence(tr("F12")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_spaceship);

  act = main_menu->addAction(_("Achievements"));
  connect(act, &QAction::triggered, this, &mr_menu::slot_achievements);

  act = main_menu->addAction(_("Endgame report"));
  menu_list.insert(ENDGAME, act);
  connect(act, &QAction::triggered, this, &mr_menu::slot_endgame);

  // Battle Groups Menu
  main_menu = this->addMenu(_("Battle Groups"));

  act = main_menu->addAction(_("Select Battle Group 1"));
  act->setShortcut(QKeySequence(tr("Shift+F1")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_bg1select);

  act = main_menu->addAction(_("Assign Battle Group 1"));
  act->setShortcut(QKeySequence(tr("Ctrl+F1")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_bg1assign);

  act = main_menu->addAction(_("Append to Battle Group 1"));
  act->setShortcut(QKeySequence(tr("Ctrl+Shift+F1")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_bg1append);

  act = main_menu->addAction(_("Select Battle Group 2"));
  act->setShortcut(QKeySequence(tr("Shift+F2")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_bg2select);

  act = main_menu->addAction(_("Assign Battle Group 2"));
  act->setShortcut(QKeySequence(tr("Ctrl+F2")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_bg2assign);

  act = main_menu->addAction(_("Append to Battle Group 2"));
  act->setShortcut(QKeySequence(tr("Ctrl+Shift+F2")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_bg2append);

  act = main_menu->addAction(_("Select Battle Group 3"));
  act->setShortcut(QKeySequence(tr("Shift+F3")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_bg3select);

  act = main_menu->addAction(_("Assign Battle Group 3"));
  act->setShortcut(QKeySequence(tr("Ctrl+F3")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_bg3assign);

  act = main_menu->addAction(_("Append to Battle Group 3"));
  act->setShortcut(QKeySequence(tr("Ctrl+Shift+F3")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_bg3append);

  act = main_menu->addAction(_("Select Battle Group 4"));
  act->setShortcut(QKeySequence(tr("Shift+F4")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_bg4select);

  act = main_menu->addAction(_("Assign Battle Group 4"));
  act->setShortcut(QKeySequence(tr("Ctrl+F4")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_bg4assign);

  act = main_menu->addAction(_("Append to Battle Group 4"));
  act->setShortcut(QKeySequence(tr("Ctrl+Shift+F4")));
  connect(act, &QAction::triggered, this, &mr_menu::slot_bg4append);

  // Help Menu
  main_menu = this->addMenu(_("Help"));

  act = main_menu->addAction(Q_(HELP_OVERVIEW_ITEM));
  QObject::connect(act, &QAction::triggered, [this]() {
    slot_help(HELP_OVERVIEW_ITEM);
  });

  act = main_menu->addAction(Q_(HELP_PLAYING_ITEM));
  QObject::connect(act, &QAction::triggered, [this]() {
    slot_help(HELP_PLAYING_ITEM);
  });

  act = main_menu->addAction(Q_(HELP_TERRAIN_ITEM));
  QObject::connect(act, &QAction::triggered, [this]() {
    slot_help(HELP_TERRAIN_ITEM);
  });

  act = main_menu->addAction(Q_(HELP_ECONOMY_ITEM));
  QObject::connect(act, &QAction::triggered, [this]() {
    slot_help(HELP_ECONOMY_ITEM);
  });

  act = main_menu->addAction(Q_(HELP_CITIES_ITEM));
  QObject::connect(act, &QAction::triggered, [this]() {
    slot_help(HELP_CITIES_ITEM);
  });

  act = main_menu->addAction(Q_(HELP_IMPROVEMENTS_ITEM));
  QObject::connect(act, &QAction::triggered, [this]() {
    slot_help(HELP_IMPROVEMENTS_ITEM);
  });

  act = main_menu->addAction(Q_(HELP_WONDERS_ITEM));
  QObject::connect(act, &QAction::triggered, [this]() {
    slot_help(HELP_WONDERS_ITEM);
  });

  act = main_menu->addAction(Q_(HELP_UNITS_ITEM));
  QObject::connect(act, &QAction::triggered, [this]() {
    slot_help(HELP_UNITS_ITEM);
  });

  act = main_menu->addAction(Q_(HELP_COMBAT_ITEM));
  QObject::connect(act, &QAction::triggered, [this]() {
    slot_help(HELP_COMBAT_ITEM);
  });

  act = main_menu->addAction(Q_(HELP_ZOC_ITEM));
  QObject::connect(act, &QAction::triggered, [this]() {
    slot_help(HELP_ZOC_ITEM);
  });

  act = main_menu->addAction(Q_(HELP_GOVERNMENT_ITEM));
  QObject::connect(act, &QAction::triggered, [this]() {
    slot_help(HELP_GOVERNMENT_ITEM);
  });

  act = main_menu->addAction(Q_(HELP_MULTIPLIER_ITEM));
  QObject::connect(act, &QAction::triggered, [this]() {
    slot_help(HELP_MULTIPLIER_ITEM);
  });

  act = main_menu->addAction(Q_(HELP_DIPLOMACY_ITEM));
  QObject::connect(act, &QAction::triggered, [this]() {
    slot_help(HELP_DIPLOMACY_ITEM);
  });

  act = main_menu->addAction(Q_(HELP_TECHS_ITEM));
  QObject::connect(act, &QAction::triggered, [this]() {
    slot_help(HELP_TECHS_ITEM);
  });

  act = main_menu->addAction(Q_(HELP_SPACE_RACE_ITEM));
  QObject::connect(act, &QAction::triggered, [this]() {
    slot_help(HELP_SPACE_RACE_ITEM);
  });

  act = main_menu->addAction(Q_(HELP_RULESET_ITEM));
#ifdef __APPLE__
  // only needed on Mac, prevents qt from moving the menu item
  act->setMenuRole(QAction::NoRole);
#endif // __APPLE__
  QObject::connect(act, &QAction::triggered, [this]() {
    slot_help(HELP_RULESET_ITEM);
  });

  act = main_menu->addAction(Q_(HELP_TILESET_ITEM));
#ifdef __APPLE__
  // only needed on Mac, prevents qt from moving the menu item
  act->setMenuRole(QAction::NoRole);
#endif // __APPLE__
  QObject::connect(act, &QAction::triggered, [this]() {
    slot_help(HELP_TILESET_ITEM);
  });

  act = main_menu->addAction(Q_(HELP_MUSICSET_ITEM));
#ifdef __APPLE__
  // only needed on Mac, prevents qt from moving the menu item
  act->setMenuRole(QAction::NoRole);
#endif // __APPLE__
  QObject::connect(act, &QAction::triggered, [this]() {
    slot_help(HELP_MUSICSET_ITEM);
  });

  act = main_menu->addAction(Q_(HELP_NATIONS_ITEM));
#ifdef __APPLE__
  // only needed on Mac, prevents qt from moving the menu item
  act->setMenuRole(QAction::NoRole);
#endif // __APPLE__
  QObject::connect(act, &QAction::triggered, [this]() {
    slot_help(HELP_NATIONS_ITEM);
  });

  main_menu->addSeparator();

  act = main_menu->addAction(Q_(HELP_CONNECTING_ITEM));
  QObject::connect(act, &QAction::triggered, [this]() {
    slot_help(HELP_CONNECTING_ITEM);
  });

  act = main_menu->addAction(Q_(HELP_CONTROLS_ITEM));
  QObject::connect(act, &QAction::triggered, [this]() {
    slot_help(HELP_CONTROLS_ITEM);
  });

  act = main_menu->addAction(Q_(HELP_CMA_ITEM));
  QObject::connect(act, &QAction::triggered, [this]() {
    slot_help(HELP_CMA_ITEM);
  });

  act = main_menu->addAction(Q_(HELP_CHATLINE_ITEM));
  QObject::connect(act, &QAction::triggered, [this]() {
    slot_help(HELP_CHATLINE_ITEM);
  });

  act = main_menu->addAction(Q_(HELP_WORKLIST_EDITOR_ITEM));
  QObject::connect(act, &QAction::triggered, [this]() {
    slot_help(HELP_WORKLIST_EDITOR_ITEM);
  });

  main_menu->addSeparator();

  act = main_menu->addAction(Q_(HELP_LANGUAGES_ITEM));
  QObject::connect(act, &QAction::triggered, [this]() {
    slot_help(HELP_LANGUAGES_ITEM);
  });

  act = main_menu->addAction(Q_(HELP_COPYING_ITEM));
  QObject::connect(act, &QAction::triggered, [this]() {
    slot_help(HELP_COPYING_ITEM);
  });

  act = main_menu->addAction(Q_(HELP_ABOUT_ITEM));
#ifdef __APPLE__
  // only needed on Mac, prevents qt from moving the menu item
  act->setMenuRole(QAction::NoRole);
#endif // __APPLE__
  QObject::connect(act, &QAction::triggered, [this]() {
    slot_help(HELP_ABOUT_ITEM);
  });

  menus = this->findChildren<QMenu*>();
  for (i = 0; i < menus.count(); i++) {
    menus[i]->setAttribute(Qt::WA_TranslucentBackground);
  }

  this->setVisible(false);
}

/**********************************************************************//**
  Sets given tile for delayed order
**************************************************************************/
void mr_menu::set_tile_for_order(tile *ptile)
{
  for (int i = 0; i < units_list.nr_units; i++) {
    units_list.unit_list.at(units_list.unit_list.count() - i -1)->ptile = ptile;
  }
}

/**********************************************************************//**
  Inner foreach() loop of mr_menu::execute_shortcut()
  Implemented as separate function to avoid shadow warnings about
  internal variables of foreach() inside foreach()
**************************************************************************/
bool mr_menu::execute_shortcut_inner(const QMenu *m, QKeySequence seq)
{
  foreach (QAction *a, m->actions()) {
    if (a->shortcut() == seq && a->isEnabled()) {
      a->activate(QAction::Trigger);
      return TRUE;
    }
  }

  return FALSE;
}

/**********************************************************************//**
  Finds QAction bounded to given shortcut and triggers it
**************************************************************************/
void mr_menu::execute_shortcut(int sid)
{
  QList<QMenu*> menus;
  QKeySequence seq;
  fc_shortcut *fcs;

  if (sid == SC_GOTO) {
    gui()->mapview_wdg->menu_click = true;
    slot_unit_goto();
    return;
  }
  fcs = fc_shortcuts::sc()->get_shortcut(static_cast<shortcut_id>(sid));
  seq = QKeySequence(shortcut_to_string(fcs));

  menus = findChildren<QMenu*>();
  foreach (const QMenu *m, menus) {
    if (execute_shortcut_inner(m, seq)) {
      return;
    }
  }
}

/**********************************************************************//**
  Inner foreach() loop of mr_menu::shortcut_exist()
  Implemented as separate function to avoid shadow warnings about
  internal variables of foreach() inside foreach()
**************************************************************************/
bool mr_menu::shortcut_exist_inner(const QMenu *m, QKeySequence seq,
                                   fc_shortcut *fcs, QString *ret)
{
  foreach (QAction *a, m->actions()) {
    if (a->shortcut() == seq && fcs->mouse == Qt::AllButtons) {
      *ret = a->text();

      return TRUE;
    }
  }

  return FALSE;
}

/**********************************************************************//**
  Returns string assigned to shortcut or empty string if doesn't exist
**************************************************************************/
QString mr_menu::shortcut_exist(fc_shortcut *fcs)
{
  QList<QMenu*> menus;
  QKeySequence seq;

  seq = QKeySequence(shortcut_to_string(fcs));
  menus = findChildren<QMenu *>();
  foreach (const QMenu *m, menus) {
    QString ret;

    if (shortcut_exist_inner(m, seq, fcs, &ret)) {
      return ret;
    }
  }

  return QString();
}

/**********************************************************************//**
  Inner foreach() loop of mr_menu::shortcut_2_menustring()
  Implemented as separate function to avoid shadow warnings about
  internal variables of foreach() inside foreach()
**************************************************************************/
bool mr_menu::shortcut_2_menustring_inner(const QMenu *m, QKeySequence seq,
                                          QString *ret)
{
  foreach (QAction *a, m->actions()) {
    if (a->shortcut() == seq) {
      *ret = a->text() + " ("
        + a->shortcut().toString(QKeySequence::NativeText) + ")";

      return TRUE;
    }
  }

  return FALSE;
}

/**********************************************************************//**
  Returns string bounded to given shortcut
**************************************************************************/
QString mr_menu::shortcut_2_menustring(int sid)
{
  QList<QMenu *> menus;
  QKeySequence seq;
  fc_shortcut *fcs;

  fcs = fc_shortcuts::sc()->get_shortcut(static_cast<shortcut_id>(sid));
  seq = QKeySequence(shortcut_to_string(fcs));

  menus = findChildren<QMenu *>();
  foreach (const QMenu *m, menus) {
    QString ret;

    if (shortcut_2_menustring_inner(m, seq, &ret)) {
      return ret;
    }
  }

  return QString();
}

/**********************************************************************//**
  Updates airlift menu
**************************************************************************/
void mr_menu::update_airlift_menu()
{
  Unit_type_id utype_id;
  QAction *act;
  struct player *pplayer;

  airlift_menu->clear();
  if (client_is_observer()) {
    return;
  }

  pplayer = client_player();
  unit_type_iterate(utype) {
    utype_id = utype_index(utype);

    if (!can_player_build_unit_now(pplayer, utype)
        || !utype_can_do_action(utype, ACTION_AIRLIFT)) {
      continue;
    }
    if (!can_player_build_unit_now(pplayer, utype)
        && !has_player_unit_type(utype_id)) {
      continue;
    }
    // Defeat keyboard shortcut mnemonics
    act = airlift_menu->addAction(QString(utype_name_translation(utype))
                                  .replace("&", "&&"));
    act->setCheckable(true);
    act->setData(utype_id);
    if (airlift_type_id == utype_id) {
      act->setChecked(true);
    }
    connect(act, &QAction::triggered, this, &mr_menu::slot_quickairlift_set);
    airlift_type->addAction(act);
  } unit_type_iterate_end;
}

/****************************************************************************
  Updates "build path" menu
****************************************************************************/
void mr_menu::update_roads_menu()
{
  QAction *act;
  struct unit_list *punits = nullptr;
  bool enabled = false;

  foreach(act, roads_menu->actions()) {
    removeAction(act);
    act->deleteLater();
  }
  roads_menu->clear();
  roads_menu->setDisabled(true);
  if (client_is_observer()) {
    return;
  }

  punits = get_units_in_focus();
  extra_type_by_cause_iterate(EC_ROAD, pextra) {
    if (pextra->buildable) {
      int road_id;

      // Defeat keyboard shortcut mnemonics
      act = roads_menu->addAction(QString(extra_name_translation(pextra))
                                  .replace("&", "&&"));
      road_id = pextra->id;
      act->setData(road_id);
      QObject::connect(act, &QAction::triggered, [this,road_id]() {
        slot_build_path(road_id);
      });
      if (can_units_do_activity_targeted_client(punits,
        ACTIVITY_GEN_ROAD, pextra)) {
        act->setEnabled(true);
        enabled = true;
      } else {
        act->setDisabled(true);
      }
    }
  } extra_type_by_cause_iterate_end;

  if (enabled) {
    roads_menu->setEnabled(true);
  }
}

/****************************************************************************
  Updates "build bases" menu
****************************************************************************/
void mr_menu::update_bases_menu()
{
  QAction *act;
  struct unit_list *punits = nullptr;
  bool enabled = false;

  foreach(act, bases_menu->actions()) {
    removeAction(act);
    act->deleteLater();
  }
  bases_menu->clear();
  bases_menu->setDisabled(true);

  if (client_is_observer()) {
    return;
  }

  punits = get_units_in_focus();
  extra_type_by_cause_iterate(EC_BASE, pextra) {
    if (pextra->buildable) {
      int base_id;

      // Defeat keyboard shortcut mnemonics
      act = bases_menu->addAction(QString(extra_name_translation(pextra))
                                  .replace("&", "&&"));
      base_id = pextra->id;
      act->setData(base_id);
      QObject::connect(act, &QAction::triggered, [this,base_id]() {
        slot_build_base(base_id);
      });
      if (can_units_do_activity_targeted_client(punits, ACTIVITY_BASE, pextra)) {
        act->setEnabled(true);
        enabled = true;
      } else {
        act->setDisabled(true);
      }
    }
  } extra_type_by_cause_iterate_end;

  if (enabled) {
    bases_menu->setEnabled(true);
  }
}

/**********************************************************************//**
  Enables/disables menu items and renames them depending on key in menu_list
**************************************************************************/
void mr_menu::menus_sensitive()
{
  QList <QAction * >values;
  QList <munit > keys;
  QMultiHash <munit, QAction *>::iterator i;
  struct unit_list *punits = nullptr;
  struct road_type *proad;
  struct extra_type *tgt;
  bool any_cities = false;
  bool city_on_tile = false;
  bool units_all_same_tile = true;
  const struct tile *ptile = nullptr;
  const struct unit_type *ptype = nullptr;

  players_iterate(pplayer) {
    if (city_list_size(pplayer->cities)) {
      any_cities = true;
      break;
    }
  } players_iterate_end;

  // Disable first all sensitive menus
  foreach(QAction *a, menu_list) {
    a->setEnabled(false);
  }

  if (client_is_observer()) {
    multiplayer_menu->setDisabled(true);
  } else {
    multiplayer_menu->setDisabled(false);
  }

  // Non unit menus
  keys = menu_list.keys();
  foreach (munit key, keys) {
    i = menu_list.find(key);
    while (i != menu_list.end() && i.key() == key) {
      switch (key) {
      case SAVE:
        if (can_client_access_hack() && C_S_RUNNING <= client_state()) {
          i.value()->setEnabled(true);
        }
        break;
      case NOT_4_OBS:
        if (!client_is_observer()) {
          i.value()->setEnabled(true);
        }
        break;
      case MULTIPLIERS:
        if (!client_is_observer() && multiplier_count() > 0) {
          i.value()->setEnabled(true);
          i.value()->setVisible(true);
        } else {
          i.value()->setVisible(false);
        }
        break;
      case ENDGAME:
        if (gui()->is_repo_dlg_open("END")) {
          i.value()->setEnabled(true);
          i.value()->setVisible(true);
        } else {
          i.value()->setVisible(false);
        }
        break;
      case TOP_CITIES:
        i.value()->setEnabled(game.info.top_cities_count > 0);
        if (game.info.top_cities_count > 0) {
          i.value()->setText(QString(PL_("Top %1 City", "Top %1 Cities",
                                         game.info.top_cities_count))
                             .arg(game.info.top_cities_count));
        } else {
          i.value()->setText(QString(_("Top Cities")));
        }
        break;
      default:
        break;
      }
      i++;
    }
  }

  if (!can_client_issue_orders() || get_num_units_in_focus() == 0) {
    return;
  }

  punits = get_units_in_focus();
  unit_list_iterate(punits, punit) {
    if (tile_city(unit_tile(punit))) {
      city_on_tile = true;
      break;
    }
  } unit_list_iterate_end;

  unit_list_iterate(punits, punit) {
    const struct unit_type *ntype;

    fc_assert((ptile == nullptr) == (ptype == nullptr));

    ntype = unit_type_get(punit);

    // 'ntype == ptype' is correct check even when ptype is still nullptr
    if (ptile != nullptr && ntype == ptype && unit_tile(punit) != ptile) {
      units_all_same_tile = false;
    }

    if (ptype == nullptr || ntype == ptype) {
      ptile = unit_tile(punit);
      ptype = ntype;
    }
  } unit_list_iterate_end;

  keys = menu_list.keys();
  foreach(munit key, keys) {
    i = menu_list.find(key);
    while (i != menu_list.end() && i.key() == key) {
      switch (key) {
      case STANDARD:
        i.value()->setEnabled(true);
        break;

      case EXPLORE:
        if (can_units_do_activity_client(punits, ACTIVITY_EXPLORE)) {
          i.value()->setEnabled(true);
        }
        break;

      case BOARD:
        if (units_can_load(punits)) {
          i.value()->setEnabled(true);
        }
        break;

      case DEBOARD:
        if (units_can_unload(&(wld.map), punits)) {
          i.value()->setEnabled(true);
        }
        break;

      case TRANSPORTER:
        if (units_are_occupied(punits)) {
          i.value()->setEnabled(true);
        }
        break;

      case CONVERT:
        if (units_can_convert(&(wld.map), punits)) {
          i.value()->setEnabled(true);
        }
        break;

      case MINE:
        if (can_units_do_activity_client(punits, ACTIVITY_MINE)) {
          i.value()->setEnabled(true);
        }

        if (units_all_same_tile) {
          if (units_have_type_flag(punits, UTYF_WORKERS, TRUE)) {
            struct extra_type *pextra = nullptr;

            /* FIXME: this overloading doesn't work well with multiple focus
             * units. */
            unit_list_iterate(punits, builder) {
              pextra = next_extra_for_tile(unit_tile(builder), EC_MINE,
                                           unit_owner(builder), builder);
              if (pextra != nullptr) {
                break;
              }
            } unit_list_iterate_end;

            if (pextra != nullptr) {
              i.value()->setText(
                // TRANS: Build mine of specific type
                QString(_("Build %1"))
                .arg(extra_name_translation(pextra))
                .replace("&", "&&"));
            } else {
              i.value()->setText(QString(_("Build Mine")));
            }
          } else {
            i.value()->setText(QString(_("Build Mine")));
          }
        }
        break;

      case IRRIGATION:
        if (can_units_do_activity_client(punits, ACTIVITY_IRRIGATE)) {
          i.value()->setEnabled(true);
        }
        if (units_all_same_tile) {
          if (units_have_type_flag(punits, UTYF_WORKERS, TRUE)) {
            struct extra_type *pextra = nullptr;

            /* FIXME: this overloading doesn't work well with multiple focus
             * units. */
            unit_list_iterate(punits, builder) {
              pextra = next_extra_for_tile(unit_tile(builder), EC_IRRIGATION,
                                           unit_owner(builder), builder);
              if (pextra != nullptr) {
                break;
              }
            } unit_list_iterate_end;

            if (pextra != nullptr) {
              i.value()->setText(
                // TRANS: Build irrigation of specific type
                QString(_("Build %1"))
                .arg(extra_name_translation(pextra))
                .replace("&", "&&"));
            } else {
              i.value()->setText(QString(_("Build Irrigation")));
            }
          } else {
            i.value()->setText(QString(_("Build Irrigation")));
          }
        }
        break;

      case CULTIVATE:
        if (can_units_do_activity_client(punits, ACTIVITY_CULTIVATE)) {
          i.value()->setEnabled(true);
        }
        if (units_all_same_tile) {
          struct unit *punit = unit_list_get(punits, 0);
          struct tile *atile = unit_tile(punit);
          struct terrain *pterrain = tile_terrain(atile);

          if (pterrain->cultivate_result != T_NONE) {
            i.value()->setText(
              // TRANS: Transform terrain to specific type
              QString(_("Cultivate to %1"))
              .arg(QString(get_tile_change_menu_text(atile, ACTIVITY_CULTIVATE)))
              .replace("&", "&&"));
          } else {
            i.value()->setText(QString(_("Cultivate")));
          }
        } else {
          i.value()->setText(QString(_("Cultivate")));
        }
        break;

      case PLANT:
        if (can_units_do_activity_client(punits, ACTIVITY_PLANT)) {
          i.value()->setEnabled(true);
        }
        if (units_all_same_tile) {
          struct unit *punit = unit_list_get(punits, 0);
          struct tile *atile = unit_tile(punit);
          struct terrain *pterrain = tile_terrain(atile);

          if (pterrain->plant_result != T_NONE) {
            i.value()->setText(
              // TRANS: Transform terrain to specific type
              QString(_("Plant to %1"))
              .arg(QString(get_tile_change_menu_text(atile, ACTIVITY_PLANT)))
              .replace("&", "&&"));
          } else {
            i.value()->setText(QString(_("Plant")));
          }
        } else {
          i.value()->setText(QString(_("Plant")));
        }
        break;

      case TRANSFORM:
        if (can_units_do_activity_client(punits, ACTIVITY_TRANSFORM)) {
          i.value()->setEnabled(true);
        } else {
          break;
        }
        if (units_all_same_tile) {
          struct unit *punit = unit_list_get(punits, 0);
          struct tile *atile = unit_tile(punit);
          struct terrain *pterrain = tile_terrain(atile);

          if (pterrain->transform_result != T_NONE
              && pterrain->transform_result != pterrain) {
            i.value()->setText(
              // TRANS: Transform terrain to specific type
              QString(_("Transform to %1"))
              .arg(QString(get_tile_change_menu_text(atile, ACTIVITY_TRANSFORM)))
              .replace("&", "&&"));
          } else {
            i.value()->setText(_("Transform Terrain"));
          }
        }
        break;

      case BUILD:
        if (can_units_do_on_map(&(wld.map), punits,
                                unit_can_add_or_build_city)) {
          i.value()->setEnabled(true);
        }
        if (city_on_tile
            && units_can_do_action(punits, ACTION_JOIN_CITY, true)) {
          i.value()->setText(
            QString(action_id_name_translation(ACTION_JOIN_CITY))
            .replace("&", "&&"));
        } else {
          i.value()->setText(
            QString(action_id_name_translation(ACTION_FOUND_CITY))
            .replace("&", "&&"));
        }
        break;

      case ROAD:
        {
          struct extra_type *pextra = nullptr;

          if (can_units_do_any_road(&(wld.map), punits)) {
            i.value()->setEnabled(true);
          }
          unit_list_iterate(punits, punit) {
            pextra = next_extra_for_tile(unit_tile(punit), EC_ROAD,
                                        unit_owner(punit), punit);
            if (pextra != nullptr) {
              break;
            }
          } unit_list_iterate_end;

          if (pextra != nullptr) {
            i.value()->setText(
              // TRANS: Build road of specific type
              QString(_("Build %1"))
              .arg(extra_name_translation(pextra))
              .replace("&", "&&"));
          }
        }
        break;

      case FORTIFY:
        if (can_units_do_activity_client(punits, ACTIVITY_FORTIFYING)) {
          i.value()->setEnabled(true);
        }
        break;

      case FORTRESS:
        if (can_units_do_base_gui(punits, BASE_GUI_FORTRESS)) {
          i.value()->setEnabled(true);
        }
        break;

      case AIRBASE:
        if (can_units_do_base_gui(punits, BASE_GUI_AIRBASE)) {
          i.value()->setEnabled(true);
        }
        break;

      case CLEAN:
        if (can_units_do_activity_client(punits, ACTIVITY_CLEAN)) {
          i.value()->setEnabled(true);
        }
        break;

      case SENTRY:
        if (can_units_do_activity_client(punits, ACTIVITY_SENTRY)) {
          i.value()->setEnabled(true);
        }
        break;

      case PARADROP:
        if (can_units_do_on_map(&(wld.map), punits, can_unit_paradrop)) {
          i.value()->setEnabled(true);
          i.value()->setText(QString(action_id_name_translation(ACTION_PARADROP))
                             .replace("&", "&&"));
        }
        break;

      case PILLAGE:
        if (can_units_do_activity_client(punits, ACTIVITY_PILLAGE)) {
          i.value()->setEnabled(true);
        }
        break;

      case HOMECITY:
        if (can_units_do_on_map(&(wld.map), punits, can_unit_change_homecity)) {
          i.value()->setEnabled(true);
        }
        break;

      case WAKEUP:
        if (units_have_activity_on_tile(punits, ACTIVITY_SENTRY)) {
          i.value()->setEnabled(true);
        }
        break;

      case AUTOWORKER:
        if (can_units_do(punits, can_unit_do_autoworker)) {
          i.value()->setEnabled(true);
        }
        if (units_contain_cityfounder(punits)) {
          i.value()->setText(_("Auto Settler"));
        } else {
          i.value()->setText(_("Auto Worker"));
        }
        break;
      case CONNECT_ROAD:
        proad = road_by_gui_type(ROAD_GUI_ROAD);
        if (proad != nullptr) {
          tgt = road_extra_get(proad);
        } else {
          break;
        }
        if (can_units_do_connect(punits, ACTIVITY_GEN_ROAD, tgt)) {
          i.value()->setEnabled(true);
        }
        break;

      case DISBAND:
        if (units_can_do_action(punits, ACTION_DISBAND_UNIT, true)) {
          i.value()->setEnabled(true);
        }
        break;

      case CONNECT_RAIL:
        proad = road_by_gui_type(ROAD_GUI_RAILROAD);
        if (proad != nullptr) {
          tgt = road_extra_get(proad);
        } else {
          break;
        }
        if (can_units_do_connect(punits, ACTIVITY_GEN_ROAD, tgt)) {
          i.value()->setEnabled(true);
        }
        break;

      case CONNECT_MAGLEV:
        proad = road_by_gui_type(ROAD_GUI_MAGLEV);
        if (proad != nullptr) {
          tgt = road_extra_get(proad);
        } else {
          break;
        }
        if (can_units_do_connect(punits, ACTIVITY_GEN_ROAD, tgt)) {
          i.value()->setEnabled(true);
        }
        break;

      case CONNECT_IRRIGATION:
        {
          struct extra_type_list *extras = extra_type_list_by_cause(EC_IRRIGATION);

          if (extra_type_list_size(extras) > 0) {
            struct extra_type *pextra;

            pextra = extra_type_list_get(extra_type_list_by_cause(EC_IRRIGATION), 0);
            if (can_units_do_connect(punits, ACTIVITY_IRRIGATE, pextra)) {
              i.value()->setEnabled(true);
            }
          }
        }
        break;

      case GOTO_CITY:
        if (any_cities) {
          i.value()->setEnabled(true);
        }
        break;

      case AIRLIFT:
        if (any_cities) {
          i.value()->setEnabled(true);
        }
        break;

      case BUILD_WONDER:
        i.value()->setText(
          QString(action_id_name_translation(ACTION_HELP_WONDER))
          .replace("&", "&&"));
        if (can_units_do_on_map(&(wld.map), punits, unit_can_help_build_wonder_here)) {
          i.value()->setEnabled(true);
        }
        break;

      case AUTOTRADEROUTE:
        if (units_can_do_action(punits, ACTION_TRADE_ROUTE, TRUE)) {
          i.value()->setEnabled(true);
        }
        break;

      case ORDER_TRADE_ROUTE:
        i.value()->setText(
          QString(action_id_name_translation(ACTION_TRADE_ROUTE))
          .replace("&", "&&"));
        if (can_units_do(punits, unit_can_est_trade_route_here)) {
          i.value()->setEnabled(true);
        }
        break;

      case ORDER_DIPLOMAT_DLG:
        if (units_can_do_action(punits, ACTION_ANY, TRUE)) {
          i.value()->setEnabled(true);
        }
        break;

      case UPGRADE:
        if (units_can_upgrade(&(wld.map), punits)) {
          i.value()->setEnabled(true);
        }
        break;
      default:
        break;
      }

      i++;
    }
  }
}

/**********************************************************************//**
  Slot for showing research tab
**************************************************************************/
void mr_menu::slot_show_research_tab()
{
  qtg_science_report_dialog_popup(true);
}

/**********************************************************************//**
  Slot for showing spaceship
**************************************************************************/
void mr_menu::slot_spaceship()
{
  struct player *pplayer = client_player();

  if (pplayer != nullptr) {
    popup_spaceship_dialog(pplayer);
  }
}

/**********************************************************************//**
  Slot for showing economy tab
**************************************************************************/
void mr_menu::slot_show_eco_report()
{
  economy_report_dialog_popup(false);
}

/**********************************************************************//**
  Changes tab to mapview
**************************************************************************/
void mr_menu::slot_show_map()
{
  ::gui()->game_tab_widget->setCurrentIndex(0);
}

/**********************************************************************//**
  Slot for showing units tab
**************************************************************************/
void mr_menu::slot_show_units_report()
{
  toggle_units_report(true);
}

/**********************************************************************//**
  Slot for showing nations report
**************************************************************************/
void mr_menu::slot_show_nations()
{
  popup_players_dialog(false);
}

/**********************************************************************//**
  Slot for showing cities report
**************************************************************************/
void mr_menu::slot_show_cities()
{
  city_report_dialog_popup(false);
}

/**********************************************************************//**
  Action "BUILD_CITY"
**************************************************************************/
void mr_menu::slot_build_city()
{
  unit_list_iterate(get_units_in_focus(), punit) {
    /* FIXME: this can provide different actions for different units...
     * not good! */
    /* Enable the button for adding to a city in all cases, so we
       get an eventual error message from the server if we try. */
    if (unit_can_add_or_build_city(&(wld.map), punit)) {
      request_unit_build_city(punit);
    } else if (utype_can_do_action(unit_type_get(punit), ACTION_HELP_WONDER)) {
      request_unit_caravan_action(punit, ACTION_HELP_WONDER);
    }
  } unit_list_iterate_end;
}

/**********************************************************************//**
  Action "CLEAN"
**************************************************************************/
void mr_menu::slot_clean()
{
  unit_list_iterate(get_units_in_focus(), punit) {
    struct extra_type *pextra;

    pextra = prev_extra_in_tile(unit_tile(punit), ERM_CLEAN,
                                unit_owner(punit), punit);

    if (pextra != nullptr) {
      request_new_unit_activity_targeted(punit, ACTIVITY_CLEAN, pextra);
    }
  } unit_list_iterate_end;
}

/**********************************************************************//**
  Action "CONNECT WITH IRRIGATION"
**************************************************************************/
void mr_menu::slot_conn_irrigation()
{
  struct extra_type_list *extras = extra_type_list_by_cause(EC_IRRIGATION);

  if (extra_type_list_size(extras) > 0) {
    struct extra_type *pextra;

    pextra = extra_type_list_get(extra_type_list_by_cause(EC_IRRIGATION), 0);

    key_unit_connect(ACTIVITY_IRRIGATE, pextra);
  }
}

/**********************************************************************//**
  Action "CONNECT WITH RAILROAD"
**************************************************************************/
void mr_menu::slot_conn_rail()
{
  struct road_type *prail = road_by_gui_type(ROAD_GUI_RAILROAD);

  if (prail != nullptr) {
    struct extra_type *tgt;

    tgt = road_extra_get(prail);
    key_unit_connect(ACTIVITY_GEN_ROAD, tgt);
  }
}

/**********************************************************************//**
  Action "CONNECT WITH MAGLEV"
**************************************************************************/
void mr_menu::slot_conn_maglev()
{
  struct road_type *pmaglev = road_by_gui_type(ROAD_GUI_MAGLEV);

  if (pmaglev != nullptr) {
    struct extra_type *tgt;

    tgt = road_extra_get(pmaglev);
    key_unit_connect(ACTIVITY_GEN_ROAD, tgt);
  }
}

/**********************************************************************//**
  Action "BUILD FORTRESS"
**************************************************************************/
void mr_menu::slot_unit_fortress()
{
  key_unit_fortress();
}

/**********************************************************************//**
  Action "BUILD AIRBASE"
**************************************************************************/
void mr_menu::slot_unit_airbase()
{
  key_unit_airbase();
}

/**********************************************************************//**
  Action "CONNECT WITH ROAD"
**************************************************************************/
void mr_menu::slot_conn_road()
{
  struct road_type *proad = road_by_gui_type(ROAD_GUI_ROAD);

  if (proad != nullptr) {
    struct extra_type *tgt;

    tgt = road_extra_get(proad);
    key_unit_connect(ACTIVITY_GEN_ROAD, tgt);
  }
}

/**********************************************************************//**
  Action "TRANSFORM TERRAIN"
**************************************************************************/
void mr_menu::slot_transform()
{
  key_unit_transform();
}

/**********************************************************************//**
  Action "PARADROP"
**************************************************************************/
void mr_menu::slot_paradrop()
{
  unit_list_iterate(get_units_in_focus(), punit) {
    if (can_unit_paradrop(&(wld.map), punit)) {
      key_unit_paradrop();
    }
  } unit_list_iterate_end;
}

/**********************************************************************//**
  Action "PILLAGE"
**************************************************************************/
void mr_menu::slot_pillage()
{
  key_unit_pillage();
}

/**********************************************************************//**
  Do... the selected action
**************************************************************************/
void mr_menu::slot_action()
{
  key_unit_action_select_tgt();
}

/**********************************************************************//**
  Action "AUTO_WORKER"
**************************************************************************/
void mr_menu::slot_auto_worker()
{
  key_unit_auto_work();
}

/**********************************************************************//**
  Action "BUILD_ROAD"
**************************************************************************/
void mr_menu::slot_build_road()
{
  unit_list_iterate(get_units_in_focus(), punit) {
    /* FIXME: this can provide different actions for different units...
     * not good! */
    struct extra_type *tgt = next_extra_for_tile(unit_tile(punit),
                                                 EC_ROAD,
                                                 unit_owner(punit),
                                                 punit);
    bool building_road = false;

    if (tgt != nullptr
        && can_unit_do_activity_targeted_client(punit, ACTIVITY_GEN_ROAD, tgt)) {
      request_new_unit_activity_targeted(punit, ACTIVITY_GEN_ROAD, tgt);
      building_road = true;
    }

    if (!building_road && unit_can_est_trade_route_here(punit)) {
      request_unit_caravan_action(punit, ACTION_TRADE_ROUTE);
    }
  } unit_list_iterate_end;
}

/**********************************************************************//**
  Action "BUILD_IRRIGATION"
**************************************************************************/
void mr_menu::slot_build_irrigation()
{
  key_unit_irrigate();
}

/**********************************************************************//**
  Action "CULTIVATE"
**************************************************************************/
void mr_menu::slot_cultivate()
{
  key_unit_cultivate();
}

/**********************************************************************//**
  Action "BUILD_MINE"
**************************************************************************/
void mr_menu::slot_build_mine()
{
  key_unit_mine();
}

/**********************************************************************//**
  Action "PLANT"
**************************************************************************/
void mr_menu::slot_plant()
{
  key_unit_plant();
}

/**********************************************************************//**
  Action "FORTIFY"
**************************************************************************/
void mr_menu::slot_unit_fortify()
{
  key_unit_fortify();
}

/**********************************************************************//**
  Action "SENTRY"
**************************************************************************/
void mr_menu::slot_unit_sentry()
{
  key_unit_sentry();
}

/**********************************************************************//**
  Action "CONVERT"
**************************************************************************/
void mr_menu::slot_convert()
{
  key_unit_convert();
}

/**********************************************************************//**
  Action "DISBAND UNIT"
**************************************************************************/
void mr_menu::slot_disband()
{
  popup_disband_dialog(get_units_in_focus());
}

/**********************************************************************//**
  Clears delayed orders
**************************************************************************/
void mr_menu::slot_orders_clear()
{
  delayed_order = false;
  units_list.clear();
}

/**********************************************************************//**
  Sets/unset rally point
**************************************************************************/
void mr_menu::slot_rally()
{
  gui()->rallies.hover_tile = false;
  gui()->rallies.hover_city = true;
}

/**********************************************************************//**
  Adds one city to trade planning
**************************************************************************/
void mr_menu::slot_trade_city()
{
  gui()->trade_gen.hover_city = true;
}

/**********************************************************************//**
  Adds all cities to trade planning
**************************************************************************/
void mr_menu::slot_trade_add_all()
{
  gui()->trade_gen.add_all_cities();
}

/**********************************************************************//**
  Trade calculation slot
**************************************************************************/
void mr_menu::slot_calculate()
{
  gui()->trade_gen.calculate();
}

/**********************************************************************//**
  Slot for clearing trade routes
**************************************************************************/
void mr_menu::slot_clear_trade()
{
  gui()->trade_gen.clear_trade_planing();
}

/**********************************************************************//**
  Sends automatic caravan
**************************************************************************/
void mr_menu::slot_autocaravan()
{
  qtiles gilles;
  struct unit *punit;
  struct city *homecity;
  struct tile *home_tile;
  struct tile *dest_tile;
  bool sent = false;

  punit = head_of_units_in_focus();
  homecity = game_city_by_number(punit->homecity);
  home_tile = homecity->tile;
  foreach(gilles, gui()->trade_gen.lines) {
    if ((gilles.t1 == home_tile || gilles.t2 == home_tile)
         && gilles.autocaravan == nullptr) {
      // Send caravan
      if (gilles.t1 == home_tile) {
        dest_tile = gilles.t2;
      } else {
        dest_tile = gilles.t1;
      }
      if (send_goto_tile(punit, dest_tile)) {
        int i;
        i = gui()->trade_gen.lines.indexOf(gilles);
        gilles = gui()->trade_gen.lines.takeAt(i);
        gilles.autocaravan = punit;
        gui()->trade_gen.lines.append(gilles);
        sent = true;
        break;
      }
    }
  }

  if (!sent) {
    gui()->infotab->chtwdg->append(_("Didn't find any trade route"
                                     " to establish"));
  }
}

/**********************************************************************//**
  Slot for setting quick airlift
**************************************************************************/
void mr_menu::slot_quickairlift_set()
{
  QVariant v;
  QAction *act;

  act = qobject_cast<QAction *>(sender());
  v = act->data();
  airlift_type_id = v.toInt();
}

/**********************************************************************//**
  Slot for choosing default action vs unit
**************************************************************************/
void mr_menu::slot_action_vs_unit()
{
  QAction *act;

  act = qobject_cast<QAction *>(sender());
  qdef_act::action()->vs_unit_set(act->data().toInt());
}

/**********************************************************************//**
  Slot for choosing default action vs city
**************************************************************************/
void mr_menu::slot_action_vs_city()
{
  QAction *act;

  act = qobject_cast<QAction *>(sender());
  qdef_act::action()->vs_city_set(act->data().toInt());
}

/**********************************************************************//**
  Slot for quick airlifting
**************************************************************************/
void mr_menu::slot_quickairlift()
{
  quick_airlifting = true;
}

/**********************************************************************//**
  Delayed goto
**************************************************************************/
void mr_menu::slot_delayed_goto()
{
  qfc_delayed_unit_item *unit_item;
  int i = 0;
  delay_order dg;

  delayed_order = true;
  dg = D_GOTO;

  struct unit_list *punits = get_units_in_focus();
  if (unit_list_size(punits) == 0) {
    return;
  }
  if (hover_state != HOVER_GOTO) {
    set_hover_state(punits, HOVER_GOTO, ACTIVITY_LAST, nullptr,
                    NO_TARGET, NO_TARGET, ACTION_NONE, ORDER_LAST);
    enter_goto_state(punits);
    create_line_at_mouse_pos();
    control_mouse_cursor(nullptr);
  }
  unit_list_iterate(get_units_in_focus(), punit) {
    i++;
    unit_item = new qfc_delayed_unit_item(dg, punit->id);
    units_list.add(unit_item);
    units_list.nr_units = i;
  } unit_list_iterate_end;
}

/**********************************************************************//**
  Executes stored orders
**************************************************************************/
void mr_menu::slot_execute_orders()
{
  qfc_delayed_unit_item *fui;
  struct unit *punit;
  struct tile *last_tile;
  struct tile *new_tile;
  int i = 0;

  foreach (fui, units_list.unit_list) {
    i++;
    punit = unit_list_find(client_player()->units, fui->id);
    if (punit == nullptr) {
      continue;
    }
    last_tile = punit->tile;
    new_tile = find_last_unit_pos(punit, i);
    if (new_tile != nullptr) {
      punit->tile = new_tile;
    }
    if (is_tiles_adjacent(punit->tile, fui->ptile)) {
      request_move_unit_direction(punit,
                                  get_direction_for_step(&(wld.map),
                                                         punit->tile,
                                                         fui->ptile));
    } else {
      send_attack_tile(punit, fui->ptile);
    }
    punit->tile = last_tile;
  }
  units_list.clear();
}

/**********************************************************************//**
  Action "BOARD INTO TRANSPORTER"
**************************************************************************/
void mr_menu::slot_board()
{
  unit_list_iterate(get_units_in_focus(), punit) {
    qtg_request_transport(punit, unit_tile(punit));
  } unit_list_iterate_end;
}

/**********************************************************************//**
  Action "UNIT PATROL"
**************************************************************************/
void mr_menu::slot_patrol()
{
  key_unit_patrol();
}

/**********************************************************************//**
  Action "RETURN TO NEAREST CITY"
**************************************************************************/
void mr_menu::slot_return_to_city()
{
  unit_list_iterate(get_units_in_focus(), punit) {
    request_unit_return(punit);
  } unit_list_iterate_end;
}

/**********************************************************************//**
  Action "GOTO/AIRLIFT TO CITY"
**************************************************************************/
void mr_menu::slot_airlift()
{
  popup_goto_dialog();
}

/**********************************************************************//**
  Action "SET HOMECITY"
**************************************************************************/
void mr_menu::slot_set_home()
{
  key_unit_homecity();
}

/**********************************************************************//**
  Action "DEBOARD FROM TRANSPORTED"
**************************************************************************/
void mr_menu::slot_deboard()
{
  unit_list_iterate(get_units_in_focus(), punit) {
    request_unit_unload(punit);
  } unit_list_iterate_end;
}

/**********************************************************************//**
  Action "UNLOAD ALL UNITS FROM TRANSPORTER"
**************************************************************************/
void mr_menu::slot_unload_all()
{
  key_unit_unload_all();
}

/**********************************************************************//**
  Action "UNSENTRY(WAKEUP) ALL UNITS"
**************************************************************************/
void mr_menu::slot_unsentry()
{
  key_unit_wakeup_others();
}

/**********************************************************************//**
  Action "UPGRADE UNITS"
**************************************************************************/
void mr_menu::slot_upgrade()
{
  popup_upgrade_dialog(get_units_in_focus());
}

/**********************************************************************//**
  Action "GOTO"
**************************************************************************/
void mr_menu::slot_unit_goto()
{
  key_unit_goto();
}

/**********************************************************************//**
  Action "EXPLORE"
**************************************************************************/
void mr_menu::slot_unit_explore()
{
  key_unit_auto_explore();
}

/**********************************************************************//**
  Action "CENTER VIEW"
**************************************************************************/
void mr_menu::slot_center_view()
{
  request_center_focus_unit();
}

/**********************************************************************//**
  Action "Lock interface"
**************************************************************************/
void mr_menu::slot_lock()
{
  if (gui()->interface_locked) {
    enable_interface(false);
  } else {
    enable_interface(true);
  }
  gui()->interface_locked = !gui()->interface_locked;
}

/**********************************************************************//**
  Helper function to hide/show widgets
**************************************************************************/
void enable_interface(bool enable)
{
  QList<close_widget *> lc;
  QList<move_widget *> lm;
  QList<resize_widget *> lr;
  int i;

  lc = gui()->findChildren<close_widget *>();
  lm = gui()->findChildren<move_widget *>();
  lr = gui()->findChildren<resize_widget *>();

  for (i = 0; i < lc.size(); ++i) {
    lc.at(i)->setVisible(!enable);
  }
  for (i = 0; i < lm.size(); ++i) {
    lm.at(i)->setVisible(!enable);
  }
  for (i = 0; i < lr.size(); ++i) {
    lr.at(i)->setVisible(!enable);
  }
}

/**********************************************************************//**
  Action "SET FULLSCREEN"
**************************************************************************/
void mr_menu::slot_fullscreen()
{
  gui_options.gui_qt_fullscreen = !gui_options.gui_qt_fullscreen;

  gui()->apply_fullscreen();
}

/**********************************************************************//**
  Action "VIEW/HIDE MINIMAP"
**************************************************************************/
void mr_menu::slot_minimap_view()
{
  if (minimap_status->isChecked()) {
    ::gui()->minimapview_wdg->show();
  } else {
    ::gui()->minimapview_wdg->hide();
  }
}

/**********************************************************************//**
  Action "Show/Dont show new turn info"
**************************************************************************/
void mr_menu::slot_show_new_turn_text()
{
  if (osd_status->isChecked()) {
    gui()->qt_settings.show_new_turn_text = true;
  } else {
    gui()->qt_settings.show_new_turn_text = false;
  }
}

/**********************************************************************//**
  Action "Show/Dont battle log"
**************************************************************************/
void mr_menu::slot_battlelog()
{
  if (btlog_status->isChecked()) {
    gui()->qt_settings.show_battle_log = true;
  } else {
    gui()->qt_settings.show_battle_log = false;
  }
}

/**********************************************************************//**
  Action "SHOW BORDERS"
**************************************************************************/
void mr_menu::slot_borders()
{
  key_map_borders_toggle();
}

/**********************************************************************//**
  Action "SHOW NATIVE TILES"
**************************************************************************/
void mr_menu::slot_native_tiles()
{
  key_map_native_toggle();
}

/**********************************************************************//**
  Action "SHOW BUY COST"
**************************************************************************/
void mr_menu::slot_city_buycost()
{
  key_city_buycost_toggle();
}

/**********************************************************************//**
  Action "SHOW CITY GROWTH"
**************************************************************************/
void mr_menu::slot_city_growth()
{
  key_city_growth_toggle();
}

/**********************************************************************//**
  Action "RELOAD ZOOMED IN TILESET"
**************************************************************************/
void mr_menu::zoom_in()
{
  gui()->map_scale = gui()->map_scale * 1.2f;
  tilespec_reread(tileset_basename(tileset), true, gui()->map_scale);
}

/**********************************************************************//**
  Action "RESET ZOOM TO DEFAULT"
**************************************************************************/
void mr_menu::zoom_reset()
{
  QFont *qf;

  gui()->map_scale = 1.0f;
  qf = fc_font::instance()->get_font(fonts::city_names);
  qf->setPointSize(fc_font::instance()->city_fontsize);
  qf = fc_font::instance()->get_font(fonts::city_productions);
  qf->setPointSize(fc_font::instance()->prod_fontsize);
  tilespec_reread(tileset_basename(tileset), true, gui()->map_scale);
}

/**********************************************************************//**
  Action "SCALE FONTS WHEN SCALING MAP"
***************************************************************************/
void mr_menu::zoom_scale_fonts()
{
  QFont *qf;

  if (scale_fonts_status->isChecked()) {
    gui()->map_font_scale = true;
  } else {
    qf = fc_font::instance()->get_font(fonts::city_names);
    qf->setPointSize(fc_font::instance()->city_fontsize);
    qf = fc_font::instance()->get_font(fonts::city_productions);
    qf->setPointSize(fc_font::instance()->prod_fontsize);
    gui()->map_font_scale = false;
  }
  update_city_descriptions();
}

/**********************************************************************//**
  Action "RELOAD ZOOMED OUT TILESET"
**************************************************************************/
void mr_menu::zoom_out()
{
  gui()->map_scale = gui()->map_scale / 1.2f;
  tilespec_reread(tileset_basename(tileset), true, gui()->map_scale);
}

/**********************************************************************//**
  Action "SHOW CITY NAMES"
**************************************************************************/
void mr_menu::slot_city_names()
{
  key_city_names_toggle();
}

/**********************************************************************//**
  Action "SHOW CITY OUTLINES"
**************************************************************************/
void mr_menu::slot_city_outlines()
{
  key_city_outlines_toggle();
}

/**********************************************************************//**
  Action "SHOW CITY OUTPUT"
**************************************************************************/
void mr_menu::slot_city_output()
{
  key_city_output_toggle();
}

/**********************************************************************//**
  Action "SHOW CITY PRODUCTION"
**************************************************************************/
void mr_menu::slot_city_production()
{
  key_city_productions_toggle();
}

/**********************************************************************//**
  Action "SHOW CITY TRADE ROUTES"
**************************************************************************/
void mr_menu::slot_city_trade_routes()
{
  key_city_trade_routes_toggle();
}

/**********************************************************************//**
  Action "SHOW UNIT STACK SIZE"
**************************************************************************/
void mr_menu::slot_stack_size()
{
  key_unit_stack_size_toggle();
}

/**********************************************************************//**
  Action "SHOW FULLBAR"
**************************************************************************/
void mr_menu::slot_fullbar()
{
  key_city_full_bar_toggle();
}

/**********************************************************************//**
  Action "SHOW MAP GRID"
**************************************************************************/
void mr_menu::slot_map_grid()
{
  key_map_grid_toggle();
}

/**********************************************************************//**
  Action "DONE MOVING"
**************************************************************************/
void mr_menu::slot_done_moving()
{
  key_unit_done();
}

/**********************************************************************//**
  Action "SELECT ALL UNITS ON TILE"
**************************************************************************/
void mr_menu::slot_select_all_tile()
{
  request_unit_select(get_units_in_focus(), SELTYPE_ALL, SELLOC_TILE);
}

/**********************************************************************//**
  Action "SELECT ONE UNITS/DESELECT OTHERS"
**************************************************************************/
void mr_menu::slot_select_one()
{
  request_unit_select(get_units_in_focus(), SELTYPE_SINGLE, SELLOC_TILE);
}

/**********************************************************************//**
  Action "SELECT SAME UNITS ON CONTINENT"
**************************************************************************/
void mr_menu::slot_select_same_continent()
{
  if (!gui_options.unit_selection_clears_orders || confirm_disruptive_selection()) {
    request_unit_select(get_units_in_focus(), SELTYPE_SAME, SELLOC_CONT);
  }
}

/**********************************************************************//**
  Action "SELECT SAME TYPE EVERYWHERE"
**************************************************************************/
void mr_menu::slot_select_same_everywhere()
{
  if (!gui_options.unit_selection_clears_orders || confirm_disruptive_selection()) {
    request_unit_select(get_units_in_focus(), SELTYPE_SAME, SELLOC_WORLD);
  }
}

/**********************************************************************//**
  Action "SELECT SAME TYPE ON TILE"
**************************************************************************/
void mr_menu::slot_select_same_tile()
{
  request_unit_select(get_units_in_focus(), SELTYPE_SAME, SELLOC_TILE);
}

/**********************************************************************//**
  Action "WAIT"
**************************************************************************/
void mr_menu::slot_wait()
{
  key_unit_wait();
}

/**********************************************************************//**
  Shows units filter
**************************************************************************/
void mr_menu::slot_unit_filter()
{
  unit_hud_selector *uhs;

  uhs = new unit_hud_selector(gui()->central_wdg);
  uhs->show_me();
}

/**********************************************************************//**
  Action "SHOW DEMOGRAPGHICS REPORT"
**************************************************************************/
void mr_menu::slot_demographics()
{
  send_report_request(REPORT_DEMOGRAPHIC);
}

/**********************************************************************//**
  Action "SHOW ACHIEVEMENTS REPORT"
**************************************************************************/
void mr_menu::slot_achievements()
{
  send_report_request(REPORT_ACHIEVEMENTS);
}

/**********************************************************************//**
  Action "SHOW ENDGAME REPORT"
**************************************************************************/
void mr_menu::slot_endgame()
{
  popup_endgame_report();
}

/**********************************************************************//**
  Action "SHOW TOP CITIES"
**************************************************************************/
void mr_menu::slot_top_cities()
{
  send_report_request(REPORT_TOP_CITIES);
}

/**********************************************************************//**
  Action "SHOW WONDERS REPORT"
**************************************************************************/
void mr_menu::slot_traveler()
{
  send_report_request(REPORT_WONDERS_OF_THE_WORLD_LONG);
}

/**********************************************************************//**
  Select Battle Group 1
**************************************************************************/
void mr_menu::slot_bg1select()
{
  key_unit_select_battlegroup(0, FALSE);
}

/**********************************************************************//**
  Assign Battle Group 1
**************************************************************************/
void mr_menu::slot_bg1assign()
{
  key_unit_assign_battlegroup(0, FALSE);
}

/**********************************************************************//**
  Append Battle Group 1
**************************************************************************/
void mr_menu::slot_bg1append()
{
  key_unit_assign_battlegroup(0, TRUE);
}

/**********************************************************************//**
  Select Battle Group 2
**************************************************************************/
void mr_menu::slot_bg2select()
{
  key_unit_select_battlegroup(1, FALSE);
}

/**********************************************************************//**
  Assign Battle Group 2
**************************************************************************/
void mr_menu::slot_bg2assign()
{
  key_unit_assign_battlegroup(1, FALSE);
}

/**********************************************************************//**
  Append Battle Group 2
**************************************************************************/
void mr_menu::slot_bg2append()
{
  key_unit_assign_battlegroup(1, TRUE);
}

/**********************************************************************//**
  Select Battle Group 3
**************************************************************************/
void mr_menu::slot_bg3select()
{
  key_unit_select_battlegroup(2, FALSE);
}

/**********************************************************************//**
  Assign Battle Group 3
**************************************************************************/
void mr_menu::slot_bg3assign()
{
  key_unit_assign_battlegroup(2, FALSE);
}

/**********************************************************************//**
  Append Battle Group 3
**************************************************************************/
void mr_menu::slot_bg3append()
{
  key_unit_assign_battlegroup(2, TRUE);
}

/**********************************************************************//**
  Select Battle Group 4
**************************************************************************/
void mr_menu::slot_bg4select()
{
  key_unit_select_battlegroup(3, FALSE);
}

/**********************************************************************//**
  Assign Battle Group 4
**************************************************************************/
void mr_menu::slot_bg4assign()
{
  key_unit_assign_battlegroup(3, FALSE);
}

/**********************************************************************//**
  Append Battle Group 4
**************************************************************************/
void mr_menu::slot_bg4append()
{
  key_unit_assign_battlegroup(3, TRUE);
}

/**********************************************************************//**
  Shows rulesets to load
**************************************************************************/
void mr_menu::tileset_custom_load()
{
  QDialog *dialog = new QDialog(this);
  QLabel *label;
  QPushButton *but;
  QVBoxLayout *layout;
  const struct strvec *tlset_list;
  const struct option *poption;
  QStringList sl;
  QString s;

  sl << "default_tileset_overhead_name" << "default_tileset_iso_name"
     << "default_tileset_hex_name" << "default_tileset_isohex_name";
  layout = new QVBoxLayout;
  dialog->setWindowTitle(_("Available tilesets"));
  label = new QLabel;
  label->setText(_("Some tilesets might not be compatible with current"
                   " map topology!"));
  layout->addWidget(label);

  foreach (s, sl) {
    QByteArray on_bytes;

    on_bytes = s.toUtf8();
    poption = optset_option_by_name(client_optset, on_bytes.data());
    tlset_list = get_tileset_list(poption);
    strvec_iterate(tlset_list, value) {
      but = new QPushButton(value);
      connect(but, &QAbstractButton::clicked, this, &mr_menu::load_new_tileset);
      layout->addWidget(but);
    } strvec_iterate_end;
  }
  dialog->setSizeGripEnabled(true);
  dialog->setLayout(layout);
  dialog->show();
}

/**********************************************************************//**
  Slot for loading new tileset
**************************************************************************/
void mr_menu::load_new_tileset()
{
  QPushButton *but;
  QByteArray tn_bytes;

  but = qobject_cast<QPushButton *>(sender());
  tn_bytes = but->text().toUtf8();
  tilespec_reread(tn_bytes.data(), true, 1.0f);
  gui()->map_scale = 1.0f;
  but->parentWidget()->close();
}

/**********************************************************************//**
  Action "Calculate trade routes"
**************************************************************************/
void mr_menu::calc_trade_routes()
{
  gui()->trade_gen.calculate();
}

/**********************************************************************//**
  Action "TAX RATES"
**************************************************************************/
void mr_menu::slot_popup_tax_rates()
{
  popup_rates_dialog();
}

/**********************************************************************//**
  Action "MULTIPLIERS RATES"
**************************************************************************/
void mr_menu::slot_popup_mult_rates()
{
  popup_multiplier_dialog();
}

/**********************************************************************//**
  Actions "HELP_*"
**************************************************************************/
void mr_menu::slot_help(const QString &topic)
{
  popup_help_dialog_typed(Q_(topic.toStdString().c_str()), HELP_ANY);
}

/****************************************************************
  Actions "BUILD_PATH_*"
*****************************************************************/
void mr_menu::slot_build_path(int id)
{
  unit_list_iterate(get_units_in_focus(), punit) {
    extra_type_by_cause_iterate(EC_ROAD, pextra) {
      if (pextra->buildable && pextra->id == id
          && can_unit_do_activity_targeted_client(punit, ACTIVITY_GEN_ROAD,
                                                  pextra)) {
        request_new_unit_activity_targeted(punit, ACTIVITY_GEN_ROAD, pextra);
      }
    } extra_type_by_cause_iterate_end;
  } unit_list_iterate_end;
}

/****************************************************************
  Actions "BUILD_BASE_*"
*****************************************************************/
void mr_menu::slot_build_base(int id)
{
  unit_list_iterate(get_units_in_focus(), punit) {
    extra_type_by_cause_iterate(EC_BASE, pextra) {
      if (pextra->buildable && pextra->id == id
          && can_unit_do_activity_targeted_client(punit, ACTIVITY_BASE,
                                                  pextra)) {
        request_new_unit_activity_targeted(punit, ACTIVITY_BASE, pextra);
      }
    } extra_type_by_cause_iterate_end;
  } unit_list_iterate_end;
}



/**********************************************************************//**
  Invoke dialog with local options
**************************************************************************/
void mr_menu::local_options()
{
  gui()->popup_client_options();
}

/**********************************************************************//**
  Invoke dialog with shortcut options
**************************************************************************/
void mr_menu::shortcut_options()
{
  popup_shortcuts_dialog();
}

/**********************************************************************//**
  Invoke dialog with server options
**************************************************************************/
void mr_menu::server_options()
{
  gui()->pr_options->popup_server_options();
}

/**********************************************************************//**
  Invoke dialog with server options
**************************************************************************/
void mr_menu::messages_options()
{
  popup_messageopt_dialog();
}

/**********************************************************************//**
  Menu Save Options Now
**************************************************************************/
void mr_menu::save_options_now()
{
  options_save(nullptr);
}

/**********************************************************************//**
  Invoke popup for quitting game
**************************************************************************/
void mr_menu::quit_game()
{
  popup_quit_dialog();
}

/**********************************************************************//**
  Menu Save Map Image
**************************************************************************/
void mr_menu::save_image()
{
  int current_width, current_height;
  int full_size_x, full_size_y;
  QString path, storage_path;
  hud_message_box *saved = new hud_message_box(gui()->central_wdg);
  bool map_saved;
  QString img_name;

  full_size_x = (MAP_NATIVE_WIDTH + 2) * tileset_tile_width(tileset);
  full_size_y = (MAP_NATIVE_HEIGHT + 2) * tileset_tile_height(tileset);
  current_width = gui()->mapview_wdg->width();
  current_height = gui()->mapview_wdg->height();
  if (tileset_hex_width(tileset) > 0) {
    full_size_y = full_size_y * 11 / 20;
  } else if (tileset_is_isometric(tileset)) {
    full_size_y = full_size_y / 2;
  }
  map_canvas_resized(full_size_x, full_size_y);
  img_name = QString("Freeciv-Turn%1").arg(game.info.turn);
  if (client_has_player()) {
    img_name = img_name + "-"
                + QString(nation_plural_for_player(client_player()));
  }
  storage_path = freeciv_storage_dir();
  path = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
  if (!storage_path.isEmpty() && QDir(storage_path).isReadable()) {
    img_name = storage_path + DIR_SEPARATOR + img_name;
  } else if (!path.isEmpty()) {
    img_name = path + DIR_SEPARATOR + img_name;
  } else {
    img_name = QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
               + DIR_SEPARATOR + img_name;
  }
  map_saved = mapview.store->map_pixmap.save(img_name, "png");
  map_canvas_resized(current_width, current_height);
  saved->setStandardButtons(QMessageBox::Ok);
  saved->setDefaultButton(QMessageBox::Cancel);
  saved->setAttribute(Qt::WA_DeleteOnClose);
  if (map_saved) {
    saved->set_text_title("Image saved as:\n" + img_name, _("Success"));
  } else {
    saved->set_text_title(_("Failed to save image of the map"), _("Error"));
  }
  saved->show();
}

/**********************************************************************//**
  Menu Save Game
**************************************************************************/
void mr_menu::save_game()
{
  send_save_game(nullptr);
}

/**********************************************************************//**
  Menu Save Game As...
**************************************************************************/
void mr_menu::save_game_as()
{
  QString str;
  QString current_file;
  QString location;

  strvec_iterate(get_save_dirs(), dirname) {
    location = dirname;
    // choose last location
  } strvec_iterate_end;

  str = QString(_("Save Games"))
        + QString(" (*.sav *.sav.bz2 *.sav.gz *.sav.xz)");
  current_file = QFileDialog::getSaveFileName(gui()->central_wdg,
                                              _("Save Game As..."),
                                              location, str);
  if (!current_file.isEmpty()) {
    QByteArray cf_bytes;

    cf_bytes = current_file.toUtf8();
    send_save_game(cf_bytes.data());
  }
}

/**********************************************************************//**
  Back to Main Menu
**************************************************************************/
void mr_menu::back_to_menu()
{
  hud_message_box *ask;

  if (is_server_running()) {
    ask = new hud_message_box(gui()->central_wdg);
    ask->set_text_title(_("Leaving a local game will end it!"), "Leave game");
    ask->setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
    ask->setDefaultButton(QMessageBox::Cancel);
    ask->setAttribute(Qt::WA_DeleteOnClose);

    connect(ask, &hud_message_box::accepted, [=]() {
      if (client.conn.used) {
        gui()->infotab->msgwdg->clr();
        disconnect_from_server(TRUE);
      }
    });
    ask->show();
  } else {
    disconnect_from_server(TRUE);
  }
}

/**********************************************************************//**
  Menu item Volume Up
**************************************************************************/
void mr_menu::volume_up()
{
  struct option *poption = optset_option_by_name(client_optset, "sound_effects_volume");

  gui_options.sound_effects_volume += 10;
  gui_options.sound_effects_volume = CLIP(0, gui_options.sound_effects_volume, 100);
  option_changed(poption);
}

/**********************************************************************//**
  Menu item Volume Down
**************************************************************************/
void mr_menu::volume_down()
{
  struct option *poption = optset_option_by_name(client_optset, "sound_effects_volume");

  gui_options.sound_effects_volume -= 10;
  gui_options.sound_effects_volume = CLIP(0, gui_options.sound_effects_volume, 100);
  option_changed(poption);
}

/**********************************************************************//**
  Prompt to confirm disruptive selection
**************************************************************************/
bool mr_menu::confirm_disruptive_selection()
{
  hud_message_box* ask = new hud_message_box(gui()->central_wdg);

  ask->setIcon(QMessageBox::Warning);
  ask->setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
  ask->setDefaultButton(QMessageBox::Cancel);
  ask->setAttribute(Qt::WA_DeleteOnClose);
  return (ask->set_text_title(_("Selection will cancel current assignments!"),
                              _("Confirm Disruptive Selection"), true)
          == QMessageBox::Ok);
}

/**********************************************************************//**
  Airlift unit type to city acity from each city
**************************************************************************/
void multiairlift(struct city *acity, Unit_type_id ut)
{
  struct tile *ptile;

  city_list_iterate(client_player()->cities, pcity) {
    if (get_city_bonus(pcity, EFT_AIRLIFT) > 0) {
      ptile = city_tile(pcity);
      unit_list_iterate(ptile->units, punit) {
        if (punit->utype == utype_by_number(ut)) {
          request_unit_airlift(punit, acity);
          break;
        }
      } unit_list_iterate_end;
    }
  } city_list_iterate_end;
}
