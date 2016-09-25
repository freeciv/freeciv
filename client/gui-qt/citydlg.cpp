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
#include <QDesktopWidget>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>

// utility
#include "support.h"

// client
#include "citydlg_common.h"
#include "climisc.h"
#include "control.h"
#include "global_worklist.h"
#include "helpdata.h"
#include "mapview_common.h"
#include "movement.h"
#include "sprite.h"
#include "text.h"
#include "tilespec.h"

// common
#include "citizens.h"
#include "city.h"
#include "game.h"

//gui-qt
#include "citydlg.h"
#include "colors.h"

//agents
#include "cma_fec.h"

extern QApplication *qapp;
static bool city_dlg_created = false; /** defines if dialog for city has been
                                       * already created. It's created only
                                       * once per client
                                       */
static city_dialog *city_dlg;
extern QString split_text(QString text, bool cut);
extern QString cut_helptext(QString text);

/****************************************************************************
  Draws X on pixmap pointing its useless
****************************************************************************/
static void pixmap_put_x(QPixmap *pix)
{
  QPen pen(QColor(0, 0, 0));
  QPainter p;

  pen.setWidth(2);
  p.begin(pix);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(pen);
  p.drawLine(0, 0, pix->width(), pix->height());
  p.drawLine(pix->width(), 0, 0, pix->height());
  p.end();
}

/****************************************************************************
  Class representing one unit, allows context menu, holds pixmap for it
****************************************************************************/
unit_item::unit_item(QWidget *parent, struct unit *punit, 
                     bool supp, int hppy_cost) : QLabel()
{
  setParent(parent);
  happy_cost = hppy_cost;
  supported = supp;
  unit_pixmap = NULL;
  qunit = punit;

  if (punit) {
    if (supported) {
      unit_pixmap = qtg_canvas_create(tileset_full_tile_width(tileset),
                                      tileset_unit_with_upkeep_height(tileset));
    } else {
      unit_pixmap = qtg_canvas_create(tileset_full_tile_width(tileset),
                                      tileset_unit_height(tileset));
    }
    unit_pixmap->map_pixmap.fill(Qt::transparent);
    put_unit(punit, unit_pixmap, 0, 0);
    if (supported) {
      put_unit_city_overlays(punit, unit_pixmap, 0,
                             tileset_unit_layout_offset_y(tileset),
                             punit->upkeep, happy_cost);
    }
  }
  create_actions();
  setFixedWidth(unit_pixmap->map_pixmap.width() + 4);
  setFixedHeight(unit_pixmap->map_pixmap.height());
  setToolTip(unit_description(qunit));
}

/****************************************************************************
  Sets pixmap for unit_item class
****************************************************************************/
void unit_item::init_pix()
{
  setPixmap(unit_pixmap->map_pixmap);
  update();
}

/****************************************************************************
  Destructor for unit item
****************************************************************************/
unit_item::~unit_item()
{
  if (unit_pixmap) {
    qtg_canvas_free(unit_pixmap);
  }
}

/****************************************************************************
  Context menu handler
****************************************************************************/
void unit_item::contextMenuEvent(QContextMenuEvent *event)
{
  QMenu *menu = new QMenu(this);

  if (!can_client_issue_orders()) {
    return;
  }
  if (unit_owner(qunit) != client_player()) {
    return;
  }
  menu->addAction(activate);
  menu->addAction(activate_and_close);
  if (sentry) {
    menu->addAction(sentry);
  }
  if (fortify) {
    menu->addAction(fortify);
  }
  if (change_home) {
    menu->addAction(change_home);
  }
  if (load) {
    menu->addAction(load);
  }
  if (unload) {
    menu->addAction(unload);
  }
  if (unload_trans) {
    menu->addAction(unload_trans);
  }
  if (disband_action) {
    menu->addAction(disband_action);
  }
  if (upgrade) {
    menu->addAction(upgrade);
  }
  menu->popup(event->globalPos());
}

/****************************************************************************
  Initializes context menu
****************************************************************************/
void unit_item::create_actions()
{
  struct unit_list *qunits;

  qunits = unit_list_new();
  unit_list_append(qunits, qunit);
  activate = new QAction(_("Activate unit"), this);
  connect(activate, SIGNAL(triggered()), this, SLOT(activate_unit()));
  activate_and_close = new QAction(_("Activate and close dialog"), this);
  connect(activate_and_close, SIGNAL(triggered()), this,
          SLOT(activate_and_close_dialog()));

  if (can_unit_do_activity(qunit, ACTIVITY_SENTRY)) {
    sentry = new QAction(_("Sentry unit"), this);
    connect(sentry, SIGNAL(triggered()), this, SLOT(sentry_unit()));
  } else {
    sentry = NULL;
  }
  if (can_unit_do_activity(qunit, ACTIVITY_FORTIFYING)) {
    fortify = new QAction(_("Fortify unit"), this);
    connect(fortify, SIGNAL(triggered()), this, SLOT(fortify_unit()));
  } else {
    fortify = NULL;
  }
  if (!unit_has_type_flag(qunit, UTYF_UNDISBANDABLE)) {
    disband_action = new QAction(_("Disband unit"), this);
    connect(disband_action, SIGNAL(triggered()), this, SLOT(disband()));
  } else {
    disband_action = NULL;
  }
  if (can_unit_change_homecity(qunit)) {
    change_home = new QAction(_("Change homecity"), this);
    connect(change_home, SIGNAL(triggered()), this, SLOT(change_homecity()));
  } else {
    change_home = NULL;
  }
  if (units_can_load(qunits)) {
    load = new QAction(_("Load"), this);
    connect(load, SIGNAL(triggered()), this, SLOT(load_unit()));
  } else {
    load = NULL;
  }
  if (units_can_unload(qunits)) {
    unload = new QAction(_("Unload"), this);
    connect(unload, SIGNAL(triggered()), this, SLOT(unload_unit()));
  } else {
    unload = NULL;
  }
  if (units_are_occupied(qunits)) {
    unload_trans = new QAction(_("Unload All From Transporter"), this);
    connect(unload_trans, SIGNAL(triggered()), this, SLOT(unload_all()));
  } else {
    unload_trans = NULL;
  }
  if (units_can_upgrade(qunits)) {
    upgrade = new QAction(_("Upgrade Unit"), this);
    connect(upgrade, SIGNAL(triggered()), this, SLOT(upgrade_unit()));
  } else {
    upgrade = NULL;
  }
  unit_list_destroy(qunits);
}

/****************************************************************************
  Popups MessageBox  for disbanding unit and disbands it
****************************************************************************/
void unit_item::disband()
{
  struct unit_list *punits;
  struct unit *punit = player_unit_by_number(client_player(), qunit->id);

  if (punit == nullptr) {
    return;
  }

  punits = unit_list_new();
  unit_list_append(punits, punit);
  popup_disband_dialog(punits);
  unit_list_destroy(punits);
}

/****************************************************************************
  Loads unit into some tranport
****************************************************************************/
void unit_item::load_unit()
{
  request_unit_load(qunit, NULL);
}

/****************************************************************************
  Unloads unit
****************************************************************************/
void unit_item::unload_unit()
{
  request_unit_unload(qunit);
}

/****************************************************************************
  Unloads all untis from transporter
****************************************************************************/
void unit_item::unload_all()
{
  request_unit_unload_all(qunit);
}

/****************************************************************************
  Upgrades unit
****************************************************************************/
void unit_item::upgrade_unit()
{
  struct unit_list *qunits;
  qunits = unit_list_new();
  unit_list_append(qunits, qunit);
  popup_upgrade_dialog(qunits);
  unit_list_destroy(qunits);
}

/****************************************************************************
  Changes homecity for given unit
****************************************************************************/
void unit_item::change_homecity()
{
  if (qunit) {
    request_unit_change_homecity(qunit);
  }
}

/****************************************************************************
  Activates unit and closes city dialog
****************************************************************************/
void unit_item::activate_and_close_dialog()
{
  if (qunit) {
    unit_focus_set(qunit);
    qtg_popdown_all_city_dialogs();
  }
}

/****************************************************************************
  Activates unit in city dialog
****************************************************************************/
void unit_item::activate_unit()
{
  if (qunit) {
    unit_focus_set(qunit);
  }
}

/****************************************************************************
  Fortifies unit in city dialog
****************************************************************************/
void unit_item::fortify_unit()
{
  if (qunit) {
    request_unit_fortify(qunit);
  }
}

/****************************************************************************
  Mouse entered widget
****************************************************************************/
void unit_item::enterEvent(QEvent *event)
{
  if (unit_pixmap) {
    delete unit_pixmap;
  }
  if (qunit) {
    if (supported) {
      unit_pixmap = qtg_canvas_create(tileset_full_tile_width(tileset),
                                      tileset_unit_with_upkeep_height(tileset));
    } else {
      unit_pixmap = qtg_canvas_create(tileset_full_tile_width(tileset),
                                      tileset_unit_height(tileset));
    }
    unit_pixmap->map_pixmap.fill(QColor(200, 200, 200));
    put_unit(qunit, unit_pixmap, 0, 0);
    if (supported) {
      put_unit_city_overlays(qunit, unit_pixmap, 0,
                             tileset_unit_layout_offset_y(tileset),
                             qunit->upkeep, happy_cost);
    }
  }
  init_pix();
}

/****************************************************************************
  Mouse left widget
****************************************************************************/
void unit_item::leaveEvent(QEvent *event)
{
  if (unit_pixmap) {
    delete unit_pixmap;
  }
  if (qunit) {
    if (supported) {
      unit_pixmap = qtg_canvas_create(tileset_full_tile_width(tileset),
                                      tileset_unit_with_upkeep_height(tileset));
    } else {
      unit_pixmap = qtg_canvas_create(tileset_full_tile_width(tileset),
                                      tileset_unit_height(tileset));
    }
    unit_pixmap->map_pixmap.fill(Qt::transparent);
    put_unit(qunit, unit_pixmap, 0, 0);
    if (supported) {
      put_unit_city_overlays(qunit, unit_pixmap, 0,
                             tileset_unit_layout_offset_y(tileset),
                             qunit->upkeep, happy_cost);
    }
  }
  init_pix();
}

/****************************************************************************
  Mouse press event -activates unit and closes dialog
****************************************************************************/
void unit_item::mousePressEvent(QMouseEvent *event)
{
  if (event->button() == Qt::LeftButton) {
    if (qunit) {
      unit_focus_set(qunit);
      qtg_popdown_all_city_dialogs();
    }
  }
}

/****************************************************************************
  Sentries unit in city dialog
****************************************************************************/
void unit_item::sentry_unit()
{
  if (qunit) {
    request_unit_sentry(qunit);
  }
}

/****************************************************************************
  Class representing list of units ( unit_item 's)
****************************************************************************/
unit_info::unit_info(QWidget *parent, bool supp) : QWidget()
{
  setParent(parent);
  layout = new QHBoxLayout(this);
  init_layout();
  supports = supp;
}

/****************************************************************************
  Destructor for unit_info
****************************************************************************/
unit_info::~unit_info()
{
  qDeleteAll(unit_list);
  unit_list.clear();
}

/****************************************************************************
  Adds one unit to list
****************************************************************************/
void unit_info::add_item(unit_item *item)
{
  unit_list.append(item);
}

/****************************************************************************
  Initiazlizes layout ( layout needs to be changed after adding units )
****************************************************************************/
void unit_info::init_layout()
{
  QSizePolicy size_fixed_policy(QSizePolicy::Fixed, 
                                QSizePolicy::MinimumExpanding, 
                                QSizePolicy::Slider);
  layout->setContentsMargins(3, 6, 3, 6);
  setSizePolicy(size_fixed_policy);
  setLayout(layout);
}


/****************************************************************************
  Updates units
****************************************************************************/
void unit_info::update_units()
{
  int i = unit_list.count();
  int j;
  unit_item *ui;

  setUpdatesEnabled(false);
  hide();
  for (j = 0; j < i; j++) {
    ui = unit_list[j];
    layout->addWidget(ui, 0, Qt::AlignVCenter);
  }
  show();
  setUpdatesEnabled(true);
  layout->update();
  updateGeometry();
}

/****************************************************************************
  Cleans layout - run it before layout initialization
****************************************************************************/
void unit_info::clear_layout()
{
  int i = unit_list.count();
  unit_item *ui;
  int j;
  setUpdatesEnabled(false);
  setMouseTracking(false);
  for (j = 0; j < i; j++) {
    ui = unit_list[j];
    layout->removeWidget(ui);
    delete ui;
  }
  while (!unit_list.empty()) {
    unit_list.removeFirst();
  }
  setMouseTracking(true);
  setUpdatesEnabled(true);
}

/****************************************************************************
  city_label is used only for showing citizens icons
  and was created only to catch mouse events
****************************************************************************/
city_label::city_label(int t, QWidget *parent) : QLabel(parent)
{
  type = t;
}

/****************************************************************************
  Mouse handler for city_label
****************************************************************************/
void city_label::mousePressEvent(QMouseEvent *event)
{
  int citnum, i;
  int w = tileset_small_sprite_width(tileset);
  int num_citizens = pcity->size;

  if (cma_is_city_under_agent(pcity, NULL)) {
    return;
  }
  i = 1 + (num_citizens * 5 / 200);
  w = w / i;
  citnum = event->x() / w;
  if (!can_client_issue_orders()) {
    return;
  }
  city_rotate_specialist(pcity, citnum);
}

/****************************************************************************
  Just sets target city for city_label
****************************************************************************/
void city_label::set_city(city *pciti)
{
  pcity = pciti;
}

/****************************************************************************
  Used for showing tiles and workers view in city dialog
****************************************************************************/
city_map::city_map(QWidget *parent): QWidget(parent)
{
  setParent(parent);
  radius = 0;
  wdth = get_citydlg_canvas_width();
  hight = get_citydlg_canvas_height();
  cutted_width = wdth;
  cutted_height = hight;
  view = qtg_canvas_create(wdth, hight);
  view->map_pixmap.fill(Qt::black);
  miniview = qtg_canvas_create(0, 0);
  miniview->map_pixmap.fill(Qt::black);
  delta_x = 0;
  delta_y = 0;
  setMinimumSize(300, 300);
}

/****************************************************************************
  Destructor for city map
****************************************************************************/
city_map::~city_map()
{
  qtg_canvas_free(view);
  qtg_canvas_free(miniview);
}

/****************************************************************************
  Redraws whole view in city_map
****************************************************************************/
void city_map::paintEvent(QPaintEvent *event)
{
  QPainter painter;
  QString str;

  painter.begin(this);
  painter.drawPixmap(0, 0, cutted_width, cutted_height,
                     miniview->map_pixmap);
  if (cma_is_city_under_agent(mcity, NULL)) {
    QPixmap pix(cutted_width, cutted_height);
    pix.fill(QColor(90, 90, 90, 90));
    painter.drawPixmap(0, 0, cutted_width, cutted_height, pix);
    painter.setPen(QColor(120, 120, 120));
    /* TRANS: %1 is custom string choosen by player. */
    str = QString(_("Governor %1"))
          .arg(cmafec_get_short_descr_of_city(mcity));
    painter.drawText(5, cutted_height - 10, str);
  }
  painter.end();
}

/****************************************************************************
  Calls function to put pixmap on view ( it doesn't draw on screen )
****************************************************************************/
void city_map::set_pixmap(struct city *pcity)
{
  int r, max_r;

  r = sqrt(city_map_radius_sq_get(pcity));
  if (radius != r) {
    max_r = sqrt(rs_max_city_radius_sq());
    radius = r;
    delete miniview;
    cutted_width = wdth * (r + 1) / max_r;
    cutted_height = hight * (r + 1) / max_r;
    cutted_width = qMin(cutted_width, wdth);
    cutted_height = qMin(cutted_height, hight);
    delta_x = (wdth - cutted_width) / 2;
    delta_y = (hight - cutted_height) / 2;
    miniview = qtg_canvas_create(cutted_width, cutted_height);
    miniview->map_pixmap.fill(Qt::black);
    setMinimumSize(cutted_width, cutted_height);
    setMaximumSize(cutted_width, cutted_height);
    parentWidget()->updateGeometry();
  }
  city_dialog_redraw_map(pcity, view);
  qtg_canvas_copy(miniview, view, delta_x, delta_y,
                  0, 0, cutted_width, cutted_height);
  mcity = pcity;
}

/****************************************************************************
  Used to change workers on view
****************************************************************************/
void city_map::mousePressEvent(QMouseEvent *event)
{
  int canvas_x, canvas_y, city_x, city_y;

  if (!can_client_issue_orders()) {
    return;
  }

  canvas_x = event->x() + delta_x;
  canvas_y = event->y() + delta_y;
  if (canvas_to_city_pos(&city_x, &city_y, city_map_radius_sq_get(mcity),
                         canvas_x, canvas_y)) {
    city_toggle_worker(mcity, city_x, city_y);
  }
}

/****************************************************************************
  Constructor for city_dialog, sets layouts, policies ...
****************************************************************************/
city_dialog::city_dialog(QWidget *parent): QDialog(parent)
{
  QSizePolicy size_expanding_policy(QSizePolicy::Expanding,
                                    QSizePolicy::Expanding);
  QSizePolicy size_fixed_policy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  QStringList info_list;
  QFont *small_font;
  QFont f = QApplication::font();
  QFontMetrics fm(f);
  int info_nr;
  int iter;
  QHeaderView *header;
  QLabel *ql;
  QLabel *lab2;
  int h = 2 * fm.height() + 2;
  small_font = gui()->fc_fonts.get_font("gui_qt_font_city_label");
  QString city_stl = "QPushButton{font-weight: bold italic; border: "
                     "0px;text-align: right;}"
                     "QPushButton:hover "
                     "{background-color: rgb(225, 225, 225)}";
  QString cityb_style = "QPushButton{border: 0px;}"
                        "QPushButton:hover "
                        "{background-color: rgb(225, 225, 225)}";

  setParent(parent);
  setMouseTracking(true);
  setWindowFlags(Qt::Dialog);
  selected_row_p = -1;
  pcity = NULL;
  lcity_name = new QPushButton(this);
  lcity_name->setStyleSheet(city_stl);
  lcity_name->setToolTip(_("Click to change city name"));
  tab_widget = new QTabWidget(parent);
  tab_widget->setTabPosition(QTabWidget::South);
  tab_widget->setDocumentMode(false);
  overview_tab = new QWidget();
  tab_widget->addTab(overview_tab, _("Overview"));
  production_tab = new QWidget();
  tab_widget->addTab(production_tab, _("Production"));
  happiness_tab = new QWidget();
  tab_widget->addTab(happiness_tab, _("Happiness"));
  governor_tab = new QWidget();
  tab_widget->addTab(governor_tab, _("Governor"));

  production_grid_layout = new QGridLayout();
  overview_grid_layout = new QGridLayout();
  happiness_grid_layout = new QGridLayout();
  cma_grid_layout = new QGridLayout();
  main_grid_layout = new QGridLayout();
  main_grid_layout->addWidget(tab_widget, 0, 0, 6, 8);
  main_grid_layout->setContentsMargins(3, 3, 3, 3);

  size_expanding_policy.setHorizontalStretch(0);
  size_expanding_policy.setVerticalStretch(0);
  current_building = 0;

  /** Overview tab initialization */
  {
    QScrollArea *scroll;
    QScrollArea *scroll2;
    QGroupBox *map_box = new QGroupBox(this);
    QHBoxLayout *v_layout = new QHBoxLayout;
    QGroupBox *prod_box = new QGroupBox(this);
    QGridLayout *prod_layout = new QGridLayout;

    info_widget = new QWidget(overview_tab); /** City information widget
                                        * texts about surpluses and so on */
    info_grid_layout = new QGridLayout(parent);
    info_list << _("Food:") << _("Prod:") << _("Trade:") << _("Gold:")
              << _("Luxury:") << _("Science:") << _("Granary:")
              << _("Change in:") << _("Corruption:") << _("Waste:")
              << _("Pollution:") << _("Plague Risk:");
    info_nr = info_list.count();
    info_widget->setFont(*small_font);
    info_grid_layout->setSpacing(0);
    for (iter = 0; iter < info_nr; iter++) {
      ql = new QLabel(info_list[iter], info_widget);
      info_grid_layout->addWidget(ql, iter, 0);
      qlt[iter] = new QLabel(info_widget);
      info_grid_layout->addWidget(qlt[iter], iter, 1);
    }
    info_widget->setLayout(info_grid_layout);
    info_widget->setMinimumHeight(2 * fm.height() + 24
                                  + tileset_tile_height(tileset)
                                  + tileset_unit_with_upkeep_height(tileset));
    production_combo = new progress_bar(parent);
    production_combo->setToolTip(_("Click to change current production"));

    connect(production_combo, SIGNAL(clicked()), SLOT(show_targets()));
    buy_button = new QPushButton();
    buy_button->setIcon(fc_icons::instance()->get_icon("help-donate"));
    connect(lcity_name, SIGNAL(clicked()), SLOT(city_rename()));
    connect(buy_button, SIGNAL(clicked()), SLOT(buy()));
    item_button = new QPushButton();
    item_button->setStyleSheet(cityb_style);
    item_button->setToolTip(_("Click left button to change current "
                              "production"));
    connect(item_button, SIGNAL(clicked()), SLOT(show_targets()));
    production_table = new QTableWidget();
    production_table->setColumnCount(3);
    production_table->setProperty("showGrid", "false");
    production_table->setProperty("selectionBehavior", "SelectRows");
    production_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    production_table->verticalHeader()->setVisible(false);
    production_table->horizontalHeader()->setVisible(false);
    production_table->setSelectionMode(QAbstractItemView::SingleSelection);
    production_table->setAlternatingRowColors(true);
    production_table->setWordWrap(true);
    production_combo->setFixedHeight(h);
    production_combo->setMinimumWidth(200);
    connect(production_table, SIGNAL(itemDoubleClicked(QTableWidgetItem *)),
            SLOT(dbl_click(QTableWidgetItem *)));
    citizens_label = new city_label(FEELING_FINAL, this);
    citizen_pixmap = NULL;
    view = new city_map(this);
    v_layout->addWidget(view);
    map_box->setLayout(v_layout);
    map_box->setTitle(_("City map"));
    supp_units = new QLabel();
    supp_units->setFixedHeight(fm.height() + 4);
    curr_units = new QLabel();
    curr_units->setFixedHeight(fm.height() + 4);
    supported_units = new unit_info(this, true);
    scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setMinimumHeight(tileset_unit_with_upkeep_height(tileset) + 6);
    scroll->setMaximumHeight(tileset_unit_with_upkeep_height(tileset) + 6
                              + scroll->horizontalScrollBar()->height());
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(supported_units);
    current_units = new unit_info(this, false);
    scroll2 = new QScrollArea;
    scroll2->setWidgetResizable(true);
    scroll2->setMinimumHeight(tileset_unit_height(tileset) + 6);
    scroll2->setMaximumHeight(tileset_unit_height(tileset) + 6
                              + scroll2->horizontalScrollBar()->height());
    scroll2->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll2->setWidget(current_units);
    prod_layout->addWidget(item_button, 0, 1, 1, 1);
    prod_layout->addWidget(production_combo, 0, 0, 1, 1);
    prod_layout->addWidget(buy_button, 1, 0, 1, 2);
    prod_layout->addWidget(production_table, 2, 0, 3, 2);
    prod_box->setLayout(prod_layout);

    /*    0     1     2     3     4       5       6
    0  [.CITIZENS LABEL                   ][CITY NAME]
    1  [......V..I...E...W........... ][SELECT][ICON]
    2  [............................. ][.BUY.BUTTON..]
    3  [......V..I...E..W............ ][..PROD.......]
    4  [............................. ][.............]
    5  [.....V...I...E..W............][...TABLE.....]
    6  [.INFO] [.LABEL1.....................]
    7  [.INFO] [.SCROLL.1..........................]
    8  [.INFO] [LABEL2......................]
    9  [.INFO] [..SCROLL.2.........................]
    */
    overview_grid_layout->addWidget(prod_box, 1, 5, 4, 2);
    overview_grid_layout->addWidget(info_widget, 6, 0, 4, 1);
    overview_grid_layout->addWidget(citizens_label, 0, 0, 1, 6);
    overview_grid_layout->addWidget(map_box, 1, 0, 4, 5);
    overview_grid_layout->addWidget(lcity_name, 0, 6, 1, 1);
    overview_grid_layout->addWidget(curr_units, 8, 1, 1, 5);
    overview_grid_layout->addWidget(scroll2, 9, 1, 1, 6);
    overview_grid_layout->addWidget(supp_units, 6, 1, 1, 5);
    overview_grid_layout->addWidget(scroll, 7, 1, 1, 6);
    overview_grid_layout->setColumnStretch(4, 10);
    overview_grid_layout->setColumnStretch(5, 10);
    overview_grid_layout->setColumnStretch(6, 10);
  }
  /** Production tab initialization */
  {
    QLabel *label_p = new QLabel;
    QLabel *label_up = new QLabel;
    QLabel *ql = new QLabel(_("Disband if build settler at size 1"));
    QVBoxLayout *vbox = new QVBoxLayout;
    QVBoxLayout *vbox_layout = new QVBoxLayout;
    QGroupBox *qgb = new QGroupBox;
    QGroupBox *qgbprod = new QGroupBox;
    QGroupBox *group_box = new QGroupBox(_("Worklist Option"));
    QGridLayout *qgrid = new QGridLayout;
    QPushButton *but2 = new QPushButton;
    QHBoxLayout *work_but_layout = new QHBoxLayout;
    work_next_but = new QPushButton(fc_icons::instance()->get_icon(
                                    "go-down"), "");
    work_prev_but = new QPushButton(fc_icons::instance()->get_icon(
                                    "go-up"), "");
    work_add_but = new QPushButton(fc_icons::instance()->get_icon(
                                    "list-add"), "");
    work_rem_but = new QPushButton(style()->standardIcon(
                                    QStyle::SP_DialogDiscardButton), "");
    work_but_layout->addWidget(work_add_but);
    work_but_layout->addWidget(work_next_but);
    work_but_layout->addWidget(work_prev_but);
    work_but_layout->addWidget(work_rem_but);
    but_menu_worklist = new QPushButton;
    item_button_p = new QPushButton;

    item_button_p->setToolTip(_("Click left button to change "
                                "current production"));
    item_button_p->setStyleSheet(cityb_style);
    label_p->setAlignment(Qt::AlignLeft);
    label_p->setText(_("Show future targets"));
    label_up->setAlignment(Qt::AlignLeft);
    label_up->setText(_("Show units"));
    pcity_name = new QPushButton(this);
    pcity_name->setToolTip(_("Click to change city name"));
    pcity_name->setStyleSheet(city_stl);
    production_combo_p = new progress_bar(parent);
    production_combo_p->setToolTip(_("Click to change worklist production"));
    p_table_p = new QTableWidget;
    buy_button_p = new QPushButton(parent);
    buy_button_p->setIcon(fc_icons::instance()->get_icon("help-donate"));
    future_targets_p = new QCheckBox;
    future_targets_p->setChecked(false);
    show_units_p = new QCheckBox;
    show_units_p->setChecked(true);

    r1 = new QRadioButton(_("Change"));
    r2 = new QRadioButton(_("Insert Before"));
    r3 = new QRadioButton(_("Insert After"));
    r4 = new QRadioButton(_("Add Last"));
    r4->setChecked(true);
    vbox->addLayout(work_but_layout);
    vbox->addWidget(r1);
    vbox->addWidget(r2);
    vbox->addWidget(r3);
    vbox->addWidget(r4);
    group_box->setLayout(vbox);

    p_table_p->setColumnCount(3);
    p_table_p->setProperty("showGrid", "false");
    p_table_p->setProperty("selectionBehavior", "SelectRows");
    p_table_p->setEditTriggers(QAbstractItemView::NoEditTriggers);
    p_table_p->verticalHeader()->setVisible(false);
    p_table_p->horizontalHeader()->setVisible(false);
    p_table_p->setSelectionMode(QAbstractItemView::SingleSelection);
    production_combo_p->setFixedHeight(h);
    p_table_p->setMinimumWidth(200);
    p_table_p->setSizePolicy(size_expanding_policy);
    p_table_p->setContextMenuPolicy(Qt::CustomContextMenu);
    header = p_table_p->horizontalHeader();
    header->setStretchLastSection(true);

    qgbprod->setTitle(_("Worklist"));
    vbox_layout->addWidget(production_combo_p);
    vbox_layout->addWidget(p_table_p);
    qgbprod->setLayout(vbox_layout);

    disband_at_one = new QCheckBox;
    connect(disband_at_one, SIGNAL(stateChanged(int)),
            SLOT(disband_state_changed(int)));
    qgrid->addWidget(future_targets_p, 0, 0, 1, 1);
    qgrid->addWidget(label_p, 0, 1, 1, 1);
    qgrid->addWidget(show_units_p, 1, 0, 1, 1);
    qgrid->addWidget(disband_at_one, 2, 0, 1, 1);
    qgrid->addWidget(ql, 2, 1, 1, 1);
    qgrid->addWidget(label_up, 1, 1, 1, 1);
    qgrid->setColumnStretch(1, 10);
    qgb->setLayout(qgrid);
    but_menu_worklist->setText(_("Worklist menu"));
    but_menu_worklist->setIcon(style()->standardIcon(
                                 QStyle::SP_FileLinkIcon));
    but2->setText(_("Save worklist"));
    but2->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));

    /*
           0   1    2    3     4       5       6
      0
      1
      2   [prod.combo]      [..group.butts.][c.name]
      3   [..........]      [...group.box..]
      4   [.p_table_p]
      5   [..........]        [icon.][buy]
      6                       [..menu....][create]
     */

    production_grid_layout->addWidget(qgb, 4, 4, 1, 3);
    production_grid_layout->addWidget(group_box, 2, 4, 2, 2);
    production_grid_layout->addWidget(buy_button_p, 5, 5, 1, 1);
    production_grid_layout->addWidget(qgbprod, 2, 0, 5, 1);
    production_grid_layout->addWidget(item_button_p, 5, 4, 1, 1);
    production_grid_layout->addWidget(but_menu_worklist, 6, 4, 1, 2);
    production_grid_layout->addWidget(but2, 6, 6, 1, 1);
    production_grid_layout->addWidget(pcity_name, 2, 6, 1, 1);

    connect(p_table_p,
            SIGNAL(customContextMenuRequested(const QPoint &)), this,
            SLOT(display_worklist_menu(const QPoint &)));
    connect(but_menu_worklist, SIGNAL(clicked()), SLOT(delete_prod()));
    connect(but2, SIGNAL(clicked()), SLOT(save_worklist()));
    connect(buy_button_p, SIGNAL(clicked()), SLOT(buy()));
    connect(production_combo_p, SIGNAL(clicked()),
            SLOT(show_targets_worklist()));
    connect(work_add_but, SIGNAL(clicked()), SLOT(show_targets_worklist()));
    connect(work_prev_but, SIGNAL(clicked()), SLOT(worklist_up()));
    connect(work_next_but, SIGNAL(clicked()), SLOT(worklist_down()));
    connect(work_rem_but, SIGNAL(clicked()), SLOT(worklist_del()));
    connect(item_button_p, SIGNAL(clicked()),
            SLOT(show_targets()));
    connect(p_table_p,
            SIGNAL(itemDoubleClicked(QTableWidgetItem *)),
            SLOT(dbl_click_p(QTableWidgetItem *)));
    connect(p_table_p->selectionModel(),
            SIGNAL(selectionChanged(const QItemSelection &,
                                    const QItemSelection &)),
            SLOT(item_selected(const QItemSelection &,
                               const QItemSelection &)));
    connect(pcity_name, SIGNAL(clicked()), SLOT(city_rename()));
  } /** Production tab initialization end */


  {/** Happiness tab initialization */

    QGroupBox *qgbox = new QGroupBox(_("Happiness"));
    QGridLayout *gridl = new QGridLayout;

    nationality_table = new QTableWidget;
    nationality_table->setColumnCount(3);
    nationality_table->setProperty("showGrid", "false");
    nationality_table->setProperty("selectionBehavior", "SelectRows");
    nationality_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    nationality_table->verticalHeader()->setVisible(false);
    nationality_table->horizontalHeader()->setStretchLastSection(true);

    info_list.clear();
    info_list << _("Cities:") << _("Luxuries:") << _("Buildings:")
              << _("Nationality:") << _("Units:") <<  _("Wonders:");
    info_nr = info_list.count();
    for (int i = 0; i < info_list.count(); i++) {
      lab_table[i] = new city_label(1 + i, this);
      gridl->addWidget(lab_table[i], i, 1, 1, 1);
      lab2 = new QLabel(this);
      lab2->setFont(*small_font);
      lab2->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
      lab2->setText(info_list.at(i));
      gridl->addWidget(lab2, i, 0, 1, 1);
    }
    gridl->setSpacing(0);
    qgbox->setLayout(gridl);
    info_view = new city_map(this);
    happiness_grid_layout->addWidget(info_view, 0, 0, 1, 1);
    happiness_grid_layout->addWidget(qgbox, 1, 0, 1 , 1);
    happiness_grid_layout->addWidget(nationality_table, 0, 1, 1, 1);
    happiness_grid_layout->setRowStretch(1, 10);
    happiness_grid_layout->setColumnStretch(1, 10);
  } /** Happiness tab initialization end */

  { /** CMA tab initialization */
    QGroupBox *qgbox = new QGroupBox(_("Presets:"));
    QGroupBox *qsliderbox = new QGroupBox(_("Governor settings"));
    QGroupBox *result_box = new QGroupBox(_("Results:"));
    QHBoxLayout *hbox = new QHBoxLayout;
    QGridLayout *gridl = new QGridLayout;
    QGridLayout *slider_grid = new QGridLayout;
    QStringList str_list;
    QLabel *some_label;
    QSlider *slider;
    QPushButton *qpush1
      = new QPushButton(style()->standardIcon(QStyle::SP_DialogDiscardButton),
                        _("Delete"));
    QPushButton *qpush2
      = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton),
                        _("Save"));

    connect(qpush1, SIGNAL(pressed()), SLOT(cma_remove()));
    connect(qpush2, SIGNAL(pressed()), SLOT(save_cma()));

    cma_info_text = new QLabel;
    cma_info_text->setFont(*small_font);
    cma_info_text->setAlignment(Qt::AlignCenter);
    cma_table = new QTableWidget;
    cma_table->setColumnCount(1);
    cma_table->setProperty("showGrid", "false");
    cma_table->setProperty("selectionBehavior", "SelectRows");
    cma_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    cma_table->setSelectionMode(QAbstractItemView::SingleSelection);
    cma_table->verticalHeader()->setVisible(false);
    cma_table->horizontalHeader()->setVisible(false);
    cma_table->horizontalHeader()->setSectionResizeMode(
                                                      QHeaderView::Stretch);

    connect(cma_table->selectionModel(),
            SIGNAL(selectionChanged(const QItemSelection &,
                                    const QItemSelection &)),
            SLOT(cma_selected(const QItemSelection &,
                              const QItemSelection &)));
    connect(cma_table, SIGNAL(cellDoubleClicked(int, int)), this,
            SLOT(cma_double_clicked(int, int)));
    gridl->addWidget(cma_table, 0, 0, 1, 2);
    gridl->addWidget(qpush1, 1, 0, 1, 2);
    qgbox->setLayout(gridl);
    hbox->addWidget(cma_info_text);
    result_box->setLayout(hbox);
    str_list << _("Food") << _("Shield") << _("Trade") << _("Gold")
             << _("Luxury") << _("Science") << _("Celebrate");
    some_label = new QLabel(_("Minimal Surplus"));
    some_label->setAlignment(Qt::AlignCenter);
    slider_grid->addWidget(some_label, 0, 1, 1, 2);
    some_label = new QLabel(_("Priority"));
    some_label->setAlignment(Qt::AlignCenter);
    slider_grid->addWidget(some_label, 0, 3, 1, 2);
    for (int i = 0; i < str_list.count(); i++) {
      some_label = new QLabel(str_list.at(i));
      slider_grid->addWidget(some_label, i + 1, 0, 1, 1);
      some_label = new QLabel("0");
      some_label->setMinimumWidth(25);
      if (i != str_list.count() - 1) {
        slider = new QSlider(Qt::Horizontal);
        slider->setFocusPolicy(Qt::TabFocus);
        slider_tab[2 * i] = slider;
        slider->setRange(-20, 20);
        slider->setSingleStep(1);
        slider_grid->addWidget(some_label, i + 1, 1, 1, 1);
        slider_grid->addWidget(slider, i + 1, 2, 1, 1);
        slider->setProperty("FC", QVariant::fromValue((void *)some_label));
        connect(slider, SIGNAL(valueChanged(int)), SLOT(cma_slider(int)));
      } else {
        cma_celeb_checkbox = new QCheckBox;
        slider_grid->addWidget(cma_celeb_checkbox, i + 1, 2 , 1 , 1);
        connect(cma_celeb_checkbox,
                SIGNAL(stateChanged(int)), SLOT(cma_celebrate_changed(int)));
      }
      some_label = new QLabel("0");
      some_label->setMinimumWidth(25);
      slider = new QSlider(Qt::Horizontal);
      slider->setFocusPolicy(Qt::TabFocus);
      slider->setRange(0, 25);
      slider_tab[2 * i + 1] = slider;
      slider->setProperty("FC", QVariant::fromValue((void *)some_label));
      slider_grid->addWidget(some_label, i + 1, 3, 1, 1);
      slider_grid->addWidget(slider, i + 1, 4, 1, 1);
      connect(slider, SIGNAL(valueChanged(int)), SLOT(cma_slider(int)));
    }

    cma_enable_but = new QPushButton();
    cma_enable_but->setFocusPolicy(Qt::TabFocus);
    connect(cma_enable_but, SIGNAL(pressed()), SLOT(cma_enable()));
    slider_grid->addWidget(cma_enable_but, O_LAST + 4, 1, 1, 2);
    slider_grid->addWidget(qpush2, O_LAST + 4, 3, 1, 2);

    qsliderbox->setLayout(slider_grid);
    cma_result = new QLabel;
    cma_result_pix = new QLabel;
    cma_grid_layout->addWidget(qgbox, 0, 0, 4 , 1);
    cma_grid_layout->addWidget(result_box, 0, 1, 1, 3);
    cma_grid_layout->addWidget(cma_result, 1, 2, 1, 1);
    cma_grid_layout->addWidget(cma_result_pix, 1, 1, 1, 1);
    cma_grid_layout->addWidget(qsliderbox, 2, 1, 2, 3);
  } /** CMA tab initialization end */

  overview_tab->setLayout(overview_grid_layout);
  production_tab->setLayout(production_grid_layout);
  happiness_tab->setLayout(happiness_grid_layout);
  governor_tab->setLayout(cma_grid_layout);

  button = new QPushButton();
  button->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));
  connect(button, SIGNAL(clicked()), SLOT(hide()));
  button->setText(_("Close"));
  main_grid_layout->addWidget(button, 6, 6, 1, 2);

  next_city_but = new QPushButton();
  next_city_but->setIcon(style()->standardIcon(QStyle::SP_ArrowRight));
  connect(next_city_but, SIGNAL(clicked()), SLOT(next_city()));
  next_city_but->setText(_("Next City"));
  main_grid_layout->addWidget(next_city_but, 6, 4, 1, 2);

  prev_city_but = new QPushButton();
  connect(prev_city_but, SIGNAL(clicked()), SLOT(prev_city()));
  prev_city_but->setText(_("Previous City"));
  prev_city_but->setIcon(style()->standardIcon(QStyle::SP_ArrowLeft));
  main_grid_layout->addWidget(prev_city_but, 6, 2, 1, 2);

  setLayout(main_grid_layout);
  ::city_dlg_created = true;
}

/****************************************************************************
  Updates buttons/widgets which should be enabled/disabled
****************************************************************************/
void city_dialog::update_disabled()
{
  if (NULL == client.conn.playing
      || city_owner(pcity) != client.conn.playing) {
    prev_city_but->setDisabled(true);
    next_city_but->setDisabled(true);
    buy_button->setDisabled(true);
    buy_button_p->setDisabled(true);
    item_button->setDisabled(true);
    item_button_p->setDisabled(true);
    cma_enable_but->setDisabled(true);
    production_combo->setDisabled(true);
    production_combo_p->setDisabled(true);
    but_menu_worklist->setDisabled(true);
    current_units->setDisabled(true);
    supported_units->setDisabled(true);

    view->setDisabled(true);
    info_view->setDisabled(true);
    if (!client_is_observer()) {
      tab_widget->setTabEnabled(1, false);
      tab_widget->setTabEnabled(2, false);
      tab_widget->setTabEnabled(3, false);
    }
  } else {
    prev_city_but->setEnabled(true);
    next_city_but->setEnabled(true);
    buy_button->setEnabled(true);
    buy_button_p->setEnabled(true);
    item_button->setEnabled(true);
    item_button_p->setEnabled(true);
    cma_enable_but->setEnabled(true);
    production_combo->setEnabled(true);
    production_combo_p->setEnabled(true);
    but_menu_worklist->setEnabled(true);
    current_units->setEnabled(true);
    supported_units->setEnabled(true);
    view->setEnabled(true);
    info_view->setEnabled(true);
    tab_widget->setTabEnabled(1, true);
    tab_widget->setTabEnabled(2, true);
    tab_widget->setTabEnabled(3, true);
  }
  if (can_client_issue_orders()) {
    cma_enable_but->setEnabled(true);
  } else  {
    cma_enable_but->setDisabled(true);
  }
  update_prod_buttons();
}

/****************************************************************************
  Update sensitivity of buttons in production tab
****************************************************************************/
void city_dialog::update_prod_buttons()
{
  work_next_but->setDisabled(true);
  work_prev_but->setDisabled(true);
  work_add_but->setDisabled(true);
  work_rem_but->setDisabled(true);
  if (client.conn.playing && city_owner(pcity) == client.conn.playing) {
    work_add_but->setEnabled(true);
    if (selected_row_p >= 0 && selected_row_p < p_table_p->rowCount()) {
      work_rem_but->setEnabled(true);
    }
    if (selected_row_p >= 0 && selected_row_p < p_table_p->rowCount() - 1) {
      work_next_but->setEnabled(true);
    }
    if (selected_row_p > 0 && selected_row_p < p_table_p->rowCount()) {
      work_prev_but->setEnabled(true);
    }
  }
}

/****************************************************************************
  City dialog destructor
****************************************************************************/
city_dialog::~city_dialog()
{
  if (citizen_pixmap) {
    delete citizen_pixmap;
  }
  cma_table->clear();
  production_table->clear();
  p_table_p->clear();
  nationality_table->clear();
  current_units->clear_layout();
  supported_units->clear_layout();
  ::city_dlg_created = false;
}

/****************************************************************************
  City rename dialog input
****************************************************************************/
void city_dialog::city_rename()
{
  bool ok;
  QString text = QInputDialog::getText(this,
                                     _("Rename City"),
                                     _("What should we rename the city to?"),
                                     QLineEdit::Normal,
                                     city_name(pcity), &ok);

  if (ok && !text.isEmpty()) {
    ::city_rename(pcity, text.toLocal8Bit().data());
  }
}

/****************************************************************************
  Save cma dialog input
****************************************************************************/
void city_dialog::save_cma()
{
  struct cm_parameter param;
  bool ok;
  QString text = QInputDialog::getText(this,
                                       _("Name new preset"),
                                       _("What should we name the preset?"),
                                       QLineEdit::Normal,
                                       _("new preset"), &ok);

  if (ok && !text.isEmpty()) {
    param.allow_disorder = false;
    param.allow_specialists = true;
    param.require_happy = cma_celeb_checkbox->isChecked();
    param.happy_factor = slider_tab[2 * O_LAST + 1]->value();
    for (int i = O_FOOD; i < O_LAST; i++) {
      param.minimal_surplus[i] = slider_tab[2 * i]->value();
      param.factor[i] = slider_tab[2 * i + 1]->value();
    }
    cmafec_preset_add(text.toLocal8Bit().data(), &param);
    update_cma_tab();
  }
}

/****************************************************************************
  Enables cma slot, triggered by clicked button or changed cma
****************************************************************************/
void city_dialog::cma_enable()
{
  if (cma_is_city_under_agent(pcity, NULL)) {
    cma_release_city(pcity);
    return;
  }
  cma_changed();
  update_cma_tab();
}

/****************************************************************************
  Sliders moved and cma has been changed
****************************************************************************/
void city_dialog::cma_changed()
{
  struct cm_parameter param;

  param.allow_disorder = false;
  param.allow_specialists = true;
  param.require_happy = cma_celeb_checkbox->isChecked();
  param.happy_factor = slider_tab[2 * O_LAST + 1]->value();
  for (int i = O_FOOD; i < O_LAST; i++) {
    param.minimal_surplus[i] = slider_tab[2 * i]->value();
    param.factor[i] = slider_tab[2 * i + 1]->value();
  }
  cma_put_city_under_agent(pcity, &param);
}

/****************************************************************************
  Double click on some row ( column is unused )
****************************************************************************/
void city_dialog::cma_double_clicked(int row, int column)
{
  const struct cm_parameter *param;

  param = cmafec_preset_get_parameter(row);

  if (cma_is_city_under_agent(pcity, NULL)) {
    cma_release_city(pcity);
  }
  cma_put_city_under_agent(pcity, param);
}

/****************************************************************************
  CMA has been selected from list
****************************************************************************/
void city_dialog::cma_selected(const QItemSelection &sl,
                               const QItemSelection &ds)
{
  const struct cm_parameter *param;
  QModelIndex index;
  QModelIndexList indexes = sl.indexes();

  if (indexes.isEmpty() || cma_table->signalsBlocked()) {
    return;
  }
  index = indexes.at(0);
  int ind = index.row();

  if (cma_table->currentRow() == -1 || cmafec_preset_num() == 0) {
    return;
  }
  param = cmafec_preset_get_parameter(ind);
  update_sliders();
  if (cma_is_city_under_agent(pcity, NULL)) {
    cma_release_city(pcity);
    cma_put_city_under_agent(pcity, param);
  }
}

/****************************************************************************
  Updates sliders ( cma params )
****************************************************************************/
void city_dialog::update_sliders()
{
  const struct cm_parameter *param;
  int output;
  QVariant qvar;
  QLabel *label;

  if (cma_table->currentRow() == -1 || cmafec_preset_num() == 0) {
    return;
  }
  param = cmafec_preset_get_parameter(cma_table->currentRow());

  for (output = O_FOOD; output < 2 * O_LAST; output++) {
    slider_tab[output]->blockSignals(true);
  }
  for (output = O_FOOD; output < O_LAST; output++) {
    qvar = slider_tab[2 * output + 1]->property("FC");
    label = reinterpret_cast<QLabel *>(qvar.value<void *>());
    label->setText(QString::number(param->factor[output]));
    slider_tab[2 * output + 1]->setValue(param->factor[output]);
    qvar = slider_tab[2 * output]->property("FC");
    label = reinterpret_cast<QLabel *>(qvar.value<void *>());
    label->setText(QString::number(param->minimal_surplus[output]));
    slider_tab[2 * output]->setValue(param->minimal_surplus[output]);
  }
  slider_tab[2 * O_LAST + 1]->blockSignals(true);
  qvar = slider_tab[2 * O_LAST + 1]->property("FC");
  label = reinterpret_cast<QLabel *>(qvar.value<void *>());
  label->setText(QString::number(param->happy_factor));
  slider_tab[2 * O_LAST + 1]->setValue(param->happy_factor);
  slider_tab[2 * O_LAST + 1]->blockSignals(false);
  cma_celeb_checkbox->blockSignals(true);
  cma_celeb_checkbox->setChecked(param->require_happy);
  cma_celeb_checkbox->blockSignals(false);
  for (output = O_FOOD; output < 2 * O_LAST; output++) {
    slider_tab[output]->blockSignals(false);
  }
}

/****************************************************************************
  Updates cma tab
****************************************************************************/
void city_dialog::update_cma_tab()
{
  QString s;
  QTableWidgetItem *item;
  struct cm_parameter param;
  QPixmap pix;
  int i;

  cma_table->clear();
  cma_table->setRowCount(0);
  for (int i = 0; i < cmafec_preset_num(); i++) {
    item = new QTableWidgetItem;
    item->setText(cmafec_preset_get_descr(i));
    cma_table->insertRow(i);
    cma_table->setItem(i, 0, item);
  }
  if (cmafec_preset_num() == 0) {
    cma_table->insertRow(0);
    item = new QTableWidgetItem;
    item->setText(_("No governor defined"));
    cma_table->setItem(0, 0, item);
  }
  if (cma_is_city_under_agent(pcity, NULL)) {
    s = QString(cmafec_get_short_descr_of_city(pcity));
    pix = style()->standardPixmap(QStyle::SP_DialogApplyButton);
    pix = pix.scaled(2 * pix.width(), 2 * pix.height(),
                     Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    cma_result_pix->setPixmap(pix);
    cma_result_pix->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    /* TRANS: %1 is custom string chosen player */
    cma_result->setText(QString(_("<h3>Governor Enabled - %1</h3>")).arg(s));
    cma_result->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  } else {
    pix = style()->standardPixmap(QStyle::SP_DialogCancelButton);
    pix = pix.scaled(1.6 * pix.width(), 1.6 * pix.height(),
                     Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    cma_result_pix->setPixmap(pix);
    cma_result_pix->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    cma_result->setText(QString(_("<h3>Governor Disabled</h3>")));
    cma_result->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  }
  if (cma_is_city_under_agent(pcity, NULL)) {
    cmafec_get_fe_parameter(pcity, &param);
    i = cmafec_preset_get_index_of_parameter(const_cast < const struct
                                             cm_parameter * const >(&param));
    if (i >= 0 && i < cma_table->rowCount()) {
      cma_table->blockSignals(true);
      cma_table->setCurrentCell(i, 0);
      cma_table->blockSignals(false);
    }
    cma_enable_but->setText(_("Disable"));
  } else {
    cma_enable_but->setText(_("Enable"));
  }
  update_sliders();
  update_results_text();
}

/****************************************************************************
  Removes selected CMA
****************************************************************************/
void city_dialog::cma_remove()
{
  int i;
  QMessageBox ask(gui()->central_wdg);
  int ret;

  i = cma_table->currentRow();
  if (i == -1 || cmafec_preset_num() == 0) {
    return;
  }
  ask.setText(_("Remove this preset?"));
  ask.setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
  ask.setDefaultButton(QMessageBox::Cancel);
  ask.setWindowTitle(cmafec_preset_get_descr(i));
  ret = ask.exec();

  switch (ret) {
  case QMessageBox::Cancel:
    return;
  case QMessageBox::Ok:
    cmafec_preset_remove(i);
    update_cma_tab();
    break;
  }
}

/****************************************************************************
  CMA option 'celebrate' qcheckbox state has been changed
****************************************************************************/
void city_dialog::cma_celebrate_changed(int val)
{
  if (cma_is_city_under_agent(pcity, NULL)) {
    cma_changed();
    update_cma_tab();
  }
}

/****************************************************************************
  CMA options on slider has been changed
****************************************************************************/
void city_dialog::cma_slider(int value)
{
  QVariant qvar;
  QSlider *slider;
  QLabel *label;
  QString s;

  slider = qobject_cast<QSlider *>(sender());
  qvar = slider->property("FC");
  if (qvar.isNull() || !qvar.isValid()) {
    return;
  }
  label = reinterpret_cast<QLabel *>(qvar.value<void *>());
  label->setText(QString::number(value));
  if (cma_is_city_under_agent(pcity, NULL)) {
    cma_changed();
    update_cma_tab();
  }
}

/****************************************************************************
  Updates text result in cma tab
****************************************************************************/
void city_dialog::update_results_text()
{

  QString str;
  struct cm_parameter param;
  struct cm_result *result = cm_result_new(pcity);

  cmafec_get_fe_parameter(pcity, &param);
  cm_result_from_main_map(result, pcity);
  str = cmafec_get_result_descr(pcity, result, &param);
  cma_info_text->setText(str);

  if (!cma_is_city_under_agent(pcity, NULL)) {
    cma_info_text->setDisabled(true);
  } else {
    cma_info_text->setDisabled(false);
  }
  cm_result_destroy(result);
}

/****************************************************************************
  Received signal about changed qcheckbox - disbnad at size 1
****************************************************************************/
void city_dialog::disband_state_changed(int state)
{
  bv_city_options new_options;

  BV_CLR_ALL(new_options);
  if (state == Qt::Checked) {
    BV_SET(new_options, CITYO_DISBAND);
  } else if (state == Qt::Unchecked) {
    BV_CLR(new_options, CITYO_DISBAND);
  }
  if (!client_is_observer()) {
    dsend_packet_city_options_req(&client.conn, pcity->id, new_options);
  }
}

/****************************************************************************
  Context menu on production tab in city worklist
****************************************************************************/
void city_dialog::display_worklist_menu(const QPoint &p)
{
  bool worklist_defined = true;
  int which_menu;
  cid c_id;
  QMenu *insert_menu;
  QMenu *add_menu;
  QMenu list_menu(this);
  QMap<QString, cid> list;
  QAction wl_clear(_("Clear"), 0);
  QAction *action;
  QAction *act;
  QAction wl_empty(_("(no worklists defined)"), 0);
  QVariant qvar;
  QMap<QString, cid>::const_iterator map_iter;

  if (!can_client_issue_orders()) {
    return;
  }
  add_menu = list_menu.addMenu(_("Change worklist"));
  insert_menu = list_menu.addMenu(_("Insert worklist"));
  connect(&wl_clear, SIGNAL(triggered()), SLOT(clear_worklist()));
  list_menu.addAction(&wl_clear);
  list.clear();

  global_worklists_iterate(pgwl) {
    list.insert(global_worklist_name(pgwl), global_worklist_id(pgwl));
  } global_worklists_iterate_end;

  if (list.count() == 0) {
    add_menu->addAction(&wl_empty);
    insert_menu->addAction(&wl_empty);
    worklist_defined = false;
  }

  map_iter = list.constBegin();
  while (map_iter != list.constEnd()) {
    action = add_menu->addAction(map_iter.key());
    action->setData(map_iter.value());
    action->setProperty("FC", 1);
    action = insert_menu->addAction(map_iter.key());
    action->setData(map_iter.value());
    action->setProperty("FC", 2);
    map_iter++;
  }

  act = 0;
  act = list_menu.exec(QCursor::pos());
  if (act) {
    qvar = act->property("FC");
    if (!qvar.isValid() || qvar.isNull() || !worklist_defined) {
      return;
    }
    which_menu = qvar.toInt();
    qvar = act->data();
    c_id = qvar.toInt();
    if (which_menu == 1) { /* Change Worklist */
      city_set_queue(pcity,
                     global_worklist_get(global_worklist_by_id(c_id)));
    } else if (which_menu == 2) { /* Insert Worklist */
      if (worklist_defined) {
        city_queue_insert_worklist(pcity, selected_row_p,
                                   global_worklist_get(global_worklist_by_id(
                                                         c_id)));
      }
    }
  }
}


/****************************************************************************
  Enables/disables buy buttons depending on gold
****************************************************************************/
void city_dialog::update_buy_button()
{
  QString str;
  int value;

  buy_button->setDisabled(true);
  buy_button_p->setDisabled(true);

  if (!client_is_observer() && client.conn.playing != NULL) {
    value = city_production_buy_gold_cost(pcity);
    str = QString(PL_("Buy (%1 gold)", "Buy (%1 gold)", value)).arg(QString::number(value));
    if (client.conn.playing->economic.gold >= value && value != 0) {
      buy_button->setEnabled(true);
      buy_button_p->setEnabled(true);
    }
  } else {
    str = QString(_("Buy"));
  }
  buy_button->setText(str);
  buy_button_p->setText(str);
}

/****************************************************************************
  Redraws citizens for city_label (citizens_label)
****************************************************************************/
void city_dialog::update_citizens()
{
  enum citizen_category citizens[MAX_CITY_SIZE];
  int i, j, width, height;
  QPainter p;
  QPixmap *pix;
  int num_citizens = get_city_citizen_types(pcity, FEELING_FINAL, citizens);
  int w = tileset_small_sprite_width(tileset);
  int h = tileset_small_sprite_height(tileset);

  i = 1 + (num_citizens * 5 / 200);
  w = w  / i;
  QRect source_rect(0, 0, w, h);
  QRect dest_rect(0, 0, w, h);
  width = w * num_citizens;
  height = h;

  if (citizen_pixmap) {
    delete citizen_pixmap;
  }
  citizen_pixmap = new QPixmap(width, height);
  for (j = 0, i = 0; i < num_citizens; i++, j++) {
    dest_rect.moveTo(i * w, 0);
    pix = get_citizen_sprite(tileset, citizens[j], j, pcity)->pm;
    p.begin(citizen_pixmap);
    p.drawPixmap(dest_rect, *pix, source_rect);
    p.end();
  }
  citizens_label->set_city(pcity);
  citizens_label->setPixmap(*citizen_pixmap);

  lab_table[FEELING_FINAL]->setPixmap(*citizen_pixmap);
  lab_table[FEELING_FINAL]->setToolTip(text_happiness_wonders(pcity));
  for (int k = 0; k < FEELING_LAST - 1; k++) {
    lab_table[k]->set_city(pcity);
    num_citizens = get_city_citizen_types(pcity,
                                          static_cast<citizen_feeling>(k),
                                          citizens);
    for (j = 0, i = 0; i < num_citizens; i++, j++) {
      dest_rect.moveTo(i * w, 0);
      pix = get_citizen_sprite(tileset, citizens[j], j, pcity)->pm;
      p.begin(citizen_pixmap);
      p.drawPixmap(dest_rect, *pix, source_rect);
      p.end();
    }
    lab_table[k]->setPixmap(*citizen_pixmap);
    switch (k) {
    case FEELING_BASE:
      lab_table[k]->setToolTip(text_happiness_cities(pcity));
      break;
    case FEELING_LUXURY:
      lab_table[k]->setToolTip(text_happiness_luxuries(pcity));
      break;
    case FEELING_EFFECT :
      lab_table[k]->setToolTip(text_happiness_buildings(pcity));
      break;
    case FEELING_NATIONALITY:
      lab_table[k]->setToolTip(text_happiness_nationality(pcity));
      break;
    case FEELING_MARTIAL:
      lab_table[k]->setToolTip(text_happiness_units(pcity));
      break;
    default:
      break;
    }
  }
}

/****************************************************************************
  Various refresh after getting new info/reply from server
****************************************************************************/
void city_dialog::refresh()
{
  setUpdatesEnabled(false);
  production_combo->blockSignals(true);
  production_combo_p->blockSignals(true);
  if (pcity) {
    view->set_pixmap(pcity);
    info_view->set_pixmap(pcity);
    view->update();
    info_view->update();
    update_title();
    update_info_label();
    update_buy_button();
    update_citizens();
    update_building();
    update_improvements();
    update_units();
    update_settings();
    update_nation_table();
    update_cma_tab();
    update_disabled();
  } else {
    destroy_city_dialog();
  }
  production_combo_p->blockSignals(false);
  production_combo->blockSignals(false);
  setUpdatesEnabled(true);
  updateGeometry();
  update();
}

void city_dialog::update_settings()
{
  disband_at_one->setChecked(is_city_option_set(pcity, CITYO_DISBAND));
}

/****************************************************************************
  Updates nationality table in happiness tab
****************************************************************************/
void city_dialog::update_nation_table()
{
  QFont f = QApplication::font();
  QFontMetrics fm(f);
  QPixmap *pix = NULL;
  QPixmap pix_scaled;
  QString str;
  QStringList info_list;
  QTableWidgetItem *item;
  char buf[8];
  citizens nationality_i;
  int h;
  int i = 0;
  struct sprite *sprite;

  h = fm.height() + 6;
  nationality_table->clear();
  nationality_table->setRowCount(0);
  info_list.clear();
  info_list << _("#") << _("Flag") << _("Nation");
  nationality_table->setHorizontalHeaderLabels(info_list);

  citizens_iterate(pcity, pslot, nationality) {
    nationality_table->insertRow(i);
    for (int j = 0; j < nationality_table->columnCount(); j++) {
      item = new QTableWidgetItem;
      switch (j) {
      case 0:
        nationality_i = citizens_nation_get(pcity, pslot);
        if (nationality_i == 0) {
          str = "-";
        } else {
          fc_snprintf(buf, sizeof(buf), "%d", nationality_i);
          str = QString(buf);
        }
        item->setText(str);
        break;
      case 1:
        sprite = get_nation_flag_sprite(tileset,
                                        nation_of_player
                                        (player_slot_get_player(pslot)));
        if (sprite != NULL) {
          pix = sprite->pm;
          pix_scaled = pix->scaledToHeight(h);
          item->setData(Qt::DecorationRole, pix_scaled);
        } else {
          item->setText("FLAG MISSING");
        }
        break;
      case 2:
        item->setText(nation_adjective_for_player
                     (player_slot_get_player(pslot)));
        break;
      default:
        break;
      }
      nationality_table->setItem(i, j, item);
    }
    i++;
  } citizens_iterate_end;
  nationality_table->horizontalHeader()->setStretchLastSection(false);
  nationality_table->resizeColumnsToContents();
  nationality_table->resizeRowsToContents();
  nationality_table->horizontalHeader()->setStretchLastSection(true);
}

/****************************************************************************
  Updates information label ( food, prod ... surpluses ...)
****************************************************************************/
void city_dialog::update_info_label()
{
  int illness = 0;
  char buffer[512];
  char buf[2 * NUM_INFO_FIELDS][512];
  int granaryturns;

  enum { FOOD = 0, SHIELD = 2, TRADE = 4, GOLD = 6, LUXURY = 8, SCIENCE = 10,
         GRANARY = 12, GROWTH = 14, CORRUPTION = 16, WASTE = 18,
         POLLUTION = 20, ILLNESS = 22
       };

  /* fill the buffers with the necessary info */
  fc_snprintf(buf[FOOD], sizeof(buf[FOOD]), "%3d (%+4d)",
              pcity->prod[O_FOOD], pcity->surplus[O_FOOD]);
  fc_snprintf(buf[SHIELD], sizeof(buf[SHIELD]), "%3d (%+4d)",
              pcity->prod[O_SHIELD] + pcity->waste[O_SHIELD],
              pcity->surplus[O_SHIELD]);
  fc_snprintf(buf[TRADE], sizeof(buf[TRADE]), "%3d (%+4d)",
              pcity->surplus[O_TRADE] + pcity->waste[O_TRADE],
              pcity->surplus[O_TRADE]);
  fc_snprintf(buf[GOLD], sizeof(buf[GOLD]), "%3d (%+4d)",
              pcity->prod[O_GOLD], pcity->surplus[O_GOLD]);
  fc_snprintf(buf[LUXURY], sizeof(buf[LUXURY]), "%3d",
              pcity->prod[O_LUXURY]);
  fc_snprintf(buf[SCIENCE], sizeof(buf[SCIENCE]), "%3d",
              pcity->prod[O_SCIENCE]);
  fc_snprintf(buf[GRANARY], sizeof(buf[GRANARY]), "%4d/%-4d",
              pcity->food_stock, city_granary_size(city_size_get(pcity)));

  get_city_dialog_output_text(pcity, O_FOOD, buf[FOOD + 1],
                              sizeof(buf[FOOD + 1]));
  get_city_dialog_output_text(pcity, O_SHIELD, buf[SHIELD + 1],
                              sizeof(buf[SHIELD + 1]));
  get_city_dialog_output_text(pcity, O_TRADE, buf[TRADE + 1],
                              sizeof(buf[TRADE + 1]));
  get_city_dialog_output_text(pcity, O_GOLD, buf[GOLD + 1],
                              sizeof(buf[GOLD + 1]));
  get_city_dialog_output_text(pcity, O_SCIENCE, buf[SCIENCE + 1],
                              sizeof(buf[SCIENCE + 1]));
  get_city_dialog_output_text(pcity, O_LUXURY, buf[LUXURY + 1],
                              sizeof(buf[LUXURY + 1]));
  get_city_dialog_pollution_text(pcity, buf[POLLUTION+1],
                                 sizeof(buf[POLLUTION +1]));
  get_city_dialog_illness_text(pcity, buf[ILLNESS + 1],
                               sizeof(buf[ILLNESS + 1]));

  granaryturns = city_turns_to_grow(pcity);
  if (granaryturns == 0) {
    /* TRANS: city growth is blocked.  Keep short. */
    fc_snprintf(buf[GROWTH], sizeof(buf[GROWTH]), _("blocked"));
  } else if (granaryturns == FC_INFINITY) {
    /* TRANS: city is not growing.  Keep short. */
    fc_snprintf(buf[GROWTH], sizeof(buf[GROWTH]), _("never"));
  } else {
    /* A negative value means we'll have famine in that many turns.
       But that's handled down below. */
    /* TRANS: city growth turns.  Keep short. */
    fc_snprintf(buf[GROWTH], sizeof(buf[GROWTH]),
                PL_("%d turn", "%d turns", abs(granaryturns)),
                abs(granaryturns));
  }

  fc_snprintf(buf[CORRUPTION], sizeof(buf[CORRUPTION]), "%4d",
              pcity->waste[O_TRADE]);
  fc_snprintf(buf[WASTE], sizeof(buf[WASTE]), "%4d", pcity->waste[O_SHIELD]);
  fc_snprintf(buf[POLLUTION], sizeof(buf[POLLUTION]), "%4d",
              pcity->pollution);

  if (!game.info.illness_on) {
    fc_snprintf(buf[ILLNESS], sizeof(buf[ILLNESS]), " -.-");
  } else {
    illness = city_illness_calc(pcity, NULL, NULL, NULL, NULL);
    /* illness is in tenth of percent */
    fc_snprintf(buf[ILLNESS], sizeof(buf[ILLNESS]), "%4.1f",
                (float) illness / 10.0);
  }

  get_city_dialog_output_text(pcity, O_FOOD, buffer, sizeof(buffer));
  for (int i = 0; i < NUM_INFO_FIELDS; i++) {
    int j = 2 * i;
    qlt[i]->setText(QString(buf[2 * i]));
    if (j != GROWTH && j != GRANARY && j!= WASTE && j!= CORRUPTION){
      qlt[i]->setToolTip(QString(buf[2 * i + 1]));
    }
  }
}

/****************************************************************************
  Setups whole city dialog, public function
****************************************************************************/
void city_dialog::setup_ui(struct city *qcity)
{
  QPixmap q_pix = *get_icon_sprite(tileset, ICON_CITYDLG)->pm;
  QIcon q_icon =::QIcon(q_pix);

  setWindowIcon(q_icon);
  pcity = qcity;
  production_combo->blockSignals(true);
  production_combo_p->blockSignals(true);
  refresh();
  production_combo_p->blockSignals(false);
  production_combo->blockSignals(false);

}

/****************************************************************************
  Action on double clicked buildings in city (sell it)
****************************************************************************/
void city_dialog::dbl_click(QTableWidgetItem *item)
{
  QMessageBox ask(gui()->central_wdg);
  QString s;
  QVariant qvar;
  char buf[256];
  cid uid;
  int price;
  int ret;
  struct universal selected;
  struct impr_type *pimprove;

  qvar = item->data(Qt::UserRole);
  uid = qvar.toInt();
  selected = cid_decode(uid);
  pimprove = selected.value.building;

  if (!can_client_issue_orders()) {
    return;
  }
  if (test_player_sell_building_now(client.conn.playing, pcity,
                                    pimprove) != TR_SUCCESS) {
    return;
  }
  price = impr_sell_gold(pimprove);
  fc_snprintf(buf, ARRAY_SIZE(buf),
              PL_("Sell %s for %d gold?",
                  "Sell %s for %d gold?", price),
              city_improvement_name_translation(pcity, pimprove), price);

  s = QString(buf);
  ask.setText(s);
  ask.setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
  ask.setDefaultButton(QMessageBox::Cancel);
  ask.setWindowTitle(_("Sell improvement?"));
  ret = ask.exec();

  switch (ret) {
  case QMessageBox::Cancel:
    return;
  case QMessageBox::Ok:
    city_sell_improvement(pcity, improvement_number(pimprove));
    break;
  }
}

/****************************************************************************
  Removes selected item from city worklist
****************************************************************************/
void city_dialog::delete_prod()
{
  display_worklist_menu(QCursor::pos());
}

/****************************************************************************
  Double clicked item in worklist table in production tab
****************************************************************************/
void city_dialog::dbl_click_p(QTableWidgetItem *item)
{
  struct worklist queue;
  city_get_queue(pcity, &queue);
  if (selected_row_p < 0 || selected_row_p > worklist_length(&queue)) {
    return;
  }
  worklist_remove(&queue, selected_row_p);
  city_set_queue(pcity, &queue);
}

/****************************************************************************
  Updates layouts for supported and present units in city
****************************************************************************/
void city_dialog::update_units()
{
  unit_item *ui;
  struct unit_list *units;
  char buf[256];
  int n;
  int happy_cost;
  int free_unhappy = get_city_bonus(pcity, EFT_MAKE_CONTENT_MIL);
  supported_units->setUpdatesEnabled(false);
  supported_units->clear_layout();

  if (NULL != client.conn.playing
      && city_owner(pcity) != client.conn.playing) {
    units = pcity->client.info_units_supported;
  } else {
    units = pcity->units_supported;
  }

  unit_list_iterate(units, punit) {
    happy_cost = city_unit_unhappiness(punit, &free_unhappy);
    ui = new unit_item(this, punit, true, happy_cost);
    ui->init_pix();
    supported_units->add_item(ui);
  } unit_list_iterate_end;
  n = unit_list_size(units);
  fc_snprintf(buf, sizeof(buf), _("Supported units %d"), n);
  supp_units->setText(QString(buf));
  supported_units->update_units();
  supported_units->setUpdatesEnabled(true);
  current_units->setUpdatesEnabled(true);
  current_units->clear_layout();

  if (NULL != client.conn.playing
      && city_owner(pcity) != client.conn.playing) {
    units = pcity->client.info_units_present;
  } else {
    units = pcity->tile->units;
  }

  unit_list_iterate(units, punit) {
    ui = new unit_item(this ,punit, false);
    ui->init_pix();
    current_units->add_item(ui);
  } unit_list_iterate_end;

  n = unit_list_size(units);
  fc_snprintf(buf, sizeof(buf), _("Present units %d"), n);
  curr_units->setText(QString(buf));

  current_units->update_units();
  current_units->setUpdatesEnabled(true);
}

/****************************************************************************
  Selection changed in production tab, in worlist tab
****************************************************************************/
void city_dialog::item_selected(const QItemSelection &sl,
                                const QItemSelection &ds)
{
  QModelIndex index;
  QModelIndexList indexes = sl.indexes();

  if (indexes.isEmpty()) {
    return;
  }
  index = indexes.at(0);
  selected_row_p = index.row();
  update_prod_buttons();
}

/****************************************************************************
  Changes city_dialog to next city after pushing next city button
****************************************************************************/
void city_dialog::next_city()
{
  int size, i, j;
  struct city *other_pcity = NULL;

  if (NULL == client.conn.playing) {
    return;
  }
  size = city_list_size(client.conn.playing->cities);
  if (size == 1) {
    return;
  }
  for (i = 0; i < size; i++) {
    if (pcity == city_list_get(client.conn.playing->cities, i)) {
      break;
    }
  }
  for (j = 1; j < size; j++) {
    other_pcity = city_list_get(client.conn.playing->cities,
                                (i + j + size) % size);
  }
  qtg_real_city_dialog_popup(other_pcity);
}

/****************************************************************************
  Changes city_dialog to previous city after pushing prev city button
****************************************************************************/
void city_dialog::prev_city()
{
  int size, i, j;
  struct city *other_pcity = NULL;

  if (NULL == client.conn.playing) {
    return;
  }
  size = city_list_size(client.conn.playing->cities);
  if (size == 1) {
    return;
  }
  for (i = 0; i < size; i++) {
    if (pcity == city_list_get(client.conn.playing->cities, i)) {
      break;
    }
  }
  for (j = 1; j < size; j++) {
    other_pcity = city_list_get(client.conn.playing->cities,
                                (i - j + size) % size);

  }
  qtg_real_city_dialog_popup(other_pcity);
}

/****************************************************************************
  Updates building improvement/unit
****************************************************************************/
void city_dialog::update_building()
{
  char buf[32];
  QPixmap *pix;
  QString str;
  struct sprite *sprite;
  int cost = city_production_build_shield_cost(pcity);

  get_city_dialog_production(pcity, buf, sizeof(buf));
  production_combo->setRange(0, cost);
  production_combo_p->setRange(0, cost);
  if (pcity->shield_stock >= cost) {
    production_combo->setValue(cost);
    production_combo_p->setValue(cost);
  } else {
    production_combo->setValue(pcity->shield_stock);
    production_combo_p->setValue(pcity->shield_stock);
  }
  production_combo->setAlignment(Qt::AlignCenter);
  production_combo_p->setAlignment(Qt::AlignCenter);
  str = QString(buf);
  str = str.simplified();
  production_combo->setFormat(QString("(%p%) %1\n%2")
                              .arg(city_production_name_translation(pcity),
                                   str));

  production_combo_p->setFormat(QString("(%p%) %2\n%1")
                                .arg(city_production_name_translation(pcity),
                                     str));

  if (VUT_UTYPE == pcity->production.kind) {
    sprite = get_unittype_sprite(tileset, pcity->production.value.utype,
                                 direction8_invalid(), true);
  } else {

    sprite = get_building_sprite(tileset, pcity->production.value.building);
  }

  if (sprite != NULL) {
    pix = sprite->pm;
    item_button->setIconSize(pix->size());
    item_button->setIcon(QIcon(*pix));
    item_button->updateGeometry();
    item_button_p->setIconSize(pix->size());
    item_button_p->setIcon(QIcon(*pix));
    item_button_p->setMaximumWidth(pix->width() * 2);
    item_button_p->updateGeometry();
  }
  production_combo->updateGeometry();
  production_combo_p->updateGeometry();

}

/****************************************************************************
  Buy button. Shows message box asking for confirmation
****************************************************************************/
void city_dialog::buy()
{
  char buf[1024];
  int ret;
  const char *name = city_production_name_translation(pcity);
  int value = city_production_buy_gold_cost(pcity);
  const QString title = _("Buy ?");
  QMessageBox ask(gui()->central_wdg);

  if (!can_client_issue_orders()) {
    return;
  }

  fc_snprintf(buf, ARRAY_SIZE(buf), PL_("Treasury contains %d gold.",
                                        "Treasury contains %d gold.",
                                        client_player()->economic.gold),
              client_player()->economic.gold);
  ask.setInformativeText(buf);
  fc_snprintf(buf, ARRAY_SIZE(buf), PL_("Buy %s for %d gold?",
                                        "Buy %s for %d gold?", value),
              name, value);

  ask.setText(buf);
  ask.setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
  ask.setDefaultButton(QMessageBox::Cancel);
  ask.setIcon(QMessageBox::Question);
  ask.setWindowTitle(title);
  ret = ask.exec();

  switch (ret) {
  case QMessageBox::Cancel:
    return;
    break;
  case QMessageBox::Ok:
    city_buy_production(pcity);
    break;
  }
}

/****************************************************************************
  Updates list of improvements
****************************************************************************/
void city_dialog::update_improvements()
{
  QFont f = QApplication::font();
  QFontMetrics fm(f);
  QPixmap *pix = NULL;
  QPixmap pix_scaled;
  QString str;
  QTableWidgetItem *qitem;
  struct sprite *sprite;
  int h, cost, total, item, targets_used, col, upkeep;
  struct item items[MAX_NUM_PRODUCTION_TARGETS];
  struct universal targets[MAX_NUM_PRODUCTION_TARGETS];
  struct worklist queue;
  QSize size;

  h = fm.height() + 6;
  targets_used = collect_already_built_targets(targets, pcity);
  name_and_sort_items(targets, targets_used, items, false, pcity);

  production_table->clear();
  production_table->setRowCount(0);

  total = 0;
  for (item = 0; item < targets_used; item++) {
    struct universal target = items[item].item;

    fc_assert_action(VUT_IMPROVEMENT == target.kind, continue);
    /* This takes effects (like Adam Smith's) into account. */
    upkeep = city_improvement_upkeep(pcity, target.value.building);
    sprite = get_building_sprite(tileset, target.value.building);
    if (sprite != NULL) {
      pix = sprite->pm;
      pix_scaled = pix->scaledToHeight(h);
    }
    production_table->insertRow(item);
    for (col = 0; col < 3; col++) {
      qitem = new QTableWidgetItem();
      qitem->setData(Qt::UserRole, cid_encode(target));
      switch (col) {
      case 0:
        if (sprite != NULL) {
          qitem->setData(Qt::DecorationRole, pix_scaled);
          size.setHeight(h);
          size.setWidth(pix_scaled.width() + 6);
          qitem->setSizeHint(size);
        }
        break;
      case 1:
        qitem->setText(items[item].descr);
        break;
      case 2:
        qitem->setTextAlignment(Qt::AlignRight);
        qitem->setText(QString::number(upkeep));
      }
      production_table->setItem(item, col, qitem);
      /**
       * FIXME total not used
       */
      total += upkeep;
    }
  }
  /* turn stretch off and on to prevent double refresh */
  production_table->horizontalHeader()->setStretchLastSection(false);
  production_table->resizeColumnsToContents();
  production_table->resizeRowsToContents();
  production_table->horizontalHeader()->setStretchLastSection(true);
  city_get_queue(pcity, &queue);
  p_table_p->setRowCount(worklist_length(&queue));

  for (int i = 0; i < worklist_length(&queue); i++) {
    struct universal target = queue.entries[i];

    if (VUT_UTYPE == target.kind) {
      str = utype_values_translation(target.value.utype);
      cost = utype_build_shield_cost(target.value.utype);
      sprite = get_unittype_sprite(tileset, target.value.utype,
                                   direction8_invalid(), true);
    } else {
      str = city_improvement_name_translation(pcity, target.value.building);
      sprite = get_building_sprite(tileset, target.value.building);
      if (improvement_has_flag(target.value.building, IF_GOLD)) {
        cost = -1;
      } else {
        cost = impr_build_shield_cost(target.value.building);
      }
    }
    for (col = 0; col < 3; col++) {
      qitem = new QTableWidgetItem();
      switch (col) {
      case 0:
        if (sprite) {
          pix = sprite->pm;
          pix_scaled = pix->scaledToHeight(h);
          qitem->setData(Qt::DecorationRole, pix_scaled);
        }
        break;
      case 1:
        qitem->setText(str);
        break;
      case 2:
        qitem->setTextAlignment(Qt::AlignRight);
        qitem->setText(QString::number(cost));
        break;
      }
      p_table_p->setItem(i, col, qitem);
    }
  }
  p_table_p->horizontalHeader()->setStretchLastSection(false);
  p_table_p->resizeColumnsToContents();
  p_table_p->resizeRowsToContents();
  p_table_p->horizontalHeader()->setStretchLastSection(true);
}

/****************************************************************************
  Slot executed when user changed production in customized table widget
****************************************************************************/
void city_dialog::production_changed(int index)
{
  cid cid;
  QVariant qvar;

  if (can_client_issue_orders()) {
    cid = qvar.toInt();
    city_change_production(pcity, cid_production(cid));
  }
}

/****************************************************************************
  Shows customized table widget with available items to produce
  Shows default targets in overview city page
****************************************************************************/
void city_dialog::show_targets()
{
  production_widget *pw;

  pw = new production_widget(this, pcity, false, 0, 0, true);
  pw->show();
}

/****************************************************************************
  Shows customized table widget with available items to produce
  Shows customized targets in city production page
****************************************************************************/
void city_dialog::show_targets_worklist()
{
  production_widget *pw;
  int when = 1;
  bool future = false;
  bool show_units = true;

  if (future_targets_p->isChecked()) {
    future = true;
  }
  if (!show_units_p->isChecked()) {
    show_units = false;
  }
  if (r2->isChecked()) {
    when = 2;
  } else if (r3->isChecked()) {
    when = 3;
  } else if
  (r4->isChecked()) {
    when = 4;
  }

  pw = new production_widget(this, pcity, future, when, selected_row_p,
                             show_units);
  pw->show();
}

/****************************************************************************
  Clears worklist in production page
****************************************************************************/
void city_dialog::clear_worklist()
{
  struct worklist empty;

  if (!can_client_issue_orders()) {
    return;
  }
  worklist_init(&empty);
  city_set_worklist(pcity, &empty);
}

/****************************************************************************
  Move current item on worklist up
****************************************************************************/
void city_dialog::worklist_up()
{
  QModelIndex index;
  struct worklist queue;
  struct universal *target = new universal;
  QVariant qvar;

  if (selected_row_p < 1 || selected_row_p >= p_table_p->rowCount()) {
    return;
  }
  city_get_queue(pcity, &queue);
  worklist_peek_ith(&queue, target, selected_row_p);
  worklist_remove(&queue, selected_row_p);
  worklist_insert(&queue, *target, selected_row_p - 1);
  city_set_queue(pcity, &queue);
  index = p_table_p->model()->index(selected_row_p - 1, 0);
  p_table_p->setCurrentIndex(index);
  delete target;

}

/****************************************************************************
  Remove current item on worklist
****************************************************************************/
void city_dialog::worklist_del()
{
  QTableWidgetItem *item;
  if (selected_row_p < 0 
      || selected_row_p >= p_table_p->rowCount()) {
    return;
  }
  item = p_table_p->item(selected_row_p, 0);
  dbl_click_p(item);
  update_prod_buttons();
}

/****************************************************************************
  Move current item on worklist down
****************************************************************************/
void city_dialog::worklist_down()
{
  QModelIndex index;
  struct worklist queue;
  struct universal *target = new universal;
  QVariant qvar;

  if (selected_row_p < 0 || selected_row_p >= p_table_p->rowCount() - 1 ) {
    return;
  }
  city_get_queue(pcity, &queue);
  worklist_peek_ith(&queue, target, selected_row_p);
  worklist_remove(&queue, selected_row_p);
  worklist_insert(&queue, *target, selected_row_p + 1);
  city_set_queue(pcity, &queue);
  index = p_table_p->model()->index(selected_row_p + 1, 0);
  p_table_p->setCurrentIndex(index);
  delete target;
}

/****************************************************************************
  Save worklist
****************************************************************************/
void city_dialog::save_worklist()
{
  struct worklist queue;
  struct global_worklist *gw;
  bool ok;
  QString text = QInputDialog::getText(this,
                                       _("Save current worklist"),
                                       _("What should we name new worklist?"),
                                       QLineEdit::Normal,
                                       _("New worklist"), &ok);

  if (ok && !text.isEmpty()) {
    gw = global_worklist_new(text.toLocal8Bit().data());
    city_get_queue(pcity, &queue);
    global_worklist_set(gw, &queue);
  }
}

/****************************************************************************
  Puts city name and people count on title
****************************************************************************/
void city_dialog::update_title()
{
  QString buf;

  lcity_name->setText(QString(city_name(pcity)));
  pcity_name->setText(QString(city_name(pcity)));
  if (city_unhappy(pcity)) {
    /* TRANS: city dialog title */
    buf = QString(_("%1 - %2 citizens - DISORDER")).arg(city_name(pcity),
                                 population_to_text(city_population(pcity)));
  } else if (city_celebrating(pcity)) {
    /* TRANS: city dialog title */
    buf = QString(_("%1 - %2 citizens - celebrating")).arg(city_name(pcity),
                                 population_to_text(city_population(pcity)));
  } else if (city_happy(pcity)) {
    /* TRANS: city dialog title */
    buf = QString(_("%1 - %2 citizens - happy")).arg(city_name(pcity),
                                 population_to_text(city_population(pcity)));
  } else {
    /* TRANS: city dialog title */
    buf = QString(_("%1 - %2 citizens")).arg(city_name(pcity),
                                 population_to_text(city_population(pcity)));
  }

  setWindowTitle(buf);
}


/**************************************************************************
  Pop up (or bring to the front) a dialog for the given city.  It may or
  may not be modal.
  To make modal create it as new city_dialog(gui()->central_wdg))
**************************************************************************/
void qtg_real_city_dialog_popup(struct city *pcity)
{
  if (!::city_dlg_created) {
    ::city_dlg = new city_dialog(gui()->mapview_wdg);
  }

  city_dlg->setup_ui(pcity);
  city_dlg->showNormal();
  city_dlg->activateWindow();
  city_dlg->raise();
}

/****************************************************************************
  Closes city dialog
****************************************************************************/
void destroy_city_dialog()
{
  if (!::city_dlg_created) {
    return;
  }
  city_dlg->close();
  ::city_dlg_created = false;
}


/**************************************************************************
  Close the dialog for the given city.
**************************************************************************/
void qtg_popdown_city_dialog(struct city *pcity)
{
  if (!::city_dlg_created) {
    return;
  }
  destroy_city_dialog();
}

/**************************************************************************
  Close the dialogs for all cities.
**************************************************************************/
void qtg_popdown_all_city_dialogs()
{
  destroy_city_dialog();
}

/**************************************************************************
  Refresh (update) all data for the given city's dialog.
**************************************************************************/
void qtg_real_city_dialog_refresh(struct city *pcity)
{
  if (!::city_dlg_created) {
    return;
  }
  city_dlg->refresh();
}

/**************************************************************************
  Update city dialogs when the given unit's status changes.  This
  typically means updating both the unit's home city (if any) and the
  city in which it is present (if any).
**************************************************************************/
void qtg_refresh_unit_city_dialogs(struct unit *punit)
{
  /* PORTME */
#if 0
  /* Demo code */
  struct city *pcity_sup, *pcity_pre;
  struct city_dialog *pdialog;

  pcity_sup = game_city_by_number(punit->homecity);
  pcity_pre = tile_city(punit->tile);

  if (pcity_sup && (pdialog = get_city_dialog(pcity_sup))) {
    city_dialog_update_supported_units(pdialog);
  }

  if (pcity_pre && (pdialog = get_city_dialog(pcity_pre))) {
    city_dialog_update_present_units(pdialog);
  }
#endif
}

/**************************************************************************
  Return whether the dialog for the given city is open.
**************************************************************************/
bool qtg_city_dialog_is_open(struct city *pcity)
{
  if (!::city_dlg_created) {
    return false;
  }
  if (city_dlg->pcity == pcity && city_dlg->isVisible()) {
    return true;
  }
  return false;
}

/**************************************************************************
  Event filter for catching tooltip events
**************************************************************************/
bool fc_tooltip::eventFilter(QObject *obj, QEvent *ev)
{
  QHelpEvent *help_event;
  QString item_tooltip;
  QRect rect;

  if (ev->type() == QEvent::ToolTip) {
    QAbstractItemView *view = qobject_cast<QAbstractItemView *>(obj->parent());
    if (!view) {
      return false;
    }

    help_event = static_cast<QHelpEvent *>(ev);
    QPoint pos = help_event->pos();
    QModelIndex index = view->indexAt(pos);
    if (!index.isValid()) {
      return false;
    }
    item_tooltip = view->model()->data(index, Qt::ToolTipRole).toString();
    rect = view->visualRect(index);
    rect.setX(rect.x() + help_event->globalPos().x());
    rect.setY(rect.y() + help_event->globalPos().y());

    if (!item_tooltip.isEmpty()) {
      QToolTip::showText(help_event->globalPos(), item_tooltip, view, rect);
    } else {
      QToolTip::hideText();
    }
    return true;
  }
  return false;
}

QString bold(QString text)
{
  return QString("<b>" + text + "</b>");
}


/***************************************************************************
  Returns improvement properties to append in tooltip
***************************************************************************/
QString get_tooltip_improvement(impr_type *building)
{
  QString def_str;

  def_str = "<p style='white-space:pre'><b>"
            + QString(improvement_name_translation(building))
            + "</b>\n";
  def_str += QString(_("Cost: %1, Upkeep: %2\n\n"))
             .arg(impr_build_shield_cost(building))
             .arg(building->upkeep);
  return def_str;
}

/***************************************************************************
  Returns unit properties to append in tooltip
***************************************************************************/
QString get_tooltip_unit(struct unit_type *unit)
 {
  QString def_str;

  def_str = "<b>" + QString(utype_name_translation(unit)) + "</b>\n";
  def_str += "<table><tr><td>" + bold(QString(_("Attack:"))) + " "
             + QString::number(unit->attack_strength)
             + QString("</td><td>") + bold(QString(_("Defense:"))) + " "
             + QString::number(unit->defense_strength)
             + QString("</td><td>") + bold(QString(_("Move:"))) + " "
             + QString(move_points_text(unit->move_rate, TRUE))
             + QString("</td></tr><tr><td>")
             + bold(QString(_("Cost:"))) + " "
             + QString::number(utype_build_shield_cost(unit))
             + QString("</td><td>") + bold(QString(_("Basic Upkeep:")))
             + " " + QString(helptext_unit_upkeep_str(unit))
             + QString("</td></tr><tr><td>")
             + bold(QString(_("Hitpoints:"))) + " "
             + QString::number(unit->hp)
             + QString("</td><td>") + bold(QString(_("FirePower:"))) + " "
             + QString::number(unit->firepower)
             + QString("</td><td>") + bold(QString(_("Vision:"))) + " "
             + QString::number((int) sqrt((double) unit->vision_radius_sq))
             + QString("</td></tr></table><p style='white-space:pre'>");
  return def_str;
};

/**************************************************************************
  Returns shortened help for given universal ( stored in qvar )
**************************************************************************/
QString get_tooltip(QVariant qvar)
{
  QString str, def_str, ret_str;
  QStringList sl;
  char buffer[8192];
  char buf2[1];

  buf2[0] = '\0';
  struct universal *target;
  target = reinterpret_cast<universal *>(qvar.value<void *>());
  if (target == NULL) {
  } else if (VUT_UTYPE == target->kind) {
    def_str = get_tooltip_unit(target->value.utype);
    str = helptext_unit(buffer, sizeof(buffer), client.conn.playing,
                        buf2, target->value.utype);
  } else {
    if (!improvement_has_flag(target->value.building, IF_GOLD)) {
      def_str = get_tooltip_improvement(target->value.building);
    }
    str = helptext_building(buffer, sizeof(buffer), client.conn.playing,
                            NULL, target->value.building);
  }

  /* Remove all lines from help which has '*' in first 3 chars */
  ret_str = cut_helptext(str);
  ret_str = split_text(ret_str, true);
  ret_str = ret_str.trimmed();
  ret_str = def_str + ret_str;

  return ret_str;
}

/***************************************************************************
  City item delegate constructor
***************************************************************************/
city_production_delegate::city_production_delegate(QPoint sh,
                                                   QObject *parent,
                                                   struct city *city)
  : QItemDelegate(parent)
{
  pd = sh;
  item_height = sh.y();
  pcity = city;
}

/***************************************************************************
  City item delgate paint event
***************************************************************************/
void city_production_delegate::paint(QPainter *painter,
                                     const QStyleOptionViewItem &option,
                                     const QModelIndex &index) const
{
  struct universal *target;
  QString name;
  QVariant qvar;
  QPixmap *pix;
  QPixmap pix_scaled;
  QRect rect1;
  QRect rect2;
  struct sprite *sprite;
  bool useless = false;
  bool is_coinage = false;
  bool is_neutral = false;
  bool is_sea = false;
  bool is_flying = false;
  bool is_unit = true;
  QPixmap pix_dec(option.rect.width(), option.rect.height());
  QStyleOptionViewItem opt;
  color col;
  QIcon icon = qapp->style()->standardIcon(QStyle::SP_DialogCancelButton);
  bool free_sprite = false;

  if (!option.rect.isValid()) {
    return;
  }

  qvar = index.data();
  if (qvar.isNull()) {
    return;
  }
  target = reinterpret_cast<universal *>(qvar.value<void *>());
  if (target == NULL) {
    col.qcolor = Qt::white;
    sprite = qtg_create_sprite(100, 100, &col);
    free_sprite = true;
    *sprite->pm = icon.pixmap(100, 100);
    name = _("Cancel");
    is_unit = false;
  } else if (VUT_UTYPE == target->kind) {
    name = utype_name_translation(target->value.utype);
    is_neutral = utype_has_flag(target->value.utype, UTYF_CIVILIAN);
    if (utype_move_type(target->value.utype) == UMT_SEA) {
      is_sea = true;
    }
    if (utype_move_type(target->value.utype) == UMT_BOTH) {
      is_flying = true;
    }
    sprite = get_unittype_sprite(tileset, target->value.utype,
                                 direction8_invalid(), true);
  } else {
    is_unit = false;
    name = improvement_name_translation(target->value.building);
    sprite = get_building_sprite(tileset, target->value.building);
    useless = is_improvement_redundant(pcity, target->value.building);
    is_coinage = improvement_has_flag(target->value.building, IF_GOLD);
  }
  if (sprite != NULL) {
    pix = sprite->pm;
    pix_scaled = pix->scaledToHeight(item_height - 2);
    if (useless) {
      pixmap_put_x(&pix_scaled);
    }
  }
  opt = QItemDelegate::setOptions(index, option);
  painter->save();
  opt.displayAlignment = Qt::AlignLeft;
  opt.textElideMode = Qt::ElideMiddle;
  QItemDelegate::drawBackground(painter, opt, index);
  rect1 = option.rect;
  rect1.setWidth(pix_scaled.width() + 4);
  rect2 = option.rect;
  rect2.setLeft(option.rect.left() + rect1.width());
  rect2.setTop(rect2.top() + (rect2.height()
               - painter->fontMetrics().height()) / 2 );
  QItemDelegate::drawDisplay(painter, opt, rect2, name);
  if (is_unit) {
    if (is_sea) {
      pix_dec.fill(QColor(0, 0, 255, 80));
    } else if (is_flying) {
      pix_dec.fill(QColor(220, 0, 0, 80));
    } else if (is_neutral) {
      pix_dec.fill(QColor(0, 120, 0, 40));
    } else {
      pix_dec.fill(QColor(0, 0, 150, 40));
    }
    QItemDelegate::drawDecoration(painter, option, option.rect, pix_dec);
  }
  if (is_coinage) {
    pix_dec.fill(QColor(255, 255, 0, 70));
    QItemDelegate::drawDecoration(painter, option, option.rect, pix_dec);
  }
  if (!pix_scaled.isNull()) {
    QItemDelegate::drawDecoration(painter, opt, rect1, pix_scaled);
  }
  drawFocus(painter, opt, option.rect);

  painter->restore();
  if (free_sprite == TRUE) {
    qtg_free_sprite(sprite);
  }
}

/****************************************************************************
  Draws focus for given item
****************************************************************************/
void city_production_delegate::drawFocus(QPainter *painter,
                                         const QStyleOptionViewItem &option,
                                         const QRect &rect) const
{
  QPixmap pix(option.rect.width(), option.rect.height());

  if ((option.state & QStyle::State_MouseOver) == 0 || !rect.isValid()) {
    return;
  }
  pix.fill(QColor(50, 50, 50, 50));
  QItemDelegate::drawDecoration(painter, option, option.rect, pix);
}


/***************************************************************************
  Size hint for city item delegate
***************************************************************************/
QSize city_production_delegate::sizeHint(const QStyleOptionViewItem &option,
                                         const QModelIndex &index) const
{
  QSize s;

  s.setWidth(pd.x());
  s.setHeight(pd.y());
  return s;
}

/****************************************************************************
  Production item constructor
****************************************************************************/
production_item::production_item(struct universal *ptarget, 
                                 QObject *parent): QObject()
{
  setParent(parent);
  target = ptarget;
}

/****************************************************************************
  Production item destructor
****************************************************************************/
production_item::~production_item()
{
  /* allocated as renegade in model */
  if (target != NULL) {
    delete target;
  }
}

/****************************************************************************
  Returns stored data
****************************************************************************/
QVariant production_item::data() const
{
  return QVariant::fromValue((void *)target);
}

/****************************************************************************
  Sets data for item, must be declared.
****************************************************************************/
bool production_item::setData()
{
  return false;
}

/****************************************************************************
  Constructor for city production model
****************************************************************************/
city_production_model::city_production_model(struct city *pcity, bool f,
                                             bool su, QObject *parent)
  : QAbstractListModel(parent)
{
  show_units = su;
  mcity = pcity;
  future_t = f;
  populate();
}

/****************************************************************************
  Destructor for city production model
****************************************************************************/
city_production_model::~city_production_model()
{
  qDeleteAll(city_target_list);
  city_target_list.clear();
}

/****************************************************************************
  Returns data from model
****************************************************************************/
QVariant city_production_model::data(const QModelIndex &index, int role) const
{
  if (!index.isValid()) return QVariant();
  if (index.row() >= 0 && index.row() < rowCount() && index.column() >= 0
      && index.column() < columnCount()
      && (index.column() + index.row() * 3 < city_target_list.count())) {
    if (role == Qt::ToolTipRole) {
      return get_tooltip(city_target_list[index.row() * 3
                                          + index.column()]->data());
    }
    return city_target_list[index.row() * 3 + index.column()]->data();
  }
  return QVariant();
}

/****************************************************************************
  Fills model with data
****************************************************************************/
void city_production_model::populate()
{
  production_item *pi;
  struct universal targets[MAX_NUM_PRODUCTION_TARGETS];
  struct item items[MAX_NUM_PRODUCTION_TARGETS];
  struct universal *renegade;
  int item, targets_used;
  QString str;

  QFont f = QApplication::font();
  QFontMetrics fm(f);
  sh.setY(fm.height() * 2);
  sh.setX(0);

  qDeleteAll(city_target_list);
  city_target_list.clear();

  targets_used = collect_eventually_buildable_targets(targets, mcity,
                                                      future_t);
  name_and_sort_items(targets, targets_used, items, false, mcity);

  for (item = 0; item < targets_used; item++) {
    if (future_t || can_city_build_now(mcity, items[item].item)) {
      renegade = new universal(items[item].item);
      /* renagade deleted in production_item destructor */
      pi = new production_item(renegade, this);
      if (VUT_UTYPE == renegade->kind) {
        str = utype_name_translation(renegade->value.utype);
        sh.setX(qMax(sh.x(), fm.width(str)));
        if (show_units == true) {
          city_target_list << pi;
        }
      } else {
        str = improvement_name_translation(renegade->value.building);
        sh.setX(qMax(sh.x(), fm.width(str)));
        city_target_list << pi;
      }
    }
  }
  renegade = NULL;
  pi = new production_item(renegade, this);
  city_target_list << pi;
  sh.setX(2 * sh.y() + sh.x());
  sh.setX(qMin(sh.x(), 250));
}

/****************************************************************************
  Sets data in model
****************************************************************************/
bool city_production_model::setData(const QModelIndex &index,
                                    const QVariant &value, int role)
{
  if (!index.isValid() || role != Qt::DisplayRole || role != Qt::ToolTipRole)
    return false;
  if (index.row() >= 0 && index.row() < rowCount() && index.column() >= 0
      && index.column() < columnCount()) {
    bool change = city_target_list[index.row()]->setData();
    return change;
  }
  return false;
}

/****************************************************************************
  Constructor for production widget
  future - show future targets
  show_units - if to show units
  when - where to insert
  curr - current index to insert
  buy - buy if possible
****************************************************************************/
production_widget::production_widget(QWidget *parent, struct city *pcity,
                                     bool future, int when, int curr,
                                     bool show_units, bool buy): QTableView()
{
  QPoint pos, sh;
  int desk_width = QApplication::desktop()->width();
  int desk_height = QApplication::desktop()->height();
  fc_tt = new fc_tooltip(this);
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowFlags(Qt::Popup);
  verticalHeader()->setVisible(false);
  horizontalHeader()->setVisible(false);
  setProperty("showGrid", false);
  curr_selection = curr;
  sh_units = show_units;
  pw_city = pcity;
  buy_it = buy;
  when_change = when;
  list_model = new city_production_model(pw_city, future, show_units, this);
  sh = list_model->sh;
  c_p_d = new city_production_delegate(sh, this, pw_city);
  setItemDelegate(c_p_d);
  setModel(list_model);
  viewport()->installEventFilter(fc_tt);
  installEventFilter(this);
  connect(selectionModel(), SIGNAL(selectionChanged(const QItemSelection &,
                                                    const QItemSelection &)),
          SLOT(prod_selected(const QItemSelection &,
                             const QItemSelection &)));
  resizeRowsToContents();
  resizeColumnsToContents();
  setFixedWidth(3 * sh.x() + 6);
  setFixedHeight(list_model->rowCount()*sh.y() + 6);
  if (width() > desk_width) {
    setFixedWidth(desk_width);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  } else {
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  }
  if (height() > desk_height) {
    setFixedHeight(desk_height);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  } else {
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  }

  pos = QCursor::pos();
  if (pos.x() + width() > desk_width) {
    pos.setX(desk_width - width());
  } else if (pos.x() - width() < 0) {
    pos.setX(0);
  }
  if (pos.y() + height() > desk_height) {
    pos.setY(desk_height - height());
  } else if (pos.y() - height() < 0) {
    pos.setY(0);
  }
  move(pos);
  setMouseTracking(true);
  setFocus();
}

/****************************************************************************
  Mouse press event for production widget
****************************************************************************/
void production_widget::mousePressEvent(QMouseEvent *event)
{
  if (event->button() == Qt::RightButton) {
    close();
    return;
  }
  QAbstractItemView::mousePressEvent(event);
}

/****************************************************************************
  Event filter for production widget
****************************************************************************/
bool production_widget::eventFilter(QObject *obj, QEvent *ev)
{
  QRect pw_rect;
  QPoint br;
  if (obj != this)
    return false;

  if (ev->type() == QEvent::MouseButtonPress) {
    pw_rect.setTopLeft(pos());
    br.setX(pos().x() + width());
    br.setY(pos().y() + height());
    pw_rect.setBottomRight(br);
    if (!pw_rect.contains(QCursor::pos())) {
      close();
    }
  }
  return false;
}

/****************************************************************************
  Changed selection in production widget
****************************************************************************/
void production_widget::prod_selected(const QItemSelection &sl,
                                      const QItemSelection &ds)
{
  QModelIndexList indexes = selectionModel()->selectedIndexes();
  QModelIndex index;
  QVariant qvar;
  struct worklist queue;
  struct universal *target;

  if (indexes.isEmpty() || client_is_observer()) {
    return;
  }
  index = indexes.at(0);
  qvar = index.data(Qt::UserRole);
  if (qvar.isNull()) {
    return;
  }
  target = reinterpret_cast<universal *>(qvar.value<void *>());
  if (target != NULL) {
    city_get_queue(pw_city, &queue);
    switch (when_change) {
    case 0: /*Change current tech*/
      city_change_production(pw_city, *target);
      if (city_can_buy(pw_city) && buy_it) {
        city_buy_production(pw_city);
      }
      break;
    case 1:                 /* Change current (selected on list)*/
      if (curr_selection < 0 || curr_selection > worklist_length(&queue)) {
        curr_selection = 0;
      }
      worklist_remove(&queue, curr_selection);
      worklist_insert(&queue, *target, curr_selection);
      city_set_queue(pw_city, &queue);
      break;
    case 2:                 /* Insert before */
      if (curr_selection < 0 || curr_selection > worklist_length(&queue)) {
        curr_selection = 0;
      }
      curr_selection--;
      curr_selection = qMax(0, curr_selection);
      worklist_insert(&queue, *target, curr_selection);
      city_set_queue(pw_city, &queue);
      break;
    case 3:                 /* Insert after */
      if (curr_selection < 0 || curr_selection > worklist_length(&queue)) {
        curr_selection = 0;
      }
      curr_selection++;
      worklist_insert(&queue, *target, curr_selection);
      city_set_queue(pw_city, &queue);
      break;
    case 4:                 /* Add last */
      city_queue_insert(pw_city, -1, *target);
      break;
    default:
      break;
    }
  }
  close();
  destroy();
}

/****************************************************************************
  Destructor for production widget
****************************************************************************/
production_widget::~production_widget()
{
  delete c_p_d;
  delete list_model;
  viewport()->removeEventFilter(fc_tt);
  removeEventFilter(this);
}

