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
#ifndef FC__EXTRAS_H
#define FC__EXTRAS_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* common */
#include "base.h"
#include "fc_types.h"
#include "road.h"

struct extras_type
{
  int id;
  enum extras_type_id type;

  union
  {
    enum tile_special_type special;
    struct base_type base;
    struct road_type road;
  } data;
};

void extras_init(void);
void extras_free(void);

struct extras_type *extras_by_number(int id);

struct extras_type *extras_type_get(enum extras_type_id type, int subid);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__ROAD_H */
