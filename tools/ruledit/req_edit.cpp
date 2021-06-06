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
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>

// utility
#include "fcintl.h"

// common
#include "reqtext.h"
#include "requirements.h"

// ruledit
#include "ruledit.h"
#include "ruledit_qt.h"
#include "univ_value.h"

#include "req_edit.h"

/**********************************************************************//**
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
  connect(ui, SIGNAL(req_vec_may_have_changed(const requirement_vector *)),
          this, SLOT(incoming_req_vec_change(const requirement_vector *)));

  clear_selected();
  req_vector = preqs;

  req_list = new QListWidget(this);

  connect(req_list, SIGNAL(itemSelectionChanged()), this, SLOT(select_req()));
  main_layout->addWidget(req_list);

  lbl = new QLabel(R__("Type:"));
  active_layout->addWidget(lbl, 0);
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
  active_layout->addWidget(edit_type_button, 1);

  lbl = new QLabel(R__("Value:"));
  active_layout->addWidget(lbl, 2);
  edit_value_enum_button = new QToolButton();
  edit_value_enum_menu = new QMenu();
  edit_value_enum_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
  edit_value_enum_button->setPopupMode(QToolButton::MenuButtonPopup);
  connect(edit_value_enum_menu, SIGNAL(triggered(QAction *)),
          this, SLOT(univ_value_enum_menu(QAction *)));
  edit_value_enum_button->setMenu(edit_value_enum_menu);
  edit_value_enum_menu->setVisible(false);
  active_layout->addWidget(edit_value_enum_button, 3);
  edit_value_nbr_field = new QSpinBox();
  edit_value_nbr_field->setRange(-100000, 100000);
  edit_value_nbr_field->setVisible(false);
  connect(edit_value_nbr_field, SIGNAL(valueChanged(int)), this,
          SLOT(univ_value_edit(int)));
  active_layout->addWidget(edit_value_nbr_field, 4);

  lbl = new QLabel(R__("Range:"));
  active_layout->addWidget(lbl, 5);
  edit_range_button = new QToolButton();
  menu = new QMenu();
  edit_range_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
  edit_range_button->setPopupMode(QToolButton::MenuButtonPopup);
  connect(menu, SIGNAL(triggered(QAction *)), this, SLOT(req_range_menu(QAction *)));
  edit_range_button->setMenu(menu);
  req_range_iterate(range_id) {
    menu->addAction(req_range_name(range_id));
  } req_range_iterate_end;
  active_layout->addWidget(edit_range_button, 6);

  edit_present_button = new QToolButton();
  menu = new QMenu();
  edit_present_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
  edit_present_button->setPopupMode(QToolButton::MenuButtonPopup);
  connect(menu, SIGNAL(triggered(QAction *)), this, SLOT(req_present_menu(QAction *)));
  edit_present_button->setMenu(menu);
  menu->addAction(R__("Allows"));
  menu->addAction(R__("Prevents"));
  active_layout->addWidget(edit_present_button, 7);

  main_layout->addLayout(active_layout);

  add_button = new QPushButton(QString::fromUtf8(R__("Add Requirement")), this);
  connect(add_button, SIGNAL(pressed()), this, SLOT(add_now()));
  reqedit_layout->addWidget(add_button, 0, 0);

  delete_button = new QPushButton(QString::fromUtf8(R__("Delete Requirement")), this);
  connect(delete_button, SIGNAL(pressed()), this, SLOT(delete_now()));
  reqedit_layout->addWidget(delete_button, 1, 0);

  close_button = new QPushButton(QString::fromUtf8(R__("Close")), this);
  connect(close_button, SIGNAL(pressed()), this, SLOT(close_now()));
  reqedit_layout->addWidget(close_button, 2, 0);

  refresh();

  main_layout->addLayout(reqedit_layout);

  setLayout(main_layout);
  setWindowTitle(target);
}

/**********************************************************************//**
  Refresh information in list widget item.
**************************************************************************/
void req_edit::refresh_item(QListWidgetItem *item, struct requirement *preq)
{
  char buf[512];

  buf[0] = '\0';
  if (!req_text_insert(buf, sizeof(buf), NULL, preq, VERB_ACTUAL, "")) {
    if (preq->present) {
      universal_name_translation(&preq->source, buf, sizeof(buf));
    } else {
      char buf2[256];

      universal_name_translation(&preq->source, buf2, sizeof(buf2));
      fc_snprintf(buf, sizeof(buf), R__("%s prevents"), buf2);
    }
  }

  item->setText(QString::fromUtf8(buf));
}

/**********************************************************************//**
  Refresh the row of currently selected item.
**************************************************************************/
void req_edit::refresh_selected()
{
  if (selected != nullptr) {
    int i;
    QListWidgetItem *item;

    for (i = 0; (item = req_list->item(i)) != nullptr; i++) {
      if (item->isSelected()) {
        refresh_item(item, selected);
      }
    }
  }
}

/**********************************************************************//**
  Refresh the information.
**************************************************************************/
void req_edit::refresh()
{
  int i = 0;

  req_list->clear();

  requirement_vector_iterate(req_vector, preq) {
    QListWidgetItem *item = new QListWidgetItem();

    refresh_item(item, preq);

    req_list->insertItem(i++, item);
    if (selected == preq) {
      item->setSelected(true);
    }
  } requirement_vector_iterate_end;

  fill_active();
}

/**********************************************************************//**
  The selected requirement has changed.
**************************************************************************/
void req_edit::update_selected()
{
  if (selected != nullptr) {
    selected_values = *selected;
  }
}

/**********************************************************************//**
  Unselect the currently selected requirement.
**************************************************************************/
void req_edit::clear_selected()
{
  selected = nullptr;

  selected_values.source.kind = VUT_NONE;
  selected_values.source.value.advance = NULL;
  selected_values.range = REQ_RANGE_LOCAL;
  selected_values.present = true;
  selected_values.survives = false;
  selected_values.quiet = false;
}

/**********************************************************************//**
  User pushed close button
**************************************************************************/
void req_edit::close_now()
{
  ui->unregister_req_edit(this);
  done(0);
}

/**********************************************************************//**
  User selected requirement from the list.
**************************************************************************/
void req_edit::select_req()
{
  int i = 0;

  requirement_vector_iterate(req_vector, preq) {
    QListWidgetItem *item = req_list->item(i++);

    if (item != nullptr && item->isSelected()) {
      selected = preq;
      update_selected();
      fill_active();
      return;
    }
  } requirement_vector_iterate_end;
}

struct uvb_data
{
  QSpinBox *number;
  QToolButton *enum_button;
  QMenu *menu;
  struct universal *univ;
};

/**********************************************************************//**
  Callback for filling menu values
**************************************************************************/
static void universal_value_cb(const char *value, bool current, void *cbdata)
{
  struct uvb_data *data = (struct uvb_data *)cbdata;
  
  if (value == NULL) {
    int kind, val;

    universal_extraction(data->univ, &kind, &val);
    data->number->setValue(val);
    data->number->setVisible(true);
  } else {
    data->enum_button->setVisible(true);
    data->menu->addAction(value);
    if (current) {
      data->enum_button->setText(value);
    }
  }
}

/**********************************************************************//**
  Fill active menus from selected req.
**************************************************************************/
void req_edit::fill_active()
{
  if (selected != nullptr) {
    struct uvb_data udata;

    edit_type_button->setText(universals_n_name(selected->source.kind));
    udata.number = edit_value_nbr_field;
    udata.enum_button = edit_value_enum_button;
    udata.menu = edit_value_enum_menu;
    udata.univ = &selected->source;
    edit_value_enum_menu->clear();
    edit_value_enum_button->setVisible(false);
    edit_value_nbr_field->setVisible(false);
    universal_kind_values(&selected->source, universal_value_cb, &udata);
    edit_range_button->setText(req_range_name(selected->range));
    if (selected->present) {
      edit_present_button->setText(R__("Allows"));
    } else {
      edit_present_button->setText(R__("Prevents"));
    }
  }
}

/**********************************************************************//**
  User selected type for the requirement.
**************************************************************************/
void req_edit::req_type_menu(QAction *action)
{
  QByteArray un_bytes = action->text().toUtf8();
  enum universals_n univ = universals_n_by_name(un_bytes.data(),
                                                fc_strcasecmp);

  if (selected != nullptr) {
    selected->source.kind = univ;
    universal_value_initial(&selected->source);
    update_selected();
  }

  refresh();
}

/**********************************************************************//**
  User selected range for the requirement.
**************************************************************************/
void req_edit::req_range_menu(QAction *action)
{
  QByteArray un_bytes = action->text().toUtf8();
  enum req_range range = req_range_by_name(un_bytes.data(),
                                           fc_strcasecmp);

  if (selected != nullptr) {
    selected->range = range;
    update_selected();
  }

  refresh();
}

/**********************************************************************//**
  User selected 'present' value for the requirement.
**************************************************************************/
void req_edit::req_present_menu(QAction *action)
{
  if (selected != nullptr) {
    /* FIXME: Should have more reliable implementation than checking
     *        against *translated* text */
    if (action->text() == R__("Prevents")) {
      selected->present = FALSE;
    } else {
      selected->present = TRUE;
    }
    update_selected();
  }

  refresh();
}

/**********************************************************************//**
  User selected value for the requirement.
**************************************************************************/
void req_edit::univ_value_enum_menu(QAction *action)
{
  if (selected != nullptr) {
    QByteArray un_bytes = action->text().toUtf8();

    universal_value_from_str(&selected->source, un_bytes.data());

    update_selected();
    refresh();
  }
}

/**********************************************************************//**
  User entered numerical requirement value.
**************************************************************************/
void req_edit::univ_value_edit(int value)
{
  if (selected != nullptr) {
    char buf[32];

    fc_snprintf(buf, sizeof(buf), "%d", value);

    universal_value_from_str(&selected->source,
                             buf);

    update_selected();

    /* No full refresh() with fill_selected()
     * as that would take focus out after every digit user types */
    refresh_selected();
  }
}

/**********************************************************************//**
  User requested new requirement
**************************************************************************/
void req_edit::add_now()
{
  struct requirement new_req;

  new_req = req_from_values(VUT_NONE, REQ_RANGE_LOCAL,
                            false, true, false, 0);

  requirement_vector_append(req_vector, new_req);

  refresh();

  emit req_vec_may_have_changed(req_vector);
}

/**********************************************************************//**
  User requested requirement deletion 
**************************************************************************/
void req_edit::delete_now()
{
  if (selected != nullptr) {
    size_t i;

    for (i = 0; i < requirement_vector_size(req_vector); i++) {
      if (requirement_vector_get(req_vector, i) == selected) {
        requirement_vector_remove(req_vector, i);
        break;
      }
    }

    clear_selected();

    refresh();

    emit req_vec_may_have_changed(req_vector);
  }
}

/**********************************************************************//**
  The requirement vector may have been changed.
  @param vec the requirement vector that may have been changed.
**************************************************************************/
void req_edit::incoming_req_vec_change(const requirement_vector *vec)
{
  if (req_vector == vec) {
    /* The selected requirement may be gone */

    selected = nullptr;
    requirement_vector_iterate(req_vector, preq) {
      if (are_requirements_equal(preq, &selected_values)) {
        selected = preq;
        break;
      }
    } requirement_vector_iterate_end;

    if (selected == nullptr) {
      /* The currently selected requirement was deleted. */
      clear_selected();
    }

    refresh();
  }
}

/**********************************************************************//**
  User clicked windows close button.
**************************************************************************/
void req_edit::closeEvent(QCloseEvent *event)
{
  ui->unregister_req_edit(this);
}
