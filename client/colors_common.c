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
#include <fc_config.h>
#endif

/* utility */
#include "log.h"
#include "shared.h"

/* common */
#include "player.h"
#include "rgbcolor.h"

/* client/include */
#include "colors_g.h"

/* client */
#include "tilespec.h"

#include "colors_common.h"

struct color_system {
  struct rgbcolor **stdcolors;
};

/****************************************************************************
  Called when the client first starts to allocate the default colors.

  Currently this must be called in ui_main, generally after UI
  initialization.
****************************************************************************/
struct color_system *color_system_read(struct section_file *file)
{
  struct color_system *colors = fc_malloc(sizeof(*colors));
  enum color_std stdcolor;

  colors->stdcolors = fc_calloc(COLOR_LAST, sizeof(*colors->stdcolors));

  for (stdcolor= color_std_begin(); stdcolor!= color_std_end();
       stdcolor= color_std_next(stdcolor)) {
    struct rgbcolor *prgbcolor = NULL;
    if (rgbcolor_load(file, &prgbcolor, "colors.%s0",
                      color_std_name(stdcolor))) {
      *(colors->stdcolors + stdcolor) = prgbcolor;
    } else {
      log_error("Color %s: %s", color_std_name(stdcolor), secfile_error());
      *(colors->stdcolors + stdcolor) = rgbcolor_new(0, 0, 0);
    }
  }

  return colors;
}

/****************************************************************************
  Called when the client first starts to free any allocated colors.
****************************************************************************/
void color_system_free(struct color_system *colors)
{
  enum color_std stdcolor;

  for (stdcolor= color_std_begin(); stdcolor!= color_std_end();
       stdcolor= color_std_next(stdcolor)) {
    rgbcolor_destroy(*(colors->stdcolors + stdcolor));
  }

  free(colors->stdcolors);

  free(colors);
}

/****************************************************************************
  Return the RGB color, allocating it if necessary.
****************************************************************************/
struct color *ensure_color(struct rgbcolor *rgb)
{
  fc_assert_ret_val(rgb != NULL, NULL);

  if (!rgb->color) {
    rgb->color = color_alloc(rgb->r, rgb->g, rgb->b);
  }
  return rgb->color;
}

/****************************************************************************
  Return a pointer to the given "standard" color.
****************************************************************************/
struct color *get_color(const struct tileset *t, enum color_std stdcolor)
{
  struct color_system *colors = get_color_system(t);

  fc_assert_ret_val(colors != NULL, NULL);

  return ensure_color(*(colors->stdcolors + stdcolor));
}

/**********************************************************************
  Return the color of the player.
***********************************************************************/
struct color *get_player_color(const struct tileset *t,
                               const struct player *pplayer)
{
  fc_assert_ret_val(pplayer != NULL, NULL);
  fc_assert_ret_val(pplayer->rgb != NULL, NULL);

  return ensure_color(pplayer->rgb);
}

/****************************************************************************
  Return a pointer to the given "terrain" color.
****************************************************************************/
struct color *get_terrain_color(const struct tileset *t,
                                const struct terrain *pterrain)
{
  fc_assert_ret_val(pterrain != NULL, NULL);
  fc_assert_ret_val(pterrain->rgb != NULL, NULL);

  return ensure_color(pterrain->rgb);
}
