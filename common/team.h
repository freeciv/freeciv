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

#include "tech.h"

#define MAX_NUM_TEAM_SLOTS MAX_NUM_PLAYER_SLOTS

struct team {
  struct player_research research;
  struct player_list *plrlist;
  const struct team **tslot;
};

/* General team accessor functions. */
void team_slots_init(void);
bool team_slots_initialised(void);
void team_slots_free(void);
int team_slot_count(void);
int team_slot_index(const struct team **tslot);
struct team *team_slot_get_team(const struct team **tslot);
bool team_slot_is_used(const struct team **tslot);
const struct team **team_slot_by_number(int team_id);

struct team *team_new(int team_id);
void team_destroy(struct team *pteam);
int team_count(void);
int team_index(const struct team *pteam);
int team_number(const struct team *pteam);
struct team *team_by_number(const int team_id);

struct team *find_team_by_rule_name(const char *team_name);

const char *team_rule_name(const struct team *pteam);
const char *team_name_translation(struct team *pteam);

/* Ancillary routines */
void team_add_player(struct player *pplayer, struct team *pteam);
void team_remove_player(struct player *pplayer);

/* iterate over all team slots */
#define team_slots_iterate(_tslot)                                          \
  {                                                                         \
    const struct team **_tslot;                                             \
    int _tslot##_index = 0;                                                 \
    if (team_slots_initialised()) {                                         \
      for (; _tslot##_index < team_slot_count(); _tslot##_index++) {        \
        _tslot = team_slot_by_number(_tslot##_index);
#define team_slots_iterate_end                                              \
      }                                                                     \
    }                                                                       \
  }

/* iterate over all teams, which are used at the moment */
#define teams_iterate(_pteam)                                               \
  team_slots_iterate(_tslot) {                                              \
    if (!team_slot_is_used(_tslot)) {                                       \
      continue;                                                             \
    }                                                                       \
    struct team *_pteam = team_slot_get_team(_tslot);
#define teams_iterate_end                                                   \
  } team_slots_iterate_end;

#endif /* FC__TEAM_H */
