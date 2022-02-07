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
#include <QSpinBox>

// utility
#include "fcintl.h"

// common
#include "effects.h"

// ruledit
#include "ruledit.h"
#include "ruledit_qt.h"
#include "validity.h"

#include "effect_edit.h"

#define NO_MULTIPLIER_NAME "No Multiplier"

/**********************************************************************//**
  Setup effect_edit object
**************************************************************************/
effect_edit::effect_edit(ruledit_gui *ui_in, QString target,
                         struct universal *filter_in,
                         enum effect_filter_main_class efmc_in) : QDialog()
{
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  QGridLayout *effect_edit_layout = new QGridLayout();
  QHBoxLayout *active_layout = new QHBoxLayout();
  QPushButton *close_button;
  QPushButton *button;
  QMenu *menu;
  QLabel *lbl;
  enum effect_type eff;
  int row;

  ui = ui_in;
  selected = nullptr;
  if (filter_in == nullptr) {
    fc_assert(efmc_in != EFMC_NORMAL);
    filter.kind = VUT_NONE;
  } else {
    fc_assert(efmc_in == EFMC_NORMAL);
    filter = *filter_in;
  }
  name = target;

  efmc = efmc_in;

  list_widget = new QListWidget(this);
  effects = effect_list_new();

  connect(list_widget, SIGNAL(itemSelectionChanged()), this, SLOT(select_effect()));
  main_layout->addWidget(list_widget);

  lbl = new QLabel(R__("Type:"));
  active_layout->addWidget(lbl, 0);
  edit_type_button = new QToolButton(this);
  menu = new QMenu();
  edit_type_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
  edit_type_button->setPopupMode(QToolButton::MenuButtonPopup);
  connect(menu, SIGNAL(triggered(QAction *)), this, SLOT(effect_type_menu(QAction *)));
  edit_type_button->setMenu(menu);
  for (eff = (enum effect_type)0; eff < EFT_COUNT;
       eff = (enum effect_type)(eff + 1)) {
    menu->addAction(effect_type_name(eff));
  }
  active_layout->addWidget(edit_type_button, 1);

  lbl = new QLabel(R__("Value:"));
  active_layout->addWidget(lbl, 2);
  value_box = new QSpinBox(this);
  value_box->setRange(-1000, 1000);
  active_layout->addWidget(value_box, 3);
  connect(value_box, SIGNAL(valueChanged(int)), this, SLOT(set_value(int)));

  main_layout->addLayout(active_layout);
  row = 0;


  mp_button = new QToolButton();
  mp_button->setParent(this);
  mp_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
  mp_button->setPopupMode(QToolButton::MenuButtonPopup);
  menu = new QMenu();
  connect(menu, SIGNAL(triggered(QAction *)), this, SLOT(multiplier_menu(QAction *)));
  mp_button->setMenu(menu);
  menu->addAction(NO_MULTIPLIER_NAME); // FIXME: Can't translate, as we compare to "rule_name"
  multipliers_re_active_iterate(pmul) {
    menu->addAction(multiplier_rule_name(pmul));
  } multipliers_re_active_iterate_end;
  effect_edit_layout->addWidget(mp_button, row++, 0);

  button = new QPushButton(QString::fromUtf8(R__("Requirements")), this);
  connect(button, SIGNAL(pressed()), this, SLOT(edit_reqs()));
  effect_edit_layout->addWidget(button, row++, 0);

  button = new QPushButton(QString::fromUtf8(R__("Add Effect")), this);
  connect(button, SIGNAL(pressed()), this, SLOT(add_now()));
  effect_edit_layout->addWidget(button, row++, 0);

  button = new QPushButton(QString::fromUtf8(R__("Delete Effect")), this);
  connect(button, SIGNAL(pressed()), this, SLOT(delete_now()));
  effect_edit_layout->addWidget(button, row++, 0);

  close_button = new QPushButton(QString::fromUtf8(R__("Close")), this);
  connect(close_button, SIGNAL(pressed()), this, SLOT(close_now()));
  effect_edit_layout->addWidget(close_button, row++, 0);

  refresh();

  main_layout->addLayout(effect_edit_layout);

  setLayout(main_layout);
  setWindowTitle(target);
}

/**********************************************************************//**
  Effect edit destructor
**************************************************************************/
effect_edit::~effect_edit()
{
  effect_list_destroy(effects);
}

/**********************************************************************//**
  Callback to fill effects list from iterate_effect_cache()
**************************************************************************/
static bool effect_list_fill_cb(struct effect *peffect, void *data)
{
  struct effect_list_fill_data *cbdata = (struct effect_list_fill_data *)data;

  if (cbdata->filter->kind == VUT_NONE) {
    if (cbdata->efmc == EFMC_NONE) {
      // Look for empty req lists.
      if (requirement_vector_size(&peffect->reqs) == 0) {
        cbdata->edit->add_effect_to_list(peffect, cbdata);
      }
    } else {
      fc_assert(cbdata->efmc == EFMC_ALL);
      cbdata->edit->add_effect_to_list(peffect, cbdata);
    }
  } else if (universal_is_mentioned_by_requirements(&peffect->reqs,
                                                    cbdata->filter)) {
    cbdata->edit->add_effect_to_list(peffect, cbdata);
  }

  return true;
}

/**********************************************************************//**
  Refresh the information.
**************************************************************************/
void effect_edit::refresh()
{
  struct effect_list_fill_data cb_data;

  list_widget->clear();
  effect_list_clear(effects);
  cb_data.filter = &filter;
  cb_data.efmc = efmc;
  cb_data.edit = this;
  cb_data.num = 0;

  iterate_effect_cache(effect_list_fill_cb, &cb_data);

  fill_active();
}

/**********************************************************************//**
  Add entry to effect list.
**************************************************************************/
void effect_edit::add_effect_to_list(struct effect *peffect,
                                     struct effect_list_fill_data *fill_data)
{
  char buf[512];
  QListWidgetItem *item;

  fc_snprintf(buf, sizeof(buf), R__("Effect #%d: %s"),
              fill_data->num + 1, effect_type_name(peffect->type));

  item = new QListWidgetItem(QString::fromUtf8(buf));
  list_widget->insertItem(fill_data->num++, item);
  effect_list_append(effects, peffect);
  if (selected == peffect) {
    item->setSelected(true);
  }
}

/**********************************************************************//**
  Getter for filter
**************************************************************************/
struct universal *effect_edit::filter_get()
{
  return &filter;
}

/**********************************************************************//**
  User pushed close button
**************************************************************************/
void effect_edit::close_now()
{
  ui->unregister_effect_edit(this);
  done(0);
}

/**********************************************************************//**
  User selected effect from the list.
**************************************************************************/
void effect_edit::select_effect()
{
  int i = 0;

  effect_list_iterate(effects, peffect) {
    QListWidgetItem *item = list_widget->item(i++);

    if (item != nullptr && item->isSelected()) {
      selected = peffect;
      selected_nbr = i;
      fill_active();
      return;
    }
  } effect_list_iterate_end;
}

/**********************************************************************//**
  Fill active menus from selected effect.
**************************************************************************/
void effect_edit::fill_active()
{
  if (selected != nullptr) {
    edit_type_button->setText(effect_type_name(selected->type));
    value_box->setValue(selected->value);
    if (selected->multiplier != NULL) {
      mp_button->setText(multiplier_rule_name(selected->multiplier));
    } else {
      mp_button->setText(NO_MULTIPLIER_NAME);
    }
  } else {
    mp_button->setText(NO_MULTIPLIER_NAME);
  }
}

/**********************************************************************//**
  User selected type for the effect.
**************************************************************************/
void effect_edit::effect_type_menu(QAction *action)
{
  QByteArray en_bytes = action->text().toUtf8();
  enum effect_type type = effect_type_by_name(en_bytes.data(),
                                              fc_strcasecmp);

  if (selected != nullptr) {
    selected->type = type;
  }

  ui->refresh_effect_edits();
}

/**********************************************************************//**
  Read value from spinbox to effect
**************************************************************************/
void effect_edit::set_value(int value)
{
  if (selected != nullptr) {
    selected->value = value;
  }

  ui->refresh_effect_edits();
}

/**********************************************************************//**
  User wants to edit requirements
**************************************************************************/
void effect_edit::edit_reqs()
{
  if (selected != nullptr) {
    char buf[128];
    QByteArray en_bytes;

    en_bytes = name.toUtf8();
    fc_snprintf(buf, sizeof(buf), R__("%s effect #%d"), en_bytes.data(),
                selected_nbr);

    ui->open_req_edit(QString::fromUtf8(buf), &selected->reqs);
  }
}

/**********************************************************************//**
  User clicked windows close button.
**************************************************************************/
void effect_edit::closeEvent(QCloseEvent *event)
{
  ui->unregister_effect_edit(this);
}

/**********************************************************************//**
  User requested new effect
**************************************************************************/
void effect_edit::add_now()
{
  struct effect *peffect = effect_new((enum effect_type)0, 0, NULL);

  if (filter.kind != VUT_NONE) {
    struct requirement req;

    req = req_from_str(universal_type_rule_name(&filter),
                       nullptr, false, true, false,
                       universal_rule_name(&filter));
    effect_req_append(peffect, req);
  }

  refresh();
}

/**********************************************************************//**
  User requested effect deletion
**************************************************************************/
void effect_edit::delete_now()
{
  if (selected != nullptr) {
    effect_remove(selected);

    selected = nullptr;

    refresh();
  }
}

/**********************************************************************//**
  User selected multiplier for the effect
**************************************************************************/
void effect_edit::multiplier_menu(QAction *action)
{
  QByteArray an_bytes = action->text().toUtf8();

  if (selected == nullptr) {
    return;
  }

  if (!strcasecmp(NO_MULTIPLIER_NAME, an_bytes)) {
    selected->multiplier = NULL;
  } else {
    selected->multiplier = multiplier_by_rule_name(an_bytes);
  }

  refresh();
}
