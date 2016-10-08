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
#include "government.h"

// ruledit
#include "req_edit.h"
#include "ruledit.h"
#include "ruledit_qt.h"
#include "validity.h"

#include "tab_enablers.h"

/**************************************************************************
  Setup tab_enabler object
**************************************************************************/
tab_enabler::tab_enabler(ruledit_gui *ui_in) : QWidget()
{
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  QGridLayout *enabler_layout = new QGridLayout();
  QLabel *label;
  QPushButton *add_button;
  QPushButton *delete_button;
  QPushButton *reqs_button;

  ui = ui_in;
  selected = 0;

  enabler_list = new QListWidget(this);

  connect(enabler_list, SIGNAL(itemSelectionChanged()), this, SLOT(select_enabler()));
  main_layout->addWidget(enabler_list);

  enabler_layout->setSizeConstraint(QLayout::SetMaximumSize);

  label = new QLabel(QString::fromUtf8(R__("Type")));
  label->setParent(this);
  enabler_layout->addWidget(label, 0, 0);
  type = new QLabel();
  enabler_layout->addWidget(type, 0, 2);

  reqs_button = new QPushButton(QString::fromUtf8(R__("Actor Requirements")), this);
  connect(reqs_button, SIGNAL(pressed()), this, SLOT(edit_actor_reqs()));
  enabler_layout->addWidget(reqs_button, 1, 2);
  show_experimental(reqs_button);

  reqs_button = new QPushButton(QString::fromUtf8(R__("Target Requirements")), this);
  connect(reqs_button, SIGNAL(pressed()), this, SLOT(edit_target_reqs()));
  enabler_layout->addWidget(reqs_button, 2, 2);
  show_experimental(reqs_button);

  add_button = new QPushButton(QString::fromUtf8(R__("Add Enabler")), this);
  connect(add_button, SIGNAL(pressed()), this, SLOT(add_now()));
  enabler_layout->addWidget(add_button, 3, 0);
  show_experimental(add_button);

  delete_button = new QPushButton(QString::fromUtf8(R__("Remove this Enabler")), this);
  connect(delete_button, SIGNAL(pressed()), this, SLOT(delete_now()));
  enabler_layout->addWidget(delete_button, 3, 2);
  show_experimental(delete_button);

  refresh();

  main_layout->addLayout(enabler_layout);

  setLayout(main_layout);
}

/**************************************************************************
  Refresh the information.
**************************************************************************/
void tab_enabler::refresh()
{
  int n = 0;

  enabler_list->clear();

  action_enablers_iterate(enabler) {
    if (!enabler->disabled) {
      char buffer[512];
      QListWidgetItem *item;

      fc_snprintf(buffer, sizeof(buffer), "#%d: %s", n,
                  action_rule_name(enabler_get_action(enabler)));

      item = new QListWidgetItem(QString::fromUtf8(buffer));

      enabler_list->insertItem(n++, item);
    }
  } action_enablers_iterate_end;
}

/**************************************************************************
  Update info of the enabler
**************************************************************************/
void tab_enabler::update_enabler_info(struct action_enabler *enabler)
{
  selected = enabler;

  if (selected != nullptr) {
    QString dispn = QString::fromUtf8(action_rule_name(enabler_get_action(enabler)));

    type->setText(dispn);
  } else {
    type->setText("None");
  }
}

/**************************************************************************
  User selected enabler from the list.
**************************************************************************/
void tab_enabler::select_enabler()
{
  int i = 0;

  action_enablers_iterate(enabler) {
    QListWidgetItem *item = enabler_list->item(i++);

    if (item != nullptr && item->isSelected()) {
      update_enabler_info(enabler);
    }
  } action_enablers_iterate_end;
}

/**************************************************************************
  User requested enabler deletion 
**************************************************************************/
void tab_enabler::delete_now()
{
  if (selected != nullptr) {
    selected->disabled = true;

    refresh();
    update_enabler_info(nullptr);
  }
}

/**************************************************************************
  Initialize new enabler for use.
**************************************************************************/
bool tab_enabler::initialize_new_enabler(struct action_enabler *enabler)
{
  return true;
}

/**************************************************************************
  User requested new enabler
**************************************************************************/
void tab_enabler::add_now()
{
  struct action_enabler *new_enabler;

  // Try to reuse freed enabler slot
  action_enablers_iterate(enabler) {
    if (enabler->disabled) {
      if (initialize_new_enabler(enabler)) {
        enabler->disabled = false;
        update_enabler_info(enabler);
        refresh();
      }
      return;
    }
  } action_enablers_iterate_end;

  // Try to add completely new enabler
  new_enabler = action_enabler_new();

  new_enabler->action = ACTION_MARKETPLACE;

  action_enabler_add(new_enabler);
}

/**************************************************************************
  User wants to edit target reqs
**************************************************************************/
void tab_enabler::edit_target_reqs()
{
  if (selected != nullptr) {
    req_edit *redit = new req_edit(ui, QString::fromUtf8(R__("Enabler (target)")),
                                   &selected->target_reqs);

    redit->show();
  }
}

/**************************************************************************
  User wants to edit actor reqs
**************************************************************************/
void tab_enabler::edit_actor_reqs()
{
  if (selected != nullptr) {
    req_edit *redit = new req_edit(ui, QString::fromUtf8(R__("Enabler (actor)")),
                                   &selected->actor_reqs);

    redit->show();
  }
}

