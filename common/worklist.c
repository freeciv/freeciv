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


/****************************************************************
...
****************************************************************/
struct worklist *create_worklist(void)
{
  struct worklist *pwl = fc_malloc(sizeof(struct worklist));
  init_worklist(pwl);

  return pwl;
}

/****************************************************************
  Initialize a worklist to be empty and have a default name.
  For elements, only really need to set [0], but initialize the
  rest to avoid junk values in savefile.
****************************************************************/
void init_worklist(struct worklist *pwl)
{
  int i;

  pwl->is_valid = 1;
  strcpy(pwl->name, "a worklist");

  for (i = 0; i < MAX_LEN_WORKLIST; i++) {
    pwl->wlefs[i] = WEF_END;
    pwl->wlids[i] = 0;
  }
}

/****************************************************************
...
****************************************************************/
void destroy_worklist(struct worklist *pwl)
{
  free(pwl);
}

/****************************************************************
...
****************************************************************/
int worklist_length(const struct worklist *pwl)
{
  int len = 0;

  if (pwl != NULL) {
    for (len = 0; len < MAX_LEN_WORKLIST && pwl->wlefs[len] != WEF_END; len++) ;
  }

  return len;
}

/****************************************************************
...
****************************************************************/
int worklist_is_empty(const struct worklist *pwl)
{
  return pwl == NULL || pwl->wlefs[0] == WEF_END;
}

/****************************************************************
  Fill in the id and is_unit values for the head of the worklist
  if the worklist is non-empty.  Return 1 iff id and is_unit
  are valid.
****************************************************************/
int worklist_peek(const struct worklist *pwl, int *id, int *is_unit)
{
  if (worklist_is_empty(pwl))
    return 0;

  return worklist_peek_ith(pwl, id, is_unit, 0);
}

/****************************************************************
  Fill in the id and is_unit values for the ith element in the
  worklist.  If the worklist has fewer than i elements, return 0.
****************************************************************/
int worklist_peek_ith(const struct worklist *pwl, int *id, int *is_unit,
		      int idx)
{
  int i;

  /* Out of possible bounds. */
  if (idx < 0 || MAX_LEN_WORKLIST <= idx)
    return 0;

  /* Worklist isn't long enough. */
  for (i = 0; i <= idx; i++)
    if (pwl->wlefs[i] == WEF_END)
      return 0;

  *is_unit = (pwl->wlefs[idx] == WEF_UNIT);
  *id = pwl->wlids[idx];

  return 1;
}

/****************************************************************
...
****************************************************************/
void worklist_advance(struct worklist *pwl)
{
  worklist_remove(pwl, 0);
}  

/****************************************************************
...
****************************************************************/
void copy_worklist(struct worklist *dst, const struct worklist *src)
{
  memcpy(dst, src, sizeof(struct worklist));
}

/****************************************************************
...
****************************************************************/
void worklist_remove(struct worklist *pwl, int idx)
{
  /* Don't try to remove something way outside of the worklist. */
  if (idx < 0 || MAX_LEN_WORKLIST <= idx)
    return;

  /* Slide everything up one spot. */
  if (idx < MAX_LEN_WORKLIST-1) {
    memmove(&pwl->wlefs[idx], &pwl->wlefs[idx+1],
	    sizeof(enum worklist_elem_flag) * (MAX_LEN_WORKLIST-1-idx));
    memmove(&pwl->wlids[idx], &pwl->wlids[idx+1],
	    sizeof(int) * (MAX_LEN_WORKLIST-1-idx));
  }

  pwl->wlefs[MAX_LEN_WORKLIST-1] = WEF_END;
  pwl->wlids[MAX_LEN_WORKLIST-1] = 0;
}
