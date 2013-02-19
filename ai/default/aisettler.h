/********************************************************************** 
 Freeciv - Copyright (C) 2002 - The Freeciv Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef FC__AISETTLER_H
#define FC__AISETTLER_H

/* utility */
#include "support.h"            /* bool type */

/* common */
#include "city.h"
#include "fc_types.h"

struct ai_data;
struct ai_plr;
struct tile_data_cache;

void ai_auto_settler_init(struct ai_plr *ai);
void ai_auto_settler_free(struct ai_plr *ai);

void dai_auto_settler_reset(struct player *pplayer);
void dai_auto_settler_run(struct player *pplayer, struct unit *punit,
                          struct settlermap *state);
void dai_auto_settler_cont(struct player *pplayer, struct unit *punit,
                           struct settlermap *state);

void contemplate_new_city(struct city *pcity);

#endif /* FC__AISETTLER_H */
