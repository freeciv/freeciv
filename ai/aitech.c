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

int ai_tech_goal_turns(struct player *pplayer, int i)
{
#ifndef TECH_TURNS_IS_FIXED
  return(tech_goal_turns(pplayer, i));
#endif
  return pplayer->ai.tech_turns[i];
}

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

void find_prerequisites(struct player *pplayer, int i, int *prereq)
{
  /* add tech_want[i] / j to all subtechs */
  int t1 = advances[i].req[0];
  int t2 = advances[i].req[1];
  if (get_invention(pplayer, t1) == TECH_REACHABLE) prereq[t1]++;
  if (get_invention(pplayer, t1) == TECH_UNKNOWN)
        find_prerequisites(pplayer, t1, prereq);
  if (get_invention(pplayer, t2) == TECH_REACHABLE) prereq[t2]++;
  if (get_invention(pplayer, t2) == TECH_UNKNOWN)
        find_prerequisites(pplayer, t2, prereq);
}

void ai_select_tech(struct player *pplayer, struct ai_choice *choice)
{
  int i, j, k;
  int values[A_LAST];
  int prereq[A_LAST];
  int c = MAX(1, city_list_size(&pplayer->cities));
  memset(values, 0, sizeof(values));
  for (i = A_NONE; i < A_LAST; i++) {
    if (pplayer->ai.tech_want[i]) {
/*      printf("%s wants %s with desire %d.\n", pplayer->name,
           advances[i].name, pplayer->ai.tech_want[i]); */
      j = ai_tech_goal_turns(pplayer, i);
      if (j == 1) values[i] += pplayer->ai.tech_want[i];
      else if (j) {
        memset(prereq, 0, sizeof(prereq));
        find_prerequisites(pplayer, i, prereq);
        for (k = A_NONE; k < A_LAST; k++) {
           if (prereq[k]) values[k] += pplayer->ai.tech_want[i] / j;
        }
      }
    }
  }
  j = 0;
  for (i = A_NONE; i < A_LAST; i++) {
    values[i] /= c;
    if (values[i] > values[j]) j = i;
  }
/*  printf("%s wants %s with desire %d (%d).\n", pplayer->name,
    advances[j].name, values[j], pplayer->ai.tech_want[j]); */
  choice->choice = j;
  choice->want = values[j];
  choice->type = values[pplayer->research.researching]; /* hijacking this ...
                                          in order to leave tech_wants alone */
  return;
}


/**************************************************************************
... this funct is unreliable.  don't use if avoidable. -- Syela
void ai_select_tech_goal(struct player *pplayer, struct ai_choice *choice) 
{
  int i, best = A_NONE;
  int values[A_LAST];
  int c = MAX(1, city_list_size(&pplayer->cities));
  for (i = A_NONE; i < A_LAST; i++) {
    values[i] = tech_want_rec(pplayer, i);
    update_research(pplayer);
  }
  for (i = A_NONE; i < A_LAST; i++) {
    if (get_invention(pplayer, i) != TECH_KNOWN) {
      values[i] /= (ai_tech_goal_turns(pplayer, i) * c);
      if (values[i] > values[best]) best = i;
    }
  }
  printf("%s wants %s with desire %d.\n", pplayer->name,
      advances[best].name, values[best]);
  choice->choice = best;
  choice->want = values[best] / c;
}
**************************************************************************/

void calculate_tech_turns(struct player *pplayer)
{
  int i, j, t1, t2;
#ifndef TECH_TURNS_IS_FIXED
  return;
#endif
  memset(pplayer->ai.tech_turns, 0, sizeof(pplayer->ai.tech_turns));
  for (i = 1; i < A_LAST; i++) { /* skipping A_NONE */
    pplayer->ai.tech_turns[i] = tech_goal_turns(pplayer, i);
  } /* calculating this a LOT less often than military_advise_tech would. -- Syela */
}


void ai_select_tech_goal(struct player *pplayer, struct ai_choice *choice) 
{
  calculate_tech_turns(pplayer); /* probably important. */
  ai_select_tech(pplayer, choice);
}

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
  if (bestchoice.want) 
    pplayer->ai.tech_goal = bestchoice.choice;
}

 
int tech_want_rec(struct player *plr, int goal)
{
  if (goal <= A_NONE || goal >= A_LAST ||
      get_invention(plr, goal) == TECH_KNOWN ||
      get_invention(plr, goal) == TECH_MARKED)
    return 0;
  set_invention(plr, goal, TECH_MARKED);
  return (tech_want_rec(plr, advances[goal].req[0]) +
          tech_want_rec(plr, advances[goal].req[1]) +
          plr->ai.tech_want[goal]);
}

void ai_manage_tech(struct player *pplayer)
{
  struct ai_choice choice;
  int penalty;

  penalty = (pplayer->got_tech ? 0 : pplayer->research.researched);
  if (penalty + pplayer->research.researched > research_time(pplayer))
    return; /* this is a kluge */

  ai_select_tech(pplayer, &choice);
  if (choice.choice != pplayer->research.researching) {
    if ((choice.want - choice.type) > penalty) { /* changing */
      printf("%s switching from %s to %s with penalty of %d.\n",
        pplayer->name, advances[pplayer->research.researching].name,
        advances[choice.choice].name, penalty);
      choose_tech(pplayer, choice.choice);
    }
  }
/* crossing my fingers on this one! -- Syela */
}
