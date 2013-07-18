/****************************************************************************
 Freeciv - Copyright (C) 2004 - The Freeciv Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
****************************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* utility */
#include "rand.h"

/* common */
#include "base.h"
#include "game.h"
#include "road.h"

#include "extras.h"

#define MAX_EXTRA_TYPES (S_LAST + MAX_BASE_TYPES + MAX_ROAD_TYPES)

static struct extra_type extras[MAX_EXTRA_TYPES];

static struct extra_type_list *caused_by[EC_COUNT + 1];

/****************************************************************************
  Initialize extras structures.
****************************************************************************/
void extras_init(void)
{
  int i;

  for (i = 0; i < EC_COUNT + 1; i++) {
    caused_by[i] = extra_type_list_new();
  }

  for (i = 0; i < MAX_EXTRA_TYPES; i++) {
    requirement_vector_init(&(extras[i].reqs));
    extras[i].id = i;
  }

  for (i = 0; i < S_LAST; i++) {
    extras[i].type = EXTRA_SPECIAL;
    extras[i].data.special = i;
    switch(i) {
    case S_IRRIGATION:
      extra_to_caused_by_list(&extras[i], EC_IRRIGATION);
      break;
    case S_MINE:
      extra_to_caused_by_list(&extras[i], EC_MINE);
      break;
    case S_POLLUTION:
      extra_to_caused_by_list(&extras[i], EC_POLLUTION);
      break;
    case S_HUT:
      extra_to_caused_by_list(&extras[i], EC_HUT);
      break;
    case S_FARMLAND:
      extra_to_caused_by_list(&extras[i], EC_FARMLAND);
      break;
    case S_FALLOUT:
      extra_to_caused_by_list(&extras[i], EC_FALLOUT);
      break;
    default:
      extra_to_caused_by_list(&extras[i], EC_NONE);
      break;
    }
  }
}

/****************************************************************************
  Free the memory associated with extras
****************************************************************************/
void extras_free(void)
{
  int i;

  base_types_free();
  road_types_free();

  for (i = 0; i < EC_COUNT + 1; i++) {
    extra_type_list_destroy(caused_by[i]);
  }

  for (i = 0; i < MAX_EXTRA_TYPES; i++) {
    requirement_vector_free(&(extras[i].reqs));
  }
}

/**************************************************************************
  Return the number of extra_types.
**************************************************************************/
int extra_count(void)
{
  return S_LAST + game.control.num_base_types + game.control.num_road_types;
}

/**************************************************************************
  Return the extra id.
**************************************************************************/
int extra_number(const struct extra_type *pextra)
{
  fc_assert_ret_val(NULL != pextra, -1);

  return pextra->id;
}

/**************************************************************************
  Return the extra index.
**************************************************************************/
int extra_index(const struct extra_type *pextra)
{
  fc_assert_ret_val(NULL != pextra, -1);

  return pextra - extras;
}

/****************************************************************************
  Return extras type of given id.
****************************************************************************/
struct extra_type *extra_by_number(int id)
{
  fc_assert_ret_val(id >= 0 && id < MAX_EXTRA_TYPES, NULL);

  return &extras[id];
}

/****************************************************************************
  Get extra of the given type and given subid
****************************************************************************/
struct extra_type *extra_type_get(enum extra_type_id type, int subid)
{
  switch (type) {
  case EXTRA_SPECIAL:
    return extra_by_number(subid);
  case EXTRA_BASE:
    return extra_by_number(S_LAST + subid);
  case EXTRA_ROAD:
    return extra_by_number(S_LAST + game.control.num_base_types + subid);
  }

  return NULL;
}

/**************************************************************************
  Return the (translated) name of the extra type.
  You don't have to free the return pointer.
**************************************************************************/
const char *extra_name_translation(const struct extra_type *pextra)
{
  return name_translation(&pextra->name);
}

/**************************************************************************
  Return the (untranslated) rule name of the extra type.
  You don't have to free the return pointer.
**************************************************************************/
const char *extra_rule_name(const struct extra_type *pextra)
{
  return rule_name(&pextra->name);
}

/**************************************************************************
  Returns extra type matching rule name or NULL if there is no extra type
  with such name.
**************************************************************************/
struct extra_type *extra_type_by_rule_name(const char *name)
{
  const char *qs = Qn_(name);

  extra_type_iterate(pextra) {
    if (!fc_strcasecmp(extra_rule_name(pextra), qs)) {
      return pextra;
    }
  } extra_type_iterate_end;

  return NULL;
}

/**************************************************************************
  Returns extra type matching the translated name, or NULL if there is no
  extra type with that name.
**************************************************************************/
struct extra_type *extra_type_by_translated_name(const char *name)
{
  extra_type_iterate(pextra) {
    if (0 == strcmp(extra_name_translation(pextra), name)) {
      return pextra;
    }
  } extra_type_iterate_end;

  return NULL;
}

/**************************************************************************
  Returns base type of extra.
**************************************************************************/
struct base_type *extra_base_get(struct extra_type *pextra)
{
  fc_assert_ret_val(pextra->type == EXTRA_BASE, NULL);

  return &(pextra->data.base);
}

/**************************************************************************
  Returns road type of extra.
**************************************************************************/
struct road_type *extra_road_get(struct extra_type *pextra)
{
  fc_assert_ret_val(pextra->type == EXTRA_ROAD, NULL);

  return &(pextra->data.road);
}

/**************************************************************************
  Returns base type of extra. Uses const parameter.
**************************************************************************/
const struct base_type *extra_base_get_const(const struct extra_type *pextra)
{
  fc_assert_ret_val(pextra->type == EXTRA_BASE, NULL);

  return &(pextra->data.base);
}

/**************************************************************************
  Returns road type of extra. Uses const parameter.
**************************************************************************/
const struct road_type *extra_road_get_const(const struct extra_type *pextra)
{
  fc_assert_ret_val(pextra->type == EXTRA_ROAD, NULL);

  return &(pextra->data.road);
}

/**************************************************************************
  Returns extra type for given cause.
**************************************************************************/
struct extra_type_list *extra_type_list_by_cause(enum extra_cause cause)
{
  fc_assert(cause < EC_COUNT || cause == EC_NONE);

  return caused_by[cause];
}

/**************************************************************************
  Return random extra type for given cause.

  Use this function only when you absolutely need to get just one
  extra_type and have no other way to determine which one. This is meant
  to be only temporary solution until there's better ways to select the
  correct extra_type.
**************************************************************************/
struct extra_type *rand_extra_type_by_cause(enum extra_cause cause)
{
  struct extra_type_list *full_list = extra_type_list_by_cause(cause);
  int options = extra_type_list_size(full_list);

  if (options == 0) {
    return NULL;
  }

  return extra_type_list_get(full_list, fc_rand(options));
}

/**************************************************************************
  Add extra type to list of extra caused by given cause.
**************************************************************************/
void extra_to_caused_by_list(struct extra_type *pextra, enum extra_cause cause)
{
  fc_assert(cause < EC_COUNT || cause == EC_NONE);

  extra_type_list_append(caused_by[cause], pextra);
}

/****************************************************************************
  Is there extra of the given type cardinally near tile?
****************************************************************************/
bool is_extra_card_near(const struct tile *ptile, const struct extra_type *pextra)
{
  cardinal_adjc_iterate(ptile, adjc_tile) {
    if (tile_has_extra(adjc_tile, pextra)) {
      return TRUE;
    }
  } cardinal_adjc_iterate_end;

  return FALSE;
}

/****************************************************************************
  Is there extra of the given type near tile?
****************************************************************************/
bool is_extra_near_tile(const struct tile *ptile, const struct extra_type *pextra)
{
  adjc_iterate(ptile, adjc_tile) {
    if (tile_has_extra(adjc_tile, pextra)) {
      return TRUE;
    }
  } adjc_iterate_end;

  return FALSE;
}

/****************************************************************************
  Tells if extra can build to tile if all other requirements are met.
****************************************************************************/
bool extra_can_be_built(const struct extra_type *pextra,
                        const struct tile *ptile)
{
  /* We support only specials at this time. */
  fc_assert(pextra->type == EXTRA_SPECIAL);

  if (tile_has_extra(ptile, pextra)) {
    /* Extra exist already */
    return FALSE;
  }

  return TRUE;
}

/****************************************************************************
  Tells if player can build extra to tile with suitable unit.
****************************************************************************/
static bool can_build_extra_base(const struct extra_type *pextra,
                                 const struct player *pplayer,
                                 const struct tile *ptile)
{
  switch(pextra->type) {
  case EXTRA_SPECIAL:
    if (!extra_can_be_built(pextra, ptile)) {
      return FALSE;
    }
    break;
  case EXTRA_ROAD:
    if (!can_build_road_base(&(pextra->data.road), pplayer, ptile)) {
      return FALSE;
    }
    break;
  case EXTRA_BASE:
    if (!base_can_be_built(&(pextra->data.base), ptile)) {
      return FALSE;
    }
    break;
  }

  return TRUE;
}

/****************************************************************************
  Tells if player can build extra to tile with suitable unit.
****************************************************************************/
bool player_can_build_extra(const struct extra_type *pextra,
                            const struct player *pplayer,
                            const struct tile *ptile)
{
  if (!can_build_extra_base(pextra, pplayer, ptile)) {
    return FALSE;
  }

  return are_reqs_active(pplayer, NULL, NULL, ptile,
                         NULL, NULL, NULL, &pextra->reqs,
                         RPT_POSSIBLE);
}

/****************************************************************************
  Tells if unit can build extra on tile.
****************************************************************************/
bool can_build_extra(struct extra_type *pextra,
                     const struct unit *punit,
                     const struct tile *ptile)
{
  struct player *pplayer = unit_owner(punit);

  if (!can_build_extra_base(pextra, pplayer, ptile)) {
    return FALSE;
  }

  return are_reqs_active(pplayer, NULL, NULL, ptile,
                         unit_type(punit), NULL, NULL, &pextra->reqs,
                         RPT_CERTAIN);
}
