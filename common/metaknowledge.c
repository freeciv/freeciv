/***********************************************************************
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

/**********************************************************************//**
  Returns TRUE iff the target_tile it self and all tiles cardinally
  adjacent to it are seen by pow_player.
**************************************************************************/
static bool is_tile_seen_cadj(const struct player *pow_player,
                              const struct tile *target_tile)
{
  /* The tile it self is unseen. */
  if (!tile_is_seen(target_tile, pow_player)) {
    return FALSE;
  }

  /* A cardinally adjacent tile is unseen. */
  cardinal_adjc_iterate(&(wld.map), target_tile, ptile) {
    if (!tile_is_seen(ptile, pow_player)) {
      return FALSE;
    }
  } cardinal_adjc_iterate_end;

  /* They are all seen. */
  return TRUE;
}

/**********************************************************************//**
  Returns TRUE iff the target_tile it self and all tiles adjacent to it
  are seen by pow_player.
**************************************************************************/
static bool is_tile_seen_adj(const struct player *pow_player,
                             const struct tile *target_tile)
{
  /* The tile it self is unseen. */
  if (!tile_is_seen(target_tile, pow_player)) {
    return FALSE;
  }

  /* An adjacent tile is unseen. */
  adjc_iterate(&(wld.map), target_tile, ptile) {
    if (!tile_is_seen(ptile, pow_player)) {
      return FALSE;
    }
  } adjc_iterate_end;

  /* They are all seen. */
  return TRUE;
}

/**********************************************************************//**
  Returns TRUE iff all tiles of a city are seen by pow_player.
**************************************************************************/
static bool is_tile_seen_city(const struct player *pow_player,
                              const struct city *target_city)
{
  /* Don't know the city radius. */
  if (!can_player_see_city_internals(pow_player, target_city)) {
    return FALSE;
  }

  /* A tile of the city is unseen */
  city_tile_iterate(city_map_radius_sq_get(target_city),
                    city_tile(target_city), ptile) {
    if (!tile_is_seen(ptile, pow_player)) {
      return FALSE;
    }
  } city_tile_iterate_end;

  /* They are all seen. */
  return TRUE;
}

/**********************************************************************//**
  Returns TRUE iff all the tiles of a city and all the tiles of its trade
  partners are seen by pow_player.
**************************************************************************/
static bool is_tile_seen_traderoute(const struct player *pow_player,
                                    const struct city *target_city)
{
  /* Don't know who the trade routes will go to. */
  if (!can_player_see_city_internals(pow_player, target_city)) {
    return FALSE;
  }

  /* A tile of the city is unseen */
  if (!is_tile_seen_city(pow_player, target_city)) {
    return FALSE;
  }

  /* A tile of a trade parter is unseen */
  trade_partners_iterate(target_city, trade_partner) {
    if (!is_tile_seen_city(pow_player, trade_partner)) {
      return FALSE;
    }
  } trade_partners_iterate_end;

  /* They are all seen. */
  return TRUE;
}

/**********************************************************************//**
  Returns TRUE iff pplayer can see all the symmetric diplomatic
  relationships of tplayer.
**************************************************************************/
static bool can_plr_see_all_sym_diplrels_of(const struct player *pplayer,
                                            const struct player *tplayer)
{
  if (pplayer == tplayer) {
    /* Can see own relationships. */
    return TRUE;
  }

  if (team_has_embassy(pplayer->team, tplayer)) {
    /* Gets reports from the embassy. */
    return TRUE;
  }

  if (player_diplstate_get(pplayer, tplayer)->contact_turns_left > 0) {
    /* Can see relationships during contact turns. */
    return TRUE;
  }

  return FALSE;
}

/**********************************************************************//**
  Is an evaluation of the requirement accurate when pov_player evaluates
  it?

  context may be NULL. This is equivalent to passing an empty context.

  TODO: Move the data to a data file. That will
        - let non programmers help complete it and/or fix what is wrong
        - let clients not written in C use the data
**************************************************************************/
static bool is_req_knowable(const struct player *pov_player,
                            const struct req_context *context,
                            const struct player *other_player,
                            const struct requirement *req,
                            const enum   req_problem_type prob_type)
{
  fc_assert_ret_val_msg(NULL != pov_player, FALSE, "No point of view");

  if (context == NULL) {
    context = req_context_empty();
  }

  if (req->source.kind == VUT_UTFLAG
      || req->source.kind == VUT_UTYPE
      || req->source.kind == VUT_UCLASS
      || req->source.kind == VUT_UCFLAG
      || req->source.kind == VUT_MINVETERAN
      || req->source.kind == VUT_MINHP) {
    switch (req->range) {
    case REQ_RANGE_LOCAL:
      if (context->unit == NULL) {
        /* The unit may exist but not be passed when the problem type is
         * RPT_POSSIBLE. */
        return prob_type == RPT_CERTAIN;
      }

      return can_player_see_unit(pov_player, context->unit);
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

  if (req->source.kind == VUT_UNITSTATE) {
    fc_assert_ret_val_msg(req->range == REQ_RANGE_LOCAL, FALSE, "Wrong range");

    if (context->unit == NULL) {
      /* The unit may exist but not be passed when the problem type is
       * RPT_POSSIBLE. */
      return prob_type == RPT_CERTAIN;
    }

    switch (req->source.value.unit_state) {
    case USP_TRANSPORTED:
    case USP_LIVABLE_TILE:
    case USP_TRANSPORTING:
    case USP_NATIVE_TILE:
    case USP_NATIVE_EXTRA:
      /* Known if the unit is seen by the player. */
      return can_player_see_unit(pov_player, context->unit);
    case USP_HAS_HOME_CITY:
    case USP_MOVED_THIS_TURN:
      /* Known to the unit's owner. */
      return unit_owner(context->unit) == pov_player;
    case USP_COUNT:
      fc_assert_msg(req->source.value.unit_state != USP_COUNT,
                    "Invalid unit state property.");
      /* Invalid property is unknowable. */
      return FALSE;
    }
  }

  if (req->source.kind == VUT_MINMOVES) {
    fc_assert_ret_val_msg(req->range == REQ_RANGE_LOCAL, FALSE, "Wrong range");

    if (context->unit == NULL) {
      /* The unit may exist but not be passed when the problem type is
       * RPT_POSSIBLE. */
      return prob_type == RPT_CERTAIN;
    }

    switch (req->range) {
    case REQ_RANGE_LOCAL:
      /* The owner can see if their unit has move fragments left. */
      return unit_owner(context->unit) == pov_player;
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADEROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_COUNT:
      /* Invalid range */
      return FALSE;
    }
  }

  if (req->source.kind == VUT_ACTIVITY) {
    fc_assert_ret_val_msg(req->range == REQ_RANGE_LOCAL,
                          FALSE, "Wrong range");

    if (context->unit == NULL) {
      /* The unit may exist but not be passed when the problem type is
       * RPT_POSSIBLE. */
      return prob_type == RPT_CERTAIN;
    }

    if (unit_owner(context->unit) == pov_player) {
      return TRUE;
    }

    /* Sent in package_short_unit() */
    return can_player_see_unit(pov_player, context->unit);
  }

  if (req->source.kind == VUT_DIPLREL
      || req->source.kind == VUT_DIPLREL_TILE
      || req->source.kind == VUT_DIPLREL_TILE_O
      || req->source.kind == VUT_DIPLREL_UNITANY
      || req->source.kind == VUT_DIPLREL_UNITANY_O) {
    switch (req->range) {
    case REQ_RANGE_LOCAL:
      if (other_player == NULL
          || context->player == NULL) {
        /* The two players may exist but not be passed when the problem
         * type is RPT_POSSIBLE. */
        return prob_type == RPT_CERTAIN;
      }

      if (pov_player == context->player
          || pov_player == other_player)  {
        return TRUE;
      }

      if (can_plr_see_all_sym_diplrels_of(pov_player, context->player)
          || can_plr_see_all_sym_diplrels_of(pov_player, other_player)) {
        return TRUE;
      }

      /* TODO: Non symmetric diplomatic relationships. */
      break;
    case REQ_RANGE_PLAYER:
      if (context->player == NULL) {
        /* The target player may exist but not be passed when the problem
         * type is RPT_POSSIBLE. */
        return prob_type == RPT_CERTAIN;
      }

      if (pov_player == context->player) {
        return TRUE;
      }

      if (can_plr_see_all_sym_diplrels_of(pov_player, context->player)) {
        return TRUE;
      }

      /* TODO: Non symmetric diplomatic relationships. */
      break;
    case REQ_RANGE_TEAM:
      /* TODO */
      break;
    case REQ_RANGE_ALLIANCE:
      /* TODO */
      break;
    case REQ_RANGE_WORLD:
      /* TODO */
      break;
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADEROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_COUNT:
      /* Invalid range */
      return FALSE;
      break;
    }
  }

  if (req->source.kind == VUT_MINSIZE) {
    if (context->city == NULL) {
      /* The city may exist but not be passed when the problem type is
       * RPT_POSSIBLE. */
      return prob_type == RPT_CERTAIN;
    }

    if (player_can_see_city_externals(pov_player, context->city)) {
      return TRUE;
    }
  }

  if (req->source.kind == VUT_CITYTILE) {
    struct city *pcity;

    if (context->tile == NULL) {
      /* The tile may exist but not be passed when the problem type is
       * RPT_POSSIBLE. */
      return prob_type == RPT_CERTAIN;
    }

    switch (req->range) {
    case REQ_RANGE_LOCAL:
      /* Known because the tile is seen */
      if (tile_is_seen(context->tile, pov_player)) {
        return TRUE;
      }

      /* The player knows their city even if they can't see it */
      pcity = tile_city(context->tile);
      return pcity && city_owner(pcity) == pov_player;
    case REQ_RANGE_CADJACENT:
      /* Known because the tile is seen */
      if (is_tile_seen_cadj(pov_player, context->tile)) {
        return TRUE;
      }

      /* The player knows their city even if they can't see it */
      cardinal_adjc_iterate(&(wld.map), context->tile, ptile) {
        pcity = tile_city(ptile);
        if (pcity && city_owner(pcity) == pov_player) {
          return TRUE;
        }
      } cardinal_adjc_iterate_end;

      /* Unknown */
      return FALSE;
    case REQ_RANGE_ADJACENT:
      /* Known because the tile is seen */
      if (is_tile_seen_adj(pov_player, context->tile)) {
        return TRUE;
      }

      /* The player knows their city even if they can't see it */
      adjc_iterate(&(wld.map), context->tile, ptile) {
        pcity = tile_city(ptile);
        if (pcity && city_owner(pcity) == pov_player) {
          return TRUE;
        }
      } adjc_iterate_end;

      /* Unknown */
      return FALSE;
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADEROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_COUNT:
      /* Invalid range */
      return FALSE;
    }
  }

  if (req->source.kind == VUT_IMPR_GENUS) {
    /* The only legal range when this was written was local. */
    fc_assert(req->range == REQ_RANGE_LOCAL);

    if (context->city == NULL) {
      /* RPT_CERTAIN: Can't be. No city to contain it.
       * RPT_POSSIBLE: A city like that may exist but not be passed. */
      return prob_type == RPT_CERTAIN;
    }

    /* Local BuildingGenus could be about city production. */
    return can_player_see_city_internals(pov_player, context->city);
  }

  if (req->source.kind == VUT_IMPROVEMENT) {
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
      /* Could be known for trade routes to cities owned by pov_player as
       * long as the requirement is present. Not present requirements would
       * require knowledge that no trade routes to another foreign city
       * exists (since all possible trade routes are to a city owned by
       * pov_player). Not worth the complexity, IMHO. */
      return FALSE;
    case REQ_RANGE_CITY:
    case REQ_RANGE_LOCAL:
      if (context->city == NULL) {
        /* RPT_CERTAIN: Can't be. No city to contain it.
         * RPT_POSSIBLE: A city like that may exist but not be passed. */
        return prob_type == RPT_CERTAIN;
      }

      if (can_player_see_city_internals(pov_player, context->city)) {
        /* Anyone that can see city internals (like the owner) known all
         * its improvements. */
        return TRUE;
      }

      if (is_improvement_visible(req->source.value.building)
          && player_can_see_city_externals(pov_player, context->city)) {
        /* Can see visible improvements when the outside of the city is
         * seen. */
        return TRUE;
      }

      /* No way to know if a city has an improvement */
      return FALSE;
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_COUNT:
      /* Not supported by the requirement type. */
      return FALSE;
    }
  }

  if (req->source.kind == VUT_NATION
      || req->source.kind == VUT_NATIONGROUP) {
    if (context->player == NULL
        && (req->range == REQ_RANGE_PLAYER
            || req->range == REQ_RANGE_TEAM
            || req->range == REQ_RANGE_ALLIANCE)) {
      /* The player (that can have a nationality or be alllied to someone
       * with the nationality) may exist but not be passed when the problem
       * type is RPT_POSSIBLE. */
      return prob_type == RPT_CERTAIN;
    }

    return TRUE;
  }

  if (req->source.kind == VUT_ADVANCE || req->source.kind == VUT_TECHFLAG) {
    if (req->range == REQ_RANGE_PLAYER) {
      if (context->player == NULL) {
        /* The player (that may or may not possess the tech) may exist but
         * not be passed when the problem type is RPT_POSSIBLE. */
        return prob_type == RPT_CERTAIN;
      }

      return can_see_techs_of_target(pov_player, context->player);
    } else if (req->range == REQ_RANGE_WORLD && req->survives) {
      /* game.info.global_advances is sent to each player */
      return TRUE;
    }
  }

  if (req->source.kind == VUT_GOVERNMENT) {
    if (req->range == REQ_RANGE_PLAYER) {
      if (context->player == NULL) {
        /* The player (that may or may not possess the tech) may exist but
         * not be passed when the problem type is RPT_POSSIBLE. */
        return prob_type == RPT_CERTAIN;
      }

      return (pov_player == context->player
              || could_intel_with_player(pov_player, context->player));
    }
  }

  if (req->source.kind == VUT_MAXTILEUNITS) {
    if (context->tile == NULL) {
      /* The tile may exist but not be passed when the problem type is
       * RPT_POSSIBLE. */
      return prob_type == RPT_CERTAIN;
    }

    switch (req->range) {
    case REQ_RANGE_LOCAL:
      return can_player_see_hypotetic_units_at(pov_player, context->tile);
    case REQ_RANGE_CADJACENT:
      if (!can_player_see_hypotetic_units_at(pov_player, context->tile)) {
        return FALSE;
      }
      cardinal_adjc_iterate(&(wld.map), context->tile, adjc_tile) {
        if (!can_player_see_hypotetic_units_at(pov_player, adjc_tile)) {
          return FALSE;
        }
      } cardinal_adjc_iterate_end;

      return TRUE;
    case REQ_RANGE_ADJACENT:
      if (!can_player_see_hypotetic_units_at(pov_player, context->tile)) {
        return FALSE;
      }
      adjc_iterate(&(wld.map), context->tile, adjc_tile) {
        if (!can_player_see_hypotetic_units_at(pov_player, adjc_tile)) {
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

  if (req->source.kind == VUT_TERRAIN
      || req->source.kind == VUT_TERRFLAG
      || req->source.kind == VUT_TERRAINCLASS
      || req->source.kind == VUT_TERRAINALTER
      || req->source.kind == VUT_EXTRA
      || req->source.kind == VUT_EXTRAFLAG
      || req->source.kind == VUT_ROADFLAG) {
    if (context->tile == NULL) {
      /* The tile may exist but not be passed when the problem type is
       * RPT_POSSIBLE. */
      return prob_type == RPT_CERTAIN;
    }

    switch (req->range) {
    case REQ_RANGE_LOCAL:
      return tile_is_seen(context->tile, pov_player);
    case REQ_RANGE_CADJACENT:
      /* TODO: The answer is known when the universal is located on a seen
       * tile. Is returning TRUE in those cases worth the added complexity
       * and the extra work for the computer? */
      return is_tile_seen_cadj(pov_player, context->tile);
    case REQ_RANGE_ADJACENT:
      /* TODO: The answer is known when the universal is located on a seen
       * tile. Is returning TRUE in those cases worth the added complexity
       * and the extra work for the computer? */
      return is_tile_seen_adj(pov_player, context->tile);
    case REQ_RANGE_CITY:
      /* TODO: The answer is known when the universal is located on a seen
       * tile. Is returning TRUE in those cases worth the added complexity
       * and the extra work for the computer? */
      return is_tile_seen_city(pov_player, context->city);
    case REQ_RANGE_TRADEROUTE:
      /* TODO: The answer is known when the universal is located on a seen
       * tile. Is returning TRUE in those cases worth the added complexity
       * and the extra work for the computer? */
      return is_tile_seen_traderoute(pov_player, context->city);
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_COUNT:
      /* Non existing range for requirement types. */
      return FALSE;
    }
  }

  if (req->source.kind == VUT_ACTION
      || req->source.kind == VUT_OTYPE) {
    /* This requirement type is intended to specify the situation. */
    return TRUE;
  }

  if (req->source.kind == VUT_SERVERSETTING) {
    /* Only visible server settings can be requirements. */
    return TRUE;
  }

  /* Uncertain or no support added yet. */
  return FALSE;
}

/**********************************************************************//**
  Evaluate a single requirement given pov_player's knowledge.

  context may be NULL. This is equivalent to passing an empty context.

  Note: Assumed to use pov_player's data.
**************************************************************************/
enum fc_tristate
mke_eval_req(const struct player *pov_player,
             const struct req_context *context,
             const struct player *other_player,
             const struct requirement *req,
             const enum   req_problem_type prob_type)
{
  if (!is_req_knowable(pov_player, context, other_player,
                       req, prob_type)) {
    return TRI_MAYBE;
  }

  if (is_req_active(context, other_player, req, prob_type)) {
    return TRI_YES;
  } else {
    return TRI_NO;
  }
}

/**********************************************************************//**
  Evaluate a requirement vector given pov_player's knowledge.

  context may be NULL. This is equivalent to passing an empty context.

  Note: Assumed to use pov_player's data.
**************************************************************************/
enum fc_tristate
mke_eval_reqs(const struct player *pov_player,
              const struct req_context *context,
              const struct player *other_player,
              const struct requirement_vector *reqs,
              const enum   req_problem_type prob_type)
{
  enum fc_tristate current;
  enum fc_tristate result;

  result = TRI_YES;
  requirement_vector_iterate(reqs, preq) {
    current = mke_eval_req(pov_player, context, other_player,
                           preq, prob_type);
    if (current == TRI_NO) {
      return TRI_NO;
    } else if (current == TRI_MAYBE) {
      result = TRI_MAYBE;
    }
  } requirement_vector_iterate_end;

  return result;
}

/**********************************************************************//**
  Can pow_player see the techs of target player?
**************************************************************************/
bool can_see_techs_of_target(const struct player *pow_player,
                             const struct player *target_player)
{
  return pow_player == target_player
      || team_has_embassy(pow_player->team, target_player);
}
