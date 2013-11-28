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

// utility
#include "fcintl.h"
#include "log.h"
#include "registry.h"

// ruledit
#include "ruledit_qt.h"
#include "rulesave.h"

#include "tab_misc.h"

/**************************************************************************
  Setup tab_misc object
**************************************************************************/
tab_misc::tab_misc(QWidget *parent, ruledit_gui *ui_in) :
  QWidget(parent)
{
  QGridLayout *main_layout = new QGridLayout(this);
  QLabel *save_label;
  QLabel *name_label;

  ui = ui_in;

  main_layout->setSizeConstraint(QLayout::SetMaximumSize);

  name_label = new QLabel(R__("Ruleset name"));
  name_label->setParent(this);
  main_layout->addWidget(name_label, 0, 0);
  name = new QLineEdit(this);
  name->setText("");
  main_layout->addWidget(name, 0, 1);
  save_label = new QLabel(R__("Save to directory"));
  save_label->setParent(this);
  main_layout->addWidget(save_label, 1, 0);
  savedir = new QLineEdit(this);
  savedir->setText("ruledit-tmp");
  savedir->setFocus();
  connect(savedir, SIGNAL(returnPressed()), this, SLOT(savedir_given()));
  main_layout->addWidget(savedir, 1, 1);

  setLayout(main_layout);
}

/**************************************************************************
  User entered savedir
**************************************************************************/
void tab_misc::savedir_given()
{
  save_ruleset(savedir->text().toUtf8().data(), name->text().toUtf8().data());

  ui->display_msg(R__("Ruleset saved"));
}
