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

#include <math.h> /* ceil, floor */
#include <stdarg.h>

/* utility */
#include "astring.h"

/* common */
#include "actions.h"
#include "city.h"
#include "combat.h"
#include "fc_interface.h"
#include "game.h"
#include "map.h"
#include "movement.h"
#include "research.h"
#include "server_settings.h"
#include "tile.h"
#include "unit.h"

/* Custom data types for obligatory hard action requirements. */

/* A contradiction to an obligatory hard requirement. */
struct action_enabler_contradiction {
  /* A requirement that contradicts the obligatory hard requirement. */
  struct requirement req;

  /* Is the obligatory hard requirement in the action enabler's target
   * requirement vector? If FALSE it is in its actor requirement vector. */
  bool is_target;
};

/* One or more alternative obligatory hard requirement contradictions. */
struct ae_contra_or {
  int alternatives;
  /* The obligatory hard requirement is fulfilled if a contradiction exists
   * that doesn't contradict the action enabler. */
  struct action_enabler_contradiction *alternative;

  /* How many obligatory reqs use this */
  int users;
};

/* An obligatory hard action requirement */
struct obligatory_req {
  /* The requirement is fulfilled if the action enabler doesn't contradict
   * one of the contradictions listed here. */
  struct ae_contra_or *contras;

  /* The error message to show when the hard obligatory requirement is
   * missing. Must be there. */
  const char *error_msg;
};

#define SPECVEC_TAG obligatory_req
#define SPECVEC_TYPE struct obligatory_req
#include "specvec.h"
#define obligatory_req_vector_iterate(obreq_vec, pobreq) \
  TYPED_VECTOR_ITERATE(struct obligatory_req, obreq_vec, pobreq)
#define obligatory_req_vector_iterate_end VECTOR_ITERATE_END

/* Values used to interpret action probabilities.
 *
 * Action probabilities are sent over the network. A change in a value here
 * will therefore change the network protocol.
 *
 * A change in a value here should also update the action probability
 * format documentation in fc_types.h */
/* The lowest possible probability value (0%) */
#define ACTPROB_VAL_MIN 0
/* The highest possible probability value (100%) */
#define ACTPROB_VAL_MAX 200
/* A probability increase of 1% corresponds to this increase. */
#define ACTPROB_VAL_1_PCT (ACTPROB_VAL_MAX / 100)
/* Action probability doesn't apply when min is this. */
#define ACTPROB_VAL_NA 253
/* Action probability unsupported when min is this. */
#define ACTPROB_VAL_NOT_IMPL 254

static struct action *actions[MAX_NUM_ACTIONS];
struct action_auto_perf auto_perfs[MAX_NUM_ACTION_AUTO_PERFORMERS];
static bool actions_initialized = FALSE;

static struct action_enabler_list *action_enablers_by_action[MAX_NUM_ACTIONS];

/* Hard requirements relates to action result. */
static struct obligatory_req_vector obligatory_hard_reqs[ACTRES_NONE];

static struct astring ui_name_str = ASTRING_INIT;

static struct action *
unit_action_new(action_id id,
                enum action_result result,
                enum action_target_kind target_kind,
                enum action_sub_target_kind sub_target_kind,
                enum act_tgt_compl tgt_compl,
                bool rare_pop_up,
                bool unitwaittime_controlled,
                enum moves_actor_kind moves_actor,
                const int min_distance,
                const int max_distance,
                bool actor_consuming_always);

static bool is_enabler_active(const struct action_enabler *enabler,
			      const struct player *actor_player,
			      const struct city *actor_city,
			      const struct impr_type *actor_building,
			      const struct tile *actor_tile,
                              const struct unit *actor_unit,
			      const struct unit_type *actor_unittype,
			      const struct output_type *actor_output,
			      const struct specialist *actor_specialist,
			      const struct player *target_player,
			      const struct city *target_city,
			      const struct impr_type *target_building,
			      const struct tile *target_tile,
                              const struct unit *target_unit,
			      const struct unit_type *target_unittype,
			      const struct output_type *target_output,
			      const struct specialist *target_specialist);

static inline bool
action_prob_is_signal(const struct act_prob probability);
static inline bool
action_prob_not_relevant(const struct act_prob probability);
static inline bool
action_prob_not_impl(const struct act_prob probability);

/* Make sure that an action distance can target the whole map. */
FC_STATIC_ASSERT(MAP_DISTANCE_MAX <= ACTION_DISTANCE_LAST_NON_SIGNAL,
                 action_range_can_not_cover_the_whole_map);

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
    obligatory_req_vector_append(&obligatory_hard_reqs[res], oreq);

    /* Register the new user. */
    oreq.contras->users++;
  }
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
static void hard_code_oblig_hard_reqs(void)
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

  /* Why this is a hard requirement: there is a hard requirement that
   * the actor player is at war with the owner of any city on the
   * target tile. It can't move to the ruleset as long as Bombard and Attack
   * are targeted at unit stacks only. Having the same requirement
   * against each unit in the stack as against any city at the tile
   * ensures compatibility with any future solution that allows the
   * requirement against any city on the target tile to move to the
   * ruleset. The Freeciv code assumes that ACTRES_ATTACK has this. */
  oblig_hard_req_register(req_from_values(VUT_DIPLREL, REQ_RANGE_LOCAL,
                                          FALSE, FALSE, TRUE, DS_WAR),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " a target the actor is at war with."),
                          ACTRES_BOMBARD,
                          ACTRES_ATTACK,
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
                          ACTRES_NONE);
  oblig_hard_req_register(req_from_values(VUT_DIPLREL, REQ_RANGE_LOCAL,
                                          FALSE, TRUE, TRUE, DS_WAR),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " a domestic or allied target."),
                          ACTRES_TRANSPORT_EMBARK,
                          ACTRES_TRANSPORT_BOARD,
                          ACTRES_NONE);
  oblig_hard_req_register(req_from_values(VUT_DIPLREL, REQ_RANGE_LOCAL,
                                          FALSE, TRUE, TRUE, DS_CEASEFIRE),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " a domestic or allied target."),
                          ACTRES_TRANSPORT_EMBARK,
                          ACTRES_TRANSPORT_BOARD,
                          ACTRES_NONE);
  oblig_hard_req_register(req_from_values(VUT_DIPLREL, REQ_RANGE_LOCAL,
                                          FALSE, TRUE, TRUE, DS_PEACE),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " a domestic or allied target."),
                          ACTRES_TRANSPORT_EMBARK,
                          ACTRES_TRANSPORT_BOARD,
                          ACTRES_NONE);
  oblig_hard_req_register(req_from_values(VUT_DIPLREL, REQ_RANGE_LOCAL,
                                          FALSE, TRUE, TRUE, DS_NO_CONTACT),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " a domestic or allied target."),
                          ACTRES_TRANSPORT_EMBARK,
                          ACTRES_TRANSPORT_BOARD,
                          ACTRES_NONE);

  /* Why this is a hard requirement: Preserve semantics of the Settlers
   * unit type flag. */
  oblig_hard_req_register(req_from_values(VUT_UTFLAG, REQ_RANGE_LOCAL,
                                          FALSE, FALSE, TRUE,
                                          UTYF_SETTLERS),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " that the actor has"
                             " the Settlers utype flag."),
                          ACTRES_TRANSFORM_TERRAIN,
                          ACTRES_CULTIVATE,
                          ACTRES_PLANT,
                          ACTRES_ROAD,
                          ACTRES_BASE,
                          ACTRES_MINE,
                          ACTRES_IRRIGATE,
                          ACTRES_CLEAN_POLLUTION,
                          ACTRES_CLEAN_FALLOUT,
                          ACTRES_NONE);

  /* Why this is a hard requirement: Preserve semantics of the NoCities
   * terrain flag. */
  oblig_hard_req_register(req_from_values(VUT_TERRFLAG, REQ_RANGE_LOCAL,
                                          FALSE, TRUE, TRUE,
                                          TER_NO_CITIES),
                          TRUE,
                          N_("All action enablers for %s must require that"
                          " the target doesn't has"
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

  /* Why this is a hard requirement: Preserve semantics of NoHome
   * flag. Need to replace other uses in game engine before this can
   * be demoted to a regular unit flag. */
  oblig_hard_req_register(req_from_values(VUT_UTFLAG, REQ_RANGE_LOCAL,
                                          FALSE, TRUE, TRUE, UTYF_NOHOME),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " that the actor doesn't have"
                             " the NoHome utype flag."),
                          ACTRES_HOME_CITY, ACTRES_NONE);

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
                       req_from_values(VUT_CITYTILE, REQ_RANGE_LOCAL,
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

  /* Why this is a hard requirement: Consistency with ACTRES_ATTACK.
   * Assumed by other locations in the Freeciv code. Examples:
   * unit_move_to_tile_test() and unit_conquer_city(). */
  oblig_hard_req_register(req_from_values(VUT_DIPLREL, REQ_RANGE_LOCAL,
                                          FALSE, FALSE, TRUE, DS_WAR),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " that the actor is at war with the target."),
                          ACTRES_CONQUER_CITY,
                          ACTRES_NONE);

  /* Why this is a hard requirement: a unit must move into a city to
   * conquer it, move into a transport to embark and move out of a transport
   * to disembark from it. */
  oblig_hard_req_register(req_from_values(VUT_MINMOVES, REQ_RANGE_LOCAL,
                                          FALSE, FALSE, TRUE, 1),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " that the actor has a movement point left."),
                          ACTRES_CONQUER_CITY,
                          ACTRES_TRANSPORT_DISEMBARK,
                          ACTRES_TRANSPORT_EMBARK,
                          ACTRES_NONE);

  /* Why this is a hard requirement: this eliminates the need to
   * check if units transported by the actor unit can exist at the
   * same tile as all the units in the occupied city.
   *
   * This makes an implicit rule explicit:
   * 1. A unit must move into a city to conquer it.
   * 2. It can't move into the city if the tile contains a non allied
   *    unit (see unit_move_to_tile_test()).
   * 3. A city could, at the time this rule was made explicit, only
   *    contain units allied to its owner.
   * 3. A player could, at the time this rule was made explicit, not
   *    be allied to a player that is at war with another ally.
   * 4. A player could, at the time this rule was made explicit, only
   *    conquer a city belonging to someone he was at war with.
   * Conclusion: the conquered city had to be empty.
   */
  oblig_hard_req_register(req_from_values(VUT_MAXTILEUNITS, REQ_RANGE_LOCAL,
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
                          ACTRES_PARADROP, ACTRES_AIRLIFT, ACTRES_NONE);

  /* Why this is a hard requirement: Assumed in the code. */
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
                          ACTRES_TRANSPORT_ALIGHT, ACTRES_NONE);
  oblig_hard_req_register(req_from_values(VUT_UNITSTATE, REQ_RANGE_LOCAL,
                                          FALSE, FALSE, TRUE,
                                          USP_TRANSPORTED),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " that the actor is transported."),
                          ACTRES_TRANSPORT_ALIGHT,
                          ACTRES_TRANSPORT_DISEMBARK,
                          ACTRES_NONE);
  oblig_hard_req_register(req_from_values(VUT_UNITSTATE, REQ_RANGE_LOCAL,
                                          FALSE, FALSE, TRUE,
                                          USP_LIVABLE_TILE),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " that the actor is on a livable tile."),
                          ACTRES_TRANSPORT_ALIGHT, ACTRES_NONE);

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

  /* Why this is a hard requirement: assumed by the Freeciv code. */
  oblig_hard_req_register(req_from_values(VUT_CITYTILE, REQ_RANGE_LOCAL,
                                          FALSE, FALSE, TRUE,
                                          CITYT_CENTER),
                          FALSE,
                          N_("All action enablers for %s must require"
                             " that the actor unit is in a city."),
                          ACTRES_AIRLIFT,
                          ACTRES_NONE);
}

/**********************************************************************//**
  Hard code the obligatory hard requirements that needs access to the
  ruleset before they can be generated.
**************************************************************************/
static void hard_code_oblig_hard_reqs_ruleset(void)
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
    }
  } nations_iterate_end;
}

/**********************************************************************//**
  Hard code the actions.
**************************************************************************/
static void hard_code_actions(void)
{
  actions[ACTION_SPY_POISON] =
      unit_action_new(ACTION_SPY_POISON, ACTRES_SPY_POISON,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, TRUE);
  actions[ACTION_SPY_POISON_ESC] =
      unit_action_new(ACTION_SPY_POISON_ESC, ACTRES_SPY_POISON,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, FALSE);
  actions[ACTION_SPY_SABOTAGE_UNIT] =
      unit_action_new(ACTION_SPY_SABOTAGE_UNIT, ACTRES_SPY_SABOTAGE_UNIT,
                      ATK_UNIT, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, TRUE);
  actions[ACTION_SPY_SABOTAGE_UNIT_ESC] =
      unit_action_new(ACTION_SPY_SABOTAGE_UNIT_ESC, ACTRES_SPY_SABOTAGE_UNIT,
                      ATK_UNIT, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, FALSE);
  actions[ACTION_SPY_BRIBE_UNIT] =
      unit_action_new(ACTION_SPY_BRIBE_UNIT, ACTRES_SPY_BRIBE_UNIT,
                      ATK_UNIT, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      /* Tries a forced move if the target unit is alone at
                       * its tile and not in a city. Takes all movement if
                       * the forced move fails. */
                      MAK_FORCED,
                      0, 1, FALSE);
  actions[ACTION_SPY_SABOTAGE_CITY] =
      unit_action_new(ACTION_SPY_SABOTAGE_CITY, ACTRES_SPY_SABOTAGE_CITY,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, TRUE);
  actions[ACTION_SPY_SABOTAGE_CITY_ESC] =
      unit_action_new(ACTION_SPY_SABOTAGE_CITY_ESC, ACTRES_SPY_SABOTAGE_CITY,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, FALSE);
  actions[ACTION_SPY_TARGETED_SABOTAGE_CITY] =
      unit_action_new(ACTION_SPY_TARGETED_SABOTAGE_CITY,
                      ACTRES_SPY_TARGETED_SABOTAGE_CITY,
                      ATK_CITY, ASTK_BUILDING,
                      ACT_TGT_COMPL_MANDATORY, FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, TRUE);
  actions[ACTION_SPY_SABOTAGE_CITY_PRODUCTION] =
      unit_action_new(ACTION_SPY_SABOTAGE_CITY_PRODUCTION,
                      ACTRES_SPY_SABOTAGE_CITY_PRODUCTION,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, TRUE);
  actions[ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC] =
      unit_action_new(ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC,
                      ACTRES_SPY_TARGETED_SABOTAGE_CITY,
                      ATK_CITY, ASTK_BUILDING,
                      ACT_TGT_COMPL_MANDATORY, FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, FALSE);
  actions[ACTION_SPY_SABOTAGE_CITY_PRODUCTION_ESC] =
      unit_action_new(ACTION_SPY_SABOTAGE_CITY_PRODUCTION_ESC,
                      ACTRES_SPY_SABOTAGE_CITY_PRODUCTION,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, FALSE);
  actions[ACTION_SPY_INCITE_CITY] =
      unit_action_new(ACTION_SPY_INCITE_CITY, ACTRES_SPY_INCITE_CITY,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, TRUE);
  actions[ACTION_SPY_INCITE_CITY_ESC] =
      unit_action_new(ACTION_SPY_INCITE_CITY_ESC, ACTRES_SPY_INCITE_CITY,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, FALSE);
  actions[ACTION_ESTABLISH_EMBASSY] =
      unit_action_new(ACTION_ESTABLISH_EMBASSY,
                      ACTRES_ESTABLISH_EMBASSY,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, FALSE);
  actions[ACTION_ESTABLISH_EMBASSY_STAY] =
      unit_action_new(ACTION_ESTABLISH_EMBASSY_STAY,
                      ACTRES_ESTABLISH_EMBASSY,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, TRUE);
  actions[ACTION_SPY_STEAL_TECH] =
      unit_action_new(ACTION_SPY_STEAL_TECH,
                      ACTRES_SPY_STEAL_TECH,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, TRUE);
  actions[ACTION_SPY_STEAL_TECH_ESC] =
      unit_action_new(ACTION_SPY_STEAL_TECH_ESC,
                      ACTRES_SPY_STEAL_TECH,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, FALSE);
  actions[ACTION_SPY_TARGETED_STEAL_TECH] =
      unit_action_new(ACTION_SPY_TARGETED_STEAL_TECH,
                      ACTRES_SPY_TARGETED_STEAL_TECH,
                      ATK_CITY, ASTK_TECH,
                      ACT_TGT_COMPL_MANDATORY, FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, TRUE);
  actions[ACTION_SPY_TARGETED_STEAL_TECH_ESC] =
      unit_action_new(ACTION_SPY_TARGETED_STEAL_TECH_ESC,
                      ACTRES_SPY_TARGETED_STEAL_TECH,
                      ATK_CITY, ASTK_TECH,
                      ACT_TGT_COMPL_MANDATORY, FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, FALSE);
  actions[ACTION_SPY_INVESTIGATE_CITY] =
      unit_action_new(ACTION_SPY_INVESTIGATE_CITY,
                      ACTRES_SPY_INVESTIGATE_CITY,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, FALSE);
  actions[ACTION_INV_CITY_SPEND] =
      unit_action_new(ACTION_INV_CITY_SPEND,
                      ACTRES_SPY_INVESTIGATE_CITY,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, TRUE);
  actions[ACTION_SPY_STEAL_GOLD] =
      unit_action_new(ACTION_SPY_STEAL_GOLD,
                      ACTRES_SPY_STEAL_GOLD,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, TRUE);
  actions[ACTION_SPY_STEAL_GOLD_ESC] =
      unit_action_new(ACTION_SPY_STEAL_GOLD_ESC,
                      ACTRES_SPY_STEAL_GOLD,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, FALSE);
  actions[ACTION_SPY_SPREAD_PLAGUE] =
      unit_action_new(ACTION_SPY_SPREAD_PLAGUE,
                      ACTRES_SPY_SPREAD_PLAGUE,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, FALSE);
  actions[ACTION_TRADE_ROUTE] =
      unit_action_new(ACTION_TRADE_ROUTE, ACTRES_TRADE_ROUTE,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_STAYS, 0, 1, TRUE);
  actions[ACTION_MARKETPLACE] =
      unit_action_new(ACTION_MARKETPLACE, ACTRES_MARKETPLACE,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_STAYS, 0, 1, TRUE);
  actions[ACTION_HELP_WONDER] =
      unit_action_new(ACTION_HELP_WONDER, ACTRES_HELP_WONDER,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_STAYS, 0, 1, TRUE);
  actions[ACTION_CAPTURE_UNITS] =
      unit_action_new(ACTION_CAPTURE_UNITS, ACTRES_CAPTURE_UNITS,
                      ATK_UNITS, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE, MAK_STAYS,
                      /* A single domestic unit at the target tile will make
                       * the action illegal. It must therefore be performed
                       * from another tile. */
                      1, 1,
                      FALSE);
  actions[ACTION_FOUND_CITY] =
      unit_action_new(ACTION_FOUND_CITY, ACTRES_FOUND_CITY,
                      ATK_TILE, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, TRUE, TRUE, MAK_STAYS,
                      /* Illegal to perform to a target on another tile.
                       * Reason: The Freeciv code assumes that the city
                       * founding unit is located at the tile were the new
                       * city is founded. */
                      0, 0,
                      TRUE);
  actions[ACTION_JOIN_CITY] =
      unit_action_new(ACTION_JOIN_CITY, ACTRES_JOIN_CITY,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, TRUE, TRUE,
                      MAK_STAYS, 0, 1, TRUE);
  actions[ACTION_STEAL_MAPS] =
      unit_action_new(ACTION_STEAL_MAPS, ACTRES_STEAL_MAPS,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, TRUE);
  actions[ACTION_STEAL_MAPS_ESC] =
      unit_action_new(ACTION_STEAL_MAPS_ESC, ACTRES_STEAL_MAPS,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, FALSE);
  actions[ACTION_BOMBARD] =
      unit_action_new(ACTION_BOMBARD, ACTRES_BOMBARD,
                      /* FIXME: Target is actually Units + City */
                      ATK_UNITS, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE, MAK_STAYS,
                      /* A single domestic unit at the target tile will make
                       * the action illegal. It must therefore be performed
                       * from another tile. */
                      1,
                      /* Overwritten by the ruleset's bombard_max_range */
                      1,
                      FALSE);
  actions[ACTION_BOMBARD2] =
      unit_action_new(ACTION_BOMBARD2, ACTRES_BOMBARD,
                      /* FIXME: Target is actually Units + City */
                      ATK_UNITS, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_STAYS,
                      /* A single domestic unit at the target tile will make
                       * the action illegal. It must therefore be performed
                       * from another tile. */
                      1,
                      /* Overwritten by the ruleset's bombard_2_max_range */
                      1,
                      FALSE);
  actions[ACTION_BOMBARD3] =
      unit_action_new(ACTION_BOMBARD3, ACTRES_BOMBARD,
                      /* FIXME: Target is actually Units + City */
                      ATK_UNITS, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE, MAK_STAYS,
                      /* A single domestic unit at the target tile will make
                       * the action illegal. It must therefore be performed
                       * from another tile. */
                      1,
                      /* Overwritten by the ruleset's bombard_3_max_range */
                      1,
                      FALSE);
  actions[ACTION_SPY_NUKE] =
      unit_action_new(ACTION_SPY_NUKE, ACTRES_SPY_NUKE, ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, TRUE);
  actions[ACTION_SPY_NUKE_ESC] =
      unit_action_new(ACTION_SPY_NUKE_ESC, ACTRES_SPY_NUKE,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, FALSE);
  actions[ACTION_NUKE] =
      unit_action_new(ACTION_NUKE, ACTRES_NUKE,
                      ATK_TILE, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE,  TRUE, TRUE,
                      MAK_STAYS, 0,
                      /* Overwritten by the ruleset's
                       * explode_nuclear_max_range */
                      0,
                      TRUE);
  actions[ACTION_NUKE_CITY] =
      unit_action_new(ACTION_NUKE_CITY, ACTRES_NUKE_CITY,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE,  TRUE, TRUE,
                      MAK_STAYS, 1, 1, TRUE);
  actions[ACTION_NUKE_UNITS] =
      unit_action_new(ACTION_NUKE_UNITS, ACTRES_NUKE_UNITS,
                      ATK_UNITS, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE,  TRUE, TRUE,
                      MAK_STAYS, 1, 1, TRUE);
  actions[ACTION_DESTROY_CITY] =
      unit_action_new(ACTION_DESTROY_CITY, ACTRES_DESTROY_CITY,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE,  TRUE, TRUE,
                      MAK_STAYS, 0, 1, FALSE);
  actions[ACTION_EXPEL_UNIT] =
      unit_action_new(ACTION_EXPEL_UNIT, ACTRES_EXPEL_UNIT,
                      ATK_UNIT, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_STAYS, 0, 1, FALSE);
  actions[ACTION_RECYCLE_UNIT] =
      unit_action_new(ACTION_RECYCLE_UNIT, ACTRES_RECYCLE_UNIT,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, TRUE, TRUE, MAK_STAYS,
                      /* Illegal to perform to a target on another tile to
                       * keep the rules exactly as they were for now. */
                      0, 1,
                      TRUE);
  actions[ACTION_DISBAND_UNIT] =
      unit_action_new(ACTION_DISBAND_UNIT,
                      /* Can't be ACTRES_NONE because
                       * action_success_actor_consume() sets unit lost
                       * reason based on action result. */
                      ACTRES_DISBAND_UNIT,
                      ATK_SELF, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, TRUE, TRUE,
                      MAK_STAYS, 0, 0, TRUE);
  actions[ACTION_HOME_CITY] =
      unit_action_new(ACTION_HOME_CITY, ACTRES_HOME_CITY,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, TRUE, FALSE,
                      /* Illegal to perform to a target on another tile to
                       * keep the rules exactly as they were for now. */
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_UPGRADE_UNIT] =
      unit_action_new(ACTION_UPGRADE_UNIT, ACTRES_UPGRADE_UNIT,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, TRUE, TRUE, MAK_STAYS,
                      /* Illegal to perform to a target on another tile to
                       * keep the rules exactly as they were for now. */
                      0, 0,
                      FALSE);
  actions[ACTION_PARADROP] =
      unit_action_new(ACTION_PARADROP, ACTRES_PARADROP,
                      ATK_TILE, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, TRUE, TRUE,
                      MAK_TELEPORT,
                      1,
                      /* Still limited by each unit type's
                       * paratroopers_range field. */
                      ACTION_DISTANCE_MAX,
                      FALSE);
  actions[ACTION_AIRLIFT] =
      unit_action_new(ACTION_AIRLIFT, ACTRES_AIRLIFT,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, TRUE, TRUE,
                      MAK_TELEPORT,
                      1,
                      /* Overwritten by the ruleset's airlift_max_range. */
                      ACTION_DISTANCE_UNLIMITED,
                      FALSE);
  actions[ACTION_ATTACK] =
      unit_action_new(ACTION_ATTACK, ACTRES_ATTACK,
                      /* FIXME: Target is actually City, each unit at the
                       * target tile (Units) and, depending on the
                       * unreachable_protects setting, each or any
                       * *non transported* unit at the target tile. */
                      ATK_UNITS, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      /* Tries a forced move if the target unit's tile has
                       * no non allied units and the occupychance dice roll
                       * tells it to move. */
                      MAK_FORCED,
                      1, 1, FALSE);
  actions[ACTION_SUICIDE_ATTACK] =
      unit_action_new(ACTION_SUICIDE_ATTACK, ACTRES_ATTACK,
                      /* FIXME: Target is actually City, each unit at the
                       * target tile (Units) and, depending on the
                       * unreachable_protects setting, each or any
                       * *non transported* unit at the target tile. */
                      ATK_UNITS, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_FORCED, 1, 1, TRUE);
  actions[ACTION_STRIKE_BUILDING] =
      unit_action_new(ACTION_STRIKE_BUILDING, ACTRES_STRIKE_BUILDING,
                      ATK_CITY, ASTK_BUILDING,
                      ACT_TGT_COMPL_MANDATORY, FALSE, FALSE,
                      MAK_STAYS, 1, 1, FALSE);
  actions[ACTION_STRIKE_PRODUCTION] =
      unit_action_new(ACTION_STRIKE_PRODUCTION, ACTRES_STRIKE_PRODUCTION,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, FALSE,
                      MAK_STAYS, 1, 1, FALSE);
  actions[ACTION_CONQUER_CITY] =
      unit_action_new(ACTION_CONQUER_CITY, ACTRES_CONQUER_CITY,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_CONQUER_CITY2] =
      unit_action_new(ACTION_CONQUER_CITY2, ACTRES_CONQUER_CITY,
                      ATK_CITY, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_HEAL_UNIT] =
      unit_action_new(ACTION_HEAL_UNIT, ACTRES_HEAL_UNIT,
                      ATK_UNIT, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_STAYS, 0, 1, FALSE);
  actions[ACTION_TRANSFORM_TERRAIN] =
      unit_action_new(ACTION_TRANSFORM_TERRAIN, ACTRES_TRANSFORM_TERRAIN,
                      ATK_TILE, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_CULTIVATE] =
      unit_action_new(ACTION_CULTIVATE, ACTRES_CULTIVATE,
                      ATK_TILE, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_PLANT] =
      unit_action_new(ACTION_PLANT, ACTRES_PLANT, ATK_TILE, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_PILLAGE] =
      unit_action_new(ACTION_PILLAGE, ACTRES_PILLAGE,
                      ATK_TILE, ASTK_EXTRA,
                      ACT_TGT_COMPL_FLEXIBLE, TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_CLEAN_POLLUTION] =
      unit_action_new(ACTION_CLEAN_POLLUTION, ACTRES_CLEAN_POLLUTION,
                      ATK_TILE, ASTK_EXTRA,
                      ACT_TGT_COMPL_FLEXIBLE, TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_CLEAN_FALLOUT] =
      unit_action_new(ACTION_CLEAN_FALLOUT, ACTRES_CLEAN_FALLOUT,
                      ATK_TILE, ASTK_EXTRA,
                      ACT_TGT_COMPL_FLEXIBLE, TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_FORTIFY] =
      unit_action_new(ACTION_FORTIFY, ACTRES_FORTIFY,
                      ATK_SELF, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_SENTRY] =
      unit_action_new(ACTION_SENTRY, ACTRES_SENTRY,
                      ATK_SELF, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_ROAD] =
      unit_action_new(ACTION_ROAD, ACTRES_ROAD,
                      ATK_TILE, ASTK_EXTRA_NOT_THERE,
                      ACT_TGT_COMPL_MANDATORY, TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_CONVERT] =
      unit_action_new(ACTION_CONVERT, ACTRES_CONVERT,
                      ATK_SELF, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_BASE] =
      unit_action_new(ACTION_BASE, ACTRES_BASE,
                      ATK_TILE, ASTK_EXTRA_NOT_THERE,
                      ACT_TGT_COMPL_MANDATORY, TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_MINE] =
      unit_action_new(ACTION_MINE, ACTRES_MINE,
                      ATK_TILE, ASTK_EXTRA_NOT_THERE,
                      ACT_TGT_COMPL_MANDATORY, TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_IRRIGATE] =
      unit_action_new(ACTION_IRRIGATE, ACTRES_IRRIGATE,
                      ATK_TILE, ASTK_EXTRA_NOT_THERE,
                      ACT_TGT_COMPL_MANDATORY, TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_TRANSPORT_ALIGHT] =
      unit_action_new(ACTION_TRANSPORT_ALIGHT, ACTRES_TRANSPORT_ALIGHT,
                      ATK_UNIT, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_TRANSPORT_BOARD] =
      unit_action_new(ACTION_TRANSPORT_BOARD, ACTRES_TRANSPORT_BOARD,
                      ATK_UNIT, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_TRANSPORT_UNLOAD] =
      unit_action_new(ACTION_TRANSPORT_UNLOAD, ACTRES_TRANSPORT_UNLOAD,
                      ATK_UNIT, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_TRANSPORT_DISEMBARK1] =
      unit_action_new(ACTION_TRANSPORT_DISEMBARK1,
                      ACTRES_TRANSPORT_DISEMBARK,
                      ATK_TILE, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_TRANSPORT_DISEMBARK2] =
      unit_action_new(ACTION_TRANSPORT_DISEMBARK2,
                      ACTRES_TRANSPORT_DISEMBARK,
                      ATK_TILE, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_TRANSPORT_EMBARK] =
      unit_action_new(ACTION_TRANSPORT_EMBARK, ACTRES_TRANSPORT_EMBARK,
                      ATK_UNIT, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_SPY_ATTACK] =
      unit_action_new(ACTION_SPY_ATTACK, ACTRES_SPY_ATTACK,
                      ATK_UNITS, ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_STAYS, 1, 1, FALSE);
  actions[ACTION_USER_ACTION1] =
      unit_action_new(ACTION_USER_ACTION1, ACTRES_NONE,
                      /* Overwritten by the ruleset */
                      ATK_CITY,
                      ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_UNREPRESENTABLE,
                      /* Overwritten by the ruleset */
                      0, 1, FALSE);
  actions[ACTION_USER_ACTION2] =
      unit_action_new(ACTION_USER_ACTION2, ACTRES_NONE,
                      /* Overwritten by the ruleset */
                      ATK_CITY,
                      ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_UNREPRESENTABLE,
                      /* Overwritten by the ruleset */
                      0, 1, FALSE);
  actions[ACTION_USER_ACTION3] =
      unit_action_new(ACTION_USER_ACTION3, ACTRES_NONE,
                      /* Overwritten by the ruleset */
                      ATK_CITY,
                      ASTK_NONE,
                      ACT_TGT_COMPL_SIMPLE, FALSE, TRUE,
                      MAK_UNREPRESENTABLE,
                      /* Overwritten by the ruleset */
                      0, 1, FALSE);
}

/**********************************************************************//**
  Initialize the actions and the action enablers.
**************************************************************************/
void actions_init(void)
{
  int i, j;

  /* Hard code the actions */
  hard_code_actions();

  /* Initialize the action enabler list */
  action_iterate(act) {
    action_enablers_by_action[act] = action_enabler_list_new();
  } action_iterate_end;

  /* Initialize action obligatory hard requirements. */

  /* Obligatory hard requirements are sorted by action result in memory.
   * This makes it easy to access the data. */
  for (i = 0; i < ACTRES_NONE; i++) {
    /* Prepare each action result's storage area. */
    obligatory_req_vector_init(&obligatory_hard_reqs[i]);
  }

  /* Obligatory hard requirements are sorted by requirement in the source
   * code. This makes it easy to read, modify and explain it. */
  hard_code_oblig_hard_reqs();

  /* Initialize the action auto performers. */
  for (i = 0; i < MAX_NUM_ACTION_AUTO_PERFORMERS; i++) {
    /* Nothing here. Nothing after this point. */
    auto_perfs[i].cause = AAPC_COUNT;

    /* The criteria to pick *this* auto performer for its cause. */
    requirement_vector_init(&auto_perfs[i].reqs);

    for (j = 0; j < MAX_NUM_ACTIONS; j++) {
      /* Nothing here. Nothing after this point. */
      auto_perfs[i].alternatives[j] = ACTION_NONE;
    }
  }

  /* The actions them self are now initialized. */
  actions_initialized = TRUE;
}

/**********************************************************************//**
  Generate action related data based on the currently loaded ruleset. Done
  before ruleset sanity checking and ruleset compatibility post
  processing.
**************************************************************************/
void actions_rs_pre_san_gen(void)
{
  /* Some obligatory hard requirements needs access to the rest of the
   * ruleset. */
  hard_code_oblig_hard_reqs_ruleset();
}

/**********************************************************************//**
  Free the actions and the action enablers.
**************************************************************************/
void actions_free(void)
{
  int i;

  /* Don't consider the actions to be initialized any longer. */
  actions_initialized = FALSE;

  action_iterate(act) {
    action_enabler_list_iterate(action_enablers_by_action[act], enabler) {
      action_enabler_free(enabler);
    } action_enabler_list_iterate_end;

    action_enabler_list_destroy(action_enablers_by_action[act]);

    FC_FREE(actions[act]);
  } action_iterate_end;

  /* Free the obligatory hard action requirements. */
  for (i = 0; i < ACTRES_NONE; i++) {
    obligatory_req_vector_iterate(&obligatory_hard_reqs[i], oreq) {
      ae_contra_close(oreq->contras);
    } obligatory_req_vector_iterate_end;
    obligatory_req_vector_free(&obligatory_hard_reqs[i]);
  }

  /* Free the action auto performers. */
  for (i = 0; i < MAX_NUM_ACTION_AUTO_PERFORMERS; i++) {
    requirement_vector_free(&auto_perfs[i].reqs);
  }

  astr_free(&ui_name_str);
}

/**********************************************************************//**
  Returns TRUE iff the actions are initialized.

  Doesn't care about action enablers.
**************************************************************************/
bool actions_are_ready(void)
{
  if (!actions_initialized) {
    /* The actions them self aren't initialized yet. */
    return FALSE;
  }

  action_iterate(act) {
    if (actions[act]->ui_name[0] == '\0') {
      /* An action without a UI name exists means that the ruleset haven't
       * loaded yet. The ruleset loading will assign a default name to
       * any actions not named by the ruleset. The client will get this
       * name from the server. */
      return FALSE;
    }
  } action_iterate_end;

  /* The actions should be ready for use. */
  return TRUE;
}

/**********************************************************************//**
  Create a new action.
**************************************************************************/
static struct action *action_new(action_id id,
                                 enum action_result result,
                                 enum action_target_kind target_kind,
                                 enum action_sub_target_kind sub_target_kind,
                                 enum act_tgt_compl tgt_compl,
                                 const int min_distance,
                                 const int max_distance,
                                 bool actor_consuming_always)
{
  struct action *action;

  action = fc_malloc(sizeof(*action));

  action->id = id;

  action->result = result;

  action->actor_kind = AAK_UNIT;
  action->target_kind = target_kind;
  action->sub_target_kind = sub_target_kind;

  /* ASTK_NONE implies ACT_TGT_COMPL_SIMPLE and
   * !ASTK_NONE implies !ACT_TGT_COMPL_SIMPLE */
  fc_assert_msg(((sub_target_kind == ASTK_NONE
                  && tgt_compl == ACT_TGT_COMPL_SIMPLE)
                 || (sub_target_kind != ASTK_NONE
                     && tgt_compl != ACT_TGT_COMPL_SIMPLE)),
                "%s contradicts itself regarding sub targets.",
                action_rule_name(action));

  action->target_complexity = tgt_compl;

  /* The distance between the actor and itself is always 0. */
  fc_assert(target_kind != ATK_SELF
            || (min_distance == 0 && max_distance == 0));

  action->min_distance = min_distance;
  action->max_distance = max_distance;

  action->actor_consuming_always = actor_consuming_always;

  /* Loaded from the ruleset. Until generalized actions are ready it has to
   * be defined seperatly from other action data. */
  action->ui_name[0] = '\0';
  action->quiet = FALSE;
  BV_CLR_ALL(action->blocked_by);

  return action;
}

/**********************************************************************//**
  Create a new action performed by a unit actor.
**************************************************************************/
static struct action *
unit_action_new(action_id id,
                enum action_result result,
                enum action_target_kind target_kind,
                enum action_sub_target_kind sub_target_kind,
                enum act_tgt_compl tgt_compl,
                bool rare_pop_up,
                bool unitwaittime_controlled,
                enum moves_actor_kind moves_actor,
                const int min_distance,
                const int max_distance,
                bool actor_consuming_always)
{
  struct action *act = action_new(id, result,
                                  target_kind, sub_target_kind,
                                  tgt_compl,
                                  min_distance, max_distance,
                                  actor_consuming_always);

  act->actor.is_unit.rare_pop_up = rare_pop_up;

  act->actor.is_unit.unitwaittime_controlled = unitwaittime_controlled;

  act->actor.is_unit.moves_actor = moves_actor;

  return act;
}

/**********************************************************************//**
  Returns TRUE iff the specified action ID refers to a valid action.
**************************************************************************/
bool action_id_exists(const action_id act_id)
{
  /* Actions are still hard coded. */
  return gen_action_is_valid(act_id) && actions[act_id];
}

/**********************************************************************//**
  Return the action with the given id.

  Returns NULL if no action with the given id exists.
**************************************************************************/
struct action *action_by_number(action_id act_id)
{
  if (!action_id_exists(act_id)) {
    /* Nothing to return. */

    log_verbose("Asked for non existing action numbered %d", act_id);

    return NULL;
  }

  fc_assert_msg(actions[act_id], "Action %d don't exist.", act_id);

  return actions[act_id];
}

/**********************************************************************//**
  Return the action with the given name.

  Returns NULL if no action with the given name exists.
**************************************************************************/
struct action *action_by_rule_name(const char *name)
{
  /* Actions are still hard coded in the gen_action enum. */
  action_id act_id = gen_action_by_name(name, fc_strcasecmp);

  if (!action_id_exists(act_id)) {
    /* Nothing to return. */

    log_verbose("Asked for non existing action named %s", name);

    return NULL;
  }

  return action_by_number(act_id);
}

/**********************************************************************//**
  Get the actor kind of an action.
**************************************************************************/
enum action_actor_kind action_get_actor_kind(const struct action *paction)
{
  fc_assert_ret_val_msg(paction, AAK_COUNT, "Action doesn't exist.");

  return paction->actor_kind;
}

/**********************************************************************//**
  Get the target kind of an action.
**************************************************************************/
enum action_target_kind action_get_target_kind(
    const struct action *paction)
{
  fc_assert_ret_val_msg(paction, ATK_COUNT, "Action doesn't exist.");

  return paction->target_kind;
}

/**********************************************************************//**
  Get the sub target kind of an action.
**************************************************************************/
enum action_sub_target_kind action_get_sub_target_kind(
    const struct action *paction)
{
  fc_assert_ret_val_msg(paction, ASTK_COUNT, "Action doesn't exist.");

  return paction->sub_target_kind;
}

/**********************************************************************//**
  Get the battle kind that can prevent an action.
**************************************************************************/
enum action_battle_kind action_get_battle_kind(const struct action *pact)
{
  switch (pact->result) {
  case ACTRES_ATTACK:
    return ABK_STANDARD;
  case ACTRES_SPY_ATTACK:
  case ACTRES_SPY_POISON:
  case ACTRES_SPY_STEAL_GOLD:
  case ACTRES_SPY_SABOTAGE_CITY:
  case ACTRES_SPY_TARGETED_SABOTAGE_CITY:
  case ACTRES_SPY_SABOTAGE_CITY_PRODUCTION:
  case ACTRES_SPY_STEAL_TECH:
  case ACTRES_SPY_TARGETED_STEAL_TECH:
  case ACTRES_SPY_INCITE_CITY:
  case ACTRES_SPY_BRIBE_UNIT:
  case ACTRES_SPY_SABOTAGE_UNIT:
  case ACTRES_STEAL_MAPS:
  case ACTRES_SPY_NUKE:
  case ACTRES_SPY_SPREAD_PLAGUE:
    return ABK_DIPLOMATIC;
  default:
    return ABK_NONE;
  }
}

/**********************************************************************//**
  Returns TRUE iff performing the specified action has the specified
  result.
**************************************************************************/
bool action_has_result(const struct action *paction,
                       enum action_result result)
{
  fc_assert_ret_val(paction, FALSE);
  fc_assert_ret_val(action_result_is_valid(result), FALSE);

  return paction->result == result;
}

/**********************************************************************//**
  Returns TRUE iff the specified action allows the player to provide
  details in addition to actor and target. Returns FALSE if the action
  doesn't support any additional details.
**************************************************************************/
bool action_has_complex_target(const struct action *paction)
{
  fc_assert_ret_val(paction != NULL, FALSE);

  return paction->target_complexity >= ACT_TGT_COMPL_FLEXIBLE;
}

/**********************************************************************//**
  Returns TRUE iff the specified action REQUIRES the player to provide
  details in addition to actor and target. Returns FALSE if the action
  doesn't support any additional details or if they can be set by Freeciv
  it self.
**************************************************************************/
bool action_requires_details(const struct action *paction)
{
  fc_assert_ret_val(paction != NULL, FALSE);

  return paction->target_complexity >= ACT_TGT_COMPL_MANDATORY;
}

/**********************************************************************//**
  Returns TRUE iff a unit's ability to perform this action will pop up the
  action selection dialog before the player asks for it only in exceptional
  cases.

  An example of an exceptional case is when the player tries to move a
  unit to a tile it can't move to but can perform this action to.
**************************************************************************/
bool action_id_is_rare_pop_up(action_id act_id)
{
  fc_assert_ret_val_msg((action_id_exists(act_id)),
                        FALSE, "Action %d don't exist.", act_id);
  fc_assert_ret_val(action_id_get_actor_kind(act_id) == AAK_UNIT, FALSE);

  return actions[act_id]->actor.is_unit.rare_pop_up;
}

/**********************************************************************//**
  Returns TRUE iff the specified distance between actor and target is
  sm,aller or equal to the max range accepted by the specified action.
**************************************************************************/
bool action_distance_inside_max(const struct action *action,
                                const int distance)
{
  return (distance <= action->max_distance
          || action->max_distance == ACTION_DISTANCE_UNLIMITED);
}

/**********************************************************************//**
  Returns TRUE iff the specified distance between actor and target is
  within the range acceptable to the specified action.
**************************************************************************/
bool action_distance_accepted(const struct action *action,
                              const int distance)
{
  fc_assert_ret_val(action, FALSE);

  return (distance >= action->min_distance
          && action_distance_inside_max(action, distance));
}

/**********************************************************************//**
  Returns TRUE iff blocked will be illegal if blocker is legal.
**************************************************************************/
bool action_would_be_blocked_by(const struct action *blocked,
                                const struct action *blocker)
{
  fc_assert_ret_val(blocked, FALSE);
  fc_assert_ret_val(blocker, FALSE);

  return BV_ISSET(blocked->blocked_by, action_number(blocker));
}

/**********************************************************************//**
  Get the universal number of the action.
**************************************************************************/
int action_number(const struct action *action)
{
  return action->id;
}

/**********************************************************************//**
  Get the rule name of the action.
**************************************************************************/
const char *action_rule_name(const struct action *action)
{
  /* Rule name is still hard coded. */
  return action_id_rule_name(action->id);
}

/**********************************************************************//**
  Get the action name used when displaying the action in the UI. Nothing
  is added to the UI name.
**************************************************************************/
const char *action_name_translation(const struct action *action)
{
  /* Use action_id_name_translation() to format the UI name. */
  return action_id_name_translation(action->id);
}

/**********************************************************************//**
  Get the rule name of the action.
**************************************************************************/
const char *action_id_rule_name(action_id act_id)
{
  fc_assert_msg(actions[act_id], "Action %d don't exist.", act_id);

  return gen_action_name(act_id);
}

/**********************************************************************//**
  Get the action name used when displaying the action in the UI. Nothing
  is added to the UI name.
**************************************************************************/
const char *action_id_name_translation(action_id act_id)
{
  return action_prepare_ui_name(act_id, "", ACTPROB_NA, NULL);
}

/**********************************************************************//**
  Get the action name with a mnemonic ready to display in the UI.
**************************************************************************/
const char *action_get_ui_name_mnemonic(action_id act_id,
                                        const char *mnemonic)
{
  return action_prepare_ui_name(act_id, mnemonic, ACTPROB_NA, NULL);
}

/**********************************************************************//**
  Returns a text representation of the action probability prob unless it
  is a signal. Returns NULL if prob is a signal.

  The returned string is in statically allocated astring, and thus this
  function is not thread-safe.
**************************************************************************/
static const char *action_prob_to_text(const struct act_prob prob)
{
  static struct astring chance = ASTRING_INIT;

  /* How to interpret action probabilities like prob is documented in
   * fc_types.h */
  if (action_prob_is_signal(prob)) {
    fc_assert(action_prob_not_impl(prob)
              || action_prob_not_relevant(prob));

    /* Unknown because of missing server support or should not exits. */
    return NULL;
  }

  if (prob.min == prob.max) {
    /* Only one probability in range. */

    /* TRANS: the probability that an action will succeed. Given in
     * percentage. Resolution is 0.5%. */
    astr_set(&chance, _("%.1f%%"), (double)prob.max / ACTPROB_VAL_1_PCT);
  } else {
    /* TRANS: the interval (end points included) where the probability of
     * the action's success is. Given in percentage. Resolution is 0.5%. */
    astr_set(&chance, _("[%.1f%%, %.1f%%]"),
             (double)prob.min / ACTPROB_VAL_1_PCT,
             (double)prob.max / ACTPROB_VAL_1_PCT);
  }

  return astr_str(&chance);
}

/**********************************************************************//**
  Get the UI name ready to show the action in the UI. It is possible to
  add a client specific mnemonic; it is assumed that if the mnemonic
  appears in the action name it can be escaped by doubling.
  Success probability information is interpreted and added to the text.
  A custom text can be inserted before the probability information.

  The returned string is in statically allocated astring, and thus this
  function is not thread-safe.
**************************************************************************/
const char *action_prepare_ui_name(action_id act_id, const char *mnemonic,
                                   const struct act_prob prob,
                                   const char *custom)
{
  struct astring chance = ASTRING_INIT;

  /* Text representation of the probability. */
  const char *probtxt;

  if (!actions_are_ready()) {
    /* Could be a client who haven't gotten the ruleset yet */

    /* so there shouldn't be any action probability to show */
    fc_assert(action_prob_not_relevant(prob));

    /* but the action should be valid */
    fc_assert_ret_val_msg(action_id_exists(act_id),
                          "Invalid action",
                          "Invalid action %d", act_id);

    /* and no custom text will be inserted */
    fc_assert(custom == NULL || custom[0] == '\0');

    /* Make the best of what is known */
    astr_set(&ui_name_str, _("%s%s (name may be wrong)"),
             mnemonic, action_id_rule_name(act_id));

    /* Return the guess. */
    return astr_str(&ui_name_str);
  }

  probtxt = action_prob_to_text(prob);

  /* Format the info part of the action's UI name. */
  if (probtxt != NULL && custom != NULL) {
    /* TRANS: action UI name's info part with custom info and probability.
     * Hint: you can move the paren handling from this sting to the action
     * names if you need to add extra information (like a mnemonic letter
     * that doesn't appear in the action UI name) to it. In that case you
     * must do so for all strings with this comment and for every action
     * name. To avoid a `()` when no UI name info part is added you have
     * to add the extra information to every action name or remove the
     * surrounding parens. */
    astr_set(&chance, _(" (%s; %s)"), custom, probtxt);
  } else if (probtxt != NULL) {
    /* TRANS: action UI name's info part with probability.
     * Hint: you can move the paren handling from this sting to the action
     * names if you need to add extra information (like a mnemonic letter
     * that doesn't appear in the action UI name) to it. In that case you
     * must do so for all strings with this comment and for every action
     * name. To avoid a `()` when no UI name info part is added you have
     * to add the extra information to every action name or remove the
     * surrounding parens. */
    astr_set(&chance, _(" (%s)"), probtxt);
  } else if (custom != NULL) {
    /* TRANS: action UI name's info part with custom info.
     * Hint: you can move the paren handling from this sting to the action
     * names if you need to add extra information (like a mnemonic letter
     * that doesn't appear in the action UI name) to it. In that case you
     * must do so for all strings with this comment and for every action
     * name. To avoid a `()` when no UI name info part is added you have
     * to add the extra information to every action name or remove the
     * surrounding parens. */
    astr_set(&chance, _(" (%s)"), custom);
  } else {
    /* No info part to display. */
    astr_clear(&chance);
  }

  fc_assert_msg(actions[act_id], "Action %d don't exist.", act_id);

  /* Escape any instances of the mnemonic in the action's UI format string.
   * (Assumes any mnemonic can be escaped by doubling, and that they are
   * unlikely to appear in a format specifier. True for clients seen so
   * far: Gtk's _ and Qt's &) */
  {
    struct astring fmtstr = ASTRING_INIT;
    const char *ui_name = _(actions[act_id]->ui_name);

    if (mnemonic[0] != '\0') {
      const char *hit;

      fc_assert(!strchr(mnemonic, '%'));
      while ((hit = strstr(ui_name, mnemonic))) {
        astr_add(&fmtstr, "%.*s%s%s", (int)(hit - ui_name), ui_name,
                 mnemonic, mnemonic);
        ui_name = hit + strlen(mnemonic);
      }
    }
    astr_add(&fmtstr, "%s", ui_name);

    /* Use the modified format string */
    astr_set(&ui_name_str, astr_str(&fmtstr), mnemonic,
             astr_str(&chance));

    astr_free(&fmtstr);
  }

  astr_free(&chance);

  return astr_str(&ui_name_str);
}

/**********************************************************************//**
  Explain an action probability in a way suitable for a tool tip for the
  button that starts it.
  @return an explanation of what an action probability means

  The returned string is in statically allocated astring, and thus this
  function is not thread-safe.
**************************************************************************/
const char *action_prob_explain(const struct act_prob prob)
{
  static struct astring tool_tip = ASTRING_INIT;

  if (action_prob_is_signal(prob)) {
    fc_assert(action_prob_not_impl(prob));

    /* Missing server support. No in game action will change this. */
    astr_clear(&tool_tip);
  } else if (prob.min == prob.max) {
    /* TRANS: action probability of success. Given in percentage.
     * Resolution is 0.5%. */
    astr_set(&tool_tip, _("The probability of success is %.1f%%."),
             (double)prob.max / ACTPROB_VAL_1_PCT);
  } else {
    astr_set(&tool_tip,
             /* TRANS: action probability interval (min to max). Given in
              * percentage. Resolution is 0.5%. The string at the end is
              * shown when the interval is wide enough to not be caused by
              * rounding. It explains that the interval is imprecise because
              * the player doesn't have enough information. */
             _("The probability of success is %.1f%%, %.1f%% or somewhere"
               " in between.%s"),
             (double)prob.min / ACTPROB_VAL_1_PCT,
             (double)prob.max / ACTPROB_VAL_1_PCT,
             prob.max - prob.min > 1 ?
               /* TRANS: explanation used in the action probability tooltip
                * above. Preserve leading space. */
               _(" (This is the most precise interval I can calculate "
                 "given the information our nation has access to.)") :
               "");
  }

  return astr_str(&tool_tip);
}

/**********************************************************************//**
  Get the unit type role corresponding to the ability to do the specified
  action.
**************************************************************************/
int action_get_role(const struct action *paction)
{
  fc_assert_msg(AAK_UNIT == action_get_actor_kind(paction),
                "Action %s isn't performed by a unit",
                action_rule_name(paction));

  return paction->id + L_LAST;
}

/**********************************************************************//**
  Returns the unit activity this action may cause or ACTIVITY_LAST if the
  action doesn't result in a unit activity.
**************************************************************************/
enum unit_activity action_get_activity(const struct action *paction)
{
  fc_assert_msg(AAK_UNIT == action_get_actor_kind(paction),
                "Action %s isn't performed by a unit",
                action_rule_name(paction));

  if (action_has_result(paction, ACTRES_FORTIFY)) {
    return ACTIVITY_FORTIFYING;
  } else if (action_has_result(paction, ACTRES_SENTRY)) {
    return ACTIVITY_SENTRY;
  } else if (action_has_result(paction, ACTRES_BASE)) {
    return ACTIVITY_BASE;
  } else if (action_has_result(paction, ACTRES_ROAD)) {
    return ACTIVITY_GEN_ROAD;
  } else if (action_has_result(paction, ACTRES_PILLAGE)) {
    return ACTIVITY_PILLAGE;
  } else if (action_has_result(paction, ACTRES_CLEAN_POLLUTION)) {
    return ACTIVITY_POLLUTION;
  } else if (action_has_result(paction, ACTRES_CLEAN_FALLOUT)) {
    return ACTIVITY_FALLOUT;
  } else if (action_has_result(paction, ACTRES_TRANSFORM_TERRAIN)) {
    return ACTIVITY_TRANSFORM;
  } else if (action_has_result(paction, ACTRES_CONVERT)) {
    return ACTIVITY_CONVERT;
  } else if (action_has_result(paction, ACTRES_PLANT)) {
    return ACTIVITY_PLANT;
  } else if (action_has_result(paction, ACTRES_MINE)) {
    return ACTIVITY_MINE;
  } else if (action_has_result(paction, ACTRES_CULTIVATE)) {
    return ACTIVITY_CULTIVATE;
  } else if (action_has_result(paction, ACTRES_IRRIGATE)) {
    return ACTIVITY_IRRIGATE;
  } else {
    return ACTIVITY_LAST;
  }
}

/**********************************************************************//**
  Returns the unit activity time (work) this action takes (requires) or
  ACT_TIME_INSTANTANEOUS if the action happens at once.

  See update_unit_activity() and tile_activity_time()
**************************************************************************/
int action_get_act_time(const struct action *paction,
                        const struct unit *actor_unit,
                        const struct tile *tgt_tile,
                        const struct extra_type *tgt_extra)
{
  enum unit_activity pactivity = action_get_activity(paction);

  if (pactivity == ACTIVITY_LAST) {
    /* Happens instantaneously, not at turn change. */
    return ACT_TIME_INSTANTANEOUS;
  }

  switch (pactivity) {
  case ACTIVITY_PILLAGE:
  case ACTIVITY_POLLUTION:
  case ACTIVITY_FALLOUT:
  case ACTIVITY_BASE:
  case ACTIVITY_GEN_ROAD:
  case ACTIVITY_IRRIGATE:
  case ACTIVITY_MINE:
  case ACTIVITY_CULTIVATE:
  case ACTIVITY_PLANT:
  case ACTIVITY_TRANSFORM:
    return tile_activity_time(pactivity, tgt_tile, tgt_extra);
  case ACTIVITY_FORTIFYING:
    return 1;
  case ACTIVITY_CONVERT:
    return unit_type_get(actor_unit)->convert_time * ACTIVITY_FACTOR;
  case ACTIVITY_SENTRY:
    /* Happens instantaneously, not at turn change. */
    return ACT_TIME_INSTANTANEOUS;
  case ACTIVITY_EXPLORE:
  case ACTIVITY_IDLE:
  case ACTIVITY_FORTIFIED:
  case ACTIVITY_GOTO:
  case ACTIVITY_UNKNOWN:
  case ACTIVITY_PATROL_UNUSED:
  case ACTIVITY_LAST:
  case ACTIVITY_OLD_ROAD:
  case ACTIVITY_OLD_RAILROAD:
  case ACTIVITY_FORTRESS:
  case ACTIVITY_AIRBASE:
    /* Should not happen. Caught by the assertion below. */
    break;
  }

  fc_assert(FALSE);
  return ACT_TIME_INSTANTANEOUS;
}

/**********************************************************************//**
  Returns TRUE iff the specified action can create the specified extra.
**************************************************************************/
bool action_creates_extra(const struct action *paction,
                          const struct extra_type *pextra)
{
  fc_assert(paction != NULL);
  if (pextra == NULL) {
    return FALSE;
  }

  if (!pextra->buildable) {
    return FALSE;
  }

  switch (paction->result) {
  case ACTRES_ROAD:
    return is_extra_caused_by(pextra, EC_ROAD);
  case ACTRES_BASE:
    return is_extra_caused_by(pextra, EC_BASE);
  case ACTRES_MINE:
    return is_extra_caused_by(pextra, EC_MINE);
  case ACTRES_IRRIGATE:
    return is_extra_caused_by(pextra, EC_IRRIGATION);
  case ACTRES_ESTABLISH_EMBASSY:
  case ACTRES_SPY_INVESTIGATE_CITY:
  case ACTRES_SPY_POISON:
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
  case ACTRES_CAPTURE_UNITS:
  case ACTRES_FOUND_CITY:
  case ACTRES_JOIN_CITY:
  case ACTRES_STEAL_MAPS:
  case ACTRES_BOMBARD:
  case ACTRES_SPY_NUKE:
  case ACTRES_NUKE:
  case ACTRES_NUKE_CITY:
  case ACTRES_NUKE_UNITS:
  case ACTRES_DESTROY_CITY:
  case ACTRES_EXPEL_UNIT:
  case ACTRES_RECYCLE_UNIT:
  case ACTRES_DISBAND_UNIT:
  case ACTRES_HOME_CITY:
  case ACTRES_UPGRADE_UNIT:
  case ACTRES_PARADROP:
  case ACTRES_AIRLIFT:
  case ACTRES_STRIKE_BUILDING:
  case ACTRES_STRIKE_PRODUCTION:
  case ACTRES_ATTACK:
  case ACTRES_CONQUER_CITY:
  case ACTRES_HEAL_UNIT:
  case ACTRES_TRANSFORM_TERRAIN:
  case ACTRES_CULTIVATE:
  case ACTRES_PLANT:
  case ACTRES_PILLAGE:
  case ACTRES_CLEAN_POLLUTION:
  case ACTRES_CLEAN_FALLOUT:
  case ACTRES_FORTIFY:
  case ACTRES_SENTRY:
  case ACTRES_CONVERT:
  case ACTRES_TRANSPORT_ALIGHT:
  case ACTRES_TRANSPORT_UNLOAD:
  case ACTRES_TRANSPORT_DISEMBARK:
  case ACTRES_TRANSPORT_BOARD:
  case ACTRES_TRANSPORT_EMBARK:
  case ACTRES_SPY_ATTACK:
  case ACTRES_SPY_SPREAD_PLAGUE:
  case ACTRES_NONE:
    break;
  }

  return FALSE;
}

/**********************************************************************//**
  Returns TRUE iff the specified action can remove the specified extra.
**************************************************************************/
bool action_removes_extra(const struct action *paction,
                          const struct extra_type *pextra)
{
  fc_assert(paction != NULL);
  if (pextra == NULL) {
    return FALSE;
  }

  switch (paction->result) {
  case ACTRES_PILLAGE:
    return is_extra_removed_by(pextra, ERM_PILLAGE);
  case ACTRES_CLEAN_POLLUTION:
    return is_extra_removed_by(pextra, ERM_CLEANPOLLUTION);
  case ACTRES_CLEAN_FALLOUT:
    return is_extra_removed_by(pextra, ERM_CLEANFALLOUT);
  case ACTRES_ESTABLISH_EMBASSY:
  case ACTRES_SPY_INVESTIGATE_CITY:
  case ACTRES_SPY_POISON:
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
  case ACTRES_CAPTURE_UNITS:
  case ACTRES_FOUND_CITY:
  case ACTRES_JOIN_CITY:
  case ACTRES_STEAL_MAPS:
  case ACTRES_BOMBARD:
  case ACTRES_SPY_NUKE:
  case ACTRES_NUKE:
  case ACTRES_NUKE_CITY:
  case ACTRES_NUKE_UNITS:
  case ACTRES_DESTROY_CITY:
  case ACTRES_EXPEL_UNIT:
  case ACTRES_RECYCLE_UNIT:
  case ACTRES_DISBAND_UNIT:
  case ACTRES_HOME_CITY:
  case ACTRES_UPGRADE_UNIT:
  case ACTRES_PARADROP:
  case ACTRES_AIRLIFT:
  case ACTRES_STRIKE_BUILDING:
  case ACTRES_STRIKE_PRODUCTION:
  case ACTRES_ATTACK:
  case ACTRES_CONQUER_CITY:
  case ACTRES_HEAL_UNIT:
  case ACTRES_TRANSFORM_TERRAIN:
  case ACTRES_CULTIVATE:
  case ACTRES_PLANT:
  case ACTRES_FORTIFY:
  case ACTRES_SENTRY:
  case ACTRES_ROAD:
  case ACTRES_CONVERT:
  case ACTRES_BASE:
  case ACTRES_MINE:
  case ACTRES_IRRIGATE:
  case ACTRES_TRANSPORT_ALIGHT:
  case ACTRES_TRANSPORT_UNLOAD:
  case ACTRES_TRANSPORT_DISEMBARK:
  case ACTRES_TRANSPORT_BOARD:
  case ACTRES_TRANSPORT_EMBARK:
  case ACTRES_SPY_ATTACK:
  case ACTRES_SPY_SPREAD_PLAGUE:
  case ACTRES_NONE:
    break;
  }

  return FALSE;
}

/**********************************************************************//**
  Create a new action enabler.
**************************************************************************/
struct action_enabler *action_enabler_new(void)
{
  struct action_enabler *enabler;

  enabler = fc_malloc(sizeof(*enabler));
  enabler->disabled = FALSE;
  requirement_vector_init(&enabler->actor_reqs);
  requirement_vector_init(&enabler->target_reqs);

  /* Make sure that action doesn't end up as a random value that happens to
   * be a valid action id. */
  enabler->action = ACTION_NONE;

  return enabler;
}

/**********************************************************************//**
  Free resources allocated for the action enabler.
**************************************************************************/
void action_enabler_free(struct action_enabler *enabler)
{
  requirement_vector_free(&enabler->actor_reqs);
  requirement_vector_free(&enabler->target_reqs);

  free(enabler);
}

/**********************************************************************//**
  Create a new copy of an existing action enabler.
**************************************************************************/
struct action_enabler *
action_enabler_copy(const struct action_enabler *original)
{
  struct action_enabler *enabler = action_enabler_new();

  enabler->action = original->action;

  requirement_vector_copy(&enabler->actor_reqs, &original->actor_reqs);
  requirement_vector_copy(&enabler->target_reqs, &original->target_reqs);

  return enabler;
}

/**********************************************************************//**
  Add an action enabler to the current ruleset.
**************************************************************************/
void action_enabler_add(struct action_enabler *enabler)
{
  /* Sanity check: a non existing action enabler can't be added. */
  fc_assert_ret(enabler);
  /* Sanity check: a non existing action doesn't have enablers. */
  fc_assert_ret(action_id_exists(enabler->action));

  action_enabler_list_append(
        action_enablers_for_action(enabler->action),
        enabler);
}

/**********************************************************************//**
  Remove an action enabler from the current ruleset.

  Returns TRUE on success.
**************************************************************************/
bool action_enabler_remove(struct action_enabler *enabler)
{
  /* Sanity check: a non existing action enabler can't be removed. */
  fc_assert_ret_val(enabler, FALSE);
  /* Sanity check: a non existing action doesn't have enablers. */
  fc_assert_ret_val(action_id_exists(enabler->action), FALSE);

  return action_enabler_list_remove(
        action_enablers_for_action(enabler->action),
        enabler);
}

/**********************************************************************//**
  Get all enablers for an action in the current ruleset.
**************************************************************************/
struct action_enabler_list *
action_enablers_for_action(action_id action)
{
  /* Sanity check: a non existing action doesn't have enablers. */
  fc_assert_ret_val(action_id_exists(action), NULL);

  return action_enablers_by_action[action];
}

/**********************************************************************//**
  Returns a suggestion to add an obligatory hard requirement to an action
  enabler or NULL if no hard obligatory reqs were missing. It is the
  responsibility of the caller to free the suggestion when it is done with
  it.
  @param enabler the action enabler to suggest a fix for.
  @return a problem with fix suggestions or NULL if no obligatory hard
          requirement problems were detected.
**************************************************************************/
struct req_vec_problem *
action_enabler_suggest_repair_oblig(const struct action_enabler *enabler)
{
  struct action *paction;

  /* Sanity check: a non existing action enabler is missing but it doesn't
   * miss any obligatory hard requirements. */
  fc_assert_ret_val(enabler, FALSE);

  /* Sanity check: a non existing action doesn't have any obligatory hard
   * requirements. */
  fc_assert_ret_val(action_id_exists(enabler->action), FALSE);
  paction = action_by_number(enabler->action);

  if (paction->result == ACTRES_NONE) {
    /* No hard coded results means no obiligatory requirements. */
    return NULL;
  }

  obligatory_req_vector_iterate(&obligatory_hard_reqs[paction->result],
      obreq) {
    struct req_vec_problem *out;
    int i;
    bool fulfilled = FALSE;

    /* Check each alternative. */
    for (i = 0; i < obreq->contras->alternatives; i++) {
      const struct requirement_vector *ae_vec;

      /* Select action enabler requirement vector. */
      ae_vec = (obreq->contras->alternative[i].is_target
                ? &enabler->target_reqs
                : &enabler->actor_reqs);

      if (does_req_contradicts_reqs(&obreq->contras->alternative[i].req,
                                    ae_vec)) {
        /* It is enough that one alternative accepts the enabler. */
        fulfilled = TRUE;
        break;
      }

      /* Fall back to the next alternative */
    }

    if (fulfilled) {
      /* This obligatory hard requirement isn't a problem. */
      continue;
    }

    /* Missing hard obligatory requirement detected */

    out = req_vec_problem_new(obreq->contras->alternatives,
                              obreq->error_msg,
                              action_rule_name(paction));

    for (i = 0; i < obreq->contras->alternatives; i++) {
      const struct requirement_vector *ae_vec;

      /* Select action enabler requirement vector. */
      ae_vec = (obreq->contras->alternative[i].is_target
                ? &enabler->target_reqs
                : &enabler->actor_reqs);

      /* The suggested fix is to append a requirement that makes the enabler
       * contradict the missing hard obligatory requirement detector. */
      out->suggested_solutions[i].operation = RVCO_APPEND;
      out->suggested_solutions[i].vector_number
          = action_enabler_vector_number(enabler, ae_vec);

      /* Change the requirement from what should conflict to what is
       * wanted. */
      out->suggested_solutions[i].req.present
          = !obreq->contras->alternative[i].req.present;
      out->suggested_solutions[i].req.source
          = obreq->contras->alternative[i].req.source;
      out->suggested_solutions[i].req.range
          = obreq->contras->alternative[i].req.range;
      out->suggested_solutions[i].req.survives
          = obreq->contras->alternative[i].req.survives;
      out->suggested_solutions[i].req.quiet
          = obreq->contras->alternative[i].req.quiet;
    }

    /* Return the first problem found. The next problem will be detected
     * during the next call. */
    return out;
  } obligatory_req_vector_iterate_end;

  /* No obligatory req problems found. */
  return NULL;
}

/**********************************************************************//**
  Returns the first local DiplRel requirement in the specified requirement
  vector or NULL if it doesn't have a local DiplRel requirement.
  @param vec the requirement vector to look in
  @return the first local DiplRel requirement.
**************************************************************************/
static struct requirement *
req_vec_first_local_diplrel(const struct requirement_vector *vec)
{
  requirement_vector_iterate(vec, preq) {
    if (preq->source.kind == VUT_DIPLREL
        && preq->range == REQ_RANGE_LOCAL) {
      return preq;
    }
  } requirement_vector_iterate_end;

  return NULL;
}

/**********************************************************************//**
  Returns the first requirement in the specified requirement vector that
  contradicts the specified requirement or NULL if no contradiction was
  detected.
  @param req the requirement that may contradict the vector
  @param vec the requirement vector to look in
  @return the first local DiplRel requirement.
**************************************************************************/
static struct requirement *
req_vec_first_contradiction_in_vec(const struct requirement *req,
                                   const struct requirement_vector *vec)
{
  /* If the requirement is contradicted by any requirement in the vector it
   * contradicts the entire requirement vector. */
  requirement_vector_iterate(vec, preq) {
    if (are_requirements_contradictions(req, preq)) {
      return preq;
    }
  } requirement_vector_iterate_end;

  /* Not a singe requirement in the requirement vector is contradicted be
   * the specified requirement. */
  return NULL;
}

/**********************************************************************//**
  Detects a local DiplRel requirement in a tile targeted action without
  an explicit claimed requirement in the target reqs.
  @param enabler the enabler to look at
  @return the problem or NULL if no problem was found
**************************************************************************/
static struct req_vec_problem *
enabler_tile_tgt_local_diplrel_implies_claimed(
    const struct action_enabler *enabler)
{
  struct req_vec_problem *out;
  struct requirement *local_diplrel;
  struct requirement *claimed_req;
  struct requirement tile_is_claimed;
  struct requirement tile_is_unclaimed;

  struct action *paction = action_by_number(enabler->action);

  if (action_get_target_kind(paction) != ATK_TILE) {
    /* Not tile targeted */
    return NULL;
  }

  local_diplrel = req_vec_first_local_diplrel(&enabler->actor_reqs);
  if (local_diplrel == NULL) {
    /* No local diplrel */
    return NULL;
  }

  /* Tile is unclaimed as a requirement. */
  tile_is_unclaimed.range = REQ_RANGE_LOCAL;
  tile_is_unclaimed.survives = FALSE;
  tile_is_unclaimed.source.kind = VUT_CITYTILE;
  tile_is_unclaimed.present = FALSE;
  tile_is_unclaimed.source.value.citytile = CITYT_CLAIMED;

  claimed_req = req_vec_first_contradiction_in_vec(&tile_is_unclaimed,
                                                   &enabler->target_reqs);

  if (claimed_req) {
    /* Already clear */
    return NULL;
  }

  /* Tile is claimed as a requirement. */
  tile_is_claimed.range = REQ_RANGE_LOCAL;
  tile_is_claimed.survives = FALSE;
  tile_is_claimed.source.kind = VUT_CITYTILE;
  tile_is_claimed.present = TRUE;
  tile_is_claimed.source.value.citytile = CITYT_CLAIMED;

  out = req_vec_problem_new(
          1,
          /* TRANS: ruledit shows this when an enabler for a tile targeted
           * action requires that the actor has a diplomatic relationship to
           * the target but doesn't require that the target tile is claimed.
           * (DiplRel requirements to an unclaimed tile are never fulfilled
           * so this is implicit.) */
          N_("Requirement {%s} implies a claimed "
             "tile. No diplomatic relation to Nature."),
          req_to_fstring(local_diplrel));

  /* The solution is to add the requirement that the tile is claimed */
  out->suggested_solutions[0].req = tile_is_claimed;
  out->suggested_solutions[0].vector_number
      = action_enabler_vector_number(enabler, &enabler->target_reqs);
  out->suggested_solutions[0].operation = RVCO_APPEND;

  return out;
}

/**********************************************************************//**
  Returns the first action enabler specific contradiction in the specified
  enabler or NULL if no enabler specific contradiction is found.
  @param enabler the enabler to look at
  @return the first problem and maybe a suggested fix
**************************************************************************/
static struct req_vec_problem *
enabler_first_self_contradiction(const struct action_enabler *enabler)
{
  struct req_vec_problem *out;
  struct requirement *local_diplrel;
  struct requirement *unclaimed_req;
  struct requirement tile_is_claimed;

  struct action *paction = action_by_number(enabler->action);

  if (action_get_target_kind(paction) != ATK_TILE) {
    /* Not tile targeted */
    return NULL;
  }

  local_diplrel = req_vec_first_local_diplrel(&enabler->actor_reqs);
  if (local_diplrel == NULL) {
    /* No local diplrel */
    return NULL;
  }

  /* Tile is claimed as a requirement. */
  tile_is_claimed.range = REQ_RANGE_LOCAL;
  tile_is_claimed.survives = FALSE;
  tile_is_claimed.source.kind = VUT_CITYTILE;
  tile_is_claimed.present = TRUE;
  tile_is_claimed.source.value.citytile = CITYT_CLAIMED;

  unclaimed_req = req_vec_first_contradiction_in_vec(&tile_is_claimed,
                                                     &enabler->target_reqs);

  if (unclaimed_req == NULL) {
    /* No unclaimed req */
    return NULL;
  }

  out = req_vec_problem_new(
          2,
          /* TRANS: ruledit shows this when an enabler for a tile targeted
           * action requires that the target tile is unclaimed and that the
           * actor has a diplomatic relationship to the target. (DiplRel
           * requirements to an unclaimed tile are never fulfilled.) */
          N_("No diplomatic relation to Nature."
             " Requirements {%s} and {%s} contradict each other."),
          req_to_fstring(local_diplrel), req_to_fstring(unclaimed_req));

  /* The first suggestion is to remove the diplrel */
  out->suggested_solutions[0].req = *local_diplrel;
  out->suggested_solutions[0].vector_number
      = action_enabler_vector_number(enabler, &enabler->actor_reqs);
  out->suggested_solutions[0].operation = RVCO_REMOVE;

  /* The 2nd is to remove the requirement that the tile is unclaimed */
  out->suggested_solutions[1].req = *unclaimed_req;
  out->suggested_solutions[1].vector_number
      = action_enabler_vector_number(enabler, &enabler->target_reqs);
  out->suggested_solutions[1].operation = RVCO_REMOVE;

  return out;
}

/**********************************************************************//**
  Returns a suggestion to fix the specified action enabler or NULL if no
  fix is found to be needed. It is the responsibility of the caller to
  free the suggestion when it is done with it.
**************************************************************************/
struct req_vec_problem *
action_enabler_suggest_repair(const struct action_enabler *enabler)
{
  struct req_vec_problem *out;

  out = action_enabler_suggest_repair_oblig(enabler);
  if (out != NULL) {
    return out;
  }

  out = req_vec_get_first_contradiction(&enabler->actor_reqs,
                                        action_enabler_vector_number,
                                        enabler);
  if (out != NULL) {
    return out;
  }

  out = req_vec_get_first_contradiction(&enabler->target_reqs,
                                        action_enabler_vector_number,
                                        enabler);
  if (out != NULL) {
    return out;
  }

  /* Enabler specific contradictions. */
  out = enabler_first_self_contradiction(enabler);
  if (out != NULL) {
    return out;
  }

  /* Needed in action not enabled explanation finding. */
  out = enabler_tile_tgt_local_diplrel_implies_claimed(enabler);
  if (out != NULL) {
    return out;
  }

  /* No problems found. */
  return NULL;
}

/**********************************************************************//**
  Returns the first action enabler specific clarification possibility in
  the specified enabler or NULL if no enabler specific contradiction is
  found.
  @param enabler the enabler to look at
  @return the first problem and maybe a suggested fix
**************************************************************************/
static struct req_vec_problem *
enabler_first_clarification(const struct action_enabler *enabler)
{
  struct req_vec_problem *out;

  out = NULL;

  return out;
}

/**********************************************************************//**
  Returns a suggestion to improve the specified action enabler or NULL if
  nothing to improve is found to be needed. It is the responsibility of the
  caller to free the suggestion when it is done with it. A possible
  improvement isn't always an error.
  @param enabler the enabler to improve
  @return a suggestion to improve the specified action enabler
**************************************************************************/
struct req_vec_problem *
action_enabler_suggest_improvement(const struct action_enabler *enabler)
{
  struct action *paction;
  struct req_vec_problem *out;

  out = action_enabler_suggest_repair(enabler);
  if (out) {
    /* A bug, not just a potential improvement */
    return out;
  }

  paction = action_by_number(enabler->action);

  /* Detect unused action enablers. */
  if (action_get_actor_kind(paction) == AAK_UNIT) {
    bool has_user = FALSE;

    unit_type_iterate(pactor) {
      if (action_actor_utype_hard_reqs_ok(paction->result, pactor)
          && requirement_fulfilled_by_unit_type(pactor,
                                                &(enabler->actor_reqs))) {
        has_user = TRUE;
        break;
      }
    } unit_type_iterate_end;

    if (!has_user) {
      /* TRANS: ruledit warns a user about an unused action enabler */
      out = req_vec_problem_new(0, N_("This action enabler is never used"
                                      " by any unit."));
    }
  }
  if (out != NULL) {
    return out;
  }

  out = enabler_first_clarification(enabler);

  return out;
}

/**********************************************************************//**
  Returns the requirement vector number of the specified requirement
  vector in the specified action enabler.
  @param enabler the action enabler that may own the vector.
  @param vec the requirement vector to number.
  @return the requirement vector number the vector has in this enabler.
**************************************************************************/
req_vec_num_in_item
action_enabler_vector_number(const void *enabler,
                             const struct requirement_vector *vec)
{
  struct action_enabler *ae = (struct action_enabler *)enabler;

  if (vec == &ae->actor_reqs) {
    return 0;
  } else if (vec == &ae->target_reqs) {
    return 1;
  } else {
    return -1;
  }
}

/********************************************************************//**
  Returns a writable pointer to the specified requirement vector in the
  action enabler or NULL if the action enabler doesn't have a requirement
  vector with that requirement vector number.
  @param enabler the action enabler that may own the vector.
  @param number the item's requirement vector number.
  @return a pointer to the specified requirement vector.
************************************************************************/
struct requirement_vector *
action_enabler_vector_by_number(const void *enabler,
                                req_vec_num_in_item number)
{
  struct action_enabler *ae = (struct action_enabler *)enabler;

  fc_assert_ret_val(number >= 0, NULL);

  switch (number) {
  case 0:
    return &ae->actor_reqs;
  case 1:
    return &ae->target_reqs;
  default:
    return NULL;
  }
}

/*********************************************************************//**
  Returns the name of the given requirement vector number n in an action
  enabler or NULL if enablers don't have a requirement vector with that
  number.
  @param vec the requirement vector to name
  @return the requirement vector name or NULL.
**************************************************************************/
const char *action_enabler_vector_by_number_name(req_vec_num_in_item vec)
{
  switch (vec) {
  case 0:
    /* TRANS: requirement vector in an action enabler (ruledit) */
    return _("actor_reqs");
  case 1:
    /* TRANS: requirement vector in an action enabler (ruledit) */
    return _("target_reqs");
  default:
    return NULL;
  }
}

/**********************************************************************//**
  Returns TRUE iff the specified player knows (has seen) the specified
  tile.
**************************************************************************/
static bool plr_knows_tile(const struct player *plr,
                           const struct tile *ttile)
{
  return plr && ttile && (tile_get_known(ttile, plr) != TILE_UNKNOWN);
}

/**********************************************************************//**
  Returns TRUE iff the specified player can see the specified tile.
**************************************************************************/
static bool plr_sees_tile(const struct player *plr,
                          const struct tile *ttile)
{
  return plr && ttile && (tile_get_known(ttile, plr) == TILE_KNOWN_SEEN);
}

/**********************************************************************//**
  Returns the local building type of a city target.

  target_city can't be NULL
**************************************************************************/
static const struct impr_type *
tgt_city_local_building(const struct city *target_city)
{
  /* Only used with city targets */
  fc_assert_ret_val(target_city, NULL);

  if (target_city->production.kind == VUT_IMPROVEMENT) {
    /* The local building is what the target city currently is building. */
    return target_city->production.value.building;
  } else {
    /* In the current semantic the local building is always the building
     * being built. */
    /* TODO: Consider making the local building the target building for
     * actions that allows specifying a building target. */
    return NULL;
  }
}

/**********************************************************************//**
  Returns the local unit type of a city target.

  target_city can't be NULL
**************************************************************************/
static const struct unit_type *
tgt_city_local_utype(const struct city *target_city)
{
  /* Only used with city targets */
  fc_assert_ret_val(target_city, NULL);

  if (target_city->production.kind == VUT_UTYPE) {
    /* The local unit type is what the target city currently is
     * building. */
    return target_city->production.value.utype;
  } else {
    /* In the current semantic the local utype is always the type of the
     * unit being built. */
    return NULL;
  }
}

/**********************************************************************//**
  Returns the target tile for actions that may block the specified action.
  This is needed because some actions can be blocked by an action with a
  different target kind. The target tile could therefore be missing.

  Example: The ATK_SELF action ACTION_DISBAND_UNIT can be blocked by the
  ATK_CITY action ACTION_RECYCLE_UNIT.
**************************************************************************/
static const struct tile *
blocked_find_target_tile(const action_id act_id,
                         const struct unit *actor_unit,
                         const struct tile *target_tile_arg,
                         const struct city *target_city,
                         const struct unit *target_unit)
{
  if (target_tile_arg != NULL) {
    /* Trust the caller. */
    return target_tile_arg;
  }

  switch (action_id_get_target_kind(act_id)) {
  case ATK_CITY:
    fc_assert_ret_val(target_city, NULL);
    return city_tile(target_city);
  case ATK_UNIT:
    fc_assert_ret_val(target_unit, NULL);
    return unit_tile(target_unit);
  case ATK_UNITS:
    fc_assert_ret_val(target_unit || target_tile_arg, NULL);
    if (target_unit) {
      return unit_tile(target_unit);
    }
    /* Fall through. */
  case ATK_TILE:
    fc_assert_ret_val(target_tile_arg, NULL);
    return target_tile_arg;
  case ATK_SELF:
    fc_assert_ret_val(actor_unit, NULL);
    return unit_tile(actor_unit);
  case ATK_COUNT:
    /* Handled below. */
    break;
  }

  fc_assert_msg(FALSE, "Bad action target kind %d for action %d",
                action_id_get_target_kind(act_id), act_id);
  return NULL;
}

/**********************************************************************//**
  Returns the target city for actions that may block the specified action.
  This is needed because some actions can be blocked by an action with a
  different target kind. The target city argument could therefore be
  missing.

  Example: The ATK_SELF action ACTION_DISBAND_UNIT can be blocked by the
  ATK_CITY action ACTION_RECYCLE_UNIT.
**************************************************************************/
static const struct city *
blocked_find_target_city(const action_id act_id,
                         const struct unit *actor_unit,
                         const struct tile *target_tile,
                         const struct city *target_city_arg,
                         const struct unit *target_unit)
{
  if (target_city_arg != NULL) {
    /* Trust the caller. */
    return target_city_arg;
  }

  switch (action_id_get_target_kind(act_id)) {
  case ATK_CITY:
    fc_assert_ret_val(target_city_arg, NULL);
    return target_city_arg;
  case ATK_UNIT:
    fc_assert_ret_val(target_unit, NULL);
    fc_assert_ret_val(unit_tile(target_unit), NULL);
    return tile_city(unit_tile(target_unit));
  case ATK_UNITS:
    fc_assert_ret_val(target_unit || target_tile, NULL);
    if (target_unit) {
      fc_assert_ret_val(unit_tile(target_unit), NULL);
      return tile_city(unit_tile(target_unit));
    }
    /* Fall through. */
  case ATK_TILE:
    fc_assert_ret_val(target_tile, NULL);
    return tile_city(target_tile);
  case ATK_SELF:
    fc_assert_ret_val(actor_unit, NULL);
    fc_assert_ret_val(unit_tile(actor_unit), NULL);
    return tile_city(unit_tile(actor_unit));
  case ATK_COUNT:
    /* Handled below. */
    break;
  }

  fc_assert_msg(FALSE, "Bad action target kind %d for action %d",
                action_id_get_target_kind(act_id), act_id);
  return NULL;
}

/**********************************************************************//**
  Returns the action that blocks the specified action or NULL if the
  specified action isn't blocked.

  An action that can block another blocks when it is forced and possible.
**************************************************************************/
struct action *action_is_blocked_by(const action_id act_id,
                                    const struct unit *actor_unit,
                                    const struct tile *target_tile_arg,
                                    const struct city *target_city_arg,
                                    const struct unit *target_unit)
{


  const struct tile *target_tile
      = blocked_find_target_tile(act_id, actor_unit, target_tile_arg,
                                 target_city_arg, target_unit);
  const struct city *target_city
      = blocked_find_target_city(act_id, actor_unit, target_tile,
                                 target_city_arg, target_unit);

  action_iterate(blocker_id) {
    fc_assert_action(action_id_get_actor_kind(blocker_id) == AAK_UNIT,
                     continue);

    if (!action_id_would_be_blocked_by(act_id, blocker_id)) {
      /* It doesn't matter if it is legal. It won't block the action. */
      continue;
    }

    switch (action_id_get_target_kind(blocker_id)) {
    case ATK_CITY:
      if (!target_city) {
        /* Can't be enabled. No target. */
        continue;
      }
      if (is_action_enabled_unit_on_city(blocker_id,
                                         actor_unit, target_city)) {
        return action_by_number(blocker_id);
      }
      break;
    case ATK_UNIT:
      if (!target_unit) {
        /* Can't be enabled. No target. */
        continue;
      }
      if (is_action_enabled_unit_on_unit(blocker_id,
                                         actor_unit, target_unit)) {
        return action_by_number(blocker_id);
      }
      break;
    case ATK_UNITS:
      if (!target_tile) {
        /* Can't be enabled. No target. */
        continue;
      }
      if (is_action_enabled_unit_on_units(blocker_id,
                                          actor_unit, target_tile)) {
        return action_by_number(blocker_id);
      }
      break;
    case ATK_TILE:
      if (!target_tile) {
        /* Can't be enabled. No target. */
        continue;
      }
      if (is_action_enabled_unit_on_tile(blocker_id,
                                         actor_unit, target_tile, NULL)) {
        return action_by_number(blocker_id);
      }
      break;
    case ATK_SELF:
      if (is_action_enabled_unit_on_self(blocker_id, actor_unit)) {
        return action_by_number(blocker_id);
      }
      break;
    case ATK_COUNT:
      fc_assert_action(action_id_get_target_kind(blocker_id) != ATK_COUNT,
                       continue);
      break;
    }
  } action_iterate_end;

  /* Not blocked. */
  return NULL;
}

/**********************************************************************//**
  Returns TRUE if the specified unit type can perform an action with the
  wanted result given that an action enabler later will enable it.

  This is done by checking the action result's hard requirements. Hard
  requirements must be TRUE before an action can be done. The reason why
  is usually that code dealing with the action assumes that the
  requirements are true. A requirement may also end up here if it can't be
  expressed in a requirement vector or if its absence makes the action
  pointless.

  When adding a new hard requirement here:
   * explain why it is a hard requirement in a comment.

  @param result the action result to check the hard reqs for
  @param actor_unittype the unit type that may be able to act
  @param ignore_third_party ignore if potential targets etc exists
  @return TRUE iff the specified unit type can perform the wanted action
          given that an action enabler later will enable it.
**************************************************************************/
static bool
action_actor_utype_hard_reqs_ok_full(enum action_result result,
                                     const struct unit_type *actor_unittype,
                                     bool ignore_third_party)
{
  switch (result) {
  case ACTRES_JOIN_CITY:
    if (actor_unittype->pop_cost <= 0) {
      /* Reason: Must have population to add. */
      return FALSE;
    }
    break;

  case ACTRES_BOMBARD:
    if (actor_unittype->bombard_rate <= 0) {
      /* Reason: Can't bombard if it never fires. */
      return FALSE;
    }

    if (actor_unittype->attack_strength <= 0) {
      /* Reason: Can't bombard without attack strength. */
      return FALSE;
    }

    break;

  case ACTRES_UPGRADE_UNIT:
    if (actor_unittype->obsoleted_by == U_NOT_OBSOLETED) {
      /* Reason: Nothing to upgrade to. */
      return FALSE;
    }
    break;

  case ACTRES_ATTACK:
    if (actor_unittype->attack_strength <= 0) {
      /* Reason: Can't attack without strength. */
      return FALSE;
    }
    break;

  case ACTRES_CONVERT:
    if (!actor_unittype->converted_to) {
      /* Reason: must be able to convert to something. */
      return FALSE;
    }
    break;

  case ACTRES_TRANSPORT_UNLOAD:
    if (actor_unittype->transport_capacity < 1) {
      /* Reason: can't transport anything to unload. */
      return FALSE;
    }
    break;

  case ACTRES_TRANSPORT_BOARD:
  case ACTRES_TRANSPORT_EMBARK:
  case ACTRES_TRANSPORT_ALIGHT:
  case ACTRES_TRANSPORT_DISEMBARK:
    if (!ignore_third_party) {
      bool has_transporter = FALSE;

      unit_type_iterate(utrans) {
        if (can_unit_type_transport(utrans, utype_class(actor_unittype))) {
          has_transporter = TRUE;
          break;
        }
      } unit_type_iterate_end;

      if (!has_transporter) {
        /* Reason: no other unit can transport this unit. */
        return FALSE;
      }
    }
    break;

  case ACTRES_ESTABLISH_EMBASSY:
  case ACTRES_SPY_INVESTIGATE_CITY:
  case ACTRES_SPY_POISON:
  case ACTRES_SPY_STEAL_GOLD:
  case ACTRES_SPY_SPREAD_PLAGUE:
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
  case ACTRES_CAPTURE_UNITS:
  case ACTRES_FOUND_CITY:
  case ACTRES_STEAL_MAPS:
  case ACTRES_SPY_NUKE:
  case ACTRES_NUKE:
  case ACTRES_NUKE_CITY:
  case ACTRES_NUKE_UNITS:
  case ACTRES_DESTROY_CITY:
  case ACTRES_EXPEL_UNIT:
  case ACTRES_RECYCLE_UNIT:
  case ACTRES_DISBAND_UNIT:
  case ACTRES_HOME_CITY:
  case ACTRES_PARADROP:
  case ACTRES_AIRLIFT:
  case ACTRES_STRIKE_BUILDING:
  case ACTRES_STRIKE_PRODUCTION:
  case ACTRES_CONQUER_CITY:
  case ACTRES_HEAL_UNIT:
  case ACTRES_PILLAGE:
  case ACTRES_CLEAN_POLLUTION:
  case ACTRES_CLEAN_FALLOUT:
  case ACTRES_FORTIFY:
  case ACTRES_SENTRY:
  case ACTRES_TRANSFORM_TERRAIN:
  case ACTRES_CULTIVATE:
  case ACTRES_PLANT:
  case ACTRES_ROAD:
  case ACTRES_BASE:
  case ACTRES_MINE:
  case ACTRES_IRRIGATE:
  case ACTRES_SPY_ATTACK:
  case ACTRES_NONE:
    /* No hard unit type requirements. */
    break;
  }

  return TRUE;
}

/**********************************************************************//**
  Returns TRUE if the specified unit type can perform an action with the
  wanted result given that an action enabler later will enable it.

  This is done by checking the action result's hard requirements. Hard
  requirements must be TRUE before an action can be done. The reason why
  is usually that code dealing with the action assumes that the
  requirements are true. A requirement may also end up here if it can't be
  expressed in a requirement vector or if its absence makes the action
  pointless.

  @param result the action result to check the hard reqs for
  @param actor_unittype the unit type that may be able to act
  @return TRUE iff the specified unit type can perform the wanted action
          given that an action enabler later will enable it.
**************************************************************************/
bool
action_actor_utype_hard_reqs_ok(enum action_result result,
                                const struct unit_type *actor_unittype)
{
  return action_actor_utype_hard_reqs_ok_full(result, actor_unittype,
                                              FALSE);
}

/**********************************************************************//**
  Returns TRUE iff the wanted action is possible as far as the actor is
  concerned given that an action enabler later will enable it. Will, unlike
  action_actor_utype_hard_reqs_ok(), check the actor unit's current state.

  Can return maybe when not omniscient. Should always return yes or no when
  omniscient.
**************************************************************************/
static enum fc_tristate
action_hard_reqs_actor(enum action_result result,
                       const struct player *actor_player,
                       const struct city *actor_city,
                       const struct impr_type *actor_building,
                       const struct tile *actor_tile,
                       const struct unit *actor_unit,
                       const struct unit_type *actor_unittype,
                       const struct output_type *actor_output,
                       const struct specialist *actor_specialist,
                       const bool omniscient,
                       const struct city *homecity)
{
  if (!action_actor_utype_hard_reqs_ok_full(result, actor_unittype, TRUE)) {
    /* Info leak: The actor player knows the type of his unit. */
    /* The actor unit type can't perform the action because of hard
     * unit type requirements. */
    return TRI_NO;
  }

  switch (result) {
  case ACTRES_PARADROP:
    /* Reason: Keep the old rules. */
    /* Info leak: The player knows if his unit already has paradropped this
     * turn. */
    if (actor_unit->paradropped) {
      return TRI_NO;
    }

    break;

  case ACTRES_AIRLIFT:
    {
      /* Obligatory hard requirement. Checked here too since
       * action_hard_reqs_actor() may be called before any
       * action enablers are checked. */
      if (actor_city == NULL) {
        /* No city to airlift from. */
        return TRI_NO;
      }

      if (actor_player != city_owner(actor_city)
          && !(game.info.airlifting_style & AIRLIFTING_ALLIED_SRC
               && pplayers_allied(actor_player, city_owner(actor_city)))) {
        /* Not allowed to airlift from this source. */
        return TRI_NO;
      }

      if (!(omniscient || city_owner(actor_city) == actor_player)) {
        /* Can't check for airlifting capacity. */
        return TRI_MAYBE;
      }

      if (0 >= actor_city->airlift) {
        /* The source cannot airlift for this turn (maybe already airlifted
         * or no airport).
         *
         * Note that (game.info.airlifting_style & AIRLIFTING_UNLIMITED_SRC)
         * is not handled here because it applies only when the source city
         * has at least one remaining airlift.
         * See also do_airline() in server/unittools.h. */
        return TRI_NO;
      }
    }
    break;

  case ACTRES_CONVERT:
    /* Reason: Keep the old rules. */
    /* Info leak: The player knows his unit's cargo and location. */
    if (!unit_can_convert(actor_unit)) {
      return TRI_NO;
    }
    break;

  case ACTRES_TRANSPORT_BOARD:
  case ACTRES_TRANSPORT_EMBARK:
    if (unit_transported(actor_unit)) {
      if (!can_unit_unload(actor_unit, unit_transport_get(actor_unit))) {
        /* Can't leave current transport. */
        return TRI_NO;
      }
    }
    break;

  case ACTRES_TRANSPORT_DISEMBARK:
    if (!can_unit_unload(actor_unit, unit_transport_get(actor_unit))) {
      /* Keep the old rules about Unreachable and disembarks. */
      return TRI_NO;
    }
    break;

  case ACTRES_SENTRY:
    if (!can_unit_survive_at_tile(&(wld.map), actor_unit, actor_tile)
        && !unit_transported(actor_unit)) {
      /* Don't let units sentry on tiles they will die on. */
      return TRI_NO;
    }
    break;

  case ACTRES_ESTABLISH_EMBASSY:
  case ACTRES_SPY_INVESTIGATE_CITY:
  case ACTRES_SPY_POISON:
  case ACTRES_SPY_STEAL_GOLD:
  case ACTRES_SPY_SPREAD_PLAGUE:
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
  case ACTRES_CAPTURE_UNITS:
  case ACTRES_FOUND_CITY:
  case ACTRES_JOIN_CITY:
  case ACTRES_STEAL_MAPS:
  case ACTRES_BOMBARD:
  case ACTRES_SPY_NUKE:
  case ACTRES_NUKE:
  case ACTRES_NUKE_CITY:
  case ACTRES_NUKE_UNITS:
  case ACTRES_DESTROY_CITY:
  case ACTRES_EXPEL_UNIT:
  case ACTRES_RECYCLE_UNIT:
  case ACTRES_DISBAND_UNIT:
  case ACTRES_HOME_CITY:
  case ACTRES_UPGRADE_UNIT:
  case ACTRES_ATTACK:
  case ACTRES_STRIKE_BUILDING:
  case ACTRES_STRIKE_PRODUCTION:
  case ACTRES_CONQUER_CITY:
  case ACTRES_HEAL_UNIT:
  case ACTRES_TRANSFORM_TERRAIN:
  case ACTRES_CULTIVATE:
  case ACTRES_PLANT:
  case ACTRES_PILLAGE:
  case ACTRES_CLEAN_POLLUTION:
  case ACTRES_CLEAN_FALLOUT:
  case ACTRES_FORTIFY:
  case ACTRES_ROAD:
  case ACTRES_BASE:
  case ACTRES_MINE:
  case ACTRES_IRRIGATE:
  case ACTRES_TRANSPORT_ALIGHT:
  case ACTRES_TRANSPORT_UNLOAD:
  case ACTRES_SPY_ATTACK:
  case ACTRES_NONE:
    /* No hard unit requirements. */
    break;
  }

  return TRI_YES;
}

/**********************************************************************//**
  Returns if the wanted action is possible given that an action enabler
  later will enable it.

  Can return maybe when not omniscient. Should always return yes or no when
  omniscient.

  This is done by checking the action's hard requirements. Hard
  requirements must be fulfilled before an action can be done. The reason
  why is usually that code dealing with the action assumes that the
  requirements are true. A requirement may also end up here if it can't be
  expressed in a requirement vector or if its absence makes the action
  pointless.

  When adding a new hard requirement here:
   * explain why it is a hard requirement in a comment.
   * remember that this is called from action_prob(). Should information
     the player don't have access to be used in a test it must check if
     the evaluation can see the thing being tested.
**************************************************************************/
static enum fc_tristate
is_action_possible(const action_id wanted_action,
                   const struct player *actor_player,
                   const struct city *actor_city,
                   const struct impr_type *actor_building,
                   const struct tile *actor_tile,
                   const struct unit *actor_unit,
                   const struct unit_type *actor_unittype,
                   const struct output_type *actor_output,
                   const struct specialist *actor_specialist,
                   const struct player *target_player,
                   const struct city *target_city,
                   const struct impr_type *target_building,
                   const struct tile *target_tile,
                   const struct unit *target_unit,
                   const struct unit_type *target_unittype,
                   const struct output_type *target_output,
                   const struct specialist *target_specialist,
                   const struct extra_type *target_extra,
                   const bool omniscient,
                   const struct city *homecity)
{
  bool can_see_tgt_unit;
  bool can_see_tgt_tile;
  enum fc_tristate out;
  struct terrain *pterrain;
  struct action *paction;

  fc_assert_msg((action_id_get_target_kind(wanted_action) == ATK_CITY
                 && target_city != NULL)
                || (action_id_get_target_kind(wanted_action) == ATK_TILE
                    && target_tile != NULL)
                || (action_id_get_target_kind(wanted_action) == ATK_UNIT
                    && target_unit != NULL)
                || (action_id_get_target_kind(wanted_action) == ATK_UNITS
                    /* At this level each individual unit is tested. */
                    && target_unit != NULL)
                || (action_id_get_target_kind(wanted_action) == ATK_SELF),
                "Missing target!");

  paction = action_by_number(wanted_action);

  /* Only check requirement against the target unit if the actor can see it
   * or if the evaluator is omniscient. The game checking the rules is
   * omniscient. The player asking about his odds isn't. */
  can_see_tgt_unit = (omniscient || (target_unit
                                     && can_player_see_unit(actor_player,
                                                            target_unit)));

  /* Only check requirement against the target tile if the actor can see it
   * or if the evaluator is omniscient. The game checking the rules is
   * omniscient. The player asking about his odds isn't. */
  can_see_tgt_tile = (omniscient
                      || plr_sees_tile(actor_player, target_tile));

  /* Info leak: The player knows where his unit is. */
  if (action_get_target_kind(paction) != ATK_SELF
      && !action_distance_accepted(paction,
                                   real_map_distance(actor_tile,
                                                     target_tile))) {
    /* The distance between the actor and the target isn't inside the
     * action's accepted range. */
    return TRI_NO;
  }

  switch (action_get_target_kind(paction)) {
  case ATK_UNIT:
    /* The Freeciv code for all actions that is controlled by action
     * enablers and targets a unit assumes that the acting
     * player can see the target unit.
     * Examples:
     * - action_prob_vs_unit()'s quick check that the distance between actor
     *   and target is acceptable would leak distance to target unit if the
     *   target unit can't be seen.
     */
    if (!can_player_see_unit(actor_player, target_unit)) {
      return TRI_NO;
    }
    break;
  case ATK_CITY:
    /* The Freeciv code assumes that the player is aware of the target
     * city's existence. (How can you order an airlift to a city when you
     * don't know its city ID?) */
    if (fc_funcs->player_tile_city_id_get(city_tile(target_city),
                                          actor_player)
        != target_city->id) {
      return TRI_NO;
    }
    break;
  case ATK_UNITS:
  case ATK_TILE:
  case ATK_SELF:
    /* No special player knowledge checks. */
    break;
  case ATK_COUNT:
    fc_assert(action_get_target_kind(paction) != ATK_COUNT);
    break;
  }

  if (action_is_blocked_by(wanted_action, actor_unit,
                           target_tile, target_city, target_unit)) {
    /* Allows an action to block an other action. If a blocking action is
     * legal the actions it blocks becomes illegal. */
    return TRI_NO;
  }

  /* Actor specific hard requirements. */
  out = action_hard_reqs_actor(paction->result,
                               actor_player, actor_city, actor_building,
                               actor_tile, actor_unit, actor_unittype,
                               actor_output, actor_specialist,
                               omniscient, homecity);

  if (out == TRI_NO) {
    /* Illegal because of a hard actor requirement. */
    return TRI_NO;
  }

  /* Hard requirements for individual actions. */
  switch (paction->result) {
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

    if (utype_player_already_has_this_unique(actor_player,
                                             target_unittype)) {
      return TRI_NO;
    }

    /* FIXME: Capture Unit may want to look for more than one unique unit
     * of the same kind at the target tile. Currently caught by sanity
     * check in do_capture_units(). */

    break;

  case ACTRES_SPY_TARGETED_STEAL_TECH:
    /* Reason: The Freeciv code don't support selecting a target tech
     * unless it is known that the victim player has it. */
    /* Info leak: The actor player knowns who's techs he can see. */
    if (!can_see_techs_of_target(actor_player, target_player)) {
      return TRI_NO;
    }

    break;

  case ACTRES_SPY_STEAL_GOLD:
    /* If actor_unit can do the action the actor_player can see how much
     * gold target_player have. Not requireing it is therefore pointless.
     */
    if (target_player->economic.gold <= 0) {
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
      if (!can_cities_trade(homecity, target_city)) {
        return TRI_NO;
      }

      /* There are more restrictions on establishing a trade route than on
       * entering the market place. */
      if (action_has_result(paction, ACTRES_TRADE_ROUTE)
          && !can_establish_trade_route(homecity, target_city)) {
        return TRI_NO;
      }
    }

    break;

  case ACTRES_HELP_WONDER:
  case ACTRES_RECYCLE_UNIT:
    /* It is only possible to help the production if the production needs
     * the help. (If not it would be possible to add shields for something
     * that can't legally receive help if it is build later) */
    /* Info leak: The player knows that the production in his own city has
     * been hurried (bought or helped). The information isn't revealed when
     * asking for action probabilities since omniscient is FALSE. */
    if (!omniscient
        && !can_player_see_city_internals(actor_player, target_city)) {
      return TRI_MAYBE;
    }

    if (!(target_city->shield_stock
          < city_production_build_shield_cost(target_city))) {
      return TRI_NO;
    }

    break;

  case ACTRES_FOUND_CITY:
    if (game.scenario.prevent_new_cities) {
      /* Reason: allow scenarios to disable city founding. */
      /* Info leak: the setting is public knowledge. */
      return TRI_NO;
    }

    if (can_see_tgt_tile && tile_city(target_tile)) {
      /* Reason: a tile can have 0 or 1 cities. */
      return TRI_NO;
    }

    if (citymindist_prevents_city_on_tile(target_tile)) {
      if (omniscient) {
        /* No need to check again. */
        return TRI_NO;
      } else {
        square_iterate(&(wld.map), target_tile,
                       game.info.citymindist - 1, otile) {
          if (tile_city(otile) != NULL
              && plr_sees_tile(actor_player, otile)) {
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
      square_iterate(&(wld.map), target_tile,
                     game.info.citymindist - 1, otile) {
        if (!plr_sees_tile(actor_player, otile)) {
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
          && !player_can_see_city_externals(actor_player, target_city)) {
        return TRI_MAYBE;
      }

      new_pop = city_size_get(target_city) + unit_pop_value(actor_unit);

      if (new_pop > game.info.add_to_size_limit) {
        /* Reason: Make the add_to_size_limit setting work. */
        return TRI_NO;
      }

      if (!city_can_grow_to(target_city, new_pop)) {
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

  case ACTRES_BOMBARD:
    /* FIXME: Target of Bombard should be city and units. */
    if (tile_city(target_tile)
        && !pplayers_at_war(city_owner(tile_city(target_tile)),
                            actor_player)) {
      return TRI_NO;
    }

    break;


  case ACTRES_NUKE_UNITS:
    if (unit_attack_units_at_tile_result(actor_unit, target_tile)
        != ATT_OK) {
      /* Unreachable. */
      return TRI_NO;
    }

    break;

  case ACTRES_HOME_CITY:
    /* Reason: can't change to what is. */
    /* Info leak: The player knows his unit's current home city. */
    if (homecity != NULL && homecity->id == target_city->id) {
      /* This is already the unit's home city. */
      return TRI_NO;
    }

    {
      int slots = unit_type_get(actor_unit)->city_slots;

      if (slots > 0 && city_unit_slots_available(target_city) < slots) {
        return TRI_NO;
      }
    }

    break;

  case ACTRES_UPGRADE_UNIT:
    /* Reason: Keep the old rules. */
    /* Info leak: The player knows his unit's type. He knows if he can
     * build the unit type upgraded to. If the upgrade happens in a foreign
     * city that fact may leak. This can be seen as a price for changing
     * the rules to allow upgrading in a foreign city.
     * The player knows how much gold he has. If the Upgrade_Price_Pct
     * effect depends on information he don't have that information may
     * leak. The player knows the location of his unit. He knows if the
     * tile has a city and if the unit can exist there outside a transport.
     * The player knows his unit's cargo. By knowing their number and type
     * he can predict if there will be room for them in the unit upgraded
     * to as long as he knows what unit type his unit will end up as. */
    if (unit_upgrade_test(actor_unit, FALSE) != UU_OK) {
      return TRI_NO;
    }

    break;

  case ACTRES_PARADROP:
    /* Reason: Keep the old rules. */
    /* Info leak: The player knows if he knows the target tile. */
    if (!plr_knows_tile(actor_player, target_tile)) {
      return TRI_NO;
    }

    if (plr_sees_tile(actor_player, target_tile)) {
      /* Check for seen stuff that may kill the actor unit. */

      /* Reason: Keep the old rules. Be merciful. */
      /* Info leak: The player sees the target tile. */
      if (!can_unit_exist_at_tile(&(wld.map), actor_unit, target_tile)
          && (!game.info.paradrop_to_transport
              || !unit_could_load_at(actor_unit, target_tile))) {
        return TRI_NO;
      }

      /* Reason: Keep the old rules. Be merciful. */
      /* Info leak: The player sees the target tile. */
      if (is_non_attack_city_tile(target_tile, actor_player)) {
        return TRI_NO;
      }

      /* Reason: Keep the old rules. Be merciful. */
      /* Info leak: The player sees all units checked. Invisible units are
       * igonered. */
      unit_list_iterate(target_tile->units, pother) {
        if (can_player_see_unit(actor_player, pother)
            && pplayers_non_attack(actor_player, unit_owner(pother))) {
          return TRI_NO;
        }
      } unit_list_iterate_end;
    }

    /* Reason: Keep paratroopers_range working. */
    /* Info leak: The player knows the location of the actor and of the
     * target tile. */
    if (unit_type_get(actor_unit)->paratroopers_range
        < real_map_distance(actor_tile, target_tile)) {
      return TRI_NO;
    }

    break;

  case ACTRES_AIRLIFT:
    /* Reason: Keep the old rules. */
    /* Info leak: same as test_unit_can_airlift_to() */
    switch (test_unit_can_airlift_to(omniscient ? NULL : actor_player,
                                     actor_unit, target_city)) {
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
    /* Reason: Keep the old rules. */
    if (!can_unit_attack_tile(actor_unit, target_tile)) {
      return TRI_NO;
    }
    break;

  case ACTRES_CONQUER_CITY:
    /* Reason: "Conquer City" involves moving into the city. */
    if (!unit_can_move_to_tile(&(wld.map), actor_unit, target_tile,
                               FALSE, TRUE)) {
      return TRI_NO;
    }

    break;

  case ACTRES_HEAL_UNIT:
    /* Reason: It is not the healthy who need a doctor, but the sick. */
    /* Info leak: the actor can see the target's HP. */
    if (!(target_unit->hp < target_unittype->hp)) {
      return TRI_NO;
    }
    break;

  case ACTRES_TRANSFORM_TERRAIN:
    pterrain = tile_terrain(target_tile);
    if (pterrain->transform_result == T_NONE
        || pterrain == pterrain->transform_result
        || !terrain_surroundings_allow_change(target_tile,
                                              pterrain->transform_result)
        || (terrain_has_flag(pterrain->transform_result, TER_NO_CITIES)
            && (tile_city(target_tile)))) {
      return TRI_NO;
    }
    break;

  case ACTRES_CULTIVATE:
    pterrain = tile_terrain(target_tile);
    if (pterrain->cultivate_result == NULL) {
      return TRI_NO;
    }
    if (!terrain_surroundings_allow_change(target_tile,
                                           pterrain->cultivate_result)
        || (terrain_has_flag(pterrain->cultivate_result, TER_NO_CITIES)
            && tile_city(target_tile))) {
      return TRI_NO;
    }
    break;

  case ACTRES_PLANT:
    pterrain = tile_terrain(target_tile);
    if (pterrain->plant_result == NULL) {
      return TRI_NO;
    }
    if (!terrain_surroundings_allow_change(target_tile,
                                           pterrain->plant_result)
        || (terrain_has_flag(pterrain->plant_result, TER_NO_CITIES)
            && tile_city(target_tile))) {
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
    if (!can_build_road(extra_road_get(target_extra), actor_unit, target_tile)) {
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
    if (!can_build_base(actor_unit,
                        extra_base_get(target_extra), target_tile)) {
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

    pterrain = tile_terrain(target_tile);
    if (pterrain->mining_time == 0) {
      return TRI_NO;
    }

    if (!can_build_extra(target_extra, actor_unit, target_tile)) {
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

    pterrain = tile_terrain(target_tile);
    if (pterrain->irrigation_time == 0) {
      return TRI_NO;
    }

    if (!can_build_extra(target_extra, actor_unit, target_tile)) {
      return TRI_NO;
    }
    break;

  case ACTRES_PILLAGE:
    pterrain = tile_terrain(target_tile);
    if (pterrain->pillage_time == 0) {
      return TRI_NO;
    }

    {
      bv_extras pspresent = get_tile_infrastructure_set(target_tile, NULL);
      bv_extras psworking = get_unit_tile_pillage_set(target_tile);
      bv_extras pspossible;

      BV_CLR_ALL(pspossible);
      extra_type_iterate(pextra) {
        int idx = extra_index(pextra);

        /* Only one unit can pillage a given improvement at a time */
        if (BV_ISSET(pspresent, idx)
            && (!BV_ISSET(psworking, idx)
                || actor_unit->activity_target == pextra)
            && can_remove_extra(pextra, actor_unit, target_tile)) {
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

  case ACTRES_CLEAN_POLLUTION:
    {
      const struct extra_type *pextra;

      pterrain = tile_terrain(target_tile);
      if (pterrain->clean_pollution_time == 0) {
        return TRI_NO;
      }

      if (target_extra != NULL) {
        pextra = target_extra;
      } else {
        /* TODO: Make sure that all callers set target so that
         * we don't need this fallback. */
        pextra = prev_extra_in_tile(target_tile,
                                    ERM_CLEANPOLLUTION,
                                    actor_player,
                                    actor_unit);
        if (pextra == NULL) {
          /* No available pollution extras */
          return TRI_NO;
        }
      }

      if (!is_extra_removed_by(pextra, ERM_CLEANPOLLUTION)) {
        return TRI_NO;
      }

      if (!can_remove_extra(pextra, actor_unit, target_tile)) {
        return TRI_NO;
      }

      if (tile_has_extra(target_tile, pextra)) {
        return TRI_YES;
      }

      return TRI_NO;
    }
    break;

  case ACTRES_CLEAN_FALLOUT:
    {
      const struct extra_type *pextra;

      pterrain = tile_terrain(target_tile);
      if (pterrain->clean_fallout_time == 0) {
        return TRI_NO;
      }

      if (target_extra != NULL) {
        pextra = target_extra;
      } else {
        /* TODO: Make sure that all callers set target so that
         * we don't need this fallback. */
        pextra = prev_extra_in_tile(target_tile,
                                    ERM_CLEANFALLOUT,
                                    actor_player,
                                    actor_unit);
        if (pextra == NULL) {
          /* No available pollution extras */
          return TRI_NO;
        }
      }

      if (!is_extra_removed_by(pextra, ERM_CLEANFALLOUT)) {
        return TRI_NO;
      }

      if (!can_remove_extra(pextra, actor_unit, target_tile)) {
        return TRI_NO;
      }

      if (tile_has_extra(target_tile, pextra)) {
        return TRI_YES;
      }

      return TRI_NO;
    }
    break;

  case ACTRES_TRANSPORT_ALIGHT:
    if (!can_unit_unload(actor_unit, target_unit)) {
      /* Keep the old rules about Unreachable and disembarks. */
      return TRI_NO;
    }
    break;

  case ACTRES_TRANSPORT_BOARD:
    if (unit_transported(actor_unit)) {
      if (target_unit == unit_transport_get(actor_unit)) {
        /* Already inside this transport. */
        return TRI_NO;
      }
    }
    if (!could_unit_load(actor_unit, target_unit)) {
      /* Keep the old rules. */
      return TRI_NO;
    }
    break;

  case ACTRES_TRANSPORT_UNLOAD:
    if (!can_unit_unload(target_unit, actor_unit)) {
      /* Keep the old rules about Unreachable and disembarks. */
      return TRI_NO;
    }
    break;

  case ACTRES_TRANSPORT_DISEMBARK:
    if (!unit_can_move_to_tile(&(wld.map), actor_unit, target_tile,
                               FALSE, FALSE)) {
      /* Reason: involves moving to the tile. */
      return TRI_NO;
    }

    /* We cannot move a transport into a tile that holds
     * units or cities not allied with all of our cargo. */
    if (get_transporter_capacity(actor_unit) > 0) {
      unit_list_iterate(unit_tile(actor_unit)->units, pcargo) {
        if (unit_contained_in(pcargo, actor_unit)
            && (is_non_allied_unit_tile(target_tile, unit_owner(pcargo))
                || is_non_allied_city_tile(target_tile,
                                           unit_owner(pcargo)))) {
           return TRI_NO;
        }
      } unit_list_iterate_end;
    }
    break;

  case ACTRES_TRANSPORT_EMBARK:
    if (unit_transported(actor_unit)) {
      if (target_unit == unit_transport_get(actor_unit)) {
        /* Already inside this transport. */
        return TRI_NO;
      }
    }
    if (!could_unit_load(actor_unit, target_unit)) {
      /* Keep the old rules. */
      return TRI_NO;
    }
    if (!unit_can_move_to_tile(&(wld.map), actor_unit, target_tile,
                               FALSE, FALSE)) {
      /* Reason: involves moving to the tile. */
      return TRI_NO;
    }
    /* We cannot move a transport into a tile that holds
     * units or cities not allied with all of our cargo. */
    if (get_transporter_capacity(actor_unit) > 0) {
      unit_list_iterate(unit_tile(actor_unit)->units, pcargo) {
        if (unit_contained_in(pcargo, actor_unit)
            && (is_non_allied_unit_tile(target_tile, unit_owner(pcargo))
                || is_non_allied_city_tile(target_tile,
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
          && !can_player_see_hypotetic_units_at(actor_player,
                                                target_tile)) {
        /* May have a hidden diplomatic defender. */
        return TRI_MAYBE;
      }

      found = FALSE;
      unit_list_iterate(target_tile->units, punit) {
        struct player *uplayer = unit_owner(punit);

        if (uplayer == actor_player) {
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

  case ACTRES_SPY_SPREAD_PLAGUE:
    /* Enabling this action with illness_on = FALSE prevents spread. */
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
  case ACTRES_NUKE_CITY:
  case ACTRES_DESTROY_CITY:
  case ACTRES_EXPEL_UNIT:
  case ACTRES_DISBAND_UNIT:
  case ACTRES_CONVERT:
  case ACTRES_STRIKE_BUILDING:
  case ACTRES_STRIKE_PRODUCTION:
  case ACTRES_FORTIFY:
  case ACTRES_SENTRY:
  case ACTRES_NONE:
    /* No known hard coded requirements. */
    break;
  }

  return out;
}

/**********************************************************************//**
  Return TRUE iff the action enabler is active
**************************************************************************/
static bool is_enabler_active(const struct action_enabler *enabler,
			      const struct player *actor_player,
			      const struct city *actor_city,
			      const struct impr_type *actor_building,
			      const struct tile *actor_tile,
                              const struct unit *actor_unit,
			      const struct unit_type *actor_unittype,
			      const struct output_type *actor_output,
			      const struct specialist *actor_specialist,
			      const struct player *target_player,
			      const struct city *target_city,
			      const struct impr_type *target_building,
			      const struct tile *target_tile,
                              const struct unit *target_unit,
			      const struct unit_type *target_unittype,
			      const struct output_type *target_output,
			      const struct specialist *target_specialist)
{
  return are_reqs_active(actor_player, target_player, actor_city,
                         actor_building, actor_tile,
                         actor_unit, actor_unittype,
                         actor_output, actor_specialist, NULL,
                         &enabler->actor_reqs, RPT_CERTAIN)
      && are_reqs_active(target_player, actor_player, target_city,
                         target_building, target_tile,
                         target_unit, target_unittype,
                         target_output, target_specialist, NULL,
                         &enabler->target_reqs, RPT_CERTAIN);
}

/**********************************************************************//**
  Returns TRUE if the wanted action is enabled.

  Note that the action may disable it self because of hard requirements
  even if an action enabler returns TRUE.
**************************************************************************/
static bool is_action_enabled(const action_id wanted_action,
                              const struct player *actor_player,
                              const struct city *actor_city,
                              const struct impr_type *actor_building,
                              const struct tile *actor_tile,
                              const struct unit *actor_unit,
                              const struct unit_type *actor_unittype,
                              const struct output_type *actor_output,
                              const struct specialist *actor_specialist,
                              const struct player *target_player,
                              const struct city *target_city,
                              const struct impr_type *target_building,
                              const struct tile *target_tile,
                              const struct unit *target_unit,
                              const struct unit_type *target_unittype,
                              const struct output_type *target_output,
                              const struct specialist *target_specialist,
                              const struct extra_type *target_extra,
                              const struct city *actor_home)
{
  enum fc_tristate possible;

  possible = is_action_possible(wanted_action,
                                actor_player, actor_city,
                                actor_building, actor_tile,
                                actor_unit, actor_unittype,
                                actor_output, actor_specialist,
                                target_player, target_city,
                                target_building, target_tile,
                                target_unit, target_unittype,
                                target_output, target_specialist,
                                target_extra,
                                TRUE, actor_home);

  if (possible != TRI_YES) {
    /* This context is omniscient. Should be yes or no. */
    fc_assert_msg(possible != TRI_MAYBE,
                  "Is omniscient, should get yes or no.");

    /* The action enablers are irrelevant since the action it self is
     * impossible. */
    return FALSE;
  }

  action_enabler_list_iterate(action_enablers_for_action(wanted_action),
                              enabler) {
    if (is_enabler_active(enabler, actor_player, actor_city,
                          actor_building, actor_tile,
                          actor_unit, actor_unittype,
                          actor_output, actor_specialist,
                          target_player, target_city,
                          target_building, target_tile,
                          target_unit, target_unittype,
                          target_output, target_specialist)) {
      return TRUE;
    }
  } action_enabler_list_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  Returns TRUE if actor_unit can do wanted_action to target_city as far as
  action enablers are concerned.

  See note in is_action_enabled for why the action may still be disabled.
**************************************************************************/
static bool
is_action_enabled_unit_on_city_full(const action_id wanted_action,
                                    const struct unit *actor_unit,
                                    const struct city *actor_home,
                                    const struct tile *actor_tile,
                                    const struct city *target_city)
{
  const struct impr_type *target_building;
  const struct unit_type *target_utype;

  if (actor_unit == NULL || target_city == NULL) {
    /* Can't do an action when actor or target are missing. */
    return FALSE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_id_get_actor_kind(wanted_action),
                        FALSE, "Action %s is performed by %s not %s",
                        action_id_rule_name(wanted_action),
                        action_actor_kind_name(
                          action_id_get_actor_kind(wanted_action)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_CITY
                        == action_id_get_target_kind(wanted_action),
                        FALSE, "Action %s is against %s not %s",
                        action_id_rule_name(wanted_action),
                        action_target_kind_name(
                          action_id_get_target_kind(wanted_action)),
                        action_target_kind_name(ATK_CITY));

  fc_assert_ret_val(actor_tile, FALSE);

  if (!unit_can_do_action(actor_unit, wanted_action)) {
    /* No point in continuing. */
    return FALSE;
  }

  target_building = tgt_city_local_building(target_city);
  target_utype = tgt_city_local_utype(target_city);

  return is_action_enabled(wanted_action,
                           unit_owner(actor_unit), tile_city(actor_tile),
                           NULL, actor_tile,
                           actor_unit, unit_type_get(actor_unit),
                           NULL, NULL,
                           city_owner(target_city), target_city,
                           target_building, city_tile(target_city),
                           NULL, target_utype, NULL, NULL, NULL,
                           actor_home);
}

/**********************************************************************//**
  Returns TRUE if actor_unit can do wanted_action to target_city as far as
  action enablers are concerned.

  See note in is_action_enabled for why the action may still be disabled.
**************************************************************************/
bool is_action_enabled_unit_on_city(const action_id wanted_action,
                                    const struct unit *actor_unit,
                                    const struct city *target_city)
{
  return is_action_enabled_unit_on_city_full(wanted_action, actor_unit,
                                             unit_home(actor_unit),
                                             unit_tile(actor_unit),
                                             target_city);
}

/**********************************************************************//**
  Returns TRUE if actor_unit can do wanted_action to target_unit as far as
  action enablers are concerned.

  See note in is_action_enabled for why the action may still be disabled.
**************************************************************************/
static bool
is_action_enabled_unit_on_unit_full(const action_id wanted_action,
                                    const struct unit *actor_unit,
                                    const struct city *actor_home,
                                    const struct tile *actor_tile,
                                    const struct unit *target_unit)
{
  if (actor_unit == NULL || target_unit == NULL) {
    /* Can't do an action when actor or target are missing. */
    return FALSE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_id_get_actor_kind(wanted_action),
                        FALSE, "Action %s is performed by %s not %s",
                        action_id_rule_name(wanted_action),
                        action_actor_kind_name(
                          action_id_get_actor_kind(wanted_action)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_UNIT
                        == action_id_get_target_kind(wanted_action),
                        FALSE, "Action %s is against %s not %s",
                        action_id_rule_name(wanted_action),
                        action_target_kind_name(
                          action_id_get_target_kind(wanted_action)),
                        action_target_kind_name(ATK_UNIT));

  fc_assert_ret_val(actor_tile, FALSE);

  if (!unit_can_do_action(actor_unit, wanted_action)) {
    /* No point in continuing. */
    return FALSE;
  }

  return is_action_enabled(wanted_action,
                           unit_owner(actor_unit), tile_city(actor_tile),
                           NULL, actor_tile,
                           actor_unit, unit_type_get(actor_unit),
                           NULL, NULL,
                           unit_owner(target_unit),
                           tile_city(unit_tile(target_unit)), NULL,
                           unit_tile(target_unit),
                           target_unit, unit_type_get(target_unit),
                           NULL, NULL, NULL,
                           actor_home);
}

/**********************************************************************//**
  Returns TRUE if actor_unit can do wanted_action to target_unit as far as
  action enablers are concerned.

  See note in is_action_enabled for why the action may still be disabled.
**************************************************************************/
bool is_action_enabled_unit_on_unit(const action_id wanted_action,
                                    const struct unit *actor_unit,
                                    const struct unit *target_unit)
{
  return is_action_enabled_unit_on_unit_full(wanted_action, actor_unit,
                                             unit_home(actor_unit),
                                             unit_tile(actor_unit),
                                             target_unit);
}

/**********************************************************************//**
  Returns TRUE if actor_unit can do wanted_action to all units on the
  target_tile as far as action enablers are concerned.

  See note in is_action_enabled for why the action may still be disabled.
**************************************************************************/
static bool
is_action_enabled_unit_on_units_full(const action_id wanted_action,
                                     const struct unit *actor_unit,
                                     const struct city *actor_home,
                                     const struct tile *actor_tile,
                                     const struct tile *target_tile)
{
  if (actor_unit == NULL || target_tile == NULL
      || unit_list_size(target_tile->units) == 0) {
    /* Can't do an action when actor or target are missing. */
    return FALSE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_id_get_actor_kind(wanted_action),
                        FALSE, "Action %s is performed by %s not %s",
                        action_id_rule_name(wanted_action),
                        action_actor_kind_name(
                          action_id_get_actor_kind(wanted_action)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_UNITS
                        == action_id_get_target_kind(wanted_action),
                        FALSE, "Action %s is against %s not %s",
                        action_id_rule_name(wanted_action),
                        action_target_kind_name(
                          action_id_get_target_kind(wanted_action)),
                        action_target_kind_name(ATK_UNITS));

  fc_assert_ret_val(actor_tile, FALSE);

  if (!unit_can_do_action(actor_unit, wanted_action)) {
    /* No point in continuing. */
    return FALSE;
  }

  unit_list_iterate(target_tile->units, target_unit) {
    if (!is_action_enabled(wanted_action,
                           unit_owner(actor_unit), tile_city(actor_tile),
                           NULL, actor_tile,
                           actor_unit, unit_type_get(actor_unit),
                           NULL, NULL,
                           unit_owner(target_unit),
                           tile_city(unit_tile(target_unit)), NULL,
                           unit_tile(target_unit),
                           target_unit, unit_type_get(target_unit),
                           NULL, NULL, NULL, actor_home)) {
      /* One unit makes it impossible for all units. */
      return FALSE;
    }
  } unit_list_iterate_end;

  /* Not impossible for any of the units at the tile. */
  return TRUE;
}

/**********************************************************************//**
  Returns TRUE if actor_unit can do wanted_action to all units on the
  target_tile as far as action enablers are concerned.

  See note in is_action_enabled for why the action may still be disabled.
**************************************************************************/
bool is_action_enabled_unit_on_units(const action_id wanted_action,
                                     const struct unit *actor_unit,
                                     const struct tile *target_tile)
{
  return is_action_enabled_unit_on_units_full(wanted_action, actor_unit,
                                              unit_home(actor_unit),
                                              unit_tile(actor_unit),
                                              target_tile);
}

/**********************************************************************//**
  Returns TRUE if actor_unit can do wanted_action to the target_tile as far
  as action enablers are concerned.

  See note in is_action_enabled for why the action may still be disabled.
**************************************************************************/
static bool
is_action_enabled_unit_on_tile_full(const action_id wanted_action,
                                    const struct unit *actor_unit,
                                    const struct city *actor_home,
                                    const struct tile *actor_tile,
                                    const struct tile *target_tile,
                                    const struct extra_type *target_extra)
{
  if (actor_unit == NULL || target_tile == NULL) {
    /* Can't do an action when actor or target are missing. */
    return FALSE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_id_get_actor_kind(wanted_action),
                        FALSE, "Action %s is performed by %s not %s",
                        action_id_rule_name(wanted_action),
                        action_actor_kind_name(
                          action_id_get_actor_kind(wanted_action)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_TILE
                        == action_id_get_target_kind(wanted_action),
                        FALSE, "Action %s is against %s not %s",
                        action_id_rule_name(wanted_action),
                        action_target_kind_name(
                          action_id_get_target_kind(wanted_action)),
                        action_target_kind_name(ATK_TILE));

  fc_assert_ret_val(actor_tile, FALSE);

  if (!unit_can_do_action(actor_unit, wanted_action)) {
    /* No point in continuing. */
    return FALSE;
  }

  return is_action_enabled(wanted_action,
                           unit_owner(actor_unit), tile_city(actor_tile),
                           NULL, actor_tile,
                           actor_unit, unit_type_get(actor_unit),
                           NULL, NULL,
                           tile_owner(target_tile), tile_city(target_tile),
                           NULL, target_tile, NULL, NULL, NULL, NULL,
                           target_extra,
                           actor_home);
}

/**********************************************************************//**
  Returns TRUE if actor_unit can do wanted_action to the target_tile as far
  as action enablers are concerned.

  See note in is_action_enabled for why the action may still be disabled.
**************************************************************************/
bool is_action_enabled_unit_on_tile(const action_id wanted_action,
                                    const struct unit *actor_unit,
                                    const struct tile *target_tile,
                                    const struct extra_type *target_extra)
{
  return is_action_enabled_unit_on_tile_full(wanted_action, actor_unit,
                                             unit_home(actor_unit),
                                             unit_tile(actor_unit),
                                             target_tile, target_extra);
}

/**********************************************************************//**
  Returns TRUE if actor_unit can do wanted_action to itself as far as
  action enablers are concerned.

  See note in is_action_enabled() for why the action still may be
  disabled.
**************************************************************************/
static bool
is_action_enabled_unit_on_self_full(const action_id wanted_action,
                                    const struct unit *actor_unit,
                                    const struct city *actor_home,
                                    const struct tile *actor_tile)
{
  if (actor_unit == NULL) {
    /* Can't do an action when the actor is missing. */
    return FALSE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_id_get_actor_kind(wanted_action),
                        FALSE, "Action %s is performed by %s not %s",
                        action_id_rule_name(wanted_action),
                        action_actor_kind_name(
                          action_id_get_actor_kind(wanted_action)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_SELF
                        == action_id_get_target_kind(wanted_action),
                        FALSE, "Action %s is against %s not %s",
                        action_id_rule_name(wanted_action),
                        action_target_kind_name(
                          action_id_get_target_kind(wanted_action)),
                        action_target_kind_name(ATK_SELF));

  fc_assert_ret_val(actor_tile, FALSE);

  if (!unit_can_do_action(actor_unit, wanted_action)) {
    /* No point in continuing. */
    return FALSE;
  }

  return is_action_enabled(wanted_action,
                           unit_owner(actor_unit), tile_city(actor_tile),
                           NULL, actor_tile,
                           actor_unit, unit_type_get(actor_unit),
                           NULL, NULL,
                           NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                           actor_home);
}

/**********************************************************************//**
  Returns TRUE if actor_unit can do wanted_action to itself as far as
  action enablers are concerned.

  See note in is_action_enabled() for why the action still may be
  disabled.
**************************************************************************/
bool is_action_enabled_unit_on_self(const action_id wanted_action,
                                    const struct unit *actor_unit)
{
  return is_action_enabled_unit_on_self_full(wanted_action, actor_unit,
                                             unit_home(actor_unit),
                                             unit_tile(actor_unit));
}

/**********************************************************************//**
  Find out if the action is enabled, may be enabled or isn't enabled given
  what the player owning the actor knowns.

  A player don't always know everything needed to figure out if an action
  is enabled or not. A server side AI with the same limits on its knowledge
  as a human player or a client should use this to figure out what is what.

  Assumes to be called from the point of view of the actor. Its knowledge
  is assumed to be given in the parameters.

  Returns TRI_YES if the action is enabled, TRI_NO if it isn't and
  TRI_MAYBE if the player don't know enough to tell.

  If meta knowledge is missing TRI_MAYBE will be returned.
**************************************************************************/
static enum fc_tristate
action_enabled_local(const action_id wanted_action,
                     const struct player *actor_player,
                     const struct city *actor_city,
                     const struct impr_type *actor_building,
                     const struct tile *actor_tile,
                     const struct unit *actor_unit,
                     const struct output_type *actor_output,
                     const struct specialist *actor_specialist,
                     const struct player *target_player,
                     const struct city *target_city,
                     const struct impr_type *target_building,
                     const struct tile *target_tile,
                     const struct unit *target_unit,
                     const struct output_type *target_output,
                     const struct specialist *target_specialist)
{
  enum fc_tristate current;
  enum fc_tristate result;

  result = TRI_NO;
  action_enabler_list_iterate(action_enablers_for_action(wanted_action),
                              enabler) {
    current = fc_tristate_and(mke_eval_reqs(actor_player, actor_player,
                                            target_player, actor_city,
                                            actor_building, actor_tile,
                                            actor_unit, actor_output,
                                            actor_specialist,
                                            &enabler->actor_reqs,
                                            RPT_CERTAIN),
                              mke_eval_reqs(actor_player, target_player,
                                            actor_player, target_city,
                                            target_building, target_tile,
                                            target_unit, target_output,
                                            target_specialist,
                                            &enabler->target_reqs,
                                            RPT_CERTAIN));
    if (current == TRI_YES) {
      return TRI_YES;
    } else if (current == TRI_MAYBE) {
      result = TRI_MAYBE;
    }
  } action_enabler_list_iterate_end;

  return result;
}

/**********************************************************************//**
  Find out if the effect value is known

  The knowledge of the actor is assumed to be given in the parameters.

  If meta knowledge is missing TRI_MAYBE will be returned.
**************************************************************************/
static bool is_effect_val_known(enum effect_type effect_type,
                                const struct player *pow_player,
                                const struct player *target_player,
                                const struct player *other_player,
                                const struct city *target_city,
                                const struct impr_type *target_building,
                                const struct tile *target_tile,
                                const struct unit *target_unit,
                                const struct output_type *target_output,
                                const struct specialist *target_specialist)
{
  effect_list_iterate(get_effects(effect_type), peffect) {
    if (TRI_MAYBE == mke_eval_reqs(pow_player, target_player,
                                   other_player, target_city,
                                   target_building, target_tile,
                                   target_unit, target_output,
                                   target_specialist,
                                   &(peffect->reqs), RPT_CERTAIN)) {
      return FALSE;
    }
  } effect_list_iterate_end;

  return TRUE;
}

/**********************************************************************//**
  Does the target has any techs the actor don't?
**************************************************************************/
static enum fc_tristate
tech_can_be_stolen(const struct player *actor_player,
                   const struct player *target_player)
{
  const struct research *actor_research = research_get(actor_player);
  const struct research *target_research = research_get(target_player);

  if (actor_research != target_research) {
    if (can_see_techs_of_target(actor_player, target_player)) {
      advance_iterate(A_FIRST, padvance) {
        Tech_type_id i = advance_number(padvance);

        if (research_invention_state(target_research, i) == TECH_KNOWN
            && research_invention_gettable(actor_research, i,
                                           game.info.tech_steal_allow_holes)
            && (research_invention_state(actor_research, i) == TECH_UNKNOWN
                || (research_invention_state(actor_research, i)
                    == TECH_PREREQS_KNOWN))) {
          return TRI_YES;
        }
      } advance_iterate_end;
    } else {
      return TRI_MAYBE;
    }
  }

  return TRI_NO;
}

/**********************************************************************//**
  The action probability that pattacker will win a diplomatic battle.

  It is assumed that pattacker and pdefender have different owners and that
  the defender can defend in a diplomatic battle.

  See diplomat_success_vs_defender() in server/diplomats.c
**************************************************************************/
static struct act_prob ap_dipl_battle_win(const struct unit *pattacker,
                                          const struct unit *pdefender)
{
  /* Keep unconverted until the end to avoid scaling each step */
  int chance;
  struct act_prob out;

  /* Superspy always win */
  if (unit_has_type_flag(pdefender, UTYF_SUPERSPY)) {
    /* A defending UTYF_SUPERSPY will defeat every possible attacker. */
    return ACTPROB_IMPOSSIBLE;
  }
  if (unit_has_type_flag(pattacker, UTYF_SUPERSPY)) {
    /* An attacking UTYF_SUPERSPY will defeat every possible defender
     * except another UTYF_SUPERSPY. */
    return ACTPROB_CERTAIN;
  }

  /* Base chance is 50% */
  chance = 50;

  /* Spy attack bonus */
  if (unit_has_type_flag(pattacker, UTYF_SPY)) {
    chance += 25;
  }

  /* Spy defense bonus */
  if (unit_has_type_flag(pdefender, UTYF_SPY)) {
    chance -= 25;
  }

  /* Veteran attack and defense bonus */
  {
    const struct veteran_level *vatt =
        utype_veteran_level(unit_type_get(pattacker), pattacker->veteran);
    const struct veteran_level *vdef =
        utype_veteran_level(unit_type_get(pdefender), pdefender->veteran);

    chance += vatt->power_fact - vdef->power_fact;
  }

  /* Defense bonus. */
  {
    if (!is_effect_val_known(EFT_SPY_RESISTANT, unit_owner(pattacker),
                             tile_owner(pdefender->tile),  NULL,
                             tile_city(pdefender->tile), NULL,
                             pdefender->tile, NULL, NULL, NULL)) {
      return ACTPROB_NOT_KNOWN;
    }

    /* Reduce the chance of an attack by EFT_SPY_RESISTANT percent. */
    chance -= chance
              * get_target_bonus_effects(NULL,
                                         tile_owner(pdefender->tile), NULL,
                                         tile_city(pdefender->tile), NULL,
                                         pdefender->tile, NULL, NULL, NULL,
                                         NULL, NULL,
                                         EFT_SPY_RESISTANT) / 100;
  }

  /* Convert to action probability */
  out.min = chance * ACTPROB_VAL_1_PCT;
  out.max = chance * ACTPROB_VAL_1_PCT;

  return out;
}

/**********************************************************************//**
  The action probability that pattacker will win a diplomatic battle.

  See diplomat_infiltrate_tile() in server/diplomats.c
**************************************************************************/
static struct act_prob ap_diplomat_battle(const struct unit *pattacker,
                                          const struct unit *pvictim,
                                          const struct tile *tgt_tile)
{
  struct unit *pdefender;

  fc_assert_ret_val(tgt_tile, ACTPROB_NOT_KNOWN);

  if (!can_player_see_hypotetic_units_at(unit_owner(pattacker),
                                         tgt_tile)) {
    /* Don't leak information about unseen defenders. */
    return ACTPROB_NOT_KNOWN;
  }

  pdefender = get_diplomatic_defender(pattacker, pvictim, tgt_tile);

  if (pdefender) {
    /* There will be a diplomatic battle in stead of an action. */
    return ap_dipl_battle_win(pattacker, pdefender);
  };

  /* No diplomatic battle will occur. */
  return ACTPROB_CERTAIN;
}

/**********************************************************************//**
  Returns the action probability for when a target is unseen.
**************************************************************************/
static struct act_prob act_prob_unseen_target(action_id act_id,
                                              const struct unit *actor_unit)
{
  if (action_maybe_possible_actor_unit(act_id, actor_unit)) {
    /* Unknown because the target is unseen. */
    return ACTPROB_NOT_KNOWN;
  } else {
    /* The actor it self can't do this. */
    return ACTPROB_IMPOSSIBLE;
  }
}

/**********************************************************************//**
  An action's probability of success.

  "Success" indicates that the action achieves its goal, not that the
  actor survives. For actions that cost money it is assumed that the
  player has and is willing to spend the money. This is so the player can
  figure out what his odds are before deciding to get the extra money.
**************************************************************************/
static struct act_prob
action_prob(const action_id wanted_action,
            const struct player *actor_player,
            const struct city *actor_city,
            const struct impr_type *actor_building,
            const struct tile *actor_tile,
            const struct unit *actor_unit,
            const struct unit_type *actor_unittype_p,
            const struct output_type *actor_output,
            const struct specialist *actor_specialist,
            const struct city *actor_home,
            const struct player *target_player,
            const struct city *target_city,
            const struct impr_type *target_building,
            const struct tile *target_tile,
            const struct unit *target_unit,
            const struct unit_type *target_unittype_p,
            const struct output_type *target_output,
            const struct specialist *target_specialist,
            const struct extra_type *target_extra)
{
  int known;
  struct act_prob chance;
  const struct unit_type *actor_unittype;
  const struct unit_type *target_unittype;

  const struct action *paction = action_by_number(wanted_action);

  if (actor_unittype_p == NULL && actor_unit != NULL) {
    actor_unittype = unit_type_get(actor_unit);
  } else {
    actor_unittype = actor_unittype_p;
  }

  if (target_unittype_p == NULL && target_unit != NULL) {
    target_unittype = unit_type_get(target_unit);
  } else {
    target_unittype = target_unittype_p;
  }

  known = is_action_possible(wanted_action,
                             actor_player, actor_city,
                             actor_building, actor_tile,
                             actor_unit, actor_unittype,
                             actor_output, actor_specialist,
                             target_player, target_city,
                             target_building, target_tile,
                             target_unit, target_unittype,
                             target_output, target_specialist,
                             target_extra,
                             FALSE, actor_home);

  if (known == TRI_NO) {
    /* The action enablers are irrelevant since the action it self is
     * impossible. */
    return ACTPROB_IMPOSSIBLE;
  }

  chance = ACTPROB_NOT_IMPLEMENTED;

  known = fc_tristate_and(known,
                          action_enabled_local(wanted_action,
                                               actor_player, actor_city,
                                               actor_building, actor_tile,
                                               actor_unit,
                                               actor_output,
                                               actor_specialist,
                                               target_player, target_city,
                                               target_building, target_tile,
                                               target_unit,
                                               target_output,
                                               target_specialist));

  switch (paction->result) {
  case ACTRES_SPY_POISON:
    /* All uncertainty comes from potential diplomatic battles. */
    chance = ap_diplomat_battle(actor_unit, NULL, target_tile);
    break;
  case ACTRES_SPY_STEAL_GOLD:
    /* TODO */
    break;
  case ACTRES_SPY_SPREAD_PLAGUE:
    /* TODO */
    break;
  case ACTRES_STEAL_MAPS:
    /* TODO */
    break;
  case ACTRES_SPY_SABOTAGE_UNIT:
    /* All uncertainty comes from potential diplomatic battles. */
    chance = ap_diplomat_battle(actor_unit, target_unit, target_tile);
    break;
  case ACTRES_SPY_BRIBE_UNIT:
    /* All uncertainty comes from potential diplomatic battles. */
    chance = ap_diplomat_battle(actor_unit, target_unit, target_tile);
    break;
  case ACTRES_SPY_ATTACK:
    /* All uncertainty comes from potential diplomatic battles. */
    chance = ap_diplomat_battle(actor_unit, NULL, target_tile);
    break;
  case ACTRES_SPY_SABOTAGE_CITY:
    /* TODO */
    break;
  case ACTRES_SPY_TARGETED_SABOTAGE_CITY:
    /* TODO */
    break;
  case ACTRES_SPY_SABOTAGE_CITY_PRODUCTION:
    /* TODO */
    break;
  case ACTRES_SPY_INCITE_CITY:
    /* TODO */
    break;
  case ACTRES_ESTABLISH_EMBASSY:
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_SPY_STEAL_TECH:
    /* Do the victim have anything worth taking? */
    known = fc_tristate_and(known,
                            tech_can_be_stolen(actor_player,
                                               target_player));

    /* TODO: Calculate actual chance */

    break;
  case ACTRES_SPY_TARGETED_STEAL_TECH:
    /* Do the victim have anything worth taking? */
    known = fc_tristate_and(known,
                            tech_can_be_stolen(actor_player,
                                               target_player));

    /* TODO: Calculate actual chance */

    break;
  case ACTRES_SPY_INVESTIGATE_CITY:
    /* There is no risk that the city won't get investigated. */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_TRADE_ROUTE:
    /* TODO */
    break;
  case ACTRES_MARKETPLACE:
    /* Possible when not blocked by is_action_possible() */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_HELP_WONDER:
    /* Possible when not blocked by is_action_possible() */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_CAPTURE_UNITS:
    /* No battle is fought first. */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_EXPEL_UNIT:
    /* No battle is fought first. */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_BOMBARD:
    /* No battle is fought first. */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_FOUND_CITY:
    /* Possible when not blocked by is_action_possible() */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_JOIN_CITY:
    /* Possible when not blocked by is_action_possible() */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_SPY_NUKE:
    /* TODO: not implemented yet because:
     * - possible diplomatic battle could be handled with
     *   ap_diplomat_battle() so not a problem.
     * - dice roll diplchance * Action_Odds_Pct has no action probability
     *   calculation function yet. */
    break;
  case ACTRES_NUKE:
    /* TODO */
    break;
  case ACTRES_NUKE_CITY:
    /* TODO */
    break;
  case ACTRES_NUKE_UNITS:
    /* TODO */
    break;
  case ACTRES_DESTROY_CITY:
    /* No battle is fought first. */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_RECYCLE_UNIT:
    /* No battle is fought first. */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_DISBAND_UNIT:
    /* No battle is fought first. */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_HOME_CITY:
    /* No battle is fought first. */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_UPGRADE_UNIT:
    /* No battle is fought first. */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_PARADROP:
    /* TODO */
    break;
  case ACTRES_AIRLIFT:
    /* Possible when not blocked by is_action_possible() */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_ATTACK:
    {
      struct unit *defender_unit = get_defender(actor_unit, target_tile);

      if (can_player_see_unit(actor_player, defender_unit)) {
        double unconverted = unit_win_chance(actor_unit, defender_unit);

        chance.min = MAX(ACTPROB_VAL_MIN,
                         floor((double)ACTPROB_VAL_MAX * unconverted));
        chance.max = MIN(ACTPROB_VAL_MAX,
                         ceil((double)ACTPROB_VAL_MAX * unconverted));
      } else if (known == TRI_YES) {
        known = TRI_MAYBE;
      }
    }
    break;
  case ACTRES_STRIKE_BUILDING:
    /* TODO: not implemented yet because:
     * - dice roll 100% * Action_Odds_Pct has no action probability
     *   calculation function yet.
     * - sub target building may be missing. May be missing without player
     *   knowledge if it isn't visible. See is_improvement_visible() and
     *   can_player_see_city_internals(). */
    break;
  case ACTRES_STRIKE_PRODUCTION:
    /* TODO: not implemented yet because:
     * - dice roll 100% * Action_Odds_Pct has no action probability
     *   calculation function yet. */
    break;
  case ACTRES_CONQUER_CITY:
    /* No battle is fought first. */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_HEAL_UNIT:
    /* No battle is fought first. */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_TRANSFORM_TERRAIN:
  case ACTRES_CULTIVATE:
  case ACTRES_PLANT:
  case ACTRES_PILLAGE:
  case ACTRES_CLEAN_POLLUTION:
  case ACTRES_CLEAN_FALLOUT:
  case ACTRES_FORTIFY:
  case ACTRES_SENTRY:
  case ACTRES_ROAD:
  case ACTRES_CONVERT:
  case ACTRES_BASE:
  case ACTRES_MINE:
  case ACTRES_IRRIGATE:
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_TRANSPORT_ALIGHT:
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_TRANSPORT_BOARD:
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_TRANSPORT_EMBARK:
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_TRANSPORT_UNLOAD:
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_TRANSPORT_DISEMBARK:
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_NONE:
    /* Accommodate ruleset authors that wishes to roll the dice in Lua.
     * Would be ACTPROB_CERTAIN if not for that. */
    /* TODO: maybe allow the ruleset author to give a probability from
     * Lua? */
    chance = ACTPROB_NOT_IMPLEMENTED;
    break;
  }

  /* Non signal action probabilities should be in range. */
  fc_assert_action((action_prob_is_signal(chance)
                    || chance.max <= ACTPROB_VAL_MAX),
                   chance.max = ACTPROB_VAL_MAX);
  fc_assert_action((action_prob_is_signal(chance)
                    || chance.min >= ACTPROB_VAL_MIN),
                   chance.min = ACTPROB_VAL_MIN);

  switch (known) {
  case TRI_NO:
    return ACTPROB_IMPOSSIBLE;
    break;
  case TRI_MAYBE:
    return ACTPROB_NOT_KNOWN;
    break;
  case TRI_YES:
    return chance;
    break;
  };

  fc_assert_msg(FALSE, "Should be yes, maybe or no");

  return ACTPROB_NOT_IMPLEMENTED;
}

/**********************************************************************//**
  Get the actor unit's probability of successfully performing the chosen
  action on the target city.
**************************************************************************/
static struct act_prob
action_prob_vs_city_full(const struct unit* actor_unit,
                         const struct city *actor_home,
                         const struct tile *actor_tile,
                         const action_id act_id,
                         const struct city* target_city)
{
  const struct impr_type *target_building;
  const struct unit_type *target_utype;

  if (actor_unit == NULL || target_city == NULL) {
    /* Can't do an action when actor or target are missing. */
    return ACTPROB_IMPOSSIBLE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_id_get_actor_kind(act_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is performed by %s not %s",
                        action_id_rule_name(act_id),
                        action_actor_kind_name(
                          action_id_get_actor_kind(act_id)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_CITY == action_id_get_target_kind(act_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is against %s not %s",
                        action_id_rule_name(act_id),
                        action_target_kind_name(
                          action_id_get_target_kind(act_id)),
                        action_target_kind_name(ATK_CITY));

  fc_assert_ret_val(actor_tile, ACTPROB_IMPOSSIBLE);

  if (!unit_can_do_action(actor_unit, act_id)) {
    /* No point in continuing. */
    return ACTPROB_IMPOSSIBLE;
  }

  /* Doesn't leak information about city position since an unknown city
   * can't be targeted and a city can't move. */
  if (!action_id_distance_accepted(act_id,
          real_map_distance(actor_tile,
                            city_tile(target_city)))) {
    /* No point in continuing. */
    return ACTPROB_IMPOSSIBLE;
  }

  /* Doesn't leak information since it must be 100% certain from the
   * player's perspective that the blocking action is legal. */
  if (action_is_blocked_by(act_id, actor_unit,
                           city_tile(target_city), target_city, NULL)) {
    /* Don't offer to perform an action known to be blocked. */
    return ACTPROB_IMPOSSIBLE;
  }

  if (!player_can_see_city_externals(unit_owner(actor_unit), target_city)) {
    /* The invisible city at this tile may, as far as the player knows, not
     * exist anymore. */
    return act_prob_unseen_target(act_id, actor_unit);
  }

  target_building = tgt_city_local_building(target_city);
  target_utype = tgt_city_local_utype(target_city);

  return action_prob(act_id,
                     unit_owner(actor_unit), tile_city(actor_tile),
                     NULL, actor_tile, actor_unit, NULL,
                     NULL, NULL, actor_home,
                     city_owner(target_city), target_city,
                     target_building, city_tile(target_city),
                     NULL, target_utype, NULL, NULL, NULL);
}

/**********************************************************************//**
  Get the actor unit's probability of successfully performing the chosen
  action on the target city.
**************************************************************************/
struct act_prob action_prob_vs_city(const struct unit* actor_unit,
                                    const action_id act_id,
                                    const struct city* target_city)
{
  return action_prob_vs_city_full(actor_unit,
                                  unit_home(actor_unit),
                                  unit_tile(actor_unit),
                                  act_id, target_city);
}

/**********************************************************************//**
  Get the actor unit's probability of successfully performing the chosen
  action on the target unit.
**************************************************************************/
static struct act_prob
action_prob_vs_unit_full(const struct unit* actor_unit,
                         const struct city *actor_home,
                         const struct tile *actor_tile,
                         const action_id act_id,
                         const struct unit* target_unit)
{
  if (actor_unit == NULL || target_unit == NULL) {
    /* Can't do an action when actor or target are missing. */
    return ACTPROB_IMPOSSIBLE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_id_get_actor_kind(act_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is performed by %s not %s",
                        action_id_rule_name(act_id),
                        action_actor_kind_name(
                          action_id_get_actor_kind(act_id)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_UNIT == action_id_get_target_kind(act_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is against %s not %s",
                        action_id_rule_name(act_id),
                        action_target_kind_name(
                          action_id_get_target_kind(act_id)),
                        action_target_kind_name(ATK_UNIT));

  fc_assert_ret_val(actor_tile, ACTPROB_IMPOSSIBLE);

  if (!unit_can_do_action(actor_unit, act_id)) {
    /* No point in continuing. */
    return ACTPROB_IMPOSSIBLE;
  }

  /* Doesn't leak information about unit position since an unseen unit can't
   * be targeted. */
  if (!action_id_distance_accepted(act_id,
          real_map_distance(actor_tile,
                            unit_tile(target_unit)))) {
    /* No point in continuing. */
    return ACTPROB_IMPOSSIBLE;
  }

  return action_prob(act_id,
                     unit_owner(actor_unit), tile_city(actor_tile),
                     NULL, actor_tile, actor_unit, NULL,
                     NULL, NULL,
                     actor_home,
                     unit_owner(target_unit),
                     tile_city(unit_tile(target_unit)), NULL,
                     unit_tile(target_unit),
                     target_unit, NULL, NULL, NULL, NULL);
}

/**********************************************************************//**
  Get the actor unit's probability of successfully performing the chosen
  action on the target unit.
**************************************************************************/
struct act_prob action_prob_vs_unit(const struct unit* actor_unit,
                                    const action_id act_id,
                                    const struct unit* target_unit)
{
  return action_prob_vs_unit_full(actor_unit,
                                  unit_home(actor_unit),
                                  unit_tile(actor_unit),
                                  act_id,
                                  target_unit);
}

/**********************************************************************//**
  Get the actor unit's probability of successfully performing the chosen
  action on all units at the target tile.
**************************************************************************/
static struct act_prob
action_prob_vs_units_full(const struct unit* actor_unit,
                          const struct city *actor_home,
                          const struct tile *actor_tile,
                          const action_id act_id,
                          const struct tile* target_tile)
{
  struct act_prob prob_all;

  if (actor_unit == NULL || target_tile == NULL) {
    /* Can't do an action when actor or target are missing. */
    return ACTPROB_IMPOSSIBLE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_id_get_actor_kind(act_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is performed by %s not %s",
                        action_id_rule_name(act_id),
                        action_actor_kind_name(
                          action_id_get_actor_kind(act_id)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_UNITS == action_id_get_target_kind(act_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is against %s not %s",
                        action_id_rule_name(act_id),
                        action_target_kind_name(
                          action_id_get_target_kind(act_id)),
                        action_target_kind_name(ATK_UNITS));

  fc_assert_ret_val(actor_tile, ACTPROB_IMPOSSIBLE);

  if (!unit_can_do_action(actor_unit, act_id)) {
    /* No point in continuing. */
    return ACTPROB_IMPOSSIBLE;
  }

  /* Doesn't leak information about unit stack position since it is
   * specified as a tile and an unknown tile's position is known. */
  if (!action_id_distance_accepted(act_id,
                                   real_map_distance(actor_tile,
                                                     target_tile))) {
    /* No point in continuing. */
    return ACTPROB_IMPOSSIBLE;
  }

  /* Doesn't leak information since the actor player can see the target
   * tile. */
  if (tile_is_seen(target_tile, unit_owner(actor_unit))
      && tile_city(target_tile) != NULL
      && !utype_can_do_act_if_tgt_citytile(unit_type_get(actor_unit),
                                           act_id,
                                           CITYT_CENTER, TRUE)) {
    /* Don't offer to perform actions that never can target a unit stack in
     * a city. */
    return ACTPROB_IMPOSSIBLE;
  }

  /* Doesn't leak information since it must be 100% certain from the
   * player's perspective that the blocking action is legal. */
  unit_list_iterate(target_tile->units, target_unit) {
    if (action_is_blocked_by(act_id, actor_unit,
                             target_tile, tile_city(target_tile),
                             target_unit)) {
      /* Don't offer to perform an action known to be blocked. */
      return ACTPROB_IMPOSSIBLE;
    }
  } unit_list_iterate_end;

  /* Must be done here since an empty unseen tile will result in
   * ACTPROB_IMPOSSIBLE. */
  if (unit_list_size(target_tile->units) == 0) {
    /* Can't act against an empty tile. */

    if (player_can_trust_tile_has_no_units(unit_owner(actor_unit),
                                           target_tile)) {
      /* Known empty tile. */
      return ACTPROB_IMPOSSIBLE;
    } else {
      /* The player doesn't know that the tile is empty. */
      return act_prob_unseen_target(act_id, actor_unit);
    }
  }

  if ((action_id_has_result_safe(act_id, ACTRES_ATTACK)
       || action_id_has_result_safe(act_id, ACTRES_BOMBARD))
      && tile_city(target_tile) != NULL
      && !pplayers_at_war(city_owner(tile_city(target_tile)),
                          unit_owner(actor_unit))) {
    /* Hard coded rule: can't "Bombard", "Suicide Attack", or "Attack"
     * units in non enemy cities. */
    return ACTPROB_IMPOSSIBLE;
  }

  /* Invisible units at this tile can make the action legal or illegal.
   * Invisible units can be stacked with visible units. The possible
   * existence of invisible units therefore makes the result uncertain. */
  prob_all = (can_player_see_hypotetic_units_at(unit_owner(actor_unit),
                                                target_tile)
              ? ACTPROB_CERTAIN : ACTPROB_NOT_KNOWN);

  unit_list_iterate(target_tile->units, target_unit) {
    struct act_prob prob_unit;

    if (!can_player_see_unit(unit_owner(actor_unit), target_unit)) {
      /* Only visible units are considered. The invisible units contributed
       * their uncertainty to prob_all above. */
      continue;
    }

    prob_unit = action_prob(act_id,
                            unit_owner(actor_unit),
                            tile_city(actor_tile),
                            NULL, actor_tile, actor_unit, NULL,
                            NULL, NULL,
                            actor_home,
                            unit_owner(target_unit),
                            tile_city(unit_tile(target_unit)), NULL,
                            unit_tile(target_unit),
                            target_unit, NULL, NULL,
                            NULL, NULL);

    if (!action_prob_possible(prob_unit)) {
      /* One unit makes it impossible for all units. */
      return ACTPROB_IMPOSSIBLE;
    } else if (action_prob_not_impl(prob_unit)) {
      /* Not implemented dominates all except impossible. */
      prob_all = ACTPROB_NOT_IMPLEMENTED;
    } else {
      fc_assert_msg(!action_prob_is_signal(prob_unit),
                    "Invalid probability [%d, %d]",
                    prob_unit.min, prob_unit.max);

      if (action_prob_is_signal(prob_all)) {
        /* Special values dominate regular values. */
        continue;
      }

      /* Probability against all target units considered until this moment
       * and the probability against this target unit. */
      prob_all.min = (prob_all.min * prob_unit.min) / ACTPROB_VAL_MAX;
      prob_all.max = (prob_all.max * prob_unit.max) / ACTPROB_VAL_MAX;
      break;
    }
  } unit_list_iterate_end;

  /* Not impossible for any of the units at the tile. */
  return prob_all;
}

/**********************************************************************//**
  Get the actor unit's probability of successfully performing the chosen
  action on all units at the target tile.
**************************************************************************/
struct act_prob action_prob_vs_units(const struct unit* actor_unit,
                                     const action_id act_id,
                                     const struct tile* target_tile)
{
  return action_prob_vs_units_full(actor_unit,
                                   unit_home(actor_unit),
                                   unit_tile(actor_unit),
                                   act_id,
                                   target_tile);
}

/**********************************************************************//**
  Get the actor unit's probability of successfully performing the chosen
  action on the target tile.
**************************************************************************/
static struct act_prob
action_prob_vs_tile_full(const struct unit *actor_unit,
                         const struct city *actor_home,
                         const struct tile *actor_tile,
                         const action_id act_id,
                         const struct tile *target_tile,
                         const struct extra_type *target_extra)
{
  if (actor_unit == NULL || target_tile == NULL) {
    /* Can't do an action when actor or target are missing. */
    return ACTPROB_IMPOSSIBLE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_id_get_actor_kind(act_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is performed by %s not %s",
                        action_id_rule_name(act_id),
                        action_actor_kind_name(
                          action_id_get_actor_kind(act_id)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_TILE == action_id_get_target_kind(act_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is against %s not %s",
                        action_id_rule_name(act_id),
                        action_target_kind_name(
                          action_id_get_target_kind(act_id)),
                        action_target_kind_name(ATK_TILE));

  fc_assert_ret_val(actor_tile, ACTPROB_IMPOSSIBLE);

  if (!unit_can_do_action(actor_unit, act_id)) {
    /* No point in continuing. */
    return ACTPROB_IMPOSSIBLE;
  }

  /* Doesn't leak information about tile position since an unknown tile's
   * position is known. */
  if (!action_id_distance_accepted(act_id,
                                   real_map_distance(actor_tile,
                                                     target_tile))) {
    /* No point in continuing. */
    return ACTPROB_IMPOSSIBLE;
  }

  return action_prob(act_id,
                     unit_owner(actor_unit), tile_city(actor_tile),
                     NULL, actor_tile, actor_unit, NULL,
                     NULL, NULL, actor_home,
                     tile_owner(target_tile), tile_city(target_tile), NULL,
                     target_tile, NULL, NULL, NULL, NULL, target_extra);
}

/**********************************************************************//**
  Get the actor unit's probability of successfully performing the chosen
  action on the target tile.
**************************************************************************/
struct act_prob action_prob_vs_tile(const struct unit *actor_unit,
                                    const action_id act_id,
                                    const struct tile *target_tile,
                                    const struct extra_type *target_extra)
{
  return action_prob_vs_tile_full(actor_unit,
                                  unit_home(actor_unit),
                                  unit_tile(actor_unit),
                                  act_id, target_tile, target_extra);
}

/**********************************************************************//**
  Get the actor unit's probability of successfully performing the chosen
  action on itself.
**************************************************************************/
static struct act_prob
action_prob_self_full(const struct unit* actor_unit,
                      const struct city *actor_home,
                      const struct tile *actor_tile,
                      const action_id act_id)
{
  if (actor_unit == NULL) {
    /* Can't do the action when the actor is missing. */
    return ACTPROB_IMPOSSIBLE;
  }

  /* No point in checking distance to target. It is always 0. */

  fc_assert_ret_val_msg(AAK_UNIT == action_id_get_actor_kind(act_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is performed by %s not %s",
                        action_id_rule_name(act_id),
                        action_actor_kind_name(
                          action_id_get_actor_kind(act_id)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_SELF == action_id_get_target_kind(act_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is against %s not %s",
                        action_id_rule_name(act_id),
                        action_target_kind_name(
                          action_id_get_target_kind(act_id)),
                        action_target_kind_name(ATK_SELF));

  fc_assert_ret_val(actor_tile, ACTPROB_IMPOSSIBLE);

  if (!unit_can_do_action(actor_unit, act_id)) {
    /* No point in continuing. */
    return ACTPROB_IMPOSSIBLE;
  }

  return action_prob(act_id,
                     unit_owner(actor_unit), tile_city(actor_tile),
                     NULL, actor_tile, actor_unit, NULL, NULL, NULL,
                     actor_home,
                     NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                     NULL);
}

/**********************************************************************//**
  Get the actor unit's probability of successfully performing the chosen
  action on itself.
**************************************************************************/
struct act_prob action_prob_self(const struct unit* actor_unit,
                                 const action_id act_id)
{
  return action_prob_self_full(actor_unit,
                               unit_home(actor_unit),
                               unit_tile(actor_unit),
                               act_id);
}

/**********************************************************************//**
  Returns the actor unit's probability of successfully performing the
  specified action against the action specific target.
  @param paction the action to perform
  @param act_unit the actor unit
  @param tgt_city the target for city targeted actions
  @param tgt_unit the target for unit targeted actions
  @param tgt_tile the target for tile and unit stack targeted actions
  @param extra_tgt the target for extra sub targeted actions
  @return the action probability of performing the action
**************************************************************************/
struct act_prob action_prob_unit_vs_tgt(const struct action *paction,
                                        const struct unit *act_unit,
                                        const struct city *tgt_city,
                                        const struct unit *tgt_unit,
                                        const struct tile *tgt_tile,
                                        const struct extra_type *extra_tgt)
{
  /* Assume impossible until told otherwise. */
  struct act_prob prob = ACTPROB_IMPOSSIBLE;

  fc_assert_ret_val(paction, ACTPROB_IMPOSSIBLE);
  fc_assert_ret_val(act_unit, ACTPROB_IMPOSSIBLE);

  switch (action_get_target_kind(paction)) {
  case ATK_UNITS:
    if (tgt_tile) {
      prob = action_prob_vs_units(act_unit, paction->id, tgt_tile);
    }
    break;
  case ATK_TILE:
    if (tgt_tile) {
      prob = action_prob_vs_tile(act_unit, paction->id, tgt_tile, extra_tgt);
    }
    break;
  case ATK_CITY:
    if (tgt_city) {
      prob = action_prob_vs_city(act_unit, paction->id, tgt_city);
    }
    break;
  case ATK_UNIT:
    if (tgt_unit) {
      prob = action_prob_vs_unit(act_unit, paction->id, tgt_unit);
    }
    break;
  case ATK_SELF:
    prob = action_prob_self(act_unit, paction->id);
    break;
  case ATK_COUNT:
    log_error("Invalid action target kind");
    break;
  }

  return prob;
}

/**********************************************************************//**
  Returns a speculation about the actor unit's probability of successfully
  performing the chosen action on the target city given the specified
  game state changes.
**************************************************************************/
struct act_prob action_speculate_unit_on_city(const action_id act_id,
                                              const struct unit *actor,
                                              const struct city *actor_home,
                                              const struct tile *actor_tile,
                                              const bool omniscient_cheat,
                                              const struct city* target)
{
  /* FIXME: some unit state requirements still depend on the actor unit's
   * current position rather than on actor_tile. Maybe this function should
   * return ACTPROB_NOT_IMPLEMENTED when one of those is detected and no
   * other requirement makes the action ACTPROB_IMPOSSIBLE? */

  if (omniscient_cheat) {
    if (is_action_enabled_unit_on_city_full(act_id,
                                            actor, actor_home, actor_tile,
                                            target)) {
      return ACTPROB_CERTAIN;
    } else {
      return ACTPROB_IMPOSSIBLE;
    }
  } else {
    return action_prob_vs_city_full(actor, actor_home, actor_tile,
                                    act_id, target);
  }
}

/**********************************************************************//**
  Returns a speculation about the actor unit's probability of successfully
  performing the chosen action on the target unit given the specified
  game state changes.
**************************************************************************/
struct act_prob
action_speculate_unit_on_unit(action_id act_id,
                              const struct unit *actor,
                              const struct city *actor_home,
                              const struct tile *actor_tile,
                              bool omniscient_cheat,
                              const struct unit *target)
{
  /* FIXME: some unit state requirements still depend on the actor unit's
   * current position rather than on actor_tile. Maybe this function should
   * return ACTPROB_NOT_IMPLEMENTED when one of those is detected and no
   * other requirement makes the action ACTPROB_IMPOSSIBLE? */

  if (omniscient_cheat) {
    if (is_action_enabled_unit_on_unit_full(act_id,
                                            actor, actor_home, actor_tile,
                                            target)) {
      return ACTPROB_CERTAIN;
    } else {
      return ACTPROB_IMPOSSIBLE;
    }
  } else {
    return action_prob_vs_unit_full(actor, actor_home, actor_tile,
                                    act_id, target);
  }
}

/**********************************************************************//**
  Returns a speculation about the actor unit's probability of successfully
  performing the chosen action on the target unit stack given the specified
  game state changes.
**************************************************************************/
struct act_prob
action_speculate_unit_on_units(action_id act_id,
                               const struct unit *actor,
                               const struct city *actor_home,
                               const struct tile *actor_tile,
                               bool omniscient_cheat,
                               const struct tile *target)
{
  /* FIXME: some unit state requirements still depend on the actor unit's
   * current position rather than on actor_tile. Maybe this function should
   * return ACTPROB_NOT_IMPLEMENTED when one of those is detected and no
   * other requirement makes the action ACTPROB_IMPOSSIBLE? */

  if (omniscient_cheat) {
    if (is_action_enabled_unit_on_units_full(act_id,
                                             actor, actor_home, actor_tile,
                                             target)) {
      return ACTPROB_CERTAIN;
    } else {
      return ACTPROB_IMPOSSIBLE;
    }
  } else {
    return action_prob_vs_units_full(actor, actor_home, actor_tile,
                                     act_id, target);
  }
}

/**********************************************************************//**
  Returns a speculation about the actor unit's probability of successfully
  performing the chosen action on the target tile (and, if specified,
  extra) given the specified game state changes.
**************************************************************************/
struct act_prob
action_speculate_unit_on_tile(action_id act_id,
                              const struct unit *actor,
                              const struct city *actor_home,
                              const struct tile *actor_tile,
                              bool omniscient_cheat,
                              const struct tile *target_tile,
                              const struct extra_type *target_extra)
{
  /* FIXME: some unit state requirements still depend on the actor unit's
   * current position rather than on actor_tile. Maybe this function should
   * return ACTPROB_NOT_IMPLEMENTED when one of those is detected and no
   * other requirement makes the action ACTPROB_IMPOSSIBLE? */

  if (omniscient_cheat) {
    if (is_action_enabled_unit_on_tile_full(act_id,
                                            actor, actor_home, actor_tile,
                                            target_tile, target_extra)) {
      return ACTPROB_CERTAIN;
    } else {
      return ACTPROB_IMPOSSIBLE;
    }
  } else {
    return action_prob_vs_tile_full(actor, actor_home, actor_tile,
                                    act_id, target_tile, target_extra);
  }
}

/**********************************************************************//**
  Returns a speculation about the actor unit's probability of successfully
  performing the chosen action on itself given the specified game state
  changes.
**************************************************************************/
struct act_prob
action_speculate_unit_on_self(action_id act_id,
                              const struct unit *actor,
                              const struct city *actor_home,
                              const struct tile *actor_tile,
                              bool omniscient_cheat)
{
  /* FIXME: some unit state requirements still depend on the actor unit's
   * current position rather than on actor_tile. Maybe this function should
   * return ACTPROB_NOT_IMPLEMENTED when one of those is detected and no
   * other requirement makes the action ACTPROB_IMPOSSIBLE? */

  if (omniscient_cheat) {
    if (is_action_enabled_unit_on_self_full(act_id,
                                            actor, actor_home, actor_tile)) {
      return ACTPROB_CERTAIN;
    } else {
      return ACTPROB_IMPOSSIBLE;
    }
  } else {
    return action_prob_self_full(actor, actor_home, actor_tile,
                                 act_id);
  }
}

/**********************************************************************//**
  Returns the impossible action probability.
**************************************************************************/
struct act_prob action_prob_new_impossible(void)
{
  struct act_prob out = { ACTPROB_VAL_MIN, ACTPROB_VAL_MIN };

  return out;
}

/**********************************************************************//**
  Returns the certain action probability.
**************************************************************************/
struct act_prob action_prob_new_certain(void)
{
  struct act_prob out = { ACTPROB_VAL_MAX, ACTPROB_VAL_MAX };

  return out;
}

/**********************************************************************//**
  Returns the n/a action probability.
**************************************************************************/
struct act_prob action_prob_new_not_relevant(void)
{
  struct act_prob out = { ACTPROB_VAL_NA, ACTPROB_VAL_MIN};

  return out;
}

/**********************************************************************//**
  Returns the "not implemented" action probability.
**************************************************************************/
struct act_prob action_prob_new_not_impl(void)
{
  struct act_prob out = { ACTPROB_VAL_NOT_IMPL, ACTPROB_VAL_MIN };

  return out;
}

/**********************************************************************//**
  Returns the "user don't know" action probability.
**************************************************************************/
struct act_prob action_prob_new_unknown(void)
{
  struct act_prob out = { ACTPROB_VAL_MIN, ACTPROB_VAL_MAX };

  return out;
}

/**********************************************************************//**
  Returns TRUE iff the given action probability belongs to an action that
  may be possible.
**************************************************************************/
bool action_prob_possible(const struct act_prob probability)
{
  return (ACTPROB_VAL_MIN < probability.max
          || action_prob_not_impl(probability));
}

/**********************************************************************//**
  Returns TRUE iff the given action probability is certain that its action
  is possible.
**************************************************************************/
bool action_prob_certain(const struct act_prob probability)
{
  return (ACTPROB_VAL_MAX == probability.min
          && ACTPROB_VAL_MAX == probability.max);
}

/**********************************************************************//**
  Returns TRUE iff the given action probability represents the lack of
  an action probability.
**************************************************************************/
static inline bool
action_prob_not_relevant(const struct act_prob probability)
{
  return probability.min == ACTPROB_VAL_NA
         && probability.max == ACTPROB_VAL_MIN;
}

/**********************************************************************//**
  Returns TRUE iff the given action probability represents that support
  for finding this action probability currently is missing from Freeciv.
**************************************************************************/
static inline bool
action_prob_not_impl(const struct act_prob probability)
{
  return probability.min == ACTPROB_VAL_NOT_IMPL
         && probability.max == ACTPROB_VAL_MIN;
}

/**********************************************************************//**
  Returns TRUE iff the given action probability represents a special
  signal value rather than a regular action probability value.
**************************************************************************/
static inline bool
action_prob_is_signal(const struct act_prob probability)
{
  return probability.max < probability.min;
}

/**********************************************************************//**
  Returns TRUE iff ap1 and ap2 are equal.
**************************************************************************/
bool are_action_probabilitys_equal(const struct act_prob *ap1,
                                   const struct act_prob *ap2)
{
  return ap1->min == ap2->min && ap1->max == ap2->max;
}

/**********************************************************************//**
  Compare action probabilities. Prioritize the lowest possible value.
**************************************************************************/
int action_prob_cmp_pessimist(const struct act_prob ap1,
                              const struct act_prob ap2)
{
  struct act_prob my_ap1;
  struct act_prob my_ap2;

  /* The action probabilities are real. */
  fc_assert(!action_prob_not_relevant(ap1));
  fc_assert(!action_prob_not_relevant(ap2));

  /* Convert any signals to ACTPROB_NOT_KNOWN. */
  if (action_prob_is_signal(ap1)) {
    /* Assert that it is OK to convert the signal. */
    fc_assert(action_prob_not_impl(ap1));

    my_ap1 = ACTPROB_NOT_KNOWN;
  } else {
    my_ap1 = ap1;
  }

  if (action_prob_is_signal(ap2)) {
    /* Assert that it is OK to convert the signal. */
    fc_assert(action_prob_not_impl(ap2));

    my_ap2 = ACTPROB_NOT_KNOWN;
  } else {
    my_ap2 = ap2;
  }

  /* The action probabilities now have a comparison friendly form. */
  fc_assert(!action_prob_is_signal(my_ap1));
  fc_assert(!action_prob_is_signal(my_ap2));

  /* Do the comparison. Start with min. Continue with max. */
  if (my_ap1.min < my_ap2.min) {
    return -1;
  } else if (my_ap1.min > my_ap2.min) {
    return 1;
  } else if (my_ap1.max < my_ap2.max) {
    return -1;
  } else if (my_ap1.max > my_ap2.max) {
    return 1;
  } else {
    return 0;
  }
}

/**********************************************************************//**
  Returns double in the range [0-1] representing the minimum of the given
  action probability.
**************************************************************************/
double action_prob_to_0_to_1_pessimist(const struct act_prob ap)
{
  struct act_prob my_ap;

  /* The action probability is real. */
  fc_assert(!action_prob_not_relevant(ap));

  /* Convert any signals to ACTPROB_NOT_KNOWN. */
  if (action_prob_is_signal(ap)) {
    /* Assert that it is OK to convert the signal. */
    fc_assert(action_prob_not_impl(ap));

    my_ap = ACTPROB_NOT_KNOWN;
  } else {
    my_ap = ap;
  }

  /* The action probability now has a math friendly form. */
  fc_assert(!action_prob_is_signal(my_ap));

  return (double)my_ap.min / (double) ACTPROB_VAL_MAX;
}

/**********************************************************************//**
  Returns ap1 with ap2 as fall back in cases where ap1 doesn't happen.
  Said in math that is: P(A) + P(A') * P(B)

  This is useful to calculate the probability of doing action A or, when A
  is impossible, falling back to doing action B.
**************************************************************************/
struct act_prob action_prob_fall_back(const struct act_prob *ap1,
                                      const struct act_prob *ap2)
{
  struct act_prob my_ap1;
  struct act_prob my_ap2;
  struct act_prob out;

  /* The action probabilities are real. */
  fc_assert(ap1 && !action_prob_not_relevant(*ap1));
  fc_assert(ap2 && !action_prob_not_relevant(*ap2));

  if (action_prob_is_signal(*ap1)
      && are_action_probabilitys_equal(ap1, ap2)) {
    /* Keep the information rather than converting the signal to
     * ACTPROB_NOT_KNOWN. */

    /* Assert that it is OK to convert the signal. */
    fc_assert(action_prob_not_impl(*ap1));

    out.min = ap1->min;
    out.max = ap2->max;

    return out;
  }

  /* Convert any signals to ACTPROB_NOT_KNOWN. */
  if (action_prob_is_signal(*ap1)) {
    /* Assert that it is OK to convert the signal. */
    fc_assert(action_prob_not_impl(*ap1));

    my_ap1.min = ACTPROB_VAL_MIN;
    my_ap1.max = ACTPROB_VAL_MAX;
  } else {
    my_ap1.min = ap1->min;
    my_ap1.max = ap1->max;
  }

  if (action_prob_is_signal(*ap2)) {
    /* Assert that it is OK to convert the signal. */
    fc_assert(action_prob_not_impl(*ap2));

    my_ap2.min = ACTPROB_VAL_MIN;
    my_ap2.max = ACTPROB_VAL_MAX;
  } else {
    my_ap2.min = ap2->min;
    my_ap2.max = ap2->max;
  }

  /* The action probabilities now have a math friendly form. */
  fc_assert(!action_prob_is_signal(my_ap1));
  fc_assert(!action_prob_is_signal(my_ap2));

  /* Do the math. */
  out.min = my_ap1.min + (((ACTPROB_VAL_MAX - my_ap1.min) * my_ap2.min)
                          / ACTPROB_VAL_MAX);
  out.max = my_ap1.max + (((ACTPROB_VAL_MAX - my_ap1.max) * my_ap2.max)
                          / ACTPROB_VAL_MAX);

  /* Cap at 100%. */
  out.min = MIN(out.min, ACTPROB_VAL_MAX);
  out.max = MIN(out.max, ACTPROB_VAL_MAX);

  return out;
}

/**********************************************************************//**
  Returns the initial odds of an action not failing its dice roll.
**************************************************************************/
int action_dice_roll_initial_odds(const struct action *paction)
{
  switch (paction->result) {
  case ACTRES_STRIKE_BUILDING:
  case ACTRES_STRIKE_PRODUCTION:
    /* No initial odds. */
    return 100;
  case ACTRES_SPY_SPREAD_PLAGUE:
  case ACTRES_SPY_STEAL_TECH:
  case ACTRES_SPY_TARGETED_STEAL_TECH:
  case ACTRES_SPY_INCITE_CITY:
  case ACTRES_SPY_SABOTAGE_CITY:
  case ACTRES_SPY_TARGETED_SABOTAGE_CITY:
  case ACTRES_SPY_SABOTAGE_CITY_PRODUCTION:
  case ACTRES_SPY_STEAL_GOLD:
  case ACTRES_STEAL_MAPS:
  case ACTRES_SPY_NUKE:
    /* Take the initial odds from the diplchance setting. */
    return server_setting_value_int_get(
               server_setting_by_name("diplchance"));
  case ACTRES_ESTABLISH_EMBASSY:
  case ACTRES_SPY_INVESTIGATE_CITY:
  case ACTRES_SPY_POISON:
  case ACTRES_TRADE_ROUTE:
  case ACTRES_MARKETPLACE:
  case ACTRES_HELP_WONDER:
  case ACTRES_SPY_BRIBE_UNIT:
  case ACTRES_SPY_SABOTAGE_UNIT:
  case ACTRES_CAPTURE_UNITS:
  case ACTRES_FOUND_CITY:
  case ACTRES_JOIN_CITY:
  case ACTRES_BOMBARD:
  case ACTRES_NUKE:
  case ACTRES_NUKE_CITY:
  case ACTRES_NUKE_UNITS:
  case ACTRES_DESTROY_CITY:
  case ACTRES_EXPEL_UNIT:
  case ACTRES_RECYCLE_UNIT:
  case ACTRES_DISBAND_UNIT:
  case ACTRES_HOME_CITY:
  case ACTRES_UPGRADE_UNIT:
  case ACTRES_PARADROP:
  case ACTRES_AIRLIFT:
  case ACTRES_ATTACK:
  case ACTRES_CONQUER_CITY:
  case ACTRES_HEAL_UNIT:
  case ACTRES_TRANSFORM_TERRAIN:
  case ACTRES_CULTIVATE:
  case ACTRES_PLANT:
  case ACTRES_PILLAGE:
  case ACTRES_CLEAN_POLLUTION:
  case ACTRES_CLEAN_FALLOUT:
  case ACTRES_FORTIFY:
  case ACTRES_SENTRY:
  case ACTRES_ROAD:
  case ACTRES_CONVERT:
  case ACTRES_BASE:
  case ACTRES_MINE:
  case ACTRES_IRRIGATE:
  case ACTRES_TRANSPORT_ALIGHT:
  case ACTRES_TRANSPORT_UNLOAD:
  case ACTRES_TRANSPORT_DISEMBARK:
  case ACTRES_TRANSPORT_BOARD:
  case ACTRES_TRANSPORT_EMBARK:
  case ACTRES_SPY_ATTACK:
  case ACTRES_NONE:
    /* No additional dice roll. */
    break;
  }

  /* The odds of the action not being stopped by its dice roll when the dice
   * isn't thrown is 100%. ACTION_ODDS_PCT_DICE_ROLL_NA is above 100% */
  return ACTION_ODDS_PCT_DICE_ROLL_NA;
}

/**********************************************************************//**
  Returns the odds of an action not failing its dice roll.
**************************************************************************/
int action_dice_roll_odds(const struct player *act_player,
                          const struct unit *act_unit,
                          const struct city *tgt_city,
                          const struct player *tgt_player,
                          const struct action *paction)
{
  int odds = action_dice_roll_initial_odds(paction);

  fc_assert_action_msg(odds >= 0 && odds <= 100,
                       odds = 100,
                       "Bad initial odds for action number %d."
                       " Does it roll the dice at all?",
                       paction->id);

  /* Let the Action_Odds_Pct effect modify the odds. The advantage of doing
   * it this way in stead of rolling twice is that Action_Odds_Pct can
   * increase the odds. */
  odds += ((odds
            * get_target_bonus_effects(NULL,
                                       act_player, tgt_player,
                                       tgt_city, NULL, NULL,
                                       act_unit, unit_type_get(act_unit),
                                       NULL, NULL, paction,
                                       EFT_ACTION_ODDS_PCT))
           / 100);

  /* Odds are between 0% and 100%. */
  return CLIP(0, odds, 100);
}

/**********************************************************************//**
  Will a player with the government gov be immune to the action act?
**************************************************************************/
bool action_immune_government(struct government *gov, action_id act)
{
  /* Always immune since its not enabled. Doesn't count. */
  if (action_enabler_list_size(action_enablers_for_action(act)) == 0) {
    return FALSE;
  }

  action_enabler_list_iterate(action_enablers_for_action(act), enabler) {
    if (requirement_fulfilled_by_government(gov, &(enabler->target_reqs))) {
      return FALSE;
    }
  } action_enabler_list_iterate_end;

  return TRUE;
}

/**********************************************************************//**
  Returns TRUE if the wanted action can be done to the target.
**************************************************************************/
static bool is_target_possible(const action_id wanted_action,
			       const struct player *actor_player,
			       const struct player *target_player,
			       const struct city *target_city,
			       const struct impr_type *target_building,
			       const struct tile *target_tile,
                               const struct unit *target_unit,
			       const struct unit_type *target_unittype,
			       const struct output_type *target_output,
			       const struct specialist *target_specialist)
{
  action_enabler_list_iterate(action_enablers_for_action(wanted_action),
                              enabler) {
    if (are_reqs_active(target_player, actor_player, target_city,
                        target_building, target_tile,
                        target_unit, target_unittype,
                        target_output, target_specialist, NULL,
                        &enabler->target_reqs, RPT_POSSIBLE)) {
      return TRUE;
    }
  } action_enabler_list_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  Returns TRUE if the wanted action can be done to the target city.
**************************************************************************/
bool is_action_possible_on_city(action_id act_id,
                                const struct player *actor_player,
                                const struct city* target_city)
{
  fc_assert_ret_val_msg(ATK_CITY == action_id_get_target_kind(act_id),
                        FALSE, "Action %s is against %s not cities",
                        action_id_rule_name(act_id),
                        action_target_kind_name(
                          action_id_get_target_kind(act_id)));

  return is_target_possible(act_id, actor_player,
                            city_owner(target_city), target_city, NULL,
                            city_tile(target_city), NULL, NULL,
                            NULL, NULL);
}

/**********************************************************************//**
  Returns TRUE if the wanted action (as far as the player knows) can be
  performed right now by the specified actor unit if an approriate target
  is provided.
**************************************************************************/
bool action_maybe_possible_actor_unit(const action_id act_id,
                                      const struct unit *actor_unit)
{
  const struct player *actor_player = unit_owner(actor_unit);
  const struct tile *actor_tile = unit_tile(actor_unit);
  const struct city *actor_city = tile_city(actor_tile);
  const struct unit_type *actor_unittype = unit_type_get(actor_unit);
  const struct action *paction = action_by_number(act_id);

  enum fc_tristate result;

  fc_assert_ret_val(actor_unit, FALSE);

  if (!utype_can_do_action(actor_unit->utype, act_id)) {
    /* The unit type can't perform the action. */
    return FALSE;
  }

  result = action_hard_reqs_actor(paction->result,
                                  actor_player, actor_city, NULL,
                                  actor_tile, actor_unit, actor_unittype,
                                  NULL, NULL, FALSE,
                                  game_city_by_number(actor_unit->homecity));

  if (result == TRI_NO) {
    /* The hard requirements aren't fulfilled. */
    return FALSE;
  }

  action_enabler_list_iterate(action_enablers_for_action(act_id),
                              enabler) {
    const enum fc_tristate current
        = mke_eval_reqs(actor_player,
                        actor_player, NULL, actor_city, NULL, actor_tile,
                        actor_unit, NULL, NULL,
                        &enabler->actor_reqs,
                        /* Needed since no player to evaluate DiplRel
                         * requirements against. */
                        RPT_POSSIBLE);

    if (current == TRI_YES
        || current == TRI_MAYBE) {
      /* The ruleset requirements may be fulfilled. */
      return TRUE;
    }
  } action_enabler_list_iterate_end;

  /* No action enabler allows this action. */
  return FALSE;
}

/**********************************************************************//**
  Returns TRUE if the specified action can't be done now but would have
  been legal if the unit had full movement.
**************************************************************************/
bool action_mp_full_makes_legal(const struct unit *actor,
                                const action_id act_id)
{
  fc_assert(action_id_exists(act_id) || act_id == ACTION_ANY);

  /* Check if full movement points may enable the specified action. */
  return !utype_may_act_move_frags(unit_type_get(actor),
                                   act_id,
                                   actor->moves_left)
      && utype_may_act_move_frags(unit_type_get(actor),
                                  act_id,
                                  unit_move_rate(actor));
}

/**********************************************************************//**
  Returns action auto performer rule slot number num so it can be filled.
**************************************************************************/
struct action_auto_perf *action_auto_perf_slot_number(const int num)
{
  fc_assert_ret_val(num >= 0, NULL);
  fc_assert_ret_val(num < MAX_NUM_ACTION_AUTO_PERFORMERS, NULL);

  return &auto_perfs[num];
}

/**********************************************************************//**
  Returns action auto performer rule number num.

  Used in action_auto_perf_iterate()

  WARNING: If the cause of the returned action performer rule is
  AAPC_COUNT it means that it is unused.
**************************************************************************/
const struct action_auto_perf *action_auto_perf_by_number(const int num)
{
  return action_auto_perf_slot_number(num);
}

/**********************************************************************//**
  Is there any action enablers of the given type not blocked by universals?
**************************************************************************/
bool action_univs_not_blocking(const struct action *paction,
                               struct universal *actor_uni,
                               struct universal *target_uni)
{
  action_enabler_list_iterate(action_enablers_for_action(paction->id),
                              enab) {
    if ((actor_uni == NULL
         || universal_fulfills_requirements(FALSE, &(enab->actor_reqs),
                                            actor_uni))
        && (target_uni == NULL
            || universal_fulfills_requirements(FALSE, &(enab->target_reqs),
                                               target_uni))) {
      return TRUE;
    }
  } action_enabler_list_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  Terminate an action list of the specified size.
  @param act_list the list to end
  @param size the number of elements to include in the list
**************************************************************************/
void action_list_end(action_id *act_list, int size)
{
  fc_assert_ret(size <= MAX_NUM_ACTIONS);

  if (size < MAX_NUM_ACTIONS) {
    /* An action list is terminated by ACTION_NONE */
    act_list[size] = ACTION_NONE;
  }
}

/**********************************************************************//**
  Add all actions with the specified result to the specified action list
  starting at the specified position.
  @param act_list the list to add the actions to
  @param position index in act_list that is updated as action are added
  @param result all actions with this result are added.
**************************************************************************/
void action_list_add_all_by_result(action_id *act_list,
                                   int *position,
                                   enum action_result result)
{
  action_iterate(act) {
    struct action *paction = action_by_number(act);
    if (paction->result == result) {
      /* Assume one result for each action. */
      fc_assert_ret(*position < MAX_NUM_ACTIONS);

      act_list[(*position)++] = paction->id;
    }
  } action_iterate_end;
}

/**********************************************************************//**
  Return ui_name ruleset variable name for the action.

  TODO: make actions generic and put ui_name in a field of the action.
**************************************************************************/
const char *action_ui_name_ruleset_var_name(int act)
{
  switch ((enum gen_action)act) {
  case ACTION_SPY_POISON:
    return "ui_name_poison_city";
  case ACTION_SPY_POISON_ESC:
    return "ui_name_poison_city_escape";
  case ACTION_SPY_SABOTAGE_UNIT:
    return "ui_name_sabotage_unit";
  case ACTION_SPY_SABOTAGE_UNIT_ESC:
    return "ui_name_sabotage_unit_escape";
  case ACTION_SPY_BRIBE_UNIT:
    return "ui_name_bribe_unit";
  case ACTION_SPY_SABOTAGE_CITY:
    return "ui_name_sabotage_city";
  case ACTION_SPY_SABOTAGE_CITY_ESC:
    return "ui_name_sabotage_city_escape";
  case ACTION_SPY_TARGETED_SABOTAGE_CITY:
    return "ui_name_targeted_sabotage_city";
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION:
    return "ui_name_sabotage_city_production";
  case ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC:
    return "ui_name_targeted_sabotage_city_escape";
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION_ESC:
    return "ui_name_sabotage_city_production_escape";
  case ACTION_SPY_INCITE_CITY:
    return "ui_name_incite_city";
  case ACTION_SPY_INCITE_CITY_ESC:
    return "ui_name_incite_city_escape";
  case ACTION_ESTABLISH_EMBASSY:
    return "ui_name_establish_embassy";
  case ACTION_ESTABLISH_EMBASSY_STAY:
    return "ui_name_establish_embassy_stay";
  case ACTION_SPY_STEAL_TECH:
    return "ui_name_steal_tech";
  case ACTION_SPY_STEAL_TECH_ESC:
    return "ui_name_steal_tech_escape";
  case ACTION_SPY_TARGETED_STEAL_TECH:
    return "ui_name_targeted_steal_tech";
  case ACTION_SPY_TARGETED_STEAL_TECH_ESC:
    return "ui_name_targeted_steal_tech_escape";
  case ACTION_SPY_INVESTIGATE_CITY:
    return "ui_name_investigate_city";
  case ACTION_INV_CITY_SPEND:
    return "ui_name_investigate_city_spend_unit";
  case ACTION_SPY_STEAL_GOLD:
    return "ui_name_steal_gold";
  case ACTION_SPY_STEAL_GOLD_ESC:
    return "ui_name_steal_gold_escape";
  case ACTION_SPY_SPREAD_PLAGUE:
    return "ui_name_spread_plague";
  case ACTION_STEAL_MAPS:
    return "ui_name_steal_maps";
  case ACTION_STEAL_MAPS_ESC:
    return "ui_name_steal_maps_escape";
  case ACTION_TRADE_ROUTE:
    return "ui_name_establish_trade_route";
  case ACTION_MARKETPLACE:
    return "ui_name_enter_marketplace";
  case ACTION_HELP_WONDER:
    return "ui_name_help_wonder";
  case ACTION_CAPTURE_UNITS:
    return "ui_name_capture_units";
  case ACTION_EXPEL_UNIT:
    return "ui_name_expel_unit";
  case ACTION_FOUND_CITY:
    return "ui_name_found_city";
  case ACTION_JOIN_CITY:
    return "ui_name_join_city";
  case ACTION_BOMBARD:
    return "ui_name_bombard";
  case ACTION_BOMBARD2:
    return "ui_name_bombard_2";
  case ACTION_BOMBARD3:
    return "ui_name_bombard_3";
  case ACTION_SPY_NUKE:
    return "ui_name_suitcase_nuke";
  case ACTION_SPY_NUKE_ESC:
    return "ui_name_suitcase_nuke_escape";
  case ACTION_NUKE:
    return "ui_name_explode_nuclear";
  case ACTION_NUKE_CITY:
    return "ui_name_nuke_city";
  case ACTION_NUKE_UNITS:
    return "ui_name_nuke_units";
  case ACTION_DESTROY_CITY:
    return "ui_name_destroy_city";
  case ACTION_RECYCLE_UNIT:
    return "ui_name_recycle_unit";
  case ACTION_DISBAND_UNIT:
    return "ui_name_disband_unit";
  case ACTION_HOME_CITY:
    return "ui_name_home_city";
  case ACTION_UPGRADE_UNIT:
    return "ui_name_upgrade_unit";
  case ACTION_PARADROP:
    return "ui_name_paradrop_unit";
  case ACTION_AIRLIFT:
    return "ui_name_airlift_unit";
  case ACTION_ATTACK:
    return "ui_name_attack";
  case ACTION_SUICIDE_ATTACK:
    return "ui_name_suicide_attack";
  case ACTION_STRIKE_BUILDING:
    return "ui_name_surgical_strike_building";
  case ACTION_STRIKE_PRODUCTION:
    return "ui_name_surgical_strike_production";
  case ACTION_CONQUER_CITY:
    return "ui_name_conquer_city";
  case ACTION_CONQUER_CITY2:
    return "ui_name_conquer_city_2";
  case ACTION_HEAL_UNIT:
    return "ui_name_heal_unit";
  case ACTION_TRANSFORM_TERRAIN:
    return "ui_name_transform_terrain";
  case ACTION_CULTIVATE:
    return "ui_name_cultivate";
  case ACTION_PLANT:
    return "ui_name_plant";
  case ACTION_PILLAGE:
    return "ui_name_pillage";
  case ACTION_CLEAN_POLLUTION:
    return "ui_name_clean_pollution";
  case ACTION_CLEAN_FALLOUT:
    return "ui_name_clean_fallout";
  case ACTION_FORTIFY:
    return "ui_name_fortify";
  case ACTION_SENTRY:
    return "ui_name_sentry";
  case ACTION_ROAD:
    return "ui_name_road";
  case ACTION_CONVERT:
    return "ui_name_convert_unit";
  case ACTION_BASE:
    return "ui_name_build_base";
  case ACTION_MINE:
    return "ui_name_build_mine";
  case ACTION_IRRIGATE:
    return "ui_name_irrigate";
  case ACTION_TRANSPORT_ALIGHT:
    return "ui_name_transport_alight";
  case ACTION_TRANSPORT_BOARD:
    return "ui_name_transport_board";
  case ACTION_TRANSPORT_EMBARK:
    return "ui_name_transport_embark";
  case ACTION_TRANSPORT_UNLOAD:
    return "ui_name_transport_unload";
  case ACTION_TRANSPORT_DISEMBARK1:
    return "ui_name_transport_disembark";
  case ACTION_TRANSPORT_DISEMBARK2:
    return "ui_name_transport_disembark_2";
  case ACTION_SPY_ATTACK:
    return "ui_name_spy_attack";
  case ACTION_USER_ACTION1:
    return "ui_name_user_action_1";
  case ACTION_USER_ACTION2:
    return "ui_name_user_action_2";
  case ACTION_USER_ACTION3:
    return "ui_name_user_action_3";
  case ACTION_COUNT:
    break;
  }

  fc_assert(act >= 0 && act < ACTION_COUNT);
  return NULL;
}

/**********************************************************************//**
  Return default ui_name for the action
**************************************************************************/
const char *action_ui_name_default(int act)
{
  switch (act) {
  case ACTION_SPY_POISON:
    /* TRANS: _Poison City (3% chance of success). */
    return N_("%sPoison City%s");
  case ACTION_SPY_POISON_ESC:
    /* TRANS: _Poison City and Escape (3% chance of success). */
    return N_("%sPoison City and Escape%s");
  case ACTION_SPY_SABOTAGE_UNIT:
    /* TRANS: S_abotage Enemy Unit (3% chance of success). */
    return N_("S%sabotage Enemy Unit%s");
  case ACTION_SPY_SABOTAGE_UNIT_ESC:
    /* TRANS: S_abotage Enemy Unit and Escape (3% chance of success). */
    return N_("S%sabotage Enemy Unit and Escape%s");
  case ACTION_SPY_BRIBE_UNIT:
    /* TRANS: Bribe Enemy _Unit (3% chance of success). */
    return N_("Bribe Enemy %sUnit%s");
  case ACTION_SPY_SABOTAGE_CITY:
    /* TRANS: _Sabotage City (3% chance of success). */
    return N_("%sSabotage City%s");
  case ACTION_SPY_SABOTAGE_CITY_ESC:
    /* TRANS: _Sabotage City and Escape (3% chance of success). */
    return N_("%sSabotage City and Escape%s");
  case ACTION_SPY_TARGETED_SABOTAGE_CITY:
    /* TRANS: Industria_l Sabotage (3% chance of success). */
    return N_("Industria%sl Sabotage%s");
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION:
    /* TRANS: Industria_l Sabotage Production (3% chance of success). */
    return N_("Industria%sl Sabotage Production%s");
  case ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC:
    /* TRANS: Industria_l Sabotage and Escape (3% chance of success). */
    return N_("Industria%sl Sabotage and Escape%s");
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION_ESC:
    /* TRANS: Industria_l Sabotage Production and Escape (3% chance of success). */
    return N_("Industria%sl Sabotage Production and Escape%s");
  case ACTION_SPY_INCITE_CITY:
    /* TRANS: Incite a Re_volt (3% chance of success). */
    return N_("Incite a Re%svolt%s");
  case ACTION_SPY_INCITE_CITY_ESC:
    /* TRANS: Incite a Re_volt and Escape (3% chance of success). */
    return N_("Incite a Re%svolt and Escape%s");
  case ACTION_ESTABLISH_EMBASSY:
    /* TRANS: Establish _Embassy (100% chance of success). */
    return N_("Establish %sEmbassy%s");
  case ACTION_ESTABLISH_EMBASSY_STAY:
    /* TRANS: Becom_e Ambassador (100% chance of success). */
    return N_("Becom%se Ambassador%s");
  case ACTION_SPY_STEAL_TECH:
    /* TRANS: Steal _Technology (3% chance of success). */
    return N_("Steal %sTechnology%s");
  case ACTION_SPY_STEAL_TECH_ESC:
    /* TRANS: Steal _Technology and Escape (3% chance of success). */
    return N_("Steal %sTechnology and Escape%s");
  case ACTION_SPY_TARGETED_STEAL_TECH:
    /* TRANS: In_dustrial Espionage (3% chance of success). */
    return N_("In%sdustrial Espionage%s");
  case ACTION_SPY_TARGETED_STEAL_TECH_ESC:
    /* TRANS: In_dustrial Espionage and Escape (3% chance of success). */
    return N_("In%sdustrial Espionage and Escape%s");
  case ACTION_SPY_INVESTIGATE_CITY:
    /* TRANS: _Investigate City (100% chance of success). */
    return N_("%sInvestigate City%s");
  case ACTION_INV_CITY_SPEND:
    /* TRANS: _Investigate City (spends the unit) (100% chance of
     * success). */
    return N_("%sInvestigate City (spends the unit)%s");
  case ACTION_SPY_STEAL_GOLD:
    /* TRANS: Steal _Gold (100% chance of success). */
    return N_("Steal %sGold%s");
  case ACTION_SPY_STEAL_GOLD_ESC:
    /* TRANS: Steal _Gold and Escape (100% chance of success). */
    return N_("Steal %sGold and Escape%s");
  case ACTION_SPY_SPREAD_PLAGUE:
    /* TRANS: Spread _Plague (100% chance of success). */
    return N_("Spread %sPlague%s");
  case ACTION_STEAL_MAPS:
    /* TRANS: Steal _Maps (100% chance of success). */
    return N_("Steal %sMaps%s");
  case ACTION_STEAL_MAPS_ESC:
    /* TRANS: Steal _Maps and Escape (100% chance of success). */
    return N_("Steal %sMaps and Escape%s");
  case ACTION_TRADE_ROUTE:
    /* TRANS: Establish Trade _Route (100% chance of success). */
    return N_("Establish Trade %sRoute%s");
  case ACTION_MARKETPLACE:
    /* TRANS: Enter _Marketplace (100% chance of success). */
    return N_("Enter %sMarketplace%s");
  case ACTION_HELP_WONDER:
    /* TRANS: Help _build Wonder (100% chance of success). */
    return N_("Help %sbuild Wonder%s");
  case ACTION_CAPTURE_UNITS:
    /* TRANS: _Capture Units (100% chance of success). */
    return N_("%sCapture Units%s");
  case ACTION_EXPEL_UNIT:
    /* TRANS: _Expel Unit (100% chance of success). */
    return N_("%sExpel Unit%s");
  case ACTION_FOUND_CITY:
    /* TRANS: _Found City (100% chance of success). */
    return N_("%sFound City%s");
  case ACTION_JOIN_CITY:
    /* TRANS: _Join City (100% chance of success). */
    return N_("%sJoin City%s");
  case ACTION_BOMBARD:
    /* TRANS: B_ombard (100% chance of success). */
    return N_("B%sombard%s");
  case ACTION_BOMBARD2:
    /* TRANS: B_ombard 2 (100% chance of success). */
    return N_("B%sombard 2%s");
  case ACTION_BOMBARD3:
    /* TRANS: B_ombard 3 (100% chance of success). */
    return N_("B%sombard 3%s");
  case ACTION_SPY_NUKE:
    /* TRANS: Suitcase _Nuke (100% chance of success). */
    return N_("Suitcase %sNuke%s");
  case ACTION_SPY_NUKE_ESC:
    /* TRANS: Suitcase _Nuke and Escape (100% chance of success). */
    return N_("Suitcase %sNuke and Escape%s");
  case ACTION_NUKE:
    /* TRANS: Explode _Nuclear (100% chance of success). */
    return N_("Explode %sNuclear%s");
  case ACTION_NUKE_CITY:
    /* TRANS: _Nuke City (100% chance of success). */
    return N_("%sNuke City%s");
  case ACTION_NUKE_UNITS:
    /* TRANS: _Nuke Units (100% chance of success). */
    return N_("%sNuke Units%s");
  case ACTION_DESTROY_CITY:
    /* TRANS: Destroy _City (100% chance of success). */
    return N_("Destroy %sCity%s");
  case ACTION_RECYCLE_UNIT:
    /* TRANS: Rec_ycle Unit (100% chance of success). */
    return N_("Rec%sycle Unit%s");
  case ACTION_DISBAND_UNIT:
    /* TRANS: _You're Fired (100% chance of success). */
    return N_("%sYou're Fired%s");
  case ACTION_HOME_CITY:
    /* TRANS: Set _Home City (100% chance of success). */
    return N_("Set %sHome City%s");
  case ACTION_UPGRADE_UNIT:
    /* TRANS: _Upgrade Unit (100% chance of success). */
    return N_("%sUpgrade Unit%s");
  case ACTION_PARADROP:
    /* TRANS: Drop _Paratrooper (100% chance of success). */
    return N_("Drop %sParatrooper%s");
  case ACTION_AIRLIFT:
    /* TRANS: _Airlift to City (100% chance of success). */
    return N_("%sAirlift to City%s");
  case ACTION_ATTACK:
    /* TRANS: _Attack (100% chance of success). */
    return N_("%sAttack%s");
  case ACTION_SUICIDE_ATTACK:
    /* TRANS: _Suicide Attack (100% chance of success). */
    return N_("%sSuicide Attack%s");
  case ACTION_STRIKE_BUILDING:
    /* TRANS: Surgical Str_ike Building (100% chance of success). */
    return N_("Surgical Str%sike Building%s");
  case ACTION_STRIKE_PRODUCTION:
    /* TRANS: Surgical Str_ike Production (100% chance of success). */
    return N_("Surgical Str%sike Production%s");
  case ACTION_CONQUER_CITY:
    /* TRANS: _Conquer City (100% chance of success). */
    return N_("%sConquer City%s");
  case ACTION_CONQUER_CITY2:
    /* TRANS: _Conquer City 2 (100% chance of success). */
    return N_("%sConquer City 2%s");
  case ACTION_HEAL_UNIT:
    /* TRANS: Heal _Unit (3% chance of success). */
    return N_("Heal %sUnit%s");
  case ACTION_TRANSFORM_TERRAIN:
    /* TRANS: _Transform Terrain (3% chance of success). */
    return N_("%sTransform Terrain%s");
  case ACTION_CULTIVATE:
    /* TRANS: Transform by _Cultivating (3% chance of success). */
    return N_("Transform by %sCultivating%s");
  case ACTION_PLANT:
    /* TRANS: Transform by _Planting (3% chance of success). */
    return N_("Transform by %sPlanting%s");
  case ACTION_PILLAGE:
    /* TRANS: Pilla_ge (100% chance of success). */
    return N_("Pilla%sge%s");
  case ACTION_CLEAN_POLLUTION:
    /* TRANS: Clean _Pollution (100% chance of success). */
    return N_("Clean %sPollution%s");
  case ACTION_CLEAN_FALLOUT:
    /* TRANS: Clean _Fallout (100% chance of success). */
    return N_("Clean %sFallout%s");
  case ACTION_FORTIFY:
    /* TRANS: _Fortify (100% chance of success). */
    return N_("%sFortify%s");
  case ACTION_SENTRY:
    /* TRANS: _Sentry (100% chance of success). */
    return N_("%sSentry%s");
  case ACTION_ROAD:
    /* TRANS: Build _Road (100% chance of success). */
    return N_("Build %sRoad%s");
  case ACTION_CONVERT:
    /* TRANS: _Convert Unit (100% chance of success). */
    return N_("%sConvert Unit%s");
  case ACTION_BASE:
    /* TRANS: _Build Base (100% chance of success). */
    return N_("%sBuild Base%s");
  case ACTION_MINE:
    /* TRANS: Build _Mine (100% chance of success). */
    return N_("Build %sMine%s");
  case ACTION_IRRIGATE:
    /* TRANS: Build _Irrigation (100% chance of success). */
    return N_("Build %sIrrigation%s");
  case ACTION_TRANSPORT_ALIGHT:
    /* TRANS: _Alight (100% chance of success). */
    return N_("%sAlight%s");
  case ACTION_TRANSPORT_BOARD:
    /* TRANS: _Board (100% chance of success). */
    return N_("%sBoard%s");
  case ACTION_TRANSPORT_EMBARK:
    /* TRANS: _Embark (100% chance of success). */
    return N_("%sEmbark%s");
  case ACTION_TRANSPORT_UNLOAD:
    /* TRANS: _Unload (100% chance of success). */
    return N_("%sUnload%s");
  case ACTION_TRANSPORT_DISEMBARK1:
    /* TRANS: _Disembark (100% chance of success). */
    return N_("%sDisembark%s");
  case ACTION_TRANSPORT_DISEMBARK2:
    /* TRANS: _Disembark 2 (100% chance of success). */
    return N_("%sDisembark 2%s");
  case ACTION_SPY_ATTACK:
    /* TRANS: _Eliminate Diplomat (100% chance of success). */
    return N_("%sEliminate Diplomat%s");
  case ACTION_USER_ACTION1:
    /* TRANS: _User Action 1 (100% chance of success). */
    return N_("%sUser Action 1%s");
  case ACTION_USER_ACTION2:
    /* TRANS: _User Action 2 (100% chance of success). */
    return N_("%sUser Action 2%s");
  case ACTION_USER_ACTION3:
    /* TRANS: _User Action 3 (100% chance of success). */
    return N_("%sUser Action 3%s");
  }

  return NULL;
}

/**********************************************************************//**
  Return min range ruleset variable name for the action or NULL if min
  range can't be set in the ruleset.

  TODO: make actions generic and put min_range in a field of the action.
**************************************************************************/
const char *action_min_range_ruleset_var_name(int act)
{
  switch ((enum gen_action)act) {
  case ACTION_SPY_POISON:
  case ACTION_SPY_POISON_ESC:
  case ACTION_SPY_SABOTAGE_UNIT:
  case ACTION_SPY_SABOTAGE_UNIT_ESC:
  case ACTION_SPY_BRIBE_UNIT:
  case ACTION_SPY_SABOTAGE_CITY:
  case ACTION_SPY_SABOTAGE_CITY_ESC:
  case ACTION_SPY_TARGETED_SABOTAGE_CITY:
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION:
  case ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC:
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION_ESC:
  case ACTION_SPY_INCITE_CITY:
  case ACTION_SPY_INCITE_CITY_ESC:
  case ACTION_ESTABLISH_EMBASSY:
  case ACTION_ESTABLISH_EMBASSY_STAY:
  case ACTION_SPY_STEAL_TECH:
  case ACTION_SPY_STEAL_TECH_ESC:
  case ACTION_SPY_TARGETED_STEAL_TECH:
  case ACTION_SPY_TARGETED_STEAL_TECH_ESC:
  case ACTION_SPY_INVESTIGATE_CITY:
  case ACTION_INV_CITY_SPEND:
  case ACTION_SPY_STEAL_GOLD:
  case ACTION_SPY_STEAL_GOLD_ESC:
  case ACTION_SPY_SPREAD_PLAGUE:
  case ACTION_STEAL_MAPS:
  case ACTION_STEAL_MAPS_ESC:
  case ACTION_TRADE_ROUTE:
  case ACTION_MARKETPLACE:
  case ACTION_HELP_WONDER:
  case ACTION_CAPTURE_UNITS:
  case ACTION_EXPEL_UNIT:
  case ACTION_FOUND_CITY:
  case ACTION_JOIN_CITY:
  case ACTION_SPY_NUKE:
  case ACTION_SPY_NUKE_ESC:
  case ACTION_NUKE_CITY:
  case ACTION_NUKE_UNITS:
  case ACTION_DESTROY_CITY:
  case ACTION_RECYCLE_UNIT:
  case ACTION_DISBAND_UNIT:
  case ACTION_HOME_CITY:
  case ACTION_UPGRADE_UNIT:
  case ACTION_PARADROP:
  case ACTION_AIRLIFT:
  case ACTION_ATTACK:
  case ACTION_SUICIDE_ATTACK:
  case ACTION_STRIKE_BUILDING:
  case ACTION_STRIKE_PRODUCTION:
  case ACTION_CONQUER_CITY:
  case ACTION_CONQUER_CITY2:
  case ACTION_HEAL_UNIT:
  case ACTION_TRANSFORM_TERRAIN:
  case ACTION_CULTIVATE:
  case ACTION_PLANT:
  case ACTION_PILLAGE:
  case ACTION_CLEAN_POLLUTION:
  case ACTION_CLEAN_FALLOUT:
  case ACTION_FORTIFY:
  case ACTION_SENTRY:
  case ACTION_ROAD:
  case ACTION_CONVERT:
  case ACTION_BASE:
  case ACTION_MINE:
  case ACTION_IRRIGATE:
  case ACTION_TRANSPORT_ALIGHT:
  case ACTION_TRANSPORT_BOARD:
  case ACTION_TRANSPORT_EMBARK:
  case ACTION_TRANSPORT_UNLOAD:
  case ACTION_TRANSPORT_DISEMBARK1:
  case ACTION_TRANSPORT_DISEMBARK2:
  case ACTION_BOMBARD:
  case ACTION_BOMBARD2:
  case ACTION_BOMBARD3:
  case ACTION_NUKE:
  case ACTION_SPY_ATTACK:
    /* Min range is not ruleset changeable */
    return NULL;
  case ACTION_USER_ACTION1:
    return "user_action_1_min_range";
  case ACTION_USER_ACTION2:
    return "user_action_2_min_range";
  case ACTION_USER_ACTION3:
    return "user_action_3_min_range";
  case ACTION_COUNT:
    break;
  }

  fc_assert(act >= 0 && act < ACTION_COUNT);
  return NULL;
}

/**********************************************************************//**
  Return default min range for the action if it is ruleset settable.
**************************************************************************/
int action_min_range_default(int act)
{
  switch ((enum gen_action)act) {
  case ACTION_SPY_POISON:
  case ACTION_SPY_POISON_ESC:
  case ACTION_SPY_SABOTAGE_UNIT:
  case ACTION_SPY_SABOTAGE_UNIT_ESC:
  case ACTION_SPY_BRIBE_UNIT:
  case ACTION_SPY_SABOTAGE_CITY:
  case ACTION_SPY_SABOTAGE_CITY_ESC:
  case ACTION_SPY_TARGETED_SABOTAGE_CITY:
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION:
  case ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC:
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION_ESC:
  case ACTION_SPY_INCITE_CITY:
  case ACTION_SPY_INCITE_CITY_ESC:
  case ACTION_ESTABLISH_EMBASSY:
  case ACTION_ESTABLISH_EMBASSY_STAY:
  case ACTION_SPY_STEAL_TECH:
  case ACTION_SPY_STEAL_TECH_ESC:
  case ACTION_SPY_TARGETED_STEAL_TECH:
  case ACTION_SPY_TARGETED_STEAL_TECH_ESC:
  case ACTION_SPY_INVESTIGATE_CITY:
  case ACTION_INV_CITY_SPEND:
  case ACTION_SPY_STEAL_GOLD:
  case ACTION_SPY_STEAL_GOLD_ESC:
  case ACTION_SPY_SPREAD_PLAGUE:
  case ACTION_STEAL_MAPS:
  case ACTION_STEAL_MAPS_ESC:
  case ACTION_TRADE_ROUTE:
  case ACTION_MARKETPLACE:
  case ACTION_HELP_WONDER:
  case ACTION_CAPTURE_UNITS:
  case ACTION_EXPEL_UNIT:
  case ACTION_FOUND_CITY:
  case ACTION_JOIN_CITY:
  case ACTION_SPY_NUKE:
  case ACTION_SPY_NUKE_ESC:
  case ACTION_NUKE_CITY:
  case ACTION_NUKE_UNITS:
  case ACTION_DESTROY_CITY:
  case ACTION_RECYCLE_UNIT:
  case ACTION_DISBAND_UNIT:
  case ACTION_HOME_CITY:
  case ACTION_UPGRADE_UNIT:
  case ACTION_PARADROP:
  case ACTION_AIRLIFT:
  case ACTION_ATTACK:
  case ACTION_SUICIDE_ATTACK:
  case ACTION_STRIKE_BUILDING:
  case ACTION_STRIKE_PRODUCTION:
  case ACTION_CONQUER_CITY:
  case ACTION_CONQUER_CITY2:
  case ACTION_HEAL_UNIT:
  case ACTION_TRANSFORM_TERRAIN:
  case ACTION_CULTIVATE:
  case ACTION_PLANT:
  case ACTION_PILLAGE:
  case ACTION_CLEAN_POLLUTION:
  case ACTION_CLEAN_FALLOUT:
  case ACTION_FORTIFY:
  case ACTION_SENTRY:
  case ACTION_ROAD:
  case ACTION_CONVERT:
  case ACTION_BASE:
  case ACTION_MINE:
  case ACTION_IRRIGATE:
  case ACTION_TRANSPORT_ALIGHT:
  case ACTION_TRANSPORT_BOARD:
  case ACTION_TRANSPORT_EMBARK:
  case ACTION_TRANSPORT_UNLOAD:
  case ACTION_TRANSPORT_DISEMBARK1:
  case ACTION_TRANSPORT_DISEMBARK2:
  case ACTION_BOMBARD:
  case ACTION_BOMBARD2:
  case ACTION_BOMBARD3:
  case ACTION_NUKE:
  case ACTION_SPY_ATTACK:
    /* Non ruleset defined action min range not supported here */
    fc_assert_msg(FALSE, "Probably wrong value.");
    return RS_DEFAULT_ACTION_MIN_RANGE;
  case ACTION_USER_ACTION1:
  case ACTION_USER_ACTION2:
  case ACTION_USER_ACTION3:
    return RS_DEFAULT_ACTION_MIN_RANGE;
  case ACTION_COUNT:
    break;
  }

  fc_assert(act >= 0 && act < ACTION_COUNT);
  return 0;
}

/**********************************************************************//**
  Return max range ruleset variable name for the action or NULL if max
  range can't be set in the ruleset.

  TODO: make actions generic and put max_range in a field of the action.
**************************************************************************/
const char *action_max_range_ruleset_var_name(int act)
{
  switch ((enum gen_action)act) {
  case ACTION_SPY_POISON:
  case ACTION_SPY_POISON_ESC:
  case ACTION_SPY_SABOTAGE_UNIT:
  case ACTION_SPY_SABOTAGE_UNIT_ESC:
  case ACTION_SPY_BRIBE_UNIT:
  case ACTION_SPY_SABOTAGE_CITY:
  case ACTION_SPY_SABOTAGE_CITY_ESC:
  case ACTION_SPY_TARGETED_SABOTAGE_CITY:
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION:
  case ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC:
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION_ESC:
  case ACTION_SPY_INCITE_CITY:
  case ACTION_SPY_INCITE_CITY_ESC:
  case ACTION_ESTABLISH_EMBASSY:
  case ACTION_ESTABLISH_EMBASSY_STAY:
  case ACTION_SPY_STEAL_TECH:
  case ACTION_SPY_STEAL_TECH_ESC:
  case ACTION_SPY_TARGETED_STEAL_TECH:
  case ACTION_SPY_TARGETED_STEAL_TECH_ESC:
  case ACTION_SPY_INVESTIGATE_CITY:
  case ACTION_INV_CITY_SPEND:
  case ACTION_SPY_STEAL_GOLD:
  case ACTION_SPY_STEAL_GOLD_ESC:
  case ACTION_SPY_SPREAD_PLAGUE:
  case ACTION_STEAL_MAPS:
  case ACTION_STEAL_MAPS_ESC:
  case ACTION_TRADE_ROUTE:
  case ACTION_MARKETPLACE:
  case ACTION_CAPTURE_UNITS:
  case ACTION_EXPEL_UNIT:
  case ACTION_FOUND_CITY:
  case ACTION_JOIN_CITY:
  case ACTION_SPY_NUKE:
  case ACTION_SPY_NUKE_ESC:
  case ACTION_DESTROY_CITY:
  case ACTION_DISBAND_UNIT:
  case ACTION_HOME_CITY:
  case ACTION_UPGRADE_UNIT:
  case ACTION_PARADROP:
  case ACTION_ATTACK:
  case ACTION_SUICIDE_ATTACK:
  case ACTION_STRIKE_BUILDING:
  case ACTION_STRIKE_PRODUCTION:
  case ACTION_CONQUER_CITY:
  case ACTION_CONQUER_CITY2:
  case ACTION_HEAL_UNIT:
  case ACTION_TRANSFORM_TERRAIN:
  case ACTION_CULTIVATE:
  case ACTION_PLANT:
  case ACTION_PILLAGE:
  case ACTION_CLEAN_POLLUTION:
  case ACTION_CLEAN_FALLOUT:
  case ACTION_FORTIFY:
  case ACTION_SENTRY:
  case ACTION_ROAD:
  case ACTION_CONVERT:
  case ACTION_BASE:
  case ACTION_MINE:
  case ACTION_IRRIGATE:
  case ACTION_TRANSPORT_ALIGHT:
  case ACTION_TRANSPORT_BOARD:
  case ACTION_TRANSPORT_EMBARK:
  case ACTION_TRANSPORT_UNLOAD:
  case ACTION_TRANSPORT_DISEMBARK1:
  case ACTION_TRANSPORT_DISEMBARK2:
  case ACTION_SPY_ATTACK:
    /* Max range is not ruleset changeable */
    return NULL;
  case ACTION_HELP_WONDER:
    return "help_wonder_max_range";
  case ACTION_RECYCLE_UNIT:
    return "recycle_unit_max_range";
  case ACTION_BOMBARD:
    return "bombard_max_range";
  case ACTION_BOMBARD2:
    return "bombard_2_max_range";
  case ACTION_BOMBARD3:
    return "bombard_3_max_range";
  case ACTION_NUKE:
    return "explode_nuclear_max_range";
  case ACTION_NUKE_CITY:
    return "nuke_city_max_range";
  case ACTION_NUKE_UNITS:
    return "nuke_units_max_range";
  case ACTION_AIRLIFT:
      return "airlift_max_range";
  case ACTION_USER_ACTION1:
    return "user_action_1_max_range";
  case ACTION_USER_ACTION2:
    return "user_action_2_max_range";
  case ACTION_USER_ACTION3:
    return "user_action_3_max_range";
  case ACTION_COUNT:
    break;
  }

  fc_assert(act >= 0 && act < ACTION_COUNT);
  return NULL;
}

/**********************************************************************//**
  Return default max range for the action if it is ruleset settable.
**************************************************************************/
int action_max_range_default(int act)
{
  switch ((enum gen_action)act) {
  case ACTION_SPY_POISON:
  case ACTION_SPY_POISON_ESC:
  case ACTION_SPY_SABOTAGE_UNIT:
  case ACTION_SPY_SABOTAGE_UNIT_ESC:
  case ACTION_SPY_BRIBE_UNIT:
  case ACTION_SPY_SABOTAGE_CITY:
  case ACTION_SPY_SABOTAGE_CITY_ESC:
  case ACTION_SPY_TARGETED_SABOTAGE_CITY:
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION:
  case ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC:
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION_ESC:
  case ACTION_SPY_INCITE_CITY:
  case ACTION_SPY_INCITE_CITY_ESC:
  case ACTION_ESTABLISH_EMBASSY:
  case ACTION_ESTABLISH_EMBASSY_STAY:
  case ACTION_SPY_STEAL_TECH:
  case ACTION_SPY_STEAL_TECH_ESC:
  case ACTION_SPY_TARGETED_STEAL_TECH:
  case ACTION_SPY_TARGETED_STEAL_TECH_ESC:
  case ACTION_SPY_INVESTIGATE_CITY:
  case ACTION_INV_CITY_SPEND:
  case ACTION_SPY_STEAL_GOLD:
  case ACTION_SPY_STEAL_GOLD_ESC:
  case ACTION_STEAL_MAPS:
  case ACTION_STEAL_MAPS_ESC:
  case ACTION_SPY_SPREAD_PLAGUE:
  case ACTION_TRADE_ROUTE:
  case ACTION_MARKETPLACE:
  case ACTION_CAPTURE_UNITS:
  case ACTION_EXPEL_UNIT:
  case ACTION_FOUND_CITY:
  case ACTION_JOIN_CITY:
  case ACTION_SPY_NUKE:
  case ACTION_SPY_NUKE_ESC:
  case ACTION_DESTROY_CITY:
  case ACTION_DISBAND_UNIT:
  case ACTION_HOME_CITY:
  case ACTION_UPGRADE_UNIT:
  case ACTION_PARADROP:
  case ACTION_ATTACK:
  case ACTION_SUICIDE_ATTACK:
  case ACTION_STRIKE_BUILDING:
  case ACTION_STRIKE_PRODUCTION:
  case ACTION_CONQUER_CITY:
  case ACTION_CONQUER_CITY2:
  case ACTION_HEAL_UNIT:
  case ACTION_TRANSFORM_TERRAIN:
  case ACTION_CULTIVATE:
  case ACTION_PLANT:
  case ACTION_PILLAGE:
  case ACTION_CLEAN_POLLUTION:
  case ACTION_CLEAN_FALLOUT:
  case ACTION_FORTIFY:
  case ACTION_SENTRY:
  case ACTION_ROAD:
  case ACTION_CONVERT:
  case ACTION_BASE:
  case ACTION_MINE:
  case ACTION_IRRIGATE:
  case ACTION_TRANSPORT_ALIGHT:
  case ACTION_TRANSPORT_BOARD:
  case ACTION_TRANSPORT_EMBARK:
  case ACTION_TRANSPORT_UNLOAD:
  case ACTION_TRANSPORT_DISEMBARK1:
  case ACTION_TRANSPORT_DISEMBARK2:
  case ACTION_SPY_ATTACK:
    /* Non ruleset defined action max range not supported here */
    fc_assert_msg(FALSE, "Probably wrong value.");
    return RS_DEFAULT_ACTION_MAX_RANGE;
  case ACTION_HELP_WONDER:
  case ACTION_RECYCLE_UNIT:
    return RS_DEFAULT_ACTION_MAX_RANGE;
  case ACTION_BOMBARD:
  case ACTION_BOMBARD2:
  case ACTION_BOMBARD3:
    return RS_DEFAULT_ACTION_MAX_RANGE;
  case ACTION_NUKE:
    return RS_DEFAULT_EXPLODE_NUCLEAR_MAX_RANGE;
  case ACTION_NUKE_CITY:
  case ACTION_NUKE_UNITS:
    return RS_DEFAULT_ACTION_MAX_RANGE;
  case ACTION_AIRLIFT:
    return ACTION_DISTANCE_UNLIMITED;
  case ACTION_USER_ACTION1:
  case ACTION_USER_ACTION2:
  case ACTION_USER_ACTION3:
    return RS_DEFAULT_ACTION_MAX_RANGE;
  case ACTION_COUNT:
    break;
  }

  fc_assert(act >= 0 && act < ACTION_COUNT);
  return 0;
}

/**********************************************************************//**
  Return target kind ruleset variable name for the action or NULL if min
  range can't be set in the ruleset.

  TODO: make actions generic and put target_kind in a field of the action.
**************************************************************************/
const char *action_target_kind_ruleset_var_name(int act)
{
  switch ((enum gen_action)act) {
  case ACTION_SPY_POISON:
  case ACTION_SPY_POISON_ESC:
  case ACTION_SPY_SABOTAGE_UNIT:
  case ACTION_SPY_SABOTAGE_UNIT_ESC:
  case ACTION_SPY_BRIBE_UNIT:
  case ACTION_SPY_SABOTAGE_CITY:
  case ACTION_SPY_SABOTAGE_CITY_ESC:
  case ACTION_SPY_TARGETED_SABOTAGE_CITY:
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION:
  case ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC:
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION_ESC:
  case ACTION_SPY_INCITE_CITY:
  case ACTION_SPY_INCITE_CITY_ESC:
  case ACTION_ESTABLISH_EMBASSY:
  case ACTION_ESTABLISH_EMBASSY_STAY:
  case ACTION_SPY_STEAL_TECH:
  case ACTION_SPY_STEAL_TECH_ESC:
  case ACTION_SPY_TARGETED_STEAL_TECH:
  case ACTION_SPY_TARGETED_STEAL_TECH_ESC:
  case ACTION_SPY_INVESTIGATE_CITY:
  case ACTION_INV_CITY_SPEND:
  case ACTION_SPY_STEAL_GOLD:
  case ACTION_SPY_STEAL_GOLD_ESC:
  case ACTION_SPY_SPREAD_PLAGUE:
  case ACTION_STEAL_MAPS:
  case ACTION_STEAL_MAPS_ESC:
  case ACTION_TRADE_ROUTE:
  case ACTION_MARKETPLACE:
  case ACTION_HELP_WONDER:
  case ACTION_CAPTURE_UNITS:
  case ACTION_EXPEL_UNIT:
  case ACTION_FOUND_CITY:
  case ACTION_JOIN_CITY:
  case ACTION_SPY_NUKE:
  case ACTION_SPY_NUKE_ESC:
  case ACTION_NUKE_CITY:
  case ACTION_NUKE_UNITS:
  case ACTION_DESTROY_CITY:
  case ACTION_RECYCLE_UNIT:
  case ACTION_DISBAND_UNIT:
  case ACTION_HOME_CITY:
  case ACTION_UPGRADE_UNIT:
  case ACTION_PARADROP:
  case ACTION_AIRLIFT:
  case ACTION_ATTACK:
  case ACTION_SUICIDE_ATTACK:
  case ACTION_STRIKE_BUILDING:
  case ACTION_STRIKE_PRODUCTION:
  case ACTION_CONQUER_CITY:
  case ACTION_CONQUER_CITY2:
  case ACTION_HEAL_UNIT:
  case ACTION_TRANSFORM_TERRAIN:
  case ACTION_CULTIVATE:
  case ACTION_PLANT:
  case ACTION_PILLAGE:
  case ACTION_CLEAN_POLLUTION:
  case ACTION_CLEAN_FALLOUT:
  case ACTION_FORTIFY:
  case ACTION_SENTRY:
  case ACTION_ROAD:
  case ACTION_CONVERT:
  case ACTION_BASE:
  case ACTION_MINE:
  case ACTION_IRRIGATE:
  case ACTION_TRANSPORT_ALIGHT:
  case ACTION_TRANSPORT_BOARD:
  case ACTION_TRANSPORT_EMBARK:
  case ACTION_TRANSPORT_UNLOAD:
  case ACTION_TRANSPORT_DISEMBARK1:
  case ACTION_TRANSPORT_DISEMBARK2:
  case ACTION_BOMBARD:
  case ACTION_BOMBARD2:
  case ACTION_BOMBARD3:
  case ACTION_NUKE:
  case ACTION_SPY_ATTACK:
    /* Target kind is not ruleset changeable */
    return NULL;
  case ACTION_USER_ACTION1:
    return "user_action_1_target_kind";
  case ACTION_USER_ACTION2:
    return "user_action_2_target_kind";
  case ACTION_USER_ACTION3:
    return "user_action_3_target_kind";
  case ACTION_COUNT:
    break;
  }

  fc_assert(act >= 0 && act < ACTION_COUNT);
  return NULL;
}

/**********************************************************************//**
  Return actor consuming always ruleset variable name for the action or
  NULL if actor consuming always can't be set in the ruleset.

  TODO: make actions generic and put actor consuming always in a field of
  the action.
**************************************************************************/
const char *action_actor_consuming_always_ruleset_var_name(action_id act)
{
  switch ((enum gen_action)act) {
  case ACTION_SPY_POISON:
  case ACTION_SPY_POISON_ESC:
  case ACTION_SPY_SABOTAGE_UNIT:
  case ACTION_SPY_SABOTAGE_UNIT_ESC:
  case ACTION_SPY_BRIBE_UNIT:
  case ACTION_SPY_SABOTAGE_CITY:
  case ACTION_SPY_SABOTAGE_CITY_ESC:
  case ACTION_SPY_TARGETED_SABOTAGE_CITY:
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION:
  case ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC:
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION_ESC:
  case ACTION_SPY_INCITE_CITY:
  case ACTION_SPY_INCITE_CITY_ESC:
  case ACTION_ESTABLISH_EMBASSY:
  case ACTION_ESTABLISH_EMBASSY_STAY:
  case ACTION_SPY_STEAL_TECH:
  case ACTION_SPY_STEAL_TECH_ESC:
  case ACTION_SPY_TARGETED_STEAL_TECH:
  case ACTION_SPY_TARGETED_STEAL_TECH_ESC:
  case ACTION_SPY_INVESTIGATE_CITY:
  case ACTION_INV_CITY_SPEND:
  case ACTION_SPY_STEAL_GOLD:
  case ACTION_SPY_STEAL_GOLD_ESC:
  case ACTION_STEAL_MAPS:
  case ACTION_STEAL_MAPS_ESC:
  case ACTION_TRADE_ROUTE:
  case ACTION_MARKETPLACE:
  case ACTION_HELP_WONDER:
  case ACTION_CAPTURE_UNITS:
  case ACTION_EXPEL_UNIT:
  case ACTION_FOUND_CITY:
  case ACTION_JOIN_CITY:
  case ACTION_SPY_NUKE:
  case ACTION_SPY_NUKE_ESC:
  case ACTION_NUKE_CITY:
  case ACTION_NUKE_UNITS:
  case ACTION_DESTROY_CITY:
  case ACTION_RECYCLE_UNIT:
  case ACTION_DISBAND_UNIT:
  case ACTION_HOME_CITY:
  case ACTION_UPGRADE_UNIT:
  case ACTION_PARADROP:
  case ACTION_AIRLIFT:
  case ACTION_ATTACK:
  case ACTION_SUICIDE_ATTACK:
  case ACTION_STRIKE_BUILDING:
  case ACTION_STRIKE_PRODUCTION:
  case ACTION_CONQUER_CITY:
  case ACTION_CONQUER_CITY2:
  case ACTION_HEAL_UNIT:
  case ACTION_TRANSFORM_TERRAIN:
  case ACTION_CULTIVATE:
  case ACTION_PLANT:
  case ACTION_PILLAGE:
  case ACTION_CLEAN_POLLUTION:
  case ACTION_CLEAN_FALLOUT:
  case ACTION_FORTIFY:
  case ACTION_SENTRY:
  case ACTION_ROAD:
  case ACTION_CONVERT:
  case ACTION_BASE:
  case ACTION_MINE:
  case ACTION_IRRIGATE:
  case ACTION_TRANSPORT_ALIGHT:
  case ACTION_TRANSPORT_BOARD:
  case ACTION_TRANSPORT_EMBARK:
  case ACTION_TRANSPORT_UNLOAD:
  case ACTION_TRANSPORT_DISEMBARK1:
  case ACTION_TRANSPORT_DISEMBARK2:
  case ACTION_BOMBARD:
  case ACTION_BOMBARD2:
  case ACTION_BOMBARD3:
  case ACTION_NUKE:
  case ACTION_SPY_ATTACK:
    /* actor consuming always is not ruleset changeable */
    return NULL;
  case ACTION_SPY_SPREAD_PLAGUE:
    return "spread_plague_actor_consuming_always";
  case ACTION_USER_ACTION1:
    return "user_action_1_actor_consuming_always";
  case ACTION_USER_ACTION2:
    return "user_action_2_actor_consuming_always";
  case ACTION_USER_ACTION3:
    return "user_action_3_actor_consuming_always";
  case ACTION_COUNT:
    break;
  }

  fc_assert(act >= 0 && act < ACTION_COUNT);
  return NULL;
}
