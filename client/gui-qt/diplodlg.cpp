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
#include <QCloseEvent>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QSpinBox>
#include <QPainter>
#include <QPushButton>

// common
#include "government.h"
#include "player.h"

// client
#include "client_main.h"
#include "svgflag.h"

// gui-qt
#include "colors.h"
#include "diplodlg.h"
#include "fc_client.h"
#include "gui_main.h"
#include "sidebar.h"

extern "C" {
  int client_player_number(void);
  struct player *player_by_number(const int player_id);
}

typedef advance *p_advance;
typedef city *p_city;

#define FLAG_HEIGHT_DIPLDLG 150

/************************************************************************//**
  Constructor for diplomacy widget
****************************************************************************/
diplo_wdg::diplo_wdg(struct treaty *ptreaty,
                     struct player *they, struct player *initiator): QWidget()
{
  struct player *we;
  color *colr;
  QString text;
  QString text2;
  QString text_tooltip;
  QLabel *plr1_label;
  QLabel *plr2_label;
  QLabel *label;
  QLabel *label2;
  QLabel *label3;
  QLabel *label4;
  QPushButton *add_clause1;
  QPushButton *add_clause2;
  QPalette palette;
  struct sprite *sprite, *sprite2;
  char plr_buf[4 * MAX_LEN_NAME];
  const struct player_diplstate *state;
  QHeaderView *header;
  struct color *textcolors[2] = {
    get_color(tileset, COLOR_MAPVIEW_CITYTEXT),
    get_color(tileset, COLOR_MAPVIEW_CITYTEXT_DARK)
  };
  int clause_column;
  bool svg = is_svg_flag_enabled();

  if (they == initiator) {
    we = client_player();
  } else {
    we = initiator;
  }
  p1_accept = false;
  p2_accept = false;
  plr1 = we;
  plr2 = they;
  layout = new QGridLayout;

  treaty = ptreaty;
  state = player_diplstate_get(we, they);
  text_tooltip = QString(diplstate_type_translated_name(state->type));
  if (state->turns_left > 0) {
    text_tooltip = text_tooltip + " (";
    text_tooltip = text_tooltip + QString(PL_("%1 turn left",
                                              "%1 turns left",
                                             state->turns_left))
                                          .arg(state->turns_left);
    text_tooltip = text_tooltip + ")";
  }
  label3 = new QLabel;
  text = "<b><h3><center>"
         + QString(nation_plural_for_player(we))
           .toHtmlEscaped()
         + "</center></h3></b>";
  colr = get_player_color(tileset, we);
  text = "<style>h3{background-color: "
         + colr->qcolor.name() + ";" + "color: " + color_best_contrast(colr,
             textcolors, ARRAY_SIZE(textcolors))->qcolor.name()
         + "}</style>" + text;
  label3->setText(text);
  label3->setMinimumWidth(300);
  label4 = new QLabel;
  text = "<b><h3><center>"
         + QString(nation_plural_for_player(they))
           .toHtmlEscaped()
         + "</center></h3></b></body>";
  colr = get_player_color(tileset, they);
  text = "<style>h3{background-color: "
         + colr->qcolor.name() + ";" + "color: " + color_best_contrast(colr,
             textcolors, ARRAY_SIZE(textcolors))->qcolor.name()
         + "}</style>" + text;
  label4->setMinimumWidth(300);
  label4->setText(text);
  layout->addWidget(label3, 0, 5);
  layout->addWidget(label4, 5, 5);
  plr1_label = new QLabel;
  label = new QLabel;
  sprite = get_nation_flag_sprite(tileset,
                                  nation_of_player(we));
  if (sprite != nullptr) {
    if (svg) {
      plr1_label->setPixmap(sprite->pm->scaledToHeight(FLAG_HEIGHT_DIPLDLG));
    } else  {
      plr1_label->setPixmap(*sprite->pm);
    }
  } else {
    plr1_label->setText("FLAG MISSING");
  }
  text = ruler_title_for_player(we, plr_buf, sizeof(plr_buf));
  text = "<b><center>" + text.toHtmlEscaped() + "</center></b>";
  label->setText(text);
  plr1_accept = new QLabel;
  layout->addWidget(plr1_label, 1, 0);
  layout->addWidget(label, 1, 5);
  layout->addWidget(plr1_accept, 1, 10);
  label2 = new QLabel;
  sprite2 = get_nation_flag_sprite(tileset,
                                   nation_of_player(they));
  plr2_label = new QLabel;
  if (sprite2 != nullptr) {
    if (svg) {
      plr2_label->setPixmap(sprite2->pm->scaledToHeight(FLAG_HEIGHT_DIPLDLG));
    } else {
      plr2_label->setPixmap(*sprite2->pm);
    }
  } else {
    plr2_label->setText("FLAG MISSING");
  }
  text2 = title_for_player(they, plr_buf, sizeof(plr_buf));
  text2 = "<b><center>" + text2.toHtmlEscaped() + "</center></b>";
  label2->setText(text2);
  plr2_accept = new QLabel;
  layout->addWidget(plr2_label, 6, 0);
  layout->addWidget(label2, 6, 5);
  layout->addWidget(plr2_accept, 6, 10);

  if (clause_enabled(CLAUSE_GOLD)) {
    QLabel *goldlab1;
    QLabel *goldlab2;
    QSpinBox *gold_edit1;
    QSpinBox *gold_edit2;

    goldlab1 = new QLabel(_("Gold:"));
    goldlab1->setAlignment(Qt::AlignRight);
    goldlab2 = new QLabel(_("Gold:"));
    goldlab2->setAlignment(Qt::AlignRight);
    gold_edit1 = new QSpinBox;
    gold_edit2 = new QSpinBox;
    gold_edit1->setMinimum(0);
    gold_edit2->setMinimum(0);
    gold_edit1->setFocusPolicy(Qt::ClickFocus);
    gold_edit2->setFocusPolicy(Qt::ClickFocus);
    gold_edit2->setMaximum(we->economic.gold);
    gold_edit1->setMaximum(they->economic.gold);
    connect(gold_edit1, SIGNAL(valueChanged(int)), SLOT(gold_changed1(int)));
    connect(gold_edit2, SIGNAL(valueChanged(int)), SLOT(gold_changed2(int)));
    layout->addWidget(goldlab1, 7, 4);
    layout->addWidget(goldlab2, 3, 4);
    layout->addWidget(gold_edit1, 7, 5);
    layout->addWidget(gold_edit2, 3, 5);
    clause_column = 6;
  } else {
    clause_column = 5;
  }

  add_clause1 = new QPushButton(style()->standardIcon(QStyle::SP_ArrowRight),
                                _("Add Clause..."));
  add_clause2 = new QPushButton(style()->standardIcon(QStyle::SP_ArrowRight),
                                _("Add Clause..."));
  add_clause1->setFocusPolicy(Qt::ClickFocus);
  add_clause2->setFocusPolicy(Qt::ClickFocus);
  connect(add_clause1, &QAbstractButton::clicked, this, &diplo_wdg::show_menu_p2);
  connect(add_clause2, &QAbstractButton::clicked, this, &diplo_wdg::show_menu_p1);
  layout->addWidget(add_clause1, 7, clause_column);
  layout->addWidget(add_clause2, 3, clause_column);

  text_edit = new QTableWidget();
  text_edit->setColumnCount(1);
  text_edit->setProperty("showGrid", "false");
  text_edit->setProperty("selectionBehavior", "SelectRows");
  text_edit->setEditTriggers(QAbstractItemView::NoEditTriggers);
  text_edit->verticalHeader()->setVisible(false);
  text_edit->horizontalHeader()->setVisible(false);
  text_edit->setSelectionMode(QAbstractItemView::SingleSelection);
  text_edit->setFocusPolicy(Qt::NoFocus);
  header = text_edit->horizontalHeader();
  header->setStretchLastSection(true);
  connect(text_edit, &QTableWidget::itemDoubleClicked,
          this, &diplo_wdg::dbl_click);
  text_edit->clearContents();
  text_edit->setRowCount(0);
  layout->addWidget(text_edit, 9, 0, 8, 11);
  accept_treaty =
    new QPushButton(style()->standardIcon(QStyle::SP_DialogYesButton),
                    _("Accept treaty"));
  cancel_treaty =
    new QPushButton(style()->standardIcon(QStyle::SP_DialogNoButton),
                    _("Cancel meeting"));
  connect(accept_treaty, &QAbstractButton::clicked, this, &diplo_wdg::response_accept);
  connect(cancel_treaty, &QAbstractButton::clicked, this, &diplo_wdg::response_cancel);
  layout->addWidget(accept_treaty, 17, 5);
  layout->addWidget(cancel_treaty, 17, 6);

  if (client_player() != they) {
    label4->setToolTip(text_tooltip);
    plr2_label->setToolTip(text_tooltip);
  }
  if (client_player() != we) {
    label3->setToolTip(text_tooltip);
    plr1_label->setToolTip(text_tooltip);
  }

  accept_treaty->setAutoDefault(true);
  accept_treaty->setDefault(true);
  cancel_treaty->setAutoDefault(true);
  setLayout(layout);
  update_wdg();
}

/************************************************************************//**
  Destructor for diplomacy widget
****************************************************************************/
diplo_wdg::~diplo_wdg()
{
}

/************************************************************************//**
  Double click on treaty list - it removes clicked clause from list
****************************************************************************/
void diplo_wdg::dbl_click(QTableWidgetItem *item)
{
  int i, r;

  r = item->row();
  i = 0;
  clause_list_iterate(treaty->clauses, pclause) {
    if (i == r) {
      dsend_packet_diplomacy_remove_clause_req(&client.conn,
                                               player_number(treaty->plr1),
                                               player_number(pclause->from),
                                               pclause->type,
                                               pclause->value);
      return;
    }
    i++;
  } clause_list_iterate_end;
}

/************************************************************************//**
  Received event about diplomacy widget being closed
****************************************************************************/
void diplo_wdg::closeEvent(QCloseEvent *event)
{
  if (C_S_RUNNING == client_state()) {
    response_cancel();
  }
  event->accept();
}

/************************************************************************//**
  Gold changed on first spinner
****************************************************************************/
void diplo_wdg::gold_changed1(int val)
{
  dsend_packet_diplomacy_create_clause_req(&client.conn,
                                           player_number(plr2),
                                           player_number(plr2),
                                           CLAUSE_GOLD, val);
}

/************************************************************************//**
  Gold changed on second spinner
****************************************************************************/
void diplo_wdg::gold_changed2(int val)
{
  dsend_packet_diplomacy_create_clause_req(&client.conn,
                                           player_number(plr2),
                                           player_number(plr1),
                                           CLAUSE_GOLD, val);
}

/************************************************************************//**
  Shows popup menu with available clauses to create
****************************************************************************/
void diplo_wdg::show_menu(struct player *pplayer)
{
  struct player *other_player;
  struct player *pgiver, *pother;
  enum diplstate_type ds;
  QAction *all_advancs;
  QAction *some_action;
  QMap <QString, int>city_list;
  QMap <QString, int>::const_iterator city_iter;
  QMap <QString, Tech_type_id> adv_list;
  QMap <QString, Tech_type_id>::const_iterator adv_iter;
  QMenu *map_menu = nullptr;
  QMenu *adv_menu, *city_menu, *pacts_menu;
  QMenu *menu = new QMenu(this);
  int id;

  curr_player = pplayer;
  if (curr_player == plr1) {
    other_player = plr2;
  } else {
    other_player = plr1;
  }
  pgiver = pplayer;
  pother = other_player;

  // Maps
  if (clause_enabled(CLAUSE_MAP)) {
    QAction *world_map;

    map_menu = menu->addMenu(_("Maps"));
    world_map = new QAction(_("World-map"), this);
    connect(world_map, &QAction::triggered, this, &diplo_wdg::world_map_clause);
    map_menu->addAction(world_map);
  }

  if (clause_enabled(CLAUSE_SEAMAP)) {
    QAction *sea_map;

    if (map_menu == nullptr) {
      map_menu = menu->addMenu(_("Maps"));
    }

    sea_map = new QAction(_("Sea-map"), this);
    connect(sea_map, &QAction::triggered, this, &diplo_wdg::sea_map_clause);
    map_menu->addAction(sea_map);
  }

  // Trading: advances
  if (clause_enabled(CLAUSE_ADVANCE)) {
    const struct research *gresearch = research_get(pgiver);
    const struct research *oresearch = research_get(pother);

    adv_menu = menu->addMenu(_("Advances"));
    advance_iterate(padvance) {
      Tech_type_id i = advance_number(padvance);

      if (research_invention_state(gresearch, i) == TECH_KNOWN
          && research_invention_gettable(oresearch, i,
                                         game.info.tech_trade_allow_holes)
          && (research_invention_state(oresearch, i) == TECH_UNKNOWN
              || research_invention_state(oresearch, i)
                 == TECH_PREREQS_KNOWN)) {
        adv_list.insert(advance_name_translation(padvance), i);
      }
    } advance_iterate_end;

    // All advances
    all_advancs = new QAction(_("All advances"), this);
    connect(all_advancs, &QAction::triggered, this, &diplo_wdg::all_advances);
    adv_menu->addAction(all_advancs);
    adv_menu->addSeparator();

    // QMap is sorted by default when iterating
    adv_iter = adv_list.constBegin();
    if (adv_list.count() > 0) {
      while (adv_iter != adv_list.constEnd()) {
        id = adv_iter.value();
        some_action = adv_menu->addAction(adv_iter.key());
        connect(some_action, &QAction::triggered, this,
                CAPTURE_DEFAULT_THIS () {
          give_advance(id);
        });
        adv_iter++;
      }
    } else {
      adv_menu->setDisabled(true);
    }

  }

  // Trading: cities.
  if (clause_enabled(CLAUSE_CITY)) {
    city_menu = menu->addMenu(_("Cities"));

    city_list_iterate(pgiver->cities, pcity) {
      if (!is_capital(pcity)) {
        city_list.insert(pcity->name, pcity->id);
      }
    } city_list_iterate_end;
    city_iter = city_list.constBegin();
    if (city_list.count() > 0) {
      while (city_iter != city_list.constEnd()) {
        id = city_iter.value();
        some_action = city_menu->addAction(city_iter.key());
        connect(some_action, &QAction::triggered, this,
                CAPTURE_DEFAULT_THIS () {
          give_city(id);
        });
        city_iter++;
      }
    } else {
      city_menu->setDisabled(true);
    }
  }

  if (clause_enabled(CLAUSE_VISION)) {
    some_action = new QAction(_("Give shared vision"), this);
    connect(some_action, &QAction::triggered, this,
            &diplo_wdg::give_shared_vision);
    menu->addAction(some_action);
    if (gives_shared_vision(pgiver, pother)) {
      some_action->setDisabled(true);
    }
  }

  if (clause_enabled(CLAUSE_EMBASSY)) {
    some_action = new QAction(_("Give embassy"), this);
    connect(some_action, &QAction::triggered, this, &diplo_wdg::give_embassy);
    menu->addAction(some_action);
    if (player_has_real_embassy(pother, pgiver)) {
      some_action->setDisabled(true);
    }
  }

  // Pacts
  if (curr_player == client_player()) {
    pacts_menu = menu->addMenu(_("Pacts"));
    ds = player_diplstate_get(pgiver, pother)->type;

    if (clause_enabled(CLAUSE_CEASEFIRE)) {
      some_action = new QAction(Q_("?diplomatic_state:Cease-fire"), this);
      connect(some_action, &QAction::triggered, this, &diplo_wdg::pact_ceasfire);
      pacts_menu->addAction(some_action);
      if (ds == DS_CEASEFIRE || ds == DS_TEAM) {
        some_action->setDisabled(true);
      }
    }

    if (clause_enabled(CLAUSE_PEACE)) {
      some_action = new QAction(Q_("?diplomatic_state:Peace"), this);
      connect(some_action, &QAction::triggered, this, &diplo_wdg::pact_peace);
      pacts_menu->addAction(some_action);
      if (ds == DS_PEACE || ds == DS_TEAM) {
        some_action->setDisabled(true);
      }
    }

    if (clause_enabled(CLAUSE_ALLIANCE)) {
      some_action = new QAction(Q_("?diplomatic_state:Alliance"), this);
      connect(some_action, &QAction::triggered, this, &diplo_wdg::pact_allianze);
      pacts_menu->addAction(some_action);
      if (ds == DS_ALLIANCE || ds == DS_TEAM) {
        some_action->setDisabled(true);
      }
    }
  }

  // Check user response for not defined responses in slots
  menu->setAttribute(Qt::WA_DeleteOnClose);
  menu->popup(QCursor::pos());
}

/************************************************************************//**
  Give embassy menu activated
****************************************************************************/
void diplo_wdg::give_embassy()
{
  dsend_packet_diplomacy_create_clause_req(&client.conn,
                                           player_number(treaty->plr1),
                                           player_number(curr_player),
                                           CLAUSE_EMBASSY, 0);
}

/************************************************************************//**
  Give shared vision menu activated
****************************************************************************/
void diplo_wdg::give_shared_vision()
{
  dsend_packet_diplomacy_create_clause_req(&client.conn,
                                           player_number(treaty->plr1),
                                           player_number(curr_player),
                                           CLAUSE_VISION, 0);
}

/************************************************************************//**
  Create alliance menu activated
****************************************************************************/
void diplo_wdg::pact_allianze()
{
  dsend_packet_diplomacy_create_clause_req(&client.conn,
                                           player_number(treaty->plr1),
                                           player_number(curr_player),
                                           CLAUSE_ALLIANCE, 0);
}

/************************************************************************//**
  Ceasefire pact menu activated
****************************************************************************/
void diplo_wdg::pact_ceasfire()
{
  dsend_packet_diplomacy_create_clause_req(&client.conn,
                                           player_number(treaty->plr1),
                                           player_number(curr_player),
                                           CLAUSE_CEASEFIRE, 0);
}

/************************************************************************//**
  Peace pact menu activated
****************************************************************************/
void diplo_wdg::pact_peace()
{
  dsend_packet_diplomacy_create_clause_req(&client.conn,
                                           player_number(treaty->plr1),
                                           player_number(curr_player),
                                           CLAUSE_PEACE, 0);
}

/************************************************************************//**
  Sea map menu activated
****************************************************************************/
void diplo_wdg::sea_map_clause()
{
  dsend_packet_diplomacy_create_clause_req(&client.conn,
                                           player_number(treaty->plr1),
                                           player_number(curr_player),
                                           CLAUSE_SEAMAP, 0);
}

/************************************************************************//**
  World map menu activated
****************************************************************************/
void diplo_wdg::world_map_clause()
{
  dsend_packet_diplomacy_create_clause_req(&client.conn,
                                           player_number(treaty->plr1),
                                           player_number(curr_player),
                                           CLAUSE_MAP, 0);
}

/************************************************************************//**
  Give city menu activated
****************************************************************************/
void diplo_wdg::give_city(int city_num)
{
  struct player *giver, *dest, *other;

  giver = curr_player;
  if (curr_player == plr1) {
    dest = plr2;
  } else {
    dest = plr1;
  }

  if (giver == client_player()) {
    other = dest;
  } else {
    other = giver;
  }

  dsend_packet_diplomacy_create_clause_req(&client.conn, player_number(other),
                                           player_number(giver),
                                           CLAUSE_CITY, city_num);
}

/************************************************************************//**
  Give advance menu activated
****************************************************************************/
void diplo_wdg::give_advance(int tech)
{
  struct player *giver, *dest, *other;

  giver = curr_player;
  if (curr_player == plr1) {
    dest = plr2;
  } else {
    dest = plr1;
  }

  if (giver == client_player()) {
    other = dest;
  } else {
    other = giver;
  }

  dsend_packet_diplomacy_create_clause_req(&client.conn, player_number(other),
                                           player_number(giver),
                                           CLAUSE_ADVANCE, tech);
}

/************************************************************************//**
  Give all advances menu activated
****************************************************************************/
void diplo_wdg::all_advances()
{
  struct player *giver, *dest, *other;
  const struct research *dresearch, *gresearch;

  giver = curr_player;
  if (curr_player == plr1) {
    dest = plr2;
  } else {
    dest = plr1;
  }

  if (giver == client_player()) {
    other = dest;
  } else {
    other = giver;
  }

  // All techs.

  dresearch = research_get(dest);
  gresearch = research_get(giver);

  advance_iterate(padvance) {
    Tech_type_id i = advance_number(padvance);

    if (research_invention_state(gresearch, i) == TECH_KNOWN
        && research_invention_gettable(dresearch, i,
                                       game.info.tech_trade_allow_holes)
        && (research_invention_state(dresearch, i) == TECH_UNKNOWN
            || research_invention_state(dresearch, i)
            == TECH_PREREQS_KNOWN)) {
      dsend_packet_diplomacy_create_clause_req(&client.conn, player_number(other),
                                               player_number(giver),
                                               CLAUSE_ADVANCE, i);
    }
  } advance_iterate_end;
}

/************************************************************************//**
  Show menu for second player
****************************************************************************/
void diplo_wdg::show_menu_p2()
{
  show_menu(plr2);
}

/************************************************************************//**
  Show menu for first player
****************************************************************************/
void diplo_wdg::show_menu_p1()
{
  show_menu(plr1);
}

/************************************************************************//**
  Sets index in QTabWidget
****************************************************************************/
void diplo_wdg::set_index(int ind)
{
  index = ind;
}

/************************************************************************//**
  Sets index in QTabWidget
****************************************************************************/
int diplo_wdg::get_index()
{
  return index;
}

/************************************************************************//**
  Updates diplomacy widget - updates clauses and redraws pixmaps
****************************************************************************/
void diplo_wdg::update_wdg()
{
  bool blank;
  int i;
  struct sprite *sprite;
  QPixmap *pix = nullptr;
  QTableWidgetItem *qitem;

  blank = true;
  text_edit->clearContents();
  text_edit->setRowCount(0);
  i = 0;
  clause_list_iterate(treaty->clauses, pclause) {
    char buf[128];

    client_diplomacy_clause_string(buf, sizeof(buf), pclause);
    text_edit->insertRow(i);
    qitem = new QTableWidgetItem();
    qitem->setText(buf);
    qitem->setTextAlignment(Qt::AlignLeft);
    text_edit->setItem(i, 0, qitem);
    blank = false;
    i++;
  } clause_list_iterate_end;

  if (blank) {
    text_edit->insertRow(0);
    qitem = new QTableWidgetItem();
    qitem->setText(_("--- This treaty is blank. "
                     "Please add some clauses. ---"));
    qitem->setTextAlignment(Qt::AlignLeft);
    text_edit->setItem(0, 0, qitem);
  }

  sprite = get_treaty_thumb_sprite(tileset, treaty->accept0);
  if (sprite != nullptr) {
    pix = sprite->pm;
    plr1_accept->setPixmap(*pix);
  } else {
    plr1_accept->setText("PIXMAP MISSING");
  }

  sprite = get_treaty_thumb_sprite(tileset, treaty->accept1);
  if (sprite != nullptr) {
    pix = sprite->pm;
    plr2_accept->setPixmap(*pix);
  } else {
    plr2_accept->setText("PIXMAP MISSING");
  }
}

/************************************************************************//**
  Restores original nations pixmap
****************************************************************************/
void diplo_wdg::restore_pixmap()
{
  gui()->sw_diplo->set_pixmap(fc_icons::instance()->get_pixmap("nations"));
  gui()->sw_diplo->resize_pixmap(gui()->sw_diplo->width(),
                                 gui()->sw_diplo->height());
  gui()->sw_diplo->set_custom_labels(QString());
  gui()->sw_diplo->update_final_pixmap();
}

/************************************************************************//**
  Button 'Accept treaty' has been clicked
****************************************************************************/
void diplo_wdg::response_accept()
{
  restore_pixmap();
  dsend_packet_diplomacy_accept_treaty_req(&client.conn,
                                           player_number(treaty->plr1));
}

/************************************************************************//**
  Button 'Cancel treaty' has been clicked
****************************************************************************/
void diplo_wdg::response_cancel()
{
  restore_pixmap();
  dsend_packet_diplomacy_cancel_meeting_req(&client.conn,
                                            player_number(treaty->plr1));
}

/************************************************************************//**
  Constructor for diplomacy dialog
****************************************************************************/
diplo_dlg::diplo_dlg(struct treaty *ptreaty, struct player *they,
                     struct player *initiator) : QTabWidget()
{
  add_widget(ptreaty, they, initiator);
  setFocusPolicy(Qt::ClickFocus);
}

/************************************************************************//**
  Creates new diplomacy widget and adds to diplomacy dialog
****************************************************************************/
void diplo_dlg::add_widget(struct treaty *ptreaty, struct player *they,
                           struct player *initiator)
{
  diplo_wdg *dw;
  struct sprite *sprite;
  QPixmap *pix;
  int i;

  pix = nullptr;
  dw = new diplo_wdg(ptreaty, they, initiator);
  treaty_list.insert(they, dw);
  i = addTab(dw, nation_plural_for_player(they));
  dw->set_index(i);
  sprite = get_nation_flag_sprite(tileset,
                                  nation_of_player(they));
  if (sprite != nullptr) {
    pix = sprite->pm;
  }
  if (pix != nullptr) {
    setTabIcon(i, QIcon(*pix));
  }
}

/************************************************************************//**
  Sets given widget as active in current dialog
****************************************************************************/
void diplo_dlg::make_active(struct player *party)
{
  QWidget *w;

  w = find_widget(party);
  if (w == nullptr) {
    return;
  }
  setCurrentWidget(w);
}

/************************************************************************//**
  Initializes some data for diplomacy dialog
****************************************************************************/
bool diplo_dlg::init(bool raise)
{
  if (!can_client_issue_orders()) {
    return false;
  }
  if (!is_human(client_player())) {
    return false;
  }
  setAttribute(Qt::WA_DeleteOnClose);
  gui()->gimme_place(this, "DDI");
  index = gui()->add_game_tab(this);
  gui()->game_tab_widget->setCurrentIndex(index);

  return true;
}

/************************************************************************//**
  Destructor for diplomacy dialog
****************************************************************************/
diplo_dlg::~diplo_dlg()
{
  QMapIterator<struct player *, diplo_wdg *>i(treaty_list);
  diplo_wdg *dw;

  while (i.hasNext()) {
    i.next();
    dw = i.value();
    dw->close();
    removeTab(dw->get_index());
    dw->deleteLater();
  }
  gui()->remove_repo_dlg("DDI");
  gui()->game_tab_widget->setCurrentIndex(0);
}

/************************************************************************//**
  Finds diplomacy widget in current dialog
****************************************************************************/
diplo_wdg *diplo_dlg::find_widget(struct player *they)
{
  return treaty_list.value(they);
}

/************************************************************************//**
  Closes given diplomacy widget
****************************************************************************/
void diplo_dlg::close_widget(struct player *they)
{
  diplo_wdg *dw;

  dw = treaty_list.take(they);
  removeTab(dw->get_index());
  dw->deleteLater();
  if (treaty_list.isEmpty()) {
    close();
  }
}

/************************************************************************//**
  Updates all diplomacy widgets in current dialog
****************************************************************************/
void diplo_dlg::update_dlg()
{
  QMapIterator <struct player *, diplo_wdg *>i(treaty_list);
  diplo_wdg *dw;

  while (i.hasNext()) {
    i.next();
    dw = i.value();
    dw->update_wdg();
  }
}

/************************************************************************//**
  Existing dialog requested again.
****************************************************************************/
void diplo_dlg::reactivate()
{
  raise();
}

/************************************************************************//**
  Update a player's acceptance status of a treaty (traditionally shown
  with the thumbs-up/thumbs-down sprite).
****************************************************************************/
void qtg_recv_accept_treaty(struct treaty *ptreaty, struct player *they)
{
  int i;
  diplo_dlg *dd;
  diplo_wdg *dw;
  QWidget *w;

  if (!gui()->is_repo_dlg_open("DDI")) {
    return;
  }
  i = gui()->gimme_index_of("DDI");
  fc_assert(i != -1);
  w = gui()->game_tab_widget->widget(i);
  dd = qobject_cast<diplo_dlg *>(w);
  dw = dd->find_widget(they);

  fc_assert(dw->treaty == ptreaty);

  dw->update_wdg();
}

/************************************************************************//**
  Handle the start of a diplomacy meeting - usually by popping up a
  diplomacy dialog.
****************************************************************************/
void qtg_init_meeting(struct treaty *ptreaty, struct player *they,
                      struct player *initiator)
{
  int i;
  diplo_dlg *dd;
  QPainter p;
  QPixmap *pix, *def_pix, *pix2, *pix3, *def_pix_del;
  QWidget *w;
  QWidget *fw;
  bool was_open;

  if (client_is_observer()) {
    return;
  }

  if (gui()->current_page() != PAGE_GAME) {
    gui()->switch_page(PAGE_GAME);
  }

  pix2 = new QPixmap();
  def_pix_del = new QPixmap();
  pix = get_nation_flag_sprite(tileset,
                               nation_of_player(they))->pm;
  *pix2 = pix->scaledToWidth(gui()->sw_diplo->width() - 2,
                             Qt::SmoothTransformation);
  if (pix2->height() > gui()->sw_diplo->height()) {
    *pix2 = pix->scaledToHeight(gui()->sw_diplo->height(),
                             Qt::SmoothTransformation);
  }
  pix3 = new QPixmap(gui()->sw_diplo->width(), gui()->sw_diplo->height());
  pix3->fill(Qt::transparent);
  def_pix = fc_icons::instance()->get_pixmap("nations");
  *def_pix_del = def_pix->scaled(gui()->sw_diplo->width(),
                                 gui()->sw_diplo->height(),
                                 Qt::IgnoreAspectRatio,
                                 Qt::SmoothTransformation);
  p.begin(pix3);
  p.drawPixmap(1, 1, *pix2);
  p.drawPixmap(0, 0, *def_pix_del);
  p.end();
  gui()->sw_diplo->set_pixmap(pix3);
  gui()->sw_diplo->resize_pixmap(gui()->sw_diplo->width(),
                                 gui()->sw_diplo->height());
  gui()->sw_diplo->set_custom_labels(QString(nation_plural_for_player(
                                                               they)));
  gui()->sw_diplo->update_final_pixmap();
  delete pix2;
  delete def_pix;
  delete def_pix_del;

  was_open = gui()->is_repo_dlg_open("DDI");
  if (!was_open) {
    dd = new diplo_dlg(ptreaty, they, initiator);

    if (!dd->init(false)) {
      delete dd;
      return;
    }
    dd->update_dlg();
    dd->make_active(they);
  }
  i = gui()->gimme_index_of("DDI");
  fc_assert(i != -1);
  w = gui()->game_tab_widget->widget(i);
  dd = qobject_cast<diplo_dlg *>(w);

  if (was_open) {
    dd->reactivate();
  }

  fw = dd->find_widget(they);
  if (fw == nullptr) {
    dd->add_widget(ptreaty, they, initiator);
    gui()->game_tab_widget->setCurrentIndex(i);
  }
  dd->make_active(they);

  // Bring it to front if user requested meeting
  if (initiator == client_player()) {
    gui()->game_tab_widget->setCurrentIndex(i);
  }
}

/**********************************************************************//**
  Prepare to clause creation or removal.
**************************************************************************/
void qtg_prepare_clause_updt(struct treaty *ptreaty, struct player *they)
{
  // Not needed
}

/************************************************************************//**
  Update the diplomacy dialog by adding a clause.
****************************************************************************/
void qtg_recv_create_clause(struct treaty *ptreaty, struct player *they)
{
  int i;
  diplo_dlg *dd;
  diplo_wdg *dw;
  QWidget *w;

  if (!gui()->is_repo_dlg_open("DDI")) {
    log_normal("DDI not open");
    return;
  }
  i = gui()->gimme_index_of("DDI");
  fc_assert(i != -1);
  w = gui()->game_tab_widget->widget(i);
  dd = qobject_cast<diplo_dlg *>(w);
  dw = dd->find_widget(they);
  fc_assert(dw->treaty == ptreaty);
  dw->update_wdg();
}

/************************************************************************//**
  Update the diplomacy dialog when the meeting is canceled (the dialog
  should be closed).
****************************************************************************/
void qtg_recv_cancel_meeting(struct treaty *ptreaty, struct player *they,
                             struct player *initiator)
{
  int i;
  diplo_dlg *dd;
  QWidget *w;

  if (!gui()->is_repo_dlg_open("DDI")) {
    return;
  }
  i = gui()->gimme_index_of("DDI");
  fc_assert(i != -1);
  w = gui()->game_tab_widget->widget(i);
  dd = qobject_cast<diplo_dlg *>(w);
  dd->close_widget(they);
}

/************************************************************************//**
  Update the diplomacy dialog by removing a clause.
****************************************************************************/
void qtg_recv_remove_clause(struct treaty *ptreaty, struct player *they)
{
  int i;
  diplo_dlg *dd;
  diplo_wdg *dw;
  QWidget *w;

  if (!gui()->is_repo_dlg_open("DDI")) {
    return;
  }

  i = gui()->gimme_index_of("DDI");
  fc_assert(i != -1);
  w = gui()->game_tab_widget->widget(i);
  dd = qobject_cast<diplo_dlg *>(w);
  dw = dd->find_widget(they);
  fc_assert(dw->treaty == ptreaty);
  dw->update_wdg();
}

/************************************************************************//**
  Close all open diplomacy dialogs.

  Called when the client disconnects from game.
****************************************************************************/
void close_all_diplomacy_dialogs(void)
{
  int i;
  diplo_dlg *dd;
  QWidget *w;

  current_app()->alert(gui()->central_wdg);
  if (!gui()->is_repo_dlg_open("DDI")) {
    return;
  }
  i = gui()->gimme_index_of("DDI");
  fc_assert(i != -1);
  w = gui()->game_tab_widget->widget(i);
  dd = qobject_cast<diplo_dlg *>(w);
  dd->close();
  delete dd;
}
