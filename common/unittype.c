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
#include "game.h"
#include "government.h"
#include "movement.h"
#include "player.h"
#include "tech.h"
#include "unitlist.h"

#include "unittype.h"

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

  if (get_player_bonus(pplayer, EFT_FANATICS)
      && BV_ISSET(ut->flags, UTYF_FANATIC)) {
    /* Special case: fanatics have no upkeep under fanaticism. */
    return 0;
  }

  /* switch shield upkeep to gold upkeep if
     - the effect 'EFT_SHIELD2GOLD_FACTOR' is non-zero (it gives the
       conversion factor in percent) and
     - the unit has the corresponding flag set (UTYF_SHIELD2GOLD)
     FIXME: Should the ai know about this? */
  gold_upkeep_factor = get_player_bonus(pplayer, EFT_SHIELD2GOLD_FACTOR);
  gold_upkeep_factor = (gold_upkeep_factor > 0) ? gold_upkeep_factor : 0;
  if (gold_upkeep_factor > 0 && utype_has_flag(ut, UTYF_SHIELD2GOLD)) {
    switch (otype) {
      case O_GOLD:
        val = ceil((0.01 * gold_upkeep_factor) * ut->upkeep[O_SHIELD]);
        break;
      case O_SHIELD:
        val = 0;
        break;
      default:
        /* fall through */
        break;
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
  Return whether the unit can take over ennemy cities.
****************************************************************************/
bool unit_can_take_over(const struct unit *punit)
{
  return utype_can_take_over(unit_type(punit));
}

/****************************************************************************
  Return whether the unit type can take over ennemy cities.
****************************************************************************/
bool utype_can_take_over(const struct unit_type *punittype)
{
  return (uclass_has_flag(utype_class(punittype), UCF_CAN_OCCUPY_CITY)
          && !utype_has_flag(punittype, UTYF_CIVILIAN));
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
  Return move type of the unit class
**************************************************************************/
enum unit_move_type uclass_move_type(const struct unit_class *pclass)
{
  return pclass->move_type;
}

/**************************************************************************
  Return move type of the unit type
**************************************************************************/
enum unit_move_type utype_move_type(const struct unit_type *punittype)
{
  return uclass_move_type(utype_class(punittype));
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
              move_points_text(punittype->move_rate, NULL, NULL, FALSE));
  if (utype_fuel(punittype)) {
    cat_snprintf(buffer, sizeof(buffer), "(%s)",
                 move_points_text(punittype->move_rate * utype_fuel(punittype),
                                  NULL, NULL, FALSE));
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
char *unit_type_flag_id_name_cb(enum unit_type_flag_id flag)
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

  if (utype_has_flag(punittype, UTYF_NUCLEAR)
      && !get_player_bonus(p, EFT_ENABLE_NUKE) > 0) {
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
  if (player_invention_state(p, advance_number(punittype->require_advance)) != TECH_KNOWN) {
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
  if (utype_has_flag(punittype, UTYF_UNIQUE)) {
    /* FIXME: This could be slow if we have lots of units. We could
     * consider keeping an array of unittypes updated with this info 
     * instead. */
    unit_list_iterate(p->units, punit) {
      if (unit_type(punit) == punittype) { 
        return FALSE;
      }
    } unit_list_iterate_end;
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
static int n_with_role[L_LAST];
static struct unit_type **with_role[L_LAST];

/**************************************************************************
Do the real work for role_unit_precalcs, for one role (or flag), given by i.
**************************************************************************/
static void precalc_one(int i,
			bool (*func_has)(const struct unit_type *, int))
{
  int j;

  /* Count: */
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

  for (i = 0; i < L_LAST; i++) {
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
    for (i = 0; i < L_LAST; i++) {
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
  first_init = FALSE;
}

/**************************************************************************
How many unit types have specified role/flag.
**************************************************************************/
int num_role_units(int role)
{
  fc_assert_ret_val((role >= 0 && role <= UTYF_LAST_USER_FLAG)
                    || (role >= L_FIRST && role < L_LAST), -1);
  fc_assert_ret_val(!first_init, -1);
  return n_with_role[role];
}

/**************************************************************************
Return index-th unit with specified role/flag.
Index -1 means (n-1), ie last one.
**************************************************************************/
struct unit_type *get_role_unit(int role, int index)
{
  fc_assert_ret_val((role >= 0 && role <= UTYF_LAST_USER_FLAG)
                    || (role >= L_FIRST && role < L_LAST), NULL);
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
                    || (role >= L_FIRST && role < L_LAST), NULL);
  fc_assert_ret_val(!first_init, NULL);

  for(j=n_with_role[role]-1; j>=0; j--) {
    u = with_role[role][j];
    if (can_city_build_unit_now(pcity, u)) {
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
                    || (role >= L_FIRST && role < L_LAST), NULL);
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
                    || (role >= L_FIRST && role < L_LAST), NULL);
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
    unit_types[i].veteran = NULL;
    unit_types[i].bonuses = combat_bonus_list_new();
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
  unit_type_iterate(punittype) {
    unit_type_free(punittype);
  } unit_type_iterate_end;
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

/***************************************************************
 Returns unit class pointer for a unit type.
***************************************************************/
struct unit_class *utype_class(const struct unit_type *punittype)
{
  fc_assert(NULL != punittype->uclass);
  return punittype->uclass;
}

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
  if (utype_has_flag(punittype, UTYF_NO_VETERAN)
      || utype_veteran_levels(punittype) <= 1) {
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

  names_set(&vlevel->name, vlist_name, NULL);
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
