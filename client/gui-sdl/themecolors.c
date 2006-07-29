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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>

/* gui-sdl */
#include "colors.h"
#include "themespec.h"

#include "themecolors.h"

/* An RGBAcolor contains the R,G,B,A bitvalues for a color.  The color itself
 * holds the color structure for this color but may be NULL (it's allocated
 * on demand at runtime). */
struct rgbacolor {
  int r, g, b, a;
  struct color *color;
};

struct theme_color_system {
  struct rgbacolor colors[(THEME_COLOR_LAST - COLOR_LAST)];
};

static char *color_names[] = {
  "background_brown",
  "quick_info_bg",
  "quick_info_text",
  "quick_info_frame",
  "disabled",
  "citydlg_prod",
  "citydlg_support",
  "citydlg_trade",
  "citydlg_gold",
  "citydlg_lux",
  "citydlg_food_surplus",
  "citydlg_upkeep",
  "citydlg_science",
  "citydlg_happy",
  "citydlg_celeb",
  "citydlg_impr",
  "citydlg_buy",
  "citydlg_sell",
  "citydlg_panel",
  "citydlg_infopanel",
  "citydlg_foodperturn",
  "citydlg_corruption",
  "citydlg_foodstock",
  "citydlg_shieldstock",
  "citydlg_granary",
  "citydlg_stocks",
  "citydlg_growth",
  "cityrep_foodstock",
  "cityrep_trade",
  "cityrep_prod",
  "cma_text",
  "unitupgrade_text",
  "advancedterraindlg_text",
  "revolutiondlg_text",
  "nationdlg_legend",
  "newcitydlg_text",
  "wardlg_text",
  "meswin_active_text",
  "meswin_active_text2",
  "plrdlg_armistice",
  "plrdlg_war",
  "plrdlg_ceasefire",
  "plrdlg_peace",
  "plrdlg_alliance",
  "joingame_text",
  "joingame_frame",
  "selectionrectangle",
  "red_disabled",
  "checkbox_label_text",
  "text_std",
  "widget_text_normal",
  "widget_text_selected",
  "widget_text_pressed",
  "widget_text_disabled",
  "custom_widget_text_normal",
  "custom_widget_text_selected",
  "custom_widget_text_pressed",
  "custom_widget_text_disabled",
  "custom_widget_frame_selected",
  "custom_widget_frame_pressed",
  "themelabel2_bg",
  "label_bar",
  "window_frame",
  "editfield_caret",  
};

struct theme_color_system *theme_color_system_read(struct section_file *file)
{
  int i;
  struct theme_color_system *colors = fc_malloc(sizeof(*colors));

  assert(ARRAY_SIZE(color_names) == (THEME_COLOR_LAST - COLOR_LAST));
  for (i = 0; i < (THEME_COLOR_LAST - COLOR_LAST); i++) {
    colors->colors[i].r
      = secfile_lookup_int(file, "colors.%s0.r", color_names[i]);
    colors->colors[i].g
      = secfile_lookup_int(file, "colors.%s0.g", color_names[i]);
    colors->colors[i].b
      = secfile_lookup_int(file, "colors.%s0.b", color_names[i]);
    colors->colors[i].a
      = secfile_lookup_int(file, "colors.%s0.a", color_names[i]);
    colors->colors[i].color = NULL;
  }
  
  return colors;
}

/****************************************************************************
  Called when the client first starts to free any allocated colors.
****************************************************************************/
void theme_color_system_free(struct theme_color_system *colors)
{
  int i;

  for (i = 0; i < (THEME_COLOR_LAST - COLOR_LAST); i++) {
    if (colors->colors[i].color) {
      color_free(colors->colors[i].color);
    }
  }
  
  free(colors);
}

/****************************************************************************
  Return the RGBA color, allocating it if necessary.
****************************************************************************/
static struct color *ensure_color(struct rgbacolor *rgba)
{
  if (!rgba->color) {
    rgba->color = color_alloc_rgba(rgba->r, rgba->g, rgba->b, rgba->a);
  }
  return rgba->color;
}

/****************************************************************************
  Return a pointer to the given "theme" color.
****************************************************************************/
struct color *theme_get_color(const struct theme *t, enum theme_color color)
{
  return ensure_color(&theme_get_color_system(t)->colors[color]);
}
