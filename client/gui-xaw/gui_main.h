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
#ifndef FC__GUI_MAIN_H
#define FC__GUI_MAIN_H

#include <X11/Intrinsic.h>

#include "gui_main_g.h"

void main_quit_freeciv(void);
void main_show_info_popup(XEvent *event);

extern Atom         wm_delete_window;
extern Display     *display;
extern int          display_depth;
extern int          screen_number;
extern GC           civ_gc; 
extern GC           fill_bg_gc;
extern GC           fill_tile_gc;
extern GC           font_gc;
extern GC           prod_font_gc;
extern Pixmap       gray50;
extern Pixmap       gray25;
extern Pixmap       single_tile_pixmap;
extern Pixmap       map_canvas_store;
extern int          map_canvas_store_twidth;
extern int          map_canvas_store_theight;
extern Pixmap       overview_canvas_store;
extern int          overview_canvas_store_width;
extern int          overview_canvas_store_height;
extern Widget       map_vertical_scrollbar;
extern Widget       map_horizontal_scrollbar;
extern Widget       left_column_form;
extern Widget       menu_form;
extern Widget       below_menu_form;
extern Widget       bottom_form;
extern Widget       map_form;
extern Widget       map_canvas;
extern Widget       overview_canvas;
extern Widget       econ_label[10];
extern Widget       bulb_label;
extern Widget       sun_label;
extern Widget       flake_label;
extern Widget       government_label;
extern Widget       timeout_label;
extern Widget       unit_info_label;
extern Widget       turn_done_button;
extern Widget       info_command;
extern Widget       toplevel;
extern Widget       main_form;
extern Window       root_window;
extern Widget       inputline_text;
extern Widget       outputwindow_text;
extern XFontStruct *main_font_struct;
extern XFontStruct *prod_font_struct;
extern XtAppContext app_context;

#endif  /* FC__GUI_MAIN_H */
