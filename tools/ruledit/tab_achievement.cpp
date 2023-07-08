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
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>

// utility
#include "fcintl.h"
#include "log.h"

// common
#include "achievements.h"
#include "game.h"

// ruledit
#include "req_edit.h"
#include "ruledit.h"
#include "ruledit_qt.h"
#include "validity.h"

#include "tab_achievement.h"

/**********************************************************************//**
  Setup tab_achievement object
**************************************************************************/
tab_achievement::tab_achievement(ruledit_gui *ui_in) : QWidget()
{
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  QGridLayout *ach_layout = new QGridLayout();
  QLabel *label;
  QPushButton *effects_button;
  QPushButton *add_button;
  QPushButton *delete_button;

  ui = ui_in;
  selected = nullptr;

  ach_list = new QListWidget(this);

  connect(ach_list,
          SIGNAL(itemSelectionChanged()), this, SLOT(select_achievement()));
  main_layout->addWidget(ach_list);

  ach_layout->setSizeConstraint(QLayout::SetMaximumSize);

  label = new QLabel(QString::fromUtf8(R__("Rule Name")));
  label->setParent(this);
  rname = new QLineEdit(this);
  rname->setText(R__("None"));
  connect(rname, SIGNAL(returnPressed()), this, SLOT(name_given()));
  ach_layout->addWidget(label, 0, 0);
  ach_layout->addWidget(rname, 0, 2);

  label = new QLabel(QString::fromUtf8(R__("Name")));
  label->setParent(this);
  same_name = new QCheckBox();
  connect(same_name, SIGNAL(toggled(bool)), this, SLOT(same_name_toggle(bool)));
  name = new QLineEdit(this);
  name->setText(R__("None"));
  connect(name, SIGNAL(returnPressed()), this, SLOT(name_given()));
  ach_layout->addWidget(label, 1, 0);
  ach_layout->addWidget(same_name, 1, 1);
  ach_layout->addWidget(name, 1, 2);

  effects_button = new QPushButton(QString::fromUtf8(R__("Effects")), this);
  connect(effects_button, SIGNAL(pressed()), this, SLOT(edit_effects()));
  ach_layout->addWidget(effects_button, 3, 2);

  add_button = new QPushButton(QString::fromUtf8(R__("Add Achievement")), this);
  connect(add_button, SIGNAL(pressed()), this, SLOT(add_now()));
  ach_layout->addWidget(add_button, 4, 0);
  show_experimental(add_button);

  delete_button = new QPushButton(QString::fromUtf8(R__("Remove this Achievement")),
                                  this);
  connect(delete_button, SIGNAL(pressed()), this, SLOT(delete_now()));
  ach_layout->addWidget(delete_button, 4, 2);
  show_experimental(delete_button);

  refresh();
  update_achievement_info(nullptr);

  main_layout->addLayout(ach_layout);

  setLayout(main_layout);
}

/**********************************************************************//**
  Refresh the information.
**************************************************************************/
void tab_achievement::refresh()
{
  ach_list->clear();

  achievements_re_active_iterate(pach) {
    QListWidgetItem *item
      = new QListWidgetItem(QString::fromUtf8(achievement_rule_name(pach)));

    ach_list->insertItem(achievement_index(pach), item);
  } achievements_re_active_iterate_end;
}

/**********************************************************************//**
  Update info of the achievement
**************************************************************************/
void tab_achievement::update_achievement_info(struct achievement *pach)
{
  selected = pach;

  if (selected != 0) {
    QString dispn = QString::fromUtf8(untranslated_name(&(pach->name)));
    QString rulen = QString::fromUtf8(achievement_rule_name(pach));

    name->setText(dispn);
    rname->setText(rulen);
    if (dispn == rulen) {
      name->setEnabled(false);
      same_name->setChecked(true);
    } else {
      same_name->setChecked(false);
      name->setEnabled(true);
    }
  } else {
    name->setText(R__("None"));
    rname->setText(R__("None"));
    same_name->setChecked(true);
    name->setEnabled(false);
  }
}

/**********************************************************************//**
  User selected achievement from the list.
**************************************************************************/
void tab_achievement::select_achievement()
{
  QList<QListWidgetItem *> select_list = ach_list->selectedItems();

  if (!select_list.isEmpty()) {
    QByteArray gn_bytes;

    gn_bytes = select_list.at(0)->text().toUtf8();
    update_achievement_info(achievement_by_rule_name(gn_bytes.data()));
  }
}

/**********************************************************************//**
  User entered name for the achievement
**************************************************************************/
void tab_achievement::name_given()
{
  if (selected != nullptr) {
    QByteArray name_bytes;
    QByteArray rname_bytes;

    achievements_iterate(pach) {
      if (pach != selected && !pach->ruledit_disabled) {
        rname_bytes = rname->text().toUtf8();
        if (!strcmp(achievement_rule_name(pach), rname_bytes.data())) {
          ui->display_msg(R__("An achievement with that rule name already exists!"));
          return;
        }
      }
    } achievements_iterate_end;

    if (same_name->isChecked()) {
      name->setText(rname->text());
    }

    name_bytes = name->text().toUtf8();
    rname_bytes = rname->text().toUtf8();
    names_set(&(selected->name), 0,
              name_bytes.data(),
              rname_bytes.data());
    refresh();
  }
}

/**********************************************************************//**
  User requested achievement deletion
**************************************************************************/
void tab_achievement::delete_now()
{
  if (selected != 0) {
    requirers_dlg *requirers;

    requirers = ui->create_requirers(achievement_rule_name(selected));
    if (is_achievement_needed(selected, &ruledit_qt_display_requirers,
                              requirers)) {
      return;
    }

    selected->ruledit_disabled = true;

    refresh();
    update_achievement_info(nullptr);
  }
}

/**********************************************************************//**
  Initialize new achievement for use.
**************************************************************************/
bool tab_achievement::initialize_new_achievement(struct achievement *pach)
{
  if (achievement_by_rule_name("New Achievement") != nullptr) {
    return false;
  }

  name_set(&(pach->name), 0, "New Achievement");

  return true;
}

/**********************************************************************//**
  User requested new achievement
**************************************************************************/
void tab_achievement::add_now()
{
  struct achievement *new_ach;

  // Try to reuse freed achievement slot
  achievements_iterate(pach) {
    if (pach->ruledit_disabled) {
      if (initialize_new_achievement(pach)) {
        pach->ruledit_disabled = false;
        update_achievement_info(pach);
        refresh();
      }
      return;
    }
  } achievements_iterate_end;

  // Try to add completely new achievement
  if (game.control.num_achievement_types >= MAX_ACHIEVEMENT_TYPES) {
    return;
  }

  // num_achievement_types must be big enough to hold new achievement or
  // achievement_by_number() fails.
  game.control.num_achievement_types++;
  new_ach = achievement_by_number(game.control.num_achievement_types - 1);
  if (initialize_new_achievement(new_ach)) {
    update_achievement_info(new_ach);

    refresh();
  } else {
    game.control.num_achievement_types--; // Restore
  }
}

/**********************************************************************//**
  Toggled whether rule_name and name should be kept identical
**************************************************************************/
void tab_achievement::same_name_toggle(bool checked)
{
  name->setEnabled(!checked);
  if (checked) {
    name->setText(rname->text());
  }
}

/**********************************************************************//**
  User wants to edit effects
**************************************************************************/
void tab_achievement::edit_effects()
{
  if (selected != nullptr) {
    struct universal uni;

    uni.value.achievement = selected;
    uni.kind = VUT_ACHIEVEMENT;

    ui->open_effect_edit(QString::fromUtf8(achievement_rule_name(selected)),
                         &uni, EFMC_NORMAL);
  }
}
