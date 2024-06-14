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

#ifndef FC__EDIT_GOV_H
#define FC__EDIT_GOV_H

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

// Qt
#include <QDialog>

// ruledit
#include "values_dlg.h"

class QGridLayout;
class QLineEdit;

class ruledit_gui;

class edit_gov : public values_dlg
{
  Q_OBJECT

  public:
    explicit edit_gov(ruledit_gui *ui_in, struct government *gov_in);
    void refresh();

  private:
    ruledit_gui *ui;
    struct government *gov;
    QLineEdit *gfx_tag;
    QLineEdit *gfx_tag_alt;
    QLineEdit *sound_tag;
    QLineEdit *sound_tag_alt;
    QLineEdit *sound_tag_alt2;

  protected:
    void closeEvent(QCloseEvent *cevent);

  private slots:
    void gfx_tag_given();
    void gfx_tag_alt_given();
    void sound_tag_given();
    void sound_tag_alt_given();
    void sound_tag_alt2_given();
    void helptext();
};


#endif // FC__EDIT_GOV_H
