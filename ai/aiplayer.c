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

/* common */
#include "ai.h"
#include "city.h"
#include "unit.h"

/* ai */
#include "aidata.h"

#include "aiplayer.h"

static struct ai_type *self = NULL;

/**************************************************************************
  Set pointer to ai type of the default ai.
**************************************************************************/
void default_ai_set_self(struct ai_type *ai)
{
  self = ai;
}

/**************************************************************************
  Get pointer to ai type of the default ai.
**************************************************************************/
struct ai_type *default_ai_get_self(void)
{
  return self;
}

/**************************************************************************
  Initialize player for use with default AI. Note that this is called
  for all players, not just for those default AI is controlling.
**************************************************************************/
void ai_player_alloc(struct player *pplayer)
{
  struct ai_plr *player_data = fc_calloc(1, sizeof(struct ai_plr));

  player_set_ai_data(pplayer, default_ai_get_self(), player_data);

  ai_data_init(pplayer);
}

/**************************************************************************
  Free player from use with default AI.
**************************************************************************/
void ai_player_free(struct player *pplayer)
{
  struct ai_plr *player_data = def_ai_player_data(pplayer);

  ai_data_close(pplayer);

  if (player_data != NULL) {
    player_set_ai_data(pplayer, default_ai_get_self(), NULL);
    FC_FREE(player_data);
  }
}
