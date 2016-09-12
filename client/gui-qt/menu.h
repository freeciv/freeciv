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

#ifndef FC__MENU_H
#define FC__MENU_H

extern "C" {
#include "menu_g.h"
}

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

// Qt
#include <QDialog>
#include <QMenuBar>

// client
#include "control.h"

class QLabel;
class QPushButton;
class QSignalMapper;
class QScrollArea;

/** used for indicating menu about current option - for renaming
 * and enabling, disabling */
enum munit {
  STANDARD,
  EXPLORE,
  LOAD,
  UNLOAD,
  TRANSPORTER,
  DISBAND,
  CONVERT,
  MINE,
  IRRIGATION,
  TRANSFORM,
  PILLAGE,
  BUILD,
  ROAD,
  FORTRESS,
  AIRBASE,
  POLLUTION,
  FALLOUT,
  SENTRY,
  HOMECITY,
  WAKEUP,
  AUTOSETTLER,
  CONNECT_ROAD,
  CONNECT_RAIL,
  CONNECT_IRRIGATION,
  GOTO_CITY,
  AIRLIFT,
  BUILD_WONDER,
  ORDER_TRADEROUTE,
  ORDER_DIPLOMAT_DLG,
  NUKE,
  UPGRADE,
  GO_AND_BUILD_CITY,
  NOT_4_OBS,
  SAVE
};

/**************************************************************************
  Class for filtering chosen units
**************************************************************************/
class unit_filter {
public:
  unit_filter();
  void reset_activity();
  void reset_other();
  bool any_activity;
  bool forified;
  bool idle;
  bool sentried;
  bool any;
  bool full_mp;
  bool full_hp;
  bool full_hp_mp;
};

/**************************************************************************
  Custom dialog to show information
**************************************************************************/
class fc_message_box : public QDialog
{
  Q_OBJECT

public:
  fc_message_box() {};
  void info(QWidget *parent, const QString &title, const QString &mess);

private:
  QLabel *label;
  QScrollArea *scroll;
  QPushButton *ok_button;
};

/****************************************************************************
  Instantiable government menu.
****************************************************************************/
class gov_menu : public QMenu
{
  Q_OBJECT
  static QSet<gov_menu *> instances;

  QSignalMapper *gov_mapper;
  QVector<QAction *> actions;

public:
  gov_menu(QWidget* parent = 0);
  virtual ~gov_menu();

  static void create_all();
  static void update_all();

public slots:
  void revolution();
  void change_gov(int target_gov);

  void create();
  void update();
};

/**************************************************************************
  Class representing global menus in gameview
**************************************************************************/
class mr_menu : public QMenuBar
{
  Q_OBJECT
  QMenu *menu;
  QMenu *filter_menu;
  QActionGroup *filter_act;
  QActionGroup *filter_any;;
  QHash<munit, QAction*> menu_list;
  unit_filter u_filter;
public:
  mr_menu();
  void setup_menus();
  void menus_sensitive();
  QAction *minimap_status;
  QAction *chat_status;
  QAction *messages_status;
private slots:
  /* game menu */
  void local_options();
  void server_options();
  void messages_options();
  void save_options_now();
  void save_game();
  void save_game_as();
  void back_to_menu();
  void quit_game();

  /* help menu */
  void slot_help(const QString &topic);

  /*used by work menu*/
  void slot_build_city();
  void slot_go_build_city();
  void slot_auto_settler();
  void slot_build_road();
  void slot_build_irrigation();
  void slot_build_mine();
  void slot_conn_road();
  void slot_conn_rail();
  void slot_conn_irrigation();
  void slot_transform();
  void slot_clean_pollution();
  void slot_clean_fallout();

  /*used by unit menu */
  void slot_unit_sentry();
  void slot_unit_explore();
  void slot_unit_goto();
  void slot_airlift();
  void slot_return_to_city();
  void slot_patrol();
  void slot_unsentry();
  void slot_load();
  void slot_unload();
  void slot_unload_all();
  void slot_set_home();
  void slot_upgrade();
  void slot_convert();
  void slot_disband();

  /*used by combat menu*/
  void slot_unit_fortify();
  void slot_unit_airbase();
  void slot_pillage();
  void slot_action();
  void slot_explode_nuclear();

  /*used by view menu*/
  void slot_center_view();
  void slot_minimap_view();
  void slot_fullscreen();
  void slot_city_outlines();
  void slot_city_output();
  void slot_map_grid();
  void slot_borders();
  void slot_fullbar();
  void slot_native_tiles();
  void slot_city_growth();
  void slot_city_production();
  void slot_city_buycost();
  void slot_city_traderoutes();
  void slot_city_names();
  
  /*used by select menu */
  void slot_select_one();
  void slot_select_all_tile();
  void slot_select_same_tile();
  void slot_select_same_continent();
  void slot_select_same_everywhere();
  void slot_done_moving();
  void slot_wait();
  void slot_filter();
  void slot_filter_other();

  /*used by civilization menu */
  void slot_show_map();
  void slot_popup_tax_rates();
  void slot_show_eco_report();
  void slot_show_units_report();
  void slot_show_nations();
  void slot_show_cities();
  void slot_show_research_tab();
  void slot_spaceship();
  void slot_demographics();
  void slot_top_five();
  void slot_traveler();
  void slot_show_chat();
  void slot_show_messages();

private:
  void unit_select(struct unit_list *punits, 
                   enum unit_select_type_mode seltype,
                   enum unit_select_location_mode selloc);
  void apply_filter(struct unit *punit);
  void apply_2nd_filter(struct unit *punit);
  QSignalMapper *signal_help_mapper;
};

#endif /* FC__MENU_H */
