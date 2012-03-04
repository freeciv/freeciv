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

// modinst
#include "download.h"
#include "mpcmdline.h"
#include "mpdb.h"
#include "modinst.h"

#include "mpgui_qt.h"

struct fcmp_params fcmp = { MODPACK_LIST_URL, NULL };

static mpgui *gui;

/**************************************************************************
  Entry point for whole freeciv-mp-qt program.
**************************************************************************/
int main(int argc, char **argv)
{
  enum log_level loglevel = LOG_NORMAL;
  int ui_options;

  registry_module_init();
  log_init(NULL, loglevel, NULL, NULL, -1);

  /* This modifies argv! */
  ui_options = fcmp_parse_cmdline(argc, argv);

  if (ui_options != -1) {
    QApplication *qapp;
    QMainWindow *main_window;
    QWidget *central;

    load_install_info_lists(&fcmp);

    qapp = new QApplication(ui_options, argv);
    main_window = new QMainWindow;
    central = new QWidget;

    main_window->setGeometry(0, 30, 640, 60);
    main_window->setWindowTitle(_("Freeciv modpack installer (Qt)"));

    gui = new mpgui;

    gui->setup(central);

    main_window->setCentralWidget(central);
    main_window->setVisible(true);

    qapp->exec();

    delete gui;
    delete qapp;

    save_install_info_lists();
  }

  log_close();
  registry_module_close();

  return EXIT_SUCCESS;
}

/**************************************************************************
  Progress indications from downloader
**************************************************************************/
static void msg_callback(const char *msg)
{
  gui->display_msg(msg);
}

/**************************************************************************
  Setup GUI object
**************************************************************************/
void mpgui::setup(QWidget *central)
{
#define URL_LABEL_TEXT "Modpack URL"

  QLabel *URL_label;
  QFont font("times", 12);
  QFontMetrics fm(font);
  int label_len = fm.width(_(URL_LABEL_TEXT));
  int label_end = label_len + 5;

  URL_label = new QLabel(_(URL_LABEL_TEXT));
  URL_label->setGeometry(5, 0, label_len, 30);
  URL_label->setParent(central);

  URLedit = new QLineEdit(central);
  URLedit->setText(DEFAULT_URL_START);
  URLedit->setGeometry(label_end, 0, fm.width(EXAMPLE_URL), 30);
  URLedit->setFocus();

  connect(URLedit, SIGNAL(returnPressed()), this, SLOT(URL_given()));

  msg_dspl = new QLabel(_("Select modpack to install"));
  msg_dspl->setParent(central);

  msg_dspl->setGeometry(0, 40, 640, 30);
  msg_dspl->setAlignment(Qt::AlignHCenter);
}

/**************************************************************************
  Display status message
**************************************************************************/
void mpgui::display_msg(const char *msg)
{
  msg_dspl->setText(msg);
}

/**************************************************************************
  User entered URL
**************************************************************************/
void mpgui::URL_given()
{
  const char *errmsg;

  errmsg = download_modpack(URLedit->text().toUtf8().data(),
			    &fcmp, msg_callback, NULL);

  if (errmsg != NULL) {
    display_msg(errmsg);
  } else {
    display_msg(_("Ready"));
  }
}
