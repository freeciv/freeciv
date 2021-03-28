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

#include <stdlib.h>


/* utility */
#include "fcintl.h"

#include "counters.h"

static struct counter counters[MAX_COUNTERS] =
{
  { "Owned", COUNTER_OWNED, CTGT_CITY, 0 }
};

static struct counter *counters_city[MAX_COUNTERS];
static int number_city_counters;

/************************************************************************//**
  Initialize counters system
****************************************************************************/
void counters_init(void)
{
  int i;

  number_city_counters = 0;

  for (i = 0; i < MAX_COUNTERS; i++) {

    if (counters[i].type == COUNTER_OWNED) {
      /* City counter type */
      counters_city[number_city_counters] = &counters[i];
      counters[i].index = number_city_counters;
      counters[i].target = CTGT_CITY;
      number_city_counters++;
    }
  }
}

/************************************************************************//**
  Free resources allocated by counters system
****************************************************************************/
void counters_free(void)
{
  number_city_counters = 0;
}

/************************************************************************//**
  Return number of city counters.
****************************************************************************/
int counters_get_city_counters_count(void)
{
  return number_city_counters;
}

/************************************************************************//**
  Return counter by given id
****************************************************************************/
struct counter *counter_by_id(int id)
{
  fc_assert_ret_val(id < MAX_COUNTERS, NULL);

  return &counters[id];
}

/************************************************************************//**
    Return id of a given counter
****************************************************************************/
int counter_id(struct counter *pcount)
{
  fc_assert_ret_val(NULL != pcount, -1);
  return pcount - counters;
}

/************************************************************************//**
    Search for counter by rule name
    (return matched counter if found or NULL)
****************************************************************************/
struct counter *counter_by_rule_name(const char *name)
{
  int i;
  fc_assert_ret_val(NULL != name, NULL);
  fc_assert_ret_val('\0' != name[0], NULL);

  for (i = 0; i < MAX_COUNTERS; i++)
  {
    if (0 == fc_strcasecmp(name, counters[i].rule_name))
    {
      return &counters[i];
    }
  }

  return NULL;
}

/************************************************************************//**
     Return rule name of a given counter
****************************************************************************/
const char *counter_rule_name(struct counter *pcount)
{
  fc_assert_ret_val(NULL != pcount, NULL);
  return pcount->rule_name;
}

/************************************************************************//**
   Return index in global counter's array
***************************************************************************/
int counter_index(struct counter *pcount)
{
  fc_assert_ret_val(NULL != pcount, -1);
  return pcount->index;
}

/************************************************************************//**
   Return counter by given index
****************************************************************************/
struct counter *counter_by_index(int index, enum counter_target target)
{
  switch (target)
  {
    case CTGT_CITY:
      return counters_city[index];
  }

  return NULL;
}
