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

#ifndef FC__DIPLODLG_H
#define FC__DIPLODLG_H

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

// Qt
#include <QMap>
#include <QTabWidget>

extern "C" {
#include "diplodlg_g.h"
}

// common
#include "research.h"

//  client
#include "game.h"
#include "sprite.h"
#include "tilespec.h"

class QCloseEvent;
class QGridLayout;
class QIcon;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTableWidgetItem;

/****************************************************************************
  Diplomacy tab for one nation
****************************************************************************/
class diplo_wdg: public QWidget
{
  Q_OBJECT
  QGridLayout *layout;
  QLabel *plr1_accept;
  QLabel *plr2_accept;
  QPushButton *accept_treaty;
  QPushButton *cancel_treaty;
  QTableWidget *text_edit;

public:
  diplo_wdg(struct treaty *ptreaty,
            struct player *they, struct player *initiator);
  ~diplo_wdg();
  void update_wdg();
  void set_index(int ind);
  int get_index();
  struct treaty *treaty;

private slots:
  void all_advances();
  void dbl_click(QTableWidgetItem *item);
  void give_advance(int tech);
  void give_city(int city_num);
  void give_embassy();
  void give_shared_vision();
  void gold_changed1(int val);
  void gold_changed2(int val);
  void pact_allianze();
  void pact_ceasfire();
  void pact_peace();
  void response_accept();
  void response_cancel();
  void sea_map_clause();
  void show_menu(struct player *pplayer);
  void show_menu_p1();
  void show_menu_p2();
  void world_map_clause();
  void restore_pixmap();

protected:
  void closeEvent(QCloseEvent *event);
private:
  struct player *plr1;
  struct player *plr2;
  int active_menu;
  struct player *curr_player;
  bool p1_accept;
  bool p2_accept;
  int index;
};

/****************************************************************************
  Diplomacy dialog containing many diplo_wdg
****************************************************************************/
class diplo_dlg: public QTabWidget
{
  Q_OBJECT
  QMap<struct player *, diplo_wdg *> treaty_list;

public:
  diplo_dlg(struct treaty *ptreaty, struct player *they,
            struct player *initiator);
  ~diplo_dlg();
  void reactivate();
  void update_dlg();
  bool init(bool raise);
  diplo_wdg *find_widget(struct player *they);
  void close_widget(struct player *they);
  void add_widget(struct treaty *ptreaty, struct player *they,
                  struct player *initiator);
  void make_active(struct player *party);

private:
  int index;
};

#endif // FC__DIPLODLG_H
