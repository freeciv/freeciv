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

#ifndef FC__BARBARIAN_H
#define FC__BARBARIAN_H

#include "player.h"

#define MIN_UNREST_DIST   5
#define MAX_UNREST_DIST   8

#define UPRISE_CIV_SIZE  10
#define UPRISE_CIV_MORE  30
#define UPRISE_CIV_MOST  50

#define LAND_BARBARIAN    1
#define SEA_BARBARIAN     2

#define BARBARIAN_LIFE    5

#define MAP_FACTOR     2000  /* adjust this to get a good uprising frequency */

int unleash_barbarians(struct player* vict, int x, int y);
void summon_barbarians(void);
int is_land_barbarian(struct player *pplayer);
int is_sea_barbarian(struct player *pplayer);

#endif  /* FC__BARBARIAN_H */
