/**********************************************************************
 Freeciv - Copyright (C) 2003 - M.C. Kaufman
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef FC__LOCKFILE_H
#define FC__LOCKFILE_H

#include "shared.h"

bool create_lock(void);
void remove_lock(void);

#endif /* FC__LOCKFILE_H */
