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
#include "player.h"
#include "support.h"
#include "tech.h"

#include "unittype.h"


struct unit_type unit_types[U_LAST];
/* the unit_types array is now setup in:
   server/ruleset.c (for the server)
   client/packhand.c (for the client)
*/

static const char *move_type_names[] = {
  "Land", "Sea", "Heli", "Air"
};
static const char *flag_names[] = {
  "Caravan", "Missile", "IgZOC", "NonMil", "IgTer", "Carrier",
  "OneAttack", "Pikemen", "Horse", "IgWall", "FieldUnit", "AEGIS",
  "Fighter", "Marines", "Partial_Invis", "Settlers", "Diplomat",
  "Trireme", "Nuclear", "Spy", "Transform", "Paratroopers",
  "Airbase", "Cities", "IgTired", "Missile_Carrier", "No_Land_Attack"
};
static const char *role_names[] = {
  "FirstBuild", "Explorer", "Hut", "HutTech", "Partisan",
  "DefendOk", "DefendGood", "AttackFast", "AttackStrong",
  "Ferryboat", "Barbarian", "BarbarianTech", "BarbarianBoat",
  "BarbarianBuild", "BarbarianBuildTech", "BarbarianLeader",
  "BarbarianSea", "BarbarianSeaTech"
};
static const char *unit_class_names[] = {
  "Air",
  "Helicopter",
  "Land",
  "Missile",
  "Nuclear",
  "Sea"
};

/**************************************************************************
Returns 1 if the unit_type "exists" in this game, 0 otherwise.
A unit_type doesn't exist if one of:
- id is out of range
- the unit_type has been flagged as removed by setting its
  tech_requirement to A_LAST.
**************************************************************************/
int unit_type_exists(Unit_Type_id id)
{
  if (id<0 || id>=U_LAST || id>=game.num_unit_types)
    return 0;
  else 
    return unit_types[id].tech_requirement!=A_LAST;
}

/**************************************************************************
...
**************************************************************************/
struct unit_type *get_unit_type(Unit_Type_id id)
{
  return &unit_types[id];
}

/**************************************************************************
...
**************************************************************************/
int is_ground_unittype(Unit_Type_id id)
{
  return (unit_types[id].move_type == LAND_MOVING);
}

/**************************************************************************
...
**************************************************************************/
int is_air_unittype(Unit_Type_id id)
{
  return (unit_types[id].move_type == AIR_MOVING);
}

/**************************************************************************
...
**************************************************************************/
int is_heli_unittype(Unit_Type_id id)
{
  return (unit_types[id].move_type == HELI_MOVING);
}

/**************************************************************************
...
**************************************************************************/
int is_water_unit(Unit_Type_id id)
{
  return (unit_types[id].move_type == SEA_MOVING);
}

/**************************************************************************
...
**************************************************************************/
int utype_shield_cost(struct unit_type *ut, struct government *g)
{
  return ut->shield_cost * g->unit_shield_cost_factor;
}

/**************************************************************************
...
**************************************************************************/
int utype_food_cost(struct unit_type *ut, struct government *g)
{
  return ut->food_cost * g->unit_food_cost_factor;
}

/**************************************************************************
...
**************************************************************************/
int utype_happy_cost(struct unit_type *ut, struct government *g)
{
  return ut->happy_cost * g->unit_happy_cost_factor;
}

/**************************************************************************
...
**************************************************************************/
int utype_gold_cost(struct unit_type *ut, struct government *g)
{
  return ut->gold_cost * g->unit_gold_cost_factor;
}

/**************************************************************************
...
**************************************************************************/
int unit_flag(Unit_Type_id id, int flag)
{
  assert(flag>=0 && flag<F_LAST);
  return BOOL_VAL(unit_types[id].flags & (1<<flag));
}

/**************************************************************************
...
**************************************************************************/
int unit_has_role(Unit_Type_id id, int role)
{
  assert(role>=L_FIRST && role<L_LAST);
  return BOOL_VAL(unit_types[id].roles & (1<<(role-L_FIRST)));
}

/**************************************************************************
...
**************************************************************************/
int unit_value(Unit_Type_id id)
{
  return (unit_types[id].build_cost);
}

/**************************************************************************
...
**************************************************************************/
char *unit_name(Unit_Type_id id)
{
  return (unit_types[id].name);
}

/**************************************************************************
...
**************************************************************************/
char *get_unit_name(Unit_Type_id id)
{
  struct unit_type *ptype;
  static char buffer[256];
  ptype =get_unit_type(id);
  if (ptype->fuel) {
    my_snprintf(buffer, sizeof(buffer),
		"%s [%d/%d/%d(%d)]", ptype->name, ptype->attack_strength,
		ptype->defense_strength,
		ptype->move_rate/3,(ptype->move_rate/3)*ptype->fuel);
  } else {
    my_snprintf(buffer, sizeof(buffer),
		"%s [%d/%d/%d]", ptype->name, ptype->attack_strength,
		ptype->defense_strength, ptype->move_rate/3);
  }
  return buffer;
}

/**************************************************************************
...
**************************************************************************/
const char *unit_class_name(Unit_Class_id id)
{
  if ((id >= 0) && (id < UCL_LAST)) {
    return unit_class_names[id];
  } else {
    return "";
  }
}

/**************************************************************************
 Return a string with all the names of units with this flag
 Return NULL if no unit with this flag exists.
 The string must be free'd

 TODO: if there are more than 4 units with this flag return
       a fallback string (e.g. first unit name + "and similar units"
**************************************************************************/
char *get_units_with_flag_string(int flag)
{
  int count=num_role_units(flag);

  if(count==1)
    return mystrdup(unit_name(get_role_unit(flag,0)));

  if(count) {
    struct astring astr;

    astr_init(&astr);
    astr_minsize(&astr,1);
    astr.str[0] = 0;

    while(count--) {
      int u = get_role_unit(flag,count);
      char *unitname = unit_name(u);

      /* there should be something like astr_append() */
      astr_minsize(&astr,astr.n+strlen(unitname));
      strcat(astr.str,unitname);

      if(count==1) {
	char *and_str = _(" and ");
        astr_minsize(&astr,astr.n+strlen(and_str));
        strcat(astr.str, and_str);
      }
      else {
        if(count != 0) {
          astr_minsize(&astr,astr.n+strlen(", "));
          strcat(astr.str,", ");
        }
        else return astr.str;
      }
    }
  }
  return NULL;
}

/**************************************************************************
...
**************************************************************************/
int can_upgrade_unittype(struct player *pplayer, Unit_Type_id id)
{
  Unit_Type_id best_upgrade = -1;

  if (!can_player_build_unit_direct(pplayer, id))
    return -1;
  while (unit_type_exists(id = unit_types[id].obsoleted_by))
    if (can_player_build_unit_direct(pplayer, id))
      best_upgrade = id;

  return best_upgrade;
}

/**************************************************************************
...
**************************************************************************/
int unit_upgrade_price(struct player *pplayer, Unit_Type_id from,
		       Unit_Type_id to)
{
  int total, build;
  build = unit_value(from)/2;
  total = unit_value(to);
  if (build>=total)
    return 0;
  return (total-build)*2+(total-build)*(total-build)/20; 
}

/**************************************************************************
Does a linear search of unit_types[].name
Returns U_LAST if none match.
**************************************************************************/
Unit_Type_id find_unit_type_by_name(char *s)
{
  int i;

  for( i=0; i<game.num_unit_types; i++ ) {
    if (strcmp(unit_types[i].name, s)==0)
      return i;
  }
  return U_LAST;
}

/**************************************************************************
  Convert unit_move_type names to enum; case insensitive;
  returns 0 if can't match.
**************************************************************************/
enum unit_move_type unit_move_type_from_str(char *s)
{
  enum unit_move_type i;

  /* a compile-time check would be nicer, but this will do: */
  assert(sizeof(move_type_names)/sizeof(char*)==AIR_MOVING-LAND_MOVING+1);

  for(i=LAND_MOVING; i<=AIR_MOVING; i++) {
    if (mystrcasecmp(move_type_names[i-LAND_MOVING], s)==0) {
      return i;
    }
  }
  return 0;
}

/**************************************************************************
  Convert Unit_Class_id names to enum; case insensitive;
  returns UCL_LAST if can't match.
**************************************************************************/
Unit_Class_id unit_class_from_str(char *s)
{
  Unit_Class_id i;

  assert(sizeof(unit_class_names)/sizeof(unit_class_names[0])==UCL_LAST);

  for (i = 0; i < UCL_LAST; i++) {
    if (mystrcasecmp(unit_class_names[i], s)==0) {
      return i;
    }
  }
  return UCL_LAST;
}

/**************************************************************************
  Convert flag names to enum; case insensitive;
  returns F_LAST if can't match.
**************************************************************************/
enum unit_flag_id unit_flag_from_str(char *s)
{
  enum unit_flag_id i;

  assert(sizeof(flag_names)/sizeof(char*)==F_LAST);
  
  for(i=0; i<F_LAST; i++) {
    if (mystrcasecmp(flag_names[i], s)==0) {
      return i;
    }
  }
  return F_LAST;
}

/**************************************************************************
  Convert role names to enum; case insensitive;
  returns L_LAST if can't match.
**************************************************************************/
enum unit_role_id unit_role_from_str(char *s)
{
  enum unit_role_id i;

  assert(sizeof(role_names)/sizeof(char*)==L_LAST-L_FIRST);
  
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
int can_player_build_unit_direct(struct player *p, Unit_Type_id id)
{  
  if (!unit_type_exists(id))
    return 0;
  if (unit_flag(id, F_NUCLEAR) && !game.global_wonders[B_MANHATTEN])
    return 0;
  if (get_invention(p,unit_types[id].tech_requirement)!=TECH_KNOWN)
    return 0;
  return 1;
}

/**************************************************************************
Whether player can build given unit somewhere;
returns 0 if unit is obsolete.
**************************************************************************/
int can_player_build_unit(struct player *p, Unit_Type_id id)
{  
  if (!can_player_build_unit_direct(p, id))
    return 0;
  while(unit_type_exists((id = unit_types[id].obsoleted_by)))
    if (can_player_build_unit_direct(p, id))
	return 0;
  return 1;
}

/**************************************************************************
Whether player can _eventually_ build given unit somewhere -- ie,
returns 1 if unit is available with current tech OR will be available
with future tech.  returns 0 if unit is obsolete.
**************************************************************************/
int can_player_eventually_build_unit(struct player *p, Unit_Type_id id)
{
  if (!unit_type_exists(id))
    return 0;
  while(unit_type_exists((id = unit_types[id].obsoleted_by)))
    if (can_player_build_unit_direct(p, id))
	return 0;
  return 1;
}

/**************************************************************************
The following functions use static variables so we can quickly look up
which unit types have given flag or role.
For these functions flags and roles are considered to be in the same "space",
and any "role" argument can also be a "flag".
Only units which pass unit_type_exists are counted.
Unit order is in terms of the order in the units ruleset.
**************************************************************************/
static int first_init = 1;
static int n_with_role[L_LAST];
static Unit_Type_id *with_role[L_LAST];

/**************************************************************************
Do the real work for role_unit_precalcs, for one role (or flag), given by i.
**************************************************************************/
static void precalc_one(int i, int (*func_has)(Unit_Type_id, int))
{
  Unit_Type_id u;
  int j;

  /* Count: */
  for(u=0; u<game.num_unit_types; u++) {
    if(unit_type_exists(u) && func_has(u, i)) {
      n_with_role[i]++;
    }
  }
  if(n_with_role[i] > 0) {
    with_role[i] = fc_malloc(n_with_role[i]*sizeof(Unit_Type_id));
    for(j=0, u=0; u<game.num_unit_types; u++) {
      if(unit_type_exists(u) && func_has(u, i)) {
	with_role[i][j++] = u;
      }
    }
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
    precalc_one(i, unit_flag);
  }
  for(i=L_FIRST; i<L_LAST; i++) {
    precalc_one(i, unit_has_role);
  }
  first_init = 0;
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
Unit_Type_id get_role_unit(int role, int index)
{
  assert((role>=0 && role<F_LAST) || (role>=L_FIRST && role<L_LAST));
  if (index==-1) index = n_with_role[role]-1;
  assert(index>=0 && index<n_with_role[role]);
  return with_role[role][index];
}

/**************************************************************************
Return "best" unit this city can build, with given role/flag.
Returns U_LAST if none match.
**************************************************************************/
Unit_Type_id best_role_unit(struct city *pcity, int role)
{
  Unit_Type_id u;
  int j;
  
  assert((role>=0 && role<F_LAST) || (role>=L_FIRST && role<L_LAST));

  for(j=n_with_role[role]-1; j>=0; j--) {
    u = with_role[role][j];
    if (can_build_unit(pcity, u)) {
      return u;
    }
  }
  return U_LAST;
}

