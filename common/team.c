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
#include "log.h"
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
  int index;

  assert(team_name != NULL);
  assert(NUM_TEAMS <= MAX_NUM_TEAMS);

  /* Can't use team_iterate here since it skips empty teams. */
  for (index = 0; index < NUM_TEAMS; index++) {
    struct team *pteam = team_get_by_id(index);

    if (mystrcasecmp(team_name, team_get_name_orig(pteam)) == 0) {
      return pteam;
    }
  }

  return NULL;
}

/****************************************************************************
  Returns pointer to a team given its id
****************************************************************************/
struct team *team_get_by_id(Team_type_id id)
{
  if (id < 0 || id >= NUM_TEAMS) {
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
  assert(pplayer != NULL);
  assert(pteam != NULL && &teams[pteam->index] == pteam);

  freelog(LOG_DEBUG, "Adding player %d/%s to team %s.",
	  pplayer->player_no, pplayer->username,
	  pteam ? team_get_name(pteam) : "(none)");

  /* Remove the player from the old team, if any.  The player's team should
   * only be NULL for a few instants after the player was created; after
   * that they should automatically be put on a team.  So although we
   * check for a NULL case here this is only needed for that one
   * situation. */
  team_remove_player(pplayer);

  /* Put the player on the new team. */
  pplayer->team = pteam;
  
  pteam->players++;
  assert(pteam->players <= MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS);
}

/****************************************************************************
  Remove the player from the team.  This should only be called when deleting
  a player; since every player must always be on a team.

  Note in some very rare cases a player may not be on a team.  It's safe
  to call this function anyway.
****************************************************************************/
void team_remove_player(struct player *pplayer)
{
  if (pplayer->team) {
    freelog(LOG_DEBUG, "Removing player %d/%s from team %s (%d)",
	    pplayer->player_no, pplayer->username,
	    pplayer->team ? team_get_name(pplayer->team) : "(none)",
	    pplayer->team ? pplayer->team->players : 0);
    pplayer->team->players--;
    assert(pplayer->team->players >= 0);
  }
  pplayer->team = NULL;
}

/****************************************************************************
  Return the translated name of the team.
****************************************************************************/
const char *team_get_name(const struct team *pteam)
{
  return _(team_get_name_orig(pteam));
}

/****************************************************************************
  Return the untranslated name of the team.
****************************************************************************/
const char *team_get_name_orig(const struct team *pteam)
{
  if (!pteam) {
    return N_("(none)");
  }
  return game.info.team_names_orig[pteam->index];
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
  for (i = 0; i < NUM_TEAMS; i++) {
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

  for (i = 0; i < MAX_NUM_TEAMS; i++) {
    /* mark as unused */
    teams[i].index = i;

    teams[i].players = 0;
    player_research_init(&(teams[i].research));
  }
}
