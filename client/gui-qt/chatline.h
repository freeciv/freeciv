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

//Qt
#include <QTextEdit>
#include <QLineEdit>
#include <QCheckBox>

/***************************************************************************
  Class for chat widget
***************************************************************************/
class chatwdg : public QWidget
{
  Q_OBJECT
public:
  chatwdg(QWidget *parent);
  void append(QString str);
private slots:
  void send();
  void state_changed(int state);
protected:
  void paint(QPainter *painter, QPaintEvent *event);
  void paintEvent(QPaintEvent *event);
private:
  QTextEdit *chat_output;
  QLineEdit *chat_line;
  QCheckBox *cb;

};

#endif                        /* FC__CHATLINE_H */
