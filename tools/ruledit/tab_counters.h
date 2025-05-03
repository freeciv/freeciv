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

#ifndef FC__TAB_COUNTER_H
#define FC__TAB_COUNTER_H

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

// Qt
#include <QCheckBox>
#include <QComboBox>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QWidget>

// common
#include "counters.h"

class ruledit_gui;

class tab_counter : public QWidget
{
  Q_OBJECT

  public:
    explicit tab_counter(ruledit_gui *ui_in);
    void refresh();
    static void techs_to_menu(QMenu *fill_menu);
    static QString counter_name(struct counter *padv);

  private:
    ruledit_gui *ui;

    QLineEdit *name;
    QLineEdit *rname;
    QSpinBox *checkpoint;
    QSpinBox *def;
    QComboBox *type;
    QCheckBox *same_name;
    QPushButton *effects_button;

    QListWidget *counter_list;

    struct counter *selected;

  private slots:
    void update_counter_info(struct counter *counter);
    void checkpoint_given(int val);
    void default_given(int val);
    void name_given();
    void select_counter();
    void add_now();
    void delete_now();
    void same_name_toggle(bool checked);
    void edit_effects();
    bool initialize_new_counter(struct counter *padv);
    void counter_behavior_selected(int item);
};


#endif // FC__TAB_COUNTER_H
