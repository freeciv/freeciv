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

#ifndef FC__REQUIREMENTS_H
#define FC__REQUIREMENTS_H

#include "fc_types.h"
#include "tech.h"

/* The type of a requirement.  This must correspond to req_type_names[]
 * in requirements.c. */
enum req_type {
  REQ_NONE,
  REQ_TECH,
  REQ_GOV,
  REQ_BUILDING,
  REQ_SPECIAL,
  REQ_TERRAIN,
  REQ_LAST
};

/* Range of requirements.  This must correspond to req_range_names[]
 * in requirements.c. */
enum req_range {
  REQ_RANGE_LOCAL,
  REQ_RANGE_CITY,
  REQ_RANGE_CONTINENT,
  REQ_RANGE_PLAYER,
  REQ_RANGE_WORLD,
  REQ_RANGE_LAST   /* keep this last */
};

/* An requirement is targeted at a certain target type.  For building and
 * unit reqs the target will be a city; for tech reqs it will be a player;
 * for effect reqs it may be anything. */
enum target_type {
  TARGET_PLAYER,
  TARGET_CITY,
  TARGET_BUILDING 
};

/* A requirement. This requirement is basically a conditional; it may or
 * may not be active on a target.  If it is active then something happens.
 * For instance units and buildings have requirements to be built, techs
 * have requirements to be researched, and effects have requirements to be
 * active. */
struct requirement {
  enum req_type type;			/* requirement type */
  enum req_range range;			/* requirement range */
  bool survives; /* set if destroyed sources satisfy the req*/

  union {
    Tech_Type_id tech;			/* requirement tech */
    int gov;				/* requirement government */
    Impr_Type_id building;		/* requirement building */
    enum tile_special_type special;	/* requirement special */
    Terrain_type_id terrain;		/* requirement terrain type */
  } value;				/* requirement value */
};

enum req_range req_range_from_str(const char *str);
struct requirement req_from_str(const char *type, const char *range,
				bool survives, const char *value);

void req_get_values(struct requirement *req,
		    int *type, int *range, bool *survives, int *value);
struct requirement req_from_values(int type, int range,
				   bool survives, int value);

bool is_req_active(enum target_type target,
		   const struct player *target_player,
		   const struct city *target_city,
		   Impr_Type_id target_building,
		   const struct tile *target_tile,
		   const struct requirement *req);

int count_buildings_in_range(enum target_type target,
			     const struct player *target_player,
			     const struct city *target_city,
			     Impr_Type_id target_building,
			     enum req_range range, bool survives,
			     Impr_Type_id source);

#endif  /* FC__REQUIREMENTS_H */
