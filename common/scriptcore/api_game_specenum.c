/*****************************************************************************
 Freeciv - Copyright (C) 2010 - The Freeciv Project
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

#include <string.h>

/* dependencies/lua */
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

/* utility */
#include "support.h"

/* common */
#include "events.h"
#include "fc_types.h"

/* common/scriptcore */
#include "api_specenum.h"

#include "api_game_specenum.h"


/**********************************************************************//**
  Define the __index function for each exported specenum type.
**************************************************************************/
API_SPECENUM_DEFINE_INDEX(event_type, "E_")
API_SPECENUM_DEFINE_INDEX_REV(tech_cost_style);
API_SPECENUM_DEFINE_INDEX_REV(tech_leakage_style);

/**********************************************************************//**
  Load the specenum modules into Lua state L.
**************************************************************************/
int api_game_specenum_open(lua_State *L)
{
  API_SPECENUM_CREATE_TABLE(L, event_type, "E");
  API_SPECENUM_CREATE_TABLE_REV(L, tech_cost_style, "TECH_COST");
  API_SPECENUM_CREATE_TABLE_REV(L, tech_leakage_style, "TECH_LEAKAGE");

  return 0;
}
