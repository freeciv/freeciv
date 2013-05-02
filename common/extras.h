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

enum extras_type_id 
  {
    EXTRAS_ROAD,
    EXTRAS_BASE,
    EXTRAS_SPECIAL
  };

void extras_init(void);
void extras_free(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__ROAD_H */
