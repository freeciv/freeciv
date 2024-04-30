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
#include "improvement.h"

// ruledit
#include "helpeditor.h"
#include "ruledit.h"
#include "ruledit_qt.h"
#include "tab_tech.h"

#include "edit_impr.h"


#define FLAGROWS 15

/**********************************************************************//**
  Setup edit_impr object
**************************************************************************/
edit_impr::edit_impr(ruledit_gui *ui_in, struct impr_type *impr_in) : values_dlg()
{
  QHBoxLayout *main_layout = new QHBoxLayout(this);
  QGridLayout *impr_layout = new QGridLayout();
  QLabel *label;
  QPushButton *button;
  QMenu *menu;
  int row;
  int rowcount;
  int column;

  ui = ui_in;
  impr = impr_in;

  flag_layout = new QGridLayout();

  setWindowTitle(QString::fromUtf8(improvement_rule_name(impr)));

  label = new QLabel(QString::fromUtf8(R__("Build Cost")));
  label->setParent(this);

  bcost = new QSpinBox(this);
  bcost->setRange(0, 10000);
  connect(bcost, SIGNAL(valueChanged(int)), this, SLOT(set_bcost_value(int)));

  row = 0;
  impr_layout->addWidget(label, row, 0);
  impr_layout->addWidget(bcost, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Upkeep")));
  label->setParent(this);

  upkeep = new QSpinBox(this);
  upkeep->setRange(0, 1000);
  connect(upkeep, SIGNAL(valueChanged(int)), this, SLOT(set_upkeep_value(int)));

  impr_layout->addWidget(label, row, 0);
  impr_layout->addWidget(upkeep, row++, 1);

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
  impr_layout->addWidget(genus_button, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Graphics tag")));
  label->setParent(this);

  gfx_tag = new QLineEdit(this);
  connect(gfx_tag, SIGNAL(returnPressed()), this, SLOT(gfx_tag_given()));

  impr_layout->addWidget(label, row, 0);
  impr_layout->addWidget(gfx_tag, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Alt graphics tag")));
  label->setParent(this);

  gfx_tag_alt = new QLineEdit(this);
  connect(gfx_tag_alt, SIGNAL(returnPressed()), this, SLOT(gfx_tag_alt_given()));

  impr_layout->addWidget(label, row, 0);
  impr_layout->addWidget(gfx_tag_alt, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Second alt gfx tag")));
  label->setParent(this);

  gfx_tag_alt2 = new QLineEdit(this);
  connect(gfx_tag_alt2, SIGNAL(returnPressed()), this, SLOT(gfx_tag_alt2_given()));

  impr_layout->addWidget(label, row, 0);
  impr_layout->addWidget(gfx_tag_alt2, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Sound tag")));
  label->setParent(this);

  sound_tag = new QLineEdit(this);
  connect(sound_tag, SIGNAL(returnPressed()), this, SLOT(sound_tag_given()));

  impr_layout->addWidget(label, row, 0);
  impr_layout->addWidget(sound_tag, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Alt sound tag")));
  label->setParent(this);

  sound_tag_alt = new QLineEdit(this);
  connect(sound_tag_alt, SIGNAL(returnPressed()), this, SLOT(sound_tag_alt_given()));

  impr_layout->addWidget(label, row, 0);
  impr_layout->addWidget(sound_tag_alt, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Second alt sound tag")));
  label->setParent(this);

  sound_tag_alt2 = new QLineEdit(this);
  connect(sound_tag_alt2, SIGNAL(returnPressed()), this, SLOT(sound_tag_alt2_given()));

  impr_layout->addWidget(label, row, 0);
  impr_layout->addWidget(sound_tag_alt2, row++, 1);

  button = new QPushButton(QString::fromUtf8(R__("Helptext")), this);
  connect(button, SIGNAL(pressed()), this, SLOT(helptext()));
  impr_layout->addWidget(button, row++, 1);

  rowcount = 0;
  column = 0;
  for (int i = 0; i < IF_COUNT; i++) {
    enum impr_flag_id flag = (enum impr_flag_id)i;
    QCheckBox *check = new QCheckBox();

    label = new QLabel(impr_flag_id_name(flag));
    flag_layout->addWidget(label, rowcount, column + 1);

    check->setChecked(BV_ISSET(impr->flags, flag));
    flag_layout->addWidget(check, rowcount, column);

    if (++rowcount >= FLAGROWS) {
      column += 2;
      rowcount = 0;
    }
  }

  refresh();

  main_layout->addLayout(impr_layout);
  main_layout->addLayout(flag_layout);

  setLayout(main_layout);
}

/**********************************************************************//**
  User is closing dialog.
**************************************************************************/
void edit_impr::closeEvent(QCloseEvent *cevent)
{
  int rowcount;
  int column;

  close_help();

  // Save values from text fields.
  gfx_tag_given();
  gfx_tag_alt_given();
  gfx_tag_alt2_given();
  sound_tag_given();
  sound_tag_alt_given();
  sound_tag_alt2_given();

  BV_CLR_ALL(impr->flags);
  rowcount = 0;
  column = 0;
  for (int i = 0; i < IF_COUNT; i++) {
    QCheckBox *check = static_cast<QCheckBox *>(flag_layout->itemAtPosition(rowcount, column)->widget());

    if (check->isChecked()) {
      BV_SET(impr->flags, i);
    }

    if (++rowcount >= FLAGROWS) {
      rowcount = 0;
      column += 2;
    }
  }

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
  gfx_tag->setText(impr->graphic_str);
  gfx_tag_alt->setText(impr->graphic_alt);
  gfx_tag_alt2->setText(impr->graphic_alt2);
  sound_tag->setText(impr->soundtag);
  sound_tag_alt->setText(impr->soundtag_alt);
  sound_tag_alt2->setText(impr->soundtag_alt2);
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

/**********************************************************************//**
  User entered new graphics tag.
**************************************************************************/
void edit_impr::gfx_tag_given()
{
  QByteArray tag_bytes = gfx_tag->text().toUtf8();

  sz_strlcpy(impr->graphic_str, tag_bytes);
}

/**********************************************************************//**
  User entered new alternative graphics tag.
**************************************************************************/
void edit_impr::gfx_tag_alt_given()
{
  QByteArray tag_bytes = gfx_tag_alt->text().toUtf8();

  sz_strlcpy(impr->graphic_alt, tag_bytes);
}

/**********************************************************************//**
  User entered new secondary alternative graphics tag.
**************************************************************************/
void edit_impr::gfx_tag_alt2_given()
{
  QByteArray tag_bytes = gfx_tag_alt2->text().toUtf8();

  sz_strlcpy(impr->graphic_alt2, tag_bytes);
}

/**********************************************************************//**
  User entered new sound tag.
**************************************************************************/
void edit_impr::sound_tag_given()
{
  QByteArray tag_bytes = sound_tag->text().toUtf8();

  sz_strlcpy(impr->soundtag, tag_bytes);
}

/**********************************************************************//**
  User entered new alternative sound tag.
**************************************************************************/
void edit_impr::sound_tag_alt_given()
{
  QByteArray tag_bytes = sound_tag_alt->text().toUtf8();

  sz_strlcpy(impr->soundtag_alt, tag_bytes);
}

/**********************************************************************//**
  User entered new second alternative sound tag.
**************************************************************************/
void edit_impr::sound_tag_alt2_given()
{
  QByteArray tag_bytes = sound_tag_alt2->text().toUtf8();

  sz_strlcpy(impr->soundtag_alt2, tag_bytes);
}

/**********************************************************************//**
  User pressed helptext button
**************************************************************************/
void edit_impr::helptext()
{
  open_help(&impr->helptext);
}
