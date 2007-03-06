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

#include "base.h"
#include "city.h"
#include "events.h"
#include "government.h"
#include "idex.h"
#include "map.h"
#include "movement.h"
#include "player.h"
#include "tech.h"
#include "unit.h"
#include "unitlist.h"

#include "script.h"

#include "barbarian.h"
#include "citytools.h"
#include "cityturn.h"
#include "maphand.h"
#include "plrhand.h"
#include "sanitycheck.h"
#include "sernet.h"
#include "settlers.h"
#include "spacerace.h"
#include "srv_main.h"
#include "techtools.h"
#include "unithand.h"
#include "unittools.h"

#include "aicity.h"
#include "aiunit.h"

static int evaluate_city_name_priority(struct tile *ptile,
				       struct city_name *city_name,
				       int default_priority);
static char *search_for_city_name(struct tile *ptile, struct city_name *city_names,
				  struct player *pplayer);
static void server_set_tile_city(struct city *pcity, int city_x, int city_y,
				 enum city_tile_type type);
static void update_city_tile_status(struct city *pcity, int city_x,
				    int city_y);

/****************************************************************************
  Freeze the workers (citizens on tiles) for the city.  They will not be
  auto-arranged until unfreeze_workers is called.

  Long explanation:

  Historically auto_arrange_workers was called every time a city changed.
  If the city grew or shrunk, a new tile became available or was removed,
  the function would be called.  However in at least one place this breaks.
  In some operations (like transfer_city) multiple things may change and
  the city is not left in a sane state in between.  Calling
  auto_arrange_workers after each change means it's called with an "insane"
  city.  This can lead at best to a failed sanity check with a wasted call,
  or at worse to a more major bug.  The solution is freeze_workers and
  thaw_workers.

  Call freeze_workers to freeze the auto-arranging of citizens.  So long as
  the freeze is in place no arrangement will be done for this city.  Any
  call to auto_arrange_workers will just queue up an arrangement for later.
  Later when thaw_workers is called, the freeze is removed and the
  auto-arrange will be done if there is any arrangement pending.

  Freezing may safely be done more than once.

  It is thus always safe to call freeze and thaw around any set of city
  actions.  However this is unlikely to be needed in very many places.
****************************************************************************/
void city_freeze_workers(struct city *pcity)
{
  pcity->server.workers_frozen++;
}

/****************************************************************************
  Thaw (unfreeze) the workers (citizens on tiles) for the city.  The workers
  will be auto-arranged if there is an arrangement pending.  See explanation
  in freeze_workers().
****************************************************************************/
void city_thaw_workers(struct city *pcity)
{
  pcity->server.workers_frozen--;
  assert(pcity->server.workers_frozen >= 0);
  if (pcity->server.workers_frozen == 0 && pcity->server.needs_arrange) {
    auto_arrange_workers(pcity);
  }
}

/****************************************************************
Returns the priority of the city name at the given position,
using its own internal algorithm.  Lower priority values are
more desired, and all priorities are non-negative.

This function takes into account game.natural_city_names, and
should be able to deal with any future options we want to add.
*****************************************************************/
static int evaluate_city_name_priority(struct tile *ptile,
				       struct city_name *city_name,
				       int default_priority)
{
  /* Lower values mean higher priority. */
  float priority = (float)default_priority;
  int goodness;

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
  if (!game.info.natural_city_names) {
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
  goodness = tile_has_special(ptile, S_RIVER) ?
	      city_name->river : -city_name->river;
  if (goodness > 0) {
    priority /= mult_factor;
  } else if (goodness < 0) {
    priority *= mult_factor;
  }

  terrain_type_iterate(pterrain) {
    /* Now we do the same for every available terrain. */
    goodness
      = is_terrain_near_tile(ptile, pterrain)
      ? city_name->terrain[pterrain->index]
      : -city_name->terrain[pterrain->index];
    if (goodness > 0) {
      priority /= mult_factor;
    } else if (goodness < 0) {
      priority *= mult_factor;
    }
  } terrain_type_iterate_end;

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
static char *search_for_city_name(struct tile *ptile, struct city_name *city_names,
				  struct player *pplayer)
{
  int choice, best_priority = -1;
  char* best_name = NULL;

  for (choice = 0; city_names[choice].name; choice++) {
    if (!game_find_city_by_name(city_names[choice].name)
	&& is_allowed_city_name(pplayer, city_names[choice].name, NULL, 0)) {
      int priority = evaluate_city_name_priority(ptile, &city_names[choice],
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
   that are in another player's default city names. (E.g., Swedish may not
   call new cities or rename old cities as Helsinki, because it's in
   Finns' default city names.  Duplicated names may be used by
   either nation.)
**************************************************************************/
bool is_allowed_city_name(struct player *pplayer, const char *city_name,
			  char *error_buf, size_t bufsz)
{
  struct connection *pconn = find_conn_by_user(pplayer->username);

  /* Mode 1: A city name has to be unique for each player. */
  if (game.info.allowed_city_names == 1 &&
      city_list_find_name(pplayer->cities, city_name)) {
    if (error_buf) {
      my_snprintf(error_buf, bufsz, _("You already have a city called %s."),
		  city_name);
    }
    return FALSE;
  }

  /* Modes 2,3: A city name has to be globally unique. */
  if ((game.info.allowed_city_names == 2 
       || game.info.allowed_city_names == 3)
      && game_find_city_by_name(city_name)) {
    if (error_buf) {
      my_snprintf(error_buf, bufsz,
		  _("A city called %s already exists."), city_name);
    }
    return FALSE;
  }

  /* General rule: any name in our ruleset is allowed. */
  if (is_default_city_name(city_name, pplayer)) {
    return TRUE;
  }

  /* 
   * Mode 3: Check that the proposed city name is not in another
   * player's default city names.  Note the name will already have been
   * allowed if it is in this player's default city names list.
   */
  if (game.info.allowed_city_names == 3) {
    struct player *pother = NULL;

    players_iterate(player2) {
      if (player2 != pplayer && is_default_city_name(city_name, player2)) {
	pother = player2;
	break;
      }
    } players_iterate_end;

    if (pother != NULL) {
      if (error_buf) {
	my_snprintf(error_buf, bufsz, _("Can't use %s as a city name. It is "
					"reserved for %s."),
		    city_name, get_nation_name_plural(pother->nation));
      }
      return FALSE;
    }
  }

  /* To prevent abuse, only players with HACK access (usually local
   * connections) can use non-ascii names.  Otherwise players could use
   * confusing garbage names in multi-player games.
   *
   * We can even reach here for an AI player, if all the cities of the
   * original nation are exhausted and the backup nations have non-ascii
   * names in them. */
  if (!is_ascii_name(city_name)
      && (!pconn || pconn->access_level != ALLOW_HACK)) {
    if (error_buf) {
      my_snprintf(error_buf, bufsz,
		  _("%s is not a valid name. Only ASCII or "
		    "ruleset names are allowed for cities."),
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
char *city_name_suggestion(struct player *pplayer, struct tile *ptile)
{
  int i = 0, j;
  bool nations_selected[game.control.nation_count];
  struct nation_type *nation_list[game.control.nation_count];
  int queue_size;

  static const int num_tiles = MAP_MAX_WIDTH * MAP_MAX_HEIGHT; 

  /* tempname must be static because it's returned below. */
  static char tempname[MAX_LEN_NAME];

  /* This function follows a straightforward algorithm to look through
   * nations to find a city name.
   *
   * We start by adding the player's nation to the queue.  Then we proceed:
   * - Pick a random nation from the queue.
   * - If it has a valid city name, use that.
   * - Otherwise, add all parent and child nations to the queue.
   * - If the queue is empty, add all remaining nations to it and continue.
   *
   * Variables used:
   * - nation_list is a queue of nations to look through.
   * - nations_selected tells whether each nation is in the queue already
   * - queue_size gives the size of the queue (number of nations in it).
   * - i is the current position in the queue.
   * Note that nations aren't removed from the queue after they're processed.
   * New nations are just added onto the end.
   */

  freelog(LOG_VERBOSE, "Suggesting city name for %s at (%d,%d)",
	  pplayer->name, ptile->x, ptile->y);
  
  memset(nations_selected, 0, sizeof(nations_selected));

  queue_size = 1;
  nation_list[0] = pplayer->nation;
  nations_selected[pplayer->nation->index] = TRUE;

  while (i < game.control.nation_count) {
    for (; i < queue_size; i++) {
      char *name;
      struct nation_type *nation;

      {
	/* Pick a random nation from the queue. */
	const int which = i + myrand(queue_size - i);
	struct nation_type *tmp = nation_list[i];

	nation_list[i] = nation_list[which];
	nation_list[which] = tmp;
      }

      nation = nation_list[i];
      name = search_for_city_name(ptile, nation->city_names, pplayer);

      freelog(LOG_DEBUG, "Looking through %s.", nation->name);

      if (name) {
	return name;
      }

      /* Append the nation's parent nations into the search tree. */
      for (j = 0; nation->parent_nations[j] != NO_NATION_SELECTED; j++) {
	struct nation_type *n = nation->parent_nations[j];

	if (!nations_selected[n->index]) {
	  nation_list[queue_size] = n;
	  nations_selected[n->index] = TRUE;
	  queue_size++;
	  freelog(LOG_DEBUG, "Parent %s.", n->name);
	}
      }

      /* Append the nation's civil war nations into the search tree. */
      for (j = 0; nation->civilwar_nations[j] != NO_NATION_SELECTED; j++) {
	struct nation_type *n = nation->civilwar_nations[j];

	if (!nations_selected[n->index]) {
	  nation_list[queue_size] = n;
	  nations_selected[n->index] = TRUE;
	  queue_size++;
	  freelog(LOG_DEBUG, "Child %s.", n->name);
	}
      }
    }

    /* Append all remaining nations. */
    nations_iterate(n) {
      if (!nations_selected[n->index]) {
	nation_list[queue_size] = n;
	nations_selected[n->index] = TRUE;
	queue_size++;
	freelog(LOG_DEBUG, "Misc nation %s.", n->name);
      }
    } nations_iterate_end;
  }

  for (i = 1; i <= num_tiles; i++ ) {
    my_snprintf(tempname, MAX_LEN_NAME, _("City no. %d"), i);
    if (!game_find_city_by_name(tempname)) {
      return tempname;
    }
  }
  
  /* This had better be impossible! */
  assert(FALSE);
  sz_strlcpy(tempname, _("A poorly-named city"));
  return tempname;
}

/**************************************************************************
  calculate the remaining build points 
**************************************************************************/
int build_points_left(struct city *pcity)
{
  int cost = impr_build_shield_cost(pcity->production.value);

  return cost - pcity->shield_stock;
}

/**************************************************************************
  Will unit of this type be created as veteran?
**************************************************************************/
int do_make_unit_veteran(struct city *pcity,
			 const struct unit_type *punittype)
{
  /* we current don't have any wonder or building that have influence on 
     settler/worker units */
  if (unit_type_flag(punittype, F_SETTLERS)
      || unit_type_flag(punittype, F_CITIES)) {
    return 0;
  }

  if (get_unittype_bonus(pcity->owner, pcity->tile, punittype,
			 EFT_VETERAN_BUILD) > 0) {
    return 1;
  }
  
  return 0;
}

/*********************************************************************
  Change home city of a unit with verbose output.
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
      notify_player(from_player, punit->tile, E_UNIT_RELOCATED,
		    _("Changed homecity of %s to %s."),
		    unit_name(punit->type), tocity->name);
    }
  } else {
    struct city *in_city = tile_get_city(punit->tile);
    if (in_city) {
      freelog(LOG_VERBOSE, "Transfered %s in %s from %s to %s",
	      unit_name(punit->type), in_city->name,
	      from_player->name, to_player->name);
      if (verbose) {
	notify_player(from_player, punit->tile, E_UNIT_RELOCATED,
		      _("Transfered %s in %s from %s to %s."),
		      unit_name(punit->type), in_city->name,
		      from_player->name, to_player->name);
      }
    } else if (can_unit_exist_at_tile(punit, tocity->tile)) {
      freelog(LOG_VERBOSE, "Transfered %s from %s to %s",
	      unit_name(punit->type),
	      from_player->name, to_player->name);
      if (verbose) {
	notify_player(from_player, punit->tile, E_UNIT_RELOCATED,
		      _("Transfered %s from %s to %s."),
		      unit_name(punit->type),
		      from_player->name, to_player->name);
      }
    } else {
      freelog(LOG_VERBOSE, "Could not transfer %s from %s to %s",
	      unit_name(punit->type),
	      from_player->name, to_player->name);
      if (verbose) {
	notify_player(from_player, punit->tile, E_UNIT_LOST,
		      _("%s from %s lost in transfer to %s's %s"),
		      unit_name(punit->type),
		      from_player->name, to_player->name, tocity->name);
      }
      wipe_unit(punit);
      return;
    }
  }
  real_unit_change_homecity(punit, tocity);
}

/*********************************************************************
 Units in a bought city are transferred to the new owner, units 
 supported by the city, but held in other cities are updated to
 reflect those cities as their new homecity.  Units supported 
 by the bought city, that are not in a city square may be deleted.

 - Kris Bubendorfer <Kris.Bubendorfer@MCS.VUW.AC.NZ>

pplayer: The player recieving the units if they are not disbanded and
         are not in a city
pvictim: The owner of the city the units are transferred from.
units:   A list of units to be transferred, typically a cities unit list.
pcity:   Default city the units are transferred to.
exclude_city: The units cannot be transferred to this city.
kill_outside: Units outside this range are deleted. -1 means no units
              are deleted.
verbose: Messages are sent to the involved parties.
***********************************************************************/
void transfer_city_units(struct player *pplayer, struct player *pvictim, 
			 struct unit_list *units, struct city *pcity,
			 struct city *exclude_city,
			 int kill_outside, bool verbose)
{
  struct tile *ptile = pcity->tile;

  /* Transfer enemy units in the city to the new owner.
   * Only relevant if we are transferring to another player. */
  if (pplayer != pvictim) {
    unit_list_iterate_safe((ptile)->units, vunit)  {
      /* Don't transfer units already owned by new city-owner --wegge */ 
      if (unit_owner(vunit) == pvictim) {
	transfer_unit(vunit, pcity, verbose);
	unit_list_unlink(units, vunit);
      } else if (!pplayers_allied(pplayer, unit_owner(vunit))) {
        /* the owner of vunit is allied to pvictim but not to pplayer */
        bounce_unit(vunit, verbose);
      }
    } unit_list_iterate_safe_end;
  }

  /* Any remaining units supported by the city are either given new home
     cities or maybe destroyed */
  unit_list_iterate_safe(units, vunit) {
    struct city *new_home_city = tile_get_city(vunit->tile);
    if (new_home_city && new_home_city != exclude_city
	&& city_owner(new_home_city) == unit_owner(vunit)) {
      /* unit is in another city: make that the new homecity,
	 unless that city is actually the same city (happens if disbanding) */
      transfer_unit(vunit, new_home_city, verbose);
    } else if (kill_outside == -1
	       || real_map_distance(vunit->tile, ptile) <= kill_outside) {
      /* else transfer to specified city. */
      transfer_unit(vunit, pcity, verbose);
    } else {
      /* The unit is lost.  Call notify_player (in all other cases it is
       * called autmatically). */
      freelog(LOG_VERBOSE, "Lost %s's %s at (%d,%d) when %s was lost.",
	      unit_owner(vunit)->name, unit_name(vunit->type),
	      vunit->tile->x, vunit->tile->y, pcity->name);
      if (verbose) {
	notify_player(unit_owner(vunit), vunit->tile,
			 E_UNIT_LOST,
			 _("%s lost along with control of %s."),
			 unit_name(vunit->type), pcity->name);
      }
      wipe_unit(vunit);
    }
  } unit_list_iterate_safe_end;

  unit_list_iterate(pcity->units_supported, punit) {
    assert(punit->homecity == pcity->id);
    assert(unit_owner(punit) == pplayer);
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
struct city *find_closest_owned_city(struct player *pplayer, struct tile *ptile,
				     bool sea_required, struct city *pexclcity)
{
  int dist = -1;
  struct city *rcity = NULL;
  city_list_iterate(pplayer->cities, pcity)
    if ((real_map_distance(ptile, pcity->tile) < dist || dist == -1) &&
        (!sea_required || is_ocean_near_tile(pcity->tile)) &&
        (!pexclcity || (pexclcity != pcity))) {
      dist = real_map_distance(ptile, pcity->tile);
      rcity = pcity;
    }
  city_list_iterate_end;

  return rcity;
}

/**************************************************************************
  called when a player conquers a city, remove buildings (not wonders and 
  always palace) with game.info.razechance% chance, barbarians destroy more
  set the city's shield stock to 0
**************************************************************************/
static void raze_city(struct city *pcity)
{
  int razechance = game.info.razechance;

  /* land barbarians are more likely to destroy city improvements */
  if (is_land_barbarian(city_owner(pcity)))
    razechance += 30;

  built_impr_iterate(pcity, i) {
    if (is_small_wonder(i)) {
      /* small wonders are always razed
       * This is not strictly necessary as transfer_city()
       * would remove small wonders anyway. */
      pcity->improvements[i] = I_NONE;
    }
    if (is_improvement(i) && (myrand(100) < razechance)) {
      pcity->improvements[i] = I_NONE;
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
      if (can_cities_trade(pcity, oldtradecity)
          && can_establish_trade_route(pcity, oldtradecity)) {   
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
static void build_free_small_wonders(struct player *pplayer,
				     const char *const old_capital_name,
				     bv_imprs *had_small_wonders)
{
  int size = city_list_size(pplayer->cities);

  if (size == 0) {
    /* The last city was removed or transferred to the enemy. R.I.P. */
    return;
  }

  impr_type_iterate(id) {
    if (BV_ISSET(*had_small_wonders, id)) {
      struct city *pnew_city;

      assert(find_city_from_small_wonder(pplayer, id) == NULL);

      pnew_city = city_list_get(pplayer->cities, myrand(size));

      city_add_improvement(pnew_city, id);
      pplayer->small_wonders[id] = pnew_city->id;

      /*
       * send_player_cities will recalculate all cities and send them to
       * the client.
       */
      send_player_cities(pplayer);

      notify_player(pplayer, pnew_city->tile, E_CITY_LOST,
		    _("You lost %s. A new %s was built in %s."),
		    old_capital_name, get_improvement_name(id),
		    pnew_city->name);
      /* 
       * The enemy want to see the new capital in his intelligence
       * report. 
       */
      send_city_info(NULL, pnew_city);
    }
  } impr_type_iterate_end;
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
  int i;
  struct unit_list *old_city_units = unit_list_new();
  struct player *pgiver = city_owner(pcity);
  int old_trade_routes[NUM_TRADEROUTES];
  bv_imprs had_small_wonders;
  char old_city_name[MAX_LEN_NAME];
  struct vision *old_vision;

  assert(pgiver != ptaker);

  city_freeze_workers(pcity);

  unit_list_iterate(pcity->units_supported, punit) {
    unit_list_prepend(old_city_units, punit);
    /* otherwise we might delete the homecity from under the unit
       in the client. */
    punit->homecity = 0;
    send_unit_info(NULL, punit);
  } unit_list_iterate_end;
  unit_list_unlink_all(pcity->units_supported);

  /* Remove all global improvement effects that this city confers (but
     then restore the local improvement list - we need this to restore the
     global effects for the new city owner) */
  BV_CLR_ALL(had_small_wonders);
  built_impr_iterate(pcity, i) {
    city_remove_improvement(pcity, i);

    if (is_small_wonder(i)) {
      BV_SET(had_small_wonders, i);
    } else {
      pcity->improvements[i] = I_ACTIVE;
    }
  } built_impr_iterate_end;

  give_citymap_from_player_to_player(pcity, pgiver, ptaker);
  old_vision = pcity->server.vision;
  pcity->server.vision = vision_new(ptaker, pcity->tile, FALSE);
  vision_layer_iterate(v) {
    vision_change_sight(pcity->server.vision, v,
			vision_get_sight(old_vision, v));
  } vision_layer_iterate_end;

  sz_strlcpy(old_city_name, pcity->name);
  if (game.info.allowed_city_names == 1
      && city_list_find_name(ptaker->cities, pcity->name)) {
    sz_strlcpy(pcity->name,
	       city_name_suggestion(ptaker, pcity->tile));
    notify_player(ptaker, pcity->tile, E_BAD_COMMAND,
		     _("You already had a city called %s."
		       " The city was renamed to %s."), old_city_name,
		     pcity->name);
  }

  /* Has to follow the unfog call above. */
  city_list_unlink(pgiver->cities, pcity);
  pcity->owner = ptaker;
  map_claim_ownership(pcity->tile, ptaker, pcity->tile);
  city_list_prepend(ptaker->cities, pcity);

  transfer_city_units(ptaker, pgiver, old_city_units,
		      pcity, NULL,
		      kill_outside, transfer_unit_verbose);
  /* The units themselves are allready freed by transfer_city_units. */
  unit_list_unlink_all(old_city_units);
  unit_list_free(old_city_units);

  if (resolve_stack) {
    resolve_unit_stacks(pgiver, ptaker, transfer_unit_verbose);
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
   * with the transferred city.
   */
  for (i = 0; i < NUM_TRADEROUTES; i++) {
    struct city *pother_city = find_city_by_id(pcity->trade[i]);
    if (pother_city) {
      reality_check_city(ptaker, pother_city->tile);
      update_dumb_city(ptaker, pother_city);
      send_city_info(ptaker, pother_city);
    }
  }

  city_refresh(pcity);

  /* 
   * maybe_make_contact have to be called before
   * update_city_tile_status_map below since the diplomacy status can
   * influence if a tile is available.
   */
  maybe_make_contact(pcity->tile, ptaker);

  map_city_radius_iterate(pcity->tile, ptile) {
    update_city_tile_status_map(pcity, ptile);
  } map_city_radius_iterate_end;
  auto_arrange_workers(pcity);
  city_thaw_workers(pcity);
  if (raze)
    raze_city(pcity);

  /* Restore any global improvement effects that this city confers */
  built_impr_iterate(pcity, i) {
    city_add_improvement(pcity, i);
  } built_impr_iterate_end;

  /* Set production to something valid for pplayer, if not. */
  if ((pcity->production.is_unit
       && !can_build_unit_direct(pcity,
				 get_unit_type(pcity->production.value)))
      || (!pcity->production.is_unit
          && !can_build_improvement(pcity, pcity->production.value))) {
    advisor_choose_build(ptaker, pcity);
  } 

  send_city_info(NULL, pcity);

  /* What wasn't obsolete for the old owner may be so now. */
  remove_obsolete_buildings_city(pcity, TRUE);

  if (terrain_control.may_road
      && player_knows_techs_with_flag (ptaker, TF_RAILROAD)
      && !tile_has_special(pcity->tile, S_RAILROAD)) {
    notify_player(ptaker, pcity->tile, E_CITY_TRANSFER,
		  _("The people in %s are stunned by your"
		    " technological insight!\n"
		    "      Workers spontaneously gather and upgrade"
		    " the city with railroads."),
		  pcity->name);
    tile_set_special(pcity->tile, S_RAILROAD);
    update_tile_knowledge(pcity->tile);
  }

  /* Build a new palace for free if the player lost her capital and
     savepalace is on. */
  if (game.info.savepalace) {
    build_free_small_wonders(pgiver, pcity->name, &had_small_wonders);
  }

  /* Remove the sight points from the giver...and refresh the city's
   * vision range, since it might be different under the new owner. */
  vision_clear_sight(old_vision);
  vision_free(old_vision);
  city_refresh_vision(pcity);

  sanity_check_city(pcity);
  sync_cities();
}

/**************************************************************************
...
**************************************************************************/
void create_city(struct player *pplayer, struct tile *ptile,
		 const char *name)
{
  struct city *pcity;
  int x_itr, y_itr;
  struct nation_type *nation = get_nation_by_plr(pplayer);
  struct base_type *pbase;

  freelog(LOG_DEBUG, "Creating city %s", name);

  /* Ensure that we claim the ground we stand on */
  map_claim_ownership(ptile, pplayer, ptile);

  if (terrain_control.may_road) {
    tile_set_special(ptile, S_ROAD);
    if (player_knows_techs_with_flag(pplayer, TF_RAILROAD)) {
      tile_set_special(ptile, S_RAILROAD);
      update_tile_knowledge(ptile);
    }
  }

  /* It is possible that update_tile_knowledge() already sent tile information
   * to some players, but we don't want to make any special handling for
   * those cases.  The network code may prevent asecond packet from being
   * sent anyway. */
  send_tile_info(NULL, ptile);

  pcity = create_city_virtual(pplayer, ptile, name);
  pcity->ai.trade_want = TRADE_WEIGHTING;
  pcity->id = get_next_id_number();
  idex_register_city(pcity);

  if (!pplayer->capital) {
    int i;
    pplayer->capital = TRUE;

    for (i = 0; i < MAX_NUM_BUILDING_LIST; i++) {
      if (game.rgame.global_init_buildings[i] == B_LAST) {
	break;
      }
      city_add_improvement(pcity, game.rgame.global_init_buildings[i]);
      if (is_small_wonder(game.rgame.global_init_buildings[i])) {
        pplayer->small_wonders[game.rgame.global_init_buildings[i]] = pcity->id;
      }
      assert(!is_great_wonder(game.rgame.global_init_buildings[i]));
    }
    for (i = 0; i < MAX_NUM_BUILDING_LIST; i++) {
      if (nation->init_buildings[i] == B_LAST) {
	break;
      }
      city_add_improvement(pcity, nation->init_buildings[i]);
    }
  }

  /* Before arranging workers to show unknown land */
  pcity->server.vision = vision_new(pplayer, ptile, FALSE);
  city_refresh_vision(pcity);

  tile_set_city(ptile, pcity);

  city_list_prepend(pplayer->cities, pcity);

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

  /* Place a worker at the city center; this is the free-worked tile.
   * This must be done before the city refresh (below) so that the city
   * is in a sane state. */
  server_set_tile_city(pcity, CITY_MAP_SIZE/2, CITY_MAP_SIZE/2, C_TILE_WORKER);

  /* Refresh the city.  First a city refresh is done (this shouldn't
   * send any packets to the client because the city has no supported units)
   * then rearrange the workers.  Note that auto_arrange_workers does its
   * own refresh call; it is safest to do our own controlled city_refresh
   * first. */
  city_refresh(pcity);
  auto_arrange_workers(pcity);

  /* Put vision back to normal, if base acted as a watchtower */
  pbase = tile_get_base(ptile);
  if (pbase) {
    tile_remove_base(ptile);
    unit_list_refresh_vision(ptile->units);
  }

  update_tile_knowledge(ptile);

  pcity->synced = FALSE;
  send_city_info(NULL, pcity);
  sync_cities(); /* Will also send pcity. */

  notify_player(pplayer, ptile, E_CITY_BUILD,
		   _("You have founded %s."), pcity->name);
  maybe_make_contact(ptile, city_owner(pcity));

  unit_list_iterate((ptile)->units, punit) {
    struct city *home = find_city_by_id(punit->homecity);

    /* Catch fortress building, transforming into ocean, etc. */
    if (!can_unit_continue_current_activity(punit)) {
      handle_unit_activity_request(punit, ACTIVITY_IDLE);
    }

    /* Update happiness (the unit may no longer cause unrest). */
    if (home) {
      city_refresh(home);
      send_city_info(city_owner(home), home);
    }
  } unit_list_iterate_end;
  sanity_check_city(pcity);

  script_signal_emit("city_built", 1, API_TYPE_CITY, pcity);
}

/**************************************************************************
  Remove a city from the game.
**************************************************************************/
void remove_city(struct city *pcity)
{
  int o;
  struct player *pplayer = city_owner(pcity);
  struct tile *ptile = pcity->tile;
  bv_imprs had_small_wonders;
  char *city_name = mystrdup(pcity->name);
  struct vision *old_vision;
  int id = pcity->id; /* We need this even after memory has been freed */

  BV_CLR_ALL(had_small_wonders);
  built_impr_iterate(pcity, i) {
    city_remove_improvement(pcity, i);

    if (is_small_wonder(i)) {
      BV_SET(had_small_wonders, i);
    }
  } built_impr_iterate_end;

  /* Rehome units in other cities */
  unit_list_iterate_safe(pcity->units_supported, punit) {
    struct city *new_home_city = tile_get_city(punit->tile);

    if (new_home_city
	&& new_home_city != pcity
	&& city_owner(new_home_city) == pplayer) {
      transfer_unit(punit, new_home_city, TRUE);
    }
  } unit_list_iterate_safe_end;

  /* make sure ships are not left on land when city is removed. */
  unit_list_iterate_safe(ptile->units, punit) {
    bool moved;
    struct unit_type *punittype = unit_type(punit);

    if (is_native_tile(punittype, ptile)) {
      continue;
    }

    handle_unit_activity_request(punit, ACTIVITY_IDLE);
    moved = FALSE;
    adjc_iterate(ptile, tile1) {
      if (!moved && is_native_tile(punittype, tile1)) {
	if (could_unit_move_to_tile(punit, tile1) == 1) {
	  moved = handle_unit_move_request(punit, tile1, FALSE, TRUE);
	  if (moved) {
	    notify_player(unit_owner(punit), NULL, E_UNIT_RELOCATED,
                          _("Moved %s out of disbanded city %s "
                            "since it cannot stay on %s."),
                          unit_type(punit)->name, pcity->name,
                          get_name(tile_get_terrain(ptile)));
            break;
	  }
	}
      }
    } adjc_iterate_end;
    if (!moved) {
      notify_player(unit_owner(punit), NULL, E_UNIT_LOST,
		       _("When %s was disbanded your %s could not "
			 "get out, and it was therefore lost."),
		       pcity->name, unit_type(punit)->name);
      wipe_unit(punit);
    }
  } unit_list_iterate_safe_end;

  /* Any remaining supported units are destroyed */
  unit_list_iterate_safe(pcity->units_supported, punit) {
    wipe_unit(punit);
  } unit_list_iterate_safe_end;

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

  old_vision = pcity->server.vision;
  pcity->server.vision = NULL;
  game_remove_city(pcity);

  players_iterate(other_player) {
    if (map_is_known_and_seen(ptile, other_player, V_MAIN)) {
      reality_check_city(other_player, ptile);
    }
  } players_iterate_end;
  conn_list_iterate(game.est_connections, pconn) {
    if (!pconn->player && pconn->observer) {
      /* For detached observers we have to send a specific packet.  This is
       * a hack necessitated by the private map that exists for players but
       * not observers. */
      dsend_packet_city_remove(pconn, id);
    }
  } conn_list_iterate_end;

  vision_clear_sight(old_vision);
  vision_free(old_vision);

  /* Update available tiles in adjacent cities. */
  map_city_radius_iterate(ptile, tile1) {
    /* For every tile the city could have used. */
    map_city_radius_iterate(tile1, tile2) {
      /* We see what cities are inside reach of the tile. */
      struct city *pcity = tile_get_city(tile2);
      if (pcity) {
	update_city_tile_status_map(pcity, tile1);
      }
    } map_city_radius_iterate_end;
  } map_city_radius_iterate_end;

  /* Build a new palace for free if the player lost her capital and
     savepalace is on. */
  if (game.info.savepalace) {
    build_free_small_wonders(pplayer, city_name, &had_small_wonders);
  }

  free(city_name);

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

  /* If not at war, may peacefully enter city. Or, if we cannot occupy
   * the city, this unit entering will not trigger the effects below. */
  if (!pplayers_at_war(pplayer, cplayer)
      || !COULD_OCCUPY(punit)) {
    return;
  }

  /* Okay, we're at war - invader captures/destroys city... */
  
  /* If a capital is captured, then spark off a civil war 
     - Kris Bubendorfer
     Also check spaceships --dwp
  */
  if (is_capital(pcity)
      && (cplayer->spaceship.state == SSHIP_STARTED
          || cplayer->spaceship.state == SSHIP_LAUNCHED)) {
    spaceship_lost(cplayer);
  }
  
  if (is_capital(pcity)
      && city_list_size(cplayer->cities) >= game.info.civilwarsize
      && game.info.civilwarsize < GAME_MAX_CIVILWARSIZE
      && get_num_human_and_ai_players() < MAX_NUM_PLAYERS
      && civil_war_triggered(cplayer)) {
    /* Do a civil war only if there's an available unused nation. */
    nations_iterate(pnation) {
      if (is_nation_playable(pnation)
	  && pnation->is_available
	  && !pnation->player) {
	do_civil_war = TRUE;
	break;
      }
    } nations_iterate_end;
  }

  /* 
   * We later remove a citizen. Lets check if we can save this since
   * the city will be destroyed.
   */
  if (pcity->size <= 1) {
    notify_player(pplayer, pcity->tile, E_UNIT_WIN_ATT,
		     _("You destroy %s completely."), pcity->name);
    notify_player(cplayer, pcity->tile, E_CITY_LOST, 
		     _("%s has been destroyed by %s."), 
		     pcity->name, pplayer->name);
    remove_city(pcity);
    if (do_civil_war) {
      civil_war(cplayer);
    }
    return;
  }

  coins = cplayer->economic.gold;
  coins = myrand((coins / 20) + 1) + (coins * (pcity->size)) / 200;
  pplayer->economic.gold += coins;
  cplayer->economic.gold -= coins;
  send_player_info(cplayer, cplayer);
  if (pcity->original != pplayer) {
    if (coins > 0) {
      notify_player(pplayer, pcity->tile, E_UNIT_WIN_ATT, 
		    PL_("You conquer %s; your lootings accumulate"
			" to %d gold!",
			"You conquer %s; your lootings accumulate"
			" to %d gold!", coins), 
		    pcity->name, coins);
      notify_player(cplayer, pcity->tile, E_CITY_LOST, 
		    PL_("%s conquered %s and looted %d gold"
			" from the city.",
			"%s conquered %s and looted %d gold"
			" from the city.", coins),
		    pplayer->name, pcity->name, coins);
    } else {
      notify_player(pplayer, pcity->tile, E_UNIT_WIN_ATT, 
		    _("You conquer %s"), pcity->name);
      notify_player(cplayer, pcity->tile, E_CITY_LOST, 
		    _("%s conquered %s."), pplayer->name, pcity->name);
    }
  } else {
    if (coins > 0) {
      notify_player(pplayer, pcity->tile, E_UNIT_WIN_ATT, 
		    PL_("You have liberated %s!"
			" Lootings accumulate to %d gold.",
			"You have liberated %s!"
			" Lootings accumulate to %d gold.", coins),
		    pcity->name, coins);
      notify_player(cplayer, pcity->tile, E_CITY_LOST, 
		    PL_("%s liberated %s and looted %d gold"
			" from the city.",
			"%s liberated %s and looted %d gold"
			" from the city.", coins),
		    pplayer->name, pcity->name, coins);
    } else {
      notify_player(pplayer, pcity->tile, E_UNIT_WIN_ATT, 
		    _("You have liberated %s!"), pcity->name);
      notify_player(cplayer, pcity->tile, E_CITY_LOST, 
		    _("%s liberated %s."), pplayer->name, pcity->name);
    }
  }

  steal_a_tech(pplayer, cplayer, A_UNSET);
  make_partisans(pcity);

  /* We transfer the city first so that it is in a consistent state when
   * the size is reduced. */
  transfer_city(pplayer, pcity , 0, TRUE, TRUE, TRUE);
  city_reduce_size(pcity, 1);
  send_player_info(pplayer, pplayer); /* Update techs */

  if (do_civil_war) {
    civil_war(cplayer);
  }
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
static void package_dumb_city(struct player* pplayer, struct tile *ptile,
			      struct packet_city_short_info *packet)
{
  struct player_tile *pdtile = map_get_player_tile(ptile, pplayer);
  struct dumb_city *pdcity = pdtile->city;
  struct city *pcity = tile_get_city(ptile);

  packet->id = pdcity->id;
  if (ptile->owner) {
    /* Use tile owner information not city owner information. */
    packet->owner = ptile->owner->player_no;
  } else {
    packet->owner = pdcity->owner->player_no;
  }
  packet->x = ptile->x;
  packet->y = ptile->y;
  sz_strlcpy(packet->name, pdcity->name);

  packet->size = pdcity->size;

  packet->occupied = pdcity->occupied;
  packet->happy = pdcity->happy;
  packet->unhappy = pdcity->unhappy;

  if (pcity && player_has_traderoute_with_city(pplayer, pcity)) {
    packet->tile_trade = pcity->citizen_base[O_TRADE];
  } else {
    packet->tile_trade = 0;
  }

  packet->improvements = pdcity->improvements;
}

/**************************************************************************
  Update plrtile information about the city, and send out information to
  the clients if it has changed.
**************************************************************************/
void refresh_dumb_city(struct city *pcity)
{
  players_iterate(pplayer) {
    if (map_is_known_and_seen(pcity->tile, pplayer, V_MAIN)
	|| player_has_traderoute_with_city(pplayer, pcity)) {
      if (update_dumb_city(pplayer, pcity)) {
	struct packet_city_short_info packet;

	if (city_owner(pcity) != pplayer) {
	  /* Don't send the short_city information to someone who can see the
	   * city's internals.  Doing so would really confuse the client. */
	  package_dumb_city(pplayer, pcity->tile, &packet);
	  lsend_packet_city_short_info(pplayer->connections, &packet);
	}
      }
    }
  } players_iterate_end;

  /* Don't send to non-player observers since they don't have 'dumb city'
   * information. */
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
  struct packet_city_short_info sc_pack;

  /* Send to everyone who can see the city. */
  players_iterate(pplayer) {
    if (can_player_see_city_internals(pplayer, pcity)) {
      if (!nocity_send || pplayer != city_owner(pcity)) {
	update_dumb_city(powner, pcity);
	package_city(pcity, &packet, FALSE);
	lsend_packet_city_info(powner->connections, &packet);
      }
    } else {
      if (map_is_known_and_seen(pcity->tile, pplayer, V_MAIN)
	  || player_has_traderoute_with_city(pplayer, pcity)) {
	update_dumb_city(pplayer, pcity);
	package_dumb_city(pplayer, pcity->tile, &sc_pack);
	lsend_packet_city_short_info(pplayer->connections, &sc_pack);
      }
    }
  } players_iterate_end;

  /* send to non-player observers:
   * should these only get dumb_city type info?
   */
  conn_list_iterate(game.est_connections, pconn) {
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
  conn_list_iterate(dest, pconn) {
    struct player *pplayer = pconn->player;
    if (!pplayer && !pconn->observer) {
      continue;
    }
    whole_map_iterate(ptile) {
      if (!pplayer || map_get_player_tile(ptile, pplayer)->city) {
	send_city_info_at_tile(pplayer, pconn->self, NULL, ptile);
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
    send_city_info_at_tile(dest, dest->connections, pcity, pcity->tile);
  }
}

/**************************************************************************
Send info about a city, as seen by pviewer, to dest (usually dest will
be pviewer->connections). If pplayer can see the city we update the city
info first. If not we just use the info from the players private map.

If (pviewer == NULL) this is for observers, who see everything (?)
For this function dest may not be NULL.  See send_city_info() and
broadcast_city_info().

If pcity is non-NULL it should be same as tile_get_city(x,y); if pcity
is NULL, this function calls tile_get_city(x,y) (it is ok if this
returns NULL).

Sometimes a player's map contain a city that doesn't actually exist. Use
reality_check_city(pplayer, ptile) to update that. Remember to NOT send info
about a city to a player who thinks the tile contains another city. If you
want to update the clients info of the tile you must use
reality_check_city(pplayer, ptile) first. This is generally taken care of
automatically when a tile becomes visible.
**************************************************************************/
void send_city_info_at_tile(struct player *pviewer, struct conn_list *dest,
			    struct city *pcity, struct tile *ptile)
{
  struct player *powner = NULL;
  struct packet_city_info packet;
  struct packet_city_short_info sc_pack;
  struct dumb_city *pdcity;

  if (!pcity)
    pcity = tile_get_city(ptile);
  if (pcity)
    powner = city_owner(pcity);

  if (powner && powner == pviewer) {
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
    } else {
      if (!map_is_known(ptile, pviewer)) {
	/* Without the conditional we'd have an infinite loop here. */
	map_show_tile(pviewer, ptile);
      }
      if (map_is_known_and_seen(ptile, pviewer, V_MAIN)) {
	if (pcity) {		/* it's there and we see it; update and send */
	  update_dumb_city(pviewer, pcity);
	  package_dumb_city(pviewer, ptile, &sc_pack);
	  lsend_packet_city_short_info(dest, &sc_pack);
	}
      } else {			/* not seen; send old info */
	pdcity = map_get_player_tile(ptile, pviewer)->city;
	if (pdcity) {
	  package_dumb_city(pviewer, ptile, &sc_pack);
	  lsend_packet_city_short_info(dest, &sc_pack);
	}
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
  int x, y, i;

  packet->id=pcity->id;
  packet->owner = pcity->owner->player_no;
  packet->x = pcity->tile->x;
  packet->y = pcity->tile->y;
  sz_strlcpy(packet->name, pcity->name);

  packet->size=pcity->size;
  for (i=0;i<5;i++) {
    packet->ppl_happy[i]=pcity->ppl_happy[i];
    packet->ppl_content[i]=pcity->ppl_content[i];
    packet->ppl_unhappy[i]=pcity->ppl_unhappy[i];
    packet->ppl_angry[i]=pcity->ppl_angry[i];
  }
  /* The number of data in specilists[] array */
  packet->specialists_size = SP_COUNT;
  specialist_type_iterate(sp) {
    packet->specialists[sp] = pcity->specialists[sp];
  } specialist_type_iterate_end;
  for (i = 0; i < NUM_TRADEROUTES; i++) {
    packet->trade[i]=pcity->trade[i];
    packet->trade_value[i]=pcity->trade_value[i];
  }

  output_type_iterate(o) {
    packet->surplus[o] = pcity->surplus[o];
    packet->waste[o] = pcity->waste[o];
    packet->unhappy_penalty[o] = pcity->unhappy_penalty[o];
    packet->prod[o] = pcity->prod[o];
    packet->citizen_base[o] = pcity->citizen_base[o];
    packet->usage[o] = pcity->usage[o];
  } output_type_iterate_end;

  packet->food_stock=pcity->food_stock;
  packet->shield_stock=pcity->shield_stock;
  packet->pollution=pcity->pollution;
  packet->city_options = pcity->city_options;
  
  packet->production_is_unit = pcity->production.is_unit;
  packet->production_value = pcity->production.value;

  packet->turn_last_built=pcity->turn_last_built;
  packet->turn_founded = pcity->turn_founded;
  packet->changed_from_id = pcity->changed_from.value;
  packet->changed_from_is_unit = pcity->changed_from.is_unit;
  packet->before_change_shields=pcity->before_change_shields;
  packet->disbanded_shields=pcity->disbanded_shields;
  packet->caravan_shields=pcity->caravan_shields;
  packet->last_turns_shield_surplus = pcity->last_turns_shield_surplus;

  copy_worklist(&packet->worklist, &pcity->worklist);
  packet->diplomat_investigate=dipl_invest;

  packet->airlift = pcity->airlift;
  packet->did_buy = pcity->did_buy;
  packet->did_sell = pcity->did_sell;
  packet->was_happy = pcity->was_happy;
  for (y = 0; y < CITY_MAP_SIZE; y++) {
    for (x = 0; x < CITY_MAP_SIZE; x++) {
      packet->city_map[x + y * CITY_MAP_SIZE] = get_worker_city(pcity, x, y);
    }
  }

  BV_CLR_ALL(packet->improvements);
  impr_type_iterate(i) {
    if (city_got_building(pcity, i)) {
      BV_SET(packet->improvements, i);
    }
  } impr_type_iterate_end;
}

/**************************************************************************
updates a players knowledge about a city. If the player_tile already
contains a city it must be the same city (avoid problems by always calling
reality_check_city() first)

Returns TRUE iff anything has changed for the player city (i.e., if the
client needs to be updated with a *short* city packet).  This information
is only used in refresh_dumb_cities; elsewhere the data is (of necessity)
broadcast regardless.
**************************************************************************/
bool update_dumb_city(struct player *pplayer, struct city *pcity)
{
  struct player_tile *plrtile = map_get_player_tile(pcity->tile,
						    pplayer);
  struct dumb_city *pdcity = plrtile->city;
  /* pcity->occupied isn't used at the server, so we go straight to the
   * unit list to check the occupied status. */
  bool occupied =
    (unit_list_size(pcity->tile->units) > 0);
  bool happy = city_happy(pcity), unhappy = city_unhappy(pcity);
  bv_imprs improvements;

  BV_CLR_ALL(improvements);
  impr_type_iterate(i) {
    if (is_improvement_visible(i) && city_got_building(pcity, i)) {
      BV_SET(improvements, i);
    }
  } impr_type_iterate_end;

  if (pdcity
      && pdcity->id == pcity->id
      && strcmp(pdcity->name, pcity->name) == 0
      && pdcity->size == pcity->size
      && pdcity->occupied == occupied
      && pdcity->happy == happy
      && pdcity->unhappy == unhappy
      && pdcity->owner == pcity->owner
      && BV_ARE_EQUAL(pdcity->improvements, improvements)) {
    return FALSE;
  }

  if (!plrtile->city) {
    pdcity = plrtile->city = fc_malloc(sizeof(*pdcity));
    plrtile->city->id = pcity->id;
  }
  if (pdcity->id != pcity->id) {
    freelog(LOG_ERROR, "Trying to update old city (wrong ID)"
	    " at %i,%i for player %s",
	    pcity->tile->x, pcity->tile->y, pplayer->name);
    pdcity->id = pcity->id;   /* ?? */
  }
  sz_strlcpy(pdcity->name, pcity->name);
  pdcity->size = pcity->size;
  pdcity->occupied = occupied;
  pdcity->happy = happy;
  pdcity->unhappy = unhappy;
  pdcity->owner = pcity->owner;
  pdcity->improvements = improvements;

  return TRUE;
}

/**************************************************************************
Removes outdated (nonexistant) cities from a player
**************************************************************************/
void reality_check_city(struct player *pplayer,struct tile *ptile)
{
  struct city *pcity;
  struct dumb_city *pdcity = map_get_player_tile(ptile, pplayer)->city;

  if (pdcity) {
    pcity = ptile->city;
    if (!pcity || (pcity && pcity->id != pdcity->id)) {
      dlsend_packet_city_remove(pplayer->connections, pdcity->id);
      free(pdcity);
      map_get_player_tile(ptile, pplayer)->city = NULL;
    }
  }
}

/**************************************************************************
  Remove the trade route between pc1 and pc2 (if one exists).
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
  Remove/cancel the city's least valuable trade route.
**************************************************************************/
static void remove_smallest_trade_route(struct city *pcity)
{
  int slot;

  if (get_city_min_trade_route(pcity, &slot) <= 0) {
    return;
  }

  remove_trade_route(pcity, find_city_by_id(pcity->trade[slot]));
}

/**************************************************************************
  Establish a trade route.  Notice that there has to be space for them, 
  so you should check can_establish_trade_route first.
**************************************************************************/
void establish_trade_route(struct city *pc1, struct city *pc2)
{
  int i;

  if (city_num_trade_routes(pc1) == NUM_TRADEROUTES) {
    remove_smallest_trade_route(pc1);
  }

  if (city_num_trade_routes(pc2) == NUM_TRADEROUTES) {
    remove_smallest_trade_route(pc2);
  }

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
}

/****************************************************************************
  Sell the improvement from the city, and give the player the owner.  Not
  all buildings can be sold.

  I guess the player should always be the city owner?
****************************************************************************/
void do_sell_building(struct player *pplayer, struct city *pcity,
		      Impr_type_id id)
{
  if (can_city_sell_building(pcity, id)) {
    pplayer->economic.gold += impr_sell_gold(id);
    building_lost(pcity, id);
  }
}

/****************************************************************************
  Destroy the improvement in the city straight-out.  Note that this is
  different from selling a building.
****************************************************************************/
void building_lost(struct city *pcity, Impr_type_id id)
{
  struct player *owner = city_owner(pcity);
  bool was_capital = is_capital(pcity);

  city_remove_improvement(pcity,id);
  if ((was_capital && !is_capital(pcity))
      && (owner->spaceship.state == SSHIP_STARTED
	  || owner->spaceship.state == SSHIP_LAUNCHED)) {
    /* If the capital was lost (by destruction of the palace) production on
     * the spaceship is lost. */
    spaceship_lost(owner);
  }

  /* Re-update the city's visible area.  This updates fog if the vision
   * range increases or decreases. */
  city_refresh_vision(pcity);
}

/**************************************************************************
  Change the build target.
**************************************************************************/
void change_build_target(struct player *pplayer, struct city *pcity,
			 struct city_production target,
			 enum event_type event)
{
  const char *name;
  const char *source;

  /* If the city is already building this thing, don't do anything */
  if (pcity->production.is_unit == target.is_unit &&
      pcity->production.value == target.value) {
    return;
  }

  /* Is the city no longer building a wonder? */
  if (!pcity->production.is_unit && is_great_wonder(pcity->production.value) &&
      (event != E_IMP_AUTO && event != E_WORKLIST)) {
    /* If the build target is changed because of an advisor's suggestion or
       because the worklist advances, then the wonder was completed -- 
       don't announce that the player has *stopped* building that wonder. 
       */
    notify_player(NULL, pcity->tile, E_WONDER_STOPPED,
		     _("The %s have stopped building The %s in %s."),
		     get_nation_name_plural(pplayer->nation),
		     get_impr_name_ex(pcity, pcity->production.value),
		     pcity->name);
  }

  /* Manage the city change-production penalty.
     (May penalize, restore or do nothing to the shield_stock.) */
  pcity->shield_stock = city_change_production_penalty(pcity, target);

  /* Change build target. */
  pcity->production = target;

  /* What's the name of the target? */
  if (target.is_unit) {
    name = get_unit_type(pcity->production.value)->name;
  } else {
    name = get_improvement_name(pcity->production.value);
  }

  switch (event) {
    case E_WORKLIST: source = _(" from the worklist"); break;
/* Should we give the AI auto code credit?
    case E_IMP_AUTO: source = _(" as suggested by the AI advisor"); break;
*/
    default: source = ""; break;
  }

  /* Tell the player what's up. */
  /* FIXME: this may give bad grammar when translated if the 'source'
   * string can have multiple values. */
  notify_player(pplayer, pcity->tile, event,
		   /* TRANS: "<city> is building <production><source>." */
		   _("%s is building %s%s."),
		   pcity->name, name, source);

  /* If the city is building a wonder, tell the rest of the world
     about it. */
  if (!pcity->production.is_unit && is_great_wonder(pcity->production.value)) {
    notify_player(NULL, pcity->tile, E_WONDER_STARTED,
		     _("The %s have started building The %s in %s."),
		     get_nation_name_plural(pplayer->nation),
		     get_impr_name_ex(pcity, pcity->production.value),
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
  struct tile *ptile;

  if (!(ptile = city_map_to_map(pcity, city_x, city_y))) {
    return FALSE;
  }
  
  if (is_enemy_unit_tile(ptile, city_owner(pcity))
      && !is_free_worked_tile(city_x, city_y)) {
    return FALSE;
  }

  if (!map_is_known(ptile, city_owner(pcity))) {
    return FALSE;
  }

  if (ptile->worked && ptile->worked != pcity) {
    return FALSE;
  }

  if (ptile->owner && ptile->owner != pcity->owner) {
    return FALSE;
  }

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
    struct tile *ptile = city_map_to_map(pcity, city_x, city_y);

    map_city_radius_iterate(ptile, tile1) {
      struct city *pcity2 = tile_get_city(tile1);
      if (pcity2 && pcity2 != pcity) {
	int city_x2, city_y2;
	bool is_valid;

	is_valid = map_to_city_map(&city_x2, &city_y2, pcity2, ptile);
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

/****************************************************************************
  Updates the worked status of the tile (in map coordinates) for the city.
  If the status changes auto_arrange_workers may be called.  The caller needs
  to call sync_cities afterward for the affected city to be synced with the
  client.  auto_arrange_workers may be called within this function if a
  worker is displaced.

  It is safe to pass an out-of-range tile to this function.
****************************************************************************/
void update_city_tile_status_map(struct city *pcity, struct tile *ptile)
{
  int city_x, city_y;

  if (map_to_city_map(&city_x, &city_y, pcity, ptile)) {
    update_city_tile_status(pcity, city_x, city_y);
  }
}

/**************************************************************************
Updates the worked status of a tile.
city_x, city_y is in city map coords.
You need to call sync_cities for the affected cities to be synced with the
client.
**************************************************************************/
static void update_city_tile_status(struct city *pcity, int city_x,
				    int city_y)
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
      pcity->specialists[DEFAULT_SPECIALIST]++; /* keep city sanity */
      auto_arrange_workers(pcity); /* will place the displaced */
      city_refresh(pcity);
      send_city_info(NULL, pcity);
    }
    break;

  case C_TILE_UNAVAILABLE:
    if (is_available) {
      server_set_tile_city(pcity, city_x, city_y, C_TILE_EMPTY);
      /* We used to do an auto_arrange_workers in this case also.  But
       * that's a bad idea since it will spuriously move around workers that
       * have already been carefully placed. */
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

/**************************************************************************
  For each city adjacent to ptile, check all the buildings in the city.
  Any which have unmet terrain requirements will be sold.  This is called
  after any terrain changes (but this may be tied to the default ruleset).

  FIXME: This function isn't very general.  It would be better to check
  each turn to make sure all requirements of all buildings of all cities
  are met, and sell any buildings that can't be supported.  Terrains aren't
  the only requirement that may disappear.
**************************************************************************/
void city_landlocked_sell_coastal_improvements(struct tile *ptile)
{
  adjc_iterate(ptile, tile1) {
    struct city *pcity = tile_get_city(tile1);

    if (pcity && !is_ocean_near_tile(tile1)) {
      struct player *pplayer = city_owner(pcity);

      /* Sell all buildings (but not Wonders) that must be next to the ocean */
      built_impr_iterate(pcity, impr) {
        if (!can_city_sell_building(pcity, impr)) {
          continue;
        }

	requirement_vector_iterate(&get_improvement_type(impr)->reqs, preq) {
	  if (preq->source.type == REQ_TERRAIN
	      && !is_req_active(city_owner(pcity), pcity, NULL,
				NULL, NULL, NULL, NULL,
				preq)) {
          do_sell_building(pplayer, pcity, impr);
          notify_player(pplayer, tile1, E_IMP_SOLD,
                           _("You sell %s in %s (now landlocked)"
                             " for %d gold."),
                           get_improvement_name(impr), pcity->name,
                           impr_sell_gold(impr)); 
	  }
	} requirement_vector_iterate_end;
      } built_impr_iterate_end;
    }
  } adjc_iterate_end;
}

/****************************************************************************
  Refresh the city's vision.

  This function has very small overhead and can be called any time effects
  may have changed the vision range of the city.
****************************************************************************/
void city_refresh_vision(struct city *pcity)
{
  int radius_sq = get_city_bonus(pcity, EFT_CITY_VISION_RADIUS_SQ);

  vision_change_sight(pcity->server.vision, V_MAIN, radius_sq);
  vision_change_sight(pcity->server.vision, V_INVIS, 2);
}
