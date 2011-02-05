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
#ifndef FC__AIDATA_H
#define FC__AIDATA_H

/* utility */
#include "support.h"

struct player;

struct ai_plr
{
  bool phase_initialized;

  int last_num_continents;
  int last_num_oceans;

  /* Keep track of available ocean channels */
  bool *channels;
};

void ai_data_init(struct player *pplayer);
void ai_data_close(struct player *pplayer);

void ai_data_phase_begin(struct player *pplayer, bool is_new_phase);
void ai_data_phase_finished(struct player *pplayer);

struct ai_plr *ai_plr_data_get(struct player *pplayer);

bool ai_channel(struct player *pplayer, Continent_id c1, Continent_id c2);

#endif /* FC__AIDATA_H */
