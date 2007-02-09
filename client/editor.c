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
#include "tilespec.h"

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
    destroy_city_virtual(selected_city);
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
static enum cursor_type editor_unit(struct tile *ptile, bool testing)
{
  /* FIXME: Do checks to see if the placement is allowed, so that the
   * cursor can be set properly. */
  if (!testing) {
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

    packet.movesleft = unit_type(selected_unit)->move_rate;
    packet.hp = unit_type(selected_unit)->hp;
    packet.fuel = unit_type(selected_unit)->fuel;

    packet.activity_count = selected_unit->activity_count;

    send_packet_edit_unit(&aconnection, &packet);
  }

  return CURSOR_EDIT_ADD;
}

/****************************************************************************
 basically package_city in citytools.c
****************************************************************************/
static enum cursor_type editor_city(struct tile *ptile, bool testing)
{
  /* FIXME: Do checks to see if the placement is allowed, so that the
   * cursor can be set properly. */
  if (!testing) {
    struct packet_edit_create_city packet = {
      .owner = selected_city->owner->player_no,
      .x = ptile->x,
      .y = ptile->y
    };

    send_packet_edit_create_city(&aconnection, &packet);
  }

  return CURSOR_EDIT_ADD;
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
static enum cursor_type editor_paint(struct tile *ptile, bool testing)
{
  struct tile tile = *ptile;

  switch (selected_paint_type) {
  case EPAINT_TERRAIN:
    if (selected_terrain && tile.terrain != selected_terrain) {
      tile.terrain = selected_terrain;
    } else {
      return CURSOR_INVALID;
    }
    break;
  case EPAINT_SPECIAL:
    /* add new special to existing specials on the tile */
    if (selected_special == S_LAST && tile_has_any_specials(&tile)) {
      tile_clear_all_specials(&tile);
    } else if (selected_special != S_LAST
	       && !tile_has_special(&tile, selected_special)) {
      tile_add_special(&tile, selected_special);
    } else {
      return CURSOR_INVALID;
    }
    break;
  case EPAINT_LAST:
  default:
    return CURSOR_INVALID;
  }

  if (!testing) {
    /* send the result to the server for changing */
    /* FIXME: No way to change resources. */
    dsend_packet_edit_tile(&aconnection, ptile->x, ptile->y,
			   tile.terrain->index,
			   tile.resource ? tile.resource->index : -1,
			   tile.special);
  }

  return CURSOR_EDIT_PAINT;
}

/****************************************************************************
  if the client is in edit_mode, then this function captures clicks on the
  map canvas.

  If the testing parameter is given then no actions are taken, but the
  return value indicates what would happen if a click was made.
****************************************************************************/
static enum cursor_type editor_click(struct tile *ptile, bool testing)
{
  /* Editing tiles that we can't see (or are fogged) will only lead to
   * problems. */
  if (client_tile_get_known(ptile) != TILE_KNOWN) {
    return CURSOR_INVALID;
  }

  switch (selected_tool) {
  case ETOOL_PAINT:
    return editor_paint(ptile, testing);
  case ETOOL_UNIT:
    return editor_unit(ptile, testing);
  case ETOOL_CITY:
    if (ptile->city) {
      if (!testing) {
        do_map_click(ptile, SELECT_POPUP);
      }
      return CURSOR_SELECT;      
    }
    return editor_city(ptile, testing);
  case ETOOL_PLAYER:
  case ETOOL_DELETE:
  case ETOOL_LAST:
    break;
  }
  return CURSOR_INVALID;
}

/****************************************************************************
  Return TRUE if an editor click on this tile is allowed - if not then a
  normal click should be done instead.
****************************************************************************/
bool can_do_editor_click(struct tile *ptile)
{
  /* Previously, editor clicks were not allowed on city tiles; instead
   * a click here would cause a regular click and the citydlg would open.
   * This made it impossible to add a unit or paint a terrain underneath
   * a city. */
  return can_conn_edit(&aconnection);
}

/****************************************************************************
  if the client is in edit_mode, then this function captures clicks on the
  map canvas.
****************************************************************************/
void editor_do_click(struct tile *ptile)
{
  (void) editor_click(ptile, FALSE);
}

/****************************************************************************
  if the client is in edit_mode, then this function returns the cursor_type
  indicating what action would be taken on a click.  This can be used by
  the code to set the cursor so the user can anticipate what clicking will
  do.
****************************************************************************/
enum cursor_type editor_test_click(struct tile *ptile)
{
  return editor_click(ptile, TRUE);
}
