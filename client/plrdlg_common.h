/********************************************************************** 
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
#ifndef FC__PLRDLG_COMMON_H
#define FC__PLRDLG_COMMON_H

#include "shared.h"		/* bool type */

void plrdlg_freeze(void);
void plrdlg_thaw(void);
void plrdlg_force_thaw(void);
bool is_plrdlg_frozen(void);

#endif  /* FC__PLRDLG_COMMON_H */
