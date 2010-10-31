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

/* utility */
#include "log.h"
#include "shared.h"

/* common */
#include "player.h"

/* client/include */
#include "colors_g.h"

/* client */
#include "tilespec.h"

#include "colors_common.h"


/* An RGBcolor contains the R,G,B bitvalues for a color.  The color itself
 * holds the color structure for this color but may be NULL (it's allocated
 * on demand at runtime). */
struct rgbcolor {
  int r, g, b;
  struct color *color;
};

static struct rgbcolor *rgbcolor_copy(const struct rgbcolor *prgb);
static void rgbcolor_destroy(struct rgbcolor *prgb);

#define SPECHASH_TAG terrain_color
#define SPECHASH_KEY_TYPE char *
#define SPECHASH_DATA_TYPE struct rgbcolor *
#define SPECHASH_KEY_VAL genhash_str_val_func
#define SPECHASH_KEY_COMP genhash_str_comp_func
#define SPECHASH_KEY_COPY genhash_str_copy_func
#define SPECHASH_KEY_FREE genhash_str_free_func
#define SPECHASH_DATA_COPY rgbcolor_copy
#define SPECHASH_DATA_FREE rgbcolor_destroy
#include "spechash.h"

struct color_system {
  struct rgbcolor colors[COLOR_LAST];

  int num_player_colors;
  struct rgbcolor *player_colors;

  /* Terrain colors: we have one color per terrain. These are stored in a
   * larger-than-necessary array. There's also a hash that is used to store
   * all colors; this is created when the tileset toplevel is read and later
   * used when the rulesets are received. */
  struct terrain_color_hash *terrain_hash;
  struct rgbcolor terrain_colors[MAX_NUM_TERRAINS];
};

char *color_names[] = {
  /* Mapview */
  "mapview_unknown",
  "mapview_citytext",
  "mapview_cityblocked",
  "mapview_goto",
  "mapview_selection",
  "mapview_trade_route_line",
  "mapview_trade_routes_all_built",
  "mapview_trade_routes_some_built",
  "mapview_trade_routes_no_built",
  "mapview_city_link",
  "mapview_tile_link",
  "mapview_unit_link",

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
  "overview_ground",
  "overview_viewrect",

  /* Reqtree */
  "reqtree_researching",
  "reqtree_known",
  "reqtree_goal_prereqs_known",
  "reqtree_goal_unknown",
  "reqtree_prereqs_known",
  "reqtree_unknown",
  "reqtree_unreachable",
  "reqtree_background",
  "reqtree_text",
  "reqtree_edge",

  /* Player dialog */
  "playerdlg_background"
};

/****************************************************************************
  Duplicate a rgb color.
****************************************************************************/
static struct rgbcolor *rgbcolor_copy(const struct rgbcolor *prgb)
{
  struct rgbcolor *pnew = fc_malloc(sizeof(*pnew));

  *pnew = *prgb;
  return pnew;
}

/****************************************************************************
  Free a rgb color.
****************************************************************************/
static void rgbcolor_destroy(struct rgbcolor *prgb)
{
  fc_assert_ret(NULL != prgb);
  free(prgb);
}

/****************************************************************************
  Called when the client first starts to allocate the default colors.

  Currently this must be called in ui_main, generally after UI
  initialization.
****************************************************************************/
struct color_system *color_system_read(struct section_file *file)
{
  int i;
  struct color_system *colors = fc_malloc(sizeof(*colors));

  fc_assert_ret_val(ARRAY_SIZE(color_names) == COLOR_LAST, NULL);
  for (i = 0; i < COLOR_LAST; i++) {
    if (!secfile_lookup_int(file, &colors->colors[i].r,
                            "colors.%s0.r", color_names[i])
        || !secfile_lookup_int(file, &colors->colors[i].g,
                               "colors.%s0.g", color_names[i])
        || !secfile_lookup_int(file, &colors->colors[i].b,
                               "colors.%s0.b", color_names[i])) {
      log_error("Color %s: %s", color_names[i], secfile_error());
      colors->colors[i].r = 0;
      colors->colors[i].g = 0;
      colors->colors[i].b = 0;
    }
    colors->colors[i].color = NULL;
  }

  for (i = 0; i < player_slot_count(); i++) {
    if (NULL == secfile_entry_lookup(file, "colors.player%d.r", i)) {
      break;
    }
  }
  colors->num_player_colors = MAX(i, 1);
  colors->player_colors = fc_malloc(colors->num_player_colors
				    * sizeof(*colors->player_colors));
  if (i == 0) {
    /* Use a simple fallback. */
    log_error("Missing colors.player. See misc/colors.tilespec.");
    colors->player_colors[0].r = 128;
    colors->player_colors[0].g = 0;
    colors->player_colors[0].b = 0;
    colors->player_colors[0].color = NULL;
  } else {
    for (i = 0; i < colors->num_player_colors; i++) {
      struct rgbcolor *rgb = &colors->player_colors[i];

      if (!secfile_lookup_int(file, &rgb->r, "colors.player%d.r", i)
          || !secfile_lookup_int(file, &rgb->g, "colors.player%d.g", i)
          || !secfile_lookup_int(file, &rgb->b, "colors.player%d.b", i)) {
        log_error("Player color %d: %s", i, secfile_error());
        rgb->r = 0;
        rgb->g = 0;
        rgb->b = 0;
      }
      rgb->color = NULL;
    }
  }

  for (i = 0; i < ARRAY_SIZE(colors->terrain_colors); i++) {
    struct rgbcolor *rgb = &colors->terrain_colors[i];

    rgb->r = rgb->g = rgb->b = 0;
    rgb->color = NULL;
  }
  colors->terrain_hash = terrain_color_hash_new();
  for (i = 0; ; i++) {
    struct rgbcolor rgb;
    const char *key;

    if (!secfile_lookup_int(file, &rgb.r, "colors.tiles%d.r", i)
        || !secfile_lookup_int(file, &rgb.g, "colors.tiles%d.g", i)
        || !secfile_lookup_int(file, &rgb.b, "colors.tiles%d.b", i)) {
      break;
    }

    rgb.color = NULL;
    key = secfile_lookup_str(file, "colors.tiles%d.tag", i);

    if (NULL == key) {
      log_error("warning: tag for tiles %d: %s", i, secfile_error());
    } else if (!terrain_color_hash_insert(colors->terrain_hash, key, &rgb)) {
      log_error("warning: already have a color for %s", key);
    }
  }

  return colors;
}

/****************************************************************************
  Called when terrain info is received from the server.
****************************************************************************/
void color_system_setup_terrain(struct color_system *colors,
                                const struct terrain *pterrain,
                                const char *tag)
{
  struct rgbcolor *rgb;

  if (terrain_color_hash_lookup(colors->terrain_hash, tag, &rgb)) {
    colors->terrain_colors[terrain_index(pterrain)] = *rgb;
  } else {
    log_error("[colors] missing [tile_%s] for \"%s\".",
              tag, terrain_rule_name(pterrain));
    /* Fallback: the color remains black. */
  }
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
  for (i = 0; i < ARRAY_SIZE(colors->terrain_colors); i++) {
    if (colors->terrain_colors[i].color) {
      color_free(colors->terrain_colors[i].color);
    }
  }
  terrain_color_hash_destroy(colors->terrain_hash);
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
    int index = player_index(pplayer);

    fc_assert_ret_val(index >= 0 && colors->num_player_colors > 0, NULL);
    index %= colors->num_player_colors;
    return ensure_color(&colors->player_colors[index]);
  } else {
    /* Always fails. */
    fc_assert(NULL != pplayer);
    return NULL;
  }
}

/****************************************************************************
  Return a pointer to the given "terrain" color.

  Each terrain has a color associated.  This is usually used to draw the
  overview.
****************************************************************************/
struct color *get_terrain_color(const struct tileset *t,
				const struct terrain *pterrain)
{
  if (pterrain) {
    struct color_system *colors = get_color_system(t);

    return ensure_color(&colors->terrain_colors[terrain_index(pterrain)]);
  } else {
    /* Always fails. */
    fc_assert(NULL != pterrain);
    return NULL;
  }
}
