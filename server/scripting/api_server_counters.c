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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* utility */
#include "fciconv.h"

/* common */
#include "city.h"
#include "counters.h"
#include "name_translation.h"

/* common/scriptcore */
#include "luascript.h"

/* server */
#include "cityturn.h"

#include "api_server_counters.h"


/**********************************************************************//**
  Increase value of the given counter for given city.
**************************************************************************/
void api_counter_increase(lua_State *L, Counter *c, City *city)
{
  LUASCRIPT_CHECK_ARG_NIL(L, city, 3, City, );

  if (CB_USER != c->type) {
    fc_printf("%s\n", "[CB_USER == c->type] Lua: cannot change non-user counter");
    return;
  }

  city->counter_values[counter_index(c)]++;

  city_counters_refresh(city);
}

/**********************************************************************//**
  Reset value of the given counter for given city.
**************************************************************************/
void api_counter_zero(lua_State *L, Counter *c, City *city)
{
  LUASCRIPT_CHECK_ARG_NIL(L, city, 3, City, );

  if (CB_USER != c->type) {
    fc_printf("%s\n", "[CB_USER == c->type] Lua: cannot change non-user counter");
    return;
  }

  city->counter_values[counter_index(c)] = 0;

  city_counters_refresh(city);
}
