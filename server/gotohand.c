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
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "combat.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "rand.h"

#include "maphand.h"
#include "settlers.h"
#include "unithand.h"
#include "unittools.h"

#include "gotohand.h"

struct move_cost_map warmap;

struct stack_element {
  unsigned char x, y;
};

/* These are used for airplane GOTOs with waypoints */
#define MAXFUEL 100

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

enum refuel_type {
  FUEL_START = 0, FUEL_GOAL, FUEL_CITY, FUEL_AIRBASE
};

struct refuel {
  enum refuel_type type;
  unsigned int x, y;
  int turns;
  int moves_left;
  struct refuel *coming_from;
};

static struct refuel *refuels[MAP_MAX_WIDTH*MAP_MAX_HEIGHT]; /* enough :P */
static unsigned int refuellist_size;

static void make_list_of_refuel_points(struct player *pplayer);
static void dealloc_refuel_stack(void);
static int find_air_first_destination(struct unit *punit, int *dest_x, int *dest_y);
static int airspace_looks_safe(int x, int y, struct player *pplayer);

/* These are used for all GOTO's */

/* A byte must be able to hold this value i.e. is must be less than
   256. */
#define MAXCOST 255
#define MAXARRAYS 10000
#define ARRAYLENGTH 10

struct mappos_array {
  int first_pos;
  int last_pos;
  struct map_position pos[ARRAYLENGTH];
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

/**************************************************************************
...
**************************************************************************/
static void init_queue(void)
{
  int i;
  static int is_initialized = FALSE;
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
  if (mappos_arrays[array_count] == NULL)
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
static void add_to_mapqueue(int cost, int x, int y)
{
  struct mappos_array *our_array;

  assert(cost < MAXCOST && cost >= 0);

  our_array = cost_lookup[cost].last_array;
  if (our_array == NULL) {
    our_array = get_empty_array();
    cost_lookup[cost].first_array = our_array;
    cost_lookup[cost].last_array = our_array;
  } else if (our_array->last_pos == ARRAYLENGTH-1) {
    our_array->next_array = get_empty_array();
    our_array = our_array->next_array;
    cost_lookup[cost].last_array = our_array;
  }

  our_array->pos[++(our_array->last_pos)].x = x;
  our_array->pos[our_array->last_pos].y = y;
  if (cost > highest_cost)
    highest_cost = cost;
  freelog(LOG_DEBUG, "adding cost:%i at %i,%i", cost, x, y);
}

/**************************************************************************
...
**************************************************************************/
static int get_from_mapqueue(int *x, int *y)
{
  struct mappos_array *our_array;
  freelog(LOG_DEBUG, "trying get");
  while (lowest_cost < MAXCOST) {
    if (lowest_cost > highest_cost)
      return FALSE;
    our_array = cost_lookup[lowest_cost].first_array;
    if (our_array == NULL) {
      lowest_cost++;
      continue;
    }
    if (our_array->last_pos < our_array->first_pos) {
      if (our_array->next_array != NULL) {
	cost_lookup[lowest_cost].first_array = our_array->next_array;
	continue; /* note NOT "lowest_cost++;" */
      } else {
	cost_lookup[lowest_cost].first_array = NULL;
	lowest_cost++;
	continue;
      }
    }
    *x = our_array->pos[our_array->first_pos].x;
    *y = our_array->pos[our_array->first_pos].y;
    our_array->first_pos++;
    return TRUE;
  }
  return FALSE;
}

/**************************************************************************
Reset the movecosts of the warmap.
**************************************************************************/
static void init_warmap(int orig_x, int orig_y, enum unit_move_type move_type)
{
  int x;

  if (warmap.cost[0] == NULL) {
    for (x = 0; x < map.xsize; x++) {
      warmap.cost[x]=fc_malloc(map.ysize*sizeof(unsigned char));
      warmap.seacost[x]=fc_malloc(map.ysize*sizeof(unsigned char));
      warmap.vector[x]=fc_malloc(map.ysize*sizeof(unsigned char));
    }
  }

  init_queue();

  switch (move_type) {
  case LAND_MOVING:
  case HELI_MOVING:
  case AIR_MOVING:
    for (x = 0; x < map.xsize; x++)
      memset(warmap.cost[x], MAXCOST, map.ysize*sizeof(unsigned char));
    warmap.cost[orig_x][orig_y] = 0;
    break;
  case SEA_MOVING:
    for (x = 0; x < map.xsize; x++)
      memset(warmap.seacost[x], MAXCOST, map.ysize*sizeof(unsigned char));
    warmap.seacost[orig_x][orig_y] = 0;
    break;
  default:
    freelog(LOG_ERROR, "Bad move_type in init_warmap().");
  }
}  

/*******************************************************************/
static void init_refuel(
  struct refuel *pRefuel,
  int x, int y,
  enum refuel_type type,
  int turns, int moves_left)
{
  pRefuel->x = x;
  pRefuel->y = y;
  pRefuel->type = type;
  pRefuel->turns = turns;
  pRefuel->moves_left = moves_left;
  pRefuel->coming_from = NULL;
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
  int x, y, move_cost;
  int orig_x, orig_y;
  int igter;
  int maxcost = THRESHOLD * 6 + 2; /* should be big enough without being TOO big */
  struct tile *ptile;
  struct player *pplayer;

  if (pcity != NULL) {
    orig_x = pcity->x;
    orig_y = pcity->y;
    pplayer = city_owner(pcity);
  } else {
    orig_x = punit->x;
    orig_y = punit->y;
    pplayer = unit_owner(punit);
  }

  init_warmap(orig_x, orig_y, move_type);
  add_to_mapqueue(0, orig_x, orig_y);

  if (punit != NULL && unit_flag(punit, F_IGTER))
    igter = TRUE;
  else
    igter = FALSE;

  /* FIXME: Should this apply only to F_CITIES units? -- jjm */
  if (punit != NULL
      && unit_flag(punit, F_SETTLERS)
      && unit_type(punit)->move_rate==3)
    maxcost /= 2;
  /* (?) was punit->type == U_SETTLERS -- dwp */

  while (get_from_mapqueue(&x, &y)) {
    ptile = map_get_tile(x, y);
    adjc_dir_iterate(x, y, x1, y1, dir) {
      switch (move_type) {
      case LAND_MOVING:
	if (warmap.cost[x1][y1] <= warmap.cost[x][y])
	  continue; /* No need for all the calculations */

        if (map_get_terrain(x1, y1) == T_OCEAN) {
          if (punit != NULL && ground_unit_transporter_capacity(x1, y1, pplayer) > 0)
	    move_cost = SINGLE_MOVE;
          else
	    continue;
	} else if (ptile->terrain == T_OCEAN) {
	  int base_cost = get_tile_type(map_get_terrain(x1, y1))->movement_cost * SINGLE_MOVE;
	  move_cost = igter ? MOVE_COST_ROAD : MIN(base_cost, unit_type(punit)->move_rate);
        } else if (igter)
	  /* NOT c = 1 (Syela) [why not? - Thue] */
	  move_cost = (ptile->move_cost[dir] ? SINGLE_MOVE : 0);
        else if (punit != NULL)
	  move_cost = MIN(ptile->move_cost[dir], unit_type(punit)->move_rate);
	/* else c = ptile->move_cost[k]; 
	   This led to a bad bug where a unit in a swamp was considered too far away */
        else { /* we have a city */
	  int tmp = map_get_tile(x1, y1)->move_cost[DIR_REVERSE(dir)];
          move_cost = (ptile->move_cost[dir] + tmp +
		       (ptile->move_cost[dir] > tmp ? 1 : 0))/2;
        }

        move_cost += warmap.cost[x][y];
        if (warmap.cost[x1][y1] > move_cost && move_cost < maxcost) {
          warmap.cost[x1][y1] = move_cost;
          add_to_mapqueue(move_cost, x1, y1);
        }
	break;


      case SEA_MOVING:
        move_cost = SINGLE_MOVE;
        move_cost += warmap.seacost[x][y];
        if (warmap.seacost[x1][y1] > move_cost && move_cost < maxcost) {
	  /* by adding the move_cost to the warmap regardless if we
	     can move between we allow for shore bombardment/transport
	     to inland positions/etc. */
          warmap.seacost[x1][y1] = move_cost;
	  if (ptile->move_cost[dir] == MOVE_COST_FOR_VALID_SEA_STEP) {
	    add_to_mapqueue(move_cost, x1, y1);
	  }
	}
	break;
      default:
	move_cost = 0; /* silence compiler warning */
	freelog(LOG_FATAL, "Bad/unimplemented move_type in really_generate_warmap().");
	abort();
      }
    } adjc_dir_iterate_end;
  }

  freelog(LOG_DEBUG, "Generated warmap for (%d,%d).",
	  orig_x, orig_y); 
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
	  (pcity != NULL ? pcity->name : "NULL"),
	  (punit != NULL ? unit_type(punit)->name : "NULL"));

  if (punit != NULL) {
    /* 
     * Checking for an existing warmap. If the previous warmap was for
     * a city and our unit is in that city, use city's warmap.
     */
    if (pcity != NULL && pcity == warmap.warcity) {
      return;
    }

    /* 
     * If the previous warmap was for the same unit and it's still
     * correct (warmap.(sea)cost[x][y] == 0), reuse it.
     */
    if (warmap.warunit == punit &&
	(is_sailing_unit(punit) ? (warmap.seacost[punit->x][punit->y] == 0)
	 : (warmap.cost[punit->x][punit->y] == 0))) {
      return;
    }

    pcity = NULL;
  }

  warmap.warcity = pcity;
  warmap.warunit = punit;

  if (punit != NULL) {
    if (is_sailing_unit(punit)) {
      really_generate_warmap(pcity, punit, SEA_MOVING);
    } else {
      really_generate_warmap(pcity, punit, LAND_MOVING);
    }
    warmap.orig_x = punit->x;
    warmap.orig_y = punit->y;
  } else {
    really_generate_warmap(pcity, punit, LAND_MOVING);
    really_generate_warmap(pcity, punit, SEA_MOVING);
    warmap.orig_x = pcity->x;
    warmap.orig_y = pcity->y;
  }
}

/**************************************************************************
Return the direction that takes us most directly to dest_x,dest_y.
The direction is a value for use in DIR_DX[] and DIR_DY[] arrays.

FIXME: This is a bit crude; this only gives one correct path, but sometimes
       there can be more than one straight path (fx going NW then W is the
       same as going W then NW)
**************************************************************************/
static int straightest_direction(int src_x, int src_y, int dest_x, int dest_y)
{
  int best_dir;
  int diff_x, diff_y;

  /* Should we go up or down, east or west: diff_x/y is the "step" in x/y. */
  map_distance_vector(&diff_x, &diff_y, src_x, src_y, dest_x, dest_y);

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
static int goto_zoc_ok(struct unit *punit, int src_x, int src_y,
		       int dest_x, int dest_y, unsigned char came_from)
{
  if (can_step_taken_wrt_to_zoc
      (punit->type, unit_owner(punit), src_x, src_y, dest_x, dest_y))
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
  if (src_x == punit->x && src_y == punit->y
      && is_non_allied_unit_tile(map_get_tile(dest_x, dest_y),
				  unit_owner(punit)) == NULL)
    return FALSE;

  {
    struct player *owner = unit_owner(punit);

    adjc_dir_iterate(src_x, src_y, x, y, dir) {
      /* if we didn't come from there */
      if ((1 << dir & came_from) == 0 && (map_get_terrain(x, y) != T_OCEAN)
	  /* and there is an enemy there */
	  && is_enemy_unit_tile(map_get_tile(x, y), owner) != NULL) {
	/* then it counts in the zoc claculation */
	return FALSE;
      }
    } adjc_dir_iterate_end;
    return TRUE;
  }
}

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
static int find_the_shortest_path(struct unit *punit,
				  int dest_x, int dest_y,
				  enum goto_move_restriction restriction)
{
  struct player *pplayer = unit_owner(punit);
  int igter, x, y;
  int orig_x, orig_y;
  struct tile *psrctile, *pdesttile;
  enum unit_move_type move_type = unit_type(punit)->move_type;
  int maxcost = MAXCOST;
  int move_cost, total_cost;
  int straight_dir = 0;	/* init to silence compiler warning */
  static unsigned char local_vector[MAP_MAX_WIDTH][MAP_MAX_HEIGHT];
  struct unit *pcargo;
  /* 
   * Land/air units use warmap.cost while sea units use
   * warmap.seacost.  Don't ask me why.  --JDS 
   */
  unsigned char **warmap_cost =
      (move_type == SEA_MOVING) ? warmap.seacost : warmap.cost;

  orig_x = punit->x;
  orig_y = punit->y;

  if ((dest_x == orig_x) && (dest_y == orig_y)) return TRUE; /* [Kero] */
  
  local_vector[orig_x][orig_y] = 0;

  init_warmap(punit->x, punit->y, move_type);
  add_to_mapqueue(0, orig_x, orig_y);

  if (punit != NULL && unit_flag(punit, F_IGTER))
    igter = TRUE;
  else
    igter = FALSE;

  /* If we have a passenger abord a ship we must be sure he can disembark
     This shouldn't be neccesary, as ZOC does not have an effect for sea-land
     movement, but some code in aiunit.c assumes the goto work like this, so
     I will leave it for now */
  if (move_type == SEA_MOVING) {
    pcargo = other_passengers(punit);
    if (pcargo != NULL)
      if (map_get_terrain(dest_x, dest_y) == T_OCEAN ||
	  is_non_allied_unit_tile(map_get_tile(dest_x, dest_y),
				   unit_owner(pcargo)) == NULL
	  || is_allied_city_tile(map_get_tile(dest_x, dest_y),
				 unit_owner(pcargo)) != NULL
	  || unit_flag(pcargo, F_MARINES)
	  || is_my_zoc(unit_owner(pcargo), dest_x, dest_y))
	pcargo = NULL;
  } else
    pcargo = NULL;

  /* until we have found the shortest paths */
  while (get_from_mapqueue(&x, &y)) {
    psrctile = map_get_tile(x, y);

    if (restriction == GOTO_MOVE_STRAIGHTEST)
      straight_dir = straightest_direction(x, y, dest_x, dest_y);

    /* Try to move to all tiles adjacent to x,y. The coordinats of the tile we
       try to move to are x1,y1 */
    adjc_dir_iterate(x, y, x1, y1, dir) {
      if ((restriction == GOTO_MOVE_CARDINAL_ONLY)
	  && !DIR_IS_CARDINAL(dir)) continue;

      pdesttile = map_get_tile(x1, y1);

      switch (move_type) {
      case LAND_MOVING:
	if (warmap.cost[x1][y1] <= warmap.cost[x][y])
	  continue; /* No need for all the calculations. Note that this also excludes
		       RR loops, ie you can't create a cycle with the same move_cost */

	if (pdesttile->terrain == T_OCEAN) {
	  if (ground_unit_transporter_capacity(x1, y1, unit_owner(punit)) <= 0)
	    continue;
	  else
	    move_cost = 3;
	} else if (psrctile->terrain == T_OCEAN) {
	  int base_cost = get_tile_type(pdesttile->terrain)->movement_cost * SINGLE_MOVE;
	  move_cost = igter ? 1 : MIN(base_cost, unit_type(punit)->move_rate);
	} else if (igter)
	  move_cost = (psrctile->move_cost[dir] ? SINGLE_MOVE : 0);
	else
	  move_cost = MIN(psrctile->move_cost[dir], unit_type(punit)->move_rate);

	if (!pplayer->ai.control && !map_get_known(x1, y1, pplayer)) {
	  /* Don't go into the unknown. 5*SINGLE_MOVE is an arbitrary deterrent. */
	  move_cost = (restriction == GOTO_MOVE_STRAIGHTEST) ? SINGLE_MOVE : 5*SINGLE_MOVE;
	} else if (is_non_allied_unit_tile(pdesttile, unit_owner(punit)) != NULL) {
	  if (psrctile->terrain == T_OCEAN && !unit_flag(punit, F_MARINES)) {
	    continue; /* Attempting to attack from a ship */
	  }

	  if (x1 != dest_x || y1 != dest_y) { /* Allow players to target anything */
	    if (pplayer->ai.control) {
	      if ((is_enemy_unit_tile(pdesttile, unit_owner(punit)) == NULL
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
	} else if (is_non_allied_city_tile(pdesttile, unit_owner(punit)) != NULL) {
	  if (psrctile->terrain == T_OCEAN && !unit_flag(punit, F_MARINES)) {
	    continue; /* Attempting to attack from a ship */
	  }

	  if ((is_non_attack_city_tile(pdesttile, unit_owner(punit)) != NULL
	       || !is_military_unit(punit))) {
	    if (!is_diplomat_unit(punit)
		&& (x1 != dest_x || y1 != dest_y)) /* Allow players to target anything */
	      continue;
	    else
	      move_cost = SINGLE_MOVE;
	  }
	} else if (!goto_zoc_ok(punit, x, y, x1, y1, local_vector[x][y])) {
	  continue;
	}

	if (restriction == GOTO_MOVE_STRAIGHTEST && dir == straight_dir)
	  move_cost /= SINGLE_MOVE;

	total_cost = move_cost + warmap.cost[x][y];
	break;

      case SEA_MOVING:
	if (warmap.seacost[x1][y1] <= warmap.seacost[x][y])
	  continue; /* No need for all the calculations */

	/* allow ships to target a shore */
	if (psrctile->move_cost[dir] != MOVE_COST_FOR_VALID_SEA_STEP
	    && (dest_x != x1 || dest_y != y1)) {
	  continue;
	}
	else if (unit_flag(punit, F_TRIREME)
		 && trireme_loss_pct(unit_owner(punit), x1, y1) > 0) {
	  move_cost = 2*SINGLE_MOVE+1;
	} else {
	  move_cost = SINGLE_MOVE;
	}

	/* See previous comment on pcargo */
	if (x1 == dest_x && y1 == dest_y && pcargo != NULL
	    && move_cost < 20 * SINGLE_MOVE
	    && !is_my_zoc(unit_owner(pcargo), x, y)) {
	  move_cost = 20 * SINGLE_MOVE;
	}

	if (!pplayer->ai.control && !map_get_known(x1, y1, pplayer))
	  move_cost = (restriction == GOTO_MOVE_STRAIGHTEST) ? SINGLE_MOVE : 5*SINGLE_MOVE; /* arbitrary deterrent. */

	/* We don't allow attacks during GOTOs here; you can almost
	   always find a way around enemy units on sea */
	if (x1 != dest_x || y1 != dest_y) {
	  if (is_non_allied_unit_tile(pdesttile, unit_owner(punit)) != NULL
	      || is_non_allied_city_tile(pdesttile, unit_owner(punit)) != NULL)
	    continue;
	}

	if ((restriction == GOTO_MOVE_STRAIGHTEST) && (dir == straight_dir))
	  move_cost /= SINGLE_MOVE;

	total_cost = move_cost + warmap.seacost[x][y];

	/* For the ai, maybe avoid going close to enemies. */
	if (pplayer->ai.control &&
	    warmap.seacost[x][y] < punit->moves_left
	    && total_cost < maxcost
	    && total_cost >= punit->moves_left - (get_transporter_capacity(punit) >
			      unit_type(punit)->attack_strength ? 3 : 2)
	    && enemies_at(punit, x1, y1)) {
	  total_cost += unit_type(punit)->move_rate;
	  freelog(LOG_DEBUG, "%s#%d@(%d,%d) dissuaded from (%d,%d) -> (%d,%d)",
		  unit_type(punit)->name, punit->id,
		  punit->x, punit->y, x1, y1,
		  punit->goto_dest_x, punit->goto_dest_y);
	}
	break;

      case AIR_MOVING:
      case HELI_MOVING:
	if (warmap.cost[x1][y1] <= warmap.cost[x][y])
	  continue; /* No need for all the calculations */

	move_cost = SINGLE_MOVE;
	/* Planes could run out of fuel, therefore we don't care if territory
	   is unknown. Also, don't attack except at the destination. */

	/* This should probably use airspace_looks_safe instead. */
	if (is_non_allied_unit_tile(pdesttile, unit_owner(punit)) != NULL) {
	  if (x1 != dest_x || y1 != dest_y) {
	    continue;
	  }
	} else {
	  struct city *pcity =
	      is_non_allied_city_tile(pdesttile, unit_owner(punit));
	  if (pcity != NULL
	      && (pplayers_non_attack(unit_owner(punit), city_owner(pcity))
		  || !is_heli_unit(punit)))
	    continue;
	}

	if ((restriction == GOTO_MOVE_STRAIGHTEST) && (dir == straight_dir))
	  move_cost /= SINGLE_MOVE;
	total_cost = move_cost + warmap.cost[x][y];
	break;

      default:
	move_cost = MAXCOST;	/* silence compiler warning */
	freelog(LOG_FATAL, "Bad move_type in find_the_shortest_path().");
	abort();
      } /****** end switch ******/

      /* Add the route to our warmap if it is worth keeping */
      if (total_cost < maxcost) {
	if (warmap_cost[x1][y1] > total_cost) {
	  warmap_cost[x1][y1] = total_cost;
	  add_to_mapqueue(total_cost, x1, y1);
	  local_vector[x1][y1] = 1 << DIR_REVERSE(dir);
	  freelog(LOG_DEBUG,
		  "Candidate: %s from (%d, %d) to (%d, %d), cost %d",
		  dir_get_name(dir), x, y, x1, y1, total_cost);
	} else if (warmap_cost[x1][y1] == total_cost) {
	  local_vector[x1][y1] |= 1 << DIR_REVERSE(dir);
	  freelog(LOG_DEBUG,
		  "Co-Candidate: %s from (%d, %d) to (%d, %d), cost %d",
		  dir_get_name(dir), x, y, x1, y1, total_cost);
	}
      }

      if (x1 == dest_x && y1 == dest_y && maxcost > total_cost) {
	freelog(LOG_DEBUG, "Found path, cost = %d", total_cost);
	/* Make sure we stop searching when we have no hope of finding a shorter path */
        maxcost = total_cost + 1;
      }
    } adjc_dir_iterate_end;
  }

  freelog(LOG_DEBUG, "GOTO: (%d, %d) -> (%d, %d), cost = %d", 
	  orig_x, orig_y, dest_x, dest_y, maxcost - 1);

  if (maxcost == MAXCOST)
    return FALSE; /* No route */

  /*** Succeeded. The vector at the destination indicates which way we get there.
     Now backtrack to remove all the blind paths ***/
  for (x = 0; x < map.xsize; x++)
    memset(warmap.vector[x], 0, map.ysize*sizeof(unsigned char));

  init_queue();
  add_to_mapqueue(0, dest_x, dest_y);

  while (TRUE) {
    if (!get_from_mapqueue(&x, &y))
      break;

    adjc_dir_iterate(x, y, x1, y1, dir) {
      if ((restriction == GOTO_MOVE_CARDINAL_ONLY)
	  && !DIR_IS_CARDINAL(dir)) continue;

      if (local_vector[x][y] & (1<<dir)) {
	move_cost = (move_type == SEA_MOVING) ? warmap.seacost[x1][y1] : warmap.cost[x1][y1];

        add_to_mapqueue(MAXCOST-1 - move_cost, x1, y1);
	/* Mark it on the warmap */
	warmap.vector[x1][y1] |= 1 << DIR_REVERSE(dir);	
        local_vector[x][y] -= 1<<dir; /* avoid repetition */
	freelog(LOG_DEBUG, "PATH-SEGMENT: %s from (%d, %d) to (%d, %d)",
		dir_get_name(DIR_REVERSE(dir)), x1, y1, x, y);
      }
    } adjc_dir_iterate_end;
  }

  return TRUE;
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
			    const int dest_x, const int dest_y)
{
#define UNIT_DEFENSE(punit, x, y, defence_multiplier) \
  ((get_simple_defense_power((punit)->type, (x), (y)) * \
    (defence_multiplier)) / 2)

#define UNIT_RATING(punit, x, y, defence_multiplier) \
  (UNIT_DEFENSE(punit, x, y, defence_multiplier) * (punit)->hp)

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

  int fitness[8], best_fitness = DONT_SELECT_ME_FITNESS;
  struct unit *passenger;
  struct player *pplayer = unit_owner(punit);
  int afraid_of_sinking = (unit_flag(punit, F_TRIREME) && 
                           !player_owns_active_wonder(pplayer, B_LIGHTHOUSE));
  /* 
   * If the destination is one step away, look around first or just go
   * there?
   */ 
  int do_full_check = afraid_of_sinking; 

  if (map_get_terrain(punit->x, punit->y) == T_OCEAN) {
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
    int dir;

    if (base_get_direction_for_step(punit->x, punit->y, dest_x, dest_y, &dir)
	&& warmap.vector[punit->x][punit->y] & (1 << dir)
	&& !(restriction == GOTO_MOVE_CARDINAL_ONLY
	     && !DIR_IS_CARDINAL(dir))) {
      return dir;
    }
  }

  memset(fitness, 0, sizeof(fitness));

  /*
   * Loop over all directions, fill the fitness array and update
   * best_fitness.
   */
  adjc_dir_iterate(punit->x, punit->y, x, y, dir) {
    int defence_multiplier, num_of_allied_units, best_friendly_defence,
	base_move_cost;
    struct tile *ptile = map_get_tile(x, y);
    struct city *pcity = map_get_city(x, y);
    struct unit *best_ally;

    /* 
     * Is it an allowed direction?  is it marked on the warmap?
     */
    if (!(warmap.vector[punit->x][punit->y] & (1 << dir))
	|| ((restriction == GOTO_MOVE_CARDINAL_ONLY)
	    && !DIR_IS_CARDINAL(dir))) {
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
      base_move_cost = map_get_tile(punit->x, punit->y)->move_cost[dir];
    } else {
      base_move_cost = SINGLE_MOVE;
    }

    if (unit_flag(punit, F_IGTER) && base_move_cost >= MOVE_COST_ROAD) {
      base_move_cost = MOVE_COST_ROAD;
    }

    freelog(LOG_DEBUG, "%d@(%d,%d) evaluating (%d,%d)[%d/%d]", punit->id, 
            punit->x, punit->y, x, y, base_move_cost, punit->moves_left);

    /* 
     * Calculate the defence multiplier of this tile. Both the unit
     * itself and its ally benefit from it.
     */
    defence_multiplier = 2;
    if (pcity != NULL) {
      if (city_got_citywalls(pcity)) {
	defence_multiplier += 2;
      }
      if (city_got_building(pcity, B_SDI)) {
	defence_multiplier++;
      }
      if (city_got_building(pcity, B_SAM)) {
	defence_multiplier++;
      }
      if (city_got_building(pcity, B_COASTAL)) {
	defence_multiplier++;
      }
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
	      UNIT_RATING(aunit, x, y, defence_multiplier);
	  num_of_allied_units++;

	  if (best_ally == NULL || rating_of_current_ally > rating_of_best_ally) {
	    best_ally = aunit;
	    rating_of_best_ally = rating_of_current_ally;
	  }
	}
      } unit_list_iterate_end;
    }

    if (best_ally != NULL) {
      best_friendly_defence =
	  MAX(UNIT_DEFENSE(punit, x, y, defence_multiplier),
	      UNIT_DEFENSE(best_ally, x, y, defence_multiplier));
    } else {
      best_friendly_defence =
	  UNIT_DEFENSE(punit, x, y, defence_multiplier);
    }

    /* 
     * Fill fitness[dir] based on the rating of punit and best_ally.
     */
    {
      /* calculate some clever weights basing on defence stats */
      int rating_of_ally, rating_of_unit =
	  UNIT_RATING(punit, x, y, defence_multiplier);
      
      assert((best_ally != NULL) == (num_of_allied_units > 0));

      if (best_ally != NULL) {
	rating_of_ally = UNIT_RATING(best_ally, x, y, defence_multiplier);
      } else {
	rating_of_ally = 0;	/* equivalent of having 0 HPs */
      }

      if (num_of_allied_units == 0) {
	fitness[dir] = rating_of_unit;
      } else if (pcity != NULL || ptile->special & S_FORTRESS) {
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
    if ((ptile->special & S_ROAD) || (ptile->special & S_RAILROAD)) {
      fitness[dir] += 10;
    }

    /* 
     * What is around the tile we are about to step to?
     */
    adjc_iterate(x, y, tmp_x, tmp_y) {
      struct tile *adjtile = map_get_tile(tmp_x, tmp_y);

      if (!map_get_known(tmp_x, tmp_y, pplayer)) {
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
	    if (!attack_of_enemy) {
	      continue;
	    }
	    
	    if (passenger != NULL && !is_ground_unit(aunit)) {
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
    if (afraid_of_sinking && !is_coast_seen(x, y, pplayer)) {
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
                     || !punit->ai.passenger 
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
      freelog(LOG_DEBUG, "New best = %d: (%d, %d) -> (%d, %d)",
	      best_fitness, punit->x, punit->y, x, y);
    }
  } adjc_dir_iterate_end;

  if (best_fitness == DONT_SELECT_ME_FITNESS && afraid_of_sinking
      && !is_coastline(punit->x, punit->y)) {
    /* 
     * We've got a trireme in the middle of the sea. With
     * best_fitness==DONT_SELECT_ME_FITNESS, it'll end its turn right
     * there and could very well die. We'll try to rescue.
     */
    freelog(LOG_DEBUG,
	    "%s's trireme in trouble at (%d,%d), looking for coast",
	    pplayer->name, punit->x, punit->y);

    adjc_dir_iterate(punit->x, punit->y, x, y, dir) {
      if (is_coast_seen(x, y, pplayer)) {
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
      return dir;
    }
  }

#undef UNIT_DEFENSE
#undef UNIT_RATING
#undef UNKNOWN_FITNESS_PENALTY_PERCENT
#undef DONT_SELECT_ME_FITNESS
}

/**************************************************************************
Basic checks as to whether a GOTO is possible.
**************************************************************************/
int goto_is_sane(struct unit *punit, int x, int y, int omni)
{  
  struct player *pplayer = unit_owner(punit);
  int possible = 0;
  if (same_pos(punit->x, punit->y, x, y)) return TRUE;
  if (is_ground_unit(punit) &&
      (omni || map_get_known_and_seen(x, y, pplayer))) {
    if (map_get_terrain(x, y) == T_OCEAN) {
      if (ground_unit_transporter_capacity(x, y, pplayer) > 0) {
	adjc_iterate(punit->x, punit->y, tmp_x, tmp_y) {
	  if (map_get_continent(tmp_x, tmp_y) == map_get_continent(x, y))
	    possible++;
	} adjc_iterate_end;
      }
    } else { /* going to a land tile */
      if (map_get_continent(punit->x, punit->y) ==
            map_get_continent(x, y))
         possible++;
      else {
	adjc_iterate(punit->x, punit->y, tmp_x, tmp_y) {
	  if (map_get_continent(tmp_x, tmp_y) == map_get_continent(x, y))
	    possible++;
	} adjc_iterate_end;
      }
    }
    return possible > 0;
  } else if (is_sailing_unit(punit) &&
	     (omni || map_get_known_and_seen(x, y, pplayer)) &&
	     map_get_terrain(x, y) != T_OCEAN && map_get_city(x, y) == NULL &&
	     !is_terrain_near_tile(x, y, T_OCEAN)) {
    return FALSE;
  }

  return TRUE;
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
			      int trigger_special_ability)
{
  struct player *pplayer = unit_owner(punit);
  int x, y, dir;
  int unit_id, dest_x, dest_y, waypoint_x, waypoint_y;
  struct unit *penemy = NULL;
  enum goto_result status;

  if (punit->pgr != NULL) {
    /* we have a precalculated goto route */
    return goto_route_execute(punit);
  }

  unit_id = punit->id;
  dest_x = waypoint_x = punit->goto_dest_x;
  dest_y = waypoint_y = punit->goto_dest_y;

  if (same_pos(punit->x, punit->y, dest_x, dest_y) ||
      !goto_is_sane(punit, dest_x, dest_y, FALSE)) {
    punit->activity = ACTIVITY_IDLE;
    punit->connecting = FALSE;
    send_unit_info(NULL, punit);
    if (same_pos(punit->x, punit->y, dest_x, dest_y)) {
      return GR_ARRIVED;
    } else {
      return GR_FAILED;
    }
  }

  if(!punit->moves_left) {
    send_unit_info(NULL, punit);
    return GR_OUT_OF_MOVEPOINTS;
  }

  if (is_air_unit(punit)) {
    if (find_air_first_destination(punit, &waypoint_x, &waypoint_y)) {
      /* this is a special case for air units who do not always want to move. */
      if (same_pos(waypoint_x, waypoint_y, punit->x, punit->y)) {
	advance_unit_focus(punit);
	return GR_OUT_OF_MOVEPOINTS; /* out of fuel */
      }
    } else {
      freelog(LOG_VERBOSE, "Did not find an airroute for "
	      "%s's %s at (%d, %d) -> (%d, %d)",
	      pplayer->name, unit_type(punit)->name,
	      punit->x, punit->y, dest_x, dest_y);
      punit->activity = ACTIVITY_IDLE;
      punit->connecting = FALSE;
      send_unit_info(NULL, punit);
      return GR_FAILED;
    }
  }

  /* This has the side effect of marking the warmap with the possible paths */
  if (find_the_shortest_path(punit, waypoint_x, waypoint_y, restriction)) {
    do { /* move the unit along the path chosen by find_the_shortest_path() while we can */
      int last_tile, is_real;

      if (!punit->moves_left) {
	return GR_OUT_OF_MOVEPOINTS;
      }

      dir = find_a_direction(punit, restriction, waypoint_x, waypoint_y);
      if (dir < 0) {
	freelog(LOG_DEBUG, "%s#%d@(%d,%d) stalling so it won't be killed.",
		unit_type(punit)->name, punit->id,
		punit->x, punit->y);
	return GR_FAILED;
      }

      freelog(LOG_DEBUG, "Going %s", dir_get_name(dir));
      is_real = MAPSTEP(x, y, punit->x, punit->y, dir);
      assert(is_real);

      penemy = is_enemy_unit_tile(map_get_tile(x, y), unit_owner(punit));
      assert(punit->moves_left);
      last_tile = same_pos(x, y, punit->goto_dest_x, punit->goto_dest_y);

      if (!handle_unit_move_request(punit, x, y, FALSE,
				    !(last_tile && trigger_special_ability))) {
	freelog(LOG_DEBUG, "Couldn't handle it.");
	if (punit->moves_left) {
	  punit->activity=ACTIVITY_IDLE;
	  send_unit_info(NULL, punit);
	  return GR_FAILED;
	}
      } else {
	freelog(LOG_DEBUG, "Handled.");
      }
      if (player_find_unit_by_id(pplayer, unit_id) == NULL) {
	return GR_DIED;		/* unit died during goto! */
      }

      /* Don't attack more than once per goto */
      if (penemy != NULL && !pplayer->ai.control) { /* Should I cancel for ai's too? */
 	punit->activity = ACTIVITY_IDLE;
 	send_unit_info(NULL, punit);
 	return GR_FOUGHT;
      }

      if(punit->x!=x || punit->y!=y) {
	freelog(LOG_DEBUG, "Aborting, out of movepoints.");
	send_unit_info(NULL, punit);
	return GR_OUT_OF_MOVEPOINTS;
      }

      /* single step connecting unit when it can do it's activity */
      if (punit->connecting
	  && can_unit_do_activity(punit, punit->activity)) {
	/* for connecting unit every step is a destination */
	return GR_ARRIVED;
      }

      freelog(LOG_DEBUG, "Moving on.");
    } while(!(x==waypoint_x && y==waypoint_y));
  } else {
    freelog(LOG_VERBOSE, "Did not find the shortest path for "
	    "%s's %s at (%d, %d) -> (%d, %d)",
	    pplayer->name, unit_type(punit)->name,
	    punit->x, punit->y, dest_x, dest_y);
    handle_unit_activity_request(punit, ACTIVITY_IDLE);
    punit->connecting = FALSE;
    send_unit_info(NULL, punit);
    return GR_FAILED;
  }
  /** Finished moving the unit for this turn **/

  /* ensure that the connecting unit will perform it's activity
     on the destination file too. */
  if (punit->connecting && can_unit_do_activity(punit, punit->activity))
    return GR_ARRIVED;

  /* normally we would just do this unconditionally, but if we had an
     airplane goto we might not be finished even if the loop exited */
  if (punit->x == dest_x && punit->y == dest_y) {
    if (punit->activity != ACTIVITY_PATROL) {
      punit->activity = ACTIVITY_IDLE;
    }
    status = GR_ARRIVED;
  } else {
    advance_unit_focus(punit);
    /* we have a plane refueling at a waypoint */
    status = GR_OUT_OF_MOVEPOINTS;
  }

  punit->connecting = FALSE;
  send_unit_info(NULL, punit);
  return status;
}

/**************************************************************************
Calculate and return cost (in terms of move points) for unit to move
to specified destination.
Currently only used in autoattack.c
**************************************************************************/
int calculate_move_cost(struct unit *punit, int dest_x, int dest_y)
{
  /* perhaps we should do some caching -- fisch */

  if (is_air_unit(punit) || is_heli_unit(punit)) {
    /* The warmap only really knows about land and sea
       units, so for these we just assume cost = distance.
       (times 3 for road factor).
       (Could be wrong if there are things in the way.)
    */
    return SINGLE_MOVE * real_map_distance(punit->x, punit->y, dest_x, dest_y);
  }

  generate_warmap(NULL, punit);

  if (is_sailing_unit(punit))
    return warmap.seacost[dest_x][dest_y];
  else /* ground unit */
    return warmap.cost[dest_x][dest_y];
}

/**************************************************************************
This list should probably be made by having a list of airbase and then
adding the players cities. It takes a little time to iterate over the map
as it is now, but as it is only used in the players turn that does not
matter.
**************************************************************************/
static void make_list_of_refuel_points(struct player *pplayer)
{
  struct refuel *prefuel;
  struct city *pcity;
  struct tile *ptile;

  refuellist_size = 0;

  whole_map_iterate(x, y) {
    ptile = map_get_tile(x, y);
    if ((pcity = is_allied_city_tile(ptile, pplayer)) != NULL
	&& is_non_allied_unit_tile(ptile, pplayer) == NULL) {
      prefuel = fc_malloc(sizeof(struct refuel));
      init_refuel(prefuel, x, y, FUEL_CITY, MAP_MAX_HEIGHT + MAP_MAX_WIDTH,
		  0);
      refuels[refuellist_size++] = prefuel;
      continue;
    }
    if ((ptile = map_get_tile(x, y))->special & S_AIRBASE) {
      if (is_non_allied_unit_tile(ptile, pplayer) != NULL)
	continue;
      prefuel = fc_malloc(sizeof(struct refuel));
      init_refuel(prefuel, x, y,
		  FUEL_AIRBASE, MAP_MAX_HEIGHT + MAP_MAX_WIDTH, 0);
      refuels[refuellist_size++] = prefuel;
    }
  } whole_map_iterate_end;
}

/**************************************************************************
...
**************************************************************************/
static void dealloc_refuel_stack(void)
{
  int i; 
  for (i = 0; i < refuellist_size; i++)
    free(refuels[i]);
}

/**************************************************************************
We need to find a route that our airplane can use without crashing. The
first waypoint of this route is returned inside the arguments dest_x and
dest_y. This can be the point where we start, fx when a plane with few moves
left need to stay in a city to refuel.

First we create a list of all possible refuel points (friendly cities and
airbases on the map). Then the destination is added to this list.
We keep a list of the refuel points we have reached (been_there[]). this is
initialized with the unit position.
We have an array of pointer into the been_there[] stack, indicating where
we shall start and stop (turnindex[]). turnindex[0] is pointing at the top
of the list, and turnindex[i] is pointing at where we should start to
iterate if we want to try to move in turn i.

The algorithm used is Dijkstra. Thus we need to check whether we can arrive at
the goal in 1 turn, then in 2 turns, then in 3 and so on. When we can arrive at
the goal, we want to have as much movement points left as possible. This is a
bit tricky, since Bombers have fuel for more than one turn. This code should
work for airplanes with fuel for >2 turns as well.

Once we reach the pgoal node we finish the current run, then stop (set
reached_goal to 1). Since each node has exactly one pointer to a previous node
(nature of Dijkstra), we can just follow the pointers all the way back to the
start node. The node immediately before (after) the startnode is our first
waypoint, and is returned.

Possible changes:
We might also make sure that fx a fighter will have enough fuel to fly back
to a city after reaching it's destination. This could be done by making a
list of goal from which the last jump on the route could be done
satisfactory. We should also make sure that bombers given an order to
attack a unit will not make the attack on it's last fuel point etc. etc.
These last points would be necessary to make the AI use planes.
the list of extra goals would also in most case stop the algorithm from
iterating over all cities in the case of an impossible goto. In fact those
extra goal are so smart that you should implement them! :)

      -Thue
      [Kero]
**************************************************************************/
static int find_air_first_destination(
  struct unit *punit, /* input param */
  int *dest_x, int *dest_y) /* output param */
{
  struct player *pplayer = unit_owner(punit);
  static struct refuel **been_there;
  static unsigned int *turn_index;
  struct refuel start, *pgoal;
  unsigned int fullmoves = unit_type(punit)->move_rate/SINGLE_MOVE;
  unsigned int fullfuel = unit_type(punit)->fuel;

  int reached_goal;
  int turns, start_turn;
  int max_moves, moves_left;
  int new_nodes, no_new_nodes;
  int i, j, k;

  if (been_there == NULL) {
    /* These are big enough for anything! */
    been_there = fc_malloc((map.xsize*map.ysize+2)*sizeof(struct refuel *));
    turn_index = fc_malloc((map.xsize*map.ysize)*sizeof(unsigned int));
  }

  freelog(LOG_DEBUG, "unit @(%d, %d) fuel=%d, moves_left=%d\n",
	  punit->x, punit->y, punit->fuel, punit->moves_left);

  init_refuel(&start, punit->x, punit->y, FUEL_START, 0, punit->moves_left/3);
  if (punit->fuel > 1) start.moves_left += (punit->fuel-1) * fullmoves;
  been_there[0] = &start;

  make_list_of_refuel_points(pplayer);
  pgoal = fc_malloc(sizeof(struct refuel));
  init_refuel(pgoal, punit->goto_dest_x, punit->goto_dest_y, FUEL_GOAL,
    MAP_MAX_HEIGHT + MAP_MAX_WIDTH, 0);
  refuels[refuellist_size++] = pgoal;

  assert(!same_pos(punit->x, punit->y, punit->goto_dest_x, punit->goto_dest_y));
  reached_goal = FALSE; /* assume start.(x,y) != pgoal->(x,y) */
  turns = 0;
  turn_index[0] = 0;
  new_nodes = 1; /* the node where we start has been added already */
  no_new_nodes = 0;
  /* while we did not reach the goal and found new nodes in the last full fuel
     turns, continue searching */
  while (!reached_goal && (no_new_nodes<fullfuel)) {
    turns++;
    freelog(LOG_DEBUG, "looking for arrival in %d turns", turns);
    turn_index[turns] = turn_index[turns-1] + new_nodes;
    new_nodes = 0;
    /* now find out for turn turns where we can arrive (e.g. we can arrive
       in 5 turns: when fullfuel is 3, we can come from turn 2, 3 and 4) */
    start_turn = turns-fullfuel;
    if (start_turn < 0) start_turn = 0;
    for (i=start_turn; i<turns; i++) {
      freelog(LOG_DEBUG, "can we arrive from turn %d?", i);
      for (j=turn_index[i]; j<turn_index[i+1]; j++) {
	struct refuel *pfrom = been_there[j];
	if (j == 0) {
	  /* start pos is special case as we maybe don't have full moves and/or fuel */
	  max_moves = punit->moves_left/SINGLE_MOVE;
	  if (turns-i > 1) {
	    max_moves += MIN(punit->fuel-1, turns-i-1) * fullmoves;
	  }
	} else {
	  max_moves = pfrom->moves_left - (fullfuel-(turns-i)) * fullmoves;
	}
	freelog(LOG_DEBUG, "unit @@(%d,%d) may use %d moves",
		pfrom->x, pfrom->y, max_moves);
	/* try to move to all of the refuel stations and the goal */
	for (k=0; k<refuellist_size; k++) {
	  struct refuel *pto = refuels[k];
	  moves_left = air_can_move_between(max_moves,
	      pfrom->x, pfrom->y, pto->x, pto->y, pplayer);
	  if (moves_left != -1) {
	    int unit_moves_left = moves_left + (pfrom->moves_left - max_moves);
	    freelog(LOG_DEBUG, "moves_left=%d, unit_moves_left=%d, pto->=%d",
		    moves_left, unit_moves_left, pto->moves_left);
	    if ( (pto->turns > turns) ||
		 ((pto->turns == turns) && (unit_moves_left > pto->moves_left))
	    ) {
	      been_there[turn_index[turns] + new_nodes] = pto;
	      new_nodes++;
	      pto->coming_from = pfrom;
	      pto->moves_left = unit_moves_left;
	      pto->turns = turns;
	      if (pto->type == FUEL_GOAL) reached_goal = TRUE;

	      freelog(LOG_DEBUG, "insert (%i,%i) -> (%i,%i) %d, %d", pfrom->x,
		      pfrom->y, pto->x, pto->y, pto->turns, unit_moves_left);

	    }
	  }
	}
      }
    }
    if (new_nodes == 0) no_new_nodes++; else no_new_nodes = 0;
    /* update moves_left for the next round (we have a full fuel tank) */
    for (j=0; j<new_nodes; j++) {
      been_there[turn_index[turns]+j]->moves_left = fullmoves*fullfuel;
    }
  }

  freelog(LOG_DEBUG, "backtracking");
  /* backtrack */
  if (reached_goal) {
    while (pgoal->coming_from->type != FUEL_START) {
      pgoal = pgoal->coming_from;
      freelog(LOG_DEBUG, "%i,%i", pgoal->x, pgoal->y);
    }
    freelog(LOG_DEBUG, "success");
  } else
    freelog(LOG_DEBUG, "no success");
  freelog(LOG_DEBUG, "air goto done");

  *dest_x = pgoal->x;
  *dest_y = pgoal->y;

  dealloc_refuel_stack();

  return reached_goal;
}

/**************************************************************************
 Returns true if the airspace at given map position _looks_ safe to
 the given player. The airspace is unsafe if the player believes
 there is an enemy unit on it. This is tricky, since we have to
 consider what the player knows/doesn't know about the tile.
**************************************************************************/
static int airspace_looks_safe(int x, int y, struct player *pplayer)
{
  struct tile * ptile = map_get_tile(x, y);

  /* 
   * We do handicap checks for the player with ai_handicap(). This
   * function returns true if the player is handicapped. For human
   * players they'll always return true. This is the desired behavior.
   */

  /* If the tile's unknown, we (may) assume it's safe. */
  if (ai_handicap(pplayer, H_MAP) && !map_get_known(x, y, pplayer)) {
    return AIR_ASSUMES_UNKNOWN_SAFE;
  }

  /* This is bad: there could be a city there that the player doesn't
      know about.  How can we check that? */
  if (is_non_allied_city_tile(ptile, pplayer) != NULL) {
    return FALSE;
  }

  /* If the tile's fogged we again (may) assume it's safe. */
  if (ai_handicap(pplayer, H_FOG) &&
      !map_get_known_and_seen(x, y, pplayer)) {
    return AIR_ASSUMES_FOGGED_SAFE;
  }

  /* The tile is unfogged so we can check for enemy units on the
     tile. */
  return is_non_allied_unit_tile(ptile, pplayer) == NULL;
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
int air_can_move_between(int moves, int src_x, int src_y,
			 int dest_x, int dest_y, struct player *pplayer)
{
  int x, y, dist;
  int total_distance = real_map_distance(src_x, src_y, dest_x, dest_y);

  freelog(LOG_DEBUG,
	  "air_can_move_between(moves=%d, src=(%i,%i), "
	  "dest=(%i,%i), player=%s)", moves, src_x, src_y, dest_x, dest_y,
	  pplayer->name);

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
  x = src_x, y = src_y;
  
  /* 
   * We don't check the endpoint of the goto, since it is possible
   * that the endpoint is a tile which has an enemy which should be
   * attacked. But we do check that all points in between are safe.
   */
  for (dist = total_distance; dist > 1; dist--) {
    /* Warning: straightest_direction may not actually follow the
       straight line. */
    int dir = straightest_direction(x, y, dest_x, dest_y);
    int nx, ny;

    if (!MAPSTEP(nx, ny, x, y, dir)
	|| !airspace_looks_safe(nx, ny, pplayer)) {
      break;
    }
    x = nx, y = ny;
  }
  if (dist == 1) {
    /* Looks like the O(n) quicksearch worked. */
    assert(real_map_distance(x, y, dest_x, dest_y) == 1);
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

  init_warmap(src_x, src_y, AIR_MOVING);

  /* The 0 is inaccurate under A*, but it doesn't matter. */
  add_to_mapqueue(0, src_x, src_y);

  while (get_from_mapqueue(&x, &y)) {
    adjc_dir_iterate(x, y, x1, y1, dir) {
      if (warmap.cost[x1][y1] != MAXCOST) {
	continue;
      }

      /*
       * This comes before the airspace_looks_safe check because it's
       * okay to goto into an enemy. 
       */
      if (x1 == dest_x && y1 == dest_y) {
	/* We're there! */
	freelog(LOG_DEBUG, "air_can_move_between: movecost: %i",
		warmap.cost[x][y] + MOVE_COST_AIR);
	/* The -MOVE_COST_AIR is because we haven't taken the final
	   step yet. */
	return moves - warmap.cost[x][y] - MOVE_COST_AIR;
      }

      /* We refuse to goto through unsafe airspace. */
      if (airspace_looks_safe(x1, y1, pplayer)) {
	int cost = warmap.cost[x][y] + MOVE_COST_AIR;

	warmap.cost[x1][y1] = cost;

	/* Now for A* we find the minimum total cost. */
	cost += real_map_distance(x1, y1, dest_x, dest_y);
	if (cost <= moves) {
	  add_to_mapqueue(cost, x1, y1);
	}
      }
    } adjc_dir_iterate_end;
  }

  freelog(LOG_DEBUG, "air_can_move_between: no route found");
  return -1;
}
