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

// Qt
#include <QApplication>
#include <QMainWindow>
#include <QMessageBox>

// common
#include "game.h"
#include "government.h"
#include "road.h"
#include "unit.h"

// client
#include "control.h"

// gui-qt
#include "qtg_cxxside.h"
#include "fc_client.h"
#include "chatline.h"
#include "cityrep.h"
#include "dialogs.h"
#include "gui_main.h"
#include "plrdlg.h"
#include "ratesdlg.h"
#include "repodlgs.h"
#include "spaceshipdlg.h"
#include "sprite.h"

#include "menu.h"

extern QApplication *qapp;
/**************************************************************************
  Initialize menus (sensitivity, name, etc.) based on the
  current state and current ruleset, etc.  Call menus_update().
**************************************************************************/
void real_menus_init(void)
{
  /* PORTME */
  gui()->menu_bar->rm_gov_menu();
  gui()->menu_bar->setup_gov_menu();
}

/**************************************************************************
  Update all of the menus (sensitivity, name, etc.) based on the
  current state.
**************************************************************************/
void real_menus_update(void)
{
  /* PORTME */
  gui()->menu_bar->gov_menu_sensitive();
  gui()->menu_bar->menus_sensitive();
}

/****************************************************************************
  Return the text for the tile, changed by the activity.
  Should only be called for irrigation, mining, or transformation, and
  only when the activity changes the base terrain type.
****************************************************************************/
static const char *get_tile_change_menu_text(struct tile *ptile,
                                             enum unit_activity activity)
{
  struct tile *newtile = tile_virtual_new(ptile);
  const char *text;

  tile_apply_activity(newtile, activity, NULL);
  text = tile_get_info_text(newtile, FALSE, 0);
  tile_virtual_destroy(newtile);
  return text;
}

/****************************************************************************
  Constructor for global menubar in gameview
****************************************************************************/
mr_menu::mr_menu() : QMenuBar()
{
  gov_menu = NULL;
  signal_gov_mapper = NULL;
}

/****************************************************************************
  Initializes menu system, and add custom enum(munit) for most of options
  Notice that if you set option for QAction->setChecked(option) it will
  check/uncheck automatically without any intervention
****************************************************************************/
void mr_menu::setup_menus()
{
  QAction *act;
  QMenu *pr;

  /* Game Menu */
  menu = this->addMenu(_("Game"));
  pr = menu;
  menu = menu->addMenu(_("Options"));
  act = menu->addAction(_("Set local options"));
  connect(act, SIGNAL(triggered()), this, SLOT(local_options()));
  act = menu->addAction(_("Server Options"));
  connect(act, SIGNAL(triggered()), this, SLOT(server_options()));
  menu = pr;
  act = menu->addAction(_("Quit"));
  act->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
  connect(act, SIGNAL(triggered()), this, SLOT(quit_game()));

  /* View Menu */
  menu = this->addMenu(Q_("?verb:View"));
  act = menu->addAction(_("Center View"));
  act->setShortcut(QKeySequence(tr("c")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_center_view()));
  menu->addSeparator();
  act = menu->addAction(_("Fullscreen"));
  act->setShortcut(QKeySequence(tr("alt+return")));
  act->setCheckable(true);
  act->setChecked(options.fullscreen_mode);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_fullscreen()));
  menu->addSeparator();
  minimap_status = menu->addAction(_("Minimap"));
  minimap_status->setCheckable(true);
  minimap_status->setShortcut(QKeySequence(tr("ctrl+m")));
  minimap_status->setChecked(true);
  connect(minimap_status, SIGNAL(triggered()), this, 
          SLOT(slot_minimap_view()));
  messages_status = menu->addAction(_("Messages"));
  messages_status->setCheckable(true);
  messages_status->setChecked(true);
  connect(messages_status, SIGNAL(triggered()), this, SLOT(slot_show_messages()));
  chat_status = menu->addAction(_("Chat"));
  chat_status->setCheckable(true);
  chat_status->setChecked(true);
  connect(chat_status, SIGNAL(triggered()), this, SLOT(slot_show_chat()));
  menu->addSeparator();
  act = menu->addAction(_("City Outlines"));
  act->setShortcut(QKeySequence(tr("Ctrl+y")));
  act->setCheckable(true);
  act->setChecked(options.draw_city_outlines);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_city_outlines()));
  act = menu->addAction(_("City Output"));
  act->setCheckable(true);
  act->setChecked(options.draw_city_output);
  act->setShortcut(QKeySequence(tr("ctrl+w")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_city_output()));
  act = menu->addAction(_("Map Grid"));
  act->setShortcut(QKeySequence(tr("ctrl+g")));
  act->setCheckable(true);
  act->setChecked(options.draw_map_grid);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_map_grid()));
  act = menu->addAction(_("National Borders"));
  act->setCheckable(true);
  act->setChecked(options.draw_borders);
  act->setShortcut(QKeySequence(tr("ctrl+b")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_borders()));
  act = menu->addAction(_("City Full Bar"));
  act->setCheckable(true);
  act->setChecked(options.draw_full_citybar);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_fullbar()));
  act = menu->addAction(_("City Names"));
  act->setCheckable(true);
  act->setChecked(options.draw_city_names);
  act->setShortcut(QKeySequence(tr("ctrl+n")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_city_names()));
  act = menu->addAction(_("City Growth"));
  act->setCheckable(true);
  act->setChecked(options.draw_city_growth);
  act->setShortcut(QKeySequence(tr("ctrl+r")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_city_growth()));
  act = menu->addAction(_("City Production Levels"));
  act->setCheckable(true);
  act->setChecked(options.draw_city_productions);
  act->setShortcut(QKeySequence(tr("ctrl+p")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_city_production()));
  act = menu->addAction(_("City Buy Cost"));
  act->setCheckable(true);
  act->setChecked(options.draw_city_buycost);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_city_buycost()));
  act = menu->addAction(_("City Traderoutes"));
  act->setCheckable(true);
  act->setChecked(options.draw_city_trade_routes);
  act->setShortcut(QKeySequence(tr("ctrl+d")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_city_traderoutes()));

  /* Select Menu */
  menu = this->addMenu(_("Select"));
  act = menu->addAction(_("Single Unit (Unselect Others)"));
  act->setShortcut(QKeySequence(tr("z")));
  menu_list.insertMulti(STANDARD, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_select_one()));
  act = menu->addAction(_("All On Tile"));
  act->setShortcut(QKeySequence(tr("v")));
  menu_list.insertMulti(STANDARD, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_select_all_tile()));
  menu->addSeparator();
  act = menu->addAction(_("Same Type on Tile"));
  act->setShortcut(QKeySequence(tr("shift+v")));
  menu_list.insertMulti(STANDARD, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_select_same_tile()));
  act = menu->addAction(_("Same Type on Continent"));
  act->setShortcut(QKeySequence(tr("shift+c")));
  menu_list.insertMulti(STANDARD, act);
  connect(act, SIGNAL(triggered()), this, 
          SLOT(slot_select_same_continent()));
  act = menu->addAction(_("Same Type Everywhere"));
  act->setShortcut(QKeySequence(tr("shift+x")));
  menu_list.insertMulti(STANDARD, act);
  connect(act, SIGNAL(triggered()), this, 
          SLOT(slot_select_same_everywhere()));
  menu->addSeparator();
  act = menu->addAction(_("Unit selection dialog"));
  menu_list.insertMulti(STANDARD, act);
  act->setShortcut(QKeySequence(tr("ctrl+s")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_selection_dialog()));
  menu->addSeparator();
  act = menu->addAction(_("Wait"));
  act->setShortcut(QKeySequence(tr("w")));
  menu_list.insertMulti(STANDARD, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_wait()));
  act = menu->addAction(_("Done"));
  act->setShortcut(QKeySequence(tr("space")));
  menu_list.insertMulti(STANDARD, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_done_moving()));

  /* Unit Menu */
  menu = this->addMenu(_("Unit"));
  act = menu->addAction(_("Go to Tile"));
  act->setShortcut(QKeySequence(tr("g")));
  menu_list.insertMulti(STANDARD, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_unit_goto()));
  act = menu->addAction(_("Go to Nearest City"));
  act->setShortcut(QKeySequence(tr("shift+g")));
  menu_list.insertMulti(GOTO_CITY, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_return_to_city()));
  menu->addSeparator();
  act = menu->addAction(_("Auto Explore"));
  menu_list.insertMulti(EXPLORE, act);
  act->setShortcut(QKeySequence(tr("x")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_unit_explore()));
  act = menu->addAction(_("Patrol"));
  menu_list.insertMulti(STANDARD, act);
  act->setEnabled(false);
  act->setShortcut(QKeySequence(tr("q")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_patrol()));
  menu->addSeparator();
  act = menu->addAction(_("Sentry"));
  act->setShortcut(QKeySequence(tr("s")));
  menu_list.insertMulti(SENTRY, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_unit_sentry()));
  act = menu->addAction(_("Unsentry All On Tile"));
  act->setShortcut(QKeySequence(tr("shift+s")));
  menu_list.insertMulti(WAKEUP, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_unsentry()));
  menu->addSeparator();
  act = menu->addAction(_("Load"));
  act->setShortcut(QKeySequence(tr("l")));
  menu_list.insertMulti(LOAD, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_load()));
  act = menu->addAction(_("Unload"));
  act->setShortcut(QKeySequence(tr("u")));
  menu_list.insertMulti(UNLOAD, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_unload()));
  act = menu->addAction(_("Unload All From Transporter"));
  act->setShortcut(QKeySequence(tr("shift+u")));
  menu_list.insertMulti(TRANSPORTER, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_unload_all()));
  menu->addSeparator();
  act = menu->addAction(_("Set Home City"));
  menu_list.insertMulti(HOMECITY, act);
  act->setShortcut(QKeySequence(tr("h")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_set_home()));
  act = menu->addAction(_("Upgrade"));
  act->setShortcut(QKeySequence(tr("shift+u")));
  menu_list.insertMulti(UPGRADE, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_upgrade()));
  act = menu->addAction(_("Convert"));
  act->setShortcut(QKeySequence(tr("ctrl+o")));
  menu_list.insertMulti(CONVERT, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_convert()));
  act = menu->addAction(_("Disband"));
  act->setShortcut(QKeySequence(tr("shift+d")));
  menu_list.insertMulti(DISBAND, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_disband()));

  /* Combat Menu */
  menu = this->addMenu(_("Combat"));
  act = menu->addAction(_("Fortify Unit"));
  menu_list.insertMulti(FORTRESS, act);
  act->setShortcut(QKeySequence(tr("f")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_unit_fortify()));
  act = menu->addAction(_("Build Type B Base"));
  menu_list.insertMulti(AIRBASE, act);
  act->setShortcut(QKeySequence(tr("e")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_unit_airbase()));
  menu->addSeparator();
  act = menu->addAction(_("Pillage"));
  menu_list.insertMulti(PILLAGE, act);
  act->setShortcut(QKeySequence(tr("shift+p")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_pillage()));
  act = menu->addAction(_("Diplomat/Spy actions"));
  menu_list.insertMulti(ORDER_DIPLOMAT_DLG, act);
  act->setShortcut(QKeySequence(tr("d")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_action()));

  /* Work Menu */
  menu = this->addMenu(_("Work"));
  act = menu->addAction(_("Build City"));
  act->setShortcut(QKeySequence(tr("b")));
  menu_list.insertMulti(BUILD, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_build_city()));
  act = menu->addAction(_("Go And Build City"));
  menu_list.insertMulti(SETTLER, act);
  act->setShortcut(QKeySequence(tr("shift+b")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_go_build_city()));
  act = menu->addAction(_("Auto Settler"));
  act->setShortcut(QKeySequence(tr("a")));
  menu_list.insertMulti(AUTOSETTLER, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_auto_settler()));
  menu->addSeparator();
  act = menu->addAction(_("Build Road"));
  menu_list.insertMulti(ROAD, act);
  act->setShortcut(QKeySequence(tr("r")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_build_road()));
  act = menu->addAction(_("Build Irrigation"));
  act->setShortcut(QKeySequence(tr("i")));
  menu_list.insertMulti(IRRIGATION, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_build_irrigation()));
  act = menu->addAction(_("Build Mine"));
  act->setShortcut(QKeySequence(tr("m")));
  menu_list.insertMulti(MINE, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_build_mine()));
  menu->addSeparator();
  act = menu->addAction(_("Connect With Road"));
  act->setShortcut(QKeySequence(tr("shift+r")));
  menu_list.insertMulti(CONNECT_ROAD, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_conn_road()));
  act = menu->addAction(_("Connect With Railroad"));
  menu_list.insertMulti(CONNECT_RAIL, act);
  act->setShortcut(QKeySequence(tr("shift+l")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_conn_rail()));
  act = menu->addAction(_("Connect With Irrigation"));
  menu_list.insertMulti(CONNECT_IRRIGATION, act);
  act->setShortcut(QKeySequence(tr("shift+i")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_conn_irrigation()));
  menu->addSeparator();
  act = menu->addAction(_("Transform Terrain"));
  menu_list.insertMulti(TRANSFORM, act);
  act->setShortcut(QKeySequence(tr("o")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_transform()));
  act = menu->addAction(_("Clean Pollution"));
  menu_list.insertMulti(POLLUTION, act);
  act->setShortcut(QKeySequence(tr("p")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_clean_pollution()));
  act = menu->addAction(_("Clean Nuclear Fallout"));
  menu_list.insertMulti(FALLOUT, act);
  act->setShortcut(QKeySequence(tr("n")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_clean_fallout()));

  /* Civilization menu */
  menu = this->addMenu(_("Civilization"));
  act = menu->addAction(_("Tax Rates..."));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_popup_tax_rates()));
  menu->addSeparator();

  gov_menu= menu->addMenu(_("Government"));
  menu->addSeparator();

  act = menu->addAction(Q_("?noun:View"));
  act->setShortcut(QKeySequence(tr("F1")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_show_map()));

  act = menu->addAction(_("Units"));
  act->setShortcut(QKeySequence(tr("F2")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_show_units_report()));

  act = menu->addAction(_("Players"));
  act->setShortcut(QKeySequence(tr("F3")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_show_nations()));
  
  act = menu->addAction(_("Cities"));
  act->setShortcut(QKeySequence(tr("F4")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_show_cities()));

  act = menu->addAction(_("Economy"));
  act->setShortcut(QKeySequence(tr("F5")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_show_eco_report()));

  act = menu->addAction(_("Research"));
  act->setShortcut(QKeySequence(tr("F6")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_show_research_tab()));

  act = menu->addAction(_("Wonders of the World"));
  act->setShortcut(QKeySequence(tr("F7")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_traveler()));

  act = menu->addAction(_("Top Five Cities"));
  act->setShortcut(QKeySequence(tr("F8")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_top_five()));

  act = menu->addAction(_("Demographics"));
  act->setShortcut(QKeySequence(tr("F11")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_demographics()));

  act = menu->addAction(_("Spaceship"));
  act->setShortcut(QKeySequence(tr("F12")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_spaceship()));

  act = menu->addAction(_("Achievements"));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_achievements()));

  /* Help Menu */
  menu = this->addMenu(_("Help"));
  act = menu->addAction(_("Copying"));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_menu_copying()));

  act = menu->addAction(_("About Qt"));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_about_qt()));

  this->setVisible(false);
}

/****************************************************************************
  Enables/disables menu items and renames them depending on key in menu_list
****************************************************************************/
void mr_menu::menus_sensitive()
{
  QList <QAction * >values;
  QList <munit > keys;
  QHash <munit, QAction *>::iterator i;
  struct unit_list *punits = NULL;
  struct road_type *proad;
  struct extra_type *tgt;
  bool any_cities = false;
  bool city_on_tile;
  bool units_all_same_tile = true;
  const struct tile *ptile = NULL;
  struct terrain *pterrain;
  const struct unit_type *ptype = NULL;

  players_iterate(pplayer) {
    if (city_list_size(pplayer->cities)) {
      any_cities = true;
      break;
    }
  } players_iterate_end;

  /** Disable first all sensitive menus */
  foreach(QAction * a, menu_list) {
    a->setEnabled(false);
  }

  if (can_client_issue_orders() == false || get_num_units_in_focus() == 0) {
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
    fc_assert((ptile == NULL) == (ptype == NULL));
    if (ptile || ptype) {
      if (unit_tile(punit) != ptile) {
        units_all_same_tile = false;
      }
      if (unit_type(punit) == ptype) {
        ptile = unit_tile(punit);
        ptype = unit_type(punit);
      }
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
        if (can_units_do_activity(punits, ACTIVITY_EXPLORE)) {
          i.value()->setEnabled(true);
        }
        break;

      case LOAD:
        if (units_can_load(punits)) {
          i.value()->setEnabled(true);
        }
        break;

      case UNLOAD:
        if (units_can_unload(punits)) {
          i.value()->setEnabled(true);
        }
        break;

      case TRANSPORTER:
        if (units_are_occupied(punits)) {
          i.value()->setEnabled(true);
        }
        break;

      case CONVERT:
        if (units_can_convert(punits)) {
          i.value()->setEnabled(true);
        }
        break;

      case MINE:
        if (can_units_do_activity(punits, ACTIVITY_MINE)) {
          i.value()->setEnabled(true);
        }

        if (units_all_same_tile) {
          struct unit *punit = unit_list_get(punits, 0);

          pterrain = tile_terrain(unit_tile(punit));
          if (pterrain->mining_result != T_NONE
              && pterrain->mining_result != pterrain) {
            i.value()->setText(
              QString(_("Transform to %1")).
                      /* TRANS: Transfrom terrain to specific type */
                      arg(QString(get_tile_change_menu_text
                      (unit_tile(punit), ACTIVITY_MINE))));
          } else if (units_have_type_flag(punits, UTYF_SETTLERS, TRUE)){
            struct extra_type *pextra = NULL;

            /* FIXME: this overloading doesn't work well with multiple focus
             * units. */
            unit_list_iterate(punits, punit) {
              pextra = next_extra_for_tile(unit_tile(punit), EC_MINE,
                                           unit_owner(punit), punit);
              if (pextra != NULL) {
                break;
              }
            } unit_list_iterate_end;

            if (pextra != NULL) {
              /* TRANS: Build mine of specific type */
              i.value()->setText(QString(_("Build %1"))
                .arg(extra_name_translation(pextra)));
            } else {
              i.value()->setText(QString(_("Build Mine")));
            }
          } else {
            i.value()->setText(QString(_("Build Mine")));
          }
        }
        break;

      case IRRIGATION:
        if (can_units_do_activity(punits, ACTIVITY_IRRIGATE)) {
          i.value()->setEnabled(true);
        }
        if (units_all_same_tile) {
          struct unit *punit = unit_list_get(punits, 0);

          pterrain = tile_terrain(unit_tile(punit));
          if (pterrain->irrigation_result != T_NONE
              && pterrain->irrigation_result != pterrain) {
            i.value()->setText(QString(_("Transform to %1")).
                      /* TRANS: Transfrom terrain to specific type */
                      arg(QString(get_tile_change_menu_text
                      (unit_tile(punit), ACTIVITY_IRRIGATE))));
          } else if (units_have_type_flag(punits, UTYF_SETTLERS, TRUE)){
            struct extra_type *pextra = NULL;

            /* FIXME: this overloading doesn't work well with multiple focus
             * units. */
            unit_list_iterate(punits, punit) {
              pextra = next_extra_for_tile(unit_tile(punit), EC_IRRIGATION,
                                           unit_owner(punit), punit);
              if (pextra != NULL) {
                break;
              }
            } unit_list_iterate_end;

            if (pextra != NULL) {
              /* TRANS: Build irrigation of specific type */
              i.value()->setText(QString(_("Build %1"))
                .arg(extra_name_translation(pextra)));
            } else {
              i.value()->setText(QString(_("Build Irrigation")));
            }
          } else {
            i.value()->setText(QString(_("Build Irrigation")));
          }
          
        }
        break;

      case TRANSFORM:
        if (can_units_do_activity(punits, ACTIVITY_TRANSFORM)) {
          i.value()->setEnabled(true);
        } else {
          break;
        }
        if (units_all_same_tile) {
          struct unit *punit = unit_list_get(punits, 0);
          pterrain = tile_terrain(unit_tile(punit));
          punit = unit_list_get(punits, 0);
          pterrain = tile_terrain(unit_tile(punit));
          if (pterrain->transform_result != T_NONE
              && pterrain->transform_result != pterrain) {
            i.value()->setText(QString(_("Transform to %1")).
                      /* TRANS: Transfrom terrain to specific type */
                      arg(QString(get_tile_change_menu_text
                              (unit_tile(punit), ACTIVITY_TRANSFORM))));
          } else {
            i.value()->setText(_("Transform Terrain"));
          }
        }
        break;

      case BUILD:
        if (can_units_do(punits, unit_can_add_or_build_city)) {
          i.value()->setEnabled(true);
        } else {
          break;
        }
        if (city_on_tile
            && units_have_type_flag(punits, UTYF_ADD_TO_CITY, true)) {
          i.value()->setText(_("Add to City"));
        } else {
          i.value()->setText(_("Build City"));
        }
        break;

      case ROAD:
        if (can_units_do_any_road(punits)) {
          i.value()->setEnabled(true);
        }
        break;

      case SETTLER:
        if (can_units_do(punits, unit_can_add_or_build_city)) {
          i.value()->setEnabled(true);
        }
        break;

      case FORTRESS:
        if (can_units_do_base_gui(punits, BASE_GUI_FORTRESS)
            || can_units_do_activity(punits, ACTIVITY_FORTIFYING)) {
          i.value()->setEnabled(true);
        }
        if (can_units_do_activity(punits, ACTIVITY_FORTIFYING)) {
          i.value()->setText(_("Fortify Unit"));
        } else {
          i.value()->setText(_("Build Type A Base"));
        }
        break;

      case AIRBASE:
        if (can_units_do_base_gui(punits, BASE_GUI_AIRBASE)) {
          i.value()->setEnabled(true);
        }
        break;

      case POLLUTION:
        if (can_units_do_activity(punits, ACTIVITY_POLLUTION)
            || can_units_do(punits, can_unit_paradrop)) {
          i.value()->setEnabled(true);
        }
        if (units_have_type_flag(punits, UTYF_PARATROOPERS, true)) {
          i.value()->setText(_("Drop Paratrooper"));
        } else {
          i.value()->setText(_("Clean Pollution"));
        }
        break;

      case FALLOUT:
        if (can_units_do_activity(punits, ACTIVITY_FALLOUT)) {
          i.value()->setEnabled(true);
        }
        break;

      case SENTRY:
        if (can_units_do_activity(punits, ACTIVITY_SENTRY)) {
          i.value()->setEnabled(true);
        }
        break;

      case PILLAGE:
        if (can_units_do_activity(punits, ACTIVITY_PILLAGE)) {
          i.value()->setEnabled(true);
        }
        break;

      case HOMECITY:
        if (can_units_do(punits, can_unit_change_homecity)) {
          i.value()->setEnabled(true);
        }
        break;

      case WAKEUP:
        if (units_have_activity_on_tile(punits, ACTIVITY_SENTRY)) {
          i.value()->setEnabled(true);
        }
        break;

      case AUTOSETTLER:
        if (can_units_do(punits, can_unit_do_autosettlers)) {
          i.value()->setEnabled(true);
        }
        break;

      case CONNECT_ROAD:
        proad = road_by_compat_special(ROCO_ROAD);
        if (proad != NULL) {
          tgt = road_extra_get(proad);
        } else {
          break;
        }
        if (can_units_do_connect(punits, ACTIVITY_GEN_ROAD, tgt)) {
          i.value()->setEnabled(true);
        }
        break;

      case DISBAND:
        if (units_have_type_flag(punits, UTYF_UNDISBANDABLE, false)) {
          i.value()->setEnabled(true);
        }

      case CONNECT_RAIL:
        proad = road_by_compat_special(ROCO_RAILROAD);
        if (proad != NULL) {
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

      case BUILD_WONDER:
        if (can_units_do(punits, unit_can_help_build_wonder_here)) {
          i.value()->setEnabled(true);
        }
        break;

      case ORDER_TRADEROUTE:
        if (can_units_do(punits, unit_can_est_trade_route_here)) {
          i.value()->setEnabled(true);
        }
        break;

      case ORDER_DIPLOMAT_DLG:
        if (can_units_act_against_own_tile(punits)) {
          i.value()->setEnabled(true);
        }
        break;

      case NUKE:
        if (units_have_type_flag(punits, UTYF_NUCLEAR, true)) {
          i.value()->setEnabled(true);
        }
        break;

      case UPGRADE:
        if (units_can_upgrade(punits)) {
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

/****************************************************************************
  Builds government menu and adds to gov_menu as submenu
****************************************************************************/
void mr_menu::setup_gov_menu()
{
  QAction *act;
  struct government *gov;
  struct government *revol_gov;
  QPixmap *pix;
  int j = 2;

  revol_gov = game.government_during_revolution;
  if (gov_menu && gov_menu->isEmpty()) {
    gov_count = government_count();

    if (signal_gov_mapper) {
      delete signal_gov_mapper;
    }

    signal_gov_mapper = new QSignalMapper;
    act = gov_menu->addAction(QString::fromUtf8(_("Revolution...")));
    connect(act, SIGNAL(triggered()), this, SLOT(revolution()));
    gov_list.insert(0, act);
    act = gov_menu->addSeparator();
    gov_list.insert(1, act);

    if (gov_count > 0) {
      for (int i = 0; i < gov_count; i++) {
        gov = government_by_number(i);
        pix = get_government_sprite(tileset, gov)->pm;
        if (gov != revol_gov) { /** skip revolution goverment */
          act = gov_menu->addAction(QString::fromUtf8(
                                      gov->name._private_translated_));
          if (pix != NULL) {
            act->setIcon(QIcon(*pix));
          }
          gov_list.insert(j, act); /** governments on list start from 2
                                    *  second (1) is separator, first (0)
                                    *  is "Revolution..." text
                                    *  list is needed to set menus
                                    *  enabled/disabled and to remove it
                                    *  after starting new game */
          j++;
          connect(act, SIGNAL(triggered()), signal_gov_mapper, SLOT(map()));
          signal_gov_mapper->setMapping(act, i);
        }
      }
    }
    connect(signal_gov_mapper, SIGNAL(mapped(const int &)),
             this, SLOT(slot_gov_change(const int &)));
  }
}

/****************************************************************************
  Calls revolutions MessageBox
****************************************************************************/
void mr_menu::revolution()
{
  popup_revolution_dialog();
}

/****************************************************************************
  Enables/Disables goverment submenu items
****************************************************************************/
void mr_menu::gov_menu_sensitive()
{
  int i, j;
  QAction *act;
  struct government *gover;

  i = gov_list.count();

  for (j = 2; j < i; j++) {
    act = gov_list.at(j);
    gover = government_by_number(j-1);

    if (can_change_to_government(client.conn.playing, gover)) {
      act->setEnabled(true);
    } else {
      act->setDisabled(true);
    }
  }
}

/****************************************************************************
  Removes all item in government submenu
****************************************************************************/
void mr_menu::rm_gov_menu()
{
  QAction *act;

  while (!gov_list.empty()) {
    act = gov_list.takeFirst();
    gov_menu->removeAction(act);
    /** gov menu should be empty now, reinit it using setup_gov_menu */
  }
}

/****************************************************************************
  User has choosen target governement in menu
****************************************************************************/
void mr_menu::slot_gov_change (const int &target)
{
  struct government *gov;

  gov = government_by_number(target);
  popup_revolution_dialog(gov);
}

/****************************************************************************
  Copying item selected from Help menu.
****************************************************************************/
void mr_menu::slot_menu_copying()
{
  QMessageBox info(this);
  QString s = QString::fromUtf8(_("Freeciv is covered by the GPL. "))
              + QString::fromUtf8(_("See file COPYING distributed with "
                                    "Freeciv for full license text."));

  info.setText(s);
  info.setStandardButtons(QMessageBox::Ok);
  info.setDefaultButton(QMessageBox::Cancel);
  info.setIcon(QMessageBox::Information);
  info.setWindowTitle(_("Copying"));
  info.exec();
}

/***************************************************************************
  Slot for showing research tab
***************************************************************************/
void mr_menu::slot_show_research_tab()
{
  science_report_dialog_popup(true);
}

/***************************************************************************
  Slot for showing spaceship
***************************************************************************/
void mr_menu::slot_spaceship()
{
  if (NULL != client.conn.playing) {
    popup_spaceship_dialog(client.conn.playing);
  }
}

/***************************************************************************
  Slot for showing economy tab
***************************************************************************/
void mr_menu::slot_show_eco_report()
{
  economy_report_dialog_popup(false);
}

/***************************************************************************
  Changes tab to mapview
***************************************************************************/
void mr_menu::slot_show_map()
{
  ::gui()->game_tab_widget->setCurrentIndex(0);
}

/***************************************************************************
  Slot for showing units tab
***************************************************************************/
void mr_menu::slot_show_units_report()
{
  units_report_dialog_popup(false);
}

void mr_menu::slot_show_nations()
{
  popup_players_dialog(false);
}

void mr_menu::slot_show_cities()
{
  city_report_dialog_popup(false);
}

/****************************************************************
  Action "BUILD_CITY"
*****************************************************************/
void mr_menu::slot_build_city()
{
  unit_list_iterate(get_units_in_focus(), punit) {
    /* FIXME: this can provide different actions for different units...
     * not good! */
    /* Enable the button for adding to a city in all cases, so we
       get an eventual error message from the server if we try. */
    if (unit_can_add_or_build_city(punit)) {
      request_unit_build_city(punit);
    } else if (unit_has_type_flag(punit, UTYF_HELP_WONDER)) {
      request_unit_caravan_action(punit, PACKET_UNIT_HELP_BUILD_WONDER);
    }
  } unit_list_iterate_end;
}

/***************************************************************************
  Action "CLEAN FALLOUT"
***************************************************************************/
void mr_menu::slot_clean_fallout()
{
  key_unit_fallout();
}

/***************************************************************************
  Action "CLEAN POLLUTION and PARADROP"
***************************************************************************/
void mr_menu::slot_clean_pollution()
{
  unit_list_iterate(get_units_in_focus(), punit) {
    /* FIXME: this can provide different actions for different units...
     * not good! */
    struct extra_type *pextra;

    pextra = prev_extra_in_tile(unit_tile(punit), ERM_CLEANPOLLUTION,
                                unit_owner(punit), punit);
    if (pextra != NULL) {
      request_new_unit_activity_targeted(punit, ACTIVITY_POLLUTION, pextra);
    } else if (can_unit_paradrop(punit)) {
      /* FIXME: This is getting worse, we use a key_unit_*() function
       * which assign the order for all units!  Very bad! */
      key_unit_paradrop();
    }
  } unit_list_iterate_end;
}

/***************************************************************************
  Action "CONNECT WITH IRRIGATION"
***************************************************************************/
void mr_menu::slot_conn_irrigation()
{
  struct extra_type_list *extras = extra_type_list_by_cause(EC_IRRIGATION);

  if (extra_type_list_size(extras) > 0) {
    struct extra_type *pextra;

    pextra = extra_type_list_get(extra_type_list_by_cause(EC_IRRIGATION), 0);

    key_unit_connect(ACTIVITY_IRRIGATE, pextra);
  }
}

/***************************************************************************
  Action "CONNECT WITH RAILROAD"
***************************************************************************/
void mr_menu::slot_conn_rail()
{
  struct road_type *prail = road_by_compat_special(ROCO_RAILROAD);

  if (prail != NULL) {
    struct extra_type *tgt;

    tgt = road_extra_get(prail);
    key_unit_connect(ACTIVITY_GEN_ROAD, tgt);
  }
}

/***************************************************************************
  Action "BUILD AIRBASE"
***************************************************************************/
void mr_menu::slot_unit_airbase()
{
  key_unit_airbase();
}

/***************************************************************************
  Action "CONNECT WITH ROAD"
***************************************************************************/
void mr_menu::slot_conn_road()
{
  struct road_type *proad = road_by_compat_special(ROCO_ROAD);

  if (proad != NULL) {
    struct extra_type *tgt;

    tgt = road_extra_get(proad);
    key_unit_connect(ACTIVITY_GEN_ROAD, tgt);
  }
}

/***************************************************************************
  Action "GO TO AND BUILD CITY"
***************************************************************************/
void mr_menu::slot_go_build_city()
{
  request_unit_goto(ORDER_BUILD_CITY);
}

/***************************************************************************
  Action "TRANSFROM TERRAIN"
***************************************************************************/
void mr_menu::slot_transform()
{
  key_unit_transform();
}

/***************************************************************************
  Action "PILLAGE"
***************************************************************************/
void mr_menu::slot_pillage()
{
  key_unit_pillage();
}

/***************************************************************************
  Diplomat/Spy action
***************************************************************************/
void mr_menu::slot_action()
{
  key_unit_diplomat_actions();
}


/****************************************************************
  Action "AUTO_SETTLER"
*****************************************************************/
void mr_menu::slot_auto_settler()
{
  key_unit_auto_settle();
}

/****************************************************************
  Action "BUILD_ROAD"
*****************************************************************/
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

    if (tgt != NULL
        && can_unit_do_activity_targeted(punit, ACTIVITY_GEN_ROAD, tgt)) {
      request_new_unit_activity_targeted(punit, ACTIVITY_GEN_ROAD, tgt);
      building_road = true;
    }

    if (!building_road && unit_can_est_trade_route_here(punit)) {
      request_unit_caravan_action(punit, PACKET_UNIT_ESTABLISH_TRADE);
    }
  } unit_list_iterate_end;
}

/****************************************************************
  Action "BUILD_IRRIGATION"
*****************************************************************/
void mr_menu::slot_build_irrigation()
{
  key_unit_irrigate();
}

/****************************************************************
  Action "BUILD_MINE"
*****************************************************************/
void mr_menu::slot_build_mine()
{
  key_unit_mine();
}
/****************************************************************
  Action "FORTIFY AND BUILD BASE A (USUALLY FORTRESS)"
*****************************************************************/
void mr_menu::slot_unit_fortify()
{
  unit_list_iterate(get_units_in_focus(), punit) {
    /* FIXME: this can provide different actions for different units...
     * not good! */
    struct base_type *pbase = get_base_by_gui_type(BASE_GUI_FORTRESS,
                                                   punit, unit_tile(punit));
    struct extra_type *pextra = NULL;

    if (pbase) {
      pextra = base_extra_get(pbase);
    }

    if (pextra && can_unit_do_activity_targeted(punit, ACTIVITY_BASE, pextra)) {
      request_new_unit_activity_targeted(punit, ACTIVITY_BASE, pextra);
    } else {
      request_unit_fortify(punit);
    }
  } unit_list_iterate_end;
}
/****************************************************************
  Action "SENTRY"
*****************************************************************/
void mr_menu::slot_unit_sentry()
{
  key_unit_sentry();
}

/***************************************************************************
  Action "CONVERT"
***************************************************************************/
void mr_menu::slot_convert()
{
  key_unit_convert();
}

/***************************************************************************
  Action "DISBAND UNIT"
***************************************************************************/
void mr_menu::slot_disband()
{
  popup_disband_dialog(get_units_in_focus());
}

/***************************************************************************
  Action "LOAD INTO TRANSPORTER"
***************************************************************************/
void mr_menu::slot_load()
{
  unit_list_iterate(get_units_in_focus(), punit) {
    request_unit_load(punit, NULL);
  } unit_list_iterate_end;
}

/***************************************************************************
  Action "UNIT PATROL"
***************************************************************************/
void mr_menu::slot_patrol()
{
  key_unit_patrol();
}

/***************************************************************************
  Action "RETURN TO NEAREST CITY"
***************************************************************************/
void mr_menu::slot_return_to_city()
{
  unit_list_iterate(get_units_in_focus(), punit) {
    request_unit_return(punit);
  } unit_list_iterate_end;
}

/***************************************************************************
  Action "SET HOMECITY"
***************************************************************************/
void mr_menu::slot_set_home()
{
  key_unit_homecity();
}

/***************************************************************************
  Action "UNLOAD FROM TRANSPORTED"
***************************************************************************/
void mr_menu::slot_unload()
{
  unit_list_iterate(get_units_in_focus(), punit) {
    request_unit_unload(punit);
  } unit_list_iterate_end;
}

/***************************************************************************
  Action "UNLOAD ALL UNITS FROM TRANSPORTER"
***************************************************************************/
void mr_menu::slot_unload_all()
{
  key_unit_unload_all();
}

/***************************************************************************
  Action "UNSENTRY(WAKEUP) ALL UNITS"
***************************************************************************/
void mr_menu::slot_unsentry()
{
  key_unit_wakeup_others();
}

/***************************************************************************
  Action "UPGRADE UNITS"
***************************************************************************/
void mr_menu::slot_upgrade()
{
  popup_upgrade_dialog(get_units_in_focus());
}

/****************************************************************
  Action "GOTO"
*****************************************************************/
void mr_menu::slot_unit_goto()
{
  key_unit_goto();
}

/****************************************************************
  Action "EXPLORE"
*****************************************************************/
void mr_menu::slot_unit_explore()
{
  key_unit_auto_explore();
}

/****************************************************************
  Action "CENTER VIEW"
*****************************************************************/
void mr_menu::slot_center_view()
{
  request_center_focus_unit();
}

/***************************************************************************
  Action "SET FULLSCREEN"
***************************************************************************/
void mr_menu::slot_fullscreen()
{
  if (!options.fullscreen_mode) {
    gui()->main_window->showFullScreen();
    gui()->mapview_wdg->showFullScreen();
  } else {
    gui()->main_window->showNormal();
  }
  options.fullscreen_mode = !options.fullscreen_mode;
}

/***************************************************************************
  Action "SHOW CHAT"
***************************************************************************/
void mr_menu::slot_show_chat()
{
  if (gui()->infotab->hidden_mess && !gui()->infotab->hidden_chat) {
    gui()->infotab->hide_chat(true);
    gui()->infotab->hide_me();
    return;
  }
  if (gui()->infotab->hidden_mess && gui()->infotab->hidden_chat) {
    gui()->infotab->hide_me();
    gui()->infotab->hide_messages(true);
    gui()->infotab->hide_chat(false);
    return;
  }
  if (!gui()->infotab->hidden_chat) {
    gui()->infotab->hide_chat(true);
  } else {
    gui()->infotab->hide_chat(false);
  }
}

/***************************************************************************
  Action "SHOW MESSAGES"
***************************************************************************/
void mr_menu::slot_show_messages()
{
  if (!gui()->infotab->hidden_mess && gui()->infotab->hidden_chat) {
    gui()->infotab->hide_messages(true);
    gui()->infotab->hide_me();
    return;
  }
  if (gui()->infotab->hidden_mess && gui()->infotab->hidden_chat) {
    gui()->infotab->hide_me();
    gui()->infotab->hide_messages(false);
    gui()->infotab->hide_chat(true);
    return;
  }
  if (!gui()->infotab->hidden_mess) {
    gui()->infotab->hide_messages(true);
  } else {
    gui()->infotab->hide_messages(false);
  }
}
/****************************************************************
  Action "VIEW/HIDE MINIMAP"
*****************************************************************/
void mr_menu::slot_minimap_view()
{
  if (minimap_status->isChecked()){
    ::gui()->minimapview_wdg->show();
  } else {
    ::gui()->minimapview_wdg->hide();
  }
}

/****************************************************************
  Action "SHOW BORDERS"
*****************************************************************/
void mr_menu::slot_borders()
{
  key_map_borders_toggle();
}

/***************************************************************************
  Action "SHOW BUY COST"
***************************************************************************/
void mr_menu::slot_city_buycost()
{
  key_city_buycost_toggle();
}

/***************************************************************************
  Action "SHOW CITY GROWTH"
***************************************************************************/
void mr_menu::slot_city_growth()
{
  key_city_growth_toggle();
}

/***************************************************************************
  Action "SHOW CITY NAMES"
***************************************************************************/
void mr_menu::slot_city_names()
{
  key_city_names_toggle();
}

/***************************************************************************
  Action "SHOW CITY OUTLINES"
***************************************************************************/
void mr_menu::slot_city_outlines()
{
  key_city_outlines_toggle();
}

/***************************************************************************
  Action "SHOW CITY OUTPUT"
***************************************************************************/
void mr_menu::slot_city_output()
{
  key_city_output_toggle();
}

/***************************************************************************
  Action "SHOW CITY PRODUCTION"
***************************************************************************/
void mr_menu::slot_city_production()
{
  key_city_productions_toggle();
}

/***************************************************************************
  Action "SHOW CITY TRADEROUTES"
***************************************************************************/
void mr_menu::slot_city_traderoutes()
{
  key_city_trade_routes_toggle();
}

/***************************************************************************
  Action "SHOW FULLBAR"
***************************************************************************/
void mr_menu::slot_fullbar()
{
  key_city_full_bar_toggle();
}

/***************************************************************************
  Action "SHOW MAP GRID"
***************************************************************************/
void mr_menu::slot_map_grid()
{
  key_map_grid_toggle();
}

/***************************************************************************
  Action "DONE MOVING"
***************************************************************************/
void mr_menu::slot_done_moving()
{
  key_unit_done();
}

/***************************************************************************
  Action "SELECT ALL UNITS ON TILE"
***************************************************************************/
void mr_menu::slot_select_all_tile()
{
  request_unit_select(get_units_in_focus(), SELTYPE_ALL, SELLOC_TILE);
}

/***************************************************************************
  Action "SELECT ONE UNITS/DESELECT OTHERS"
***************************************************************************/
void mr_menu::slot_select_one()
{
  request_unit_select(get_units_in_focus(), SELTYPE_SINGLE, SELLOC_TILE);
}

/***************************************************************************
  Action "SELLECT SAME UNITS ON CONTINENT"
***************************************************************************/
void mr_menu::slot_select_same_continent()
{
  request_unit_select(get_units_in_focus(), SELTYPE_SAME, SELLOC_CONT);
}

/***************************************************************************
  Action "SELECT SAME TYPE EVERYWHERE"
***************************************************************************/
void mr_menu::slot_select_same_everywhere()
{
  request_unit_select(get_units_in_focus(), SELTYPE_SAME, SELLOC_WORLD);
}

/***************************************************************************
  Action "SELECT SAME TYPE ON TILE"
***************************************************************************/
void mr_menu::slot_select_same_tile()
{
  request_unit_select(get_units_in_focus(), SELTYPE_SAME, SELLOC_TILE);
}

/***************************************************************************
  Action "SHOW UNIT SELECTION DIALOG"
***************************************************************************/
void mr_menu::slot_selection_dialog()
{
  struct unit *punit = head_of_units_in_focus();
  if (punit != NULL && punit->tile){
    unit_select_dialog_popup(punit->tile);
  }
}

/***************************************************************************
  Action "WAIT"
***************************************************************************/
void mr_menu::slot_wait()
{
  key_unit_wait();
}

/****************************************************************
  Action "SHOW DEMOGRAPGHICS REPORT"
*****************************************************************/
void mr_menu::slot_demographics()
{
  send_report_request(REPORT_DEMOGRAPHIC);
}

/****************************************************************
  Action "SHOW ACHIEVEMENTS REPORT"
*****************************************************************/
void mr_menu::slot_achievements()
{
  send_report_request(REPORT_ACHIEVEMENTS);
}

/****************************************************************
  Action "SHOW TOP FIVE CITIES"
*****************************************************************/
void mr_menu::slot_top_five()
{
  send_report_request(REPORT_TOP_5_CITIES);
}

/****************************************************************
  Action "SHOW WONDERS REPORT"
*****************************************************************/
void mr_menu::slot_traveler()
{
  send_report_request(REPORT_WONDERS_OF_THE_WORLD);
}


/****************************************************************
  Action "TAX RATES"
*****************************************************************/
void mr_menu::slot_popup_tax_rates()
{
  popup_rates_dialog();
}

/****************************************************************
  Information about Qt version
*****************************************************************/
void mr_menu::slot_about_qt()
{
  qapp->aboutQt();
}

/****************************************************************
  Invoke dialog with local options
*****************************************************************/
void mr_menu::local_options()
{
  gui()->popup_client_options();
}

/****************************************************************
  Invoke dialog with server options
*****************************************************************/
void mr_menu::server_options()
{
  gui()->popup_server_options();
}

/***************************************************************************
  Invoke popup for quiting game
***************************************************************************/
void mr_menu::quit_game()
{
  popup_quit_dialog();
}
