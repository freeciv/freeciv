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
static bool bounds_check_nation(const struct nation_type *pnation,
				int loglevel, const char *func_name)
{
  if (game.control.nation_count == 0) {
    freelog(loglevel, "%s before nations setup", func_name);
    return FALSE;
  }
  if (pnation->index < 0
      || pnation->index >= game.control.nation_count
      || &nations[pnation->index] != pnation) {
    freelog(loglevel, "Bad nation id %d (count %d) in %s",
	    pnation->index, game.control.nation_count, func_name);
    return FALSE;
  }
  return TRUE;
}

/****************************************************************************
  Returns the nation that has the given (translated) name.
  Returns NO_NATION_SELECTED if none match.
****************************************************************************/
struct nation_type *find_nation_by_translated_name(const char *name)
{
  nations_iterate(pnation) {
    if (0 == strcmp(nation_name_translation(pnation), name)) {
      return pnation;
    }
  } nations_iterate_end;

  return NO_NATION_SELECTED;
}

/****************************************************************************
  Returns the nation that has the given (untranslated) rule name.
  Returns NO_NATION_SELECTED if none match.
****************************************************************************/
struct nation_type *find_nation_by_rule_name(const char *name)
{
  const char *qname = Qn_(name);

  nations_iterate(pnation) {
    if (0 == mystrcasecmp(nation_rule_name(pnation), qname)) {
      return pnation;
    }
  } nations_iterate_end;

  return NO_NATION_SELECTED;
}

/****************************************************************************
  Return the (untranslated) rule name of the nation.
  You don't have to free the return pointer.
****************************************************************************/
const char *nation_rule_name(const struct nation_type *pnation)
{
  if (!bounds_check_nation(pnation, LOG_ERROR, "nation_rule_name")) {
    return "";
  }
  return Qn_(pnation->name_single.vernacular);
}

/****************************************************************************
  Return the (translated) name of the given nation. 
  You don't have to free the return pointer.
****************************************************************************/
const char *nation_name_translation(struct nation_type *pnation)
{
  if (!bounds_check_nation(pnation, LOG_ERROR, "nation_name_translation")) {
    return "";
  }
  if (NULL == pnation->name_single.translated) {
    /* delayed (unified) translation */
    pnation->name_single.translated = ('\0' == pnation->name_single.vernacular[0])
				      ? pnation->name_single.vernacular
				      : Q_(pnation->name_single.vernacular);
  }
  return pnation->name_single.translated;
}

/****************************************************************************
  Return the (translated) plural name of the given nation. 
  You don't have to free the return pointer.
****************************************************************************/
const char *nation_plural_translation(struct nation_type *pnation)
{
  if (!bounds_check_nation(pnation, LOG_ERROR, "nation_plural_translation")) {
    return "";
  }
  if (NULL == pnation->name_plural.translated) {
    /* delayed (unified) translation */
    pnation->name_plural.translated = ('\0' == pnation->name_plural.vernacular[0])
				      ? pnation->name_plural.vernacular
				      : Q_(pnation->name_plural.vernacular);
  }
  return pnation->name_plural.translated;
}

/****************************************************************************
  Return the (translated) name of the given nation of a player. 
  You don't have to free the return pointer.
****************************************************************************/
const char *nation_name_for_player(const struct player *pplayer)
{
  return nation_name_translation(nation_of_player(pplayer));
}

/****************************************************************************
  Return the (translated) plural name of the given nation of a player. 
  You don't have to free the return pointer.
****************************************************************************/
const char *nation_plural_for_player(const struct player *pplayer)
{
  return nation_plural_translation(nation_of_player(pplayer));
}

/****************************************************************************
  Return whether a nation is "playable"; i.e., whether a human player can
  choose this nation.  Barbarian and observer nations are not playable.

  This does not check whether a nation is "used" or "available".
****************************************************************************/
bool is_nation_playable(const struct nation_type *nation)
{
  if (!bounds_check_nation(nation, LOG_FATAL, "is_nation_playable")) {
    die("wrong nation %d", nation->index);
  }
  return nation->is_playable;
}

/****************************************************************************
  Returns which kind of barbarians can use this nation.

  This does not check whether a nation is "used" or "available".
****************************************************************************/
enum barbarian_type nation_barbarian_type(const struct nation_type *nation)
{
  if (!bounds_check_nation(nation, LOG_FATAL, "nation_barbarian_type")) {
    die("wrong nation %d", nation->index);
  }
  return nation->barb_type;
}

/***************************************************************
Returns pointer to the array of the nation leader names, and
sets dim to number of leaders.
***************************************************************/
struct leader *get_nation_leaders(const struct nation_type *nation, int *dim)
{
  if (!bounds_check_nation(nation, LOG_FATAL, "get_nation_leader_names")) {
    die("wrong nation %d", nation->index);
  }
  *dim = nation->leader_count;
  return nation->leaders;
}

/****************************************************************************
  Returns pointer to the preferred set of nations that can fork from the
  nation.  The array is terminated by a NO_NATION_SELECTED value.
****************************************************************************/
struct nation_type **get_nation_civilwar(const struct nation_type *nation)
{
  return nation->civilwar_nations;
}

/***************************************************************
Returns sex of given leader name. If names is not found,
return 1 (meaning male).
***************************************************************/
bool get_nation_leader_sex(const struct nation_type *nation, const char *name)
{
  int i;
  
  if (!bounds_check_nation(nation, LOG_ERROR, "get_nation_leader_sex")) {
    return FALSE;
  }
  for (i = 0; i < nation->leader_count; i++) {
    if (strcmp(nation->leaders[i].name, name) == 0) {
      return nation->leaders[i].is_male;
    }
  }
  return TRUE;
}

/***************************************************************
checks if given leader name exist for given nation.
***************************************************************/
bool check_nation_leader_name(const struct nation_type *pnation,
			      const char *name)
{
  int i;
  
  if (!bounds_check_nation(pnation, LOG_ERROR, "check_nation_leader_name")) {
    return TRUE;			/* ? */
  }
  for (i = 0; i < pnation->leader_count; i++) {
    if (strcmp(name, pnation->leaders[i].name) == 0) {
      return TRUE;
    }
  }
  return FALSE;
}

/****************************************************************************
  Return the nation of a player.
****************************************************************************/
struct nation_type *nation_of_player(const struct player *plr)
{
  assert(plr != NULL);
  if (!bounds_check_nation(plr->nation, LOG_FATAL, "nation_of_player")) {
    die("wrong nation %d", plr->nation->index);
  }
  return plr->nation;
}

/****************************************************************************
  Return the nation of the player who owns the city.
****************************************************************************/
struct nation_type *nation_of_city(const struct city *pcity)
{
  assert(pcity != NULL);
  return nation_of_player(city_owner(pcity));
}

/****************************************************************************
  Return the nation of the player who owns the unit.
****************************************************************************/
struct nation_type *nation_of_unit(const struct unit *punit)
{
  assert(punit != NULL);
  return nation_of_player(unit_owner(punit));
}

/****************************************************************************
  Return the nation with the given index.

  This function returns NULL for an out-of-range index (some callers
  rely on this).
****************************************************************************/
struct nation_type *nation_by_number(const Nation_type_id nation)
{
  if (nation < 0 || nation >= game.control.nation_count) {
    return NULL;
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
    nations[i].index = i;
  }
}

/***************************************************************
 De-allocate resources associated with the given nation.
***************************************************************/
static void nation_free(struct nation_type *pnation)
{
  int i;

  for (i = 0; i < pnation->leader_count; i++) {
    free(pnation->leaders[i].name);
  }
  if (pnation->leaders) {
    free(pnation->leaders);
    pnation->leaders = NULL;
  }
  
  if (pnation->legend) {
    free(pnation->legend);
    pnation->legend = NULL;
  }

  if (pnation->civilwar_nations) {
    free(pnation->civilwar_nations);
    pnation->civilwar_nations = NULL;
  }

  if (pnation->parent_nations) {
    free(pnation->parent_nations);
    pnation->parent_nations = NULL;
  }
  
  if (pnation->groups) {
    free(pnation->groups);
    pnation->groups = NULL;
  }
  
  if (pnation->conflicts_with) {
    free(pnation->conflicts_with);
    pnation->conflicts_with = NULL;
  }

  nation_city_names_free(pnation->city_names);
  pnation->city_names = NULL;
}

/***************************************************************
 De-allocate the currently allocated nations and remove all
 nation groups
***************************************************************/
void nations_free(void)
{
  if (!nations) {
    return;
  }

  nations_iterate(pnation) {
    nation_free(pnation);
  } nations_iterate_end;

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
int city_style_of_nation(const struct nation_type *pnation)
{
  if (!bounds_check_nation(pnation, LOG_FATAL, "city_style_of_nation")) {
    die("wrong nation %d", pnation->index);
  }
  return pnation->city_style;
}

/***************************************************************
  Add new group into the array of groups.
***************************************************************/
struct nation_group *add_new_nation_group(const char *name)
{
  int i;
  struct nation_group *group;

  if (strlen(name) >= ARRAY_SIZE(group->name)) {
    freelog(LOG_FATAL, "Too-long group name %s.", name);
    exit(EXIT_FAILURE);
  }

  for (i = 0; i < num_nation_groups; i++) {
    if (mystrcasecmp(Qn_(name), Qn_(nation_groups[i].name)) == 0) {
      freelog(LOG_FATAL, "Duplicate group name %s.",
	      Qn_(name));
      exit(EXIT_FAILURE);
    }
  }
  if (num_nation_groups == MAX_NUM_NATION_GROUPS) {
    freelog(LOG_FATAL, "Too many groups of nations (%d is the maximum)",
	    MAX_NUM_NATION_GROUPS);
    exit(EXIT_FAILURE);
  }

  group = nation_groups + num_nation_groups;
  group->index = num_nation_groups;
  sz_strlcpy(group->name, name);
  group->match = 0;

  num_nation_groups++;

  return group;
}

/****************************************************************************
  Return the number of nation groups.
****************************************************************************/
int get_nation_groups_count(void)
{
  return num_nation_groups;
}

/****************************************************************************
  Return the government with the given index.

  This function returns NULL for an out-of-range index (some callers
  rely on this).
****************************************************************************/
struct nation_group* nation_group_by_number(int id)
{
  if (id < 0 && id >= num_nation_groups) {
    return NULL;
  }
  return &nation_groups[id];
}

/****************************************************************************
  Return the nation group that has the given (untranslated) rule name.
  Returns NULL if no group is found.
****************************************************************************/
struct nation_group *find_nation_group_by_rule_name(const char *name)
{
  const char *qname = Qn_(name);
  int i;

  for (i = 0; i < num_nation_groups; i++) {
    if (0 == mystrcasecmp(Qn_(nation_groups[i].name), qname)) {
      return &nation_groups[i];
    }
  }

  return NULL;
}

/****************************************************************************
  Check if the given nation is in a given group
****************************************************************************/
bool is_nation_in_group(struct nation_type *nation,
			struct nation_group *group)
{
  int i;

  for (i = 0; i < nation->num_groups; i++) {
    if (nation->groups[i] == group) {
      return TRUE;
    }
  }
  return FALSE;
}

/****************************************************************************
  Frees and resets all nation group data.
****************************************************************************/
void nation_groups_free(void)
{
  num_nation_groups = 0;
}

/****************************************************************************
  Return TRUE iff the editor is allowed to edit the player's nation in
  pregame.
****************************************************************************/
bool can_conn_edit_players_nation(const struct connection *pconn,
				  const struct player *pplayer)
{
  return (can_conn_edit(pconn)
          || (game.info.is_new_game
	      && ((!pconn->observer && pconn->player == pplayer)
	           || pconn->access_level >= ALLOW_CTRL)));
}
