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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "game.h"
#include "log.h"
#include "map.h"
#include "mem.h"

#include "gamelog.h"

int gamelog_level;		/* also accessed from stdinhand.c */
static char *gamelog_filename;

/**************************************************************************
filename can be NULL, which means no logging.
***************************************************************************/
void gamelog_init(char *filename)
{
  gamelog_level=GAMELOG_FULL;
  gamelog_filename=filename;
}

/**************************************************************************/
void gamelog_set_level(int level)
{
  gamelog_level=level;
}

/**************************************************************************/
void gamelog(int level, char *message, ...)
{
  va_list args;
  char buf[512];
  FILE *fs;

  if (gamelog_filename==NULL)
    return;
  if (level > gamelog_level)
    return;

  fs=fopen(gamelog_filename, "a");
  if(!fs) {
    freelog(LOG_FATAL, "couldn't open gamelogfile: %s for appending.\n", 
	    gamelog_filename);
    exit(1);
  }
  
  va_start(args, message);
  vsprintf(buf, message, args);
  if (level==GAMELOG_MAP){
    fprintf(fs,"%s\n",buf);
  } else if (level==GAMELOG_EOT){
    fprintf(fs,"*** %s\n",buf);
  } else {
    fprintf(fs,"%i %s\n", game.year,buf);
  }
  fflush(fs);
  fclose(fs);
}


void gamelog_map(void)
{
  int x, y;
  char *hline = fc_calloc(map.xsize+1,sizeof(char));

  for (y=0;y<map.ysize;y++) {
    for (x=0;x<map.xsize;x++) {
      hline[x] = (map_get_terrain(x,y)==T_OCEAN) ? ' ' : '.';
    }
    gamelog(GAMELOG_MAP,"%s",hline);
  }
  free(hline);
}

/*Stuff for gamelog_save()*/
struct player_score_entry {
  int idx;
  int value;
};

static int secompare1(const void *a, const void *b)
{
  return (((const struct player_score_entry *)b)->value -
	  ((const struct player_score_entry *)a)->value);
}

void gamelog_save(void){
  /*lifted from historian_largest()*/
  int i;
  char buffer[4096];
  char buf2[4096];
  struct player_score_entry *size=
    fc_malloc(sizeof(struct player_score_entry)*game.nplayers);
  for (i=0;i<game.nplayers;i++) {
    size[i].value=total_player_citizens(&game.players[i]);
    size[i].idx=i;
  }
  qsort(size, game.nplayers, sizeof(struct player_score_entry), secompare1);
  buffer[0]=0;
  for (i=0;i<game.nplayers;i++) {
    sprintf(buf2,"%2d: %s(%i)  ",i+1, get_race_name_plural(game.players[size[i].idx].race), size[i].value);
    strcat(buffer,buf2);
  }
  gamelog(GAMELOG_EOT,buffer);
  free(size);
}



