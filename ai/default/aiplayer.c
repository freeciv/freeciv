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
#include "city.h"
#include "unit.h"

/* ai */
#include "aidata.h"

#include "aiplayer.h"

static struct ai_type *self = NULL;

/**************************************************************************
  Set pointer to ai type of the default ai.
**************************************************************************/
void default_ai_set_self(struct ai_type *ai)
{
  self = ai;
}

/**************************************************************************
  Get pointer to ai type of the default ai.
**************************************************************************/
struct ai_type *default_ai_get_self(void)
{
  return self;
}

/**************************************************************************
  Initialize player for use with default AI. Note that this is called
  for all players, not just for those default AI is controlling.
**************************************************************************/
void dai_player_alloc(struct player *pplayer)
{
  struct ai_plr *player_data = fc_calloc(1, sizeof(struct ai_plr));

  player_set_ai_data(pplayer, default_ai_get_self(), player_data);

  dai_data_init(pplayer);
}

/**************************************************************************
  Free player from use with default AI.
**************************************************************************/
void dai_player_free(struct player *pplayer)
{
  struct ai_plr *player_data = def_ai_player_data(pplayer, default_ai_get_self());

  dai_data_close(pplayer);

  if (player_data != NULL) {
    player_set_ai_data(pplayer, default_ai_get_self(), NULL);
    FC_FREE(player_data);
  }
}

/**************************************************************************
  Store player specific data to savegame
**************************************************************************/
void dai_player_save(struct player *pplayer, struct section_file *file, int plrno)
{
  players_iterate(aplayer) {
    struct ai_dip_intel *adip = dai_diplomacy_get(pplayer, aplayer);
    char buf[32];

    fc_snprintf(buf, sizeof(buf), "player%d.ai%d", plrno,
                player_index(aplayer));

    secfile_insert_int(file, adip->spam,
                       "%s.spam", buf);
    secfile_insert_int(file, adip->countdown,
                       "%s.countdown", buf);
    secfile_insert_int(file, adip->war_reason,
                       "%s.war_reason", buf);
    secfile_insert_int(file, adip->ally_patience,
                       "%s.patience", buf);
    secfile_insert_int(file, adip->warned_about_space,
                       "%s.warn_space", buf);
    secfile_insert_int(file, adip->asked_about_peace,
                       "%s.ask_peace", buf);
    secfile_insert_int(file, adip->asked_about_alliance,
                       "%s.ask_alliance", buf);
    secfile_insert_int(file, adip->asked_about_ceasefire,
                       "%s.ask_ceasefire", buf);
  } players_iterate_end;
}

/**************************************************************************
  Load player specific data from savegame
**************************************************************************/
void dai_player_load(struct player *pplayer, struct section_file *file, int plrno)
{
  players_iterate(aplayer) {
    struct ai_dip_intel *adip = dai_diplomacy_get(pplayer, aplayer);
    char buf[32];

    fc_snprintf(buf, sizeof(buf), "player%d.ai%d", plrno,
                player_index(aplayer));

    adip->spam
         = secfile_lookup_int_default(file, 0, "%s.spam", buf);
    adip->countdown
         = secfile_lookup_int_default(file, -1, "%s.countdown", buf);
    adip->war_reason
         = secfile_lookup_int_default(file, 0, "%s.war_reason", buf);
    adip->ally_patience
         = secfile_lookup_int_default(file, 0, "%s.patience", buf);
    adip->warned_about_space
         = secfile_lookup_int_default(file, 0, "%s.warn_space", buf);
    adip->asked_about_peace
         = secfile_lookup_int_default(file, 0, "%s.ask_peace", buf);
    adip->asked_about_alliance
         = secfile_lookup_int_default(file, 0, "%s.ask_alliance", buf);
    adip->asked_about_ceasefire
         = secfile_lookup_int_default(file, 0, "%s.ask_ceasefire", buf);
  } players_iterate_end;
}
