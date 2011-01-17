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

#endif /* FC__LUSSCRIPT_H */
