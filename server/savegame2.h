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
#ifndef FC__SAVEGAME2_H
#define FC__SAVEGAME2_H

struct section_file;

void savegame2_load(struct section_file *file);
void savegame2_save(struct section_file *file, const char *save_reason,
                    bool scenario);

void mainmap_specials_to_roads(void);
void plrmap_specials_to_roads(struct player *plr);

#endif /* FC__SAVEGAME2_H */
