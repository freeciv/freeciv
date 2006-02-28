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

#include <SDL/SDL.h>

#include "dialogs_g.h"

struct GUI;
  
void popup_advanced_terrain_dialog(struct tile *ptile, Uint16 pos_x, Uint16 pos_y);
void popdown_advanced_terrain_dialog(void);

const char *sdl_map_get_tile_info_text(struct tile *pTile);
const char *sdl_get_tile_defense_info_text(struct tile *pTile);
void put_window_near_map_tile(struct GUI *pWindow,
  		int window_width, int window_height, struct tile *ptile);
void popup_unit_upgrade_dlg(struct unit *pUnit, bool city);
void popup_revolution_dialog(void);

#define create_active_iconlabel(pBuf, pDest, pStr, pString, pCallback)   \
do { 									 \
  pStr = create_str16_from_char(pString, 10);				 \
  pStr->style |= TTF_STYLE_BOLD;					 \
  pBuf = create_iconlabel(NULL, pDest, pStr, 				 \
    	     (WF_DRAW_THEME_TRANSPARENT|WF_DRAW_TEXT_LABEL_WITH_SPACE)); \
  pBuf->string16->bgcol.unused = 128;					 \
  pBuf->string16->render = 3;						 \
  pBuf->action = pCallback;						 \
} while(0)

#endif	/* FC__DIALOGS_H */
