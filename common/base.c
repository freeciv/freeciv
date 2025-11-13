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

/************************************************************************//**
  Tells if player can build base to tile with suitable unit.
****************************************************************************/
bool player_can_build_base(const struct base_type *pbase,
                           const struct player *pplayer,
                           const struct tile *ptile)
{
  struct extra_type *pextra;

  if (!can_build_extra_base(base_extra_get(pbase), pplayer, ptile)) {
    return FALSE;
  }

  pextra = base_extra_get(pbase);

  return are_reqs_active(&(const struct req_context) {
                           .player = pplayer,
                           .tile = ptile,
                         },
                         &(const struct req_context) {
                          .player = tile_owner(ptile),
                         },
                         &pextra->reqs, RPT_POSSIBLE);
}

/************************************************************************//**
  Can unit build base to given tile?
****************************************************************************/
bool can_build_base(const struct unit *punit, const struct base_type *pbase,
                    const struct tile *ptile)
{
  struct extra_type *pextra = base_extra_get(pbase);
  struct player *pplayer = unit_owner(punit);

  if (!can_build_extra_base(pextra, pplayer, ptile)) {
    return FALSE;
  }

  return are_reqs_active(&(const struct req_context) {
                           .player = pplayer,
                           .tile = ptile,
                           .unit = punit,
                           .unittype = unit_type_get(punit),
                         },
                         &(const struct req_context) {
                          .player = tile_owner(ptile),
                         },
                         &pextra->reqs, RPT_CERTAIN);
}

/************************************************************************//**
  Returns base_type entry for an ID value.
****************************************************************************/
struct base_type *base_by_number(const Base_type_id id)
{
  struct extra_type_list *bases;

  bases = extra_type_list_by_cause(EC_BASE);

  if (bases == nullptr || id < 0 || id >= extra_type_list_size(bases)) {
    return nullptr;
  }

  return extra_base_get(extra_type_list_get(bases, id));
}

/************************************************************************//**
  Return the base index.
****************************************************************************/
Base_type_id base_number(const struct base_type *pbase)
{
  fc_assert_ret_val(pbase != nullptr, -1);

  return pbase->item_number;
}

/************************************************************************//**
  Return extra that base is.
****************************************************************************/
struct extra_type *base_extra_get(const struct base_type *pbase)
{
  return pbase->self;
}

/************************************************************************//**
  Return the number of base_types.
****************************************************************************/
Base_type_id base_count(void)
{
  return game.control.num_base_types;
}

/************************************************************************//**
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

/************************************************************************//**
  Free the memory associated with base types
****************************************************************************/
void base_types_free(void)
{
}

/************************************************************************//**
  Get best gui_type base for given parameters
****************************************************************************/
struct base_type *get_base_by_gui_type(enum base_gui_type type,
                                       const struct unit *punit,
                                       const struct tile *ptile)
{
  extra_type_by_cause_iterate(EC_BASE, pextra) {
    struct base_type *pbase = extra_base_get(pextra);

    if (type == pbase->gui_type
        && (punit == nullptr || can_build_base(punit, pbase, ptile))) {
      return pbase;
    }
  } extra_type_by_cause_iterate_end;

  return nullptr;
}

/************************************************************************//**
  Does this base type claim territory?
****************************************************************************/
bool territory_claiming_base(const struct base_type *pbase)
{
  return pbase->border_sq >= 0;
}
