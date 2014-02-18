/**********************************************************************
 Freeciv - Copyright (C) 1996-2013 - Freeciv Development Team
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
#include <fc_config.h>
#endif

/* common */
#include "diptreaty.h"
#include "metaknowledge.h"
#include "tile.h"

/**************************************************************************
  An AND function for mk_eval_result.
**************************************************************************/
enum mk_eval_result mke_and(enum mk_eval_result one,
                            enum mk_eval_result two) {
  if (MKE_FALSE == one || MKE_FALSE == two) {
    return MKE_FALSE;
  }

  if (MKE_UNCERTAIN == one || MKE_UNCERTAIN == two) {
    return MKE_UNCERTAIN;
  }

  return MKE_TRUE;
}

/**************************************************************************
  Is an evalutaion of the requirement accurate when pow_player evaluates
  it?

  TODO: Move the data to a data file. That will
        - let non programmers help complete it and/or fix what is wrong
        - let clients not written in C use the data
**************************************************************************/
static bool is_req_knowable(const struct player *pow_player,
                            const struct player *target_player,
                            const struct player *other_player,
                            const struct city *target_city,
                            const struct impr_type *target_building,
                            const struct tile *target_tile,
                            const struct unit *target_unit,
                            const struct output_type *target_output,
                            const struct specialist *target_specialist,
                            const struct requirement *req)
{
  fc_assert_ret_val_msg(NULL != pow_player, false, "No point of view");

  if (req->source.kind == VUT_UTFLAG || req->source.kind == VUT_UTYPE) {
    return target_unit && can_player_see_unit(pow_player, target_unit);
  }

  if (req->source.kind == VUT_DIPLREL
      && pow_player == target_player
      && (req->range == REQ_RANGE_LOCAL
          || req->range == REQ_RANGE_PLAYER)) {
    return TRUE;
  }

  if (req->source.kind == VUT_MINSIZE && target_city != NULL) {
    enum known_type vision =
        tile_get_known(city_tile(target_city), pow_player);

    if (vision == TILE_KNOWN_SEEN
        || city_owner(target_city) == pow_player) {
      return TRUE;
    }
  }

  if (req->source.kind == VUT_CITYTILE
      && req->range == REQ_RANGE_LOCAL) {
    enum known_type vision =
        tile_get_known(target_tile, pow_player);

    if (vision == TILE_KNOWN_SEEN
        || (target_city && city_owner(target_city) == pow_player)) {
      return TRUE;
    }
  }

  if (req->source.kind == VUT_NATION) {
    return TRUE;
  }

  if (req->source.kind == VUT_ADVANCE || req->source.kind == VUT_TECHFLAG) {
    if (req->range == REQ_RANGE_PLAYER
        && (pow_player == target_player
            || player_has_embassy(pow_player, target_player))) {
      return TRUE;
    }
  }

  if (req->source.kind == VUT_GOVERNMENT) {
    if (req->range == REQ_RANGE_PLAYER
        && (pow_player == target_player
            || could_intel_with_player(pow_player, target_player))) {
      return TRUE;
    }
  }

  /* Uncertain or no support added yet. */
  return FALSE;
}

/**************************************************************************
  Evaluate a single requirement given pow_player's knowledge.

  Note: Assumed to use pow_player's data.
**************************************************************************/
enum mk_eval_result
mke_eval_req(const struct player *pow_player,
             const struct player *target_player,
             const struct player *other_player,
             const struct city *target_city,
             const struct impr_type *target_building,
             const struct tile *target_tile,
             const struct unit *target_unit,
             const struct output_type *target_output,
             const struct specialist *target_specialist,
             const struct requirement *req)
{
  if (!is_req_knowable(pow_player, target_player, other_player,
                       target_city, target_building, target_tile,
                       target_unit, target_output,
                       target_specialist, req)) {
    return MKE_UNCERTAIN;
  }

  if (is_req_active(target_player, other_player, target_city,
                    target_building, target_tile, unit_type(target_unit),
                    target_output, target_specialist, req, RPT_CERTAIN)) {
    return MKE_TRUE;
  } else {
    return MKE_FALSE;
  }
}

/**************************************************************************
  Evaluate a requirement vector given pow_player's knowledge.

  Note: Assumed to use pow_player's data.
**************************************************************************/
enum mk_eval_result
mke_eval_reqs(const struct player *pow_player,
              const struct player *target_player,
              const struct player *other_player,
              const struct city *target_city,
              const struct impr_type *target_building,
              const struct tile *target_tile,
              const struct unit *target_unit,
              const struct output_type *target_output,
              const struct specialist *target_specialist,
              const struct requirement_vector *reqs) {
  enum mk_eval_result current;
  enum mk_eval_result result;

  result = MKE_TRUE;
  requirement_vector_iterate(reqs, preq) {
    current = mke_eval_req(pow_player, target_player, other_player,
                           target_city, target_building, target_tile,
                           target_unit, target_output,
                           target_specialist, preq);
    if (current == MKE_FALSE) {
      return MKE_FALSE;
    } else if (current == MKE_UNCERTAIN) {
      result = MKE_UNCERTAIN;
    }
  } requirement_vector_iterate_end;

  return result;
}
