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

#include "fc_client.h"
#include "optiondlg.h"

extern QApplication *qapp;

/****************************************************************************
  Constructor
****************************************************************************/
fc_client::fc_client() : QObject()
{
  QTextCodec::setCodecForCStrings(QTextCodec::codecForName("UTF-8"));
  /**
   * Somehow freeciv-client-common asks to switch to page when all widgets
   * haven't been created yet by Qt, even constructor finished job,
   * so we null all Qt objects, so while switching we will know if they
   * were created.
   * After adding new QObjects null them here.
   */
  main_window = NULL;
  main_wdg = NULL;
  connect_lan = NULL;
  connect_metaserver = NULL;
  central_layout = NULL;
  output_window = NULL;
  scenarios_view = NULL;
  connect_host_edit = NULL;
  connect_port_edit = NULL;
  connect_login_edit = NULL;
  connect_password_edit = NULL;
  connect_confirm_password_edit = NULL;
  button = NULL;
  button_box = NULL;
  server_notifier = NULL;
  chat_line = NULL;
  connect_tab_widget = NULL;
  lan_widget = NULL;
  wan_widget = NULL;
  info_widget = NULL;
  saves_load = NULL;
  scenarios_load = NULL;
  start_players = NULL;
  meta_scan_timer = NULL;
  lan_scan_timer = NULL;
  status_bar = NULL;
  status_bar_label = NULL;
  menu_bar = NULL;
  mapview_wdg = NULL;
  messages_window = NULL;
  game_info_label = NULL;
  central_wdg = NULL;
  game_tab_widget = NULL;
  ver_dock_layout = NULL;
  start_players_tree = NULL;

  for (int i = 0; i < LAST_WIDGET; i++) {
    dock_widget[i] = NULL;
  }

  for (int i = 0; i <= PAGE_GGZ; i++) {
    pages_layout[i] = NULL;
    pages[i] = NULL;
  }
  main_window = new QMainWindow;
  central_wdg = new QWidget;
  central_layout = new QGridLayout;

  // General part not related to any single page
  create_dock_widgets();
  menu_bar = new mr_menu();
  menu_bar->setup_menus();
  main_window->setMenuBar(menu_bar);
  status_bar = main_window->statusBar();
  status_bar_label = new QLabel;
  status_bar_label->setAlignment(Qt::AlignCenter);
  status_bar->addWidget(status_bar_label, 1);
  set_status_bar(_("Welcome to Freeciv"));

  switch_page_mapper = new QSignalMapper;
  // PAGE_MAIN
  pages[PAGE_MAIN] = new QWidget(central_wdg);
  page = PAGE_MAIN;
  create_main_page();

  // PAGE_START
  pages[PAGE_START] = new QWidget(central_wdg);
  create_start_page();

  // PAGE_SCENARIO
  pages[PAGE_SCENARIO] = new QWidget(central_wdg);
  create_scenario_page();

  // PAGE_LOAD
  pages[PAGE_LOAD] = new QWidget(central_wdg);
  create_load_page();

  // PAGE_NETWORK
  pages[PAGE_NETWORK] = new QWidget(central_wdg);
  create_network_page();
  meta_scan_timer = new QTimer;
  lan_scan_timer = new QTimer;
  pages[PAGE_NETWORK]->setVisible(false);

  // PAGE_GAME
  pages[PAGE_GAME] = new QWidget(central_wdg);
  init_mapcanvas_and_overview();
  map_canvas_resized (MAPVIEW_WIDTH, MAPVIEW_HEIGHT);
  create_game_page();
  pages[PAGE_GAME]->setVisible(false);

  // PAGE_GGZ
  pages[PAGE_GGZ] = NULL;
  central_layout->addLayout(pages_layout[PAGE_MAIN], 1, 1);
  central_layout->addLayout(pages_layout[PAGE_NETWORK], 1, 1);
  central_layout->addLayout(pages_layout[PAGE_LOAD], 1, 1);
  central_layout->addLayout(pages_layout[PAGE_SCENARIO], 1, 1);
  central_layout->addLayout(pages_layout[PAGE_START], 1, 1);
  central_layout->addLayout(pages_layout[PAGE_GAME], 1, 1);
  central_wdg->setLayout(central_layout);
  main_window->setCentralWidget(central_wdg);
  main_window->addDockWidget(Qt::RightDockWidgetArea,
                             dock_widget[ (int) OUTPUT_DOCK_WIDGET]);
  main_window->addDockWidget(Qt::BottomDockWidgetArea,
                             dock_widget[ (int) MESSAGE_DOCK_WIDGET]);

  connect(switch_page_mapper, SIGNAL(mapped( int)),
                this, SLOT(switch_page(int)));
  fc_fonts.init_fonts();
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
  Main part of gui-qt
****************************************************************************/
void fc_client::main(QApplication *qapp)
{

  qRegisterMetaType<QTextCursor> ("QTextCursor");
  real_output_window_append(_("This is Qt-client for Freeciv."), NULL, -1);
  chat_welcome_message();

  real_output_window_append(_("In this early Qt-client development phase "
                              "the only way to connect server is via"
                              "commandline autoconnect parameter \"-a\""),
                            NULL, -1);

  set_client_state(C_S_DISCONNECTED);

  startTimer(TIMER_INTERVAL);
  qapp->exec();

  free_mapcanvas_and_overview();
  tileset_free_tiles(tileset);
}

/****************************************************************************
  Appends text to chat window
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
  Argument is int cause QSignalMapper doesn't want to work with enum
  Because chat widget is in 2 layouts we need to switch between them here
  (addWidget removes it from prevoius layout automatically)
****************************************************************************/
void fc_client::switch_page(int new_pg)
{
  char buf[256];
  enum client_pages new_page;
  new_page = static_cast<client_pages>(new_pg);
  main_window->menuBar()->setVisible(false);
  hide_dock_widgets();
  ver_dock_layout->addWidget(output_window);
  ver_dock_layout->addWidget(chat_line);

  for (int i = 0; i < PAGE_GGZ + 1; i++) {
    if (i == new_page) {
      show_children(pages_layout[i], true);
    } else {
      show_children(pages_layout[i], false);
    }
  }

  page = new_page;
  switch (page) {
  case PAGE_MAIN:
    show_dock_widget(static_cast<int>(OUTPUT_DOCK_WIDGET), true);
    break;
  case PAGE_START:
    pages_layout[PAGE_START]->addWidget(chat_line, 5, 0, 1, 3);
    pages_layout[PAGE_START]->addWidget(output_window,3,0,2,8);
    break;
  case PAGE_LOAD:
    update_load_page();
    break;
  case PAGE_GAME:
    main_window->menuBar()->setVisible(true);
    show_dock_widget(static_cast<int>(OUTPUT_DOCK_WIDGET), true);
    show_dock_widget(static_cast<int>(MESSAGE_DOCK_WIDGET), true);
    mapview_wdg->setFocus();
    break;
  case PAGE_SCENARIO:
    update_scenarios_page();
    break;
  case PAGE_NETWORK:
    hide_dock_widgets();
    update_network_lists();
    connect_host_edit->setText(server_host);
    fc_snprintf(buf, sizeof(buf), "%d", server_port);
    connect_port_edit->setText(buf);
    connect_login_edit->setText(user_name);
    break;
  case PAGE_GGZ:
    break;
  }
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
  real_output_window_append(chat_line->text().toUtf8().data(), NULL, -1);
  chat_line->clear();
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

/****************************************************************************
  Quit App
****************************************************************************/
void fc_client::quit()
{
  qapp->quit();
}

/****************************************************************************
  Disconnect from server and return to MAIN PAGE
****************************************************************************/
void fc_client::slot_disconnect()
{
  if (client.conn.used) {
    disconnect_from_server();
  }

  switch_page(PAGE_MAIN);
}

/****************************************************************************
  User clicked Observe button in START_PAGE
****************************************************************************/
void fc_client::slot_pregame_observe()
{
  if (client_is_observer() || client_is_global_observer()) {
    send_chat("/take -");
    obs_button->setText(_("Don't Observe"));
  } else {
    send_chat("/obs");
    obs_button->setText(_("Observe"));
  }
}

/****************************************************************************
  User clicked Start in START_PAGE
****************************************************************************/
void fc_client::slot_pregame_start()
{
  send_chat("/start");
}

/****************************************************************************
  Shows all widgets for given layout
****************************************************************************/
void fc_client::show_children(const QLayout* layout, bool show)
{
  QLayoutItem *item = NULL;
  QWidget *widget = NULL;

  if (layout) {
    for (int i = 0; i < layout->count(); i++) {
      item = layout->itemAt(i);
      widget = item ? item->widget() : 0;

      if (widget) {
        widget->setVisible(show);
      }

      if (item->layout()) {
        show_children(item->layout(), show);
      }
    }
  }
}

/****************************************************************************
  Finds not used index on game_view_tab and returns it
****************************************************************************/
int fc_client::gimme_place()
{
  int is_not_in;

  for (int i = 1; i < 1000; i++) {
    is_not_in = places.indexOf(i);

    if (is_not_in == -1) {
      places.append(i);
      places.indexOf(i);
      return i;
    }
  }

  log_error("Failed to find place for new tab widget");
  return 0;
}

/****************************************************************************
  Removes given index from list of used indexes
****************************************************************************/
void fc_client::remove_place(int index)
{
  places.removeAll(index);
}

/****************************************************************************
  Checks if given report is opened, if you create new report as tab on game
  page, figure out some original string and put in in repodlg.h as comment to
  that QWidget class.
****************************************************************************/
bool fc_client::is_repo_dlg_open(QString str)
{
  int i;

  i = opened_repo_dlgs.indexOf(str);

  if (i == -1) {
    return false;
  }

  return true;
}

/****************************************************************************
  Returns index on game tab page of given report dialog 
****************************************************************************/
int fc_client::gimme_index_of(QString str)
{
  int i;

  i = opened_repo_dlgs.indexOf(str);

  return places.at(i);
}

/****************************************************************************
  Adds new report dialog string to the list marking it as opened
****************************************************************************/
void fc_client::add_repo_dlg(QString str)
{
  opened_repo_dlgs.append(str);
}

/****************************************************************************
  Removes report dialog string from the list marking it as closed
****************************************************************************/
void fc_client::remove_repo_dlg(QString str)
{
  opened_repo_dlgs.removeAll(str);
}

/****************************************************************************
  Popups client options
****************************************************************************/
void fc_client::popup_client_options()
{
  option_dialog_popup(_("Set local options"), client_optset);
}

/****************************************************************************
  Popups client options
****************************************************************************/
void fc_client::popup_server_options()
{
  option_dialog_popup(_("Set server options"), server_optset);
}

/****************************************************************************
  Returns desired font
****************************************************************************/
QFont *fc_font::get_font(QString name)
{
  /**
   * example: get_font("gui_qt_font_city_label")
   */

  if (font_map.contains(name)) {
    return font_map[name];
  } else {
    return NULL;
  }
}

/****************************************************************************
  Initiazlizes fonts from client options
****************************************************************************/
void fc_font::init_fonts()
{
  QFont *f;
  QString s;

  /**
   * default font names are:
   * gui_qt_font_city_label
   * gui_qt_font_notify_label and so on.
   * (full list is in options.c in client dir)
   */

  options_iterate(client_optset, poption) {
    if (option_type(poption) == OT_FONT) {
      f = new QFont;
      s = option_font_get(poption);
      f->fromString(s);
      s = option_name(poption);
      set_font(s, f);
    }
  } options_iterate_end;
}

/****************************************************************************
  Adds new font or overwrite old one
****************************************************************************/
void fc_font::set_font(QString name, QFont * qf)
{
  if (font_map.contains(name)) {
    font_map.remove(name);
  };
  font_map[name] = qf;
}
