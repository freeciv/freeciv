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
#include <fc_config.h>
#endif

/* Common */
#include "base.h"
#include "movement.h"
#include "unittype.h"

#include "advruleset.h"

/**************************************************************************
  Initialise the unit data from the ruleset for the advisors.
**************************************************************************/
void adv_units_ruleset_init(void)
{
  bv_special special;
  bv_bases bases;

  BV_CLR_ALL(special); /* Can it move even without road */
  BV_CLR_ALL(bases);

  unit_class_iterate(pclass) {
    bool move_land_enabled  = FALSE; /* Can move at some land terrains */
    bool move_land_disabled = FALSE; /* Cannot move at some land terrains */
    bool move_sea_enabled   = FALSE; /* Can move at some ocean terrains */
    bool move_sea_disabled  = FALSE; /* Cannot move at some ocean terrains */

    terrain_type_iterate(pterrain) {
      if (is_native_to_class(pclass, pterrain, special, bases)) {
        /* Can move at terrain */
        if (is_ocean(pterrain)) {
          move_sea_enabled = TRUE;
        } else {
          move_land_enabled = TRUE;
        }
      } else {
        /* Cannot move at terrain */
        if (is_ocean(pterrain)) {
          move_sea_disabled = TRUE;
        } else {
          move_land_disabled = TRUE;
        }
      }
    } terrain_type_iterate_end;

    if (move_land_enabled && !move_land_disabled) {
      pclass->adv.land_move = MOVE_FULL;
    } else if (move_land_enabled && move_land_disabled) {
      pclass->adv.land_move = MOVE_PARTIAL;
    } else {
      fc_assert(!move_land_enabled);
      pclass->adv.land_move = MOVE_NONE;
    }

    if (move_sea_enabled && !move_sea_disabled) {
      pclass->adv.sea_move = MOVE_FULL;
    } else if (move_sea_enabled && move_sea_disabled) {
      pclass->adv.sea_move = MOVE_PARTIAL;
    } else {
      fc_assert(!move_sea_enabled);
      pclass->adv.sea_move = MOVE_NONE;
    }

  } unit_class_iterate_end;
}
