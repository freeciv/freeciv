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

#ifndef FC__EDIT_UTYPE_H
#define FC__EDIT_UTYPE_H

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

// Qt
#include <QDialog>

// ruledit
#include "values_dlg.h"

class QGridLayout;
class QLineEdit;
class QSpinBox;
class QToolButton;

class ruledit_gui;

class edit_utype : public values_dlg
{
  Q_OBJECT

  public:
    explicit edit_utype(ruledit_gui *ui_in, struct unit_type *utype_in);
    void refresh();

  private:
    ruledit_gui *ui;
    struct unit_type *utype;
    QToolButton *req_button;
    QToolButton *class_button;
    QSpinBox *bcost;
    QSpinBox *attack;
    QSpinBox *defense;
    QSpinBox *hitpoints;
    QSpinBox *firepower;
    QSpinBox *move_rate;
    QSpinBox *cargo_capacity;
    QLineEdit *gfx_tag;
    QLineEdit *gfx_tag_alt;
    QLineEdit *sound_move_tag;
    QLineEdit *sound_move_tag_alt;
    QLineEdit *sound_fight_tag;
    QLineEdit *sound_fight_tag_alt;

    QGridLayout *cargo_layout;
    QGridLayout *flag_layout;

  protected:
    void closeEvent(QCloseEvent *cevent);

  private slots:
    void req_menu(QAction *action);
    void class_menu(QAction *action);
    void set_bcost_value(int value);
    void set_attack_value(int value);
    void set_defense_value(int value);
    void set_hitpoints(int value);
    void set_firepower(int value);
    void set_move_rate(int value);
    void set_cargo_capacity(int value);
    void gfx_tag_given();
    void gfx_tag_alt_given();
    void sound_move_tag_given();
    void sound_move_tag_alt_given();
    void sound_fight_tag_given();
    void sound_fight_tag_alt_given();
    void helptext();
};

#endif // FC__EDIT_UTYPE_H
