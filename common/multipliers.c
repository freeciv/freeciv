/********************************************************************** 
 Freeciv - Copyright (C) 2014 Lach Sławomir
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
#include "multipliers.h"

static struct multiplier multipliers[MAX_MULTIPLIERS_COUNT];
static int multiplier_count;
static int multiplier_used;

/***************************************************************
 Returns multiplier associated to given number
***************************************************************/
struct multiplier *multiplier_by_number(int id)
{
  fc_assert_ret_val(id >= 0 && id < multiplier_count, NULL);

  return &multipliers[id];
}

/***************************************************************
 Returns array index(multipliers array) of given multiplier
***************************************************************/
int multiplier_index(const struct multiplier *pmul)
{
  fc_assert_ret_val(NULL != pmul, -1);

  return pmul - multipliers;
}

/***************************************************************
 Initializes first free multiplier in multipliers array
 and returns pointer to it.
***************************************************************/
struct multiplier *multiplier_new(void)
{
  struct multiplier *mul = &multipliers[multiplier_used];

  multiplier_used++;

  mul->start = 0;
  mul->stop  = 0;
  mul->step  = 0;
  mul->def   = 0;

  return mul;
}

/***************************************************************
 Initialize all multipliers
***************************************************************/
void multipliers_init(void)
{
  multiplier_count = 0;
  multiplier_used = 0;
}

/***************************************************************
 Free all multipliers
***************************************************************/
void multipliers_free(void)
{
}

/***************************************************************
 Set number of loaded multipliers.
***************************************************************/
void set_multiplier_count(int count)
{
  multiplier_count = count;
}

/***************************************************************
 Returns number of loaded multipliers.
***************************************************************/
int get_multiplier_count(void)
{
  return multiplier_count;
}
