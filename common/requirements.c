/********************************************************************** 
 Freeciv - Copyright (C) 1996-2004 - The Freeciv Project
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

#include "support.h"

#include "government.h"
#include "improvement.h"
#include "map.h"
#include "requirements.h"

static const char *req_type_names[] = {
  "None",
  "Tech",
  "Gov",
  "Building",
  "Special",
  "Terrain"
};

/****************************************************************************
  Parse a requirement type and value string into a requrement structure.
  Returns REQ_LAST on error.

  Pass this some values like "Building", "Factory".
****************************************************************************/
struct requirement req_from_str(const char *type, const char *value)
{
  struct requirement req;
  const struct government *pgov;

  assert(ARRAY_SIZE(req_type_names) == REQ_LAST);

  for (req.type = 0; req.type < ARRAY_SIZE(req_type_names); req.type++) {
    if (0 == mystrcasecmp(req_type_names[req.type], type)) {
      break;
    }
  }

  switch (req.type) {
  case REQ_NONE:
    return req;
  case REQ_TECH:
    req.value.tech = find_tech_by_name(value);
    if (req.value.tech != A_LAST) {
      return req;
    }
    break;
  case REQ_GOV:
    pgov = find_government_by_name(value);
    if (pgov != NULL) {
      req.value.gov = pgov->index;
      return req;
    }
    break;
  case REQ_BUILDING:
    req.value.building = find_improvement_by_name(value);
    if (req.value.building != B_LAST) {
      return req;
    }
    break;
  case REQ_SPECIAL:
    req.value.special = get_special_by_name(value);
    if (req.value.special != S_NO_SPECIAL) {
      return req;
    }
    break;
  case REQ_TERRAIN:
    req.value.terrain = get_terrain_by_name(value);
    if (req.value.terrain != T_UNKNOWN) {
      return req;
    }
    break;
  case REQ_LAST:
    break;
  }

  /* If we reach here there's been an error. */
  req.type = REQ_LAST;
  return req;
}

/****************************************************************************
  Return the value of a req as a serializable integer.  This is the opposite
  of req_set_value.
****************************************************************************/
int req_get_value(struct requirement *req)
{
  switch (req->type) {
  case REQ_NONE:
    return 0;
  case REQ_TECH:
    return req->value.tech;
  case REQ_GOV:
    return req->value.gov;
  case REQ_BUILDING:
    return req->value.building;
  case REQ_SPECIAL:
    return req->value.special;
  case REQ_TERRAIN:
    return req->value.terrain;
  case REQ_LAST:
    break;
  }

  assert(0);
  return 0;
}

/****************************************************************************
  Set the value of a req from a serializable integer.  This is the opposite
  of req_get_value.
****************************************************************************/
void req_set_value(struct requirement *req, int value)
{
  switch (req->type) {
  case REQ_NONE:
    return;
  case REQ_TECH:
    req->value.tech = value;
    return;
  case REQ_GOV:
    req->value.gov = value;
    return;
  case REQ_BUILDING:
    req->value.building = value;
    return;
  case REQ_SPECIAL:
    req->value.special = value;
    return;
  case REQ_TERRAIN:
    req->value.terrain = value;
    return;
  case REQ_LAST:
    return;
  }

  assert(0);
}

/****************************************************************************
  Checks the requirement to see if it is active on the given target.

  target gives the type of the target
  (player,city,building,tile) give the exact target
  source gives the source type of the effect
  peffect gives the exact effect value

  Make sure you give all aspects of the target when calling this function:
  for instance if you have TARGET_CITY pass the city's owner as the target
  player as well as the city itself as the target city.
****************************************************************************/
bool is_req_active(enum target_type target,
		   const struct player *target_player,
		   const struct city *target_city,
		   Impr_Type_id target_building,
		   const struct tile *target_tile,
		   const struct requirement *req)
{
  /* Note the target may actually not exist.  In particular, effects that
   * have a REQ_SPECIAL or REQ_TERRAIN may often be passed to this function
   * with a city as their target.  In this case the requirement is simply
   * not met. */
  switch (req->type) {
  case REQ_NONE:
    return TRUE;
  case REQ_TECH:
    /* The requirement is filled if the player owns the tech. */
    return (target_player
	    && get_invention(target_player, req->value.tech) == TECH_KNOWN);
  case REQ_GOV:
    /* The requirement is filled if the player is using the government. */
    return target_player && (target_player->government == req->value.gov);
  case REQ_BUILDING:
    /* The requirement is filled if there's at least one of the building
     * in the city.  (This is a slightly nonstandard use of
     * count_sources_in_range.) */
    return (count_sources_in_range(target, target_player, target_city,
				   target_building, EFR_CITY, FALSE,
				   req->value.building) > 0);
  case REQ_SPECIAL:
    /* The requirement is filled if the tile has the special. */
    return target_tile && tile_has_special(target_tile, req->value.special);
  case REQ_TERRAIN:
    /* The requirement is filled if the tile has the terrain. */
    return target_tile && (target_tile->terrain  == req->value.terrain);
  case REQ_LAST:
    break;
  }

  assert(0);
  return FALSE;
}
