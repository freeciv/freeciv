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
#include "game.h"
#include "unittype.h"

// ruledit
#include "ruledit.h"
#include "ruledit_qt.h"
#include "tab_tech.h"

#include "edit_utype.h"

/**********************************************************************//**
  Setup edit_utype object
**************************************************************************/
edit_utype::edit_utype(ruledit_gui *ui_in, struct unit_type *utype_in) : values_dlg()
{
  QHBoxLayout *main_layout = new QHBoxLayout(this);
  QGridLayout *unit_layout = new QGridLayout();
  QLabel *label;
  QPushButton *button;
  QMenu *menu;
  int row = 0;
  int rowcount;
  int column;

  ui = ui_in;
  utype = utype_in;

  cargo_layout = new QGridLayout();
  flag_layout = new QGridLayout();

  setWindowTitle(QString::fromUtf8(utype_rule_name(utype)));

  label = new QLabel(QString::fromUtf8(R__("Class")));
  label->setParent(this);
  class_button = new QToolButton();
  class_button->setParent(this);
  class_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
  class_button->setPopupMode(QToolButton::MenuButtonPopup);
  menu = new QMenu();
  connect(menu, SIGNAL(triggered(QAction *)), this, SLOT(class_menu(QAction *)));

  unit_class_re_active_iterate(pclass) {
    menu->addAction(uclass_rule_name(pclass));
  } unit_class_re_active_iterate_end;

  class_button->setMenu(menu);

  unit_layout->addWidget(label, row, 0);
  unit_layout->addWidget(class_button, row++, 1);

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

  label = new QLabel(QString::fromUtf8(R__("Hitpoints")));
  label->setParent(this);

  hitpoints = new QSpinBox(this);
  hitpoints->setRange(1, 1000);
  connect(hitpoints, SIGNAL(valueChanged(int)), this, SLOT(set_hitpoints(int)));

  unit_layout->addWidget(label, row, 0);
  unit_layout->addWidget(hitpoints, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Firepower")));
  label->setParent(this);

  firepower = new QSpinBox(this);
  firepower->setRange(0, 200);
  connect(firepower, SIGNAL(valueChanged(int)), this, SLOT(set_firepower(int)));

  unit_layout->addWidget(label, row, 0);
  unit_layout->addWidget(firepower, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Move Rate")));
  label->setParent(this);

  move_rate = new QSpinBox(this);
  move_rate->setRange(0, 50);
  connect(move_rate, SIGNAL(valueChanged(int)), this, SLOT(set_move_rate(int)));

  unit_layout->addWidget(label, row, 0);
  unit_layout->addWidget(move_rate, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Transport Capacity")));
  label->setParent(this);

  cargo_capacity = new QSpinBox(this);
  cargo_capacity->setRange(0, 50);
  connect(cargo_capacity, SIGNAL(valueChanged(int)), this, SLOT(set_cargo_capacity(int)));

  unit_layout->addWidget(label, row, 0);
  unit_layout->addWidget(cargo_capacity, row++, 1);

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

  label = new QLabel(QString::fromUtf8(R__("Move sound tag")));
  label->setParent(this);

  sound_move_tag = new QLineEdit(this);
  connect(sound_move_tag, SIGNAL(returnPressed()), this,
          SLOT(sound_move_tag_given()));

  unit_layout->addWidget(label, row, 0);
  unit_layout->addWidget(sound_move_tag, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Alt move sound tag")));
  label->setParent(this);

  sound_move_tag_alt = new QLineEdit(this);
  connect(sound_move_tag_alt, SIGNAL(returnPressed()), this,
          SLOT(sound_move_tag_alt_given()));

  unit_layout->addWidget(label, row, 0);
  unit_layout->addWidget(sound_move_tag_alt, row++, 1);

    label = new QLabel(QString::fromUtf8(R__("Fight sound tag")));
  label->setParent(this);

  sound_fight_tag = new QLineEdit(this);
  connect(sound_fight_tag, SIGNAL(returnPressed()), this,
          SLOT(sound_fight_tag_given()));

  unit_layout->addWidget(label, row, 0);
  unit_layout->addWidget(sound_fight_tag, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Alt fight sound tag")));
  label->setParent(this);

  sound_fight_tag_alt = new QLineEdit(this);
  connect(sound_fight_tag_alt, SIGNAL(returnPressed()), this,
          SLOT(sound_fight_tag_alt_given()));

  unit_layout->addWidget(label, row, 0);
  unit_layout->addWidget(sound_fight_tag_alt, row++, 1);

  button = new QPushButton(QString::fromUtf8(R__("Helptext")), this);
  connect(button, SIGNAL(pressed()), this, SLOT(helptext()));
  unit_layout->addWidget(button, row++, 1);

  label = new QLabel(QString::fromUtf8(R__("Cargo")));
  cargo_layout->addWidget(label, 0, 0);
  for (int i = 0; i < game.control.num_unit_classes; i++) {
    QCheckBox *check = new QCheckBox();

    label = new QLabel(uclass_rule_name(uclass_by_number(i)));
    cargo_layout->addWidget(label, i + 1, 0);

    check->setChecked(BV_ISSET(utype->cargo, i));
    cargo_layout->addWidget(check, i + 1, 1);
  }

  rowcount = 0;
  column = 0;
  for (int i = 0; i < UTYF_LAST_USER_FLAG; i++) {
    enum unit_type_flag_id flag = (enum unit_type_flag_id)i;
    QCheckBox *check = new QCheckBox();

    label = new QLabel(unit_type_flag_id_name(flag));
    flag_layout->addWidget(label, rowcount, column + 1);

    check->setChecked(BV_ISSET(utype->flags, flag));
    flag_layout->addWidget(check, rowcount, column);

    if (++rowcount >= 25) {
      column += 2;
      rowcount = 0;
    }
  }

  refresh();

  main_layout->addLayout(unit_layout);
  main_layout->addLayout(cargo_layout);
  main_layout->addLayout(flag_layout);

  setLayout(main_layout);
}

/**********************************************************************//**
  User is closing dialog.
**************************************************************************/
void edit_utype::closeEvent(QCloseEvent *cevent)
{
  int rowcount;
  int column;

  close_help();

  // Save values from text fields.
  gfx_tag_given();
  gfx_tag_alt_given();
  gfx_tag_alt2_given();
  sound_move_tag_given();
  sound_move_tag_alt_given();
  sound_fight_tag_given();
  sound_fight_tag_alt_given();

  BV_CLR_ALL(utype->cargo);
  for (int i = 0; i < game.control.num_unit_classes; i++) {
    QCheckBox *check = static_cast<QCheckBox *>(cargo_layout->itemAtPosition(i + 1, 1)->widget());

    if (check->isChecked()) {
      BV_SET(utype->cargo, i);
    }
  }

  BV_CLR_ALL(utype->flags);
  rowcount = 0;
  column = 0;
  for (int i = 0; i < UTYF_LAST_USER_FLAG; i++) {
    QCheckBox *check = static_cast<QCheckBox *>(flag_layout->itemAtPosition(rowcount, column)->widget());

    if (check->isChecked()) {
      BV_SET(utype->flags, i);
    }

    if (++rowcount >= 25) {
      rowcount = 0;
      column += 2;
    }
  }

  utype->ruledit_dlg = nullptr;
}

/**********************************************************************//**
  Refresh the information.
**************************************************************************/
void edit_utype::refresh()
{
  class_button->setText(uclass_rule_name(utype->uclass));
  bcost->setValue(utype->build_cost);
  attack->setValue(utype->attack_strength);
  defense->setValue(utype->defense_strength);
  hitpoints->setValue(utype->hp);
  firepower->setValue(utype->firepower);
  move_rate->setValue(utype->move_rate);
  cargo_capacity->setValue(utype->transport_capacity);
  gfx_tag->setText(utype->graphic_str);
  gfx_tag_alt->setText(utype->graphic_alt);
  gfx_tag_alt2->setText(utype->graphic_alt2);
  sound_move_tag->setText(utype->sound_move);
  sound_move_tag_alt->setText(utype->sound_move_alt);
  sound_fight_tag->setText(utype->sound_fight);
  sound_fight_tag_alt->setText(utype->sound_fight_alt);
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
  Read hitpoints from spinbox
**************************************************************************/
void edit_utype::set_hitpoints(int value)
{
  utype->hp = value;
}

/**********************************************************************//**
  Read firepower from spinbox
**************************************************************************/
void edit_utype::set_firepower(int value)
{
  utype->firepower = value;
}

/**********************************************************************//**
  Read move rate from spinbox
**************************************************************************/
void edit_utype::set_move_rate(int value)
{
  utype->move_rate = value;
}

/**********************************************************************//**
  Read cargo capacity from spinbox
**************************************************************************/
void edit_utype::set_cargo_capacity(int value)
{
  utype->transport_capacity = value;
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

/**********************************************************************//**
  User entered new move sound tag.
**************************************************************************/
void edit_utype::sound_move_tag_given()
{
  QByteArray tag_bytes = sound_move_tag->text().toUtf8();

  sz_strlcpy(utype->sound_move, tag_bytes);
}

/**********************************************************************//**
  User entered new alternative move sound tag.
**************************************************************************/
void edit_utype::sound_move_tag_alt_given()
{
  QByteArray tag_bytes = sound_move_tag_alt->text().toUtf8();

  sz_strlcpy(utype->sound_move_alt, tag_bytes);
}

/**********************************************************************//**
  User entered new move fight tag.
**************************************************************************/
void edit_utype::sound_fight_tag_given()
{
  QByteArray tag_bytes = sound_fight_tag->text().toUtf8();

  sz_strlcpy(utype->sound_fight, tag_bytes);
}

/**********************************************************************//**
  User entered new alternative fight sound tag.
**************************************************************************/
void edit_utype::sound_fight_tag_alt_given()
{
  QByteArray tag_bytes = sound_fight_tag_alt->text().toUtf8();

  sz_strlcpy(utype->sound_fight_alt, tag_bytes);
}

/**********************************************************************//**
  User selected class
**************************************************************************/
void edit_utype::class_menu(QAction *action)
{
  QByteArray cn_bytes;
  struct unit_class *pclass;

  cn_bytes = action->text().toUtf8();
  pclass = unit_class_by_rule_name(cn_bytes.data());

  utype->uclass = pclass;

  refresh();
}

/**********************************************************************//**
  User pressed helptext button
**************************************************************************/
void edit_utype::helptext()
{
  open_help(&utype->helptext);
}
