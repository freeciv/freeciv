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

#include <intuition/intuition.h>
#include <libraries/mui.h>
#include <proto/muimaster.h>
#include <proto/graphics.h>

#include "colors.h"
#include "gui_main.h"
#include "muistuff.h"

#define COLOR(r,g,b) (((r)<<16) | ((g)<<8) | b)

long colors_rgb[COLOR_MUI_LAST] = {
  COLOR(  0,   0,   0),  /* Black */
  COLOR(255, 255, 255),  /* White */
  COLOR(255,   0,   0),  /* Red */
  COLOR(255, 255,   0),  /* Yellow */
  COLOR(  0, 255, 200),  /* Cyan */
  COLOR(  0, 200,   0),  /* Ground (green) */
  COLOR(  0,   0, 200),  /* Ocean (blue) */
  COLOR( 86,  86,  86),  /* Background (gray) */
  COLOR(128,   0,   0),  /* race0 */
  COLOR(128, 255, 255),  /* race1 */
  COLOR(255,   0,   0),  /* race2 */
  COLOR(255,   0, 128),  /* race3 */
  COLOR(  0,   0, 128),  /* race4 */
  COLOR(255,   0, 255),  /* race5 */
  COLOR(255, 128,   0),  /* race6 */
  COLOR(255, 255, 128),  /* race7 */
  COLOR(255, 128, 128),  /* race8 */
  COLOR(  0,   0, 255),  /* race9 */
  COLOR(  0, 255,   0),  /* race10 */
  COLOR(  0, 128, 128),  /* race11 */
  COLOR(  0,  64,  64),  /* race12 */
  COLOR(198, 198, 198),  /* race13 */

  /* mui colors */
  COLOR(116, 168, 116),  /* known tech (green) */
  COLOR(212, 208, 116),  /* reachable tech (yellow) */
  COLOR(200, 152, 152),  /* unknown tech (red) */
};

long colors_pen[COLOR_MUI_LAST]; /* The map class will fill this */
