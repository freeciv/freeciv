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

#include <stdio.h>
#include <string.h>

#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "log.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "rand.h"
#include "support.h"
#include "version.h"

#include "report.h"

enum historian_type {
        HISTORIAN_RICHEST=0, 
        HISTORIAN_ADVANCED=1,
        HISTORIAN_MILITARY=2,
        HISTORIAN_HAPPIEST=3,
        HISTORIAN_LARGEST=4};

static const char *historian_message[]={
    N_("%s report on the RICHEST Civilizations in the World."),
    N_("%s report on the most ADVANCED Civilizations in the World."),
    N_("%s report on the most MILITARIZED Civilizations in the World."),
    N_("%s report on the HAPPIEST Civilizations in the World."),
    N_("%s report on the LARGEST Civilizations in the World.")
};

static const char *historian_name[]={
    N_("Herodotus'"),
    N_("Thucydides'"),
    N_("Pliny the Elder's"),
    N_("Livy's"),
    N_("Toynbee's"),
    N_("Gibbon's"),
    N_("Ssu-ma Ch'ien's"),
    N_("Pan Ku's")
};

struct player_score_entry {
  int idx;
  int value;
};

/**************************************************************************
...
**************************************************************************/
static int secompare(const void *a, const void *b)
{
  return (((const struct player_score_entry *)b)->value -
	  ((const struct player_score_entry *)a)->value);
}

static const char *greatness[MAX_NUM_PLAYERS] = {
  N_("Magnificent"),  N_("Glorious"), N_("Great"), N_("Decent"),
  N_("Mediocre"), N_("Hilarious"), N_("Worthless"), N_("Pathetic"),
  N_("Useless"), "Useless", "Useless", "Useless", "Useless", "Useless",
  "Useless", "Useless", "Useless", "Useless", "Useless", "Useless",
  "Useless", "Useless", "Useless", "Useless", "Useless", "Useless",
  "Useless", "Useless", "Useless", "Useless"
};

/**************************************************************************
...
**************************************************************************/
static void historian_generic(enum historian_type which_news)
{
  int i,j=0;
  int rank=0;
  int which_historian;
  char buffer[4096];
  char title[1024];
  struct player_score_entry *size=
    fc_malloc(sizeof(struct player_score_entry)*game.nplayers);

  for (i=0;i<game.nplayers;i++) {
    if (game.players[i].is_alive && !is_barbarian(&game.players[i])) {
      switch(which_news) {
      case HISTORIAN_RICHEST:
	size[j].value=game.players[i].economic.gold;
	break;
      case HISTORIAN_ADVANCED:
	size[j].value = (game.players[i].score.techs
			 +game.players[i].future_tech);
	break;
      case HISTORIAN_MILITARY:
	size[j].value=game.players[i].score.units;
	break;
      case HISTORIAN_HAPPIEST: 
	size[j].value=
	  ((game.players[i].score.happy-game.players[i].score.unhappy)*1000)
	  /(1+total_player_citizens(&game.players[i]));
	break;
      case HISTORIAN_LARGEST:
	size[j].value=total_player_citizens(&game.players[i]);
	break;
      }
      size[j].idx=i;
      j++;
    } /* else the player is dead or barbarian */
  }
  qsort(size, j, sizeof(struct player_score_entry), secompare);
  buffer[0]=0;
  for (i=0;i<j;i++) {
    if (i == 0 || size[i].value < size[i-1].value)
      rank = i;
    cat_snprintf(buffer, sizeof(buffer),
		 _("%2d: The %s %s\n"), rank+1, _(greatness[rank]),
		 get_nation_name_plural(game.players[size[i].idx].nation));
  }
  free(size);
  which_historian = myrand(ARRAY_SIZE(historian_name));
  my_snprintf(title, sizeof(title), _(historian_message[which_news]),
    _(historian_name[which_historian]));
  page_conn_etype(&game.game_connections, _("Historian Publishes!"),
		  title, buffer, BROADCAST_EVENT);
}

/**************************************************************************
...
**************************************************************************/
static int nr_wonders(struct city *pcity)
{
  int i;
  int res=0;
  for (i=0;i<game.num_impr_types;i++)
    if (is_wonder(i) && city_got_building(pcity, i))
      res++;
  return res;
}

/**************************************************************************
  Send report listing the "best" 5 cities in the world.
**************************************************************************/
void report_top_five_cities(struct conn_list *dest)
{
  struct player_score_entry *size=
    fc_malloc(sizeof(struct player_score_entry)*5);
  int i;
  char buffer[4096];
  struct city *pcity;
  buffer[0]=0;
  for (i=0;i<5;i++) {
    size[i].value=0;
    size[i].idx=0;
  }
  for (i=0;i<game.nplayers;i++) {
    city_list_iterate(game.players[i].cities, pcity) {
      if ((pcity->size+nr_wonders(pcity)*5)>size[4].value) {
	size[4].value=pcity->size+nr_wonders(pcity)*5;
	size[4].idx=pcity->id;
	qsort(size, 5, sizeof(struct player_score_entry), secompare);
      }
    }
    city_list_iterate_end;
  }
  for (i=0;i<5;i++) {
    pcity=find_city_by_id(size[i].idx);
    if (pcity) { 
      cat_snprintf(buffer, sizeof(buffer),
		   _("%2d: The %s City of %s of size %d, with %d wonders\n"),
		   i+1, get_nation_name(city_owner(pcity)->nation),pcity->name, 
		   pcity->size, nr_wonders(pcity));
    }
  }
  free(size);
  page_conn(dest, _("Traveler's Report:"),
	    _("The Five Greatest Cities in the World!"), buffer);
}

/**************************************************************************
  Send report listing all built and destroyed wonders, and wonders
  currently being built.
**************************************************************************/
void report_wonders_of_the_world(struct conn_list *dest)
{
  int i;
  struct city *pcity;
  char buffer[4096];
  buffer[0]=0;
  for (i=0;i<game.num_impr_types;i++) {
    if (is_wonder(i)) {
      if (game.global_wonders[i]) {
	if ((pcity=find_city_by_id(game.global_wonders[i]))) {
	  cat_snprintf(buffer, sizeof(buffer), _("%s in %s (%s)\n"),
		       get_impr_name_ex(pcity, i), pcity->name,
		       get_nation_name(city_owner(pcity)->nation));
	} else {
	  cat_snprintf(buffer, sizeof(buffer), _("%s has been DESTROYED\n"),
		       get_improvement_type(i)->name);
	}
      }
    }
  }
  for (i=0;i<game.num_impr_types;i++) {
    if (is_wonder(i)) {
      players_iterate(pplayer) {
	city_list_iterate(pplayer->cities, pcity2) {
	  if (pcity2->currently_building == i && !pcity2->is_building_unit) {
	    cat_snprintf(buffer, sizeof(buffer),
			 _("(building %s in %s (%s))\n"),
			 get_improvement_type(i)->name, pcity2->name,
			 get_nation_name(pplayer->nation));
	  }
	} city_list_iterate_end;
      } players_iterate_end;
    }
  }
  page_conn(dest, _("Traveler's Report:"),
	    _("Wonders of the World"), buffer);
}

/**************************************************************************
...
**************************************************************************/
static int rank_population(struct player *pplayer)
{
  int basis=pplayer->score.population;
  int place=1;
  int i;
  for (i=0;i<game.nplayers;i++) {
    if (game.players[i].score.population>basis &&
	game.players[i].is_alive && !is_barbarian(&game.players[i]))
      place++;
  }
  return place;
}

/**************************************************************************
...
**************************************************************************/
static struct player *best_population(void)
{
  struct player *pplayer = &game.players[0];
  int i;
  for(i = 1; i < game.nplayers; i++) {
    if(game.players[i].score.population > pplayer->score.population &&
       game.players[i].is_alive && !is_barbarian(&game.players[i])) {
      pplayer = &game.players[i];
    }
  }
  return pplayer;
}

/**************************************************************************
...
**************************************************************************/
static int rank_landarea(struct player *pplayer)
{
  int basis=pplayer->score.landarea;
  int place=1;
  int i;
  for (i=0;i<game.nplayers;i++) {
    if (game.players[i].score.landarea>basis &&
	game.players[i].is_alive && !is_barbarian(&game.players[i]))
      place++;
  }
  return place;
}

/**************************************************************************
...
**************************************************************************/
static struct player *best_landarea(void)
{
  struct player *pplayer = &game.players[0];
  int i;
  for(i = 1; i < game.nplayers; i++) {
    if(game.players[i].score.landarea > pplayer->score.landarea &&
       game.players[i].is_alive && !is_barbarian(&game.players[i])) {
      pplayer = &game.players[i];
    }
  }
  return pplayer;
}

/**************************************************************************
...
**************************************************************************/
static int rank_settledarea(struct player *pplayer)
{
  int basis=pplayer->score.settledarea;
  int place=1;
  int i;
  for (i=0;i<game.nplayers;i++) {
    if (game.players[i].score.settledarea>basis &&
	game.players[i].is_alive && !is_barbarian(&game.players[i]))
      place++;
  }
  return place;
}

/**************************************************************************
...
**************************************************************************/
static struct player *best_settledarea(void)
{
  struct player *pplayer = &game.players[0];
  int i;
  for(i = 1; i < game.nplayers; i++) {
    if(game.players[i].score.settledarea > pplayer->score.settledarea &&
       game.players[i].is_alive && !is_barbarian(&game.players[i])) {
      pplayer = &game.players[i];
    }
  }
  return pplayer;
}

/**************************************************************************
...
**************************************************************************/
static int rank_calc_research(struct player *pplayer)
{
  return (pplayer->score.techout * 100) / (total_bulbs_required(pplayer));
}

/**************************************************************************
...
**************************************************************************/
static int rank_research(struct player *pplayer)
{
  int basis = rank_calc_research(pplayer), place = 1;

  players_iterate(other) {
    if (other->is_alive && !is_barbarian(other)
	&& rank_calc_research(other) > basis) {
      place++;
    }
  } players_iterate_end;

  return place;
}

/**************************************************************************
...
**************************************************************************/
static struct player *best_research(void)
{
  struct player *pplayer = NULL;

  players_iterate(other) {
    if (other->is_alive && !is_barbarian(other)
	&& (pplayer == NULL
	    || rank_calc_research(other) > rank_calc_research(pplayer))) {
      pplayer = other;
    }
  } players_iterate_end;

  return pplayer;
}

/**************************************************************************
...
**************************************************************************/
static int rank_calc_literacy(struct player *pplayer)
{
  int pop = civ_population(pplayer);

  if (pop <= 0) {
    return 0;
  } else if (pop >= 10000) {
    return pplayer->score.literacy/(pop/100);
  } else {
    return (pplayer->score.literacy*100)/pop;
  }
}

/**************************************************************************
...
**************************************************************************/
static int rank_literacy(struct player *pplayer)
{
  int basis=rank_calc_literacy(pplayer);
  int place=1;
  int i;
  for (i=0;i<game.nplayers;i++) {
    if (rank_calc_literacy(&game.players[i])>basis &&
	game.players[i].is_alive && !is_barbarian(&game.players[i]))
      place++;
  }
  return place;
}

/**************************************************************************
...
**************************************************************************/
static struct player *best_literacy(void)
{
  struct player *pplayer = &game.players[0];
  int i;
  for(i = 1; i < game.nplayers; i++) {
    if(rank_calc_literacy(&game.players[i]) > rank_calc_literacy(pplayer) &&
       game.players[i].is_alive && !is_barbarian(&game.players[i])) {
      pplayer = &game.players[i];
    }
  }
  return pplayer;
}

/**************************************************************************
...
**************************************************************************/
static int rank_production(struct player *pplayer)
{
  int basis=pplayer->score.mfg;
  int place=1;
  int i;
  for (i=0;i<game.nplayers;i++) {
    if (game.players[i].score.mfg>basis &&
	game.players[i].is_alive && !is_barbarian(&game.players[i]))
      place++;
  }
  return place;
}

/**************************************************************************
...
**************************************************************************/
static struct player *best_production(void)
{
  struct player *pplayer = &game.players[0];
  int i;
  for(i = 1; i < game.nplayers; i++) {
    if(game.players[i].score.mfg > pplayer->score.mfg &&
       game.players[i].is_alive && !is_barbarian(&game.players[i])) {
      pplayer = &game.players[i];
    }
  }
  return pplayer;
}

/**************************************************************************
...
**************************************************************************/
static int rank_economics(struct player *pplayer)
{
  int basis=pplayer->score.bnp;
  int place=1;
  int i;
  for (i=0;i<game.nplayers;i++) {
    if (game.players[i].score.bnp>basis &&
	game.players[i].is_alive && !is_barbarian(&game.players[i]))
      place++;
  }
  return place;
}

/**************************************************************************
...
**************************************************************************/
static struct player *best_economics(void)
{
  struct player *pplayer = &game.players[0];
  int i;
  for(i = 1; i < game.nplayers; i++) {
    if(game.players[i].score.bnp > pplayer->score.bnp &&
       game.players[i].is_alive && !is_barbarian(&game.players[i])) {
      pplayer = &game.players[i];
    }
  }
  return pplayer;
}

/**************************************************************************
...
**************************************************************************/
static int rank_pollution(struct player *pplayer)
{
  int basis=pplayer->score.pollution;
  int place=1;
  int i;
  for (i=0;i<game.nplayers;i++) {
    if (game.players[i].score.pollution<basis &&
	game.players[i].is_alive && !is_barbarian(&game.players[i]))
      place++;
  }
  return place;
}

/**************************************************************************
...
**************************************************************************/
static struct player *best_pollution(void)
{
  struct player *pplayer = &game.players[0];
  int i;
  for(i = 1; i < game.nplayers; i++) {
    if(game.players[i].score.pollution < pplayer->score.pollution &&
       game.players[i].is_alive && !is_barbarian(&game.players[i])) {
      pplayer = &game.players[i];
    }
  }
  return pplayer;
}

/**************************************************************************
...
**************************************************************************/
static int rank_calc_mil_service(struct player *pplayer)
{
  return (pplayer->score.units * 5000) / (10 + civ_population(pplayer));
}

/**************************************************************************
...
**************************************************************************/
static int rank_mil_service(struct player *pplayer)
{
  int basis=rank_calc_mil_service(pplayer);
  int place=1;
  int i;
  for (i=0;i<game.nplayers;i++) {
    if (rank_calc_mil_service(&game.players[i])<basis &&
	game.players[i].is_alive && !is_barbarian(&game.players[i]))
      place++;
  }
  return place;
}

/**************************************************************************
...
**************************************************************************/
static struct player *best_mil_service(void)
{
  struct player *pplayer = &game.players[0];
  int i;
  for(i = 1; i < game.nplayers; i++) {
    if(rank_calc_mil_service(&game.players[i]) < rank_calc_mil_service(pplayer) &&
       game.players[i].is_alive && !is_barbarian(&game.players[i])) {
      pplayer = &game.players[i];
    }
  }
  return pplayer;
}

/**************************************************************************
...
**************************************************************************/
static char *value_units(char *val, char *uni)
{
  static char buf[64] = "??";

  if ((strlen (val) + strlen (uni) + 1) > sizeof (buf))
    {
      return (buf);
    }

  my_snprintf(buf, sizeof(buf), "%s%s", val, uni);

  return (buf);
}

/**************************************************************************
...
**************************************************************************/
static char *number_to_ordinal_string(int num, int parens)
{
  static char buf[16];
  char *fmt;

  fmt = parens ? "(%d%s)" : "%d%s";

  switch (num)
    {
    case 1:
      my_snprintf(buf, sizeof(buf), fmt, num, _("st"));
      break;
    case 2:
      my_snprintf(buf, sizeof(buf), fmt, num, _("nd"));
      break;
    case 3:
      my_snprintf(buf, sizeof(buf), fmt, num, _("rd"));
      break;
    default:
      if (num > 0)
	{
	  my_snprintf(buf, sizeof(buf), fmt, num, _("th"));
	}
      else
	{
	  my_snprintf(buf, sizeof(buf),
		      (parens ? "(%s%s)" : "%s%s"), "??", _("th"));
	}
      break;
    }

  return (buf);
}

/**************************************************************************
...
**************************************************************************/

#define DEM_KEY_ROW_POPULATION        'N'
#define DEM_KEY_ROW_LAND_AREA         'A'
#define DEM_KEY_ROW_SETTLED_AREA      'S'
#define DEM_KEY_ROW_RESEARCH_SPEED    'R'
#define DEM_KEY_ROW_LITERACY          'L'
#define DEM_KEY_ROW_PRODUCTION        'P'
#define DEM_KEY_ROW_ECONOMICS         'E'
#define DEM_KEY_ROW_MILITARY_SERVICE  'M'
#define DEM_KEY_ROW_POLLUTION         'O'
#define DEM_KEY_COL_QUANTITY          'q'
#define DEM_KEY_COL_RANK              'r'
#define DEM_KEY_COL_BEST              'b'

enum dem_flag
{
  DEM_NONE = 0x00,
  DEM_COL_QUANTITY = 0x01,
  DEM_COL_RANK = 0x02,
  DEM_COL_BEST = 0x04,
  DEM_ROW = 0xFF
};

struct dem_key
{
  char key;
  char *name;
  enum dem_flag flag;
};

/**************************************************************************
...
**************************************************************************/
static void dem_line_item(char *outptr, int nleft, struct player *pplayer,
			  char key, enum dem_flag selcols)
{
  static char *fmt_quan = " %-18s";
  static char *fmt_rank = " %6s";
  static char *fmt_best = "   %s: %s";
  struct player *best_player;

  switch (key)
    {
    case DEM_KEY_ROW_POPULATION:
      if (selcols & DEM_COL_QUANTITY)
	{
	  outptr = end_of_strn (outptr, &nleft);
	  my_snprintf(outptr, nleft, fmt_quan,
		      population_to_text(pplayer->score.population));
	}
      if (selcols & DEM_COL_RANK)
	{
	  outptr = end_of_strn (outptr, &nleft);
	  my_snprintf (outptr, nleft, fmt_rank,
		       number_to_ordinal_string(rank_population(pplayer), TRUE));
	}
      if (selcols & DEM_COL_BEST && 
          player_has_embassy(pplayer, (best_player = best_population())) )
        {
	  outptr = end_of_strn (outptr, &nleft);
	  my_snprintf(outptr, nleft, fmt_best,
		      get_nation_name_plural(best_player->nation),
		      population_to_text(best_player->score.population));
        }
      break;
    case DEM_KEY_ROW_LAND_AREA:
      if (selcols & DEM_COL_QUANTITY)
	{
	  outptr = end_of_strn (outptr, &nleft);
	  my_snprintf (outptr, nleft, fmt_quan,
		       value_units (int_to_text (pplayer->score.landarea),
				    _(" sq. mi.")));
	}
      if (selcols & DEM_COL_RANK)
	{
	  outptr = end_of_strn (outptr, &nleft);
	  my_snprintf (outptr, nleft, fmt_rank,
		       number_to_ordinal_string(rank_landarea(pplayer), TRUE));
	}
      if (selcols & DEM_COL_BEST && 
          player_has_embassy(pplayer, (best_player = best_landarea())) )
        {
	  outptr = end_of_strn (outptr, &nleft);
	  my_snprintf (outptr, nleft, fmt_best,
		       get_nation_name_plural(best_player->nation),
		       value_units (int_to_text(best_player->score.landarea),
				    _(" sq. mi.")));
        }
      break;
    case DEM_KEY_ROW_SETTLED_AREA:
      if (selcols & DEM_COL_QUANTITY)
	{
	  outptr = end_of_strn (outptr, &nleft);
	  my_snprintf (outptr, nleft, fmt_quan,
		       value_units (int_to_text (pplayer->score.settledarea),
				    _(" sq. mi.")));
	}
      if (selcols & DEM_COL_RANK)
	{
	  outptr = end_of_strn (outptr, &nleft);
	  my_snprintf (outptr, nleft, fmt_rank,
		       number_to_ordinal_string(rank_settledarea(pplayer),
						TRUE));
	}
      if (selcols & DEM_COL_BEST && 
          player_has_embassy(pplayer, (best_player = best_settledarea())) )
        {
	  outptr = end_of_strn (outptr, &nleft);
	  my_snprintf (outptr, nleft, fmt_best,
		       get_nation_name_plural(best_player->nation),
		       value_units (int_to_text(best_player->score.settledarea),
				    _(" sq. mi.")));
        }
      break;
    case DEM_KEY_ROW_RESEARCH_SPEED:
      if (selcols & DEM_COL_QUANTITY)
	{
	  outptr = end_of_strn (outptr, &nleft);
	  my_snprintf (outptr, nleft, fmt_quan,
		       value_units (int_to_text (rank_calc_research(pplayer)),
				    "%"));
	}
      if (selcols & DEM_COL_RANK)
	{
	  outptr = end_of_strn (outptr, &nleft);
	  my_snprintf (outptr, nleft, fmt_rank,
		       number_to_ordinal_string(rank_research(pplayer), TRUE));
	}
      if (selcols & DEM_COL_BEST && 
          player_has_embassy(pplayer, (best_player = best_research())) )
        {
	  outptr = end_of_strn (outptr, &nleft);
	  my_snprintf (outptr, nleft, fmt_best,
		       get_nation_name_plural(best_player->nation),
		       value_units(int_to_text(rank_calc_research(best_player)),
				   "%"));
        }
      break;
    case DEM_KEY_ROW_LITERACY:
      if (selcols & DEM_COL_QUANTITY)
	{
	  outptr = end_of_strn (outptr, &nleft);
	  my_snprintf (outptr, nleft, fmt_quan,
		       value_units (int_to_text (rank_calc_literacy(pplayer)),
				    "%"));
	}
      if (selcols & DEM_COL_RANK)
	{
	  outptr = end_of_strn (outptr, &nleft);
	  my_snprintf (outptr, nleft, fmt_rank,
		       number_to_ordinal_string(rank_literacy(pplayer), TRUE));
	}
      if (selcols & DEM_COL_BEST && 
          player_has_embassy(pplayer, (best_player = best_literacy())) )
        {
	  outptr = end_of_strn (outptr, &nleft);
	  my_snprintf (outptr, nleft, fmt_best,
		       get_nation_name_plural(best_player->nation),
		       value_units(int_to_text(rank_calc_literacy(best_player)),
				   "%"));
        }
      break;
    case DEM_KEY_ROW_PRODUCTION:
      if (selcols & DEM_COL_QUANTITY)
	{
	  outptr = end_of_strn (outptr, &nleft);
	  my_snprintf (outptr, nleft, fmt_quan,
		       value_units (int_to_text (MAX(0,pplayer->score.mfg)),
				    _(" M tons")));
	}
      if (selcols & DEM_COL_RANK)
	{
	  outptr = end_of_strn (outptr, &nleft);
	  my_snprintf (outptr, nleft, fmt_rank,
		       number_to_ordinal_string(rank_production(pplayer), TRUE));
	}
      if (selcols & DEM_COL_BEST && 
          player_has_embassy(pplayer, (best_player = best_production())) )
        {
	  outptr = end_of_strn (outptr, &nleft);
	  my_snprintf (outptr, nleft, fmt_best,
		       get_nation_name_plural(best_player->nation),
		       value_units(int_to_text(MAX(0,best_player->score.mfg)),
				   _(" M tons")));
        }
      break;
    case DEM_KEY_ROW_ECONOMICS:
      if (selcols & DEM_COL_QUANTITY)
	{
	  outptr = end_of_strn (outptr, &nleft);
	  my_snprintf (outptr, nleft, fmt_quan,
		       value_units (int_to_text (pplayer->score.bnp),
				    _(" M goods")));
	}
      if (selcols & DEM_COL_RANK)
	{
	  outptr = end_of_strn (outptr, &nleft);
	  my_snprintf (outptr, nleft, fmt_rank,
		       number_to_ordinal_string(rank_economics(pplayer), TRUE));
	}
      if (selcols & DEM_COL_BEST && 
          player_has_embassy(pplayer, (best_player = best_economics())) )
        {
	  outptr = end_of_strn (outptr, &nleft);
	  my_snprintf (outptr, nleft, fmt_best,
		       get_nation_name_plural(best_player->nation),
		       value_units(int_to_text(best_player->score.bnp),
				   _(" M goods")));
        }
      break;
    case DEM_KEY_ROW_MILITARY_SERVICE:
      if (selcols & DEM_COL_QUANTITY)
	{
	  outptr = end_of_strn (outptr, &nleft);
	  my_snprintf (outptr, nleft, fmt_quan,
		       value_units (int_to_text (rank_calc_mil_service(pplayer)),
				    _(" months")));
	}
      if (selcols & DEM_COL_RANK)
	{
	  outptr = end_of_strn (outptr, &nleft);
	  my_snprintf (outptr, nleft, fmt_rank,
		       number_to_ordinal_string(rank_mil_service(pplayer),
						TRUE));
	}
      if (selcols & DEM_COL_BEST && 
          player_has_embassy(pplayer, (best_player = best_mil_service())) )
        {
	  outptr = end_of_strn (outptr, &nleft);
	  my_snprintf (outptr, nleft, fmt_best,
		       get_nation_name_plural(best_player->nation),
		       value_units(int_to_text(rank_calc_mil_service(best_player)),
				   _(" months")));
        }
      break;
    case DEM_KEY_ROW_POLLUTION:
      if (selcols & DEM_COL_QUANTITY)
	{
	  outptr = end_of_strn (outptr, &nleft);
	  my_snprintf (outptr, nleft, fmt_quan,
		       value_units (int_to_text (pplayer->score.pollution),
				    _(" tons")));
	}
      if (selcols & DEM_COL_RANK)
	{
	  outptr = end_of_strn (outptr, &nleft);
	  my_snprintf (outptr, nleft, fmt_rank,
		       number_to_ordinal_string(rank_pollution(pplayer), TRUE));
	}
      if (selcols & DEM_COL_BEST && 
          player_has_embassy(pplayer, (best_player = best_pollution())) )
        {
	  outptr = end_of_strn (outptr, &nleft);
	  my_snprintf (outptr, nleft, fmt_best,
		       get_nation_name_plural(best_player->nation),
		       value_units (int_to_text(best_player->score.pollution),
				    _(" tons")));
        }
      break;
    }
}

/*************************************************************************
  Send demographics report; what gets reported depends on value of
  demographics server option.  
*************************************************************************/
void report_demographics(struct connection *pconn)
{
  struct player *pplayer = pconn->player;
  char civbuf[1024];
  char buffer[4096] = "";
  int inx;
  int anyrows;
  enum dem_flag selcols;
  char *outptr = buffer;
  int nleft = sizeof(buffer);
  static char *fmt_name = "%-18s";
  static struct dem_key keytable[] =
  {
    { DEM_KEY_ROW_POPULATION,       N_("Population"),       DEM_ROW },
    { DEM_KEY_ROW_LAND_AREA,        N_("Land Area"),        DEM_ROW },
    { DEM_KEY_ROW_SETTLED_AREA,     N_("Settled Area"),     DEM_ROW },
    { DEM_KEY_ROW_RESEARCH_SPEED,   N_("Research Speed"),   DEM_ROW },
    { DEM_KEY_ROW_LITERACY,         N_("Literacy"),         DEM_ROW },
    { DEM_KEY_ROW_PRODUCTION,       N_("Production"),       DEM_ROW },
    { DEM_KEY_ROW_ECONOMICS,        N_("Economics"),        DEM_ROW },
    { DEM_KEY_ROW_MILITARY_SERVICE, N_("Military Service"), DEM_ROW },
    { DEM_KEY_ROW_POLLUTION,        N_("Pollution"),        DEM_ROW },
    { DEM_KEY_COL_QUANTITY,         "",                     DEM_COL_QUANTITY },
    { DEM_KEY_COL_RANK,             "",                     DEM_COL_RANK },
    { DEM_KEY_COL_BEST,             "",                     DEM_COL_BEST }
  };

  anyrows = FALSE;
  selcols = DEM_NONE;
  for (inx = 0; inx < ARRAY_SIZE(keytable); inx++) {
    if (strchr(game.demography, keytable[inx].key)) {
      if (keytable[inx].flag == DEM_ROW) {
	anyrows = TRUE;
      } else {
	selcols |= keytable[inx].flag;
      }
    }
  }

  if (pplayer == NULL || !pplayer->is_alive || !anyrows
      || (selcols == DEM_NONE)) {
    page_conn(&pconn->self, _("Demographics Report:"),
	      _("Sorry, the Demographics report is unavailable."),
	      "");
    return;
  }

  my_snprintf (civbuf, sizeof(civbuf), _("The %s of the %s"),
	       get_government_name (pplayer->government),
	       get_nation_name_plural (pplayer->nation));

  for (inx = 0; inx < ARRAY_SIZE(keytable); inx++)
    {
      if ((strchr (game.demography, keytable[inx].key)) &&
	  (keytable[inx].flag == DEM_ROW))
	{
	  outptr = end_of_strn (outptr, &nleft);
	  my_snprintf (outptr, nleft, fmt_name, _(keytable[inx].name));
	  outptr = end_of_strn (outptr, &nleft);
	  dem_line_item (outptr, nleft, pplayer, keytable[inx].key, selcols);
	  outptr = end_of_strn (outptr, &nleft);
	  mystrlcpy (outptr, "\n", nleft);
	}
    }

  page_conn(&pconn->self, _("Demographics Report:"), civbuf, buffer);
}

/**************************************************************************
Create a log file of the civilizations so you can see what was happening.
**************************************************************************/
static void log_civ_score(void)
{ 
  int fom; /* fom == figure-of-merit */
  char *ptr;
  char line[64];
  int i, n, ln, ni;
  int index, dummy;
  char name[64];
  int foms = 0;
  int plrs = -1;
  enum { SL_CREATE, SL_APPEND, SL_UNSPEC } oper = SL_UNSPEC;

  static char *magic = "#FREECIV SCORELOG %s\n";
  static char *logname = "civscore.log";
  static char *endmark = "end";
  static FILE *fp = NULL;
  static int disabled = 0;

  /* add new tags only at end of this list;
     maintaining the order of old tags is critical */
  static char *tags[] =
  {
    "pop",
    "bnp",
    "mfg",
    "cities",
    "techs",
    "munits",
    "settlers",     /* "original" tags end here */

    "wonders",
    "techout",
    "landarea",
    "settledarea",
    "pollution",
    "literacy",
    "spaceship",    /* new 1.8.2 tags end here */

    "gold",
    "taxrate",
    "scirate",
    "luxrate",

    "riots",
    "happypop",
    "contentpop",
    "unhappypop",

    "taxmen",
    "scientists",
    "elvis",
    "gov",
    "corruption",   /* new 1.11.5 tags end here*/

    NULL            /* end of list */
  };

  if (disabled)
    {
      return;
    }

  if (!fp)
    {
      if (game.year == GAME_START_YEAR)
	{
	  oper = SL_CREATE;
	}
      else
	{
	  fp = fopen (logname, "r");
	  if (!fp)
	    {
	      oper = SL_CREATE;
	    }
	  else
	    {
	      for (ln = 1; ; ln++)
		{
		  if (!(fgets (line, sizeof (line), fp)))
		    {
		      if (ferror (fp)) {
			freelog(LOG_ERROR, "Can't read scorelog file header!");
		      } else {
			freelog(LOG_ERROR, "Unterminated scorelog file header!");
		      }
		      goto log_civ_score_disable;
		    }

		  ptr = strchr (line, '\n');
		  if (!ptr)
		    {
		      freelog(LOG_ERROR,
			      "Scorelog file line %d is too long!", ln);
		      goto log_civ_score_disable;
		    }
		  *ptr = '\0';

		  if (plrs < 0)
		    {
		      if ((ln == 1) && (line[0] == '#'))
			{
			  continue;
			}

		      if (0 == strcmp (line, endmark))
			{
			  plrs++;
			}
		      else
			{
			  if (!(tags[foms]))
			    {
			      freelog (LOG_ERROR,
				       "Too many entries in scorelog header!");
			      goto log_civ_score_disable;
			    }

			  ni = sscanf (line, "%d %s", &index, name);
			  if ((ni != 2) || (index != foms) ||
			      (0 != strcmp (name, tags[foms])))
			    {
			      freelog (LOG_ERROR,
				       "Scorelog file line %d is bad!", ln);
			      goto log_civ_score_disable;
			    }

			  foms++;
			}
		    }
		  else
		    {
		      if (0 == strcmp (line, endmark))
			{
			  break;
			}

		      ni = sscanf (line, "%d %s", &index, name);
		      if ((ni != 2) || (index != plrs))
			{
			  freelog (LOG_ERROR,
				   "Scorelog file line %d is bad!", ln);
			  goto log_civ_score_disable;
			}
		      if (0 != strcmp (name, game.players[plrs].name))
			{
			  oper = SL_CREATE;
			  break;
			}

		      plrs++;
		    }
		}

	      if (oper == SL_UNSPEC)
		{
		  if (fseek (fp, -100, SEEK_END))
		    {
		      freelog (LOG_ERROR,
			       "Can't seek to end of scorelog file!");
		      goto log_civ_score_disable;
		    }

		  if (!(fgets (line, sizeof (line), fp)))
		    {
		      if (ferror (fp)) {
			freelog(LOG_ERROR, "Can't read scorelog file!");
		      } else {
			freelog(LOG_ERROR, "Unterminated scorelog file!");
		      }
		      goto log_civ_score_disable;
		    }
		  ptr = strchr (line, '\n');
		  if (!ptr)
		    {
		      freelog (LOG_ERROR,
			       "Scorelog file line is too long!");
		      goto log_civ_score_disable;
		    }
		  *ptr = '\0';

		  if (!(fgets (line, sizeof (line), fp)))
		    {
		      if (ferror (fp))
			{
			  freelog (LOG_ERROR,
				   "Can't read scorelog file!");
			}
		      else
			{
			  freelog (LOG_ERROR,
				   "Unterminated scorelog file!");
			}
		      goto log_civ_score_disable;
		    }
		  ptr = strchr (line, '\n');
		  if (!ptr)
		    {
		      freelog (LOG_ERROR,
			       "Scorelog file line is too long!");
		      goto log_civ_score_disable;
		    }
		  *ptr = '\0';

		  ni = sscanf (line, "%d %d %d %d",
			       &dummy, &dummy, &index, &dummy);
		  if (ni != 4)
		    {
		      freelog (LOG_ERROR,
			       "Scorelog file line is bad!");
		      goto log_civ_score_disable;
		    }

		  if (index >= game.year)
		    {
		      freelog (LOG_ERROR,
			       "Scorelog years overlap -- logging disabled!");
		      goto log_civ_score_disable;
		    }

		  tags[foms] = NULL;
		  oper = SL_APPEND;
		}

	      fclose (fp);
	      fp = NULL;
	    }
	}

      switch (oper)
	{
	case SL_CREATE:
	  fp = fopen (logname, "w");
	  if (!fp)
	    {
	      freelog (LOG_ERROR, "Can't open scorelog file for creation!");
	      goto log_civ_score_disable;
	    }
	  fprintf (fp, magic, VERSION_STRING);
	  for (i = 0; tags[i]; i++)
	    {
	      fprintf (fp, "%d %s\n", i, tags[i]);
	    }
	  fprintf (fp, "%s\n", endmark);
	  for (n = 0; n < game.nplayers; n++)
	    {
	      if (is_barbarian (&(game.players[n])))
		{
		  continue;
		}
	      fprintf (fp, "%d %s\n", n, game.players[n].name);
	    }
	  fprintf (fp, "%s\n", endmark);
	  break;
	case SL_APPEND:
	  fp = fopen (logname, "a");
	  if (!fp)
	    {
	      freelog (LOG_ERROR, "Can't open scorelog file for appending!");
	      goto log_civ_score_disable;
	    }
	  break;
	default:
	  freelog (LOG_ERROR, "log_civ_score: bad operation %d", (int)oper);
	  goto log_civ_score_disable;
	}
    }

  for (i = 0; tags[i]; i++)
    {
      for (n = 0; n < game.nplayers; n++)
	{
	  if (is_barbarian (&(game.players[n])))
	    {
	      continue;
	    }
	  switch (i)
	    {
	    case 0:
	      fom = total_player_citizens (&(game.players[n]));
	      break;
	    case 1:
	      fom = game.players[n].score.bnp;
	      break;
	    case 2:
	      fom = game.players[n].score.mfg;
	      break;
	    case 3:
	      fom = game.players[n].score.cities;
	      break;
	    case 4:
	      fom = game.players[n].score.techs;
	      break;
	    case 5:
	      fom = 0;
	      /* count up military units */
	      unit_list_iterate (game.players[n].units, punit)
		if (is_military_unit (punit))
		  fom++;
	      unit_list_iterate_end;
	      break;
	    case 6:
	      fom = 0;
	      /* count up settlers */
	      unit_list_iterate (game.players[n].units, punit)
		if (unit_flag (punit, F_CITIES))
		  fom++;
	      unit_list_iterate_end;
	      break;
	    case 7:
	      fom = game.players[n].score.wonders;
	      break;
	    case 8:
	      fom = game.players[n].score.techout;
	      break;
	    case 9:
	      fom = game.players[n].score.landarea;
	      break;
	    case 10:
	      fom = game.players[n].score.settledarea;
	      break;
	    case 11:
	      fom = game.players[n].score.pollution;
	      break;
	    case 12:
	      fom = game.players[n].score.literacy;
	      break;
	    case 13:
	      fom = game.players[n].score.spaceship;
	      break;
	    case 14: /* gold */
	      fom = game.players[n].economic.gold;
	      break;
	    case 15: /* taxrate */
	      fom = game.players[n].economic.tax;
	      break;
	    case 16: /* scirate */
	      fom = game.players[n].economic.science;
	      break;
	    case 17: /* luxrate */
	      fom = game.players[n].economic.luxury;
	      break;
	    case 18: /* riots */
	      fom = 0;
	      city_list_iterate (game.players[n].cities, pcity)
                if (pcity->anarchy > 0)
                  fom++;
	      city_list_iterate_end;
	      break;
	    case 19: /* happypop */
	      fom = game.players[n].score.happy;
	      break;
	    case 20: /* contentpop */
	      fom = game.players[n].score.content;
	      break;
	    case 21: /* unhappypop */
	      fom = game.players[n].score.unhappy;
	      break;
	    case 22: /* taxmen */
	      fom = game.players[n].score.taxmen;
	      break;
	    case 23: /* scientists */
	      fom = game.players[n].score.scientists;
	      break;
	    case 24: /* elvis */
	      fom = game.players[n].score.elvis;
	      break;
	    case 25: /* gov */
	      fom = game.players[n].government;
	      break;
            case 26: /* corruption */
	      fom = 0;
	      city_list_iterate (game.players[n].cities, pcity)
                fom+=pcity->corruption;
	      city_list_iterate_end;
	      break;
	    default:
	      fom = 0; /* -Wall demands we init this somewhere! */
	    }

	  fprintf (fp, "%d %d %d %d\n", i, n, game.year, fom);
	}
    }

  fflush (fp);

  return;

log_civ_score_disable:

  if (fp)
    {
      fclose (fp);
      fp = NULL;
    }

  disabled = 1;
}

/**************************************************************************
...
**************************************************************************/
void make_history_report(void)
{
  static int report=0;
  static int time_to_report=20;
  int i;

  for (i=0;i<game.nplayers;i++)
    civ_score(&game.players[i]);

  if (game.scorelog)
    log_civ_score();

  if (game.nplayers==1)
    return;

  time_to_report--;

  if (time_to_report>0) 
    return;

  time_to_report=myrand(20)+20;

  historian_generic(report);
  
  report=(report+1)%5;
}

/**************************************************************************
...
**************************************************************************/
void report_scores(int final)
{
  int i,j=0;
  char buffer[4096];

  struct player_score_entry *size=
    fc_malloc(sizeof(struct player_score_entry)*game.nplayers);

  for (i=0;i<game.nplayers;i++) {
    size[i].value=civ_score(&game.players[i]);
    size[i].idx=i;
  }
  qsort(size, game.nplayers, sizeof(struct player_score_entry), secompare);
  buffer[0]=0;
  for (i=0;i<game.nplayers;i++) {
    if( !is_barbarian(&game.players[size[i].idx]) ) {
      cat_snprintf(buffer, sizeof(buffer),
		   _("%2d: The %s %s scored %d points\n"), j+1, _(greatness[j]),
		   get_nation_name_plural(game.players[size[i].idx].nation),
		   size[i].value);
      j++;
    }
  }
  free(size);
  page_conn(&game.game_connections,
	    final ? _("Final Report:") : _("Progress Scores:"),
	    _("The Greatest Civilizations in the world."), buffer);

}

/**************************************************************************
This function pops up a non-modal message dialog on the player's desktop
**************************************************************************/
void page_conn(struct conn_list *dest, char *caption, char *headline,
	       char *lines) {
  page_conn_etype(dest, caption, headline, lines, -1);
}


/**************************************************************************
This function pops up a non-modal message dialog on the player's desktop
event == -1: message should not be ignored by clients watching AI players with 
             ai_popup_windows off.  Example: Server Options, Demographics 
             Report, etc.
event == BROADCAST_EVENT: message can safely be ignored by clients watching AI
                          players with ai_popup_windows off.
         For example: Herodot's report... and similar messages.
**************************************************************************/
void page_conn_etype(struct conn_list *dest, char *caption, char *headline,
		      char *lines, int event) 
{
  int len;
  struct packet_generic_message genmsg;

  len = my_snprintf(genmsg.message, sizeof(genmsg.message),
		    "%s\n%s\n%s", caption, headline, lines);
  if (len == -1) {
    freelog(LOG_ERROR, "Message truncated in page_conn_etype()!");
  }
  genmsg.event = event;
  
  lsend_packet_generic_message(dest, PACKET_PAGE_MSG, &genmsg);
}
