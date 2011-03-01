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
#include "player.h"

/* server */
#include "aiiface.h"

/* server/advisors */
#include "advdata.h"
#include "autosettlers.h"

/* ai */
#include "advdiplomacy.h"
#include "advmilitary.h"
#include "aicity.h"
#include "aidata.h"
#include "aiferry.h"
#include "aihand.h"
#include "aiplayer.h"
#include "aisettler.h"
#include "aitools.h"
#include "aiunit.h"

#include "defaultai.h"

/* This function is only needed as symbol in module. Nevertheless
   gcc requires that there is prior prototype. */
const char *fc_ai_default_capstr(void);

/**************************************************************************
  Return module capability string
**************************************************************************/
const char *fc_ai_default_capstr(void)
{
  return FC_AI_MOD_CAPSTR;
}

/**************************************************************************
  Setup player ai_funcs function pointers.
**************************************************************************/
bool fc_ai_default_setup(struct ai_type *ai)
{
  default_ai_set_self(ai);

  strncpy(ai->name, FC_AI_DEFAULT_NAME, sizeof(ai->name));

  /* ai->funcs.game_free = NULL; */

  ai->funcs.player_alloc = ai_player_alloc;
  ai->funcs.player_free = ai_player_free;
  ai->funcs.player_save = ai_player_save;
  ai->funcs.player_load = ai_player_load;
  ai->funcs.gained_control = assess_danger_player;
  /* ai->funcs.lost_control = NULL; */
  ai->funcs.split_by_civil_war = assess_danger_player;

  ai->funcs.phase_begin = ai_data_phase_begin;
  ai->funcs.phase_finished = ai_data_phase_finished;

  ai->funcs.city_alloc = ai_city_alloc;
  ai->funcs.city_free = ai_city_free;
  /*
    ai->funcs.city_got = NULL;
    ai->funcs.city_lost = NULL;
  */
  ai->funcs.city_save = ai_city_save;
  ai->funcs.city_load = ai_city_load;
  ai->funcs.choose_building = ai_build_adv_override;
  ai->funcs.impr_want = want_techs_for_improvement_effect;
  ai->funcs.impr_keep_want = dont_want_tech_obsoleting_impr;

  ai->funcs.units_ruleset_init = ai_units_ruleset_init;

  /* FIXME: We should allocate memory only for units owned by
     default ai in unit_got. We track no data
     about enemy units.
     But advisors code still depends on some default ai data (role) to be
     always allocated. */
  /*
    ai->funcs.unit_alloc = NULL;
    ai->funcs.unit_free = NULL;
    ai->funcs.unit_got = ai_unit_init;
    ai->funcs.unit_lost = ai_unit_close;
  */
  ai->funcs.unit_alloc = ai_unit_init;
  ai->funcs.unit_free = ai_unit_close;
  ai->funcs.unit_got = NULL;
  ai->funcs.unit_lost = NULL;
  ai->funcs.unit_created = aiferry_init_ferry;

  ai->funcs.unit_turn_end = ai_unit_turn_end;
  ai->funcs.unit_move = ai_unit_move_or_attack;
  ai->funcs.unit_task = ai_unit_new_adv_task;

  ai->funcs.unit_save = ai_unit_save;
  ai->funcs.unit_load = ai_unit_load;

  ai->funcs.settler_reset = ai_auto_settler_reset;
  ai->funcs.settler_run = ai_auto_settler_run;

  ai->funcs.first_activities = ai_do_first_activities;
  ai->funcs.diplomacy_actions = ai_diplomacy_actions;
  ai->funcs.last_activities = ai_do_last_activities;
  ai->funcs.treaty_evaluate = ai_treaty_evaluate;
  ai->funcs.treaty_accepted = ai_treaty_accepted;
  ai->funcs.first_contact = ai_diplomacy_first_contact;
  ai->funcs.incident = ai_incident;

  ai->funcs.consider_plr_dangerous = ai_consider_plr_dangerous;
  ai->funcs.consider_tile_dangerous = ai_consider_tile_dangerous;

  return TRUE;
}
