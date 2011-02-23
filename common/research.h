/****************************************************************************
 Freeciv - Copyright (C) 2004 - The Freeciv Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
****************************************************************************/
#ifndef FC__RESEARCH_H
#define FC__RESEARCH_H

/* utility */
#include "iterator.h"
#include "support.h"

/* common */
#include "fc_types.h"
#include "tech.h"

struct player_research {
  /* The number of techs and future techs the player has
   * researched/acquired. */
  int techs_researched, future_tech;

  /* Invention being researched in. Valid values for researching are:
   *  - any existing tech (not A_NONE)
   *  - A_FUTURE
   *  - A_UNSET (indicates need for choosing new research)
   * For enemies, A_UNKNOWN is sent to the client, but not on server.
   *
   * bulbs_researched tracks how many bulbs have been accumulated toward
   * this research target. */
  Tech_type_id researching;
  int bulbs_researched;

  /* If the player changes his research target in a turn, he loses some or
   * all of the bulbs he's accumulated toward that target.  We save the
   * original info from the start of the turn so that if he changes back
   * he will get the bulbs back.
   *
   * Has the same values as researching, plus A_UNKNOWN used between turns
   * (not -1 anymore) for savegames. */
  Tech_type_id researching_saved;
  int bulbs_researching_saved;

  /* If the player completed a research this turn, this value is turned on
   * and changing targets may be done without penalty. */
  bool got_tech;

  struct {
    /* One of TECH_UNKNOWN, TECH_KNOWN or TECH_PREREQS_KNOWN. */
    enum tech_state state;

    /* 
     * required_techs, num_required_techs and bulbs_required are
     * cached values. Updated from build_required_techs (which is
     * called by player_research_update).
     */
    bv_techs required_techs;
    int num_required_techs, bulbs_required;
  } inventions[A_LAST];

  /* Tech goal (similar to worklists; when one tech is researched the next
   * tech toward the goal will be chosen).  May be A_NONE. */
  Tech_type_id tech_goal;

  /*
   * Cached values. Updated by player_research_update.
   */
  int num_known_tech_with_flag[TF_COUNT];

  /* Tech upkeep in bulbs. Updated by player_research_update. */
  int tech_upkeep;
};

/* Common functions. */
void player_researches_init(void);

struct player_research *player_research_get(const struct player *pplayer);

/* Iterating utilities. */
struct research_iter;

size_t research_iter_sizeof(void);
struct iterator *research_iter_init(struct research_iter *it);

#define player_researches_iterate(presearch)                                \
  generic_iterate(struct research_iter, struct player_research *,           \
                  presearch, research_iter_sizeof, research_iter_init)
#define player_researches_iterate_end generic_iterate_end

#endif  /* FC__RESEARCH_H */
