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
#include <QLineEdit>
#include <QMenu>
#include <QSpinBox>
#include <QToolButton>

// common
#include "unittype.h"

// ruledit
#include "ruledit.h"
#include "ruledit_qt.h"
#include "tab_tech.h"

#include "edit_utype.h"

/**********************************************************************//**
  Setup edit_utype object
**************************************************************************/
edit_utype::edit_utype(ruledit_gui *ui_in, struct unit_type *utype_in) : QDialog()
{
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  QGridLayout *unit_layout = new QGridLayout();
  QLabel *label;
  int row = 0;

  ui = ui_in;
  utype = utype_in;

  setWindowTitle(QString::fromUtf8(utype_rule_name(utype)));

  label = new QLabel(QString::fromUtf8(R__("Build Cost")));
  label->setParent(this);

  bcost = new QSpinBox(this);
  bcost->setRange(0, 10000);
  connect(bcost, SIGNAL
          (valueChanged(int)), this, SLOT(set_bcost_value(int)));

  unit_layout->addWidget(label, row, 0);
  unit_layout->addWidget(bcost, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Attack Strength")));
  label->setParent(this);

  attack = new QSpinBox(this);
  attack->setRange(0, 1000);
  connect(attack, SIGNAL(valueChanged(int)), this, SLOT(set_attack_value(int)));

  unit_layout->addWidget(label, row, 0);
  unit_layout->addWidget(attack, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Defense Strength")));
  label->setParent(this);

  defense = new QSpinBox(this);
  defense->setRange(0, 1000);
  connect(defense, SIGNAL(valueChanged(int)), this, SLOT(set_defense_value(int)));

  unit_layout->addWidget(label, row, 0);
  unit_layout->addWidget(defense, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Move Rate")));
  label->setParent(this);

  move_rate = new QSpinBox(this);
  move_rate->setRange(0, 50);
  connect(move_rate, SIGNAL(valueChanged(int)), this, SLOT(set_move_rate(int)));

  unit_layout->addWidget(label, row, 0);
  unit_layout->addWidget(move_rate, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Graphics tag")));
  label->setParent(this);

  gfx_tag = new QLineEdit(this);
  connect(gfx_tag, SIGNAL(returnPressed()), this, SLOT(gfx_tag_given()));

  unit_layout->addWidget(label, row, 0);
  unit_layout->addWidget(gfx_tag, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Alt graphics tag")));
  label->setParent(this);

  gfx_tag_alt = new QLineEdit(this);
  connect(gfx_tag_alt, SIGNAL(returnPressed()), this, SLOT(gfx_tag_alt_given()));

  unit_layout->addWidget(label, row, 0);
  unit_layout->addWidget(gfx_tag_alt, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Second alt gfx tag")));
  label->setParent(this);

  gfx_tag_alt2 = new QLineEdit(this);
  connect(gfx_tag_alt2, SIGNAL(returnPressed()), this, SLOT(gfx_tag_alt2_given()));

  unit_layout->addWidget(label, row, 0);
  unit_layout->addWidget(gfx_tag_alt2, row++, 1);

  refresh();

  main_layout->addLayout(unit_layout);

  setLayout(main_layout);
}

/**********************************************************************//**
  User is closing dialog.
**************************************************************************/
void edit_utype::closeEvent(QCloseEvent *cevent)
{
  // Save values from text fields.
  gfx_tag_given();
  gfx_tag_alt_given();
  gfx_tag_alt2_given();

  utype->ruledit_dlg = nullptr;
}

/**********************************************************************//**
  Refresh the information.
**************************************************************************/
void edit_utype::refresh()
{
  bcost->setValue(utype->build_cost);
  attack->setValue(utype->attack_strength);
  defense->setValue(utype->defense_strength);
  move_rate->setValue(utype->move_rate);
  gfx_tag->setText(utype->graphic_str);
  gfx_tag_alt->setText(utype->graphic_alt);
  gfx_tag_alt2->setText(utype->graphic_alt2);
}

/**********************************************************************//**
  Read build cost value from spinbox
**************************************************************************/
void edit_utype::set_bcost_value(int value)
{
  utype->build_cost = value;
}

/**********************************************************************//**
  Read attack strength value from spinbox
**************************************************************************/
void edit_utype::set_attack_value(int value)
{
  utype->attack_strength = value;
}

/**********************************************************************//**
  Read defense strength value from spinbox
**************************************************************************/
void edit_utype::set_defense_value(int value)
{
  utype->defense_strength = value;
}

/**********************************************************************//**
  Read move rater from spinbox
**************************************************************************/
void edit_utype::set_move_rate(int value)
{
  utype->move_rate = value;
}

/**********************************************************************//**
  User entered new graphics tag.
**************************************************************************/
void edit_utype::gfx_tag_given()
{
  QByteArray tag_bytes = gfx_tag->text().toUtf8();

  sz_strlcpy(utype->graphic_str, tag_bytes);
}

/**********************************************************************//**
  User entered new alternative graphics tag.
**************************************************************************/
void edit_utype::gfx_tag_alt_given()
{
  QByteArray tag_bytes = gfx_tag_alt->text().toUtf8();

  sz_strlcpy(utype->graphic_alt, tag_bytes);
}

/**********************************************************************//**
  User entered new secondary alternative graphics tag.
**************************************************************************/
void edit_utype::gfx_tag_alt2_given()
{
  QByteArray tag_bytes = gfx_tag_alt2->text().toUtf8();

  sz_strlcpy(utype->graphic_alt2, tag_bytes);
}
