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

#include "shortcuts.h"

class QIcon;
class QHBoxLayout;
class QLabel;
class move_widget;
struct unit_list;
struct tile;
struct unit;

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


#endif /* FC__HUDWIDGET_H */

