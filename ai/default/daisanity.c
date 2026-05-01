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

/* server */
#include "sanitycheck.h"

/* ai/default */
#include "daidata.h"
#include "daiplayer.h"

#include "daisanity.h"

/**********************************************************************//**
  Run sanity checking for the AI player.
**************************************************************************/
void dai_sanity_check(struct ai_type *ait, struct player *pplayer)
{
#ifdef SANITY_CHECKING

#define SANITY_CHECK(check) \
  fc_assert_full(__FILE__, __FUNCTION__, __FC_LINE__, check, , NOLOGMSG, NOLOGMSG)

  struct player *wt;

  if (!pplayer->is_alive) {
    return;
  }

  wt = def_ai_player_data(pplayer, ait)->diplomacy.war_target;

  players_iterate_alive(opponent) {
    struct ai_dip_intel *adip = dai_diplomacy_get(ait, pplayer, opponent);
    bool at_war = pplayers_at_war(pplayer, opponent);
    bool war_target = (wt == opponent);

    /* SANITY_CHECK(adip->countdown < 0 || !at_war); */
    SANITY_CHECK(adip->countdown >= -1 || at_war || war_target);
  } players_iterate_alive_end;

#endif /* SANITY_CHECKING */
}
