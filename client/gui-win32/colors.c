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

#include <windows.h>

#include "colors.h"

LONG rgb_std[COLOR_STD_LAST] = {
  RGB(  0,   0,   0),  /* Black */
  RGB(255, 255, 255),  /* White */
  RGB(255,   0,   0),  /* Red */
  RGB(255, 255,   0),  /* Yellow */
  RGB(  0, 255, 200),  /* Cyan */
  RGB(  0, 200,   0),  /* Ground (green) */
  RGB(  0,   0, 200),  /* Ocean (blue) */
  RGB( 86,  86,  86),  /* Background (gray) */
  RGB(128,   0,   0),  /* race0 */
  RGB(128, 255, 255),  /* race1 */
  RGB(255,   0,   0),  /* race2 */
  RGB(255,   0, 128),  /* race3 */
  RGB(  0,   0, 128),  /* race4 */
  RGB(255,   0, 255),  /* race5 */
  RGB(255, 128,   0),  /* race6 */
  RGB(255, 255, 128),  /* race7 */
  RGB(255, 128, 128),  /* race8 */
  RGB(  0,   0, 255),  /* race9 */
  RGB(  0, 255,   0),  /* race10 */
  RGB(  0, 128, 128),  /* race11 */
  RGB(  0,  64,  64),  /* race12 */
  RGB(198, 198, 198),  /* race13 */
};

HPEN pen_std[COLOR_STD_LAST];
HBRUSH brush_std[COLOR_STD_LAST];  

/**************************************************************************

**************************************************************************/       
static void alloc_standard_colors(void)
{
  int i;
  for(i=0;i<COLOR_STD_LAST;i++)
    {
      pen_std[i]=CreatePen(PS_SOLID, 0, rgb_std[i]);

      brush_std[i]=CreateSolidBrush(rgb_std[i]);
    }
}

/**************************************************************************

**************************************************************************/
void
init_color_system(void)
{
  alloc_standard_colors();
}
