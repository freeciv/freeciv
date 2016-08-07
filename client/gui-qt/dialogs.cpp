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
#include <QComboBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QRadioButton>
#include <QTableWidgetItem>
#include <QTextEdit>

// utility
#include "astring.h"

// common
#include "city.h"
#include "game.h"
#include "government.h"
#include "improvement.h"
#include "movement.h"
#include "nation.h"

// client
#include "control.h"
#include "helpdata.h"
#include "text.h"

#include "packhand.h"
#include "tilespec.h"

// gui-qt
#include "dialogs.h"
#include "qtg_cxxside.h"
#include "sprite.h"

extern void popdown_all_spaceships_dialogs();
extern void popdown_players_report();
extern void popdown_economy_report();
extern void popdown_units_report();
extern void popdown_science_report();
extern void popdown_city_report();
extern void popdown_endgame_report();

static void diplomat_keep_moving(QVariant data1, QVariant data2);
static void diplomat_incite(QVariant data1, QVariant data2);
static void spy_request_sabotage_list(QVariant data1, QVariant data2);
static void spy_sabotage(QVariant data1, QVariant data2);
static void spy_steal(QVariant data1, QVariant data2);
static void spy_steal_something(QVariant data1, QVariant data2);
static void diplomat_steal(QVariant data1, QVariant data2);
static void spy_poison(QVariant data1, QVariant data2);
static void diplomat_embassy(QVariant data1, QVariant data2);
static void spy_sabotage_unit(QVariant data1, QVariant data2);
static void diplomat_investigate(QVariant data1, QVariant data2);
static void diplomat_sabotage(QVariant data1, QVariant data2);
static void diplomat_bribe(QVariant data1, QVariant data2);
static void caravan_establish_trade(QVariant data1, QVariant data2);
static void caravan_help_build(QVariant data1, QVariant data2);
static void keep_moving(QVariant data1, QVariant data2);
static void caravan_keep_moving(QVariant data1, QVariant data2);
static void pillage_something(QVariant data1, QVariant data2);

static int caravan_city_id = 0;
static int caravan_unit_id = 0;
static bool caravan_dialog_open = false;
static bool is_showing_pillage_dialog = false;
static choice_dialog *caravan_dialog = NULL;
static races_dialog* race_dialog;
static bool is_race_dialog_open = false;

/***************************************************************************
 Constructor for selecting nations
***************************************************************************/
races_dialog::races_dialog(struct player *pplayer,
                           QWidget * parent) : QDialog(parent)
{
  int i;
  QGridLayout *qgroupbox_layout;
  QGroupBox *no_name;
  QTableWidgetItem *item;
  QPixmap *pix;
  QHeaderView *header;
  QSize size;
  QString title;
  QLabel *ns_label;

  setAttribute(Qt::WA_DeleteOnClose);
  is_race_dialog_open = true;
  main_layout = new QGridLayout;
  nation_tabs = new QTableWidget();
  selected_nation_tabs = new QTableWidget;
  city_styles = new QTableWidget;
  ok_button = new QPushButton;
  qnation_set =  new QComboBox;
  ns_label = new QLabel;
  tplayer = pplayer;

  selected_nation = -1;
  selected_style = -1;
  selected_sex = -1;
  setWindowTitle(_("Select Nation"));
  selected_nation_tabs->setRowCount(0);
  selected_nation_tabs->setColumnCount(1);
  selected_nation_tabs->setSelectionMode(QAbstractItemView::SingleSelection);
  selected_nation_tabs->verticalHeader()->setVisible(false);
  selected_nation_tabs->horizontalHeader()->setVisible(false);
  selected_nation_tabs->setProperty("showGrid", "true");
  selected_nation_tabs->setEditTriggers(QAbstractItemView::NoEditTriggers);
  selected_nation_tabs->setShowGrid(false);
  selected_nation_tabs->setAlternatingRowColors(true);

  nation_tabs->setRowCount(0);
  nation_tabs->setColumnCount(1);
  nation_tabs->setSelectionMode(QAbstractItemView::SingleSelection);
  nation_tabs->verticalHeader()->setVisible(false);
  nation_tabs->horizontalHeader()->setVisible(false);
  nation_tabs->setProperty("showGrid", "true");
  nation_tabs->setEditTriggers(QAbstractItemView::NoEditTriggers);
  nation_tabs->setShowGrid(false);
  ns_label->setText(_("Nation Set:"));
  city_styles->setRowCount(0);
  city_styles->setColumnCount(2);
  city_styles->setSelectionMode(QAbstractItemView::SingleSelection);
  city_styles->verticalHeader()->setVisible(false);
  city_styles->horizontalHeader()->setVisible(false);
  city_styles->setProperty("showGrid", "false");
  city_styles->setProperty("selectionBehavior", "SelectRows");
  city_styles->setEditTriggers(QAbstractItemView::NoEditTriggers);
  city_styles->setShowGrid(false);

  qgroupbox_layout = new QGridLayout;
  no_name = new QGroupBox(parent);
  leader_name = new QComboBox(no_name);
  is_male = new QRadioButton(no_name);
  is_female = new QRadioButton(no_name);

  leader_name->setEditable(true);
  qgroupbox_layout->addWidget(leader_name, 1, 0, 1, 2);
  qgroupbox_layout->addWidget(is_male, 2, 1);
  qgroupbox_layout->addWidget(is_female, 2, 0);
  is_female->setText(_("Female"));
  is_male->setText(_("Male"));
  no_name->setLayout(qgroupbox_layout);

  description = new QTextEdit;
  description->setReadOnly(true);
  description->setText(_("Choose nation"));
  no_name->setTitle(_("Your leader name"));

  /**
   * Fill city styles, no need to update them later
   */

  for (i = 0; i < game.control.styles_count; i++) {
    if (city_style_has_requirements(&::city_styles[i])) {
      continue;
    }
    item = new QTableWidgetItem;
    city_styles->insertRow(i);
    pix = get_sample_city_sprite(tileset, i)->pm;
    item->setData(Qt::DecorationRole, *pix);
    item->setData(Qt::UserRole, i);
    size.setWidth(pix->width());
    size.setHeight(pix->height());
    item->setSizeHint(size);
    city_styles->setItem(i, 0, item);
    item = new QTableWidgetItem;
    item->setText(city_style_name_translation(i));
    city_styles->setItem(i, 1, item);
  }
  header = city_styles->horizontalHeader();
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
  header->setSectionResizeMode(QHeaderView::Stretch);
#else
  header->setResizeMode(QHeaderView::Stretch);
#endif
  header->resizeSections(QHeaderView::ResizeToContents);
  header = city_styles->verticalHeader();
  header->resizeSections(QHeaderView::ResizeToContents);
  nation_sets_iterate(pset) {
    qnation_set->addItem(nation_set_name_translation(pset),
                         nation_set_rule_name(pset));
  } nation_sets_iterate_end;
  /* create nation sets */
  refresh();

  connect(nation_tabs->selectionModel(),
          SIGNAL(selectionChanged(const QItemSelection &,
                                  const QItemSelection &)),
          SLOT(group_selected(const QItemSelection &,
                              const QItemSelection &)));
  connect(city_styles->selectionModel(),
          SIGNAL(selectionChanged(const QItemSelection &,
                                  const QItemSelection &)),
          SLOT(style_selected(const QItemSelection &,
                              const QItemSelection &)));
  connect(selected_nation_tabs->selectionModel(),
          SIGNAL(selectionChanged(const QItemSelection &,
                                  const QItemSelection &)),
          SLOT(nation_selected(const QItemSelection &,
                              const QItemSelection &)));
  connect(leader_name, SIGNAL(currentIndexChanged(int)),
          SLOT(leader_selected(int)));
  connect(leader_name->lineEdit(), SIGNAL(returnPressed()),
          SLOT(ok_pressed()));
  connect(qnation_set, SIGNAL(currentIndexChanged(int)),
          SLOT(nationset_changed(int)));
  connect(nation_tabs->selectionModel(),
          SIGNAL(selectionChanged(const QItemSelection &,
                                  const QItemSelection &)),
          SLOT(group_selected(const QItemSelection &,
                              const QItemSelection &)));

  ok_button = new QPushButton;
  ok_button->setText(_("Cancel"));
  connect(ok_button, SIGNAL(pressed()), SLOT(cancel_pressed()));
  main_layout->addWidget(ok_button, 8, 2, 1, 1);
  random_button = new QPushButton;
  random_button->setText(_("Random"));
  connect(random_button, SIGNAL(pressed()), SLOT(random_pressed()));
  main_layout->addWidget(random_button, 8, 0, 1, 1);
  ok_button = new QPushButton;
  ok_button->setText(_("Ok"));
  connect(ok_button, SIGNAL(pressed()), SLOT(ok_pressed()));
  main_layout->addWidget(ok_button, 8, 3, 1, 1);
  main_layout->addWidget(no_name, 0, 3, 2, 1);
  if (nation_set_count() > 1) {
    main_layout->addWidget(ns_label, 0, 0, 1, 1);
    main_layout->addWidget(qnation_set, 0, 1, 1, 1);
    main_layout->addWidget(nation_tabs, 1, 0, 5, 2);
  } else {
    main_layout->addWidget(nation_tabs, 0, 0, 6, 2);
  }
  main_layout->addWidget(city_styles, 2, 3, 4, 1);
  main_layout->addWidget(description, 6, 0, 2, 4);
  main_layout->addWidget(selected_nation_tabs, 0, 2, 6, 1);

  setLayout(main_layout);
  set_index(0);
  update();
  set_index(-99);

  if (C_S_RUNNING == client_state()) {
    title = _("Edit Nation");
  } else if (NULL != pplayer && pplayer == client.conn.playing) {
    title = _("What Nation Will You Be?");
  } else {
    title = _("Pick Nation");
  }

  update_nationset_combo();
  setWindowTitle(title);
}

/***************************************************************************
  Destructor for races dialog
***************************************************************************/
races_dialog::~races_dialog()
{
  ::is_race_dialog_open = false;
}

/***************************************************************************
  Sets first index to call update of nation list
***************************************************************************/
void races_dialog::refresh()
{
  struct nation_group *group;
  QTableWidgetItem *item;
  QHeaderView *header;
  int i;
  int count;

  nation_tabs->clearContents();
  nation_tabs->setRowCount(0);
  nation_tabs->insertRow(0);
  item =  new QTableWidgetItem;
  item->setText(_("All nations"));
  item->setData(Qt::UserRole, -99);
  nation_tabs->setItem(0, 0, item);

  for (i = 1; i < nation_group_count() + 1; i++) {
    group = nation_group_by_number(i - 1);
    count = 0;
    /* checking if group is empty */
    nations_iterate(pnation) {
      if (!is_nation_playable(pnation)
          || !is_nation_pickable(pnation)
          || !nation_is_in_group(pnation, group)) {
        continue;
      }
      count ++;
    } nations_iterate_end;
    if (count == 0) {
      continue;
    }
    nation_tabs->insertRow(i);
    item =  new QTableWidgetItem;
    item->setData(Qt::UserRole, i - 1);
    item->setText(nation_group_name_translation(group));
    nation_tabs->setItem(i, 0, item);
  }
  header = nation_tabs->horizontalHeader();
  header->resizeSections(QHeaderView::Stretch);
  header = nation_tabs->verticalHeader();
  header->resizeSections(QHeaderView::ResizeToContents);
  set_index(-99);
}

/***************************************************************************
  Updates nation_set combo ( usually called from option change )
***************************************************************************/
void races_dialog::update_nationset_combo()
{
  struct option *popt;
  struct nation_set *s;

  popt = optset_option_by_name(server_optset, "nationset");
  if (popt) {
    s = nation_set_by_setting_value(option_str_get(popt));
    qnation_set->setCurrentIndex(nation_set_index(s));
    qnation_set->setToolTip(nation_set_description(s));
  }
}

/***************************************************************************
  Selected group of nation
***************************************************************************/
void races_dialog::group_selected(const QItemSelection &sl,
                                  const QItemSelection &ds)
{
  QModelIndexList indexes = sl.indexes();
  QModelIndex index ;

  if (indexes.isEmpty()) {
    return;
  }
  index = indexes.at(0);
  set_index(index.row());
}


/***************************************************************************
  Sets new nations' group by current current selection,
  index is used only when there is no current selection.
***************************************************************************/
void races_dialog::set_index(int index)
{
  QTableWidgetItem *item;
  QPixmap *pix;
  QFont f;
  struct nation_group *group;
  int i;
  struct sprite *s;
  QHeaderView *header;
  selected_nation_tabs->clearContents();
  selected_nation_tabs->setRowCount(0);

  last_index = 0;
  i = nation_tabs->currentRow();
  if (i != -1) {
    item = nation_tabs->item(i, 0);
    index = item->data(Qt::UserRole).toInt();
  }

  group = nation_group_by_number(index);
  i = 0;
  nations_iterate(pnation) {
    if (!is_nation_playable(pnation)
        || !is_nation_pickable(pnation)) {
      continue;
    }
    if (!nation_is_in_group(pnation, group) && index != -99) {
      continue;
    }
    item = new QTableWidgetItem;
    selected_nation_tabs->insertRow(i);
    s = get_nation_flag_sprite(tileset, pnation);
    if (pnation->player) {
      f = item->font();
      f.setStrikeOut(true);
      item->setFont(f);
    }
    pix = s->pm;
    item->setData(Qt::DecorationRole, *pix);
    item->setData(Qt::UserRole, nation_number(pnation));
    item->setText(nation_adjective_translation(pnation));
    selected_nation_tabs->setItem(i, 0, item);
  } nations_iterate_end;

  selected_nation_tabs->sortByColumn(0, Qt::AscendingOrder);
  header = selected_nation_tabs->horizontalHeader();
  header->resizeSections(QHeaderView::Stretch);
  header = selected_nation_tabs->verticalHeader();
  header->resizeSections(QHeaderView::ResizeToContents);
}

/***************************************************************************
  Sets selected nation and updates style and leaders selector
***************************************************************************/
void races_dialog::nation_selected(const QItemSelection &selected,
                                   const QItemSelection &deselcted)
{
  char buf[4096];
  QModelIndex index ;
  QVariant qvar,qvar2;
  QModelIndexList indexes = selected.indexes();
  QString str;
  QTableWidgetItem *item;
  int style, ind;

  if (indexes.isEmpty()) {
    return;
  }

  index = indexes.at(0);
  if (indexes.isEmpty()){
    return;
  }
  qvar = index.data(Qt::UserRole);
  selected_nation = qvar.toInt();

  helptext_nation(buf, sizeof(buf), nation_by_number(selected_nation), NULL);
  description->setText(buf);
  leader_name->clear();
  if (client.conn.playing == tplayer) {
    leader_name->addItem(client.conn.playing->name, true);
  }
  nation_leader_list_iterate(nation_leaders(nation_by_number
                                            (selected_nation)), pleader) {
    str = QString::fromUtf8(nation_leader_name(pleader));
    leader_name->addItem(str, nation_leader_is_male(pleader));
  } nation_leader_list_iterate_end;

  /**
   * select style for nation
   */

  style = city_style_of_nation(nation_by_number(selected_nation));
  qvar = qvar.fromValue<int>(style);
  item = new QTableWidgetItem;

  for (ind = 0; ind < city_styles->rowCount(); ind++) {
    item = city_styles->item(ind, 0);

    if (item->data(Qt::UserRole) == qvar) {
      city_styles->selectRow(ind);
    }
  }
}

/***************************************************************************
  Sets selected style
***************************************************************************/
void races_dialog::style_selected(const QItemSelection &selected,
                                   const QItemSelection &deselcted)
{
  QModelIndex index ;
  QVariant qvar;
  QModelIndexList indexes = selected.indexes();

  if (indexes.isEmpty()) {
    return;
  }

  index = indexes.at(0);
  qvar = index.data(Qt::UserRole);
  selected_style = qvar.toInt();
}

/***************************************************************************
  Sets selected leader
***************************************************************************/
void races_dialog::leader_selected(int index)
{
  if (leader_name->itemData(index).toBool()) {
    is_male->setChecked(true);
    is_female->setChecked(false);
    selected_sex=0;
  } else {
    is_male->setChecked(false);
    is_female->setChecked(true);
    selected_sex=1;
  }
}

/***************************************************************************
  Button accepting all selection has been pressed, closes dialog if
  everything is ok
***************************************************************************/
void races_dialog::ok_pressed()
{

  if (selected_nation == -1) {
    return;
  }

  if (selected_sex == -1) {
    output_window_append(ftc_client, _("You must select your sex."));
    return;
  }

  if (selected_style == -1) {
    output_window_append(ftc_client, _("You must select your city style."));
    return;
  }

  if (leader_name->currentText().length() == 0) {
    output_window_append(ftc_client, _("You must type a legal name."));
    return;
  }

  if (nation_by_number(selected_nation)->player != NULL) {
    output_window_append(ftc_client,
                         _("Nation has been chosen by other player"));
    return;
  }
  dsend_packet_nation_select_req(&client.conn, player_number(tplayer),
                                 selected_nation, selected_sex,
                                 leader_name->currentText().toUtf8().data(),
                                 selected_style);
  close();
  deleteLater();
}

/***************************************************************************
  Constructor for notify dialog
***************************************************************************/
notify_dialog::notify_dialog(const char *caption, const char *headline, 
                             const char *lines, QWidget *parent)
                             : fcwidget()
{
  int x, y;
  QString qlines;

  setCursor(Qt::ArrowCursor);
  setParent(parent);
  setFrameStyle(QFrame::Box);
  setWindowOpacity(0.5);
  cw = new close_widget(this);
  cw->put_to_corner();

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
  qcaption = QString(caption);
  qheadline = QString(headline);
  qlines = QString(lines);
#else
  qcaption = QString::fromAscii(caption);
  qheadline = QString::fromAscii(headline);
  qlines = QString::fromAscii(lines);
#endif
  qlist = qlines.split("\n");
  small_font =::gui()->fc_fonts.get_font("gui_qt_font_notify_label");
  x = 0;
  y = 0;
  calc_size(x, y);
  resize(x, y);
  gui()->mapview_wdg->find_place(gui()->mapview_wdg->width() - x - 4, 4,
                                 x, y, x, y, 0);
  move(x, y);
  was_destroyed = false;
}

/***************************************************************************
  Calculates size of notify dialog
***************************************************************************/
void notify_dialog::calc_size(int &x, int &y)
{
  QFontMetrics fm(*small_font);
  int i;
  QStringList str_list;

  str_list = qlist;
  str_list << qcaption << qheadline;

  for (i = 0; i < str_list.count(); i++) {
    x = qMax(x, fm.width(str_list.at(i)));
    y = y + 3 + fm.height();
  }
  x = x + 15;
}

/***************************************************************************
  Paint Event for notify dialog
***************************************************************************/
void notify_dialog::paintEvent(QPaintEvent * paint_event)
{
  QPainter painter(this);
  QPainterPath path;
  QPen pen;
  QFontMetrics fm(*small_font);
  int i;

  pen.setWidth(1);
  pen.setColor(QColor(232, 255, 0));
  painter.setBrush(QColor(0, 0, 0, 175));
  painter.drawRect(0, 0, width(), height());
  painter.setFont(*small_font);
  painter.setPen(pen);
  painter.drawText(10, fm.height() + 3, qcaption);
  painter.drawText(10, 2 * fm.height() + 6, qheadline);
  for (i = 0; i < qlist.count(); i++) {
    painter.drawText(10, 3 + (fm.height() + 3) * (i + 3), qlist[i]);
  }
  painter.drawLine(0,0,width(),0);
  painter.drawLine(0,height()-1,width(),height()-1);
  painter.drawLine(0,0,0,height());
  painter.drawLine(width()-1,0,width()-1,height());
  cw->put_to_corner();
}

/**************************************************************************
  Called when mouse button was pressed, just to close on right click
**************************************************************************/
void notify_dialog::mousePressEvent(QMouseEvent *event)
{
  cursor = event->globalPos() - geometry().topLeft();
  if (event->button() == Qt::RightButton){
    was_destroyed = true;
    close();
  }
}

/**************************************************************************
  Called when mouse button was pressed and moving around
**************************************************************************/
void notify_dialog::mouseMoveEvent(QMouseEvent *event)
{
  move(event->globalPos() - cursor);
  setCursor(Qt::SizeAllCursor);
}

/**************************************************************************
  Called when mouse button unpressed. Restores cursor.
**************************************************************************/
void notify_dialog::mouseReleaseEvent(QMouseEvent *event)
{
  setCursor(Qt::ArrowCursor);
}

/***************************************************************************
  Called when close button was pressed
***************************************************************************/
void notify_dialog::update_menu()
{
  was_destroyed = true;
  destroy();
}

/***************************************************************************
  Destructor for notify dialog, notice that somehow object is not destroyed
  immediately, so it can be still visible for parent, check boolean
  was_destroyed if u suspect it could not be destroyed yet
***************************************************************************/
notify_dialog::~notify_dialog()
{
  was_destroyed = true;
  destroy();
}

/***************************************************************************
  Button canceling all selections has been pressed. 
***************************************************************************/
void races_dialog::cancel_pressed()
{
  delete this;
}

/***************************************************************************
  Sets random nation
***************************************************************************/
void races_dialog::random_pressed()
{
  dsend_packet_nation_select_req(&client.conn,player_number(tplayer),-1,
                                 false, "", 0);
  delete this;
}

/***************************************************************************
  Notify goto dialog constructor
***************************************************************************/
notify_goto::notify_goto(const char *headline, const char *lines,
                         const struct text_tag_list *tags, tile *ptile,
                         QWidget *parent): QMessageBox(parent)
{
  setAttribute(Qt::WA_DeleteOnClose);
  goto_but = this->addButton(_("Goto Location"), QMessageBox::ActionRole);
  goto_but->setIcon(style()->standardIcon(
                      QStyle::SP_ToolBarHorizontalExtensionButton));
  inspect_but = this->addButton(_("Inspect City"), QMessageBox::ActionRole);
  inspect_but->setIcon(style()->standardIcon(QStyle::SP_FileDialogToParent));

  close_but = this->addButton(QMessageBox::Close);
  gtile = ptile;
  if (!gtile) {
    goto_but->setDisabled(true);
    inspect_but->setDisabled(true);
  } else {
    struct city *pcity = tile_city(gtile);
    inspect_but->setEnabled(NULL != pcity
                            && city_owner(pcity) == client.conn.playing);
  }
  setWindowTitle(headline);
  setText(lines);
  connect(goto_but, SIGNAL(pressed()), SLOT(goto_tile()));
  connect(inspect_but, SIGNAL(pressed()), SLOT(inspect_city()));
  connect(close_but, SIGNAL(pressed()), SLOT(close()));
  show();
}

/***************************************************************************
  Clicked goto tile in notify goto dialog
***************************************************************************/
void notify_goto::goto_tile()
{
  center_tile_mapcanvas(gtile);
  close();
}

/***************************************************************************
  Clicked inspect city in notify goto dialog
***************************************************************************/
void notify_goto::inspect_city()
{
  struct city *pcity = tile_city(gtile);
  if (pcity) {
    qtg_real_city_dialog_popup(pcity);
  }
  close();
}

/***************************************************************************
  User changed nation_set
***************************************************************************/
void races_dialog::nationset_changed(int index)
{
  QString rule_name;
  char *rn;
  struct option *poption = optset_option_by_name(server_optset, "nationset");
  rule_name = qnation_set->currentData().toString();
  rn = rule_name.toLocal8Bit().data();
  if (nation_set_by_setting_value(option_str_get(poption))
      != nation_set_by_rule_name(rn)) {
    option_str_set(poption, rn);
  }
}

/**************************************************************************
  Popup a dialog to display information about an event that has a
  specific location.  The user should be given the option to goto that
  location.
**************************************************************************/
void popup_notify_goto_dialog(const char *headline, const char *lines,
                              const struct text_tag_list *tags,
                              struct tile *ptile)
{
  notify_goto *ask = new notify_goto(headline, lines, tags, ptile,
                                     gui()->central_wdg);
  ask->show();
}

/**************************************************************************
  Popup a dialog to display connection message from server.
**************************************************************************/
void popup_connect_msg(const char *headline, const char *message)
{
  QMessageBox msg(gui()->central_wdg);

  msg.setText(message);
  msg.setStandardButtons(QMessageBox::Ok);
  msg.setWindowTitle(headline);
  msg.exec();

}

/**************************************************************************
  Popup a generic dialog to display some generic information.
**************************************************************************/
void popup_notify_dialog(const char *caption, const char *headline,
                         const char *lines)
{
  notify_dialog *nd = new notify_dialog(caption, headline, lines,
                                       gui()->mapview_wdg);
  nd->show();
}

/**************************************************************************
  Popup the nation selection dialog.
**************************************************************************/
void popup_races_dialog(struct player *pplayer)
{
  if (!is_race_dialog_open) {
    race_dialog = new races_dialog(pplayer, gui()->central_wdg);
    is_race_dialog_open = true;
    race_dialog->show();
  }
  race_dialog->showNormal();
}

/**************************************************************************
  Close the nation selection dialog.  This should allow the user to
  (at least) select a unit to activate.
**************************************************************************/
void popdown_races_dialog(void)
{
  if (is_race_dialog_open){
    race_dialog->close();
    is_race_dialog_open = false;
  }
}

/**************************************************************************
  Popup a dialog window to select units on a particular tile.
**************************************************************************/
void unit_select_dialog_popup(struct tile *ptile)
{
  if(ptile != NULL && (unit_list_size(ptile->units) > 1
      || (unit_list_size(ptile->units) == 1 && tile_city(ptile)))) {
    gui()->toggle_unit_sel_widget(ptile);
  }
}

/**************************************************************************
  Update the dialog window to select units on a particular tile.
**************************************************************************/
void unit_select_dialog_update_real(void)
{
  gui()->update_unit_sel();
}

/**************************************************************************
  Updates nationset combobox
**************************************************************************/
void update_nationset_combo()
{
  if (is_race_dialog_open) {
    race_dialog->update_nationset_combo();
  }
}

/**************************************************************************
  The server has changed the set of selectable nations.
**************************************************************************/
void races_update_pickable(bool nationset_change)
{
  if (is_race_dialog_open){
    race_dialog->refresh();
  }
}

/**************************************************************************
  In the nation selection dialog, make already-taken nations unavailable.
  This information is contained in the packet_nations_used packet.
**************************************************************************/
void races_toggles_set_sensitive(void)
{
  if (is_race_dialog_open) {
    race_dialog->refresh();
  }
}

/**************************************************************************
  Popup a dialog asking if the player wants to start a revolution.
**************************************************************************/
void popup_revolution_dialog(struct government *government)
{
  QMessageBox ask(gui()->central_wdg);
  int ret;

  if (0 > client.conn.playing->revolution_finishes) {
    ask.setText(_("You say you wanna revolution?"));
    ask.setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
    ask.setDefaultButton(QMessageBox::Cancel);
    ask.setIcon(QMessageBox::Warning);
    ask.setWindowTitle(_("Revolution!"));
    ret = ask.exec();

    switch (ret) {
    case QMessageBox::Cancel:
      break;
    case QMessageBox::Ok:
      revolution_response(government);
      break;
    }
  } else {
    revolution_response(government);
  }
}

/**************************************************************************
  Constructor for choice_dialog_button_data
**************************************************************************/
choice_dialog_button_data::choice_dialog_button_data(QPushButton *button,
                                                     pfcn_void func,
                                                     QVariant data1,
                                                     QVariant  data2)
{
  this->button = button;
  this->func = func;
  this->data1 = data1;
  this->data2 = data2;
}

/**************************************************************************
  Destructor for choice_dialog_button_data
**************************************************************************/
choice_dialog_button_data::~choice_dialog_button_data()
{
  /* Don't delete the stored data! */
}

/**************************************************************************
  Get the button it self.
**************************************************************************/
QPushButton *choice_dialog_button_data::getButton()
{
  return button;
}

/**************************************************************************
  Get the function to call when the button is pressed.
**************************************************************************/
pfcn_void choice_dialog_button_data::getFunc()
{
  return func;
}

/**************************************************************************
  Get the first piece of data to feed the function when the button is
  pressed.
**************************************************************************/
QVariant choice_dialog_button_data::getData1()
{
  return data1;
}

/**************************************************************************
  Get the second piece of data to feed the function when the button is
  pressed.
**************************************************************************/
QVariant choice_dialog_button_data::getData2()
{
  return data2;
}

/***************************************************************************
  Constructor for choice_dialog
***************************************************************************/
choice_dialog::choice_dialog(const QString title, const QString text,
                             QWidget *parent): QWidget(parent)
{
  QLabel *l = new QLabel(text);

  signal_mapper = new QSignalMapper(this);
  layout = new QVBoxLayout(this);

  layout->addWidget(l);
  setWindowFlags(Qt::Dialog);
  setWindowTitle(title);
  setAttribute(Qt::WA_DeleteOnClose);
  gui()->set_diplo_dialog(this);
  unit_id = -1;
}

/***************************************************************************
  Destructor for choice dialog
***************************************************************************/
choice_dialog::~choice_dialog()
{
  data1_list.clear();
  data2_list.clear();
  delete signal_mapper;
  gui()->set_diplo_dialog(NULL);

}

/***************************************************************************
  Sets layout for choice dialog
***************************************************************************/
void choice_dialog::set_layout()
{
  connect(signal_mapper, SIGNAL(mapped(const int &)),
           this, SLOT(execute_action(const int &)));
  setLayout(layout);
}

/***************************************************************************
  Adds new action for choice dialog
***************************************************************************/
void choice_dialog::add_item(QString title, pfcn_void func, QVariant data1,
                             QVariant data2)
{
   QPushButton *button = new QPushButton(title);
   connect(button, SIGNAL(clicked()), signal_mapper, SLOT(map()));
   signal_mapper->setMapping(button, func_list.count());
   func_list.append(func);
   data1_list.append(data1);
   data2_list.append(data2);
   layout->addWidget(button);
}

/***************************************************************************
  Shows choice dialog
***************************************************************************/
void choice_dialog::show_me()
{
  QPoint p;

  p = mapFromGlobal(QCursor::pos());
  p.setY(p.y()-this->height());
  p.setX(p.x()-this->width());
  move(p);
  show();
}

/***************************************************************************
  Returns layout in choice dialog
***************************************************************************/
QVBoxLayout *choice_dialog::get_layout()
{
  return layout;
}

/***************************************************************************
  Run chosen action and close dialog
***************************************************************************/
void choice_dialog::execute_action(const int action)
{
  pfcn_void func = func_list.at(action);
  func(data1_list.at(action), data2_list.at(action));
  close();
}

/**************************************************************************
  Put the button in the stack and temporarily remove it. When
  unstack_all_buttons() is called all buttons in the stack will be added
  to the end of the dialog.

  Can be used to place a button below existing buttons or below buttons
  added while it was in the stack.
**************************************************************************/
void choice_dialog::stack_button(const int button_number)
{
  choice_dialog_button_data *data = NULL;

  fc_assert_msg(0 <= button_number, "Invalid button number");
  if (0 > button_number) {
    return;
  }

  /* Start with grabbing the data. */
  data = new choice_dialog_button_data(
      qobject_cast<QPushButton *>(layout
                                  ->itemAt(button_number + 1)
                                  ->widget()),
      func_list.at(button_number),
      data1_list.at(button_number), data2_list.at(button_number));

  /* Store the data in the stack. */
  last_buttons_stack.append(data);

  /* Temporary remove the button so it will end up below buttons added
   * before unstack_all_buttons() is called. */
  layout->removeWidget(data->getButton());

  /* The old mappings may not be valid after reinsertion. */
  signal_mapper->removeMappings(data->getButton());

  /* Synchronize the lists with the layout. */
  func_list.removeAt(button_number);
  data1_list.removeAt(button_number);
  data2_list.removeAt(button_number);
}

/**************************************************************************
  Put all the buttons in the stack back to the dialog. They will appear
  after any other buttons. See stack_button()
**************************************************************************/
void choice_dialog::unstack_all_buttons()
{
  while (!last_buttons_stack.isEmpty()) {
    choice_dialog_button_data *data = last_buttons_stack.takeLast();

    /* Restore mapping. */
    signal_mapper->setMapping(data->getButton(), func_list.count());

    /* Reinsert the button below the other buttons. */
    func_list.append(data->getFunc());
    data1_list.append(data->getData1());
    data2_list.append(data->getData2());
    layout->addWidget(data->getButton());
  }
}

/***************************************************************************
  Action establish trade for choice dialog
***************************************************************************/
static void caravan_establish_trade(QVariant data1, QVariant data2)
{
  dsend_packet_unit_establish_trade(&client.conn, data2.toInt());
  caravan_dialog_open = false;
  process_caravan_arrival(NULL);
}

/***************************************************************************
  Action help build wonder for choice dialog
***************************************************************************/
static void caravan_help_build(QVariant data1, QVariant data2)
{
  dsend_packet_unit_help_build_wonder(&client.conn, data2.toInt());
  caravan_dialog_open = false;
  process_caravan_arrival(NULL);
}

/***************************************************************************
  Action 'do nothing' with caravan for choice dialog
***************************************************************************/
static void caravan_keep_moving(QVariant data1, QVariant data2)
{
  caravan_dialog_open = false;
  process_caravan_arrival(NULL);
}

/***************************************************************************
  Empty action for choice dialog (just do nothing)
***************************************************************************/
static void keep_moving(QVariant data1, QVariant data2)
{
}
/***************************************************************************
  Starts revolution with targeted government as target or anarchy otherwise
***************************************************************************/
void revolution_response(struct government *government)
{
  if (!government) {
    start_revolution();
  } else {
    set_government_choice(government);
  }
}
/**************************************************************************
  Popup a dialog giving a player choices when their caravan arrives at
  a city (other than its home city).  Example:
    - Establish trade route.
    - Help build wonder.
    - Keep moving.
**************************************************************************/
void popup_caravan_dialog(struct unit *punit,
                          struct city *phomecity, struct city *pdestcity)
{
  char title_buf[128], buf[128], buf2[1024];
  bool can_establish, can_trade, can_wonder;
  struct city* destcity;
  struct unit* caravan;
  QString wonder;
  QString str;
  QVariant qv1, qv2;
  pfcn_void func;

  fc_snprintf(title_buf, sizeof(title_buf),
              /* TRANS: %s is a unit type */
              _("Your %s Has Arrived"), unit_name_translation(punit));
  fc_snprintf(buf, sizeof(buf),
              _("Your %s from %s reaches the city of %s.\nWhat now?"),
              unit_name_translation(punit),
              city_name(phomecity), city_name(pdestcity));

  caravan_dialog = new choice_dialog(QString(title_buf),
                                     QString(buf),
                                     gui()->game_tab_widget);
  caravan_dialog_open = true;
  qv1 = pdestcity->id;
  qv2 = punit->id;
  caravan_city_id = pdestcity->id;
  caravan_unit_id = punit->id;
  can_trade = (unit_has_type_flag(punit, UTYF_TRADE_ROUTE)
               && can_cities_trade(phomecity, pdestcity));
  can_establish = can_trade
                  && can_establish_trade_route(phomecity, pdestcity);
  destcity = game_city_by_number(pdestcity->id);
  caravan = game_unit_by_number(punit->id);

  if (destcity && caravan && unit_can_help_build_wonder(caravan, destcity)) {
    can_wonder = true;
    fc_snprintf(buf2, sizeof(buf2), _("Help build Wonder (%d remaining)"),
                impr_build_shield_cost(destcity->production.value.building)
                - destcity->shield_stock);
    wonder = QString(buf2);
  } else {
    can_wonder = false;
    wonder = QString(_("Help build Wonder"));
  }

  if (can_trade) {
    func = caravan_establish_trade;
    str = can_establish ? QString(_("Establish Trade route")) :
          QString(_("Enter Marketplace"));
    caravan_dialog->add_item(str, func, qv1, qv2);
  }
  if (can_wonder) {
    func = caravan_help_build;
    caravan_dialog->add_item(wonder, func, qv1, qv2);
  }
  func = caravan_keep_moving;
  caravan_dialog->add_item(QString(_("Keep moving")), func, qv1, qv2);

  caravan_dialog->set_layout();
  caravan_dialog->show_me();
}

/**************************************************************************
  Is there currently a caravan dialog open?  This is important if there
  can be only one such dialog at a time; otherwise return false.
**************************************************************************/
bool caravan_dialog_is_open(int *unit_id, int *city_id)
{
  if (unit_id) {
    *unit_id = caravan_unit_id;
  }
  if (city_id) {
    *city_id = caravan_city_id;
  }
  return caravan_dialog_open;
}

/**************************************************************************
  Popup a dialog giving a diplomatic unit some options when moving into
  the target tile.
**************************************************************************/
void popup_diplomat_dialog(struct unit *punit, struct tile *dest_tile)
{
  struct city *pcity;
  struct unit *ptunit;
  struct astring title = ASTRING_INIT, text = ASTRING_INIT;
  int diplomat_id;
  int diplomat_target_id;
  QVariant qv1, qv2;
  pfcn_void func;

  if ((pcity = tile_city(dest_tile))) {
    /* Spy/Diplomat acting against a city */
    diplomat_target_id = pcity->id;
    diplomat_id = punit->id;
    gui()->set_current_unit(diplomat_id, diplomat_target_id);
    astr_set(&title,
             /* TRANS: %s is a unit name, e.g., Spy */
             _("Choose Your %s's Strategy"), unit_name_translation(punit));
    astr_set(&text,
             _("Your %s has arrived at %s.\nWhat is your command?"),
             unit_name_translation(punit),
             city_name(pcity));
    choice_dialog *cd = new choice_dialog(astr_str(&title),
                                                   astr_str(&text),
                                                   gui()->game_tab_widget);
    qv1 = punit->id;
    qv2 = pcity->id;
    cd->unit_id = diplomat_id;
    if (diplomat_can_do_action(punit, DIPLOMAT_EMBASSY, dest_tile)) {
      func = diplomat_embassy;
      cd->add_item(QString(_("Establish Embassy")),
                            func, qv1, qv2);
    }
    if (diplomat_can_do_action(punit, DIPLOMAT_INVESTIGATE, dest_tile)) {
      func = diplomat_investigate;
      cd->add_item(QString(_("Investigate City")), func, qv1, qv2);
    }
    if (!unit_has_type_flag(punit, UTYF_SPY)) {
      if (diplomat_can_do_action(punit, DIPLOMAT_SABOTAGE, dest_tile)) {
        func = diplomat_sabotage;
        cd->add_item(QString(_("Sabotage City")), func, qv1, qv2);
      }
      if (diplomat_can_do_action(punit, DIPLOMAT_STEAL, dest_tile)) {
        func = diplomat_steal;
        cd->add_item(QString(_("Steal Technology")),
                              func, qv1, qv2);
      }
      if (diplomat_can_do_action(punit, DIPLOMAT_INCITE, dest_tile)) {
        func = diplomat_incite;
        cd->add_item(QString(_("Incite a Revolt")), func, qv1, qv2);
      }
      if (unit_can_move_to_tile(punit, dest_tile, FALSE)) {
        func = diplomat_keep_moving;
        cd->add_item(QString(_("Keep moving")), func, qv1, qv2);
      }
    } else  {
      if (diplomat_can_do_action(punit, SPY_POISON, dest_tile)) {
        func = spy_poison;
        cd->add_item(QString(_("Poison City")), func, qv1, qv2);
      }
      if (diplomat_can_do_action(punit, DIPLOMAT_SABOTAGE, dest_tile)) {
        func = spy_request_sabotage_list;
        cd->add_item(QString(_("Industrial Sabotage")),
                              func, qv1, qv2);
      }
      if (diplomat_can_do_action(punit, DIPLOMAT_STEAL, dest_tile)) {
        func = spy_steal;
        cd->add_item(QString(_("Steal Technology")),
                              func, qv1, qv2);
      }
      if (diplomat_can_do_action(punit, DIPLOMAT_INCITE, dest_tile)) {
        func = diplomat_incite;
        cd->add_item(QString(_("Incite a Revolt")), func, qv1, qv2);
      }
      if (unit_can_move_to_tile(punit, dest_tile, FALSE)) {
        func = diplomat_keep_moving;
        cd->add_item(QString(_("Keep moving")), func, qv1, qv2);
      }
    }
    cd->set_layout();
    cd->show_me();
  } else {
    if ((ptunit = unit_list_get(dest_tile->units, 0))) {
      /* Spy/Diplomat acting against a unit */

      diplomat_target_id = ptunit->id;
      diplomat_id = punit->id;
      gui()->set_current_unit(diplomat_id, diplomat_target_id);
      astr_set(&text,
               /* TRANS: %s is a unit name, e.g., Diplomat, Spy */
               _("Your %s is waiting for your command."),
               unit_name_translation(punit));
      choice_dialog *cd = new choice_dialog(_("Subvert Enemy Unit"),
                                                     astr_str(&text),
                                                     gui()->game_tab_widget);
      qv2 = ptunit->id;
      qv1 = punit->id;
      if (diplomat_can_do_action(punit, DIPLOMAT_BRIBE, dest_tile)) {
        func = diplomat_bribe;
        cd->add_item(QString(_("Bribe Enemy Unit")), func, qv1, qv2);
      }
      if (diplomat_can_do_action(punit, SPY_SABOTAGE_UNIT, dest_tile)) {
        func = spy_sabotage_unit;
        cd->add_item(QString(_("Sabotage Enemy Unit")),
                              func, qv1, qv2);
      }
      func = keep_moving;
      cd->add_item(QString(_("Do nothing")), func, qv1, qv2);
      cd->set_layout();
      cd->show_me();
    }
  }
  astr_free(&title);
  astr_free(&text);
}

/***************************************************************************
  Action bribe unit for choice dialog
***************************************************************************/
static void diplomat_bribe(QVariant data1, QVariant data2)
{
  int diplomat_id = data1.toInt();
  int diplomat_target_id = data2.toInt();

  if (NULL != game_unit_by_number(diplomat_id)
      && NULL != game_unit_by_number(diplomat_target_id)) {
    request_diplomat_answer(DIPLOMAT_BRIBE, diplomat_id,
                            diplomat_target_id, 0);
  }
}

/***************************************************************************
  Action sabotage unit for choice dialog
***************************************************************************/
static void spy_sabotage_unit(QVariant data1, QVariant data2)
{
  int diplomat_id = data1.toInt();
  int diplomat_target_id = data2.toInt();

  request_diplomat_action(SPY_SABOTAGE_UNIT, diplomat_id,
                          diplomat_target_id, 0);
}

/***************************************************************************
  Action steal tech with spy for choice dialog
***************************************************************************/
static void spy_steal(QVariant data1, QVariant data2)
{
  QString str;
  QVariant qv1, qv2;
  pfcn_void func;
  int diplomat_id = data1.toInt();
  int diplomat_target_id = data2.toInt();
  struct city *pvcity = game_city_by_number(diplomat_target_id);
  struct player *pvictim = NULL;
  choice_dialog *cd;

  if (pvcity) {
    pvictim = city_owner(pvcity);
  }
  cd = gui()->get_diplo_dialog();
  if (cd != NULL) {
    cd->close();
  }
  struct astring stra = ASTRING_INIT;
  cd = new choice_dialog(_("Steal"), _("Steal Technology"),
                         gui()->game_tab_widget);
  qv1 = data1;
  struct player *pplayer = client.conn.playing;
  if (pvictim) {
    advance_index_iterate(A_FIRST, i) {
      if (player_invention_state(pvictim, i) == TECH_KNOWN &&
          (player_invention_state(pplayer, i) == TECH_UNKNOWN ||
           player_invention_state(pplayer, i) == TECH_PREREQS_KNOWN)) {
        func = spy_steal_something;
        str = advance_name_for_player(client.conn.playing, i);
        cd->add_item(str, func, qv1, i);
      }
    } advance_index_iterate_end;
    astr_set(&stra, _("At %s's Discretion"),
             unit_name_translation(game_unit_by_number(diplomat_id)));
    func = spy_steal_something;
    str = astr_str(&stra);
    cd->add_item(str, func, qv1, A_UNSET);
    cd->set_layout();
    cd->show_me();
  }
  astr_free(&stra);
}

/***************************************************************************
  Action steal given tech for choice dialog
***************************************************************************/
static void spy_steal_something(QVariant data1, QVariant data2)
{
  int diplomat_id;
  int diplomat_target_id;

  gui()->get_current_unit(&diplomat_id, &diplomat_target_id);
  if (NULL != game_unit_by_number(diplomat_id)
      && NULL != game_city_by_number(diplomat_target_id)) {
    request_diplomat_action(DIPLOMAT_STEAL, diplomat_id,
                            diplomat_target_id, data2.toInt());
  }
}

/***************************************************************************
  Action  request sabotage list for choice dialog
***************************************************************************/
static void spy_request_sabotage_list(QVariant data1, QVariant data2)
{
  int diplomat_id = data1.toInt();
  int diplomat_target_id = data2.toInt();

  if (NULL != game_unit_by_number(diplomat_id)
      && NULL != game_city_by_number(diplomat_target_id)) {
    request_diplomat_answer(DIPLOMAT_SABOTAGE, diplomat_id,
                            diplomat_target_id, 0);
  }
}

/***************************************************************************
  Action  poison city for choice dialog
***************************************************************************/
static void spy_poison(QVariant data1, QVariant data2)
{
  int diplomat_id = data1.toInt();
  int diplomat_target_id = data2.toInt();

  if (NULL != game_unit_by_number(diplomat_id)
      && NULL != game_city_by_number(diplomat_target_id)) {
    request_diplomat_action(SPY_POISON, diplomat_id, diplomat_target_id, 0);
  }
}

/***************************************************************************
  Action establish embassy for choice dialog
***************************************************************************/
static void diplomat_embassy(QVariant data1, QVariant data2)
{
  int diplomat_id = data1.toInt();
  int diplomat_target_id = data2.toInt();

  if (NULL != game_unit_by_number(diplomat_id)
      && NULL != game_city_by_number(diplomat_target_id)) {
    request_diplomat_action(DIPLOMAT_EMBASSY, diplomat_id,
                            diplomat_target_id, 0);
  }
}

/***************************************************************************
  Action investigate city for choice dialog
***************************************************************************/
static void diplomat_investigate(QVariant data1, QVariant data2)
{
  int diplomat_id = data1.toInt();
  int diplomat_target_id = data2.toInt();

  if (NULL != game_city_by_number(diplomat_target_id)
      && NULL != game_unit_by_number(diplomat_id)) {
    request_diplomat_action(DIPLOMAT_INVESTIGATE, diplomat_id,
                            diplomat_target_id, 0);
  }
}

/***************************************************************************
  Action sabotage for choice dialog
***************************************************************************/
static void diplomat_sabotage(QVariant data1, QVariant data2)
{
  int diplomat_id = data1.toInt();
  int diplomat_target_id = data2.toInt();

  if (NULL != game_unit_by_number(diplomat_id)
      && NULL != game_city_by_number(diplomat_target_id)) {
    request_diplomat_action(DIPLOMAT_SABOTAGE, diplomat_id,
                            diplomat_target_id, B_LAST + 1);
  }
}

/***************************************************************************
  Action steal with diplomat for choice dialog
***************************************************************************/
static void diplomat_steal(QVariant data1, QVariant data2)
{
  int diplomat_id = data1.toInt();
  int diplomat_target_id = data2.toInt();

  if (NULL != game_unit_by_number(diplomat_id)
      && NULL != game_city_by_number(diplomat_target_id)) {
    request_diplomat_action(DIPLOMAT_STEAL, diplomat_id,
                            diplomat_target_id, A_UNSET);
  }
}

/***************************************************************************
  Action incite revolt for choice dialog
***************************************************************************/
static void diplomat_incite(QVariant data1, QVariant data2)
{
  int diplomat_id = data1.toInt();
  int diplomat_target_id = data2.toInt();

  if (NULL != game_unit_by_number(diplomat_id)
      && NULL != game_city_by_number(diplomat_target_id)) {
    request_diplomat_answer(DIPLOMAT_INCITE, diplomat_id,
                            diplomat_target_id, 0);
  }
}

/***************************************************************************
  Action keep moving with diplomat for choice dialog
***************************************************************************/
static void diplomat_keep_moving(QVariant data1, QVariant data2)
{
  struct unit *punit;
  struct city *pcity;
  int diplomat_id = data1.toInt();
  int diplomat_target_id = data2.toInt();

  if ((punit = game_unit_by_number(diplomat_id))
      && (pcity = game_city_by_number(diplomat_target_id))
      && !same_pos(unit_tile(punit), city_tile(pcity))) {
    request_diplomat_action(DIPLOMAT_MOVE, diplomat_id,
                            diplomat_target_id, 0);
  }
}

/**************************************************************************
  Popup a window asking a diplomatic unit if it wishes to incite the
  given enemy city.
**************************************************************************/
void popup_incite_dialog(struct unit *actor, struct city *pcity, int cost)
{
  char buf[1024];
  char buf2[1024];
  int ret;
  int diplomat_id;
  int diplomat_target_id;

  fc_snprintf(buf, ARRAY_SIZE(buf), PL_("Treasury contains %d gold.",
                                        "Treasury contains %d gold.",
                                        client_player()->economic.gold),
              client_player()->economic.gold);

  if (INCITE_IMPOSSIBLE_COST == cost) {
    QMessageBox incite_impossible;

    fc_snprintf(buf2, ARRAY_SIZE(buf2),
                _("You can't incite a revolt in %s."), city_name(pcity));
    incite_impossible.setText(QString(buf2));
    incite_impossible.exec();
  } else if (cost <= client_player()->economic.gold) {
    QMessageBox ask(gui()->central_wdg);

    fc_snprintf(buf2, ARRAY_SIZE(buf2),
                PL_("Incite a revolt for %d gold?\n%s",
                    "Incite a revolt for %d gold?\n%s", cost), cost, buf);
    ask.setText(QString(buf2));
    ask.setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
    ask.setDefaultButton(QMessageBox::Cancel);
    ask.setIcon(QMessageBox::Question);
    ask.setWindowTitle(_("Incite a Revolt!"));
    ret = ask.exec();
    switch (ret) {
    case QMessageBox::Cancel:
      return;
      break;
    case QMessageBox::Ok:
      gui()->get_current_unit(&diplomat_id, &diplomat_target_id);
      request_diplomat_action(DIPLOMAT_INCITE, diplomat_id,
                              diplomat_target_id, 0);
      break;
    }
  } else {
    QMessageBox too_much;

    fc_snprintf(buf2, ARRAY_SIZE(buf2),
                PL_("Inciting a revolt costs %d gold.\n%s",
                    "Inciting a revolt costs %d gold.\n%s", cost), cost,
                buf);
    too_much.setText(QString(buf2));
    too_much.exec();
  }
}

/**************************************************************************
  Popup a dialog asking a diplomatic unit if it wishes to bribe the
  given enemy unit.
**************************************************************************/
void popup_bribe_dialog(struct unit *actor, struct unit *punit, int cost)
{
  QMessageBox ask(gui()->central_wdg);
  int ret;
  QString str;
  char buf[1024];
  char buf2[1024];
  int diplomat_id;
  int diplomat_target_id;

  gui()->get_current_unit(&diplomat_id, &diplomat_target_id);
  fc_snprintf(buf, ARRAY_SIZE(buf), PL_("Treasury contains %d gold.",
                                        "Treasury contains %d gold.",
                                        client_player()->economic.gold),
              client_player()->economic.gold);
  if (unit_has_type_flag(punit, UTYF_UNBRIBABLE)) {
    ask.setWindowTitle(_("Ooops..."));
    ask.setText(_("This unit cannot be bribed!"));
    ask.exec();
    return;
  }

  ask.setWindowTitle(QString(_("Bribe Enemy Unit")));
  if (cost <= client_player()->economic.gold) {
    fc_snprintf(buf2, ARRAY_SIZE(buf2), PL_("Bribe unit for %d gold?\n%s",
                                            "Bribe unit for %d gold?\n%s",
                                            cost), cost, buf);
    ask.setText(QString(buf2));
    ask.setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
    ask.setDefaultButton(QMessageBox::Cancel);
    ask.setIcon(QMessageBox::Question);
    ret = ask.exec();
    switch (ret) {
    case QMessageBox::Cancel:
      break;
    case QMessageBox::Ok:
      request_diplomat_action(DIPLOMAT_BRIBE, diplomat_id,
                              diplomat_target_id, 0);
      break;
    default:
      break;
    }
  } else {
    fc_snprintf(buf2, ARRAY_SIZE(buf2),
                PL_("Bribing the unit costs %d gold.\n%s",
                    "Bribing the unit costs %d gold.\n%s", cost), cost, buf);
    ask.setText(buf2);
    ask.setWindowTitle(_("Traitors Demand Too Much!"));
    ask.exec();
  }
}

/***************************************************************************
  Action pillage for choice dialog
***************************************************************************/
static void pillage_something(QVariant data1, QVariant data2)
{
  int punit_id;
  int what;
  struct unit *punit;

  what = data1.toInt();
  punit_id = data2.toInt();
  punit = game_unit_by_number(punit_id);
  struct act_tgt target;

    if (what >= S_LAST + game.control.num_base_types) {
      target.type = ATT_ROAD;
      target.obj.road = what - S_LAST - game.control.num_base_types;
    } else if (what >= S_LAST) {
      target.type = ATT_BASE;
      target.obj.base = what - S_LAST;
    } else {
      target.type = ATT_SPECIAL;
      target.obj.spe = static_cast<tile_special_type>(what);
    }

    request_new_unit_activity_targeted(punit, ACTIVITY_PILLAGE,
                                       &target);
  ::is_showing_pillage_dialog = false;
}

/***************************************************************************
  Action sabotage with spy for choice dialog
***************************************************************************/
static void spy_sabotage(QVariant data1, QVariant data2)
{
  int diplomat_id;
  int diplomat_target_id;

  gui()->get_current_unit(&diplomat_id, &diplomat_target_id);
  if (NULL != game_unit_by_number(diplomat_id)
        && NULL != game_city_by_number(diplomat_target_id)) {
      request_diplomat_action(DIPLOMAT_SABOTAGE, diplomat_id,
                              diplomat_target_id,  data2.toInt()+1);
    }
}

/**************************************************************************
  Popup a dialog asking a diplomatic unit if it wishes to sabotage the
  given enemy city.
**************************************************************************/
void popup_sabotage_dialog(struct unit *actor, struct city *pcity)
{

  QString str;
  QVariant qv1, qv2;
  int diplomat_id;
  int diplomat_target_id;
  pfcn_void func;
  choice_dialog *cd = new choice_dialog(_("Sabotage"),
                                        _("Select Improvement to Sabotage"),
                                        gui()->game_tab_widget);
  int nr = 0;
  struct astring stra = ASTRING_INIT;

  gui()->get_current_unit(&diplomat_id, &diplomat_target_id);
  qv1 = diplomat_id;
  func = spy_sabotage;
  cd->add_item(QString(_("City Production")), func, qv1, -1);
  city_built_iterate(pcity, pimprove) {
    if (pimprove->sabotage > 0) {
      func = spy_sabotage;
      str = city_improvement_name_translation(pcity, pimprove);
      qv2 = nr;
      cd->add_item(str, func, qv1, improvement_number(pimprove));
      nr++;
    }
  } city_built_iterate_end;
  astr_set(&stra, _("At %s's Discretion"),
           unit_name_translation(game_unit_by_number(diplomat_id)));
  func = spy_sabotage;
  str = astr_str(&stra);
  cd->add_item(str, func, qv1, B_LAST);
  cd->set_layout();
  cd->show_me();
  astr_free(&stra);
}

/**************************************************************************
  Popup a dialog asking the unit which improvement they would like to
  pillage.
**************************************************************************/
void popup_pillage_dialog(struct unit *punit, bv_special spe,
                          bv_bases bases, bv_roads roads)
{
  QString str;
  QVariant qv1, qv2;
  pfcn_void func;
  struct act_tgt tgt;
  choice_dialog *cd;

  if (is_showing_pillage_dialog){
    return;
  }
  cd = new choice_dialog(_("What To Pillage"), _("Select what to pillage:"),
                         gui()->game_tab_widget);
  qv2 = punit->id;
    while (get_preferred_pillage(&tgt, spe, bases, roads)) {
      int what = S_LAST;
      bv_special what_spe;
      bv_bases what_base;
      bv_roads what_road;

      BV_CLR_ALL(what_spe);
      BV_CLR_ALL(what_base);
      BV_CLR_ALL(what_road);

      switch (tgt.type) {
        case ATT_SPECIAL:
          BV_SET(what_spe, tgt.obj.spe);
          what = tgt.obj.spe;
          clear_special(&spe, tgt.obj.spe);
          break;
        case ATT_BASE:
          BV_SET(what_base, tgt.obj.base);
          what = tgt.obj.base + S_LAST;
          BV_CLR(bases, tgt.obj.base);
          break;
        case ATT_ROAD:
          BV_SET(what_road, tgt.obj.road);
          what = tgt.obj.road + S_LAST + game.control.num_base_types;
          BV_CLR(roads, tgt.obj.road);
          break;
      }
      func = pillage_something;
      str = get_infrastructure_text(what_spe, what_base, what_road);
      qv1 = what;
      cd->add_item(str, func, qv1,qv2);
    }
  cd->set_layout();
  cd->show_me();
}

/***************************************************************************
  Disband Message box contructor
***************************************************************************/
disband_box::disband_box(struct unit_list *punits,
                         QWidget *parent) : QMessageBox(parent)
{
  QString str;
  QPushButton *pb;

  setAttribute(Qt::WA_DeleteOnClose);
  setModal(false);
  cpunits = unit_list_new();
  unit_list_iterate(punits, punit) {
    unit_list_append(cpunits, punit);
  } unit_list_iterate_end;

  str = QString(PL_("Are you sure you want to disband that %1 unit?",
                  "Are you sure you want to disband those %1 units?",
                  unit_list_size(punits))).arg(unit_list_size(punits));
  setText(str);
  pb = addButton(_("Yes"), QMessageBox::AcceptRole);
  addButton(_("No"), QMessageBox::RejectRole);
  setDefaultButton(QMessageBox::Cancel);
  setIcon(QMessageBox::Question);
  setWindowTitle(_("Disband units"));
  connect(pb, SIGNAL(clicked()), this, SLOT(disband_clicked()));
}

/***************************************************************************
  Clicked Yes in disband box
***************************************************************************/
void disband_box::disband_clicked()
{
  unit_list_iterate(cpunits, punit) {
    if (!unit_has_type_flag(punit, UTYF_UNDISBANDABLE)) {
      request_unit_disband(punit);
    }
  } unit_list_iterate_end;
}

/***************************************************************************
  Destructor for disband box
***************************************************************************/
disband_box::~disband_box()
{
  unit_list_destroy(cpunits);
}

/****************************************************************************
  Pops up a dialog to confirm disband of the unit(s).
****************************************************************************/
void popup_disband_dialog(struct unit_list *punits)
{
  disband_box *ask = new disband_box(punits, gui()->central_wdg);
  ask->show();
}

/**************************************************************************
  Ruleset (modpack) has suggested loading certain tileset. Confirm from
  user and load.
**************************************************************************/
void popup_tileset_suggestion_dialog(void)
{
  qDebug() << Q_FUNC_INFO << "PORTME";
}

/****************************************************************
  Ruleset (modpack) has suggested loading certain soundset. Confirm from
  user and load.
*****************************************************************/
void popup_soundset_suggestion_dialog(void)
{
  qDebug() << Q_FUNC_INFO << "PORTME";
}

/**************************************************************************
  Tileset (modpack) has suggested loading certain theme. Confirm from
  user and load.
**************************************************************************/
bool popup_theme_suggestion_dialog(const char *theme_name)
{
  /* PORTME */
  return false;
}

/**************************************************************************
  This function is called when the client disconnects or the game is
  over.  It should close all dialog windows for that game.
**************************************************************************/
void popdown_all_game_dialogs(void)
{
  int i;
  QList <choice_dialog *> cd_list;
  QList <notify_dialog *> nd_list;

  cd_list = gui()->game_tab_widget->findChildren <choice_dialog *>();
  for (i = 0; i < cd_list.count(); i++) {
      cd_list[i]->close();
  }
  nd_list = gui()->game_tab_widget->findChildren <notify_dialog *>();
  for (i = 0; i < nd_list.count(); i++) {
      nd_list[i]->close();
  }

  popdown_help_dialog();
  popdown_players_report();
  popdown_all_spaceships_dialogs();
  popdown_economy_report();
  popdown_units_report();
  popdown_science_report();
  popdown_city_report();
  popdown_endgame_report();
  gui()->popdown_unit_sel();
}

/****************************************************************
  Returns id of a diplomat currently handled in diplomat dialog
*****************************************************************/
int diplomat_handled_in_diplomat_dialog(void)
{
  choice_dialog *cd = gui()->get_diplo_dialog();

  if (cd != NULL){
    return cd->unit_id;
  } else {
    return -1;
  }
}

/****************************************************************
  Closes the diplomat dialog
****************************************************************/
void close_diplomat_dialog(void)
{
  choice_dialog *cd;

  cd = gui()->get_diplo_dialog();
  if (cd != NULL){
    cd->close();
  }
  gui()->set_current_unit(-1, -1);
}

/****************************************************************
  Updates caravan dialog
****************************************************************/
void caravan_dialog_update(void)
{
  char buf2[1024];
  struct city *destcity;
  struct unit *caravan;
  QString wonder;
  QString str;
  QVariant qv1, qv2;
  pfcn_void func;
  bool can_wonder;
  bool wonder_button_not_found;
  int i;
  int kmbn;
  QVBoxLayout *layout;
  QPushButton *qpb;

  if (caravan_dialog == NULL) {
    return;
  }

  destcity = game_city_by_number(caravan_city_id);
  caravan = game_unit_by_number(caravan_unit_id);
  can_wonder = destcity && caravan
               && unit_can_help_build_wonder(caravan, destcity);

  wonder_button_not_found = TRUE;
  i = 0;
  kmbn = -1;
  layout = caravan_dialog->get_layout();
  foreach (func, caravan_dialog->func_list) {
    if (func == caravan_help_build) {
      wonder_button_not_found = FALSE;
      if (can_wonder) {
        fc_snprintf(buf2, sizeof(buf2),
                  _("Help build Wonder (%d remaining)"),
                  impr_build_shield_cost(destcity->production.value.building)
                  - destcity->shield_stock);
        wonder = QString(buf2);
      } else {
        wonder = QString(_("Help build Wonder"));
      }

      qpb = qobject_cast<QPushButton *>(layout->itemAt(i + 1)->widget());
      qpb->setText(wonder);
      qpb->setEnabled(can_wonder);
    } else if (func == caravan_keep_moving) {
      /* Store the number of the Keep moving button for later insert. */
      kmbn = i;
    }
    i++;
  }

  if (can_wonder && wonder_button_not_found) {
    QString title;

    if (0 <= kmbn) {
      /* Temporary remove the Keep moving button so it won't end up above
       * the Help build Wonder button. */
      caravan_dialog->stack_button(kmbn);
    }

    title = QString(_("Help build Wonder (%1 remaining)")).arg(
          impr_build_shield_cost(destcity->production.value.building)
          - destcity->shield_stock);
    caravan_dialog->add_item(title, caravan_help_build,
                             destcity->id, caravan->id);

    if (0 <= kmbn) {
      /* Reinsert the "Keep moving" button below the
       * Help build Wonder button. */
      caravan_dialog->unstack_all_buttons();
    }
  }
}

/****************************************************************
  Player has gained a new tech.
*****************************************************************/
void show_tech_gained_dialog(Tech_type_id tech)
{
}

/****************************************************************
  Show tileset error dialog.
*****************************************************************/
void show_tileset_error(const char *msg)
{
  QMessageBox ask(gui()->central_wdg);
  char buf[1024];

  fc_snprintf(buf, sizeof(buf),
              _("Tileset problem, it's probably incompatible with the"
                " ruleset:\n%s\nProgram will now exit."), msg);
  ask.setText(buf);
  ask.setStandardButtons(QMessageBox::Ok);
  ask.setWindowTitle(_("Tileset error"));
  ask.exec();
  gui()->quit();

}

/****************************************************************
  Popup dialog for upgrade units
*****************************************************************/
void popup_upgrade_dialog(struct unit_list *punits)
{
  char buf[512];
  QMessageBox ask(gui()->central_wdg);
  int ret;

  if (!punits || unit_list_size(punits) == 0) {
    return;
  }
  if (!get_units_upgrade_info(buf, sizeof(buf), punits)) {
    ask.setWindowTitle(_("Upgrade Unit!"));
    ask.setStandardButtons(QMessageBox::Ok);
    ask.setIcon(QMessageBox::Information);
  } else {
    ask.setWindowTitle(_("Upgrade Obsolete Units"));
    ask.setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
    ask.setIcon(QMessageBox::Question);
    ask.setDefaultButton(QMessageBox::Cancel);
  }
  ask.setText(QString(buf));
  ret = ask.exec();

  switch (ret) {
  case QMessageBox::Cancel:
    return;
    break;
  case QMessageBox::Ok:
    unit_list_iterate(punits, punit) {
      request_unit_upgrade(punit);
    }
    unit_list_iterate_end;
    break;
  }
}

/****************************************************************
  Contructor for unit_select
*****************************************************************/
unit_select::unit_select(tile *ptile, QWidget *parent)
{
  QPoint p, final_p;

  setParent(parent);
  utile = ptile;
  pix = NULL;
  show_line = 0;
  highligh_num = -1;
  ufont = new QFont;
  ufont->setItalic(true);
  info_font = gui()->fc_fonts.get_font("gui_qt_font_notify_label");
  update_units();
  h_pix = NULL;
  create_pixmap();
  p = mapFromGlobal(QCursor::pos());
  cw = new close_widget(this);
  setMouseTracking(true);
  final_p.setX(p.x());
  final_p.setY(p.y());
  if (p.x() + width() > parentWidget()->width()) {
    final_p.setX(parentWidget()->width() - width());
  }
  if (p.y() - height() < 0) {
    final_p.setY(height());
  }
  move(final_p.x(), final_p.y() - height());
  setFocus();
}
/****************************************************************
  Destructor for unit select
*****************************************************************/
unit_select::~unit_select()
{
    delete h_pix;
    delete pix;
    delete ufont;
    delete cw;
}

/****************************************************************
  Create pixmap of whole widget except borders (pix)
*****************************************************************/
void unit_select::create_pixmap()
{
  struct unit *punit;
  QPixmap *tmp_pix;
  struct canvas *unit_pixmap;
  QList <QPixmap*>pix_list;
  QPainter p;
  QString str;
  int x, y, i;
  int rate, f;
  int a;
  QPen pen;
  QFontMetrics fm(*info_font);

  if (pix != NULL) {
    delete pix;
    pix = NULL;
  };

  update_units();
  if (unit_list.count() > 0) {
  punit = unit_list.at(0);
  item_size.setWidth(tileset_full_tile_width(tileset));
  item_size.setHeight(tileset_tile_height(tileset) * 3 / 2);
  more = false;
  if (h_pix == NULL) {
    h_pix = new QPixmap(item_size.width(), item_size.height());
    h_pix->fill(QColor(100, 100, 100, 140));
  }
  if (unit_list.size() < 5) {
    row_count = 1;
    pix = new QPixmap((unit_list.size()) * item_size.width(),
                      item_size.height());
  } else if (unit_list.size() < 9) {
    row_count = 2;
    pix = new QPixmap(4 * item_size.width(), 2 * item_size.height());
  } else {
    row_count = 3;
    if (unit_list_size(utile->units) > unit_list.size() - 1) {
      more = true;
    }
    pix = new QPixmap(4 * item_size.width(), 3 * item_size.height());
  }
  pix->fill(Qt::transparent);
  foreach(punit, unit_list) {
    unit_pixmap = qtg_canvas_create(tileset_full_tile_width(tileset),
                                    tileset_tile_height(tileset) * 3 / 2);
    unit_pixmap->map_pixmap.fill(Qt::transparent);
    put_unit(punit, unit_pixmap, 0, 0);
    pix_list.push_back(&unit_pixmap->map_pixmap);
  }
  a = qMin(item_size.width() / 4, 12);
  x = 0, y = -item_size.height(), i = -1;
  p.begin(pix);
  ufont->setPixelSize(a);
  p.setFont(*ufont);
  pen.setColor(QColor(232, 255, 0));
  p.setPen(pen);

  while (!pix_list.isEmpty()) {
    tmp_pix = pix_list.takeFirst();
    i++;
    if (i % 4 == 0) {
      x = 0;
      y = y + item_size.height();
    }
    punit = unit_list.at(i);
    Q_ASSERT(punit != NULL);
    rate = unit_type(punit)->move_rate;
    f = ((punit->fuel) - 1);
    if (i == highligh_num) {
      p.drawPixmap(x, y, *h_pix);
      p.drawPixmap(x, y, *tmp_pix);
    } else {
      p.drawPixmap(x, y, *tmp_pix);
    }

    if (utype_fuel(unit_type(punit))) {
      str = QString(move_points_text((rate * f) + punit->moves_left, false));
    } else {
      str = QString(move_points_text(punit->moves_left, false));
    }
    /* TRANS: MP = Movement points */
    str = QString(_("MP:")) + str;
    p.drawText(x, y + item_size.height() - 4, str);

    x = x + item_size.width();
    delete tmp_pix;
  }
  p.end();
  setFixedWidth(pix->width() + 20);
  setFixedHeight(pix->height() + 2 * (fm.height() + 6));
  qDeleteAll(pix_list.begin(), pix_list.end());
  }
}

/****************************************************************
  Event for mouse moving around unit_select
*****************************************************************/
void unit_select::mouseMoveEvent(QMouseEvent *event)
{
  int a, b;
  int old_h;
  QFontMetrics fm(*info_font);

  old_h = highligh_num;
  highligh_num = -1;
  if (event->x() > width() - 11
      || event->y() > height() - fm.height() - 5
      || event->y() < fm.height() + 3 || event->x() < 11) {
    /** do nothing if mouse is on border, just skip next if */
  } else if (row_count > 0) {
    a = (event->x() - 10) / item_size.width();
    b = (event->y() - fm.height() - 3) / item_size.height();
    highligh_num = b * 4 + a;
  }
  if (old_h != highligh_num) {
    create_pixmap();
    update();
  }
}

/****************************************************************
  Mouse pressed event for unit_select.
  Left Button - chooses units
  Right Button - closes widget
*****************************************************************/
void unit_select::mousePressEvent(QMouseEvent *event)
{
  struct unit *punit;
  if (event->button() == Qt::RightButton) {
    was_destroyed = true;
    close();
    destroy();
  }
  if (event->button() == Qt::LeftButton && highligh_num != -1) {
    update_units();
    if (highligh_num >= unit_list.count()) {
      return;
    }
    punit = unit_list.at(highligh_num);
    unit_focus_set(punit);
    was_destroyed = true;
    close();
    destroy();
  }
}

/****************************************************************
  Redirected paint event
*****************************************************************/
void unit_select::paint(QPainter *painter, QPaintEvent *event)
{
  QFontMetrics fm(*info_font);
  int h, i;
  int *f_size;
  QPen pen;
  QString str, str2;
  struct unit *punit;
  int point_size = info_font->pointSize();
  int pixel_size = info_font->pixelSize();

  if (point_size < 0) {
    f_size = &pixel_size;
  } else {
    f_size = &point_size;
  }
  if (highligh_num != -1 && highligh_num < unit_list.count()) {
    punit = unit_list.at(highligh_num);
    /* TRANS: HP - hit points */
    str2 = QString(_("%1 HP:%2/%3")).arg(QString(unit_activity_text(punit)),
                                      QString::number(punit->hp),
                                      QString::number(unit_type(punit)->hp));
  }
  str = QString(PL_("%1 unit", "%1 units",
                    unit_list_size(utile->units)))
                .arg(unit_list_size(utile->units));
  for (i = *f_size; i > 4; i--) {
    if (point_size < 0) {
      info_font->setPixelSize(i);
    } else  {
      info_font->setPointSize(i);
    }
    QFontMetrics qfm(*info_font);
    if (10 + qfm.width(str2) < width()) {
      break;
    }
  }
  h = fm.height();
  painter->setBrush(QColor(0, 0, 0, 135));
  painter->drawRect(0, 0, width(), height());
  if (pix != NULL) {
    painter->drawPixmap(10, h + 3, *pix);
    pen.setColor(QColor(232, 255, 0));
    painter->setPen(pen);
    painter->setFont(*info_font);
    painter->drawText(10, h, str);
    if (highligh_num != -1 && highligh_num < unit_list.count()) {
      painter->drawText(10, height() - 5, str2);
    }
  }
  if (point_size < 0) {
    info_font->setPixelSize(*f_size);
  } else {
    info_font->setPointSize(*f_size);
  }
  cw->put_to_corner();
}
/****************************************************************
  Paint event, redirects to paint(...)
*****************************************************************/
void unit_select::paintEvent(QPaintEvent *event)
{
  QPainter painter;

  painter.begin(this);
  paint(&painter, event);
  painter.end();
}

/****************************************************************
  Function from abstract fcwidget to update menu, its not needed
  cause widget is easy closable via right mouse click
*****************************************************************/
void unit_select::update_menu()
{
  was_destroyed = true;
  close();
  destroy();
}

/****************************************************************
  Updates unit list on tile
*****************************************************************/
void unit_select::update_units()
{
  int i = 1;
  struct unit_list *punit_list;

  if (utile == NULL) {
    struct unit *punit = head_of_units_in_focus();
    if (punit) {
      utile = unit_tile(punit);
    }
  }
  unit_list.clear();
  if (utile != nullptr) {
    punit_list = utile->units;
    if (punit_list != nullptr) {
      unit_list_iterate(utile->units, punit) {
        if (i > show_line * 4)
          unit_list.push_back(punit);
        i++;
      } unit_list_iterate_end;
    }
  }
  if (unit_list.count() == 0) {
    close();
  }
}

/****************************************************************
  Close event for unit_select, restores focus to map
*****************************************************************/
void unit_select::closeEvent(QCloseEvent* event)
{
  gui()->mapview_wdg->setFocus();
  QWidget::closeEvent(event);
}

/****************************************************************
  Mouse wheel event for unit_select
*****************************************************************/
void unit_select::wheelEvent(QWheelEvent *event)
{
  int nr;

  if (more == false && utile == NULL) {
    return;
  }
  nr = qCeil(static_cast<qreal>(unit_list_size(utile->units)) / 4) - 3;
  if (event->delta() < 0) {
    show_line++;
    show_line = qMin(show_line, nr);
  } else {
    show_line--;
    show_line = qMax(0, show_line);
  }
  update_units();
  create_pixmap();
  update();
  event->accept();
}

/****************************************************************
  Keyboard handler for unit_select
*****************************************************************/
void unit_select::keyPressEvent(QKeyEvent *event)
{
  if (event->key() == Qt::Key_Escape) {
    was_destroyed = true;
    close();
    destroy();
  }
  QWidget::keyPressEvent(event);
}

/***************************************************************************
 Set current unit handled in diplo dialog
***************************************************************************/
void fc_client::set_current_unit(int curr, int target)
{
  current_unit_id = curr;
  current_unit_target_id  = target;
}

/***************************************************************************
 Get current unit handled in diplo dialog
***************************************************************************/
void fc_client::get_current_unit(int *curr, int *target)
{
  *curr = current_unit_id;
  *target = current_unit_target_id;
}

/***************************************************************************
 Set current diplo dialog
***************************************************************************/
void fc_client::set_diplo_dialog(choice_dialog *widget)
{
  opened_dialog = widget;
}

/***************************************************************************
 Get current diplo dialog
***************************************************************************/
choice_dialog* fc_client::get_diplo_dialog()
{
  return opened_dialog;
}

