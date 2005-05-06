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

#include "game.h"
#include "log.h"
#include "mem.h"
#include "player.h"
#include "shared.h"
#include "support.h"
#include "tech.h"

#include "government.h"

struct government *governments = NULL;

/****************************************************************************
  Does a linear search of the governments to find the one that matches the
  given (translated) name.  Returns NULL if none match.
****************************************************************************/
struct government *find_government_by_name(const char *name)
{
  government_iterate(gov) {
    if (mystrcasecmp(gov->name, name) == 0) {
      return gov;
    }
  } government_iterate_end;

  return NULL;
}

/****************************************************************************
  Does a linear search of the governments to find the one that matches the
  given original (untranslated) name.  Returns NULL if none match.
****************************************************************************/
struct government *find_government_by_name_orig(const char *name)
{
  government_iterate(gov) {
    if (mystrcasecmp(gov->name_orig, name) == 0) {
      return gov;
    }
  } government_iterate_end;

  return NULL;
}

/****************************************************************************
  Return the government with the given ID.
****************************************************************************/
struct government *get_government(int gov)
{
  assert(game.control.government_count > 0 && gov >= 0
	 && gov < game.control.government_count);
  assert(governments[gov].index == gov);
  return &governments[gov];
}

/****************************************************************************
  Return this player's government.
****************************************************************************/
struct government *get_gov_pplayer(const struct player *pplayer)
{
  assert(pplayer != NULL);
  return get_government(pplayer->government);
}

/****************************************************************************
  Return the government of the player who owns the city.
****************************************************************************/
struct government *get_gov_pcity(const struct city *pcity)
{
  assert(pcity != NULL);
  return get_gov_pplayer(city_owner(pcity));
}


/***************************************************************
...
***************************************************************/
const char *get_ruler_title(int gov, bool male, int nation)
{
  struct government *g = get_government(gov);
  struct ruler_title *best_match = NULL;
  int i;

  for(i=0; i<g->num_ruler_titles; i++) {
    struct ruler_title *title = &g->ruler_titles[i];
    if (title->nation == DEFAULT_TITLE && !best_match) {
      best_match = title;
    } else if (title->nation == nation) {
      best_match = title;
      break;
    }
  }

  if (best_match) {
    return male ? best_match->male_title : best_match->female_title;
  } else {
    freelog(LOG_ERROR,
	    "get_ruler_title: found no title for government %d (%s) nation %d",
	    gov, g->name, nation);
    return male ? "Mr." : "Ms.";
  }
}

/***************************************************************
...
***************************************************************/
const char *get_government_name(int type)
{
  if(type >= 0 && type < game.control.government_count)
    return governments[type].name;
  return "";
}

/***************************************************************
  Can change to government if appropriate tech exists, and one of:
   - no required tech (required is A_NONE)
   - player has required tech
   - we have an appropriate wonder
***************************************************************/
bool can_change_to_government(struct player *pplayer, int government)
{
  struct government *gov = &governments[government];

  if (government < 0 || government >= game.control.government_count) {
    assert(0);
    return FALSE;
  }

  if (get_player_bonus(pplayer, EFT_ANY_GOVERNMENT) > 0) {
    /* Note, this may allow govs that are on someone else's "tech tree". */
    return TRUE;
  }

  return are_reqs_active(pplayer, NULL, NULL, NULL, NULL,
			 gov->req, MAX_NUM_REQS);
}

/***************************************************************
...
***************************************************************/
void set_ruler_title(struct government *gov, int nation,
                     const char *male, const char *female)
{
  struct ruler_title *title;

  gov->num_ruler_titles++;
  gov->ruler_titles =
    fc_realloc(gov->ruler_titles,
      gov->num_ruler_titles*sizeof(struct ruler_title));
  title = &(gov->ruler_titles[gov->num_ruler_titles-1]);

  title->nation = nation;

  sz_strlcpy(title->male_title_orig, male);
  title->male_title = title->male_title_orig;

  sz_strlcpy(title->female_title_orig, female);
  title->female_title = title->female_title_orig;
}

/***************************************************************
 Allocate space for the given number of governments.
***************************************************************/
void governments_alloc(int num)
{
  int index;

  governments = fc_calloc(num, sizeof(struct government));
  game.control.government_count = num;

  for (index = 0; index < num; index++) {
    governments[index].index = index;
  }
}

/***************************************************************
 De-allocate resources associated with the given government.
***************************************************************/
static void government_free(struct government *gov)
{
  free(gov->ruler_titles);
  gov->ruler_titles = NULL;

  free(gov->helptext);
  gov->helptext = NULL;
}

/***************************************************************
 De-allocate the currently allocated governments.
***************************************************************/
void governments_free(void)
{
  government_iterate(gov) {
    government_free(gov);
  } government_iterate_end;
  free(governments);
  governments = NULL;
  game.control.government_count = 0;
}
