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

/* common */
#include "fc_types.h"
#include "player.h"

/* server */

#include "mood.h"

/**************************************************************************
  What is the player mood?
**************************************************************************/
enum mood_type player_mood(struct player *pplayer)
{
  players_iterate(other) {
    struct player_diplstate *state;

    state = player_diplstate_get(pplayer, other);

    if (state->type == DS_WAR) {
      return MOOD_COMBAT;
    }
  } players_iterate_end;

  return MOOD_PEACEFUL;
}
