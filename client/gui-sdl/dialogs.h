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
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifndef FC__DIALOGS_H
#define FC__DIALOGS_H


#include "dialogs_g.h"

struct tile;
struct GUI;
  
void popup_advanced_terrain_dialog(int x , int y);
const char *sdl_map_get_tile_info_text(struct tile *pTile);
const char *sdl_get_tile_defense_info_text(struct tile *pTile);
void put_window_near_map_tile(struct GUI *pWindow,
  		int window_width, int window_height, int x, int y);
void popup_unit_upgrade_dlg(struct unit *pUnit, bool city);
  
#endif	/* FC__DIALOGS_H */
