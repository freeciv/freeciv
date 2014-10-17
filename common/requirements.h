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
 * Used in the network protocol.
 * Order is important -- wider ranges should come later -- some code
 * assumes a total order, or tests for e.g. >= REQ_RANGE_PLAYER.
 * Ranges of similar types should be supersets, for example:
 *  - the set of Adjacent tiles contains the set of CAdjacent tiles,
 *    and both contain the center Local tile (a requirement on the local
 *    tile is also within Adjacent range);
 *  - World contains Alliance contains Player (a requirement we ourselves
 *    have is also within Alliance range). */
#define SPECENUM_NAME req_range
#define SPECENUM_VALUE0 REQ_RANGE_LOCAL
#define SPECENUM_VALUE0NAME "Local"
#define SPECENUM_VALUE1 REQ_RANGE_CADJACENT
#define SPECENUM_VALUE1NAME "CAdjacent"
#define SPECENUM_VALUE2 REQ_RANGE_ADJACENT
#define SPECENUM_VALUE2NAME "Adjacent"
#define SPECENUM_VALUE3 REQ_RANGE_CITY
#define SPECENUM_VALUE3NAME "City"
#define SPECENUM_VALUE4 REQ_RANGE_TRADEROUTE
#define SPECENUM_VALUE4NAME "Traderoute"
#define SPECENUM_VALUE5 REQ_RANGE_CONTINENT
#define SPECENUM_VALUE5NAME "Continent"
#define SPECENUM_VALUE6 REQ_RANGE_PLAYER
#define SPECENUM_VALUE6NAME "Player"
#define SPECENUM_VALUE7 REQ_RANGE_TEAM
#define SPECENUM_VALUE7NAME "Team"
#define SPECENUM_VALUE8 REQ_RANGE_ALLIANCE
#define SPECENUM_VALUE8NAME "Alliance"
#define SPECENUM_VALUE9 REQ_RANGE_WORLD
#define SPECENUM_VALUE9NAME "World"
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
  bool present;	 /* set if the requirement is to be present */
};

#define SPECVEC_TAG requirement
#define SPECVEC_TYPE struct requirement
#include "specvec.h"
#define requirement_vector_iterate(req_vec, preq) \
  TYPED_VECTOR_ITERATE(struct requirement, req_vec, preq)
#define requirement_vector_iterate_end VECTOR_ITERATE_END

/* General requirement functions. */
struct requirement req_from_str(const char *type, const char *range,
				bool survives, bool present,
				const char *value);
const char *req_to_fstring(const struct requirement *req);

void req_get_values(const struct requirement *req, int *type,
		    int *range, bool *survives, bool *present,
		    int *value);
struct requirement req_from_values(int type, int range,
				   bool survives, bool present, int value);

bool are_requirements_equal(const struct requirement *req1,
			    const struct requirement *req2);

bool are_requirements_opposites(const struct requirement *req1,
                                const struct requirement *req2);

bool are_requirements_contradictions(const struct requirement *req1,
                                     const struct requirement *req2);

bool is_req_active(const struct player *target_player,
		   const struct player *other_player,
		   const struct city *target_city,
		   const struct impr_type *target_building,
		   const struct tile *target_tile,
                   const struct unit *target_unit,
                   const struct unit_type *target_unittype,
		   const struct output_type *target_output,
		   const struct specialist *target_specialist,
		   const struct requirement *req,
                   const enum   req_problem_type prob_type);
bool are_reqs_active(const struct player *target_player,
		     const struct player *other_player,
		     const struct city *target_city,
		     const struct impr_type *target_building,
		     const struct tile *target_tile,
                     const struct unit *target_unit,
		     const struct unit_type *target_unittype,
		     const struct output_type *target_output,
		     const struct specialist *target_specialist,
		     const struct requirement_vector *reqs,
                     const enum   req_problem_type prob_type);

bool is_req_unchanging(const struct requirement *req);

bool is_req_in_vec(const struct requirement *req,
                   const struct requirement_vector *vec);

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

void universal_found_functions_init(void);
bool universal_fulfills_requirement(bool check_necessary,
                                    const struct requirement_vector *reqs,
                                    const struct universal *source);

/* Accessors to determine if a universal fulfills a requirement vector.
 * When adding an additional accessor, be sure to add the appropriate
 * item_found function in universal_found_callbacks_init(). */
#define requirement_fulfilled_by_government(_gov_, _rqs_)                  \
  universal_fulfills_requirement(FALSE, (_rqs_),                           \
      &(struct universal){.kind = VUT_GOVERNMENT, .value = {.govern = (_gov_)}})
#define requirement_fulfilled_by_improvement(_imp_, _rqs_)                 \
  universal_fulfills_requirement(FALSE, (_rqs_),                           \
    &(struct universal){.kind = VUT_IMPROVEMENT,                           \
                        .value = {.building = (_imp_)}})
#define requirement_fulfilled_by_unit_class(_uc_, _rqs_)                   \
  universal_fulfills_requirement(FALSE, (_rqs_),                           \
      &(struct universal){.kind = VUT_UCLASS, .value = {.uclass = (_uc_)}})
#define requirement_fulfilled_by_unit_type(_ut_, _rqs_)                    \
  universal_fulfills_requirement(FALSE, (_rqs_),                           \
      &(struct universal){.kind = VUT_UTYPE, .value = {.utype = (_ut_)}})

#define requirement_needs_improvement(_imp_, _rqs_)                        \
  universal_fulfills_requirement(TRUE, (_rqs_),                            \
    &(struct universal){.kind = VUT_IMPROVEMENT,                           \
                        .value = {.building = (_imp_)}})

int requirement_kind_state_pos(const int id,
                               const enum req_range range,
                               const bool present,
                               const int count_id);

#define requirement_unit_state_pos(_id_, _present_)                       \
  requirement_kind_state_pos(_id_, REQ_RANGE_LOCAL, _present_, USP_COUNT)

#define requirement_diplrel_ereq(_id_, _range_, _present_)                \
  requirement_kind_state_pos(_id_, _range_, _present_, DRO_LAST)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__REQUIREMENTS_H */
