/*****************************************************************************
 Freeciv - Copyright (C) 2023 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
*****************************************************************************/

#ifndef FC__API_GAME_COUNTERS_H
#define FC__API_GAME_COUNTERS_H

/* common/scriptcore */
#include "luascript_types.h"

struct lua_State;

void api_counter_increase(lua_State *L, Counter *c, City *city);
void api_counter_zero(lua_State *L, Counter *c, City *city);
#endif /* FCAPI_GAME_COUNTERS_H */
