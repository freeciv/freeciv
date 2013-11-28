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

#ifndef FC__TAB_TECH_H
#define FC__TAB_TECH_H

// Qt
#include <QWidget>

class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;

class ruledit_gui;

struct advance;

class tab_tech : public QWidget
{
  Q_OBJECT

  public:
    explicit tab_tech(QWidget *parent, ruledit_gui *ui_in);

  private:
    ruledit_gui *ui;
    void update_tech_info(struct advance *adv);
    QString tech_name(struct advance *padv);

    QLabel *name;
    QLabel *rname;
    QLabel *req1;
    QLabel *req2;
    QLabel *root_req;
    QListWidget *tech_list;

  private slots:
    void select_tech();
};


#endif // FC__TAB_TECH_H
