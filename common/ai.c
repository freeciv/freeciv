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

static struct ai_type ai_types[FC_AI_LAST];

static int ai_type_count = 0;

/***************************************************************
  Returns ai_type of given id.
***************************************************************/
struct ai_type *get_ai_type(int id)
{
  fc_assert(id >= 0 && id < FC_AI_LAST);

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
  int ainbr = ai - ai_types;

  fc_assert_ret_val(ainbr >= 0 && ainbr < FC_AI_LAST, 0);

  return ainbr;
}

/***************************************************************
  Find ai type with given name.
***************************************************************/
struct ai_type *ai_type_by_name(const char *search)
{
  size_t len = strlen(search);

  ai_type_iterate(ai) {
    if (!strncmp(ai->name, search, len)) {
      return ai;
    }
  } ai_type_iterate_end;

  return NULL;
}

/***************************************************************
  Return next free ai_type
***************************************************************/
struct ai_type *ai_type_alloc(void)
{
  if (ai_type_count >= FC_AI_LAST) {
    return NULL;
  }

  return get_ai_type(ai_type_count++);
}
