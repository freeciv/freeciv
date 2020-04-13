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
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QRadioButton>
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
  Setup tab_unit object
**************************************************************************/
tab_unit::tab_unit(ruledit_gui *ui_in) : QWidget()
{
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  QGridLayout *unit_layout = new QGridLayout();
  QLabel *label;

  ui = ui_in;
  selected = 0;

  unit_list = new QListWidget(this);

  connect(unit_list, SIGNAL(itemSelectionChanged()), this, SLOT(select_unit()));
  main_layout->addWidget(unit_list);

  unit_layout->setSizeConstraint(QLayout::SetMaximumSize);

  label = new QLabel(QString::fromUtf8(R__("Rule Name")));
  label->setParent(this);
  rname = new QLineEdit(this);
  rname->setText("None");
  connect(rname, SIGNAL(returnPressed()), this, SLOT(name_given()));
  unit_layout->addWidget(label, 0, 0);
  unit_layout->addWidget(rname, 0, 2);

  label = new QLabel(QString::fromUtf8(R__("Name")));
  label->setParent(this);
  same_name = new QRadioButton();
  connect(same_name, SIGNAL(toggled(bool)), this, SLOT(same_name_toggle(bool)));
  name = new QLineEdit(this);
  name->setText("None");
  connect(name, SIGNAL(returnPressed()), this, SLOT(name_given()));
  unit_layout->addWidget(label, 1, 0);
  unit_layout->addWidget(same_name, 1, 1);
  unit_layout->addWidget(name, 1, 2);

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
    if (!ptype->disabled) {
      QListWidgetItem *item = new QListWidgetItem(utype_rule_name(ptype));

      unit_list->insertItem(utype_index(ptype), item);
    }
  } unit_type_iterate_end;
}

/**************************************************************************
  Update info of the unit
**************************************************************************/
void tab_unit::update_utype_info(struct unit_type *ptype)
{
  selected = ptype;

  if (selected != nullptr) {
    QString dispn = QString::fromUtf8(untranslated_name(&(ptype->name)));
    QString rulen = QString::fromUtf8(utype_rule_name(ptype));

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
    name->setText("None");
    rname->setText("None");
    same_name->setChecked(true);
    name->setEnabled(false);
  }
}

/**************************************************************************
  User selected unit from the list.
**************************************************************************/
void tab_unit::select_unit()
{
  QList<QListWidgetItem *> select_list = unit_list->selectedItems();

  if (!select_list.isEmpty()) {
    QByteArray un_bytes;

    un_bytes = select_list.at(0)->text().toUtf8();
    update_utype_info(unit_type_by_rule_name(un_bytes.data()));
  }
}

/**************************************************************************
  User entered name for the unit
**************************************************************************/
void tab_unit::name_given()
{
  if (selected != nullptr) {
    QByteArray name_bytes;
    QByteArray rname_bytes;

    unit_type_iterate(ptype) {
      if (ptype != selected && !ptype->disabled) {
        rname_bytes = rname->text().toUtf8();
        if (!strcmp(utype_rule_name(ptype), rname_bytes.data())) {
          ui->display_msg(R__("A unit type with that rule name already "
                              "exists!"));
          return;
        }
      }
    } unit_type_iterate_end;

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

/**************************************************************************
  Initialize new tech for use.
**************************************************************************/
bool tab_unit::initialize_new_utype(struct unit_type *ptype)
{
  if (unit_type_by_rule_name("New Unit") != nullptr) {
    return false;
  }

  name_set(&(ptype->name), 0, "New Unit");
  return true;
}

/**************************************************************************
  Toggled whether rule_name and name should be kept identical
**************************************************************************/
void tab_unit::same_name_toggle(bool checked)
{
  name->setEnabled(!checked);
  if (checked) {
    name->setText(rname->text());
  }
}
