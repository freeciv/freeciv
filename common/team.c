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

#include <stdlib.h>

/* utility */
#include "fcintl.h"
#include "log.h"
#include "shared.h"
#include "support.h"

/* common */
#include "game.h"
#include "player.h"
#include "team.h"

static struct {
  const struct team **tslots;
  int used_slots;
} team_slots;

static void team_defaults(struct team *pteam);

/***************************************************************
  Initialise all team slots (= pointer to team pointers).
***************************************************************/
void team_slots_init(void)
{
  int i;

  /* Init team slots. */
  team_slots.tslots = fc_calloc(team_slot_count(),
                                sizeof(*team_slots.tslots));
  /* Can't use the defined functions as the needed data will be
   * defined here. */
  for (i = 0; i < team_slot_count(); i++) {
    const struct team **tslot = team_slots.tslots + i;
    *tslot = NULL;
  }
  team_slots.used_slots = 0;
}

/***************************************************************
  ...
***************************************************************/
bool team_slots_initialised(void)
{
  return (team_slots.tslots != NULL);
}

/***************************************************************
  Remove all team slots.
***************************************************************/
void team_slots_free(void)
{
  team_slots_iterate(tslot) {
    team_destroy(team_slot_get_team(tslot));
  } team_slots_iterate_end;
  free(team_slots.tslots);
  team_slots.tslots = NULL;
  team_slots.used_slots = 0;
}

/***************************************************************
  ...
***************************************************************/
int team_slot_count(void)
{
  return (MAX_NUM_TEAM_SLOTS);
}

/***************************************************************
  ...
***************************************************************/
int team_slot_index(const struct team **tslot)
{
  fc_assert_ret_val(team_slots_initialised(), -1);
  fc_assert_ret_val(tslot != NULL, -1);

  return tslot - team_slots.tslots;
}

/***************************************************************
  ...
***************************************************************/
struct team *team_slot_get_team(const struct team **tslot)
{
  fc_assert_ret_val(team_slots_initialised(), NULL);
  fc_assert_ret_val(tslot != NULL, NULL);

  /* some magic casts */
  return (struct team*) *tslot;
}

/***************************************************************
  Returns TRUE is this slot is "used" i.e. corresponds to a
  valid, initialized team that exists in the game.
***************************************************************/
bool team_slot_is_used(const struct team **tslot)
{
  /* No team slot available, if the game is not initialised. */
  if (!team_slots_initialised()) {
    return FALSE;
  }

  return (*tslot != NULL);
}

/***************************************************************
  Return the possibly unused and uninitialized team slot, a
  pointer to a pointer of the team struct.
***************************************************************/
const struct team **team_slot_by_number(int team_id)
{
  const struct team **tslot;

  if (!team_slots_initialised()
      || !(0 <= team_id && team_id < team_slot_count())) {
    return NULL;
  }

  tslot = team_slots.tslots + team_id;

  return tslot;
}


/****************************************************************************
  ...
****************************************************************************/
struct team *team_new(int team_id)
{
  struct team *pteam = NULL;

  /* team_id == -1: select first free team slot */
  fc_assert_ret_val(-1 <= team_id && team_id < team_slot_count(), NULL);

  if (team_count() == team_slot_count()) {
    log_normal("[%s] Can't create a new team: all team slots full!",
               __FUNCTION__);
    return NULL;
  }

  if (team_id == -1) {
    /* no team position specified */
    team_slots_iterate(tslot) {
      if (!team_slot_is_used(tslot)) {
        team_id = team_slot_index(tslot);
        break;
      }
    } team_slots_iterate_end;

    fc_assert_ret_val(team_id != -1, NULL);
  }

  fc_assert_ret_val(!team_slot_is_used(team_slot_by_number(team_id)), NULL);

  /* now create the team */
  log_debug("Create team for slot %d.", team_id);
  pteam = fc_calloc(1, sizeof(*pteam));
  /* save the team slot in the team struct ... */
  pteam->tslot = team_slot_by_number(team_id);
  /* .. and the team in the team slot */
  *pteam->tslot = pteam;

  /* set default values */
  team_defaults(pteam);

  /* increase number of teams */
  team_slots.used_slots++;

  return pteam;
}

/****************************************************************************
  ...
****************************************************************************/
static void team_defaults(struct team *pteam)
{
  player_research_init(&pteam->research);
  pteam->plrlist = player_list_new();
}

/****************************************************************************
  ...
****************************************************************************/
void team_destroy(struct team *pteam)
{
  const struct team **tslot;

  if (pteam == NULL) {
    return;
  }

  fc_assert_ret(player_list_size(pteam->plrlist) == 0);

  /* save team slot */
  tslot = pteam->tslot;

  fc_assert_ret(*tslot == pteam);

  free(pteam);
  *tslot = NULL;
  team_slots.used_slots--;
}

/**************************************************************************
  Return the number of teams.
**************************************************************************/
int team_count(void)
{
  return team_slots.used_slots;
}

/**************************************************************************
  Return the team index.
**************************************************************************/
int team_index(const struct team *pteam)
{
  return team_number(pteam);
}

/**************************************************************************
  Return the team index/number/id.
**************************************************************************/
int team_number(const struct team *pteam)
{
  fc_assert_ret_val(NULL != pteam, -1);
  return team_slot_index(pteam->tslot);
}

/**************************************************************************
  Return struct team pointer for the given team index.
**************************************************************************/
struct team *team_by_number(const int team_id)
{
  const struct team **tslot;

  tslot = team_slot_by_number(team_id);

  if (!team_slot_is_used(tslot)) {
    return NULL;
  }

  return team_slot_get_team(tslot);
}

/****************************************************************************
 Does a linear search of game.info.team_names_orig[]
 Returns NULL when none match.
****************************************************************************/
struct team *find_team_by_rule_name(const char *team_name)
{
  int i;

  fc_assert_ret_val(team_name != NULL, NULL);

  /* Can't use team_iterate here since it skips unused teams. */
  for (i = 0; i < team_slot_count(); i++) {
    struct team *pteam = team_by_number(i);

    if (0 == fc_strcasecmp(team_rule_name(pteam), team_name)) {
      return pteam;
    }
  }

  return NULL;
}

/****************************************************************************
  Return the translated name of the team.
****************************************************************************/
const char *team_name_translation(struct team *pteam)
{
  return _(team_rule_name(pteam));
}

/****************************************************************************
  Return the untranslated name of the team.
****************************************************************************/
const char *team_rule_name(const struct team *pteam)
{
  fc_assert_ret_val(pteam != NULL, NULL);
  return game.info.team_names_orig[team_index(pteam)];
}

/****************************************************************************
  Set a player to a team.  Removes the previous team affiliation if
  necessary.
****************************************************************************/
void team_add_player(struct player *pplayer, struct team *pteam)
{
  fc_assert_ret(pplayer != NULL);

  if (pteam == NULL) {
    pteam = team_new(-1);
  }

  fc_assert_ret(pteam != NULL);

  if (pteam == pplayer->team) {
    /* It is the team of the player. */
    return;
  }

  log_debug("Adding player %d/%s to team %s.", player_number(pplayer),
            pplayer->username, team_rule_name(pteam));

  /* Remove the player from the old team, if any. */
  team_remove_player(pplayer);

  /* Put the player on the new team. */
  pplayer->team = pteam;
  player_list_append(pteam->plrlist, pplayer);
}

/****************************************************************************
  Remove the player from the team.  This should only be called when deleting
  a player; since every player must always be on a team.

  Note in some very rare cases a player may not be on a team.  It's safe
  to call this function anyway.
****************************************************************************/
void team_remove_player(struct player *pplayer)
{
  struct team *pteam;

  if (pplayer->team) {
    pteam = pplayer->team;

    log_debug("Removing player %d/%s from team %s (%d)",
              player_number(pplayer), player_name(pplayer),
              team_rule_name(pteam), player_list_size(pteam->plrlist));
    player_list_remove(pteam->plrlist, pplayer);

    if (player_list_size(pteam->plrlist) == 0) {
      team_destroy(pteam);
    }
  }
  pplayer->team = NULL;
}
