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
#include "government.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "score.h"
#include "srv_main.h"
#include "support.h"

#include "gamelog.h"
#include "stdinhand.h"

int gamelog_level;		/* also accessed from stdinhand.c */
static char *gamelog_filename;

/* Stuff for gamelog_status() */
struct player_score_entry {
  int idx;
  int value;
};

static void gamelog_status(char *buffer, int len);

/* must match the enum in gamelog.h */
const char *treaty_clause_strings[] = {
  "Embassy",
  "Tech",
  "Gold",
  "Map",
  "Seamap",
  "City",
  "Ceasefire",
  "Peace",
  "Alliance",
  "Team",
  "Shared Vision"
};

/* must match the enum in gamelog.h */
const char *endgame_strings[] = {
  "None",
  "Draw",
  "Lone Win",
  "Team Win",
  "Allied Win"
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
 Place the toplevel xml tags for each line. Also add the year and turn 
 attributes to the tag. "element" is the name of the tag.
**************************************************************************/
static void gamelog_put_prefix(char *buf, int len, const char *element)
{
  char buf2[5000];

  my_snprintf(buf2, sizeof(buf2), "<%s y=\"%d\" t=\"%d\">%s</%s>", element,
              game.year, game.turn, buf, element);
  
  mystrlcpy(buf, buf2, len);
}

/**************************************************************************
 Does the heavy lifing. Note that this function takes variable arguments.
 This means that you'd better know what you're doing when altering a gamelog
 call.

 Following are the parameters to be passed given "level".
 See the case statement for more information on what the parameters are for:

  GAMELOG_BEGIN
    none.
  GAMELOG_END
    none.
  GAMELOG_JUDGE
    int
  GAMELOG_MAP
    none
  GAMELOG_PLAYER
    struct player *
  GAMELOG_TEAM
    struct team *
  GAMELOG_WONDER
    struct city *
  GAMELOG_FOUNDCITY
    struct city *
  GAMELOG_LOSECITY
    struct player *
    struct player *
    struct city *
    char *
  GAMELOG_DISBANDCITY
    struct city *
  GAMELOG_TECH
    struct player *
    struct player * (can be NULL)
    int
    char * (only present if second player is not NULL)
  GAMELOG_EMBASSY
    struct player *
    struct city *
  GAMELOG_GOVERNMENT
    struct player *
  GAMELOG_REVOLT
    struct player *
  GAMELOG_GENO
    struct player *
  GAMELOG_TREATY
    int
    struct player *
    struct player *
    struct city *   (can be NULL, present only if int is GL_CITY)
  GAMELOG_DIPLSTATE
    struct player *
    struct player *
    int
  GAMELOG_STATUS
    none
  GAMELOG_FULL
    do not call this.
  GAMELOG_INFO
    struct player *
  GAMELOG_UNITLOSS
    struct unit *
    struct player * (can be NULL)
    char * (only present if player is NULL)
  GAMELOG_UNITGAMELOSS
    struct unit *
  GAMELOG_BUILD
    struct city *
  GAMELOG_RATECHANGE
    struct player *
  GAMELOG_EVERYTHING
     do not call this
  GAMELOG_DEBUG
     do not call this

**************************************************************************/
void gamelog(int level, ...)
{
  va_list args;
  char buf[4096] = "", msg[512] = "";
  char *word = NULL;
  struct player *pplayer = NULL, *pplayer2 = NULL;
  struct city *pcity = NULL;
  struct unit *punit = NULL;
  struct team *pteam = NULL;
  int num;
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

  va_start(args, level);

  switch (level) {
  case GAMELOG_GOVERNMENT:
    pplayer = va_arg(args, struct player *);

    my_snprintf(buf, sizeof(buf),
                "<n>%d</n><name>%s</name><m>%s form a %s</m>",
                pplayer->player_no, get_government_name(pplayer->government),
                get_nation_name_plural(pplayer->nation),
                get_government_name(pplayer->government));
    gamelog_put_prefix(buf, sizeof(buf), "gov");
    break;
  case GAMELOG_REVOLT:
    pplayer = va_arg(args, struct player *);

    my_snprintf(buf, sizeof(buf), "<n>%d</n><m>%s</m>",
                pplayer->player_no, get_nation_name_plural(pplayer->nation));
    gamelog_put_prefix(buf, sizeof(buf), "rev");
    break;
  case GAMELOG_FOUNDCITY:
    pcity = va_arg(args, struct city *);

    my_snprintf(buf, sizeof(buf), "<n>%d</n><name>%s</name>"
                "<x>%d</x><y>%d</y><m>%s (%d,%d) founded by the %s</m>",
                city_owner(pcity)->player_no,
                pcity->name, pcity->tile->x, pcity->tile->y,
                pcity->name, pcity->tile->x, pcity->tile->y,
                get_nation_name_plural(city_owner(pcity)->nation));
    gamelog_put_prefix(buf, sizeof(buf), "cityf");
    break;
  case GAMELOG_LOSECITY:
    pplayer = va_arg(args, struct player *);
    pplayer2 = va_arg(args, struct player *);
    pcity = va_arg(args, struct city *);
    word = va_arg(args, char *);

    my_snprintf(buf, sizeof(buf), "<n1>%d</n1><n2>%d</n2>"
                "<name>%s</name><x>%d</x><y>%d</y>"
                "<m>%s (%s) (%d,%d) %s by %s</m>", 
                pplayer->player_no, pplayer2->player_no,
                pcity->name, pcity->tile->x, pcity->tile->y,
                pcity->name, get_nation_name_plural(pplayer->nation),
                pcity->tile->x, pcity->tile->y, word,
                get_nation_name_plural(pplayer2->nation));
    gamelog_put_prefix(buf, sizeof(buf), "cityl");
    break;
  case GAMELOG_DISBANDCITY:
    pcity = va_arg(args, struct city *);

    my_snprintf(buf, sizeof(buf), "<n>%d</n><name>%s</name>"
                "<x>%d</x><y>%d</y><m>%s (%d, %d) disbanded by the %s</m>",
                city_owner(pcity)->player_no,
                pcity->name, pcity->tile->x, pcity->tile->y,
                pcity->name, pcity->tile->x, pcity->tile->y,
                get_nation_name_plural(city_owner(pcity)->nation));
    gamelog_put_prefix(buf, sizeof(buf), "cityd");
    break;
  case GAMELOG_TREATY:
    num = va_arg(args, int);
    pplayer = va_arg(args, struct player *);
    pplayer2 = va_arg(args, struct player *);

    switch(num) {
    case GL_EMBASSY:
      sz_strlcpy(msg, "<m>Treaty - Embassy between the %s and the %s</m>");
      break;
    case GL_TECH:
      sz_strlcpy(msg, "<m>Treaty - Tech given to the %s from the %s</m>");
      break;
    case GL_GOLD:
      sz_strlcpy(msg, "<m>Treaty - Gold given to the %s from the %s</m>");
      break;
    case GL_MAP:
      sz_strlcpy(msg, "<m>Treaty - Map given to the %s from the %s</m>");
      break;
    case GL_SEAMAP:
      sz_strlcpy(msg, "<m>Treaty - Seamap given to the %s from the %s</m>");
      break;
    case GL_CITY:
      pcity = va_arg(args, struct city *);
      sz_strlcpy(msg,
                 "<m>Treaty - City (%s) given to the %s from the %s</m>");
      break;
    case GL_CEASEFIRE:
      sz_strlcpy(msg, "<m>Treaty - Ceasefire between the %s and the %s</m>");
      break;
    case GL_PEACE:
      sz_strlcpy(msg, "<m>Treaty - Peace between the %s and the %s</m>");
      break;
    case GL_ALLIANCE:
      sz_strlcpy(msg, "<m>Treaty - Alliance between the %s and the %s</m>");
      break;
    case GL_TEAM:
      sz_strlcpy(msg, "<m>Treaty - Team between the %s and the %s</m>");
      break;
    case GL_VISION:
      sz_strlcpy(msg,
                 "<m>Treaty - Shared Vision between the %s and the %s</m>");
      break;
    default:
      break;
    }

    if (pcity) {
      my_snprintf(buf, sizeof(buf),
                  "<n1>%d</n1><n2>%d</n2><city>%s</city><type>%s</type>",
                  pplayer->player_no, pplayer2->player_no, pcity->name,
                  treaty_clause_strings[num]);
      cat_snprintf(buf, sizeof(buf), msg, 
                  pcity->name, get_nation_name_plural(pplayer2->nation),
                  get_nation_name_plural(pplayer->nation));
    } else {
      my_snprintf(buf, sizeof(buf),
                  "<n1>%d</n1><n2>%d</n2><type>%s</type>",
                  pplayer->player_no, pplayer2->player_no, 
                  treaty_clause_strings[num]);
      cat_snprintf(buf, sizeof(buf), msg,
                  get_nation_name_plural(pplayer->nation),
                  get_nation_name_plural(pplayer2->nation));
    }
    gamelog_put_prefix(buf, sizeof(buf), "treaty");
    break;
  case GAMELOG_DIPLSTATE:
    pplayer = va_arg(args, struct player *);
    pplayer2 = va_arg(args, struct player *);
    num = va_arg(args, int);

    my_snprintf(buf, sizeof(buf),
                "<n1>%d</n1><n2>%d</n2><type>%s</type>",
                pplayer->player_no, pplayer2->player_no,  diplstate_text(num));
    cat_snprintf(buf, sizeof(buf),
                "<m>The diplomatic state between the %s and the %s is %s</m>",
                get_nation_name_plural(pplayer->nation),
                get_nation_name_plural(pplayer2->nation), diplstate_text(num));
    gamelog_put_prefix(buf, sizeof(buf), "dipl");
    break;
  case GAMELOG_TECH:
    pplayer = va_arg(args, struct player *);
    pplayer2 = va_arg(args, struct player *);
    num = va_arg(args, int);

    if (pplayer2) {
      word = va_arg(args, char *);

      my_snprintf(buf, sizeof(buf),
                  "<n1>%d</n1><n2>%d</n2><name>%s</name>"
                  "<m>%s %s %s from the %s</m>",
                  pplayer->player_no, pplayer2->player_no,
                  get_tech_name(pplayer, (Tech_Type_id) num),
                  get_nation_name_plural(pplayer->nation), word,
                  get_tech_name(pplayer, (Tech_Type_id) num),
                  get_nation_name_plural(pplayer2->nation));
    } else {
      my_snprintf(buf, sizeof(buf),
                  "<n1>%d</n1><name>%s</name>"
                  "<m>%s discover %s</m>",
                  pplayer->player_no,
                  get_tech_name(pplayer, (Tech_Type_id) num),
                  get_nation_name_plural(pplayer->nation),
                  get_tech_name(pplayer, (Tech_Type_id) num));
    }
    gamelog_put_prefix(buf, sizeof(buf), "tech");
    break;
  case GAMELOG_UNITLOSS:
    punit = va_arg(args, struct unit *);
    pplayer = va_arg(args, struct player *);

    if (pplayer) {
      my_snprintf(buf, sizeof(buf),
                  "<n1>%d</n1><n2>%d</n2><name>%s</name>"
                  "<m>%s lose %s to the %s</m>",
                  unit_owner(punit)->player_no, pplayer->player_no,
                  unit_name(punit->type),
                  get_nation_name_plural(unit_owner(punit)->nation),
                  unit_name(punit->type),
                  get_nation_name_plural(pplayer->nation));
    } else {
      word = va_arg(args, char *);
      my_snprintf(buf, sizeof(buf),
                  "<n1>%d</n1><name>%s</name><m>%s lose %s (%s)</m>",
                  unit_owner(punit)->player_no, unit_name(punit->type),
                  get_nation_name_plural(unit_owner(punit)->nation),
                  unit_name(punit->type), word);
    }
    gamelog_put_prefix(buf, sizeof(buf), "unitl");
    break;
  case GAMELOG_UNITGAMELOSS:
    punit = va_arg(args, struct unit *);

    my_snprintf(buf, sizeof(buf),
                "<n>%d</n><name>%s</name>"
                "<m>%s lost a game loss unit and died</m>",
                unit_owner(punit)->player_no, unit_name(punit->type),
                get_nation_name_plural(unit_owner(punit)->nation));
    gamelog_put_prefix(buf, sizeof(buf), "gamel");
    break;
  case GAMELOG_EMBASSY:
    pplayer = va_arg(args, struct player *);
    pcity = va_arg(args, struct city *);

    my_snprintf(buf, sizeof(buf),
                "<n1>%d</n1><n2>%d</n2><name>%s</name><x>%d</x><y>%d</y>"
                "<m>%s establish an embassy in %s (%s) (%d,%d)</m>",
                pplayer->player_no, city_owner(pcity)->player_no,
                pcity->name, pcity->tile->x, pcity->tile->y,
                get_nation_name_plural(pplayer->nation),
                pcity->name,
                get_nation_name_plural(city_owner(pcity)->nation),
                pcity->tile->x, pcity->tile->y);
    gamelog_put_prefix(buf, sizeof(buf), "embassy");
    break;
  case GAMELOG_BUILD:
  case GAMELOG_WONDER:
    pcity = va_arg(args, struct city *);

    my_snprintf(buf, sizeof(buf),
                "<n>%d</n><city>%s</city><u>%d</u>"
                "<w>%d</w><name>%s</name><m>%s build %s in %s</m>",
                city_owner(pcity)->player_no, pcity->name,
                pcity->is_building_unit ? 1 : 0,
                (!pcity->is_building_unit 
                 && is_wonder(pcity->currently_building)) ? 1 : 0,
                pcity->is_building_unit ? 
                  unit_types[pcity->currently_building].name :
                  get_impr_name_ex(pcity, pcity->currently_building),
                get_nation_name_plural(city_owner(pcity)->nation),
                pcity->is_building_unit ? 
                  unit_types[pcity->currently_building].name :
                  get_impr_name_ex(pcity, pcity->currently_building),
                pcity->name);
    gamelog_put_prefix(buf, sizeof(buf), "build");
    break;
  case GAMELOG_GENO:
    pplayer = va_arg(args, struct player *);

    my_snprintf(buf, sizeof(buf),
                "<n>%d</n><b>%d</b><m>%s civilization destroyed</m>",
                pplayer->player_no, is_barbarian(pplayer) ? 1 : 0,
                is_barbarian(pplayer) ? get_nation_name(pplayer->nation)
                                      : pplayer->name);
    gamelog_put_prefix(buf, sizeof(buf), "geno");
    break;
  case GAMELOG_RATECHANGE:
    pplayer = va_arg(args, struct player *);

    my_snprintf(buf, sizeof(buf), 
                "<n>%d</n><tax>%d</tax><lux>%d</lux><sci>%d</sci>",
                pplayer->player_no, pplayer->economic.tax, 
                pplayer->economic.luxury, pplayer->economic.science);
    gamelog_put_prefix(buf, sizeof(buf), "rates");
    break;
  case GAMELOG_INFO:
    pplayer = va_arg(args, struct player *);
    {
      int food = 0, shields = 0, trade = 0, settlers = 0;

      unit_list_iterate(pplayer->units, punit) {
        if (unit_flag(punit, F_CITIES)) {
          settlers++;
        }
      } unit_list_iterate_end;
      city_list_iterate(pplayer->cities, pcity) {
        shields += pcity->shield_prod;
        food += pcity->food_prod;
        trade += pcity->trade_prod;
      } city_list_iterate_end;

      my_snprintf(buf, sizeof(buf), "<n>%d</n><cities>%d</cities>"
                  "<pop>%d</pop><food>%d</food><prod>%d</prod>"
                  "<trade>%d</trade><settlers>%d</settlers><units>%d</units>",
                  pplayer->player_no, city_list_size(&pplayer->cities),
                  total_player_citizens(pplayer), food, shields, trade, 
                  settlers, unit_list_size(&pplayer->units));
    }
    gamelog_put_prefix(buf, sizeof(buf), "info");
    break;
  case GAMELOG_PLAYER:
    pplayer = va_arg(args, struct player *);

    my_snprintf(buf, sizeof(buf), "<n>%d</n><u>%s</u><c>%d</c>"
                "<ai>%s</ai><nat>%s</nat><l>%s</l>",
                pplayer->player_no, pplayer->username,
                pplayer->is_connected ? 1 : 0,
                pplayer->ai.control ? 
                  name_of_skill_level(pplayer->ai.skill_level) : "",
                get_nation_name_plural(pplayer->nation), pplayer->name);
    gamelog_put_prefix(buf, sizeof(buf), "player");
    break;
  case GAMELOG_TEAM:
    pteam = va_arg(args, struct team *);

    my_snprintf(buf, sizeof(buf), "<id>%d</id><name>%s</name>",
                                  pteam->id, pteam->name);
    players_iterate(aplayer) {
      if (aplayer->team == pteam->id) {
        cat_snprintf(buf, sizeof(buf), "<n>%d</n>", aplayer->player_no);
      }
    } players_iterate_end;
    gamelog_put_prefix(buf, sizeof(buf), "team");
    break;
  case GAMELOG_BEGIN:
    my_snprintf(buf, sizeof(buf),
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<gamelog version=\"2.0\">");
    break; 
  case GAMELOG_END:
    my_snprintf(buf, sizeof(buf), "</gamelog>");
    break;
  case GAMELOG_JUDGE:
    num = va_arg(args, int);

    switch(num) {
    case GL_NONE:
    case GL_DRAW:
      my_snprintf(buf, sizeof(buf), "<type>%s</type>", endgame_strings[num]);
      msg[0] = '\0';
      break;
    case GL_LONEWIN:
      pplayer = va_arg(args, struct player *);

      my_snprintf(buf, sizeof(buf), "<type>%s</type><n>%d</n>",
                  endgame_strings[num], pplayer->player_no);
      my_snprintf(msg, sizeof(msg),
                  (pplayer->spaceship.state == SSHIP_ARRIVED)
                  ? "The %s spaceship has arrived at Alpha Centauri."
                  : "Game ended in victory for the %s",
                  get_nation_name_plural(pplayer->nation));
      break;
    case GL_ALLIEDWIN:
      my_snprintf(buf, sizeof(buf), "<type>%s</type>", endgame_strings[num]);
      players_iterate(aplayer) {
        if (aplayer->is_alive) {
          cat_snprintf(buf, sizeof(buf), "<n>%d</n>", aplayer->player_no);
        }
      } players_iterate_end;
      my_snprintf(msg, sizeof(msg), "Game ended in allied victory");
      break;
    case GL_TEAMWIN:
      pteam = va_arg(args, struct team *);
      my_snprintf(buf, sizeof(buf), "<type>%s</type>", endgame_strings[num]);
      players_iterate(aplayer) {
        if (aplayer->team == pteam->id) {
          cat_snprintf(buf, sizeof(buf), "<n>%d</n>", aplayer->player_no);
        }
      } players_iterate_end;
      my_snprintf(msg, sizeof(msg), "Team victory to %s", pteam->name);
      break;
    default:
      break;
    }
    cat_snprintf(buf, sizeof(buf), "<m>%s</m>", msg);
    gamelog_put_prefix(buf, sizeof(buf), "judge");
    break;
  case GAMELOG_MAP:
    /* this is big, so it's special */
    {
      int nat_x, nat_y, i = 0;
      char *mapline = fc_malloc(((map.xsize + 1) * map.ysize) + 1);

      for (nat_y = 0; nat_y < map.ysize; nat_y++) {
        for (nat_x = 0; nat_x < map.xsize; nat_x++) {
          struct tile *ptile = native_pos_to_tile(nat_x, nat_y);

          mapline[i++] = is_ocean(map_get_terrain(ptile)) ? ' ' : '.';
        }
        mapline[i++] = '\n';
      }
      mapline[i] = '\0';

      va_end(args);

      fprintf(fs, "<map>\n%s</map>\n", mapline);
      fflush(fs);
      fclose(fs);
      free(mapline);
      return;
    }
    break;
  case GAMELOG_STATUS:
    gamelog_status(buf, sizeof(buf));
    gamelog_put_prefix(buf, sizeof(buf), "status");
    break;
  default:
    assert(0);
    break;
  }

  va_end(args);

  fprintf(fs, "%s\n", buf);
  fflush(fs);
  fclose(fs);
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
static void gamelog_status(char *buffer, int len) {

  int i, count = 0, highest = -1;
  struct player *highest_plr = NULL;
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

  buffer[0] = '\0';
  qsort(rank, count, sizeof(struct player_score_entry), secompare1);

  for (i = 0; i < count; i++) {
    cat_snprintf(buffer, len, "<plr><no>%d</no><r>%d</r><s>%d</s></plr>",
		 game.players[rank[i].idx].player_no,
                 rank[i].value, size[rank[i].idx].value);
  }
}
