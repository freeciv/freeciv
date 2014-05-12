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

/****************************************************************************
  Cost of performing an attack.
  This ends the turn for some units.
****************************************************************************/
static inline int attack_move_cost(const struct pf_parameter *param)
{
  if (uclass_has_flag(param->uclass, UCF_MISSILE)
      || BV_ISSET(param->unit_flags, UTYF_ONEATTACK)) {
    /* Can't move any more this turn. */
    return param->move_rate;
  } else {
    return SINGLE_MOVE;
  }
}

/* ===================== Capability Functions ======================== */

enum pf_move_tile {
  PF_MT_NONE = 0,
  PF_MT_NATIVE = 1 << 0,
  PF_MT_CITY = 1 << 1,
  PF_MT_TRANSPORT = 1 << 2
};

/****************************************************************************
  Determines if a tile is an acceptable tile for attacking.
  Should include any potential rationale for a unit to actually attack the
  target tile from the source tile. Do not use this function as part of a
  test of whether a unit may move to/from a tile: many tiles that pass this
  test may be unsuitable for some units to move to/from.
****************************************************************************/
static inline bool pf_able_to_attack(const struct tile *target,
                                     const struct pf_parameter *param,
                                     const struct tile *position)
{
  if (is_non_allied_unit_tile(target, param->owner)
      && ((uclass_has_flag(param->uclass, UCF_ATTACK_NON_NATIVE)
           && !BV_ISSET(param->unit_flags, UTYF_ONLY_NATIVE_ATTACK))
          || is_native_tile_to_class(param->uclass, target))
      && (uclass_has_flag(param->uclass, UCF_ATT_FROM_NON_NATIVE)
          || BV_ISSET(param->unit_flags, UTYF_MARINES)
          || is_native_tile_to_class(param->uclass, position))) {

    /* FIXME: Should perform some checks from combat.c:
     * can_player_attack_tile(): Can we attack? Are we at war?
     * can_unit_attack_units_at_tile(): reachability
     * (more?)
     *
     * These functions would need refactoring to take class arguments
     * or reimplementation here, as the current form is unusable or
     * repetitive. They might also be called by the consumers, so
     * be duplicative here.
     */

    return TRUE;
  }

  return FALSE;
}

/****************************************************************************
  Determines if a tile is an acceptable tile for movement.
  Should include any potential rationale for a unit to actually move to/from
  the tile. Do not use this function as part of a test of whether a unit may
  attack a tile: many tiles that pass this test may be unsuitable for some
  units to attack to/from.

  Does not check if the tile is occupied by non-allied units.

  'src_tile_result' is set to PF_MT_NONE if we are checking the source tile.
****************************************************************************/
static inline enum pf_move_tile
pf_is_ok_move_tile(const struct pf_parameter *param,
                   const struct tile *ptile,
                   enum pf_move_tile src_tile_result)
{
  enum pf_move_tile result = PF_MT_NONE;
  struct city *pcity = tile_city(ptile);

  if ((is_native_tile_to_class(param->uclass, ptile)
       && (!BV_ISSET(param->unit_flags, UTYF_TRIREME)
           || is_safe_ocean(ptile)))) {
    result |= PF_MT_NATIVE;
  }

  if (NULL != pcity
      && (uclass_has_flag(param->uclass, UCF_CAN_OCCUPY_CITY)
          || pplayers_allied(param->owner, city_owner(pcity)))
      && ((src_tile_result & PF_MT_CITY) /* City channel previously tested */
          || uclass_has_flag(param->uclass, UCF_BUILD_ANYWHERE)
          || is_native_near_tile(param->uclass, ptile)
          || (1 == game.info.citymindist
              && is_city_channel_tile(param->uclass, ptile, NULL)))) {
    result |= PF_MT_CITY;
  }

  unit_list_iterate(ptile->units, punit) {
    if (pplayers_allied(unit_owner(punit), param->owner)
        && (PF_MT_NONE == src_tile_result || !unit_has_orders(punit))
        && can_unit_type_transport(unit_type(punit), param->uclass)) {
      result |= PF_MT_TRANSPORT;
      break;
    }
  } unit_list_iterate_end;

  return result;
}

/****************************************************************************
  Here goes move additional checks to determine if a move is possible or
  not.
****************************************************************************/
static inline bool pf_move_check(const struct pf_parameter *param,
                                 const struct tile *src,
                                 enum pf_move_tile src_tile_result,
                                 const struct tile *dst,
                                 enum pf_move_tile dst_tile_result)
{
  if (PF_MT_NATIVE == dst_tile_result
      && (PF_MT_NATIVE & src_tile_result)
      && !is_native_move(param->uclass, src, dst)) {
    return FALSE;
  }

  return TRUE;
}

/****************************************************************************
  Determines if the move between two tiles is possible.
  Do not use this function as part of a test of whether a unit may attack a
  tile: many tiles that pass this test may be unsuitable for some units to
  attack to/from.

  Does not check if the tile is occupied by non-allied units.
****************************************************************************/
static inline bool pf_able_to_move(const struct pf_parameter *param,
                                   const struct tile *src,
                                   const struct tile *dst)
{
  enum pf_move_tile src_tile_result, dst_tile_result;

  return ((src_tile_result = pf_is_ok_move_tile(param, src, PF_MT_NONE))
          && (dst_tile_result = pf_is_ok_move_tile(param, dst,
                                                   src_tile_result))
          && pf_move_check(param, src, src_tile_result,
                           dst, dst_tile_result));
}

/* ===================== Move Cost Callbacks ========================= */

/****************************************************************************
  A cost function for regular movement. Permits attacks.
  Use with a TB callback to prevent passing through occupied tiles.
  Does not permit passing through non-native tiles without transport.
****************************************************************************/
static int normal_move(const struct tile *src,
                       enum direction8 dir,
                       const struct tile *dst,
                       const struct pf_parameter *param)
{
  if (pf_able_to_attack(dst, param, src)) {
    return attack_move_cost(param);
  } else if (pf_able_to_move(param, src, dst)) {
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
                        enum direction8 dir,
                        const struct tile *dst,
                        const struct pf_parameter *param)
{
  enum pf_move_tile src_tile_result, dst_tile_result;

  if ((src_tile_result = pf_is_ok_move_tile(param, src, PF_MT_NONE))) {
    if ((dst_tile_result = pf_is_ok_move_tile(param, dst, src_tile_result))
        && pf_move_check(param, src, src_tile_result,
                         dst, dst_tile_result)) {
      return single_move_cost(param, src, dst);
    } else {
      return overlap_move_cost(param);
    }
  }
  return PF_IMPOSSIBLE_MC;
}

/****************************************************************************
  A cost function for attack movement. Permits attacks.
  Does not permit passing through non-allied occupied tiles.
  Does not permit passing through non-native tiles without transport.
****************************************************************************/
static int attack_move(const struct tile *src,
                       enum direction8 dir,
                       const struct tile *dst,
                       const struct pf_parameter *param)
{
  if (!is_non_allied_unit_tile(src, param->owner)) {
    /* Only keep moving if we haven't just attacked. */
    if (pf_able_to_attack(dst, param, src)) {
      return attack_move_cost(param);
    } else if (pf_able_to_move(param, src, dst)) {
      return single_move_cost(param, src, dst);
    }
  }
  return PF_IMPOSSIBLE_MC;
}

/****************************************************************************
  A cost function for amphibious movement.
****************************************************************************/
static int amphibious_move(const struct tile *ptile, enum direction8 dir,
                           const struct tile *ptile1,
                           const struct pf_parameter *param)
{
  struct pft_amphibious *amphibious = param->data;
  const bool src_ferry = is_native_tile_to_class(amphibious->sea.uclass, ptile);
  const bool dst_ferry = is_native_tile_to_class(amphibious->sea.uclass, ptile1);
  const bool dst_psng = is_native_tile_to_class(amphibious->land.uclass, ptile1);
  int cost, scale;

  if (src_ferry && dst_ferry) {
    /* Sea move */
    cost = amphibious->sea.get_MC(ptile, dir, ptile1, &amphibious->sea);
    scale = amphibious->sea_scale;
  } else if (src_ferry && is_allied_city_tile(ptile1, param->owner)) {
    /* Moving from native terrain to a city. */
    cost = amphibious->sea.get_MC(ptile, dir, ptile1, &amphibious->sea);
    scale = amphibious->sea_scale;
  } else if (src_ferry && dst_psng) {
    /* Disembark; use land movement function to handle UTYF_MARINES */
    cost = amphibious->land.get_MC(ptile, dir, ptile1, &amphibious->land);
    scale = amphibious->land_scale;
  } else if (src_ferry) {
    /* Neither ferry nor passenger can enter tile. */
    cost = PF_IMPOSSIBLE_MC;
    scale = amphibious->sea_scale;
  } else if (is_allied_city_tile(ptile, param->owner) && dst_ferry) {
    /* Leaving port (same as sea move) */
    cost = amphibious->sea.get_MC(ptile, dir, ptile1, &amphibious->sea);
    scale = amphibious->sea_scale;
  } else if (!dst_psng) {
    /* Now we have disembarked, our ferry can not help us - we have to
     * stay on the land. */
    cost = PF_IMPOSSIBLE_MC;
    scale = amphibious->land_scale;
  } else {
    /* land move */
    cost = amphibious->land.get_MC(ptile, dir, ptile1, &amphibious->land);
    scale = amphibious->land_scale;
  }
  if (cost != PF_IMPOSSIBLE_MC) {
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

/* =====================  Tools for filling parameters =============== */

/**********************************************************************
  Fill general use parameters to defaults.
***********************************************************************/
static void pft_fill_default_parameter(struct pf_parameter *parameter,
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
  BV_CLR_ALL(parameter->unit_flags);

  parameter->uclass = punitclass;
  parameter->unit_flags = punittype->flags;
}

/**********************************************************************
  Fill general use parameters to defaults for an unit type.
***********************************************************************/
static void pft_fill_utype_default_parameter(struct pf_parameter *parameter,
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
  parameter->owner = powner;

  if (!BV_ISSET(parameter->unit_flags, UTYF_CIVILIAN)) {
    parameter->can_invade_tile = player_can_invade_tile;
  } else {
    parameter->can_invade_tile = NULL;
  }

  parameter->omniscience = FALSE;
}

/**********************************************************************
  Fill general use parameters to defaults for an unit.
***********************************************************************/
static void pft_fill_unit_default_parameter(struct pf_parameter *parameter,
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
  parameter->owner = unit_owner(punit);

  if (!BV_ISSET(parameter->unit_flags, UTYF_CIVILIAN)) {
    parameter->can_invade_tile = player_can_invade_tile;
  } else {
    parameter->can_invade_tile = NULL;
  }

  parameter->omniscience = FALSE;
}

/**********************************************************************
  Base function to fill classic parameters.
***********************************************************************/
static void pft_fill_parameter(struct pf_parameter *parameter,
                               const struct unit_type *punittype)
{
  parameter->get_MC = normal_move;

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
  parameter->get_MC = attack_move;

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
  parameter->combined.get_TB = amphibious_behaviour;
  parameter->combined.get_EC = amphibious_extra_cost;
  parameter->combined.is_pos_dangerous = amphibious_is_pos_dangerous;
  parameter->combined.get_moves_left_req = NULL;
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
