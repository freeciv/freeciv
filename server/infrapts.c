/***********************************************************************
 Freeciv - Copyright (C) 1996 - 2004 The Freeciv Project Team
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

/* common */
#include "map.h"

/* server */
#include <hand_gen.h>       /* <> so looked from the build directory first. */
#include "maphand.h"
#include "notify.h"
#include "plrhand.h"

/************************************************************************//**
  Handle player_place_infra packet
****************************************************************************/
void handle_player_place_infra(struct player *pplayer, int tile, int extra)
{
  struct tile *ptile;
  struct extra_type *pextra;

  if (!terrain_control.infrapoints) {
    return;
  }

  ptile = index_to_tile(&(wld.map), tile);
  pextra = extra_by_number(extra);

  if (ptile == NULL || pextra == NULL) {
    return;
  }

  if (!map_is_known_and_seen(ptile, pplayer, V_MAIN)) {
    notify_player(pplayer, NULL, E_INFRAPOINTS, ftc_server,
                  _("Cannot place %s to unseen tile."),
                  extra_name_translation(pextra));
    return;
  }

  if (pplayer->economic.infra_points < pextra->infracost) {
    notify_player(pplayer, NULL, E_INFRAPOINTS, ftc_server,
                  _("Cannot place %s for lack of infrapoints."),
                  extra_name_translation(pextra));
    return;
  }

  if (!player_can_place_extra(pextra, pplayer, ptile)) {
    notify_player(pplayer, NULL, E_INFRAPOINTS, ftc_server,
                  _("Cannot place unbuildable %s."),
                  extra_name_translation(pextra));
    return;
  }

  pplayer->economic.infra_points -= pextra->infracost;
  send_player_info_c(pplayer, pplayer->connections);

  ptile->placing = pextra;

  if (pextra->build_time > 0) {
    ptile->infra_turns = pextra->build_time;
  } else {
    ptile->infra_turns = tile_terrain(ptile)->placing_time * pextra->build_time_factor;
  }

  /* update_tile_knowledge() would not know to send the tile
   * when only placing has changed, so send it explicitly. */
  send_tile_info(pplayer->connections, ptile, FALSE);
}
