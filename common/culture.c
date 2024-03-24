/****************************************************************************
 Freeciv - Copyright (C) 2004 - The Freeciv Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
****************************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* common */
#include "city.h"
#include "effects.h"
#include "game.h"
#include "player.h"

#include "culture.h"

/************************************************************************//**
  Return current culture score of the city.
****************************************************************************/
int city_culture(const struct city *pcity)
{
  return pcity->history
    + get_city_bonus(pcity, EFT_PERFORMANCE)
    * (100 + get_city_bonus(pcity, EFT_CULTURE_PCT)) / 100;
}

/************************************************************************//**
  How much history city gains this turn.
****************************************************************************/
int city_history_gain(const struct city *pcity)
{
  return get_city_bonus(pcity, EFT_HISTORY)
    * (100 + get_city_bonus(pcity, EFT_CULTURE_PCT)) / 100
    + pcity->history * game.info.history_interest_pml / 1000;
}

/************************************************************************//**
  Return current culture score of the player.
****************************************************************************/
int player_culture(const struct player *plr)
{
  int culture = plr->history
    + get_player_bonus(plr, EFT_NATION_PERFORMANCE)
    * (100 + get_player_bonus(plr, EFT_CULTURE_PCT)) / 100;

  city_list_iterate(plr->cities, pcity) {
    culture += city_culture(pcity);
  } city_list_iterate_end;

  return culture;
}

/************************************************************************//**
  How much nation-wide history player gains this turn. Does NOT include
  history gains of individual cities.
****************************************************************************/
int nation_history_gain(const struct player *pplayer)
{
  return get_player_bonus(pplayer, EFT_NATION_HISTORY)
    * (100 + get_player_bonus(pplayer, EFT_CULTURE_PCT)) / 100
    + pplayer->history * game.info.history_interest_pml / 1000;
}
