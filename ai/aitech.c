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
#include <string.h>

#include "game.h"
#include "government.h"
#include "log.h"
#include "player.h"
#include "tech.h"

#include "plrhand.h"

#include "advattitude.h"
#include "advforeign.h"
#include "advleader.h"
#include "advmilitary.h"
#include "advscience.h"
#include "advtrade.h"
#include "aitools.h"

#include "aitech.h"

/**************************************************************************
.. AI got some tech goals, and should try to fulfill them. 
**************************************************************************/

/**************************************************************************
.. calculate next government wish.
**************************************************************************/
static Tech_Type_id get_government_tech(struct player *plr)
{
  int goal = get_nation_by_plr(plr)->goals.government;
  int subgoal = get_government(goal)->subgoal;
  
  if (can_change_to_government(plr, goal)) {
    freelog(LOG_DEBUG, "get_gov_tech (%s): have %d", plr->name, goal);
    return A_NONE;
  }

  if (subgoal >= 0) {
    struct government *subgov = get_government(subgoal);
    if (get_invention(plr, subgov->required_tech) == TECH_KNOWN) {
      freelog(LOG_DEBUG, "get_gov_tech (%s): have sub %d %s",
	      plr->name, goal, subgov->name);
      return get_government(goal)->required_tech;
    } else {
      freelog(LOG_DEBUG, "get_gov_tech (%s): do sub %d %s",
	      plr->name, goal, subgov->name);
      return subgov->required_tech;
    }
  } else {
    freelog(LOG_DEBUG, "get_gov_tech (%s): no sub %d", plr->name, goal);
    return get_government(goal)->required_tech;
  }
}

/**************************************************************************
  Returns tech corresponding to players wonder goal from nations[],
  if it makes sense, and wonder is not already built and not obsolete.
  Otherwise returns A_NONE.
**************************************************************************/
static Tech_Type_id get_wonder_tech(struct player *plr)
{
  int building = get_nation_by_plr(plr)->goals.wonder;
  
  if (improvement_exists(building)
      && game.global_wonders[building] == 0
      && !wonder_obsolete(building)) {
    Tech_Type_id tech = improvement_types[building].tech_req;

    if (tech_exists(tech) && get_invention(plr, tech) != TECH_KNOWN)
      return tech;
  }
  return A_NONE;
}

/**************************************************************************
  ...
**************************************************************************/
static void ai_next_tech_goal_default(struct player *pplayer, 
				      struct ai_choice *choice)
{
  struct nation_type *prace = get_nation_by_plr(pplayer);
  int bestdist = A_LAST + 1;
  int dist, i;
  Tech_Type_id goal = A_NONE;
  Tech_Type_id tech;

  for (i = 0 ; i < MAX_NUM_TECH_GOALS; i++) {
    Tech_Type_id j = prace->goals.tech[i];
    if (!tech_exists(j) || get_invention(pplayer, j) == TECH_KNOWN) 
      continue;
    dist = num_unknown_techs_for_goal(pplayer, j);
    if (dist < bestdist) { 
      bestdist = dist;
      goal = j;
      break; /* remove this to restore old functionality -- Syela */
    }
  } 
  tech = get_government_tech(pplayer);
  if (tech != A_NONE && tech_exists(tech)) {
    dist = num_unknown_techs_for_goal(pplayer, tech);
    if (dist < bestdist) { 
      bestdist = dist;
      goal = tech;
    }
  }
  tech = get_wonder_tech(pplayer);
  if (tech != A_NONE) {
    dist = num_unknown_techs_for_goal(pplayer, tech);
    if (dist < bestdist) { 
/*    bestdist = dist; */ /* useless, reinclude when adding a new if statement */
      goal = tech;
    }
  }
  if (goal != A_NONE) {
    choice->choice = goal;
    choice->want = 1;
  }
}

static void adjust_tech_choice(struct player *pplayer, struct ai_choice *cur, 
			       struct ai_choice *best, int advisor)
{
  if (cur->want != 0) {
    leader_adjust_tech_choice(pplayer, cur, advisor);
    copy_if_better_choice(cur, best);
  }    
}

/* Syela-code starts here ................................. */

/**************************************************************************
  ...
**************************************************************************/
static void ai_select_tech(struct player *pplayer, struct ai_choice *choice,
			   struct ai_choice *gol)
{
  Tech_Type_id i, k, l;
  int j;
  int num_cities_nonzero;
  int values[A_LAST];
  int goal_values[A_LAST];

  if((num_cities_nonzero = city_list_size(&pplayer->cities)) < 1)
    num_cities_nonzero = 1;
  memset(values, 0, sizeof(values));
  memset(goal_values, 0, sizeof(goal_values));
  for (i = A_FIRST; i < game.num_tech_types; i++) {
    j = num_unknown_techs_for_goal(pplayer, i);
    if (j > 0) { /* if we already got it we don't want it */
      values[i] += pplayer->ai.tech_want[i];
      for (k = A_FIRST; k < game.num_tech_types; k++) {
	if (is_tech_a_req_for_goal(pplayer, k, i)) {
	  values[k] += pplayer->ai.tech_want[i] / j;
	}
      }
    }
  }

  for (i = A_FIRST; i < game.num_tech_types; i++) {
    if (num_unknown_techs_for_goal(pplayer, i) > 0) {
      for (k = A_FIRST; k < game.num_tech_types; k++) {
	if (is_tech_a_req_for_goal(pplayer, k, i)) {
          goal_values[i] += values[k];
        }
      }
      goal_values[i] += values[i];
      
/* this is the best I could do.  It still sometimes does freaky stuff like
setting goal to Republic and learning Monarchy, but that's what it's supposed
to be doing; it just looks strange. -- Syela */
      
      goal_values[i] /= num_unknown_techs_for_goal(pplayer, i);
      if (num_unknown_techs_for_goal(pplayer, i) < 6) {
	freelog(LOG_DEBUG, "%s: want = %d, value = %d, goal_value = %d",
		advances[i].name, pplayer->ai.tech_want[i],
		values[i], goal_values[i]);
      }
    } else goal_values[i] = 0;
  }

  l = A_NONE; k = A_NONE;
  for (i = A_FIRST; i < game.num_tech_types; i++) {
    if (values[i] > values[l] && get_invention(pplayer, i) == TECH_REACHABLE) l = i;
    if (goal_values[i] > goal_values[k]) k = i;
  }
  freelog(LOG_DEBUG, "%s wants %s with desire %d (%d).", pplayer->name,
		advances[l].name, values[l], pplayer->ai.tech_want[l]);
  if (choice) {
    choice->choice = l;
    choice->want = values[l] / num_cities_nonzero;
    choice->type = values[pplayer->research.researching] / num_cities_nonzero;
    /* hijacking this ... in order to leave tech_wants alone */
  }

  if (gol) {
    gol->choice = k;
    gol->want = goal_values[k] / num_cities_nonzero;
    gol->type = goal_values[pplayer->ai.tech_goal] / num_cities_nonzero;
    freelog(LOG_DEBUG,
	    "Gol->choice = %s, gol->want = %d, goal_value = %d, "
	    "num_cities_nonzero = %d",
	    advances[gol->choice].name, gol->want, goal_values[k],
	    num_cities_nonzero);
  }
  return;
}

/**************************************************************************
  ...
**************************************************************************/
static void ai_select_tech_goal(struct player *pplayer, struct ai_choice *choice)
{
  ai_select_tech(pplayer, NULL, choice);
}

/**************************************************************************
  ...
**************************************************************************/
void ai_next_tech_goal(struct player *pplayer)
{
  struct ai_choice bestchoice, curchoice;
  
  bestchoice.choice = A_NONE;      
  bestchoice.want   = 0;

  ai_select_tech_goal(pplayer, &curchoice);
  copy_if_better_choice(&curchoice, &bestchoice); /* not dealing with the rest */

  military_advisor_choose_tech(pplayer, &curchoice);  
  adjust_tech_choice(pplayer, &curchoice, &bestchoice, ADV_MILITARY);
  
  trade_advisor_choose_tech(pplayer, &curchoice);  
  adjust_tech_choice(pplayer, &curchoice, &bestchoice, ADV_TRADE);
  
  science_advisor_choose_tech(pplayer, &curchoice);  
  adjust_tech_choice(pplayer, &curchoice, &bestchoice, ADV_SCIENCE);

  foreign_advisor_choose_tech(pplayer, &curchoice);
  adjust_tech_choice(pplayer, &curchoice, &bestchoice, ADV_FOREIGN);
 
  attitude_advisor_choose_tech(pplayer, &curchoice);
  adjust_tech_choice(pplayer, &curchoice , &bestchoice, ADV_ATTITUDE);

  if (bestchoice.want == 0) {/* remove when the ai is done */
    ai_next_tech_goal_default(pplayer, &bestchoice); 
  }
  if (bestchoice.want != 0) 
    pplayer->ai.tech_goal = bestchoice.choice;
}

/**************************************************************************
  ...
**************************************************************************/
void ai_manage_tech(struct player *pplayer)
{
  struct ai_choice choice, gol;
  int penalty;

  penalty = (pplayer->got_tech ? 0 : pplayer->research.bulbs_researched);

  ai_select_tech(pplayer, &choice, &gol);
  if (choice.choice != pplayer->research.researching) {
    /* changing */
    if ((choice.want - choice.type) > penalty &&
	penalty + pplayer->research.bulbs_researched <=
	total_bulbs_required(pplayer)) {
      freelog(LOG_DEBUG, "%s switching from %s to %s with penalty of %d.",
	      pplayer->name, advances[pplayer->research.researching].name,
	      advances[choice.choice].name, penalty);
      choose_tech(pplayer, choice.choice);
    }
  }
  /* crossing my fingers on this one! -- Syela (seems to have worked!) */
  if (gol.choice != pplayer->ai.tech_goal) {
    freelog(LOG_DEBUG, "%s changing goal from %s (want = %d) to %s (want = %d)",
	    pplayer->name, advances[pplayer->ai.tech_goal].name, gol.type,
	    advances[gol.choice].name, gol.want);
    choose_tech_goal(pplayer, gol.choice);
  }
}
