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
#include <gtk/gtk.h>

#include <stdio.h>

#include <log.h>
#include <colors.h>

extern GtkWidget *	toplevel;


/* This is just so we can print the visual class intelligibly */
/*static char *visual_class[] = {
   "StaticGray",
   "GrayScale",
   "StaticColor",
   "PseudoColor",
   "TrueColor",
   "DirectColor"
};
*/
struct rgbtriple {
  int r, g, b;
} colors_standard_rgb[COLOR_STD_LAST] = {
  {  0,   0,   0},
  {255, 255, 255},
  {255,   0,   0}, 
  {100, 100,   0},
  {  0, 255, 200},
  {  0, 200,   0},
  {  0,   0, 200},
  {128,   0,   0},  /* races */
  {128, 255, 255},  /* races */
  {255,   0,   0},  /* races */
  {255,   0, 128},  /* races */
  {  0,   0, 128},  /* races */
  {255,   0, 255},  /* races */
  {255, 128,   0},  /* races */
  {255, 255, 128},  /* races */
  {255, 128, 128},  /* races */
  {  0,   0, 255},  /* races */
  {  0, 255,   0},  /* races */
  {  0, 128, 128},  /* races */
  {128,  64,  64},  /* races */
  {192, 192, 192},  /* races */
};

GdkColormap *cmap;
GdkColor colors_standard	[COLOR_STD_LAST];


/*************************************************************
...
*************************************************************/
void color_error( void )
{
    log_string( LOG_FATAL, "Colormap ran out of entries -  exiting" );
    gtk_main_quit();
}


/*************************************************************
...
*************************************************************/
void alloc_standard_colors( void )
{
    GdkColormap *cmap;

    int i;

    cmap = gtk_widget_get_colormap( toplevel );

    for ( i = 0; i < COLOR_STD_LAST; i++ )
    {
	colors_standard[i].red		= colors_standard_rgb[i].r<<8;
	colors_standard[i].green	= colors_standard_rgb[i].g<<8;
	colors_standard[i].blue		= colors_standard_rgb[i].b<<8;
    
	if ( !gdk_color_alloc( cmap, &colors_standard[i] ) )
	    color_error();
    }
}

/*************************************************************
...
*************************************************************/
enum Display_color_type get_visual( void )
{
    GdkVisual   *visual;

    visual	= gtk_widget_get_visual( toplevel );

    if ( visual->type == GDK_VISUAL_GRAYSCALE )
    { 
	log_string( LOG_DEBUG, "found grayscale(?) display." );
	return GRAYSCALE_DISPLAY;
    }

    log_string( LOG_DEBUG, "color system booted ok." );

    return COLOR_DISPLAY;
}



/*************************************************************
...
*************************************************************/
void init_color_system( void )
{
    cmap = gdk_window_get_colormap( toplevel->window );

    alloc_standard_colors();
}
