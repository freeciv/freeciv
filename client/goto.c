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

#include <assert.h>
#include <string.h>

#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "unit.h"

#include "clinet.h"
#include "control.h"
#include "mapview_g.h"

#include "goto.h"

static void undraw_line(void);

/**************************************************************************
Various stuff for the goto routes
**************************************************************************/
#define INITIAL_ARRAY_LENGTH 100
int goto_array_length = INITIAL_ARRAY_LENGTH; /* allocated length */
int goto_array_index = 0; /* points to where the next element should be inserted */
struct map_position *goto_array = NULL;

struct waypoint {
  int x;
  int y;
  int goto_array_start; /* Here the first tile in the route from x,y exclusive
			   x, y is/should be inserted. */
};
#define INITIAL_WAYPOINT_LENGTH 50
struct waypoint *waypoint_list = NULL;
int waypoint_list_length = INITIAL_WAYPOINT_LENGTH; /* allocated length */
int waypoint_list_index = 0; /* points to where the next element should be inserted */

int is_active = 0;


struct client_goto_map goto_map;

/* These are used for all GOTO's */

#define MAXCOST 0x7FFF /* max for signed short */
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

/* This is a constant time priority queue. One drawback: you cannot insert
   an item with priority less than the smallest item (which shouldn't be a
   problem for our uses) */

/* FIXME: The queue use an array with the movecosts as indices.
   This was smart in the server where the max movecost is 255.
   It is not a good idea here in the client where the max movecost 0xFFFF.
   It is impossible to use when we raise the max movecost to 0xFFFFFFFF
   as we intend to.

   Setting max movecost to 0xFFFFFFFF would allow us to evaluate the tiles
   much more fine-grained, putting attractiveness evaluations beside pure
   cost into it.
   It would also be nice when we are able to change the definitions of
   SINGLE_MOVE etc to larger numbers.

   Se we need to change the implementation at some point. */

/**************************************************************************
Called once per use of the queue.
**************************************************************************/
static void init_queue(void)
{
  int i;
  static int is_allocated = 0;
  if (!is_allocated) {
    for (i = 0; i < MAXARRAYS; i++) {
      mappos_arrays[i] = NULL;
    }
    is_allocated = 1;
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
      return 0;
    our_array = cost_lookup[lowest_cost].first_array;
    if (our_array == NULL) {
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
    *x = our_array->pos[our_array->first_pos].x;
    *y = our_array->pos[our_array->first_pos].y;
    our_array->first_pos++;
    freelog(LOG_DEBUG, "got %i,%i, at cost %i", *x, *y, goto_map.move_cost[*x][*y]);
    return 1;
  }
  return 0;
}

/********************************************************************** 
Called once per game.
***********************************************************************/
void init_client_goto(void)
{
  int x_itr;
  static int is_init = 0, old_xsize;

  if (!goto_array) {
    goto_array = fc_malloc(INITIAL_ARRAY_LENGTH
					    * sizeof(struct map_position));
    waypoint_list = fc_malloc(INITIAL_WAYPOINT_LENGTH * sizeof(struct waypoint));
  }

  if (is_init) {
    for (x_itr = 0; x_itr < old_xsize; x_itr++) {
      free(goto_map.move_cost[x_itr]);
      free(goto_map.vector[x_itr]);
      free(goto_map.drawn[x_itr]);
    }
    free(goto_map.move_cost);
    free(goto_map.vector);
    free(goto_map.drawn);
  }

  goto_map.move_cost = fc_malloc(map.xsize * sizeof(short *));
  goto_map.vector = fc_malloc(map.xsize * sizeof(char *));
  goto_map.drawn = fc_malloc(map.xsize * sizeof(char *));
  for (x_itr = 0; x_itr < map.xsize; x_itr++) {
    goto_map.move_cost[x_itr] = fc_malloc(map.ysize * sizeof(short));
    goto_map.vector[x_itr] = fc_malloc(map.ysize * sizeof(char));
    goto_map.drawn[x_itr] = fc_malloc(map.ysize * sizeof(char) * 4);
  }
  goto_map.unit_id = -1;
  goto_map.src_x = -1;
  goto_map.src_y = -1;
  whole_map_iterate(x, y) {
    int dir;
    for (dir=0; dir<4; dir++)
      *(goto_map.drawn[x] + y * 4 + dir) = 0;
  } whole_map_iterate_end;
  initialize_move_costs();

  is_init = 1;
  old_xsize = map.xsize;
}

/********************************************************************** 
Called once per goto; resets the goto map.
***********************************************************************/
static void init_goto_map(struct unit *punit, int src_x, int src_y)
{
  goto_map.unit_id = punit->id;
  whole_map_iterate(x, y) {
    goto_map.move_cost[x][y] = MAXCOST;
    goto_map.vector[x][y] = 0;
  } whole_map_iterate_end;
  goto_map.src_x = src_x;
  goto_map.src_y = src_y;
  goto_map.move_cost[src_x][src_y] = 0;
}

/**************************************************************************
Can we move between for ZOC? (only for land units).
**************************************************************************/
static int goto_zoc_ok(struct unit *punit, int src_x, int src_y,
		       int dest_x, int dest_y)
{
  if (unit_flag(punit, F_IGZOC))
    return 1;
  if (is_allied_unit_tile(map_get_tile(dest_x, dest_y), unit_owner(punit)))
    return 1;
  if (map_get_city(src_x, src_y) || map_get_city(dest_x, dest_y))
    return 1;
  if (map_get_terrain(src_x,src_y)==T_OCEAN || map_get_terrain(dest_x,dest_y)==T_OCEAN)
    return 1;
  return is_my_zoc(unit_owner(punit), src_x, src_y)
      || is_my_zoc(unit_owner(punit), dest_x, dest_y);
}

/********************************************************************** 
fills out the goto_map with move costs and vectors as to how we get
there. Somewhat similar to the one in server/gotohand.c

Note that a tile currently have max 1 vector pointing to it, as opposed
to marking all routes of the lowest cost as it is done in
find_the_shortest_path().
This function currently only takes move cost into account, not how
exposed the unit will be while it moves. There are 2 ways of adding
this:
-Mark all routes with optimal move cost and choose between them
 afterwards, as done in find_the_shortest_path(). This has the
 disadvantage that we cannot take a slightly longer way to avoid the
 enemy.
-Factor the thread to a unit at a given tile into the tile move cost.
 This is the best way IMO, as it doesn't have the problem mentioned
 above. We will probably have to make movecost an int instead of a short
 to make it possible to differentiate finely enough between the tiles.
***********************************************************************/
static void create_goto_map(struct unit *punit, int src_x, int src_y,
			    enum goto_move_restriction restriction)
{
  int x, y;
  struct tile *psrctile, *pdesttile;
  enum unit_move_type move_type = unit_type(punit)->move_type;
  int move_cost, total_cost;
  int igter = unit_flag(punit, F_IGTER);
  int add_to_queue;

  init_queue();
  init_goto_map(punit, src_x, src_y);

  add_to_mapqueue(0, src_x, src_y);

  while (get_from_mapqueue(&x, &y)) { /* until all accesible is marked */
    psrctile = map_get_tile(x, y);

    /* Try to move to all tiles adjacent to x,y. The coordinats of the
       tile we try to move to are x1,y1 */
    adjc_dir_iterate(x, y, x1, y1, dir) {
      if ((restriction == GOTO_MOVE_CARDINAL_ONLY)
	  && !DIR_IS_CARDINAL(dir))
	continue;

      pdesttile = map_get_tile(x1, y1);
      add_to_queue = 1;

      switch (move_type) {
      case LAND_MOVING:
	if (goto_map.move_cost[x1][y1] <= goto_map.move_cost[x][y])
	  continue; /* No need for all the calculations. Note that this also excludes
		       RR loops, ie you can't create a cycle with the same move_cost */

	if (pdesttile->terrain == T_OCEAN) {
	  if (ground_unit_transporter_capacity(x1, y1, unit_owner(punit))
	      <= 0)
	    continue;
	  else
	    move_cost = SINGLE_MOVE;
	} else if (psrctile->terrain == T_OCEAN) {
	  int base_cost = get_tile_type(pdesttile->terrain)->movement_cost * SINGLE_MOVE;
	  move_cost =
	      igter ? MOVE_COST_ROAD : MIN(base_cost,
					   unit_type(punit)->move_rate);
	  if (src_x != x || src_y != y) {
	    /* Attempting to make a path through a sea transporter */
	    move_cost += MOVE_COST_ROAD; /* Rather arbitrary deterrent */
	  }
	} else if (igter) {
	  move_cost = (psrctile->move_cost[dir] ? MOVE_COST_ROAD : 0);
	} else {
	  move_cost =
	      MIN(psrctile->move_cost[dir], unit_type(punit)->move_rate);
	}

	if (pdesttile->terrain == T_UNKNOWN) {
	  /* Don't go into the unknown. * 3 is an arbitrary deterrent. */
	  move_cost = (restriction == GOTO_MOVE_STRAIGHTEST) ? SINGLE_MOVE : 3 * SINGLE_MOVE;
	} else if (is_non_allied_unit_tile(pdesttile, unit_owner(punit))) {
	  if (psrctile->terrain == T_OCEAN && !unit_flag(punit, F_MARINES)) {
	    continue; /* Attempting to attack from a ship */
	  } else {
	    add_to_queue = 0;
	    move_cost = SINGLE_MOVE;
	  }
	} else if (is_non_allied_city_tile(pdesttile, unit_owner(punit))) {
	  if (psrctile->terrain == T_OCEAN && !unit_flag(punit, F_MARINES)) {
	    continue; /* Attempting to attack from a ship */
	  } else {
	    add_to_queue = 0;
	  }
	} else if (!goto_zoc_ok(punit, x, y, x1, y1))
	  continue;

	/* Add the route to our warmap if it is worth keeping */
	total_cost = move_cost + goto_map.move_cost[x][y];
	if (goto_map.move_cost[x1][y1] > total_cost) {
	  goto_map.move_cost[x1][y1] = total_cost;
	  if (add_to_queue)
	    add_to_mapqueue(total_cost, x1, y1);
	  goto_map.vector[x1][y1] = 1 << DIR_REVERSE(dir);
	  freelog(LOG_DEBUG,
		  "Candidate: %s from (%d, %d) to (%d, %d), cost %d",
		  dir_get_name(dir), x, y, x1, y1, total_cost);
	}
	break;

      case SEA_MOVING:
	if (goto_map.move_cost[x1][y1] <= goto_map.move_cost[x][y])
	  continue; /* No need for all the calculations */

	if (pdesttile->terrain == T_UNKNOWN) {
	  move_cost = 2*SINGLE_MOVE; /* arbitrary */
	} else if (is_non_allied_unit_tile(pdesttile, unit_owner(punit))
		   || is_non_allied_city_tile(pdesttile, unit_owner(punit))) {
	  add_to_queue = 0;
	  move_cost = SINGLE_MOVE;
	} else if (psrctile->move_cost[dir] != MOVE_COST_FOR_VALID_SEA_STEP) {
	  continue;
	} else if (unit_flag(punit, F_TRIREME) 
		   && trireme_loss_pct(unit_owner(punit), x1, y1) > 0) {
	  move_cost = 2*SINGLE_MOVE+1;
	} else {
	  move_cost = SINGLE_MOVE;
	}

	total_cost = move_cost + goto_map.move_cost[x][y];

	/* Add the route to our warmap if it is worth keeping */
	if (goto_map.move_cost[x1][y1] > total_cost) {
	  goto_map.move_cost[x1][y1] = total_cost;
	  if (add_to_queue)
	    add_to_mapqueue(total_cost, x1, y1);
	  goto_map.vector[x1][y1] = 1 << DIR_REVERSE(dir);
	  freelog(LOG_DEBUG,
		  "Candidate: %s from (%d, %d) to (%d, %d), cost %d",
		  dir_get_name(dir), x, y, x1, y1, total_cost);
	}
	break;

      case AIR_MOVING:
      case HELI_MOVING:
	if (goto_map.move_cost[x1][y1] <= goto_map.move_cost[x][y])
	  continue; /* No need for all the calculations */

	move_cost = SINGLE_MOVE;
	/* Planes could run out of fuel, therefore we don't care if territory
	   is unknown. Also, don't attack except at the destination. */

	if (is_non_allied_unit_tile(pdesttile, unit_owner(punit))) {
	  add_to_queue = 0;
	}

	/* Add the route to our warmap if it is worth keeping */
	total_cost = move_cost + goto_map.move_cost[x][y];
	if (goto_map.move_cost[x1][y1] > total_cost) {
	  goto_map.move_cost[x1][y1] = total_cost;
	  if (add_to_queue)
	    add_to_mapqueue(total_cost, x1, y1);
	  goto_map.vector[x1][y1] = 1 << DIR_REVERSE(dir);
	  freelog(LOG_DEBUG,
		  "Candidate: %s from (%d, %d) to (%d, %d), cost %d",
		  dir_get_name(dir), x, y, x1, y1, total_cost);
	}
	break;

      default:
	move_cost = 0;	/* silence compiler warning */
	freelog(LOG_FATAL, "Bad move_type in create_goto_map().");
	abort();
	break;
      } /****** end switch ******/
    } adjc_dir_iterate_end;
  } /* end while */
}

/**************************************************************************
Insert a point and draw the line on the map.
Will extend the array if needed.
**************************************************************************/
static void goto_array_insert(int x, int y)
{
  int dir, old_x, old_y;

  /* too small; alloc a bigger one */
  if (goto_array_index == goto_array_length) {
    goto_array_length *= 2;
    goto_array =
      fc_realloc(goto_array, goto_array_length * sizeof(struct map_position));
  }

  /* draw onto map */
  if (goto_array_index == 0) {
    old_x = waypoint_list[0].x;
    old_y = waypoint_list[0].y;
  } else {
    old_x = goto_array[goto_array_index-1].x;
    old_y = goto_array[goto_array_index-1].y;
  }

  /* 
   * TODO: if true, the code below breaks badly. goto_array_index was
   * 0 and the waypoint had our current position, which doesn't seem
   * too unreasonable.
   */
  assert(!(old_x == x && old_y == y));

  dir = get_direction_for_step(old_x, old_y, x, y);

  draw_segment(old_x, old_y, dir);

  /* insert into array */
  goto_array[goto_array_index].x = x;
  goto_array[goto_array_index].y = y;
  goto_array_index++;
}

/********************************************************************** 
...
***********************************************************************/
static void insert_waypoint(int x, int y)
{
  struct waypoint *pwaypoint;

  /* too small; alloc a bigger one */
  if (waypoint_list_index == waypoint_list_length) {
    waypoint_list_length *= 2;
    waypoint_list =
      fc_realloc(waypoint_list, waypoint_list_length * sizeof(struct waypoint));
  }

  pwaypoint = &waypoint_list[waypoint_list_index];
  pwaypoint->x = x;
  pwaypoint->y = y;
  pwaypoint->goto_array_start = goto_array_index;
  waypoint_list_index++;
}

/********************************************************************** 
Inserts a waypoint at the end of the current goto line.
***********************************************************************/
void goto_add_waypoint(void)
{
  int x, y;
  struct unit *punit = find_unit_by_id(goto_map.unit_id);
  assert(is_active);
  assert(punit && punit == get_unit_in_focus());
  get_line_dest(&x, &y);
  create_goto_map(punit, x, y, GOTO_MOVE_ANY);
  insert_waypoint(x, y);
}


/********************************************************************** 
Returns whether there were any waypoint popped (we don't remove the
initial position)
***********************************************************************/
int goto_pop_waypoint(void)
{
  int dest_x, dest_y, new_way_x, new_way_y;
  struct unit *punit = find_unit_by_id(goto_map.unit_id);
  assert(is_active);
  assert(punit && punit == get_unit_in_focus());

  if (waypoint_list_index == 1)
    return 0; /* we don't have any waypoint but the start pos. */
  
  new_way_x = waypoint_list[waypoint_list_index-2].x;
  new_way_y = waypoint_list[waypoint_list_index-2].y;
  get_line_dest(&dest_x, &dest_y); /* current dest. */

  undraw_line(); /* line from deleted waypoint to current dest */
  waypoint_list_index--; /* delete waypoint */
  undraw_line(); /* line from next waypoint to deleted waypoint */
  create_goto_map(punit, new_way_x, new_way_y, GOTO_MOVE_ANY);
  draw_line(dest_x, dest_y); /* line from next waypoint to current dest */

  return 1;
}

/********************************************************************** 
...
***********************************************************************/
void enter_goto_state(struct unit *punit)
{
  assert(!is_active);
  create_goto_map(punit, punit->x, punit->y, GOTO_MOVE_ANY);
  goto_array_index = 0;
  waypoint_list_index = 0;
  insert_waypoint(punit->x, punit->y);
  is_active = 1;
}

/********************************************************************** 
...
***********************************************************************/
void exit_goto_state(void)
{
  if (!is_active)
    return;

  while (waypoint_list_index != 1 || goto_array_index != 0) {
    undraw_line();
    if (waypoint_list_index > 1)
      waypoint_list_index--;
  }

  is_active = 0;
}

/********************************************************************** 
...
***********************************************************************/
int goto_is_active(void)
{
  return is_active;
}

/********************************************************************** 
...
***********************************************************************/
void get_line_dest(int *x, int *y)
{
  assert(is_active);

  if (goto_array_index) {
    *x = goto_array[goto_array_index-1].x;
    *y = goto_array[goto_array_index-1].y;
  } else {
    *x = goto_map.src_x;
    *y = goto_map.src_y;
  }
}



/********************************************************************** 
Every line segment has 2 ends; we only keep track of it at one end (the
one from which dir i <4).
This function returns pointer to the correct char.
***********************************************************************/
static unsigned char *get_drawn_char(int x, int y, int dir)
{
  int x1, y1, is_real;

  /* Replace with check for is_normal_tile later */  
  assert(is_real_tile(x, y));
  normalize_map_pos(&x, &y);

  is_real = MAPSTEP(x1, y1, x, y, dir);

  /* It makes no sense to draw a goto line to a non-existant tile. */
  assert(is_real);

  if (dir >= 4) {
    x = x1;
    y = y1;
    dir = DIR_REVERSE(dir);
  }

  return goto_map.drawn[x] + y*4 + dir;
}

/********************************************************************** 
...
***********************************************************************/
void increment_drawn(int x, int y, int dir)
{
  /* don't overflow unsigned char. */
  assert(*get_drawn_char(x, y, dir) < 255);
  *get_drawn_char(x, y, dir) += 1;
}

/********************************************************************** 
...
***********************************************************************/
void decrement_drawn(int x, int y, int dir)
{
  assert(*get_drawn_char(x, y, dir) > 0);
  *get_drawn_char(x, y, dir) -= 1;
}

/********************************************************************** 
...
***********************************************************************/
int get_drawn(int x, int y, int dir)
{
  int dummy_x, dummy_y;

  if (!MAPSTEP(dummy_x, dummy_y, x, y, dir))
    return 0;

  return *get_drawn_char(x, y, dir);
}


/********************************************************************** 
Pop one tile and undraw it from the map.
Not many checks here.
***********************************************************************/
static void undraw_one(void)
{
  int line_x, line_y;
  int dir;

  /* current line destination */
  int dest_x = goto_array[goto_array_index-1].x;
  int dest_y = goto_array[goto_array_index-1].y;
  struct waypoint *pwaypoint = &waypoint_list[waypoint_list_index-1];

  /* find the new line destination */
  if (goto_array_index > pwaypoint->goto_array_start + 1) {
    line_x = goto_array[goto_array_index-2].x;
    line_y = goto_array[goto_array_index-2].y;
  } else {
    line_x = pwaypoint->x;
    line_y = pwaypoint->y;
  }

  /* undraw the line segment */

  dir = get_direction_for_step(line_x, line_y, dest_x, dest_y);
  undraw_segment(line_x, line_y, dir);

  assert(goto_array_index > 0);
  goto_array_index--;
}

/********************************************************************** 
Like, removes the line drawn with draw_line().
If the route contains multiple waypoints we will delete down to the
next one and then return.

Works by recursively deleting one tile at a time from the route.
***********************************************************************/
static void undraw_line(void)
{
  struct waypoint *pwaypoint;

  if (!is_active)
    return;

  assert(waypoint_list_index);
  pwaypoint = &waypoint_list[waypoint_list_index-1]; /* active source */

  /* If we are down at a waypoint and we have two waypoints on top
     of each other Just pop a waypoint. */
  if (goto_array_index == pwaypoint->goto_array_start) {
    return;
  }

  if (goto_array_index == 0)
    return;

  while (goto_array_index > pwaypoint->goto_array_start) {
    undraw_one();
  }
}

#define INITIAL_ROUTE_LENGTH 30
struct map_position *route = NULL;
int route_length = INITIAL_ROUTE_LENGTH;
int route_index = 0;
/********************************************************************** 
Return what index the first element in the new route should be inserted.
ie, we don't want to first undraw a lot of a route and then redraw it,
so we find the common part.
***********************************************************************/
static int find_route(int x, int y)
{
  int last_x, last_y;
  int i, first_index;

  if (route == NULL) {
    route = fc_malloc(route_length * sizeof(struct map_position));
  }

  last_x = waypoint_list[waypoint_list_index-1].x;
  last_y = waypoint_list[waypoint_list_index-1].y;

  first_index = waypoint_list[waypoint_list_index-1].goto_array_start;

  if (x == last_x && y == last_y)
    return first_index;

  /* Try to see of we can find this position in the goto array */
  for (i = goto_array_index - 1; i >= first_index; i--) {
    if (x == goto_array[i].x && y == goto_array[i].y) {
      return i+1; /* found common point */
    }
  }

  adjc_dir_iterate(x, y, new_x, new_y, dir) {
    if (goto_map.vector[x][y] & (1<<dir)) {
      /* expand array as neccesary */
      if (route_index == route_length) {
	route_length *= 2;
	route = fc_realloc(route, route_length * sizeof(struct map_position));
      }

      route[route_index].x = x;
      route[route_index].y = y;
      route_index++;
      return find_route(new_x, new_y); /* how about recoding freeciv in MosML? */
    }
  } adjc_dir_iterate_end;

  assert(0); /* should find direction... */
  return -1; /* why can't the compiler figure out that create_goto_map()
		will newer create a vector that leads to a pos without a
		vector without that pos being the source? :P */
}

/********************************************************************** 
Puts a line to dest_x, dest_y on the map according to the current
goto_map.
If there is no route to the dest then don't draw anything.
***********************************************************************/
void draw_line(int dest_x, int dest_y)
{
  int start_index;

  assert(is_active);

  /* Replace with check for is_normal_tile later */
  assert(is_real_tile(dest_x, dest_y));
  normalize_map_pos(&dest_x, &dest_y);

  if (!goto_map.vector[dest_x][dest_y]) {
    undraw_line();
    return;
  }

  /* puts the route into "route" */
  start_index = find_route(dest_x, dest_y);

  /* undraw the part of the route we cannot reuse. */
  while (goto_array_index > start_index)
    undraw_one();

  while (route_index > 0) {
    goto_array_insert(route[route_index-1].x, route[route_index-1].y);
    route_index--;
  }
}



/********************************************************************** 
...
***********************************************************************/
void send_patrol_route(struct unit *punit)
{
  struct packet_goto_route p;
  int i, j;
  p.unit_id = punit->id;
  p.length = goto_array_index * 2 + 1;
  p.first_index = 0;
  p.last_index = goto_array_index * 2;
  p.pos = fc_malloc((p.length) * sizeof(struct map_position));
  j = 0;
  for (i = 0; i < goto_array_index; i++) {
    p.pos[j++] = goto_array[i];
  }
  for (i = goto_array_index-2; i >= 0; i--) {
    p.pos[j++] = goto_array[i];
  }
  p.pos[j].x = punit->x;
  p.pos[j].y = punit->y;
  send_packet_goto_route(&aconnection, &p, ROUTE_PATROL);
  free(p.pos);
}

/********************************************************************** 
...
***********************************************************************/
void send_goto_route(struct unit *punit)
{
  struct packet_goto_route p;
  int i;
  p.unit_id = punit->id;
  p.length = goto_array_index + 1;
  p.first_index = 0;
  p.last_index = goto_array_index;
  p.pos = fc_malloc((p.length) * sizeof(struct map_position));
  for (i = 0; i < goto_array_index; i++) {
    p.pos[i] = goto_array[i];
  }
  send_packet_goto_route(&aconnection, &p, ROUTE_GOTO);
  free(p.pos);
}
