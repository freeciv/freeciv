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
#include "ailog.h"
#include "aitools.h"

#include "aitech.h"

struct ai_tech_choice {
  Tech_Type_id choice;   /* The id of the most needed tech */
  int want;              /* Want of the most needed tech */
  int current_want;      /* Want of the tech which currenlty researched 
			  * or is our current goal */
};

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
  int num_cities_nonzero = MAX(1, city_list_size(pplayer->cities));
  int values[A_LAST];
  int goal_values[A_LAST];

  memset(values, 0, sizeof(values));
  memset(goal_values, 0, sizeof(goal_values));

  /* Fill in values for the techs: want of the tech 
   * + average want of those we will discover en route */
  tech_type_iterate(i) {
    if (tech_exists(i)) {
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
    }
  } tech_type_iterate_end;

  /* Fill in the values for the tech goals */
  tech_type_iterate(i) {
    if (tech_exists(i)) {
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
    }
  } tech_type_iterate_end;

  newtech = A_UNSET;
  newgoal = A_UNSET;
  tech_type_iterate(i) {
    if (tech_exists(i)) {
      if (values[i] > values[newtech]
	  && tech_is_available(pplayer, i)
	  && get_invention(pplayer, i) == TECH_REACHABLE) {
	newtech = i;
      }
      if (goal_values[i] > goal_values[newgoal]
	  && tech_is_available(pplayer, i)) {
	newgoal = i;
      }
    }
  } tech_type_iterate_end;
#ifdef REALLY_DEBUG_THIS
  tech_type_iterate(id) {
    if (values[id] > 0 && get_invention(pplayer, id) == TECH_REACHABLE) {
      TECH_LOG(LOG_DEBUG, pplayer, id, "turn end want: %d", values[id]);
    }
  } tech_type_iterate_end;
#endif
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

  ai_select_tech(pplayer, &choice, &goal);
  if (choice.choice != pplayer->research.researching) {
    /* changing */
    if ((choice.want - choice.current_want) > penalty &&
	penalty + pplayer->research.bulbs_researched <=
	total_bulbs_required(pplayer)) {
      TECH_LOG(LOG_DEBUG, pplayer, choice.choice, "new research, was %s, "
               "penalty was %d", 
               get_tech_name(pplayer, pplayer->research.researching),
               penalty);
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
