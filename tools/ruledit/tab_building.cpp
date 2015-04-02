/********************************************************************** 
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
#include "ruledit.h"
#include "ruledit_qt.h"
#include "validity.h"

#include "tab_building.h"

/**************************************************************************
  Setup tab_building object
**************************************************************************/
tab_building::tab_building(ruledit_gui *ui_in) : QWidget()
{
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  QGridLayout *bldg_layout = new QGridLayout();
  QLabel *label;
  QPushButton *add_button;
  QPushButton *delete_button;

  ui = ui_in;
  selected = 0;

  bldg_list = new QListWidget(this);

  connect(bldg_list, SIGNAL(itemSelectionChanged()), this, SLOT(select_bldg()));
  main_layout->addWidget(bldg_list);

  bldg_layout->setSizeConstraint(QLayout::SetMaximumSize);

  label = new QLabel(QString::fromUtf8(R__("Name")));
  label->setParent(this);
  name = new QLineEdit(this);
  name->setText("None");
  connect(name, SIGNAL(returnPressed()), this, SLOT(name_given()));
  bldg_layout->addWidget(label, 0, 0);
  bldg_layout->addWidget(name, 0, 1);

  label = new QLabel(QString::fromUtf8(R__("Rule Name")));
  label->setParent(this);
  rname = new QLineEdit(this);
  rname->setText("None");
  connect(rname, SIGNAL(returnPressed()), this, SLOT(name_given()));
  bldg_layout->addWidget(label, 1, 0);
  bldg_layout->addWidget(rname, 1, 1);

  add_button = new QPushButton(QString::fromUtf8(R__("Add Building")), this);
  connect(add_button, SIGNAL(pressed()), this, SLOT(add_now2()));
  bldg_layout->addWidget(add_button, 5, 0);
  show_experimental(add_button);

  delete_button = new QPushButton(QString::fromUtf8(R__("Remove this Building")), this);
  connect(delete_button, SIGNAL(pressed()), this, SLOT(delete_now()));
  bldg_layout->addWidget(delete_button, 5, 1);
  show_experimental(delete_button);

  refresh();

  main_layout->addLayout(bldg_layout);

  setLayout(main_layout);
}

/**************************************************************************
  Refresh the information.
**************************************************************************/
void tab_building::refresh()
{
  bldg_list->clear();

  improvement_iterate(pimpr) {
    if (!pimpr->disabled) {
      QListWidgetItem *item = new QListWidgetItem(improvement_rule_name(pimpr));

      bldg_list->insertItem(improvement_index(pimpr), item);
    }
  } improvement_iterate_end;
}

/**************************************************************************
  Update info of the building
**************************************************************************/
void tab_building::update_bldg_info(struct impr_type *pimpr)
{
  selected = pimpr;

  if (selected != 0) {
    name->setText(QString::fromUtf8(untranslated_name(&(pimpr->name))));
    rname->setText(QString::fromUtf8(improvement_rule_name(pimpr)));
  } else {
    name->setText("None");
    rname->setText("None");
  }
}

/**************************************************************************
  User selected building from the list.
**************************************************************************/
void tab_building::select_bldg()
{
  QList<QListWidgetItem *> select_list = bldg_list->selectedItems();

  if (!select_list.isEmpty()) {
    update_bldg_info(improvement_by_rule_name(select_list.at(0)->text().toUtf8().data()));
  }
}

/**************************************************************************
  User entered name for the building
**************************************************************************/
void tab_building::name_given()
{
  if (selected != nullptr) {
    improvement_iterate(pimpr) {
      if (pimpr != selected && !pimpr->disabled) {
        if (!strcmp(improvement_rule_name(pimpr), rname->text().toUtf8().data())) {
          ui->display_msg(R__("Building with that rule name already exist!"));
          return;
        }
      }
    } improvement_iterate_end;
    names_set(&(selected->name), 0,
              name->text().toUtf8().data(),
              rname->text().toUtf8().data());
    refresh();
  }
}

/**************************************************************************
  User requested building deletion 
**************************************************************************/
void tab_building::delete_now()
{
  requirers_dlg *requirers;

  requirers = ui->create_requirers(improvement_rule_name(selected));
  if (is_building_needed(selected, &ruledit_qt_display_requirers, requirers)) {
    return;
  }

  selected->disabled = true;

  refresh();
  update_bldg_info(0);
}

/**************************************************************************
  Initialize new tech for use.
**************************************************************************/
void tab_building::initialize_new_bldg(struct impr_type *pimpr)
{
  name_set(&(pimpr->name), "None", "None");
}

/**************************************************************************
  User requested new building
**************************************************************************/
void tab_building::add_now2()
{
  struct impr_type *new_bldg;

  /* Try to reuse freed building slot */
  improvement_iterate(pimpr) {
    if (pimpr->disabled) {
      pimpr->disabled = false;
      initialize_new_bldg(pimpr);
      update_bldg_info(pimpr);
      refresh();
      return;
    }
  } improvement_iterate_end;

  /* Try to add completely new building */
  if (game.control.num_impr_types >= B_LAST) {
    return;
  }

  new_bldg = improvement_by_number(game.control.num_impr_types++);
  initialize_new_bldg(new_bldg);
  update_bldg_info(new_bldg);

  refresh();
}
