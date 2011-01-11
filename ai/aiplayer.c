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
#include <config.h>
#endif

/* common */
#include "ai.h"
#include "city.h"
#include "unit.h"

#include "aiplayer.h"

static struct ai_type *self = NULL;

/**************************************************************************
  Set pointer to ai type of the default ai.
**************************************************************************/
void default_ai_set_self(struct ai_type *ai)
{
  self = ai;
}

/**************************************************************************
  Get pointer to ai type of the default ai.
**************************************************************************/
struct ai_type *default_ai_get_self(void)
{
  return self;
}
