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

// Qt
#include <QMenuBar>
#include <QObject>
#include <QSignalMapper>

class mr_menu : public QMenuBar
{
  Q_OBJECT
  QMenu *menu;
  QMenu *gov_menu;
  QList<QAction*> gov_list;
public:
  mr_menu();
  void setup_menus();
  void setup_gov_menu();
  void gov_menu_sensitive();
  void rm_gov_menu();
private slots:
  /* help menu */
  void slot_menu_copying();
  void slot_about_qt();

  /*used by work menu*/
  void slot_build_city();
  void slot_build_road();
  void slot_build_irrigation();
  void slot_build_mine();

  /*used by unit menu */
  void slot_unit_sentry();

  /*used by combat menu*/
  void slot_unit_fortify();

  /*used by view menu*/
  void slot_center_view();

  /*used by civilization menu */
  void slot_popup_tax_rates();
  void slot_show_research_tab();
  void slot_gov_change(const int &target);
  void revolution();

private:
  int gov_count;
  int gov_target;
  QSignalMapper *signal_gov_mapper;
};

#endif /* FC__MENU_H */
