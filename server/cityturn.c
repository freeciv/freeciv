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
#include <config.h>
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "rand.h"
#include "shared.h"
#include "support.h"

#include "city.h"
#include "events.h"
#include "game.h"
#include "government.h"
#include "map.h"
#include "player.h"
#include "specialist.h"
#include "tech.h"
#include "unit.h"
#include "unitlist.h"

#include "script.h"

#include "citytools.h"
#include "cityturn.h"
#include "maphand.h"
#include "plrhand.h"
#include "sanitycheck.h"
#include "settlers.h"
#include "spacerace.h"
#include "srv_main.h"
#include "techtools.h"
#include "unittools.h"
#include "unithand.h"

#include "cm.h"

#include "advdomestic.h"
#include "aicity.h"
#include "aidata.h"
#include "ailog.h"
#include "aitools.h"


/* Queue for pending city_refresh() */
static struct city_list *city_refresh_queue = NULL;


static void check_pollution(struct city *pcity);
static void city_populate(struct city *pcity);

static bool worklist_change_build_target(struct player *pplayer,
					 struct city *pcity);

static bool city_distribute_surplus_shields(struct player *pplayer,
					    struct city *pcity);
static bool city_build_building(struct player *pplayer, struct city *pcity);
static bool city_build_unit(struct player *pplayer, struct city *pcity);
static bool city_build_stuff(struct player *pplayer, struct city *pcity);
static struct impr_type *building_upgrades_to(struct city *pcity,
					      struct impr_type *pimprove);
static void upgrade_building_prod(struct city *pcity);
static struct unit_type *unit_upgrades_to(struct city *pcity,
					  struct unit_type *id);
static void upgrade_unit_prod(struct city *pcity);
static void pay_for_buildings(struct player *pplayer, struct city *pcity);

static bool disband_city(struct city *pcity);

static void define_orig_production_values(struct city *pcity);
static void update_city_activity(struct player *pplayer, struct city *pcity);
static void nullify_caravan_and_disband_plus(struct city *pcity);

/**************************************************************************
...
**************************************************************************/
void city_refresh(struct city *pcity)
{
  pcity->server.needs_refresh = FALSE;
  city_refresh_from_main_map(pcity, TRUE);

   /* AI would calculate this 1000 times otherwise; better to do it
      once -- Syela */
   pcity->ai.trade_want
     = TRADE_WEIGHTING - city_waste(pcity, O_TRADE, TRADE_WEIGHTING);
}

/**************************************************************************
...
called on government change or wonder completion or stuff like that -- Syela
**************************************************************************/
void city_refresh_for_player(struct player *pplayer)
{
  conn_list_do_buffer(pplayer->connections);
  city_list_iterate(pplayer->cities, pcity)
    city_refresh(pcity);
    send_city_info(pplayer, pcity);
  city_list_iterate_end;
  conn_list_do_unbuffer(pplayer->connections);
}

/****************************************************************************
  Queue pending city_refresh() for later.
****************************************************************************/
void city_refresh_queue_add(struct city *pcity)
{
  if (NULL == city_refresh_queue) {
    city_refresh_queue = city_list_new();
  } else if (city_list_find_id(city_refresh_queue, pcity->id)) {
    return;
  }

  city_list_prepend(city_refresh_queue, pcity);
  pcity->server.needs_refresh = TRUE;
}

/*************************************************************************
  Refresh the listed cities.
  Called after significant changes to borders, and arranging workers.
*************************************************************************/
void city_refresh_queue_processing(void)
{
  if (NULL == city_refresh_queue) {
    return;
  }

  city_list_iterate(city_refresh_queue, pcity) {
    if (pcity->server.needs_refresh) {
      city_refresh(pcity);
      send_city_info(city_owner(pcity), pcity);
    }
  } city_list_iterate_end;

  city_list_unlink_all(city_refresh_queue);
  city_list_free(city_refresh_queue);
  city_refresh_queue = NULL;
}

/**************************************************************************
...
**************************************************************************/
void remove_obsolete_buildings_city(struct city *pcity, bool refresh)
{
  struct player *pplayer = city_owner(pcity);
  bool sold = FALSE;

  city_built_iterate(pcity, pimprove) {
    if (improvement_obsolete(pplayer, pimprove)
     && can_city_sell_building(pcity, pimprove)) {
      do_sell_building(pplayer, pcity, pimprove);
      notify_player(pplayer, pcity->tile, E_IMP_SOLD, 
		       _("%s is selling %s (obsolete) for %d."),
		       city_name(pcity),
		       improvement_name_translation(pimprove), 
		       impr_sell_gold(pimprove));
      sold = TRUE;
    }
  } city_built_iterate_end;

  if (sold && refresh) {
    city_refresh(pcity);
    send_city_info(pplayer, pcity);
    send_player_info(pplayer, NULL); /* Send updated gold to all */
  }
}

/**************************************************************************
...
**************************************************************************/
void remove_obsolete_buildings(struct player *pplayer)
{
  city_list_iterate(pplayer->cities, pcity) {
    remove_obsolete_buildings_city(pcity, FALSE);
  } city_list_iterate_end;
}

/**************************************************************************
  Rearrange workers according to a cm_result struct.  The caller must make
  sure that the result is valid.
**************************************************************************/
void apply_cmresult_to_city(struct city *pcity,
			    const struct cm_result *const cmr)
{
  struct tile *pcenter = city_tile(pcity);

  /* Now apply results */
  city_tile_iterate_skip_free_cxy(pcenter, ptile, x, y) {
    struct city *pwork = tile_worked(ptile);

    if (cmr->worker_positions_used[x][y]) {
      if (NULL == pwork) {
        city_map_update_worker(pcity, ptile, x, y);
      } else {
        assert(pwork == pcity);
      }
    } else {
      if (pwork == pcity) {
        city_map_update_empty(pcity, ptile, x, y);
      }
    }
  } city_tile_iterate_skip_free_cxy_end;

  specialist_type_iterate(sp) {
    pcity->specialists[sp] = cmr->specialists[sp];
  } specialist_type_iterate_end;
}

/**************************************************************************
  Call sync_cities() to send the affected cities to the clients.
**************************************************************************/
void auto_arrange_workers(struct city *pcity)
{
  struct cm_parameter cmp;
  struct cm_result cmr;

  /* See comment in freeze_workers(): we can't rearrange while
   * workers are frozen (i.e. multiple updates need to be done). */
  if (pcity->server.workers_frozen > 0) {
    pcity->server.needs_arrange = TRUE;
    return;
  }
  TIMING_LOG(AIT_CITIZEN_ARRANGE, TIMER_START);

  /* Freeze the workers and make sure all the tiles around the city
   * are up to date.  Then thaw, but hackishly make sure that thaw
   * doesn't call us recursively, which would waste time. */
  city_freeze_workers(pcity);
  pcity->server.needs_arrange = FALSE;

  city_map_update_all(pcity);

  pcity->server.needs_arrange = FALSE;
  city_thaw_workers(pcity);

  /* Now start actually rearranging. */
  sanity_check_city(pcity);
  cm_clear_cache(pcity);

  cm_init_parameter(&cmp);
  cmp.require_happy = FALSE;
  cmp.allow_disorder = FALSE;
  cmp.allow_specialists = TRUE;

  /* We used to look at pplayer->ai.xxx_priority to determine the values
   * to be used here.  However that doesn't work at all because those values
   * are on a different scale.  Later the ai may wish to adjust its
   * priorities - this should be done via a separate set of variables. */
  if (pcity->size > 1) {
    if (pcity->size <= game.info.notradesize) {
      cmp.factor[O_FOOD] = 15;
    } else {
      cmp.factor[O_FOOD] = 10;
    }
  } else {
    /* Growing to size 2 is the highest priority. */
    cmp.factor[O_FOOD] = 20;
  }
  cmp.factor[O_SHIELD] = 5;
  cmp.factor[O_TRADE] = 0; /* Trade only provides gold/science. */
  cmp.factor[O_GOLD] = 2;
  cmp.factor[O_LUXURY] = 0; /* Luxury only influences happiness. */
  cmp.factor[O_SCIENCE] = 2;
  cmp.happy_factor = 0;

  cmp.minimal_surplus[O_FOOD] = 1;
  cmp.minimal_surplus[O_SHIELD] = 1;
  cmp.minimal_surplus[O_TRADE] = 0;
  cmp.minimal_surplus[O_GOLD] = -FC_INFINITY;
  cmp.minimal_surplus[O_LUXURY] = 0;
  cmp.minimal_surplus[O_SCIENCE] = 0;

  cm_query_result(pcity, &cmp, &cmr);

  if (!cmr.found_a_valid) {
    /* Drop surpluses and try again. */
    cmp.minimal_surplus[O_FOOD] = 0;
    cmp.minimal_surplus[O_SHIELD] = 0;
    cmp.minimal_surplus[O_GOLD] = -FC_INFINITY;
    cm_query_result(pcity, &cmp, &cmr);
  }
  if (!cmr.found_a_valid) {
    /* Emergency management.  Get _some_ result.  This doesn't use
     * cm_init_emergency_parameter so we can keep the factors from
     * above. */
    output_type_iterate(o) {
      cmp.minimal_surplus[o] = MIN(cmp.minimal_surplus[o],
				   MIN(pcity->surplus[o], 0));
    } output_type_iterate_end;
    cmp.require_happy = FALSE;
    cmp.allow_disorder = city_owner(pcity)->ai.control ? FALSE : TRUE;
    cm_query_result(pcity, &cmp, &cmr);
  }
  if (!cmr.found_a_valid) {
    /* Should never happen. */
    CITY_LOG(LOG_DEBUG, pcity, "emergency management");
    cm_init_emergency_parameter(&cmp);
    cm_query_result(pcity, &cmp, &cmr);
  }
  assert(cmr.found_a_valid);

  apply_cmresult_to_city(pcity, &cmr);

  sanity_check_city_all(pcity);

  city_refresh(pcity);
  TIMING_LOG(AIT_CITIZEN_ARRANGE, TIMER_STOP);
}

/**************************************************************************
Notices about cities that should be sent to all players.
**************************************************************************/
void send_global_city_turn_notifications(struct conn_list *dest)
{
  if (!dest) {
    dest = game.all_connections;
  }

  players_iterate(pplayer) {
    city_list_iterate(pplayer->cities, pcity) {
      struct impr_type *pimprove = pcity->production.value.building;

      /* can_player_build_improvement_now() checks whether wonder is build
	 elsewhere (or destroyed) */
      if (VUT_IMPROVEMENT == pcity->production.kind
          && is_great_wonder(pimprove)
	  && (city_production_turns_to_build(pcity, TRUE) <= 1)
	  && can_player_build_improvement_now(city_owner(pcity), pimprove)) {
	notify_conn(dest, pcity->tile,
		       E_WONDER_WILL_BE_BUILT,
		       _("Notice: Wonder %s in %s will be finished"
			 " next turn."), 
		       improvement_name_translation(pimprove),
		       city_name(pcity));
      }
    } city_list_iterate_end;
  } players_iterate_end;
}

/**************************************************************************
  Send turn notifications for specified city to specified connections.
  Neither dest nor pcity may be NULL.
**************************************************************************/
void send_city_turn_notifications(struct conn_list *dest, struct city *pcity)
{
  int turns_growth, turns_granary;
  bool can_grow;
  struct impr_type *pimprove = pcity->production.value.building;
 
  if (pcity->surplus[O_FOOD] > 0) {
    turns_growth = (city_granary_size(pcity->size) - pcity->food_stock - 1)
		   / pcity->surplus[O_FOOD];

    if (get_city_bonus(pcity, EFT_GROWTH_FOOD) == 0
	&& get_current_construction_bonus(pcity, EFT_GROWTH_FOOD,
                                          RPT_CERTAIN) > 0
	&& pcity->surplus[O_SHIELD] > 0) {
      /* From the check above, the surplus must always be positive. */
      turns_granary = (impr_build_shield_cost(pimprove)
		       - pcity->shield_stock) / pcity->surplus[O_SHIELD];
      /* if growth and granary completion occur simultaneously, granary
	 preserves food.  -AJS */
      if (turns_growth < 5 && turns_granary < 5
	  && turns_growth < turns_granary) {
	notify_conn(dest, pcity->tile,
			 E_CITY_GRAN_THROTTLE,
			 _("Suggest throttling growth in %s to use %s "
			   "(being built) more effectively."),
			city_name(pcity),
			improvement_name_translation(pimprove));
      }
    }

    can_grow = city_can_grow_to(pcity, pcity->size + 1);

    if ((turns_growth <= 0) && !city_celebrating(pcity) && can_grow) {
      notify_conn(dest, pcity->tile,
		       E_CITY_MAY_SOON_GROW,
		       _("%s may soon grow to size %i."),
		       city_name(pcity),
		       pcity->size + 1);
    }
  } else {
    if (pcity->food_stock + pcity->surplus[O_FOOD] <= 0
	&& pcity->surplus[O_FOOD] < 0) {
      notify_conn(dest, pcity->tile,
		     E_CITY_FAMINE_FEARED,
		     _("Warning: Famine feared in %s."),
		     city_name(pcity));
    }
  }
  
}

/**************************************************************************
...
**************************************************************************/
void update_city_activities(struct player *pplayer)
{
  int gold;
  gold=pplayer->economic.gold;
  pplayer->bulbs_last_turn = 0;
  city_list_iterate_safe(pplayer->cities, pcity)
     update_city_activity(pplayer, pcity);
  city_list_iterate_safe_end;
  pplayer->ai.prev_gold = gold;
  /* This test include the cost of the units because pay_for_units is called
   * in update_city_activity */
  if (gold - (gold - pplayer->economic.gold) * 3 < 0) {
    notify_player(pplayer, NULL, E_LOW_ON_FUNDS,
		     _("WARNING, we're LOW on FUNDS sire."));  
  }
    /* uncomment to unbalance the game, like in civ1 (CLG)
      if (pplayer->got_tech && pplayer->research->researched > 0)    
        pplayer->research->researched=0;
    */
}

/**************************************************************************
  Reduce the city specialists by some (positive) value.
  Return the amount of reduction.
**************************************************************************/
static int city_reduce_specialists(struct city *pcity, int change)
{
  int want = change;
  assert(0 < change);

  specialist_type_iterate(sp) {
    int fix = MIN(want, pcity->specialists[sp]);

    pcity->specialists[sp] -= fix;
    want -= fix;
  } specialist_type_iterate_end;

  return change - want;
}

/**************************************************************************
  Reduce the city workers by some (positive) value.
  Return the amount of reduction.
**************************************************************************/
static int city_reduce_workers(struct city *pcity, int change)
{
  struct tile *pcenter = city_tile(pcity);
  int want = change;

  assert(0 < change);

  city_tile_iterate_skip_free_cxy(pcenter, ptile, city_x, city_y) {
    if (0 < want
     && tile_worked(ptile) == pcity) {
      city_map_update_empty(pcity, ptile, city_x, city_y);
      want--;
    }
  } city_tile_iterate_skip_free_cxy_end;

  return change - want;
}

/**************************************************************************
  Reduce the city size.  Return TRUE if the city survives the population
  loss.
**************************************************************************/
bool city_reduce_size(struct city *pcity, int pop_loss,
                      struct player *destroyer)
{
  int loss_remain;
  int i;

  if (pop_loss == 0) {
    return TRUE;
  }

  if (pcity->size <= pop_loss) {

    script_signal_emit("city_destroyed", 3,
                       API_TYPE_CITY, pcity,
                       API_TYPE_PLAYER, pcity->owner,
                       API_TYPE_PLAYER, destroyer);

    remove_city(pcity);
    return FALSE;
  }
  pcity->size -= pop_loss;

  city_refresh_vision(pcity);

  /* Cap the food stock at the new granary size. */
  if (pcity->food_stock > city_granary_size(pcity->size)) {
    pcity->food_stock = city_granary_size(pcity->size);
  }

  /* First try to kill off the specialists */
  loss_remain = pop_loss - city_reduce_specialists(pcity, pop_loss);

  /* Update number of people in each feelings category.
   * This must be after new pcity->size and specialists counts
   * have been set, and before any auto_arrange_workers() */
  city_refresh(pcity);

  if (loss_remain > 0) {
    /* Take it out on workers */
    loss_remain -= city_reduce_workers(pcity, loss_remain);

    /* Then rearrange workers */
    auto_arrange_workers(pcity);
    sync_cities();
  } else {
    send_city_info(city_owner(pcity), pcity);
  }

  if (0 != loss_remain) {
    freelog(LOG_FATAL, "city_reduce_size()"
            " has remaining %d of %d for \"%s\"[%d]",
            loss_remain, pop_loss,
            city_name(pcity), pcity->size);
    assert(0);
  }

  /* Update cities that have trade routes with us */
  for (i = 0; i < NUM_TRADEROUTES; i++) {
    struct city *pcity2 = game_find_city_by_number(pcity->trade[i]);

    if (pcity2) {
      city_refresh(pcity2);
    }
  }

  sanity_check_city(pcity);
  return TRUE;
}

/**************************************************************************
  Repair the city population without affecting city size.
  Used by savegame.c and sanitycheck.c
**************************************************************************/
void city_repair_size(struct city *pcity, int change)
{
  if (change > 0) {
    pcity->specialists[DEFAULT_SPECIALIST] += change;
  } else if (change < 0) {
    int need = change + city_reduce_specialists(pcity, -change);

    if (0 > need) {
      need += city_reduce_workers(pcity, -need);
    }

    if (0 != need) {
      freelog(LOG_FATAL, "city_repair_size()"
              " has remaining %d of %d for \"%s\"[%d]",
              need, change,
              city_name(pcity), pcity->size);
      assert(0);
    }
  }
}

/**************************************************************************
  Return the percentage of food that is lost in this city.

  Normally this value is 0% but this can be increased by EFT_GROWTH_FOOD
  effects.
**************************************************************************/
static int granary_savings(const struct city *pcity)
{
  int savings = get_city_bonus(pcity, EFT_GROWTH_FOOD);

  return CLIP(0, savings, 100);
}

/**************************************************************************
  Increase city size by one. We do not refresh borders or send info about
  the city to the clients as part of this function. There might be several
  calls to this function at once, and those actions are needed only once.
**************************************************************************/
static bool city_increase_size(struct city *pcity)
{
  int i, new_food;
  int savings_pct = granary_savings(pcity);
  bool have_square = FALSE;
  bool rapture_grow = city_rapture_grow(pcity); /* check before size increase! */
  struct tile *pcenter = city_tile(pcity);
  struct player *powner = city_owner(pcity);
  struct impr_type *pimprove = pcity->production.value.building;
  int saved_id = pcity->id;

  if (!city_can_grow_to(pcity, pcity->size + 1)) { /* need improvement */
    if (get_current_construction_bonus(pcity, EFT_SIZE_ADJ, RPT_CERTAIN) > 0
        || get_current_construction_bonus(pcity, EFT_SIZE_UNLIMIT, RPT_CERTAIN) > 0) {
      notify_player(powner, pcity->tile, E_CITY_AQ_BUILDING,
		       _("%s needs %s (being built) "
			 "to grow any further."),
			city_name(pcity),
			improvement_name_translation(pimprove));
    } else {
      notify_player(powner, pcity->tile, E_CITY_AQUEDUCT,
		       _("%s needs an improvement to grow any further."),
		       city_name(pcity));
    }
    /* Granary can only hold so much */
    new_food = (city_granary_size(pcity->size)
		* (100 * 100 - game.info.aqueductloss * (100 - savings_pct))
		/ (100 * 100));
    pcity->food_stock = MIN(pcity->food_stock, new_food);
    return FALSE;
  }

  pcity->size++;

  /* Do not empty food stock if city is growing by celebrating */
  if (rapture_grow) {
    new_food = city_granary_size(pcity->size);
  } else {
    new_food = city_granary_size(pcity->size) * savings_pct / 100;
  }
  pcity->food_stock = MIN(pcity->food_stock, new_food);

  /* If there is enough food, and the city is big enough,
   * make new citizens into scientists or taxmen -- Massimo */

  /* Ignore food if no square can be worked */
  city_tile_iterate_skip_free_cxy(pcenter, ptile, cx, cy) {
    if (tile_worked(ptile) != pcity /* quick test */
     && city_can_work_tile(pcity, ptile)) {
      have_square = TRUE;
    }
  } city_tile_iterate_skip_free_cxy_end;

  if ((pcity->surplus[O_FOOD] >= 2 || !have_square)
      && is_city_option_set(pcity, CITYO_NEW_EINSTEIN)) {
    pcity->specialists[best_specialist(O_SCIENCE, pcity)]++;
  } else if ((pcity->surplus[O_FOOD] >= 2 || !have_square)
	     && is_city_option_set(pcity, CITYO_NEW_TAXMAN)) {
    pcity->specialists[best_specialist(O_GOLD, pcity)]++;
  } else {
    pcity->specialists[DEFAULT_SPECIALIST]++; /* or else city is !sane */
    auto_arrange_workers(pcity);
  }

  city_refresh(pcity);

  /* Update cities that have trade routes with us */
  for (i = 0; i < NUM_TRADEROUTES; i++) {
    struct city *pcity2 = game_find_city_by_number(pcity->trade[i]);

    if (pcity2) {
      city_refresh(pcity2);
    }
  }

  notify_player(powner, pcity->tile, E_CITY_GROWTH,
                _("%s grows to size %d."),
                city_name(pcity),
                pcity->size);
  script_signal_emit("city_growth", 2,
		      API_TYPE_CITY, pcity, API_TYPE_INT, pcity->size);
  if (city_exist(saved_id)) {
    /* Script didn't destroy this city */
    sanity_check_city(pcity);
  }
  sync_cities();

  return TRUE;
}

/****************************************************************************
  Change the city size.  Return TRUE iff the city is still alive afterwards.
****************************************************************************/
bool city_change_size(struct city *pcity, int size)
{
  assert(size >= 0 && size <= MAX_CITY_SIZE);

  if (size > pcity->size) {
    /* Increase city size until size reached, or increase fails */
    while (size > pcity->size && city_increase_size(pcity)) ;
  } else if (size < pcity->size) {
    /* We assume that city_change_size() is never called because
     * of enemy actions. If that changes, enemy must be passed
     * to city_reduce_size() */
    return city_reduce_size(pcity, pcity->size - size, NULL);
  }

  city_refresh_vision(pcity);

  return TRUE;
}

/**************************************************************************
  Check whether the population can be increased or
  if the city is unable to support a 'settler'...
**************************************************************************/
static void city_populate(struct city *pcity)
{
  int saved_id = pcity->id;

  pcity->food_stock += pcity->surplus[O_FOOD];
  if (pcity->food_stock >= city_granary_size(pcity->size) 
     || city_rapture_grow(pcity)) {
    city_increase_size(pcity);
    city_refresh_vision(pcity);
  } else if (pcity->food_stock < 0) {
    /* FIXME: should this depend on units with ability to build
     * cities or on units that require food in uppkeep?
     * I'll assume citybuilders (units that 'contain' 1 pop) -- sjolie
     * The above may make more logical sense, but in game terms
     * you want to disband a unit that is draining your food
     * reserves.  Hence, I'll assume food upkeep > 0 units. -- jjm
     */
    unit_list_iterate_safe(pcity->units_supported, punit) {
      if (unit_type(punit)->upkeep[O_FOOD] > 0 
          && !unit_has_type_flag(punit, F_UNDISBANDABLE)) {

	notify_player(city_owner(pcity), pcity->tile, E_UNIT_LOST_MISC,
			 _("Famine feared in %s, %s lost!"), 
			 city_name(pcity),
			 unit_name_translation(punit));
 
        wipe_unit(punit);

        if (city_exist(saved_id)) {
          pcity->food_stock = (city_granary_size(pcity->size)
                               * granary_savings(pcity)) / 100;
        }
	return;
      }
    } unit_list_iterate_safe_end;
    if (pcity->size > 1) {
      notify_player(city_owner(pcity), pcity->tile, E_CITY_FAMINE,
		       _("Famine causes population loss in %s."),
		       city_name(pcity));
    } else {
      notify_player(city_owner(pcity), pcity->tile, E_CITY_FAMINE,
		    _("Famine destroys %s entirely."),
		    city_name(pcity));
    }
    pcity->food_stock = (city_granary_size(pcity->size - 1)
			 * granary_savings(pcity)) / 100;
    city_reduce_size(pcity, 1, NULL);
  }
}

/**************************************************************************
...
**************************************************************************/
void advisor_choose_build(struct player *pplayer, struct city *pcity)
{
  struct ai_choice choice;

  /* See what AI has to say */
  ai_advisor_choose_building(pcity, &choice);
  if (valid_improvement(choice.value.building)) {
    struct universal target = {
      .kind = VUT_IMPROVEMENT,
      .value = {.building = choice.value.building}
    };

    change_build_target(pplayer, pcity, target, E_IMP_AUTO);
    return;
  }

  /* Build the first thing we can think of (except a new palace). */
  improvement_iterate(pimprove) {
    if (can_city_build_improvement_now(pcity, pimprove)
	&& !building_has_effect(pimprove, EFT_CAPITAL_CITY)) {
      struct universal target = {
        .kind = VUT_IMPROVEMENT,
        .value = {.building = pimprove}
      };

      change_build_target(pplayer, pcity, target, E_IMP_AUTO);
      return;
    }
  } improvement_iterate_end;
}

/**************************************************************************
  Examine the worklist and change the build target.  Return 0 if no
  targets are available to change to.  Otherwise return non-zero.  Has
  the side-effect of removing from the worklist any no-longer-available
  targets as well as the target actually selected, if any.
**************************************************************************/
static bool worklist_change_build_target(struct player *pplayer,
					 struct city *pcity)
{
  struct universal target;
  bool success = FALSE;
  int i;
  int saved_id = pcity->id;
  bool city_checked = TRUE; /* This is used to avoid spurious city_exist() calls */
  struct worklist *pwl = &pcity->worklist;

  if (worklist_is_empty(pwl)) {
    /* Nothing in the worklist; bail now. */
    return FALSE;
  }

  i = 0;
  while (!success && i < worklist_length(pwl)) {

    if (!city_checked) {
      if (!city_exist(saved_id)) {
        /* Some script has removed useless city that cannot build
         * what it is told to! */
        return FALSE;
      }
      city_checked = TRUE;
    }

    if (worklist_peek_ith(pwl, &target, i)) {
      success = can_city_build_now(pcity, target);
    } else {
      success = FALSE;
    }
    i++;

    if (success) {
      break; /* while */
    }

    switch (target.kind) {
    case VUT_UTYPE:
    {
      struct unit_type *ptarget = target.value.utype;
      struct unit_type *pupdate = unit_upgrades_to(pcity, ptarget);

      /* Maybe we can just upgrade the target to what the city /can/ build. */
      if (U_NOT_OBSOLETED == pupdate) {
	/* Nope, we're stuck.  Skip this item from the worklist. */
	notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
			 _("%s can't build %s from the worklist; "
			   "tech not yet available.  Postponing..."),
			 city_name(pcity),
			 utype_name_translation(ptarget));
	script_signal_emit("unit_cant_be_built", 3,
			   API_TYPE_UNIT_TYPE, ptarget,
			   API_TYPE_CITY, pcity,
			   API_TYPE_STRING, "need_tech");
        city_checked = FALSE;
	break;
      }
      success = can_city_build_unit_later(pcity, pupdate);
      if (!success) {
	/* If the city can never build this unit or its descendants,
	 * drop it. */
	notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
			 _("%s can't build %s from the worklist.  "
			   "Purging..."),
			 city_name(pcity),
			 /* Yes, warn about the targets that's actually
			    in the worklist, not its obsolete-closure
			    pupdate. */
			 utype_name_translation(ptarget));
	script_signal_emit("unit_cant_be_built", 3,
			   API_TYPE_UNIT_TYPE, ptarget,
			   API_TYPE_CITY, pcity,
			   API_TYPE_STRING, "never");
        if (city_exist(saved_id)) {
          city_checked = TRUE;
          /* Purge this worklist item. */
          i--;
          worklist_remove(pwl, i);
        } else {
          city_checked = FALSE;
        }
      } else {
	/* Yep, we can go after pupdate instead.  Joy! */
	notify_player(pplayer, pcity->tile, E_WORKLIST,
			 _("Production of %s is upgraded to %s in %s."),
			 utype_name_translation(ptarget), 
			 utype_name_translation(pupdate),
			 city_name(pcity));
	target.value.utype = pupdate;
      }
      break;
    }
    case VUT_IMPROVEMENT:
    {
      struct impr_type *ptarget = target.value.building;
      struct impr_type *pupdate = building_upgrades_to(pcity, ptarget);

      /* If the city can never build this improvement, drop it. */
      success = can_city_build_improvement_later(pcity, pupdate);
      if (!success) {
	/* Nope, never in a million years. */
	notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
			 _("%s can't build %s from the worklist.  "
			   "Purging..."),
			 city_name(pcity),
			 city_improvement_name_translation(pcity, ptarget));
	script_signal_emit("building_cant_be_built", 3,
			   API_TYPE_BUILDING_TYPE, ptarget,
			   API_TYPE_CITY, pcity,
			   API_TYPE_STRING, "never");
        if (city_exist(saved_id)) {
          city_checked = TRUE;
          /* Purge this worklist item. */
          i--;
          worklist_remove(pwl, i);
        } else {
          city_checked = FALSE;
        }
	break;
      }

      /* Maybe this improvement has been obsoleted by something that
	 we can build. */
      if (pupdate == ptarget) {
	bool known = FALSE;

	/* Nope, no use.  *sigh*  */
	requirement_vector_iterate(&ptarget->reqs, preq) {
	  if (!is_req_active(pplayer, pcity, NULL, NULL, NULL, NULL, NULL,
			     preq, RPT_POSSIBLE)) {
	    known = TRUE;
	    switch (preq->source.kind) {
	    case VUT_ADVANCE:
	      notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
			       _("%s can't build %s from the worklist; "
				 "tech %s not yet available.  Postponing..."),
			       city_name(pcity),
			       city_improvement_name_translation(pcity, ptarget),
			       advance_name_for_player(pplayer,
					     advance_number(preq->source.value.advance)));
	      script_signal_emit("building_cant_be_built", 3,
				 API_TYPE_BUILDING_TYPE, ptarget,
				 API_TYPE_CITY, pcity,
				 API_TYPE_STRING, "need_tech");
	      break;
	    case VUT_IMPROVEMENT:
	      notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
			       _("%s can't build %s from the worklist; "
				 "need to have %s first.  Postponing..."),
			       city_name(pcity),
			       city_improvement_name_translation(pcity, ptarget),
			       city_improvement_name_translation(pcity,
						preq->source.value.building));
	      script_signal_emit("building_cant_be_built", 3,
				 API_TYPE_BUILDING_TYPE, ptarget,
				 API_TYPE_CITY, pcity,
				 API_TYPE_STRING, "need_building");
	      break;
	    case VUT_GOVERNMENT:
	      notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
			       _("%s can't build %s from the worklist; "
				 "it needs %s government.  Postponing..."),
			       city_name(pcity),
			       city_improvement_name_translation(pcity, ptarget),
			       government_name_translation(preq->source.value.govern));
	      script_signal_emit("building_cant_be_built", 3,
				 API_TYPE_BUILDING_TYPE, ptarget,
				 API_TYPE_CITY, pcity,
				 API_TYPE_STRING, "need_government");
	      break;
	    case VUT_SPECIAL:
	      notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
			       _("%s can't build %s from the worklist; "
				 "%s special is required.  Postponing..."),
			       city_name(pcity),
			       city_improvement_name_translation(pcity, ptarget),
			       special_name_translation(preq->source.value.special));
	      script_signal_emit("building_cant_be_built", 3,
				 API_TYPE_BUILDING_TYPE, ptarget,
				 API_TYPE_CITY, pcity,
				 API_TYPE_STRING, "need_special");
	      break;
	    case VUT_TERRAIN:
	      notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
			       _("%s can't build %s from the worklist; "
				 "%s terrain is required.  Postponing..."),
			       city_name(pcity),
			       city_improvement_name_translation(pcity, ptarget),
			       terrain_name_translation(preq->source.value.terrain));
	      script_signal_emit("building_cant_be_built", 3,
				 API_TYPE_BUILDING_TYPE, ptarget,
				 API_TYPE_CITY, pcity,
				 API_TYPE_STRING, "need_terrain");
	      break;
	    case VUT_NATION:
	      /* FIXME: we should skip rather than postpone, since we'll
	       * never be able to meet this req... */
	      notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
			       _("%s can't build %s from the worklist; "
				 "only %s may build this.  Postponing..."),
			       city_name(pcity),
			       city_improvement_name_translation(pcity, ptarget),
			       nation_plural_translation(preq->source.value.nation));
	      script_signal_emit("building_cant_be_built", 3,
				 API_TYPE_BUILDING_TYPE, ptarget,
				 API_TYPE_CITY, pcity,
				 API_TYPE_STRING, "need_nation");
	      break;
	    case VUT_MINSIZE:
	      notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
			       _("%s can't build %s from the worklist; "
				 "city must be of size %d.  Postponing..."),
			       city_name(pcity),
			       city_improvement_name_translation(pcity, ptarget),
			       preq->source.value.minsize);
	      script_signal_emit("building_cant_be_built", 3,
				 API_TYPE_BUILDING_TYPE, ptarget,
				 API_TYPE_CITY, pcity,
				 API_TYPE_STRING, "need_minsize");
	      break;
            case VUT_AI_LEVEL:
              /* FIXME: we should skip rather than postpone, since we'll
               * never be able to meet this req... */
              notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
                            _("%s can't build %s from the worklist; "
                              "only AI of level %s may build this.  "
                              "Postponing..."),
                            city_name(pcity),
                            city_improvement_name_translation(pcity, ptarget),
                            ai_level_name(preq->source.value.ai_level));
              script_signal_emit("building_cant_be_built", 3,
                                 API_TYPE_BUILDING_TYPE, ptarget,
                                 API_TYPE_CITY, pcity,
                                 API_TYPE_STRING, "need_ai_level");
	      break;
	    case VUT_TERRAINCLASS:
	      notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
			       _("%s can't build %s from the worklist; "
				 "%s terrain class is required.  Postponing..."),
			       city_name(pcity),
			       city_improvement_name_translation(pcity, ptarget),
			       terrain_class_name_translation(preq->source.value.terrainclass));
	      script_signal_emit("building_cant_be_built", 3,
				 API_TYPE_BUILDING_TYPE, ptarget,
				 API_TYPE_CITY, pcity,
				 API_TYPE_STRING, "need_terrainclass");
	      break;
	    case VUT_UTYPE:
	    case VUT_UTFLAG:
	    case VUT_UCLASS:
	    case VUT_UCFLAG:
	    case VUT_OTYPE:
	    case VUT_SPECIALIST:
	      /* Will only happen with a bogus ruleset. */
	      freelog(LOG_ERROR, "worklist_change_build_target()"
	      	      " has bogus preq");
	      break;
	    case VUT_NONE:
	    case VUT_LAST:
	    default:
	      freelog(LOG_FATAL, "worklist_change_build_target()"
	      	      " called with invalid preq");
	      assert(0);
	      break;
	    };
	    break;
	  }

          /* Almost all cases emit signal in the end, so city check needed. */
          if (!city_exist(saved_id)) {
            /* Some script has removed city */
            return FALSE;
          }
          city_checked = TRUE;

	} requirement_vector_iterate_end;

	if (!known) {
	  /* This shouldn't happen...
	     FIXME: make can_city_build_improvement_now() return a reason enum. */
	  notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
			   _("%s can't build %s from the worklist; "
			     "Reason unknown!  Postponing..."),
			   city_name(pcity),
			   city_improvement_name_translation(pcity, ptarget));
	}
      } else {
	/* Hey, we can upgrade the improvement!  */
	notify_player(pplayer, pcity->tile, E_WORKLIST,
			 _("Production of %s is upgraded to %s in %s."),
			 city_improvement_name_translation(pcity, ptarget), 
			 city_improvement_name_translation(pcity, pupdate),
			 city_name(pcity));
	target.value.building = pupdate;
	success = TRUE;
      }
      break;
    }
    default:
      /* skip useless target */
      freelog(LOG_ERROR, "worklist_change_build_target()"
	      " has unrecognized target kind (%d)",
	      target.kind);
      break;
    };
  } /* while */

  if (success) {
    /* All okay.  Switch targets. */
    change_build_target(pplayer, pcity, target, E_WORKLIST);

    /* i is the index immediately _after_ the item we're changing to.
       Remove the (i-1)th item from the worklist. */
    worklist_remove(pwl, i - 1);
  }

  if (worklist_is_empty(pwl)) {
    /* There *was* something in the worklist, but it's empty now.  Bug the
       player about it. */
    notify_player(pplayer, pcity->tile, E_WORKLIST,
		  /* TRANS: The <city> worklist .... */
		  _("The %s worklist is now empty."),
		  city_name(pcity));
  }

  return success;
}

/**************************************************************************
  Assuming we just finished building something, find something new to
  build.  The policy is: use the worklist if we can; if not, try not
  changing; if we must change, get desparate and use the AI advisor.
**************************************************************************/
static void choose_build_target(struct player *pplayer,
				struct city *pcity)
{
  /* Pick the next thing off the worklist. */
  if (worklist_change_build_target(pplayer, pcity)) {
    return;
  }

  /* Try building the same thing again.  Repeat building doesn't require a
   * call to change_build_target, so just return. */
  switch (pcity->production.kind) {
  case VUT_UTYPE:
    /* We can build a unit again unless it's unique. */
    if (!utype_has_flag(pcity->production.value.utype, F_UNIQUE)) {
      return;
    }
    break;
  case VUT_IMPROVEMENT:
    if (can_city_build_improvement_now(pcity, pcity->production.value.building)) {
      /* We can build space and coinage again, and possibly others. */
      return;
    }
    break;
  default:
    /* fallthru */
    break;
  };

  /* Find *something* to do! */
  freelog(LOG_DEBUG, "Trying advisor_choose_build.");
  advisor_choose_build(pplayer, pcity);
  freelog(LOG_DEBUG, "Advisor_choose_build didn't kill us.");
}

/**************************************************************************
  Follow the list of replaced_by buildings until we hit something that
  we can build.  Returns NULL if we can't upgrade at all (including if the
  original building is unbuildable).
**************************************************************************/
static struct impr_type *building_upgrades_to(struct city *pcity,
					      struct impr_type *pimprove)
{
  struct impr_type *check = pimprove;
  struct impr_type *best_upgrade = NULL;

  if (!can_city_build_improvement_direct(pcity, check)) {
    return NULL;
  }
  while (valid_improvement(check = check->replaced_by)) {
    if (can_city_build_improvement_direct(pcity, check)) {
      best_upgrade = check;
    }
  }

  return best_upgrade;
}

/**************************************************************************
  Try to upgrade production in pcity.
**************************************************************************/
static void upgrade_building_prod(struct city *pcity)
{
  struct impr_type *producing = pcity->production.value.building;
  struct impr_type *upgrading = building_upgrades_to(pcity, producing);

  if (upgrading && can_city_build_improvement_now(pcity, upgrading)) {
    notify_player(city_owner(pcity), pcity->tile, E_UNIT_UPGRADED,
		     _("Production of %s is upgraded to %s in %s."),
		     improvement_name_translation(producing),
		     improvement_name_translation(upgrading),
		     city_name(pcity));
    pcity->production.kind = VUT_IMPROVEMENT;
    pcity->production.value.building = upgrading;
  }
}

/**************************************************************************
  Follow the list of obsoleted_by units until we hit something that
  we can build.  Return NULL when we can't upgrade at all.  NB:  returning
  something doesn't guarantee that pcity really _can_ build it; just that
  pcity can't build whatever _obsoletes_ it.

  FIXME: this function is a duplicate of can_upgrade_unittype.
**************************************************************************/
static struct unit_type *unit_upgrades_to(struct city *pcity,
					  struct unit_type *punittype)
{
  struct unit_type *check = punittype;
  struct unit_type *best_upgrade = U_NOT_OBSOLETED;

  if (!can_city_build_unit_direct(pcity, punittype)) {
    return U_NOT_OBSOLETED;
  }
  while ((check = check->obsoleted_by) != U_NOT_OBSOLETED) {
    if (can_city_build_unit_direct(pcity, check)) {
      best_upgrade = check;
    }
  }

  return best_upgrade;
}

/**************************************************************************
  Try to upgrade production in pcity.
**************************************************************************/
static void upgrade_unit_prod(struct city *pcity)
{
  struct unit_type *producing = pcity->production.value.utype;
  struct unit_type *upgrading = unit_upgrades_to(pcity, producing);

  if (upgrading && can_city_build_unit_direct(pcity, upgrading)) {
    notify_player(city_owner(pcity), pcity->tile, E_UNIT_UPGRADED, 
		  _("Production of %s is upgraded to %s in %s."),
		  utype_name_translation(producing),
		  utype_name_translation(upgrading), 
		  city_name(pcity));
    pcity->production.value.utype = upgrading;
  }
}

/**************************************************************************
  Disband units if we don't have enough shields to support them.  Returns
  FALSE if the _city_ is disbanded as a result.
**************************************************************************/
static bool city_distribute_surplus_shields(struct player *pplayer,
					    struct city *pcity)
{
  if (pcity->surplus[O_SHIELD] < 0) {
    unit_list_iterate_safe(pcity->units_supported, punit) {
      if (utype_upkeep_cost(unit_type(punit), pplayer, O_SHIELD) > 0
	  && pcity->surplus[O_SHIELD] < 0
          && !unit_has_type_flag(punit, F_UNDISBANDABLE)) {
	notify_player(pplayer, pcity->tile, E_UNIT_LOST_MISC,
			 _("%s can't upkeep %s, unit disbanded."),
			 city_name(pcity),
			 unit_name_translation(punit));
        handle_unit_disband(pplayer, punit->id);
	/* pcity->surplus[O_SHIELD] is automatically updated. */
      }
    } unit_list_iterate_safe_end;
  }

  if (pcity->surplus[O_SHIELD] < 0) {
    /* Special case: F_UNDISBANDABLE. This nasty unit won't go so easily.
     * It'd rather make the citizens pay in blood for their failure to upkeep
     * it! If we make it here all normal units are already disbanded, so only
     * undisbandable ones remain. */
    unit_list_iterate_safe(pcity->units_supported, punit) {
      int upkeep = utype_upkeep_cost(unit_type(punit), pplayer, O_SHIELD);

      if (upkeep > 0 && pcity->surplus[O_SHIELD] < 0) {
	assert(unit_has_type_flag(punit, F_UNDISBANDABLE));
	notify_player(pplayer, pcity->tile, E_UNIT_LOST_MISC,
			 _("Citizens in %s perish for their failure to "
			 "upkeep %s!"),
			 city_name(pcity),
			 unit_name_translation(punit));
	if (!city_reduce_size(pcity, 1, NULL)) {
	  return FALSE;
	}

	/* No upkeep for the unit this turn. */
	pcity->surplus[O_SHIELD] += upkeep;
      }
    } unit_list_iterate_safe_end;
  }

  /* Now we confirm changes made last turn. */
  pcity->shield_stock += pcity->surplus[O_SHIELD];
  pcity->before_change_shields = pcity->shield_stock;
  pcity->last_turns_shield_surplus = pcity->surplus[O_SHIELD];

  return TRUE;
}

/**************************************************************************
  Returns FALSE when the city is removed, TRUE otherwise.
**************************************************************************/
static bool city_build_building(struct player *pplayer, struct city *pcity)
{
  bool space_part;
  int mod;
  struct impr_type *pimprove = pcity->production.value.building;
  int saved_id = pcity->id;

  if (city_production_has_flag(pcity, IF_GOLD)) {
    assert(pcity->surplus[O_SHIELD] >= 0);
    /* pcity->before_change_shields already contains the surplus from
     * this turn. */
    pplayer->economic.gold += pcity->before_change_shields;
    pcity->before_change_shields = 0;
    pcity->shield_stock = 0;
    choose_build_target(pplayer, pcity);
    return TRUE;
  }
  upgrade_building_prod(pcity);

  if (!can_city_build_improvement_now(pcity, pimprove)) {
    notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
		     _("%s is building %s, which "
		       "is no longer available."),
		     city_name(pcity),
		     city_improvement_name_translation(pcity, pimprove));
    script_signal_emit("building_cant_be_built", 3,
		       API_TYPE_BUILDING_TYPE, pimprove,
		       API_TYPE_CITY, pcity,
		       API_TYPE_STRING, "unavailable");
    return TRUE;
  }
  if (pcity->shield_stock >= impr_build_shield_cost(pimprove)) {
    if (is_small_wonder(pimprove)) {
      city_list_iterate(pplayer->cities, wcity) {
	if (city_has_building(wcity, pimprove)) {
	  city_remove_improvement(wcity, pimprove);
	  break;
	}
      } city_list_iterate_end;
    }

    space_part = TRUE;
    if (get_current_construction_bonus(pcity, EFT_SS_STRUCTURAL,
                                       RPT_CERTAIN) > 0) {
      pplayer->spaceship.structurals++;
    } else if (get_current_construction_bonus(pcity, EFT_SS_COMPONENT,
                                              RPT_CERTAIN) > 0) {
      pplayer->spaceship.components++;
    } else if (get_current_construction_bonus(pcity, EFT_SS_MODULE,
                                              RPT_CERTAIN) > 0) {
      pplayer->spaceship.modules++;
    } else {
      space_part = FALSE;
      city_add_improvement(pcity, pimprove);
    }
    pcity->before_change_shields -= impr_build_shield_cost(pimprove);
    pcity->shield_stock -= impr_build_shield_cost(pimprove);
    pcity->turn_last_built = game.info.turn;
    /* to eliminate micromanagement */
    if (is_great_wonder(pimprove)) {
      game.info.great_wonders[improvement_index(pimprove)] = pcity->id;

      notify_player(NULL, pcity->tile, E_WONDER_BUILD,
		       _("The %s have finished building %s in %s."),
		       nation_plural_for_player(pplayer),
		       city_improvement_name_translation(pcity, pimprove),
		       city_name(pcity));

    } else if (is_small_wonder(pimprove)) {
      pplayer->small_wonders[improvement_index(pimprove)] = pcity->id;
    }

    notify_player(pplayer, pcity->tile, E_IMP_BUILD,
		     _("%s has finished building %s."),
		     city_name(pcity),
		     improvement_name_translation(pimprove));
    script_signal_emit("building_built", 2,
		       API_TYPE_BUILDING_TYPE, pimprove,
		       API_TYPE_CITY, pcity);

    if (!city_exist(saved_id)) {
      /* Script removed city */
      return FALSE;
    }

    /* Call this function since some buildings may change the
     * the vision range of a city */
    city_refresh_vision(pcity);

    if ((mod = get_current_construction_bonus(pcity, EFT_GIVE_IMM_TECH,
                                              RPT_CERTAIN))) {
      int i;

      notify_player(pplayer, NULL, E_TECH_GAIN,
		    PL_("%s boosts research; you gain %d immediate advance.",
			"%s boosts research; you gain %d immediate advances.",
			mod),
		    improvement_name_translation(pimprove),
		    mod);

      for (i = 0; i < mod; i++) {
	Tech_type_id tech = give_immediate_free_tech(pplayer);

	notify_embassies(pplayer, NULL, NULL, E_TECH_GAIN,
	    _("The %s have acquired %s from %s."),
	    nation_plural_for_player(pplayer),
	    advance_name_for_player(pplayer, tech),
	    improvement_name_translation(pimprove));
      }
    }
    if (space_part && pplayer->spaceship.state == SSHIP_NONE) {
      notify_player(NULL, pcity->tile, E_SPACESHIP,
		       _("The %s have started "
			 "building a spaceship!"),
		       nation_plural_for_player(pplayer));
      pplayer->spaceship.state = SSHIP_STARTED;
    }
    if (space_part) {
      send_spaceship_info(pplayer, NULL);
    } else {
      city_refresh(pcity);
    }

    /* Move to the next thing in the worklist */
    choose_build_target(pplayer, pcity);
  }

  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
static bool city_build_unit(struct player *pplayer, struct city *pcity)
{
  struct unit_type *utype;

  upgrade_unit_prod(pcity);
  utype = pcity->production.value.utype;

  /* We must make a special case for barbarians here, because they are
     so dumb. Really. They don't know the prerequisite techs for units
     they build!! - Per */
  if (!can_city_build_unit_direct(pcity, utype)
      && !is_barbarian(pplayer)) {
    notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
        _("%s is building %s, which is no longer available."),
        city_name(pcity),
        utype_name_translation(utype));

    /* Log before signal emitting, so pointers are certainly valid */
    freelog(LOG_VERBOSE, "%s %s tried to build %s, which is not available.",
            nation_rule_name(nation_of_city(pcity)),
            city_name(pcity),
            utype_rule_name(utype));
    script_signal_emit("unit_cant_be_built", 3,
		       API_TYPE_UNIT_TYPE, utype,
		       API_TYPE_CITY, pcity,
		       API_TYPE_STRING, "unavailable");
    return TRUE;
  }
  if (pcity->shield_stock >= utype_build_shield_cost(utype)) {
    int pop_cost = utype_pop_value(utype);
    struct unit *punit;
    int saved_city_id = pcity->id;

    /* Should we disband the city? -- Massimo */
    if (pcity->size == pop_cost
	&& is_city_option_set(pcity, CITYO_DISBAND)) {
      return !disband_city(pcity);
    }

    if (pcity->size <= pop_cost) {
      notify_player(pplayer, pcity->tile, E_CITY_CANTBUILD,
		       _("%s can't build %s yet."),
		       city_name(pcity),
		       utype_name_translation(utype));
      script_signal_emit("unit_cant_be_built", 3,
			 API_TYPE_UNIT_TYPE, utype,
			 API_TYPE_CITY, pcity,
			 API_TYPE_STRING, "pop_cost");
      return TRUE;
    }

    assert(pop_cost == 0 || pcity->size >= pop_cost);

    /* don't update turn_last_built if we returned above */
    pcity->turn_last_built = game.info.turn;

    punit = create_unit(pplayer, pcity->tile, utype,
			do_make_unit_veteran(pcity, utype),
			pcity->id, 0);

    /* After we created the unit remove the citizen. This will also
       rearrange the worker to take into account the extra resources
       (food) needed. */
    if (pop_cost > 0) {
      city_reduce_size(pcity, pop_cost, NULL);
    }

    /* to eliminate micromanagement, we only subtract the unit's
       cost */
    pcity->before_change_shields -= utype_build_shield_cost(utype);
    pcity->shield_stock -= utype_build_shield_cost(utype);

    notify_player(pplayer, pcity->tile, E_UNIT_BUILT,
		     /* TRANS: <city> is finished building <unit/building>. */
		     _("%s is finished building %s."),
		     city_name(pcity),
		     utype_name_translation(utype));

    script_signal_emit("unit_built",
		       2, API_TYPE_UNIT, punit, API_TYPE_CITY, pcity);

    if (city_exist(saved_city_id)) {
      /* Done building this unit; time to move on to the next. */
      choose_build_target(pplayer, pcity);
    }
  }
  return TRUE;
}

/**************************************************************************
  Returns FALSE when the city is removed, TRUE otherwise.
**************************************************************************/
static bool city_build_stuff(struct player *pplayer, struct city *pcity)
{
  if (!city_distribute_surplus_shields(pplayer, pcity)) {
    return FALSE;
  }

  nullify_caravan_and_disband_plus(pcity);
  define_orig_production_values(pcity);

  switch (pcity->production.kind) {
  case VUT_IMPROVEMENT:
    return city_build_building(pplayer, pcity);
  case VUT_UTYPE:
    return city_build_unit(pplayer, pcity);
  default:
    /* must never happen! */
    assert(0);
    break;
  };
  return FALSE;
}

/**************************************************************************
  Pay for upkeep costs for all buildings, or sell them.
**************************************************************************/
static void pay_for_buildings(struct player *pplayer, struct city *pcity)
{
  city_built_iterate(pcity, pimprove) {
    if (can_city_sell_building(pcity, pimprove)) {
      int upkeep = city_improvement_upkeep(pcity, pimprove);

      if (pplayer->economic.gold - upkeep < 0) {
	notify_player(pplayer, pcity->tile, E_IMP_AUCTIONED,
			 _("Can't afford to maintain %s in %s, "
			   "building sold!"),
			 improvement_name_translation(pimprove),
			 city_name(pcity));
	do_sell_building(pplayer, pcity, pimprove);
	city_refresh(pcity);
      } else
        pplayer->economic.gold -= upkeep;
    }
  } city_built_iterate_end;
}

/**************************************************************************
 Add some Pollution if we have waste
**************************************************************************/
static void check_pollution(struct city *pcity)
{
  struct tile *ptile;
  struct tile *pcenter = city_tile(pcity);
  int k=100;

  if (pcity->pollution != 0 && myrand(100) <= pcity->pollution) {
    while (k > 0) {
      /* place pollution somewhere in city radius */
      int cx = myrand(CITY_MAP_SIZE);
      int cy = myrand(CITY_MAP_SIZE);

      /* if is a corner tile or not a real map position */
      if (!is_valid_city_coords(cx, cy)
	  || !(ptile = city_map_to_tile(pcenter, cx, cy))) {
	continue;
      }

      if (!terrain_has_flag(tile_terrain(ptile), TER_NO_POLLUTION)
	  && !tile_has_special(ptile, S_POLLUTION)) {
	tile_set_special(ptile, S_POLLUTION);
	update_tile_knowledge(ptile);
	notify_player(city_owner(pcity), pcenter, E_POLLUTION,
		      _("Pollution near %s."),
		      city_name(pcity));
	return;
      }
      k--;
    }
    freelog(LOG_DEBUG, "pollution not placed: city: %s", city_name(pcity));
  }
}

/**************************************************************************
  Returns the cost to incite a city. This depends on the size of the city,
  the number of happy, unhappy and angry citizens, whether it is
  celebrating, how close it is to the capital, how many units it has and
  upkeeps, presence of courthouse and its buildings and wonders.
**************************************************************************/
int city_incite_cost(struct player *pplayer, struct city *pcity)
{
  struct city *capital;
  int dist, size, cost;

  if (get_city_bonus(pcity, EFT_NO_INCITE) > 0) {
    return INCITE_IMPOSSIBLE_COST;
  }

  /* Gold factor */
  cost = city_owner(pcity)->economic.gold + game.info.base_incite_cost;

  unit_list_iterate(pcity->tile->units, punit) {
    cost += (unit_build_shield_cost(punit)
	     * game.info.incite_unit_factor);
  } unit_list_iterate_end;

  /* Buildings */
  city_built_iterate(pcity, pimprove) {
    cost += impr_build_shield_cost(pimprove) * game.info.incite_improvement_factor;
  } city_built_iterate_end;

  /* Stability bonuses */
  if (!city_unhappy(pcity)) {
    cost *= 2;
  }
  if (city_celebrating(pcity)) {
    cost *= 2;
  }

  /* City is empty */
  if (unit_list_size(pcity->tile->units) == 0) {
    cost /= 2;
  }

  /* Buy back is cheap, conquered cities are also cheap */
  if (city_owner(pcity) != pcity->original) {
    if (pplayer == pcity->original) {
      cost /= 2;            /* buy back: 50% price reduction */
    } else {
      cost = cost * 2 / 3;  /* buy conquered: 33% price reduction */
    }
  }

  /* Distance from capital */
  capital = find_palace(city_owner(pcity));
  if (capital) {
    int tmp = map_distance(capital->tile, pcity->tile);
    dist = MIN(32, tmp);
  } else {
    /* No capital? Take max penalty! */
    dist = 32;
  }

  size = MAX(1, pcity->size
                + pcity->feel[CITIZEN_HAPPY][FEELING_FINAL]
                - pcity->feel[CITIZEN_UNHAPPY][FEELING_FINAL]
                - pcity->feel[CITIZEN_ANGRY][FEELING_FINAL] * 3);
  cost *= size;
  cost *= game.info.incite_total_factor;
  cost = cost / (dist + 3);
  
  cost += (cost * get_city_bonus(pcity, EFT_INCITE_COST_PCT)) / 100;
  cost /= 100;

  return cost;
}

/**************************************************************************
 Called every turn, at beginning of turn, for every city.
**************************************************************************/
static void define_orig_production_values(struct city *pcity)
{
  /* Remember what this city is building last turn, so that on the next turn
   * the player can switch production to something else and then change it
   * back without penalty.  This has to be updated _before_ production for
   * this turn is calculated, so that the penalty will apply if the player
   * changes production away from what has just been completed.  This makes
   * sense if you consider what this value means: all the shields in the
   * city have been dedicated toward the project that was chosen last turn,
   * so the player shouldn't be penalized if the governor has to pick
   * something different.  See city_change_production_penalty(). */
  pcity->changed_from = pcity->production;

  freelog(LOG_DEBUG,
	  "In %s, building %s.  Beg of Turn shields = %d",
	  city_name(pcity),
	  universal_rule_name(&pcity->changed_from),
	  pcity->before_change_shields);
}

/**************************************************************************
...
**************************************************************************/
static void nullify_caravan_and_disband_plus(struct city *pcity)
{
  pcity->disbanded_shields=0;
  pcity->caravan_shields=0;
}

/**************************************************************************
...
**************************************************************************/
void nullify_prechange_production(struct city *pcity)
{
  nullify_caravan_and_disband_plus(pcity);
  pcity->before_change_shields=0;
}

/**************************************************************************
 Called every turn, at end of turn, for every city.
**************************************************************************/
static void update_city_activity(struct player *pplayer, struct city *pcity)
{
  struct government *g = government_of_city(pcity);
  int saved_id = pcity->id;

  city_refresh(pcity);

  /* reporting of celebrations rewritten, copying the treatment of disorder below,
     with the added rapture rounds count.  991219 -- Jing */
  if (city_build_stuff(pplayer, pcity)) {
    if (city_celebrating(pcity)) {
      pcity->rapture++;
      if (pcity->rapture == 1) {
        notify_player(pplayer, pcity->tile, E_CITY_LOVE,
                      _("Celebrations in your honour in %s."),
                      city_name(pcity));
      }
    } else {
      if (pcity->rapture != 0) {
        notify_player(pplayer, pcity->tile, E_CITY_NORMAL,
                      _("Celebrations canceled in %s."),
                      city_name(pcity));
      }
      pcity->rapture=0;
    }
    pcity->was_happy=city_happy(pcity);

    /* City population updated here, after the rapture stuff above. --Jing */
    {
      int id=pcity->id;
      city_populate(pcity);
      if(!player_find_city_by_id(pplayer, id))
	return;
    }

    pcity->is_updated=TRUE;

    pcity->did_sell=FALSE;
    pcity->did_buy = FALSE;
    pcity->airlift = get_city_bonus(pcity, EFT_AIRLIFT);
    update_tech(pplayer, pcity->prod[O_SCIENCE]);
    pplayer->economic.gold+=pcity->prod[O_GOLD];
    pay_for_units(pplayer, pcity);
    if (city_exist(saved_id)) {
      pay_for_buildings(pplayer, pcity);

      if(city_unhappy(pcity)) { 
        pcity->anarchy++;
        if (pcity->anarchy == 1) {
          notify_player(pplayer, pcity->tile, E_CITY_DISORDER,
                        _("Civil disorder in %s."),
                        city_name(pcity));
        } else {
          notify_player(pplayer, pcity->tile, E_CITY_DISORDER,
                        _("CIVIL DISORDER CONTINUES in %s."),
                        city_name(pcity));
        }
      } else {
        if (pcity->anarchy != 0) {
          notify_player(pplayer, pcity->tile, E_CITY_NORMAL,
                        _("Order restored in %s."),
                        city_name(pcity));
        }
        pcity->anarchy = 0;
      }
      check_pollution(pcity);

      send_city_info(NULL, pcity);
      if (pcity->anarchy>2 
          && get_player_bonus(pplayer, EFT_REVOLUTION_WHEN_UNHAPPY) > 0) {
        notify_player(pplayer, pcity->tile, E_ANARCHY,
                      _("The people have overthrown your %s, "
                        "your country is in turmoil."),
                      government_name_translation(g));
        handle_player_change_government(pplayer, government_number(g));
      }
      sanity_check_city(pcity);
    }
  }
}

/**************************************************************************
 Disband a city into the built unit, supported by the closest city.
**************************************************************************/
static bool disband_city(struct city *pcity)
{
  struct player *pplayer = city_owner(pcity);
  struct tile *ptile = pcity->tile;
  struct city *rcity=NULL;
  struct unit_type *utype = pcity->production.value.utype;
  int saved_id = pcity->id;

  /* find closest city other than pcity */
  rcity = find_closest_owned_city(pplayer, ptile, FALSE, pcity);

  if (!rcity) {
    /* What should we do when we try to disband our only city? */
    notify_player(pplayer, ptile, E_CITY_CANTBUILD,
		     _("%s can't build %s yet, "
		     "and we can't disband our only city."),
		     city_name(pcity),
		     utype_name_translation(utype));
    script_signal_emit("unit_cant_be_built", 3,
		       API_TYPE_UNIT_TYPE, utype,
		       API_TYPE_CITY, pcity,
		       API_TYPE_STRING, "pop_cost");
    if (!city_exist(saved_id)) {
      /* Script decided to remove even the last city */
      return TRUE;
    } else {
      return FALSE;
    }
  }

  (void) create_unit(pplayer, ptile, utype,
		     do_make_unit_veteran(pcity, utype),
		     pcity->id, 0);

  /* Shift all the units supported by pcity (including the new unit)
   * to rcity.  transfer_city_units does not make sure no units are
   * left floating without a transport, but since all units are
   * transferred this is not a problem. */
  transfer_city_units(pplayer, pplayer, pcity->units_supported, rcity, 
                      pcity, -1, TRUE);

  notify_player(pplayer, ptile, E_UNIT_BUILT,
		   /* TRANS: "<city> is disbanded into Settler." */
		   _("%s is disbanded into %s."), 
		   city_name(pcity),
		   utype_name_translation(utype));

  remove_city(pcity);
  return TRUE;
}
