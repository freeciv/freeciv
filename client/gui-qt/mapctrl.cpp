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
#include <QInputDialog>
#include <QKeyEvent>
#include <QPushButton>
#include <QMouseEvent>

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
#include "qtg_cxxside.h"



/**************************************************************************
  Popup a dialog to ask for the name of a new city.  The given string
  should be used as a suggestion.
**************************************************************************/
void popup_newcity_dialog(struct unit *punit, const char *suggestname)
{
  bool ok;
  QString text = QInputDialog::getText(gui()->central_wdg,
                                       _("Build New City"),
                                       _("What should we call our new city?"),
                                       QLineEdit::Normal,
                                       QString::fromUtf8(suggestname), &ok);
  int index = tile_index(unit_tile(punit));

  if (!ok) {
    cancel_city(index_to_tile(index));
  } else {
    finish_city(index_to_tile(index), text.toLocal8Bit().data());
  }

  return;
}

/**************************************************************************
  A turn done button should be provided for the player.  This function
  is called to toggle it between active/inactive.
**************************************************************************/
void set_turn_done_button_state(bool state)
{
  gui()->end_turn_rect->set_turn_button_enabled(state);
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
  struct tile *ptile;
  QPoint local_pos;
  Qt::KeyboardModifiers key_mod = QApplication::keyboardModifiers();
  bool is_shift = key_mod.testFlag(Qt::ShiftModifier);
  bool is_ctrl = key_mod.testFlag(Qt::ControlModifier);

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
      return;
    case Qt::Key_Space:
      if (is_ctrl) {
        local_pos = gui()->mapview_wdg->mapFromGlobal(QCursor::pos());
        ptile = canvas_pos_to_tile(local_pos.x(), local_pos.y());
        if (ptile != nullptr && unit_list_size(ptile->units) > 0) {
          gui()->toggle_unit_sel_widget(ptile);
        }
      }
      return;
    default:
      break;
    }
  }
}

/**************************************************************************
  Mouse buttons handler for map_view
**************************************************************************/
void map_view::mousePressEvent(QMouseEvent *event)
{
  struct tile *ptile = nullptr;
  struct city *pcity = nullptr;
  bool alt;
  bool ctrl;
  bool shft;
  QPoint pos;
  production_widget *pw;

  alt = false;
  ctrl = false;
  shft = false;

  if (event->modifiers() & Qt::AltModifier) {
    alt = true;
  }
  if (event->modifiers() & Qt::ControlModifier) {
    ctrl = true;
  }
  if (event->modifiers() & Qt::ShiftModifier) {
    shft = true;
  }
  pos = gui()->mapview_wdg->mapFromGlobal(QCursor::pos());
  ptile = canvas_pos_to_tile(pos.x(), pos.y());
  pcity = ptile ? tile_city(ptile) : NULL;

  /* Right Mouse Button pressed */
  if (event->button() == Qt::RightButton) {
    if (alt && ctrl && ptile) {
        gui()->infotab->chtwdg->make_link(ptile);
      /* <SHIFT + CONTROL> + RMB: Paste Production. */
    } else if (shft && ctrl && pcity != NULL) {
      clipboard_paste_production(pcity);
      /* <SHIFT> + RMB on city/unit: Copy Production. */
    } else if (shft && clipboard_copy_production(ptile)) {
      /* <CONTROL> + RMB : Quickselect a land unit. */
    } else if (ctrl) {
      action_button_pressed(event->pos().x(),event->pos().y(), SELECT_LAND);
      /* <SHIFT> + <ALT> + RMB : Show/hide workers. */
    } else if (shft && alt) {
      key_city_overlay(event->pos().x(), event->pos().y());
    } else {
      recenter_button_pressed(event->pos().x(), event->pos().y());
    }
  }

  if (pcity && pcity->owner != client_player()) {
    pcity = nullptr;
  }

  /* Left Button */
  if (event->button() == Qt::LeftButton) {
    if (ctrl && shft && pcity) {
      pw = new production_widget(this, pcity, false, 0, 0, true, true);
      pw->show();
    } else if (ctrl && pcity) {
      pw = new production_widget(this, pcity, false, 0, 0, true);
      pw->show();
      /* <SHIFT> + <CONTROL> + LMB : Adjust workers. */
    } else if (shft && ctrl) {
      adjust_workers_button_pressed(event->pos().x(), event->pos().y());
      /* <CONTROL> + LMB : Quickselect a sea unit. */
    } else if (ctrl) {
      action_button_pressed(event->pos().x(), event->pos().y(), SELECT_SEA);
      /* <SHIFT> + LMB: Append focus unit. */
    } else if (ptile && shft) {
      action_button_pressed(event->pos().x(), event->pos().y(),
                            SELECT_APPEND);
    } else {
      action_button_pressed(event->pos().x(), event->pos().y(), SELECT_POPUP);
    }
  }

  /* Middle Button */
  if (event->button() == Qt::MiddleButton) {
    /* <CONTROL> + MMB: Wake up sentries. */
    if (ctrl) {
      wakeup_button_pressed(event->pos().x(), event->pos().y());
    } else {
      gui()->popup_tile_info(ptile);
    }
  }

}
/**************************************************************************
  Mouse release event for map_view
**************************************************************************/
void map_view::mouseReleaseEvent(QMouseEvent *event)
{
  if (event->button() == Qt::MiddleButton) {
    gui()->popdown_tile_info();
  }
  if (event->button() == Qt::LeftButton) {
    release_goto_button(event->x(), event->y());
  }
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

