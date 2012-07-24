/**********************************************************************
 Freeciv - Copyright (C) 1996-2004 - The Freeciv Team
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

#include "pages.h"

Q_DECLARE_METATYPE (QDockWidget::DockWidgetFeatures)
static struct server_scan *meta_scan, *lan_scan;
static bool holding_srv_list_mutex = false;
static enum connection_state connection_status;

/**************************************************************************
  Sets the "page" that the client should show.  See also pages_g.h.
**************************************************************************/
void qtg_real_set_client_page(enum client_pages page)
{
  gui()->switch_page(page);
}

/****************************************************************************
  Set the list of available rulesets.  The default ruleset should be
  "default", and if the user changes this then set_ruleset() should be
  called.
****************************************************************************/
void qtg_gui_set_rulesets(int num_rulesets, char **rulesets)
{
  /* PORTME */
}

/**************************************************************************
  Returns current client page
**************************************************************************/
enum client_pages qtg_get_current_client_page()
{
  return gui()->current_page();
}

/**************************************************************************
  update the start page.
**************************************************************************/
void update_start_page(void)
{
  /* PORTME */
}

/**************************************************************************
  Creates buttons and layouts for start page.
**************************************************************************/
void fc_client::create_main_page(void)
{

  QPixmap main_graphics(tileset_main_intro_filename(tileset));
  QLabel* free_main_pic = new QLabel;
  pages_layout[PAGE_MAIN] = new QGridLayout;
  QStringList buttons_names;
  int buttons_nr;

  free_main_pic->setPixmap(main_graphics);
  pages_layout[PAGE_MAIN]->addWidget(free_main_pic,
                                     0, 0, 1, 2, Qt::AlignCenter);
  buttons_names << _("Start new game") << _("Start scenario game")
                << _("Load saved game") << _("Connect to network game")
                << _("Options") << _("Quit");

  buttons_nr = buttons_names.count();

  for (int iter = 0; iter < buttons_nr; iter++) {
    button = new QPushButton(buttons_names[iter]);

    switch (iter) {
    case 0:
      pages_layout[PAGE_MAIN]->addWidget(button, 1, 0);
      break;
    case 1:
      pages_layout[PAGE_MAIN]->addWidget(button, 2, 0);
      QObject::connect(button, SIGNAL(clicked()), this,
                       SLOT(slot_switch_to_scenario_page()));
      break;
    case 2:
      pages_layout[PAGE_MAIN]->addWidget(button, 3, 0);
      QObject::connect(button, SIGNAL(clicked()), this,
                       SLOT(slot_switch_to_load_page()));
      break;
    case 3:
      pages_layout[PAGE_MAIN]->addWidget(button, 1, 1);
      QObject::connect(button, SIGNAL(clicked()), this,
                       SLOT(slot_switch_to_network_page()));
      break;
    case 4:
      pages_layout[PAGE_MAIN]->addWidget(button, 2, 1);
      break;
    case 5:
      pages_layout[PAGE_MAIN]->addWidget(button, 3, 1);
      QObject::connect(button, SIGNAL(clicked()), this, SLOT(quit()));
      break;
    default:
      break;
    }
  }
}

/**************************************************************************
  Update network page connection state.
**************************************************************************/
void fc_client::set_connection_state(enum connection_state state)
{
  switch (state) {
  case LOGIN_TYPE:
    set_status_bar("");
    connect_password_edit->setText("");
    connect_confirm_password_edit->setText("");
    connect_confirm_password_edit->setReadOnly(true);
    break;
  case NEW_PASSWORD_TYPE:
    connect_password_edit->setText("");
    connect_confirm_password_edit->setText("");
    connect_confirm_password_edit->setReadOnly(false);
    connect_password_edit->setFocus(Qt::OtherFocusReason);
    break;
  case ENTER_PASSWORD_TYPE:
    connect_password_edit->setText("");
    connect_confirm_password_edit->setText("");
    connect_confirm_password_edit->setReadOnly(true);
    connect_password_edit->setFocus(Qt::OtherFocusReason);


    break;
  case WAITING_TYPE:
    set_status_bar("");
    connect_confirm_password_edit->setReadOnly(true);

    break;
  }

  connection_status = state;
}

/***************************************************************************
  Creates buttons and layouts for network page.
***************************************************************************/
void fc_client::create_network_page(void)
{
  pages_layout[PAGE_NETWORK] = new QGridLayout;
  QVBoxLayout *page_network_layout = new QVBoxLayout;
  QGridLayout *page_network_grid_layout = new QGridLayout;
  QHBoxLayout *page_network_lan_layout = new QHBoxLayout;
  QHBoxLayout *page_network_wan_layout = new QHBoxLayout;


  connect_host_edit = new QLineEdit;
  connect_port_edit = new QLineEdit;
  connect_login_edit = new QLineEdit;
  connect_password_edit = new QLineEdit;
  connect_confirm_password_edit = new QLineEdit;

  connect_password_edit->setEchoMode(QLineEdit::Password);
  connect_confirm_password_edit->setEchoMode(QLineEdit::Password);

  connect_password_edit->setDisabled(true);
  connect_confirm_password_edit->setDisabled(true);
  connect_confirm_password_edit->setReadOnly (true);
  connect_tab_widget = new QTabWidget;
  connect_lan = new QWidget;
  connect_metaserver = new QWidget;
  lan_widget = new QTableWidget;
  wan_widget = new QTableWidget;
  info_widget = new QTableWidget;

  QStringList servers_list;
  servers_list << _("Server Name") << _("Port") << _("Version")
               << _("Status") << _("Players") << _("Comment");
  QStringList server_info;
  server_info << _("Name") << _("Type") << _("Host") << _("Nation");

  lan_widget->setRowCount(0);
  lan_widget->setColumnCount(servers_list.count());
  lan_widget->verticalHeader()->setVisible(false);

  wan_widget->setRowCount(0);
  wan_widget->setColumnCount(servers_list.count());
  wan_widget->verticalHeader()->setVisible(false);

  info_widget->setRowCount(0);
  info_widget->setColumnCount(server_info.count());
  info_widget->verticalHeader()->setVisible(false);

  lan_widget->setHorizontalHeaderLabels(servers_list);
  lan_widget->setProperty("showGrid", "false");
  lan_widget->setProperty("selectionBehavior", "SelectRows");
  lan_widget->setEditTriggers(QAbstractItemView::NoEditTriggers);

  connect(lan_widget->selectionModel(),
          SIGNAL(selectionChanged(const QItemSelection &, const QItemSelection &)), this,
          SLOT(slot_selection_changed(const QItemSelection &, const QItemSelection &)));

  wan_widget->setHorizontalHeaderLabels(servers_list);
  wan_widget->setProperty("showGrid", "false");
  wan_widget->setProperty("selectionBehavior", "SelectRows");
  wan_widget->setEditTriggers(QAbstractItemView::NoEditTriggers);

  connect(wan_widget->selectionModel(),
          SIGNAL(selectionChanged(const QItemSelection &, const QItemSelection &)), this,
          SLOT(slot_selection_changed(const QItemSelection &, const QItemSelection &)));

  info_widget->setHorizontalHeaderLabels(server_info);
  info_widget->setProperty("selectionBehavior", "SelectRows");
  info_widget->setEditTriggers(QAbstractItemView::NoEditTriggers);

  QHeaderView *header;
  header = lan_widget->horizontalHeader();
  header->resizeSections(QHeaderView::Stretch);
  header = wan_widget->horizontalHeader();
  header->resizeSections(QHeaderView::Stretch);

  QLabel *connect_msg;
  QStringList label_names;
  label_names << _("Connect") << _("Port") << _("Login")
              << _("Password") << _("Confirm Password");
  connect_msg = new QLabel;

  for (int i = 0; i < label_names.count(); i++) {
    connect_msg = new QLabel;
    connect_msg->setText(label_names[i]);
    page_network_grid_layout->addWidget(connect_msg, i, 0, Qt::AlignHCenter);
  }

  page_network_grid_layout->addWidget(connect_host_edit, 0, 1);
  page_network_grid_layout->addWidget(connect_port_edit, 1, 1);
  page_network_grid_layout->addWidget(connect_login_edit, 2, 1);
  page_network_grid_layout->addWidget(connect_password_edit, 3, 1);
  page_network_grid_layout->addWidget(connect_confirm_password_edit, 4, 1);

  page_network_grid_layout->addWidget(info_widget, 0, 2, 5, 4);

  QPushButton *network_button = new QPushButton;
  network_button->setText(tr(_("&Refresh")));
  QObject::connect(network_button, SIGNAL(clicked()), this,
                   SLOT(update_network_lists()));
  page_network_grid_layout->addWidget(network_button, 5, 0);

  network_button = new QPushButton;
  network_button->setText(tr(_("&Cancel")));
  QObject::connect(network_button, SIGNAL(clicked()), this,
                   SLOT(slot_switch_to_main_page()));
  page_network_grid_layout->addWidget(network_button, 5, 2, 1, 1);

  network_button = new QPushButton;
  network_button->setText(tr(_("&Connect")));
  page_network_grid_layout->addWidget(network_button, 5, 5, 1, 1);
  QObject::connect(network_button, SIGNAL(clicked()), this,
                   SLOT(slot_connect()));

  connect_lan->setLayout(page_network_lan_layout);
  connect_metaserver->setLayout(page_network_wan_layout);
  page_network_lan_layout->addWidget(lan_widget, 1);
  page_network_wan_layout->addWidget(wan_widget, 1);
  page_network_layout->addWidget(connect_tab_widget, 1);
  connect_tab_widget->addTab(connect_lan, _("LAN"));
  connect_tab_widget->addTab(connect_metaserver, _("NETWORK"));
  page_network_grid_layout->setColumnStretch(3, 4);
  pages_layout[PAGE_NETWORK]->addLayout(page_network_layout, 1, 1);
  pages_layout[PAGE_NETWORK]->addLayout(page_network_grid_layout, 2, 1);

}

/***************************************************************************
  Sets application status bar
***************************************************************************/
void fc_client::set_status_bar(QString message)
{
  status_bar_label->setText (message);
}

/***************************************************************************
  Creates context menu in TableView when clicked on some player
***************************************************************************/
void create_conn_menu(struct player *pplayer, struct connection *pconn)
{
  /* FIXME */
  char buf[128];
  fc_snprintf(buf, sizeof(buf), _("%s info"),
              pconn ? pconn->username : player_name(pplayer));

  if (NULL != pplayer) {

  }
}

/***************************************************************************
  Creates buttons and layouts for load page.
***************************************************************************/
void fc_client::create_load_page()
{
  pages_layout[PAGE_LOAD] = new QGridLayout;
  QPushButton *but;

  saves_load = new QTableWidget;

  QStringList sav;
  sav << _("Choose Saved Game to Load") << _("Time");

  saves_load->setRowCount (0);
  saves_load->setColumnCount (sav.count());
  saves_load->setHorizontalHeaderLabels (sav);

  saves_load->setProperty("showGrid", "false");
  saves_load->setProperty("selectionBehavior", "SelectRows");
  saves_load->setEditTriggers(QAbstractItemView::NoEditTriggers);

  connect(saves_load->selectionModel(),
          SIGNAL(selectionChanged(const QItemSelection &, const QItemSelection &)), this,
          SLOT(slot_selection_changed(const QItemSelection &, const QItemSelection &)));

  QHeaderView *header;
  header = saves_load->horizontalHeader();
  header->resizeSections(QHeaderView::Stretch);

  pages_layout[PAGE_LOAD]->addWidget(saves_load, 0, 0, 1, 4);

  but = new QPushButton;
  but->setText(tr(_("Browse")));
  pages_layout[PAGE_LOAD]->addWidget (but, 1, 0);

  but = new QPushButton;
  but->setText(tr(_("Cancel")));
  QObject::connect(but, SIGNAL(clicked()), this,
                   SLOT(slot_switch_to_main_page()));
  pages_layout[PAGE_LOAD]->addWidget(but, 1, 2);

  but = new QPushButton;
  but->setText(tr(_("Load")));
  pages_layout[PAGE_LOAD]->addWidget(but, 1, 3);

}

/***************************************************************************
 * Creates buttons and layouts for scenario page.
 **************************************************************************/
void fc_client::create_scenario_page()
{
  pages_layout[PAGE_SCENARIO] = new QGridLayout;
  QPushButton *but;

  scenarios_load = new QTableWidget;
  scenarios_view = new QTextEdit;
  QStringList sav;
  QSpacerItem *free_space = new QSpacerItem(50, 100);
  sav << tr(_("Choose a Scenario"));
  scenarios_load->setRowCount(0);
  scenarios_load->setColumnCount(sav.count());
  scenarios_load->setHorizontalHeaderLabels(sav);
  scenarios_load->setProperty("showGrid", "false");
  scenarios_load->setProperty("selectionBehavior", "SelectRows");
  scenarios_load->setEditTriggers(QAbstractItemView::NoEditTriggers);
  pages_layout[PAGE_SCENARIO]->addWidget(scenarios_load, 0, 0, 1, 2);
  pages_layout[PAGE_SCENARIO]->addWidget(scenarios_view, 0, 3, 1, 2);
  pages_layout[PAGE_SCENARIO]->addItem(free_space, 1, 0);
  scenarios_view->setReadOnly(true);

  QHeaderView *header;
  header = scenarios_load->horizontalHeader();
  header->resizeSections(QHeaderView::ResizeToContents);

  but = new QPushButton;
  but->setText(tr(_("Browse")));
  pages_layout[PAGE_SCENARIO]->addWidget (but, 2, 0);

  but = new QPushButton;
  but->setText(tr(_("Cancel")));
  QObject::connect(but, SIGNAL(clicked()), this,
                   SLOT(slot_switch_to_main_page()));
  pages_layout[PAGE_SCENARIO]->addWidget(but, 2, 3);

  but = new QPushButton;
  but->setText(tr(_("Load")));
  pages_layout[PAGE_SCENARIO]->addWidget(but, 2, 4);
}

/***************************************************************************
  Creates buttons and layouts for start page.
***************************************************************************/
void fc_client::create_start_page()
{
  pages_layout[PAGE_START] = new QGridLayout;
  start_players = new QTableWidget;
  QStringList player_widget_list;

  player_widget_list << _("Name") << _("Ready") << _("Leader")
                     << _("Flag") << _("Nation");
  start_players->setRowCount(0);
  start_players->setColumnCount(player_widget_list.count());
  start_players->setHorizontalHeaderLabels(player_widget_list);
  QHeaderView *header;
  header = start_players->horizontalHeader();
  header->resizeSections(QHeaderView::ResizeToContents);
  QPushButton *but;
  but = new QPushButton;
  but->setText(_("More Game Options"));
  pages_layout[PAGE_START]->addWidget(but, 6, 0);
  pages_layout[PAGE_START]->addWidget(start_players, 0, 0, 5, 4);
  but = new QPushButton;
  but->setText(_("Disconnect"));
  QObject::connect(but, SIGNAL(clicked()), this, SLOT(slot_disconnect()));
  pages_layout[PAGE_START]->addWidget(but, 7, 0);
  but = new QPushButton;
  but->setText(_("Pick Nation"));
  pages_layout[PAGE_START]->addWidget(but, 7, 1);
  but = new QPushButton;
  but->setText(_("Observe"));
  pages_layout[PAGE_START]->addWidget(but, 7, 2);
  QObject::connect(but, SIGNAL(clicked()), this,
                   SLOT(slot_pregame_observe()));
  but = new QPushButton;
  but->setText(_("Start"));
  pages_layout[PAGE_START]->addWidget(but, 7, 3);
  QObject::connect(but, SIGNAL(clicked()), this,
                   SLOT(slot_pregame_start()));
}

/***************************************************************************
  Creates buttons and layouts for game page.
***************************************************************************/
void fc_client::create_game_page()
{
  QGridLayout *game_layout;
  QSizePolicy game_info_label_policy;

  pages_layout[PAGE_GAME] = new QGridLayout;
  game_main_widget = new QWidget;
  game_layout = new QGridLayout;
  game_tab_widget = new QTabWidget;
  game_tab_widget->setTabPosition(QTabWidget::South);
  game_tab_widget->setDocumentMode(false);
  mapview_wdg = new map_view();
  mapview_wdg->setFocusPolicy(Qt::WheelFocus);

  game_info_label = new info_label;
  game_info_label_policy.setHorizontalPolicy(QSizePolicy::Expanding);
  game_info_label_policy.setVerticalPolicy(QSizePolicy::Fixed);
  game_info_label->setSizePolicy(game_info_label_policy);

  game_layout->addWidget(mapview_wdg, 1, 0);
  game_main_widget->setLayout(game_layout);

  game_tab_widget->setMinimumSize(600,400);
  game_tab_widget->setTabsClosable(true);
  add_game_tab(game_main_widget, "View", 0);

  QObject::connect(game_tab_widget, SIGNAL(tabCloseRequested(int)),
                   SLOT(slot_close_widget(int)));

  pages_layout[PAGE_GAME]->addWidget(game_tab_widget, 1, 0);
  pages_layout[PAGE_GAME]->addWidget(game_info_label, 2, 0);
}

/***************************************************************************
  Closes given widget on game tab view, returns if succeeded
***************************************************************************/
bool fc_client::slot_close_widget(int index)
{
  bool ret = false;

  QWidget *w = game_tab_widget->widget(index);

  /** we wont close map view
   * even requested, Qt cannot set one of tab widgets
   * without close button, only all or none */
  if (w != game_main_widget) {
    game_tab_widget->removeTab(index);
    ret = w->close();
    delete w;
  } else {
    ret = false;
  }

  return ret;
}

/***************************************************************************
  Inserts tab widget to game view page
***************************************************************************/
int fc_client::add_game_tab(QWidget *widget, QString title, int index)
{
  return game_tab_widget->insertTab(index,widget, title);
}

/***************************************************************************
  Removes given tab widget from game page
***************************************************************************/
void fc_client::rm_game_tab(int index)
{
  game_tab_widget->removeTab(index);
}

/***************************************************************************
  Updates list of servers in network page in proper QTableViews
***************************************************************************/
void fc_client::update_server_list(enum server_scan_type sstype,
                                   const struct server_list *list)
{
  QTableWidget* sel = NULL;
  QString host, portstr;
  int port;

  switch (sstype) {
  case SERVER_SCAN_LOCAL:
    sel = lan_widget;
    break;
  case SERVER_SCAN_GLOBAL:
    sel = wan_widget;
    break;
  default:
    break;
  }

  if (!sel) {
    return;
  }

  if (!list) {
    return;
  }

  host = connect_host_edit->text();
  portstr = connect_port_edit->text();
  port = portstr.toInt();
  sel->clearContents();
  sel->setRowCount(0);
  int row = 0;
  server_list_iterate(list, pserver) {
    char buf[20];
    sel->insertRow(row);

    if (pserver->humans >= 0) {
      fc_snprintf(buf, sizeof(buf), "%d", pserver->humans);
    } else {
      strncpy(buf, _("Unknown"), sizeof(buf));
    }

    int tmp = pserver->port;
    QString tstring = QString::number(tmp);

    for (int col = 0; col < 6; col++) {
      QTableWidgetItem *item;
      item = new QTableWidgetItem();

      switch (col) {
      case 0:
        item->setText(pserver->host);
        break;
      case 1:
        item->setText(tstring);
        break;
      case 2:
        item->setText(pserver->version);
        break;
      case 3:
        item->setText(_(pserver->state));
        break;
      case 4:
        item->setText(buf);
        break;
      case 5:
        item->setText(pserver->message);
        break;
      default:
        break;
      }

      sel->setItem(row, col, item);
    }

    if (host == pserver->host && port == pserver->port) {
      sel->selectRow(row);
    }

    row++;
  }
  server_list_iterate_end;
}


/**************************************************************************
  Callback function for when there's an error in the server scan.
**************************************************************************/
void server_scan_error(struct server_scan *scan, const char *message)
{
  output_window_append(ftc_client, message);
  log_error("%s", message);

  switch (server_scan_get_type(scan)) {
  case SERVER_SCAN_LOCAL:
    server_scan_finish(lan_scan);
    lan_scan = NULL;
    break;
  case SERVER_SCAN_GLOBAL:
    server_scan_finish(meta_scan);
    meta_scan = NULL;
    break;
  case SERVER_SCAN_LAST:
    break;
  }
}

/**************************************************************************
  Free the server scans.
**************************************************************************/
void fc_client::destroy_server_scans(void)
{
  if (meta_scan) {
    server_scan_finish (meta_scan);
    meta_scan = NULL;
  }

  if (meta_scan_timer != 0) {
    meta_scan_timer->stop();
    meta_scan_timer = NULL;
  }

  if (lan_scan) {
    server_scan_finish(lan_scan);
    lan_scan = NULL;
  }

  if (lan_scan_timer != 0) {
    lan_scan_timer->stop();
    lan_scan_timer = NULL;
  }
}

/**************************************************************************
  Stop and restart the metaserver and lan server scans.
**************************************************************************/
void fc_client::update_network_lists(void)
{
  destroy_server_scans();

  lan_scan_timer = new QTimer(this);
  lan_scan = server_scan_begin(SERVER_SCAN_LOCAL, server_scan_error);
  connect(lan_scan_timer, SIGNAL(timeout()), this, SLOT(slot_lan_scan()));
  lan_scan_timer->start(500);

  meta_scan = server_scan_begin(SERVER_SCAN_GLOBAL, server_scan_error);
  meta_scan_timer = new QTimer(this);

}

/**************************************************************************
  This function updates the list of servers every so often.
**************************************************************************/
bool fc_client::check_server_scan(server_scan* data)
{
  struct server_scan *scan = data;
  enum server_scan_status stat;

  if (!scan) {
    return false;
  }

  stat = server_scan_poll(scan);

  if (stat >= SCAN_STATUS_PARTIAL) {
    enum server_scan_type type;
    struct srv_list *srvrs;

    type = server_scan_get_type(scan);
    srvrs = server_scan_get_list(scan);
    fc_allocate_mutex(&srvrs->mutex);
    holding_srv_list_mutex = true;
    update_server_list(type, srvrs->servers);
    holding_srv_list_mutex = false;
    fc_release_mutex(&srvrs->mutex);
  }

  if (stat == SCAN_STATUS_ERROR || stat == SCAN_STATUS_DONE) {
    return false;
  }

  return true;
}

/***************************************************************************
  Executes lan scan network
***************************************************************************/
void fc_client::slot_lan_scan()
{
  check_server_scan(lan_scan);
}

/***************************************************************************
  Executes metaserver scan network
***************************************************************************/
void fc_client::slot_meta_scan()
{
  check_server_scan (meta_scan);
}

/***************************************************************************
  Selection chnaged in some tableview on some page
***************************************************************************/
void fc_client::slot_selection_changed(const QItemSelection &selected,
                                       const QItemSelection &deselected)
{

  QModelIndexList indexes = selected.indexes();
  client_pages i = current_page();

  if (indexes.isEmpty()) {
    return;
  }

  QModelIndex index ;

  switch (i) {
  case PAGE_NETWORK:
    index = indexes.at(0);
    connect_host_edit->setText(index.data().toString());
    index = indexes.at(1);
    connect_port_edit->setText(index.data().toString());
    break;
  case PAGE_SCENARIO:
    index = indexes.at(0);
    scenarios_view->setText(index.data().toString());
    break;
  default:
    break;
  }

}
/***************************************************************************
  Create dock_widgets
***************************************************************************/
void fc_client::create_dock_widgets()
{
  int i;
  QVBoxLayout *h_box = new QVBoxLayout;
  QWidget *wi;

  output_window = new QTextEdit(central_wdg);
  chat_line = new QLineEdit(central_wdg);
  messages_window = new QTableWidget(central_wdg);
  messages_window->setRowCount(0);
  messages_window->setColumnCount(1);
  messages_window->setProperty("showGrid", "false");
  messages_window->setProperty("selectionBehavior", "SelectRows");
  messages_window->setEditTriggers(QAbstractItemView::NoEditTriggers);
  messages_window->verticalHeader()->setVisible(false);
  messages_window->horizontalHeader()->setVisible(false);
  messages_window->setFocusPolicy(Qt::NoFocus);
  QHeaderView *header;
  header = messages_window->horizontalHeader();
  header->resizeSections(QHeaderView::Stretch);
  output_window->setReadOnly(true);
  output_window->setVisible(true);
  chat_line->setVisible(true);
  connect(chat_line, SIGNAL(returnPressed()), this, SLOT(chat()));
  chat_line->setReadOnly(false);

  for (i = OUTPUT_DOCK_WIDGET ; i <  LAST_WIDGET; i++) {
    wi = new QWidget;
    dock_widget[i] = new QDockWidget;

    switch (i) {
    case 0: //CHAT WIDGET
      h_box->addWidget(output_window);
      h_box->addWidget(chat_line);
      wi->setLayout(h_box);
      dock_widget[i]->setWidget(wi);
      dock_widget[i]->setFeatures(QDockWidget::DockWidgetMovable
                                  | QDockWidget::DockWidgetFloatable);
      dock_widget[i]->setWindowTitle(_("Chat"));
      break;
    case 1: //MESSAGE_DOCK_WIDGET
      dock_widget[i]->setWidget(messages_window);
      dock_widget[i]->setFeatures(QDockWidget::DockWidgetMovable
                                  | QDockWidget::DockWidgetFloatable);
      dock_widget[i]->setWindowTitle(_("Messages"));
      dock_widget[i]->setFocusPolicy(Qt::NoFocus);
      break;
    default:
      break;
    }
  }
}

/***************************************************************************
  Hides ALL dock widgets
***************************************************************************/
void fc_client::hide_dock_widgets()
{
  int i;

  for (i = OUTPUT_DOCK_WIDGET ; i <  LAST_WIDGET; i++) {
    dock_widget[i]->hide();
  }
}

/***************************************************************************
  Shows one widget = dw
***************************************************************************/
void fc_client::show_dock_widget(int dw)
{
  int i;

  for (i = OUTPUT_DOCK_WIDGET ; i <  LAST_WIDGET; i++) {
    if (i == dw) {
      dock_widget[i]->show();
    }
  }
}

/***************************************************************************
  Updates saves to load and updates in tableview = saves_load
***************************************************************************/
void fc_client::update_load_page(void)
{
  struct fileinfo_list *files = fileinfolist_infix(get_save_dirs(),
                                                   ".sav", false);
  int row = 0;
  saves_load->clearContents();
  saves_load->setRowCount(0);
  fileinfo_list_iterate(files, pfile) {
    QTableWidgetItem *item;
    item = new QTableWidgetItem();
    saves_load->insertRow(row);
    item->setText(pfile->name);
    saves_load->setItem(row, 0, item);
    item = new QTableWidgetItem();
    QDateTime dt = QDateTime::fromTime_t(pfile->mtime);
    item->setText(dt.toString(Qt::TextDate));
    saves_load->setItem(row, 1, item);
    row++;
  }
  fileinfo_list_iterate_end;
  fileinfo_list_destroy(files);
}

/***************************************************************************
  Gets scenarios list and updates it in TableWidget = scenarios_load
***************************************************************************/
void fc_client::update_scenarios_page(void)
{
  struct fileinfo_list *files = fileinfolist_infix(get_scenario_dirs(),
                                                   ".sav", false);
  int row = 0;
  scenarios_load->clearContents();
  scenarios_load->setRowCount(0);
  fileinfo_list_iterate(files, pfile) {
    QTableWidgetItem *item;

    item = new QTableWidgetItem();
    scenarios_load->insertRow(row);
    item->setText(pfile->name);
    scenarios_load->setItem(row, 0, item);
    scenarios_view->setText(pfile->fullname);
    row++;
  }
  fileinfo_list_iterate_end;
  fileinfo_list_destroy(files);
}

/**************************************************************************
  configure the dialog depending on what type of authentication request the
  server is making.
**************************************************************************/
void fc_client::handle_authentication_req(enum authentication_type type,
                                          const char *message)
{
  set_status_bar(QString::fromUtf8(message));

  switch (type) {
  case AUTH_NEWUSER_FIRST:
  case AUTH_NEWUSER_RETRY:
    set_connection_state(NEW_PASSWORD_TYPE);
    return;
  case AUTH_LOGIN_FIRST:

    /* if we magically have a password already present in 'password'
     * then, use that and skip the password entry dialog */
    if (password[0] != '\0') {
      struct packet_authentication_reply reply;

      sz_strlcpy(reply.password, password);
      send_packet_authentication_reply(&client.conn, &reply);
      return;
    } else {
      set_connection_state(ENTER_PASSWORD_TYPE);
    }

    return;
  case AUTH_LOGIN_RETRY:
    set_connection_state(ENTER_PASSWORD_TYPE);
    return;
  }

  log_error("Unsupported authentication type %d: %s.", type, message);
}

/**************************************************************************
  if on the network page, switch page to the login page (with new server
  and port). if on the login page, send connect and/or authentication
  requests to the server.
**************************************************************************/
void fc_client::slot_connect()
{
  char errbuf [512];
  struct packet_authentication_reply reply;

  switch (connection_status) {
  case LOGIN_TYPE:
    connect_password_edit->setDisabled(true);
    connect_confirm_password_edit->setDisabled(true);
    sz_strlcpy(user_name, connect_login_edit->text().toLatin1().data());
    sz_strlcpy(server_host, connect_host_edit->text().toLatin1().data());
    server_port = atoi(connect_port_edit->text().toLatin1().data());

    if (connect_to_server(user_name, server_host, server_port,
                          errbuf, sizeof(errbuf)) != -1) {
    } else {
      set_status_bar(QString::fromUtf8(errbuf));
      output_window_append(ftc_client, errbuf);
    }

    return;
  case NEW_PASSWORD_TYPE:
    connect_password_edit->setDisabled(false);
    connect_confirm_password_edit->setDisabled(false);
    sz_strlcpy(password, connect_password_edit->text().toLatin1().data());
    sz_strlcpy(reply.password,
               connect_confirm_password_edit->text().toLatin1().data());

    if (strncmp(reply.password, password, MAX_LEN_NAME) == 0) {
      password[0] = '\0';
      send_packet_authentication_reply(&client.conn, &reply);
      set_connection_state(WAITING_TYPE);
    } else {
      set_status_bar(_("Passwords don't match, enter password."));
      set_connection_state(NEW_PASSWORD_TYPE);
    }

    return;
  case ENTER_PASSWORD_TYPE:
    connect_password_edit->setDisabled(false);
    connect_confirm_password_edit->setDisabled(false);
    sz_strlcpy(reply.password,
               connect_password_edit->text().toLatin1().data());
    send_packet_authentication_reply(&client.conn, &reply);
    set_connection_state(WAITING_TYPE);
    return;
  case WAITING_TYPE:
    return;
  }

  log_error("Unsupported connection status: %d", connection_status);
}
