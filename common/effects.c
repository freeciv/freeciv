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

#include "effects.h"
#include "game.h"
#include "government.h"
#include "improvement.h"
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
  "Building_Cost",
  "Building_Cost_Pct",
  "Capital_City",
  "Capital_Exists",
  "Corrupt_Adj",
  "Corrupt_Pct",
  "Enable_Nuke",
  "Enable_Space",
  "Enemy_Peaceful",
  "Food_Add_Tile",
  "Food_Bonus",
  "Food_Pct",
  "Food_Inc_Tile",
  "Food_Per_Tile",
  "Force_Content",
  "Force_Content_Pct",
  "Give_Imm_Adv",
  "Growth_Food",
  "Have_Embassies",
  "Improve_Rep",
  "Luxury_Bonus",
  "Luxury_Pct",
  "Make_Content",
  "Make_Content_Mil",
  "Make_Content_Mil_Per",
  "Make_Content_Pct",
  "Make_Happy",
  "May_Declare_War",
  "No_Anarchy",
  "No_Sink_Deep",
  "Nuke_Proof",
  "Pollu_Adj",
  "Pollu_Pct",
  "Pollu_Pop_Adj",
  "Pollu_Pop_Pct",
  "Pollu_Prod_Adj",
  "Pollu_Prod_Pct",
  "Prod_Add_Tile",
  "Prod_Bonus",
  "Prod_Pct",
  "Prod_Inc_Tile",
  "Prod_Per_Tile",
  "Prod_To_Gold",
  "Reveal_Cities",
  "Reveal_Map",
  "Revolt_Dist_Adj",
  "Revolt_Dist_Pct",
  "Science_Bonus",
  "Science_Pct",
  "Size_Unlimit",
  "Slow_Nuke_Winter",
  "Slow_Global_Warm",
  "SS_Structural",
  "SS_Component",
  "SS_Module",
  "Spy_Resistant",
  "Tax_Bonus",
  "Tax_Pct",
  "Trade_Add_Tile",
  "Trade_Bonus",
  "Trade_Pct",
  "Trade_Inc_Tile",
  "Trade_Per_Tile",
  "Trade_Route_Pct",
  "Unit_Attack",
  "Unit_Attack_Firepower",
  "Unit_Cost",
  "Unit_Cost_Pct",
  "Unit_Defend",
  "Unit_Defend_Firepower",
  "Unit_Move",
  "Unit_No_Lose_Pop",
  "Unit_Recover",
  "Unit_Repair",
  "Unit_Vet_Combat",
  "Unit_Veteran",
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

/**************************************************************************
  Return TRUE iff the two effects are equal.
**************************************************************************/
bool are_effects_equal(const struct impr_effect *const peff1,
		       const struct impr_effect *const peff2)
{
#define T(name) if(peff1->name!=peff2->name) return FALSE;
  T(type);
  T(range);
  T(amount);
  T(survives);
  T(cond_bldg);
  T(cond_gov);
  T(cond_adv);
  T(cond_eff);
  T(aff_unit);
  T(aff_terr);
  T(aff_spec);
  return TRUE;
}

/**************************************************************************
  Returns TRUE if the building has any effect bonuses of the given type.

  Note that this function returns a boolean rather than an integer value
  giving the exact bonus.  Finding the exact bonus requires knowing the
  effect range and may take longer.  This function should only be used
  in situations where the range doesn't matter.

  TODO:
    1.  This function does not access the effect data directly; instead
        it just associates the effect with a building.
    2.  Only a few effects are supported.
**************************************************************************/
bool building_has_effect(Impr_Type_id building, enum effect_type effect)
{
  switch (effect) {
  case EFT_PROD_TO_GOLD:
    return building == B_CAPITAL;
  case EFT_SS_STRUCTURAL:
    return building == B_SSTRUCTURAL;
  case EFT_SS_COMPONENT:
    return building == B_SCOMP;
  case EFT_SS_MODULE:
    return building == B_SMODULE;
  default:
    break;
  }
  assert(0);
  return FALSE;
}

/**************************************************************************
  Returns the effect bonus the currently-in-construction-item will provide.

  Note this is not called get_current_production_bonus because that would
  be confused with EFT_PROD_BONUS.

  TODO:
    1.  This function does not access the effect data directly; instead
        it just associates the effect with a building.
    2.  Only a few effects are supported.
**************************************************************************/
int get_current_construction_bonus(const struct city *pcity,
				   enum effect_type effect)
{
  if (pcity->is_building_unit) {
    return 0; /* No effects for units. */
  }

  switch (effect) {
  case EFT_PROD_TO_GOLD:
    return (pcity->currently_building == B_CAPITAL) ? 1 : 0;
  case EFT_SS_STRUCTURAL:
    return (pcity->currently_building == B_SSTRUCTURAL) ? 1 : 0;
  case EFT_SS_COMPONENT:
    return (pcity->currently_building == B_SCOMP) ? 1 : 0;
  case EFT_SS_MODULE:
    return (pcity->currently_building == B_SMODULE) ? 1 : 0;
  default:
    /* All others unsupported. */
    break;
  }

  assert(0);
  return 0;
}
