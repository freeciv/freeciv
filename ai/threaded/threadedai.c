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
#include "taimsg.h"
#include "taiplayer.h"

const char *fc_ai_threaded_capstr(void);
bool fc_ai_threaded_setup(struct ai_type *ai);

static void tai_init_self(struct ai_type *ai);
static struct ai_type *tai_get_self(void);

static struct ai_type *self = NULL;

/**************************************************************************
  Set pointer to ai type of the threaded ai.
**************************************************************************/
static void tai_init_self(struct ai_type *ai)
{
  self = ai;

  tai_init_threading();
}

/**************************************************************************
  Get pointer to ai type of the threaded ai.
**************************************************************************/
static struct ai_type *tai_get_self(void)
{
  return self;
}

#define TAI_FUNC_WRAP(_func, ...)     \
struct ai_type *ait = tai_get_self(); \
_func(ait, __VA_ARGS__ );

/**************************************************************************
  Call default ai with threaded ai type as parameter.
**************************************************************************/
static void twai_player_alloc(struct player *pplayer)
{
  TAI_FUNC_WRAP(tai_player_alloc, pplayer);
}

/**************************************************************************
  Call default ai with threaded ai type as parameter.
**************************************************************************/
static void twai_player_free(struct player *pplayer)
{
  TAI_FUNC_WRAP(tai_player_free, pplayer);
}

/**************************************************************************
  Call default ai with threaded ai type as parameter.
**************************************************************************/
static void twai_control_gained(struct player *pplayer)
{
  TAI_FUNC_WRAP(tai_control_gained, pplayer);
}

/**************************************************************************
  Call default ai with threaded ai type as parameter.
**************************************************************************/
static void twai_control_lost(struct player *pplayer)
{
  TAI_FUNC_WRAP(tai_control_lost, pplayer);
}

/**************************************************************************
  Call default ai with threaded ai type as parameter.
**************************************************************************/
static void twai_first_activities(struct player *pplayer)
{
  TAI_FUNC_WRAP(tai_first_activities, pplayer);
}

/**************************************************************************
  Call default ai with threaded ai type as parameter.
**************************************************************************/
static void twai_phase_finished(struct player *pplayer)
{
  TAI_FUNC_WRAP(tai_phase_finished, pplayer);
}

/**************************************************************************
  Call default ai with threaded ai type as parameter.
**************************************************************************/
static void twai_refresh(struct player *pplayer)
{
  TAI_FUNC_WRAP(tai_refresh, pplayer);
}

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

  tai_init_self(ai);

  ai->funcs.player_alloc = twai_player_alloc;
  ai->funcs.player_free = twai_player_free;
  ai->funcs.gained_control = twai_control_gained;
  ai->funcs.lost_control = twai_control_lost;

  ai->funcs.first_activities = twai_first_activities;
  ai->funcs.phase_finished = twai_phase_finished;

  ai->funcs.refresh = twai_refresh;

  return TRUE;
}
