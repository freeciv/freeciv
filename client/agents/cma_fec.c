/********************************************************************** 
 Freeciv - Copyright (C) 2001 - R. Falke
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

/**************************************************************************
 This is the common file for all front-end (Front End Common) for the
 citizen management agent (CMA).
**************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <string.h>

#include "fcintl.h"
#include "mem.h"
#include "log.h"
#include "support.h"
#include "game.h"

#include "attribute.h"
#include "agents.h"
#include "cma_fec.h"

#define RESULT_COLUMNS		10
#define BUFFER_SIZE		100
#define MAX_LEN_PRESET_NAME	80

struct cma_preset {
  char *descr;
  struct cma_parameter parameter;
} cma_preset;

#define SPECLIST_TAG preset
#define SPECLIST_TYPE struct cma_preset
#include "speclist.h"

#define SPECLIST_TAG preset
#define SPECLIST_TYPE struct cma_preset
#include "speclist_c.h"

#define preset_list_iterate(presetlist, ppreset) \
    TYPED_LIST_ITERATE(struct cma_preset, presetlist, ppreset)
#define preset_list_iterate_end  LIST_ITERATE_END

static struct preset_list preset_list;
static int preset_list_has_been_initialized = 0;

/****************************************************************************
 Is called if the game removes a city. It will clear the 
 "fe parameter" attribute to reduce the size of the savegame.   
*****************************************************************************/
static void city_remove(struct city *pcity)
{
  attr_city_set(ATTR_CITY_CMAFE_PARAMETER, pcity->id, 0, NULL);
}

/**************************************************************************
 Initialize the presets if there are no presets loaded on startup.
**************************************************************************/
void cmafec_init(void)
{
  struct agent self;

  if (!preset_list_has_been_initialized) {
    preset_list_init(&preset_list);
    preset_list_has_been_initialized = 1;
  }

  memset(&self, 0, sizeof(self));
  strcpy(self.name, "CMA");
  self.level = 1;
  self.city_callbacks[CB_REMOVE] = city_remove;
  register_agent(&self);
}

/**************************************************************************
 Sets the front-end parameter.
**************************************************************************/
void cmafec_set_fe_parameter(struct city *pcity,
			     const struct cma_parameter *const parameter)
{
  attr_city_set(ATTR_CITY_CMAFE_PARAMETER, pcity->id,
		sizeof(struct cma_parameter), parameter);
}

/****************************************************************
 Return the front-end parameter for the given city. Returns a dummy
 parameter if no parameter was set.
*****************************************************************/
void cmafec_get_fe_parameter(struct city *pcity, struct cma_parameter *dest)
{
  int len;
  struct cma_parameter parameter;

  /* our fe_parameter could be stale. our agents parameter is uptodate */
  if (cma_is_city_under_agent(pcity, &parameter)) {
    cma_copy_parameter(dest, &parameter);
    cmafec_set_fe_parameter(pcity, dest);
  } else {
    len = attr_city_get(ATTR_CITY_CMAFE_PARAMETER, pcity->id,
			  sizeof(struct cma_parameter), dest);

    if (len != 0) {
      assert(len == sizeof(struct cma_parameter));
    } else {
      /* We haven't seen this city previously; create a new dummy parameter. */
      int i;

      for (i = 0; i < NUM_STATS; i++) {
        dest->minimal_surplus[i] = 0;
        dest->factor[i] = 1;
      }

      dest->happy_factor = 1;
      dest->require_happy = 0;
      dest->factor_target = FT_SURPLUS;

      cmafec_set_fe_parameter(pcity, dest);
    }
  }
}

/**************************************************************************
 Adds a preset.
**************************************************************************/
void cmafec_preset_add(char *descr_name, struct cma_parameter *pparam)
{
  struct cma_preset *ppreset = fc_malloc(sizeof(struct cma_preset));

  if (!preset_list_has_been_initialized) {
    preset_list_init(&preset_list);
    preset_list_has_been_initialized = 1;
  }

  cma_copy_parameter(&ppreset->parameter, pparam);
  ppreset->descr = fc_malloc(MAX_LEN_PRESET_NAME);
  mystrlcpy(ppreset->descr, descr_name, MAX_LEN_PRESET_NAME);
  preset_list_insert(&preset_list, ppreset);
}

/**************************************************************************
 Removes a preset.
**************************************************************************/
void cmafec_preset_remove(int index)
{
  struct cma_preset *ppreset;

  assert(index >= 0 && index < cmafec_preset_num());

  ppreset = preset_list_get(&preset_list, index);
  preset_list_unlink(&preset_list, ppreset);
}

/**************************************************************************
 Returns the indexed preset's description.
**************************************************************************/
char *cmafec_preset_get_descr(int index)
{
  struct cma_preset *ppreset;

  assert(index >= 0 && index < cmafec_preset_num());

  ppreset = preset_list_get(&preset_list, index);
  return ppreset->descr;
}

/**************************************************************************
 Returns the indexed preset's parameter.
**************************************************************************/
const struct cma_parameter *const cmafec_preset_get_parameter(int index)
{
  struct cma_preset *ppreset;

  assert(index >= 0 && index < cmafec_preset_num());

  ppreset = preset_list_get(&preset_list, index);
  return &ppreset->parameter;
}

/**************************************************************************
 Returns the index of the preset which matches the given
 parameter. Returns -1 if no preset could be found.
**************************************************************************/
int cmafec_preset_get_index_of_parameter(const struct cma_parameter
					 *const parameter)
{
  int i;

  for (i = 0; i < preset_list_size(&preset_list); i++) {
    struct cma_preset *ppreset = preset_list_get(&preset_list, i);
    if (cma_are_parameter_equal(&ppreset->parameter, parameter)) {
      return i;
    }
  }
  return -1;
}

/**************************************************************************
 Returns the total number of presets.
**************************************************************************/
int cmafec_preset_num(void)
{
  return preset_list_size(&preset_list);
}

/**************************************************************************
...
**************************************************************************/
const char *const cmafec_get_short_descr_of_city(struct city *pcity)
{
  struct cma_parameter parameter;

  if (!cma_is_city_under_agent(pcity, &parameter)) {
    return _("none");
  } else {
    return cmafec_get_short_descr(&parameter);
  }
}

/**************************************************************************
 Returns the description of the matching preset or "custom" if no
 preset could be found.
**************************************************************************/
const char *const cmafec_get_short_descr(const struct cma_parameter *const
					 parameter)
{
  int index = cmafec_preset_get_index_of_parameter(parameter);

  if (index == -1) {
    return _("custom");
  } else {
    return cmafec_preset_get_descr(index);
  }
}

/**************************************************************************
...
**************************************************************************/
static const char *const get_city_growth_string(struct city *pcity,
						int surplus)
{
  int stock, cost, turns;
  static char buffer[50];

  if (surplus == 0) {
    my_snprintf(buffer, sizeof(buffer), N_("never"));
    return buffer;
  }

  stock = pcity->food_stock;
  cost = city_granary_size(pcity->size);

  stock += surplus;

  if (stock >= cost) {
    turns = 1;
  } else if (surplus > 0) {
    turns = ((cost - stock - 1) / surplus) + 1 + 1;
  } else {
    if (stock < 0) {
      turns = -1;
    } else {
      turns = (stock / surplus);
    }
  }
  my_snprintf(buffer, sizeof(buffer), "%d turn(s)", turns);
  return buffer;
}

/**************************************************************************
...
**************************************************************************/
static const char *const get_prod_complete_string(struct city *pcity,
						  int surplus)
{
  int stock, cost, turns;
  static char buffer[50];

  if (surplus <= 0) {
    my_snprintf(buffer, sizeof(buffer), N_("never"));
    return buffer;
  }

  stock = pcity->shield_stock;
  if (pcity->is_building_unit) {
    cost = get_unit_type(pcity->currently_building)->build_cost;
  } else {
    if (pcity->currently_building == B_CAPITAL) {
      my_snprintf(buffer, sizeof(buffer),
		  get_improvement_type(pcity->currently_building)->name);
      return buffer;
    }
    cost = get_improvement_type(pcity->currently_building)->build_cost;
  }

  stock += surplus;

  if (stock >= cost) {
    turns = 1;
  } else if (surplus > 0) {
    turns = ((cost - stock - 1) / surplus) + 1 + 1;
  } else {
    if (stock < 0) {
      turns = -1;
    } else {
      turns = (stock / surplus);
    }
  }
  my_snprintf(buffer, sizeof(buffer), "%d turn(s)", turns);
  return buffer;
}

/**************************************************************************
...
**************************************************************************/
const char *const cmafec_get_result_descr(struct city *pcity,
					  const struct cma_result *const
					  result,
					  const struct cma_parameter *const
					  parameter)
{
  int j;
  char buf[RESULT_COLUMNS][BUFFER_SIZE];
  static char buffer[600];

  if (!result->found_a_valid) {
    for (j = 0; j < RESULT_COLUMNS; j++)
      my_snprintf(buf[j], BUFFER_SIZE, "---");
  } else {
    my_snprintf(buf[0], BUFFER_SIZE, "%3d(%+3d)",
		result->production[FOOD], result->surplus[FOOD]);
    my_snprintf(buf[1], BUFFER_SIZE, "%3d(%+3d)",
		result->production[SHIELD], result->surplus[SHIELD]);
    my_snprintf(buf[2], BUFFER_SIZE, "%3d(%+3d)",
		result->production[TRADE], result->surplus[TRADE]);

    my_snprintf(buf[3], BUFFER_SIZE, "%3d(%+3d)",
		result->production[GOLD], result->surplus[GOLD]);
    my_snprintf(buf[4], BUFFER_SIZE, "%3d(%+3d)",
		result->production[LUXURY], result->surplus[LUXURY]);
    my_snprintf(buf[5], BUFFER_SIZE, "%3d(%+3d)",
		result->production[SCIENCE], result->surplus[SCIENCE]);

    my_snprintf(buf[6], BUFFER_SIZE, "%d/%d/%d/%d%s",
		pcity->size -
		(result->entertainers + result->scientists +
		 result->taxmen), result->entertainers, result->scientists,
		result->taxmen, result->happy ? _(" happy") : "");

    my_snprintf(buf[7], BUFFER_SIZE, "%s",
		get_city_growth_string(pcity, result->surplus[FOOD]));
    my_snprintf(buf[8], BUFFER_SIZE, "%s",
		get_prod_complete_string(pcity, result->surplus[SHIELD]));
    my_snprintf(buf[9], BUFFER_SIZE, "%s",
		cmafec_get_short_descr(parameter));
  }

  my_snprintf(buffer, sizeof(buffer),
	      _("Name: %s\n"
		"Food:       %10s Gold:    %10s\n"
		"Production: %10s Luxury:  %10s\n"
		"Trade:      %10s Science: %10s\n"
		"\n"
		"    People (W/E/S/T): %s\n"
		"          City grows: %s\n"
		"Production completed: %s"),
	      buf[9],
	      buf[0], buf[3],
	      buf[1], buf[4], buf[2], buf[5], buf[6], buf[7], buf[8]);

  freelog(LOG_DEBUG, "\n%s", buffer);
  return buffer;
}
