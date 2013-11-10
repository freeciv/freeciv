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

#ifndef FC__RULEDIT_QT_H
#define FC__RULEDIT_QT_H

// Qt
#include <QApplication>
#include <QObject>
#include <QLabel>
#include <QLineEdit>

class ruledit_gui : public QObject
{
  Q_OBJECT

  public:
    void setup(QApplication *qapp, QWidget *central);
    void display_msg(const char *msg);
    int run();
    void close();

  private slots:
    void savedir_given();

  private:
    QApplication *app;
    QLabel *msg_dspl;

    QLineEdit *savedir;
};

bool ruledit_qt_setup(int argc, char **argv);
int ruledit_qt_run();
void ruledit_qt_close();

#endif // FC__RULEDIT_QT_H
