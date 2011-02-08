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
#include <fc_config.h>
#endif

/* common */
#include "game.h"
#include "map.h"

/* server/advisors */
#include "advdata.h"

/* ai */
#include "aiferry.h"
#include "aiplayer.h"

#include "aidata.h"

/****************************************************************************
  Initialize ai data structure
****************************************************************************/
void ai_data_init(struct player *pplayer)
{
  struct ai_plr *ai = def_ai_player_data(pplayer);

  ai->phase_initialized = FALSE;

  ai->last_num_continents = -1;
  ai->last_num_oceans = -1;

  ai->channels = NULL;
}

/****************************************************************************
  Deinitialize ai data structure
****************************************************************************/
void ai_data_close(struct player *pplayer)
{
}

/**************************************************************************
  Return whether data phase is currently open. Data phase is open
  between ai_data_phase_begin() and ai_data_phase_finished() calls.
**************************************************************************/
bool is_ai_data_phase_open(struct player *pplayer)
{
  struct ai_plr *ai = def_ai_player_data(pplayer);

  return ai->phase_initialized;
}

/****************************************************************************
  Make and cache lots of calculations needed for other functions.
****************************************************************************/
void ai_data_phase_begin(struct player *pplayer, bool is_new_phase)
{
  struct ai_plr *ai = def_ai_player_data(pplayer);

  /* Note that this refreshes advisor data if needed. ai_plr_data_get()
     is expected to refresh advisor data if needed, and ai_plr_data_get()
     depends on this call
     ai_plr_data_get()->ai_data_phase_begin()->adv_data_get() to do it.
     If you change this, you may need to adjust ai_plr_data_get() also. */
  struct adv_data *adv = adv_data_get(pplayer);
  int i;

  if (ai->phase_initialized) {
    return;
  }

  ai->phase_initialized = TRUE;

  /* Store current number of known continents and oceans so we can compare
     against it later in order to see if ai data needs refreshing. */
  ai->last_num_continents = adv->num_continents;
  ai->last_num_oceans = adv->num_oceans;

  /*** Channels ***/

  /* Ways to cross from one ocean to another through a city. */
  ai->channels = fc_calloc((adv->num_oceans + 1) * (adv->num_oceans + 1), sizeof(int));
  players_iterate(aplayer) {
    if (pplayers_allied(pplayer, aplayer)) {
      city_list_iterate(aplayer->cities, pcity) {
        adjc_iterate(pcity->tile, tile1) {
          if (is_ocean_tile(tile1)) {
            adjc_iterate(pcity->tile, tile2) {
              if (is_ocean_tile(tile2) 
                  && tile_continent(tile1) != tile_continent(tile2)) {
                ai->channels[(-tile_continent(tile1)) * adv->num_oceans
                             + (-tile_continent(tile2))] = TRUE;
                ai->channels[(-tile_continent(tile2)) * adv->num_oceans
                             + (-tile_continent(tile1))] = TRUE;
              }
            } adjc_iterate_end;
          }
        } adjc_iterate_end;
      } city_list_iterate_end;
    }
  } players_iterate_end;

  /* If we can go i -> j and j -> k, we can also go i -> k. */
  for(i = 1; i <= adv->num_oceans; i++) {
    int j;

    for(j = 1; j <= adv->num_oceans; j++) {
      if (ai->channels[i * adv->num_oceans + j]) {
        int k;

        for(k = 1; k <= adv->num_oceans; k++) {
          ai->channels[i * adv->num_oceans + k] |= 
            ai->channels[j * adv->num_oceans + k];
        }
      }
    }
  }

  if (game.server.debug[DEBUG_FERRIES]) {
    for(i = 1; i <= adv->num_oceans; i++) {
      int j;

      for(j = 1; j <= adv->num_oceans; j++) {
        if (ai->channels[i * adv->num_oceans + j]) {
          log_test("%s: oceans %d and %d are connected",
                   player_name(pplayer), i, j);
       }
      }
    }
  }

  aiferry_init_stats(pplayer);
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

  free(ai->channels);
  ai->channels = NULL;

  ai->phase_initialized = FALSE;
}

/****************************************************************************
  Get current default ai data related to player
****************************************************************************/
struct ai_plr *ai_plr_data_get(struct player *pplayer)
{
  struct ai_plr *ai = def_ai_player_data(pplayer);

  fc_assert_ret_val(ai != NULL, NULL);

  /* This assert really is required. See longer comment
     in adv_data_get() for equivalent code. */
  fc_assert(ai->phase_initialized);

  if (ai->last_num_continents != map.num_continents
      || ai->last_num_oceans != map.num_oceans) {
    /* We have discovered more continents, recalculate! */
    ai_data_phase_finished(pplayer);
    ai_data_phase_begin(pplayer, FALSE);
  }

  return ai;
}

/**************************************************************************
  Is there a channel going from ocean c1 to ocean c2?
  Returns FALSE if either is not an ocean.
**************************************************************************/
bool ai_channel(struct player *pplayer, Continent_id c1, Continent_id c2)
{
  struct ai_plr *ai = ai_plr_data_get(pplayer);
  struct adv_data *adv = adv_data_get(pplayer);

  if (c1 >= 0 || c2 >= 0) {
    return FALSE;
  }

  return (c1 == c2 || ai->channels[(-c1) * adv->num_oceans + (-c2)]);
}
