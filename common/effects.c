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
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <string.h>

#include "effects.h"
#include "game.h"
#include "government.h"
#include "improvement.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "support.h"
#include "tech.h"
#include "shared.h" /* ARRAY_SIZE */


/* Names of effect ranges.
 * (These must correspond to enum effect_range_id in effects.h.)
 * do not change these unless you know what you're doing! */
static const char *effect_range_names[] = {
  "Local",
  "City",
  "Island",
  "Player",
  "World"
};

/* Names of effect types.
 * (These must correspond to enum effect_type_id in effects.h.) */
static const char *effect_type_names[] = {
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
enum effect_range effect_range_from_str(const char *str)
{
  enum effect_range ret_id;

  assert(ARRAY_SIZE(effect_range_names) == EFR_LAST);

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
const char *effect_range_name(enum effect_range id)
{
  assert(ARRAY_SIZE(effect_range_names) == EFR_LAST);

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
enum effect_type effect_type_from_str(const char *str)
{
  enum effect_type ret_id;

  assert(ARRAY_SIZE(effect_type_names) == EFT_LAST);

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
const char *effect_type_name(enum effect_type id)
{
  assert(ARRAY_SIZE(effect_type_names) == EFT_LAST);

  if (id < EFT_LAST) {
    return effect_type_names[id];
  } else {
    return NULL;
  }
}
