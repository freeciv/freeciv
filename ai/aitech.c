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

#include <player.h>
#include <aitools.h>
#include <tech.h>
#include <game.h>

#include <advleader.h>
#include <advmilitary.h>
#include <advtrade.h>
#include <advscience.h>
#include <advforeign.h>
#include <advattitude.h>

/**************************************************************************
.. AI got some tech goals, and should try to fulfill them. 
**************************************************************************/

/**************************************************************************
.. calculate next government wish.
**************************************************************************/
int get_government_tech(struct player *plr)
{
  int government = get_race(plr)->goals.government;
  if (can_change_to_government(plr, government))
    return 0;
  switch (government) {
  case G_MONARCHY:
    return A_MONARCHY;
    break;

  case G_COMMUNISM:
    if (get_invention(plr, A_MONARCHY) == TECH_KNOWN)
      return A_COMMUNISM;
    else
      return A_MONARCHY;
    break;

  case G_REPUBLIC:
      return A_REPUBLIC;
    break;

  case G_DEMOCRACY:
    if (get_invention(plr, A_REPUBLIC) == TECH_KNOWN)
      return A_DEMOCRACY;
    else 
      return A_REPUBLIC;
    break;
  }
  return 0; /* to make compiler happy */
}
int get_wonder_tech(struct player *plr)
{
  int building;
  int tech=0; /* make compiler happy */
  building = get_race(plr)->goals.wonder;
  if (game.global_wonders[building] || wonder_obsolete(building)) 
    return 0;
  tech = improvement_types[building].tech_requirement;
  if (get_invention(plr, tech) == TECH_KNOWN)
    return 0;
  return tech;
}


void ai_next_tech_goal_default(struct player *pplayer, 
			       struct ai_choice *choice)
{
  struct player_race *prace;
  int bestdist = A_LAST + 1;
  int dist, i;
  int goal = 0;
  int tech;
  prace = get_race(pplayer);
  for (i = 0 ; i < TECH_GOALS; i++) {
    if (get_invention(pplayer, prace->goals.tech[i]) == TECH_KNOWN) 
      continue;
    dist = tech_goal_turns(pplayer, prace->goals.tech[i]);
    if (dist < bestdist) { 
      bestdist = dist;
      goal = prace->goals.tech[i];
      break; /* remove this to restore old functionality -- Syela */
    }
  } 
  tech = get_government_tech(pplayer);
  if (tech) {
    dist = tech_goal_turns(pplayer, tech);
    if (dist < bestdist) { 
      bestdist = dist;
      goal = tech;
    }
  }
  tech = get_wonder_tech(pplayer);
  if (tech) {
    dist = tech_goal_turns(pplayer, tech);
    if (dist < bestdist) { 
      bestdist = dist;
      goal = tech;
    }
  }
  if (goal) {
    choice->choice = goal;
    choice->want = 1;
  }
}

void adjust_tech_choice(struct player *pplayer, struct ai_choice *cur, 
			       struct ai_choice *best, int advisor)
{
  if (cur->want) {
    leader_adjust_tech_choice(pplayer, cur, advisor);
    copy_if_better_choice(cur, best);
  }    
}

void ai_next_tech_goal(struct player *pplayer)
{
  struct ai_choice bestchoice, curchoice;
  
  bestchoice.choice = A_NONE;      
  bestchoice.want   = 0;

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
  if (bestchoice.want) 
    pplayer->ai.tech_goal = bestchoice.choice;
}

 
void ai_manage_tech(struct player *pplayer) 
{
 
}

