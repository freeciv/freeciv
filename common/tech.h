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

#include "shared.h"

#include "fc_types.h"
#include "nation.h" /* Nation_Type_id */

typedef int Tech_Type_id;
/*
  Above typedef replaces old "enum tech_type_id"; see comments about
  Unit_Type_id in unit.h, since mainly apply here too, except don't
  use Tech_Type_id very widely, and don't use (-1) flag values. (?)
*/

#define A_NONE 0
#define A_FIRST 1
#define A_LAST MAX_NUM_ITEMS
#define A_UNSET (A_LAST-1)
#define A_FUTURE (A_LAST-2)
#define A_NOINFO (A_LAST-3)
#define A_LAST_REAL A_NOINFO

/*
   A_NONE is the root tech. All players always know this tech. It is
   used as a flag in various cases where there is no tech-requirement.

   A_FIRST is the first real tech id value

   A_UNSET is a value which indicates that no tech is selected (for
   research).

   A_FUTURE is a value which indicates that the player is researching
   a future tech.

   A_LAST is a value which is guaranteed to be larger than all
   actual tech id values.  It is used as a flag value; it can
   also be used for fixed allocations to ensure able to hold
   full number of techs.
*/

enum tech_flag_id {
  TF_BONUS_TECH, /* player gets extra tech if rearched first */
  TF_BOAT_FAST,  /* all sea units get one extra move point */
  TF_BRIDGE,    /* "Settler" unit types can build bridges over rivers */
  TF_RAILROAD,  /* "Settler" unit types can build rail roads */
  TF_FORTRESS,  /* "Settler" unit types can build fortress */
  TF_WATCHTOWER, /* Units get enhanced visionrange in a fortress (=fortress acts also as a watchtower) */
  TF_POPULATION_POLLUTION_INC,  /* Increase the pollution factor created by popultaion by one */
  TF_TRADE_REVENUE_REDUCE, /* When known by the player establishing a trade route 
                              reduces the initial revenue by cumulative factors of 2/3 */
  TF_AIRBASE,   /* "Airbase" unit types can build Airbases */
  TF_FARMLAND,  /* "Settler" unit types can build farmland */
  TF_REDUCE_TRIREME_LOSS1, /* Reduces chance of Trireme being lost at sea */
  TF_REDUCE_TRIREME_LOSS2, /* Reduces chance of Trireme being lost at sea */
  TF_BUILD_AIRBORNE, /* Player can build air units */
  TF_LAST
};

enum tech_state {
  TECH_UNKNOWN = 0,
  TECH_KNOWN = 1,
  TECH_REACHABLE = 2
};

struct advance {
  const char *name; /* Translated string - doesn't need freeing. */
  char name_orig[MAX_LEN_NAME];	      /* untranslated */
  char graphic_str[MAX_LEN_NAME];	/* which named sprite to use */
  char graphic_alt[MAX_LEN_NAME];	/* alternate icon name */
  Tech_Type_id req[2];
  Tech_Type_id root_req;		/* A_NONE means unrestricted */
  unsigned int flags;
  char *helptext;

  struct Sprite *sprite;		/* icon of tech. */
	  
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

BV_DEFINE(tech_vector, A_LAST);

enum tech_state get_invention(const struct player *pplayer,
			      Tech_Type_id tech);
void set_invention(struct player *pplayer, Tech_Type_id tech,
		   enum tech_state value);
void update_research(struct player *pplayer);
Tech_Type_id get_next_tech(struct player *pplayer, Tech_Type_id goal);

bool tech_is_available(struct player *pplayer, Tech_Type_id id);
bool tech_exists(Tech_Type_id id);
Tech_Type_id find_tech_by_name(const char *s);
Tech_Type_id find_tech_by_name_orig(const char *s);

bool tech_flag(Tech_Type_id tech, enum tech_flag_id flag);
enum tech_flag_id tech_flag_from_str(const char *s);
Tech_Type_id find_tech_by_flag(int index, enum tech_flag_id flag);

int total_bulbs_required(struct player *pplayer);
int base_total_bulbs_required(struct player *pplayer,Tech_Type_id tech);
bool techs_have_fixed_costs(void);

int num_unknown_techs_for_goal(struct player *pplayer, Tech_Type_id goal);
int total_bulbs_required_for_goal(struct player *pplayer, Tech_Type_id goal);
bool is_tech_a_req_for_goal(struct player *pplayer, Tech_Type_id tech,
			   Tech_Type_id goal);
bool is_future_tech(Tech_Type_id tech);
const char *get_tech_name(struct player *pplayer, Tech_Type_id tech);

void precalc_tech_data(void);

void techs_free(void);

extern struct advance advances[];

/* This iterator iterates over almost all technologies.  It includes A_NONE
 * and non-existent technologies, but not A_FUTURE. */
#define tech_type_iterate(tech_id)                                          \
{                                                                           \
  Tech_Type_id tech_id;                                                     \
  for (tech_id = A_NONE; tech_id < game.num_tech_types; tech_id++) {

#define tech_type_iterate_end                                               \
  }                                                                         \
}

#endif  /* FC__TECH_H */
