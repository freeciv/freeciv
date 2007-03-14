/****************************************************************************
 Freeciv - Copyright (C) 2005 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
****************************************************************************/

#ifndef FC__UNITLIST_H
#define FC__UNITLIST_H

#include "fc_types.h"
#include "unit.h"		/* for diplomat_actions */
#include "unittype.h"		/* for unit_flag_id */

/* get 'struct unit_list' and related functions: */
#define SPECLIST_TAG unit
#define SPECLIST_TYPE struct unit
#include "speclist.h"

#define unit_list_iterate(unitlist, punit) \
    TYPED_LIST_ITERATE(struct unit, unitlist, punit)
#define unit_list_iterate_end  LIST_ITERATE_END

#define unit_list_iterate_safe(unitlist, punit)				    \
{									    \
  int _size = unit_list_size(unitlist);					    \
									    \
  if (_size > 0) {							    \
    int _ids[_size], _i = 0;						    \
									    \
    unit_list_iterate(unitlist, punit) {				    \
      _ids[_i++] = punit->id;						    \
    } unit_list_iterate_end;						    \
    for (_i = 0; _i < _size; _i++) {					    \
      struct unit *punit = find_unit_by_id(_ids[_i]);			    \
									    \
      if (punit) {

#define unit_list_iterate_safe_end					    \
      }									    \
    }									    \
  }									    \
}

struct unit *unit_list_find(const struct unit_list *punitlist, int unit_id);

void unit_list_sort_ord_map(struct unit_list *punitlist);
void unit_list_sort_ord_city(struct unit_list *punitlist);

bool can_units_do(const struct unit_list *punits,
		  bool (can_fn)(const struct unit *punit));
bool can_units_do_activity(const struct unit_list *punits,
			   enum unit_activity activity);
bool can_units_do_base(const struct unit_list *punits,
                       enum base_type_id base);
bool can_units_do_base_gui(const struct unit_list *punits,
                           enum base_gui_type base_gui);
bool can_units_do_diplomat_action(const struct unit_list *punits,
				  enum diplomat_actions action);
bool units_have_flag(const struct unit_list *punits, enum unit_flag_id flag,
		     bool has_flag);
bool units_are_occupied(const struct unit_list *punits);
bool units_can_load(const struct unit_list *punits);
bool units_can_unload(const struct unit_list *punits);
bool units_have_activity_on_tile(const struct unit_list *punits,
				 enum unit_activity activity);

#endif  /* FC__UNITLIST_H */
