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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <stdio.h>

#include <gtk/gtk.h>

#include "log.h"
#include "mem.h"

#include "gui_main.h"

#include "colors.h"

/*************************************************************
  Get display color type of default visual
*************************************************************/
enum Display_color_type get_visual(void)
{
  GdkVisual *visual;

  gtk_widget_push_colormap (gdk_rgb_get_colormap());

  visual = gtk_widget_get_default_visual();

  if (visual->type == GDK_VISUAL_STATIC_GRAY) { 
    /* StaticGray, use black and white */
    log_verbose("found B/W display.");
    return BW_DISPLAY;
  }

  if(visual->type < GDK_VISUAL_STATIC_COLOR) {
    /* No color visual available at default depth */
    log_verbose("found grayscale(?) display.");
    return GRAYSCALE_DISPLAY;
  }

  log_verbose("color system booted ok.");

  return COLOR_DISPLAY;
}

/****************************************************************************
  Allocate a color (adjusting it for our colormap if necessary on paletted
  systems) and return a pointer to it.
****************************************************************************/
struct color *color_alloc(int r, int g, int b)
{
  struct color *color = fc_malloc(sizeof(*color));
  GdkColormap *cmap = gtk_widget_get_default_colormap();

  color->color.red = (r << 8) + r;
  color->color.green = (g << 8) + g;
  color->color.blue = (b << 8) + b;
  gdk_rgb_find_color(cmap, &color->color);

  return color;
}

/****************************************************************************
  Free a previously allocated color.  See color_alloc.
****************************************************************************/
void color_free(struct color *color)
{
  free(color);
}

/****************************************************************************
  Fill the string with the color in "#rrggbb" mode.  Use it instead of
  gdk_color() which have been included in gtk2.12 version only.
****************************************************************************/
size_t color_to_string(GdkColor *color, char *string, size_t length)
{
  fc_assert_ret_val(NULL != string, 0);
  fc_assert_ret_val(0 < length, 0);

  if (NULL == color) {
    string[0] = '\0';
    return 0;
  } else {
    return fc_snprintf(string, length, "#%02x%02x%02x",
                       color->red >> 8, color->green >> 8, color->blue >> 8);
  }
}
