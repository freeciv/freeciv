/***********************************************************************
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

/* dependencies/cvercmp */
#include "cvercmp.h"

/* utilities */
#include "deprecations.h"
#include "log.h"
#include "rand.h"

/* common */
#include "map.h"
#include "version.h"

/* common/scriptcore */
#include "luascript.h"

#include "api_common_utilities.h"

/********************************************************************//**
  Generate random number.
************************************************************************/
int api_utilities_random(lua_State *L, int min, int max)
{
  double roll;

  LUASCRIPT_CHECK_STATE(L, 0);

  roll = ((double) (fc_rand(MAX_UINT32) % MAX_UINT32) / MAX_UINT32);

  return (min + floor(roll * (max - min + 1)));
}

/********************************************************************//**
  Name and version of freeciv.
  Deprecated because of the confusing function name.
************************************************************************/
const char *api_utilities_fc_version(lua_State *L)
{
  LUASCRIPT_CHECK_STATE(L, 0);

  log_deprecation("Deprecated: lua construct \"fc_version\", "
                  "deprecated since \"3.2\", used. "
                  "Use \"name_version\" instead.");

  return freeciv_name_version();
}

/********************************************************************//**
  Return the name and version of freeciv
************************************************************************/
const char *api_utilities_name_version(lua_State *L)
{
  LUASCRIPT_CHECK_STATE(L, 0);

  return freeciv_name_version();
}

/********************************************************************//**
  Comparable freeciv version
************************************************************************/
const char *api_utilities_comparable_version(lua_State *L)
{
  LUASCRIPT_CHECK_STATE(L, 0);

  return fc_comparable_version();
}

/********************************************************************//**
  Version string with no name
************************************************************************/
const char *api_utilities_version_string(lua_State *L)
{
  LUASCRIPT_CHECK_STATE(L, 0);

  return freeciv_datafile_version();
}

/********************************************************************//**
  Compare two version strings. Return which one is bigger, or zero
  if they are equal.
************************************************************************/
int api_utilities_versions_compare(lua_State *L,
                                   const char *ver1, const char *ver2)
{
  enum cvercmp_type result;

  LUASCRIPT_CHECK_STATE(L, 0);
  LUASCRIPT_CHECK_ARG_NIL(L, ver1, 2, string, 0);
  LUASCRIPT_CHECK_ARG_NIL(L, ver2, 3, string, 0);

  result = cvercmp_cmp(ver1, ver2);

  switch (result) {
  case CVERCMP_EQUAL:
    return 0;
  case CVERCMP_GREATER:
    return 1;
  case CVERCMP_LESSER:
    return -1;
  default:
    fc_assert(result == CVERCMP_EQUAL
              || result == CVERCMP_GREATER
              || result == CVERCMP_LESSER);
    return 0;
  }
}

/********************************************************************//**
  One log message. This module is used by script_game and script_auth.
************************************************************************/
void api_utilities_log_base(lua_State *L, int level, const char *message)
{
  struct fc_lua *fcl;

  LUASCRIPT_CHECK_STATE(L);
  LUASCRIPT_CHECK_ARG_NIL(L, message, 3, string);

  fcl = luascript_get_fcl(L);

  LUASCRIPT_CHECK(L, fcl != nullptr, "Undefined Freeciv lua state!");

  luascript_log(fcl, level, "%s", message);
}

/*********************************************************************//***
  Just return the direction as number
**************************************************************************/
int api_utilities_direction_id(lua_State *L, Direction dir)
{
  LUASCRIPT_CHECK_STATE(L, 0);

  return (int) dir;
}

/**********************************************************************//***
  Get direction name
***************************************************************************/
const char *api_utilities_dir2str(lua_State *L, Direction dir)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK(L, is_valid_dir(dir), "Direction is invalid",
                  nullptr);

  return direction8_name(dir);
}

/********************************************************************//**
  Convert text describing direction into direction
************************************************************************/
const Direction *api_utilities_str2dir(lua_State *L, const char *dir)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);
  LUASCRIPT_CHECK_ARG_NIL(L, dir, 2, string, nullptr);

  return luascript_dir(direction8_by_name(dir, fc_strcasecmp));
}

/********************************************************************//**
  Previous (counter-clockwise) valid direction
************************************************************************/
const Direction *api_utilities_dir_ccw(lua_State *L, Direction dir)
{
  Direction new_dir = dir;

  LUASCRIPT_CHECK_STATE(L, nullptr);

  do {
    new_dir = dir_ccw(new_dir);
  } while (!is_valid_dir(new_dir));

  return luascript_dir(new_dir);
}

/********************************************************************//**
  Next (clockwise) valid direction
************************************************************************/
const Direction *api_utilities_dir_cw(lua_State *L, Direction dir)
{
  Direction new_dir = dir;

  LUASCRIPT_CHECK_STATE(L, nullptr);

  do {
    new_dir = dir_cw(new_dir);
  } while (!is_valid_dir(new_dir));

  return luascript_dir(new_dir);
}

/********************************************************************//**
  Opposite direction - validity not checked, but it's valid iff
  original direction is.
************************************************************************/
const Direction *api_utilities_opposite_dir(lua_State *L, Direction dir)
{
  LUASCRIPT_CHECK_STATE(L, nullptr);

  return luascript_dir(opposite_direction(dir));
}

/********************************************************************//**
  Is a direction cardinal one?
************************************************************************/
bool api_utilities_direction_is_cardinal(lua_State *L, Direction dir)
{
  LUASCRIPT_CHECK_STATE(L, FALSE);

  return is_cardinal_dir(dir);
}

/********************************************************************//**
  Lua script wants to warn about use of deprecated construct.
************************************************************************/
void api_utilities_deprecation_warning(lua_State *L, char *method,
                                       char *replacement,
                                       char *deprecated_since)
{
  if (are_deprecation_warnings_enabled()) {
    /* TODO: Keep track which deprecations we have already warned about,
     *       and do not keep spamming about them. */
    if (deprecated_since != nullptr && replacement != nullptr) {
      log_deprecation_always("Deprecated: lua construct \"%s\", "
                             "deprecated since \"%s\", used. "
                             "Use \"%s\" instead", method,
                             deprecated_since, replacement);
    } else if (replacement != nullptr) {
      log_deprecation_always("Deprecated: lua construct \"%s\" used. "
                             "Use \"%s\" instead", method, replacement);
    } else {
      log_deprecation_always("Deprecated: lua construct \"%s\" used.",
                             method);
    }
  }
}
