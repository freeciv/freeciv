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
#include "road.h"

#include "extras.h"

#define MAX_EXTRAS_TYPES (S_LAST + MAX_BASE_TYPES + MAX_ROAD_TYPES)

static struct extras_type extras[MAX_EXTRAS_TYPES];

/****************************************************************************
  Initialize extras structures.
****************************************************************************/
void extras_init(void)
{
  int i;

  for (i = 0; i < S_LAST; i++) {
    extras[i].id = i;
    extras[i].type = EXTRAS_SPECIAL;
    extras[i].data.special = i;
  }
  for (; i < S_LAST + MAX_BASE_TYPES; i++) {
    extras[i].id = i;
    extras[i].type = EXTRAS_BASE;
  }
  for (; i < MAX_EXTRAS_TYPES; i++) {
    extras[i].id = i;
    extras[i].type = EXTRAS_ROAD;
  }


  base_types_init();
  road_types_init();
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
struct extras_type *extras_by_number(int id)
{
  fc_assert_ret_val(id >= 0 && id < MAX_EXTRAS_TYPES, NULL);

  return &extras[id];
}

/****************************************************************************
  Get extras of the given type and given subid
****************************************************************************/
struct extras_type *extras_type_get(enum extras_type_id type, int subid)
{
  switch (type) {
  case EXTRAS_SPECIAL:
    return extras_by_number(subid);
  case EXTRAS_BASE:
    return extras_by_number(S_LAST + subid);
  case EXTRAS_ROAD:
    return extras_by_number(S_LAST + MAX_BASE_TYPES + subid);
  }

  return NULL;
}
