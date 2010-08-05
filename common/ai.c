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
#include <config.h>
#endif

#include <string.h>

/* utility */
#include "log.h"                /* fc_assert */

/* common */
#include "ai.h"

static struct ai_type ai_types[1];

/***************************************************************
  Returns ai_type of given id. Currently only one ai_type,
  id AI_DEFAULT, is supported.
***************************************************************/
struct ai_type *get_ai_type(int id)
{
  fc_assert(id == AI_DEFAULT);

  return &ai_types[id];
}

/***************************************************************
  Initializes AI structure.
***************************************************************/
void init_ai(struct ai_type *ai)
{
  memset(ai, 0, sizeof(*ai));
}

/***************************************************************
  Returns id of the given ai_type.
***************************************************************/
int ai_type_number(const struct ai_type *ai)
{
  return ai - ai_types;
}
