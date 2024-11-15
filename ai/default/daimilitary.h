/***********************************************************************
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
#ifndef FC__DAIMILITARY_H
#define FC__DAIMILITARY_H

/* common */
#include "fc_types.h"
#include "unittype.h"

/* server/advisors */
#include "advchoice.h"

struct civ_map;

#ifdef FREECIV_WEB
#define ASSESS_DANGER_MAX_DISTANCE         40
#define AI_HANDICAP_DISTANCE_LIMIT         6  /* TODO: 20 for non-web */
#endif /* FREECIV_WEB */

/* When an enemy has this or lower number of cities left, try harder
   to finish them off. */
#define FINISH_HIM_CITY_COUNT              5

typedef struct unit_list *(player_unit_list_getter)(struct player *pplayer);

struct unit_type *dai_choose_defender_versus(struct city *pcity,
                                             struct unit *attacker);
struct adv_choice *military_advisor_choose_build(struct ai_type *ait,
                                                 const struct civ_map *nmap,
                                                 struct player *pplayer,
                                                 struct city *pcity,
                                                 player_unit_list_getter ul_cb);
void dai_assess_danger_player(struct ai_type *ait,
                              const struct civ_map *nmap, struct player *pplayer);
int assess_defense_quadratic(struct ai_type *ait, struct city *pcity);
int assess_defense_unit(struct ai_type *ait, struct city *pcity,
                        struct unit *punit, bool igwall);
int assess_defense(struct ai_type *ait, struct city *pcity);
bool dai_process_defender_want(struct ai_type *ait, const struct civ_map *nmap,
                               struct player *pplayer,
                               struct city *pcity, unsigned int danger,
                               struct adv_choice *choice, adv_want extra_want);

#endif /* FC__DAIMILITARY_H */
