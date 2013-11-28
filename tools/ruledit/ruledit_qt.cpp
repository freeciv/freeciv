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
#include <QVBoxLayout>

// utility
#include "fcintl.h"
#include "log.h"
#include "registry.h"

// ruledit
#include "tab_misc.h"
#include "tab_tech.h"
#include "ruledit.h"

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
  main_window->setWindowTitle(R__("Freeciv ruleset editor"));
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
  tab_misc *misc;
  tab_tech *tech;
  QVBoxLayout *main_layout = new QVBoxLayout(central);

  app = qapp;

  msg_dspl = new QLabel(R__("Welcome to freeciv-ruledit"));
  msg_dspl->setParent(central);

  msg_dspl->setAlignment(Qt::AlignHCenter);

  stack = new QTabWidget(central);

  misc = new tab_misc(central, this);
  stack->addTab(misc, R__("Misc"));
  tech = new tab_tech(central, this);
  stack->addTab(tech, R__("Tech"));

  main_layout->addWidget(stack);
  main_layout->addWidget(msg_dspl);

  central->setLayout(main_layout);
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
