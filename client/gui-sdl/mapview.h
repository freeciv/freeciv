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
                          mapview.h  -  description
                             -------------------
    begin                : Aug 10 2002
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafa³ Bursig <bursig@poczta.fm>
 *********************************************************************/

#ifndef FC__MAPVIEW_H
#define FC__MAPVIEW_H

#include "mapview_g.h"
#include "mapview_common.h"

struct unit;
struct city;

void get_new_view_rectsize(void);
void tmp_map_surfaces_init(void);
void redraw_unit_info_label(struct unit *pUnit, struct GUI *pInfo_Window);
void real_blink_active_unit(void);
SDL_Surface * create_city_map(struct city *pCity);
SDL_Surface * get_terrain_surface(int x , int y);  
int correction_map_pos(int *pCol, int *pRow);
void put_unit_pixmap_draw(struct unit *pUnit, SDL_Surface * pDest,
			  Sint16 map_x, Sint16 map_y);
void flush_rect(SDL_Rect rect);
void sdl_dirty_rect(SDL_Rect rect);

#endif	/* FC__MAPVIEW_H */
