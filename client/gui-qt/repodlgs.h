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

// client
#include "climisc.h"

// Qt
#include <QObject>
#include <QWidget>

class QComboBox;
class QGridLayout;
class QGroupBox;
class QHBoxLayout;
class QItemSelection;
class QLabel;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QTableWidget;
class QTableWidgetItem;

/****************************************************************************
  Helper item for comboboxes, holding string of tech and its id
****************************************************************************/
struct qlist_item {
  QString tech_str;
  Tech_type_id id;
};

/****************************************************************************
  Helper item for research diagram, about drawn rectangles and what
  tech/unit/improvement they point to.
****************************************************************************/
class req_tooltip_help
{
public:
  req_tooltip_help();
  QRect rect;
  Tech_type_id tech_id;
  struct unit_type *tunit;
  struct impr_type *timpr;
  struct government *tgov;
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
  void reset();
  QSize size();

private:
  void mousePressEvent(QMouseEvent *event);
  void mouseMoveEvent(QMouseEvent *event);
  void paintEvent(QPaintEvent *event);
  void create_tooltip_help();
  struct canvas *pcanvas;
  struct reqtree *req;
  int width;
  int height;
  QList<req_tooltip_help*> tt_help;
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
  void init(bool raise);
  void redraw();
  void reset_tree();

private:
  void update_reqtree();
  int index;

private slots:
  void current_tech_changed(int index);
  void goal_tech_changed(int index);
};

/****************************************************************************
  Tab widget to display units report (F2)
****************************************************************************/
class units_report: public QWidget
{
  Q_OBJECT
  QPushButton *find_button;
  QPushButton *upgrade_button;
  QTableWidget *units_widget;

public:
  units_report();
  ~units_report();
  void update_report();
  void init();

private:
  int index;
  int curr_row;
  int max_row;
  Unit_type_id uid;
  struct unit *find_nearest_unit(const struct unit_type *utype,
                                      struct tile *ptile);

private slots:
  void find_units();
  void upgrade_units();
  void selection_changed(const QItemSelection &sl,
                         const QItemSelection &ds);

};

/****************************************************************************
  Tab widget to display economy report (F5)
****************************************************************************/
class eco_report: public QWidget
{
  Q_OBJECT
  QPushButton *disband_button;
  QPushButton *sell_button;
  QPushButton *sell_redun_button;
  QTableWidget *eco_widget;
  QLabel *eco_label;

public:
  eco_report();
  ~eco_report();
  void update_report();
  void init();

private:
  int index;
  int curr_row;
  int max_row;
  cid uid;
  int counter;

private slots:
  void disband_units();
  void sell_buildings();
  void sell_redundant();
  void selection_changed(const QItemSelection &sl,
                         const QItemSelection &ds);
};

/****************************************************************************
  Tab widget to display economy report (F5)
****************************************************************************/
class endgame_report: public QWidget
{
  Q_OBJECT
  QTableWidget *end_widget;

public:
  endgame_report(const struct packet_endgame_report *packet);
  ~endgame_report();
  void update_report(const struct packet_endgame_player *packet);
  void init();

private:
  int index;
  int players;

};


bool comp_less_than(const qlist_item &q1, const qlist_item &q2);
void popdown_economy_report();
void popdown_units_report();
void popdown_science_report();
void popdown_endgame_report();

#endif /* FC__REPODLGS_H */
