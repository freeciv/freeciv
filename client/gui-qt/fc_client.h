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

#ifndef FC__FC_CLIENT_H
#define FC__FC_CLIENT_H

// In this case we have to include fc_config.h from header file.
// Some other headers we include demand that fc_config.h must be
// included also. Usually all source files include fc_config.h, but
// there's moc generated meta_fc_client.cpp file which does not.
#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

// Qt
#include <QApplication>
#include <QGraphicsView>
#include <QLineEdit>
#include <QMainWindow>
#include <QObject>
#include <QSocketNotifier>
#include <QString>
#include <QTextEdit>

// gui-qt
#include "pages.h"


class fc_client : public QObject
{
  Q_OBJECT

 public:
  fc_client();
  ~fc_client();

  void main(QApplication *qapp);

  void add_server_source(int sock);

  void switch_page(enum client_pages new_page);
  enum client_pages current_page();

  void append_output_window(const QString &str);

 private slots:
  void server_input(int sock);
  void chat();
  void menu_copying();

 private:
  void setup_menus();
  bool chat_active_on_page(enum client_pages check);

  QMainWindow *main_window;
  QWidget *central_wdg;
  QWidget *pages[(int) PAGE_GGZ + 1];
  QGraphicsView *mapview_wdg;

  QTextEdit *output_window;
  QLineEdit *chat_line;

  QSocketNotifier *server_notifier;

  enum client_pages page;

 protected:
  void timerEvent(QTimerEvent *event);
};

// Return fc_client instance. Implementation in gui_main.cpp
class fc_client *gui();

#endif /* FC__FC_CLIENT_H */
