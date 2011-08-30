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

#ifndef FC__LUSSCRIPT_H
#define FC__LUSSCRIPT_H

/* dependencies/tolua */
#include "tolua.h"

/* utility */
#include "support.h"            /* fc__attribute() */

/* server/scripting */
#include "script_types.h"

struct section_file;

lua_State *state_lua;

/* internal api error functions */
int script_error(const char *format, ...)
    fc__attribute((__format__ (__printf__, 1, 2)));

int script_arg_error(int narg, const char *msg);

int luascript_report(lua_State *L, int status, const char *code);
void luascript_push_args(lua_State *L, int nargs, enum api_types *parg_types,
                         va_list args);
bool luascript_func_check(lua_State *L, const char *funcname);
int luascript_call(lua_State *L, int narg, int nret, const char *code);
int luascript_do_string(lua_State *L, const char *str, const char *name);
int luascript_do_file(lua_State *L, const char *filename);

/* error functions for lua scripts */
int luascript_error(lua_State *L, const char *format, va_list vargs);
int luascript_arg_error(lua_State *L, int narg, const char *msg);

/* callback invocation function. */
bool luascript_callback_invoke(lua_State *L, const char *callback_name,
                               int nargs, va_list args);
void luascript_remove_exported_object(lua_State *L, void *object);
lua_State *luascript_new(void);
void luascript_destroy(lua_State *L);

/* callback invocation function. */
bool script_callback_invoke(const char *callback_name, int nargs,
                            enum api_types *parg_types, va_list args);

/* Returns additional arguments on failure. */
#define SCRIPT_ASSERT_CAT(str1, str2) str1 ## str2

/* Script assertion (for debugging only) */
#ifdef DEBUG
#define SCRIPT_ASSERT(check, ...)                                            \
  if (!(check)) {                                                            \
    script_error("in %s() [%s::%d]: the assertion '%s' failed.",             \
                 __FUNCTION__, __FILE__, __LINE__, #check);                  \
    return SCRIPT_ASSERT_CAT(, __VA_ARGS__);                                 \
  }
#else
#define SCRIPT_ASSERT(check, ...)
#endif

/* script_error on failed check */
#define SCRIPT_CHECK(check, msg, ...)                                        \
  if (!(check)) {                                                            \
    script_error(msg);                                                       \
    return SCRIPT_ASSERT_CAT(, __VA_ARGS__);                                 \
  }

/* script_arg_error on failed check */
#define SCRIPT_CHECK_ARG(check, narg, msg, ...)                              \
  if (!(check)) {                                                            \
    script_arg_error(narg, msg);                                             \
    return SCRIPT_ASSERT_CAT(, __VA_ARGS__);                                 \
  }

/* script_arg_error on nil value */
#define SCRIPT_CHECK_ARG_NIL(value, narg, type, ...)                         \
  if ((value) == NULL) {                                                     \
    script_arg_error(narg, "got 'nil', '" #type "' expected");               \
    return SCRIPT_ASSERT_CAT(, __VA_ARGS__);                                 \
  }

/* script_arg_error on nil value */
#define SCRIPT_CHECK_SELF(value, ...)                                        \
  if ((value) == NULL) {                                                     \
    script_arg_error(1, "got 'nil' for self");                               \
    return SCRIPT_ASSERT_CAT(, __VA_ARGS__);                                 \
  }

#endif /* FC__LUSSCRIPT_H */
