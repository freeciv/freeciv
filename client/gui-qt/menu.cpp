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
#include <QMenuBar>

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
#include "ratesdlg.h"
#include "repodlgs.h"

#include "menu.h"

extern QApplication *qapp;
extern void popup_revolution_dialog(struct government *government = 0);
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
}

mr_menu::mr_menu() : QMenuBar()
{
  gov_menu = NULL;
  signal_gov_mapper = NULL;
}

/****************************************************************************
  Initializes menu system
****************************************************************************/
void mr_menu::setup_menus()
{
  QAction *act;

  /* View Menu */
  menu = this->addMenu(_("View"));
  act = menu->addAction(_("Center View"));
  act->setShortcut(QKeySequence(tr("c")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_center_view()));

  /* Unit Menu */
  menu = this->addMenu(_("Unit"));
  act = menu->addAction(_("Sentry"));
  act->setShortcut(QKeySequence(tr("s")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_unit_sentry()));

  /* Combat Menu */
  menu = this->addMenu(_("Combat"));
  act = menu->addAction(_("Fortify"));
  act->setShortcut(QKeySequence(tr("f")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_unit_fortify()));

  /* Work Menu */
  menu = this->addMenu(_("Work"));
  act = menu->addAction(_("Build city"));
  act->setShortcut(QKeySequence(tr("b")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_build_city()));

  act = menu->addAction(_("Build road"));
  act->setShortcut(QKeySequence(tr("r")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_build_road()));

  act = menu->addAction(_("Build irrigation"));
  act->setShortcut(QKeySequence(tr("i")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_build_irrigation()));

  act = menu->addAction(_("Build mine"));
  act->setShortcut(QKeySequence(tr("m")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_build_mine()));

  /* Civilization menu */
  menu = this->addMenu(_("Civilization"));
  act = menu->addAction(_("Tax rates"));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_popup_tax_rates()));
  menu->addSeparator();

  gov_menu= menu->addMenu(_("Government"));
  menu->addSeparator();

  act = menu->addAction(_("Research"));
  act->setShortcut(QKeySequence(tr("F6")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_show_research_tab()));

  /* Help Menu */
  menu = this->addMenu(_("Help"));
  act = menu->addAction(_("Copying"));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_menu_copying()));

  act = menu->addAction(_("About Qt"));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_about_qt()));

  this->setVisible(false);
}

/****************************************************************************
  Builds government menu and adds to gov_menu as submenu
****************************************************************************/
void mr_menu::setup_gov_menu()
{
  QAction *act;
  struct government *gov;
  struct government *revol_gov;
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
        if (gov != revol_gov) { /** skip revolution goverment */
          act = gov_menu->addAction(QString::fromUtf8(
                                      gov->name._private_translated_));
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
  QMessageBox info;
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
  science_report_dialog_popup(false);
}


/****************************************************************
  Action "BUILD_CITY"
*****************************************************************/
void mr_menu::slot_build_city()
{
  key_unit_build_city();
}

/****************************************************************
  Action "BUILD_ROAD"
*****************************************************************/
void mr_menu::slot_build_road()
{
  key_unit_road();
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
  Action "FORTIFY"
*****************************************************************/
void mr_menu::slot_unit_fortify()
{
  key_unit_fortify();
}
/****************************************************************
  Action "SENTRY"
*****************************************************************/
void mr_menu::slot_unit_sentry()
{
  key_unit_sentry();
}

/****************************************************************
  Action "CENTER VIEW"
*****************************************************************/
void mr_menu::slot_center_view()
{
  request_center_focus_unit();
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
