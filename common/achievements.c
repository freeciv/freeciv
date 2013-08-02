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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* utility */
#include "fcintl.h"
#include "log.h"
#include "rand.h"
#include "shared.h"

/* common */
#include "game.h"
#include "player.h"
#include "spaceship.h"

#include "achievements.h"

static struct achievement achievements[MAX_ACHIEVEMENT_TYPES];

/****************************************************************
  Initialize achievements.
****************************************************************/
void achievements_init(void)
{
  int i;

  for (i = 0; i < ARRAY_SIZE(achievements); i++) {
    achievements[i].id = i;
    achievements[i].first = NULL;
  }
}

/****************************************************************************
  Free the memory associated with achievements
****************************************************************************/
void achievements_free(void)
{
}

/**************************************************************************
  Return the achievement id.
**************************************************************************/
int achievement_number(const struct achievement *pach)
{
  fc_assert_ret_val(NULL != pach, -1);

  return pach->id;
}

/**************************************************************************
  Return the achievement index.
**************************************************************************/
int achievement_index(const struct achievement *pach)
{
  fc_assert_ret_val(NULL != pach, -1);

  return pach - achievements;
}

/****************************************************************************
  Return achievements of given id.
****************************************************************************/
struct achievement *achievement_by_number(int id)
{
  fc_assert_ret_val(id >= 0 && id < game.control.num_achievement_types, NULL);

  return &achievements[id];
}

/****************************************************************************
  Return translated name of this achievement type.
****************************************************************************/
const char *achievement_name_translation(struct achievement *pach)
{
  return name_translation(&pach->name);
}

/****************************************************************************
  Return untranslated name of this achievement type.
****************************************************************************/
const char *achievement_rule_name(struct achievement *pach)
{
  return rule_name(&pach->name);
}

/**************************************************************************
  Returns achievement matching rule name or NULL if there is no achievement
  with such name.
**************************************************************************/
struct achievement *achievement_by_rule_name(const char *name)
{
  const char *qs = Qn_(name);

  achievements_iterate(pach) {
    if (!fc_strcasecmp(achievement_rule_name(pach), qs)) {
      return pach;
    }
  } achievements_iterate_end;

  return NULL;
}

/****************************************************************************
  Check if some player has now achieved the achievement and return the player
  in question.
****************************************************************************/
struct player *achievement_plr(struct achievement *ach)
{
  struct player_list *achievers = player_list_new();
  struct player *credited = NULL;

  players_iterate(pplayer) {
    if (achievement_check(ach, pplayer)) {
      player_list_append(achievers, pplayer);
    }
  } players_iterate_end;

  if (player_list_size(achievers) > 0) {
    /* If multiple players achieved at the same turn, randomly select one
     * as the one who won the race. */
    credited = player_list_get(achievers, fc_rand(player_list_size(achievers)));

    ach->first = credited;
  }

  player_list_destroy(achievers);

  return credited;
}

/****************************************************************************
  Check if player has now achieved the achievement.
****************************************************************************/
bool achievement_check(struct achievement *ach, struct player *pplayer)
{
  if (ach->first != NULL) {
    /* It was already achieved */
    return FALSE;
  }

  switch(ach->type) {
  case ACHIEVEMENT_SPACESHIP:
    return pplayer->spaceship.state == SSHIP_LAUNCHED;
  case ACHIEVEMENT_COUNT:
    break;
  }

  log_error("achievement_check(): Illegal achievement type %d", ach->type);

  return FALSE;
}

/****************************************************************************
  Return message to send to first player gaining the achievement.
****************************************************************************/
const char *achievement_first_msg(struct achievement *ach)
{
  switch(ach->type) {
  case ACHIEVEMENT_SPACESHIP:
    return _("You're the first one to launch spaceship towards Alpha Centauri!");
  case ACHIEVEMENT_COUNT:
    break;
  }

  log_error("achievement_first_msg(): Illegal achievement type %d", ach->type);

  return NULL;
}
