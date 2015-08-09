/**********************************************************************
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

#include <string.h>
#include <math.h> /* ceil */

/* utility */
#include "astring.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "shared.h"
#include "string_vector.h"
#include "support.h"

/* common */
#include "ai.h"
#include "game.h"
#include "government.h"
#include "movement.h"
#include "player.h"
#include "research.h"
#include "unitlist.h"

#include "unittype.h"

#define MAX_UNIT_ROLES L_LAST + ACTION_COUNT

static struct unit_type unit_types[U_LAST];
static struct unit_class unit_classes[UCL_LAST];
/* the unit_types and unit_classes arrays are now setup in:
   server/ruleset.c (for the server)
   client/packhand.c (for the client)
*/

static struct user_flag user_type_flags[MAX_NUM_USER_UNIT_FLAGS];

/**************************************************************************
  Return the first item of unit_types.
**************************************************************************/
struct unit_type *unit_type_array_first(void)
{
  if (game.control.num_unit_types > 0) {
    return unit_types;
  }
  return NULL;
}

/**************************************************************************
  Return the last item of unit_types.
**************************************************************************/
const struct unit_type *unit_type_array_last(void)
{
  if (game.control.num_unit_types > 0) {
    return &unit_types[game.control.num_unit_types - 1];
  }
  return NULL;
}

/**************************************************************************
  Return the number of unit types.
**************************************************************************/
Unit_type_id utype_count(void)
{
  return game.control.num_unit_types;
}

/**************************************************************************
  Return the unit type index.

  Currently same as utype_number(), paired with utype_count()
  indicates use as an array index.
**************************************************************************/
Unit_type_id utype_index(const struct unit_type *punittype)
{
  fc_assert_ret_val(punittype, -1);
  return punittype - unit_types;
}

/**************************************************************************
  Return the unit type index.
**************************************************************************/
Unit_type_id utype_number(const struct unit_type *punittype)
{
  fc_assert_ret_val(punittype, -1);
  return punittype->item_number;
}

/**************************************************************************
  Return a pointer for the unit type struct for the given unit type id.

  This function returns NULL for invalid unit pointers (some callers
  rely on this).
**************************************************************************/
struct unit_type *utype_by_number(const Unit_type_id id)
{
  if (id < 0 || id >= game.control.num_unit_types) {
    return NULL;
  }
  return &unit_types[id];
}

/**************************************************************************
  Return the unit type for this unit.
**************************************************************************/
struct unit_type *unit_type(const struct unit *punit)
{
  fc_assert_ret_val(NULL != punit, NULL);
  return punit->utype;
}


/**************************************************************************
  Returns the upkeep of a unit of this type under the given government.
**************************************************************************/
int utype_upkeep_cost(const struct unit_type *ut, struct player *pplayer,
                      Output_type_id otype)
{
  int val = ut->upkeep[otype], gold_upkeep_factor;

  if (BV_ISSET(ut->flags, UTYF_FANATIC)
      && get_player_bonus(pplayer, EFT_FANATICS) > 0) {
    /* Special case: fanatics have no upkeep under fanaticism. */
    return 0;
  }

  /* switch shield upkeep to gold upkeep if
     - the effect 'EFT_SHIELD2GOLD_FACTOR' is non-zero (it gives the
       conversion factor in percent) and
     - the unit has the corresponding flag set (UTYF_SHIELD2GOLD)
     FIXME: Should the ai know about this? */
  if (utype_has_flag(ut, UTYF_SHIELD2GOLD)
      && (otype == O_GOLD || otype == O_SHIELD)) {
    gold_upkeep_factor = get_player_bonus(pplayer, EFT_SHIELD2GOLD_FACTOR);
    if (gold_upkeep_factor > 0) {
      switch (otype) {
      case O_GOLD:
        val = ceil((0.01 * gold_upkeep_factor) * ut->upkeep[O_SHIELD]);
        break;
      case O_SHIELD:
        val = 0;
        break;
      default:
        fc_assert(otype == O_GOLD || otype == O_SHIELD);
        break;
      }
    }
  }

  val *= get_player_output_bonus(pplayer, get_output_type(otype), 
                                 EFT_UPKEEP_FACTOR);
  return val;
}

/**************************************************************************
  Return the "happy cost" (the number of citizens who are discontented)
  for this unit.
**************************************************************************/
int utype_happy_cost(const struct unit_type *ut, 
                     const struct player *pplayer)
{
  return ut->happy_cost * get_player_bonus(pplayer, EFT_UNHAPPY_FACTOR);
}

/**************************************************************************
  Return whether the given unit class has the flag.
**************************************************************************/
bool uclass_has_flag(const struct unit_class *punitclass,
                     enum unit_class_flag_id flag)
{
  fc_assert_ret_val(unit_class_flag_id_is_valid(flag), FALSE);
  return BV_ISSET(punitclass->flags, flag);
}

/**************************************************************************
  Return whether the given unit type has the flag.
**************************************************************************/
bool utype_has_flag(const struct unit_type *punittype, int flag)
{
  fc_assert_ret_val(unit_type_flag_id_is_valid(flag), FALSE);
  return BV_ISSET(punittype->flags, flag);
}

/**************************************************************************
  Return whether the unit has the given flag.
**************************************************************************/
bool unit_has_type_flag(const struct unit *punit, enum unit_type_flag_id flag)
{
  return utype_has_flag(unit_type(punit), flag);
}

/**************************************************************************
  Return whether the given unit type has the role.  Roles are like
  flags but have no meaning except to the AI.
**************************************************************************/
bool utype_has_role(const struct unit_type *punittype, int role)
{
  fc_assert_ret_val(role >= L_FIRST && role < L_LAST, FALSE);
  return BV_ISSET(punittype->roles, role - L_FIRST);
}

/**************************************************************************
  Return whether the unit has the given role.
**************************************************************************/
bool unit_has_type_role(const struct unit *punit, enum unit_role_id role)
{
  return utype_has_role(unit_type(punit), role);
}

/****************************************************************************
  Return whether the unit can take over enemy cities.
****************************************************************************/
bool unit_can_take_over(const struct unit *punit)
{
  return unit_owner(punit)->ai_common.barbarian_type != ANIMAL_BARBARIAN
    && utype_can_take_over(unit_type(punit));
}

/****************************************************************************
  Return whether the unit type can take over enemy cities.
****************************************************************************/
bool utype_can_take_over(const struct unit_type *punittype)
{
  return (uclass_has_flag(utype_class(punittype), UCF_CAN_OCCUPY_CITY)
          && !utype_has_flag(punittype, UTYF_CIVILIAN));
}

/****************************************************************************
  Return TRUE iff the given cargo type has no restrictions on when it can
  load onto the given transporter.
  (Does not check that cargo is valid for transport!)
****************************************************************************/
bool utype_can_freely_load(const struct unit_type *pcargotype,
                           const struct unit_type *ptranstype)
{
  return BV_ISSET(pcargotype->embarks,
                  uclass_index(utype_class(ptranstype)));
}

/****************************************************************************
  Return TRUE iff the given cargo type has no restrictions on when it can
  unload from the given transporter.
  (Does not check that cargo is valid for transport!)
****************************************************************************/
bool utype_can_freely_unload(const struct unit_type *pcargotype,
                             const struct unit_type *ptranstype)
{
  return BV_ISSET(pcargotype->disembarks,
                  uclass_index(utype_class(ptranstype)));
}

/* Fake action representing any hostile action. */
#define ACTION_HOSTILE ACTION_COUNT + 1

/* Number of real and fake actions. */
#define ACTION_AND_FAKES ACTION_HOSTILE + 1

/* Cache of what generalized (ruleset defined) action enabler controlled
 * actions units of each type can perform. Checking if any action can be
 * done at all is done via the fake action ACTION_ANY. If any hostile
 * action can be performed is done via ACTION_HOSTILE. */
static bv_unit_types unit_can_act_cache[ACTION_AND_FAKES];

/**************************************************************************
  Cache what generalized (ruleset defined) action enabler controlled
  actions a unit of the given type can perform.
**************************************************************************/
static void unit_can_act_cache_set(struct unit_type *putype)
{
  /* Clear old values. */
  action_iterate(act_id) {
    BV_CLR(unit_can_act_cache[act_id], utype_index(putype));
  } action_iterate_end;
  BV_CLR(unit_can_act_cache[ACTION_ANY], utype_index(putype));
  BV_CLR(unit_can_act_cache[ACTION_HOSTILE], utype_index(putype));

  /* See if the unit type can do an action controlled by generalized action
   * enablers */
  action_enablers_iterate(enabler) {
    if (action_get_actor_kind(enabler->action) == AAK_UNIT
        && action_actor_utype_hard_reqs_ok(enabler->action, putype)
        && requirement_fulfilled_by_unit_type(putype,
                                              &(enabler->actor_reqs))) {
      log_debug("act_cache: %s can %s",
                utype_rule_name(putype), gen_action_name(enabler->action));
      BV_SET(unit_can_act_cache[enabler->action], utype_index(putype));
      BV_SET(unit_can_act_cache[ACTION_ANY], utype_index(putype));
      if (action_is_hostile(enabler->action)) {
        BV_SET(unit_can_act_cache[ACTION_HOSTILE], utype_index(putype));
      }
    }
  } action_enablers_iterate_end;
}

/**************************************************************************
  Return TRUE iff units of this type can do actions controlled by
  generalized (ruleset defined) action enablers.
**************************************************************************/
bool is_actor_unit_type(const struct unit_type *putype)
{
  return utype_can_do_action(putype, ACTION_ANY);
}

/**************************************************************************
  Return TRUE iff units of the given type can do the specified generalized
  (ruleset defined) action enabler controlled action.
**************************************************************************/
bool utype_can_do_action(const struct unit_type *putype,
                         const int action_id)
{
  return BV_ISSET(unit_can_act_cache[action_id], utype_index(putype));
}

/**************************************************************************
  Return TRUE iff the unit type can perform the action corresponding to
  the unit type role.
**************************************************************************/
static bool utype_can_do_action_role(const struct unit_type *putype,
                                     const int role)
{
  return utype_can_do_action(putype, role - L_LAST);
}

/**************************************************************************
  Return TRUE iff units of this type can do hostile actions controlled by
  generalized (ruleset defined) action enablers.
**************************************************************************/
bool utype_acts_hostile(const struct unit_type *putype)
{
  return utype_can_do_action(putype, ACTION_HOSTILE);
}

/* Cache if any action at all may be possible when the actor unit's state
 * is...
 * bit 0 to USP_COUNT - 1: Possible when the corresponding property is TRUE
 * bit USP_COUNT to ((USP_COUNT - 1) * 2): Possible when the corresponding
 * property is FALSE
 */
BV_DEFINE(bv_ustate_act_cache, ((USP_COUNT - 1) * 2));

/* Caches for each unit type */
static bv_ustate_act_cache ustate_act_cache[U_LAST][ACTION_AND_FAKES];
static bv_diplrel_all_reqs dipl_rel_action_cache[U_LAST][ACTION_AND_FAKES];

/**************************************************************************
  Cache if any action may be possible for a unit of the type putype for
  each unit state property. Since a unit state property could be ignored
  both present and !present must be checked.
**************************************************************************/
static void unit_state_action_cache_set(struct unit_type *putype)
{
  struct requirement req;
  int uidx = utype_index(putype);

  /* The unit is not yet known to be allowed to perform any actions no
   * matter what its unit state is. */
  action_iterate(action_id) {
    BV_CLR_ALL(ustate_act_cache[uidx][action_id]);
  } action_iterate_end;
  BV_CLR_ALL(ustate_act_cache[uidx][ACTION_ANY]);
  BV_CLR_ALL(ustate_act_cache[uidx][ACTION_HOSTILE]);

  if (!is_actor_unit_type(putype)) {
    /* Not an actor unit. */
    return;
  }

  /* Common for every situation */
  req.range = REQ_RANGE_LOCAL;
  req.survives = FALSE;
  req.source.kind = VUT_UNITSTATE;

  for (req.source.value.unit_state = ustate_prop_begin();
       req.source.value.unit_state != ustate_prop_end();
       req.source.value.unit_state = ustate_prop_next(
         req.source.value.unit_state)) {

    /* No action will ever be possible in a specific unit state if the
     * opposite unit state is required in all action enablers.
     * No unit state except present and !present of the same property
     * implies or conflicts with another so the tests can be simple. */
    action_enablers_iterate(enabler) {
      if (requirement_fulfilled_by_unit_type(putype,
                                             &(enabler->actor_reqs))
          && action_get_actor_kind(enabler->action) == AAK_UNIT) {
        /* Not required to be absent, so OK if present */
        req.present = FALSE;
        if (!is_req_in_vec(&req, &(enabler->actor_reqs))) {
          BV_SET(ustate_act_cache[utype_index(putype)][ACTION_ANY],
              requirement_unit_state_ereq(req.source.value.unit_state,
                                         TRUE));
          BV_SET(ustate_act_cache[utype_index(putype)][enabler->action],
              requirement_unit_state_ereq(req.source.value.unit_state,
                                         TRUE));
          if (action_is_hostile(enabler->action)) {
            BV_SET(ustate_act_cache[utype_index(putype)][ACTION_HOSTILE],
                requirement_unit_state_ereq(req.source.value.unit_state,
                                           TRUE));
          }
        }

        /* Not required to be present, so OK if absent */
        req.present = TRUE;
        if (!is_req_in_vec(&req, &(enabler->actor_reqs))) {
          BV_SET(ustate_act_cache[utype_index(putype)][ACTION_ANY],
                 requirement_unit_state_ereq(req.source.value.unit_state,
                                            FALSE));
          BV_SET(ustate_act_cache[utype_index(putype)][enabler->action],
                 requirement_unit_state_ereq(req.source.value.unit_state,
                                            FALSE));
          if (action_is_hostile(enabler->action)) {
            BV_SET(ustate_act_cache[utype_index(putype)][ACTION_HOSTILE],
                   requirement_unit_state_ereq(req.source.value.unit_state,
                                              FALSE));
          }
        }
      }
    } action_enablers_iterate_end;
  }
}

/**************************************************************************
  Cache what actions may be possible for a unit of the type putype for
  each local DiplRel variation. Since a diplomatic relationship could be
  ignored both present and !present must be checked.

  Note: since can_unit_act_when_local_diplrel_is() only supports querying
  the local range no values for the other ranges are set.
**************************************************************************/
static void local_dipl_rel_action_cache_set(struct unit_type *putype)
{
  struct requirement req;
  int putype_id = utype_index(putype);

  /* The unit is not yet known to be allowed to perform any actions no
   * matter what the diplomatic state is. */
  action_iterate(action_id) {
    BV_CLR_ALL(dipl_rel_action_cache[putype_id][action_id]);
  } action_iterate_end;
  BV_CLR_ALL(dipl_rel_action_cache[putype_id][ACTION_ANY]);
  BV_CLR_ALL(dipl_rel_action_cache[putype_id][ACTION_HOSTILE]);

  if (!is_actor_unit_type(putype)) {
    /* Not an actor unit. */
    return;
  }

  /* Common for every situation */
  req.range = REQ_RANGE_LOCAL;
  req.survives = FALSE;
  req.source.kind = VUT_DIPLREL;

  /* DiplRel starts with diplstate_type and ends with diplrel_other */
  for (req.source.value.diplrel = diplstate_type_begin();
       req.source.value.diplrel != DRO_LAST;
       req.source.value.diplrel++) {

    /* No action will ever be possible in a specific diplomatic relation if
     * its presence contradicts all action enablers.
     * Everything was set to false above. It is therefore OK to only change
     * the cache when units can do an action given a certain diplomatic
     * relationship property value. */
    action_enablers_iterate(enabler) {
      if (requirement_fulfilled_by_unit_type(putype,
                                             &(enabler->actor_reqs))
          && action_get_actor_kind(enabler->action) == AAK_UNIT) {
        req.present = TRUE;
        if (!does_req_contradicts_reqs(&req, &(enabler->actor_reqs))) {
          BV_SET(dipl_rel_action_cache[putype_id][enabler->action],
                 requirement_diplrel_ereq(req.source.value.diplrel,
                                          REQ_RANGE_LOCAL, TRUE));
          BV_SET(dipl_rel_action_cache[putype_id][ACTION_HOSTILE],
                 requirement_diplrel_ereq(req.source.value.diplrel,
                                          REQ_RANGE_LOCAL, TRUE));
          BV_SET(dipl_rel_action_cache[putype_id][ACTION_ANY],
                 requirement_diplrel_ereq(req.source.value.diplrel,
                                          REQ_RANGE_LOCAL, TRUE));
        }

        req.present = FALSE;
        if (!does_req_contradicts_reqs(&req, &(enabler->actor_reqs))) {
          BV_SET(dipl_rel_action_cache[putype_id][enabler->action],
              requirement_diplrel_ereq(req.source.value.diplrel,
                                       REQ_RANGE_LOCAL, FALSE));
          BV_SET(dipl_rel_action_cache[putype_id][ACTION_HOSTILE],
              requirement_diplrel_ereq(req.source.value.diplrel,
                                       REQ_RANGE_LOCAL, FALSE));
          BV_SET(dipl_rel_action_cache[putype_id][ACTION_ANY],
              requirement_diplrel_ereq(req.source.value.diplrel,
                                       REQ_RANGE_LOCAL, FALSE));
        }
      }
    } action_enablers_iterate_end;
  }
}

struct range {
  int min;
  int max;
};

#define MOVES_LEFT_INFINITY -1

/**************************************************************************
  Get the legal range of move fragments left of the specified requirement
  vector.
**************************************************************************/
static struct range *moves_left_range(struct requirement_vector *reqs)
{
  struct range *prange = fc_malloc(sizeof(prange));

  prange->min = 0;
  prange->max = MOVES_LEFT_INFINITY;

  requirement_vector_iterate(reqs, preq) {
    if (preq->source.kind == VUT_MINMOVES) {
      if (preq->present) {
        prange->min = preq->source.value.minmoves;
      } else {
        prange->max = preq->source.value.minmoves;
      }
    }
  } requirement_vector_iterate_end;

  return prange;
}

/**************************************************************************
  Cache if any action may be possible for a unit of the type putype given
  the property tested for. Since a it could be ignored both present and
  !present must be checked.
**************************************************************************/
void unit_type_action_cache_set(struct unit_type *ptype)
{
  unit_can_act_cache_set(ptype);
  unit_state_action_cache_set(ptype);
  local_dipl_rel_action_cache_set(ptype);
}

/**************************************************************************
  Cache what unit types may be allowed do what actions, both at all and
  when certain properties are true.
**************************************************************************/
void unit_type_action_cache_init(void)
{
  unit_type_iterate(u) {
    unit_type_action_cache_set(u);
  } unit_type_iterate_end;
}

/**************************************************************************
  Return TRUE iff there exists an (action enabler controlled) action that a
  unit of the type punit_type can perform while its unit state property
  prop has the value is_there.
**************************************************************************/
bool can_unit_act_when_ustate_is(const struct unit_type *punit_type,
                                 const enum ustate_prop prop,
                                 const bool is_there)
{
  return utype_can_do_act_when_ustate(punit_type, ACTION_ANY, prop, is_there);
}

/**************************************************************************
  Return TRUE iff the unit type can do the specified (action enabler
  controlled) action while its unit state property prop has the value
  is_there.
**************************************************************************/
bool utype_can_do_act_when_ustate(const struct unit_type *punit_type,
                                  const int action_id,
                                  const enum ustate_prop prop,
                                  const bool is_there)
{
  return BV_ISSET(ustate_act_cache[utype_index(punit_type)][action_id],
      requirement_unit_state_ereq(prop, is_there));
}

/**************************************************************************
  Return TRUE iff the given (action enabler controlled) action can be
  performed by a unit of the given type while the given property of its
  owner's diplomatic relationship to the target's owner has the given
  value.

  Note: since this only supports the local range no information for other
  ranges are stored in dipl_rel_action_cache.
**************************************************************************/
bool can_utype_do_act_if_tgt_diplrel(const struct unit_type *punit_type,
                                     const int action_id,
                                     const int prop,
                                     const bool is_there)
{
  int utype_id = utype_index(punit_type);

  return BV_ISSET(dipl_rel_action_cache[utype_id][action_id],
      requirement_diplrel_ereq(prop, REQ_RANGE_LOCAL, is_there));
}

/**************************************************************************
  Return TRUE iff the given (action enabler controlled) action may be
  performed by a unit of the given type that has the given number of move
  fragments left.

  Note: Values aren't cached. If a performance critical user appears it
  would be a good idea to cache the (merged) ranges of move fragments
  where a unit of the given type can perform the specified action.
**************************************************************************/
bool utype_may_act_move_frags(struct unit_type *punit_type,
                              const int action_id,
                              const int move_fragments)
{
  struct range *ml_range;

  if (!is_actor_unit_type(punit_type)) {
    /* Not an actor unit. */
    return FALSE;
  }

  if (action_get_actor_kind(action_id) != AAK_UNIT) {
    /* This action isn't performed by any unit at all so this unit type
     * can't do it. */
    return FALSE;
  }

  action_enabler_list_iterate(action_enablers_for_action(action_id),
                              enabler) {
    if (!requirement_fulfilled_by_unit_type(punit_type,
                                            &(enabler->actor_reqs))) {
      /* This action can't be performed by this unit type at all. */
      continue;
    }

    ml_range = moves_left_range(&(enabler->actor_reqs));
    if (ml_range->min <= move_fragments
        && (ml_range->max == MOVES_LEFT_INFINITY
            || ml_range->max > move_fragments)) {
      /* The number of move fragments is in range of the action enabler. */

      free(ml_range);

      return TRUE;
    }

    free(ml_range);
  } action_enabler_list_iterate_end;

  return FALSE;
}

/****************************************************************************
  Returns the number of shields it takes to build this unit type.
****************************************************************************/
int utype_build_shield_cost(const struct unit_type *punittype)
{
  return MAX(punittype->build_cost * game.info.shieldbox / 100, 1);
}

/****************************************************************************
  Returns the number of shields it takes to build this unit.
****************************************************************************/
int unit_build_shield_cost(const struct unit *punit)
{
  return utype_build_shield_cost(unit_type(punit));
}

/****************************************************************************
  Returns the amount of gold it takes to rush this unit.
****************************************************************************/
int utype_buy_gold_cost(const struct unit_type *punittype,
			int shields_in_stock)
{
  int cost = 0;
  const int missing = utype_build_shield_cost(punittype) - shields_in_stock;

  if (missing > 0) {
    cost = 2 * missing + (missing * missing) / 20;
  }
  if (shields_in_stock == 0) {
    cost *= 2;
  }
  return cost;
}

/****************************************************************************
  Returns the number of shields received when this unit type is disbanded.
****************************************************************************/
int utype_disband_shields(const struct unit_type *punittype)
{
  return utype_build_shield_cost(punittype) / 2;
}

/****************************************************************************
  Returns the number of shields received when this unit is disbanded.
****************************************************************************/
int unit_disband_shields(const struct unit *punit)
{
  return utype_disband_shields(unit_type(punit));
}

/**************************************************************************
  How much city shrinks when it builds unit of this type.
**************************************************************************/
int utype_pop_value(const struct unit_type *punittype)
{
  return (punittype->pop_cost);
}

/**************************************************************************
  How much population is put to building this unit.
**************************************************************************/
int unit_pop_value(const struct unit *punit)
{
  return utype_pop_value(unit_type(punit));
}

/**************************************************************************
  Return move type of the unit type
**************************************************************************/
enum unit_move_type utype_move_type(const struct unit_type *punittype)
{
  return utype_class(punittype)->move_type;
}

/**************************************************************************
  Return the (translated) name of the unit type.
  You don't have to free the return pointer.
**************************************************************************/
const char *utype_name_translation(const struct unit_type *punittype)
{
  return name_translation(&punittype->name);
}

/**************************************************************************
  Return the (translated) name of the unit.
  You don't have to free the return pointer.
**************************************************************************/
const char *unit_name_translation(const struct unit *punit)
{
  return utype_name_translation(unit_type(punit));
}

/**************************************************************************
  Return the (untranslated) rule name of the unit type.
  You don't have to free the return pointer.
**************************************************************************/
const char *utype_rule_name(const struct unit_type *punittype)
{
  return rule_name(&punittype->name);
}

/**************************************************************************
  Return the (untranslated) rule name of the unit.
  You don't have to free the return pointer.
**************************************************************************/
const char *unit_rule_name(const struct unit *punit)
{
  return utype_rule_name(unit_type(punit));
}

/**************************************************************************
  Return string describing unit type values.
  String is static buffer that gets reused when function is called again.
**************************************************************************/
const char *utype_values_string(const struct unit_type *punittype)
{
  static char buffer[256];

  /* Print in two parts as move_points_text() returns a static buffer */
  fc_snprintf(buffer, sizeof(buffer), "%d/%d/%s",
              punittype->attack_strength,
              punittype->defense_strength,
              move_points_text(punittype->move_rate, TRUE));
  if (utype_fuel(punittype)) {
    cat_snprintf(buffer, sizeof(buffer), "(%s)",
                 move_points_text(punittype->move_rate * utype_fuel(punittype),
                                  TRUE));
  }
  return buffer;
}

/**************************************************************************
  Return string with translated unit name and list of its values.
  String is static buffer that gets reused when function is called again.
**************************************************************************/
const char *utype_values_translation(const struct unit_type *punittype)
{
  static char buffer[256];

  fc_snprintf(buffer, sizeof(buffer),
              "%s [%s]",
              utype_name_translation(punittype),
              utype_values_string(punittype));
  return buffer;
}

/**************************************************************************
  Return the (translated) name of the unit class.
  You don't have to free the return pointer.
**************************************************************************/
const char *uclass_name_translation(const struct unit_class *pclass)
{
  return name_translation(&pclass->name);
}

/**************************************************************************
  Return the (untranslated) rule name of the unit class.
  You don't have to free the return pointer.
**************************************************************************/
const char *uclass_rule_name(const struct unit_class *pclass)
{
  return rule_name(&pclass->name);
}

/****************************************************************************
  Return a string with all the names of units with this flag. If "alts" is
  set, separate with "or", otherwise "and". Return FALSE if no unit with
  this flag exists.
****************************************************************************/
bool role_units_translations(struct astring *astr, int flag, bool alts)
{
  const int count = num_role_units(flag);

  if (4 < count) {
    if (alts) {
      astr_set(astr, _("%s or similar units"),
               utype_name_translation(get_role_unit(flag, 0)));
    } else {
      astr_set(astr, _("%s and similar units"),
               utype_name_translation(get_role_unit(flag, 0)));
    }
    return TRUE;
  } else if (0 < count) {
    const char *vec[count];
    int i;

    for (i = 0; i < count; i++) {
      vec[i] = utype_name_translation(get_role_unit(flag, i));
    }

    if (alts) {
      astr_build_or_list(astr, vec, count);
    } else {
      astr_build_and_list(astr, vec, count);
    }
    return TRUE;
  }
  return FALSE;
}

/**************************************************************************
  Return whether this player can upgrade this unit type (to any other
  unit type).  Returns NULL if no upgrade is possible.
**************************************************************************/
struct unit_type *can_upgrade_unittype(const struct player *pplayer,
				       struct unit_type *punittype)
{
  struct unit_type *upgrade = punittype;
  struct unit_type *best_upgrade = NULL;

  /* For some reason this used to check
   * can_player_build_unit_direct() for the unittype
   * we're updating from.
   * That check is now removed as it prevented legal updates
   * of units player had acquired via bribing, capturing,
   * diplomatic treaties, or lua script. */

  while ((upgrade = upgrade->obsoleted_by) != U_NOT_OBSOLETED) {
    if (can_player_build_unit_direct(pplayer, upgrade)) {
      best_upgrade = upgrade;
    }
  }

  return best_upgrade;
}

/**************************************************************************
  Return the cost (gold) of upgrading a single unit of the specified type
  to the new type.  This price could (but currently does not) depend on
  other attributes (like nation or government type) of the player the unit
  belongs to.
**************************************************************************/
int unit_upgrade_price(const struct player *pplayer,
		       const struct unit_type *from,
		       const struct unit_type *to)
{
  int base_cost = utype_buy_gold_cost(to, utype_disband_shields(from));

  return base_cost
    * (100 + get_player_bonus(pplayer, EFT_UPGRADE_PRICE_PCT))
    / 100;
}

/**************************************************************************
  Returns the unit type that has the given (translated) name.
  Returns NULL if none match.
**************************************************************************/
struct unit_type *unit_type_by_translated_name(const char *name)
{
  unit_type_iterate(punittype) {
    if (0 == strcmp(utype_name_translation(punittype), name)) {
      return punittype;
    }
  } unit_type_iterate_end;

  return NULL;
}

/**************************************************************************
  Returns the unit type that has the given (untranslated) rule name.
  Returns NULL if none match.
**************************************************************************/
struct unit_type *unit_type_by_rule_name(const char *name)
{
  const char *qname = Qn_(name);

  unit_type_iterate(punittype) {
    if (0 == fc_strcasecmp(utype_rule_name(punittype), qname)) {
      return punittype;
    }
  } unit_type_iterate_end;

  return NULL;
}

/**************************************************************************
  Returns the unit class that has the given (untranslated) rule name.
  Returns NULL if none match.
**************************************************************************/
struct unit_class *unit_class_by_rule_name(const char *s)
{
  const char *qs = Qn_(s);

  unit_class_iterate(pclass) {
    if (0 == fc_strcasecmp(uclass_rule_name(pclass), qs)) {
      return pclass;
    }
  } unit_class_iterate_end;
  return NULL;
}

/**************************************************************************
  Initialize user unit type flags.
**************************************************************************/
void user_unit_type_flags_init(void)
{
  int i;

  for (i = 0; i < MAX_NUM_USER_UNIT_FLAGS; i++) {
    user_flag_init(&user_type_flags[i]);
  }
}

/**************************************************************************
  Sets user defined name for unit flag.
**************************************************************************/
void set_user_unit_type_flag_name(enum unit_type_flag_id id, const char *name,
                                  const char *helptxt)
{
  int ufid = id - UTYF_USER_FLAG_1;

  fc_assert_ret(id >= UTYF_USER_FLAG_1 && id <= UTYF_LAST_USER_FLAG);

  if (user_type_flags[ufid].name != NULL) {
    FC_FREE(user_type_flags[ufid].name);
    user_type_flags[ufid].name = NULL;
  }

  if (name && name[0] != '\0') {
    user_type_flags[ufid].name = fc_strdup(name);
  }

  if (user_type_flags[ufid].helptxt != NULL) {
    free(user_type_flags[ufid].helptxt);
    user_type_flags[ufid].helptxt = NULL;
  }

  if (helptxt && helptxt[0] != '\0') {
    user_type_flags[ufid].helptxt = fc_strdup(helptxt);
  }
}

/**************************************************************************
  Unit type flag name callback, called from specenum code.
**************************************************************************/
const char *unit_type_flag_id_name_cb(enum unit_type_flag_id flag)
{
  if (flag < UTYF_USER_FLAG_1 || flag > UTYF_LAST_USER_FLAG) {
    return NULL;
  }

  return user_type_flags[flag - UTYF_USER_FLAG_1].name;
}

/**************************************************************************
  Return the (untranslated) helptxt of the user unit flag.
**************************************************************************/
const char *unit_type_flag_helptxt(enum unit_type_flag_id id)
{
  fc_assert(id >= UTYF_USER_FLAG_1 && id <= UTYF_LAST_USER_FLAG);

  return user_type_flags[id - UTYF_USER_FLAG_1].helptxt;
}

/**************************************************************************
  Returns TRUE iff the unit type is unique and the player already has one.
**************************************************************************/
bool utype_player_already_has_this_unique(const struct player *pplayer,
                                          const struct unit_type *putype)
{
  if (!utype_has_flag(putype, UTYF_UNIQUE)) {
    /* This isn't a unique unit type. */
    return FALSE;
  }

  unit_list_iterate(pplayer->units, existing_unit) {
    if (putype == unit_type(existing_unit)) {
      /* FIXME: This could be slow if we have lots of units. We could
       * consider keeping an array of unittypes updated with this info
       * instead. */

      return TRUE;
    }
  } unit_list_iterate_end;

  /* The player doesn't already have one. */
  return FALSE;
}

/**************************************************************************
Whether player can build given unit somewhere,
ignoring whether unit is obsolete and assuming the
player has a coastal city.
**************************************************************************/
bool can_player_build_unit_direct(const struct player *p,
                                  const struct unit_type *punittype)
{
  fc_assert_ret_val(NULL != punittype, FALSE);

  if (is_barbarian(p)
      && !utype_has_role(punittype, L_BARBARIAN_BUILD)
      && !utype_has_role(punittype, L_BARBARIAN_BUILD_TECH)) {
    /* Barbarians can build only role units */
    return FALSE;
  }

  if (utype_can_do_action(punittype, ACTION_NUKE)
      && get_player_bonus(p, EFT_ENABLE_NUKE) <= 0) {
    return FALSE;
  }
  if (utype_has_flag(punittype, UTYF_NOBUILD)) {
    return FALSE;
  }

  if (utype_has_flag(punittype, UTYF_BARBARIAN_ONLY)
      && !is_barbarian(p)) {
    /* Unit can be built by barbarians only and this player is
     * not barbarian */
    return FALSE;
  }

  if (punittype->need_government
      && punittype->need_government != government_of_player(p)) {
    return FALSE;
  }
  if (research_invention_state(research_get(p),
                               advance_number(punittype->require_advance))
      != TECH_KNOWN) {
    if (!is_barbarian(p)) {
      /* Normal players can never build units without knowing tech
       * requirements. */
      return FALSE;
    }
    if (!utype_has_role(punittype, L_BARBARIAN_BUILD)) {
      /* Even barbarian cannot build this unit without tech */

      /* Unit has to have L_BARBARIAN_BUILD_TECH role
       * In the beginning of this function we checked that
       * barbarian player tries to build only role
       * L_BARBARIAN_BUILD or L_BARBARIAN_BUILD_TECH units. */
      fc_assert_ret_val(utype_has_role(punittype, L_BARBARIAN_BUILD_TECH),
                        FALSE);

      /* Client does not know all the advances other players have
       * got. So following gives wrong answer in the client.
       * This is called at the client when received create_city
       * packet for a barbarian city. City initialization tries
       * to find L_FIRSTBUILD unit. */

      if (!game.info.global_advances[advance_index(punittype->require_advance)]) {
        /* Nobody knows required tech */
        return FALSE;
      }
    }
  }

  if (utype_player_already_has_this_unique(p, punittype)) {
    /* A player can only have one unit of each unique unit type. */
    return FALSE;
  }

  /* If the unit has a building requirement, we check to see if the player
   * can build that building.  Note that individual cities may not have
   * that building, so they still may not be able to build the unit. */
  if (punittype->need_improvement) {
    if (is_great_wonder(punittype->need_improvement)
        && (great_wonder_is_built(punittype->need_improvement)
            || great_wonder_is_destroyed(punittype->need_improvement))) {
      /* It's already built great wonder */
      if (great_wonder_owner(punittype->need_improvement) != p) {
        /* Not owned by this player. Either destroyed or owned by somebody
         * else. */
        return FALSE;
      }
    } else {
      if (!can_player_build_improvement_direct(p,
                                               punittype->need_improvement)) {
        return FALSE;
      }
    }
  }

  return TRUE;
}

/**************************************************************************
Whether player can build given unit somewhere;
returns FALSE if unit is obsolete.
**************************************************************************/
bool can_player_build_unit_now(const struct player *p,
			       const struct unit_type *punittype)
{
  if (!can_player_build_unit_direct(p, punittype)) {
    return FALSE;
  }
  while ((punittype = punittype->obsoleted_by) != U_NOT_OBSOLETED) {
    if (can_player_build_unit_direct(p, punittype)) {
	return FALSE;
    }
  }
  return TRUE;
}

/**************************************************************************
Whether player can _eventually_ build given unit somewhere -- ie,
returns TRUE if unit is available with current tech OR will be available
with future tech. Returns FALSE if unit is obsolete.
**************************************************************************/
bool can_player_build_unit_later(const struct player *p,
                                 const struct unit_type *punittype)
{
  fc_assert_ret_val(NULL != punittype, FALSE);
  if (utype_has_flag(punittype, UTYF_NOBUILD)) {
    return FALSE;
  }
  while ((punittype = punittype->obsoleted_by) != U_NOT_OBSOLETED) {
    if (can_player_build_unit_direct(p, punittype)) {
      return FALSE;
    }
  }
  return TRUE;
}

/**************************************************************************
The following functions use static variables so we can quickly look up
which unit types have given flag or role.
For these functions flags and roles are considered to be in the same "space",
and any "role" argument can also be a "flag".
Unit order is in terms of the order in the units ruleset.
**************************************************************************/
static bool first_init = TRUE;
static int n_with_role[MAX_UNIT_ROLES];
static struct unit_type **with_role[MAX_UNIT_ROLES];

/**************************************************************************
Do the real work for role_unit_precalcs, for one role (or flag), given by i.
**************************************************************************/
static void precalc_one(int i,
			bool (*func_has)(const struct unit_type *, int))
{
  int j;

  /* Count: */
  fc_assert(n_with_role[i] == 0);
  unit_type_iterate(u) {
    if (func_has(u, i)) {
      n_with_role[i]++;
    }
  } unit_type_iterate_end;

  if(n_with_role[i] > 0) {
    with_role[i] = fc_malloc(n_with_role[i] * sizeof(*with_role[i]));
    j = 0;
    unit_type_iterate(u) {
      if (func_has(u, i)) {
	with_role[i][j++] = u;
      }
    } unit_type_iterate_end;
    fc_assert(j == n_with_role[i]);
  }
}

/****************************************************************************
  Free memory allocated by role_unit_precalcs().
****************************************************************************/
void role_unit_precalcs_free(void)
{
  int i;

  for (i = 0; i < MAX_UNIT_ROLES; i++) {
    free(with_role[i]);
    with_role[i] = NULL;
    n_with_role[i] = 0;
  }
}

/****************************************************************************
  Initialize; it is safe to call this multiple times (e.g., if units have
  changed due to rulesets in client).
****************************************************************************/
void role_unit_precalcs(void)
{
  int i;

  if (first_init) {
    for (i = 0; i < MAX_UNIT_ROLES; i++) {
      with_role[i] = NULL;
      n_with_role[i] = 0;
    }
  } else {
    role_unit_precalcs_free();
  }

  for (i = 0; i <= UTYF_LAST_USER_FLAG; i++) {
    precalc_one(i, utype_has_flag);
  }
  for (i = L_FIRST; i < L_LAST; i++) {
    precalc_one(i, utype_has_role);
  }
  for (i = L_LAST; i < MAX_UNIT_ROLES; i++) {
    precalc_one(i, utype_can_do_action_role);
  }
  first_init = FALSE;
}

/**************************************************************************
How many unit types have specified role/flag.
**************************************************************************/
int num_role_units(int role)
{
  fc_assert_ret_val((role >= 0 && role <= UTYF_LAST_USER_FLAG)
                    || (role >= L_FIRST && role < L_LAST)
                    || (role >= L_LAST && role < MAX_UNIT_ROLES), -1);
  fc_assert_ret_val(!first_init, -1);
  return n_with_role[role];
}

/**************************************************************************
  Iterate over all the role units and feed them to callback.
  Once callback returns TRUE, no further units are feeded to it and
  we return the unit that caused callback to return TRUE
**************************************************************************/
struct unit_type *role_units_iterate(int role, role_unit_callback cb, void *data)
{
  int i;

  for (i = 0; i < n_with_role[role]; i++) {
    if (cb(with_role[role][i], data)) {
      return with_role[role][i];
    }
  }

  return NULL;
}

/**************************************************************************
  Iterate over all the role units and feed them to callback, starting
  from the last one.
  Once callback returns TRUE, no further units are feeded to it and
  we return the unit that caused callback to return TRUE
**************************************************************************/
struct unit_type *role_units_iterate_backwards(int role, role_unit_callback cb, void *data)
{
  int i;

  for (i = n_with_role[role] - 1; i >= 0; i--) {
    if (cb(with_role[role][i], data)) {
     return with_role[role][i];
    }
  }

  return NULL;
}

/**************************************************************************
Return index-th unit with specified role/flag.
Index -1 means (n-1), ie last one.
**************************************************************************/
struct unit_type *get_role_unit(int role, int index)
{
  fc_assert_ret_val((role >= 0 && role <= UTYF_LAST_USER_FLAG)
                    || (role >= L_FIRST && role < L_LAST)
                    || (role >= L_LAST && role < MAX_UNIT_ROLES), NULL);
  fc_assert_ret_val(!first_init, NULL);
  if (index==-1) {
    index = n_with_role[role]-1;
  }
  fc_assert_ret_val(index >= 0 && index < n_with_role[role], NULL);
  return with_role[role][index];
}

/**************************************************************************
  Return "best" unit this city can build, with given role/flag.
  Returns NULL if none match. "Best" means highest unit type id.
**************************************************************************/
struct unit_type *best_role_unit(const struct city *pcity, int role)
{
  struct unit_type *u;
  int j;

  fc_assert_ret_val((role >= 0 && role <= UTYF_LAST_USER_FLAG)
                    || (role >= L_FIRST && role < L_LAST)
                    || (role >= L_LAST && role < MAX_UNIT_ROLES), NULL);
  fc_assert_ret_val(!first_init, NULL);

  for(j=n_with_role[role]-1; j>=0; j--) {
    u = with_role[role][j];
    if ((1 != utype_fuel(u) || uclass_has_flag(utype_class(u), UCF_MISSILE))
        && can_city_build_unit_now(pcity, u)) {
      /* Allow fuel==1 units when pathfinding can handle them. */
      return u;
    }
  }
  return NULL;
}

/**************************************************************************
  Return "best" unit the player can build, with given role/flag.
  Returns NULL if none match. "Best" means highest unit type id.

  TODO: Cache the result per player?
**************************************************************************/
struct unit_type *best_role_unit_for_player(const struct player *pplayer,
				       int role)
{
  int j;

  fc_assert_ret_val((role >= 0 && role <= UTYF_LAST_USER_FLAG)
                    || (role >= L_FIRST && role < L_LAST)
                    || (role >= L_LAST && role < MAX_UNIT_ROLES), NULL);
  fc_assert_ret_val(!first_init, NULL);

  for(j = n_with_role[role]-1; j >= 0; j--) {
    struct unit_type *utype = with_role[role][j];

    if (can_player_build_unit_now(pplayer, utype)) {
      return utype;
    }
  }

  return NULL;
}

/**************************************************************************
  Return first unit the player can build, with given role/flag.
  Returns NULL if none match.  Used eg when placing starting units.
**************************************************************************/
struct unit_type *first_role_unit_for_player(const struct player *pplayer,
					int role)
{
  int j;

  fc_assert_ret_val((role >= 0 && role <= UTYF_LAST_USER_FLAG)
                    || (role >= L_FIRST && role < L_LAST)
                    || (role >= L_LAST && role < MAX_UNIT_ROLES), NULL);
  fc_assert_ret_val(!first_init, NULL);

  for(j = 0; j < n_with_role[role]; j++) {
    struct unit_type *utype = with_role[role][j];

    if (can_player_build_unit_now(pplayer, utype)) {
      return utype;
    }
  }

  return NULL;
}

/****************************************************************************
  Inialize unit-type structures.
****************************************************************************/
void unit_types_init(void)
{
  int i;

  /* Can't use unit_type_iterate or utype_by_number here because
   * num_unit_types isn't known yet. */
  for (i = 0; i < ARRAY_SIZE(unit_types); i++) {
    unit_types[i].item_number = i;
    unit_types[i].helptext = NULL;
    unit_types[i].veteran = NULL;
    unit_types[i].bonuses = combat_bonus_list_new();
    unit_types[i].disabled = FALSE;
  }
}

/**************************************************************************
  Frees the memory associated with this unit type.
**************************************************************************/
static void unit_type_free(struct unit_type *punittype)
{
  if (NULL != punittype->helptext) {
    strvec_destroy(punittype->helptext);
    punittype->helptext = NULL;
  }

  veteran_system_destroy(punittype->veteran);
  combat_bonus_list_iterate(punittype->bonuses, pbonus) {
    FC_FREE(pbonus);
  } combat_bonus_list_iterate_end;
  combat_bonus_list_destroy(punittype->bonuses);
}

/***************************************************************
 Frees the memory associated with all unit types.
***************************************************************/
void unit_types_free(void)
{
  int i;

  /* Can't use unit_type_iterate or utype_by_number here because
   * we want to free what unit_types_init() has allocated. */
  for (i = 0; i < ARRAY_SIZE(unit_types); i++) {
    unit_type_free(unit_types + i);
  }
}

/***************************************************************
  Frees the memory associated with all unit type flags
***************************************************************/
void unit_type_flags_free(void)
{
  int i;

  for (i = 0; i < MAX_NUM_USER_UNIT_FLAGS; i++) {
    user_flag_free(&user_type_flags[i]);
  }
}

/**************************************************************************
  Return the first item of unit_classes.
**************************************************************************/
struct unit_class *unit_class_array_first(void)
{
  if (game.control.num_unit_classes > 0) {
    return unit_classes;
  }
  return NULL;
}

/**************************************************************************
  Return the last item of unit_classes.
**************************************************************************/
const struct unit_class *unit_class_array_last(void)
{
  if (game.control.num_unit_classes > 0) {
    return &unit_classes[game.control.num_unit_classes - 1];
  }
  return NULL;
}

/**************************************************************************
  Return the unit_class count.
**************************************************************************/
Unit_Class_id uclass_count(void)
{
  return game.control.num_unit_classes;
}

#ifndef uclass_index
/**************************************************************************
  Return the unit_class index.

  Currently same as uclass_number(), paired with uclass_count()
  indicates use as an array index.
**************************************************************************/
Unit_Class_id uclass_index(const struct unit_class *pclass)
{
  fc_assert_ret_val(pclass, -1);
  return pclass - unit_classes;
}
#endif /* uclass_index */

/**************************************************************************
  Return the unit_class index.
**************************************************************************/
Unit_Class_id uclass_number(const struct unit_class *pclass)
{
  fc_assert_ret_val(pclass, -1);
  return pclass->item_number;
}

/****************************************************************************
  Returns unit class pointer for an ID value.
****************************************************************************/
struct unit_class *uclass_by_number(const Unit_Class_id id)
{
  if (id < 0 || id >= game.control.num_unit_classes) {
    return NULL;
  }
  return &unit_classes[id];
}

#ifndef utype_class
/***************************************************************
 Returns unit class pointer for a unit type.
***************************************************************/
struct unit_class *utype_class(const struct unit_type *punittype)
{
  fc_assert(NULL != punittype->uclass);
  return punittype->uclass;
}
#endif /* utype_class */

/***************************************************************
 Returns unit class pointer for a unit.
***************************************************************/
struct unit_class *unit_class(const struct unit *punit)
{
  return utype_class(unit_type(punit));
}

/****************************************************************************
  Initialize unit_class structures.
****************************************************************************/
void unit_classes_init(void)
{
  int i;

  /* Can't use unit_class_iterate or uclass_by_number here because
   * num_unit_classes isn't known yet. */
  for (i = 0; i < ARRAY_SIZE(unit_classes); i++) {
    unit_classes[i].item_number = i;
    unit_classes[i].cache.refuel_bases = NULL;
    unit_classes[i].cache.native_tile_extras = NULL;
    unit_classes[i].cache.subset_movers = NULL;
  }
}

/****************************************************************************
  Free resources allocated for unit classes.
****************************************************************************/
void unit_classes_free(void)
{
  int i;

  for (i = 0; i < ARRAY_SIZE(unit_classes); i++) {
    if (unit_classes[i].cache.refuel_bases != NULL) {
      extra_type_list_destroy(unit_classes[i].cache.refuel_bases);
      unit_classes[i].cache.refuel_bases = NULL;
    }
    if (unit_classes[i].cache.native_tile_extras != NULL) {
      extra_type_list_destroy(unit_classes[i].cache.native_tile_extras);
      unit_classes[i].cache.native_tile_extras = NULL;
    }
    if (unit_classes[i].cache.subset_movers != NULL) {
      unit_class_list_destroy(unit_classes[i].cache.subset_movers);
    }
  }
}

/****************************************************************************
  Return veteran system used for this unit type.
****************************************************************************/
const struct veteran_system *
  utype_veteran_system(const struct unit_type *punittype)
{
  fc_assert_ret_val(punittype != NULL, NULL);

  if (punittype->veteran) {
    return punittype->veteran;
  }

  fc_assert_ret_val(game.veteran != NULL, NULL);
  return game.veteran;
}

/****************************************************************************
  Return veteran level properties of given unit in given veterancy level.
****************************************************************************/
const struct veteran_level *
  utype_veteran_level(const struct unit_type *punittype, int level)
{
  const struct veteran_system *vsystem = utype_veteran_system(punittype);

  fc_assert_ret_val(vsystem != NULL, NULL);
  fc_assert_ret_val(vsystem->definitions != NULL, NULL);
  fc_assert_ret_val(vsystem->levels > level, NULL);

  return (vsystem->definitions + level);
}

/****************************************************************************
  Return translated name of the given veteran level.
  NULL if this unit type doesn't have different veteran levels.
****************************************************************************/
const char *utype_veteran_name_translation(const struct unit_type *punittype,
                                           int level)
{
  if (utype_veteran_levels(punittype) <= 1) {
    return NULL;
  } else {
    const struct veteran_level *vlvl = utype_veteran_level(punittype, level);
    return name_translation(&vlvl->name);
  }
}

/****************************************************************************
  Return veteran levels of the given unit type.
****************************************************************************/
int utype_veteran_levels(const struct unit_type *punittype)
{
  const struct veteran_system *vsystem = utype_veteran_system(punittype);

  fc_assert_ret_val(vsystem != NULL, 1);

  return vsystem->levels;
}

/****************************************************************************
  Return whether this unit type's veteran system, if any, confers a power
  factor bonus at any level (it could just add extra moves).
****************************************************************************/
bool utype_veteran_has_power_bonus(const struct unit_type *punittype)
{
  int i, initial_power_fact = utype_veteran_level(punittype, 0)->power_fact;
  for (i = 1; i < utype_veteran_levels(punittype); i++) {
    if (utype_veteran_level(punittype, i)->power_fact > initial_power_fact) {
      return TRUE;
    }
  }
  return FALSE;
}

/****************************************************************************
  Allocate new veteran system structure with given veteran level count.
****************************************************************************/
struct veteran_system *veteran_system_new(int count)
{
  struct veteran_system *vsystem;

  /* There must be at least one level. */
  fc_assert_ret_val(count > 0, NULL);

  vsystem = fc_calloc(1, sizeof(*vsystem));
  vsystem->levels = count;
  vsystem->definitions = fc_calloc(vsystem->levels,
                                   sizeof(*vsystem->definitions));

  return vsystem;
}

/****************************************************************************
  Free veteran system
****************************************************************************/
void veteran_system_destroy(struct veteran_system *vsystem)
{
  if (vsystem) {
    if (vsystem->definitions) {
      free(vsystem->definitions);
    }
    free(vsystem);
  }
}

/****************************************************************************
  Fill veteran level in given veteran system with given information.
****************************************************************************/
void veteran_system_definition(struct veteran_system *vsystem, int level,
                               const char *vlist_name, int vlist_power,
                               int vlist_move, int vlist_raise,
                               int vlist_wraise)
{
  struct veteran_level *vlevel;

  fc_assert_ret(vsystem != NULL);
  fc_assert_ret(vsystem->levels > level);

  vlevel = vsystem->definitions + level;

  names_set(&vlevel->name, NULL, vlist_name, NULL);
  vlevel->power_fact = vlist_power;
  vlevel->move_bonus = vlist_move;
  vlevel->raise_chance = vlist_raise;
  vlevel->work_raise_chance = vlist_wraise;
}

/**************************************************************************
  Return pointer to ai data of given unit type and ai type.
**************************************************************************/
void *utype_ai_data(const struct unit_type *ptype, const struct ai_type *ai)
{
  return ptype->ais[ai_type_number(ai)];
}

/**************************************************************************
  Attach ai data to unit type
**************************************************************************/
void utype_set_ai_data(struct unit_type *ptype, const struct ai_type *ai,
                       void *data)
{
  ptype->ais[ai_type_number(ai)] = data;
}

/****************************************************************************
  Set caches for unit class.
****************************************************************************/
void set_unit_class_caches(struct unit_class *pclass)
{
  pclass->cache.refuel_bases = extra_type_list_new();
  pclass->cache.native_tile_extras = extra_type_list_new();
  pclass->cache.subset_movers = unit_class_list_new();

  extra_type_iterate(pextra) {
    if (is_native_extra_to_uclass(pextra, pclass)) {
      if (extra_has_flag(pextra, EF_REFUEL)) {
        extra_type_list_append(pclass->cache.refuel_bases, pextra);
      }
      if (extra_has_flag(pextra, EF_NATIVE_TILE)) {
        extra_type_list_append(pclass->cache.native_tile_extras, pextra);
      }
    }
  } extra_type_iterate_end;

  unit_class_iterate(pcharge) {
    bool subset_mover = TRUE;

    terrain_type_iterate(pterrain) {
      if (BV_ISSET(pterrain->native_to, uclass_index(pcharge))
          && !BV_ISSET(pterrain->native_to, uclass_index(pclass))) {
        subset_mover = FALSE;
      }
    } terrain_type_iterate_end;

    if (subset_mover) {
      extra_type_iterate(pextra) {
        if (is_native_extra_to_uclass(pextra, pcharge)
            && !is_native_extra_to_uclass(pextra, pclass)) {
          subset_mover = FALSE;
        }
      } extra_type_list_iterate_end;
    }

    if (subset_mover) {
      unit_class_list_append(pclass->cache.subset_movers, pcharge);
    }
  } unit_class_iterate_end;
}

/**************************************************************************
  What move types nativity of this extra will give?
**************************************************************************/
static enum unit_move_type move_type_from_extra(struct extra_type *pextra,
                                                struct unit_class *puc)
{
  bool land_allowed = TRUE;
  bool sea_allowed = TRUE;

  if (!extra_has_flag(pextra, EF_NATIVE_TILE)) {
    return unit_move_type_invalid();
  }
  if (!is_native_extra_to_uclass(pextra, puc)) {
    return unit_move_type_invalid();
  }

  if (is_extra_caused_by(pextra, EC_ROAD)
      && road_has_flag(extra_road_get(pextra), RF_RIVER)) {
    /* Natural rivers are created to land only */
    sea_allowed = FALSE;
  }

  requirement_vector_iterate(&pextra->reqs, preq) {
    if (preq->source.kind == VUT_TERRAINCLASS) {
      if (!preq->present) {
        if (preq->source.value.terrainclass == TC_LAND) {
          land_allowed = FALSE;
        } else if (preq->source.value.terrainclass == TC_OCEAN) {
          sea_allowed = FALSE;
        }
      } else {
        if (preq->source.value.terrainclass == TC_LAND) {
          sea_allowed = FALSE;
        } else if (preq->source.value.terrainclass == TC_OCEAN) {
          land_allowed = FALSE;
        }
      }
    } else if (preq->source.kind == VUT_TERRAIN) {
     if (!preq->present) {
        if (preq->source.value.terrain->tclass == TC_LAND) {
          land_allowed = FALSE;
        } else if (preq->source.value.terrain->tclass == TC_OCEAN) {
          sea_allowed = FALSE;
        }
      } else {
        if (preq->source.value.terrain->tclass == TC_LAND) {
          sea_allowed = FALSE;
        } else if (preq->source.value.terrain->tclass == TC_OCEAN) {
          land_allowed = FALSE;
        }
      }
    }
  } requirement_vector_iterate_end;

  if (land_allowed && sea_allowed) {
    return UMT_BOTH;
  }
  if (land_allowed && !sea_allowed) {
    return UMT_LAND;
  }
  if (!land_allowed && sea_allowed) {
    return UMT_SEA;
  }

  return unit_move_type_invalid();
}

/****************************************************************************
  Set move_type for unit class.
****************************************************************************/
void set_unit_move_type(struct unit_class *puclass)
{
  bool land_moving = FALSE;
  bool sea_moving = FALSE;

  extra_type_iterate(pextra) {
    enum unit_move_type eut = move_type_from_extra(pextra, puclass);

    if (eut == UMT_BOTH) {
      land_moving = TRUE;
      sea_moving = TRUE;
    } else if (eut == UMT_LAND) {
      land_moving = TRUE;
    } else if (eut == UMT_SEA) {
      sea_moving = TRUE;
    }
  } extra_type_iterate_end;

  terrain_type_iterate(pterrain) {
    bv_extras extras;

    BV_CLR_ALL(extras);

    if (is_native_to_class(puclass, pterrain, extras)) {
      if (is_ocean(pterrain)) {
        sea_moving = TRUE;
      } else {
        land_moving = TRUE;
      }
    }
  } terrain_type_iterate_end;

  if (land_moving && sea_moving) {
    puclass->move_type = UMT_BOTH;
  } else if (sea_moving) {
    puclass->move_type = UMT_SEA;
  } else {
    /* If unit has no native terrains, it is considered land moving */
    puclass->move_type = UMT_LAND;
  }
}

/****************************************************************************
  Is cityfounder type
****************************************************************************/
bool utype_is_cityfounder(struct unit_type *utype)
{
  if (game.scenario.prevent_new_cities) {
    /* No unit is allowed to found new cities */
    return FALSE;
  }

  return utype_can_do_action(utype, ACTION_FOUND_CITY);
}
