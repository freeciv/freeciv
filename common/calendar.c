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

/* common */
#include "game.h"
#include "victory.h"

#include "calendar.h"

/***************************************************************
  Returns the next year in the game.
***************************************************************/
int game_next_year(int year)
{
  int increase = get_world_bonus(EFT_TURN_YEARS);
  const int slowdown = (victory_enabled(VC_SPACERACE)
			? get_world_bonus(EFT_SLOW_DOWN_TIMELINE) : 0);
  int fragment_years;

  if (game.info.year_0_hack) {
    /* hacked it to get rid of year 0 */
    year = 0;
    game.info.year_0_hack = FALSE;
  }

    /* !McFred: 
       - want year += 1 for spaceship.
    */

  /* test game with 7 normal AI's, gen 4 map, foodbox 10, foodbase 0: 
   * Gunpowder about 0 AD
   * Railroad  about 500 AD
   * Electricity about 1000 AD
   * Refining about 1500 AD (212 active units)
   * about 1750 AD
   * about 1900 AD
   */

  /* Note the slowdown operates even if Enable_Space is not active.  See
   * README.effects for specifics. */
  if (slowdown >= 3) {
    if (increase > 1) {
      increase = 1;
    }
  } else if (slowdown >= 2) {
    if (increase > 2) {
      increase = 2;
    }
  } else if (slowdown >= 1) {
    if (increase > 5) {
      increase = 5;
    }
  }

  if (game.info.calendar_fragments) {
    game.info.fragment_count += get_world_bonus(EFT_TURN_FRAGMENTS);
    fragment_years = game.info.fragment_count / game.info.calendar_fragments;

    increase += fragment_years;
    game.info.fragment_count -= fragment_years * game.info.calendar_fragments;
  }

  year += increase;

  if (year == 0 && game.info.calendar_skip_0) {
    year = 1;
    game.info.year_0_hack = TRUE;
  }

  return year;
}

/***************************************************************
  Advance the game year.
***************************************************************/
void game_advance_year(void)
{
  game.info.year = game_next_year(game.info.year);
  game.info.turn++;
}

/****************************************************************************
  Produce a statically allocated textual representation of the given
  year.
****************************************************************************/
const char *textyear(int year)
{
  static char y[32];

  if (year < 0) {
    /* TRANS: <year> <label> -> "1000 BC" */
    fc_snprintf(y, sizeof(y), _("%d %s"), -year,
                game.info.negative_year_label);
  } else {
    /* TRANS: <year> <label> -> "1000 AD" */
    fc_snprintf(y, sizeof(y), _("%d %s"), year,
                game.info.positive_year_label);
  }

  return y;
}
