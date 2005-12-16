/********************************************************************** 
 Freeciv - Copyright (C) 2005 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifndef FC__THEMECOLORS_H
#define FC__THEMECOLORS_H

#include "registry.h"

#include "fc_types.h"

#include "colors_common.h"

/* The color system is designed on the assumption that almost, but
 * not quite, all displays will be truecolor. */

enum theme_color {
  COLOR_THEME_BACKGROUND_BROWN = COLOR_LAST, /* Background2 (brown) */
  COLOR_THEME_QUICK_INFO,	 /* Quick info Background color */
  COLOR_THEME_DISABLED,		 /* disable color */
  COLOR_THEME_CITY_PROD,	 /* city production color */
  COLOR_THEME_CITY_SUPPORT,	 /* city units support color */
  COLOR_THEME_CITY_TRADE,	 /* city trade color */
  COLOR_THEME_CITY_GOLD,	 /* city gold color */
  COLOR_THEME_CITY_LUX,		 /* city luxuries color */
  COLOR_THEME_CITY_FOOD_SURPLUS, /* city food surplus color */
  COLOR_THEME_CITY_UPKEEP,	 /* city upkeep color */
  COLOR_THEME_CITY_SCIENCE,	 /* city science color */
  COLOR_THEME_CITY_HAPPY,	 /* city happy color */
  COLOR_THEME_CITY_CELEB,	 /* city celebrating color */
  COLOR_THEME_RED_DISABLED,	 /* player at war but can't meet or get intel. data */
  THEME_COLOR_LAST
};

struct color;
struct theme_color_system;
struct theme;

struct color *theme_get_color(const struct theme *t, enum theme_color color);
        
/* Functions used by the theme to allocate the color system. */
struct theme_color_system *theme_color_system_read(struct section_file *file);

void theme_color_system_free(struct theme_color_system *colors);

#endif /* FC__THEMECOLORS_H */
