/**********************************************************************
 Freeciv - Copyright (C) 2005 - The Freeciv Project
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
#include "effects.h"

/* server/scripting */
#include "script.h"

#include "api_effects.h"

/************************************************************************
  Returns the effect bonus in the world
************************************************************************/
int api_effects_world_bonus(const char *effect_type)
{
  enum effect_type etype = EFT_LAST;
  SCRIPT_CHECK_ARG_NIL(effect_type, 1, string, 0);

  etype = effect_type_from_str(effect_type);
  if (etype == EFT_LAST) {
    return 0;
  }
  return get_world_bonus(etype);
}

/************************************************************************
  Returns the effect bonus for a player
************************************************************************/
int api_effects_player_bonus(Player *pplayer, const char *effect_type)
{
  enum effect_type etype = EFT_LAST;

  SCRIPT_CHECK_ARG_NIL(pplayer, 1, Player, 0);
  SCRIPT_CHECK_ARG_NIL(effect_type, 2, string, 0);

  etype = effect_type_from_str(effect_type);
  if (etype == EFT_LAST) {
    return 0;
  }
  return get_player_bonus(pplayer, etype);
}

/************************************************************************
  Returns the effect bonus at a city.
************************************************************************/
int api_effects_city_bonus(City *pcity, const char *effect_type)
{
  enum effect_type etype = EFT_LAST;

  SCRIPT_CHECK_ARG_NIL(pcity, 1, City, 0);
  SCRIPT_CHECK_ARG_NIL(effect_type, 2, string, 0);

  etype = effect_type_from_str(effect_type);
  if (etype == EFT_LAST) {
    return 0;
  }
  return get_city_bonus(pcity, etype);
}
