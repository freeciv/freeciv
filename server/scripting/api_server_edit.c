/*****************************************************************************
 Freeciv - Copyright (C) 2005 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
*****************************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* utility */
#include "deprecations.h"
#include "rand.h"

/* common */
#include "map.h"
#include "movement.h"
#include "research.h"
#include "unittype.h"

/* common/scriptcore */
#include "api_game_find.h"
#include "luascript.h"

/* server */
#include "aiiface.h"
#include "barbarian.h"
#include "citytools.h"
#include "cityturn.h" /* city_refresh() auto_arrange_workers() */
#include "console.h" /* enum rfc_status */
#include "gamehand.h"
#include "maphand.h"
#include "notify.h"
#include "plrhand.h"
#include "srv_main.h" /* game_was_started() */
#include "stdinhand.h"
#include "techtools.h"
#include "unithand.h"
#include "unittools.h"

/* server/scripting */
#include "script_server.h"

/* server/generator */
#include "mapgen_utils.h"

#include "api_server_edit.h"


/**********************************************************************//**
  Warn about use of a deprecated number of arguments.
**************************************************************************/
static void deprecated_semantic_warning(const char *call, const char *aka,
                                        const char *deprecated_since)
{
  if (are_deprecation_warnings_enabled()) {
    log_deprecation_always(
        "Deprecated: Lua call %s aka %s filling out the remaining"
        " parameters based on the old rules is deprecated"
        " since Freeciv %s.",
        call, aka, deprecated_since);
  }
}

/**********************************************************************//**
  A wrapper around transform_unit() that correctly processes
  some unsafe requests. punit and to_unit must not be NULL.
**************************************************************************/
static bool
ur_transform_unit(struct unit *punit, const struct unit_type *to_unit,
                  int vet_loss)
{
  if (UU_OK == unit_transform_result(&(wld.map), punit, to_unit)) {
    /* Avoid getting overt veteranship if a user requests increasing it */
    if (vet_loss < 0) {
      int vl = utype_veteran_levels(to_unit);

      vl = punit->veteran - vl + 1;
      if (vl >= 0) {
        vet_loss = 0;
      } else {
        vet_loss = MAX(vet_loss, vl);
      }
    }
    transform_unit(punit, to_unit, vet_loss);
    return TRUE;
  } else {
    return FALSE;
  }
}

/**********************************************************************//**
  Unleash barbarians on a tile, for example from a hut
**************************************************************************/
bool api_edit_unleash_barbarians(lua_State *L, Tile *ptile)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, ptile, 2, Tile, FALSE);

  return unleash_barbarians(ptile);
}

/**********************************************************************//**
  Place partisans for a player around a tile (normally around a city).
**************************************************************************/
void api_edit_place_partisans(lua_State *L, Tile *ptile, Player *pplayer,
                              int count, int sq_radius)
{
  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_ARG_NIL(L, ptile, 2, Tile);
  LUASCRIPT_CHECK_ARG_NIL(L, pplayer, 3, Player);
  LUASCRIPT_CHECK_ARG(L, 0 <= sq_radius, 5, "radius must be positive");
  LUASCRIPT_CHECK(L, 0 < num_role_units(L_PARTISAN),
                  "no partisans in ruleset");

  return place_partisans(ptile, pplayer, count, sq_radius);
}

/**********************************************************************//**
  Create a new unit.
**************************************************************************/
Unit *api_edit_create_unit(lua_State *L, Player *pplayer, Tile *ptile,
                           Unit_Type *ptype, int veteran_level,
                           City *homecity, int moves_left)
{
  return api_edit_create_unit_full(L, pplayer, ptile, ptype, veteran_level,
                                   homecity, moves_left, -1, NULL);
}

/**********************************************************************//**
  Create a new unit.
**************************************************************************/
Unit *api_edit_create_unit_full(lua_State *L, Player *pplayer,
                                Tile *ptile,
                                Unit_Type *ptype, int veteran_level,
                                City *homecity, int moves_left,
                                int hp_left,
                                Unit *ptransport)
{
  struct fc_lua *fcl;
  struct city *pcity;
  struct unit *punit;
#ifndef FREECIV_NDEBUG
  bool placed;
#endif

  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_ARG_NIL(L, pplayer, 2, Player, NULL);
  LUASCRIPT_CHECK_ARG_NIL(L, ptile, 3, Tile, NULL);

  fcl = luascript_get_fcl(L);

  LUASCRIPT_CHECK(L, fcl != NULL, "Undefined Freeciv lua state!", NULL);

  if (ptype == NULL
      || ptype < unit_type_array_first() || ptype > unit_type_array_last()) {
    return NULL;
  }

  if (is_non_allied_unit_tile(ptile, pplayer)) {
    luascript_log(fcl, LOG_ERROR, "create_unit_full: tile is occupied by "
                                  "enemy unit");
    return NULL;
  }

  pcity = tile_city(ptile);
  if (pcity != NULL && !pplayers_allied(pplayer, city_owner(pcity))) {
    luascript_log(fcl, LOG_ERROR, "create_unit_full: tile is occupied by "
                                  "enemy city");
    return NULL;
  }

  if (utype_player_already_has_this_unique(pplayer, ptype)) {
    luascript_log(fcl, LOG_ERROR,
                  "create_unit_full: player already has unique unit");
    return NULL;
  }

  punit = unit_virtual_prepare(pplayer, ptile, ptype, veteran_level,
                               homecity ? homecity->id : 0,
                               moves_left, hp_left);
  if (ptransport) {
    /* The unit maybe can't freely load into the transport
     * but must be able to be in it, see can_unit_load() */
    int ret = same_pos(ptile, unit_tile(ptransport))
       && could_unit_be_in_transport(punit, ptransport);

    if (!ret) {
      unit_virtual_destroy(punit);
      luascript_log(fcl, LOG_ERROR, "create_unit_full: '%s' cannot transport "
                                    "'%s' here",
                    utype_rule_name(unit_type_get(ptransport)),
                    utype_rule_name(ptype));
      return NULL;
    }
  } else if (!can_exist_at_tile(&(wld.map), ptype, ptile)) {
    unit_virtual_destroy(punit);
    luascript_log(fcl, LOG_ERROR, "create_unit_full: '%s' cannot exist at "
                                  "tile", utype_rule_name(ptype));
    return NULL;
  }

#ifndef FREECIV_NDEBUG
  placed =
#endif
  place_unit(punit, pplayer, homecity, ptransport, TRUE);
  fc_assert_action(placed, unit_virtual_destroy(punit); punit = NULL);

  return punit;
}

/**********************************************************************//**
  Teleport unit to destination tile
**************************************************************************/
bool api_edit_unit_teleport(lua_State *L, Unit *punit, Tile *dest,
                            Unit *embark_to, bool allow_disembark,
                            bool conquer_city, bool conquer_extra,
                            bool enter_hut, bool frighten_hut)
{
  bool alive;

  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, punit, 2, Unit, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, dest, 3, Tile, FALSE);

  LUASCRIPT_CHECK(L, !(enter_hut && frighten_hut),
                  "Can't both enter and frighten a hut at the same time",
                  TRUE);

  if (!allow_disembark && unit_transported(punit)) {
    /* Can't leave the transport. */
    return TRUE;
  }

  if (unit_teleport_to_tile_test(&(wld.map), punit, ACTIVITY_IDLE,
                                 unit_tile(punit), dest, FALSE,
                                 embark_to, TRUE) != MR_OK) {
    /* Can't teleport to target. Return that unit is still alive. */
    return TRUE;
  }

  /* Teleport first so destination is revealed even if unit dies */
  alive = unit_move(punit, dest, 0,
                    embark_to, embark_to != NULL,
                    conquer_city, conquer_extra,
                    enter_hut, frighten_hut);
  if (alive) {
    struct player *owner = unit_owner(punit);
    struct city *pcity = tile_city(dest);

    if (!can_unit_exist_at_tile(&(wld.map), punit, dest)
        && !unit_transported(punit)) {
      wipe_unit(punit, ULR_NONNATIVE_TERR, NULL);
      return FALSE;
    }
    if (is_non_allied_unit_tile(dest, owner)
        || (pcity && !pplayers_allied(city_owner(pcity), owner))) {
      wipe_unit(punit, ULR_STACK_CONFLICT, NULL);
      return FALSE;
    }
  }

  return alive;
}

/**********************************************************************//**
  Teleport unit to destination tile
**************************************************************************/
bool api_edit_unit_teleport_old(lua_State *L, Unit *punit, Tile *dest)
{
  bool alive;
  struct city *pcity;

  deprecated_semantic_warning("edit.unit_teleport(unit, dest)",
                              "Unit:teleport(dest)", "3.1");

  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, punit, 2, Unit, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, dest, 3, Tile, FALSE);

  /* Teleport first so destination is revealed even if unit dies */
  alive = unit_move(punit, dest, 0,
                    /* Auto embark kept for backward compatibility. I have
                     * no objection if you see the old behavior as a bug and
                     * remove auto embarking completely or for transports
                     * the unit can't legally board. -- Sveinung */
                    NULL, TRUE,
                    /* Backwards compatibility for old scripts in rulesets
                     * and (scenario) savegames. I have no objection if you
                     * see the old behavior as a bug and remove auto
                     * conquering completely or for cities the unit can't
                     * legally conquer. -- Sveinung */
                    ((pcity = tile_city(dest))
                     && (unit_owner(punit)->ai_common.barbarian_type
                         != ANIMAL_BARBARIAN)
                     && uclass_has_flag(unit_class_get(punit),
                                        UCF_CAN_OCCUPY_CITY)
                     && !unit_has_type_flag(punit, UTYF_CIVILIAN)
                     && pplayers_at_war(unit_owner(punit),
                                        city_owner(pcity))),
                    (extra_owner(dest) == NULL
                     || pplayers_at_war(extra_owner(dest),
                                        unit_owner(punit)))
                    && tile_has_claimable_base(dest, unit_type_get(punit)),
                    /* Backwards compatibility: unit_enter_hut() would
                     * return without doing anything if the unit was
                     * HUT_NOTHING. Setting this parameter to FALSE makes
                     * sure unit_enter_hut() isn't called. */
                    unit_can_do_action_result(punit, ACTRES_HUT_ENTER),
                    unit_can_do_action_result(punit, ACTRES_HUT_FRIGHTEN));
  if (alive) {
    struct player *owner = unit_owner(punit);

    if (!can_unit_exist_at_tile(&(wld.map), punit, dest)) {
      wipe_unit(punit, ULR_NONNATIVE_TERR, NULL);
      return FALSE;
    }
    if (is_non_allied_unit_tile(dest, owner)
        || (pcity && !pplayers_allied(city_owner(pcity), owner))) {
      wipe_unit(punit, ULR_STACK_CONFLICT, NULL);
      return FALSE;
    }
  }

  return alive;
}

/***********************************************************************//**
  Force a unit to perform an action against a city.
***************************************************************************/
bool api_edit_perform_action_unit_vs_city(lua_State *L, Unit *punit,
                                          Action *paction, City *tgt)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, punit, 2, Unit, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, paction, 3, Action, FALSE);
  LUASCRIPT_CHECK_ARG(L, action_get_target_kind(paction) == ATK_CITY, 3,
                      "Not a city-targeted action", FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, tgt, 4, City, FALSE);

  fc_assert_ret_val(action_get_actor_kind(paction) == AAK_UNIT, FALSE);
  fc_assert_ret_val(!action_has_result(paction, ACTRES_FOUND_CITY), FALSE);
  if (is_action_enabled_unit_on_city(&(wld.map), paction->id, punit, tgt)) {
    return unit_perform_action(unit_owner(punit), punit->id,
                               tgt->id, IDENTITY_NUMBER_ZERO, "",
                               paction->id, ACT_REQ_RULES);
  } else {
    /* Action not enabled */
    return FALSE;
  }
}

/***********************************************************************//**
  Force a unit to perform an action against a city and a building.
***************************************************************************/
bool api_edit_perform_action_unit_vs_city_impr(lua_State *L, Unit *punit,
                                               Action *paction, City *tgt,
                                               Building_Type *sub_tgt)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, punit, 2, Unit, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, paction, 3, Action, FALSE);
  LUASCRIPT_CHECK_ARG(L, action_get_target_kind(paction) == ATK_CITY, 3,
                      "Not a city-targeted action", FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, tgt, 4, City, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, sub_tgt, 5, Building_Type, FALSE);

  fc_assert_ret_val(action_get_actor_kind(paction) == AAK_UNIT, FALSE);
  fc_assert_ret_val(!action_has_result(paction, ACTRES_FOUND_CITY), FALSE);
  if (is_action_enabled_unit_on_city(&(wld.map), paction->id, punit, tgt)) {
    return unit_perform_action(unit_owner(punit), punit->id,
                               tgt->id, sub_tgt->item_number, "",
                               paction->id, ACT_REQ_RULES);
  } else {
    /* Action not enabled */
    return FALSE;
  }
}

/***********************************************************************//**
  Force a unit to perform an action against a city and a tech.
***************************************************************************/
bool api_edit_perform_action_unit_vs_city_tech(lua_State *L, Unit *punit,
                                               Action *paction, City *tgt,
                                               Tech_Type *sub_tgt)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, punit, 2, Unit, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, paction, 3, Action, FALSE);
  LUASCRIPT_CHECK_ARG(L, action_get_target_kind(paction) == ATK_CITY, 3,
                      "Not a city-targeted action", FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, tgt, 4, City, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, sub_tgt, 5, Tech_Type, FALSE);

  fc_assert_ret_val(action_get_actor_kind(paction) == AAK_UNIT, FALSE);
  fc_assert_ret_val(!action_has_result(paction, ACTRES_FOUND_CITY), FALSE);
  if (is_action_enabled_unit_on_city(&(wld.map), paction->id, punit, tgt)) {
    return unit_perform_action(unit_owner(punit), punit->id,
                               tgt->id, sub_tgt->item_number, "",
                               paction->id, ACT_REQ_RULES);
  } else {
    /* Action not enabled */
    return FALSE;
  }
}

/***********************************************************************//**
  Force a unit to perform an action against a unit.
***************************************************************************/
bool api_edit_perform_action_unit_vs_unit(lua_State *L, Unit *punit,
                                          Action *paction, Unit *tgt)
{
  const struct civ_map *nmap = &(wld.map);

  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, punit, 2, Unit, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, paction, 3, Action, FALSE);
  LUASCRIPT_CHECK_ARG(L, action_get_target_kind(paction) == ATK_UNIT, 3,
                      "Not a unit-targeted action", FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, tgt, 4, Unit, FALSE);

  fc_assert_ret_val(action_get_actor_kind(paction) == AAK_UNIT, FALSE);
  fc_assert_ret_val(!action_has_result(paction, ACTRES_FOUND_CITY), FALSE);
  if (is_action_enabled_unit_on_unit(nmap, paction->id, punit, tgt)) {
    return unit_perform_action(unit_owner(punit), punit->id,
                               tgt->id, IDENTITY_NUMBER_ZERO, "",
                               paction->id, ACT_REQ_RULES);
  } else {
    /* Action not enabled */
    return FALSE;
  }
}

/***********************************************************************//**
  Force a unit to perform an action against a tile.
***************************************************************************/
bool api_edit_perform_action_unit_vs_tile(lua_State *L, Unit *punit,
                                          Action *paction, Tile *tgt)
{
  bool enabled = FALSE;
  const struct civ_map *nmap = &(wld.map);

  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, punit, 2, Unit, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, paction, 3, Action, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, tgt, 4, Tile, FALSE);

  fc_assert_ret_val(action_get_actor_kind(paction) == AAK_UNIT, FALSE);
  switch (action_get_target_kind(paction)) {
  case ATK_UNITS:
    enabled = is_action_enabled_unit_on_units(nmap, paction->id, punit, tgt);
    break;
  case ATK_TILE:
    enabled = is_action_enabled_unit_on_tile(nmap, paction->id, punit,
                                             tgt, NULL);
    break;
  case ATK_EXTRAS:
    enabled = is_action_enabled_unit_on_extras(nmap, paction->id, punit,
                                               tgt, NULL);
    break;
  case ATK_CITY:
    /* Not handled here. */
    LUASCRIPT_CHECK_ARG(L, action_get_target_kind(paction) != ATK_CITY, 3,
                        "City-targeted action applied to tile", FALSE);
    break;
  case ATK_UNIT:
    /* Not handled here. */
    LUASCRIPT_CHECK_ARG(L, action_get_target_kind(paction) != ATK_UNIT, 3,
                        "Unit-targeted action applied to tile", FALSE);
    break;
  case ATK_SELF:
    /* Not handled here. */
    LUASCRIPT_CHECK_ARG(L, action_get_target_kind(paction) != ATK_SELF, 3,
                        "Self-targeted action applied to tile", FALSE);
    break;
  case ATK_COUNT:
    /* Should not exist */
    fc_assert(action_get_target_kind(paction) != ATK_COUNT);
    break;
  }

  if (enabled) {
    return unit_perform_action(unit_owner(punit), punit->id,
                               tile_index(tgt), IDENTITY_NUMBER_ZERO,
                               city_name_suggestion(unit_owner(punit), tgt),
                               paction->id, ACT_REQ_RULES);
  } else {
    /* Action not enabled */
    return FALSE;
  }
}

/***********************************************************************//**
  Force a unit to perform an action against a tile and an extra.
***************************************************************************/
bool api_edit_perform_action_unit_vs_tile_extra(lua_State *L, Unit *punit,
                                                Action *paction, Tile *tgt,
                                                const char *sub_tgt)
{
  struct extra_type *sub_target;
  bool enabled = FALSE;
  const struct civ_map *nmap = &(wld.map);

  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, punit, 2, Unit, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, paction, 3, Action, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, tgt, 4, Tile, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, sub_tgt, 5, string, FALSE);

  sub_target = extra_type_by_rule_name(sub_tgt);
  LUASCRIPT_CHECK_ARG(L, sub_target != NULL, 5, "No such extra", FALSE);

  fc_assert_ret_val(action_get_actor_kind(paction) == AAK_UNIT, FALSE);
  switch (action_get_target_kind(paction)) {
  case ATK_UNITS:
    enabled = is_action_enabled_unit_on_units(nmap, paction->id, punit, tgt);
    break;
  case ATK_TILE:
    enabled = is_action_enabled_unit_on_tile(nmap, paction->id, punit,
                                             tgt, sub_target);
    break;
  case ATK_EXTRAS:
    enabled = is_action_enabled_unit_on_extras(nmap, paction->id, punit,
                                               tgt, sub_target);
    break;
  case ATK_CITY:
    /* Not handled here. */
    LUASCRIPT_CHECK_ARG(L, action_get_target_kind(paction) != ATK_CITY, 3,
                        "City-targeted action applied to tile", FALSE);
    break;
  case ATK_UNIT:
    /* Not handled here. */
    LUASCRIPT_CHECK_ARG(L, action_get_target_kind(paction) != ATK_UNIT, 3,
                        "Unit-targeted action applied to tile", FALSE);
    break;
  case ATK_SELF:
    /* Not handled here. */
    LUASCRIPT_CHECK_ARG(L, action_get_target_kind(paction) != ATK_SELF, 3,
                        "Self-targeted action applied to tile", FALSE);
    break;
  case ATK_COUNT:
    /* Should not exist */
    fc_assert(action_get_target_kind(paction) != ATK_COUNT);
    break;
  }

  if (enabled) {
    return unit_perform_action(unit_owner(punit), punit->id,
                               tile_index(tgt), sub_target->id,
                               city_name_suggestion(unit_owner(punit), tgt),
                               paction->id, ACT_REQ_RULES);
  } else {
    /* Action not enabled */
    return FALSE;
  }
}

/***********************************************************************//**
  Force a unit to perform an action against it self.
***************************************************************************/
bool api_edit_perform_action_unit_vs_self(lua_State *L, Unit *punit,
                                          Action *paction)
{
  const struct civ_map *nmap = &(wld.map);

  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, punit, 2, Unit, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, paction, 3, Action, FALSE);
  LUASCRIPT_CHECK_ARG(L, action_get_target_kind(paction) == ATK_SELF, 3,
                      "Not a self-targeted action", FALSE);

  fc_assert_ret_val(action_get_actor_kind(paction) == AAK_UNIT, FALSE);
  fc_assert_ret_val(!action_has_result(paction, ACTRES_FOUND_CITY), FALSE);
  if (is_action_enabled_unit_on_self(nmap, paction->id, punit)) {
    return unit_perform_action(unit_owner(punit), punit->id,
                               IDENTITY_NUMBER_ZERO, IDENTITY_NUMBER_ZERO,
                               "",
                               paction->id, ACT_REQ_RULES);
  } else {
    /* Action not enabled */
    return FALSE;
  }
}

/**********************************************************************//**
  Change unit orientation
**************************************************************************/
void api_edit_unit_turn(lua_State *L, Unit *punit, Direction dir)
{
  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_ARG_NIL(L, punit, 2, Unit);
 
  if (direction8_is_valid(dir)) {
    punit->facing = dir;

    send_unit_info(NULL, punit);
  } else {
    log_error("Illegal direction %d for unit from lua script", dir);
  }
}

/**********************************************************************//**
  Upgrade punit for free in the default manner, lose vet_loss vet levels.
  Returns if the upgrade was possible.
**************************************************************************/
bool api_edit_unit_upgrade(lua_State *L, Unit *punit, int vet_loss)
{
  const struct unit_type *ptype;

  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, punit, FALSE);

  ptype = can_upgrade_unittype(unit_owner(punit), unit_type_get(punit));
  if (!ptype) {
    return FALSE;
  }
  return ur_transform_unit(punit, ptype, vet_loss);
}

/**********************************************************************//**
  Transform punit to ptype, decreasing vet_loss veteranship levels.
  Returns if the transformation was possible.
**************************************************************************/
bool api_edit_unit_transform(lua_State *L, Unit *punit, Unit_Type *ptype,
                             int vet_loss)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, punit, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, ptype, 3, Unit_Type, FALSE);

  return ur_transform_unit(punit, ptype, vet_loss);
}

/**********************************************************************//**
  Kill the unit.
**************************************************************************/
void api_edit_unit_kill(lua_State *L, Unit *punit, const char *reason,
                        Player *killer)
{
  enum unit_loss_reason loss_reason;

  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_ARG_NIL(L, punit, 2, Unit);
  LUASCRIPT_CHECK_ARG_NIL(L, reason, 3, string);

  loss_reason = unit_loss_reason_by_name(reason, fc_strcasecmp);

  LUASCRIPT_CHECK_ARG(L, unit_loss_reason_is_valid(loss_reason), 3,
                      "Invalid unit loss reason");

  wipe_unit(punit, loss_reason, killer);
}

/**********************************************************************//**
  Change terrain on tile
**************************************************************************/
bool api_edit_change_terrain(lua_State *L, Tile *ptile, Terrain *pterr)
{
  struct terrain *old_terrain;

  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, ptile, 2, Tile, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, pterr, 3, Terrain, FALSE);

  old_terrain = tile_terrain(ptile);

  if (old_terrain == pterr
      || (terrain_has_flag(pterr, TER_NO_CITIES)
          && tile_city(ptile) != NULL)) {
    return FALSE;
  }

  tile_change_terrain(ptile, pterr);
  fix_tile_on_terrain_change(ptile, old_terrain, FALSE);
  if (need_to_reassign_continents(old_terrain, pterr)) {
    assign_continent_numbers();

    /* FIXME: adv / ai phase handling like in check_terrain_change() */

    send_all_known_tiles(NULL);
  }

  update_tile_knowledge(ptile);

  tile_change_side_effects(ptile, TRUE);

  return TRUE;
}

/**********************************************************************//**
  Create a new city.
**************************************************************************/
bool api_edit_create_city(lua_State *L, Player *pplayer, Tile *ptile,
                          const char *name)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, pplayer, 2, Player, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, ptile, 3, Tile, FALSE);

  /* TODO: Allow initial citizen to be of nationality other than owner */
  return create_city_for_player(pplayer, ptile, name);
}

/**********************************************************************//**
  Destroy a city
**************************************************************************/
void api_edit_remove_city(lua_State *L, City *pcity)
{
  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_ARG_NIL(L, pcity, 2, City);

  remove_city(pcity);
}

/**********************************************************************//**
  Create a building to a city
**************************************************************************/
void api_edit_create_building(lua_State *L, City *pcity, Building_Type *impr)
{
  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_ARG_NIL(L, pcity, 2, City);
  LUASCRIPT_CHECK_ARG_NIL(L, impr, 3, Building_Type);
  /* FIXME: may "Special" impr be buildable? */
  LUASCRIPT_CHECK_ARG(L, !is_special_improvement(impr), 3,
                      "It is a special item, not a city building");

  if (!city_has_building(pcity, impr)) {
    bool need_game_info = FALSE;
    bool need_plr_info = FALSE;
    struct player *old_owner = NULL, *pplayer = city_owner(pcity);
    struct city *oldcity;

    oldcity = build_or_move_building(pcity, impr, &old_owner);
    if (oldcity) {
      need_plr_info = TRUE;
    }
    if (old_owner && old_owner != pplayer) {
      /* Great wonders make more changes. */
      need_game_info = TRUE;
    }

    if (oldcity) {
      if (city_refresh(oldcity)) {
        auto_arrange_workers(oldcity);
      }
      send_city_info(NULL, oldcity);
    }

    if (city_refresh(pcity)) {
      auto_arrange_workers(pcity);
    }
    send_city_info(NULL, pcity);
    if (need_game_info) {
      send_game_info(NULL);
      send_player_info_c(old_owner, NULL);
    }
    if (need_plr_info) {
      send_player_info_c(pplayer, NULL);
    }
  }
}

/**********************************************************************//**
  Remove a building from a city
**************************************************************************/
void api_edit_remove_building(lua_State *L, City *pcity, Building_Type *impr)
{
  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_ARG_NIL(L, pcity, 2, City);
  LUASCRIPT_CHECK_ARG_NIL(L, impr, 3, Building_Type);

  if (city_has_building(pcity, impr)) {
    city_remove_improvement(pcity, impr);
    send_city_info(NULL, pcity);

    if (is_wonder(impr)) {
      if (is_great_wonder(impr)) {
        send_game_info(NULL);
      }
      send_player_info_c(city_owner(pcity), NULL);
    }
  }
}

/**********************************************************************//**
  Create a new player.
**************************************************************************/
Player *api_edit_create_player(lua_State *L, const char *username,
                               Nation_Type *pnation, const char *ai)
{
  struct player *pplayer = NULL;
  char buf[128] = "";
  struct fc_lua *fcl;

  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_ARG_NIL(L, username, 2, string, NULL);
  if (!ai) {
    ai = default_ai_type_name();
  }

  fcl = luascript_get_fcl(L);

  LUASCRIPT_CHECK(L, fcl != NULL, "Undefined Freeciv lua state!", NULL);

  if (game_was_started()) {
    create_command_newcomer(username, ai, FALSE, pnation, &pplayer,
                            buf, sizeof(buf));
  } else {
    create_command_pregame(username, ai, FALSE, &pplayer,
                           buf, sizeof(buf));
  }

  if (strlen(buf) > 0) {
    luascript_log(fcl, LOG_NORMAL, "%s", buf);
  }

  return pplayer;
}

/**********************************************************************//**
  Change pplayer's gold by amount.
**************************************************************************/
void api_edit_change_gold(lua_State *L, Player *pplayer, int amount)
{
  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_ARG_NIL(L, pplayer, 2, Player);

  pplayer->economic.gold = MAX(0, pplayer->economic.gold + amount);

  send_player_info_c(pplayer, NULL);
}

/**********************************************************************//**
  Give pplayer technology ptech. Quietly returns NULL if
  player already has this tech; otherwise returns the tech granted.
  Use NULL for ptech to grant a random tech.
  sends script signal "tech_researched" with the given reason
**************************************************************************/
Tech_Type *api_edit_give_technology(lua_State *L, Player *pplayer,
                                    Tech_Type *ptech, int cost,
                                    bool notify,
                                    const char *reason)
{
  struct research *presearch;
  Tech_type_id id;
  Tech_Type *result;

  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_ARG_NIL(L, pplayer, 2, Player, NULL);
  LUASCRIPT_CHECK_ARG(L, cost >= -3, 4, "Unknown give_tech() cost value", NULL);

  presearch = research_get(pplayer);
  if (ptech) {
    id = advance_number(ptech);
  } else {
    id = pick_free_tech(presearch);
  }

  if (is_future_tech(id)
      || research_invention_state(presearch, id) != TECH_KNOWN) {
    if (cost < 0) {
      if (cost == -1) {
        cost = game.server.freecost;
      } else if (cost == -2) {
        cost = game.server.conquercost;
      } else if (cost == -3) {
        cost = game.server.diplbulbcost;
      } else {
        
        cost = 0;
      }
    }
    research_apply_penalty(presearch, id, cost);
    found_new_tech(presearch, id, FALSE, TRUE);
    result = advance_by_number(id);
    script_tech_learned(presearch, pplayer, result, reason);

    if (notify && result != NULL) {
      const char *adv_name = research_advance_name_translation(presearch, id);
      char research_name[MAX_LEN_NAME * 2];

      research_pretty_name(presearch, research_name, sizeof(research_name));

      notify_player(pplayer, NULL, E_TECH_GAIN, ftc_server,
                    Q_("?fromscript:You acquire %s."), adv_name);
      notify_research(presearch, pplayer, E_TECH_GAIN, ftc_server,
                      /* TRANS: "The Greeks ..." or "The members of
                       * team Red ..." */
                      Q_("?fromscript:The %s acquire %s and share this "
                         "advance with you."),
                      nation_plural_for_player(pplayer), adv_name);
      notify_research_embassies(presearch, NULL, E_TECH_EMBASSY, ftc_server,
                                /* TRANS: "The Greeks ..." or "The members of
                                 * team Red ..." */
                                Q_("?fromscript:The %s acquire %s."),
                                research_name, adv_name);
    }

    return result;
  } else {
    return NULL;
  }
}

/**********************************************************************//**
  Modify player's trait value.
**************************************************************************/
bool api_edit_trait_mod_set(lua_State *L, Player *pplayer,
                            const char *tname, const int mod)
{
  enum trait tr;

  LUASCRIPT_CHECK_STATE(L, -1);
  LUASCRIPT_CHECK_ARG_NIL(L, pplayer, 2, Player, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, tname, 3, string, FALSE);

  tr = trait_by_name(tname, fc_strcasecmp);

  LUASCRIPT_CHECK_ARG(L, trait_is_valid(tr), 3, "no such trait", 0);

  pplayer->ai_common.traits[tr].mod += mod;

  return TRUE;
}

/**********************************************************************//**
  Create a new owned extra.
**************************************************************************/
void api_edit_create_owned_extra(lua_State *L, Tile *ptile,
                                 const char *name, Player *pplayer)
{
  struct extra_type *pextra;

  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_ARG_NIL(L, ptile, 2, Tile);

  if (name == NULL) {
    return;
  }

  pextra = extra_type_by_rule_name(name);

  if (pextra != NULL) {
    create_extra(ptile, pextra, pplayer);
    update_tile_knowledge(ptile);
    tile_change_side_effects(ptile, TRUE);
  }
}

/**********************************************************************//**
  Create a new extra.
**************************************************************************/
void api_edit_create_extra(lua_State *L, Tile *ptile, const char *name)
{
  api_edit_create_owned_extra(L, ptile, name, NULL);
}

/**********************************************************************//**
  Create a new base.
**************************************************************************/
void api_edit_create_base(lua_State *L, Tile *ptile, const char *name,
                          Player *pplayer)
{
  api_edit_create_owned_extra(L, ptile, name, pplayer);
}

/**********************************************************************//**
  Add a new road.
**************************************************************************/
void api_edit_create_road(lua_State *L, Tile *ptile, const char *name)
{
  api_edit_create_owned_extra(L, ptile, name, NULL);
}

/**********************************************************************//**
  Remove extra from tile, if present
**************************************************************************/
void api_edit_remove_extra(lua_State *L, Tile *ptile, const char *name)
{
  struct extra_type *pextra;

  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_ARG_NIL(L, ptile, 2, Tile);

  if (name == NULL) {
    return;
  }

  pextra = extra_type_by_rule_name(name);

  if (pextra != NULL && tile_has_extra(ptile, pextra)) {
    tile_extra_rm_apply(ptile, pextra);
    update_tile_knowledge(ptile);
    tile_change_side_effects(ptile, TRUE);
  }
}

/**********************************************************************//**
  Set tile label text.
**************************************************************************/
void api_edit_tile_set_label(lua_State *L, Tile *ptile, const char *label)
{
  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_SELF(L, ptile);
  LUASCRIPT_CHECK_ARG_NIL(L, label, 3, string);

  tile_set_label(ptile, label);
  if (server_state() >= S_S_RUNNING) {
    send_tile_info(NULL, ptile, FALSE);
  }
}

/**********************************************************************//**
  Reveal tile as it is currently to the player.
**************************************************************************/
void api_edit_tile_show(lua_State *L, Tile *ptile, Player *pplayer)
{
  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_SELF(L, ptile);
  LUASCRIPT_CHECK_ARG_NIL(L, pplayer, 3, Player);

  map_show_tile(pplayer, ptile);
}

/**********************************************************************//**
  Try to hide tile from player.
**************************************************************************/
bool api_edit_tile_hide(lua_State *L, Tile *ptile, Player *pplayer)
{
  struct city *pcity;

  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, ptile, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, pplayer, 3, Player, FALSE);

  if (map_is_known_and_seen(ptile, pplayer, V_MAIN)) {
    /* Can't hide currently seen tile */
    return FALSE;
  }

  pcity = tile_city(ptile);

  if (pcity != NULL) {
    trade_partners_iterate(pcity, partner) {
      if (really_gives_vision(pplayer, city_owner(partner))) {
        /* Can't remove vision about trade partner */
        return FALSE;
      }
    } trade_partners_iterate_end;
  }

  dbv_clr(&pplayer->tile_known, tile_index(ptile));

  send_tile_info(pplayer->connections, ptile, TRUE);

  return TRUE;
}

/**********************************************************************//**
  Global climate change.
**************************************************************************/
void api_edit_climate_change(lua_State *L, enum climate_change_type type,
                             int effect)
{
  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_ARG(L, type == CLIMATE_CHANGE_GLOBAL_WARMING
                      || type == CLIMATE_CHANGE_NUCLEAR_WINTER,
                      2, "invalid climate change type");
  LUASCRIPT_CHECK_ARG(L, effect > 0, 3, "effect must be greater than zero");

  climate_change(type == CLIMATE_CHANGE_GLOBAL_WARMING, effect);
}

/**********************************************************************//**
  Provoke a civil war.
**************************************************************************/
Player *api_edit_civil_war(lua_State *L, Player *pplayer, int probability)
{
  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_ARG_NIL(L, pplayer, 2, Player, NULL);
  LUASCRIPT_CHECK_ARG(L, probability >= 0 && probability <= 100,
                      3, "must be a percentage", NULL);

  if (!civil_war_possible(pplayer, FALSE, FALSE)) {
    return NULL;
  }

  if (probability == 0) {
    /* Calculate chance with normal rules */
    if (!civil_war_triggered(pplayer)) {
      return NULL;
    }
  } else {
    /* Fixed chance specified by script */
    if (fc_rand(100) >= probability) {
      return NULL;
    }
  }

  return civil_war(pplayer);
}

/**********************************************************************//**
  Make player winner of the scenario
**************************************************************************/
void api_edit_player_victory(lua_State *L, Player *pplayer)
{
  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_SELF(L, pplayer);

  player_status_add(pplayer, PSTATUS_WINNER);
}

/**********************************************************************//**
  Move a unit.
**************************************************************************/
bool api_edit_unit_move(lua_State *L, Unit *punit, Tile *ptile,
                        int movecost,
                        Unit *embark_to, bool disembark,
                        bool conquer_city, bool conquer_extra,
                        bool enter_hut, bool frighten_hut)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, punit, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, ptile, 3, Tile, FALSE);
  LUASCRIPT_CHECK_ARG(L, movecost >= 0, 4, "Negative move cost!", FALSE);

  LUASCRIPT_CHECK(L, !(enter_hut && frighten_hut),
                  "Can't both enter and frighten a hut at the same time",
                  TRUE);

  if (!disembark && unit_transported(punit)) {
    /* Can't leave the transport. */
    return TRUE;
  }

  if (unit_move_to_tile_test(&(wld.map), punit, ACTIVITY_IDLE,
                             unit_tile(punit), ptile, TRUE,
                             FALSE, embark_to, TRUE) != MR_OK) {
    /* Can't move to target. Return that unit is still alive. */
    return TRUE;
  }

  return unit_move(punit, ptile, movecost,
                   embark_to, embark_to != NULL,
                   conquer_city, conquer_extra,
                   enter_hut, frighten_hut);
}

/**********************************************************************//**
  Move a unit.
**************************************************************************/
bool api_edit_unit_move_old(lua_State *L, Unit *punit, Tile *ptile,
                            int movecost)
{
  struct city *pcity;

  deprecated_semantic_warning("edit.unit_move(unit, moveto, movecost)",
                              "Unit:move(moveto, movecost)", "3.1");

  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, punit, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, ptile, 3, Tile, FALSE);
  LUASCRIPT_CHECK_ARG(L, movecost >= 0, 4, "Negative move cost!", FALSE);

  return unit_move(punit, ptile, movecost,
                   /* Auto embark kept for backward compatibility. I have
                    * no objection if you see the old behavior as a bug and
                    * remove auto embarking completely or for transports
                    * the unit can't legally board. -- Sveinung */
                   NULL, TRUE,
                   /* Backwards compatibility for old scripts in rulesets
                    * and (scenario) savegames. I have no objection if you
                    * see the old behavior as a bug and remove auto
                    * conquering completely or for cities the unit can't
                    * legally conquer. -- Sveinung */
                   ((pcity = tile_city(ptile))
                    && (unit_owner(punit)->ai_common.barbarian_type
                        != ANIMAL_BARBARIAN)
                    && uclass_has_flag(unit_class_get(punit),
                                       UCF_CAN_OCCUPY_CITY)
                    && !unit_has_type_flag(punit, UTYF_CIVILIAN)
                    && pplayers_at_war(unit_owner(punit),
                                       city_owner(pcity))),
                   (extra_owner(ptile) == NULL
                    || pplayers_at_war(extra_owner(ptile),
                                       unit_owner(punit)))
                   && tile_has_claimable_base(ptile, unit_type_get(punit)),
                   /* Backwards compatibility: unit_enter_hut() would
                    * return without doing anything if the unit was
                    * HUT_NOTHING. Setting this parameter to FALSE makes
                    * sure unit_enter_hut() isn't called. */
                   unit_can_do_action_result(punit, ACTRES_HUT_ENTER),
                   unit_can_do_action_result(punit, ACTRES_HUT_FRIGHTEN));
}

/**********************************************************************//**
  Prohibit unit from moving
**************************************************************************/
void api_edit_unit_moving_disallow(lua_State *L, Unit *punit)
{
  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_SELF(L, punit);

  if (punit != NULL) {
    punit->stay = TRUE;
  }
}

/**********************************************************************//**
  Allow unit to move
**************************************************************************/
void api_edit_unit_moving_allow(lua_State *L, Unit *punit)
{
  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_SELF(L, punit);

  if (punit != NULL) {
    punit->stay = FALSE;
  }
}

/**********************************************************************//**
  Add history to a city
**************************************************************************/
void api_edit_city_add_history(lua_State *L, City *pcity, int amount)
{
  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_SELF(L, pcity);

  pcity->history += amount;
}

/**********************************************************************//**
  Add history to a player
**************************************************************************/
void api_edit_player_add_history(lua_State *L, Player *pplayer, int amount)
{
  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_SELF(L, pplayer);

  pplayer->history += amount;
}

/**********************************************************************//**
  Give bulbs to a player
**************************************************************************/
void api_edit_player_give_bulbs(lua_State *L, Player *pplayer, int amount)
{
  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_SELF(L, pplayer);

  update_bulbs(pplayer, amount, TRUE);

  send_research_info(research_get(pplayer), NULL);
}
