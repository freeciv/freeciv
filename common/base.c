/****************************************************************************
 Freeciv - Copyright (C) 2004 - The Freeciv Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
****************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>

#include "base.h"

static struct base_type base_types[BASE_LAST];

static const char *base_type_flag_names[] = {
  "NoAggressive", "DefenseBonus", "NoStackDeath", "Watchtower",
  "ClaimTerritory", "DiplomatDefense", "Refuel", "NoHPLoss",
  "AttackUnreachable", "ParadropFrom"
};

/****************************************************************************
  Check if base provides effect
****************************************************************************/
bool base_flag(const struct base_type *pbase, enum base_flag_id flag)
{
  return BV_ISSET(pbase->flags, flag);
}

/**************************************************************************
  Return the translated name of the base type.
**************************************************************************/
const char *base_name(const struct base_type *pbase)
{
  return pbase->name;
}

/****************************************************************************
  Determine base type from specials. Returns NULL if there is no base
****************************************************************************/
struct base_type *base_type_get_from_special(bv_special spe)
{
  if (contains_special(spe, S_FORTRESS)) {
    return base_type_get_by_id(BASE_FORTRESS);
  }
  if (contains_special(spe, S_AIRBASE)) {
    return base_type_get_by_id(BASE_AIRBASE);
  }

  return NULL;
}

/**************************************************************************
  Convert base flag names to enum; case insensitive;
  returns BF_LAST if can't match.
**************************************************************************/
enum base_flag_id base_flag_from_str(const char *s)
{
  enum base_flag_id i;
  
  assert(ARRAY_SIZE(base_type_flag_names) == BF_LAST);
  
  for(i = 0; i < BF_LAST; i++) {
    if (mystrcasecmp(base_type_flag_names[i], s)==0) {
      return i;
    }
  }
  return BF_LAST;
}
  
/****************************************************************************
  Returns base type structure for an ID value.
****************************************************************************/
struct base_type *base_type_get_by_id(Base_type_id id)
{
  if (id < 0 || id >= BASE_LAST) {
    return NULL;
  }
  return &base_types[id];
}

/****************************************************************************
  Inialize base_type structures.
****************************************************************************/
void base_types_init(void)
{
  int i;

  for (i = 0; i < ARRAY_SIZE(base_types); i++) {
    base_types[i].id = i;
  }
}
