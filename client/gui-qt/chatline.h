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

#ifndef FC__CHATLINE_H
#define FC__CHATLINE_H

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

extern "C" {
#include "chatline_g.h"
}

#include "listener.h"

//Qt
#include <QEvent>
#include <QTextBrowser>
#include <QLineEdit>
#include <QCheckBox>

class QPushButton;

QString apply_tags(QString str, const struct text_tag_list *tags,
                   bool colors_change);

/***************************************************************************
  Listener for chat. See listener<> for information about how to use it
***************************************************************************/
class chat_listener : public listener<chat_listener>
{
public:
  virtual void chat_message_received(const QString &,
                                     const struct text_tag_list *);

  void send_chat_message(const QString &message);
};

/***************************************************************************
  Class for chat widget
***************************************************************************/
class chatwdg : public QWidget, private chat_listener
{
  Q_OBJECT
public:
  chatwdg(QWidget *parent);
  void append(const QString &str);
  QLineEdit *chat_line;
  void make_link(struct tile *ptile);
  void update_font();
  void update_widgets();
  int default_size(int lines);
  void scroll_to_bottom();
private slots:
  void send();
  void state_changed(int state);
  void rm_links();
  void anchor_clicked(const QUrl &link);
protected:
  void paint(QPainter *painter, QPaintEvent *event);
  void paintEvent(QPaintEvent *event);
  bool eventFilter(QObject *obj, QEvent *event);
private:
  void chat_message_received(const QString &message,
                             const struct text_tag_list *tags);

  QTextBrowser *chat_output;
  QPushButton *remove_links;
  QCheckBox *cb;

};

class version_message_event : public QEvent
{
  QString message;
public:
  explicit version_message_event(const QString &message);
  QString get_message() const { return message; }
};

#endif                        /* FC__CHATLINE_H */
