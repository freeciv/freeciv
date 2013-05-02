/****************************************************************************
 Freeciv - Copyright (C) 2004 - The Freeciv Team
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
#include <fc_config.h>
#endif

/* common */
#include "base.h"
#include "road.h"

#include "extras.h"

/****************************************************************************
  Initialize extras structures.
****************************************************************************/
void extras_init(void)
{
  base_types_init();
  road_types_init();
}

/****************************************************************************
  Free the memory associated with extras
****************************************************************************/
void extras_free(void)
{
  base_types_free();
  road_types_free();
}
