
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
#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* utility */
#include "iterator.h"
#include "log.h"
#include "shared.h"
#include "support.h"

/* common */
#include "fc_types.h"
#include "game.h"
#include "player.h"
#include "name_translation.h"
#include "team.h"
#include "tech.h"

#include "research.h"


struct research_iter {
  struct iterator vtable;
  int index;
};
#define RESEARCH_ITER(p) ((struct research_iter *) p)

struct research_player_iter {
  struct iterator vtable;
  union {
    struct player *pplayer;
    struct player_list_link *plink;
  };
};
#define RESEARCH_PLAYER_ITER(p) ((struct research_player_iter *) p)

static struct research research_array[MAX_NUM_PLAYER_SLOTS];

static struct name_translation advance_unset_name = NAME_INIT;
static struct name_translation advance_future_name = NAME_INIT;
static struct name_translation advance_unknown_name = NAME_INIT;


/****************************************************************************
  Initializes all player research structure.
****************************************************************************/
void researches_init(void)
{
  int i;

  /* Ensure we have enough space for players or teams. */
  fc_assert(ARRAY_SIZE(research_array) >= team_slot_count());
  fc_assert(ARRAY_SIZE(research_array) >= player_slot_count());

  memset(research_array, 0, sizeof(research_array));
  for (i = 0; i < ARRAY_SIZE(research_array); i++) {
    research_array[i].tech_goal = A_UNSET;
    research_array[i].researching = A_UNSET;
    research_array[i].researching_saved = A_UNKNOWN;
    research_array[i].future_tech = 0;
  }

  /* Set technology names. */
  /* TRANS: "None" tech */
  name_set(&advance_unset_name, NULL, N_("None"));
  name_set(&advance_future_name, NULL, N_("Future Tech."));
  /* TRANS: "Unknown" advance/technology */
  name_set(&advance_unknown_name, NULL, N_("(Unknown)"));
}

/****************************************************************************
  Returns the index of the research in the array.
****************************************************************************/
int research_number(const struct research *presearch)
{
  fc_assert_ret_val(NULL != presearch, -1);
  return presearch - research_array;
}

/****************************************************************************
  Returns the research for the given index.
****************************************************************************/
struct research *research_by_number(int number)
{
  fc_assert_ret_val(0 <= number, NULL);
  fc_assert_ret_val(ARRAY_SIZE(research_array) > number, NULL);
  return &research_array[number];
}

/****************************************************************************
  Returns the research structure associated with the player.
****************************************************************************/
struct research *research_get(const struct player *pplayer)
{
  if (NULL == pplayer) {
    /* Special case used at client side. */
    return NULL;
  } else if (game.info.team_pooled_research) {
    return &research_array[team_number(pplayer->team)];
  } else {
    return &research_array[player_number(pplayer)];
  }
}


#define SPECVEC_TAG string
#define SPECVEC_TYPE char *
#include "specvec.h"

/****************************************************************************
  Return the name translation for 'tech'. Utility for
  research_advance_rule_name() and research_advance_translated_name().
****************************************************************************/
static inline const struct name_translation *
research_advance_name(Tech_type_id tech)
{
  if (A_UNSET == tech) {
    return &advance_unset_name;
  } else if (A_FUTURE == tech) {
    return &advance_future_name;
  } else if (A_UNKNOWN == tech) {
    return &advance_unknown_name;
  } else {
    const struct advance *padvance = advance_by_number(tech);

    fc_assert_ret_val(NULL != padvance, NULL);
    return &padvance->name;
  }
}

/****************************************************************************
  Store the rule name of the given tech (including A_FUTURE) in 'buf'.
  'presearch' may be NULL.
  We don't return a static buffer because that would break anything that
  needed to work with more than one name at a time.
****************************************************************************/
const char *research_advance_rule_name(const struct research *presearch,
                                       Tech_type_id tech)
{
  if (A_FUTURE == tech && NULL != presearch) {
    static struct string_vector future;
    const int no = presearch->future_tech;
    int i;

    /* research->future_tech == 0 means "Future Tech. 1". */
    for (i = future.size; i <= no; i++) {
      string_vector_append(&future, NULL);
    }
    if (NULL == future.p[no]) {
      char buffer[256];

      fc_snprintf(buffer, sizeof(buffer), "%s %d",
                  rule_name(&advance_future_name),
                  no + 1);
      future.p[no] = fc_strdup(buffer);
    }
    return future.p[no];
  }

  return rule_name(research_advance_name(tech));
}

/****************************************************************************
  Store the translated name of the given tech (including A_FUTURE) in 'buf'.
  'presearch' may be NULL.
  We don't return a static buffer because that would break anything that
  needed to work with more than one name at a time.
****************************************************************************/
const char *
research_advance_name_translation(const struct research *presearch,
                                  Tech_type_id tech)
{
  if (A_FUTURE == tech && NULL != presearch) {
    static struct string_vector future;
    const int no = presearch->future_tech;
    int i;

    /* research->future_tech == 0 means "Future Tech. 1". */
    for (i = future.size; i <= no; i++) {
      string_vector_append(&future, NULL);
    }
    if (NULL == future.p[no]) {
      char buffer[256];

      fc_snprintf(buffer, sizeof(buffer), "%s %d",
                  name_translation(&advance_future_name),
                  no + 1);
      future.p[no] = fc_strdup(buffer);
    }
    return future.p[no];
  }

  return name_translation(research_advance_name(tech));
}


/****************************************************************************
  Returns state of the tech for current research.
  This can be: TECH_KNOWN, TECH_UNKNOWN, or TECH_PREREQS_KNOWN
  Should be called with existing techs or A_FUTURE

  If 'presearch' is NULL this checks whether any player knows the tech
  (used by the client).
****************************************************************************/
enum tech_state research_invention_state(const struct research *presearch,
                                         Tech_type_id tech)
{
  fc_assert_ret_val(tech == A_FUTURE
                    || (tech >= 0 && tech < game.control.num_tech_types),
                    -1);

  if (NULL != presearch) {
    return presearch->inventions[tech].state;
  } else if (tech != A_FUTURE && game.info.global_advances[tech]) {
    return TECH_KNOWN;
  } else {
    return TECH_UNKNOWN;
  }
}

/****************************************************************************
  Set research knowledge about tech to given state.
****************************************************************************/
enum tech_state research_invention_set(struct research *presearch,
                                       Tech_type_id tech,
                                       enum tech_state value)
{
  enum tech_state old = presearch->inventions[tech].state;

  if (old == value) {
    return old;
  }
  presearch->inventions[tech].state = value;

  if (value == TECH_KNOWN) {
    game.info.global_advances[tech] = TRUE;
  }
  return old;
}

/****************************************************************************
  Returns TRUE iff the given tech is ever reachable by the players sharing
  the research by checking tech tree limitations.

  'presearch' may be NULL in which case a simplified result is returned
  (used by the client).
****************************************************************************/
bool research_invention_reachable(const struct research *presearch,
                                  const Tech_type_id tech)
{
  Tech_type_id root;

  if (!valid_advance_by_number(tech)) {
    return FALSE;
  }

  root = advance_required(tech, AR_ROOT);
  if (A_NONE != root) {
    if (root == tech) {
      /* This tech requires itself; it can only be reached by special means
       * (init_techs, lua script, ...).
       * If you already know it, you can "reach" it; if not, not. (This case
       * is needed for descendants of this tech.) */
      return TECH_KNOWN == research_invention_state(presearch, tech);
    } else {
      /* Recursive check if the player can ever reach this tech (root tech
       * and both requirements). */
      return (research_invention_reachable(presearch, root)
              && research_invention_reachable(presearch,
                                              advance_required(tech,
                                                               AR_ONE))
              && research_invention_reachable(presearch,
                                              advance_required(tech,
                                                               AR_TWO)));
    }
  }

  return TRUE;
}

/****************************************************************************
  Returns TRUE iff the given tech can be given to the players sharing the
  research immediately.

  If reachable_ok is TRUE, any reachable tech is ok. If it's FALSE,
  getting the tech must not leave holes to the known techs tree.
****************************************************************************/
bool research_invention_gettable(const struct research *presearch,
                                 const Tech_type_id tech,
                                 bool reachable_ok)
{
  Tech_type_id req;

  if (!valid_advance_by_number(tech)) {
    return FALSE;
  }

  /* Tech with root req is immediately gettable only if root req is already
   * known. */
  req = advance_required(tech, AR_ROOT);

  if (req != A_NONE
      && research_invention_state(presearch, req) != TECH_KNOWN) {
    return FALSE;
  }

  if (reachable_ok) {
    /* Any recursively reachable tech is ok */
    return TRUE;
  }

  req = advance_required(tech, AR_ONE);
  if (req != A_NONE
      && research_invention_state(presearch, req) != TECH_KNOWN) {
    return FALSE;
  }
  req = advance_required(tech, AR_TWO);
  if (req != A_NONE
      && research_invention_state(presearch, req) != TECH_KNOWN) {
    return FALSE;
  }

  return TRUE;
}

/****************************************************************************
  Return the next tech we should research to advance towards our goal.
  Returns A_UNSET if nothing is available or the goal is already known.
****************************************************************************/
Tech_type_id research_goal_step(const struct research *presearch,
                                Tech_type_id goal)
{
  const struct advance *pgoal = valid_advance_by_number(goal);

  if (NULL == pgoal
      || !research_invention_reachable(presearch, goal)) {
    return A_UNSET;
  }

  advance_req_iterate(pgoal, preq) {
    switch (research_invention_state(presearch, advance_number(preq))) {
    case TECH_PREREQS_KNOWN:
      return advance_number(preq);
    case TECH_KNOWN:
    case TECH_UNKNOWN:
       break;
    };
  } advance_req_iterate_end;
  return A_UNSET;
}

/****************************************************************************
  Returns the number of technologies the player need to research to get
  the goal technology. This includes the goal technology. Technologies
  are only counted once.

  'presearch' may be NULL in which case it will returns the total number
  of technologies needed for reaching the goal.
****************************************************************************/
int research_goal_unknown_techs(const struct research *presearch,
                                Tech_type_id goal)
{
  const struct advance *pgoal = valid_advance_by_number(goal);

  if (NULL == pgoal) {
    return 0;
  } else if (NULL != presearch) {
    return presearch->inventions[goal].num_required_techs;
  } else {
    return pgoal->num_reqs;
  }
}

/****************************************************************************
  Function to determine cost (in bulbs) of reaching goal technology.
  These costs _include_ the cost for researching the goal technology
  itself.

  'presearch' may be NULL in which case it will returns the total number
  of bulbs needed for reaching the goal.
****************************************************************************/
int research_goal_bulbs_required(const struct research *presearch,
                                 Tech_type_id goal)
{
  const struct advance *pgoal = valid_advance_by_number(goal);

  if (NULL == pgoal) {
    return 0;
  } else if (NULL != presearch) {
    return presearch->inventions[goal].bulbs_required;
  } else if (0 == game.info.tech_cost_style) {
     return game.info.base_tech_cost * pgoal->num_reqs
            * (pgoal->num_reqs + 1) / 2;
  } else {
    int bulbs_required = 0;

    advance_req_iterate(pgoal, preq) {
      bulbs_required += preq->cost;
    } advance_req_iterate_end;
    return bulbs_required;
  }
}

/****************************************************************************
  Returns if the given tech has to be researched to reach the goal. The
  goal itself isn't a requirement of itself.

  'presearch' may be NULL.
****************************************************************************/
bool research_goal_tech_req(const struct research *presearch,
                            Tech_type_id goal, Tech_type_id tech)
{
  const struct advance *pgoal, *ptech;

  if (tech == goal
      || NULL == (pgoal = valid_advance_by_number(goal))
      || NULL == (ptech = valid_advance_by_number(tech))) {
    return FALSE;
  } else if (NULL != presearch) {
    return BV_ISSET(presearch->inventions[goal].required_techs, tech);
  } else {
    advance_req_iterate(pgoal, preq) {
      if (preq == ptech) {
        return TRUE;
      }
    } advance_req_iterate_end;
    return FALSE;
  }
}


/****************************************************************************
  Returns the real size of the player research iterator.
****************************************************************************/
size_t research_iter_sizeof(void)
{
  return sizeof(struct research_iter);
}

/****************************************************************************
  Returns the research structure pointed by the iterator.
****************************************************************************/
static void *research_iter_get(const struct iterator *it)
{
  return &research_array[RESEARCH_ITER(it)->index];
}

/****************************************************************************
  Jump to next team research structure.
****************************************************************************/
static void research_iter_team_next(struct iterator *it)
{
  struct research_iter *rit = RESEARCH_ITER(it);

  if (team_slots_initialised()) {
    do {
      rit->index++;
    } while (rit->index < ARRAY_SIZE(research_array) && !it->valid(it));
  }
}

/****************************************************************************
  Returns FALSE if there is no valid team at current index.
****************************************************************************/
static bool research_iter_team_valid(const struct iterator *it)
{
  struct research_iter *rit = RESEARCH_ITER(it);

  return (0 <= rit->index
          && ARRAY_SIZE(research_array) > rit->index
          && NULL != team_by_number(rit->index));
}

/****************************************************************************
  Jump to next player research structure.
****************************************************************************/
static void research_iter_player_next(struct iterator *it)
{
  struct research_iter *rit = RESEARCH_ITER(it);

  if (player_slots_initialised()) {
    do {
      rit->index++;
    } while (rit->index < ARRAY_SIZE(research_array) && !it->valid(it));
  }
}

/****************************************************************************
  Returns FALSE if there is no valid player at current index.
****************************************************************************/
static bool research_iter_player_valid(const struct iterator *it)
{
  struct research_iter *rit = RESEARCH_ITER(it);

  return (0 <= rit->index
          && ARRAY_SIZE(research_array) > rit->index
          && NULL != player_by_number(rit->index));
}

/****************************************************************************
  Initializes a player research iterator.
****************************************************************************/
struct iterator *research_iter_init(struct research_iter *it)
{
  struct iterator *base = ITERATOR(it);

  base->get = research_iter_get;
  it->index = -1;

  if (game.info.team_pooled_research) {
    base->next = research_iter_team_next;
    base->valid = research_iter_team_valid;
  } else {
    base->next = research_iter_player_next;
    base->valid = research_iter_player_valid;
  }

  base->next(base);
  return base;
}

/****************************************************************************
  Returns the real size of the research player iterator.
****************************************************************************/
size_t research_player_iter_sizeof(void)
{
  return sizeof(struct research_player_iter);
}

/****************************************************************************
  Returns player of the iterator.
****************************************************************************/
static void research_player_iter_validate(struct iterator *it)
{
  const struct player *pplayer;

  for (pplayer = iterator_get(it); NULL != pplayer && !pplayer->is_alive;
       pplayer = iterator_get(it)) {
    iterator_next(it);
  }
}

/****************************************************************************
  Returns player of the iterator.
****************************************************************************/
static void *research_player_iter_pooled_get(const struct iterator *it)
{
  return player_list_link_data(RESEARCH_PLAYER_ITER(it)->plink);
}

/****************************************************************************
  Returns the next player sharing the research.
****************************************************************************/
static void research_player_iter_pooled_next(struct iterator *it)
{
  struct research_player_iter *rpit = RESEARCH_PLAYER_ITER(it);

  rpit->plink = player_list_link_next(rpit->plink);
  research_player_iter_validate(it);
}

/****************************************************************************
  Returns whether the iterate is valid.
****************************************************************************/
static bool research_player_iter_pooled_valid(const struct iterator *it)
{
  return NULL != RESEARCH_PLAYER_ITER(it)->plink;
}

/****************************************************************************
  Returns player of the iterator.
****************************************************************************/
static void *research_player_iter_not_pooled_get(const struct iterator *it)
{
  return RESEARCH_PLAYER_ITER(it)->pplayer;
}

/****************************************************************************
  Invalidate the iterator.
****************************************************************************/
static void research_player_iter_not_pooled_next(struct iterator *it)
{
  RESEARCH_PLAYER_ITER(it)->pplayer = NULL;
}

/****************************************************************************
  Returns whether the iterate is valid.
****************************************************************************/
static bool research_player_iter_not_pooled_valid(const struct iterator *it)
{
  return NULL != RESEARCH_PLAYER_ITER(it)->pplayer;
}

/****************************************************************************
  Initializes a research player iterator.
****************************************************************************/
struct iterator *research_player_iter_init(struct research_player_iter *it,
                                           const struct research *presearch)
{
  struct iterator *base = ITERATOR(it);

  if (game.info.team_pooled_research) {
    base->get = research_player_iter_pooled_get;
    base->next = research_player_iter_pooled_next;
    base->valid = research_player_iter_pooled_valid;
    it->plink = player_list_head(team_members(team_by_number(research_number
                                                             (presearch))));
  } else {
    base->get = research_player_iter_not_pooled_get;
    base->next = research_player_iter_not_pooled_next;
    base->valid = research_player_iter_not_pooled_valid;
    it->pplayer = player_by_number(research_number(presearch));
  }
  research_player_iter_validate(base);

  return base;
}
