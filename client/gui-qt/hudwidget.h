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

#ifndef FC__HUDWIDGET_H
#define FC__HUDWIDGET_H

// Qt
#include <QDialog>
#include <QLabel>
#include <QMessageBox>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QTableWidget>

// common
#include "unit.h"

// qt-client
#include "shortcuts.h"

class QIcon;
class QHBoxLayout;
class QLabel;
class QRadioButton;
class QComboBox;
class move_widget;
struct unit_list;
struct tile;
struct unit;

void show_new_turn_info();
bool has_player_unit_type(Unit_type_id utype);

/****************************************************************************
  Custom message box with animated background
****************************************************************************/
class hud_message_box: public QMessageBox
{
  Q_OBJECT
  QElapsedTimer m_timer;

public:
  hud_message_box(QWidget *parent);
  ~hud_message_box();
  void set_text_title(QString s1, QString s2);

protected:
  void paintEvent(QPaintEvent *event);
  void timerEvent(QTimerEvent *event);
  void keyPressEvent(QKeyEvent *event);
private:
  int m_animate_step;
  QString text;
  QString title;
  QFontMetrics *fm_text;
  QFontMetrics *fm_title;
  QFont f_text;
  QString cs1, cs2;
  QFont f_title;
  int top;
  int mult;
};

/****************************************************************************
  Class for showing text on screen
****************************************************************************/
class hud_text: public QWidget
{
  Q_OBJECT
public:
  hud_text(QString s, int time_secs, QWidget *parent);
  ~hud_text();
  void show_me();
protected:
  void paintEvent(QPaintEvent *event);
  void timerEvent(QTimerEvent *event);
private:
  void center_me();
  QRect bound_rect;
  int timeout;
  int m_animate_step;
  QString text;
  QElapsedTimer m_timer;
  QFontMetrics *fm_text;
  QFont f_text;
  QFont f_title;
};

/****************************************************************************
  Custom input box with animated background
****************************************************************************/
class hud_input_box: public QDialog
{
  Q_OBJECT
  QElapsedTimer m_timer;

public:
  hud_input_box(QWidget *parent);
  ~hud_input_box();
  void set_text_title_definput(QString s1, QString s2, QString def_input);
  QLineEdit input_edit;

protected:
  void paintEvent(QPaintEvent *event);
  void timerEvent(QTimerEvent *event);
private:
  int m_animate_step;
  QString text;
  QString title;
  QFontMetrics *fm_text;
  QFontMetrics *fm_title;
  QFont f_text;
  QString cs1, cs2;
  QFont f_title;
  int top;
  int mult;
};

/****************************************************************************
  Custom label to center on current unit
****************************************************************************/
class click_label : public QLabel
{
  Q_OBJECT
public:
  click_label();
signals:
  void left_clicked();
private slots:
  void on_clicked();
protected:
  void mousePressEvent(QMouseEvent *e);
};

/****************************************************************************
  Single action on unit actions
****************************************************************************/
class hud_action : public QWidget
{
  Q_OBJECT
  QPixmap *action_pixmap;
  bool focus;
public:
  hud_action(QWidget *parent);
  ~hud_action();
  void set_pixmap(QPixmap *p);
  shortcut_id action_shortcut;
signals:
  void left_clicked();
  void right_clicked();
protected:
  void paintEvent(QPaintEvent *event);
  void mousePressEvent(QMouseEvent *e);
  void mouseMoveEvent(QMouseEvent *event);
  void leaveEvent(QEvent *event);
  void enterEvent(QEvent *event);
private slots:
  void on_clicked();
  void on_right_clicked();
};

/****************************************************************************
  List of unit actions
****************************************************************************/
class unit_actions: public QWidget
{
  Q_OBJECT
public:
  unit_actions(QWidget *parent, unit *punit);
  ~unit_actions();
  void init_layout();
  int update_actions();
  void clear_layout();
  QHBoxLayout *layout;
  QList<hud_action *> actions;
private:
  unit *current_unit;
};

/****************************************************************************
  Widget showing current unit, tile and possible actions
****************************************************************************/
class hud_units: public QFrame
{
  Q_OBJECT
  click_label unit_label;
  click_label tile_label;
  QLabel text_label;
  QFont *ufont;
  QHBoxLayout *main_layout;
  unit_actions *unit_icons;
public:
  hud_units(QWidget *parent);
  ~hud_units();
  void update_actions(unit_list *punits);
protected:
  void moveEvent(QMoveEvent *event);
private:
  move_widget *mw;
  unit_list *ul_units;
  tile *current_tile;
};

/****************************************************************************
  Widget allowing load units on transport
****************************************************************************/
class hud_unit_loader : public QTableWidget
{
  QList<unit * > transports;
  Q_OBJECT
public:
  hud_unit_loader(struct unit *pcargo, struct tile *ptile);
  ~hud_unit_loader();
  void show_me();
protected slots:
  void selection_changed(const QItemSelection&, const QItemSelection&);
private:
  struct unit *cargo;
  struct tile *qtile;
};

class unit_hud_selector : public QFrame
{
  Q_OBJECT
  QVBoxLayout *main_layout;
  QComboBox *unit_sel_type;
  QPushButton *select;
  QPushButton *cancel;
public:
  unit_hud_selector(QWidget *parent);
  ~unit_hud_selector();
  void show_me();
private slots:
  void select_units(int x = 0);
  void select_units(bool x);
  void uhs_select();
  void uhs_cancel();
protected:
  void keyPressEvent(QKeyEvent *event);
private:
  bool activity_filter(struct unit *punit);
  bool hp_filter(struct unit *punit);
  bool island_filter(struct unit *punit);
  bool type_filter(struct unit *punit);

  QRadioButton *any_activity;
  QRadioButton *fortified;
  QRadioButton *idle;
  QRadioButton *sentried;

  QRadioButton *any;
  QRadioButton *full_mp;
  QRadioButton *full_hp;
  QRadioButton *full_hp_mp;

  QRadioButton *this_tile;
  QRadioButton *this_continent;
  QRadioButton *main_continent;
  QRadioButton *everywhere;

  QRadioButton *this_type;
  QRadioButton *any_type;
  QLabel result_label;
};

#endif /* FC__HUDWIDGET_H */

