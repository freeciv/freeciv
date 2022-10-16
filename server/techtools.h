/***********************************************************************
 Freeciv - Copyright (C) 2005 The Freeciv Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef FC__TECHTOOLS_H
#define FC__TECHTOOLS_H

/* common */
#include "government.h"
#include "player.h"
#include "tech.h"

struct research;

struct cur_govs_data {
  struct {
    bool *govs;
  } *players;
};

void research_apply_penalty(struct research *presearch, Tech_type_id tech,
                            int penalty_percent);
void do_tech_parasite_effects(struct player *pplayer);

void send_research_info(const struct research *presearch,
                        const struct conn_list *dest);

void script_tech_learned(struct research *presearch,
                         struct player *originating_plr, struct advance *tech,
                         const char *reason);
void found_new_tech(struct research *presearch, Tech_type_id tech_found,
                    bool was_discovery, bool saving_bulbs);
void update_bulbs(struct player *pplayer, int bulbs, bool check_tech,
                  bool free_bulbs);
void init_tech(struct research *presearch, bool update);
void choose_tech(struct research *presearch, Tech_type_id tech);
void choose_random_tech(struct research *presearch);
void choose_tech_goal(struct research *presearch, Tech_type_id tech);
Tech_type_id steal_a_tech(struct player *pplayer, struct player *target,
                Tech_type_id preferred);

Tech_type_id pick_free_tech(struct research *presearch);
void give_immediate_free_tech(struct research *presearch, Tech_type_id tech);
void give_initial_techs(struct research *presearch, int num_random_techs);

bool tech_transfer(struct player *plr_recv, struct player *plr_donor,
                   Tech_type_id tech);

struct cur_govs_data *create_current_governments_data(struct research *presearch);
struct cur_govs_data *create_current_governments_data_all(void);
void free_current_governments_data(struct cur_govs_data *data);
void notify_new_government_options(struct player *pplayer, struct cur_govs_data *data,
                                   const char *reason);

#endif  /* FC__TECHTOOLS_H */
