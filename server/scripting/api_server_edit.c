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
#include <fc_config.h>
#endif

/* utility */
#include "rand.h"

/* common */
#include "research.h"
#include "unittype.h"

/* common/scriptcore */
#include "api_game_find.h"
#include "luascript.h"
#include "luascript_signal.h"
#include "luascript_types.h"

/* server */
#include "aiiface.h"
#include "barbarian.h"
#include "citytools.h"
#include "maphand.h"
#include "movement.h"
#include "plrhand.h"
#include "score.h"
#include "srv_main.h" /* game_was_started() */
#include "stdinhand.h"
#include "techtools.h"
#include "unittools.h"

/* server/scripting */
#include "script_server.h"

#include "api_server_edit.h"


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
  Global climate change.
**************************************************************************/
void api_actions_climate_change(enum climate_change_type type, int effect)
{
  SCRIPT_CHECK_ARG(type == CLIMATE_CHANGE_GLOBAL_WARMING
                   || type == CLIMATE_CHANGE_NUCLEAR_WINTER,
                   1, "invalid climate change type");
  SCRIPT_CHECK_ARG(effect > 0, 3, "effect must be greater than zero");
  climate_change(type == CLIMATE_CHANGE_GLOBAL_WARMING, effect);
}

/**************************************************************************
  Provoke a civil war.
**************************************************************************/
Player *api_actions_civil_war(Player *pplayer, int probability)
{
  SCRIPT_CHECK_ARG_NIL(pplayer, 1, Player, NULL);
  SCRIPT_CHECK_ARG(probability >= 0 && probability <= 100,
                   2, "must be a percentage", NULL);

  if (!civil_war_possible(pplayer, FALSE, FALSE)) {
    return NULL;
  }

  if (probability == 0) {
    /* Calculate chance with normal rules */
    if (!civil_war_triggered(pplayer)) {
      return NULL;
    }
  } else {
    /* Fixed chance specified by script */
    if (fc_rand(100) >= probability) {
      return NULL;
    }
  }

  return civil_war(pplayer);
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
    struct unit *pvirt = unit_virtual_create(pplayer, NULL, ptype,
                                             veteran_level);
    unit_tile_set(pvirt, ptile);
    pvirt->homecity = homecity ? homecity->id : 0;
    ret = can_unit_load(pvirt, ptransport);
    unit_virtual_destroy(pvirt);
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
  char buf[128] = "";

  SCRIPT_CHECK_ARG_NIL(username, 1, string, NULL);

  if (game_was_started()) {
    create_command_newcomer(username, FC_AI_DEFAULT_NAME,
                            FALSE, pnation, &pplayer,
                            buf, sizeof(buf));
  } else {
    create_command_pregame(username, FC_AI_DEFAULT_NAME,
                           FALSE, &pplayer,
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
    script_server_signal_emit("tech_researched", 3,
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

/**************************************************************************
  Return the value for the fcdb setting 'type'.
**************************************************************************/
void api_utilities_cmd_reply(int cmdid, struct connection *caller,
                             int rfc_status, const char *msg)
{
  if (cmdid != CMD_FCDB) {
    log_error("Use of forbitten command id from lua script: %s (%d).",
              command_name_by_number(cmdid), cmdid);
    return;
  }

  cmd_reply(cmdid, caller, rfc_status, "%s", msg);
}

/**************************************************************************
  Return the civilization score (total) for player
**************************************************************************/
int api_methods_player_civilization_score(Player *pplayer)
{
  SCRIPT_CHECK_SELF(pplayer, 0);
  return get_civ_score(pplayer);
}

/**************************************************************************
  Make player winner of the scenario
**************************************************************************/
void api_methods_player_victory(Player *pplayer)
{
  SCRIPT_CHECK_SELF(pplayer);
  player_status_add(pplayer, PSTATUS_WINNER);
}

/**************************************************************************
  Teleport unit to destination tile
**************************************************************************/
bool api_methods_unit_teleport(Unit *punit, Tile *dest)
{
  bool alive;

  /* Teleport first so destination is revealed even if unit dies */
  alive = unit_move(punit, dest, 0);
  if (alive) {
    struct player *owner = unit_owner(punit);
    struct city *pcity = tile_city(dest);

    if (!can_unit_exist_at_tile(punit, dest)) {
      wipe_unit(punit, ULR_NONNATIVE_TERR);
      return FALSE;
    }
    if (is_non_allied_unit_tile(dest, owner)
        || (pcity && !pplayers_allied(city_owner(pcity), owner))) {
      wipe_unit(punit, ULR_STACK_CONFLICT);
      return FALSE;
    }
  }

  return alive;
}

/**************************************************************************
  Change unit orientation
**************************************************************************/
void api_methods_unit_turn(Unit *punit, Direction dir)
{
  if (direction8_is_valid(dir)) {
    punit->facing = dir;

    send_unit_info_to_onlookers(NULL, punit, unit_tile(punit), FALSE);
  } else {
    log_error("Illegal direction %d for unit from lua script", dir);
  }
}
