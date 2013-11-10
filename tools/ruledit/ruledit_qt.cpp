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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

// Qt
#include <QApplication>
#include <QLabel>
#include <QMainWindow>

// utility
#include "fcintl.h"
#include "log.h"
#include "registry.h"

// ruledit
#include "ruledit.h"
#include "rulesave.h"

#include "ruledit_qt.h"

static ruledit_gui *gui;

/**************************************************************************
  Setup ruledit-qt gui.
**************************************************************************/
bool ruledit_qt_setup(int argc, char **argv)
{
  QApplication *qapp;
  QMainWindow *main_window;
  QWidget *central;

  qapp = new QApplication(argc, argv);
  main_window = new QMainWindow;
  main_window->setGeometry(0, 30, 640, 60);
  main_window->setWindowTitle(_("Freeciv ruleset editor"));
  central = new QWidget;

  gui = new ruledit_gui;
  gui->setup(qapp, central);
  main_window->setCentralWidget(central);
  main_window->setVisible(true);

  return true;
}

/**************************************************************************
  Execute ruledit-qt gui.
**************************************************************************/
int ruledit_qt_run()
{
  return gui->run();
}

/**************************************************************************
  Close ruledit-qt gui.
**************************************************************************/
void ruledit_qt_close()
{
  gui->close();

  delete gui;
}

/**************************************************************************
  Setup GUI object
**************************************************************************/
void ruledit_gui::setup(QApplication *qapp, QWidget *central)
{
#define SAVE_LABEL_TEXT "Save to directory"

  QLabel *save_label;
  QFont font("times", 12);
  QFontMetrics fm(font);
  int label_len = fm.width(_(SAVE_LABEL_TEXT));
  int label_end = label_len + 5;

  app = qapp;

  save_label = new QLabel(_(SAVE_LABEL_TEXT));
  save_label->setGeometry(5, 0, label_len, 30);
  save_label->setParent(central);

  savedir = new QLineEdit(central);
  savedir->setText("ruledit-tmp");
  savedir->setGeometry(label_end, 0, fm.width("ruledit-template"), 30);
  savedir->setFocus();
  connect(savedir, SIGNAL(returnPressed()), this, SLOT(savedir_given()));

  msg_dspl = new QLabel(_("Welcome to freeciv-ruledit"));
  msg_dspl->setParent(central);

  msg_dspl->setGeometry(0, 40, 640, 30);
  msg_dspl->setAlignment(Qt::AlignHCenter);
}

/**************************************************************************
  Execute GUI object
**************************************************************************/
int ruledit_gui::run()
{
  app->exec();

  return EXIT_SUCCESS;
}

/**************************************************************************
  Close GUI object
**************************************************************************/
void ruledit_gui::close()
{
  delete app;
}

/**************************************************************************
  Display status message
**************************************************************************/
void ruledit_gui::display_msg(const char *msg)
{
  msg_dspl->setText(msg);
}

/**************************************************************************
  User entered savedir
**************************************************************************/
void ruledit_gui::savedir_given()
{
  save_ruleset(savedir->text().toUtf8().data(), ruleset_name());

  display_msg(_("Ruleset saved"));
}
