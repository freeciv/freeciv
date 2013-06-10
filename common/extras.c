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

/* common */
#include "base.h"
#include "game.h"
#include "road.h"

#include "extras.h"

#define MAX_EXTRA_TYPES (S_LAST + MAX_BASE_TYPES + MAX_ROAD_TYPES)

static struct extra_type extras[MAX_EXTRA_TYPES];

/****************************************************************************
  Initialize extras structures.
****************************************************************************/
void extras_init(void)
{
  int i;

  for (i = 0; i < S_LAST; i++) {
    extras[i].id = i;
    extras[i].type = EXTRA_SPECIAL;
    extras[i].data.special = i;
  }

  for (; i < MAX_EXTRA_TYPES; i++) {
    extras[i].id = i;
  }
}

/****************************************************************************
  Free the memory associated with extras
****************************************************************************/
void extras_free(void)
{
  base_types_free();
  road_types_free();
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
