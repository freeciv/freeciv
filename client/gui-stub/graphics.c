/* graphics.c -- PLACEHOLDER */

#include <stdlib.h>

#include "graphics.h"


int
isometric_view_supported(void)
{
	/* PORTME */
	return 0;
}

int
overhead_view_supported(void)
{
	/* PORTME */
	return 0;
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

char **
gfx_fileextensions(void)
{
	/* PORTME */

  /* hack to allow stub to run */
  static char *ext[] =
  {
    "xpm",
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
