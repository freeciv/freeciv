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
#include "game.h"
#include "map.h"
#include "metaknowledge.h"
#include "tile.h"
#include "traderoutes.h"

/**************************************************************************
  An AND function for fc_tristate.
**************************************************************************/
enum fc_tristate tri_and(enum fc_tristate one,
                         enum fc_tristate two)
{
  if (TRI_NO == one || TRI_NO == two) {
    return TRI_NO;
  }

  if (TRI_MAYBE == one || TRI_MAYBE == two) {
    return TRI_MAYBE;
  }

  return TRI_YES;
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

  if (req->source.kind == VUT_UTFLAG
      || req->source.kind == VUT_UTYPE
      || req->source.kind == VUT_UCLASS
      || req->source.kind == VUT_UCFLAG) {
    switch (req->range) {
    case REQ_RANGE_LOCAL:
      return target_unit && can_player_see_unit(pow_player, target_unit);
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADEROUTE:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_COUNT:
      return FALSE;
    }
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

  if (req->source.kind == VUT_IMPROVEMENT) {
    /* Anyone that can see city internals (like the owner) */
    if (can_player_see_city_internals(pow_player, target_city)) {
      return TRUE;
    }

    /* Cities not owned by pow_player */
    switch (req->range) {
    case REQ_RANGE_WORLD:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_CONTINENT:
      /* Only wonders (great or small) can be required in those ranges.
       * Wonders are always visible. */
      return TRUE;
    case REQ_RANGE_TRADEROUTE:
      /* Could be known for trade routes to cities owned by pow_player as
       * long as the requirement is present. Not present requirements would
       * require knowledge that no trade routes to another foreign city
       * exists (since all possible trade routes are to a city owned by
       * pow_player). Not worth the complexity, IMHO. */
      return FALSE;
    case REQ_RANGE_CITY:
    case REQ_RANGE_LOCAL:
      /* Can't see invisible improvements in foreign cities. */
      if (!is_improvement_visible(req->source.value.building)) {
        return FALSE;
      }

      /* Can see visible improvements in seen cities. */
      if (tile_get_known(city_tile(target_city), pow_player)
          == TILE_KNOWN_SEEN) {
        return TRUE;
      }

      /* Can see visible improvements in cities traded with. */
      trade_routes_iterate(target_city, trade_city) {
        if (city_owner(trade_city) == pow_player) {
          return TRUE;
        }
      } trade_routes_iterate_end;

      /* No way to know if a city has an improvement */
      return FALSE;
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_COUNT:
      /* Not supported by the requirement type. */
      return FALSE;
    }
  }

  if (req->source.kind == VUT_NATION) {
    return TRUE;
  }

  if (req->source.kind == VUT_ADVANCE || req->source.kind == VUT_TECHFLAG) {
    if (req->range == REQ_RANGE_PLAYER
        && can_see_techs_of_target(pow_player, target_player)) {
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

  if (req->source.kind == VUT_MAXTILEUNITS) {
    switch (req->range) {
    case REQ_RANGE_LOCAL:
      return can_player_see_hypotetic_units_at(pow_player, target_tile);
    case REQ_RANGE_CADJACENT:
      if (!can_player_see_hypotetic_units_at(pow_player, target_tile)) {
        return FALSE;
      }
      cardinal_adjc_iterate(target_tile, adjc_tile) {
        if (!can_player_see_hypotetic_units_at(pow_player, adjc_tile)) {
          return FALSE;
        }
      } cardinal_adjc_iterate_end;

      return TRUE;
    case REQ_RANGE_ADJACENT:
      if (!can_player_see_hypotetic_units_at(pow_player, target_tile)) {
        return FALSE;
      }
      adjc_iterate(target_tile, adjc_tile) {
        if (!can_player_see_hypotetic_units_at(pow_player, adjc_tile)) {
          return FALSE;
        }
      } adjc_iterate_end;

      return TRUE;
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADEROUTE:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_COUNT:
      /* Non existing. */
      return FALSE;
    }
  }

  /* Uncertain or no support added yet. */
  return FALSE;
}

/**************************************************************************
  Evaluate a single requirement given pow_player's knowledge.

  Note: Assumed to use pow_player's data.
**************************************************************************/
enum fc_tristate
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
  const struct unit_type *target_unittype;

  if (!is_req_knowable(pow_player, target_player, other_player,
                       target_city, target_building, target_tile,
                       target_unit, target_output,
                       target_specialist, req)) {
    return TRI_MAYBE;
  }

  if (target_unit) {
    target_unittype = unit_type(target_unit);
  } else {
    target_unittype = NULL;
  }

  if (is_req_active(target_player, other_player, target_city,
                    target_building, target_tile, target_unit, target_unittype,
                    target_output, target_specialist, req, RPT_CERTAIN)) {
    return TRI_YES;
  } else {
    return TRI_NO;
  }
}

/**************************************************************************
  Evaluate a requirement vector given pow_player's knowledge.

  Note: Assumed to use pow_player's data.
**************************************************************************/
enum fc_tristate
mke_eval_reqs(const struct player *pow_player,
              const struct player *target_player,
              const struct player *other_player,
              const struct city *target_city,
              const struct impr_type *target_building,
              const struct tile *target_tile,
              const struct unit *target_unit,
              const struct output_type *target_output,
              const struct specialist *target_specialist,
              const struct requirement_vector *reqs)
{
  enum fc_tristate current;
  enum fc_tristate result;

  result = TRI_YES;
  requirement_vector_iterate(reqs, preq) {
    current = mke_eval_req(pow_player, target_player, other_player,
                           target_city, target_building, target_tile,
                           target_unit, target_output,
                           target_specialist, preq);
    if (current == TRI_NO) {
      return TRI_NO;
    } else if (current == TRI_MAYBE) {
      result = TRI_MAYBE;
    }
  } requirement_vector_iterate_end;

  return result;
}

/**************************************************************************
  Can pow_player see the techs of target player?
**************************************************************************/
bool can_see_techs_of_target(const struct player *pow_player,
                             const struct player *target_player)
{
  return pow_player == target_player
      || player_has_embassy(pow_player, target_player);
}
