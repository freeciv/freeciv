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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "city.h"
#include "mem.h"
#include "unit.h"

#include "worklist.h"

/****************************************************************
  Initialize a worklist to be empty and have a default name.
  For elements, only really need to set [0], but initialize the
  rest to avoid junk values in savefile.
****************************************************************/
void init_worklist(struct worklist *pwl)
{
  int i;

  pwl->is_valid = TRUE;
  strcpy(pwl->name, "a worklist");

  for (i = 0; i < MAX_LEN_WORKLIST; i++) {
    pwl->wlefs[i] = WEF_END;
    pwl->wlids[i] = 0;
  }
}

/****************************************************************************
  Returns the number of entries in the worklist.  The returned value can
  also be used as the next available worklist index (assuming that
  len < MAX_LEN_WORKLIST).
****************************************************************************/
int worklist_length(const struct worklist *pwl)
{
  int len = 0;

  if (pwl) {
    while (len < MAX_LEN_WORKLIST && pwl->wlefs[len] != WEF_END) {
      len++;
    }
  }

  return len;
}

/****************************************************************
...
****************************************************************/
bool worklist_is_empty(const struct worklist *pwl)
{
  return !pwl || pwl->wlefs[0] == WEF_END;
}

/****************************************************************
  Fill in the id and is_unit values for the head of the worklist
  if the worklist is non-empty.  Return 1 iff id and is_unit
  are valid.
****************************************************************/
bool worklist_peek(const struct worklist *pwl, int *id, bool *is_unit)
{
  if (worklist_is_empty(pwl))
    return FALSE;

  return worklist_peek_ith(pwl, id, is_unit, 0);
}

/****************************************************************
  Fill in the id and is_unit values for the ith element in the
  worklist.  If the worklist has fewer than i elements, return 0.
****************************************************************/
bool worklist_peek_ith(const struct worklist *pwl, int *id, bool *is_unit,
		      int idx)
{
  int i;

  /* Out of possible bounds. */
  if (idx < 0 || MAX_LEN_WORKLIST <= idx)
    return FALSE;

  /* Worklist isn't long enough. */
  for (i = 0; i <= idx; i++)
    if (pwl->wlefs[i] == WEF_END)
      return FALSE;

  *is_unit = (pwl->wlefs[idx] == WEF_UNIT);
  *id = pwl->wlids[idx];

  return TRUE;
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

/****************************************************************************
  Adds the id to the next available slot in the worklist.  'id' is the ID of
  the unit/building to be produced; is_unit specifies whether it's a unit or
  a building.  Returns TRUE if successful.
****************************************************************************/
bool worklist_append(struct worklist *pwl, int id, bool is_unit)
{
  int next_index = worklist_length(pwl);

  if (next_index >= MAX_LEN_WORKLIST) {
    return FALSE;
  }
  
  pwl->wlefs[next_index] = (is_unit ? WEF_UNIT : WEF_IMPR);
  pwl->wlids[next_index] = id;
  
  if (next_index + 1 < MAX_LEN_WORKLIST) {
    pwl->wlefs[next_index + 1] = WEF_END;
    pwl->wlids[next_index + 1] = 0;
  }

  return TRUE;
}

/****************************************************************************
  Inserts the production at the location idx in the worklist, thus moving
  all subsequent entries down.  'id' specifies the unit/building to
  be produced; is_unit tells whether it's a unit or building.  Returns TRUE
  if successful.
****************************************************************************/
bool worklist_insert(struct worklist *pwl, int id, bool is_unit, int idx)
{
  int len = worklist_length(pwl);

  if (len >= MAX_LEN_WORKLIST || idx > len) {
    /* NOTE: the insert will fail rather than just dropping the last item. */
    return FALSE;
  }

  /* move all active values down an index to get room for new id
   * move from [idx .. len - 1] to [idx + 1 .. len].
   * Don't copy WEF_END (the terminator) because that might end up outside of
   * the list. */
  memmove(&pwl->wlefs[idx + 1], &pwl->wlefs[idx],
	  sizeof(pwl->wlefs[0]) * (len - idx));
  memmove(&pwl->wlids[idx + 1], &pwl->wlids[idx],
	  sizeof(pwl->wlids[0]) * (len - idx));
  
  pwl->wlefs[idx] = (is_unit ? WEF_UNIT : WEF_IMPR);
  pwl->wlids[idx] = id;
  
  /* Since we don't copy the WEF_END, need to reinsert it at the end
   * if there is room. */
  if (len + 1 < MAX_LEN_WORKLIST) {
    pwl->wlefs[len + 1] = WEF_END;
    pwl->wlids[len + 1] = 0;
  }
  
  return TRUE;
}

/**************************************************************************
  Return TRUE iff the two worklists are equal.
**************************************************************************/
bool are_worklists_equal(const struct worklist *wlist1,
			 const struct worklist *wlist2)
{
  int i, len = worklist_length(wlist1);

  if (wlist1->is_valid != wlist2->is_valid) {
    return FALSE;
  }
  if (worklist_length(wlist2) != len) {
    return FALSE;
  }

  for (i = 0; i < len; i++) {
    if (wlist1->wlefs[i] != wlist2->wlefs[i] ||
	wlist1->wlids[i] != wlist2->wlids[i]) {
      return FALSE;
    }
  }

  return TRUE;
}
