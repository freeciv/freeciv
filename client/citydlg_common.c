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

#include "city.h"
#include "fcintl.h"
#include "log.h"
#include "support.h"

#include "clinet.h"
#include "control.h"
#include "options.h"		/* for concise_city_production */
#include "tilespec.h"		/* for is_isometric */

#include "citydlg_common.h"

/**************************************************************************
  Return the width of the city dialog canvas.
**************************************************************************/
int get_citydlg_canvas_width(void)
{
  if (is_isometric) {
    return 4 * NORMAL_TILE_WIDTH;
  } else {
    return 5 * NORMAL_TILE_WIDTH;
  }
}

/**************************************************************************
  Return the height of the city dialog canvas.
**************************************************************************/
int get_citydlg_canvas_height(void)
{
  if (is_isometric) {
    return 4 * NORMAL_TILE_HEIGHT;
  } else {
    return 5 * NORMAL_TILE_HEIGHT;
  }
}

/**************************************************************************
This converts a city coordinate position to citymap canvas coordinates
(either isometric or overhead).  It should be in cityview.c instead.
**************************************************************************/
void city_pos_to_canvas_pos(int city_x, int city_y, int *canvas_x,
			    int *canvas_y)
{
  if (is_isometric) {
    /*
     * The top-left corner is in the center of tile (-2, 2).  However,
     * we're looking for the top-left corner of the tile, so we
     * subtract off half a tile in each direction.  For a more
     * rigorous example, see map_pos_to_canvas_pos().
     */
    int iso_x = (city_x - city_y) - (-4);
    int iso_y = (city_x + city_y) - (0);

    *canvas_x = (iso_x - 1) * NORMAL_TILE_WIDTH / 2;
    *canvas_y = (iso_y - 1) * NORMAL_TILE_HEIGHT / 2;
  } else {
    *canvas_x = city_x * NORMAL_TILE_WIDTH;
    *canvas_y = city_y * NORMAL_TILE_HEIGHT;
  }
}

/**************************************************************************
This converts a citymap canvas position to a city coordinate position
(either isometric or overhead).  It should be in cityview.c instead.
**************************************************************************/
void canvas_pos_to_city_pos(int canvas_x, int canvas_y, int *map_x, int *map_y)
{
  int orig_canvas_x = canvas_x, orig_canvas_y = canvas_y;

  if (is_isometric) {
    const int W = NORMAL_TILE_WIDTH, H = NORMAL_TILE_HEIGHT;

    /* Shift the tile right so the top corner of tile (-2,2) is at
       canvas position (0,0). */
    canvas_y += H / 2;

    /* Perform a pi/4 rotation, with scaling.  See canvas_pos_to_map_pos
       for a full explanation. */
    *map_x = DIVIDE(canvas_x * H + canvas_y * W, W * H);
    *map_y = DIVIDE(canvas_y * W - canvas_x * H, W * H);

    /* Add on the offset of the top-left corner to get the final
       coordinates (like in canvas_pos_to_map_pos). */
    *map_x -= 2;
    *map_y += 2;
  } else {
    *map_x = canvas_x / NORMAL_TILE_WIDTH;
    *map_y = canvas_y / NORMAL_TILE_HEIGHT;
  }
  freelog(LOG_DEBUG, "canvas_pos_to_city_pos(pos=(%d,%d))=(%d,%d)",
	  orig_canvas_x, orig_canvas_y, *map_x, *map_y);
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
    cost = get_unit_type(pcity->currently_building)->build_cost;
  } else {
    cost = get_improvement_type(pcity->currently_building)->build_cost;
  }

  if (!pcity->is_building_unit && pcity->currently_building == B_CAPITAL) {
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
  if (!is_unit && id == B_CAPITAL) {
    my_snprintf(buffer, buffer_len, _("%s (XX) %d/turn"),
		get_impr_name_ex(pcity, id), MAX(0, pcity->shield_surplus));
  } else {
    int turns = city_turns_to_build(pcity, id, is_unit, TRUE);
    const char *name;
    int cost;

    if (is_unit) {
      name = get_unit_name(id);
      cost = get_unit_type(id)->build_cost;
    } else {
      name = get_impr_name_ex(pcity, id);
      cost = get_improvement_type(id)->build_cost;
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
    my_snprintf(buf[2], column_size, "%d", ptype->build_cost);
  } else {
    /* Total & turns left meaningless on capitalization */
    if (id == B_CAPITAL) {
      my_snprintf(buf[0], column_size, get_improvement_type(id)->name);
      buf[1][0] = '\0';
      my_snprintf(buf[2], column_size, "---");
    } else {
      my_snprintf(buf[0], column_size, get_improvement_type(id)->name);

      /* from city.c get_impr_name_ex() */
      if (pcity && wonder_replacement(pcity, id)) {
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
		  get_improvement_type(id)->build_cost);
    }
  }

  /* Add the turns-to-build entry in the 4th position */
  if (pcity) {
    if (!is_unit && id == B_CAPITAL) {
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
			    enum citizen_type *citizens)
{
  int i = 0, n;
  assert(index >= 0 && index < 5);

  for (n = 0; n < pcity->ppl_happy[index]; n++, i++) {
    citizens[i] = CITIZEN_HAPPY;
  }
  for (n = 0; n < pcity->ppl_content[index]; n++, i++) {
    citizens[i] = CITIZEN_CONTENT;
  }
  for (n = 0; n < pcity->ppl_unhappy[index]; n++, i++) {
    citizens[i] = CITIZEN_UNHAPPY;
  }
  for (n = 0; n < pcity->ppl_angry[index]; n++, i++) {
    citizens[i] = CITIZEN_ANGRY;
  }

  for (n = 0; n < pcity->ppl_elvis; n++, i++) {
    citizens[i] = CITIZEN_ELVIS;
  }
  for (n = 0; n < pcity->ppl_scientist; n++, i++) {
    citizens[i] = CITIZEN_SCIENTIST;
  }
  for (n = 0; n < pcity->ppl_taxman; n++, i++) {
    citizens[i] = CITIZEN_TAXMAN;
  }

  assert(i == pcity->size);
}

/**************************************************************************
  Rotate the given specialist citizen to the next type of citizen.
**************************************************************************/
void city_rotate_specialist(struct city *pcity, int citizen_index)
{
  enum citizen_type citizens[MAX_CITY_SIZE];
  enum citizen_type from, to;

  if (citizen_index < 0 || citizen_index >= pcity->size) {
    return;
  }

  get_city_citizen_types(pcity, 4, citizens);

  switch (citizens[citizen_index]) {
  case CITIZEN_ELVIS:
    from = SP_ELVIS;
    to = SP_SCIENTIST;
    break;
  case CITIZEN_SCIENTIST:
    from = SP_SCIENTIST;
    to = SP_TAXMAN;
    break;
  case CITIZEN_TAXMAN:
    from = SP_TAXMAN;
    to = SP_ELVIS;
    break;
  default:
    return;
  }

  city_change_specialist(pcity, from, to);
}
    
/**************************************************************************
  Activate all units on the given map tile.
**************************************************************************/
void activate_all_units(int map_x, int map_y)
{
  struct unit_list *punit_list = &map_get_tile(map_x, map_y)->units;
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
  struct packet_city_request packet;

  packet.city_id = pcity->id;
  packet.build_id = build_id;
  packet.is_build_id_unit_id = is_unit;

  /* Fill out unused fields. */
  packet.worker_x = packet.worker_y = -1;
  packet.specialist_from = packet.specialist_to = -1;

  return send_packet_city_request(&aconnection, &packet, PACKET_CITY_CHANGE);
}

/**************************************************************************
  Change the production of a given city.  Return the request ID.
**************************************************************************/
int city_sell_improvement(struct city *pcity, Impr_Type_id sell_id)
{
  struct packet_city_request packet;

  packet.city_id = pcity->id;
  packet.build_id = sell_id;

  /* Fill out unused fields. */
  packet.is_build_id_unit_id = FALSE;
  packet.worker_x = packet.worker_y = -1;
  packet.specialist_from = packet.specialist_to = -1;

  return send_packet_city_request(&aconnection, &packet, PACKET_CITY_SELL);
}

/**************************************************************************
  Change a specialist in the given city.  Return the request ID.
**************************************************************************/
int city_change_specialist(struct city *pcity, enum specialist_type from,
			   enum specialist_type to)
{
  struct packet_city_request packet;

  packet.city_id = pcity->id;
  packet.specialist_from = from;
  packet.specialist_to = to;

  /* Fill out unused fields. */
  packet.build_id = -1;
  packet.is_build_id_unit_id = FALSE;
  packet.worker_x = packet.worker_y = -1;

  return send_packet_city_request(&aconnection, &packet,
				  PACKET_CITY_CHANGE_SPECIALIST);
}

/**************************************************************************
  Toggle a worker<->specialist at the given city tile.  Return the
  request ID.
**************************************************************************/
int city_toggle_worker(struct city *pcity, int city_x, int city_y)
{
  struct packet_city_request packet;
  enum packet_type ptype;

  assert(is_valid_city_coords(city_x, city_y));

  packet.city_id = pcity->id;
  packet.worker_x = city_x;
  packet.worker_y = city_y;

  /* Fill out unused fields. */
  packet.build_id = -1;
  packet.is_build_id_unit_id = FALSE;
  packet.specialist_from = packet.specialist_to = -1;

  if (pcity->city_map[city_x][city_y] == C_TILE_WORKER) {
    ptype = PACKET_CITY_MAKE_SPECIALIST;
  } else if (pcity->city_map[city_x][city_y] == C_TILE_EMPTY) {
    ptype = PACKET_CITY_MAKE_WORKER;
  } else {
    return 0;
  }

  freelog(LOG_DEBUG, "city_toggle_worker(city='%s'(%d), x=%d, y=%d, %s)",
	  pcity->name, pcity->id, city_x, city_y,
	  (ptype == PACKET_CITY_MAKE_SPECIALIST) ? "clear" : "set");

  return send_packet_city_request(&aconnection, &packet, ptype);
}
