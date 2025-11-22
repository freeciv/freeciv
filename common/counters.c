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

/* common */
#include "game.h"

/* utility */
#include "fcintl.h"

#include "counters.h"

/* All (of each type) counters array + related data.
 * The number of length of data in this array
 * game kept in game control packet/struct */
static struct counter counters[MAX_COUNTERS];

/* City counters array + related data */
static struct counter *counters_city[MAX_COUNTERS];
static int number_city_counters;

/************************************************************************//**
  Initialize counters system
****************************************************************************/
void counters_init(void)
{
  fc_assert(game.control.num_counters == 0);

  number_city_counters = 0;
}


/************************************************************************//**
  Free resources allocated by counters system
****************************************************************************/
void counters_free(void)
{
  /* TODO: Is freeing translated name needed? If is, write the right
   * code here
   */

  int i;

  for (i = 0; i < game.control.num_counters; i++) {

    if (counters[i].helptext != nullptr) {
      strvec_destroy(counters[i].helptext);
      counters[i].helptext = nullptr;
    }
  }

  game.control.num_counters = 0;
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
  fc_assert_ret_val(id < game.control.num_counters, nullptr);

  return &counters[id];
}

/************************************************************************//**
  Attaching given counter type to array containing counter type
  related to cities. Counter must be present in array for
  each counter in game, but we do not check this.
****************************************************************************/
void attach_city_counter(struct counter *counter)
{
  counters_city[number_city_counters] = counter;
  counters_city[number_city_counters]->index = number_city_counters;
  counters_city[number_city_counters]->ruledit_disabled = FALSE;
  number_city_counters++;
}

/************************************************************************//**
    Return id of a given counter
****************************************************************************/
int counter_id(struct counter *pcount)
{
  fc_assert_ret_val(pcount != nullptr, -1);

  return pcount - counters;
}

/************************************************************************//**
    Search for counter by rule name
    (return matched counter if found or nullptr)
****************************************************************************/
struct counter *counter_by_rule_name(const char *name)
{
  int i;
  fc_assert_ret_val(name != nullptr, nullptr);
  fc_assert_ret_val('\0' != name[0], nullptr);

  for (i = 0; i < game.control.num_counters; i++)
  {
    if (0 == fc_strcasecmp(name, counter_rule_name(&counters[i])))
    {
      return &counters[i];
    }
  }

  return nullptr;
}

/************************************************************************//**
    Search for counter by translated name
    (return matched counter if found or nullptr)
****************************************************************************/
struct counter *counter_by_translated_name(const char *name)
{
  int i;
  fc_assert_ret_val(name != nullptr, nullptr);
  fc_assert_ret_val('\0' != name[0], nullptr);

  for (i = 0; i < game.control.num_counters; i++)
  {
    if (0 == fc_strcasecmp(name,
                           counter_name_translation(&counters[i])))
    {
      return &counters[i];
    }
  }

  return nullptr;
}

/************************************************************************//**
    Returns translated name of given counter
****************************************************************************/
const char *counter_name_translation(const struct counter *counter)
{
  return name_translation_get(&counter->name);
}

/************************************************************************//**
     Return rule name of a given counter
****************************************************************************/
const char *counter_rule_name(struct counter *pcount)
{
  fc_assert_ret_val(pcount != nullptr, nullptr);

  return rule_name_get(&pcount->name);
}

/************************************************************************//**
   Return index in global counter's array
***************************************************************************/
int counter_index(const struct counter *pcount)
{
  fc_assert_ret_val(pcount != nullptr, -1);

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

  return nullptr;
}
