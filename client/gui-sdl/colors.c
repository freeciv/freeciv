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
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "SDL.h"

/* client */
#include "tilespec.h"

/* gui-sdl */
#include "themespec.h"

#include "colors.h"

/**************************************************************************
  ...
**************************************************************************/
SDL_Color * get_game_colorRGB(int color_id)
{
  if (color_id >= color_std_max()) {
    enum theme_color themecolor = color_id - COLOR_LAST;
    return theme_get_color(theme, themecolor)->color;
  } else {
    enum color_std stdcolor = color_id;

    fc_assert_ret_val(color_std_is_valid(stdcolor), NULL);

    return get_color(tileset, stdcolor)->color;
  }
}

/**************************************************************************
  ...
**************************************************************************/
struct color *color_alloc_rgba(int r, int g, int b, int a) {

  struct color *result = fc_malloc(sizeof(*result));	
	
  SDL_Color *pcolor = fc_malloc(sizeof(*pcolor));
  pcolor->r = r;
  pcolor->g = g;
  pcolor->b = b;
  pcolor->unused = a;
	
  result->color = pcolor;
  
  return result;
}

struct color *color_alloc(int r, int g, int b) {

  struct color *result = fc_malloc(sizeof(*result));	
	
  SDL_Color *pcolor = fc_malloc(sizeof(*pcolor));
  pcolor->r = r;
  pcolor->g = g;
  pcolor->b = b;
  pcolor->unused = 255;
	
  result->color = pcolor;
  
  return result;
}

void color_free(struct color *pcolor) {
  if (!pcolor) {
    return;
  }

  if (pcolor->color) {
    free(pcolor->color);
  }
  free(pcolor);
}
