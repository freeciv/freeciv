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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "city.h"
#include "mem.h"
#include "unit.h"

#include "worklist.h"

struct worklist *create_worklist(void) {
  struct worklist *pwl = fc_malloc(sizeof(struct worklist));
  init_worklist(pwl);
  
  return pwl;
}
  
void init_worklist(struct worklist *pwl) {
  pwl->ids[0] = WORKLIST_END;
  pwl->is_valid = 1;
  strcpy(pwl->name, "a worklist");
}

void destroy_worklist(struct worklist *pwl) {
  free(pwl);
}

int worklist_is_empty(struct worklist *pwl) {
  return pwl==NULL || pwl->ids[0] == WORKLIST_END;
}
  
int worklist_peek(struct worklist *pwl, int *id, int *is_unit) {
  if (worklist_is_empty(pwl))
    return 0;

  return worklist_peek_ith(pwl, id, is_unit, 0);
}


int worklist_peek_ith(struct worklist *pwl, int *id, int *is_unit, int idx) {
  if (idx < 0 || MAX_LEN_WORKLIST <= idx)
    return 0;

  *is_unit = pwl->ids[idx] >= B_LAST;
  *id = *is_unit ? pwl->ids[idx]-B_LAST : pwl->ids[idx];

  return 1;
}

int worklist_peek_id(struct worklist *pwl) {
  return worklist_peek_id_ith(pwl, 0);
}

int worklist_peek_id_ith(struct worklist *pwl, int idx) {
  return pwl->ids[idx];
}

void worklist_advance(struct worklist *pwl) {
  memmove(&pwl->ids[0], &pwl->ids[1], sizeof(int) * (MAX_LEN_WORKLIST-1));
  pwl->ids[MAX_LEN_WORKLIST-1] = WORKLIST_END;
}  

void copy_worklist(struct worklist *dst, struct worklist *src) {
  memcpy(dst->ids, src->ids, sizeof(int) * MAX_LEN_WORKLIST);
  strcpy(dst->name, src->name);
  dst->is_valid = src->is_valid;
}
