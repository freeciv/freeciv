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

// utility
#include "support.h"

// client
#include "city.h"
#include "game.h"
#include "climisc.h"
#include "tilespec.h"
#include "sprite.h"
#include "citydlg_common.h"
#include "mapview_common.h"
#include "control.h"


#include "citydlg.h"


#ifndef CLAMP
#define CLAMP(x, low, high)  (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))
#endif

static bool city_dlg_created = false; /** defines if dialog for city has been
                                       * already created. It's created only
                                       * once per client
                                       */
static city_dialog *city_dlg;


/****************************************************************************
  Class representing one unit, allows context menu, holds pixmap for it
****************************************************************************/
unit_item::unit_item(struct unit *punit) : QLabel()
{
  unit_pixmap = NULL;
  qunit = punit;

  if (punit) {
    unit_pixmap = qtg_canvas_create(tileset_full_tile_width(tileset),
                                    tileset_tile_height(tileset) * 3 / 2);
    unit_pixmap->map_pixmap.fill(Qt::transparent);
    put_unit(punit, unit_pixmap, 0, 0);
  }
  create_actions();
}

/****************************************************************************
  Sets pixmap for unit_item class
****************************************************************************/
void unit_item::init_pix()
{
  setPixmap(unit_pixmap->map_pixmap);
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
  QMenu menu(this);

  menu.addAction(activate);
  menu.addAction(activate_and_close);
  menu.addAction(sentry);
  menu.addAction(fortify);
  menu.addAction(disband_action);
  menu.addAction(change_home);
  menu.exec(event->globalPos());
}


/****************************************************************************
  Initializes context menu
****************************************************************************/
void unit_item::create_actions()
{
  /* FIXME -> set tooltips to show on status bar */
  activate = new QAction(_("Activate unit"), this);
  activate->setStatusTip(_("Activate given unit"));
  connect(activate, SIGNAL(triggered()), this, SLOT(activate_unit()));

  activate_and_close = new QAction(_("Activate and close dialog"), this);
  activate_and_close->setStatusTip(_("Activates given unit and closes"
                                     " dialog"));
  connect(activate_and_close, SIGNAL(triggered()), this,
          SLOT(activate_and_close_dialog()));

  sentry = new QAction(_("Sentry unit"), this);
  sentry->setStatusTip(_("Sentry given unit"));
  connect(sentry, SIGNAL(triggered()), this, SLOT(sentry_unit()));

  fortify = new QAction(_("Fortify unit"), this);
  fortify->setStatusTip(_("Fortify given unit ( Doesn't grant extra bonus"
                          " to defence. Each unit in city gets such bonus"
                          " without fortifying"));
  connect(fortify, SIGNAL(triggered()), this, SLOT(fortify_unit()));

  disband_action = new QAction(_("Disband unit"), this);
  disband_action->setStatusTip(_("Disbands given unit ( City will get"
                                 " half of production of unit"));
  connect(disband_action, SIGNAL(triggered()), this, SLOT(disband()));

  change_home = new QAction(_("Change homecity"), this);
  change_home->setStatusTip(_("Changes homecity of given unit"
                              " to current city"));
  connect(change_home, SIGNAL(triggered()), this, SLOT(change_homecity()));
}

/****************************************************************************
  Popups MessageBox  for disbanding unit and disbands it
****************************************************************************/
void unit_item::disband()
{
  QMessageBox ask;
  int ret;

  ask.setText(_("Are you sure you want to disband that unit?"));
  ask.setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
  ask.setDefaultButton(QMessageBox::Cancel);
  ask.setIcon(QMessageBox::Critical);
  ask.setWindowTitle(_("Disband unit"));
  ret = ask.exec();

  switch (ret) {
  case QMessageBox::Cancel:
    return;
    break;
  case QMessageBox::Ok:

    if (!unit_has_type_flag(qunit, UTYF_UNDISBANDABLE) && qunit) {
      request_unit_disband(qunit);
      break;
    }
  }
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
unit_info::unit_info() : QWidget()
{
  layout = new QHBoxLayout;
}

/****************************************************************************
  Destructor for unit_info
****************************************************************************/
unit_info::~unit_info()
{
  while (!unit_list.empty()) {
    unit_list.removeFirst();
  }
}

/****************************************************************************
  Adds one unit to list
****************************************************************************/
void unit_info::add_item(unit_item* item)
{
  unit_list.append(item);
}

/****************************************************************************
  Initiazlizes layout ( layout needs to change after adding units )
****************************************************************************/
void unit_info::init_layout()
{
  int i = unit_list.count();
  unit_item *ui;
  int j;

  for (j = 0; j < i; j++) {
    ui = unit_list[j];
    layout->addWidget(ui);
  }
  setLayout(layout);
}

/****************************************************************************
  Cleans layout - run it before layout initialization 
****************************************************************************/
void unit_info::clear_layout()
{
  int i = unit_list.count();
  unit_item *ui;
  int j;

  for (j = 0; j < i; j++) {
    ui = unit_list[j];
    layout->removeWidget (ui);
    delete ui;
  }

  while (!unit_list.empty()) {
    unit_list.removeFirst();
  }
}

/****************************************************************************
  city_label is used only for showing citizens icons
  and was created only to catch mouse events
****************************************************************************/
city_label::city_label(QWidget * parent) : QLabel(parent)
{
}

/****************************************************************************
  Mouse handler for city_label
****************************************************************************/
void city_label::mousePressEvent(QMouseEvent * event)
{
  int width = tileset_small_sprite_width(tileset);
  int citnum = event->x() / width;
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
city_map::city_map():QWidget()
{
  width = get_citydlg_canvas_width();
  height = get_citydlg_canvas_height();

  view = qtg_canvas_create(width, height);
  view->map_pixmap.fill(Qt::black);
}

/****************************************************************************
  Redraws whole view in city_map
****************************************************************************/
void city_map::paintEvent(QPaintEvent * event)
{
  QPainter painter;

  painter.begin(this);
  painter.drawPixmap(0, 0, width, height, view->map_pixmap);
  painter.end();
}

/****************************************************************************
  Calls function to put pixmap on view ( it doesn't draw on screen )
****************************************************************************/
void city_map::set_pixmap(struct city *pcity)
{
  city_dialog_redraw_map(pcity, view);
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

  canvas_x = event->x();
  canvas_y = event->y();
  if (canvas_to_city_pos(&city_x, &city_y,
                         city_map_radius_sq_get(mcity),
                         canvas_x, canvas_y)) {
    city_toggle_worker(mcity, city_x, city_y);
  }
}

/****************************************************************************
  Constructor for city_dialog, sets layouts, policies ... 
****************************************************************************/
city_dialog::city_dialog(QWidget * parent):QDialog(parent)
{
  QSizePolicy size_expanding_policy(QSizePolicy::Expanding,
                                    QSizePolicy::Expanding);
  QSizePolicy size_fixed_policy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  QStringList info_list;
  int info_nr;
  QLabel *ql;
  int iter;
  QHeaderView *header;
  QLabel *lab;
  QLabel *lab2;

  pcity = NULL;
  tab_widget = new QTabWidget (parent);
  tab_widget->setTabPosition (QTabWidget::South);
  tab_widget->setDocumentMode (false);
  overview_tab = new QWidget();
  tab_widget->addTab (overview_tab, _("Overview"));
  production_tab = new QWidget();
  tab_widget->addTab (production_tab, _("Production"));

  production_grid_layout = new QGridLayout();
  overview_grid_layout = new QGridLayout();
  main_grid_layout = new QGridLayout();
  main_grid_layout->addWidget(tab_widget,0,0,5,8);

  size_expanding_policy.setHorizontalStretch(0);
  size_expanding_policy.setVerticalStretch(0);
  current_building = 0;

  /** Overview tab initiazlization */

  info_widget = new QWidget (overview_tab); /** City information widget
                                      * texts about surpluses and so on */
  info_grid_layout = new QGridLayout(parent);
  info_list << _("Food:") << _("Prod:") << _("Trade:") << _("Gold:")
      << _("Luxury:") << _("Science:") << _("Granary:")
      << _("Change in:") << _("Corruption:") << _("Waste:")
      << _("Pollution:") << _("Plague Risk:");
  info_nr = info_list.count();
  for (iter = 0; iter < info_nr; iter++) {
    ql = new QLabel(info_list[iter], info_widget);
    info_grid_layout->addWidget(ql, iter, 0);
    qlt[iter] = new QLabel(info_widget);
    info_grid_layout->addWidget(qlt[iter], iter, 1);
  }
  info_widget->setLayout(info_grid_layout);
  overview_grid_layout->addWidget(info_widget, 1, 0,3,1);

  production_combo = new QComboBox(parent); /** Combo for choosing production
                                            * FIXME needs delegation for
                                            * showing ProgressBar
                                            * or change it to QlistView
                                            * or treeView */
  overview_grid_layout->addWidget(production_combo, 0, 5,1,1);
  QObject::connect(production_combo, SIGNAL(currentIndexChanged(int)),
                   SLOT(production_changed(int)));

  buy_button = new QPushButton();
  QObject::connect(buy_button, SIGNAL(clicked()), SLOT(buy()));

  overview_grid_layout->addWidget(buy_button, 1, 5,1,1);

  production_table = new QTableWidget();
  production_table->setColumnCount(3);
  production_table->setProperty("showGrid", "false");
  production_table->verticalHeader()->setVisible(false);
  production_table->horizontalHeader()->setVisible(false);

  header = production_table->horizontalHeader();
  header->resizeSections(QHeaderView::Interactive);
  overview_grid_layout->addWidget(production_table, 2, 5,3,1);

  citizens_label = new city_label();
  citizen_pixmap = NULL;
  overview_grid_layout->addWidget(citizens_label, 0, 1);

  view = new city_map();
  view->setMinimumSize(view->width, view->height);
  overview_grid_layout->addWidget(view, 1, 1,4,1);

  lab = new QLabel (_("Supported units"));
  lab2 = new QLabel (_("Present units"));

  current_units = new unit_info;
  overview_grid_layout->addWidget(lab2, 6,0,1,6);
  overview_grid_layout->addWidget(current_units, 7,0,1,6);
  supported_units = new unit_info;
  overview_grid_layout->addWidget(lab, 8,0,1,6);
  overview_grid_layout->addWidget(supported_units, 9,0,1,6);

  /** Overview tab initialization end */

  overview_tab->setLayout(overview_grid_layout);
  production_tab->setLayout(production_grid_layout);


  button = new QPushButton();
  QObject::connect(button, SIGNAL(clicked()), SLOT(hide()));
  button->setText(_("Close"));
  main_grid_layout->addWidget(button, 6, 6,1,2);

  button = new QPushButton();
  QObject::connect(button, SIGNAL(clicked()), SLOT(next_city()));
  button->setText(_("Next City"));
  main_grid_layout->addWidget(button, 6, 3,1,3);

  button = new QPushButton();
  QObject::connect(button, SIGNAL(clicked()), SLOT(prev_city()));
  button->setText(_("Previous City"));
  main_grid_layout->addWidget(button, 6, 0,1,3);

  setLayout(main_grid_layout);
  ::city_dlg_created = true;

  /**
   * Layout to be updated, but various font handling might be ported first
   */
}

/****************************************************************************
  Enables/disables buy button depending on gold ( also enables combo )
****************************************************************************/
void city_dialog::update_buy_button()
{
  QString str;
  int value;
  const bool is_coinage =
    improvement_has_flag(pcity->production.value.building, IF_GOLD);

  value = city_production_buy_gold_cost(pcity);
  str = QString::fromUtf8(_("Buy ( ")) + QString::number(value) + " gold )";
  buy_button->setText(str);
  if (client.conn.playing->economic.gold >= value && value != 0) {
    buy_button->setEnabled(true);
  } else {
    buy_button->setDisabled(true);
  }

  if (value == 0 && !is_coinage) {
    production_combo->setDisabled(true);
  } else {
    production_combo->setEnabled(true);
  }
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
  citizens_label->setPixmap(*citizen_pixmap);
}

/****************************************************************************
  Various refresh after getting new info/reply from server
****************************************************************************/
void city_dialog::refresh()
{
  if (pcity) {
    view->set_pixmap(pcity);
    view->update();
    update_title();
    update_info_label();
    update_buy_button();
    update_citizens();
    update_units();
  }
}

/****************************************************************************
  Updates information label ( food, prod ... surpluses ...)
****************************************************************************/
void city_dialog::update_info_label()
{
  int illness = 0;
  char buf[NUM_INFO_FIELDS][512];
  int granaryturns;

  enum { FOOD, SHIELD, TRADE, GOLD, LUXURY, SCIENCE,
    GRANARY, GROWTH, CORRUPTION, WASTE, POLLUTION, ILLNESS
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

  for (int i = 0; i < NUM_INFO_FIELDS; i++) {
    qlt[i]->setText(QString::fromAscii(buf[i]));
  }
}

/****************************************************************************
  Setups whole city dialog, public function
****************************************************************************/
void city_dialog::setup_ui(struct city *qcity)
{
  QPixmap q_pix = *get_icon_sprite(tileset, ICON_CITYDLG)->pm;
  QIcon q_icon=::QIcon(q_pix);

  setWindowIcon(q_icon);
  pcity = qcity;
  production_combo->blockSignals(true);
  citizens_label->set_city(pcity);
  update_title();
  update_info_label();
  update_units();
  update_building();
  update_improvements();
  view->set_pixmap(pcity);
  update_buy_button();
  production_combo->blockSignals(false);

}

/****************************************************************************
  Updates layouts for supported and present units in city
****************************************************************************/
void city_dialog::update_units()
{
  unit_item *ui;
  struct unit_list *units;

  supported_units->clear_layout();

  if (NULL != client.conn.playing
      && city_owner(pcity) != client.conn.playing) {
    units = pcity->client.info_units_supported;
  } else {
    units = pcity->units_supported;
  }

  unit_list_iterate (units, punit) {
    ui = new unit_item(punit);
    ui->init_pix();
    supported_units->add_item(ui);
  } unit_list_iterate_end;
  supported_units->init_layout();

  current_units->clear_layout();

  if (NULL != client.conn.playing
      && city_owner(pcity) != client.conn.playing) {
    units = pcity->client.info_units_present;
  } else {
    units = pcity->tile->units;
  }

  unit_list_iterate(units, punit) {
    ui = new unit_item(punit);
    ui->init_pix();
    current_units->add_item(ui);
  } unit_list_iterate_end;
  current_units->init_layout();

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
  Updates available buildings to build in combobox
****************************************************************************/
void city_dialog::update_building()
{
  /* FIXME  - no progress bar so far */
  double pct;
  struct universal targets[MAX_NUM_PRODUCTION_TARGETS];
  struct item items[MAX_NUM_PRODUCTION_TARGETS];
  int targets_used, item;
  int cost = city_production_build_shield_cost(pcity);
  int indx;

  production_combo->clear();

  /* pct - progress in building  PORTME */
  if (cost > 0) {
    pct = static_cast <double>(pcity->shield_stock)
          /static_cast<double >(cost);
    pct = CLAMP(pct, 0.0, 1.0);
  } else {
    pct = 1.0;
  }

  targets_used = collect_eventually_buildable_targets(targets, pcity, FALSE);
  name_and_sort_items(targets, targets_used, items, FALSE, pcity);

  for (item = 0; item < targets_used; item++) {
    if (can_city_build_now(pcity, items[item].item)) {
      const char *name;
      struct sprite *sprite;
      struct universal target = items[item].item;

      if (VUT_UTYPE == target.kind) {
        name = utype_name_translation(target.value.utype);
        sprite = get_unittype_sprite(tileset, target.value.utype,
                                     direction8_invalid());
      } else {
        name = improvement_name_translation(target.value.building);
        sprite = get_building_sprite(tileset, target.value.building);
      }
      production_combo->insertItem(item, *sprite->pm, name,
                                   (int) cid_encode(items[item].item));
    }
  }

  indx = production_combo->findData((int) cid_encode(pcity->production));
  production_combo->setCurrentIndex(indx);
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
  const QString title = QString::fromUtf8(_("Buy"))+QString::fromAscii(" ? ");
  QMessageBox ask(this);

   /**
   * If players lacks gold then he couldn't press button
   * cause it was disabled so no check for gold sufficienty
   */

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
  int total, item, targets_used, col;
  struct universal targets[MAX_NUM_PRODUCTION_TARGETS];
  struct item items[MAX_NUM_PRODUCTION_TARGETS];
  int upkeep;
  struct sprite *sprite;

  targets_used = collect_already_built_targets(targets, pcity);
  name_and_sort_items(targets, targets_used, items, FALSE, pcity);

  production_table->clearContents();
  production_table->setRowCount(0);

  total = 0;
  for (item = 0; item < targets_used; item++) {
    struct universal target = items[item].item;

    fc_assert_action(VUT_IMPROVEMENT == target.kind, continue);
    /* This takes effects (like Adam Smith's) into account. */
    upkeep = city_improvement_upkeep(pcity, target.value.building);
    sprite = get_building_sprite(tileset, target.value.building);

    production_table->insertRow(item);
    for (col = 0; col < 3; col++) {
      QTableWidgetItem *qitem;

      qitem = new QTableWidgetItem();
      switch (col) {
      case 0:
        if (sprite->pm) {
          qitem->setData(Qt::DecorationRole, *sprite->pm);
        } else {
          qitem->setText("ERROR");
        }
        break;
      case 1:
        qitem->setText(items[item].descr);
        break;
      case 2:
        qitem->setText(QString::number(upkeep));
      }
      production_table->setItem(item, col, qitem);
    /**
     * FIXME total not used
     */
      total += upkeep;
    }
  }

}

/****************************************************************************
  Slot executed when user changed production in combobox
****************************************************************************/
void city_dialog::production_changed(int index)
{
  cid cid;
  QVariant qvar;

  if (can_client_issue_orders()) {
    qvar = production_combo->itemData(index);
    cid = qvar.toInt();
    city_change_production(pcity, cid_production(cid));
  }
}

/****************************************************************************
  Puts city name and people count on title
****************************************************************************/
void city_dialog::update_title()
{
  QString buf;

  buf = QString::fromUtf8(city_name(pcity)) + " - "
    + QString::fromUtf8(population_to_text(city_population(pcity)))
    + " " + _("citizens");

  if (city_unhappy(pcity)) {
    buf = buf + " - " + _("DISORDER");
  } else if (city_celebrating(pcity)) {
    buf = buf + " - " + _("celebrating");
  } else if (city_happy(pcity)) {
    buf = buf + " - " + _("happy");
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
    ::city_dlg = new city_dialog();
  }

  city_dlg->setup_ui(pcity);
  city_dlg->show();
}

/**************************************************************************
  Close the dialog for the given city.
**************************************************************************/
void qtg_popdown_city_dialog(struct city *pcity)
{
  if (!::city_dlg_created) {
    return;
  }
  city_dlg->hide();
}

/**************************************************************************
  Close the dialogs for all cities.
**************************************************************************/
void qtg_popdown_all_city_dialogs()
{
  /**
   * For now only 1 city dialog could be active
   * Does it need to FIXME ?
   */
  if (!::city_dlg_created) {
    return;
  }
  city_dlg->hide();
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
  /* PORTME */
  return FALSE;
}
