/***********************************************************************
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
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

// gui-qt
#include "fc_client.h"
#include "gui_main.h"
#include "plrdlg.h"

/**********************************************************************//**
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

/**********************************************************************//**
  Slighty increase default cell height
**************************************************************************/
QSize plr_item_delegate::sizeHint(const QStyleOptionViewItem &option,
                                  const QModelIndex &index) const
{
  QSize r;

  r =  QItemDelegate::sizeHint(option, index);
  r.setHeight(r.height() + 4);

  return r;
}

/**********************************************************************//**
  Paint event for custom player item delegation
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
  QStyleOptionViewItem opt = QItemDelegate::setOptions(index, option);
  QFontMetrics *fm;
  QFont f;
  QPixmap pm;

  painter->save();
  switch (player_dlg_columns[index.column()].type) {
  case COL_FLAG:
    QItemDelegate::drawBackground(painter, opt, index);
    pm = index.data().value<QPixmap>();
    f = *fc_font::instance()->get_font(fonts::default_font);
    fm = new QFontMetrics(f);

    // We used to do the scaling on index.data() side,
    // but it was scaling the original flag, needed in the
    // full size for other uses.
    pm = pm.scaledToHeight(fm->height());
    delete fm;
    QItemDelegate::drawDecoration(painter, opt, option.rect, pm);
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
    if (index.data().toInt() == -1) {
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

/**********************************************************************//**
  Constructor for plr_item
**************************************************************************/
plr_item::plr_item(struct player *pplayer): QObject()
{
  ipplayer = pplayer;
}

/**********************************************************************//**
  Sets data for plr_item (not used)
**************************************************************************/
bool plr_item::setData(int column, const QVariant &value, int role)
{
  return false;
}

/**********************************************************************//**
  Returns data from item
**************************************************************************/
QVariant plr_item::data(int column, int role) const
{
  QFont f;
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
    return *(get_nation_flag_sprite(tileset, nation_of_player(ipplayer))->pm);
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
    if (str.toInt() != 0) {
      return str.toInt();
    } else if (str == "?") {
      return -1;
    }
    return str;
  default:
    return QVariant();
  }
}

/**********************************************************************//**
  Constructor for player model
**************************************************************************/
plr_model::plr_model(QObject *parent): QAbstractListModel(parent)
{
  populate();
}

/**********************************************************************//**
  Destructor for player model
**************************************************************************/
plr_model::~plr_model()
{
  qDeleteAll(plr_list);
  plr_list.clear();
}

/**********************************************************************//**
  Returns data from model
**************************************************************************/
QVariant plr_model::data(const QModelIndex &index, int role) const
{
  if (!index.isValid()) {
    return QVariant();
  }

  if (index.row() >= 0 && index.row() < rowCount() && index.column() >= 0
      && index.column() < columnCount()) {
    return plr_list[index.row()]->data(index.column(), role);
  }

  return QVariant();
}

/**********************************************************************//**
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

/**********************************************************************//**
  Sets data in model
**************************************************************************/
bool plr_model::setData(const QModelIndex &index, const QVariant &value,
                        int role)
{
  if (!index.isValid() || role != Qt::DisplayRole) {
    return false;
  }

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

/**********************************************************************//**
  Notifies that row has been changed
**************************************************************************/
void plr_model::notify_plr_changed(int row)
{
  emit dataChanged(index(row, 0), index(row, columnCount() - 1));
}

/**********************************************************************//**
  Fills model with data
**************************************************************************/
void plr_model::populate()
{
  plr_item *pi;

  qDeleteAll(plr_list);
  plr_list.clear();
  beginResetModel();

  // This is duplicated at plr_sorter::lessThan() for
  // index <-> player mapping to match
  players_iterate(pplayer) {
    if ((is_barbarian(pplayer))) {
      continue;
    }
    pi = new plr_item(pplayer);
    plr_list << pi;
  } players_iterate_end;

  endResetModel();
}

/**********************************************************************//**
  Constructor for the player/nation dialog sorter
**************************************************************************/
plr_sorter::plr_sorter(QObject *parent) : QSortFilterProxyModel(parent)
{
}

/**********************************************************************//**
  Destructor for the player/nation dialog sorter
**************************************************************************/
plr_sorter::~plr_sorter()
{
}

/**********************************************************************//**
  Sort the player/nation dialog
**************************************************************************/
bool plr_sorter::lessThan(const QModelIndex &left,
                          const QModelIndex &right) const
{
  struct player_dlg_column *column = &(player_dlg_columns[left.column()]);

  if (column->sort_func != nullptr) {
    int li = left.row();
    int ri = right.row();
    int i = 0;
    struct player *lplr = nullptr;
    struct player *rplr = nullptr;

    // Duplicates populate() logic to find player matching index
    players_iterate(pplayer) {
      if ((is_barbarian(pplayer))) {
        continue;
      }

      if (i == li) {
        lplr = pplayer;
      }
      if (i++ == ri) {
        rplr = pplayer;
      }
    } players_iterate_end;

    // Convert three-state (left better, equal, right better)
    // return from sort_func() to lessThan() boolean
    return column->sort_func(lplr, rplr) < 0;
  }

  // Use default sort when no sort function defined
  return QSortFilterProxyModel::lessThan(left, right);
}

/**********************************************************************//**
  Constructor for plr_widget
**************************************************************************/
plr_widget::plr_widget(plr_report *pr): QTreeView()
{
  plr = pr;
  other_player = nullptr;
  selected_player = nullptr;
  pid = new plr_item_delegate(this);
  setItemDelegate(pid);
  list_model = new plr_model(this);
  filter_model = new plr_sorter(this);
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
  setAlternatingRowColors(true);
  header()->setContextMenuPolicy(Qt::CustomContextMenu);
  hide_columns();
  connect(header(), &QWidget::customContextMenuRequested,
          this, &plr_widget::display_header_menu);
  connect(selectionModel(),
          SIGNAL(selectionChanged(const QItemSelection &,
                                  const QItemSelection &)),
          SLOT(nation_selected(const QItemSelection &,
                               const QItemSelection &)));
}

/**********************************************************************//**
  Restores selection of previously selected nation
**************************************************************************/
void plr_widget::restore_selection()
{
  QItemSelection selection;
  QModelIndex i;
  struct player *pplayer;
  QVariant qvar;

  if (selected_player == nullptr) {
    return;
  }

  for (int j = 0; j < filter_model->rowCount(); j++) {
    i = filter_model->index(j, 0);
    qvar = i.data(Qt::UserRole);
    if (qvar.isNull()) {
      continue;
    }
    pplayer = reinterpret_cast<struct player *>(qvar.value<void *>());
    if (selected_player == pplayer) {
      selection.append(QItemSelectionRange(i));
    }
  }
  selectionModel()->select(selection, QItemSelectionModel::Rows
                           | QItemSelectionModel::SelectCurrent);
}

/**********************************************************************//**
  Displays menu on header by right clicking
**************************************************************************/
void plr_widget::display_header_menu(const QPoint &)
{
  QMenu *hideshow_column = new QMenu(this);
  QList<QAction *> actions;

  hideshow_column->setTitle(_("Column visibility"));

  for (int i = 0; i < list_model->columnCount(); ++i) {
    QAction *myAct = hideshow_column->addAction(
                       list_model->headerData(i, Qt::Horizontal,
                                              Qt::DisplayRole).toString());
    myAct->setCheckable(true);
    myAct->setChecked(!isColumnHidden(i));
    actions.append(myAct);
  }

  hideshow_column->setAttribute(Qt::WA_DeleteOnClose);
  connect(hideshow_column, &QMenu::triggered, this,
          CAPTURE_DEFAULT_THIS (QAction *act) {
    int col;
    struct player_dlg_column *pcol;

    if (!act) {
      return;
    }

    col = actions.indexOf(act);
    fc_assert_ret(col >= 0);
    pcol = &player_dlg_columns[col];
    pcol->show = !pcol->show;
    setColumnHidden(col, !isColumnHidden(col));
    if (!isColumnHidden(col) && columnWidth(col) <= 5)
      setColumnWidth(col, 100);
  });

  hideshow_column->popup(QCursor::pos());
}

/**********************************************************************//**
  Returns information about column if hidden
**************************************************************************/
QVariant plr_model::hide_data(int section) const
{
  struct player_dlg_column *pcol;

  pcol = &player_dlg_columns[section];

  return pcol->show;
}

/**********************************************************************//**
  Hides columns in plr widget, depending on info from plr_list
**************************************************************************/
void plr_widget::hide_columns()
{
  int col;

  for (col = 0; col < list_model->columnCount(); col++) {
    if (!list_model->hide_data(col).toBool()) {
      setColumnHidden(col, !isColumnHidden(col));
    }
  }
}

/**********************************************************************//**
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
  QString etax, esci, elux, egold, egov;
  QString cult;
  QString nl = "<br>";
  QStringList sorted_list_a;
  QStringList sorted_list_b;
  struct player *pplayer;
  int a, b;
  bool added;
  bool entry_exist = false;
  struct player *me;
  Tech_type_id tech_id;
  bool global_observer = client_is_global_observer();
  QString rule;
  bool intel;

  other_player = nullptr;
  intel_str.clear();
  tech_str.clear();
  ally_str.clear();
  wonder_str.clear();

  if (indexes.isEmpty()) {
    selected_player = nullptr;
    plr->update_report(false);
    return;
  }

  index = indexes.at(0);
  qvar = index.data(Qt::UserRole);
  pplayer = reinterpret_cast<player *>(qvar.value<void *>());
  selected_player = pplayer;
  other_player = pplayer;

  if (!pplayer->is_alive) {
    plr->update_report(false);
    return;
  }

  me = client_player();
  pcity = player_primary_capital(pplayer);
  research = research_get(pplayer);

  switch (research->researching) {
  case A_UNKNOWN:
    res = _("(Unknown)");
    break;
  case A_UNSET:
    if (global_observer || team_has_embassy(me->team, pplayer)) {
      res = _("(none)");
    } else {
      res = _("(Unknown)");
    }
    break;
  default:
    res = QString(research_advance_name_translation(research,
                                                    research->researching))
          + sp + "(" + QString::number(research->bulbs_researched) + "/"
          + QString::number(research->client.researching_cost) + ")";
    break;
  }
  if (global_observer || team_has_embassy(me->team, pplayer)) {
    etax = QString::number(pplayer->economic.tax) + "%";
    esci = QString::number(pplayer->economic.science) + "%";
    elux = QString::number(pplayer->economic.luxury) + "%";
    cult = QString::number(pplayer->client.culture);
  } else {
    etax = _("(Unknown)");
    esci = _("(Unknown)");
    elux = _("(Unknown)");
    cult = _("(Unknown)");
  }

  intel = global_observer || could_intel_with_player(me, pplayer);

  if (intel || pplayer == me) {
    egold = QString::number(pplayer->economic.gold);
    egov = QString(government_name_for_player(pplayer));
  } else {
    egold = _("(Unknown)");
    egov = _("(Unknown)");
  }
  // Formatting rich text
  intel_str =
    // TRANS: this and similar literal strings interpreted as (Qt) HTML
    QString("<table><tr><td><b>") + _("Nation") + QString("</b></td><td>")
    + QString(nation_adjective_for_player(pplayer)).toHtmlEscaped()
    + QString("</td><tr><td><b>") + _("Ruler:") + QString("</b></td><td>")
    + QString(title_for_player(pplayer, tbuf, sizeof(tbuf)))
      .toHtmlEscaped()
    + QString("</td></tr><tr><td><b>") + _("Government:")
    + QString("</b></td><td>") + egov.toHtmlEscaped()
    + QString("</td></tr><tr><td><b>") + _("Capital:")
    + QString("</b></td><td>")
    + QString(((!pcity) ? _("(Unknown)") : city_name_get(pcity)))
      .toHtmlEscaped()
    + QString("</td></tr><tr><td><b>") + _("Gold:")
    + QString("</b></td><td>") + egold.toHtmlEscaped()
    + QString("</td></tr><tr><td><b>") + _("Tax:")
    + QString("</b></td><td>") + etax.toHtmlEscaped()
    + QString("</td></tr><tr><td><b>") + _("Science:")
    + QString("</b></td><td>") + esci.toHtmlEscaped()
    + QString("</td></tr><tr><td><b>") + _("Luxury:")
    + QString("</b></td><td>") + elux.toHtmlEscaped()
    + QString("</td></tr><tr><td><b>") + _("Researching:")
    + QString("</b></td><td>") + res.toHtmlEscaped()
    + QString("<td></tr><tr><td><b>") + _("Culture:")
    + QString("</b></td><td>") + cult.toHtmlEscaped()

    // HACK: Reserve extra line of space in case some line wraps to two later
    + QString("</td>\u200B<td>")
    + QString("</td></table>");

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
      if (static_cast<int>(state->type) == i && intel) {
        if (!added) {
          ally_str = ally_str  + QString("<b>")
                     + QString(diplstate_type_translated_name(
                                 static_cast<diplstate_type>(i)))
                       .toHtmlEscaped()
                     + ": "  + QString("</b>") + nl;
          added = true;
        }
        if (gives_shared_vision(pplayer, other)) {
          ally_str = ally_str + "(◐‿◑)";
        }
        ally_str = ally_str
          + QString(nation_plural_for_player(other)).toHtmlEscaped()
          + ", ";
        entry_exist = true;
      }
    } players_iterate_alive_end;
    if (entry_exist) {
      ally_str.replace(ally_str.lastIndexOf(","), 1, ".");
    }
  }
  my_research = research_get(me);

  if (!global_observer) {
    if (team_has_embassy(me->team, pplayer) && me != pplayer) {
      a = 0;
      b = 0;
      techs_known = QString(_("<b>Techs unknown by %1:</b>")).
                    arg(QString(nation_plural_for_player(pplayer))
                        .toHtmlEscaped());
      techs_unknown = QString(_("<b>Techs unknown by you :</b>"));

      advance_iterate(padvance) {
        tech_id = advance_number(padvance);
        if (research_invention_state(my_research, tech_id) == TECH_KNOWN
            && (research_invention_state(research, tech_id)
                != TECH_KNOWN)) {
          a++;
          sorted_list_a << research_advance_name_translation(research,
                                                             tech_id);
        }
        if (research_invention_state(my_research, tech_id) != TECH_KNOWN
            && (research_invention_state(research, tech_id) == TECH_KNOWN)) {
          b++;
          sorted_list_b << research_advance_name_translation(research,
                                                             tech_id);
        }
      } advance_iterate_end;
      sorted_list_a.sort(Qt::CaseInsensitive);
      sorted_list_b.sort(Qt::CaseInsensitive);
      foreach (res, sorted_list_a) {
        techs_known = techs_known + QString("<i>")
                      + res.toHtmlEscaped() + ","
                      + QString("</i>") + sp;
      }
      foreach (res, sorted_list_b) {
        techs_unknown = techs_unknown + QString("<i>")
                        + res.toHtmlEscaped() + ","
                        + QString("</i>") + sp;
      }
      if (a == 0) {
        techs_known = techs_known + QString("<i>") + sp
                      + QString(Q_("?tech:None")) + QString("</i>");
      } else {
        techs_known.replace(techs_known.lastIndexOf(","), 1, ".");
      }
      if (b == 0) {
        techs_unknown = techs_unknown + QString("<i>") + sp
                        + QString(Q_("?tech:None")) + QString("</i>");
      } else {
        techs_unknown.replace(techs_unknown.lastIndexOf(","), 1, ".");
      }
      tech_str = techs_known + nl + techs_unknown;
    }
  } else {
    tech_str = QString(_("<b>Techs known by %1:</b>")).
               arg(QString(nation_plural_for_player(pplayer)).toHtmlEscaped());
    advance_iterate(padvance) {
      tech_id = advance_number(padvance);
      if (research_invention_state(research, tech_id) == TECH_KNOWN) {
        sorted_list_a << research_advance_name_translation(research, tech_id);
      }
    } advance_iterate_end;
    sorted_list_a.sort(Qt::CaseInsensitive);
    foreach (res, sorted_list_a) {
      tech_str = tech_str + QString("<i>") + res.toHtmlEscaped() + ","
                    + QString("</i>") + sp;
    }
  }

  switch (game.info.small_wonder_visibility) {
  case WV_ALWAYS:
    rule = _("All Wonders are known");
    break;
  case WV_NEVER:
    rule = _("Small Wonders not known");
    break;
  case WV_EMBASSY:
    rule = _("Small Wonders visible if we have an embassy");
    break;
  }

  wonder_str = "<b>"
    + QString(_("Wonders of %1 Empire")).arg(QString(nation_plural_for_player(pplayer)).toHtmlEscaped())
    + "</b>" + nl + QString(_("Rule: ")) + rule + nl;

  improvement_iterate(impr) {
    if (is_wonder(impr)) {
      const char *cityname;
      QString notes;
      QString wonstr;

      if (wonder_is_built(pplayer, impr)) {
        struct city *wcity = city_from_wonder(pplayer, impr);

        if (wcity != nullptr) {
          cityname = city_name_get(wcity);
        } else {
          cityname = _("(unknown city)");
        }
        if (improvement_obsolete(pplayer, impr, nullptr)) {
          notes = _(" (obsolete)");
        }
      } else if (wonder_is_lost(pplayer, impr)) {
        cityname = _("(lost)");
      } else {
        continue;
      }

      if (is_great_wonder(impr)) {
        wonstr = QString("<b>") + improvement_name_translation(impr) + QString("</b>");
      } else {
        wonstr = improvement_name_translation(impr);
      }

      wonder_str += wonstr + QString(" ") + cityname + notes + nl;
    }
  } improvement_iterate_end;

  plr->update_report(false);
}

/**********************************************************************//**
  Returns model used in widget
**************************************************************************/
plr_model *plr_widget::get_model() const
{
  return list_model;
}

/**********************************************************************//**
  Destructor for player widget
**************************************************************************/
plr_widget::~plr_widget()
{
  delete pid;
  delete list_model;
  delete filter_model;
  gui()->qt_settings.player_repo_sort_col = header()->sortIndicatorSection();
  gui()->qt_settings.player_report_sort = header()->sortIndicatorOrder();
}

/**********************************************************************//**
  Constructor for plr_report
**************************************************************************/
plr_report::plr_report():QWidget()
{
  QFrame *line;

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
  wonder_label = new QLabel;
  wonder_label->setFrameStyle(QFrame::StyledPanel);
  wonder_label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  wonder_label->setWordWrap(true);
  wonder_label->setTextFormat(Qt::RichText);
  meet_but = new QPushButton;
  meet_but->setText(_("Meet"));
  cancel_but = new QPushButton;
  cancel_but->setText(_("Cancel Treaty"));
  withdraw_but = new QPushButton;
  withdraw_but->setText(_("Withdraw Vision"));
  toggle_ai_but = new QPushButton;
  toggle_ai_but->setText(_("AI Mode"));

  show_relations = new QPushButton;
  if (gui_options.gui_qt_show_relations_panel) {
    show_relations->setText(_("Hide Relations"));
  } else {
    show_relations->setText(_("Show Relations"));
  }

  show_techs = new QPushButton;
  if (gui_options.gui_qt_show_techs_panel) {
    show_techs->setText(_("Hide Techs"));
  } else {
    show_techs->setText(_("Show Techs"));
  }

  show_wonders = new QPushButton;
  if (gui_options.gui_qt_show_wonders_panel) {
    show_wonders->setText(_("Hide Wonders"));
  } else {
    show_wonders->setText(_("Show Wonders"));
  }

  meet_but->setDisabled(true);
  cancel_but->setDisabled(true);
  withdraw_but->setDisabled(true);
  v_splitter->addWidget(plr_wdg);
  h_splitter->addWidget(plr_label);
  h_splitter->addWidget(ally_label);
  h_splitter->addWidget(tech_label);
  h_splitter->addWidget(wonder_label);
  ally_label->setVisible(gui_options.gui_qt_show_relations_panel);
  tech_label->setVisible(gui_options.gui_qt_show_techs_panel);
  wonder_label->setVisible(gui_options.gui_qt_show_wonders_panel);
  v_splitter->addWidget(h_splitter);
  layout->addWidget(v_splitter);
  hlayout->addWidget(meet_but);
  hlayout->addWidget(cancel_but);
  hlayout->addWidget(withdraw_but);
  hlayout->addWidget(toggle_ai_but);
  line = new QFrame();
  line->setFrameShape(QFrame::VLine);
  hlayout->addWidget(line);
  hlayout->addWidget(show_relations);
  hlayout->addWidget(show_techs);
  hlayout->addWidget(show_wonders);
  hlayout->addStretch();
  layout->addLayout(hlayout);
  connect(meet_but, &QAbstractButton::pressed, this, &plr_report::req_meeeting);
  connect(cancel_but, &QAbstractButton::pressed, this, &plr_report::req_caancel_threaty);
  connect(withdraw_but, &QAbstractButton::pressed, this, &plr_report::req_wiithdrw_vision);
  connect(toggle_ai_but, &QAbstractButton::pressed, this, &plr_report::toggle_ai_mode);
  connect(show_relations, &QAbstractButton::pressed, this, &plr_report::show_relations_toggle);
  connect(show_techs, &QAbstractButton::pressed, this, &plr_report::show_techs_toggle);
  connect(show_wonders, &QAbstractButton::pressed, this, &plr_report::show_wonders_toggle);
  setLayout(layout);

  if (gui()->qt_settings.player_repo_sort_col != -1) {
    plr_wdg->sortByColumn(gui()->qt_settings.player_repo_sort_col,
                          gui()->qt_settings.player_report_sort);
  }
}

/**********************************************************************//**
  Destructor for plr_report
**************************************************************************/
plr_report::~plr_report()
{
  gui()->remove_repo_dlg("PLR");
}

/**********************************************************************//**
  Adds plr_report to tab widget
**************************************************************************/
void plr_report::init()
{
  gui()->gimme_place(this, "PLR");
  index = gui()->add_game_tab(this);
  gui()->game_tab_widget->setCurrentIndex(index);
}

/**********************************************************************//**
  Public function to call meeting
**************************************************************************/
void plr_report::call_meeting()
{
  if (meet_but->isEnabled()) {
    req_meeeting();
  }
}

/**********************************************************************//**
  Slot for canceling threaty (name changed to cheat autoconnect, and
  doubled execution)
**************************************************************************/
void plr_report::req_caancel_threaty()
{
  dsend_packet_diplomacy_cancel_pact(&client.conn,
                                     player_number(other_player),
                                     CLAUSE_CEASEFIRE);
}

/**********************************************************************//**
  Slot for meeting request
**************************************************************************/
void plr_report::req_meeeting()
{
  struct treaty *ptreaty = find_treaty(client_player(), other_player);

  if (ptreaty != nullptr) {
    qtg_init_meeting(ptreaty, other_player, client_player());
  } else {
    dsend_packet_diplomacy_init_meeting_req(&client.conn,
                                            player_number(other_player));
  }
}

/**********************************************************************//**
  Slot for withdrawing vision
**************************************************************************/
void plr_report::req_wiithdrw_vision()
{
  dsend_packet_diplomacy_cancel_pact(&client.conn,
                                     player_number(other_player),
                                     CLAUSE_VISION);
}

/**********************************************************************//**
  Slot for changing AI mode
**************************************************************************/
void plr_report::toggle_ai_mode()
{
  if (plr_wdg->other_player == client_player()) {
    send_chat_printf("/away \"%s\"",
                     player_name(plr_wdg->other_player));
    return;
  } else {
    QMenu *ai_menu = new QMenu(this);
    QAction *toggle_ai_act;
    int level;

    toggle_ai_act = new QAction(_("Toggle AI Mode"), nullptr);
    ai_menu->addAction(toggle_ai_act);

    ai_menu->addSeparator();
    for (level = 0; level < AI_LEVEL_COUNT; level++) {
      if (is_settable_ai_level(static_cast<ai_level>(level))) {
        QAction *ai_level_act;
        QString ln = ai_level_translated_name(static_cast<ai_level>(level));

        ai_level_act = new QAction(ln, nullptr);
        ai_level_act->setData(QVariant::fromValue(level));
        ai_menu->addAction(ai_level_act);
      }
    }

    ai_menu->setAttribute(Qt::WA_DeleteOnClose);
    connect(ai_menu, &QMenu::triggered,
            CAPTURE_DEFAULT_THIS (QAction *act) {
      int lvl;

      if (act == toggle_ai_act) {
        send_chat_printf("/aitoggle \"%s\"",
                         player_name(plr_wdg->other_player));
        return;
      }

      if (act && act->isVisible()) {
        lvl = act->data().toInt();
        if (is_human(plr_wdg->other_player)) {
          send_chat_printf("/aitoggle \"%s\"",
                           player_name(plr_wdg->other_player));
        }
        send_chat_printf("/%s %s", ai_level_cmd(static_cast<ai_level>(lvl)),
                         player_name(plr_wdg->other_player));
      }
    });

    ai_menu->popup(QCursor::pos());
  }
}

/**********************************************************************//**
  Slot to enable/disable relations display
**************************************************************************/
void plr_report::show_relations_toggle()
{
  if (ally_label->isVisible()) {
    ally_label->hide();
    show_relations->setText(_("Show Relations"));
    gui_options.gui_qt_show_relations_panel = FALSE;
  } else {
    ally_label->show();
    show_relations->setText(_("Hide Relations"));
    gui_options.gui_qt_show_relations_panel = TRUE;
  }
}

/**********************************************************************//**
  Slot to enable/disable techs display
**************************************************************************/
void plr_report::show_techs_toggle()
{
  if (tech_label->isVisible()) {
    tech_label->hide();
    show_techs->setText(_("Show Techs"));
    gui_options.gui_qt_show_techs_panel = FALSE;
  } else {
    tech_label->show();
    show_techs->setText(_("Hide Techs"));
    gui_options.gui_qt_show_techs_panel = TRUE;
  }
}

/**********************************************************************//**
  Slot to enable/disable wonders display
**************************************************************************/
void plr_report::show_wonders_toggle()
{
  if (wonder_label->isVisible()) {
    wonder_label->hide();
    show_wonders->setText(_("Show Wonders"));
    gui_options.gui_qt_show_wonders_panel = FALSE;
  } else {
    wonder_label->show();
    show_wonders->setText(_("Hide Wonders"));
    gui_options.gui_qt_show_wonders_panel = TRUE;
  }
}

/**********************************************************************//**
  Handle mouse click
**************************************************************************/
void plr_widget::mousePressEvent(QMouseEvent *event)
{
  QModelIndex index = this->indexAt(event->pos());

  if (index.isValid() && event->button() == Qt::RightButton
      && can_client_issue_orders()) {
     plr->call_meeting();
  }

  QTreeView::mousePressEvent(event);
}

/**********************************************************************//**
  Updates widget
**************************************************************************/
void plr_report::update_report(bool update_selection)
{
  QModelIndex qmi;
  int player_count = 0;
  struct player *cplayer;

  /* First make sure number of rows is correct - there can be new
   * nations because of civil war. We don't want our actions to
   * trigger automatic sorting of the model while number of rows
   * is not correct. */
  players_iterate(pplayer) {
    if ((is_barbarian(pplayer))) {
      continue;
    }
    player_count++;
  } players_iterate_end;

  if (player_count != plr_wdg->get_model()->rowCount()) {
    plr_wdg->get_model()->populate();
  }

  // Force updating selected player information
  if (update_selection) {
    qmi = plr_wdg->currentIndex();
    if (qmi.isValid()) {
      plr_wdg->clearSelection();
      plr_wdg->setCurrentIndex(qmi);
    }
  }

  plr_wdg->header()->resizeSections(QHeaderView::ResizeToContents);
  meet_but->setDisabled(true);
  cancel_but->setDisabled(true);
  withdraw_but->setDisabled(true);
  plr_label->setText(plr_wdg->intel_str);
  ally_label->setText(plr_wdg->ally_str);
  tech_label->setText(plr_wdg->tech_str);
  wonder_label->setText(plr_wdg->wonder_str);
  other_player = plr_wdg->other_player;
  if (other_player == nullptr || !can_client_issue_orders()) {
    return;
  }

  cplayer = client_player();
  if (cplayer != nullptr) {
    if (other_player != cplayer) {

      // We keep button sensitive in case of DIPL_SENATE_BLOCKING, so that player
      // can request server side to check requirements of those effects with omniscience
      if (pplayer_can_cancel_treaty(cplayer, other_player) != DIPL_ERROR) {
        cancel_but->setEnabled(true);
      }
      toggle_ai_but->setText(_("AI Mode"));
    } else {
      toggle_ai_but->setText(_("Away Mode"));
    }
  } else {
    toggle_ai_but->setText(_("AI Mode"));
  }
  if (gives_shared_vision(cplayer, other_player)
      && !players_on_same_team(cplayer, other_player)) {
    withdraw_but->setEnabled(true);
  }
  if (can_meet_with_player(other_player)) {
    meet_but->setEnabled(true);
  }

  plr_wdg->restore_selection();
}

/**********************************************************************//**
  Display the player list dialog.  Optionally raise it.
**************************************************************************/
void popup_players_dialog(bool raise)
{
  int i;
  QWidget *w;

  if (!gui()->is_repo_dlg_open("PLR")) {
    plr_report *pr = new plr_report;

    pr->init();
    pr->update_report();
  } else {
    plr_report *pr;

    i = gui()->gimme_index_of("PLR");
    w = gui()->game_tab_widget->widget(i);
    if (w->isVisible()) {
      gui()->game_tab_widget->setCurrentIndex(0);
      return;
    }
    pr = reinterpret_cast<plr_report*>(w);
    gui()->game_tab_widget->setCurrentWidget(pr);
    pr->update_report();
  }
}

/**********************************************************************//**
  Update all information in the player list dialog.
**************************************************************************/
void real_players_dialog_update(void *unused)
{
  int i;
  plr_report *pr;
  QWidget *w;

  if (gui()->is_repo_dlg_open("PLR")) {
    i = gui()->gimme_index_of("PLR");
    if (gui()->game_tab_widget->currentIndex() == i) {
      w = gui()->game_tab_widget->widget(i);
      pr = reinterpret_cast<plr_report *>(w);
      pr->update_report();
    }
  }
}

/**********************************************************************//**
  Closes players report
**************************************************************************/
void popdown_players_report()
{
  int i;
  plr_report *pr;
  QWidget *w;

  if (gui()->is_repo_dlg_open("PLR")) {
    i = gui()->gimme_index_of("PLR");
    fc_assert(i != -1);
    w = gui()->game_tab_widget->widget(i);
    pr = reinterpret_cast<plr_report *>(w);
    pr->deleteLater();
  }
}
