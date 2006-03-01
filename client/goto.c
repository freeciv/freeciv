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
#include <string.h>

#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "pf_tools.h"
#include "unit.h"

#include "civclient.h"
#include "clinet.h"
#include "control.h"
#include "mapview_g.h"

#include "goto.h"

#define PATH_LOG_LEVEL          LOG_DEBUG
#define PACKET_LOG_LEVEL        LOG_DEBUG

/*
 * The whole path is seperated by waypoints into parts. The number of parts is
 * number of waypoints + 1.  Each part has its own starting position and 
 * therefore requires it's own map.
 */
struct part {
  int start_moves_left;
  struct tile *start_tile, *end_tile;
  int end_moves_left;
  int time;
  struct pf_path *path;
  struct pf_map *map;
};

static struct {
  /* For each tile and each direction we store the number of lines going out 
   * of the tile in this direction.  Since each line is undirected, we only 
   * store the 4 lower-numbered directions for each tile; the 4 upper-numbered
   * directions are stored as reverses from the target tile.
   * Notes: 1. This assumes that 
   * - there are 8 directions
   * - out of every two opposite directions (like NORTH and SOUTH) one and 
   *   only one has number less than 4
   * 2. There _can_ be more than one line drawn between two tiles, because of 
   * the waypoints. */
  struct {
    unsigned char drawn[4];
  } *tiles;
  int unit_id;                  /* The unit of the goto map */
  struct part *parts;
  int num_parts;
  struct pf_parameter template;
} goto_map;

#define DRAWN(ptile, dir) (goto_map.tiles[(ptile)->index].drawn[dir])

static void increment_drawn(struct tile *src_tile, enum direction8 dir);
static void decrement_drawn(struct tile *src_tile, enum direction8 dir);
static void reset_last_part(void);


/**************************************************************************
  Various stuff for the goto routes
**************************************************************************/
static bool is_active = FALSE;
static bool is_init = FALSE;
static int connect_initial;

/********************************************************************** 
  Called once per game.
***********************************************************************/
void init_client_goto(void)
{
  if (is_init) {
    free_client_goto();
  }

  goto_map.tiles = fc_malloc(map.xsize * map.ysize
                             * sizeof(*goto_map.tiles));
  goto_map.parts = NULL;
  goto_map.num_parts = 0;
  goto_map.unit_id = -1;
  whole_map_iterate(ptile) {
    int dir;
    for (dir = 0; dir < 4; dir++) {
      DRAWN(ptile, dir) = 0;
    }
  } whole_map_iterate_end;
  initialize_move_costs();

  is_init = TRUE;
}

/********************************************************************** 
  Deallocate goto structures.
***********************************************************************/
void free_client_goto(void)
{
  if (is_init) {
    free(goto_map.tiles);
    if (goto_map.parts) {
      free(goto_map.parts);
    }

    memset(&goto_map, 0, sizeof(goto_map));

    is_init = FALSE;
  }
}

/********************************************************************** 
  Change the destination of the last part to the given position if a
  path can be found. If not the destination is set to the start.
***********************************************************************/
static void update_last_part(struct tile *ptile)
{
  struct part *p = &goto_map.parts[goto_map.num_parts - 1];
  struct pf_path *new_path;
  struct tile *old_tile = p->start_tile;
  int i, start_index = 0;

  freelog(LOG_DEBUG, "update_last_part(%d,%d) old (%d,%d)-(%d,%d)",
          TILE_XY(ptile), TILE_XY(p->start_tile), TILE_XY(p->end_tile));
  new_path = pf_get_path(p->map, ptile);

  if (!new_path) {
    freelog(PATH_LOG_LEVEL, "  no path found");
    reset_last_part();
    return;
  }

  freelog(PATH_LOG_LEVEL, "  path found:");
  pf_print_path(PATH_LOG_LEVEL, new_path);

  if (p->path) {
    /* We had a path drawn already.  Determine how much of it we can reuse
     * in drawing the new path. */
    for (i = 0; i < MIN(new_path->length, p->path->length) - 1; i++) {
      struct pf_position *a = &p->path->positions[i];
      struct pf_position *b = &new_path->positions[i];

      if (a->dir_to_next_pos != b->dir_to_next_pos
          || !same_pos(a->tile, b->tile)) {
        break;
      }
    }
    start_index = i;

    /* Erase everything we cannot reuse */
    for (; i < p->path->length - 1; i++) {
      struct pf_position *a = &p->path->positions[i];

      if (is_valid_dir(a->dir_to_next_pos)) {
	decrement_drawn(a->tile, a->dir_to_next_pos);
      } else {
	assert(i < p->path->length - 1
	       && a->tile == p->path->positions[i + 1].tile);
      }
    }
    pf_destroy_path(p->path);
    p->path = NULL;

    old_tile = p->end_tile;
  }

  /* Draw the new path */
  for (i = start_index; i < new_path->length - 1; i++) {
    struct pf_position *a = &new_path->positions[i];

    if (is_valid_dir(a->dir_to_next_pos)) {
      increment_drawn(a->tile, a->dir_to_next_pos);
    } else {
      assert(i < new_path->length - 1
	     && a->tile == new_path->positions[i + 1].tile);
    }
  }
  p->path = new_path;
  p->end_tile = ptile;
  p->end_moves_left = pf_last_position(p->path)->moves_left;

  if (hover_state == HOVER_CONNECT) {
    int move_rate = goto_map.template.move_rate;
    int moves = pf_last_position(p->path)->total_MC;

    p->time = moves / move_rate;
    if (connect_initial > 0) {
      p->time += connect_initial;
    }
    freelog(PATH_LOG_LEVEL, "To (%d,%d) MC: %d, connect_initial: %d",
            TILE_XY(ptile), moves, connect_initial);

  } else {
    p->time = pf_last_position(p->path)->turn;
  }

  /* Refresh tiles so turn information is shown. */
  refresh_tile_mapcanvas(old_tile, FALSE);
  refresh_tile_mapcanvas(ptile, FALSE);
}

/********************************************************************** 
  Change the drawn path to a size of 0 steps by setting it to the
  start position.
***********************************************************************/
static void reset_last_part(void)
{
  struct part *p = &goto_map.parts[goto_map.num_parts - 1];

  if (!same_pos(p->start_tile, p->end_tile)) {
    /* Otherwise no need to update */
    update_last_part(p->start_tile);
  }
}

/********************************************************************** 
  Add a part. Depending on the num of already existing parts the start
  of the new part is either the unit position (for the first part) or
  the destination of the last part (not the first part).
***********************************************************************/
static void add_part(void)
{
  struct part *p;
  struct pf_parameter parameter = goto_map.template;

  goto_map.num_parts++;
  goto_map.parts =
      fc_realloc(goto_map.parts,
                 goto_map.num_parts * sizeof(*goto_map.parts));
  p = &goto_map.parts[goto_map.num_parts - 1];

  if (goto_map.num_parts == 1) {
    /* first part */
    struct unit *punit = find_unit_by_id(goto_map.unit_id);

    p->start_tile = punit->tile;
    p->start_moves_left = punit->moves_left;
  } else {
    struct part *prev = &goto_map.parts[goto_map.num_parts - 2];

    p->start_tile = prev->end_tile;
    p->start_moves_left = prev->end_moves_left;
    parameter.moves_left_initially = p->start_moves_left;
  }
  p->path = NULL;
  p->end_tile = p->start_tile;
  p->time = 0;
  parameter.start_tile = p->start_tile;
  parameter.moves_left_initially = p->start_moves_left;
  p->map = pf_create_map(&parameter);
}

/********************************************************************** 
  Remove the last part, erasing the corresponding path segment.
***********************************************************************/
static void remove_last_part(void)
{
  struct part *p = &goto_map.parts[goto_map.num_parts - 1];

  assert(goto_map.num_parts >= 1);

  reset_last_part();
  if (p->path) {
    /* We do not always have a path */
    pf_destroy_path(p->path);
  }
  pf_destroy_map(p->map);
  goto_map.num_parts--;
}

/********************************************************************** 
  Inserts a waypoint at the end of the current goto line.
***********************************************************************/
void goto_add_waypoint(void)
{
  struct part *p = &goto_map.parts[goto_map.num_parts - 1];

  assert(is_active);
  assert(find_unit_by_id(goto_map.unit_id)
	 && find_unit_by_id(goto_map.unit_id) == get_unit_in_focus());

  if (!same_pos(p->start_tile, p->end_tile)) {
    /* Otherwise the last part has zero length. */
    add_part();
  }
}

/********************************************************************** 
  Returns whether there were any waypoint popped (we don't remove the
  initial position)
***********************************************************************/
bool goto_pop_waypoint(void)
{
  struct part *p = &goto_map.parts[goto_map.num_parts - 1];
  struct tile *end_tile = p->end_tile;

  assert(is_active);
  assert(find_unit_by_id(goto_map.unit_id)
	 && find_unit_by_id(goto_map.unit_id) == get_unit_in_focus());

  if (goto_map.num_parts == 1) {
    /* we don't have any waypoint but the start pos. */
    return FALSE;
  }

  remove_last_part();

  /* 
   * Set the end position of the previous part (now the last) to the
   * end position of the last part (now gone). I.e. redraw a line to
   * the mouse position. 
   */
  update_last_part(end_tile);
  return TRUE;
}

/********************************************************************** 
  PF callback to get the path with the minimal number of steps (out of 
  all shortest paths).
***********************************************************************/
static int get_EC(const struct tile *ptile, enum known_type known,
		  struct pf_parameter *param)
{
  return 1;
}

/********************************************************************** 
  PF callback to prohibit going into the unknown.  Also makes sure we 
  don't plan our route through enemy city/tile.
***********************************************************************/
static enum tile_behavior get_TB_aggr(const struct tile *ptile,
				      enum known_type known,
                                      struct pf_parameter *param)
{
  if (known == TILE_UNKNOWN) {
    if (!goto_into_unknown) {
      return TB_IGNORE;
    }
  } else if (is_non_allied_unit_tile(ptile, param->owner)
	     || is_non_allied_city_tile(ptile, param->owner)) {
    /* Can attack but can't count on going through */
    return TB_DONT_LEAVE;
  }
  return TB_NORMAL;
}

/********************************************************************** 
  PF callback for caravans. Caravans doesn't go into the unknown and
  don't attack enemy units but enter enemy cities.
***********************************************************************/
static enum tile_behavior get_TB_caravan(const struct tile *ptile,
					 enum known_type known,
					 struct pf_parameter *param)
{
  if (known == TILE_UNKNOWN) {
    if (!goto_into_unknown) {
      return TB_IGNORE;
    }
  } else if (is_non_allied_city_tile(ptile, param->owner)) {
    /* F_TRADE_ROUTE units can travel to, but not through, enemy cities.
     * FIXME: F_HELP_WONDER units cannot.  */
    return TB_DONT_LEAVE;
  } else if (is_non_allied_unit_tile(ptile, param->owner)) {
    /* Note this must be below the city check. */
    return TB_IGNORE;
  }

  /* Includes empty, allied, or allied-city tiles. */
  return TB_NORMAL;
}

/****************************************************************************
  Return the number of MP needed to do the connect activity at this
  position.  A negative number means it's impossible.
****************************************************************************/
static int get_activity_time(const struct tile *ptile,
			     struct player *pplayer)
{
  struct tile_type *ttype = get_tile_type(ptile->terrain);
  int activity_mc = 0;

  assert(hover_state == HOVER_CONNECT);
  assert(terrain_control.may_road);
 
  switch (connect_activity) {
  case ACTIVITY_IRRIGATE:
    if (ttype->irrigation_time == 0) {
      return -1;
    }
    if (map_has_special(ptile, S_MINE)) {
      /* Don't overwrite mines. */
      return -1;
    }

    if (tile_has_special(ptile, S_IRRIGATION)) {
      break;
    }

    activity_mc = ttype->irrigation_time;
    break;
  case ACTIVITY_RAILROAD:
  case ACTIVITY_ROAD:
    if (!tile_has_special(ptile, S_ROAD)) {
      if (ttype->road_time == 0
	  || (tile_has_special(ptile, S_RIVER)
	      && !player_knows_techs_with_flag(pplayer, TF_BRIDGE))) {
	/* 0 means road is impossible here (??) */
	return -1;
      }
      activity_mc += ttype->road_time;
    }
    if (connect_activity == ACTIVITY_ROAD 
        || tile_has_special(ptile, S_RAILROAD)) {
      break;
    }
    activity_mc += ttype->rail_time;
    /* No break */
    break;
  default:
    die("Invalid connect activity.");
  }

  return activity_mc;
}

/****************************************************************************
  When building a road or a railroad, we don't want to go next to 
  nonallied cities
****************************************************************************/
static bool is_non_allied_city_adjacent(struct player *pplayer,
					const struct tile *ptile)
{
  adjc_iterate(ptile, tile1) {
    if (is_non_allied_city_tile(tile1, pplayer)) {
      return TRUE;
    }
  } adjc_iterate_end;
  
  return FALSE;
}

/****************************************************************************
  PF jumbo callback for the cost of a connect by road. 
  In road-connect mode we are concerned with 
  (1) the number of steps of the resulting path
  (2) (the tie-breaker) time to build the path (travel plus activity time).
  In rail-connect the priorities are reversed.

  param->data should contain the result of
  get_activity_rate(punit) / ACTIVITY_FACTOR.
****************************************************************************/
static int get_connect_road(const struct tile *src_tile, enum direction8 dir,
			    const struct tile *dest_tile,
			    int src_cost, int src_extra,
			    int *dest_cost, int *dest_extra,
			    struct pf_parameter *param)
{
  int activity_time, move_cost, moves_left;
  int total_cost, total_extra;

  if (map_get_known(dest_tile, param->owner) == TILE_UNKNOWN) {
    return -1;
  }

  activity_time = get_activity_time(dest_tile, param->owner);  
  if (activity_time < 0) {
    return -1;
  }

  move_cost = param->get_MC(src_tile, dir, dest_tile, param);
  if (move_cost == PF_IMPOSSIBLE_MC) {
    return -1;
  }

  if (is_non_allied_city_adjacent(param->owner, dest_tile)) {
    /* We don't want to build roads to enemies plus get ZoC problems */
    return -1;
  }

  /* Ok, the move is possible.  What are the costs? */

  /* Extra cost here is the final length of the road */
  total_extra = src_extra + 1;

  /* Special cases: get_MC function doesn't know that we would have built
   * a road (railroad) on src tile by that time */
  if (map_has_special(dest_tile, S_ROAD)) {
    move_cost = MOVE_COST_ROAD;
  }
  if (connect_activity == ACTIVITY_RAILROAD
      && map_has_special(dest_tile, S_RAILROAD)) {
    move_cost = MOVE_COST_RAIL;
  }

  move_cost = MIN(move_cost, param->move_rate);
  total_cost = src_cost;
  moves_left = param->move_rate - (src_cost % param->move_rate);
  if (moves_left < move_cost) {
    /* Emulating TM_WORST_TIME */
    total_cost += moves_left;
  }
  total_cost += move_cost;

  /* Now need to include the activity cost.  If we have moves left, they
   * will count as a full turn towards the activity time */
  moves_left = param->move_rate - (total_cost % param->move_rate);
  if (activity_time > 0) {
    int speed = *(int *)param->data;
    
    activity_time /= speed;
    activity_time--;
    total_cost += moves_left;
  }
  total_cost += activity_time * param->move_rate;

  /* Now we determine if we have found a better path.  When building a road,
   * we care most about the length of the result.  When building a rail, we 
   * care most about the constructions time (assuming MOVE_COST_RAIL == 0) */

  /* *dest_cost==-1 means we haven't reached dest until now */
  if (*dest_cost != -1) {
    if (connect_activity == ACTIVITY_ROAD) {
      if (total_extra > *dest_extra 
	  || (total_extra == *dest_extra && total_cost >= *dest_cost)) {
	/* No, this path is worse than what we already have */
	return -1;
      }
    } else {
      /* assert(connect_activity == ACTIVITY_RAILROAD) */
      if (total_cost > *dest_cost 
	  || (total_cost == *dest_cost && total_cost >= *dest_cost)) {
	return -1;
      }
    }
  }

  /* Ok, we found a better path! */  
  *dest_cost = total_cost;
  *dest_extra = total_extra;
  
  return (connect_activity == ACTIVITY_ROAD ? 
	  total_extra * PF_TURN_FACTOR + total_cost : 
	  total_cost * PF_TURN_FACTOR + total_extra);
}

/****************************************************************************
  PF jumbo callback for the cost of a connect by irrigation. 
  Here we are only interested in how long it will take to irrigate the path.

  param->data should contain the result of get_activity_rate(punit) / 10.
****************************************************************************/
static int get_connect_irrig(const struct tile *src_tile,
			     enum direction8 dir,
			     const struct tile *dest_tile,
                             int src_cost, int src_extra,
                             int *dest_cost, int *dest_extra,
                             struct pf_parameter *param)
{
  int activity_time, move_cost, moves_left, total_cost;

  if (map_get_known(dest_tile, param->owner) == TILE_UNKNOWN) {
    return -1;
  }

  activity_time = get_activity_time(dest_tile, param->owner);  
  if (activity_time < 0) {
    return -1;
  }

  if (!is_cardinal_dir(dir)) {
    return -1;
  }

  move_cost = param->get_MC(src_tile, dir, dest_tile, param);
  if (move_cost == PF_IMPOSSIBLE_MC) {
    return -1;
  }

  if (is_non_allied_city_adjacent(param->owner, dest_tile)) {
    /* We don't want to build irrigation for enemies plus get ZoC problems */
    return -1;
  }

  /* Ok, the move is possible.  What are the costs? */

  move_cost = MIN(move_cost, param->move_rate);
  total_cost = src_cost;
  moves_left = param->move_rate - (src_cost % param->move_rate);
  if (moves_left < move_cost) {
    /* Emulating TM_WORST_TIME */
    total_cost += moves_left;
  }
  total_cost += move_cost;

  /* Now need to include the activity cost.  If we have moves left, they
   * will count as a full turn towards the activity time */
  moves_left = param->move_rate - (total_cost % param->move_rate);
  if (activity_time > 0) {
    int speed = *(int *)param->data;
    
    activity_time /= speed;
    activity_time--;
    total_cost += moves_left;
  }
  total_cost += activity_time * param->move_rate;

  /* *dest_cost==-1 means we haven't reached dest until now */
  if (*dest_cost != -1 && total_cost > *dest_cost) {
      return -1;
  }

  /* Ok, we found a better path! */  
  *dest_cost = total_cost;
  *dest_extra = 0;
  
  return total_cost;
}

/********************************************************************** 
  PF callback to prohibit going into the unknown (conditionally).  Also
  makes sure we don't plan to attack anyone.
***********************************************************************/
static enum tile_behavior no_fights_or_unknown_goto(const struct tile *ptile,
						    enum known_type known,
						    struct pf_parameter *p)
{
  if (known == TILE_UNKNOWN && goto_into_unknown) {
    /* Special case allowing goto into the unknown. */
    return TB_NORMAL;
  }

  return no_fights_or_unknown(ptile, known, p);
}

/********************************************************************** 
  Fill the PF parameter with the correct client-goto values.
***********************************************************************/
static void fill_client_goto_parameter(struct unit *punit,
				       struct pf_parameter *parameter)
{
  static int speed;

  pft_fill_unit_parameter(parameter, punit);
  assert(parameter->get_EC == NULL);
  parameter->get_EC = get_EC;
  assert(parameter->get_TB == NULL);
  assert(parameter->get_MC != NULL);
  if (hover_state == HOVER_CONNECT) {
    if (connect_activity == ACTIVITY_IRRIGATE) {
      parameter->get_costs = get_connect_irrig;
    } else {
      parameter->get_costs = get_connect_road;
    }
    parameter->is_pos_dangerous = NULL;

    speed = get_activity_rate(punit) / ACTIVITY_FACTOR;
    parameter->data = &speed;

    /* Take into account the activity time at the origin */
    connect_initial = get_activity_time(punit->tile, 
                                        unit_owner(punit)) / speed;
    assert(connect_initial >= 0);
    if (connect_initial > 0) {
      parameter->moves_left_initially = 0;
      if (punit->moves_left == 0) {
	connect_initial++;
      }
    } /* otherwise moves_left_initially = punit->moves_left (default) */
  }

  if (is_attack_unit(punit) || is_diplomat_unit(punit)) {
    parameter->get_TB = get_TB_aggr;
  } else if (unit_flag(punit, F_TRADE_ROUTE)
	     || unit_flag(punit, F_HELP_WONDER)) {
    parameter->get_TB = get_TB_caravan;
  } else {
    parameter->get_TB = no_fights_or_unknown_goto;
  }

  /* Note that in connect mode the "time" does not correspond to any actual
   * move rate.
   *
   * FIXME: for units traveling across dangerous terrains with partial MP
   * (which is very rare) using TM_BEST_TIME could cause them to die. */
  parameter->turn_mode = TM_BEST_TIME;
  parameter->start_tile = punit->tile;

  /* Omniscience is always FALSE in the client */
  parameter->omniscience = FALSE;
}

/********************************************************************** 
  Enter the goto state: activate, prepare PF-template and add the 
  initial part.
***********************************************************************/
void enter_goto_state(struct unit *punit)
{
  assert(!is_active);

  goto_map.unit_id = punit->id;
  assert(goto_map.num_parts == 0);

  fill_client_goto_parameter(punit, &goto_map.template);

  add_part();
  is_active = TRUE;
}

/********************************************************************** 
  Tidy up and deactivate goto state.
***********************************************************************/
void exit_goto_state(void)
{
  if (!is_active) {
    return;
  }

  while (goto_map.num_parts > 0) {
    remove_last_part();
  }
  free(goto_map.parts);
  goto_map.parts = NULL;

  is_active = FALSE;
}

/********************************************************************** 
  Is goto state active?
***********************************************************************/
bool goto_is_active(void)
{
  return is_active;
}

/********************************************************************** 
  Return the current end of the drawn goto line.
***********************************************************************/
struct tile *get_line_dest(void)
{
  struct part *p = &goto_map.parts[goto_map.num_parts - 1];

  assert(is_active);

  return p->end_tile;
}

/**************************************************************************
  Return the path length (in turns).
***************************************************************************/
int get_goto_turns(void)
{
  int time = 0, i;

  for(i = 0; i < goto_map.num_parts; i++) {
    time += goto_map.parts[i].time;
  }

  return time;
}

/********************************************************************** 
  Puts a line to dest_tile on the map according to the current
  goto_map.
  If there is no route to the dest then don't draw anything.
***********************************************************************/
void draw_line(struct tile *dest_tile)
{
  assert(is_active);

  update_last_part(dest_tile);

  /* Update goto data in info label. */
  update_unit_info_label(get_unit_in_focus());
}

/****************************************************************************
  Send a packet to the server to request that the current orders be
  cleared.
****************************************************************************/
void request_orders_cleared(struct unit *punit)
{
  struct packet_unit_orders p;

  if (!can_client_issue_orders()) {
    return;
  }

  /* Clear the orders by sending an empty orders path. */
  freelog(PACKET_LOG_LEVEL, "Clearing orders for unit %d.", punit->id);
  p.unit_id = punit->id;
  p.repeat = p.vigilant = FALSE;
  p.length = 0;
  p.dest_x = punit->tile->x;
  p.dest_y = punit->tile->y;
  send_packet_unit_orders(&aconnection, &p);
}

/**************************************************************************
  Send a path as a goto or patrol route to the server.
**************************************************************************/
static void send_path_orders(struct unit *punit, struct pf_path *path,
			     bool repeat, bool vigilant,
			     enum unit_activity final_activity)
{
  struct packet_unit_orders p;
  int i;
  struct tile *old_tile;

  p.unit_id = punit->id;
  p.repeat = repeat;
  p.vigilant = vigilant;

  freelog(PACKET_LOG_LEVEL, "Orders for unit %d:", punit->id);

  /* We skip the start position. */
  p.length = path->length - 1;
  assert(p.length < MAX_LEN_ROUTE);
  old_tile = path->positions[0].tile;

  freelog(PACKET_LOG_LEVEL, "  Repeat: %d.  Vigilant: %d.  Length: %d",
	  p.repeat, p.vigilant, p.length);

  /* If the path has n positions it takes n-1 steps. */
  for (i = 0; i < path->length - 1; i++) {
    struct tile *new_tile = path->positions[i + 1].tile;

    if (same_pos(new_tile, old_tile)) {
      p.orders[i] = ORDER_FULL_MP;
      p.dir[i] = -1;
      freelog(PACKET_LOG_LEVEL, "  packet[%d] = wait: %d,%d",
	      i, TILE_XY(old_tile));
    } else {
      p.orders[i] = ORDER_MOVE;
      p.dir[i] = get_direction_for_step(old_tile, new_tile);
      p.activity[i] = ACTIVITY_LAST;
      freelog(PACKET_LOG_LEVEL, "  packet[%d] = move %s: %d,%d => %d,%d",
 	      i, dir_get_name(p.dir[i]),
	      TILE_XY(old_tile), TILE_XY(new_tile));
      p.activity[i] = ACTIVITY_LAST;
    }
    old_tile = new_tile;
  }

  if (final_activity != ACTIVITY_LAST) {
    p.orders[i] = ORDER_ACTIVITY;
    p.dir[i] = -1;
    p.activity[i] = final_activity;
    p.length++;
  }

  p.dest_x = old_tile->x;
  p.dest_y = old_tile->y;

  send_packet_unit_orders(&aconnection, &p);
}

/**************************************************************************
  Send an arbitrary goto path for the unit to the server.
**************************************************************************/
void send_goto_path(struct unit *punit, struct pf_path *path,
		    enum unit_activity final_activity)
{
  send_path_orders(punit, path, FALSE, FALSE, final_activity);
}

/**************************************************************************
  Send the current patrol route (i.e., the one generated via HOVER_STATE)
  to the server.
**************************************************************************/
void send_patrol_route(struct unit *punit)
{
  int i;
  struct pf_path *path = NULL, *return_path;
  struct pf_parameter parameter = goto_map.template;
  struct pf_map *map;

  assert(is_active);
  assert(punit->id == goto_map.unit_id);

  parameter.start_tile = goto_map.parts[goto_map.num_parts - 1].end_tile;
  parameter.moves_left_initially
    = goto_map.parts[goto_map.num_parts - 1].end_moves_left;
  map = pf_create_map(&parameter);
  return_path = pf_get_path(map, goto_map.parts[0].start_tile);
  if (!return_path) {
    die("No return path found!");
  }

  for (i = 0; i < goto_map.num_parts; i++) {
    path = pft_concat(path, goto_map.parts[i].path);
  }
  path = pft_concat(path, return_path);

  pf_destroy_map(map);
  pf_destroy_path(return_path);

  send_path_orders(punit, path, TRUE, TRUE, ACTIVITY_LAST);

  pf_destroy_path(path);
}

/**************************************************************************
  Send the current connect route (i.e., the one generated via HOVER_STATE)
  to the server.
**************************************************************************/
void send_connect_route(struct unit *punit, enum unit_activity activity)
{
  struct pf_path *path = NULL;
  int i;
  struct packet_unit_orders p;
  struct tile *old_tile;

  assert(is_active);
  assert(punit->id == goto_map.unit_id);

  memset(&p, 0, sizeof(p));

  for (i = 0; i < goto_map.num_parts; i++) {
    path = pft_concat(path, goto_map.parts[i].path);
  }

  p.unit_id = punit->id;
  p.repeat = FALSE;
  p.vigilant = FALSE; /* Should be TRUE? */

  p.length = 0;
  old_tile = path->positions[0].tile;

  for (i = 0; i < path->length; i++) {
    switch (activity) {
    case ACTIVITY_IRRIGATE:
      if (!map_has_special(old_tile, S_IRRIGATION)) {
	/* Assume the unit can irrigate or we wouldn't be here. */
	p.orders[p.length] = ORDER_ACTIVITY;
	p.activity[p.length] = ACTIVITY_IRRIGATE;
	p.length++;
      }
      break;
    case ACTIVITY_ROAD:
    case ACTIVITY_RAILROAD:
      if (!map_has_special(old_tile, S_ROAD)) {
	/* Assume the unit can build the road or we wouldn't be here. */
	p.orders[p.length] = ORDER_ACTIVITY;
	p.activity[p.length] = ACTIVITY_ROAD;
	p.length++;
      }
      if (activity == ACTIVITY_RAILROAD) {
	if (!map_has_special(old_tile, S_RAILROAD)) {
	  /* Assume the unit can build the rail or we wouldn't be here. */
	  p.orders[p.length] = ORDER_ACTIVITY;
	  p.activity[p.length] = ACTIVITY_RAILROAD;
	  p.length++;
	}
      }
      break;
    default:
      die("Invalid connect activity.");
      break;
    }

    if (i != path->length - 1) {
      struct tile *new_tile = path->positions[i + 1].tile;

      assert(!same_pos(new_tile, old_tile));

      p.orders[p.length] = ORDER_MOVE;
      p.dir[p.length] = get_direction_for_step(old_tile, new_tile);
      p.length++;

      old_tile = new_tile;
    }
  }

  p.dest_x = old_tile->x;
  p.dest_y = old_tile->y;

  send_packet_unit_orders(&aconnection, &p);
}

/**************************************************************************
  Send the current goto route (i.e., the one generated via
  HOVER_STATE) to the server.  The route might involve more than one
  part if waypoints were used.  FIXME: danger paths are not supported.
**************************************************************************/
void send_goto_route(struct unit *punit)
{
  struct pf_path *path = NULL;
  int i;

  assert(is_active);
  assert(punit->id == goto_map.unit_id);

  for (i = 0; i < goto_map.num_parts; i++) {
    path = pft_concat(path, goto_map.parts[i].path);
  }

  send_goto_path(punit, path, ACTIVITY_LAST);
  pf_destroy_path(path);
}

/* ================= drawn functions ============================ */

/********************************************************************** 
  Every line segment has 2 ends; we only keep track of it at one end
  (the one from which dir i <4). This function returns pointer to the
  correct char. This function is for internal use only. Use get_drawn
  when in doubt.
***********************************************************************/
static unsigned char *get_drawn_char(struct tile *ptile, enum direction8 dir)
{
  struct tile *tile1;

  tile1 = mapstep(ptile, dir);

  if (dir >= 4) {
    ptile = tile1;
    dir = DIR_REVERSE(dir);
  }

  return &DRAWN(ptile, dir);
}

/**************************************************************************
  Increments the number of segments at the location, and draws the
  segment if necessary.
**************************************************************************/
static void increment_drawn(struct tile *src_tile, enum direction8 dir)
{
  unsigned char *count = get_drawn_char(src_tile, dir);

  freelog(LOG_DEBUG, "increment_drawn(src=(%d,%d) dir=%s)",
          TILE_XY(src_tile), dir_get_name(dir));

  if (*count < 255) {
    (*count)++;
  } else {
    /* don't overflow unsigned char. */
    assert(*count < 255);
  }

  if (*count == 1) {
    draw_segment(src_tile, dir);
  }
}

/**************************************************************************
  Decrements the number of segments at the location, and clears the
  segment if necessary.
**************************************************************************/
static void decrement_drawn(struct tile *src_tile, enum direction8 dir)
{
  unsigned char *count = get_drawn_char(src_tile, dir);

  freelog(LOG_DEBUG, "decrement_drawn(src=(%d,%d) dir=%s)",
          TILE_XY(src_tile), dir_get_name(dir));

  if (*count > 0) {
    (*count)--;
  } else {
    /* don't underflow unsigned char. */
    assert(*count > 0);
  }

  if (*count == 0) {
    undraw_segment(src_tile, dir);
  }
}

/****************************************************************************
  Return TRUE if there is a line drawn from (x,y) in the given direction.
  This is used by mapview to determine whether to draw a goto line.
****************************************************************************/
bool is_drawn_line(struct tile *ptile, int dir)
{
  if (!mapstep(ptile, dir)) {
    return 0;
  }

  return (*get_drawn_char(ptile, dir) != 0);
}

/**************************************************************************
  Find the path to the nearest (fastest to reach) allied city for the
  unit, or NULL if none is reachable.
***************************************************************************/
struct pf_path *path_to_nearest_allied_city(struct unit *punit)
{
  struct city *pcity = NULL;
  struct pf_parameter parameter;
  struct pf_map *map;
  struct pf_path *path = NULL;

  if ((pcity = is_allied_city_tile(punit->tile, game.player_ptr))) {
    /* We're already on a city - don't go anywhere. */
    return NULL;
  }

  fill_client_goto_parameter(punit, &parameter);
  map = pf_create_map(&parameter);

  while (pf_next(map)) {
    struct pf_position pos;

    pf_next_get_position(map, &pos);

    if ((pcity = is_allied_city_tile(pos.tile, game.player_ptr))) {
      break;
    }
  }

  if (pcity) {
    path = pf_next_get_path(map);
  }

  pf_destroy_map(map);

  return path;
}
