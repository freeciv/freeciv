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

#include <assert.h>

#include "plrdlg_g.h"

#include "plrdlg_common.h"

static int frozen_level = 0;

/******************************************************************
 Turn off updating of player dialog
*******************************************************************/
void plrdlg_freeze(void)
{
  frozen_level++;
}

/******************************************************************
 Turn on updating of player dialog
*******************************************************************/
void plrdlg_thaw(void)
{
  frozen_level--;
  assert(frozen_level >= 0);
  if (frozen_level == 0) {
    update_players_dialog();
  }
}

/******************************************************************
 Turn on updating of player dialog
*******************************************************************/
void plrdlg_force_thaw(void)
{
  frozen_level = 1;
  plrdlg_thaw();
}

/******************************************************************
 ...
*******************************************************************/
bool is_plrdlg_frozen(void)
{
  return frozen_level > 0;
}
