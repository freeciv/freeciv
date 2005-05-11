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

#include "shared.h"

#include "player.h"

#include "colors_g.h"

#include "colors_common.h"

struct rgbtriple {
  int r, g, b;
};

struct rgbtriple colors_standard_rgb[COLOR_STD_LAST] = {
  {  0,   0,   0},  /* Black */
  {255, 255, 255},  /* White */
  {255,   0,   0},  /* Red */
  {255, 255,   0},  /* Yellow */
  {  0, 255, 200},  /* Cyan */
  {  0, 200,   0},  /* Ground (green) */
  {  0,   0, 200},  /* Ocean (blue) */
  { 86,  86,  86},  /* Background (gray) */

  /* TODO: Rename these appropriately. */
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

struct rgbtriple colors_player_rgb[] = {
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

static struct color *colors[COLOR_STD_LAST];
static struct color *player_colors[ARRAY_SIZE(colors_player_rgb)];

static bool initted = FALSE;

/****************************************************************************
  Called when the client first starts to allocate the default colors.

  Currently this must be called in ui_main, generally after UI
  initialization.
****************************************************************************/
void init_color_system(void)
{
  int i;

  assert(!initted);
  initted = TRUE;

  for (i = 0; i < COLOR_STD_LAST; i++) {
    colors[i] = color_alloc(colors_standard_rgb[i].r,
			    colors_standard_rgb[i].g,
			    colors_standard_rgb[i].b);
  }

  for (i = 0; i < ARRAY_SIZE(player_colors); i++) {
    player_colors[i] = color_alloc(colors_player_rgb[i].r,
				   colors_player_rgb[i].g,
				   colors_player_rgb[i].b);
  }
}

/****************************************************************************
  Called when the client first starts to free any allocated colors.
****************************************************************************/
void color_free_system(void)
{
  int i;

  assert(initted);
  initted = FALSE;

  for (i = 0; i < COLOR_STD_LAST; i++) {
    color_free(colors[i]);
  }
  for (i = 0; i < ARRAY_SIZE(player_colors); i++) {
    color_free(player_colors[i]);
  }
}

/****************************************************************************
  Return a pointer to the given "standard" color.
****************************************************************************/
struct color *get_color(enum color_std color)
{
  return colors[color];
}

/**********************************************************************
  Not sure which module to put this in...
  It used to be that each nation had a color, when there was
  fixed number of nations.  Now base on player number instead,
  since still limited to less than 14.  Could possibly improve
  to allow players to choose their preferred color etc.
  A hack added to avoid returning more that COLOR_STD_RACE13.
  But really there should be more colors available -- jk.
***********************************************************************/
struct color *get_player_color(const struct player *pplayer)
{
  if (pplayer) {
    int index = pplayer->player_no;

    assert(index >= 0);
    index %= ARRAY_SIZE(player_colors);
    return player_colors[index];
  } else {
    assert(0);
    return NULL;
  }
}
