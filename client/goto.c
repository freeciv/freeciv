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

#include "capability.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "unit.h"

#include "control.h"
#include "mapview_g.h"

#include "goto.h"

extern struct connection aconnection;

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
  for (i = 0; i < map.xsize; i++)
    memset(goto_map.returned[i], 0, map.ysize*sizeof(char));
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
    if (goto_map.returned[*x][*y]) {
      freelog(LOG_DEBUG, "returned before. getting next");
      return get_from_mapqueue(x, y);
    } else {
      freelog(LOG_DEBUG, "got %i,%i, at cost %i", *x, *y, goto_map.move_cost[*x][*y]);
      return 1;
    }
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

  if (is_init) {
    for (x_itr = 0; x_itr < old_xsize; x_itr++) {
      free(goto_map.move_cost[x_itr]);
      free(goto_map.vector[x_itr]);
      free(goto_map.returned[x_itr]);
      free(goto_map.drawn[x_itr]);
    }
    free(goto_map.move_cost);
    free(goto_map.vector);
    free(goto_map.returned);
    free(goto_map.drawn);
  }

  goto_map.move_cost = fc_malloc(map.xsize * sizeof(short *));
  goto_map.vector = fc_malloc(map.xsize * sizeof(char *));
  goto_map.returned = fc_malloc(map.xsize * sizeof(char *));
  goto_map.drawn = fc_malloc(map.xsize * sizeof(char *));
  for (x_itr = 0; x_itr < map.xsize; x_itr++) {
    goto_map.move_cost[x_itr] = fc_malloc(map.ysize * sizeof(short));
    goto_map.vector[x_itr] = fc_malloc(map.ysize * sizeof(char));
    goto_map.returned[x_itr] = fc_malloc(map.ysize * sizeof(char));
    goto_map.drawn[x_itr] = fc_malloc(map.ysize * sizeof(char));
  }
  goto_map.unit_id = -1;
  goto_map.src_x = -1;
  goto_map.src_y = -1;
  whole_map_iterate(x, y) {
    goto_map.drawn[x][y] = 0;
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
  if (unit_flag(punit->type, F_IGZOC))
    return 1;
  if (is_allied_unit_tile(map_get_tile(dest_x, dest_y), punit->owner))
    return 1;
  if (map_get_city(src_x, src_y) || map_get_city(dest_x, dest_y))
    return 1;
  if (map_get_terrain(src_x,src_y)==T_OCEAN || map_get_terrain(dest_x,dest_y)==T_OCEAN)
    return 1;
  return is_my_zoc(punit, src_x, src_y) || is_my_zoc(punit, dest_x, dest_y);
}

/********************************************************************** 
fills out the goto_map with move costs and vectors as to how we get
there. Somewhat similar to the one in server/gotohand.c

Note that a tile currently have max 1 vector pointing to it, as opposed
to marking all routes of the lowest cost as it is done in
find_the_shortest_path().
***********************************************************************/
void create_goto_map(struct unit *punit, int src_x, int src_y,
		     enum goto_move_restriction restriction)
{
  static const char *d[] = { "NW", "N", "NE", "W", "E", "SW", "S", "SE" };
  int x, y, x1, y1, dir;
  struct tile *psrctile, *pdesttile;
  enum unit_move_type move_type = unit_types[punit->type].move_type;
  int move_cost, total_cost;
  int igter = unit_flag(punit->type, F_IGTER);
  int add_to_queue;

  init_queue();
  init_goto_map(punit, src_x, src_y);

  add_to_mapqueue(0, src_x, src_y);

  while (get_from_mapqueue(&x, &y)) { /* until all accesible is marked */
    psrctile = map_get_tile(x, y);

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
      add_to_queue = 1;

      switch (move_type) {
      case LAND_MOVING:
	if (goto_map.move_cost[x1][y1] <= goto_map.move_cost[x][y])
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
	  move_cost = (psrctile->move_cost[dir] ? 1 : 0);
	else
	  move_cost = MIN(psrctile->move_cost[dir], unit_types[punit->type].move_rate);

	if (pdesttile->terrain == T_UNKNOWN) {
	  /* Don't go into the unknown. 9 is an arbitrary deterrent. */
	  move_cost = (restriction == GOTO_MOVE_STRAIGHTEST) ? 3 : 9;
	} else if (is_non_allied_unit_tile(pdesttile, punit->owner)) {
	  add_to_queue = 0;
	  move_cost = 3;
	} else if (is_non_allied_city_tile(pdesttile, punit->owner)) {
	  add_to_queue = 0;
	  move_cost = 3;
	} else if (!goto_zoc_ok(punit, x, y, x1, y1))
	  continue;

	/* Add the route to our warmap if it is worth keeping */
	total_cost = move_cost + goto_map.move_cost[x][y];
	if (goto_map.move_cost[x1][y1] > total_cost) {
	  goto_map.move_cost[x1][y1] = total_cost;
	  if (add_to_queue)
	    add_to_mapqueue(total_cost, x1, y1);
	  goto_map.vector[x1][y1] = 128>>dir;
	  freelog(LOG_DEBUG,
		  "Candidate: %s from (%d, %d) to (%d, %d), cost %d",
		  d[dir], x, y, x1, y1, total_cost);
	}
	break;

      case SEA_MOVING:
	if (goto_map.move_cost[x1][y1] <= goto_map.move_cost[x][y])
	  continue; /* No need for all the calculations */

	if (pdesttile->terrain == T_UNKNOWN)
	  move_cost = 6; /* arbitrary */
	else if (psrctile->move_cost[dir] != -3) /* is -3 if sea units can move between */
	  continue;
	else if (unit_flag(punit->type, F_TRIREME) && !is_coastline(x1, y1))
	  move_cost = 7;
	else
	  move_cost = 3;

	if (pdesttile->terrain == T_UNKNOWN) /* arbitrary deterrent. */
	  move_cost = 9;

	if (is_non_allied_unit_tile(pdesttile, punit->owner)
	    || is_non_allied_city_tile(pdesttile, punit->owner))
	  add_to_queue = 0;

	total_cost = move_cost + goto_map.move_cost[x][y];

	/* Add the route to our warmap if it is worth keeping */
	if (goto_map.move_cost[x1][y1] > total_cost) {
	  goto_map.move_cost[x1][y1] = total_cost;
	  if (add_to_queue)
	    add_to_mapqueue(total_cost, x1, y1);
	  goto_map.vector[x1][y1] = 128>>dir;
	  freelog(LOG_DEBUG,
		  "Candidate: %s from (%d, %d) to (%d, %d), cost %d",
		  d[dir], x, y, x1, y1, total_cost);
	}
	break;

      case AIR_MOVING:
      case HELI_MOVING:
	if (goto_map.move_cost[x1][y1] <= goto_map.move_cost[x][y])
	  continue; /* No need for all the calculations */

	move_cost = 3;
	/* Planes could run out of fuel, therefore we don't care if territory
	   is unknown. Also, don't attack except at the destination. */

	if (is_non_allied_unit_tile(pdesttile, punit->owner)) {
	  add_to_queue = 0;
	}

	/* Add the route to our warmap if it is worth keeping */
	total_cost = move_cost + goto_map.move_cost[x][y];
	if (goto_map.move_cost[x1][y1] > total_cost) {
	  goto_map.move_cost[x1][y1] = total_cost;
	  if (add_to_queue)
	    add_to_mapqueue(total_cost, x1, y1);
	  goto_map.vector[x1][y1] = 128>>dir;
	  freelog(LOG_DEBUG,
		  "Candidate: %s from (%d, %d) to (%d, %d), cost %d",
		  d[dir], x, y, x1, y1, total_cost);
	}
	break;

      default:
	move_cost = 0;	/* silence compiler warning */
	freelog(LOG_FATAL, "Bad move_type in find_the_shortest_path().");
	abort();
	break;
      } /****** end switch ******/
    } /* end  for (dir = 0; dir < 8; dir++) */
  } /* end while */
}

/********************************************************************** 
...
***********************************************************************/
static void goto_map_backtrace(int dest_x, int dest_y)
{
  int dir, x, y;

  if (dest_x == goto_map.src_x && dest_y == goto_map.src_y)
    return; /* job well done */

  assert(goto_map.vector[dest_x][dest_y]);

  for (dir = 0; dir < 8; dir++) {
    if (goto_map.vector[dest_x][dest_y] & (1<<dir)) {
      x = map_adjust_x(dest_x + DIR_DX[dir]);
      y = dest_y + DIR_DY[dir];
      assert(y >= 0 && y < map.ysize);
       /* note the order of calls; makes sure the positions are pushed
	  onto the stack in the desired order. */
      goto_map_backtrace(x, y);
      goto_array_insert(dest_x, dest_y);
      return;
    }
  }
  /* If things are consistent we never get here */
  abort();
}

int line_dest_x = -1;
int line_dest_y = -1;
/********************************************************************** 
Puts a line to dest_x, dest_y on the map according to the current goto_map
***********************************************************************/
void draw_line(int dest_x, int dest_y)
{
  int dir, x, y;

  if (!goto_map.vector[dest_x][dest_y])
    return;

  if (line_dest_x == -1) {
    line_dest_x = dest_x;
    line_dest_y = dest_y;
  }

  if (!has_capability("activity_patrol", aconnection.capability))
    return;

  if (dest_x == goto_map.src_x && dest_y == goto_map.src_y)
    return; /* job well done */

  for (dir = 0; dir < 8; dir++) {
    if (goto_map.vector[dest_x][dest_y] & (1<<dir)) {
      x = map_adjust_x(dest_x + DIR_DX[dir]);
      y = dest_y + DIR_DY[dir];
      assert(y >= 0 && y < map.ysize);
      draw_segment(x, y, dest_x, dest_y);
      draw_line(x, y); /* I like recursion! :) */
      return;
    }
  }
}

/********************************************************************** 
Like, removes the line drawn with draw_line().
***********************************************************************/
void undraw_line(void)
{
  int dir, x, y;

  if (line_dest_x == goto_map.src_x && line_dest_y == goto_map.src_y) {
    line_dest_x = -1;
    return; /* job well done */
  }
  if (line_dest_x == -1)
    return;

  if (!has_capability("activity_patrol", aconnection.capability))
    return;

  assert(goto_map.vector[line_dest_x][line_dest_y]);

  for (dir = 0; dir < 8; dir++) {
    if (goto_map.vector[line_dest_x][line_dest_y] & (1<<dir)) {
      x = map_adjust_x(line_dest_x + DIR_DX[dir]);
      y = line_dest_y + DIR_DY[dir];
      assert(y >= 0 && y < map.ysize);
      undraw_segment(x, y, line_dest_x, line_dest_y);
      line_dest_x = x;
      line_dest_y = y;
      undraw_line(); /* I like recursion! :) */
      return;
    }
  }
}

/**************************************************************************
Various stuff for the goto routes
**************************************************************************/
#define INITIAL_ARRAY_LENGTH 100
int goto_array_length = INITIAL_ARRAY_LENGTH;
int goto_array_index = 0; /* points to where the next element should be inserted */
struct map_position *goto_array = NULL;

void goto_array_clear(void)
{
  goto_array_index = 0;
}

/**************************************************************************
Will extend the array if needed.
**************************************************************************/
void goto_array_insert(int x, int y)
{
  if (!goto_array)
    goto_array = fc_malloc(INITIAL_ARRAY_LENGTH * sizeof(struct map_position));

  /* too small; alloc a bigger one */
  if (goto_array_index == goto_array_length) {
    goto_array_length *= 2;
    goto_array =
      fc_realloc(goto_array, goto_array_length * sizeof(struct map_position));
  }

  goto_array[goto_array_index].x = x;
  goto_array[goto_array_index].y = y;
  goto_array_index++;
}

/********************************************************************** 
...
***********************************************************************/
int transfer_route_to_stack(int dest_x, int dest_y)
{
  if (goto_map.vector[dest_x][dest_y]) {
    goto_map_backtrace(dest_x, dest_y);
    return 1;
  } else {
    return 0;
  }
}

/********************************************************************** 
For patrol; this function will have to be rewritten when waypoints are
implemented.
***********************************************************************/
int transfer_route_to_stack_circular(int dest_x, int dest_y)
{
  int index;
  if (goto_map.vector[dest_x][dest_y]) {
    goto_map_backtrace(dest_x, dest_y);
    for (index = goto_array_index-2; index >= 0; index--)
      goto_array_insert(goto_array[index].x, goto_array[index].y);
    goto_array_insert(goto_map.src_x, goto_map.src_y);
    return 1;
  } else {
    return 0;
  }
}

/**************************************************************************
...
**************************************************************************/
void goto_array_send(struct unit *punit)
{
  struct packet_goto_route p;
  int i;
  int type;
  p.unit_id = punit->id;
  p.length = goto_array_index + 1;
  p.first_index = 0;
  p.last_index = goto_array_index;
  /* This is freed by send_packet_goto_route() */
  p.pos = fc_malloc((p.length) * sizeof(struct map_position));
  for (i = 0; i < goto_array_index; i++) {
    p.pos[i] = goto_array[i];
  }

  switch (hover_state) {
  case HOVER_GOTO:
    type = ROUTE_GOTO;
    break;
  case HOVER_PATROL:
    type = ROUTE_PATROL;
    break;
  default:
    type = 0;
    abort();
  }

  send_packet_goto_route(&aconnection, &p, type);
  free(p.pos);
  goto_array_clear();
}
