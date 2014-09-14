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

/* utility */
#include "shared.h"

/* common */
#include "player.h"

#include "handicaps.h"

/**************************************************************************
  Initialize handicaps for player
**************************************************************************/
void handicaps_init(struct player *pplayer)
{
  if (pplayer->ai_common.handicaps != NULL) {
    return;
  }

  pplayer->ai_common.handicaps = fc_malloc(sizeof(bv_handicap));
  BV_CLR_ALL(*((bv_handicap *)pplayer->ai_common.handicaps));
}

/**************************************************************************
  Free resources associated with player handicaps.
**************************************************************************/
void handicaps_close(struct player *pplayer)
{
  if (pplayer->ai_common.handicaps == NULL) {
    return;
  }

  free(pplayer->ai_common.handicaps);
  pplayer->ai_common.handicaps = NULL;
}

/**************************************************************************
  Set player handicaps
**************************************************************************/
void handicaps_set(struct player *pplayer, bv_handicap handicaps)
{
  *((bv_handicap *)pplayer->ai_common.handicaps) = handicaps;
}

/**************************************************************************
  AI players may have handicaps - allowing them to cheat or preventing
  them from using certain algorithms.  This function returns whether the
  player has the given handicap.  Human players are assumed to have no
  handicaps.
**************************************************************************/
bool has_handicap(const struct player *pplayer, enum handicap_type htype)
{
  if (!pplayer->ai_controlled) {
    return TRUE;
  }
  return BV_ISSET(*((bv_handicap *)pplayer->ai_common.handicaps), htype);
}
