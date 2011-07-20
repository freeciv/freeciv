/********************************************************************** 
 Freeciv - Copyright (C) 1996-2005 - Freeciv Development Team
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
#include <QMenuBar>

// client
#include "chatline_common.h"
#include "client_main.h"
#include "clinet.h"
#include "mapview_common.h"
#include "tilespec.h"

// gui-qt
#include "chatline.h"
#include "sprite.h"

#include "fc_client.h"

// Layout
const int MAPVIEW_X = 0;
const int MAPVIEW_Y = 0;
const int TOTAL_WIDTH = 1000;
const int MAPVIEW_WIDTH = TOTAL_WIDTH;
const int MAPVIEW_HEIGHT = 600;
const int OUTPUT_X = 0;
const int OUTPUT_Y = MAPVIEW_Y + MAPVIEW_HEIGHT;
const int OUTPUT_WIDTH = TOTAL_WIDTH;
const int OUTPUT_HEIGHT = 200;
const int CHAT_X = 0;
const int CHAT_Y = OUTPUT_Y + OUTPUT_HEIGHT;
const int CHAT_WIDTH = TOTAL_WIDTH;
const int CHAT_HEIGHT = 30;
const int TOTAL_HEIGHT = CHAT_Y + CHAT_HEIGHT;

/****************************************************************************
  Constructor
****************************************************************************/
fc_client::fc_client() : QObject()
{
  main_window = new QMainWindow;
  central_wdg = new QWidget;

  // PAGE_MAIN
  pages[PAGE_MAIN] = new QWidget(central_wdg);

  page = PAGE_MAIN;

  // PAGE_START
  pages[PAGE_START] = new QWidget(central_wdg);

  pages[PAGE_START]->setVisible(false);

  // PAGE_SCENARIO
  pages[PAGE_SCENARIO] = NULL;

  // PAGE_LOAD
  pages[PAGE_LOAD] = NULL;

  // PAGE_NETWORK
  pages[PAGE_NETWORK] = NULL;

  // PAGE_GAME
  pages[PAGE_GAME] = new QWidget(central_wdg);

  pages[PAGE_GAME]->setVisible(false);

  mapview_wdg = new QGraphicsView(pages[PAGE_GAME]);

  mapview_wdg->setGeometry(MAPVIEW_X, MAPVIEW_Y, MAPVIEW_WIDTH, MAPVIEW_HEIGHT);

  // PAGE_GGZ
  pages[PAGE_GGZ] = NULL;


  // General part not related to any single page
  main_window->setGeometry(0, 0, TOTAL_WIDTH, TOTAL_HEIGHT);

  output_window = new QTextEdit(central_wdg);
  chat_line = new QLineEdit(central_wdg);

  output_window->setReadOnly(true);
  output_window->setGeometry(OUTPUT_X, OUTPUT_Y, OUTPUT_WIDTH, OUTPUT_HEIGHT);

  chat_line->setGeometry(CHAT_X, CHAT_Y, CHAT_WIDTH, CHAT_HEIGHT);
  connect(chat_line, SIGNAL(returnPressed()), this, SLOT(chat()));
  chat_line->setReadOnly(true);

  setup_menus();
  main_window->setCentralWidget(central_wdg);
  main_window->setVisible(true);
}

/****************************************************************************
  Destructor
****************************************************************************/
fc_client::~fc_client()
{
  delete main_window;
}

/****************************************************************************
  Initializes menu system
****************************************************************************/
void fc_client::setup_menus()
{
  QMenu *help_menu;
  QAction *act;

  help_menu = main_window->menuBar()->addMenu(_("Help"));
  act = help_menu->addAction(_("Copying"));
  connect(act, SIGNAL(triggered()), this, SLOT(menu_copying()));
}

/****************************************************************************
  Main part of gui-qt
****************************************************************************/
void fc_client::main(QApplication *qapp)
{
  real_output_window_append(_("This is Qt-client for Freeciv."), NULL, -1);
  chat_welcome_message();

  real_output_window_append(_("In this early Qt-client development phase "
                              "the only way to connect server is via commandline "
                              "autoconnect parameter \"-a\""), NULL, -1);

  tileset_init(tileset);
  tileset_load_tiles(tileset);

  init_mapcanvas_and_overview();
  map_canvas_resized(MAPVIEW_WIDTH, MAPVIEW_HEIGHT);

  set_client_state(C_S_DISCONNECTED);

  startTimer(TIMER_INTERVAL);

  qapp->exec();

  free_mapcanvas_and_overview();
  tileset_free_tiles(tileset);
}

/****************************************************************************
  New line of text for output window received
****************************************************************************/
void fc_client::append_output_window(const QString &str)
{
  output_window->append(str);
}

/****************************************************************************
  Return whether chatline should be active on page.
****************************************************************************/
bool fc_client::chat_active_on_page(enum client_pages check)
{
  if (check == PAGE_START || check == PAGE_GAME) {
    return true;
  }

  return false;
}

/****************************************************************************
  Switch from one client page to another.
****************************************************************************/
void fc_client::switch_page(enum client_pages new_page)
{
  bool chat_old_act;
  bool chat_new_act;

  if (page == new_page || !pages[new_page]) {
    return;
  }

  chat_old_act = chat_active_on_page(page);
  chat_new_act = chat_active_on_page(new_page);

  pages[new_page]->setVisible(true);
  if (chat_new_act && !chat_old_act) {
    chat_line->setReadOnly(false);
  }
  pages[page]->setVisible(false);
  if (!chat_new_act && chat_old_act) {
    chat_line->setReadOnly(true);
  }

  page = new_page;
}

/****************************************************************************
  Returns currently open page
****************************************************************************/
enum client_pages fc_client::current_page()
{
  return page;
}

/****************************************************************************
  Add notifier for server input
****************************************************************************/
void fc_client::add_server_source(int sock)
{
  server_notifier = new QSocketNotifier(sock, QSocketNotifier::Read);

  connect(server_notifier, SIGNAL(activated(int)), this, SLOT(server_input(int)));
}

/****************************************************************************
  There is input from server
****************************************************************************/
void fc_client::server_input(int sock)
{
  input_from_server(sock);
}

/****************************************************************************
  Enter pressed on chatline
****************************************************************************/
void fc_client::chat()
{
  send_chat(chat_line->text().toUtf8().data());
  chat_line->clear();
}

/****************************************************************************
  Copying item selected from Help menu.
****************************************************************************/
void fc_client::menu_copying()
{
  real_output_window_append(_("Freeciv is covered by the GPL."), NULL, -1);
  real_output_window_append(_("See file COPYING distributed with "
                              "freeciv for full license text."), NULL, -1);
}

/****************************************************************************
  Timer event handling
****************************************************************************/
void fc_client::timerEvent(QTimerEvent *event)
{
  // Prevent current timer from repeating with possibly wrong interval
  killTimer(event->timerId());

  // Call timer callback in client common code and
  // start new timer with correct interval
  startTimer(real_timer_callback() * 1000);
}
