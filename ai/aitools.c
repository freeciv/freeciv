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

#include <stdio.h>
#include <stdlib.h>

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

#include "aicity.h"
#include "aidata.h"
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

/**************************************************************************
  Create a virtual unit to use in build want estimation. pcity can be 
  NULL.
**************************************************************************/
struct unit *create_unit_virtual(struct player *pplayer, struct city *pcity,
                                 Unit_Type_id type, bool make_veteran)
{
  struct unit *punit;
  punit=fc_calloc(1, sizeof(struct unit));

  assert(pcity);
  punit->type = type;
  punit->owner = pplayer->player_no;
  CHECK_MAP_POS(pcity->x, pcity->y);
  punit->x = pcity->x;
  punit->y = pcity->y;
  punit->goto_dest_x = 0;
  punit->goto_dest_y = 0;
  punit->veteran = make_veteran;
  punit->homecity = pcity->id;
  punit->upkeep = 0;
  punit->upkeep_food = 0;
  punit->upkeep_gold = 0;
  punit->unhappiness = 0;
  /* A unit new and fresh ... */
  punit->foul = FALSE;
  punit->fuel = unit_type(punit)->fuel;
  punit->hp = unit_type(punit)->hp;
  punit->moves_left = unit_move_rate(punit);
  punit->moved = FALSE;
  punit->paradropped = FALSE;
  if (is_barbarian(pplayer)) {
    punit->fuel = BARBARIAN_LIFE;
  }
  /* AI.control is probably always true... */
  punit->ai.control = FALSE;
  punit->ai.ai_role = AIUNIT_NONE;
  punit->ai.ferryboat = 0;
  punit->ai.passenger = 0;
  punit->ai.bodyguard = 0;
  punit->ai.charge = 0;
  punit->bribe_cost = -1; /* flag value */
  punit->transported_by = -1;
  punit->pgr = NULL;
  set_unit_activity(punit, ACTIVITY_IDLE);

  return punit;
}

/**************************************************************************
  Free the memory used by virtual unit
  It is assumed (since it's virtual) that it's not registered or 
  listed anywhere.
**************************************************************************/
void destroy_unit_virtual(struct unit *punit)
{
  if (punit->pgr) {
    /* Should never happen, but do it anyway for completeness */
    free(punit->pgr->pos);
    free(punit->pgr);
    punit->pgr = NULL;
  }

  free(punit);
}

/**************************************************************************
  This will eventually become the ferry-enabled goto. For now, it just
  wraps ai_unit_goto()
**************************************************************************/
bool ai_unit_gothere(struct unit *punit)
{
  CHECK_UNIT(punit);
  assert(punit->goto_dest_x != -1 && punit->goto_dest_y != -1);
  if (ai_unit_goto(punit, punit->goto_dest_x, punit->goto_dest_y)) {
    return TRUE; /* ... and survived */
  } else {
    return FALSE; /* we died */
  }
}

/**************************************************************************
  Go to specified destination but do not disturb existing role or activity
  and do not clear the role's destination. Return FALSE iff we died.

  FIXME: add some logging functionality to replace GOTO_LOG()
**************************************************************************/
bool ai_unit_goto(struct unit *punit, int x, int y)
{
  enum goto_result result;
  int oldx = punit->goto_dest_x, oldy = punit->goto_dest_y;
  enum unit_activity activity = punit->activity;

  CHECK_UNIT(punit);
  /* TODO: log error on same_pos with punit->x|y */
  punit->goto_dest_x = x;
  punit->goto_dest_y = y;
  handle_unit_activity_request(punit, ACTIVITY_GOTO);
  result = do_unit_goto(punit, GOTO_MOVE_ANY, FALSE);
  if (result != GR_DIED) {
    handle_unit_activity_request(punit, activity);
    punit->goto_dest_x = oldx;
    punit->goto_dest_y = oldy;
    return TRUE;
  }
  return FALSE;
}

/**************************************************************************
  Ensure unit sanity by telling charge that we won't bodyguard it anymore,
  add and remove city spot reservation, and set destination.
**************************************************************************/
void ai_unit_new_role(struct unit *punit, enum ai_unit_task task, int x, int y)
{
  struct unit *charge = find_unit_by_id(punit->ai.charge);

  if (punit->activity == ACTIVITY_GOTO) {
    /* It would indicate we're going somewhere otherwise */
    handle_unit_activity_request(punit, ACTIVITY_IDLE);
  }

  if (punit->ai.ai_role == AIUNIT_BUILD_CITY) {
    assert(is_normal_map_pos(punit->goto_dest_x, punit->goto_dest_y));
    remove_city_from_minimap(punit->goto_dest_x, punit->goto_dest_y);
  }

  if (charge && (charge->ai.bodyguard == punit->id)) {
    /* ensure we don't let the unit believe we bodyguard it */
    charge->ai.bodyguard = BODYGUARD_NONE;
  }
  punit->ai.charge = BODYGUARD_NONE;

  punit->ai.ai_role = task;
/* TODO:
  punit->goto_dest_x = x;
  punit->goto_dest_y = y; */

  if (punit->ai.ai_role == AIUNIT_BUILD_CITY) {
    assert(is_normal_map_pos(x, y));
    add_city_to_minimap(x, y);
  }
}

/**************************************************************************
  Try to make pcity our new homecity. Fails if we can't upkeep it. Assumes
  success from server.
**************************************************************************/
bool ai_unit_make_homecity(struct unit *punit, struct city *pcity)
{
  CHECK_UNIT(punit);
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
    struct packet_unit_request packet;
    packet.unit_id = punit->id;
    packet.city_id = pcity->id;
    handle_unit_change_homecity(unit_owner(punit), &packet);
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
  struct packet_move_unit pmove;
  int sanity = punit->id;
  bool alive;

  CHECK_UNIT(punit);
  assert(unit_owner(punit)->ai.control);
  assert(is_normal_map_pos(x, y));
  assert(is_tiles_adjacent(punit->x, punit->y, x, y));

  pmove.x = x;
  pmove.y = y;
  pmove.unid = punit->id;
  handle_unit_activity_request(punit, ACTIVITY_IDLE);
  handle_move_unit(unit_owner(punit), &pmove);
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
  struct packet_move_unit pmove;
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
      && unit_type(punit)->move_rate > map_move_cost(punit, x, y)
      && enemies_at(punit, x, y)
      && !enemies_at(punit, punit->x, punit->y)) {
    UNIT_LOG(LOG_DEBUG, punit, "ending move early to stay out of trouble");
    return FALSE;
  }

  /* go */
  pmove.x = x;
  pmove.y = y;
  pmove.unid = punit->id;
  handle_unit_activity_request(punit, ACTIVITY_IDLE);
  handle_move_unit(unit_owner(punit), &pmove);

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
  int dist = MAX(map.xsize / 2, map.ysize);
  int con = map_get_continent(x, y, NULL);

  players_iterate(pplay) {
    if ((enemy) && (pplayer) && (!pplayers_at_war(pplayer,pplay))) continue;

    city_list_iterate(pplay->cities, pcity)
      if (real_map_distance(x, y, pcity->x, pcity->y) < dist &&
         (everywhere || con == 0 || con == 
          map_get_continent(pcity->x, pcity->y, NULL)) &&
         (!pplayer || map_get_known(pcity->x, pcity->y, pplayer))) {
        dist = real_map_distance(x, y, pcity->x, pcity->y);
        pc = pcity;
      }
    city_list_iterate_end;
  } players_iterate_end;

  return(pc);
}

/**************************************************************************
  Is it a city/fortress or will the whole stack die in an attack
  TODO: use new killstack thing
**************************************************************************/
bool is_stack_vulnerable(int x, int y)
{
  return !(map_get_city(x, y) != NULL ||
	   map_has_special(x, y, S_FORTRESS) ||
	   map_has_special(x, y, S_AIRBASE) );
}

/**************************************************************************
  Change government, pretty fast...
**************************************************************************/
void ai_government_change(struct player *pplayer, int gov)
{
  struct packet_player_request preq;

  if (gov == pplayer->government) {
    return;
  }
  preq.government = gov;
  pplayer->revolution = 0;
  pplayer->government = game.government_when_anarchy;
  handle_player_government(pplayer, &preq);
  pplayer->revolution = -1; /* yes, I really mean this. -- Syela */
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
  choice->choice = A_NONE;
  choice->want = 0;
  choice->type = CT_NONE;
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
  }
}

/**************************************************************************
...
**************************************************************************/
void ai_advisor_choose_building(struct city *pcity, struct ai_choice *choice)
{ /* I prefer the ai_choice as a return value; gcc prefers it as an arg -- Syela */
  Impr_Type_id id = B_LAST;
  int danger = 0, downtown = 0, cities = 0;
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
    if (!is_wonder(i) ||
       (!pcity->is_building_unit && is_wonder(pcity->currently_building) &&
       pcity->shield_stock >= improvement_value(i) / 2) ||
       (!is_building_other_wonder(pcity) &&
        pcity->ai.grave_danger == 0 && /* otherwise caravans will be killed! */
        pcity->ai.downtown * cities >= downtown &&
        pcity->ai.danger * cities <= danger)) { /* too many restrictions? */
/* trying to keep wonders in safe places with easy caravan access -- Syela */
      if(pcity->ai.building_want[i]>want) {
/* we have to do the can_build check to avoid Built Granary.  Now Building Granary. */
        if (can_build_improvement(pcity, i)) {
          want=pcity->ai.building_want[i];
          id=i;
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
The following evaluates the unhappiness caused by military units
in the field (or aggressive) at a city when at Republic or Democracy

Now generalised somewhat for government rulesets, though I'm not sure
whether it is fully general for all possible parameters/combinations.
 --dwp
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

  if (variant==0 && have_police) {
    /* ??  This does the right thing for normal Republic and Democ -- dwp */
    free_happy += g->unit_happy_cost_factor;
  }
  
  unit_list_iterate(pcity->units_supported, punit) {
    int happy_cost = utype_happy_cost(unit_type(punit), g);

    if (happy_cost<=0)
      continue;

    /* See discussion/rules in cityturn.c:city_support() --dwp */
    if (!unit_being_aggressive(punit)) {
      if (is_field_unit(punit)) {
	happy_cost = 1;
      } else {
	happy_cost = 0;
      }
    }
    if (happy_cost<=0)
      continue;

    if (variant==1 && have_police) {
      happy_cost--;
    }
    adjust_city_free_cost(&free_happy, &happy_cost);
    
    if (happy_cost>0)
      unhap += happy_cost;
  }
  unit_list_iterate_end;
 
  if (unhap < 0) unhap = 0;
  return unhap > 0;
}
