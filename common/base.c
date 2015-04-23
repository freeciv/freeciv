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
#include <fc_config.h>
#endif

/* common */
#include "extras.h"
#include "game.h"
#include "map.h"
#include "tile.h"
#include "unit.h"

#include "base.h"

/****************************************************************************
  Check if base provides effect
****************************************************************************/
bool base_has_flag(const struct base_type *pbase, enum base_flag_id flag)
{
  return BV_ISSET(pbase->flags, flag);
}

/****************************************************************************
  Returns TRUE iff any cardinally adjacent tile contains a base with
  the given flag (does not check ptile itself)
****************************************************************************/
bool is_base_flag_card_near(const struct tile *ptile, enum base_flag_id flag)
{
  cardinal_adjc_iterate(ptile, adjc_tile) {
    base_type_iterate(pbase) {
      if (base_has_flag(pbase, flag) && tile_has_base(adjc_tile, pbase)) {
        return TRUE;
      }
    } base_type_iterate_end;
  } cardinal_adjc_iterate_end;

  return FALSE;
}

/****************************************************************************
  Returns TRUE iff any adjacent tile contains a base with the given flag
  (does not check ptile itself)
****************************************************************************/
bool is_base_flag_near_tile(const struct tile *ptile, enum base_flag_id flag)
{
  adjc_iterate(ptile, adjc_tile) {
    base_type_iterate(pbase) {
      if (base_has_flag(pbase, flag) && tile_has_base(adjc_tile, pbase)) {
        return TRUE;
      }
    } base_type_iterate_end;
  } adjc_iterate_end;

  return FALSE;
}

/****************************************************************************
  Is tile native to base?
****************************************************************************/
bool is_native_tile_to_base(const struct base_type *pbase,
                            const struct tile *ptile)
{
  struct extra_type *pextra;

  pextra = base_extra_get(pbase);

  return are_reqs_active(NULL, NULL, NULL, NULL, ptile,
                         NULL, NULL, NULL, NULL, NULL,
                         &pextra->reqs, RPT_POSSIBLE);
}

/****************************************************************************
  Base provides base flag for unit? Checks if base provides flag and if
  base is native to unit.
****************************************************************************/
bool base_has_flag_for_utype(const struct base_type *pbase,
                             enum base_flag_id flag,
                             const struct unit_type *punittype)
{
  return base_has_flag(pbase, flag)
    && is_native_extra_to_utype(base_extra_get(pbase), punittype);
}

/**************************************************************************
  Return the (translated) name of the base type.
  You don't have to free the return pointer.
**************************************************************************/
const char *base_name_translation(const struct base_type *pbase)
{
  struct extra_type *pextra = base_extra_get(pbase);

  if (pextra == NULL) {
    return NULL;
  }

  return extra_name_translation(pextra);
}

/**************************************************************************
  Return the (untranslated) rule name of the base type.
  You don't have to free the return pointer.
**************************************************************************/
const char *base_rule_name(const struct base_type *pbase)
{
  struct extra_type *pextra = base_extra_get(pbase);

  if (pextra == NULL) {
    return NULL;
  }

  return extra_rule_name(pextra);
}

/**************************************************************************
  Returns base type matching rule name or NULL if there is no base type
  with such name.
**************************************************************************/
struct base_type *base_type_by_rule_name(const char *name)
{
  struct extra_type *pextra = extra_type_by_rule_name(name);

  if (pextra == NULL || !is_extra_caused_by(pextra, EC_BASE)) {
    return NULL;
  }

  return extra_base_get(pextra);
}

/**************************************************************************
  Returns base type matching the translated name, or NULL if there is no
  base type with that name.
**************************************************************************/
struct base_type *base_type_by_translated_name(const char *name)
{
  struct extra_type *pextra = extra_type_by_translated_name(name);

  if (pextra == NULL || !is_extra_caused_by(pextra, EC_BASE)) {
    return NULL;
  }

  return extra_base_get(pextra);
}

/**************************************************************************
  Can unit build base to given tile?
**************************************************************************/
bool base_can_be_built(const struct base_type *pbase,
                       const struct tile *ptile)
{
  if (tile_terrain(ptile)->base_time == 0) {
    /* Bases cannot be built on this terrain. */
    return FALSE;
  }

  if (!(base_extra_get(pbase)->buildable)) {
    /* Base type not buildable. */
    return FALSE;
  }

  if (tile_has_base(ptile, pbase)) {
    /* Exist already */
    return FALSE;
  }

  if (tile_city(ptile) != NULL && pbase->border_sq >= 0) {
    return FALSE;
  }

  return TRUE;
}

/****************************************************************************
  Tells if player can build base to tile with suitable unit.
****************************************************************************/
bool player_can_build_base(const struct base_type *pbase,
                           const struct player *pplayer,
                           const struct tile *ptile)
{
  struct extra_type *pextra;

  if (!base_can_be_built(pbase, ptile)) {
    return FALSE;
  }

  pextra = base_extra_get(pbase);

  return are_reqs_active(pplayer, tile_owner(ptile), NULL, NULL, ptile,
                         NULL, NULL, NULL, NULL, NULL,
                         &pextra->reqs, RPT_POSSIBLE);
}

/**************************************************************************
  Can unit build base to given tile?
**************************************************************************/
bool can_build_base(const struct unit *punit, const struct base_type *pbase,
                    const struct tile *ptile)
{
  struct extra_type *pextra;

  if (!base_can_be_built(pbase, ptile)) {
    return FALSE;
  }

  pextra = base_extra_get(pbase);

  return are_reqs_active(unit_owner(punit), tile_owner(ptile), NULL, NULL,
                         ptile, punit, unit_type(punit), NULL, NULL, NULL,
                         &pextra->reqs, RPT_CERTAIN);
}

/****************************************************************************
  Returns base_type entry for an ID value.
****************************************************************************/
struct base_type *base_by_number(const Base_type_id id)
{
  struct extra_type_list *bases;

  bases = extra_type_list_by_cause(EC_BASE);

  if (bases == NULL || id < 0 || id >= extra_type_list_size(bases)) {
    return NULL;
  }

  return extra_base_get(extra_type_list_get(bases, id));
}

/**************************************************************************
  Return the base index.
**************************************************************************/
Base_type_id base_number(const struct base_type *pbase)
{
  fc_assert_ret_val(NULL != pbase, -1);
  return pbase->item_number;
}

/**************************************************************************
  Return the base index.

  Currently same as base_number(), paired with base_count()
  indicates use as an array index.
**************************************************************************/
Base_type_id base_index(const struct base_type *pbase)
{
  fc_assert_ret_val(NULL != pbase, -1);

  /* FIXME: */
  /*  return pbase - base_types; */
  return base_number(pbase);
}

/**************************************************************************
  Return extra that base is.
**************************************************************************/
struct extra_type *base_extra_get(const struct base_type *pbase)
{
  return pbase->self;
}

/**************************************************************************
  Return the number of base_types.
**************************************************************************/
Base_type_id base_count(void)
{
  return game.control.num_base_types;
}

/****************************************************************************
  Initialize base_type structures.
****************************************************************************/
void base_type_init(struct extra_type *pextra, int idx)
{
  struct base_type *pbase;

  pbase = fc_malloc(sizeof(*pbase));

  pextra->data.base = pbase;

  pbase->item_number = idx;
  pbase->self = pextra;
}

/****************************************************************************
  Free the memory associated with base types
****************************************************************************/
void base_types_free(void)
{
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

/**************************************************************************
  Does this base type claim territory?
**************************************************************************/
bool territory_claiming_base(const struct base_type *pbase)
{
  return pbase->border_sq >= 0;
}
