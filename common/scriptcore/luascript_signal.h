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

#ifndef FC__SCRIPT_SIGNAL_H
#define FC__SCRIPT_SIGNAL_H

#include <stdarg.h>

struct section_file;

void script_signal_emit_valist(const char *signal_name, int nargs,
                               va_list args);
void script_signal_create_valist(const char *signal_name, int nargs,
                                 va_list args);
void script_signal_create(const char *signal_name, int nargs, ...);
void script_signal_connect(const char *signal_name,
                           const char *callback_name);

void script_signals_init(void);
void script_signals_free(void);

#endif

