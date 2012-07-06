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

#ifndef FC__MPGUI_QT_H
#define FC__MPGUI_QT_H

// Qt
#include <QObject>
#include <QLabel>
#include <QLineEdit>

class mpgui : public QObject
{
  Q_OBJECT

  public:
    void setup(QWidget *central);
    void display_msg(const char *msg);

  private slots:
    void URL_given();

  private:

    QLineEdit *URLedit;
    QLabel *msg_dspl;
};

#endif // FC__MPGUI_QT_H
