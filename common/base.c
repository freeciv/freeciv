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

/* utility */
#include "fcintl.h"
#include "log.h"
#include "string_vector.h"

/* common */
#include "extras.h"
#include "game.h"
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
  the given flag
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
  Is base native to unit class?
****************************************************************************/
bool is_native_base_to_uclass(const struct base_type *pbase,
                              const struct unit_class *pclass)
{
  return BV_ISSET(pbase->native_to, uclass_index(pclass));
}

/****************************************************************************
  Is base native to unit type?
****************************************************************************/
bool is_native_base_to_utype(const struct base_type *pbase,
                             const struct unit_type *punittype)
{
  return is_native_base_to_uclass(pbase, utype_class(punittype));
}

/****************************************************************************
  Is tile native to base?
****************************************************************************/
bool is_native_tile_to_base(const struct base_type *pbase,
                            const struct tile *ptile)
{
  struct extra_type *pextra;

  pextra = base_extra_get(pbase);

  return are_reqs_active(NULL, NULL, NULL, ptile,
                         NULL, NULL, NULL, &pextra->reqs, RPT_POSSIBLE);
}

/****************************************************************************
  Base provides base flag for unit? Checks if base provides flag and if
  base is native to unit.
****************************************************************************/
bool base_has_flag_for_utype(const struct base_type *pbase,
                             enum base_flag_id flag,
                             const struct unit_type *punittype)
{
  return base_has_flag(pbase, flag) && is_native_base_to_utype(pbase, punittype);
}

/**************************************************************************
  Return the (translated) name of the base type.
  You don't have to free the return pointer.
**************************************************************************/
const char *base_name_translation(const struct base_type *pbase)
{
  struct extra_type *pextra = extra_type_get(EXTRA_BASE, pbase->item_number);

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
  struct extra_type *pextra = extra_type_get(EXTRA_BASE, pbase->item_number);

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

  if (pextra == NULL || pextra->type != EXTRA_BASE) {
    return NULL;
  }

  return &pextra->data.base;
}

/**************************************************************************
  Returns base type matching the translated name, or NULL if there is no
  base type with that name.
**************************************************************************/
struct base_type *base_type_by_translated_name(const char *name)
{
  struct extra_type *pextra = extra_type_by_translated_name(name);

  if (pextra == NULL || pextra->type != EXTRA_BASE) {
    return NULL;
  }

  return &pextra->data.base;
}

/****************************************************************************
  Is there base of the given type cardinally near tile?
****************************************************************************/
bool is_base_card_near(const struct tile *ptile, const struct base_type *pbase)
{
  cardinal_adjc_iterate(ptile, adjc_tile) {
    if (tile_has_base(adjc_tile, pbase)) {
      return TRUE;
    }
  } cardinal_adjc_iterate_end;

  return FALSE;
}

/****************************************************************************
  Is there base of the given type near tile?
****************************************************************************/
bool is_base_near_tile(const struct tile *ptile, const struct base_type *pbase)
{
  adjc_iterate(ptile, adjc_tile) {
    if (tile_has_base(adjc_tile, pbase)) {
      return TRUE;
    }
  } adjc_iterate_end;

  return FALSE;
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

  if (!pbase->buildable) {
    /* Base type not buildable. */
    return FALSE;
  }

  if (tile_has_base(ptile, pbase)) {
    /* Exist already */
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

  return are_reqs_active(pplayer, NULL, NULL, ptile,
                         NULL, NULL, NULL, &pextra->reqs, RPT_POSSIBLE);
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

  return are_reqs_active(unit_owner(punit), NULL, NULL, ptile,
                         unit_type(punit), NULL, NULL, &pextra->reqs,
                         RPT_CERTAIN);
}

/****************************************************************************
  Returns base_type entry for an ID value.
****************************************************************************/
struct base_type *base_by_number(const Base_type_id id)
{
  if (id < 0 || id >= game.control.num_base_types) {
    return NULL;
  }

  return &extra_type_get(EXTRA_BASE, id)->data.base;
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
void base_type_init(int idx)
{
  struct extra_type *pextra = extra_type_get(EXTRA_BASE, idx);

  pextra->type = EXTRA_BASE;

  pextra->data.base.item_number = idx;
  pextra->data.base.helptext = NULL;
  pextra->data.base.self = pextra;
}

/****************************************************************************
  Free the memory associated with base types
****************************************************************************/
void base_types_free(void)
{
  base_type_iterate(pbase) {
    if (NULL != pbase->helptext) {
      strvec_destroy(pbase->helptext);
      pbase->helptext = NULL;
    }
  } base_type_iterate_end;
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
  Can two bases coexist in same tile?
**************************************************************************/
bool can_bases_coexist(const struct base_type *base1, const struct base_type *base2)
{
  if (base1 == base2) {
    return TRUE;
  }

  return !BV_ISSET(base1->conflicts, base_index(base2));
}

/**************************************************************************
  Does this base type claim territory?
**************************************************************************/
bool territory_claiming_base(const struct base_type *pbase)
{
  return pbase->border_sq >= 0;
}

/**************************************************************************
  Who owns bases on tile
**************************************************************************/
struct player *base_owner(const struct tile *ptile)
{
  return ptile->extras_owner;
}
