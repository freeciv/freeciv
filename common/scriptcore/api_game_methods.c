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
#include "deprecations.h"

/* common */
#include "achievements.h"
#include "actions.h"
#include "calendar.h"
#include "citizens.h"
#include "culture.h"
#include "featured_text.h"
#include "game.h"
#include "government.h"
#include "improvement.h"
#include "map.h"
#include "movement.h"
#include "nation.h"
#include "research.h"
#include "tech.h"
#include "terrain.h"
#include "tile.h"
#include "unitlist.h"
#include "unittype.h"

/* common/scriptcore */
#include "luascript.h"

#include "api_game_methods.h"


/**********************************************************************//**
  Return the current turn.
**************************************************************************/
int api_methods_game_turn(lua_State *L)
{
  LUASCRIPT_CHECK_STATE(L, 0);

  return game.info.turn;
}

/**********************************************************************//**
  Return the current year.
**************************************************************************/
int api_methods_game_year(lua_State *L)
{
  LUASCRIPT_CHECK_STATE(L, 0);

  return game.info.year;
}

/**********************************************************************//**
  Return the current year fragment.
**************************************************************************/
int api_methods_game_year_fragment(lua_State *L)
{
  LUASCRIPT_CHECK_STATE(L, 0);

  return game.info.fragment_count;
}

/**********************************************************************//**
  Return textual representation of the current calendar time.
**************************************************************************/
const char *api_methods_game_year_text(lua_State *L)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);

  return calendar_text();
}

/**********************************************************************//**
  Return the current turn, as if real turns started from 0.
**************************************************************************/
int api_methods_game_turn_deprecated(lua_State *L)
{
  LUASCRIPT_CHECK_STATE(L, 0);

  log_deprecation("Deprecated: lua construct \"game:turn\", deprecated since \"3.0\", used. "
                  "Use \"game:current_turn\" instead.");

  if (game.info.turn > 0) {
    return game.info.turn - 1;
  }

  return game.info.turn;
}

/**********************************************************************//**
  Return name of the current ruleset.
**************************************************************************/
const char *api_methods_game_rulesetdir(lua_State *L)
{
  return game.server.rulesetdir;
}

/**********************************************************************//**
  Return name of the current ruleset.
**************************************************************************/
const char *api_methods_game_ruleset_name(lua_State *L)
{
  return game.control.name;
}

/**********************************************************************//**
  Return name of the current tech cost style
**************************************************************************/
const char *api_methods_tech_cost_style(lua_State *L)
{
  return tech_cost_style_name(game.info.tech_cost_style);
}

/**********************************************************************//**
  Return name of the current tech leakage style
**************************************************************************/
const char *api_methods_tech_leakage_style(lua_State *L)
{
  return tech_leakage_style_name(game.info.tech_leakage);
}

/**********************************************************************//**
  Return TRUE if pbuilding is a wonder.
**************************************************************************/
bool api_methods_building_type_is_wonder(lua_State *L,
                                         Building_Type *pbuilding)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, pbuilding, FALSE);

  return is_wonder(pbuilding);
}

/**********************************************************************//**
  Return TRUE if pbuilding is a great wonder.
**************************************************************************/
bool api_methods_building_type_is_great_wonder(lua_State *L,
                                               Building_Type *pbuilding)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, pbuilding, FALSE);

  return is_great_wonder(pbuilding);
}

/**********************************************************************//**
  Return TRUE if pbuilding is a small wonder.
**************************************************************************/
bool api_methods_building_type_is_small_wonder(lua_State *L,
                                               Building_Type *pbuilding)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, pbuilding, FALSE);

  return is_small_wonder(pbuilding);
}

/**********************************************************************//**
  Return TRUE if pbuilding is a building.
**************************************************************************/
bool api_methods_building_type_is_improvement(lua_State *L,
                                              Building_Type *pbuilding)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, pbuilding, FALSE);

  return is_improvement(pbuilding);
}

/**********************************************************************//**
  Return rule name for Building_Type
**************************************************************************/
const char *api_methods_building_type_rule_name(lua_State *L,
                                                Building_Type *pbuilding)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pbuilding, nullptr);

  return improvement_rule_name(pbuilding);
}

/**********************************************************************//**
  Return translated name for Building_Type
**************************************************************************/
const char
  *api_methods_building_type_name_translation(lua_State *L,
                                              Building_Type *pbuilding)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pbuilding, nullptr);

  return improvement_name_translation(pbuilding);
}

/**********************************************************************//**
  Return TRUE iff city has building
**************************************************************************/
bool api_methods_city_has_building(lua_State *L, City *pcity,
                                   Building_Type *building)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, pcity, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, building, 3, Building_Type, FALSE);

  return city_has_building(pcity, building);
}

/**********************************************************************//**
  Return the square raduis of the city map.
**************************************************************************/
int api_methods_city_map_sq_radius(lua_State *L, City *pcity)
{
  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_SELF(L, pcity, 0);

  return city_map_radius_sq_get(pcity);
}

/**********************************************************************//**
  Return the size of the city.
**************************************************************************/
int api_methods_city_size_get(lua_State *L, City *pcity)
{
  LUASCRIPT_CHECK_STATE(L, 1);
  LUASCRIPT_CHECK_SELF(L, pcity, 1);

  return city_size_get(pcity);
}

/**********************************************************************//**
  Return the tile of the city.
**************************************************************************/
Tile *api_methods_city_tile_get(lua_State *L, City *pcity)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pcity, nullptr);

  return pcity->tile;
}

/**********************************************************************//**
  How much city inspires partisans for a player.
**************************************************************************/
int api_methods_city_inspire_partisans(lua_State *L, City *self,
                                       Player *inspirer)
{
  bool inspired = FALSE;

  if (!game.info.citizen_nationality) {
    if (self->original == inspirer) {
      inspired = TRUE;
    }
  } else {
    if (game.info.citizen_partisans_pct > 0) {
      if (is_server()) {
        citizens own = citizens_nation_get(self, inspirer->slot);
        citizens total = city_size_get(self);

        if (total > 0 && (own * 100 / total) >= game.info.citizen_partisans_pct) {
          inspired = TRUE;
        }
      }
      /* else is_client() -> don't consider inspired by default. */
    } else if (self->original == inspirer) {
      inspired = TRUE;
    }
  }

  if (inspired) {
    /* Cannot use get_city_bonus() as it would use city's current owner
     * instead of inspirer. */
    return get_target_bonus_effects(nullptr,
                                    &(const struct req_context) {
                                      .player = inspirer,
                                      .city = self,
                                      .tile = city_tile(self),
                                    },
                                    nullptr, EFT_INSPIRE_PARTISANS);
  }

  return 0;
}

/**********************************************************************//**
  How much culture city has?
**************************************************************************/
int api_methods_city_culture_get(lua_State *L, City *pcity)
{
  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_SELF(L, pcity, 0);

  return city_culture(pcity);
}

/**********************************************************************//**
Returns rule name of the counter.
**************************************************************************/
const char *api_methods_counter_rule_name(lua_State *L, Counter *c)
{
  LUASCRIPT_CHECK_ARG_NIL(L, c, 2, Counter, nullptr);

  return counter_rule_name(c);
}

/**********************************************************************//**
Returns translation name of the counter.
**************************************************************************/
const char *api_methods_counter_name_translation(lua_State *L, Counter *c)
{
  LUASCRIPT_CHECK_ARG_NIL(L, c, 2, Counter, nullptr);

  return counter_name_translation(c);
}

/**********************************************************************//**
  Obtain city's counter value
**************************************************************************/
int api_methods_counter_city_get(lua_State *L, Counter *c, City *city)
{
  LUASCRIPT_CHECK_ARG_NIL(L, city, 3, City, -1);

  return city->counter_values[counter_index(c)];
}

/**********************************************************************//**
  Return TRUE iff city happy
**************************************************************************/
bool api_methods_is_city_happy(lua_State *L, City *pcity)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, pcity, FALSE);

  /* Note: if clients ever have virtual cities or sth, needs amending */
  return is_server() ? city_happy(pcity) : pcity->client.happy;
}

/**********************************************************************//**
  Return TRUE iff city is unhappy
**************************************************************************/
bool api_methods_is_city_unhappy(lua_State *L, City *pcity)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, pcity, FALSE);

  /* Note: if clients ever have virtual cities or sth, needs amending */
  return is_server() ? city_unhappy(pcity) : pcity->client.unhappy;
}

/**********************************************************************//**
  Return TRUE iff city is celebrating
**************************************************************************/
bool api_methods_is_city_celebrating(lua_State *L, City *pcity)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, pcity, FALSE);

  return city_celebrating(pcity);
}

/**********************************************************************//**
  Return TRUE iff city is government center
**************************************************************************/
bool api_methods_is_gov_center(lua_State *L, City *pcity)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, pcity, FALSE);

  return is_gov_center(pcity);
}

/**********************************************************************//**
  Return TRUE if city is capital
**************************************************************************/
bool api_methods_is_capital(lua_State *L, City *pcity)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, pcity, FALSE);

  return is_capital(pcity);
}

/**********************************************************************//**
  Return TRUE if city is primary capital
**************************************************************************/
bool api_methods_is_primary_capital(lua_State *L, City *pcity)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, pcity, FALSE);

  return pcity->capital == CAPITAL_PRIMARY;
}

/**********************************************************************//**
  Return number of citizens of the given nationality in a city
**************************************************************************/
int api_methods_city_nationality_citizens(lua_State *L, City *pcity,
                                          Player *nationality)
{
  LUASCRIPT_CHECK_STATE(L, -1);
  LUASCRIPT_CHECK_SELF(L, pcity, -1);

  return citizens_nation_get(pcity, nationality->slot);
}

/**********************************************************************//**
  Return number of specialists of type s working in pcity.
  If no s is specified, return number of citizens employed as specialists.
**************************************************************************/
int api_methods_city_num_specialists(lua_State *L, City *pcity,
                                     Specialist *s)
{
  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_SELF(L, pcity, 0);

  if (nullptr != s) {
    return pcity->specialists[specialist_index(s)];
  } else {
    return city_specialists(pcity);
  }
}

/**********************************************************************//**
  Return if pcity agrees with reqs of specialist s
**************************************************************************/
bool api_methods_city_can_employ(lua_State *L, City *pcity, Specialist *s)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, pcity, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, s, 3, Specialist, FALSE);

  return city_can_use_specialist(pcity, specialist_index(s));
}

/**********************************************************************//**
   Return rule name for Government
**************************************************************************/
const char *api_methods_government_rule_name(lua_State *L,
                                             Government *pgovernment)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pgovernment, nullptr);

  return government_rule_name(pgovernment);
}

/**********************************************************************//**
  Return translated name for Government
**************************************************************************/
const char *api_methods_government_name_translation(lua_State *L,
                                                    Government *pgovernment)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pgovernment, nullptr);

  return government_name_translation(pgovernment);
}

/**********************************************************************//**
  Return rule name for Nation_Type
**************************************************************************/
const char *api_methods_nation_type_rule_name(lua_State *L,
                                              Nation_Type *pnation)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pnation, nullptr);

  return nation_rule_name(pnation);
}

/**********************************************************************//**
  Return translated adjective for Nation_Type
**************************************************************************/
const char *api_methods_nation_type_name_translation(lua_State *L,
                                                     Nation_Type *pnation)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pnation, nullptr);

  return nation_adjective_translation(pnation);
}

/**********************************************************************//**
  Return translated plural noun for Nation_Type
**************************************************************************/
const char *api_methods_nation_type_plural_translation(lua_State *L,
                                                       Nation_Type *pnation)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pnation, nullptr);

  return nation_plural_translation(pnation);
}

/**********************************************************************//**
  Return gui type string of the controlling connection.
**************************************************************************/
const char *api_methods_player_controlling_gui(lua_State *L,
                                               Player *pplayer)
{
  struct connection *conn = nullptr;

  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pplayer, nullptr);

  conn_list_iterate(pplayer->connections, pconn) {
    if (!pconn->observer) {
      conn = pconn;
      break;
    }
  } conn_list_iterate_end;

  if (conn == nullptr) {
    return "None";
  }

  return gui_type_name(conn->client_gui);
}

/**********************************************************************//**
  Return TRUE iff player has wonder
**************************************************************************/
bool api_methods_player_has_wonder(lua_State *L, Player *pplayer,
                                   Building_Type *building)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, pplayer, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, building, 3, Building_Type, FALSE);

  return wonder_is_built(pplayer, building);
}

/**********************************************************************//**
  Return player number
**************************************************************************/
int api_methods_player_number(lua_State *L, Player *pplayer)
{
  LUASCRIPT_CHECK_STATE(L, -1);
  LUASCRIPT_CHECK_SELF(L, pplayer, -1);

  return player_number(pplayer);
}

/**********************************************************************//**
  Return the number of cities pplayer has.
**************************************************************************/
int api_methods_player_num_cities(lua_State *L, Player *pplayer)
{
  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_SELF(L, pplayer, 0);

  return city_list_size(pplayer->cities);
}

/**********************************************************************//**
  Return the number of units pplayer has.
**************************************************************************/
int api_methods_player_num_units(lua_State *L, Player *pplayer)
{
  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_SELF(L, pplayer, 0);

  return unit_list_size(pplayer->units);
}

/**********************************************************************//**
  Return gold for Player
**************************************************************************/
int api_methods_player_gold(lua_State *L, Player *pplayer)
{
  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_SELF(L, pplayer, 0);

  return pplayer->economic.gold;
}

/**********************************************************************//**
  Return amount of infrapoints Player has
**************************************************************************/
int api_methods_player_infrapoints(lua_State *L, Player *pplayer)
{
  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_SELF(L, pplayer, 0);

  return pplayer->economic.infra_points;
}

/**********************************************************************//**
  Return TRUE if Player knows advance ptech.
**************************************************************************/
bool api_methods_player_knows_tech(lua_State *L, Player *pplayer,
                                   Tech_Type *ptech)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, pplayer, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, ptech, 3, Tech_Type, FALSE);

  return research_invention_state(research_get(pplayer),
                                  advance_number(ptech)) == TECH_KNOWN;
}

/**********************************************************************//**
  Return TRUE iff pplayer can research ptech now (but does not know it).
  In client, considers known information only.
**************************************************************************/
bool api_method_player_can_research(lua_State *L, Player *pplayer,
                                    Tech_Type *ptech)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, pplayer, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, ptech, 3, Tech_Type, FALSE);

  return TECH_PREREQS_KNOWN
    == research_invention_state(research_get(pplayer),
                                advance_number(ptech));
}

/**********************************************************************//**
  Return current ptech research (or losing) cost for pplayer
  pplayer is optional, simplified calculation occurs without it
  that does not take into account "Tech_Cost_Factor" effect.
  In client, calculates a value summing only known leakage
  sources that may be different from the actual one given
  by :researching_cost() method. For techs not currently
  researchable, often can't calculate an actual value even
  on server (bases on current potential leakage sources, and,
  for "CivI|II" style, current pplayer's number of known techs or,
  if pplayer is absent, minimal number of techs to get to ptech).
**************************************************************************/
int api_methods_player_tech_cost(lua_State *L, Player *pplayer,
                                 Tech_Type *ptech)
{
  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_ARG_NIL(L, ptech, 3, Tech_Type, 0);

  if (!pplayer && TECH_COST_CIV1CIV2 == game.info.tech_cost_style) {
    /* Avoid getting error messages and return at least something */
    return ptech->cost * (double) game.info.sciencebox / 100.0;
  }
  return
    research_total_bulbs_required(pplayer
                                  ? research_get(pplayer) : nullptr,
                                  advance_index(ptech), TRUE);
}

/**********************************************************************//**
  Returns current research target for a player.
  If the player researches a future tech, returns a string with
  its translated name.
  If the target is unset or unknown, returns nil.
  In clients, an unknown value usually is nil but may be different
  if an embassy has been lost during the session (see OSDN#45076)
**************************************************************************/
lua_Object
api_methods_player_researching(lua_State *L, Player *pplayer)
{
  const struct research *presearch;
  int rr;

  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_SELF(L, pplayer, 0);
  presearch = research_get(pplayer);
  LUASCRIPT_CHECK(L, presearch, "player's research not set", 0);

  rr = presearch->researching;

  switch (rr) {
  case A_FUTURE:
    lua_pushstring(L, research_advance_name_translation(presearch, rr));
    break;
  case A_UNSET:
  case A_UNKNOWN:
    lua_pushnil(L);
    break;
  default:
    /* A regular tech */
    fc_assert(rr >= A_FIRST && rr <= A_LAST);
    tolua_pushusertype(L, advance_by_number(rr), "Tech_Type");
  }

  return lua_gettop(L);
}

/**********************************************************************//**
  Number of bulbs on the research stock of pplayer
  In clients, unknown value is initialized with 0 but can be different
  if an embassy has been lost during the session (see OSDN#45076)
**************************************************************************/
int api_methods_player_bulbs(lua_State *L, Player *pplayer)
{
  const struct research *presearch;

  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_SELF(L, pplayer, 0);
  presearch = research_get(pplayer);
  LUASCRIPT_CHECK(L, presearch, "player's research not set", 0);

  return presearch->bulbs_researched;
}

/**********************************************************************//**
  Total cost of pplayer's current research target.
  In clients, unknown value is initialized with 0 but can be different
  if an embassy has been lost during the session (see OSDN#45076)
**************************************************************************/
int api_methods_player_research_cost(lua_State *L, Player *pplayer)
{
  const struct research *presearch;

  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_SELF(L, pplayer, 0);
  presearch = research_get(pplayer);
  LUASCRIPT_CHECK(L, presearch, "player's research not set", 0);

  return is_server()
   ? research_total_bulbs_required(presearch, presearch->researching, FALSE)
   : presearch->client.researching_cost;
}

/**********************************************************************//**
  Number of future techs known to pplayer
  In clients, unknown value is initialized with 0 but can be different
  if an embassy has been lost during the session (see OSDN#45076)
**************************************************************************/
int api_methods_player_future(lua_State *L, Player *pplayer)
{
  const struct research *presearch;

  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_SELF(L, pplayer, 0);
  presearch = research_get(pplayer);
  LUASCRIPT_CHECK(L, presearch, "player's research not set", 0);

  return presearch->future_tech;
}

/**********************************************************************//**
  How much culture player has?
**************************************************************************/
int api_methods_player_culture_get(lua_State *L, Player *pplayer)
{
  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_SELF(L, pplayer, 0);

  return player_culture(pplayer);
}

/**********************************************************************//**
  Does player have flag set?
**************************************************************************/
bool api_methods_player_has_flag(lua_State *L, Player *pplayer,
                                 const char *flag)
{
  enum plr_flag_id flag_val;

  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_SELF(L, pplayer, 0);

  flag_val = plr_flag_id_by_name(flag, fc_strcasecmp);

  if (plr_flag_id_is_valid(flag_val)) {
    return player_has_flag(pplayer, flag_val);
  }

  return FALSE;
}

/**********************************************************************//**
  Return a unit type the player potentially can upgrade utype to,
  or nil if the player can't upgrade it
**************************************************************************/
Unit_Type *api_methods_player_can_upgrade(lua_State *L, Player *pplayer,
                                          Unit_Type *utype)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pplayer, nullptr);
  LUASCRIPT_CHECK_ARG_NIL(L, utype, 3, Unit_Type, nullptr);

  return (Unit_Type *)can_upgrade_unittype(pplayer, utype);
}

/**********************************************************************//**
  Certain tests if pplayer generally can build utype units, maybe after
  building a required improvement. Does not consider obsoletion.
**************************************************************************/
bool api_methods_player_can_build_unit_direct(lua_State *L, Player *pplayer,
                                              Unit_Type *utype)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, pplayer, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, utype, 3, Unit_Type, FALSE);

  return can_player_build_unit_direct(pplayer, utype, TRUE);
}

/**********************************************************************//**
  Certain tests if pplayer generally can build itype buildings.
**************************************************************************/
bool api_methods_player_can_build_impr_direct(lua_State *L, Player *pplayer,
                                              Building_Type *itype)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, pplayer, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, itype, 3, Building_Type, FALSE);

  return can_player_build_improvement_direct(pplayer, itype);
}

/**********************************************************************//**
  Return if pplayer agrees with Player+-ranged reqs of specialist s
**************************************************************************/
bool api_methods_player_can_employ(lua_State *L, Player *pplayer,
                                   Specialist *s)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, pplayer, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, s, 3, Specialist, FALSE);

  return
    are_reqs_active_ranges(REQ_RANGE_PLAYER, REQ_RANGE_WORLD,
                              &(const struct req_context) {
                                .player = pplayer,
                              }, nullptr, &s->reqs, RPT_POSSIBLE);
}

/**********************************************************************//**
  Find player's primary capital, if known
**************************************************************************/
City *api_methods_player_primary_capital(lua_State *L, Player *pplayer)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pplayer, nullptr);

  return player_primary_capital(pplayer);
}

/**********************************************************************//**
  Return diplomatic state between the players.
**************************************************************************/
const char *api_methods_get_diplstate(lua_State *L, Player *pplayer1,
                                      Player *pplayer2)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pplayer1, nullptr);
  LUASCRIPT_CHECK_ARG_NIL(L, pplayer2, 3, Player, nullptr);

  if (pplayer1 == pplayer2) {
    return "Self";
  }

  return Qn_(diplstate_type_name(player_diplstate_get(pplayer1, pplayer2)->type));
}

/**********************************************************************//**
  Return whether player has an embassy with target player.
**************************************************************************/
bool api_methods_player_has_embassy(lua_State *L, Player *pplayer,
                                    Player *target)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, pplayer, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, target, 3, Player, FALSE);

  return player_has_embassy(pplayer, target);
}

/**********************************************************************//**
  Return whether player's team has an embassy with target player.
**************************************************************************/
bool api_methods_player_team_has_embassy(lua_State *L, Player *pplayer,
                                         Player *target)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, pplayer, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, target, 3, Player, FALSE);

  return team_has_embassy(pplayer->team, target);
}

/**********************************************************************//**
  Return if a unit can upgrade considering where it is now.
  If is_free is FALSE, considers local city and the owner's treasury.
**************************************************************************/
bool api_methods_unit_can_upgrade(lua_State *L, Unit *punit, bool is_free)
{

  return UU_OK == unit_upgrade_test(&(wld.map), punit, is_free);
}

/**********************************************************************//**
  Return a name of the problem unit may have being transformed to ptype
  where it is now, or nil if no problem seems to exist.
**************************************************************************/
const char *api_methods_unit_transform_problem(lua_State *L, Unit *punit,
                                               Unit_Type *ptype)
{
  enum unit_upgrade_result uu;

  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, punit, nullptr);
  LUASCRIPT_CHECK_ARG_NIL(L, ptype, 3, Unit_Type, nullptr);

  uu = unit_transform_result(&(wld.map), punit, ptype);
  switch (uu) {
  case UU_OK:
    return nullptr;
  case UU_NOT_ENOUGH_ROOM:
    return "cargo";
  case UU_UNSUITABLE_TRANSPORT:
    return "transport";
  case UU_NOT_TERRAIN:
    return "terrain";
  case UU_NOT_ACTIVITY:
    return "activity";
  case UU_NO_UNITTYPE:
  case UU_NO_MONEY:
  case UU_NOT_IN_CITY:
  case UU_NOT_CITY_OWNER:
    /* should not get here */
    break;
  }

  fc_assert_msg(FALSE, "Unexpected unit transform result %i", uu);

  return "\?";
}

/**********************************************************************//**
  Whether player currently sees the unit
**************************************************************************/
bool api_methods_unit_seen(lua_State *L, Unit *self, Player *watcher)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, self, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, watcher, 3, Player, FALSE);

  return can_player_see_unit(watcher, self);
}

/**********************************************************************//**
  Return TRUE if players share research.
**************************************************************************/
bool api_methods_player_shares_research(lua_State *L, Player *pplayer,
                                        Player *aplayer)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, pplayer, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, aplayer, 3, Player, FALSE);

  return research_get(pplayer) == research_get(aplayer);
}

/**********************************************************************//**
  Return name of the research group player belongs to.
**************************************************************************/
const char *api_methods_research_rule_name(lua_State *L, Player *pplayer)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pplayer, nullptr);

  return research_rule_name(research_get(pplayer));
}

/**********************************************************************//**
  Return name of the research group player belongs to.
**************************************************************************/
const char *api_methods_research_name_translation(lua_State *L,
                                                  Player *pplayer)
{
  static char buf[MAX_LEN_MSG];

  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pplayer, nullptr);

  (void) research_pretty_name(research_get(pplayer), buf, ARRAY_SIZE(buf));

  return buf;
}

/**********************************************************************//**
  Return Lua list of all players
  FIXME: safe function, no reason to hide
**************************************************************************/
lua_Object api_methods_private_list_players(lua_State *L)
{
  lua_Object result = 0;
  int i = 0;

  LUASCRIPT_CHECK_STATE(L, 0);
  lua_createtable(L, player_count(), 0);
  result = lua_gettop(L);
  players_iterate(pplayer) {
    tolua_pushfieldusertype(L, result, ++i, pplayer, "Player");
  } players_iterate_end;
  return result;
}

/**********************************************************************//**
  Return list head for unit list for Player
**************************************************************************/
Unit_List_Link *api_methods_private_player_unit_list_head(lua_State *L,
                                                          Player *pplayer)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pplayer, nullptr);
  return unit_list_head(pplayer->units);
}

/**********************************************************************//**
  Return list head for city list for Player
**************************************************************************/
City_List_Link *api_methods_private_player_city_list_head(lua_State *L,
                                                          Player *pplayer)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pplayer, nullptr);

  return city_list_head(pplayer->cities);
}

/**********************************************************************//**
  Return rule name for Tech_Type
**************************************************************************/
const char *api_methods_tech_type_rule_name(lua_State *L, Tech_Type *ptech)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, ptech, nullptr);

  return advance_rule_name(ptech);
}

/**********************************************************************//**
  Return translated name for Tech_Type
**************************************************************************/
const char *api_methods_tech_type_name_translation(lua_State *L,
                                                   Tech_Type *ptech)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, ptech, nullptr);

  return advance_name_translation(ptech);
}

/**********************************************************************//**
  Return rule name for Terrain
**************************************************************************/
const char *api_methods_terrain_rule_name(lua_State *L, Terrain *pterrain)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pterrain, nullptr);

  return terrain_rule_name(pterrain);
}

/**********************************************************************//**
  Return translated name for Terrain
**************************************************************************/
const char *api_methods_terrain_name_translation(lua_State *L,
                                                 Terrain *pterrain)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pterrain, nullptr);

  return terrain_name_translation(pterrain);
}

/**********************************************************************//**
  Return name of the terrain's class
**************************************************************************/
const char *api_methods_terrain_class_name(lua_State *L, Terrain *pterrain)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pterrain, nullptr);

  return terrain_class_name(terrain_type_terrain_class(pterrain));
}

/**********************************************************************//**
  Return rule name for Disaster
**************************************************************************/
const char *api_methods_disaster_rule_name(lua_State *L, Disaster *pdis)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pdis, nullptr);

  return disaster_rule_name(pdis);
}

/**********************************************************************//**
  Return translated name for Disaster
**************************************************************************/
const char *api_methods_disaster_name_translation(lua_State *L,
                                                  Disaster *pdis)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pdis, nullptr);

  return disaster_name_translation(pdis);
}

/**********************************************************************//**
  Return rule name for Achievement
**************************************************************************/
const char *api_methods_achievement_rule_name(lua_State *L,
                                              Achievement *pach)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pach, nullptr);

  return achievement_rule_name(pach);
}

/**********************************************************************//**
  Return translated name for Achievement
**************************************************************************/
const char *api_methods_achievement_name_translation(lua_State *L,
                                                     Achievement *pach)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pach, nullptr);

  return achievement_name_translation(pach);
}

/**********************************************************************//**
  Return rule name for Action
**************************************************************************/
const char *api_methods_action_rule_name(lua_State *L, Action *pact)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pact, nullptr);

  return action_id_rule_name(pact->id);
}

/**********************************************************************//**
  Return translated name for Action
**************************************************************************/
const char *api_methods_action_name_translation(lua_State *L, Action *pact)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pact, nullptr);

  return action_id_name_translation(pact->id);
}

/**********************************************************************//**
  Return target kind for Action
**************************************************************************/
const char *api_methods_action_target_kind(lua_State *L, Action *pact)
{
  struct action *paction;

  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pact, nullptr);

  paction = action_by_number(pact->id);
  fc_assert_ret_val(paction, "error: no action");

  return action_target_kind_name(action_get_target_kind(paction));
}

/**********************************************************************//**
  Return specialist rule name
**************************************************************************/
const char *api_methods_specialist_rule_name(lua_State *L, Specialist *s)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, s, nullptr);

  return specialist_rule_name(s);
}

/**********************************************************************//**
  Return translated name for specialist
**************************************************************************/
const char *api_methods_specialist_name_translation(lua_State *L,
                                                    Specialist *s)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, s, nullptr);

  return specialist_plural_translation(s);
}

/**********************************************************************//**
  Return if specialist is a superspecialist
**************************************************************************/
bool api_methods_specialist_is_super(lua_State *L, Specialist *s)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, s, FALSE);

  return is_super_specialist(s);
}

/**********************************************************************//**
  Return the native x coordinate of the tile.
**************************************************************************/
int api_methods_tile_nat_x(lua_State *L, Tile *ptile)
{
  LUASCRIPT_CHECK_STATE(L, -1);
  LUASCRIPT_CHECK_SELF(L, ptile, -1);

  return index_to_native_pos_x(tile_index(ptile));
}

/**********************************************************************//**
  Return the native y coordinate of the tile.
**************************************************************************/
int api_methods_tile_nat_y(lua_State *L, Tile *ptile)
{
  LUASCRIPT_CHECK_STATE(L, -1);
  LUASCRIPT_CHECK_SELF(L, ptile, -1);

  return index_to_native_pos_y(tile_index(ptile));
}

/**********************************************************************//**
  Return the map x coordinate of the tile.
**************************************************************************/
int api_methods_tile_map_x(lua_State *L, Tile *ptile)
{
  LUASCRIPT_CHECK_STATE(L, -1);
  LUASCRIPT_CHECK_SELF(L, ptile, -1);

  return index_to_map_pos_x(tile_index(ptile));
}

/**********************************************************************//**
  Return the map y coordinate of the tile.
**************************************************************************/
int api_methods_tile_map_y(lua_State *L, Tile *ptile)
{
  LUASCRIPT_CHECK_STATE(L, -1);
  LUASCRIPT_CHECK_SELF(L, ptile, -1);

  return index_to_map_pos_y(tile_index(ptile));
}

/**********************************************************************//**
  Return City on ptile, else nullptr
**************************************************************************/
City *api_methods_tile_city(lua_State *L, Tile *ptile)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, ptile, nullptr);

  return tile_city(ptile);
}

/**********************************************************************//**
  Return TRUE if there is a city inside the maximum city radius from ptile.
**************************************************************************/
bool api_methods_tile_city_exists_within_max_city_map(lua_State *L,
                                                      Tile *ptile,
                                                      bool may_be_on_center)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, ptile, FALSE);

  return city_exists_within_max_city_map(&(wld.map), ptile, may_be_on_center);
}

/**********************************************************************//**
  Return TRUE if there is a extra with rule name name on ptile.
  If no name is specified return true if there is a extra on ptile.
**************************************************************************/
bool api_methods_tile_has_extra(lua_State *L, Tile *ptile,
                                const char *name)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, ptile, FALSE);

  if (!name) {
    extra_type_iterate(pextra) {
      if (tile_has_extra(ptile, pextra)) {
        return TRUE;
      }
    } extra_type_iterate_end;

    return FALSE;
  } else {
    struct extra_type *pextra;

    pextra = extra_type_by_rule_name(name);

    return (pextra != nullptr && tile_has_extra(ptile, pextra));
  }
}

/**********************************************************************//**
  Return TRUE if there is a base with rule name name on ptile.
  If no name is specified return true if there is any base on ptile.
**************************************************************************/
bool api_methods_tile_has_base(lua_State *L, Tile *ptile, const char *name)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, ptile, FALSE);

  if (!name) {
    extra_type_by_cause_iterate(EC_BASE, pextra) {
      if (tile_has_extra(ptile, pextra)) {
        return TRUE;
      }
    } extra_type_by_cause_iterate_end;

    return FALSE;
  } else {
    struct extra_type *pextra;

    pextra = extra_type_by_rule_name(name);

    return (pextra != nullptr && is_extra_caused_by(pextra, EC_BASE)
            && tile_has_extra(ptile, pextra));
  }
}

/**********************************************************************//**
  Return TRUE if there is a road with rule name name on ptile.
  If no name is specified return true if there is any road on ptile.
**************************************************************************/
bool api_methods_tile_has_road(lua_State *L, Tile *ptile, const char *name)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, ptile, FALSE);

  if (!name) {
    extra_type_by_cause_iterate(EC_ROAD, pextra) {
      if (tile_has_extra(ptile, pextra)) {
        return TRUE;
      }
    } extra_type_by_cause_iterate_end;

    return FALSE;
  } else {
    struct extra_type *pextra;

    pextra = extra_type_by_rule_name(name);

    return (pextra != nullptr && is_extra_caused_by(pextra, EC_ROAD)
            && tile_has_extra(ptile, pextra));
  }
}

/**********************************************************************//**
  Return the extra owner for the specified extra on ptile or nullptr if
  the extra isn't there.
  If no name is specified the owner of the first owned extra at the tile
  is returned.
**************************************************************************/
Player *api_methods_tile_extra_owner(lua_State *L,
                                     Tile *ptile, const char *extra_name)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, ptile, nullptr);

  if (extra_name) {
    struct extra_type *pextra;

    pextra = extra_type_by_rule_name(extra_name);
    LUASCRIPT_CHECK_ARG(L, pextra != nullptr, 3,
                        "unknown extra type", nullptr);

    if (tile_has_extra(ptile, pextra)) {
      /* All extras have the same owner. */
      return extra_owner(ptile);
    } else {
      /* The extra isn't there. */
      return nullptr;
    }
  } else {
    extra_type_iterate(pextra) {
      if (tile_has_extra(ptile, pextra)) {
        /* All extras have the same owner. */
        return extra_owner(ptile);
      }
    } extra_type_iterate_end;

    return nullptr;
  }
}

/**********************************************************************//**
  Is tile occupied by enemies
**************************************************************************/
bool api_methods_enemy_tile(lua_State *L, Tile *ptile, Player *against)
{
  struct city *pcity;

  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, ptile, FALSE);

  if (is_non_allied_unit_tile(ptile, against, FALSE)) {
    return TRUE;
  }

  pcity = tile_city(ptile);
  if (pcity != nullptr && !pplayers_allied(against, city_owner(pcity))) {
    return TRUE;
  }

  return FALSE;
}

/**********************************************************************//**
  Return number of units on tile
**************************************************************************/
int api_methods_tile_num_units(lua_State *L, Tile *ptile)
{
  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_SELF(L, ptile, 0);

  return unit_list_size(ptile->units);
}

/**********************************************************************//**
  Return list head for unit list for Tile
**************************************************************************/
Unit_List_Link *api_methods_private_tile_unit_list_head(lua_State *L,
                                                        Tile *ptile)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, ptile, nullptr);

  return unit_list_head(ptile->units);
}

/**********************************************************************//**
  Return nth tile iteration index (for internal use)
  Will return the next index, or an index < 0 when done
**************************************************************************/
int api_methods_private_tile_next_outward_index(lua_State *L, Tile *pstart,
                                                int tindex, int max_dist)
{
  int dx, dy;
  int newx, newy;
  int startx, starty;

  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_SELF(L, pstart, 0);

  if (tindex < 0) {
    return 0;
  }

  index_to_map_pos(&startx, &starty, tile_index(pstart));

  tindex++;
  while (tindex < MAP_NUM_ITERATE_OUTWARDS_INDICES) {
    if (MAP_ITERATE_OUTWARDS_INDICES[tindex].dist > max_dist) {
      return -1;
    }
    dx = MAP_ITERATE_OUTWARDS_INDICES[tindex].dx;
    dy = MAP_ITERATE_OUTWARDS_INDICES[tindex].dy;
    newx = dx + startx;
    newy = dy + starty;

    if (!normalize_map_pos(&(wld.map), &newx, &newy)) {
      tindex++;
      continue;
    }

    return tindex;
  }

  return -1;
}

/**********************************************************************//**
  Return tile for nth iteration index (for internal use)
**************************************************************************/
Tile *api_methods_private_tile_for_outward_index(lua_State *L,
                                                 Tile *pstart, int tindex)
{
  int newx, newy;

  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pstart, nullptr);
  LUASCRIPT_CHECK_ARG(L,
                      tindex >= 0 && tindex < MAP_NUM_ITERATE_OUTWARDS_INDICES,
                      3, "index out of bounds", nullptr);

  index_to_map_pos(&newx, &newy, tile_index(pstart));
  newx += MAP_ITERATE_OUTWARDS_INDICES[tindex].dx;
  newy += MAP_ITERATE_OUTWARDS_INDICES[tindex].dy;

  if (!normalize_map_pos(&(wld.map), &newx, &newy)) {
    return nullptr;
  }

  return map_pos_to_tile(&(wld.map), newx, newy);
}

/**********************************************************************//**
  Return squared distance between tiles 1 and 2
**************************************************************************/
int api_methods_tile_sq_distance(lua_State *L, Tile *ptile1, Tile *ptile2)
{
  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_SELF(L, ptile1, 0);
  LUASCRIPT_CHECK_ARG_NIL(L, ptile2, 3, Tile, 0);

  return sq_map_distance(ptile1, ptile2);
}

/**********************************************************************//**
  Can punit found a city on its tile?

  FIXME: Unlike name suggests, this is currently used for finding
         cities from huts and the like, and is not suitable for checking
         if specified unit could act to build a city.
**************************************************************************/
bool api_methods_unit_city_can_be_built_here(lua_State *L, Unit *punit)
{
  const struct civ_map *nmap = &(wld.map);

  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, punit, FALSE);

  return city_can_be_built_here(nmap, unit_tile(punit), punit, TRUE);
}

/**********************************************************************//**
  Return the tile of the unit.
**************************************************************************/
Tile *api_methods_unit_tile_get(lua_State *L, Unit *punit)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, punit, nullptr);

  return unit_tile(punit);
}

/**********************************************************************//**
  Whether player knows the tile
**************************************************************************/
bool api_methods_tile_known(lua_State *L, Tile *self, Player *watcher)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, self, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, watcher, 3, Player, FALSE);

  return tile_get_known(self, watcher) != TILE_UNKNOWN;
}

/**********************************************************************//**
  Whether player currently sees the tile
**************************************************************************/
bool api_methods_tile_seen(lua_State *L, Tile *self, Player *watcher)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, self, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, watcher, 3, Player, FALSE);

  return tile_get_known(self, watcher) == TILE_KNOWN_SEEN;
}

/**********************************************************************//**
  Get unit orientation
**************************************************************************/
const Direction *api_methods_unit_orientation_get(lua_State *L,
                                                  Unit *punit)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_ARG_NIL(L, punit, 2, Unit, nullptr);

  return luascript_dir(punit->facing);
}

/**********************************************************************//**
  Return Unit that transports punit, if any.
**************************************************************************/
Unit *api_methods_unit_transporter(lua_State *L, Unit *punit)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, punit, nullptr);

  return punit->transporter;
}

/**********************************************************************//**
  Return list head for cargo list for Unit
**************************************************************************/
Unit_List_Link *api_methods_private_unit_cargo_list_head(lua_State *L,
                                                         Unit *punit)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, punit, nullptr);

  return unit_list_head(punit->transporting);
}

/**********************************************************************//**
  Return TRUE if punit_type has flag.
**************************************************************************/
bool api_methods_unit_type_has_flag(lua_State *L, Unit_Type *punit_type,
                                    const char *flag)
{
  enum unit_type_flag_id id;

  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, punit_type, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, flag, 3, string, FALSE);

  id = unit_type_flag_id_by_name(flag, fc_strcasecmp);
  if (unit_type_flag_id_is_valid(id)) {
    return utype_has_flag(punit_type, id);
  } else {
    luascript_error(L, "Unit type flag \"%s\" does not exist", flag);
    return FALSE;
  }
}

/**********************************************************************//**
  Return TRUE if punit_type has role.
**************************************************************************/
bool api_methods_unit_type_has_role(lua_State *L, Unit_Type *punit_type,
                                    const char *role)
{
  enum unit_role_id id;

  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, punit_type, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, role, 3, string, FALSE);

  id = unit_role_id_by_name(role, fc_strcasecmp);
  if (unit_role_id_is_valid(id)) {
    return utype_has_role(punit_type, id);
  } else {
    luascript_error(L, "Unit role \"%s\" does not exist", role);
    return FALSE;
  }
}

/**********************************************************************//**
  Return TRUE iff the unit type can exist on the tile.
**************************************************************************/
bool api_methods_unit_type_can_exist_at_tile(lua_State *L,
                                             Unit_Type *punit_type,
                                             Tile *ptile)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);
  LUASCRIPT_CHECK_SELF(L, punit_type, FALSE);
  LUASCRIPT_CHECK_ARG_NIL(L, ptile, 3, Tile, FALSE);

  return can_exist_at_tile(&(wld.map), punit_type, ptile);
}

/**********************************************************************//**
  Return rule name for Unit_Type
**************************************************************************/
const char *api_methods_unit_type_rule_name(lua_State *L,
                                            Unit_Type *punit_type)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, punit_type, nullptr);

  return utype_rule_name(punit_type);
}

/**********************************************************************//**
  Return translated name for Unit_Type
**************************************************************************/
const char *api_methods_unit_type_name_translation(lua_State *L,
                                                   Unit_Type *punit_type)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, punit_type, nullptr);

  return utype_name_translation(punit_type);
}


/**********************************************************************//**
  Return Unit for list link
**************************************************************************/
Unit *api_methods_unit_list_link_data(lua_State *L,
                                      Unit_List_Link *ul_link)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);

  return unit_list_link_data(ul_link);
}

/**********************************************************************//**
  Return next list link or nullptr when link is the last link
**************************************************************************/
Unit_List_Link *api_methods_unit_list_next_link(lua_State *L,
                                                Unit_List_Link *ul_link)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);

  return unit_list_link_next(ul_link);
}

/**********************************************************************//**
  Return City for list link
**************************************************************************/
City *api_methods_city_list_link_data(lua_State *L,
                                      City_List_Link *cl_link)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);

  return city_list_link_data(cl_link);
}

/**********************************************************************//**
  Return next list link or nullptr when link is the last link
**************************************************************************/
City_List_Link *api_methods_city_list_next_link(lua_State *L,
                                                City_List_Link *cl_link)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);

  return city_list_link_next(cl_link);
}

/**********************************************************************//**
  Return featured text link of the tile.
**************************************************************************/
const char *api_methods_tile_link(lua_State *L, Tile *ptile)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, ptile, nullptr);

  return tile_link(ptile);
}

/**********************************************************************//**
  Return featured text link of the unit tile.
**************************************************************************/
const char *api_methods_unit_tile_link(lua_State *L, Unit *punit)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, punit, nullptr);

  return unit_tile_link(punit);
}

/**********************************************************************//**
  Return featured text link of the city tile.
**************************************************************************/
const char *api_methods_city_tile_link(lua_State *L, City *pcity)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pcity, nullptr);

  return city_tile_link(pcity);
}

/**********************************************************************//**
  Return featured text link of the unit.
**************************************************************************/
const char *api_methods_unit_link(lua_State *L, Unit *punit)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, punit, nullptr);

  return unit_link(punit);
}

/**********************************************************************//**
  Return featured text link of the city.
**************************************************************************/
const char *api_methods_city_link(lua_State *L, City *pcity)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_SELF(L, pcity, nullptr);

  return city_link(pcity);
}
