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

#include "log.h"
#include "shared.h"

#include "player.h"

#include "colors_g.h"

#include "colors_common.h"
#include "tilespec.h"

/* An RGBcolor contains the R,G,B bitvalues for a color.  The color itself
 * holds the color structure for this color but may be NULL (it's allocated
 * on demand at runtime). */
struct rgbcolor {
  int r, g, b;
  struct color *color;
};

struct color_system {
  struct rgbcolor colors[COLOR_LAST];

  int num_player_colors;
  struct rgbcolor *player_colors;
};

char *color_names[] = {
  /* Mapview */
  "mapview_unknown",
  "mapview_citytext",
  "mapview_cityblocked",
  "mapview_goto",
  "mapview_selection",

  /* Spaceship */
  "spaceship_background",

  /* Overview */
  "overview_unknown",
  "overview_mycity",
  "overview_alliedcity",
  "overview_enemycity",
  "overview_myunit",
  "overview_alliedunit",
  "overview_enemyunit",
  "overview_ocean",
  "overview_foggedocean",
  "overview_ground",
  "overview_foggedground",
  "overview_viewrect",

  /* Reqtree */
  "reqtree_researching",
  "reqtree_known",
  "reqtree_reachablegoal",
  "reqtree_unreachablegoal",
  "reqtree_reachable",
  "reqtree_unreachable",
  "reqtree_background",
  "reqtree_text",

  /* Player dialog */
  "playerdlg_background"
};

/****************************************************************************
  Called when the client first starts to allocate the default colors.

  Currently this must be called in ui_main, generally after UI
  initialization.
****************************************************************************/
struct color_system *color_system_read(struct section_file *file)
{
  int i;
  struct color_system *colors = fc_malloc(sizeof(*colors));

  assert(ARRAY_SIZE(color_names) == COLOR_LAST);
  for (i = 0; i < COLOR_LAST; i++) {
    colors->colors[i].r
      = secfile_lookup_int(file, "colors.%s0.r", color_names[i]);
    colors->colors[i].g
      = secfile_lookup_int(file, "colors.%s0.g", color_names[i]);
    colors->colors[i].b
      = secfile_lookup_int(file, "colors.%s0.b", color_names[i]);
    colors->colors[i].color = NULL;
  }

  for (i = 0; i < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS; i++) {
    if (!section_file_lookup(file, "colors.player%d.r", i)) {
      break;
    }
  }
  colors->num_player_colors = MAX(i, 1);
  colors->player_colors = fc_malloc(colors->num_player_colors
				    * sizeof(*colors->player_colors));
  if (i == 0) {
    /* Use a simple fallback. */
    freelog(LOG_ERROR,
	    "Missing colors.player.  See misc/colors.tilespec.");
    colors->player_colors[0].r = 128;
    colors->player_colors[0].g = 0;
    colors->player_colors[0].b = 0;
    colors->player_colors[0].color = NULL;
  } else {
    for (i = 0; i < colors->num_player_colors; i++) {
      struct rgbcolor *rgb = &colors->player_colors[i];

      rgb->r = secfile_lookup_int(file, "colors.player%d.r", i);
      rgb->g = secfile_lookup_int(file, "colors.player%d.g", i);
      rgb->b = secfile_lookup_int(file, "colors.player%d.b", i);
      rgb->color = NULL;
    }
  }

  return colors;
}

/****************************************************************************
  Called when the client first starts to free any allocated colors.
****************************************************************************/
void color_system_free(struct color_system *colors)
{
  int i;

  for (i = 0; i < COLOR_LAST; i++) {
    if (colors->colors[i].color) {
      color_free(colors->colors[i].color);
    }
  }
  for (i = 0; i < colors->num_player_colors; i++) {
    if (colors->player_colors[i].color) {
      color_free(colors->player_colors[i].color);
    }
  }
  free(colors->player_colors);
  free(colors);
}

/****************************************************************************
  Return the RGB color, allocating it if necessary.
****************************************************************************/
static struct color *ensure_color(struct rgbcolor *rgb)
{
  if (!rgb->color) {
    rgb->color = color_alloc(rgb->r, rgb->g, rgb->b);
  }
  return rgb->color;
}

/****************************************************************************
  Return a pointer to the given "standard" color.
****************************************************************************/
struct color *get_color(const struct tileset *t, enum color_std color)
{
  return ensure_color(&get_color_system(t)->colors[color]);
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
struct color *get_player_color(const struct tileset *t,
			       const struct player *pplayer)
{
  if (pplayer) {
    struct color_system *colors = get_color_system(t);
    int index = pplayer->player_no;

    assert(index >= 0 && colors->num_player_colors > 0);
    index %= colors->num_player_colors;
    return ensure_color(&colors->player_colors[index]);
  } else {
    assert(0);
    return NULL;
  }
}
