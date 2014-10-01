/********************************************************************** 
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

#ifndef FC__RULEDIT_QT_H
#define FC__RULEDIT_QT_H

// Qt
#include <QApplication>
#include <QObject>
#include <QLabel>
#include <QTabWidget>

// ruledit
#include "rulesave.h"

class QLineEdit;
class QStackedLayout;

class requirers_dlg;
class tab_building;
class tab_misc;
class tab_tech;
class tab_unit;
class tab_nation;

class ruledit_gui : public QObject
{
  Q_OBJECT

  public:
    void setup(QWidget *central_in);
    void display_msg(const char *msg);
    void clear_required(const char *title);
    void show_required(const char *msg);
    void flush_widgets();

    struct rule_data data;

  private:
    QLabel *msg_dspl;
    QTabWidget *stack;
    QLineEdit *ruleset_select;
    QWidget *central;
    QStackedLayout *main_layout;

    requirers_dlg *requirers;

    tab_building *bldg;
    tab_misc *misc;
    tab_tech *tech;
    tab_unit *unit;
    tab_nation *nation;

  private slots:
    void launch_now();
};

bool ruledit_qt_setup(int argc, char **argv);
int ruledit_qt_run();
void ruledit_qt_close();
void ruledit_qt_display_requirers(const char *msg);

#endif // FC__RULEDIT_QT_H
