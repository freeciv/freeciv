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

struct improvement_entry
{
  Impr_Type_id type;
  int count, cost, total_cost;
};

void get_economy_report_data(struct improvement_entry *entries,
			     int *num_entries_used, int *total_cost,
			     int *total_income);

#endif /* FC__REPODLGS_COMMON_H */
