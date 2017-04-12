/***********************************************************************
 Freeciv - Copyright (C) 2017 - Freeciv Development Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifndef FC_SERVER_SETTINGS_H
#define FC_SERVER_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Server setting types. */
#define SPECENUM_NAME sset_type
#define SPECENUM_VALUE0 SST_BOOL
#define SPECENUM_VALUE1 SST_INT
#define SPECENUM_VALUE2 SST_STRING
#define SPECENUM_VALUE3 SST_ENUM
#define SPECENUM_VALUE4 SST_BITWISE
#include "specenum_gen.h"

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif // FC_SERVER_SETTINGS_H
