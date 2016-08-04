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

// ruledit
#include "ruledit.h"

#include "req_edit.h"

/**************************************************************************
  Setup req_edit object
**************************************************************************/
req_edit::req_edit(ruledit_gui *ui_in, QString target,
                   struct requirement_vector *preqs) : QDialog()
{
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  QGridLayout *reqedit_layout = new QGridLayout();
  QPushButton *close_button;
  QPushButton *add_button;
  QPushButton *delete_button;

  ui = ui_in;
  selected = nullptr;
  req_vector = preqs;

  req_list = new QListWidget(this);

  connect(req_list, SIGNAL(itemSelectionChanged()), this, SLOT(select_req()));
  main_layout->addWidget(req_list);

  add_button = new QPushButton(QString::fromUtf8(R__("Add Requirement")), this);
  connect(add_button, SIGNAL(pressed()), this, SLOT(add_now()));
  reqedit_layout->addWidget(add_button, 0, 0);
  show_experimental(add_button);

  delete_button = new QPushButton(QString::fromUtf8(R__("Delete Requirement")), this);
  connect(delete_button, SIGNAL(pressed()), this, SLOT(delete_now()));
  reqedit_layout->addWidget(delete_button, 1, 0);
  show_experimental(delete_button);

  close_button = new QPushButton(QString::fromUtf8(R__("Close")), this);
  connect(close_button, SIGNAL(pressed()), this, SLOT(close_now()));
  reqedit_layout->addWidget(close_button, 2, 0);

  refresh();

  main_layout->addLayout(reqedit_layout);

  setLayout(main_layout);
  setWindowTitle(target);
}

/**************************************************************************
  Refresh the information.
**************************************************************************/
void req_edit::refresh()
{
  int i = 0;

  req_list->clear();

  requirement_vector_iterate(req_vector, preq) {
    if (preq->present) {
      char buf[512];
      QListWidgetItem *item;

      universal_name_translation(&preq->source, buf, sizeof(buf));
      item = new QListWidgetItem(QString::fromUtf8(buf));
      req_list->insertItem(i++, item);
    }
  } requirement_vector_iterate_end;
}

/**************************************************************************
  User pushed close button
**************************************************************************/
void req_edit::close_now()
{
  done(0);
}

/**************************************************************************
  User selected requirement from the list.
**************************************************************************/
void req_edit::select_req()
{
  int i = 0;

  requirement_vector_iterate(req_vector, preq) {
    if (req_list->item(i++)->isSelected()) {
      selected = preq;
      break;
    }
  } requirement_vector_iterate_end;
}

/**************************************************************************
  User requested new requirement
**************************************************************************/
void req_edit::add_now()
{
  struct requirement new_req;

  new_req = req_from_values(VUT_NONE, REQ_RANGE_LOCAL,
                            false, true, false, 0);

  requirement_vector_append(req_vector, new_req);

  refresh();
}

/**************************************************************************
  User requested requirement deletion 
**************************************************************************/
void req_edit::delete_now()
{
  if (selected != nullptr) {
    int end = requirement_vector_size(req_vector) - 1;
    struct requirement *last = requirement_vector_get(req_vector, end);

    requirement_vector_iterate(req_vector, new_req) {
      if (new_req == selected) {
        *new_req = *last;
        break;
      }
    } requirement_vector_iterate_end;

    requirement_vector_reserve(req_vector, end);

    refresh();
  }
}
