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

/***************************************************************************
                          gui_zoom.h  -  description
                             -------------------
    begin                : Jul 25 2002
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
 ***************************************************************************/

#ifndef _GUI_ZOOM_H
#define _GUI_ZOOM_H

SDL_Surface *ZoomSurface(const SDL_Surface * pSrc,
			 double zoomx, double zoomy, int smooth);
SDL_Surface *ResizeSurface(const SDL_Surface * pSrc, Uint16 new_width,
			   Uint16 new_height, int smooth);

#endif				/* _GUI_ZOOM_h */
