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
#include "fcintl.h"
#include "string_vector.h"

/* common */
#include "fc_types.h"
#include "game.h"
#include "movement.h"
#include "name_translation.h"
#include "unittype.h"

#include "road.h"

static struct road_type roads[MAX_ROAD_TYPES];

/**************************************************************************
  Return the road id.
**************************************************************************/
Road_type_id road_number(const struct road_type *proad)
{
  fc_assert_ret_val(NULL != proad, -1);

  return proad->id;
}

/**************************************************************************
  Return the road index.

  Currently same as road_number(), paired with road_count()
  indicates use as an array index.
**************************************************************************/
Road_type_id road_index(const struct road_type *proad)
{
  fc_assert_ret_val(NULL != proad, -1);

  return proad - roads;
}

/**************************************************************************
  Return the number of road_types.
**************************************************************************/
Road_type_id road_count(void)
{
  return game.control.num_road_types;
}

/****************************************************************************
  Return road type of given id.
****************************************************************************/
struct road_type *road_by_number(Road_type_id id)
{
  fc_assert_ret_val(id >= 0 && id < game.control.num_road_types, NULL);

  return &roads[id];
}

/****************************************************************************
  Initialize road_type structures.
****************************************************************************/
void road_types_init(void)
{
  int i;

  for (i = 0; i < MAX_ROAD_TYPES; i++) {
    roads[i].id = i;
    requirement_vector_init(&roads[i].reqs);
    roads[i].hiders = NULL;
    roads[i].helptext = NULL;
  }
}

/****************************************************************************
  Free the memory associated with road types
****************************************************************************/
void road_types_free(void)
{
  road_type_iterate(proad) {
    requirement_vector_free(&proad->reqs);
    if (proad->hiders != NULL) {
      road_type_list_destroy(proad->hiders);
      proad->hiders = NULL;
    }
    if (NULL != proad->helptext) {
      strvec_destroy(proad->helptext);
      proad->helptext = NULL;
    }
  } road_type_iterate_end;
}

/****************************************************************************
  Return tile special that used to represent this road type.
****************************************************************************/
enum road_compat road_compat_special(const struct road_type *proad)
{
  return proad->compat;
}

/****************************************************************************
  Return road type represented by given compatibility special, or NULL if
  special does not represent road type at all.
****************************************************************************/
struct road_type *road_by_compat_special(enum road_compat compat)
{
  if (compat == ROCO_NONE) {
    return NULL;
  }

  road_type_iterate(proad) {
    if (road_compat_special(proad) == compat) {
      return proad;
    }
  } road_type_iterate_end;

  return NULL;
}

/****************************************************************************
  Return translated name of this road type.
****************************************************************************/
const char *road_name_translation(struct road_type *road)
{
  return name_translation(&road->name);
}

/****************************************************************************
  Return untranslated name of this road type.
****************************************************************************/
const char *road_rule_name(const struct road_type *road)
{
  return rule_name(&road->name);
}

/**************************************************************************
  Returns road type matching rule name or NULL if there is no road type
  with such name.
**************************************************************************/
struct road_type *road_type_by_rule_name(const char *name)
{
  const char *qs = Qn_(name);

  road_type_iterate(proad) {
    if (!fc_strcasecmp(road_rule_name(proad), qs)) {
      return proad;
    }
  } road_type_iterate_end;

  return NULL;
}

/**************************************************************************
  Returns road type matching the translated name, or NULL if there is no
  road type with that name.
**************************************************************************/
struct road_type *road_type_by_translated_name(const char *name)
{
  road_type_iterate(proad) {
    if (0 == strcmp(road_name_translation(proad), name)) {
      return proad;
    }
  } road_type_iterate_end;

  return NULL;
}

/****************************************************************************
  Is road native to unit class?
****************************************************************************/
bool is_native_road_to_uclass(const struct road_type *proad,
                              const struct unit_class *pclass)
{
  return BV_ISSET(proad->native_to, uclass_index(pclass));
}

/****************************************************************************
  Tells if road can build to tile if all other requirements are met.
****************************************************************************/
bool road_can_be_built(const struct road_type *proad, const struct tile *ptile)
{
  if (!terrain_control.may_road) {
    return FALSE;
  }

 if (!proad->buildable) {
    /* Road type not buildable. */
    return FALSE;
  }

  if (tile_has_road(ptile, proad)) {
    /* Road exist already */
    return FALSE;
  }

  if (tile_terrain(ptile)->road_time == 0) {
    return FALSE;
  }

  return TRUE;
}

/****************************************************************************
  Tells if player can build road to tile with suitable unit.
****************************************************************************/
static bool can_build_road_base(const struct road_type *proad,
                                const struct player *pplayer,
                                const struct tile *ptile)
{
  if (!road_can_be_built(proad, ptile)) {
    return FALSE;
  }

  if (road_has_flag(proad, RF_REQUIRES_BRIDGE)
      && !player_knows_techs_with_flag(pplayer, TF_BRIDGE)) {
    if (tile_has_special(ptile, S_RIVER)) {
      return FALSE;
    }
    /* TODO: Cache list of road types with RF_PREVENTS_OTHER_ROADS
     *       after ruleset loading and use that list here instead
     *       of always iterating through all road types. */
    road_type_iterate(old) {
      if (road_has_flag(old, RF_PREVENTS_OTHER_ROADS)
          && tile_has_road(ptile, old)) {
        return FALSE;
      }
    } road_type_iterate_end;
  }

  return TRUE;
}

/****************************************************************************
  Tells if player can build road to tile with suitable unit.
****************************************************************************/
bool player_can_build_road(const struct road_type *proad,
                           const struct player *pplayer,
                           const struct tile *ptile)
{
  if (!can_build_road_base(proad, pplayer, ptile)) {
    return FALSE;
  }

  return are_reqs_active(pplayer, NULL, NULL, ptile,
                         NULL, NULL, NULL, &proad->reqs,
                         RPT_POSSIBLE);
}

/****************************************************************************
  Tells if unit can build road on tile.
****************************************************************************/
bool can_build_road(struct road_type *proad,
		    const struct unit *punit,
		    const struct tile *ptile)
{
  struct player *pplayer = unit_owner(punit);

  if (!can_build_road_base(proad, pplayer, ptile)) {
    return FALSE;
  }

  return are_reqs_active(pplayer, NULL, NULL, ptile,
                         unit_type(punit), NULL, NULL, &proad->reqs,
                         RPT_CERTAIN);
}

/****************************************************************************
  Count tiles with specified road near the tile. Can be called with NULL
  road.
****************************************************************************/
int count_road_near_tile(const struct tile *ptile, const struct road_type *proad)
{
  int count = 0;

  if (proad == NULL) {
    return 0;
  }

  adjc_iterate(ptile, adjc_tile) {
    if (tile_has_road(adjc_tile, proad)) {
      count++;
    }
  } adjc_iterate_end;

  return count;
}

/****************************************************************************
  Count tiles with any river near the tile.
****************************************************************************/
int count_river_near_tile(const struct tile *ptile)
{
  int count = 0;

  cardinal_adjc_iterate(ptile, adjc_tile) {
    if (tile_has_river(adjc_tile)) {
      count++;
    }
  } cardinal_adjc_iterate_end;

  return count;
}

/****************************************************************************
  Count tiles with river of specific type near the tile.
****************************************************************************/
int count_river_type_near_tile(const struct tile *ptile,
                               const struct road_type *priver,
                               bool percentage)
{
  int count = 0;
  int total = 0;

  cardinal_adjc_iterate(ptile, adjc_tile) {
    if ((priver != NULL && tile_has_road(adjc_tile, priver))
        || (priver == NULL && tile_has_special(adjc_tile, S_OLD_RIVER))) {
      count++;
    }
    total++;
  } cardinal_adjc_iterate_end;

  if (percentage) {
    count = count * 100 / total;
  }
  return count;
}

/****************************************************************************
  Is there road of the given type cardinally near tile?
****************************************************************************/
bool is_road_card_near(const struct tile *ptile, const struct road_type *proad)
{
  cardinal_adjc_iterate(ptile, adjc_tile) {
    if (tile_has_road(adjc_tile, proad)) {
      return TRUE;
    }
  } cardinal_adjc_iterate_end;

  return FALSE;
}

/****************************************************************************
  Is there road of the given type near tile?
****************************************************************************/
bool is_road_near_tile(const struct tile *ptile, const struct road_type *proad)
{
  adjc_iterate(ptile, adjc_tile) {
    if (tile_has_road(adjc_tile, proad)) {
      return TRUE;
    }
  } adjc_iterate_end;

  return FALSE;
}

/****************************************************************************
  Check if road provides effect
****************************************************************************/
bool road_has_flag(const struct road_type *proad, enum road_flag_id flag)
{
  return BV_ISSET(proad->flags, flag);
}

/****************************************************************************
  Is tile native to road?
****************************************************************************/
bool is_native_tile_to_road(const struct road_type *proad,
                            const struct tile *ptile)
{
  if (road_has_flag(proad, RF_RIVER)) {
    if (!terrain_has_flag(tile_terrain(ptile), TER_CAN_HAVE_RIVER)) {
      return FALSE;
    }
  } else if (tile_terrain(ptile)->road_time == 0) {
    return FALSE;
  }

  return are_reqs_active(NULL, NULL, NULL, ptile,
                         NULL, NULL, NULL, &proad->reqs, RPT_POSSIBLE);
}
 
/****************************************************************************
  Returns next road that unit or player can build to tile.
****************************************************************************/
struct road_type *next_road_for_tile(struct tile *ptile, struct player *pplayer,
                                     struct unit *punit)
{
  fc_assert(punit != NULL || pplayer != NULL);

  road_type_iterate(proad) {
    if (!tile_has_road(ptile, proad)) {
      if (punit != NULL) {
        if (can_build_road(proad, punit, ptile)) {
          return proad;
        }
      } else {
        if (player_can_build_road(proad, pplayer, ptile)) {
          return proad;
        }
      }
    }
  } road_type_iterate_end;

  return NULL;
}
