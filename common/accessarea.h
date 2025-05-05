/***********************************************************************
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
#ifndef FC__ACCESSAREA_H
#define FC__ACCESSAREA_H

/* common */
#include "city.h"
#include "fc_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* get 'struct access_area_list' and related functions: */
#define SPECLIST_TAG aarea
#define SPECLIST_TYPE struct access_area
#include "speclist.h"

#define aarea_list_iterate(aarealist, parea) \
    TYPED_LIST_ITERATE(struct access_area, aarealist, parea)
#define aarea_list_iterate_end  LIST_ITERATE_END

void access_info_init(const struct unit_type *aunit);
void access_info_close(void);
const struct unit_type *access_info_access_unit(void);

void access_areas_refresh(struct civ_map *nmap, struct player *plr);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__ACCESSAREA_H */
