/********************************************************************** 
 Freeciv - Copyright (C) 2004 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef FC__AIEXPLORER_H
#define FC__AIEXPLORER_H

#include "shared.h"		/* bool type */

#include "fc_types.h"

/*
 * Handle eXplore mode of a unit (explorers are always in eXplore mode 
 * for AI) - explores unknown territory, finds huts.
 *
 * Returns whether there is any more territory to be explored.
 */
bool ai_manage_explorer(struct unit *punit);

/*
 * TODO: Enable AI build new explorers.  Either (the old way) write a 
 * function yo estimate a city's want to build an explorer or 
 * (the new way) estimate nation-wide want for explorers and find a city 
 * which will build them.
 */

#endif /* FC__AIEXPLORER_H */
