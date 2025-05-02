/***********************************************************************
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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif /* HAVE_CONFIG_H */

#include "fc_prehdrs.h"

#ifdef FREECIV_MSWINDOWS
#include <windows.h>
#endif

#include <stdio.h>

/* utility */
#include "support.h"

#include "executable.h"

/********************************************************************//**
  Common part of executable initialization
************************************************************************/
void executable_init(void)
{
  /* Load Windows post-crash debugger */
#ifdef FREECIV_MSWINDOWS
# ifndef FREECIV_NDEBUG
  if (LoadLibrary("exchndl.dll") == nullptr) {
#  ifdef FREECIV_DEBUG
    fprintf(stderr, "exchndl.dll could not be loaded, no crash debugger\n");
#  endif /* FREECIV_DEBUG */
  }
# endif /* FREECIV_NDEBUG */
#endif /* FREECIV_MSWINDOWS */
}
