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
#include <QHeaderView>
#include <QSplitter>

// gui-qt
#include "plrdlg.h"
#include "qtg_cxxside.h"

/**************************************************************************
  Help function to draw checkbox inside delegate
**************************************************************************/
static QRect check_box_rect(const QStyleOptionViewItem
                            &view_item_style_options)
{
  QStyleOptionButton cbso;
  QRect check_box_rect = QApplication::style()->subElementRect(
                           QStyle::SE_CheckBoxIndicator, &cbso);
  QPoint check_box_point(view_item_style_options.rect.x() +
                         view_item_style_options.rect.width() / 2 -
                         check_box_rect.width() / 2,
                         view_item_style_options.rect.y() +
                         view_item_style_options.rect.height() / 2 -
                         check_box_rect.height() / 2);
  return QRect(check_box_point, check_box_rect.size());
}

/**************************************************************************
  Slighty increase deafult cell height
**************************************************************************/
QSize plr_item_delegate::sizeHint(const QStyleOptionViewItem &option,
                                  const QModelIndex &index) const
{
  QSize r;
  r =  QItemDelegate::sizeHint(option, index);
  r.setHeight(r.height() + 4);
  return r;
}

/**************************************************************************
  Paint evenet for custom player item delegation
**************************************************************************/
void plr_item_delegate::paint(QPainter *painter, const QStyleOptionViewItem
                              &option, const QModelIndex &index) const
{
  QStyleOptionButton but;
  QStyleOptionButton cbso;
  bool b;
  QString str;
  QRect rct;
  QPixmap pix(16, 16);

  QStyleOptionViewItemV3 opt = QItemDelegate::setOptions(index, option);
  painter->save();
  switch (player_dlg_columns[index.column()].type) {
  case COL_FLAG:
    QItemDelegate::drawBackground(painter, opt, index);
    QItemDelegate::drawDecoration(painter, opt, option.rect,
                                  index.data().value<QPixmap>());
    break;
  case COL_COLOR:
    pix.fill(index.data().value <QColor> ());
    QItemDelegate::drawBackground(painter, opt, index);
    QItemDelegate::drawDecoration(painter, opt, option.rect, pix);
    break;
  case COL_BOOLEAN:
    b = index.data().toBool();
    QItemDelegate::drawBackground(painter, opt, index);
    cbso.state |= QStyle::State_Enabled;
    if (b) {
      cbso.state |= QStyle::State_On;
    } else {
      cbso.state |= QStyle::State_Off;
    }
    cbso.rect = check_box_rect(option);

    QApplication::style()->drawControl(QStyle::CE_CheckBox, &cbso, painter);
    break;
  case COL_TEXT:
    QItemDelegate::paint(painter, option, index);
    break;
  case COL_RIGHT_TEXT:
    QItemDelegate::drawBackground(painter, opt, index);
    opt.displayAlignment = Qt::AlignRight;
    rct = option.rect;
    rct.setTop((rct.top() + rct.bottom()) / 2
               - opt.fontMetrics.height() / 2);
    rct.setBottom((rct.top()+rct.bottom()) / 2
                  + opt.fontMetrics.height() / 2);
    if (index.data().toInt() == -1){
      str = "?";
    } else {
      str = index.data().toString();
    }
    QItemDelegate::drawDisplay(painter, opt, rct, str);
    break;
  default:
    QItemDelegate::paint(painter, option, index);
  }
  painter->restore();
}

/**************************************************************************
  Constructor for plr_item
**************************************************************************/
plr_item::plr_item(struct player *pplayer): QObject()
{
  ipplayer = pplayer;
}

/**************************************************************************
  Sets data for plr_item (not used)
**************************************************************************/
bool plr_item::setData(int column, const QVariant &value, int role)
{
  return false;
}

/**************************************************************************
  Returns data from item
**************************************************************************/
QVariant plr_item::data(int column, int role) const
{
  QPixmap *pix;
  QString str;
  struct player_dlg_column *pdc;

  if (role == Qt::UserRole) {
    return QVariant::fromValue((void *)ipplayer);
  }
  if (role != Qt::DisplayRole) {
    return QVariant();
  }
  pdc = &player_dlg_columns[column];
  switch (player_dlg_columns[column].type) {
  case COL_FLAG:
    pix = get_nation_flag_sprite(tileset, nation_of_player(ipplayer))->pm;
    return *pix;
    break;
  case COL_COLOR:
    return get_player_color(tileset, ipplayer)->qcolor;
    break;
  case COL_BOOLEAN:
    return pdc->bool_func(ipplayer);
    break;
  case COL_TEXT:
    return pdc->func(ipplayer);
    break;
  case COL_RIGHT_TEXT:
    str = pdc->func(ipplayer);
    if (str.toInt() != 0){
      return str.toInt();
    } else if (str == "?"){
      return -1;
    }
    return str;
  default:
    return QVariant();
  }
  return QVariant();
}

/**************************************************************************
  Constructor for player model
**************************************************************************/
plr_model::plr_model(QObject *parent): QAbstractListModel(parent)
{
  populate();
}

/**************************************************************************
  Destructor for player model
**************************************************************************/
plr_model::~plr_model()
{
  qDeleteAll(plr_list);
  plr_list.clear();
}

/**************************************************************************
  Returns data from model
**************************************************************************/
QVariant plr_model::data(const QModelIndex &index, int role) const
{
  if (!index.isValid()) return QVariant();
  if (index.row() >= 0 && index.row() < rowCount() && index.column() >= 0
      && index.column() < columnCount())
    return plr_list[index.row()]->data(index.column(), role);
  return QVariant();
}

/**************************************************************************
  Returns header data from model
**************************************************************************/
QVariant plr_model::headerData(int section, Qt::Orientation orientation, 
                               int role) const
{
  struct player_dlg_column *pcol;
  if (orientation == Qt::Horizontal && section < num_player_dlg_columns) {
    if (role == Qt::DisplayRole) {
      pcol = &player_dlg_columns[section];
      return pcol->title;
    }
  }
  return QVariant();
}

/**************************************************************************
  Sets data in model
**************************************************************************/
bool plr_model::setData(const QModelIndex &index, const QVariant &value, 
                        int role)
{
  if (!index.isValid() || role != Qt::DisplayRole) return false;
  if (index.row() >= 0 && index.row() < rowCount() && index.column() >= 0 
    && index.column() < columnCount()) {
    bool change = plr_list[index.row()]->setData(index.column(), value, role);
    if (change) {
      notify_plr_changed(index.row());
    }
    return change;
  }
  return false;
}

/**************************************************************************
  Notifies that row has been changed
**************************************************************************/
void plr_model::notify_plr_changed(int row)
{
  emit dataChanged(index(row, 0), index(row, columnCount() - 1));
}

/**************************************************************************
  Fills model with data
**************************************************************************/
void plr_model::populate()
{
  plr_item *pi;
  players_iterate(pplayer) {
    if ((is_barbarian(pplayer))){
      continue;
    }
    pi = new plr_item(pplayer);
    plr_list << pi;
  } players_iterate_end;
}


/**************************************************************************
  Constructor for plr_widget
**************************************************************************/
plr_widget::plr_widget(plr_report *pr): QTreeView()
{
  plr = pr;
  other_player = NULL;
  pid = new plr_item_delegate(this);
  setItemDelegate(pid);
  list_model = new plr_model(this);
  filter_model = new QSortFilterProxyModel();
  filter_model->setDynamicSortFilter(true);
  filter_model->setSourceModel(list_model);
  filter_model->setFilterRole(Qt::DisplayRole);
  setModel(filter_model);
  setRootIsDecorated(false);
  setAllColumnsShowFocus(true);
  setSortingEnabled(true);
  setSelectionMode(QAbstractItemView::SingleSelection);
  setItemsExpandable(false);
  setAutoScroll(true);
  header()->setContextMenuPolicy(Qt::CustomContextMenu);
  hide_columns();
  connect(header(), SIGNAL(customContextMenuRequested(const QPoint &)),
          this, SLOT(display_header_menu(const QPoint &)));
  connect(selectionModel(),
          SIGNAL(selectionChanged(const QItemSelection &,
                                  const QItemSelection &)),
          SLOT(nation_selected(const QItemSelection &,
                               const QItemSelection &)));
}

/**************************************************************************
  Displays menu on header by right clicking
**************************************************************************/
void plr_widget::display_header_menu(const QPoint &)
{
  struct player_dlg_column *pcol;
  QMenu hideshowColumn(this);
  hideshowColumn.setTitle(_("Column visibility"));
  QList<QAction *> actions;
  for (int i = 0; i < list_model->columnCount(); ++i) {
    QAction *myAct = hideshowColumn.addAction(
                       list_model->headerData(i, Qt::Horizontal, 
                                              Qt::DisplayRole).toString());
    myAct->setCheckable(true);
    myAct->setChecked(!isColumnHidden(i));
    actions.append(myAct);
  }
  QAction *act = hideshowColumn.exec(QCursor::pos());
  if (act) {
    int col = actions.indexOf(act);
    Q_ASSERT(col >= 0);
    pcol = &player_dlg_columns[col];
    pcol->show = !pcol->show;
    setColumnHidden(col, !isColumnHidden(col));
    if (!isColumnHidden(col) && columnWidth(col) <= 5)
      setColumnWidth(col, 100);
  }
}

/**************************************************************************
  Returns information about column if hidden
**************************************************************************/
QVariant plr_model::hide_data(int section) const
{
  struct player_dlg_column *pcol;
  pcol = &player_dlg_columns[section];
  return pcol->show;
}

/**************************************************************************
  Hides columns in plr widget, depending on info from plr_list
**************************************************************************/
void plr_widget::hide_columns()
{
  for (int col = 0; col < list_model->columnCount(); col++) {
   if(!list_model->hide_data(col).toBool()){
    setColumnHidden(col, !isColumnHidden(col));
   }
  }
}


/**************************************************************************
  Slot for selecting player/nation
**************************************************************************/
void plr_widget::nation_selected(const QItemSelection &sl,
                                 const QItemSelection &ds)
{
  QModelIndex index;
  QVariant qvar;
  QModelIndexList indexes = sl.indexes();
  struct city *pcity;
  const struct player_diplstate *state;
  struct research *my_research, *research;
  char tbuf[256];
  QString res;
  QString sp = " ";
  QString nl = "<br>";
  struct player *pplayer;
  int a , b;
  bool added;
  bool entry_exist = false;
  struct player *me;
  Tech_type_id tech_id;

  other_player = NULL;
  intel_str.clear();
  tech_str.clear();
  ally_str.clear();
  if (indexes.isEmpty()) {
    plr->update_report();
    return;
  }
  index = indexes.at(0);
  qvar = index.data(Qt::UserRole);
  pplayer = reinterpret_cast<player *>(qvar.value<void *>());
  other_player = pplayer;
  if (pplayer->is_alive == false) {
    plr->update_report();
    return;
  }
  pcity = player_capital(pplayer);
  research = research_get(pplayer);

  switch (research->researching) {
  case A_UNKNOWN:
    res = _("(Unknown)");
    break;
  case A_UNSET:
    res = _("(none)");
    break;
  default:
    res = QString(research_advance_name_translation(research,
                                                    research->researching))
          + sp + "(" + QString::number(research->bulbs_researched) + "/"
          + QString::number(research->client.researching_cost) + ")";
    break;
  }
  /** Formatting rich text */
  intel_str =
    QString("<table><tr><td><b>") + _("Nation") + QString("</b></td><td>")
    + QString(nation_adjective_for_player(pplayer))
    + QString("</td><tr><td><b>") + N_("Ruler:") + QString("</b></td><td>")
    + QString(ruler_title_for_player(pplayer, tbuf, sizeof(tbuf)))
    + QString("</td></tr><tr><td><b>") + N_("Government:") 
    + QString("</b></td><td>") + QString(government_name_for_player(pplayer))
    + QString("</td></tr><tr><td><b>") + N_("Capital:")
    + QString("</b></td><td>")
    + QString(((!pcity) ? _("(unknown)") : city_name(pcity)))
    + QString("</td></tr><tr><td><b>") + N_("Gold:")
    + QString("</b></td><td>") + QString::number(pplayer->economic.gold)
    + QString("</td></tr><tr><td><b>") + N_("Tax:")
    + QString("</b></td><td>") + QString::number(pplayer->economic.tax)
    + QString("%</td></tr><tr><td><b>") + N_("Science:")
    + QString("</b></td><td>") + QString::number(pplayer->economic.science)
    + QString("%</td></tr><tr><td><b>") + N_("Luxury:")
    + QString("</b></td><td>") + QString::number(pplayer->economic.luxury)
    + QString("%</td></tr><tr><td><b>") + N_("Researching:")
    + QString("</b></td><td>") + res + QString("</td></table>");

  for (int i = 0; i < static_cast<int>(DS_LAST); i++) {
    added = false;
    if (entry_exist) {
      ally_str += "<br>";
    }
    entry_exist = false;
    players_iterate_alive(other) {
      if (other == pplayer || is_barbarian(other)) {
        continue;
      }
      state = player_diplstate_get(pplayer, other);
      if (static_cast<int>(state->type) == i) {
        if (added == false) {
          ally_str = ally_str  + QString("<b>")
                     + QString(diplstate_type_translated_name(
                                 static_cast<diplstate_type>(i)))
                     + ": "  + QString("</b>") + nl;
          added = true;
        }
        ally_str = ally_str + nation_plural_for_player(other) + ", ";
        entry_exist = true;
      }
    } players_iterate_alive_end;
    if (entry_exist) {
      ally_str.replace(ally_str.lastIndexOf(","), 1, ".");
    }
  }
  me = client_player();
  my_research = research_get(me);
  if ((player_has_embassy(me, pplayer) || client_is_global_observer())
      && me != pplayer) {
    a = 0;
    b = 0;
    techs_known = QString(_("<b>Techs unknown by %1:</b>")).
                    arg(nation_plural_for_player(pplayer));
    techs_unknown = QString(_("<b>Techs unknown by you :</b>"));

    advance_iterate(A_FIRST, padvance) {
      tech_id = advance_number(padvance);
      if (research_invention_state(my_research, tech_id) == TECH_KNOWN
          && (research_invention_state(research, tech_id) == TECH_UNKNOWN)) {
        a++;
        techs_known = techs_known + QString("<i>") 
                      + research_advance_name_translation(research, tech_id)
                      + "," + QString("</i>") + sp;
      }
      if (research_invention_state(my_research, tech_id) == TECH_UNKNOWN
          && (research_invention_state(research, tech_id) == TECH_KNOWN)) {
        b++;
        techs_unknown = techs_unknown + QString("<i>")
                        + research_advance_name_translation(research,
                                                            tech_id)
                        + "," + QString("</i>") + sp;
      }
    } advance_iterate_end;

    if (a == 0) {
      techs_known = techs_known + QString("<i>") + sp
                    + QString(_("None")) + QString("</i>");
    } else {
      techs_known.replace(techs_known.lastIndexOf(","), 1, ".");
    }
    if (b == 0) {
      techs_unknown = techs_unknown + QString("<i>") + sp
                      + QString(_("None")) + QString("</i>");
    } else {
      techs_unknown.replace(techs_unknown.lastIndexOf(","), 1, ".");
    }
    tech_str = techs_known + nl + techs_unknown;
  }
  plr->update_report();
}

/**************************************************************************
  Returns model used in widget
**************************************************************************/
plr_model *plr_widget::get_model() const
{
  return list_model;
}

/**************************************************************************
  Destructor for player widget
**************************************************************************/
plr_widget::~plr_widget()
{
  delete pid;
  delete list_model;
  delete filter_model;
}

/**************************************************************************
  Constructor for plr_report
**************************************************************************/
plr_report::plr_report():QWidget()
{
  v_splitter = new QSplitter(Qt::Vertical);
  h_splitter = new QSplitter(Qt::Horizontal);
  layout = new QVBoxLayout;
  hlayout = new QHBoxLayout;
  plr_wdg = new plr_widget(this);
  plr_label = new QLabel;
  plr_label->setFrameStyle(QFrame::StyledPanel);
  plr_label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  plr_label->setWordWrap(true);
  plr_label->setTextFormat(Qt::RichText);
  ally_label = new QLabel;
  ally_label->setFrameStyle(QFrame::StyledPanel);
  ally_label->setTextFormat(Qt::RichText);
  ally_label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  ally_label->setWordWrap(true);
  tech_label = new QLabel;
  tech_label->setTextFormat(Qt::RichText);
  tech_label->setFrameStyle(QFrame::StyledPanel);
  tech_label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  tech_label->setWordWrap(true);
  meet_but = new QPushButton;
  meet_but->setText(_("Meet"));
  cancel_but = new QPushButton;
  cancel_but->setText(_("Cancel Treaty"));
  withdraw_but = new QPushButton;
  withdraw_but->setText(_("Withdraw Vision"));
  meet_but->setDisabled(true);
  cancel_but->setDisabled(true);
  withdraw_but->setDisabled(true);
  v_splitter->addWidget(plr_wdg);
  h_splitter->addWidget(plr_label);
  h_splitter->addWidget(ally_label);
  h_splitter->addWidget(tech_label);
  v_splitter->addWidget(h_splitter);
  layout->addWidget(v_splitter);
  hlayout->addWidget(meet_but);
  hlayout->addWidget(cancel_but);
  hlayout->addWidget(withdraw_but);
  hlayout->addStretch();
  layout->addLayout(hlayout);
  connect(meet_but, SIGNAL(pressed()), SLOT(req_meeeting()));
  connect(cancel_but, SIGNAL(pressed()), SLOT(req_caancel_threaty()));
  connect(withdraw_but, SIGNAL(pressed()), SLOT(req_wiithdrw_vision()));
  setLayout(layout);
}

/**************************************************************************
  Destructor for plr_report
**************************************************************************/
plr_report::~plr_report()
{
  gui()->remove_repo_dlg("PLR");
}

/**************************************************************************
  Adds plr_report to tab widget
**************************************************************************/
void plr_report::init()
{
  gui()->gimme_place(this, "PLR");
  index = gui()->add_game_tab(this, _("Players"));
  gui()->game_tab_widget->setCurrentIndex(index);
}

/**************************************************************************
  Slot for canceling threaty (name changed to cheat autoconnect, and
  doubled execution)
**************************************************************************/
void plr_report::req_caancel_threaty()
{
  dsend_packet_diplomacy_cancel_pact(&client.conn, 
                                     player_number(other_player),
                                     CLAUSE_CEASEFIRE);
}

/**************************************************************************
  Slot for meeting request
**************************************************************************/
void plr_report::req_meeeting()
{
  dsend_packet_diplomacy_init_meeting_req(&client.conn,
                                          player_number(other_player));
}

/**************************************************************************
  Slot for withdrawing vision
**************************************************************************/
void plr_report::req_wiithdrw_vision()
{
  dsend_packet_diplomacy_cancel_pact(&client.conn, 
                                     player_number(other_player),
                                     CLAUSE_VISION);
}

/**************************************************************************
  Updates widget
**************************************************************************/
void plr_report::update_report()
{
  struct player_diplstate *ds;
  meet_but->setDisabled(true);
  cancel_but->setDisabled(true);
  withdraw_but->setDisabled(true);
  plr_label->setText(plr_wdg->intel_str);
  ally_label->setText(plr_wdg->ally_str);
  tech_label->setText(plr_wdg->tech_str);
  other_player = plr_wdg->other_player;
  if (other_player == NULL || can_client_issue_orders() == false) {
    return;
  }
  if (NULL != client.conn.playing
      && other_player != client.conn.playing) {
    ds = player_diplstate_get(client_player(), other_player);
    if (ds->type != DS_WAR && ds->type != DS_NO_CONTACT) {
        cancel_but->setEnabled(true);
    }
  }
  if (gives_shared_vision(client_player(), other_player)) {
    withdraw_but->setEnabled(true);
  }
  if (can_meet_with_player(other_player) == true) {
    meet_but->setEnabled(true);
  }
}

/**************************************************************************
  Display the player list dialog.  Optionally raise it.
**************************************************************************/
void popup_players_dialog(bool raise)
{
  int i;
  plr_report *pr;
  QWidget *w;
  if (!gui()->is_repo_dlg_open("PLR")) {
    plr_report *pr = new plr_report;
    pr->init();
    pr->update_report();
  } else {
    i = gui()->gimme_index_of("PLR");
    fc_assert(i != -1);
    w = gui()->game_tab_widget->widget(i);
    pr = reinterpret_cast<plr_report*>(w);
    gui()->game_tab_widget->setCurrentWidget(pr);
  }
}

/**************************************************************************
  Update all information in the player list dialog.
**************************************************************************/
void real_players_dialog_update(void)
{
  int i;
  plr_report *pr;
  QWidget *w;

  if (gui()->is_repo_dlg_open("PLR")) {
    i = gui()->gimme_index_of("PLR");
    fc_assert(i != -1);
    w = gui()->game_tab_widget->widget(i);
    pr = reinterpret_cast<plr_report *>(w);
    pr->update_report();
  }
}
