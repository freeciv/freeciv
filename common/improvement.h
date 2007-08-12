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
#ifndef FC__IMPROVEMENT_H
#define FC__IMPROVEMENT_H

/* City Improvements, including Wonders.  (Alternatively "Buildings".) */

#include "shared.h"		/* MAX_LEN_NAME */

#include "effects.h"
#include "fc_types.h"
#include "tech.h"		/* Tech_type_id */

/* B_LAST is a value which is guaranteed to be larger than all
 * actual Impr_type_id values.  It is used as a flag value;
 * it can also be used for fixed allocations to ensure ability
 * to hold full number of improvement types.  */
#define B_LAST MAX_NUM_ITEMS

/* Improvement status (for cities' lists of improvements)
 * An enum or bitfield would be neater here, but we use a typedef for
 * a) less memory usage and b) compatibility with old behaviour */
typedef unsigned char Impr_Status;
#define I_NONE       0   /* Improvement not built */
#define I_ACTIVE     1   /* Improvement built, and having its effect */


/* Changing these breaks network compatibility. */
enum impr_flag_id {
  IF_VISIBLE_BY_OTHERS,  /* improvement should be visible to others without spying */
  IF_SAVE_SMALL_WONDER,  /* this small wonder is moved to another city if game.savepalace is on. */
  IF_GOLD,		 /* when built, gives gold */
  IF_LAST
};

enum impr_genus_id {
  IG_GREAT_WONDER,
  IG_SMALL_WONDER,
  IG_IMPROVEMENT,
  IG_SPECIAL,
  IG_LAST
};

BV_DEFINE(bv_imprs, B_LAST);

/* Type of improvement. (Read from buildings.ruleset file.) */
struct impr_type {
  int index;  /* Index in improvement_types array */
  struct name_translation name;
  char graphic_str[MAX_LEN_NAME];	/* city icon of improv. */
  char graphic_alt[MAX_LEN_NAME];	/* city icon of improv. */
  struct requirement_vector reqs;
  struct advance *obsolete_by;		/* A_NEVER = never obsolete */
  Impr_type_id replaced_by;		/* B_LAST = never replaced */
  int build_cost;			/* Use wrappers to access this. */
  int upkeep;
  int sabotage;		/* Base chance of diplomat sabotage succeeding. */
  enum impr_genus_id genus;		/* genus; e.g. GreatWonder */
  unsigned int flags;
  char *helptext;
  char soundtag[MAX_LEN_NAME];
  char soundtag_alt[MAX_LEN_NAME];
};


/* General improvement accessor functions. */
struct impr_type *improvement_by_number(const Impr_type_id id);
bool improvement_exists(Impr_type_id id);

Impr_type_id find_improvement_by_rule_name(const char *s);
Impr_type_id find_improvement_by_translated_name(const char *s);

const char *improvement_rule_name(Impr_type_id id);
const char *improvement_name_translation(Impr_type_id id);

/* General improvement flag accessor routines */
bool improvement_has_flag(Impr_type_id id, enum impr_flag_id flag);
enum impr_flag_id find_improvement_flag_by_rule_name(const char *s);

/* Ancillary routines */
int impr_build_shield_cost(Impr_type_id id);
int impr_buy_gold_cost(Impr_type_id id, int shields_in_stock);
int impr_sell_gold(Impr_type_id id);

bool is_improvement_visible(Impr_type_id id);

/* player related improvement functions */
bool improvement_obsolete(const struct player *pplayer, Impr_type_id id);

bool can_player_build_improvement_direct(const struct player *p,
					 Impr_type_id id);
bool can_player_build_improvement(const struct player *p, Impr_type_id id);
bool can_player_eventually_build_improvement(const struct player *p,
					     Impr_type_id id);

/* General genus accessor routines */
enum impr_genus_id find_genus_by_rule_name(const char *s);

/* Initialization and iteration */
void improvements_init(void);
void improvements_free(void);

/* Iterates over all improvements. Creates a new variable names m_i
 * with type Impr_type_id which holds the id of the current improvement. */
#define impr_type_iterate(m_i)                                                \
{                                                                             \
  Impr_type_id m_i;                                                           \
  for (m_i = 0; m_i < game.control.num_impr_types; m_i++) {

#define impr_type_iterate_end                                                 \
  }                                                                           \
}

bool is_great_wonder(Impr_type_id id);
bool is_small_wonder(Impr_type_id id);
bool is_wonder(Impr_type_id id);
bool is_improvement(Impr_type_id id);

struct city *find_city_from_great_wonder(Impr_type_id id);
struct city *find_city_from_small_wonder(const struct player *pplayer,
					 Impr_type_id id);

bool great_wonder_was_built(Impr_type_id id);

bool can_sell_building(Impr_type_id id);
bool can_city_sell_building(struct city *pcity, Impr_type_id id);

#endif  /* FC__IMPROVEMENT_H */

