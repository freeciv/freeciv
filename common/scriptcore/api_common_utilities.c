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

#include <math.h>

/* common */
#include "version.h"

/* utilities */
#include "log.h"
#include "rand.h"

/* common/scriptcore */
#include "luascript.h"

#include "api_common_utilities.h"

/************************************************************************
  Generate random number.
************************************************************************/
int api_utilities_random(lua_State *L, int min, int max)
{
  double roll;

  LUASCRIPT_CHECK_STATE(L, 0);

  roll = ((double) (fc_rand(MAX_UINT32) % MAX_UINT32) / MAX_UINT32);

  return (min + floor(roll * (max - min + 1)));
}

/************************************************************************
  Return the version of freeciv lua script
************************************************************************/
const char *api_utilities_fc_version(lua_State *L)
{
  return freeciv_name_version();
}

/**************************************************************************
  One log message. This module is used by script_game and script_auth.
**************************************************************************/
void api_utilities_log_base(lua_State *L, int level, const char *message)
{
  struct fc_lua *fcl;

  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_ARG_NIL(L, message, 3, string);

  fcl = luascript_get_fcl(L);

  LUASCRIPT_CHECK(L, fcl != NULL, "Undefined freeciv lua state!");

  luascript_log(fcl, level, "%s", message);
}

/**************************************************************************
  Convert text describing direction into direction
**************************************************************************/
Direction api_utilities_str2dir(lua_State *L, const char *dir)
{
  LUASCRIPT_CHECK_STATE(L, direction8_invalid());
  LUASCRIPT_CHECK_ARG_NIL(L, dir, 2, string, direction8_invalid());

  return direction8_by_name(dir, fc_strcasecmp);
}
