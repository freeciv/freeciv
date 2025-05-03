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
#include <QCheckBox>
#include <QGridLayout>
#include <QLineEdit>
#include <QPushButton>

// utility
#include "fcintl.h"
#include "log.h"

// common
#include "counters.h"
#include "fc_types.h"
#include "game.h"

// ruledit
#include "ruledit.h"
#include "ruledit_qt.h"
#include "validity.h"

#include "tab_counters.h"

/**********************************************************************//**
  Setup tab_counter object
**************************************************************************/
tab_counter::tab_counter(ruledit_gui *ui_in) : QWidget()
{
#define empty_widget (nullptr)
/* We use const array of pointers and hopes compiler
 * will unroll loop
 */
#define widgets_row(...) { \
                           column = 0; \
                           QWidget * const wid[] = { __VA_ARGS__ }; \
                           for (unsigned int i = 0; \
                                i < sizeof(wid) / sizeof(wid[0]); i++) {\
                                  if (wid[i]) { \
                                    counter_layout->addWidget(wid[i], \
                                                              row, \
                                                              column);\
                                  }\
                                  column++;\
                                };\
                                row++;\
                          }

  QVBoxLayout *main_layout = new QVBoxLayout(this);
  QGridLayout *counter_layout = new QGridLayout();
  QLabel *label;
  QPushButton *add_button;
  QPushButton *delete_button;
  int row = 0;
  int column = 0;

  ui = ui_in;
  selected = nullptr;

  counter_list = new QListWidget(this);

  connect(counter_list, SIGNAL(itemSelectionChanged()), this, SLOT(select_counter()));
  main_layout->addWidget(counter_list);

  counter_layout->setSizeConstraint(QLayout::SetMaximumSize);

  label = new QLabel(QString::fromUtf8(R__("Rule Name")));
  label->setParent(this);
  rname = new QLineEdit(this);
  rname->setText(R__("None"));
  connect(rname, SIGNAL(returnPressed()), this, SLOT(name_given()));
  widgets_row(label, empty_widget, rname);

  label = new QLabel(QString::fromUtf8(R__("Name")));
  label->setParent(this);

  name = new QLineEdit(this);
  name->setText(R__("None"));
  connect(name, SIGNAL(returnPressed()), this, SLOT(name_given()));
  same_name = new QCheckBox(this);
  connect(same_name, SIGNAL(toggled(bool)), this, SLOT(same_name_toggle(bool)));
  same_name->setChecked(true);
  widgets_row(label, same_name, name);

  label = new QLabel(QString::fromUtf8(R__("Default Value")), this);
  label->setParent(this);
  def = new QSpinBox(this);
  def->setMaximum(INT_MAX);
  def->setMinimum(INT_MIN);
  connect(def, SIGNAL(valueChanged(int)), this, SLOT(default_given(int)));
  widgets_row(label, empty_widget, def);


  label = new QLabel(QString::fromUtf8(R__("Checkpoint")), this);
  label->setParent(this);
  checkpoint = new QSpinBox(this);
  checkpoint->setMaximum(INT_MAX);
  checkpoint->setMinimum(INT_MIN);
  connect(checkpoint, SIGNAL(valueChanged(int)), this, SLOT(checkpoint_given(int)));
  widgets_row(label, empty_widget, checkpoint);

  {
    enum counter_behavior current;
    type = new QComboBox(this);

    label = new QLabel(QString::fromUtf8(R__("Behavior")));
    label->setParent(this);

    for (current = counter_behavior_begin(); current > counter_behavior_end();
         current = counter_behavior_next(current)) {
      QVariant value(current);

      type->addItem(counter_behavior_name(current), value);
    };

    connect(type, SIGNAL(activated(int)), this, SLOT(counter_behavior_selected(int)));
    widgets_row(label, empty_widget, type);
  }

  effects_button = new QPushButton(QString::fromUtf8(R__("Effects")), this);
  connect(effects_button, SIGNAL(pressed()), this, SLOT(edit_effects()));
  widgets_row(empty_widget, empty_widget, effects_button);

  add_button = new QPushButton(QString::fromUtf8(R__("Add Counter")), this);
  connect(add_button, SIGNAL(pressed()), this, SLOT(add_now()));

  delete_button = new QPushButton(QString::fromUtf8(R__("Remove this Counter")), this);
  connect(delete_button, SIGNAL(pressed()), this, SLOT(delete_now()));

  widgets_row(empty_widget, empty_widget, add_button, delete_button);
  show_experimental(add_button);
  show_experimental(delete_button);

  refresh();
  update_counter_info(nullptr);

  main_layout->addLayout(counter_layout);

  setLayout(main_layout);
#undef empty_widget
#undef widgets_row
}

/**********************************************************************//**
  Called when counter behavior is set by user
**************************************************************************/
void tab_counter::counter_behavior_selected(int item)
{
  QVariant item_data;

  if (nullptr == selected) {

    return;
  }
  item_data = type->currentData();
  selected->type =  (enum counter_behavior) item_data.toInt();

  update_counter_info(selected);
  refresh();
}

/**********************************************************************//**
  Refresh the information.
**************************************************************************/
void tab_counter::refresh()
{
  counter_list->clear();

  counters_re_iterate(pcount) {
    QListWidgetItem *item = new QListWidgetItem(counter_rule_name(pcount));

    counter_list->insertItem(counter_index(pcount), item);
  } counters_re_iterate_end;
}

/**********************************************************************//**
  Update info of the counter
**************************************************************************/
void tab_counter::update_counter_info(struct counter *counter)
{
  selected = counter;

  if (selected != nullptr) {
    QString dispn = QString::fromUtf8(untranslated_name(&(counter->name)));
    QString rulen = QString::fromUtf8(rule_name_get(&(counter->name)));

    name->setText(dispn);
    rname->setText(rulen);

    if (dispn == rulen) {
      name->setEnabled(false);
    } else {
      name->setEnabled(true);
    }

    checkpoint->setValue(selected->checkpoint);
    def->setValue(selected->def);
  } else {
    name->setText(R__("None"));
    rname->setText(R__("None"));
    name->setEnabled(false);
  }
}

/**********************************************************************//**
  User selected counter from the list.
**************************************************************************/
void tab_counter::select_counter()
{
  char *cname;

  QList<QListWidgetItem *> select_list = counter_list->selectedItems();

  if (!select_list.isEmpty()) {
    QByteArray tn_bytes;

    tn_bytes = select_list.at(0)->text().toUtf8();

    cname = tn_bytes.data();
    update_counter_info(counter_by_rule_name(cname));
  }
}


/**********************************************************************//**
 User entered checkpoint value
**************************************************************************/
void tab_counter::checkpoint_given(int val)
{
  if (nullptr != selected) {

    selected->checkpoint = val;
  }
}

/**********************************************************************//**
 User entered default value
**************************************************************************/
void tab_counter::default_given(int val)
{
  if (nullptr != selected) {

    selected->def = val;
  }
}

/**********************************************************************//**
  User entered name for counter
**************************************************************************/
void tab_counter::name_given()
{
  if (selected != nullptr) {
    QByteArray name_bytes;
    QByteArray rname_bytes;

    city_counters_iterate(pcounter) {
      if (pcounter != selected) {
        rname_bytes = rname->text().toUtf8();
        if (!strcmp(counter_rule_name(pcounter), rname_bytes.data())) {
          ui->display_msg(R__("A counter with that rule name already exists!"));
          return;
        }
      }
    } city_counters_iterate_end;

    name_bytes = name->text().toUtf8();
    rname_bytes = rname->text().toUtf8();
    names_set(&(selected->name), 0,
              name_bytes.data(),
              rname_bytes.data());
    refresh();
  }
}

/**********************************************************************//**
  User requested counter deletion
**************************************************************************/
void tab_counter::delete_now()
{

  if (nullptr != selected) {
    requirers_dlg *requirers;

    requirers = ui->create_requirers(counter_rule_name(selected));
    if (is_counter_needed(selected, &ruledit_qt_display_requirers, requirers)) {
      return;
    }

    selected->ruledit_disabled = true;

    refresh();
    update_counter_info(nullptr);
  }
}

/**********************************************************************//**
  Initialize new counter for use.
**************************************************************************/
bool tab_counter::initialize_new_counter(struct counter *counter)
{
  counter->checkpoint = 0;
  counter->def = 0;
  counter->target = CTGT_CITY;
  counter->type = CB_CITY_OWNED_TURNS;
  counter->index = counters_get_city_counters_count() - 1;
  counter->ruledit_disabled = FALSE;

  name_set(&counter->name, 0, "New counter");
  if (counter->helptext != nullptr) {
    strvec_clear(counter->helptext);
  }

  return true;
}

/**********************************************************************//**
  User requested new counter
**************************************************************************/
void tab_counter::add_now()
{
   struct counter *new_counter;

  // Try to reuse freed counter slot
  city_counters_iterate(pcount) {
    if (pcount->type == COUNTER_BEHAVIOR_LAST
        || pcount->ruledit_disabled) {
      if (initialize_new_counter(pcount)) {
        update_counter_info(pcount);
        refresh();
      }
      return;
    }
  } city_counters_iterate_end;

  // Try to add completely new counter
  if (counters_get_city_counters_count() >= MAX_COUNTERS) {
    return;
  }

  // num_counter_types must be big enough to hold new counter or
  // counter_by_number() fails.
  game.control.num_counters++;
  new_counter = counter_by_id(counters_get_city_counters_count());
  if (initialize_new_counter(new_counter)) {
    update_counter_info(new_counter);
    attach_city_counter(new_counter);
    refresh();
  }
}

/**********************************************************************//**
  Toggled whether rule_name and name should be kept identical
**************************************************************************/
void tab_counter::same_name_toggle(bool checked)
{
  name->setEnabled(!checked);
  if (checked) {
    name->setText(rname->text());
  }
}

/**********************************************************************//**
  User wants to edit effects
**************************************************************************/
void tab_counter::edit_effects()
{
  if (selected != nullptr) {
    struct universal uni;

    uni.value.counter = selected;
    uni.kind = VUT_COUNTER;

    ui->open_effect_edit(QString::fromUtf8(counter_rule_name(selected)),
                         &uni, EFMC_NORMAL);
  }
}
