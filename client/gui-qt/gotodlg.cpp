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
#include <QCheckBox>
#include <QHBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QTableWidget>

// common
#include "game.h"

// client
#include "client_main.h"
#include "control.h"
#include "goto.h"
#include "text.h"

// gui-qt
#include "fc_client.h"
#include "gotodlg.h"
#include "qtg_cxxside.h"
#include "sprite.h"

#define SPECENUM_NAME gotodlg_columns

#define SPECENUM_VALUE0 GOTODLG_CITY
#define SPECENUM_VALUE0NAME N_("City")
#define SPECENUM_VALUE1 GOTODLG_NATION
#define SPECENUM_VALUE1NAME N_("Nation")
#define SPECENUM_VALUE2 GOTODLG_CONTINENT
#define SPECENUM_VALUE2NAME N_("Continent")
#define SPECENUM_VALUE3 GOTODLG_BUILDING
#define SPECENUM_VALUE3NAME N_("Building")
#define SPECENUM_VALUE4 GOTODLG_AIRLIFT
#define SPECENUM_VALUE4NAME N_("Airlift")
#define SPECENUM_VALUE5 GOTODLG_SIZE
#define SPECENUM_VALUE5NAME N_("Size")
#define SPECENUM_VALUE6 GOTODLG_DISTANCE
#define SPECENUM_VALUE6NAME N_("Distance")
#define SPECENUM_VALUE7 GOTODLG_TRADE
#define SPECENUM_VALUE7NAME N_("Trade")

#define SPECENUM_COUNT NUM_GOTODLG_COLUMNS // number of columns in the goto dialog
#include "specenum_gen.h"


/***********************************************************************//**
  Constructor for goto_dialog
***************************************************************************/
goto_dialog::goto_dialog(QWidget *parent): qfc_dialog(parent)
{
  QStringList headers_lst;
  QHBoxLayout *hb;

  for (enum gotodlg_columns col = gotodlg_columns_begin();
    col != gotodlg_columns_end();
    col = gotodlg_columns_next(col)) {

    headers_lst << gotodlg_columns_name(col);
  }

  setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setParent(parent);
  goto_tab = new QTableWidget;
  goto_city = new QPushButton(_("&Goto"));
  airlift_city = new QPushButton(_("&Airlift"));
  close_but = new QPushButton(_("&Close"));
  layout = new QGridLayout;

  show_all = new QCheckBox;
  show_all->setChecked(false);
  show_all_label = new QLabel(_("Show All Cities"));
  show_all_label->setAlignment(Qt::AlignLeft);
  hb = new QHBoxLayout;
  hb->addWidget(show_all);
  hb->addWidget(show_all_label, Qt::AlignLeft);

  goto_tab->setProperty("showGrid", "false");
  goto_tab->setSelectionBehavior(QAbstractItemView::SelectRows);
  goto_tab->setEditTriggers(QAbstractItemView::NoEditTriggers);
  goto_tab->verticalHeader()->setVisible(false);
  goto_tab->horizontalHeader()->setVisible(true);
  goto_tab->setSelectionMode(QAbstractItemView::SingleSelection);
  goto_tab->setColumnCount(NUM_GOTODLG_COLUMNS);
  goto_tab->setHorizontalHeaderLabels(headers_lst);
  goto_tab->setSortingEnabled(true);
  goto_tab->horizontalHeader()->setSectionResizeMode(
                                             QHeaderView::ResizeToContents);

  layout->addWidget(goto_tab, 0, 0, 1, 4);
  layout->setRowStretch(0, 100);
  layout->addItem(hb, 1, 0, 1, 2);
  layout->addWidget(goto_city, 2, 0, 1, 1);
  layout->addWidget(airlift_city, 2, 1, 1, 1);
  layout->addWidget(close_but, 2, 3, 1, 1);

  if (width() < goto_tab->horizontalHeader()->width()) {
    resize(goto_tab->horizontalHeader()->width(), height());
  }
  connect(close_but, &QAbstractButton::clicked, this, &goto_dialog::close_dlg);
  connect(goto_city, &QAbstractButton::clicked, this, &goto_dialog::go_to_city);
  connect(airlift_city, &QAbstractButton::clicked, this, &goto_dialog::airlift_to);
#ifdef FC_QT6X_MODE
  // Qt-6.7
  connect(show_all, &QCheckBox::checkStateChanged,
          this, &goto_dialog::checkbox_changed);
#else  // FC_QT6X_MODE
  connect(show_all, &QCheckBox::stateChanged,
          this, &goto_dialog::checkbox_changed_depr);
#endif // FC_QT6X_MODE
  connect(goto_tab->selectionModel(),
          SIGNAL(selectionChanged(const QItemSelection &,
                                  const QItemSelection &)),
          SLOT(item_selected(const QItemSelection &,
                             const QItemSelection &)));

  setLayout(layout);
  original_tile = nullptr;
  setFocus();
}

/***********************************************************************//**
  Sets variables which must be destroyed later
***************************************************************************/
void goto_dialog::init()
{
  if (original_tile != nullptr) {
    tile_virtual_destroy(original_tile);
  }
  original_tile = tile_virtual_new(get_center_tile_mapcanvas());
}

/***********************************************************************//**
  Destructor for goto dialog
***************************************************************************/
goto_dialog::~goto_dialog()
{
}

/***********************************************************************//**
  Slot for checkbox 'all nations'
***************************************************************************/
void goto_dialog::checkbox_changed(Qt::CheckState state)
{
  update_dlg();
}

/***********************************************************************//**
  Slot for checkbox 'all nations'
  This deprecated slot is used in Qt < 6.7
***************************************************************************/
void goto_dialog::checkbox_changed_depr(int state)
{
  update_dlg();
}

/***********************************************************************//**
  User has chosen some city on table
***************************************************************************/
void goto_dialog::item_selected(const QItemSelection &sl,
                                const QItemSelection &ds)
{
  int i;
  int city_id;
  QModelIndex index;
  QModelIndexList indexes = sl.indexes();
  QTableWidgetItem *item;
  struct city *dest;
  bool can_airlift;

  if (indexes.isEmpty()) {
    return;
  }
  index = indexes.at(0);
  i = index.row();
  item = goto_tab->item(i, 0);
  city_id = item->data(Qt::UserRole).toInt();
  dest = game_city_by_number(city_id);
  if (dest == nullptr) {
    return;
  }
  center_tile_mapcanvas(city_tile(dest));
  can_airlift = false;
  unit_list_iterate(get_units_in_focus(), punit) {
    if (unit_can_airlift_to(&(wld.map), punit, dest)) {
      can_airlift = true;
      break;
    }
  } unit_list_iterate_end;

  if (can_airlift) {
    airlift_city->setEnabled(true);
  } else {
    airlift_city->setDisabled(true);
  }

}

/***********************************************************************//**
  Sorts dialog by default column (0)
***************************************************************************/
void goto_dialog::sort_def()
{
  goto_tab->sortByColumn(0, Qt::AscendingOrder);
}

/***********************************************************************//**
  Shows and moves widget
***************************************************************************/
void goto_dialog::show_me()
{
  QPoint p, final_p;
  p = QCursor::pos();
  p = parentWidget()->mapFromGlobal(p);
  final_p.setX(p.x());
  final_p.setY(p.y());
  if (p.x() + width() > parentWidget()->width()) {
    final_p.setX(parentWidget()->width() - width());
  }
  if (p.y() - height() < 0) {
    final_p.setY(height());
  }
  move(final_p.x(), final_p.y() - height());
  if (original_tile == nullptr) {
    init();
  }
  show();
}

/***********************************************************************//**
  Updates table in widget
***************************************************************************/
void goto_dialog::update_dlg()
{
  goto_tab->clearContents();
  goto_tab->setRowCount(0);
  goto_tab->setSortingEnabled(false);
  if (show_all->isChecked()) {
    players_iterate(pplayer) {
      fill_tab(pplayer);
    } players_iterate_end;
  } else {
    fill_tab(client_player());
  }
  goto_tab->setSortingEnabled(true);
  goto_tab->horizontalHeader()->setStretchLastSection(false);
  goto_tab->resizeRowsToContents();
  goto_tab->horizontalHeader()->setStretchLastSection(true);
}

/***********************************************************************//**
  Helper for function for filling table
***************************************************************************/
void goto_dialog::fill_tab(player *pplayer)
{
  int i;

  QString str;
  const char *at;
  QFont f = QApplication::font();
  QFontMetrics fm(f);
  int h;
  struct sprite *sprite;
  QPixmap *pix;
  QPixmap pix_scaled;
  QTableWidgetItem *item;
  unit *punit = nullptr;
  int rnum = 0;
  int rvalue = 0;
  enum route_direction rdir = RDIR_NONE;
  int rdir_value = 0;

  h = fm.height() + 6;
  i = goto_tab->rowCount();
  city_list_iterate(pplayer->cities, pcity) {
    goto_tab->insertRow(i);
    for (enum gotodlg_columns col = gotodlg_columns_begin();
      col != gotodlg_columns_end();
      col = gotodlg_columns_next(col)) {

      item = new QTableWidgetItem;
      str.clear();

      switch (col) {
      case GOTODLG_CITY:
        str = city_name_get(pcity);
        break;

      case GOTODLG_NATION:
        sprite = get_nation_flag_sprite(tileset, nation_of_player(pplayer));
        if (sprite != nullptr) {
          pix = sprite->pm;
          pix_scaled = pix->scaledToHeight(h);
          item->setData(Qt::DecorationRole, pix_scaled);
        }
        str = nation_adjective_translation(nation_of_player(pplayer));
        break;

      case GOTODLG_BUILDING:
        str = city_production_name_translation(pcity);
        break;

      case GOTODLG_CONTINENT:
        str.setNum(tile_continent(pcity->tile));
        while (str.length() < 3) {
          str.prepend('0');
        }
        break;

      case GOTODLG_AIRLIFT:
        at = get_airlift_text(get_units_in_focus(), pcity);
        if (at == nullptr) {
          str = "-";
        } else {
          str = at;
        }
        item->setTextAlignment(Qt::AlignHCenter);
        break;

      case GOTODLG_SIZE:
        str.setNum(pcity->size);
        while (str.length() < 2) {
          str.prepend('0');
        }
        break;

      case GOTODLG_DISTANCE:
        punit = head_of_units_in_focus();
        if (punit == nullptr) {
          str = "-";
          item->setTextAlignment(Qt::AlignHCenter);
        } else {
          str.setNum(sq_map_distance(pcity->tile, unit_tile(punit)));
          while (str.length() < 6) {
            str.prepend('0');
          }
        }
        break;

      case GOTODLG_TRADE:
        punit = head_of_units_in_focus();
        rnum = 0;
        rvalue = 0;
        rdir = RDIR_NONE;
        rdir_value = 0;
        if (punit != nullptr) {
          trade_routes_iterate(pcity, proute) {
            rnum++;
            rvalue += proute->value;
            if (punit->homecity == proute->partner) {
              rdir = proute->dir;
              rdir_value = proute->value;
            }
          } trade_routes_iterate_end;
        }

        if (rnum == 0) {
          str = "-";
          item->setTextAlignment(Qt::AlignHCenter);
        } else {
          str.setNum(rnum);
          str.append(" (");
          while (str.length() < 3) {
            str.prepend('0');
          }
          str.append(" (");
          if (rvalue < 100) {
            str.append('0');
          }
          if (rvalue < 10) {
            str.append('0');
          }
          str.append(QString::number(rvalue));
          str.append(") ");

          switch (rdir) {

          case RDIR_BIDIRECTIONAL:
            str.append("<< ");
            str.append(QString::number(rdir_value));
            str.append(" >>");
            break;

          case RDIR_FROM:
            str.append(QString::number(rdir_value));
            str.append(" >>");
            break;

          case RDIR_TO:
            str.append("<< ");
            str.append(QString::number(rdir_value));
            break;

          case RDIR_NONE:
            break;
          }
        }
        break;

      case NUM_GOTODLG_COLUMNS:
        fc_assert(col != NUM_GOTODLG_COLUMNS);
        break;
      }

      item->setText(str);
      item->setData(Qt::UserRole, pcity->id);
      goto_tab->setItem(i, col, item);
    }
    i++;
  } city_list_iterate_end;
}

/***********************************************************************//**
  Slot for airlifting unit
***************************************************************************/
void goto_dialog::airlift_to()
{
  struct city *pdest;

  if (goto_tab->currentRow() == -1) {
    return;
  }
  pdest = game_city_by_number(goto_tab->item(goto_tab->currentRow(),
                                             0)->data(Qt::UserRole).toInt());
  if (pdest) {
    unit_list_iterate(get_units_in_focus(), punit) {
      if (unit_can_airlift_to(&(wld.map), punit, pdest)) {
        request_unit_airlift(punit, pdest);
      }
    } unit_list_iterate_end;
  }
}

/***********************************************************************//**
  Slot for goto for city
***************************************************************************/
void goto_dialog::go_to_city()
{
  struct city *pdest;

  if (goto_tab->currentRow() == -1) {
    return;
  }

  pdest = game_city_by_number(goto_tab->item(goto_tab->currentRow(),
                                             0)->data(Qt::UserRole).toInt());
  if (pdest) {
    unit_list_iterate(get_units_in_focus(), punit) {
      send_goto_tile(punit, pdest->tile);
    } unit_list_iterate_end;
  }
}

/***********************************************************************//**
  Slot for hiding dialog
***************************************************************************/
void goto_dialog::close_dlg()
{
  if (original_tile != nullptr) {
    center_tile_mapcanvas(original_tile);
    tile_virtual_destroy(original_tile);
    original_tile = nullptr;
  }

  hide();
}

/***********************************************************************//**
  Paints rectangles for goto_dialog
***************************************************************************/
void goto_dialog::paint(QPainter *painter, QPaintEvent *event)
{
  painter->setBrush(QColor(0, 0, 30, 85));
  painter->drawRect(0, 0, width(), height());
  painter->setBrush(QColor(0, 0, 0, 85));
  painter->drawRect(5, 5, width() - 10, height() - 10);
}

/***********************************************************************//**
  Paint event for goto_dialog
***************************************************************************/
void goto_dialog::paintEvent(QPaintEvent *event)
{
  QPainter painter;

  painter.begin(this);
  paint(&painter, event);
  painter.end();
}

/***********************************************************************//**
  Popup a dialog to have the focus unit goto to a city.
***************************************************************************/
void popup_goto_dialog(void)
{
  goto_dialog *gtd;

  if (C_S_RUNNING != client_state()) {
    return;
  }
  if (get_num_units_in_focus() == 0) {
    return;
  }
  if (!client_has_player()) {
    return;
  }

  gtd = gui()->gtd;

  if (gtd != nullptr) {
    gtd->init();
    gtd->update_dlg();
    gtd->sort_def();
    gtd->show_me();
  }
}
