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
  A_NONE=0,
  A_LAST=89
  /*
     A_LAST is a value which is guaranteed to be larger than all
     actual tech id values.  It is used as a flag value; it can
     also be used for fixed allocations to ensure able to hold
     full number of tech.
  */
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
