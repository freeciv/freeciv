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

#include "city.h"
#include "fcintl.h"
#include "log.h"
#include "support.h"

#include "citydlg_g.h"
#include "mapview_g.h"

#include "citydlg_common.h"
#include "civclient.h"		/* for can_client_issue_orders() */
#include "climap.h"
#include "clinet.h"
#include "control.h"
#include "mapview_common.h"
#include "options.h"		/* for concise_city_production */
#include "tilespec.h"		/* for is_isometric */

static int citydlg_width, citydlg_height;

/**************************************************************************
  Return the width of the city dialog canvas.
**************************************************************************/
int get_citydlg_canvas_width(void)
{
  return citydlg_width;
}

/**************************************************************************
  Return the height of the city dialog canvas.
**************************************************************************/
int get_citydlg_canvas_height(void)
{
  return citydlg_height;
}

/**************************************************************************
  Calculate the citydlg width and height.
**************************************************************************/
void generate_citydlg_dimensions(void)
{
  int min_x = 0, max_x = 0, min_y = 0, max_y = 0;

  city_map_iterate(city_x, city_y) {
    int canvas_x, canvas_y;

    map_to_gui_vector(&canvas_x, &canvas_y,
		      city_x - CITY_MAP_RADIUS, city_y - CITY_MAP_RADIUS);

    min_x = MIN(canvas_x, min_x);
    max_x = MAX(canvas_x, max_x);
    min_y = MIN(canvas_y, min_y);
    max_y = MAX(canvas_y, max_y);
  } city_map_iterate_end;

  citydlg_width = max_x - min_x + NORMAL_TILE_WIDTH;
  citydlg_height = max_y - min_y + NORMAL_TILE_HEIGHT;
}

/**************************************************************************
  Converts a (cartesian) city position to citymap canvas coordinates.
  Returns TRUE if the city position is valid.
**************************************************************************/
bool city_to_canvas_pos(int *canvas_x, int *canvas_y, int city_x, int city_y)
{
  const int x0 = CITY_MAP_RADIUS, y0 = CITY_MAP_RADIUS;
  const int width = get_citydlg_canvas_width();
  const int height = get_citydlg_canvas_height();

  /* The citymap is centered over the center of the citydlg canvas. */
  map_to_gui_vector(canvas_x, canvas_y, city_x - x0, city_y - y0);
  *canvas_x += (width - NORMAL_TILE_WIDTH) / 2;
  *canvas_y += (height - NORMAL_TILE_HEIGHT) / 2;

  if (!is_valid_city_coords(city_x, city_y)) {
    assert(FALSE);
    return FALSE;
  }
  return TRUE;
}

/**************************************************************************
  Converts a citymap canvas position to a (cartesian) city coordinate
  position.  Returns TRUE iff the city position is valid.
**************************************************************************/
bool canvas_to_city_pos(int *city_x, int *city_y, int canvas_x, int canvas_y)
{
  int orig_canvas_x = canvas_x, orig_canvas_y = canvas_y;
  const int width = get_citydlg_canvas_width();
  const int height = get_citydlg_canvas_height();

  /* The citymap is centered over the center of the citydlg canvas. */
  canvas_x -= (width - NORMAL_TILE_WIDTH) / 2;
  canvas_y -= (height - NORMAL_TILE_HEIGHT) / 2;

  if (is_isometric) {
    const int W = NORMAL_TILE_WIDTH, H = NORMAL_TILE_HEIGHT;

    /* Shift the tile left so the top corner of the origin tile is at
       canvas position (0,0). */
    canvas_x -= W / 2;

    /* Perform a pi/4 rotation, with scaling.  See canvas_pos_to_map_pos
       for a full explanation. */
    *city_x = DIVIDE(canvas_x * H + canvas_y * W, W * H);
    *city_y = DIVIDE(canvas_y * W - canvas_x * H, W * H);
  } else {
    *city_x = DIVIDE(canvas_x, NORMAL_TILE_WIDTH);
    *city_y = DIVIDE(canvas_y, NORMAL_TILE_HEIGHT);
  }

  /* Add on the offset of the top-left corner to get the final
   * coordinates (like in canvas_to_map_pos). */
  *city_x += CITY_MAP_RADIUS;
  *city_y += CITY_MAP_RADIUS;

  freelog(LOG_DEBUG, "canvas_to_city_pos(pos=(%d,%d))=(%d,%d)",
	  orig_canvas_x, orig_canvas_y, *city_x, *city_y);

  return is_valid_city_coords(*city_x, *city_y);
}

/* Iterate over all known tiles in the city.  This iteration follows the
 * painter's algorithm and can be used for drawing. */
#define citydlg_known_iterate(pcity, city_x, city_y,			    \
			      ptile, canvas_x, canvas_y)		    \
{                                                                           \
  int _itr;								    \
                                                                            \
  /* We must go in order to preserve the painter's algorithm. */	    \
  for (_itr = 0; _itr < CITY_MAP_SIZE * CITY_MAP_SIZE; _itr++) {            \
    int city_x = _itr / CITY_MAP_SIZE, city_y = _itr % CITY_MAP_SIZE;	    \
    int canvas_x, canvas_y;						    \
    struct tile *ptile;							    \
                                                                            \
    if (is_valid_city_coords(city_x, city_y)				    \
	&& (ptile = city_map_to_map(pcity, city_x, city_y))		    \
	&& tile_get_known(ptile)					    \
	&& city_to_canvas_pos(&canvas_x, &canvas_y, city_x, city_y)) {	    \

#define citydlg_known_iterate_end                                           \
    }                                                                       \
  }                                                                         \
}

/****************************************************************************
  Draw the full city map onto the canvas store.  Works for both isometric
  and orthogonal views.
****************************************************************************/
void city_dialog_redraw_map(struct city *pcity,
			    struct canvas *pcanvas)
{
  /* First make it all black. */
  canvas_put_rectangle(pcanvas, COLOR_STD_BLACK, 0, 0,
		       get_citydlg_canvas_width(),
		       get_citydlg_canvas_height());

  citydlg_known_iterate(pcity, city_x, city_y,
			ptile, canvas_x, canvas_y) {
    if (is_isometric) {
      put_one_tile_iso(pcanvas, ptile, canvas_x, canvas_y, TRUE);
    } else {
      put_one_tile(pcanvas, ptile, canvas_x, canvas_y, TRUE);
    }
  } citydlg_known_iterate_end;

  /* We have to put the output afterwards or it will be covered
   * in iso-view. */
  citydlg_known_iterate(pcity, city_x, city_y,
			ptile, canvas_x, canvas_y) {
    if (pcity->city_map[city_x][city_y] == C_TILE_WORKER) {
      put_city_tile_output(pcity, city_x, city_y,
			   pcanvas, canvas_x, canvas_y);
    }
  } citydlg_known_iterate_end;

  /* This sometimes will draw one of the lines on top of a city or
   * unit pixmap (in iso-view). This should maybe be moved to
   * put_one_tile to fix this, but maybe it wouldn't be a good idea because
   * the lines would get obscured. */
  citydlg_known_iterate(pcity, city_x, city_y,
			ptile, canvas_x, canvas_y) {
    if (pcity->city_map[city_x][city_y] == C_TILE_UNAVAILABLE) {
      put_red_frame_tile(pcanvas, canvas_x, canvas_y);
    }
  } citydlg_known_iterate_end;
}

/**************************************************************************
  Find the city dialog city production text for the given city, and
  place it into the buffer.  This will check the
  concise_city_production option.  pcity may be NULL; in this case a
  filler string is returned.
**************************************************************************/
void get_city_dialog_production(struct city *pcity,
				char *buffer, size_t buffer_len)
{
  int turns, cost, stock;

  if (pcity == NULL) {
    /* 
     * Some GUIs use this to build a "filler string" so that they can
     * properly size the widget to hold the string.  This has some
     * obvious problems; the big one is that we have two forms of time
     * information: "XXX turns" and "never".  Later this may need to
     * be extended to return the longer of the two; in the meantime
     * translators can fudge it by changing this "filler" string. 
     */
    my_snprintf(buffer, buffer_len, Q_("?filler:XXX/XXX XXX turns"));
    return;
  }

  turns = city_turns_to_build(pcity, pcity->currently_building,
			      pcity->is_building_unit, TRUE);
  stock = pcity->shield_stock;

  if (pcity->is_building_unit) {
    cost = unit_build_shield_cost(pcity->currently_building);
  } else {
    cost = impr_build_shield_cost(pcity->currently_building);
  }

  if (get_current_construction_bonus(pcity, EFT_PROD_TO_GOLD) > 0) {
    my_snprintf(buffer, buffer_len, _("%3d gold per turn"),
		MAX(0, pcity->shield_surplus));
  } else {
    char time[50];

    if (turns < 999) {
      if (concise_city_production) {
	my_snprintf(time, sizeof(time), "%3d", turns);
      } else {
	my_snprintf(time, sizeof(time),
		    PL_("%3d turn", "%3d turns", turns), turns);
      }
    } else {
      my_snprintf(time, sizeof(time), "%s",
		  concise_city_production ? "-" : _("never"));
    }

    if (concise_city_production) {
      my_snprintf(buffer, buffer_len, _("%3d/%3d:%s"), stock, cost, time);
    } else {
      my_snprintf(buffer, buffer_len, _("%3d/%3d %s"), stock, cost, time);
    }
  }
}


/**************************************************************************
 Pretty sprints the info about a production (name, info, cost, turns
 to build) into a single text string.

 This is very similar to get_city_dialog_production_row; the
 difference is that instead of placing the data into an array of
 strings it all goes into one long string.  This means it can be used
 by frontends that do not use a tabled structure, but it also gives
 less flexibility.
**************************************************************************/
void get_city_dialog_production_full(char *buffer, size_t buffer_len,
				     int id, bool is_unit,
				     struct city *pcity)
{
  if (!is_unit && building_has_effect(id, EFT_PROD_TO_GOLD)) {
    my_snprintf(buffer, buffer_len, _("%s (XX) %d/turn"),
		get_impr_name_ex(pcity, id), MAX(0, pcity->shield_surplus));
  } else {
    int turns = city_turns_to_build(pcity, id, is_unit, TRUE);
    const char *name;
    int cost;

    if (is_unit) {
      name = get_unit_name(id);
      cost = unit_build_shield_cost(id);
    } else {
      name = get_impr_name_ex(pcity, id);
      cost = impr_build_shield_cost(id);
    }

    if (turns < 999) {
      my_snprintf(buffer, buffer_len,
		  PL_("%s (%d) %d turn", "%s (%d) %d turns", turns),
		  name, cost, turns);
    } else {
      my_snprintf(buffer, buffer_len, "%s (%d) never", name, cost);
    }
  }
}

/**************************************************************************
 Pretty sprints the info about a production in 4 columns (name, info,
 cost, turns to build). The columns must each have a size of
 column_size bytes.
**************************************************************************/
void get_city_dialog_production_row(char *buf[], size_t column_size, int id,
				    bool is_unit, struct city *pcity)
{
  if (is_unit) {
    struct unit_type *ptype = get_unit_type(id);

    my_snprintf(buf[0], column_size, unit_name(id));

    /* from unit.h get_unit_name() */
    if (ptype->fuel > 0) {
      my_snprintf(buf[1], column_size, "%d/%d/%d(%d)",
		  ptype->attack_strength, ptype->defense_strength,
		  ptype->move_rate / 3,
		  (ptype->move_rate / 3) * ptype->fuel);
    } else {
      my_snprintf(buf[1], column_size, "%d/%d/%d", ptype->attack_strength,
		  ptype->defense_strength, ptype->move_rate / 3);
    }
    my_snprintf(buf[2], column_size, "%d", unit_build_shield_cost(id));
  } else {
    /* Total & turns left meaningless on capitalization */
    if (building_has_effect(id, EFT_PROD_TO_GOLD)) {
      my_snprintf(buf[0], column_size, get_improvement_type(id)->name);
      buf[1][0] = '\0';
      my_snprintf(buf[2], column_size, "---");
    } else {
      my_snprintf(buf[0], column_size, get_improvement_type(id)->name);

      /* from city.c get_impr_name_ex() */
      if (pcity && is_building_replaced(pcity, id)) {
	my_snprintf(buf[1], column_size, "*");
      } else {
	const char *state = "";

	if (is_wonder(id)) {
	  state = _("Wonder");
	  if (game.global_wonders[id] != 0) {
	    state = _("Built");
	  }
	  if (wonder_obsolete(id)) {
	    state = _("Obsolete");
	  }
	}
	my_snprintf(buf[1], column_size, "%s", state);
      }

      my_snprintf(buf[2], column_size, "%d",
		  impr_build_shield_cost(id));
    }
  }

  /* Add the turns-to-build entry in the 4th position */
  if (pcity) {
    if (!is_unit && building_has_effect(id, EFT_PROD_TO_GOLD)) {
      my_snprintf(buf[3], column_size, _("%d/turn"),
		  MAX(0, pcity->shield_surplus));
    } else {
      int turns = city_turns_to_build(pcity, id, is_unit, FALSE);
      if (turns < 999) {
	my_snprintf(buf[3], column_size, "%d", turns);
      } else {
	my_snprintf(buf[3], column_size, "%s", _("never"));
      }
    }
  } else {
    my_snprintf(buf[3], column_size, "---");
  }
}

/**************************************************************************
  Provide a list of all citizens in the city, in order.  "index"
  should be the happiness index (currently [0..4]; 4 = final
  happiness).  "citizens" should be an array large enough to hold all
  citizens (use MAX_CITY_SIZE to be on the safe side).
**************************************************************************/
void get_city_citizen_types(struct city *pcity, int index,
			    struct citizen_type *citizens)
{
  int i = 0, n;
  assert(index >= 0 && index < 5);

  for (n = 0; n < pcity->ppl_happy[index]; n++, i++) {
    citizens[i].type = CITIZEN_HAPPY;
  }
  for (n = 0; n < pcity->ppl_content[index]; n++, i++) {
    citizens[i].type = CITIZEN_CONTENT;
  }
  for (n = 0; n < pcity->ppl_unhappy[index]; n++, i++) {
    citizens[i].type = CITIZEN_UNHAPPY;
  }
  for (n = 0; n < pcity->ppl_angry[index]; n++, i++) {
    citizens[i].type = CITIZEN_ANGRY;
  }

  specialist_type_iterate(sp) {
    for (n = 0; n < pcity->specialists[sp]; n++, i++) {
      citizens[i].type = CITIZEN_SPECIALIST;
      citizens[i].spec_type = sp;
    }
  } specialist_type_iterate_end;

  assert(i == pcity->size);
}

/**************************************************************************
  Rotate the given specialist citizen to the next type of citizen.
**************************************************************************/
void city_rotate_specialist(struct city *pcity, int citizen_index)
{
  struct citizen_type citizens[MAX_CITY_SIZE];
  Specialist_type_id from, to;

  if (citizen_index < 0 || citizen_index >= pcity->size) {
    return;
  }

  get_city_citizen_types(pcity, 4, citizens);

  if (citizens[citizen_index].type != CITIZEN_SPECIALIST) {
    return;
  }
  from = citizens[citizen_index].spec_type;

  /* Loop through all specialists in order until we find a usable one
   * (or run out of choices). */
  to = from;
  assert(to >= 0 && to < SP_COUNT);
  do {
    to = (to + 1) % SP_COUNT;
  } while (to != from && !city_can_use_specialist(pcity, to));

  if (from != to) {
    city_change_specialist(pcity, from, to);
  }
}
    
/**************************************************************************
  Activate all units on the given map tile.
**************************************************************************/
void activate_all_units(struct tile *ptile)
{
  struct unit_list *punit_list = &ptile->units;
  struct unit *pmyunit = NULL;

  unit_list_iterate((*punit_list), punit) {
    if (game.player_idx == punit->owner) {
      /* Activate this unit. */
      pmyunit = punit;
      request_new_unit_activity(punit, ACTIVITY_IDLE);
    }
  } unit_list_iterate_end;
  if (pmyunit) {
    /* Put the focus on one of the activated units. */
    set_unit_focus(pmyunit);
  }
}

/**************************************************************************
  Change the production of a given city.  Return the request ID.
**************************************************************************/
int city_change_production(struct city *pcity, bool is_unit, int build_id)
{
  return dsend_packet_city_change(&aconnection, pcity->id, build_id,
				  is_unit);
}

/**************************************************************************
  Set the worklist for a given city.  Return the request ID.

  Note that the worklist does NOT include the current production.
**************************************************************************/
int city_set_worklist(struct city *pcity, struct worklist *pworklist)
{
  struct worklist copy;

  copy_worklist(&copy, pworklist);

  /* Don't send the worklist name to the server. */
  copy.name[0] = '\0';

  return dsend_packet_city_worklist(&aconnection, pcity->id, &copy);
}

/**************************************************************************
  Insert an item into the city's worklist.

  Note that the queue DOES include the current production.
**************************************************************************/
bool city_queue_insert(struct city *pcity, int position,
		       bool item_is_unit, int item_id)
{
  if (position == 0) {
    int old_id;
    bool old_is_unit;

    /* Insert as current production. */
    if (item_is_unit && !can_build_unit_direct(pcity, item_id)) {
      return FALSE;
    }
    if (!item_is_unit && !can_build_improvement_direct(pcity, item_id)) {
      return FALSE;
    }

    old_id = pcity->currently_building;
    old_is_unit = pcity->is_building_unit;
    if (!worklist_insert(&pcity->worklist, old_id, old_is_unit, 0)) {
      return FALSE;
    }

    city_set_worklist(pcity, &pcity->worklist);
    city_change_production(pcity, item_is_unit, item_id);
  } else if (position >= 1
	     && position <= worklist_length(&pcity->worklist)) {
    /* Insert into middle. */
    if (item_is_unit && !can_eventually_build_unit(pcity, item_id)) {
      return FALSE;
    }
    if (!item_is_unit && !can_eventually_build_improvement(pcity, item_id)) {
      return FALSE;
    }
    if (!worklist_insert(&pcity->worklist, item_id, item_is_unit,
			 position - 1)) {
      return FALSE;
    }
    city_set_worklist(pcity, &pcity->worklist);
  } else {
    /* Insert at end. */
    if (item_is_unit && !can_eventually_build_unit(pcity, item_id)) {
      return FALSE;
    }
    if (!item_is_unit && !can_eventually_build_improvement(pcity, item_id)) {
      return FALSE;
    }
    if (!worklist_append(&pcity->worklist, item_id, item_is_unit)) {
      return FALSE;
    }
    city_set_worklist(pcity, &pcity->worklist);
  }
  return TRUE;
}

/**************************************************************************
  Get the city current production and the worklist, like it should be.
**************************************************************************/
void city_get_queue(struct city *pcity, struct worklist *pqueue)
{
  int id;
  bool is_unit;

  copy_worklist(pqueue, &pcity->worklist);

  /* The GUI wants current production to be in the task list, but the
     worklist API wants it out for reasons unknown. Perhaps someone enjoyed
     making things more complicated than necessary? So I dance around it. */

  /* We want the current production to be in the queue. Always. */
  worklist_remove(pqueue, MAX_LEN_WORKLIST - 1);

  id = pcity->currently_building;
  is_unit = pcity->is_building_unit;
  worklist_insert(pqueue, id, is_unit, 0);
}

/**************************************************************************
  Set the city current production and the worklist, like it should be.
**************************************************************************/
void city_set_queue(struct city *pcity, struct worklist *pqueue)
{
  struct worklist copy;
  int id;
  bool is_unit;

  copy_worklist(&copy, pqueue);

  /* The GUI wants current production to be in the task list, but the
     worklist API wants it out for reasons unknown. Perhaps someone enjoyed
     making things more complicated than necessary? So I dance around it. */
  if (worklist_peek(&copy, &id, &is_unit)) {
    worklist_advance(&copy);

    city_set_worklist(pcity, &copy);
    city_change_production(pcity, is_unit, id);
  } else {
    /* You naughty boy, you can't erase the current production. Nyah! */
    if (worklist_is_empty(&pcity->worklist)) {
      refresh_city_dialog(pcity);
    } else {
      city_set_worklist(pcity, &copy);
    }
  }
}

/**************************************************************************
  Return TRUE iff the city can buy.
**************************************************************************/
bool city_can_buy(const struct city *pcity)
{
  return (can_client_issue_orders()
	  && !pcity->did_buy
	  && city_buy_cost(pcity) > 0);
}

/**************************************************************************
  Change the production of a given city.  Return the request ID.
**************************************************************************/
int city_sell_improvement(struct city *pcity, Impr_Type_id sell_id)
{
  return dsend_packet_city_sell(&aconnection, pcity->id, sell_id);
}

/**************************************************************************
  Buy the current production item in a given city.  Return the request ID.
**************************************************************************/
int city_buy_production(struct city *pcity)
{
  return dsend_packet_city_buy(&aconnection, pcity->id);
}

/**************************************************************************
  Change a specialist in the given city.  Return the request ID.
**************************************************************************/
int city_change_specialist(struct city *pcity, Specialist_type_id from,
			   Specialist_type_id to)
{
  return dsend_packet_city_change_specialist(&aconnection, pcity->id, from,
					     to);
}

/**************************************************************************
  Toggle a worker<->specialist at the given city tile.  Return the
  request ID.
**************************************************************************/
int city_toggle_worker(struct city *pcity, int city_x, int city_y)
{
  assert(is_valid_city_coords(city_x, city_y));

  if (pcity->city_map[city_x][city_y] == C_TILE_WORKER) {
    return dsend_packet_city_make_specialist(&aconnection, pcity->id, city_x,
					     city_y);
  } else if (pcity->city_map[city_x][city_y] == C_TILE_EMPTY) {
    return dsend_packet_city_make_worker(&aconnection, pcity->id, city_x,
					 city_y);
  } else {
    return 0;
  }
}

/**************************************************************************
  Tell the server to rename the city.  Return the request ID.
**************************************************************************/
int city_rename(struct city *pcity, const char *name)
{
  return dsend_packet_city_rename(&aconnection, pcity->id, name);
}
