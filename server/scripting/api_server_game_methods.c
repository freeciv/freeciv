/***********************************************************************
 Freeciv - Copyright (C) 1996-2015 - Freeciv Development Team
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
#include "research.h"

/* common/scriptcore */
#include "luascript.h"

/* ai */
#include "aitraits.h" /* ai_trait_get_value() */

/* server */
#include "hand_gen.h"
#include "plrhand.h"
#include "report.h"

/* server/scripting */
#include "script_server.h"

#include "api_server_game_methods.h"

/**********************************************************************//**
  Return the current value of an AI trait in force (base+mod)
**************************************************************************/
int api_methods_player_trait(lua_State *L, Player *pplayer,
                             const char *tname)
{
  enum trait tr;

  LUASCRIPT_CHECK_STATE(L, -1);
  LUASCRIPT_CHECK_SELF(L, pplayer, -1);
  LUASCRIPT_CHECK_ARG_NIL(L, tname, 3, string, 0);

  tr = trait_by_name(tname, fc_strcasecmp);

  LUASCRIPT_CHECK_ARG(L, trait_is_valid(tr), 3, "no such trait", 0);

  return ai_trait_get_value(tr, pplayer);
}

/**********************************************************************//**
  Return the current base value of an AI trait (not including Lua mod)
**************************************************************************/
int api_methods_player_trait_base(lua_State *L, Player *pplayer,
                                  const char *tname)
{
  enum trait tr;

  LUASCRIPT_CHECK_STATE(L, -1);
  LUASCRIPT_CHECK_SELF(L, pplayer, -1);
  LUASCRIPT_CHECK_ARG_NIL(L, tname, 3, string, 0);

  tr = trait_by_name(tname, fc_strcasecmp);

  LUASCRIPT_CHECK_ARG(L, trait_is_valid(tr), 3, "no such trait", 0);

  return pplayer->ai_common.traits[tr].val;
}

/**********************************************************************//**
  Return the current Lua increment to an AI trait
  (can be changed with api_edit_trait_mod_set())
**************************************************************************/
int api_methods_player_trait_current_mod(lua_State *L, Player *pplayer,
                                         const char *tname)
{
  enum trait tr;

  LUASCRIPT_CHECK_STATE(L, -1);
  LUASCRIPT_CHECK_SELF(L, pplayer, -1);
  LUASCRIPT_CHECK_ARG_NIL(L, tname, 3, string, 0);

  tr = trait_by_name(tname, fc_strcasecmp);

  LUASCRIPT_CHECK_ARG(L, trait_is_valid(tr), 3, "no such trait", 0);

  return pplayer->ai_common.traits[tr].mod;
}

/**********************************************************************//**
  Mark the player as one who lost the game, optionally giving some loot
  to looter. This method only marks the nation for wipeout that happens
  only when kill_dying_players() does the reaper's job.
  FIXME: client may be not aware if its player is killed in a script.
**************************************************************************/
void api_methods_player_lose(lua_State *L, Player *pplayer, Player *looter)
{
  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_SELF(L, pplayer);
  LUASCRIPT_CHECK_ARG(L, pplayer->is_alive, 2, "the player has already lost");

  if (looter) {
    LUASCRIPT_CHECK_ARG(L, looter->is_alive, 3, "dead players can't loot");
    player_loot_player(looter, pplayer);
  }
  player_status_add(pplayer, PSTATUS_DYING);
}

/**********************************************************************//**
  Return the minimum random trait value that will be allocated for a nation
**************************************************************************/
int api_methods_nation_trait_min(lua_State *L, Nation_Type *pnation,
                                 const char *tname)
{
  enum trait tr;

  LUASCRIPT_CHECK_STATE(L, -1);
  LUASCRIPT_CHECK_SELF(L, pnation, -1);
  LUASCRIPT_CHECK_ARG_NIL(L, tname, 3, string, 0);

  tr = trait_by_name(tname, fc_strcasecmp);

  LUASCRIPT_CHECK_ARG(L, trait_is_valid(tr), 3, "no such trait", 0);

  return pnation->server.traits[tr].min;
}

/**********************************************************************//**
  Return the maximum random trait value that will be allocated for a nation
**************************************************************************/
int api_methods_nation_trait_max(lua_State *L, Nation_Type *pnation,
                                 const char *tname)
{
  enum trait tr;

  LUASCRIPT_CHECK_STATE(L, -1);
  LUASCRIPT_CHECK_SELF(L, pnation, -1);
  LUASCRIPT_CHECK_ARG_NIL(L, tname, 3, string, 0);

  tr = trait_by_name(tname, fc_strcasecmp);

  LUASCRIPT_CHECK_ARG(L, trait_is_valid(tr), 3, "no such trait", 0);

  return pnation->server.traits[tr].max;
}

/**********************************************************************//**
  Return the default trait value that will be allocated for a nation
**************************************************************************/
int api_methods_nation_trait_default(lua_State *L, Nation_Type *pnation,
                                     const char *tname)
{
  enum trait tr;

  LUASCRIPT_CHECK_STATE(L, -1);
  LUASCRIPT_CHECK_SELF(L, pnation, -1);
  LUASCRIPT_CHECK_ARG_NIL(L, tname, 3, string, 0);

  tr = trait_by_name(tname, fc_strcasecmp);

  LUASCRIPT_CHECK_ARG(L, trait_is_valid(tr), 3, "no such trait", 0);

  return pnation->server.traits[tr].fixed;
}

/**********************************************************************//**
  In multiresearch mode, returns bulbs saved for a specific tech
  in pplayer's research.
  In other modes, returns the additional bulbs the player may get switching
  to this tech (negative for penalty)
**************************************************************************/
int api_methods_player_tech_bulbs(lua_State *L, Player *pplayer,
                                  Tech_Type *tech)
{
  const struct research *presearch;
  Tech_type_id tn;

  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_SELF(L, pplayer, 0);
  LUASCRIPT_CHECK_ARG_NIL(L, tech, 3, Tech_Type, 0);
  tn = advance_number(tech);
  presearch = research_get(pplayer);
  LUASCRIPT_CHECK(L, presearch, "player's research not set", 0);

  if (game.server.multiresearch) {
    return presearch->inventions[tn].bulbs_researched_saved;
  } else {
    if (presearch->researching_saved == tn) {
      return
        presearch->bulbs_researching_saved - presearch->bulbs_researched;
    } else if (tn != presearch->researching
               && presearch->bulbs_researched > 0) {
      int bound_bulbs = presearch->bulbs_researched - presearch->free_bulbs;
      int penalty;

      if (bound_bulbs <= 0) {
        return 0;
      }
      penalty = bound_bulbs * game.server.techpenalty / 100;

      return -MIN(penalty, presearch->bulbs_researched);
    } else {
      return 0;
    }
  }
}

/**********************************************************************//**
  Returns bulbs that can be freely transferred to a new research target.
**************************************************************************/
int api_methods_player_free_bulbs(lua_State *L, Player *pplayer)
{
  const struct research *presearch;

  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_SELF(L, pplayer, 0);
  presearch = research_get(pplayer);
  LUASCRIPT_CHECK(L, presearch, "player's research not set", 0);

  return presearch->free_bulbs;
}

/**********************************************************************//**
  Return score of the type associated to the tag.
**************************************************************************/
int api_methods_tag_score(lua_State *L, Player *pplayer, const char *tag)
{
  LUASCRIPT_CHECK_STATE(L, -1);
  LUASCRIPT_CHECK_SELF(L, pplayer, -1);

  return get_tag_score(tag, pplayer);
}

/**********************************************************************//**
  Return player's love towards another.
**************************************************************************/
int api_methods_love(lua_State *L, Player *pplayer, Player *towards)
{
  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_SELF(L, pplayer, 0);
  LUASCRIPT_CHECK_ARG_NIL(L, towards, 3, Player, 0);

  return pplayer->ai_common.love[player_number(towards)] * 1000 / MAX_AI_LOVE;
}

/**********************************************************************//**
  Add player love towards another.
**************************************************************************/
void api_methods_add_love(lua_State *L, Player *pplayer, Player *towards,
                          int amount)
{
  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_SELF(L, pplayer);
  LUASCRIPT_CHECK_ARG_NIL(L, towards, 3, Player);

  pplayer->ai_common.love[player_number(towards)]
    += amount * MAX_AI_LOVE / 1000;
}

/**********************************************************************//**
  Try to cancel a pact between players.
**************************************************************************/
void api_methods_cancel_pact(lua_State *L, Player *pplayer, Player *towards)
{
  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_SELF(L, pplayer);
  LUASCRIPT_CHECK_ARG_NIL(L, towards, 3, Player);

  handle_diplomacy_cancel_pact(pplayer, player_number(towards), CLAUSE_LAST);
}
