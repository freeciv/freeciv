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
#ifndef FC__ROAD_H
#define FC__ROAD_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define SPECENUM_NAME road_flag_id
/* Tile with this road is considered native for units traveling the road. */
#define SPECENUM_VALUE0 RF_NATIVE_TILE
#define SPECENUM_VALUE0NAME "NativeTile"
#define SPECENUM_COUNT RF_COUNT
#include "specenum_gen.h"

BV_DEFINE(bv_road_flags, RF_COUNT);

struct road_type {
  int id;
  struct name_translation name;
  int move_cost;
  int tile_bonus[O_LAST];
  enum unit_activity act;
  enum tile_special_type special;

  struct requirement_vector reqs;
  bv_unit_classes native_to;
  bv_road_flags flags;
};

/* General road type accessor functions. */
Road_type_id road_count(void);
Road_type_id road_index(const struct road_type *proad);
Road_type_id road_number(const struct road_type *proad);

struct road_type *road_by_number(Road_type_id id);

enum unit_activity road_activity(struct road_type *road);
struct road_type *road_by_activity(enum unit_activity act);

enum tile_special_type road_special(const struct road_type *road);
struct road_type *road_by_special(enum tile_special_type spe);

const char *road_name_translation(struct road_type *road);
const char *road_rule_name(struct road_type *road);
struct road_type *road_type_by_rule_name(const char *name);

bool is_road_card_near(const struct tile *ptile, const struct road_type *proad);
bool is_road_near_tile(const struct tile *ptile, const struct road_type *proad);

bool road_has_flag(const struct road_type *proad, enum road_flag_id flag);

bool is_native_road_to_uclass(const struct road_type *proad,
                              const struct unit_class *pclass);

bool can_build_road(struct road_type *road,
		    const struct unit *punit,
		    const struct tile *ptile);
bool player_can_build_road(struct road_type *road,
			   const struct player *pplayer,
			   const struct tile *ptile);

struct road_type *road_type_by_eroad(enum eroad type);

void fill_road_vector_from_specials(bv_roads *roads, bv_special *specials);
void fill_special_vector_from_roads(bv_roads *roads, bv_special *specials);

#define road_type_iterate(_p)                    \
{                                                \
  int _i_;                                       \
  for (_i_ = 0; _i_ < ROAD_LAST ; _i_++) {       \
    struct road_type *_p = road_by_number(_i_);

#define road_type_iterate_end                    \
  }}


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__ROAD_H */
