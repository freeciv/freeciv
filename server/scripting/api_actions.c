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
#include "research.h"
#include "unittype.h"

/* server */
#include "barbarian.h"
#include "citytools.h"
#include "console.h" /* enum rfc_status */
#include "maphand.h"
#include "movement.h"
#include "plrhand.h"
#include "srv_main.h" /* game_was_started() */
#include "stdinhand.h"
#include "techtools.h"
#include "unittools.h"

/* server/scripting */
#include "api_find.h"
#include "script.h"
#include "script_signal.h"

#include "api_actions.h"


/**************************************************************************
  Unleash barbarians on a tile, for example from a hut
**************************************************************************/
bool api_actions_unleash_barbarians(Tile *ptile)
{
  SCRIPT_CHECK_ARG_NIL(ptile, 1, Tile, FALSE);
  return unleash_barbarians(ptile);
}

/**************************************************************************
  Place partisans for a player around a tile (normally around a city).
**************************************************************************/
void api_actions_place_partisans(Tile *ptile, Player *pplayer,
                                 int count, int sq_radius)
{
  SCRIPT_CHECK_ARG_NIL(ptile, 1, Tile);
  SCRIPT_CHECK_ARG_NIL(pplayer, 2, Player);
  SCRIPT_CHECK_ARG(0 <= sq_radius, 4, "radius must be positive");
  SCRIPT_CHECK(0 < num_role_units(L_PARTISAN), "no partisans in ruleset");
  return place_partisans(ptile, pplayer, count, sq_radius);
}

/**************************************************************************
  Create a new unit.
**************************************************************************/
Unit *api_actions_create_unit(Player *pplayer, Tile *ptile, Unit_Type *ptype,
                              int veteran_level, City *homecity,
                              int moves_left)
{
  return api_actions_create_unit_full(pplayer, ptile, ptype, veteran_level,
                                      homecity, moves_left, -1, NULL);
}

/**************************************************************************
  Create a new unit.
**************************************************************************/
Unit *api_actions_create_unit_full(Player *pplayer, Tile *ptile,
                                   Unit_Type *ptype,
                                   int veteran_level, City *homecity,
                                   int moves_left, int hp_left,
                                   Unit *ptransport)
{
  SCRIPT_CHECK_ARG_NIL(pplayer, 1, Player, NULL);
  SCRIPT_CHECK_ARG_NIL(ptile, 2, Tile, NULL);

  if (ptype == NULL
      || ptype < unit_type_array_first() || ptype > unit_type_array_last()) {
    return NULL;
  }

  if (ptransport) {
    /* Extensive check to see if transport and unit are compatible */
    int ret;
    struct unit *pvirt = create_unit_virtual(pplayer, NULL, ptype,
                                             veteran_level);
    pvirt->tile = ptile;
    pvirt->homecity = homecity ? homecity->id : 0;
    ret = can_unit_load(pvirt, ptransport);
    destroy_unit_virtual(pvirt);
    if (!ret) {
      log_error("create_unit_full: '%s' cannot transport '%s' here",
                utype_rule_name(unit_type(ptransport)),
                utype_rule_name(ptype));
      return NULL;
    }
  } else if (!can_exist_at_tile(ptype, ptile)) {
    log_error("create_unit_full: '%s' cannot exist at tile",
              utype_rule_name(ptype));
    return NULL;
  }

  return create_unit_full(pplayer, ptile, ptype, veteran_level,
                          homecity ? homecity->id : 0, moves_left,
                          hp_left, ptransport);
}

/**************************************************************************
  Create a new city.
**************************************************************************/
void api_actions_create_city(Player *pplayer, Tile *ptile, const char *name)
{
  SCRIPT_CHECK_ARG_NIL(pplayer, 1, Player);
  SCRIPT_CHECK_ARG_NIL(ptile, 2, Tile);

  if (!name || name[0] == '\0') {
    name = city_name_suggestion(pplayer, ptile);
  }
  create_city(pplayer, ptile, name);
}

/**************************************************************************
  Create a new player.
**************************************************************************/
Player *api_actions_create_player(const char *username,
                                  Nation_Type *pnation)
{
  struct player *pplayer = NULL;
  enum rfc_status status;
  char buf[128] = "";

  SCRIPT_CHECK_ARG_NIL(username, 1, string, NULL);

  if (game_was_started()) {
    status = create_command_newcomer(username, FALSE, pnation, &pplayer,
                                     buf, sizeof(buf));
  } else {
    status = create_command_pregame(username, FALSE, &pplayer,
                                    buf, sizeof(buf));
  }

  if (strlen(buf) > 0) {
    log_normal("%s", buf);
  }

  return pplayer;
}

/**************************************************************************
  Change pplayer's gold by amount.
**************************************************************************/
void api_actions_change_gold(Player *pplayer, int amount)
{
  SCRIPT_CHECK_ARG_NIL(pplayer, 1, Player);

  pplayer->economic.gold = MAX(0, pplayer->economic.gold + amount);
}

/**************************************************************************
  Give pplayer technology ptech.  Quietly returns A_NONE (zero) if 
  player already has this tech; otherwise returns the tech granted.
  Use NULL for ptech to grant a random tech.
  sends script signal "tech_researched" with the given reason
**************************************************************************/
Tech_Type *api_actions_give_technology(Player *pplayer, Tech_Type *ptech,
                                       const char *reason)
{
  Tech_type_id id;
  Tech_Type *result;

  SCRIPT_CHECK_ARG_NIL(pplayer, 1, Player, NULL);

  if (ptech) {
    id = advance_number(ptech);
  } else {
    if (player_research_get(pplayer)->researching == A_UNSET) {
      choose_random_tech(pplayer);
    }
    id = player_research_get(pplayer)->researching;
  }

  if (player_invention_state(pplayer, id) != TECH_KNOWN) {
    do_free_cost(pplayer, id);
    found_new_tech(pplayer, id, FALSE, TRUE);
    result = advance_by_number(id);
    script_signal_emit("tech_researched", 3,
                       API_TYPE_TECH_TYPE, result,
                       API_TYPE_PLAYER, pplayer,
                       API_TYPE_STRING, reason);
    return result;
  } else {
    return advance_by_number(A_NONE);
  }
}

/**************************************************************************
  Create a new base.
**************************************************************************/
void api_actions_create_base(Tile *ptile, const char *name, Player *pplayer)
{
  struct base_type *pbase;

  SCRIPT_CHECK_ARG_NIL(ptile, 1, Tile);

  if (!name) {
    return;
  }

  pbase = base_type_by_rule_name(name);

  if (pbase) {
    create_base(ptile, pbase, pplayer);
  }
}
