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

#include <gtk/gtk.h>

#include "gui_main_g.h"

extern GdkFont *        main_fontset;
extern GdkFont *        prod_fontset;
extern GdkGC *          civ_gc;
extern GdkGC *          mask_fg_gc;
extern GdkGC *          mask_bg_gc;
extern GdkGC *          fill_bg_gc;
extern GdkGC *          fill_tile_gc;
extern GdkGC *          thin_line_gc;
extern GdkGC *          thick_line_gc;
extern GdkPixmap *      gray50;
extern GdkPixmap *      gray25;
extern GdkPixmap *      black50;
extern GdkPixmap *      mask_bitmap;
extern GdkPixmap *      map_canvas_store;
extern int              map_canvas_store_twidth;
extern int              map_canvas_store_theight;
extern GdkPixmap *      overview_canvas_store;
extern int              overview_canvas_store_width;
extern int              overview_canvas_store_height;
extern GdkPixmap *      single_tile_pixmap;
extern int              single_tile_pixmap_width;
extern int              single_tile_pixmap_height;
extern GtkText *        main_message_area;
extern GtkWidget *      text_scrollbar;
extern GtkWidget *      toplevel;
extern GtkWidget *      top_vbox;
extern GtkWidget *      main_frame_civ_name;
extern GtkWidget *      main_label_info;
extern GtkWidget *      econ_label[10];
extern GtkWidget *      bulb_label;
extern GtkWidget *      sun_label;
extern GtkWidget *      flake_label;
extern GtkWidget *      government_label;
extern GtkWidget *      map_canvas;             /* GtkDrawingArea */
extern GtkWidget *      overview_canvas;        /* GtkDrawingArea */
extern GtkWidget *      timeout_label;
extern GtkWidget *      turn_done_button;
extern GtkWidget *      unit_info_label;
extern GtkWidget *      unit_info_frame;
extern GtkWidget *      map_horizontal_scrollbar;
extern GtkWidget *      map_vertical_scrollbar;
extern GdkWindow *      root_window;

#endif  /* FC__GUI_MAIN_H */
