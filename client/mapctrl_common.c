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

#include "combat.h"
#include "log.h"
#include "support.h"

#include "agents.h"
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

#include "mapctrl_common.h"

/* Update the workers for a city on the map, when the update is received */
struct city *city_workers_display = NULL;

static bool turn_done_state;
static bool is_turn_done_state_valid = FALSE;

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
  int map_x, map_y, canvas_x, canvas_y;

  if (!can_client_change_view()) {
    return;
  }

  canvas_x = mapview_canvas.width / 2;
  canvas_y = mapview_canvas.height / 2;
  canvas_x += DIR_DX[gui_dir] * mapview_canvas.width / 2;
  canvas_y += DIR_DY[gui_dir] * mapview_canvas.height / 2;
  if (!canvas_to_map_pos(&map_x, &map_y, canvas_x, canvas_y)) {
    nearest_real_pos(&map_x, &map_y);
  }
  center_tile_mapcanvas(map_x, map_y);
}

/**************************************************************************
  Do some appropriate action when the "main" mouse button (usually
  left-click) is pressed.  For more sophisticated user control use (or
  write) a different xxx_button_pressed function.
**************************************************************************/
void action_button_pressed(int canvas_x, int canvas_y)
{
  int map_x, map_y;

  if (can_client_change_view()) {
    /* FIXME: Some actions here will need to check can_client_issue_orders.
     * But all we can check is the lowest common requirement. */
    if (canvas_to_map_pos(&map_x, &map_y, canvas_x, canvas_y)) {
      do_map_click(map_x, map_y);
    }
  }
}

/**************************************************************************
  Wakeup sentried units on the tile of the specified location.  Usually
  this is done with SHIFT+left-click.
**************************************************************************/
void wakeup_button_pressed(int canvas_x, int canvas_y)
{
  int map_x, map_y;

  if (can_client_issue_orders()) {
    if (canvas_to_map_pos(&map_x, &map_y, canvas_x, canvas_y)) {
      wakeup_sentried_units(map_x, map_y);
    }
  }
}

/**************************************************************************
  Adjust the position of city workers from the mapview.  Usually this is
  done with SHIFT+left-click.
**************************************************************************/
void adjust_workers_button_pressed(int canvas_x, int canvas_y)
{
  int map_x, map_y, city_x, city_y;
  struct city *pcity;
  struct packet_city_request packet;
  enum city_tile_type worker;

  if (can_client_issue_orders()) {
    if (canvas_to_map_pos(&map_x, &map_y, canvas_x, canvas_y)) {
      pcity = find_city_near_tile(map_x, map_y);
      if (pcity && !cma_is_city_under_agent(pcity, NULL)) {
	if (!map_to_city_map(&city_x, &city_y, pcity, map_x, map_y)) {
	  assert(0);
	}

	packet.city_id = pcity->id;
	packet.worker_x = city_x;
	packet.worker_y = city_y;

	worker = get_worker_city(pcity, city_x, city_y);
	if (worker == C_TILE_WORKER) {
	  send_packet_city_request(&aconnection, &packet,
				   PACKET_CITY_MAKE_SPECIALIST);
	} else if (worker == C_TILE_EMPTY) {
	  send_packet_city_request(&aconnection, &packet,
				   PACKET_CITY_MAKE_WORKER);
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
}

/**************************************************************************
  Recenter the map on the canvas location, on user request.  Usually this
  is done with a right-click.
**************************************************************************/
void recenter_button_pressed(int canvas_x, int canvas_y)
{
  int map_x, map_y;

  if (can_client_change_view()) {
    if (!canvas_to_map_pos(&map_x, &map_y, canvas_x, canvas_y)) {
      nearest_real_pos(&map_x, &map_y);
    }
    center_tile_mapcanvas(map_x, map_y);
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
  if ((hover_state == HOVER_GOTO || hover_state == HOVER_PATROL)
      && draw_goto_line) {
    int x, y, old_x, old_y;

    if (!canvas_to_map_pos(&x, &y, canvas_x, canvas_y)) {
      nearest_real_pos(&x, &y);
    }

    get_line_dest(&old_x, &old_y);
    if (!same_pos(old_x, old_y, x, y)) {
      draw_line(x, y);
    }
  }
}

/**************************************************************************
  Find the focus unit's chance of success at attacking/defending the
  given tile.  Return FALSE if the values cannot be determined (e.g., no
  units on the tile).
**************************************************************************/
bool get_chance_to_win(int *att_chance, int *def_chance,
		       int map_x, int map_y)
{
  struct unit *my_unit, *defender, *attacker;

  if (!(my_unit = get_unit_in_focus())
      || !(defender = get_defender(my_unit, map_x, map_y))
      || !(attacker = get_attacker(my_unit, map_x, map_y))) {
    return FALSE;
  }

  /* chance to win when active unit is attacking the selected unit */
  *att_chance = unit_win_chance(my_unit, defender) * 100;

  /* chance to win when selected unit is attacking the active unit */
  *def_chance = (1.0 - unit_win_chance(attacker, my_unit)) * 100;

  return TRUE;
}

static void add_line(char* buf, size_t bufsz, char *format, ...)
                     fc__attribute((format(printf, 3, 4)));

/****************************************************************************
  Add a full line of text to the buffer.
****************************************************************************/
static void add_line(char* buf, size_t bufsz, char *format, ...)
{
  va_list args;

  if (buf[0] != '\0') {
    mystrlcat(buf, "\n", bufsz);
  }

  va_start(args, format);
  my_vsnprintf(buf + strlen(buf), bufsz - strlen(buf), format, args);
  va_end(args);

  assert(strlen(buf) + 1 < bufsz);
}

/************************************************************************
Text to popup on middle-click
************************************************************************/
char* popup_info_text(int xtile, int ytile)
{
  static char out[256];
  char buf[64];
  struct city *pcity = map_get_city(xtile, ytile);
  struct tile *ptile = map_get_tile(xtile, ytile);
  struct unit *punit = find_visible_unit(ptile);
  const char *diplo_nation_plural_adjectives[DS_LAST] =
    {Q_("?nation:Neutral"), Q_("?nation:Hostile"),
     "" /* unused, DS_CEASEFIRE*/,
     Q_("?nation:Peaceful"), Q_("?nation:Friendly"), 
     Q_("?nation:Mysterious")};
  const char *diplo_city_adjectives[DS_LAST] =
    {Q_("?city:Neutral"), Q_("?city:Hostile"),
     "" /*unused, DS_CEASEFIRE */,
     Q_("?city:Peaceful"), Q_("?city:Friendly"), Q_("?city:Mysterious")};

  out[0] = '\0';
#ifdef DEBUG
  add_line(out, sizeof(out), _("Location: (%d, %d) [%d]"), 
	   xtile, ytile, ptile->continent); 
#endif /*DEBUG*/
  add_line(out, sizeof(out), _("Terrain: %s"),
	   map_get_tile_info_text(xtile, ytile));
  add_line(out, sizeof(out), _("Food/Prod/Trade: %s"),
	   map_get_tile_fpt_text(xtile, ytile));
  if (tile_has_special(ptile, S_HUT)) {
    add_line(out, sizeof(out), _("Minor Tribe Village"));
  }
  if (game.borders > 0 && !pcity) {
    struct player *owner = map_get_owner(xtile, ytile);
    struct player_diplstate *ds = game.player_ptr->diplstates;

    if (owner == game.player_ptr){
      add_line(out, sizeof(out), _("Our Territory"));
    } else if (owner) {
      if (ds[owner->player_no].type == DS_CEASEFIRE) {
	int turns = ds[owner->player_no].turns_left;

	add_line(out, sizeof(out), PL_("%s territory (%d turn ceasefire)",
				       "%s territory (%d turn ceasefire)",
				       turns),
		 get_nation_name(owner->nation), turns);
      } else {
	add_line(out, sizeof(out), _("Territory of the %s %s"),
		 diplo_nation_plural_adjectives[ds[owner->player_no].type],
		 get_nation_name_plural(owner->nation));
      }
    } else {
      add_line(out, sizeof(out), _("Unclaimed territory"));
    }
  }
  if (pcity) {
    /* Look at city owner, not tile owner (the two should be the same, if
     * borders are in use). */
    struct player *owner = city_owner(pcity);
    struct player_diplstate *ds = game.player_ptr->diplstates;

    if (owner == game.player_ptr){
      /* TRANS: "City: <name> (<nation>)" */
      add_line(out, sizeof(out), _("City: %s (%s)"), pcity->name,
	       get_nation_name(owner->nation));
    } else if (owner) {
      if (ds[owner->player_no].type == DS_CEASEFIRE) {
	int turns = ds[owner->player_no].turns_left;

        add_line(out, sizeof(out), PL_("City: %s (%s, %d turn ceasefire)",
				       "City: %s (%s, %d turn ceasefire)",
				       turns),
		 get_nation_name(owner->nation),
		 diplo_city_adjectives[ds[owner->player_no].type],
		 turns);
      } else {
        /* TRANS: "City: <name> (<nation>,<diplomatic_state>)" */
        add_line(out, sizeof(out), _("City: %s (%s,%s)"), pcity->name,
		 get_nation_name(owner->nation),
		 diplo_city_adjectives[ds[owner->player_no].type]);
      }
    }
    if (city_got_citywalls(pcity)) {
      sz_strlcat(out, _(" with City Walls"));
    }
  } 
  if (get_tile_infrastructure_set(ptile)) {
    add_line(out, sizeof(out), _("Infrastructure: %s"),
	     map_get_infrastructure_text(ptile->special));
  }
  buf[0] = '\0';
  if (concat_tile_activity_text(buf, sizeof(buf), xtile, ytile)) {
    add_line(out, sizeof(out), _("Activity: %s"), buf);
  }
  if (punit && !pcity) {
    char tmp[64] = {0};
    struct unit_type *ptype = unit_type(punit);
    if (punit->owner == game.player_idx) {
	struct city *pcity;
	pcity=player_find_city_by_id(game.player_ptr, punit->homecity);
	if (pcity)
	  my_snprintf(tmp, sizeof(tmp), "/%s", pcity->name);
      }
    add_line(out, sizeof(out), _("Unit: %s(%s%s)"), ptype->name,
	     get_nation_name(unit_owner(punit)->nation), tmp);
    if (punit->owner != game.player_idx){
      struct unit *apunit;
      if ((apunit = get_unit_in_focus())) {
	/* chance to win when active unit is attacking the selected unit */
	int att_chance = unit_win_chance(apunit, punit) * 100;
	
	/* chance to win when selected unit is attacking the active unit */
	int def_chance = (1.0 - unit_win_chance(punit, apunit)) * 100;
	
	add_line(out, sizeof(out), _("Chance to win: A:%d%% D:%d%%"),
		 att_chance, def_chance);
      }
    }
    add_line(out, sizeof(out), _("A:%d D:%d FP:%d HP:%d/%d%s"),
	     ptype->attack_strength, 
	     ptype->defense_strength, ptype->firepower, punit->hp, 
	     ptype->hp, punit->veteran ? _(" V") : "");
    if (punit->owner == game.player_idx && unit_list_size(&ptile->units) >= 2){
      my_snprintf(buf, sizeof(buf), _("  (%d more)"),
		  unit_list_size(&ptile->units) - 1);
      sz_strlcat(out, buf);
    }
  } 
  return out;
}

	
