
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

/* common */
#include "fc_types.h"
#include "game.h"
#include "player.h"
#include "team.h"

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
  fc_assert_ret_val(NULL != pplayer, NULL);

  if (game.info.team_pooled_research) {
    return &research_array[team_number(pplayer->team)];
  } else {
    return &research_array[player_number(pplayer)];
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
