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
                          colors.c  -  description
                             -------------------
    begin                : Mon Jul 15 2002
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafa³ Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <SDL/SDL.h>

#include "colors.h"

/*
  r, g, b, a
*/
static SDL_Color colors_standard_rgb[COLOR_STD_LAST] = {
  {0, 0, 0, 255},		/* Black */
  {255, 255, 255, 255},		/* White */
  {255, 0, 0, 255},		/* Red */
  {255, 255, 0, 255},		/* Yellow */
  {0, 255, 200, 255},		/* Cyan */
  {0, 200, 0, 255},		/* Ground (green) */
  {0, 0, 200, 255},		/* Ocean (blue) */
  {86, 86, 86, 255},		/* Background (gray) */
  {128, 0, 0, 255},		/* race0 */
  {128, 255, 255, 255},		/* race1 */
  {255, 0, 0, 255},		/* race2 */
  {255, 0, 128, 255},		/* race3 */
  {0, 0, 128, 255},		/* race4 */
  {255, 0, 255, 255},		/* race5 */
  {255, 128, 0, 255},		/* race6 */
  {255, 255, 128, 255},		/* race7 */
  {255, 128, 128, 255},		/* race8 */
  {0, 0, 255, 255},		/* race9 */
  {0, 255, 0, 255},		/* race10 */
  {0, 128, 128, 255},		/* race11 */
  {0, 64, 64, 255},		/* race12 */
  {198, 198, 198, 255}		/* race13 */
};

/*
  r, g, b, a
*/
static SDL_Color SDLClient_standard_rgba_colors[SDLCLIENT_STD_COLOR_LAST -
						COLOR_STD_LAST] = {
  {138, 114, 82, 255},		/* Background2 (brown) */
  {190, 190, 190, 255},		/* Grid line color */
  {0, 200, 0, 150},		/* Quick info (green) */
  {0, 0, 0, 128},		/* FOG OF WAR color */
  {90, 90, 90, 255},		/* disable color */
  {125, 170, 220, 255},		/* city production color */
  {20, 210, 200, 255},		/* city units support color */
  {226, 82, 13, 255},		/* city trade color */
  {220, 186, 60, 255},		/* city gold color */
  {238, 156, 7, 255},		/* city luxuries color */
  {105, 190, 90, 255},		/* city food surplus color */
  {12, 18, 108, 255},		/* city unkeep color */
  {212, 172, 206, 255},		/* city science color */
  {255, 255, 0, 255},		/* city happy color */
  {190, 115, 5, 255}		/* city celebrating color */
};

/**************************************************************************
  Initialize colors for the game.
**************************************************************************/
void init_color_system(void)
{
}

/**************************************************************************
  ...
**************************************************************************/
SDL_Color * get_game_colorRGB(Uint32 color_offset)
{
  if (color_offset >= COLOR_STD_LAST) {
    return &SDLClient_standard_rgba_colors[color_offset - COLOR_STD_LAST];
  }
  return &colors_standard_rgb[color_offset];
}

/**************************************************************************
  ...
**************************************************************************/
Uint32 get_game_color(Uint32 color_offset , SDL_Surface *pDest)
{
  SDL_Color *pColor = get_game_colorRGB(color_offset);
  return SDL_MapRGBA(pDest->format, pColor->r, pColor->g,
		     pColor->b, pColor->unused);
}
