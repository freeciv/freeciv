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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* utility */
#include "deprecations.h"

/* common/scriptcore */
#include "luascript.h"

/* server/scripting */
#include "script_fcdb.h"

#include "api_fcdb_base.h"

#define OPTION_DEPR_PREFIX "#deprecated."

/**********************************************************************//**
  Return the value for the fcdb setting 'type'.
**************************************************************************/
const char *api_fcdb_option(lua_State *L, const char *type)
{
  LUASCRIPT_CHECK_STATE(L, NULL);
  LUASCRIPT_CHECK_ARG_NIL(L, type, 2, string, NULL);

  if (!fc_strncasecmp(OPTION_DEPR_PREFIX, type,
                      strlen(OPTION_DEPR_PREFIX))) {
    type = type + strlen(OPTION_DEPR_PREFIX);
    log_deprecation("Option name for fdb.option(\"%s\") given "
                    "in a way deprecated since 2.5. "
                    "Use literal string instead.", type);
  }

  return fcdb_option_get(type);
}
