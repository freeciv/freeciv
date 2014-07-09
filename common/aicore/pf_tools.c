/********************************************************************** 
 Freeciv - Copyright (C) 2003 - The Freeciv Project
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

#include <string.h>

/* utility */
#include "bitvector.h"
#include "log.h"
#include "mem.h"

/* common */
#include "base.h"
#include "combat.h"
#include "game.h"
#include "movement.h"
#include "tile.h"
#include "unit.h"
#include "unittype.h"

#include "pf_tools.h"

/* ===================== Cost of Specific Moves ====================== */

/****************************************************************************
  Cost of moving one normal step.
****************************************************************************/
static inline int single_move_cost(const struct pf_parameter *param,
                                   const struct tile *src_tile,
                                   const struct tile *dest_tile)
{
  return map_move_cost(param->owner, param->uclass, src_tile, dest_tile,
                       BV_ISSET(param->unit_flags, UTYF_IGTER));
}

/****************************************************************************
  Cost of an overlap move (move to a non-native tile).
  This should always be the last tile reached.
****************************************************************************/
static inline int overlap_move_cost(const struct pf_parameter *param)
{
  return param->move_rate;
}


/* ===================== Capability Functions ======================== */

/****************************************************************************
  Determines if a path to 'ptile' would be considered as action rather than
  normal move: attack, diplomat action, caravan action.

  FIXME: For diplomat actions, we should take in account action enablers.
****************************************************************************/
static enum pf_action pf_get_action(const struct tile *ptile,
                                    const struct pf_parameter *param)
{
  bool non_allied_city = (NULL != is_non_allied_city_tile(ptile,
                                                          param->owner));

  if (non_allied_city) {
    if (PF_AA_TRADE_ROUTE & param->actions) {
      return PF_ACTION_TRADE_ROUTE;
    }

    if (PF_AA_DIPLOMAT & param->actions) {
      return PF_ACTION_DIPLOMAT;
    }
  }

  if (is_non_allied_unit_tile(ptile, param->owner)) {
    if (PF_AA_DIPLOMAT & param->actions) {
      return PF_ACTION_DIPLOMAT;
    }

    if (PF_AA_UNIT_ATTACK & param->actions
        && ((uclass_has_flag(param->uclass, UCF_ATTACK_NON_NATIVE)
             && !BV_ISSET(param->unit_flags, UTYF_ONLY_NATIVE_ATTACK))
            || is_native_tile_to_class(param->uclass, ptile))
        && (!param->omniscience
            || can_player_attack_tile(param->owner, ptile))) {
      /* FIXME: we should also test for unit reachability. */
      return PF_ACTION_ATTACK;
    }
  }

  if (non_allied_city
      && PF_AA_CITY_ATTACK & param->actions
      && ((uclass_has_flag(param->uclass, UCF_ATTACK_NON_NATIVE)
           && !BV_ISSET(param->unit_flags, UTYF_ONLY_NATIVE_ATTACK))
          || is_native_tile_to_class(param->uclass, ptile))) {
    /* Consider that there are units, even if is_non_allied_unit_tile()
     * returned NULL (usally when '!param->omniscience'). */
    return PF_ACTION_ATTACK;
  }

  return PF_ACTION_NONE;
}

/****************************************************************************
  Determines if an action is possible from 'src' to 'dst': attack, diplomat
  action, or caravan action.
****************************************************************************/
static bool pf_action_possible(const struct tile *src,
                               enum pf_move_scope src_scope,
                               const struct tile *dst,
                               enum pf_action action,
                               const struct pf_parameter *param)
{
  if (PF_ACTION_ATTACK == action) {
    return (uclass_has_flag(param->uclass, UCF_ATT_FROM_NON_NATIVE)
            || BV_ISSET(param->unit_flags, UTYF_MARINES)
            || PF_MS_NATIVE & src_scope);
  } else if (PF_ACTION_DIPLOMAT == action) {
    return (PF_MS_NATIVE | PF_MS_CITY) & src_scope;
  }
  return TRUE;
}

/****************************************************************************
  Determine how it is possible to move from/to 'ptile'. The checks for
  specific move from tile to tile is done in pf_move_possible().
****************************************************************************/
static enum pf_move_scope
pf_get_move_scope(const struct tile *ptile,
                  enum pf_move_scope previous_scope,
                  const struct pf_parameter *param)
{
  enum pf_move_scope scope = PF_MS_NONE;
  struct city *pcity = tile_city(ptile);

  if ((is_native_tile_to_class(param->uclass, ptile)
       && (!BV_ISSET(param->unit_flags, UTYF_TRIREME)
           || is_safe_ocean(ptile)))) {
    scope |= PF_MS_NATIVE;
  }

  if (NULL != pcity
      && (uclass_has_flag(param->uclass, UCF_CAN_OCCUPY_CITY)
          || pplayers_allied(param->owner, city_owner(pcity)))
      && ((previous_scope & PF_MS_CITY) /* City channel previously tested */
          || uclass_has_flag(param->uclass, UCF_BUILD_ANYWHERE)
          || is_native_near_tile(param->uclass, ptile)
          || (1 == game.info.citymindist
              && is_city_channel_tile(param->uclass, ptile, NULL)))) {
    scope |= PF_MS_CITY;
  }

  if (PF_MS_NONE == scope) {
    /* Check for transporters. Useless if we already got another way to
     * move. */
    unit_list_iterate(ptile->units, punit) {
      if (pplayers_allied(unit_owner(punit), param->owner)
          && !unit_has_orders(punit)
          && can_unit_type_transport(unit_type(punit), param->uclass)) {
        scope |= PF_MS_TRANSPORT;
        break;
      }
    } unit_list_iterate_end;
  }

  return scope;
}

/****************************************************************************
  A cost function for amphibious movement.
****************************************************************************/
static enum pf_move_scope
amphibious_move_scope(const struct tile *ptile,
                      enum pf_move_scope previous_scope,
                      const struct pf_parameter *param)
{
  struct pft_amphibious *amphibious = param->data;
  enum pf_move_scope scope;

  scope = pf_get_move_scope(ptile, previous_scope, &amphibious->land);

  if (!(PF_MS_TRANSPORT & scope)
      && ((PF_MS_NATIVE | PF_MS_CITY)
          & pf_get_move_scope(ptile, scope, &amphibious->sea))) {
    scope |= PF_MS_TRANSPORT;
  }
  return scope;
}

/****************************************************************************
  Determines if the move between two tiles is possible.
  Do not use this function as part of a test of whether a unit may attack a
  tile: many tiles that pass this test may be unsuitable for some units to
  attack to/from.

  Does not check if the tile is occupied by non-allied units.
****************************************************************************/
static inline bool pf_move_possible(const struct tile *src,
                                    enum pf_move_scope src_scope,
                                    const struct tile *dst,
                                    enum pf_move_scope dst_scope,
                                    const struct pf_parameter *param)
{
  fc_assert(PF_MS_NONE != src_scope);

  if (PF_MS_NONE == dst_scope) {
    return FALSE;
  }

  if (PF_MS_NATIVE == dst_scope
      && (PF_MS_NATIVE & src_scope)
      && !is_native_move(param->uclass, src, dst)) {
    return FALSE;
  }

  return TRUE;
}


/* ===================== Move Cost Callbacks ========================= */

/****************************************************************************
  A cost function for regular movement. Permits attacks.
  Use with a TB callback to prevent passing through occupied tiles.
  Does not permit passing through non-native tiles without transport.
****************************************************************************/
static int normal_move(const struct tile *src,
                       enum pf_move_scope src_scope,
                       const struct tile *dst,
                       enum pf_move_scope dst_scope,
                       const struct pf_parameter *param)
{
  if (pf_move_possible(src, src_scope, dst, dst_scope, param)) {
    return single_move_cost(param, src, dst);
  }
  return PF_IMPOSSIBLE_MC;
}

/****************************************************************************
  A cost function for overlap movement. Do not consider enemy units and
  attacks.
  Permits moves one step into non-native terrain (for ferries, etc.)
  Use with a TB callback to prevent passing through occupied tiles.
  Does not permit passing through non-native tiles without transport.
****************************************************************************/
static int overlap_move(const struct tile *src,
                        enum pf_move_scope src_scope,
                        const struct tile *dst,
                        enum pf_move_scope dst_scope,
                        const struct pf_parameter *param)
{
  if (pf_move_possible(src, src_scope, dst, dst_scope, param)) {
    return single_move_cost(param, src, dst);
  } else if (!(PF_MS_NATIVE & dst_scope)) {
    return overlap_move_cost(param);
  }
  return PF_IMPOSSIBLE_MC;
}

/****************************************************************************
  A cost function for amphibious movement.
****************************************************************************/
static int amphibious_move(const struct tile *ptile,
                           enum pf_move_scope src_scope,
                           const struct tile *ptile1,
                           enum pf_move_scope dst_scope,
                           const struct pf_parameter *param)
{
  struct pft_amphibious *amphibious = param->data;
  int cost, scale;

  if (PF_MS_TRANSPORT & src_scope) {
    if (PF_MS_TRANSPORT & dst_scope) {
      /* Sea move, moving from native terrain to a city, or leaving port. */
      cost = amphibious->sea.get_MC(ptile,
                                    (PF_MS_CITY & src_scope) | PF_MS_NATIVE,
                                    ptile1,
                                    (PF_MS_CITY & dst_scope) | PF_MS_NATIVE,
                                    &amphibious->sea);
      scale = amphibious->sea_scale;
    } else if (PF_MS_NATIVE & dst_scope) {
      /* Disembark; use land movement function to handle UTYF_MARINES */
      cost = amphibious->land.get_MC(ptile, PF_MS_TRANSPORT, ptile1,
                                     PF_MS_NATIVE, &amphibious->land);
      scale = amphibious->land_scale;
    } else {
      /* Neither ferry nor passenger can enter tile. */
      return PF_IMPOSSIBLE_MC;
    }
  } else if ((PF_MS_NATIVE | PF_MS_CITY) & dst_scope) {
    /* Land move */
    cost = amphibious->land.get_MC(ptile, PF_MS_NATIVE, ptile1,
                                   PF_MS_NATIVE, &amphibious->land);
    scale = amphibious->land_scale;
  } else {
    /* Now we have disembarked, our ferry can not help us - we have to
     * stay on the land. */
    return PF_IMPOSSIBLE_MC;
  }
  if (cost != PF_IMPOSSIBLE_MC && cost < FC_INFINITY) {
    cost *= scale;
  }
  return cost;
}

/* ===================== Extra Cost Callbacks ======================== */

/****************************************************************************
  Extra cost call back for amphibious movement
****************************************************************************/
static int amphibious_extra_cost(const struct tile *ptile,
                                 enum known_type known,
                                 const struct pf_parameter *param)
{
  struct pft_amphibious *amphibious = param->data;
  const bool ferry_move = is_native_tile_to_class(amphibious->sea.uclass, ptile);
  int cost, scale;

  if (known == TILE_UNKNOWN) {
    /* We can travel almost anywhere */
    cost = SINGLE_MOVE;
    scale = MAX(amphibious->sea_scale, amphibious->land_scale);
  } else if (ferry_move && amphibious->sea.get_EC) {
    /* Do the EC callback for sea moves. */
    cost = amphibious->sea.get_EC(ptile, known, &amphibious->sea);
    scale = amphibious->sea_scale;
  } else if (!ferry_move && amphibious->land.get_EC) {
    /* Do the EC callback for land moves. */
    cost = amphibious->land.get_EC(ptile, known, &amphibious->land);
    scale = amphibious->land_scale;
  } else {
    cost = 0;
    scale = 1;
  }

  if (cost != PF_IMPOSSIBLE_MC) {
    cost *= scale;
  }
  return cost;
}


/* ===================== Tile Behaviour Callbacks ==================== */

/********************************************************************** 
  PF callback to prohibit going into the unknown.  Also makes sure we 
  don't plan to attack anyone.
***********************************************************************/
enum tile_behavior no_fights_or_unknown(const struct tile *ptile,
                                        enum known_type known,
                                        const struct pf_parameter *param)
{
  if (known == TILE_UNKNOWN
      || is_non_allied_unit_tile(ptile, param->owner)
      || is_non_allied_city_tile(ptile, param->owner)) {
    /* Can't attack */
    return TB_IGNORE;
  }
  return TB_NORMAL;
}

/********************************************************************** 
  PF callback to prohibit attacking anyone.
***********************************************************************/
enum tile_behavior no_fights(const struct tile *ptile,
                             enum known_type known,
                             const struct pf_parameter *param)
{
  if (is_non_allied_unit_tile(ptile, param->owner)
      || is_non_allied_city_tile(ptile, param->owner)) {
    /* Can't attack */
    return TB_IGNORE;
  }
  return TB_NORMAL;
}

/****************************************************************************
  PF callback to prohibit attacking anyone, except at the destination.
****************************************************************************/
enum tile_behavior no_intermediate_fights(const struct tile *ptile,
                                          enum known_type known,
                                          const struct pf_parameter *param)
{
  if (is_non_allied_unit_tile(ptile, param->owner)
      || is_non_allied_city_tile(ptile, param->owner)) {
    return TB_DONT_LEAVE;
  }
  return TB_NORMAL;
}

/*********************************************************************
  A callback for amphibious movement
*********************************************************************/
static enum tile_behavior
amphibious_behaviour(const struct tile *ptile, enum known_type known,
                     const struct pf_parameter *param)
{
  struct pft_amphibious *amphibious = param->data;
  const bool ferry_move = is_native_tile_to_class(amphibious->sea.uclass, ptile);

  /* Simply a wrapper for the sea or land tile_behavior callbacks. */
  if (ferry_move && amphibious->sea.get_TB) {
    return amphibious->sea.get_TB(ptile, known, &amphibious->sea);
  } else if (!ferry_move && amphibious->land.get_TB) {
    return amphibious->land.get_TB(ptile, known, &amphibious->land);
  }
  return TB_NORMAL;
}

/* ===================== Required Moves Lefts Callbacks ================= */

/****************************************************************************
  Refueling base for air units.
****************************************************************************/
static bool is_possible_base_fuel(const struct tile *ptile,
                                  const struct pf_parameter *param)
{
  enum known_type tile_known;
  tile_known = (param->omniscience ? TILE_KNOWN_SEEN
                : tile_get_known(ptile, param->owner));

  if (tile_known == TILE_UNKNOWN) {
    /* Cannot guess if it is */
    return FALSE;
  }

  if (is_allied_city_tile(ptile, param->owner)) {
    return TRUE;
  }

  if (param->uclass->cache.refuel_bases != NULL) {
    extra_type_list_iterate(param->uclass->cache.refuel_bases, pextra) {
      /* All airbases are considered possible, simply attack enemies. */
      if (tile_has_extra(ptile, pextra)) {
        return TRUE;
      }
    } extra_type_list_iterate_end;
  }

  if (tile_known == TILE_KNOWN_UNSEEN) {
    /* Cannot see units */
    return FALSE;
  }

  /* Check for carriers */
  unit_list_iterate(ptile->units, ptrans) {
    if (can_unit_type_transport(unit_type(ptrans), param->uclass)
        && !unit_has_orders(ptrans)
        && (get_transporter_occupancy(ptrans)
            < get_transporter_capacity(ptrans))) {
      return TRUE;
    }
  } unit_list_iterate_end;

  return FALSE;
}

/****************************************************************************
  Check if there is a safe position to move.
****************************************************************************/
static int get_closest_safe_tile_distance(const struct tile *src_tile,
                                          const struct pf_parameter *param,
                                          int max_distance)
{
  /* This iteration should, according to the documentation in map.h iterate
   * tiles from the center tile, so we stop the iteration to the first found
   * refuel point (as it should be the closest). */
  iterate_outward_dxy(src_tile, max_distance, ptile, x, y) {
    if (tile_get_known(ptile, param->owner) == TILE_UNKNOWN) {
      /* Cannot guess if the tile is safe */
      continue;
    }
    if (is_possible_base_fuel(ptile, param)) {
      return map_vector_to_real_distance(x, y);
    }
  } iterate_outward_dxy_end;

  return -1;
}

/****************************************************************************
  Position-dangerous callback for air units.
****************************************************************************/
static int get_fuel_moves_left_req(const struct tile *ptile,
                                   enum known_type known,
                                   const struct pf_parameter *param)
{
  int dist, max;

  if (is_possible_base_fuel(ptile, param)) {
    return 0;
  }

  /* Upper bound for search for refuel point. Sometimes unit can have more
   * moves left than its own move rate due to wonder transfer. Compare
   * pf_moves_left_initially(). */
  max = MAX(param->moves_left_initially
            + (param->fuel_left_initially - 1) * param->move_rate,
            param->move_rate * param->fuel);
  dist = get_closest_safe_tile_distance(ptile, param, max / SINGLE_MOVE);

  return dist != -1 ? dist * SINGLE_MOVE : PF_IMPOSSIBLE_MC;
}

/* ====================  Postion Dangerous Callbacks =================== */

/****************************************************************************
  Position-dangerous callback for amphibious movement.
****************************************************************************/
static bool amphibious_is_pos_dangerous(const struct tile *ptile,
                                        enum known_type known,
                                        const struct pf_parameter *param)
{
  struct pft_amphibious *amphibious = param->data;
  const bool ferry_move = is_native_tile_to_class(amphibious->sea.uclass, ptile);

  /* Simply a wrapper for the sea or land danger callbacks. */
  if (ferry_move && amphibious->sea.is_pos_dangerous) {
    return amphibious->sea.is_pos_dangerous(ptile, known, param);
  } else if (!ferry_move && amphibious->land.is_pos_dangerous) {
    return amphibious->land.is_pos_dangerous(ptile, known, param);
  }
  return FALSE;
}

/* =======================  Tools for filling parameters ================= */

/****************************************************************************
  Fill general use parameters to defaults.
****************************************************************************/
static inline void
pft_fill_default_parameter(struct pf_parameter *parameter,
                           const struct unit_type *punittype)
{
  struct unit_class *punitclass = utype_class(punittype);

  parameter->unknown_MC = punittype->unknown_move_cost;
  parameter->get_TB = NULL;
  parameter->get_EC = NULL;
  parameter->is_pos_dangerous = NULL;
  parameter->get_moves_left_req = NULL;
  parameter->get_costs = NULL;
  parameter->get_zoc = NULL;
  parameter->get_move_scope = pf_get_move_scope;
  parameter->get_action = NULL;
  parameter->is_action_possible = NULL;
  parameter->actions = PF_AA_NONE;

  parameter->uclass = punitclass;
  parameter->unit_flags = punittype->flags;
}

/****************************************************************************
  Enable default actions.
****************************************************************************/
static inline void
pft_enable_default_actions(struct pf_parameter *parameter)
{
  if (!BV_ISSET(parameter->unit_flags, UTYF_CIVILIAN)) {
    parameter->actions |= PF_AA_UNIT_ATTACK;
    parameter->get_action = pf_get_action;
    parameter->is_action_possible = pf_action_possible;
    if (!parameter->omniscience) {
      /* Consider units hided in cities. */
      parameter->actions |= PF_AA_CITY_ATTACK;
    }
  }
  if (BV_ISSET(parameter->unit_flags, UTYF_DIPLOMAT)) {
    /* FIXME: it should consider action enablers. */
    parameter->actions |= PF_AA_DIPLOMAT;
    parameter->get_action = pf_get_action;
    parameter->is_action_possible = pf_action_possible;
  }
  if (BV_ISSET(parameter->unit_flags, UTYF_TRADE_ROUTE)) {
    parameter->actions |= PF_AA_TRADE_ROUTE;
    parameter->get_action = pf_get_action;
  }
}

/****************************************************************************
  Fill general use parameters to defaults for an unit type.
****************************************************************************/
static inline void
pft_fill_utype_default_parameter(struct pf_parameter *parameter,
                                 const struct unit_type *punittype,
                                 struct tile *pstart_tile,
                                 struct player *powner)
{
  pft_fill_default_parameter(parameter, punittype);

  parameter->start_tile = pstart_tile;
  parameter->moves_left_initially = punittype->move_rate;
  parameter->move_rate = punittype->move_rate;
  if (utype_fuel(punittype)) {
    parameter->fuel_left_initially = utype_fuel(punittype);
    parameter->fuel = utype_fuel(punittype);
  } else {
    parameter->fuel = 1;
    parameter->fuel_left_initially = 1;
  }
  parameter->transported_initially = FALSE;
  parameter->owner = powner;

  parameter->omniscience = FALSE;
}

/****************************************************************************
  Fill general use parameters to defaults for an unit.
****************************************************************************/
static inline void
pft_fill_unit_default_parameter(struct pf_parameter *parameter,
                                const struct unit *punit)
{
  pft_fill_default_parameter(parameter, unit_type(punit));

  parameter->start_tile = unit_tile(punit);
  parameter->moves_left_initially = punit->moves_left;
  parameter->move_rate = unit_move_rate(punit);
  if (utype_fuel(unit_type(punit))) {
    parameter->fuel_left_initially = punit->fuel;
    parameter->fuel = utype_fuel(unit_type(punit));
  } else {
    parameter->fuel = 1;
    parameter->fuel_left_initially = 1;
  }
  parameter->transported_initially = unit_transported(punit);
  parameter->owner = unit_owner(punit);

  parameter->omniscience = FALSE;
}

/****************************************************************************
  Base function to fill classic parameters.
****************************************************************************/
static inline void pft_fill_parameter(struct pf_parameter *parameter,
                                      const struct unit_type *punittype)
{
  parameter->get_MC = normal_move;
  parameter->ignore_none_scopes = TRUE;
  pft_enable_default_actions(parameter);

  if (!parameter->get_moves_left_req && utype_fuel(punittype)) {
    /* Unit needs fuel */
    parameter->get_moves_left_req = get_fuel_moves_left_req;
  }

  if (!unit_type_really_ignores_zoc(punittype)) {
    parameter->get_zoc = is_my_zoc;
  } else {
    parameter->get_zoc = NULL;
  }
}

/**********************************************************************
  Fill classic parameters for an unit type.
***********************************************************************/
void pft_fill_utype_parameter(struct pf_parameter *parameter,
                              const struct unit_type *punittype,
                              struct tile *pstart_tile,
                              struct player *pplayer)
{
  pft_fill_utype_default_parameter(parameter, punittype,
                                   pstart_tile, pplayer);
  pft_fill_parameter(parameter, punittype);
}

/**********************************************************************
  Fill classic parameters for an unit.
***********************************************************************/
void pft_fill_unit_parameter(struct pf_parameter *parameter,
			     const struct unit *punit)
{
  pft_fill_unit_default_parameter(parameter, punit);
  pft_fill_parameter(parameter, unit_type(punit));
}

/**********************************************************************
  pft_fill_*_overlap_param() base function.

  Switch on one tile overlapping into the non-native terrain.
  For sea/land bombardment and for ferries.
**********************************************************************/
static void pft_fill_overlap_param(struct pf_parameter *parameter,
                                   const struct unit_type *punittype)
{
  parameter->get_MC = overlap_move;
  parameter->ignore_none_scopes = FALSE;

  if (!unit_type_really_ignores_zoc(punittype)) {
    parameter->get_zoc = is_my_zoc;
  } else {
    parameter->get_zoc = NULL;
  }
}

/**********************************************************************
  Switch on one tile overlapping into the non-native terrain.
  For sea/land bombardment and for ferry types.
**********************************************************************/
void pft_fill_utype_overlap_param(struct pf_parameter *parameter,
                                  const struct unit_type *punittype,
                                  struct tile *pstart_tile,
                                  struct player *pplayer)
{
  pft_fill_utype_default_parameter(parameter, punittype,
                                   pstart_tile, pplayer);
  pft_fill_overlap_param(parameter, punittype);
}


/**********************************************************************
  Switch on one tile overlapping into the non-native terrain.
  For sea/land bombardment and for ferries.
**********************************************************************/
void pft_fill_unit_overlap_param(struct pf_parameter *parameter,
				 const struct unit *punit)
{
  pft_fill_unit_default_parameter(parameter, punit);
  pft_fill_overlap_param(parameter, unit_type(punit));
}

/**********************************************************************
  pft_fill_*_attack_param() base function.

  Consider attacking and non-attacking possibilities properly.
**********************************************************************/
static void pft_fill_attack_param(struct pf_parameter *parameter,
                                  const struct unit_type *punittype)
{
  parameter->get_MC = normal_move;
  parameter->ignore_none_scopes = TRUE;
  pft_enable_default_actions(parameter);
  /* We want known units! */
  parameter->actions &= ~PF_AA_CITY_ATTACK;

  if (!unit_type_really_ignores_zoc(punittype)) {
    parameter->get_zoc = is_my_zoc;
  } else {
    parameter->get_zoc = NULL;
  }

  /* It is too complicated to work with danger here */
  parameter->is_pos_dangerous = NULL;
  parameter->get_moves_left_req = NULL;
}

/**********************************************************************
  pft_fill_*_attack_param() base function.

  Consider attacking and non-attacking possibilities properly.
**********************************************************************/
void pft_fill_utype_attack_param(struct pf_parameter *parameter,
                                 const struct unit_type *punittype,
                                 struct tile *pstart_tile,
                                 struct player *pplayer)
{
  pft_fill_utype_default_parameter(parameter, punittype,
                                   pstart_tile, pplayer);
  pft_fill_attack_param(parameter, punittype);
}

/**********************************************************************
  pft_fill_*_attack_param() base function.

  Consider attacking and non-attacking possibilities properly.
**********************************************************************/
void pft_fill_unit_attack_param(struct pf_parameter *parameter,
                                const struct unit *punit)
{
  pft_fill_unit_default_parameter(parameter, punit);
  pft_fill_attack_param(parameter, unit_type(punit));
}

/****************************************************************************
  Fill parameters for combined sea-land movement.
  This is suitable for the case of a land unit riding a ferry.
  The starting position of the ferry is taken to be the starting position for
  the PF. The passenger is assumed to initailly be on the given ferry.
  The destination may be inland, in which case the passenger will ride
  the ferry to a beach head, disembark, then continue on land.
  One complexity of amphibious movement is that the movement rate on land
  might be different from that at sea. We therefore scale up the movement
  rates (and the corresponding movement consts) to the product of the two
  rates.
****************************************************************************/
void pft_fill_amphibious_parameter(struct pft_amphibious *parameter)
{
  const int move_rate = parameter->land.move_rate * parameter->sea.move_rate;

  parameter->combined = parameter->sea;
  parameter->land_scale = move_rate / parameter->land.move_rate;
  parameter->sea_scale = move_rate / parameter->sea.move_rate;
  parameter->combined.moves_left_initially *= parameter->sea_scale;
  parameter->combined.move_rate = move_rate;
  parameter->combined.get_MC = amphibious_move;
  parameter->combined.get_move_scope = amphibious_move_scope;
  parameter->combined.get_TB = amphibious_behaviour;
  parameter->combined.get_EC = amphibious_extra_cost;
  if (NULL != parameter->land.is_pos_dangerous
      || NULL != parameter->sea.is_pos_dangerous) {
    parameter->combined.is_pos_dangerous = amphibious_is_pos_dangerous;
  } else {
    parameter->combined.is_pos_dangerous = NULL;
  }
  parameter->combined.get_moves_left_req = NULL;
  parameter->combined.get_action = NULL;
  parameter->combined.is_action_possible = NULL;
  BV_CLR_ALL(parameter->combined.unit_flags);

  parameter->combined.data = parameter;
}

/**********************************************************************
  Concatenate two paths together.  The additional segment (src_path)
  should start where the initial segment (dest_path) stops.  The
  overlapping position is removed.

  If dest_path == NULL, we just copy the src_path and nothing else.
***********************************************************************/
struct pf_path *pft_concat(struct pf_path *dest_path,
			   const struct pf_path *src_path)
{
  if (!dest_path) {
    dest_path = fc_malloc(sizeof(*dest_path));
    dest_path->length = src_path->length;
    dest_path->positions =
	fc_malloc(sizeof(*dest_path->positions) * dest_path->length);
    memcpy(dest_path->positions, src_path->positions,
	   sizeof(*dest_path->positions) * dest_path->length);
  } else {
    int old_length = dest_path->length;

    fc_assert_ret_val(pf_path_last_position(dest_path)->tile
                      == src_path->positions[0].tile, NULL);
    fc_assert_ret_val(pf_path_last_position(dest_path)->moves_left
                      == src_path->positions[0].moves_left, NULL);
    dest_path->length += src_path->length - 1;
    dest_path->positions =
	fc_realloc(dest_path->positions,
		   sizeof(*dest_path->positions) * dest_path->length);
    /* Be careful to include the first position of src_path, it contains
     * the direction (it is undefined in the last position of dest_path) */
    memcpy(dest_path->positions + old_length - 1, src_path->positions,
	   src_path->length * sizeof(*dest_path->positions));
  }
  return dest_path;
}

/****************************************************************************
  Remove the part of a path leading up to a given tile.
  If given tile is on the path more than once
  then the first occurrance will be the one used.
  If tile is not on the path at all, returns FALSE and
  path is not changed at all.
****************************************************************************/
bool pft_advance_path(struct pf_path *path, struct tile *ptile)
{
  int i;
  struct pf_position *new_positions;

  for (i = 0; path->positions[i].tile != ptile ; i++) {
    if (i >= path->length) {
      return FALSE;
    }
  }
  fc_assert_ret_val(i < path->length, FALSE);
  path->length -= i;
  new_positions = fc_malloc(sizeof(*path->positions) * path->length);
  memcpy(new_positions, path->positions + i,
	 path->length * sizeof(*path->positions));
  free(path->positions);
  path->positions = new_positions;

  return TRUE;
}
