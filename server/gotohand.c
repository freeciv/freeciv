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

#include "capability.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "rand.h"

#include "maphand.h"
#include "settlers.h"
#include "unitfunc.h"
#include "unithand.h"
#include "unittools.h"

#include "gotohand.h"

struct move_cost_map warmap;

struct stack_element {
  unsigned char x, y;
};

#define WARQUEUE_DIM 16384
static struct stack_element warqueue[WARQUEUE_DIM];
/* WARQUEUE_DIM must be a power of 2, size of order (MAP_MAX_WIDTH*MAP_MAX_HEIGHT);
 * (Could be non-power-of-2 and then use modulus in add_to_warqueue
 * and get_from_warqueue.)
 * It is used to make the queue circular by wrapping, to awoid problems on large maps.
 * (see add_to_warqueue and get_from_warqueue).
 * Note that a single (x,y) can appear multiple
 * times in warqueue due to a sequence of paths with successively
 * smaller costs.  --dwp
 */

/* warqueuesize points to the top of the queue.
   warnodes at the buttom */
static unsigned int warqueuesize;
static unsigned int warnodes;

/* These are used for airplane GOTOs with waypoints */
#define MAXFUEL 100

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

static struct refuel *refuels[MAP_MAX_WIDTH*MAP_MAX_HEIGHT]; /* enought :P */
static unsigned int refuellist_size;

static void make_list_of_refuel_points(struct player *pplayer);
static void dealloc_refuel_stack();
static int find_air_first_destination(struct unit *punit, int *dest_x, int *dest_y);

/**************************************************************************
The "warqueuesize & (WARQUEUE_DIM-1)" make the queue wrap if too big.
**************************************************************************/
static void add_to_warqueue(int x, int y)
{
  unsigned int i = warqueuesize & (WARQUEUE_DIM-1);
  warqueue[i].x = x;
  warqueue[i].y = y;
  warqueuesize++;
}

/**************************************************************************
The "i &= (WARQUEUE_DIM-1);" make the queue wrap if too big.
**************************************************************************/
static void get_from_warqueue(unsigned int i, int *x, int *y)
{
  assert(i<warqueuesize && warqueuesize-i<WARQUEUE_DIM);
  i &= (WARQUEUE_DIM-1);
  *x = warqueue[i].x;
  *y = warqueue[i].y;
}

/**************************************************************************
Reset the movecosts of the warmap.
**************************************************************************/
static void init_warmap(int orig_x, int orig_y, enum unit_move_type move_type)
{
  int x;

  if (!warmap.cost[0]) {
    for (x = 0; x < map.xsize; x++) {
      warmap.cost[x]=fc_malloc(map.ysize*sizeof(unsigned char));
      warmap.seacost[x]=fc_malloc(map.ysize*sizeof(unsigned char));
      warmap.vector[x]=fc_malloc(map.ysize*sizeof(unsigned char));
    }
  }

  switch (move_type) {
  case LAND_MOVING:
    for (x = 0; x < map.xsize; x++)
      memset(warmap.cost[x], 255, map.ysize*sizeof(unsigned char));
    warmap.cost[orig_x][orig_y] = 0;
    break;
  case SEA_MOVING:
    for (x = 0; x < map.xsize; x++)
      memset(warmap.seacost[x], 255, map.ysize*sizeof(unsigned char));
    warmap.seacost[orig_x][orig_y] = 0;
    break;
  default:
    freelog(LOG_FATAL, "Bad move_type in init_warmap().");
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
is 255. (the value it is initialized with)
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
  int x, y, move_cost, dir, x1, y1;
  int orig_x, orig_y;
  int igter;
  int maxcost = THRESHOLD * 6 + 2; /* should be big enough without being TOO big */
  struct tile *ptile;
  struct player *pplayer;

  if (pcity) {
    orig_x = pcity->x;
    orig_y = pcity->y;
    pplayer = &game.players[pcity->owner];
  } else {
    orig_x = punit->x;
    orig_y = punit->y;
    pplayer = &game.players[punit->owner];
  }

  init_warmap(orig_x, orig_y, move_type);
  warqueuesize = 0;
  warnodes = 0;

  add_to_warqueue(orig_x, orig_y);

  if (punit && unit_flag(punit->type, F_IGTER))
    igter = 1;
  else
    igter = 0;

  /* FIXME: Should this apply only to F_CITIES units? -- jjm */
  if (punit
      && unit_flag(punit->type, F_SETTLERS)
      && get_unit_type(punit->type)->move_rate==3)
    maxcost /= 2;
  /* (?) was punit->type == U_SETTLERS -- dwp */

  do {
    get_from_warqueue(warnodes, &x, &y);
    warnodes++;
    ptile = map_get_tile(x, y);
    for (dir = 0; dir < 8; dir++) {
      x1 = x + DIR_DX[dir];
      y1 = y + DIR_DY[dir];
      if (!normalize_map_pos(&x1, &y1))
	continue;

      switch (move_type) {
      case LAND_MOVING:
	if (warmap.cost[x1][y1] <= warmap.cost[x][y])
	  continue; /* No need for all the calculations */

        if (map_get_terrain(x1, y1) == T_OCEAN) {
          if (punit && ground_unit_transporter_capacity(x1, y1, pplayer->player_no) > 0)
	    move_cost = 3;
          else
	    continue;
        } else if (ptile->terrain == T_OCEAN)
	  move_cost = 3;
        else if (igter)
	  /* NOT c = 1 (Syela) [why not? - Thue] */
	  move_cost = (ptile->move_cost[dir] ? 3 : 0);
        else if (punit)
	  move_cost = MIN(ptile->move_cost[dir], unit_types[punit->type].move_rate);
	/* else c = ptile->move_cost[k]; 
	   This led to a bad bug where a unit in a swamp was considered too far away */
        else { /* we have a city */
          int tmp = map_get_tile(x1, y1)->move_cost[7-dir]; /* The move in the opposite direction*/
          move_cost = (ptile->move_cost[dir] + tmp +
		       (ptile->move_cost[dir] > tmp ? 1 : 0))/2;
        }

        move_cost = warmap.cost[x][y] + move_cost;
        if (warmap.cost[x1][y1] > move_cost && move_cost < maxcost) {
          warmap.cost[x1][y1] = move_cost;
          add_to_warqueue(x1, y1);
        }
	break;


      case SEA_MOVING:
        move_cost = 3;
        move_cost = warmap.seacost[x][y] + move_cost;
        if (warmap.seacost[x1][y1] > move_cost && move_cost < maxcost) {
	  /* by adding the move_cost to the warmap regardless if we can move between
	     we allow for shore bombardment/transport to inland positions/etc. */
          warmap.seacost[x1][y1] = move_cost;
          if (ptile->move_cost[dir] == -3) /* -3 means ships can move between */
	    add_to_warqueue(x1, y1);
	}
	break;
      default:
	move_cost = 0; /* silence compiler warning */
	freelog(LOG_FATAL, "Bad/unimplemented move_type in really_generate_warmap().");
	abort();
      }
    } /* end for */
  } while (warqueuesize > warnodes);

  if (warnodes > WARQUEUE_DIM) {
    freelog(LOG_VERBOSE, "Warning: %u nodes in map #%d for (%d, %d)",
	 warnodes, move_type, orig_x, orig_y);
  }

  freelog(LOG_DEBUG, "Generated warmap for (%d,%d) with %u nodes checked.",
	  orig_x, orig_y, warnodes); 
  /* warnodes is often as much as 2x the size of the continent -- Syela */
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
	  (punit ? unit_types[punit->type].name : "NULL"));

  if (punit) {
    if (pcity && pcity == warmap.warcity)
      return;
    if (warmap.warunit == punit && !warmap.cost[punit->x][punit->y])
      return;

    pcity = NULL;
  }

  warmap.warcity = pcity;
  warmap.warunit = punit;

  if (punit) {
    if (is_sailing_unit(punit)) {
      init_warmap(punit->x, punit->y, LAND_MOVING);
      really_generate_warmap(pcity, punit, SEA_MOVING);
    } else {
      init_warmap(punit->x, punit->y, SEA_MOVING);
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
Resets the warmap for a new GOTO calculation.
FIXME: Do we have to memset the warmap vector?
**************************************************************************/
static void init_gotomap(int orig_x, int orig_y, enum unit_move_type move_type)
{
  int x;

  switch (move_type) {
  case LAND_MOVING:
  case HELI_MOVING:
  case AIR_MOVING:
    init_warmap(orig_x, orig_y, LAND_MOVING);
    break;
  case SEA_MOVING:
    init_warmap(orig_x, orig_y, SEA_MOVING);
    break;
  }

  for (x = 0; x < map.xsize; x++) {
    memset(warmap.vector[x], 0, map.ysize*sizeof(unsigned char));
  }

  return;
}

/**************************************************************************
Returns false if you are going the in opposite direction of the destination.
The 3 directions most opposite the one to the target is considered wrong.
"dir" is the direction you get if you use x = DIR_DX[dir], y = DIR_DY[dir]
**************************************************************************/
static int dir_ok(int src_x, int src_y, int dest_x, int dest_y, int dir)
{
  int diff_x, diff_y;
  if (dest_x > src_x) {
    diff_x = dest_x-src_x < map.xsize/2 ? 1 : -1;
  } else if (dest_x < src_x) {
    diff_x = src_x-dest_x < map.xsize/2 ? -1 : 1;
  } else { /* dest_x == src_x */
    diff_x = 0;
  }
  if (dest_y != src_y)
    diff_y = dest_y > src_y ? 1 : -1;
  else
    diff_y = 0;

  switch(dir) {
  case 0:
    if (diff_x >= 0 && diff_y >= 0) return 0;
    else return 1;
  case 1:
    if (diff_y == 1) return 0;
    else return 1;
  case 2:
    if (diff_x <= 0 && diff_y >= 0) return 0;
    else return 1;
  case 3:
    if (diff_x == 1) return 0;
    else return 1;
  case 4:
    if (diff_x == -1) return 0;
    else return 1;
  case 5:
    if (diff_x >= 0 && diff_y <= 0) return 0;
    else return 1;
  case 6:
    if (diff_y == -1) return 0;
    else return 1;
  case 7:
    if (diff_x <= 0 && diff_y <= 0) return 0;
    else return 1;
  default:
    freelog(LOG_NORMAL, "Bad dir_ok call: (%d, %d) -> (%d, %d), %d",
	    src_x, src_y, dest_x, dest_y, dir);
    return 0;
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
  int go_x, go_y;

  /* Should we go up or down, east or west: go_x/y is the "step" in x/y.
     Will allways be -1 or 1 even if src_x == dest_x or src_y == dest_y. */
  go_x = dest_x > src_x ?
    (dest_x-src_x < map.xsize/2 ? 1 : -1) :
    (src_x-dest_x < map.xsize/2 ? -1 : 1);
  go_y = dest_y > src_y ? 1 : -1;

  if (src_x == dest_x)
    best_dir = (go_y > 0) ? 6 : 1;
  else if (src_y == dest_y)
    best_dir = (go_x > 0) ? 4 : 3;
  else if (go_x > 0)
    best_dir = (go_y > 0) ? 7 : 2;
  else /* go_x < 0 */
    best_dir = (go_y > 0) ? 5 : 0;

  return (best_dir);
}

/**************************************************************************
Can we move between for ZOC? (only for land units). Includes a speciel case
only relevant for GOTOs (see below). 
**************************************************************************/
static int goto_zoc_ok(struct unit *punit, int src_x, int src_y,
		       int dest_x, int dest_y)
{
  if (unit_flag(punit->type, F_IGZOC))
    return 1;
  if (is_allied_unit_tile(map_get_tile(dest_x, dest_y), punit->owner))
    return 1;
  if (map_get_city(src_x, src_y) || map_get_city(dest_x, dest_y))
    return 1;
  if (map_get_terrain(src_x,src_y)==T_OCEAN || map_get_terrain(dest_x,dest_y)==T_OCEAN)
    return 1;
  if (is_my_zoc(punit, src_x, src_y) || is_my_zoc(punit, dest_x, dest_y))
    return 1;

  /* Both positions are currently enemy ZOC. Since the AI depend on it's
     units bumping into enemies while on GOTO it need to be able to plot
     a course which attacks enemy units.
     This code makes sure that if there is a unit in the way that the GOTO
     made a path over (attack), the unit's ZOC effect (which is now
     irrelevant, it is dead) doesn't have any effect.
     If this wasn't here a path involving two enemy units would not be
     found by the algoritm.

     FIXME: We currently use dir_ok to asses where we came from; it would
     be more correct if the function was passed the direction we actually
     came from (dir) */

  /* If we just started the GOTO the enemy unit blocking ZOC on the tile
     we come from is still alive. */
  if (src_x == punit->x && src_y == punit->y
      && !is_non_allied_unit_tile(map_get_tile(dest_x, dest_y), punit->owner))
    return 0;

  {
    int dir;
    int x, y;
    int owner = punit->owner;

    for (dir = 0; dir < 8; dir++) {
      x = map_adjust_x(src_x + DIR_DX[dir]);
      y = map_adjust_y(src_y + DIR_DY[dir]);

      if (!dir_ok(dest_x, dest_y, punit->goto_dest_x, punit->goto_dest_y, dir))
	continue;
      if ((map_get_terrain(x, y)!=T_OCEAN) && is_enemy_unit_tile(map_get_tile(x, y), owner))
	return 0;
    }
    return 0;
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
Whenever we reach a tile we see how many movepoints it took to get there,
and compare it to eventual previous moves to the tile. If the route is
faster or just as fast we mark the direction from which we came on the
destination tile. (via a vector local to this function). If we come to a
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
  static const char *d[] = { "NW", "N", "NE", "W", "E", "SW", "S", "SE" };
  int igter, x, y, x1, y1, dir;
  int orig_x, orig_y;
  struct tile *psrctile, *pdesttile;
  enum unit_move_type move_type = unit_types[punit->type].move_type;
  int maxcost = 255;
  int move_cost, total_cost;
  int straight_dir = 0;	/* init to silence compiler warning */
  static unsigned char local_vector[MAP_MAX_WIDTH][MAP_MAX_HEIGHT];
  struct unit *pcargo;

  orig_x = punit->x;
  orig_y = punit->y;

  if ((dest_x == orig_x) && (dest_y == orig_y)) return 1; /* [Kero] */
  
  local_vector[orig_x][orig_y] = 0;

  init_gotomap(punit->x, punit->y, move_type);
  warqueuesize = 0;
  warnodes = 0;

  add_to_warqueue(orig_x, orig_y);

  if (punit && unit_flag(punit->type, F_IGTER))
    igter = 1;
  else
    igter = 0;

  /* If we have a passenger abord a ship we must be sure he can disembark
     This shouldn't be neccesary, as ZOC does not have an effect for sea-land
     movement, but some code in aiunit.c assumes the goto work like this, so
     I will leave it for now */
  if (move_type == SEA_MOVING) {
    pcargo = other_passengers(punit);
    if (pcargo)
      if (map_get_terrain(dest_x, dest_y) == T_OCEAN ||
	  !is_non_allied_unit_tile(map_get_tile(dest_x, dest_y), pcargo->owner) ||
	  is_allied_city_tile(map_get_tile(dest_x, dest_y), pcargo->owner) ||
	  unit_flag(pcargo->type, F_MARINES) ||
	  is_my_zoc(pcargo, dest_x, dest_y))
	pcargo = NULL;
  } else
    pcargo = NULL;

  /* until we have found the shortest paths */
  do {
    get_from_warqueue(warnodes, &x, &y);
    warnodes++; /* points to bottom of queue */
    psrctile = map_get_tile(x, y);

    if (restriction == GOTO_MOVE_STRAIGHTEST)
      straight_dir = straightest_direction(x, y, dest_x, dest_y);

    /* Try to move to all tiles adjacent to x,y. The coordinats of the tile we
       try to move to are x1,y1 */
    for (dir = 0; dir < 8; dir++) {
      /* if the direction is N, S, E or W d[dir][1] is the /0 character... */
      if ((restriction == GOTO_MOVE_CARDINAL_ONLY) && d[dir][1]) continue;

      x1 = x + DIR_DX[dir];
      y1 = y + DIR_DY[dir];
      if (!normalize_map_pos(&x1, &y1))
	continue;
      
      pdesttile = map_get_tile(x1, y1);

      switch (move_type) {
      case LAND_MOVING:
	if (warmap.cost[x1][y1] <= warmap.cost[x][y])
	  continue; /* No need for all the calculations. Note that this also excludes
		       RR loops, ie you can't create a cycle with the same move_cost */

	if (pdesttile->terrain == T_OCEAN) {
	  if (ground_unit_transporter_capacity(x1, y1, punit->owner) <= 0)
	    continue;
	  else
	    move_cost = 3;
	} else if (psrctile->terrain == T_OCEAN)
	  move_cost = 3;
	else if (igter)
	  move_cost = (psrctile->move_cost[dir] ? 3 : 0);
	else
	  move_cost = MIN(psrctile->move_cost[dir], unit_types[punit->type].move_rate);

	if (!pplayer->ai.control && !map_get_known(x1, y1, pplayer)) {
	  /* Don't go into the unknown. 15 is an arbitrary deterrent. */
	  move_cost = (restriction == GOTO_MOVE_STRAIGHTEST) ? 3 : 15;
	} else if (is_non_allied_unit_tile(pdesttile, punit->owner)) {
	  if (x1 != dest_x || y1 != dest_y) { /* Allow players to target anything */
	    if (pplayer->ai.control) {
	      if ((!is_enemy_unit_tile(pdesttile, punit->owner)
		   || !is_military_unit(punit))
		  && !is_diplomat_unit(punit)) {
		continue; /* unit is non_allied and non_enemy, ie non_attack */
	      } else {
		move_cost = 3; /* Attack cost */
	      }
	    } else {
	      continue; /* non-AI players don't attack on goto */
	    }
	  } else {
	    move_cost = 3;
	  }
	} else if (is_non_allied_city_tile(pdesttile, punit->owner)) {
	  if ((is_non_attack_city_tile(pdesttile, punit->owner)
	       || !is_military_unit(punit))) {
	    if (!is_diplomat_unit(punit)
		&& (x1 != dest_x || y1 != dest_y)) /* Allow players to target anything */
	      continue;
	    else
	      move_cost = 3;
	  }
	} else if (!goto_zoc_ok(punit, x, y, x1, y1))
	  continue;

	if (restriction == GOTO_MOVE_STRAIGHTEST && dir == straight_dir)
	  move_cost /= 3;

	/* Add the route to our warmap if it is worth keeping */
	total_cost = move_cost + warmap.cost[x][y];
	if (total_cost < maxcost) {
          if (warmap.cost[x1][y1] > total_cost) {
            warmap.cost[x1][y1] = total_cost;
            add_to_warqueue(x1, y1);
            local_vector[x1][y1] = 128>>dir;
	    freelog(LOG_DEBUG,
		    "Candidate: %s from (%d, %d) to (%d, %d), cost %d",
		    d[dir], x, y, x1, y1, total_cost);
          } else if (warmap.cost[x1][y1] == total_cost) {
            local_vector[x1][y1] |= 128>>dir;
	    freelog(LOG_DEBUG,
		    "Co-Candidate: %s from (%d, %d) to (%d, %d), cost %d",
		    d[dir], x, y, x1, y1, total_cost);
          }
        }
	break;

      case SEA_MOVING:
	if (warmap.seacost[x1][y1] <= warmap.seacost[x][y])
	  continue; /* No need for all the calculations */

	if (psrctile->move_cost[dir] != -3 /* is -3 if sea units can move between */
	    && (dest_x != x1 || dest_y != y1)) /* allow ai transports to target a shore */
	  continue;
	else if (unit_flag(punit->type, F_TRIREME) && !is_coastline(x1, y1))
	  move_cost = 7;
	else
	  move_cost = 3;

	/* See previous comment on pcargo */
	if (x1 == dest_x && y1 == dest_y && pcargo && move_cost < 60 &&
	    !is_my_zoc(pcargo, x, y))
	  move_cost = 60;

	if (!pplayer->ai.control && !map_get_known(x1, y1, pplayer))
	  move_cost = (restriction == GOTO_MOVE_STRAIGHTEST) ? 3 : 15; /* arbitrary deterrent. */

	/* We don't allow attacks during GOTOs here; you can almost
	   always find a way around enemy units on sea */
	if (x1 != dest_x || y1 != dest_y) {
	  if (is_non_allied_unit_tile(pdesttile, punit->owner)
	      || is_non_allied_city_tile(pdesttile, punit->owner))
	    continue;
	}

	if ((restriction == GOTO_MOVE_STRAIGHTEST) && (dir == straight_dir))
	  move_cost /= 3;

	total_cost = move_cost + warmap.seacost[x][y];

	/* For the ai, maybe avoid going close to enemies. */
	if (pplayer->ai.control &&
	    warmap.seacost[x][y] < punit->moves_left
	    && total_cost < maxcost
	    && total_cost >= punit->moves_left - (get_transporter_capacity(punit) >
			      unit_types[punit->type].attack_strength ? 3 : 2)
	    && enemies_at(punit, x1, y1)) {
	  total_cost += unit_types[punit->type].move_rate;
	  freelog(LOG_DEBUG, "%s#%d@(%d,%d) dissuaded from (%d,%d) -> (%d,%d)",
		  unit_types[punit->type].name, punit->id,
		  punit->x, punit->y, x1, y1,
		  punit->goto_dest_x, punit->goto_dest_y);
	}

	/* Add the route to our warmap if it is worth keeping */
	if (total_cost < maxcost) {
	  if (warmap.seacost[x1][y1] > total_cost) {
	    warmap.seacost[x1][y1] = total_cost;
	    add_to_warqueue(x1, y1);
	    local_vector[x1][y1] = 128>>dir;
	    freelog(LOG_DEBUG,
		    "Candidate: %s from (%d, %d) to (%d, %d), cost %d",
		    d[dir], x, y, x1, y1, total_cost);
	  } else if (warmap.seacost[x1][y1] == total_cost) {
	    local_vector[x1][y1] |= 128>>dir;
	    freelog(LOG_DEBUG,
		    "Co-Candidate: %s from (%d, %d) to (%d, %d), cost %d",
		    d[dir], x, y, x1, y1, total_cost);
	  }
	}
	break;

      case AIR_MOVING:
      case HELI_MOVING:
	if (warmap.cost[x1][y1] <= warmap.cost[x][y])
	  continue; /* No need for all the calculations */

	move_cost = 3;
	/* Planes could run out of fuel, therefore we don't care if territory
	   is unknown. Also, don't attack except at the destination. */

	if (is_non_allied_unit_tile(pdesttile, punit->owner)) {
	  if (x1 != dest_x || y1 != dest_y) {
	    continue;
	  }
	} else {
	  struct city *pcity = is_non_allied_city_tile(pdesttile, punit->owner);
	  if (pcity
	      && (players_non_attack(punit->owner, pcity->owner)
		  || !is_heli_unit(punit)))
	    continue;
	}

	if ((restriction == GOTO_MOVE_STRAIGHTEST) && (dir == straight_dir))
	  move_cost /= 3;

	/* Add the route to our warmap if it is worth keeping */
	total_cost = move_cost + warmap.cost[x][y];
	if (total_cost < maxcost) {
	  if (warmap.cost[x1][y1] > total_cost) {
	    warmap.cost[x1][y1] = total_cost;
	    add_to_warqueue(x1, y1);
	    local_vector[x1][y1] = 128>>dir;
	    freelog(LOG_DEBUG,
		    "Candidate: %s from (%d, %d) to (%d, %d), cost %d",
		    d[dir], x, y, x1, y1, total_cost);
	  } else if (warmap.cost[x1][y1] == total_cost) {
	    local_vector[x1][y1] |= 128>>dir;
	    freelog(LOG_DEBUG,
		    "Co-Candidate: %s from (%d, %d) to (%d, %d), cost %d",
		    d[dir], x, y, x1, y1, total_cost);
	  }
	}
	break;

      default:
	move_cost = 255;	/* silence compiler warning */
	freelog(LOG_FATAL, "Bad move_type in find_the_shortest_path().");
	abort();
	break;
      } /****** end switch ******/

      if (x1 == dest_x && y1 == dest_y && maxcost > total_cost) {
	freelog(LOG_DEBUG, "Found path, cost = %d", total_cost);
	/* Make sure we stop searching when we have no hope of finding a shorter path */
        maxcost = total_cost + 1;
      }
    } /* end  for (dir = 0; dir < 8; dir++) */
  } while (warqueuesize > warnodes);

  freelog(LOG_DEBUG, "GOTO: (%d, %d) -> (%d, %d), %u nodes, cost = %d", 
	  orig_x, orig_y, dest_x, dest_y, warnodes, maxcost - 1);

  if (maxcost == 255)
    return 0; /* No route */

  /*** Succeeded. The vector at the destination indicates which way we get there.
     Now backtrack to remove all the blind paths ***/
  warnodes = 0;
  warqueuesize = 0;
  add_to_warqueue(dest_x, dest_y);

  do {
    get_from_warqueue(warnodes, &x, &y);
    warnodes++;

    for (dir = 0; dir < 8; dir++) {
      if ((restriction == GOTO_MOVE_CARDINAL_ONLY) && d[dir][1])
	continue;

      if (local_vector[x][y] & (1<<dir)) {
	x1 = x + DIR_DX[dir];
	y1 = y + DIR_DY[dir];
	if (!normalize_map_pos(&x1, &y1))
	  continue;

	if (move_type == LAND_MOVING)
	  assert(warmap.cost[x1][y1] != 255);
        add_to_warqueue(x1, y1);
        warmap.vector[x1][y1] |= 128>>dir; /* Mark it on the warmap */
        local_vector[x][y] -= 1<<dir; /* avoid repetition */
	freelog(LOG_DEBUG, "PATH-SEGMENT: %s from (%d, %d) to (%d, %d)",
		d[7-dir], x1, y1, x, y);
      }
    }
  } while (warqueuesize > warnodes);
  freelog(LOG_DEBUG, "BACKTRACE: %u nodes", warnodes);

  return 1;
}

/**************************************************************************
This is used to choose among the valid directions marked on the warmap by
the find_the_shortest_path() function.
Returns a direction as used in DIR_DX[] and DIR_DY[] arrays, or -1 if none could be
found.

This function does not check for loops, which we currently rely on
find_the_shortest_path() to make sure there are none of. If the warmap did
contain a loop repeated calls of this function may result in the unit going
in cycles forever.
**************************************************************************/
static int find_a_direction(struct unit *punit,
			    enum goto_move_restriction restriction,
			    const int dest_x, const int dest_y)
{
  int k, d[8], x, y, n, a, best = 0, d0, d1, h0, h1, u, c;
  char *dir[] = { "NW", "N", "NE", "W", "E", "SW", "S", "SE" };
  struct tile *ptile, *adjtile;
  int nearland;
  struct city *pcity;
  struct unit *passenger;
  struct player *pplayer = get_player(punit->owner);

  if (map_get_terrain(punit->x, punit->y) == T_OCEAN)
    passenger = other_passengers(punit);
  else passenger = NULL;

  /* If we can get to the destination rigth away there is nothing to be gained
     from going round in little circles to move across desirable squares */
  for (k = 0; k < 8; k++) {
    if (warmap.vector[punit->x][punit->y]&(1<<k)
	&& !(restriction == GOTO_MOVE_CARDINAL_ONLY && dir[k][1])) {
      x = map_adjust_x(punit->x + DIR_DX[k]);
      y = map_adjust_y(punit->y + DIR_DY[k]);
      if (x == dest_x && y == dest_y)
	return k;
    }
  }

  for (k = 0; k < 8; k++) {
    if ((restriction == GOTO_MOVE_CARDINAL_ONLY) && dir[k][1]) continue;
    if (!(warmap.vector[punit->x][punit->y]&(1<<k))) d[k] = 0;
    else {
      if (is_ground_unit(punit))
        c = map_get_tile(punit->x, punit->y)->move_cost[k];
      else c = 3;
      if (unit_flag(punit->type, F_IGTER) && c) c = 1;
      x = map_adjust_x(punit->x + DIR_DX[k]); y = map_adjust_y(punit->y + DIR_DY[k]);
      if (passenger) {
	freelog(LOG_DEBUG, "%d@(%d,%d) evaluating (%d,%d)[%d/%d]",
		punit->id, punit->x, punit->y, x, y, c, punit->moves_left);
      }
      ptile = map_get_tile(x, y);
      d0 = get_simple_defense_power(punit->type, x, y);
      pcity = map_get_city(x, y);
      n = 2;
      if (pcity) { /* this code block inspired by David Pfitzner -- Syela */
        if (city_got_citywalls(pcity)) n += 2;
        if (city_got_building(pcity, B_SDI)) n++;
        if (city_got_building(pcity, B_SAM)) n++;
        if (city_got_building(pcity, B_COASTAL)) n++;
      }
      d0 = (d0 * n) / 2;
      h0 = punit->hp; h1 = 0; d1 = 0; u = 1;
      unit_list_iterate(ptile->units, aunit)
        if (!players_allied(aunit->owner, punit->owner)) d1 = -1; /* MINIMUM priority */
        else {
          u++;
          a = get_simple_defense_power(aunit->type, x, y) * n / 2;
          if (a * aunit->hp > d1 * h1) { d1 = a; h1 = aunit->hp; }
        }
      unit_list_iterate_end;
      if (u == 1) d[k] = d0 * h0;
      else if (pcity || ptile->special&S_FORTRESS)
        d[k] = MAX(d0 * h0, d1 * h1);
      else if ((d0 * h0) <= (d1 * h1)) d[k] = (d1 * h1) * (u - 1) / u;
      else d[k] = MIN(d0 * h0 * u, d0 * h0 * d0 * h0 * (u - 1) / MAX(u, (d1 * h1 * u)));
      if (d0 > d1) d1 = d0;

      if (ptile->special&S_ROAD) d[k] += 10; /* in case we change directions */
      if (ptile->special&S_RAILROAD) d[k] += 10; /* next turn, roads are nice */

      nearland = 0;
      if (!pplayer->ai.control && !map_get_known(x, y, pplayer)) nearland++;
      for (n = 0; n < 8; n++) {
        adjtile = map_get_tile(x + DIR_DX[n], y + DIR_DY[n]);
        if (adjtile->terrain != T_OCEAN) nearland++;
        if (!((adjtile->known)&(1u<<punit->owner))) {
          if (punit->moves_left <= c) d[k] -= (d[k]/16); /* Avoid the unknown */
          else d[k]++; /* nice but not important */
        } else { /* NOTE: Not being omniscient here!! -- Syela */
          unit_list_iterate(adjtile->units, aunit) /* lookin for trouble */
            if (players_at_war(aunit->owner, punit->owner)
		&& (a = get_attack_power(aunit))) {
              if (punit->moves_left < c + 3) { /* can't fight */
                if (passenger && !is_ground_unit(aunit)) d[k] = -99;
                else d[k] -= d1 * (aunit->hp * a * a / (a * a + d1 * d1));
              }
            }
          unit_list_iterate_end;
        } /* end this-tile-is-seen else */
      } /* end tiles-adjacent-to-dest for */
 
      if (unit_flag(punit->type, F_TRIREME) && !nearland) {
        if (punit->moves_left < 6) d[k] = -1; /* Tired of Kaput!! -- Syela */
        else if (punit->moves_left == 6) {
          for (n = 0; n < 8; n++) {
            if ((warmap.vector[x][y]&(1<<n))) {
              if (is_coastline(x + DIR_DX[n], y + DIR_DY[n])) nearland++;
            }
          }
          if (!nearland) d[k] = 1;
        }
      }

      if (d[k] < 1 && (!game.players[punit->owner].ai.control ||
         !punit->ai.passenger || punit->moves_left >= 6)) d[k] = 1;
      if (d[k] > best) { 
        best = d[k];
	freelog(LOG_DEBUG, "New best = %d: (%d, %d) -> (%d, %d)",
		best, punit->x, punit->y, x, y);
      }
    } /* end is-a-valid-vector */
  } /* end for */

  if (!best) {
    return(-1);
  }

  do {
    do {
      k = myrand(8);
    } while ((restriction == GOTO_MOVE_CARDINAL_ONLY) && dir[k][1]);
  } while (d[k] < best);
  return(k);
}

/**************************************************************************
Basic checks as to whether a GOTO is possible.
**************************************************************************/
int goto_is_sane(struct unit *punit, int x, int y, int omni)
{  
  struct player *pplayer = unit_owner(punit);
  int k, possible = 0;
  if (same_pos(punit->x, punit->y, x, y)) return 1;
  if (is_ground_unit(punit) && 
          (omni || map_get_known_and_seen(x, y, pplayer->player_no))) {
    if (map_get_terrain(x, y) == T_OCEAN) {
      if (ground_unit_transporter_capacity(x, y, pplayer->player_no) > 0) {
        for (k = 0; k < 8; k++) {
          if (map_get_continent(punit->x, punit->y) ==
              map_get_continent(x + DIR_DX[k], y + DIR_DY[k]))
            possible++;
        }
      }
    } else { /* going to a land tile */
      if (map_get_continent(punit->x, punit->y) ==
            map_get_continent(x, y))
         possible++;
      else {
        for (k = 0; k < 8; k++) {
          if (map_get_continent(punit->x + DIR_DX[k], punit->y + DIR_DY[k]) ==
              map_get_continent(x, y))
            possible++;
        }
      }
    }
    return(possible);
  } else if (is_sailing_unit(punit) &&
	     (omni || map_get_known_and_seen(x, y, pplayer->player_no)) &&
	     map_get_terrain(x, y) != T_OCEAN && !map_get_city(x, y) &&
	     !is_terrain_near_tile(x, y, T_OCEAN)) {
    return(0);
  }

  return(1);
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
void do_unit_goto(struct unit *punit, enum goto_move_restriction restriction)
{
  struct player *pplayer = unit_owner(punit);
  int x, y, dir;
  static const char *d[] = { "NW", "N", "NE", "W", "E", "SW", "S", "SE" };
  int unit_id, dest_x, dest_y, waypoint_x, waypoint_y;
  struct unit *penemy = NULL;

  unit_id = punit->id;
  dest_x = waypoint_x = punit->goto_dest_x;
  dest_y = waypoint_y = punit->goto_dest_y;

  if (same_pos(punit->x, punit->y, dest_x, dest_y) ||
      !goto_is_sane(punit, dest_x, dest_y, 0)) {
    punit->activity = ACTIVITY_IDLE;
    punit->connecting = 0;
    send_unit_info(0, punit);
    return;
  }

  if(!punit->moves_left) {
    send_unit_info(0, punit);
    return;
  }

  if (is_air_unit(punit)) {
    if (!find_air_first_destination(punit, &waypoint_x, &waypoint_y)) {
      freelog(LOG_VERBOSE, "Did not find an airroute for "
	      "%s's %s at (%d, %d) -> (%d, %d)",
	      pplayer->name, unit_types[punit->type].name,
	      punit->x, punit->y, dest_x, dest_y);
      punit->activity = ACTIVITY_IDLE;
      punit->connecting = 0;
      send_unit_info(0, punit);
      return;
    }
  }

  /* This has the side effect of marking the warmap with the possible paths */
  if (find_the_shortest_path(punit, waypoint_x, waypoint_y, restriction)) {
    do { /* move the unit along the path chosen by find_the_shortest_path() while we can */
      if (!punit->moves_left)
	return;

      dir = find_a_direction(punit, restriction, waypoint_x, waypoint_y);
      if (dir < 0) {
	freelog(LOG_DEBUG, "%s#%d@(%d,%d) stalling so it won't be killed.",
		unit_types[punit->type].name, punit->id,
		punit->x, punit->y);
	return;
      }

      freelog(LOG_DEBUG, "Going %s", d[dir]);
      x = map_adjust_x(punit->x + DIR_DX[dir]);
      y = punit->y + DIR_DY[dir]; /* no need to adjust this */
      penemy = is_enemy_unit_tile(map_get_tile(x, y), punit->owner);

      if (!punit->moves_left)
	return;
      if (!handle_unit_move_request(punit, x, y, FALSE)) {
	freelog(LOG_DEBUG, "Couldn't handle it.");
	if (punit->moves_left) {
	  punit->activity=ACTIVITY_IDLE;
	  send_unit_info(0, punit);
	  return;
	}
      } else {
	freelog(LOG_DEBUG, "Handled.");
      }
      if (!player_find_unit_by_id(pplayer, unit_id))
	return; /* unit died during goto! */

      /* Don't attack more than once per goto */
      if (penemy && !pplayer->ai.control) { /* Should I cancel for ai's too? */
 	punit->activity = ACTIVITY_IDLE;
 	send_unit_info(0, punit);
 	return;
      }

      if(punit->x!=x || punit->y!=y) {
	freelog(LOG_DEBUG, "Aborting, out of movepoints.");
	send_unit_info(0, punit);
	return; /* out of movepoints */
      }

      /* single step connecting unit when it can do it's activity */
      if (punit->connecting && can_unit_do_activity(punit, punit->activity))
	return;

      freelog(LOG_DEBUG, "Moving on.");
    } while(!(x==waypoint_x && y==waypoint_y));
  } else {
    freelog(LOG_VERBOSE, "Did not find the shortest path for "
	    "%s's %s at (%d, %d) -> (%d, %d)",
	    pplayer->name, unit_types[punit->type].name,
	    punit->x, punit->y, dest_x, dest_y);
    handle_unit_activity_request(punit, ACTIVITY_IDLE);
  }
  /** Finished moving the unit for this turn **/

  /* ensure that the connecting unit will perform it's activity
     on the destination file too. */
  if (punit->connecting && can_unit_do_activity(punit, punit->activity))
    return;

  /* normally we would just do this unconditionally, but if we had an
     airplane goto we might not be finished even if the loop exited */
  if (punit->x == dest_x && punit->y == dest_y)
    punit->activity=ACTIVITY_IDLE;
  else if (punit->moves_left) {
    struct packet_generic_integer packet;
    packet.value = punit->id;
    lsend_packet_generic_integer(&pplayer->connections,
				 PACKET_ADVANCE_FOCUS, &packet);
  }

  punit->connecting=0;
  send_unit_info(0, punit);
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
    return 3 * real_map_distance(punit->x, punit->y, dest_x, dest_y);
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
  int x, y;
  int playerid = pplayer->player_no;
  struct refuel *prefuel;
  struct city *pcity;
  struct tile *ptile;

  refuellist_size = 0;

  for (x = 0; x < map.xsize; x++) {
    for (y = 0; y < map.ysize; y++) {
      ptile = map_get_tile(x,y);
      if ((pcity = is_allied_city_tile(ptile, playerid))
	  && !is_non_allied_unit_tile(ptile, playerid)) {
	prefuel = fc_malloc(sizeof(struct refuel));
	init_refuel(prefuel, x, y, FUEL_CITY, MAP_MAX_HEIGHT+MAP_MAX_WIDTH, 0);
	refuels[refuellist_size++] = prefuel;
	continue;
      }
      if ((ptile = map_get_tile(x,y))->special&S_AIRBASE) {
	if (is_non_allied_unit_tile(ptile, playerid))
	  continue;
	prefuel = fc_malloc(sizeof(struct refuel));
	init_refuel(prefuel, x, y,
		    FUEL_AIRBASE, MAP_MAX_HEIGHT+MAP_MAX_WIDTH, 0);
	refuels[refuellist_size++] = prefuel;
      }
    }
  }
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
  unsigned int fullmoves = get_unit_type(punit->type)->move_rate/3;
  unsigned int fullfuel = get_unit_type(punit->type)->fuel;

  unsigned int reached_goal;
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
  reached_goal = 0; /* assume start.(x,y) != pgoal->(x,y) */
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
	  max_moves = punit->moves_left/3;
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
	      pfrom->x, pfrom->y, pto->x, pto->y, pplayer->player_no);
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
	      if (pto->type == FUEL_GOAL) reached_goal = 1;

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
      pgoal = pgoal->coming_from;;
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
 Returns #moves left when player playerid is moving a unit from src_x,src_y to
 dest_x,dest_y in moves moves. [Kero]
 If there was no way to move between returns -1.

The function has 3 stages:
Try to rule out the possibility in O(1) time              else
Try to quickly verify in O(moves) time                    else
Create a movemap to decide with certainty in O(moves2) time.
Each step should catch the vast majority of tries.
**************************************************************************/
int air_can_move_between(int moves, int src_x, int src_y,
			 int dest_x, int dest_y, int playerid)
{
  int x, y, go_x, go_y, i, movescount;
  struct tile *ptile;
  freelog(LOG_DEBUG, "naive_air_can_move_between %i,%i -> %i,%i, moves: %i",
	  src_x, src_y, dest_x, dest_y, moves);

  /* O(1) */
  if (real_map_distance(src_x, src_y, dest_x, dest_y) > moves) return -1;
  if (real_map_distance(src_x, src_y, dest_x, dest_y) == 0) return moves;

  /* O(moves). Try to go to the destination in a ``straight'' line. */
  x = src_x; y = src_y;
  movescount = moves;
  while (real_map_distance(x, y, dest_x, dest_y) > 1) {
    if (movescount <= 1)
      goto TRYFULL;

    go_x = (x > dest_x ?
	    (x-dest_x < map.xsize/2 ? -1 : 1) :
	    (dest_x-x < map.xsize/2 ? 1 : -1));
    go_y = (dest_y > y ? 1 : -1);

    freelog(LOG_DEBUG, "%i,%i to %i,%i. go_x: %i, go_y:%i",
	    x, y, dest_x, dest_y, go_x, go_y);
    if (x == dest_x) {
      for (i = x-1 ; i <= x+1; i++)
	if ((ptile = map_get_tile(i, y+go_y))
	    /* && is_real_tile(i, y+go_y) */
	    && ! is_non_allied_unit_tile(ptile, playerid)) {
	  x = i;
	  y += go_y;
	  goto NEXTCYCLE;
	}
      goto TRYFULL;
    } else if (y == dest_y) {
      for (i = y-1 ; i <= y+1; i++)
	if ((ptile = map_get_tile(x+go_x, i))
	    && is_real_tile(x+go_x, i)
	    && ! is_non_allied_unit_tile(ptile, playerid)) {
	  x += go_x;
	  y = i;
	  goto NEXTCYCLE;
	}
      goto TRYFULL;
    }

    /* (x+go_x, y+go_y) is always real, given (x, y) is real */
    ptile = map_get_tile(x+go_x, y+go_y);
    if (! is_non_allied_unit_tile(ptile, playerid)) {
      x += go_x;
      y += go_y;
      goto NEXTCYCLE;
    }

    /* (x+go_x, y) is always real, given (x, y) is real */
    ptile = map_get_tile(x+go_x, y);
    if (! is_non_allied_unit_tile(ptile, playerid)) {
      x += go_x;
      goto NEXTCYCLE;
    }

    /* (x, y+go_y) is always real, given (x, y) is real */
    ptile = map_get_tile(x, y+go_y);
    if (! is_non_allied_unit_tile(ptile, playerid)) {
      y += go_y;
      goto NEXTCYCLE;
    }

    /* we didn't advance.*/
    goto TRYFULL;

  NEXTCYCLE:
    movescount--;
  }
  /* if this loop stopped we made it! We found a way! */
  freelog(LOG_DEBUG, "end of loop; success");
  return movescount-1;

  /* Nope, we couldn't find a quick way. Lets do the full monty.
     Copied from find_the_shortest_path. If you want to know how
     it works refer to the documentation there */
 TRYFULL:
  freelog(LOG_DEBUG, "didn't work. Lets try full");
  {
    int x1, y1, dir;
    struct unit *penemy;

    init_gotomap(src_x, src_y, AIR_MOVING);
    warqueuesize = 0;
    warnodes = 0;
    add_to_warqueue(src_x, src_y);

    do {
      get_from_warqueue(warnodes, &x, &y);
      warnodes++;

      for (dir = 0; dir < 8; dir++) {
	x1 = x + DIR_DX[dir];
	y1 = y + DIR_DY[dir];
	if (!normalize_map_pos(&x1, &y1))
	  continue;
	if (warmap.cost[x1][y1] <= warmap.cost[x][y])
	  continue; /* No need for all the calculations */

	if (warmap.cost[x1][y1] == 255) {
	  ptile = map_get_tile(x1, y1);
	  penemy = is_non_allied_unit_tile(ptile, playerid);
	  if (penemy
	      || (x1 == dest_x && y1 == dest_y)) { /* allow attack goto's */
	    warmap.cost[x1][y1] = warmap.cost[x][y] + 1;
	    add_to_warqueue(x1, y1);
	  }
	}
      } /* end for */
    } while (warmap.cost[x][y] <= moves
	     && warmap.cost[dest_x][dest_y] == 255
	     && warnodes < warqueuesize);

    freelog(LOG_DEBUG, "movecost: %i", warmap.cost[dest_x][dest_y]);
  }
  return (warmap.cost[dest_x][dest_y] <= moves)?
    moves - warmap.cost[dest_x][dest_y]: -1;
}
