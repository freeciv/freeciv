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

#ifndef FC__API_UTILITIES_H
#define FC__API_UTILITIES_H

/* server/scripting */
#include "script_types.h"

int api_utilities_random(int min, int max);

void api_utilities_log_base(int level, const char *message);

Direction api_utilities_str2dir(const char *dir);

#endif /* FC__API_UTILITIES_H */
