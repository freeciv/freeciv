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

/* utility */
#include "log.h"
#include "mem.h"

/* common */
#include "city.h"
#include "effects.h"
#include "game.h"
#include "government.h"
#include "map.h"
#include "movement.h"
#include "research.h"
#include "unit.h"
#include "unitlist.h"

/* common/aicore */
#include "aisupport.h"
#include "path_finding.h"
#include "pf_tools.h"

/* server */
#include "citytools.h"
#include "cityturn.h"
#include "diplhand.h"
#include "maphand.h"
#include "plrhand.h"
#include "srv_log.h"
#include "unittools.h"

/* server/advisors */
#include "advcity.h"
#include "advtools.h"
#include "autosettlers.h"

#include "advdata.h"

static void adv_dipl_new(const struct player *plr1,
                         const struct player *plr2);
static void adv_dipl_free(const struct player *plr1,
                          const struct player *plr2);
static struct adv_dipl *adv_dipl_get(const struct player *plr1,
                                     const struct player *plr2);

/**************************************************************************
  Precalculates some important data about the improvements in the game
  that we use later in ai/aicity.c.  We mark improvements as 'calculate'
  if we want to run a full test on them, as 'estimate' if we just want
  to do some guesses on them, or as 'unused' is they are useless to us.
  Then we find the largest range of calculatable effects in the
  improvement and record it for later use.
**************************************************************************/
static void adv_data_city_impr_calc(struct player *pplayer,
				    struct adv_data *adv)
{
  int count[ADV_IMPR_LAST];

  memset(count, 0, sizeof(count));

  improvement_iterate(pimprove) {
    struct universal source = {
      .kind = VUT_IMPROVEMENT,
      .value = {.building = pimprove}
    };

    adv->impr_calc[improvement_index(pimprove)] = ADV_IMPR_ESTIMATE;

    /* Find largest extension */
    effect_list_iterate(get_req_source_effects(&source), peffect) {
      switch (peffect->type) {
#if 0
      /* TODO */
      case EFT_FORCE_CONTENT:
      case EFT_FORCE_CONTENT_PCT:
      case EFT_MAKE_CONTENT:
      case EFT_MAKE_CONTENT_MIL:
      case EFT_MAKE_CONTENT_MIL_PER:
      case EFT_MAKE_CONTENT_PCT:
      case EFT_MAKE_HAPPY:
#endif
      case EFT_CAPITAL_CITY:
      case EFT_POLLU_POP_PCT:
      case EFT_POLLU_PROD_PCT:
      case EFT_OUTPUT_BONUS:
      case EFT_OUTPUT_BONUS_2:
      case EFT_OUTPUT_WASTE_PCT:
      case EFT_UPKEEP_FREE:
	requirement_list_iterate(peffect->reqs, preq) {
	  if (VUT_IMPROVEMENT == preq->source.kind
	      && preq->source.value.building == pimprove) {
            if (adv->impr_calc[improvement_index(pimprove)] != ADV_IMPR_CALCULATE_FULL) {
	      adv->impr_calc[improvement_index(pimprove)] = ADV_IMPR_CALCULATE;
            }
	    if (preq->range > adv->impr_range[improvement_index(pimprove)]) {
	      adv->impr_range[improvement_index(pimprove)] = preq->range;
	    }
	  }
	} requirement_list_iterate_end;
        break;
      case EFT_OUTPUT_ADD_TILE:
      case EFT_OUTPUT_PER_TILE:
      case EFT_OUTPUT_INC_TILE:
	requirement_list_iterate(peffect->reqs, preq) {
	  if (VUT_IMPROVEMENT == preq->source.kind
	      && preq->source.value.building == pimprove) {
	    adv->impr_calc[improvement_index(pimprove)] = ADV_IMPR_CALCULATE_FULL;
	    if (preq->range > adv->impr_range[improvement_index(pimprove)]) {
	      adv->impr_range[improvement_index(pimprove)] = preq->range;
	    }
	  }
	} requirement_list_iterate_end;
      break;
      default:
      /* Nothing! */
      break;
      }
    } effect_list_iterate_end;
  } improvement_iterate_end;
}

/**************************************************************************
  Check if the player still takes advantage of EFT_TECH_PARASITE.
  Research is useless if there are still techs which may be given to the
  player for free.
**************************************************************************/
static bool player_has_really_useful_tech_parasite(struct player* pplayer)
{
  int players_needed = get_player_bonus(pplayer, EFT_TECH_PARASITE);

  if (players_needed == 0) {
    return FALSE;
  }
  
  advance_index_iterate(A_FIRST, tech) {
    int players_having;

    if (!player_invention_reachable(pplayer, tech, FALSE)
        || TECH_KNOWN == player_invention_state(pplayer, tech)) {
      continue;
    }

    players_having = 0;

    players_iterate_alive(aplayer) {
      if (aplayer == pplayer) {
        continue;
      }

      if (TECH_KNOWN == player_invention_state(aplayer, tech)
          || player_research_get(aplayer)->researching == tech) {
	players_having++;
	if (players_having >= players_needed) {
	  return TRUE;
	}
      }
    } players_iterate_alive_end;
  } advance_index_iterate_end;
  return FALSE;
}

/**************************************************************************
  Analyze rulesets. Must be run after rulesets are loaded, unlike
  _init, which must be run before savegames are loaded, which is usually
  before rulesets.
**************************************************************************/
void adv_data_analyze_rulesets(struct player *pplayer)
{
  struct adv_data *adv = pplayer->server.adv;

  fc_assert_ret(adv != NULL);

  adv_data_city_impr_calc(pplayer, adv);
}

/**************************************************************************
  This function is called each turn to initialize pplayer->ai.stats.units.
**************************************************************************/
static void count_my_units(struct player *pplayer)
{
  struct adv_data *adv = adv_data_get(pplayer);

  memset(&adv->stats.units, 0, sizeof(adv->stats.units));

  unit_list_iterate(pplayer->units, punit) {
    struct unit_class *pclass = unit_class(punit);

    if (pclass->adv.land_move != MOVE_NONE
        && pclass->adv.sea_move != MOVE_NONE) {
      /* Can move both land and ocean */
      adv->stats.units.amphibious++;
    } else if (pclass->adv.land_move != MOVE_NONE) {
      /* Can move only at land */
      adv->stats.units.land++;
    } else if (pclass->adv.sea_move != MOVE_NONE) {
      /* Can move only at sea */
      adv->stats.units.sea++;
    }

    if (unit_has_type_flag(punit, F_TRIREME)) {
      adv->stats.units.triremes++;
    }
    if (uclass_has_flag(unit_class(punit), UCF_MISSILE)) {
      adv->stats.units.missiles++;
    }
    if (unit_has_type_flag(punit, F_PARATROOPERS)) {
      adv->stats.units.paratroopers++;
    }
    if (can_upgrade_unittype(pplayer, unit_type(punit)) >= 0) {
      adv->stats.units.upgradeable++;
    }
  } unit_list_iterate_end;
}

/**************************************************************************
  Return whether data phase is currently open. Data phase is open
  between adv_data_phase_init() and adv_data_phase_done() calls.
**************************************************************************/
bool is_adv_data_phase_open(struct player *pplayer)
{
  struct adv_data *adv = pplayer->server.adv;

  return adv->phase_is_initialized;
}

/**************************************************************************
  Make and cache lots of calculations needed for other functions.

  Returns TRUE if new data was created, FALSE if data existed already.

  Note: We use map.num_continents here rather than pplayer->num_continents
  because we are omniscient and don't care about such trivialities as who
  can see what.

  FIXME: We should try to find the lowest common defence strength of our
  defending units, and ignore enemy units that are incapable of harming 
  us, instead of just checking attack strength > 1.
**************************************************************************/
bool adv_data_phase_init(struct player *pplayer, bool is_new_phase)
{
  struct adv_data *adv = pplayer->server.adv;
  int i;
  int nuke_units;
  bool danger_of_nukes;

  fc_assert_ret_val(adv != NULL, FALSE);

  if (adv->phase_is_initialized) {
    return FALSE;
  }
  adv->phase_is_initialized = TRUE;

  TIMING_LOG(AIT_AIDATA, TIMER_START);

  nuke_units = num_role_units(F_NUCLEAR);
  danger_of_nukes = FALSE;

  /*** Threats ***/

  adv->num_continents    = map.num_continents;
  adv->num_oceans        = map.num_oceans;
  adv->threats.continent = fc_calloc(adv->num_continents + 1, sizeof(bool));
  adv->threats.invasions = FALSE;
  adv->threats.nuclear   = 0; /* none */
  adv->threats.ocean     = fc_calloc(adv->num_oceans + 1, sizeof(bool));
  adv->threats.igwall    = FALSE;

  players_iterate(aplayer) {
    if (!adv_is_player_dangerous(pplayer, aplayer)) {
      continue;
    }

    /* The idea is that if there aren't any hostile cities on
     * our continent, the danger of land attacks is not big
     * enough to warrant city walls. Concentrate instead on 
     * coastal fortresses and hunting down enemy transports. */
    city_list_iterate(aplayer->cities, acity) {
      Continent_id continent = tile_continent(acity->tile);
      if (continent >= 0) {
        adv->threats.continent[continent] = TRUE;
      }
    } city_list_iterate_end;

    unit_list_iterate(aplayer->units, punit) {
      if (unit_has_type_flag(punit, F_IGWALL)) {
        adv->threats.igwall = TRUE;
      }

      if (is_sailing_unit(punit)) {
        /* If the enemy has not started sailing yet, or we have total
         * control over the seas, don't worry, keep attacking. */
        if (get_transporter_capacity(punit) > 0) {
          unit_class_iterate(punitclass) {
            if (punitclass->move_type == UMT_LAND
                && can_unit_type_transport(unit_type(punit), punitclass)) {
              /* Enemy can transport some land units! */
              adv->threats.invasions = TRUE;
              break;
            }
          } unit_class_iterate_end;
        }

        /* The idea is that while our enemies don't have any offensive
         * seaborne units, we don't have to worry. Go on the offensive! */
        if (unit_type(punit)->attack_strength > 1) {
	  if (is_ocean_tile(unit_tile(punit))) {
	    Continent_id continent = tile_continent(unit_tile(punit));
	    adv->threats.ocean[-continent] = TRUE;
	  } else {
	    adjc_iterate(unit_tile(punit), tile2) {
	      if (is_ocean_tile(tile2)) {
	        Continent_id continent = tile_continent(tile2);
	        adv->threats.ocean[-continent] = TRUE;
	      }
	    } adjc_iterate_end;
	  }
        } 
        continue;
      }

      /* If our enemy builds missiles, worry about missile defence. */
      if (uclass_has_flag(unit_class(punit), UCF_MISSILE)
          && unit_type(punit)->attack_strength > 1) {
        adv->threats.missile = TRUE;
      }

      /* If he builds nukes, worry a lot. */
      if (unit_has_type_flag(punit, F_NUCLEAR)) {
        danger_of_nukes = TRUE;
      }
    } unit_list_iterate_end;

    /* Check for nuke capability */
    for (i = 0; i < nuke_units; i++) {
      struct unit_type *nuke = get_role_unit(F_NUCLEAR, i);

      if (can_player_build_unit_direct(aplayer, nuke)) { 
        adv->threats.nuclear = 1;
      }
    }
  } players_iterate_end;

  /* Increase from fear to terror if opponent actually has nukes */
  if (danger_of_nukes) {
    adv->threats.nuclear++; /* sum of both fears */
  }

  /*** Exploration ***/

  adv->explore.land_done = TRUE;
  adv->explore.sea_done = TRUE;
  adv->explore.continent = fc_calloc(adv->num_continents + 1, sizeof(bool));
  adv->explore.ocean = fc_calloc(adv->num_oceans + 1, sizeof(bool));
  whole_map_iterate(ptile) {
    Continent_id continent = tile_continent(ptile);

    if (is_ocean_tile(ptile)) {
      if (adv->explore.sea_done && ai_handicap(pplayer, H_TARGETS) 
          && !map_is_known(ptile, pplayer)) {
	/* We're not done there. */
        adv->explore.sea_done = FALSE;
        adv->explore.ocean[-continent] = TRUE;
      }
      /* skip rest, which is land only */
      continue;
    }
    if (adv->explore.continent[tile_continent(ptile)]) {
      /* we don't need more explaining, we got the point */
      continue;
    }
    if (tile_has_special(ptile, S_HUT) 
        && (!ai_handicap(pplayer, H_HUTS)
             || map_is_known(ptile, pplayer))) {
      adv->explore.land_done = FALSE;
      adv->explore.continent[continent] = TRUE;
      continue;
    }
    if (ai_handicap(pplayer, H_TARGETS) && !map_is_known(ptile, pplayer)) {
      /* this AI must explore */
      adv->explore.land_done = FALSE;
      adv->explore.continent[continent] = TRUE;
    }
  } whole_map_iterate_end;

  /*** Statistics ***/

  adv->stats.workers = fc_calloc(adv->num_continents + 1, sizeof(int));
  adv->stats.cities = fc_calloc(adv->num_continents + 1, sizeof(int));
  adv->stats.average_production = 0;
  city_list_iterate(pplayer->cities, pcity) {
    Continent_id continent = tile_continent(pcity->tile);
    if (continent >= 0) {
      adv->stats.cities[continent]++;
    }
    adv->stats.average_production += pcity->surplus[O_SHIELD];
  } city_list_iterate_end;
  adv->stats.average_production /= MAX(1, city_list_size(pplayer->cities));
  unit_list_iterate(pplayer->units, punit) {
    struct tile *ptile = unit_tile(punit);

    if (!is_ocean_tile(ptile) && unit_has_type_flag(punit, F_SETTLERS)) {
      adv->stats.workers[(int)tile_continent(unit_tile(punit))]++;
    }
  } unit_list_iterate_end;

  /*** Diplomacy ***/

  players_iterate(aplayer) {
    struct adv_dipl *dip = adv_dipl_get(pplayer, aplayer);

    dip->allied_with_enemy = FALSE;
    players_iterate(check_pl) {
      if (pplayers_allied(aplayer, check_pl)
          && player_diplstate_get(pplayer, check_pl)->type == DS_WAR) {
        dip->allied_with_enemy = TRUE;
      }
    } players_iterate_end;
  } players_iterate_end;

  adv->dipl.spacerace_leader = player_leading_spacerace();

  adv->dipl.production_leader = NULL;
  players_iterate(aplayer) {
    if (adv->dipl.production_leader == NULL
        || adv->dipl.production_leader->score.mfg < aplayer->score.mfg) {
      adv->dipl.production_leader = aplayer;
    }
  } players_iterate_end;

  /*** Priorities ***/

  /* NEVER set these to zero! Weight values are usually multiplied by 
   * these values, so be careful with them. They are used in city 
   * and government calculations, and food and shields should be 
   * slightly bigger because we only look at surpluses there. They
   * are all WAGs. */
  adv->food_priority = FOOD_WEIGHTING;
  adv->shield_priority = SHIELD_WEIGHTING;
  if (adv_wants_science(pplayer)) {
    adv->luxury_priority = 1;
    adv->science_priority = TRADE_WEIGHTING;
  } else {
    adv->luxury_priority = TRADE_WEIGHTING;
    adv->science_priority = 1;
  }
  adv->gold_priority = TRADE_WEIGHTING;
  adv->happy_priority = 1;
  adv->unhappy_priority = TRADE_WEIGHTING; /* danger */
  adv->angry_priority = TRADE_WEIGHTING * 3; /* grave danger */
  adv->pollution_priority = POLLUTION_WEIGHTING;

  /* Research want */
  if (is_future_tech(player_research_get(pplayer)->researching)
      || player_has_really_useful_tech_parasite(pplayer)) {
    adv->wants_science = FALSE;
  } else {
    adv->wants_science = TRUE;
  }
  
  /* max num cities
   * The idea behind this code is that novice players don't understand that
   * expansion is critical and find it very annoying.
   * With the following code AI players will try to be only a bit better 
   * than the best human players. This should lead to more exciting games
   * for the beginners.
   */
  if (ai_handicap(pplayer, H_EXPANSION)) {
    bool found_human = FALSE;
    adv->max_num_cities = 3;
    players_iterate_alive(aplayer) {
      if (aplayer == pplayer || aplayer->ai_controlled) {
        continue;
      }
      adv->max_num_cities = MAX(adv->max_num_cities,
				city_list_size(aplayer->cities) + 3);
      found_human = TRUE;
    } players_iterate_alive_end;
    if (!found_human) {
      adv->max_num_cities = MAP_INDEX_SIZE;
    }
  } else {
    adv->max_num_cities = MAP_INDEX_SIZE;
  }

  count_my_units(pplayer);

  TIMING_LOG(AIT_AIDATA, TIMER_STOP);

  /* Government */
  TIMING_LOG(AIT_GOVERNMENT, TIMER_START);
  adv_best_government(pplayer);
  TIMING_LOG(AIT_GOVERNMENT, TIMER_STOP);

  return TRUE;
}

/**************************************************************************
  Clean up our mess.
**************************************************************************/
void adv_data_phase_done(struct player *pplayer)
{
  struct adv_data *adv = pplayer->server.adv;

  fc_assert_ret(adv != NULL);

  if (!adv->phase_is_initialized) {
    return;
  }

  free(adv->explore.ocean);
  adv->explore.ocean = NULL;

  free(adv->explore.continent);
  adv->explore.continent = NULL;

  free(adv->threats.continent);
  adv->threats.continent = NULL;

  free(adv->threats.ocean);
  adv->threats.ocean = NULL;

  free(adv->stats.workers);
  adv->stats.workers = NULL;

  free(adv->stats.cities);
  adv->stats.cities = NULL;

  adv->num_continents = 0;
  adv->num_oceans     = 0;

  adv->phase_is_initialized = FALSE;
}

/**************************************************************************
  Return a pointer to our data
**************************************************************************/
struct adv_data *adv_data_get(struct player *pplayer)
{
  struct adv_data *adv = pplayer->server.adv;

  fc_assert_ret_val(adv != NULL, NULL);

  /* It's certainly indication of bug causing problems
     if this adv_data_get() gets called between adv_data_phase_done() and
     adv_data_phase_init(), since we may end up calling those
     functions if number of known continents has changed.

     Consider following case:
       Correct call order would be:
       a) adv_data_phase_init()
       b)   adv_data_get() -> adv_data_phase_done()
       c)   adv_data_get() -> adv_data_phase_init()
       d) adv_data_phase_done()
       e) do something
       f) adv_data_phase_init()

       In (e) data phase would be closed and data would be
       correctly initialized at (f), which is probably beginning
       next turn.

       Buggy version where adv_data_get() (b&c) gets called after (d):
       a) adv_data_phase_init()
       d) adv_data_phase_done()
       b)   adv_data_get() -> adv_data_phase_done()
       c)   adv_data_get() -> adv_data_phase_init()
       e) do something
       f) adv_data_phase_init()

       Now in (e) data phase would be open. When adv_data_phase_init()
       then finally gets called and it really should recreate data
       to match situation of new turn, it detects that data phase
       is already initialized and does nothing.

       So, this assertion is here for a reason!

       Code below tries to fix the situation best it can if such a bug is
       encountered. Since we are probably going to trust that to be enough
       instead of making intrusive fixes for actual bug in stable branch,
       do not assert for non-debug builds of stable versions. */
#if defined(DEBUG) || defined(IS_DEVEL_VERSION)
  fc_assert(adv->phase_is_initialized);
#endif

  if (adv->num_continents != map.num_continents
      || adv->num_oceans != map.num_oceans) {
    /* we discovered more continents, recalculate! */

    if (adv->phase_is_initialized) {
      /* KLUDGE for buggy situations. Only call these in this order if
         inside data phase.
         This is blanket "fix" for all cases where adv_data_get() is called
         at illegal time. This at least minimize bad effects of such calls. */
      adv_data_phase_done(pplayer);
      adv_data_phase_init(pplayer, FALSE);
    } else {
      /* Call them in "wrong" order so we return recalculated data to caller,
         but leave data phase closed. */
      log_debug("%s advisor data phase closed when adv_data_get() called",
                player_name(pplayer));
      adv_data_phase_init(pplayer, FALSE);
      adv_data_phase_done(pplayer);
    }
  }

  return adv;
}

/**************************************************************************
  Allocate memory for advisor data. Save to call multiple times.
**************************************************************************/
void adv_data_init(struct player *pplayer)
{
  struct adv_data *adv;

  if (pplayer->server.adv == NULL) {
    pplayer->server.adv = fc_calloc(1, sizeof(*pplayer->server.adv));
  }
  adv = pplayer->server.adv;

  adv->government_want = fc_calloc(government_count() + 1,
                                   sizeof(*adv->government_want));

  adv->dipl.adv_dipl_slots = fc_calloc(player_slot_count(),
                                       sizeof(*adv->dipl.adv_dipl_slots));
  player_slots_iterate(pslot) {
    struct adv_dipl **dip_slot =
      adv->dipl.adv_dipl_slots + player_slot_index(pslot);
    *dip_slot = NULL;
  } player_slots_iterate_end;

  players_iterate(aplayer) {
    adv_dipl_new(pplayer, aplayer);
    if (aplayer != pplayer) {
      adv_dipl_new(aplayer, pplayer);
    }
  } players_iterate_end;

  adv_data_default(pplayer);
}

/**************************************************************************
  Initialize with sane values.
**************************************************************************/
void adv_data_default(struct player *pplayer)
{
  struct adv_data *adv = pplayer->server.adv;

  fc_assert_ret(adv != NULL);

  adv->govt_reeval = 0;
  memset(adv->government_want, 0,
         (government_count() + 1) * sizeof(*adv->government_want));

  adv->wonder_city = 0;

  adv->wants_science = TRUE;
  adv->celebrate = FALSE;
  adv->max_num_cities = 10000;
}

/**************************************************************************
  Free memory for advisor data.
**************************************************************************/
void adv_data_close(struct player *pplayer)
{
  struct adv_data *adv = pplayer->server.adv;

  fc_assert_ret(NULL != adv);

  adv_data_phase_done(pplayer);

  if (adv->government_want != NULL) {
    free(adv->government_want);
  }

  if (adv->dipl.adv_dipl_slots != NULL) {
    players_iterate(aplayer) {
      adv_dipl_free(pplayer, aplayer);
      if (aplayer != pplayer) {
        adv_dipl_free(aplayer, pplayer);
      }
    } players_iterate_end;
    FC_FREE(adv->dipl.adv_dipl_slots);
  }

  if (adv != NULL) {
    free(adv);
  }

  pplayer->server.adv = NULL;
}

/****************************************************************************
  Allocate new advisor diplomacy slot
****************************************************************************/
static void adv_dipl_new(const struct player *plr1,
                         const struct player *plr2)
{
  struct adv_dipl **dip_slot =
    plr1->server.adv->dipl.adv_dipl_slots + player_index(plr2);

  *dip_slot = fc_calloc(1, sizeof(struct adv_dipl));
}

/****************************************************************************
  Free resources allocated for diplomacy information between two players.
****************************************************************************/
static void adv_dipl_free(const struct player *plr1,
                          const struct player *plr2)
{
  struct adv_dipl **dip_slot =
    plr1->server.adv->dipl.adv_dipl_slots + player_index(plr2);

  if (*dip_slot != NULL) {
    FC_FREE(*dip_slot);
    *dip_slot = NULL;
  }
}

/**************************************************************************
  Returns diplomatic state type between two players
**************************************************************************/
static struct adv_dipl *adv_dipl_get(const struct player *plr1,
                                     const struct player *plr2)
{
  struct adv_dipl **dip_slot =
    plr1->server.adv->dipl.adv_dipl_slots + player_index(plr2);

  return *dip_slot;
}

/**************************************************************************
  Find best government to aim for.
  We do it by setting our government to all possible values and calculating
  our GDP (total ai_eval_calc_city) under this government.  If the very
  best of the governments is not available to us (it is not yet discovered),
  we record it in the goal.gov structure with the aim of wanting the
  necessary tech more.  The best of the available governments is recorded 
  in goal.revolution.  We record the want of each government, and only
  recalculate this data every ai->govt_reeval_turns turns.

  Note: Call this _before_ doing taxes!
**************************************************************************/
void adv_best_government(struct player *pplayer)
{
  struct adv_data *adv = adv_data_get(pplayer);
  int best_val = 0;
  int bonus = 0; /* in percentage */
  struct government *current_gov = government_of_player(pplayer);

  adv->goal.govt.gov = current_gov;
  adv->goal.govt.val = 0;
  adv->goal.govt.req = A_UNSET;
  adv->goal.revolution = current_gov;

  if (ai_handicap(pplayer, H_AWAY) || !pplayer->is_alive) {
    return;
  }

  if (adv->govt_reeval == 0) {
    governments_iterate(gov) {
      int val = 0;
      int dist;

      if (gov == game.government_during_revolution) {
        continue; /* pointless */
      }
      if (gov->ai.better
          && can_change_to_government(pplayer, gov->ai.better)) {
        continue; /* we have better governments available */
      }
      pplayer->government = gov;
      /* Ideally we should change tax rates here, but since
       * this is a rather big CPU operation, we'd rather not. */
      check_player_max_rates(pplayer);
      city_list_iterate(pplayer->cities, acity) {
        auto_arrange_workers(acity);
      } city_list_iterate_end;
      city_list_iterate(pplayer->cities, pcity) {
        val += adv_eval_calc_city(pcity, adv);
      } city_list_iterate_end;

      /* Bonuses for non-economic abilities. We increase val by
       * a very small amount here to choose govt in cases where
       * we have no cities yet. */
      bonus += get_player_bonus(pplayer, EFT_VETERAN_BUILD) > 0 ? 3 : 0;
      bonus -= get_player_bonus(pplayer, EFT_REVOLUTION_WHEN_UNHAPPY) > 0 ? 3 : 0;
      bonus += get_player_bonus(pplayer, EFT_NO_INCITE) > 0 ? 4 : 0;
      bonus += get_player_bonus(pplayer, EFT_UNBRIBABLE_UNITS) > 0 ? 2 : 0;
      bonus += get_player_bonus(pplayer, EFT_INSPIRE_PARTISANS) > 0 ? 3 : 0;
      bonus += get_player_bonus(pplayer, EFT_RAPTURE_GROW) > 0 ? 2 : 0;
      bonus += get_player_bonus(pplayer, EFT_FANATICS) > 0 ? 3 : 0;
      bonus += get_player_bonus(pplayer, EFT_OUTPUT_INC_TILE) * 8;

      val += (val * bonus) / 100;

      /* FIXME: handle reqs other than technologies. */
      dist = 0;
      requirement_vector_iterate(&gov->reqs, preq) {
	if (VUT_ADVANCE == preq->source.kind) {
	  dist += MAX(1, num_unknown_techs_for_goal(pplayer,
						    advance_number(preq->source.value.advance)));
	}
      } requirement_vector_iterate_end;
      val = amortize(val, dist);
      adv->government_want[government_index(gov)] = val; /* Save want */
    } governments_iterate_end;
    /* Now reset our gov to it's real state. */
    pplayer->government = current_gov;
    city_list_iterate(pplayer->cities, acity) {
      auto_arrange_workers(acity);
    } city_list_iterate_end;
    if (player_is_cpuhog(pplayer)) {
      adv->govt_reeval = 1;
    } else {
      adv->govt_reeval = CLIP(5, city_list_size(pplayer->cities), 20);
    }
  }
  adv->govt_reeval--;

  /* Figure out which government is the best for us this turn. */
  governments_iterate(gov) {
    int gi = government_index(gov);
    if (adv->government_want[gi] > best_val 
        && can_change_to_government(pplayer, gov)) {
      best_val = adv->government_want[gi];
      adv->goal.revolution = gov;
    }
    if (adv->government_want[gi] > adv->goal.govt.val) {
      adv->goal.govt.gov = gov;
      adv->goal.govt.val = adv->government_want[gi];

      /* FIXME: handle reqs other than technologies. */
      adv->goal.govt.req = A_NONE;
      requirement_vector_iterate(&gov->reqs, preq) {
	if (VUT_ADVANCE == preq->source.kind) {
	  adv->goal.govt.req = advance_number(preq->source.value.advance);
	  break;
	}
      } requirement_vector_iterate_end;
    }
  } governments_iterate_end;
  /* Goodness of the ideal gov is calculated relative to the goodness of the
   * best of the available ones. */
  adv->goal.govt.val -= best_val;
}

/**************************************************************************
  Return whether science would help us at all.
**************************************************************************/
bool adv_wants_science(struct player *pplayer)
{
  return adv_data_get(pplayer)->wants_science;
}


/**********************************************************************
  There are some signs that a player might be dangerous: We are at 
  war with him, he has done lots of ignoble things to us, he is an 
  ally of one of our enemies (a ticking bomb to be sure), we don't like him,
  diplomatic state is neutral or we have cease fire.
***********************************************************************/
bool adv_is_player_dangerous(struct player *pplayer,
                             struct player *aplayer)
{
  struct adv_dipl *dip;
  enum diplstate_type ds;
  enum danger_consideration dang = DANG_UNDECIDED;

  if (pplayer->ai_controlled) {
    /* Give AI code possibility to decide itself */
    CALL_PLR_AI_FUNC(consider_plr_dangerous, pplayer, pplayer, aplayer, &dang);
  }

  if (dang == DANG_NOT) {
    return FALSE;
  }

  if (dang == DANG_YES) {
    return TRUE;;
  }

  if (pplayer == aplayer) {
    /* We always trust ourself */
    return FALSE;
  }
  
  ds = player_diplstate_get(pplayer, aplayer)->type;
  
  if (ds == DS_WAR || ds == DS_CEASEFIRE) {
    /* It's already a war or aplayer can declare it soon */
    return TRUE;
  }

  dip = adv_dipl_get(pplayer, aplayer);

  if (dip->allied_with_enemy) {
    /* Don't trust someone who will declare war on us soon */
    return TRUE;
  }

  if (player_diplstate_get(pplayer, aplayer)->has_reason_to_cancel > 0) {
    return TRUE;
  }

  if (pplayer->ai_common.love[player_index(aplayer)] < MAX_AI_LOVE / 10) {
    /* We don't trust players who we don't like. Note that 
     * aplayer's units inside pplayer's borders decreases AI's love */
    return TRUE;
  }
  
  return FALSE;
}
