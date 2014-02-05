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
#include <QPushButton>

// utility
#include "fcintl.h"
#include "log.h"
#include "registry.h"

// common
#include "game.h"

// ruledit
#include "ruledit_qt.h"
#include "rulesave.h"

#include "tab_misc.h"

/**************************************************************************
  Setup tab_misc object
**************************************************************************/
tab_misc::tab_misc(ruledit_gui *ui_in) : QWidget()
{
  QGridLayout *main_layout = new QGridLayout(this);
  QLabel *save_label;
  QLabel *name_label;
  QPushButton *save_button;

  ui = ui_in;

  main_layout->setSizeConstraint(QLayout::SetMaximumSize);

  name_label = new QLabel(R__("Ruleset name"));
  name_label->setParent(this);
  main_layout->addWidget(name_label, 0, 0);
  name = new QLineEdit(this);
  main_layout->addWidget(name, 0, 1);
  save_label = new QLabel(R__("Save to directory"));
  save_label->setParent(this);
  main_layout->addWidget(save_label, 1, 0);
  savedir = new QLineEdit(this);
  savedir->setText("ruledit-tmp");
  savedir->setFocus();
  main_layout->addWidget(savedir, 1, 1);
  save_button = new QPushButton(R__("Save now"), this);
  connect(save_button, SIGNAL(pressed()), this, SLOT(save_now()));
  main_layout->addWidget(save_button, 2, 0);

  refresh();

  setLayout(main_layout);
}

/**************************************************************************
  Refresh the information.
**************************************************************************/
void tab_misc::refresh()
{
  name->setText(game.control.name);
}

/**************************************************************************
  User entered savedir
**************************************************************************/
void tab_misc::save_now()
{
  char nameUTF8[MAX_LEN_NAME];

  strncpy(nameUTF8, name->text().toUtf8().data(), sizeof(nameUTF8));

  if (nameUTF8 != NULL && nameUTF8[0] != '\0') {
    strncpy(game.control.name, nameUTF8, sizeof(game.control.name));
  }

  save_ruleset(savedir->text().toUtf8().data(), nameUTF8);

  ui->display_msg(R__("Ruleset saved"));
}
