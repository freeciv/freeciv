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
                          colors.h  -  description
                             -------------------
    begin                : Mon Jul 15 2002
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafa³ Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifndef FC__COLORS_H
#define FC__COLORS_H

#include "colors_g.h"

enum SDLClient_std_color {
  COLOR_STD_BACKGROUND_BROWN = COLOR_STD_LAST,	/* Background2 (brown) */
  COLOR_STD_GRID,		/* Grid line color */
  QUICK_INFO,			/* Quick info Background color */
  SDLCLIENT_STD_COLOR_LAST
};

SDL_Color *get_game_colorRGB(Uint32 color_offset);
Uint32 get_game_color(Uint32 color_offset);

#endif				/* FC__COLORS_H */
