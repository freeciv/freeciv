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
#include "government.h"
#include "idex.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "player.h"
#include "rand.h"
#include "shared.h"
#include "support.h"
#include "tech.h"
#include "unit.h"

#include "barbarian.h"
#include "cityturn.h"
#include "gamelog.h"
#include "maphand.h"
#include "player.h"
#include "plrhand.h"
#include "sernet.h"
#include "settlers.h"
#include "spacerace.h"
#include "srv_main.h"
#include "unithand.h"
#include "unittools.h"

#include "aicity.h"
#include "aiunit.h"

#include "citytools.h"

static int evaluate_city_name_priority(int x, int y,
				       struct city_name *city_name,
				       int default_priority);
static char *search_for_city_name(int x, int y, struct city_name *city_names,
				  struct player *pplayer);
static void server_set_tile_city(struct city *pcity, int city_x, int city_y,
				 enum city_tile_type type);

struct city_name *misc_city_names;

/****************************************************************
Returns the priority of the city name at the given position,
using its own internal algorithm.  Lower priority values are
more desired, and all priorities are non-negative.

This function takes into account game.natural_city_names, and
should be able to deal with any future options we want to add.
*****************************************************************/
static int evaluate_city_name_priority(int x, int y,
				       struct city_name *city_name,
				       int default_priority)
{
  /* Lower values mean higher priority. */
  float priority = (float)default_priority;
  int goodness;
  enum tile_terrain_type type;

  /* Increasing this value will increase the difference caused by
     (non-)matching terrain.  A matching terrain is mult_factor
     "better" than an unlisted terrain, which is mult_factor
     "better" than a non-matching terrain. */
  const float mult_factor = 1.4;

  /*
   * If natural city names aren't being used, we just return the
   * base value.  This will have the effect of the first-listed
   * city being used.  We do this here rather than special-casing
   * it elewhere because this localizes everything to this
   * function, even though it's a bit inefficient.
   */
  if (!game.natural_city_names) {
    return default_priority;
  }

  /*
   * Assuming we're using the natural city naming system, we use
   * an internal alorithm to calculate the priority of each name.
   * It's a pretty fuzzy algorithm; we basically do the following:
   *   - Change the priority scale from 0..n to 10..n+10.  This means
   *     each successive city is 10% lower priority than the first.
   *   - Multiply by a semi-random number.  This has the effect of
   *     decreasing rounding errors in the successive calculations,
   *     as well as introducing a smallish random effect.
   *   - Check over all the applicable terrains, and
   *     multiply or divide the priority based on whether or not
   *     the terrain matches.  See comment below.
   */

  priority += 10.0;
  priority *= 10.0 + myrand(5);

  /*
   * The terrain priority in the city_name struct will be either
   * -1, 0, or 1.  We therefore take this as-is if the terrain is
   * present, or negate it if not.
   *
   * The reason we multiply as well as divide the value is so
   * that cities that don't care what terrain they are on (which
   * is the default) will be left in the middle of the pack.  If
   * we _only_ multiplied (or divided), then cities that had more
   * terrain labels would have their priorities hurt (or helped).
   */
  goodness = map_has_special(x, y, S_RIVER) ?
	      city_name->river : -city_name->river;
  if (goodness > 0) {
    priority /= mult_factor;
  } else if (goodness < 0) {
    priority *= mult_factor;
  }

  for (type = T_FIRST; type < T_COUNT; type++) {
    /* Now we do the same for every available terrain. */
    goodness = is_terrain_near_tile(x, y, type) ?
		 city_name->terrain[type] : -city_name->terrain[type];
    if (goodness > 0) {
      priority /= mult_factor;
    } else if (goodness < 0) {
      priority *= mult_factor;
    }
  }

  return (int)priority;	
}

/**************************************************************************
Checks if a city name belongs to default city names of a particular
player.
**************************************************************************/
static bool is_default_city_name(const char *name, struct player *pplayer)
{
  struct nation_type *nation = get_nation_by_plr(pplayer);
  int choice;

  for (choice = 0; nation->city_names[choice].name; choice++) {
    if (mystrcasecmp(name, nation->city_names[choice].name) == 0) {
      return TRUE;
    }
  }

  return FALSE;
}

/****************************************************************
Searches through a city name list (a struct city_name array)
to pick the best available city name, and returns a pointer to
it.  The function checks if the city name is available and calls
evaluate_city_name_priority to determine the priority of the
city name.  If the list has no valid entries in it, NULL will be
returned.
*****************************************************************/
static char *search_for_city_name(int x, int y, struct city_name *city_names,
				  struct player *pplayer)
{
  int choice, best_priority = -1;
  char* best_name = NULL;

  for (choice = 0; city_names[choice].name; choice++) {
    if (!game_find_city_by_name(city_names[choice].name) &&
	is_allowed_city_name(pplayer, city_names[choice].name, x, y, FALSE)) {
      int priority = evaluate_city_name_priority(x, y, &city_names[choice],
						 choice);

      if (best_priority == -1 || priority < best_priority) {
	best_priority = priority;
	best_name = city_names[choice].name;
      }
    }
  }

  return best_name;
}

/**************************************************************************
Checks, if a city name is allowed for a player. If not, reports a
reason for rejection. There's 4 different modes:
0: no restrictions,
1: a city name has to be unique to player
2: a city name has to be globally unique
3: a city name has to be globally unique, and players can't use names
   that are in another player's default city names. (E.g. Swedish may not
   call new cities or rename old cities as Helsinki, because it's in
   Finns' default city names)
**************************************************************************/
bool is_allowed_city_name(struct player *pplayer, const char *city_name,
			  int x, int y, bool notify_player)
{
  /* Mode 0: No restrictions to city names */
  if (game.allowed_city_names == 0) {
    return TRUE;
  }

  /* Mode 1: A city name has to be unique to player */
  if (game.allowed_city_names == 1 &&
      city_list_find_name(&pplayer->cities, city_name)) {
    if (notify_player) {
      notify_player_ex(pplayer, x, y, E_NOEVENT,
		       _("Game: You already have a city called %s"),
		       city_name);
    }
    return FALSE;
  }

  /* 
   * Mode 3: Check first that the proposed city name is not in another
   * player's default city names.
   */
  if (game.allowed_city_names == 3) {
    struct player *pother = NULL;

    players_iterate(player2) {
      if (player2 != pplayer && is_default_city_name(city_name, player2)) {
	pother = player2;
	break;
      }
    } players_iterate_end;

    if (pother != NULL) {
      if (notify_player) {
	notify_player_ex(pplayer, x, y, E_NOEVENT,
			 _("Game: Can't use %s as a city name. "
			   "It is reserved for %s."),
			 city_name, get_nation_name_plural(pother->nation));
      }
      return FALSE;
    }
  }

  /* Modes 2,3: A city name has to be globally unique */
  if ((game.allowed_city_names == 2 ||
       game.allowed_city_names == 3) && game_find_city_by_name(city_name)) {
    if (notify_player) {
      notify_player_ex(pplayer, x, y, E_NOEVENT,
		       _("Game: A city called %s already exists."),
		       city_name);
    }
    return FALSE;
  }

  return TRUE;
}

/****************************************************************
Come up with a default name when a new city is about to be built.
Handle running out of names etc. gracefully.  Maybe we should keep
track of which names have been rejected by the player, so that we do
not suggest them again?
Returned pointer points into internal data structures or static
buffer etc, and should be considered read-only (and not freed)
by caller.
*****************************************************************/
char *city_name_suggestion(struct player *pplayer, int x, int y)
{
  char *name;
  int i;
  struct nation_type *nation = get_nation_by_plr(pplayer);
  /* tempname must be static because it's returned below. */
  static char tempname[MAX_LEN_NAME];

  static const int num_tiles = MAP_MAX_WIDTH * MAP_MAX_HEIGHT; 

  freelog(LOG_VERBOSE, "Suggesting city name for %s at (%d,%d)",
	  pplayer->name, x, y);
  
  CHECK_MAP_POS(x,y);

  name = search_for_city_name(x, y, nation->city_names, pplayer);
  if (name) {
    return name;
  }

  name = search_for_city_name(x, y, misc_city_names, pplayer);
  if (name) {
    return name;
  }

  for (i = 1; i <= num_tiles; i++ ) {
    my_snprintf(tempname, MAX_LEN_NAME, _("City no. %d"), i);
    if (!game_find_city_by_name(tempname)) 
      return tempname;
  }
  
  /* This had better be impossible! */
  assert(FALSE);
  return _("A poorly-named city");
}

/****************************************************************
...
*****************************************************************/
bool city_got_barracks(struct city *pcity)
{
  return (city_affected_by_wonder(pcity, B_SUNTZU) ||
	  city_got_building(pcity, B_BARRACKS) || 
	  city_got_building(pcity, B_BARRACKS2) ||
	  city_got_building(pcity, B_BARRACKS3));
}

/**************************************************************************
...
**************************************************************************/
bool can_sell_building(struct city *pcity, int id)
{
  return (city_got_building(pcity, id) ? !is_wonder(id) : FALSE);
}

/**************************************************************************
...
**************************************************************************/
struct city *find_city_wonder(Impr_Type_id id)
{
  return (find_city_by_id(game.global_wonders[id]));
}

/**************************************************************************
  calculate the remaining build points 
**************************************************************************/
int build_points_left(struct city *pcity)
{
  return (improvement_value(pcity->currently_building) - pcity->shield_stock);
}

/**************************************************************************
...
**************************************************************************/
bool is_worked_here(int x, int y)
{
  return (map_get_tile(x, y)->worked != NULL); /* saves at least 10% of runtime CPU usage! */
}

/**************************************************************************
The value of excees food is dependent on the amount of food it takes for a
city to increase in size. This amount is in turn dependent on the citysize,
hence this function.
The value returned from this function does not take into account whether
increasing a city's size is attractive, but only how effective the food
will be.
**************************************************************************/
int food_weighting(int city_size)
{
  static int cache[MAX_CITY_SIZE];
  static bool cache_valid = FALSE;

  if (!cache_valid) {
    int size = 0;

    for (size = 1; size < MAX_CITY_SIZE; size++) {
      int food_weighting_is_for = 4;	/* FOOD_WEIGHTING applies to city with
					   foodbox width of 4 */
      int weighting = (food_weighting_is_for * FOOD_WEIGHTING) / (1 + size);

      /* If the citysize is 1 we assume it will not be so for long, and
         so adjust the value a little downwards. */
      if (size == 1) {
	weighting = (weighting * 3) / 4;
      }
      cache[size] = weighting;
    }
    cache_valid = TRUE;
  }

  assert(city_size > 0 && city_size < MAX_CITY_SIZE);

  return cache[city_size];
}

/**************************************************************************
...
**************************************************************************/
int city_tile_value(struct city *pcity, int x, int y, int foodneed, int prodneed)
{
  int i, j, k;
  struct player *plr;

  plr = city_owner(pcity);

  i = city_get_food_tile(x, y, pcity);
  if (foodneed > 0) i += 9 * (MIN(i, foodneed));
/* *= 10 led to stupidity with foodneed = 1, mine, and farmland -- Syela */
  i *= food_weighting(MAX(2,pcity->size));
  
  j = city_get_shields_tile(x, y, pcity);
  if (prodneed > 0) j += 9 * (MIN(j, prodneed));
  j *= SHIELD_WEIGHTING * city_shield_bonus(pcity);
  j /= 100;

  k = city_get_trade_tile(x, y, pcity) * pcity->ai.trade_want *
      (city_tax_bonus(pcity) * (plr->economic.tax + plr->economic.luxury) +
       city_science_bonus(pcity) * plr->economic.science) / 10000;

  return(i + j + k);
}  

/**************************************************************************
...
**************************************************************************/
int worst_worker_tile_value(struct city *pcity)
{
  int worst = 0, tmp;
  city_map_iterate(x, y) {
    if (is_city_center(x, y))
      continue;
    if (get_worker_city(pcity, x, y) == C_TILE_WORKER) {
      tmp = city_tile_value(pcity, x, y, 0, 0);
      if (tmp < worst || worst == 0) worst = tmp;
    }
  } city_map_iterate_end;
  return(worst);
}

/**************************************************************************
...
**************************************************************************/
int best_worker_tile_value(struct city *pcity)
{
  int best = 0, tmp;
  city_map_iterate(x, y) {
    if (is_city_center(x, y) ||
	(get_worker_city(pcity, x, y) == C_TILE_WORKER) ||
	can_place_worker_here(pcity, x, y)) {
      tmp = city_tile_value(pcity, x, y, 0, 0);
      if (tmp < best || best == 0) best = tmp;
    }
  } city_map_iterate_end;
  return(best);
}

/**************************************************************************
...
**************************************************************************/
int settler_eats(struct city *pcity)
{
  struct government *g = get_gov_pcity(pcity);
  int free_food   = citygov_free_food(pcity, g);
  int eat = 0;

  unit_list_iterate(pcity->units_supported, this_unit) {
    int food_cost = utype_food_cost(unit_type(this_unit), g);
    adjust_city_free_cost(&free_food, &food_cost);
    if (food_cost > 0) {
      eat += food_cost;
    }
  }
  unit_list_iterate_end;

  return eat;
}

/**************************************************************************
...
**************************************************************************/
bool is_building_other_wonder(struct city *pc)
{
  struct player *pplayer = city_owner(pc);
  city_list_iterate(pplayer->cities, pcity) 
    if ((pc != pcity) && !(pcity->is_building_unit) && is_wonder(pcity->currently_building) && map_get_continent(pcity->x, pcity->y) == map_get_continent(pc->x, pc->y))
      return TRUE;
  city_list_iterate_end;
  return FALSE;
}

/**************************************************************************
...
**************************************************************************/
bool built_elsewhere(struct city *pc, int wonder)
{
  struct player *pplayer = city_owner(pc);
  city_list_iterate(pplayer->cities, pcity) 
    if ((pc != pcity) && !(pcity->is_building_unit) && pcity->currently_building == wonder)
      return TRUE;
  city_list_iterate_end;
  return FALSE;
}

#ifdef UNUSED
/**************************************************************************
... (unused)
**************************************************************************/
void eval_buildings(struct city *pcity,int *values)
{
  int i, tech;
  struct player *plr = city_owner(pcity);
  struct government *g = get_gov_pcity(pcity);
  tech = (plr->research.researching != A_NONE);

  impr_type_iterate(i) {
    if (is_wonder(i) && can_build_improvement(pcity, i) && !built_elsewhere(pcity, i)) {
      if (wonder_obsolete(i))
	values[i]=1;
      else
	values[i]=99;
    } else
    values[i]=0;
  } impr_type_iterate_end;
  
  if (government_has_hint(g, G_FAVORS_GROWTH) || pcity->size < 5) {
    if (can_build_improvement(pcity, B_GRANARY)) 
      values[B_GRANARY]=pcity->food_surplus*50;
  }
  if (can_build_improvement(pcity, B_SUPERMARKET) &&
      player_knows_techs_with_flag(plr, TF_FARMLAND))
    values[B_SUPERMARKET]=pcity->size*55;

  if (can_build_improvement(pcity, B_AQUEDUCT)
      && pcity->size > game.aqueduct_size-2)
    values[B_AQUEDUCT]=pcity->size*125+pcity->food_surplus*50;
  if (can_build_improvement(pcity, B_SEWER)
      && pcity->size > game.sewer_size-1)
    values[B_SEWER]=pcity->size*100+pcity->food_surplus*50;
  
  if (can_build_improvement(pcity, B_HARBOUR) && (pcity->size > 5)) 
    values[B_HARBOUR]=pcity->size*60;
  if (can_build_improvement(pcity, B_OFFSHORE) && 
      !can_build_improvement(pcity, B_HARBOUR))
    values[B_OFFSHORE]=pcity->size*60;
  
  if (can_build_improvement(pcity, B_MARKETPLACE)) 
    values[B_MARKETPLACE]=pcity->trade_prod*200;
  
  if (pcity->trade_prod > 15) {
    if (can_build_improvement(pcity, B_BANK)) 
      values[B_BANK]=pcity->tax_total*100;

    if (can_build_improvement(pcity, B_STOCK)) 
      values[B_STOCK]=pcity->tax_total*100;
  }

  if (g->trade_bonus > 0 && can_build_improvement(pcity, B_SUPERHIGHWAYS))
    values[B_SUPERHIGHWAYS]=pcity->trade_prod*60;
  
  if (can_build_improvement(pcity, B_COURTHOUSE)) {
    if (g->corruption_level) 
      values[B_COURTHOUSE]=pcity->corruption*100;
    else 
      values[B_COURTHOUSE] =
	  (pcity->ppl_angry[4] * 2 + pcity->ppl_unhappy[4]) * 200 +
	  pcity->ppl_elvis * 400;
  }
  if (tech) {
    if (can_build_improvement(pcity, B_LIBRARY)) 
      values[B_LIBRARY]=pcity->science_total*200;
    
    if (can_build_improvement(pcity, B_UNIVERSITY)) 
      values[B_UNIVERSITY]=pcity->science_total*101;
    
    if (can_build_improvement(pcity, B_RESEARCH)) 
      values[B_RESEARCH]=pcity->science_total*100;
  }
  if (can_build_improvement(pcity, B_AIRPORT))
    values[B_AIRPORT]=pcity->shield_prod*49;

  if (pcity->shield_prod >= 15)
    if (can_build_improvement(pcity, B_PORT))
      values[B_PORT]=pcity->shield_prod*48;

  if (pcity->shield_prod >= 5) {
    if (can_build_improvement(pcity, B_BARRACKS))
      values[B_BARRACKS]=pcity->shield_prod*50;

    if (can_build_improvement(pcity, B_BARRACKS2))
      values[B_BARRACKS2]=pcity->shield_prod*50;

    if (can_build_improvement(pcity, B_BARRACKS3))
      values[B_BARRACKS3]=pcity->shield_prod*50;
  }
  if (can_build_improvement(pcity, B_TEMPLE))
    values[B_TEMPLE] =
	(pcity->ppl_angry[4] * 2 + pcity->ppl_unhappy[4]) * 200 +
	pcity->ppl_elvis * 600;

  if (can_build_improvement(pcity, B_COLOSSEUM))
    values[B_COLOSSEUM] =
	(pcity->ppl_angry[4] * 2 + pcity->ppl_unhappy[4]) * 200 +
	pcity->ppl_elvis * 300;

  if (can_build_improvement(pcity, B_CATHEDRAL))
    values[B_CATHEDRAL] =
	(pcity->ppl_angry[4] * 2 + pcity->ppl_unhappy[4]) * 201 +
	pcity->ppl_elvis * 300;

  if (!(tech && plr->economic.tax > 50)) {
    if (can_build_improvement(pcity, B_COASTAL))
      values[B_COASTAL]=pcity->size*36;
    if (can_build_improvement(pcity, B_CITY))
      values[B_CITY]=pcity->size*35;
  } 
  if (!(tech && plr->economic.tax > 40)) {
    if (can_build_improvement(pcity, B_SAM))
      values[B_SAM]=pcity->size*24;
    if (can_build_improvement(pcity, B_SDI))
      values[B_SDI]=pcity->size*25;
  }
  if (pcity->shield_prod >= 10)
    if (can_build_improvement(pcity, B_FACTORY)) 
      values[B_FACTORY]=pcity->shield_prod*125;

  if (city_got_building(pcity, B_FACTORY)) {
    
    if (can_build_improvement(pcity, B_HYDRO))
      values[B_HYDRO]=pcity->shield_prod*100+pcity->pollution*100;
    
    if (can_build_improvement(pcity, B_NUCLEAR))
      values[B_NUCLEAR]=pcity->shield_prod*101+pcity->pollution*100;
    
    if (can_build_improvement(pcity, B_POWER))
      values[B_POWER]=pcity->shield_prod*100;
  }

  if (can_build_improvement(pcity, B_MFG)) 
    values[B_MFG]=pcity->shield_prod*125;

  if (can_build_improvement(pcity, B_MASS)) 
    values[B_MASS]=pcity->pollution*(100+pcity->size);

  if (can_build_improvement(pcity, B_RECYCLING)) 
    values[B_RECYCLING]=pcity->pollution*(100+pcity->shield_prod);
    
  if (can_build_improvement(pcity, B_CAPITAL))
    values[B_CAPITAL]=pcity->shield_prod;
}
#endif

/**************************************************************************
...
**************************************************************************/
bool do_make_unit_veteran(struct city *pcity, Unit_Type_id id)
{
  if (unit_type_flag(id, F_DIPLOMAT))
    return government_has_flag(get_gov_pcity(pcity), G_BUILD_VETERAN_DIPLOMAT);

  if (is_ground_unittype(id) || improvement_variant(B_BARRACKS)==1)
    return city_got_barracks(pcity);
  else if (is_water_unit(id))
    return (city_affected_by_wonder(pcity, B_LIGHTHOUSE) || city_got_building(pcity, B_PORT));
  else
    return city_got_building(pcity, B_AIRPORT);
}

/**************************************************************************
...
**************************************************************************/
int city_shield_bonus(struct city *pcity)
{
  return pcity->shield_bonus;
}

/**************************************************************************
...
**************************************************************************/
int city_tax_bonus(struct city *pcity)
{
  return pcity->tax_bonus;
}

/**************************************************************************
...
**************************************************************************/
int city_science_bonus(struct city *pcity)
{
  return pcity->science_bonus;
}

/**************************************************************************
...
**************************************************************************/
bool wants_to_be_bigger(struct city *pcity)
{
  if (pcity->size < game.aqueduct_size) return TRUE;
  if (city_got_building(pcity, B_SEWER)) return TRUE;
  if (city_got_building(pcity, B_AQUEDUCT)
      && pcity->size < game.sewer_size) return TRUE;
  if (!pcity->is_building_unit) {
    if (pcity->currently_building == B_SEWER && pcity->did_buy) return TRUE;
    if (pcity->currently_building == B_AQUEDUCT && pcity->did_buy) return TRUE;
  } /* saves a lot of stupid flipflops -- Syela */
  return FALSE;
}

/*********************************************************************
Note: the old unit is not wiped here.
***********************************************************************/
static void transfer_unit(struct unit *punit, struct city *tocity,
			  bool verbose)
{
  struct player *from_player = unit_owner(punit);
  struct player *to_player = city_owner(tocity);

  if (from_player == to_player) {
    freelog(LOG_VERBOSE, "Changed homecity of %s's %s to %s",
	    from_player->name, unit_name(punit->type), tocity->name);
    if (verbose) {
      notify_player(from_player, _("Game: Changed homecity of %s to %s."),
		    unit_name(punit->type), tocity->name);
    }
  } else {
    struct city *in_city = map_get_city(punit->x, punit->y);
    if (in_city) {
      freelog(LOG_VERBOSE, "Transfered %s in %s from %s to %s",
	      unit_name(punit->type), in_city->name,
	      from_player->name, to_player->name);
      if (verbose) {
	notify_player(from_player, _("Game: Transfered %s in %s from %s to %s."),
		      unit_name(punit->type), in_city->name,
		      from_player->name, to_player->name);
      }
    } else {
      freelog(LOG_VERBOSE, "Transfered %s from %s to %s",
	      unit_name(punit->type),
	      from_player->name, to_player->name);
      if (verbose) {
	notify_player(from_player, _("Game: Transfered %s from %s to %s."),
		      unit_name(punit->type),
		      from_player->name, to_player->name);
      }
    }
  }

  (void) create_unit_full(to_player, punit->x, punit->y, punit->type,
			  punit->veteran, tocity->id, punit->moves_left,
			  punit->hp);
}

/*********************************************************************
 Units in a bought city are transferred to the new owner, units 
 supported by the city, but held in other cities are updated to
 reflect those cities as their new homecity.  Units supported 
 by the bought city, that are not in a city square may be deleted.

 - Kris Bubendorfer <Kris.Bubendorfer@MCS.VUW.AC.NZ>

pplayer: The player recieving the units if they are not disbanded and
         are not in a city
pvictim: The owner of the city the units are transfered from.
units:   A list of units to be transfered, typically a cities unit list.
pcity:   Default city the units are transfered to.
exclude_city: The units cannot be transfered to this city.
kill_outside: Units outside this range are deleted. -1 means no units
              are deleted.
verbose: Messages are sent to the involved parties.
***********************************************************************/
void transfer_city_units(struct player *pplayer, struct player *pvictim, 
			 struct unit_list *units, struct city *pcity,
			 struct city *exclude_city,
			 int kill_outside, bool verbose)
{
  int x = pcity->x;
  int y = pcity->y;

  /* Transfer enemy units in the city to the new owner.
     Only relevant if we are transfering to another player. */
  if (pplayer != pvictim) {
    unit_list_iterate(map_get_tile(x, y)->units, vunit)  {
      /* Don't transfer units already owned by new city-owner --wegge */ 
      if (unit_owner(vunit) == pvictim) {
	transfer_unit(vunit, pcity, verbose);
	wipe_unit_safe(vunit, &myiter);
	unit_list_unlink(units, vunit);
      }
    } unit_list_iterate_end;
  }

  /* Any remaining units supported by the city are either given new home
     cities or maybe destroyed */
  unit_list_iterate(*units, vunit) {
    struct city *new_home_city = map_get_city(vunit->x, vunit->y);
    if (new_home_city && new_home_city != exclude_city) {
      /* unit is in another city: make that the new homecity,
	 unless that city is actually the same city (happens if disbanding) */
      transfer_unit(vunit, new_home_city, verbose);
    } else if (kill_outside == -1
	       || real_map_distance(vunit->x, vunit->y, x, y) <= kill_outside) {
      /* else transfer to specified city. */
      transfer_unit(vunit, pcity, verbose);
    }

    /* not deleting cargo as it may be carried by units transfered later.
       no cargo deletion => no trouble with "units" list.
       In cases where the cargo can be left without transport the calling
       function should take that into account. */
    wipe_unit_spec_safe(vunit, NULL, FALSE);
  } unit_list_iterate_end;
}

/**********************************************************************
dist_nearest_city (in ai.c) does not seem to do what I want or expect
this function finds the closest friendly city to pos x,y.  I'm sure 
there must be a similar function somewhere, I just can't find it.

                               - Kris Bubendorfer 

If sea_required, returned city must be adjacent to ocean.
If pexclcity, do not return it as the closest city.
Returns NULL if no satisfactory city can be found.
***********************************************************************/
struct city *find_closest_owned_city(struct player *pplayer, int x, int y,
				     bool sea_required, struct city *pexclcity)
{
  int dist = -1;
  struct city *rcity = NULL;
  city_list_iterate(pplayer->cities, pcity)
    if ((real_map_distance(x, y, pcity->x, pcity->y) < dist || dist == -1) && 
	(!sea_required || is_terrain_near_tile(pcity->x, pcity->y, T_OCEAN)) &&
	(!pexclcity || (pexclcity != pcity))) {
      dist = real_map_distance(x, y, pcity->x, pcity->y);
      rcity = pcity;
    }      
  city_list_iterate_end;

  return rcity;
}

/**************************************************************************
  called when a player conquers a city, remove buildings (not wonders and 
  always palace) with game.razechance% chance, barbarians destroy more
  set the city's shield stock to 0
**************************************************************************/
static void raze_city(struct city *pcity)
{
  int razechance = game.razechance;

  /* We don't use city_remove_improvement here as the global effects
     stuff has already been handled by transfer_city */
  pcity->improvements[B_PALACE]=I_NONE;

  /* land barbarians are more likely to destroy city improvements */
  if (is_land_barbarian(city_owner(pcity)))
    razechance += 30;

  built_impr_iterate(pcity, i) {
    if (!is_wonder(i) && (myrand(100) < razechance)) {
      pcity->improvements[i]=I_NONE;
    }
  } built_impr_iterate_end;

  nullify_prechange_production(pcity);
  pcity->shield_stock = 0;
}

/**************************************************************************
The following has to be called every time a city, pcity, has changed
owner to update the city's traderoutes.
**************************************************************************/
static void reestablish_city_trade_routes(struct city *pcity, int cities[]) 
{
  int i;
  struct city *oldtradecity;

  for (i = 0; i < NUM_TRADEROUTES; i++) {
    if (cities[i] != 0) {
      oldtradecity = find_city_by_id(cities[i]);
      assert(oldtradecity != NULL);
      if (can_establish_trade_route(pcity, oldtradecity)) {   
	establish_trade_route(pcity, oldtradecity);
      }
      /* refresh regardless; either it lost a trade route or
	 the trade route revenue changed. */
      city_refresh(oldtradecity);
      send_city_info(city_owner(oldtradecity), oldtradecity);
    }
  }
}

/**************************************************************************
Create a palace in a random city. Used when the capital was conquered.
**************************************************************************/
static void build_free_palace(struct player *pplayer,
			       const char *const old_capital_name)
{
  int size = city_list_size(&pplayer->cities);
  struct city *pnew_capital;

  if (size == 0) {
    /* The last city was removed or transfered to the enemy. R.I.P. */
    return;
  }

  assert(find_palace(pplayer) == NULL);

  pnew_capital = city_list_get(&pplayer->cities, myrand(size));

  city_add_improvement(pnew_capital, B_PALACE);

  /*
   * send_player_cities will recalculate all cities and send them to
   * the client.
   */
  send_player_cities(pplayer);

  notify_player(pplayer, _("Game: You lost your capital %s. A new palace "
			   "was built in %s."), old_capital_name,
		pnew_capital->name);

  /* 
   * The enemy want to see the new capital in his intelligence
   * report. 
   */
  send_city_info(NULL, pnew_capital);
}

/**********************************************************************
Handles all transactions in relation to transferring a city.

The kill_outside and transfer_unit_verbose arguments are passed to
transfer_city_units(), which is called in the middle of the function.
***********************************************************************/
void transfer_city(struct player *ptaker, struct city *pcity,
		   int kill_outside, bool transfer_unit_verbose,
		   bool resolve_stack, bool raze)
{
  struct map_position *resolve_list = NULL;
  int i, no_units = 0;
  struct unit_list old_city_units;
  struct player *pgiver = city_owner(pcity);
  int old_trade_routes[NUM_TRADEROUTES];
  bool had_palace = pcity->improvements[B_PALACE] != I_NONE;
  char old_city_name[MAX_LEN_NAME];

  assert(pgiver != ptaker);

  unit_list_init(&old_city_units);
  unit_list_iterate(pcity->units_supported, punit) {
    unit_list_insert(&old_city_units, punit);
    /* otherwise we might delete the homecity from under the unit
       in the client. */
    punit->homecity = 0;
    send_unit_info(NULL, punit);
  } unit_list_iterate_end;
  unit_list_unlink_all(&pcity->units_supported);
  unit_list_init(&pcity->units_supported);

  /* Remove all global improvement effects that this city confers (but
     then restore the local improvement list - we need this to restore the
     global effects for the new city owner) */
  built_impr_iterate(pcity, i) {
    city_remove_improvement(pcity, i);
    pcity->improvements[i] = I_ACTIVE;
  } built_impr_iterate_end;

  give_citymap_from_player_to_player(pcity, pgiver, ptaker);
  map_unfog_pseudo_city_area(ptaker, pcity->x, pcity->y);

  sz_strlcpy(old_city_name, pcity->name);
  if (game.allowed_city_names == 1
      && city_list_find_name(&ptaker->cities, pcity->name)) {
    sz_strlcpy(pcity->name,
	       city_name_suggestion(ptaker, pcity->x, pcity->y));
    notify_player_ex(ptaker, pcity->x, pcity->y, E_NOEVENT,
		     _("You already had a city called %s."
		       " The city was renamed to %s."), old_city_name,
		     pcity->name);
  }

  /* Has to follow the unfog call above. */
  city_list_unlink(&pgiver->cities, pcity);
  pcity->owner = ptaker->player_no;
  city_list_insert(&ptaker->cities, pcity);

  /* transfer_city_units() destroys the city's units_supported
     list; we save the list so we can resolve units afterwards. */
  if (resolve_stack) {
    no_units = unit_list_size(&old_city_units);
    if (no_units > 0) {
      resolve_list = fc_malloc((no_units + 1) * sizeof(struct map_position));
      i = 0;
      unit_list_iterate(old_city_units, punit) {
	resolve_list[i].x = punit->x;
	resolve_list[i].y = punit->y;
	i++;
      } unit_list_iterate_end;
      resolve_list[i].x = pcity->x;
      resolve_list[i].y = pcity->y;
      assert(i == no_units);
    }
  }

  transfer_city_units(ptaker, pgiver, &old_city_units,
		      pcity, NULL,
		      kill_outside, transfer_unit_verbose);
  /* The units themselves are allready freed by transfer_city_units. */
  unit_list_unlink_all(&old_city_units);
  reset_move_costs(pcity->x, pcity->y);

  if (resolve_stack && (no_units > 0) && resolve_list) {
    for (i = 0; i < no_units+1 ; i++)
      resolve_unit_stack(resolve_list[i].x, resolve_list[i].y,
			 transfer_unit_verbose);
    free(resolve_list);
    resolve_list = NULL;
  }

  /* Update the city's trade routes. */
  for (i = 0; i < NUM_TRADEROUTES; i++)
    old_trade_routes[i] = pcity->trade[i];
  for (i = 0; i < NUM_TRADEROUTES; i++) {
    struct city *pother_city = find_city_by_id(pcity->trade[i]);

    assert(pcity->trade[i] == 0 || pother_city != NULL);

    if (pother_city) {
      remove_trade_route(pother_city, pcity);
    }
  }
  reestablish_city_trade_routes(pcity, old_trade_routes);

  /*
   * Give the new owner infos about all cities which have a traderoute
   * with the transfered city.
   */
  for (i = 0; i < NUM_TRADEROUTES; i++) {
    struct city *pother_city = find_city_by_id(pcity->trade[i]);
    if (pother_city) {
      update_dumb_city(ptaker, pother_city);
      send_city_info(ptaker, pother_city);
    }
  }

  city_refresh(pcity);
  map_city_radius_iterate(pcity->x, pcity->y, x, y) {
    update_city_tile_status_map(pcity, x, y);
  } map_city_radius_iterate_end;
  auto_arrange_workers(pcity);
  initialize_infrastructure_cache(pcity);
  if (raze)
    raze_city(pcity);

  /* Restore any global improvement effects that this city confers */
  built_impr_iterate(pcity, i) {
    city_add_improvement(pcity, i);
  } built_impr_iterate_end;
  update_all_effects();

  /* If the city was building something we haven't invented we
     must change production. */
  /* advisor_choose_build(pcity);  we add the civ bug here :) */

  send_city_info(NULL, pcity);

  /* What wasn't obsolete for the old owner may be so now. */
  remove_obsolete_buildings_city(pcity, TRUE);

  if (terrain_control.may_road
      && player_knows_techs_with_flag (ptaker, TF_RAILROAD)
      && !map_has_special(pcity->x, pcity->y, S_RAILROAD)) {
    notify_player(ptaker,
		  _("Game: The people in %s are stunned by your"
		    " technological insight!\n"
		    "      Workers spontaneously gather and upgrade"
		    " the city with railroads."),
		  pcity->name);
    map_set_special(pcity->x, pcity->y, S_RAILROAD);
    send_tile_info(NULL, pcity->x, pcity->y);
  }

  map_fog_pseudo_city_area(pgiver, pcity->x, pcity->y);
  maybe_make_first_contact(pcity->x, pcity->y, ptaker);

  gamelog(GAMELOG_LOSEC, _("%s lose %s (%i,%i)"),
	  get_nation_name_plural(pgiver->nation), pcity->name, pcity->x,
	  pcity->y);

  /* Build a new palace for free if the player lost her capital and
     savepalace is on. */
  if (had_palace && game.savepalace) {
    build_free_palace(pgiver, pcity->name);
  }

  sync_cities();
}

/**************************************************************************
...
**************************************************************************/
void create_city(struct player *pplayer, const int x, const int y,
		 const char *name)
{
  struct city *pcity;
  int i, x_itr, y_itr;

  freelog(LOG_DEBUG, "Creating city %s", name);
  gamelog(GAMELOG_FOUNDC, _("%s (%i, %i) founded by the %s"), name, x, y,
	  get_nation_name_plural(pplayer->nation));

  if (terrain_control.may_road) {
    map_set_special(x, y, S_ROAD);
    if (player_knows_techs_with_flag(pplayer, TF_RAILROAD))
      map_set_special(x, y, S_RAILROAD);
  }
  send_tile_info(NULL, x, y);

  pcity=fc_malloc(sizeof(struct city));

  pcity->id=get_next_id_number();
  idex_register_city(pcity);
  pcity->owner=pplayer->player_no;
  pcity->x=x;
  pcity->y=y;
  sz_strlcpy(pcity->name, name);
  pcity->size=1;
  pcity->ppl_elvis=1;
  pcity->ppl_scientist=pcity->ppl_taxman=0;
  pcity->ppl_happy[4] = 0;
  pcity->ppl_content[4] = 1;
  pcity->ppl_unhappy[4] = 0;
  pcity->ppl_angry[4] = 0;
  pcity->was_happy = FALSE;
  pcity->steal=0;
  for (i = 0; i < NUM_TRADEROUTES; i++)
    pcity->trade_value[i]=pcity->trade[i]=0;
  pcity->food_stock=0;
  pcity->shield_stock=0;
  pcity->trade_prod=0;
  pcity->original = pplayer->player_no;
  pcity->is_building_unit = TRUE;
  pcity->turn_founded = game.turn;
  pcity->did_buy = TRUE;
  pcity->did_sell = FALSE;
  pcity->airlift = FALSE;
  pcity->currently_building=best_role_unit(pcity, L_FIRSTBUILD);

  /* Set up the worklist */
  init_worklist(&pcity->worklist);

  /* Initialise list of improvements with City- and Building-wide equiv_range */
  improvement_status_init(pcity->improvements,
			  ARRAY_SIZE(pcity->improvements));

  /* Initialise city's vector of improvement effects. */
  ceff_vector_init(&pcity->effects);

  if (!pplayer->capital) {
    pplayer->capital = TRUE;
    city_add_improvement(pcity,B_PALACE);
    update_all_effects();
  }
  pcity->turn_last_built = game.year;
  pcity->turn_changed_target = game.year;
  pcity->changed_from_id = 0;
  pcity->changed_from_is_unit = FALSE;
  pcity->before_change_shields = 0;
  pcity->disbanded_shields = 0;
  pcity->caravan_shields = 0;
  pcity->anarchy=0;
  pcity->rapture=0;

  pcity->city_options = CITYOPT_DEFAULT;
  
  pcity->ai.ai_role = AICITY_NONE;
  pcity->ai.trade_want = TRADE_WEIGHTING; 
  memset(pcity->ai.building_want, 0, sizeof(pcity->ai.building_want));
  pcity->ai.workremain = 1; /* there's always work to be done! */
  pcity->ai.danger = -1; /* flag, may come in handy later */
  pcity->corruption = 0;
  pcity->shield_bonus = 100;
  pcity->tax_bonus = 100;
  pcity->science_bonus = 100;

  /* Before arranging workers to show unknown land */
  map_unfog_pseudo_city_area(pplayer, x, y);

  map_set_city(x, y, pcity);

  unit_list_init(&pcity->units_supported);
  city_list_insert(&pplayer->cities, pcity);
  add_city_to_minimap(x, y);

  /* it is possible to build a city on a tile that is already worked
   * this will displace the worker on the newly-built city's tile -- Syela */
  for (y_itr = 0; y_itr < CITY_MAP_SIZE; y_itr++) {
    for (x_itr = 0; x_itr < CITY_MAP_SIZE; x_itr++) {
      if (is_valid_city_coords(x_itr, y_itr)
	  && city_can_work_tile(pcity, x_itr, y_itr))
	pcity->city_map[x_itr][y_itr] = C_TILE_EMPTY;
      else
	pcity->city_map[x_itr][y_itr] = C_TILE_UNAVAILABLE;
    }
  }

  server_set_tile_city(pcity, CITY_MAP_SIZE/2, CITY_MAP_SIZE/2, C_TILE_WORKER);
  auto_arrange_workers(pcity);

  city_refresh(pcity);

  city_incite_cost(pcity);

  /* Put vision back to normal, if fortress acted as a watchtower */
  if (player_knows_techs_with_flag(pplayer, TF_WATCHTOWER)
      && map_has_special(x, y, S_FORTRESS)) {
    unit_list_iterate(map_get_tile(x, y)->units, punit) {
      unfog_area(pplayer, punit->x, punit->y,
		 unit_type(punit)->vision_range);
      fog_area(pplayer, punit->x, punit->y, get_watchtower_vision(punit));
    }
    unit_list_iterate_end;
  }
  map_clear_special(x, y, S_FORTRESS);
  send_tile_info(NULL, x, y);

  initialize_infrastructure_cache(pcity);
  reset_move_costs(x, y);
/* I stupidly thought that setting S_ROAD took care of this, but of course
the city_id isn't set when S_ROAD is set, so reset_move_costs doesn't allow
sea movement at the point it's called.  This led to a problem with the
warmap (but not the GOTOmap warmap) which meant the AI was very reluctant
to use ferryboats.  I really should have identified this sooner. -- Syela */

  pcity->synced = FALSE;
  send_city_info(NULL, pcity);
  sync_cities(); /* Will also send pcity. */

  notify_player_ex(pplayer, x, y, E_CITY_BUILD,
		   _("Game: You have founded %s"), pcity->name);
  maybe_make_first_contact(x, y, city_owner(pcity));

  /* Catch fortress building, transforming into ocean, etc. */
  unit_list_iterate(map_get_tile(x, y)->units, punit) {
    if (!can_unit_continue_current_activity(punit))
      handle_unit_activity_request(punit, ACTIVITY_IDLE);
  } unit_list_iterate_end;
}

/**************************************************************************
...
**************************************************************************/
void remove_city(struct city *pcity)
{
  int o, x, y;
  struct player *pplayer = city_owner(pcity);
  struct tile *ptile = map_get_tile(pcity->x, pcity->y);
  bool effect_update, had_palace = pcity->improvements[B_PALACE] != I_NONE;
  char *city_name = strdup(pcity->name);

  gamelog(GAMELOG_LOSEC, _("%s lose %s (%i,%i)"),
	  get_nation_name_plural(pplayer->nation), pcity->name, pcity->x,
	  pcity->y);

  /* Explicitly remove all improvements, to properly remove any global effects
     and to handle the preservation of "destroyed" effects. */
  effect_update=FALSE;

  built_impr_iterate(pcity, i) {
    effect_update = TRUE;
    city_remove_improvement(pcity, i);
  } built_impr_iterate_end;

  if (effect_update)
    update_all_effects();

  /* This is cutpasted with modifications from transfer_city_units. Yes, it is ugly.
     But I couldn't see a nice way to make them use the same code */
  unit_list_iterate(pcity->units_supported, punit) {
    struct city *new_home_city = map_get_city(punit->x, punit->y);
    x = punit->x; y = punit->y;
    if (new_home_city && new_home_city != pcity) {
      /* unit is in another city: make that the new homecity,
	 unless that city is actually the same city (happens if disbanding) */
      freelog(LOG_VERBOSE, "Changed homecity of %s in %s",
	      unit_name(punit->type), new_home_city->name);
      notify_player(pplayer, _("Game: Changed homecity of %s in %s."),
		    unit_name(punit->type), new_home_city->name);
      (void) create_unit_full(city_owner(new_home_city), x, y,
		       punit->type, punit->veteran, new_home_city->id,
		       punit->moves_left, punit->hp);
    }

    wipe_unit_safe(punit, &myiter);
  } unit_list_iterate_end;

  x = pcity->x;
  y = pcity->y;

  /* make sure ships are not left on land when city is removed. */
 MOVE_SEA_UNITS:
  unit_list_iterate(ptile->units, punit) {
    bool moved;
    if (!punit
	|| !same_pos(punit->x, punit->y, x, y)
	|| !is_sailing_unit(punit)) {
      continue;
    }

    handle_unit_activity_request(punit, ACTIVITY_IDLE);
    moved = FALSE;
    adjc_iterate(x, y, x1, y1) {
      if (map_get_terrain(x1, y1) == T_OCEAN) {
	if (could_unit_move_to_tile(punit, x, y, x1, y1) == 1) {
	  moved = handle_unit_move_request(punit, x1, y1, FALSE, TRUE);
	  if (moved) {
	    notify_player_ex(unit_owner(punit), -1, -1, E_NOEVENT,
			     _("Game: Moved %s out of disbanded city %s "
			       "to avoid being landlocked."),
			     unit_type(punit)->name, pcity->name);
	    goto OUT;
	  }
	}
      }
    } adjc_iterate_end;
  OUT:
    if (!moved) {
      notify_player_ex(unit_owner(punit), -1, -1, E_NOEVENT,
		       _("Game: When %s was disbanded your %s could not "
			 "get out, and it was therefore stranded."),
		       pcity->name, unit_type(punit)->name);
      wipe_unit(punit);
    }
    /* We just messed with the unit list. Avoid trouble by starting over.
       Note that the problem is reduced by one unit, so no loop trouble. */
    goto MOVE_SEA_UNITS;
  } unit_list_iterate_end;

  for (o = 0; o < NUM_TRADEROUTES; o++) {
    struct city *pother_city = find_city_by_id(pcity->trade[o]);

    assert(pcity->trade[o] == 0 || pother_city != NULL);

    if (pother_city) {
      remove_trade_route(pother_city, pcity);
    }
  }

  /* idex_unregister_city() is called in game_remove_city() below */

  /* dealloc_id(pcity->id); We do NOT dealloc cityID's as the cities may still be
     alive in the client. As the number of removed cities is small the leak is
     acceptable. */

/* DO NOT remove city from minimap here. -- Syela */
  
  game_remove_city(pcity);

  players_iterate(other_player) {
    if (map_get_known_and_seen(x, y, other_player)) {
      reality_check_city(other_player, x, y);
    }
  } players_iterate_end;

  map_fog_pseudo_city_area(pplayer, x, y);

  reset_move_costs(x, y);

  /* Update available tiles in adjacent cities. */
  map_city_radius_iterate(x, y, x1, y1) {
    /* For every tile the city could have used. */
    map_city_radius_iterate(x1, y1, x2, y2) {
      /* We see what cities are inside reach of the tile. */
      struct city *pcity = map_get_city(x2, y2);
      if (pcity) {
	update_city_tile_status_map(pcity, x1, y1);
      }
    } map_city_radius_iterate_end;
  } map_city_radius_iterate_end;

  /* Build a new palace for free if the player lost her capital and
     savepalace is on. */
  if (had_palace && game.savepalace) {
    build_free_palace(pplayer, city_name);
    free(city_name);
  }

  sync_cities();
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_enter_city(struct unit *punit, struct city *pcity)
{
  bool do_civil_war = FALSE;
  int coins;
  struct player *pplayer = unit_owner(punit);
  struct player *cplayer = city_owner(pcity);

  /* if not at war, may peacefully enter city */
  if (!pplayers_at_war(pplayer, cplayer))
    return;

  /* okay, we're at war - invader captures/destroys city... */
  
  /* If a capital is captured, then spark off a civil war 
     - Kris Bubendorfer
     Also check spaceships --dwp
  */
  if(city_got_building(pcity, B_PALACE)
     && ((cplayer->spaceship.state == SSHIP_STARTED)
	 || (cplayer->spaceship.state == SSHIP_LAUNCHED))) {
    spaceship_lost(cplayer);
  }
  
  if (city_got_building(pcity, B_PALACE)
      && city_list_size(&cplayer->cities) >= game.civilwarsize
      && game.nplayers < game.nation_count
      && game.civilwarsize < GAME_MAX_CIVILWARSIZE
      && get_num_human_and_ai_players() < MAX_NUM_PLAYERS
      && civil_war_triggered(cplayer)) {
    do_civil_war = TRUE;
  }

  /* 
   * We later remove a citizen. Lets check if we can save this since
   * the city will be destroyed.
   */
  if (pcity->size<=1) {
    notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT,
		     _("Game: You destroy %s completely."), pcity->name);
    notify_player_ex(cplayer, pcity->x, pcity->y, E_CITY_LOST, 
		     _("Game: %s has been destroyed by %s."), 
		     pcity->name, pplayer->name);
    gamelog(GAMELOG_LOSEC, _("%s (%s) (%i,%i) destroyed by %s"), pcity->name,
	    get_nation_name(city_owner(pcity)->nation), pcity->x, pcity->y,
	    get_nation_name_plural(pplayer->nation));
    remove_city_from_minimap(pcity->x, pcity->y);
    remove_city(pcity);
    if (do_civil_war)
      civil_war(cplayer);
    return;
  }

  city_reduce_size(pcity, 1);
  coins=cplayer->economic.gold;
  coins=myrand((coins/20)+1)+(coins*(pcity->size))/200;
  pplayer->economic.gold+=coins;
  cplayer->economic.gold-=coins;
  send_player_info(cplayer, cplayer);
  if (pcity->original != pplayer->player_no) {
    notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT, 
		     _("Game: You conquer %s, your lootings accumulate"
		       " to %d gold!"), 
		     pcity->name, coins);
    notify_player_ex(cplayer, pcity->x, pcity->y, E_CITY_LOST, 
		     _("Game: %s conquered %s and looted %d gold"
		       " from the city."),
		     pplayer->name, pcity->name, coins);
    gamelog(GAMELOG_CONQ, _("%s (%s) (%i,%i) conquered by %s"), pcity->name,
	    get_nation_name(city_owner(pcity)->nation), pcity->x, pcity->y,
	    get_nation_name_plural(pplayer->nation));
  } else {
    notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT, 
		     _("Game: You have liberated %s!!"
		       " lootings accumulate to %d gold."),
		     pcity->name, coins);
    
    notify_player_ex(cplayer, pcity->x, pcity->y, E_CITY_LOST, 
		     _("Game: %s liberated %s and looted %d gold"
		       " from the city."),
		     pplayer->name, pcity->name, coins);
    gamelog(GAMELOG_CONQ, _("%s (%s) (%i,%i) liberated by %s"), pcity->name,
	    get_nation_name(city_owner(pcity)->nation), pcity->x, pcity->y,
	    get_nation_name_plural(pplayer->nation));
  }

  get_a_tech(pplayer, cplayer);
  make_partisans(pcity);

  transfer_city(pplayer, pcity , 0, FALSE, TRUE, TRUE);
  send_player_info(pplayer, pplayer); /* Update techs */

  if (do_civil_war)
    civil_war(cplayer);
}

/**************************************************************************
  Returns true if the player owns a city which has a traderoute with
  the given city.
**************************************************************************/
static bool player_has_traderoute_with_city(struct player *pplayer,
					   struct city *pcity)
{
  int i;

  for (i = 0; i < NUM_TRADEROUTES; i++) {
    struct city *other = find_city_by_id(pcity->trade[i]);
    if (other && city_owner(other) == pplayer) {
      return TRUE;
    }
  }
  return FALSE;
}

/**************************************************************************
This fills out a package from a players dumb_city.
**************************************************************************/
static void package_dumb_city(struct player* pplayer, int x, int y,
			      struct packet_short_city *packet)
{
  struct dumb_city *pdcity = map_get_player_tile(x, y, pplayer)->city;
  struct city *pcity = map_get_city(x, y);

  packet->id=pdcity->id;
  packet->owner=pdcity->owner;
  packet->x=x;
  packet->y=y;
  sz_strlcpy(packet->name, pdcity->name);

  packet->size=pdcity->size;
  if (map_get_known_and_seen(x, y, pplayer)) {
    /* Since the tile is visible the player can see the tile,
       and if it didn't actually have a city pdcity would be NULL */
    assert(pcity != NULL);
    packet->happy = !city_unhappy(pcity);
  } else {
    packet->happy = TRUE;
  }

  if (pcity && pcity->id == pdcity->id && city_got_building(pcity, B_PALACE))
    packet->capital = TRUE;
  else
    packet->capital = FALSE;

  packet->walls = pdcity->has_walls;

  if (pcity && player_has_traderoute_with_city(pplayer, pcity)) {
    packet->tile_trade = pcity->tile_trade;
  } else {
    packet->tile_trade = 0;
  }
}

/**************************************************************************
  Broadcast info about a city to all players who observe the tile. 
  If the player can see the city we update the city info first.
  If not we just use the info from the players private map.
  See also comments to send_city_info_at_tile().
  (Split off from send_city_info_at_tile() because that was getting
  too difficult for me to understand... --dwp)
**************************************************************************/
static void broadcast_city_info(struct city *pcity)
{
  struct player *powner = city_owner(pcity);
  struct packet_city_info packet;
  struct packet_short_city sc_pack;

  /* nocity_send is used to inhibit sending cities to the owner between
   * turn updates
   */
  if (!nocity_send) {    /* first send all info to the owner */
    update_dumb_city(powner, pcity);
    package_city(pcity, &packet, FALSE);
    lsend_packet_city_info(&powner->connections, &packet);
  }

  /* send to all others who can see the city: */
  players_iterate(pplayer) {
    if(city_owner(pcity) == pplayer) continue; /* already sent above */
    if (map_get_known_and_seen(pcity->x, pcity->y, pplayer) ||
	player_has_traderoute_with_city(pplayer, pcity)) {
      update_dumb_city(pplayer, pcity);
      package_dumb_city(pplayer, pcity->x, pcity->y, &sc_pack);
      lsend_packet_short_city(&pplayer->connections, &sc_pack);
    }
  } players_iterate_end;

  /* send to non-player observers:
   * should these only get dumb_city type info?
   */
  conn_list_iterate(game.game_connections, pconn) {
    if (!pconn->player && pconn->observer) {
      package_city(pcity, &packet, FALSE);
      send_packet_city_info(pconn, &packet);
    }
  }
  conn_list_iterate_end;
}

/**************************************************************************
  Send to each client information about the cities it knows about.
  dest may not be NULL
**************************************************************************/
void send_all_known_cities(struct conn_list *dest)
{
  conn_list_do_buffer(dest);
  conn_list_iterate(*dest, pconn) {
    struct player *pplayer = pconn->player;
    if (!pplayer && !pconn->observer) {
      continue;
    }
    whole_map_iterate(x, y) {
      if (!pplayer || map_get_player_tile(x, y, pplayer)->city) {
	send_city_info_at_tile(pplayer, &pconn->self, NULL, x, y);
      }
    } whole_map_iterate_end;
  }
  conn_list_iterate_end;
  conn_list_do_unbuffer(dest);
  flush_packets();
}

/**************************************************************************
...
**************************************************************************/
void send_player_cities(struct player *pplayer)
{
  city_list_iterate(pplayer->cities, pcity) {
    city_refresh(pcity);
    send_city_info(pplayer, pcity);
  }
  city_list_iterate_end;
}


/**************************************************************************
  A wrapper, accessing either broadcast_city_info() (dest==NULL),
  or a convenience case of send_city_info_at_tile().
  Must specify non-NULL pcity.
**************************************************************************/
void send_city_info(struct player *dest, struct city *pcity)
{
  assert(pcity != NULL);

  if (server_state != RUN_GAME_STATE && server_state != GAME_OVER_STATE)
    return;

  if (dest == city_owner(pcity) && nocity_send)
    return;

  if (!dest || dest == city_owner(pcity))
    pcity->synced = TRUE;
  if (!dest) {
    broadcast_city_info(pcity);
  } else {
    send_city_info_at_tile(dest, &dest->connections, pcity, pcity->x, pcity->y);
  }
}

/**************************************************************************
Send info about a city, as seen by pviewer, to dest (usually dest will
be pviewer->connections). If pplayer can see the city we update the city
info first. If not we just use the info from the players private map.

If (pviewer == NULL) this is for observers, who see everything (?)
For this function dest may not be NULL.  See send_city_info() and
broadcast_city_info().

If pcity is non-NULL it should be same as map_get_city(x,y); if pcity
is NULL, this function calls map_get_city(x,y) (it is ok if this
returns NULL).

Sometimes a player's map contain a city that doesn't actually exist. Use
reality_check_city(pplayer, x,y) to update that. Remember to NOT send info
about a city to a player who thinks the tile contains another city. If you
want to update the clients info of the tile you must use
reality_check_city(pplayer, x, y) first. This is generally taken care of
automatically when a tile becomes visible.
**************************************************************************/
void send_city_info_at_tile(struct player *pviewer, struct conn_list *dest,
			    struct city *pcity, int x, int y)
{
  struct player *powner = NULL;
  struct packet_city_info packet;
  struct packet_short_city sc_pack;
  struct dumb_city *pdcity;

  if (!pcity)
    pcity = map_get_city(x,y);
  if (pcity)
    powner = city_owner(pcity);

  if (powner == pviewer) {
    /* send info to owner */
    /* This case implies powner non-NULL which means pcity non-NULL */
    /* nocity_send is used to inhibit sending cities to the owner between
       turn updates */
    if (!nocity_send) {
      /* send all info to the owner */
      update_dumb_city(powner, pcity);
      package_city(pcity, &packet, FALSE);
      lsend_packet_city_info(dest, &packet);
    }
  }
  else {
    /* send info to non-owner */
    if (!pviewer) {	/* observer */
      if (pcity) {
	package_city(pcity, &packet, FALSE);   /* should be dumb_city info? */
	lsend_packet_city_info(dest, &packet);
      }
    } else if (map_get_known_and_seen(x, y, pviewer)) {
      if (pcity) { /* it's there and we see it; update and send */
	update_dumb_city(pviewer, pcity);
	package_dumb_city(pviewer, x, y, &sc_pack);
	lsend_packet_short_city(dest, &sc_pack);
      }
    } else { /* not seen; send old info */
      pdcity = map_get_player_tile(x, y, pviewer)->city;
      if (pdcity) {
	package_dumb_city(pviewer, x, y, &sc_pack);
	lsend_packet_short_city(dest, &sc_pack);
      }
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void package_city(struct city *pcity, struct packet_city_info *packet,
		  bool dipl_invest)
{
  int i, x, y;
  char *p;
  packet->id=pcity->id;
  packet->owner=pcity->owner;
  packet->x=pcity->x;
  packet->y=pcity->y;
  sz_strlcpy(packet->name, pcity->name);

  packet->size=pcity->size;
  for (i=0;i<5;i++) {
    packet->ppl_happy[i]=pcity->ppl_happy[i];
    packet->ppl_content[i]=pcity->ppl_content[i];
    packet->ppl_unhappy[i]=pcity->ppl_unhappy[i];
    packet->ppl_angry[i]=pcity->ppl_angry[i];
  }
  packet->ppl_elvis=pcity->ppl_elvis;
  packet->ppl_scientist=pcity->ppl_scientist;
  packet->ppl_taxman=pcity->ppl_taxman;
  for (i = 0; i < NUM_TRADEROUTES; i++) {
    packet->trade[i]=pcity->trade[i];
    packet->trade_value[i]=pcity->trade_value[i];
  }

  packet->food_prod=pcity->food_prod;
  packet->food_surplus=pcity->food_surplus;
  packet->shield_prod=pcity->shield_prod;
  packet->shield_surplus=pcity->shield_surplus;
  packet->trade_prod=pcity->trade_prod;
  packet->tile_trade=pcity->tile_trade;
  packet->corruption=pcity->corruption;
  
  packet->luxury_total=pcity->luxury_total;
  packet->tax_total=pcity->tax_total;
  packet->science_total=pcity->science_total;
  
  packet->food_stock=pcity->food_stock;
  packet->shield_stock=pcity->shield_stock;
  packet->pollution=pcity->pollution;

  packet->city_options=pcity->city_options;
  
  packet->is_building_unit=pcity->is_building_unit;
  packet->currently_building=pcity->currently_building;

  packet->turn_last_built=pcity->turn_last_built;
  packet->turn_changed_target=pcity->turn_changed_target;
  packet->turn_founded = pcity->turn_founded;
  packet->changed_from_id=pcity->changed_from_id;
  packet->changed_from_is_unit=pcity->changed_from_is_unit;
  packet->before_change_shields=pcity->before_change_shields;
  packet->disbanded_shields=pcity->disbanded_shields;
  packet->caravan_shields=pcity->caravan_shields;

  copy_worklist(&packet->worklist, &pcity->worklist);
  packet->diplomat_investigate=dipl_invest;

  packet->airlift=pcity->airlift;
  packet->did_buy=pcity->did_buy;
  packet->did_sell=pcity->did_sell;
  packet->was_happy=pcity->was_happy;
  p=packet->city_map;
  for(y=0; y<CITY_MAP_SIZE; y++)
    for(x=0; x<CITY_MAP_SIZE; x++)
      *p++=get_worker_city(pcity, x, y)+'0';
  *p='\0';

  p=packet->improvements;

  impr_type_iterate(i) {
    *p++ = (city_got_building(pcity, i)) ? '1' : '0';
  } impr_type_iterate_end;

  *p='\0';
}

/**************************************************************************
updates a players knowledge about a city. If the player_tile already
contains a city it must be the same city (avoid problems by always calling
reality_check city first)
**************************************************************************/
void update_dumb_city(struct player *pplayer, struct city *pcity)
{
  struct player_tile *plrtile = map_get_player_tile(pcity->x, pcity->y,
						    pplayer);
  struct dumb_city *pdcity;
  if (!plrtile->city) {
    plrtile->city = fc_malloc(sizeof(struct dumb_city));
    plrtile->city->id = pcity->id;
  }
  pdcity = plrtile->city;
  if (pdcity->id != pcity->id) {
    freelog(LOG_ERROR, "Trying to update old city (wrong ID)"
	    " at %i,%i for player %s", pcity->x, pcity->y, pplayer->name);
    pdcity->id = pcity->id;   /* ?? */
  }
  sz_strlcpy(pdcity->name, pcity->name);
  pdcity->size = pcity->size;
  pdcity->has_walls = city_got_citywalls(pcity);
  pdcity->owner = pcity->owner;
}

/**************************************************************************
Removes outdated (nonexistant) cities from a player
**************************************************************************/
void reality_check_city(struct player *pplayer,int x, int y)
{
  struct packet_generic_integer packet;
  struct city *pcity;
  struct dumb_city *pdcity = map_get_player_tile(x, y, pplayer)->city;

  if (pdcity) {
    pcity = map_get_tile(x,y)->city;
    if (!pcity || (pcity && pcity->id != pdcity->id)) {
      packet.value=pdcity->id;
      lsend_packet_generic_integer(&pplayer->connections, PACKET_REMOVE_CITY,
				   &packet);
      free(pdcity);
      map_get_player_tile(x, y, pplayer)->city = NULL;
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void remove_trade_route(struct city *pc1, struct city *pc2)
{
  int i;

  assert(pc1 && pc2);

  for (i = 0; i < NUM_TRADEROUTES; i++) {
    if (pc1->trade[i] == pc2->id)
      pc1->trade[i] = 0;
    if (pc2->trade[i] == pc1->id)
      pc2->trade[i] = 0;
  }
}

/**************************************************************************
Establish a trade route, notice that there has to be space for them, 
So use can_establish_Trade_route first.
returns the revenue aswell.
**************************************************************************/
int establish_trade_route(struct city *pc1, struct city *pc2)
{
  int i, tb;

  for (i = 0; i < NUM_TRADEROUTES; i++) {
    if (pc1->trade[i] == 0) {
      pc1->trade[i]=pc2->id;
      break;
    }
  }
  for (i = 0; i < NUM_TRADEROUTES; i++) {
    if (pc2->trade[i] == 0) {
      pc2->trade[i]=pc1->id;
      break;
    }
  }

  tb=(map_distance(pc1->x, pc1->y, pc2->x, pc2->y)+10);
/* should this be real_map_distance?  Leaving for now -- Syela */
  tb=(tb*(pc1->trade_prod+pc2->trade_prod))/24;
  /* 
   * fudge factor to more closely approximate Civ2 behavior (Civ2 is
   * really very different -- this just fakes it a little better) 
   */
  tb *= 3;

  /* 
   * one time bonus is not affected by factors in
   * trade_between_cities() 
   */
  for (i = 0; i < num_known_tech_with_flag(city_owner(pc1),
					   TF_TRADE_REVENUE_REDUCE); i++) {
    tb = (tb * 2) / 3;
  }
  /* was: A_RAILROAD, A_FLIGHT */
  return tb;
}

/**************************************************************************
...
**************************************************************************/
void do_sell_building(struct player *pplayer, struct city *pcity, int id)
{
  if (!is_wonder(id)) {
    pplayer->economic.gold += improvement_value(id);
    building_lost(pcity, id);
  }
}

/**************************************************************************
...
**************************************************************************/
void building_lost(struct city *pcity, int id)
{
  struct player *owner = city_owner(pcity);

  city_remove_improvement(pcity,id);
  if (id == B_PALACE &&
      ((owner->spaceship.state == SSHIP_STARTED) ||
       (owner->spaceship.state == SSHIP_LAUNCHED))) {
    spaceship_lost(owner);
  }
}

/**************************************************************************
  Change the build target.
**************************************************************************/
void change_build_target(struct player *pplayer, struct city *pcity, 
			 int target, bool is_unit, int event)
{
  char *name;
  char *source;

  /* If the city is already building this thing, don't do anything */
  if (pcity->is_building_unit == is_unit &&
      pcity->currently_building == target)
    return;

  /* Is the city no longer building a wonder? */
  if(!pcity->is_building_unit && is_wonder(pcity->currently_building) &&
     (event != E_IMP_AUTO && event != E_WORKLIST)) {
    /* If the build target is changed because of an advisor's suggestion or
       because the worklist advances, then the wonder was completed -- 
       don't announce that the player has *stopped* building that wonder. 
       */
    notify_player_ex(NULL, pcity->x, pcity->y, E_WONDER_STOPPED,
		     _("Game: The %s have stopped building The %s in %s."),
		     get_nation_name_plural(pplayer->nation),
		     get_impr_name_ex(pcity, pcity->currently_building),
		     pcity->name);
  }

  /* Manage the city change-production penalty.
     (May penalize, restore or do nothing to the shield_stock.) */
  city_change_production_penalty(pcity, target, is_unit, TRUE);

  /* Change build target. */
  pcity->currently_building = target;
  pcity->is_building_unit = is_unit;

  /* What's the name of the target? */
  if (is_unit)
    name = unit_types[pcity->currently_building].name;
  else
    name = improvement_types[pcity->currently_building].name;

  switch (event) {
    case E_WORKLIST: source = _(" from the worklist"); break;
/* Should we give the AI auto code credit?
    case E_IMP_AUTO: source = _(" as suggested by the AI advisor"); break;
*/
    default: source = ""; break;
  }

  /* Tell the player what's up. */
  if (event != 0)
    notify_player_ex(pplayer, pcity->x, pcity->y, event,
		     _("Game: %s is building %s%s."),
		     pcity->name, name, source);
  else
    notify_player_ex(pplayer, pcity->x, pcity->y, E_UNIT_BUILD,
		     _("Game: %s is building %s."), 
		     pcity->name, name);

  /* If the city is building a wonder, tell the rest of the world
     about it. */
  if (!pcity->is_building_unit && is_wonder(pcity->currently_building)) {
    notify_player_ex(NULL, pcity->x, pcity->y, E_WONDER_STARTED,
		     _("Game: The %s have started building The %s in %s."),
		     get_nation_name_plural(pplayer->nation),
		     get_impr_name_ex(pcity, pcity->currently_building),
		     pcity->name);
  }
}





/**************************************************************************
...
**************************************************************************/
bool can_place_worker_here(struct city *pcity, int city_x, int city_y)
{
  return get_worker_city(pcity, city_x, city_y) == C_TILE_EMPTY;
}

/**************************************************************************
Returns if a tile is available to be worked.
Return true if the city itself is currently working the tile (and can
continue.)
city_x, city_y is in city map coords.
**************************************************************************/
bool city_can_work_tile(struct city *pcity, int city_x, int city_y)
{
  int map_x, map_y;
  struct tile *ptile;

  if (!city_map_to_map(&map_x, &map_y, pcity, city_x, city_y))
    return FALSE;
  ptile = map_get_tile(map_x, map_y);

  if (is_enemy_unit_tile(ptile, city_owner(pcity))
      && !is_city_center(city_x, city_y))
    return FALSE;

  if (!map_get_known(map_x, map_y, city_owner(pcity)))
    return FALSE;

  if (ptile->worked && ptile->worked != pcity)
    return FALSE;

  return TRUE;
}

/**************************************************************************
Sets tile worked status.
city_x, city_y is in city map coords.
You need to call sync_cities for the affected cities to be synced with the
client.

You should not use this function unless you really know what you are doing!
Better functions are
server_set_worker_city()
server_remove_worker_city()
update_city_tile_status()
**************************************************************************/
static void server_set_tile_city(struct city *pcity, int city_x, int city_y,
				 enum city_tile_type type)
{
  enum city_tile_type current;
  assert(is_valid_city_coords(city_x, city_y));
  current = pcity->city_map[city_x][city_y];
  assert(current != type);

  set_worker_city(pcity, city_x, city_y, type);
  pcity->synced = FALSE;

  /* Update adjacent cities. */
  {
    int map_x, map_y;
    bool is_real;

    is_real = city_map_to_map(&map_x, &map_y, pcity, city_x, city_y);
    assert(is_real);

    map_city_radius_iterate(map_x, map_y, x1, y1) {
      struct city *pcity2 = map_get_city(x1, y1);
      if (pcity2 && pcity2 != pcity) {
	int city_x2, city_y2;
	bool is_valid;

	is_valid = map_to_city_map(&city_x2, &city_y2, pcity2, map_x,
					 map_y);
	assert(is_valid);
	update_city_tile_status(pcity2, city_x2, city_y2);
      }
    } map_city_radius_iterate_end;
  }
}

/**************************************************************************
city_x, city_y is in city map coords.
You need to call sync_cities for the affected cities to be synced with the
client.
**************************************************************************/
void server_remove_worker_city(struct city *pcity, int city_x, int city_y)
{
  assert(is_valid_city_coords(city_x, city_y));
  assert(get_worker_city(pcity, city_x, city_y) == C_TILE_WORKER);
  server_set_tile_city(pcity, city_x, city_y, C_TILE_EMPTY);
}

/**************************************************************************
city_x, city_y is in city map coords.
You need to call sync_cities for the affected cities to be synced with the
client.
**************************************************************************/
void server_set_worker_city(struct city *pcity, int city_x, int city_y)
{
  assert(is_valid_city_coords(city_x, city_y));
  assert(get_worker_city(pcity, city_x, city_y) == C_TILE_EMPTY);
  server_set_tile_city(pcity, city_x, city_y, C_TILE_WORKER);
}

/**************************************************************************
Wrapper.
You need to call sync_cities for the affected cities to be synced with the
client.
**************************************************************************/
void update_city_tile_status_map(struct city *pcity, int map_x, int map_y)
{
  int city_x, city_y;
  bool is_valid;

  is_valid = map_to_city_map(&city_x, &city_y, pcity, map_x, map_y);
  assert(is_valid);
  update_city_tile_status(pcity, city_x, city_y);
}

/**************************************************************************
Updates the worked status of a tile.
city_x, city_y is in city map coords.
You need to call sync_cities for the affected cities to be synced with the
client.
**************************************************************************/
void update_city_tile_status(struct city *pcity, int city_x, int city_y)
{
  enum city_tile_type current;
  bool is_available;

  assert(is_valid_city_coords(city_x, city_y));

  current = get_worker_city(pcity, city_x, city_y);
  is_available = city_can_work_tile(pcity, city_x, city_y);

  switch (current) {
  case C_TILE_WORKER:
    if (!is_available) {
      server_set_tile_city(pcity, city_x, city_y, C_TILE_UNAVAILABLE);
      add_adjust_workers(pcity); /* will place the displaced */
      city_refresh(pcity);
    }
    break;

  case C_TILE_UNAVAILABLE:
    if (is_available) {
      server_set_tile_city(pcity, city_x, city_y, C_TILE_EMPTY);
    }
    break;

  case C_TILE_EMPTY:
    if (!is_available) {
      server_set_tile_city(pcity, city_x, city_y, C_TILE_UNAVAILABLE);
    }
    break;
  }
}

/**************************************************************************
...
**************************************************************************/
void sync_cities(void)
{
  if (nocity_send)
    return;

  players_iterate(pplayer) {
    city_list_iterate(pplayer->cities, pcity) {
      /* sending will set synced to 1. */
      if (!pcity->synced)
	send_city_info(pplayer, pcity);
    } city_list_iterate_end;
  } players_iterate_end;
}

/**************************************************************************
...
**************************************************************************/
void check_city_workers(struct player *pplayer)
{
  city_list_iterate(pplayer->cities, pcity) {
    city_map_iterate(x, y) {
      update_city_tile_status(pcity, x, y);
    } city_map_iterate_end;
  } city_list_iterate_end;
  sync_cities();
}
