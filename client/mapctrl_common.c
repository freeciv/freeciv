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

#include "agents.h"
#include "civclient.h"
#include "log.h"
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

  new_state = (get_client_state() == CLIENT_GAME_RUNNING_STATE
	       && !game.player_ptr->turn_done && !agents_busy()
	       && (!game.player_ptr->ai.control || ai_manual_turn_done)
	       && !client_is_observer()
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
