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

struct city;
struct player;

#define G_MAGIC (127)		/* magic constant, used as flag value */

/* special values for free_* fields -- SKi */
#define G_CITY_SIZE_FREE          G_MAGIC

/* other flags (on/off) -- SKi */
#define G_BUILD_VETERAN_DIPLOMAT  (0x01)  /* and Spies or other F_DIPLOMAT */
#define G_REVOLUTION_WHEN_UNHAPPY (0x02)
#define G_HAS_SENATE              (0x04)
#define G_UNBRIBABLE              (0x08)
#define G_INSPIRES_PARTISANS      (0x10)
#define G_IS_NICE                 (0x20)
#define G_FAVORS_GROWTH           (0x40)

/* each government has a NULL-record terminated list of ruler titles,
 * at least one entry (the default) should be in it, the list is traversed
 * when a players title is needed.
 * -- SKi */
#define DEFAULT_TITLE  G_MAGIC
#define NULL_RULER_TITLE { 0, NULL, NULL }

struct ruler_title
{
  int   race;
  char *male_title;
  char *female_title;
};

/* This is struct government itself.  All information about
 * a form of government is contained inhere.
 * -- SKi */
struct government
{
  int   index;			/* index into governments[] array */
  char *name;			/* government name */
  int   graphic;		/* offset in government icons */
  int   required_tech;		/* tech required to change to this gov */

  struct ruler_title *ruler_title;  /* ruler titles */

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
  int   rapture_size;		/* minimum city size for rapture; if 0,
				   rapture is not possible */

  /* unit cost modifiers */
  int   unit_happy_cost_factor;
  int   unit_shield_cost_factor;
  int   unit_food_cost_factor;
  int   unit_gold_cost_factor;
  
  /* base cost that a city does not have to "pay" for */
  int   free_happy;
  int   free_shield;
  int   free_food;
  int   free_gold;
  
  /* government production penalties -- SKi */
  int   trade_before_penalty;
  int   shields_before_penalty;
  int   food_before_penalty;

  /* government production penalties when celebrating */
  int   celeb_trade_before_penalty;
  int   celeb_shields_before_penalty;
  int   celeb_food_before_penalty;

  /* government production bonuses -- SKi */
  int   trade_bonus;
  int   shield_bonus;
  int   food_bonus;

  /* government production bonuses when celebrating */
  int   celeb_trade_bonus;
  int   celeb_shield_bonus;
  int   celeb_food_bonus;

  /* corruption modifiers -- SKi */
  int   corruption_level;
  int   corruption_modifier;
  int   fixed_corruption_distance;
  int   corruption_distance_factor;
  int   extra_corruption_distance;
  
  /* various other flags */
  int   flags;
};

extern struct government *governments;

struct government *get_government(int gov);
struct government *get_gov_iplayer(int player_num);
struct government *get_gov_pplayer(struct player *pplayer);
struct government *get_gov_pcity(struct city *pcity);

struct government *find_government_by_name(char *name);

int get_government_max_rate(int type);
int get_government_civil_war_prob(int type);
char *get_government_name(int type);
char *get_ruler_title(int gov, int male, int race);

int can_change_to_government(struct player *pplayer, int government);

int government_graphic(int gov);

#endif  /* FC__GOVERNMENT_H */
