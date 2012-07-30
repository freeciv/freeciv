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

#ifndef FC__FC_CLIENT_H
#define FC__FC_CLIENT_H

// In this case we have to include fc_config.h from header file.
// Some other headers we include demand that fc_config.h must be
// included also. Usually all source files include fc_config.h, but
// there's moc generated meta_fc_client.cpp file which does not.
#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

// client
#include "chatline_common.h"
#include "client_main.h"
#include "clinet.h"
#include "mapview_common.h"
#include "tilespec.h"

// Qt
#include <QApplication>
#include <QLineEdit>
#include <QDialog>
#include <QMainWindow>
#include <QObject>
#include <QSocketNotifier>
#include <QString>
#include <QTextEdit>
#include <QTableWidget>
#include <QLabel>
#include <QTimer>

// common
#include "packets.h"

// client
#include "chatline_common.h"
#include "client_main.h"
#include "clinet.h"
#include "mapview_common.h"
#include "servers.h"
#include "tilespec.h"

// gui-qt
#include "pages.h"
#include "mapview.h"
#include "menu.h"
#include "canvas.h"
#include "ratesdlg.h"
#include "chatline.h"

// Layout
const int MAPVIEW_X = 0;
const int MAPVIEW_Y = 0;
const int TOTAL_WIDTH = 500;
const int MAPVIEW_WIDTH = TOTAL_WIDTH;
const int MAPVIEW_HEIGHT = 600;

enum connection_state {
  LOGIN_TYPE,
  NEW_PASSWORD_TYPE,
  ENTER_PASSWORD_TYPE,
  WAITING_TYPE
};


enum DOCK_WIDGETS {
  OUTPUT_DOCK_WIDGET = 0,
  MESSAGE_DOCK_WIDGET,
  /* ADD NEXT DOCK WIDGETS HERE */
  LAST_WIDGET
};

class MainWindow;

class fc_client : public QObject
{
  Q_OBJECT

  QMainWindow *main_window;
  QWidget *main_wdg;
  QWidget *pages[ (int) PAGE_GGZ + 1];
  QWidget *connect_lan;
  QWidget *connect_metaserver;
  QWidget *game_main_widget;

  QGridLayout *central_layout;
  QGridLayout *pages_layout[PAGE_GGZ + 1];

  QTextEdit *output_window;
  QTextEdit *scenarios_view;

  QLineEdit *connect_host_edit;
  QLineEdit *connect_port_edit;
  QLineEdit *connect_login_edit;
  QLineEdit *connect_password_edit;
  QLineEdit *connect_confirm_password_edit;

  QPushButton *button;

  QDialogButtonBox* button_box;

  QSocketNotifier *server_notifier;

  QLineEdit *chat_line;

  QTabWidget *connect_tab_widget;

  QTableWidget* lan_widget;
  QTableWidget* wan_widget;
  QTableWidget* info_widget;
  QTableWidget* saves_load;
  QTableWidget* scenarios_load;
  QTableWidget* start_players;

  QTimer* meta_scan_timer;
  QTimer* lan_scan_timer;

  QDockWidget *dock_widget[ (int) LAST_WIDGET ];
  QStatusBar *status_bar;
  QLabel *status_bar_label;


public:
  fc_client();
  ~fc_client();

  void main(QApplication *);
  map_view* mapview_wdg;
  void add_server_source(int);

  void switch_page(enum client_pages);
  enum client_pages current_page();

  void append_output_window(const QString &);
  void set_status_bar(QString);
  int add_game_tab(QWidget *widget, QString title, int index);
  void rm_game_tab(int index); /* doesn't delete widget */

  mr_idle mr_idler;
  QTableWidget *messages_window;
  info_label *game_info_label;
  QWidget *central_wdg;
  mr_menu *menu_bar;
  QTabWidget *game_tab_widget;
  int gimme_place();
  int gimme_index_of(QString str);
  void add_repo_dlg(QString str);
  void remove_repo_dlg(QString str);
  bool is_repo_dlg_open(QString str);
  void remove_place(int index);

private slots:

  void server_input(int sock);
  void chat();
  void quit();
  void slot_switch_to_main_page();
  void slot_switch_to_network_page();
  void slot_switch_to_load_page();
  void slot_switch_to_scenario_page();
  void slot_switch_to_start_page();
  void slot_switch_to_game_page();
  void slot_lan_scan();
  void slot_meta_scan();
  void slot_connect();
  void slot_disconnect();
  void slot_pregame_observe();
  void slot_pregame_start();
  void update_network_lists();
  bool slot_close_widget(int index);

protected slots:

  void slot_selection_changed(const QItemSelection&, const QItemSelection&);

private:

  void create_main_page();
  void create_network_page();
  void create_load_page();
  void create_scenario_page();
  void create_start_page();
  void create_game_page();
  bool chat_active_on_page(enum client_pages);
  void show_children(const QLayout*, bool);
  void destroy_server_scans (void);
  void update_server_list(enum server_scan_type,
                           const struct server_list *);
  bool check_server_scan (server_scan*);
  void create_dock_widgets();
  void hide_dock_widgets();
  void show_dock_widget(int);
  void update_load_page(void);
  void update_scenarios_page(void);
  void set_connection_state(enum connection_state state);
  void handle_authentication_req(
    enum authentication_type type, const char *message);

  enum client_pages page;
  bool observer_mode;
  QList<int> places;
  QStringList opened_repo_dlgs;

protected:
  void timerEvent(QTimerEvent *);


signals:
  void keyCaught(QKeyEvent *e);

};

// Return fc_client instance. Implementation in gui_main.cpp
class fc_client *gui();

#endif /* FC__FC_CLIENT_H */


