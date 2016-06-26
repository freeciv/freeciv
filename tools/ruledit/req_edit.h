/***********************************************************************
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

#ifndef FC__REQ_EDIT_H
#define FC__REQ_EDIT_H

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

// Qt
#include <QDialog>
#include <QTextEdit>

class ruledit_gui;

class req_edit : public QDialog
{
  Q_OBJECT

  public:
  explicit req_edit(ruledit_gui *ui_in, QString target,
                    const struct requirement_vector *preqs);
    void clear(const char *title);
    void add(const char *msg);

  private:
    ruledit_gui *ui;

    QTextEdit *area;

  private slots:
    void close_now();
};


#endif // FC__REQ_EDIT_H
