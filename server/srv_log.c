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

#include <stdarg.h>

/* utility */
#include "astring.h"
#include "log.h"
#include "shared.h"
#include "support.h"
#include "timing.h"

/* common */
#include "city.h"
#include "game.h"
#include "unit.h"

/* server */
#include "notify.h"
#include "srv_main.h"

/* server/advisors */
#include "advdata.h"

/* ai */
#include "aicity.h"
#include "aiunit.h"
#include "defaultai.h"

#include "srv_log.h"

static struct timer *aitimer[AIT_LAST][2];
static int recursion[AIT_LAST];

/* General AI logging functions */

/**************************************************************************
  Log player tech messages.
**************************************************************************/
void real_tech_log(const char *file, const char *function, int line,
                   enum log_level level, bool notify,
                   const struct player *pplayer, struct advance *padvance,
                   const char *msg, ...)
{
  char buffer[500];
  char buffer2[500];
  va_list ap;

  if (!valid_advance(padvance) || advance_by_number(A_NONE) == padvance) {
    return;
  }

  fc_snprintf(buffer, sizeof(buffer), "%s::%s (want %d, dist %d) ",
              player_name(pplayer),
              advance_name_by_player(pplayer, advance_number(padvance)),
              pplayer->ai_common.tech_want[advance_index(padvance)],
              num_unknown_techs_for_goal(pplayer, advance_number(padvance)));

  va_start(ap, msg);
  fc_vsnprintf(buffer2, sizeof(buffer2), msg, ap);
  va_end(ap);

  cat_snprintf(buffer, sizeof(buffer), "%s", buffer2);
  if (notify) {
    notify_conn(NULL, NULL, E_AI_DEBUG, ftc_log, "%s", buffer);
  }
  do_log(file, function, line, FALSE, level, "%s", buffer);
}

/**************************************************************************
  Log player messages, they will appear like this
    
  where ti is timer, co countdown and lo love for target, who is e.
**************************************************************************/
void real_diplo_log(const char *file, const char *function, int line,
                    enum log_level level, bool notify,
                    const struct player *pplayer,
                    const struct player *aplayer, const char *msg, ...)
{
  char buffer[500];
  char buffer2[500];
  va_list ap;
  const struct ai_dip_intel *adip;

  /* Don't use ai_data_get since it can have side effects. */
  adip = ai_diplomacy_get(pplayer, aplayer);

  fc_snprintf(buffer, sizeof(buffer), "%s->%s(l%d,c%d,d%d%s): ",
              player_name(pplayer),
              player_name(aplayer),
              pplayer->ai_common.love[player_index(aplayer)],
              adip->countdown,
              adip->distance,
              adip->is_allied_with_enemy ? "?" :
              (adip->at_war_with_ally ? "!" : ""));

  va_start(ap, msg);
  fc_vsnprintf(buffer2, sizeof(buffer2), msg, ap);
  va_end(ap);

  cat_snprintf(buffer, sizeof(buffer), "%s", buffer2);
  if (notify) {
    notify_conn(NULL, NULL, E_AI_DEBUG, ftc_log, "%s", buffer);
  }
  do_log(file, function, line, FALSE, level, "%s", buffer);
}

/**************************************************************************
  Log city messages, they will appear like this
    2: Polish Romenna(5,35) [s1 d106 u11 g1] must have Archers ...
**************************************************************************/
void real_city_log(const char *file, const char *function, int line,
                   enum log_level level, bool notify,
                   const struct city *pcity, const char *msg, ...)
{
  char buffer[500];
  char buffer2[500];
  va_list ap;
  struct ai_city *city_data = def_ai_city_data(pcity);

  fc_snprintf(buffer, sizeof(buffer), "%s %s(%d,%d) [s%d d%d u%d g%d] ",
              nation_rule_name(nation_of_city(pcity)),
              city_name(pcity),
              TILE_XY(pcity->tile), pcity->size,
              city_data->danger, city_data->urgency,
              city_data->grave_danger);

  va_start(ap, msg);
  fc_vsnprintf(buffer2, sizeof(buffer2), msg, ap);
  va_end(ap);

  cat_snprintf(buffer, sizeof(buffer), "%s", buffer2);
  if (notify) {
    notify_conn(NULL, NULL, E_AI_DEBUG, ftc_log, "%s", buffer);
  }
  do_log(file, function, line, FALSE, level, "%s", buffer);
}

/**************************************************************************
  Log unit messages, they will appear like this
    2: Polish Archers[139] (5,35)->(0,0){0,0} stays to defend city
  where [] is unit id, ()->() are coordinates present and goto, and
  {,} contains bodyguard and ferryboat ids.
**************************************************************************/
void real_unit_log(const char *file, const char *function, int line,
                   enum log_level level,  bool notify,
                   const struct unit *punit, const char *msg, ...)
{
  char buffer[500];
  char buffer2[500];
  va_list ap;
  int gx, gy;
  struct unit_ai *unit_data = def_ai_unit_data(punit);

  if (punit->goto_tile) {
    gx = punit->goto_tile->x;
    gy = punit->goto_tile->y;
  } else {
    gx = gy = -1;
  }
  
  fc_snprintf(buffer, sizeof(buffer),
	      "%s %s[%d] %s (%d,%d)->(%d,%d){%d,%d} ",
              nation_rule_name(nation_of_unit(punit)),
              unit_rule_name(punit),
              punit->id,
	      get_activity_text(punit->activity),
	      TILE_XY(punit->tile),
	      gx, gy,
              unit_data->bodyguard, unit_data->ferryboat);

  va_start(ap, msg);
  fc_vsnprintf(buffer2, sizeof(buffer2), msg, ap);
  va_end(ap);

  cat_snprintf(buffer, sizeof(buffer), "%s", buffer2);
  if (notify) {
    notify_conn(NULL, NULL, E_AI_DEBUG, ftc_log, "%s", buffer);
  }
  do_log(file, function, line, FALSE, level, "%s", buffer);
}

/**************************************************************************
  Log message for bodyguards. They will appear like this
    2: Polish Mech. Inf.[485] bodyguard (38,22){Riflemen:574@37,23} was ...
  note that these messages are likely to wrap if long.
**************************************************************************/
void real_bodyguard_log(const char *file, const char *function, int line,
                        enum log_level level,  bool notify,
                        const struct unit *punit, const char *msg, ...)
{
  char buffer[500];
  char buffer2[500];
  va_list ap;
  const struct unit *pcharge;
  const struct city *pcity;
  int id = -1;
  int charge_x = -1;
  int charge_y = -1;
  const char *type = "guard";
  const char *s = "none";
  struct unit_ai *unit_data = def_ai_unit_data(punit);

  pcity = game_city_by_number(unit_data->charge);
  pcharge = game_unit_by_number(unit_data->charge);
  if (pcharge) {
    charge_x = pcharge->tile->x;
    charge_y = pcharge->tile->y;
    id = pcharge->id;
    type = "bodyguard";
    s = unit_rule_name(pcharge);
  } else if (pcity) {
    charge_x = pcity->tile->x;
    charge_y = pcity->tile->y;
    id = pcity->id;
    type = "cityguard";
    s = city_name(pcity);
  }
  /* else perhaps the charge died */

  fc_snprintf(buffer, sizeof(buffer),
              "%s %s[%d] %s (%d,%d){%s:%d@%d,%d} ",
              nation_rule_name(nation_of_unit(punit)),
              unit_rule_name(punit),
              punit->id,
              type,
              TILE_XY(punit->tile),
              s, id, charge_x, charge_y);

  va_start(ap, msg);
  fc_vsnprintf(buffer2, sizeof(buffer2), msg, ap);
  va_end(ap);

  cat_snprintf(buffer, sizeof(buffer), "%s", buffer2);
  if (notify) {
    notify_conn(NULL, NULL, E_AI_DEBUG, ftc_log, "%s", buffer);
  }
  do_log(file, function, line, FALSE, level, "%s", buffer);
}

/**************************************************************************
  Measure the time between the calls.  Used to see where in the AI too
  much CPU is being used.
**************************************************************************/
void TIMING_LOG(enum ai_timer timer, enum ai_timer_activity activity)
{
  static int turn = -1;
  int i;

  if (turn == -1) {
    for (i = 0; i < AIT_LAST; i++) {
      aitimer[i][0] = new_timer(TIMER_CPU, TIMER_ACTIVE);
      aitimer[i][1] = new_timer(TIMER_CPU, TIMER_ACTIVE);
      recursion[i] = 0;
    }
  }

  if (game.info.turn != turn) {
    turn = game.info.turn;
    for (i = 0; i < AIT_LAST; i++) {
      clear_timer(aitimer[i][0]);
    }
    fc_assert(activity == TIMER_START);
  }

  if (activity == TIMER_START && recursion[timer] == 0) {
    start_timer(aitimer[timer][0]);
    start_timer(aitimer[timer][1]);
    recursion[timer]++;
  } else if (activity == TIMER_STOP && recursion[timer] == 1) {
    stop_timer(aitimer[timer][0]);
    stop_timer(aitimer[timer][1]);
    recursion[timer]--;
  }
}

/**************************************************************************
  Print results
**************************************************************************/
void TIMING_RESULTS(void)
{
  char buf[200];

#define AILOG_OUT(text, which)                                              \
  fc_snprintf(buf, sizeof(buf), "  %s: %g sec turn, %g sec game", text,     \
              read_timer_seconds(aitimer[which][0]),                        \
              read_timer_seconds(aitimer[which][1]));                       \
  log_test("%s", buf);                                                      \
  notify_conn(NULL, NULL, E_AI_DEBUG, ftc_log, "%s", buf);

  log_test("  --- AI timing results ---");
  notify_conn(NULL, NULL, E_AI_DEBUG, ftc_log,
              "  --- AI timing results ---");
  AILOG_OUT("Total AI time", AIT_ALL);
  AILOG_OUT("Movemap", AIT_MOVEMAP);
  AILOG_OUT("Units", AIT_UNITS);
  AILOG_OUT(" - Military", AIT_MILITARY);
  AILOG_OUT(" - Attack", AIT_ATTACK);
  AILOG_OUT(" - Defense", AIT_DEFENDERS);
  AILOG_OUT(" - Ferry", AIT_FERRY);
  AILOG_OUT(" - Rampage", AIT_RAMPAGE);
  AILOG_OUT(" - Bodyguard", AIT_BODYGUARD);
  AILOG_OUT(" - Recover", AIT_RECOVER);
  AILOG_OUT(" - Caravan", AIT_CARAVAN);
  AILOG_OUT(" - Hunter", AIT_HUNTER);
  AILOG_OUT(" - Airlift", AIT_AIRLIFT);
  AILOG_OUT(" - Diplomat", AIT_DIPLOMAT);
  AILOG_OUT(" - Air", AIT_AIRUNIT);
  AILOG_OUT(" - Explore", AIT_EXPLORER);
  AILOG_OUT("fstk", AIT_FSTK);
  AILOG_OUT("Settlers", AIT_SETTLERS);
  AILOG_OUT("Workers", AIT_WORKERS);
  AILOG_OUT("Government", AIT_GOVERNMENT);
  AILOG_OUT("Taxes", AIT_TAXES);
  AILOG_OUT("Cities", AIT_CITIES);
  AILOG_OUT(" - Buildings", AIT_BUILDINGS);
  AILOG_OUT(" - Danger", AIT_DANGER);
  AILOG_OUT(" - Worker want", AIT_CITY_TERRAIN);
  AILOG_OUT(" - Military want", AIT_CITY_MILITARY);
  AILOG_OUT(" - Settler want", AIT_CITY_SETTLERS);
  AILOG_OUT("Citizen arrange", AIT_CITIZEN_ARRANGE);
  AILOG_OUT("Tech", AIT_TECH);
}
