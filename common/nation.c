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

/**********************************************************************
   Functions for handling the nations and teams.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>

#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "log.h"
#include "mem.h"
#include "player.h"
#include "support.h"
#include "tech.h"

#include "nation.h"

static struct nation_type *nations = NULL;
static struct team teams[MAX_NUM_TEAMS];

/***************************************************************
  Returns 1 if nid is a valid nation id, else 0.
  If returning 0, prints log message with given loglevel
  quoting given func name, explaining problem.
***************************************************************/
static bool bounds_check_nation_id(Nation_Type_id nid, int loglevel,
				  const char *func_name)
{
  if (game.nation_count==0) {
    freelog(loglevel, "%s before nations setup", func_name);
    return FALSE;
  }
  if (nid < 0 || nid >= game.nation_count) {
    freelog(loglevel, "Bad nation id %d (count %d) in %s",
	    nid, game.nation_count, func_name);
    return FALSE;
  }
  return TRUE;
}

/***************************************************************
Find nation by (translated) name
***************************************************************/
Nation_Type_id find_nation_by_name(const char *name)
{
  int i;

  for(i=0; i<game.nation_count; i++)
     if(mystrcasecmp(name, get_nation_name (i)) == 0)
	return i;

  return NO_NATION_SELECTED;
}

/***************************************************************
Find nation by (untranslated) original name
***************************************************************/
Nation_Type_id find_nation_by_name_orig(const char *name)
{
  int i;

  for(i=0; i<game.nation_count; i++)
     if(mystrcasecmp(name, get_nation_name_orig (i)) == 0)
	return i;

  return NO_NATION_SELECTED;
}

/***************************************************************
Returns (translated) name of the nation
***************************************************************/
const char *get_nation_name(Nation_Type_id nation)
{
  if (!bounds_check_nation_id(nation, LOG_ERROR, "get_nation_name")) {
    return "";
  }
  return nations[nation].name;
}

/***************************************************************
Returns (untranslated) original name of the nation
***************************************************************/
const char *get_nation_name_orig(Nation_Type_id nation)
{
  if (!bounds_check_nation_id(nation, LOG_ERROR, "get_nation_name_orig")) {
    return "";
  }
  return nations[nation].name_orig;
}

/***************************************************************
Returns pointer to the array of the nation leader names, and
sets dim to number of leaders.
***************************************************************/
struct leader *get_nation_leaders(Nation_Type_id nation, int *dim)
{
  if (!bounds_check_nation_id(nation, LOG_FATAL, "get_nation_leader_names")) {
    die("wrong nation %d", nation);
  }
  *dim = nations[nation].leader_count;
  return nations[nation].leaders;
}

/****************************************************************************
  Returns pointer to the preferred set of nations that can fork from the
  nation.  The array is terminated by a NO_NATION_SELECTED value.
****************************************************************************/
Nation_Type_id* get_nation_civilwar(Nation_Type_id nation)
{
  return nations[nation].civilwar_nations;
}

/***************************************************************
Returns sex of given leader name. If names is not found,
return 1 (meaning male).
***************************************************************/
bool get_nation_leader_sex(Nation_Type_id nation, const char *name)
{
  int i;
  
  if (!bounds_check_nation_id(nation, LOG_ERROR, "get_nation_leader_sex")) {
    return FALSE;
  }
  for (i = 0; i < nations[nation].leader_count; i++) {
    if (strcmp(nations[nation].leaders[i].name, name) == 0) {
      return nations[nation].leaders[i].is_male;
    }
  }
  return TRUE;
}

/***************************************************************
checks if given leader name exist for given nation.
***************************************************************/
bool check_nation_leader_name(Nation_Type_id nation, const char *name)
{
  int i;
  
  if (!bounds_check_nation_id(nation, LOG_ERROR, "check_nation_leader_name")) {
    return TRUE;			/* ? */
  }
  for (i = 0; i < nations[nation].leader_count; i++) {
    if (strcmp(name, nations[nation].leaders[i].name) == 0) {
      return TRUE;
    }
  }
  return FALSE;
}

/***************************************************************
Returns plural name of the nation.
***************************************************************/
const char *get_nation_name_plural(Nation_Type_id nation)
{
  if (!bounds_check_nation_id(nation, LOG_ERROR, "get_nation_name_plural")) {
    return "";
  }
  return nations[nation].name_plural;
}

/***************************************************************
Returns pointer to a nation 
***************************************************************/
struct nation_type *get_nation_by_plr(struct player *plr)
{
  assert(plr != NULL);
  if (!bounds_check_nation_id(plr->nation, LOG_FATAL, "get_nation_by_plr")) {
    die("wrong nation %d", plr->nation);
  }
  return &nations[plr->nation];
}

/***************************************************************
  ...
***************************************************************/
struct nation_type *get_nation_by_idx(Nation_Type_id nation)
{
  if (!bounds_check_nation_id(nation, LOG_FATAL, "get_nation_by_idx")) {
    die("wrong nation %d", nation);
  }
  return &nations[nation];
}

/***************************************************************
 Allocate space for the given number of nations.
***************************************************************/
void nations_alloc(int num)
{
  nations = (struct nation_type *)fc_calloc(num, sizeof(struct nation_type));
  game.nation_count = num;
}

/***************************************************************
 De-allocate resources associated with the given nation.
***************************************************************/
static void nation_free(Nation_Type_id nation)
{
  int i;
  struct nation_type *p = get_nation_by_idx(nation);

  for (i = 0; i < p->leader_count; i++) {
    free(p->leaders[i].name);
  }
  if (p->leaders) {
    free(p->leaders);
    p->leaders = NULL;
  }
  
  if (p->class) {
    free(p->class);
    p->class = NULL;
  }
  
  if (p->legend) {
    free(p->legend);
    p->legend = NULL;
  }

  if (p->civilwar_nations) {
    free(p->civilwar_nations);
    p->civilwar_nations = NULL;
  }

  if (p->parent_nations) {
    free(p->parent_nations);
    p->parent_nations = NULL;
  }

  nation_city_names_free(p->city_names);
  p->city_names = NULL;
}

/***************************************************************
 De-allocate the currently allocated nations.
***************************************************************/
void nations_free()
{
  Nation_Type_id nation;

  if (!nations) {
    return;
  }

  for (nation = 0; nation < game.nation_count; nation++) {
    nation_free(nation);
  }

  free(nations);
  nations = NULL;
  game.nation_count = 0;
}

/***************************************************************
 deallocates an array of city names. needs to be separate so 
 server can use it individually (misc_city_names)
***************************************************************/
void nation_city_names_free(struct city_name *city_names)
{
  int i;

  if (city_names) {
    /* 
     * Unfortunately, this monstrosity of a loop is necessary given
     * the setup of city_names.  But that setup does make things
     * simpler elsewhere.
     */
    for (i = 0; city_names[i].name; i++) {
      free(city_names[i].name);
    }
    free(city_names);
  }
}

/***************************************************************
Returns nation's city style
***************************************************************/
int get_nation_city_style(Nation_Type_id nation)
{
  if (!bounds_check_nation_id(nation, LOG_FATAL, "get_nation_city_style")) {
    die("wrong nation %d", nation);
  }
  return nations[nation].city_style;
}

/***************************************************************
  Returns the id of a team given its name, or TEAM_NONE if 
  not found.
***************************************************************/
Team_Type_id team_find_by_name(const char *team_name)
{
  assert(team_name != NULL);

  team_iterate(pteam) {
     if(mystrcasecmp(team_name, pteam->name) == 0) {
	return pteam->id;
     }
  } team_iterate_end;

  return TEAM_NONE;
}

/***************************************************************
  Returns pointer to a team given its id
***************************************************************/
struct team *team_get_by_id(Team_Type_id id)
{
  assert(id == TEAM_NONE || (id < MAX_NUM_TEAMS && id >= 0));
  if (id == TEAM_NONE) {
    return NULL;
  }
  return &teams[id];
}

/***************************************************************
  Count living members of given team
***************************************************************/
int team_count_members_alive(Team_Type_id id)
{
  struct team *pteam = team_get_by_id(id);
  int count = 0;

  if (pteam == NULL) {
    return 0;
  }
  assert(pteam->id < MAX_NUM_TEAMS && pteam->id != TEAM_NONE);
  players_iterate(pplayer) {
    if (pplayer->is_alive && pplayer->team == pteam->id) {
      count++;
    }
  } players_iterate_end;
  return count;
}

/***************************************************************
  Set a player to a team. Removes previous team affiliation,
  creates a new team if it does not exist.
***************************************************************/
void team_add_player(struct player *pplayer, const char *team_name)
{
  Team_Type_id team_id, i;

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

/***************************************************************
  Removes a player from a team, and removes the team if empty of
  players
***************************************************************/
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

/***************************************************************
  Initializes team structure
***************************************************************/
void team_init()
{
  Team_Type_id i;

  assert(TEAM_NONE < 0 || TEAM_NONE >= MAX_NUM_TEAMS);

  for (i = 0; i < MAX_NUM_TEAMS; i++) {
    /* mark as unused */
    teams[i].id = TEAM_NONE;
    teams[i].name[0] = '\0';
  }
}
