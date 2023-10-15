/***********************************************************************
 Freeciv - Copyright (C) 1996-2005 - Freeciv Development Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef FC__RALLYPOINTDLG_H
#define FC__RALLYPOINTDLG_H

enum rally_phase { RALLY_NONE, RALLY_CITY, RALLY_TILE };

void rally_dialog_popup(void);
enum rally_phase rally_placement_phase(void);
bool rally_set_tile(struct tile *ptile);

#endif  /* FC__RALLYPOINTDLG_H */
