/********************************************************************** 
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

#include <assert.h>

#include "game.h"
#include "repodlgs_g.h"

#include "repodlgs_common.h"

/****************************************************************
  Fills out the array of struct improvement_entry given by
  entries. The array must be able to hold at least B_LAST entries.
*****************************************************************/
void get_economy_report_data(struct improvement_entry *entries,
			     int *num_entries_used, int *total_cost,
			     int *total_income)
{
  *num_entries_used = 0;
  *total_cost = 0;

  impr_type_iterate(impr_id) {
    if (!is_wonder(impr_id)) {
      int count = 0, cost = 0;
      city_list_iterate(game.player_ptr->cities, pcity) {
	if (city_got_building(pcity, impr_id)) {
	  count++;
	  cost += improvement_upkeep(pcity, impr_id);
	}
      }
      city_list_iterate_end;

      if (count == 0) {
	continue;
      }

      entries[*num_entries_used].type = impr_id;
      entries[*num_entries_used].count = count;
      entries[*num_entries_used].total_cost = cost;
      entries[*num_entries_used].cost = cost / count;
      (*num_entries_used)++;
      
      *total_cost += cost;
    }
  } impr_type_iterate_end;

  *total_income = 0;

  city_list_iterate(game.player_ptr->cities, pcity) {
    *total_income += pcity->tax_total;
    if (!pcity->is_building_unit && pcity->currently_building == B_CAPITAL) {
      *total_income += pcity->shield_surplus;
    }
  } city_list_iterate_end;
}

static int frozen_level = 0;

/******************************************************************
 Turn off updating of reports
*******************************************************************/
void report_dialogs_freeze(void)
{
  frozen_level++;
}

/******************************************************************
 Turn on updating of reports
*******************************************************************/
void report_dialogs_thaw(void)
{
  frozen_level--;
  assert(frozen_level >= 0);
  if (frozen_level == 0) {
    update_report_dialogs();
  }
}

/******************************************************************
 Turn on updating of reports
*******************************************************************/
void report_dialogs_force_thaw(void)
{
  frozen_level = 1;
  report_dialogs_thaw();
}

/******************************************************************
 ...
*******************************************************************/
bool is_report_dialogs_frozen(void)
{
  return frozen_level > 0;
}
