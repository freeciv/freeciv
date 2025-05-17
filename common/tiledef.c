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

#include "tiledef.h"

static struct tiledef tiledefs[MAX_TILEDEFS];

/************************************************************************//**
  Initialize tiledef structures.
****************************************************************************/
void tiledefs_init(void)
{
  int i;

  for (i = 0; i < MAX_TILEDEFS; i++) {
    tiledefs[i].id = i;
    tiledefs[i].extras = extra_type_list_new();
  }
}

/************************************************************************//**
  Free the memory associated with tiledef
****************************************************************************/
void tiledefs_free(void)
{
  int i;

  for (i = 0; i < MAX_TILEDEFS; i++) {
    extra_type_list_destroy(tiledefs[i].extras);
  }
}
