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
#ifndef FC__CITYDLG_H
#define FC__CITYDLG_H

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

class QComboBox;
class QDialog;
class QGridLayout;
class QLabel;
class QPushButton;
class QTabWidget;
class QTableView;
class QTableWidget;
class QProgressBar;
class QItemDelegate;
class QVariant;
class QVBoxLayout;

#define NUM_INFO_FIELDS 12

// client
#include "canvas.h"


/****************************************************************************
  Single item on unit_info in city dialog representing one unit
****************************************************************************/
class unit_item:public QLabel
{

  Q_OBJECT

  QAction *disband_action;
  QAction *change_home;
  QAction *activate;
  QAction *activate_and_close;
  QAction *sentry;
  QAction *fortify;

public:

  unit_item(struct unit *punit);
  ~unit_item();
  void init_pix();

private:

  struct unit *qunit;
  struct canvas *unit_pixmap;
  void contextMenuEvent(QContextMenuEvent *ev);
  void create_actions();

private slots:

  void disband();
  void change_homecity();
  void activate_unit();
  void activate_and_close_dialog();
  void sentry_unit();
  void fortify_unit();

};

/****************************************************************************
  Shows list of units ( as labels - unit_info )
****************************************************************************/
class unit_info:public QWidget
{

  Q_OBJECT

public:

  unit_info();
  ~unit_info();
  void add_item(unit_item *item);
  void init_layout();
  void clear_layout();

private:

  QHBoxLayout *layout;
  QList<unit_item*> unit_list;

};

/****************************************************************************
  Class used for showing tiles and workers view in city dialog
****************************************************************************/
class city_map:public QWidget {

  Q_OBJECT

  canvas *view;

public:

  city_map();
  void set_pixmap(struct city *pcity);
  int width;
  int height;

private:

  void mousePressEvent(QMouseEvent *event);
  void paintEvent(QPaintEvent *event);
  struct city *mcity;

};


/****************************************************************************
  city_label is used only for showing citizens icons
  and was created to catch mouse events
****************************************************************************/
class city_label:public QLabel {

  Q_OBJECT

public:

  city_label(QWidget *parent = 0);
  void set_city(struct city *pcity);

private:

  struct city *pcity;

protected:

  void mousePressEvent(QMouseEvent *event);

};

/**
 * FIXME
 * dialog auto-resize widgets might be nervous
 * needs some fixed policy for changing size for each widget
 * 
 * needs also selling buildings after click on production_table
 * (built improvements)
 * 
 * Change Combo Box to ListView with icons and text or TreeView
 * + QProgressBar delegation
 * 
 * City view in amplio is damn big, needs rescale or clip black borders
 * that second option might be nice,there is a lot of black, it might be
 * calculated in construtor what to clip, and every redraw just draw clipped
 * pixmap + fix on mouse event
 */

class city_dialog:public QDialog {

  Q_OBJECT

  QWidget *widget;
  QGridLayout *main_grid_layout;
  QGridLayout *overview_grid_layout;
  QGridLayout *production_grid_layout;
  city_map *view;
  city_label *citizens_label;
  QGridLayout *info_grid_layout;
  QGroupBox *info_labels_group;
  QWidget *info_widget;
  QLabel *qlt[NUM_INFO_FIELDS];
  QComboBox *production_combo;
  QTableWidget *production_table;
  QTabWidget *tab_widget;
  QWidget *overview_tab;
  QWidget *production_tab;
  QWidget *happiness_tab;
  QWidget *governor_tab;
  QWidget *settings_tab;
  QPushButton *button;
  QPushButton *buy_button;
  QPixmap *citizen_pixmap;
  unit_info *current_units;
  unit_info *supported_units;

public:

  city_dialog(QWidget *parent = 0);
  void setup_ui(struct city *qcity);
  void refresh();

private:

  struct city *pcity;
  int current_building;
  void update_title();
  void update_building();
  void update_info_label();
  void update_buy_button();
  void update_citizens();
  void update_improvements();
  void update_units();

  private slots:

  void next_city();
  void prev_city();
  void production_changed(int index);
  void buy();

};

#endif				/* FC__CITYDLG_H */
