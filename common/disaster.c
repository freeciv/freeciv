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

/* utility */
#include "fcintl.h"

/* common */
#include "fc_types.h"
#include "game.h"
#include "name_translation.h"

#include "disaster.h"

static struct disaster_type disasters[DISASTER_LAST] =
  {
    { DISASTER_EARTHQUAKE, N_("Earthquake"), 10, {} },
    { DISASTER_PLAGUE, N_("Plague"), 10, {} },
    { DISASTER_FIRE, N_("Fire"), 10, {} }
  };


/****************************************************************************
  Initialize disaster_type structures.
****************************************************************************/
void disaster_types_init(void)
{
  int i;

  for (i = 0; i < ARRAY_SIZE(disasters); i++) {
    disasters[i].id = i;
    BV_CLR_ALL(disasters[i].effects);
  }

  BV_SET(disasters[DISASTER_EARTHQUAKE].effects, DE_DESTROY_BUILDING);

  BV_SET(disasters[DISASTER_PLAGUE].effects, DE_REDUCE_POP);

  BV_SET(disasters[DISASTER_FIRE].effects, DE_DESTROY_BUILDING);
}

/****************************************************************************
  Free the memory associated with disaster types
****************************************************************************/
void disaster_types_free(void)
{
}

/**************************************************************************
  Return the disaster id.
**************************************************************************/
Disaster_type_id disaster_number(const struct disaster_type *pdis)
{
  fc_assert_ret_val(NULL != pdis, -1);

  return pdis->id;
}

/**************************************************************************
  Return the disaster index.

  Currently same as disaster_number(), paired with disaster_count()
  indicates use as an array index.
**************************************************************************/
Disaster_type_id disaster_index(const struct disaster_type *pdis)
{
  fc_assert_ret_val(NULL != pdis, -1);

  return pdis - disasters;
}

/**************************************************************************
  Return the number of disaster_types.
**************************************************************************/
Disaster_type_id disaster_count(void)
{
  return game.control.num_disaster_types;
}

/****************************************************************************
  Return disaster type of given id.
****************************************************************************/
struct disaster_type *disaster_by_number(Disaster_type_id id)
{
  fc_assert_ret_val(id >= 0 && id < game.control.num_disaster_types, NULL);

  return &disasters[id];
}

/****************************************************************************
  Return translated name of this disaster type.
****************************************************************************/
const char *disaster_name_translation(struct disaster_type *pdis)
{
  /*  return name_translation(&pdis->name); */

  return _(pdis->name);
}

/****************************************************************************
  Return untranslated name of this disaster type.
****************************************************************************/
const char *disaster_rule_name(struct disaster_type *pdis)
{
  /*  return rule_name(&pdis->name); */

  return pdis->name;
}

/****************************************************************************
  Check if disaster provides effect
****************************************************************************/
bool disaster_has_effect(const struct disaster_type *pdis,
                         enum disaster_effect_id effect)
{
  return BV_ISSET(pdis->effects, effect);
}
