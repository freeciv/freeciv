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
#ifndef FC__WORKLIST_H
#define FC__WORKLIST_H

#include "shared.h"		/* MAX_LEN_NAME */

#define MAX_LEN_WORKLIST 16
#define MAX_NUM_WORKLISTS 16

/* worklist element flags */
enum worklist_elem_flag {
  WEF_END,	/* element is past end of list */
  WEF_UNIT,	/* element specifies a unit to be built */
  WEF_IMPR,	/* element specifies an improvement to be built */
  WEF_LAST	/* leave this last */
};

/* a worklist */
struct worklist {
  bool is_valid;
  char name[MAX_LEN_NAME];
  enum worklist_elem_flag wlefs[MAX_LEN_WORKLIST];
  int wlids[MAX_LEN_WORKLIST];
};

void init_worklist(struct worklist *pwl);

int worklist_length(const struct worklist *pwl);
bool worklist_is_empty(const struct worklist *pwl);
bool worklist_peek(const struct worklist *pwl, int *id, bool *is_unit);
bool worklist_peek_ith(const struct worklist *pwl, int *id, bool *is_unit,
		      int idx);
void worklist_advance(struct worklist *pwl);

void copy_worklist(struct worklist *dst, const struct worklist *src);
void worklist_remove(struct worklist *pwl, int idx);
bool worklist_append(struct worklist *pwl, int id, bool is_unit);
bool worklist_insert(struct worklist *pwl, int id, bool is_unit, int idx);
bool are_worklists_equal(const struct worklist *wlist1,
			 const struct worklist *wlist2);


#endif /* FC__WORKLIST_H */
