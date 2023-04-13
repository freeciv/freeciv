/****************************************************************************
 Freeciv - Copyright (C) 2023 The Freeciv Team
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
#include "city.h"
#include "combat.h"
#include "fc_interface.h"
#include "game.h"
#include "map.h"
#include "metaknowledge.h"
#include "movement.h"
#include "player.h"
#include "requirements.h"
#include "tile.h"
#include "traderoutes.h"

#include "actres.h"

static struct actres act_results[ACTRES_LAST] = {
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_ESTABLISH_EMBASSY */
    FALSE},
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_SPY_INVESTIGATE_CITY */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC,    /* ACTRES_SPY_POISON */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC,    /* ACTRES_SPY_STEAL_GOLD */
    TRUE},
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC,    /* ACTRES_SPY_SABOTAGE_CITY */
    TRUE },
  { ACT_TGT_COMPL_MANDATORY, ABK_DIPLOMATIC, /* ACTRES_SPY_TARGETED_SABOTAGE_CITY */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC,    /* ACTRES_SPY_SABOTAGE_CITY_PRODUCTION */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC,    /* ACTRES_SPY_STEAL_TECH */
    TRUE },
  { ACT_TGT_COMPL_MANDATORY, ABK_DIPLOMATIC, /* ACTRES_SPY_TARGETED_STEAL_TECH */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC,    /* ACTRES_SPY_INCITE_CITY */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_TRADE_ROUTE */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_MARKETPLACE */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_HELP_WONDER */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC,    /* ACTRES_SPY_BRIBE_UNIT */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC,    /* ACTRES_SPY_SABOTAGE_UNIT */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_CAPTURE_UNITS */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_FOUND_CITY */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_JOIN_CITY */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC,    /* ACTRES_STEAL_MAPS */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_BOMBARD */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC,    /* ACTRES_SPY_NUKE */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_NUKE */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_NUKE_UNITS */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_DESTROY_CITY */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_EXPEL_UNIT */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_DISBAND_UNIT_RECOVER */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_DISBAND_UNIT */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_HOME_CITY */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_UPGRADE_UNIT */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_PARADROP */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_AIRLIFT */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_STANDARD,      /* ACTRES_ATTACK */
    TRUE },
  { ACT_TGT_COMPL_MANDATORY, ABK_NONE,       /* ACTRES_STRIKE_BUILDING */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_STRIKE_PRODUCTION */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_CONQUER_CITY */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_HEAL_UNIT */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_TRANSFORM_TERRAIN */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_CULTIVATE */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_PLANT */
    FALSE },
  { ACT_TGT_COMPL_FLEXIBLE, ABK_NONE,        /* ACTRES_PILLAGE */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_FORTIFY */
    FALSE },
  { ACT_TGT_COMPL_MANDATORY, ABK_NONE,       /* ACTRES_ROAD */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_CONVERT */
    FALSE },
  { ACT_TGT_COMPL_MANDATORY, ABK_NONE,       /* ACTRES_BASE */
    FALSE },
  { ACT_TGT_COMPL_MANDATORY, ABK_NONE,       /* ACTRES_MINE */
    FALSE },
  { ACT_TGT_COMPL_MANDATORY, ABK_NONE,       /* ACTRES_IRRIGATE */
    FALSE },
  { ACT_TGT_COMPL_FLEXIBLE, ABK_NONE,        /* ACTRES_CLEAN_POLLUTION */
    FALSE },
  { ACT_TGT_COMPL_FLEXIBLE, ABK_NONE,        /* ACTRES_CLEAN_FALLOUT */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_TRANSPORT_DEBOARD */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_TRANSPORT_UNLOAD */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_TRANSPORT_DISEMBARK */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_TRANSPORT_BOARD */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_TRANSPORT_EMBARK */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC,    /* ACTRES_SPY_SPREAD_PLAGUE */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC,    /* ACTRES_SPY_ATTACK */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_CONQUER_EXTRAS */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_HUT_ENTER */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_HUT_FRIGHTEN */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_UNIT_MOVE */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_PARADROP_CONQUER */
    FALSE }, /* TODO: should this be hostile? */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_HOMELESS */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_STANDARD,      /* ACTRES_WIPE_UNITS */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_SPY_ESCAPE */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_TRANSPORT_LOAD */
    FALSE },
  { ACT_TGT_COMPL_FLEXIBLE, ABK_NONE,        /* ACTRES_CLEAN */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_TELEPORT */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_ENABLER_CHECK */
    FALSE }
};

/*********************************************************************//**
  Initialize action results system
*************************************************************************/
void actres_init(void)
{
}

/*********************************************************************//**
  Free resources allocated by the action results system
*************************************************************************/
void actres_free(void)
{
}

/**********************************************************************//**
  Returns the sub target complexity for the action with the specified
  result when it has the specified target kind and sub target kind.
**************************************************************************/
enum act_tgt_compl actres_target_compl_calc(enum action_result result)
{
  if (result == ACTRES_NONE) {
    return ACT_TGT_COMPL_SIMPLE;
  }

  fc_assert_ret_val(action_result_is_valid(result), ACT_TGT_COMPL_SIMPLE);

  return act_results[result].sub_tgt_compl;
}

/**********************************************************************//**
  Get the battle kind that can prevent an action.
**************************************************************************/
enum action_battle_kind actres_get_battle_kind(enum action_result result)
{
  if (result == ACTRES_NONE) {
    return ABK_NONE;
  }

  fc_assert_ret_val(action_result_is_valid(result), ABK_NONE);

  return act_results[result].battle_kind;
}

/**********************************************************************//**
  Returns TRUE iff the specified action result indicates hostile action.
**************************************************************************/
bool actres_is_hostile(enum action_result result)
{
  if (result == ACTRES_NONE) {
    return FALSE;
  }

  return act_results[result].hostile;
}

/**********************************************************************//**
  Returns TRUE iff the specified player knows (has seen) the specified
  tile.
**************************************************************************/
static bool plr_knows_tile(const struct player *plr,
                           const struct tile *ttile)
{
  return plr != nullptr
    && ttile != nullptr
    && (tile_get_known(ttile, plr) != TILE_UNKNOWN);
}

/**********************************************************************//**
  Could an action with this kind of result be made?
**************************************************************************/
enum fc_tristate actres_possible(enum action_result result,
                                 const struct req_context *actor,
                                 const struct req_context *target,
                                 const struct extra_type *target_extra,
                                 enum fc_tristate def,
                                 bool omniscient,
                                 const struct city *homecity)
{
  struct terrain *pterrain;
  bool can_see_tgt_unit;
  bool can_see_tgt_tile;

  /* Only check requirement against the target unit if the actor can see it
   * or if the evaluator is omniscient. The game checking the rules is
   * omniscient. The player asking about their odds isn't. */
  can_see_tgt_unit = (omniscient || (target->unit
                                     && can_player_see_unit(actor->player,
                                                            target->unit)));

  /* Only check requirement against the target tile if the actor can see it
   * or if the evaluator is omniscient. The game checking the rules is
   * omniscient. The player asking about their odds isn't. */
  can_see_tgt_tile = (omniscient
                      || can_player_see_tile(actor->player, target->tile));

  switch (result) {
  case ACTRES_CAPTURE_UNITS:
  case ACTRES_SPY_BRIBE_UNIT:
    /* Why this is a hard requirement: Can't transfer a unique unit if the
     * actor player already has one. */
    /* Info leak: This is only checked for when the actor player can see
     * the target unit. Since the target unit is seen its type is known.
     * The fact that a city hiding the unseen unit is occupied is known. */

    if (!can_see_tgt_unit) {
      /* An omniscient player can see the target unit. */
      fc_assert(!omniscient);

      return TRI_MAYBE;
    }

    if (utype_player_already_has_this_unique(actor->player,
                                             target->unittype)) {
      return TRI_NO;
    }

    /* FIXME: Capture Unit may want to look for more than one unique unit
     * of the same kind at the target tile. Currently caught by sanity
     * check in do_capture_units(). */

    break;

  case ACTRES_SPY_TARGETED_STEAL_TECH:
    /* Reason: The Freeciv code don't support selecting a target tech
     * unless it is known that the victim player has it. */
    /* Info leak: The actor player knowns whose techs they can see. */
    if (!can_see_techs_of_target(actor->player, target->player)) {
      return TRI_NO;
    }

    break;

  case ACTRES_SPY_STEAL_GOLD:
    /* If actor->unit can do the action the actor->player can see how much
     * gold target->player have. Not requiring it is therefore pointless.
     */
    if (target->player->economic.gold <= 0) {
      return TRI_NO;
    }

    break;

  case ACTRES_TRADE_ROUTE:
  case ACTRES_MARKETPLACE:
    {
      /* Checked in action_hard_reqs_actor() */
      fc_assert_ret_val(homecity != NULL, TRI_NO);

      /* Can't establish a trade route or enter the market place if the
       * cities can't trade at all. */
      /* TODO: Should this restriction (and the above restriction that the
       * actor unit must have a home city) be kept for Enter Marketplace? */
      if (!can_cities_trade(homecity, target->city)) {
        return TRI_NO;
      }

      /* There are more restrictions on establishing a trade route than on
       * entering the market place. */
      if (result == ACTRES_TRADE_ROUTE
          && !can_establish_trade_route(homecity, target->city)) {
        return TRI_NO;
      }
    }

    break;

  case ACTRES_HELP_WONDER:
  case ACTRES_DISBAND_UNIT_RECOVER:
    /* It is only possible to help the production if the production needs
     * the help. (If not it would be possible to add shields for something
     * that can't legally receive help if it is build later) */
    /* Info leak: The player knows that the production in their own city has
     * been hurried (bought or helped). The information isn't revealed when
     * asking for action probabilities since omniscient is FALSE. */
    if (!omniscient
        && !can_player_see_city_internals(actor->player, target->city)) {
      return TRI_MAYBE;
    }

    if (!(target->city->shield_stock
          < city_production_build_shield_cost(target->city))) {
      return TRI_NO;
    }

    break;

  case ACTRES_FOUND_CITY:
    if (game.scenario.prevent_new_cities) {
      /* Reason: allow scenarios to disable city founding. */
      /* Info leak: the setting is public knowledge. */
      return TRI_NO;
    }

    if (can_see_tgt_tile && tile_city(target->tile)) {
      /* Reason: a tile can have 0 or 1 cities. */
      return TRI_NO;
    }

    if (citymindist_prevents_city_on_tile(target->tile)) {
      if (omniscient) {
        /* No need to check again. */
        return TRI_NO;
      } else {
        square_iterate(&(wld.map), target->tile,
                       game.info.citymindist - 1, otile) {
          if (tile_city(otile) != NULL
              && can_player_see_tile(actor->player, otile)) {
            /* Known to be blocked by citymindist */
            return TRI_NO;
          }
        } square_iterate_end;
      }
    }

    /* The player may not have enough information to be certain. */

    if (!can_see_tgt_tile) {
      /* Need to know if target tile already has a city, has TER_NO_CITIES
       * terrain, is non native to the actor or is owned by a foreigner. */
      return TRI_MAYBE;
    }

    if (!omniscient) {
      /* The player may not have enough information to find out if
       * citymindist blocks or not. This doesn't depend on if it blocks. */
      square_iterate(&(wld.map), target->tile,
                     game.info.citymindist - 1, otile) {
        if (!can_player_see_tile(actor->player, otile)) {
          /* Could have a city that blocks via citymindist. Even if this
           * tile has TER_NO_CITIES terrain the player don't know that it
           * didn't change and had a city built on it. */
          return TRI_MAYBE;
        }
      } square_iterate_end;
    }

    break;

  case ACTRES_JOIN_CITY:
    {
      int new_pop;

      if (!omniscient
          && !player_can_see_city_externals(actor->player, target->city)) {
        return TRI_MAYBE;
      }

      new_pop = city_size_get(target->city) + unit_pop_value(actor->unit);

      if (new_pop > game.info.add_to_size_limit) {
        /* Reason: Make the add_to_size_limit setting work. */
        return TRI_NO;
      }

      if (!city_can_grow_to(target->city, new_pop)) {
        /* Reason: respect city size limits. */
        /* Info leak: when it is legal to join a foreign city is legal and
         * the EFT_SIZE_UNLIMIT effect or the EFT_SIZE_ADJ effect depends on
         * something the actor player don't have access to.
         * Example: depends on a building (like Aqueduct) that isn't
         * VisibleByOthers. */
        return TRI_NO;
      }
    }

    break;

  case ACTRES_NUKE_UNITS:
    /* Has only action specific requirements */
    break;

  case ACTRES_HOME_CITY:
    /* Reason: can't change to what is. */
    /* Info leak: The player knows their unit's current home city. */
    if (homecity != NULL && homecity->id == target->city->id) {
      /* This is already the unit's home city. */
      return TRI_NO;
    }

    {
      int slots = unit_type_get(actor->unit)->city_slots;

      if (slots > 0 && city_unit_slots_available(target->city) < slots) {
        return TRI_NO;
      }
    }

    break;

  case ACTRES_UPGRADE_UNIT:
    /* Reason: Keep the old rules. */
    /* Info leak: The player knows their unit's type. They know if they can
     * build the unit type upgraded to. If the upgrade happens in a foreign
     * city that fact may leak. This can be seen as a price for changing
     * the rules to allow upgrading in a foreign city.
     * The player knows how much gold they have. If the Upgrade_Price_Pct
     * effect depends on information they don't have that information may
     * leak. The player knows the location of their unit. They know if the
     * tile has a city and if the unit can exist there outside a transport.
     * The player knows their unit's cargo. By knowing the number and type
     * of cargo, they can predict if there will be enough room in the unit
     * upgraded to, as long as they know what unit type their unit will end
     * up as. */
    if (unit_upgrade_test(actor->unit, FALSE) != UU_OK) {
      return TRI_NO;
    }

    break;

  case ACTRES_PARADROP:
  case ACTRES_PARADROP_CONQUER:
    /* Reason: Keep the old rules. */
    /* Info leak: The player knows if they know the target tile. */
    if (!plr_knows_tile(actor->player, target->tile)) {
      return TRI_NO;
    }

    /* Reason: Keep paratroopers_range working. */
    /* Info leak: The player knows the location of the actor and of the
     * target tile. */
    if (unit_type_get(actor->unit)->paratroopers_range
        < real_map_distance(actor->tile, target->tile)) {
      return TRI_NO;
    }

    break;

  case ACTRES_AIRLIFT:
    /* Reason: Keep the old rules. */
    /* Info leak: same as test_unit_can_airlift_to() */
    switch (test_unit_can_airlift_to(omniscient ? NULL : actor->player,
                                     actor->unit, target->city)) {
    case AR_OK:
      return TRI_YES;
    case AR_OK_SRC_UNKNOWN:
    case AR_OK_DST_UNKNOWN:
      return TRI_MAYBE;
    case AR_NO_MOVES:
    case AR_WRONG_UNITTYPE:
    case AR_OCCUPIED:
    case AR_NOT_IN_CITY:
    case AR_BAD_SRC_CITY:
    case AR_BAD_DST_CITY:
    case AR_SRC_NO_FLIGHTS:
    case AR_DST_NO_FLIGHTS:
      return TRI_NO;
    }

    break;

  case ACTRES_ATTACK:
    break;

  case ACTRES_WIPE_UNITS:
    unit_list_iterate(target->tile->units, punit) {
      if (get_total_defense_power(actor->unit, punit) > 0) {
        return TRI_NO;
      }
    } unit_list_iterate_end;
    break;

  case ACTRES_CONQUER_CITY:
    /* Reason: "Conquer City" involves moving into the city. */
    if (!unit_can_move_to_tile(&(wld.map), actor->unit, target->tile,
                               FALSE, FALSE, TRUE)) {
      return TRI_NO;
    }

    break;

  case ACTRES_CONQUER_EXTRAS:
    /* Reason: "Conquer Extras" involves moving to the tile. */
    if (!unit_can_move_to_tile(&(wld.map), actor->unit, target->tile,
                               FALSE, FALSE, FALSE)) {
      return TRI_NO;
    }
    /* Reason: Must have something to claim. The more specific restriction
     * that the base must be native to the actor unit is hard coded in
     * unit_move(), in action_actor_utype_hard_reqs_ok_full(), in
     * helptext_extra(), in helptext_unit(), in do_attack() and in
     * diplomat_bribe(). */
    if (!tile_has_claimable_base(target->tile, actor->unittype)) {
      return TRI_NO;
    }
    break;

  case ACTRES_HEAL_UNIT:
    /* Reason: It is not the healthy who need a doctor, but the sick. */
    /* Info leak: the actor can see the target's HP. */
    if (!can_see_tgt_unit) {
      return TRI_MAYBE;
    }
    if (!(target->unit->hp < target->unittype->hp)) {
      return TRI_NO;
    }
    break;

  case ACTRES_TRANSFORM_TERRAIN:
    pterrain = tile_terrain(target->tile);
    if (pterrain->transform_result == T_NONE
        || pterrain == pterrain->transform_result
        || !terrain_surroundings_allow_change(target->tile,
                                              pterrain->transform_result)
        || (terrain_has_flag(pterrain->transform_result, TER_NO_CITIES)
            && (tile_city(target->tile)))) {
      return TRI_NO;
    }
    break;

  case ACTRES_CULTIVATE:
    pterrain = tile_terrain(target->tile);
    if (pterrain->cultivate_result == NULL) {
      return TRI_NO;
    }
    if (!terrain_surroundings_allow_change(target->tile,
                                           pterrain->cultivate_result)
        || (terrain_has_flag(pterrain->cultivate_result, TER_NO_CITIES)
            && tile_city(target->tile))) {
      return TRI_NO;
    }
    break;

  case ACTRES_PLANT:
    pterrain = tile_terrain(target->tile);
    if (pterrain->plant_result == NULL) {
      return TRI_NO;
    }
    if (!terrain_surroundings_allow_change(target->tile,
                                           pterrain->plant_result)
        || (terrain_has_flag(pterrain->plant_result, TER_NO_CITIES)
            && tile_city(target->tile))) {
      return TRI_NO;
    }
    break;

  case ACTRES_ROAD:
    if (target_extra == NULL) {
      return TRI_NO;
    }
    if (!is_extra_caused_by(target_extra, EC_ROAD)) {
      /* Reason: This is not a road. */
      return TRI_NO;
    }
    if (!can_build_road(extra_road_get(target_extra), actor->unit,
                        target->tile)) {
      return TRI_NO;
    }
    break;

  case ACTRES_BASE:
    if (target_extra == NULL) {
      return TRI_NO;
    }
    if (!is_extra_caused_by(target_extra, EC_BASE)) {
      /* Reason: This is not a base. */
      return TRI_NO;
    }
    if (!can_build_base(actor->unit,
                        extra_base_get(target_extra), target->tile)) {
      return TRI_NO;
    }
    break;

  case ACTRES_MINE:
    if (target_extra == NULL) {
      return TRI_NO;
    }
    if (!is_extra_caused_by(target_extra, EC_MINE)) {
      /* Reason: This is not a mine. */
      return TRI_NO;
    }

    if (!can_build_extra(target_extra, actor->unit, target->tile)) {
      return TRI_NO;
    }
    break;

  case ACTRES_IRRIGATE:
    if (target_extra == NULL) {
      return TRI_NO;
    }
    if (!is_extra_caused_by(target_extra, EC_IRRIGATION)) {
      /* Reason: This is not an irrigation. */
      return TRI_NO;
    }

    if (!can_build_extra(target_extra, actor->unit, target->tile)) {
      return TRI_NO;
    }
    break;

  case ACTRES_PILLAGE:
    pterrain = tile_terrain(target->tile);
    if (pterrain->pillage_time == 0) {
      return TRI_NO;
    }

    {
      bv_extras pspresent = get_tile_infrastructure_set(target->tile, NULL);
      bv_extras psworking = get_unit_tile_pillage_set(target->tile);
      bv_extras pspossible;

      BV_CLR_ALL(pspossible);
      extra_type_iterate(pextra) {
        int idx = extra_index(pextra);

        /* Only one unit can pillage a given improvement at a time */
        if (BV_ISSET(pspresent, idx)
            && (!BV_ISSET(psworking, idx)
                || actor->unit->activity_target == pextra)
            && can_remove_extra(pextra, actor->unit, target->tile)) {
          bool required = FALSE;

          extra_type_iterate(pdepending) {
            if (BV_ISSET(pspresent, extra_index(pdepending))) {
              extra_deps_iterate(&(pdepending->reqs), pdep) {
                if (pdep == pextra) {
                  required = TRUE;
                  break;
                }
              } extra_deps_iterate_end;
            }
            if (required) {
              break;
            }
          } extra_type_iterate_end;

          if (!required) {
            BV_SET(pspossible, idx);
          }
        }
      } extra_type_iterate_end;

      if (!BV_ISSET_ANY(pspossible)) {
        /* Nothing available to pillage */
        return TRI_NO;
      }

      if (target_extra != NULL) {
        if (!game.info.pillage_select) {
          /* Hobson's choice (this case mostly exists for old clients) */
          /* Needs to match what unit_activity_assign_target chooses */
          struct extra_type *tgt;

          tgt = get_preferred_pillage(pspossible);

          if (tgt != target_extra) {
            /* Only one target allowed, which wasn't the requested one */
            return TRI_NO;
          }
        }

        if (!BV_ISSET(pspossible, extra_index(target_extra))) {
          return TRI_NO;
        }
      }
    }
    break;

  case ACTRES_CLEAN:
    {
      const struct extra_type *pextra = NULL;

      pterrain = tile_terrain(target->tile);

      if (target_extra != NULL) {
        if (tile_has_extra(target->tile, target_extra)
            && (is_extra_removed_by(target_extra, ERM_CLEAN)
                || is_extra_removed_by(target_extra, ERM_CLEANPOLLUTION)
                || is_extra_removed_by(target_extra, ERM_CLEANFALLOUT))) {
          pextra = target_extra;
        }
      } else {
        /* TODO: Make sure that all callers set target so that
         * we don't need this fallback. */

        pextra = prev_extra_in_tile(target->tile,
                                    ERM_CLEAN,
                                    actor->player,
                                    actor->unit);

        if (pextra == NULL) {
          pextra = prev_extra_in_tile(target->tile,
                                      ERM_CLEANPOLLUTION,
                                      actor->player,
                                      actor->unit);

          if (pextra == NULL) {
            pextra = prev_extra_in_tile(target->tile,
                                        ERM_CLEANFALLOUT,
                                        actor->player,
                                        actor->unit);
          }
        }
      }

      if (pextra != NULL && pterrain->extra_removal_times[extra_index(pextra)] > 0
          && can_remove_extra(pextra, actor->unit, target->tile)) {
        return TRI_YES;
      }

      return TRI_NO;
    }

  case ACTRES_CLEAN_POLLUTION:
    {
      const struct extra_type *pextra;

      pterrain = tile_terrain(target->tile);

      if (target_extra != NULL) {
        pextra = target_extra;

        if (!is_extra_removed_by(pextra, ERM_CLEANPOLLUTION)) {
          return TRI_NO;
        }

        if (!tile_has_extra(target->tile, pextra)) {
          return TRI_NO;
        }
      } else {
        /* TODO: Make sure that all callers set target so that
         * we don't need this fallback. */
        pextra = prev_extra_in_tile(target->tile,
                                    ERM_CLEANPOLLUTION,
                                    actor->player,
                                    actor->unit);
        if (pextra == NULL) {
          /* No available pollution extras */
          return TRI_NO;
        }
      }

      if (pterrain->extra_removal_times[extra_index(pextra)] == 0) {
        return TRI_NO;
      }

      if (can_remove_extra(pextra, actor->unit, target->tile)) {
        return TRI_YES;
      }

      return TRI_NO;
    }
    break;

  case ACTRES_CLEAN_FALLOUT:
    {
      const struct extra_type *pextra;

      pterrain = tile_terrain(target->tile);

      if (target_extra != NULL) {
        pextra = target_extra;

        if (!is_extra_removed_by(pextra, ERM_CLEANFALLOUT)) {
          return TRI_NO;
        }

        if (!tile_has_extra(target->tile, pextra)) {
          return TRI_NO;
        }
      } else {
        /* TODO: Make sure that all callers set target so that
         * we don't need this fallback. */
        pextra = prev_extra_in_tile(target->tile,
                                    ERM_CLEANFALLOUT,
                                    actor->player,
                                    actor->unit);
        if (pextra == NULL) {
          /* No available fallout extras */
          return TRI_NO;
        }
      }

      if (pterrain->extra_removal_times[extra_index(pextra)] == 0) {
        return TRI_NO;
      }

      if (can_remove_extra(pextra, actor->unit, target->tile)) {
        return TRI_YES;
      }

      return TRI_NO;
    }
    break;

  case ACTRES_TRANSPORT_DEBOARD:
    if (!can_unit_unload(actor->unit, target->unit)) {
      /* Keep the old rules about Unreachable and disembarks. */
      return TRI_NO;
    }
    break;

  case ACTRES_TRANSPORT_BOARD:
    if (unit_transported(actor->unit)) {
      if (target->unit == unit_transport_get(actor->unit)) {
        /* Already inside this transport. */
        return TRI_NO;
      }
    }
    if (!could_unit_load(actor->unit, target->unit)) {
      /* Keep the old rules. */
      return TRI_NO;
    }
    break;

  case ACTRES_TRANSPORT_LOAD:
    if (unit_transported(target->unit)) {
      if (actor->unit == unit_transport_get(target->unit)) {
        /* Already transporting this unit. */
        return TRI_NO;
      }
    }
    if (!could_unit_load(target->unit, actor->unit)) {
      /* Keep the old rules. */
      return TRI_NO;
    }
    if (unit_transported(target->unit)) {
      if (!can_unit_unload(target->unit,
                           unit_transport_get(target->unit))) {
        /* Can't leave current transport. */
        return TRI_NO;
      }
    }
    break;

  case ACTRES_TRANSPORT_UNLOAD:
    if (!can_unit_unload(target->unit, actor->unit)) {
      /* Keep the old rules about Unreachable and disembarks. */
      return TRI_NO;
    }
    break;

  case ACTRES_TRANSPORT_DISEMBARK:
    if (!unit_can_move_to_tile(&(wld.map), actor->unit, target->tile,
                               FALSE, FALSE, FALSE)) {
      /* Reason: involves moving to the tile. */
      return TRI_NO;
    }

    /* We cannot move a transport into a tile that holds
     * units or cities not allied with all of our cargo. */
    if (get_transporter_capacity(actor->unit) > 0) {
      unit_list_iterate(unit_tile(actor->unit)->units, pcargo) {
        if (unit_contained_in(pcargo, actor->unit)
            && (is_non_allied_unit_tile(target->tile, unit_owner(pcargo))
                || is_non_allied_city_tile(target->tile,
                                           unit_owner(pcargo)))) {
           return TRI_NO;
        }
      } unit_list_iterate_end;
    }
    break;

  case ACTRES_TRANSPORT_EMBARK:
    if (unit_transported(actor->unit)) {
      if (target->unit == unit_transport_get(actor->unit)) {
        /* Already inside this transport. */
        return TRI_NO;
      }
    }
    if (!could_unit_load(actor->unit, target->unit)) {
      /* Keep the old rules. */
      return TRI_NO;
    }
    if (!unit_can_move_to_tile(&(wld.map), actor->unit, target->tile,
                               FALSE, TRUE, FALSE)) {
      /* Reason: involves moving to the tile. */
      return TRI_NO;
    }
    /* We cannot move a transport into a tile that holds
     * units or cities not allied with all of our cargo. */
    if (get_transporter_capacity(actor->unit) > 0) {
      unit_list_iterate(unit_tile(actor->unit)->units, pcargo) {
        if (unit_contained_in(pcargo, actor->unit)
            && (is_non_allied_unit_tile(target->tile, unit_owner(pcargo))
                || is_non_allied_city_tile(target->tile,
                                           unit_owner(pcargo)))) {
           return TRI_NO;
        }
      } unit_list_iterate_end;
    }
    break;

  case ACTRES_SPY_ATTACK:
    {
      bool found;

      if (!omniscient
          && !can_player_see_hypotetic_units_at(actor->player,
                                                target->tile)) {
        /* May have a hidden diplomatic defender. */
        return TRI_MAYBE;
      }

      found = FALSE;
      unit_list_iterate(target->tile->units, punit) {
        struct player *uplayer = unit_owner(punit);

        if (uplayer == actor->player) {
          /* Won't defend against its owner. */
          continue;
        }

        if (unit_has_type_flag(punit, UTYF_SUPERSPY)) {
          /* This unbeatable diplomatic defender will defend before any
           * that can be beaten. */
          found = FALSE;
          break;
        }

        if (unit_has_type_flag(punit, UTYF_DIPLOMAT)) {
          /* Found a beatable diplomatic defender. */
          found = TRUE;
          break;
        }
      } unit_list_iterate_end;

      if (!found) {
        return TRI_NO;
      }
    }
    break;

  case ACTRES_HUT_ENTER:
  case ACTRES_HUT_FRIGHTEN:
    /* Reason: involves moving to the tile. */
    if (!unit_can_move_to_tile(&(wld.map), actor->unit, target->tile,
                               FALSE, FALSE, FALSE)) {
      return TRI_NO;
    }
    if (!unit_can_displace_hut(actor->unit, target->tile)) {
      /* Reason: keep extra rmreqs working. */
      return TRI_NO;
    }
    break;

  case ACTRES_UNIT_MOVE:
  case ACTRES_TELEPORT:

    if (result == ACTRES_UNIT_MOVE) {
      /* Reason: is moving to the tile. */
      if (!unit_can_move_to_tile(&(wld.map), actor->unit, target->tile,
                                 FALSE, FALSE, FALSE)) {
        return TRI_NO;
      }
    } else {
      fc_assert(result == ACTRES_TELEPORT);

      /* Reason: is teleporting to the tile. */
      if (!unit_can_teleport_to_tile(&(wld.map), actor->unit, target->tile,
                                     FALSE, FALSE)) {
        return TRI_NO;
      }
    }

    /* Reason: Don't override "Transport Embark" */
    if (!can_unit_exist_at_tile(&(wld.map), actor->unit, target->tile)) {
      return TRI_NO;
    }

    /* We cannot move a transport into a tile that holds
     * units or cities not allied with all of our cargo. */
    if (get_transporter_capacity(actor->unit) > 0) {
      unit_list_iterate(unit_tile(actor->unit)->units, pcargo) {
        if (unit_contained_in(pcargo, actor->unit)
            && (is_non_allied_unit_tile(target->tile, unit_owner(pcargo))
                || is_non_allied_city_tile(target->tile,
                                           unit_owner(pcargo)))) {
           return TRI_NO;
        }
      } unit_list_iterate_end;
    }
    break;

  case ACTRES_SPY_ESCAPE:
    /* Reason: Be merciful. */
    /* Info leak: The player know if they have any cities. */
    if (city_list_size(actor->player->cities) < 1) {
      return TRI_NO;
    }
    break;

  case ACTRES_SPY_SPREAD_PLAGUE:
    /* Enabling this action with illness_on = FALSE prevents spread. */
    break;
  case ACTRES_BOMBARD:
    {
      /* Allow bombing only if there's reachable units in the target
       * tile. Bombing without anybody taking damage is certainly not
       * what user really wants.
       * Allow bombing when player does not know if there's units in
       * target tile. */
      if (tile_city(target->tile) == NULL
          && fc_funcs->player_tile_vision_get(target->tile, actor->player,
                                              V_MAIN)
          && fc_funcs->player_tile_vision_get(target->tile, actor->player,
                                              V_INVIS)
          && fc_funcs->player_tile_vision_get(target->tile, actor->player,
                                              V_SUBSURFACE)) {
        bool hiding_extra = FALSE;

        extra_type_iterate(pextra) {
          if (pextra->eus == EUS_HIDDEN
              && tile_has_extra(target->tile, pextra)) {
            hiding_extra = TRUE;
            break;
          }
        } extra_type_iterate_end;

        if (!hiding_extra) {
          bool has_target = FALSE;

          unit_list_iterate(target->tile->units, punit) {
            if (is_unit_reachable_at(punit, actor->unit, target->tile)) {
              has_target = TRUE;
              break;
            }
          } unit_list_iterate_end;

          if (!has_target) {
            return TRI_NO;
          }
        }
      }
    }
    break;
  case ACTRES_ESTABLISH_EMBASSY:
  case ACTRES_SPY_INVESTIGATE_CITY:
  case ACTRES_SPY_POISON:
  case ACTRES_SPY_SABOTAGE_CITY:
  case ACTRES_SPY_TARGETED_SABOTAGE_CITY:
  case ACTRES_SPY_SABOTAGE_CITY_PRODUCTION:
  case ACTRES_SPY_STEAL_TECH:
  case ACTRES_SPY_INCITE_CITY:
  case ACTRES_SPY_SABOTAGE_UNIT:
  case ACTRES_STEAL_MAPS:
  case ACTRES_SPY_NUKE:
  case ACTRES_NUKE:
  case ACTRES_DESTROY_CITY:
  case ACTRES_EXPEL_UNIT:
  case ACTRES_DISBAND_UNIT:
  case ACTRES_CONVERT:
  case ACTRES_STRIKE_BUILDING:
  case ACTRES_STRIKE_PRODUCTION:
  case ACTRES_FORTIFY:
  case ACTRES_HOMELESS:
  case ACTRES_ENABLER_CHECK:
  case ACTRES_NONE:
    /* No known hard coded requirements. */
    break;
  }

  return def;
}
