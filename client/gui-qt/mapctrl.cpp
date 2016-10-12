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

// std
#include <stdlib.h>

// Qt
#include <QApplication>
#include <QKeyEvent>
#include <QPushButton>
#include <QMouseEvent>

//common
#include "control.h"
#include "goto.h"

// client
#include "client_main.h"
#include "climap.h"
#include "control.h"
#include "mapctrl.h"
#include "tile.h"
#include "unit.h"

// gui-qt
#include "fc_client.h"
#include "citydlg.h"
#include "shortcuts.h"
#include "qtg_cxxside.h"

extern void side_disable_endturn(bool do_restore);

/**************************************************************************
  Popup a dialog to ask for the name of a new city.  The given string
  should be used as a suggestion.
**************************************************************************/
void popup_newcity_dialog(struct unit *punit, const char *suggestname)
{
  hud_input_box ask(gui()->central_wdg);
  int index = tile_index(unit_tile(punit));
  ask.set_text_title_definput(_("What should we call our new city?"),
                              _("Build New City"), QString(suggestname));
  if (ask.exec() == QDialog::Accepted) {
    finish_city(index_to_tile(index),
                ask.input_edit.text().toLocal8Bit().data());
  } else {
    cancel_city(index_to_tile(index));
  }

  return;
}

/**************************************************************************
  A turn done button should be provided for the player.  This function
  is called to toggle it between active/inactive.
**************************************************************************/
void set_turn_done_button_state(bool state)
{
  side_disable_endturn(state);
}

/**************************************************************************
  Draw a goto or patrol line at the current mouse position.
**************************************************************************/
void create_line_at_mouse_pos(void)
{
  QPoint global_pos, local_pos;
  int x, y;

  global_pos = QCursor::pos();
  local_pos = gui()->mapview_wdg->mapFromGlobal(global_pos);
  x = local_pos.x();
  y = local_pos.y();

  if (x >= 0 && y >= 0 && x < mapview.width && y < mapview.width) {
    update_line(x, y);
  }
}

/**************************************************************************
 The Area Selection rectangle. Called by center_tile_mapcanvas() and
 when the mouse pointer moves.
**************************************************************************/
void update_rect_at_mouse_pos(void)
{
  /* PLS DONT PORT IT */
}

/**************************************************************************
  Keyboard handler for map_view
**************************************************************************/
void map_view::keyPressEvent(QKeyEvent * event)
{
  QPoint local_pos;
  Qt::KeyboardModifiers key_mod = QApplication::keyboardModifiers();
  bool is_shift = key_mod.testFlag(Qt::ShiftModifier);

  if (C_S_RUNNING == client_state()) {
    if (is_shift) {
      switch (event->key()) {
      case Qt::Key_Return:
      case Qt::Key_Enter:
        key_end_turn();
        return;
      default:
        break;
      }
    }

    switch (event->key()) {
    case Qt::Key_Up:
    case Qt::Key_8:
      key_unit_move(DIR8_NORTH);
      return;
    case Qt::Key_Left:
    case Qt::Key_4:
      key_unit_move(DIR8_WEST);
      return;
    case Qt::Key_Right:
    case Qt::Key_6:
      key_unit_move(DIR8_EAST);
      return;
    case Qt::Key_Down:
    case Qt::Key_2:
      key_unit_move(DIR8_SOUTH);
      return;
    case Qt::Key_PageUp:
    case Qt::Key_9:
      key_unit_move(DIR8_NORTHEAST);
      return;
    case Qt::Key_PageDown:
    case Qt::Key_3:
      key_unit_move(DIR8_SOUTHEAST);
      return;
    case Qt::Key_Home:
    case Qt::Key_7:
      key_unit_move(DIR8_NORTHWEST);
      return;
    case Qt::Key_End:
    case Qt::Key_1:
      key_unit_move(DIR8_SOUTHWEST);
      return;
    case Qt::Key_5:
    case Qt::Key_Clear:
      key_recall_previous_focus_unit();
      return;
    case Qt::Key_Escape:
      key_cancel_action();
      if (gui()->infotab->chat_maximized == true) {
        gui()->infotab->restore_chat();
      }
      return;
    case Qt::Key_Enter:
    case Qt::Key_Return:
      if (gui()->infotab->chat_maximized == false) {
        gui()->infotab->maximize_chat();
        gui()->infotab->chtwdg->chat_line->setFocus();
      }
      return;
    default:
      break;
    }
    shortcut_pressed(event->key());
  }
}

/**************************************************************************
  Pressed mouse or keyboard
**************************************************************************/
void map_view::shortcut_pressed(int key)
{
  Qt::MouseButtons bt;
  QPoint pos;
  fc_shortcut *sc;
  Qt::KeyboardModifiers md;
  production_widget *pw;
  struct tile *ptile = nullptr;
  struct city *pcity = nullptr;

  md = QApplication::keyboardModifiers();
  bt = QApplication::mouseButtons();
  pos = mapFromGlobal(QCursor::pos());

  ptile = canvas_pos_to_tile(pos.x(), pos.y());
  pcity = ptile ? tile_city(ptile) : nullptr;

  if (pcity && pcity->owner != client_player()) {
    pcity = nullptr;
  }

  /* Trade Generator - skip */
  if (bt == Qt::LeftButton
      && gui()->trade_gen.hover_city == true) {
    ptile = canvas_pos_to_tile(pos.x(), pos.y());
    gui()->menu_bar->set_tile_for_order(ptile);
    gui()->trade_gen.hover_city = false;
    gui()->trade_gen.add_tile(ptile);
    gui()->mapview_wdg->repaint();
    return;
  }

  /* Rally point - select city - skip */
  if (bt == Qt::LeftButton
      && gui()->rallies.hover_city == true) {
    char text[1024];
    ptile = canvas_pos_to_tile(pos.x(), pos.y());
    if (tile_city(ptile)) {
      gui()->rallies.hover_city = false;
      gui()->rallies.hover_tile = true;
      gui()->rallies.rally_city = tile_city(ptile);

      if (gui()->rallies.clear(tile_city(ptile))) {
        fc_snprintf(text, sizeof(text),
                    _("Rally point cleared for city %s"),
                    city_link(tile_city(ptile)));
        output_window_append(ftc_client, text);
        gui()->rallies.hover_tile = false;
        return;
      }
      fc_snprintf(text, sizeof(text),
                  _("Selected city %s. Now choose rally point."),
                  city_link(tile_city(ptile)));
      output_window_append(ftc_client, text);
    } else {
      output_window_append(ftc_client, _("No city selected. Aborted"));
    }
    return;
  }

  /* Rally point - select tile  - skip */
  if (bt == Qt::LeftButton && gui()->rallies.hover_tile == true) {
    char text[1024];
    qfc_rally *rally = new qfc_rally;
    rally->ptile = canvas_pos_to_tile(pos.x(), pos.y());
    rally->pcity = gui()->rallies.rally_city;
    fc_snprintf(text, sizeof(text),
                _("Tile %s set as rally point from city %s."),
                tile_link(ptile), city_link(rally->pcity));
    gui()->rallies.hover_tile = false;
    gui()->rallies.add(rally);
    output_window_append(ftc_client, text);
    return;
  }

  if (bt == Qt::LeftButton && gui()->menu_bar->delayed_order == true) {
    ptile = canvas_pos_to_tile(pos.x(), pos.y());
    gui()->menu_bar->set_tile_for_order(ptile);
    set_hover_state(NULL, HOVER_NONE, ACTIVITY_LAST, NULL, ORDER_LAST);
    exit_goto_state();
    gui()->menu_bar->delayed_order = false;
    return;
  }
  if (bt == Qt::LeftButton  && gui()->infotab->chat_maximized == true) {
    gui()->infotab->restore_chat();
  }
  if (bt  == Qt::LeftButton && gui()->menu_bar->quick_airlifting == true) {
    ptile = canvas_pos_to_tile(pos.x(), pos.y());
    if (tile_city(ptile)) {
      multiairlift(tile_city(ptile), gui()->menu_bar->airlift_type_id);
    } else {
      output_window_append(ftc_client, "No city selected for airlift");
    }
    gui()->menu_bar->quick_airlifting = false;
    return;
  }
  /* Check configured shortcuts */
  if (gui()->menu_bar->delayed_order == false) {
    sc = fc_shortcuts::sc()->get_shortcut(SC_QUICK_SELECT);
    if (((key && key == sc->key) || bt == sc->mouse) && md == sc->mod
        && pcity != nullptr) {
      pw = new production_widget(this, pcity, false, 0, 0, true);
      pw->show();
      return;
    }

    sc = fc_shortcuts::sc()->get_shortcut(SC_SHOW_UNITS);
    if (((key && key == sc->key) || bt == sc->mouse) && md == sc->mod
        && ptile != nullptr && unit_list_size(ptile->units) > 0) {
      gui()->toggle_unit_sel_widget(ptile);
      return;
    }

    sc = fc_shortcuts::sc()->get_shortcut(SC_COPY_PROD);
    if (((key && key == sc->key) || bt == sc->mouse) && md == sc->mod
        && ptile != nullptr) {
      clipboard_copy_production(ptile);
      return;
    }

    sc = fc_shortcuts::sc()->get_shortcut(SC_PASTE_PROD);
    if (((key && key == sc->key) || bt == sc->mouse) && md == sc->mod
        && pcity != nullptr) {
      clipboard_paste_production(pcity);
      return;
    }

    sc = fc_shortcuts::sc()->get_shortcut(SC_HIDE_WORKERS);
    if (((key && key == sc->key) || bt == sc->mouse) && md == sc->mod) {
      key_city_overlay(pos.x(), pos.y());
      return;
    }
    sc = fc_shortcuts::sc()->get_shortcut(SC_MAKE_LINK);
    if (((key && key == sc->key) || bt == sc->mouse) && md == sc->mod
        && ptile != nullptr) {
      gui()->infotab->chtwdg->make_link(ptile);
      return;
    }


    sc = fc_shortcuts::sc()->get_shortcut(SC_QUICK_BUY);
    if (((key && key == sc->key) || bt == sc->mouse) && md == sc->mod
        && pcity != nullptr) {
      pw = new production_widget(this, pcity, false, 0, 0, true, true);
      pw->show();
      return;
    }
    sc = fc_shortcuts::sc()->get_shortcut(SC_APPEND_FOCUS);
    if (((key && key == sc->key) || bt == sc->mouse) && md == sc->mod) {
      action_button_pressed(pos.x(), pos.y(), SELECT_APPEND);
      return;
    }
    sc = fc_shortcuts::sc()->get_shortcut(SC_ADJUST_WORKERS);
    if (((key && key == sc->key) || bt == sc->mouse) && md == sc->mod) {
      adjust_workers_button_pressed(pos.x(), pos.y());
      return;
    }
  }

  sc = fc_shortcuts::sc()->get_shortcut(SC_SCROLL_MAP);
  if (((key && key == sc->key) || bt == sc->mouse) && md == sc->mod) {
    recenter_button_pressed(pos.x(), pos.y());
    return;
  }
  sc = fc_shortcuts::sc()->get_shortcut(SC_SELECT_BUTTON);
  if (((key && key == sc->key) || bt == sc->mouse) && md == sc->mod) {
    action_button_pressed(pos.x(), pos.y(), SELECT_POPUP);
    return;
  }

  sc = fc_shortcuts::sc()->get_shortcut(SC_POPUP_INFO);
  if (((key && key == sc->key) || bt == sc->mouse)
      && md == sc->mod && ptile != nullptr) {
    gui()->popup_tile_info(ptile);
    return;
  }

  sc = fc_shortcuts::sc()->get_shortcut(SC_WAKEUP_SENTRIES);
  if (((key && key == sc->key) || bt == sc->mouse) && md == sc->mod) {
    wakeup_button_pressed(pos.x(), pos.y());
    return;
  }
}

/**************************************************************************
  Releasad mouse buttons
**************************************************************************/
void map_view::shortcut_released(Qt::MouseButton bt)
{
  QPoint pos;
  fc_shortcut *sc;
  Qt::KeyboardModifiers md;
  md = QApplication::keyboardModifiers();
  pos = mapFromGlobal(QCursor::pos());

  sc = fc_shortcuts::sc()->get_shortcut(SC_POPUP_INFO);
  if (bt == sc->mouse && md == sc->mod) {
    gui()->popdown_tile_info();
    return;
  }

  sc = fc_shortcuts::sc()->get_shortcut(SC_SELECT_BUTTON);
  if (bt == sc->mouse && md == sc->mod) {
    release_goto_button(pos.x(), pos.y());
    return;
  }
}
/**************************************************************************
  Mouse buttons handler for map_view
**************************************************************************/
void map_view::mousePressEvent(QMouseEvent *event)
{
  shortcut_pressed(0);
}
/**************************************************************************
  Mouse release event for map_view
**************************************************************************/
void map_view::mouseReleaseEvent(QMouseEvent *event)
{
  shortcut_released(event->button());
}

/**************************************************************************
  Mouse movement handler for map_view
**************************************************************************/
void map_view::mouseMoveEvent(QMouseEvent *event)
{
  update_line(event->pos().x(), event->pos().y());
  if (keyboardless_goto_button_down && hover_state == HOVER_NONE) {
    maybe_activate_keyboardless_goto(event->pos().x(), event->pos().y());
  }
  control_mouse_cursor(canvas_pos_to_tile(event->pos().x(),
                                          event->pos().y()));
}

/**************************************************************************
  Popups information label tile
**************************************************************************/
void fc_client::popup_tile_info(struct tile *ptile)
{
  struct unit *punit = NULL;

  Q_ASSERT(info_tile_wdg == NULL);
  if (TILE_UNKNOWN != client_tile_get_known(ptile)) {
    mapdeco_set_crosshair(ptile, true);
    punit = find_visible_unit(ptile);
    if (punit) {
      mapdeco_set_gotoroute(punit);
      if (punit->goto_tile && unit_has_orders(punit)) {
        mapdeco_set_crosshair(punit->goto_tile, true);
      }
    }
    info_tile_wdg = new info_tile(ptile, mapview_wdg);
    info_tile_wdg->show();
  }
}

/**************************************************************************
  Popdowns information label tile
**************************************************************************/
void fc_client::popdown_tile_info()
{
  mapdeco_clear_crosshairs();
  mapdeco_clear_gotoroutes();
  if (info_tile_wdg != NULL) {
    info_tile_wdg->close();
    delete info_tile_wdg;
    info_tile_wdg = NULL;
  }
}

