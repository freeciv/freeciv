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

/* common/scriptcore */
#include "luascript.h"

/* server */
#include "score.h"
#include "srv_main.h"

/* server/scripting */
#include "script_server.h"

#include "api_server_base.h"

/*****************************************************************************
  Return the civilization score (total) for player
*****************************************************************************/
int api_server_player_civilization_score(lua_State *L, Player *pplayer)
{
  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_SELF(L, pplayer, 0);

  return get_civ_score(pplayer);
}

/*****************************************************************************
  Returns TRUE if the game was started.
*****************************************************************************/
bool api_server_was_started(lua_State *L)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);

  return game_was_started();
}

/*****************************************************************************
  Save the game (a manual save is triggert).
*****************************************************************************/
void api_server_save(lua_State *L, const char *filename)
{
  LUASCRIPT_CHECK_STATE(L);

  save_game(filename, "User request (Lua)", FALSE);
}
