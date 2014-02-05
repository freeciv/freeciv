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
#include <QListWidget>
#include <QMenu>
#include <QToolButton>

// utility
#include "fcintl.h"
#include "log.h"

// common
#include "tech.h"

// ruledit
#include "ruledit_qt.h"

#include "tab_tech.h"

/**************************************************************************
  Setup tab_tech object
**************************************************************************/
tab_tech::tab_tech(ruledit_gui *ui_in) : QWidget()
{
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  QGridLayout *tech_layout = new QGridLayout();
  QLabel *label;

  ui = ui_in;
  selected = NULL;

  tech_list = new QListWidget(this);

  connect(tech_list, SIGNAL(itemSelectionChanged()), this, SLOT(select_tech()));
  main_layout->addWidget(tech_list);

  tech_layout->setSizeConstraint(QLayout::SetMaximumSize);

  label = new QLabel(R__("Name"));
  label->setParent(this);
  name = new QLabel("None");
  name->setParent(this);
  tech_layout->addWidget(label, 0, 0);
  tech_layout->addWidget(name, 0, 1);

  label = new QLabel(R__("Rule Name"));
  label->setParent(this);
  rname = new QLabel("None");
  rname->setParent(this);
  tech_layout->addWidget(label, 1, 0);
  tech_layout->addWidget(rname, 1, 1);

  label = new QLabel(R__("Req1"));
  label->setParent(this);
  req1_button = new QToolButton();
  req1_button->setParent(this);
  req1 = prepare_req_button(req1_button, AR_ONE);
  connect(req1_button, SIGNAL(pressed()), this, SLOT(req1_jump()));
  tech_layout->addWidget(label, 2, 0);
  tech_layout->addWidget(req1_button, 2, 1);

  label = new QLabel(R__("Req2"));
  label->setParent(this);
  req2_button = new QToolButton();
  req2 = prepare_req_button(req2_button, AR_TWO);
  connect(req2_button, SIGNAL(pressed()), this, SLOT(req2_jump()));
  tech_layout->addWidget(label, 3, 0);
  tech_layout->addWidget(req2_button, 3, 1);

  label = new QLabel(R__("Root Req"));
  label->setParent(this);
  root_req_button = new QToolButton();
  root_req_button->setParent(this);
  root_req = prepare_req_button(root_req_button, AR_ROOT);
  connect(root_req_button, SIGNAL(pressed()), this, SLOT(root_req_jump()));
  tech_layout->addWidget(label, 4, 0);
  tech_layout->addWidget(root_req_button, 4, 1);

  refresh();

  main_layout->addLayout(tech_layout);

  setLayout(main_layout);
}

/**************************************************************************
  Refresh the information.
**************************************************************************/
void tab_tech::refresh()
{
  tech_list->clear();

  advance_iterate(A_FIRST, padv) {
    QListWidgetItem *item = new QListWidgetItem(advance_rule_name(padv));

    tech_list->insertItem(advance_index(padv), item);
  } advance_iterate_end;

  techs_to_menu(req1);
  techs_to_menu(req2);
  techs_to_menu(root_req);
}

/**************************************************************************
  Build tech req button
**************************************************************************/
QMenu *tab_tech::prepare_req_button(QToolButton *button, enum tech_req rn)
{
  QMenu *menu = new QMenu();

  button->setToolButtonStyle(Qt::ToolButtonTextOnly);
  button->setPopupMode(QToolButton::MenuButtonPopup);

  switch (rn) {
  case AR_ONE:
    connect(menu, SIGNAL(triggered(QAction *)), this, SLOT(req1_menu(QAction *)));
    break;
  case AR_TWO:
    connect(menu, SIGNAL(triggered(QAction *)), this, SLOT(req2_menu(QAction *)));
    break;
  case AR_ROOT:
    connect(menu, SIGNAL(triggered(QAction *)), this, SLOT(root_req_menu(QAction *)));
    break;
  case AR_SIZE:
    fc_assert(rn != AR_SIZE);
    break;
  }

  button->setMenu(menu);

  return menu;
}

/**************************************************************************
  Fill menu with all possible tech values
**************************************************************************/
void tab_tech::techs_to_menu(QMenu *fill_menu)
{
  fill_menu->clear();

  advance_iterate(A_NONE, padv) {
    fill_menu->addAction(tech_name(padv));
  } advance_iterate_end;
}

/**************************************************************************
  Display name of the tech
**************************************************************************/
QString tab_tech::tech_name(struct advance *padv)
{
  if (padv == A_NEVER) {
    return R__("Never");
  }

  return advance_rule_name(padv);
}

/**************************************************************************
  Update info of the tech
**************************************************************************/
void tab_tech::update_tech_info(struct advance *adv)
{
  selected = adv;

  name->setText(untranslated_name(&(adv->name)));
  rname->setText(rule_name(&(adv->name)));
  req1_button->setText(tech_name(adv->require[AR_ONE]));
  req2_button->setText(tech_name(adv->require[AR_TWO]));
  root_req_button->setText(tech_name(adv->require[AR_ROOT]));
}

/**************************************************************************
  User selected tech from the list.
**************************************************************************/
void tab_tech::select_tech()
{
  QList<QListWidgetItem *> select_list = tech_list->selectedItems();

  if (!select_list.isEmpty()) {
    update_tech_info(advance_by_rule_name(select_list.at(0)->text().toUtf8().data()));
  }
}

/**************************************************************************
  Req1 of the current tech selected.
**************************************************************************/
void tab_tech::req1_jump()
{
  if (selected != NULL && advance_number(selected->require[AR_ONE]) != A_NONE) {
    update_tech_info(selected->require[AR_ONE]);
  }
}

/**************************************************************************
  Req2 of the current tech selected.
**************************************************************************/
void tab_tech::req2_jump()
{
  if (selected != NULL && advance_number(selected->require[AR_TWO]) != A_NONE) {
    update_tech_info(selected->require[AR_TWO]);
  }
}

/**************************************************************************
  Root req of the current tech selected.
**************************************************************************/
void tab_tech::root_req_jump()
{
  if (selected != NULL && advance_number(selected->require[AR_ROOT]) != A_NONE) {
    update_tech_info(selected->require[AR_ROOT]);
  }
}

void tab_tech::req1_menu(QAction *action)
{
  struct advance *padv = advance_by_rule_name(action->text().toUtf8().data());

  if (padv != NULL) {
    selected->require[AR_ONE] = padv;

    update_tech_info(selected);
  }
}

void tab_tech::req2_menu(QAction *action)
{
  struct advance *padv = advance_by_rule_name(action->text().toUtf8().data());

  if (padv != NULL) {
    selected->require[AR_TWO] = padv;

    update_tech_info(selected);
  }
}

void tab_tech::root_req_menu(QAction *action)
{
  struct advance *padv = advance_by_rule_name(action->text().toUtf8().data());

  if (padv != NULL) {
    selected->require[AR_ROOT] = padv;

    update_tech_info(selected);
  }
}
