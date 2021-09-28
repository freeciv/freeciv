/****************************************************************************
 Freeciv - Copyright (C) 2004 - The Freeciv Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
****************************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* utility */
#include "support.h"

#include "sex.h"

/************************************************************************//**
  Return sex by the name provided
****************************************************************************/
int sex_by_name(const char *name)
{
  if (!fc_strcasecmp("Male", name)) {
    return SEX_MALE;
  }

  if (!fc_strcasecmp("Female", name)) {
    return SEX_FEMALE;
  }

  return SEX_UNKNOWN;
}
