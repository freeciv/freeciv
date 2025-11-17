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

/* utility */
#include "rand.h" /* fc_rand() */

/* common */
#include "ai.h"
#include "game.h"
#include "map.h"
#include "movement.h"
#include "player.h"
#include "research.h"
#include "tech.h"
#include "tile.h"

/* server */
#include "aiiface.h"
#include "barbarian.h"
#include "nation.h"
#include "plrhand.h"
#include "srv_main.h"
#include "stdinhand.h"
#include "techtools.h"
#include "unittools.h"

/* ai */
#include "difficulty.h"

#include "animals.h"

/************************************************************************//**
  Return a randon suitable animal type for the terrain
****************************************************************************/
static const struct unit_type *animal_for_terrain(struct terrain *pterr)
{
  if (pterr->num_animals == 0) {
    return nullptr;
  } else {
    return pterr->animals[fc_rand(pterr->num_animals)];
  }
}

/************************************************************************//**
  Try to add one animal to the map.
****************************************************************************/
static void place_animal(struct player *plr, int sqrdist)
{
  struct tile *ptile = rand_map_pos(&(wld.map));
  const struct unit_type *ptype;

  extra_type_by_rmcause_iterate(ERM_ENTER, pextra) {
    if (tile_has_extra(ptile, pextra)) {
      /* Animals should not displace huts */
      /* FIXME: could animals not entering nor frightening huts appear here? */
      return;
    }
  } extra_type_by_rmcause_iterate_end;

  if (unit_list_size(ptile->units) > 0) {
    /* Below we check against enemy units nearby. Here we make sure
     * there's no multiple animals in the very same tile. */
    return;
  }

  circle_iterate(&(wld.map), ptile, sqrdist, check) {
    if (tile_city(check) != NULL
        || is_non_allied_unit_tile(check, plr, TRUE)) {
      return;
    }
  } circle_iterate_end;

  ptype = animal_for_terrain(tile_terrain(ptile));

  if (ptype != NULL && !utype_player_already_has_this_unique(plr, ptype)) {
    struct unit *punit;

    fc_assert_ret(can_exist_at_tile(&(wld.map), ptype, ptile));

    punit = create_unit(plr, ptile, ptype, 0, 0, -1);

    send_unit_info(NULL, punit);
  }
}

/************************************************************************//**
  Create animal kingdom player and their units.
****************************************************************************/
void create_animals(void)
{
  struct nation_type *anination;
  struct player *plr;
  struct research *presearch;
  int i;

  if (wld.map.server.animals <= 0) {
    return;
  }

  anination = pick_a_nation(NULL, FALSE, TRUE, ANIMAL_BARBARIAN);

  if (anination == NO_NATION_SELECTED) {
    return;
  }

  plr = server_create_player(-1, default_ai_type_name(), NULL, FALSE);
  if (plr == NULL) {
    return;
  }
  /* Freeciv-web depends on AI-status being set already before server_player_init() */
  set_as_ai(plr);
  server_player_init(plr, TRUE, TRUE);

  player_set_nation(plr, anination);
  player_nation_defaults(plr, anination, TRUE);

  assign_player_colors();

  server.nbarbarians++;

  sz_strlcpy(plr->username, _(ANON_USER_NAME));
  plr->unassigned_user = TRUE;
  plr->is_connected = FALSE;
  plr->government = init_government_of_nation(anination);
  plr->economic.gold = 100;

  plr->phase_done = TRUE;

  plr->ai_common.barbarian_type = ANIMAL_BARBARIAN;
  set_ai_level_directer(plr, game.info.skill_level);

  presearch = research_get(plr);
  init_tech(presearch, TRUE);
  give_initial_techs(presearch, 0);

  /* Ensure that we are at war with everyone else */
  barbarian_initial_wars(plr);

  CALL_PLR_AI_FUNC(gained_control, plr, plr);

  send_player_all_c(plr, NULL);
  /* Send research info after player info, else the client will complain
   * about invalid team. */
  send_research_info(presearch, NULL);

  for (i = 0;
       i < MAP_NATIVE_WIDTH * MAP_NATIVE_HEIGHT * wld.map.server.animals / 1000;
       i++) {
    place_animal(plr, 2 * 2 + 1 * 1);
  }
}
