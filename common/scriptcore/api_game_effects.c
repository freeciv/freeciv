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

/* common */
#include "effects.h"

/* common/scriptcore */
#include "luascript.h"

#include "api_game_effects.h"

/**********************************************************************//**
  Returns the effect bonus in the world
**************************************************************************/
int api_effects_world_bonus(lua_State *L, const char *effect_type)
{
  enum effect_type etype = EFT_COUNT;

  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_ARG_NIL(L, effect_type, 2, string, 0);

  etype = effect_type_by_name(effect_type, fc_strcasecmp);
  if (!effect_type_is_valid(etype)) {
    return 0;
  }
  return get_world_bonus(etype);
}

/**********************************************************************//**
  Returns the effect bonus for a player
**************************************************************************/
int api_effects_player_bonus(lua_State *L, Player *pplayer,
                             const char *effect_type)
{
  enum effect_type etype = EFT_COUNT;

  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_ARG_NIL(L, pplayer, 2, Player, 0);
  LUASCRIPT_CHECK_ARG_NIL(L, effect_type, 3, string, 0);

  etype = effect_type_by_name(effect_type, fc_strcasecmp);
  if (!effect_type_is_valid(etype)) {
    return 0;
  }
  return get_player_bonus(pplayer, etype);
}

/**********************************************************************//**
  Returns the effect bonus at a city.
**************************************************************************/
int api_effects_city_bonus(lua_State *L, City *pcity,
                           const char *effect_type)
{
  enum effect_type etype = EFT_COUNT;

  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_ARG_NIL(L, pcity, 2, City, 0);
  LUASCRIPT_CHECK_ARG_NIL(L, effect_type, 3, string, 0);

  etype = effect_type_by_name(effect_type, fc_strcasecmp);
  if (!effect_type_is_valid(etype)) {
    return 0;
  }
  return get_city_bonus(pcity, etype);
}

/**********************************************************************//**
  Returns the effect bonus at a unit.
  Can take other_player to support local DiplRel requirements.
**************************************************************************/
int api_effects_unit_bonus(lua_State *L, Unit *punit, Player *other_player,
                           const char *effect_type)
{
  enum effect_type etype = EFT_COUNT;

  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_ARG_NIL(L, punit, 2, Unit, 0);
  LUASCRIPT_CHECK_ARG_NIL(L, effect_type, 4, string, 0);

  etype = effect_type_by_name(effect_type, fc_strcasecmp);
  if (!effect_type_is_valid(etype)) {
    return 0;
  }

  return get_target_bonus_effects(nullptr,
                                  &(const struct req_context) {
                                    .player = unit_owner(punit),
                                    .city = unit_tile(punit)
                                      ? tile_city(unit_tile(punit)) : nullptr,
                                    .tile = unit_tile(punit),
                                    .unit = punit,
                                    .unittype = unit_type_get(punit),
                                  },
                                  &(const struct req_context) {
                                    .player = other_player,
                                  },
                                  etype);
}

/***********************************************************************//**
  Get effect bonus at tile.

  @param  L            Lua State to use
  @param  ptile        Tile to get the effect value for
  @param  pcity        Optional: City related to the effect
                       (e.g. City harvesting the tile)
  @param  output_id    Optional: Indetifier of the output related
                       to the effect (e.g. Output to harvest from the tile)
  @param  effect_type  Effect to check
  @return              Effect bonus value
***************************************************************************/
int api_effects_tile_bonus(lua_State *L, Tile *ptile, City *pcity,
                           const char *output_id, const char *effect_type)
{
  const struct player *pplayer;
  const struct output_type *poutput = nullptr;
  enum effect_type etype;

  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_ARG_NIL(L, ptile, 2, Tile, 0);
  LUASCRIPT_CHECK_ARG_NIL(L, effect_type, 5, string, 0);

  etype = effect_type_by_name(effect_type, fc_strcasecmp);
  if (!effect_type_is_valid(etype)) {
    return 0;
  }

  if (output_id != nullptr) {
    enum output_type_id id = output_type_by_identifier(output_id);

    if (id != O_LAST) {
      poutput = get_output_type(id);
    } else {
      log_warn(_("Unknown output identifier \"%s\""), output_id);
    }
  }

  pplayer = (pcity != nullptr ? city_owner(pcity) : nullptr);

  return get_target_bonus_effects(nullptr,
                                  &(const struct req_context) {
                                    .player = pplayer,
                                    .city = pcity,
                                    .tile = ptile,
                                    .output = poutput,
                                  },
                                  nullptr,
                                  etype);
}

/***********************************************************************//**
  Get effect bonus for specialist.

  @param  L            Lua State to use
  @param  s            Specialist to get the effect value for
  @param  d            Optional: City or Player for which the effect
                       is calculated (if City, its owner becomes the player)
  @param  output_id    Optional: Indetifier of the output related
                       to the effect (e.g. Output to harvest from s)
  @param  effect_type  Effect to check
  @return              Effect bonus value
***************************************************************************/
int
api_effects_specialist_bonus(lua_State *L, Specialist *s, lua_Object d,
                             const char *output_id, const char *effect_type)
{
  struct req_context ctx = {.specialist = s};
  enum effect_type etype;

  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_ARG_NIL(L, s, 2, Specialist, 0);
  LUASCRIPT_CHECK_ARG_NIL(L, effect_type, 5, string, 0);

  etype = effect_type_by_name(effect_type, fc_strcasecmp);
  if (!effect_type_is_valid(etype)) {
    return 0;
  }

  if (output_id != nullptr) {
    enum output_type_id id = output_type_by_identifier(output_id);

    if (O_LAST == id){
      char msg[255];

      fc_snprintf(msg, sizeof(msg),
                  "Unknown output identifier \"%s\"", output_id);
      LUASCRIPT_CHECK_ARG(L, id != O_LAST, 3, msg, 0); /* Fails */
    }
    ctx.output = get_output_type(id);
  }

  if (0 != d) {
    tolua_Error e; /* ignored */

    if (tolua_isusertype(L, d, "Player", 0, &e)) {
      /* nil handled here */
      ctx.player = (Player *) lua_touserdata(L, d);
    } else if (tolua_isusertype(L, d, "City", 0, &e)) {
      ctx.city = (City *) lua_touserdata(L, d);
      if (ctx.city) {
        ctx.player = city_owner(ctx.city);
      }
    } else {
      char msg[255];

      fc_snprintf(msg, sizeof(msg),
                  "got '%s', City or Player expected", tolua_typename(L, d));
      LUASCRIPT_CHECK_ARG(L, FALSE, 2, msg, 0); /* Fails */
    }
  }

  return get_target_bonus_effects(nullptr, &ctx, nullptr, etype);
}

/***********************************************************************//**
  Returns the effect bonus at a tile and the specified unit.
  Unlike effects.unit_bonus() the city the effect is evaluated against is
  the city at ptile tile rather than the city at the unit's tile.
***************************************************************************/
int api_effects_unit_vs_tile_bonus(lua_State *L, Unit *punit, Tile *ptile,
                                   const char *effect_type)
{
  enum effect_type etype = EFT_COUNT;

  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_ARG_NIL(L, punit, 2, Unit, 0);
  LUASCRIPT_CHECK_ARG_NIL(L, ptile, 3, Tile, 0);
  LUASCRIPT_CHECK_ARG_NIL(L, effect_type, 4, string, 0);

  etype = effect_type_by_name(effect_type, fc_strcasecmp);
  if (!effect_type_is_valid(etype)) {
    return 0;
  }

  return get_unit_vs_tile_bonus(ptile, punit, etype);
}
