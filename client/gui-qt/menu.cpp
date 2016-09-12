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
#include <QFileDialog>
#include <QMainWindow>
#include <QMessageBox>
#include <QScrollArea>

// utility 
#include "string_vector.h"

// common
#include "game.h"
#include "government.h"
#include "name_translation.h"
#include "road.h"
#include "unit.h"

// client
#include "connectdlg_common.h"
#include "control.h"
#include "helpdata.h"

// client/include
#include "helpdlg_g.h"

// gui-qt
#include "qtg_cxxside.h"
#include "fc_client.h"
#include "chatline.h"
#include "cityrep.h"
#include "dialogs.h"
#include "gotodlg.h"
#include "gui_main.h"
#include "messagedlg.h"
#include "plrdlg.h"
#include "ratesdlg.h"
#include "repodlgs.h"
#include "spaceshipdlg.h"
#include "sprite.h"

#include "menu.h"

extern QApplication *qapp;
/**************************************************************************
  Initialize menus (sensitivity, name, etc.) based on the
  current state and current ruleset, etc.  Call menus_update().
**************************************************************************/
void real_menus_init(void)
{
  /* PORTME */
  gov_menu::create_all();
}

/**************************************************************************
  Update all of the menus (sensitivity, name, etc.) based on the
  current state.
**************************************************************************/
void real_menus_update(void)
{
  if (C_S_RUNNING == client_state()) {
    gui()->menu_bar->menus_sensitive();
    gov_menu::update_all();
  }
}

/****************************************************************************
  Return the text for the tile, changed by the activity.
  Should only be called for irrigation, mining, or transformation, and
  only when the activity changes the base terrain type.
****************************************************************************/
static const char *get_tile_change_menu_text(struct tile *ptile,
                                             enum unit_activity activity)
{
  struct tile *newtile = tile_virtual_new(ptile);
  const char *text;

  tile_apply_activity(newtile, activity);
  text = tile_get_info_text(newtile, FALSE, 0);
  tile_virtual_destroy(newtile);
  return text;
}

/**************************************************************************
  Displays dialog with scrollbars, like QMessageBox with ok button
**************************************************************************/
void fc_message_box::info(QWidget *parent, const QString &title,
                          const QString &message) {
  setParent(parent);
  setWindowTitle(title);
  setWindowFlags(Qt::Window);
  QVBoxLayout *vb = new  QVBoxLayout;
  label = new QLabel;
  label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
  label->setContentsMargins(2, 2, 2, 2);
  label->setWordWrap(true);
  label->setText(message);
  scroll = new QScrollArea(this);
  scroll->setWidget(label);
  scroll->setWidgetResizable(true);

  ok_button = new QPushButton(this);
  connect(ok_button, SIGNAL(clicked()), this, SLOT(close()));
  ok_button->setText(_("Roger that"));

  vb->addWidget(scroll);
  vb->addWidget(ok_button);
  setLayout(vb);
  setFixedHeight(400);
  setFixedWidth(600);
  show();
}

/****************************************************************************
  Unit filter constructor
****************************************************************************/
unit_filter::unit_filter()
{
  reset_activity();
  reset_other();
  any = true;
  any_activity = true;
}

/****************************************************************************
  Resets activity filter to all disabled
****************************************************************************/
void unit_filter::reset_activity()
{
  forified = false;
  sentried = false;
  idle = false;
  any_activity = false;
}

/****************************************************************************
  Resets 2nd filter to all disbaled
****************************************************************************/
void unit_filter::reset_other()
{
  full_hp = false;
  full_mp = false;
  full_hp_mp = false;
  any = false;
}

/****************************************************************************
  Creates a new government menu.
****************************************************************************/
gov_menu::gov_menu(QWidget* parent) :
  QMenu(_("Government"), parent),
  gov_mapper(new QSignalMapper())
{
  // Register ourselves to get updates for free.
  instances << this;
}

/****************************************************************************
  Destructor.
****************************************************************************/
gov_menu::~gov_menu()
{
  instances.remove(this);
}

/****************************************************************************
  Creates the menu once the government list is known.
****************************************************************************/
void gov_menu::create() {
  QAction *action;
  struct government *gov, *revol_gov;
  int gov_count, i;

  // Clear any content
  foreach(action, QWidget::actions()) {
    removeAction(action);
    action->deleteLater();
  }
  actions.clear();
  gov_mapper->deleteLater();
  gov_mapper = new QSignalMapper();

  gov_count = government_count();
  actions.reserve(gov_count + 1);
  action = addAction(_("Revolution..."));
  connect(action, &QAction::triggered, this, &gov_menu::revolution);
  actions.append(action);

  addSeparator();

  // Add an action for each government. There is no icon yet.
  revol_gov = game.government_during_revolution;
  for (i = 0; i < gov_count; ++i) {
    gov = government_by_number(i);
    if (gov != revol_gov) { // Skip revolution goverment
      action = addAction(name_translation(&gov->name));
      // We need to keep track of the gov <-> action mapping to be able to
      // set enabled/disabled depending on available govs.
      actions.append(action);
      connect(action, SIGNAL(triggered()), gov_mapper, SLOT(map()));
      gov_mapper->setMapping(action, i);
    }
  }
  connect(gov_mapper, SIGNAL(mapped(int)), this, SLOT(change_gov(int)));
}

/****************************************************************************
  Updates the menu to take gov availability into account.
****************************************************************************/
void gov_menu::update()
{
  struct government *gov, *revol_gov;
  struct sprite *sprite;
  int gov_count, i, j;

  gov_count = government_count();
  revol_gov = game.government_during_revolution;
  for (i = 0, j = 0; i < gov_count; ++i) {
    gov = government_by_number(i);
    if (gov != revol_gov) { // Skip revolution goverment
      sprite = get_government_sprite(tileset, gov);
      if (sprite != NULL) {
        actions[j + 1]->setIcon(QIcon(*(sprite->pm)));
      }
      actions[j + 1]->setEnabled(
        can_change_to_government(client.conn.playing, gov));
      ++j;
    } else {
      actions[0]->setEnabled(!client_is_observer());
    }
  }
}

/****************************************************************************
  Shows the dialog asking for confirmation before starting a revolution.
****************************************************************************/
void gov_menu::revolution()
{
  popup_revolution_dialog();
}

/****************************************************************************
  Shows the dialog asking for confirmation before starting a revolution.
****************************************************************************/
void gov_menu::change_gov(int target_gov)
{
  popup_revolution_dialog(government_by_number(target_gov));
}

/****************************************************************************
  Keeps track of all gov_menu instances.
****************************************************************************/
QSet<gov_menu *> gov_menu::instances = QSet<gov_menu *>();

/****************************************************************************
 * Updates all gov_menu instances.
 ****************************************************************************/
void gov_menu::create_all()
{
  foreach (gov_menu *m, instances) {
    m->create();
  }
}

/****************************************************************************
  Updates all gov_menu instances.
****************************************************************************/
void gov_menu::update_all()
{
  foreach (gov_menu *m, instances) {
    m->update();
  }
}

/****************************************************************************
  Applies activity filter to given unit
****************************************************************************/
void mr_menu::apply_filter(struct unit *punit)
{
  if (punit->activity == ACTIVITY_FORTIFIED && u_filter.forified) {
    apply_2nd_filter(punit);
  }
  if (punit->activity == ACTIVITY_SENTRY && u_filter.sentried) {
    apply_2nd_filter(punit);
  }
  if (punit->activity == ACTIVITY_IDLE && u_filter.idle) {
    apply_2nd_filter(punit);
  }
  if (u_filter.any_activity){
    apply_2nd_filter(punit);
  }
}

/****************************************************************************
  Applies miscelanous filter for given unit
****************************************************************************/
void mr_menu::apply_2nd_filter(struct unit *punit)
{
  if (punit->hp >= punit->utype->hp && u_filter.full_hp
      && !u_filter.full_hp_mp) {
    unit_focus_add(punit);
  }
  if (punit->moves_left  >= punit->utype->move_rate && u_filter.full_mp
      && !u_filter.full_hp_mp) {
    unit_focus_add(punit);
  }
  if (punit->hp >= punit->utype->hp
      && punit->moves_left  >= punit->utype->move_rate
      && u_filter.full_hp_mp) {
    unit_focus_add(punit);
  }
  if (u_filter.any) {
    unit_focus_add(punit);
  }
}

/****************************************************************************
  Selects units based on selection modes
****************************************************************************/
void mr_menu::unit_select(struct unit_list *punits,
                         enum unit_select_type_mode seltype,
                         enum unit_select_location_mode selloc)
{
  QList<const struct tile *> tiles;
  QList<struct unit_type *> types;
  QList<Continent_id> conts;

  const struct player *pplayer;
  const struct tile *ptile;
  struct unit *punit_first;

  if (!can_client_change_view() || !punits
      || unit_list_size(punits) < 1) {
    return;
  }

  punit_first = unit_list_get(punits, 0);

  if (seltype == SELTYPE_SINGLE) {
    unit_focus_set(punit_first);
    return;
  }
  pplayer = unit_owner(punit_first);
  unit_list_iterate(punits, punit) {
    if (seltype == SELTYPE_SAME) {
      types.append(unit_type(punit));
    }

    ptile = unit_tile(punit);
    if (selloc == SELLOC_TILE) {
      tiles.append(ptile);
    } else if (selloc == SELLOC_CONT) {
      conts.append(ptile->continent);
    }
  } unit_list_iterate_end;

  if (selloc == SELLOC_TILE) {
    foreach (ptile, tiles) {
      unit_list_iterate(ptile->units, punit) {
        if (unit_owner(punit) != pplayer) {
          continue;
        }
        if (seltype == SELTYPE_SAME
            && !types.contains(unit_type(punit))) {
          continue;
        }
        apply_filter(punit);
      } unit_list_iterate_end;
    }
  } else {
    unit_list_iterate(pplayer->units, punit) {
      ptile = unit_tile(punit);
      if ((seltype == SELTYPE_SAME
           && !types.contains(unit_type(punit)))
          || (selloc == SELLOC_CONT
              && !conts.contains(ptile->continent))) {
        continue;
      }
      apply_filter(punit);
    } unit_list_iterate_end;
  }
}


/****************************************************************************
  Constructor for global menubar in gameview
****************************************************************************/
mr_menu::mr_menu() : QMenuBar()
{
}

/****************************************************************************
  Initializes menu system, and add custom enum(munit) for most of options
  Notice that if you set option for QAction->setChecked(option) it will
  check/uncheck automatically without any intervention
****************************************************************************/
void mr_menu::setup_menus()
{
  QAction *act;
  QMenu *pr;

  /* Game Menu */
  menu = this->addMenu(_("Game"));
  pr = menu;
  menu = menu->addMenu(_("Options"));
  act = menu->addAction(_("Set local options"));
  connect(act, SIGNAL(triggered()), this, SLOT(local_options()));
  act = menu->addAction(_("Server Options"));
  connect(act, SIGNAL(triggered()), this, SLOT(server_options()));
  act = menu->addAction(_("Messages"));
  connect(act, SIGNAL(triggered()), this, SLOT(messages_options()));
  act = menu->addAction(_("Save Options Now"));
  act->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
  connect(act, SIGNAL(triggered()), this, SLOT(save_options_now()));
  act = menu->addAction(_("Save Options on Exit"));
  act->setCheckable(true);
  act->setChecked(save_options_on_exit);
  menu = pr;
  menu->addSeparator();
  act = menu->addAction(_("Save Game"));
  act->setShortcut(QKeySequence(tr("Ctrl+s")));
  act->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
  menu_list.insertMulti(SAVE, act);
  connect(act, SIGNAL(triggered()), this, SLOT(save_game()));
  act = menu->addAction(_("Save Game As..."));
  menu_list.insertMulti(SAVE, act);
  act->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
  connect(act, SIGNAL(triggered()), this, SLOT(save_game_as()));
  menu->addSeparator();
  act = menu->addAction(_("Leave game"));
  act->setIcon(style()->standardIcon(QStyle::SP_DialogDiscardButton));
  connect(act, SIGNAL(triggered()), this, SLOT(back_to_menu()));
  act = menu->addAction(_("Quit"));
  act->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
  connect(act, SIGNAL(triggered()), this, SLOT(quit_game()));

  /* View Menu */
  menu = this->addMenu(Q_("?verb:View"));
  act = menu->addAction(_("Center View"));
  act->setShortcut(QKeySequence(tr("c")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_center_view()));
  menu->addSeparator();
  act = menu->addAction(_("Fullscreen"));
  act->setShortcut(QKeySequence(tr("alt+return")));
  act->setCheckable(true);
  act->setChecked(fullscreen_mode);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_fullscreen()));
  menu->addSeparator();
  minimap_status = menu->addAction(_("Minimap"));
  minimap_status->setCheckable(true);
  minimap_status->setShortcut(QKeySequence(tr("ctrl+m")));
  minimap_status->setChecked(true);
  connect(minimap_status, SIGNAL(triggered()), this, 
          SLOT(slot_minimap_view()));
  messages_status = menu->addAction(_("Messages"));
  messages_status->setCheckable(true);
  messages_status->setChecked(true);
  connect(messages_status, SIGNAL(triggered()), this, SLOT(slot_show_messages()));
  chat_status = menu->addAction(_("Chat"));
  chat_status->setCheckable(true);
  chat_status->setChecked(true);
  connect(chat_status, SIGNAL(triggered()), this, SLOT(slot_show_chat()));
  menu->addSeparator();
  act = menu->addAction(_("City Outlines"));
  act->setShortcut(QKeySequence(tr("Ctrl+y")));
  act->setCheckable(true);
  act->setChecked(draw_city_outlines);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_city_outlines()));
  act = menu->addAction(_("City Output"));
  act->setCheckable(true);
  act->setChecked(draw_city_output);
  act->setShortcut(QKeySequence(tr("ctrl+w")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_city_output()));
  act = menu->addAction(_("Map Grid"));
  act->setShortcut(QKeySequence(tr("ctrl+g")));
  act->setCheckable(true);
  act->setChecked(draw_map_grid);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_map_grid()));
  act = menu->addAction(_("National Borders"));
  act->setCheckable(true);
  act->setChecked(draw_borders);
  act->setShortcut(QKeySequence(tr("ctrl+b")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_borders()));
  act = menu->addAction(_("Native Tiles"));
  act->setCheckable(true);
  act->setChecked(draw_native);
  act->setShortcut(QKeySequence(tr("ctrl+shift+n")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_native_tiles()));
  act = menu->addAction(_("City Full Bar"));
  act->setCheckable(true);
  act->setChecked(draw_full_citybar);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_fullbar()));
  act = menu->addAction(_("City Names"));
  act->setCheckable(true);
  act->setChecked(draw_city_names);
  act->setShortcut(QKeySequence(tr("ctrl+n")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_city_names()));
  act = menu->addAction(_("City Growth"));
  act->setCheckable(true);
  act->setChecked(draw_city_growth);
  act->setShortcut(QKeySequence(tr("ctrl+r")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_city_growth()));
  act = menu->addAction(_("City Production Levels"));
  act->setCheckable(true);
  act->setChecked(draw_city_productions);
  act->setShortcut(QKeySequence(tr("ctrl+p")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_city_production()));
  act = menu->addAction(_("City Buy Cost"));
  act->setCheckable(true);
  act->setChecked(draw_city_buycost);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_city_buycost()));
  act = menu->addAction(_("City Traderoutes"));
  act->setCheckable(true);
  act->setChecked(draw_city_trade_routes);
  act->setShortcut(QKeySequence(tr("ctrl+d")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_city_traderoutes()));

  /* Select Menu */
  menu = this->addMenu(_("Select"));
  act = menu->addAction(_("Single Unit (Unselect Others)"));
  act->setShortcut(QKeySequence(tr("z")));
  menu_list.insertMulti(STANDARD, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_select_one()));
  act = menu->addAction(_("All On Tile"));
  act->setShortcut(QKeySequence(tr("v")));
  menu_list.insertMulti(STANDARD, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_select_all_tile()));
  menu->addSeparator();
  act = menu->addAction(_("Same Type on Tile"));
  act->setShortcut(QKeySequence(tr("shift+v")));
  menu_list.insertMulti(STANDARD, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_select_same_tile()));
  act = menu->addAction(_("Same Type on Continent"));
  act->setShortcut(QKeySequence(tr("shift+c")));
  menu_list.insertMulti(STANDARD, act);
  connect(act, SIGNAL(triggered()), this, 
          SLOT(slot_select_same_continent()));
  act = menu->addAction(_("Same Type Everywhere"));
  act->setShortcut(QKeySequence(tr("shift+x")));
  menu_list.insertMulti(STANDARD, act);
  connect(act, SIGNAL(triggered()), this, 
          SLOT(slot_select_same_everywhere()));
  menu->addSeparator();
  act = menu->addAction(_("Wait"));
  act->setShortcut(QKeySequence(tr("w")));
  menu_list.insertMulti(STANDARD, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_wait()));
  act = menu->addAction(_("Done"));
  act->setShortcut(QKeySequence(tr("space")));
  menu_list.insertMulti(STANDARD, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_done_moving()));

  filter_act = new QActionGroup(this);
  filter_any = new QActionGroup(this);
  filter_menu = menu->addMenu(_("Units Filter"));
  /* TRANS: Any activity for given selection of units */
  act = filter_menu->addAction(_("Any activity"));
  act->setCheckable(true);
  act->setShortcut(QKeySequence(tr("ctrl+1")));
  act->setChecked(u_filter.any_activity);
  act->setData(qVariantFromValue((void *) &u_filter.any_activity));
  filter_act->addAction(act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_filter()));
  act = filter_menu->addAction(_("Idle"));
  act->setCheckable(true);
  act->setChecked(u_filter.idle);
  act->setShortcut(QKeySequence(tr("ctrl+2")));
  act->setData(qVariantFromValue((void *) &u_filter.idle));
  filter_act->addAction(act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_filter()));
  act = filter_menu->addAction(_("Fortified"));
  act->setCheckable(true);
  act->setChecked(u_filter.forified);
  act->setShortcut(QKeySequence(tr("ctrl+3")));
  act->setData(qVariantFromValue((void *) &u_filter.forified));
  filter_act->addAction(act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_filter()));
  act = filter_menu->addAction(_("Sentried"));
  act->setCheckable(true);
  act->setChecked(u_filter.sentried);
  act->setShortcut(QKeySequence(tr("ctrl+4")));
  act->setData(qVariantFromValue((void *) &u_filter.sentried));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_filter()));
  filter_act->addAction(act);
  filter_menu->addSeparator();
  act = filter_menu->addAction(_("Any unit"));
  act->setCheckable(true);
  act->setChecked(u_filter.any);
  act->setShortcut(QKeySequence(tr("ctrl+6")));
  act->setData(qVariantFromValue((void *) &u_filter.any));
  filter_any->addAction(act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_filter_other()));
  act = filter_menu->addAction(_("Full HP"));
  act->setCheckable(true);
  act->setChecked(u_filter.idle);
  act->setShortcut(QKeySequence(tr("ctrl+7")));
  act->setData(qVariantFromValue((void *) &u_filter.full_hp));
  filter_any->addAction(act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_filter_other()));
  act = filter_menu->addAction(_("Full MP"));
  act->setCheckable(true);
  act->setChecked(u_filter.full_mp);
  act->setShortcut(QKeySequence(tr("ctrl+8")));
  act->setData(qVariantFromValue((void *) &u_filter.full_mp));
  filter_any->addAction(act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_filter_other()));
  act = filter_menu->addAction(_("Full HP and MP"));
  act->setCheckable(true);
  act->setChecked(u_filter.full_hp_mp);
  act->setShortcut(QKeySequence(tr("ctrl+9")));
  act->setData(qVariantFromValue((void *) &u_filter.full_hp_mp));
  filter_any->addAction(act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_filter_other()));

  /* Unit Menu */
  menu = this->addMenu(_("Unit"));
  act = menu->addAction(_("Go to Tile"));
  act->setShortcut(QKeySequence(tr("g")));
  menu_list.insertMulti(STANDARD, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_unit_goto()));
  act = menu->addAction(_("Go to Nearest City"));
  act->setShortcut(QKeySequence(tr("shift+g")));
  menu_list.insertMulti(GOTO_CITY, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_return_to_city()));
  act = menu->addAction(_("Go to/Airlift to City..."));
  act->setShortcut(QKeySequence(tr("t")));
  menu_list.insertMulti(AIRLIFT, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_airlift()));
  menu->addSeparator();
  act = menu->addAction(_("Auto Explore"));
  menu_list.insertMulti(EXPLORE, act);
  act->setShortcut(QKeySequence(tr("x")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_unit_explore()));
  act = menu->addAction(_("Patrol"));
  menu_list.insertMulti(STANDARD, act);
  act->setEnabled(false);
  act->setShortcut(QKeySequence(tr("q")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_patrol()));
  menu->addSeparator();
  act = menu->addAction(_("Sentry"));
  act->setShortcut(QKeySequence(tr("s")));
  menu_list.insertMulti(SENTRY, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_unit_sentry()));
  act = menu->addAction(_("Unsentry All On Tile"));
  act->setShortcut(QKeySequence(tr("shift+s")));
  menu_list.insertMulti(WAKEUP, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_unsentry()));
  menu->addSeparator();
  act = menu->addAction(_("Load"));
  act->setShortcut(QKeySequence(tr("l")));
  menu_list.insertMulti(LOAD, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_load()));
  act = menu->addAction(_("Unload"));
  act->setShortcut(QKeySequence(tr("u")));
  menu_list.insertMulti(UNLOAD, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_unload()));
  act = menu->addAction(_("Unload All From Transporter"));
  act->setShortcut(QKeySequence(tr("shift+u")));
  menu_list.insertMulti(TRANSPORTER, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_unload_all()));
  menu->addSeparator();
  act = menu->addAction(_("Set Home City"));
  menu_list.insertMulti(HOMECITY, act);
  act->setShortcut(QKeySequence(tr("h")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_set_home()));
  act = menu->addAction(_("Upgrade"));
  act->setShortcut(QKeySequence(tr("shift+u")));
  menu_list.insertMulti(UPGRADE, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_upgrade()));
  act = menu->addAction(_("Convert"));
  act->setShortcut(QKeySequence(tr("ctrl+o")));
  menu_list.insertMulti(CONVERT, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_convert()));
  act = menu->addAction(_("Disband"));
  act->setShortcut(QKeySequence(tr("shift+d")));
  menu_list.insertMulti(DISBAND, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_disband()));

  /* Combat Menu */
  menu = this->addMenu(_("Combat"));
  act = menu->addAction(_("Fortify Unit"));
  menu_list.insertMulti(FORTRESS, act);
  act->setShortcut(QKeySequence(tr("f")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_unit_fortify()));
  act = menu->addAction(_("Build Type B Base"));
  menu_list.insertMulti(AIRBASE, act);
  act->setShortcut(QKeySequence(tr("e")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_unit_airbase()));
  menu->addSeparator();
  act = menu->addAction(_("Pillage"));
  menu_list.insertMulti(PILLAGE, act);
  act->setShortcut(QKeySequence(tr("shift+p")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_pillage()));
  act = menu->addAction(_("Diplomat/Spy actions"));
  menu_list.insertMulti(ORDER_DIPLOMAT_DLG, act);
  act->setShortcut(QKeySequence(tr("d")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_action()));
  act = menu->addAction(_("Explode Nuclear"));
  menu_list.insertMulti(NUKE, act);
  act->setShortcut(QKeySequence(tr("shift+n")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_explode_nuclear()));

  /* Work Menu */
  menu = this->addMenu(_("Work"));
  act = menu->addAction(_("Build City"));
  act->setShortcut(QKeySequence(tr("b")));
  menu_list.insertMulti(BUILD, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_build_city()));
  act = menu->addAction(_("Go And Build City"));
  menu_list.insertMulti(GO_AND_BUILD_CITY, act);
  act->setShortcut(QKeySequence(tr("shift+b")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_go_build_city()));
  act = menu->addAction(_("Auto Settler"));
  act->setShortcut(QKeySequence(tr("a")));
  menu_list.insertMulti(AUTOSETTLER, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_auto_settler()));
  menu->addSeparator();
  act = menu->addAction(_("Build Road"));
  menu_list.insertMulti(ROAD, act);
  act->setShortcut(QKeySequence(tr("r")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_build_road()));
  act = menu->addAction(_("Build Irrigation"));
  act->setShortcut(QKeySequence(tr("i")));
  menu_list.insertMulti(IRRIGATION, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_build_irrigation()));
  act = menu->addAction(_("Build Mine"));
  act->setShortcut(QKeySequence(tr("m")));
  menu_list.insertMulti(MINE, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_build_mine()));
  menu->addSeparator();
  act = menu->addAction(_("Connect With Road"));
  act->setShortcut(QKeySequence(tr("shift+r")));
  menu_list.insertMulti(CONNECT_ROAD, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_conn_road()));
  act = menu->addAction(_("Connect With Railroad"));
  menu_list.insertMulti(CONNECT_RAIL, act);
  act->setShortcut(QKeySequence(tr("shift+l")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_conn_rail()));
  act = menu->addAction(_("Connect With Irrigation"));
  menu_list.insertMulti(CONNECT_IRRIGATION, act);
  act->setShortcut(QKeySequence(tr("shift+i")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_conn_irrigation()));
  menu->addSeparator();
  act = menu->addAction(_("Transform Terrain"));
  menu_list.insertMulti(TRANSFORM, act);
  act->setShortcut(QKeySequence(tr("o")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_transform()));
  act = menu->addAction(_("Clean Pollution"));
  menu_list.insertMulti(POLLUTION, act);
  act->setShortcut(QKeySequence(tr("p")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_clean_pollution()));
  act = menu->addAction(_("Clean Nuclear Fallout"));
  menu_list.insertMulti(FALLOUT, act);
  act->setShortcut(QKeySequence(tr("n")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_clean_fallout()));
  act = menu->addAction(_("Help build Wonder"));
  act->setShortcut(QKeySequence(tr("b")));
  menu_list.insertMulti(BUILD_WONDER, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_build_city()));
  act = menu->addAction(_("Establish Trade Route"));
  act->setShortcut(QKeySequence(tr("r")));
  menu_list.insertMulti(ORDER_TRADEROUTE, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_build_road()));

  /* Civilization menu */
  menu = this->addMenu(_("Civilization"));
  act = menu->addAction(_("Tax Rates..."));
  menu_list.insertMulti(NOT_4_OBS, act);
  connect(act, SIGNAL(triggered()), this, SLOT(slot_popup_tax_rates()));
  menu->addSeparator();

  menu->addMenu(new class gov_menu());
  menu->addSeparator();

  act = menu->addAction(Q_("?noun:View"));
  act->setShortcut(QKeySequence(tr("F1")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_show_map()));

  act = menu->addAction(_("Units"));
  act->setShortcut(QKeySequence(tr("F2")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_show_units_report()));

  /* TRANS: Also menu item, but 'headers' should be good enough. */
  act = menu->addAction(Q_("?header:Players"));
  act->setShortcut(QKeySequence(tr("F3")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_show_nations()));

  act = menu->addAction(_("Cities"));
  act->setShortcut(QKeySequence(tr("F4")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_show_cities()));

  act = menu->addAction(_("Economy"));
  act->setShortcut(QKeySequence(tr("F5")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_show_eco_report()));

  act = menu->addAction(_("Research"));
  act->setShortcut(QKeySequence(tr("F6")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_show_research_tab()));

  act = menu->addAction(_("Wonders of the World"));
  act->setShortcut(QKeySequence(tr("F7")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_traveler()));

  act = menu->addAction(_("Top Five Cities"));
  act->setShortcut(QKeySequence(tr("F8")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_top_five()));

  act = menu->addAction(_("Demographics"));
  act->setShortcut(QKeySequence(tr("F11")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_demographics()));

  act = menu->addAction(_("Spaceship"));
  act->setShortcut(QKeySequence(tr("F12")));
  connect(act, SIGNAL(triggered()), this, SLOT(slot_spaceship()));

  /* Help Menu */
  menu = this->addMenu(_("Help"));

  signal_help_mapper = new QSignalMapper(this);
  connect(signal_help_mapper, SIGNAL(mapped(const QString &)),
          this, SLOT(slot_help(const QString &)));

  act = menu->addAction(Q_(HELP_OVERVIEW_ITEM));
  connect(act, SIGNAL(triggered()), signal_help_mapper, SLOT(map()));
  signal_help_mapper->setMapping(act, HELP_OVERVIEW_ITEM);

  act = menu->addAction(Q_(HELP_PLAYING_ITEM));
  connect(act, SIGNAL(triggered()), signal_help_mapper, SLOT(map()));
  signal_help_mapper->setMapping(act, HELP_PLAYING_ITEM);

  act = menu->addAction(Q_(HELP_TERRAIN_ITEM));
  connect(act, SIGNAL(triggered()), signal_help_mapper, SLOT(map()));
  signal_help_mapper->setMapping(act, HELP_TERRAIN_ITEM);

  act = menu->addAction(Q_(HELP_ECONOMY_ITEM));
  connect(act, SIGNAL(triggered()), signal_help_mapper, SLOT(map()));
  signal_help_mapper->setMapping(act, HELP_ECONOMY_ITEM);

  act = menu->addAction(Q_(HELP_CITIES_ITEM));
  connect(act, SIGNAL(triggered()), signal_help_mapper, SLOT(map()));
  signal_help_mapper->setMapping(act, HELP_CITIES_ITEM);

  act = menu->addAction(Q_(HELP_IMPROVEMENTS_ITEM));
  connect(act, SIGNAL(triggered()), signal_help_mapper, SLOT(map()));
  signal_help_mapper->setMapping(act, HELP_IMPROVEMENTS_ITEM);

  act = menu->addAction(Q_(HELP_WONDERS_ITEM));
  connect(act, SIGNAL(triggered()), signal_help_mapper, SLOT(map()));
  signal_help_mapper->setMapping(act, HELP_WONDERS_ITEM);

  act = menu->addAction(Q_(HELP_UNITS_ITEM));
  connect(act, SIGNAL(triggered()), signal_help_mapper, SLOT(map()));
  signal_help_mapper->setMapping(act, HELP_UNITS_ITEM);

  act = menu->addAction(Q_(HELP_COMBAT_ITEM));
  connect(act, SIGNAL(triggered()), signal_help_mapper, SLOT(map()));
  signal_help_mapper->setMapping(act, HELP_COMBAT_ITEM);

  act = menu->addAction(Q_(HELP_ZOC_ITEM));
  connect(act, SIGNAL(triggered()), signal_help_mapper, SLOT(map()));
  signal_help_mapper->setMapping(act, HELP_ZOC_ITEM);

  act = menu->addAction(Q_(HELP_GOVERNMENT_ITEM));
  connect(act, SIGNAL(triggered()), signal_help_mapper, SLOT(map()));
  signal_help_mapper->setMapping(act, HELP_GOVERNMENT_ITEM);

  act = menu->addAction(Q_(HELP_ECONOMY_ITEM));
  connect(act, SIGNAL(triggered()), signal_help_mapper, SLOT(map()));
  signal_help_mapper->setMapping(act, HELP_ECONOMY_ITEM);

  act = menu->addAction(Q_(HELP_DIPLOMACY_ITEM));
  connect(act, SIGNAL(triggered()), signal_help_mapper, SLOT(map()));
  signal_help_mapper->setMapping(act, HELP_DIPLOMACY_ITEM);

  act = menu->addAction(Q_(HELP_TECHS_ITEM));
  connect(act, SIGNAL(triggered()), signal_help_mapper, SLOT(map()));
  signal_help_mapper->setMapping(act, HELP_TECHS_ITEM);

  act = menu->addAction(Q_(HELP_SPACE_RACE_ITEM));
  connect(act, SIGNAL(triggered()), signal_help_mapper, SLOT(map()));
  signal_help_mapper->setMapping(act, HELP_SPACE_RACE_ITEM);

  act = menu->addAction(Q_(HELP_IMPROVEMENTS_ITEM));
  connect(act, SIGNAL(triggered()), signal_help_mapper, SLOT(map()));
  signal_help_mapper->setMapping(act, HELP_IMPROVEMENTS_ITEM);

  act = menu->addAction(Q_(HELP_RULESET_ITEM));
  connect(act, SIGNAL(triggered()), signal_help_mapper, SLOT(map()));
  signal_help_mapper->setMapping(act, HELP_RULESET_ITEM);

  act = menu->addAction(Q_(HELP_NATIONS_ITEM));
  connect(act, SIGNAL(triggered()), signal_help_mapper, SLOT(map()));
  signal_help_mapper->setMapping(act, HELP_NATIONS_ITEM);

  menu->addSeparator();

  act = menu->addAction(Q_(HELP_CONNECTING_ITEM));
  connect(act, SIGNAL(triggered()), signal_help_mapper, SLOT(map()));
  signal_help_mapper->setMapping(act, HELP_CONNECTING_ITEM);

  act = menu->addAction(Q_(HELP_CONTROLS_ITEM));
  connect(act, SIGNAL(triggered()), signal_help_mapper, SLOT(map()));
  signal_help_mapper->setMapping(act, HELP_CONTROLS_ITEM);

  act = menu->addAction(Q_(HELP_CMA_ITEM));
  connect(act, SIGNAL(triggered()), signal_help_mapper, SLOT(map()));
  signal_help_mapper->setMapping(act, HELP_CMA_ITEM);

  act = menu->addAction(Q_(HELP_CHATLINE_ITEM));
  connect(act, SIGNAL(triggered()), signal_help_mapper, SLOT(map()));
  signal_help_mapper->setMapping(act, HELP_CHATLINE_ITEM);

  act = menu->addAction(Q_(HELP_WORKLIST_EDITOR_ITEM));
  connect(act, SIGNAL(triggered()), signal_help_mapper, SLOT(map()));
  signal_help_mapper->setMapping(act, HELP_WORKLIST_EDITOR_ITEM);

  menu->addSeparator();

  act = menu->addAction(Q_(HELP_LANGUAGES_ITEM));
  connect(act, SIGNAL(triggered()), signal_help_mapper, SLOT(map()));
  signal_help_mapper->setMapping(act, HELP_LANGUAGES_ITEM);

  act = menu->addAction(Q_(HELP_COPYING_ITEM));
  connect(act, SIGNAL(triggered()), signal_help_mapper, SLOT(map()));
  signal_help_mapper->setMapping(act, HELP_COPYING_ITEM);

  act = menu->addAction(Q_(HELP_ABOUT_ITEM));
  connect(act, SIGNAL(triggered()), signal_help_mapper, SLOT(map()));
  signal_help_mapper->setMapping(act, HELP_ABOUT_ITEM);

  this->setVisible(false);
}

/****************************************************************************
  Enables/disables menu items and renames them depending on key in menu_list
****************************************************************************/
void mr_menu::menus_sensitive()
{
  QList <QAction * >values;
  QList <munit > keys;
  QHash <munit, QAction *>::iterator i;
  struct unit_list *punits = NULL;
  struct road_type *proad;
  struct act_tgt tgt;
  bool any_cities = false;
  bool city_on_tile = false;
  bool units_all_same_tile = true;
  const struct tile *ptile = NULL;
  struct terrain *pterrain;
  const struct unit_type *ptype = NULL;

  players_iterate(pplayer) {
    if (city_list_size(pplayer->cities)) {
      any_cities = true;
      break;
    }
  } players_iterate_end;

  /** Disable first all sensitive menus */
  foreach(QAction * a, menu_list) {
    a->setEnabled(false);
  }

  /* Non unit menus */
  keys = menu_list.keys();
  foreach (munit key, keys) {
    i = menu_list.find(key);
    while (i != menu_list.end() && i.key() == key) {
      switch (key) {
      case SAVE:
        if (can_client_access_hack() && C_S_RUNNING <= client_state()) {
          i.value()->setEnabled(true);
        }
        break;
      case NOT_4_OBS:
        if (client_is_observer() == false) {
          i.value()->setEnabled(true);
        }
      default:
        break;
      }
      i++;
    }
  }

  if (can_client_issue_orders() == false || get_num_units_in_focus() == 0) {
    return;
  }

  punits = get_units_in_focus();
  unit_list_iterate(punits, punit) {
    if (tile_city(unit_tile(punit))) {
      city_on_tile = true;
      break;
    }
  } unit_list_iterate_end;

  unit_list_iterate(punits, punit) {
    fc_assert((ptile == NULL) == (ptype == NULL));
    if (ptile || ptype) {
      if (unit_tile(punit) != ptile) {
        units_all_same_tile = false;
      }
      if (unit_type(punit) == ptype) {
        ptile = unit_tile(punit);
        ptype = unit_type(punit);
      }
    }
  } unit_list_iterate_end;

  keys = menu_list.keys();
  foreach(munit key, keys) {
    i = menu_list.find(key);
    while (i != menu_list.end() && i.key() == key) {
      switch (key) {
      case STANDARD:
        i.value()->setEnabled(true);
        break;

      case EXPLORE:
        if (can_units_do_activity(punits, ACTIVITY_EXPLORE)) {
          i.value()->setEnabled(true);
        }
        break;

      case LOAD:
        if (units_can_load(punits)) {
          i.value()->setEnabled(true);
        }
        break;

      case UNLOAD:
        if (units_can_unload(punits)) {
          i.value()->setEnabled(true);
        }
        break;

      case TRANSPORTER:
        if (units_are_occupied(punits)) {
          i.value()->setEnabled(true);
        }
        break;

      case CONVERT:
        if (units_can_convert(punits)) {
          i.value()->setEnabled(true);
        }
        break;

      case MINE:
        if (can_units_do_activity(punits, ACTIVITY_MINE)) {
          i.value()->setEnabled(true);
        }

        if (units_all_same_tile) {
          struct unit *punit = unit_list_get(punits, 0);
          pterrain = tile_terrain(unit_tile(punit));
          if (pterrain->mining_result != T_NONE
              && pterrain->mining_result != pterrain) {
            i.value()->setText(
              QString(_("Transform to %1")).
              /* TRANS: Transfrom terrain to specific type */
              arg(QString(get_tile_change_menu_text
                          (unit_tile(punit), ACTIVITY_MINE))));
          } else {
            i.value()->setText(_("Build Mine"));
          }
        }
        break;

      case IRRIGATION:
        if (can_units_do_activity(punits, ACTIVITY_IRRIGATE)) {
          i.value()->setEnabled(true);
        }
        if (units_all_same_tile) {
          struct unit *punit = unit_list_get(punits, 0);
          pterrain = tile_terrain(unit_tile(punit));
          if (pterrain->irrigation_result != T_NONE
              && pterrain->irrigation_result != pterrain) {
            i.value()->setText(QString(_("Transform to %1")).
                               /* TRANS: Transfrom terrain to specific type */
                               arg(QString(get_tile_change_menu_text
                                           (unit_tile(punit),
                                           ACTIVITY_IRRIGATE))));
          } else if (tile_has_special(unit_tile(punit), S_IRRIGATION)
                     && player_knows_techs_with_flag(unit_owner(punit),
                                                     TF_FARMLAND)) {
            i.value()->setText(QString(_("Build Farmland")));
          } else {
            i.value()->setText(QString(_("Build Irrigation")));
          }
        }
        break;

      case TRANSFORM:
        if (can_units_do_activity(punits, ACTIVITY_TRANSFORM)) {
          i.value()->setEnabled(true);
        } else {
          break;
        }
        if (units_all_same_tile) {
          struct unit *punit = unit_list_get(punits, 0);
          pterrain = tile_terrain(unit_tile(punit));
          punit = unit_list_get(punits, 0);
          pterrain = tile_terrain(unit_tile(punit));
          if (pterrain->transform_result != T_NONE
              && pterrain->transform_result != pterrain) {
            i.value()->setText(QString(_("Transform to %1")).
                        /* TRANS: Transfrom terrain to specific type */
                        arg(QString(get_tile_change_menu_text
                        (unit_tile(punit), ACTIVITY_TRANSFORM))));
          } else {
            i.value()->setText(_("Transform Terrain"));
          }
        }
        break;

      case BUILD:
        if (can_units_do(punits, unit_can_add_or_build_city)) {
          i.value()->setEnabled(true);
        } else {
          break;
        }
        if (city_on_tile
            && units_have_type_flag(punits, UTYF_ADD_TO_CITY, true)) {
          i.value()->setText(_("Add to City"));
        } else {
          i.value()->setText(_("Build City"));
        }
        break;

      case ROAD:
        if (can_units_do_any_road(punits)) {
          i.value()->setEnabled(true);
        }
        break;

      case FORTRESS:
        if (can_units_do_base_gui(punits, BASE_GUI_FORTRESS)
            || can_units_do_activity(punits, ACTIVITY_FORTIFYING)) {
          i.value()->setEnabled(true);
        }
        if (can_units_do_activity(punits, ACTIVITY_FORTIFYING)) {
          i.value()->setText(_("Fortify Unit"));
        } else {
          i.value()->setText(_("Build Type A Base"));
        }
        break;

      case AIRBASE:
        if (can_units_do_base_gui(punits, BASE_GUI_AIRBASE)) {
          i.value()->setEnabled(true);
        }
        break;

      case POLLUTION:
        if (can_units_do_activity(punits, ACTIVITY_POLLUTION)
            || can_units_do(punits, can_unit_paradrop)) {
          i.value()->setEnabled(true);
        }
        if (units_have_type_flag(punits, UTYF_PARATROOPERS, true)) {
          i.value()->setText(_("Drop Paratrooper"));
        } else {
          i.value()->setText(_("Clean Pollution"));
        }
        break;

      case FALLOUT:
        if (can_units_do_activity(punits, ACTIVITY_FALLOUT)) {
          i.value()->setEnabled(true);
        }
        break;

      case SENTRY:
        if (can_units_do_activity(punits, ACTIVITY_SENTRY)) {
          i.value()->setEnabled(true);
        }
        break;

      case PILLAGE:
        if (can_units_do_activity(punits, ACTIVITY_PILLAGE)) {
          i.value()->setEnabled(true);
        }
        break;

      case HOMECITY:
        if (can_units_do(punits, can_unit_change_homecity)) {
          i.value()->setEnabled(true);
        }
        break;

      case WAKEUP:
        if (units_have_activity_on_tile(punits, ACTIVITY_SENTRY)) {
          i.value()->setEnabled(true);
        }
        break;

      case AUTOSETTLER:
        if (can_units_do(punits, can_unit_do_autosettlers)) {
          i.value()->setEnabled(true);
        }
        if (units_have_type_flag(punits, UTYF_CITIES, true)) {
          i.value()->setText(_("Auto Settler"));
        } else {
          i.value()->setText(_("Auto Worker"));
        }
        break;
      case GO_AND_BUILD_CITY:
        if (units_have_type_flag(punits, UTYF_CITIES, TRUE)) {
          i.value()->setEnabled(true);
        }
        break;
      case CONNECT_ROAD:
        proad = road_by_compat_special(ROCO_ROAD);
        if (proad != NULL) {
          tgt.type = ATT_ROAD;
          tgt.obj.road = road_number(proad);
        } else {
          break;
        }
        if (can_units_do_connect(punits, ACTIVITY_GEN_ROAD, &tgt)) {
          i.value()->setEnabled(true);
        }
        break;

      case DISBAND:
        if (units_have_type_flag(punits, UTYF_UNDISBANDABLE, false)) {
          i.value()->setEnabled(true);
        }

      case CONNECT_RAIL:
        proad = road_by_compat_special(ROCO_RAILROAD);
        if (proad != NULL) {
          tgt.type = ATT_ROAD;
          tgt.obj.road = road_number(proad);
        } else {
          break;
        }
        if (can_units_do_connect(punits, ACTIVITY_GEN_ROAD, &tgt)) {
          i.value()->setEnabled(true);
        }
        break;

      case CONNECT_IRRIGATION:
        if (can_units_do_connect(punits, ACTIVITY_IRRIGATE, NULL)) {
          i.value()->setEnabled(true);
        }
        break;

      case GOTO_CITY:
        if (any_cities) {
          i.value()->setEnabled(true);
        }
        break;

      case AIRLIFT:
        if (any_cities) {
          i.value()->setEnabled(true);
        }
        break;

      case BUILD_WONDER:
        if (can_units_do(punits, unit_can_help_build_wonder_here)) {
          i.value()->setEnabled(true);
        }
        break;

      case ORDER_TRADEROUTE:
        if (can_units_do(punits, unit_can_est_trade_route_here)) {
          i.value()->setEnabled(true);
        }
        break;

      case ORDER_DIPLOMAT_DLG:
        if (can_units_do_diplomat_action(punits, DIPLOMAT_ANY_ACTION)) {
          i.value()->setEnabled(true);
        }
        break;

      case NUKE:
        if (units_have_type_flag(punits, UTYF_NUCLEAR, true)) {
          i.value()->setEnabled(true);
        }
        break;

      case UPGRADE:
        if (units_can_upgrade(punits)) {
          i.value()->setEnabled(true);
        }
        break;
      default:
        break;
      }

      i++;
    }
  }
}

/***************************************************************************
  Slot for showing research tab
***************************************************************************/
void mr_menu::slot_show_research_tab()
{
  science_report_dialog_popup(true);
}

/***************************************************************************
  Slot for showing spaceship
***************************************************************************/
void mr_menu::slot_spaceship()
{
  if (NULL != client.conn.playing) {
    popup_spaceship_dialog(client.conn.playing);
  }
}

/***************************************************************************
  Slot for showing economy tab
***************************************************************************/
void mr_menu::slot_show_eco_report()
{
  economy_report_dialog_popup(false);
}

/***************************************************************************
  Changes tab to mapview
***************************************************************************/
void mr_menu::slot_show_map()
{
  ::gui()->game_tab_widget->setCurrentIndex(0);
}

/***************************************************************************
  Slot for showing units tab
***************************************************************************/
void mr_menu::slot_show_units_report()
{
  units_report_dialog_popup(false);
}

/***************************************************************************
  Slot for showing nations report
***************************************************************************/
void mr_menu::slot_show_nations()
{
  popup_players_dialog(false);
}

/***************************************************************************
  Slot for showing cities report
***************************************************************************/
void mr_menu::slot_show_cities()
{
  city_report_dialog_popup(false);
}

/****************************************************************
  Action "BUILD_CITY"
*****************************************************************/
void mr_menu::slot_build_city()
{
  unit_list_iterate(get_units_in_focus(), punit) {
    /* FIXME: this can provide different actions for different units...
     * not good! */
    /* Enable the button for adding to a city in all cases, so we
       get an eventual error message from the server if we try. */
    if (unit_can_add_or_build_city(punit)) {
      request_unit_build_city(punit);
    } else if (unit_has_type_flag(punit, UTYF_HELP_WONDER)) {
      request_unit_caravan_action(punit, PACKET_UNIT_HELP_BUILD_WONDER);
    }
  } unit_list_iterate_end;
}

/***************************************************************************
  Action "CLEAN FALLOUT"
***************************************************************************/
void mr_menu::slot_clean_fallout()
{
  key_unit_fallout();
}

/***************************************************************************
  Action "CLEAN POLLUTION and PARADROP"
***************************************************************************/
void mr_menu::slot_clean_pollution()
{
  unit_list_iterate(get_units_in_focus(), punit) {
    /* FIXME: this can provide different actions for different units...
     * not good! */
    if (can_unit_do_activity(punit, ACTIVITY_POLLUTION)) {
      request_new_unit_activity(punit, ACTIVITY_POLLUTION);
    } else if (can_unit_paradrop(punit)) {
      /* FIXME: This is getting worse, we use a key_unit_*() function
       * which assign the order for all units!  Very bad! */
      key_unit_paradrop();
    }
  } unit_list_iterate_end;
}

/***************************************************************************
  Action "CONNECT WITH IRRIGATION"
***************************************************************************/
void mr_menu::slot_conn_irrigation()
{
  key_unit_connect(ACTIVITY_IRRIGATE, NULL);
}

/***************************************************************************
  Action "CONNECT WITH RAILROAD"
***************************************************************************/
void mr_menu::slot_conn_rail()
{
  struct road_type *prail = road_by_compat_special(ROCO_RAILROAD);
  struct act_tgt tgt;
  if (prail != NULL) {
    tgt.type = ATT_ROAD;
    tgt.obj.road = road_number(prail);
    key_unit_connect(ACTIVITY_GEN_ROAD, &tgt);
  }
}

/***************************************************************************
  Action "BUILD AIRBASE"
***************************************************************************/
void mr_menu::slot_unit_airbase()
{
  key_unit_airbase();
}

/***************************************************************************
  Action "CONNECT WITH ROAD"
***************************************************************************/
void mr_menu::slot_conn_road()
{
  struct road_type *proad = road_by_compat_special(ROCO_ROAD);
  struct act_tgt tgt;
  if (proad != NULL) {
    tgt.type = ATT_ROAD;
    tgt.obj.road = road_number(proad);
    key_unit_connect(ACTIVITY_GEN_ROAD, &tgt);
  }
}

/***************************************************************************
  Action "GO TO AND BUILD CITY"
***************************************************************************/
void mr_menu::slot_go_build_city()
{
  request_unit_goto(ORDER_BUILD_CITY);
}

/***************************************************************************
  Action "TRANSFROM TERRAIN"
***************************************************************************/
void mr_menu::slot_transform()
{
  key_unit_transform();
}

/***************************************************************************
  Action "PILLAGE"
***************************************************************************/
void mr_menu::slot_pillage()
{
  key_unit_pillage();
}

/***************************************************************************
  Action "EXPLODE NUCLEAR"
***************************************************************************/
void mr_menu::slot_explode_nuclear()
{
  key_unit_nuke();
}

/***************************************************************************
  Diplomat/Spy action
***************************************************************************/
void mr_menu::slot_action()
{
  key_unit_diplomat_actions();
}


/****************************************************************
  Action "AUTO_SETTLER"
*****************************************************************/
void mr_menu::slot_auto_settler()
{
  key_unit_auto_settle();
}

/****************************************************************
  Action "BUILD_ROAD"
*****************************************************************/
void mr_menu::slot_build_road()
{
  struct act_tgt tgt;
  unit_list_iterate(get_units_in_focus(), punit) {
    /* FIXME: this can provide different actions for different units...
     * not good! */
    struct road_type *proad = next_road_for_tile(unit_tile(punit),
                                                 unit_owner(punit),
                                                 punit);
    bool building_road = false;

    if (proad != NULL) {
      tgt.type = ATT_ROAD;
      tgt.obj.road = road_number(proad);

      if (can_unit_do_activity_targeted(punit, ACTIVITY_GEN_ROAD, &tgt)) {
        request_new_unit_activity_road(punit, proad);
        building_road = true;
      }
    }

    if (!building_road && unit_can_est_trade_route_here(punit)) {
      request_unit_caravan_action(punit, PACKET_UNIT_ESTABLISH_TRADE);
    }
  } unit_list_iterate_end;
}

/****************************************************************
  Action "BUILD_IRRIGATION"
*****************************************************************/
void mr_menu::slot_build_irrigation()
{
  key_unit_irrigate();
}

/****************************************************************
  Action "BUILD_MINE"
*****************************************************************/
void mr_menu::slot_build_mine()
{
  key_unit_mine();
}
/****************************************************************
  Action "FORTIFY AND BUILD BASE A (USUALLY FORTRESS)"
*****************************************************************/
void mr_menu::slot_unit_fortify()
{
  unit_list_iterate(get_units_in_focus(), punit) {
    /* FIXME: this can provide different actions for different units...
     * not good! */
    struct base_type *pbase = get_base_by_gui_type(BASE_GUI_FORTRESS,
                                                   punit, unit_tile(punit));

    if (pbase && can_unit_do_activity_base(punit, pbase->item_number)) {
      request_new_unit_activity_base(punit, pbase);
    } else {
      request_unit_fortify(punit);
    }
  } unit_list_iterate_end;
}
/****************************************************************
  Action "SENTRY"
*****************************************************************/
void mr_menu::slot_unit_sentry()
{
  key_unit_sentry();
}

/***************************************************************************
  Action "CONVERT"
***************************************************************************/
void mr_menu::slot_convert()
{
  key_unit_convert();
}

/***************************************************************************
  Action "DISBAND UNIT"
***************************************************************************/
void mr_menu::slot_disband()
{
  popup_disband_dialog(get_units_in_focus());
}

/***************************************************************************
  Action "LOAD INTO TRANSPORTER"
***************************************************************************/
void mr_menu::slot_load()
{
  unit_list_iterate(get_units_in_focus(), punit) {
    request_unit_load(punit, NULL);
  } unit_list_iterate_end;
}

/***************************************************************************
  Action "UNIT PATROL"
***************************************************************************/
void mr_menu::slot_patrol()
{
  key_unit_patrol();
}

/***************************************************************************
  Action "RETURN TO NEAREST CITY"
***************************************************************************/
void mr_menu::slot_return_to_city()
{
  unit_list_iterate(get_units_in_focus(), punit) {
    request_unit_return(punit);
  } unit_list_iterate_end;
}

/***************************************************************************
  Action "GOTO/AIRLIFT TO CITY"
***************************************************************************/
void mr_menu::slot_airlift()
{
  popup_goto_dialog();
}

/***************************************************************************
  Action "SET HOMECITY"
***************************************************************************/
void mr_menu::slot_set_home()
{
  key_unit_homecity();
}

/***************************************************************************
  Action "UNLOAD FROM TRANSPORTED"
***************************************************************************/
void mr_menu::slot_unload()
{
  unit_list_iterate(get_units_in_focus(), punit) {
    request_unit_unload(punit);
  } unit_list_iterate_end;
}

/***************************************************************************
  Action "UNLOAD ALL UNITS FROM TRANSPORTER"
***************************************************************************/
void mr_menu::slot_unload_all()
{
  key_unit_unload_all();
}

/***************************************************************************
  Action "UNSENTRY(WAKEUP) ALL UNITS"
***************************************************************************/
void mr_menu::slot_unsentry()
{
  key_unit_wakeup_others();
}

/***************************************************************************
  Action "UPGRADE UNITS"
***************************************************************************/
void mr_menu::slot_upgrade()
{
  popup_upgrade_dialog(get_units_in_focus());
}

/****************************************************************
  Action "GOTO"
*****************************************************************/
void mr_menu::slot_unit_goto()
{
  key_unit_goto();
}

/****************************************************************
  Action "EXPLORE"
*****************************************************************/
void mr_menu::slot_unit_explore()
{
  key_unit_auto_explore();
}

/****************************************************************
  Action "CENTER VIEW"
*****************************************************************/
void mr_menu::slot_center_view()
{
  request_center_focus_unit();
}

/***************************************************************************
  Action "SET FULLSCREEN"
***************************************************************************/
void mr_menu::slot_fullscreen()
{
  if (!fullscreen_mode){
    gui()->showFullScreen();
    gui()->mapview_wdg->showFullScreen();
  } else {
    gui()->showNormal();
  }
  fullscreen_mode = !fullscreen_mode;
}

/***************************************************************************
  Action "SHOW CHAT"
***************************************************************************/
void mr_menu::slot_show_chat()
{
  if (gui()->infotab->hidden_mess && !gui()->infotab->hidden_chat) {
    gui()->infotab->hide_chat(true);
    gui()->infotab->hide_me();
    return;
  }
  if (gui()->infotab->hidden_mess && gui()->infotab->hidden_chat) {
    gui()->infotab->hide_me();
    gui()->infotab->hide_messages(true);
    gui()->infotab->hide_chat(false);
    return;
  }
  if (!gui()->infotab->hidden_chat) {
    gui()->infotab->hide_chat(true);
  } else {
    gui()->infotab->hide_chat(false);
  }
}

/***************************************************************************
  Action "SHOW MESSAGES"
***************************************************************************/
void mr_menu::slot_show_messages()
{
  if (!gui()->infotab->hidden_mess && gui()->infotab->hidden_chat) {
    gui()->infotab->hide_messages(true);
    gui()->infotab->hide_me();
    return;
  }
  if (gui()->infotab->hidden_mess && gui()->infotab->hidden_chat) {
    gui()->infotab->hide_me();
    gui()->infotab->hide_messages(false);
    gui()->infotab->hide_chat(true);
    return;
  }
  if (!gui()->infotab->hidden_mess) {
    gui()->infotab->hide_messages(true);
  } else {
    gui()->infotab->hide_messages(false);
  }
}
/****************************************************************
  Action "VIEW/HIDE MINIMAP"
*****************************************************************/
void mr_menu::slot_minimap_view()
{
  if (minimap_status->isChecked()){
    ::gui()->minimapview_wdg->show();
  } else {
    ::gui()->minimapview_wdg->hide();
  }
}

/****************************************************************
  Action "SHOW DEMOGRAPGHICS REPORT"
*****************************************************************/
void mr_menu::slot_borders()
{
  key_map_borders_toggle();
}

/****************************************************************
  Action "SHOW NATIVE TILES"
*****************************************************************/
void mr_menu::slot_native_tiles()
{
  key_map_native_toggle();
}

/***************************************************************************
  Action "SHOW BUY COST"
***************************************************************************/
void mr_menu::slot_city_buycost()
{
  key_city_buycost_toggle();
}

/***************************************************************************
  Action "SHOW CITY GROWTH"
***************************************************************************/
void mr_menu::slot_city_growth()
{
  key_city_growth_toggle();
}

/***************************************************************************
  Action "SHOW CITY NAMES"
***************************************************************************/
void mr_menu::slot_city_names()
{
  key_city_names_toggle();
}

/***************************************************************************
  Action "SHOW CITY OUTLINES"
***************************************************************************/
void mr_menu::slot_city_outlines()
{
  key_city_outlines_toggle();
}

/***************************************************************************
  Action "SHOW CITY OUTPUT"
***************************************************************************/
void mr_menu::slot_city_output()
{
  key_city_output_toggle();
}

/***************************************************************************
  Action "SHOW CITY PRODUCTION"
***************************************************************************/
void mr_menu::slot_city_production()
{
  key_city_productions_toggle();
}

/***************************************************************************
  Action "SHOW CITY TRADEROUTES"
***************************************************************************/
void mr_menu::slot_city_traderoutes()
{
  key_city_trade_routes_toggle();
}

/***************************************************************************
  Action "SHOW FULLBAR"
***************************************************************************/
void mr_menu::slot_fullbar()
{
  key_city_full_bar_toggle();
}

/***************************************************************************
  Action "SHOW MAP GRID"
***************************************************************************/
void mr_menu::slot_map_grid()
{
  key_map_grid_toggle();
}

/***************************************************************************
  Action "DONE MOVING"
***************************************************************************/
void mr_menu::slot_done_moving()
{
  key_unit_done();
}

/***************************************************************************
  Action "SELECT ALL UNITS ON TILE"
***************************************************************************/
void mr_menu::slot_select_all_tile()
{
  unit_select(get_units_in_focus(), SELTYPE_ALL, SELLOC_TILE);
}

/***************************************************************************
  Action "SELECT ONE UNITS/DESELECT OTHERS"
***************************************************************************/
void mr_menu::slot_select_one()
{
  unit_select(get_units_in_focus(), SELTYPE_SINGLE, SELLOC_TILE);
}

/***************************************************************************
  Action "SELLECT SAME UNITS ON CONTINENT"
***************************************************************************/
void mr_menu::slot_select_same_continent()
{
  unit_select(get_units_in_focus(), SELTYPE_SAME, SELLOC_CONT);
}

/***************************************************************************
  Action "SELECT SAME TYPE EVERYWHERE"
***************************************************************************/
void mr_menu::slot_select_same_everywhere()
{
  unit_select(get_units_in_focus(), SELTYPE_SAME, SELLOC_WORLD);
}

/***************************************************************************
  Action "SELECT SAME TYPE ON TILE"
***************************************************************************/
void mr_menu::slot_select_same_tile()
{
  unit_select(get_units_in_focus(), SELTYPE_SAME, SELLOC_TILE);
}


/***************************************************************************
  Action "WAIT"
***************************************************************************/
void mr_menu::slot_wait()
{
  key_unit_wait();
}

/***************************************************************************
  Filter units has been changed
***************************************************************************/
void mr_menu::slot_filter()
{
  QAction *act;
  QVariant v;
  bool *bp;
  bool b;

  /* toggle u_filter.value */
  act = qobject_cast<QAction *>(sender());
  v = act->data();
  bp = (bool *) v.value<void *>();
  b = *bp;
  u_filter.reset_activity();
  *bp = !b;
}

/***************************************************************************
  Second filter units has been changed
***************************************************************************/
void mr_menu::slot_filter_other()
{
  QAction *act;
  QVariant v;
  bool *bp;
  bool b;

  /* toggle u_filter.value */
  act = qobject_cast<QAction *>(sender());
  v = act->data();
  bp = (bool *) v.value<void *>();
  b = *bp;
  u_filter.reset_other();
  *bp = !b;
}

/****************************************************************
  Action "SHOW DEMOGRAPGHICS REPORT"
*****************************************************************/
void mr_menu::slot_demographics()
{
  send_report_request(REPORT_DEMOGRAPHIC);
}

/****************************************************************
  Action "SHOW TOP FIVE CITIES"
*****************************************************************/
void mr_menu::slot_top_five()
{
  send_report_request(REPORT_TOP_5_CITIES);
}

/****************************************************************
  Action "SHOW WONDERS REPORT"
*****************************************************************/
void mr_menu::slot_traveler()
{
  send_report_request(REPORT_WONDERS_OF_THE_WORLD);
}


/****************************************************************
  Action "TAX RATES"
*****************************************************************/
void mr_menu::slot_popup_tax_rates()
{
  popup_rates_dialog();
}

/****************************************************************
  Actions "HELP_*"
*****************************************************************/
void mr_menu::slot_help(const QString &topic)
{
  popup_help_dialog_typed(Q_(topic.toStdString().c_str()), HELP_ANY);
}

/****************************************************************
  Invoke dialog with local options
*****************************************************************/
void mr_menu::local_options()
{
  gui()->popup_client_options();
}

/****************************************************************
  Invoke dialog with server options
*****************************************************************/
void mr_menu::server_options()
{
  gui()->pr_options->popup_server_options();
}

/****************************************************************
  Invoke dialog with server options
*****************************************************************/
void mr_menu::messages_options()
{
  popup_messageopt_dialog();
}

/****************************************************************
  Menu Save Options Now
*****************************************************************/
void mr_menu::save_options_now()
{
  options_save(NULL);
}

/***************************************************************************
  Invoke popup for quiting game
***************************************************************************/
void mr_menu::quit_game()
{
  popup_quit_dialog();
}

/***************************************************************************
  Menu Save Game
***************************************************************************/
void mr_menu::save_game()
{
  send_save_game(NULL);
}

/***************************************************************************
  Menu Save Game As...
***************************************************************************/
void mr_menu::save_game_as()
{
  QString str;
  QString current_file;
  QString location;

  strvec_iterate(get_save_dirs(), dirname) {
    location = dirname;
    // choose last location
  } strvec_iterate_end;

  str = QString(_("Save Games"))
        + QString(" (*.sav *.sav.bz2 *.sav.gz *.sav.xz)");
  current_file = QFileDialog::getSaveFileName(gui()->central_wdg,
                                              _("Save Game As..."),
                                              location, str);
  if (current_file.isEmpty() == false) {
    send_save_game(current_file.toLocal8Bit().data());
  }
}

/***************************************************************************
  Back to Main Menu
***************************************************************************/
void mr_menu::back_to_menu()
{
  QMessageBox ask(gui()->central_wdg);
  int ret;
  if (is_server_running()) {
    ask.setText(_("Leaving a local game will end it!"));
    ask.setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
    ask.setDefaultButton(QMessageBox::Cancel);
    ask.setIcon(QMessageBox::Warning);
    ask.setWindowTitle("Leave game");
    ret = ask.exec();

    switch (ret) {
    case QMessageBox::Cancel:
      break;
    case QMessageBox::Ok:
      if (client.conn.used) {
        disconnect_from_server();
      }
      break;
    }
  } else {
    disconnect_from_server();
  }
}

