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
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "game.h"
#include "log.h"
#include "mem.h"
#include "player.h"
#include "shared.h"
#include "tech.h"

#include "government.h"

/* TODO:
 * o Update and turn on government evaluation code.
 * o Check exact government rules vs Civ1,Civ2.
 * o The clients display wrong upkeep icons in the city display,
 *   not sure what to do here.  (Does the new terrain-ruleset code
 *   have icons that could be used here?)
 * o When change government, server should update cities and unit
 *   upkeep etc and send updated info to client.
 * o Implement actual cost for unit gold upkeep.
 * o Possibly remove ai_gov_tech_hint stuff?
 *   (See usage in ai_manage_cities() in aicity.c)
 * o Update help system, including dynamic help on governments.
 * o Test the new government evaluation code (AI).
 *   [ It seems fine to me, although it favours Democracy very early
 *   on. This is because of the huge trade bonus. -SKi ]
 */

/*
 * WISHLIST:
 * o Implement the features needed for fundamentalism:
 *   - Units that require a government to be built, and have special
 *     upkeep values under that government. (Fanatics).
 *   - NO_UNHAPPY_CITIZENS flag for governments.
 *   - CONVERT_TITHES_TO_MONEY flag for governments (surplus only?).
 *   - A research penalty that is applied after all other modifiers.
 *   - A diplomatic penalty modifier when international incidents occur.
 *     (Spy places nuke in city, goes to war, etc).
 *     [ Is this one of those Civ II "features" best be ignored?  I am
 *     inclined to think so -SKi ]
 * o Features needed for CTP-style rules, just more trade, science and
 *   production modifiers.  (Just counting CTP governments, only
 *   basics).
 */

struct government *governments;

struct ai_gov_tech_hint ai_gov_tech_hints[MAX_NUM_TECH_LIST];

static char *flag_names[] = {
  "Build_Veteran_Diplomats", "Revolution_When_Unhappy", "Has_Senate",
  "Unbribable", "Inspires_Partisans", "Rapture_City_Growth"
};
static char *hint_names[] = {
  "Is_Nice", "Favors_Growth"
};

/***************************************************************
  Convert flag names to enum; case insensitive;
  returns G_LAST_FLAG if can't match.
***************************************************************/
enum government_flag_id government_flag_from_str(const char *s)
{
  enum government_flag_id i;

  assert(sizeof(flag_names)/sizeof(char*)==G_LAST_FLAG);
  
  for(i=0; i<G_LAST_FLAG; i++) {
    if (mystrcasecmp(flag_names[i], s)==0) {
      return i;
    }
  }
  return G_LAST_FLAG;
}

/***************************************************************
...
***************************************************************/
int government_has_flag(const struct government *gov,
			enum government_flag_id flag)
{
  assert(flag>=0 && flag<G_LAST_FLAG);
  return gov->flags & (1<<flag);
}

/***************************************************************
  Convert hint names to enum; case insensitive;
  returns G_LAST_HINT if can't match.
***************************************************************/
enum government_hint_id government_hint_from_str(const char *s)
{
  enum government_hint_id i;

  assert(sizeof(hint_names)/sizeof(char*)==G_LAST_HINT);
  
  for(i=0; i<G_LAST_HINT; i++) {
    if (mystrcasecmp(hint_names[i], s)==0) {
      return i;
    }
  }
  return G_LAST_HINT;
}

/***************************************************************
...
***************************************************************/
int government_has_hint(const struct government *gov,
			enum government_hint_id hint)
{
  assert(hint>=0 && hint<G_LAST_FLAG);
  return gov->hints & (1<<hint);
}

/***************************************************************
...
***************************************************************/
struct government *find_government_by_name(char *name)
{
  int i;

  for (i = 0; i < game.government_count; ++i) {
    if (mystrcasecmp(governments[i].name, name) == 0) {
      return &governments[i];
    }
  }
  return NULL;
}

/***************************************************************
...
***************************************************************/
struct government *get_government(int gov)
{
  assert(game.government_count > 0 && gov >= 0 && gov < game.government_count);
  return &governments[gov];
}

/***************************************************************
...
***************************************************************/
struct government *get_gov_pplayer(struct player *pplayer)
{
  int gov;
  
  assert(pplayer);
  gov = pplayer->government;
  assert(game.government_count > 0 && gov >= 0 && gov < game.government_count);
  return &governments[gov];
}

/***************************************************************
...
***************************************************************/
struct government *get_gov_iplayer(int player_num)
{
  int gov;
  
  assert(player_num >= 0 && player_num < game.nplayers);
  gov = game.players[player_num].government;
  assert(game.government_count > 0 && gov >= 0 && gov < game.government_count);
  return &governments[gov];
}

/***************************************************************
...
***************************************************************/
struct government *get_gov_pcity(struct city *pcity)
{
  assert(pcity);
  return get_gov_iplayer(pcity->owner);
}


/***************************************************************
...
***************************************************************/
char *get_ruler_title(int gov, int male, int nation)
{
  struct government *g = get_government(gov);
  struct ruler_title *best_match = NULL;
  int i;

  for(i=0; i<g->num_ruler_titles; i++) {
    struct ruler_title *title = &g->ruler_titles[i];
    if (title->nation == DEFAULT_TITLE && best_match == NULL) {
      best_match = title;
    } else if (title->nation == nation) {
      best_match = title;
      break;
    }
  }

  if (best_match) {
    return male ? best_match->male_title : best_match->female_title;
  } else {
    freelog(LOG_NORMAL,
	    "get_ruler_title: found no title for government %d (%s) nation %d",
	    gov, g->name, nation);
    return male ? "Mr." : "Ms.";
  }
}

/***************************************************************
...
***************************************************************/
int get_government_max_rate(int type)
{
  if(type >= 0 && type < game.government_count)
    return governments[type].max_rate;
  return 50;
}

/***************************************************************
Added for civil war probability computation - Kris Bubendorfer
***************************************************************/
int get_government_civil_war_prob(int type)
{
  if(type >= 0 && type < game.government_count)
    return governments[type].civil_war;
  return 0;
}

/***************************************************************
...
***************************************************************/
char *get_government_name(int type)
{
  if(type >= 0 && type < game.government_count)
    return governments[type].name;
  return "";
}

/***************************************************************
  Can change to government if appropriate tech exists, and one of:
   - no required tech (required is A_NONE)
   - player has required tech
   - we have an appropriate wonder
***************************************************************/
int can_change_to_government(struct player *pplayer, int government)
{
  int req;

  assert(game.government_count > 0 &&
	 government >= 0 && government < game.government_count);

  req = governments[government].required_tech;
  if (!tech_exists(req))
    return 0;
  else 
    return (req == A_NONE
	    || (get_invention(pplayer, req) == TECH_KNOWN)
	    || player_owns_active_govchange_wonder(pplayer));
}

/***************************************************************
...
***************************************************************/
void set_ruler_title(struct government *gov, int nation,
                     char *male, char *female)
{
  struct ruler_title *title;

  gov->num_ruler_titles++;
  gov->ruler_titles
    = fc_realloc(gov->ruler_titles,
                 gov->num_ruler_titles*sizeof(struct ruler_title));
  title = &(gov->ruler_titles[gov->num_ruler_titles-1]);

  title->nation = nation;
  strcpy(title->male_title, male);
  strcpy(title->female_title, female);
}
