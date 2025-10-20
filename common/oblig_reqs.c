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

#include <stdarg.h>

/* utility */
#include "mem.h"

/* common */
#include "actres.h"
#include "nation.h"
#include "player.h"

#include "oblig_reqs.h"


/* Hard requirements relates to action result. */
static struct obligatory_req_vector oblig_hard_reqs_r[ACTRES_NONE];
static struct obligatory_req_vector oblig_hard_reqs_sr[ACT_SUB_RES_COUNT];


/**********************************************************************//**
  Returns a new array of alternative action enabler contradictions. Only
  one has to not contradict the enabler for it to be seen as fulfilled.

  @param alternatives the number of action enabler contradictions
                      followed by the enabler contradictions specified as
                      alternating contradicting requirement and a bool
                      that is TRUE if the requirement contradicts the
                      enabler's target requirement vector and FALSE if it
                      contradicts the enabler's actor vector.
  @returns a new array of alternative action enabler contradictions.
**************************************************************************/
static struct ae_contra_or *req_contradiction_or(int alternatives, ...)
{
  struct ae_contra_or *out;
  int i;
  va_list args;

  fc_assert_ret_val(alternatives > 0, NULL);
  out = fc_malloc(sizeof(*out));
  out->users = 0;
  out->alternatives = alternatives;
  out->alternative = fc_malloc(sizeof(out->alternative[0]) * alternatives);

  va_start(args, alternatives);
  for (i = 0; i < alternatives; i++) {
    struct requirement contradiction = va_arg(args, struct requirement);
    bool is_target = va_arg(args, int);

    out->alternative[i].req = contradiction;
    out->alternative[i].is_target = is_target;
  }
  va_end(args);

  return out;
}

/**********************************************************************//**
  Tell an ae_contra_or that one of its users is done with it.

  @param contra the ae_contra_or the user is done with.
**************************************************************************/
static void ae_contra_close(struct ae_contra_or *contra)
{
  contra->users--;

  if (contra->users < 1) {
    /* No users left. Delete. */
    FC_FREE(contra->alternative);
    FC_FREE(contra);
  }
}

/**********************************************************************//**
  Register an obligatory hard requirement for the specified action sub
  results.

  @param contras if one alternative here doesn't contradict the enabler it
                 is accepted.
  @param error_message error message if an enabler contradicts all contras.
  @param args list of action sub results that should be unable to
              contradict all specified contradictions.
**************************************************************************/
static void voblig_hard_req_reg_sub_res(struct ae_contra_or *contras,
                                        const char *error_message,
                                        va_list args)
{
  struct obligatory_req oreq;
  enum action_sub_result res;

  /* A non null action message is used to indicate that an obligatory hard
   * requirement is missing. */
  fc_assert_ret(error_message);

  /* Pack the obligatory hard requirement. */
  oreq.contras = contras;
  oreq.error_msg = error_message;

  /* Add the obligatory hard requirement to each action sub result it
   * applies to. */
  while (ACT_SUB_RES_COUNT != (res = va_arg(args,
                                            enum action_sub_result))) {
    /* Any invalid action sub result should terminate the loop before this
     * assertion. */
    fc_assert_ret_msg(action_sub_result_is_valid(res),
                      "Invalid action result %d", res);

    /* Add to list for action result */
    obligatory_req_vector_append(&oblig_hard_reqs_sr[res], oreq);

    /* Register the new user. */
    oreq.contras->users++;
  }
}

/**********************************************************************//**
  Register an obligatory hard requirement for the specified action sub
  results.

  @param contras if one alternative here doesn't contradict the enabler it
                 is accepted.
  @param error_message error message if an enabler contradicts all contras.
                       Followed by a list of action sub results that should
                       be unable to contradict all specified
                       contradictions.
**************************************************************************/
static void oblig_hard_req_reg_sub_res(struct ae_contra_or *contras,
                                       const char *error_message,
                                       ...)
{
  va_list args;

  /* Add the obligatory hard requirement to each action result it applies
   * to. */
  va_start(args, error_message);
  voblig_hard_req_reg_sub_res(contras, error_message, args);
  va_end(args);
}

/**********************************************************************//**
  Register an obligatory hard requirement for the specified action results.

  @param contras if one alternative here doesn't contradict the enabler it
                 is accepted.
  @param error_message error message if an enabler contradicts all contras.
  @param args list of action results that should be unable to contradict
              all specified contradictions.
**************************************************************************/
static void voblig_hard_req_reg(struct ae_contra_or *contras,
                                const char *error_message,
                                va_list args)
{
  struct obligatory_req oreq;
  enum action_result res;
  int users = 0;

  /* A non null action message is used to indicate that an obligatory hard
   * requirement is missing. */
  fc_assert_ret(error_message);

  /* Pack the obligatory hard requirement. */
  oreq.contras = contras;
  oreq.error_msg = error_message;

  /* Add the obligatory hard requirement to each action result it applies
   * to. */
  while (ACTRES_NONE != (res = va_arg(args, enum action_result))) {
    /* Any invalid action result should terminate the loop before this
     * assertion. */
    fc_assert_ret_msg(action_result_is_valid(res),
                      "Invalid action result %d", res);

    /* Add to list for action result */
    obligatory_req_vector_append(&oblig_hard_reqs_r[res], oreq);

    /* Register the new user. */
    users++;
  }

  fc_assert(users > 0);

  oreq.contras->users += users;
}

/**********************************************************************//**
  Register an obligatory hard requirement for the specified action results.

  @param contras if one alternative here doesn't contradict the enabler it
                 is accepted.
  @param error_message error message if an enabler contradicts all contras.
                       Followed by a list of action results that should be
                       unable to contradict all specified contradictions.
**************************************************************************/
static void oblig_hard_req_reg(struct ae_contra_or *contras,
                               const char *error_message,
                               ...)
{
  va_list args;

  /* Add the obligatory hard requirement to each action result it applies
   * to. */
  va_start(args, error_message);
  voblig_hard_req_reg(contras, error_message, args);
  va_end(args);
}

/**********************************************************************//**
  Register an obligatory hard requirement for the action results it
  applies to.

  The vararg parameter is a list of action ids it applies to terminated
  by ACTION_NONE.
**************************************************************************/
static void oblig_hard_req_register(struct requirement contradiction,
                                    bool is_target,
                                    const char *error_message,
                                    ...)
{
  struct ae_contra_or *contra;
  va_list args;

  /* Pack the obligatory hard requirement. */
  contra = req_contradiction_or(1, contradiction, is_target);

  /* Add the obligatory hard requirement to each action result it applies
   * to. */
  va_start(args, error_message);
  voblig_hard_req_reg(contra, error_message, args);
  va_end(args);
}

/**********************************************************************//**
  Hard code the obligatory hard requirements that don't depend on the rest
  of the ruleset. They are sorted by requirement to make it easy to read,
  modify and explain them.
**************************************************************************/
void hard_code_oblig_hard_reqs(void)
{
  /* Why this is a hard requirement: There is currently no point in
   * allowing the listed actions against domestic targets.
   * (Possible counter argument: crazy hack involving the Lua
   * callback action_started_callback() to use an action to do
   * something else. */
  /* TODO: Unhardcode as a part of false flag operation support. */
  oblig_hard_req_register(req_from_values(VUT_DIPLREL, REQ_RANGE_LOCAL,
                                          FALSE, FALSE, TRUE, DRO_FOREIGN),
                          FALSE,
                          N_("All action enablers for %s must require a "
                             "foreign target."),
                          ACTRES_ESTABLISH_EMBASSY,
                          ACTRES_SPY_INVESTIGATE_CITY,
                          ACTRES_SPY_STEAL_GOLD,
                          ACTRES_STEAL_MAPS,
                          ACTRES_SPY_STEAL_TECH,
                          ACTRES_SPY_TARGETED_STEAL_TECH,
                          ACTRES_SPY_INCITE_CITY,
                          ACTRES_SPY_BRIBE_UNIT,
                          ACTRES_CAPTURE_UNITS,
                          ACTRES_CONQUER_CITY,
                          ACTRES_SPY_ESCAPE,
                          ACTRES_NONE);
  /* The same for tile targeted actions that also can be done to unclaimed
   * tiles. */
  oblig_hard_req_reg(req_contradiction_or(
                       2,
                       req_from_values(VUT_DIPLREL, REQ_RANGE_LOCAL,
                                       FALSE, FALSE, TRUE, DRO_FOREIGN),
                       FALSE,
                       req_from_values(VUT_CITYTILE, REQ_RANGE_TILE,
                                       FALSE, TRUE, TRUE,
                                       CITYT_CLAIMED),
                       TRUE),
                     /* TRANS: error message for ruledit */
                     N_("All action enablers for %s must require a "
                        "non domestic target."),
                     ACTRES_PARADROP_CONQUER,
                     ACTRES_NONE);
  /* The same for tile extras targeted actions that also can be done to
   * unowned extras. */
  oblig_hard_req_reg(req_contradiction_or(
                       2,
                       req_from_values(VUT_DIPLREL, REQ_RANGE_LOCAL,
                                       FALSE, FALSE, TRUE, DRO_FOREIGN),
                       FALSE,
                       req_from_values(VUT_CITYTILE, REQ_RANGE_TILE,
                                       FALSE, TRUE, TRUE,
                                       CITYT_EXTRAS_OWNED),
                       TRUE),
                     /* TRANS: error message for ruledit */
                     N_("All action enablers for %s must require a "
                        "non domestic target."),
                     ACTRES_CONQUER_EXTRAS,
                     ACTRES_NONE);

  /* Why this is a hard requirement: There is currently no point in
   * establishing an embassy when a real embassy already exists.
   * (Possible exception: crazy hack using the Lua callback
   * action_started_callback() to make establish embassy do something
   * else even if the UI still call the action Establish Embassy) */
  oblig_hard_req_register(req_from_values(VUT_DIPLREL, REQ_RANGE_LOCAL,
                                          FALSE, TRUE, TRUE,
                                          DRO_HAS_REAL_EMBASSY),
                          FALSE,
                          N_("All action enablers for %s must require the"
                             " absence of a real embassy."),
                          ACTRES_ESTABLISH_EMBASSY,
                          ACTRES_NONE);

  /* Why this is a hard requirement: There is currently no point in
   * fortifying an already fortified unit. */
  oblig_hard_req_register(req_from_values(VUT_ACTIVITY, REQ_RANGE_LOCAL,
                                          FALSE, TRUE, TRUE,
                                          ACTIVITY_FORTIFIED),
                          FALSE,
                          N_("All action enablers for %s must require that"
                             " the actor unit isn't already fortified."),
                          ACTRES_FORTIFY,
                          ACTRES_NONE);

  /* Why this is a hard requirement: Keep the old rules. Need to work
   * out corner cases. */
  oblig_hard_req_register(req_from_values(VUT_DIPLREL, REQ_RANGE_LOCAL,
                                          FALSE, TRUE, TRUE, DRO_FOREIGN),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " a domestic target."),
                          ACTRES_UPGRADE_UNIT, ACTRES_NONE);

  /* Why this is a hard requirement: The code expects that only domestic and
   * allied transports can be boarded. */
  oblig_hard_req_register(req_from_values(VUT_DIPLREL, REQ_RANGE_LOCAL,
                                          FALSE, TRUE, TRUE, DS_ARMISTICE),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " a domestic or allied target."),
                          ACTRES_TRANSPORT_EMBARK,
                          ACTRES_TRANSPORT_BOARD,
                          ACTRES_TRANSPORT_LOAD,
                          ACTRES_NONE);
  oblig_hard_req_register(req_from_values(VUT_DIPLREL, REQ_RANGE_LOCAL,
                                          FALSE, TRUE, TRUE, DS_WAR),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " a domestic or allied target."),
                          ACTRES_TRANSPORT_EMBARK,
                          ACTRES_TRANSPORT_BOARD,
                          ACTRES_TRANSPORT_LOAD,
                          ACTRES_NONE);
  oblig_hard_req_register(req_from_values(VUT_DIPLREL, REQ_RANGE_LOCAL,
                                          FALSE, TRUE, TRUE, DS_CEASEFIRE),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " a domestic or allied target."),
                          ACTRES_TRANSPORT_EMBARK,
                          ACTRES_TRANSPORT_BOARD,
                          ACTRES_TRANSPORT_LOAD,
                          ACTRES_NONE);
  oblig_hard_req_register(req_from_values(VUT_DIPLREL, REQ_RANGE_LOCAL,
                                          FALSE, TRUE, TRUE, DS_PEACE),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " a domestic or allied target."),
                          ACTRES_TRANSPORT_EMBARK,
                          ACTRES_TRANSPORT_BOARD,
                          ACTRES_TRANSPORT_LOAD,
                          ACTRES_NONE);
  oblig_hard_req_register(req_from_values(VUT_DIPLREL, REQ_RANGE_LOCAL,
                                          FALSE, TRUE, TRUE, DS_NO_CONTACT),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " a domestic or allied target."),
                          ACTRES_TRANSPORT_EMBARK,
                          ACTRES_TRANSPORT_BOARD,
                          ACTRES_TRANSPORT_LOAD,
                          ACTRES_NONE);

  /* Why this is a hard requirement: Preserve semantics of the Settlers
   * unit type flag. */
  oblig_hard_req_register(req_from_values(VUT_UTFLAG, REQ_RANGE_LOCAL,
                                          FALSE, FALSE, TRUE,
                                          UTYF_WORKERS),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " that the actor has"
                             " the Workers utype flag."),
                          ACTRES_TRANSFORM_TERRAIN,
                          ACTRES_CULTIVATE,
                          ACTRES_PLANT,
                          ACTRES_ROAD,
                          ACTRES_MINE,
                          ACTRES_IRRIGATE,
                          ACTRES_CLEAN,
                          ACTRES_NONE);

  /* Why this is a hard requirement: Preserve semantics of the rule that a
   * *_time of 0 disables the action. */
  oblig_hard_req_register(req_from_values(VUT_TERRAINALTER, REQ_RANGE_TILE,
                                          FALSE, FALSE, FALSE,
                                          TA_CAN_IRRIGATE),
                          TRUE,
                          N_("All action enablers for %s must require"
                             " that the target"
                             " tile's terrain's irrigation_time"
                             " is above 0. (See \"TerrainAlter\"'s"
                             " \"CanIrrigate\")"),
                          ACTRES_IRRIGATE,
                          ACTRES_NONE);
  oblig_hard_req_register(req_from_values(VUT_TERRAINALTER, REQ_RANGE_TILE,
                                          FALSE, FALSE, FALSE,
                                          TA_CAN_MINE),
                          TRUE,
                          N_("All action enablers for %s must require"
                             " that the target"
                             " tile's terrain's mining_time"
                             " is above 0. (See \"TerrainAlter\"'s"
                             " \"CanMine\")"),
                          ACTRES_MINE,
                          ACTRES_NONE);
  oblig_hard_req_register(req_from_values(VUT_TERRAINALTER, REQ_RANGE_TILE,
                                          FALSE, FALSE, FALSE,
                                          TA_CAN_ROAD),
                          TRUE,
                          N_("All action enablers for %s must require"
                             " that the target"
                             " tile's terrain's road_time"
                             " is above 0. (See \"TerrainAlter\"'s"
                             " \"CanRoad\")"),
                          ACTRES_ROAD,
                          ACTRES_NONE);
  oblig_hard_req_register(req_from_values(VUT_TERRAINALTER, REQ_RANGE_TILE,
                                          FALSE, FALSE, FALSE,
                                          TA_CAN_BASE),
                          TRUE,
                          N_("All action enablers for %s must require"
                             " that the target"
                             " tile's terrain's base_time"
                             " is above 0. (See \"TerrainAlter\"'s"
                             " \"CanBase\")"),
                          ACTRES_BASE,
                          ACTRES_NONE);

  /* Why this is a hard requirement: Preserve semantics of the NoCities
   * terrain flag. */
  oblig_hard_req_register(req_from_values(VUT_TERRFLAG, REQ_RANGE_TILE,
                                          FALSE, TRUE, TRUE,
                                          TER_NO_CITIES),
                          TRUE,
                          N_("All action enablers for %s must require that"
                             " the target doesn't have"
                             " the NoCities terrain flag."),
                          ACTRES_FOUND_CITY,
                          ACTRES_NONE);

  /* It isn't possible to establish a trade route from a non existing
   * city. The Freeciv code assumes this applies to Enter Marketplace
   * too. */
  oblig_hard_req_register(req_from_values(VUT_UNITSTATE, REQ_RANGE_LOCAL,
                                          FALSE, FALSE, TRUE,
                                          USP_HAS_HOME_CITY),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " that the actor has a home city."),
                          ACTRES_TRADE_ROUTE,
                          ACTRES_MARKETPLACE,
                          ACTRES_NONE);

  /* Why this is a hard requirement:
   * - preserve the semantics of the NonMil unit type flag. */
  oblig_hard_req_reg(req_contradiction_or(
                       3,
                       req_from_values(VUT_UTFLAG, REQ_RANGE_LOCAL,
                                       FALSE, FALSE, TRUE,
                                       UTYF_CIVILIAN),
                       FALSE,
                       req_from_values(VUT_DIPLREL, REQ_RANGE_LOCAL,
                                       FALSE, TRUE, TRUE, DS_PEACE),
                       FALSE,
                       req_from_values(VUT_CITYTILE, REQ_RANGE_TILE,
                                       FALSE, TRUE, TRUE,
                                       CITYT_CLAIMED),
                       TRUE),
                     /* TRANS: error message for ruledit */
                     N_("All action enablers for %s must require"
                        " that the actor has the NonMil utype flag"
                        " or that the target tile is unclaimed"
                        " or that the diplomatic relation to"
                        " the target tile owner isn't peace."),
                     ACTRES_PARADROP,
                     ACTRES_PARADROP_CONQUER,
                     ACTRES_NONE);

  /* Why this is a hard requirement: Preserve semantics of NonMil
   * flag. Need to replace other uses in game engine before this can
   * be demoted to a regular unit flag. */
  oblig_hard_req_register(req_from_values(VUT_UTFLAG, REQ_RANGE_LOCAL,
                                          FALSE, TRUE, TRUE, UTYF_CIVILIAN),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " that the actor doesn't have"
                             " the NonMil utype flag."),
                          ACTRES_ATTACK,
                          ACTRES_CONQUER_CITY,
                          ACTRES_WIPE_UNITS,
                          ACTRES_NONE);
  oblig_hard_req_reg(req_contradiction_or(
                       2,
                       req_from_values(VUT_UTFLAG, REQ_RANGE_LOCAL,
                                       FALSE, TRUE, TRUE, UTYF_CIVILIAN),
                       FALSE,
                       req_from_values(VUT_CITYTILE, REQ_RANGE_TILE,
                                       FALSE, TRUE, TRUE,
                                       CITYT_CENTER),
                       TRUE),
                     /* TRANS: error message for ruledit */
                     N_("All action enablers for %s must require"
                        " no city at the target tile or"
                        " that the actor doesn't have"
                        " the NonMil utype flag."),
                     ACTRES_PARADROP_CONQUER,
                     ACTRES_NONE);

  /* Why this is a hard requirement: Preserve semantics of
   * CanOccupyCity unit class flag. */
  oblig_hard_req_register(req_from_values(VUT_UCFLAG, REQ_RANGE_LOCAL,
                                          FALSE, FALSE, TRUE,
                                          UCF_CAN_OCCUPY_CITY),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " that the actor has"
                             " the CanOccupyCity uclass flag."),
                          ACTRES_CONQUER_CITY,
                          ACTRES_NONE);
  oblig_hard_req_reg(req_contradiction_or(
                       2,
                       req_from_values(VUT_UCFLAG, REQ_RANGE_LOCAL,
                                       FALSE, FALSE, TRUE,
                                       UCF_CAN_OCCUPY_CITY),
                       FALSE,
                       req_from_values(VUT_CITYTILE, REQ_RANGE_TILE,
                                       FALSE, TRUE, TRUE,
                                       CITYT_CENTER),
                       TRUE),
                     /* TRANS: error message for ruledit */
                     N_("All action enablers for %s must require"
                        " no city at the target tile or"
                        " that the actor has"
                        " the CanOccupyCity uclass flag."),
                     ACTRES_PARADROP_CONQUER,
                     ACTRES_NONE);

  /* Why this is a hard requirement: Consistency with ACTRES_ATTACK.
   * Assumed by other locations in the Freeciv code. Examples:
   * unit_move_to_tile_test(), unit_conquer_city() and do_paradrop(). */
  oblig_hard_req_register(req_from_values(VUT_DIPLREL, REQ_RANGE_LOCAL,
                                          FALSE, FALSE, TRUE, DS_WAR),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " that the actor is at war with the target."),
                          ACTRES_CONQUER_CITY,
                          ACTRES_NONE);
  oblig_hard_req_reg(req_contradiction_or(
                       2,
                       req_from_values(VUT_DIPLREL, REQ_RANGE_LOCAL,
                                       FALSE, FALSE, TRUE, DS_WAR),
                       FALSE,
                       req_from_values(VUT_CITYTILE, REQ_RANGE_TILE,
                                       FALSE, TRUE, TRUE,
                                       CITYT_CENTER),
                       TRUE),
                     /* TRANS: error message for ruledit */
                     N_("All action enablers for %s must require"
                        " no city at the target tile or"
                        " that the actor is at war with the target."),
                     ACTRES_PARADROP_CONQUER,
                     ACTRES_NONE);

  /* Why this is a hard requirement: a unit must move into a city to
   * conquer it, move into a transport to embark, move out of a transport
   * to disembark from it, move into an extra to conquer it and move into a
   * hut to enter/frighten it. */
  oblig_hard_req_register(req_from_values(VUT_MINMOVES, REQ_RANGE_LOCAL,
                                          FALSE, FALSE, TRUE, 1),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " that the actor has a movement point left."),
                          ACTRES_CONQUER_CITY,
                          ACTRES_TRANSPORT_DISEMBARK,
                          ACTRES_TRANSPORT_EMBARK,
                          ACTRES_CONQUER_EXTRAS,
                          ACTRES_HUT_ENTER,
                          ACTRES_HUT_FRIGHTEN,
                          ACTRES_UNIT_MOVE,
                          ACTRES_NONE);

  /* Why this is a hard requirement: this eliminates the need to
   * check if units transported by the actor unit can exist at the
   * same tile as all the units in the occupied city.
   *
   * This makes an implicit rule explicit:
   * 1. A unit must move into a city to conquer it.
   * 2. It can't move into the city if the tile contains a non-allied
   *    unit (see unit_move_to_tile_test()).
   * 3. A city could, at the time this rule was made explicit, only
   *    contain units allied to its owner.
   * 3. A player could, at the time this rule was made explicit, not
   *    be allied to a player that is at war with another ally.
   * 4. A player could, at the time this rule was made explicit, only
   *    conquer a city belonging to someone they were at war with.
   * Conclusion: the conquered city had to be empty.
   */
  oblig_hard_req_register(req_from_values(VUT_MAXTILETOTALUNITS, REQ_RANGE_TILE,
                                          FALSE, FALSE, TRUE, 0),
                          TRUE,
                          N_("All action enablers for %s must require"
                             " that the target city is empty."),
                          ACTRES_CONQUER_CITY,
                          ACTRES_NONE);

  /* Why this is a hard requirement: Assumed in the code. Corner case
   * where diplomacy prevents a transported unit to go to the target
   * tile. The paradrop code doesn't check if transported units can
   * coexist with the target tile city and units. */
  oblig_hard_req_register(req_from_values(VUT_UNITSTATE, REQ_RANGE_LOCAL,
                                          FALSE, TRUE, TRUE,
                                          USP_TRANSPORTING),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " that the actor isn't transporting"
                             " another unit."),
                          ACTRES_PARADROP,
                          ACTRES_PARADROP_CONQUER,
                          ACTRES_AIRLIFT,
                          ACTRES_NONE);

  /* Why this is a hard requirement: sanity. */
  oblig_hard_req_register(req_from_values(VUT_UNITSTATE, REQ_RANGE_LOCAL,
                                          FALSE, FALSE, FALSE,
                                          USP_HAS_HOME_CITY),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " that the actor has a home city."),
                          ACTRES_HOMELESS, ACTRES_NONE);

  /* Why this is a hard requirement: Assumed in the code.
   * See hrm Bug #772516 - https://www.hostedredmine.com/issues/772516 */
  oblig_hard_req_register(req_from_values(VUT_UNITSTATE, REQ_RANGE_LOCAL,
                                          FALSE, TRUE, TRUE,
                                          USP_TRANSPORTING),
                          TRUE,
                          N_("All action enablers for %s must require"
                             " that the target isn't transporting another"
                             " unit."),
                          ACTRES_CAPTURE_UNITS, ACTRES_NONE);

  /* Why this is a hard requirement: sanity. */
  oblig_hard_req_register(req_from_values(VUT_UNITSTATE, REQ_RANGE_LOCAL,
                                          FALSE, FALSE, TRUE,
                                          USP_TRANSPORTING),
                          TRUE,
                          N_("All action enablers for %s must require"
                             " that the target is transporting a unit."),
                          ACTRES_TRANSPORT_DEBOARD, ACTRES_NONE);
  oblig_hard_req_register(req_from_values(VUT_UNITSTATE, REQ_RANGE_LOCAL,
                                          FALSE, FALSE, TRUE,
                                          USP_TRANSPORTED),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " that the actor is transported."),
                          ACTRES_TRANSPORT_DEBOARD,
                          ACTRES_TRANSPORT_DISEMBARK,
                          ACTRES_NONE);
  oblig_hard_req_register(req_from_values(VUT_UNITSTATE, REQ_RANGE_LOCAL,
                                          FALSE, FALSE, TRUE,
                                          USP_LIVABLE_TILE),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " that the actor is on a livable tile."),
                          ACTRES_TRANSPORT_DEBOARD, ACTRES_NONE);

  /* Why this is a hard requirement: sanity. */
  oblig_hard_req_register(req_from_values(VUT_UNITSTATE, REQ_RANGE_LOCAL,
                                          FALSE, FALSE, TRUE,
                                          USP_TRANSPORTING),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " that the actor is transporting a unit."),
                          ACTRES_TRANSPORT_UNLOAD, ACTRES_NONE);
  oblig_hard_req_register(req_from_values(VUT_UNITSTATE, REQ_RANGE_LOCAL,
                                          FALSE, FALSE, TRUE,
                                          USP_TRANSPORTED),
                          TRUE,
                          N_("All action enablers for %s must require"
                             " that the target is transported."),
                          ACTRES_TRANSPORT_UNLOAD, ACTRES_NONE);
  oblig_hard_req_register(req_from_values(VUT_UNITSTATE, REQ_RANGE_LOCAL,
                                          FALSE, FALSE, TRUE,
                                          USP_LIVABLE_TILE),
                          TRUE,
                          N_("All action enablers for %s must require"
                             " that the target is on a livable tile."),
                          ACTRES_TRANSPORT_UNLOAD, ACTRES_NONE);

  /* Why this is a hard requirement: Use ACTRES_TRANSPORT_DISEMBARK to
   * disembark. Assumed by the Freeciv code. */
  oblig_hard_req_register(req_from_values(VUT_UNITSTATE, REQ_RANGE_LOCAL,
                                          FALSE, TRUE, TRUE,
                                          USP_TRANSPORTED),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " that the actor isn't transported."),
                          ACTRES_UNIT_MOVE,
                          ACTRES_TELEPORT, ACTRES_TELEPORT_CONQUER,
                          ACTRES_NONE);

  /* Why this is a hard requirement: assumed by the Freeciv code. */
  oblig_hard_req_register(req_from_values(VUT_CITYTILE, REQ_RANGE_TILE,
                                          FALSE, FALSE, TRUE,
                                          CITYT_CENTER),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " that the actor unit is in a city."),
                          ACTRES_AIRLIFT,
                          ACTRES_NONE);

  /* Why this is a hard requirement: Give meaning to the HutFrighten unit
   * class flag. The point of it is to keep our options open for how both
   * entering and frightening a hut at the same tile should be handled.
   * See hrm Feature #920427 */
  oblig_hard_req_reg_sub_res(req_contradiction_or(
                               1,
                               req_from_values(VUT_UCFLAG, REQ_RANGE_LOCAL,
                                               FALSE, TRUE, FALSE,
                                               UCF_HUT_FRIGHTEN),
                               FALSE),
                             N_("All action enablers for %s must require"
                                " that the actor unit doesn't have the"
                                " HutFrighten unit class flag."),
                             ACT_SUB_RES_HUT_ENTER,
                             ACT_SUB_RES_COUNT);
  oblig_hard_req_reg_sub_res(req_contradiction_or(
                               1,
                               req_from_values(VUT_UCFLAG, REQ_RANGE_LOCAL,
                                               FALSE, FALSE, FALSE,
                                               UCF_HUT_FRIGHTEN),
                               FALSE),
                             N_("All action enablers for %s must require"
                                " that the actor unit has the HutFrighten"
                                " unit class flag."),
                             ACT_SUB_RES_HUT_FRIGHTEN,
                             ACT_SUB_RES_COUNT);
}

/**********************************************************************//**
  Hard code the obligatory hard requirements that needs access to the
  ruleset before they can be generated.
**************************************************************************/
void hard_code_oblig_hard_reqs_ruleset(void)
{
  /* Why this is a hard requirement: the "animal can't conquer a city"
   * rule. Assumed in unit_can_take_over(). */
  nations_iterate(pnation) {
    if (nation_barbarian_type(pnation) == ANIMAL_BARBARIAN) {
      oblig_hard_req_register(req_from_values(VUT_NATION, REQ_RANGE_PLAYER,
                                              FALSE, TRUE, TRUE,
                                              nation_number(pnation)),
                              FALSE,
                              N_("All action enablers for %s must require"
                                 " a non animal player actor."),
                              ACTRES_CONQUER_CITY,
                              ACTRES_NONE);
      oblig_hard_req_reg(req_contradiction_or(
                           2,
                           req_from_values(VUT_NATION, REQ_RANGE_PLAYER,
                                           FALSE, TRUE, TRUE,
                                           nation_number(pnation)),
                           FALSE,
                           req_from_values(VUT_CITYTILE, REQ_RANGE_TILE,
                                           FALSE, TRUE, TRUE,
                                           CITYT_CENTER),
                           TRUE),
                         /* TRANS: error message for ruledit */
                         N_("All action enablers for %s must require"
                            " no city at the target tile or"
                            " a non animal player actor."),
                         ACTRES_PARADROP_CONQUER,
                         ACTRES_NONE);
    }
  } nations_iterate_end;
}

/**********************************************************************//**
  Initialize hard reqs system.
**************************************************************************/
void oblig_hard_reqs_init(void)
{
  int i;

  /* Obligatory hard requirements sorted by action result in memory. */
  for (i = 0; i < ACTRES_NONE; i++) {
    /* Prepare each action result's storage area. */
    obligatory_req_vector_init(&oblig_hard_reqs_r[i]);
  }

  /* Obligatory hard requirements sorted by action sub result in memory. */
  for (i = 0; i < ACT_SUB_RES_COUNT; i++) {
    /* Prepare each action sub result's storage area. */
    obligatory_req_vector_init(&oblig_hard_reqs_sr[i]);
  }
}

/**********************************************************************//**
  Free resources reserved for hard reqs system.
**************************************************************************/
void oblig_hard_reqs_free(void)
{
  int i;

  for (i = 0; i < ACTRES_NONE; i++) {
    obligatory_req_vector_iterate(&oblig_hard_reqs_r[i], oreq) {
      ae_contra_close(oreq->contras);
    } obligatory_req_vector_iterate_end;
    obligatory_req_vector_free(&oblig_hard_reqs_r[i]);
  }

  for (i = 0; i < ACT_SUB_RES_COUNT; i++) {
    obligatory_req_vector_iterate(&oblig_hard_reqs_sr[i], oreq) {
      ae_contra_close(oreq->contras);
    } obligatory_req_vector_iterate_end;
    obligatory_req_vector_free(&oblig_hard_reqs_sr[i]);
  }
}

/**********************************************************************//**
  Get obligatory requirements for action result.

  @param  res Action result to get requirements for
  @return     Vector of obligatory requirements
**************************************************************************/
struct obligatory_req_vector *oblig_hard_reqs_get(enum action_result res)
{
  return &(oblig_hard_reqs_r[res]);
}

/**********************************************************************//**
  Get obligatory requirements for action with sub result.

  @param  res Action sub result to get requirements for
  @return     Vector of obligatory requirements
**************************************************************************/
struct obligatory_req_vector *oblig_hard_reqs_get_sub(enum action_sub_result res)
{
  return &(oblig_hard_reqs_sr[res]);
}
