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
#include <QPushButton>

// common
#include "government.h"

// ruledit
#include "helpeditor.h"
#include "ruledit.h"
#include "ruledit_qt.h"

#include "edit_gov.h"


/**********************************************************************//**
  Setup edit_gov object
**************************************************************************/
edit_gov::edit_gov(ruledit_gui *ui_in, struct government *gov_in) : values_dlg()
{
  QHBoxLayout *main_layout = new QHBoxLayout(this);
  QGridLayout *gov_layout = new QGridLayout();
  QLabel *label;
  QPushButton *button;
  int row;

  ui = ui_in;
  gov = gov_in;

  setWindowTitle(QString::fromUtf8(government_rule_name(gov)));

  row = 0;

  label = new QLabel(QString::fromUtf8(R__("Graphics tag")));
  label->setParent(this);

  gfx_tag = new QLineEdit(this);
  connect(gfx_tag, SIGNAL(returnPressed()), this, SLOT(gfx_tag_given()));

  gov_layout->addWidget(label, row, 0);
  gov_layout->addWidget(gfx_tag, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Alt graphics tag")));
  label->setParent(this);

  gfx_tag_alt = new QLineEdit(this);
  connect(gfx_tag_alt, SIGNAL(returnPressed()), this, SLOT(gfx_tag_alt_given()));

  gov_layout->addWidget(label, row, 0);
  gov_layout->addWidget(gfx_tag_alt, row++, 1);

  button = new QPushButton(QString::fromUtf8(R__("Helptext")), this);
  connect(button, SIGNAL(pressed()), this, SLOT(helptext()));
  gov_layout->addWidget(button, row++, 1);

  refresh();

  main_layout->addLayout(gov_layout);

  setLayout(main_layout);
}

/**********************************************************************//**
  User is closing dialog.
**************************************************************************/
void edit_gov::closeEvent(QCloseEvent *cevent)
{
  close_help();

  // Save values from text fields.
  gfx_tag_given();
  gfx_tag_alt_given();

  gov->ruledit_dlg = nullptr;
}

/**********************************************************************//**
  Refresh the information.
**************************************************************************/
void edit_gov::refresh()
{
  gfx_tag->setText(gov->graphic_str);
  gfx_tag_alt->setText(gov->graphic_alt);
}

/**********************************************************************//**
  User entered new graphics tag.
**************************************************************************/
void edit_gov::gfx_tag_given()
{
  QByteArray tag_bytes = gfx_tag->text().toUtf8();

  sz_strlcpy(gov->graphic_str, tag_bytes);
}

/**********************************************************************//**
  User entered new alternative graphics tag.
**************************************************************************/
void edit_gov::gfx_tag_alt_given()
{
  QByteArray tag_bytes = gfx_tag_alt->text().toUtf8();

  sz_strlcpy(gov->graphic_alt, tag_bytes);
}

/**********************************************************************//**
  User pressed helptext button
**************************************************************************/
void edit_gov::helptext()
{
  open_help(&gov->helptext);
}
