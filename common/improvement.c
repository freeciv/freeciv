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
#include <assert.h>
#include <string.h>

#include "game.h"
#include "support.h"
#include "tech.h"

#include "improvement.h"

/**************************************************************************
All the city improvements:
Use get_improvement_type(id) to access the array.
The improvement_types array is now setup in:
   server/ruleset.c (for the server)
   client/packhand.c (for the client)
**************************************************************************/
struct impr_type improvement_types[B_LAST];

/* Names of effect ranges.
 * (These must correspond to enum effect_range_id in improvement.h.)
 */
static char *effect_range_names[] = {
  "None",
  "Building",
  "City",
  "Island",
  "Player",
  "World"
};

/* Names of effect types.
 * (These must correspond to enum effect_type_id in improvement.h.)
 */
static char *effect_type_names[] = {
  "Adv_Parasite",
  "Airlift",
  "Any_Government",
  "Barb_Attack",
  "Barb_Defend",
  "Capital_City",
  "Capital_Exists",
  "Enable_Nuke",
  "Enable_Space",
  "Enemy_Peaceful",
  "Food_Add_Tile",
  "Food_Bonus",
  "Food_Inc_Tile",
  "Food_Per_Tile",
  "Give_Imm_Adv",
  "Growth_Food",
  "Have_Embassies",
  "Improve_Rep",
  "Luxury_Bonus",
  "Luxury_Pct",
  "Make_Content",
  "Make_Content_Mil",
  "Make_Content_Pct",
  "Make_Happy",
  "May_Declare_War",
  "No_Anarchy",
  "No_Sink_Deep",
  "Nuke_Proof",
  "Pollu_Adj",
  "Pollu_Adj_Pop",
  "Pollu_Adj_Prod",
  "Pollu_Set",
  "Pollu_Set_Pop",
  "Pollu_Set_Prod",
  "Prod_Add_Tile",
  "Prod_Bonus",
  "Prod_Inc_Tile",
  "Prod_Per_Tile",
  "Prod_To_Gold",
  "Reduce_Corrupt",
  "Reduce_Waste",
  "Reveal_Cities",
  "Reveal_Map",
  "Revolt_Dist",
  "Science_Bonus",
  "Science_Pct",
  "Size_Unlimit",
  "Slow_Nuke_Winter",
  "Slow_Global_Warm",
  "Space_Part",
  "Spy_Resistant",
  "Tax_Bonus",
  "Tax_Pct",
  "Trade_Add_Tile",
  "Trade_Bonus",
  "Trade_Inc_Tile",
  "Trade_Per_Tile",
  "Trade_Route_Pct",
  "Unit_Defend",
  "Unit_Move",
  "Unit_No_Lose_Pop",
  "Unit_Recover",
  "Unit_Repair",
  "Unit_Vet_Combat",
  "Unit_Veteran",
  "Upgrade_Units",
  "Upgrade_One_Step",
  "Upgrade_One_Leap",
  "Upgrade_All_Step",
  "Upgrade_All_Leap",
  "Upkeep_Free"
};

/**************************************************************************
  Convert effect range names to enum; case insensitive;
  returns EFR_LAST if can't match.
**************************************************************************/
Eff_Range_id effect_range_from_str(char *str)
{
  Eff_Range_id ret_id;

  assert(sizeof(effect_range_names)/sizeof(effect_range_names[0])==EFR_LAST);

  for (ret_id = 0; ret_id < EFR_LAST; ret_id++) {
    if (0 == mystrcasecmp(effect_range_names[ret_id], str)) {
      return ret_id;
    }
  }

  return EFR_LAST;
}

/**************************************************************************
  Return effect range name; NULL if bad id.
**************************************************************************/
char *effect_range_name(Eff_Range_id id)
{
  assert(sizeof(effect_range_names)/sizeof(effect_range_names[0])==EFR_LAST);

  if (id < EFR_LAST) {
    return effect_range_names[id];
  } else {
    return NULL;
  }
}

/**************************************************************************
  Convert effect type names to enum; case insensitive;
  returns EFT_LAST if can't match.
**************************************************************************/
Eff_Type_id effect_type_from_str(char *str)
{
  Eff_Type_id ret_id;

  assert(sizeof(effect_type_names)/sizeof(effect_type_names[0])==EFT_LAST);

  for (ret_id = 0; ret_id < EFT_LAST; ret_id++) {
    if (0 == mystrcasecmp(effect_type_names[ret_id], str)) {
      return ret_id;
    }
  }

  return EFT_LAST;
}

/**************************************************************************
  Return effect type name; NULL if bad id.
**************************************************************************/
char *effect_type_name(Eff_Type_id id)
{
  assert(sizeof(effect_type_names)/sizeof(effect_type_names[0])==EFT_LAST);

  if (id < EFT_LAST) {
    return effect_type_names[id];
  } else {
    return NULL;
  }
}

/**************************************************************************
Returns 1 if the improvement_type "exists" in this game, 0 otherwise.
An improvement_type doesn't exist if one of:
- id is out of range;
- the improvement_type has been flagged as removed by setting its
  tech_req to A_LAST;
- it is a space part, and the spacerace is not enabled.
Arguably this should be called improvement_type_exists, but that's too long.
**************************************************************************/
int improvement_exists(Impr_Type_id id)
{
  if (id<0 || id>=B_LAST || id>=game.num_impr_types)
    return 0;

  if ((id==B_SCOMP || id==B_SMODULE || id==B_SSTRUCTURAL)
      && !game.spacerace)
    return 0;

  return (improvement_types[id].tech_req!=A_LAST);
}

/**************************************************************************
...
**************************************************************************/
struct impr_type *get_improvement_type(Impr_Type_id id)
{
  return &improvement_types[id];
}

/**************************************************************************
...
**************************************************************************/
char *get_improvement_name(Impr_Type_id id)
{
  return get_improvement_type(id)->name; 
}

/**************************************************************************
...
**************************************************************************/
int improvement_value(Impr_Type_id id)
{
  return (improvement_types[id].build_cost);
}

/**************************************************************************
...
**************************************************************************/
int is_wonder(Impr_Type_id id)
{
  return (improvement_types[id].is_wonder);
}

/**************************************************************************
Does a linear search of improvement_types[].name
Returns B_LAST if none match.
**************************************************************************/
Impr_Type_id find_improvement_by_name(char *s)
{
  int i;

  for( i=0; i<game.num_impr_types; i++ ) {
    if (strcmp(improvement_types[i].name, s)==0)
      return i;
  }
  return B_LAST;
}

/**************************************************************************
FIXME: remove when gen-impr obsoletes
**************************************************************************/
int improvement_variant(Impr_Type_id id)
{
  return improvement_types[id].variant;
}

/**************************************************************************
...
**************************************************************************/
int improvement_obsolete(struct player *pplayer, Impr_Type_id id) 
{
  if (improvement_types[id].obsolete_by==A_NONE) 
    return 0;
  return (get_invention(pplayer, improvement_types[id].obsolete_by)
	  ==TECH_KNOWN);
}

/**************************************************************************
...
**************************************************************************/
int wonder_obsolete(Impr_Type_id id)
{ 
  if (improvement_types[id].obsolete_by==A_NONE)
    return 0;
  return (game.global_advances[improvement_types[id].obsolete_by]);
}

/**************************************************************************
Barbarians don't get enough knowledges to be counted as normal players.
**************************************************************************/
int is_wonder_useful(Impr_Type_id id)
{
  if ((id == B_GREAT) && (get_num_human_and_ai_players () < 3)) return 0;
  return 1;
}

/**************************************************************************
  Whether player could build this improvement, assuming they had
  the tech req, and assuming a city with the right pre-reqs etc.
**************************************************************************/
int could_player_eventually_build_improvement(struct player *p,
					      Impr_Type_id id)
{
  if (!improvement_exists(id))
    return 0;

  /* You can't build an improvement if it's tech requirement is Never. */
  /*   if (improvement_types[id].tech_req == A_LAST) return 0; */
  /* This is what "exists" means, done by improvement_exists() above --dwp */
  
  if (id == B_SSTRUCTURAL || id == B_SCOMP || id == B_SMODULE) {
    if (!game.global_wonders[B_APOLLO]) {
      return 0;
    } else {
      if (p->spaceship.state >= SSHIP_LAUNCHED)
	return 0;
      if (id == B_SSTRUCTURAL && p->spaceship.structurals >= NUM_SS_STRUCTURALS)
	return 0;
      if (id == B_SCOMP && p->spaceship.components >= NUM_SS_COMPONENTS)
	return 0;
      if (id == B_SMODULE && p->spaceship.modules >= NUM_SS_MODULES)
	return 0;
    }
  }
  if (is_wonder(id)) {
    if (game.global_wonders[id]) return 0;
  } else {
    if (improvement_obsolete(p, id)) return 0;
  }
  return 1;
}

/**************************************************************************
...
**************************************************************************/
int could_player_build_improvement(struct player *p, Impr_Type_id id)
{
  if (!could_player_eventually_build_improvement(p, id))
    return 0;

  /* Make sure we have the tech /now/.*/
  if (get_invention(p, improvement_types[id].tech_req) == TECH_KNOWN)
    return 1;
  return 0;
}
  
/**************************************************************************
Can a player build this improvement somewhere?  Ignores the
fact that player may not have a city with appropriate prereqs.
**************************************************************************/
int can_player_build_improvement(struct player *p, Impr_Type_id id)
{
  if (!improvement_exists(id))
    return 0;
  if (!player_knows_improvement_tech(p,id))
    return 0;
  return(could_player_build_improvement(p, id));
}
