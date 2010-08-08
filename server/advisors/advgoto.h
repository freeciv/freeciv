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
#ifndef FC__ADVGOTO_H
#define FC__ADVGOTO_H

bool adv_follow_path(struct unit *punit, struct pf_path *path,
                     struct tile *ptile);
bool adv_unit_execute_path(struct unit *punit, struct pf_path *path);

#endif   /* FC__ADVGOTO_H */
