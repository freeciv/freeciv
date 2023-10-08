/***********************************************************************
 Freeciv - Copyright (C) 2006 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef FC__WIDGET_WINDOW_H
#define FC__WIDGET_WINDOW_H

struct widget *create_window(struct gui_layer *pdest, utf8_str *title,
                             Uint16 w, Uint16 h, Uint32 flags);

struct widget *create_window_skeleton(struct gui_layer *pdest,
                                      utf8_str *title, Uint32 flags);

bool resize_window(struct widget *pwindow, SDL_Surface *bcgd,
                   SDL_Color *pcolor, Uint16 new_w, Uint16 new_h);

bool move_window(struct widget *pwindow);

#endif /* FC__WIDGET_WINDOW_H */
