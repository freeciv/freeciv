/*****************************************************************************
 Freeciv - Copyright (C) 2005 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
*****************************************************************************/

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

/* server */
#include "aiiface.h"
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
#include "script_server.h"

#include "api_server_edit.h"


/*****************************************************************************
  Unleash barbarians on a tile, for example from a hut
*****************************************************************************/
bool api_edit_unleash_barbarians(lua_State *L, Tile *ptile)
{
  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_ARG_NIL(L, ptile, 2, Tile, FALSE);

  return unleash_barbarians(ptile);
}

/*****************************************************************************
  Place partisans for a player around a tile (normally around a city).
*****************************************************************************/
void api_edit_place_partisans(lua_State *L, Tile *ptile, Player *pplayer,
                              int count, int sq_radius)
{
  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_ARG_NIL(L, ptile, 2, Tile);
  LUASCRIPT_CHECK_ARG_NIL(L, pplayer, 3, Player);
  LUASCRIPT_CHECK_ARG(L, 0 <= sq_radius, 5, "radius must be positive");
  LUASCRIPT_CHECK(L, 0 < num_role_units(L_PARTISAN),
                  "no partisans in ruleset");

  return place_partisans(ptile, pplayer, count, sq_radius);
}

/*****************************************************************************
  Create a new unit.
*****************************************************************************/
Unit *api_edit_create_unit(lua_State *L, Player *pplayer, Tile *ptile,
                           Unit_Type *ptype, int veteran_level,
                           City *homecity, int moves_left)
{
  return api_edit_create_unit_full(L, pplayer, ptile, ptype, veteran_level,
                                      homecity, moves_left, -1, NULL);
}

/*****************************************************************************
  Create a new unit.
*****************************************************************************/
Unit *api_edit_create_unit_full(lua_State *L, Player *pplayer, Tile *ptile,
                                Unit_Type *ptype, int veteran_level,
                                City *homecity, int moves_left, int hp_left,
                                Unit *ptransport)
{
  struct fc_lua *fcl;

  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_ARG_NIL(L, pplayer, 2, Player, NULL);
  LUASCRIPT_CHECK_ARG_NIL(L, ptile, 3, Tile, NULL);

  fcl = luascript_get_fcl(L);

  LUASCRIPT_CHECK(L, fcl != NULL, "Undefined freeciv lua state!", NULL);

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
      luascript_log(fcl, LOG_ERROR, "create_unit_full: '%s' cannot transport "
                                    "'%s' here",
                    utype_rule_name(unit_type(ptransport)),
                    utype_rule_name(ptype));
      return NULL;
    }
  } else if (!can_exist_at_tile(ptype, ptile)) {
    luascript_log(fcl, LOG_ERROR, "create_unit_full: '%s' cannot exist at "
                                  "tile", utype_rule_name(ptype));
    return NULL;
  }

  return create_unit_full(pplayer, ptile, ptype, veteran_level,
                          homecity ? homecity->id : 0, moves_left,
                          hp_left, ptransport);
}

/*****************************************************************************
  Teleport unit to destination tile
*****************************************************************************/
bool api_edit_unit_teleport(lua_State *L, Unit *punit, Tile *dest)
{
  bool alive;

  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, punit, 2, Unit, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, dest, 3, Tile, FALSE);

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

/*****************************************************************************
  Change unit orientation
*****************************************************************************/
void api_edit_unit_turn(lua_State *L, Unit *punit, Direction dir)
{
  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_ARG_NIL(L, punit, 2, Unit);
 
  if (direction8_is_valid(dir)) {
    punit->facing = dir;

    send_unit_info_to_onlookers(NULL, punit, unit_tile(punit), FALSE);
  } else {
    log_error("Illegal direction %d for unit from lua script", dir);
  }
}

/*****************************************************************************
  Create a new city.
*****************************************************************************/
void api_edit_create_city(lua_State *L, Player *pplayer, Tile *ptile,
                          const char *name)
{
  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_ARG_NIL(L, pplayer, 2, Player);
  LUASCRIPT_CHECK_ARG_NIL(L, ptile, 3, Tile);

  if (!name || name[0] == '\0') {
    name = city_name_suggestion(pplayer, ptile);
  }
  create_city(pplayer, ptile, name);
}

/*****************************************************************************
  Create a new player.
*****************************************************************************/
Player *api_edit_create_player(lua_State *L, const char *username,
                               Nation_Type *pnation)
{
  struct player *pplayer = NULL;
  char buf[128] = "";
  struct fc_lua *fcl;

  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_ARG_NIL(L, username, 2, string, NULL);

  fcl = luascript_get_fcl(L);

  LUASCRIPT_CHECK(L, fcl != NULL, "Undefined freeciv lua state!", NULL);

  if (game_was_started()) {
    create_command_newcomer(username, default_ai_type_name(), FALSE, pnation,
                            &pplayer, buf, sizeof(buf));
  } else {
    create_command_pregame(username, default_ai_type_name(), FALSE, &pplayer,
                           buf, sizeof(buf));
  }

  if (strlen(buf) > 0) {
    luascript_log(fcl, LOG_NORMAL, "%s", buf);
  }

  return pplayer;
}

/*****************************************************************************
  Change pplayer's gold by amount.
*****************************************************************************/
void api_edit_change_gold(lua_State *L, Player *pplayer, int amount)
{
  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_ARG_NIL(L, pplayer, 2, Player);

  pplayer->economic.gold = MAX(0, pplayer->economic.gold + amount);
}

/*****************************************************************************
  Give pplayer technology ptech. Quietly returns A_NONE (zero) if
  player already has this tech; otherwise returns the tech granted.
  Use NULL for ptech to grant a random tech.
  sends script signal "tech_researched" with the given reason
*****************************************************************************/
Tech_Type *api_edit_give_technology(lua_State *L, Player *pplayer,
                                    Tech_Type *ptech, const char *reason)
{
  Tech_type_id id;
  Tech_Type *result;

  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_ARG_NIL(L, pplayer, 2, Player, NULL);

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

/*****************************************************************************
  Create a new base.
*****************************************************************************/
void api_edit_create_base(lua_State *L, Tile *ptile, const char *name,
                          Player *pplayer)
{
  struct base_type *pbase;

  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_ARG_NIL(L, ptile, 2, Tile);

  if (!name) {
    return;
  }

  pbase = base_type_by_rule_name(name);

  if (pbase) {
    create_base(ptile, pbase, pplayer);
  }
}

/*****************************************************************************
  Global climate change.
*****************************************************************************/
void api_edit_climate_change(lua_State *L, enum climate_change_type type,
                             int effect)
{
  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_ARG(L, type == CLIMATE_CHANGE_GLOBAL_WARMING
                      || type == CLIMATE_CHANGE_NUCLEAR_WINTER,
                      2, "invalid climate change type");
  LUASCRIPT_CHECK_ARG(L, effect > 0, 3, "effect must be greater than zero");

  climate_change(type == CLIMATE_CHANGE_GLOBAL_WARMING, effect);
}

/*****************************************************************************
  Provoke a civil war.
*****************************************************************************/
Player *api_edit_civil_war(lua_State *L, Player *pplayer, int probability)
{
  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_ARG_NIL(L, pplayer, 2, Player, NULL);
  LUASCRIPT_CHECK_ARG(L, probability >= 0 && probability <= 100,
                      3, "must be a percentage", NULL);

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

/*****************************************************************************
  Make player winner of the scenario
*****************************************************************************/
void api_edit_player_victory(lua_State *L, Player *pplayer)
{
  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_SELF(L, pplayer);

  player_status_add(pplayer, PSTATUS_WINNER);
}

/*****************************************************************************
  Move a unit.
*****************************************************************************/
bool api_edit_unit_move(lua_State *L, Unit *punit, Tile *ptile,
                        int movecost)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, punit, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, ptile, 3, Tile, FALSE);
  LUASCRIPT_CHECK_ARG(L, movecost >= 0, 4, "Negative move cost!", FALSE);

  return unit_move(punit, ptile, movecost);
}
