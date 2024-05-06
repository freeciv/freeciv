/***********************************************************************
 Freeciv - Copyright (C) 2023 The Freeciv Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifndef FC__EDIT_EXTRA_H
#define FC__EDIT_EXTRA_H

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

class ruledit_gui;

class edit_extra : public values_dlg
{
  Q_OBJECT

  public:
    explicit edit_extra(ruledit_gui *ui_in, struct extra_type *extra_in);
    void refresh();

  private:
    ruledit_gui *ui;
    struct extra_type *extra;
    QLineEdit *gfx_tag;
    QLineEdit *gfx_tag_alt;
    QLineEdit *act_gfx;
    QLineEdit *act_gfx_alt;
    QLineEdit *act_gfx_alt2;
    QLineEdit *rmact_gfx;
    QLineEdit *rmact_gfx_alt;
    QLineEdit *rmact_gfx_alt2;

    QGridLayout *natives_layout;
    QGridLayout *flag_layout;

  protected:
    void closeEvent(QCloseEvent *cevent);

  private slots:
    void gfx_tag_given();
    void gfx_tag_alt_given();
    void act_gfx_given();
    void act_gfx_alt_given();
    void act_gfx_alt2_given();
    void rmact_gfx_given();
    void rmact_gfx_alt_given();
    void rmact_gfx_alt2_given();
    void helptext();
};

#endif // FC__EDIT_EXTRA_H
