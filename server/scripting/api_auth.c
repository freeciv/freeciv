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

/* utility */
#include "log.h"

/* common */
#include "connection.h"

/* server */
#include "auth.h"

/* server/scripting */
#include "script_fcdb.h"

#include "api_auth.h"

/**************************************************************************
  ...
**************************************************************************/
const char *api_auth_get_username(Connection *pconn)
{
  SCRIPT_FCDB_CHECK_SELF(pconn, NULL);
  fc_assert_ret_val(conn_is_valid(pconn), NULL);

  return auth_get_username(pconn);
}

/**************************************************************************
  ...
**************************************************************************/
const char *api_auth_get_ipaddr(Connection *pconn)
{
  SCRIPT_FCDB_CHECK_SELF(pconn, NULL);
  fc_assert_ret_val(conn_is_valid(pconn), NULL);

  return auth_get_ipaddr(pconn);
}

/**************************************************************************
  ...
**************************************************************************/
bool api_auth_set_password(Connection *pconn, const char *password)
{
  SCRIPT_FCDB_CHECK_SELF(pconn, FALSE);
  fc_assert_ret_val(conn_is_valid(pconn), FALSE);

  return auth_set_password(pconn, password);
}

/**************************************************************************
  ...
**************************************************************************/
const char *api_auth_get_password(Connection *pconn)
{
  SCRIPT_FCDB_CHECK_SELF(pconn, NULL);
  fc_assert_ret_val(conn_is_valid(pconn), NULL);

  return auth_get_password(pconn);
}
