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
  bool hidden_mess;
private:
  QPushButton *msg_button;
  QPushButton *chat_button;
  QPushButton *hide_button;
  QSize last_size;
  bool hidden_state;
  bool layout_changed;
  void change_layout();
  void update_menu();
  bool resize_mode;
  QPoint cursor;
public slots:
  void hide_me();
private slots:
  void activate_msg();
  void activate_chat();
protected:
  void paint(QPainter *painter, QPaintEvent *event);
  void paintEvent(QPaintEvent *event);
  void mousePressEvent(QMouseEvent *event);
  void mouseMoveEvent(QMouseEvent *event);
  void mouseReleaseEvent(QMouseEvent *event);
};

#endif /* FC__MESSAGEWIN_H */
