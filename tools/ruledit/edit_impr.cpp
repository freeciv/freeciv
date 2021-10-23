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
#include <QGridLayout>
#include <QMenu>
#include <QSpinBox>
#include <QToolButton>

// common
#include "improvement.h"

// ruledit
#include "ruledit.h"
#include "ruledit_qt.h"
#include "tab_tech.h"

#include "edit_impr.h"

/**********************************************************************//**
  Setup edit_impr object
**************************************************************************/
edit_impr::edit_impr(ruledit_gui *ui_in, struct impr_type *impr_in) : QDialog()
{
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  QGridLayout *impr_layout = new QGridLayout();
  QLabel *label;
  QMenu *menu;
  int row;

  ui = ui_in;
  impr = impr_in;

  setWindowTitle(QString::fromUtf8(improvement_rule_name(impr)));

  label = new QLabel(QString::fromUtf8(R__("Build Cost")));
  label->setParent(this);

  bcost = new QSpinBox(this);
  bcost->setRange(0, 10000);
  connect(bcost, SIGNAL(valueChanged(int)), this, SLOT(set_bcost_value(int)));

  row = 0;
  impr_layout->addWidget(label, row, 0);
  impr_layout->addWidget(bcost, row++, 2);

  label = new QLabel(QString::fromUtf8(R__("Upkeep")));
  label->setParent(this);

  upkeep = new QSpinBox(this);
  upkeep->setRange(0, 1000);
  connect(upkeep, SIGNAL(valueChanged(int)), this, SLOT(set_upkeep_value(int)));

  impr_layout->addWidget(label, row, 0);
  impr_layout->addWidget(upkeep, row++, 2);

  label = new QLabel(QString::fromUtf8(R__("Genus")));
  label->setParent(this);
  genus_button = new QToolButton();
  genus_button->setParent(this);
  genus_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
  genus_button->setPopupMode(QToolButton::MenuButtonPopup);
  menu = new QMenu();
  connect(menu, SIGNAL(triggered(QAction *)), this, SLOT(genus_menu(QAction *)));

  genus_iterate(genus) {
    menu->addAction(impr_genus_id_name(genus));
  } genus_iterate_end;

  genus_button->setMenu(menu);

  impr_layout->addWidget(label, row, 0);
  impr_layout->addWidget(genus_button, row++, 2);

  refresh();

  main_layout->addLayout(impr_layout);

  setLayout(main_layout);
}

/**********************************************************************//**
  User is closing dialog.
**************************************************************************/
void edit_impr::closeEvent(QCloseEvent *cevent)
{
  impr->ruledit_dlg = nullptr;
}

/**********************************************************************//**
  Refresh the information.
**************************************************************************/
void edit_impr::refresh()
{
  bcost->setValue(impr->build_cost);
  upkeep->setValue(impr->upkeep);
  genus_button->setText(impr_genus_id_name(impr->genus));
}

/**********************************************************************//**
  Read build cost value from spinbox
**************************************************************************/
void edit_impr::set_bcost_value(int value)
{
  impr->build_cost = value;

  refresh();
}

/**********************************************************************//**
  Read upkeep value from spinbox
**************************************************************************/
void edit_impr::set_upkeep_value(int value)
{
  impr->upkeep = value;

  refresh();
}

/**********************************************************************//**
  User selected genus
**************************************************************************/
void edit_impr::genus_menu(QAction *action)
{
  QByteArray gn_bytes;
  enum impr_genus_id genus;

  gn_bytes = action->text().toUtf8();
  genus = impr_genus_id_by_name(gn_bytes.data(), fc_strcasecmp);

  impr->genus = genus;

  refresh();
}
