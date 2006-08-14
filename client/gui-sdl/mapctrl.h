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
                          mapctrl.h  -  description
                             -------------------
    begin                : Thu Sep 05 2002
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
 *********************************************************************/

#ifndef FC__MAPCTRL_H
#define FC__MAPCTRL_H

#include <SDL/SDL.h>

#include "fc_types.h"

#include "gui_main.h"

#include "mapctrl_g.h"

#ifdef SMALL_SCREEN

#define BLOCKM_W		28
#define BLOCKU_W                15
#define DEFAULT_OVERVIEW_W	78
#define DEFAULT_OVERVIEW_H	52
#define DEFAULT_UNITS_W		(78 + BLOCKU_W)
#define DEFAULT_UNITS_H		52

#else

#define BLOCKM_W		52
#define BLOCKU_W                30
#define DEFAULT_OVERVIEW_W	156
#define DEFAULT_OVERVIEW_H	104
#define DEFAULT_UNITS_W		(156 + BLOCKU_W)
#define DEFAULT_UNITS_H		104

#endif

extern int MINI_MAP_W;
extern int MINI_MAP_H;

void popdown_newcity_dialog(void);

void Init_MapView(void);
void Remake_MiniMap(int w, int h);
void reset_main_widget_dest_buffer(void);
void set_new_units_window_pos(void);
void set_new_mini_map_window_pos(void);
int resize_minimap(void);
int resize_unit_info(void);
struct widget * get_unit_info_window_widget(void);
struct widget * get_minimap_window_widget(void);
struct widget * get_tax_rates_widget(void);
struct widget * get_research_widget(void);
struct widget * get_revolution_widget(void);
void enable_and_redraw_find_city_button(void);
void enable_and_redraw_revolution_button(void);
void enable_main_widgets(void);
void disable_main_widgets(void);
bool map_event_handler(SDL_keysym Key);

void button_down_on_map(struct mouse_button_behavior *button_behavior);
void button_up_on_map(struct mouse_button_behavior *button_behavior);

#endif	/* FC__MAPCTRL_H */
