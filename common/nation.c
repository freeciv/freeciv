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
   Functions for handling the nations.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>

#include "connection.h"
#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "log.h"
#include "mem.h"
#include "nation.h"
#include "player.h"
#include "support.h"
#include "tech.h"

static struct nation_type *nations = NULL;

static int num_nation_groups;
static struct nation_group nation_groups[MAX_NUM_NATION_GROUPS];

/***************************************************************
  Returns 1 if nid is a valid nation id, else 0.
  If returning 0, prints log message with given loglevel
  quoting given func name, explaining problem.
***************************************************************/
static bool bounds_check_nation_id(Nation_type_id nid, int loglevel,
				  const char *func_name)
{
  if (game.control.nation_count==0) {
    freelog(loglevel, "%s before nations setup", func_name);
    return FALSE;
  }
  if (nid < 0 || nid >= game.control.nation_count) {
    freelog(loglevel, "Bad nation id %d (count %d) in %s",
	    nid, game.control.nation_count, func_name);
    return FALSE;
  }
  return TRUE;
}

/***************************************************************
Find nation by (translated) name
***************************************************************/
Nation_type_id find_nation_by_name(const char *name)
{
  int i;

  for (i = 0; i < game.control.nation_count; i++)
     if(mystrcasecmp(name, get_nation_name (i)) == 0)
	return i;

  return NO_NATION_SELECTED;
}

/***************************************************************
Find nation by (untranslated) original name
***************************************************************/
Nation_type_id find_nation_by_name_orig(const char *name)
{
  int i;

  for(i = 0; i < game.control.nation_count; i++)
     if(mystrcasecmp(name, get_nation_name_orig (i)) == 0)
	return i;

  return NO_NATION_SELECTED;
}

/***************************************************************
Returns (translated) name of the nation
***************************************************************/
const char *get_nation_name(Nation_type_id nation)
{
  if (!bounds_check_nation_id(nation, LOG_ERROR, "get_nation_name")) {
    return "";
  }
  return nations[nation].name;
}

/***************************************************************
Returns (untranslated) original name of the nation
***************************************************************/
const char *get_nation_name_orig(Nation_type_id nation)
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
struct leader *get_nation_leaders(Nation_type_id nation, int *dim)
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
Nation_type_id* get_nation_civilwar(Nation_type_id nation)
{
  return nations[nation].civilwar_nations;
}

/***************************************************************
Returns sex of given leader name. If names is not found,
return 1 (meaning male).
***************************************************************/
bool get_nation_leader_sex(Nation_type_id nation, const char *name)
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
bool check_nation_leader_name(Nation_type_id nation, const char *name)
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
const char *get_nation_name_plural(Nation_type_id nation)
{
  if (!bounds_check_nation_id(nation, LOG_ERROR, "get_nation_name_plural")) {
    return "";
  }
  return nations[nation].name_plural;
}

/***************************************************************
Returns pointer to a nation 
***************************************************************/
struct nation_type *get_nation_by_plr(const struct player *plr)
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
struct nation_type *get_nation_by_idx(Nation_type_id nation)
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
  int i;

  nations = fc_calloc(num, sizeof(nations[0]));
  game.control.nation_count = num;

  for (i = 0; i < num; i++) {
    /* HACK: this field is declared const to keep anyone from changing
     * them.  But we have to set it somewhere!  This should be the only
     * place. */
    *(int *)&nations[i].index = i;
  }
}

/***************************************************************
 De-allocate resources associated with the given nation.
***************************************************************/
static void nation_free(Nation_type_id nation)
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
  
  if (p->groups) {
    free(p->groups);
    p->groups = NULL;
  }

  nation_city_names_free(p->city_names);
  p->city_names = NULL;
}

/***************************************************************
 De-allocate the currently allocated nations and remove all
 nation groups
***************************************************************/
void nations_free()
{
  Nation_type_id nation;

  if (!nations) {
    return;
  }

  for (nation = 0; nation < game.control.nation_count; nation++) {
    nation_free(nation);
  }

  free(nations);
  nations = NULL;
  game.control.nation_count = 0;
  num_nation_groups = 0;
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
int get_nation_city_style(Nation_type_id nation)
{
  if (!bounds_check_nation_id(nation, LOG_FATAL, "get_nation_city_style")) {
    die("wrong nation %d", nation);
  }
  return nations[nation].city_style;
}

/***************************************************************
  Add new group into the array of groups. If a group with
  the same name already exists don't create new one, but return
  old one
***************************************************************/
struct nation_group* add_new_nation_group(const char* name)
{
  int i;
  for (i = 0; i < num_nation_groups; i++) {
    if (mystrcasecmp(name, nation_groups[i].name) == 0) {
      return &nation_groups[i];
    }
  }
  if (i == MAX_NUM_NATION_GROUPS) {
    die("Too many groups of nations");
  }
  sz_strlcpy(nation_groups[i].name, name);
  nation_groups[i].match = 0;
  return &nation_groups[num_nation_groups++];
}

/***************************************************************
***************************************************************/
int get_nation_groups_count(void)
{
  return num_nation_groups;
}

struct nation_group* get_nation_group_by_id(int id)
{
  return &nation_groups[id];
}

/***************************************************************
  Check if the given nation is in a given group
***************************************************************/
bool nation_in_group(struct nation_type* nation, const char* group_name)
{
  int i;
  for (i = 0; i < nation->num_groups; i++) {
    if (mystrcasecmp(nation->groups[i]->name, group_name) == 0) {
      return TRUE;
    }
  }
  return FALSE;
}

/****************************************************************************
  Return TRUE iff the editor is allowed to edit the player's nation in
  pregame.
****************************************************************************/
bool can_conn_edit_players_nation(const struct connection *pconn,
				  const struct player *pplayer)
{
  return (game.is_new_game
	  && ((!pconn->observer && pconn->player == pplayer)
	      || pconn->access_level >= ALLOW_CTRL));
}
