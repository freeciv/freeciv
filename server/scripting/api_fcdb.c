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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* server/scripting */
#include "script_fcdb.h"

#include "api_fcdb.h"

/**************************************************************************
  Return the value for the fcdb setting 'type'.
**************************************************************************/
const char *api_fcdb_option(enum fcdb_option_type type)
{
  SCRIPT_FCDB_CHECK_ARG(fcdb_option_type_is_valid(type), 1,
                        "Invalid freeciv database option type.", NULL);

  return fcdb_option_get(type);
}

/**************************************************************************
  Return the value for the fcdb setting 'type'.
**************************************************************************/
void api_fcdb_error(const char *err_msg)
{
  script_fcdb_error("%s", err_msg);
}
