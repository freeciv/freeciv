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
#include <QToolButton>

// common
#include "extras.h"

// ruledit
#include "ruledit.h"
#include "ruledit_qt.h"

#include "edit_extra.h"

/**********************************************************************//**
  Setup edit_extra object
**************************************************************************/
edit_extra::edit_extra(ruledit_gui *ui_in, struct extra_type *extra_in)
  : QDialog()
{
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  QGridLayout *extra_layout = new QGridLayout();
  QLabel *label;
  int row = 0;

  ui = ui_in;
  extra = extra_in;

  setWindowTitle(QString::fromUtf8(extra_rule_name(extra)));

  label = new QLabel(QString::fromUtf8(R__("Graphics tag")));
  label->setParent(this);

  gfx_tag = new QLineEdit(this);
  connect(gfx_tag, SIGNAL(returnPressed()), this, SLOT(gfx_tag_given()));

  extra_layout->addWidget(label, row, 0);
  extra_layout->addWidget(gfx_tag, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Alt graphics tag")));
  label->setParent(this);

  gfx_tag_alt = new QLineEdit(this);
  connect(gfx_tag_alt, SIGNAL(returnPressed()), this, SLOT(gfx_tag_alt_given()));

  extra_layout->addWidget(label, row, 0);
  extra_layout->addWidget(gfx_tag_alt, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Activity graphics tag")));
  label->setParent(this);

  act_gfx = new QLineEdit(this);
  connect(act_gfx, SIGNAL(returnPressed()), this, SLOT(act_gfx_given()));

  extra_layout->addWidget(label, row, 0);
  extra_layout->addWidget(act_gfx, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Alt activity gfx tag")));
  label->setParent(this);

  act_gfx_alt = new QLineEdit(this);
  connect(act_gfx_alt, SIGNAL(returnPressed()), this, SLOT(act_gfx_alt_given()));

  extra_layout->addWidget(label, row, 0);
  extra_layout->addWidget(act_gfx_alt, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Second alt activity gfx tag")));
  label->setParent(this);

  act_gfx_alt2 = new QLineEdit(this);
  connect(act_gfx_alt2, SIGNAL(returnPressed()), this, SLOT(act_gfx_alt2_given()));

  extra_layout->addWidget(label, row, 0);
  extra_layout->addWidget(act_gfx_alt2, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Removal activity graphics tag")));
  label->setParent(this);

  rmact_gfx = new QLineEdit(this);
  connect(rmact_gfx, SIGNAL(returnPressed()), this, SLOT(rmact_gfx_given()));

  extra_layout->addWidget(label, row, 0);
  extra_layout->addWidget(rmact_gfx, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Alt removal activity gfx tag")));
  label->setParent(this);

  rmact_gfx_alt = new QLineEdit(this);
  connect(rmact_gfx_alt, SIGNAL(returnPressed()), this, SLOT(rmact_gfx_alt_given()));

  extra_layout->addWidget(label, row, 0);
  extra_layout->addWidget(rmact_gfx_alt, row++, 1);

  refresh();

  main_layout->addLayout(extra_layout);

  setLayout(main_layout);
}

/**********************************************************************//**
  User is closing dialog.
**************************************************************************/
void edit_extra::closeEvent(QCloseEvent *cevent)
{
  // Save values from text fields.
  gfx_tag_given();
  gfx_tag_alt_given();
  act_gfx_given();
  act_gfx_alt_given();
  act_gfx_alt2_given();
  rmact_gfx_given();
  rmact_gfx_alt_given();

  extra->ruledit_dlg = nullptr;
}

/**********************************************************************//**
  Refresh the information.
**************************************************************************/
void edit_extra::refresh()
{
  gfx_tag->setText(extra->graphic_str);
  gfx_tag_alt->setText(extra->graphic_alt);
  act_gfx->setText(extra->activity_gfx);
  act_gfx_alt->setText(extra->act_gfx_alt);
  act_gfx_alt2->setText(extra->act_gfx_alt2);
  rmact_gfx->setText(extra->rmact_gfx);
  rmact_gfx_alt->setText(extra->rmact_gfx_alt);
}

/**********************************************************************//**
  User entered new graphics tag.
**************************************************************************/
void edit_extra::gfx_tag_given()
{
  QByteArray tag_bytes = gfx_tag->text().toUtf8();

  sz_strlcpy(extra->graphic_str, tag_bytes);
}

/**********************************************************************//**
  User entered new alternative graphics tag.
**************************************************************************/
void edit_extra::gfx_tag_alt_given()
{
  QByteArray tag_bytes = gfx_tag_alt->text().toUtf8();

  sz_strlcpy(extra->graphic_alt, tag_bytes);
}

/**********************************************************************//**
  User entered new activity graphics tag.
**************************************************************************/
void edit_extra::act_gfx_given()
{
  QByteArray tag_bytes = act_gfx_alt->text().toUtf8();

  sz_strlcpy(extra->activity_gfx, tag_bytes);
}

/**********************************************************************//**
  User entered new alternative activity graphics tag.
**************************************************************************/
void edit_extra::act_gfx_alt_given()
{
  QByteArray tag_bytes = act_gfx_alt->text().toUtf8();

  sz_strlcpy(extra->act_gfx_alt, tag_bytes);
}

/**********************************************************************//**
  User entered new secondary alternative activity graphics tag.
**************************************************************************/
void edit_extra::act_gfx_alt2_given()
{
  QByteArray tag_bytes = act_gfx_alt2->text().toUtf8();

  sz_strlcpy(extra->act_gfx_alt2, tag_bytes);
}

/**********************************************************************//**
  User entered new alternative graphics tag.
**************************************************************************/
void edit_extra::rmact_gfx_given()
{
  QByteArray tag_bytes = rmact_gfx->text().toUtf8();

  sz_strlcpy(extra->rmact_gfx, tag_bytes);
}

/**********************************************************************//**
  User entered new alternative removal activity graphics tag.
**************************************************************************/
void edit_extra::rmact_gfx_alt_given()
{
  QByteArray tag_bytes = rmact_gfx_alt->text().toUtf8();

  sz_strlcpy(extra->rmact_gfx_alt, tag_bytes);
}
