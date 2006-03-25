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
#ifndef FC__NATION_H
#define FC__NATION_H

#include "shared.h"		/* MAX_LEN_NAME */

#include "fc_types.h"
#include "terrain.h"		/* T_COUNT */

#define MAX_NUM_TECH_GOALS 10

/* Changing this value will break network compatibility. */
#define NO_NATION_SELECTED (NULL)

/* 
 * Purpose of this constant is to catch invalid ruleset and network
 * data and to allow static allocation of the nation_info packet.
 */
#define MAX_NUM_LEADERS MAX_NUM_ITEMS

#define MAX_NUM_NATION_GROUPS 128

/*
 * The city_name structure holds information about a default choice for
 * the city name.  The "name" field is, of course, just the name for
 * the city.  The "river" and "terrain" fields are entries recording
 * whether the terrain is present near the city - we give higher priority
 * to cities which have matching terrain.  In the case of a river we only
 * care if the city is _on_ the river, for other terrain features we give
 * the bonus if the city is close to the terrain.  Both of these entries
 * may hold a value of 0 (no preference), 1 (city likes the terrain), or -1
 * (city doesn't like the terrain).
 *
 * This is controlled through the nation's ruleset like this:
 *   cities = "Washington (ocean, river, swamp)", "New York (!mountains)"
 */
typedef int ternary;
struct city_name {
  char* name;
  ternary river;
  ternary terrain[MAX_NUM_TERRAINS];	
};

struct leader {
  char *name;
  bool is_male;
};

struct nation_group {
  int index;

  char name[MAX_LEN_NAME];
  
  /* How much the AI will try to select a nation in the same group */
  int match;
};

struct nation_type {
  int index;
  /* Pointer values are allocated on load then freed in free_nations(). */
  const char *name; /* Translated string - doesn't need freeing. */
  const char *name_plural; /* Translated string - doesn't need freeing. */
  char flag_graphic_str[MAX_LEN_NAME];
  char flag_graphic_alt[MAX_LEN_NAME];
  int  leader_count;
  struct leader *leaders;
  int city_style;
  struct city_name *city_names;		/* The default city names. */
  char *legend;				/* may be empty */

  bool is_playable, is_barbarian;

  /* civilwar_nations is a NO_NATION_SELECTED-terminated list of index of
   * the nations that can fork from this one.  parent_nations is the inverse
   * of this array.  Server only. */
  struct nation_type **civilwar_nations;
  struct nation_type **parent_nations;

  /* untranslated copies: */
  char name_orig[MAX_LEN_NAME];
  char name_plural_orig[MAX_LEN_NAME];

  /* Items given to this nation at game start.  Server only. */
  int init_techs[MAX_NUM_TECH_LIST];
  int init_buildings[MAX_NUM_BUILDING_LIST];
  struct government *init_government;
  struct unit_type *init_units[MAX_NUM_UNIT_LIST];

  /* Groups which this nation is assigned to */
  int num_groups;
  struct nation_group **groups;
  
  /* Nations which we don't want in the same game.
   * For example, British and English. */
  int num_conflicts;
  struct nation_type **conflicts_with;

  /* Unavailable nations aren't allowed to be chosen in the scenario. */
  bool is_available;

  struct player *player; /* Who's using the nation, or NULL. */
};

struct nation_type *find_nation_by_name(const char *name);
struct nation_type *find_nation_by_name_orig(const char *name);
const char *get_nation_name(const struct nation_type *nation);
const char *get_nation_name_plural(const struct nation_type *nation);
const char *get_nation_name_orig(const struct nation_type *nation);
bool is_nation_playable(const struct nation_type *nation);
bool is_nation_barbarian(const struct nation_type *nation);
struct leader *get_nation_leaders(const struct nation_type *nation, int *dim);
struct nation_type **get_nation_civilwar(const struct nation_type *nation);
bool get_nation_leader_sex(const struct nation_type *nation,
			   const char *name);
struct nation_type *get_nation_by_plr(const struct player *plr);
struct nation_type *get_nation_by_idx(Nation_type_id nation);
bool check_nation_leader_name(const struct nation_type *nation,
			      const char *name);
void nations_alloc(int num);
void nations_free(void);
void nation_city_names_free(struct city_name *city_names);
int get_nation_city_style(const struct nation_type *nation);

struct nation_group *add_new_nation_group(const char *name);
int get_nation_groups_count(void);
struct nation_group* get_nation_group_by_id(int id);
struct nation_group *find_nation_group_by_name_orig(const char *name);
bool is_nation_in_group(struct nation_type *nation,
			struct nation_group *group);
void nation_groups_free(void);

#define nation_groups_iterate(pgroup)					    \
{									    \
  int _index;								    \
									    \
  for (_index = 0; _index < get_nation_groups_count(); _index++) {	    \
    struct nation_group *pgroup = get_nation_group_by_id(_index);

#define nation_groups_iterate_end					    \
  }									    \
}

bool can_conn_edit_players_nation(const struct connection *pconn,
				  const struct player *pplayer);

/* Iterate over nations.  This iterates over all nations, including
 * unplayable ones (use is_nation_playable to filter if necessary). */
#define nations_iterate(pnation)					    \
{									    \
  int NI_index;								    \
									    \
  for (NI_index = 0;							    \
       NI_index < game.control.nation_count;				    \
       NI_index++) {							    \
    struct nation_type *pnation = get_nation_by_idx(NI_index);

#define nations_iterate_end						    \
  }									    \
}

#endif  /* FC__NATION_H */
