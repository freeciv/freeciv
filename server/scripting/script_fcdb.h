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

#ifndef FC__SCRIPT_FCDB_H
#define FC__SCRIPT_FCDB_H

/* utility */
#include "support.h"            /* fc__attribute() */

/* server */
#include "fcdb.h"

/* Return values of the freeciv database functions via luasql: */
#define SPECENUM_NAME fcdb_status
/* - sql error */
#define SPECENUM_VALUE0 FCDB_ERROR
/* - sql querry was successful and with a positive result */
#define SPECENUM_VALUE1 FCDB_SUCCESS_TRUE
/* - sql querry was successful but there is a negative result */
#define SPECENUM_VALUE2 FCDB_SUCCESS_FALSE
#include "specenum_gen.h"

/* internal api error functions */
int script_fcdb_error(const char *format, ...)
    fc__attribute((__format__ (__printf__, 1, 2)));

int script_fcdb_arg_error(int narg, const char *msg);

/* fcdb script functions. */
bool script_fcdb_init(const char *fcdb_luafile);
enum fcdb_status script_fcdb_call(const char *func_name, int nargs, ...);
void script_fcdb_free(void);

bool script_fcdb_do_string(const char *str);

/* Returns additional arguments on failure. */
#define SCRIPT_FCDB_ASSERT_CAT(str1, str2) str1 ## str2

/* Script assertion (for debugging only) */
#ifdef DEBUG
#define SCRIPT_FCDB_ASSERT(check, ...)                                      \
  if (!(check)) {                                                           \
    script_fcdb_error("in %s() [%s::%d]: the assertion '%s' failed.",       \
                 __FUNCTION__, __FILE__, __LINE__, #check);                 \
    return SCRIPT_FCDB_ASSERT_CAT(, __VA_ARGS__);                           \
  }
#else
#define SCRIPT_FCDB_ASSERT(check, ...)
#endif

/* script_fcdb_error on failed check */
#define SCRIPT_FCDB_CHECK(check, msg, ...)                                  \
  if (!(check)) {                                                           \
    script_fcdb_error(msg);                                                 \
    return SCRIPT_FCDB_ASSERT_CAT(, __VA_ARGS__);                           \
  }

/* script_fcdb_arg_error on failed check */
#define SCRIPT_FCDB_CHECK_ARG(check, narg, msg, ...)                        \
  if (!(check)) {                                                           \
    script_fcdb_arg_error(narg, msg);                                       \
    return SCRIPT_FCDB_ASSERT_CAT(, __VA_ARGS__);                           \
  }

/* script_fcdb_arg_error on nil value */
#define SCRIPT_FCDB_CHECK_ARG_NIL(value, narg, type, ...)                   \
  if ((value) == NULL) {                                                    \
    script_fcdb_arg_error(narg, "got 'nil', '" #type "' expected");         \
    return SCRIPT_FCDB_ASSERT_CAT(, __VA_ARGS__);                           \
  }

/* script_fcdb_arg_error on nil value */
#define SCRIPT_FCDB_CHECK_SELF(value, ...)                                  \
  if ((value) == NULL) {                                                    \
    script_fcdb_arg_error(1, "got 'nil' for self");                         \
    return SCRIPT_FCDB_ASSERT_CAT(, __VA_ARGS__);                           \
  }

#endif /* FC__SCRIPT_FCDB_FCDB_H */
