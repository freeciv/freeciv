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

#ifndef FC__EDIT_TECH_H
#define FC__EDIT_TECH_H

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

class edit_tech : public values_dlg
{
  Q_OBJECT

  public:
    explicit edit_tech(ruledit_gui *ui_in, struct advance *tech_in);
    void refresh();

  private:
    ruledit_gui *ui;
    struct advance *tech;
    QSpinBox *cost;
    QLineEdit *gfx_tag;
    QLineEdit *gfx_tag_alt;

    QGridLayout *flag_layout;

  protected:
    void closeEvent(QCloseEvent *cevent);

  private slots:
    void set_cost_value(int value);
    void gfx_tag_given();
    void gfx_tag_alt_given();
    void helptext();
};


#endif // FC__EDIT_TECH_H
