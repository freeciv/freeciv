/********************************************************************** 
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifndef FC__SUPPORT_H
#define FC__SUPPORT_H

/********************************************************************** 
  Replacements for functions which are not available on all platforms.
  Where the functions are available natively, these are just wrappers.
  See also some functions in shared.h (some of which should be moved
  here), and functions/macros in mem.h.  See support.c for more
  comments.
***********************************************************************/

#include <stdlib.h>		/* size_t */
#include <stdarg.h>

#include "attribute.h"

int my_snprintf(char *str, size_t n, const char *format, ...)
     fc__attribute((format (printf, 3, 4)));

int my_vsnprintf(char *str, size_t n, const char *format, va_list ap );

#endif  /* FC__SUPPORT_H */
