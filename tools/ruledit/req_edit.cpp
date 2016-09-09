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
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>

// utility
#include "fcintl.h"

// common
#include "reqtext.h"
#include "requirements.h"

// ruledit
#include "ruledit.h"
#include "univ_value.h"

#include "req_edit.h"

/**************************************************************************
  Setup req_edit object
**************************************************************************/
req_edit::req_edit(ruledit_gui *ui_in, QString target,
                   struct requirement_vector *preqs) : QDialog()
{
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  QGridLayout *reqedit_layout = new QGridLayout();
  QHBoxLayout *active_layout = new QHBoxLayout();
  QPushButton *close_button;
  QPushButton *add_button;
  QPushButton *delete_button;
  QMenu *menu;
  QLabel *lbl;

  ui = ui_in;
  selected = nullptr;
  req_vector = preqs;

  req_list = new QListWidget(this);

  connect(req_list, SIGNAL(itemSelectionChanged()), this, SLOT(select_req()));
  main_layout->addWidget(req_list);

  lbl = new QLabel(R__("Type:"));
  active_layout->addWidget(lbl, 0, 0);
  edit_type_button = new QToolButton();
  menu = new QMenu();
  edit_type_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
  edit_type_button->setPopupMode(QToolButton::MenuButtonPopup);
  connect(menu, SIGNAL(triggered(QAction *)), this, SLOT(req_type_menu(QAction *)));
  edit_type_button->setMenu(menu);
  universals_iterate(univ_id) {
    struct universal dummy;

    dummy.kind = univ_id;
    if (universal_value_initial(&dummy)) {
      menu->addAction(universals_n_name(univ_id));
    }
  } universals_iterate_end;
  active_layout->addWidget(edit_type_button, 1, 0);

  lbl = new QLabel(R__("Value:"));
  active_layout->addWidget(lbl, 2, 0);
  edit_value_enum_button = new QToolButton();
  edit_value_enum_menu = new QMenu();
  edit_value_enum_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
  edit_value_enum_button->setPopupMode(QToolButton::MenuButtonPopup);
  connect(edit_value_enum_menu, SIGNAL(triggered(QAction *)),
          this, SLOT(univ_value_enum_menu(QAction *)));
  edit_value_enum_button->setMenu(edit_value_enum_menu);
  edit_value_enum_menu->setVisible(false);
  active_layout->addWidget(edit_value_enum_button, 3, 0);
  edit_value_nbr_field = new QLineEdit();
  edit_value_nbr_field->setVisible(false);
  connect(edit_value_nbr_field, SIGNAL(returnPressed()), this, SLOT(univ_value_edit()));
  active_layout->addWidget(edit_value_nbr_field, 4,0 );

  lbl = new QLabel(R__("Range:"));
  active_layout->addWidget(lbl, 5, 0);
  edit_range_button = new QToolButton();
  menu = new QMenu();
  edit_range_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
  edit_range_button->setPopupMode(QToolButton::MenuButtonPopup);
  connect(menu, SIGNAL(triggered(QAction *)), this, SLOT(req_range_menu(QAction *)));
  edit_range_button->setMenu(menu);
  req_range_iterate(range_id) {
    menu->addAction(req_range_name(range_id));
  } req_range_iterate_end;
  active_layout->addWidget(edit_range_button, 6, 0);

  main_layout->addLayout(active_layout);

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

      buf[0] = '\0';
      if (!req_text_insert(buf, sizeof(buf), NULL, preq, VERB_ACTUAL)) {
        universal_name_translation(&preq->source, buf, sizeof(buf));
      }
      item = new QListWidgetItem(QString::fromUtf8(buf));
      req_list->insertItem(i++, item);
      if (selected == preq) {
        item->setSelected(true);
      }
    }
  } requirement_vector_iterate_end;

  fill_active();
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
    QListWidgetItem *item = req_list->item(i++);

    if (item != nullptr && item->isSelected()) {
      selected = preq;
      fill_active();
      return;
    }
  } requirement_vector_iterate_end;
}

struct uvb_data
{
  QLineEdit *number;
  QToolButton *enum_button;
  QMenu *menu;
  struct universal *univ;
};

/**************************************************************************
  Callback for filling menu values
**************************************************************************/
static void universal_value_cb(const char *value, bool current, void *cbdata)
{
  struct uvb_data *data = (struct uvb_data *)cbdata;
  
  if (value == NULL) {
    int kind, val;

    universal_extraction(data->univ, &kind, &val);
    data->number->setText(QString::number(val));
    data->number->setVisible(true);
  } else {
    data->enum_button->setVisible(true);
    data->menu->addAction(value);
    if (current) {
      data->enum_button->setText(value);
    }
  }
}

/**************************************************************************
  Fill active menus from selected req.
**************************************************************************/
void req_edit::fill_active()
{
  if (selected != nullptr) {
    struct uvb_data data;

    edit_type_button->setText(universals_n_name(selected->source.kind));
    data.number = edit_value_nbr_field;
    data.enum_button = edit_value_enum_button;
    data.menu = edit_value_enum_menu;
    data.univ = &selected->source;
    edit_value_enum_menu->clear();
    edit_value_enum_button->setVisible(false);
    edit_value_nbr_field->setVisible(false);
    universal_kind_values(&selected->source, universal_value_cb, &data);
    edit_range_button->setText(req_range_name(selected->range));
  }
}

/**************************************************************************
  User selected type for the requirement.
**************************************************************************/
void req_edit::req_type_menu(QAction *action)
{
  enum universals_n univ = universals_n_by_name(action->text().toUtf8().data(),
                                                fc_strcasecmp);

  if (selected != nullptr) {
    selected->source.kind = univ;
    universal_value_initial(&selected->source);
  }

  refresh();
}

/**************************************************************************
  User selected range for the requirement.
**************************************************************************/
void req_edit::req_range_menu(QAction *action)
{
  enum req_range range = req_range_by_name(action->text().toUtf8().data(),
                                           fc_strcasecmp);

  if (selected != nullptr) {
    selected->range = range;
  }

  refresh();
}

/**************************************************************************
  User selected value for the requirement.
**************************************************************************/
void req_edit::univ_value_enum_menu(QAction *action)
{
  if (selected != nullptr) {
    universal_value_from_str(&selected->source, action->text().toUtf8().data());

    refresh();
  }
}

/**************************************************************************
  User entered numerical requirement value.
**************************************************************************/
void req_edit::univ_value_edit()
{
  if (selected != nullptr) {
    universal_value_from_str(&selected->source,
                             edit_value_nbr_field->text().toUtf8().data());

    refresh();
  }
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

    selected = nullptr;

    refresh();
  }
}
