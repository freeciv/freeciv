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

#include "genlist.h"
#include "shared.h"		/* MAX_LEN_NAME */

#define MAX_LEN_WORKLIST 16
#define MAX_NUM_WORKLISTS 16
#define WORKLIST_END (U_LAST+B_LAST+MAX_NUM_WORKLISTS)

struct worklist {
  int is_valid;
  char name[MAX_LEN_NAME];
  int ids[MAX_LEN_WORKLIST];
};

struct worklist *create_worklist(void);
void init_worklist(struct worklist *pwl);
void destroy_worklist(struct worklist *pwl);

int worklist_is_empty(struct worklist *pwl);
int worklist_peek(struct worklist *pwl, int *id, int *is_unit);
int worklist_peek_ith(struct worklist *pwl, int *id, int *is_unit, int idx);
int worklist_peek_id(struct worklist *pwl);
int worklist_peek_id_ith(struct worklist *pwl, int idx);
void worklist_advance(struct worklist *pwl);

void copy_worklist(struct worklist *dst, struct worklist *src);

#endif /* FC__WORKLIST_H */
