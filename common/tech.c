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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "game.h"
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
enum tech_state get_invention(struct player *plr, Tech_Type_id tech)
{
  if(!tech_exists(tech))
    return TECH_UNKNOWN;
  return plr->research.inventions[tech];
}

/**************************************************************************
...
**************************************************************************/
void set_invention(struct player *plr, Tech_Type_id tech,
		   enum tech_state value)
{
  if(plr->research.inventions[tech]==value)
    return;

  plr->research.inventions[tech]=value;

  if(value==TECH_KNOWN) {
    game.global_advances[tech]++;
  }
}


/**************************************************************************
...
**************************************************************************/
void update_research(struct player *plr)
{
  Tech_Type_id i;
  
  for (i=0;i<game.num_tech_types;i++) {
    enum tech_state known;

    if(!tech_exists(i)) {
      plr->research.inventions[i]=TECH_UNKNOWN;
      continue;
    }
    known=get_invention(plr, i);
    if (known == TECH_REACHABLE || known == TECH_MARKED)
      plr->research.inventions[i]=TECH_UNKNOWN;
    if (get_invention(plr, i) == TECH_UNKNOWN
       && get_invention(plr, advances[i].req[0])==TECH_KNOWN   
       && get_invention(plr, advances[i].req[1])==TECH_KNOWN)
      plr->research.inventions[i]=TECH_REACHABLE;
  }
}

/**************************************************************************
  We mark nodes visited so we won't count them more than once, this
  function isn't to be called direct, use num_unknown_techs_for_goal
  instead.
**************************************************************************/
static int num_unknown_techs_for_goal_helper(struct player *plr,
					     Tech_Type_id goal)
{
  if (goal == A_NONE || !tech_exists(goal) ||
      get_invention(plr, goal) == TECH_KNOWN || 
      get_invention(plr, goal) == TECH_MARKED) 
    return 0; 
  set_invention(plr, goal, TECH_MARKED);
  return (num_unknown_techs_for_goal_helper(plr, advances[goal].req[0]) +
	  num_unknown_techs_for_goal_helper(plr, advances[goal].req[1]) + 
	  1);
}

/**************************************************************************
 Returns the number of techs the player need to research to get the goal
 tech, techs are only counted once.
**************************************************************************/
int num_unknown_techs_for_goal(struct player *plr, Tech_Type_id goal)
{
  int result = num_unknown_techs_for_goal_helper(plr, goal);

  update_research(plr);
  return result;
}


/**************************************************************************
...don't use this function directly, call get_next_tech instead.
**************************************************************************/
static Tech_Type_id get_next_tech_rec(struct player *plr, Tech_Type_id goal)
{
  Tech_Type_id sub_goal;
  if (!tech_exists(goal) || get_invention(plr, goal) == TECH_KNOWN)
    return A_NONE;
  if (get_invention(plr, goal) == TECH_REACHABLE)
    return goal;
  sub_goal = get_next_tech_rec(plr, advances[goal].req[0]);
  if (sub_goal != A_NONE)
    return sub_goal;
  else
    return get_next_tech_rec(plr, advances[goal].req[1]);
}

/**************************************************************************
... this could be simpler, but we might have or get loops in the tech tree
    so i try to avoid endless loops.
    if return value > A_LAST then we have a bug
    caller should do something in that case.
**************************************************************************/
Tech_Type_id get_next_tech(struct player *plr, Tech_Type_id goal)
{
  if (goal == A_NONE || !tech_exists(goal) ||
      get_invention(plr, goal) == TECH_KNOWN) 
    return A_NONE; 
  return (get_next_tech_rec(plr, goal));
}


/**************************************************************************
Returns 1 if the tech "exists" in this game, 0 otherwise.
A tech doesn't exist if one of:
- id is out of range
- the tech has been flagged as removed by setting its req values
  to A_LAST (this function returns 0 if either req is A_LAST, rather
  than both, to be on the safe side)
**************************************************************************/
int tech_exists(Tech_Type_id id)
{
  if (id<0 || id>=game.num_tech_types)
    return 0;
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
int tech_flag(Tech_Type_id tech, enum tech_flag_id flag)
{
  assert(flag>=0 && flag<TF_LAST);
  return BOOL_VAL(advances[tech].flags & (1<<flag));
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
    return INFINITY;
  }

  return ((total_bulbs_required(pplayer) + current_output - 1)
	  / current_output);
}
