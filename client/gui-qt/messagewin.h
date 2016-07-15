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

#ifndef FC__MESSAGEWIN_H
#define FC__MESSAGEWIN_H

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

extern "C" {
#include "messagewin_g.h"
}

//Qt
#include <QTableWidget>
#include <QGridLayout>
#include <QPushButton>

//qt-client
#include "chatline.h"
#include "mapview.h"


/***************************************************************************
  QPushButton receiving right click event
***************************************************************************/
class right_click_button : public QPushButton
{
  Q_OBJECT
public:
  explicit right_click_button(QWidget *parent = 0);
signals:
  void right_clicked();
protected:
  void mousePressEvent(QMouseEvent *e);
};

/***************************************************************************
  Class representing message output
***************************************************************************/
class messagewdg : public QWidget
{
  Q_OBJECT
public:
  messagewdg(QWidget *parent);
  void msg_update();
  void clr();
  void msg(const struct message *pmsg);
private:
  QTableWidget *mesg_table;
  QGridLayout *layout;
  QPixmap *pix;
protected:
  void paint(QPainter *painter, QPaintEvent *event);
  void paintEvent(QPaintEvent *event);
  void resizeEvent(QResizeEvent *event);
public slots:
  void item_selected(const QItemSelection &sl, const QItemSelection &ds);
};

/***************************************************************************
  Class which manages chat and messages
***************************************************************************/
class info_tab : public fcwidget
{
  Q_OBJECT
public:
  info_tab(QWidget *parent);
  QGridLayout *layout;
  messagewdg *msgwdg;
  chatwdg *chtwdg;
  void hide_chat(bool hyde);
  void hide_messages(bool hyde);
  bool hidden_chat;
  bool locked;
  bool hidden_mess;
  int whats_hidden;
private:
  void change_layout();
  void update_menu();
  QPoint cursor;
  right_click_button *chat_button;
  QPushButton *hide_button;
  QPushButton *lock_button;
  right_click_button *msg_button;
  QSize last_size;
  bool hidden_state;
  bool layout_changed;
  bool resize_mode;
  bool resx;
  bool resy;
  int chat_stretch;
  int msg_stretch;
public slots:
  void hide_me();
  void lock();
private slots:
  void activate_msg();
  void activate_chat();
  void on_right_clicked();
protected:
  void paint(QPainter *painter, QPaintEvent *event);
  void paintEvent(QPaintEvent *event);
  void mousePressEvent(QMouseEvent *event);
  void mouseMoveEvent(QMouseEvent *event);
  void mouseReleaseEvent(QMouseEvent *event);
};

#endif /* FC__MESSAGEWIN_H */
