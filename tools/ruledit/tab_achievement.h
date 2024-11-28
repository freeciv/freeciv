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

#ifndef FC__TAB_ACHIEVEMENT_H
#define FC__TAB_ACHIEVEMENT_H

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

// Qt
#include <QWidget>

class QCheckBox;
class QLineEdit;
class QListWidget;
class QMenu;
class QSpinBox;
class QToolButton;

class ruledit_gui;

class tab_achievement : public QWidget
{
  Q_OBJECT

  public:
    explicit tab_achievement(ruledit_gui *ui_in);
    void refresh();

  private:
    ruledit_gui *ui;
    void update_achievement_info(struct achievement *pach);
    bool initialize_new_achievement(struct achievement *pach);

    QLineEdit *name;
    QLineEdit *rname;
    QListWidget *ach_list;
    QCheckBox *same_name;
    QToolButton *type_button;
    QMenu *type_menu;
    QSpinBox *value_box;

    struct achievement *selected;

  private slots:
    void name_given();
    void select_achievement();
    void add_now();
    void delete_now();
    void same_name_toggle(bool checked);
    void edit_effects();
    void edit_type(QAction *action);
    void set_value(int value);
};

#endif // FC__TAB_ACHIEVEMENT_H
