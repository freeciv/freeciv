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
#include "game.h"
#include "improvement.h"

// ruledit
#include "edit_impr.h"
#include "ruledit.h"
#include "ruledit_qt.h"
#include "validity.h"

#include "tab_building.h"

/**********************************************************************//**
  Setup tab_building object
**************************************************************************/
tab_building::tab_building(ruledit_gui *ui_in) : QWidget()
{
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  QGridLayout *bldg_layout = new QGridLayout();
  QLabel *label;
  QPushButton *add_button;
  QPushButton *delete_button;
  QPushButton *reqs_button;
  QPushButton *effects_button;
  QPushButton *edit_button;

  ui = ui_in;
  selected = 0;

  bldg_list = new QListWidget(this);

  connect(bldg_list, SIGNAL(itemSelectionChanged()), this, SLOT(select_bldg()));
  main_layout->addWidget(bldg_list);

  bldg_layout->setSizeConstraint(QLayout::SetMaximumSize);

  label = new QLabel(QString::fromUtf8(R__("Rule Name")));
  label->setParent(this);
  rname = new QLineEdit(this);
  rname->setText(R__("None"));
  connect(rname, SIGNAL(returnPressed()), this, SLOT(name_given()));
  bldg_layout->addWidget(label, 0, 0);
  bldg_layout->addWidget(rname, 0, 2);

  label = new QLabel(QString::fromUtf8(R__("Name")));
  label->setParent(this);
  same_name = new QCheckBox();
  connect(same_name, SIGNAL(toggled(bool)), this, SLOT(same_name_toggle(bool)));
  name = new QLineEdit(this);
  name->setText(R__("None"));
  connect(name, SIGNAL(returnPressed()), this, SLOT(name_given()));
  bldg_layout->addWidget(label, 1, 0);
  bldg_layout->addWidget(same_name, 1, 1);
  bldg_layout->addWidget(name, 1, 2);

  edit_button = new QPushButton(QString::fromUtf8(R__("Edit Values")), this);
  connect(edit_button, SIGNAL(pressed()), this, SLOT(edit_now()));
  bldg_layout->addWidget(edit_button, 2, 2);

  reqs_button = new QPushButton(QString::fromUtf8(R__("Requirements")), this);
  connect(reqs_button, SIGNAL(pressed()), this, SLOT(edit_reqs()));
  bldg_layout->addWidget(reqs_button, 3, 2);

  effects_button = new QPushButton(QString::fromUtf8(R__("Effects")), this);
  connect(effects_button, SIGNAL(pressed()), this, SLOT(edit_effects()));
  bldg_layout->addWidget(effects_button, 4, 2);

  add_button = new QPushButton(QString::fromUtf8(R__("Add Building")), this);
  connect(add_button, SIGNAL(pressed()), this, SLOT(add_now2()));
  bldg_layout->addWidget(add_button, 5, 0);

  delete_button = new QPushButton(QString::fromUtf8(R__("Remove this Building")), this);
  connect(delete_button, SIGNAL(pressed()), this, SLOT(delete_now()));
  bldg_layout->addWidget(delete_button, 5, 2);

  refresh();
  update_bldg_info(nullptr);

  main_layout->addLayout(bldg_layout);

  setLayout(main_layout);
}

/**********************************************************************//**
  Refresh the information.
**************************************************************************/
void tab_building::refresh()
{
  bldg_list->clear();

  improvement_re_active_iterate(pimpr) {
    QListWidgetItem *item = new QListWidgetItem(improvement_rule_name(pimpr));

    bldg_list->insertItem(improvement_index(pimpr), item);
  } improvement_re_active_iterate_end;
}

/**********************************************************************//**
  Update info of the building
**************************************************************************/
void tab_building::update_bldg_info(struct impr_type *pimpr)
{
  selected = pimpr;

  if (selected != 0) {
    QString dispn = QString::fromUtf8(untranslated_name(&(pimpr->name)));
    QString rulen = QString::fromUtf8(improvement_rule_name(pimpr));

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
  User selected building from the list.
**************************************************************************/
void tab_building::select_bldg()
{
  QList<QListWidgetItem *> select_list = bldg_list->selectedItems();

  if (!select_list.isEmpty()) {
    QByteArray bn_bytes;

    bn_bytes = select_list.at(0)->text().toUtf8();
    update_bldg_info(improvement_by_rule_name(bn_bytes.data()));
  }
}

/**********************************************************************//**
  User entered name for the building
**************************************************************************/
void tab_building::name_given()
{
  if (selected != nullptr) {
    QByteArray name_bytes;
    QByteArray rname_bytes;

    improvement_iterate(pimpr) {
      if (pimpr != selected && !pimpr->ruledit_disabled) {
        rname_bytes = rname->text().toUtf8();
        if (!strcmp(improvement_rule_name(pimpr), rname_bytes.data())) {
          ui->display_msg(R__("A building with that rule name already "
                              "exists!"));
          return;
        }
      }
    } improvement_iterate_end;

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
  User requested building deletion
**************************************************************************/
void tab_building::delete_now()
{
  if (selected != nullptr) {
    requirers_dlg *requirers;

    requirers = ui->create_requirers(improvement_rule_name(selected));
    if (is_building_needed(selected, &ruledit_qt_display_requirers, requirers)) {
      return;
    }

    selected->ruledit_disabled = true;

    if (selected->ruledit_dlg != nullptr) {
      ((edit_impr *)selected->ruledit_dlg)->done(0);
    }

    refresh();
    update_bldg_info(nullptr);
  }
}

/**********************************************************************//**
  Initialize new building for use.
**************************************************************************/
bool tab_building::initialize_new_bldg(struct impr_type *pimpr)
{
  if (improvement_by_rule_name("New Building") != nullptr) {
    return false;
  }

  name_set(&(pimpr->name), 0, "New Building");
  BV_CLR_ALL(pimpr->flags);
  if (pimpr->helptext != nullptr) {
    strvec_clear(pimpr->helptext);
  }

  return true;
}

/**********************************************************************//**
  User requested new building

  FIXME: get_req_source_effects() calls afterwards for the newly
         added building will lack effects that existed beforehand,
         and are provided via, e.g., BuildingFlag requirement.
**************************************************************************/
void tab_building::add_now2()
{
  struct impr_type *new_bldg;

  // Try to reuse freed building slot
  improvement_iterate(pimpr) {
    if (pimpr->ruledit_disabled) {
      if (initialize_new_bldg(pimpr)) {
        pimpr->ruledit_disabled = false;
        update_bldg_info(pimpr);
        refresh();
      }
      return;
    }
  } improvement_iterate_end;

  // Try to add completely new building
  if (game.control.num_impr_types >= B_LAST) {
    return;
  }

  // num_impr_types must be big enough to hold new building or
  // improvement_by_number() fails.
  game.control.num_impr_types++;
  new_bldg = improvement_by_number(game.control.num_impr_types - 1);
  if (initialize_new_bldg(new_bldg)) {
    update_bldg_info(new_bldg);

    refresh();
  } else {
    game.control.num_impr_types--; // Restore
  }
}

/**********************************************************************//**
  Toggled whether rule_name and name should be kept identical
**************************************************************************/
void tab_building::same_name_toggle(bool checked)
{
  name->setEnabled(!checked);
  if (checked) {
    name->setText(rname->text());
  }
}

/**********************************************************************//**
  User wants to edit reqs
**************************************************************************/
void tab_building::edit_reqs()
{
  if (selected != nullptr) {
    ui->open_req_edit(QString::fromUtf8(improvement_rule_name(selected)),
                      &selected->reqs);
  }
}

/**********************************************************************//**
  User wants to edit effects
**************************************************************************/
void tab_building::edit_effects()
{
  if (selected != nullptr) {
    struct universal uni;

    uni.value.building = selected;
    uni.kind = VUT_IMPROVEMENT;

    ui->open_effect_edit(QString::fromUtf8(improvement_rule_name(selected)),
                         &uni, EFMC_NORMAL);
  }
}

/**********************************************************************//**
  User requested building edit dialog
**************************************************************************/
void tab_building::edit_now()
{
  if (selected != nullptr) {
    if (selected->ruledit_dlg == nullptr) {
      edit_impr *edit = new edit_impr(ui, selected);

      edit->show();
      selected->ruledit_dlg = edit;
    } else {
      ((edit_impr *)selected->ruledit_dlg)->raise();
    }
  }
}
