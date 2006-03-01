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
#include <config.h>
#endif

#include <assert.h>
#include <string.h>

#include "mem.h"

#include "game.h"

#include "pf_tools.h"


static void pft_fill_unit_default_parameter(struct pf_parameter *parameter,
					    struct unit *punit);

/* ===================== Move Cost Callbacks ========================= */

/*************************************************************
  A cost function for SEA_MOVING.  Allows shore bombardment.
  Should be used in conjunction with a TB callback which 
  prohibits going through an enemy city/tile.
*************************************************************/
static int seamove(const struct tile *ptile, enum direction8 dir,
                   const struct tile *ptile1, struct pf_parameter *param)
{
  /* MOVE_COST_FOR_VALID_SEA_STEP means ships can move between */
  if (ptile->move_cost[dir] == MOVE_COST_FOR_VALID_SEA_STEP
      || is_non_allied_unit_tile(ptile1, param->owner)
      || is_non_allied_city_tile(ptile1, param->owner)) {
    return SINGLE_MOVE;
  } else {
    return PF_IMPOSSIBLE_MC;
  }
}

/*************************************************************
  SINGLE_MOVE cost function for AIR_MOVING
*************************************************************/
static int single_airmove(const struct tile *ptile, enum direction8 dir,
			  const struct tile *ptile1,
			  struct pf_parameter *param)
{
  return SINGLE_MOVE; /* simple, eh? */
}

/*************************************************************
  A cost function for SEA_MOVING.  Does not allow shore 
  bombardment.
*************************************************************/
static int seamove_no_bombard(const struct tile *ptile, enum direction8 dir,
			      const struct tile *ptile1,
			      struct pf_parameter *param)
{
  /* MOVE_COST_FOR_VALID_SEA_STEP means ships can move between */
  if (ptile->move_cost[dir] == MOVE_COST_FOR_VALID_SEA_STEP
      && !is_non_allied_city_tile(ptile1, param->owner)) {
    return SINGLE_MOVE;
  } else {
    return PF_IMPOSSIBLE_MC;
  }
}

/************************************************************
  A cost function for a sea unit which allows going one step 
  into the land (shore bombardment).
  Things to remember: we should prevent going from land to
  anywhere, unless we are leaving a friendly city, in which
  case we can move into the ocean but not into the land.
************************************************************/
static int sea_overlap_move(const struct tile *ptile, enum direction8 dir,
			    const struct tile *ptile1,
			    struct pf_parameter *param)
{
  if (is_ocean(ptile->terrain)) {
    return SINGLE_MOVE;
  } else if (is_allied_city_tile(ptile, param->owner)
	     && is_ocean(ptile1->terrain)) {
    return SINGLE_MOVE;
  }

  return PF_IMPOSSIBLE_MC;
}

/**********************************************************************
  Sea attack is the same as overlap (consider bombardment) but we don't
  want to pass through enemy tiles.
**********************************************************************/
static int sea_attack_move(const struct tile *src_tile, enum direction8 dir,
			   const struct tile *dest_tile,
			   struct pf_parameter *param)
{
  if (is_ocean(src_tile->terrain)) {
    if (is_non_allied_unit_tile(src_tile, param->owner)) {
      return PF_IMPOSSIBLE_MC;
    }
    return SINGLE_MOVE;
  } else if (is_allied_city_tile(src_tile, param->owner)
	     && is_ocean(dest_tile->terrain)) {
    return SINGLE_MOVE;
  }

  return PF_IMPOSSIBLE_MC;
}

/************************************************************ 
  LAND_MOVE cost function for a unit 
************************************************************/
static int normal_move_unit(const struct tile *ptile, enum direction8 dir,
			    const struct tile *ptile1,
			    struct pf_parameter *param)
{
  Terrain_type_id terrain1 = ptile1->terrain;
  int move_cost;

  if (is_ocean(terrain1)) {
    if (ground_unit_transporter_capacity(ptile1, param->owner) > 0) {
      move_cost = SINGLE_MOVE;
    } else {
      move_cost = PF_IMPOSSIBLE_MC;
    }
  } else if (is_ocean(ptile->terrain)) {
    if (!BV_ISSET(param->unit_flags, F_MARINES)
        && (is_non_allied_unit_tile(ptile1, param->owner) 
            || is_non_allied_city_tile(ptile1, param->owner))) {
      move_cost = PF_IMPOSSIBLE_MC;
    } else {
      move_cost = get_tile_type(terrain1)->movement_cost * SINGLE_MOVE;
    }
  } else {
    move_cost = ptile->move_cost[dir];
  }

  return move_cost;
}

/******************************************************************* 
  LAND_MOVE cost function for a unit, but taking into account
  possibilities of attacking.
*******************************************************************/
static int land_attack_move(const struct tile *src_tile, enum direction8 dir,
			    const struct tile *tgt_tile,
			    struct pf_parameter *param)
{
  int move_cost;

  if (is_ocean(tgt_tile->terrain)) {

    /* Any-to-Sea */
    if (ground_unit_transporter_capacity(tgt_tile, param->owner) > 0) {
      move_cost = SINGLE_MOVE;
    } else {
      move_cost = PF_IMPOSSIBLE_MC;
    }
  } else if (is_ocean(src_tile->terrain)) {

    /* Sea-to-Land. */
    if (!is_non_allied_unit_tile(tgt_tile, param->owner)
        && !is_non_allied_city_tile(tgt_tile, param->owner)) {
      move_cost 
        = get_tile_type(tgt_tile->terrain)->movement_cost * SINGLE_MOVE;
    } else if (BV_ISSET(param->unit_flags, F_MARINES)) {
      /* Can attack!! */
      move_cost = SINGLE_MOVE;
    } else {
      move_cost = PF_IMPOSSIBLE_MC;
    }
  } else {

    /* Land-to-Land */
    if (is_non_allied_unit_tile(src_tile, param->owner)) {
      /* Cannot pass through defended tiles */
      move_cost = PF_IMPOSSIBLE_MC;
    } else if (is_non_allied_unit_tile(tgt_tile, param->owner)) {

      /* Attack! */
      move_cost = SINGLE_MOVE;
    } else {
      /* Normal move */
      move_cost = src_tile->move_cost[dir];
    }
  }

  return move_cost;
}


/************************************************************ 
  A cost function for a land unit, which allows going into
  the ocean (with moves costing SINGLE_MOVE).  It is 
  recommended to use dont_cross_ocean TB callback with this 
  one, so we don't venture too far into the ocean ;)

  Alternatively, we can change the flow to
  if (is_ocean(ptile->terrain)) {
    move_cost = PF_IMPOSSIBLE_MC;
  } else if (is_ocean(terrain1)) {
    move_cost = SINGLE_MOVE;
  } else {
    move_cost = ptile->move_cost[dir];
  }
  which will achieve the same without call-back.
************************************************************/
static int land_overlap_move(const struct tile *ptile, enum direction8 dir,
			     const struct tile *ptile1,
			     struct pf_parameter *param)
{
  Terrain_type_id terrain1 = ptile1->terrain;
  int move_cost;

  if (is_ocean(terrain1)) {
    move_cost = SINGLE_MOVE;
  } else if (is_ocean(ptile->terrain)) {
    move_cost = get_tile_type(terrain1)->movement_cost * SINGLE_MOVE;
  } else {
    move_cost = ptile->move_cost[dir];
  }

  return move_cost;
}

/************************************************************ 
  Reversed LAND_MOVE cost function for a unit.
  Will be used. DO NOT REMOVE.
************************************************************/
#ifdef UNUSED
static int reverse_move_unit(const struct tile *tile0, enum direction8 dir,
			     const struct tile *ptile,
			     struct pf_parameter *param)
{
  int terrain0 = map_get_terrain(tile0);
  int terrain1 = ptile->terrain;
  int move_cost = PF_IMPOSSIBLE_MC;

  if (is_ocean(terrain1)) {
    if (ground_unit_transporter_capacity(ptile, param->owner) > 0) {
      /* Landing */
      move_cost = get_tile_type(terrain0)->movement_cost * SINGLE_MOVE;
    } else {
      /* Nothing to land from */
      move_cost = PF_IMPOSSIBLE_MC;
    }
  } else if (is_ocean(terrain0)) {
    /* Boarding */
    move_cost = SINGLE_MOVE;
  } else {
    move_cost = ptile->move_cost[DIR_REVERSE(dir)];
  }

  return move_cost;
}
#endif

/************************************************************ 
  IGTER_MOVE cost function for a unit 
************************************************************/
static int igter_move_unit(const struct tile *ptile, enum direction8 dir,
			   const struct tile *ptile1,
			   struct pf_parameter *param)
{
  int move_cost;

  if (is_ocean(ptile1->terrain)) {
    if (ground_unit_transporter_capacity(ptile1, param->owner) > 0) {
      move_cost = MOVE_COST_ROAD;
    } else {
      move_cost = PF_IMPOSSIBLE_MC;
    }
  } else if (is_ocean(ptile->terrain)) {
    if (!BV_ISSET(param->unit_flags, F_MARINES)
        && (is_non_allied_unit_tile(ptile1, param->owner) 
            || is_non_allied_city_tile(ptile1, param->owner))) {
      move_cost = PF_IMPOSSIBLE_MC;
    } else {
      move_cost = MOVE_COST_ROAD;
    }
  } else {
    move_cost = (ptile->move_cost[dir] != 0 ? MOVE_COST_ROAD : 0);
  }
  return move_cost;
}

/************************************************************ 
  Reversed IGTER_MOVE cost function for a unit.
  Will be used. DO NOT REMOVE.
************************************************************/
#ifdef UNUSED
static int reverse_igter_move_unit(const struct tile *tile0,
				   enum direction8 dir,
				   const struct tile *ptile,
				   struct pf_parameter *param)
{
  int move_cost;

  if (is_ocean(map_get_terrain(ptile))) {
    if (ground_unit_transporter_capacity(ptile, param->owner) > 0) {
      /* Landing */
      move_cost = MOVE_COST_ROAD;
    } else {
      move_cost = PF_IMPOSSIBLE_MC;
    }
  } else if (is_ocean(map_get_terrain(tile0))) {
    /* Boarding */
    move_cost = MOVE_COST_ROAD;
  } else {
    move_cost =
	(ptile->move_cost[DIR_REVERSE(dir)] != 0 ? MOVE_COST_ROAD : 0);
  }
  return move_cost;
}
#endif


/* ===================== Extra Cost Callbacks ======================== */

/*********************************************************************
  An example of EC callback.  DO NOT REMOVE you pricks!
*********************************************************************/
#ifdef UNUSED
static int afraid_of_dark_forest(const struct tile *ptile,
				 enum known_type known,
				 struct pf_parameter *param)
{
  if (map_get_terrain(ptile) == T_FOREST) {
    /* Willing to spend extra 2 turns to go around a forest tile */
    return PF_TURN_FACTOR * 2;
  }

  return 0;
}
#endif


/* ===================== Tile Behaviour Callbacks ==================== */

/*********************************************************************
  A callback for maps overlapping one square into the ocean.  Insures 
  that we don't continue walking over ocean.
*********************************************************************/
static enum tile_behavior dont_cross_ocean(const struct tile *ptile,
					   enum known_type known,
					   struct pf_parameter *param)
{
  if (is_ocean(ptile->terrain)) {
    return TB_DONT_LEAVE;
  }
  return TB_NORMAL;
}

/********************************************************************** 
  PF callback to prohibit going into the unknown.  Also makes sure we 
  don't plan to attack anyone.
***********************************************************************/
enum tile_behavior no_fights_or_unknown(const struct tile *ptile,
                                        enum known_type known,
                                        struct pf_parameter *param)
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
enum tile_behavior no_fights(const struct tile *ptile, enum known_type known,
			     struct pf_parameter *param)
{
  if (is_non_allied_unit_tile(ptile, param->owner)
      || is_non_allied_city_tile(ptile, param->owner)) {
    /* Can't attack */
    return TB_IGNORE;
  }
  return TB_NORMAL;
}


/* =====================  Postion Dangerous Callbacks ================ */

/**********************************************************************
  An example of position-dangerous callback.  For triremes.
  FIXME: it cheats.
***********************************************************************/
static bool trireme_is_pos_dangerous(const struct tile *ptile,
				     enum known_type known,
				     struct pf_parameter *param)
{
  /* Assume that unknown tiles are unsafe. */
  if (known == TILE_UNKNOWN) {
    return TRUE;
  }

  /* We test TER_UNSAFE even though under the current ruleset there is no
   * way for a trireme to be on a TER_UNSAFE tile. */
  /* Unsafe or unsafe-ocean tiles without cities are dangerous. */
  return ((terrain_has_flag(ptile->terrain, TER_UNSAFE) 
	  || (is_ocean(ptile->terrain) && !is_safe_ocean(ptile)))
	  && ptile->city == NULL);
}

/**********************************************************************
  Position-dangerous callback for all units other than triremes.
***********************************************************************/
static bool is_pos_dangerous(const struct tile *ptile, enum known_type known,
			     struct pf_parameter *param)
{
  /* Unsafe tiles without cities are dangerous. */
  return (terrain_has_flag(ptile->terrain, TER_UNSAFE)
	  && ptile->city == NULL);
}

/* =====================  Tools for filling parameters =============== */

/**********************************************************************
  Fill unit-dependent parameters
***********************************************************************/
void pft_fill_unit_parameter(struct pf_parameter *parameter,
			     struct unit *punit)
{
  pft_fill_unit_default_parameter(parameter, punit);

  switch (unit_type(punit)->move_type) {
  case LAND_MOVING:
    if (unit_flag(punit, F_IGTER)) {
      parameter->get_MC = igter_move_unit;
    } else {
      parameter->get_MC = normal_move_unit;
    }
    break;
  case SEA_MOVING:
    if (unit_flag(punit, F_NO_LAND_ATTACK)) {
      parameter->get_MC = seamove_no_bombard;
    } else {
      parameter->get_MC = seamove;
    }
    break;
  case AIR_MOVING:
    parameter->get_MC = single_airmove;
    break;
  default:
    die("unknown move_type");
  }

  if (unit_type(punit)->move_type == LAND_MOVING 
      && !unit_flag(punit, F_IGZOC)) {
    parameter->get_zoc = is_my_zoc;
  } else {
    parameter->get_zoc = NULL;
  }

  if (unit_flag(punit, F_TRIREME)
      && base_trireme_loss_pct(unit_owner(punit), punit) > 0) {
    parameter->turn_mode = TM_WORST_TIME;
    parameter->is_pos_dangerous = trireme_is_pos_dangerous;
  } else if (base_unsafe_terrain_loss_pct(unit_owner(punit), punit) > 0) {
    parameter->turn_mode = TM_WORST_TIME;
    parameter->is_pos_dangerous = is_pos_dangerous;
  }
}

/**********************************************************************
  Switch on one tile overlapping into the sea/land 
  ("sea/land bombardment")
**********************************************************************/
void pft_fill_unit_overlap_param(struct pf_parameter *parameter,
				 struct unit *punit)
{
  pft_fill_unit_default_parameter(parameter, punit);

  switch (unit_type(punit)->move_type) {
  case LAND_MOVING:
    parameter->get_MC = land_overlap_move;
    parameter->get_TB = dont_cross_ocean;
    break;
  case SEA_MOVING:
    parameter->get_MC = sea_overlap_move;
    break;
  default:
    die("Unsupported move_type");
  }

  parameter->get_zoc = NULL;

  if (unit_flag(punit, F_TRIREME)
      && base_trireme_loss_pct(unit_owner(punit), punit) > 0) {
    parameter->is_pos_dangerous = trireme_is_pos_dangerous;
  } else if (base_unsafe_terrain_loss_pct(unit_owner(punit), punit) > 0) {
    parameter->is_pos_dangerous = is_pos_dangerous;
  }
}

/**********************************************************************
  Consider attacking and non-attacking possibilities properly
**********************************************************************/
void pft_fill_unit_attack_param(struct pf_parameter *parameter,
                                struct unit *punit)
{
  pft_fill_unit_default_parameter(parameter, punit);

  switch (unit_type(punit)->move_type) {
  case LAND_MOVING:
    parameter->get_MC = land_attack_move;
    break;
  case SEA_MOVING:
    parameter->get_MC = sea_attack_move;
    break;
  default:
    die("Unsupported move_type");
  }

  if (unit_type(punit)->move_type == LAND_MOVING 
      && !unit_flag(punit, F_IGZOC)) {
    parameter->get_zoc = is_my_zoc;
  } else {
    parameter->get_zoc = NULL;
  }

  /* It is too complicated to work with danger here */
  parameter->is_pos_dangerous = NULL;
}

/**********************************************************************
  Fill general use parameters to defaults
***********************************************************************/
static void pft_fill_unit_default_parameter(struct pf_parameter *parameter,
					    struct unit *punit)
{
  parameter->turn_mode = TM_CAPPED;
  if (is_air_unit(punit) || is_heli_unit(punit)) {
    parameter->unknown_MC = SINGLE_MOVE;
  } else if (is_sailing_unit(punit)) {
    parameter->unknown_MC = 2 * SINGLE_MOVE;
  } else {
    assert(is_ground_unit(punit));
    parameter->unknown_MC = SINGLE_MOVE;
    terrain_type_iterate(t) {
      int mr = 2 * get_tile_type(t)->movement_cost;

      parameter->unknown_MC = MAX(mr, parameter->unknown_MC);
    } terrain_type_iterate_end;
  }
  parameter->get_TB = NULL;
  parameter->get_EC = NULL;
  parameter->is_pos_dangerous = NULL;
  parameter->get_costs = NULL;
  parameter->get_zoc = NULL;
  BV_CLR_ALL(parameter->unit_flags);

  parameter->start_tile = punit->tile;
  parameter->moves_left_initially = punit->moves_left;
  parameter->move_rate = unit_move_rate(punit);
  parameter->owner = unit_owner(punit);
  parameter->unit_flags = unit_type(punit)->flags;

  parameter->omniscience = !ai_handicap(unit_owner(punit), H_MAP);
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

    assert(pf_last_position(dest_path)->tile == src_path->positions[0].tile);
    assert(pf_last_position(dest_path)->moves_left ==
	   src_path->positions[0].moves_left);
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
