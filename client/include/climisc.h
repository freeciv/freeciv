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
#ifndef FC__CLIMISC_H
#define FC__CLIMISC_H

struct city;

void client_remove_player(int plr_id);
void client_remove_city(struct city *pcity);
void client_remove_unit(int unit_id);
char *tilefilename(char *name);
void climap_init_continents(void);
void climap_update_continents(int x, int y);

#endif  /* FC__CLIMISC_H */

