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
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "player.h"
#include "support.h"
#include "shared.h" /* ARRAY_SIZE */

#include "tech.h"

struct advance advances[A_LAST];
/* the advances array is now setup in:
   server/ruleset.c (for the server)
   client/packhand.c (for the client) */

static const char *flag_names[] = {
  "Bonus_Tech", "Boat_Fast", "Bridge", "Railroad", "Fortress",
  "Watchtower", "Population_Pollution_Inc", "Trade_Revenue_Reduce",
  "Airbase", "Farmland", "Reduce_Trireme_Loss1", "Reduce_Trireme_Loss2"
};
/* Note that these strings must correspond with the enums in tech_flag_id,
   in common/tech.h */

/**************************************************************************
...
**************************************************************************/
enum tech_state get_invention(struct player *pplayer, Tech_Type_id tech)
{
  assert(tech >= 0 || tech < game.num_tech_types);

  return pplayer->research.inventions[tech].state;
}

/**************************************************************************
...
**************************************************************************/
void set_invention(struct player *pplayer, Tech_Type_id tech,
		   enum tech_state value)
{
  if (pplayer->research.inventions[tech].state == value) {
    return;
  }

  pplayer->research.inventions[tech].state = value;

  if (value == TECH_KNOWN) {
    game.global_advances[tech]++;
  }
}

/**************************************************************************
  Returns if the given tech has to be researched to reach the
  goal. The goal itself isn't a requirement of itself.
**************************************************************************/
bool is_tech_a_req_for_goal(struct player *pplayer, Tech_Type_id tech,
			    Tech_Type_id goal)
{
  if (tech == goal) {
    return FALSE;
  } else {
    return TEST_BIT(pplayer->research.inventions[goal].
		    required_techs[tech / 8], tech % 8);
  }
}

/**************************************************************************
  Marks all techs which are requirements for goal in
  pplayer->research.inventions[goal].required_techs. Works recursive.
**************************************************************************/
static void build_required_techs_helper(struct player *pplayer,
					Tech_Type_id tech,
					Tech_Type_id goal)
{
  /* The is_tech_a_req_for_goal condition is true if the tech is
   * already marked */
  if (tech == A_NONE || !tech_exists(tech)
      || get_invention(pplayer, tech) == TECH_KNOWN
      || is_tech_a_req_for_goal(pplayer, tech, goal)) {
    return;
  }

  /* Mark the tech as required for the goal */
  pplayer->research.inventions[goal].required_techs[tech / 8] |=
      (1 << (tech % 8));

  build_required_techs_helper(pplayer, advances[tech].req[0], goal);
  build_required_techs_helper(pplayer, advances[tech].req[1], goal);
}

/**************************************************************************
  Updates required_techs, num_required_techs and bulbs_required in
  pplayer->research.inventions[goal].
**************************************************************************/
static void build_required_techs(struct player *pplayer, Tech_Type_id goal)
{
  Tech_Type_id i;
  int counter;

  memset(pplayer->research.inventions[goal].required_techs, 0,
	 sizeof(pplayer->research.inventions[goal].required_techs));

  if (get_invention(pplayer, goal) == TECH_KNOWN) {
    pplayer->research.inventions[goal].num_required_techs = 0;
    pplayer->research.inventions[goal].bulbs_required = 0;
    return;
  }
  
  build_required_techs_helper(pplayer, goal, goal);

  /* Include the goal tech */
  pplayer->research.inventions[goal].bulbs_required =
      base_total_bulbs_required(pplayer, goal);
  pplayer->research.inventions[goal].num_required_techs = 1;

  counter = 0;
  for (i = A_FIRST; i < game.num_tech_types; i++) {
    if (!is_tech_a_req_for_goal(pplayer, i, goal)) {
      continue;
    }

    /* 
     * This is needed to get a correct result for the
     * base_total_bulbs_required call.
     */
    pplayer->research.techs_researched++;
    counter++;

    pplayer->research.inventions[goal].num_required_techs++;
    pplayer->research.inventions[goal].bulbs_required +=
	base_total_bulbs_required(pplayer, i);
  }

  /* Undo the changes made above */
  pplayer->research.techs_researched -= counter;
}

/**************************************************************************
  Marks reachable techs. Calls build_required_techs to update the
  cache of requirements.
**************************************************************************/
void update_research(struct player *pplayer)
{
  Tech_Type_id i;

  for (i = 0; i < game.num_tech_types; i++) {
    if (!tech_exists(i)) {
      set_invention(pplayer, i, TECH_UNKNOWN);
    } else {
      if (get_invention(pplayer, i) == TECH_REACHABLE) {
	set_invention(pplayer, i, TECH_UNKNOWN);
      }

      if (get_invention(pplayer, i) == TECH_UNKNOWN
	  && get_invention(pplayer, advances[i].req[0]) == TECH_KNOWN
	  && get_invention(pplayer, advances[i].req[1]) == TECH_KNOWN) {
	set_invention(pplayer, i, TECH_REACHABLE);
      }
    }
    build_required_techs(pplayer, i);
  }
}

/**************************************************************************
...don't use this function directly, call get_next_tech instead.
**************************************************************************/
static Tech_Type_id get_next_tech_rec(struct player *pplayer,
				      Tech_Type_id goal)
{
  Tech_Type_id sub_goal;

  if (!tech_exists(goal) || get_invention(pplayer, goal) == TECH_KNOWN) {
    return A_NONE;
  }
  if (get_invention(pplayer, goal) == TECH_REACHABLE) {
    return goal;
  }
  sub_goal = get_next_tech_rec(pplayer, advances[goal].req[0]);
  if (sub_goal != A_NONE) {
    return sub_goal;
  } else {
    return get_next_tech_rec(pplayer, advances[goal].req[1]);
  }
}

/**************************************************************************
... this could be simpler, but we might have or get loops in the tech tree
    so i try to avoid endless loops.
    if return value > A_LAST then we have a bug
    caller should do something in that case.
**************************************************************************/
Tech_Type_id get_next_tech(struct player *pplayer, Tech_Type_id goal)
{
  if (goal == A_NONE || !tech_exists(goal) ||
      get_invention(pplayer, goal) == TECH_KNOWN) {
    return A_NONE;
  }
  return (get_next_tech_rec(pplayer, goal));
}

/**************************************************************************
Returns 1 if the tech "exists" in this game, 0 otherwise.
A tech doesn't exist if one of:
- id is out of range
- the tech has been flagged as removed by setting its req values
  to A_LAST (this function returns 0 if either req is A_LAST, rather
  than both, to be on the safe side)
**************************************************************************/
bool tech_exists(Tech_Type_id id)
{
  if (id<0 || id>=game.num_tech_types)
    return FALSE;
  else 
    return advances[id].req[0]!=A_LAST && advances[id].req[1]!=A_LAST;
}

/**************************************************************************
Does a linear search of advances[].name
Returns A_LAST if none match.
**************************************************************************/
Tech_Type_id find_tech_by_name(const char *s)
{
  Tech_Type_id i;

  for( i=0; i<game.num_tech_types; i++ ) {
    if (strcmp(advances[i].name, s)==0)
      return i;
  }
  return A_LAST;
}

/**************************************************************************
 Return TRUE if the tech has this flag otherwise FALSE
**************************************************************************/
bool tech_flag(Tech_Type_id tech, enum tech_flag_id flag)
{
  assert(flag>=0 && flag<TF_LAST);
  return TEST_BIT(advances[tech].flags, flag);
}

/**************************************************************************
 Convert flag names to enum; case insensitive;
 returns TF_LAST if can't match.
**************************************************************************/
enum tech_flag_id tech_flag_from_str(char *s)
{
  enum tech_flag_id i;

  assert(ARRAY_SIZE(flag_names) == TF_LAST);
  
  for(i=0; i<TF_LAST; i++) {
    if (mystrcasecmp(flag_names[i], s)==0) {
      return i;
    }
  }
  return TF_LAST;
}

/**************************************************************************
 Search for a tech with a given flag starting at index
 Returns A_LAST if no tech has been found
**************************************************************************/
Tech_Type_id find_tech_by_flag(int index, enum tech_flag_id flag)
{
  Tech_Type_id i;
  for(i=index;i<game.num_tech_types;i++)
  {
    if(tech_flag(i,flag)) return i;
  }
  return A_LAST;
}

/**************************************************************************
 Returns number of turns to advance (assuming current state of
 civilization).
**************************************************************************/
int tech_turns_to_advance(struct player *pplayer)
{
  /* The number of bulbs the civilization produces every turn. */
  int current_output = 0;

  city_list_iterate(pplayer->cities, pcity) {
    current_output += pcity->science_total;
  } city_list_iterate_end;

  if (current_output <= 0) {
    return FC_INFINITY;
  }

  return ((total_bulbs_required(pplayer) + current_output - 1)
	  / current_output);
}

/**************************************************************************
  Returns the number of bulbs which are required to finished the
  currently researched tech denoted by
  pplayer->research.researching. This is _NOT_ the number of bulbs
  which are left to get the advance. Use the term
  "total_bulbs_required(pplayer) - pplayer->research.bulbs_researched"
  if you want this.
**************************************************************************/
int total_bulbs_required(struct player *pplayer)
{
  return base_total_bulbs_required(pplayer, pplayer->research.researching);
}

/**************************************************************************
 Function to determine cost for technology. Equation is determined
 from game.rgame.tech_cost_style and game.rgame.tech_leakage.

 tech_cost_style:
 0 - Civ (I|II) style. Every new tech add game.researchcost to the
     cost of the next tech.
 1 - Cost of technology is (1+num_parents)*researchcost, where
     num_parents == number of requirement for tech, counted recursively.
 2 - Cost are read from tech.ruleset. Missing costs are generated by
     style 1.

 tech_leakage:
 0 - No reduction of the technology cost.
 1 - Technology cost is reduced depending on the number of players
     which already know the tech and you have an embassy with.
 2 - Technology cost is reduced depending on the number of all players
     (human, AI and barbarians) which already know the tech.
 3 - Technology cost is reduced depending on the number of normal
     players (human and AI) which already know the tech.
**************************************************************************/
int base_total_bulbs_required(struct player *pplayer, Tech_Type_id tech)
{
  int cost, tech_cost_style = game.rgame.tech_cost_style;

  if (!is_future_tech(tech) && get_invention(pplayer, tech) == TECH_KNOWN) {
    /* A non-future tech which is already known costs nothing. */
    return 0;
  }

  if (is_future_tech(tech)) {
    /* Future techs use style 0 */
    tech_cost_style = 0;
  }

  if (tech_cost_style == 2 && advances[tech].preset_cost == -1) {
    /* No preset, using style 1 */
    tech_cost_style = 1;
  }

  switch (tech_cost_style) {
  case 0:
    cost = pplayer->research.techs_researched * game.researchcost;
    break;
  case 1:
    cost = advances[tech].num_reqs * game.researchcost;
    break;
  case 2:
    cost = (advances[tech].preset_cost * game.researchcost) /
	GAME_DEFAULT_RESEARCHCOST;
    break;
  default:
    freelog(LOG_ERROR, "Invalid tech_cost_style %d %d",
	    game.rgame.tech_cost_style, tech_cost_style);
    assert(0);
    exit(EXIT_FAILURE);
  }

  /* Research becomes more expensive. */
  if (game.year > 0) {
    cost *= 2;
  }

  switch (game.rgame.tech_leakage) {
  case 0:
    /* no change */
    break;

  case 1:
    {
      int players = 0, players_with_tech_and_embassy = 0;

      players_iterate(other) {
	players++;
	if (get_invention(other, tech) == TECH_KNOWN
	    && player_has_embassy(pplayer, other)) {
	  players_with_tech_and_embassy++;
	}
      } players_iterate_end;

      cost = ((players - players_with_tech_and_embassy) * cost) / players;
    }
    break;

  case 2:
    {
      int players = 0, players_with_tech = 0;

      players_iterate(other) {
	players++;
	if (get_invention(other, tech) == TECH_KNOWN) {
	  players_with_tech++;
	}
      } players_iterate_end;

      cost = ((players - players_with_tech) * cost) / players;
    }
    break;

  case 3:
    {
      int players = 0, players_with_tech = 0;

      players_iterate(other) {
	if (is_barbarian(other)) {
	  continue;
	}
	players++;
	if (get_invention(other, tech) == TECH_KNOWN) {
	  players_with_tech++;
	}
      } players_iterate_end;

      cost = ((players - players_with_tech) * cost) / players;
    }
    break;

  default:
    freelog(LOG_ERROR, "Invalid tech_leakage %d", game.rgame.tech_leakage);
    assert(0);
    exit(EXIT_FAILURE);
  }

  /* If we have many players, tech cost may drop to 0.  */
  if (cost == 0) {
    cost = 1;
  }

  return cost;
}

/**************************************************************************
 Returns the number of technologies the player need to research to get
 the goal technology. This includes the goal technology. Technologies
 are only counted once.
**************************************************************************/
int num_unknown_techs_for_goal(struct player *pplayer, Tech_Type_id goal)
{
  return pplayer->research.inventions[goal].num_required_techs;
}

/**************************************************************************
 Function to determine cost (in bulbs) of reaching goal
 technology. These costs _include_ the cost for researching the goal
 technology itself.
**************************************************************************/
int total_bulbs_required_for_goal(struct player *pplayer,
				  Tech_Type_id goal)
{
  return pplayer->research.inventions[goal].bulbs_required;
}

/**************************************************************************
 Returns number of requirements for the given tech. To not count techs
 double a memory (the counted array) is needed.
**************************************************************************/
static int precalc_tech_data_helper(Tech_Type_id tech, bool *counted)
{
  if (tech == A_NONE || !tech_exists(tech) || counted[tech]) {
    return 0;
  }

  counted[tech] = TRUE;

  return 1 + 
      precalc_tech_data_helper(advances[tech].req[0], counted)+ 
      precalc_tech_data_helper(advances[tech].req[1], counted);
}

/**************************************************************************
 Function to precalculate needed data for technologies.
**************************************************************************/
void precalc_tech_data()
{
  Tech_Type_id tech;
  bool counted[A_LAST];

  for (tech = A_FIRST; tech < game.num_tech_types; tech++) {
    memset(counted, 0, sizeof(counted));
    advances[tech].num_reqs = precalc_tech_data_helper(tech, counted);
  }
}

/**************************************************************************
 Is the given tech a future tech.
**************************************************************************/
bool is_future_tech(Tech_Type_id tech)
{
  /*
   * Future techs can be identify in two ways: the "tech >=
   * game.num_tech_types" condition and the "tech == A_NONE"
   * condition. FIXME: clean this up.
   */
  return (tech >= game.num_tech_types) || (tech == A_NONE);
}

/**************************************************************************
 Return the name of the given tech. You don't have to free the return
 pointer.
**************************************************************************/
const char *get_tech_name(struct player *pplayer, Tech_Type_id tech)
{
  static char buffer[200];

  if (!is_future_tech(tech)) {
    my_snprintf(buffer, sizeof(buffer), "%s", advances[tech].name);
  } else {
    my_snprintf(buffer, sizeof(buffer), _("Future Tech. %d"),
		pplayer->future_tech + 1);
  }
  return buffer;
}

/**************************************************************************
 Returns true if the costs for the given technology will stay constant
 during the game. False otherwise.
**************************************************************************/
bool techs_have_fixed_costs()
{
  return ((game.rgame.tech_cost_style == 1
	   || game.rgame.tech_cost_style == 2)
	  && game.rgame.tech_leakage == 0);
}
