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
                          gui_main.h  -  description
                             -------------------
    begin                : Sep 6 2002
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafa³ Bursig <bursig@poczta.fm>
 ***************************************************************************/

#ifndef FC__GUI_MAIN_H
#define FC__GUI_MAIN_H

#include "gui_main_g.h"

/* #define DEBUG_SDL */

/* SDL client Flags */
#define CF_NONE				0
#define CF_ORDERS_WIDGETS_CREATED	0x01
#define CF_MAP_UNIT_W_CREATED		0x02
#define CF_UNIT_INFO_SHOW		0x04
#define CF_MINI_MAP_SHOW		0x08
#define CF_OPTION_OPEN			0x10
#define CF_OPTION_MAIN			0x20
#define CF_GANE_JUST_STARTED		0x40
#define CF_REVOLUTION			0x80
#define CF_TOGGLED_FULLSCREEN		0x100
#define CF_CITY_DIALOG_IS_OPEN		0x200
#define CF_CHANGED_PROD			0x400
#define CF_CHANGED_CITY_NAME		0x800
#define CF_CITY_STATUS_SPECIAL		0x1000
#define CF_CHANGE_TAXRATE_TAX_BLOCK	0x2000
#define CF_CHANGE_TAXRATE_LUX_BLOCK	0x4000
#define CF_CHANGE_TAXRATE_SCI_BLOCK	0x8000
#define CF_CIV3_CITY_TEXT_STYLE		0x10000
#define CF_DRAW_MAP_DITHER		0x20000

extern struct Sdl Main;
extern struct GUI *pSellected_Widget;
extern Uint32 SDL_Client_Flags;

void add_autoconnect_to_timer(void);

#endif				/* FC__GUI_MAIN_H */
