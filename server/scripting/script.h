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

#ifndef FC__SCRIPT_H
#define FC__SCRIPT_H

/* server/scripting */
#include "script_signal.h"

/* utility */
#include "support.h"            /* fc__attribute() */

struct section_file;

/* internal api error functions */
int script_error(const char *fmt, ...)
    fc__attribute((__format__ (__printf__, 1, 2)));

int script_arg_error(int narg, const char *msg);

/* callback invocation function. */
bool script_callback_invoke(const char *callback_name,
			    int nargs, va_list args);

void script_remove_exported_object(void *object);

/* script functions. */
bool script_init(void);
void script_free(void);
bool script_do_string(const char *str);
bool script_do_file(const char *filename);

/* script state i/o. */
void script_state_load(struct section_file *file);
void script_state_save(struct section_file *file);


/* Returns additional arguments on failure. */
#define SCRIPT_ASSERT_CAT(str1, str2) str1 ## str2

/* Script assertion (for debugging only) */
#ifdef DEBUG
#define SCRIPT_ASSERT(check, ...)                                           \
  if (!(check)) {                                                           \
    script_error("in %s() [%s::%d]: the assertion '%s' failed.",            \
                 __FUNCTION__, __FILE__, __LINE__, #check);                 \
    return SCRIPT_ASSERT_CAT(, __VA_ARGS__);                                \
  }
#else
#define SCRIPT_ASSERT(check, ...)
#endif

/* script_error on failed check */
#define SCRIPT_CHECK(check, msg, ...)                                       \
  if (!(check)) {                                                           \
    script_error(msg);                                                      \
    return SCRIPT_ASSERT_CAT(, __VA_ARGS__);                                \
  }

/* script_arg_error on failed check */
#define SCRIPT_CHECK_ARG(check, narg, msg, ...)                             \
  if (!(check)) {                                                           \
    script_arg_error(narg, msg);                                            \
    return SCRIPT_ASSERT_CAT(, __VA_ARGS__);                                \
  }

/* script_arg_error on nil value */
#define SCRIPT_CHECK_ARG_NIL(value, narg, type, ...)                        \
  if ((value) == NULL) {                                                    \
    script_arg_error(narg, "got 'nil', '" #type "' expected");              \
    return SCRIPT_ASSERT_CAT(, __VA_ARGS__);                                \
  }

/* script_arg_error on nil value */
#define SCRIPT_CHECK_SELF(value, ...)                                       \
  if ((value) == NULL) {                                                    \
    script_arg_error(1, "got 'nil' for self");                              \
    return SCRIPT_ASSERT_CAT(, __VA_ARGS__);                                \
  }

#endif /* FC__SCRIPT_H */

