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
tab_tech::tab_tech(QWidget *parent, ruledit_gui *ui_in) :
  QWidget(parent)
{
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  QGridLayout *tech_layout = new QGridLayout();
  QLabel *label;

  ui = ui_in;

  tech_list = new QListWidget(this);

  advance_iterate(A_FIRST, padv) {
    QListWidgetItem *item = new QListWidgetItem(advance_rule_name(padv));

    tech_list->insertItem(advance_index(padv), item);
  } advance_iterate_end;

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
  req1 = new QLabel("None");
  req1->setParent(this);
  tech_layout->addWidget(label, 2, 0);
  tech_layout->addWidget(req1, 2, 1);

  label = new QLabel(R__("Req2"));
  label->setParent(this);
  req2 = new QLabel("None");
  req2->setParent(this);
  tech_layout->addWidget(label, 3, 0);
  tech_layout->addWidget(req2, 3, 1);

  label = new QLabel(R__("Root Req"));
  label->setParent(this);
  root_req = new QLabel("None");
  root_req->setParent(this);
  tech_layout->addWidget(label, 4, 0);
  tech_layout->addWidget(root_req, 4, 1);

  main_layout->addLayout(tech_layout);

  setLayout(main_layout);
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
  name->setText(untranslated_name(&(adv->name)));
  rname->setText(rule_name(&(adv->name)));
  req1->setText(tech_name(adv->require[AR_ONE]));
  req2->setText(tech_name(adv->require[AR_TWO]));
  root_req->setText(tech_name(adv->require[AR_ROOT]));
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
