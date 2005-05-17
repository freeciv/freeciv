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
Team_type_id team_find_by_name(const char *team_name)
{
  assert(team_name != NULL);

  team_iterate(pteam) {
    if (mystrcasecmp(team_name, pteam->name) == 0) {
      return pteam->id;
    }
  } team_iterate_end;

  return TEAM_NONE;
}

/****************************************************************************
  Returns pointer to a team given its id
****************************************************************************/
struct team *team_get_by_id(Team_type_id id)
{
  assert(id == TEAM_NONE || (id < MAX_NUM_TEAMS && id >= 0));
  if (id == TEAM_NONE) {
    return NULL;
  }
  return &teams[id];
}

/****************************************************************************
  Set a player to a team. Removes previous team affiliation,
  creates a new team if it does not exist.
****************************************************************************/
void team_add_player(struct player *pplayer, const char *team_name)
{
  Team_type_id team_id, i;

  assert(pplayer != NULL && team_name != NULL);

  /* find or create team */
  team_id = team_find_by_name(team_name);
  if (team_id == TEAM_NONE) {
    /* see if we have another team available */
    for (i = 0; i < MAX_NUM_TEAMS; i++) {
      if (teams[i].id == TEAM_NONE) {
        team_id = i;
        break;
      }
    }
    /* check if too many teams */
    if (team_id == TEAM_NONE) {
      die("Impossible: Too many teams!");
    }
    /* add another team */
    teams[team_id].id = team_id;
    sz_strlcpy(teams[team_id].name, team_name);
  }
  pplayer->team = team_id;
}

/****************************************************************************
  Removes a player from a team, and removes the team if empty of
  players
****************************************************************************/
void team_remove_player(struct player *pplayer)
{
  bool others = FALSE;

  if (pplayer->team == TEAM_NONE) {
    return;
  }

  assert(pplayer->team < MAX_NUM_TEAMS && pplayer->team >= 0);

  /* anyone else using my team? */
  players_iterate(aplayer) {
    if (aplayer->team == pplayer->team && aplayer != pplayer) {
      others = TRUE;
      break;
    }
  } players_iterate_end;

  /* no other team members left? remove team! */
  if (!others) {
    teams[pplayer->team].id = TEAM_NONE;
  }
  pplayer->team = TEAM_NONE;
}

/****************************************************************************
  Initializes team structure
****************************************************************************/
void team_init(void)
{
  Team_type_id i;

  assert(TEAM_NONE < 0 || TEAM_NONE >= MAX_NUM_TEAMS);

  for (i = 0; i < MAX_NUM_TEAMS; i++) {
    /* mark as unused */
    teams[i].id = TEAM_NONE;
    teams[i].name[0] = '\0';
  }
}
