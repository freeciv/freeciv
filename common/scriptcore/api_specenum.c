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

/* dependencies/lua */
#include "lua.h"

/* utilit */
#include "log.h"

#include "api_specenum.h"

/**********************************************************************//**
  Create a module table and set the member lookup function.
**************************************************************************/
void api_specenum_create_table(lua_State *L, const char *name,
                               lua_CFunction findex)
{
  /* Insert a module table in the global environment,
   * or reuse any preexisting table */
  lua_getglobal(L, name);
  if (lua_isnil(L, -1)) {
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setglobal(L, name);
  }
  fc_assert_ret(lua_istable(L, -1));
  /* Create a metatable */
  lua_newtable(L);                /* stack: module mt */
  lua_pushliteral(L, "__index");
  lua_pushcfunction(L, findex);    /* stack: module mt '__index' index */
  lua_rawset(L, -3);              /* stack: module mt */
  lua_setmetatable(L, -2);        /* stack: module */
  lua_pop(L, 1);
}
