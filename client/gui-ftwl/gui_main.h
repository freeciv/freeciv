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

#include "colors_g.h"
#include "gui_main_g.h"
#include "common_types.h"

struct osda;

struct canvas {
  struct osda *osda;
  struct sw_widget *widget;
};

void popup_mapcanvas(void);
void popdown_mapcanvas(void);

extern struct sw_widget *root_window;

be_color enum_color_to_be_color(int color);

enum color_ext {
  COLOR_EXT_BLUE = COLOR_STD_LAST,
  COLOR_EXT_TOOLTIP_FOREGROUND,
  COLOR_EXT_TOOLTIP_BACKGROUND,
  COLOR_EXT_GRAY,
  COLOR_EXT_LIGHT_GRAY,
  COLOR_EXT_GREEN,
  COLOR_EXT_DEFAULT_WINDOW_BACKGROUND,
  COLOR_EXT_SELECTED_ROW,
  COLOR_EXT_DARK_BLUE,
  COLOR_EXT_OUTERSPACE_FG,
  COLOR_EXT_OUTERSPACE_BG,
  COLOR_EXT_LAST
};

#endif				/* FC__GUI_MAIN_H */
