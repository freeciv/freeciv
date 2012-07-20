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

// std
#include <stdlib.h>

// Qt
#include <QKeyEvent>
#include <QPushButton>
#include <QMouseEvent>

// client
#include "mapctrl.h"
#include "client_main.h"
#include "unit.h"
#include "control.h"
#include "tile.h"

// gui-qt
#include "fc_client.h"
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
  int index=tile_index(unit_tile(punit));

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
  if (state) {
    gui()->game_info_label->end_turn_button->setEnabled(true);
  } else {
    gui()->game_info_label->end_turn_button->setDisabled(true);
  }
}

/**************************************************************************
  Draw a goto or patrol line at the current mouse position.
**************************************************************************/
void create_line_at_mouse_pos(void)
{
  /* PORTME */
}

/**************************************************************************
 The Area Selection rectangle. Called by center_tile_mapcanvas() and
 when the mouse pointer moves.
**************************************************************************/
void update_rect_at_mouse_pos(void)
{
  /* PORTME */
}

/**************************************************************************
  Keyboard handler for map_view
**************************************************************************/
void map_view::keyPressEvent(QKeyEvent * event)
{
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
      key_recall_previous_focus_unit();
      return;
    case Qt::Key_Escape:
      key_cancel_action();
      return;
    default:
      break;
    }
  }
}

/**************************************************************************
 * Mouse Handler for map_view
 *************************************************************************/
void map_view::mousePressEvent(QMouseEvent *event)
{
  struct tile* ptile;
  struct city *pcity;
  int i;

  if (event->button() == Qt::RightButton) {
    recenter_button_pressed(event->x(), event->y());
  }

  i = 0;

  /* Left Button */
  if (event->button() == Qt::LeftButton) {
    ptile = canvas_pos_to_tile(event->x(), event->y());
    /* check if over city */
    if(((pcity = tile_city(ptile)) != NULL)
       && (city_owner(pcity) == client.conn.playing)) {
    qtg_real_city_dialog_popup(pcity);
    return;
    }
    /* check if clicked unit */
    unit_list_iterate(ptile->units, punit) {
      i++;

      if (i == 1) {
        unit_focus_set(punit);
      }
    }
    unit_list_iterate_end;
  }
}
