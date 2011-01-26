/********************************************************************** 
 Freeciv - Copyright (C) 2002 - The Freeciv Project
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

/* ai */
#include "aiplayer.h"

#include "aidata.h"

/****************************************************************************
  Initialize ai data structure
****************************************************************************/
void ai_data_init(struct player *pplayer)
{
  struct ai_plr *ai = def_ai_player_data(pplayer);

  ai->phase_initialized = FALSE;
}

/****************************************************************************
  Deinitialize ai data structure
****************************************************************************/
void ai_data_close(struct player *pplayer)
{
}

/****************************************************************************
  Make and cache lots of calculations needed for other functions.
****************************************************************************/
void ai_data_phase_begin(struct player *pplayer, bool is_new_phase)
{
  struct ai_plr *ai = def_ai_player_data(pplayer);

  if (ai->phase_initialized) {
    return;
  }

  ai->phase_initialized = TRUE;
}

/****************************************************************************
  Clean up ai data after phase finished.
****************************************************************************/
void ai_data_phase_finished(struct player *pplayer)
{
  struct ai_plr *ai = def_ai_player_data(pplayer);

  if (!ai->phase_initialized) {
    return;
  }

  ai->phase_initialized = FALSE;
}

/**************************************************************************
  Return a pointer to our data
**************************************************************************/
struct ai_plr *ai_plr_data_get(struct player *pplayer)
{
  struct ai_plr *data = def_ai_player_data(pplayer);

  fc_assert_ret_val(data != NULL, NULL);

  return data;
}
