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
#ifndef FC__AITOOLS_H
#define FC__AITOOLS_H

#include "shared.h"		/* bool type */

#include "fc_types.h"
#include "unit.h"		/* enum ai_unit_task */
#include "unittype.h"		/* Unit_Type_id */

struct ai_choice;
struct pf_path;

#ifdef DEBUG
#define CHECK_UNIT(punit)                                        \
 (assert(punit != NULL),                                         \
  assert(punit->type < U_LAST),                                  \
  assert(punit->owner < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS),   \
  assert(find_unit_by_id(punit->id) != NULL))
#else
#define CHECK_UNIT(punit) assert(TRUE)
#endif

enum bodyguard_enum {
  BODYGUARD_WANTED=-1,
  BODYGUARD_NONE
};

int military_amortize(struct player *pplayer, struct city *pcity, 
                      int value, int delay, int build_cost);
int stack_cost(struct unit *pdef);

bool ai_unit_execute_path(struct unit *punit, struct pf_path *path);
bool ai_gothere(struct player *pplayer, struct unit *punit, 
                struct tile *dst_tile);
bool ai_unit_goto(struct unit *punit, struct tile *ptile);

void ai_unit_new_role(struct unit *punit, enum ai_unit_task task, 
                      struct tile *ptile);
bool ai_unit_make_homecity(struct unit *punit, struct city *pcity);
bool ai_unit_attack(struct unit *punit, struct tile *ptile);
bool ai_unit_move(struct unit *punit, struct tile *ptile);

struct city *dist_nearest_city(struct player *pplayer, struct tile *ptile,
                               bool everywhere, bool enemy);

void ai_government_change(struct player *pplayer, int gov);
int ai_gold_reserve(struct player *pplayer);

void init_choice(struct ai_choice *choice);
void adjust_choice(int value, struct ai_choice *choice);
void copy_if_better_choice(struct ai_choice *cur, struct ai_choice *best);
void ai_advisor_choose_building(struct city *pcity, struct ai_choice *choice);
bool ai_assess_military_unhappiness(struct city *pcity, struct government *g);

bool ai_wants_no_science(struct player *pplayer);

bool is_player_dangerous(struct player *pplayer, struct player *aplayer);

#endif  /* FC__AITOOLS_H */
