/***********************************************************************
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

/* common */
#include "map.h"
#include "world_object.h"

#include "texaiworld.h"

static struct world texai_world;

/**************************************************************************
  Initialize world object for texai
**************************************************************************/
void texai_world_init(void)
{
  map_init(&(texai_world.map), TRUE);
  map_allocate(&(texai_world.map));
}

/**************************************************************************
  Free resources allocated for texai world object
**************************************************************************/
void texai_world_close(void)
{
  map_free(&(texai_world.map));
}
