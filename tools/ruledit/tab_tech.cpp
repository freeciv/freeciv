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
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>

// utility
#include "fcintl.h"
#include "log.h"

// common
#include "game.h"
#include "tech.h"

// ruledit
#include "edit_tech.h"
#include "ruledit.h"
#include "ruledit_qt.h"
#include "validity.h"

#include "tab_tech.h"

/**********************************************************************//**
  Setup tab_tech object
**************************************************************************/
tab_tech::tab_tech(ruledit_gui *ui_in) : QWidget()
{
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  QGridLayout *tech_layout = new QGridLayout();
  QLabel *label;
  QPushButton *effects_button;
  QPushButton *add_button;
  QPushButton *delete_button;
  QPushButton *edit_button;
  int row = 0;

  ui = ui_in;
  selected = 0;

  tech_list = new QListWidget(this);

  connect(tech_list, SIGNAL(itemSelectionChanged()), this, SLOT(select_tech()));
  main_layout->addWidget(tech_list);

  tech_layout->setSizeConstraint(QLayout::SetMaximumSize);

  label = new QLabel(QString::fromUtf8(R__("Rule Name")));
  label->setParent(this);
  rname = new QLineEdit(this);
  rname->setText(R__("None"));
  connect(rname, SIGNAL(returnPressed()), this, SLOT(name_given()));
  tech_layout->addWidget(label, row, 0);
  tech_layout->addWidget(rname, row++, 2);

  label = new QLabel(QString::fromUtf8(R__("Name")));
  label->setParent(this);
  same_name = new QCheckBox();
  connect(same_name, SIGNAL(toggled(bool)), this, SLOT(same_name_toggle(bool)));
  name = new QLineEdit(this);
  name->setText(R__("None"));
  connect(name, SIGNAL(returnPressed()), this, SLOT(name_given()));
  tech_layout->addWidget(label, row, 0);
  tech_layout->addWidget(same_name, row, 1);
  tech_layout->addWidget(name, row++, 2);

  edit_button = new QPushButton(QString::fromUtf8(R__("Edit Values")), this);
  connect(edit_button, SIGNAL(pressed()), this, SLOT(edit_now()));
  tech_layout->addWidget(edit_button, row++, 2);

  label = new QLabel(QString::fromUtf8(R__("Req1")));
  label->setParent(this);
  req1_button = new QToolButton();
  req1_button->setParent(this);
  req1 = prepare_req_button(req1_button, AR_ONE);
  connect(req1_button, SIGNAL(pressed()), this, SLOT(req1_jump()));
  tech_layout->addWidget(label, row, 0);
  tech_layout->addWidget(req1_button, row++, 2);

  label = new QLabel(QString::fromUtf8(R__("Req2")));
  label->setParent(this);
  req2_button = new QToolButton();
  req2 = prepare_req_button(req2_button, AR_TWO);
  connect(req2_button, SIGNAL(pressed()), this, SLOT(req2_jump()));
  tech_layout->addWidget(label, row, 0);
  tech_layout->addWidget(req2_button, row++, 2);

  label = new QLabel(QString::fromUtf8(R__("Root Req")));
  label->setParent(this);
  root_req_button = new QToolButton();
  root_req_button->setParent(this);
  root_req = prepare_req_button(root_req_button, AR_ROOT);
  connect(root_req_button, SIGNAL(pressed()), this, SLOT(root_req_jump()));
  tech_layout->addWidget(label, row, 0);
  tech_layout->addWidget(root_req_button, row++, 2);

  effects_button = new QPushButton(QString::fromUtf8(R__("Effects")), this);
  connect(effects_button, SIGNAL(pressed()), this, SLOT(edit_effects()));
  tech_layout->addWidget(effects_button, row++, 2);

  add_button = new QPushButton(QString::fromUtf8(R__("Add tech")), this);
  connect(add_button, SIGNAL(pressed()), this, SLOT(add_now()));
  tech_layout->addWidget(add_button, row, 0);

  delete_button = new QPushButton(QString::fromUtf8(R__("Remove this tech")), this);
  connect(delete_button, SIGNAL(pressed()), this, SLOT(delete_now()));
  tech_layout->addWidget(delete_button, row++, 2);

  refresh();
  update_tech_info(nullptr);

  main_layout->addLayout(tech_layout);

  setLayout(main_layout);
}

/**********************************************************************//**
  Refresh the information.
**************************************************************************/
void tab_tech::refresh()
{
  tech_list->clear();

  advance_re_active_iterate(padv) {
    QListWidgetItem *item = new QListWidgetItem(advance_rule_name(padv));

    tech_list->insertItem(advance_index(padv), item);
  } advance_re_active_iterate_end;

  techs_to_menu(req1);
  techs_to_menu(req2);
  techs_to_menu(root_req);
}

/**********************************************************************//**
  Build tech req button
**************************************************************************/
QMenu *tab_tech::prepare_req_button(QToolButton *button, enum tech_req rn)
{
  QMenu *menu = new QMenu();

  button->setToolButtonStyle(Qt::ToolButtonTextOnly);
  button->setPopupMode(QToolButton::MenuButtonPopup);

  switch (rn) {
  case AR_ONE:
    connect(menu, SIGNAL(triggered(QAction *)), this, SLOT(req1_menu(QAction *)));
    break;
  case AR_TWO:
    connect(menu, SIGNAL(triggered(QAction *)), this, SLOT(req2_menu(QAction *)));
    break;
  case AR_ROOT:
    connect(menu, SIGNAL(triggered(QAction *)), this, SLOT(root_req_menu(QAction *)));
    break;
  case AR_SIZE:
    fc_assert(rn != AR_SIZE);
    break;
  }

  button->setMenu(menu);

  return menu;
}

/**********************************************************************//**
  Fill menu with all possible tech values
**************************************************************************/
void tab_tech::techs_to_menu(QMenu *fill_menu)
{
  fill_menu->clear();

  advance_iterate_all(padv) {
    fill_menu->addAction(tech_name(padv));
  } advance_iterate_all_end;
}

/**********************************************************************//**
  Display name of the tech
**************************************************************************/
QString tab_tech::tech_name(struct advance *padv)
{
  if (padv == A_NEVER) {
    return QString::fromUtf8(R__("Never"));
  }

  return QString::fromUtf8(advance_rule_name(padv));
}

/**********************************************************************//**
  Update info of the tech
**************************************************************************/
void tab_tech::update_tech_info(struct advance *adv)
{
  selected = adv;

  if (selected != nullptr) {
    QString dispn = QString::fromUtf8(untranslated_name(&(adv->name)));
    QString rulen = QString::fromUtf8(rule_name_get(&(adv->name)));

    name->setText(dispn);
    rname->setText(rulen);

    if (dispn == rulen) {
      name->setEnabled(false);
      same_name->setChecked(true);
    } else {
      same_name->setChecked(false);
      name->setEnabled(true);
    }

    req1_button->setText(tech_name(adv->require[AR_ONE]));
    req2_button->setText(tech_name(adv->require[AR_TWO]));
    root_req_button->setText(tech_name(adv->require[AR_ROOT]));
  } else {
    name->setText(R__("None"));
    rname->setText(R__("None"));
    // FIXME: Could these be translated, or do we depend on
    //        them matching English rule_name of tech "None"?
    req1_button->setText("None");
    req2_button->setText("None");
    root_req_button->setText("None");
    same_name->setChecked(true);
    name->setEnabled(false);
  }
}

/**********************************************************************//**
  User selected tech from the list.
**************************************************************************/
void tab_tech::select_tech()
{
  QList<QListWidgetItem *> select_list = tech_list->selectedItems();

  if (!select_list.isEmpty()) {
    QByteArray tn_bytes;

    tn_bytes = select_list.at(0)->text().toUtf8();
    update_tech_info(advance_by_rule_name(tn_bytes.data()));
  }
}

/**********************************************************************//**
  Req1 of the current tech selected.
**************************************************************************/
void tab_tech::req1_jump()
{
  if (selected != 0 && advance_number(selected->require[AR_ONE]) != A_NONE) {
    update_tech_info(selected->require[AR_ONE]);
  }
}

/**********************************************************************//**
  Req2 of the current tech selected.
**************************************************************************/
void tab_tech::req2_jump()
{
  if (selected != 0 && advance_number(selected->require[AR_TWO]) != A_NONE) {
    update_tech_info(selected->require[AR_TWO]);
  }
}

/**********************************************************************//**
  Root req of the current tech selected.
**************************************************************************/
void tab_tech::root_req_jump()
{
  if (selected != 0 && advance_number(selected->require[AR_ROOT]) != A_NONE) {
    update_tech_info(selected->require[AR_ROOT]);
  }
}

/**********************************************************************//**
  User selected tech to be req1
**************************************************************************/
void tab_tech::req1_menu(QAction *action)
{
  struct advance *padv;
  QByteArray an_bytes;

  an_bytes = action->text().toUtf8();
  padv = advance_by_rule_name(an_bytes.data());

  if (padv != 0 && selected != 0) {
    selected->require[AR_ONE] = padv;

    update_tech_info(selected);
  }
}

/**********************************************************************//**
  User selected tech to be req2
**************************************************************************/
void tab_tech::req2_menu(QAction *action)
{
  struct advance *padv;
  QByteArray an_bytes;

  an_bytes = action->text().toUtf8();
  padv = advance_by_rule_name(an_bytes.data());

  if (padv != 0 && selected != 0) {
    selected->require[AR_TWO] = padv;

    update_tech_info(selected);
  }
}

/**********************************************************************//**
  User selected tech to be root_req
**************************************************************************/
void tab_tech::root_req_menu(QAction *action)
{
  struct advance *padv;
  QByteArray an_bytes;

  an_bytes = action->text().toUtf8();
  padv = advance_by_rule_name(an_bytes.data());

  if (padv != 0 && selected != 0) {
    selected->require[AR_ROOT] = padv;

    update_tech_info(selected);
  }
}

/**********************************************************************//**
  User entered name for tech
**************************************************************************/
void tab_tech::name_given()
{
  if (selected != nullptr) {
    QByteArray name_bytes;
    QByteArray rname_bytes;

    advance_iterate(padv) {
      if (padv != selected
          && padv->require[AR_ONE] != A_NEVER) {
        rname_bytes = rname->text().toUtf8();
        if (!strcmp(advance_rule_name(padv), rname_bytes.data())) {
          ui->display_msg(R__("A tech with that rule name already exists!"));
          return;
        }
      }
    } advance_iterate_end;

    if (same_name->isChecked()) {
      name->setText(rname->text());
    }

    name_bytes = name->text().toUtf8();
    rname_bytes = rname->text().toUtf8();
    names_set(&(selected->name), 0,
              name_bytes.data(),
              rname_bytes.data());
    refresh();
  }
}

/**********************************************************************//**
  User requested tech deletion
**************************************************************************/
void tab_tech::delete_now()
{
  if (selected != 0) {
    requirers_dlg *requirers;

    requirers = ui->create_requirers(advance_rule_name(selected));
    if (is_tech_needed(selected, &ruledit_qt_display_requirers, requirers)) {
      return;
    }

    selected->require[AR_ONE] = A_NEVER;

    if (selected->ruledit_dlg != nullptr) {
      ((edit_tech *)selected->ruledit_dlg)->done(0);
    }

    refresh();
    update_tech_info(nullptr);
  }
}

/**********************************************************************//**
  Initialize new tech for use.
**************************************************************************/
bool tab_tech::initialize_new_tech(struct advance *padv)
{
  struct advance *none = advance_by_number(A_NONE);

  if (advance_by_rule_name("New Tech") != nullptr) {
    return false;
  }

  padv->require[AR_ONE] = none;
  padv->require[AR_TWO] = none;
  padv->require[AR_ROOT] = none;
  name_set(&(padv->name), 0, "New Tech");
  BV_CLR_ALL(padv->flags);
  if (padv->helptext != nullptr) {
    strvec_clear(padv->helptext);
  }

  return true;
}

/**********************************************************************//**
  User requested new tech
**************************************************************************/
void tab_tech::add_now()
{
  struct advance *new_adv;

  // Try to reuse freed tech slot
  advance_iterate(padv) {
    if (padv->require[AR_ONE] == A_NEVER) {
      if (initialize_new_tech(padv)) {
        update_tech_info(padv);
        refresh();
      }
      return;
    }
  } advance_iterate_end;

  // Try to add completely new tech
  if (game.control.num_tech_types >= A_LAST) {
    return;
  }

  // num_tech_types must be big enough to hold new tech or
  // advance_by_number() fails.
  game.control.num_tech_types++;
  new_adv = advance_by_number(game.control.num_tech_types - 1);
  if (initialize_new_tech(new_adv)) {
    update_tech_info(new_adv);
    refresh();
  } else {
    game.control.num_tech_types--; // Restore
  }
}

/**********************************************************************//**
  Toggled whether rule_name and name should be kept identical
**************************************************************************/
void tab_tech::same_name_toggle(bool checked)
{
  name->setEnabled(!checked);
  if (checked) {
    name->setText(rname->text());
  }
}

/**********************************************************************//**
  User wants to edit effects
**************************************************************************/
void tab_tech::edit_effects()
{
  if (selected != nullptr) {
    struct universal uni;

    uni.value.advance = selected;
    uni.kind = VUT_ADVANCE;

    ui->open_effect_edit(QString::fromUtf8(advance_rule_name(selected)),
                         &uni, EFMC_NORMAL);
  }
}

/**********************************************************************//**
  User requested tech edit dialog
**************************************************************************/
void tab_tech::edit_now()
{
  if (selected != nullptr) {
    if (selected->ruledit_dlg == nullptr) {
      edit_tech *edit = new edit_tech(ui, selected);

      edit->show();
      selected->ruledit_dlg = edit;
    } else {
      ((edit_tech *)selected->ruledit_dlg)->raise();
    }
  }
}
