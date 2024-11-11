/***********************************************************************
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* dependencies/lua */
#include "lua.h" /* lua_Integer */

/* utility */
#include "astring.h"
#include "capability.h"
#include "fcintl.h"
#include "mem.h"
#include "rand.h"
#include "shared.h"

/* common */
#include "actions.h"
#include "ai.h"
#include "city.h"
#include "combat.h"
#include "events.h"
#include "featured_text.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "movement.h"
#include "packets.h"
#include "player.h"
#include "research.h"
#include "specialist.h"
#include "traderoutes.h"
#include "unit.h"
#include "unitlist.h"

/* common/scriptcore */
#include "luascript_types.h"

/* server */
#include "actiontools.h"
#include "barbarian.h"
#include "citizenshand.h"
#include "citytools.h"
#include "cityturn.h"
#include "diplomats.h"
#include "maphand.h"
#include "notify.h"
#include "plrhand.h"
#include "sanitycheck.h"
#include "spacerace.h"
#include "srv_main.h"
#include "techtools.h"
#include "unittools.h"

/* server/advisors */
#include "autoexplorer.h"
#include "autosettlers.h"

/* server/scripting */
#include "script_server.h"

#include "unithand.h"

/* An explanation why an action isn't enabled. */
struct ane_expl {
  /* The kind of reason why an action isn't enabled. */
  enum ane_kind kind;

  union {
    /* The city without the needed capacity- */
    struct city *capacity_city;

    /* The bad terrain in question. */
    struct terrain *no_act_terrain;

    /* The player to advice declaring war on. */
    struct player *no_war_with;

    /* The player to advice breaking peace with. */
    struct player *peace_with;

    /* The nation that can't be involved. */
    struct nation_type *no_act_nation;

    /* The unit type that can't be targeted. */
    const struct unit_type *no_tgt_utype;

    /* The action that blocks the action. */
    struct action *blocker;

    /* The required distance. */
    int distance;

    /* The required amount of gold. */
    int gold_needed;
  };
};

static bool unit_activity_internal(struct unit *punit,
                                   enum unit_activity new_activity);
static bool unit_activity_targeted_internal(struct unit *punit,
                                            enum unit_activity new_activity,
                                            struct extra_type **new_target);
static void illegal_action(struct player *pplayer,
                           struct unit *actor,
                           action_id stopped_action,
                           struct player *tgt_player,
                           struct tile *target_tile,
                           const struct city *target_city,
                           const struct unit *target_unit,
                           int request_kind,
                           const enum action_requester requester);
static bool city_add_unit(struct player *pplayer, struct unit *punit,
                          struct city *pcity, const struct action *paction);
static bool city_build(struct player *pplayer, struct unit *punit,
                       struct tile *ptile, const char *name,
                       const struct action *paction);
static bool do_unit_establish_trade(struct player *pplayer,
                                    struct unit *punit,
                                    struct city *pcity_dest,
                                    const struct action *paction);

static bool unit_do_help_build(struct player *pplayer,
                               struct unit *punit,
                               struct city *pcity_dest,
                               const struct action *paction);
static bool unit_do_regular_move(struct player *actor_player,
                                 struct unit *actor_unit,
                                 struct tile *target_tile,
                                 const struct action *paction);
static bool unit_bombard(struct unit *punit, struct tile *ptile,
                         const struct action *paction);
static bool unit_nuke(struct player *pplayer, struct unit *punit,
                      struct tile *def_tile,
                      const struct action *paction);
static bool unit_do_destroy_city(struct player *act_player,
                                 struct unit *act_unit,
                                 struct city *tgt_city,
                                 const struct action *paction);
static bool do_unit_change_homecity(struct unit *punit,
                                    struct city *pcity,
                                    const struct action *paction);
static bool do_attack(struct unit *actor_unit, struct tile *target_tile,
                      const struct action *paction);
static bool do_unit_strike_city_production(const struct player *act_player,
                                           struct unit *act_unit,
                                           struct city *tgt_city,
                                           const struct action *paction);
static bool do_unit_strike_city_building(const struct player *act_player,
                                         struct unit *act_unit,
                                         struct city *tgt_city,
                                         Impr_type_id tgt_bld_id,
                                         const struct action *paction);
static bool do_unit_conquer_city(struct player *act_player,
                                 struct unit *act_unit,
                                 struct city *tgt_city,
                                 struct action *paction);
static bool do_action_activity(struct unit *punit,
                               const struct action *paction);
static bool do_action_activity_targeted(struct unit *punit,
                                        const struct action *paction,
                                        struct extra_type **new_target);
static inline bool
non_allied_not_listed_at(const struct player *pplayer,
                         const int *list, int n, const struct tile *ptile);

/**********************************************************************//**
  Upgrade all units of a given type.
**************************************************************************/
void handle_unit_type_upgrade(struct player *pplayer, Unit_type_id uti)
{
  const struct unit_type *to_unittype;
  struct unit_type *from_unittype = utype_by_number(uti);
  int number_of_upgraded_units = 0;
  struct action *paction = action_by_number(ACTION_UPGRADE_UNIT);
  const struct civ_map *nmap = &(wld.map);

  if (NULL == from_unittype) {
    /* Probably died or bribed. */
    log_verbose("handle_unit_type_upgrade() invalid unit type %d", uti);
    return;
  }

  to_unittype = can_upgrade_unittype(pplayer, from_unittype);
  if (!to_unittype) {
    notify_player(pplayer, NULL, E_BAD_COMMAND, ftc_server,
                  _("Illegal packet, can't upgrade %s (yet)."),
                  utype_name_translation(from_unittype));
    return;
  }

  /* 
   * Try to upgrade units. The order we upgrade in is arbitrary (if
   * the player really cared they should have done it manually). 
   */
  conn_list_do_buffer(pplayer->connections);
  unit_list_iterate(pplayer->units, punit) {
    if (unit_type_get(punit) == from_unittype) {
      struct city *pcity = tile_city(unit_tile(punit));

      if (is_action_enabled_unit_on_city(nmap, paction->id, punit, pcity)
          && unit_perform_action(pplayer, punit->id, pcity->id, 0, "",
                                 paction->id, ACT_REQ_SS_AGENT)) {
        number_of_upgraded_units++;
      } else if (UU_NO_MONEY == unit_upgrade_test(nmap, punit, FALSE)) {
        break;
      }
    }
  } unit_list_iterate_end;
  conn_list_do_unbuffer(pplayer->connections);

  /* Alert the player about what happened. */
  if (number_of_upgraded_units > 0) {
    const int cost = unit_upgrade_price(pplayer, from_unittype, to_unittype);
    notify_player(pplayer, NULL, E_UNIT_UPGRADED, ftc_server,
                  /* FIXME: plurality of number_of_upgraded_units ignored!
                   * (Plurality of unit names is messed up anyway.) */
                  /* TRANS: "2 Musketeers upgraded to Riflemen for 100 gold."
                   * Plurality is in gold (second %d), not units. */
                  PL_("%d %s upgraded to %s for %d gold.",
                      "%d %s upgraded to %s for %d gold.",
                      cost * number_of_upgraded_units),
                  number_of_upgraded_units,
                  utype_name_translation(from_unittype),
                  utype_name_translation(to_unittype),
                  cost * number_of_upgraded_units);
    send_player_info_c(pplayer, pplayer->connections);
  } else {
    notify_player(pplayer, NULL, E_UNIT_UPGRADED, ftc_server,
                  _("No units could be upgraded."));
  }
}

/**********************************************************************//**
  Upgrade the unit to a newer unit type.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
**************************************************************************/
static bool do_unit_upgrade(struct player *pplayer,
                            struct unit *punit, struct city *pcity,
                            enum action_requester ordered_by,
                            const struct action *paction)
{
  const struct unit_type *from_unit = unit_type_get(punit);
  const struct unit_type *to_unit = can_upgrade_unittype(pplayer, from_unit);
  int cost = unit_upgrade_price(pplayer, from_unit, to_unit);

  transform_unit(punit, to_unit, game.server.upgrade_veteran_loss);
  pplayer->economic.gold -= cost;
  send_player_info_c(pplayer, pplayer->connections);

  if (ordered_by == ACT_REQ_PLAYER) {
    notify_player(pplayer, unit_tile(punit), E_UNIT_UPGRADED, ftc_server,
                  PL_("%s upgraded to %s for %d gold.",
                      "%s upgraded to %s for %d gold.", cost),
                  utype_name_translation(from_unit),
                  unit_link(punit),
                  cost);
  }

  return TRUE;
}

/**********************************************************************//**
  Helper function for do_capture_units(). Tells if ptile contains
  a unit not allied to pplayer whose id is not on the list.
**************************************************************************/
static inline bool
non_allied_not_listed_at(const struct player *pplayer,
                         const int *list, int n, const struct tile *ptile)
{
  unit_list_iterate(ptile->units, punit) {
    if (!pplayers_allied(pplayer, unit_owner(punit))) {
      bool listed = FALSE;
      int id = punit->id;
      int i;

      for (i = 0; i < n; i++) {
        if (id == list[i]) {
          listed = TRUE;
          break;
        }
      }
      if (!listed) {
        return TRUE;
      }
    }
  } unit_list_iterate_end;
  return FALSE;
}

/**********************************************************************//**
  Capture all the units at pdesttile using punit.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
**************************************************************************/
static bool do_capture_units(struct player *pplayer,
                             struct unit *punit,
                             struct tile *pdesttile,
                             const struct action *paction)
{
  struct city *pcity;
  char capturer_link[MAX_LEN_LINK];
  char hcity_name[MAX_LEN_NAME] = {'\0'};
  const char *capturer_nation = nation_plural_for_player(pplayer);
  bv_unit_types unique_on_tile;
  const struct unit_type *act_utype;
  int id, hcity;
  int n = 0, capt[unit_list_size(pdesttile->units)];
  bool lost_with_city = FALSE;
  int i;

  /* Sanity check: The actor still exists. */
  fc_assert_ret_val(pplayer, FALSE);
  fc_assert_ret_val(punit, FALSE);
  id = punit->id;

  act_utype = unit_type_get(punit);

  /* Sanity check: make sure that the capture won't result in the actor
   * ending up with more than one unit of each unique unit type. */
  BV_CLR_ALL(unique_on_tile);
  unit_list_iterate(pdesttile->units, to_capture) {
    bool unique_conflict = FALSE;

    /* Check what the player already has. */
    if (utype_player_already_has_this_unique(pplayer,
                                             unit_type_get(to_capture))) {
      /* The player already has a unit of this kind. */
      unique_conflict = TRUE;
    }

    if (utype_has_flag(unit_type_get(to_capture), UTYF_UNIQUE)) {
      /* The type of the units at the tile must also be checked. Two allied
       * players can both have their unique unit at the same tile.
       * Capturing them both would give the actor two units of a kind that
       * is supposed to be unique. */

      if (BV_ISSET(unique_on_tile, utype_index(unit_type_get(to_capture)))) {
        /* There is another unit of the same kind at this tile. */
        unique_conflict = TRUE;
      } else {
        /* Remember the unit type in case another unit of the same kind is
         * encountered later. */
        BV_SET(unique_on_tile, utype_index(unit_type_get(to_capture)));
      }
    }

    if (unique_conflict) {
      log_debug("capture units: already got unique unit");
      notify_player(pplayer, pdesttile, E_UNIT_ILLEGAL_ACTION, ftc_server,
                    /* TRANS: You can only have one Leader. */
                    _("You can only have one %s."),
                    unit_link(to_capture));

      return FALSE;
    }
    /* Remember the units here
     * for the mess callbacks may do in the process of transferring */
     capt[n++] = to_capture->id;
  } unit_list_iterate_end;

  /* N.B: unit_link() always returns the same pointer. */
  sz_strlcpy(capturer_link, unit_link(punit));

  pcity = tile_city(pdesttile);
  hcity = game.server.homecaughtunits
    ? punit->homecity : IDENTITY_NUMBER_ZERO;
  if (hcity) {
    /* Rarely, we'll need it... */
    sz_strlcpy(hcity_name, city_name_get(game_city_by_number(hcity)));
  }

  for (i = 0; i < n; i++) {
    struct unit *to_capture = game_unit_by_number(capt[i]);
    struct player *uplayer;
    const char *victim_link;
    const struct unit_type *utype;
    struct tile *ptile = NULL;
    bool really_lost = FALSE;

    if (!to_capture) {
      continue;
    }
    uplayer = unit_owner(to_capture);
    if (uplayer == pplayer) {
      /* Somehow transferred by scripts (e.g. diplomat incited a city) */
      continue;
    }
    utype = unit_type_get(to_capture);
    really_lost = lost_with_city && !utype_has_flag(utype, UTYF_NOHOME);
    uplayer->score.units_lost++;
    if (!really_lost) {
      /* A hack: if the captured unit is lost with a capturer's city,
       * we link the old unit, otherwise the new one */
      to_capture = unit_change_owner(to_capture, pplayer,
                                     hcity, ULR_CAPTURED);
    }
    if (!to_capture) {
      /* Lost during capturing */
      victim_link = utype_name_translation(utype);
    } else {
      /* As unit_change_owner() currently remove the old unit and
       * replace by a new one (with a new id), we want to make link to
       * the new unit. */
      victim_link = unit_link(to_capture);
      ptile = unit_tile(to_capture);
      /* Notify capturer only if there is a gain */
      notify_player(pplayer, pdesttile, E_MY_DIPLOMAT_BRIBE, ftc_server,
                    /* TRANS: <unit> ... <unit> */
                    _("Your %s succeeded in capturing the %s %s."),
                    capturer_link, nation_adjective_for_player(uplayer),
                    victim_link);
    }

    /* Notify loser */
    notify_player(uplayer, pdesttile,
                  E_ENEMY_DIPLOMAT_BRIBE, ftc_server,
                  /* TRANS: <unit> ... <Poles> */
                  _("Your %s was captured by the %s."),
                  victim_link, capturer_nation);

    /* May cause an incident */
    action_consequence_success(paction, pplayer, act_utype, uplayer,
                               pdesttile, victim_link);

    if (really_lost) {
      /* The city for which the unit was captured has perished! */
      /* Nobody actually gets the unit. */
      pplayer->score.units_lost++;
      notify_player(pplayer, pdesttile,
                    E_UNIT_LOST_MISC, ftc_server,
                    _("%s lost along with control of %s."),
                    victim_link, hcity_name);
      /* As in unit_change_owner(), don't say pplayer is killer */
      wipe_unit(to_capture, ULR_CAPTURED, NULL);
      continue;
    }

    if (to_capture
        && (NULL != pcity /* Keep old behavior */
            || is_non_allied_city_tile(ptile, unit_owner(to_capture))
            || (unit_owner(to_capture) == pplayer
                ? non_allied_not_listed_at(pplayer, capt + (i + 1),
                                           n - (i + 1), ptile)
                : (bool)
                  is_non_allied_unit_tile(ptile, unit_owner(to_capture))))) {
      /* The captured unit is in a city or with a foreign unit
       * that its owner is not capturing. Bounce it. */
      bounce_unit(to_capture, TRUE);
    }

    /* Check if the city we are going to home units in stays. */
    if (hcity && i + 1 < n && !player_city_by_number(pplayer, hcity)) {
      /* Oops, it's lost. Maybe the capturer is rehomed? */
      if (player_unit_by_number(pplayer, id)) {
        /* Well, it's natural to home them here now */
        hcity = punit->homecity;
      } else {
        /* Removing the rest of the stack (except "NoHome" units) */
        lost_with_city = TRUE;
      }
    }
  }

  if (!unit_is_alive(id)) {
    /* Callbacks took the capturer, nothing more to do */
    return TRUE;
  }
  unit_did_action(punit);
  unit_forget_last_activity(punit);

  send_unit_info(NULL, punit);

  return TRUE;
}

/**********************************************************************//**
  Expel the target unit to its owner's capital.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
**************************************************************************/
static bool do_expel_unit(struct player *pplayer,
                          struct unit *actor,
                          struct unit *target,
                          const struct action *paction)
{
  char target_link[MAX_LEN_LINK];
  struct player *uplayer;
  struct tile *target_tile;
  struct city *pcity;
  const struct unit_type *act_utype;

  /* Maybe it didn't survive the Lua call back. Why wasn't this caught by
   * the caller? Check in the code that emits the signal. */
  fc_assert_ret_val(target, FALSE);

  uplayer = unit_owner(target);

  /* A unit is supposed to have an owner. */
  fc_assert_ret_val(uplayer, FALSE);

  /* Maybe it didn't survive the Lua call back. Why wasn't this caught by
   * the caller? Check in the code that emits the signal. */
  fc_assert_ret_val(actor, FALSE);
  act_utype = unit_type_get(actor);

  /* Where is the actor player? */
  fc_assert_ret_val(pplayer, FALSE);

  target_tile = unit_tile(target);

  /* Expel the target unit to its owner's primary capital. */
  /* TODO: Could be also nearest secondary capital */
  pcity = player_primary_capital(uplayer);

  /* N.B: unit_link() always returns the same pointer. */
  sz_strlcpy(target_link, unit_link(target));

  if (pcity == NULL) {
    /* No where to send the expelled unit. */

    /* The price of failing an expulsion is a single move. */
    actor->moves_left = MAX(0, actor->moves_left - SINGLE_MOVE);
    send_unit_info(NULL, actor);

    /* Notify the actor player. */
    notify_player(pplayer, target_tile, E_UNIT_ACTION_FAILED, ftc_server,
                  /* TRANS: <Poles> <Spy> */
                  _("The %s don't have a capital to expel their %s to."),
                  nation_plural_for_player(uplayer), target_link);

    /* Nothing more could be done. */
    return FALSE;
  }

  /* Please review the code below and above (including the strings sent to
   * the players) before allowing expulsion to non capital cities. */
  fc_assert(is_capital(pcity));

  /* Notify everybody involved. */
  notify_player(pplayer, target_tile, E_UNIT_DID_EXPEL, ftc_server,
                /* TRANS: <Border Patrol> ... <Spy> */
                _("Your %s succeeded in expelling the %s %s."),
                unit_link(actor), nation_adjective_for_player(uplayer),
                target_link);
  notify_player(uplayer, target_tile, E_UNIT_WAS_EXPELLED, ftc_server,
                /* TRANS: <unit> ... <Poles> */
                _("Your %s was expelled by the %s."),
                target_link, nation_plural_for_player(pplayer));

  /* Being expelled destroys all remaining movement. */
  if (!teleport_unit_to_city(target, pcity, 0, FALSE)) {
    log_error("Bug in unit expulsion: unit can't teleport.");

    return FALSE;
  }

  /* This may cause a diplomatic incident */
  action_consequence_success(paction, pplayer, act_utype, uplayer,
                             target_tile, target_link);

  /* Mission accomplished. */
  return TRUE;
}

/**********************************************************************//**
  Claim all ownable extras at tgt_tile.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
**************************************************************************/
static bool do_conquer_extras(struct player *act_player,
                              struct unit *act_unit,
                              struct tile *tgt_tile,
                              const struct action *paction)
{
  bool success;
  const struct unit_type *act_utype;
  int move_cost = map_move_cost_unit(&(wld.map), act_unit, tgt_tile);
  struct player *tgt_player = extra_owner(tgt_tile);

  /* Sanity check */
  fc_assert_ret_val(act_unit, FALSE);
  fc_assert_ret_val(tgt_tile, FALSE);

  act_utype = unit_type_get(act_unit);

  unit_move(act_unit, tgt_tile, move_cost,
            NULL, BV_ISSET(paction->sub_results, ACT_SUB_RES_MAY_EMBARK),
            FALSE, TRUE,
            BV_ISSET(paction->sub_results, ACT_SUB_RES_HUT_ENTER),
            BV_ISSET(paction->sub_results, ACT_SUB_RES_HUT_FRIGHTEN));

  success = extra_owner(tgt_tile) == act_player;

  if (success) {
    /* May cause an incident */
    action_consequence_success(paction, act_player, act_utype,
                               tgt_player, tgt_tile,
                               tile_link(tgt_tile));
  }

  return success;
}

/**********************************************************************//**
  Restore some of the target unit's hit points.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
**************************************************************************/
static bool do_heal_unit(struct player *act_player,
                         struct unit *act_unit,
                         struct unit *tgt_unit,
                         const struct action *paction)
{
  int healing_limit;
  int tgt_hp_max;
  struct player *tgt_player;
  struct tile *tgt_tile;
  char act_unit_link[MAX_LEN_LINK];
  char tgt_unit_link[MAX_LEN_LINK];
  const char *tgt_unit_owner;
  const struct unit_type *act_utype;

  /* Sanity checks: got all the needed input. */
  fc_assert_ret_val(act_player, FALSE);
  fc_assert_ret_val(act_unit, FALSE);
  fc_assert_ret_val(tgt_unit, FALSE);

  act_utype = unit_type_get(act_unit);

  /* The target unit can't have more HP than this. */
  tgt_hp_max = unit_type_get(tgt_unit)->hp;

  /* Sanity check: target isn't at full health and can therefore can be
   * healed. */
  fc_assert_ret_val(tgt_unit->hp < tgt_hp_max, FALSE);

  /* Fetch the target unit's owner. */
  tgt_player = unit_owner(tgt_unit);
  fc_assert_ret_val(tgt_player, FALSE);

  /* Fetch the target unit's tile. */
  tgt_tile = unit_tile(tgt_unit);
  fc_assert_ret_val(tgt_tile, FALSE);

  /* The max amount of HP that can be added. */
  healing_limit = ((get_target_bonus_effects(
                      NULL,
                      &(const struct req_context) {
                        .player = unit_owner(act_unit),
                        .city = tile_city(unit_tile(act_unit)),
                        .tile = unit_tile(act_unit),
                        .unit = act_unit,
                        .unittype = unit_type_get(act_unit),
                        .action = paction,
                      },
                      unit_owner(tgt_unit),
                      EFT_HEAL_UNIT_PCT
                    ) + 100)
                   * tgt_hp_max) / 100;

  /* Heal the target unit. */
  tgt_unit->hp = MIN(tgt_unit->hp + healing_limit, tgt_hp_max);
  send_unit_info(NULL, tgt_unit);

  send_unit_info(NULL, act_unit);

  /* Every call to unit_link() overwrites the previous. Two units are being
   * linked to. */
  sz_strlcpy(act_unit_link, unit_link(act_unit));
  sz_strlcpy(tgt_unit_link, unit_link(tgt_unit));

  /* Notify everybody involved. */
  if (act_player == tgt_player) {
    /* TRANS: used instead of nation adjective when the nation is
     * domestic. */
    tgt_unit_owner = _("your");
  } else {
    tgt_unit_owner = nation_adjective_for_player(unit_nationality(tgt_unit));
  }

  notify_player(act_player, tgt_tile, E_MY_UNIT_DID_HEAL, ftc_server,
                /* TRANS: If foreign: Your Leader heals Finnish Warrior.
                 * If domestic: Your Leader heals your Warrior. */
                _("Your %s heals %s %s."),
                act_unit_link, tgt_unit_owner, tgt_unit_link);

  if (act_player != tgt_player) {
    notify_player(tgt_player, tgt_tile, E_MY_UNIT_WAS_HEALED, ftc_server,
                  /* TRANS: Norwegian ... Leader ... Warrior */
                  _("%s %s heals your %s."),
                  nation_adjective_for_player(unit_nationality(act_unit)),
                  act_unit_link, tgt_unit_link);
  }

  /* This may have diplomatic consequences. */
  action_consequence_success(paction, act_player, act_utype, tgt_player,
                             tgt_tile, unit_link(tgt_unit));

  return TRUE;
}

/**********************************************************************//**
  Unload actor unit from target unit.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
**************************************************************************/
static bool do_unit_alight(struct player *act_player,
                           struct unit *act_unit,
                           struct unit *tgt_unit,
                           const struct action *paction)
{
  /* Unload the unit and send out info to clients. */
  unit_transport_unload_send(act_unit);

  return TRUE;
}

/**********************************************************************//**
  Have the actor unit board the target unit.

  Assumes that all checks for action legality has been done.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
**************************************************************************/
static bool do_unit_board(struct player *act_player,
                          struct unit *act_unit,
                          struct unit *tgt_unit,
                          const struct action *paction)
{
  if (unit_transported(act_unit)) {
    unit_transport_unload(act_unit);
  }

  /* Load the unit and send out info to clients. */
  unit_transport_load_send(act_unit, tgt_unit);

  return TRUE;
}

/**********************************************************************//**
  Unload target unit from actor unit.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
**************************************************************************/
static bool do_unit_unload(struct player *act_player,
                           struct unit *act_unit,
                           struct unit *tgt_unit,
                           const struct action *paction)
{
  /* Unload the unit and send out info to clients. */
  unit_transport_unload_send(tgt_unit);

  return TRUE;
}

/**********************************************************************//**
  Disembark actor unit from target unit to target tile.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
**************************************************************************/
static bool do_disembark(struct player *act_player,
                         struct unit *act_unit,
                         struct tile *tgt_tile,
                         const struct action *paction)
{
  int move_cost = map_move_cost_unit(&(wld.map), act_unit, tgt_tile);

  /* Sanity checks */
  fc_assert_ret_val(act_player, FALSE);
  fc_assert_ret_val(act_unit, FALSE);
  fc_assert_ret_val(tgt_tile, FALSE);
  fc_assert_ret_val(paction, FALSE);

  unit_move(act_unit, tgt_tile, move_cost,
            NULL, BV_ISSET(paction->sub_results, ACT_SUB_RES_MAY_EMBARK),
            FALSE, FALSE,
            BV_ISSET(paction->sub_results, ACT_SUB_RES_HUT_ENTER),
            BV_ISSET(paction->sub_results, ACT_SUB_RES_HUT_FRIGHTEN));

  return TRUE;
}

/**********************************************************************//**
  Enter a hut at the target tile.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
**************************************************************************/
static bool do_unit_hut(struct player *act_player,
                        struct unit *act_unit,
                        struct tile *tgt_tile,
                        const struct action *paction)
{
  int move_cost = map_move_cost_unit(&(wld.map), act_unit, tgt_tile);

  /* Sanity checks */
  fc_assert_ret_val(act_player, FALSE);
  fc_assert_ret_val(act_unit, FALSE);
  fc_assert_ret_val(tgt_tile, FALSE);
  fc_assert_ret_val(paction, FALSE);

  unit_move(act_unit, tgt_tile, move_cost,
            NULL, BV_ISSET(paction->sub_results, ACT_SUB_RES_MAY_EMBARK),
            FALSE, FALSE,
            BV_ISSET(paction->sub_results, ACT_SUB_RES_HUT_ENTER),
            BV_ISSET(paction->sub_results, ACT_SUB_RES_HUT_FRIGHTEN));

  return TRUE;
}

/**********************************************************************//**
  Have the actor unit embark the target unit.

  Assumes that all checks for action legality has been done.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
**************************************************************************/
static bool do_unit_embark(struct player *act_player,
                           struct unit *act_unit,
                           struct unit *tgt_unit,
                           const struct action *paction)
{
  struct tile *tgt_tile;
  int move_cost;

  /* Sanity checks */
  fc_assert_ret_val(act_player, FALSE);
  fc_assert_ret_val(act_unit, FALSE);
  fc_assert_ret_val(tgt_unit, FALSE);
  fc_assert_ret_val(paction, FALSE);

  if (unit_transported(act_unit)) {
    /* Assumed to be legal. */
    unit_transport_unload(act_unit);
  }

  /* Do it. */
  tgt_tile = unit_tile(tgt_unit);
  move_cost = map_move_cost_unit(&(wld.map), act_unit, tgt_tile);
  unit_move(act_unit, tgt_tile, move_cost,
            tgt_unit, BV_ISSET(paction->sub_results,
                               ACT_SUB_RES_MAY_EMBARK),
            FALSE, FALSE,
            BV_ISSET(paction->sub_results, ACT_SUB_RES_HUT_ENTER),
            BV_ISSET(paction->sub_results, ACT_SUB_RES_HUT_FRIGHTEN));

  return TRUE;
}

/**********************************************************************//**
  Deletes a unit's home city making it unhomed.

  Returns TRUE iff the action could be done, FALSE if it couldn't.
**************************************************************************/
static bool do_unit_make_homeless(struct unit *punit,
                                  const struct action *paction)
{
  unit_change_homecity_handling(punit, NULL, TRUE);

  return punit->homecity == IDENTITY_NUMBER_ZERO;
}

/**********************************************************************//**
  Returns TRUE iff the player is able to change their diplomatic
  relationship to the other player to war.

  Note that the player can't declare war on someone they already are at war
  with.
**************************************************************************/
static bool rel_may_become_war(const struct player *pplayer,
                               const struct player *oplayer)
{
  enum diplstate_type ds;

  fc_assert_ret_val(pplayer, FALSE);
  fc_assert_ret_val(oplayer, FALSE);

  ds = player_diplstate_get(pplayer, oplayer)->type;

  /* The player can't declare war on
   * someone they already are at war with. */
  return ds != DS_WAR
      /* The player can't declare war on a teammate or on themself. */
      && ds != DS_TEAM && pplayer != oplayer;
}

/**********************************************************************//**
  Returns TRUE iff player1 declaring war on player2 is possible and would
  result in a unit of the specified type belonging to player1 going
  from being unable to do the specified action to player2 to being able to
  perform it.
**************************************************************************/
static bool
need_war_enabler(const struct unit_type *actor_utype,
                 const struct action *paction,
                 struct player *player1,
                 struct player *player2,
                 bool act_if_diplrel_kind(const struct unit_type *,
                                          const action_id,
                                          const int,
                                          const bool))
{
  if (player2 == NULL) {
    /* No one to declare war on */
    return FALSE;
  }

  if (!rel_may_become_war(player1, player2)) {
    /* Can't declare war. */
    return FALSE;
  }

  if (act_if_diplrel_kind(actor_utype, paction->id,
                          player_diplstate_get(player1,
                                               player2)->type,
                          TRUE)) {
    /* The current diplrel isn't the problem. */
    return FALSE;
  }

  if (!act_if_diplrel_kind(actor_utype, paction->id, DS_WAR, TRUE)) {
    /* War won't make this action legal. */
    return FALSE;
  }

  return TRUE;
}

/**********************************************************************//**
  Returns the first player that may enable the specified action if war is
  declared.

  Helper for need_war_player(). Use it instead.
**************************************************************************/
static struct player *need_war_player_hlp(const struct unit *actor,
                                          const action_id act,
                                          const struct tile *target_tile,
                                          const struct city *target_city,
                                          const struct unit *target_unit)
{
  struct player *target_player = NULL;
  struct player *actor_player = unit_owner(actor);
  struct action *paction = action_by_number(act);

  fc_assert_ret_val(paction != NULL, NULL);

  if (action_id_get_actor_kind(act) != AAK_UNIT) {
    /* No unit can ever do this action so it isn't relevant. */
    return NULL;
  }

  if (!unit_can_do_action(actor, act)) {
    /* The unit can't do the action no matter if there is war or not. */
    return NULL;
  }

  /* Look for hard coded war requirements without support for looking up in
   * an action enabler requirement. */
  switch (paction->result) {
  case ACTRES_ATTACK:
    /* Target is a unit stack but a city can block it. */
    fc_assert_action(action_get_target_kind(paction) == ATK_UNITS, break);
    if (target_tile) {
      struct city *tcity;

      if ((tcity = tile_city(target_tile))
          && rel_may_become_war(unit_owner(actor), city_owner(tcity))) {
        return city_owner(tcity);
      }
    }
    break;

  case ACTRES_PARADROP:
  case ACTRES_PARADROP_CONQUER:
    /* Target is a tile but a city can block it. */
    fc_assert_action(action_get_target_kind(paction) == ATK_TILE, break);
    if (target_tile
        && map_is_known_and_seen(target_tile, actor_player, V_MAIN)) {
      /* Seen tile unit savers */

      struct city *tcity;

      if ((tcity = tile_non_attack_city(target_tile, actor_player))) {
        return city_owner(tcity);
      }
    }
    break;
  case ACTRES_CONQUER_EXTRAS:
  case ACTRES_ESTABLISH_EMBASSY:
  case ACTRES_SPY_INVESTIGATE_CITY:
  case ACTRES_SPY_POISON:
  case ACTRES_SPY_SPREAD_PLAGUE:
  case ACTRES_SPY_STEAL_GOLD:
  case ACTRES_SPY_SABOTAGE_CITY:
  case ACTRES_SPY_TARGETED_SABOTAGE_CITY:
  case ACTRES_SPY_SABOTAGE_CITY_PRODUCTION:
  case ACTRES_SPY_STEAL_TECH:
  case ACTRES_SPY_TARGETED_STEAL_TECH:
  case ACTRES_SPY_INCITE_CITY:
  case ACTRES_TRADE_ROUTE:
  case ACTRES_MARKETPLACE:
  case ACTRES_HELP_WONDER:
  case ACTRES_SPY_BRIBE_UNIT:
  case ACTRES_SPY_SABOTAGE_UNIT:
  case ACTRES_CAPTURE_UNITS: /* Only foreign is a hard req. */
  case ACTRES_FOUND_CITY:
  case ACTRES_JOIN_CITY:
  case ACTRES_STEAL_MAPS:
  case ACTRES_SPY_NUKE:
  case ACTRES_NUKE:
  case ACTRES_NUKE_UNITS:
  case ACTRES_DESTROY_CITY:
  case ACTRES_EXPEL_UNIT:
  case ACTRES_DISBAND_UNIT_RECOVER:
  case ACTRES_DISBAND_UNIT:
  case ACTRES_HOME_CITY:
  case ACTRES_HOMELESS:
  case ACTRES_UPGRADE_UNIT:
  case ACTRES_AIRLIFT:
  case ACTRES_HEAL_UNIT:
  case ACTRES_STRIKE_BUILDING:
  case ACTRES_STRIKE_PRODUCTION:
  case ACTRES_BOMBARD:
  case ACTRES_CONQUER_CITY:
  case ACTRES_TRANSFORM_TERRAIN:
  case ACTRES_CULTIVATE:
  case ACTRES_PLANT:
  case ACTRES_PILLAGE:
  case ACTRES_CLEAN_POLLUTION:
  case ACTRES_CLEAN_FALLOUT:
  case ACTRES_FORTIFY:
  case ACTRES_CONVERT:
  case ACTRES_ROAD:
  case ACTRES_BASE:
  case ACTRES_MINE:
  case ACTRES_IRRIGATE:
  case ACTRES_TRANSPORT_ALIGHT:
  case ACTRES_TRANSPORT_UNLOAD:
  case ACTRES_TRANSPORT_DISEMBARK:
  case ACTRES_TRANSPORT_BOARD:
  case ACTRES_TRANSPORT_EMBARK:
  case ACTRES_SPY_ATTACK:
  case ACTRES_HUT_ENTER:
  case ACTRES_HUT_FRIGHTEN:
  case ACTRES_UNIT_MOVE:
  case ACTRES_NONE:
    /* No special help. */
    break;
  }

  /* Look for war requirements from the action enablers. */
  switch (action_get_target_kind(paction)) {
  case ATK_CITY:
    if (target_city == NULL) {
      /* No target city. */
      return NULL;
    }

    target_player = city_owner(target_city);
    break;
  case ATK_UNIT:
    if (target_unit == NULL) {
      /* No target unit. */
      return NULL;
    }
    target_player = unit_owner(target_unit);
    break;
  case ATK_UNITS:
    if (target_tile == NULL) {
      /* No target units since no target tile. */
      return NULL;
    }

    unit_list_iterate(target_tile->units, tunit) {
      if (rel_may_become_war(actor_player, unit_owner(tunit))) {
        target_player = unit_owner(tunit);
        break;
      }
    } unit_list_iterate_end;
    break;
  case ATK_TILE:
    if (target_tile == NULL) {
      /* No target tile. */
      return NULL;
    }
    target_player = tile_owner(target_tile);
    break;
  case ATK_EXTRAS:
    if (target_tile == NULL) {
      /* No target tile. */
      return NULL;
    }
    target_player = target_tile->owner;
    break;
  case ATK_SELF:
    /* Can't declare war on itself. */
    return NULL;
    break;
  case ATK_COUNT:
    /* Nothing to check. */
    fc_assert(action_id_get_target_kind(act) != ATK_COUNT);
    return NULL;
  }

  if (target_player == NULL) {
    /* Declaring war won't enable the specified action. */
    return NULL;
  }

  /* Look for DiplRelTileOther war requirements from the action enablers. */
  if (target_tile != NULL
      && need_war_enabler(unit_type_get(actor), paction,
                          actor_player, tile_owner(target_tile),
                          utype_can_act_if_tgt_diplrel_tile_other)) {
    return tile_owner(target_tile);
  }

  /* Look for DiplRel war requirements from the action enablers. */
  if (need_war_enabler(unit_type_get(actor), paction,
                       actor_player, target_player,
                       can_utype_do_act_if_tgt_diplrel)) {
    return target_player;
  }

  /* No check if other, non war, diplomatic states also could make the
   * action legal. This is need_war_player() so war is always the answer.
   * If you disagree and decide to add support please check that
   * webperimental's "can't found a city on a tile belonging to a non enemy"
   * rule still is detected. */

  return NULL;
}

/**********************************************************************//**
  Returns the first player that may enable the specified action if war is
  declared. If the specified action is ACTION_ANY the first player that
  may enable any action at all if war is declared will be returned.
**************************************************************************/
static struct player *need_war_player(const struct unit *actor,
                                      const action_id act_id,
                                      const struct tile *target_tile,
                                      const struct city *target_city,
                                      const struct unit *target_unit)
{
  if (act_id == ACTION_ANY) {
    /* Any action at all will do. */
    action_iterate(act) {
      struct player *war_player;

      war_player = need_war_player_hlp(actor, act,
                                       target_tile, target_city,
                                       target_unit);

      if (war_player != NULL) {
        /* Declaring war on this player may enable this action. */
        return war_player;
      }
    } action_iterate_end;

    /* No action at all may be enabled by declaring war. */
    return NULL;
  } else {
    /* Look for the specified action. */
    return need_war_player_hlp(actor, act_id,
                               target_tile, target_city,
                               target_unit);
  }
}

/**********************************************************************//**
  Returns TRUE iff the specified tile has a unit seen by and not allied to
  the specified player.
**************************************************************************/
static bool
tile_has_units_not_allied_to_but_seen_by(const struct tile *ptile,
                                         const struct player *pplayer)
{
  unit_list_iterate(ptile->units, pother) {
    if (can_player_see_unit(pplayer, pother)
        && !pplayers_allied(pplayer, unit_owner(pother))) {
      return TRUE;
    }
  } unit_list_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  Returns TRUE iff the specified terrain type blocks the specified action.

  If the "action" is ACTION_ANY all actions are checked.
**************************************************************************/
static bool does_terrain_block_action(const action_id act_id,
                                      bool is_target,
                                      struct unit *actor_unit,
                                      struct terrain *pterrain)
{
  if (act_id == ACTION_ANY) {
    /* Any action is OK. */
    action_iterate(alt_act) {
      if (utype_can_do_action(unit_type_get(actor_unit), alt_act)
          && !does_terrain_block_action(alt_act, is_target,
                                        actor_unit, pterrain)) {
        /* Only one action has to be possible. */
        return FALSE;
      }
    } action_iterate_end;

    /* No action enabled. */
    return TRUE;
  }

  /* ACTION_ANY is handled above. */
  fc_assert_ret_val(action_id_exists(act_id), FALSE);

  action_enabler_list_iterate(action_enablers_for_action(act_id),
                              enabler) {
    if (requirement_fulfilled_by_terrain(pterrain,
            (is_target ? &enabler->target_reqs : &enabler->actor_reqs))
        && requirement_fulfilled_by_unit_type(unit_type_get(actor_unit),
                                              &enabler->actor_reqs)) {
      /* This terrain kind doesn't block this action enabler. */
      return FALSE;
    }
  } action_enabler_list_iterate_end;

  return TRUE;
}

/**********************************************************************//**
  Returns TRUE iff the specified nation blocks the specified action.

  If the "action" is ACTION_ANY all actions are checked.
**************************************************************************/
static bool does_nation_block_action(const action_id act_id,
                                     bool is_target,
                                     struct unit *actor_unit,
                                     struct nation_type *pnation)
{
  if (act_id == ACTION_ANY) {
    /* Any action is OK. */
    action_iterate(alt_act) {
      if (utype_can_do_action(unit_type_get(actor_unit), alt_act)
          && !does_nation_block_action(alt_act, is_target,
                                       actor_unit, pnation)) {
        /* Only one action has to be possible. */
        return FALSE;
      }
    } action_iterate_end;

    /* No action enabled. */
    return TRUE;
  }

  /* ACTION_ANY is handled above. */
  fc_assert_ret_val(action_id_exists(act_id), FALSE);

  action_enabler_list_iterate(action_enablers_for_action(act_id),
                              enabler) {
    if (requirement_fulfilled_by_nation(pnation,
                                       (is_target ? &enabler->target_reqs
                                                  : &enabler->actor_reqs))
        && requirement_fulfilled_by_unit_type(unit_type_get(actor_unit),
                                              &enabler->actor_reqs)) {
      /* This nation doesn't block this action enabler. */
      return FALSE;
    }
  } action_enabler_list_iterate_end;

  return TRUE;
}

/**********************************************************************//**
  Returns an explanation why punit can't perform the specified action
  based on the current game state.
**************************************************************************/
static struct ane_expl *expl_act_not_enabl(struct unit *punit,
                                           const action_id act_id,
                                           const struct tile *target_tile,
                                           const struct city *target_city,
                                           const struct unit *target_unit)
{
  struct player *must_war_player;
  const struct action *paction;
  struct action *blocker;
  struct player *act_player = unit_owner(punit);
  const struct unit_type *act_utype = unit_type_get(punit);
  struct player *tgt_player = NULL;
  struct ane_expl *explnat = fc_malloc(sizeof(struct ane_expl));
  struct civ_map *nmap = &(wld.map);
  bool can_exist = can_unit_exist_at_tile(nmap, punit, unit_tile(punit));
  bool on_native = is_native_tile(unit_type_get(punit), unit_tile(punit));
  int action_custom;

  /* Not know yet. (Initialize before the below check.) */
  explnat->kind = ANEK_UNKNOWN;

  paction = action_by_number(act_id);

  if (act_id != ACTION_ANY) {
    /* A specific action should have a suitable target. */
    switch (action_get_target_kind(paction)) {
    case ATK_CITY:
      if (target_city == NULL) {
        explnat->kind = ANEK_MISSING_TARGET;
      }
      break;
    case ATK_UNIT:
      if (target_unit == NULL) {
        explnat->kind = ANEK_MISSING_TARGET;
      }
      break;
    case ATK_UNITS:
    case ATK_TILE:
    case ATK_EXTRAS:
      if (target_tile == NULL) {
        explnat->kind = ANEK_MISSING_TARGET;
      }
      break;
    case ATK_SELF:
      /* No other target. */
      break;
    case ATK_COUNT:
      fc_assert(action_get_target_kind(paction) != ATK_COUNT);
      break;
    }
  }

  if (explnat->kind == ANEK_MISSING_TARGET) {
    /* No point continuing. */
    return explnat;
  }

  if (act_id == ACTION_ANY) {
    /* Find the target player of some actions. */
    if (target_city) {
      /* Individual city targets have the highest priority. */
      tgt_player = city_owner(target_city);
    } else if (target_unit) {
      /* Individual unit targets have the next priority. */
      tgt_player = unit_owner(target_unit);
    } else if (target_tile) {
      /* Tile targets have the lowest priority. */
      tgt_player = tile_owner(target_tile);
    }
  } else {
    /* Find the target player of this action. */
    switch (action_get_target_kind(paction)) {
    case ATK_CITY:
      tgt_player = city_owner(target_city);
      break;
    case ATK_UNIT:
      tgt_player = unit_owner(target_unit);
      break;
    case ATK_TILE:
      tgt_player = tile_owner(target_tile);
      break;
    case ATK_EXTRAS:
      tgt_player = target_tile->extras_owner;
      break;
    case ATK_UNITS:
      /* A unit stack may contain units with multiple owners. Pick the
       * first one. */
      if (target_tile
          && unit_list_size(target_tile->units) > 0) {
        tgt_player = unit_owner(unit_list_get(target_tile->units, 0));
      }
      break;
    case ATK_SELF:
      /* A unit acting against itself. */
      tgt_player = act_player;
      break;
    case ATK_COUNT:
      fc_assert(action_get_target_kind(paction) != ATK_COUNT);
      break;
    }
  }

  if (act_id == ACTION_ANY) {
    action_custom = 0;
  } else {
    switch (paction->result) {
    case ACTRES_UPGRADE_UNIT:
      action_custom = unit_upgrade_test(nmap, punit, FALSE);
      break;
    case ACTRES_AIRLIFT:
      action_custom = test_unit_can_airlift_to(nmap, NULL, punit, target_city);
      break;
    case ACTRES_NUKE_UNITS:
      action_custom = unit_attack_units_at_tile_result(punit, paction,
                                                       target_tile);
      break;
    case ACTRES_ATTACK:
      action_custom = unit_attack_units_at_tile_result(punit, paction,
                                                       target_tile);
      break;
    case ACTRES_CONQUER_CITY:
      if (target_city) {
        action_custom = unit_move_to_tile_test(nmap, punit,
                                               punit->activity,
                                               unit_tile(punit),
                                               city_tile(target_city),
                                               FALSE, FALSE, NULL, TRUE);
      } else {
        action_custom = MR_OK;
      }
      break;
    case ACTRES_TRANSPORT_EMBARK:
      if (target_unit) {
        action_custom = unit_move_to_tile_test(nmap, punit,
                                               punit->activity,
                                               unit_tile(punit),
                                               unit_tile(target_unit),
                                               FALSE, TRUE, NULL, FALSE);
      } else {
        action_custom = MR_OK;
      }
      break;
    case ACTRES_TRANSPORT_DISEMBARK:
    case ACTRES_HUT_ENTER:
    case ACTRES_HUT_FRIGHTEN:
    case ACTRES_CONQUER_EXTRAS:
    case ACTRES_UNIT_MOVE:
      if (target_tile) {
        action_custom = unit_move_to_tile_test(nmap, punit,
                                               punit->activity,
                                               unit_tile(punit),
                                               target_tile,
                                               FALSE, FALSE, NULL, FALSE);
      } else {
        action_custom = MR_OK;
      }
      break;
    default:
      action_custom = 0;
      break;
    }
  }

  if (!unit_can_do_action(punit, act_id)) {
    explnat->kind = ANEK_ACTOR_UNIT;
  } else if (action_has_result_safe(paction, ACTRES_FOUND_CITY)
             && tile_city(target_tile)) {
    explnat->kind = ANEK_BAD_TARGET;
  } else if ((action_has_result_safe(paction, ACTRES_PARADROP_CONQUER)
              || action_has_result_safe(paction, ACTRES_PARADROP))
             && tile_has_units_not_allied_to_but_seen_by(target_tile,
                                                         act_player)) {
    explnat->kind = ANEK_TGT_NON_ALLIED_UNITS_ON_TILE;
  } else if ((!can_exist
       && !utype_can_do_act_when_ustate(unit_type_get(punit), act_id,
                                        USP_LIVABLE_TILE, FALSE))
      || (can_exist
          && !utype_can_do_act_when_ustate(unit_type_get(punit), act_id,
                                           USP_LIVABLE_TILE, TRUE))) {
    explnat->kind = ANEK_BAD_TERRAIN_ACT;
    explnat->no_act_terrain = tile_terrain(unit_tile(punit));
  } else if ((!on_native
       && !utype_can_do_act_when_ustate(unit_type_get(punit), act_id,
                                        USP_NATIVE_TILE, FALSE))
      || (on_native
          && !utype_can_do_act_when_ustate(unit_type_get(punit), act_id,
                                           USP_NATIVE_TILE, TRUE))) {
    explnat->kind = ANEK_BAD_TERRAIN_ACT;
    explnat->no_act_terrain = tile_terrain(unit_tile(punit));
  } else if (punit
             && does_terrain_block_action(act_id, FALSE,
                 punit, tile_terrain(unit_tile(punit)))) {
    /* No action enabler allows acting against this terrain kind. */
    explnat->kind = ANEK_BAD_TERRAIN_ACT;
    explnat->no_act_terrain = tile_terrain(unit_tile(punit));
  } else if (action_has_result_safe(paction, ACTRES_FOUND_CITY)
             && target_tile
             && terrain_has_flag(tile_terrain(target_tile),
                                 TER_NO_CITIES)) {
    explnat->kind = ANEK_BAD_TERRAIN_TGT;
    explnat->no_act_terrain = tile_terrain(target_tile);
  } else if ((action_has_result_safe(paction, ACTRES_PARADROP)
              || action_has_result_safe(paction, ACTRES_PARADROP_CONQUER))
             && target_tile != NULL
             && map_is_known_and_seen(target_tile, act_player,
                                      V_MAIN)
             && (!can_unit_exist_at_tile(nmap, punit, target_tile)
                 && (!BV_ISSET(paction->sub_results, ACT_SUB_RES_MAY_EMBARK)
                     || !unit_could_load_at(punit, target_tile)))) {
    explnat->kind = ANEK_BAD_TERRAIN_TGT;
    explnat->no_act_terrain = tile_terrain(target_tile);
  } else if (target_tile
             && does_terrain_block_action(act_id, TRUE,
                 punit, tile_terrain(target_tile))) {
    /* No action enabler allows acting against this terrain kind. */
    explnat->kind = ANEK_BAD_TERRAIN_TGT;
    explnat->no_act_terrain = tile_terrain(target_tile);
  } else if (unit_transported(punit)
             && !utype_can_do_act_when_ustate(unit_type_get(punit), act_id,
                                              USP_TRANSPORTED, TRUE)) {
    explnat->kind = ANEK_IS_TRANSPORTED;
  } else if (!unit_transported(punit)
             && !utype_can_do_act_when_ustate(unit_type_get(punit), act_id,
                                              USP_TRANSPORTED, FALSE)) {
    explnat->kind = ANEK_IS_NOT_TRANSPORTED;
  } else if (0 < get_transporter_occupancy(punit)
             && !utype_can_do_act_when_ustate(unit_type_get(punit), act_id,
                                              USP_TRANSPORTING, TRUE)) {
    explnat->kind = ANEK_IS_TRANSPORTING;
  } else if (!(0 < get_transporter_occupancy(punit))
             && !utype_can_do_act_when_ustate(unit_type_get(punit), act_id,
                                              USP_TRANSPORTING, FALSE)) {
    explnat->kind = ANEK_IS_NOT_TRANSPORTING;
  } else if ((punit->homecity > 0)
             && !utype_can_do_act_when_ustate(unit_type_get(punit), act_id,
                                              USP_HAS_HOME_CITY, TRUE)) {
    explnat->kind = ANEK_ACTOR_HAS_HOME_CITY;
  } else if ((punit->homecity <= 0)
             && !utype_can_do_act_when_ustate(unit_type_get(punit), act_id,
                                              USP_HAS_HOME_CITY, FALSE)) {
    explnat->kind = ANEK_ACTOR_HAS_NO_HOME_CITY;
  } else if ((punit->homecity <= 0)
             && (action_has_result_safe(paction, ACTRES_TRADE_ROUTE)
                 || action_has_result_safe(paction, ACTRES_MARKETPLACE))) {
    explnat->kind = ANEK_ACTOR_HAS_NO_HOME_CITY;
  } else if (act_player && tgt_player
             && (player_diplstate_get(act_player, tgt_player)->type
                 == DS_PEACE)
             && can_utype_do_act_if_tgt_diplrel(unit_type_get(punit),
                                                act_id,
                                                DS_PEACE,
                                                FALSE)
             && !can_utype_do_act_if_tgt_diplrel(unit_type_get(punit),
                                                 act_id,
                                                 DS_PEACE,
                                                 TRUE)) {
    explnat->kind = ANEK_PEACE;
    explnat->peace_with = tgt_player;
  } else if ((must_war_player = need_war_player(punit,
                                                act_id,
                                                target_tile,
                                                target_city,
                                                target_unit))) {
    explnat->kind = ANEK_NO_WAR;
    explnat->no_war_with = must_war_player;
  } else if (action_mp_full_makes_legal(punit, act_id)) {
    explnat->kind = ANEK_LOW_MP;
  } else if (tgt_player != NULL
             && act_player != tgt_player
             && !can_utype_do_act_if_tgt_diplrel(unit_type_get(punit),
                                                 act_id,
                                                 DRO_FOREIGN,
                                                 TRUE)) {
    explnat->kind = ANEK_FOREIGN;
  } else if (tgt_player != NULL
             && act_player == tgt_player
             && !can_utype_do_act_if_tgt_diplrel(unit_type_get(punit),
                                                 act_id,
                                                 DRO_FOREIGN,
                                                 FALSE)) {
    explnat->kind = ANEK_DOMESTIC;
  } else if (punit != NULL
             && does_nation_block_action(act_id, FALSE,
                                         punit, act_player->nation)) {
    explnat->kind = ANEK_NATION_ACT;
    explnat->no_act_nation = act_player->nation;
  } else if (tgt_player
             && does_nation_block_action(act_id, TRUE,
                                         punit, tgt_player->nation)) {
    explnat->kind = ANEK_NATION_TGT;
    explnat->no_act_nation = tgt_player->nation;
  } else if ((target_tile && tile_city(target_tile))
             && !utype_may_act_tgt_city_tile(unit_type_get(punit),
                                             act_id,
                                             CITYT_CENTER,
                                             TRUE)) {
    explnat->kind = ANEK_IS_CITY_CENTER;
  } else if ((target_tile && !tile_city(target_tile))
             && !utype_may_act_tgt_city_tile(unit_type_get(punit),
                                             act_id,
                                             CITYT_CENTER,
                                             FALSE)) {
    explnat->kind = ANEK_IS_NOT_CITY_CENTER;
  } else if ((target_tile && tile_owner(target_tile) != NULL)
             && !utype_may_act_tgt_city_tile(unit_type_get(punit),
                                             act_id,
                                             CITYT_CLAIMED,
                                             TRUE)) {
    explnat->kind = ANEK_TGT_IS_CLAIMED;
  } else if ((target_tile && tile_owner(target_tile) == NULL)
             && !utype_may_act_tgt_city_tile(unit_type_get(punit),
                                             act_id,
                                             CITYT_CLAIMED,
                                             FALSE)) {
    explnat->kind = ANEK_TGT_IS_UNCLAIMED;
  } else if (paction && punit
             && ((target_tile
                  && !action_distance_inside_max(paction,
                      real_map_distance(unit_tile(punit), target_tile)))
                 || (target_city
                     && !action_distance_inside_max(paction,
                         real_map_distance(unit_tile(punit),
                                           city_tile(target_city))))
                 || (target_unit
                     && !action_distance_inside_max(paction,
                         real_map_distance(unit_tile(punit),
                                           unit_tile(target_unit)))))) {
    explnat->kind = ANEK_DISTANCE_FAR;
    explnat->distance = paction->max_distance;
  } else if ((action_has_result_safe(paction, ACTRES_PARADROP_CONQUER)
              || action_has_result_safe(paction, ACTRES_PARADROP))
             && punit && target_tile
             && real_map_distance(unit_tile(punit), target_tile)
                > unit_type_get(punit)->paratroopers_range) {
    explnat->kind = ANEK_DISTANCE_FAR;
    explnat->distance = unit_type_get(punit)->paratroopers_range;
  } else if (paction && punit
             && ((target_tile
                  && real_map_distance(unit_tile(punit), target_tile)
                      < paction->min_distance)
                 || (target_city
                     && real_map_distance(unit_tile(punit),
                                          city_tile(target_city))
                        < paction->min_distance)
                 || (target_unit
                     && real_map_distance(unit_tile(punit),
                                          unit_tile(target_unit))
                        < paction->min_distance))) {
    explnat->kind = ANEK_DISTANCE_NEAR;
    explnat->distance = paction->min_distance;
  } else if (target_city
             && (action_has_result_safe(paction, ACTRES_JOIN_CITY)
                 && action_actor_utype_hard_reqs_ok(paction,
                                                    unit_type_get(punit))
                 && (city_size_get(target_city) + unit_pop_value(punit)
                     > game.info.add_to_size_limit))) {
    /* TODO: Check max city size requirements from action enabler target
     * vectors. */
    explnat->kind = ANEK_CITY_TOO_BIG;
  } else if (target_city
             && (action_has_result_safe(paction, ACTRES_JOIN_CITY)
                 && action_actor_utype_hard_reqs_ok(paction,
                                                    unit_type_get(punit))
                 && (!city_can_grow_to(target_city,
                                       city_size_get(target_city)
                                       + unit_pop_value(punit))))) {
    explnat->kind = ANEK_CITY_POP_LIMIT;
  } else if ((action_has_result_safe(paction, ACTRES_NUKE_UNITS)
              || action_has_result_safe(paction, ACTRES_ATTACK))
             && action_custom != ATT_OK) {
    switch (action_custom) {
    case ATT_NON_ATTACK:
      explnat->kind = ANEK_ACTOR_UNIT;
      break;
    case ATT_UNREACHABLE:
      explnat->kind = ANEK_TGT_UNREACHABLE;
      break;
    case ATT_NONNATIVE_SRC:
      explnat->kind = ANEK_BAD_TERRAIN_ACT;
      explnat->no_act_terrain = tile_terrain(unit_tile(punit));
      break;
    case ATT_NONNATIVE_DST:
      explnat->kind = ANEK_BAD_TERRAIN_TGT;
      explnat->no_act_terrain = tile_terrain(target_tile);
      break;
    default:
      fc_assert(action_custom != ATT_OK);
      explnat->kind = ANEK_UNKNOWN;
      break;
    }
  } else if (action_has_result_safe(paction, ACTRES_AIRLIFT)
             && action_custom == AR_SRC_NO_FLIGHTS) {
    explnat->kind = ANEK_CITY_NO_CAPACITY;
    explnat->capacity_city = tile_city(unit_tile(punit));
  } else if (action_has_result_safe(paction, ACTRES_AIRLIFT)
             && action_custom == AR_DST_NO_FLIGHTS) {
    explnat->kind = ANEK_CITY_NO_CAPACITY;
    explnat->capacity_city = game_city_by_number(target_city->id);
  } else if (action_has_result_safe(paction, ACTRES_FOUND_CITY)
             && citymindist_prevents_city_on_tile(nmap, target_tile)) {
    explnat->kind = ANEK_CITY_TOO_CLOSE_TGT;
  } else if ((action_has_result_safe(paction, ACTRES_PARADROP_CONQUER)
              || action_has_result_safe(paction, ACTRES_PARADROP))
             && target_tile != NULL
             && !map_is_known(target_tile, act_player)) {
    explnat->kind = ANEK_TGT_TILE_UNKNOWN;
  } else if ((action_has_result_safe(paction, ACTRES_CONQUER_CITY)
              || action_id_has_result_safe(act_id, ACTRES_CONQUER_EXTRAS)
              || action_id_has_result_safe(act_id, ACTRES_HUT_ENTER)
              || action_id_has_result_safe(act_id, ACTRES_HUT_FRIGHTEN)
              || action_id_has_result_safe(act_id, ACTRES_UNIT_MOVE)
              || action_has_result_safe(paction,
                                        ACTRES_TRANSPORT_EMBARK)
              || action_has_result_safe(paction,
                                        ACTRES_TRANSPORT_DISEMBARK))
             && action_custom != MR_OK) {
    switch (action_custom) {
    case MR_CANNOT_DISEMBARK:
      explnat->kind = ANEK_DISEMBARK_ACT;
      break;
    case MR_TRIREME:
      explnat->kind = ANEK_TRIREME_MOVE;
      break;
    case MR_DESTINATION_OCCUPIED_BY_NON_ALLIED_UNIT:
      explnat->kind = ANEK_TGT_NON_ALLIED_UNITS_ON_TILE;
      break;
    default:
      fc_assert(action_custom != MR_OK);
      explnat->kind = ANEK_UNKNOWN;
      break;
    }
  } else if (action_has_result_safe(paction, ACTRES_SPY_BRIBE_UNIT)
             && utype_player_already_has_this_unique(act_player,
                 unit_type_get(target_unit))) {
    explnat->kind = ANEK_TGT_IS_UNIQUE_ACT_HAS;
    explnat->no_tgt_utype = unit_type_get(target_unit);
  } else if ((game.scenario.prevent_new_cities
              && utype_can_do_action(unit_type_get(punit), ACTION_FOUND_CITY))
             && (action_has_result_safe(paction, ACTRES_FOUND_CITY)
                 || act_id == ACTION_ANY)) {
    /* Please add a check for any new action forbidding scenario setting
     * above this comment. */
    explnat->kind = ANEK_SCENARIO_DISABLED;
  } else if (action_has_result_safe(paction, ACTRES_UPGRADE_UNIT)
             && action_custom == UU_NO_MONEY) {
    explnat->kind = ANEK_ACT_NOT_ENOUGH_MONEY;
    explnat->gold_needed = unit_upgrade_price(act_player, act_utype,
                                              can_upgrade_unittype(
                                                  act_player, act_utype));
  } else if (paction
             && (blocker = action_is_blocked_by(nmap, paction, punit,
                                                target_tile, target_city,
                                                target_unit))) {
    explnat->kind = ANEK_ACTION_BLOCKS;
    explnat->blocker = blocker;
  } else {
    explnat->kind = ANEK_UNKNOWN;
  }

  return explnat;
}

/**********************************************************************//**
  Give the reason kind why an action isn't enabled.
**************************************************************************/
enum ane_kind action_not_enabled_reason(struct unit *punit,
                                        action_id act_id,
                                        const struct tile *target_tile,
                                        const struct city *target_city,
                                        const struct unit *target_unit)
{
  struct ane_expl *explnat = expl_act_not_enabl(punit, act_id,
                                                target_tile,
                                                target_city, target_unit);
  enum ane_kind out = explnat->kind;

  free(explnat);

  return out;
}

/**********************************************************************//**
  Explain why punit can't perform any action at all based on its current
  game state.
**************************************************************************/
static void explain_why_no_action_enabled(struct unit *punit,
                                          const struct tile *target_tile,
                                          const struct city *target_city,
                                          const struct unit *target_unit)
{
  struct player *pplayer = unit_owner(punit);
  struct ane_expl *explnat = expl_act_not_enabl(punit, ACTION_ANY,
                                                target_tile,
                                                target_city, target_unit);
  const struct civ_map *nmap = &(wld.map);

  switch (explnat->kind) {
  case ANEK_ACTOR_UNIT:
    /* This shouldn't happen unless the client is buggy given the current
     * users. */
    fc_assert_msg(explnat->kind != ANEK_ACTOR_UNIT,
                  "Asked to explain why a non actor can't act.");

    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  _("Unit cannot do anything."));
    break;
  case ANEK_MISSING_TARGET:
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  _("Your %s found no suitable target."),
                  unit_name_translation(punit));
    break;
  case ANEK_BAD_TARGET:
    /* This shouldn't happen at the moment. Only specific action checks
     * will trigger bad target checks. This is a reply to a question about
     * any action. */
    fc_assert(explnat->kind != ANEK_BAD_TARGET);

    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  _("Your %s found no suitable target."),
                  unit_name_translation(punit));
    break;
  case ANEK_BAD_TERRAIN_ACT:
    {
      const char *types[utype_count()];
      int i = 0;

      if (!utype_can_do_act_when_ustate(unit_type_get(punit),
                                        ACTION_ANY, USP_LIVABLE_TILE,
                                        FALSE)
          && !can_unit_exist_at_tile(nmap, punit, unit_tile(punit))) {
        unit_type_iterate(utype) {
          if (utype_can_do_act_when_ustate(utype, ACTION_ANY,
                                           USP_LIVABLE_TILE, FALSE)) {
            types[i++] = utype_name_translation(utype);
          }
        } unit_type_iterate_end;
      }

      if (0 < i) {
        struct astring astr = ASTRING_INIT;

        notify_player(pplayer, unit_tile(punit),
                      E_BAD_COMMAND, ftc_server,
                      /* TRANS: terrain name
                       * "Your Diplomat cannot act from Ocean. Only
                       * Spy or Partisan ... */
                      _("Your %s cannot act from %s. "
                        "Only %s can act from a non livable tile."),
                      unit_name_translation(punit),
                      terrain_name_translation(explnat->no_act_terrain),
                      astr_build_or_list(&astr, types, i));

        astr_free(&astr);
      } else {
        notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                      /* TRANS: terrain name */
                      _("Unit cannot act from %s."),
                      terrain_name_translation(explnat->no_act_terrain));
      }
    }
    break;
  case ANEK_BAD_TERRAIN_TGT:
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  /* TRANS: terrain name */
                  _("Unit cannot act against %s."),
                  terrain_name_translation(explnat->no_act_terrain));
    break;
  case ANEK_IS_TRANSPORTED:
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  _("This unit is being transported, and"
                    " so cannot act."));
    break;
  case ANEK_IS_NOT_TRANSPORTED:
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  _("This unit cannot act when it isn't being "
                    "transported."));
    break;
  case ANEK_IS_TRANSPORTING:
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  _("This unit is transporting, and"
                    " so cannot act."));
    break;
  case ANEK_IS_NOT_TRANSPORTING:
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  _("This unit cannot act when it isn't transporting."));
    break;
  case ANEK_ACTOR_HAS_HOME_CITY:
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  _("This unit has a home city, and so cannot act."));
    break;
  case ANEK_ACTOR_HAS_NO_HOME_CITY:
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  _("This unit cannot act unless it has a home city."));
    break;
  case ANEK_NO_WAR:
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  _("You must declare war on %s first. Try using "
                    "the Nations report"
#ifndef FREECIV_WEB
                    " (F3)"
#endif /* FREECIV_WEB */
                    "."),
                  player_name(explnat->no_war_with));
    break;
  case ANEK_PEACE:
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  _("You must break peace with %s first. Try using "
                    "the Nations report to declare war"
#ifndef FREECIV_WEB
                    " (F3)"
#endif /* FREECIV_WEB */
                    "."),
                  player_name(explnat->peace_with));
    break;
  case ANEK_DOMESTIC:
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  _("This unit cannot act against domestic targets."));
    break;
  case ANEK_FOREIGN:
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  _("This unit cannot act against foreign targets."));
    break;
  case ANEK_TGT_NON_ALLIED_UNITS_ON_TILE:
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  /* TRANS: Riflemen */
                  _("%s cannot act against tiles with non allied units."),
                  unit_name_translation(punit));
    break;
  case ANEK_NATION_ACT:
     notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                   /* TRANS: Swedish ... Riflemen */
                   _("%s %s cannot act."),
                   nation_adjective_translation(explnat->no_act_nation),
                   unit_name_translation(punit));
     break;
  case ANEK_NATION_TGT:
     notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                   /* TRANS: ... Pirate ... */
                   _("This unit cannot act against %s targets."),
                   nation_adjective_translation(explnat->no_act_nation));
     break;
  case ANEK_LOW_MP:
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  _("This unit has too few moves left to act."));
    break;
  case ANEK_IS_CITY_CENTER:
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  _("This unit cannot act against city centers."));
    break;
  case ANEK_IS_NOT_CITY_CENTER:
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  _("This unit cannot act against non city centers."));
    break;
  case ANEK_TGT_IS_CLAIMED:
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  _("This unit cannot act against claimed tiles."));
    break;
  case ANEK_TGT_IS_UNCLAIMED:
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  _("This unit cannot act against unclaimed tiles."));
    break;
  case ANEK_DISTANCE_NEAR:
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  _("This unit is too near its target to act."));
    break;
  case ANEK_DISTANCE_FAR:
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  _("This unit is too far away from its target to act."));
    break;
  case ANEK_SCENARIO_DISABLED:
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  _("Can't perform any action this scenario permits."));
    break;
  case ANEK_CITY_TOO_CLOSE_TGT:
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  _("Can't perform any action this close to a city."));
    break;
  case ANEK_CITY_TOO_BIG:
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  /* TRANS: Settler ... Berlin */
                  _("%s can't do anything to %s. It is too big."),
                  unit_name_translation(punit),
                  city_name_get(target_city));
    break;
  case ANEK_CITY_POP_LIMIT:
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  /* TRANS: London ... Settlers */
                  _("%s needs an improvement to grow, so "
                    "%s cannot do anything to it."),
                  city_name_get(target_city),
                  unit_name_translation(punit));
    break;
  case ANEK_CITY_NO_CAPACITY:
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  /* TRANS: Paris ... Warriors (think: airlift) */
                  _("%s don't have enough capacity, so "
                    "%s cannot do anything."),
                  city_name_get(explnat->capacity_city),
                  unit_name_translation(punit));
    break;
  case ANEK_TGT_TILE_UNKNOWN:
    notify_player(pplayer, target_tile, E_BAD_COMMAND, ftc_server,
                  /* TRANS: Paratroopers ... */
                  _("%s can't do anything to an unknown target tile."),
                  unit_name_translation(punit));
    break;
  case ANEK_ACT_NOT_ENOUGH_MONEY:
    {
      char tbuf[MAX_LEN_MSG];

      /* TRANS: Used below. Separate so treasury content too can determine
       * if this is plural. */
      fc_snprintf(tbuf, ARRAY_SIZE(tbuf), PL_("Treasury contains %d gold.",
                                              "Treasury contains %d gold.",
                                              pplayer->economic.gold),
                  pplayer->economic.gold);

      notify_player(pplayer, target_tile, E_BAD_COMMAND, ftc_server,
                    /* TRANS: "Spy can't do anything. 154 gold may help.
                     * Treasury contains 100 gold." */
                    PL_("%s can't do anything. %d gold may help. %s",
                        "%s can't do anything. %d gold may help. %s",
                        explnat->gold_needed),
                    unit_name_translation(punit),
                    explnat->gold_needed, tbuf);
    }
    break;
  case ANEK_TRIREME_MOVE:
    notify_player(pplayer, target_tile, E_BAD_COMMAND, ftc_server,
                  _("%s cannot move that far from the coast line."),
                  unit_link(punit));
    break;
  case ANEK_DISEMBARK_ACT:
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  _("%s cannot disembark outside of a city or a native base "
                    "for %s."),
                  unit_link(punit),
                  utype_name_translation(
                      unit_type_get(unit_transport_get(punit))));
    break;
  case ANEK_TGT_UNREACHABLE:
    notify_player(pplayer, target_tile, E_BAD_COMMAND, ftc_server,
                  _("%s can't do anything since there is an unreachable "
                    "unit."),
                  unit_name_translation(punit));
    break;
  case ANEK_TGT_IS_UNIQUE_ACT_HAS:
    notify_player(pplayer, target_tile, E_BAD_COMMAND, ftc_server,
                  _("%s can't do anything since you already have a %s."),
                  unit_name_translation(punit),
                  utype_name_translation(explnat->no_tgt_utype));
    break;
  case ANEK_ACTION_BLOCKS:
    /* If an action blocked another action the blocking action must be
     * possible. */
    fc_assert(explnat->kind != ANEK_ACTION_BLOCKS);
    fc__fallthrough; /* Fall through to unknown cause. */
  case ANEK_UNKNOWN:
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  _("No action possible."));
    break;
  }

  free(explnat);
}

/**********************************************************************//**
  Handle a query for what actions a unit may do.

  MUST always send a reply so the client can move on in the queue. This
  includes when the client give invalid input. That the acting unit died
  before the server received a request for what actions it could do should
  not stop the client from processing the next unit in the queue.
**************************************************************************/
void handle_unit_get_actions(struct connection *pc,
                             const struct packet_unit_get_actions *packet)
{
  struct player *actor_player;
  struct unit *actor_unit;
  struct tile *target_tile;
  struct act_prob probabilities[MAX_NUM_ACTIONS];
  struct unit *target_unit;
  struct city *target_city;
  struct extra_type *target_extra;
  int actor_target_distance;
  const struct player_tile *plrtile;
  int target_extra_id = packet->target_extra_id;
  const struct civ_map *nmap = &(wld.map);
  int actor_unit_id, target_unit_id_client;

  /* No potentially legal action is known yet. If none is found the player
   * should get an explanation. */
  bool at_least_one_action = FALSE;

  /* A target should only be sent if it is possible to act against it */
  int target_city_id = IDENTITY_NUMBER_ZERO;
  int target_unit_id = IDENTITY_NUMBER_ZERO;

  if (!has_capability("ids32", pc->capability)) {
    actor_unit_id = packet->actor_unit_id16;
    target_unit_id_client = packet->target_unit_id16;
  } else {
    actor_unit_id = packet->actor_unit_id32;
    target_unit_id_client = packet->target_unit_id32;
  }

  actor_player = pc->playing;
  actor_unit = game_unit_by_number(actor_unit_id);
  target_tile = index_to_tile(nmap, packet->target_tile_id);

  /* Initialize the action probabilities. */
  action_iterate(act) {
    probabilities[act] = ACTPROB_NA;
  } action_iterate_end;

  /* Check if the request is valid. */
  if (!target_tile || !actor_unit || !actor_player
      || actor_unit->owner != actor_player) {
    dsend_packet_unit_actions(pc, actor_unit_id, actor_unit_id,
                              IDENTITY_NUMBER_ZERO, IDENTITY_NUMBER_ZERO,
                              IDENTITY_NUMBER_ZERO, IDENTITY_NUMBER_ZERO,
                              packet->target_tile_id, packet->target_extra_id,
                              packet->request_kind,
                              probabilities);
    return;
  }

  /* Select the targets. */

  if (target_unit_id_client == IDENTITY_NUMBER_ZERO) {
    /* Find a new target unit. */
    target_unit = action_tgt_unit(actor_unit, target_tile, TRUE);
  } else {
    /* Prepare the client selected target unit. */
    target_unit = game_unit_by_number(target_unit_id_client);
  }

  /* Find the target city. */
  target_city = action_tgt_city(actor_unit, target_tile, TRUE);

  /* The specified target unit must be located at the target tile. */
  if (target_unit && unit_tile(target_unit) != target_tile) {
    notify_player(actor_player, unit_tile(actor_unit),
                  E_BAD_COMMAND, ftc_server,
                  _("Target not at target tile."));
    dsend_packet_unit_actions(pc, actor_unit_id, actor_unit_id,
                              IDENTITY_NUMBER_ZERO, IDENTITY_NUMBER_ZERO,
                              IDENTITY_NUMBER_ZERO, IDENTITY_NUMBER_ZERO,
                              packet->target_tile_id, packet->target_extra_id,
                              packet->request_kind,
                              probabilities);
    return;
  }

  if (packet->target_extra_id == EXTRA_NONE) {
    /* See if a target extra can be found. */
    target_extra = action_tgt_tile_extra(actor_unit, target_tile, TRUE);
  } else {
    /* Use the client selected target extra. */
    target_extra = extra_by_number(packet->target_extra_id);
  }

  /* The player may have outdated information about the target tile.
   * Limiting the player knowledge look up to the target tile is OK since
   * all targets must be located at it. */
  plrtile = map_get_player_tile(target_tile, actor_player);

  /* Distance between actor and target tile. */
  actor_target_distance = real_map_distance(unit_tile(actor_unit),
                                            target_tile);

  /* Find out what can be done to the targets. */

  /* Set the probability for the actions. */
  action_iterate(act) {
    if (action_id_get_actor_kind(act) != AAK_UNIT) {
      /* Not relevant. */
      continue;
    }

    switch (action_id_get_target_kind(act)) {
    case ATK_CITY:
      if (plrtile && plrtile->site) {
        /* Only a known city may be targeted. */
        if (target_city) {
          /* Calculate the probabilities. */
          probabilities[act] = action_prob_vs_city(nmap, actor_unit, act,
                                                   target_city);
        } else if (!tile_is_seen(target_tile, actor_player)
                   && action_maybe_possible_actor_unit(nmap, act, actor_unit)
                   && action_id_distance_accepted(act,
                                                  actor_target_distance)) {
          /* The target city is non existing. The player isn't aware of this
           * fact because they can't see the tile it was located on. The
           * actor unit it self doesn't contradict the requirements to
           * perform the action. The (no longer existing) target city was
           * known to be close enough. */
          probabilities[act] = ACTPROB_NOT_KNOWN;
        } else {
          /* The actor unit is known to be unable to act or the target city
           * is known to be too far away. */
          probabilities[act] = ACTPROB_IMPOSSIBLE;
        }
      } else {
        /* No target to act against. */
        probabilities[act] = ACTPROB_IMPOSSIBLE;
      }
      break;
    case ATK_UNIT:
      if (target_unit) {
        /* Calculate the probabilities. */
        probabilities[act] = action_prob_vs_unit(nmap, actor_unit, act,
                                                 target_unit);
      } else {
        /* No target to act against. */
        probabilities[act] = ACTPROB_IMPOSSIBLE;
      }
      break;
    case ATK_UNITS:
      if (target_tile) {
        /* Calculate the probabilities. */
        probabilities[act] = action_prob_vs_units(nmap, actor_unit, act,
                                                  target_tile);
      } else {
        /* No target to act against. */
        probabilities[act] = ACTPROB_IMPOSSIBLE;
      }
      break;
    case ATK_TILE:
      if (target_tile) {
        /* Calculate the probabilities. */
        probabilities[act] = action_prob_vs_tile(nmap, actor_unit, act,
                                                 target_tile, target_extra);
      } else {
        /* No target to act against. */
        probabilities[act] = ACTPROB_IMPOSSIBLE;
      }
      break;
    case ATK_EXTRAS:
      if (target_tile) {
        /* Calculate the probabilities. */
        probabilities[act] = action_prob_vs_extras(nmap, actor_unit, act,
                                                   target_tile,
                                                   target_extra);
      } else {
        /* No target to act against. */
        probabilities[act] = ACTPROB_IMPOSSIBLE;
      }
      break;
    case ATK_SELF:
      if (actor_target_distance == 0) {
        /* Calculate the probabilities. */
        probabilities[act] = action_prob_self(nmap, actor_unit, act);
      } else {
        /* Don't bother with self targeted actions unless the actor is
         * asking about what can be done to its own tile. */
        probabilities[act] = ACTPROB_IMPOSSIBLE;
      }
      break;
    case ATK_COUNT:
      fc_assert_action(action_id_get_target_kind(act) != ATK_COUNT,
                       probabilities[act] = ACTPROB_IMPOSSIBLE);
      break;
    }
  } action_iterate_end;

  /* Analyze the probabilities. Decide what targets to send and if an
   * explanation is needed. */
  action_iterate(act) {
    if (action_prob_possible(probabilities[act])) {
      /* An action can be done. No need to explain why no action can be
       * done. */
      at_least_one_action = TRUE;

      switch (action_id_get_target_kind(act)) {
      case ATK_CITY:
        /* The city should be sent as a target since it is possible to act
         * against it. */

        /* All city targeted actions requires that the player is aware of
         * the target city. It is therefore in the player's map. */
        fc_assert_action(plrtile, continue);
        fc_assert_action(plrtile->site, continue);

        target_city_id = plrtile->site->identity;
        break;
      case ATK_UNIT:
        /* The unit should be sent as a target since it is possible to act
         * against it. */
        fc_assert(target_unit != NULL);
        target_unit_id = target_unit->id;
        break;
      case ATK_TILE:
      case ATK_EXTRAS:
        /* The target tile isn't selected here so it hasn't changed. */
        fc_assert(target_tile != NULL);

        if (target_extra && action_id_has_complex_target(act)) {
          /* The target extra may have been set here. */
          target_extra_id = target_extra->id;
        }
        break;
      case ATK_UNITS:
        /* The target tile isn't selected here so it hasn't changed. */
        fc_assert(target_tile != NULL);
        break;
      case ATK_SELF:
        /* The target unit is the actor unit. It is already sent. */
        fc_assert(actor_unit != NULL);
        break;
      case ATK_COUNT:
        fc_assert_msg(action_id_get_target_kind(act) != ATK_COUNT,
                      "Invalid action target kind.");
        break;
      }

      if (target_city_id != IDENTITY_NUMBER_ZERO
          && target_unit_id != IDENTITY_NUMBER_ZERO) {
        /* No need to find out more. */
        break;
      }
    }
  } action_iterate_end;

  /* Send possible actions and targets. */
  dsend_packet_unit_actions(pc,
                            actor_unit_id, actor_unit_id,
                            target_unit_id, target_unit_id,
                            target_city_id, target_city_id,
                            packet->target_tile_id, target_extra_id,
                            packet->request_kind,
                            probabilities);

  if (packet->request_kind == REQEST_PLAYER_INITIATED && !at_least_one_action) {
    /* The user should get an explanation why no action is possible. */
    explain_why_no_action_enabled(actor_unit,
                                  target_tile, target_city, target_unit);
  }
}

/**********************************************************************//**
  Try to explain to the player why an action is illegal.

  Event type should be E_BAD_COMMAND if the player should know that the
  action is illegal or E_UNIT_ILLEGAL_ACTION if new information potentially
  is being revealed to the player.
**************************************************************************/
void illegal_action_msg(struct player *pplayer,
                        const enum event_type event,
                        struct unit *actor,
                        const action_id stopped_action,
                        const struct tile *target_tile,
                        const struct city *target_city,
                        const struct unit *target_unit)
{
  struct ane_expl *explnat;
  const struct civ_map *nmap = &(wld.map);

  /* Explain why the action was illegal. */
  explnat = expl_act_not_enabl(actor, stopped_action,
                               target_tile, target_city, target_unit);
  switch (explnat->kind) {
  case ANEK_ACTOR_UNIT:
    {
      struct astring astr = ASTRING_INIT;

      if (role_units_translations(&astr,
                                  action_id_get_role(stopped_action),
                                  TRUE)) {
        notify_player(pplayer, unit_tile(actor),
                      event, ftc_server,
                      /* TRANS: Only Diplomat or Spy can do Steal Gold. */
                      _("Only %s can do %s."),
                      astr_str(&astr),
                      action_id_name_translation(stopped_action));
        astr_free(&astr);
      } else {
        notify_player(pplayer, unit_tile(actor),
                      event, ftc_server,
                      /* TRANS: Spy can't do Capture Units. */
                      _("%s can't do %s."),
                      unit_name_translation(actor),
                      action_id_name_translation(stopped_action));
      }
    }
    break;
  case ANEK_MISSING_TARGET:
    notify_player(pplayer, unit_tile(actor), event, ftc_server,
                  /* TRANS: "Your Spy found ... suitable for
                   * Bribe Enemy Unit." */
                  _("Your %s found no target suitable for %s."),
                  unit_name_translation(actor),
                  action_id_name_translation(stopped_action));
    break;
  case ANEK_BAD_TARGET:
    notify_player(pplayer, unit_tile(actor), event, ftc_server,
                  /* TRANS: "Having your Spy do Bribe Enemy Unit to
                   * this target ..." */
                  _("Having your %s do %s to this target is redundant."),
                  unit_name_translation(actor),
                  action_id_name_translation(stopped_action));
    break;
  case ANEK_BAD_TERRAIN_ACT:
    {
      const char *types[utype_count()];
      int i = 0;

      if (!utype_can_do_act_when_ustate(unit_type_get(actor),
                                        stopped_action, USP_LIVABLE_TILE,
                                        FALSE)
          && !can_unit_exist_at_tile(nmap, actor, unit_tile(actor))) {
        unit_type_iterate(utype) {
          if (utype_can_do_act_when_ustate(utype, stopped_action,
                                           USP_LIVABLE_TILE, FALSE)) {
            types[i++] = utype_name_translation(utype);
          }
        } unit_type_iterate_end;
      }

      if (0 < i) {
        struct astring astr = ASTRING_INIT;

        notify_player(pplayer, unit_tile(actor),
                      event, ftc_server,
                      /* TRANS: action name.
                       * "Your Spy can't do Steal Gold from Ocean.
                       * Only Explorer or Partisan can do Steal Gold ..." */
                      _("Your %s can't do %s from %s. "
                        "Only %s can do %s from a non livable tile."),
                      unit_name_translation(actor),
                      action_id_name_translation(stopped_action),
                      terrain_name_translation(explnat->no_act_terrain),
                      astr_build_or_list(&astr, types, i),
                      action_id_name_translation(stopped_action));

        astr_free(&astr);
      } else {
        notify_player(pplayer, unit_tile(actor),
                      event, ftc_server,
                      /* TRANS: action name.
                       * "Your Spy can't do Steal Gold from Ocean." */
                      _("Your %s can't do %s from %s."),
                      unit_name_translation(actor),
                      action_id_name_translation(stopped_action),
                      terrain_name_translation(explnat->no_act_terrain));
      }
    }
    break;
  case ANEK_BAD_TERRAIN_TGT:
    notify_player(pplayer, unit_tile(actor),
                  event, ftc_server,
                  /* TRANS: action name.
                   * "Your Spy can't do Industrial Sabotage to Mountains." */
                  _("Your %s can't do %s to %s."),
                  unit_name_translation(actor),
                  action_id_name_translation(stopped_action),
                  terrain_name_translation(explnat->no_act_terrain));
    break;
  case ANEK_IS_TRANSPORTED:
    notify_player(pplayer, unit_tile(actor),
                  event, ftc_server,
                  /* TRANS: action name.
                   * "Your Spy can't do Industrial Sabotage while ..." */
                  _("Your %s can't do %s while being transported."),
                  unit_name_translation(actor),
                  action_id_name_translation(stopped_action));
    break;
  case ANEK_IS_NOT_TRANSPORTED:
    notify_player(pplayer, unit_tile(actor),
                  event, ftc_server,
                  /* TRANS: action name.
                   * "Your Spy can't do Industrial Sabotage while ..." */
                  _("Your %s can't do %s while not being transported."),
                  unit_name_translation(actor),
                  action_id_name_translation(stopped_action));
    break;
  case ANEK_IS_TRANSPORTING:
    notify_player(pplayer, unit_tile(actor),
                  event, ftc_server,
                  /* TRANS: action name.
                   * "Your Spy can't do Industrial Sabotage while ..." */
                  _("Your %s can't do %s while transporting."),
                  unit_name_translation(actor),
                  action_id_name_translation(stopped_action));
    break;
  case ANEK_IS_NOT_TRANSPORTING:
    notify_player(pplayer, unit_tile(actor),
                  event, ftc_server,
                  /* TRANS: action name.
                   * "Your Spy can't do Industrial Sabotage while ..." */
                  _("Your %s can't do %s while not transporting."),
                  unit_name_translation(actor),
                  action_id_name_translation(stopped_action));
    break;
  case ANEK_ACTOR_HAS_HOME_CITY:
    notify_player(pplayer, unit_tile(actor),
                  event, ftc_server,
                  /* TRANS: action name.
                   * "Your Spy can't do Industrial Sabotage because ..." */
                  _("Your %s can't do %s because it has a home city."),
                  unit_name_translation(actor),
                  action_id_name_translation(stopped_action));
    break;
  case ANEK_ACTOR_HAS_NO_HOME_CITY:
    notify_player(pplayer, unit_tile(actor),
                  event, ftc_server,
                  /* TRANS: action name.
                   * "Your Spy can't do Industrial Sabotage because ..." */
                  _("Your %s can't do %s because it is homeless."),
                  unit_name_translation(actor),
                  action_id_name_translation(stopped_action));
    break;
  case ANEK_NO_WAR:
    notify_player(pplayer, unit_tile(actor),
                  event, ftc_server,
                  /* TRANS: action name.
                   * "Your Spy can't do Industrial Sabotage while you
                   * aren't at war with Prester John." */
                  _("Your %s can't do %s while you"
                    " aren't at war with %s."),
                  unit_name_translation(actor),
                  action_id_name_translation(stopped_action),
                  player_name(explnat->no_war_with));
    break;
  case ANEK_PEACE:
    notify_player(pplayer, unit_tile(actor),
                  event, ftc_server,
                  /* TRANS: action name.
                   * "Your Spy can't do Industrial Sabotage while you
                   * are at peace with Prester John. Try using the
                   * Nations report (F3)." */
                  _("Your %s can't do %s while you "
                    "are at peace with %s. Try using "
                    "the Nations report to declare war"
#ifndef FREECIV_WEB
                    " (F3)"
#endif /* FREECIV_WEB */
                    "."),
                  unit_name_translation(actor),
                  action_id_name_translation(stopped_action),
                  player_name(explnat->peace_with));
    break;
  case ANEK_DOMESTIC:
    notify_player(pplayer, unit_tile(actor),
                  event, ftc_server,
                  /* TRANS: action name.
                   * "Your Riflemen can't do Expel Unit to domestic 
                   * unit stacks." */
                  _("Your %s can't do %s to domestic %s."),
                  unit_name_translation(actor),
                  action_id_name_translation(stopped_action),
                  action_target_kind_translated_name(
                    action_id_get_target_kind(stopped_action)));
    break;
  case ANEK_FOREIGN:
    notify_player(pplayer, unit_tile(actor),
                  event, ftc_server,
                  /* TRANS: action name.
                   * "Your Leader can't do Use Court Physician to foreign
                   * unit stacks." */
                  _("Your %s can't do %s to foreign %s."),
                  unit_name_translation(actor),
                  action_id_name_translation(stopped_action),
                  action_target_kind_translated_name(
                    action_id_get_target_kind(stopped_action)));
    break;
  case ANEK_TGT_NON_ALLIED_UNITS_ON_TILE:
    notify_player(pplayer, unit_tile(actor),
                  event, ftc_server,
                  /* TRANS: Paratroopers ... Drop Paratrooper */
                  _("Your %s can't do %s to tiles with non allied units."),
                  unit_name_translation(actor),
                  action_id_name_translation(stopped_action));
    break;
  case ANEK_NATION_ACT:
    notify_player(pplayer, unit_tile(actor),
                  event, ftc_server,
                  /* TRANS: action name.
                   * "Swedish Riflemen can't do Expel Unit." */
                  _("%s %s can't do %s."),
                  nation_adjective_translation(explnat->no_act_nation),
                  unit_name_translation(actor),
                  action_id_name_translation(stopped_action));
    break;
  case ANEK_NATION_TGT:
    notify_player(pplayer, unit_tile(actor),
                  event, ftc_server,
                  /* TRANS: action name.
                   * "Riflemen... Expel Unit... Pirate Migrants." */
                  _("Your %s can't do %s to %s %s."),
                  unit_name_translation(actor),
                  action_id_name_translation(stopped_action),
                  nation_adjective_translation(explnat->no_act_nation),
                  action_target_kind_translated_name(
                    action_id_get_target_kind(stopped_action)));
    break;
  case ANEK_LOW_MP:
    notify_player(pplayer, unit_tile(actor),
                  event, ftc_server,
                  /* TRANS: action name.
                   * "Your Spy has ... to do Bribe Enemy Unit." */
                  _("Your %s has too few moves left to do %s."),
                  unit_name_translation(actor),
                  action_id_name_translation(stopped_action));
    break;
  case ANEK_IS_CITY_CENTER:
    notify_player(pplayer, unit_tile(actor),
                  event, ftc_server,
                  /* TRANS: action name.
                   * "Your Spy can't do Bribe Enemy Unit to city centers." */
                  _("Your %s can't do %s to city centers."),
                  unit_name_translation(actor),
                  action_id_name_translation(stopped_action));
    break;
  case ANEK_IS_NOT_CITY_CENTER:
    notify_player(pplayer, unit_tile(actor),
                  event, ftc_server,
                  /* TRANS: action name.
                   * "Your Spy can only do Investigate City to
                   * city centers." */
                  _("Your %s can only do %s to city centers."),
                  unit_name_translation(actor),
                  action_id_name_translation(stopped_action));
    break;
  case ANEK_TGT_IS_CLAIMED:
    notify_player(pplayer, unit_tile(actor),
                  event, ftc_server,
                  /* TRANS: action name.
                   * "Your Settlers can't do Build City to claimed tiles." */
                  _("Your %s can't do %s to claimed tiles."),
                  unit_name_translation(actor),
                  action_id_name_translation(stopped_action));
    break;
  case ANEK_TGT_IS_UNCLAIMED:
    notify_player(pplayer, unit_tile(actor),
                  event, ftc_server,
                  /* TRANS: action name.
                   * "Your Spy can't do Bribe Enemy Unit to
                   * unclaimed tiles." */
                  _("Your %s can't do %s to unclaimed tiles."),
                  unit_name_translation(actor),
                  action_id_name_translation(stopped_action));
    break;
  case ANEK_DISTANCE_NEAR:
    notify_player(pplayer, unit_tile(actor),
                  event, ftc_server,
                  /* TRANS: action name.
                   * "Your Spy must be at least 2 tiles away to do
                   * Incite a Revolt and Escape." */
                  PL_("Your %s must be at least %d tile away to do %s.",
                      "Your %s must be at least %d tiles away to do %s.",
                      explnat->distance),
                  unit_name_translation(actor),
                  explnat->distance,
                  action_id_name_translation(stopped_action));
    break;
  case ANEK_DISTANCE_FAR:
    notify_player(pplayer, unit_tile(actor),
                  event, ftc_server,
                  /* TRANS: action name.
                   * "Your Diplomat can't be more than 1 tile away to do
                   * Establish Embassy." */
                  PL_("Your %s can't be more than %d tile away to do %s.",
                      "Your %s can't be more than %d tiles away to do %s.",
                      explnat->distance),
                  unit_name_translation(actor),
                  explnat->distance,
                  action_id_name_translation(stopped_action));
    break;
  case ANEK_SCENARIO_DISABLED:
    notify_player(pplayer, unit_tile(actor),
                  event, ftc_server,
                  /* TRANS: Can't do Build City in this scenario. */
                  _("Can't do %s in this scenario."),
                  action_id_name_translation(stopped_action));
    break;
  case ANEK_CITY_TOO_CLOSE_TGT:
    notify_player(pplayer, unit_tile(actor),
                  event, ftc_server,
                  /* TRANS: Can't do Build City this close to a city. */
                  _("Can't do %s this close to a city."),
                  action_id_name_translation(stopped_action));
    break;
  case ANEK_CITY_TOO_BIG:
    notify_player(pplayer, unit_tile(actor),
                  event, ftc_server,
                  /* TRANS: Settlers ... Join City ... London */
                  _("%s can't do %s to %s. It is too big."),
                  unit_name_translation(actor),
                  action_id_name_translation(stopped_action),
                  city_name_get(target_city));
    break;
  case ANEK_CITY_POP_LIMIT:
    notify_player(pplayer, unit_tile(actor),
                  event, ftc_server,
                  /* TRANS: London ... Settlers ... Join City */
                  _("%s needs an improvement to grow, so "
                    "%s cannot do %s."),
                  city_name_get(target_city),
                  unit_name_translation(actor),
                  action_id_name_translation(stopped_action));
    break;
  case ANEK_CITY_NO_CAPACITY:
    notify_player(pplayer, unit_tile(actor),
                  event, ftc_server,
                  /* TRANS: Paris ... Airlift to City ... Warriors */
                  _("%s has no capacity to %s %s."),
                  city_name_get(explnat->capacity_city),
                  action_id_name_translation(stopped_action),
                  unit_name_translation(actor));
    break;
  case ANEK_TGT_TILE_UNKNOWN:
    notify_player(pplayer, unit_tile(actor),
                  event, ftc_server,
                  /* TRANS: Paratroopers ... Drop Paratrooper */
                  _("%s can't do %s to an unknown tile."),
                  unit_name_translation(actor),
                  action_id_name_translation(stopped_action));
    break;
  case ANEK_ACT_NOT_ENOUGH_MONEY:
    {
      char tbuf[MAX_LEN_MSG];

      /* TRANS: Used below. Separate so treasury content too can determine
       * if this is plural. */
      fc_snprintf(tbuf, ARRAY_SIZE(tbuf), PL_("Treasury contains %d gold.",
                                              "Treasury contains %d gold.",
                                              pplayer->economic.gold),
                  pplayer->economic.gold);

      notify_player(pplayer, unit_tile(actor),
                    event, ftc_server,
                    /* TRANS: "Spy can't do Bribe Unit for 154 gold.
                     * Treasury contains 100 gold." */
                    PL_("%s can't do %s for %d gold. %s",
                        "%s can't do %s for %d gold. %s",
                        explnat->gold_needed),
                    unit_name_translation(actor),
                    action_id_name_translation(stopped_action),
                    explnat->gold_needed, tbuf);
    }
    break;
  case ANEK_TRIREME_MOVE:
    notify_player(pplayer, target_tile, event, ftc_server,
                  /* TRANS: "Trireme cannot move ..." */
                  _("%s cannot move that far from the coast line."),
                  unit_link(actor));
    break;
  case ANEK_DISEMBARK_ACT:
    notify_player(pplayer, unit_tile(actor), event, ftc_server,
                  /* TRANS: "Riflemen cannot disembark ... native base
                   * for Helicopter." */
                  _("%s cannot disembark outside of a city or a native base "
                    "for %s."),
                  unit_link(actor),
                  utype_name_translation(
                      unit_type_get(unit_transport_get(actor))));
    break;
  case ANEK_TGT_UNREACHABLE:
    notify_player(pplayer, target_tile,
                  event, ftc_server,
                  /* TRANS: "Your Spy can't do Bribe Enemy Unit there ..." */
                  _("Your %s can't do %s there since there's an "
                    "unreachable unit."),
                  unit_name_translation(actor),
                  action_id_name_translation(stopped_action));
    break;
  case ANEK_TGT_IS_UNIQUE_ACT_HAS:
    notify_player(pplayer, target_tile, event, ftc_server,
                  /* TRANS: "You already have a Leader." */
                  _("You already have a %s."),
                  utype_name_translation(explnat->no_tgt_utype));
    break;
  case ANEK_ACTION_BLOCKS:
    {
      char *stop_act_name = fc_strdup(action_id_name_translation(stopped_action));

      notify_player(pplayer, unit_tile(actor),
                    event, ftc_server,
                    /* TRANS: Freight ... Disband Unit Recover ... Help Wonder ... */
                    _("Your %s can't do %s when %s is legal."),
                    unit_name_translation(actor),
                    stop_act_name,
                    action_id_name_translation(explnat->blocker->id));
      free(stop_act_name);
    }
    break;
  case ANEK_UNKNOWN:
    notify_player(pplayer, unit_tile(actor),
                  event, ftc_server,
                  /* TRANS: action name.
                   * "Your Spy was unable to do Bribe Enemy Unit." */
                  _("Your %s was unable to do %s."),
                  unit_name_translation(actor),
                  action_id_name_translation(stopped_action));
    break;
  }

  free(explnat);
}

/**********************************************************************//**
  Punish a player for trying to perform an action that turned out to be
  illegal. The punishment, if any at all, is specified by the ruleset.
  @param pplayer the player to punish.
  @param information_revealed if finding out that the action is illegal
                              reveals new information.
  @param act_unit the actor unit performing the action.
  @param stopped_action the illegal action.
  @param tgt_player the owner of the intended target of the action.
  @param tgt_tile the tile of the target of the action.
  @param requester who ordered the action performed?
  @return TRUE iff player was punished for trying to do the illegal action.
**************************************************************************/
static bool illegal_action_pay_price(struct player *pplayer,
                                     bool information_revealed,
                                     struct unit *act_unit,
                                     struct action *stopped_action,
                                     struct player *tgt_player,
                                     struct tile *tgt_tile,
                                     const enum action_requester requester)
{
  int punishment_mp;
  int punishment_hp;

  const struct req_context actor_ctxt = {
    .player = unit_owner(act_unit),
    .unit = act_unit,
    .unittype = unit_type_get(act_unit),
    .action = stopped_action,
  };

  /* Don't punish the player for something the game did. Don't tell the
   * player that the rules required the game to try to do something
   * illegal. */
  fc_assert_ret_val_msg((requester == ACT_REQ_PLAYER
                         || requester == ACT_REQ_SS_AGENT),
                        FALSE,
                        "The player wasn't responsible for this.");

  if (!information_revealed) {
    /* The player already had enough information to determine that this
     * action is illegal. Don't punish a client error or an accident. */
    return FALSE;
  }

  /* The mistake may have a cost. */

  /* HP cost */
  punishment_hp = get_target_bonus_effects(NULL, &actor_ctxt, tgt_player,
                                           EFT_ILLEGAL_ACTION_HP_COST);

  /* Stay in range */
  punishment_hp = MAX(0, punishment_hp);

  /* Punish the unit's hit points. */
  act_unit->hp = MAX(0, act_unit->hp - punishment_hp);

  if (punishment_hp != 0) {
    if (utype_is_moved_to_tgt_by_action(stopped_action,
                                        unit_type_get(act_unit))) {
      /* The consolation prize is some information about the potentially
       * distant target tile and maybe some contacts. */
      map_show_circle(pplayer, tgt_tile,
                      unit_type_get(act_unit)->vision_radius_sq);
      maybe_make_contact(tgt_tile, pplayer);
    }

    if (act_unit->hp > 0) {
      /* The actor unit survived */

      /* The player probably wants to be disturbed if their unit was punished
       * with the loss of hit points. */
      notify_player(pplayer, unit_tile(act_unit),
                    E_UNIT_ILLEGAL_ACTION, ftc_server,
                    /* TRANS: Spy ... 5 ... Drop Paratrooper */
                    _("Your %s lost %d hit points while attempting to"
                      " do %s."),
                    unit_name_translation(act_unit), punishment_hp,
                    action_name_translation(stopped_action));
      send_unit_info(NULL, act_unit);
    } else {
      /* The unit didn't survive */

      /* The player probably wants to be disturbed if their unit was punished
       * with death. */
      notify_player(pplayer, unit_tile(act_unit),
                    E_UNIT_ILLEGAL_ACTION, ftc_server,
                    /* TRANS: Spy ... Drop Paratrooper */
                    _("Your %s was killed while attempting to do %s."),
                    unit_name_translation(act_unit),
                    action_name_translation(stopped_action));

      wipe_unit(act_unit, ULR_KILLED, NULL);
      act_unit = NULL;

      return TRUE;
    }
  }

  /* MP cost */
  punishment_mp = get_target_bonus_effects(NULL, &actor_ctxt, tgt_player,
                                           EFT_ILLEGAL_ACTION_MOVE_COST);

  /* Stay in range */
  punishment_mp = MAX(0, punishment_mp);

  /* Punish the unit's move fragments. */
  act_unit->moves_left = MAX(0, act_unit->moves_left - punishment_mp);
  send_unit_info(NULL, act_unit);

  if (punishment_mp != 0) {
    /* The player probably wants to be disturbed if their unit was punished
     * with the loss of movement points. */
    notify_player(pplayer, unit_tile(act_unit),
                  E_UNIT_ILLEGAL_ACTION, ftc_server,
                  /* TRANS: Spy ... movement point text that may include
                   * fractions. */
                  _("Your %s lost %s MP for attempting an illegal action."),
                  unit_name_translation(act_unit),
                  move_points_text(punishment_mp, TRUE));
  }

  return punishment_mp != 0 || punishment_hp != 0;
}

/**********************************************************************//**
  Tell the client that the action it requested is illegal. This can be
  caused by the player (and therefore the client) not knowing that some
  condition of an action no longer is true.
**************************************************************************/
static void illegal_action(struct player *pplayer,
                           struct unit *actor,
                           action_id stopped_action_id,
                           struct player *tgt_player,
                           struct tile *target_tile,
                           const struct city *target_city,
                           const struct unit *target_unit,
                           int request_kind,
                           const enum action_requester requester)
{
  bool information_revealed;
  bool was_punished;
  const struct civ_map *nmap = &(wld.map);

  struct action *stopped_action = action_by_number(stopped_action_id);

  /* Why didn't the game check before trying something illegal? Did a good
   * reason to not call is_action_enabled_unit_on...() appear? The game is
   * omniscient... */
  fc_assert(requester != ACT_REQ_RULES);


  information_revealed = action_prob_possible(action_prob_unit_vs_tgt(
                                                 nmap,
                                                 stopped_action,
                                                 actor,
                                                 target_city, target_unit,
                                                 target_tile, NULL));

  if (request_kind == REQEST_PLAYER_INITIATED) {
    /* This is a foreground request. */
    illegal_action_msg(pplayer, (information_revealed
                                 ? E_UNIT_ILLEGAL_ACTION : E_BAD_COMMAND),
                       actor, stopped_action_id,
                       target_tile, target_city, target_unit);
  }

  was_punished = illegal_action_pay_price(pplayer, information_revealed,
                                          actor, stopped_action,
                                          tgt_player, target_tile,
                                          requester);

  if (request_kind != REQEST_PLAYER_INITIATED && was_punished) {
    /* FIXME: Temporary work around to prevent wrong information and/or
     * crashes. See hrm Bug #879880 */
    /* TODO: Get the explanation before the punishment and show it here.
     * See hrm Bug #879881 */
    notify_player(pplayer, unit_tile(actor),
                  (information_revealed
                   ? E_UNIT_ILLEGAL_ACTION : E_BAD_COMMAND), ftc_server,
                  _("No explanation why you couldn't do %s. This is a bug."
                    " Sorry about that. -- Sveinung"),
                  action_id_name_translation(stopped_action_id));
  }
}

/**********************************************************************//**
  Inform the client that something went wrong during a unit diplomat query
**************************************************************************/
static void unit_query_impossible(struct connection *pc,
                                  const int actor_id,
                                  const int target_id,
                                  int request_kind)
{
  dsend_packet_unit_action_answer(pc,
                                  actor_id, actor_id,
                                  target_id,
                                  0,
                                  ACTION_NONE,
                                  request_kind);
}

/**********************************************************************//**
  Tell the client the cost of bribing a unit, inciting a revolt, or
  any other parameters needed for action.

  Only send result back to the requesting connection, not all
  connections for that player.
**************************************************************************/
void handle_unit_action_query(struct connection *pc,
                              int actor_id16,
                              int actor_id32,
                              const int target_id,
                              const action_id action_type,
                              int request_kind)
{
  struct player *pplayer = pc->playing;
  struct unit *pactor;
  struct action *paction = action_by_number(action_type);
  struct unit *punit = game_unit_by_number(target_id);
  struct city *pcity = game_city_by_number(target_id);
  const struct civ_map *nmap = &(wld.map);

  if (!has_capability("ids32", pc->capability)) {
    actor_id32 = actor_id16;
  }

  pactor = player_unit_by_number(pplayer, actor_id32);

  if (NULL == paction) {
    /* Non existing action */
    log_error("handle_unit_action_query() the action %d doesn't exist.",
              action_type);

    unit_query_impossible(pc, actor_id32, target_id, request_kind);
    return;
  }

  if (NULL == pactor) {
    /* Probably died or bribed. */
    log_verbose("handle_unit_action_query() invalid actor %d",
                actor_id32);
    unit_query_impossible(pc, actor_id32, target_id, request_kind);
    return;
  }

  switch (paction->result) {
  case ACTRES_SPY_BRIBE_UNIT:
    if (punit
        && is_action_enabled_unit_on_unit(nmap, action_type,
                                          pactor, punit)) {
      dsend_packet_unit_action_answer(pc,
                                      actor_id32, actor_id32, target_id,
                                      unit_bribe_cost(punit, pplayer),
                                      action_type, request_kind);
    } else {
      illegal_action(pplayer, pactor, action_type,
                     punit ? unit_owner(punit) : NULL,
                     NULL, NULL, punit, request_kind, ACT_REQ_PLAYER);
      unit_query_impossible(pc, actor_id32, target_id, request_kind);
      return;
    }
    break;
  case ACTRES_SPY_INCITE_CITY:
    if (pcity
        && is_action_enabled_unit_on_city(nmap, action_type,
                                          pactor, pcity)) {
      dsend_packet_unit_action_answer(pc,
                                      actor_id32, actor_id32, target_id,
                                      city_incite_cost(pplayer, pcity),
                                      action_type, request_kind);
    } else {
      illegal_action(pplayer, pactor, action_type,
                     pcity ? city_owner(pcity) : NULL,
                     NULL, pcity, NULL, request_kind, ACT_REQ_PLAYER);
      unit_query_impossible(pc, actor_id32, target_id, request_kind);
      return;
    }
    break;
  case ACTRES_UPGRADE_UNIT:
    if (pcity
        && is_action_enabled_unit_on_city(nmap, action_type,
                                          pactor, pcity)) {
      const struct unit_type *tgt_utype;
      int upgr_cost;

      tgt_utype = can_upgrade_unittype(pplayer, unit_type_get(pactor));
      /* Already checked via is_action_enabled_unit_on_city() */
      fc_assert_ret(tgt_utype);
      upgr_cost = unit_upgrade_price(pplayer,
                                      unit_type_get(pactor), tgt_utype);

      dsend_packet_unit_action_answer(pc,
                                      actor_id32, actor_id32, target_id,
                                      upgr_cost, action_type,
                                      request_kind);
    } else {
      illegal_action(pplayer, pactor, action_type,
                     pcity ? city_owner(pcity) : NULL,
                     NULL, pcity, NULL, request_kind, ACT_REQ_PLAYER);
      unit_query_impossible(pc, actor_id32, target_id, request_kind);
      return;
    }
    break;
  case ACTRES_SPY_TARGETED_SABOTAGE_CITY:
  case ACTRES_STRIKE_BUILDING:
    if (pcity
        && is_action_enabled_unit_on_city(nmap, action_type,
                                          pactor, pcity)) {
      spy_send_sabotage_list(pc, pactor, pcity,
                             action_by_number(action_type), request_kind);
    } else {
      illegal_action(pplayer, pactor, action_type,
                     pcity ? city_owner(pcity) : NULL,
                     NULL, pcity, NULL, request_kind, ACT_REQ_PLAYER);
      unit_query_impossible(pc, actor_id32, target_id, request_kind);
      return;
    }
    break;
  default:
    unit_query_impossible(pc, actor_id32, target_id, request_kind);
    return;
  };
}

/**********************************************************************//**
  Handle a request to do an action.

  action_type must be a valid action.
**************************************************************************/
void handle_unit_do_action(struct player *pplayer,
                           const struct packet_unit_do_action *packet)
{
  if (!has_capability("ids32", pplayer->current_conn->capability)) {
    (void) unit_perform_action(pplayer, packet->actor_id16, packet->target_id,
                               packet->sub_tgt_id, packet->name,
                               packet->action_type, ACT_REQ_PLAYER);
  } else {
    (void) unit_perform_action(pplayer, packet->actor_id32, packet->target_id,
                               packet->sub_tgt_id, packet->name,
                               packet->action_type, ACT_REQ_PLAYER);
  }
}

/**********************************************************************//**
  Handle unit action

  action_type must be a valid action.
**************************************************************************/
void unit_do_action(struct player *pplayer,
                    const int actor_id,
                    const int target_id,
                    const int sub_tgt_id,
                    const char *name,
                    const action_id action_type)
{
  unit_perform_action(pplayer, actor_id, target_id,
                      sub_tgt_id, name, action_type, ACT_REQ_PLAYER);
}

/**********************************************************************//**
  Execute a request to perform an action and let the caller know if it was
  performed or not.

  The action must be a valid action.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
**************************************************************************/
bool unit_perform_action(struct player *pplayer,
                         const int actor_id,
                         const int target_id,
                         const int sub_tgt_id_incoming,
                         const char *name,
                         const action_id action_type,
                         const enum action_requester requester)
{
  struct action *paction;
  int sub_tgt_id;
  struct unit *actor_unit = player_unit_by_number(pplayer, actor_id);
  struct tile *target_tile = NULL;
  struct extra_type *target_extra;
  struct impr_type *sub_tgt_impr;
  struct unit *punit = NULL;
  struct city *pcity = NULL;
  const struct civ_map *nmap = &(wld.map);

  if (!action_id_exists(action_type)) {
    /* Non existing action */
    log_error("unit_perform_action() the action %d doesn't exist.",
              action_type);

    return FALSE;
  }

  paction = action_by_number(action_type);

  if (NULL == actor_unit) {
    /* Probably died or bribed. */
    log_verbose("unit_perform_action() invalid actor %d",
                actor_id);
    return FALSE;
  }

  switch (action_get_target_kind(paction)) {
  case ATK_CITY:
    pcity = game_city_by_number(target_id);
    if (pcity == NULL) {
      log_verbose("unit_perform_action() invalid target city %d",
                  target_id);
      return FALSE;
    }
    target_tile = city_tile(pcity);
    fc_assert_ret_val(target_tile != NULL, FALSE);
    break;
  case ATK_UNIT:
    punit = game_unit_by_number(target_id);
    if (punit == NULL) {
      log_verbose("unit_perform_action() invalid target unit %d",
                  target_id);
      return FALSE;
    }
    target_tile = unit_tile(punit);
    fc_assert_ret_val(target_tile != NULL, FALSE);
    pcity = tile_city(target_tile);
    break;
  case ATK_UNITS:
  case ATK_TILE:
  case ATK_EXTRAS:
    target_tile = index_to_tile(nmap, target_id);
    if (target_tile == NULL) {
      log_verbose("unit_perform_action() invalid target tile %d",
                  target_id);
      return FALSE;
    }
    pcity = tile_city(target_tile);
    break;
  case ATK_SELF:
    target_tile = unit_tile(actor_unit);
    fc_assert_ret_val(target_tile != NULL, FALSE);
    pcity = tile_city(target_tile);
    break;
  case ATK_COUNT:
    fc_assert_ret_val(action_get_target_kind(paction) != ATK_COUNT, FALSE);
    break;
  }

  /* Server side sub target assignment */
  if (paction->target_complexity == ACT_TGT_COMPL_FLEXIBLE
      && sub_tgt_id_incoming == NO_TARGET) {
    sub_tgt_id = action_sub_target_id_for_action(paction, actor_unit);
  } else {
    sub_tgt_id = sub_tgt_id_incoming;
  }

  if (sub_tgt_id >= 0 && sub_tgt_id < MAX_EXTRA_TYPES
      && sub_tgt_id != NO_TARGET) {
    target_extra = extra_by_number(sub_tgt_id);
    fc_assert(!(target_extra->ruledit_disabled));
  } else {
    target_extra = NULL;
  }

  sub_tgt_impr = improvement_by_number(sub_tgt_id);

  /* Sub targets should now be assigned */
  switch (paction->sub_target_kind) {
  case ASTK_NONE:
    /* No sub target. */
    break;
  case ASTK_BUILDING:
    if (sub_tgt_impr == NULL) {
      /* Missing sub target */
      return FALSE;
    }
    break;
  case ASTK_TECH:
    /* Not handled here yet */
    break;
  case ASTK_EXTRA:
  case ASTK_EXTRA_NOT_THERE:
    if (target_extra == NULL) {
      /* Missing sub target */
      return FALSE;
    }
    break;
  case ASTK_COUNT:
    break;
  }

  if (actres_get_activity(paction->result) != ACTIVITY_LAST
      && unit_activity_needs_target_from_client(
           actres_get_activity(paction->result))
      && target_extra == NULL) {
    /* Missing required action extra target. */
    log_verbose("unit_perform_action() action %d requires action "
                "but extra id %d is invalid.",
                action_type, sub_tgt_id);
    return FALSE;
  }

  if (paction->actor.is_unit.unitwaittime_controlled
      && !unit_can_do_action_now(actor_unit)) {
    /* Action not possible due to unitwaittime setting. */
    return FALSE;
  }

#define ACTION_PERFORM_UNIT_CITY(action, actor, target, action_performer) \
  if (pcity                                                               \
      && is_action_enabled_unit_on_city(nmap, action_type,                \
                                        actor_unit, pcity)) {             \
    bool success;                                                         \
    script_server_signal_emit("action_started_unit_city",                 \
                              action_by_number(action), actor, target);   \
    if (!actor || !unit_is_alive(actor_id)) {                             \
      /* Actor unit was destroyed during pre action Lua. */               \
      return FALSE;                                                       \
    }                                                                     \
    if (!target || !city_exist(target_id)) {                              \
      /* Target city was destroyed during pre action Lua. */              \
      return FALSE;                                                       \
    }                                                                     \
    success = action_performer;                                           \
    if (success) {                                                        \
      action_success_actor_price(paction, actor_id, actor);               \
    }                                                                     \
    script_server_signal_emit("action_finished_unit_city",                \
                              action_by_number(action), success,          \
                              unit_is_alive(actor_id) ? actor : NULL,     \
                              city_exist(target_id) ? target : NULL);     \
    return success;                                                       \
  } else {                                                                \
    illegal_action(pplayer, actor_unit, action_type,                      \
                   pcity ? city_owner(pcity) : NULL, NULL, pcity, NULL,   \
                   TRUE, requester);                                      \
  }

#define ACTION_PERFORM_UNIT_SELF(action, actor, action_performer)         \
  if (actor_unit                                                          \
      && is_action_enabled_unit_on_self(nmap, action_type, actor_unit)) { \
    bool success;                                                         \
    script_server_signal_emit("action_started_unit_self",                 \
                              action_by_number(action), actor);           \
    if (!actor || !unit_is_alive(actor_id)) {                             \
      /* Actor unit was destroyed during pre action Lua. */               \
      return FALSE;                                                       \
    }                                                                     \
    success = action_performer;                                           \
    if (success) {                                                        \
      action_success_actor_price(paction, actor_id, actor);               \
    }                                                                     \
    script_server_signal_emit("action_finished_unit_self",                \
                              action_by_number(action), success,          \
                              unit_is_alive(actor_id) ? actor : NULL);    \
    return success;                                                       \
  } else {                                                                \
    illegal_action(pplayer, actor_unit, action_type,                      \
                   unit_owner(actor_unit), NULL, NULL, actor_unit,        \
                   TRUE, requester);                                      \
  }

#define ACTION_PERFORM_UNIT_UNIT(action, actor, target, action_performer) \
  if (punit                                                               \
      && is_action_enabled_unit_on_unit(nmap, action_type, actor_unit, punit)) { \
    bool success;                                                         \
    script_server_signal_emit("action_started_unit_unit",                 \
                              action_by_number(action), actor, target);   \
    if (!actor || !unit_is_alive(actor_id)) {                             \
      /* Actor unit was destroyed during pre action Lua. */               \
      return FALSE;                                                       \
    }                                                                     \
    if (!target || !unit_is_alive(target_id)) {                           \
      /* Target unit was destroyed during pre action Lua. */              \
      return FALSE;                                                       \
    }                                                                     \
    success = action_performer;                                           \
    if (success) {                                                        \
      action_success_actor_price(paction, actor_id, actor);               \
      action_success_target_pay_mp(paction, target_id, punit);            \
    }                                                                     \
    script_server_signal_emit("action_finished_unit_unit",                \
                              action_by_number(action), success,          \
                              unit_is_alive(actor_id) ? actor : NULL,     \
                              unit_is_alive(target_id) ? target : NULL);  \
    return success;                                                       \
  } else {                                                                \
    illegal_action(pplayer, actor_unit, action_type,                      \
                   punit ? unit_owner(punit) : NULL, NULL, NULL, punit,   \
                   TRUE, requester);                                      \
  }

#define ACTION_PERFORM_UNIT_UNITS(action, actor, target, action_performer)\
  if (target_tile                                                         \
      && is_action_enabled_unit_on_units(nmap, action_type,               \
                                         actor_unit, target_tile)) {      \
    bool success;                                                         \
    script_server_signal_emit("action_started_unit_units",                \
                              action_by_number(action), actor, target);   \
    if (!actor || !unit_is_alive(actor_id)) {                             \
      /* Actor unit was destroyed during pre action Lua. */               \
      return FALSE;                                                       \
    }                                                                     \
    success = action_performer;                                           \
    if (success) {                                                        \
      action_success_actor_price(paction, actor_id, actor);               \
    }                                                                     \
    script_server_signal_emit("action_finished_unit_units",               \
                              action_by_number(action), success,          \
                              unit_is_alive(actor_id) ? actor : NULL,     \
                              target);                                    \
    return success;                                                       \
  } else {                                                                \
    illegal_action(pplayer, actor_unit, action_type,                      \
                   NULL, target_tile, NULL, NULL,                         \
                   TRUE, requester);                                      \
  }

#define ACTION_PERFORM_UNIT_TILE(action, actor, target, action_performer) \
  if (target_tile                                                         \
      && is_action_enabled_unit_on_tile(nmap, action_type,                \
                                        actor_unit, target_tile,          \
                                        target_extra)) {                  \
    bool success;                                                         \
    script_server_signal_emit("action_started_unit_tile",                 \
                              action_by_number(action), actor, target);   \
    if (!actor || !unit_is_alive(actor_id)) {                             \
      /* Actor unit was destroyed during pre action Lua. */               \
      return FALSE;                                                       \
    }                                                                     \
    success = action_performer;                                           \
    if (success) {                                                        \
      action_success_actor_price(paction, actor_id, actor);               \
    }                                                                     \
    script_server_signal_emit("action_finished_unit_tile",                \
                              action_by_number(action), success,          \
                              unit_is_alive(actor_id) ? actor : NULL,     \
                              target);                                    \
    return success;                                                       \
  } else {                                                                \
    illegal_action(pplayer, actor_unit, action_type,                      \
                   target_tile ? tile_owner(target_tile) : NULL,          \
                   target_tile, NULL, NULL,                               \
                   TRUE, requester);                                      \
  }

#define ACTION_PERFORM_UNIT_EXTRAS(action, actor, target, action_performer)\
  if (target_tile                                                         \
      && is_action_enabled_unit_on_extras(nmap, action_type,              \
                                          actor_unit, target_tile,        \
                                          target_extra)) {                \
    bool success;                                                         \
    script_server_signal_emit("action_started_unit_extras",               \
                              action_by_number(action), actor, target);   \
    if (!actor || !unit_is_alive(actor_id)) {                             \
      /* Actor unit was destroyed during pre action Lua. */               \
      return FALSE;                                                       \
    }                                                                     \
    success = action_performer;                                           \
    if (success) {                                                        \
      action_success_actor_price(paction, actor_id, actor);               \
    }                                                                     \
    script_server_signal_emit("action_finished_unit_extras",              \
                              action_by_number(action), success,          \
                              unit_is_alive(actor_id) ? actor : NULL,     \
                              target);                                    \
    return success;                                                       \
  } else {                                                                \
    illegal_action(pplayer, actor_unit, action_type,                      \
                   target_tile ? target_tile->extras_owner : NULL,        \
                   target_tile, NULL, NULL,                               \
                   TRUE, requester);                                      \
  }

#define ACTION_PERFORM_UNIT_ANY(paction, actor,                           \
                                target_city, target_unit, target_tile,    \
                                action_performer)                         \
  switch (action_get_target_kind(paction)) {                              \
  case ATK_CITY:                                                          \
    ACTION_PERFORM_UNIT_CITY(paction->id, actor, target_city,             \
                             action_performer);                           \
    break;                                                                \
  case ATK_UNIT:                                                          \
    ACTION_PERFORM_UNIT_UNIT(paction->id, actor, target_unit,             \
                             action_performer);                           \
    break;                                                                \
  case ATK_UNITS:                                                         \
    ACTION_PERFORM_UNIT_UNITS(paction->id, actor, target_tile,            \
                              action_performer);                          \
    break;                                                                \
  case ATK_TILE:                                                          \
    ACTION_PERFORM_UNIT_TILE(paction->id, actor, target_tile,             \
                             action_performer);                           \
    break;                                                                \
  case ATK_EXTRAS:                                                        \
    ACTION_PERFORM_UNIT_EXTRAS(paction->id, actor, target_tile,           \
                               action_performer);                         \
    break;                                                                \
  case ATK_SELF:                                                          \
    ACTION_PERFORM_UNIT_SELF(paction->id, actor, TRUE);                   \
    break;                                                                \
  case ATK_COUNT:                                                         \
    fc_assert(action_get_target_kind(paction) != ATK_COUNT);              \
    break;                                                                \
  }

  switch (paction->result) {
  case ACTRES_SPY_BRIBE_UNIT:
    ACTION_PERFORM_UNIT_UNIT(action_type, actor_unit, punit,
                             diplomat_bribe(pplayer, actor_unit, punit,
                                            paction));
    break;
  case ACTRES_SPY_SABOTAGE_UNIT:
    /* Difference is caused by data in the action structure. */
    ACTION_PERFORM_UNIT_UNIT(action_type, actor_unit, punit,
                             spy_sabotage_unit(pplayer, actor_unit,
                                               punit, paction));
    break;
  case ACTRES_EXPEL_UNIT:
    ACTION_PERFORM_UNIT_UNIT(action_type, actor_unit, punit,
                             do_expel_unit(pplayer, actor_unit, punit,
                                           paction));
    break;
  case ACTRES_HEAL_UNIT:
    ACTION_PERFORM_UNIT_UNIT(action_type, actor_unit, punit,
                             do_heal_unit(pplayer, actor_unit, punit,
                                          paction));
    break;
  case ACTRES_TRANSPORT_ALIGHT:
    ACTION_PERFORM_UNIT_UNIT(action_type, actor_unit, punit,
                             do_unit_alight(pplayer, actor_unit, punit,
                                            paction));
    break;
  case ACTRES_TRANSPORT_UNLOAD:
    ACTION_PERFORM_UNIT_UNIT(action_type, actor_unit, punit,
                             do_unit_unload(pplayer, actor_unit, punit,
                                            paction));
    break;
  case ACTRES_TRANSPORT_BOARD:
    ACTION_PERFORM_UNIT_UNIT(action_type, actor_unit, punit,
                             do_unit_board(pplayer, actor_unit, punit,
                                           paction));
    break;
  case ACTRES_TRANSPORT_EMBARK:
    ACTION_PERFORM_UNIT_UNIT(action_type, actor_unit, punit,
                             do_unit_embark(pplayer, actor_unit, punit,
                                            paction));
    break;
  case ACTRES_DISBAND_UNIT:
    /* All consequences are handled by the action system. */
    ACTION_PERFORM_UNIT_SELF(action_type, actor_unit, TRUE);
    break;
  case ACTRES_FORTIFY:
    ACTION_PERFORM_UNIT_SELF(action_type, actor_unit,
                             do_action_activity(actor_unit, paction));
    break;
  case ACTRES_CONVERT:
    ACTION_PERFORM_UNIT_SELF(action_type, actor_unit,
                             do_action_activity(actor_unit, paction));
    break;
  case ACTRES_HOMELESS:
    ACTION_PERFORM_UNIT_SELF(action_type, actor_unit,
                             do_unit_make_homeless(actor_unit, paction));
    break;
  case ACTRES_SPY_SABOTAGE_CITY:
    /* Difference is caused by data in the action structure. */
    ACTION_PERFORM_UNIT_CITY(action_type, actor_unit, pcity,
                             diplomat_sabotage(pplayer, actor_unit, pcity,
                                               B_LAST, paction));
    break;
  case ACTRES_SPY_TARGETED_SABOTAGE_CITY:
    /* Difference is caused by data in the action structure. */
    ACTION_PERFORM_UNIT_CITY(action_type, actor_unit, pcity,
                             diplomat_sabotage(pplayer, actor_unit, pcity,
                                               sub_tgt_impr->item_number,
                                               paction));
    break;
  case ACTRES_SPY_SABOTAGE_CITY_PRODUCTION:
    /* Difference is caused by data in the action structure. */
    ACTION_PERFORM_UNIT_CITY(action_type, actor_unit, pcity,
                             diplomat_sabotage(pplayer, actor_unit, pcity,
                                               -1, paction));
    break;
  case ACTRES_SPY_POISON:
    /* Difference is caused by data in the action structure. */
    ACTION_PERFORM_UNIT_CITY(action_type, actor_unit, pcity,
                             spy_poison(pplayer, actor_unit, pcity,
                                        paction));
    break;
  case ACTRES_SPY_SPREAD_PLAGUE:
    ACTION_PERFORM_UNIT_CITY(action_type, actor_unit, pcity,
                             spy_spread_plague(pplayer, actor_unit, pcity,
                                               paction));
    break;
  case ACTRES_SPY_INVESTIGATE_CITY:
    /* Difference is caused by data in the action structure. */
    ACTION_PERFORM_UNIT_CITY(action_type, actor_unit, pcity,
                             diplomat_investigate(pplayer,
                                                  actor_unit, pcity,
                                                  paction));
    break;
  case ACTRES_ESTABLISH_EMBASSY:
    /* Difference is caused by data in the action structure. */
    ACTION_PERFORM_UNIT_CITY(action_type, actor_unit, pcity,
                             diplomat_embassy(pplayer, actor_unit, pcity,
                                              paction));
    break;
  case ACTRES_SPY_INCITE_CITY:
    /* Difference is caused by data in the action structure. */
    ACTION_PERFORM_UNIT_CITY(action_type, actor_unit, pcity,
                             diplomat_incite(pplayer, actor_unit, pcity,
                                             paction));
    break;
  case ACTRES_SPY_STEAL_TECH:
    /* Difference is caused by data in the action structure. */
    ACTION_PERFORM_UNIT_CITY(action_type, actor_unit, pcity,
                             diplomat_get_tech(pplayer, actor_unit, pcity,
                                               A_UNSET, paction));
    break;
  case ACTRES_SPY_TARGETED_STEAL_TECH:
    /* Difference is caused by data in the action structure. */
    ACTION_PERFORM_UNIT_CITY(action_type, actor_unit, pcity,
                             diplomat_get_tech(pplayer, actor_unit, pcity,
                                               sub_tgt_id, paction));
    break;
  case ACTRES_SPY_STEAL_GOLD:
    /* Difference is caused by data in the action structure. */
    ACTION_PERFORM_UNIT_CITY(action_type, actor_unit, pcity,
                             spy_steal_gold(pplayer, actor_unit, pcity,
                                            paction));
    break;
  case ACTRES_STEAL_MAPS:
    /* Difference is caused by data in the action structure. */
    ACTION_PERFORM_UNIT_CITY(action_type, actor_unit, pcity,
                             spy_steal_some_maps(pplayer, actor_unit,
                                                 pcity, paction));
    break;
  case ACTRES_TRADE_ROUTE:
    ACTION_PERFORM_UNIT_CITY(action_type, actor_unit, pcity,
                             do_unit_establish_trade(pplayer, actor_unit,
                                                     pcity, paction));
    break;
  case ACTRES_MARKETPLACE:
    ACTION_PERFORM_UNIT_CITY(action_type, actor_unit, pcity,
                             do_unit_establish_trade(pplayer, actor_unit,
                                                     pcity, paction));
    break;
  case ACTRES_HELP_WONDER:
    ACTION_PERFORM_UNIT_CITY(action_type, actor_unit, pcity,
                             unit_do_help_build(pplayer,
                                                actor_unit, pcity,
                                                paction));
    break;
  case ACTRES_SPY_NUKE:
    /* Difference is caused by data in the action structure. */
    ACTION_PERFORM_UNIT_CITY(action_type, actor_unit, pcity,
                             spy_nuke_city(pplayer, actor_unit, pcity,
                                           paction));
    break;
  case ACTRES_JOIN_CITY:
    ACTION_PERFORM_UNIT_CITY(action_type, actor_unit, pcity,
                             city_add_unit(pplayer, actor_unit, pcity,
                                           paction));
    break;
  case ACTRES_DESTROY_CITY:
    ACTION_PERFORM_UNIT_CITY(action_type, actor_unit, pcity,
                             unit_do_destroy_city(pplayer,
                                                  actor_unit, pcity,
                                                  paction));
    break;
  case ACTRES_DISBAND_UNIT_RECOVER:
    ACTION_PERFORM_UNIT_CITY(action_type, actor_unit, pcity,
                             unit_do_help_build(pplayer, actor_unit, pcity,
                                                paction));
    break;
  case ACTRES_HOME_CITY:
    ACTION_PERFORM_UNIT_CITY(action_type, actor_unit, pcity,
                             do_unit_change_homecity(actor_unit, pcity,
                                                     paction));
    break;
  case ACTRES_UPGRADE_UNIT:
    ACTION_PERFORM_UNIT_CITY(action_type, actor_unit, pcity,
                             do_unit_upgrade(pplayer, actor_unit,
                                             pcity, requester, paction));
    break;
  case ACTRES_CONQUER_CITY:
    /* Difference is caused by the ruleset. ("Fake generalized" actions) */
    ACTION_PERFORM_UNIT_CITY(action_type, actor_unit, pcity,
                             do_unit_conquer_city(pplayer, actor_unit,
                                                  pcity, paction));
    break;
  case ACTRES_STRIKE_BUILDING:
    ACTION_PERFORM_UNIT_CITY(action_type, actor_unit, pcity,
                             do_unit_strike_city_building(pplayer,
                                                          actor_unit,
                                                          pcity,
                                                          sub_tgt_impr->item_number,
                                                          paction));
    break;
  case ACTRES_STRIKE_PRODUCTION:
    ACTION_PERFORM_UNIT_CITY(action_type, actor_unit, pcity,
                             do_unit_strike_city_production(pplayer,
                                                            actor_unit,
                                                            pcity,
                                                            paction));
    break;
  case ACTRES_CONQUER_EXTRAS:
    ACTION_PERFORM_UNIT_EXTRAS(action_type, actor_unit, target_tile,
                               do_conquer_extras(pplayer, actor_unit,
                                                 target_tile, paction));
    break;
  case ACTRES_AIRLIFT:
    ACTION_PERFORM_UNIT_CITY(action_type, actor_unit, pcity,
                             do_airline(actor_unit, pcity, paction));
    break;
  case ACTRES_CAPTURE_UNITS:
    ACTION_PERFORM_UNIT_UNITS(action_type, actor_unit, target_tile,
                              do_capture_units(pplayer, actor_unit,
                                               target_tile, paction));
    break;
  case ACTRES_BOMBARD:
    /* Difference is caused by the ruleset. ("Fake generalized" actions) */
    ACTION_PERFORM_UNIT_UNITS(action_type, actor_unit, target_tile,
                              unit_bombard(actor_unit, target_tile,
                                           paction));
    break;
  case ACTRES_ATTACK:
    /* Difference is caused by data in the action structure. */
    ACTION_PERFORM_UNIT_UNITS(action_type, actor_unit, target_tile,
                              do_attack(actor_unit, target_tile, paction));
    break;
  case ACTRES_NUKE_UNITS:
    ACTION_PERFORM_UNIT_UNITS(action_type, actor_unit, target_tile,
                              unit_nuke(pplayer, actor_unit, target_tile,
                                        paction));
    break;
  case ACTRES_SPY_ATTACK:
    ACTION_PERFORM_UNIT_UNITS(action_type, actor_unit, target_tile,
                              spy_attack(pplayer, actor_unit, target_tile,
                                         paction));
    break;
  case ACTRES_FOUND_CITY:
    ACTION_PERFORM_UNIT_TILE(action_type, actor_unit, target_tile,
                             city_build(pplayer, actor_unit,
                                        target_tile, name, paction));
    break;
  case ACTRES_NUKE:
    ACTION_PERFORM_UNIT_ANY(paction, actor_unit,
                            pcity, punit, target_tile,
                            unit_nuke(pplayer, actor_unit, target_tile,
                                      paction));
    break;
  case ACTRES_PARADROP:
  case ACTRES_PARADROP_CONQUER:
    ACTION_PERFORM_UNIT_TILE(action_type, actor_unit, target_tile,
                             do_paradrop(actor_unit, target_tile, paction));
    break;
  case ACTRES_TRANSPORT_DISEMBARK:
    /* Difference is caused by the ruleset. ("Fake generalized" actions) */
    ACTION_PERFORM_UNIT_TILE(action_type, actor_unit, target_tile,
                             do_disembark(pplayer, actor_unit,
                                          target_tile, paction));
    break;
  case ACTRES_HUT_ENTER:
    ACTION_PERFORM_UNIT_TILE(action_type, actor_unit, target_tile,
                             do_unit_hut(pplayer, actor_unit,
                                         target_tile, paction));
    break;
  case ACTRES_HUT_FRIGHTEN:
    ACTION_PERFORM_UNIT_TILE(action_type, actor_unit, target_tile,
                             do_unit_hut(pplayer, actor_unit,
                                         target_tile, paction));
    break;
  case ACTRES_UNIT_MOVE:
    ACTION_PERFORM_UNIT_TILE(action_type, actor_unit, target_tile,
                             unit_do_regular_move(pplayer, actor_unit,
                                                  target_tile, paction));
    break;
  case ACTRES_TRANSFORM_TERRAIN:
    ACTION_PERFORM_UNIT_TILE(action_type, actor_unit, target_tile,
                             do_action_activity(actor_unit, paction));
    break;
  case ACTRES_CULTIVATE:
    ACTION_PERFORM_UNIT_TILE(action_type, actor_unit, target_tile,
                             do_action_activity(actor_unit, paction));
    break;
  case ACTRES_PLANT:
    ACTION_PERFORM_UNIT_TILE(action_type, actor_unit, target_tile,
                             do_action_activity(actor_unit, paction));
    break;
  case ACTRES_PILLAGE:
    ACTION_PERFORM_UNIT_ANY(paction, actor_unit, pcity, punit, target_tile,
                            do_action_activity_targeted(actor_unit,
                                                        paction,
                                                        &target_extra));
    break;
  case ACTRES_CLEAN_POLLUTION:
    ACTION_PERFORM_UNIT_TILE(action_type, actor_unit, target_tile,
                             do_action_activity_targeted(actor_unit,
                                                         paction,
                                                         &target_extra));
    break;
  case ACTRES_CLEAN_FALLOUT:
    ACTION_PERFORM_UNIT_TILE(action_type, actor_unit, target_tile,
                             do_action_activity_targeted(actor_unit,
                                                         paction,
                                                         &target_extra));
    break;
  case ACTRES_ROAD:
    ACTION_PERFORM_UNIT_TILE(action_type, actor_unit, target_tile,
                             do_action_activity_targeted(actor_unit,
                                                         paction,
                                                         &target_extra));
    break;
  case ACTRES_BASE:
    ACTION_PERFORM_UNIT_TILE(action_type, actor_unit, target_tile,
                             do_action_activity_targeted(actor_unit,
                                                         paction,
                                                         &target_extra));
    break;
  case ACTRES_MINE:
    ACTION_PERFORM_UNIT_TILE(action_type, actor_unit, target_tile,
                             do_action_activity_targeted(actor_unit,
                                                         paction,
                                                         &target_extra));
    break;
  case ACTRES_IRRIGATE:
    ACTION_PERFORM_UNIT_TILE(action_type, actor_unit, target_tile,
                             do_action_activity_targeted(actor_unit,
                                                         paction,
                                                         &target_extra));
    break;
  case ACTRES_NONE:
    /* 100% ruleset defined. */
    ACTION_PERFORM_UNIT_ANY(paction, actor_unit, pcity, punit, target_tile,
                            TRUE);
    break;
  }

  /* Something must have gone wrong. */
  return FALSE;
}

/**********************************************************************//**
  Transfer a unit from one city (and possibly player) to another.
  If 'rehome' is not set, only change the player which owns the unit
  (the new owner is new_pcity's owner). Otherwise the new unit will be
  given a homecity, even if it was homeless before.
  This new homecity must be valid for this unit.
**************************************************************************/
void unit_change_homecity_handling(struct unit *punit, struct city *new_pcity,
                                   bool rehome)
{
  struct city *old_pcity = game_city_by_number(punit->homecity);
  struct player *old_owner = unit_owner(punit);
  struct player *new_owner = (new_pcity == NULL ? old_owner
                                                : city_owner(new_pcity));
  const struct civ_map *nmap = &(wld.map);

  /* Calling this function when new_pcity is same as old_pcity should
   * be safe with current implementation, but it is not meant to
   * be used that way. */
  fc_assert_ret(new_pcity != old_pcity);

  /* If 'rehome' is not set, this function should only be used to change
   * which player owns the unit */
  fc_assert_ret(rehome || new_owner != old_owner);

  if (old_owner != new_owner) {
    struct city *pcity = tile_city(punit->tile);

    fc_assert(!utype_player_already_has_this_unique(new_owner,
                                                    unit_type_get(punit)));

    vision_clear_sight(punit->server.vision);
    vision_free(punit->server.vision);

    if (pcity != NULL
        && !can_player_see_units_in_city(old_owner, pcity)) {
      /* Special case when city is being transferred. At this point city
       * itself has changed owner, so it's enemy city now that old owner
       * cannot see inside. All the normal methods of removing transferred
       * unit from previous owner's client think that there's no need to
       * remove unit as client shouldn't have it in first place. */
      unit_goes_out_of_sight(old_owner, punit);
    }

    /* Remove AI control of the old owner. */
    CALL_PLR_AI_FUNC(unit_lost, old_owner, punit);

    unit_list_remove(old_owner->units, punit);
    unit_list_prepend(new_owner->units, punit);
    punit->owner = new_owner;

    /* Activate AI control of the new owner. */
    CALL_PLR_AI_FUNC(unit_got, new_owner, punit);

    punit->server.vision = vision_new(new_owner, unit_tile(punit));
    unit_refresh_vision(punit);
  }

  if (rehome) {
    /* Remove from old city first and add to new city only after that. */
    if (old_pcity) {
      /* Even if unit is dead, we have to unlink unit pointer (punit). */
      unit_list_remove(old_pcity->units_supported, punit);
      /* update unit upkeep */
      city_units_upkeep(old_pcity);
    }

    if (new_pcity != NULL) {
      unit_list_prepend(new_pcity->units_supported, punit);

      /* update unit upkeep */
      city_units_upkeep(new_pcity);

      punit->homecity = new_pcity->id;
    } else {
      punit->homecity = IDENTITY_NUMBER_ZERO;
    }
  }

  if (!can_unit_continue_current_activity(nmap, punit)) {
    /* This is mainly for cases where unit owner changes to one not knowing
     * Railroad tech when unit is already building railroad.
     * Does also send_unit_info() */
    unit_activities_cancel(punit);
  } else {
    /* Send info to players and observers. */
    send_unit_info(NULL, punit);
  }

  if (new_pcity != NULL) {
    city_refresh(new_pcity);
    send_city_info(new_owner, new_pcity);
    fc_assert(unit_owner(punit) == city_owner(new_pcity));
  }

  if (old_pcity) {
    fc_assert(city_owner(old_pcity) == old_owner);
    city_refresh(old_pcity);
    send_city_info(old_owner, old_pcity);
  }

  unit_get_goods(punit);
}

/**********************************************************************//**
  Change a unit's home city.

  Returns TRUE iff the action could be done, FALSE if it couldn't.
**************************************************************************/
static bool do_unit_change_homecity(struct unit *punit,
                                    struct city *pcity,
                                    const struct action *paction)
{
  const char *giver = NULL;

  if (unit_owner(punit) != city_owner(pcity)) {
    /* This is a gift. Tell the receiver. */
    giver = player_name(unit_owner(punit));
  }

  unit_change_homecity_handling(punit, pcity, TRUE);

  if (punit->homecity == pcity->id && giver) {
    /* Notify the city owner about the gift they received. */
    notify_player(city_owner(pcity), city_tile(pcity), E_UNIT_BUILT,
                  ftc_server,
                  /* TRANS: other player ... unit type ... city name. */
                  _("%s transferred control over a %s to you in %s."),
                  giver,
                  unit_tile_link(punit),
                  city_link(pcity));;
  }

  return punit->homecity == pcity->id;
}

/**********************************************************************//**
  This function assumes that the target city is valid. It should only be
  called after checking that the unit legally can join the target city.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
**************************************************************************/
static bool city_add_unit(struct player *pplayer, struct unit *punit,
                          struct city *pcity, const struct action *paction)
{
  int amount = unit_pop_value(punit);
  const struct unit_type *act_utype;

  /* Sanity check: The actor is still alive. */
  fc_assert_ret_val(punit, FALSE);

  act_utype = unit_type_get(punit);

  /* Sanity check: The target city still exists. */
  fc_assert_ret_val(pcity, FALSE);

  fc_assert_ret_val(amount > 0, FALSE);

  city_size_add(pcity, amount);
  /* Make the new people something, otherwise city fails the checks */
  pcity->specialists[DEFAULT_SPECIALIST] += amount;
  citizens_update(pcity, unit_nationality(punit));
  /* Refresh the city data. */
  city_refresh(pcity);

  /* Notify the unit owner that the unit successfully joined the city. */
  notify_player(pplayer, city_tile(pcity), E_CITY_BUILD, ftc_server,
                _("%s added to aid %s in growing."),
                unit_tile_link(punit),
                city_link(pcity));
  if (pplayer != city_owner(pcity)) {
    /* Notify the city owner when a foreign unit joins a city. */
    notify_player(city_owner(pcity), city_tile(pcity), E_CITY_BUILD,
                  ftc_server,
                  /* TRANS: another player had their unit join your city. */
                  _("%s adds %s to your city %s."),
                  player_name(unit_owner(punit)),
                  unit_tile_link(punit),
                  city_link(pcity));;
  }

  action_consequence_success(paction, pplayer, act_utype,
                             city_owner(pcity), city_tile(pcity),
                             city_link(pcity));

  sanity_check_city(pcity);

  send_city_info(NULL, pcity);

  script_server_signal_emit("city_size_change", pcity,
                            (lua_Integer)amount, "unit_added");

  return TRUE;
}

/**********************************************************************//**
  This function assumes a certain level of consistency checking: There
  is no city under punit->(x, y), and that location is a valid one on
  which to build a city. It should only be called after a call to a
  function like test_unit_add_or_build_city, which does the checking.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
**************************************************************************/
static bool city_build(struct player *pplayer, struct unit *punit,
                       struct tile *ptile, const char *name,
                       const struct action *paction)
{
  char message[1024];
  int size;
  struct player *nationality;
  struct player *towner;
  const struct unit_type *act_utype;

  /* Sanity check: The actor still exists. */
  fc_assert_ret_val(pplayer, FALSE);
  fc_assert_ret_val(punit, FALSE);

  act_utype = unit_type_get(punit);
  towner = tile_owner(ptile);

  if (!is_allowed_city_name(pplayer, name, message, sizeof(message))) {
    notify_player(pplayer, ptile, E_BAD_COMMAND, ftc_server,
                  "%s", message);
    return FALSE;
  }

  nationality = unit_nationality(punit);

  create_city(pplayer, ptile, name, nationality);
  size = unit_type_get(punit)->city_size;
  if (size > 1) {
    struct city *pcity = tile_city(ptile);

    fc_assert_ret_val(pcity != NULL, FALSE);

    city_change_size(pcity, size, nationality, NULL);
  }

  /* May cause an incident even if the target tile is unclaimed. A ruleset
   * could give everyone a casus belli against the city founder. A rule
   * like that would make sense in a story where deep ecology is on the
   * table. (See also Voluntary Human Extinction Movement) */
  action_consequence_success(paction, pplayer, act_utype, towner,
                             ptile, tile_link(ptile));

  return TRUE;
}

/**********************************************************************//**
  Handle change in unit activity.
**************************************************************************/
static void handle_unit_change_activity_real(struct player *pplayer,
                                             int unit_id,
                                             enum unit_activity activity,
                                             struct extra_type *activity_target)
{
  struct unit *punit = player_unit_by_number(pplayer, unit_id);

  if (NULL == punit) {
    /* Probably died or bribed. */
    log_verbose("handle_unit_change_activity() invalid unit %d", unit_id);
    return;
  }

  if (punit->activity == activity
      && punit->activity_target == activity_target) {
    return;
  }

  /* Remove city spot reservations for AI settlers on city founding
   * mission, before goto_tile reset. */
  if (punit->server.adv->task != AUT_NONE) {
    adv_unit_new_task(punit, AUT_NONE, NULL);
  }

  punit->goto_tile = NULL;

  if (activity == ACTIVITY_GOTO) {
    /* Don't permit a client to set a unit's activity to ACTIVITY_GOTO.
     * Setting ACTIVITY_GOTO from the client results in a unit indicating
     * it is going somewhere while it is standing still. The appearance of
     * the unit doing something can trick the user to not make use of it.
     *
     * Handled here because adv_follow_path() uses unit_activity_handling()
     * to set a unit's activity to ACTIVITY_GOTO. */
    return;
  }

  if (activity == ACTIVITY_EXPLORE) {
    /* Please use unit_server_side_agent_set. */
    return;
  }

  /* The activity can now be set. */
  unit_activity_handling_targeted(punit, activity, &activity_target);
}

/**********************************************************************//**
  Handle change in unit activity.
**************************************************************************/
void handle_unit_change_activity(struct player *pplayer,
                                 int unit_id16, int unit_id32,
                                 enum unit_activity activity,
                                 int target_id)
{
  struct extra_type *activity_target;
  int unit_id;

  if (!has_capability("ids32", pplayer->current_conn->capability)) {
    unit_id = unit_id16;
  } else {
    unit_id = unit_id32;
  }

  if (target_id < 0 || target_id >= game.control.num_extra_types) {
    activity_target = NULL;
  } else {
    activity_target = extra_by_number(target_id);
  }

#ifdef FREECIV_WEB
  /* Web-client is not capable of selecting target, so we do it server side */
  if (activity_target == NULL) {
    struct unit *punit = player_unit_by_number(pplayer, unit_id);
    bool required = TRUE;

    if (punit == NULL) {
      return;
    }

    if (activity == ACTIVITY_IRRIGATE) {
      struct tile *ptile = unit_tile(punit);

      activity_target = next_extra_for_tile(ptile, EC_IRRIGATION,
                                            pplayer, punit);
    } else if (activity == ACTIVITY_MINE) {
      struct tile *ptile = unit_tile(punit);

      activity_target = next_extra_for_tile(ptile, EC_MINE,
                                            pplayer, punit);
    } else if (activity == ACTIVITY_BASE) {
      struct tile *ptile = unit_tile(punit);
      struct base_type *pbase =
        get_base_by_gui_type(BASE_GUI_FORTRESS, punit, ptile);

      if (pbase != NULL) {
        activity_target = base_extra_get(pbase);
      }

    } else if (activity == ACTIVITY_POLLUTION) {
      activity_target = prev_extra_in_tile(unit_tile(punit), ERM_CLEANPOLLUTION,
                                           pplayer, punit);
    } else if (activity == ACTIVITY_FALLOUT) {
      activity_target = prev_extra_in_tile(unit_tile(punit), ERM_CLEANFALLOUT,
                                           pplayer, punit);
    } else {
      required = FALSE;
    }

    if (activity_target == NULL && required) {
      /* Nothing more we can do */
      return;
    }
  }
#endif /* FREECIV_WEB */

  handle_unit_change_activity_real(pplayer, unit_id, activity, activity_target);
}

/**********************************************************************//**
  Make sure everyone who can see combat does.
**************************************************************************/
static void see_combat(struct unit *pattacker, struct unit *pdefender)
{
  struct packet_unit_short_info unit_att_short_packet, unit_def_short_packet;
  struct packet_unit_info unit_att_packet, unit_def_packet;

  /* 
   * Special case for attacking/defending:
   * 
   * Normally the player doesn't get the information about the units inside a
   * city. However for attacking/defending the player has to know the unit of
   * the other side.  After the combat a remove_unit packet will be sent
   * to the client to tidy up.
   *
   * Note these packets must be sent out before unit_versus_unit() is called,
   * so that the original unit stats (HP) will be sent.
   */
  package_short_unit(pattacker, &unit_att_short_packet,
                     UNIT_INFO_IDENTITY, 0);
  package_short_unit(pdefender, &unit_def_short_packet,
                     UNIT_INFO_IDENTITY, 0);
  package_unit(pattacker, &unit_att_packet);
  package_unit(pdefender, &unit_def_packet);

  conn_list_iterate(game.est_connections, pconn) {
    struct player *pplayer = pconn->playing;

    if (pplayer != NULL) {

      /* NOTE: this means the player can see combat between submarines even
       * if neither sub is visible.  See similar comment in send_combat. */
      if (map_is_known_and_seen(unit_tile(pattacker), pplayer, V_MAIN)
          || map_is_known_and_seen(unit_tile(pdefender), pplayer,
                                   V_MAIN)) {

        /* Units are sent even if they were visible already. They may
         * have changed orientation for combat. */
        if (pplayer == unit_owner(pattacker)) {
          send_packet_unit_info(pconn, &unit_att_packet);
        } else {
          send_packet_unit_short_info(pconn, &unit_att_short_packet, FALSE);
        }
        
        if (pplayer == unit_owner(pdefender)) {
          send_packet_unit_info(pconn, &unit_def_packet);
        } else {
          send_packet_unit_short_info(pconn, &unit_def_short_packet, FALSE);
        }
      }
    } else if (pconn->observer) {
      /* Global observer sees everything... */
      send_packet_unit_info(pconn, &unit_att_packet);
      send_packet_unit_info(pconn, &unit_def_packet);
    }
  } conn_list_iterate_end;
}

/**********************************************************************//**
  Send combat info to players.
**************************************************************************/
static void send_combat(struct unit *pattacker, struct unit *pdefender, 
                        int att_veteran, int def_veteran, int bombard)
{
  struct packet_unit_combat_info combat;

  combat.attacker_unit_id32 = pattacker->id;
  combat.attacker_unit_id16 = combat.attacker_unit_id32;
  combat.defender_unit_id32 = pdefender->id;
  combat.defender_unit_id16 = combat.defender_unit_id32;
  combat.attacker_hp = pattacker->hp;
  combat.defender_hp = pdefender->hp;
  combat.make_att_veteran = att_veteran;
  combat.make_def_veteran = def_veteran;

  players_iterate(other_player) {
    /* NOTE: this means the player can see combat between submarines even
     * if neither sub is visible.  See similar comment in see_combat. */
    if (map_is_known_and_seen(unit_tile(pattacker), other_player, V_MAIN)
        || map_is_known_and_seen(unit_tile(pdefender), other_player,
                                 V_MAIN)) {
      lsend_packet_unit_combat_info(other_player->connections, &combat);

      /* 
       * Remove the client knowledge of the units.  This corresponds to the
       * send_packet_unit_short_info calls up above.
       */
      if (!can_player_see_unit(other_player, pattacker)) {
	unit_goes_out_of_sight(other_player, pattacker);
      }
      if (!can_player_see_unit(other_player, pdefender)) {
	unit_goes_out_of_sight(other_player, pdefender);
      }
    }
  } players_iterate_end;

  /* Send combat info to non-player observers as well.  They already know
   * about the unit so no unit_info is needed. */
  conn_list_iterate(game.est_connections, pconn) {
    if (NULL == pconn->playing && pconn->observer) {
      send_packet_unit_combat_info(pconn, &combat);
    }
  } conn_list_iterate_end;
}

/**********************************************************************//**
  This function assumes the bombard is legal. The calling function should
  have already made all necessary checks.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
**************************************************************************/
static bool unit_bombard(struct unit *punit, struct tile *ptile,
                         const struct action *paction)
{
  struct player *pplayer = unit_owner(punit);
  struct city *pcity = tile_city(ptile);
  const struct unit_type *act_utype;
  const struct civ_map *nmap = &(wld.map);

  /* Sanity check: The actor still exists. */
  fc_assert_ret_val(pplayer, FALSE);
  fc_assert_ret_val(punit, FALSE);

  act_utype = unit_type_get(punit);

  log_debug("Start bombard: %s %s to %d, %d.",
            nation_rule_name(nation_of_player(pplayer)),
            unit_rule_name(punit), TILE_XY(ptile));

  unit_list_iterate_safe(ptile->units, pdefender) {
    if (is_unit_reachable_at(pdefender, punit, ptile)) {
      bool adj;
      enum direction8 facing;
      int att_hp, def_hp;

      adj = base_get_direction_for_step(nmap,
                                        punit->tile, pdefender->tile, &facing);

      if (adj) {
        punit->facing = facing;

        /* Unlike with normal attack, we don't change orientation of
         * defenders when bombarding */
      }

      unit_bombs_unit(punit, pdefender, &att_hp, &def_hp);

      notify_player(pplayer, ptile,
                    E_UNIT_WIN_ATT, ftc_server,
                    /* TRANS: Your Bomber bombards the English Rifleman.*/
                    _("Your %s bombards the %s %s."),
                    unit_name_translation(punit),
                    nation_adjective_for_player(unit_owner(pdefender)),
                    unit_name_translation(pdefender));

      notify_player(unit_owner(pdefender), ptile,
                    E_UNIT_WIN_DEF, ftc_server,
                    /* TRANS: Your Rifleman is bombarded by the French Bomber.*/
                    _("Your %s is bombarded by the %s %s."),
                    unit_name_translation(pdefender),
                    nation_adjective_for_player(pplayer),
                    unit_name_translation(punit));

      see_combat(punit, pdefender);

      punit->hp = att_hp;
      pdefender->hp = def_hp;

      send_combat(punit, pdefender, 0, 0, 1);
  
      send_unit_info(NULL, pdefender);

      /* May cause an incident */
      action_consequence_success(paction,
                                 unit_owner(punit), act_utype,
                                 unit_owner(pdefender),
                                 unit_tile(pdefender),
                                 unit_link(pdefender));
    }

  } unit_list_iterate_safe_end;

  unit_did_action(punit);
  unit_forget_last_activity(punit);
  
  if (pcity
      && city_size_get(pcity) > 1
      && get_city_bonus(pcity, EFT_UNIT_NO_LOSE_POP) <= 0
      && kills_citizen_after_attack(punit)) {
    city_reduce_size(pcity, 1, pplayer, "bombard");
    city_refresh(pcity);
    send_city_info(NULL, pcity);
  }

  send_unit_info(NULL, punit);

  return TRUE;
}

/**********************************************************************//**
  Do a "regular" nuclear attack.

  Can be stopped by an EFT_NUKE_PROOF (SDI defended) city.

  This function assumes the attack is legal. The calling function should
  have already made all necessary checks.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
**************************************************************************/
static bool unit_nuke(struct player *pplayer, struct unit *punit,
                      struct tile *def_tile, const struct action *paction)
{
  struct city *pcity;
  const struct unit_type *act_utype;
  struct civ_map *nmap = &(wld.map);

  /* Sanity check: The actor still exists. */
  fc_assert_ret_val(pplayer, FALSE);
  fc_assert_ret_val(punit, FALSE);

  act_utype = unit_type_get(punit);

  log_debug("Start nuclear attack: %s %s against (%d, %d).",
            nation_rule_name(nation_of_player(pplayer)),
            unit_rule_name(punit),
            TILE_XY(def_tile));

  if ((pcity = sdi_try_defend(nmap, pplayer, def_tile))) {
    /* FIXME: Remove the hard coded reference to SDI defense. */
    notify_player(pplayer, unit_tile(punit), E_UNIT_LOST_ATT, ftc_server,
                  _("Your %s was shot down by "
                    "SDI defenses, what a waste."), unit_tile_link(punit));
    notify_player(city_owner(pcity), def_tile, E_UNIT_WIN_DEF, ftc_server,
                  _("The nuclear attack on %s was avoided by"
                    " your SDI defense."), city_link(pcity));

    /* Trying to nuke something this close can be... unpopular. */
    action_consequence_caught(paction, pplayer, act_utype,
                              city_owner(pcity),
                              def_tile, unit_tile_link(punit));

    /* Remove the destroyed nuke. */
    wipe_unit(punit, ULR_SDI, city_owner(pcity));

    return FALSE;
  }

  dlsend_packet_nuke_tile_info(game.est_connections, tile_index(def_tile));


  /* The nuke must be wiped here so it won't be seen as a victim of its own
   * detonation. */
  if (paction->actor_consuming_always) {
    wipe_unit(punit, ULR_DETONATED, NULL);
  }

  do_nuclear_explosion(pplayer, def_tile);

  /* May cause an incident even if the target tile is unclaimed. A ruleset
   * could give everyone a casus belli against the tile nuker. A rule
   * like that would make sense in a story where detonating any nuke at all
   * could be forbidden. */
  action_consequence_success(paction, pplayer, act_utype,
                             tile_owner(def_tile),
                             def_tile,
                             tile_link(def_tile));

  return TRUE;
}

/**********************************************************************//**
  Destroy the target city.

  This function assumes the destruction is legal. The calling function
  should have already made all necessary checks.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
**************************************************************************/
static bool unit_do_destroy_city(struct player *act_player,
                                 struct unit *act_unit,
                                 struct city *tgt_city,
                                 const struct action *paction)
{
  int tgt_city_id;
  struct player *tgt_player;
  bool capital;
  bool try_civil_war = FALSE;
  const struct unit_type *act_utype;

  /* Sanity check: The actor still exists. */
  fc_assert_ret_val(act_player, FALSE);
  fc_assert_ret_val(act_unit, FALSE);

  act_utype = unit_type_get(act_unit);

  /* Sanity check: The target city still exists. */
  fc_assert_ret_val(tgt_city, FALSE);

  tgt_player = city_owner(tgt_city);

  /* How can a city be ownerless? */
  fc_assert_ret_val(tgt_player, FALSE);

  /* Save city ID. */
  tgt_city_id = tgt_city->id;

  capital = (player_primary_capital(tgt_player) == tgt_city);

  if (capital
      && (tgt_player->spaceship.state == SSHIP_STARTED
          || tgt_player->spaceship.state == SSHIP_LAUNCHED)) {
    /* Destroying this city destroys the victim's space ship. */
    spaceship_lost(tgt_player);
  }

  if (capital
      && civil_war_possible(tgt_player, TRUE, TRUE)
      && normal_player_count() < MAX_NUM_PLAYERS
      && civil_war_triggered(tgt_player)) {
    /* Destroying this city can trigger a civil war. */
    try_civil_war = TRUE;
  }

  /* Let the actor know. */
  notify_player(act_player, city_tile(tgt_city),
                E_UNIT_WIN_ATT, ftc_server,
                _("You destroy %s completely."),
                city_tile_link(tgt_city));

  if (tgt_player != act_player) {
    /* This was done to a foreign city. Inform the victim player. */
    notify_player(tgt_player, city_tile(tgt_city),
                  E_CITY_LOST, ftc_server,
                  _("%s has been destroyed by %s."),
                  city_tile_link(tgt_city),
                  player_name(act_player));
  }

  /* May cause an incident */
  action_consequence_success(paction, act_player, act_utype,
                             tgt_player, city_tile(tgt_city),
                             city_link(tgt_city));

  /* Run post city destruction Lua script. */
  script_server_signal_emit("city_destroyed", tgt_city, tgt_player,
                            act_player);

  /* Can't be sure of city existence after running script. */
  if (city_exist(tgt_city_id)) {
    remove_city(tgt_city);
  }

  if (try_civil_war) {
    /* Try to start the civil war. */
    (void) civil_war(tgt_player);
  }

  /* The city is no more. */
  return TRUE;
}

/**********************************************************************//**
  Do a "regular" attack.

  This function assumes the attack is legal. The calling function should
  have already made all necessary checks.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
**************************************************************************/
static bool do_attack(struct unit *punit, struct tile *def_tile,
                      const struct action *paction)
{
  char loser_link[MAX_LEN_LINK], winner_link[MAX_LEN_LINK];
  char attacker_vet[MAX_LEN_LINK], defender_vet[MAX_LEN_LINK];
  char attacker_fp[MAX_LEN_LINK], defender_fp[MAX_LEN_LINK];
  char attacker_tired[MAX_LEN_LINK];
  struct unit *ploser, *pwinner;
  struct city *pcity;
  int moves_used, def_moves_used; 
  int old_unit_vet, old_defender_vet, vet;
  int winner_id;
  struct player *pplayer = unit_owner(punit);
  bool adj;
  enum direction8 facing;
  int att_hp, def_hp, att_fp, def_fp;
  int att_hp_start, def_hp_start;
  int def_power, att_power;
  int att_vet, def_vet;
  struct unit *pdefender;
  const struct unit_type *act_utype = unit_type_get(punit);
  struct civ_map *nmap = &(wld.map);
  bool powerless;

  if (!(pdefender = get_defender(nmap, punit, def_tile))) {
    /* Can't fight air... */
    return FALSE;
  }

  att_hp_start = punit->hp;
  def_hp_start = pdefender->hp;
  def_power = get_total_defense_power(punit, pdefender);
  att_power = get_total_attack_power(punit, pdefender);
  get_modified_firepower(nmap, punit, pdefender, &att_fp, &def_fp);

  log_debug("Start attack: %s %s against %s %s.",
            nation_rule_name(nation_of_player(pplayer)),
            unit_rule_name(punit), 
            nation_rule_name(nation_of_unit(pdefender)),
            unit_rule_name(pdefender));

  /* Sanity checks */
  fc_assert_ret_val_msg(!pplayers_non_attack(pplayer,
                                             unit_owner(pdefender)),
                        FALSE,
                        "Trying to attack a unit with which you have peace "
                        "or cease-fire at (%d, %d).", TILE_XY(def_tile));
  fc_assert_ret_val_msg(!pplayers_allied(pplayer, unit_owner(pdefender)),
                        FALSE,
                        "Trying to attack a unit with which you have "
                        "alliance at (%d, %d).", TILE_XY(def_tile));

  moves_used = unit_move_rate(punit) - punit->moves_left;
  def_moves_used = unit_move_rate(pdefender) - pdefender->moves_left;

  adj = base_get_direction_for_step(nmap,
                                    punit->tile, pdefender->tile, &facing);

  fc_assert(adj);
  if (adj) {
    punit->facing = facing;
    pdefender->facing = opposite_direction(facing);
  }

  old_unit_vet = punit->veteran;
  old_defender_vet = pdefender->veteran;

  /* N.B.: unit_veteran_level_string always returns the same pointer. */
  sz_strlcpy(attacker_vet, unit_veteran_level_string(punit));
  sz_strlcpy(defender_vet, unit_veteran_level_string(pdefender));

  /* N.B.: unit_firepower_if_not_one always returns the same pointer. */
  sz_strlcpy(attacker_fp, unit_firepower_if_not_one(att_fp));
  sz_strlcpy(defender_fp, unit_firepower_if_not_one(def_fp));

  /* Record tired attack string before attack */
  sz_strlcpy(attacker_tired, unit_tired_attack_string(punit));

  powerless = unit_versus_unit(punit, pdefender, &att_hp, &def_hp,
                               &att_vet, &def_vet);

  if ((att_hp <= 0 || utype_is_consumed_by_action(paction, punit->utype))
      && unit_transported(punit)) {
    /* Dying attacker must be first unloaded so it doesn't die inside transport */
    unit_transport_unload_send(punit);
  }

  see_combat(punit, pdefender);

  punit->hp = att_hp;
  pdefender->hp = def_hp;

  combat_veterans(punit, pdefender, powerless, att_vet, def_vet);

  /* Adjust attackers moves_left _after_ unit_versus_unit() so that
   * the movement attack modifier is correct! --dwp
   *
   * For greater Civ2 compatibility (and game balance issues), we recompute 
   * the new total MP based on the HP the unit has left after being damaged, 
   * and subtract the MPs that had been used before the combat (plus the 
   * points used in the attack itself, for the attacker). -GJW, Glip
   */
  punit->moves_left = unit_move_rate(punit) - moves_used;
  pdefender->moves_left = unit_move_rate(pdefender) - def_moves_used;

  if (punit->moves_left < 0) {
    punit->moves_left = 0;
  }
  if (pdefender->moves_left < 0) {
    pdefender->moves_left = 0;
  }
  unit_did_action(punit);
  unit_forget_last_activity(punit);

  /* This may cause a diplomatic incident. */
  action_consequence_success(paction, pplayer, act_utype,
                             unit_owner(pdefender),
                             def_tile, unit_link(pdefender));

  if (pdefender->hp <= 0
      && (pcity = tile_city(def_tile))
      && city_size_get(pcity) > 1
      && get_city_bonus(pcity, EFT_UNIT_NO_LOSE_POP) <= 0
      && kills_citizen_after_attack(punit)) {
    city_reduce_size(pcity, 1, pplayer, "attack");
    city_refresh(pcity);
    send_city_info(NULL, pcity);
  }
  if (punit->hp > 0 && pdefender->hp > 0) {
    /* Neither died */
    send_combat(punit, pdefender, punit->veteran - old_unit_vet,
                pdefender->veteran - old_defender_vet, 0);
    return TRUE;
  }
  pwinner = (punit->hp > 0) ? punit : pdefender;
  winner_id = pwinner->id;
  ploser = (pdefender->hp > 0) ? punit : pdefender;

  vet = (pwinner->veteran == ((punit->hp > 0) ? old_unit_vet :
	old_defender_vet)) ? 0 : 1;

  send_combat(punit, pdefender, punit->veteran - old_unit_vet,
              pdefender->veteran - old_defender_vet, 0);

  /* N.B.: unit_link always returns the same pointer. */
  sz_strlcpy(loser_link, unit_tile_link(ploser));
  sz_strlcpy(winner_link,
             utype_is_consumed_by_action(paction, pwinner->utype)
             ? unit_tile_link(pwinner) : unit_link(pwinner));

  if (punit == ploser) {
    /* The attacker lost */
    log_debug("Attacker lost: %s %s against %s %s.",
              nation_rule_name(nation_of_player(pplayer)),
              unit_rule_name(punit),
              nation_rule_name(nation_of_unit(pdefender)),
              unit_rule_name(pdefender));

    notify_player(unit_owner(pwinner), unit_tile(pwinner),
                  E_UNIT_WIN_DEF, ftc_server,
                  /* TRANS: "Your green Legion [id:100 ...D:4.0 lost 1 HP,
                   * 9 HP remaining] survived the pathetic ...attack from the
                   * Greek green Warriors [id:90 ...A:1.0 HP:10]. */
                  _("Your %s %s [id:%d %sD:%.1f lost %d HP, %d HP remaining]"
                    " survived the pathetic %sattack from the %s %s %s "
                    "[id:%d %sA:%.1f HP:%d]."),
                  defender_vet,
                  winner_link,
                  pdefender->id,
                  defender_fp,
                  (float)def_power/POWER_FACTOR,
                  def_hp_start - pdefender->hp,
                  pdefender->hp,
                  attacker_tired,
                  nation_adjective_for_player(unit_owner(ploser)),
                  attacker_vet,
                  loser_link,
                  punit->id,
                  attacker_fp,
                  (float)att_power/POWER_FACTOR,
                  att_hp_start);

    if (vet) {
      notify_unit_experience(pwinner);
    }
    notify_player(unit_owner(ploser), def_tile,
                  E_UNIT_LOST_ATT, ftc_server,
                  /* TRANS: "Your attacking green Cannon [id:100 ...A:8.0
                   * failed against the Polish green Destroyer [id:200 lost
                   * 27 HP, 3 HP remaining%s]!";
                   * last %s is either "and ..." or empty string */
                 _("Your attacking %s %s [id:%d %sA:%.1f HP:%d] failed "
                   "against the %s %s %s [id:%d lost %d HP, %d HP "
                   "remaining%s]!"),
                 attacker_vet,
                 loser_link,
                 punit->id,
                 attacker_fp,
                 (float)att_power/POWER_FACTOR,
                 att_hp_start,
                 nation_adjective_for_player(unit_owner(pdefender)),
                 defender_vet,
                 winner_link,
                 pdefender->id,
                 def_hp_start - pdefender->hp,
                 pdefender->hp,
                 vet ? unit_achieved_rank_string(pdefender) : "");

    wipe_unit(ploser, ULR_KILLED, unit_owner(pwinner));
  } else {
    /* The defender lost, the attacker punit lives! */

    log_debug("Defender lost: %s %s against %s %s.",
              nation_rule_name(nation_of_player(pplayer)),
              unit_rule_name(punit),
              nation_rule_name(nation_of_unit(pdefender)),
              unit_rule_name(pdefender));

    notify_player(unit_owner(pdefender), unit_tile(pdefender),
                  E_UNIT_LOST_DEF, ftc_server,
                  /* TRANS: "Your green Warriors [id:100 ...D:1.0 HP:10]
                   * lost to an attack by the Greek green Legion
                   * [id:200 ...A:4.0 lost 1 HP, has 9 HP remaining%s]."
                   * last %s is either "and ..." or empty string */
                  _("Your %s %s [id:%d %sD:%.1f HP:%d] lost to an attack by "
                    "the %s %s %s [id:%d %sA:%.1f lost %d HP, has %d HP "
                    "remaining%s]."),
                  defender_vet,
                  loser_link,
                  pdefender->id,
                  defender_fp,
                  (float)def_power/POWER_FACTOR,
                  def_hp_start,
                  nation_adjective_for_player(unit_owner(punit)),
                  attacker_vet,
                  winner_link,
                  punit->id,
                  attacker_fp,
                  (float)att_power/POWER_FACTOR,
                  att_hp_start - pwinner->hp,
                  pwinner->hp,
                  vet ? unit_achieved_rank_string(punit) : "");

    notify_player(unit_owner(punit), unit_tile(punit),
                  E_UNIT_WIN_ATT, ftc_server,
                  /* TRANS: "Your attacking green Legion [id:200 ...A:4.0
                   * lost 1 HP, has 9 HP remaining] succeeded against the
                   * Greek green Warriors [id:100 HP:10]." */
                  _("Your attacking %s %s [id:%d %s%sA:%.1f lost %d HP, "
                    "has %d remaining] succeeded against the %s %s %s "
                    "[id:%d HP:%d]."),
                  attacker_vet,
                  winner_link,
                  punit->id,
                  attacker_fp,
                  attacker_tired,
                  (float)att_power/POWER_FACTOR,
                  att_hp_start - pwinner->hp,
                  pwinner->hp,
                  nation_adjective_for_player(unit_owner(pdefender)),
                  defender_vet,
                  loser_link,
                  pdefender->id,
                  def_hp_start);

    punit->moved = TRUE;	/* We moved */
    kill_unit(pwinner, ploser,
              vet && !utype_is_consumed_by_action(paction, punit->utype));
    if (unit_is_alive(winner_id)) {
      if (utype_is_consumed_by_action(paction, pwinner->utype)) {
        return TRUE;
      }
    } else {
      return TRUE;
    }
  }

  /* If attacker wins, and occupychance > 0, it might move in. Don't move in
   * if there are enemy units in the tile (a fortress, city or air base with
   * multiple defenders and unstacked combat). Note that this could mean
   * capturing (or destroying) a city. */

  if (pwinner == punit && fc_rand(100) < game.server.occupychance
      && !is_non_allied_unit_tile(def_tile, pplayer)) {

    /* Hack: make sure the unit has enough moves_left for the move to succeed,
       and adjust moves_left to afterward (if successful). */

    int old_moves = punit->moves_left;
    int full_moves = unit_move_rate(punit);
    int id = punit->id;

    punit->moves_left = full_moves;
    /* Post attack occupy move. */
    if (NULL != action_auto_perf_unit_do(AAPC_POST_ACTION, punit,
                                         NULL, NULL, paction,
                                         def_tile, tile_city(def_tile),
                                         NULL, NULL)) {
      if (unit_is_alive(id)) {
        int mcost = MAX(0, full_moves - punit->moves_left - SINGLE_MOVE);

        /* Move cost is bigger of attack (SINGLE_MOVE) and occupying move costs.
         * Attack SINGLE_COST is already calculated in to old_moves. */
        punit->moves_left = old_moves - mcost;
        if (punit->moves_left < 0) {
          punit->moves_left = 0;
        }
      }
    } else if (unit_is_alive(id)) {
      punit->moves_left = old_moves;
    }
  }

  /* The attacker may have died for many reasons */
  if (game_unit_by_number(winner_id) != NULL) {
    send_unit_info(NULL, pwinner);
  }

  return TRUE;
}

/**********************************************************************//**
  Have the unit perform a surgical strike against the current production
  in the target city.

  This function assumes the attack is legal. The calling function should
  have already made all necessary checks.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
**************************************************************************/
static bool do_unit_strike_city_production(const struct player *act_player,
                                           struct unit *act_unit,
                                           struct city *tgt_city,
                                           const struct action *paction)
{
  struct player *tgt_player;
  char prod[256];
  const char *clink;

  /* Sanity checks */
  fc_assert_ret_val(act_player, FALSE);
  fc_assert_ret_val(act_unit, FALSE);
  fc_assert_ret_val(tgt_city, FALSE);
  fc_assert_ret_val(paction, FALSE);

  tgt_player = city_owner(tgt_city);
  fc_assert_ret_val(tgt_player, FALSE);

  /* The surgical strike may miss. */
  {
    /* Roll the dice. */
    if (action_failed_dice_roll(act_player, act_unit,
                                tgt_city, tgt_player,
                                paction)) {
      /* Notify the player. */
      notify_player(act_player, city_tile(tgt_city),
                    E_UNIT_ACTION_ACTOR_FAILURE, ftc_server,
                    /* TRANS: unit, action, city */
                    _("Your %s failed to do %s in %s."),
                    unit_link(act_unit),
                    action_name_translation(paction),
                    city_link(tgt_city));

      /* Make the failed attempt cost a single move. */
      act_unit->moves_left = MAX(0, act_unit->moves_left - SINGLE_MOVE);

      return FALSE;
    }
  }

  /* Get name of the production */
  universal_name_translation(&tgt_city->production, prod, sizeof(prod));

  /* Destroy the production */
  tgt_city->shield_stock = 0;
  nullify_prechange_production(tgt_city);

  /* Let the players know. */
  clink = city_link(tgt_city); /* Be careful not to call city_link()
                                * again as long as we need clink */
  notify_player(act_player, city_tile(tgt_city),
                E_UNIT_ACTION_ACTOR_SUCCESS, ftc_server,
                _("Your %s succeeded in destroying"
                  " the production of %s in %s."),
                unit_link(act_unit),
                prod, clink);
  notify_player(tgt_player, city_tile(tgt_city),
                E_UNIT_ACTION_TARGET_HOSTILE, ftc_server,
                _("The production of %s was destroyed in %s,"
                  " %s are suspected."),
                prod, clink,
                nation_plural_for_player(act_player));

  return TRUE;
}

/**********************************************************************//**
  Have the unit perform a surgical strike against a building in the target
  city.

  This function assumes the attack is legal. The calling function should
  have already made all necessary checks.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
**************************************************************************/
static bool do_unit_strike_city_building(const struct player *act_player,
                                         struct unit *act_unit,
                                         struct city *tgt_city,
                                         Impr_type_id tgt_bld_id,
                                         const struct action *paction)
{
  struct player *tgt_player;
  struct impr_type *tgt_bld = improvement_by_number(tgt_bld_id);
  const char *clink;

  /* Sanity checks */
  fc_assert_ret_val(act_player, FALSE);
  fc_assert_ret_val(act_unit, FALSE);
  fc_assert_ret_val(tgt_city, FALSE);
  fc_assert_ret_val(paction, FALSE);

  tgt_player = city_owner(tgt_city);
  fc_assert_ret_val(tgt_player, FALSE);

  /* The surgical strike may miss. */
  {
    /* Roll the dice. */
    if (action_failed_dice_roll(act_player, act_unit,
                                tgt_city, tgt_player,
                                paction)) {
      /* Notify the player. */
      notify_player(act_player, city_tile(tgt_city),
                    E_UNIT_ACTION_ACTOR_FAILURE, ftc_server,
                    /* TRANS: unit, action, city */
                    _("Your %s failed to do %s in %s."),
                    unit_link(act_unit),
                    action_name_translation(paction),
                    city_link(tgt_city));

      /* Make the failed attempt cost a single move. */
      act_unit->moves_left = MAX(0, act_unit->moves_left - SINGLE_MOVE);

      return FALSE;
    }
  }

  if (!city_has_building(tgt_city, tgt_bld)) {
    /* Nothing to destroy here. */

    /* Notify the player. */
    notify_player(act_player, city_tile(tgt_city),
                  E_UNIT_ACTION_ACTOR_FAILURE, ftc_server,
                  _("Your %s didn't find a %s to %s in %s."),
                  unit_link(act_unit),
                  improvement_name_translation(tgt_bld),
                  action_name_translation(paction),
                  city_link(tgt_city));

    /* Punish the player for blindly attacking a building. */
    act_unit->moves_left = MAX(0, act_unit->moves_left - SINGLE_MOVE);

    return FALSE;
  }

  /* Destroy the building. */
  building_lost(tgt_city, tgt_bld, "attacked", act_unit);

  /* Update the player's view of the city. */
  send_city_info(NULL, tgt_city);

  /* Let the players know. */
  clink = city_link(tgt_city); /* Be careful not to call city_link()
                                * again as long as we need clink */
  notify_player(act_player, city_tile(tgt_city),
                E_UNIT_ACTION_ACTOR_SUCCESS, ftc_server,
                _("Your %s destroyed the %s in %s."),
                unit_link(act_unit),
                improvement_name_translation(tgt_bld),
                clink);
  notify_player(tgt_player, city_tile(tgt_city),
                E_UNIT_ACTION_TARGET_HOSTILE, ftc_server,
                _("The %s destroyed the %s in %s."),
                nation_plural_for_player(act_player),
                improvement_name_translation(tgt_bld),
                clink);

  return TRUE;
}

/**********************************************************************//**
  Have the unit conquer a city.

  This function assumes the attack is legal. The calling function should
  have already made all necessary checks.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
**************************************************************************/
static bool do_unit_conquer_city(struct player *act_player,
                                 struct unit *act_unit,
                                 struct city *tgt_city,
                                 struct action *paction)
{
  bool success;
  struct tile *tgt_tile = city_tile(tgt_city);
  int move_cost = map_move_cost_unit(&(wld.map), act_unit, tgt_tile);
  int tgt_city_id = tgt_city->id;
  struct player *tgt_player = city_owner(tgt_city);
  const char *victim_link = city_link(tgt_city);
  const struct unit_type *act_utype = unit_type_get(act_unit);

  /* Sanity check */
  fc_assert_ret_val(tgt_tile, FALSE);

  unit_move(act_unit, tgt_tile, move_cost, NULL, FALSE, TRUE, TRUE,
            BV_ISSET(paction->sub_results, ACT_SUB_RES_HUT_ENTER),
            BV_ISSET(paction->sub_results, ACT_SUB_RES_HUT_FRIGHTEN));

  /* The city may have been destroyed during the conquest. */
  success = (!city_exist(tgt_city_id)
             || city_owner(tgt_city) == act_player);

  if (success) {
    /* May cause an incident */
    action_consequence_success(paction, act_player, act_utype,
                               tgt_player, tgt_tile,
                               victim_link);
  }

  return success;
}

/**********************************************************************//**
  See also aiunit could_unit_move_to_tile()
**************************************************************************/
static bool can_unit_move_to_tile_with_notify(struct unit *punit,
					      struct tile *dest_tile,
					      bool igzoc,
                                              struct unit *embark_to,
                                              bool enter_enemy_city)
{
  struct tile *src_tile = unit_tile(punit);
  enum unit_move_result reason =
    unit_move_to_tile_test(&(wld.map), punit, punit->activity,
                           src_tile, dest_tile, igzoc, TRUE, embark_to,
                           enter_enemy_city);

  switch (reason) {
  case MR_OK:
    return TRUE;

  case MR_NO_WAR:
    notify_player(unit_owner(punit), src_tile, E_BAD_COMMAND, ftc_server,
                  _("Cannot attack unless you declare war first."));
    break;

  case MR_ZOC:
    notify_player(unit_owner(punit), src_tile, E_BAD_COMMAND, ftc_server,
                  _("%s can only move into your own zone of control."),
                  unit_link(punit));
    break;

  case MR_TRIREME:
    notify_player(unit_owner(punit), src_tile, E_BAD_COMMAND, ftc_server,
                  _("%s cannot move that far from the coast line."),
                  unit_link(punit));
    break;

  case MR_PEACE:
    if (tile_owner(dest_tile)) {
      notify_player(unit_owner(punit), src_tile, E_BAD_COMMAND, ftc_server,
                    _("Cannot invade unless you break peace with "
                      "%s first."),
                    player_name(tile_owner(dest_tile)));
    }
    break;

  case MR_CANNOT_DISEMBARK:
    notify_player(unit_owner(punit), src_tile, E_BAD_COMMAND, ftc_server,
                  _("%s cannot disembark outside of a city or a native base "
                    "for %s."),
                  unit_link(punit),
                  utype_name_translation(
                      unit_type_get(unit_transport_get(punit))));
    break;

  case MR_NON_NATIVE_MOVE:
    notify_player(unit_owner(punit), src_tile, E_BAD_COMMAND, ftc_server,
                  _("Terrain is unsuitable for %s units."),
                  uclass_name_translation(unit_class_get(punit)));
    break;

  default:
    /* FIXME: need more explanations someday! */
    break;
  };

  return FALSE;
}

/**********************************************************************//**
  Moves the unit from one tile to another.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
**************************************************************************/
static bool unit_do_regular_move(struct player *actor_player,
                                 struct unit *actor_unit,
                                 struct tile *target_tile,
                                 const struct action *paction)
{
  const struct unit_type *act_utype = unit_type_get(actor_unit);
  int move_cost = map_move_cost_unit(&(wld.map), actor_unit, target_tile);

  unit_move(actor_unit, target_tile, move_cost,
            NULL, BV_ISSET(paction->sub_results, ACT_SUB_RES_MAY_EMBARK),
            /* Don't override "Conquer City" */
            FALSE,
            /* Don't override "Conquer Extras" */
            FALSE,
            BV_ISSET(paction->sub_results, ACT_SUB_RES_HUT_ENTER),
            BV_ISSET(paction->sub_results, ACT_SUB_RES_HUT_FRIGHTEN));

  /* May cause an incident */
  action_consequence_success(paction, actor_player, act_utype,
                             tile_owner(target_tile),
                             target_tile, tile_link(target_tile));

  return TRUE;
}

/**********************************************************************//**
  Will try to move to/attack the tile dest_x,dest_y.  Returns TRUE if this
  was done, FALSE if it wasn't for some reason. Even if this returns TRUE,
  the unit may have died upon arrival to new tile.

  'move_do_not_act' is a special case which should normally be
  FALSE.  If TRUE any enabler controlled actions punit can perform to
  pdesttile it self or something located at it will be ignored. If FALSE
  the system will check if punit can perform any enabler controlled action
  to pdesttile. If it can the player will be asked to choose what to do. If
  it can't and punit is unable to move (or perform another non enabler
  controlled action) to pdesttile the game will try to explain why.
**************************************************************************/
bool unit_move_handling(struct unit *punit, struct tile *pdesttile,
                        bool move_do_not_act)
{
  struct player *pplayer = unit_owner(punit);
  struct unit *ptrans;
  const struct civ_map *nmap = &(wld.map);

  /*** Phase 1: Attempted action interpretation checks ***/

  /* Check if the move should be interpreted as an attempt to perform an
   * enabler controlled action to the target tile. When the move may be an
   * action attempt the server stops moving the unit, marks it as wanting a
   * decision based on its own movement to the tile it attempted to move to
   * and notifies the client.
   *
   * In response to the unit being marked as wanting a decision the client
   * can query the server for what actions the unit, given the player's
   * knowledge, may be able to perform against a target at the tile it tried
   * to move to. The server will respond to the query with the actions that
   * may be enabled and, when all actions are known to be illegal given the
   * player's knowledge, an explanation why no action could be done. The
   * client will probably use the list of potentially legal actions, if any,
   * to pop up an action selection dialog. See handle_unit_action_query()
   *
   * If move_do_not_act is TRUE the move is never interpreted as an attempt
   * to perform an enabler controlled action.
   * Examples of where this is useful is for AI moves, goto, when the player
   * attempts to move to a tile occupied by potential targets like allied
   * cities or units and during rule forced moves.
   *
   * A move is not interpreted as an attempted action because the unit is
   * able to do a self targeted action.
   *
   * A move is not interpreted as an attempted action because an action
   * with rare_pop_up set to TRUE is legal unless the unit is unable to
   * perform a regular move to the tile.
   *
   * An attempted move to a tile a unit can't move to is always interpreted
   * as trying to perform an action (unless move_do_not_act is TRUE) */
  if (!move_do_not_act) {
    const bool can_not_move = !unit_can_move_to_tile(nmap,
                                                     punit, pdesttile,
                                                     FALSE, FALSE, FALSE);
    bool one_action_may_be_legal
        =  action_tgt_unit(punit, pdesttile, can_not_move)
        || action_tgt_city(punit, pdesttile, can_not_move)
        /* A legal action with an extra sub target is a legal action */
        || action_tgt_tile_extra(punit, pdesttile, can_not_move)
        /* Tile target actions with extra sub targets are handled above */
        || action_tgt_tile(punit, pdesttile, NULL, can_not_move);

    if (one_action_may_be_legal || can_not_move) {
      /* There is a target punit, from the player's point of view, may be
       * able to act against OR punit can't do any non action move. The
       * client should therefore ask what action(s) the unit can perform
       * to any targets at pdesttile.
       *
       * In the first case the unit needs a decision about what action, if
       * any at all, to take. Asking what actions the unit can perform
       * will return a list of actions that may, from the players point of
       * view, be possible. The client can then show this list to the
       * player or, if configured to do so, make the choice it self.
       *
       * In the last case the player may need an explanation about why no
       * action could be taken. Asking what actions the unit can perform
       * will provide this explanation. */
      punit->action_decision_want = ACT_DEC_ACTIVE;
      punit->action_decision_tile = pdesttile;
      send_unit_info(player_reply_dest(pplayer), punit);

      /* The move wasn't done because the unit wanted the player to
       * decide what to do or because the unit couldn't move to the
       * target tile. */
      return FALSE;
    }
  }

  /*** Phase 2: OK now move the unit ***/
  /* This is a regular move, subject to the rules. */
  if (is_action_enabled_unit_on_tile(nmap, ACTION_UNIT_MOVE,
                                     punit, pdesttile, NULL)) {
    return unit_perform_action(pplayer, punit->id, tile_index(pdesttile),
                               NO_TARGET, "", ACTION_UNIT_MOVE,
                               ACT_REQ_PLAYER);
  } else if (is_action_enabled_unit_on_tile(nmap, ACTION_UNIT_MOVE2,
                                            punit, pdesttile, NULL)) {
    return unit_perform_action(pplayer, punit->id, tile_index(pdesttile),
                               NO_TARGET, "", ACTION_UNIT_MOVE2,
                               ACT_REQ_PLAYER);
  } else if (is_action_enabled_unit_on_tile(nmap, ACTION_UNIT_MOVE3,
                                            punit, pdesttile, NULL)) {
    return unit_perform_action(pplayer, punit->id, tile_index(pdesttile),
                               NO_TARGET, "", ACTION_UNIT_MOVE3,
                               ACT_REQ_PLAYER);
  } else if (!can_unit_survive_at_tile(nmap, punit, pdesttile)
             && ((ptrans = transporter_for_unit_at(punit, pdesttile)))
             && is_action_enabled_unit_on_unit(nmap, ACTION_TRANSPORT_EMBARK,
                                               punit, ptrans)) {
    /* "Transport Embark". */
    return unit_perform_action(pplayer, punit->id, ptrans->id,
                               NO_TARGET, "", ACTION_TRANSPORT_EMBARK,
                               ACT_REQ_PLAYER);
  } else if (!can_unit_survive_at_tile(nmap, punit, pdesttile)
             && ((ptrans = transporter_for_unit_at(punit, pdesttile)))
             && is_action_enabled_unit_on_unit(nmap, ACTION_TRANSPORT_EMBARK2,
                                               punit, ptrans)) {
    /* "Transport Embark 2". */
    return unit_perform_action(pplayer, punit->id, ptrans->id,
                               NO_TARGET, "", ACTION_TRANSPORT_EMBARK2,
                               ACT_REQ_PLAYER);
  } else if (!can_unit_survive_at_tile(nmap, punit, pdesttile)
             && ((ptrans = transporter_for_unit_at(punit, pdesttile)))
             && is_action_enabled_unit_on_unit(nmap, ACTION_TRANSPORT_EMBARK3,
                                               punit, ptrans)) {
    /* "Transport Embark 3". */
    return unit_perform_action(pplayer, punit->id, ptrans->id,
                               NO_TARGET, "", ACTION_TRANSPORT_EMBARK3,
                               ACT_REQ_PLAYER);
  } else if (is_action_enabled_unit_on_tile(nmap, ACTION_TRANSPORT_DISEMBARK1,
                                            punit, pdesttile, NULL)) {
    /* "Transport Disembark". */
    return unit_perform_action(pplayer, punit->id, tile_index(pdesttile),
                               NO_TARGET, "", ACTION_TRANSPORT_DISEMBARK1,
                               ACT_REQ_PLAYER);
  } else if (is_action_enabled_unit_on_tile(nmap, ACTION_TRANSPORT_DISEMBARK2,
                                            punit, pdesttile, NULL)) {
    /* "Transport Disembark 2". */
    return unit_perform_action(pplayer, punit->id, tile_index(pdesttile),
                               NO_TARGET, "", ACTION_TRANSPORT_DISEMBARK2,
                               ACT_REQ_PLAYER);
  } else if (is_action_enabled_unit_on_tile(nmap, ACTION_TRANSPORT_DISEMBARK3,
                                            punit, pdesttile, NULL)) {
    /* "Transport Disembark 3". */
    return unit_perform_action(pplayer, punit->id, tile_index(pdesttile),
                               NO_TARGET, "", ACTION_TRANSPORT_DISEMBARK3,
                               ACT_REQ_PLAYER);
  } else if (is_action_enabled_unit_on_tile(nmap, ACTION_TRANSPORT_DISEMBARK4,
                                            punit, pdesttile, NULL)) {
    /* "Transport Disembark 4". */
    return unit_perform_action(pplayer, punit->id, tile_index(pdesttile),
                               NO_TARGET, "", ACTION_TRANSPORT_DISEMBARK4,
                               ACT_REQ_PLAYER);
  } else if (is_action_enabled_unit_on_tile(nmap, ACTION_HUT_ENTER,
                                            punit, pdesttile, NULL)) {
    /* "Enter Hut". */
    return unit_perform_action(pplayer, punit->id, tile_index(pdesttile),
                               NO_TARGET, "", ACTION_HUT_ENTER,
                               ACT_REQ_PLAYER);
  } else if (is_action_enabled_unit_on_tile(nmap, ACTION_HUT_ENTER2,
                                            punit, pdesttile, NULL)) {
    /* "Enter Hut 2". */
    return unit_perform_action(pplayer, punit->id, tile_index(pdesttile),
                               NO_TARGET, "", ACTION_HUT_ENTER2,
                               ACT_REQ_PLAYER);
  } else if (is_action_enabled_unit_on_tile(nmap, ACTION_HUT_FRIGHTEN,
                                            punit, pdesttile, NULL)) {
    /* "Frighten Hut". */
    return unit_perform_action(pplayer, punit->id, tile_index(pdesttile),
                               NO_TARGET, "", ACTION_HUT_FRIGHTEN,
                               ACT_REQ_PLAYER);
  } else if (is_action_enabled_unit_on_tile(nmap, ACTION_HUT_FRIGHTEN2,
                                            punit, pdesttile, NULL)) {
    /* "Frighten Hut 2". */
    return unit_perform_action(pplayer, punit->id, tile_index(pdesttile),
                               NO_TARGET, "", ACTION_HUT_FRIGHTEN2,
                               ACT_REQ_PLAYER);
  } else {
    /* TODO: Extend the action not enabled explanation system to cover all
     * existing reasons and switch to using it. See hrm Feature #920229 */
    can_unit_move_to_tile_with_notify(punit, pdesttile, FALSE,
                                      NULL, FALSE);
    return FALSE;
  }
}

/**********************************************************************//**
  Help build the current production in a city.

  The amount of shields used to build the unit added to the city's shield
  stock for the current production is determined by the
  Unit_Shield_Value_Pct effect.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
**************************************************************************/
static bool unit_do_help_build(struct player *pplayer,
                               struct unit *punit,
                               struct city *pcity_dest,
                               const struct action *paction)
{
  const char *work;
  const char *prod;
  int shields;
  const struct unit_type *act_utype;
  struct player *cowner;

  /* Sanity check: The actor still exists. */
  fc_assert_ret_val(pplayer, FALSE);
  fc_assert_ret_val(punit, FALSE);

  /* Sanity check: The target city still exists. */
  fc_assert_ret_val(pcity_dest, FALSE);

  act_utype = unit_type_get(punit);
  shields = unit_shield_value(punit, unit_type_get(punit), paction);

  if (action_has_result(paction, ACTRES_HELP_WONDER)) {
    /* Add the caravan shields */
    pcity_dest->shield_stock += shields;

    /* Will be punished for changing production to something that can't
     * receive "Help Wonder" help. */
    fc_assert(city_production_gets_caravan_shields(
                  &pcity_dest->production));
    pcity_dest->caravan_shields += shields;
  } else {
    fc_assert(action_has_result(paction, ACTRES_DISBAND_UNIT_RECOVER));
    /* Add the shields from recycling the unit to the city's current
     * production. */
    pcity_dest->shield_stock += shields;

    /* If we change production later at this turn. No penalty is added. */
    pcity_dest->disbanded_shields += shields;
  }

  cowner = city_owner(pcity_dest);

  conn_list_do_buffer(cowner->connections);

  if (action_has_result(paction, ACTRES_HELP_WONDER)) {
    /* Let the player that just donated shields with "Help Wonder" know
     * the result of their donation. */
    prod = city_production_name_translation(pcity_dest);
  } else {
    fc_assert(action_has_result(paction, ACTRES_DISBAND_UNIT_RECOVER));
    /* TRANS: Your Caravan does "Disband Unit Recover" to help build the
     * current production in Bergen (4 surplus).
     * "Disband Unit Recover" says "current production" rather than its name. */
    prod = _("current production");
  }

  if (build_points_left(pcity_dest) >= 0) {
    /* TRANS: Your Caravan does "Help Wonder" to help build the
     * Pyramids in Bergen (4 remaining).
     * You can reorder '4' and 'remaining' in the actual format string. */
    work = _("remaining");
  } else {
    /* TRANS: Your Caravan does "Help Wonder" to help build the
     * Pyramids in Bergen (4 surplus).
     * You can reorder '4' and 'surplus' in the actual format string. */
    work = _("surplus");
  }

  notify_player(pplayer, city_tile(pcity_dest), E_CARAVAN_ACTION,
                ftc_server,
                /* TRANS: Your Caravan does "Help Wonder" to help build the
                 * Pyramids in Bergen (4 surplus). */
                _("Your %s does %s to help build the %s in %s (%d %s)."),
                unit_link(punit),
                action_name_translation(paction),
                prod,
                city_link(pcity_dest), 
                abs(build_points_left(pcity_dest)),
                work);

  /* May cause an incident */
  action_consequence_success(paction, pplayer, act_utype, cowner,
                             city_tile(pcity_dest), city_link(pcity_dest));

  if (cowner != unit_owner(punit)) {
    /* Tell the city owner about the gift they just received. */

    notify_player(cowner, city_tile(pcity_dest),
                  E_CARAVAN_ACTION, ftc_server,
                  /* TRANS: Help building the Pyramids in Bergen received
                   * from Persian Caravan (4 surplus). */
                  _("Help building the %s in %s received from %s %s "
                    "(%d %s)."),
                  city_production_name_translation(pcity_dest),
                  city_link(pcity_dest),
                  nation_adjective_for_player(pplayer),
                  unit_link(punit),
                  abs(build_points_left(pcity_dest)),
                  work);
  }

  send_player_info_c(cowner, pplayer->connections);
  send_city_info(cowner, pcity_dest);
  conn_list_do_unbuffer(cowner->connections);

  return TRUE;
}

/**********************************************************************//**
  Handle request to establish trade route. If pcity_dest is NULL, assumes
  that unit is inside target city.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
**************************************************************************/
static bool do_unit_establish_trade(struct player *pplayer,
                                    struct unit *punit,
                                    struct city *pcity_dest,
                                    const struct action *paction)
{
  char homecity_link[MAX_LEN_LINK], destcity_link[MAX_LEN_LINK];
  char punit_link[MAX_LEN_LINK];
  int revenue;
  bool can_establish;
  int home_overbooked = 0;
  int dest_overbooked = 0;
  int home_max;
  int dest_max;
  struct city *pcity_homecity;
  struct trade_route_list *routes_out_of_dest;
  struct trade_route_list *routes_out_of_home;
  enum trade_route_bonus_type bonus_type;
  struct goods_type *goods;
  const char *goods_str;
  const struct unit_type *act_utype;

  /* Sanity check: The actor still exists. */
  fc_assert_ret_val(pplayer, FALSE);
  fc_assert_ret_val(punit, FALSE);

  /* Sanity check: The target city still exists. */
  fc_assert_ret_val(pcity_dest, FALSE);

  act_utype = unit_type_get(punit);
  pcity_homecity = player_city_by_number(pplayer, punit->homecity);

  if (!pcity_homecity) {
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  _("Sorry, your %s cannot establish"
                    " a trade route because it has no home city."),
                  unit_link(punit));
    return FALSE;
  }

  if (game.info.goods_selection == GSM_ARRIVAL) {
    goods =  goods_from_city_to_unit(pcity_homecity, punit);
  } else {
    goods = punit->carrying;
  }
  if (goods == NULL) {
    notify_player(pplayer, unit_tile(punit), E_BAD_COMMAND, ftc_server,
                  _("Sorry, your %s cannot establish"
                    " a trade route because it's not carrying any goods."),
                  unit_link(punit));
    return FALSE;
  }

  sz_strlcpy(homecity_link, city_link(pcity_homecity));
  sz_strlcpy(destcity_link, city_link(pcity_dest));

  if (!can_cities_trade(pcity_homecity, pcity_dest)) {
    notify_player(pplayer, city_tile(pcity_dest), E_BAD_COMMAND, ftc_server,
                  _("Sorry, your %s cannot establish"
                    " a trade route between %s and %s."),
                  unit_link(punit),
                  homecity_link,
                  destcity_link);
    return FALSE;
  }

  sz_strlcpy(punit_link, unit_tile_link(punit));
  routes_out_of_home = trade_route_list_new();
  routes_out_of_dest = trade_route_list_new();

  /* This part of code works like can_establish_trade_route, except
   * that we actually do the action of making the trade route. */

  /* If we can't make a new trade route we can still get the trade bonus. */
  can_establish = action_has_result(paction, ACTRES_TRADE_ROUTE)
                  && !have_cities_trade_route(pcity_homecity, pcity_dest);

  if (can_establish) {
    home_max = max_trade_routes(pcity_homecity);
    dest_max = max_trade_routes(pcity_dest);
    home_overbooked = city_num_trade_routes(pcity_homecity) - home_max;
    dest_overbooked = city_num_trade_routes(pcity_dest) - dest_max;
  }

  if (can_establish && (home_overbooked >= 0 || dest_overbooked >= 0)) {
    int trade = trade_base_between_cities(pcity_homecity, pcity_dest);

    /* See if there's a trade route we can cancel at the home city. */
    if (home_overbooked >= 0) {
      if (home_max <= 0
          || (city_trade_removable(pcity_homecity, routes_out_of_home)
              >= trade)) {
        notify_player(pplayer, city_tile(pcity_dest),
                      E_BAD_COMMAND, ftc_server,
                     _("Sorry, your %s cannot establish"
                       " a trade route here!"),
                       punit_link);
        if (home_max > 0) {
          notify_player(pplayer, city_tile(pcity_dest),
                        E_BAD_COMMAND, ftc_server,
                        PL_("      The city of %s already has %d "
                            "better trade route!",
                            "      The city of %s already has %d "
                            "better trade routes!", home_max),
                        homecity_link,
                        home_max);
        }
	can_establish = FALSE;
      }
    }

    /* See if there's a trade route we can cancel at the dest city. */
    if (can_establish && dest_overbooked >= 0) {
      if (dest_max <= 0
          || (city_trade_removable(pcity_dest, routes_out_of_dest)
              >= trade)) {
        notify_player(pplayer, city_tile(pcity_dest),
                      E_BAD_COMMAND, ftc_server,
                      _("Sorry, your %s cannot establish"
                        " a trade route here!"),
                      punit_link);
        if (dest_max > 0) {
          notify_player(pplayer, city_tile(pcity_dest),
                        E_BAD_COMMAND, ftc_server,
                        PL_("      The city of %s already has %d "
                            "better trade route!",
                            "      The city of %s already has %d "
                            "better trade routes!", dest_max),
                        destcity_link,
                        dest_max);
        }
	can_establish = FALSE;
      }
    }
  }

  /* We now know for sure whether we can establish a trade route. */

  /* Calculate and announce initial revenue. */
  revenue = get_caravan_enter_city_trade_bonus(pcity_homecity, pcity_dest, goods,
                                               can_establish);

  bonus_type = trade_route_settings_by_type(cities_trade_route_type(pcity_homecity, pcity_dest))->bonus_type;

  conn_list_do_buffer(pplayer->connections);

  goods_str = goods_name_translation(goods);

  /* We want to keep the bonus type string as the part of the format of the PL_() strings
   * for supporting proper pluralization for it. */
  switch (bonus_type) {
  case TBONUS_NONE:
    notify_player(pplayer, city_tile(pcity_dest),
                  E_CARAVAN_ACTION, ftc_server,
                  /* TRANS: ... Caravan ... Paris ... Stockholm ... Goods */
                  _("Your %s from %s has arrived in %s carrying %s."),
                  punit_link,
                  homecity_link,
                  destcity_link,
                  goods_str);
    break;
  case TBONUS_GOLD:
    notify_player(pplayer, city_tile(pcity_dest),
                  E_CARAVAN_ACTION, ftc_server,
                  /* TRANS: ... Caravan ... Paris ... Stockholm, ... Goods... */
                  PL_("Your %s from %s has arrived in %s carrying %s,"
                      " and revenues amount to %d in gold.",
                      "Your %s from %s has arrived in %s carrying %s,"
                      " and revenues amount to %d in gold.",
                      revenue),
                  punit_link,
                  homecity_link,
                  destcity_link,
                  goods_str,
                  revenue);
    break;
  case TBONUS_SCIENCE:
    notify_player(pplayer, city_tile(pcity_dest),
                  E_CARAVAN_ACTION, ftc_server,
                  /* TRANS: ... Caravan ... Paris ... Stockholm, ... Goods... */
                  PL_("Your %s from %s has arrived in %s carrying %s,"
                      " and revenues amount to %d in research.",
                      "Your %s from %s has arrived in %s carrying %s,"
                      " and revenues amount to %d in research.",
                      revenue),
                  punit_link,
                  homecity_link,
                  destcity_link,
                  goods_str,
                  revenue);
    break;
  case TBONUS_BOTH:
    notify_player(pplayer, city_tile(pcity_dest),
                  E_CARAVAN_ACTION, ftc_server,
                  /* TRANS: ... Caravan ... Paris ... Stockholm, ... Goods... */
                  PL_("Your %s from %s has arrived in %s carrying %s,"
                      " and revenues amount to %d in gold and research.",
                      "Your %s from %s has arrived in %s carrying %s,"
                      " and revenues amount to %d in gold and research.",
                      revenue),
                  punit_link,
                  homecity_link,
                  destcity_link,
                  goods_str,
                  revenue);
    break;
  }

  if (bonus_type == TBONUS_GOLD || bonus_type == TBONUS_BOTH) {
    pplayer->economic.gold += revenue;

    send_player_info_c(pplayer, pplayer->connections);
  }

  if (bonus_type == TBONUS_SCIENCE || bonus_type == TBONUS_BOTH) {
    /* add bulbs and check for finished research */
    update_bulbs(pplayer, revenue, TRUE);

    /* Inform everyone about tech changes */
    send_research_info(research_get(pplayer), NULL);
  }

  if (can_establish) {
    struct trade_route *proute_from, *proute_to;
    struct city_list *cities_out_of_home;
    struct city_list *cities_out_of_dest;
    struct player *partner_player;

    /* Announce creation of trade route (it's not actually created until
     * later in this function, as we have to cancel existing routes, but
     * it makes more sense to announce in this order) */

    partner_player = city_owner(pcity_dest);

    /* Always tell the unit owner */
    notify_player(pplayer, NULL,
                  E_CARAVAN_ACTION, ftc_server,
                  _("New trade route established from %s to %s."),
                  homecity_link,
                  destcity_link);
    if (pplayer != partner_player) {
      notify_player(partner_player, city_tile(pcity_dest),
                    E_CARAVAN_ACTION, ftc_server,
                    _("The %s established a trade route between their "
                      "city %s and %s."),
                    nation_plural_for_player(pplayer),
                    homecity_link,
                    destcity_link);
    }

    cities_out_of_home = city_list_new();
    cities_out_of_dest = city_list_new();

    /* Now cancel any less profitable trade route from the home city. */
    trade_route_list_iterate(routes_out_of_home, premove) {
      struct trade_route *pback;

      city_list_append(cities_out_of_home, game_city_by_number(premove->partner));

      pback = remove_trade_route(pcity_homecity, premove, TRUE, FALSE);
      free(premove);
      free(pback);
    } trade_route_list_iterate_end;

    /* And the same for the dest city. */
    trade_route_list_iterate(routes_out_of_dest, premove) {
      struct trade_route *pback;

      city_list_append(cities_out_of_dest, game_city_by_number(premove->partner));

      pback = remove_trade_route(pcity_dest, premove, TRUE, FALSE);
      free(premove);
      free(pback);
    } trade_route_list_iterate_end;

    /* Actually create the new trade route */
    proute_from = fc_malloc(sizeof(struct trade_route));
    proute_from->partner = pcity_dest->id;
    proute_from->goods = goods;

    proute_to = fc_malloc(sizeof(struct trade_route));
    proute_to->partner = pcity_homecity->id;
    proute_to->goods = goods;

    if (goods_has_flag(goods, GF_BIDIRECTIONAL)) {
      proute_from->dir = RDIR_BIDIRECTIONAL;
      proute_to->dir = RDIR_BIDIRECTIONAL;
    } else {
      proute_from->dir = RDIR_FROM;
      proute_to->dir = RDIR_TO;
    }
    trade_route_list_append(pcity_homecity->routes, proute_from);
    trade_route_list_append(pcity_dest->routes, proute_to);

    /* Refresh the cities. */
    city_refresh(pcity_homecity);
    city_refresh(pcity_dest);
    city_list_iterate(cities_out_of_home, pcity) {
      city_refresh(pcity);
    } city_list_iterate_end;
    city_list_iterate(cities_out_of_dest, pcity) {
      city_refresh(pcity);
    } city_list_iterate_end;

    /* Notify the owners of the cities. */
    send_city_info(pplayer, pcity_homecity);
    send_city_info(partner_player, pcity_dest);
    city_list_iterate(cities_out_of_home, pcity) {
      send_city_info(city_owner(pcity), pcity);
    } city_list_iterate_end;
    city_list_iterate(cities_out_of_dest, pcity) {
      send_city_info(city_owner(pcity), pcity);
    } city_list_iterate_end;

    /* Notify each player about the other cities so that they know about
     * its size for the trade calculation. */
    if (pplayer != partner_player) {
      map_show_tile(partner_player, city_tile(pcity_homecity));
      send_city_info(partner_player, pcity_homecity);
      map_show_tile(pplayer, city_tile(pcity_dest));
      send_city_info(pplayer, pcity_dest);
    }

    city_list_iterate(cities_out_of_home, pcity) {
      if (partner_player != city_owner(pcity)) {
        send_city_info(partner_player, pcity);
        send_city_info(city_owner(pcity), pcity_dest);
      }
      if (pplayer != city_owner(pcity)) {
        send_city_info(pplayer, pcity);
        send_city_info(city_owner(pcity), pcity_homecity);
      }
    } city_list_iterate_end;

    city_list_iterate(cities_out_of_dest, pcity) {
      if (partner_player != city_owner(pcity)) {
        send_city_info(partner_player, pcity);
        send_city_info(city_owner(pcity), pcity_dest);
      }
      if (pplayer != city_owner(pcity)) {
        send_city_info(pplayer, pcity);
        send_city_info(city_owner(pcity), pcity_homecity);
      }
    } city_list_iterate_end;

    city_list_destroy(cities_out_of_home);
    city_list_destroy(cities_out_of_dest);
  }

  /* May cause an incident */
  action_consequence_success(paction,
                             pplayer, act_utype, city_owner(pcity_dest),
                             city_tile(pcity_dest),
                             city_link(pcity_dest));

  conn_list_do_unbuffer(pplayer->connections);

  /* Free data. */
  trade_route_list_destroy(routes_out_of_home);
  trade_route_list_destroy(routes_out_of_dest);

  return TRUE;
}

/**********************************************************************//**
  Change various unit server side client state.

  The server keeps various unit state that is owned by the client. The only
  consequence this state has for the game is how the client reacts to it.
  The state may be server side because the server writes to it or simply to
  have it end up in the save game.
**************************************************************************/
void handle_unit_sscs_set(struct player *pplayer,
                          int unit_id16, int unit_id32,
                          enum unit_ss_data_type type,
                          int value)
{
  struct unit *punit;
  const struct civ_map *nmap = &(wld.map);

  if (!has_capability("ids32", pplayer->current_conn->capability)) {
    unit_id32 = unit_id16;
  }

  punit = player_unit_by_number(pplayer, unit_id32);

  if (NULL == punit) {
    /* Being asked to unqueue a "spent" unit because the client haven't
     * been told that it's gone is expected. */
    if (type != USSDT_UNQUEUE) {
      /* Probably died or bribed. */
      log_verbose("handle_unit_sscs_set() invalid unit %d", unit_id32);
    }

    return;
  }

  switch (type) {
  case USSDT_QUEUE:
    /* Reminds the client to ask the server about what actions the unit can
     * perform against the target tile. Action decision state can be set by
     * the server it self too. */

    if (index_to_tile(nmap, value) == NULL) {
      /* Asked to be reminded to ask what actions the unit can do to a non
       * existing target tile. */
      log_verbose("unit_sscs_set() invalid target tile %d for unit %d",
                  value, unit_id32);
      break;
    }

    punit->action_decision_want = ACT_DEC_ACTIVE;
    punit->action_decision_tile = index_to_tile(nmap, value);

    /* Let the client know that this unit needs the player to decide
     * what to do. */
    send_unit_info(player_reply_dest(pplayer), punit);

    break;
  case USSDT_UNQUEUE:
    /* Delete the reminder for the client to ask the server about what
     * actions the unit can perform against a certain target tile.
     * Action decision state can be set by the server it self too. */

    punit->action_decision_want = ACT_DEC_NOTHING;
    punit->action_decision_tile = NULL;

    /* Let the client know that this unit no longer needs the player to
     * decide what to do. */
    send_unit_info(pplayer->connections, punit);

    break;
  case USSDT_BATTLE_GROUP:
    /* Battlegroups are handled entirely by the client, so all we have to
       do here is save the battlegroup ID so that it'll be persistent. */

    punit->battlegroup = CLIP(-1, value, MAX_NUM_BATTLEGROUPS);

    break;
  case USSDT_SENTRY:
    if (value == 0) {
      if (punit->activity != ACTIVITY_SENTRY) {
        return;
      }

      if (!unit_activity_internal(punit, ACTIVITY_IDLE)) {
        /* Impossible to set to Idle? */
        fc_assert(FALSE);
      }
    } else if (value == 1) {
      if (!can_unit_do_activity(nmap, punit, ACTIVITY_SENTRY)) {
        return;
      }

      if (!unit_activity_internal(punit, ACTIVITY_SENTRY)) {
        /* Should have been caught above */
        fc_assert(FALSE);
      }
    } else {
      log_verbose("handle_unit_sscs_set(): illegal sentry state for %s %d",
                  unit_rule_name(punit), punit->id);
    }
    break;
  }
}

/**********************************************************************//**
  Delete a unit's current plans.
**************************************************************************/
static void unit_plans_clear(struct unit *punit)
{
  /* Remove city spot reservations for AI settlers on city founding
   * mission. */
  adv_unit_new_task(punit, AUT_NONE, NULL);

  /* Get rid of old orders. */
  free_unit_orders(punit);

  /* Make sure that no old goto_tile remains. */
  punit->goto_tile = NULL;
}

/**********************************************************************//**
  Handle request to change controlling server side agent.
**************************************************************************/
void handle_unit_server_side_agent_set(struct player *pplayer,
                                       int unit_id16, int unit_id32,
                                       enum server_side_agent agent)
{
  struct unit *punit;

  if (!has_capability("ids32", pplayer->current_conn->capability)) {
    unit_id32 = unit_id16;
  }

  punit = player_unit_by_number(pplayer, unit_id32);

  if (NULL == punit) {
    /* Probably died or bribed. */
    log_verbose("handle_unit_server_side_agent_set() invalid unit %d",
                unit_id32);
    return;
  }

  if (!server_side_agent_is_valid(agent)) {
    /* Client error. */
    log_verbose("handle_unit_server_side_agent_set() invalid agent %d",
                agent);
    return;
  }

  /* Set the state or exit */
  if (!unit_server_side_agent_set(pplayer, punit, agent)) {
    return;
  }

  /* Give the new agent a blank slate */
  unit_plans_clear(punit);

  if (agent == SSA_AUTOEXPLORE) {
    if (!unit_activity_internal(punit, ACTIVITY_EXPLORE)) {
      /* Should have been caught above */
      fc_assert(FALSE);
      punit->ssa_controller = SSA_NONE;
    }

    /* Exploring is handled here explicitly, since the player expects to
     * see an immediate response from setting a unit to auto-explore.
     * Handling it deeper in the code leads to some tricky recursive loops -
     * see PR#2631. */
    if (punit->moves_left > 0) {
      do_explore(punit);
    }
  }
}

/**********************************************************************//**
  Change controlling server side agent.
  @returns TRUE iff the server side agent was changed.
**************************************************************************/
bool unit_server_side_agent_set(struct player *pplayer,
                                struct unit *punit,
                                enum server_side_agent agent)
{
  const struct civ_map *nmap = &(wld.map);

  /* Check that the agent can be activated for this unit. */
  switch (agent) {
  case SSA_AUTOSETTLER:
    if (!can_unit_do_autosettlers(punit)) {
      return FALSE;
    }
    break;
  case SSA_AUTOEXPLORE:
    if (!can_unit_do_activity(nmap, punit, ACTIVITY_EXPLORE)) {
      return FALSE;
    }
    break;
  case SSA_NONE:
    /* Always possible. */
    break;
  case SSA_COUNT:
    fc_assert_ret_val(agent != SSA_COUNT, FALSE);
    break;
  }

  if (punit->ssa_controller != agent) {
    punit->ssa_controller = agent;
    send_unit_info(NULL, punit);
  }

  return TRUE;
}

/**********************************************************************//**
  Update everything that needs changing when unit activity changes from
  old activity to new one.
**************************************************************************/
static void unit_activity_dependencies(struct unit *punit,
				       enum unit_activity old_activity,
                                       struct extra_type *old_target)
{
  switch (punit->activity) {
  case ACTIVITY_IDLE:
    switch (old_activity) {
    case ACTIVITY_PILLAGE: 
      {
        if (old_target != NULL) {
          unit_list_iterate_safe(unit_tile(punit)->units, punit2) {
            if (punit2->activity == ACTIVITY_PILLAGE) {
              extra_deps_iterate(&(punit2->activity_target->reqs), pdep) {
                if (pdep == old_target) {
                  set_unit_activity(punit2, ACTIVITY_IDLE);
                  send_unit_info(NULL, punit2);
                  break;
                }
              } extra_deps_iterate_end;
            }
          } unit_list_iterate_safe_end;
        }
        break;
      }
    case ACTIVITY_EXPLORE:
      /* Restore unit's control status */
      punit->ssa_controller = SSA_NONE;
      break;
    default: 
      ; /* do nothing */
    }
    break;
  case ACTIVITY_EXPLORE:
    punit->ssa_controller = SSA_AUTOEXPLORE;
    set_unit_activity(punit, ACTIVITY_EXPLORE);
    send_unit_info(NULL, punit);
    break;
  default:
    /* do nothing */
    break;
  }
}

/**********************************************************************//**
  Perform an action that is an activity.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
**************************************************************************/
static bool do_action_activity(struct unit *punit,
                               const struct action *paction)
{
  enum unit_activity new_activity = actres_get_activity(paction->result);

  fc_assert_ret_val(new_activity != ACTIVITY_LAST, FALSE);
  fc_assert_ret_val(!activity_requires_target(new_activity), FALSE);

  return unit_activity_internal(punit, new_activity);
}

/**********************************************************************//**
  Handle request for changing activity.
**************************************************************************/
bool unit_activity_handling(struct unit *punit,
                            enum unit_activity new_activity)
{
  const struct civ_map *nmap = &(wld.map);

  /* Must specify target for ACTIVITY_BASE */
  fc_assert_ret_val(new_activity != ACTIVITY_BASE
                    && new_activity != ACTIVITY_GEN_ROAD, FALSE);

  if (new_activity == ACTIVITY_PILLAGE) {
    struct extra_type *target = NULL;

    /* Assume untargeted pillaging if no target specified */
    unit_activity_handling_targeted(punit, new_activity, &target);
  } else if (can_unit_do_activity(nmap, punit, new_activity)) {
    free_unit_orders(punit);
    unit_activity_internal(punit, new_activity);
  }

  return TRUE;
}

/**********************************************************************//**
  Handle request for changing activity.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
**************************************************************************/
static bool unit_activity_internal(struct unit *punit,
                                   enum unit_activity new_activity)
{
  if (!can_unit_do_activity(&(wld.map), punit, new_activity)) {
    return FALSE;
  } else {
    enum unit_activity old_activity = punit->activity;
    struct extra_type *old_target = punit->activity_target;

    set_unit_activity(punit, new_activity);
    send_unit_info(NULL, punit);
    unit_activity_dependencies(punit, old_activity, old_target);

    return TRUE;
  }
}

/**********************************************************************//**
  Perform an action that is an activity.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
**************************************************************************/
static bool do_action_activity_targeted(struct unit *punit,
                                        const struct action *paction,
                                        struct extra_type **new_target)
{
  enum unit_activity new_activity = actres_get_activity(paction->result);

  fc_assert_ret_val(new_activity != ACTIVITY_LAST, FALSE);
  fc_assert_ret_val(activity_requires_target(new_activity),
                    unit_activity_internal(punit, new_activity));

  return unit_activity_targeted_internal(punit, new_activity, new_target);
}

/**********************************************************************//**
  Handle request for targeted activity.
**************************************************************************/
bool unit_activity_handling_targeted(struct unit *punit,
                                     enum unit_activity new_activity,
                                     struct extra_type **new_target)
{
  if (!activity_requires_target(new_activity)) {
    unit_activity_handling(punit, new_activity);
  } else if (can_unit_do_activity_targeted(&(wld.map), punit,
                                           new_activity, *new_target)) {
    struct action_list *list = action_list_by_activity(new_activity);

    free_unit_orders(punit);

    if (list != NULL && action_list_size(list) > 0) {
      /* Trigger action system */
      unit_do_action(unit_owner(punit), punit->id, punit->tile->index,
                     (*new_target) != NULL ? (*new_target)->id : NO_TARGET,
                     "", action_number(action_list_get(list, 0)));
    } else {
      unit_activity_targeted_internal(punit, new_activity, new_target);
    }
  }

  return TRUE;
}

/**********************************************************************//**
  Handle request for targeted activity.

  Returns TRUE iff action could be done, FALSE if it couldn't. Even if
  this returns TRUE, unit may have died during the action.
**************************************************************************/
static bool unit_activity_targeted_internal(struct unit *punit,
                                            enum unit_activity new_activity,
                                            struct extra_type **new_target)
{
  if (!can_unit_do_activity_targeted(&(wld.map), punit,
                                     new_activity, *new_target)) {
    return FALSE;
  } else {
    enum unit_activity old_activity = punit->activity;
    struct extra_type *old_target = punit->activity_target;
    enum unit_activity stored_activity = new_activity;

    unit_assign_specific_activity_target(punit,
                                         &new_activity, new_target);
    if (new_activity != stored_activity
        && !activity_requires_target(new_activity)) {
      /* unit_assign_specific_activity_target() changed our target activity
       * (to ACTIVITY_IDLE in practice) */
      unit_activity_handling(punit, new_activity);
    } else {
      set_unit_activity_targeted(punit, new_activity, *new_target);
      send_unit_info(NULL, punit);
      unit_activity_dependencies(punit, old_activity, old_target);

      if (new_activity == ACTIVITY_PILLAGE) {
        /* Casus Belli for when the activity successfully begins. */
        /* TODO: is it more logical to change Casus_Belli_Complete to
         * Casus_Belli_Successful_Beginning and trigger it here? */
        action_consequence_success(action_by_number(ACTION_PILLAGE),
                                   unit_owner(punit), unit_type_get(punit),
                                   tile_owner(unit_tile(punit)),
                                   unit_tile(punit),
                                   tile_link(unit_tile(punit)));
      }
    }

    return TRUE;
  }
}

/**********************************************************************//**
  Receives route packages.
**************************************************************************/
void handle_unit_orders(struct player *pplayer,
                        const struct packet_unit_orders *packet)
{
  int length = packet->length;
  struct unit *punit;
  const struct civ_map *nmap = &(wld.map);
  struct tile *src_tile = index_to_tile(nmap, packet->src_tile);
  struct unit_order *order_list;
  int unit_id;
#ifdef FREECIV_DEBUG
  int i;
#endif

  if (!has_capability("ids32", pplayer->current_conn->capability)) {
    unit_id = packet->unit_id16;
  } else {
    unit_id = packet->unit_id32;
  }

  punit = player_unit_by_number(pplayer, unit_id);

  if (NULL == punit) {
    /* Probably died or bribed. */
    log_verbose("handle_unit_orders() invalid unit %d", unit_id);
    return;
  }

  if (0 > length || MAX_LEN_ROUTE < length) {
    /* Shouldn't happen */
    log_error("handle_unit_orders() invalid %s (%d) "
              "packet length %d (max %d)", unit_rule_name(punit),
              unit_id, length, MAX_LEN_ROUTE);
    return;
  }

  if (src_tile != unit_tile(punit)) {
    /* Failed sanity check.  Usually this happens if the orders were sent
     * in the previous turn, and the client thought the unit was in a
     * different position than it's actually in.  The easy solution is to
     * discard the packet.  We don't send an error message to the client
     * here (though maybe we should?). */
    log_verbose("handle_unit_orders() invalid %s (%d) tile (%d, %d) "
                "!= (%d, %d)", unit_rule_name(punit), punit->id,
                TILE_XY(src_tile), TILE_XY(unit_tile(punit)));
    return;
  }

  if (ACTIVITY_IDLE != punit->activity) {
    /* New orders implicitly abandon current activity */
    unit_activity_handling(punit, ACTIVITY_IDLE);
  }

  if (length) {
    order_list = create_unit_orders(nmap, length, packet->orders);
    if (!order_list) {
      log_error("received invalid orders from %s for %s (%d).",
                player_name(pplayer), unit_rule_name(punit), unit_id);
      return;
    }
  }

  /* This must be before old orders are freed. If this is
   * settlers on city founding mission, city spot reservation
   * from goto_tile must be freed, and free_unit_orders() loses
   * goto_tile information */
  adv_unit_new_task(punit, AUT_NONE, NULL);

  free_unit_orders(punit);
  /* If we waited on a tile, reset punit->done_moving */
  punit->done_moving = (punit->moves_left <= 0);

  if (length == 0) {
    fc_assert(!unit_has_orders(punit));
    send_unit_info(NULL, punit);
    return;
  }

  punit->has_orders = TRUE;
  punit->orders.length = length;
  punit->orders.index = 0;
  punit->orders.repeat = packet->repeat;
  punit->orders.vigilant = packet->vigilant;
  if (length) {
    punit->orders.list = order_list;
  }

  if (!packet->repeat) {
    punit->goto_tile = index_to_tile(nmap, packet->dest_tile);
  } else {
    /* Make sure that no old goto_tile remains. */
    punit->goto_tile = NULL;
  }

#ifdef FREECIV_DEBUG
  log_debug("Orders for unit %d: length:%d", unit_id, length);
  for (i = 0; i < length; i++) {
    log_debug("  %d,%s,%s,%d,%d",
              packet->orders[i].order, dir_get_name(packet->orders[i].dir),
              packet->orders[i].order == ORDER_PERFORM_ACTION ?
                action_id_rule_name(packet->orders[i].action) :
                packet->orders[i].order == ORDER_ACTIVITY ?
                  unit_activity_name(packet->orders[i].activity) :
                  "no action/activity required",
              packet->orders[i].target,
              packet->orders[i].sub_target);
  }
#endif /* FREECIV_DEBUG */

  if (!is_player_phase(unit_owner(punit), game.info.phase)
      || execute_orders(punit, TRUE)) {
    /* Looks like the unit survived. */
    send_unit_info(NULL, punit);
  }
}

/**********************************************************************//**
  Handle worker task assigned to the city
**************************************************************************/
void handle_worker_task(struct player *pplayer,
                        const struct packet_worker_task *packet)
{
  struct city *pcity;
  struct worker_task *ptask = NULL;
  struct tile *ptile = index_to_tile(&(wld.map), packet->tile_id);

  if (!has_capability("ids32", pplayer->current_conn->capability)) {
    pcity = game_city_by_number(packet->city_id16);
  } else {
    pcity = game_city_by_number(packet->city_id32);
  }

  if (pcity == NULL || pcity->owner != pplayer || ptile == NULL) {
    return;
  }

  worker_task_list_iterate(pcity->task_reqs, ptask_old) {
    if (tile_index(ptask_old->ptile) == packet->tile_id) {
      ptask = ptask_old;
    }
  } worker_task_list_iterate_end;

  if (ptask == NULL) {
    if (packet->activity == ACTIVITY_LAST) {
      return;
    }

    ptask = fc_malloc(sizeof(struct worker_task));
    worker_task_init(ptask);
    worker_task_list_append(pcity->task_reqs, ptask);
  } else {
    if (packet->activity == ACTIVITY_LAST) {
      worker_task_list_remove(pcity->task_reqs, ptask);
      free(ptask);
      ptask = NULL;
    }
  }

  if (ptask != NULL) {
    ptask->ptile = ptile;
    ptask->act = packet->activity;
    if (packet->tgt >= 0) {
      if (packet->tgt < MAX_EXTRA_TYPES) {
        ptask->tgt = extra_by_number(packet->tgt);
      } else {
        log_debug("Illegal worker task target %d", packet->tgt);
        ptask->tgt = NULL;
      }
    } else {
      ptask->tgt = NULL;
    }
    ptask->want = packet->want;
  }

  if (ptask && !worker_task_is_sane(ptask)) {
    log_debug("Bad worker task");
    worker_task_list_remove(pcity->task_reqs, ptask);
    free(ptask);
    ptask = NULL;
    return;
  }

  lsend_packet_worker_task(pplayer->connections, packet);
}
