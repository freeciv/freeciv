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

/**********************************************************************
                          dialogs.h  -  description
                             -------------------
    begin                : Wed Jul 24 2002
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafa³ Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifndef FC__DIALOGS_H
#define FC__DIALOGS_H


#include "dialogs_g.h"

struct tile;
  
void popup_advanced_terrain_dialog(int x , int y);
const char *sdl_map_get_tile_info_text(int x, int y);

#endif	/* FC__DIALOGS_H */
