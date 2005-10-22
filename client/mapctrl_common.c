/********************************************************************** 
 Freeciv - Copyright (C) 2002 - The Freeciv Poject
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
#include <stdlib.h>		/* qsort */

#include "combat.h"
#include "fcintl.h"
#include "log.h"
#include "support.h"

#include "agents.h"
#include "chatline_common.h"
#include "cityrep_g.h"
#include "civclient.h"
#include "climisc.h"
#include "clinet.h"
#include "cma_core.h"
#include "control.h"
#include "fcintl.h"
#include "goto.h"
#include "mapctrl_g.h"
#include "mapview_g.h"
#include "options.h"
#include "tilespec.h"

#include "mapctrl_common.h"

/* Selection Rectangle */
static int rec_anchor_x, rec_anchor_y;  /* canvas coordinates for anchor */
static struct tile *rec_canvas_center_tile;
static int rec_corner_x, rec_corner_y;  /* corner to iterate from */
static int rec_w, rec_h;                /* width, heigth in pixels */

bool rbutton_down = FALSE;
bool rectangle_active = FALSE;

/* This changes the behaviour of left mouse
   button in Area Selection mode. */
bool tiles_hilited_cities = FALSE;

/* The mapcanvas clipboard */
static int clipboard = -1;
static bool clipboard_is_unit;

/* Goto with drag and drop. */
bool keyboardless_goto_button_down = FALSE;
bool keyboardless_goto_active = FALSE;
struct tile *keyboardless_goto_start_tile;

/* Update the workers for a city on the map, when the update is received */
struct city *city_workers_display = NULL;

static bool turn_done_state;
static bool is_turn_done_state_valid = FALSE;

/*************************************************************************/

static void clipboard_send_production_packet(struct city *pcity);
static void define_tiles_within_rectangle(void);

/**************************************************************************
 Called when Right Mouse Button is depressed. Record the canvas
 coordinates of the center of the tile, which may be unreal. This
 anchor is not the drawing start point, but is used to calculate
 width, height. Also record the current mapview centering.
**************************************************************************/
void anchor_selection_rectangle(int canvas_x, int canvas_y)
{
  struct tile *ptile = canvas_pos_to_nearest_tile(canvas_x, canvas_y);

  tile_to_canvas_pos(&rec_anchor_x, &rec_anchor_y, ptile);
  rec_anchor_x += NORMAL_TILE_WIDTH / 2;
  rec_anchor_y += NORMAL_TILE_HEIGHT / 2;
  /* FIXME: This may be off-by-one. */
  rec_canvas_center_tile = get_center_tile_mapcanvas();
  rec_w = rec_h = 0;
}

/**************************************************************************
 Iterate over the pixel boundaries of the rectangle and pick the tiles
 whose center falls within. Axis pixel incrementation is half tile size to
 accomodate tilesets with varying tile shapes and proportions of X/Y.

 These operations are performed on the tiles:
 -  Make tiles that contain owned cities hilited
    on the map and hilited in the City List Window.

 Later, I'll want to add unit hiliting for mass orders.       -ali
**************************************************************************/
static void define_tiles_within_rectangle(void)
{
  const int W = NORMAL_TILE_WIDTH,   half_W = W / 2;
  const int H = NORMAL_TILE_HEIGHT,  half_H = H / 2;
  const int segments_x = abs(rec_w / half_W);
  const int segments_y = abs(rec_h / half_H);

  /* Iteration direction */
  const int inc_x = (rec_w > 0 ? half_W : -half_W);
  const int inc_y = (rec_h > 0 ? half_H : -half_H);

  int x, y, x2, y2, xx, yy;

  y = rec_corner_y;
  for (yy = 0; yy <= segments_y; yy++, y += inc_y) {
    x = rec_corner_x;
    for (xx = 0; xx <= segments_x; xx++, x += inc_x) {
      struct tile *ptile;

      /*  For diamond shaped tiles, every other row is indented.
       */
      if ((yy % 2 ^ xx % 2) != 0) {
	continue;
      }

      ptile = canvas_pos_to_tile(x, y);
      if (!ptile) {
	continue;
      }

      /*  "Half-tile" indentation must match, or we'll process
       *  some tiles twice in the case of rectangular shape tiles.
       */
      tile_to_canvas_pos(&x2, &y2, ptile);

      if ((yy % 2) != 0 && ((rec_corner_x % W) ^ abs(x2 % W)) != 0) {
	continue;
      }

      /*  Tile passed all tests; process it.
       */
      if (ptile->city && ptile->city->owner == game.player_idx) {
        ptile->client.hilite = HILITE_CITY;
        tiles_hilited_cities = TRUE;
      }
    }
  }

  /* Hilite in City List Window */
  if (tiles_hilited_cities) {
    hilite_cities_from_canvas();      /* cityrep.c */
  }
}

/**************************************************************************
 Called when mouse pointer moves and rectangle is active.
**************************************************************************/
void update_selection_rectangle(int canvas_x, int canvas_y)
{
  const int W = NORMAL_TILE_WIDTH,    half_W = W / 2;
  const int H = NORMAL_TILE_HEIGHT,   half_H = H / 2;
  static struct tile *rec_tile = NULL;
  int diff_x, diff_y;
  struct tile *center_tile;
  struct tile *ptile;

  ptile = canvas_pos_to_nearest_tile(canvas_x, canvas_y);

  /*  Did mouse pointer move beyond the current tile's
   *  boundaries? Avoid macros; tile may be unreal!
   */
  if (ptile == rec_tile) {
    return;
  }
  rec_tile = ptile;

  /* Clear previous rectangle. */
  dirty_all();
  flush_dirty();

  /*  Fix canvas coords to the center of the tile.
   */
  tile_to_canvas_pos(&canvas_x, &canvas_y, ptile);
  canvas_x += half_W;
  canvas_y += half_H;

  rec_w = rec_anchor_x - canvas_x;  /* width */
  rec_h = rec_anchor_y - canvas_y;  /* height */

  /* FIXME: This may be off-by-one. */
  center_tile = get_center_tile_mapcanvas();
  map_distance_vector(&diff_x, &diff_y, center_tile, rec_canvas_center_tile);

  /*  Adjust width, height if mapview has recentered.
   */
  if (diff_x != 0 || diff_y != 0) {

    if (is_isometric) {
      rec_w += (diff_x - diff_y) * half_W;
      rec_h += (diff_x + diff_y) * half_H;

      /* Iso wrapping */
      if (abs(rec_w) > map.xsize * half_W / 2) {
        int wx = map.xsize * half_W,  wy = map.xsize * half_H;
        rec_w > 0 ? (rec_w -= wx, rec_h -= wy) : (rec_w += wx, rec_h += wy);
      }

    } else {
      rec_w += diff_x * W;
      rec_h += diff_y * H;

      /* X wrapping */
      if (abs(rec_w) > map.xsize * half_W) {
        int wx = map.xsize * W;
        rec_w > 0 ? (rec_w -= wx) : (rec_w += wx);
      }
    }
  }

  if (rec_w == 0 && rec_h == 0) {
    rectangle_active = FALSE;
    return;
  }

  /* It is currently drawn only to the screen, not backing store */
  rectangle_active = TRUE;
  draw_selection_rectangle(canvas_x, canvas_y, rec_w, rec_h);
  rec_corner_x = canvas_x;
  rec_corner_y = canvas_y;
}

/**************************************************************************
  Redraws the selection rectangle after a map flush.
**************************************************************************/
void redraw_selection_rectangle(void)
{
  if (rectangle_active) {
    draw_selection_rectangle(rec_corner_x, rec_corner_y, rec_w, rec_h);
  }
}

/**************************************************************************
...
**************************************************************************/
bool is_city_hilited(struct city *pcity)
{
  return pcity->tile->client.hilite == HILITE_CITY;
}

/**************************************************************************
 Remove hiliting from all tiles, but not from rows in the City List window.
**************************************************************************/
void cancel_tile_hiliting(void)
{
  if (tiles_hilited_cities)  {
    tiles_hilited_cities = FALSE;

    whole_map_iterate(ptile) {
      ptile->client.hilite = HILITE_NONE;
    } whole_map_iterate_end;

    update_map_canvas_visible();
  }
}

/**************************************************************************
 Action depends on whether the mouse pointer moved
 a tile between press and release.
**************************************************************************/
void release_right_button(int canvas_x, int canvas_y)
{
  if (rectangle_active) {
    define_tiles_within_rectangle();
    update_map_canvas_visible();
  } else {
    recenter_button_pressed(canvas_x, canvas_y);
  }
  rectangle_active = FALSE;
  rbutton_down = FALSE;
}

/**************************************************************************
 Left Mouse Button in Area Selection mode.
**************************************************************************/
void toggle_tile_hilite(struct tile *ptile)
{
  struct city *pcity = ptile->city;

  if (ptile->client.hilite == HILITE_CITY) {
    ptile->client.hilite = HILITE_NONE;
    if (pcity) {
      toggle_city_hilite(pcity, FALSE); /* cityrep.c */
    }
  }
  else if (pcity && pcity->owner == game.player_idx) {
    ptile->client.hilite = HILITE_CITY;
    tiles_hilited_cities = TRUE;
    toggle_city_hilite(pcity, TRUE);
  }
  else  {
    return;
  }

  refresh_tile_mapcanvas(ptile, TRUE);
}

/**************************************************************************
  The user pressed the overlay-city button (t) while the mouse was at the
  given canvas position.
**************************************************************************/
void key_city_overlay(int canvas_x, int canvas_y)
{
  struct tile *ptile = canvas_pos_to_tile(canvas_x, canvas_y);

  if (can_client_change_view() && ptile) {
    struct unit *punit;
    struct city *pcity = find_city_or_settler_near_tile(ptile, &punit);

    if (pcity) {
      toggle_city_color(pcity);
    } else if (punit) {
      toggle_unit_color(punit);
    }
  }
}

/**************************************************************************
 Shift-Left-Click on owned city or any visible unit to copy.
**************************************************************************/
void clipboard_copy_production(struct tile *ptile)
{
  char msg[MAX_LEN_MSG];
  struct city *pcity = ptile->city;

  if (pcity) {
    if (pcity->owner != game.player_idx)  {
      return;
    }
    clipboard = pcity->currently_building;
    clipboard_is_unit = pcity->is_building_unit;
  } else {
    struct unit *punit = find_visible_unit(ptile);
    if (!punit) {
      return;
    }
    if (!can_player_build_unit_direct(game.player_ptr, punit->type))  {
      my_snprintf(msg, sizeof(msg),
      _("Game: You don't know how to build %s!"),
        unit_types[punit->type].name);
      append_output_window(msg);
      return;
    }
    clipboard_is_unit = TRUE;
    clipboard = punit->type;
  }
  upgrade_canvas_clipboard();

  my_snprintf(msg, sizeof(msg), _("Game: Copy %s to clipboard."),
    clipboard_is_unit ? unit_types[clipboard].name :
    get_improvement_name(clipboard));
  append_output_window(msg);
}

/**************************************************************************
 If City tiles are hilited, paste into all those cities.
 Otherwise paste into the one city under the mouse pointer.
**************************************************************************/
void clipboard_paste_production(struct city *pcity)
{
  if (!can_client_issue_orders()) {
    return;
  }
  if (clipboard == -1) {
    append_output_window(
    _("Game: Clipboard is empty."));
    return;
  }
  if (!tiles_hilited_cities) {
    if (pcity && pcity->owner == game.player_idx) {
      clipboard_send_production_packet(pcity);
    }
    return;
  }
  else {
    connection_do_buffer(&aconnection);
    city_list_iterate(game.player_ptr->cities, pcity) {
      if (is_city_hilited(pcity)) {
        clipboard_send_production_packet(pcity);
      }
    } city_list_iterate_end;
    connection_do_unbuffer(&aconnection);
  }
}

/**************************************************************************
...
**************************************************************************/
static void clipboard_send_production_packet(struct city *pcity)
{
  cid mycid = cid_encode(clipboard_is_unit, clipboard);

  if (mycid == cid_encode_from_city(pcity)
      || !city_can_build_impr_or_unit(pcity, mycid)) {
    return;
  }

  dsend_packet_city_change(&aconnection, pcity->id, clipboard,
			   clipboard_is_unit);
}

/**************************************************************************
 A newer technology may be available for units.
 Also called from packhand.c.
**************************************************************************/
void upgrade_canvas_clipboard(void)
{
  if (clipboard_is_unit)  {
    int u = can_upgrade_unittype(game.player_ptr, clipboard);
    if (u != -1)  {
      clipboard = u;
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void release_goto_button(int canvas_x, int canvas_y)
{
  struct tile *ptile = canvas_pos_to_tile(canvas_x, canvas_y);

  if (keyboardless_goto_active && hover_state == HOVER_GOTO && ptile) {
    struct unit *punit =
        player_find_unit_by_id(game.player_ptr, hover_unit);

    do_unit_goto(ptile);
    set_hover_state(NULL, HOVER_NONE, ACTIVITY_LAST);
    update_unit_info_label(punit);
  }
  keyboardless_goto_active = FALSE;
  keyboardless_goto_button_down = FALSE;
  keyboardless_goto_start_tile = NULL;
}

/**************************************************************************
 The goto hover state is only activated when the mouse pointer moves
 beyond the tile where the button was depressed, to avoid mouse typos.
**************************************************************************/
void maybe_activate_keyboardless_goto(int canvas_x, int canvas_y)
{
  struct tile *ptile = canvas_pos_to_tile(canvas_x, canvas_y);

  if (ptile && get_unit_in_focus()
      && !same_pos(keyboardless_goto_start_tile, ptile)
      && can_client_issue_orders()) {
    keyboardless_goto_active = TRUE;
    request_unit_goto();
  }
}

/**************************************************************************
 Return TRUE iff the turn done button is enabled.
**************************************************************************/
bool get_turn_done_button_state()
{
  if (!is_turn_done_state_valid) {
    update_turn_done_button_state();
  }
  assert(is_turn_done_state_valid);

  return turn_done_state;
}

/**************************************************************************
  Scroll the mapview half a screen in the given direction.  This is a GUI
  direction; i.e., DIR8_NORTH is "up" on the mapview.
**************************************************************************/
void scroll_mapview(enum direction8 gui_dir)
{
  int gui_x = mapview_canvas.gui_x0, gui_y = mapview_canvas.gui_y0;

  if (!can_client_change_view()) {
    return;
  }

  gui_x += DIR_DX[gui_dir] * mapview_canvas.width / 2;
  gui_y += DIR_DY[gui_dir] * mapview_canvas.height / 2;
  set_mapview_origin(gui_x, gui_y);
}

/**************************************************************************
  Do some appropriate action when the "main" mouse button (usually
  left-click) is pressed.  For more sophisticated user control use (or
  write) a different xxx_button_pressed function.
**************************************************************************/
void action_button_pressed(int canvas_x, int canvas_y,
			   enum quickselect_type qtype)
{
  struct tile *ptile = canvas_pos_to_tile(canvas_x, canvas_y);

  if (can_client_change_view() && ptile) {
    /* FIXME: Some actions here will need to check can_client_issue_orders.
     * But all we can check is the lowest common requirement. */
    do_map_click(ptile, qtype);
  }
}

/**************************************************************************
  Wakeup sentried units on the tile of the specified location.
**************************************************************************/
void wakeup_button_pressed(int canvas_x, int canvas_y)
{
  struct tile *ptile = canvas_pos_to_tile(canvas_x, canvas_y);

  if (can_client_issue_orders() && ptile) {
    wakeup_sentried_units(ptile);
  }
}

/**************************************************************************
  Adjust the position of city workers from the mapview.
**************************************************************************/
void adjust_workers_button_pressed(int canvas_x, int canvas_y)
{
  int city_x, city_y;
  enum city_tile_type worker;
  struct tile *ptile = canvas_pos_to_tile(canvas_x, canvas_y);

  if (can_client_issue_orders() && ptile) {
    struct city *pcity = find_city_near_tile(ptile);

    if (pcity && !cma_is_city_under_agent(pcity, NULL)) {
      if (!map_to_city_map(&city_x, &city_y, pcity, ptile)) {
	assert(0);
      }

      worker = get_worker_city(pcity, city_x, city_y);
      if (worker == C_TILE_WORKER) {
	dsend_packet_city_make_specialist(&aconnection, pcity->id,
					  city_x, city_y);
      } else if (worker == C_TILE_EMPTY) {
	dsend_packet_city_make_worker(&aconnection, pcity->id,
				      city_x, city_y);
      } else {
	/* If worker == C_TILE_UNAVAILABLE then we can't use this tile.  No
	 * packet is sent and city_workers_display is not updated. */
	return;
      }

      /* When the city info packet is received, update the workers on the
       * map.  This is a bad hack used to selectively update the mapview
       * when we receive the corresponding city packet. */
      city_workers_display = pcity;
    }
  }
}

/**************************************************************************
  Recenter the map on the canvas location, on user request.  Usually this
  is done with a right-click.
**************************************************************************/
void recenter_button_pressed(int canvas_x, int canvas_y)
{
  /* We use the "nearest" tile here so off-map clicks will still work. */
  struct tile *ptile = canvas_pos_to_nearest_tile(canvas_x, canvas_y);

  if (can_client_change_view() && ptile) {
    center_tile_mapcanvas(ptile);
  }
}

/**************************************************************************
 Update the turn done button state.
**************************************************************************/
void update_turn_done_button_state()
{
  bool new_state;

  if (!is_turn_done_state_valid) {
    turn_done_state = FALSE;
    is_turn_done_state_valid = TRUE;
    set_turn_done_button_state(turn_done_state);
    freelog(LOG_DEBUG, "setting turn done button state init %d",
	    turn_done_state);
  }

  new_state = (can_client_issue_orders()
	       && !game.player_ptr->turn_done && !agents_busy()
	       && !turn_done_sent);
  if (new_state == turn_done_state) {
    return;
  }

  freelog(LOG_DEBUG, "setting turn done button state from %d to %d",
	  turn_done_state, new_state);
  turn_done_state = new_state;

  set_turn_done_button_state(turn_done_state);

  if (turn_done_state) {
    if (waiting_for_end_turn
	|| (game.player_ptr->ai.control && !ai_manual_turn_done)) {
      send_turn_done();
    } else {
      update_turn_done_button(TRUE);
    }
  }
}

/**************************************************************************
  Update the goto/patrol line to the given map canvas location.
**************************************************************************/
void update_line(int canvas_x, int canvas_y)
{
  if ((hover_state == HOVER_GOTO
       || hover_state == HOVER_PATROL
       || hover_state == HOVER_CONNECT)
      && draw_goto_line) {
    struct tile *ptile, *old_tile;

    ptile = canvas_pos_to_tile(canvas_x, canvas_y);
    if (!ptile) {
      return;
    }

    old_tile = get_line_dest();
    if (!same_pos(old_tile, ptile)) {
      draw_line(ptile);
    }
  }
}

/****************************************************************************
  Update the goto/patrol line to the given overview canvas location.
****************************************************************************/
void overview_update_line(int overview_x, int overview_y)
{
  if ((hover_state == HOVER_GOTO
       || hover_state == HOVER_PATROL
       || hover_state == HOVER_CONNECT)
      && draw_goto_line) {
    struct tile *ptile, *old_tile;
    int x, y;

    overview_to_map_pos(&x, &y, overview_x, overview_y);
    ptile = map_pos_to_tile(x, y);

    old_tile = get_line_dest();
    if (!same_pos(ptile, old_tile)) {
      draw_line(ptile);
    }
  }
}

/**************************************************************************
  Find the focus unit's chance of success at attacking/defending the
  given tile.  Return FALSE if the values cannot be determined (e.g., no
  units on the tile).
**************************************************************************/
bool get_chance_to_win(int *att_chance, int *def_chance, struct tile *ptile)
{
  struct unit *my_unit, *defender, *attacker;

  if (!(my_unit = get_unit_in_focus())
      || !(defender = get_defender(my_unit, ptile))
      || !(attacker = get_attacker(my_unit, ptile))) {
    return FALSE;
  }

  /* chance to win when active unit is attacking the selected unit */
  *att_chance = unit_win_chance(my_unit, defender) * 100;

  /* chance to win when selected unit is attacking the active unit */
  *def_chance = (1.0 - unit_win_chance(attacker, my_unit)) * 100;

  return TRUE;
}

/****************************************************************************
  We sort according to the following logic:

  - Transported units should immediately follow their transporter (note that
    transporting may be recursive).
  - Otherwise we sort by ID (which is what the list is originally sorted by).
****************************************************************************/
static int unit_list_compare(const void *a, const void *b)
{
  const struct unit *punit1 = *(struct unit **)a;
  const struct unit *punit2 = *(struct unit **)b;

  if (punit1->transported_by == punit2->transported_by) {
    /* For units with the same transporter or no transporter: sort by id. */
    /* Perhaps we should sort by name instead? */
    return punit1->id - punit2->id;
  } else if (punit1->transported_by == punit2->id) {
    return 1;
  } else if (punit2->transported_by == punit1->id) {
    return -1;
  } else {
    /* If the transporters aren't the same, put in order by the
     * transporters. */
    const struct unit *ptrans1 = find_unit_by_id(punit1->transported_by);
    const struct unit *ptrans2 = find_unit_by_id(punit2->transported_by);

    if (!ptrans1) {
      ptrans1 = punit1;
    }
    if (!ptrans2) {
      ptrans2 = punit2;
    }

    return unit_list_compare(&ptrans1, &ptrans2);
  }
}

/****************************************************************************
  Fill and sort the list of units on the tile.
****************************************************************************/
void fill_tile_unit_list(struct tile *ptile, struct unit **unit_list)
{
  int i = 0;

  /* First populate the unit list. */
  unit_list_iterate(ptile->units, punit) {
    unit_list[i] = punit;
    i++;
  } unit_list_iterate_end;

  /* Then sort it. */
  qsort(unit_list, i, sizeof(*unit_list), unit_list_compare);
}

