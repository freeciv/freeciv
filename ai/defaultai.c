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
#include <config.h>
#endif

/* common */
#include "ai.h"
#include "player.h"

/* server/advisors */
#include "autosettlers.h"

/* ai */
#include "aicity.h"
#include "aihand.h"
#include "aisettler.h"
#include "aitools.h"
#include "aiunit.h"

#include "defaultai.h"

/**************************************************************************
  Setup player ai_funcs function pointers.
**************************************************************************/
void fc_ai_default_setup(struct ai_type *ai)
{
  ai->funcs.data_init = ai_data_init;
  ai->funcs.data_default = ai_data_default;
  ai->funcs.data_close = ai_data_close;

  ai->funcs.city_init = ai_city_init;
  ai->funcs.city_update = ai_city_update;
  ai->funcs.city_close = ai_city_close;

  ai->funcs.unit_init = ai_unit_init;
  ai->funcs.unit_turn_end = ai_unit_turn_end;
  ai->funcs.unit_close = ai_unit_close;
  ai->funcs.unit_move = ai_unit_move_or_attack;

  ai->funcs.auto_settler = ai_auto_settler;

  ai->funcs.first_activities = ai_do_first_activities;
  ai->funcs.diplomacy_actions = ai_diplomacy_actions;
  ai->funcs.last_activities = ai_do_last_activities;
  ai->funcs.before_auto_settlers = ai_settler_init;
  ai->funcs.treaty_evaluate = ai_treaty_evaluate;
  ai->funcs.treaty_accepted = ai_treaty_accepted;
  ai->funcs.first_contact = ai_diplomacy_first_contact;
  ai->funcs.incident = ai_incident;
}
