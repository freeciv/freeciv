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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>

#include "connection.h"
#include "fcintl.h"
#include "game.h"
#include "support.h"

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

/******************************************************************
 ...
*******************************************************************/
const char *get_ping_time_text(struct player *pplayer)
{
  static char buffer[32];

  if (conn_list_size(&pplayer->connections) > 0
      && conn_list_get(&pplayer->connections, 0)->ping_time != -1.0) {
    double ping_time_in_ms =
	1000 * conn_list_get(&pplayer->connections, 0)->ping_time;

    my_snprintf(buffer, sizeof(buffer), _("%6d.%02d ms"),
		(int) ping_time_in_ms,
		((int) (ping_time_in_ms * 100.0)) % 100);
  } else {
    buffer[0] = '\0';
  }
  return buffer;
}
