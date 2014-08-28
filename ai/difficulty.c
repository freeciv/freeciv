/***********************************************************************
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
#include <fc_config.h>
#endif

/* utility */
#include "bitvector.h"

/* common */
#include "player.h"

/* ai */
#include "handicaps.h"

#include "difficulty.h"

static bv_handicap handicap_of_skill_level(int level);
static int fuzzy_of_skill_level(int level);
static int science_cost_of_skill_level(int level);
static int expansionism_of_skill_level(int level);

/**************************************************************************
  Set an AI level and related quantities, with no feedback.
**************************************************************************/
void set_ai_level_directer(struct player *pplayer, enum ai_level level)
{
  handicaps_set(pplayer, handicap_of_skill_level(level));
  pplayer->ai_common.fuzzy = fuzzy_of_skill_level(level);
  pplayer->ai_common.expand = expansionism_of_skill_level(level);
  pplayer->ai_common.science_cost = science_cost_of_skill_level(level);
  pplayer->ai_common.skill_level = level;
}

/**************************************************************************
  Returns handicap bitvector for given AI skill level
**************************************************************************/
static bv_handicap handicap_of_skill_level(int level)
{
  bv_handicap handicap;

  fc_assert(level > 0 && level <= 10);

  BV_CLR_ALL(handicap);

  switch (level) {
   case AI_LEVEL_AWAY:
     BV_SET(handicap, H_AWAY);
     BV_SET(handicap, H_FOG);
     BV_SET(handicap, H_MAP);
     BV_SET(handicap, H_RATES);
     BV_SET(handicap, H_TARGETS);
     BV_SET(handicap, H_HUTS);
     BV_SET(handicap, H_REVOLUTION);
     break;
   case AI_LEVEL_NOVICE:
   case AI_LEVEL_HANDICAPPED:
     BV_SET(handicap, H_RATES);
     BV_SET(handicap, H_TARGETS);
     BV_SET(handicap, H_HUTS);
     BV_SET(handicap, H_NOPLANES);
     BV_SET(handicap, H_DIPLOMAT);
     BV_SET(handicap, H_LIMITEDHUTS);
     BV_SET(handicap, H_DEFENSIVE);
     BV_SET(handicap, H_REVOLUTION);
     BV_SET(handicap, H_EXPANSION);
     BV_SET(handicap, H_DANGER);
     break;
   case AI_LEVEL_EASY:
     BV_SET(handicap, H_RATES);
     BV_SET(handicap, H_TARGETS);
     BV_SET(handicap, H_HUTS);
     BV_SET(handicap, H_NOPLANES);
     BV_SET(handicap, H_DIPLOMAT);
     BV_SET(handicap, H_LIMITEDHUTS);
     BV_SET(handicap, H_DEFENSIVE);
     BV_SET(handicap, H_DIPLOMACY);
     BV_SET(handicap, H_REVOLUTION);
     BV_SET(handicap, H_EXPANSION);
     break;
   case AI_LEVEL_NORMAL:
     BV_SET(handicap, H_RATES);
     BV_SET(handicap, H_TARGETS);
     BV_SET(handicap, H_HUTS);
     BV_SET(handicap, H_DIPLOMAT);
     break;
   case AI_LEVEL_EXPERIMENTAL:
     BV_SET(handicap, H_EXPERIMENTAL);
     break;
   case AI_LEVEL_CHEATING:
     BV_SET(handicap, H_RATES);
     break;
  }

  return handicap;
}

/**************************************************************************
  Return the AI fuzziness (0 to 1000) corresponding to a given skill
  level (1 to 10).  See ai_fuzzy() in common/player.c
**************************************************************************/
static int fuzzy_of_skill_level(int level)
{
  int f[11] = { -1, 0, 400/*novice*/, 300/*easy*/, 0, 0, 0, 0, 0, 0, 0 };

  fc_assert_ret_val(level > 0 && level <= 10, 0);
  return f[level];
}

/**************************************************************************
  Return the AI's science development cost; a science development cost of 100
  means that the AI develops science at the same speed as a human; a science
  development cost of 200 means that the AI develops science at half the speed
  of a human, and a sceence development cost of 50 means that the AI develops
  science twice as fast as the human.
**************************************************************************/
static int science_cost_of_skill_level(int level)
{
  int x[11] = { -1, 100, 250/*novice*/, 100/*easy*/, 100, 100, 100, 100,
                100, 100, 100 };

  fc_assert_ret_val(level > 0 && level <= 10, 0);
  return x[level];
}

/**************************************************************************
  Return the AI expansion tendency, a percentage factor to value new cities,
  compared to defaults.  0 means _never_ build new cities, > 100 means to
  (over?)value them even more than the default (already expansionistic) AI.
**************************************************************************/
static int expansionism_of_skill_level(int level)
{
  int x[11] = { -1, 100, 10/*novice*/, 10/*easy*/, 100, 100, 100, 100,
                100, 100, 100 };

  fc_assert_ret_val(level > 0 && level <= 10, 0);
  return x[level];
}
