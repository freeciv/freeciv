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
#ifndef FC__AILOG_H
#define FC__AILOG_H

#include "fc_types.h"

#include "gotohand.h"		/* enum goto_result */

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

void PLAYER_LOG(int level, struct player *pplayer, struct ai_data *ai,
                const char *msg, ...)
     fc__attribute((__format__ (__printf__, 4, 5)));
void CITY_LOG(int level, struct city *pcity, const char *msg, ...)
     fc__attribute((__format__ (__printf__, 3, 4)));
void UNIT_LOG(int level, struct unit *punit, const char *msg, ...)
     fc__attribute((__format__ (__printf__, 3, 4)));
void BODYGUARD_LOG(int level, struct unit *punit, const char *msg);

#endif  /* FC__AILOG_H */
