/********************************************************************** 
 Freeciv - Copyright (C) 2004 - A. Gorshenev
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
#include "support.h"

#include "agents.h"

#include "sha.h"

/**************************************************************************
This is the simple historian agent.
It just saves the last states of all tiles and units.
The trick is just to call this agent the last of all
so it still keeps old values whereas all other agents 
allready got the new ones.
**************************************************************************/

static struct tile *previous_tiles = NULL;
static struct unit_list previous_units;

/**************************************************************************
...
**************************************************************************/
static void sha_tile_update(struct tile *ptile)
{
  freelog(LOG_DEBUG, "sha got tile: %d ~= (%d, %d)",
	  ptile->index, TILE_XY(ptile));

#if 0
  previous_tiles[ptile->index] = *ptile;
#endif
}

/**************************************************************************
...
**************************************************************************/
static void sha_unit_change(int id)
{
  struct unit *punit = find_unit_by_id(id);
  struct unit *pold_unit = unit_list_find(&previous_units, id);

  freelog(LOG_DEBUG, "sha got unit: %d", id);

  assert(pold_unit);
  *pold_unit = *punit;
}

/**************************************************************************
...
**************************************************************************/
static void sha_unit_new(int id)
{
  struct unit *punit = find_unit_by_id(id);
  struct unit *pold_unit = create_unit_virtual(get_player(punit->owner),
					       NULL, 0, 0);

  freelog(LOG_DEBUG, "sha got unit: %d", id);

  *pold_unit = *punit;
  unit_list_insert(&previous_units, pold_unit);
}

/**************************************************************************
...
**************************************************************************/
static void sha_unit_remove(int id)
{
  struct unit *pold_unit = unit_list_find(&previous_units, id);;

  freelog(LOG_DEBUG, "sha got unit: %d", id);

  assert(pold_unit);
  unit_list_unlink(&previous_units, pold_unit);
}

/**************************************************************************
...
**************************************************************************/
void simple_historian_init(void)
{
  struct agent self;

  previous_tiles = fc_malloc(MAX_MAP_INDEX * sizeof(*previous_tiles));
  memset(previous_tiles, 0, MAX_MAP_INDEX * sizeof(*previous_tiles));

  unit_list_init(&previous_units);

  memset(&self, 0, sizeof(self));
  sz_strlcpy(self.name, "Simple Historian");

  self.level = LAST_AGENT_LEVEL;

  self.unit_callbacks[CB_REMOVE] = sha_unit_remove;
  self.unit_callbacks[CB_CHANGE] = sha_unit_change;
  self.unit_callbacks[CB_NEW] = sha_unit_new;
  self.tile_callbacks[CB_REMOVE] = sha_tile_update;
  self.tile_callbacks[CB_CHANGE] = sha_tile_update;
  self.tile_callbacks[CB_NEW] = sha_tile_update;
  register_agent(&self);
}

/**************************************************************************
Public interface
**************************************************************************/

/**************************************************************************
...
**************************************************************************/
struct tile *sha_tile_recall(struct tile *ptile)
{
  return &previous_tiles[ptile->index];
}

/**************************************************************************
...
**************************************************************************/
struct unit *sha_unit_recall(int id)
{
  return unit_list_find(&previous_units, id);
}
