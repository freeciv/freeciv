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

#include "attribute.h"

#define GAMELOG_FATAL  0
#define GAMELOG_NORMAL 1
#define GAMELOG_INIT  2
#define GAMELOG_MAP 3
#define GAMELOG_WONDER 4
#define GAMELOG_FOUNDC 5 
#define GAMELOG_LOSEC 6
#define GAMELOG_TECH 7
#define GAMELOG_EMBASSY 8
#define GAMELOG_GOVERNMENT 9
#define GAMELOG_CONQ 10
#define GAMELOG_REVOLT 11
#define GAMELOG_GENO 12
#define GAMELOG_TREATY 13
#define GAMELOG_RANK 17
#define GAMELOG_LAST 18
#define GAMELOG_EOT 19
#define GAMELOG_FULL 20
/*Unit created*/
#define GAMELOG_UNIT 21
/*Unit destroyed*/
#define GAMELOG_UNITL 22
/*Unit  lost due to fuel*/
#define GAMELOG_UNITF 23
/*Trireme lost at sea*/
#define GAMELOG_UNITTRI 24
/*Settlers lost to famine*/
#define GAMELOG_UNITFS 25
/*Improvements*/
#define GAMELOG_IMP 28
/*Taxation rate change*/
#define GAMELOG_RATE 29
#define GAMELOG_EVERYTHING 30
#define GAMELOG_DEBUG 40

void gamelog_init(char *filename);
void gamelog_set_level(int level);
void gamelog(int level, char *message, ...)
             fc__attribute((format (printf, 2, 3)));
void gamelog_map(void);
void gamelog_save(void);

extern int gamelog_level;

#endif  /* FC__GAMELOG_H */
