/*
** $Id: luasql.c,v 1.23 2007/10/29 20:58:23 carregal Exp $
** See Copyright Notice in license.html
*/

#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#if ! defined (LUA_VERSION_NUM) || LUA_VERSION_NUM < 501
#include "compat-5.1.h"
#endif


#include "luasql.h"

/*
** Typical database error situation
*/
LUASQL_API int luasql_faildirect(lua_State *L, const char *err) {
    lua_pushnil(L);
    lua_pushstring(L, err);
    return 2;
}


/*
** Return the name of the object's metatable.
** This function is used by `tostring'.
*/
static int luasql_tostring (lua_State *L) {
	char buff[100];
	pseudo_data *obj = (pseudo_data *)lua_touserdata (L, 1);
	if (obj->closed)
		strcpy (buff, "closed");
	else
		sprintf (buff, "%p", (void *)obj);
	lua_pushfstring (L, "%s (%s)", lua_tostring(L,lua_upvalueindex(1)), buff);
	return 1;
}


/*
** Create a metatable and leave it on top of the stack.
*/
LUASQL_API int luasql_createmeta (lua_State *L, const char *name, const luaL_reg *methods) {
	if (!luaL_newmetatable (L, name))
		return 0;

	/* define methods */
	luaL_openlib (L, NULL, methods, 0);

	/* define metamethods */
	lua_pushliteral (L, "__gc");
	lua_pushcfunction (L, methods->func);
	lua_settable (L, -3);

	lua_pushliteral (L, "__index");
	lua_pushvalue (L, -2);
	lua_settable (L, -3);

	lua_pushliteral (L, "__tostring");
	lua_pushstring (L, name);
	lua_pushcclosure (L, luasql_tostring, 1);
	lua_settable (L, -3);

	lua_pushliteral (L, "__metatable");
	lua_pushliteral (L, LUASQL_PREFIX"you're not allowed to get this metatable");
	lua_settable (L, -3);

	return 1;
}


/*
** Define the metatable for the object on top of the stack
*/
LUASQL_API void luasql_setmeta (lua_State *L, const char *name) {
	luaL_getmetatable (L, name);
	lua_setmetatable (L, -2);
}


/*
** Assumes the table is on top of the stack.
*/
LUASQL_API void luasql_set_info (lua_State *L) {
	lua_pushliteral (L, "_COPYRIGHT");
	lua_pushliteral (L, "Copyright (C) 2003-2007 Kepler Project");
	lua_settable (L, -3);
	lua_pushliteral (L, "_DESCRIPTION");
	lua_pushliteral (L, "LuaSQL is a simple interface from Lua to a DBMS");
	lua_settable (L, -3);
	lua_pushliteral (L, "_VERSION");
	lua_pushliteral (L, "LuaSQL 2.1.1");
	lua_settable (L, -3);
}
