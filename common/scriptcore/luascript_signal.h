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
#ifndef FC__LUASCRIPT_SIGNAL_H
#define FC__LUASCRIPT_SIGNAL_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* utility */
#include "support.h"

struct fc_lua;

void luascript_signal_init(struct fc_lua *fcl);
void luascript_signal_free(struct fc_lua *fcl);

void luascript_signal_emit_valist(struct fc_lua *fcl, const char *signal_name,
                                  int nargs, va_list args);
void luascript_signal_emit(struct fc_lua *fcl, const char *signal_name,
                           int nargs, ...);
void luascript_signal_create_valist(struct fc_lua *fcl,
                                    const char *signal_name, int nargs,
                                    va_list args);
void luascript_signal_create(struct fc_lua *fcl, const char *signal_name,
                             int nargs, ...);
void luascript_signal_callback(struct fc_lua *fcl, const char *signal_name,
                               const char *callback_name, bool create);
bool luascript_signal_callback_defined(struct fc_lua *fcl,
                                       const char *signal_name,
                                       const char *callback_name);

const char *luascript_signal_by_index(struct fc_lua *fcl, int index);
const char *luascript_signal_callback_by_index(struct fc_lua *fcl,
                                               const char *signal_name,
                                               int index);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__LUASCRIPT_SIGNAL_H */
