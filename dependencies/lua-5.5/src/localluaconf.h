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
#ifndef FC__LOCALLUACONF_H
#define FC__LOCALLUACONF_H

#ifdef HAVE_CONFIG_H
#include <liblua_config.h>
#endif

#if defined(HAVE_MKSTEMP) && defined(FREECIV_HAVE_UNISTD_H)
#define LUA_USE_MKSTEMP
#endif
#if defined(HAVE_POPEN) && defined(HAVE_PCLOSE)
#define LUA_USE_POPEN
#endif
#if defined(HAVE__LONGJMP) && defined(HAVE__SETJMP)
#define LUA_USE_ULONGJMP
#endif
#if defined(HAVE_GMTIME_R) && defined(HAVE_LOCALTIME_R)
#define LUA_USE_GMTIME_R
#endif
#if defined(HAVE_FSEEKO)
#define LUA_USE_FSEEKO
#endif

#endif /* FC__LOCALLUACONF_H */
