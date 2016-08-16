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

// Qt
#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QHeaderView>
#include <QLineEdit>
#include <QTableWidget>
#include <QTextEdit>
#include <QTreeWidget>

// common
#include "game.h"
#include "version.h"

// client
#include "client_main.h"
#include "connectdlg_common.h"

// gui-qt
#include "dialogs.h"
#include "pages.h"
#include "sprite.h"
#include "voteinfo_bar.h"

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
  int i;
  int def_idx = -1;

  gui()->pr_options->cruleset->clear();
  gui()->pr_options->cruleset->blockSignals(true);
  for (i = 0; i < num_rulesets; i++){
    gui()->pr_options->cruleset->addItem(rulesets[i], i);
    if (!strcmp("default", rulesets[i])) {
      def_idx = i;
    }
  }

  /* HACK: server should tell us the current ruleset. */
  gui()->pr_options->cruleset->setCurrentIndex(def_idx);
  gui()->pr_options->cruleset->blockSignals(false);
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
  gui()->update_start_page();
}


/**************************************************************************
  Creates buttons and layouts for start page.
**************************************************************************/
void fc_client::create_main_page(void)
{

  QPixmap main_graphics(tileset_main_intro_filename(tileset));
  QLabel* free_main_pic = new QLabel;
  QPainter painter(&main_graphics);
  QStringList buttons_names;
  int buttons_nr;
  char msgbuf[128];
  const char *rev_ver;
  QFont f = QApplication::font();
  QFontMetrics fm(f);
  int row = 0;
#if IS_BETA_VERSION
  QPalette warn_color;
  QFont *fe;
  fe = fc_fonts.get_font("gui_qt_font_beta_label");
  QLabel *beta_label = new QLabel(beta_message());
  beta_label->setFont(*fe);
#endif /* IS_BETA_VERSION */

  pages_layout[PAGE_MAIN] = new QGridLayout;

  painter.setPen(Qt::white);

  rev_ver = fc_svn_revision();
  if (rev_ver == NULL) {
    rev_ver = fc_git_revision();

    if (rev_ver == NULL) {
      /* TRANS: "version 2.6.0, Qt client" */
      fc_snprintf(msgbuf, sizeof(msgbuf), _("%s%s, Qt client"),
                  word_version(), VERSION_STRING);
    } else {
      fc_snprintf(msgbuf, sizeof(msgbuf), "%s%s",
                  word_version(), VERSION_STRING);
      painter.drawText(10,
                       main_graphics.height() - fm.descent() - fm.height() * 2,
                       msgbuf);

      /* TRANS: "commit: [modified] <git commit id>" */
      fc_snprintf(msgbuf, sizeof(msgbuf), _("commit: %s"), rev_ver);
      painter.drawText(10,
                       main_graphics.height() - fm.descent() - fm.height(),
                       msgbuf);

      strncpy(msgbuf, _("Qt client"), sizeof(msgbuf));
    }
  } else {
    /* TRANS: "version 2.6.0 (r25000), Qt client" */
    fc_snprintf(msgbuf, sizeof(msgbuf), _("%s%s (%s), Qt client"),
                word_version(), VERSION_STRING, rev_ver);
  }

  painter.drawText(main_graphics.width()-fm.width(msgbuf)-10,
                   main_graphics.height()-fm.descent(), msgbuf);
  free_main_pic->setPixmap(main_graphics);
  pages_layout[PAGE_MAIN]->addWidget(free_main_pic,
                                     row++, 0, 1, 2, Qt::AlignCenter);

#if IS_BETA_VERSION
  warn_color.setColor(QPalette::WindowText, Qt::red);
  beta_label->setPalette(warn_color);
  beta_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Maximum);
  beta_label->setAlignment(Qt::AlignCenter);
  pages_layout[PAGE_MAIN]->addWidget(beta_label,
                                     row++, 0, 1, 2, Qt::AlignHCenter);
#endif

  buttons_names << _("Start new game") << _("Start scenario game")
                << _("Load saved game") << _("Connect to network game")
                << _("Options") << _("Quit");

  buttons_nr = buttons_names.count();

  for (int iter = 0; iter < buttons_nr; iter++) {
    button = new QPushButton(buttons_names[iter]);

    switch (iter) {
    case 0:
      pages_layout[PAGE_MAIN]->addWidget(button, row + 1, 0);
      connect(button, SIGNAL(clicked()), this, SLOT(start_new_game()));
      break;
    case 1:
      pages_layout[PAGE_MAIN]->addWidget(button, row + 2, 0);
      connect(button, SIGNAL(clicked()), switch_page_mapper, SLOT(map()));
      switch_page_mapper->setMapping(button, PAGE_SCENARIO);
      break;
    case 2:
      pages_layout[PAGE_MAIN]->addWidget(button, row + 3, 0);
      connect(button, SIGNAL(clicked()), switch_page_mapper, SLOT(map()));
      switch_page_mapper->setMapping(button, PAGE_LOAD);
      break;
    case 3:
      pages_layout[PAGE_MAIN]->addWidget(button, row + 1, 1);
      connect(button, SIGNAL(clicked()), switch_page_mapper, SLOT(map()));
      switch_page_mapper->setMapping(button, PAGE_NETWORK);
      break;
    case 4:
      pages_layout[PAGE_MAIN]->addWidget(button, row + 2, 1);
      connect(button, SIGNAL(clicked()), this, SLOT(popup_client_options()));
      break;
    case 5:
      pages_layout[PAGE_MAIN]->addWidget(button, row + 3, 1);
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
    connect_password_edit->setDisabled(true);
    connect_confirm_password_edit->setText("");
    connect_confirm_password_edit->setDisabled(true);
    break;
  case NEW_PASSWORD_TYPE:
    connect_password_edit->setText("");
    connect_confirm_password_edit->setText("");
    connect_password_edit->setDisabled(false);
    connect_confirm_password_edit->setDisabled(false);
    connect_password_edit->setFocus(Qt::OtherFocusReason);
    break;
  case ENTER_PASSWORD_TYPE:
    connect_password_edit->setText("");
    connect_confirm_password_edit->setText("");
    connect_password_edit->setDisabled(false);
    connect_confirm_password_edit->setDisabled(true);
    connect_password_edit->setFocus(Qt::OtherFocusReason);


    break;
  case WAITING_TYPE:
    set_status_bar("");
    break;
  }

  connection_status = state;
}

/***************************************************************************
  Creates buttons and layouts for network page.
***************************************************************************/
void fc_client::create_network_page(void)
{
  QHeaderView *header;
  QLabel *connect_msg;
  QLabel *lan_label;
  QPushButton *network_button;

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
  connect_lan = new QWidget;
  connect_metaserver = new QWidget;
  lan_widget = new QTableWidget;
  wan_widget = new QTableWidget;
  info_widget = new QTableWidget;

  QStringList servers_list;
  servers_list << _("Server Name") << _("Port") << _("Version")
               << _("Status") << Q_("?count:Players") << _("Comment");
  QStringList server_info;
  server_info << _("Name") << _("Type") << _("Host") << _("Nation");

  lan_widget->setRowCount(0);
  lan_widget->setColumnCount(servers_list.count());
  lan_widget->verticalHeader()->setVisible(false);
  lan_widget->setAutoScroll(false);

  wan_widget->setRowCount(0);
  wan_widget->setColumnCount(servers_list.count());
  wan_widget->verticalHeader()->setVisible(false);
  wan_widget->setAutoScroll(false);

  info_widget->setRowCount(0);
  info_widget->setColumnCount(server_info.count());
  info_widget->verticalHeader()->setVisible(false);

  lan_widget->setHorizontalHeaderLabels(servers_list);
  lan_widget->setProperty("showGrid", "false");
  lan_widget->setProperty("selectionBehavior", "SelectRows");
  lan_widget->setEditTriggers(QAbstractItemView::NoEditTriggers);
  lan_widget->setSelectionMode(QAbstractItemView::SingleSelection);

  wan_widget->setHorizontalHeaderLabels(servers_list);
  wan_widget->setProperty("showGrid", "false");
  wan_widget->setProperty("selectionBehavior", "SelectRows");
  wan_widget->setEditTriggers(QAbstractItemView::NoEditTriggers);
  wan_widget->setSelectionMode(QAbstractItemView::SingleSelection);

  connect(wan_widget->selectionModel(),
          SIGNAL(selectionChanged(const QItemSelection &, const QItemSelection &)), this,
          SLOT(slot_selection_changed(const QItemSelection &, const QItemSelection &)));

  connect(lan_widget->selectionModel(),
          SIGNAL(selectionChanged(const QItemSelection &, const QItemSelection &)), this,
          SLOT(slot_selection_changed(const QItemSelection &, const QItemSelection &)));

  info_widget->setHorizontalHeaderLabels(server_info);
  info_widget->setProperty("selectionBehavior", "SelectRows");
  info_widget->setEditTriggers(QAbstractItemView::NoEditTriggers);
  info_widget->setSelectionMode(QAbstractItemView::SingleSelection);
  info_widget->setProperty("showGrid", "false");
  info_widget->setAlternatingRowColors(true);

  header = lan_widget->horizontalHeader();
  header->setSectionResizeMode(0, QHeaderView::Stretch);
  header->setStretchLastSection(true);
  header = wan_widget->horizontalHeader();
  header->setSectionResizeMode(0, QHeaderView::Stretch);
  header->setStretchLastSection(true);
  header = info_widget->horizontalHeader();
  header->setSectionResizeMode(0, QHeaderView::Stretch);
  header->setStretchLastSection(true);

  QStringList label_names;
  label_names << _("Connect") << _("Port") << _("Login")
              << _("Password") << _("Confirm Password");

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

  network_button = new QPushButton(_("Refresh"));
  QObject::connect(network_button, SIGNAL(clicked()), this,
                   SLOT(update_network_lists()));
  page_network_grid_layout->addWidget(network_button, 5, 0);

  network_button = new QPushButton(_("Cancel"));
  connect(network_button, SIGNAL(clicked()), switch_page_mapper,
          SLOT(map()));
  switch_page_mapper->setMapping(network_button, PAGE_MAIN);
  page_network_grid_layout->addWidget(network_button, 5, 2, 1, 1);

  network_button = new QPushButton(_("Connect"));
  page_network_grid_layout->addWidget(network_button, 5, 5, 1, 1);
  connect(network_button, SIGNAL(clicked()), this, SLOT(slot_connect()));
  connect(connect_password_edit, SIGNAL(returnPressed()),
          network_button, SIGNAL(clicked()));
  connect(connect_confirm_password_edit, SIGNAL(returnPressed()),
          network_button, SIGNAL(clicked()));

  connect_lan->setLayout(page_network_lan_layout);
  connect_metaserver->setLayout(page_network_wan_layout);
  page_network_lan_layout->addWidget(lan_widget, 0);
  page_network_wan_layout->addWidget(wan_widget, 1);
  lan_label = new QLabel(_("Internet servers:"));
  page_network_layout->addWidget(lan_label, 1);
  page_network_layout->addWidget(wan_widget, 10);
  lan_label = new QLabel(_("Local servers:"));
  page_network_layout->addWidget(lan_label, 1);
  page_network_layout->addWidget(lan_widget, 1);
  page_network_grid_layout->setColumnStretch(3, 4);
  pages_layout[PAGE_NETWORK]->addLayout(page_network_layout, 1, 1);
  pages_layout[PAGE_NETWORK]->addLayout(page_network_grid_layout, 2, 1);

}

/***************************************************************************
  Sets application status bar for given time in miliseconds
***************************************************************************/
void fc_client::set_status_bar(QString message, int timeout)
{
  if (status_bar_label->text().isEmpty()) {
    status_bar_label->setText(message);
    QTimer::singleShot(timeout, this, SLOT(clear_status_bar()));
  } else {
    status_bar_queue.append(message);
    while (status_bar_queue.count() > 3){
      status_bar_queue.removeFirst();
    }
  }
}

/***************************************************************************
  Clears status bar or shows next message in queue if exists
***************************************************************************/
void fc_client::clear_status_bar()
{
  QString str;
  if (status_bar_queue.isEmpty() == false) {
    str = status_bar_queue.takeFirst();
    status_bar_label->setText(str);
    QTimer::singleShot(2000, this, SLOT(clear_status_bar()));
  } else {
    status_bar_label->setText("");
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
  sav << _("Choose Saved Game to Load") << _("Date");

  saves_load->setRowCount (0);
  saves_load->setColumnCount (sav.count());
  saves_load->setHorizontalHeaderLabels (sav);

  saves_load->setProperty("showGrid", "false");
  saves_load->setProperty("selectionBehavior", "SelectRows");
  saves_load->setEditTriggers(QAbstractItemView::NoEditTriggers);
  saves_load->setSelectionMode(QAbstractItemView::SingleSelection);
  saves_load->verticalHeader()->setVisible(false);

  connect(saves_load->selectionModel(),
          SIGNAL(selectionChanged(const QItemSelection &, const QItemSelection &)), this,
          SLOT(slot_selection_changed(const QItemSelection &, const QItemSelection &)));

  QHeaderView *header;
  header = saves_load->horizontalHeader();
  header->setSectionResizeMode(0, QHeaderView::Stretch);
  header->setStretchLastSection(true);

  pages_layout[PAGE_LOAD]->addWidget(saves_load, 0, 0, 1, 4);
  connect(saves_load->selectionModel(),
          SIGNAL(selectionChanged(const QItemSelection &, const QItemSelection &)), this,
          SLOT(slot_selection_changed(const QItemSelection &, const QItemSelection &)));

  but = new QPushButton;
  but->setText(_("Browse..."));
  but->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon));
  connect(but, SIGNAL(clicked()), this, SLOT(browse_saves()));
  pages_layout[PAGE_LOAD]->addWidget (but, 1, 0);

  but = new QPushButton;
  but->setText(_("Cancel"));
  but->setIcon(QApplication::style()->standardIcon(
                                      QStyle::SP_DialogCancelButton));
  connect(but, SIGNAL(clicked()), this, SLOT(slot_disconnect()));
  switch_page_mapper->setMapping(but, PAGE_MAIN);
  pages_layout[PAGE_LOAD]->addWidget(but, 1, 2);

  but = new QPushButton;
  but->setText(_("Load"));
  but->setIcon(QApplication::style()->standardIcon(
                                      QStyle::SP_DialogOkButton));
  connect(but, SIGNAL(clicked()), this, SLOT(start_from_save()));
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
  scenarios_text = new QLabel;
  QStringList sav;
  sav << _("Choose a Scenario");
  scenarios_load->setRowCount(0);
  scenarios_load->setColumnCount(sav.count());
  scenarios_load->setHorizontalHeaderLabels(sav);
  scenarios_load->setProperty("showGrid", "false");
  scenarios_load->setProperty("selectionBehavior", "SelectRows");
  scenarios_load->setEditTriggers(QAbstractItemView::NoEditTriggers);
  scenarios_load->setSelectionMode(QAbstractItemView::SingleSelection);
  scenarios_load->verticalHeader()->setVisible(false);
  pages_layout[PAGE_SCENARIO]->addWidget(scenarios_load, 0, 0, 3, 3);
  pages_layout[PAGE_SCENARIO]->addWidget(scenarios_view, 0, 3, 3, 2);
  pages_layout[PAGE_SCENARIO]->addWidget(scenarios_text, 3, 0, 1, 4);
  scenarios_view->setReadOnly(true);
  scenarios_view->setWordWrapMode(QTextOption::WordWrap);

  QHeaderView *header;
  header = scenarios_load->horizontalHeader();
  header->setSectionResizeMode(0, QHeaderView::Stretch);
  header->setStretchLastSection(true);
  connect(scenarios_load->selectionModel(),
          SIGNAL(selectionChanged(const QItemSelection &, const QItemSelection &)), this,
          SLOT(slot_selection_changed(const QItemSelection &, const QItemSelection &)));

  but = new QPushButton;
  but->setText(_("Browse..."));
  but->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon));
  connect(but, SIGNAL(clicked()), this, SLOT(browse_scenarios()));
  pages_layout[PAGE_SCENARIO]->addWidget (but, 4, 0);

  but = new QPushButton;
  but->setText(_("Cancel"));
  but->setIcon(QApplication::style()->standardIcon(
                                        QStyle::SP_DialogCancelButton));
  connect(but, SIGNAL(clicked()), this, SLOT(slot_disconnect()));
  switch_page_mapper->setMapping(but, PAGE_MAIN);
  pages_layout[PAGE_SCENARIO]->addWidget(but, 4, 3);

  but = new QPushButton;
  but->setText(_("Load Scenario"));
  but->setIcon(QApplication::style()->standardIcon(
                                        QStyle::SP_DialogOkButton));
  connect(but, SIGNAL(clicked()), this, SLOT(start_scenario()));
  pages_layout[PAGE_SCENARIO]->addWidget(but, 4, 4);
}

/***************************************************************************
  Creates buttons and layouts for start page.
***************************************************************************/
void fc_client::create_start_page()
{
  QPushButton *but;
  QStringList player_widget_list;
  pages_layout[PAGE_START] = new QGridLayout;
  start_players_tree = new QTreeWidget;
  pr_options = new pregame_options();
  chat_line = new QLineEdit;
  output_window = new QTextEdit;
  output_window->setReadOnly(false);
  chat_line->installEventFilter(this);

  pr_options->init();
  player_widget_list << _("Name") << _("Ready") << _("Leader")
                     << _("Flag") << _("Nation") << _("Team");


  start_players_tree->setColumnCount(player_widget_list.count());
  start_players_tree->setHeaderLabels(player_widget_list);
  start_players_tree->setContextMenuPolicy(Qt::CustomContextMenu);
  start_players_tree->setProperty("selectionBehavior", "SelectRows");
  start_players_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
  start_players_tree->setRootIsDecorated(false);

  connect(start_players_tree,
          SIGNAL(customContextMenuRequested(const QPoint&)),
          SLOT(start_page_menu(QPoint)));

  pages_layout[PAGE_START]->addWidget(start_players_tree, 0, 0, 3, 6);
  pages_layout[PAGE_START]->addWidget(pr_options, 0, 6, 3, 2);
  but = new QPushButton;
  but->setText(_("Disconnect"));
  but->setIcon(style()->standardPixmap(QStyle::SP_DialogCancelButton));
  QObject::connect(but, SIGNAL(clicked()), this, SLOT(slot_disconnect()));
  pages_layout[PAGE_START]->addWidget(but, 5, 4);
  but = new QPushButton;
  but->setText(_("Pick Nation"));
  but->setIcon(fc_icons::instance()->get_icon("flag"));
  pages_layout[PAGE_START]->addWidget(but, 5, 5);
  QObject::connect(but, SIGNAL(clicked()), this,
                   SLOT(slot_pick_nation()));

  obs_button = new QPushButton;
  obs_button->setText(_("Observe"));
  obs_button->setIcon(fc_icons::instance()->get_icon("meeting-observer"));
  pages_layout[PAGE_START]->addWidget(obs_button, 5, 6);
  QObject::connect(obs_button, SIGNAL(clicked()), this,
                   SLOT(slot_pregame_observe()));
  but = new QPushButton;
  but->setText(_("Start"));
  but->setIcon(style()->standardPixmap(QStyle::SP_DialogOkButton));
  pages_layout[PAGE_START]->addWidget(but, 5, 7);
  QObject::connect(but, SIGNAL(clicked()), this,
                   SLOT(slot_pregame_start()));
  pre_vote = new pregamevote;

  pages_layout[PAGE_START]->addWidget(pre_vote, 4, 0, 1, 4);
  pages_layout[PAGE_START]->addWidget(chat_line, 5, 0, 1, 4);
  pages_layout[PAGE_START]->addWidget(output_window, 3, 0, 1, 8);
  connect(chat_line, SIGNAL(returnPressed()), this, SLOT(chat()));

}

/***************************************************************************
  Creates buttons and layouts for game page.
***************************************************************************/
void fc_client::create_game_page()
{
  QGridLayout *game_layout;

  pages_layout[PAGE_GAME] = new QGridLayout;
  game_main_widget = new QWidget;
  game_layout = new QGridLayout;
  game_tab_widget = new fc_game_tab_widget;
  game_layout->setContentsMargins(0, 0, 0, 0);
  game_layout->setSpacing(0);
  game_tab_widget->setTabPosition(QTabWidget::South);
  game_tab_widget->setDocumentMode(false);
  mapview_wdg = new map_view();
  mapview_wdg->setFocusPolicy(Qt::WheelFocus);
  minimapview_wdg = new minimap_view(mapview_wdg);
  minimapview_wdg->show();
  unitinfo_wdg = new unit_label(mapview_wdg);
  unitinfo_wdg->show();
  game_info_label = new info_label(mapview_wdg);
  game_info_label->show();
  end_turn_rect =  new end_turn_area(mapview_wdg);
  end_turn_rect->show();
  infotab = new info_tab(mapview_wdg);
  infotab->show();
  x_vote = new xvote(mapview_wdg);
  x_vote->hide();
  gtd = new goto_dialog(mapview_wdg);
  gtd->hide();

  game_layout->addWidget(mapview_wdg, 1, 0);
  game_main_widget->setLayout(game_layout);

  game_tab_widget->setMinimumSize(600,400);
  game_tab_widget->setTabsClosable(true);
  add_game_tab(game_main_widget, Q_("?noun:View"));

  QObject::connect(game_tab_widget, SIGNAL(tabCloseRequested(int)),
                   SLOT(slot_close_widget(int)));

  pages_layout[PAGE_GAME]->addWidget(game_tab_widget, 1, 0);
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
int fc_client::add_game_tab(QWidget *widget, QString title)
{
  return game_tab_widget->addTab(widget, title);
}

/***************************************************************************
  Removes given tab widget from game page
***************************************************************************/
void fc_client::rm_game_tab(int index)
{
  game_tab_widget->removeTab(index);
}

/***************************************************************************
  Browse saves directory
***************************************************************************/
void fc_client::browse_saves(void)
{
  QString str;
  str = QString(_("Save Files"))
        + QString(" (*.sav *.sav.bz2 *.sav.gz *.sav.xz)");
  current_file = QFileDialog::getOpenFileName(gui()->central_wdg,
                                              _("Open Save File"),
                                              QDir::homePath(), str);
  if (current_file.isEmpty() ==  false) {
    start_from_save();
  }
}

/***************************************************************************
  Browse scenarios directory
***************************************************************************/
void fc_client::browse_scenarios(void)
{
  QString str;
  str = QString(_("Scenarios Files"))
        + QString(" (*.sav *.sav.bz2 *.sav.gz *.sav.xz)");
  current_file = QFileDialog::getOpenFileName(gui()->central_wdg,
                                              _("Open Scenario File"),
                                              QDir::homePath(), str);
  if (current_file.isEmpty() == false) {
    start_scenario();
  }
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
  int row;
  int old_row_count;

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
  old_row_count = sel->rowCount();
  sel->clearContents();
  row = 0;
  server_list_iterate(list, pserver) {
    char buf[20];
    if (old_row_count <= row) {
      sel->insertRow(row);
    }

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
  } server_list_iterate_end;

  /* Remove unneeded rows, if there are any */
  while (old_row_count - row > 0) {
    sel->removeRow(old_row_count - 1);
    old_row_count--;
  }

}

/**************************************************************************
  Callback function for when there's an error in the server scan.
**************************************************************************/
void server_scan_error(struct server_scan *scan, const char *message)
{
  // version_message doesn't take a const char *, so we need to copy the
  // message
  QString copy = QString::fromUtf8(message);
  qtg_version_message(copy.toUtf8().data());
  log_error("%s", message);

  /* Main thread will finalize the scan later (or even concurrently) -
   * do not do anything here to cause double free or raze condition. */
}


/**************************************************************************
  Free the server scans.
**************************************************************************/
void fc_client::destroy_server_scans(void)
{
  if (meta_scan) {
    server_scan_finish(meta_scan);
    meta_scan = NULL;
  }

  if (meta_scan_timer != NULL) {
    meta_scan_timer->stop();
    meta_scan_timer->disconnect();
    delete meta_scan_timer;
    meta_scan_timer = NULL;
  }

  if (lan_scan) {
    server_scan_finish(lan_scan);
    lan_scan = NULL;
  }

  if (lan_scan_timer != NULL) {
    lan_scan_timer->stop();
    lan_scan_timer->disconnect();
    delete lan_scan_timer;
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

  meta_scan_timer = new QTimer(this);
  meta_scan = server_scan_begin(SERVER_SCAN_GLOBAL, server_scan_error);
  connect(meta_scan_timer, SIGNAL(timeout()), this, SLOT(slot_meta_scan()));
  meta_scan_timer->start(800);

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
  if (lan_scan_timer == NULL) {
    return;
  }
  check_server_scan(lan_scan);
}

/***************************************************************************
  Executes metaserver scan network
***************************************************************************/
void fc_client::slot_meta_scan()
{
  if (meta_scan_timer == NULL) {
    return;
  }
  check_server_scan(meta_scan);
}

/**************************************************************************
  spawn a server, if there isn't one, using the default settings.
**************************************************************************/
void fc_client::start_new_game()
{
  if (is_server_running() || client_start_server()) {
    /* saved settings are sent in client/options.c load_settable_options() */
  }
}

/**************************************************************************
  Starts game from chosen scenario - chosen_file (save or scenario)
**************************************************************************/
void fc_client::start_scenario()
{
  if (!is_server_running()){
    client_start_server();
    send_chat("/detach");
  }
  send_chat_printf("/load %s", current_file.toLocal8Bit().data());
  switch_page(PAGE_START);
}

/**************************************************************************
  Starts game from chosen save - chosen_file (save or scenario)
**************************************************************************/
void fc_client::start_from_save()
{
  if (!is_server_running()){
    client_start_server();
    send_chat("/detach");
  }
  send_chat_printf("/load %s", current_file.toLocal8Bit().data());
  switch_page(PAGE_START);
}

/***************************************************************************
  Selection chnaged in some tableview on some page
***************************************************************************/
void fc_client::slot_selection_changed(const QItemSelection &selected,
                                       const QItemSelection &deselected)
{

  QModelIndexList indexes = selected.indexes();
  QStringList sl;
  QModelIndex index;
  QTableWidgetItem *item;
  QItemSelectionModel *tw;
  QVariant qvar;
  int k, col, n;
  client_pages i = current_page();
  const struct server *pserver = NULL;
  struct srv_list *srvrs;

  if (indexes.isEmpty()) {
    return;
  }
  k = 0;
  switch (i) {
  case PAGE_NETWORK:
    index = indexes.at(0);
    connect_host_edit->setText(index.data().toString());
    index = indexes.at(1);
    connect_port_edit->setText(index.data().toString());

    tw = qobject_cast<QItemSelectionModel *>(sender());

    if (tw == lan_widget->selectionModel()) {
      wan_widget->clearSelection();
    } else {
      lan_widget->clearSelection();
    }

    srvrs = server_scan_get_list(meta_scan);
    if (!holding_srv_list_mutex) {
      fc_allocate_mutex(&srvrs->mutex);
    }
    if (srvrs->servers) {
      pserver = server_list_get(srvrs->servers, index.row());
    }
    if (!holding_srv_list_mutex) {
      fc_release_mutex(&srvrs->mutex);
    }
    if (!pserver || !pserver->players) {
      return;
    }
    n = pserver->nplayers;
    info_widget->clearContents();
    info_widget->setRowCount(0);
    for (k = 0; k < n; k++) {
      info_widget->insertRow(k);
      for (col = 0; col < 4; col++) {
        item = new QTableWidgetItem();
        switch (col) {
        case 0:
          item->setText(pserver->players[k].name);
          break;
        case 1:
          item->setText(pserver->players[k].type);
          break;
        case 2:
          item->setText(pserver->players[k].host);
          break;
        case 3:
          item->setText(pserver->players[k].nation);
          break;
        default:
          break;
        }
        info_widget->setItem(k, col, item);
      }
    }
  break;
  case PAGE_SCENARIO:
    index = indexes.at(0);
    qvar = index.data(Qt::UserRole);
    sl = qvar.toStringList();
    scenarios_view->setText(sl.at(0));
    if (sl.count() > 1){
      scenarios_text->setText(sl.at(2));
      current_file = sl.at(1);
    }
    break;
  case PAGE_LOAD:
    index = indexes.at(0);
    qvar = index.data(Qt::UserRole);
    current_file = qvar.toString();
  default:
    break;
  }

}

/***************************************************************************
  Updates saves to load and updates in tableview = saves_load
***************************************************************************/
void fc_client::update_load_page(void)
{
  struct fileinfo_list *files;
  int row;

  row = 0;
  files = fileinfolist_infix(get_save_dirs(), ".sav", FALSE);
  saves_load->clearContents();
  saves_load->setRowCount(0);
  fileinfo_list_iterate(files, pfile) {
    QTableWidgetItem *item;
    item = new QTableWidgetItem();
    item->setData(Qt::UserRole, pfile->fullname);
    saves_load->insertRow(row);
    item->setText(pfile->name);
    saves_load->setItem(row, 0, item);
    item = new QTableWidgetItem();
    QDateTime dt = QDateTime::fromTime_t(pfile->mtime);
    item->setText(dt.toString(Qt::TextDate));
    saves_load->setItem(row, 1, item);
    row++;
  } fileinfo_list_iterate_end;
  fileinfo_list_destroy(files);
}

/***************************************************************************
  Gets scenarios list and updates it in TableWidget = scenarios_load
***************************************************************************/
void fc_client::update_scenarios_page(void)
{
  struct fileinfo_list *files;
  int row = 0;

  scenarios_load->clearContents();
  scenarios_load->setRowCount(0);
  scenarios_text->setText("");
  scenarios_view->setText("");

  files = fileinfolist_infix(get_scenario_dirs(), ".sav", TRUE);
  fileinfo_list_iterate(files, pfile) {
    struct section_file *sf;

    if ((sf = secfile_load_section(pfile->fullname, "scenario", TRUE))
        && secfile_lookup_bool_default(sf, TRUE, "scenario.is_scenario")) {

      const char *sname, *sdescription;
      QTableWidgetItem *item;
      QString str = QString(pfile->name);
      QStringList sl;

      sname = secfile_lookup_str_default(sf, NULL, "scenario.name");
      sdescription = secfile_lookup_str_default(sf, NULL,
                                              "scenario.description");
      item = new QTableWidgetItem();
      scenarios_load->insertRow(row);
      item->setText(pfile->name);
      sl << (sname && strlen(sname) ? Q_(sname) : pfile->name)
         << pfile->fullname
         << (NULL != sdescription && '\0' != sdescription[0]
             ? Q_(sdescription) : "");
      item->setData(Qt::UserRole, sl);
      scenarios_load->setItem(row, 0, item);
      row++;
      secfile_destroy(sf);
    }
  } fileinfo_list_iterate_end;
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
  destroy_server_scans();

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
    sz_strlcpy(user_name, connect_login_edit->text().toLocal8Bit().data());
    sz_strlcpy(server_host, connect_host_edit->text().toLocal8Bit().data());
    server_port = connect_port_edit->text().toInt();

    if (connect_to_server(user_name, server_host, server_port,
                          errbuf, sizeof(errbuf)) != -1) {
    } else {
      set_status_bar(QString::fromUtf8(errbuf));
      output_window_append(ftc_client, errbuf);
    }

    return;
  case NEW_PASSWORD_TYPE:
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

/***************************************************************************
 Updates start page (start page = client connected to server, but game not
 started )
***************************************************************************/
void fc_client::update_start_page()
{
  int conn_num, i;
  QVariant qvar, qvar2;
  bool is_ready;
  QString nation, leader, team, str;
  QPixmap *pixmap;
  struct sprite *psprite;
  QTreeWidgetItem *item;
  QTreeWidgetItem *item_r;
  QList <QTreeWidgetItem*> items;
  QList <QTreeWidgetItem*> recursed_items;
  QTreeWidgetItem *player_item;
  QTreeWidgetItem *global_item;
  QTreeWidgetItem *detach_item;
  int conn_id;
  conn_num = conn_list_size(game.est_connections);

  if (conn_num == 0) {
    return;
  }

  start_players_tree->clear();
  qvar2 = 0;

  player_item = new QTreeWidgetItem();
  player_item->setText(0, Q_("?header:Players"));
  player_item->setData(0, Qt::UserRole, qvar2);

  i = 0;
  players_iterate(pplayer) {
    i++;
  } players_iterate_end;
  gui()->pr_options->max_players->blockSignals(true);
  gui()->pr_options->max_players->setValue(i);
  gui()->pr_options->max_players->blockSignals(false);
  /**
   * Inserts playing players, observing custom players, and AI )
   */

  players_iterate(pplayer) {
    item = new QTreeWidgetItem();
    conn_id = -1;
    conn_list_iterate(pplayer->connections, pconn) {
      if (pconn->playing == pplayer && !pconn->observer) {
        conn_id = pconn->id;
        break;
      }
    } conn_list_iterate_end;
    if (is_barbarian(pplayer)) {
      continue;
    }
    if (pplayer->ai_controlled) {
      is_ready = true;
    } else {
      is_ready = pplayer->is_ready;
    }

    if (pplayer->nation == NO_NATION_SELECTED) {
      nation = _("Random");

      if (pplayer->was_created) {
        leader = player_name(pplayer);
      } else {
        leader = "";
      }
    } else {
      nation = nation_adjective_for_player(pplayer);
      leader = player_name(pplayer);
    }

    if (pplayer->team) {
      team = team_name_translation(pplayer->team);
    } else {
      team = "";
    }

    for (int col = 0; col < 6; col++) {
      switch (col) {
      case 0:
        str = pplayer->username;

        if (pplayer->ai_controlled) {
          str = str + " <" + (ai_level_name(pplayer->ai_common.skill_level))
              + ">";
        }

        item->setText(col, str);
        qvar = QVariant::fromValue((void *) pplayer);
        qvar2 = 1;
        item->setData(0, Qt::UserRole, qvar2);
        item->setData(1, Qt::UserRole, qvar);
        break;
      case 1:
        if (is_ready) {
          item->setText(col, _("Yes"));
        } else {
          item->setText(col, _("No"));
        }
        break;
      case 2:
        item->setText(col, leader);
        break;
      case 3:
        if (!pplayer->nation) {
          break;
        }
        psprite = get_nation_flag_sprite(tileset, pplayer->nation);
        pixmap = psprite->pm;
        item->setData(col, Qt::DecorationRole, *pixmap);
        break;
      case 4:
        item->setText(col, nation);
        break;
      case 5:
        item->setText(col, team);
        break;
      }
    }

    /**
     * find any custom observers
     */
    recursed_items.clear();
    conn_list_iterate(pplayer->connections, pconn) {
      if (pconn->id == conn_id) {
        continue;
      }
      item_r = new QTreeWidgetItem();
      item_r->setText(0, pconn->username);
      item_r->setText(5, _("Observer"));
      recursed_items.append(item_r);
      item->addChildren(recursed_items);
    } conn_list_iterate_end;
    items.append(item);
  } players_iterate_end;

  player_item->addChildren(items);
  start_players_tree->insertTopLevelItem(0, player_item);

  /**
   * Insert global observers
   */
  items.clear();
  global_item = new QTreeWidgetItem();
  global_item->setText(0, _("Global observers"));
  qvar2 = 0;
  global_item->setData(0, Qt::UserRole, qvar2);

  conn_list_iterate(game.est_connections, pconn) {
    if (NULL != pconn->playing || !pconn->observer) {
      continue;
    }
    item = new QTreeWidgetItem();
    for (int col = 0; col < 6; col++) {
      switch (col) {
      case 0:
        item->setText(col, pconn->username);
        break;
      case 5:
        item->setText(col, _("Observer"));
        break;
      default:
        break;
      }
      items.append(item);
    }
  } conn_list_iterate_end;

  global_item->addChildren(items);
  start_players_tree->insertTopLevelItem(1, global_item);
  items.clear();

  /**
  * Insert detached
  */
  detach_item = new QTreeWidgetItem();
  detach_item->setText(0, _("Detached"));
  qvar2 = 0;
  detach_item->setData(0, Qt::UserRole, qvar2);

  conn_list_iterate(game.all_connections, pconn) {
    if (NULL != pconn->playing || pconn->observer) {
      continue;
    }
    item = new QTreeWidgetItem();
    item->setText(0, pconn->username);
    items.append(item);
  } conn_list_iterate_end;

  detach_item->addChildren(items);
  start_players_tree->insertTopLevelItem(2, detach_item);
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
  start_players_tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
#else
  start_players_tree->header()->setResizeMode(QHeaderView::ResizeToContents);
#endif
  start_players_tree->expandAll();
  update_buttons();
}

/***************************************************************************
  Updates observe button in case user started observing manually
***************************************************************************/
void fc_client::update_buttons()
{
  bool sensitive;
  QString text;
  QLayoutItem *li;
  QPushButton *pb;

  /* Observe button */
  if (client_is_observer() || client_is_global_observer()) {
    obs_button->setText(_("Don't Observe"));
  } else {
    obs_button->setText(_("Observe"));
  }

  /* Ready button */
  if (can_client_control()) {
    sensitive = true;
    if (client_player()->is_ready) {
      text = _("Not ready");
    } else {
      int num_unready = 0;

      players_iterate(pplayer) {
        if (!pplayer->ai_controlled && !pplayer->is_ready) {
          num_unready++;
        }
      } players_iterate_end;

      if (num_unready > 1) {
        text = _("Ready");
      } else {
        /* We are the last unready player so clicking here will
         * immediately start the game. */
        text = ("Start");
      }
    }
  } else {
    text = _("Start");
    if (can_client_access_hack()) {
      sensitive = true;
      players_iterate(plr) {
        if (!plr->ai_controlled) {
          /* There's human controlled player(s) in game, so it's their
           * job to start the game. */
          sensitive = false;
          break;
        }
      } players_iterate_end;
    } else {
      sensitive = false;
    }
  }
  li = pages_layout[PAGE_START]->itemAtPosition(5, 7);
  pb = qobject_cast<QPushButton *>(li->widget());
  pb->setEnabled(sensitive);
  pb->setText(text);

  /* Nation button */
  sensitive = game.info.is_new_game && can_client_control();
  li = pages_layout[PAGE_START]->itemAtPosition(5, 5);
  pb = qobject_cast<QPushButton *>(li->widget());
  pb->setEnabled(sensitive);

  sensitive = game.info.is_new_game;
  pr_options->setEnabled(sensitive);
}

/***************************************************************************
  Context menu on some player, arg Qpoint specifies some pixel on screen
***************************************************************************/
void fc_client::start_page_menu(QPoint pos)
{
  QAction *action;
  QMenu menu(start_players_tree);
  QMenu submenu_AI(start_players_tree), submenu_team(start_players_tree);
  QPoint global_pos = start_players_tree->mapToGlobal(pos);
  QString me, splayer, str, sp;
  bool need_empty_team;
  const char *level_cmd, *level_name;
  int level, count;
  QSignalMapper *player_menu_mapper;
  player *selected_player;
  QVariant qvar, qvar2;

  me = client.conn.username;
  QTreeWidgetItem *item = start_players_tree->itemAt(pos);

  if (!item) {
    return;
  }

  qvar = item->data(0, Qt::UserRole);
  qvar2 = item->data(1, Qt::UserRole);

  /**
   * qvar = 0 -> selected label -> do nothing
   * qvar = 1 -> selected player (stored in qvar2)
   */

  selected_player = NULL;
  if (qvar == 0) {
    return;
  }
  if (qvar == 1) {
    selected_player = (player *) qvar2.value < void *>();
  }
  player_menu_mapper = new QSignalMapper;
  players_iterate(pplayer) {
    if (selected_player && selected_player == pplayer) {
      splayer = QString(pplayer->name);
      sp = "\"" + splayer + "\"";
      if (me != splayer) {
        str = QString(_("Observe"));
        action = new QAction(str, start_players);
        str = "/observe " + sp;
        connect(action, SIGNAL(triggered()), player_menu_mapper,
                SLOT(map()));
        player_menu_mapper->setMapping(action, str);
        menu.addAction(action);

        if (ALLOW_CTRL <= client.conn.access_level) {
          str = QString(_("Remove player"));
          action = new QAction(str, start_players);
          str = "/remove " + sp;
          connect(action, SIGNAL(triggered()), player_menu_mapper,
                  SLOT(map()));
          player_menu_mapper->setMapping(action, str);
          menu.addAction(action);
        }
        str = QString(_("Take this player"));
        action = new QAction(str, start_players);
        str = "/take " + sp;
        connect(action, SIGNAL(triggered()), player_menu_mapper,
                SLOT(map()));
        player_menu_mapper->setMapping(action, str);
        menu.addAction(action);
      }

      if (can_conn_edit_players_nation(&client.conn, pplayer)) {
        str = QString(_("Pick nation"));
        action = new QAction(str, start_players);
        str = "PICK:" + QString(player_name(pplayer));  /* PICK is a key */
        connect(action, SIGNAL(triggered()), player_menu_mapper,
                SLOT(map()));
        player_menu_mapper->setMapping(action, str);
        menu.addAction(action);
      }

      if (pplayer->ai_controlled) {
        /**
         * Set AI difficulty submenu
         */
        if (ALLOW_CTRL <= client.conn.access_level
            && NULL != pplayer && pplayer->ai_controlled) {
          submenu_AI.setTitle(_("Set difficulty"));
          menu.addMenu(&submenu_AI);

          for (level = 0; level < AI_LEVEL_LAST; level++) {
            if (is_settable_ai_level(static_cast < ai_level > (level))) {
              level_name = ai_level_name(static_cast < ai_level > (level));
              level_cmd = ai_level_cmd(static_cast < ai_level > (level));
              action = new QAction(QString(level_name), start_players);
              str = "/" + QString(level_cmd) + " " + sp;
              connect(action, SIGNAL(triggered()), player_menu_mapper,
                      SLOT(map()));
              player_menu_mapper->setMapping(action, str);
              submenu_AI.addAction(action);
            }
          }
        }
      }

      /**
      * Put to Team X submenu
      */
      if (pplayer && game.info.is_new_game) {
        menu.addMenu(&submenu_team);
        submenu_team.setTitle(_("Put on team"));
        menu.addMenu(&submenu_team);
        count = pplayer->team ?
            player_list_size(team_members(pplayer->team)) : 0;
        need_empty_team = (count != 1);
        team_slots_iterate(tslot) {
          if (!team_slot_is_used(tslot)) {
            if (!need_empty_team) {
              continue;
            }
            need_empty_team = false;
          }
          str = team_slot_name_translation(tslot);
          action = new QAction(str, start_players);
          str = "/team" + sp + " \"" + QString(team_slot_rule_name(tslot))
              + "\"";
          connect(action, SIGNAL(triggered()),
                  player_menu_mapper, SLOT(map()));
          player_menu_mapper->setMapping(action, str);
          submenu_team.addAction(action);
        } team_slots_iterate_end;
      }

      if (ALLOW_CTRL <= client.conn.access_level && NULL != pplayer) {
        str = QString(_("Aitoggle player"));
        action = new QAction(str, start_players);
        str = "/aitoggle " + sp;
        connect(action, SIGNAL(triggered()), player_menu_mapper,
                SLOT(map()));
        player_menu_mapper->setMapping(action, str);
        menu.addAction(action);
      }
      connect(player_menu_mapper, SIGNAL(mapped(const QString &)),
              this, SLOT(send_command_to_server(const QString &)));
      menu.exec(global_pos);
      return;
    }
  } players_iterate_end;
  delete player_menu_mapper;
}

/***************************************************************************
 Calls dialg selecting nations
***************************************************************************/
void fc_client::slot_pick_nation()
{
  popup_races_dialog(client_player());
}

/***************************************************************************
  Sends commands to server, but first searches for cutom keys, if it finds
  then it makes custom action
***************************************************************************/
void fc_client::send_command_to_server(const QString &str)
{
  int index;
  QString splayer, s;

  /** Key == PICK: used for picking nation, it was put here cause those
   *  Qt slots are a bit limited ...I'm unable to pass custom player pointer
   *  or idk how to do that
   */
  s = str;
  index = str.indexOf("PICK:");

  if (index != -1) {
    s = s.remove("PICK:");
    /* now should be playername left in string */
    players_iterate(pplayer) {
      splayer = QString(pplayer->name);

      if (!splayer.compare(s)) {
        popup_races_dialog(pplayer);
      }
    } players_iterate_end;
    return;
  }

  /**
   * If client send commands to take ai, set /away to disable AI
   */

  index = str.indexOf("/take ");
  if (index != -1) {
      s = s.remove("/take ");
      players_iterate(pplayer) {
      splayer = QString(pplayer->name);
      splayer = "\"" + splayer + "\"";

      if (!splayer.compare(s)) {
        if (pplayer->ai_controlled) {
          send_chat(str.toLocal8Bit().data());
          send_chat("/away");
          return;
        }
      }
    } players_iterate_end;
  }

  send_chat(str.toLocal8Bit().data());
}
