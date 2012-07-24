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

#ifndef FC__REPODLGS_H
#define FC__REPODLGS_H

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

extern "C" {
#include "repodlgs_g.h"
}

// Qt
#include <QObject>
#include <QWidget>

class QComboBox;
class QGridLayout;
class QGroupBox;
class QHBoxLayout;
class QLabel;
class QProgressBar;
class QScrollArea;

/****************************************************************************
  Helper item for comboboxes, holding string of tech and its id
****************************************************************************/
struct qlist_item {
  QString tech_str;
  Tech_type_id id;
};

/****************************************************************************
  Custom widget representing research diagram in science_report
****************************************************************************/
class research_diagram: public QWidget
{
  Q_OBJECT

public:
  research_diagram(QWidget *parent = 0);
  ~research_diagram();
  void update_reqtree();
  QSize size();

private:
  void mousePressEvent(QMouseEvent *event);
  void paintEvent(QPaintEvent *event);
  struct canvas *pcanvas;
  struct reqtree *req;
  int width;
  int height;
};

/****************************************************************************
  Widget embedded as tab on game view (F6 default)
  Uses string "SCI" to mark it as opened
  You can check it using if (gui()->is_repo_dlg_open("SCI"))
****************************************************************************/
class science_report: public QWidget
{
  Q_OBJECT

  QComboBox *goal_combo;
  QComboBox *researching_combo;
  QGridLayout *sci_layout;
  QProgressBar *progress;
  QLabel *info_label;
  QLabel *progress_label;
  QList<qlist_item> *curr_list;
  QList<qlist_item> *goal_list;
  research_diagram *res_diag;
  QScrollArea *scroll;

public:
  science_report();
  ~science_report();
  void update_report();
  void init();
  void redraw();

private:
  void update_reqtree();
  int index;

private slots:
  void current_tech_changed(int index);
  void goal_tech_changed(int index);
};

#endif /* FC__REPODLGS_H */
