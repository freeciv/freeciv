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

// Qt
#include <QApplication>
#include <QGraphicsView>
#include <QMainWindow>
#include <QObject>
#include <QString>
#include <QTextEdit>

class fc_client : public QObject
{
 public:
  fc_client();
  ~fc_client();

  void main(QApplication *qapp);

  void append_output_window(const QString &str);

 private:
  QMainWindow *main_window;
  QGraphicsView *central_wdg;
  QTextEdit *output_window;

protected:
  void timerEvent(QTimerEvent *event);
};

// Return fc_client instance. Implementation in gui_main.cpp
class fc_client *gui();

#endif /* FC__FC_CLIENT_H */
