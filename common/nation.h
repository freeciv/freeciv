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

#include "player.h"

#define MAX_NUM_TECH_GOALS 10
#define MAX_NUM_NATIONS  63
#define MAX_NUM_LEADERS  16

typedef int Nation_Type_id;

struct Sprite;			/* opaque; client-gui specific */

enum advisor_type {ADV_ISLAND, ADV_MILITARY, ADV_TRADE, ADV_SCIENCE, ADV_FOREIGN, 
                   ADV_ATTITUDE, ADV_DOMESTIC, ADV_LAST};

struct nation_type {
  char name[MAX_LEN_NAME];
  char name_plural[MAX_LEN_NAME];
  char flag_graphic_str[MAX_LEN_NAME];
  char flag_graphic_alt[MAX_LEN_NAME];
  int  leader_count;
  char *leader_name[MAX_NUM_LEADERS];
  int  leader_is_male[MAX_NUM_LEADERS];
  char **default_city_names;
  struct Sprite *flag_sprite;

  /* AI hints */
  int attack;               /* c 0 = optimize for food, 2 =  optimize for prod  */
                            /* c0 = large amount of buildings, 2 = units */
  /* attack has been un-implemented for the time being. -- Syela */
  int expand;               /* c0 = transform first ,  2 = build cities first */
  /* expand has been un-implemented, probably permanently. -- Syela */
  int civilized;            /* c 0 = don't use nukes,  2 = use nukes, lots of pollution */
  /* civilized was never implemented, but will be eventually. -- Syela */
  int advisors[ADV_LAST];   /* never implemented either. -- Syela */

  /* Following basically disabled -- Syela */
  /* Now start from strings to be ruleset-friendly --dwp */
  struct {
    char *tech[MAX_NUM_TECH_GOALS];	/* tech goals */
    char *wonder;		/* primary Wonder (maybe primary opponent,
				   if other builds it) */
    char *government;		/* wanted government form */
  } goals_str;
  /* Following are conversions from above strings after rulesets loaded.
   * Note these are implicit zeros in initialization table, so this must
   * come at end of nation struct. Also note the client doesn't
   * use these. */
  struct {
    int tech[MAX_NUM_TECH_GOALS];
    int wonder;
    int government;
  } goals;
};

Nation_Type_id find_nation_by_name(char *name);
char *get_nation_name(Nation_Type_id nation);
char *get_nation_name_plural(Nation_Type_id nation);
char **get_nation_leader_names(Nation_Type_id nation, int *num);
int get_nation_leader_sex(Nation_Type_id nation, char *name);
struct nation_type *get_nation_by_plr(struct player *plr);
struct nation_type *get_nation_by_idx(Nation_Type_id nation);
void init_nation_goals(void);
int check_nation_leader_name(Nation_Type_id nation, char *name);
struct nation_type *alloc_nations(int num);
void free_nations(int num);

#endif  /* FC__NATION_H */
