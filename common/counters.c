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

#include "counters.h"


static struct counter counters[MAX_COUNTERS] =
{
  { "Owned", COUNTER_OWNED, 0, 0 }
};

static struct counter *counters_city[MAX_COUNTERS];

/************************************************************************//**
  Initialize counters system
****************************************************************************/
void counters_init(void)
{
  int i;
  int city_i = 0;

  for (i = 0; i < MAX_COUNTERS; i++) {
    counters[i].id = i;

    if (counters[i].type == COUNTER_OWNED) {
      /* City counter type */
      counters_city[city_i] = &counters[i];
      counters[i].index = city_i++;
      counters[i].target = CTGT_CITY;
    }
  }
}

/************************************************************************//**
  Free resources allocated by counters system
****************************************************************************/
void counters_free(void)
{
}
