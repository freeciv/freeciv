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
#include "clinet.h"
#include "cma_core.h"
#include "control.h"
#include "goto.h"
#include "mapctrl_g.h"
#include "mapview_g.h"
#include "options.h"

#include "mapctrl_common.h"

/* Update the workers for a city on the map, when the update is received */
struct city *city_workers_display = NULL;

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
  Wakeup sentried units on the tile of the specified location.  Usually
  this is done with SHIFT+left-click.
**************************************************************************/
void wakeup_button_pressed(int canvas_x, int canvas_y)
{
  int map_x, map_y;

  if (can_client_issue_orders()) {
    if (canvas_to_map_pos(&map_x, &map_y, canvas_x, canvas_y)) {
      wakeup_sentried_units(map_x, map_y);
    }
  }
}

/**************************************************************************
  Adjust the position of city workers from the mapview.  Usually this is
  done with SHIFT+left-click.
**************************************************************************/
void adjust_workers_button_pressed(int canvas_x, int canvas_y)
{
  int map_x, map_y, city_x, city_y;
  struct city *pcity;
  struct packet_city_request packet;
  enum city_tile_type worker;

  if (can_client_issue_orders()) {
    if (canvas_to_map_pos(&map_x, &map_y, canvas_x, canvas_y)) {
      pcity = find_city_near_tile(map_x, map_y);
      if (pcity && !cma_is_city_under_agent(pcity, NULL)) {
	if (!map_to_city_map(&city_x, &city_y, pcity, map_x, map_y)) {
	  assert(0);
	}

	packet.city_id = pcity->id;
	packet.worker_x = city_x;
	packet.worker_y = city_y;

	worker = get_worker_city(pcity, city_x, city_y);
	if (worker == C_TILE_WORKER) {
	  send_packet_city_request(&aconnection, &packet,
				   PACKET_CITY_MAKE_SPECIALIST);
	} else if (worker == C_TILE_EMPTY) {
	  send_packet_city_request(&aconnection, &packet,
				   PACKET_CITY_MAKE_WORKER);
	} else {
	  /* If worker == C_TILE_UNAVAILABLE then we can't use this tile.  No
	   * packet is sent and city_workers_display is not updated. */
	  return;
	}
	
	/* When the city info packet is received, update the workers on the
	 * map.  This is a bad hack used to selectively update the mapview
	 * when we receive the corresponding city packet. */
	city_workers_display = pcity;
      }
    }
  }
}

/**************************************************************************
  Recenter the map on the canvas location, on user request.  Usually this
  is done with a right-click.
**************************************************************************/
void recenter_button_pressed(int canvas_x, int canvas_y)
{
  int map_x, map_y;

  if (can_client_change_view()) {
    if (!canvas_to_map_pos(&map_x, &map_y, canvas_x, canvas_y)) {
      nearest_real_pos(&map_x, &map_y);
    }
    center_tile_mapcanvas(map_x, map_y);
  }
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
