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

#define WARSTACK_DIM 16384
/* Must be a power of 2, size of order (MAP_MAX_WIDTH*MAP_MAX_HEIGHT);
 * (Could be non-power-of-2 and then use modulus in add_to_stack
 * and get_from_warstack.)
 */
   
struct stack_element {
  unsigned char x, y;
};

#define MAXFUEL 100

enum refuel_type {
  FUEL_START = 0, FUEL_GOAL, FUEL_CITY, FUEL_AIRBASE
};

struct refuel {
  enum refuel_type type;
  unsigned int x, y;
  int turns;
  struct refuel *coming_from;
};

static struct stack_element warstack[WARSTACK_DIM];

/* This wastes ~20K of memory and should really be fixed but I'm lazy  -- Syela
 *
 * Note this is really a queue.  And now its a circular queue to avoid
 * problems with large maps.  Note that a single (x,y) can appear multiple
 * times in warstack due to a sequence of paths with successively
 * smaller costs.  --dwp
 */

static unsigned int warstacksize;
static unsigned int warnodes;

static struct refuel *refuels[MAP_MAX_WIDTH*MAP_MAX_HEIGHT]; /* enought :P */
static unsigned int refuelstacksize;

static void make_list_of_refuel_points(struct player *pplayer);
static void dealloc_refuel_stack();
static int find_air_first_destination(struct unit *punit, int *dest_x, int *dest_y);

/**************************************************************************
...
**************************************************************************/
static void add_to_stack(int x, int y)
{
  unsigned int i = warstacksize & (WARSTACK_DIM-1);
  warstack[i].x = x;
  warstack[i].y = y;
  warstacksize++;
}

/**************************************************************************
...
**************************************************************************/
static void get_from_warstack(unsigned int i, int *x, int *y)
{
  assert(i<warstacksize && warstacksize-i<WARSTACK_DIM);
  i &= (WARSTACK_DIM-1);
  *x = warstack[i].x;
  *y = warstack[i].y;
}

/**************************************************************************
...
**************************************************************************/
static void init_warmap(int orig_x, int orig_y, enum unit_move_type which)
{
  int x;

  if (!warmap.cost[0]) {
    for (x = 0; x < map.xsize; x++) {
      warmap.cost[x]=fc_malloc(map.ysize*sizeof(unsigned char));
      warmap.seacost[x]=fc_malloc(map.ysize*sizeof(unsigned char));
      warmap.vector[x]=fc_malloc(map.ysize*sizeof(unsigned char));
    }
  }

  if (which == LAND_MOVING) {
    for (x = 0; x < map.xsize; x++)
      memset(warmap.cost[x],255,map.ysize*sizeof(unsigned char));
    /* one if by land */
    warmap.cost[orig_x][orig_y] = 0;
  } else {
    for (x = 0; x < map.xsize; x++)
      memset(warmap.seacost[x],255,map.ysize*sizeof(unsigned char));
    warmap.seacost[orig_x][orig_y] = 0;
  }
}  

/**************************************************************************
...
**************************************************************************/
void really_generate_warmap(struct city *pcity, struct unit *punit, enum unit_move_type which)
{ /* let generate_warmap interface to this function */
  int x, y, c, k, xx[3], yy[3], x1, y1, tm;
  int orig_x, orig_y;
  int igter = 0;
  int ii[8] = { 0, 1, 2, 0, 2, 0, 1, 2 };
  int jj[8] = { 0, 0, 0, 1, 1, 2, 2, 2 };
  int maxcost = THRESHOLD * 6 + 2; /* should be big enough without being TOO big */
  struct tile *tile0;
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

  init_warmap(orig_x, orig_y, which);
  warstacksize = 0;
  warnodes = 0;

  add_to_stack(orig_x, orig_y);

  if (punit && unit_flag(punit->type, F_IGTER)) igter++;

  /* FIXME: Should this apply only to F_CITIES units? -- jjm */
  if (punit && unit_flag(punit->type, F_SETTLERS)
      && get_unit_type(punit->type)->move_rate==3) maxcost /= 2;
  /* (?) was punit->type == U_SETTLERS -- dwp */

  do {
    get_from_warstack(warnodes, &x, &y);
    warnodes++; /* for debug purposes */
    tile0 = map_get_tile(x, y);
    map_calc_adjacent_xy(x, y, xx, yy);
    for (k = 0; k < 8; k++) {
      x1 = xx[ii[k]];
      y1 = yy[jj[k]];
      if (which == LAND_MOVING) {
/*        if (tile0->move_cost[k] == -3 || tile0->move_cost[k] > 16) c = maxcost;*/
        if (map_get_terrain(x1, y1) == T_OCEAN) {
          if (punit && is_transporter_with_free_space(pplayer, x1, y1)) c = 3;
          else c = maxcost;
        } else if (tile0->terrain == T_OCEAN) c = 3;
        else if (igter) c = 3; /* NOT c = 1 */
        else if (punit) c = MIN(tile0->move_cost[k], unit_types[punit->type].move_rate);
/*        else c = tile0->move_cost[k]; 
This led to a bad bug where a unit in a swamp was considered too far away */
        else {
          tm = map_get_tile(x1, y1)->move_cost[7-k];
          c = (tile0->move_cost[k] + tm + (tile0->move_cost[k] > tm ? 1 : 0))/2;
        }
        
        tm = warmap.cost[x][y] + c;
        if (warmap.cost[x1][y1] > tm && tm < maxcost) {
          warmap.cost[x1][y1] = tm;
          add_to_stack(x1, y1);
        }
      } else {
        c = 3; /* allow for shore bombardment/transport/etc */
        tm = warmap.seacost[x][y] + c;
        if (warmap.seacost[x1][y1] > tm && tm < maxcost) {
          warmap.seacost[x1][y1] = tm;
          if (tile0->move_cost[k] == -3) add_to_stack(x1, y1);
        }
      }
    } /* end for */
  } while (warstacksize > warnodes);
  if (warnodes > WARSTACK_DIM) {
    freelog(LOG_VERBOSE, "Warning: %u nodes in map #%d for (%d, %d)",
	 warnodes, which, orig_x, orig_y);
  }
  freelog(LOG_DEBUG, "Generated warmap for (%d,%d) with %u nodes checked.",
	  orig_x, orig_y, warnodes); 
  /* warnodes is often as much as 2x the size of the continent -- Syela */
}

/**************************************************************************
...
**************************************************************************/
void generate_warmap(struct city *pcity, struct unit *punit)
{
  freelog(LOG_DEBUG, "Generating warmap, pcity = %s, punit = %s",
	  (pcity ? pcity->name : "NULL"),
	  (punit ? unit_types[punit->type].name : "NULL"));

  if (punit) {
    if (pcity && pcity == warmap.warcity) return; /* time-saving shortcut */
    if (warmap.warunit == punit && !warmap.cost[punit->x][punit->y]) return;
    pcity = 0;
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

/* ....... end of old advmilitary.c, beginning of old gotohand.c. ..... */

/**************************************************************************
...
**************************************************************************/
static void dir_deltas(int x0, int y0, int x1, int y1,
		       int *n, int *s, int *e, int *w)
{
  int dx;

  if (y1 > y0) {
    *n = 0;
    *s = y1 - y0;
  } else {
    *n = y0 - y1;
    *s = 0;
  }

  dx = x1 - x0;
  if (dx > map.xsize/2) {
    *e = 0;
    *w = map.xsize - dx;
  } else if (dx > 0) {
    *e = dx;
    *w = 0;
  } else if (dx + (map.xsize/2) > 0) {
    *e = 0;
    *w = 0 - dx;
  } else {
    *e = map.xsize + dx;
    *w = 0;
  }

  return;
}

/**************************************************************************
...
**************************************************************************/
static int dir_ok(int x0, int y0, int x1, int y1, int k)
{ /* The idea of this is to check less nodes in the wrong direction.
These if's might cost some CPU but hopefully less overall. -- Syela */
  int n, s, e, w;

  dir_deltas(x0, y0, x1, y1, &n, &s, &e, &w);

  if (e == map.xsize / 2 || w == map.xsize / 2) { /* thanks, Massimo */
    if (k < 3 && s >= MAX(e, w)) return 0;
    if (k > 4 && n >= MAX(e, w)) return 0;
    return 1;
  }
  switch(k) {
    case 0:
      if (e >= n && s >= w) return 0;
      else return 1;
    case 1:
      if (s && s >= e && s >= w) return 0;
      else return 1;
    case 2:
      if (w >= n && s >= e) return 0;
      else return 1;
    case 3:
      if (e && e >= n && e >= s) return 0;
      else return 1;
    case 4:
      if (w && w >= n && w >= s) return 0;
      else return 1;
    case 5:
      if (e >= s && n >= w) return 0;
      else return 1;
    case 6:
      if (n && n >= w && n >= e) return 0;
      else return 1;
    case 7:
      if (w >= s && n >= e) return 0;
      else return 1;
    default:
      freelog(LOG_NORMAL, "Bad dir_ok call: (%d, %d) -> (%d, %d), %d",
	      x0, y0, x1, y1, k);
      return 0;
  }
}

/**************************************************************************
  Similar to is_my_zoc(), but with some changes:
  - destination (x0,y0) need not be adjacent?
  - don't care about some directions?
  
  Note this function only makes sense for ground units.
**************************************************************************/
static int could_be_my_zoc(struct unit *myunit, int x0, int y0)
{
  /* Fix to bizarre did-not-find bug.  Thanks, Katvrr -- Syela */
  int ii[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
  int jj[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
  int ax, ay, k;
  int owner=myunit->owner;

  assert(is_ground_unit(myunit));
  
  if (same_pos(x0, y0, myunit->x, myunit->y))
    return 0; /* can't be my zoc */
  if (is_tiles_adjacent(x0, y0, myunit->x, myunit->y)
      && !is_enemy_unit_tile(x0, y0, owner))
    return 0;

  for (k = 0; k < 8; k++) {
    ax = map_adjust_x(myunit->x + ii[k]);
    ay = map_adjust_y(myunit->y + jj[k]);
    
    if (!dir_ok(x0, y0, myunit->goto_dest_x, myunit->goto_dest_y, k))
      continue;
    /* don't care too much about ZOC of units we've already gone past -- Syela */
    
    if ((map_get_terrain(ax,ay)!=T_OCEAN) && is_enemy_unit_tile(ax,ay,owner))
      return 0;
  }
  
  return 1;
  /* return was -1 as a flag value; I've moved -1 value into
   * could_unit_move_to_tile(), since that's where it becomes
   * distinct/relevant --dwp
   */
}

/**************************************************************************
  this WAS can_unit_move_to_tile with the notifys removed -- Syela 
  but is now a little more complicated to allow non-adjacent tiles
  returns:
    0 if can't move
    1 if zoc_ok
   -1 if zoc could be ok?
**************************************************************************/
int could_unit_move_to_tile(struct unit *punit, int x0, int y0, int x, int y)
{
  struct tile *ptile,*ptile2;
  struct city *pcity;

  if(punit->activity!=ACTIVITY_IDLE &&
     punit->activity!=ACTIVITY_GOTO && !punit->connecting)
    return 0;

  if(x<0 || x>=map.xsize || y<0 || y>=map.ysize)
    return 0;
  
  if(!is_tiles_adjacent(x0, y0, x, y))
    return 0; 

  if(is_enemy_unit_tile(x,y,punit->owner))
    return 0;

  ptile=map_get_tile(x, y);
  ptile2=map_get_tile(x0, y0);
  if(is_ground_unit(punit)) {
    /* Check condition 4 */
    if(ptile->terrain==T_OCEAN &&
       !is_transporter_with_free_space(&game.players[punit->owner], x, y))
	return 0;

    /* Moving from ocean */
    if(ptile2->terrain==T_OCEAN) {
      /* Can't attack a city from ocean unless marines */
      if(!unit_flag(punit->type, F_MARINES) && is_enemy_city_tile(x,y,punit->owner)) {
	return 0;
      }
    }
  } else if(is_sailing_unit(punit)) {
    if(ptile->terrain!=T_OCEAN && ptile->terrain!=T_UNKNOWN)
      if(!is_friendly_city_tile(x,y,punit->owner))
	return 0;
  } 

  if((pcity=map_get_city(x, y))) {
    if ((pcity->owner!=punit->owner && (is_air_unit(punit) || 
                                        !is_military_unit(punit)))) {
        return 0;  
    }
  }

  if (zoc_ok_move_gen(punit, x0, y0, x, y))
    return 1;
  else if(could_be_my_zoc(punit, x0, y0))
    return -1;	/* flag value  */
  else
    return 0;
}

/**************************************************************************
...
**************************************************************************/
static int goto_tile_cost(struct player *pplayer, struct unit *punit,
			  int x0, int y0, int x1, int y1, int m,
			  enum goto_move_restriction restriction)
{
  int i;
  if (!pplayer->ai.control && !map_get_known(x1, y1, pplayer)) {
    freelog(LOG_DEBUG, "Venturing into the unknown at (%d, %d).", x1, y1);
    /* return(3);   People seemed not to like this. -- Syela */
    return((restriction == GOTO_MOVE_STRAIGHTEST) ? 3 : 15); /* arbitrary deterrent. */
  }
  if (get_defender(pplayer, punit, x1, y1)) {
     if (same_pos(punit->goto_dest_x, punit->goto_dest_y, x1, y1))
       return(MIN(m, unit_types[punit->type].move_rate));
     if (!can_unit_attack_tile(punit, x1, y1)) return(255);
     return(15);
     /* arbitrary deterrent; if we wanted to attack, we wouldn't GOTO */
  } else {
    i = could_unit_move_to_tile(punit, x0, y0, x1, y1);
    if (!i && !unit_really_ignores_zoc(punit) && 
        !is_my_zoc(punit, x0, y0) && !is_my_zoc(punit, x1, y1) &&
        !could_be_my_zoc(punit, x0, y0)) return(255);
    if (!i && !same_pos(x1, y1, punit->goto_dest_x, punit->goto_dest_y)) return(255);
/* in order to allow transports to GOTO shore correctly */
    if (i < 0) return(15); /* there's that deterrent again */
  }
  return(MIN(m, unit_types[punit->type].move_rate));
}

/**************************************************************************
...
**************************************************************************/
static void init_gotomap(int orig_x, int orig_y)
{
  int x;

  init_warmap(orig_x,orig_y,LAND_MOVING);
  init_warmap(orig_x,orig_y,SEA_MOVING);

  for (x = 0; x < map.xsize; x++) {
    memset(warmap.vector[x],0,map.ysize*sizeof(unsigned char));
  }

  return;
} 

/**************************************************************************
...
**************************************************************************/
static int dir_ect(int x0, int y0, int x1, int y1, int k)
{
  int ii[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
  int jj[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
  int x, y;
  x = map_adjust_x(ii[k] + x0);
  y = jj[k] + y0; /* trusted, since I know who's calling us. -- Syela */
  if (same_pos(x, y, x1, y1)) return 1; /* as direct as it gets! */
  return (1 - dir_ok(x, y, x1, y1, 7-k));
}

/**************************************************************************
Return the direction that takes us most directly to dest_x,dest_y.
The direction is a value for use in ii[] and jj[] arrays.
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
This is an A* search; recursively pile tiles on a stack until we have
found a patch or we have exceded maxcost. (Or there are no more tiles in
the stack).
Whenever we reach a tile we see how many movepoints it took to get there,
and compare it to eventual previous moves to the tile. If the route is
faster or just as fast we mark the direction from which we came on the
destination tile.
We have a maxcost, as an upper limit for the lenght of gotos. When we
arrive at the destination we know that we don't have to follow paths that
have exceeded this lenght, and set maxcost to lenght+1. (the +1 because we
still want to find alternative paths of the same lenght, and comparisons
with maxcost are done with <).
When we have found all paths of the shortest lenght to the destination we
backtrack from the destination tile all the way back to the start tile,
marking the routes on the warmap as we go. (again by piling tiles on the
stack).
**************************************************************************/
static int find_the_shortest_path(struct player *pplayer, struct unit *punit,
				  int dest_x, int dest_y,
				  enum goto_move_restriction restriction)
{
  static const char *d[] = { "NW", "N", "NE", "W", "E", "SW", "S", "SE" };
  static const int ii[8] = { 0, 1, 2, 0, 2, 0, 1, 2 };
  static const int jj[8] = { 0, 0, 0, 1, 1, 2, 2, 2 };
  int igter, xx[3], yy[3], x, y, x1, y1, dir;
  int orig_x, orig_y;
  struct tile *ptile;
  enum unit_move_type move_type = unit_types[punit->type].move_type;
  int maxcost = 255;
  int move_cost;
  int straight_dir = 0;	/* init to silence compiler warning */
  unsigned char local_vector[MAP_MAX_WIDTH][MAP_MAX_HEIGHT];
  struct unit *passenger;
  
  /* Often we'll already have a working warmap, but I don't want to
     deal with merging these functions yet.  Once they work correctly
     and independently I can worry about optimizing them. -- Syela */
  memset(local_vector, 0, sizeof(local_vector));
  /* I think I only need to zero (orig_x, orig_y), but ... */

  init_gotomap(punit->x, punit->y);
  warstacksize = 0;
  warnodes = 0;

  orig_x = punit->x;
  orig_y = punit->y;

  add_to_stack(orig_x, orig_y);

  if (punit && unit_flag(punit->type, F_IGTER))
    igter = 1;
  else
    igter = 0;

  /* If we have a passenger abord a ship we must be sure he can disembark
     I have no idea how this works - leaving it as it is -Thue */
  if (move_type == SEA_MOVING) {
    passenger = other_passengers(punit);
    if (passenger)
      if (map_get_terrain(dest_x, dest_y) == T_OCEAN
	  || is_friendly_unit_tile(dest_x, dest_y, passenger->owner)
	  || is_friendly_city_tile(dest_x, dest_y, passenger->owner)
	  || unit_flag(passenger->type, F_MARINES)
	  || is_my_zoc(passenger, dest_x, dest_y))
	passenger = NULL;
  } else
    passenger = NULL;

  /* until we have found the shortest path */
  do {
    get_from_warstack(warnodes, &x, &y);
    warnodes++; /* points to bottom of stack */
    ptile = map_get_tile(x, y);

    /* Initiaze xx and yy to hold the adjacent tiles (this makes sure goto's
       will behave at the poles) and around 0,y where the x values wrap */
    map_calc_adjacent_xy(x, y, xx, yy);

    if (restriction == GOTO_MOVE_STRAIGHTEST)
      straight_dir = straightest_direction(x, y, dest_x, dest_y);

    /* Try to move to all tiles adjacent to x,y. The coordinats of the tile we
       try to move too are x1,y1 */
    for (dir = 0; dir < 8; dir++) {
      /* if the direction is N, S, E or W d[dir][1] is the /0 character... */
      if ((restriction == GOTO_MOVE_CARDINAL_ONLY) && d[dir][1]) continue;

      x1 = xx[ii[dir]];
      y1 = yy[jj[dir]];

      switch (move_type) {
      case LAND_MOVING:
        if (ptile->move_cost[dir] == -3 || ptile->move_cost[dir] > 16)
	  move_cost = maxcost; 
        else if (igter)
	  move_cost = (ptile->move_cost[dir] ? 3 : 0);
        else
	  move_cost = ptile->move_cost[dir];

        move_cost =
	  goto_tile_cost(pplayer, punit, x, y, x1, y1, move_cost, restriction);

	/* This leads to suboptimal paths choosen sometimes */
        if (!dir_ok(x, y, dest_x, dest_y, dir))
	  move_cost += move_cost;

	if ((restriction == GOTO_MOVE_STRAIGHTEST) && (dir == straight_dir))
	  move_cost /= 3;

        if (move_cost == 0 && !dir_ect(x, y, dest_x, dest_y, dir))
	  move_cost = 1;

	/* Add the route to our warmap if it is worth keeping */
        move_cost += warmap.cost[x][y];

        if (move_cost < maxcost) {
          if (warmap.cost[x1][y1] > move_cost) {
            warmap.cost[x1][y1] = move_cost;
            add_to_stack(x1, y1);
            local_vector[x1][y1] = 128>>dir;
	    freelog(LOG_DEBUG,
		    "Candidate: %s from (%d, %d) to (%d, %d), cost %d",
		    d[dir], x, y, x1, y1, move_cost);
          } else if (warmap.cost[x1][y1] == move_cost) {
            local_vector[x1][y1] |= 128>>dir;
	    freelog(LOG_DEBUG,
		    "Co-Candidate: %s from (%d, %d) to (%d, %d), cost %d",
		    d[dir], x, y, x1, y1, move_cost);
          }
        }
	break;

      case SEA_MOVING:
	if (ptile->move_cost[dir] != -3)
	  move_cost = maxcost;
	else if (unit_flag(punit->type, F_TRIREME) && !is_coastline(x1, y1))
	  move_cost = 7;
	else
	  move_cost = 3;

	move_cost =
	  goto_tile_cost(pplayer, punit, x, y, x1, y1, move_cost, restriction);

	/* Again I wonder why the passengers work this way */
	/* AI aims ennemy cities with goto even for ferryboats. 
	   These lines force ferryboats to find a land adjacent to the 
	   destination city to disembark its passengers. 
	   It explains the line 
           if (tile0->move_cost[k] == -3) add_to_stack(x1, y1);
	   in the function really_generate_warmap */

	if (x1 == dest_x && y1 == dest_y && passenger && move_cost < 60 &&
	    !is_my_zoc(passenger, x, y))
	  move_cost = 60; /* passenger cannot disembark */

	/* Again I don't see how these help */
	/* Here, the unit has to go back.
	   For example, you can enter a zoc. But, not go through a zoc.
	   That's why the cost is twice the normal one. */

	if (!dir_ok(x, y, dest_x, dest_y, dir))
	  move_cost += move_cost;

	if ((restriction == GOTO_MOVE_STRAIGHTEST) && (dir == straight_dir))
	  move_cost /= 3;

	move_cost += warmap.seacost[x][y];

	/* AI stuff; hope it is correct. No idea what it does :) */
	if (pplayer->ai.control &&
	    warmap.seacost[x][y] < punit->moves_left && move_cost < maxcost &&
	    move_cost >= punit->moves_left - (get_transporter_capacity(punit) >
			    unit_types[punit->type].attack_strength ? 3 : 2) &&
	    enemies_at(punit, x1, y1)) {
	  move_cost += unit_types[punit->type].move_rate;
	  freelog(LOG_DEBUG, "%s#%d@(%d,%d) dissuaded from (%d,%d) -> (%d,%d)",
		  unit_types[punit->type].name, punit->id,
		  punit->x, punit->y, x1, y1,
		  punit->goto_dest_x, punit->goto_dest_y);
	}

	/* Add the route to our warmap if it is worth keeping */
	if (move_cost < maxcost) {
	  if (warmap.seacost[x1][y1] > move_cost) {
	    warmap.seacost[x1][y1] = move_cost;
	    add_to_stack(x1, y1);
	    local_vector[x1][y1] = 128>>dir;
	    freelog(LOG_DEBUG,
		    "Candidate: %s from (%d, %d) to (%d, %d), cost %d",
		    d[dir], x, y, x1, y1, move_cost);
	  } else if (warmap.seacost[x1][y1] == move_cost) {
	    local_vector[x1][y1] |= 128>>dir;
	    freelog(LOG_DEBUG,
		    "Co-Candidate: %s from (%d, %d) to (%d, %d), cost %d",
		    d[dir], x, y, x1, y1, move_cost);
	  }
	}
	break;

      case AIR_MOVING:
      case HELI_MOVING:
	move_cost = 3;

	if ((restriction == GOTO_MOVE_STRAIGHTEST) && (dir == straight_dir))
	  move_cost /= 3;

	move_cost += warmap.cost[x][y];

	if (!could_unit_move_to_tile(punit, x, y, x1, y1))
	  move_cost = maxcost;

	/* Add the route to our warmap if it is worth keeping */
	if (move_cost < maxcost) {
	  if (warmap.cost[x1][y1] > move_cost) {
	    warmap.cost[x1][y1] = move_cost;
	    add_to_stack(x1, y1);
	    local_vector[x1][y1] = 128>>dir;
	    freelog(LOG_DEBUG,
		    "Candidate: %s from (%d, %d) to (%d, %d), cost %d",
		    d[dir], x, y, x1, y1, move_cost);
	  } else if (warmap.cost[x1][y1] == move_cost) {
	    local_vector[x1][y1] |= 128>>dir;
	    freelog(LOG_DEBUG,
		    "Co-Candidate: %s from (%d, %d) to (%d, %d), cost %d",
		    d[dir], x, y, x1, y1, move_cost);
	  }
	}
	break;

      default:
	move_cost = 255;	/* silence compiler warning */
	freelog(LOG_FATAL, "Bad move_type in find_the_shortest_path().");
	abort();
	break;
      } /****** end switch ******/

      if (x1 == dest_x && y1 == dest_y && maxcost > move_cost) {
	freelog(LOG_DEBUG, "Found path, cost = %d", move_cost);
	/* Make sure we stop searching when we have no hope of finding a shorter path */
        maxcost = move_cost + 1;
      }
    } /* end  for (dir = 0; dir < 8; dir++) */
  } while (warstacksize > warnodes);
  
  freelog(LOG_DEBUG, "GOTO: (%d, %d) -> (%d, %d), %u nodes, cost = %d", 
	  orig_x, orig_y, dest_x, dest_y, warnodes, maxcost - 1);

  if (maxcost == 255)
    return 0; /* No route */

  /* Succeeded. The vector at the destination indicates which way we get there.
     Now backtrack to remove all the blind paths */
  warnodes = 0;
  warstacksize = 0;
  add_to_stack(dest_x, dest_y);

  do {
    get_from_warstack(warnodes, &x, &y);
    warnodes++;
    ptile = map_get_tile(x, y);
    map_calc_adjacent_xy(x, y, xx, yy);

    for (dir = 0; dir < 8; dir++) {
      if ((restriction == GOTO_MOVE_CARDINAL_ONLY) && d[dir][1])
	continue;

      x1 = xx[ii[dir]];
      y1 = yy[jj[dir]];
      if (local_vector[x][y] & (1<<dir)) {
        add_to_stack(x1, y1);
        warmap.vector[x1][y1] |= 128>>dir; /* Mark it on the warmap */
        local_vector[x][y] -= 1<<dir; /* avoid repetition */
	freelog(LOG_DEBUG, "PATH-SEGMENT: %s from (%d, %d) to (%d, %d)",
		d[7-dir], x1, y1, x, y);
      }
    }
  } while (warstacksize > warnodes);
  freelog(LOG_DEBUG, "BACKTRACE: %u nodes", warnodes);

  return 1;
}

/**************************************************************************
...
**************************************************************************/
static int find_a_direction(struct unit *punit,
			    enum goto_move_restriction restriction)
{
  int k, d[8], x, y, n, a, best = 0, d0, d1, h0, h1, u, c;
  char *dir[] = { "NW", "N", "NE", "W", "E", "SW", "S", "SE" };
  int ii[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
  int jj[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
  struct tile *ptile, *adjtile;
  int nearland;
  struct city *pcity;
  struct unit *passenger;
  struct player *pplayer = get_player(punit->owner);

  if (map_get_terrain(punit->x, punit->y) == T_OCEAN)
    passenger = other_passengers(punit);
  else passenger = 0;

  for (k = 0; k < 8; k++) {
    if ((restriction == GOTO_MOVE_CARDINAL_ONLY) && dir[k][1]) continue;
    if (!(warmap.vector[punit->x][punit->y]&(1<<k))) d[k] = 0;
    else {
      if (is_ground_unit(punit))
        c = map_get_tile(punit->x, punit->y)->move_cost[k];
      else c = 3;
      if (unit_flag(punit->type, F_IGTER) && c) c = 1;
      x = map_adjust_x(punit->x + ii[k]); y = map_adjust_y(punit->y + jj[k]);
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
        if (aunit->owner != punit->owner) d1 = -1; /* MINIMUM priority */
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
        adjtile = map_get_tile(x + ii[n], y + jj[n]);
        if (adjtile->terrain != T_OCEAN) nearland++;
        if (!((adjtile->known)&(1u<<punit->owner))) {
          if (punit->moves_left <= c) d[k] -= (d[k]/16); /* Avoid the unknown */
          else d[k]++; /* nice but not important */
        } else { /* NOTE: Not being omniscient here!! -- Syela */
          unit_list_iterate(adjtile->units, aunit) /* lookin for trouble */
            if (aunit->owner != punit->owner && (a = get_attack_power(aunit))) {
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
              if (is_coastline(x + ii[n], y + jj[n])) nearland++;
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
...
**************************************************************************/
int goto_is_sane(struct player *pplayer, struct unit *punit, int x, int y, int omni)
{  
  int ii[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
  int jj[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
  int k, possible = 0;
  if (same_pos(punit->x, punit->y, x, y)) return 1;
  if (is_ground_unit(punit) && 
          (omni || map_get_known_and_seen(x, y, pplayer))) {
    if (map_get_terrain(x, y) == T_OCEAN) {
      if (is_transporter_with_free_space(pplayer, x, y)) {
        for (k = 0; k < 8; k++) {
          if (map_get_continent(punit->x, punit->y) ==
              map_get_continent(x + ii[k], y + jj[k]))
            possible++;
        }
      }
    } else { /* going to a land tile */
      if (map_get_continent(punit->x, punit->y) ==
            map_get_continent(x, y))
         possible++;
      else {
        for (k = 0; k < 8; k++) {
          if (map_get_continent(punit->x + ii[k], punit->y + jj[k]) ==
              map_get_continent(x, y))
            possible++;
        }
      }
    }
    return(possible);
  } else if (is_sailing_unit(punit) &&
	     (omni || map_get_known_and_seen(x, y, pplayer)) &&
	     map_get_terrain(x, y) != T_OCEAN && !map_get_city(x, y) &&
	     !is_terrain_near_tile(x, y, T_OCEAN)) {
    return(0);
  }

  return(1);
}


/**************************************************************************
If we have an air unit we use
find_air_first_destination(punit, &waypoint_x, &waypoint_y)
to get a waypoint to goto. The actual goto is still done with
find_the_shortest_path(pplayer, punit, waypoint_x, waypoint_y, restriction)

There was a really oogy if here.  Mighty Mouse told me not to axe it
because it would cost oodles of CPU time.  He's right for the most part
but Peter and others have recommended more flexibility, so this is a little
different but should still pre-empt calculation of impossible GOTO's.
     -- Syela
**************************************************************************/
void do_unit_goto(struct player *pplayer, struct unit *punit,
		  enum goto_move_restriction restriction)
{
  int x, y, k;
  int ii[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
  int jj[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
  char *d[] = { "NW", "N", "NE", "W", "E", "SW", "S", "SE" };
  int unit_id, dest_x, dest_y, waypoint_x, waypoint_y;
  struct unit *penemy = NULL;

  unit_id = punit->id;
  dest_x = waypoint_x = punit->goto_dest_x;
  dest_y = waypoint_y = punit->goto_dest_y;

  if (same_pos(punit->x, punit->y, dest_x, dest_y) ||
      !goto_is_sane(pplayer, punit, dest_x, dest_y, 0)) {
    punit->activity = ACTIVITY_IDLE;
    punit->connecting = 0;
    send_unit_info(0, punit);
    return;
  }

  if(!punit->moves_left) {
    send_unit_info(0, punit);
    return;
  }

  if (is_air_unit(punit))
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

  /* This has the side effect of marking the warmap with the possible paths */
  if (find_the_shortest_path(pplayer, punit,
			     waypoint_x, waypoint_y, restriction)) {
    do {
      if (!punit->moves_left) return;
      k = find_a_direction(punit, restriction);
      if (k < 0) {
	freelog(LOG_DEBUG, "%s#%d@(%d,%d) stalling so it won't be killed.",
		unit_types[punit->type].name, punit->id,
		punit->x, punit->y);
	/* punit->activity=ACTIVITY_IDLE;*/
	send_unit_info(0, punit);
	return;
      }
      freelog(LOG_DEBUG, "Going %s", d[k]);
      x = map_adjust_x(punit->x + ii[k]);
      y = punit->y + jj[k]; /* no need to adjust this */
      penemy = unit_list_get(&(map_get_tile(x,y)->units), 0);
      if (penemy && penemy->owner == pplayer->player_no)
	penemy = NULL;

      if(!punit->moves_left) return;
      if(!handle_unit_move_request(pplayer, punit, x, y, FALSE)) {
	freelog(LOG_DEBUG, "Couldn't handle it.");
	if(punit->moves_left) {
	  punit->activity=ACTIVITY_IDLE;
	  send_unit_info(0, punit);
	  return;
	};
      } else {
	freelog(LOG_DEBUG, "Handled.");
      }
      if (!player_find_unit_by_id(pplayer, unit_id))
	return; /* unit died during goto! */

      /* Don't attack more than once per goto*/
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
      if (punit->connecting && can_unit_do_activity (punit, punit->activity))
	return;

      freelog(LOG_DEBUG, "Moving on.");
    } while(!(x==waypoint_x && y==waypoint_y));
  } else {
    freelog(LOG_VERBOSE, "Did not find the shortest path for "
	    "%s's %s at (%d, %d) -> (%d, %d)",
	    pplayer->name, unit_types[punit->type].name,
	    punit->x, punit->y, dest_x, dest_y);
  }

  /* ensure that the connecting unit will perform it's activity
     on the destination file too. */
  if (punit->connecting && can_unit_do_activity(punit, punit->activity))
    return;

  /* normally we would just do this unconditionally, but if we had an
     airplane goto we might not be finished even if the loop exited */
  if (punit->x == dest_x && punit->y == dest_y)
    punit->activity=ACTIVITY_IDLE;
  else if (punit->moves_left) {
    struct connection *pc = pplayer->conn;
    if (pc && has_capability("advance_focus_packet", pc->capability)) {
      struct packet_generic_integer packet;
      packet.value = punit->id;
      send_packet_generic_integer(pc, PACKET_ADVANCE_FOCUS,
				  &packet);
    }
  }

  punit->connecting=0;
  send_unit_info(0, punit);
}

/**************************************************************************
Calculate and return cost (in terms of move points) for unit to move
to specified destination.  
**************************************************************************/
int calculate_move_cost(struct player *pplayer, struct unit *punit,
			int dest_x, int dest_y) {
  /* perhaps we should do some caching -- fisch */
    
  /*
  static struct unit *last_punit = NULL;

  if(last_punit != punit) {
    generate_warmap(NULL,punit);
  }
  */

  if (is_air_unit(punit) || is_heli_unit(punit)) {
    /* The warmap only really knows about land and sea
       units, so for these we just assume cost = distance.
       (times 3 for road factor).
       (Could be wrong if there are things in the way.)
    */
    return 3 * real_map_distance(punit->x, punit->y, dest_x, dest_y);
  }

  generate_warmap(NULL,punit);
  
  if(is_sailing_unit(punit))
    return warmap.seacost[dest_x][dest_y];
  else if (is_ground_unit(punit)) {
    return warmap.cost[dest_x][dest_y];
  } else {
    return warmap.cost[dest_x][dest_y] < warmap.seacost[dest_x][dest_y] ?
      warmap.cost[dest_x][dest_y]
      : warmap.seacost[dest_x][dest_y];
  }
    
}

/**************************************************************************
This list should probably be made by having a list of airbase and then
adding the players cities. It takes a little time to iterate over the map
as it is now, but as it is only used in the players turn that does not
matter.
FIXME: to make the airplanes gotos more straight it would be practical to
have the refuel points closest to the destination first. This wouldn't be
needed if we prioritizes between routes of equal lenght in
find_air_first_destination, which is a preferable solution.
**************************************************************************/
static void make_list_of_refuel_points(struct player *pplayer)
{
  int x,y;
  int playerid = pplayer->player_no;
  struct refuel *prefuel;
  struct city *pcity;
  struct tile *ptile;

  refuelstacksize = 0;

  for (x = 0; x < map.xsize; x++) {
    for (y = 0; y < map.ysize; y++) {
      if ((pcity = map_get_city(x,y)) && pcity->owner == playerid) {
	prefuel = fc_malloc(sizeof(struct refuel));
	prefuel->x = x; prefuel->y = y;
	prefuel->type = FUEL_CITY;
	prefuel->turns = 255;
	prefuel->coming_from = NULL;
	refuels[refuelstacksize++] = prefuel;
	continue;
      }
      if ((ptile = map_get_tile(x,y))->special&S_AIRBASE) {
	if (is_enemy_unit_tile(x, y, playerid))
	  continue;
	prefuel = fc_malloc(sizeof(struct refuel));
	prefuel->x = x; prefuel->y = y;
	prefuel->type = FUEL_AIRBASE;
	prefuel->turns = 255;
	prefuel->coming_from = NULL;
	refuels[refuelstacksize++] = prefuel;
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
  for (i = 0; i < refuelstacksize; i++)
    free(refuels[i]);
}

/**************************************************************************
We need to find a route that our airplane can use without crashing. The
first waypoint of this route is returned inside the arguments dest_x and
dest_y.

First we create a list of all possible refuel points (friendly cities and
airbases on the map). Then the destination is added to this stack.
We keep a list of the refuel points we have reached (been_there[]). this is
initialized with the unit position.
We have an array of pointer into the been_there[] stack, indicating where
we shall start and stop (fuelindex[]). fuelindex[0] is pointing at the top
of the stack, and fuelindex[i] is pointing at where we should start to
iterate if we want to try a goto using i fuel. For i>0 these are initialized
to zero.

We the have a do while(...) loop. This is exited once we have found a route or
there is no hope of ever finding one (the pointers into the stack of
been_there nodes all point at the top). We have a "turns" variable that
indicates how many turns we have used to build up the current been_there[]
stack. The trick is that for each while loop we find exactly those refuel
nodes that we can get to in "turns" turns. This is done by the way the
pointers into been_there are arranged; for example the pointers
fuelindex[i] and fuelindex[i+1] points to those nodes we reached i and i+1
turns ago. So by iterating from fuelindex[i] to fuelindex[i+1] in the
been_there stack (the for loop inside the while does this for all pairs
(i, i+1), i <=fullfuel), and multiplying the moves with i (the fuel we can use),
we can simulate a move over i+1 turns using i+1 fuel. Because this node
has already waited i turns before it was used, using it will result in a
route that takes exactly "turns turns".

The starting node is handled as a special case as it might not have full
fuel or movepoints. Note that once we have used another node as a waypoint
we can be sure when starting from there to have full fuel and moves again.

At the end of each while loop we advance the indices in fuelindex[].
For each refuel point we check in the been_there[] stack we try to move
between it and all unused nodes in the refuelstack[]. (this is usually quite
quick). If we can it is marked as used and the pointer to the node from the
been_there stack we came from is saved in it, and the new node put onto the
been_there[] stack. Since we at a given iteration of the while loop are as
far as we can get with the given number of turns, it means that if a node is
already marked there already exist a faster or just as fast way to get to it,
and we need not compare the two.
Since we iterate through the been_there stack starting with those jumps that
only use 1 fuel these are marked first, and therefore preferred over routes
of equal length that use 2 fuel.

Once we reach the pgoal node we stop (set found_goal to 1). Since each node
now has exactly one pointer to a previous node, we can just follow the
pointers all the way back to the start node. The node immediately before
(after) the startnode is our first waypoint, and is returned.

Possible changes:
I think the turns member of refuel points is not needed if we just deleted
the points as they can no longer be the shortest route, i.e. after the for
loop if they have been added inside the for loop. That would just be a
stack of numbers corresponding to indices in the refuel_stack array
(and then making the entries in refuel_stack nulpointers).
We might want compare routes of equal length, so that routes using cities
are preferred over routes using airports which in turn are better than
routes with long jumps. Now routes using cities or airports are preferred
over long jumps, but cities and airports are equal. It should also be made
so that the goto of the list of gotos of equal length that left the largest
amount of move points were selected.
We might also make sure that fx a fighter will have enough fuel to fly back
to a city after reaching it's destination. This could be done by making a
list of goal from which the last jump on the route could be done
satisfactory. We should also make sure that bombers given an order to
attack a unit will not make the attack on it's last fuel point etc. etc.
These last points would be necessary to make the AI use planes.
the list of extra goals would also in most case stop the algorithm from
iterating over all cities in the case of an impossible goto. In fact those
extra goal are so smart that you should implement them! :)

note: The turns variabe in the refuel struct is currently not used for
anything but a boolean marker.
      -Thue
**************************************************************************/
static int find_air_first_destination(struct unit *punit, int *dest_x, int *dest_y)
{
  struct player *pplayer = unit_owner(punit);
  static struct refuel *been_there[MAP_MAX_HEIGHT*MAP_MAX_WIDTH];
  unsigned int fuelindex[MAXFUEL+1]; /* +1: stack top pointer */
  struct refuel *prefuel, *pgoal;
  struct refuel start;
  unsigned int found_goal = 0;
  unsigned int fullmoves = get_unit_type(punit->type)->move_rate/3;
  unsigned int fullfuel = get_unit_type(punit->type)->fuel;
  int turns = 0;
  int i, j;

  assert(fullfuel <= MAXFUEL);
  for (i = 1; i <= fullfuel; i++)
    fuelindex[i] = 0;
  fuelindex[0] = 1;

  start.x     = punit->x;
  start.y     = punit->y;
  start.type  = FUEL_START;
  start.turns = 0;
  been_there[0] = &start;

  make_list_of_refuel_points(pplayer);

  pgoal        = fc_malloc(sizeof(struct refuel));
  pgoal->x     = punit->goto_dest_x;
  pgoal->y     = punit->goto_dest_y;
  pgoal->type  = FUEL_GOAL;
  pgoal->turns = 255;
  pgoal->coming_from = NULL;
  refuels[refuelstacksize++] = pgoal;

  freelog(LOG_DEBUG, "stacksize = %i", refuelstacksize);

  do { /* until we find no new nodes or we found a path */
    int new_nodes, total_moves, from, to;
    struct refuel *pfrom, *pto;
    int usefuel;
    enum refuel_type type;

    freelog(LOG_DEBUG, "round we go: %i", turns);

    new_nodes = 0;
    usefuel = 1;

    while(usefuel <= fullfuel) { /* amount of fuel we use */
      total_moves = fullmoves * usefuel;
      from = fuelindex[usefuel];
      to = fuelindex[usefuel - 1];
      freelog(LOG_DEBUG, "from = %i, to = %i, fuel = %i, total_moves = %i\n",
	      from, to, usefuel, total_moves);

      for (i = from; i<to; i++) { /* iterate through the nodes that use usefuel fuel */
	pfrom = been_there[i];
	type = pfrom->type;
	switch (type) {
	case FUEL_START:
	  if (usefuel > punit->fuel)
	    continue;
	  total_moves = punit->moves_left/3 + (usefuel-1) * fullmoves;
	default:
	  ;
	};

	for (j = 0; j < refuelstacksize; j++) { /* try to move to all of the nodes */
	  pto = refuels[j];
	  if (pto->turns >= 255
	      && naive_air_can_move_between(total_moves, pfrom->x, pfrom->y,
					    pto->x, pto->y, pplayer->player_no)) {
	    freelog(LOG_DEBUG, "inserting from = %i,%i  |  to = %i,%i",
		    pfrom->x,pfrom->y,pto->x,pto->y);

	    been_there[fuelindex[0] + new_nodes++] = pto;
	    pto->coming_from = pfrom;
	    pto->turns = turns;
	    if (pto->type == FUEL_GOAL)
	      found_goal = 1;
	  }
	}
      } /* end for */
      usefuel++;
    } /* end while(usefuel <= fullfuel) */

    turns++;

    for (i = fullfuel; i > 0; i--)
      fuelindex[i] = fuelindex[i-1];
    fuelindex[0] += new_nodes;

    freelog(LOG_DEBUG, "new_nodes = %i, turns = %i, found_goal = %i",
	    new_nodes, turns, found_goal);

  } while (!found_goal && fuelindex[0] != fuelindex[fullfuel]);

  freelog(LOG_DEBUG, "backtracking");
  /* backtrack */
  if (found_goal) {
    while ((prefuel = pgoal->coming_from)->type != FUEL_START) {
      pgoal = prefuel;
      freelog(LOG_DEBUG, "%i,%i", pgoal->x, pgoal->y);
    }
    freelog(LOG_DEBUG, "success");
  } else
    freelog(LOG_DEBUG, "no success");
  freelog(LOG_DEBUG, "air goto done");

  *dest_x = pgoal->x;
  *dest_y = pgoal->y;

  dealloc_refuel_stack();

  return found_goal;
}

/**************************************************************************
Decides whether we can move from src_x,src_y to dest_x,dest_y in moves
moves.
The function has 3 stages:
Try to rule out the possibility in O(1) time              else
Try to quickly verify in O(moves) time                    else
Create a movemap to decide with certainty in O(moves2) time.
Each step should catch the vast majority of tries.
**************************************************************************/
int naive_air_can_move_between(int moves, int src_x, int src_y,
			       int dest_x, int dest_y, int playerid)
{
  int x, y, go_x, go_y, i, movescount;
  struct tile *ptile;
  freelog(LOG_DEBUG, "naive_air_can_move_between %i,%i -> %i,%i, moves: %i",
	  src_x, src_y, dest_x, dest_y, moves);

  /* Do we have the chances of a snowball in hell? */
  if (real_map_distance(src_x, src_y, dest_x, dest_y) > moves)
    return 0;

  /* Yep. Lets first try a quick and direct
     approch that will almost allways work */
  x = src_x; y = src_y;
  movescount = moves;
  while (real_map_distance(x, y, dest_x, dest_y) > 1) {
    struct unit *punit;

    if (movescount <= 1)
      goto TRYFULL;

    go_x = x > dest_x ?
      (x-dest_x < map.xsize/2 ? -1 : 1):
      (dest_x-x < map.xsize/2 ? 1 : -1);
    go_y = dest_y > y ? 1 : -1;

    freelog(LOG_DEBUG, "%i,%i to %i,%i. go_x: %i, go_y:%i",
	    x, y, dest_x, dest_y, go_x, go_y);
    if (x == dest_x) {
      for (i = x-1 ; i <= x+1; i++)
	if ((ptile = map_get_tile(i, y+go_y))
	    /* && is_real_tile(i, y+go_y) */
	    && (!(punit = unit_list_get(&(ptile->units), 0))
		|| punit->owner == playerid)) {
	  x = i;
	  y++;
	  goto NEXTCYCLE;
	}
      goto TRYFULL;
    } else if (y == dest_y) {
      for (i = y-1 ; i <= y+1; i++)
	if ((ptile = map_get_tile(x+go_x, i))
	    && is_real_tile(x+go_x, i)
	    && (!(punit = unit_list_get(&(ptile->units), 0))
		|| punit->owner == playerid)) {
	  x += go_x;
	  y = i;
	  goto NEXTCYCLE;
	}
      goto TRYFULL;
    }

    /* (x+go_x, y+go_y) is always real, given (x, y) is real */
    ptile = map_get_tile(x+go_x, y+go_y);
    punit = unit_list_get(&(ptile->units), 0);
    if (!punit || punit->owner == playerid) {
      x += go_x;
      y += go_y;
      goto NEXTCYCLE;
    }

    /* (x+go_x, y) is always real, given (x, y) is real */
    ptile = map_get_tile(x+go_x, y);
    punit = unit_list_get(&(ptile->units), 0);
    if (!punit || punit->owner == playerid) {
      x += go_x;
      goto NEXTCYCLE;
    }

    /* (x, y+go_y) is always real, given (x, y) is real */
    ptile = map_get_tile(x, y+go_y);
    punit = unit_list_get(&(ptile->units), 0);
    if (!punit || punit->owner == playerid) {
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
  return 1;

  /* Nope, we couldn't find a quick way. Lets do the full monty.
     Copied from find_the_shortest_path. If you want to know how
     it works refer to the excellent documentation there */
 TRYFULL:
  freelog(LOG_DEBUG, "didn't work. Lets try full");
  {
    int ii[8] = { 0, 1, 2, 0, 2, 0, 1, 2 };
    int jj[8] = { 0, 0, 0, 1, 1, 2, 2, 2 };
    int xx[3], yy[3], x1, y1, k;
    struct unit *punit;

    init_gotomap(src_x, src_y);
    warstacksize = 0;
    warnodes = 0;
    add_to_stack(src_x, src_y);

    do {
      get_from_warstack(warnodes, &x, &y);
      warnodes++;
      map_calc_adjacent_xy(x, y, xx, yy);

      for (k = 0; k < 8; k++) {
	x1 = xx[ii[k]];
	y1 = yy[jj[k]];

	if (warmap.cost[x1][y1] == 255) {
	  ptile = map_get_tile(x1,y1);
	  punit = unit_list_get(&(ptile->units), 0);
	  if (!punit || punit->owner == playerid
	      || (x1 == dest_x && y1 == dest_y)) {/* allow attack goto's */
	    warmap.cost[x1][y1] = warmap.cost[x][y] + 1;
	    add_to_stack(x1, y1);
	  }
	}
      } /* end for */
    } while (warmap.cost[x][y] <= moves
	     && warmap.cost[dest_x][dest_y] == 255);

    freelog(LOG_DEBUG, "movecost: %i", warmap.cost[dest_x][dest_y]);
  }
  return (warmap.cost[dest_x][dest_y] <= moves);
}
