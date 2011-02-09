/********************************************************************** 
 Freeciv - Copyright (C) 2002 - The Freeciv Project
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
#include "player.h"

/* server */
#include "notify.h"

/* ai */
#include "aidata.h"

#include "ailog.h"


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
