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

/* server */
#include "aiiface.h"

/* server/advisors */
#include "autosettlers.h"

/* ai */
#include "defaultai.h"

/**************************************************************************
  Initialize default ai_type.
**************************************************************************/
void ai_init(void)
{
  struct ai_type *ai = ai_type_alloc();

  init_ai(ai);

  fc_ai_default_setup(ai);
}

/**************************************************************************
  Call incident function of victim.
**************************************************************************/
void call_incident(enum incident_type type, struct player *violator,
                   struct player *victim)
{
  CALL_PLR_AI_FUNC(incident, victim, type, violator, victim);
}
