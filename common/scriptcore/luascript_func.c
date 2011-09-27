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

#include <stdarg.h>

/* dependencies/lua */
#include "lua.h"
#include "lualib.h"

/* utility */
#include "string_vector.h"

/* common/scriptcore */
#include "luascript.h"
#include "luascript_types.h"

#include "luascript_func.h"

static struct luascript_func *func_new(bool required, int nargs,
                                       enum api_types *parg_types);
static void func_destroy(struct luascript_func *pfunc);

struct luascript_func {
  bool required;              /* if this function is required */
  int nargs;                  /* number of arguments to pass */
  enum api_types *arg_types;  /* argument types */
};

#define SPECHASH_TAG luascript_func
#define SPECHASH_KEY_TYPE char *
#define SPECHASH_DATA_TYPE struct luascript_func *
#define SPECHASH_KEY_VAL genhash_str_val_func
#define SPECHASH_KEY_COMP genhash_str_comp_func
#define SPECHASH_KEY_COPY genhash_str_copy_func
#define SPECHASH_KEY_FREE genhash_str_free_func
#define SPECHASH_DATA_FREE func_destroy
#include "spechash.h"

#define luascript_func_hash_keys_iterate(phash, key)                         \
  TYPED_HASH_KEYS_ITERATE(char *, phash, key)
#define luascript_func_hash_keys_iterate_end                                 \
  HASH_KEYS_ITERATE_END

/*****************************************************************************
  Create a new function definition.
*****************************************************************************/
static struct luascript_func *func_new(bool required, int nargs,
                                       enum api_types *parg_types)
{
  struct luascript_func *pfunc = fc_malloc(sizeof(*pfunc));

  pfunc->required = required;
  pfunc->nargs = nargs;
  pfunc->arg_types = parg_types;

  return pfunc;
}

/*****************************************************************************
  Free a function definition.
*****************************************************************************/
static void func_destroy(struct luascript_func *pfunc)
{
  if (pfunc->arg_types) {
    free(pfunc->arg_types);
  }
  free(pfunc);
}

/*****************************************************************************
  Test if all function are defines. If it fails (return value FALSE), the
  missing functions are listed in 'missing_func_required' and
  'missing_func_optional'.
*****************************************************************************/
bool luascript_func_check(struct fc_lua *fcl,
                          struct strvec *missing_func_required,
                          struct strvec *missing_func_optional)
{
  bool ret = TRUE;

  fc_assert_ret_val(fcl, FALSE);
  fc_assert_ret_val(fcl->funcs, FALSE);

  luascript_func_hash_keys_iterate(fcl->funcs, func_name) {
    if (!luascript_check_function(fcl, func_name)) {
      struct luascript_func *pfunc;

      fc_assert_ret_val(luascript_func_hash_lookup(fcl->funcs, func_name,
                                                   &pfunc), FALSE);

      if (pfunc->required) {
        strvec_append(missing_func_required, func_name);
      } else {
        strvec_append(missing_func_optional, func_name);
      }

      ret = FALSE;
    }
  } luascript_func_hash_keys_iterate_end;

  return ret;
}

/*****************************************************************************
  Add a required function (va version).
*****************************************************************************/
void luascript_func_add_valist(struct fc_lua *fcl, const char *func_name,
                               bool required, int nargs, va_list args)
{
  struct luascript_func *pfunc;

  fc_assert_ret(fcl);
  fc_assert_ret(fcl->funcs);

  if (luascript_func_hash_lookup(fcl->funcs, func_name, &pfunc)) {
    luascript_log(fcl, LOG_ERROR, "Function '%s' was already created.",
                  func_name);
  } else {
    enum api_types *parg_types = fc_calloc(nargs, sizeof(*parg_types));
    char *name = fc_strdup(func_name);
    int i;

    for (i = 0; i < nargs; i++) {
      *(parg_types + i) = va_arg(args, int);
    }

    pfunc = func_new(required, nargs, parg_types);

    luascript_func_hash_insert(fcl->funcs, name, pfunc);
  }
}

/*****************************************************************************
  Add a required function.
*****************************************************************************/
void luascript_func_add(struct fc_lua *fcl, const char *func_name,
                        bool required, int nargs, ...)
{
  va_list args;

  va_start(args, nargs);
  luascript_func_add_valist(fcl, func_name, required, nargs, args);
  va_end(args);
}

/*****************************************************************************
  Free the function definitions.
*****************************************************************************/
void luascript_func_free(struct fc_lua *fcl)
{
  if (fcl && fcl->funcs) {
    luascript_func_hash_destroy(fcl->funcs);
    fcl->funcs = NULL;
  }
}

/*****************************************************************************
  Initialize the structures needed to save functions definitions.
*****************************************************************************/
void luascript_func_init(struct fc_lua *fcl)
{
  fc_assert_ret(fcl != NULL);

  if (fcl->funcs == NULL) {
    /* Define the prototypes for the needed lua functions. */
    fcl->funcs = luascript_func_hash_new();
  }
}

/*****************************************************************************
  Call a lua function; an int value is returned ('retval'). The functions
  returns the success of the operation.

  Example call to the lua function 'user_load()':
    luascript_func_call("user_load", 1, API_TYPE_CONNECTION, pconn);
*****************************************************************************/
bool luascript_func_call_valist(struct fc_lua *fcl, const char *func_name,
                                int *retval, int nargs, va_list args)
{
  struct luascript_func *pfunc;
  int ret = -1; /* -1 as invalid value */
  bool success = FALSE;

  fc_assert_ret_val(fcl, FALSE);
  fc_assert_ret_val(fcl->state, FALSE);
  fc_assert_ret_val(fcl->funcs, FALSE);

  if (!luascript_func_hash_lookup(fcl->funcs, func_name, &pfunc)) {
    luascript_log(fcl, LOG_ERROR, "Lua function '%s' does not exist, "
                                  "so cannot be invoked.", func_name);
    return FALSE;
  }

  /* The function name */
  lua_getglobal(fcl->state, func_name);

  if (!lua_isfunction(fcl->state, -1)) {
    if (pfunc->required) {
      /* This function is required. Thus, this is an error. */
      luascript_log(fcl, LOG_ERROR, "Unknown lua function '%s'", func_name);
      lua_pop(fcl->state, 1);
    }
    return FALSE;
  }

  if (pfunc->nargs != nargs) {
    luascript_log(fcl, LOG_ERROR, "Lua function '%s' requires %d args "
                                  "but was passed %d on invoke.",
                  func_name, pfunc->nargs, nargs);
    return FALSE;
  }

  luascript_push_args(fcl, nargs, pfunc->arg_types, args);

  /* Call the function with nargs arguments, return 1 results */
  if (luascript_call(fcl, nargs, 1, NULL) == 0) {
    /* Successful call to the script. */
    success = TRUE;

    /* Check the return value. */
    if (lua_isnumber(fcl->state, -1)) {
      ret = lua_tonumber(fcl->state, -1);
    }
    lua_pop(fcl->state, 1);   /* pop return value */
  }

  luascript_log(fcl, LOG_VERBOSE, "Call to '%s' returned '%d' (-1 means no "
                                  "return value).", func_name, ret);

  if (retval != NULL) {
    *retval = ret;
  }

  return success;
}

/*****************************************************************************
  Call a lua function; an int value is returned.

  Example call to the lua function 'user_load()':
    luascript_func_call("user_load", 1, API_TYPE_CONNECTION, pconn);
*****************************************************************************/
bool luascript_func_call(struct fc_lua *fcl, const char *func_name,
                         int *retval, int nargs, ...)
{
  va_list args;
  bool success;

  va_start(args, nargs);
  success = luascript_func_call_valist(fcl, func_name, retval, nargs, args);
  va_end(args);

  return success;
}

/*****************************************************************************
  Return iff the function is required.
*****************************************************************************/
bool luascript_func_is_required(struct fc_lua *fcl, const char *func_name)
{
  struct luascript_func *pfunc;

  fc_assert_ret_val(fcl, FALSE);
  fc_assert_ret_val(fcl->state, FALSE);
  fc_assert_ret_val(fcl->funcs, FALSE);

  if (!luascript_func_hash_lookup(fcl->funcs, func_name, &pfunc)) {
    luascript_log(fcl, LOG_ERROR, "Lua function '%s' does not exist.",
                  func_name);
    return FALSE;
  }

  return pfunc->required;
}
