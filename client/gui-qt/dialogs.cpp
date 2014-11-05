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
#include "actions.h"
#include "city.h"
#include "game.h"
#include "government.h"
#include "improvement.h"
#include "movement.h"
#include "nation.h"
#include "research.h"

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

/* Locations for non action enabler controlled buttons. */
#define BUTTON_MOVE ACTION_MOVE
#define BUTTON_CANCEL BUTTON_MOVE + 1
#define BUTTON_TRADE_ROUTE BUTTON_MOVE + 2
#define BUTTON_MARKET_PLACE BUTTON_MOVE + 3
#define BUTTON_HELP_WONDER BUTTON_MOVE + 4
#define BUTTON_COUNT BUTTON_MOVE + 5

static void diplomat_keep_moving(QVariant data1, QVariant data2);
static void diplomat_incite(QVariant data1, QVariant data2);
static void spy_request_sabotage_list(QVariant data1, QVariant data2);
static void spy_sabotage(QVariant data1, QVariant data2);
static void spy_steal(QVariant data1, QVariant data2);
static void spy_steal_something(QVariant data1, QVariant data2);
static void diplomat_steal(QVariant data1, QVariant data2);
static void spy_poison(QVariant data1, QVariant data2);
static void spy_steal_gold(QVariant data1, QVariant data2);
static void diplomat_embassy(QVariant data1, QVariant data2);
static void spy_sabotage_unit(QVariant data1, QVariant data2);
static void diplomat_investigate(QVariant data1, QVariant data2);
static void diplomat_sabotage(QVariant data1, QVariant data2);
static void diplomat_bribe(QVariant data1, QVariant data2);
static void caravan_marketplace(QVariant data1, QVariant data2);
static void caravan_establish_trade(QVariant data1, QVariant data2);
static void caravan_help_build(QVariant data1, QVariant data2);
static void keep_moving(QVariant data1, QVariant data2);
static void pillage_something(QVariant data1, QVariant data2);
static void action_entry(choice_dialog *cd,
                         gen_action act,
                         const action_probability *action_probabilities,
                         QVariant data1, QVariant data2);


static bool is_showing_pillage_dialog = false;
static races_dialog* race_dialog;
static bool is_race_dialog_open = false;
static bool is_more_user_input_needed = FALSE;

/**********************************************************************
  Initialize a mapping between an action and the function to call if
  the action's button is pushed.
**********************************************************************/
static const QHash<enum gen_action, pfcn_void> af_map_init(void)
{
  QHash<enum gen_action, pfcn_void> action_function;

  /* Unit acting against a city target. */
  action_function[ACTION_ESTABLISH_EMBASSY] = diplomat_embassy;
  action_function[ACTION_SPY_INVESTIGATE_CITY] = diplomat_investigate;
  action_function[ACTION_SPY_POISON] = spy_poison;
  action_function[ACTION_SPY_STEAL_GOLD] = spy_steal_gold;
  action_function[ACTION_SPY_SABOTAGE_CITY] = diplomat_sabotage;
  action_function[ACTION_SPY_TARGETED_SABOTAGE_CITY] =
      spy_request_sabotage_list;
  action_function[ACTION_SPY_STEAL_TECH] = diplomat_steal;
  action_function[ACTION_SPY_TARGETED_STEAL_TECH] = spy_steal;
  action_function[ACTION_SPY_INCITE_CITY] = diplomat_incite;

  /* Unit acting against a unit target. */
  action_function[ACTION_SPY_BRIBE_UNIT] = diplomat_bribe;
  action_function[ACTION_SPY_SABOTAGE_UNIT] = spy_sabotage_unit;

  return action_function;
}

/* Mapping from an action to the function to call when its button is
 * pushed. */
static const QHash<enum gen_action, pfcn_void> af_map = af_map_init();

/***************************************************************************
 Constructor for selecting nations
***************************************************************************/
races_dialog::races_dialog(struct player *pplayer, 
                           QWidget *parent) : QDialog(parent)
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
  selected_nation_tabs = new QTableWidget;
  nation_tabs = new QTableWidget();
  styles = new QTableWidget;
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
  styles->setRowCount(0);
  styles->setColumnCount(2);
  styles->setSelectionMode(QAbstractItemView::SingleSelection);
  styles->verticalHeader()->setVisible(false);
  styles->horizontalHeader()->setVisible(false);
  styles->setProperty("showGrid", "false");
  styles->setProperty("selectionBehavior", "SelectRows");
  styles->setEditTriggers(QAbstractItemView::NoEditTriggers);
  styles->setShowGrid(false);

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
   * Fill styles, no need to update them later
   */

  styles_iterate(pstyle) {
    i = basic_city_style_for_style(pstyle);

    if (i >= 0) {
      item = new QTableWidgetItem;
      styles->insertRow(i);
      pix = get_sample_city_sprite(tileset, i)->pm;
      item->setData(Qt::DecorationRole, *pix);
      item->setData(Qt::UserRole, style_number(pstyle));
      size.setWidth(pix->width());
      size.setHeight(pix->height());
      item->setSizeHint(size);
      styles->setItem(i, 0, item);
      item = new QTableWidgetItem;
      item->setText(style_name_translation(pstyle));
      styles->setItem(i, 1, item);
    }
  } styles_iterate_end;

  header = styles->horizontalHeader();
  header->setSectionResizeMode(QHeaderView::Stretch);
  header->resizeSections(QHeaderView::ResizeToContents);
  header = styles->verticalHeader();
  header->resizeSections(QHeaderView::ResizeToContents);
  nation_sets_iterate(pset) {
    qnation_set->addItem(nation_set_name_translation(pset),
                         nation_set_rule_name(pset));
  } nation_sets_iterate_end;
  /* create nation sets */
  refresh();

  connect(styles->selectionModel(),
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
    main_layout->addWidget(nation_tabs, 1, 0, 6, 2);
  } else {
    main_layout->addWidget(nation_tabs, 0, 0, 6, 2);
  }
  main_layout->addWidget(styles, 2, 3, 4, 1);
  main_layout->addWidget(description, 6, 0, 2, 4);
  main_layout->addWidget(selected_nation_tabs, 0, 2, 6, 1);

  setLayout(main_layout);
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
    if (is_nation_group_hidden(group)) {
      continue;
    }
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
  nation_leader_list_iterate(nation_leaders(nation_by_number
                                            (selected_nation)), pleader) {
    str = QString::fromUtf8(nation_leader_name(pleader));
    leader_name->addItem(str, nation_leader_is_male(pleader));
  } nation_leader_list_iterate_end;

  /**
   * select style for nation
   */

  style = style_number(style_of_nation(nation_by_number(selected_nation)));
  qvar = qvar.fromValue<int>(style);
  item = new QTableWidgetItem;

  for (ind = 0; ind < styles->rowCount(); ind++) {
    item = styles->item(ind, 0);

    if (item->data(Qt::UserRole) == qvar) {
      styles->selectRow(ind);
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
    output_window_append(ftc_client, _("You must select your style."));
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
  delete this;
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

  qcaption = QString(caption);
  qheadline = QString(headline);
  qlines = QString(lines);
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
  qDebug() << Q_FUNC_INFO << "PORTME";
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
  if(ptile != NULL && unit_list_size(ptile->units) > 1){
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
Choice_dialog_button::Choice_dialog_button(const QString title,
                                           pfcn_void func,
                                           QVariant data1,
                                           QVariant data2)
  : QPushButton(title)
{
  this->func = func;
  this->data1 = data1;
  this->data2 = data2;
}

/**************************************************************************
  Get the function to call when the button is pressed.
**************************************************************************/
pfcn_void Choice_dialog_button::getFunc()
{
  return func;
}

/**************************************************************************
  Get the first piece of data to feed the function when the button is
  pressed.
**************************************************************************/
QVariant Choice_dialog_button::getData1()
{
  return data1;
}

/**************************************************************************
  Get the second piece of data to feed the function when the button is
  pressed.
**************************************************************************/
QVariant Choice_dialog_button::getData2()
{
  return data2;
}

/***************************************************************************
  Constructor for choice_dialog
***************************************************************************/
choice_dialog::choice_dialog(const QString title, const QString text,
                             QWidget *parent,
                             void (*run_on_close)(void)): QWidget(parent)
{
  QLabel *l = new QLabel(text);

  signal_mapper = new QSignalMapper(this);
  layout = new QVBoxLayout(this);
  this->run_on_close = run_on_close;

  layout->addWidget(l);
  setWindowFlags(Qt::Dialog);
  setWindowTitle(title);
  setAttribute(Qt::WA_DeleteOnClose);
  gui()->set_diplo_dialog(this);

  unit_id = IDENTITY_NUMBER_ZERO;
  target_id[ATK_CITY] = IDENTITY_NUMBER_ZERO;
  target_id[ATK_UNIT] = IDENTITY_NUMBER_ZERO;

  /* No buttons are added yet. */
  for (int i = 0; i < BUTTON_COUNT; i++) {
    action_button_map << NULL;
  }
}

/***************************************************************************
  Destructor for choice dialog
***************************************************************************/
choice_dialog::~choice_dialog()
{
  buttons_list.clear();
  action_button_map.clear();
  delete signal_mapper;
  gui()->set_diplo_dialog(NULL);

  if (run_on_close) {
    run_on_close();
    run_on_close = NULL;
  }
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
                             QVariant data2, QString tool_tip = "",
                             const int button_id = -1)
{
   Choice_dialog_button *button = new Choice_dialog_button(title, func,
                                                           data1, data2);
   connect(button, SIGNAL(clicked()), signal_mapper, SLOT(map()));
   signal_mapper->setMapping(button, buttons_list.count());
   buttons_list.append(button);

   if (!tool_tip.isEmpty()) {
     button->setToolTip(tool_tip);
   }

   if (0 <= button_id) {
     /* The id is valid. */
     action_button_map[button_id] = button;
   }

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

/**************************************************************************
  Get the button with the given identity.
**************************************************************************/
Choice_dialog_button *choice_dialog::get_identified_button(const int id)
{
  if (id < 0) {
    fc_assert_msg(0 <= id, "Invalid button ID.");
    return NULL;
  }

  return action_button_map[id];
}

/***************************************************************************
  Run chosen action and close dialog
***************************************************************************/
void choice_dialog::execute_action(const int action)
{
  Choice_dialog_button *button = buttons_list.at(action);
  pfcn_void func = button->getFunc();

  func(button->getData1(), button->getData2());
  close();
}

/**************************************************************************
  Put the button in the stack and temporarily remove it. When
  unstack_all_buttons() is called all buttons in the stack will be added
  to the end of the dialog.

  Can be used to place a button below existing buttons or below buttons
  added while it was in the stack.
**************************************************************************/
void choice_dialog::stack_button(Choice_dialog_button *button)
{
  /* Store the data in the stack. */
  last_buttons_stack.append(button);

  /* Temporary remove the button so it will end up below buttons added
   * before unstack_all_buttons() is called. */
  layout->removeWidget(button);

  /* The old mappings may not be valid after reinsertion. */
  signal_mapper->removeMappings(button);

  /* Synchronize the list with the layout. */
  buttons_list.removeAll(button);
}

/**************************************************************************
  Put all the buttons in the stack back to the dialog. They will appear
  after any other buttons. See stack_button()
**************************************************************************/
void choice_dialog::unstack_all_buttons()
{
  while (!last_buttons_stack.isEmpty()) {
    Choice_dialog_button *data = last_buttons_stack.takeLast();

    /* Restore mapping. */
    signal_mapper->setMapping(data, buttons_list.count());

    /* Reinsert the button below the other buttons. */
    buttons_list.append(data);
    layout->addWidget(data);
  }
}

/***************************************************************************
  Action enter market place for choice dialog
***************************************************************************/
static void caravan_marketplace(QVariant data1, QVariant data2)
{
  dsend_packet_unit_establish_trade(&client.conn,
                                    data1.toInt(), data2.toInt(), FALSE);
  process_caravan_arrival(NULL);
}

/***************************************************************************
  Action establish trade for choice dialog
***************************************************************************/
static void caravan_establish_trade(QVariant data1, QVariant data2)
{
  dsend_packet_unit_establish_trade(&client.conn,
                                    data1.toInt(), data2.toInt(), TRUE);
  process_caravan_arrival(NULL);
}

/***************************************************************************
  Action help build wonder for choice dialog
***************************************************************************/
static void caravan_help_build(QVariant data1, QVariant data2)
{
  dsend_packet_unit_help_build_wonder(&client.conn,
                                      data1.toInt(), data2.toInt());

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
  Move the queue of diplomats that need user input forward unless the
  current diplomat will need more input.
**************************************************************************/
static void diplomat_queue_handle_primary(void)
{
  if (!is_more_user_input_needed) {
    choose_action_queue_next();
  }
}

/**************************************************************************
  Move the queue of diplomats that need user input forward since the
  current diplomat got the extra input that was required.
**************************************************************************/
static void diplomat_queue_handle_secondary(void)
{
  /* Stop waiting. Move on to the next queued diplomat. */
  is_more_user_input_needed = FALSE;
  diplomat_queue_handle_primary();
}

/**************************************************************************
  Returns the label for the help build wonder button when it is possible
  to help build it.
***************************************************************************/
static QString label_help_wonder_rem(struct city *target_city)
{
  QString label = QString(_("Help build Wonder (%1 remaining)")).arg(
      impr_build_shield_cost(target_city->production.value.building)
      - target_city->shield_stock);

  return label;
}

/**************************************************************************
  Popup a dialog that allows the player to select what action a unit
  should take.
**************************************************************************/
void popup_action_selection(struct unit *actor_unit,
                            struct city *target_city,
                            struct unit *target_unit,
                            struct tile *target_tile,
                            const action_probability *act_probs)
{
  struct astring title = ASTRING_INIT, text = ASTRING_INIT;
  QVariant qv1, qv2;
  pfcn_void func;
  struct city *actor_homecity;

  bool can_marketplace;
  bool can_traderoute;
  bool can_wonder;

  /* Could be caused by the server failing to reply to a request for more
   * information or a bug in the client code. */
  fc_assert_msg(!is_more_user_input_needed,
                "Diplomat queue problem. Is another diplomat window open?");

  /* No extra input is required as no action has been chosen yet. */
  is_more_user_input_needed = FALSE;

  actor_homecity = game_city_by_number(actor_unit->homecity);

  can_marketplace = unit_has_type_flag(actor_unit,
                                       UTYF_TRADE_ROUTE)
      && target_city
      && can_cities_trade(actor_homecity, target_city);
  can_traderoute = can_marketplace
                  && can_establish_trade_route(actor_homecity,
                                               target_city);
  can_wonder = unit_has_type_flag(actor_unit, UTYF_HELP_WONDER)
      && target_city
      && unit_can_help_build_wonder(actor_unit, target_city);

  astr_set(&title,
           /* TRANS: %s is a unit name, e.g., Spy */
           _("Choose Your %s's Strategy"),
           unit_name_translation(actor_unit));

  if (target_city && actor_homecity) {
    astr_set(&text,
             _("Your %s from %s reaches the city of %s.\nWhat now?"),
             unit_name_translation(actor_unit),
             city_name(actor_homecity),
             city_name(target_city));
  } else if (target_city) {
    astr_set(&text,
             _("Your %s has arrived at %s.\nWhat is your command?"),
             unit_name_translation(actor_unit),
             city_name(target_city));
  } else if (target_unit) {
    astr_set(&text,
             /* TRANS: Your Spy is ready to act against Roman Freight. */
             _("Your %s is ready to act against %s %s."),
             unit_name_translation(actor_unit),
             nation_adjective_for_player(unit_owner(target_unit)),
             unit_name_translation(target_unit));
  } else {
    fc_assert_msg(target_unit || target_city,
                  "No target unit or target city specified.");

    astr_set(&text,
             /* TRANS: %s is a unit name, e.g., Diplomat, Spy */
             _("Your %s is waiting for your command."),
             unit_name_translation(actor_unit));
  }

  choice_dialog *cd = new choice_dialog(astr_str(&title),
                                        astr_str(&text),
                                        gui()->game_tab_widget,
                                        diplomat_queue_handle_primary);
  qv1 = actor_unit->id;

  cd->unit_id = actor_unit->id;

  if (target_city) {
    cd->target_id[ATK_CITY] = target_city->id;
  } else {
    cd->target_id[ATK_CITY] = IDENTITY_NUMBER_ZERO;
  }

  if (target_unit) {
    cd->target_id[ATK_UNIT] = target_unit->id;
  } else {
    cd->target_id[ATK_UNIT] = IDENTITY_NUMBER_ZERO;
  }

  /* Spy/Diplomat acting against a city */

  /* Set the correct target for the following actions. */
  qv2 = cd->target_id[ATK_CITY];

  action_entry(cd,
               ACTION_ESTABLISH_EMBASSY,
               act_probs,
               qv1, qv2);

  action_entry(cd,
               ACTION_SPY_INVESTIGATE_CITY,
               act_probs,
               qv1, qv2);

  action_entry(cd,
               ACTION_SPY_POISON,
               act_probs,
               qv1, qv2);

  action_entry(cd,
               ACTION_SPY_STEAL_GOLD,
               act_probs,
               qv1, qv2);

  action_entry(cd,
               ACTION_SPY_SABOTAGE_CITY,
               act_probs,
               qv1, qv2);

  action_entry(cd,
               ACTION_SPY_TARGETED_SABOTAGE_CITY,
               act_probs,
               qv1, qv2);

  action_entry(cd,
               ACTION_SPY_STEAL_TECH,
               act_probs,
               qv1, qv2);

  action_entry(cd,
               ACTION_SPY_TARGETED_STEAL_TECH,
               act_probs,
               qv1, qv2);

  action_entry(cd,
               ACTION_SPY_INCITE_CITY,
               act_probs,
               qv1, qv2);

  if (can_traderoute) {
    func = caravan_establish_trade;
    cd->add_item(QString(_("Establish Trade route")), func, qv1, qv2,
                 "", BUTTON_TRADE_ROUTE);
  }

  if (can_marketplace) {
    func = caravan_marketplace;
    cd->add_item(QString(_("Enter Marketplace")), func, qv1, qv2,
                 "", BUTTON_MARKET_PLACE);
  }

  if (can_wonder) {
    QString title;

    title = label_help_wonder_rem(target_city);
    func = caravan_help_build;
    cd->add_item(title, func, qv1, qv2, "", BUTTON_HELP_WONDER);
  }

  /* Spy/Diplomat acting against a unit */

  /* Set the correct target for the following actions. */
  qv2 = cd->target_id[ATK_UNIT];

  action_entry(cd,
               ACTION_SPY_BRIBE_UNIT,
               act_probs,
               qv1, qv2);

  action_entry(cd,
               ACTION_SPY_SABOTAGE_UNIT,
               act_probs,
               qv1, qv2);

  if (unit_can_move_to_tile(actor_unit, target_tile, FALSE)) {
    qv2 = target_tile->index;

    func = diplomat_keep_moving;
    cd->add_item(QString(_("Keep moving")), func, qv1, qv2,
                 "", BUTTON_MOVE);
  }

  func = keep_moving;
  cd->add_item(QString(_("Do nothing")), func, qv1, qv2,
               "", BUTTON_CANCEL);

  cd->set_layout();
  cd->show_me();

  astr_free(&title);
  astr_free(&text);
}

/**********************************************************************
  Show the user the action if it is enabled.
**********************************************************************/
static void action_entry(choice_dialog *cd,
                         gen_action act,
                         const action_probability *action_probabilities,
                         QVariant data1, QVariant data2)
{
  QString title;
  QString tool_tip;

  /* Don't show disabled actions. */
  if (!action_prob_possible(action_probabilities[act])) {
    return;
  }

  title = QString(action_prepare_ui_name(act, "&",
                                         action_probabilities[act]));

  tool_tip = QString(action_get_tool_tip(act, action_probabilities[act]));

  cd->add_item(title, af_map[act], data1, data2, tool_tip, act);
}

/**********************************************************************
  Update an existing button.
**********************************************************************/
static void action_entry_update(QPushButton *button,
                                gen_action act,
                                const action_probability *act_prob,
                                QVariant data1, QVariant data2)
{
  QString title;
  QString tool_tip;

  /* An action that just became impossible has its button disabled.
   * An action that became possible again must be reenabled. */
  button->setEnabled(action_prob_possible(act_prob[act]));

  /* The probability may have changed. */
  title = QString(action_prepare_ui_name(act, "&",
                                         act_prob[act]));

  tool_tip = QString(action_get_tool_tip(act, act_prob[act]));

  button->setText(title);
  button->setToolTip(tool_tip);
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
    /* Wait for the server's reply before moving on to the next queued diplomat. */
    is_more_user_input_needed = TRUE;

    request_action_details(ACTION_SPY_BRIBE_UNIT, diplomat_id,
                           diplomat_target_id);
  }
}

/***************************************************************************
  Action sabotage unit for choice dialog
***************************************************************************/
static void spy_sabotage_unit(QVariant data1, QVariant data2)
{
  int diplomat_id = data1.toInt();
  int diplomat_target_id = data2.toInt();

  request_do_action(ACTION_SPY_SABOTAGE_UNIT, diplomat_id,
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
  int nr = 0;
  QList<QVariant> actor_and_target;

  /* Wait for the player's reply before moving on to the next queued diplomat. */
  is_more_user_input_needed = TRUE;

  if (pvcity) {
    pvictim = city_owner(pvcity);
  }
  cd = gui()->get_diplo_dialog();
  if (cd != NULL) {
    cd->close();
  }
  struct astring stra = ASTRING_INIT;
  cd = new choice_dialog(_("Steal"), _("Steal Technology"),
                         gui()->game_tab_widget,
                         diplomat_queue_handle_secondary);

  /* Put both actor and target city in qv1 since qv2 is taken */
  actor_and_target.append(diplomat_id);
  actor_and_target.append(diplomat_target_id);
  qv1 = QVariant::fromValue(actor_and_target);

  struct player *pplayer = client.conn.playing;
  if (pvictim) {
    const struct research *presearch = research_get(pplayer);
    const struct research *vresearch = research_get(pvictim);

    advance_index_iterate(A_FIRST, i) {
      if (research_invention_state(vresearch, i) == TECH_KNOWN
          && (research_invention_state(presearch, i) == TECH_UNKNOWN
              || research_invention_state(presearch, i)
                 == TECH_PREREQS_KNOWN)) {
        func = spy_steal_something;
        str = research_advance_name_translation(presearch, i);
        cd->add_item(str, func, qv1, i);
        nr++;
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
  int diplomat_id = data1.toList().at(0).toInt();
  int diplomat_target_id = data1.toList().at(1).toInt();

  if (NULL != game_unit_by_number(diplomat_id)
      && NULL != game_city_by_number(diplomat_target_id)) {
    request_do_action(ACTION_SPY_TARGETED_STEAL_TECH, diplomat_id,
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
    /* Wait for the server's reply before moving on to the next queued diplomat. */
    is_more_user_input_needed = TRUE;

    request_action_details(ACTION_SPY_TARGETED_SABOTAGE_CITY, diplomat_id,
                           diplomat_target_id);
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
    request_do_action(ACTION_SPY_POISON,
                      diplomat_id, diplomat_target_id, 0);
  }
}

/***************************************************************************
  Action steal gold for choice dialog
***************************************************************************/
static void spy_steal_gold(QVariant data1, QVariant data2)
{
  int diplomat_id = data1.toInt();
  int diplomat_target_id = data2.toInt();

  if (NULL != game_unit_by_number(diplomat_id)
      && NULL != game_city_by_number(diplomat_target_id)) {
    request_do_action(ACTION_SPY_STEAL_GOLD,
                      diplomat_id, diplomat_target_id, 0);
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
    request_do_action(ACTION_ESTABLISH_EMBASSY, diplomat_id,
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
    request_do_action(ACTION_SPY_INVESTIGATE_CITY, diplomat_id,
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
    request_do_action(ACTION_SPY_SABOTAGE_CITY, diplomat_id,
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
    request_do_action(ACTION_SPY_STEAL_TECH, diplomat_id,
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
    /* Wait for the server's reply before moving on to the next queued diplomat. */
    is_more_user_input_needed = TRUE;

    request_action_details(ACTION_SPY_INCITE_CITY, diplomat_id,
                           diplomat_target_id);
  }
}

/***************************************************************************
  Action keep moving with diplomat for choice dialog
***************************************************************************/
static void diplomat_keep_moving(QVariant data1, QVariant data2)
{
  struct unit *punit;
  struct tile *ptile;
  int diplomat_id = data1.toInt();
  int diplomat_target_id = data2.toInt();

  if ((punit = game_unit_by_number(diplomat_id))
      && (ptile = index_to_tile(diplomat_target_id))
      && !same_pos(unit_tile(punit), ptile)) {
    request_do_action(ACTION_MOVE, diplomat_id,
                      diplomat_target_id, 0);
  }
}

/**************************************************************************
  Popup a window asking a diplomatic unit if it wishes to incite the
  given enemy city.
**************************************************************************/
void popup_incite_dialog(struct unit *actor, struct city *tcity, int cost)
{
  char buf[1024];
  char buf2[1024];
  int ret;
  int diplomat_id = actor->id;
  int diplomat_target_id = tcity->id;

  /* Should be set before sending request to the server. */
  fc_assert(is_more_user_input_needed);

  fc_snprintf(buf, ARRAY_SIZE(buf), PL_("Treasury contains %d gold.",
                                        "Treasury contains %d gold.",
                                        client_player()->economic.gold),
              client_player()->economic.gold);

  if (INCITE_IMPOSSIBLE_COST == cost) {
    QMessageBox incite_impossible;

    fc_snprintf(buf2, ARRAY_SIZE(buf2),
                _("You can't incite a revolt in %s."), city_name(tcity));
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
      break;
    case QMessageBox::Ok:
      request_do_action(ACTION_SPY_INCITE_CITY, diplomat_id,
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

  diplomat_queue_handle_secondary();
}

/**************************************************************************
  Popup a dialog asking a diplomatic unit if it wishes to bribe the
  given enemy unit.
**************************************************************************/
void popup_bribe_dialog(struct unit *actor, struct unit *tunit, int cost)
{
  QMessageBox ask(gui()->central_wdg);
  int ret;
  QString str;
  char buf[1024];
  char buf2[1024];
  int diplomat_id = actor->id;
  int diplomat_target_id = tunit->id;

  /* Should be set before sending request to the server. */
  fc_assert(is_more_user_input_needed);

  fc_snprintf(buf, ARRAY_SIZE(buf), PL_("Treasury contains %d gold.",
                                        "Treasury contains %d gold.",
                                        client_player()->economic.gold),
              client_player()->economic.gold);

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
      request_do_action(ACTION_SPY_BRIBE_UNIT, diplomat_id,
                        diplomat_target_id, 0);
      break;
    default:
      break;
    }
  } else {
    fc_snprintf(buf2, ARRAY_SIZE(buf2),
                PL_("Bribing the unit costs %d gold.\n%s",
                    "Bribing the unit costs %d gold.\n%s", cost), cost, buf);
    ask.setWindowTitle(_("Traitors Demand Too Much!"));
    ask.exec();
  }

  diplomat_queue_handle_secondary();
}

/***************************************************************************
  Action pillage for choice dialog
***************************************************************************/
static void pillage_something(QVariant data1, QVariant data2)
{
  int punit_id;
  int what;
  struct unit *punit;
  struct extra_type *target;

  what = data1.toInt();
  punit_id = data2.toInt();
  punit = game_unit_by_number(punit_id);
  if (punit) {
    target = extra_by_number(what);
    request_new_unit_activity_targeted(punit, ACTIVITY_PILLAGE, target);
  }
  ::is_showing_pillage_dialog = false;
}

/***************************************************************************
  Action sabotage with spy for choice dialog
***************************************************************************/
static void spy_sabotage(QVariant data1, QVariant data2)
{
  int diplomat_id = data1.toList().at(0).toInt();
  int diplomat_target_id = data1.toList().at(1).toInt();

  if (NULL != game_unit_by_number(diplomat_id)
        && NULL != game_city_by_number(diplomat_target_id)) {
      request_do_action(ACTION_SPY_TARGETED_SABOTAGE_CITY, diplomat_id,
                        diplomat_target_id,  data2.toInt()+1);
    }
}

/**************************************************************************
  Popup a dialog asking a diplomatic unit if it wishes to sabotage the
  given enemy city.
**************************************************************************/
void popup_sabotage_dialog(struct unit *actor, struct city *tcity)
{

  QString str;
  QVariant qv1, qv2;
  int diplomat_id = actor->id;
  int diplomat_target_id = tcity->id;
  pfcn_void func;
  choice_dialog *cd = new choice_dialog(_("Sabotage"),
                                        _("Select Improvement to Sabotage"),
                                        gui()->game_tab_widget,
                                        diplomat_queue_handle_secondary);
  int nr = 0;
  struct astring stra = ASTRING_INIT;
  QList<QVariant> actor_and_target;

  /* Should be set before sending request to the server. */
  fc_assert(is_more_user_input_needed);

  /* Put both actor and target city in qv1 since qv2 is taken */
  actor_and_target.append(diplomat_id);
  actor_and_target.append(diplomat_target_id);
  qv1 = QVariant::fromValue(actor_and_target);

  func = spy_sabotage;
  cd->add_item(QString(_("City Production")), func, qv1, -1);
  city_built_iterate(tcity, pimprove) {
    if (pimprove->sabotage > 0) {
      func = spy_sabotage;
      str = city_improvement_name_translation(tcity, pimprove);
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
void popup_pillage_dialog(struct unit *punit, bv_extras extras)
{
  QString str;
  QVariant qv1, qv2;
  pfcn_void func;
  choice_dialog *cd;
  struct extra_type *tgt;

  if (is_showing_pillage_dialog){
    return;
  }
  cd = new choice_dialog(_("What To Pillage"), _("Select what to pillage:"),
                         gui()->game_tab_widget);
  qv2 = punit->id;
  while ((tgt = get_preferred_pillage(extras))) {
    int what;
    bv_extras what_extras;

    BV_CLR_ALL(what_extras);

    what = extra_index(tgt);
    BV_CLR(extras, what);
    BV_SET(what_extras, what);

    func = pillage_something;
    str = get_infrastructure_text(what_extras);
    qv1 = what;
    cd->add_item(str, func, qv1, qv2);
  }
  cd->set_layout();
  cd->show_me();
}

/****************************************************************************
  Pops up a dialog to confirm disband of the unit(s).
****************************************************************************/
void popup_disband_dialog(struct unit_list *punits)
{
  QMessageBox ask(gui()->central_wdg);
  int ret;
  QString str;

  if (!punits || unit_list_size(punits) == 0) {
    return;
  }
  str = QString(PL_("Are you sure you want to disband that %1 unit?",
                  "Are you sure you want to disband those %1 units?",
                  unit_list_size(punits))).arg(unit_list_size(punits));
  ask.setText(str);
  ask.setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
  ask.setDefaultButton(QMessageBox::Cancel);
  ask.setIcon(QMessageBox::Question);
  ask.setWindowTitle(_("Disband units"));
  ret = ask.exec();

  switch (ret) {
  case QMessageBox::Cancel:
    return;
    break;
  case QMessageBox::Ok:
    unit_list_iterate(punits, punit) {
      if (!unit_has_type_flag(punit, UTYF_UNDISBANDABLE)) {
        request_unit_disband(punit);
      }
    } unit_list_iterate_end;
    break;
  }
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

/****************************************************************
  Ruleset (modpack) has suggested loading certain musicset. Confirm from
  user and load.
*****************************************************************/
void popup_musicset_suggestion_dialog(void)
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

  cd_list = gui()->game_tab_widget->findChildren <choice_dialog *>();
  for (i = 0; i < cd_list.count(); i++) {
      cd_list[i]->close();
  }
}

/**************************************************************************
  Returns the id of the actor unit currently handled in action selection
  dialog when the action selection dialog is open.
  Returns IDENTITY_NUMBER_ZERO if no action selection dialog is open.
**************************************************************************/
int action_selection_actor_unit(void)
{
  choice_dialog *cd = gui()->get_diplo_dialog();

  if (cd != NULL) {
    return cd->unit_id;
  } else {
    return IDENTITY_NUMBER_ZERO;
  }
}

/**************************************************************************
  Returns id of the target city of the actions currently handled in action
  selection dialog when the action selection dialog is open and it has a
  city target. Returns IDENTITY_NUMBER_ZERO if no action selection dialog
  is open or no city target is present in the action selection dialog.
**************************************************************************/
int action_selection_target_city(void)
{
  choice_dialog *cd = gui()->get_diplo_dialog();

  if (cd != NULL) {
    return cd->target_id[ATK_CITY];
  } else {
    return IDENTITY_NUMBER_ZERO;
  }
}

/**************************************************************************
  Returns id of the target unit of the actions currently handled in action
  selection dialog when the action selection dialog is open and it has a
  unit target. Returns IDENTITY_NUMBER_ZERO if no action selection dialog
  is open or no unit target is present in the action selection dialog.
**************************************************************************/
int action_selection_target_unit(void)
{
  choice_dialog *cd = gui()->get_diplo_dialog();

  if (cd != NULL) {
    return cd->target_id[ATK_UNIT];
  } else {
    return IDENTITY_NUMBER_ZERO;
  }
}

/**************************************************************************
  Updates the action selection dialog with new information.
**************************************************************************/
void action_selection_refresh(struct unit *actor_unit,
                              struct city *target_city,
                              struct unit *target_unit,
                              struct tile *target_tile,
                              const action_probability *act_prob)
{
  choice_dialog *asd;
  Choice_dialog_button *keep_moving_button;
  QVariant qv1, qv2;

  asd = gui()->get_diplo_dialog();
  if (asd == NULL) {
    fc_assert_msg(asd != NULL,
                  "The action selection dialog should have been open");
    return;
  }

  if (actor_unit->id != action_selection_actor_unit()) {
    fc_assert_msg(actor_unit->id == action_selection_actor_unit(),
                  "The action selection dialog is for another actor unit.");
  }

  /* Put the actor id in qv1. */
  qv1 = actor_unit->id;

  keep_moving_button = asd->get_identified_button(BUTTON_CANCEL);
  if (keep_moving_button != NULL) {
    /* Temporary remove the Keep moving button so it won't end up above
     * any added buttons. */
    asd->stack_button(keep_moving_button);
  }

  action_iterate(act) {
    if (action_get_actor_kind(act) != AAK_UNIT) {
      /* Not relevant. */
      continue;
    }

    /* Put the target id in qv2. */
    switch (action_get_target_kind(act)) {
    case ATK_UNIT:
      if (target_unit != NULL) {
        qv2 = target_unit->id;
      } else {
        fc_assert_msg(!action_prob_possible(act_prob[act])
                      || target_unit != NULL,
                      "Action enabled against non existing unit!");

        qv2 = IDENTITY_NUMBER_ZERO;
      }
      break;
    case ATK_CITY:
      if (target_city != NULL) {
        qv2 = target_city->id;
      } else {
        fc_assert_msg(!action_prob_possible(act_prob[act])
                      || target_city != NULL,
                      "Action enabled against non existing city!");

        qv2 = IDENTITY_NUMBER_ZERO;
      }
      break;
    case ATK_COUNT:
      fc_assert_msg(ATK_COUNT != action_get_target_kind(act),
                    "Bad target kind");
      continue;
    }

    if (asd->get_identified_button(act)) {
      /* Update the existing button. */
      action_entry_update(asd->get_identified_button(act),
                          (enum gen_action)act, act_prob, qv1, qv2);
    } else {
      /* Add the button (unless its probability is 0). */
      action_entry(asd, (enum gen_action)act, act_prob, qv1, qv2);
    }
  } action_iterate_end;

  if (keep_moving_button != NULL) {
    /* Reinsert the "Keep moving" button below any potential
     * buttons recently added. */
    asd->unstack_all_buttons();
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
}

/****************************************************************
  Updates caravan dialog
****************************************************************/
void caravan_dialog_update(void)
{
  struct city *destcity;
  struct unit *caravan;
  QString wonder;
  Choice_dialog_button *help_wonder_button;
  bool can_wonder;
  choice_dialog *asd = gui()->get_diplo_dialog();

  if (asd == NULL) {
    return;
  }

  destcity = game_city_by_number(asd->target_id[ATK_CITY]);
  caravan = game_unit_by_number(asd->unit_id);
  can_wonder = destcity && caravan
               && unit_can_help_build_wonder(caravan, destcity);

  help_wonder_button = asd->get_identified_button(BUTTON_HELP_WONDER);

  if (help_wonder_button != NULL) {
    if (can_wonder) {
      wonder = label_help_wonder_rem(destcity);
    } else {
      wonder = QString(_("Help build Wonder"));
    }

    help_wonder_button->setText(wonder);
    help_wonder_button->setEnabled(can_wonder);
  } else if (can_wonder) {
    Choice_dialog_button *keep_moving_button;
    QString title;

    keep_moving_button = asd->get_identified_button(BUTTON_CANCEL);

    if (keep_moving_button != NULL) {
      /* Temporary remove the Keep moving button so it won't end up above
       * the Help build Wonder button. */
      asd->stack_button(keep_moving_button);
    }

    title = label_help_wonder_rem(destcity);
    asd->add_item(title, caravan_help_build,
                  caravan->id, destcity->id,
                  "", BUTTON_HELP_WONDER);

    if (keep_moving_button != NULL) {
      /* Reinsert the "Keep moving" button below the
       * Help build Wonder button. */
      asd->unstack_all_buttons();
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
  /* PORTME */
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
}
/****************************************************************
  Destructor for unit select
*****************************************************************/
unit_select::~unit_select()
{
    delete h_pix;
    delete pix;
    delete ufont;
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
    put_unit(punit, unit_pixmap, 1.0, 0, 0);
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
}

/****************************************************************
  Updates unit list on tile
*****************************************************************/
void unit_select::update_units()
{
  int i = 1;

  if (utile == NULL) {
    struct unit *punit = head_of_units_in_focus();
    if (punit) {
      utile = unit_tile(punit);
    }
  }
  unit_list.clear();
  unit_list_iterate(utile->units, punit) {
    if (i > show_line * 4)
      unit_list.push_back(punit);
    i++;
  } unit_list_iterate_end;
  if (unit_list.count() == 0) {
    close();
  }
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

