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

#include <string.h>

#include "game.h"
#include "government.h"
#include "log.h"
#include "player.h"
#include "tech.h"

#include "plrhand.h"

#include "advmilitary.h"
#include "aitools.h"

#include "aitech.h"

struct ai_tech_choice {
  Tech_Type_id choice;   /* The id of the most needed tech */
  int want;              /* Want of the most needed tech */
  int current_want;      /* Want of the tech which currenlty researched 
			  * or is our current goal */
};

/**************************************************************************
  Returns tech corresponding to players wonder goal from nations[],
  if it makes sense, and wonder is not already built and not obsolete.
  Otherwise returns A_UNSET.
**************************************************************************/
static Tech_Type_id get_wonder_tech(struct player *plr)
{
  Impr_Type_id building = get_nation_by_plr(plr)->goals.wonder;
  
  if (improvement_exists(building)
      && game.global_wonders[building] == 0
      && !wonder_obsolete(building)) {
    Tech_Type_id tech = improvement_types[building].tech_req;

    if (tech_is_available(plr, tech) 
        && get_invention(plr, tech) != TECH_KNOWN) {
      return tech;
    }
  }
  return A_UNSET;
}

/**************************************************************************
  Puts into choice the closest (in terms of unknown techs needed) of
    - national tech goals,
    - requirements for the national wonder goal
**************************************************************************/
static Tech_Type_id ai_next_tech_goal_default(struct player *pplayer)
{
  struct nation_type *prace = get_nation_by_plr(pplayer);
  int bestdist = A_LAST + 1;
  int dist, i;
  Tech_Type_id goal = A_UNSET;
  Tech_Type_id tech;

  /* Find the closest of the national tech goals */
  for (i = 0 ; i < MAX_NUM_TECH_GOALS; i++) {
    Tech_Type_id j = prace->goals.tech[i];
    if (!tech_is_available(pplayer, j) 
        || get_invention(pplayer, j) == TECH_KNOWN) {
      continue;
    }
    dist = num_unknown_techs_for_goal(pplayer, j);
    if (dist < bestdist) { 
      bestdist = dist;
      goal = j;
      break; /* remove this to restore old functionality -- Syela */
    }
  } 

  /* Find the requirement for the national wonder */
  tech = get_wonder_tech(pplayer);
  if (tech != A_UNSET 
      && num_unknown_techs_for_goal(pplayer, tech) < bestdist) {
    goal = tech;
  }

  return goal;
}

/**************************************************************************
  Massage the numbers provided to us by ai.tech_want into unrecognizable 
  pulp.

  TODO: Write a transparent formula.

  Notes: 1. num_unknown_techs_for_goal returns 0 for known techs, 1 if tech 
  is immediately available etc.
  2. A tech is reachable means we can research it now; tech is available 
  means it's on our tech tree (different nations can have different techs).
  3. ai.tech_want is usually upped by each city, so it is divided by number
  of cities here.
  4. A tech isn't a requirement of itself.
**************************************************************************/
static void ai_select_tech(struct player *pplayer, 
			   struct ai_tech_choice *choice,
			   struct ai_tech_choice *goal)
{
  Tech_Type_id newtech, newgoal;
  int num_cities_nonzero = MAX(1, city_list_size(&pplayer->cities));
  int values[A_LAST];
  int goal_values[A_LAST];

  memset(values, 0, sizeof(values));
  memset(goal_values, 0, sizeof(goal_values));

  /* Fill in values for the techs: want of the tech 
   * + average want of those we will discover en route */
  tech_type_iterate(i) {
    int steps = num_unknown_techs_for_goal(pplayer, i);

    /* We only want it if we haven't got it (so AI is human after all) */
    if (steps > 0) { 
      values[i] += pplayer->ai.tech_want[i];
      tech_type_iterate(k) {
	if (is_tech_a_req_for_goal(pplayer, k, i)) {
	  values[k] += pplayer->ai.tech_want[i] / steps;
	}
      } tech_type_iterate_end;
    }
  } tech_type_iterate_end;

  /* Fill in the values for the tech goals */
  tech_type_iterate(i) {
    int steps = num_unknown_techs_for_goal(pplayer, i);

    if (steps == 0) {
      continue;
    }

    goal_values[i] = values[i];      
    tech_type_iterate(k) {
      if (is_tech_a_req_for_goal(pplayer, k, i)) {
	goal_values[i] += values[k];
      }
    } tech_type_iterate_end;
    /* This is the best I could do.  It still sometimes does freaky stuff
     * like setting goal to Republic and learning Monarchy, but that's what
     * it's supposed to be doing; it just looks strange. -- Syela */
    goal_values[i] /= steps;
    if (steps < 6) {
      freelog(LOG_DEBUG, "%s: want = %d, value = %d, goal_value = %d",
	      get_tech_name(pplayer, i), pplayer->ai.tech_want[i],
	      values[i], goal_values[i]);
    }
  } tech_type_iterate_end;

  newtech = A_UNSET;
  newgoal = A_UNSET;
  tech_type_iterate(i) {
    if (values[i] > values[newtech]
        && tech_is_available(pplayer, i)
        && get_invention(pplayer, i) == TECH_REACHABLE) {
      newtech = i;
    }
    if (goal_values[i] > goal_values[newgoal]
        && tech_is_available(pplayer, i)) {
      newgoal = i;
    }
  } tech_type_iterate_end;
  freelog(LOG_DEBUG, "%s wants %s with desire %d (%d).", 
	  pplayer->name, get_tech_name(pplayer, newtech), values[newtech], 
	  pplayer->ai.tech_want[newtech]);
  if (choice) {
    choice->choice = newtech;
    choice->want = values[newtech] / num_cities_nonzero;
    choice->current_want = 
      values[pplayer->research.researching] / num_cities_nonzero;
  }

  if (goal) {
    goal->choice = newgoal;
    goal->want = goal_values[newgoal] / num_cities_nonzero;
    goal->current_want = goal_values[pplayer->ai.tech_goal] / num_cities_nonzero;
    freelog(LOG_DEBUG,
	    "Goal->choice = %s, goal->want = %d, goal_value = %d, "
	    "num_cities_nonzero = %d",
	    get_tech_name(pplayer, goal->choice), goal->want,
	    goal_values[newgoal],
	    num_cities_nonzero);
  }
  return;
}

/**************************************************************************
  Choose our next tech goal.  Called by the server when a new tech is 
  discovered to determine new goal and, from it, the new tech to be 
  researched, which is quite stupid since ai_manage_tech sets a goal in 
  ai.tech_goal and we should either respect it or not bother doing it.

  TODO: Kill this function.
**************************************************************************/
void ai_next_tech_goal(struct player *pplayer)
{
  struct ai_tech_choice goal_choice = {0, 0, 0};

  ai_select_tech(pplayer, NULL, &goal_choice);

  if (goal_choice.want == 0) {
    goal_choice.choice = ai_next_tech_goal_default(pplayer);
  }
  if (goal_choice.choice != A_UNSET) {
    pplayer->ai.tech_goal = goal_choice.choice;
    freelog(LOG_DEBUG, "next_tech_goal for %s is set to %s",
	    pplayer->name, get_tech_name(pplayer, goal_choice.choice));
  }
}

/**************************************************************************
  Use AI tech hints provided in governments.ruleset to up corresponding 
  tech wants.

  TODO: The hints structure is too complicated, simplify.
**************************************************************************/
static void ai_use_gov_tech_hint(struct player *pplayer)
{
  int i;

  for (i = 0; i < MAX_NUM_TECH_LIST; i++) {
    struct ai_gov_tech_hint *hint = &ai_gov_tech_hints[i];
    
    if (hint->tech == A_LAST) {
      break;
    }
    
    if (get_invention(pplayer, hint->tech) != TECH_KNOWN) {
      int steps = num_unknown_techs_for_goal(pplayer, hint->tech);

      pplayer->ai.tech_want[hint->tech] += 
	city_list_size(&pplayer->cities)
	* (hint->turns_factor * steps + hint->const_factor);
      if (hint->get_first) {
	break;
      }
    } else {
      if (hint->done) {
	break;
      }
    }
  }
}

/**************************************************************************
  Key AI research function. Disable if we are in a team with human team
  mates in a research pool.
**************************************************************************/
void ai_manage_tech(struct player *pplayer)
{
  struct ai_tech_choice choice, goal;
  /* Penalty for switching research */
  int penalty = (pplayer->got_tech ? 0 : pplayer->research.bulbs_researched);

  /* If there are humans in our team, they will choose the techs */
  players_iterate(aplayer) {
    const struct player_diplstate *ds = pplayer_get_diplstate(pplayer, aplayer);

    if (ds->type == DS_TEAM) {
      return;
    }
  } players_iterate_end;

  ai_use_gov_tech_hint(pplayer);

  ai_select_tech(pplayer, &choice, &goal);
  if (choice.choice != pplayer->research.researching) {
    /* changing */
    if ((choice.want - choice.current_want) > penalty &&
	penalty + pplayer->research.bulbs_researched <=
	total_bulbs_required(pplayer)) {
      freelog(LOG_DEBUG, "%s switching from %s to %s with penalty of %d.",
	      pplayer->name,
	      get_tech_name(pplayer, pplayer->research.researching),
	      get_tech_name(pplayer, choice.choice), penalty);
      choose_tech(pplayer, choice.choice);
    }
  }

  /* crossing my fingers on this one! -- Syela (seems to have worked!) */
  /* It worked, in particular, because the value it sets (ai.tech_goal)
   * is practically never used, see the comment for ai_next_tech_goal */
  if (goal.choice != pplayer->ai.tech_goal) {
    freelog(LOG_DEBUG, "%s change goal from %s (want=%d) to %s (want=%d)",
	    pplayer->name, get_tech_name(pplayer, pplayer->ai.tech_goal), 
	    goal.current_want, get_tech_name(pplayer, goal.choice),
	    goal.want);
    choose_tech_goal(pplayer, goal.choice);
  }
}
