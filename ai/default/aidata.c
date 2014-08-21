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
#include "advdiplomacy.h"
#include "aiferry.h"
#include "aiplayer.h"
#include "aisettler.h"
#include "aiunit.h"

#include "aidata.h"

static void dai_diplomacy_new(struct ai_type *ait,
                              const struct player *plr1,
                              const struct player *plr2);
static void dai_diplomacy_defaults(struct ai_type *ait,
                                   const struct player *plr1,
                                   const struct player *plr2);
static void dai_diplomacy_destroy(struct ai_type *ait,
                                  const struct player *plr1,
                                  const struct player *plr2);

/****************************************************************************
  Initialize ai data structure
****************************************************************************/
void dai_data_init(struct ai_type *ait, struct player *pplayer)
{
  struct ai_plr *ai = def_ai_player_data(pplayer, ait);

  ai->phase_initialized = FALSE;

  ai->last_num_continents = -1;
  ai->last_num_oceans = -1;

  ai->diplomacy.player_intel_slots
    = fc_calloc(player_slot_count(),
                sizeof(*ai->diplomacy.player_intel_slots));
  player_slots_iterate(pslot) {
    const struct ai_dip_intel **player_intel_slot
      = ai->diplomacy.player_intel_slots + player_slot_index(pslot);
    *player_intel_slot = NULL;
  } player_slots_iterate_end;

  players_iterate(aplayer) {
    /* create ai diplomacy states for all other players */
    dai_diplomacy_new(ait, pplayer, aplayer);
    dai_diplomacy_defaults(ait, pplayer, aplayer);
    /* create ai diplomacy state of this player */
    if (aplayer != pplayer) {
      dai_diplomacy_new(ait, aplayer, pplayer);
      dai_diplomacy_defaults(ait, aplayer, pplayer);
    }
  } players_iterate_end;

  ai->diplomacy.strategy = WIN_OPEN;
  ai->diplomacy.timer = 0;
  ai->diplomacy.love_coeff = 4; /* 4% */
  ai->diplomacy.love_incr = MAX_AI_LOVE * 3 / 100;
  ai->diplomacy.req_love_for_peace = MAX_AI_LOVE / 8;
  ai->diplomacy.req_love_for_alliance = MAX_AI_LOVE / 4;

  ai->settler = NULL;

  /* Initialise autosettler. */
  dai_auto_settler_init(ai);
}

/****************************************************************************
  Deinitialize ai data structure
****************************************************************************/
void dai_data_close(struct ai_type *ait, struct player *pplayer)
{
  struct ai_plr *ai = def_ai_player_data(pplayer, ait);

  /* Free autosettler. */
  dai_auto_settler_free(ai);

  if (ai->diplomacy.player_intel_slots != NULL) {
    players_iterate(aplayer) {
      /* destroy the ai diplomacy states of this player with others ... */
      dai_diplomacy_destroy(ait, pplayer, aplayer);
      /* and of others with this player. */
      if (aplayer != pplayer) {
        dai_diplomacy_destroy(ait, aplayer, pplayer);
      }
    } players_iterate_end;
    free(ai->diplomacy.player_intel_slots);
  }
}

/**************************************************************************
  Return whether data phase is currently open. Data phase is open
  between dai_data_phase_begin() and dai_data_phase_finished() calls.
**************************************************************************/
bool is_ai_data_phase_open(struct ai_type *ait, struct player *pplayer)
{
  struct ai_plr *ai = def_ai_player_data(pplayer, ait);

  return ai->phase_initialized;
}

/****************************************************************************
  Make and cache lots of calculations needed for other functions.
****************************************************************************/
void dai_data_phase_begin(struct ai_type *ait, struct player *pplayer,
                          bool is_new_phase)
{
  struct ai_plr *ai = def_ai_player_data(pplayer, ait);
  bool close;

  /* Note that this refreshes advisor data if needed. ai_plr_data_get()
     is expected to refresh advisor data if needed, and ai_plr_data_get()
     depends on this call
     ai_plr_data_get()->ai_data_phase_begin()->adv_data_get() to do it.
     If you change this, you may need to adjust ai_plr_data_get() also. */
  struct adv_data *adv;

  if (ai->phase_initialized) {
    return;
  }

  ai->phase_initialized = TRUE;

  adv = adv_data_get(pplayer, &close);

  /* Store current number of known continents and oceans so we can compare
     against it later in order to see if ai data needs refreshing. */
  ai->last_num_continents = adv->num_continents;
  ai->last_num_oceans = adv->num_oceans;

  /*** Diplomacy ***/
  if (pplayer->ai_controlled && !is_barbarian(pplayer) && is_new_phase) {
    dai_diplomacy_begin_new_phase(ait, pplayer);
  }

  /* Set per-player variables. We must set all players, since players
   * can be created during a turn, and we don't want those to have
   * invalid values. */
  players_iterate(aplayer) {
    struct ai_dip_intel *adip = dai_diplomacy_get(ait, pplayer, aplayer);

    adip->is_allied_with_enemy = NULL;
    adip->at_war_with_ally = NULL;
    adip->is_allied_with_ally = NULL;

    players_iterate(check_pl) {
      if (check_pl == pplayer
          || check_pl == aplayer
          || !check_pl->is_alive) {
        continue;
      }
      if (pplayers_allied(aplayer, check_pl)
          && player_diplstate_get(pplayer, check_pl)->type == DS_WAR) {
       adip->is_allied_with_enemy = check_pl;
      }
      if (pplayers_allied(pplayer, check_pl)
          && player_diplstate_get(aplayer, check_pl)->type == DS_WAR) {
        adip->at_war_with_ally = check_pl;
      }
      if (pplayers_allied(aplayer, check_pl)
          && pplayers_allied(pplayer, check_pl)) {
        adip->is_allied_with_ally = check_pl;
      }
    } players_iterate_end;
  } players_iterate_end;

  /*** Statistics ***/

  ai->stats.workers = fc_calloc(adv->num_continents + 1, sizeof(int));
  unit_list_iterate(pplayer->units, punit) {
    struct tile *ptile = unit_tile(punit);

    if (!is_ocean_tile(ptile) && unit_has_type_flag(punit, UTYF_SETTLERS)) {
      ai->stats.workers[(int)tile_continent(unit_tile(punit))]++;
    }
  } unit_list_iterate_end;

  BV_CLR_ALL(ai->stats.diplomat_reservations);
  unit_list_iterate(pplayer->units, punit) {
    if (is_actor_unit(punit)
        && def_ai_unit_data(punit, ait)->task == AIUNIT_ATTACK) {
      /* Heading somewhere on a mission, reserve target. */
      struct city *pcity = tile_city(punit->goto_tile);

      if (pcity) {
        BV_SET(ai->stats.diplomat_reservations, pcity->id);
      }
    }
  } unit_list_iterate_end;

  aiferry_init_stats(ait, pplayer);

  /*** Interception engine ***/

  /* We are tracking a unit if punit->server.ai->cur_pos is not NULL. If we
   * are not tracking, start tracking by setting cur_pos. If we are,
   * fill prev_pos with previous cur_pos. This way we get the 
   * necessary coordinates to calculate a probable trajectory. */
  players_iterate_alive(aplayer) {
    if (aplayer == pplayer) {
      continue;
    }
    unit_list_iterate(aplayer->units, punit) {
      struct unit_ai *unit_data = def_ai_unit_data(punit, ait);

      if (!unit_data->cur_pos) {
        /* Start tracking */
        unit_data->cur_pos = &unit_data->cur_struct;
        unit_data->prev_pos = NULL;
      } else {
        unit_data->prev_struct = unit_data->cur_struct;
        unit_data->prev_pos = &unit_data->prev_struct;
      }
      *unit_data->cur_pos = unit_tile(punit);
    } unit_list_iterate_end;
  } players_iterate_alive_end;

  if (close) {
    adv_data_phase_done(pplayer);
  }
}

/****************************************************************************
  Clean up ai data after phase finished.
****************************************************************************/
void dai_data_phase_finished(struct ai_type *ait, struct player *pplayer)
{
  struct ai_plr *ai = def_ai_player_data(pplayer, ait);

  if (!ai->phase_initialized) {
    return;
  }

  free(ai->stats.workers);
  ai->stats.workers = NULL;

  ai->phase_initialized = FALSE;
}

/****************************************************************************
  Get current default ai data related to player.
  If close is set, data phase will be opened even if it's currently closed,
  and the boolean will be set accordingly to tell caller that phase needs
  closing.
****************************************************************************/
struct ai_plr *dai_plr_data_get(struct ai_type *ait, struct player *pplayer,
                                bool *close)
{
  struct ai_plr *ai = def_ai_player_data(pplayer, ait);

  fc_assert_ret_val(ai != NULL, NULL);

  /* This assert really is required. See longer comment
     in adv_data_get() for equivalent code. */
#if defined(DEBUG) || defined(IS_DEVEL_VERSION)
  fc_assert(close != NULL || ai->phase_initialized);
#endif

  if (close != NULL) {
    *close = FALSE;
  }

  if (ai->last_num_continents != map.num_continents
      || ai->last_num_oceans != map.num_oceans) {
    /* We have discovered more continents, recalculate! */

    /* See adv_data_get() */
    if (ai->phase_initialized) {
      dai_data_phase_finished(ait, pplayer);
      dai_data_phase_begin(ait, pplayer, FALSE);
    } else {
      /* wrong order */
      log_debug("%s ai data phase closed when dai_plr_data_get() called",
                player_name(pplayer));
      dai_data_phase_begin(ait, pplayer, FALSE);
      if (close != NULL) {
        *close = TRUE;
      } else {
        dai_data_phase_finished(ait, pplayer);
      }
    }
  } else {
    if (!ai->phase_initialized && close != NULL) {
      dai_data_phase_begin(ait, pplayer, FALSE);
      *close = TRUE;
    }
  }

  return ai;
}

/****************************************************************************
  Allocate new ai diplomacy slot
****************************************************************************/
static void dai_diplomacy_new(struct ai_type *ait,
                              const struct player *plr1,
                              const struct player *plr2)
{
  struct ai_dip_intel *player_intel;

  fc_assert_ret(plr1 != NULL);
  fc_assert_ret(plr2 != NULL);

  const struct ai_dip_intel **player_intel_slot
    = def_ai_player_data(plr1, ait)->diplomacy.player_intel_slots
      + player_index(plr2);

  fc_assert_ret(*player_intel_slot == NULL);

  player_intel = fc_calloc(1, sizeof(*player_intel));
  *player_intel_slot = player_intel;
}

/****************************************************************************
  Set diplomacy data between two players to its default values.
****************************************************************************/
static void dai_diplomacy_defaults(struct ai_type *ait,
                                   const struct player *plr1,
                                   const struct player *plr2)
{
  struct ai_dip_intel *player_intel = dai_diplomacy_get(ait, plr1, plr2);

  fc_assert_ret(player_intel != NULL);

  /* pseudorandom value */
  player_intel->spam = (player_index(plr1) + player_index(plr2)) % 5;
  player_intel->countdown = -1;
  player_intel->war_reason = WAR_REASON_NONE;
  player_intel->distance = 1;
  player_intel->ally_patience = 0;
  player_intel->asked_about_peace = 0;
  player_intel->asked_about_alliance = 0;
  player_intel->asked_about_ceasefire = 0;
  player_intel->warned_about_space = 0;
}

/***************************************************************
  Returns diplomatic state type between two players
***************************************************************/
struct ai_dip_intel *dai_diplomacy_get(struct ai_type *ait,
                                       const struct player *plr1,
                                       const struct player *plr2)
{
  fc_assert_ret_val(plr1 != NULL, NULL);
  fc_assert_ret_val(plr2 != NULL, NULL);

  const struct ai_dip_intel **player_intel_slot
    = def_ai_player_data(plr1, ait)->diplomacy.player_intel_slots
      + player_index(plr2);

  fc_assert_ret_val(player_intel_slot != NULL, NULL);

  return (struct ai_dip_intel *) *player_intel_slot;
}

/****************************************************************************
  Free resources allocated for diplomacy information between two players.
****************************************************************************/
static void dai_diplomacy_destroy(struct ai_type *ait,
                                  const struct player *plr1,
                                  const struct player *plr2)
{
  fc_assert_ret(plr1 != NULL);
  fc_assert_ret(plr2 != NULL);

  const struct ai_dip_intel **player_intel_slot
    = def_ai_player_data(plr1, ait)->diplomacy.player_intel_slots
      + player_index(plr2);

  if (*player_intel_slot != NULL) {
    free(dai_diplomacy_get(ait, plr1, plr2));
  }

  *player_intel_slot = NULL;
}
