/********************************************************************** 
 Freeciv - Copyright (C) 2005 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifndef FC__TEAM_H
#define FC__TEAM_H

#include "fc_types.h"

#define MAX_NUM_TEAMS MAX_NUM_PLAYERS
#define TEAM_NONE 255

struct team {
  Team_type_id id; /* equal to array index if active, else TEAM_NONE */
  char name[MAX_LEN_NAME];
};

void team_init(void);
Team_type_id team_find_by_name(const char *team_name);
struct team *team_get_by_id(Team_type_id id);
void team_add_player(struct player *pplayer, const char *team_name);
void team_remove_player(struct player *pplayer);

#define team_iterate(pteam)                                                 \
{                                                                           \
  struct team *pteam;                                                       \
  Team_type_id PI_p_itr;                                                    \
									    \
  for (PI_p_itr = 0; PI_p_itr < MAX_NUM_TEAMS; PI_p_itr++) {                \
    pteam = team_get_by_id(PI_p_itr);                                       \
    if (pteam->id == TEAM_NONE) {                                           \
      continue;                                                             \
    }

#define team_iterate_end                                                    \
  }                                                                         \
}

#endif /* FC__TEAM_H */
