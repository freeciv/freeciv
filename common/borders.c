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
#include <config.h>
#endif

#include <assert.h>

/* utility */
#include "fcintl.h"
#include "log.h"

/* common */
#include "game.h"
#include "tile.h"
#include "unit.h"

#include "borders.h"

/*************************************************************************
  Border radius sq from given border source tile.
*************************************************************************/
int tile_border_radius_sq(struct tile *ptile)
{
  struct city *pcity;
  int radius_sq = 0;

  if (game.info.borders_sq == 0) {
    return 0;
  }

  pcity = tile_city(ptile);

  if (pcity) {
    radius_sq = MAX(game.info.borders_sq, 2*2+1*1);
    radius_sq += pcity->size;
  } else {
    base_type_iterate(pbase) {
      if (tile_has_base(ptile, pbase) && base_has_flag(pbase, BF_CLAIM_TERRITORY)) {
        radius_sq = 5;
        break;
      }
    } base_type_iterate_end;
  }

  return radius_sq;
}

/*************************************************************************
  Is given tile source to borders.
*************************************************************************/
bool is_border_source(struct tile *ptile)
{
  if (tile_city(ptile)) {
    return TRUE;
  }

  if (tile_owner(ptile) != NULL) {
    base_type_iterate(pbase) {
      if (tile_has_base(ptile, pbase) && base_has_flag(pbase, BF_CLAIM_TERRITORY)) {
        return TRUE;
      }
    } base_type_iterate_end;
  }

  return FALSE;
}
