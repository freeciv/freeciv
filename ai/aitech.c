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

#include <player.h>
#include <aitools.h>
#include <tech.h>
#include <game.h>
#include <plrhand.h>

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

/* Syela-code starts here ................................. */

void find_prerequisites(struct player *pplayer, int i, int *prereq)
{
  /* add tech_want[i] / j to all subtechs */
  int t1,t2;
  t1 = advances[i].req[0];
  t2 = advances[i].req[1];
  if(t1>=A_LAST || t2>=A_LAST) return;
  if (get_invention(pplayer, t1) != TECH_KNOWN) prereq[t1]++;
  if (get_invention(pplayer, t1) == TECH_UNKNOWN)
        find_prerequisites(pplayer, t1, prereq);
  if (get_invention(pplayer, t2) != TECH_KNOWN) prereq[t2]++;
  if (get_invention(pplayer, t2) == TECH_UNKNOWN)
        find_prerequisites(pplayer, t2, prereq);
}

void ai_select_tech(struct player *pplayer, struct ai_choice *choice, struct ai_choice *gol)
{
  int i, j, k;
  int values[A_LAST];
  int goal_values[A_LAST];
  int prereq[A_LAST];
  unsigned char cache[A_LAST][A_LAST];
  
  int c = MAX(1, city_list_size(&pplayer->cities));
  memset(values, 0, sizeof(values));
  memset(goal_values, 0, sizeof(goal_values));
  memset(cache, 0, sizeof(cache));
  for (i = A_NONE; i < A_LAST; i++) {
    j = pplayer->ai.tech_turns[i];
    if (j) { /* if we already got it we don't want it */
      values[i] += pplayer->ai.tech_want[i];
      memset(prereq, 0, sizeof(prereq));
      find_prerequisites(pplayer, i, prereq);
      for (k = A_NONE; k < A_LAST; k++) {
        if (prereq[k]) {
          cache[i][k]++;
          values[k] += pplayer->ai.tech_want[i] / j;
        }
      }
    }
  }

  for (i = A_NONE; i < A_LAST; i++) {
    if (pplayer->ai.tech_turns[i]) {
      for (k = A_NONE; k < A_LAST; k++) {
        if (cache[i][k]) {
          goal_values[i] += values[k];
        }
      }
      goal_values[i] += values[i];
/* this is the best I could do.  It still sometimes does freaky stuff like
setting goal to Republic and learning Monarchy, but that's what it's supposed
to be doing; it just looks strange. -- Syela */
      goal_values[i] /= pplayer->ai.tech_turns[i];
/*if (pplayer->ai.tech_turns[i]<6)
printf("%s: want = %d, value = %d, goal_value = %d\n", advances[i].name,
pplayer->ai.tech_want[i], values[i], goal_values[i]);*/
    } else goal_values[i] = 0;
  }

  j = 0; k = 0;
  for (i = A_NONE; i < A_LAST; i++) {
    if (values[i] > values[j] && get_invention(pplayer, i) == TECH_REACHABLE) j = i;
    if (goal_values[i] > goal_values[k]) k = i;
  }
/*  printf("%s wants %s with desire %d (%d).\n", pplayer->name,
    advances[j].name, values[j], pplayer->ai.tech_want[j]); */
  if (choice) {
    choice->choice = j;
    choice->want = values[j] / c;
    choice->type = values[pplayer->research.researching] / c; /* hijacking this ...
                                          in order to leave tech_wants alone */
  }

  if (gol) {
    gol->choice = k;
    gol->want = goal_values[k] / c;
    gol->type = goal_values[pplayer->ai.tech_goal] / c;
/*printf("Gol->choice = %s, gol->want = %d, goal_value = %d, c = %d\n",
advances[gol->choice].name, gol->want, goal_values[k], c);*/
  }
  return;
}

void ai_select_tech_goal(struct player *pplayer, struct ai_choice *choice)
{
  ai_select_tech(pplayer, 0, choice);
}

void calculate_tech_turns(struct player *pplayer)
{
  int i, j;
  memset(pplayer->ai.tech_turns, 0, sizeof(pplayer->ai.tech_turns));
  for (i = A_NONE + 1; i < A_LAST; i++) {
    pplayer->ai.tech_turns[i] = tech_goal_turns(pplayer, i);
  }
}

void ai_next_tech_goal(struct player *pplayer)
{
  struct ai_choice bestchoice, curchoice;
  
  bestchoice.choice = A_NONE;      
  bestchoice.want   = 0;

  calculate_tech_turns(pplayer);
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

void ai_manage_tech(struct player *pplayer)
{
  struct ai_choice choice, gol;
  int penalty;

  penalty = (pplayer->got_tech ? 0 : pplayer->research.researched);

  ai_select_tech(pplayer, &choice, &gol);
  if (choice.choice != pplayer->research.researching) {
    if ((choice.want - choice.type) > penalty &&             /* changing */
   penalty + pplayer->research.researched <= research_time(pplayer)) {
/*printf("%s switching from %s to %s with penalty of %d.\n",
        pplayer->name, advances[pplayer->research.researching].name,
        advances[choice.choice].name, penalty);*/
      choose_tech(pplayer, choice.choice);
    }
  }
/* crossing my fingers on this one! -- Syela (seems to have worked!) */
  if (gol.choice != pplayer->ai.tech_goal) {
/*printf("%s changing goal from %s (want = %d) to %s (want = %d)\n",
pplayer->name, advances[pplayer->ai.tech_goal].name, gol.type,
advances[gol.choice].name, gol.want);*/
    choose_tech_goal(pplayer, gol.choice);
  }
}
