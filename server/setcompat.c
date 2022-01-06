/***********************************************************************
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
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
#include <fc_config.h>
#endif

/* ANSI */
#include <stdlib.h>

/* utility */
#include "support.h"

#include "setcompat.h"

struct set_name_compat {
  const char *old_name;
  const char *new_name;
};

static struct set_name_compat set_name_compat_S3_1_to_S3_2[] =
{
  { NULL, NULL }
};


/**********************************************************************//**
  Version agnostic helper function to find new name of a setting when
  updating between two versions.
**************************************************************************/
static const char *setcompat_name_generic(const char *old_name,
                                          struct set_name_compat *compats)
{
  int i;

  for (i = 0; compats[i].old_name != NULL; i++) {
    if (!fc_strcasecmp(old_name, compats[i].old_name)) {
      return compats[i].new_name;
    }
  }

  return old_name;
}

/**********************************************************************//**
  Find a 3.2 name of the setting with the given 3.1 name.
**************************************************************************/
const char *setcompat_S3_2_name_from_S3_1(const char *old_name)
{
  return setcompat_name_generic(old_name, set_name_compat_S3_1_to_S3_2);
}
