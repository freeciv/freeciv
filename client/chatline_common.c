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
#include <string.h>

#include "astring.h"
#include "chatline_g.h"

#include "chatline_common.h"

static int frozen_level = 0;
static struct astring remaining = ASTRING_INIT;

/**************************************************************************
  Turn on buffering, using a counter so that calls may be nested.
**************************************************************************/
void output_window_freeze()
{
  frozen_level++;

  if (frozen_level == 1) {
    assert(remaining.str == NULL);
    astr_minsize(&remaining, 1);
    remaining.str[0] = '\0';
  }
}

/**************************************************************************
  Turn off buffering if internal counter of number of times buffering
  was turned on falls to zero, to handle nested freeze/thaw pairs.
  When counter is zero, append the picked up data.
**************************************************************************/
void output_window_thaw()
{
  frozen_level--;
  assert(frozen_level >= 0);

  if (frozen_level == 0) {
    if (remaining.n > 2) {
      /* +1 to skip the initial '\n' */
      append_output_window(remaining.str + 1);
    }
    astr_free(&remaining);
  }
}

/**************************************************************************
  Turn off buffering and append the picked up data.
**************************************************************************/
void output_window_force_thaw()
{
  if (frozen_level > 0) {
    frozen_level = 1;
    output_window_thaw();
  }
}

/**************************************************************************
...
**************************************************************************/
void append_output_window(const char *astring)
{
  if (frozen_level == 0) {
    real_append_output_window(astring);
  } else {
    /* 
     * len_src doesn't include the trailing '\0'
     * len_dst does include the trailing '\0'
     */
    size_t len_src = strlen(astring), len_dst = remaining.n;

    /* +1 for the "\n" */
    astr_minsize(&remaining, len_dst + 1 + len_src);
    remaining.str[len_dst - 1] = '\n';

    /* +1 for the "\0" */
    memcpy(&remaining.str[len_dst], astring, len_src + 1);
  }
}
