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
#include "unittype.h"

// ruledit
#include "ruledit.h"
#include "ruledit_qt.h"
#include "validity.h"

#include "tab_unit.h"

/**************************************************************************
  Setup tab_building object
**************************************************************************/
tab_unit::tab_unit(ruledit_gui *ui_in) : QWidget()
{
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  QGridLayout *unit_layout = new QGridLayout();
  QLabel *label;
  QPushButton *add_button;
  QPushButton *delete_button;

  ui = ui_in;
  selected = 0;

  unit_list = new QListWidget(this);

  connect(unit_list, SIGNAL(itemSelectionChanged()), this, SLOT(select_unit()));
  main_layout->addWidget(unit_list);

  unit_layout->setSizeConstraint(QLayout::SetMaximumSize);

  label = new QLabel(R__("Name"));
  label->setParent(this);
  name = new QLineEdit(this);
  name->setText("None");
  connect(name, SIGNAL(returnPressed()), this, SLOT(name_given()));
  unit_layout->addWidget(label, 0, 0);
  unit_layout->addWidget(name, 0, 1);

  label = new QLabel(R__("Rule Name"));
  label->setParent(this);
  rname = new QLineEdit(this);
  rname->setText("None");
  connect(rname, SIGNAL(returnPressed()), this, SLOT(name_given()));
  unit_layout->addWidget(label, 1, 0);
  unit_layout->addWidget(rname, 1, 1);

  add_button = new QPushButton(R__("Add Unit"), this);
  connect(add_button, SIGNAL(pressed()), this, SLOT(add_now()));
  unit_layout->addWidget(add_button, 5, 0);
  show_experimental(add_button);

  delete_button = new QPushButton(R__("Remove this Unit"), this);
  connect(delete_button, SIGNAL(pressed()), this, SLOT(delete_now()));
  unit_layout->addWidget(delete_button, 5, 1);
  show_experimental(delete_button);

  refresh();

  main_layout->addLayout(unit_layout);

  setLayout(main_layout);
}

/**************************************************************************
  Refresh the information.
**************************************************************************/
void tab_unit::refresh()
{
  unit_list->clear();

  unit_type_iterate(ptype) {
    QListWidgetItem *item = new QListWidgetItem(utype_rule_name(ptype));

    unit_list->insertItem(utype_index(ptype), item);
  } unit_type_iterate_end;
}

/**************************************************************************
  Update info of the unit
**************************************************************************/
void tab_unit::update_unit_info(struct unit_type *ptype)
{
  selected = ptype;

  if (selected != 0) {
    name->setText(untranslated_name(&(ptype->name)));
    rname->setText(utype_rule_name(ptype));
  } else {
    name->setText("None");
    rname->setText("None");
  }
}

/**************************************************************************
  User selected unit from the list.
**************************************************************************/
void tab_unit::select_unit()
{
  QList<QListWidgetItem *> select_list = unit_list->selectedItems();

  if (!select_list.isEmpty()) {
    update_unit_info(unit_type_by_rule_name(select_list.at(0)->text().toUtf8().data()));
  }
}

/**************************************************************************
  User entered name for the unit
**************************************************************************/
void tab_unit::name_given()
{
  if (selected != nullptr) {
    names_set(&(selected->name), 0,
              name->text().toUtf8().data(),
              rname->text().toUtf8().data());
    refresh();
  }
}

/**************************************************************************
  User requested unit deletion 
**************************************************************************/
void tab_unit::delete_now()
{
#if 0
  ui->clear_required(advance_rule_name(selected));
  if (is_tech_needed(selected, &ruledit_qt_display_requirers)) {
    return;
  }

  selected->require[AR_ONE] = A_NEVER;
#endif

  refresh();
  update_unit_info(0);
}

/**************************************************************************
  Initialize new tech for use.
**************************************************************************/
void tab_unit::initialize_new_unit(struct unit_type *ptype)
{
  name_set(&(ptype->name), "None", "None");
}

/**************************************************************************
  User requested new unit
**************************************************************************/
void tab_unit::add_now()
{
#if 0
  struct advance *new_adv;

  /* Try to reuse freed tech slot */
  advance_iterate(A_FIRST, padv) {
    if (padv->require[AR_ONE] == A_NEVER) {
      initialize_new_tech(padv);
      update_tech_info(padv);
      refresh();
      return;
    }
  } advance_iterate_end;

  /* Try to add completely new tech */
  if (game.control.num_tech_types >= A_LAST) {
    return;
  }

  new_adv = advance_by_number(game.control.num_tech_types++);
  initialize_new_bldg(new_adv);
  update_tech_info(new_adv);
#endif

  refresh();
}
