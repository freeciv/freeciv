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

typedef int Tech_type_id;
/*
  Above typedef replaces old "enum tech_type_id"; see comments about
  Unit_type_id in unit.h, since mainly apply here too, except don't
  use Tech_type_id very widely, and don't use (-1) flag values. (?)
*/

#define A_NONE 0
#define A_FIRST 1
#define A_LAST MAX_NUM_ITEMS
#define A_UNSET (A_LAST-1)
#define A_FUTURE (A_LAST-2)
#define A_UNKNOWN (A_LAST-3)
#define A_LAST_REAL A_UNKNOWN

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

/* Changing these breaks network compatibility. */
enum tech_flag_id {
  TF_BONUS_TECH, /* player gets extra tech if rearched first */
  TF_BRIDGE,    /* "Settler" unit types can build bridges over rivers */
  TF_RAILROAD,  /* "Settler" unit types can build rail roads */
  TF_POPULATION_POLLUTION_INC,  /* Increase the pollution factor created by popultaion by one */
  TF_FARMLAND,  /* "Settler" unit types can build farmland */
  TF_BUILD_AIRBORNE, /* Player can build air units */
  TF_LAST
};

/* TECH_KNOWN is self-explanatory, TECH_REACHABLE are those for which all 
 * requirements are fulfilled; all others (including those which can never 
 * be reached) are TECH_UNKNOWN */
enum tech_state {
  TECH_UNKNOWN = 0,
  TECH_KNOWN = 1,
  TECH_REACHABLE = 2
};

struct advance {
  int index; /* Tech index in tech array. */
  const char *name_translated;		/* string doesn't need freeing */
  char name_rule[MAX_LEN_NAME];		/* original name for comparisons */
  char graphic_str[MAX_LEN_NAME];	/* which named sprite to use */
  char graphic_alt[MAX_LEN_NAME];	/* alternate icon name */
  Tech_type_id req[2];
  Tech_type_id root_req;		/* A_NONE means unrestricted */
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

BV_DEFINE(tech_vector, A_LAST);

struct player_research {
  /* The number of techs and future techs the player has
   * researched/acquired. */
  int techs_researched, future_tech;

  /* Invention being researched in. Valid values for researching are:
   *  - any existing tech but not A_NONE or
   *  - A_FUTURE.
   * In addition A_UNKNOWN is allowed at the client for enemies.
   *
   * bulbs_researched tracks how many bulbs have been accumulated toward
   * this research target. */
  Tech_type_id researching;        
  int bulbs_researched;

  /* If the player changes his research target in a turn, he loses some or
   * all of the bulbs he's accumulated toward that target.  We save the
   * original info from the start of the turn so that if he changes back
   * he will get the bulbs back. */
  Tech_type_id changed_from;
  int bulbs_researched_before;

  /* If the player completed a research this turn, this value is turned on
   * and changing targets may be done without penalty. */
  bool got_tech;

  struct {
    /* One of TECH_UNKNOWN, TECH_KNOWN or TECH_REACHABLE. */
    enum tech_state state;

    /* 
     * required_techs, num_required_techs and bulbs_required are
     * cached values. Updated from build_required_techs (which is
     * called by update_research).
     */
    tech_vector required_techs;
    int num_required_techs, bulbs_required;
  } inventions[A_LAST];

  /* Tech goal (similar to worklists; when one tech is researched the next
   * tech toward the goal will be chosen).  May be A_NONE. */
  Tech_type_id tech_goal;

  /*
   * Cached values. Updated by update_research.
   */
  int num_known_tech_with_flag[TF_LAST];
};

void player_research_init(struct player_research* research);

enum tech_state get_invention(const struct player *pplayer,
			      Tech_type_id tech);
void set_invention(struct player *pplayer, Tech_type_id tech,
		   enum tech_state value);
void update_research(struct player *pplayer);
Tech_type_id get_next_tech(const struct player *pplayer, Tech_type_id goal);

bool tech_is_available(const struct player *pplayer, Tech_type_id id);
bool tech_exists(Tech_type_id id);

Tech_type_id find_tech_by_rule_name(const char *s);
Tech_type_id find_tech_by_translated_name(const char *s);

bool tech_has_flag(Tech_type_id tech, enum tech_flag_id flag);
enum tech_flag_id tech_flag_from_str(const char *s);
Tech_type_id find_tech_by_flag(int index, enum tech_flag_id flag);

int total_bulbs_required(const struct player *pplayer);
int base_total_bulbs_required(const struct player *pplayer,
			      Tech_type_id tech);
bool techs_have_fixed_costs(void);

int num_unknown_techs_for_goal(const struct player *pplayer,
			       Tech_type_id goal);
int total_bulbs_required_for_goal(const struct player *pplayer,
				  Tech_type_id goal);
bool is_tech_a_req_for_goal(const struct player *pplayer, Tech_type_id tech,
			    Tech_type_id goal);
bool is_future_tech(Tech_type_id tech);
const char *advance_name_by_player(const struct player *pplayer, Tech_type_id tech);
const char *advance_name_for_player(const struct player *pplayer, Tech_type_id tech);
const char *advance_name_researching(const struct player *pplayer);

const char *advance_rule_name(Tech_type_id tech);
const char *advance_name_translation(Tech_type_id tech);

void precalc_tech_data(void);

void techs_init(void);
void techs_free(void);

extern struct advance advances[];

/* This iterator iterates over almost all technologies.  It includes A_NONE
 * and non-existent technologies, but not A_FUTURE. */
#define tech_type_iterate(tech_id)                                          \
{                                                                           \
  Tech_type_id tech_id;                                                     \
  for (tech_id = A_NONE; tech_id < game.control.num_tech_types; tech_id++) {

#define tech_type_iterate_end                                               \
  }                                                                         \
}

#endif  /* FC__TECH_H */
