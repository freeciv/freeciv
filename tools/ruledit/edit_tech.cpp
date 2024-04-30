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
#include <QCheckBox>
#include <QGridLayout>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>

// common
#include "tech.h"

// ruledit
#include "helpeditor.h"
#include "ruledit.h"
#include "ruledit_qt.h"

#include "edit_tech.h"


#define FLAGROWS 7

/**********************************************************************//**
  Setup edit_tech object
**************************************************************************/
edit_tech::edit_tech(ruledit_gui *ui_in, struct advance *tech_in) : values_dlg()
{
  QHBoxLayout *main_layout = new QHBoxLayout(this);
  QGridLayout *tech_layout = new QGridLayout();
  QLabel *label;
  QPushButton *button;
  int row;
  int rowcount;
  int column;

  ui = ui_in;
  tech = tech_in;

  flag_layout = new QGridLayout();

  setWindowTitle(QString::fromUtf8(advance_rule_name(tech)));

  label = new QLabel(QString::fromUtf8(R__("Cost")));
  label->setParent(this);

  cost = new QSpinBox(this);
  cost->setRange(0, 10000);
  connect(cost, SIGNAL(valueChanged(int)), this, SLOT(set_cost_value(int)));

  row = 0;
  tech_layout->addWidget(label, row, 0);
  tech_layout->addWidget(cost, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Graphics tag")));
  label->setParent(this);

  gfx_tag = new QLineEdit(this);
  connect(gfx_tag, SIGNAL(returnPressed()), this, SLOT(gfx_tag_given()));

  tech_layout->addWidget(label, row, 0);
  tech_layout->addWidget(gfx_tag, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Alt graphics tag")));
  label->setParent(this);

  gfx_tag_alt = new QLineEdit(this);
  connect(gfx_tag_alt, SIGNAL(returnPressed()), this, SLOT(gfx_tag_alt_given()));

  tech_layout->addWidget(label, row, 0);
  tech_layout->addWidget(gfx_tag_alt, row++, 1);

  button = new QPushButton(QString::fromUtf8(R__("Helptext")), this);
  connect(button, SIGNAL(pressed()), this, SLOT(helptext()));
  tech_layout->addWidget(button, row++, 1);

  rowcount = 0;
  column = 0;
  for (int i = 0; i < TF_COUNT; i++) {
    enum tech_flag_id flag = (enum tech_flag_id)i;
    QCheckBox *check = new QCheckBox();

    label = new QLabel(tech_flag_id_name(flag));
    flag_layout->addWidget(label, rowcount, column + 1);

    check->setChecked(BV_ISSET(tech->flags, flag));
    flag_layout->addWidget(check, rowcount, column);

    if (++rowcount >= FLAGROWS) {
      column += 2;
      rowcount = 0;
    }
  }

  refresh();

  main_layout->addLayout(tech_layout);
  main_layout->addLayout(flag_layout);

  setLayout(main_layout);
}

/**********************************************************************//**
  User is closing dialog.
**************************************************************************/
void edit_tech::closeEvent(QCloseEvent *cevent)
{
  int rowcount;
  int column;

  close_help();

  // Save values from text fields.
  gfx_tag_given();
  gfx_tag_alt_given();

  BV_CLR_ALL(tech->flags);
  rowcount = 0;
  column = 0;
  for (int i = 0; i < TF_COUNT; i++) {
    QCheckBox *check = static_cast<QCheckBox *>(flag_layout->itemAtPosition(rowcount, column)->widget());

    if (check->isChecked()) {
      BV_SET(tech->flags, i);
    }

    if (++rowcount >= FLAGROWS) {
      rowcount = 0;
      column += 2;
    }
  }

  tech->ruledit_dlg = nullptr;
}

/**********************************************************************//**
  Refresh the information.
**************************************************************************/
void edit_tech::refresh()
{
  cost->setValue(tech->cost);
  gfx_tag->setText(tech->graphic_str);
  gfx_tag_alt->setText(tech->graphic_alt);
}

/**********************************************************************//**
  Read cost value from spinbox
**************************************************************************/
void edit_tech::set_cost_value(int value)
{
  tech->cost = value;

  refresh();
}

/**********************************************************************//**
  User entered new graphics tag.
**************************************************************************/
void edit_tech::gfx_tag_given()
{
  QByteArray tag_bytes = gfx_tag->text().toUtf8();

  sz_strlcpy(tech->graphic_str, tag_bytes);
}

/**********************************************************************//**
  User entered new alternative graphics tag.
**************************************************************************/
void edit_tech::gfx_tag_alt_given()
{
  QByteArray tag_bytes = gfx_tag_alt->text().toUtf8();

  sz_strlcpy(tech->graphic_alt, tag_bytes);
}

/**********************************************************************//**
  User pressed helptext button
**************************************************************************/
void edit_tech::helptext()
{
  open_help(&tech->helptext);
}
