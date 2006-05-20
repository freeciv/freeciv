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

#include "plrhand.h"
#include "citytools.h"
#include "techtools.h"
#include "unittools.h"

#include "api_find.h"

#include "api_actions.h"


/**************************************************************************
  Create a new unit.
**************************************************************************/
Unit *api_actions_create_unit(Player *pplayer, Tile *ptile, Unit_Type *ptype,
		  	      int veteran_level, City *homecity,
			      int moves_left)
{
  return create_unit(pplayer, ptile, ptype, veteran_level,
		     homecity ? homecity->id : 0, moves_left);
}

/**************************************************************************
  Create a new city.
**************************************************************************/
void api_actions_create_city(Player *pplayer, Tile *ptile, const char *name)
{
  if (!name) {
    name = city_name_suggestion(pplayer, ptile);
  }
  create_city(pplayer, ptile, name);
}

/**************************************************************************
  Change pplayer's gold by amount.
**************************************************************************/
void api_actions_change_gold(Player *pplayer, int amount)
{
  pplayer->economic.gold += amount;
}

/**************************************************************************
  Give pplayer technology ptech.  Quietly returns A_NONE (zero) if 
  player already has this tech; otherwise returns the tech granted.
  Use NULL for ptech to grant a random tech.
**************************************************************************/
Tech_Type *api_actions_give_technology(Player *pplayer, Tech_Type *ptech)
{
  Tech_type_id id;

  if (ptech) {
    id = ptech->index;
  } else {
    if (get_player_research(pplayer)->researching == A_UNSET) {
      choose_random_tech(pplayer);
    }
    id = get_player_research(pplayer)->researching;
  }

  if (get_invention(pplayer, id) != TECH_KNOWN) {
    do_free_cost(pplayer, id);
    found_new_tech(pplayer, id, FALSE, TRUE);
    return api_find_tech_type(id);
  } else {
    return api_find_tech_type(A_NONE);
  }
}
