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
#ifndef FC__TECH_H
#define FC__TECH_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* utility */
#include "bitvector.h"
#include "shared.h"

/* common */
#include "fc_types.h"
#include "name_translation.h"

struct strvec;          /* Actually defined in "utility/string_vector.h". */

/*
  [kept for amusement and posterity]
typedef int Tech_type_id;
  Above typedef replaces old "enum tech_type_id"; see comments about
  Unit_type_id in unit.h, since mainly apply here too, except don't
  use Tech_type_id very widely, and don't use (-1) flag values. (?)
*/
/* [more accurately]
 * Unlike most other indices, the Tech_type_id is widely used, because it 
 * so frequently passed to packet and scripting.  The client menu routines 
 * sometimes add and substract these numbers.
 */
#define A_NONE 0
#define A_FIRST 1
#define A_LAST MAX_NUM_ITEMS
#define A_UNSET (A_LAST-1)
#define A_FUTURE (A_LAST-2)
#define A_UNKNOWN (A_LAST-3)
#define A_LAST_REAL A_UNKNOWN

#define A_NEVER (NULL)

/*
   A_NONE is the root tech. All players always know this tech. It is
   used as a flag in various cases where there is no tech-requirement.

   A_FIRST is the first real tech id value

   A_UNSET indicates that no tech is selected (for research).

   A_FUTURE indicates that the player is researching a future tech.

   A_UNKNOWN may be passed to other players instead of the actual value.

   A_LAST is a value that is guaranteed to be larger than all
   actual Tech_type_id values.  It is used as a flag value; it can
   also be used for fixed allocations to ensure ability to hold the
   full number of techs.

   A_NEVER is the pointer equivalent replacement for A_LAST flag value.
*/

/* Changing these breaks network compatibility. */
/* If a new flag is added techtools.c:player_tech_lost() should be checked */
#define SPECENUM_NAME tech_flag_id
/* player gets extra tech if rearched first */
#define SPECENUM_VALUE0 TF_BONUS_TECH
#define SPECENUM_VALUE0NAME "Bonus_Tech"
/* "Settler" unit types can build bridges over rivers */
#define SPECENUM_VALUE1 TF_BRIDGE
#define SPECENUM_VALUE1NAME "Bridge"
/* "Settler" unit types can build rail roads */
#define SPECENUM_VALUE2 TF_RAILROAD
#define SPECENUM_VALUE2NAME "Railroad"
/* Increase the pollution factor created by population by one */
#define SPECENUM_VALUE3 TF_POPULATION_POLLUTION_INC
#define SPECENUM_VALUE3NAME "Population_Pollution_Inc"
/* "Settler" unit types can build farmland */
#define SPECENUM_VALUE4 TF_FARMLAND
#define SPECENUM_VALUE4NAME "Farmland"
/* Player can build air units */
#define SPECENUM_VALUE5 TF_BUILD_AIRBORNE
#define SPECENUM_VALUE5NAME "Build_Airborne"
/* Keep this last. */
#define SPECENUM_COUNT TF_COUNT
#include "specenum_gen.h"

BV_DEFINE(bv_tech_flags, TF_COUNT);

/* TECH_KNOWN is self-explanatory, TECH_PREREQS_KNOWN are those for which all 
 * requirements are fulfilled; all others (including those which can never 
 * be reached) are TECH_UNKNOWN */
#define SPECENUM_NAME tech_state
/* TECH_UNKNOWN must be 0 as the code does no special initialisation after
 * memset(0), See player_researches_init(). */
#define SPECENUM_VALUE0 TECH_UNKNOWN
#define SPECENUM_VALUE1 TECH_PREREQS_KNOWN
#define SPECENUM_VALUE2 TECH_KNOWN
#include "specenum_gen.h"

enum tech_req {
  AR_ONE = 0,
  AR_TWO = 1,
  AR_ROOT = 2,
  AR_SIZE
};

struct advance {
  Tech_type_id item_number;
  struct name_translation name;
  char graphic_str[MAX_LEN_NAME];	/* which named sprite to use */
  char graphic_alt[MAX_LEN_NAME];	/* alternate icon name */

  struct advance *require[AR_SIZE];
  bv_tech_flags flags;
  struct strvec *helptext;

  /* 
   * Message displayed to the first player to get a bonus tech 
   */
  char *bonus_message;

  /* 
   * Cost of advance in bulbs as specified in ruleset. -1 means that
   * no value was set in ruleset. Server send this to client.
   */
  int preset_cost;

  /* 
   * Number of requirements this technology has _including_
   * itself. Precalculated at server then send to client.
   */
  int num_reqs;
};

BV_DEFINE(bv_techs, A_LAST);

/* General advance/technology accessor functions. */
Tech_type_id advance_count(void);
Tech_type_id advance_index(const struct advance *padvance);
Tech_type_id advance_number(const struct advance *padvance);

struct advance *advance_by_number(const Tech_type_id atype);

struct advance *valid_advance(struct advance *padvance);
struct advance *valid_advance_by_number(const Tech_type_id atype);

struct advance *advance_by_rule_name(const char *name);
struct advance *advance_by_translated_name(const char *name);

const char *advance_name_by_player(const struct player *pplayer,
				   Tech_type_id tech);
const char *advance_name_for_player(const struct player *pplayer,
				    Tech_type_id tech);
const char *advance_name_researching(const struct player *pplayer);

const char *advance_rule_name(const struct advance *padvance);
const char *advance_name_translation(const struct advance *padvance);

/* General advance/technology flag accessor routines */
bool advance_has_flag(Tech_type_id tech, enum tech_flag_id flag);

/* FIXME: oddball function used in one place */
Tech_type_id advance_by_flag(Tech_type_id index, enum tech_flag_id flag);

/* Ancillary routines */
enum tech_state player_invention_state(const struct player *pplayer,
				       Tech_type_id tech);
enum tech_state player_invention_set(struct player *pplayer,
				     Tech_type_id tech,
				     enum tech_state value);
bool player_invention_reachable(const struct player *pplayer,
                                const Tech_type_id tech,
                                bool allow_prereqs);

Tech_type_id player_research_step(const struct player *pplayer,
				  Tech_type_id goal);
void player_research_update(struct player *pplayer);

Tech_type_id advance_required(const Tech_type_id tech,
			      enum tech_req require);
struct advance *advance_requires(const struct advance *padvance,
				 enum tech_req require);

int total_bulbs_required(const struct player *pplayer);
int base_total_bulbs_required(const struct player *pplayer,
			      Tech_type_id tech);
bool techs_have_fixed_costs(void);

int num_unknown_techs_for_goal(const struct player *pplayer,
			       Tech_type_id goal);
int total_bulbs_required_for_goal(const struct player *pplayer,
				  Tech_type_id goal);
bool is_tech_a_req_for_goal(const struct player *pplayer,
			    Tech_type_id tech,
			    Tech_type_id goal);
bool is_future_tech(Tech_type_id tech);

void precalc_tech_data(void);

/* Initialization and iteration */
void techs_init(void);
void techs_free(void);

/* This iterates over almost all technologies.  It includes non-existent
 * technologies, but not A_FUTURE. */
#define advance_index_iterate(_start, _index)				\
{									\
  Tech_type_id _index = (_start);					\
  for (; _index < advance_count(); _index++) {

#define advance_index_iterate_end					\
  }									\
}

const struct advance *advance_array_last(void);

#define advance_iterate(_start, _p)					\
{									\
  struct advance *_p = advance_by_number(_start);			\
  if (NULL != _p) {							\
    for (; _p <= advance_array_last(); _p++) {

#define advance_iterate_end						\
    }									\
  }									\
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__TECH_H */
