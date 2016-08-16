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

// Qt
#include <QComboBox>
#include <QMainWindow>
#include <QMap>
#include <QSpinBox>
#include <QTabWidget>

// client
#include "chatline_common.h"
#include "client_main.h"
#include "clinet.h"
#include "mapview_common.h"
#include "tilespec.h"

// common
#include "packets.h"

// client
#include "chatline_common.h"
#include "client_main.h"
#include "climisc.h"
#include "clinet.h"
#include "mapview_common.h"
#include "servers.h"
#include "tilespec.h"

// gui-qt
#include "canvas.h"
#include "chatline.h"
#include "dialogs.h"
#include "gotodlg.h"
#include "mapview.h"
#include "messagewin.h"
#include "menu.h"
#include "pages.h"
#include "ratesdlg.h"
#include "voteinfo_bar.h"

enum connection_state {
  LOGIN_TYPE,
  NEW_PASSWORD_TYPE,
  ENTER_PASSWORD_TYPE,
  WAITING_TYPE
};

class MainWindow;
class QCompleter;
class QLabel;
class QLineEdit;
class QLineEdit;
class QString;
class QTableWidget;
class QTextEdit;
class QTimer;
class QSocketNotifier;
class QDialog;
class QApplication;
class QTreeWidget;
class QStatusBar;
class QMainWindow;
class pregame_options;

class fc_icons
{
  Q_DISABLE_COPY(fc_icons);

private:
  explicit fc_icons();
  static fc_icons* m_instance;

public:
  static fc_icons* instance();
  static void drop();
  QIcon get_icon(const QString& id);
  QString get_path(const QString& id);
};


class fc_game_tab_widget: public QTabWidget
{
  Q_OBJECT
public:
  fc_game_tab_widget();
  void change_color(int index, QColor col);
private slots:
  void restore_label_color(int index);
};

class fc_font
{
  QMap <QString, QFont *> font_map;
public:
  void set_font(QString name, QFont *qf);
  QFont* get_font(QString name);
  void init_fonts();
  void release_fonts();
};

/****************************************************************************
  Some qt-specific options like size to save between restarts
****************************************************************************/
struct fc_settings
{
  int infotab_width; /* in percent */
  int infotab_height; /* in percent */
  int player_repo_sort_col;
  Qt::SortOrder player_report_sort;
  int city_repo_sort_col;
  Qt::SortOrder city_report_sort;
};

class fc_client : public QMainWindow
{
  Q_OBJECT
  QWidget *main_wdg;
  QWidget *pages[ (int) PAGE_GGZ + 1];
  QWidget *connect_lan;
  QWidget *connect_metaserver;
  QWidget *game_main_widget;

  QGridLayout *central_layout;
  QGridLayout *pages_layout[PAGE_GGZ + 1];

  QTextEdit *output_window;
  QTextEdit *scenarios_view;
  QLabel *scenarios_text;

  QLineEdit *connect_host_edit;
  QLineEdit *connect_port_edit;
  QLineEdit *connect_login_edit;
  QLineEdit *connect_password_edit;
  QLineEdit *connect_confirm_password_edit;

  QPushButton *button;
  QPushButton *obs_button;

  QDialogButtonBox* button_box;

  QSocketNotifier *server_notifier;

  QLineEdit *chat_line;

  QTableWidget* lan_widget;
  QTableWidget* wan_widget;
  QTableWidget* info_widget;
  QTableWidget* saves_load;
  QTableWidget* scenarios_load;
  QTableWidget* start_players;
  QTreeWidget* start_players_tree;

  QTimer* meta_scan_timer;
  QTimer* lan_scan_timer;
  QTimer *update_info_timer;

  QStatusBar *status_bar;
  QSignalMapper *switch_page_mapper;
  QLabel *status_bar_label;
  info_tile *info_tile_wdg;
  choice_dialog *opened_dialog;
  int current_unit_id;
  int current_unit_target_id;

public:
  fc_client();
  ~fc_client();

  void main(QApplication *);
  map_view *mapview_wdg;
  minimap_view *minimapview_wdg;
  unit_label *unitinfo_wdg;
  void add_server_source(int);
  void remove_server_source();
  bool event(QEvent *event);

  enum client_pages current_page();

  void append_output_window(const QString &);
  void set_status_bar(QString str, int timeout = 2000);
  int add_game_tab(QWidget *widget, QString title);
  void rm_game_tab(int index); /* doesn't delete widget */
  void update_start_page();
  void toggle_unit_sel_widget(struct tile *ptile);
  void update_unit_sel();
  void popdown_unit_sel();
  void popup_tile_info(struct tile *ptile);
  void popdown_tile_info();
  void set_current_unit(int curr, int target);
  void get_current_unit(int *curr, int *target);
  void set_diplo_dialog(choice_dialog *widget);
  void update_completer();
  void handle_authentication_req(enum authentication_type type,
                                 const char *message);
  choice_dialog *get_diplo_dialog();

  QCompleter *chat_completer;
  QStringList chat_history;
  int history_pos;
  mr_idle mr_idler;
  fc_font fc_fonts;
  info_label *game_info_label;
  end_turn_area *end_turn_rect;
  QWidget *central_wdg;
  mr_menu *menu_bar;
  fc_game_tab_widget *game_tab_widget;
  messagewdg *msgwdg;
  info_tab *infotab;
  pregamevote *pre_vote;
  unit_select *unit_sel;
  xvote *x_vote;
  goto_dialog *gtd;
  QCursor *fc_cursors[CURSOR_LAST][NUM_CURSOR_FRAMES];
  pregame_options *pr_options;
  fc_settings qt_settings;
  void gimme_place(QWidget* widget, QString str);
  int gimme_index_of(QString str);
  void remove_repo_dlg(QString str);
  bool is_repo_dlg_open(QString str);
  void write_settings();
  bool is_closing();

private slots:

  void server_input(int sock);
  void chat();
  void closing();
  void slot_lan_scan();
  void slot_meta_scan();
  void slot_connect();
  void slot_disconnect();
  void slot_pregame_observe();
  void slot_pregame_start();
  void update_network_lists();
  bool slot_close_widget(int index);
  void start_page_menu(QPoint);
  void slot_pick_nation();
  void send_command_to_server(const QString &);
  void start_new_game();
  void start_scenario();
  void start_from_save();
  void browse_saves();
  void browse_scenarios();
  void clear_status_bar();

public slots:
  void switch_page(int i);
  void popup_client_options();
  void update_info_label();
  void quit();

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
  void update_load_page(void);
  void create_cursors(void);
  void update_scenarios_page(void);
  void set_connection_state(enum connection_state state);
  void update_buttons();
  void init();
  void read_settings();

  enum client_pages page;
  QMap<QString, QWidget*> opened_repo_dlgs;
  QStringList status_bar_queue;
  QString current_file;
  bool send_new_aifill_to_server;
  bool quitting;

protected:
  void timerEvent(QTimerEvent *);
  bool eventFilter(QObject *obj, QEvent *event);
  void closeEvent(QCloseEvent *event);

signals:
  void keyCaught(QKeyEvent *e);

};

/***************************************************************************
  Class for showing options in PAGE_START, options like ai_fill, ruleset
  etc.
***************************************************************************/
class pregame_options : public QWidget
{
  Q_OBJECT
  QComboBox *ailevel;
public:
  pregame_options() {};
  void init();
  QComboBox *cruleset;
  QSpinBox *max_players;
private slots:
  void max_players_change(int i);
  void ailevel_change(int i);
  void ruleset_change(int i);
public slots:
  void popup_server_options();
};

// Return fc_client instance. Implementation in gui_main.cpp
class fc_client *gui();

#endif /* FC__FC_CLIENT_H */


