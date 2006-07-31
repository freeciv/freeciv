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

#include "log.h"
#include "map.h"
#include "mem.h"
#include "pqueue.h"

#include "gotohand.h"

#include "airgoto.h"

/* These are used for airplane GOTOs with waypoints */
#define MAXFUEL 100
/* Only used in the priority function for the queue */
#define MAX_FLIGHT 256

/* Type of refuel point */
enum refuel_type {
  FUEL_START = 0, FUEL_GOAL, FUEL_CITY, FUEL_AIRBASE
};

/* Is this refuel point listed in the queue? */
enum refuel_list_status {
  RLS_NOT_YET, RLS_YES, RLS_ALREADY_NOT
};

struct refuel {
  enum refuel_type type;
  enum refuel_list_status listed;
  struct tile *tile;
  int turns;
  int moves_left;
  struct refuel *coming_from;
};

#define ALLOC_STEP 20
static struct player_refuel_list {
  struct refuel *points;
  /* Size of the actual list */
  unsigned int list_size;
  /* Size of the allocated memory in points */
  unsigned int alloc_size;
  /* It is convenient to hold additional info here */
  struct player *pplayer;
  struct unit *punit;
  int max_moves;
  int moves_per_turn;
} refuels;

static void make_list_of_refuel_points(struct player *pplayer, 
                                       bool cities_only, 
                                       int moves_per_turn, int max_moves);


/*---------------- Refuel Points C/D and access --------------------------*/
/******************************************************************
 * Access function for struct refuel
 *****************************************************************/
struct tile *get_refuel_tile(struct refuel *point)
{
  return point->tile;
}

/******************************************************************
 * Access function for struct refuel
 *****************************************************************/
unsigned int get_turns_to_refuel(struct refuel *point)
{
  return point->turns;
}

/*------------------- End of Refuel Point C/D and Access ---------------*/


/*------------------- Refuel Point List Handling -----------------------*/
/******************************************************************
 * Add new refuel point to the (static) refuel point list.
 * If the last argument start is TRUE, the point will be written at
 * the head of the list.
 *****************************************************************/
static void add_refuel_point(struct tile *ptile, 
                             enum refuel_type type, int turns, 
                             int moves_left, bool start)
{
  int pos;

  if (start) {
    pos = 0;
  } else {
    pos = refuels.list_size;
    refuels.list_size++;
  }

  if (pos +1 > refuels.alloc_size) {
    refuels.alloc_size += ALLOC_STEP;
    /* If refuels.alloc_size was zero (on the first call), 
     * then refuels.points is NULL and realloc will actually malloc */
    refuels.points = fc_realloc(refuels.points, 
                                refuels.alloc_size * sizeof(struct refuel));
    /* This memory, because refuels is static, is never freed.
     * It is just reused. */  
  }
  
  /* Fill the new point in */
  refuels.points[pos].tile = ptile;
  refuels.points[pos].type = type;
  refuels.points[pos].listed = RLS_NOT_YET;
  refuels.points[pos].turns = turns;
  refuels.points[pos].moves_left = moves_left;
  refuels.points[pos].coming_from = NULL;

  return;
}

/******************************************************************
 * Wrapper to refuel point access.  Checks range too.
 ******************************************************************/
static struct refuel *get_refuel_by_index(int i)
{
  if (i < 0 || i >= refuels.list_size) {
    return NULL;
  }

  return &refuels.points[i];
}

/*************************************************************************
This list should probably be made by having a list of airbase and then
adding the players cities. It takes a little time to iterate over the map
as it is now, but as it is only used in the players turn that does not
matter.
Can probably do some caching...
*************************************************************************/
static void make_list_of_refuel_points(struct player *pplayer, 
                                       bool cities_only, 
                                       int moves_per_turn, 
                                       int max_moves)
{
  refuels.list_size = 1;
  refuels.pplayer = pplayer;
  refuels.moves_per_turn = moves_per_turn;
  refuels.max_moves = max_moves;

  whole_map_iterate(ptile) {
    if (is_allied_city_tile(ptile, pplayer)
	&& !is_non_allied_unit_tile(ptile, pplayer) ) {
      add_refuel_point(ptile, FUEL_CITY, 
                       MAP_MAX_HEIGHT + MAP_MAX_WIDTH, 0, FALSE);
    } else if (tile_has_special(ptile, S_AIRBASE)
               && !is_non_allied_unit_tile(ptile, pplayer)
               && !cities_only) {
      add_refuel_point(ptile, FUEL_AIRBASE, 
                       MAP_MAX_HEIGHT + MAP_MAX_WIDTH, 0, FALSE);
    }
  } whole_map_iterate_end;
}

/*************************************************************************
 * Priority function for refuel points --- on the basis of their 
 * turn-distance from the starting point
 ************************************************************************/
static int queue_priority_function(const void *value)
{
  const struct refuel *point = (const struct refuel *) value;

  return -(MAX_FLIGHT * point->turns - point->moves_left);
}

/*----------------- End of Refuel Point List Handling -----------------*/


/*----------------- Refuel Point List Iterators -----------------------*/

/*************************************************************************
 * initialize air-map iterate
 * Args: (x,y) -- starting point
 * (dest_x, dest_y) -- proposed destination ( or repeat x,y if none)
 * cities_only -- will not consider airbases outside cities
 * moves_left -- moves left
 * moves_per_turn -- max moves per turn
 * max_fuel -- max fuel
 ************************************************************************/
struct pqueue *refuel_iterate_init(struct player *pplayer,
				   struct tile *ptile,
                                   struct tile *dest_tile,
                                   bool cities_only, int moves_left, 
                                   int moves_per_turn, int max_fuel)
{
  struct refuel *tmp;   
  struct pqueue *rp_queue = pq_create(MAP_MAX_WIDTH);
  short index;

  /* List of all refuel points of the player!  
   * TODO: Should cache the results */
  make_list_of_refuel_points(pplayer, cities_only, 
                             moves_per_turn, moves_per_turn * max_fuel);
  /* Add the starting point: we keep it for later backtracking */
  add_refuel_point(ptile, FUEL_START, 0, moves_left, TRUE);

  if (!same_pos(ptile, dest_tile)) {
    add_refuel_point(dest_tile, FUEL_GOAL, 
                     MAP_MAX_HEIGHT + MAP_MAX_WIDTH, 0, FALSE);
  }

  /* Process the starting point, no need to queue it
   * Note: starting position is in the head of refuels list */
  refuel_iterate_process(rp_queue, get_refuel_by_index(0));

  index = -1;
  (void) pq_peek(rp_queue, &index);
  tmp = get_refuel_by_index(index);

  if (tmp && same_pos(ptile, tmp->tile)) {
    /* We should get the starting point twice 
     * in case we start on less than full fuel */
    pq_remove(rp_queue, NULL);
    refuel_iterate_process(rp_queue, tmp);
  }

  return rp_queue;
}

/*************************************************************************
 * Get the next refuel point (wrt the turn-distance from the starting 
 * point).  Sort out the points that are already processed.
 ************************************************************************/
struct refuel *refuel_iterate_next(struct pqueue *rp_list)
{
  struct refuel *next_point;

  /* Get the next nearest point from the queue
   * (ignoring already processed ones) */
  do {
    short int index = -1;

    (void) pq_remove(rp_list, &index);

    next_point = get_refuel_by_index(index);
  } while(next_point != NULL && next_point->listed == RLS_ALREADY_NOT);


  if (next_point != NULL) {
    /* Mark as processed */
    next_point->listed = RLS_ALREADY_NOT;
  }

  return next_point;
}

/*************************************************************************
 * Process next refuel point: find all points we can reach from pfrom,
 * see if the new path is better than already existing one, add new 
 * reachable points to the priority queue.
 ************************************************************************/
void refuel_iterate_process(struct pqueue *rp_list, struct refuel *pfrom)
{
  int k;
  int max_moves 
    = (pfrom->type == FUEL_START ? pfrom->moves_left : refuels.max_moves);

  /* Iteration starts from 1, since 0 is occupied by the start point */
  for (k = 1; k < refuels.list_size; k++) {
    struct refuel *pto = get_refuel_by_index(k);
    int moves_left 
      = air_can_move_between(max_moves, 
                             pfrom->tile, pto->tile, 
                             refuels.pplayer);
    if (moves_left != -1) {
      int moves_used = max_moves - moves_left;
      /* Turns used on this flight (for units with fuel > 1) */
      int turns_used 
        = 1 + ((moves_used == 0 ? 0 : moves_used - 1) 
               / refuels.moves_per_turn);
      /* Total turns to get from the start to the pto refuelling point */
      int total_turns = pfrom->turns + turns_used;

      freelog(LOG_DEBUG, "Considering: (%i,%i)->(%i,%i), in (%d %d)",
              pfrom->tile->x, pfrom->tile->y, pto->tile->x, pto->tile->y, 
              total_turns, moves_left);
      freelog(LOG_DEBUG, "\t\t compared to (%d %d)", 
              pto->turns, pto->moves_left);

      if ( (pto->turns > total_turns) 
           || ((pto->turns == total_turns) 
               && (moves_left > pto->moves_left)) ) {
        /* Found a new refuelling point or at least a new route */
        if (pto->listed == RLS_ALREADY_NOT) {
          freelog(LOG_ERROR, "Found a shorter route to a node: (%i,%i)", 
                  pto->tile->x, pto->tile->y);
          assert(FALSE);
        }
        /* Update the info on pto */
        pto->coming_from = pfrom;
        pto->moves_left = moves_left;
        pto->turns = total_turns;
        
        /* Insert it into the queue.  No problem if it's already there */
	pq_insert(rp_list, k, queue_priority_function(pto));
        pto->listed = RLS_YES;
        
        freelog(LOG_DEBUG, "Recorded (%i,%i) from (%i,%i) in (%d %d)", 
                pto->tile->x, pto->tile->y, pfrom->tile->x, pfrom->tile->y, 
                total_turns, moves_left);
      }
    }
  }
}

/*************************************************************************
 * Clean up
 ************************************************************************/
void refuel_iterate_end(struct pqueue *rp_list)
{
  /* No need to free memory allocated for the refuels list 
   * -- it will be reused */

  /* Just destroy the queue */
  pq_destroy(rp_list);
}

/*----------------- End of Refuel Point List Iterators -------------------*/


/*----------------- Air Goto Standard Functions --------------------------*/

/************************************************************************
 * We need to find a route that our airplane can use without crashing. The
 * first waypoint of this route is returned inside the arguments dest_x and
 * dest_y. This can be the point where we start, fx when a plane with few 
 * moves left need to stay in a city to refuel.
 *
 * This is achieved by a simple use of refuel_iterate and then backtracking.
 *
 * The path chosen will be such that upon arrival the unit will have maximal 
 * moves left (among the paths that get to dest in minimal number of moves).
 *
 * Possible changes:
 * We might also make sure that fx a fighter will have enough fuel to fly back
 * to a base after reaching it's destination. This could be done by making a
 * list of goal from which the last jump on the route could be done
 * satisfactory. We should also make sure that bombers given an order to
 * attack a unit will not make the attack on it's last fuel point etc. etc.
 ***********************************************************************/
bool find_air_first_destination(struct unit *punit, struct tile **dest_tile)
{ 
  unsigned int fullmoves = unit_move_rate(punit) / SINGLE_MOVE;
  unsigned int fullfuel = unit_type(punit)->fuel;
  unsigned int moves_and_fuel_left 
    = punit->moves_left / SINGLE_MOVE + fullmoves * (punit->fuel - 1);
  struct pqueue *my_list 
    = refuel_iterate_init(unit_owner(punit), punit->tile, 
                          *dest_tile, FALSE, 
                          moves_and_fuel_left, fullmoves, fullfuel);
  struct refuel *next_point;
  bool reached_goal = FALSE;

  while((next_point = refuel_iterate_next(my_list)) != NULL) {
    freelog(LOG_DEBUG, "Next point (%d, %d), priority %d", 
            next_point->tile->x, next_point->tile->y,
            queue_priority_function(next_point));
    if (next_point -> type == FUEL_GOAL) {
      /* Found a route! */
      reached_goal = TRUE;
      break;
    }
    refuel_iterate_process(my_list, next_point);
  }

  if (reached_goal) {
    struct refuel *backtrack = next_point;
    while (backtrack->coming_from->type != FUEL_START) {
      backtrack = backtrack->coming_from;
      freelog(LOG_DEBUG, "(%i,%i) ->",
	      backtrack->tile->x, backtrack->tile->y);
    }
    freelog(LOG_DEBUG, "Found a route!");
    *dest_tile = backtrack->tile;
  } else {
    freelog(LOG_DEBUG, "Didn't find a route...");
  }
  refuel_iterate_end(my_list);

  return reached_goal;
}


/*----------------- End of Air Goto Standard Functions ------------------*/
