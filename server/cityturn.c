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

#include "city.h"
#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "player.h"
#include "rand.h"
#include "shared.h"
#include "support.h"
#include "tech.h"
#include "unit.h"

#include "citytools.h"
#include "gamelog.h"
#include "maphand.h"
#include "plrhand.h"
#include "sanitycheck.h"
#include "settlers.h"
#include "spacerace.h"
#include "srv_main.h"
#include "unittools.h"
#include "unithand.h"

#include "cm.h"

#include "advdomestic.h"
#include "aicity.h"
#include "aidata.h"
#include "ailog.h"
#include "aitools.h"

#include "cityturn.h"

static void check_pollution(struct city *pcity);
static void city_populate(struct city *pcity);

static bool worklist_change_build_target(struct player *pplayer,
					 struct city *pcity);

static bool city_distribute_surplus_shields(struct player *pplayer,
					    struct city *pcity);
static bool city_build_building(struct player *pplayer, struct city *pcity);
static bool city_build_unit(struct player *pplayer, struct city *pcity);
static bool city_build_stuff(struct player *pplayer, struct city *pcity);
static Impr_Type_id building_upgrades_to(struct city *pcity, Impr_Type_id b);
static void upgrade_building_prod(struct city *pcity);
static Unit_Type_id unit_upgrades_to(struct city *pcity, Unit_Type_id id);
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
   generic_city_refresh(pcity, TRUE, send_unit_info);
   /* AI would calculate this 1000 times otherwise; better to do it
      once -- Syela */
   pcity->ai.trade_want =
       TRADE_WEIGHTING - city_corruption(pcity, TRADE_WEIGHTING);
}

/**************************************************************************
...
called on government change or wonder completion or stuff like that -- Syela
**************************************************************************/
void global_city_refresh(struct player *pplayer)
{
  conn_list_do_buffer(&pplayer->connections);
  city_list_iterate(pplayer->cities, pcity)
    city_refresh(pcity);
    send_city_info(pplayer, pcity);
  city_list_iterate_end;
  conn_list_do_unbuffer(&pplayer->connections);
}

/**************************************************************************
...
**************************************************************************/
void remove_obsolete_buildings_city(struct city *pcity, bool refresh)
{
  struct player *pplayer = city_owner(pcity);
  bool sold = FALSE;

  built_impr_iterate(pcity, i) {
    if (!is_wonder(i) && improvement_obsolete(pplayer, i)) {
      do_sell_building(pplayer, pcity, i);
      notify_player_ex(pplayer, pcity->tile, E_IMP_SOLD, 
		       _("Game: %s is selling %s (obsolete) for %d."),
		       pcity->name, get_improvement_name(i), 
		       impr_sell_gold(i));
      sold = TRUE;
    }
  } built_impr_iterate_end;

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
void apply_cmresult_to_city(struct city *pcity, struct cm_result *cmr)
{
  /* The caller had better check this! */
  if (!cmr->found_a_valid) {
    freelog(LOG_ERROR, "apply_cmresult_to_city() called with non-valid "
            "cm_result");
    assert(0);
    return;
  }

  /* Now apply results */
  city_map_checked_iterate(pcity->tile, x, y, ptile) {
    if (pcity->city_map[x][y] == C_TILE_WORKER
        && !is_city_center(x, y)
        && !cmr->worker_positions_used[x][y]) {
      server_remove_worker_city(pcity, x, y);
    }
    if (pcity->city_map[x][y] != C_TILE_WORKER
        && !is_city_center(x, y)
        && cmr->worker_positions_used[x][y]) {
      server_set_worker_city(pcity, x, y);
    }
  } city_map_checked_iterate_end;
  specialist_type_iterate(sp) {
    pcity->specialists[sp] = cmr->specialists[sp];
  } specialist_type_iterate_end;
}

/**************************************************************************
  You need to call sync_cities so that the affected cities are synced with 
  the client.
**************************************************************************/
void auto_arrange_workers(struct city *pcity)
{
  struct cm_parameter cmp;
  struct cm_result cmr;
  struct player *pplayer = city_owner(pcity);

  /* See comment in freeze_workers(). */
  if (pcity->server.workers_frozen > 0) {
    pcity->server.needs_arrange = TRUE;
    return;
  }
  pcity->server.needs_arrange = FALSE;

  cm_init_parameter(&cmp);

  /* HACK: make sure everything is up-to-date before continuing.  This may
   * result in recursive calls to auto_arrange_workers, but it's better
   * to have these calls here than while we're reassigning workers (when
   * they will be fatal). */
  map_city_radius_iterate(pcity->tile, ptile) {
    update_city_tile_status_map(pcity, ptile);
  } map_city_radius_iterate_end;

  sanity_check_city(pcity);
  cm_clear_cache(pcity);

  cmp.require_happy = FALSE;
  cmp.allow_disorder = FALSE;
  cmp.allow_specialists = TRUE;

  /* We used to look at pplayer->ai.xxx_priority to determine the values
   * to be used here.  However that doesn't work at all because those values
   * are on a different scale.  Later the ai may wish to adjust its
   * priorities - this should be done via a separate set of variables. */
  if (pcity->size > 1) {
    if (pcity->size <= game.notradesize) {
      cmp.factor[FOOD] = 15;
    } else {
      cmp.factor[FOOD] = 10;
    }
  } else {
    /* Growing to size 2 is the highest priority. */
    cmp.factor[FOOD] = 20;
  }
  cmp.factor[SHIELD] = 5;
  cmp.factor[TRADE] = 0; /* Trade only provides gold/science. */
  cmp.factor[GOLD] = 2;
  cmp.factor[LUXURY] = 0; /* Luxury only influences happiness. */
  cmp.factor[SCIENCE] = 2;
  cmp.happy_factor = 0;

  cmp.minimal_surplus[FOOD] = 1;
  cmp.minimal_surplus[SHIELD] = 1;
  cmp.minimal_surplus[TRADE] = 0;
  cmp.minimal_surplus[GOLD] = -FC_INFINITY;
  cmp.minimal_surplus[LUXURY] = 0;
  cmp.minimal_surplus[SCIENCE] = 0;

  cm_query_result(pcity, &cmp, &cmr);

  if (!cmr.found_a_valid) {
    if (!pplayer->ai.control) {
      /* Drop surpluses and try again. */
      cmp.minimal_surplus[FOOD] = 0;
      cmp.minimal_surplus[SHIELD] = 0;
      cm_query_result(pcity, &cmp, &cmr);

      if (!cmr.found_a_valid) {
	/* Emergency management.  Get _some_ result.  This doesn't use
	 * cm_init_emergency_parameter so we can keep the factors from
	 * above. */
	cmp.minimal_surplus[FOOD] = MIN(cmp.minimal_surplus[FOOD],
					MIN(pcity->food_surplus, 0));
	cmp.minimal_surplus[SHIELD] = MIN(cmp.minimal_surplus[SHIELD],
					  MIN(pcity->shield_surplus, 0));
	cmp.require_happy = FALSE;
	cmp.allow_disorder = TRUE;
	cm_query_result(pcity, &cmp, &cmr);

	if (!cmr.found_a_valid) {
	  /* Should never happen. */
	  CITY_LOG(LOG_DEBUG, pcity, "emergency management");
	  cm_init_emergency_parameter(&cmp);
	  cm_query_result(pcity, &cmp, &cmr);
	}
      }
    } else {
      cmp.minimal_surplus[FOOD] = 0;
      cmp.minimal_surplus[SHIELD] = 0;
      cmp.minimal_surplus[GOLD] = -FC_INFINITY;
      cm_query_result(pcity, &cmp, &cmr);

      if (!cmr.found_a_valid) {
	/* Emergency management.  Get _some_ result.  This doesn't use
	 * cm_init_emergency_parameter so we can keep the factors from
	 * above. */
	cmp.minimal_surplus[FOOD] = MIN(cmp.minimal_surplus[FOOD],
					MIN(pcity->food_surplus, 0));
	cmp.minimal_surplus[SHIELD] = MIN(cmp.minimal_surplus[SHIELD],
					  MIN(pcity->shield_surplus, 0));
	cmp.require_happy = FALSE;
	cmp.require_happy = FALSE;
	cmp.allow_disorder = TRUE;
	cm_query_result(pcity, &cmp, &cmr);

	if (!cmr.found_a_valid) {
	  /* Should never happen. */
	  CITY_LOG(LOG_DEBUG, pcity, "emergency management");
	  cm_init_emergency_parameter(&cmp);
	  cm_query_result(pcity, &cmp, &cmr);
	}
      }
    }
  }
  assert(cmr.found_a_valid);

  apply_cmresult_to_city(pcity, &cmr);

  sanity_check_city(pcity);

  city_refresh(pcity);
}

/**************************************************************************
Notices about cities that should be sent to all players.
**************************************************************************/
void send_global_city_turn_notifications(struct conn_list *dest)
{
  if (!dest)
    dest = &game.all_connections;

  players_iterate(pplayer) {
    city_list_iterate(pplayer->cities, pcity) {
      /* can_player_build_improvement() checks whether wonder is build
	 elsewhere (or destroyed) */
      if (!pcity->is_building_unit && is_wonder(pcity->currently_building)
	  && (city_turns_to_build(pcity, pcity->currently_building, FALSE, TRUE)
	      <= 1)
	  && can_player_build_improvement(city_owner(pcity), pcity->currently_building)) {
	notify_conn_ex(dest, pcity->tile,
		       E_WONDER_WILL_BE_BUILT,
		       _("Game: Notice: Wonder %s in %s will be finished"
			 " next turn."), 
		       get_improvement_name(pcity->currently_building),
		       pcity->name);
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
 
  if (pcity->food_surplus > 0) {
    turns_growth = (city_granary_size(pcity->size) - pcity->food_stock - 1)
		   / pcity->food_surplus;

    if (get_city_bonus(pcity, EFT_GROWTH_FOOD) == 0
	&& get_current_construction_bonus(pcity, EFT_GROWTH_FOOD) > 0
	&& pcity->shield_surplus > 0) {
      turns_granary = (impr_build_shield_cost(pcity->currently_building)
		       - pcity->shield_stock) / pcity->shield_surplus;
      /* if growth and granary completion occur simultaneously, granary
	 preserves food.  -AJS */
      if (turns_growth < 5 && turns_granary < 5
	  && turns_growth < turns_granary) {
	notify_conn_ex(dest, pcity->tile,
			 E_CITY_GRAN_THROTTLE,
			 _("Game: Suggest throttling growth in %s to use %s "
			   "(being built) more effectively."), pcity->name,
			 improvement_types[pcity->currently_building].name);
      }
    }

    can_grow = city_can_grow_to(pcity, pcity->size + 1);

    if ((turns_growth <= 0) && !city_celebrating(pcity) && can_grow) {
      notify_conn_ex(dest, pcity->tile,
		       E_CITY_MAY_SOON_GROW,
		       _("Game: %s may soon grow to size %i."),
		       pcity->name, pcity->size + 1);
    }
  } else {
    if (pcity->food_stock + pcity->food_surplus <= 0 && pcity->food_surplus < 0) {
      notify_conn_ex(dest, pcity->tile,
		     E_CITY_FAMINE_FEARED,
		     _("Game: Warning: Famine feared in %s."),
		     pcity->name);
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
  pplayer->got_tech = FALSE;
  pplayer->research.bulbs_last_turn = 0;
  city_list_iterate(pplayer->cities, pcity)
     update_city_activity(pplayer, pcity);
  city_list_iterate_end;
  pplayer->ai.prev_gold = gold;
  /* This test include the cost of the units because pay_for_units is called
   * in update_city_activity */
  if (gold - (gold - pplayer->economic.gold) * 3 < 0) {
    notify_player_ex(pplayer, NULL, E_LOW_ON_FUNDS,
		     _("Game: WARNING, we're LOW on FUNDS sire."));  
  }
    /* uncomment to unbalance the game, like in civ1 (CLG)
      if (pplayer->got_tech && pplayer->research.researched > 0)    
        pplayer->research.researched=0;
    */
}

/**************************************************************************
  Reduce the city size.  Return TRUE if the city survives the population
  loss.
**************************************************************************/
bool city_reduce_size(struct city *pcity, int pop_loss)
{
  if (pop_loss == 0) {
    return TRUE;
  }

  if (pcity->size <= pop_loss) {
    remove_city(pcity);
    return FALSE;
  }
  pcity->size -= pop_loss;

  /* Cap the food stock at the new granary size. */
  if (pcity->food_stock > city_granary_size(pcity->size)) {
    pcity->food_stock = city_granary_size(pcity->size);
  }

  /* First try to kill off the specialists */
  while (pop_loss > 0 && city_specialists(pcity) > 0) {
    if (pcity->specialists[SP_TAXMAN] > 0) {
      pcity->specialists[SP_TAXMAN]--;
    } else if (pcity->specialists[SP_SCIENTIST] > 0) {
      pcity->specialists[SP_SCIENTIST]--;
    } else {
      assert(pcity->specialists[SP_ELVIS] > 0);
      pcity->specialists[SP_ELVIS]--; 
    }
    pop_loss--;
  }

  /* we consumed all the pop_loss in specialists */
  if (pop_loss == 0) {
    city_refresh(pcity);
    send_city_info(city_owner(pcity), pcity);
  } else {
    /* Take it out on workers */
    city_map_iterate(x, y) {
      if (get_worker_city(pcity, x, y) == C_TILE_WORKER
          && !is_city_center(x, y) && pop_loss > 0) {
        server_remove_worker_city(pcity, x, y);
        pop_loss--;
      }
    } city_map_iterate_end;
    /* Then rearrange workers */
    assert(pop_loss == 0);
    auto_arrange_workers(pcity);
    sync_cities();
  }
  sanity_check_city(pcity);
  return TRUE;
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
Note: We do not send info about the city to the clients as part of this function
**************************************************************************/
static void city_increase_size(struct city *pcity)
{
  struct player *powner = city_owner(pcity);
  bool have_square;
  int savings_pct = granary_savings(pcity), new_food;
  bool rapture_grow = city_rapture_grow(pcity); /* check before size increase! */

  if (!city_can_grow_to(pcity, pcity->size + 1)) { /* need improvement */
    if (get_current_construction_bonus(pcity, EFT_SIZE_ADJ) > 0
        || get_current_construction_bonus(pcity, EFT_SIZE_UNLIMIT) > 0) {
      notify_player_ex(powner, pcity->tile, E_CITY_AQ_BUILDING,
		       _("Game: %s needs %s (being built) "
			 "to grow any further."), pcity->name,
		       improvement_types[pcity->currently_building].name);
    } else {
      notify_player_ex(powner, pcity->tile, E_CITY_AQUEDUCT,
		       _("Game: %s needs an improvement to grow any further."),
		       pcity->name);
    }
    /* Granary can only hold so much */
    new_food = (city_granary_size(pcity->size)
		* (100 * 100 - game.aqueductloss * (100 - savings_pct))
		/ (100 * 100));
    pcity->food_stock = MIN(pcity->food_stock, new_food);
    return;
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
  have_square = FALSE;
  city_map_iterate(x, y) {
    if (can_place_worker_here(pcity, x, y)) {
      have_square = TRUE;
    }
  } city_map_iterate_end;
  if (((pcity->food_surplus >= 2) || !have_square)  &&  pcity->size >= 5  &&
      (is_city_option_set(pcity, CITYO_NEW_EINSTEIN) || 
       is_city_option_set(pcity, CITYO_NEW_TAXMAN))) {

    if (is_city_option_set(pcity, CITYO_NEW_EINSTEIN)) {
      pcity->specialists[SP_SCIENTIST]++;
    } else { /* now pcity->city_options & (1<<CITYO_NEW_TAXMAN) is true */
      pcity->specialists[SP_TAXMAN]++;
    }

  } else {
    pcity->specialists[SP_TAXMAN]++; /* or else city is !sane */
    auto_arrange_workers(pcity);
  }

  city_refresh(pcity);

  notify_player_ex(powner, pcity->tile, E_CITY_GROWTH,
                   _("Game: %s grows to size %d."), pcity->name, pcity->size);

  sanity_check_city(pcity);
  sync_cities();
}

/**************************************************************************
  Check whether the population can be increased or
  if the city is unable to support a 'settler'...
**************************************************************************/
static void city_populate(struct city *pcity)
{
  pcity->food_stock+=pcity->food_surplus;
  if(pcity->food_stock >= city_granary_size(pcity->size) 
     || city_rapture_grow(pcity)) {
    city_increase_size(pcity);
  }
  else if(pcity->food_stock<0) {
    /* FIXME: should this depend on units with ability to build
     * cities or on units that require food in uppkeep?
     * I'll assume citybuilders (units that 'contain' 1 pop) -- sjolie
     * The above may make more logical sense, but in game terms
     * you want to disband a unit that is draining your food
     * reserves.  Hence, I'll assume food upkeep > 0 units. -- jjm
     */
    unit_list_iterate_safe(pcity->units_supported, punit) {
      if (unit_type(punit)->food_cost > 0 
          && !unit_flag(punit, F_UNDISBANDABLE)) {

	notify_player_ex(city_owner(pcity), pcity->tile, E_UNIT_LOST,
			 _("Game: Famine feared in %s, %s lost!"), 
			 pcity->name, unit_type(punit)->name);
 
        gamelog(GAMELOG_UNITLOSS, punit, NULL, "famine");
        wipe_unit(punit);

	pcity->food_stock = (city_granary_size(pcity->size)
			     * granary_savings(pcity)) / 100;
	return;
      }
    } unit_list_iterate_safe_end;
    notify_player_ex(city_owner(pcity), pcity->tile, E_CITY_FAMINE,
		     _("Game: Famine causes population loss in %s."),
		     pcity->name);
    pcity->food_stock = (city_granary_size(pcity->size - 1)
			 * granary_savings(pcity)) / 100;
    city_reduce_size(pcity, 1);
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
  if (choice.choice >= 0 && choice.choice < B_LAST) {
    change_build_target(pplayer, pcity, choice.choice, FALSE, E_IMP_AUTO);
    return;
  }

  /* Build something random, undecided. */
  impr_type_iterate(i) {
    if (can_build_improvement(pcity, i)
	&& !building_has_effect(i, EFT_CAPITAL_CITY)) {
      change_build_target(pplayer, pcity, i, FALSE, E_IMP_AUTO);
      return;
    }
  } impr_type_iterate_end;
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
  bool success = FALSE;
  int i;

  if (worklist_is_empty(&pcity->worklist))
    /* Nothing in the worklist; bail now. */
    return FALSE;

  i = 0;
  while (TRUE) {
    int target;
    bool is_unit;

    /* What's the next item in the worklist? */
    if (!worklist_peek_ith(&pcity->worklist, &target, &is_unit, i))
      /* Nothing more in the worklist.  Ah, well. */
      break;

    i++;

    /* Sanity checks */
    if (is_unit &&
	!can_build_unit(pcity, target)) {
      int new_target = unit_upgrades_to(pcity, target);

      /* If the city can never build this unit or its descendants, drop it. */
      if (!can_eventually_build_unit(pcity, new_target)) {
	/* Nope, never in a million years. */
	notify_player_ex(pplayer, pcity->tile, E_CITY_CANTBUILD,
			 _("Game: %s can't build %s from the worklist.  "
			   "Purging..."),
			 pcity->name,
			 /* Yes, warn about the targets that's actually
			    in the worklist, not its obsolete-closure
			    new_target. */
			 get_unit_type(target)->name);
	/* Purge this worklist item. */
	worklist_remove(&pcity->worklist, i-1);
	/* Reset i to index to the now-next element. */
	i--;
	
	continue;
      }

      /* Maybe we can just upgrade the target to what the city /can/ build. */
      if (new_target == target) {
	/* Nope, we're stuck.  Dump this item from the worklist. */
	notify_player_ex(pplayer, pcity->tile, E_CITY_CANTBUILD,
			 _("Game: %s can't build %s from the worklist; "
			   "tech not yet available.  Postponing..."),
			 pcity->name,
			 get_unit_type(target)->name);
	continue;
      } else {
	/* Yep, we can go after new_target instead.  Joy! */
	notify_player_ex(pplayer, pcity->tile, E_WORKLIST,
			 _("Game: Production of %s is upgraded to %s in %s."),
			 get_unit_type(target)->name, 
			 get_unit_type(new_target)->name,
			 pcity->name);
	target = new_target;
      }
    } else if (!is_unit && !can_build_improvement(pcity, target)) {
      Impr_Type_id new_target = building_upgrades_to(pcity, target);

      /* If the city can never build this improvement, drop it. */
      if (!can_eventually_build_improvement(pcity, new_target)) {
	/* Nope, never in a million years. */
	notify_player_ex(pplayer, pcity->tile, E_CITY_CANTBUILD,
			 _("Game: %s can't build %s from the worklist.  "
			   "Purging..."),
			 pcity->name,
			 get_impr_name_ex(pcity, target));

	/* Purge this worklist item. */
	worklist_remove(&pcity->worklist, i-1);
	/* Reset i to index to the now-next element. */
	i--;
	
	continue;
      }


      /* Maybe this improvement has been obsoleted by something that
	 we can build. */
      if (new_target == target) {
	/* Nope, no use.  *sigh*  */
	if (!player_knows_improvement_tech(pplayer, target)) {
	  notify_player_ex(pplayer, pcity->tile, E_CITY_CANTBUILD,
			   _("Game: %s can't build %s from the worklist; "
			     "tech not yet available.  Postponing..."),
			   pcity->name,
			   get_impr_name_ex(pcity, target));
	} else if (improvement_types[target].bldg_req != B_LAST) {
	  notify_player_ex(pplayer, pcity->tile, E_CITY_CANTBUILD,
			   _("Game: %s can't build %s from the worklist; "
			     "need to have %s first.  Postponing..."),
			   pcity->name,
			   get_impr_name_ex(pcity, target),
			   get_impr_name_ex(pcity, improvement_types[target].bldg_req));
	} else {
	  /* This shouldn't happen...
	     FIXME: make can_build_improvement() return a reason enum. */
	  notify_player_ex(pplayer, pcity->tile, E_CITY_CANTBUILD,
			   _("Game: %s can't build %s from the worklist; "
			     "Reason unknown!  Postponing..."),
			   pcity->name,
			   get_impr_name_ex(pcity, target));
	}
	continue;
      } else {
	/* Hey, we can upgrade the improvement!  */
	notify_player_ex(pplayer, pcity->tile, E_WORKLIST,
			 _("Game: Production of %s is upgraded to %s in %s."),
			 get_impr_name_ex(pcity, target), 
			 get_impr_name_ex(pcity, new_target),
			 pcity->name);
	target = new_target;
      }
    }

    /* All okay.  Switch targets. */
    change_build_target(pplayer, pcity, target, is_unit, E_WORKLIST);

    success = TRUE;
    break;
  }

  if (success) {
    /* i is the index immediately _after_ the item we're changing to.
       Remove the (i-1)th item from the worklist. */
    worklist_remove(&pcity->worklist, i-1);
  }

  if (worklist_is_empty(&pcity->worklist)) {
    /* There *was* something in the worklist, but it's empty now.  Bug the
       player about it. */
    notify_player_ex(pplayer, pcity->tile, E_WORKLIST,
		     _("Game: %s's worklist is now empty."),
		     pcity->name);
  }

  return success;
}

/**************************************************************************
  Follow the list of replaced_by buildings until we hit something that
  we can build.  Returns -1 if we can't upgrade at all (including if the
  original building is unbuildable).
**************************************************************************/
static Impr_Type_id building_upgrades_to(struct city *pcity, Impr_Type_id id)
{
  Impr_Type_id check = id, latest_ok = id;

  if (!can_build_improvement_direct(pcity, check)) {
    return -1;
  }
  while (improvement_exists(check = improvement_types[check].replaced_by)) {
    if (can_build_improvement_direct(pcity, check)) {
      latest_ok = check;
    }
  }
  if (latest_ok == id) {
    return -1; /* Can't upgrade */
  }

  return latest_ok;
}

/**************************************************************************
  Try to upgrade production in pcity.
**************************************************************************/
static void upgrade_building_prod(struct city *pcity)
{
  struct player *pplayer = city_owner(pcity);
  Impr_Type_id upgrades_to = building_upgrades_to(pcity,
						  pcity->currently_building);

  if (can_build_improvement(pcity, upgrades_to)) {
    notify_player_ex(pplayer, pcity->tile, E_UNIT_UPGRADED,
		     _("Game: Production of %s is upgraded to %s in %s."),
		     get_improvement_type(pcity->currently_building)->name,
		     get_improvement_type(upgrades_to)->name,
		     pcity->name);
    pcity->currently_building = upgrades_to;
  }
}

/**************************************************************************
  Follow the list of obsoleted_by units until we hit something that
  we can build.  Return id if we can't upgrade at all.  NB:  returning
  id doesn't guarantee that pcity really _can_ build id; just that
  pcity can't build whatever _obsoletes_ id.
**************************************************************************/
static Unit_Type_id unit_upgrades_to(struct city *pcity, Unit_Type_id id)
{
  Unit_Type_id check = id, latest_ok = id;

  if (!can_build_unit_direct(pcity, check)) {
    return -1;
  }
  while(unit_type_exists(check = unit_types[check].obsoleted_by)) {
    if (can_build_unit_direct(pcity, check)) {
      latest_ok = check;
    }
  }
  if (latest_ok == id) {
    return -1; /* Can't upgrade */
  }

  return latest_ok;
}

/**************************************************************************
  Try to upgrade production in pcity.
**************************************************************************/
static void upgrade_unit_prod(struct city *pcity)
{
  struct player *pplayer = city_owner(pcity);
  int id = pcity->currently_building;
  int id2 = unit_upgrades_to(pcity, pcity->currently_building);

  if (can_build_unit_direct(pcity, id2)) {
    pcity->currently_building = id2;
    notify_player_ex(pplayer, pcity->tile, E_UNIT_UPGRADED, 
		  _("Game: Production of %s is upgraded to %s in %s."),
		  get_unit_type(id)->name, 
		  get_unit_type(id2)->name , 
		  pcity->name);
  }
}

/**************************************************************************
  Disband units if we don't have enough shields to support them.  Returns
  FALSE if the _city_ is disbanded as a result.
**************************************************************************/
static bool city_distribute_surplus_shields(struct player *pplayer,
					    struct city *pcity)
{
  struct government *g = get_gov_pplayer(pplayer);

  if (pcity->shield_surplus < 0) {
    unit_list_iterate_safe(pcity->units_supported, punit) {
      if (utype_shield_cost(unit_type(punit), g) > 0
	  && pcity->shield_surplus < 0
          && !unit_flag(punit, F_UNDISBANDABLE)) {
	notify_player_ex(pplayer, pcity->tile, E_UNIT_LOST,
			 _("Game: %s can't upkeep %s, unit disbanded."),
			 pcity->name, unit_type(punit)->name);
        handle_unit_disband(pplayer, punit->id);
	/* pcity->shield_surplus is automatically updated. */
      }
    } unit_list_iterate_safe_end;
  }

  if (pcity->shield_surplus < 0) {
    /* Special case: F_UNDISBANDABLE. This nasty unit won't go so easily.
     * It'd rather make the citizens pay in blood for their failure to upkeep
     * it! If we make it here all normal units are already disbanded, so only
     * undisbandable ones remain. */
    unit_list_iterate_safe(pcity->units_supported, punit) {
      int upkeep = utype_shield_cost(unit_type(punit), g);

      if (upkeep > 0 && pcity->shield_surplus < 0) {
	assert(unit_flag(punit, F_UNDISBANDABLE));
	notify_player_ex(pplayer, pcity->tile, E_UNIT_LOST,
			 _("Game: Citizens in %s perish for their failure to "
			 "upkeep %s!"), pcity->name, unit_type(punit)->name);
	if (!city_reduce_size(pcity, 1)) {
	  return FALSE;
	}

	/* No upkeep for the unit this turn. */
	pcity->shield_surplus += upkeep;
      }
    } unit_list_iterate_safe_end;
  }

  /* Now we confirm changes made last turn. */
  pcity->shield_stock += pcity->shield_surplus;
  pcity->before_change_shields = pcity->shield_stock;
  pcity->last_turns_shield_surplus = pcity->shield_surplus;

  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
static bool city_build_building(struct player *pplayer, struct city *pcity)
{
  bool space_part;
  int mod;

  if (get_current_construction_bonus(pcity, EFT_PROD_TO_GOLD) > 0) {
    assert(pcity->shield_surplus >= 0);
    /* pcity->before_change_shields already contains the surplus from
     * this turn. */
    pplayer->economic.gold += pcity->before_change_shields;
    pcity->before_change_shields = 0;
    pcity->shield_stock = 0;
  }
  upgrade_building_prod(pcity);
  if (!can_build_improvement(pcity, pcity->currently_building)) {
    notify_player_ex(pplayer, pcity->tile, E_CITY_CANTBUILD,
		     _("Game: %s is building %s, which "
		       "is no longer available."),
		     pcity->name, get_impr_name_ex(pcity,
						   pcity->
						   currently_building));
    return TRUE;
  }
  if (pcity->shield_stock
      >= impr_build_shield_cost(pcity->currently_building)) {
    if (pcity->currently_building == game.palace_building) {
      city_list_iterate(pplayer->cities, palace) {
	if (city_got_building(palace, game.palace_building)) {
	  city_remove_improvement(palace, game.palace_building);
	  break;
	}
      } city_list_iterate_end;
    }

    space_part = TRUE;
    if (get_current_construction_bonus(pcity, EFT_SS_STRUCTURAL) > 0) {
      pplayer->spaceship.structurals++;
    } else if (get_current_construction_bonus(pcity, EFT_SS_COMPONENT) > 0) {
      pplayer->spaceship.components++;
    } else if (get_current_construction_bonus(pcity, EFT_SS_MODULE) > 0) {
      pplayer->spaceship.modules++;
    } else {
      space_part = FALSE;
      city_add_improvement(pcity, pcity->currently_building);
    }
    pcity->before_change_shields -=
	impr_build_shield_cost(pcity->currently_building);
    pcity->shield_stock -= impr_build_shield_cost(pcity->currently_building);
    pcity->turn_last_built = game.turn;
    /* to eliminate micromanagement */
    if (is_wonder(pcity->currently_building)) {
      game.global_wonders[pcity->currently_building] = pcity->id;
      notify_player_ex(NULL, pcity->tile, E_WONDER_BUILD,
		       _("Game: The %s have finished building %s in %s."),
		       get_nation_name_plural(pplayer->nation),
		       get_impr_name_ex(pcity, pcity->currently_building),
		       pcity->name);
      /* TODO: if wonders become just-another-building, remove this */
      gamelog(GAMELOG_WONDER, pcity);
    } else {
      gamelog(GAMELOG_BUILD, pcity);
    }

    notify_player_ex(pplayer, pcity->tile, E_IMP_BUILD,
		     _("Game: %s has finished building %s."), pcity->name,
		     improvement_types[pcity->currently_building].name);


    if ((mod = get_current_construction_bonus(pcity, EFT_GIVE_IMM_TECH))) {
      int i;

      notify_player(pplayer, PL_("Game: %s boosts research; "
			         "you gain %d immediate advance.",
				 "Game: %s boosts research; "
			         "you gain %d immediate advances.",
				 mod),
		    improvement_types[pcity->currently_building].name, mod);

      for (i = 0; i < mod; i++) {
	Tech_Type_id tech = pplayer->research.researching;

	if (tech == A_UNSET) {
	  choose_random_tech(pplayer);
	  tech = pplayer->research.researching;
	}
	do_free_cost(pplayer);
	found_new_tech(pplayer, pplayer->research.researching, TRUE, TRUE, 
		       A_NONE);

	notify_embassies(pplayer, NULL,
	    _("Game: The %s have acquired %s from %s."),
	    get_nation_name_plural(pplayer->nation),
	    get_tech_name(pplayer, tech),
	    improvement_types[pcity->currently_building].name);
      }
    }
    if (space_part && pplayer->spaceship.state == SSHIP_NONE) {
      notify_player_ex(NULL, pcity->tile, E_SPACESHIP,
		       _("Game: The %s have started "
			 "building a spaceship!"),
		       get_nation_name_plural(pplayer->nation));
      pplayer->spaceship.state = SSHIP_STARTED;
    }
    if (space_part) {
      send_spaceship_info(pplayer, NULL);
    } else {
      city_refresh(pcity);
    }
    /* If there's something in the worklist, change the build target.
     * Else if just built a spaceship part, keep building the same
     * part.  (Fixme? - doesn't check whether spaceship part is still
     * sensible.)  Else co-opt AI routines as "city advisor".
     */
    if (!worklist_change_build_target(pplayer, pcity) && !space_part) {
      /* Fall back to the good old ways. */
      freelog(LOG_DEBUG, "Trying advisor_choose_build.");
      advisor_choose_build(pplayer, pcity);
      freelog(LOG_DEBUG, "Advisor_choose_build didn't kill us.");
    }
  }

  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
static bool city_build_unit(struct player *pplayer, struct city *pcity)
{
  upgrade_unit_prod(pcity);

  /* We must make a special case for barbarians here, because they are
     so dumb. Really. They don't know the prerequisite techs for units
     they build!! - Per */
  if (!can_build_unit_direct(pcity, pcity->currently_building)
      && !is_barbarian(pplayer)) {
    notify_player_ex(pplayer, pcity->tile, E_CITY_CANTBUILD,
        _("Game: %s is building %s, which is no longer available."),
        pcity->name, unit_name(pcity->currently_building));
    freelog(LOG_VERBOSE, _("%s's %s tried build %s, which is not available"),
            pplayer->name, pcity->name, unit_name(pcity->currently_building));            
    return TRUE;
  }
  if (pcity->shield_stock
      >= unit_build_shield_cost(pcity->currently_building)) {
    int pop_cost = unit_pop_value(pcity->currently_building);

    /* Should we disband the city? -- Massimo */
    if (pcity->size == pop_cost
	&& is_city_option_set(pcity, CITYO_DISBAND)) {
      return !disband_city(pcity);
    }

    if (pcity->size <= pop_cost) {
      notify_player_ex(pplayer, pcity->tile, E_CITY_CANTBUILD,
		       _("Game: %s can't build %s yet."),
		       pcity->name, unit_name(pcity->currently_building));
      return TRUE;
    }

    assert(pop_cost == 0 || pcity->size >= pop_cost);

    /* don't update turn_last_built if we returned above */
    pcity->turn_last_built = game.turn;

    (void) create_unit(pplayer, pcity->tile, pcity->currently_building,
		       do_make_unit_veteran(pcity, pcity->currently_building),
		       pcity->id, -1);

    /* After we created the unit remove the citizen. This will also
       rearrange the worker to take into account the extra resources
       (food) needed. */
    if (pop_cost > 0) {
      city_reduce_size(pcity, pop_cost);
    }

    /* to eliminate micromanagement, we only subtract the unit's
       cost */
    pcity->before_change_shields
      -= unit_build_shield_cost(pcity->currently_building);
    pcity->shield_stock
      -= unit_build_shield_cost(pcity->currently_building);

    notify_player_ex(pplayer, pcity->tile, E_UNIT_BUILT,
		     /* TRANS: <city> is finished building <unit/building>. */
		     _("Game: %s is finished building %s."),
		     pcity->name,
		     unit_types[pcity->currently_building].name);

    gamelog(GAMELOG_BUILD, pcity);

    /* If there's something in the worklist, change the build
       target. If there's nothing there, worklist_change_build_target
       won't do anything, unless the unit built is unique. */
    if (!worklist_change_build_target(pplayer, pcity) 
        && unit_type_flag(pcity->currently_building, F_UNIQUE)) {
      advisor_choose_build(pplayer, pcity);
    }
  }
  return TRUE;
}

/**************************************************************************
return 0 if the city is removed
return 1 otherwise
**************************************************************************/
static bool city_build_stuff(struct player *pplayer, struct city *pcity)
{
  if (!city_distribute_surplus_shields(pplayer, pcity)) {
    return FALSE;
  }

  nullify_caravan_and_disband_plus(pcity);
  define_orig_production_values(pcity);

  if (!pcity->is_building_unit) {
    return city_build_building(pplayer, pcity);
  } else {
    return city_build_unit(pplayer, pcity);
  }
}

/**************************************************************************
...
**************************************************************************/
static void pay_for_buildings(struct player *pplayer, struct city *pcity)
{
  built_impr_iterate(pcity, i) {
    if (!is_wonder(i)
	&& pplayer->government != game.government_when_anarchy) {
      if (pplayer->economic.gold - improvement_upkeep(pcity, i) < 0) {
	notify_player_ex(pplayer, pcity->tile, E_IMP_AUCTIONED,
			 _("Game: Can't afford to maintain %s in %s, "
			   "building sold!"),
			 improvement_types[i].name, pcity->name);
	do_sell_building(pplayer, pcity, i);
	city_refresh(pcity);
      } else
	pplayer->economic.gold -= improvement_upkeep(pcity, i);
    }
  } built_impr_iterate_end;
}

/**************************************************************************
 Add some Pollution if we have waste
**************************************************************************/
static void check_pollution(struct city *pcity)
{
  int k=100;
  if (pcity->pollution != 0 && myrand(100) <= pcity->pollution) {
    while (k > 0) {
      /* place pollution somewhere in city radius */
      int cx = myrand(CITY_MAP_SIZE);
      int cy = myrand(CITY_MAP_SIZE);
      struct tile *ptile;

      /* if is a corner tile or not a real map position */
      if (!is_valid_city_coords(cx, cy)
	  || !(ptile = city_map_to_map(pcity, cx, cy))) {
	continue;
      }

      if (!terrain_has_flag(map_get_terrain(ptile), TER_NO_POLLUTION)
	  && !map_has_special(ptile, S_POLLUTION)) {
	map_set_special(ptile, S_POLLUTION);
	update_tile_knowledge(ptile);
	notify_player_ex(city_owner(pcity), pcity->tile,
			 E_POLLUTION, _("Game: Pollution near %s."),
			 pcity->name);
	return;
      }
      k--;
    }
    freelog(LOG_DEBUG, "pollution not placed: city: %s", pcity->name);
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
  struct government *g = get_gov_pcity(pcity);
  struct city *capital;
  int dist, size, cost;

  if (government_has_flag(get_gov_pcity(pcity), G_UNBRIBABLE)) {
    return INCITE_IMPOSSIBLE_COST;
  }
  if (get_city_bonus(pcity, EFT_NO_INCITE) > 0) {
    return INCITE_IMPOSSIBLE_COST;
  }

  /* Gold factor */
  cost = city_owner(pcity)->economic.gold + 1000;

  unit_list_iterate(pcity->tile->units, punit) {
    cost += (unit_build_shield_cost(punit->type)
	     * game.incite_cost.unit_factor);
  } unit_list_iterate_end;

  /* Buildings */
  built_impr_iterate(pcity, i) {
    cost += impr_build_shield_cost(i) * game.incite_cost.improvement_factor;
  } built_impr_iterate_end;

  /* Stability bonuses */
  if (g->index != game.government_when_anarchy) {
    if (!city_unhappy(pcity)) {
      cost *= 2;
    }
    if (city_celebrating(pcity)) {
      cost *= 2;
    }
  }

  /* City is empty */
  if (unit_list_size(&pcity->tile->units) == 0) {
    cost /= 2;
  }

  /* Buy back is cheap, conquered cities are also cheap */
  if (pcity->owner != pcity->original) {
    if (pplayer->player_no == pcity->original) {
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
  dist -= (dist * get_city_bonus(pcity, EFT_INCITE_DIST_PCT)) / 100;
  if (g->fixed_corruption_distance != 0) {
    dist = MIN(g->fixed_corruption_distance, dist);
  }

  size = MAX(1, pcity->size
                + pcity->ppl_happy[4]
                - pcity->ppl_unhappy[4]
                - pcity->ppl_angry[4] * 3);
  cost *= size;
  cost *= game.incite_cost.total_factor;
  cost = cost / (dist + 3);
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
  pcity->changed_from_id = pcity->currently_building;
  pcity->changed_from_is_unit = pcity->is_building_unit;

  freelog(LOG_DEBUG,
	  "In %s, building %s.  Beg of Turn shields = %d",
	  pcity->name,
	  pcity->changed_from_is_unit ?
	    unit_types[pcity->changed_from_id].name :
	    improvement_types[pcity->changed_from_id].name,
	  pcity->before_change_shields
	  );
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
  struct government *g = get_gov_pcity(pcity);

  city_refresh(pcity);

  /* reporting of celebrations rewritten, copying the treatment of disorder below,
     with the added rapture rounds count.  991219 -- Jing */
  if (city_build_stuff(pplayer, pcity)) {
    if (city_celebrating(pcity)) {
      pcity->rapture++;
      if (pcity->rapture == 1)
	notify_player_ex(pplayer, pcity->tile, E_CITY_LOVE,
			 _("Game: We Love The %s Day celebrated in %s."), 
			 get_ruler_title(pplayer->government, pplayer->is_male,
					 pplayer->nation),
			 pcity->name);
    }
    else {
      if (pcity->rapture != 0)
	notify_player_ex(pplayer, pcity->tile, E_CITY_NORMAL,
			 _("Game: We Love The %s Day canceled in %s."),
			 get_ruler_title(pplayer->government, pplayer->is_male,
					 pplayer->nation),
			 pcity->name);
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
    pcity->airlift = (get_city_bonus(pcity, EFT_AIRLIFT) > 0);
    update_tech(pplayer, pcity->science_total);
    pplayer->economic.gold+=pcity->tax_total;
    pay_for_units(pplayer, pcity);
    pay_for_buildings(pplayer, pcity);

    if(city_unhappy(pcity)) { 
      pcity->anarchy++;
      if (pcity->anarchy == 1) 
        notify_player_ex(pplayer, pcity->tile, E_CITY_DISORDER,
	  	         _("Game: Civil disorder in %s."), pcity->name);
      else
        notify_player_ex(pplayer, pcity->tile, E_CITY_DISORDER,
		         _("Game: CIVIL DISORDER CONTINUES in %s."),
			 pcity->name);
    }
    else {
      if (pcity->anarchy != 0)
        notify_player_ex(pplayer, pcity->tile, E_CITY_NORMAL,
	  	         _("Game: Order restored in %s."), pcity->name);
      pcity->anarchy=0;
    }
    check_pollution(pcity);

    send_city_info(NULL, pcity);
    if (pcity->anarchy>2 && government_has_flag(g, G_REVOLUTION_WHEN_UNHAPPY)) {
      notify_player_ex(pplayer, pcity->tile, E_ANARCHY,
		       _("Game: The people have overthrown your %s, "
			 "your country is in turmoil."),
		       get_government_name(g->index));
      handle_player_change_government(pplayer, g->index);
    }
    sanity_check_city(pcity);
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

  /* find closest city other than pcity */
  rcity = find_closest_owned_city(pplayer, ptile, FALSE, pcity);

  if (!rcity) {
    /* What should we do when we try to disband our only city? */
    notify_player_ex(pplayer, ptile, E_CITY_CANTBUILD,
		     _("Game: %s can't build %s yet, "
		     "and we can't disband our only city."),
		     pcity->name, unit_name(pcity->currently_building));
    return FALSE;
  }

  (void) create_unit(pplayer, ptile, pcity->currently_building,
		     do_make_unit_veteran(pcity, pcity->currently_building),
		     pcity->id, -1);

  /* Shift all the units supported by pcity (including the new unit)
   * to rcity.  transfer_city_units does not make sure no units are
   * left floating without a transport, but since all units are
   * transferred this is not a problem. */
  transfer_city_units(pplayer, pplayer, &pcity->units_supported, rcity, pcity, -1, TRUE);

  notify_player_ex(pplayer, ptile, E_UNIT_BUILT,
		   /* TRANS: Settler production leads to disbanded city. */
		   _("Game: %s is disbanded into %s."), 
		   pcity->name, unit_types[pcity->currently_building].name);
  gamelog(GAMELOG_DISBANDCITY, pcity);

  remove_city(pcity);
  return TRUE;
}
