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

#ifndef FC__REPODLGS_COMMON_H
#define FC__REPODLGS_COMMON_H

#include "improvement.h"
#include "unittype.h"

#include "citydlg_common.h" /* for city request functions */

struct improvement_entry
{
  Impr_Type_id type;
  int count, cost, total_cost;
};

struct unit_entry
{
  Unit_Type_id type;
  int count, cost, total_cost;
};

void get_economy_report_data(struct improvement_entry *entries,
			     int *num_entries_used, int *total_cost,
			     int *total_income);
/* This function returns an array with the gold upkeeped units.
 * FIXME: Many clients doesn't yet use this function and show also only the
 * buildings in the economy reports 
 * I think that there should be only one function which returns an array of
 * char* arrays like some other common functions but that means updating all
 * client simultaneously and I simply can't */
void get_economy_report_units_data(struct unit_entry *entries,
				   int *num_entries_used, int *total_cost);

void report_dialogs_freeze(void);
void report_dialogs_thaw(void);
void report_dialogs_force_thaw(void);
bool is_report_dialogs_frozen(void);

struct options_settable {
  char *name;
  char *short_help;
  char *extra_help;
  int type;
  unsigned char category;

  int val;
  int default_val;
  int min;
  int max;

  char *strval;
  char *default_strval;
};

extern struct options_settable *settable_options;
extern int num_settable_options;

extern char **options_categories;
extern int num_options_categories;

void settable_options_init(void);
void settable_options_free(void);

void sell_all_improvements(Impr_Type_id impr, bool obsolete_only,
			   char *message, size_t message_sz);
void disband_all_units(Unit_Type_id type, bool in_cities_only,
		       char *message, size_t message_sz);

#endif /* FC__REPODLGS_COMMON_H */
