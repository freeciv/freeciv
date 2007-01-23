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
#include <config.h>
#endif

#include <assert.h>
#include <string.h>

#include "astring.h"
#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "mem.h"
#include "movement.h"
#include "player.h"
#include "shared.h"
#include "support.h"
#include "tech.h"
#include "unitlist.h"

#include "unittype.h"

static struct unit_type unit_types[U_LAST];
static struct unit_class unit_classes[UCL_LAST];
/* the unit_types and unit_classes arrays are now setup in:
   server/ruleset.c (for the server)
   client/packhand.c (for the client)
*/

static const char *unit_class_flag_names[] = {
  "TerrainSpeed", "DamageSlows", "CanOccupy", "Missile",
  "RoadNative", "BuildAnywhere", "Unreachable"
};
static const char *flag_names[] = {
  "TradeRoute" ,"HelpWonder", "IgZOC", "NonMil", "IgTer", 
  "OneAttack", "Pikemen", "Horse", "IgWall", "FieldUnit", 
  "AEGIS", "AttackAny", "Marines", "Partial_Invis", "Settlers", "Diplomat",
  "Trireme", "Nuclear", "Spy", "Transform", "Paratroopers",
  "Airbase", "Cities", "No_Land_Attack",
  "AddToCity", "Fanatic", "GameLoss", "Unique", "Unbribable", 
  "Undisbandable", "SuperSpy", "NoHome", "NoVeteran", "Bombarder",
  "CityBuster", "NoBuild", "BadWallAttacker", "BadCityDefender",
  "Helicopter", "AirUnit", "Fighter"
};
static const char *role_names[] = {
  "FirstBuild", "Explorer", "Hut", "HutTech", "Partisan",
  "DefendOk", "DefendGood", "AttackFast", "AttackStrong",
  "Ferryboat", "Barbarian", "BarbarianTech", "BarbarianBoat",
  "BarbarianBuild", "BarbarianBuildTech", "BarbarianLeader",
  "BarbarianSea", "BarbarianSeaTech", "Cities", "Settlers",
  "GameLoss", "Diplomat", "Hunter"
};

/**************************************************************************
  Return a pointer for the unit type struct for the given unit type id.

  This function returns NULL for invalid unit pointers (some callers
  rely on this).
**************************************************************************/
struct unit_type *get_unit_type(Unit_type_id id)
{
  if (id < 0 || id > game.control.num_unit_types) {
    return NULL;
  }
  return &unit_types[id];
}

/**************************************************************************
  Return the unit type for this unit.
**************************************************************************/
struct unit_type *unit_type(const struct unit *punit)
{
  return punit->type;
}


/**************************************************************************
  Returns the upkeep of a unit of this type under the given government.
**************************************************************************/
int utype_upkeep_cost(const struct unit_type *ut, struct player *pplayer,
                      Output_type_id otype)
{
  int val = ut->upkeep[otype];

  if (get_player_bonus(pplayer, EFT_FANATICS)
      && BV_ISSET(ut->flags, F_FANATIC)) {
    /* Special case: fanatics have no upkeep under fanaticism. */
    return 0;
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
  Return whether the given unit type (by ID) has the flag.
**************************************************************************/
bool unit_type_flag(const struct unit_type *punittype, int flag)
{
  assert(flag>=0 && flag<F_LAST);
  return BV_ISSET(punittype->flags, flag);
}

/**************************************************************************
  Return whether the given unit class has the flag.
**************************************************************************/
bool unit_class_flag(const struct unit_class *punitclass, int flag)
{
  assert(flag >= 0 && flag < UCF_LAST);
  return BV_ISSET(punitclass->flags, flag);
}

/**************************************************************************
  Return whether the unit has the given flag.
**************************************************************************/
bool unit_flag(const struct unit *punit, enum unit_flag_id flag)
{
  return unit_type_flag(punit->type, flag);
}

/**************************************************************************
  Return whether the given unit type (by ID) has the role.  Roles are like
  flags but have no meaning except to the AI.
**************************************************************************/
bool unit_has_role(const struct unit_type *punittype, int role)
{
  assert(role>=L_FIRST && role<L_LAST);
  return BV_ISSET(punittype->roles, role - L_FIRST);
}

/****************************************************************************
  Returns the number of shields it takes to build this unit.
****************************************************************************/
int unit_build_shield_cost(const struct unit_type *punittype)
{
  return MAX(punittype->build_cost * game.info.shieldbox / 100, 1);
}

/****************************************************************************
  Returns the amount of gold it takes to rush this unit.
****************************************************************************/
int unit_buy_gold_cost(const struct unit_type *punittype,
		       int shields_in_stock)
{
  int cost = 0;
  const int missing = unit_build_shield_cost(punittype) - shields_in_stock;

  if (missing > 0) {
    cost = 2 * missing + (missing * missing) / 20;
  }
  if (shields_in_stock == 0) {
    cost *= 2;
  }
  return cost;
}

/****************************************************************************
  Returns the number of shields received when this unit is disbanded.
****************************************************************************/
int unit_disband_shields(const struct unit_type *punittype)
{
  return unit_build_shield_cost(punittype) / 2;
}

/**************************************************************************
...
**************************************************************************/
int unit_pop_value(const struct unit_type *punittype)
{
  return (punittype->pop_cost);
}

/**************************************************************************
  Return move type of the unit type
**************************************************************************/
enum unit_move_type get_unit_move_type(const struct unit_type *punittype)
{
  return get_unit_class(punittype)->move_type;
}

/**************************************************************************
  Return the (translated) name of the unit type.
**************************************************************************/
const char *unit_name(const struct unit_type *punittype)
{
  return (punittype->name);
}

/**************************************************************************
  Return the original (untranslated) name of the unit type.
**************************************************************************/
const char *unit_name_orig(const struct unit_type *punittype)
{
  return punittype->name_orig;
}

/**************************************************************************
...
**************************************************************************/
const char *get_unit_name(const struct unit_type *punittype)
{
  static char buffer[256];

  if (punittype->fuel > 0) {
    my_snprintf(buffer, sizeof(buffer),
		"%s [%d/%d/%d(%d)]", punittype->name,
		punittype->attack_strength,
		punittype->defense_strength,
		punittype->move_rate / 3,
		punittype->move_rate / 3 * punittype->fuel);
  } else {
    my_snprintf(buffer, sizeof(buffer),
		"%s [%d/%d/%d]", punittype->name, punittype->attack_strength,
		punittype->defense_strength, punittype->move_rate / 3);
  }
  return buffer;
}

/**************************************************************************
  Returns the name of the unit class.
**************************************************************************/
const char *unit_class_name(const struct unit_class *pclass)
{
  assert(pclass != NULL && &unit_classes[pclass->id] == pclass);
  return pclass->name;
}

/**************************************************************************
 Return a string with all the names of units with this flag
 Return NULL if no unit with this flag exists.
 The string must be free'd

 TODO: if there are more than 4 units with this flag return
       a fallback string (e.g. first unit name + "and similar units"
**************************************************************************/
const char *get_units_with_flag_string(int flag)
{
  int count = num_role_units(flag);

  if (count == 1) {
    return mystrdup(unit_name(get_role_unit(flag, 0)));
  }

  if (count > 0) {
    struct astring astr;

    astr_init(&astr);
    astr_minsize(&astr, 1);
    astr.str[0] = 0;

    while ((count--) > 0) {
      struct unit_type *u = get_role_unit(flag, count);
      const char *unitname = unit_name(u);

      /* there should be something like astr_append() */
      astr_minsize(&astr, astr.n + strlen(unitname));
      strcat(astr.str, unitname);

      if (count == 1) {
	const char *and_str = _(" and ");

        astr_minsize(&astr, astr.n + strlen(and_str));
        strcat(astr.str, and_str);
      } else {
        if (count != 0) {
	  const char *and_comma = Q_("?and:, ");

	  astr_minsize(&astr, astr.n + strlen(and_comma));
	  strcat(astr.str, and_comma);
        } else {
	  return astr.str;
	}
      }
    }
  }
  return NULL;
}

/**************************************************************************
  Return whether this player can upgrade this unit type (to any other
  unit type).  Returns NULL if no upgrade is possible.
**************************************************************************/
struct unit_type *can_upgrade_unittype(const struct player *pplayer,
				       const struct unit_type *punittype)
{
  struct unit_type *best_upgrade = NULL, *upgrade;

  if (!can_player_build_unit_direct(pplayer, punittype)) {
    return NULL;
  }
  while ((upgrade = punittype->obsoleted_by) != U_NOT_OBSOLETED) {
    if (can_player_build_unit_direct(pplayer, upgrade)) {
      best_upgrade = upgrade;
    }
    punittype = upgrade; /* Hack to preserve const-ness */
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
  return unit_buy_gold_cost(to, unit_disband_shields(from));
}

/**************************************************************************
  Returns the unit type that has the given (translated) name.
  Returns NULL if none match.
**************************************************************************/
struct unit_type *find_unit_type_by_name(const char *name)
{
  /* Does a linear search of unit_types[].name */
  unit_type_iterate(punittype) {
    if (strcmp(punittype->name, name) == 0) {
      return punittype;
    }
  } unit_type_iterate_end;

  return NULL;
}

/**************************************************************************
  Returns the unit type that has the given original (untranslated) name.
  Returns NULL if none match.
**************************************************************************/
struct unit_type *find_unit_type_by_name_orig(const char *name_orig)
{
  /* Does a linear search of unit_types[].name_orig. */
  unit_type_iterate(punittype) {
    if (mystrcasecmp(punittype->name_orig, name_orig) == 0) {
      return punittype;
    }
  } unit_type_iterate_end;

  return NULL;
}

/**************************************************************************
  Convert Unit_Class_id names to enum; case insensitive;
  returns NULL if can't match.
**************************************************************************/
struct unit_class *unit_class_from_str(const char *s)
{
  Unit_Class_id i;

  for (i = 0; i < UCL_LAST; i++) {
    if (mystrcasecmp(unit_classes[i].name_orig, s)==0) {
      return &unit_classes[i];
    }
  }
  return NULL;
}

/**************************************************************************
  Convert unit class flag names to enum; case insensitive;
  returns UCF_LAST if can't match.
**************************************************************************/
enum unit_class_flag_id unit_class_flag_from_str(const char *s)
{
  enum unit_class_flag_id i;

  assert(ARRAY_SIZE(unit_class_flag_names) == UCF_LAST);
  
  for(i = 0; i < UCF_LAST; i++) {
    if (mystrcasecmp(unit_class_flag_names[i], s)==0) {
      return i;
    }
  }
  return UCF_LAST;
}

/**************************************************************************
  Convert flag names to enum; case insensitive;
  returns F_LAST if can't match.
**************************************************************************/
enum unit_flag_id unit_flag_from_str(const char *s)
{
  enum unit_flag_id i;

  assert(ARRAY_SIZE(flag_names) == F_LAST);
  
  for(i=0; i<F_LAST; i++) {
    if (mystrcasecmp(flag_names[i], s)==0) {
      return i;
    }
  }
  return F_LAST;
}

/**************************************************************************
  Return the original (untranslated) name of the unit flag.
**************************************************************************/
const char *get_unit_flag_name(enum unit_flag_id id)
{
  return flag_names[id];
}

/**************************************************************************
  Convert role names to enum; case insensitive;
  returns L_LAST if can't match.
**************************************************************************/
enum unit_role_id unit_role_from_str(const char *s)
{
  enum unit_role_id i;

  assert(ARRAY_SIZE(role_names) == (L_LAST - L_FIRST));
  
  for(i=L_FIRST; i<L_LAST; i++) {
    if (mystrcasecmp(role_names[i-L_FIRST], s)==0) {
      return i;
    }
  }
  return L_LAST;
}

/**************************************************************************
Whether player can build given unit somewhere,
ignoring whether unit is obsolete and assuming the
player has a coastal city.
**************************************************************************/
bool can_player_build_unit_direct(const struct player *p,
				  const struct unit_type *punittype)
{
  Impr_type_id impr_req;

  CHECK_UNIT_TYPE(punittype);
  if (unit_type_flag(punittype, F_NUCLEAR)
      && !get_player_bonus(p, EFT_ENABLE_NUKE) > 0) {
    return FALSE;
  }
  if (unit_type_flag(punittype, F_NOBUILD)) {
    return FALSE;
  }
  if (punittype->gov_requirement
      && punittype->gov_requirement != p->government) {
    return FALSE;
  }
  if (get_invention(p,punittype->tech_requirement) != TECH_KNOWN) {
    return FALSE;
  }
  if (unit_type_flag(punittype, F_UNIQUE)) {
    /* FIXME: This could be slow if we have lots of units. We could
     * consider keeping an array of unittypes updated with this info 
     * instead. */
    unit_list_iterate(p->units, punit) {
      if (punit->type == punittype) { 
        return FALSE;
      }
    } unit_list_iterate_end;
  }

  /* If the unit has a building requirement, we check to see if the player
   * can build that building.  Note that individual cities may not have
   * that building, so they still may not be able to build the unit. */
  impr_req = punittype->impr_requirement;
  if (impr_req != B_LAST
      && !can_player_build_improvement_direct(p, impr_req)) {
    return FALSE;
  }

  return TRUE;
}

/**************************************************************************
Whether player can build given unit somewhere;
returns 0 if unit is obsolete.
**************************************************************************/
bool can_player_build_unit(const struct player *p,
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
returns 1 if unit is available with current tech OR will be available
with future tech.  returns 0 if unit is obsolete.
**************************************************************************/
bool can_player_eventually_build_unit(const struct player *p,
				      const struct unit_type *punittype)
{
  CHECK_UNIT_TYPE(punittype);
  if (unit_type_flag(punittype, F_NOBUILD)) {
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
    assert(j==n_with_role[i]);
  }
}

/**************************************************************************
Initialize; it is safe to call this multiple times (eg, if units have
changed due to rulesets in client).
**************************************************************************/
void role_unit_precalcs(void)
{
  int i;
  
  if(!first_init) {
    for(i=0; i<L_LAST; i++) {
      free(with_role[i]);
    }
  }
  for(i=0; i<L_LAST; i++) {
    with_role[i] = NULL;
    n_with_role[i] = 0;
  }

  for(i=0; i<F_LAST; i++) {
    precalc_one(i, unit_type_flag);
  }
  for(i=L_FIRST; i<L_LAST; i++) {
    precalc_one(i, unit_has_role);
  }
  first_init = FALSE;
}

/**************************************************************************
How many unit types have specified role/flag.
**************************************************************************/
int num_role_units(int role)
{
  assert((role>=0 && role<F_LAST) || (role>=L_FIRST && role<L_LAST));
  return n_with_role[role];
}

/**************************************************************************
Return index-th unit with specified role/flag.
Index -1 means (n-1), ie last one.
**************************************************************************/
struct unit_type *get_role_unit(int role, int index)
{
  assert((role>=0 && role<F_LAST) || (role>=L_FIRST && role<L_LAST));
  if (index==-1) index = n_with_role[role]-1;
  assert(index>=0 && index<n_with_role[role]);
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
  
  assert((role>=0 && role<F_LAST) || (role>=L_FIRST && role<L_LAST));

  for(j=n_with_role[role]-1; j>=0; j--) {
    u = with_role[role][j];
    if (can_build_unit(pcity, u)) {
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

  assert((role >= 0 && role < F_LAST) || (role >= L_FIRST && role < L_LAST));

  for(j = n_with_role[role]-1; j >= 0; j--) {
    struct unit_type *utype = with_role[role][j];

    if (can_player_build_unit(pplayer, utype)) {
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

  assert((role >= 0 && role < F_LAST) || (role >= L_FIRST && role < L_LAST));

  for(j = 0; j < n_with_role[role]; j++) {
    struct unit_type *utype = with_role[role][j];

    if (can_player_build_unit(pplayer, utype)) {
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

  /* Can't use unit_type_iterate or get_unit_type here because
   * num_unit_types isn't known yet. */
  for (i = 0; i < ARRAY_SIZE(unit_types); i++) {
    unit_types[i].index = i;
  }
}

/**************************************************************************
  Frees the memory associated with this unit type.
**************************************************************************/
static void unit_type_free(struct unit_type *punittype)
{
  free(punittype->helptext);
  punittype->helptext = NULL;
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

/****************************************************************************
  Returns unit class structure for an ID value.  Note the possible confusion
  with get_unit_class.
****************************************************************************/
struct unit_class *unit_class_get_by_id(int id)
{
  if (id < 0 || id >= game.control.num_unit_classes) {
    return NULL;
  }
  return &unit_classes[id];
}

/***************************************************************
 Returns unit class structure

  FIXME: this function is misnamed and will cause confusion with
  unit_class_get_by_id.
***************************************************************/
struct unit_class *get_unit_class(const struct unit_type *punittype)
{
  return punittype->class;
}

/****************************************************************************
  Inialize unit-class structures.
****************************************************************************/
void unit_classes_init(void)
{
  int i;

  /* Can't use unit_class_iterate or unit_class_get_by_id here because
   * num_unit_classes isn't known yet. */
  for (i = 0; i < ARRAY_SIZE(unit_classes); i++) {
    unit_classes[i].id = i;
  }
}
