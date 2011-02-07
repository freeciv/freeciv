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
#include <fc_config.h>
#endif

#include <stdlib.h>

// client
#include "tilespec.h"

// gui-qt
#include "qtg_cxxside.h"

#include "graphics.h"

static struct sprite *intro_gfx_sprite = NULL;
static struct sprite *radar_gfx_sprite = NULL;

/****************************************************************************
  Return whether the client supports isometric view (isometric tilesets).
****************************************************************************/
bool qtg_isometric_view_supported()
{
  return FALSE;
}

/****************************************************************************
  Return whether the client supports "overhead" (non-isometric) view.
****************************************************************************/
bool qtg_overhead_view_supported()
{
  return TRUE;
}

/****************************************************************************
  Frees the introductory sprites.
****************************************************************************/
void qtg_free_intro_radar_sprites()
{
  if (intro_gfx_sprite) {
    free_sprite(intro_gfx_sprite);
    intro_gfx_sprite = NULL;
  }
  if (radar_gfx_sprite) {
    free_sprite(radar_gfx_sprite);
    radar_gfx_sprite = NULL;
  }
}
