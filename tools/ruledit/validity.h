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
#ifndef FC__VALIDITY_H
#define FC__VALIDITY_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef void (*requirers_cb)(const char *msg);

bool is_tech_needed(struct advance *padv, requirers_cb cb);
bool is_building_needed(struct impr_type *pimpr, requirers_cb cb);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__VALIDITY_H */
