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
#include <QMessageBox>
#include <QPushButton>

// utility
#include "fcintl.h"
#include "log.h"
#include "registry.h"

// common
#include "game.h"

// server
#include "rssanity.h"

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
  QLabel *version_label;
  QPushButton *save_button;
  int row = 0;

  ui = ui_in;

  main_layout->setSizeConstraint(QLayout::SetMaximumSize);

  name_label = new QLabel(R__("Ruleset name"));
  name_label->setParent(this);
  main_layout->addWidget(name_label, row, 0);
  name = new QLineEdit(this);
  main_layout->addWidget(name, row++, 1);
  version_label = new QLabel(R__("Ruleset version"));
  version_label->setParent(this);
  main_layout->addWidget(version_label, row, 0);
  version = new QLineEdit(this);
  main_layout->addWidget(version, row++, 1);
  save_label = new QLabel(R__("Save to directory"));
  save_label->setParent(this);
  main_layout->addWidget(save_label, row, 0);
  savedir = new QLineEdit(this);
  savedir->setText("ruledit-tmp");
  savedir->setFocus();
  main_layout->addWidget(savedir, row++, 1);
  save_button = new QPushButton(R__("Save now"), this);
  connect(save_button, SIGNAL(pressed()), this, SLOT(save_now()));
  main_layout->addWidget(save_button, row, 0);

  refresh();

  setLayout(main_layout);
}

/**************************************************************************
  Refresh the information.
**************************************************************************/
void tab_misc::refresh()
{
  name->setText(game.control.name);
  version->setText(game.control.version);
}

/**************************************************************************
  User entered savedir
**************************************************************************/
void tab_misc::save_now()
{
  char nameUTF8[MAX_LEN_NAME];

  ui->flush_widgets();

  strncpy(nameUTF8, name->text().toUtf8().data(), sizeof(nameUTF8));

  if (nameUTF8[0] != '\0') {
    strncpy(game.control.name, nameUTF8, sizeof(game.control.name));
  }

  strncpy(game.control.version, version->text().toUtf8().data(),
          sizeof(game.control.version));

  if (!sanity_check_ruleset_data()) {
    QMessageBox *box = new QMessageBox();

    box->setText("Current data fails sanity checks. Save anyway?");
    box->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box->exec();

    if (box->result() != QMessageBox::Yes) {
      return;
    }
  }

  save_ruleset(savedir->text().toUtf8().data(), nameUTF8,
               &(ui->data));

  ui->display_msg(R__("Ruleset saved"));
}
