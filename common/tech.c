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

#include <tech.h>
#include <player.h>
#include <game.h>

struct advance advances[]= {
  {"None",                  {A_NONE, A_NONE} }, 
  {"Advanced Flight",       { A_RADIO, A_MACHINE} }, 
  {"Alphabet",              { A_NONE, A_NONE} },
  {"Amphibious Warfare",    { A_NAVIGATION, A_TACTICS} },
  {"Astronomy",             { A_MYSTICISM, A_MATHEMATICS} },
  {"Atomic Theory",         { A_THEORY, A_PHYSICS} },
  {"Automobile",            { A_COMBUSTION, A_STEEL} },
  {"Banking",               { A_TRADE, A_REPUBLIC} },
  {"Bridge Building",       { A_IRON, A_CONSTRUCTION} },
  {"Bronze Working",        { A_NONE, A_NONE} },
  {"Ceremonial Burial",     { A_NONE, A_NONE} },
  {"Chemistry",             { A_UNIVERSITY, A_MEDICINE} },
  {"Chivalry",              { A_FEUDALISM, A_HORSEBACK} },
  {"Code of Laws",          { A_ALPHABET, A_NONE} },
  {"Combined Arms",         { A_MOBILE, A_ADVANCED} },  
  {"Combustion",            { A_REFINING, A_EXPLOSIVES} },
  {"Communism",             { A_PHILOSOPHY, A_INDUSTRIALIZATION} },
  {"Computers",             { A_MASS, A_MINIATURIZATION} },
  {"Conscription",          { A_DEMOCRACY, A_METALLURGY} },
  {"Construction",          { A_MASONRY, A_CURRENCY} },
  {"Currency",              { A_BRONZE, A_NONE} },
  {"Democracy",             { A_BANKING, A_INVENTION} },
  {"Economics",             { A_BANKING, A_UNIVERSITY} },
  {"Electricity",           { A_METALLURGY, A_MAGNETISM} },
  {"Electronics",           { A_CORPORATION, A_ELECTRICITY} },
  {"Engineering",           { A_WHEEL, A_CONSTRUCTION} },
  {"Environmentalism",      { A_RECYCLING, A_SPACEFLIGHT} },   
  {"Espionage",             { A_COMMUNISM, A_DEMOCRACY} },
  {"Explosives",            { A_GUNPOWDER, A_CHEMISTRY} },
  {"Feudalism",             { A_WARRIORCODE, A_MONARCHY} },
  {"Flight",                { A_COMBUSTION, A_THEORY} },
  {"Fundamentalism",        { A_THEOLOGY, A_CONSCRIPTION}}, 
  {"Fusion Power",          { A_POWER, A_SUPERCONDUCTOR} },
  {"Genetic Engineering",   { A_MEDICINE, A_CORPORATION} },
  {"Guerilla Warfare",      { A_COMMUNISM, A_TACTICS} },
  {"Gunpowder",             { A_INVENTION, A_IRON} },
  {"Horseback Riding",      { A_NONE, A_NONE} },
  {"Industrialization",     { A_RAILROAD, A_BANKING} },
  {"Invention",             { A_ENGINEERING, A_LITERACY} },
  {"Iron Working",          { A_BRONZE, A_WARRIORCODE} },
  {"Labor Union",           { A_MASS, A_GUERILLA} },
  {"Laser",                 { A_MASS, A_POWER} }, 
  {"Leadership",            { A_CHIVALRY, A_GUNPOWDER} },
  {"Literacy",              { A_WRITING, A_CODE} },
  {"Machine Tools",         { A_STEEL, A_TACTICS} }, 
  {"Magnetism",             { A_IRON, A_PHYSICS} },
  {"Map Making",            { A_ALPHABET, A_NONE} },
  {"Masonry",               { A_NONE, A_NONE} },
  {"Mass Production",       { A_AUTOMOBILE, A_CORPORATION} },
  {"Mathematics",           { A_ALPHABET, A_MASONRY} },
  {"Medicine",              { A_PHILOSOPHY, A_TRADE} },
  {"Metallurgy",            { A_GUNPOWDER, A_UNIVERSITY} },
  {"Miniaturization",       { A_MACHINE, A_ELECTRONICS} },
  {"Mobile Warfare",        { A_AUTOMOBILE, A_TACTICS} },
  {"Monarchy",              { A_CEREMONIAL, A_CODE} },
  {"Monotheism",            { A_PHILOSOPHY, A_POLYTHEISM} },
  {"Mysticism",             { A_CEREMONIAL, A_NONE} },
  {"Navigation",            { A_SEAFARING, A_ASTRONOMY} },
  {"Nuclear Fission",       { A_MASS, A_ATOMIC} },
  {"Nuclear Power",         { A_FISSION, A_ELECTRONICS} },
  {"Philosophy",            { A_MYSTICISM, A_LITERACY} },
  {"Physics",               { A_LITERACY, A_NAVIGATION} },
  {"Plastics",              { A_REFINING, A_SPACEFLIGHT} },
  {"Polytheism",            { A_HORSEBACK, A_CEREMONIAL} },
  {"Pottery",               { A_NONE, A_NONE} },
  {"Radio",                 { A_FLIGHT, A_ELECTRICITY} },
  {"Railroad",              { A_STEAM, A_BRIDGE} },
  {"Recycling",             { A_MASS, A_DEMOCRACY} },
  {"Refining",              { A_CHEMISTRY, A_CORPORATION} },
  {"Refrigeration",         { A_SANITATION, A_ELECTRICITY}}, 
  {"Robotics",              { A_MOBILE, A_COMPUTERS} },
  {"Rocketry",              { A_ADVANCED, A_ELECTRONICS} },
  {"Sanitation",            { A_ENGINEERING, A_MEDICINE} },
  {"Seafaring",             { A_POTTERY, A_MAPMAKING} }, 
  {"Space Flight",          { A_COMPUTERS, A_ROCKETRY} },
  {"Stealth",               { A_SUPERCONDUCTOR, A_ROBOTICS} },
  {"Steam Engine",          { A_PHYSICS, A_INVENTION} },
  {"Steel",                 { A_ELECTRICITY, A_INDUSTRIALIZATION} },
  {"Superconductors",       { A_POWER, A_LASER} },
  {"Tactics",               { A_CONSCRIPTION, A_LEADERSHIP} },
  {"The Corporation",       { A_ECONOMICS, A_INDUSTRIALIZATION} },
  {"The Republic",          { A_CODE, A_LITERACY} },
  {"The Wheel",             { A_HORSEBACK, A_NONE} },
  {"Theology",              { A_FEUDALISM, A_MONOTHEISM} },
  {"Theory Of Gravity",     { A_ASTRONOMY, A_UNIVERSITY} },
  {"Trade",                 { A_CURRENCY, A_CODE} },
  {"University",            { A_MATHEMATICS, A_PHILOSOPHY} },
  {"Warrior Code",          { A_NONE, A_NONE} }, 
  {"Writing",               { A_ALPHABET, A_NONE }}
};


/**************************************************************************
...
**************************************************************************/
int get_invention(struct player *plr, int tech)
{
  if(tech<0 || tech>=A_LAST)
    return 0;
  return plr->research.inventions[tech];
}

/**************************************************************************
...
**************************************************************************/
void set_invention(struct player *plr, int tech, int value)
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
  int i;
  
  for (i=0;i<A_LAST;i++) {
    if (get_invention(plr, i) == TECH_REACHABLE || get_invention(plr, i) == TECH_MARKED)
      plr->research.inventions[i]=TECH_UNKNOWN;
    if(get_invention(plr, i) == TECH_UNKNOWN
       && get_invention(plr, advances[i].req[0])==TECH_KNOWN   
       && get_invention(plr, advances[i].req[1])==TECH_KNOWN)
      plr->research.inventions[i]=TECH_REACHABLE;
  }
}

/**************************************************************************
  we mark nodes visited so we won't count them more than once, this function
  isn't to be called direct, use tech_goal_turns instead.
**************************************************************************/
int tech_goal_turns_rec(struct player *plr, int goal)
{
  if (goal <= A_NONE || goal >= A_LAST || 
      get_invention(plr, goal) == TECH_KNOWN || 
      get_invention(plr, goal) == TECH_MARKED) 
    return 0; 
  set_invention(plr, goal, TECH_MARKED);
  return (tech_goal_turns_rec(plr, advances[goal].req[0]) + 
          tech_goal_turns_rec(plr, advances[goal].req[1]) + 1);
}

/**************************************************************************
 returns the number of techs the player need to research to get the goal
 tech, techs are only counted once.
**************************************************************************/
int tech_goal_turns(struct player *plr, int goal)
{
  int res;
  res = tech_goal_turns_rec(plr, goal);
  update_research(plr);
  return res;
}


/**************************************************************************
...don't use this function directly, call get_next_tech instead.
**************************************************************************/
int get_next_tech_rec(struct player *plr, int goal)
{
  int sub_goal;
  if (get_invention(plr, goal) == TECH_KNOWN)
    return 0;
  if (get_invention(plr, goal) == TECH_REACHABLE)
    return goal;
  sub_goal = get_next_tech_rec(plr, advances[goal].req[0]);
  if (sub_goal) 
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

int get_next_tech(struct player *plr, int goal)
{
  if (goal <= A_NONE || goal >= A_LAST || 
      get_invention(plr, goal) == TECH_KNOWN) 
    return A_NONE; 
  return (get_next_tech_rec(plr, goal));
}


