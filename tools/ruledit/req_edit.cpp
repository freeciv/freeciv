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
#include <QPushButton>

// utility
#include "fcintl.h"

// common
#include "requirements.h"

#include "req_edit.h"

/**************************************************************************
  Setup req_edit object
**************************************************************************/
req_edit::req_edit(ruledit_gui *ui_in, QString target,
                   const struct requirement_vector *preqs) : QDialog()
{
  QGridLayout *main_layout = new QGridLayout(this);
  QPushButton *close_button;
  int row = 0;

  ui = ui_in;

  area = new QTextEdit();
  area->setParent(this);
  area->setReadOnly(true);
  main_layout->addWidget(area, row++, 0);

  close_button = new QPushButton(QString::fromUtf8(R__("Close")), this);
  connect(close_button, SIGNAL(pressed()), this, SLOT(close_now()));
  main_layout->addWidget(close_button, row++, 0);

  setLayout(main_layout);
  setWindowTitle(target);

  requirement_vector_iterate(preqs, preq) {
    char buf[512];

    if (!preq->present) {
      continue;
    }

    universal_name_translation(&preq->source,
                               buf, sizeof(buf));
    add(buf);
  } requirement_vector_iterate_end;
}

/**************************************************************************
  Clear text area
**************************************************************************/
void req_edit::clear(const char *title)
{
  area->clear();
}

/**************************************************************************
  Add req entry
**************************************************************************/
void req_edit::add(const char *msg)
{
  area->append(QString::fromUtf8(msg));
}

/**************************************************************************
  User pushed close button
**************************************************************************/
void req_edit::close_now()
{
  done(0);
}
