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

struct player;

typedef int Tech_Type_id;
/*
  Above typedef replaces old "enum tech_type_id"; see comments about
  Unit_Type_id in unit.h, since mainly apply here too, except don't
  use Tech_Type_id very widely, and don't use (-1) flag values. (?)
*/

#define A_NONE 0
#define A_FIRST 1
#define A_LAST MAX_NUM_ITEMS
/*
   A_NONE is a special tech value, used as a flag in various
   cases where no tech is required.

   A_FIRST is the first real tech id value

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
  TF_LAST
};

enum tech_state {
  TECH_UNKNOWN = 0,
  TECH_KNOWN = 1,
  TECH_REACHABLE = 2
};

struct advance {
  char name[MAX_LEN_NAME];
  char name_orig[MAX_LEN_NAME];	      /* untranslated */
  int req[2];
  unsigned int flags;
  char *helptext;

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

enum tech_state get_invention(struct player *pplayer, Tech_Type_id tech);
void set_invention(struct player *pplayer, Tech_Type_id tech,
		   enum tech_state value);
void update_research(struct player *pplayer);
Tech_Type_id get_next_tech(struct player *pplayer, Tech_Type_id goal);

bool tech_exists(Tech_Type_id id);
Tech_Type_id find_tech_by_name(const char *s);

bool tech_flag(Tech_Type_id tech, enum tech_flag_id flag);
enum tech_flag_id tech_flag_from_str(char *s);
Tech_Type_id find_tech_by_flag(int index, enum tech_flag_id flag);

int tech_turns_to_advance(struct player *pplayer);

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

extern struct advance advances[];

#endif  /* FC__TECH_H */
