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

#ifndef FC__DIALOGS_H
#define FC__DIALOGS_H

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

extern "C" {
#include "dialogs_g.h"
}

// Qt
#include <QComboBox>
#include <QItemSelection>
#include <QDialog>

class QGridLayout;
class QGorupBox;
class QRadioButton;
class QTableWidget;
class QTextEdit;
class QToolBox;
class QWidget;

void popup_races_dialog(struct player *pplayer);

/***************************************************************************
 Dialog for selecting nation, style and leader leader
***************************************************************************/
class races_dialog:public QDialog
{
  Q_OBJECT
  QGridLayout *main_layout;
  QToolBox *nation_tabs;
  QList<QWidget*> *nations_tabs_list;
  QTableWidget *selected_nation_tabs;
  QComboBox *leader_name;
  QRadioButton *is_male;
  QRadioButton *is_female;
  QTableWidget *city_styles;
  QTextEdit *description;
  QPushButton *ok_button;
  QPushButton *random_button;

public:
  races_dialog(struct player *pplayer, QWidget *parent = 0);

private slots:
  void set_index(int index);
  void nation_selected(const QItemSelection &sl, const QItemSelection &ds);
  void style_selected(const QItemSelection &sl, const QItemSelection &ds);
  void leader_selected(int index);
  void ok_pressed();
  void cancel_pressed();
  void random_pressed();

private:
  int selected_nation;
  int selected_style;
  int selected_sex;
  struct player *tplayer;
};

void popup_revolution_dialog(struct government *government = NULL);
void revolution_response(struct government *government);

#endif /* FC__DIALOGS_H */
