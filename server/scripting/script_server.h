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

#ifndef FC__SCRIPT_GAME_H
#define FC__SCRIPT_GAME_H

/* utility */
#include "support.h"            /* fc__attribute() */

/* common/scripting */
#include "luascript_signal.h"
#include "luascript_types.h"

struct section_file;

void script_remove_exported_object(void *object);

/* script functions. */
bool script_init(void);
void script_free(void);
bool script_do_string(const char *str);
bool script_do_file(const char *filename);

/* script state i/o. */
void script_state_load(struct section_file *file);
void script_state_save(struct section_file *file);

#endif /* FC__SCRIPT_GAME_H */

