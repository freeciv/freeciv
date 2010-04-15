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

/* internal api error function. */
int script_error(const char *fmt, ...)
    fc__attribute((__format__ (__printf__, 1, 2)));

/* Returns additional arguments on failure. */
#define SCRIPT_ASSERT_CAT(str1, str2) str1 ## str2
#define SCRIPT_ASSERT(check, ...)                                           \
  if (!(check)) {                                                           \
    script_error("in %s() [%s::%d]: the assertion '%s' failed.",            \
                 __FUNCTION__, __FILE__, __LINE__, #check);                 \
    return SCRIPT_ASSERT_CAT(, __VA_ARGS__);                                \
  }

/* callback invocation function. */
bool script_callback_invoke(const char *callback_name,
			    int nargs, va_list args);

void script_remove_exported_object(void *object);

/* script functions. */
bool script_init(void);
void script_free(void);
bool script_do_file(const char *filename);

/* script state i/o. */
void script_state_load(struct section_file *file);
void script_state_save(struct section_file *file);

#endif

