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

#ifndef FC__API_AUTH_H
#define FC__API_AUTH_H

/* server/scripting */
#include "luascript_types.h"

const char *api_auth_get_username(Connection *pconn);

const char *api_auth_get_ipaddr(Connection *pconn);

bool api_auth_set_password(Connection *pconn, const char *pass);
const char *api_auth_get_password(Connection *pconn);

#endif /* FC__API_AUTH_H */

