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

#ifndef FC__GAMELOG_H
#define FC__GAMELOG_H

#include "shared.h"		/* fc__attribute */

enum {
  GAMELOG_BEGIN,
  GAMELOG_END,
  GAMELOG_JUDGE,
  GAMELOG_MAP,
  GAMELOG_PLAYER,
  GAMELOG_TEAM,
  GAMELOG_WONDER,
  GAMELOG_FOUNDCITY,
  GAMELOG_LOSECITY,
  GAMELOG_DISBANDCITY,
  GAMELOG_TECH,
  GAMELOG_EMBASSY,
  GAMELOG_GOVERNMENT,
  GAMELOG_REVOLT,
  GAMELOG_GENO,
  GAMELOG_TREATY,
  GAMELOG_DIPLSTATE,
  GAMELOG_STATUS,
  GAMELOG_FULL,		/* placeholder */
  GAMELOG_INFO,
  GAMELOG_UNITLOSS,
  GAMELOG_UNITGAMELOSS,
  GAMELOG_BUILD,
  GAMELOG_RATECHANGE,
  GAMELOG_EVERYTHING,
  GAMELOG_DEBUG
};

/* end game states */
enum {
  GL_NONE,
  GL_DRAW,
  GL_LONEWIN,
  GL_TEAMWIN,
  GL_ALLIEDWIN
};

/* treaty clause types */
enum {
  GL_EMBASSY,
  GL_TECH,
  GL_GOLD,
  GL_MAP,
  GL_SEAMAP,
  GL_CITY,
  GL_CEASEFIRE,
  GL_PEACE,
  GL_ALLIANCE,
  GL_TEAM,
  GL_VISION
};

void gamelog_init(char *filename);
void gamelog_set_level(int level);
void gamelog(int level, ...);

extern int gamelog_level;

#endif  /* FC__GAMELOG_H */
