/********************************************************************** 
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
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "game.h"
#include "player.h"
#include "shared.h"
#include "tech.h"

#include "government.h"

/* TODO:
 * o Update government.ruleset to be untabulated (like the new
 *   units.ruleset).
 * o Update and turn on evaluation code currently disabled for
 *   regression testing purposes.
 * o Undo some other regression-testing constraints, and check
 *   exact government rules vs Civ1,Civ2.
 * o The clients display wrong upkeep icons in the city display,
 *   not sure what to do here.  (Does the new terrain-ruleset code
 *   have icons that could be used here?)
 * o When change government, should send more updated info to client
 *   (in particular unit upkeep icons are strange).
 * o Implement actual cost for unit gold upkeep.
 * o ai_manage_cities() in aicity.c assigns tech_want to A_CODE,
 *   A_REPUBLIC and A_MONARCHY presumably for government purposes...
 * o Move some functions from player.c to here.
 * o Update help system: meaning of Nonmil and FieldUnit unit flags;
 *   dynamic help on governments.
 * o Test the new government evaluation code (AI).
 *   [ It seems fine to me, although it favours Democracy very early
 *   on. This is because of the huge trade bonus. -SKi ]
 */

/*
 * WISHLIST:
 * o Implement the features needed for fundamentalism:
 *   - Units that require a government to be built, and have special
 *     upkeep values under that government. (Fanatics).
 *   - NO_UNHAPPY_CITIZENS flag for governments.
 *   - CONVERT_TITHES_TO_MONEY flag for governments (surplus only?).
 *   - A research penalty that is applied after all other modifiers.
 *   - A diplomatic penalty modifier when international incidents occur.
 *     (Spy places nuke in city, goes to war, etc).
 *     [ Is this one of those Civ II "features" best be ignored?  I am
 *     inclined to think so -SKi ]
 * o Features needed for CTP-style rules, just more trade, science and
 *   production modifiers.  (Just counting CTP governments, only
 *   basics).
 */

struct government *governments;

/***************************************************************
...
***************************************************************/
struct government *find_government_by_name(char *name)
{
  int i;

  for (i = 0; i < game.government_count; ++i) {
    if (mystrcasecmp(governments[i].name, name) == 0) {
      return &governments[i];
    }
  }
  return NULL;
}

/***************************************************************
...
***************************************************************/
struct government *get_government(int gov)
{
  assert(game.government_count > 0 && gov >= 0 && gov < game.government_count);
  return &governments[gov];
}

/***************************************************************
...
***************************************************************/
struct government *get_gov_pplayer(struct player *pplayer)
{
  int gov;
  
  assert(pplayer);
  gov = pplayer->government;
  assert(game.government_count > 0 && gov >= 0 && gov < game.government_count);
  return &governments[gov];
}

/***************************************************************
...
***************************************************************/
struct government *get_gov_iplayer(int player_num)
{
  int gov;
  
  assert(player_num >= 0 && player_num < game.nplayers);
  gov = game.players[player_num].government;
  assert(game.government_count > 0 && gov >= 0 && gov < game.government_count);
  return &governments[gov];
}

/***************************************************************
...
***************************************************************/
struct government *get_gov_pcity(struct city *pcity)
{
  assert(pcity);
  return get_gov_iplayer(pcity->owner);
}

/***************************************************************
  Return graphic offset for this government.  This may be
  called before governments setup, in which case returns 0.
***************************************************************/
int government_graphic(int gov)
{
  if (game.government_count > 0) {
    return get_government(gov)->graphic;
  } else {
    return 0;
  }
}

