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
#include <config.h>
#endif

#include <stdio.h>

#include <gtk/gtk.h>

#include "log.h"
#include "mem.h"

#include "gui_main.h"

#include "colors.h"

static struct rgbtriple {
  int r, g, b;
} colors_standard_rgb[COLOR_STD_LAST] = {
  {  0,   0,   0},  /* Black */
  {255, 255, 255},  /* White */
  {255,   0,   0},  /* Red */
  {255, 255,   0},  /* Yellow */
  {  0, 255, 200},  /* Cyan */
  {  0, 200,   0},  /* Ground (green) */
  {  0,   0, 200},  /* Ocean (blue) */
  { 86,  86,  86},  /* Background (gray) */
  {128,   0,   0},  /* race0 */
  {128, 255, 255},  /* race1 */
  {255,   0,   0},  /* race2 */
  {255,   0, 128},  /* race3 */
  {  0,   0, 128},  /* race4 */
  {255,   0, 255},  /* race5 */
  {255, 128,   0},  /* race6 */
  {255, 255, 128},  /* race7 */
  {255, 128, 128},  /* race8 */
  {  0,   0, 255},  /* race9 */
  {  0, 255,   0},  /* race10 */
  {  0, 128, 128},  /* race11 */
  {  0,  64,  64},  /* race12 */
  {198, 198, 198},  /* race13 */
};

GdkColor *colors_standard [COLOR_STD_LAST];

/*************************************************************
...
*************************************************************/
static void alloc_standard_colors (void)
{
  GdkColormap *cmap;
  int i;

  for (i=0, cmap=gtk_widget_get_default_colormap (); i<COLOR_STD_LAST; i++) {
    colors_standard[i]       = fc_malloc(sizeof(GdkColor));

    colors_standard[i]->red  = colors_standard_rgb[i].r<<8;
    colors_standard[i]->green= colors_standard_rgb[i].g<<8;
    colors_standard[i]->blue = colors_standard_rgb[i].b<<8;
  
    gdk_rgb_find_color(cmap, colors_standard[i]);
  }
}

/*************************************************************
...
*************************************************************/
enum Display_color_type get_visual(void)
{
  GdkVisual *visual;

  gtk_widget_push_colormap (gdk_rgb_get_colormap());

  visual = gtk_widget_get_default_visual();

  if (visual->type == GDK_VISUAL_STATIC_GRAY) { 
    /* StaticGray, use black and white */
    freelog(LOG_VERBOSE, "found B/W display.");
    return BW_DISPLAY;
  }

  if(visual->type < GDK_VISUAL_STATIC_COLOR) {
    /* No color visual available at default depth */
    freelog(LOG_VERBOSE, "found grayscale(?) display.");
    return GRAYSCALE_DISPLAY;
  }

  freelog(LOG_VERBOSE, "color system booted ok.");

  return COLOR_DISPLAY;
}

/*************************************************************
...
*************************************************************/
void init_color_system(void)
{
  alloc_standard_colors();
}

/*************************************************************
...
*************************************************************/
void free_color_system(void)
{
  int i;

  for (i = 0; i < COLOR_STD_LAST; i++) {
    free(colors_standard[i]);
  }
}
