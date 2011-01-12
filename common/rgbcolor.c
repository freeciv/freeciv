/****************************************************************************
 Freeciv - Copyright (C) 2010 - The Freeciv Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
****************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* utility */
#include "fcintl.h"
#include "log.h"
#include "registry.c"

/* common */
#include "fc_interface.h"
#include "game.h"

#include "rgbcolor.h"

/****************************************************************************
  ...
****************************************************************************/
struct rgbcolor *rgbcolor_new(int r, int g, int b)
{
  struct rgbcolor *prgbcolor;

  prgbcolor = fc_calloc(1, sizeof(*prgbcolor));
  prgbcolor->r = r;
  prgbcolor->g = g;
  prgbcolor->b = b;
  prgbcolor->color = NULL;

  return prgbcolor;
}

/****************************************************************************
  ...
****************************************************************************/
struct rgbcolor *rgbcolor_copy(const struct rgbcolor *prgbcolor)
{
  fc_assert_ret_val(prgbcolor != NULL, NULL);

  return rgbcolor_new(prgbcolor->r, prgbcolor->g, prgbcolor->b);
}

/****************************************************************************
  ...
****************************************************************************/
void rgbcolor_destroy(struct rgbcolor *prgbcolor)
{
  if (!prgbcolor) {
    return;
  }

  fc_funcs->gui_color_free(prgbcolor->color);
  free(prgbcolor);
}

/****************************************************************************
  Lookup an RGB color definition (<colorpath>.red, <colorpath>.green and
  <colorpath>.blue). Returns TRUE on success and FALSE on error.
****************************************************************************/
bool rgbcolor_load(struct section_file *file, struct rgbcolor **prgbcolor,
                   char *path, ...)
{
  int r, g, b;
  char colorpath[256];
  va_list args;

  fc_assert_ret_val(file != NULL, FALSE);
  fc_assert_ret_val(*prgbcolor == NULL, FALSE);

  va_start(args, path);
  fc_vsnprintf(colorpath, sizeof(colorpath), path, args);
  va_end(args);

  if (!secfile_lookup_int(file, &r, "%s.r", colorpath)
      || !secfile_lookup_int(file, &g, "%s.g", colorpath)
      || !secfile_lookup_int(file, &b, "%s.b", colorpath)) {
    /* One color value (red, green or blue) is missing. */
    return FALSE;
  }

  rgbcolor_check(colorpath, r, g, b);
  *prgbcolor = rgbcolor_new(r, g, b);

  return TRUE;
}

/****************************************************************************
  Save an RGB color definition (<colorpath>.red, <colorpath>.green and
  <colorpath>.blue).
****************************************************************************/
void rgbcolor_save(struct section_file *file,
                   const struct rgbcolor *prgbcolor, char *path, ...)
{
  char colorpath[256];
  va_list args;

  fc_assert_ret(file != NULL);
  fc_assert_ret(prgbcolor != NULL);

  va_start(args, path);
  fc_vsnprintf(colorpath, sizeof(colorpath), path, args);
  va_end(args);

  secfile_insert_int(file, prgbcolor->r, "%s.r", colorpath);
  secfile_insert_int(file, prgbcolor->g, "%s.g", colorpath);
  secfile_insert_int(file, prgbcolor->b, "%s.b", colorpath);
}
