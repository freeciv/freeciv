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

#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "government.h"
#include "log.h"
#include "mem.h"
#include "player.h"
#include "tech.h"

#include "nation.h"

struct nation_type *nations = NULL;

/***************************************************************
...
***************************************************************/
Nation_Type_id find_nation_by_name(char *name)
{
  int i;

  for(i=0; i<game.nation_count; i++)
     if(!mystrcasecmp(name, get_nation_name (i)))
	return i;

  return -1;
}

/***************************************************************
Returns name of the nation
***************************************************************/
char *get_nation_name(Nation_Type_id nation)
{
  return nations[nation].name;
}

/***************************************************************
Returns pointer to the array of the nation leader names, and
sets dim to number of leaders.
***************************************************************/
char **get_nation_leader_names(Nation_Type_id nation, int *dim)
{
  *dim = nations[nation].leader_count;
  return nations[nation].leader_name;
}

/***************************************************************
Returns sex of given leader name. If names is not found,
return 0 (meaning male).
***************************************************************/
int get_nation_leader_sex(Nation_Type_id nation, char *name)
{
  int i;
  for( i=0; i < nations[nation].leader_count; i++ ) {
    if( !strcmp(nations[nation].leader_name[i],name) )
      break;
  }
  if( i <  nations[nation].leader_count )
    return nations[nation].leader_is_male[i];
  else
    return 0;
}

/***************************************************************
checks if given leader name exist for given nation.
***************************************************************/
int check_nation_leader_name(Nation_Type_id nation, char *name)
{
  int i, found=0;
  for( i=0; i<nations[nation].leader_count; i++) {
    if( !strcmp(name, nations[nation].leader_name[i]) )
      found = 1;
  }
  return found;
}

/***************************************************************
Returns plural name of the nation.
***************************************************************/
char *get_nation_name_plural(Nation_Type_id nation)
{
  return nations[nation].name_plural;
}

/***************************************************************
Returns pointer to a nation 
***************************************************************/
struct nation_type *get_nation_by_plr(struct player *plr)
{
  return &nations[plr->nation];
}

struct nation_type *get_nation_by_idx(Nation_Type_id nation)
{
  return &nations[nation];
}

/***************************************************************
Returns pointer to a nation 
***************************************************************/
struct nation_type *alloc_nations(int num)
{
  nations = fc_calloc( num, sizeof(struct nation_type) );
  return nations;
}

void free_nations(int num)
{
  int i, j;

  if(!nations) return;
  for( i = 0; i < num; i++) {
    for( j = 0; j < nations[i].leader_count; j++) {
      free( nations[i].leader_name[j] );
    }
  }
  free(nations);
}

/**************************************************************************
  This converts the goal strings in nations[] to integers.
  This should only be done after rulesets are loaded!
  Messages are LOG_VERBOSE because its quite possible they
  are not real errors.
**************************************************************************/
void init_nation_goals(void)
{
  char *str, *name;
  int val, i, j;
  struct nation_type *pnation;
  struct government *gov;

  for(pnation=nations; pnation<nations+game.nation_count; pnation++) {
    name = pnation->name_plural;
    str = pnation->goals_str.government;
    gov = find_government_by_name(str); 
    if(gov == NULL) {
      freelog(LOG_VERBOSE, "Didn't match goal government name \"%s\" for %s",
	      str, name);
      val = game.government_when_anarchy;  /* flag value (no goal) (?) */
    } else {
      val = gov->index;
    }
    pnation->goals.government = val;
    
    str = pnation->goals_str.wonder;
    val = find_improvement_by_name(str);
    /* for any problems, leave as B_LAST */
    if(val == B_LAST) {
      freelog(LOG_VERBOSE, "Didn't match goal wonder \"%s\" for %s", str, name);
    } else if(!improvement_exists(val)) {
      freelog(LOG_VERBOSE, "Goal wonder \"%s\" for %s doesn't exist", str, name);
      val = B_LAST;
    } else if(!is_wonder(val)) {
      freelog(LOG_VERBOSE, "Goal wonder \"%s\" for %s not a wonder", str, name);
      val = B_LAST;
    }
    pnation->goals.wonder = val;
    freelog(LOG_DEBUG, "%s wonder goal %d %s", name, val, str);

    /* i is index is goals_str, j is index (good values) in goals */
    j = 0;			
    for(i=0; i<MAX_NUM_TECH_GOALS; i++) {
      str = pnation->goals_str.tech[i];
      if(str[0] == '\0')
	continue;
      val = find_tech_by_name(str);
      if(val == A_LAST) {
	freelog(LOG_VERBOSE, "Didn't match goal tech %d \"%s\" for %s",
		i, str, name);
      } else if(!tech_exists(val)) {
	freelog(LOG_VERBOSE, "Goal tech %d \"%s\" for %s doesn't exist",
		i, str, name);
	val = A_LAST;
      }
      if(val != A_LAST && val != A_NONE) {
	freelog(LOG_DEBUG, "%s tech goal (%d) %3d %s", name, j, val, str);
	pnation->goals.tech[j++] = val;
      }
    }
    freelog(LOG_DEBUG, "%s %d tech goals", name, j);
    if(j==0) {
      freelog(LOG_VERBOSE, "No valid goal techs for %s", name);
    }
    while(j<MAX_NUM_TECH_GOALS) {
      pnation->goals.tech[j++] = A_NONE;
    }
  }
}
