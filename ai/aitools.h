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
#include "unit.h"

/* 
 * Change these and remake to watch logs from a specific 
 * part of the AI code.
 */
#define LOGLEVEL_BODYGUARD LOG_DEBUG
#define LOGLEVEL_UNIT LOG_DEBUG
#define LOGLEVEL_GOTO LOG_DEBUG
#define LOGLEVEL_CITY LOG_DEBUG

/* General AI logging macros */

#define CITY_LOG(level, pcity, msg...)                       \
  {                                                          \
    char buffer[500];                                        \
    sprintf(buffer, "%s's %s(%d,%d) [s%d d%d u%d] ",         \
            city_owner(pcity)->name, pcity->name,            \
            pcity->x, pcity->y, pcity->size,                 \
            pcity->ai.danger, pcity->ai.urgency);            \
    cat_snprintf(buffer, sizeof(buffer), msg);               \
    freelog(MIN(LOGLEVEL_CITY, level), buffer);              \
  }

#define UNIT_LOG(level, punit, msg...)                       \
  {                                                          \
    char buffer[500];                                        \
    sprintf(buffer, "%s's %s[%d] (%d,%d)->(%d,%d) ",         \
            unit_owner(punit)->name, unit_type(punit)->name, \
          punit->id, punit->x, punit->y,                     \
          punit->goto_dest_x, punit->goto_dest_y);           \
    cat_snprintf(buffer, sizeof(buffer), msg);               \
    freelog(MIN(LOGLEVEL_UNIT, level), buffer);              \
  }

/* Only makes a log message when a GOTO fails */
#define GOTO_LOG(level, punit, result, msg...)                    \
  if (result == GR_FAILED || result == GR_FOUGHT) {               \
    char buffer[500];                                             \
    sprintf(buffer, "%s's %s[%d] on GOTO (%d,%d)->(%d,%d) %s : ", \
          unit_owner(punit)->name, unit_type(punit)->name,        \
          punit->id, punit->x, punit->y,                          \
          punit->goto_dest_x, punit->goto_dest_y,                 \
          (result==GR_FAILED) ? "failed" : "fought");             \
    cat_snprintf(buffer, sizeof(buffer), msg);                    \
    freelog(MIN(LOGLEVEL_GOTO, level), buffer);                   \
  }                                                               \

#define BODYGUARD_LOG(level, punit, msg)                        \
  {                                                             \
    char buffer[500];                                           \
    struct unit *pcharge = find_unit_by_id(punit->ai.charge);   \
    sprintf(buffer, "%s's bodyguard %s[%d] (%d,%d)[%d@%d,%d]->(%d,%d) ",\
          unit_owner(punit)->name, unit_type(punit)->name,      \
          punit->id, punit->x, punit->y,                        \
          pcharge ? pcharge->id : -1, pcharge ? pcharge->x : -1,\
          pcharge ? pcharge->y : -1,                            \
          punit->goto_dest_x, punit->goto_dest_y);              \
    cat_snprintf(buffer, sizeof(buffer), msg);                  \
    freelog(MIN(LOGLEVEL_BODYGUARD, level), buffer);            \
  }

struct ai_choice;
struct city;
struct government;
struct player;

enum bodyguard_enum {
  BODYGUARD_WANTED=-1,
  BODYGUARD_NONE
};

void ai_unit_new_role(struct unit *punit, enum ai_unit_task utask);
void ai_unit_attack(struct unit *punit, int x, int y);
bool ai_unit_move(struct unit *punit, int x, int y);

struct city *dist_nearest_city(struct player *pplayer, int x, int y,
                               bool everywhere, bool enemy);

void ai_government_change(struct player *pplayer, int gov);

int ai_gold_reserve(struct player *pplayer);

void init_choice(struct ai_choice *choice);
void adjust_choice(int value, struct ai_choice *choice);
void copy_if_better_choice(struct ai_choice *cur, struct ai_choice *best);
void ai_advisor_choose_building(struct city *pcity, struct ai_choice *choice);
bool ai_assess_military_unhappiness(struct city *pcity, struct government *g);

int ai_evaluate_government(struct player *pplayer, struct government *g);

#endif  /* FC__AITOOLS_H */
