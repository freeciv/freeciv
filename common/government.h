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
#ifndef FC__GOVERNMENT_H
#define FC__GOVERNMENT_H

#include "shared.h"

#include "fc_types.h"

struct Sprite;			/* opaque; client-gui specific */

#define G_MAGIC (127)		/* magic constant, used as flag value */

/* special values for free_* fields -- SKi */
#define G_CITY_SIZE_FREE          G_MAGIC

enum government_flag_id {
  G_BUILD_VETERAN_DIPLOMAT=0,	/* and Spies (in general: all F_DIPLOMAT) */
  G_REVOLUTION_WHEN_UNHAPPY,
  G_HAS_SENATE,			/* not implemented */
  G_UNBRIBABLE,
  G_INSPIRES_PARTISANS,
  G_RAPTURE_CITY_GROWTH,        /* allows city to grow by celebrating */
  G_FANATIC_TROOPS,             /* for building troops with F_FANATIC flag */
  G_NO_UNHAPPY_CITIZENS,        /* no unhappy citizen, needed by
				   fundamentism */
  G_CONVERT_TITHES_TO_MONEY,    /* tithes to money, needed by fundamentalism */
  G_REDUCED_RESEARCH,           /* penalty for research, needed by
				   fundamentalism */
  G_LAST_FLAG
};
#define G_FIRST_FLAG G_BUILD_VETERAN_DIPLOMAT

/* each government has a list of ruler titles, where at least
 * one entry should have nation=DEFAULT_TITLE.
 */
#define DEFAULT_TITLE  MAX_NUM_ITEMS

struct ruler_title
{
  int  nation;
  const char *male_title; /* Translated string - doesn't need freeing. */
  const char *female_title; /* Translated string - doesn't need freeing. */
  
  /* untranslated copies: */
  char male_title_orig[MAX_LEN_NAME];    
  char female_title_orig[MAX_LEN_NAME];
};

/* This is struct government itself.  All information about
 * a form of government is contained inhere.
 * -- SKi */
struct government
{
  int   index;			/* index into governments[] array */
  const char *name; /* Translated string - doesn't need freeing. */
  char  name_orig[MAX_LEN_NAME]; /* untranslated copy */
  char  graphic_str[MAX_LEN_NAME];
  char  graphic_alt[MAX_LEN_NAME];
  int   required_tech;		/* tech required to change to this gov */

  struct ruler_title *ruler_titles;
  int   num_ruler_titles;

  int   max_rate;		/* max individual Tax/Lux/Sci rate  */
  int   civil_war;              /* chance (from 100) of civil war in
				   right conditions */
  int   martial_law_max;	/* maximum number of units which can
				   enforce martial law */
  int   martial_law_per;        /* number of unhappy citizens made
				   content by each enforcer unit */
  int   empire_size_mod;	/* (signed) offset to game.cityfactor to
				   give city count when number of naturally
				   content citizens is decreased */
  int   empire_size_inc;	/* if non-zero, reduce one content citizen for
				   every empire_size_inc cities once #cities
				   exceeds game.cityfactor + empire_size_mod */
  int   rapture_size;		/* minimum city size for rapture; if 255,
				   rapture is (practically) impossible */

  /* unit cost modifiers */
  int unit_happy_cost_factor;
  int unit_upkeep_factor[O_MAX];
  
  /* base cost that a city does not have to "pay" for */
  int free_happy;
  int free_upkeep[O_MAX];
  
  /* government production penalties (when celebrating and when not) */
  int output_before_penalty[O_MAX];
  int celeb_output_before_penalty[O_MAX];

  /* government production bonuses.  These act as an EFT_XXX_INC_TILE
   * effect.  There are separate values for celebrating versus normal
   * cities. */
  int output_inc_tile[O_MAX];
  int celeb_output_inc_tile[O_MAX];

  /* waste/corruption modifiers - see governments.ruleset for more detail */
  struct gov_waste {
    int level;
    int modifier;
    int fixed_distance;
    int distance_factor;
    int extra_distance;
    int max_distance_cap;
  } waste[O_MAX];

  /* other flags: bits in enum government_flag_id order,
     use government_has_flag() to access */
  int   flags;

  struct Sprite *sprite;
  
  char *helptext;
};

extern struct government *governments;

struct government *get_government(int gov);
struct government *get_gov_pplayer(const struct player *pplayer);
struct government *get_gov_pcity(const struct city *pcity);

struct government *find_government_by_name(const char *name);
struct government *find_government_by_name_orig(const char *name);

enum government_flag_id government_flag_from_str(const char *s);
bool government_has_flag(const struct government *gov,
			enum government_flag_id flag);

int get_government_max_rate(int type);
int get_government_civil_war_prob(int type);
const char *get_government_name(int type);
const char *get_ruler_title(int gov, bool male, int nation);

bool can_change_to_government(struct player *pplayer, int government);

void set_ruler_title(struct government *gov, int nation, 
                     const char *male, const char *female);
void governments_alloc(int num);
void governments_free(void);

#define government_iterate(gov)                                             \
{                                                                           \
  int GI_index;                                                             \
                                                                            \
  for (GI_index = 0; GI_index < game.government_count; GI_index++) {        \
    struct government *gov = get_government(GI_index);                      \
    {

#define government_iterate_end                                              \
    }                                                                       \
  }                                                                         \
}

#endif  /* FC__GOVERNMENT_H */
