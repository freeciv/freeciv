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
#include <QMessageBox>
#include <QElapsedTimer>
#include <QLineEdit>

class QIcon;

/****************************************************************************
  Custom message box with animated background
****************************************************************************/
class hud_message_box: public QMessageBox
{
  Q_OBJECT
  QElapsedTimer m_timer;

public:
  hud_message_box(QWidget *parent);
  void set_text_title(QString s1, QString s2);

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
  Custom message box with animated background
****************************************************************************/
class hud_input_box: public QDialog
{
  Q_OBJECT
  QElapsedTimer m_timer;

public:
  hud_input_box(QWidget *parent);
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


#endif /* FC__HUDWIDGET_H */

