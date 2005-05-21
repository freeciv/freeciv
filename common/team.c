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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <stdlib.h>

#include "fcintl.h"
#include "shared.h"
#include "support.h"

#include "game.h"
#include "player.h"
#include "team.h"

/**********************************************************************
   Functions for handling teams.
***********************************************************************/

static struct team teams[MAX_NUM_TEAMS];

/****************************************************************************
  Returns the id of a team given its name, or TEAM_NONE if 
  not found.
****************************************************************************/
struct team *team_find_by_name(const char *team_name)
{
  assert(team_name != NULL);

  team_iterate(pteam) {
    if (mystrcasecmp(team_name, pteam->name) == 0) {
      return pteam;
    }
  } team_iterate_end;

  return NULL;
}

/****************************************************************************
  Returns pointer to a team given its id
****************************************************************************/
struct team *team_get_by_id(Team_type_id id)
{
  if (id < 0 || id >= MAX_NUM_TEAMS) {
    return NULL;
  }
  return &teams[id];
}

/****************************************************************************
  Set a player to a team.  Removes the previous team affiliation if
  necessary.
****************************************************************************/
void team_add_player(struct player *pplayer, struct team *pteam)
{
  assert(pplayer != NULL && pteam != NULL);
  assert(&teams[pteam->index] == pteam);

  /* Remove the player from the old team, if any.  The player's team should
   * only be NULL for a few instants after the player was created; after
   * that they should automatically be put on a team.  So although we
   * check for a NULL case here this is only needed for that one
   * situation. */
  if (pplayer->team) {
    team_remove_player(pplayer);
  }

  /* Put the player on the new team. */
  pplayer->team = pteam;
  pteam->players++;
  assert(pteam->players <= MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS);
}

/****************************************************************************
  Remove the player from the team.  This should only be called when deleting
  a player; since every player must always be on a team.
****************************************************************************/
void team_remove_player(struct player *pplayer)
{
  pplayer->team->players--;
  assert(pplayer->team->players >= 0);
  pplayer->team = NULL;
}

/****************************************************************************
  Returns the most empty team available.  This is the team that should be
  assigned to a newly-created player.
****************************************************************************/
struct team *find_empty_team(void)
{
  Team_type_id i;
  struct team *pbest = NULL;

  /* Can't use teams_iterate here since it skips empty teams! */
  for (i = 0; i < MAX_NUM_TEAMS; i++) {
    struct team *pteam = team_get_by_id(i);

    if (!pbest || pbest->players > pteam->players) {
      pbest = pteam;
    }
    if (pbest->players == 0) {
      /* No need to keep looking. */
      return pbest;
    }
  }

  return pbest;
}

/****************************************************************************
  Initializes team structure
****************************************************************************/
void teams_init(void)
{
  Team_type_id i;
  char *names[] = {
    N_("Team 1"),
    N_("Team 2"),
    N_("Team 3"),
    N_("Team 4"),
    N_("Team 5"),
    N_("Team 6"),
    N_("Team 7"),
    N_("Team 8"),
    N_("Team 9"),
    N_("Team 10"),
    N_("Team 11"),
    N_("Team 12"),
    N_("Team 13"),
    N_("Team 14"),
    N_("Team 15"),
    N_("Team 16"),
    N_("Team 17"),
    N_("Team 18"),
    N_("Team 19"),
    N_("Team 20"),
    N_("Team 21"),
    N_("Team 22"),
    N_("Team 23"),
    N_("Team 24"),
    N_("Team 25"),
    N_("Team 26"),
    N_("Team 27"),
    N_("Team 28"),
    N_("Team 29"),
    N_("Team 30"),
    N_("Team 31"),
    N_("Team 32"),
  };
  assert(ARRAY_SIZE(names) == MAX_NUM_TEAMS);

  for (i = 0; i < MAX_NUM_TEAMS; i++) {
    /* mark as unused */
    teams[i].index = i;
    sz_strlcpy(teams[i].name, names[i]);

    teams[i].players = 0;
  }
}
