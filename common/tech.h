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

struct player;

enum tech_type_id {  
  A_NONE, A_ADVANCED, A_ALPHABET, A_AMPHIBIOUS, A_ASTRONOMY, A_ATOMIC,
  A_AUTOMOBILE, A_BANKING, A_BRIDGE, A_BRONZE, A_CEREMONIAL,
  A_CHEMISTRY, A_CHIVALRY, A_CODE, A_COMBINED, A_COMBUSTION, A_COMMUNISM,
  A_COMPUTERS, A_CONSCRIPTION, A_CONSTRUCTION, A_CURRENCY,
  A_DEMOCRACY, A_ECONOMICS, A_ELECTRICITY, A_ELECTRONICS, A_ENGINEERING,
  A_ENVIRONMENTALISM, A_ESPIONAGE,
  A_EXPLOSIVES, A_FEUDALISM, A_FLIGHT, A_FUNDAMENTALISM, A_FUSION, A_GENETIC,
  A_GUERILLA,A_GUNPOWDER, A_HORSEBACK, A_INDUSTRIALIZATION, A_INVENTION,
  A_IRON, A_LABOR, A_LASER, A_LEADERSHIP, A_LITERACY, A_MACHINE, A_MAGNETISM, 
  A_MAPMAKING, A_MASONRY,
  A_MASS, A_MATHEMATICS, A_MEDICINE, A_METALLURGY, A_MINIATURIZATION, 
  A_MOBILE, A_MONARCHY, A_MONOTHEISM,
  A_MYSTICISM, A_NAVIGATION, A_FISSION, A_POWER, A_PHILOSOPHY,
  A_PHYSICS, A_PLASTICS, A_POLYTHEISM, A_POTTERY, A_RADIO, A_RAILROAD, 
  A_RECYCLING, A_REFINING, A_REFRIGERATION, A_ROBOTICS, A_ROCKETRY, A_SANITATION, A_SEAFARING,
  A_SPACEFLIGHT,  A_STEALTH, A_STEAM, A_STEEL, A_SUPERCONDUCTOR, A_TACTICS, 
  A_CORPORATION, A_REPUBLIC, A_WHEEL, A_THEOLOGY, A_THEORY, A_TRADE, 
  A_UNIVERSITY, A_WARRIORCODE, A_WRITING, A_LAST
};

#define TECH_UNKNOWN 0
#define TECH_KNOWN 1
#define TECH_REACHABLE 2
#define TECH_MARKED 3

struct advance {
  char name[MAX_LEN_NAME];
  int req[2];
};

int get_invention(struct player *plr, int tech);
void set_invention(struct player *plr, int tech, int value);
void update_research(struct player *plr);
int tech_goal_turns(struct player *plr, int goal);
int get_next_tech(struct player *plr, int goal);

int tech_exists(enum tech_type_id id);
enum tech_type_id find_tech_by_name(char *s);

extern struct advance advances[];

#endif  /* FC__TECH_H */
