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

#include "city.h"
#include "log.h"
#include "shared.h"
#include "support.h"
#include "unit.h"

#include "gotohand.h"

#include "ailog.h"

/* General AI logging functions */

/**************************************************************************
  Log city messages, they will appear like this
    2: c's Romenna(5,35) [s1 d106 u11 g1] must have Archers ...
**************************************************************************/
void CITY_LOG(int level, struct city *pcity, const char *msg, ...)
{
  char buffer[500];
  char buffer2[500];
  va_list ap;
  int minlevel = MIN(LOGLEVEL_CITY, level);

  if (minlevel > fc_log_level) {
    return;
  }

  my_snprintf(buffer, sizeof(buffer), "%s's %s(%d,%d) [s%d d%d u%d g%d] ",
              city_owner(pcity)->name, pcity->name,
              pcity->x, pcity->y, pcity->size,
              pcity->ai.danger, pcity->ai.urgency,
              pcity->ai.grave_danger);

  va_start(ap, msg);
  my_vsnprintf(buffer2, sizeof(buffer2), msg, ap);
  va_end(ap);

  freelog(minlevel, "%s%s", buffer, buffer2);
}

/**************************************************************************
  Log unit messages, they will appear like this
    2: c's Archers[139] (5,35)->(0,0){0} stays to defend city
**************************************************************************/
void UNIT_LOG(int level, struct unit *punit, const char *msg, ...)
{
  char buffer[500];
  char buffer2[500];
  va_list ap;
  int minlevel = MIN(LOGLEVEL_UNIT, level);

  if (minlevel > fc_log_level) {
    return;
  }

  my_snprintf(buffer, sizeof(buffer), "%s's %s[%d] (%d,%d)->(%d,%d){%d} ",
              unit_owner(punit)->name, unit_type(punit)->name,
              punit->id, punit->x, punit->y,
              punit->goto_dest_x, punit->goto_dest_y, 
              punit->ai.bodyguard);

  va_start(ap, msg);
  my_vsnprintf(buffer2, sizeof(buffer2), msg, ap);
  va_end(ap);

  freelog(minlevel, "%s%s", buffer, buffer2);
}

/**************************************************************************
  Log goto message if the goto fails. They will appear like this
    2: a's Explorer[105] on GOTO (3,25)->(5,23) failed : exploring territory
**************************************************************************/
void GOTO_LOG(int level, struct unit *punit, enum goto_result result, 
              const char *msg, ...)
{
  int minlevel = MIN(LOGLEVEL_GOTO, level);

  if (minlevel <= fc_log_level && (result == GR_FAILED || result == GR_FOUGHT)) {
    char buffer[500];
    char buffer2[500];
    va_list ap;

    my_snprintf(buffer, sizeof(buffer),
                "%s's %s[%d] on GOTO (%d,%d)->(%d,%d) %s : ",
                unit_owner(punit)->name, unit_type(punit)->name,
                punit->id, punit->x, punit->y,
                punit->goto_dest_x, punit->goto_dest_y,
               (result == GR_FAILED) ? "failed" : "fought");

    va_start(ap, msg);
    my_vsnprintf(buffer2, sizeof(buffer2), msg, ap);
    va_end(ap);

    freelog(minlevel, "%s%s", buffer, buffer2);
  }
}

/**************************************************************************
  Log message for bodyguards. They will appear like this
    2: ai4's bodyguard Mech. Inf.[485] (38,22){Riflemen:574@37,23} was ...
  note that these messages are likely to wrap if long.
**************************************************************************/
void BODYGUARD_LOG(int level, struct unit *punit, const char *msg)
{
  char buffer[500];
  int minlevel = MIN(LOGLEVEL_BODYGUARD, level);
  struct unit *pcharge;
  struct city *pcity;
  int x = -1, y = -1, id = -1;
  char *s = "none";

  if (minlevel > fc_log_level) {
    return;
  }

  pcity = find_city_by_id(punit->ai.charge);
  pcharge = find_unit_by_id(punit->ai.charge);
  if (pcharge) {
    x = pcharge->x;
    y = pcharge->y;
    id = pcharge->id;
    s = unit_type(pcharge)->name;
  } else if (pcity) {
    x = pcity->x;
    y = pcity->y;
    id = pcity->id;
    s = pcity->name;
  }
  my_snprintf(buffer, sizeof(buffer),
              "%s's bodyguard %s[%d] (%d,%d){%s:%d@%d,%d} ",
              unit_owner(punit)->name, unit_type(punit)->name,
              punit->id, punit->x, punit->y, s, id, x, y);

  freelog(minlevel, "%s%s", buffer, msg);
}
