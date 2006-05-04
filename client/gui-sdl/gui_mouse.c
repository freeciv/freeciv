/********************************************************************** 
 Freeciv - Copyright (C) 2006 The Freeciv Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <SDL/SDL.h>

/* client */
#include "tilespec.h"

/* gui-sdl */
#include "graphics.h"

#include "gui_mouse.h"

SDL_Cursor *fc_cursors[CURSOR_LAST][NUM_CURSOR_FRAMES];

bool do_cursor_animation = TRUE;
enum cursor_type mouse_cursor_type = CURSOR_DEFAULT;
bool mouse_cursor_changed = FALSE;

/**************************************************************************
  convert SDL surface to SDL cursor format (code from SDL-dev mailing list)
**************************************************************************/
static SDL_Cursor *SurfaceToCursor(SDL_Surface *image, int hx, int hy) {
        int             w, x, y;
        Uint8           *data, *mask, *d, *m, r, g, b, a;
        Uint32          color;
        SDL_Cursor      *cursor;

        w = (image->w + 7) / 8;
        data = (Uint8 *)fc_calloc(1, w * image->h * 2);
        if (data == NULL)
                return NULL;
        /*memset(data, 0, w * image->h * 2);*/
        mask = data + w * image->h;
	lock_surf(image);
        for (y = 0; y < image->h; y++) {
                d = data + y * w;
                m = mask + y * w;
                for (x = 0; x < image->w; x++) {
                        color = getpixel(image, x, y);
                        SDL_GetRGBA(color, image->format, &r, &g, &b, &a);                    
                        if (((image->flags & SDL_SRCCOLORKEY) == 0)
			    || (a != 0)) {
                                color = (r + g + b) / 3;
                                m[x / 8] |= 128 >> (x & 7);
                                if (color < 128)
                                        d[x / 8] |= 128 >> (x & 7);
                        }
                }
        }
	unlock_surf(image);
        
        cursor = SDL_CreateCursor(data, mask, w * 8, image->h, hx, hy);
	
	FC_FREE(data);
        return cursor;
}

/**************************************************************************
  Load the cursors (mouse substitute sprites), including a goto cursor,
  an airdrop cursor, a nuke cursor, and a patrol cursor.
**************************************************************************/
void load_cursors(void)
{
  enum cursor_type cursor;  
  int frame;
  SDL_Surface *pSurf;

  pStd_Cursor = SDL_GetCursor();
    
  for (cursor = 0; cursor < CURSOR_LAST; cursor++) {
    for (frame = 0; frame < NUM_CURSOR_FRAMES; frame++) {
      int hot_x, hot_y;
      struct sprite *sprite
	= get_cursor_sprite(tileset, cursor, &hot_x, &hot_y, frame);
      
      pSurf = GET_SURF(sprite);
      SDL_SetColorKey(pSurf, SDL_SRCCOLORKEY, 0);

      fc_cursors[cursor][frame] = SurfaceToCursor(pSurf, hot_x, hot_y);
    }
  }
}

/**************************************************************************
  ...
**************************************************************************/
void unload_cursors(void)
{
  enum cursor_type cursor;  
  int frame;
  
  for (cursor = 0; cursor < CURSOR_LAST; cursor++) {
    for (frame = 0; frame < NUM_CURSOR_FRAMES; frame++) {
      SDL_FreeCursor(fc_cursors[cursor][frame]);
    }
  }
  
  SDL_FreeCursor(pStd_Cursor);
  SDL_FreeCursor(pGoto_Cursor);
  SDL_FreeCursor(pDrop_Cursor);
  SDL_FreeCursor(pNuke_Cursor);
  SDL_FreeCursor(pPatrol_Cursor);
  return;
}

/**************************************************************************
  This function is used to animate the mouse cursor. 
**************************************************************************/
void animate_mouse_cursor(void)
{
  static int cursor_frame = 0;

  if (!mouse_cursor_changed) {
    return;
  }
  
  if (mouse_cursor_type == CURSOR_DEFAULT) {
    SDL_SetCursor(pStd_Cursor);
    mouse_cursor_changed = FALSE;
  } else {
    if (!do_cursor_animation || (cursor_frame == NUM_CURSOR_FRAMES)) {
      cursor_frame = 0;
    }
  
    SDL_SetCursor(fc_cursors[mouse_cursor_type][cursor_frame++]);    
  }
}

/**************************************************************************
  This function will change the current mouse cursor.
**************************************************************************/
void update_mouse_cursor(enum cursor_type new_cursor_type)
{
  mouse_cursor_type = new_cursor_type;
  mouse_cursor_changed = TRUE;
}
