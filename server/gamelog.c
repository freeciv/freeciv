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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fcintl.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "score.h"
#include "srv_main.h"
#include "support.h"

#include "gamelog.h"

int gamelog_level;		/* also accessed from stdinhand.c */
static char *gamelog_filename;

/* Stuff for gamelog_save() */
struct player_score_entry {
  int idx;
  int value;
};

/**************************************************************************
  Filename can be NULL, which means no logging.
**************************************************************************/
void gamelog_init(char *filename)
{
  gamelog_level = GAMELOG_FULL;
  if (filename && strlen(filename) > 0) {
    gamelog_filename = filename;
  } else {
    gamelog_filename = NULL;
  }
}

/**************************************************************************
  ...
**************************************************************************/
void gamelog_set_level(int level)
{
  gamelog_level = level;
}

/**************************************************************************
  ...
**************************************************************************/
void gamelog(int level, const char *message, ...)
{
  va_list args;
  char buf[512];
  FILE *fs;

  if (!gamelog_filename) {
    return;
  }
  if (level > gamelog_level) {
    return;
  }

  fs = fopen(gamelog_filename, "a");
  if (!fs) {
    freelog(LOG_FATAL, _("Couldn't open gamelogfile \"%s\" for appending."), 
	    gamelog_filename);
    exit(EXIT_FAILURE);
  }

  va_start(args, message);
  my_vsnprintf(buf, sizeof(buf), message, args);
  if (level == GAMELOG_MAP) {
    if (buf[0] == '(') {
      /* KLUGE!! FIXME: remove when we fix the gamelog format --jjm */
      fprintf(fs, "%i %s\n", game.year, buf);
    } else {
      fprintf(fs,"%s\n", buf);
    }
  } else if (level == GAMELOG_EOT) {
    fprintf(fs,"*** %s\n", buf);
  } else if (level == GAMELOG_RANK) {
    fprintf(fs,"RANK %s\n", buf);
  } else if (level == GAMELOG_TEAM) {
    fprintf(fs,"%s\n", buf);
  } else if (level == GAMELOG_STATUS) {
    fprintf(fs, "STATUS %s\n", buf);
  } else {
    fprintf(fs, "%i %s\n", game.year,buf);
  }
  fflush(fs);
  fclose(fs);
}

/**************************************************************************
  ...
**************************************************************************/
void gamelog_map(void)
{
  int nat_x, nat_y;
  char *hline = fc_calloc(map.xsize + 1, sizeof(char));

  for (nat_y = 0; nat_y < map.ysize; nat_y++) {
    for (nat_x = 0; nat_x < map.xsize; nat_x++) {
      struct tile *ptile = native_pos_to_tile(nat_x, nat_y);

      hline[nat_x] = is_ocean(map_get_terrain(ptile)) ? ' ' : '.';
    }
    gamelog(GAMELOG_MAP, "%s", hline);
  }
  free(hline);
}

/**************************************************************************
  ...
**************************************************************************/
static int secompare1(const void *a, const void *b)
{
  return (((const struct player_score_entry *)b)->value -
	  ((const struct player_score_entry *)a)->value);
}

/**************************************************************************
  Every time we save the game, we also output to the gamelog the score
  and status info.
**************************************************************************/
void gamelog_save(void) {
  int i, count = 0, highest = -1;
  struct player *highest_plr = NULL;
  char buffer[4096];
  struct player_score_entry size[game.nplayers], rank[game.nplayers];

  players_iterate(pplayer) {
    if (!is_barbarian(pplayer)) {
      rank[count].value = get_civ_score(pplayer);
      rank[count].idx = pplayer->player_no;
      size[count].value = total_player_citizens(pplayer);
      size[count].idx = pplayer->player_no;
      if (rank[count].value > highest) {
        highest = rank[count].value;
        highest_plr = pplayer;
      }
      count++;
    }
  } players_iterate_end;

  /* Draws and team victories */
  count = 0;
  players_iterate(pplayer) {
    if (!is_barbarian(pplayer)) {
      if ((BV_ISSET_ANY(srvarg.draw)
           && BV_ISSET(srvarg.draw, pplayer->player_no))
          || players_on_same_team(pplayer, highest_plr)) {
        /* We win a shared victory, so equal the score. */
        rank[count].value = highest;
      }
      count++;
    }
  } players_iterate_end;

  qsort(size, count, sizeof(struct player_score_entry), secompare1);
  buffer[0] = 0;
  for (i = 0; i < count; i++) {
    cat_snprintf(buffer, sizeof(buffer),
		 "%2d: %s(%i)  ",
		 i+1,
		 get_nation_name_plural(game.players[size[i].idx].nation),
		 size[i].value);
  }
  gamelog(GAMELOG_EOT,buffer);
  qsort(rank, count, sizeof(struct player_score_entry), secompare1);
  buffer[0] = 0;
  for (i = 0; i < count; i++) {
    cat_snprintf(buffer, sizeof(buffer),
		 "%s,%i|",
		 game.players[rank[i].idx].name,
		 rank[i].value);
  }
  gamelog(GAMELOG_RANK,buffer);
  buffer[0] = 0;
  for (i = 0; i < count; i++) {
    cat_snprintf(buffer, sizeof(buffer),
		 "%s,%i,%i,%i|",
		 game.players[rank[i].idx].name,
		 game.players[rank[i].idx].ai.control ? 1 : 0 *
		 game.players[rank[i].idx].ai.skill_level,
		 game.players[rank[i].idx].is_connected, rank[i].value);
  }
  gamelog(GAMELOG_STATUS, buffer);
}
