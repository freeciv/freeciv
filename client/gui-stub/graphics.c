/* graphics.c -- PLACEHOLDER */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>

#include "graphics.h"

bool
isometric_view_supported(void)
{
	/* PORTME */
	return FALSE;
}

bool
overhead_view_supported(void)
{
	/* PORTME */
	return TRUE;
}

void
load_intro_gfx(void)
{
	/* PORTME */
}

void
load_cursors(void)
{
	/* PORTME */
}

void
free_intro_radar_sprites(void)
{
	/* PORTME */
}

/**************************************************************************
  Return a NULL-terminated, permanently allocated array of possible
  graphics types extensions.  Extensions listed first will be checked
  first.
**************************************************************************/
const char **
gfx_fileextensions(void)
{
	/* PORTME */

  /* hack to allow stub to run */
  static const char *ext[] =
  {
    "png",
    NULL
  };

  return ext;
}

struct Sprite *
load_gfxfile(const char *filename)
{
	/* PORTME */
	return NULL;
}

struct Sprite *
crop_sprite(struct Sprite *source,
                           int x, int y, int width, int height)
{
	/* PORTME */
	return NULL;
}

void
free_sprite(struct Sprite *s)
{
	/* PORTME */
}
