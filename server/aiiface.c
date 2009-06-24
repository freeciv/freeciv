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
#include "player.h"

/* server */
#include "aiiface.h"
#include "settlers.h"

/* ai */
#include "aicity.h"
#include "aiexplorer.h"
#include "aihand.h"
#include "aisettler.h"
#include "aitools.h"

/**************************************************************************
  Initialize player ai_funcs function pointers.
**************************************************************************/
void init_ai_funcs(struct player *pplayer)
{
  pplayer->ai_funcs.auto_settlers = auto_settlers_player;
  pplayer->ai_funcs.building_advisor_init = ai_manage_buildings;
  pplayer->ai_funcs.building_advisor = ai_advisor_choose_building;
  pplayer->ai_funcs.auto_explorer = ai_manage_explorer;
  pplayer->ai_funcs.first_activities = ai_do_first_activities;
  pplayer->ai_funcs.diplomacy_actions = ai_diplomacy_actions;
  pplayer->ai_funcs.last_activities = ai_do_last_activities;
  pplayer->ai_funcs.before_auto_settlers = ai_settler_init;
  pplayer->ai_funcs.treaty_evaluate = ai_treaty_evaluate;
  pplayer->ai_funcs.treaty_accepted = ai_treaty_accepted;
  pplayer->ai_funcs.first_contact = ai_diplomacy_first_contact;
  pplayer->ai_funcs.incident = ai_incident;
}

/**************************************************************************
  Call incident function of victim, or failing that, incident function
  of violator.
**************************************************************************/
void call_incident(enum incident_type type, struct player *violator,
                   struct player *victim)
{
  if (victim && victim->ai_funcs.incident) {
    victim->ai_funcs.incident(type, violator, victim);
  } else if (violator && violator->ai_funcs.incident) {
    violator->ai_funcs.incident(type, violator, victim);
  }
}
