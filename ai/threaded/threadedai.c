/***********************************************************************
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
#include <fc_config.h>
#endif

/* common */
#include "ai.h"

/* threaded ai */
#include "taiplayer.h"

const char *fc_ai_threaded_capstr(void);
bool fc_ai_threaded_setup(struct ai_type *ai);

/**************************************************************************
  Return module capability string
**************************************************************************/
const char *fc_ai_threaded_capstr(void)
{
  return FC_AI_MOD_CAPSTR;
}

/**************************************************************************
  Setup player ai_funcs function pointers.
**************************************************************************/
bool fc_ai_threaded_setup(struct ai_type *ai)
{
  if (!has_thread_cond_impl()) {
    log_error(_("This Freeciv compilation has no full threads "
                "implementation, threaded ai cannot be used."));
    return FALSE;
  }

  strncpy(ai->name, "threaded", sizeof(ai->name));

  tai_set_self(ai);

  ai->funcs.player_alloc = tai_player_alloc;
  ai->funcs.player_free = tai_player_free;
  ai->funcs.gained_control = tai_control_gained;
  ai->funcs.lost_control = tai_control_lost;

  return TRUE;
}
