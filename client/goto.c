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
  int start_x, start_y;
  int end_moves_left;
  int end_x, end_y;
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

#define DRAWN(x, y, dir) (goto_map.tiles[map_pos_to_index(x, y)].drawn[dir])

static void increment_drawn(int src_x, int src_y, enum direction8 dir);
static void decrement_drawn(int src_x, int src_y, enum direction8 dir);
static void reset_last_part(void);

/**************************************************************************
  Various stuff for the goto routes
**************************************************************************/
static bool is_active = FALSE;
static bool is_init = FALSE;

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
  whole_map_iterate(x, y) {
    int dir;
    for (dir = 0; dir < 4; dir++) {
      DRAWN(x, y, dir) = 0;
    }
  }
  whole_map_iterate_end;
  initialize_move_costs();

  is_init = TRUE;
}

/********************************************************************** 
  Deallocate goto structures.
***********************************************************************/
void free_client_goto()
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
static void update_last_part(int x, int y)
{
  struct part *p = &goto_map.parts[goto_map.num_parts - 1];
  struct pf_path *new_path;
  int i, start_index = 0;

  freelog(LOG_DEBUG, "update_last_part(%d,%d) old (%d,%d)-(%d,%d)", x, y,
          p->start_x, p->start_y, p->end_x, p->end_y);
  new_path = pf_get_path(p->map, x, y);

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
          || !same_pos(a->x, a->y, b->x, b->y)) {
        break;
      }
    }
    start_index = i;

    /* Erase everything we cannot reuse */
    for (; i < p->path->length - 1; i++) {
      struct pf_position *a = &p->path->positions[i];

      if (is_valid_dir(a->dir_to_next_pos)) {
	decrement_drawn(a->x, a->y, a->dir_to_next_pos);
      } else {
	assert(i < p->path->length - 1
	       && a->x == p->path->positions[i + 1].x
	       && a->y == p->path->positions[i + 1].y);
      }
    }
    pf_destroy_path(p->path);
    p->path = NULL;
  }

  /* Draw the new path */
  for (i = start_index; i < new_path->length - 1; i++) {
    struct pf_position *a = &new_path->positions[i];

    if (is_valid_dir(a->dir_to_next_pos)) {
      increment_drawn(a->x, a->y, a->dir_to_next_pos);
    } else {
      assert(i < new_path->length - 1
	     && a->x == new_path->positions[i + 1].x
	     && a->y == new_path->positions[i + 1].y);
    }
  }
  p->path = new_path;
  p->end_x = x;
  p->end_y = y;
  p->end_moves_left = pf_last_position(p->path)->moves_left;
}

/********************************************************************** 
  Change the drawn path to a size of 0 steps by setting it to the
  start position.
***********************************************************************/
static void reset_last_part(void)
{
  struct part *p = &goto_map.parts[goto_map.num_parts - 1];

  if (!same_pos(p->start_x, p->start_y, p->end_x, p->end_y)) {
    /* Otherwise no need to update */
    update_last_part(p->start_x, p->start_y);
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

    p->start_x = punit->x;
    p->start_y = punit->y;
    p->start_moves_left = punit->moves_left;
  } else {
    struct part *prev = &goto_map.parts[goto_map.num_parts - 2];

    p->start_x = prev->end_x;
    p->start_y = prev->end_y;
    p->start_moves_left = prev->end_moves_left;
  }
  p->path = NULL;
  p->end_x = p->start_x;
  p->end_y = p->start_y;
  parameter.start_x = p->start_x;
  parameter.start_y = p->start_y;
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

  if (!same_pos(p->start_x, p->start_y, p->end_x, p->end_y)) {
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
  int end_x = p->end_x, end_y = p->end_y;

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
  update_last_part(end_x, end_y);
  return TRUE;
}

/********************************************************************** 
  PF callback to get the path with the minimal number of steps (out of 
  all shortest paths).
***********************************************************************/
static int get_EC(int x, int y, enum known_type known,
		  struct pf_parameter *param)
{
  return 1;
}

/********************************************************************** 
  PF callback to prohibit going into the unknown.  Also makes sure we 
  don't plan our route through enemy city/tile.
***********************************************************************/
static enum tile_behavior get_TB_aggr(int x, int y, enum known_type known,
                                      struct pf_parameter *param)
{
  struct tile *ptile = map_get_tile(x, y);

  if (known == TILE_UNKNOWN) {
    return TB_IGNORE;
  }
  if (is_non_allied_unit_tile(ptile, param->owner)
      || is_non_allied_city_tile(ptile, param->owner)) {
    /* Can attack but can't count on going through */
    return TB_DONT_LEAVE;
  }
  return TB_NORMAL;
}

/********************************************************************** 
  Fill the PF parameter with the correct client-goto values.
***********************************************************************/
static void fill_client_goto_parameter(struct unit *punit,
				       struct pf_parameter *parameter)
{
  pft_fill_default_parameter(parameter);
  pft_fill_unit_parameter(parameter, punit);
  assert(parameter->get_EC == NULL);
  parameter->get_EC = get_EC;
  assert(parameter->get_TB == NULL);
  if (unit_type(punit)->attack_strength > 0 || unit_flag(punit, F_DIPLOMAT)) {
    parameter->get_TB = get_TB_aggr;
  } else {
    parameter->get_TB = no_fights_or_unknown;
  }
  parameter->turn_mode = TM_WORST_TIME;
  parameter->start_x = punit->x;
  parameter->start_y = punit->y;

  /* May be overwritten by the caller. */
  parameter->moves_left_initially = punit->moves_left;
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
void get_line_dest(int *x, int *y)
{
  struct part *p = &goto_map.parts[goto_map.num_parts - 1];

  assert(is_active);

  *x = p->end_x;
  *y = p->end_y;
}

/********************************************************************** 
  Puts a line to dest_x, dest_y on the map according to the current
  goto_map.
  If there is no route to the dest then don't draw anything.
***********************************************************************/
void draw_line(int dest_x, int dest_y)
{
  assert(is_active);

  /* FIXME: Replace with check for is_normal_tile later */
  assert(is_real_map_pos(dest_x, dest_y));
  normalize_map_pos(&dest_x, &dest_y);

  update_last_part(dest_x, dest_y);
}

/**************************************************************************
  Send an arbitrary goto path for the unit to the server.  FIXME: danger
  paths are not supported.
**************************************************************************/
void send_goto_path(struct unit *punit, struct pf_path *path)
{
  struct packet_goto_route p;
  int i;

  p.unit_id = punit->id;

  /* we skip the start position */
  /* FIXME: but for unknown reason the server discards the last position */
  p.length = path->length - 1 + 1;
  p.first_index = 0;
  p.last_index = p.length - 1;
  p.pos = fc_malloc(p.length * sizeof(*p.pos));
  for (i = 0; i < path->length - 1; i++) {
    p.pos[i].x = path->positions[i + 1].x;
    p.pos[i].y = path->positions[i + 1].y;
    freelog(PACKET_LOG_LEVEL, "  packet[%d] = (%d,%d)",
	    i, p.pos[i].x, p.pos[i].y);
  }

  send_packet_goto_route(&aconnection, &p, ROUTE_GOTO);

  free(p.pos);
}

/**************************************************************************
  Send the current patrol route (i.e., the one generated via HOVER_STATE)
  to the server.  FIXME: danger paths are not supported.
**************************************************************************/
void send_patrol_route(struct unit *punit)
{
  struct packet_goto_route p;
  int i, j = 0;
  struct pf_path *path = NULL;

  assert(is_active);
  assert(punit->id == goto_map.unit_id);

  for (i = 0; i < goto_map.num_parts; i++) {
    path = pft_concat(path, goto_map.parts[i].path);
  }

  p.unit_id = punit->id;

  /* we skip the start position */
  /* FIXME: but for unknown reason the server discards the last position */
  p.length = 2 * (path->length - 1) + 1;
  p.first_index = 0;
  p.last_index = p.length - 1;
  p.pos = fc_malloc(p.length * sizeof(struct map_position));
  j = 0;
  for (i = 1; i < path->length; i++) {
    p.pos[j].x = path->positions[i].x;
    p.pos[j].y = path->positions[i].y;
    freelog(PACKET_LOG_LEVEL, "  packet[%d] = (%d,%d)", j, p.pos[j].x,
            p.pos[j].y);
    j++;
  }
  for (i = path->length - 2; i >= 0; i--) {
    p.pos[j].x = path->positions[i].x;
    p.pos[j].y = path->positions[i].y;
    freelog(PACKET_LOG_LEVEL, "  packet[%d] = (%d,%d)", j, p.pos[j].x,
            p.pos[j].y);
    j++;
  }
  send_packet_goto_route(&aconnection, &p, ROUTE_PATROL);
  free(p.pos);
  p.pos = NULL;
  pf_destroy_path(path);
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

  send_goto_path(punit, path);
  pf_destroy_path(path);
}

/* ================= drawn functions ============================ */

/********************************************************************** 
  Every line segment has 2 ends; we only keep track of it at one end
  (the one from which dir i <4). This function returns pointer to the
  correct char. This function is for internal use only. Use get_drawn
  when in doubt.
***********************************************************************/
static unsigned char *get_drawn_char(int x, int y, enum direction8 dir)
{
  int x1, y1;
  bool is_real;

  assert(is_valid_dir(dir));

  /* FIXME: Replace with check for is_normal_tile later */
  assert(is_real_map_pos(x, y));
  normalize_map_pos(&x, &y);

  is_real = MAPSTEP(x1, y1, x, y, dir);

  /* It makes no sense to draw a goto line to a non-existent tile. */
  assert(is_real);

  if (dir >= 4) {
    x = x1;
    y = y1;
    dir = DIR_REVERSE(dir);
  }

  return &DRAWN(x, y, dir);
}

/**************************************************************************
  Increments the number of segments at the location, and draws the
  segment if necessary.
**************************************************************************/
static void increment_drawn(int src_x, int src_y, enum direction8 dir)
{
  freelog(LOG_DEBUG, "increment_drawn(src=(%d,%d) dir=%s)",
          src_x, src_y, dir_get_name(dir));
  /* don't overflow unsigned char. */
  assert(*get_drawn_char(src_x, src_y, dir) < 255);
  *get_drawn_char(src_x, src_y, dir) += 1;

  if (get_drawn(src_x, src_y, dir) == 1) {
    draw_segment(src_x, src_y, dir);
  }
}

/**************************************************************************
  Decrements the number of segments at the location, and clears the
  segment if necessary.
**************************************************************************/
static void decrement_drawn(int src_x, int src_y, enum direction8 dir)
{
  freelog(LOG_DEBUG, "decrement_drawn(src=(%d,%d) dir=%s)",
          src_x, src_y, dir_get_name(dir));
  /* don't underflow unsigned char. */
  assert(*get_drawn_char(src_x, src_y, dir) > 0);
  *get_drawn_char(src_x, src_y, dir) -= 1;

  if (get_drawn(src_x, src_y, dir) == 0) {
    undraw_segment(src_x, src_y, dir);
  }
}

/********************************************************************** 
  Part of the public interface. Needed by mapview.
***********************************************************************/
int get_drawn(int x, int y, int dir)
{
  int dummy_x, dummy_y;

  if (!MAPSTEP(dummy_x, dummy_y, x, y, dir)) {
    return 0;
  }

  return *get_drawn_char(x, y, dir);
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

  if ((pcity = is_allied_city_tile(map_get_tile(punit->x, punit->y),
				   game.player_ptr))) {
    /* We're already on a city - don't go anywhere. */
    return NULL;
  }

  fill_client_goto_parameter(punit, &parameter);
  map = pf_create_map(&parameter);

  while (pf_next(map)) {
    struct pf_position pos;

    pf_next_get_position(map, &pos);

    if ((pcity = is_allied_city_tile(map_get_tile(pos.x, pos.y),
				     game.player_ptr))) {
      break;
    }
  }

  if (pcity) {
    path = pf_next_get_path(map);
  }

  pf_destroy_map(map);

  return path;
}
