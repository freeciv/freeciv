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

#include "citydlg_common.h"
#include "options.h"		/* for concise_city_production */
#include "tilespec.h"		/* for is_isometric */

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
  if (is_isometric) {
    *map_x = -2;
    *map_y = 2;

    /* first find an equivalent position on the left side of the screen. */
    *map_x += canvas_x / NORMAL_TILE_WIDTH;
    *map_y -= canvas_x / NORMAL_TILE_WIDTH;
    canvas_x %= NORMAL_TILE_WIDTH;

    /* Then move op to the top corner. */
    *map_x += canvas_y / NORMAL_TILE_HEIGHT;
    *map_y += canvas_y / NORMAL_TILE_HEIGHT;
    canvas_y %= NORMAL_TILE_HEIGHT;

    assert(NORMAL_TILE_WIDTH == 2 * NORMAL_TILE_HEIGHT);
    canvas_y *= 2;		/* now we have a square. */
    if (canvas_x + canvas_y > NORMAL_TILE_WIDTH / 2)
      (*map_x)++;
    if (canvas_x + canvas_y > 3 * NORMAL_TILE_WIDTH / 2)
      (*map_x)++;
    if (canvas_x - canvas_y > NORMAL_TILE_WIDTH / 2)
      (*map_y)--;
    if (canvas_y - canvas_x > NORMAL_TILE_WIDTH / 2)
      (*map_y)++;
  } else {
    *map_x = canvas_x / NORMAL_TILE_WIDTH;
    *map_y = canvas_y / NORMAL_TILE_HEIGHT;
  }
  freelog(LOG_DEBUG, "canvas_pos_to_city_pos(pos=(%d,%d))=(%d,%d)", canvas_x, canvas_y, *map_x, *map_y);
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
	char *state = "";

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
