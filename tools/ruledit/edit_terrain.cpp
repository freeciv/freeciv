/***********************************************************************
 Freeciv - Copyright (C) 2023 The Freeciv Team
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
#include <QLineEdit>
#include <QSpinBox>
#include <QToolButton>

// common
#include "terrain.h"

// ruledit
#include "ruledit.h"
#include "ruledit_qt.h"
#include "tab_tech.h"

#include "edit_terrain.h"

/**********************************************************************//**
  Setup edit_terrain object
**************************************************************************/
edit_terrain::edit_terrain(ruledit_gui *ui_in, struct terrain *ter_in) : QDialog()
{
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  QGridLayout *ter_layout = new QGridLayout();
  QLabel *label;
  int row = 0;

  ui = ui_in;
  ter = ter_in;

  setWindowTitle(QString::fromUtf8(terrain_rule_name(ter)));

  label = new QLabel(QString::fromUtf8(R__("Move Cost")));
  label->setParent(this);

  mcost = new QSpinBox(this);
  mcost->setRange(0, 100);
  connect(mcost, SIGNAL(valueChanged(int)), this, SLOT(set_mcost_value(int)));

  ter_layout->addWidget(label, row, 0);
  ter_layout->addWidget(mcost, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Defense Bonus %")));
  label->setParent(this);

  defense = new QSpinBox(this);
  defense->setRange(0, 1000);
  connect(defense, SIGNAL(valueChanged(int)), this, SLOT(set_defense_value(int)));

  ter_layout->addWidget(label, row, 0);
  ter_layout->addWidget(defense, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Graphics tag")));
  label->setParent(this);

  gfx_tag = new QLineEdit(this);
  connect(gfx_tag, SIGNAL(returnPressed()), this, SLOT(gfx_tag_given()));

  ter_layout->addWidget(label, row, 0);
  ter_layout->addWidget(gfx_tag, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Alt graphics tag")));
  label->setParent(this);

  gfx_tag_alt = new QLineEdit(this);
  connect(gfx_tag_alt, SIGNAL(returnPressed()), this, SLOT(gfx_tag_alt_given()));

  ter_layout->addWidget(label, row, 0);
  ter_layout->addWidget(gfx_tag_alt, row++, 1);

  refresh();

  main_layout->addLayout(ter_layout);

  setLayout(main_layout);
}

/**********************************************************************//**
  User is closing dialog.
**************************************************************************/
void edit_terrain::closeEvent(QCloseEvent *cevent)
{
  // Save values from text fields.
  gfx_tag_given();
  gfx_tag_alt_given();

  ter->ruledit_dlg = nullptr;
}

/**********************************************************************//**
  Refresh the information.
**************************************************************************/
void edit_terrain::refresh()
{
  mcost->setValue(ter->movement_cost);
  defense->setValue(ter->defense_bonus);
  gfx_tag->setText(ter->graphic_str);
  gfx_tag_alt->setText(ter->graphic_alt);
}

/**********************************************************************//**
  Read movement cost value from spinbox
**************************************************************************/
void edit_terrain::set_mcost_value(int value)
{
  ter->movement_cost = value;

  refresh();
}

/**********************************************************************//**
  Read defense value from spinbox
**************************************************************************/
void edit_terrain::set_defense_value(int value)
{
  ter->defense_bonus = value;

  refresh();
}

/**********************************************************************//**
  User entered new graphics tag.
**************************************************************************/
void edit_terrain::gfx_tag_given()
{
  QByteArray tag_bytes = gfx_tag->text().toUtf8();

  sz_strlcpy(ter->graphic_str, tag_bytes);
}

/**********************************************************************//**
  User entered new alternative graphics tag.
**************************************************************************/
void edit_terrain::gfx_tag_alt_given()
{
  QByteArray tag_bytes = gfx_tag_alt->text().toUtf8();

  sz_strlcpy(ter->graphic_alt, tag_bytes);
}
