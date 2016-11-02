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
#include <QApplication>
#include <QCompleter>
#include <QMainWindow>
#include <QLineEdit>
#include <QScrollBar>
#include <QStatusBar>
#include <QTabBar>
#include <QTextEdit>

// client
#include "connectdlg_common.h"

// common
#include "game.h"

// gui-qt
#include "fc_client.h"
#include "gui_main.h"
#include "optiondlg.h"
#include "sprite.h"


extern QApplication *qapp;
fc_icons* fc_icons::m_instance = 0;
/****************************************************************************
  Constructor
****************************************************************************/
fc_client::fc_client() : QMainWindow()
{
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
  QTextCodec::setCodecForCStrings(QTextCodec::codecForName("UTF-8"));
#endif
  QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
  /**
   * Somehow freeciv-client-common asks to switch to page when all widgets
   * haven't been created yet by Qt, even constructor finished job,
   * so we null all Qt objects, so while switching we will know if they
   * were created.
   * After adding new QObjects null them here.
   */
  main_wdg = NULL;
  chat_completer = NULL;
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
  msgwdg = NULL;
  infotab = NULL;
  game_info_label = NULL;
  end_turn_rect = NULL;
  central_wdg = NULL;
  game_tab_widget = NULL;
  start_players_tree = NULL;
  unit_sel = NULL;
  info_tile_wdg = NULL;
  opened_dialog = NULL;
  current_unit_id = -1;
  current_unit_target_id = -1;
  current_file = "";
  status_bar_queue.clear();
  quitting = false;
  pre_vote = NULL;
  x_vote = NULL;
  gtd = NULL;
  update_info_timer = nullptr;
  for (int i = 0; i <= PAGE_GGZ; i++) {
    pages_layout[i] = NULL;
    pages[i] = NULL;
  }
  init();
}

/****************************************************************************
  Initializes layouts for all pages
****************************************************************************/
void fc_client::init()
{
  QString path;
  central_wdg = new QWidget;
  central_layout = new QGridLayout;
  chat_completer = new QCompleter;
  central_layout->setContentsMargins(2, 2, 2, 2);

  // General part not related to any single page
  history_pos = -1;
  fc_fonts.init_fonts();
  menu_bar = new mr_menu();
  menu_bar->setup_menus();
  setMenuBar(menu_bar);
  status_bar = statusBar();
  status_bar_label = new QLabel;
  status_bar_label->setAlignment(Qt::AlignCenter);
  status_bar->addWidget(status_bar_label, 1);
  set_status_bar(_("Welcome to Freeciv"));
  create_cursors();
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
  pages[PAGE_NETWORK]->setVisible(false);

  // PAGE_GAME
  path = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
  if (path.isEmpty() == false) {
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, path);
  }
  read_settings();
  pages[PAGE_GAME] = new QWidget(central_wdg);
  init_mapcanvas_and_overview();
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
  setCentralWidget(central_wdg);

  connect(switch_page_mapper, SIGNAL(mapped( int)),
                this, SLOT(switch_page(int)));
  setVisible(true);
}

/****************************************************************************
  Destructor
****************************************************************************/
fc_client::~fc_client()
{
  status_bar_queue.clear();
  fc_fonts.release_fonts();
}

/****************************************************************************
  Main part of gui-qt
****************************************************************************/
void fc_client::main(QApplication *qapp)
{
  qRegisterMetaType<QTextCursor>("QTextCursor");
  qRegisterMetaType<QTextBlock>("QTextBlock");
  fc_allocate_ow_mutex();
  real_output_window_append(_("This is Qt-client for Freeciv."), NULL, -1);
  fc_release_ow_mutex();
  chat_welcome_message(true);

  set_client_state(C_S_DISCONNECTED);

  startTimer(TIMER_INTERVAL);
  connect(qapp, SIGNAL(aboutToQuit()), this, SLOT(closing()));
  qapp->exec();

  free_mapcanvas_and_overview();
  tileset_free_tiles(tileset);
}

/****************************************************************************
  Returns status if fc_client is being closed
****************************************************************************/
bool fc_client::is_closing()
{
  return quitting;
}

/****************************************************************************
  Called when fc_client is going to quit
****************************************************************************/
void fc_client::closing()
{
  quitting = true;
}


/****************************************************************************
  Appends text to chat window
****************************************************************************/
void fc_client::append_output_window(const QString &str)
{
  QTextCursor cursor;

  if (output_window != NULL) {
    output_window->append(str);
    chat_line->setCompleter(chat_completer);
    output_window->verticalScrollBar()->setSliderPosition(
                              output_window->verticalScrollBar()->maximum());
  }
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

  if ((new_page == PAGE_SCENARIO || new_page == PAGE_LOAD)
       && !is_server_running()) {
    current_file = "";
    client_start_server_and_set_page(new_page);
    return;
  }

  if (page == PAGE_NETWORK){
    destroy_server_scans();
  }
  menuBar()->setVisible(false);
  if (status_bar != nullptr) {
    status_bar->setVisible(true);
  }

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
    break;
  case PAGE_START:
    pre_vote->hide();
    voteinfo_gui_update();
    break;
  case PAGE_LOAD:
    update_load_page();
    break;
  case PAGE_GAME:
    gui()->infotab->chtwdg->update_widgets();
    status_bar->setVisible(false);
    gui()->infotab->chtwdg->update_font();
    showMaximized();
      /* For MS Windows, it might ingore first */
    showMaximized();
    if (fullscreen_mode){
      gui()->showFullScreen();
      gui()->mapview_wdg->showFullScreen();
    }
    menuBar()->setVisible(true);
    mapview_wdg->setFocus();
    center_on_something();
    voteinfo_gui_update();
    update_info_label();
    minimapview_wdg->reset();
    overview_size_changed();
    break;
  case PAGE_SCENARIO:
    update_scenarios_page();
    break;
  case PAGE_NETWORK:
    update_network_lists();
    set_connection_state(LOGIN_TYPE);
    connect_host_edit->setText(server_host);
    fc_snprintf(buf, sizeof(buf), "%d", server_port);
    connect_port_edit->setText(buf);
    connect_login_edit->setText(user_name);
    connect_password_edit->setDisabled(true);
    connect_confirm_password_edit->setDisabled(true);
    break;
  case PAGE_GGZ:
  default:
    if (client.conn.used) {
      disconnect_from_server();
    }
    set_client_page(PAGE_MAIN);
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

  connect(server_notifier, SIGNAL(activated(int)), this, 
          SLOT(server_input(int)));
}

/****************************************************************************
  Event handler
****************************************************************************/
bool fc_client::event(QEvent *event)
{
  if (event->type() == QEvent::User) {
    version_message_event vmevt =
        dynamic_cast<version_message_event&>(*event);
    set_status_bar(vmevt.get_message());
    return true;
  } else {
    return QMainWindow::event(event);
  }
}


/****************************************************************************
  Event filter
****************************************************************************/
bool fc_client::eventFilter(QObject *obj, QEvent *event)
{
  if (obj == chat_line) {
    if (event->type() == QEvent::KeyPress) {
      QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
      if (keyEvent->key() == Qt::Key_Up) {
        history_pos++;
        history_pos = qMin(chat_history.count(), history_pos);
        if (history_pos < chat_history.count()) {
          chat_line->setText(chat_history.at(history_pos));
        }
        if (history_pos == chat_history.count()) {
          chat_line->setText("");
        }
        return true;
      } else if (keyEvent->key() == Qt::Key_Down) {
        history_pos--;
        history_pos = qMax(-1, history_pos);
        if (history_pos < chat_history.count() && history_pos != -1) {
          chat_line->setText(chat_history.at(history_pos));
        }
        if (history_pos == -1) {
          chat_line->setText("");
        }
        return true;
      }
    }
  }
  return QObject::eventFilter(obj, event);
}

/****************************************************************************
  Closes main window
****************************************************************************/
void fc_client::closeEvent(QCloseEvent *event) {
  popup_quit_dialog();
  event->ignore();
}

/****************************************************************************
  Removes notifier
****************************************************************************/
void fc_client::remove_server_source()
{
  delete server_notifier;
}

/****************************************************************************
  There is input from server
****************************************************************************/
void fc_client::server_input(int sock)
{
#if defined(Q_WS_WIN)
  server_notifier->setEnabled(false);
#endif
  input_from_server(sock);
#if defined(Q_WS_WIN)
  server_notifier->setEnabled(true);
#endif
}

/****************************************************************************
  Enter pressed on chatline
****************************************************************************/
void fc_client::chat()
{
  send_chat(chat_line->text().toUtf8().data());
  chat_history.prepend(chat_line->text());
  chat_line->clear();
  history_pos = -1;
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
    if (game.info.is_new_game) {
      send_chat("/take -");
    } else {
      send_chat("/detach");
    }
    obs_button->setText(_("Don't Observe"));
  } else {
    send_chat("/observe");
    obs_button->setText(_("Observe"));
  }
}

/****************************************************************************
  User clicked Start in START_PAGE
****************************************************************************/
void fc_client::slot_pregame_start()
{
  if (can_client_control()) {
    dsend_packet_player_ready(&client.conn,
                              player_number(client_player()),
                              !client_player()->is_ready);
  } else {
    dsend_packet_player_ready(&client.conn, 0, TRUE);
  }
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
void fc_client::gimme_place(QWidget *widget, QString str)
{
  QString x;

  x = opened_repo_dlgs.key(widget);

  if (x.isEmpty()) {
    opened_repo_dlgs.insert(str, widget);
    return;
  }
  log_error("Failed to find place for new tab widget");
  return;
}

/****************************************************************************
  Checks if given report is opened, if you create new report as tab on game
  page, figure out some original string and put in in repodlg.h as comment to
  that QWidget class.
****************************************************************************/
bool fc_client::is_repo_dlg_open(QString str)
{
  QWidget *w;

  w = opened_repo_dlgs.value(str);

  if (w == NULL) {
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
  QWidget *w;

  w = opened_repo_dlgs.value(str);
  i = game_tab_widget->indexOf(w);
  return i;
}

/****************************************************************************
  Loads qt-specific options
****************************************************************************/
void fc_client::read_settings()
{
  QSettings s(QSettings::IniFormat, QSettings::UserScope, 
              "freeciv-qt-client");
  if (s.contains("InfoTab-xsize")) {
    qt_settings.infotab_width = s.value("InfoTab-xsize").toInt();
  } else {
    qt_settings.infotab_width = 95;
  }
  if (s.contains("InfoTab-ysize")) {
    qt_settings.infotab_height = s.value("InfoTab-ysize").toInt();
  } else {
    qt_settings.infotab_height = 29;
  }
  qt_settings.player_repo_sort_col = -1;
  qt_settings.city_repo_sort_col = -1;

}

/****************************************************************************
  Save qt-specific options
****************************************************************************/
void fc_client::write_settings()
{
  QSettings s(QSettings::IniFormat, QSettings::UserScope,
              "freeciv-qt-client");
  s.setValue("InfoTab-xsize", qt_settings.infotab_width);
  s.setValue("InfoTab-ysize", qt_settings.infotab_height);
}

/****************************************************************************
  Updates autocompleter for chat widgets
****************************************************************************/
void fc_client::update_completer()
{
  QStringList wordlist;
  QString str;

  conn_list_iterate(game.est_connections, pconn) {
    if (pconn->playing) {
      wordlist << pconn->playing->name;
      wordlist << pconn->playing->username;
    } else {
      wordlist << pconn->username;
    }
  } conn_list_iterate_end;
  players_iterate (pplayer){
    str = pplayer->name;
    if (!wordlist.contains(str)){
      wordlist << str;
    }
  } players_iterate_end
  if (chat_completer != NULL) {
    delete chat_completer;
  }
  chat_completer = new QCompleter(wordlist);
  chat_completer->setCaseSensitivity(Qt::CaseInsensitive);
  chat_completer->setCompletionMode(QCompleter::InlineCompletion);
}

/****************************************************************************
  Shows/closes unit selection widget
****************************************************************************/
void fc_client::toggle_unit_sel_widget(struct tile *ptile)
{
  if (unit_sel != NULL) {
    unit_sel->close();
    delete unit_sel;
    unit_sel = new unit_select(ptile, gui()->mapview_wdg);
    unit_sel->show();
  } else {
    unit_sel = new unit_select(ptile, gui()->mapview_wdg);
    unit_sel->show();
  }
}

/****************************************************************************
  Update unit selection widget if open
****************************************************************************/
void fc_client::update_unit_sel()
{
  if (unit_sel != NULL){
    unit_sel->update_units();
    unit_sel->create_pixmap();
    unit_sel->update();
  }
}

/****************************************************************************
  Closes unit selection widget.
****************************************************************************/
void fc_client::popdown_unit_sel()
{
  if (unit_sel != nullptr){
    unit_sel->close();
    delete unit_sel;
    unit_sel = nullptr;
  }
}

/****************************************************************************
  Removes report dialog string from the list marking it as closed
****************************************************************************/
void fc_client::remove_repo_dlg(QString str)
{
  /* if app is closing opened_repo_dlg is already deleted */
  if (is_closing() == false) {
    opened_repo_dlgs.remove(str);
  }
}

/****************************************************************************
  Popups client options
****************************************************************************/
void fc_client::popup_client_options()
{
  option_dialog_popup(_("Set local options"), client_optset);
}


void fc_client::create_cursors(void)
{
  enum cursor_type curs;
  int cursor;
  QPixmap *pix;
  int hot_x, hot_y;
  struct sprite *sprite;
  int frame;
  QCursor *c;
  for (cursor = 0; cursor < CURSOR_LAST; cursor++) {
    for (frame = 0; frame < NUM_CURSOR_FRAMES; frame++) {
      curs = static_cast<cursor_type>(cursor);
      sprite = get_cursor_sprite(tileset, curs, &hot_x, &hot_y, frame);
      pix = sprite->pm;
      c = new QCursor(*pix, hot_x, hot_y);
      fc_cursors[cursor][frame] = c;
    }
  }
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
    return font_map.value(name);
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
  Deletes all fonts
****************************************************************************/
void fc_font::release_fonts()
{
  foreach (QFont *f, font_map) {
    delete f;
  }
}


/****************************************************************************
  Adds new font or overwrite old one
****************************************************************************/
void fc_font::set_font(QString name, QFont * qf)
{
  font_map.insert(name,qf);
}

/****************************************************************************
  Game tab widget constructor
****************************************************************************/
fc_game_tab_widget::fc_game_tab_widget(): QTabWidget()
{
  connect(this, SIGNAL(currentChanged(int)), SLOT(restore_label_color(int)));
}

/****************************************************************************
  Icon provider constructor
****************************************************************************/
fc_icons::fc_icons()
{
}

/****************************************************************************
  Returns instance of fc_icons
****************************************************************************/
fc_icons *fc_icons::instance()
{
  if (!m_instance)
    m_instance = new fc_icons;
  return m_instance;
}

/****************************************************************************
  Deletes fc_icons instance
****************************************************************************/
void fc_icons::drop()
{
  if (m_instance) {
    delete m_instance;
    m_instance = 0;
  }
}

/****************************************************************************
  Returns icon by given name
****************************************************************************/
QIcon fc_icons::get_icon(const QString &id)
{
  QIcon icon = QIcon(fileinfoname(get_data_dirs(),
                            QString("themes/gui-qt/icons/"
                                    + id + ".png").toLocal8Bit().data()));
  return QIcon(icon);
}

/****************************************************************************
  Returns path for icon
****************************************************************************/
QString fc_icons::get_path(const QString &id)
{
  return fileinfoname(get_data_dirs(),
                      QString("themes/gui-qt/icons/"
                              + id + ".png").toLocal8Bit().data());
}


/****************************************************************************
  Restores black color of label
****************************************************************************/
void fc_game_tab_widget::restore_label_color(int index)
{
  change_color(index, QColor(Qt::black));
}

/****************************************************************************
  Changes color of tab widget's label
****************************************************************************/
void fc_game_tab_widget::change_color(int index, QColor col)
{
  tabBar()->setTabTextColor(index, col);
}

/****************************************************************************
  Init's layout and default values for options in START_PAGE
****************************************************************************/
void pregame_options::init()
{
  QGridLayout *layout;
  QLabel *l1, *l2, *l3;
  QPushButton *but;
  int level;

  l1 = new QLabel(_("Number of Players\n(including AI):"));
  l2 = new QLabel(_("AI Skill Level:"));
  l3 = new QLabel(_("Ruleset:"));
  layout = new QGridLayout(this);
  max_players = new QSpinBox(this);
  ailevel = new QComboBox(this);
  cruleset = new QComboBox(this);
  max_players->setRange(1, MAX_NUM_PLAYERS);

  for (level = AI_LEVEL_AWAY; level < AI_LEVEL_LAST; level++) {
    if (is_settable_ai_level(static_cast<ai_level>(level))) {
      const char *level_name = ai_level_name(static_cast<ai_level>(level));
      ailevel->addItem(level_name, level);
    }
  }
  ailevel->setCurrentIndex(-1);

  connect(max_players, SIGNAL(valueChanged(int)),
          SLOT(max_players_change(int)));
  connect(ailevel, SIGNAL(currentIndexChanged(int)),
          SLOT(ailevel_change(int)));
  connect(cruleset, SIGNAL(currentIndexChanged(int)),
          SLOT(ruleset_change(int)));

  but = new QPushButton;
  but->setText(_("More Game Options"));
  but->setIcon(fc_icons::instance()->get_icon("preferences-other"));
  QObject::connect(but, SIGNAL(clicked()), this,
                   SLOT(popup_server_options()));
  layout->addWidget(l1, 0, 1);
  layout->addWidget(l2, 1, 1);
  layout->addWidget(l3, 2, 1);
  layout->addWidget(max_players, 0, 2);
  layout->addWidget(ailevel, 1, 2);
  layout->addWidget(cruleset, 2, 2);
  layout->addWidget(but, 3, 1);
  setLayout(layout);
}

/****************************************************************************
  Slot for changing aifill value
****************************************************************************/
void pregame_options::max_players_change(int i)
{
  option_int_set(optset_option_by_name(server_optset, "aifill"), i);
}

/****************************************************************************
  Slot for changing level of AI
****************************************************************************/
void pregame_options::ailevel_change(int i)
{
  int k;
  const char *name;

  k = ailevel->currentData().toInt();
  name = ai_level_cmd(static_cast<ai_level>(k));
  send_chat_printf("/%s", name);

}

/****************************************************************************
  Slot for changing ruleset
****************************************************************************/
void pregame_options::ruleset_change(int i)
{
  if (!cruleset->currentText().isEmpty()) {
    set_ruleset(cruleset->currentText().toLocal8Bit().data());
  }
}

/****************************************************************************
  Popups client options
****************************************************************************/
void pregame_options::popup_server_options()
{
  option_dialog_popup(_("Set server options"), server_optset);
}

