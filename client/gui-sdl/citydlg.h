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
                          citydlg.h  -  description
                             -------------------
    begin                : Wed Sep 04 2002
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafa³ Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifndef FC__CITYDLG_H
#define FC__CITYDLG_H

#include "citydlg_g.h"

void undraw_city_dialog(void);
void refresh_city_dlg_background(void);
SDL_Rect *get_citydlg_rect(void);
void popup_change_production_dialog(struct city *pCity, SDL_Surface *pDest);
void popup_hurry_production_dialog(struct city *pCity, SDL_Surface *pDest);
  
#endif	/* FC__CITYDLG_H */
