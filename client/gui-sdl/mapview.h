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
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
 *********************************************************************/

#ifndef FC__MAPVIEW_H
#define FC__MAPVIEW_H

#include "mapview_g.h"
#include "mapview_common.h"

struct unit;
struct city;
  
void center_minimap_on_minimap_window(void);
void tmp_map_surfaces_init(void);
void redraw_unit_info_label(struct unit *pUnit);
void real_blink_active_unit(void);
SDL_Surface * create_city_map(struct city *pCity);
SDL_Surface * get_terrain_surface(int x, int y);  
int correction_map_pos(int *pCol, int *pRow);
void put_unit_pixmap_draw(struct unit *pUnit, SDL_Surface *pDest,
			  Sint16 map_x, Sint16 map_y);
void rebuild_focus_anim_frames(void);
void toggle_overview_mode(void);

void flush_rect(SDL_Rect rect);
void sdl_dirty_rect(SDL_Rect rect);
void unqueue_flush(void);
void queue_flush(void);
void flush_all(void);

#define sdl_contains_special(store, special)	\
	((store & special) == special)

#endif	/* FC__MAPVIEW_H */
