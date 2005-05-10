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

#include "api_methods.h"
#include "script.h"

bool api_methods_unit_type_has_role(Unit_Type *punit_type, const char *role)
{
  enum unit_role_id id = unit_role_from_str(role);

  if (id != L_LAST) {
    return unit_has_role(punit_type->index, id);
  } else {
    script_error("Unit role \"%s\" does not exist", role);
    return FALSE;
  }
}

