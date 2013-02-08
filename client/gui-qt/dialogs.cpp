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
#include <QDialog>

// common
#include "game.h"
#include "government.h"
#include "nation.h"

// client
#include "helpdata.h"
#include "packhand.h"
#include "tilespec.h"

// gui-qt
#include "dialogs.h"
#include "qtg_cxxside.h"
#include "sprite.h"

// utility
#include "rand.h"

/***************************************************************************
 Constructor for selecting nations
***************************************************************************/
races_dialog::races_dialog(struct player *pplayer, QWidget * parent):QDialog(parent)
{
  struct nation_group *group;
  int i;
  QGridLayout *qgroupbox_layout;
  QGroupBox *no_name;
  QWidget *tab_widget;
  QTableWidgetItem *item;
  QPixmap *pix;
  QHeaderView *header;
  QSize size;
  QString title;

  main_layout = new QGridLayout;
  nation_tabs = new QToolBox(parent);
  selected_nation_tabs = new QTableWidget;
  city_styles = new QTableWidget;
  ok_button = new QPushButton;
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

  city_styles->setRowCount(0);
  city_styles->setColumnCount(2);
  city_styles->setSelectionMode(QAbstractItemView::SingleSelection);
  city_styles->verticalHeader()->setVisible(false);
  city_styles->horizontalHeader()->setVisible(false);
  city_styles->setProperty("showGrid", "false");
  city_styles->setProperty("selectionBehavior", "SelectRows");
  city_styles->setEditTriggers(QAbstractItemView::NoEditTriggers);

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
  header->setResizeMode(QHeaderView::Stretch);
  header->resizeSections(QHeaderView::ResizeToContents);
  header = city_styles->verticalHeader();
  header->resizeSections(QHeaderView::ResizeToContents);
  tab_widget = new QWidget();
  nation_tabs->addItem(tab_widget, _("All Nations"));
  for (i = 0; i < nation_group_count(); i++) {
    group = nation_group_by_number(i);
    tab_widget = new QWidget();
    nation_tabs->addItem(tab_widget, nation_group_name_translation(group));
  }
  connect(nation_tabs, SIGNAL(currentChanged(int)), SLOT(set_index(int)));
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

  ok_button = new QPushButton;
  ok_button->setText(_("Cancel"));
  connect(ok_button, SIGNAL(pressed()), SLOT(cancel_pressed()));
  main_layout->addWidget(ok_button, 8, 1, 1, 1);
  random_button = new QPushButton;
  random_button->setText(_("Random"));
  connect(random_button, SIGNAL(pressed()), SLOT(random_pressed()));
  main_layout->addWidget(random_button, 8, 0, 1, 1);
  ok_button = new QPushButton;
  ok_button->setText(_("Ok"));
  connect(ok_button, SIGNAL(pressed()), SLOT(ok_pressed()));
  main_layout->addWidget(ok_button, 8, 2, 1, 1);
  main_layout->addWidget(no_name, 0, 2, 2, 1);
  main_layout->addWidget(nation_tabs, 0, 0, 6, 1);
  main_layout->addWidget(city_styles, 2, 2, 4, 1);
  main_layout->addWidget(description, 6, 0, 2, 3);
  main_layout->addWidget(selected_nation_tabs, 0, 1, 6, 1);

  setLayout(main_layout);
  set_index(0);
  update();

  if (C_S_RUNNING == client_state()) {
    title = _("Edit Nation");
  } else if (NULL != pplayer && pplayer == client.conn.playing) {
    title = _("What Nation Will You Be?");
  } else {
    title = _("Pick Nation");
  }

  setWindowTitle(title);
}

/***************************************************************************
  Sets new nations' group by given index
***************************************************************************/
void races_dialog::set_index(int index)
{
  QTableWidgetItem *item;
  QPixmap *pix;
  struct nation_group *group;
  int i = 0;
  struct sprite *s;
  QHeaderView *header;

  selected_nation_tabs->clearContents();
  selected_nation_tabs->setRowCount(0);

  group = nation_group_by_number(index - 1);
  nations_iterate(pnation) {
    if (!is_nation_playable(pnation) || !pnation->is_available) {
      continue;
    }
    if (!nation_is_in_group(pnation, group) && index != 0) {
      continue;
    }
    item = new QTableWidgetItem;
    selected_nation_tabs->insertRow(i);
    s = get_nation_flag_sprite(tileset, pnation);
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
  if (leader_name->itemData(index).toBool())
  {
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
  dsend_packet_nation_select_req(&client.conn, player_number(tplayer),
                                 selected_nation, selected_sex,
                                 leader_name->currentText().toUtf8().data(),
                                 selected_style);
  delete this;
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
                                 FALSE, "", 0);
  delete this;
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
  /* PORTME */
}

/**************************************************************************
  Popup a dialog to display connection message from server.
**************************************************************************/
void popup_connect_msg(const char *headline, const char *message)
{
  /* PORTME */
}

/**************************************************************************
  Popup a generic dialog to display some generic information.
**************************************************************************/
void popup_notify_dialog(const char *caption, const char *headline,
			 const char *lines)
{
  /* PORTME */
}

/**************************************************************************
  Popup the nation selection dialog.
**************************************************************************/
void popup_races_dialog(struct player *pplayer)
{
  races_dialog* race_dialog = new races_dialog(pplayer);
  race_dialog->show();
}

/**************************************************************************
  Close the nation selection dialog.  This should allow the user to
  (at least) select a unit to activate.
**************************************************************************/
void popdown_races_dialog(void)
{
  /* PORTME */
}

/**************************************************************************
  Popup a dialog window to select units on a particular tile.
**************************************************************************/
void unit_select_dialog_popup(struct tile *ptile)
{
  /* PORTME */
}

/**************************************************************************
  Update the dialog window to select units on a particular tile.
**************************************************************************/
void unit_select_dialog_update_real(void)
{
  /* PORTME */
}

/**************************************************************************
  In the nation selection dialog, make already-taken nations unavailable.
  This information is contained in the packet_nations_used packet.
**************************************************************************/
void races_toggles_set_sensitive(void)
{
  /* PORTME */
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
  /* PORTME */
}

/**************************************************************************
  Is there currently a caravan dialog open?  This is important if there
  can be only one such dialog at a time; otherwise return FALSE.
**************************************************************************/
bool caravan_dialog_is_open(int *unit_id, int *city_id)
{
  /* PORTME */
  return FALSE;
}

/**************************************************************************
  Popup a dialog giving a diplomatic unit some options when moving into
  the target tile.
**************************************************************************/
void popup_diplomat_dialog(struct unit *punit, struct tile *ptile)
{
  /* PORTME */
}

/**************************************************************************
  Popup a window asking a diplomatic unit if it wishes to incite the
  given enemy city.
**************************************************************************/
void popup_incite_dialog(struct city *pcity, int cost)
{
  /* PORTME */
}

/**************************************************************************
  Popup a dialog asking a diplomatic unit if it wishes to bribe the
  given enemy unit.
**************************************************************************/
void popup_bribe_dialog(struct unit *punit, int cost)
{
  /* PORTME */
}

/**************************************************************************
  Popup a dialog asking a diplomatic unit if it wishes to sabotage the
  given enemy city.
**************************************************************************/
void popup_sabotage_dialog(struct city *pcity)
{
  /* PORTME */
}

/**************************************************************************
  Popup a dialog asking the unit which improvement they would like to
  pillage.
**************************************************************************/
void popup_pillage_dialog(struct unit *punit, bv_special spe,
                          bv_bases bases, bv_roads roads)
{
  /* PORTME */
}

/****************************************************************************
  Pops up a dialog to confirm disband of the unit(s).
****************************************************************************/
void popup_disband_dialog(struct unit_list *punits)
{
  /* PORTME */
}

/**************************************************************************
  Ruleset (modpack) has suggested loading certain tileset. Confirm from
  user and load.
**************************************************************************/
void popup_tileset_suggestion_dialog(void)
{
  /* PORTME */
}

/****************************************************************
  Ruleset (modpack) has suggested loading certain soundset. Confirm from
  user and load.
*****************************************************************/
void popup_soundset_suggestion_dialog(void)
{
  /* PORTME */
}

/**************************************************************************
  Tileset (modpack) has suggested loading certain theme. Confirm from
  user and load.
**************************************************************************/
bool popup_theme_suggestion_dialog(const char *theme_name)
{
  /* PORTME */
  return FALSE;
}

/**************************************************************************
  This function is called when the client disconnects or the game is
  over.  It should close all dialog windows for that game.
**************************************************************************/
void popdown_all_game_dialogs(void)
{
  /* PORTME */
}

/****************************************************************
  Returns id of a diplomat currently handled in diplomat dialog
*****************************************************************/
int diplomat_handled_in_diplomat_dialog(void)
{
  /* PORTME */    
  return -1;  
}

/****************************************************************
  Closes the diplomat dialog
****************************************************************/
void close_diplomat_dialog(void)
{
  /* PORTME */
}

/****************************************************************
  Updates caravan dialog
****************************************************************/
void caravan_dialog_update(void)
{
  /* PORTME */
}

/****************************************************************
  Player has gained a new tech.
*****************************************************************/
void show_tech_gained_dialog(Tech_type_id tech)
{
  /* PORTME */
}
