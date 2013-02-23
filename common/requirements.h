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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "fc_types.h"

#include "tech.h"
#include "terrain.h"
#include "unittype.h"

/* Range of requirements.
 * Used in the network protocol. */
#define SPECENUM_NAME req_range
#define SPECENUM_VALUE0 REQ_RANGE_LOCAL
#define SPECENUM_VALUE0NAME "Local"
#define SPECENUM_VALUE1 REQ_RANGE_CADJACENT
#define SPECENUM_VALUE1NAME "CAdjacent"
#define SPECENUM_VALUE2 REQ_RANGE_ADJACENT
#define SPECENUM_VALUE2NAME "Adjacent"
#define SPECENUM_VALUE3 REQ_RANGE_CITY
#define SPECENUM_VALUE3NAME "City"
#define SPECENUM_VALUE4 REQ_RANGE_CONTINENT
#define SPECENUM_VALUE4NAME "Continent"
#define SPECENUM_VALUE5 REQ_RANGE_PLAYER
#define SPECENUM_VALUE5NAME "Player"
#define SPECENUM_VALUE6 REQ_RANGE_WORLD
#define SPECENUM_VALUE6NAME "World"
#define SPECENUM_COUNT REQ_RANGE_COUNT /* keep this last */
#include "specenum_gen.h"

/* A requirement. This requirement is basically a conditional; it may or
 * may not be active on a target.  If it is active then something happens.
 * For instance units and buildings have requirements to be built, techs
 * have requirements to be researched, and effects have requirements to be
 * active.
 * Used in the network protocol. */
struct requirement {
  struct universal source;		/* requirement source */
  enum req_range range;			/* requirement range */
  bool survives; /* set if destroyed sources satisfy the req*/
  bool negated;	 /* set if the requirement is to be negated */
};

#define SPECLIST_TAG requirement
#define SPECLIST_TYPE struct requirement
#include "speclist.h"
#define requirement_list_iterate(req_list, preq) \
  TYPED_LIST_ITERATE(struct requirement, req_list, preq)
#define requirement_list_iterate_end LIST_ITERATE_END

#define SPECVEC_TAG requirement
#define SPECVEC_TYPE struct requirement
#include "specvec.h"
#define requirement_vector_iterate(req_vec, preq) \
  TYPED_VECTOR_ITERATE(struct requirement, req_vec, preq)
#define requirement_vector_iterate_end VECTOR_ITERATE_END

/* General requirement functions. */
struct requirement req_from_str(const char *type, const char *range,
				bool survives, bool negated,
				const char *value);

void req_get_values(const struct requirement *req, int *type,
		    int *range, bool *survives, bool *negated,
		    int *value);
struct requirement req_from_values(int type, int range,
				   bool survives, bool negated, int value);

bool are_requirements_equal(const struct requirement *req1,
			    const struct requirement *req2);

bool is_req_active(const struct player *target_player,
		   const struct city *target_city,
		   const struct impr_type *target_building,
		   const struct tile *target_tile,
		   const struct unit_type *target_unittype,
		   const struct output_type *target_output,
		   const struct specialist *target_specialist,
		   const struct requirement *req,
                   const enum   req_problem_type prob_type);
bool are_reqs_active(const struct player *target_player,
		     const struct city *target_city,
		     const struct impr_type *target_building,
		     const struct tile *target_tile,
		     const struct unit_type *target_unittype,
		     const struct output_type *target_output,
		     const struct specialist *target_specialist,
		     const struct requirement_vector *reqs,
                     const enum   req_problem_type prob_type);

bool is_req_unchanging(const struct requirement *req);

/* General universal functions. */
int universal_number(const struct universal *source);

struct universal universal_by_number(const enum universals_n kind,
				     const int value);
struct universal universal_by_rule_name(const char *kind,
					const char *value);
void universal_extraction(const struct universal *source,
			  int *kind, int *value);

bool are_universals_equal(const struct universal *psource1,
			  const struct universal *psource2);

const char *universal_rule_name(const struct universal *psource);
const char *universal_name_translation(const struct universal *psource,
				       char *buf, size_t bufsz);
const char *universal_type_rule_name(const struct universal *psource);

int universal_build_shield_cost(const struct universal *target);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__REQUIREMENTS_H */
