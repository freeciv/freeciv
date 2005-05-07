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
#include "map.h"
#include "mem.h"
#include "shared.h" /* ARRAY_SIZE */
#include "support.h"
#include "tech.h"

#include "improvement.h"

static const char *genus_names[IG_LAST] = {
  "GreatWonder", "SmallWonder", "Improvement", "Special"
};

static const char *flag_names[] = {
  "VisibleByOthers", "SaveSmallWonder"
};
/* Note that these strings must correspond with the enums in impr_flag_id,
   in common/improvement.h */

/**************************************************************************
All the city improvements:
Use get_improvement_type(id) to access the array.
The improvement_types array is now setup in:
   server/ruleset.c (for the server)
   client/packhand.c (for the client)
**************************************************************************/
struct impr_type improvement_types[B_LAST];

/**************************************************************************
  Convert impr genus names to enum; case insensitive;
  returns IG_LAST if can't match.
**************************************************************************/
enum impr_genus_id impr_genus_from_str(const char *s)
{
  enum impr_genus_id i;

  for(i = 0; i < ARRAY_SIZE(genus_names); i++) {
    if (mystrcasecmp(genus_names[i], s) == 0) {
      break;
    }
  }
  return i;
}

/****************************************************************************
  Inialize building structures.
****************************************************************************/
void improvements_init(void)
{
  int i;

  for (i = 0; i < ARRAY_SIZE(improvement_types); i++) {
    /* HACK: this field is declared const to keep anyone from changing
     * them.  But we have to set it somewhere!  This should be the only
     * place. */
    *(int *)&improvement_types[i].index = i;
  }
}

/**************************************************************************
  Frees the memory associated with this improvement.
**************************************************************************/
static void improvement_free(Impr_type_id id)
{
  struct impr_type *p = get_improvement_type(id);

  free(p->helptext);
  p->helptext = NULL;
}

/***************************************************************
 Frees the memory associated with all improvements.
***************************************************************/
void improvements_free()
{
  impr_type_iterate(impr) {
    improvement_free(impr);
  } impr_type_iterate_end;
}

/**************************************************************************
Returns 1 if the improvement_type "exists" in this game, 0 otherwise.
An improvement_type doesn't exist if one of:
- id is out of range;
- the improvement_type has been flagged as removed by setting its
  tech_req to A_LAST;
- it is a space part, and the spacerace is not enabled.
Arguably this should be called improvement_type_exists, but that's too long.
**************************************************************************/
bool improvement_exists(Impr_type_id id)
{
  if (id < 0 || id >= B_LAST || id>= game.control.num_impr_types)
    return FALSE;

  if (!game.info.spacerace
      && (building_has_effect(id, EFT_SS_STRUCTURAL)
	  || building_has_effect(id, EFT_SS_COMPONENT)
	  || building_has_effect(id, EFT_SS_MODULE))) {
    /* This assumes that space parts don't have any other effects. */
    return FALSE;
  }

  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
struct impr_type *get_improvement_type(Impr_type_id id)
{
  return &improvement_types[id];
}

/**************************************************************************
...
**************************************************************************/
const char *get_improvement_name(Impr_type_id id)
{
  return get_improvement_type(id)->name; 
}

/****************************************************************************
  Get the original (untranslated) improvement name.
****************************************************************************/
const char *get_improvement_name_orig(Impr_type_id id)
{
  return get_improvement_type(id)->name_orig; 
}

/****************************************************************************
  Returns the number of shields it takes to build this improvement.
****************************************************************************/
int impr_build_shield_cost(Impr_type_id id)
{
  return improvement_types[id].build_cost;
}

/****************************************************************************
  Returns the amount of gold it takes to rush this improvement.
****************************************************************************/
int impr_buy_gold_cost(Impr_type_id id, int shields_in_stock)
{
  int cost = 0, missing =
      improvement_types[id].build_cost - shields_in_stock;

  if (building_has_effect(id, EFT_PROD_TO_GOLD)) {
    /* Can't buy capitalization. */
    return 0;
  }

  if (missing > 0) {
    cost = 2 * missing;
  }

  if (is_wonder(id)) {
    cost *= 2;
  }
  if (shields_in_stock == 0) {
    cost *= 2;
  }
  return cost;
}

/****************************************************************************
  Returns the amount of gold received when this improvement is sold.
****************************************************************************/
int impr_sell_gold(Impr_type_id id)
{
  return improvement_types[id].build_cost;
}

/**************************************************************************
...
**************************************************************************/
bool is_wonder(Impr_type_id id)
{
  return (is_great_wonder(id) || is_small_wonder(id));
}

/**************************************************************************
Does a linear search of improvement_types[].name
Returns B_LAST if none match.
**************************************************************************/
Impr_type_id find_improvement_by_name(const char *s)
{
  impr_type_iterate(i) {
    if (strcmp(improvement_types[i].name, s)==0)
      return i;
  } impr_type_iterate_end;

  return B_LAST;
}

/****************************************************************************
  Does a linear search of improvement_types[].name_orig to find the
  improvement that matches the given original (untranslated) name.  Returns
  B_LAST if none match.
****************************************************************************/
Impr_type_id find_improvement_by_name_orig(const char *s)
{
  impr_type_iterate(i) {
    if (mystrcasecmp(improvement_types[i].name_orig, s) == 0) {
      return i;
    }
  } impr_type_iterate_end;

  return B_LAST;
}

/**************************************************************************
 Return TRUE if the impr has this flag otherwise FALSE
**************************************************************************/
bool impr_flag(Impr_type_id id, enum impr_flag_id flag)
{
  assert(flag >= 0 && flag < IF_LAST);
  return TEST_BIT(improvement_types[id].flags, flag);
}

/**************************************************************************
 Convert flag names to enum; case insensitive;
 returns IF_LAST if can't match.
**************************************************************************/
enum impr_flag_id impr_flag_from_str(const char *s)
{
  enum impr_flag_id i;

  assert(ARRAY_SIZE(flag_names) == IF_LAST);
  
  for(i = 0; i < IF_LAST; i++) {
    if (mystrcasecmp(flag_names[i], s) == 0) {
      return i;
    }
  }
  return IF_LAST;
}

/**************************************************************************
 Return TRUE if the improvement should be visible to others without spying
**************************************************************************/
bool is_improvement_visible(Impr_type_id id)
{
  return (is_wonder(id) || impr_flag(id, IF_VISIBLE_BY_OTHERS));
}

/**************************************************************************
 Returns 1 if the improvement is obsolete, now also works for wonders
**************************************************************************/
bool improvement_obsolete(const struct player *pplayer, Impr_type_id id) 
{
  struct impr_type *impr = get_improvement_type(id);

  if (!tech_exists(impr->obsolete_by)) {
    return FALSE;
  }

  if (is_great_wonder(id)) {
    /* a great wonder is obsolete, as soon as *any* player researched the
       obsolete tech */
   return game.info.global_advances[impr->obsolete_by] != 0;
  }

  return (get_invention(pplayer, impr->obsolete_by) == TECH_KNOWN);
}

/**************************************************************************
   Whether player can build given building somewhere, ignoring whether it
   is obsolete.
**************************************************************************/
bool can_player_build_improvement_direct(const struct player *p,
					 Impr_type_id id)
{
  struct impr_type *impr;
  bool space_part = FALSE;
  int i;

  /* This also checks if tech req is Never */
  if (!improvement_exists(id)) {
    return FALSE;
  }

  impr = get_improvement_type(id);

  for (i = 0; i < MAX_NUM_REQS; i++) {
    if (impr->req[i].source.type == REQ_NONE) {
      break;
    }
    if (impr->req[i].range >= REQ_RANGE_PLAYER
	&& !is_req_active(p, NULL, NULL, NULL, NULL, NULL,
			  &impr->req[i])) {
      return FALSE;
    }
  }

  /* Check for space part construction.  This assumes that space parts have
   * no other effects. */
  if (building_has_effect(id, EFT_SS_STRUCTURAL)) {
    space_part = TRUE;
    if (p->spaceship.structurals >= NUM_SS_STRUCTURALS) {
      return FALSE;
    }
  }
  if (building_has_effect(id, EFT_SS_COMPONENT)) {
    space_part = TRUE;
    if (p->spaceship.components >= NUM_SS_COMPONENTS) {
      return FALSE;
    }
  }
  if (building_has_effect(id, EFT_SS_MODULE)) {
    space_part = TRUE;
    if (p->spaceship.modules >= NUM_SS_MODULES) {
      return FALSE;
    }
  }
  if (space_part &&
      (!get_player_bonus(p, EFT_ENABLE_SPACE) > 0
       || p->spaceship.state >= SSHIP_LAUNCHED)) {
    return FALSE;
  }

  if (is_great_wonder(id)) {
    /* Can't build wonder if already built */
    if (great_wonder_was_built(id)) {
      return FALSE;
    }
  }

  return TRUE;
}

/**************************************************************************
  Whether player can _eventually_ build given building somewhere -- i.e.,
  returns TRUE if building is available with current tech OR will be
  available with future tech.  Returns FALSE if building is obsolete.
**************************************************************************/
bool can_player_build_improvement(const struct player *p, Impr_type_id id)
{
  if (!can_player_build_improvement_direct(p, id)) {
    return FALSE;
  }
  if (improvement_obsolete(p, id)) {
    return FALSE;
  }
  return TRUE;
}

/**************************************************************************
  Whether player can _eventually_ build given building somewhere -- i.e.,
  returns TRUE if building is available with current tech OR will be
  available with future tech.  Returns FALSE if building is obsolete.
**************************************************************************/
bool can_player_eventually_build_improvement(const struct player *p,
					     Impr_type_id id)
{
  int r;
  struct impr_type *building;

  if (!improvement_exists(id)) {
    return FALSE;
  }
  if (improvement_obsolete(p, id)) {
    return FALSE;
  }

  /* Check for requirements that aren't met and that are unchanging (so
   * they can never be met). */
  building = get_improvement_type(id);
  for (r = 0; r < MAX_NUM_REQS; r++) {
    if (building->req[r].source.type == REQ_NONE) {
      break;
    }
    if (building->req[r].range >= REQ_RANGE_PLAYER
	&& is_req_unchanging(&building->req[r])
	&& !is_req_active(p, NULL, NULL, NULL, NULL, NULL,
			  &building->req[r])) {
      return FALSE;
    }
  }
  /* FIXME: should check some "unchanging" reqs here - like if there's
   * a nation requirement, we can go ahead and check it now. */

  return TRUE;
}

/**************************************************************************
  Is this building a great wonder?
**************************************************************************/
bool is_great_wonder(Impr_type_id id)
{
  return (improvement_types[id].genus == IG_GREAT_WONDER);
}

/**************************************************************************
  Is this building a small wonder?
**************************************************************************/
bool is_small_wonder(Impr_type_id id)
{
  return (improvement_types[id].genus == IG_SMALL_WONDER);
}

/**************************************************************************
  Is this building a regular improvement?
**************************************************************************/
bool is_improvement(Impr_type_id id)
{
  return (improvement_types[id].genus == IG_IMPROVEMENT);
}

/**************************************************************************
  Get the world city with this great wonder.
**************************************************************************/
struct city *find_city_from_great_wonder(Impr_type_id id)
{
  return (find_city_by_id(game.info.great_wonders[id]));
}

/**************************************************************************
  Get the player city with this small wonder.
**************************************************************************/
struct city *find_city_from_small_wonder(const struct player *pplayer,
					 Impr_type_id id)
{
  return (player_find_city_by_id(pplayer, pplayer->small_wonders[id]));
}

/**************************************************************************
  Was this great wonder built?
**************************************************************************/
bool great_wonder_was_built(Impr_type_id id)
{
  return (game.info.great_wonders[id] != 0);
}

/**************************************************************************
  Return TRUE iff the improvement can be sold.
**************************************************************************/
bool can_sell_building(Impr_type_id id)
{
  return is_improvement(id);
}

/****************************************************************************
  Return TRUE iff the city can sell the given improvement.
****************************************************************************/
bool can_city_sell_building(struct city *pcity, Impr_type_id id)
{
  return (city_got_building(pcity, id) ? can_sell_building(id) : FALSE);
}

