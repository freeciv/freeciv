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
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
 ***************************************************************************/

#ifndef FC__GUI_MAIN_H
#define FC__GUI_MAIN_H

#include "gui_main_g.h"

/* #define DEBUG_SDL */

/* SDL client Flags */
#define CF_NONE				0
#define CF_ORDERS_WIDGETS_CREATED	(1<<0)
#define CF_MAP_UNIT_W_CREATED		(1<<1)
#define CF_UNIT_INFO_SHOW		(1<<2)
#define CF_MINI_MAP_SHOW		(1<<3)
#define CF_OPTION_OPEN			(1<<4)
#define CF_OPTION_MAIN			(1<<5)
#define CF_GANE_JUST_STARTED		(1<<6)
#define CF_REVOLUTION			(1<<7)
#define CF_TOGGLED_FULLSCREEN		(1<<8)
#define CF_FOCUS_ANIMATION		(1<<9)
#define CF_CHANGED_PROD			(1<<10)
#define CF_CHANGED_CITY_NAME		(1<<11)
#define CF_CITY_STATUS_SPECIAL		(1<<12)
#define CF_CHANGE_TAXRATE_LUX_BLOCK	(1<<13)
#define CF_CHANGE_TAXRATE_SCI_BLOCK	(1<<14)
#define CF_CIV3_CITY_TEXT_STYLE		(1<<15)
#define CF_DRAW_MAP_DITHER		(1<<16)
#define CF_DRAW_CITY_GRID		(1<<17)
#define CF_DRAW_CITY_WORKER_GRID	(1<<18)
#define CF_DRAW_PLAYERS_WAR_STATUS	(1<<19)
#define CF_DRAW_PLAYERS_CEASEFIRE_STATUS	(1<<20)
#define CF_DRAW_PLAYERS_PEACE_STATUS	(1<<21)
#define CF_DRAW_PLAYERS_ALLIANCE_STATUS	(1<<22)
#define CF_DRAW_PLAYERS_NEUTRAL_STATUS	(1<<23)

extern struct canvas Main;
extern struct GUI *pSellected_Widget;
extern Uint32 SDL_Client_Flags;
extern bool LSHIFT;
extern bool RSHIFT;
extern bool LCTRL;
extern bool RCTRL;
extern bool LALT;
extern bool do_focus_animation;

void force_exit_from_event_loop(void);
void add_autoconnect_to_timer(void);
void enable_focus_animation(void);
void disable_focus_animation(void);


#define DEFAULT_MOVE_STEP 5
extern int MOVE_STEP_X, MOVE_STEP_Y;
int FilterMouseMotionEvents(const SDL_Event *event);

Uint16 gui_event_loop(void *pData, void (*loop_action)(void *pData),
	Uint16 (*key_down_handler)(SDL_keysym Key, void *pData),
        Uint16 (*key_up_handler)(SDL_keysym Key, void *pData),
	Uint16 (*mouse_button_down_handler)(SDL_MouseButtonEvent *pButtonEvent, void *pData),
        Uint16 (*mouse_button_up_handler)(SDL_MouseButtonEvent *pButtonEvent, void *pData),
        Uint16 (*mouse_motion_handler)(SDL_MouseMotionEvent *pMotionEvent, void *pData));

#endif	/* FC__GUI_MAIN_H */
