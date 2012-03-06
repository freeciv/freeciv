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

/* fcdb script functions. */
bool script_fcdb_init(const char *fcdb_luafile);
enum fcdb_status script_fcdb_call(const char *func_name, int nargs, ...);
void script_fcdb_free(void);

bool script_fcdb_do_string(struct connection *caller, const char *str);

#endif /* FC__SCRIPT_FCDB_H */
