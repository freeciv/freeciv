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

#include "city.h"
#include "combat.h"
#include "game.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "shared.h"
#include "unit.h"

#include "citymap.h"
#include "path_finding.h"
#include "pf_tools.h"

#include "airgoto.h"
#include "barbarian.h"
#include "citytools.h"
#include "cityturn.h"
#include "gotohand.h"
#include "maphand.h"
#include "plrhand.h"
#include "settlers.h"
#include "unithand.h"
#include "unittools.h"

#include "advmilitary.h"
#include "aicity.h"
#include "aidata.h"
#include "aiferry.h"
#include "ailog.h"
#include "aiunit.h"

#include "aitools.h"

/**************************************************************************
  Amortize a want modified by the shields (build_cost) we risk losing.
  We add the build time of the unit(s) we risk to amortize delay.  The
  build time is claculated as the build cost divided by the production
  output of the unit's homecity or the city where we want to produce
  the unit. If the city has less than average shield output, we
  instead use the average, to encourage long-term thinking.
**************************************************************************/
int military_amortize(struct player *pplayer, struct city *pcity,
                      int value, int delay, int build_cost)
{
  struct ai_data *ai = ai_data_get(pplayer);
  int city_output = (pcity ? pcity->shield_surplus : 1);
  int output = MAX(city_output, ai->stats.average_production);
  int build_time = build_cost / MAX(output, 1);

  if (value <= 0) {
    return 0;
  }

  return amortize(value, delay + build_time);
}

/**********************************************************************
  There are some signs that a player might be dangerous: We are at 
  war with him, he has lousy reputation, he has done lots of ignoble 
  things to us, he is an ally of one of our enemies (a ticking bomb
  to be sure), or he is our war target.
***********************************************************************/
bool is_player_dangerous(struct player *pplayer, struct player *aplayer)
{
  struct ai_data *ai = ai_data_get(pplayer);
  struct ai_dip_intel *adip 
    = &ai->diplomacy.player_intel[aplayer->player_no];

  return (pplayers_at_war(pplayer, aplayer)
          || ai->diplomacy.target == aplayer
          || pplayer->diplstates[aplayer->player_no].has_reason_to_cancel != 0
          || ai->diplomacy.acceptable_reputation > aplayer->reputation
          || adip->is_allied_with_enemy);
}

/*************************************************************************
  This is a function to execute paths returned by the path-finding engine.

  Brings our bodyguard along.
  Returns FALSE only if died.
*************************************************************************/
bool ai_unit_execute_path(struct unit *punit, struct pf_path *path)
{
  int i;

  /* We start with i = 1 for i = 0 is our present position */
  for (i = 1; i < path->length; i++) {
    int x = path->positions[i].x, y = path->positions[i].y;
    int id = punit->id;

    if (same_pos(punit->x, punit->y, x, y)) {
      UNIT_LOG(LOG_DEBUG, punit, "execute_path: waiting this turn");
      return TRUE;
    }

    /* We use ai_unit_move() for everything but the last step
     * of the way so that we abort if unexpected opposition
     * shows up. Any enemy on the target tile is expected to
     * be our target and any attack there intentional. */
    if (i == path->length - 1) {
      (void) ai_unit_attack(punit, x, y);
    } else {
      (void) ai_unit_move(punit, x, y);
    }
    if (!find_unit_by_id(id)) {
      /* Died... */
      return FALSE;
    }

    if (!same_pos(punit->x, punit->y, x, y)) {
      /* Stopped (or maybe fought) */
      return TRUE;
    }
  }

  return TRUE;
}

/****************************************************************************
  A helper function for ai_gothere.  Estimates the dangers we will
  be facing at our destination and tries to find/request a bodyguard if 
  needed.
****************************************************************************/
static void ai_gothere_bodyguard(struct unit *punit, int dest_x, int dest_y)
{
  struct player *pplayer = unit_owner(punit);
  struct ai_data *ai = ai_data_get(pplayer);
  unsigned int danger = 0;
  struct city *dcity;
  struct tile *ptile;
  
  if (is_barbarian(unit_owner(punit))) {
    /* barbarians must have more courage (ie less brains) */
    punit->ai.bodyguard = BODYGUARD_NONE;
    return;
  }

  /* Estimate enemy attack power. */
  unit_list_iterate(map_get_tile(dest_x, dest_y)->units, aunit) {
    if (HOSTILE_PLAYER(pplayer, ai, unit_owner(aunit))) {
      danger += unit_att_rating(aunit);
    }
  } unit_list_iterate_end;
  dcity = map_get_city(dest_x, dest_y);
  if (dcity && HOSTILE_PLAYER(pplayer, ai, city_owner(dcity))) {
    /* Assume enemy will build another defender, add it's attack strength */
    int d_type = ai_choose_defender_versus(dcity, punit->type);
    danger += 
      unittype_att_rating(d_type, do_make_unit_veteran(dcity, d_type), 
                          SINGLE_MOVE, unit_types[d_type].hp);
  }
  danger *= POWER_DIVIDER;

  /* If we are fast, there is less danger. */
  danger /= (unit_type(punit)->move_rate / SINGLE_MOVE);
  if (unit_flag(punit, F_IGTER)) {
    danger /= 1.5;
  }

  ptile = map_get_tile(punit->x, punit->y);
  /* We look for the bodyguard where we stand. */
  if (!unit_list_find(&ptile->units, punit->ai.bodyguard)) {
    int my_def = (punit->hp 
                  * unit_type(punit)->veteran[punit->veteran].power_fact
		  * unit_type(punit)->defense_strength
                  * POWER_FACTOR);
    
    if (danger >= my_def) {
      UNIT_LOG(LOGLEVEL_BODYGUARD, punit, 
               "want bodyguard @(%d, %d) danger=%d, my_def=%d", 
               dest_x, dest_y, danger, my_def);
      punit->ai.bodyguard = BODYGUARD_WANTED;
    } else {
      punit->ai.bodyguard = BODYGUARD_NONE;
    }
  }

  /* What if we have a bodyguard, but don't need one? */
}

#define LOGLEVEL_GOTHERE LOG_DEBUG
/****************************************************************************
  This is ferry-enabled goto.  Should not normally be used for non-ferried 
  units (i.e. planes or ships), use ai_unit_goto instead.

  Return values: TRUE if got to or next to our destination, FALSE otherwise. 

  TODO: A big one is rendezvous points.  When this is implemented, we won't
  have to be at the coast to ask for a boat to come to us.

  You MUST have warmap created before calling this function in order for 
  find_beachhead to work here. This requirement should be removed.
****************************************************************************/
bool ai_gothere(struct player *pplayer, struct unit *punit,
                int dest_x, int dest_y)
{
  CHECK_UNIT(punit);

  if (same_pos(dest_x, dest_y, punit->x, punit->y)) {
    /* Nowhere to go */
    return TRUE;
  }

  /* See if we need a bodyguard at our destination */
  /* FIXME: If bodyguard is _really_ necessary, don't go anywhere */
  ai_gothere_bodyguard(punit, dest_x, dest_y);

  if (punit->transported_by > 0 
      || !goto_is_sane(punit, dest_x, dest_y, TRUE)) {
    /* Must go by boat, call an aiferryboat function */
    if (!aiferry_gobyboat(pplayer, punit, dest_x, dest_y)) {
      return FALSE;
    }
  }

  /* Go where we should be going if we can, and are at our destination 
   * if we are on a ferry */
  if (goto_is_sane(punit, dest_x, dest_y, TRUE) && punit->moves_left > 0) {
    set_goto_dest(punit, dest_x, dest_y);
    UNIT_LOG(LOGLEVEL_GOTHERE, punit, "Walking to (%d,%d)", dest_x, dest_y);
    if (!ai_unit_goto(punit, dest_x, dest_y)) {
      /* died */
      return FALSE;
    }
    /* liable to bump into someone that will kill us.  Should avoid? */
  } else {
    UNIT_LOG(LOGLEVEL_GOTHERE, punit, "Not moving");
    return FALSE;
  }

  if (punit->ai.ferryboat > 0 && punit->transported_by <= 0) {
    /* We probably just landed, release our boat */
    aiferry_clear_boat(punit);
  }
  
  /* Dead unit shouldn't reach this point */
  CHECK_UNIT(punit);
  
  return (same_pos(punit->x, punit->y, dest_x, dest_y) 
          || is_tiles_adjacent(punit->x, punit->y, dest_x, dest_y));
}

/**************************************************************************
  Go to specified destination but do not disturb existing role or activity
  and do not clear the role's destination. Return FALSE iff we died.

  FIXME: add some logging functionality to replace GOTO_LOG()
**************************************************************************/
bool ai_unit_goto(struct unit *punit, int x, int y)
{
  enum goto_result result;
  int oldx = -1, oldy = -1;
  enum unit_activity activity = punit->activity;
  bool is_set = is_goto_dest_set(punit);

  if (is_set) {
    oldx = goto_dest_x(punit);
    oldy = goto_dest_y(punit);
  }

  CHECK_UNIT(punit);
  /* TODO: log error on same_pos with punit->x|y */
  set_goto_dest(punit, x, y);
  handle_unit_activity_request(punit, ACTIVITY_GOTO);
  result = do_unit_goto(punit, GOTO_MOVE_ANY, FALSE);
  if (result != GR_DIED) {
    handle_unit_activity_request(punit, activity);
    if (is_set) {
      set_goto_dest(punit, oldx, oldy);
    } else {
      clear_goto_dest(punit);
    }
    return TRUE;
  }
  return FALSE;
}

/**************************************************************************
  Ensure unit sanity by telling charge that we won't bodyguard it anymore,
  tell bodyguard it can roam free if our job is done, add and remove city 
  spot reservation, and set destination. If we set a unit to hunter, also
  reserve its target, and try to load it with cruise missiles or nukes
  to bring along.
**************************************************************************/
void ai_unit_new_role(struct unit *punit, enum ai_unit_task task, int x, int y)
{
  struct unit *charge = find_unit_by_id(punit->ai.charge);
  struct unit *bodyguard = find_unit_by_id(punit->ai.bodyguard);

  /* If the unit is under (human) orders we shouldn't control it. */
  assert(!unit_has_orders(punit));

  UNIT_LOG(LOG_DEBUG, punit, "changing role from %d to %d",
           punit->activity, task);

  /* Free our ferry.  Most likely it has been done already. */
  if (task == AIUNIT_NONE || task == AIUNIT_DEFEND_HOME) {
    aiferry_clear_boat(punit);
  }

  if (punit->activity == ACTIVITY_GOTO) {
    /* It would indicate we're going somewhere otherwise */
    handle_unit_activity_request(punit, ACTIVITY_IDLE);
  }

  if (punit->ai.ai_role == AIUNIT_BUILD_CITY) {
    assert(is_normal_map_pos(goto_dest_x(punit), goto_dest_y(punit)));
    citymap_free_city_spot(goto_dest_x(punit), goto_dest_y(punit), punit->id);
  }

  if (punit->ai.ai_role == AIUNIT_HUNTER) {
    /* Clear victim's hunted bit - we're no longer chasing. */
    struct unit *target = find_unit_by_id(punit->ai.target);

    if (target) {
      target->ai.hunted &= ~(1 << unit_owner(punit)->player_no);
      UNIT_LOG(LOGLEVEL_HUNT, target, "no longer hunted (new role %d, old %d)",
               task, punit->ai.ai_role);
    }
  }

  if (charge && (charge->ai.bodyguard == punit->id)) {
    /* ensure we don't let the unit believe we bodyguard it */
    charge->ai.bodyguard = BODYGUARD_NONE;
  }
  punit->ai.charge = BODYGUARD_NONE;

  punit->ai.ai_role = task;

  /* Verify and set the goto destination.  Eventually this can be a lot more
   * stringent, but for now we don't want to break things too badly. */
  if (x == -1 && y == -1) {
    /* No goto_dest. */
    clear_goto_dest(punit);
  } else {
    set_goto_dest(punit, x, y);
  }

  if (punit->ai.ai_role == AIUNIT_NONE && bodyguard) {
    ai_unit_new_role(bodyguard, AIUNIT_NONE, -1, -1);
  }

  /* Reserve city spot, _unless_ we want to add ourselves to a city. */
  if (punit->ai.ai_role == AIUNIT_BUILD_CITY && !map_get_city(x, y)) {
    assert(is_normal_map_pos(x, y));
    citymap_reserve_city_spot(x, y, punit->id);
  }
  if (punit->ai.ai_role == AIUNIT_HUNTER) {
    /* Set victim's hunted bit - the hunt is on! */
    struct unit *target = find_unit_by_id(punit->ai.target);

    assert(target != NULL);
    target->ai.hunted |= (1 << unit_owner(punit)->player_no);
    UNIT_LOG(LOGLEVEL_HUNT, target, "is being hunted");

    /* Grab missiles lying around and bring them along */
    if (unit_flag(punit, F_MISSILE_CARRIER)
        || unit_flag(punit, F_CARRIER)) {
      unit_list_iterate(map_get_tile(punit->x, punit->y)->units, missile) {
        if (missile->ai.ai_role != AIUNIT_ESCORT
            && missile->transported_by == -1
            && missile->owner == punit->owner
            && unit_flag(missile, F_MISSILE)
            && can_unit_load(missile, punit)) {
          UNIT_LOG(LOGLEVEL_HUNT, missile, "loaded on hunter");
          ai_unit_new_role(missile, AIUNIT_ESCORT, target->x, target->y);
          load_unit_onto_transporter(missile, punit);
        }
      } unit_list_iterate_end;
    }
  }
}

/**************************************************************************
  Try to make pcity our new homecity. Fails if we can't upkeep it. Assumes
  success from server.
**************************************************************************/
bool ai_unit_make_homecity(struct unit *punit, struct city *pcity)
{
  CHECK_UNIT(punit);
  assert(punit->owner == pcity->owner);

  if (punit->homecity == 0 && !unit_has_role(punit->type, L_EXPLORER)) {
    /* This unit doesn't pay any upkeep while it doesn't have a homecity,
     * so it would be stupid to give it one. There can also be good reasons
     * why it doesn't have a homecity. */
    /* However, until we can do something more useful with them, we
       will assign explorers to a city so that they can be disbanded for 
       the greater good -- Per */
    return FALSE;
  }
  if (pcity->shield_surplus - unit_type(punit)->shield_cost >= 0
      && pcity->food_surplus - unit_type(punit)->food_cost >= 0) {
    handle_unit_change_homecity(unit_owner(punit), punit->id, pcity->id);
    return TRUE;
  }
  return FALSE;
}

/**************************************************************************
  Move a bodyguard along with another unit. We assume that unit has already
  been moved to (x, y) which is a valid, safe coordinate, and that our
  bodyguard has not. This is an ai_unit_* auxiliary function, do not use 
  elsewhere.
**************************************************************************/
static void ai_unit_bodyguard_move(int unitid, int x, int y)
{
  struct unit *bodyguard = find_unit_by_id(unitid);
  struct unit *punit;
  struct player *pplayer;

  assert(bodyguard != NULL);
  pplayer = unit_owner(bodyguard);
  assert(pplayer != NULL);
  punit = find_unit_by_id(bodyguard->ai.charge);
  assert(punit != NULL);

  assert(punit->ai.bodyguard == bodyguard->id);
  assert(bodyguard->ai.charge == punit->id);

  if (!is_tiles_adjacent(x, y, bodyguard->x, bodyguard->y)) {
    return;
  }

  if (bodyguard->moves_left <= 0) {
    /* should generally should not happen */
    BODYGUARD_LOG(LOG_DEBUG, bodyguard, "was left behind by charge");
    return;
  }

  handle_unit_activity_request(bodyguard, ACTIVITY_IDLE);
  (void) ai_unit_move(bodyguard, x, y);
}

/**************************************************************************
  Check if we have a bodyguard with sanity checking and error recovery.
  Repair incompletely referenced bodyguards. When the rest of the bodyguard
  mess is cleaned up, this repairing should be replaced with an assert.
**************************************************************************/
static bool has_bodyguard(struct unit *punit)
{
  struct unit *guard;
  if (punit->ai.bodyguard > BODYGUARD_NONE) {
    if ((guard = find_unit_by_id(punit->ai.bodyguard))) {
      if (guard->ai.charge != punit->id) {
        BODYGUARD_LOG(LOG_VERBOSE, guard, "my charge didn't know about me!");
      }
      guard->ai.charge = punit->id; /* ensure sanity */
      return TRUE;
    } else {
      punit->ai.bodyguard = BODYGUARD_NONE;
      UNIT_LOG(LOGLEVEL_BODYGUARD, punit, "bodyguard disappeared!");
    }
  }
  return FALSE;
}

/**************************************************************************
  Move and attack with an ai unit. We do not wait for server reply.
**************************************************************************/
bool ai_unit_attack(struct unit *punit, int x, int y)
{
  int sanity = punit->id;
  bool alive;

  CHECK_UNIT(punit);
  assert(unit_owner(punit)->ai.control);
  assert(is_normal_map_pos(x, y));
  assert(is_tiles_adjacent(punit->x, punit->y, x, y));

  handle_unit_activity_request(punit, ACTIVITY_IDLE);
  (void) handle_unit_move_request(punit, x, y, FALSE, FALSE);
  alive = (find_unit_by_id(sanity) != NULL);

  if (alive && same_pos(x, y, punit->x, punit->y)
      && has_bodyguard(punit)) {
    ai_unit_bodyguard_move(punit->ai.bodyguard, x, y);
    /* Clumsy bodyguard might trigger an auto-attack */
    alive = (find_unit_by_id(sanity) != NULL);
  }

  return alive;
}

/**************************************************************************
  Move an ai unit. Do not attack. Do not leave bodyguard.

  This function returns only when we have a reply from the server and
  we can tell the calling function what happened to the move request.
  (Right now it is not a big problem, since we call the server directly.)
**************************************************************************/
bool ai_unit_move(struct unit *punit, int x, int y)
{
  struct unit *bodyguard;
  int sanity = punit->id;
  struct player *pplayer = unit_owner(punit);
  struct tile *ptile = map_get_tile(x,y);

  CHECK_UNIT(punit);
  assert(unit_owner(punit)->ai.control);
  assert(is_normal_map_pos(x, y));
  assert(is_tiles_adjacent(punit->x, punit->y, x, y));

  /* if enemy, stop and let ai attack function take this case */
  if (is_enemy_unit_tile(ptile, pplayer)
      || is_enemy_city_tile(ptile, pplayer)) {
    UNIT_LOG(LOG_DEBUG, punit, "movement halted due to enemy presence");
    return FALSE;
  }

  /* barbarians shouldn't enter huts */
  if (is_barbarian(pplayer) && tile_has_special(ptile, S_HUT)) {
    return FALSE;
  }

  /* don't leave bodyguard behind */
  if (has_bodyguard(punit)
      && (bodyguard = find_unit_by_id(punit->ai.bodyguard))
      && same_pos(punit->x, punit->y, bodyguard->x, bodyguard->y)
      && bodyguard->moves_left == 0) {
    UNIT_LOG(LOGLEVEL_BODYGUARD, punit, "does not want to leave "
             "its bodyguard");
    return FALSE;
  }

  /* Try not to end move next to an enemy if we can avoid it by waiting */
  if (punit->moves_left <= map_move_cost(punit, x, y)
      && unit_move_rate(punit) > map_move_cost(punit, x, y)
      && enemies_at(punit, x, y)
      && !enemies_at(punit, punit->x, punit->y)) {
    UNIT_LOG(LOG_DEBUG, punit, "ending move early to stay out of trouble");
    return FALSE;
  }

  /* go */
  handle_unit_activity_request(punit, ACTIVITY_IDLE);
  (void) handle_unit_move_request(punit, x, y, FALSE, TRUE);

  /* handle the results */
  if (find_unit_by_id(sanity) && same_pos(x, y, punit->x, punit->y)) {
    if (has_bodyguard(punit)) {
      ai_unit_bodyguard_move(punit->ai.bodyguard, x, y);
    }
    return TRUE;
  }
  return FALSE;
}

/**************************************************************************
This looks for the nearest city:
If (x,y) is the land, it looks for cities only on the same continent
unless (everywhere != 0)
If (enemy != 0) it looks only for enemy cities
If (pplayer != NULL) it looks for cities known to pplayer
**************************************************************************/
struct city *dist_nearest_city(struct player *pplayer, int x, int y,
                               bool everywhere, bool enemy)
{ 
  struct city *pc=NULL;
  int best_dist = -1;
  Continent_id con = map_get_continent(x, y);

  players_iterate(pplay) {
    /* If "enemy" is set, only consider cities whose owner we're at
     * war with. */
    if (enemy && pplayer && !pplayers_at_war(pplayer, pplay)) {
      continue;
    }

    city_list_iterate(pplay->cities, pcity) {
      int city_dist = real_map_distance(x, y, pcity->x, pcity->y);

      /* Find the closest city known to the player with a matching
       * continent. */
      if ((best_dist == -1 || city_dist < best_dist)
	  && (everywhere || con == 0
	      || con == map_get_continent(pcity->x, pcity->y))
	  && (!pplayer || map_is_known(pcity->x, pcity->y, pplayer))) {
	best_dist = city_dist;
        pc = pcity;
      }
    } city_list_iterate_end;
  } players_iterate_end;

  return(pc);
}


/**************************************************************************
  Calculate the value of the target unit including the other units which
  will die in a successful attack
**************************************************************************/
int stack_cost(struct unit *pdef)
{
  int victim_cost = 0;

  if (is_stack_vulnerable(pdef->x, pdef->y)) {
    /* lotsa people die */
    unit_list_iterate(map_get_tile(pdef->x, pdef->y)->units, aunit) {
      victim_cost += unit_build_shield_cost(aunit->type);
    } unit_list_iterate_end;
  } else {
    /* Only one unit dies if attack is successful */
    victim_cost = unit_build_shield_cost(pdef->type);
  }
  
  return victim_cost;
}

/**************************************************************************
  Change government, pretty fast...
**************************************************************************/
void ai_government_change(struct player *pplayer, int gov)
{
  if (gov == pplayer->government) {
    return;
  }
  pplayer->government = game.government_when_anarchy;

  /* The AI cheats by having instant revolutions.
   * TODO: turn this into a player handicap. */
  pplayer->revolution_finishes = game.turn;

  handle_player_government(pplayer, gov);
  /* The revolution_finishes value need not be reset at this point. */
  city_list_iterate(pplayer->cities, pcity) {
    auto_arrange_workers(pcity); /* update cities */
  } city_list_iterate_end;
}

/**************************************************************************
  Credits the AI wants to have in reserves. We need some gold to bribe
  and incite cities.

  "I still don't trust this function" -- Syela
**************************************************************************/
int ai_gold_reserve(struct player *pplayer)
{
  int i = total_player_citizens(pplayer)*2;
  return MAX(pplayer->ai.maxbuycost, i);
}

/**************************************************************************
  Sets the values of the choice to initial values.
**************************************************************************/
void init_choice(struct ai_choice *choice)
{
  choice->choice = A_UNSET;
  choice->want = 0;
  choice->type = CT_NONE;
  choice->need_boat = FALSE;
}

/**************************************************************************
...
**************************************************************************/
void adjust_choice(int value, struct ai_choice *choice)
{
  choice->want = (choice->want *value)/100;
}

/**************************************************************************
...
**************************************************************************/
void copy_if_better_choice(struct ai_choice *cur, struct ai_choice *best)
{
  if (cur->want > best->want) {
    freelog(LOG_DEBUG, "Overriding choice (%s, %d) with (%s, %d)",
	    (best->type == CT_BUILDING ? 
	     get_improvement_name(best->choice) : unit_types[best->choice].name), 
	    best->want, 
	    (cur->type == CT_BUILDING ? 
	     get_improvement_name(cur->choice) : unit_types[cur->choice].name), 
	    cur->want);
    best->choice =cur->choice;
    best->want = cur->want;
    best->type = cur->type;
    best->need_boat = cur->need_boat;
  }
}

/**************************************************************************
  Returns TRUE if pcity's owner is building any wonder in another city on
  the same continent (if so, we may want to build a caravan here).
**************************************************************************/
static bool is_building_other_wonder(struct city *pcity)
{
  struct player *pplayer = city_owner(pcity);

  city_list_iterate(pplayer->cities, acity) {
    if (pcity != acity
	&& !acity->is_building_unit
	&& is_wonder(acity->currently_building)
	&& (map_get_continent(acity->x, acity->y)
	    == map_get_continent(pcity->x, pcity->y))) {
      return TRUE;
    }
  } city_list_iterate_end;

  return FALSE;
}

/**************************************************************************
  Choose improvement we like most and put it into ai_choice.
  TODO: Clean, update the log calls.
**************************************************************************/
void ai_advisor_choose_building(struct city *pcity, struct ai_choice *choice)
{ /* I prefer the ai_choice as a return value; gcc prefers it as an arg -- Syela */
  Impr_Type_id id = B_LAST;
  unsigned int danger = 0;
  int downtown = 0, cities = 0;
  int want=0;
  struct player *plr;
        
  plr = city_owner(pcity);
     
  /* too bad plr->score isn't kept up to date. */
  city_list_iterate(plr->cities, acity)
    danger += acity->ai.danger;
    downtown += acity->ai.downtown;
    cities++;
  city_list_iterate_end;

  impr_type_iterate(i) {
    if (!is_wonder(i)
	|| (!pcity->is_building_unit && is_wonder(pcity->currently_building)
	    && pcity->shield_stock >= impr_build_shield_cost(i) / 2)
	|| (!is_building_other_wonder(pcity)
	    /* otherwise caravans will be killed! */
	    && pcity->ai.grave_danger == 0
	    && pcity->ai.downtown * cities >= downtown
	    && pcity->ai.danger * cities <= danger)) {
      /* Is this too many restrictions? */
      /* trying to keep wonders in safe places with easy caravan
       * access -- Syela */
      if(pcity->ai.building_want[i]>want) {
	/* we have to do the can_build check to avoid Built Granary.
	 * Now Building Granary. */
        if (can_build_improvement(pcity, i)) {
          want = pcity->ai.building_want[i];
          id = i;
        } else {
	  freelog(LOG_DEBUG, "%s can't build %s", pcity->name,
		  get_improvement_name(i));
	}
      } /* id is the building we like the best */
    }
  } impr_type_iterate_end;

  if (want != 0) {
    freelog(LOG_DEBUG, "AI_Chosen: %s with desire = %d for %s",
	    get_improvement_name(id), want, pcity->name);
  } else {
    freelog(LOG_DEBUG, "AI_Chosen: None for %s", pcity->name);
  }
  choice->want = want;
  choice->choice = id;
  choice->type = CT_BUILDING;
}

/**********************************************************************
  "The following evaluates the unhappiness caused by military units
  in the field (or aggressive) at a city when at Republic or 
  Democracy.

  Now generalised somewhat for government rulesets, though I'm not 
  sure whether it is fully general for all possible parameters/
  combinations." --dwp
**********************************************************************/
bool ai_assess_military_unhappiness(struct city *pcity,
                                    struct government *g)
{
  int free_happy;
  bool have_police;
  int variant;
  int unhap = 0;

  /* bail out now if happy_cost is 0 */
  if (g->unit_happy_cost_factor == 0) {
    return FALSE;
  }
  
  free_happy  = citygov_free_happy(pcity, g);
  have_police = city_got_effect(pcity, B_POLICE);
  variant = improvement_variant(B_WOMENS);

  if (variant == 0 && have_police) {
    /* ??  This does the right thing for normal Republic and Democ -- dwp */
    free_happy += g->unit_happy_cost_factor;
  }
  
  unit_list_iterate(pcity->units_supported, punit) {
    int happy_cost = utype_happy_cost(unit_type(punit), g);

    if (happy_cost <= 0) {
      continue;
    }

    /* See discussion/rules in common/city.c:city_support() */
    if (!unit_being_aggressive(punit)) {
      if (is_field_unit(punit)) {
	happy_cost = 1;
      } else {
	happy_cost = 0;
      }
    }
    if (happy_cost <= 0) {
      continue;
    }

    if (variant == 1 && have_police) {
      happy_cost--;
    }
    adjust_city_free_cost(&free_happy, &happy_cost);
    
    if (happy_cost > 0) {
      unhap += happy_cost;
    }
  } unit_list_iterate_end;
 
  if (unhap < 0) {
    unhap = 0;
  }
  return (unhap > 0);
}

/**************************************************************************
  AI doesn't want the score for future techs.
**************************************************************************/
bool ai_wants_no_science(struct player *pplayer)
{
  return is_future_tech(pplayer->research.researching);
}
