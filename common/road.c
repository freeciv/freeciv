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

/* common */
#include "fc_types.h"
#include "game.h"
#include "movement.h"
#include "name_translation.h"
#include "unittype.h"

#include "road.h"

static struct road_type roads[ROAD_LAST] =
  {
    { ROAD_ROAD, {}, SINGLE_MOVE, ACTIVITY_ROAD, S_ROAD },
    { ROAD_RAILROAD, {}, SINGLE_MOVE, ACTIVITY_RAILROAD, S_RAILROAD }
  };

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
  Return activity that is required in order to build given road type.
****************************************************************************/
enum unit_activity road_activity(struct road_type *road)
{
  return road->act;
}

/****************************************************************************
  Return road type that is built by give activity. Returns ROAD_LAST if
  activity is not road building activity at all.
****************************************************************************/
struct road_type *road_by_activity(enum unit_activity act)
{
  road_type_iterate(road) {
    if (road->act == act) {
      return road;
    }
  } road_type_iterate_end;

  return NULL;
}

/****************************************************************************
  Return tile special that represents this road type.
****************************************************************************/
enum tile_special_type road_special(const struct road_type *road)
{
  return road->special;
}

/****************************************************************************
  Return road type represented by given special, or NULL if special does
  not represent road type at all.
****************************************************************************/
struct road_type *road_by_special(enum tile_special_type spe)
{
  road_type_iterate(road) {
    if (road->special == spe) {
      return road;
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
const char *road_rule_name(struct road_type *road)
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

/****************************************************************************
  Is road native to unit class?
****************************************************************************/
bool is_native_road_to_uclass(const struct road_type *proad,
                              const struct unit_class *pclass)
{
  return BV_ISSET(proad->native_to, uclass_index(pclass));
}

/****************************************************************************
  Tells if player can build road to tile with suitable unit.
****************************************************************************/
static bool can_build_road_base(const struct road_type *road,
                                const struct player *pplayer,
                                const struct tile *ptile)
{
  if (!terrain_control.may_road) {
    return FALSE;
  }

  if (tile_has_road(ptile, road)) {
    /* Road exist already */
    return FALSE;
  }

  if (tile_terrain(ptile)->road_time == 0) {
    return FALSE;
  }

  if (tile_has_special(ptile, S_RIVER)
      && !player_knows_techs_with_flag(pplayer, TF_BRIDGE)) {
    return FALSE;
  }

  return TRUE;
}

/****************************************************************************
  Tells if player can build road to tile with suitable unit.
****************************************************************************/
bool player_can_build_road(struct road_type *road,
                           const struct player *pplayer,
                           const struct tile *ptile)
{
  if (!can_build_road_base(road, pplayer, ptile)) {
    return FALSE;
  }

  return are_reqs_active(pplayer, NULL, NULL, ptile,
                         NULL, NULL, NULL, &road->reqs,
                         RPT_POSSIBLE);
}

/****************************************************************************
  Tells if unit can build road on tile.
****************************************************************************/
bool can_build_road(struct road_type *road,
		    const struct unit *punit,
		    const struct tile *ptile)
{
  struct player *pplayer = unit_owner(punit);

  if (!can_build_road_base(road, pplayer, ptile)) {
    return FALSE;
  }

  return are_reqs_active(pplayer, NULL, NULL, ptile,
                         unit_type(punit), NULL, NULL, &road->reqs,
                         RPT_CERTAIN);
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
  Return road type matching given eroad type. This function exist only
  to help transition from old two-hardcoded-roads system to gane-roads.
****************************************************************************/
struct road_type *road_type_by_eroad(enum eroad type)
{
  road_type_iterate(proad) {
    if (proad->id == type) {
      return proad;
    }
  } road_type_iterate_end;

  return NULL;
}
