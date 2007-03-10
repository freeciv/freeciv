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
#include "game.h"
#include "tile.h"
#include "unit.h"

static struct base_type base_types[BASE_LAST];

static const char *base_type_flag_names[] = {
  "NoAggressive", "DefenseBonus", "NoStackDeath",
  "ClaimTerritory", "DiplomatDefense", "ParadropFrom"
};

/* This must correspond to enum base_gui_type in base.h */
static const char *base_gui_type_names[] = {
  "Fortress", "Airbase", "Other"
};

/****************************************************************************
  Check if base provides effect
****************************************************************************/
bool base_flag(const struct base_type *pbase, enum base_flag_id flag)
{
  return BV_ISSET(pbase->flags, flag);
}

/****************************************************************************
  Is base native to unit class?
****************************************************************************/
bool is_native_base_to_class(const struct unit_class *pclass,
                             const struct base_type *pbase)
{
  return BV_ISSET(pbase->native_to, pclass->id);
}

/****************************************************************************
  Is base native to unit?
****************************************************************************/
bool is_native_base(const struct unit_type *punittype,
                    const struct base_type *pbase)
{
  return is_native_base_to_class(get_unit_class(punittype), pbase);
}

/****************************************************************************
  Base provides base flag for unit? Checks if base provides flag and if
  base is native to unit.
****************************************************************************/
bool base_flag_affects_unit(const struct unit_type *punittype,
                            const struct base_type *pbase,
                            enum base_flag_id flag)
{
  return base_flag(pbase, flag) && is_native_base(punittype, pbase);
}

/**************************************************************************
  Return the translated name of the base type.
**************************************************************************/
const char *base_name(const struct base_type *pbase)
{
  return pbase->name;
}

/**************************************************************************
  Can unit build base to given tile?
**************************************************************************/
bool can_build_base(const struct unit *punit, const struct base_type *pbase,
                    const struct tile *ptile)
{
  if (tile_get_city(ptile)) {
    /* Bases cannot be built inside cities */
    return FALSE;
  }

  return are_reqs_active(unit_owner(punit), NULL, NULL, ptile,
                         unit_type(punit), NULL, NULL, &pbase->reqs);
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
    requirement_vector_init(&base_types[i].reqs);
  }
}

/****************************************************************************
  Free the memory associated with base types
****************************************************************************/
void base_types_free(void)
{
  base_type_iterate(pbase) {
    requirement_vector_free(&pbase->reqs);
  } base_type_iterate_end;
}

/**************************************************************************
  Convert base gui type names to enum; case insensitive;
  returns BASE_GUI_LAST if can't match.
**************************************************************************/
enum base_gui_type base_gui_type_from_str(const char *s)
{
  enum base_gui_type i;
  
  assert(ARRAY_SIZE(base_gui_type_names) == BASE_GUI_LAST);
  
  for(i = 0; i < BASE_GUI_LAST; i++) {
    if (mystrcasecmp(base_gui_type_names[i], s)==0) {
      return i;
    }
  }
  return BASE_GUI_LAST;
}

/**************************************************************************
  Get best gui_type base for given parameters
**************************************************************************/
struct base_type *get_base_by_gui_type(enum base_gui_type type,
                                       const struct unit *punit,
                                       const struct tile *ptile)
{
  base_type_iterate(pbase) {
    if (type == pbase->gui_type
        && (!punit || can_build_base(punit, pbase, ptile))) {
      return pbase;
    }
  } base_type_iterate_end;

  return NULL;
}
