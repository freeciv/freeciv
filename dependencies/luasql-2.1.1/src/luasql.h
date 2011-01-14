/*
** $Id: luasql.h,v 1.11 2007/03/21 23:12:57 mascarenhas Exp $
** See Copyright Notice in license.html
*/

#ifndef _LUASQL_
#define _LUASQL_

#ifndef LUASQL_API
#define LUASQL_API
#endif

#define LUASQL_PREFIX "LuaSQL: "
#define LUASQL_TABLENAME "luasql"
#define LUASQL_ENVIRONMENT "Each driver must have an environment metatable"
#define LUASQL_CONNECTION "Each driver must have a connection metatable"
#define LUASQL_CURSOR "Each driver must have a cursor metatable"

typedef struct {
	short  closed;
} pseudo_data;

LUASQL_API int luasql_faildirect (lua_State *L, const char *err);
LUASQL_API int luasql_createmeta (lua_State *L, const char *name, const luaL_reg *methods);
LUASQL_API void luasql_setmeta (lua_State *L, const char *name);
LUASQL_API void luasql_set_info (lua_State *L);

#endif
