/********************************************************************** 
 Freeciv - Copyright (C) 2002 - The Freeciv Poject
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
#include <config.h>
#endif

#include "log.h"

#include "agents.h"
#include "civclient.h"
#include "control.h"
#include "goto.h"
#include "mapctrl_g.h"
#include "mapview_g.h"
#include "options.h"

#include "mapctrl_common.h"

static bool turn_done_state;
static bool is_turn_done_state_valid = FALSE;

/**************************************************************************
 Return TRUE iff the turn done button is enabled.
**************************************************************************/
bool get_turn_done_button_state()
{
  if (!is_turn_done_state_valid) {
    update_turn_done_button_state();
  }
  assert(is_turn_done_state_valid);

  return turn_done_state;
}

/**************************************************************************
  Scroll the mapview half a screen in the given direction.  This is a GUI
  direction; i.e., DIR8_NORTH is "up" on the mapview.
**************************************************************************/
void scroll_mapview(enum direction8 gui_dir)
{
  int map_x, map_y, canvas_x, canvas_y;

  if (!can_client_change_view()) {
    return;
  }

  canvas_x = mapview_canvas.width / 2;
  canvas_y = mapview_canvas.height / 2;
  canvas_x += DIR_DX[gui_dir] * mapview_canvas.width / 2;
  canvas_y += DIR_DY[gui_dir] * mapview_canvas.height / 2;
  if (!canvas_to_map_pos(&map_x, &map_y, canvas_x, canvas_y)) {
    nearest_real_pos(&map_x, &map_y);
  }
  center_tile_mapcanvas(map_x, map_y);
}

/**************************************************************************
 Update the turn done button state.
**************************************************************************/
void update_turn_done_button_state()
{
  bool new_state;

  if (!is_turn_done_state_valid) {
    turn_done_state = FALSE;
    is_turn_done_state_valid = TRUE;
    set_turn_done_button_state(turn_done_state);
    freelog(LOG_DEBUG, "setting turn done button state init %d",
	    turn_done_state);
  }

  new_state = (can_client_issue_orders()
	       && !game.player_ptr->turn_done && !agents_busy()
	       && !turn_done_sent);
  if (new_state == turn_done_state) {
    return;
  }

  freelog(LOG_DEBUG, "setting turn done button state from %d to %d",
	  turn_done_state, new_state);
  turn_done_state = new_state;

  set_turn_done_button_state(turn_done_state);

  if (turn_done_state) {
    if (waiting_for_end_turn
	|| (game.player_ptr->ai.control && !ai_manual_turn_done)) {
      send_turn_done();
    } else {
      update_turn_done_button(TRUE);
    }
  }
}

/**************************************************************************
  Update the goto/patrol line to the given map canvas location.
**************************************************************************/
void update_line(int canvas_x, int canvas_y)
{
  if ((hover_state == HOVER_GOTO || hover_state == HOVER_PATROL)
      && draw_goto_line) {
    int x, y, old_x, old_y;

    if (!canvas_to_map_pos(&x, &y, canvas_x, canvas_y)) {
      nearest_real_pos(&x, &y);
    }

    get_line_dest(&old_x, &old_y);
    if (!same_pos(old_x, old_y, x, y)) {
      draw_line(x, y);
    }
  }
}
