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
#define NO_NATION_SELECTED (Nation_Type_id)(-1)

#define OBSERVER_NATION (game.nation_count - 2)

/* 
 * Purpose of this constant is to catch invalid ruleset and network
 * data and to allow static allocation of the nation_info packet.
 */
#define MAX_NUM_LEADERS MAX_NUM_ITEMS

#define MAX_NUM_TEAMS MAX_NUM_PLAYERS
#define TEAM_NONE 255

typedef int Nation_Type_id;
typedef int Team_Type_id;

struct Sprite;			/* opaque; client-gui specific */

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

struct nation_type {
  /* Pointer values are allocated on load then freed in free_nations(). */
  const char *name; /* Translated string - doesn't need freeing. */
  const char *name_plural; /* Translated string - doesn't need freeing. */
  char flag_graphic_str[MAX_LEN_NAME];
  char flag_graphic_alt[MAX_LEN_NAME];
  int  leader_count;
  struct leader *leaders;
  int city_style;
  struct city_name *city_names;		/* The default city names. */
  struct Sprite *flag_sprite;
  char *class;				/* may be empty */
  char *legend;				/* may be empty */

  /* civilwar_nations is a NO_NATION_SELECTED-terminated list of index of
   * the nations that can fork from this one.  parent_nations is the inverse
   * of this array.  Server only. */
  Nation_Type_id *civilwar_nations;
  Nation_Type_id *parent_nations;

  /* untranslated copies: */
  char name_orig[MAX_LEN_NAME];
  char name_plural_orig[MAX_LEN_NAME];

  /* Items given to this nation at game start.  Server only. */
  int init_techs[MAX_NUM_TECH_LIST];
  int init_buildings[MAX_NUM_BUILDING_LIST];

  /* Following basically disabled -- Syela */
  /* Note the client doesn't use/have these. */
  struct {
    int tech[MAX_NUM_TECH_GOALS];               /* tech goals     */
    int wonder;                                 /* primary Wonder */
    int government;
  } goals;
};

struct team {
  char name[MAX_LEN_NAME];
  Team_Type_id id; /* equal to array index if active, else TEAM_NONE */
};

Nation_Type_id find_nation_by_name(const char *name);
Nation_Type_id find_nation_by_name_orig(const char *name);
const char *get_nation_name(Nation_Type_id nation);
const char *get_nation_name_plural(Nation_Type_id nation);
const char *get_nation_name_orig(Nation_Type_id nation);
struct leader *get_nation_leaders(Nation_Type_id nation, int *dim);
Nation_Type_id *get_nation_civilwar(Nation_Type_id nation);
bool get_nation_leader_sex(Nation_Type_id nation, const char *name);
struct nation_type *get_nation_by_plr(struct player *plr);
struct nation_type *get_nation_by_idx(Nation_Type_id nation);
bool check_nation_leader_name(Nation_Type_id nation, const char *name);
void nations_alloc(int num);
void nations_free(void);
void nation_city_names_free(struct city_name *city_names);
int get_nation_city_style(Nation_Type_id nation);

void team_init(void);
Team_Type_id team_find_by_name(const char *team_name);
struct team *team_get_by_id(Team_Type_id id);
void team_add_player(struct player *pplayer, const char *team_name);
void team_remove_player(struct player *pplayer);
int team_count_members_alive(Team_Type_id id);

#define team_iterate(PI_team)                                                 \
{                                                                             \
  struct team *PI_team;                                                       \
  Team_Type_id PI_p_itr;                                                      \
  for (PI_p_itr = 0; PI_p_itr < MAX_NUM_TEAMS; PI_p_itr++) {                  \
    PI_team = team_get_by_id(PI_p_itr);                                       \
    if (PI_team->id == TEAM_NONE) {                                           \
      continue;                                                               \
    }

#define team_iterate_end                                                      \
  }                                                                           \
}

#endif  /* FC__NATION_H */
