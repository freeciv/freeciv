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
#ifndef FC__USER_H
#define FC__USER_H

#include "shared.h"

struct user {
  char name[MAX_LEN_NAME];
  char password[MAX_LEN_NAME];

  /* add more fields here as warranted */
};

#endif /* FC__USER_H */
