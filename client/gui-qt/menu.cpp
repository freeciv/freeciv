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
/**************************************************************************
  Initialize menus (sensitivity, name, etc.) based on the
  current state and current ruleset, etc.  Call menus_update().
**************************************************************************/
void real_menus_init(void)
{
  /* PORTME */
}

/**************************************************************************
  Update all of the menus (sensitivity, name, etc.) based on the
  current state.
**************************************************************************/
void real_menus_update(void)
{
  /* PORTME */
}

/****************************************************************************
  Initializes menu system
****************************************************************************/
void mr_menu::setup_menus()
{
  QMenu *menu;
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

  /* Help Menu */
  menu = this->addMenu(_("Civilization"));
  act = menu->addAction(_("Tax rates"));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_popup_tax_rates()));

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
  Copying item selected from Help menu.
****************************************************************************/
void mr_menu::slot_menu_copying()
{
  QMessageBox info;
  QString s = QString::fromUtf8(_("Freeciv is covered by the GPL. "))
              + QString::fromUtf8(_("See file COPYING distributed with "
                                    "freeciv for full license text."));
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
