/********************************************************************** 
 Freeciv - Copyright (C) 2005 - The Freeciv Project
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

#include "specialist.h"

struct specialist specialists[SP_MAX];
int num_specialist_types;
int default_specialist;

/****************************************************************************
  Initialize data for specialists.
****************************************************************************/
void specialists_init(void)
{
  int i;

  for (i = 0; i < ARRAY_SIZE(specialists); i++) {
    specialists[i].index = i;
  }
}

/****************************************************************************
  Return the specialist struct for the given specialist ID.
****************************************************************************/
struct specialist *get_specialist(Specialist_type_id spec)
{
  return &specialists[spec];
}

/****************************************************************************
  Return a string showing the number of specialists in the array.

  For instance with a city with (0,3,1) specialists call

    specialists_string(pcity->specialists);

  and you'll get "0/3/1".
****************************************************************************/
const char *specialists_string(const int *specialists)
{
  static char buf[5 * SP_MAX];

  buf[0] = '\0';

  specialist_type_iterate(sp) {
    char *separator = (buf[0] == '\0') ? "" : "/";

    cat_snprintf(buf, sizeof(buf), "%s%d", separator, specialists[sp]);
  } specialist_type_iterate_end;

  return buf;
}
