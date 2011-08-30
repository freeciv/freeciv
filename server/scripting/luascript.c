/****************************************************************************
 Freeciv - Copyright (C) 2005 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
****************************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

/* dependencies/lua */
#include "lua.h"
#include "lualib.h"

/* utility */
#include "astring.h"
#include "log.h"
#include "registry.h"

/* server/scripting */
#include "script_types.h"

#include "luascript.h"

/****************************************************************************
  Configuration for script execution time limits. Checkinterval is the
  number of executed lua instructions between checking. Disabled if 0.
****************************************************************************/
#define LUASCRIPT_MAX_EXECUTION_TIME_SEC 5.0
#define LUASCRIPT_CHECKINTERVAL 10000


/**************************************************************************
  Lua virtual machine state. (temporarily moved here)
**************************************************************************/
lua_State *state_lua = NULL;

/****************************************************************************
  Unsafe Lua builtin symbols that we to remove access to.

  If Freeciv's Lua version changes, you have to check how the set of
  unsafe functions and modules changes in the new version. Update the list of
  loaded libraries in luascript_lualibs, then update the unsafe symbols
  blacklist in luascript_unsafe_symbols.

  Once the variables are updated for the new version, update the value of
  LUASCRIPT_SECURE_LUA_VERSION

  In general, unsafe is all functionality that gives access to:
  * Reading files and running processes
  * Loading lua files or libraries
****************************************************************************/
#define LUASCRIPT_SECURE_LUA_VERSION 501

static const char *luascript_unsafe_symbols[] = {
  "debug",
  "dofile",
  "loadfile",
  NULL
};

#if LUA_VERSION_NUM != LUASCRIPT_SECURE_LUA_VERSION
#warning "The script runtime's unsafe symbols information is not up to date."
#warning "This can be a big security hole!"
#endif

/****************************************************************************
  Lua libraries to load (all default libraries, excluding operating system
  and library loading modules). See linit.c in Lua 5.1 for the default list.
****************************************************************************/
static luaL_Reg luascript_lualibs[] = {
  /* Using default libraries excluding: package, io and os */
  {"", luaopen_base},
  {LUA_TABLIBNAME, luaopen_table},
  {LUA_STRLIBNAME, luaopen_string},
  {LUA_MATHLIBNAME, luaopen_math},
  {LUA_DBLIBNAME, luaopen_debug},
  {NULL, NULL}
};

static void luascript_traceback_func_save(lua_State *L);
static void luascript_traceback_func_push(lua_State *L);
static void luascript_exec_check(lua_State *L, lua_Debug *ar);
static void luascript_hook_start(lua_State *L);
static void luascript_hook_end(lua_State *L);
static void luascript_openlibs(lua_State *L, const luaL_Reg *llib);
static void luascript_blacklist(lua_State *L, const char *lsymbols[]);

/**************************************************************************
  Internal api error function.
  Invoking this will cause Lua to stop executing the current context and
  throw an exception, so to speak.
**************************************************************************/
int script_error(const char *format, ...)
{
  va_list vargs;
  int ret;
  lua_State *state = state_lua;

  va_start(vargs, format);
  ret = luascript_error(state, format, vargs);
  va_end(vargs);

  return ret;
}

/**************************************************************************
  Like script_error, but using a prefix identifying the called lua function:
    bad argument #narg to '<func>': msg
**************************************************************************/
int script_arg_error(int narg, const char *msg)
{
  lua_State *state = state_lua;

  return luascript_arg_error(state, narg, msg);
}

/****************************************************************************
  Report a lua error.
****************************************************************************/
int luascript_report(lua_State *L, int status, const char *code)
{
  fc_assert_ret_val(L != NULL, -1);

  if (status) {
    struct astring str = ASTRING_INIT;
    const char *msg;
    int lineno;

    if (!(msg = lua_tostring(L, -1))) {
      msg = "(error with no message)";
    }

    /* Add error message. */
    astr_add_line(&str, "lua error:");
    astr_add_line(&str, "\t%s", msg);

    if (code) {
      /* Add lines around the place the parse error is. */
      if (sscanf(msg, "%*[^:]:%d:", &lineno) == 1) {
        const char *begin, *end;
        int i;

        astr_add(&str, "\n");

        i = 1;
        for (begin = code; *begin != '\0';) {
          int len;

          end = strchr(begin, '\n');
          if (end) {
            len = end - begin;
          } else {
            len = strlen(begin);
          }

          if (abs(lineno - i) <= 3) {
            const char *indicator;

            indicator = (lineno == i) ? "-->" : "   ";

            astr_add_line(&str, "\t%s%3d:\t%*.*s",
                indicator, i, len, len, begin);
          }

          i++;

          if (end) {
            begin = end + 1;
          } else {
            break;
          }
        }

        astr_add(&str, "\n");
      }
    }

    log_error("%s", astr_str(&str));

    astr_free(&str);

    lua_pop(L, 1);
  }

  return status;
}

/****************************************************************************
  Find the debug.traceback function and store in the registry
****************************************************************************/
static void luascript_traceback_func_save(lua_State *L)
{
  /* Find the debug.traceback function, if available */
  lua_getglobal(L, "debug");
  if (lua_istable(L, -1)) {
    lua_getfield(L, -1, "traceback");
    lua_setfield(L, LUA_REGISTRYINDEX, "freeciv_traceback");
  }
  lua_pop(L, 1);       /* pop debug */
}

/****************************************************************************
  Push the traceback function to the stack
****************************************************************************/
static void luascript_traceback_func_push(lua_State *L)
{
  lua_getfield(L, LUA_REGISTRYINDEX, "freeciv_traceback");
}

/****************************************************************************
  Check currently excecuting lua function for execution time limit
****************************************************************************/
static void luascript_exec_check(lua_State *L, lua_Debug *ar)
{
  lua_Number exec_clock;

  lua_getfield(L, LUA_REGISTRYINDEX, "freeciv_exec_clock");
  exec_clock = lua_tonumber(L, -1);
  lua_pop(L, 1);
  if ((float)(clock() - exec_clock)/CLOCKS_PER_SEC
      > LUASCRIPT_MAX_EXECUTION_TIME_SEC) {
    luaL_error(L, "Execution time limit exceeded in script");
  }
}

/****************************************************************************
  Setup function execution guard
****************************************************************************/
static void luascript_hook_start(lua_State *L)
{
#if LUASCRIPT_CHECKINTERVAL
  /* Store clock timestamp in the registry */
  lua_pushnumber(L, clock());
  lua_setfield(L, LUA_REGISTRYINDEX, "freeciv_exec_clock");
  lua_sethook(L, luascript_exec_check, LUA_MASKCOUNT, LUASCRIPT_CHECKINTERVAL);
#endif
}

/****************************************************************************
  Clear function execution guard
****************************************************************************/
static void luascript_hook_end(lua_State *L)
{
#if LUASCRIPT_CHECKINTERVAL
  lua_sethook(L, luascript_exec_check, 0, 0);
#endif
}

/****************************************************************************
  Push arguments into the Lua stack.
****************************************************************************/
void luascript_push_args(lua_State *L, int nargs, enum api_types *parg_types,
                         va_list args)
{
  int i;

  fc_assert_ret(L != NULL);

  for (i = 0; i < nargs; i++) {
    int type;

    type = va_arg(args, int);
    fc_assert_ret(api_types_is_valid(type));
    fc_assert_ret(type == *(parg_types + i));

    switch (type) {
      case API_TYPE_INT:
        {
          int arg;

          arg = va_arg(args, int);
          tolua_pushnumber(L, (lua_Number)arg);
        }
        break;
      case API_TYPE_BOOL:
        {
          int arg;

          arg = va_arg(args, int);
          tolua_pushboolean(L, (bool)arg);
        }
        break;
      case API_TYPE_STRING:
        {
          const char *arg;

          arg = va_arg(args, const char*);
          tolua_pushstring(L, arg);
        }
        break;
      default:
        {
          const char *name;
          void *arg;

          name = api_types_name(type);

          arg = va_arg(args, void*);
          tolua_pushusertype(L, arg, name);
        }
        break;
    }
  }
}

/**************************************************************************
  Return if the function 'funcname' is define in the lua state 'L'.
**************************************************************************/
bool luascript_func_check(lua_State *L, const char *funcname)
{
  bool defined;

  lua_getglobal(L, funcname);
  defined = lua_isfunction(L, -1);
  lua_pop(L, 1);

  return defined;
}

/****************************************************************************
  Evaluate a Lua function call or loaded script on the stack.
  Return nonzero if an error occured.

  If available pass the source code string as code, else NULL.

  Will pop function and arguments (1 + narg values) from the stack.
  Will push nret return values to the stack.

  On error, print an error message with traceback. Nothing is pushed to
  the stack.
****************************************************************************/
int luascript_call(lua_State *L, int narg, int nret, const char *code)
{
  int status;
  int base = lua_gettop(L) - narg;  /* Index of function to call */
  int traceback = 0;                /* Index of traceback function  */

  fc_assert_ret_val(L != NULL, -1);

  /* Find the traceback function, if available */
  luascript_traceback_func_push(L);
  if (lua_isfunction(L, -1)) {
    lua_insert(L, base);  /* insert traceback before function */
    traceback = base;
  } else {
    lua_pop(L, 1);   /* pop non-function traceback */
  }

  luascript_hook_start(L);
  status = lua_pcall(L, narg, nret, traceback);
  luascript_hook_end(L);

  if (status) {
    luascript_report(L, status, code);
  }

  if (traceback) {
    lua_remove(L, traceback);
  }

  return status;
}

/****************************************************************************
  lua_dostring replacement with error message showing on errors.
****************************************************************************/
int luascript_do_string(lua_State *L, const char *str, const char *name)
{
  int status;

  fc_assert_ret_val(L != NULL, -1);

  status = luaL_loadbuffer(L, str, strlen(str), name);
  if (status) {
    luascript_report(L, status, str);
  } else {
    status = luascript_call(L, 0, 0, str);
  }
  return status;
}

/****************************************************************************
  Parse and execute the script at filename.
****************************************************************************/
int luascript_do_file(lua_State *L, const char *filename)
{
  int status;

  fc_assert_ret_val(L != NULL, -1);

  status = luaL_loadfile(L, filename);
  if (status) {
    luascript_report(L, status, NULL);
  } else {
    status = luascript_call(L, 0, 0, NULL);
  }
  return status;
}

/****************************************************************************
  Internal api error function.
  Invoking this will cause Lua to stop executing the current context and
  throw an exception, so to speak.
****************************************************************************/
int luascript_error(lua_State *L, const char *format, va_list vargs)
{
  fc_assert_ret_val(L != NULL, -1);

  luaL_where(L, 1);
  lua_pushvfstring(L, format, vargs);
  lua_concat(L, 2);

  return lua_error(L);
}

/****************************************************************************
  Like script_error, but using a prefix identifying the called lua function:
    bad argument #narg to '<func>': msg
****************************************************************************/
int luascript_arg_error(lua_State *L, int narg, const char *msg)
{
  return luaL_argerror(L, narg, msg);
}

/****************************************************************************
  Mark any, if exported, full userdata representing 'object' in
  the current script state as 'Nonexistent'.
  This changes the type of the lua variable.
****************************************************************************/
void luascript_remove_exported_object(lua_State *L, void *object)
{
  if (L) {
    fc_assert_ret(object != NULL);

    /* The following is similar to
     * tolua_release(..) in src/lib/tolua_map.c
     */
    /* Find the userdata representing 'object' */
    lua_pushstring(L,"tolua_ubox");
    lua_rawget(L, LUA_REGISTRYINDEX);        /* stack: ubox */
    lua_pushlightuserdata(L, object);        /* stack: ubox u */
    lua_rawget(L, -2);                       /* stack: ubox ubox[u] */

    if (!lua_isnil(L, -1)) {
      fc_assert(object == tolua_tousertype(L, -1, NULL));
      /* Change API type to 'Nonexistent' */
      tolua_getmetatable(L, "Nonexistent");  /* stack: ubox ubox[u] mt */
      lua_setmetatable(L, -2);
      /* Set the userdata payload to NULL */
      *((void **)lua_touserdata(L, -1)) = NULL;
      /* Remove from ubox */
      lua_pushlightuserdata(L, object);      /* stack: ubox ubox[u] u */
      lua_pushnil(L);                        /* stack: ubox ubox[u] u nil */
      lua_rawset(L, -4);
    }
    lua_pop(L, 2);
  }
}

/****************************************************************************
  Open lua libraries in the array of library definitions in llib.
****************************************************************************/
static void luascript_openlibs(lua_State *L, const luaL_Reg *llib)
{
  for (; llib->func; llib++) {
    lua_pushcfunction(L, llib->func);
    lua_pushstring(L, llib->name);
    lua_call(L, 1, 0);
  }
}

/****************************************************************************
  Remove global symbols from lua state L
****************************************************************************/
static void luascript_blacklist(lua_State *L, const char *lsymbols[])
{
  int i;

  for (i = 0; lsymbols[i] != NULL; i++) {
    lua_pushnil(L);
    lua_setglobal(L, lsymbols[i]);
  }
}

/****************************************************************************
  Initialize the scripting state.
****************************************************************************/
lua_State *luascript_new(void)
{
  lua_State *L;

  L = lua_open();
  if (!L) {
    return NULL;
  }

  luascript_openlibs(L, luascript_lualibs);
  luascript_traceback_func_save(L);
  luascript_blacklist(L, luascript_unsafe_symbols);

  return L;
}

/****************************************************************************
  Free the scripting data.
****************************************************************************/
void luascript_destroy(lua_State *L)
{
  if (L) {
    lua_gc(L, LUA_GCCOLLECT, 0); /* Collected garbage */
    lua_close(L);
  }
}

/****************************************************************************
  Invoke the 'callback_name' Lua function.
****************************************************************************/
bool script_callback_invoke(const char *callback_name, int nargs,
                            enum api_types *parg_types, va_list args)
{
  bool stop_emission = FALSE;
  lua_State *state = state_lua;

  /* The function name */
  lua_getglobal(state, callback_name);

  if (!lua_isfunction(state, -1)) {
    log_error("lua error: Unknown callback '%s'", callback_name);
    lua_pop(state, 1);
    return FALSE;
  }

  luascript_push_args(state, nargs, parg_types, args);

  /* Call the function with nargs arguments, return 1 results */
  if (luascript_call(state, nargs, 1, NULL)) {
    return FALSE;
  }

  /* Shall we stop the emission of this signal? */
  if (lua_isboolean(state, -1)) {
    stop_emission = lua_toboolean(state, -1);
  }
  lua_pop(state, 1);   /* pop return value */

  return stop_emission;
}