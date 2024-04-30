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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

// Qt
#include <QGridLayout>
#include <QPushButton>

// utility
#include "fcintl.h"
#include "mem.h"
#include "string_vector.h"

// ruledit
#include "values_dlg.h"

#include "helpeditor.h"

/**********************************************************************//**
  Setup helpeditor object
**************************************************************************/
helpeditor::helpeditor(values_dlg *parent_dlg, struct strvec *helptext_in) : QDialog()
{
  QGridLayout *main_layout = new QGridLayout(this);
  QPushButton *close_button;
  int row = 0;
  int i;

  pdlg = parent_dlg;
  helptext = helptext_in;

  helps = (QTextEdit **)fc_malloc(sizeof(QTextEdit *) * strvec_size(helptext));

  i = 0;
  strvec_iterate(helptext, text) {
    helps[i] = new QTextEdit(QString::fromUtf8(text));
    helps[i]->setParent(this);
    main_layout->addWidget(helps[i++], row++, 0);
  } strvec_iterate_end;

  close_button = new QPushButton(QString::fromUtf8(R__("Close")), this);
  connect(close_button, SIGNAL(pressed()), this, SLOT(close_now()));
  main_layout->addWidget(close_button, row++, 0);

  setLayout(main_layout);
}

/**********************************************************************//**
  Close helpeditor
**************************************************************************/
void helpeditor::close()
{
  int i;
  int count = strvec_size(helptext);

  for (i = 0; i < count; i++) {
    QByteArray hbytes;

    hbytes = helps[i]->toPlainText().toUtf8();
    strvec_set(helptext, i, hbytes.data());
  }

  free(helps);

  strvec_remove_empty(helptext);

  done(0);
}

/**********************************************************************//**
  User pushed close button
**************************************************************************/
void helpeditor::close_now()
{
  // Both closes this dialog, and handles parent's own bookkeeping
  pdlg->close_help();
}
