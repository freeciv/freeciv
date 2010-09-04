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
#ifndef FC__SRV_LOG_H
#define FC__SRV_LOG_H

/* utility */
#include "bitvector.h"
#include "log.h"
#include "support.h"

/* common */
#include "fc_types.h"

struct ai_data;

/* 
 * Change these and remake to watch logs from a specific 
 * part of the AI code.
 */
#define LOGLEVEL_BODYGUARD LOG_DEBUG
#define LOGLEVEL_UNIT LOG_DEBUG
#define LOGLEVEL_GOTO LOG_DEBUG
#define LOGLEVEL_CITY LOG_DEBUG
#define LOGLEVEL_BUILD LOG_DEBUG
#define LOGLEVEL_HUNT LOG_DEBUG
#define LOGLEVEL_PLAYER LOG_DEBUG
#define LOGLEVEL_TECH LOG_DEBUG

#define LOG_AI_TEST LOG_NORMAL

enum ai_timer {
  AIT_ALL,
  AIT_MOVEMAP,
  AIT_UNITS,
  AIT_SETTLERS,
  AIT_WORKERS,
  AIT_AIDATA,
  AIT_GOVERNMENT,
  AIT_TAXES,
  AIT_CITIES,
  AIT_CITIZEN_ARRANGE,
  AIT_BUILDINGS,
  AIT_DANGER,
  AIT_TECH,
  AIT_FSTK,
  AIT_DEFENDERS,
  AIT_CARAVAN,
  AIT_HUNTER,
  AIT_AIRLIFT,
  AIT_DIPLOMAT,
  AIT_AIRUNIT,
  AIT_EXPLORER,
  AIT_EMERGENCY,
  AIT_CITY_MILITARY,
  AIT_CITY_TERRAIN,
  AIT_CITY_SETTLERS,
  AIT_ATTACK,
  AIT_MILITARY,
  AIT_RECOVER,
  AIT_BODYGUARD,
  AIT_FERRY,
  AIT_RAMPAGE,
  AIT_LAST
};

enum ai_timer_activity  {
  TIMER_START, TIMER_STOP
};

void real_tech_log(const char *file, const char *function, int line,
                   enum log_level level, bool notify,
                   const struct player *pplayer, struct advance *padvance,
                   const char *msg, ...)
                   fc__attribute((__format__ (__printf__, 8, 9)));
#define TECH_LOG(loglevel, pplayer, padvance, msg, ...)                     \
{                                                                           \
  bool notify = BV_ISSET(pplayer->server.debug, PLAYER_DEBUG_TECH);         \
  enum log_level level = (notify ? LOG_AI_TEST                              \
                          : MIN(loglevel, LOGLEVEL_TECH));                  \
  if (log_do_output_for_level(level)) {                                     \
    real_tech_log(__FILE__, __FUNCTION__, __LINE__, level, notify,          \
                  pplayer, padvance, msg, ## __VA_ARGS__);                  \
  }                                                                         \
}

void real_diplo_log(const char *file, const char *function, int line,
                    enum log_level level, bool notify,
                    const struct player *pplayer,
                    const struct player *aplayer, const char *msg, ...)
                   fc__attribute((__format__ (__printf__, 8, 9)));
#define DIPLO_LOG(loglevel, pplayer, aplayer, msg, ...)                     \
{                                                                           \
  bool notify = BV_ISSET(pplayer->server.debug, PLAYER_DEBUG_DIPLOMACY);    \
  enum log_level level = (notify ? LOG_AI_TEST                              \
                          : MIN(loglevel, LOGLEVEL_PLAYER));                \
  if (log_do_output_for_level(level)) {                                     \
    real_diplo_log(__FILE__, __FUNCTION__, __LINE__, level, notify,         \
                   pplayer, aplayer, msg, ## __VA_ARGS__);                  \
  }                                                                         \
}

void real_city_log(const char *file, const char *function, int line,
                   enum log_level level, bool notify,
                   const struct city *pcity, const char *msg, ...)
                   fc__attribute((__format__ (__printf__, 7, 8)));
#define CITY_LOG(loglevel, pcity, msg, ...)                                 \
{                                                                           \
  bool notify = pcity->server.debug;                                        \
  enum log_level level = (notify ? LOG_AI_TEST                              \
                          : MIN(loglevel, LOGLEVEL_CITY));                  \
  if (log_do_output_for_level(level)) {                                     \
    real_city_log(__FILE__, __FUNCTION__, __LINE__, level, notify,          \
                  pcity, msg, ## __VA_ARGS__);                              \
  }                                                                         \
}

void real_unit_log(const char *file, const char *function, int line,
                   enum log_level level,  bool notify,
                   const struct unit *punit, const char *msg, ...)
                   fc__attribute((__format__ (__printf__, 7, 8)));
#define UNIT_LOG(loglevel, punit, msg, ...)                                 \
{                                                                           \
  bool notify = punit->server.debug;                                        \
  enum log_level level;                                                     \
  if (!notify && tile_city(unit_tile(punit))                                \
      && tile_city(unit_tile(punit))->server.debug) {                       \
    level = LOG_AI_TEST;                                                    \
    notify = TRUE;                                                          \
  } else {                                                                  \
    level = MIN(loglevel, LOGLEVEL_UNIT);                                   \
  }                                                                         \
  if (log_do_output_for_level(level)) {                                     \
    real_unit_log(__FILE__, __FUNCTION__, __LINE__, level, notify,          \
                  punit, msg, ## __VA_ARGS__);                              \
  }                                                                         \
}

void real_bodyguard_log(const char *file, const char *function, int line,
                        enum log_level level,  bool notify,
                        const struct unit *punit, const char *msg, ...)
                        fc__attribute((__format__ (__printf__, 7, 8)));
#define BODYGUARD_LOG(loglevel, punit, msg, ...)                            \
{                                                                           \
  bool notify = punit->server.debug;                                        \
  enum log_level level = (notify ? LOG_AI_TEST                              \
                          : MIN(loglevel, LOGLEVEL_BODYGUARD));             \
  if (log_do_output_for_level(level)) {                                     \
    real_bodyguard_log(__FILE__, __FUNCTION__, __LINE__, level, notify,     \
                       punit, msg, ## __VA_ARGS__);                         \
  }                                                                         \
}

void TIMING_LOG(enum ai_timer timer, enum ai_timer_activity activity);
void TIMING_RESULTS(void);

#endif  /* FC__SRV_LOG_H */
