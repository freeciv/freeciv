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

#include "combat.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "rand.h"

#include "airgoto.h"
#include "maphand.h"
#include "settlers.h"
#include "unithand.h"
#include "unittools.h"

#include "aitools.h"

#include "gotohand.h"

struct move_cost_map warmap;

/* 
 * These settings should be either true or false.  They are used for
 * finding routes for airplanes - the airplane doesn't want to fly
 * through occupied territory, but most territory will be either
 * fogged or unknown entirely.  See airspace_looks_safe().  Note that
 * this is currently only used by the move-counting function
 * air_can_move_between(), not by the path-finding function (whatever
 * that may be).
 */
#define AIR_ASSUMES_UNKNOWN_SAFE        TRUE
#define AIR_ASSUMES_FOGGED_SAFE         TRUE

static bool airspace_looks_safe(struct tile *ptile, struct player *pplayer);


/* These are used for all GOTO's */

/* A byte must be able to hold this value i.e. is must be less than
   256. */
#define MAXCOST 255
#define MAXARRAYS 10000
#define ARRAYLENGTH 10

struct mappos_array {
  int first_pos;
  int last_pos;
  struct tile *tile[ARRAYLENGTH];
  struct mappos_array *next_array;
};

struct array_pointer {
  struct mappos_array *first_array;
  struct mappos_array *last_array;
};

static struct mappos_array *mappos_arrays[MAXARRAYS];
static struct array_pointer cost_lookup[MAXCOST];
static int array_count;
static int lowest_cost;
static int highest_cost;


/*************************************************************************
 Used to check if the warmap distance to a position is "finite".
*************************************************************************/
bool is_dist_finite(int dist)
{

  return (dist < MAXCOST);
}

/**************************************************************************
...
**************************************************************************/
static void init_queue(void)
{
  int i;
  static bool is_initialized = FALSE;

  if (!is_initialized) {
    for (i = 0; i < MAXARRAYS; i++) {
      mappos_arrays[i] = NULL;
    }
    is_initialized = TRUE;
  }

  for (i = 0; i < MAXCOST; i++) {
    cost_lookup[i].first_array = NULL;
    cost_lookup[i].last_array = NULL;
  }
  array_count = 0;
  lowest_cost = 0;
  highest_cost = 0;
}

/**************************************************************************
...
**************************************************************************/
static struct mappos_array *get_empty_array(void)
{
  struct mappos_array *parray;
  if (!mappos_arrays[array_count])
    mappos_arrays[array_count] = fc_malloc(sizeof(struct mappos_array));
  parray = mappos_arrays[array_count++];
  parray->first_pos = 0;
  parray->last_pos = -1;
  parray->next_array = NULL;
  return parray;
}

/**************************************************************************
...
**************************************************************************/
static void add_to_mapqueue(int cost, struct tile *ptile)
{
  struct mappos_array *our_array;

  assert(cost < MAXCOST && cost >= 0);

  our_array = cost_lookup[cost].last_array;
  if (!our_array) {
    our_array = get_empty_array();
    cost_lookup[cost].first_array = our_array;
    cost_lookup[cost].last_array = our_array;
  } else if (our_array->last_pos == ARRAYLENGTH-1) {
    our_array->next_array = get_empty_array();
    our_array = our_array->next_array;
    cost_lookup[cost].last_array = our_array;
  }

  our_array->tile[++(our_array->last_pos)] = ptile;
  if (cost > highest_cost)
    highest_cost = cost;
  freelog(LOG_DEBUG, "adding cost:%i at %i,%i", cost, ptile->x, ptile->y);
}

/**************************************************************************
...
**************************************************************************/
static struct tile *get_from_mapqueue(void)
{
  struct mappos_array *our_array;
  struct tile *ptile;

  freelog(LOG_DEBUG, "trying get");
  while (lowest_cost < MAXCOST) {
    if (lowest_cost > highest_cost)
      return FALSE;
    our_array = cost_lookup[lowest_cost].first_array;
    if (!our_array) {
      lowest_cost++;
      continue;
    }
    if (our_array->last_pos < our_array->first_pos) {
      if (our_array->next_array) {
	cost_lookup[lowest_cost].first_array = our_array->next_array;
	continue; /* note NOT "lowest_cost++;" */
      } else {
	cost_lookup[lowest_cost].first_array = NULL;
	lowest_cost++;
	continue;
      }
    }
    ptile = our_array->tile[our_array->first_pos];
    our_array->first_pos++;
    return ptile;
  }
  return NULL;
}

/**************************************************************************
Reset the movecosts of the warmap.
**************************************************************************/
static void init_warmap(struct tile *orig_tile, enum unit_move_type move_type)
{
  if (warmap.size != MAX_MAP_INDEX) {
    warmap.cost = fc_realloc(warmap.cost,
			     MAX_MAP_INDEX * sizeof(*warmap.cost));
    warmap.seacost = fc_realloc(warmap.seacost,
				MAX_MAP_INDEX * sizeof(*warmap.seacost));
    warmap.vector = fc_realloc(warmap.vector,
			       MAX_MAP_INDEX * sizeof(*warmap.vector));
    warmap.size = MAX_MAP_INDEX;
  }

  init_queue();

  switch (move_type) {
  case LAND_MOVING:
  case HELI_MOVING:
  case AIR_MOVING:
    assert(sizeof(*warmap.cost) == sizeof(char));
    memset(warmap.cost, MAXCOST, map.xsize * map.ysize);
    WARMAP_COST(orig_tile) = 0;
    break;
  case SEA_MOVING:
    assert(sizeof(*warmap.seacost) == sizeof(char));
    memset(warmap.seacost, MAXCOST, map.xsize * map.ysize);
    WARMAP_SEACOST(orig_tile) = 0;
    break;
  default:
    freelog(LOG_ERROR, "Bad move_type in init_warmap().");
  }
}  


/**************************************************************************
This creates a movecostmap (warmap), which is a two-dimentional array
corresponding to the real map. The value of a position is the number of
moves it would take to get there. If the function found no route the cost
is MAXCOST. (the value it is initialized with)
For sea units we let the map overlap onto the land one field, to allow
transporters and shore bombardment (ships can target the shore).
This map does NOT take enemy units onto account, nor ZOC.
Let generate_warmap interface to this function. Sometimes (in the AI when
using transports) this function will be called directly.

It is an search via Dijkstra's Algorithm
(see fx http://odin.ee.uwa.edu.au/~morris/Year2/PLDS210/dijkstra.html)
ie it piles tiles on a queue, and then use those tiles to find more tiles,
until all tiles we can reach are visited. To avoid making the warmap
potentially too big (heavy to calculate), the warmap is initialized with
a maxcost value, limiting the maximum length.

Note that this function is responsible for 20% of the CPU usage of freeciv...

This function is used by the AI in various cases when it is searching for
something to do with a unit. Maybe we could make a version that processed
the tiles in order after how many moves it took the unit to get to them,
and then gave them to the unit. This would achive 2 things:
-The unit could stop the function the instant it found what it was looking
for, or when it had already found something reasonably close and the distance
to the tiles later being searched was reasoable big. In both cases avoiding
to build the full warmap.
-The warmap would avoid examining a tile multiple times due to a series of
path with succesively smaller cost, since we would always have the smallest
possible cost the first time.
This would be done by inserting the tiles in a list after their move_cost
as they were found.
**************************************************************************/
void really_generate_warmap(struct city *pcity, struct unit *punit,
			    enum unit_move_type move_type)
{
  int move_cost;
  struct tile *orig_tile;
  bool igter;
  int maxcost = THRESHOLD * 6 + 2; /* should be big enough without being TOO big */
  struct tile *ptile;
  struct player *pplayer;

  if (pcity) {
    orig_tile = pcity->tile;
    pplayer = city_owner(pcity);
  } else {
    orig_tile = punit->tile;
    pplayer = unit_owner(punit);
  }

  init_warmap(orig_tile, move_type);
  add_to_mapqueue(0, orig_tile);

  if (punit && unit_flag(punit, F_IGTER))
    igter = TRUE;
  else
    igter = FALSE;

  /* FIXME: Should this apply only to F_CITIES units? -- jjm */
  if (punit
      && unit_flag(punit, F_SETTLERS)
      && unit_move_rate(punit)==3)
    maxcost /= 2;
  /* (?) was punit->type == U_SETTLERS -- dwp */

  while ((ptile = get_from_mapqueue())) {
    /* Just look up the cost value once.  This is a minor optimization but
     * it makes a big difference since this code is called so much. */
    unsigned char cost = ((move_type == SEA_MOVING)
			  ? WARMAP_SEACOST(ptile) : WARMAP_COST(ptile));

    adjc_dir_iterate(ptile, tile1, dir) {
      switch (move_type) {
      case LAND_MOVING:
	if (WARMAP_COST(tile1) <= cost)
	  continue; /* No need for all the calculations */

        if (is_ocean(map_get_terrain(tile1))) {
          if (punit && ground_unit_transporter_capacity(tile1, pplayer) > 0)
	    move_cost = SINGLE_MOVE;
          else
	    continue;
	} else if (is_ocean(ptile->terrain)) {
	  int base_cost = get_tile_type(map_get_terrain(tile1))->movement_cost * SINGLE_MOVE;
	  move_cost = igter ? MOVE_COST_ROAD : MIN(base_cost, unit_move_rate(punit));
        } else if (igter)
	  /* NOT c = 1 (Syela) [why not? - Thue] */
	  move_cost = (ptile->move_cost[dir] != 0 ? SINGLE_MOVE : 0);
        else if (punit)
	  move_cost = MIN(ptile->move_cost[dir], unit_move_rate(punit));
	/* else c = ptile->move_cost[k]; 
	   This led to a bad bug where a unit in a swamp was considered too far away */
        else { /* we have a city */
	  int tmp = tile1->move_cost[DIR_REVERSE(dir)];
          move_cost = (ptile->move_cost[dir] + tmp +
		       (ptile->move_cost[dir] > tmp ? 1 : 0))/2;
        }

        move_cost += cost;
        if (WARMAP_COST(tile1) > move_cost && move_cost < maxcost) {
          WARMAP_COST(tile1) = move_cost;
          add_to_mapqueue(move_cost, tile1);
        }
	break;


      case SEA_MOVING:
        move_cost = SINGLE_MOVE;
        move_cost += cost;
        if (WARMAP_SEACOST(tile1) > move_cost && move_cost < maxcost) {
	  /* by adding the move_cost to the warmap regardless if we
	     can move between we allow for shore bombardment/transport
	     to inland positions/etc. */
          WARMAP_SEACOST(tile1) = move_cost;
	  if (ptile->move_cost[dir] == MOVE_COST_FOR_VALID_SEA_STEP) {
	    add_to_mapqueue(move_cost, tile1);
	  }
	}
	break;
      default:
	move_cost = 0; /* silence compiler warning */
	die("Bad/unimplemented move_type in really_generate_warmap().");
      }
    } adjc_dir_iterate_end;
  }

  freelog(LOG_DEBUG, "Generated warmap for (%d,%d).",
	  TILE_XY(orig_tile)); 
}

/**************************************************************************
This is a wrapper for really_generate_warmap that checks if the warmap we
want is allready in existence. Also calls correctly depending on whether
pcity and/or punit is nun-NULL.
FIXME: Why is the movetype not used initialized on the warmap? Leaving it
for now.
**************************************************************************/
void generate_warmap(struct city *pcity, struct unit *punit)
{
  freelog(LOG_DEBUG, "Generating warmap, pcity = %s, punit = %s",
	  (pcity ? pcity->name : "NULL"),
	  (punit ? unit_type(punit)->name : "NULL"));

  if (punit) {
    /* 
     * Checking for an existing warmap. If the previous warmap was for
     * a city and our unit is in that city, use city's warmap.
     */
    if (pcity && pcity == warmap.warcity) {
      return;
    }

    /* 
     * If the previous warmap was for the same unit and it's still
     * correct (warmap.(sea)cost[x][y] == 0), reuse it.
     */
    if (warmap.warunit == punit &&
	(is_sailing_unit(punit) ? (WARMAP_SEACOST(punit->tile) == 0)
	 : (WARMAP_COST(punit->tile) == 0))) {
      return;
    }

    pcity = NULL;
  }

  warmap.warcity = pcity;
  warmap.warunit = punit;

  if (punit) {
    if (is_sailing_unit(punit)) {
      really_generate_warmap(pcity, punit, SEA_MOVING);
    } else {
      really_generate_warmap(pcity, punit, LAND_MOVING);
    }
    warmap.orig_tile = punit->tile;
  } else {
    really_generate_warmap(pcity, punit, LAND_MOVING);
    really_generate_warmap(pcity, punit, SEA_MOVING);
    warmap.orig_tile = pcity->tile;
  }
}

/**************************************************************************
Return the direction that takes us most directly to dest_x,dest_y.
The direction is a value for use in DIR_DX[] and DIR_DY[] arrays.

FIXME: This is a bit crude; this only gives one correct path, but sometimes
       there can be more than one straight path (fx going NW then W is the
       same as going W then NW)
**************************************************************************/
static int straightest_direction(struct tile *src_tile,
				 struct tile *dst_tile)
{
  int best_dir;
  int diff_x, diff_y;

  /* Should we go up or down, east or west: diff_x/y is the "step" in x/y. */
  map_distance_vector(&diff_x, &diff_y, src_tile, dst_tile);

  if (diff_x == 0) {
    best_dir = (diff_y > 0) ? DIR8_SOUTH : DIR8_NORTH;
  } else if (diff_y == 0) {
    best_dir = (diff_x > 0) ? DIR8_EAST : DIR8_WEST;
  } else if (diff_x > 0) {
    best_dir = (diff_y > 0) ? DIR8_SOUTHEAST : DIR8_NORTHEAST;
  } else if (diff_x < 0) {
    best_dir = (diff_y > 0) ? DIR8_SOUTHWEST : DIR8_NORTHWEST;
  } else {
    assert(0);
    best_dir = 0;
  }

  return (best_dir);
}

/**************************************************************************
Can we move between for ZOC? (only for land units). Includes a special
case only relevant for GOTOs (see below). came_from is a bit-vector
containing the directions we could have come from.
**************************************************************************/
static bool goto_zoc_ok(struct unit *punit, struct tile *src_tile,
			struct tile *dest_tile, dir_vector came_from)
{
  if (can_step_taken_wrt_to_zoc
      (punit->type, unit_owner(punit), src_tile, dest_tile))
    return TRUE;

  /* 
   * Both positions are currently enemy ZOC. Since the AI depend on
   * it's units bumping into enemies while on GOTO it need to be able
   * to plot a course which attacks enemy units.
   *
   * This code makes sure that if there is a unit in the way that the
   * GOTO made a path over (attack), the unit's ZOC effect (which is
   * now irrelevant, it is dead) doesn't have any effect.
   *
   * That is, if there is a unit standing on a tile that we (possibly)
   * came from, we will not take it into account.
   *
   * If this wasn't here a path involving two enemy units would not be
   * found by the algoritm.
   */

  /* If we just started the GOTO the enemy unit blocking ZOC on the tile
     we come from is still alive. */
  if (same_pos(src_tile, punit->tile)
      && !is_non_allied_unit_tile(dest_tile, unit_owner(punit))) {
    return FALSE;
  }

  {
    struct player *owner = unit_owner(punit);

    adjc_dir_iterate(src_tile, ptile, dir) {
      /* if we didn't come from there */
      if (!BV_ISSET(came_from, dir)
	  && !is_ocean(map_get_terrain(ptile))
	  /* and there is an enemy there */
	  && is_enemy_unit_tile(ptile, owner)) {
	/* then it counts in the zoc claculation */
	return FALSE;
      }
    } adjc_dir_iterate_end;
    return TRUE;
  }
}

/* DANGER_MOVE is the cost of movement assigned to "dangerous" tiles.  It
 * is arbitrarily larger than SINGLE_MOVE to make it a penalty. */
#define DANGER_MOVE (2 * SINGLE_MOVE + 1)

/**************************************************************************
This function mark paths on the warmaps vector showing the shortest path to
the destination.

It is an search via Dijkstra's Algorithm
(see fx http://odin.ee.uwa.edu.au/~morris/Year2/PLDS210/dijkstra.html)
We start with a list with only one tile (the
starttile), and then tries to move in each of the 8 directions from there.
This then gives us more tiles to more from. Repeat until we meet the
destination or there is no remaining tiles.
All possible paths coming out of the starttile are stored in local_vector.
After we've reached the destination, only the paths connecting starttile
with the destination are copied from local_vector to the warmap's vector.

Whenever we reach a tile we see how many movepoints it took to get there,
and compare it to eventual previous moves to the tile. If the route is
faster or just as fast we mark the direction from which we came on the
local_vector array and also record the new movecost. If we come to a
tile we have meet before, and the cost of the route we have taken is
smaller than the previous one we add the tile to the list again, to get
the tiles it leads to updated etc. The change in move_cost for the tile
will then propagate out.
We have a maxcost, as an upper limit for the length of gotos. When we
arrive at the GOTO destination with a path we know that we don't have to
follow other paths that have exceeded this length, and set maxcost to
length+1. (the +1 because we still want to find alternative paths of the
same length, and comparisons with maxcost are done with <).
When we have found all paths of the shortest length to the destination we
backtrack from the destination tile all the way back to the start tile,
marking the routes on the warmap's vector as we go. (again by piling tiles
on the queue). find_a_direction() can then use these routes to choose a
path.
Note that some move_cost values, like for going into unknown territory and
attacking while on GOTO is values not directly related to the move cost,
but rather meant to encourage/discourage certain behaviour.

The restrictions GOTO_MOVE_CARDINAL_ONLY and GOTO_MOVE_STRAIGHTEST are
currently only used for settlers building irrigation and roads.

To avoid RR loops (which may cause find_a_direction() to move a unit in
cute little cycles forever and ever...), when we have more than one path
to a tile and the second one is via RR (move_cost 0), we do NOT mark the
extra path.

At one point we used dir_ok to test if we were going in the right direction,
but the cost of the dir_ok call was too high in itself, and the penalty
sometimes lead to suboptimal paths chosen. The current implementation have
the disadvantage that it will search until all move_cost's are larger than
one found to the target, which can be expensive on RR (but not as bad as
you might think as most AI GOTO's will be long anyway).

Often we'll already have a working warmap, So we could maybe avoid using
this function to find a path if really_generate_warmap marked the paths
on the warmap. On the other side, most of the time GOTOs will be relatively
short, and doing extra work in really_generate_warmap, which will always
check a lot of tiles, may not be worth it. really_generate_warmap() is also
more crude than this function, so it would have to be expanded (which would
be _very_ expensive in CPU).

FIXME: this is a bit of a mess in not respecting FoW, and only sometimes
respecting if a square is known. (not introduced by me though) -Thue
**************************************************************************/
static bool find_the_shortest_path(struct unit *punit,
				   struct tile *dest_tile,
				  enum goto_move_restriction restriction)
{
  struct player *pplayer = unit_owner(punit);
  bool igter;
  struct tile *ptile, *orig_tile;
  struct tile *psrctile, *pdesttile;
  enum unit_move_type move_type = unit_type(punit)->move_type;
  int maxcost = MAXCOST;
  int move_cost, total_cost;
  int straight_dir = 0;	/* init to silence compiler warning */
  dir_vector local_vector[MAX_MAP_INDEX];
#define LOCAL_VECTOR(ptile) local_vector[(ptile)->index]
  struct unit *pcargo;
  /* 
   * Land/air units use warmap.cost while sea units use
   * warmap.seacost.  Some code will use both.
   */
  unsigned char *warmap_cost;

  orig_tile = punit->tile;

  if (same_pos(dest_tile, orig_tile)) {
    return TRUE;		/* [Kero] */
  }

  BV_CLR_ALL(LOCAL_VECTOR(orig_tile));

  init_warmap(punit->tile, move_type);
  warmap_cost = (move_type == SEA_MOVING) ? warmap.seacost : warmap.cost;
  add_to_mapqueue(0, orig_tile);

  if (punit && unit_flag(punit, F_IGTER))
    igter = TRUE;
  else
    igter = FALSE;

  /* If we have a passenger abord a ship we must be sure he can disembark
     This shouldn't be neccesary, as ZOC does not have an effect for sea-land
     movement, but some code in aiunit.c assumes the goto work like this, so
     I will leave it for now */
  if (move_type == SEA_MOVING) {
    pcargo = other_passengers(punit);
    if (pcargo)
      if (is_ocean(map_get_terrain(dest_tile)) ||
	  !is_non_allied_unit_tile(dest_tile, unit_owner(pcargo))
	  || is_allied_city_tile(dest_tile, unit_owner(pcargo))
	  || unit_flag(pcargo, F_MARINES)
	  || is_my_zoc(unit_owner(pcargo), dest_tile))
	pcargo = NULL;
  } else
    pcargo = NULL;

  /* until we have found the shortest paths */
  while ((ptile = get_from_mapqueue())) {
    psrctile = ptile;

    if (restriction == GOTO_MOVE_STRAIGHTEST)
      straight_dir = straightest_direction(ptile, dest_tile);

    /* Try to move to all tiles adjacent to x,y. The coordinats of the tile we
       try to move to are x1,y1 */
    adjc_dir_iterate(ptile, tile1, dir) {
      if ((restriction == GOTO_MOVE_CARDINAL_ONLY)
	  && !is_cardinal_dir(dir)) {
	continue;
      }

      pdesttile = tile1;

      switch (move_type) {
      case LAND_MOVING:
	if (WARMAP_COST(tile1) <= WARMAP_COST(ptile))
	  continue; /* No need for all the calculations. Note that this also excludes
		       RR loops, ie you can't create a cycle with the same move_cost */

	if (is_ocean(pdesttile->terrain)) {
	  if (ground_unit_transporter_capacity(tile1, unit_owner(punit)) <= 0)
	    continue;
	  else
	    move_cost = 3;
	} else if (is_ocean(psrctile->terrain)) {
	  int base_cost = get_tile_type(pdesttile->terrain)->movement_cost * SINGLE_MOVE;
	  move_cost = igter ? 1 : MIN(base_cost, unit_move_rate(punit));
	} else if (igter)
	  move_cost = (psrctile->move_cost[dir] != 0 ? SINGLE_MOVE : 0);
	else
	  move_cost = MIN(psrctile->move_cost[dir], unit_move_rate(punit));

	if (!pplayer->ai.control && !map_is_known(tile1, pplayer)) {
	  /* Don't go into the unknown. 5*SINGLE_MOVE is an arbitrary deterrent. */
	  move_cost = (restriction == GOTO_MOVE_STRAIGHTEST) ? SINGLE_MOVE : 5*SINGLE_MOVE;
	} else if (is_non_allied_unit_tile(pdesttile, unit_owner(punit))) {
	  if (is_ocean(psrctile->terrain) && !unit_flag(punit, F_MARINES)) {
	    continue; /* Attempting to attack from a ship */
	  }

	  if (!same_pos(tile1, dest_tile)) {
	    /* Allow players to target anything */
	    if (pplayer->ai.control) {
	      if ((!is_enemy_unit_tile(pdesttile, unit_owner(punit))
		   || !is_military_unit(punit))
		  && !is_diplomat_unit(punit)) {
		continue; /* unit is non_allied and non_enemy, ie non_attack */
	      } else {
		move_cost = SINGLE_MOVE; /* Attack cost */
	      }
	    } else {
	      continue; /* non-AI players don't attack on goto */
	    }
	  } else {
	    move_cost = SINGLE_MOVE;
	  }
	} else if (is_non_allied_city_tile(pdesttile, unit_owner(punit))) {
	  if (is_ocean(psrctile->terrain) && !unit_flag(punit, F_MARINES)) {
	    continue; /* Attempting to attack from a ship */
	  }

	  if ((is_non_attack_city_tile(pdesttile, unit_owner(punit))
	       || !is_military_unit(punit))) {
	    if (!is_diplomat_unit(punit)
		&& !same_pos(tile1, dest_tile)) {
	      /* Allow players to target anything */
	      continue;
	    } else {
	      /* Assign the basic move cost.  There's a penalty for
	       * dangerous tiles. */
	      move_cost
		= ((unit_loss_pct(unit_owner(punit), tile1, punit) > 0)
		   ? DANGER_MOVE : SINGLE_MOVE);
	    }
	  }
	} else if (!goto_zoc_ok(punit, ptile, tile1, LOCAL_VECTOR(ptile))) {
	  continue;
	}

	if (restriction == GOTO_MOVE_STRAIGHTEST && dir == straight_dir)
	  move_cost /= SINGLE_MOVE;

	total_cost = move_cost + WARMAP_COST(ptile);
	break;

      case SEA_MOVING:
	if (WARMAP_SEACOST(tile1) <= WARMAP_SEACOST(ptile))
	  continue; /* No need for all the calculations */

	/* allow ships to target a shore */
	if (psrctile->move_cost[dir] != MOVE_COST_FOR_VALID_SEA_STEP
	    && !same_pos(tile1, dest_tile)) {
	  continue;
	} else if (unit_loss_pct(unit_owner(punit), tile1, punit) > 0) {
	  /* Impose a penalty for travel over dangerous tiles. */
	  move_cost = DANGER_MOVE;
	} else {
	  move_cost = SINGLE_MOVE;
	}

	/* See previous comment on pcargo */
	if (same_pos(tile1, dest_tile) && pcargo
	    && move_cost < 20 * SINGLE_MOVE
	    && !is_my_zoc(unit_owner(pcargo), ptile)) {
	  move_cost = 20 * SINGLE_MOVE;
	}

	if (!pplayer->ai.control && !map_is_known(tile1, pplayer))
	  move_cost = (restriction == GOTO_MOVE_STRAIGHTEST) ? SINGLE_MOVE : 5*SINGLE_MOVE; /* arbitrary deterrent. */

	/* We don't allow attacks during GOTOs here; you can almost
	   always find a way around enemy units on sea */
	if (!same_pos(tile1, dest_tile)) {
	  if (is_non_allied_unit_tile(pdesttile, unit_owner(punit))
	      || is_non_allied_city_tile(pdesttile, unit_owner(punit)))
	    continue;
	}

	if ((restriction == GOTO_MOVE_STRAIGHTEST) && (dir == straight_dir))
	  move_cost /= SINGLE_MOVE;

	total_cost = move_cost + WARMAP_SEACOST(ptile);

	/* For the ai, maybe avoid going close to enemies. */
	if (pplayer->ai.control &&
	    WARMAP_SEACOST(ptile) < punit->moves_left
	    && total_cost < maxcost
	    && total_cost >= punit->moves_left - (get_transporter_capacity(punit) >
			      unit_type(punit)->attack_strength ? 3 : 2)
	    && enemies_at(punit, tile1)) {
	  total_cost += unit_move_rate(punit);
	  freelog(LOG_DEBUG, "%s#%d@(%d,%d) dissuaded from (%d,%d) -> (%d,%d)",
		  unit_type(punit)->name, punit->id,
		  TILE_XY(punit->tile), TILE_XY(tile1), TILE_XY(dest_tile));
	}
	break;

      case AIR_MOVING:
      case HELI_MOVING:
	if (WARMAP_COST(tile1) <= WARMAP_COST(ptile))
	  continue; /* No need for all the calculations */

	move_cost = SINGLE_MOVE;
	/* Planes could run out of fuel, therefore we don't care if territory
	   is unknown. Also, don't attack except at the destination. */

	if (!same_pos(tile1, dest_tile)
	    && !airspace_looks_safe(tile1, pplayer)) {
	  /* If it's not our destination, we check if it's safe */
	  continue;
	}

	if ((restriction == GOTO_MOVE_STRAIGHTEST) && (dir == straight_dir))
	  move_cost /= SINGLE_MOVE;
	total_cost = move_cost + WARMAP_COST(ptile);
	break;

      default:
	move_cost = MAXCOST;	/* silence compiler warning */
	total_cost = MAXCOST;	/* silence compiler warning */
	die("Bad move_type in find_the_shortest_path().");
      } /****** end switch ******/

      /* Add the route to our warmap if it is worth keeping */
      if (total_cost < maxcost) {
	if (warmap_cost[tile1->index] > total_cost) {
	  warmap_cost[tile1->index] = total_cost;
	  add_to_mapqueue(total_cost, tile1);
	  BV_CLR_ALL(LOCAL_VECTOR(tile1));
	  BV_SET(LOCAL_VECTOR(tile1), DIR_REVERSE(dir));
	  freelog(LOG_DEBUG,
		  "Candidate: %s from (%d, %d) to (%d, %d), cost %d",
		  dir_get_name(dir), TILE_XY(ptile), TILE_XY(tile1),
		  total_cost);
	} else if (warmap_cost[tile1->index] == total_cost) {
	  BV_SET(LOCAL_VECTOR(tile1), DIR_REVERSE(dir));
	  freelog(LOG_DEBUG,
		  "Co-Candidate: %s from (%d, %d) to (%d, %d), cost %d",
		  dir_get_name(dir), TILE_XY(ptile), TILE_XY(tile1),
		  total_cost);
	}
      }

      if (same_pos(tile1, dest_tile) && maxcost > total_cost) {
	freelog(LOG_DEBUG, "Found path, cost = %d", total_cost);
	/* Make sure we stop searching when we have no hope of finding a shorter path */
        maxcost = total_cost + 1;
      }
    } adjc_dir_iterate_end;
  }

  freelog(LOG_DEBUG, "GOTO: (%d, %d) -> (%d, %d), cost = %d", 
	  TILE_XY(orig_tile), TILE_XY(dest_tile), maxcost - 1);

  if (maxcost == MAXCOST)
    return FALSE; /* No route */

  /*** Succeeded. The vector at the destination indicates which way we get there.
     Now backtrack to remove all the blind paths ***/
  assert(sizeof(*warmap.vector) == sizeof(char));
  memset(warmap.vector, 0, map.xsize * map.ysize);

  init_queue();
  add_to_mapqueue(0, dest_tile);

  while (TRUE) {
    if (!(ptile = get_from_mapqueue()))
      break;

    adjc_dir_iterate(ptile, tile1, dir) {
      if ((restriction == GOTO_MOVE_CARDINAL_ONLY)
	  && !is_cardinal_dir(dir)) continue;

      if (BV_ISSET(LOCAL_VECTOR(ptile), dir)) {
	move_cost = (move_type == SEA_MOVING)
		? WARMAP_SEACOST(tile1)
		: WARMAP_COST(tile1);

        add_to_mapqueue(MAXCOST-1 - move_cost, tile1);
	/* Mark it on the warmap */
	WARMAP_VECTOR(tile1) |= 1 << DIR_REVERSE(dir);	
	BV_CLR(LOCAL_VECTOR(ptile), dir); /* avoid repetition */
	freelog(LOG_DEBUG, "PATH-SEGMENT: %s from (%d, %d) to (%d, %d)",
		dir_get_name(DIR_REVERSE(dir)),
		TILE_XY(tile1), TILE_XY(ptile));
      }
    } adjc_dir_iterate_end;
  }

  return TRUE;
}

/****************************************************************************
  Can the player see that the given ocean tile is along the coastline?
****************************************************************************/
static bool is_coast_seen(struct tile *ptile, struct player *pplayer)
{
  bool ai_always_see_map = !ai_handicap(pplayer, H_MAP);

  adjc_iterate(ptile, adjc_tile) {
    /* Is there land here, and if so can we see it? */
    if (!is_ocean(map_get_terrain(adjc_tile))
	&& (ai_always_see_map || map_is_known(adjc_tile, pplayer))) {
      return TRUE;
    }
  } adjc_iterate_end;

  return FALSE;
}

/**************************************************************************
This is used to choose among the valid directions marked on the warmap
by the find_the_shortest_path() function. Returns a direction or -1 if
none could be found.

Notes on the implementation: 

1. fitness[8] contains the fitness of the directions.  Bigger number means
  safer direction.  It takes into account: how well the unit will be
  defended at the next tile, how many possible attackers there are
  around it, whether it is likely to sink there (for triremes).

2. This function does not check for loops, which we currently rely on
  find_the_shortest_path() to make sure there are none of. If the
  warmap did contain a loop repeated calls of this function may result
  in the unit going in cycles forever.

3. It doesn't check for ZOC as it has been done in
  find_the_shortest_path (which is called every turn).

**************************************************************************/
static int find_a_direction(struct unit *punit,
			    enum goto_move_restriction restriction,
			    struct tile *dest_tile)
{
#define UNIT_DEFENSE(punit, ptile, defence_multiplier) \
  ((get_virtual_defense_power(U_LAST, (punit)->type, (ptile), FALSE, 0) * \
    (defence_multiplier)) / 2)

#define UNIT_RATING(punit, ptile, defence_multiplier) \
  (UNIT_DEFENSE(punit, ptile, defence_multiplier) * (punit)->hp)

  /*
   * 6% of the fitness value is substracted for directions where the
   * destination is unknown and we don't have enough move points.
   */
#define UNKNOWN_FITNESS_PENALTY_PERCENT	6

  /*
   * Use this if you want to indicate that this direction shouldn't be
   * selected.
   */
#define DONT_SELECT_ME_FITNESS		(-1)

  int i, fitness[8], best_fitness = DONT_SELECT_ME_FITNESS;
  struct unit *passenger;
  struct player *pplayer = unit_owner(punit);
  bool afraid_of_sinking = (unit_flag(punit, F_TRIREME)
			    && get_player_bonus(pplayer,
						EFT_NO_SINK_DEEP) == 0);

  /* 
   * If the destination is one step away, look around first or just go
   * there?
   */ 
  bool do_full_check = afraid_of_sinking; 

  if (is_ocean(map_get_terrain(punit->tile))) {
    passenger = other_passengers(punit);
  } else {
    passenger = NULL;
  }

  /* 
   * If we can get to the destination right away there is nothing to
   * be gained from going round in little circles to move across
   * desirable squares.
   *
   * Actually there are things to gain, in AI case, like the safety
   * checks -- GB
   */
  if (!do_full_check) {
    enum direction8 dir;

    if (base_get_direction_for_step(punit->tile, dest_tile, &dir)
	&& TEST_BIT(WARMAP_VECTOR(punit->tile), dir)
	&& !(restriction == GOTO_MOVE_CARDINAL_ONLY
	     && !is_cardinal_dir(dir))) {
      return dir;
    }
  }

  for (i = 0; i < 8; i++) {
    fitness[i] = DONT_SELECT_ME_FITNESS;
  }

  /*
   * Loop over all directions, fill the fitness array and update
   * best_fitness.
   */
  adjc_dir_iterate(punit->tile, ptile, dir) {
    int defence_multiplier, num_of_allied_units, best_friendly_defence,
	base_move_cost;
    struct city *pcity = map_get_city(ptile);
    struct unit *best_ally;

    /* 
     * Is it an allowed direction?  is it marked on the warmap?
     */
    if (!TEST_BIT(WARMAP_VECTOR(punit->tile), dir)
	|| ((restriction == GOTO_MOVE_CARDINAL_ONLY)
	    && !is_cardinal_dir(dir))) {
      /* make sure we don't select it later */
      fitness[dir] = DONT_SELECT_ME_FITNESS;
      continue;
    }

    /* 
     * Determine the cost of the proposed move.
     */
    if (is_ground_unit(punit)) {
      /* assuming move is valid, but what if unit is trying to board? 
	 -- GB */
      base_move_cost = punit->tile->move_cost[dir];
    } else {
      base_move_cost = SINGLE_MOVE;
    }

    if (unit_flag(punit, F_IGTER) && base_move_cost >= MOVE_COST_ROAD) {
      base_move_cost = MOVE_COST_ROAD;
    }

    freelog(LOG_DEBUG, "%d@(%d,%d) evaluating (%d,%d)[%d/%d]", punit->id, 
            TILE_XY(punit->tile), TILE_XY(ptile),
	    base_move_cost, punit->moves_left);

    /* 
     * Calculate the defence multiplier of this tile. Both the unit
     * itself and its ally benefit from it.
     */
    defence_multiplier = 2;
    if (pcity) {
      /* This isn't very accurate. */
      defence_multiplier += (get_city_bonus(pcity, EFT_LAND_DEFEND)
			     + get_city_bonus(pcity, EFT_MISSILE_DEFEND)
			     + get_city_bonus(pcity, EFT_AIR_DEFEND)
			     + get_city_bonus(pcity, EFT_SEA_DEFEND)) / 100;
    }

    /* 
     * Find the best ally unit at the target tile.
     */
    best_ally = NULL;
    num_of_allied_units = 0;
    {
      /* 
       * Initialization only for the compiler since it is guarded by
       * best_ally.
       */
      int rating_of_best_ally = 0;

      unit_list_iterate(ptile->units, aunit) {
	if (pplayers_allied(unit_owner(aunit), unit_owner(punit))) {
	  int rating_of_current_ally =
	      UNIT_RATING(aunit, ptile, defence_multiplier);
	  num_of_allied_units++;

	  if (!best_ally || rating_of_current_ally > rating_of_best_ally) {
	    best_ally = aunit;
	    rating_of_best_ally = rating_of_current_ally;
	  }
	}
      } unit_list_iterate_end;
    }

    if (best_ally) {
      best_friendly_defence =
	  MAX(UNIT_DEFENSE(punit, ptile, defence_multiplier),
	      UNIT_DEFENSE(best_ally, ptile, defence_multiplier));
    } else {
      best_friendly_defence =
	  UNIT_DEFENSE(punit, ptile, defence_multiplier);
    }

    /* 
     * Fill fitness[dir] based on the rating of punit and best_ally.
     */
    {
      /* calculate some clever weights basing on defence stats */
      int rating_of_ally, rating_of_unit =
	  UNIT_RATING(punit, ptile, defence_multiplier);
      
      assert((best_ally != NULL) == (num_of_allied_units > 0));

      if (best_ally) {
	rating_of_ally = UNIT_RATING(best_ally, ptile, defence_multiplier);
      } else {
	rating_of_ally = 0;	/* equivalent of having 0 HPs */
      }

      if (num_of_allied_units == 0) {
	fitness[dir] = rating_of_unit;
      } else if (pcity || tile_has_special(ptile, S_FORTRESS)) {
	fitness[dir] = MAX(rating_of_unit, rating_of_ally);
      } else if (rating_of_unit <= rating_of_ally) {
	fitness[dir] = rating_of_ally * (num_of_allied_units /
					 (num_of_allied_units + 1));
      } else { 
	fitness[dir] = MIN(rating_of_unit * (num_of_allied_units + 1),
			   rating_of_unit * rating_of_unit *
			   (num_of_allied_units /
			    MAX((num_of_allied_units + 1),
				(rating_of_ally *
				 (num_of_allied_units + 1)))));
      }
    }
    
    /* 
     * In case we change directions next turn, roads and railroads are
     * nice.
     */
    if (tile_has_special(ptile, S_ROAD) || tile_has_special(ptile, S_RAILROAD)) {
      fitness[dir] += 10;
    }

    /* 
     * What is around the tile we are about to step to?
     */
    adjc_iterate(ptile, adjtile) {
      if (!map_is_known(adjtile, pplayer)) {
	if (punit->moves_left < base_move_cost) {
	  /* Avoid the unknown */
	  fitness[dir] -=
	      (fitness[dir] * UNKNOWN_FITNESS_PENALTY_PERCENT) / 100;
	} else {
	  /* nice but not important */
	  fitness[dir]++;
	}
      } else {
	/* lookin for trouble */
	if (punit->moves_left < base_move_cost + SINGLE_MOVE) {
	  /* can't fight */
	  unit_list_iterate(adjtile->units, aunit) {
	    int attack_of_enemy;

	    if (!pplayers_at_war(unit_owner(aunit), unit_owner(punit))) {
	      continue;
	    }

	    attack_of_enemy = get_attack_power(aunit);
	    if (attack_of_enemy == 0) {
	      continue;
	    }
	    
	    if (passenger && !is_ground_unit(aunit)) {
	      /* really don't want that direction */
	      fitness[dir] = DONT_SELECT_ME_FITNESS;
	    } else {
	      fitness[dir] -=
		  best_friendly_defence * (aunit->hp * attack_of_enemy *
					   attack_of_enemy /
					   (attack_of_enemy *
					    attack_of_enemy +
					    best_friendly_defence *
					    best_friendly_defence));
	    }
	  } unit_list_iterate_end;
	}
      } /* end this-tile-is-seen else */
    } adjc_iterate_end;
    
    /* 
     * Try to make triremes safe 
     */
    if (afraid_of_sinking && !is_coast_seen(ptile, pplayer)) {
      if (punit->moves_left < 2 * SINGLE_MOVE) {
	fitness[dir] = DONT_SELECT_ME_FITNESS;
	continue;
      }

      /* We have enough moves and will be able to get back. */
      fitness[dir] = 1;
    }

    /* 
     * Clip the fitness value.
     */
    if (fitness[dir] < 1 && (!unit_owner(punit)->ai.control
                     || punit->ai.passenger == 0
                     || punit->moves_left >= 2 * SINGLE_MOVE)) {
      fitness[dir] = 1;
    }
    if (fitness[dir] < 0) {
      fitness[dir] = DONT_SELECT_ME_FITNESS;
    }

    /* 
     * Better than the best known?
     */
    if (fitness[dir] != DONT_SELECT_ME_FITNESS
	&& fitness[dir] > best_fitness) {
      best_fitness = fitness[dir];
      freelog(LOG_DEBUG, "New best = %d: dir=%d (%d, %d) -> (%d, %d)",
	      best_fitness, dir, TILE_XY(punit->tile), TILE_XY(ptile));
    }
  } adjc_dir_iterate_end;

  if (best_fitness == DONT_SELECT_ME_FITNESS && afraid_of_sinking
      && !is_safe_ocean(punit->tile)) {
    /* 
     * We've got a trireme in the middle of the sea. With
     * best_fitness==DONT_SELECT_ME_FITNESS, it'll end its turn right
     * there and could very well die. We'll try to rescue.
     */
    freelog(LOG_DEBUG,
	    "%s's trireme in trouble at (%d,%d), looking for coast",
	    pplayer->name, TILE_XY(punit->tile));

    adjc_dir_iterate(punit->tile, ptile, dir) {
      if (is_coast_seen(ptile, pplayer)) {
	fitness[dir] = 1;
	best_fitness = 1;
	freelog(LOG_DEBUG, "found coast");
      }
    } adjc_dir_iterate_end;
  }

  if (best_fitness == DONT_SELECT_ME_FITNESS) {
    freelog(LOG_DEBUG, "find_a_direction: no good direction found");
    return -1;
  }

  /*
   * Find random direction out of the best ones selected.
   */
  for (;;) {
    int dir = myrand(8);

    if (fitness[dir] == best_fitness) {
      freelog(LOG_DEBUG,
	      "find_a_direction: returning dir=%d with fitness=%d", dir,
	      fitness[dir]);
      return dir;
    }
  }

#undef UNIT_DEFENSE
#undef UNIT_RATING
#undef UNKNOWN_FITNESS_PENALTY_PERCENT
#undef DONT_SELECT_ME_FITNESS
}

/**************************************************************************
  Basic checks as to whether a GOTO is possible. The target (x,y) should
  be on the same continent as punit is, up to embarkation/disembarkation.
**************************************************************************/
bool goto_is_sane(struct unit *punit, struct tile *ptile, bool omni)
{  
  struct player *pplayer = unit_owner(punit);

  if (same_pos(punit->tile, ptile)) {
    return TRUE;
  }

  if (!(omni || map_is_known_and_seen(ptile, pplayer))) {
    /* The destination is in unknown -- assume sane */
    return TRUE;
  }

  switch (unit_type(punit)->move_type) {

  case LAND_MOVING:
    if (is_ocean(map_get_terrain(ptile))) {
      /* Going to a sea tile, the target should be next to our continent 
       * and with a boat */
      if (ground_unit_transporter_capacity(ptile, pplayer) > 0) {
        adjc_iterate(ptile, tmp_tile) {
          if (map_get_continent(tmp_tile) == map_get_continent(punit->tile))
            /* The target is adjacent to our continent! */
            return TRUE;
        } adjc_iterate_end;
      }
    } else {
      /* Going to a land tile: better be our continent */
      if (map_get_continent(punit->tile) == map_get_continent(ptile)) {
        return TRUE;
      } else {
        /* Well, it's not our continent, but maybe we are on a boat
         * adjacent to the target continent? */
	adjc_iterate(punit->tile, tmp_tile) {
	  if (map_get_continent(tmp_tile) == map_get_continent(ptile)) {
	    return TRUE;
          }
	} adjc_iterate_end;
      }
    }
      
    return FALSE;

  case SEA_MOVING:
    if (is_ocean(map_get_terrain(ptile))
        || is_ocean_near_tile(ptile)) {
      /* The target is sea or is accessible from sea 
       * (allow for bombardment and visiting ports) */
      return TRUE;
    }
    return FALSE;

  default:
    return TRUE;
  }
}


/**************************************************************************
Handles a unit goto (Duh?)
Uses find_the_shortest_path() to find all the shortest paths to the goal.
Uses find_a_direction() to choose between these.

If we have an air unit we use
find_air_first_destination(punit, &waypoint_x, &waypoint_y)
to get a waypoint to goto. The actual goto is still done with
find_the_shortest_path(pplayer, punit, waypoint_x, waypoint_y, restriction)
**************************************************************************/
enum goto_result do_unit_goto(struct unit *punit,
			      enum goto_move_restriction restriction,
			      bool trigger_special_ability)
{
  struct player *pplayer = unit_owner(punit);
  int unit_id;
  enum goto_result status;
  struct tile *ptile, *dest_tile, *waypoint_tile;

  assert(!unit_has_orders(punit));

  unit_id = punit->id;
  dest_tile = waypoint_tile = punit->goto_tile;

  if (same_pos(punit->tile, dest_tile) ||
      !goto_is_sane(punit, dest_tile, FALSE)) {
    punit->activity = ACTIVITY_IDLE;
    send_unit_info(NULL, punit);
    if (same_pos(punit->tile, dest_tile)) {
      return GR_ARRIVED;
    } else {
      return GR_FAILED;
    }
  }

  if(punit->moves_left == 0) {
    send_unit_info(NULL, punit);
    return GR_OUT_OF_MOVEPOINTS;
  }

  if (is_air_unit(punit)) {
    if (find_air_first_destination(punit, &waypoint_tile)) {
      /* this is a special case for air units who do not always want to move. */
      if (same_pos(waypoint_tile, punit->tile)) {
	punit->done_moving = TRUE;
	send_unit_info(NULL, punit);
	return GR_WAITING; /* out of fuel */
      }
    } else {
      freelog(LOG_VERBOSE, "Did not find an airroute for "
	      "%s's %s at (%d, %d) -> (%d, %d)",
	      pplayer->name, unit_type(punit)->name,
	      TILE_XY(punit->tile), TILE_XY(dest_tile));
      punit->activity = ACTIVITY_IDLE;
      send_unit_info(NULL, punit);
      return GR_FAILED;
    }
  }

  /* This has the side effect of marking the warmap with the possible paths */
  if (find_the_shortest_path(punit, waypoint_tile, restriction)) {
    do { /* move the unit along the path chosen by find_the_shortest_path() while we can */
      bool last_tile, success;
      struct unit *penemy = NULL;
      int dir;

      if (punit->moves_left == 0) {
	return GR_OUT_OF_MOVEPOINTS;
      }

      dir = find_a_direction(punit, restriction, waypoint_tile);
      if (dir < 0) {
	freelog(LOG_DEBUG, "%s#%d@(%d,%d) stalling so it won't be killed.",
		unit_type(punit)->name, punit->id,
		TILE_XY(punit->tile));
	return GR_FAILED;
      }

      freelog(LOG_DEBUG, "Going %s", dir_get_name(dir));
      ptile = mapstep(punit->tile, dir);

      penemy = is_enemy_unit_tile(ptile, unit_owner(punit));
      assert(punit->moves_left > 0);

      last_tile = same_pos(ptile, punit->goto_tile);

      /* Call handle_unit_move_request for humans and ai_unit_move for AI */
      success = (!pplayer->ai.control 
                 && handle_unit_move_request(punit, ptile, FALSE,
				 !(last_tile && trigger_special_ability)))
                || (pplayer->ai.control && ai_unit_move(punit, ptile));

      if (!player_find_unit_by_id(pplayer, unit_id)) {
	return GR_DIED;		/* unit died during goto! */
      }

      if (!success && punit->moves_left > 0) {
        /* failure other than ran out of movement during an 
           attempt to utilize the rand() move feature */
	punit->activity=ACTIVITY_IDLE;
	send_unit_info(NULL, punit);
	return GR_FAILED;
      }

      /* Don't attack more than once per goto */
      if (penemy && !pplayer->ai.control) {
 	punit->activity = ACTIVITY_IDLE;
 	send_unit_info(NULL, punit);
 	return GR_FOUGHT;
      }

      if(!same_pos(ptile, punit->tile)) {
	send_unit_info(NULL, punit);
	return GR_OUT_OF_MOVEPOINTS;
      }

      freelog(LOG_DEBUG, "Moving on.");
    } while(!same_pos(ptile, waypoint_tile));
  } else {
    freelog(LOG_VERBOSE, "Did not find the shortest path for "
	    "%s's %s at (%d, %d) -> (%d, %d)",
	    pplayer->name, unit_type(punit)->name,
	    TILE_XY(punit->tile), TILE_XY(dest_tile));
    handle_unit_activity_request(punit, ACTIVITY_IDLE);
    send_unit_info(NULL, punit);
    return GR_FAILED;
  }
  /** Finished moving the unit for this turn **/

  /* normally we would just do this unconditionally, but if we had an
     airplane goto we might not be finished even if the loop exited */
  if (same_pos(punit->tile, dest_tile)) {
    punit->activity = ACTIVITY_IDLE;
    status = GR_ARRIVED;
  } else {
    /* we have a plane refueling at a waypoint */
    status = GR_OUT_OF_MOVEPOINTS;
  }

  send_unit_info(NULL, punit);
  return status;
}

/**************************************************************************
Calculate and return cost (in terms of move points) for unit to move
to specified destination.
Currently only used in autoattack.c
**************************************************************************/
int calculate_move_cost(struct unit *punit, struct tile *dest_tile)
{
  /* perhaps we should do some caching -- fisch */

  if (is_air_unit(punit) || is_heli_unit(punit)) {
    /* The warmap only really knows about land and sea
       units, so for these we just assume cost = distance.
       (times 3 for road factor).
       (Could be wrong if there are things in the way.)
    */
    return SINGLE_MOVE * real_map_distance(punit->tile, dest_tile);
  }

  generate_warmap(NULL, punit);

  if (is_sailing_unit(punit))
    return WARMAP_SEACOST(dest_tile);
  else /* ground unit */
    return WARMAP_COST(dest_tile);
}


/**************************************************************************
 Returns true if the airspace at given map position _looks_ safe to
 the given player. The airspace is unsafe if the player believes
 there is an enemy unit on it. This is tricky, since we have to
 consider what the player knows/doesn't know about the tile.
**************************************************************************/
static bool airspace_looks_safe(struct tile *ptile, struct player *pplayer)
{
  /* 
   * We do handicap checks for the player with ai_handicap(). This
   * function returns true if the player is handicapped. For human
   * players they'll always return true. This is the desired behavior.
   */

  /* If the tile's unknown, we (may) assume it's safe. */
  if (ai_handicap(pplayer, H_MAP) && !map_is_known(ptile, pplayer)) {
    return AIR_ASSUMES_UNKNOWN_SAFE;
  }

  /* This is bad: there could be a city there that the player doesn't
      know about.  How can we check that? */
  if (is_non_allied_city_tile(ptile, pplayer)) {
    return FALSE;
  }

  /* If the tile's fogged we again (may) assume it's safe. */
  if (ai_handicap(pplayer, H_FOG) &&
      !map_is_known_and_seen(ptile, pplayer)) {
    return AIR_ASSUMES_FOGGED_SAFE;
  }

  /* The tile is unfogged so we can check for enemy units on the
     tile. */
  return !is_non_allied_unit_tile(ptile, pplayer);
}

/**************************************************************************
 An air unit starts (src_x,src_y) with moves moves and want to go to
 (dest_x,dest_y). It returns the number of moves left if this is
 possible without running out of moves. It returns -1 if it is
 impossible.

The function has 3 stages:
Try to rule out the possibility in O(1) time              else
Try to quickly verify in O(moves) time                    else
Do an A* search using the warmap to completely search for the path.
**************************************************************************/
int air_can_move_between(int moves, struct tile *src_tile,
			 struct tile *dest_tile, struct player *pplayer)
{
  struct tile *ptile;
  int dist, total_distance = real_map_distance(src_tile, dest_tile);

  freelog(LOG_DEBUG,
	  "air_can_move_between(moves=%d, src=(%i,%i), "
	  "dest=(%i,%i), player=%s)", moves, TILE_XY(src_tile),
	  TILE_XY(dest_tile), pplayer->name);

  /* First we do some very simple O(1) checks. */
  if (total_distance > moves) {
    return -1;
  }
  if (total_distance == 0) {
    return moves;
  }

  /* 
   * Then we do a fast O(n) straight-line check.  It'll work so long
   * as the straight-line doesn't cross any unreal tiles, unknown
   * tiles, or enemy-controlled tiles.  So, it should work most of the
   * time. 
   */
  ptile = src_tile;
  
  /* 
   * We don't check the endpoint of the goto, since it is possible
   * that the endpoint is a tile which has an enemy which should be
   * attacked. But we do check that all points in between are safe.
   */
  for (dist = total_distance; dist > 1; dist--) {
    /* Warning: straightest_direction may not actually follow the
       straight line. */
    int dir = straightest_direction(ptile, dest_tile);
    struct tile *new_tile;

    if (!(new_tile = mapstep(ptile, dir))
	|| !airspace_looks_safe(new_tile, pplayer)) {
      break;
    }
    ptile = new_tile;
  }
  if (dist == 1) {
    /* Looks like the O(n) quicksearch worked. */
    assert(real_map_distance(ptile, dest_tile) == 1);
    return moves - total_distance * MOVE_COST_AIR;
  }

  /* 
   * Finally, we do a full A* search if this isn't one of the specical
   * cases from above. This is copied from find_the_shortest_path but
   * we use real_map_distance as a minimum distance estimator for the
   * A* search. This distance estimator is used for the cost value in
   * the queue, but is not stored in the warmap itself.
   *
   * Note, A* is possible here but not in a normal FreeCiv path
   * finding because planes always take 1 movement unit to move -
   * which is not true of land units.
   */
  freelog(LOG_DEBUG,
	  "air_can_move_between: quick search didn't work. Lets try full.");

  init_warmap(src_tile, AIR_MOVING);

  /* The 0 is inaccurate under A*, but it doesn't matter. */
  add_to_mapqueue(0, src_tile);

  while ((ptile = get_from_mapqueue())) {
    adjc_dir_iterate(ptile, tile1, dir) {
      if (WARMAP_COST(tile1) != MAXCOST) {
	continue;
      }

      /*
       * This comes before the airspace_looks_safe check because it's
       * okay to goto into an enemy. 
       */
      if (same_pos(tile1, dest_tile)) {
	/* We're there! */
	freelog(LOG_DEBUG, "air_can_move_between: movecost: %i",
		WARMAP_COST(ptile) + MOVE_COST_AIR);
	/* The -MOVE_COST_AIR is because we haven't taken the final
	   step yet. */
	return moves - WARMAP_COST(ptile) - MOVE_COST_AIR;
      }

      /* We refuse to goto through unsafe airspace. */
      if (airspace_looks_safe(tile1, pplayer)) {
	int cost = WARMAP_COST(ptile) + MOVE_COST_AIR;

	WARMAP_COST(tile1) = cost;

	/* Now for A* we find the minimum total cost. */
	cost += real_map_distance(tile1, dest_tile);
	if (cost <= moves) {
	  add_to_mapqueue(cost, tile1);
	}
      }
    } adjc_dir_iterate_end;
  }

  freelog(LOG_DEBUG, "air_can_move_between: no route found");
  return -1;
}
