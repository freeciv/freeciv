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
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QStackedLayout>
#include <QVBoxLayout>

// utility
#include "fcintl.h"
#include "log.h"
#include "registry.h"

// common
#include "game.h"
#include "version.h"

// server
#include "ruleset.h"

// ruledit
#include "requirers_dlg.h"
#include "ruledit.h"
#include "tab_building.h"
#include "tab_misc.h"
#include "tab_nation.h"
#include "tab_tech.h"
#include "tab_unit.h"

#include "ruledit_qt.h"

static ruledit_gui *gui;
static QApplication *qapp;

/**************************************************************************
  Setup ruledit-qt gui.
**************************************************************************/
bool ruledit_qt_setup(int argc, char **argv)
{
  QMainWindow *main_window;
  QWidget *central;
  const QString title = QString::fromUtf8(R__("Freeciv Ruleset Editor"));

  qapp = new QApplication(argc, argv);
  main_window = new QMainWindow;
  main_window->setWindowTitle(title);
  central = new QWidget;

  gui = new ruledit_gui;
  gui->setup(central);
  main_window->setCentralWidget(central);
  main_window->setVisible(true);

  return true;
}

/**************************************************************************
  Execute ruledit-qt gui.
**************************************************************************/
int ruledit_qt_run()
{
  return qapp->exec();
}

/**************************************************************************
  Close ruledit-qt gui.
**************************************************************************/
void ruledit_qt_close()
{
  delete gui;
  delete qapp;
}

/**************************************************************************
  Display requirer list.
**************************************************************************/
void ruledit_qt_display_requirers(const char *msg)
{
  gui->show_required(msg);
}

/**************************************************************************
  Setup GUI object
**************************************************************************/
void ruledit_gui::setup(QWidget *central_in)
{
  QVBoxLayout *full_layout = new QVBoxLayout();
  QVBoxLayout *preload_layout = new QVBoxLayout();
  QWidget *preload_widget = new QWidget();
  QVBoxLayout *edit_layout = new QVBoxLayout();
  QWidget *edit_widget = new QWidget();
  QPushButton *ruleset_accept;
  QLabel *rs_label;
  QLabel *version_label;
  char verbuf[2048];
  const char *rev_ver = fc_svn_revision();

  data.nationlist = NULL;
  data.nationlist_saved = NULL;

  central = central_in;

  if (rev_ver == NULL) {
    rev_ver = fc_git_revision();

    if (rev_ver == NULL) {
      fc_snprintf(verbuf, sizeof(verbuf), "%s%s", word_version(), VERSION_STRING);
    } else {
      fc_snprintf(verbuf, sizeof(verbuf), _("%s%s\ncommit: %s"),
                  word_version(), VERSION_STRING, rev_ver);
    }
  } else {
    fc_snprintf(verbuf, sizeof(verbuf), "%s%s (%s)", word_version(), VERSION_STRING,
                rev_ver);
  }

  main_layout = new QStackedLayout();

  preload_layout->setSizeConstraint(QLayout::SetMaximumSize);
  version_label = new QLabel(verbuf);
  version_label->setAlignment(Qt::AlignHCenter);
  version_label->setParent(central);
  preload_layout->addWidget(version_label);
  rs_label = new QLabel(QString::fromUtf8(R__("Give ruleset to use as starting point.")));
  rs_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  preload_layout->addWidget(rs_label);
  ruleset_select = new QLineEdit(central);
  ruleset_select->setText("classic");
  preload_layout->addWidget(ruleset_select);
  ruleset_accept = new QPushButton(QString::fromUtf8(R__("Start editing")));
  connect(ruleset_accept, SIGNAL(pressed()), this, SLOT(launch_now()));
  preload_layout->addWidget(ruleset_accept);

  preload_widget->setLayout(preload_layout);
  main_layout->addWidget(preload_widget);

  stack = new QTabWidget(central);

  misc = new tab_misc(this);
  stack->addTab(misc, QString::fromUtf8(R__("Misc")));
  tech = new tab_tech(this);
  stack->addTab(tech, QString::fromUtf8(R__("Tech")));
  bldg = new tab_building(this);
  stack->addTab(bldg, QString::fromUtf8(R__("Buildings")));
  unit = new tab_unit(this);
  stack->addTab(unit, QString::fromUtf8(R__("Units")));
  nation = new tab_nation(this);
  stack->addTab(nation, QString::fromUtf8(R__("Nations")));

  edit_layout->addWidget(stack);

  edit_widget->setLayout(edit_layout);
  main_layout->addWidget(edit_widget);

  full_layout->addLayout(main_layout);

  msg_dspl = new QLabel(QString::fromUtf8(R__("Welcome to freeciv-ruledit")));
  msg_dspl->setParent(central);

  msg_dspl->setAlignment(Qt::AlignHCenter);

  full_layout->addWidget(msg_dspl);

  requirers = new requirers_dlg(this);

  central->setLayout(full_layout);
}

/**************************************************************************
  User entered savedir
**************************************************************************/
void ruledit_gui::launch_now()
{
  sz_strlcpy(game.server.rulesetdir, ruleset_select->text().toUtf8().data());

  if (load_rulesets(NULL, TRUE, FALSE, TRUE)) {
    display_msg(R__("Ruleset loaded"));

    /* Make freeable copy */
    if (game.server.ruledit.nationlist != NULL) {
      data.nationlist = fc_strdup(game.server.ruledit.nationlist);
    } else {
      data.nationlist = NULL;
    }

    bldg->refresh();
    misc->refresh();
    nation->refresh();
    tech->refresh();
    unit->refresh();
    main_layout->setCurrentIndex(1);
  } else {
    display_msg(R__("Ruleset loading failed!"));
  }
}

/**************************************************************************
  Display status message
**************************************************************************/
void ruledit_gui::display_msg(const char *msg)
{
  msg_dspl->setText(QString::fromUtf8(msg));
}

/**************************************************************************
  Clear requirers dlg.
**************************************************************************/
void ruledit_gui::clear_required(const char *title)
{
  requirers->clear(title);
}

/**************************************************************************
  Add entry to requirers dlg.
**************************************************************************/
void ruledit_gui::show_required(const char *msg)
{
  requirers->add(msg);

  // Show dialog if not already visible
  requirers->show();
}

/**************************************************************************
  Flush information from widgets to stores where it can be saved from.
**************************************************************************/
void ruledit_gui::flush_widgets()
{
  nation->flush_widgets();
}
