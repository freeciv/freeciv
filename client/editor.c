/********************************************************************** 
 Freeciv - Copyright (C) 2005 - The Freeciv Poject
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
#include <stdarg.h>
#include <string.h>

#include "log.h"
#include "support.h"

#include "game.h"
#include "map.h"
#include "packets.h"

#include "climap.h"
#include "clinet.h"
#include "control.h"
#include "editor.h"

/* where the selected terrain and specials for editing are stored */
static enum editor_tool_type selected_tool = ETOOL_PAINT;
static enum tile_special_type selected_special = S_LAST;
static struct terrain *selected_terrain = NULL;
static enum editor_paint_type selected_paint_type = EPAINT_LAST;
static struct unit *selected_unit;
static struct city *selected_city;

/****************************************************************************
  Initialize the editor tools data.

  FIXME: This absolutely has to be called anew each time we connect to a
  new server, since the ruleset may be different and data needs to be reset.
****************************************************************************/
void editor_init_tools(void)
{ 
  struct player *pplayer = game.player_ptr ? game.player_ptr : get_player(0);

  if (selected_unit) {
    destroy_unit_virtual(selected_unit);
  }
  selected_unit = create_unit_virtual(pplayer, 0, get_unit_type(0), 0);

  if (selected_city) {
    remove_city_virtual(selected_city);
  }
  selected_city = create_city_virtual(pplayer, NULL, "");

  selected_tool = ETOOL_PAINT;
  selected_special = S_LAST;
  selected_paint_type = EPAINT_LAST;
  selected_terrain = NULL;
}

/****************************************************************************
  Returns the currently selected editor tool type.
****************************************************************************/
void editor_set_selected_tool_type(enum editor_tool_type type)
{
  selected_tool = type;
}

/****************************************************************************
  Returns the currently selected editor paint type.
****************************************************************************/
void editor_set_selected_paint_type(enum editor_paint_type type)
{
  selected_paint_type = type;
}

/****************************************************************************
  Sets the selected editor terrain.
****************************************************************************/
void editor_set_selected_terrain(struct terrain *pterrain)
{
  selected_terrain = pterrain;
}

/****************************************************************************
  Sets the selected editor special.
****************************************************************************/
void editor_set_selected_special(enum tile_special_type special)
{
  selected_special = special;
}

/****************************************************************************
  Returns the selected unit.
****************************************************************************/
struct unit *editor_get_selected_unit(void)
{
  return selected_unit;
}

/****************************************************************************
  Returns the selected city.
****************************************************************************/
struct city *editor_get_selected_city(void)
{
  return selected_city;
}

/****************************************************************************
 problem: could be multiple units on a particular tile
 TODO: edit existing units
****************************************************************************/
static void do_unit(struct tile *ptile)
{
  struct packet_edit_unit packet;

  packet.create_new = TRUE; /* ? */
  packet.delete = FALSE;

  packet.id = selected_unit->id;
  packet.owner = selected_unit->owner->player_no;

  packet.x = ptile->x;
  packet.y = ptile->y;

  packet.homecity = selected_unit->homecity;

  packet.veteran = selected_unit->veteran;
  packet.paradropped = selected_unit->paradropped;

  packet.type = selected_unit->type->index;
  packet.transported_by = selected_unit->transported_by;

  packet.movesleft = selected_unit->moves_left;
  packet.hp = selected_unit->hp;
  packet.fuel = selected_unit->fuel;

  packet.activity_count = selected_unit->activity_count;

  send_packet_edit_unit(&aconnection, &packet);
}

/****************************************************************************
 basically package_city in citytools.c
****************************************************************************/
static void do_city(struct tile *ptile)
{
  struct packet_edit_city packet;
  struct city *pcity = selected_city;
  int i;

  packet.owner = pcity->owner->player_no;
  packet.x = ptile->x;
  packet.y = ptile->y;

  packet.size = pcity->size;
  for (i = 0; i < NUM_TRADEROUTES; i++) {
    packet.trade[i] = pcity->trade[i];
  }

  packet.food_stock = pcity->food_stock;
  packet.shield_stock = pcity->shield_stock;

  packet.turn_last_built = pcity->turn_last_built;
  packet.turn_founded = pcity->turn_founded;
  packet.changed_from_is_unit = pcity->changed_from.is_unit;
  packet.changed_from_id = pcity->changed_from.value;
  packet.before_change_shields = pcity->before_change_shields;
  packet.disbanded_shields = pcity->disbanded_shields;
  packet.caravan_shields = pcity->caravan_shields;
  packet.last_turns_shield_surplus = pcity->last_turns_shield_surplus;

  packet.diplomat_investigate = FALSE; /* FIXME: this overwrites the value! */

  packet.airlift = pcity->airlift;
  packet.did_buy = pcity->did_buy;
  packet.did_sell = pcity->did_sell;
  packet.was_happy = pcity->was_happy;

  BV_CLR_ALL(packet.improvements);
  impr_type_iterate(building) {
    if (city_got_building(pcity, building)) {
      BV_SET(packet.improvements, building);
    }
  } impr_type_iterate_end;

  send_packet_edit_city(&aconnection, &packet);
}

#if 0
/****************************************************************************
 basically package_city in citytools.c
****************************************************************************/
void do_edit_player(void)
{
  struct packet_edit_player packet;

  send_packet_edit_city(&aconnection, &packet);
}
#endif

/****************************************************************************
  Does the current paint operation onto the tile.

  For instance, if the paint operation is paint-terrain, then we just change
  the current tile's terrain to the selected terrain.
****************************************************************************/
static void do_paint(struct tile *ptile)
{
  struct tile tile = *ptile;

  switch (selected_paint_type) {
  case EPAINT_TERRAIN:
    if (selected_terrain) {
      tile.terrain = selected_terrain;
    }
    break;
  case EPAINT_SPECIAL:
    /* add new special to existing specials on the tile */
    if (selected_special == S_LAST) {
      tile_clear_all_specials(&tile);
    } else {
      tile_add_special(&tile, selected_special);
    }
    break;
  case EPAINT_LAST:
    return;
  } 

  /* send the result to the server for changing */
  /* FIXME: No way to change resources. */
  dsend_packet_edit_tile(&aconnection, ptile->x, ptile->y,
			 tile.terrain->index,
			 tile.resource ? tile.resource->index : -1,
			 tile.special);
}

/****************************************************************************
  if the client is in edit_mode, then this function captures clicks on the
  map canvas.
****************************************************************************/
void editor_do_click(struct tile *ptile)
{
  /* Editing tiles that we can't see (or are fogged) will only lead to
   * problems. */
  if (client_tile_get_known(ptile) != TILE_KNOWN) {
    return;
  }

  switch (selected_tool) {
  case ETOOL_PAINT:
    do_paint(ptile);
    break;
  case ETOOL_UNIT:
    do_unit(ptile);
    break;
  case ETOOL_CITY:
    do_city(ptile);
    break;
  case ETOOL_PLAYER:
  case ETOOL_DELETE:
  case ETOOL_LAST:
    break;
  }
}
