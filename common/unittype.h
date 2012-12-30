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
#ifndef FC__UNITTYPE_H
#define FC__UNITTYPE_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* utility */
#include "bitvector.h"
#include "shared.h"

/* common */
#include "fc_types.h"
#include "name_translation.h"

struct astring;         /* Actually defined in "utility/astring.h". */
struct strvec;          /* Actually defined in "utility/string_vector.h". */

struct ai_type;

#define U_LAST MAX_NUM_ITEMS
/*
  U_LAST is a value which is guaranteed to be larger than all
  actual Unit_type_id values.  It is used as a flag value;
  it can also be used for fixed allocations to ensure able
  to hold full number of unit types.
*/

#define SPECENUM_NAME unit_class_flag_id
#define SPECENUM_VALUE0 UCF_TERRAIN_SPEED
#define SPECENUM_VALUE0NAME "TerrainSpeed"
#define SPECENUM_VALUE1 UCF_TERRAIN_DEFENSE
#define SPECENUM_VALUE1NAME "TerrainDefense"
#define SPECENUM_VALUE2 UCF_DAMAGE_SLOWS
#define SPECENUM_VALUE2NAME "DamageSlows"
/* Can occupy enemy cities */
#define SPECENUM_VALUE3 UCF_CAN_OCCUPY_CITY
#define SPECENUM_VALUE3NAME "CanOccupyCity"
#define SPECENUM_VALUE4 UCF_MISSILE
#define SPECENUM_VALUE4NAME "Missile"
/* Considers any river tile native terrain */
#define SPECENUM_VALUE5 UCF_RIVER_NATIVE
#define SPECENUM_VALUE5NAME "RiverNative"
#define SPECENUM_VALUE6 UCF_BUILD_ANYWHERE
#define SPECENUM_VALUE6NAME "BuildAnywhere"
#define SPECENUM_VALUE7 UCF_UNREACHABLE
#define SPECENUM_VALUE7NAME "Unreachable"
/* Can collect ransom from barbarian leader */
#define SPECENUM_VALUE8 UCF_COLLECT_RANSOM
#define SPECENUM_VALUE8NAME "CollectRansom"
/* Is subject to ZOC */
#define SPECENUM_VALUE9 UCF_ZOC
#define SPECENUM_VALUE9NAME "ZOC"
/* Can fortify on land squares */
#define SPECENUM_VALUE10 UCF_CAN_FORTIFY
#define SPECENUM_VALUE10NAME "CanFortify"
#define SPECENUM_VALUE11 UCF_CAN_PILLAGE
#define SPECENUM_VALUE11NAME "CanPillage"
/* Cities can still work tile when enemy unit on it */
#define SPECENUM_VALUE12 UCF_DOESNT_OCCUPY_TILE
#define SPECENUM_VALUE12NAME "DoesntOccupyTile"
/* Can attack against units on non-native tiles */
#define SPECENUM_VALUE13 UCF_ATTACK_NON_NATIVE
#define SPECENUM_VALUE13NAME "AttackNonNative"
/* Can launch attack from non-native tile (to native tile) */
#define SPECENUM_VALUE14 UCF_ATT_FROM_NON_NATIVE
#define SPECENUM_VALUE14NAME "AttFromNonNative"
/* keep this last */
#define SPECENUM_COUNT UCF_COUNT
#include "specenum_gen.h"

BV_DEFINE(bv_unit_classes, UCL_LAST);
BV_DEFINE(bv_unit_class_flags, UCF_COUNT);

enum hut_behavior { HUT_NORMAL, HUT_NOTHING, HUT_FRIGHTEN };

enum move_level { MOVE_NONE, MOVE_PARTIAL, MOVE_FULL };

struct unit_class {
  Unit_Class_id item_number;
  struct name_translation name;
  enum unit_move_type move_type;
  int min_speed;           /* Minimum speed after damage and effects */
  int hp_loss_pct;         /* Percentage of hitpoints lost each turn not in city or airbase */
  enum hut_behavior hut_behavior;
  bv_unit_class_flags flags;

  struct {
    enum move_level land_move;
    enum move_level sea_move;
  } adv;
};

/* Unit "special effects" flags:
   Note this is now an enumerated type, and not power-of-two integers
   for bits, though unit_type.flags is still a bitfield, and code
   which uses unit_has_type_flag() without twiddling bits is unchanged.
   (It is easier to go from i to (1<<i) than the reverse.)
   See data/default/units.ruleset for documentation of their effects.
   Change the array *flag_names[] in unittype.c accordingly.
*/
#define SPECENUM_NAME unit_type_flag_id
#define SPECENUM_VALUE0 UTYF_TRADE_ROUTE
#define SPECENUM_VALUE0NAME "TradeRoute"
#define SPECENUM_VALUE1 UTYF_HELP_WONDER
#define SPECENUM_VALUE1NAME "HelpWonder"
#define SPECENUM_VALUE2 UTYF_IGZOC
#define SPECENUM_VALUE2NAME "IgZOC"
#define SPECENUM_VALUE3 UTYF_CIVILIAN
#define SPECENUM_VALUE3NAME "NonMil"
#define SPECENUM_VALUE4 UTYF_IGTER
#define SPECENUM_VALUE4NAME "IgTer"
#define SPECENUM_VALUE5 UTYF_ONEATTACK
#define SPECENUM_VALUE5NAME "OneAttack"
/* Ignores EFT_DEFEND_BONUS (for example city walls) */
#define SPECENUM_VALUE6 UTYF_IGWALL
#define SPECENUM_VALUE6NAME "IgWall"
#define SPECENUM_VALUE7 UTYF_FIELDUNIT
#define SPECENUM_VALUE7NAME "FieldUnit"
#define SPECENUM_VALUE8 UTYF_MARINES
#define SPECENUM_VALUE8NAME "Marines"
/* Invisibile except when adjacent (Submarine) */
#define SPECENUM_VALUE9 UTYF_PARTIAL_INVIS
#define SPECENUM_VALUE9NAME "Partial_Invis"
/* Does not include ability to found cities */
#define SPECENUM_VALUE10 UTYF_SETTLERS
#define SPECENUM_VALUE10NAME "Settlers"
#define SPECENUM_VALUE11 UTYF_DIPLOMAT
#define SPECENUM_VALUE11NAME "Diplomat"
/* Trireme sinking effect */
#define SPECENUM_VALUE12 UTYF_TRIREME
#define SPECENUM_VALUE12NAME "Trireme"
/* Nuclear attack effect */
#define SPECENUM_VALUE13 UTYF_NUCLEAR
#define SPECENUM_VALUE13NAME "Nuclear"
/* Enhanced spy abilities */
#define SPECENUM_VALUE14 UTYF_SPY
#define SPECENUM_VALUE14NAME "Spy"
#define SPECENUM_VALUE15 UTYF_PARATROOPERS
#define SPECENUM_VALUE15NAME "Paratroopers"
/* Can build cities */
#define SPECENUM_VALUE16 UTYF_CITIES
#define SPECENUM_VALUE16NAME "Cities"
/* Cannot attack vs non-native tiles even if class can */
#define SPECENUM_VALUE17 UTYF_ONLY_NATIVE_ATTACK
#define SPECENUM_VALUE17NAME "Only_Native_Attack"
/* unit can add to city population */
#define SPECENUM_VALUE18 UTYF_ADD_TO_CITY
#define SPECENUM_VALUE18NAME "AddToCity"
/* Only Fundamentalist government can build these units */
#define SPECENUM_VALUE19 UTYF_FANATIC
#define SPECENUM_VALUE19NAME "Fanatic"
/* Losing this unit means losing the game */
#define SPECENUM_VALUE20 UTYF_GAMELOSS
#define SPECENUM_VALUE20NAME "GameLoss"
/* A player can only have one unit of this type */
#define SPECENUM_VALUE21 UTYF_UNIQUE
#define SPECENUM_VALUE21NAME "Unique"
/* Cannot be bribed */
#define SPECENUM_VALUE22 UTYF_UNBRIBABLE
#define SPECENUM_VALUE22NAME "Unbribable"
/* Cannot be disbanded, won't easily go away */
#define SPECENUM_VALUE23 UTYF_UNDISBANDABLE
#define SPECENUM_VALUE23NAME "Undisbandable"
/* Always wins diplomatic contests */
#define SPECENUM_VALUE24 UTYF_SUPERSPY
#define SPECENUM_VALUE24NAME "SuperSpy"
/* Has no homecity */
#define SPECENUM_VALUE25 UTYF_NOHOME
#define SPECENUM_VALUE25NAME "NoHome"
/* Cannot increase veteran level */
#define SPECENUM_VALUE26 UTYF_NO_VETERAN
#define SPECENUM_VALUE26NAME "NoVeteran"
/* Has the ability to bombard */
#define SPECENUM_VALUE27 UTYF_BOMBARDER
#define SPECENUM_VALUE27NAME "Bombarder"
/* Gets double firepower against cities */
#define SPECENUM_VALUE28 UTYF_CITYBUSTER
#define SPECENUM_VALUE28NAME "CityBuster"
/* Unit cannot be built (barb leader etc) */
#define SPECENUM_VALUE29 UTYF_NOBUILD
#define SPECENUM_VALUE29NAME "NoBuild"
/* Firepower set to 1 when EFT_DEFEND_BONUS applies
 * (for example, land unit attacking city with walls) */
#define SPECENUM_VALUE30 UTYF_BADWALLATTACKER
#define SPECENUM_VALUE30NAME "BadWallAttacker"
/* Firepower set to 1 and attackers x2 when in city */
#define SPECENUM_VALUE31 UTYF_BADCITYDEFENDER
#define SPECENUM_VALUE31NAME "BadCityDefender"
/* Only barbarians can build this unit */
#define SPECENUM_VALUE32 UTYF_BARBARIAN_ONLY
#define SPECENUM_VALUE32NAME "BarbarianOnly"
/* upkeep can switch from shield to gold */
#define SPECENUM_VALUE33 UTYF_SHIELD2GOLD
#define SPECENUM_VALUE33NAME "Shield2Gold"
/* Unit can be captured */
#define SPECENUM_VALUE34 UTYF_CAPTURABLE
#define SPECENUM_VALUE34NAME "Capturable"
/* Unit can capture other */
#define SPECENUM_VALUE35 UTYF_CAPTURER
#define SPECENUM_VALUE35NAME "Capturer"

#define SPECENUM_VALUE36 UTYF_USER_FLAG_1
#define SPECENUM_VALUE37 UTYF_USER_FLAG_2
#define SPECENUM_VALUE38 UTYF_USER_FLAG_3
#define SPECENUM_VALUE39 UTYF_USER_FLAG_4
#define SPECENUM_VALUE40 UTYF_USER_FLAG_5
#define SPECENUM_VALUE41 UTYF_USER_FLAG_6
#define SPECENUM_VALUE42 UTYF_USER_FLAG_7
#define SPECENUM_VALUE43 UTYF_USER_FLAG_8
#define SPECENUM_VALUE44 UTYF_USER_FLAG_9
#define SPECENUM_VALUE45 UTYF_USER_FLAG_10
#define SPECENUM_VALUE46 UTYF_USER_FLAG_11
#define SPECENUM_VALUE47 UTYF_USER_FLAG_12
#define SPECENUM_VALUE48 UTYF_USER_FLAG_13
#define SPECENUM_VALUE49 UTYF_USER_FLAG_14
#define SPECENUM_VALUE50 UTYF_USER_FLAG_15
#define SPECENUM_VALUE51 UTYF_USER_FLAG_16

#define UTYF_LAST_USER_FLAG UTYF_USER_FLAG_16
#define MAX_NUM_USER_UNIT_FLAGS (UTYF_LAST_USER_FLAG - UTYF_USER_FLAG_1 + 1)
#define SPECENUM_NAMEOVERRIDE
#include "specenum_gen.h"

#define UTYF_MAX 64


/* Unit "roles": these are similar to unit flags but differ in that
   they don't represent intrinsic properties or abilities of units,
   but determine which units are used (mainly by the server or AI)
   in various circumstances, or "roles".
   Note that in some cases flags can act as roles, eg, we don't need
   a role for "settlers", because we can just use UTYF_SETTLERS.
   (Now have to consider UTYF_CITIES too)
   So we make sure flag values and role values are distinct,
   so some functions can use them interchangably.
   See data/classic/units.ruleset for documentation of their effects.
*/
#define L_FIRST UTYF_MAX
enum unit_role_id {
  L_FIRSTBUILD=L_FIRST, /* is built first when city established */
  L_EXPLORER,           /* initial explorer unit */
  L_HUT,                /* can be found in hut */
  L_HUT_TECH,           /* can be found in hut, global tech required */
  L_PARTISAN,           /* is created in Partisan circumstances */
  L_DEFEND_OK,          /* ok on defense (AI) */
  L_DEFEND_GOOD,        /* primary purpose is defense (AI) */
  L_ATTACK_FAST,        /* quick attacking unit (Horse..Armor) (unused)*/
  L_ATTACK_STRONG,      /* powerful attacking unit (Catapult..) (unused) */
  L_FERRYBOAT,	        /* is useful for ferrying (AI) */
  L_BARBARIAN,          /* barbarians unit, land only */
  L_BARBARIAN_TECH,     /* barbarians unit, global tech required */
  L_BARBARIAN_BOAT,     /* barbarian boat */
  L_BARBARIAN_BUILD,    /* what barbarians should build */
  L_BARBARIAN_BUILD_TECH, /* barbarians build when global tech */
  L_BARBARIAN_LEADER,   /* barbarian leader */
  L_BARBARIAN_SEA,      /* sea raider unit */
  L_BARBARIAN_SEA_TECH, /* sea raider unit, global tech required */
  L_CITIES,		/* can found cities */
  L_SETTLERS,		/* can improve terrain */
  L_GAMELOSS,		/* loss results in loss of game */
  L_DIPLOMAT,		/* can do diplomat actions */
  L_HUNTER,             /* AI hunter type unit */
  L_LAST
};
#define L_MAX 64

BV_DEFINE(bv_unit_type_flags, UTYF_MAX);
BV_DEFINE(bv_unit_type_roles, L_MAX);

#define SPECENUM_NAME combat_bonus_type
#define SPECENUM_VALUE0 CBONUS_DEFENSE_MULTIPLIER
#define SPECENUM_VALUE0NAME "DefenseMultiplier"
#define SPECENUM_VALUE1 CBONUS_DEFENSE_DIVIDER
#define SPECENUM_VALUE1NAME "DefenseDivider"
#define SPECENUM_VALUE2 CBONUS_FIREPOWER1
#define SPECENUM_VALUE2NAME "Firepower1"
#include "specenum_gen.h"

struct combat_bonus {
  enum unit_type_flag_id  flag;
  enum combat_bonus_type  type;
  int                     value;
};

/* get 'struct combat_bonus_list' and related functions: */
#define SPECLIST_TAG combat_bonus
#define SPECLIST_TYPE struct combat_bonus
#include "speclist.h"

#define combat_bonus_list_iterate(bonuslist, pbonus) \
    TYPED_LIST_ITERATE(struct combat_bonus, bonuslist, pbonus)
#define combat_bonus_list_iterate_end LIST_ITERATE_END

struct veteran_level {
  struct name_translation name; /* level/rank name */
  int power_fact; /* combat/work speed/diplomatic power factor (in %) */
  int move_bonus;
  int raise_chance; /* server only */
  int work_raise_chance; /* server only */
};

struct veteran_system {
  int levels;

  struct veteran_level *definitions;
};

struct unit_type {
  Unit_type_id item_number;
  struct name_translation name;
  char graphic_str[MAX_LEN_NAME];
  char graphic_alt[MAX_LEN_NAME];
  char sound_move[MAX_LEN_NAME];
  char sound_move_alt[MAX_LEN_NAME];
  char sound_fight[MAX_LEN_NAME];
  char sound_fight_alt[MAX_LEN_NAME];
  int build_cost;			/* Use wrappers to access this. */
  int pop_cost;  /* number of workers the unit contains (e.g., settlers, engineers)*/
  int attack_strength;
  int defense_strength;
  int move_rate;

  struct advance *require_advance;	/* may be NULL */
  struct impr_type *need_improvement;	/* may be NULL */
  struct government *need_government;	/* may be NULL */

  int vision_radius_sq;
  int transport_capacity;
  int hp;
  int firepower;
  struct combat_bonus_list *bonuses;

#define U_NOT_OBSOLETED (NULL)
  struct unit_type *obsoleted_by;
  struct unit_type *converted_to;
  int convert_time;
  int fuel;

  bv_unit_type_flags flags;
  bv_unit_type_roles roles;

  int happy_cost;  /* unhappy people in home city */
  int upkeep[O_LAST];

  int paratroopers_range; /* only valid for F_PARATROOPERS */
  int paratroopers_mr_req;
  int paratroopers_mr_sub;

  /* Additional values for the expanded veteran system */
  struct veteran_system *veteran;

  /* Values for bombardment */
  int bombard_rate;

  /* Values for founding cities */
  int city_size;

  struct unit_class *uclass;

  bv_unit_classes cargo;

  bv_unit_classes targets; /* Can attack these classes even if they are otherwise "Unreachable" */

  struct strvec *helptext;

  void *ais[FC_AI_LAST];
};

/* General unit and unit type (matched) routines */
Unit_type_id utype_count(void);
Unit_type_id utype_index(const struct unit_type *punittype);
Unit_type_id utype_number(const struct unit_type *punittype);

struct unit_type *unit_type(const struct unit *punit);
struct unit_type *utype_by_number(const Unit_type_id id);

struct unit_type *unit_type_by_rule_name(const char *name);
struct unit_type *unit_type_by_translated_name(const char *name);

const char *unit_rule_name(const struct unit *punit);
const char *utype_rule_name(const struct unit_type *punittype);

const char *unit_name_translation(const struct unit *punit);
const char *utype_name_translation(const struct unit_type *punittype);

const char *utype_values_string(const struct unit_type *punittype);
const char *utype_values_translation(const struct unit_type *punittype);

/* General unit type flag and role routines */
bool unit_has_type_flag(const struct unit *punit, enum unit_type_flag_id flag);
bool utype_has_flag(const struct unit_type *punittype, int flag);

bool unit_has_type_role(const struct unit *punit, enum unit_role_id role);
bool utype_has_role(const struct unit_type *punittype, int role);

enum unit_role_id unit_role_by_rule_name(const char *s);

void user_unit_type_flags_init(void);
void set_user_unit_type_flag_name(enum unit_type_flag_id id, const char *name,
                                  const char *helptxt);
const char *unit_type_flag_helptxt(enum unit_type_flag_id id);
const char *unit_role_rule_name(enum unit_role_id id);

bool unit_can_take_over(const struct unit *punit);
bool utype_can_take_over(const struct unit_type *punittype);

/* Functions to operate on various flag and roles. */
void role_unit_precalcs(void);
void role_unit_precalcs_free(void);
int num_role_units(int role);
struct unit_type *get_role_unit(int role, int index);
struct unit_type *best_role_unit(const struct city *pcity, int role);
struct unit_type *best_role_unit_for_player(const struct player *pplayer,
					    int role);
struct unit_type *first_role_unit_for_player(const struct player *pplayer,
					     int role);
bool role_units_translations(struct astring *astr, int flag, bool alts);

/* General unit class routines */
Unit_Class_id uclass_count(void);
Unit_Class_id uclass_index(const struct unit_class *pclass);
Unit_Class_id uclass_number(const struct unit_class *pclass);

struct unit_class *unit_class(const struct unit *punit);
struct unit_class *utype_class(const struct unit_type *punittype);
struct unit_class *uclass_by_number(const Unit_Class_id id);

struct unit_class *unit_class_by_rule_name(const char *s);

const char *uclass_rule_name(const struct unit_class *pclass);
const char *uclass_name_translation(const struct unit_class *pclass);

bool uclass_has_flag(const struct unit_class *punitclass,
                     enum unit_class_flag_id flag);

/* Ancillary routines */
int unit_build_shield_cost(const struct unit *punit);
int utype_build_shield_cost(const struct unit_type *punittype);

int utype_buy_gold_cost(const struct unit_type *punittype,
			int shields_in_stock);

const struct veteran_system *
  utype_veteran_system(const struct unit_type *punittype);
int utype_veteran_levels(const struct unit_type *punittype);
const struct veteran_level *
  utype_veteran_level(const struct unit_type *punittype, int level);
const char *utype_veteran_name_translation(const struct unit_type *punittype,
                                           int level);
bool utype_veteran_has_power_bonus(const struct unit_type *punittype);

struct veteran_system *veteran_system_new(int count);
void veteran_system_destroy(struct veteran_system *vsystem);
void veteran_system_definition(struct veteran_system *vsystem, int level,
                               const char *vlist_name, int vlist_power,
                               int vlist_move, int vlist_raise,
                               int vlist_wraise);

int unit_disband_shields(const struct unit *punit);
int utype_disband_shields(const struct unit_type *punittype);

int unit_pop_value(const struct unit *punit);
int utype_pop_value(const struct unit_type *punittype);

enum unit_move_type utype_move_type(const struct unit_type *punittype);
enum unit_move_type uclass_move_type(const struct unit_class *pclass);

/* player related unit functions */
int utype_upkeep_cost(const struct unit_type *ut, struct player *pplayer,
                      Output_type_id otype);
int utype_happy_cost(const struct unit_type *ut, const struct player *pplayer);

struct unit_type *can_upgrade_unittype(const struct player *pplayer,
				       struct unit_type *punittype);
int unit_upgrade_price(const struct player *pplayer,
		       const struct unit_type *from,
		       const struct unit_type *to);

bool can_player_build_unit_direct(const struct player *p,
				  const struct unit_type *punittype);
bool can_player_build_unit_later(const struct player *p,
				 const struct unit_type *punittype);
bool can_player_build_unit_now(const struct player *p,
			       const struct unit_type *punittype);

#define utype_fuel(ptype) (ptype)->fuel

/* Initialization and iteration */
void unit_types_init(void);
void unit_types_free(void);
void unit_type_flags_free(void);

struct unit_type *unit_type_array_first(void);
const struct unit_type *unit_type_array_last(void);

#define unit_type_iterate(_p)						\
{									\
  struct unit_type *_p = unit_type_array_first();			\
  if (NULL != _p) {							\
    for (; _p <= unit_type_array_last(); _p++) {

#define unit_type_iterate_end						\
    }									\
  }									\
}

void *utype_ai_data(const struct unit_type *ptype, const struct ai_type *ai);
void utype_set_ai_data(struct unit_type *ptype, const struct ai_type *ai,
                       void *data);

/* Initialization and iteration */
void unit_classes_init(void);

struct unit_class *unit_class_array_first(void);
const struct unit_class *unit_class_array_last(void);

#define unit_class_iterate(_p)						\
{									\
  struct unit_class *_p = unit_class_array_first();			\
  if (NULL != _p) {							\
    for (; _p <= unit_class_array_last(); _p++) {

#define unit_class_iterate_end						\
    }									\
  }									\
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__UNITTYPE_H */
