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

#include "citytools.h"
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

#define HISTORIAN_FIRST		HISTORIAN_RICHEST
#define HISTORIAN_LAST 		HISTORIAN_LARGEST

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
  struct player *player;
  int value;
};

struct city_score_entry {
  struct city *city;
  int value;
};

static int get_population(struct player *pplayer);
static int get_landarea(struct player *pplayer);
static int get_settledarea(struct player *pplayer);
static int get_research(struct player *pplayer);
static int get_literacy(struct player *pplayer);
static int get_production(struct player *pplayer);
static int get_economics(struct player *pplayer);
static int get_pollution(struct player *pplayer);
static int get_mil_service(struct player *pplayer);

static char *area_to_text(int value);
static char *percent_to_text(int value);
static char *production_to_text(int value);
static char *economics_to_text(int value);
static char *mil_service_to_text(int value);
static char *pollution_to_text(int value);

/*
 * Describes a row.
 */
static struct dem_row {
  char key;
  char *name;
  int (*get_value) (struct player *);
  char *(*to_text) (int);
  bool greater_values_are_better;
} rowtable[] = {
  {'N', N_("Population"),       get_population,  population_to_text,  TRUE },
  {'A', N_("Land Area"),        get_landarea,    area_to_text,        TRUE },
  {'S', N_("Settled Area"),     get_settledarea, area_to_text,        TRUE },
  {'R', N_("Research Speed"),   get_research,    percent_to_text,     TRUE },
  {'L', N_("Literacy"),         get_literacy,    percent_to_text,     TRUE },
  {'P', N_("Production"),       get_production,  production_to_text,  TRUE },
  {'E', N_("Economics"),        get_economics,   economics_to_text,   TRUE },
  {'M', N_("Military Service"), get_mil_service, mil_service_to_text, FALSE },
  {'O', N_("Pollution"),        get_pollution,   pollution_to_text,   FALSE }
};

enum dem_flag {
  DEM_COL_QUANTITY,
  DEM_COL_RANK,
  DEM_COL_BEST
};

/*
 * Describes a column.
 */
static struct dem_col
{
  char key;
  enum dem_flag flag;
} coltable[] = {
    { 'q', DEM_COL_QUANTITY },
    { 'r', DEM_COL_RANK },
    { 'b', DEM_COL_BEST }
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
  int i, j = 0, rank = 0;
  char buffer[4096];
  char title[1024];
  struct player_score_entry *size =
      fc_malloc(sizeof(struct player_score_entry) * game.nplayers);

  players_iterate(pplayer) {
    if (pplayer->is_alive && !is_barbarian(pplayer)) {
      switch(which_news) {
      case HISTORIAN_RICHEST:
	size[j].value = pplayer->economic.gold;
	break;
      case HISTORIAN_ADVANCED:
	size[j].value = (pplayer->score.techs + pplayer->future_tech);
	break;
      case HISTORIAN_MILITARY:
	size[j].value = pplayer->score.units;
	break;
      case HISTORIAN_HAPPIEST: 
	size[j].value =
	    (((pplayer->score.happy - pplayer->score.unhappy) * 1000) /
	     (1 + total_player_citizens(pplayer)));
	break;
      case HISTORIAN_LARGEST:
	size[j].value = total_player_citizens(pplayer);
	break;
      }
      size[j].player = pplayer;
      j++;
    } /* else the player is dead or barbarian */
  } players_iterate_end;

  qsort(size, j, sizeof(struct player_score_entry), secompare);
  buffer[0] = '\0';
  for (i = 0; i < j; i++) {
    if (i == 0 || size[i].value < size[i - 1].value) {
      rank = i;
    }
    cat_snprintf(buffer, sizeof(buffer),
		 _("%2d: The %s %s\n"), rank + 1, _(greatness[rank]),
		 get_nation_name_plural(size[i].player->nation));
  }
  free(size);
  my_snprintf(title, sizeof(title), _(historian_message[which_news]),
    _(historian_name[myrand(ARRAY_SIZE(historian_name))]));
  page_conn_etype(&game.game_connections, _("Historian Publishes!"),
		  title, buffer, BROADCAST_EVENT);
}

/**************************************************************************
 Returns the number of wonders the given city has.
**************************************************************************/
static int nr_wonders(struct city *pcity)
{
  int result = 0;

  impr_type_iterate(i) {
    if (is_wonder(i) && city_got_building(pcity, i)) {
      result++;
    }
  } impr_type_iterate_end;

  return result;
}

/**************************************************************************
  Send report listing the "best" 5 cities in the world.
**************************************************************************/
void report_top_five_cities(struct conn_list *dest)
{
  const int NUM_BEST_CITIES = 5;
  /* a wonder equals WONDER_FACTOR citizen */
  const int WONDER_FACTOR = 5;
  struct city_score_entry *size =
      fc_malloc(sizeof(struct city_score_entry) * NUM_BEST_CITIES);
  int i;
  char buffer[4096];

  for (i = 0; i < NUM_BEST_CITIES; i++) {
    size[i].value = 0;
    size[i].city = NULL;
  }

  players_iterate(pplayer) {
    city_list_iterate(pplayer->cities, pcity) {
      int value_of_pcity = pcity->size + nr_wonders(pcity) * WONDER_FACTOR;

      if (value_of_pcity > size[NUM_BEST_CITIES - 1].value) {
	size[NUM_BEST_CITIES - 1].value = value_of_pcity;
	size[NUM_BEST_CITIES - 1].city = pcity;
	qsort(size, NUM_BEST_CITIES, sizeof(struct player_score_entry),
	      secompare);
      }
    } city_list_iterate_end;
  } players_iterate_end;

  buffer[0] = '\0';
  for (i = 0; i < NUM_BEST_CITIES; i++) {
    int wonders;

    if (!size[i].city) {
	/* 
	 * pcity may be NULL if there are less then NUM_BEST_CITIES in
	 * the whole game.
	 */
      break;
    }

    cat_snprintf(buffer, sizeof(buffer),
		 _("%2d: The %s City of %s of size %d, "), i + 1,
		 get_nation_name(city_owner(size[i].city)->nation),
		 size[i].city->name, size[i].city->size);

    wonders = nr_wonders(size[i].city);
    if (wonders == 0) {
      cat_snprintf(buffer, sizeof(buffer), _("with no wonders\n"));
    } else {
      cat_snprintf(buffer, sizeof(buffer),
		   PL_("with %d wonder\n", "with %d wonders\n", wonders),
		   wonders);}
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
  char buffer[4096];

  buffer[0] = '\0';

  impr_type_iterate(i) {
    if (is_wonder(i)) {
      struct city *pcity = find_city_wonder(i);

      if (pcity) {
	cat_snprintf(buffer, sizeof(buffer), _("%s in %s (%s)\n"),
		     get_impr_name_ex(pcity, i), pcity->name,
		     get_nation_name(city_owner(pcity)->nation));
      } else if(game.global_wonders[i] != 0) {
	cat_snprintf(buffer, sizeof(buffer), _("%s has been DESTROYED\n"),
		     get_improvement_type(i)->name);
      }
    }
  } impr_type_iterate_end;

  impr_type_iterate(i) {
    if (is_wonder(i)) {
      players_iterate(pplayer) {
	city_list_iterate(pplayer->cities, pcity) {
	  if (pcity->currently_building == i && !pcity->is_building_unit) {
	    cat_snprintf(buffer, sizeof(buffer),
			 _("(building %s in %s (%s))\n"),
			 get_improvement_type(i)->name, pcity->name,
			 get_nation_name(pplayer->nation));
	  }
	} city_list_iterate_end;
      } players_iterate_end;
    }
  } impr_type_iterate_end;

  page_conn(dest, _("Traveler's Report:"),
	    _("Wonders of the World"), buffer);
}

/**************************************************************************
 Helper functions which return the value for the given player.
**************************************************************************/
static int get_population(struct player *pplayer)
{
  return pplayer->score.population;
}

static int get_landarea(struct player *pplayer)
{
    return pplayer->score.landarea;
}

static int get_settledarea(struct player *pplayer)
{
  return pplayer->score.settledarea;
}

static int get_research(struct player *pplayer)
{
  return (pplayer->score.techout * 100) / (total_bulbs_required(pplayer));
}

static int get_literacy(struct player *pplayer)
{
  int pop = civ_population(pplayer);

  if (pop <= 0) {
    return 0;
  } else if (pop >= 10000) {
    return pplayer->score.literacy / (pop / 100);
  } else {
    return (pplayer->score.literacy * 100) / pop;
  }
}

static int get_production(struct player *pplayer)
{
  return pplayer->score.mfg;
}

static int get_economics(struct player *pplayer)
{
  return pplayer->score.bnp;
}

static int get_pollution(struct player *pplayer)
{
  return pplayer->score.pollution;
}

static int get_mil_service(struct player *pplayer)
{
  return (pplayer->score.units * 5000) / (10 + civ_population(pplayer));
}

/**************************************************************************
...
**************************************************************************/
static char *value_units(int val, char *uni)
{
  static char buf[64];

  if (my_snprintf(buf, sizeof(buf), "%s%s", int_to_text(val), uni) == -1) {
    freelog(LOG_ERROR, "String truncated in value_units()!");
    assert(0);
    exit(EXIT_FAILURE);
  }

  return buf;
}

/**************************************************************************
  Helper functions which transform the given value to a string
  depending on the unit.
**************************************************************************/
static char *area_to_text(int value)
{
  return value_units(value, _(" sq. mi."));
}

static char *percent_to_text(int value)
{
  return value_units(value, "%");
}

static char *production_to_text(int value)
{
  return value_units(MAX(0, value), _(" M tons"));
}

static char *economics_to_text(int value)
{
  return value_units(value, _(" M goods"));
}

static char *mil_service_to_text(int value)
{
  return value_units(value, PL_(" month", " months", value));
}

static char *pollution_to_text(int value)
{
  return value_units(value, PL_(" ton", " tons", value));
}

/**************************************************************************
...
**************************************************************************/
static char *number_to_ordinal_string(int num)
{
  static char buf[16];
  char fmt[] = "(%d%s)";

  assert(num > 0);

  switch (num) {
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
    my_snprintf(buf, sizeof(buf), fmt, num, _("th"));
    break;
  }

  return buf;
}

/**************************************************************************
...
**************************************************************************/
static void dem_line_item(char *outptr, size_t out_size,
			  struct player *pplayer, struct dem_row *prow,
			  int selcols)
{
  if (TEST_BIT(selcols, DEM_COL_QUANTITY)) {
      cat_snprintf(outptr, out_size, " %-18s",
		   prow->to_text(prow->get_value(pplayer)));
  }

  if (TEST_BIT(selcols, DEM_COL_RANK)) {
    int basis = prow->get_value(pplayer);
    int place = 1;

    players_iterate(other) {
      if (other->is_alive && !is_barbarian(other) &&
	  ((prow->greater_values_are_better
	    && prow->get_value(other) > basis)
	   || (!prow->greater_values_are_better
	       && prow->get_value(other) < basis))) {
	place++;
      }
    } players_iterate_end;

    cat_snprintf(outptr, out_size, " %6s", number_to_ordinal_string(place));
  }

  if (TEST_BIT(selcols, DEM_COL_BEST)) {
    struct player *best_player = pplayer;
    int best_value = prow->get_value(pplayer);

    players_iterate(other) {
      if (other->is_alive && !is_barbarian(other)) {
	int value = prow->get_value(other);

	if ((prow->greater_values_are_better && value > best_value)
	    || (!prow->greater_values_are_better && value < best_value)) {
	  best_player = other;
	  best_value = value;
	}
      }
    } players_iterate_end;

    if (player_has_embassy(pplayer, best_player)) {
      cat_snprintf(outptr, out_size, "   %s: %s",
		   get_nation_name_plural(best_player->nation),
		   prow->to_text(prow->get_value(best_player)));
    }
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
  char buffer[4096];
  unsigned int i;
  bool anyrows;
  int selcols;

  selcols = 0;
  for (i = 0; i < ARRAY_SIZE(coltable); i++) {
    if (strchr(game.demography, coltable[i].key)) {
      selcols |= (1u << coltable[i].flag);
    }
  }

  anyrows = FALSE;
  for (i = 0; i < ARRAY_SIZE(rowtable); i++) {
    if (strchr(game.demography, rowtable[i].key)) {
      anyrows = TRUE;
      break;
    }
  }

  if (!pplayer || !pplayer->is_alive || !anyrows || selcols == 0) {
    page_conn(&pconn->self, _("Demographics Report:"),
	      _("Sorry, the Demographics report is unavailable."), "");
    return;
  }

  my_snprintf(civbuf, sizeof(civbuf), _("The %s of the %s"),
	      get_government_name(pplayer->government),
	      get_nation_name_plural(pplayer->nation));

  buffer[0] = '\0';
  for (i = 0; i < ARRAY_SIZE(rowtable); i++) {
    if (strchr(game.demography, rowtable[i].key)) {
      cat_snprintf(buffer, sizeof(buffer), "%-18s", _(rowtable[i].name));
      dem_line_item(buffer, sizeof(buffer), pplayer, &rowtable[i], selcols);
      sz_strlcat(buffer, "\n");
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
  int i, ln, ni;
  int index, dummy;
  char name[64];
  int foms = 0;
  int plrs = -1;
  enum { SL_CREATE, SL_APPEND, SL_UNSPEC } oper = SL_UNSPEC;

  static char *magic = "#FREECIV SCORELOG %s\n";
  static char *logname = "civscore.log";
  static char *endmark = "end";
  static FILE *fp = NULL;
  static bool disabled = FALSE;

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
	  players_iterate(pplayer) {
	    if (!is_barbarian(pplayer)) {
	      fprintf(fp, "%d %s\n", pplayer->player_no, pplayer->name);
	    }
	  } players_iterate_end
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
      players_iterate(pplayer) {
	if (is_barbarian(pplayer)) {
	  continue;
	}

	switch (i)
	    {
	    case 0:
	      fom = total_player_citizens (pplayer);
	      break;
	    case 1:
	      fom = pplayer->score.bnp;
	      break;
	    case 2:
	      fom = pplayer->score.mfg;
	      break;
	    case 3:
	      fom = pplayer->score.cities;
	      break;
	    case 4:
	      fom = pplayer->score.techs;
	      break;
	    case 5:
	      fom = 0;
	      /* count up military units */
	      unit_list_iterate (pplayer->units, punit)
		if (is_military_unit (punit))
		  fom++;
	      unit_list_iterate_end;
	      break;
	    case 6:
	      fom = 0;
	      /* count up settlers */
	      unit_list_iterate (pplayer->units, punit)
		if (unit_flag (punit, F_CITIES))
		  fom++;
	      unit_list_iterate_end;
	      break;
	    case 7:
	      fom = pplayer->score.wonders;
	      break;
	    case 8:
	      fom = pplayer->score.techout;
	      break;
	    case 9:
	      fom = pplayer->score.landarea;
	      break;
	    case 10:
	      fom = pplayer->score.settledarea;
	      break;
	    case 11:
	      fom = pplayer->score.pollution;
	      break;
	    case 12:
	      fom = pplayer->score.literacy;
	      break;
	    case 13:
	      fom = pplayer->score.spaceship;
	      break;
	    case 14: /* gold */
	      fom = pplayer->economic.gold;
	      break;
	    case 15: /* taxrate */
	      fom = pplayer->economic.tax;
	      break;
	    case 16: /* scirate */
	      fom = pplayer->economic.science;
	      break;
	    case 17: /* luxrate */
	      fom = pplayer->economic.luxury;
	      break;
	    case 18: /* riots */
	      fom = 0;
	      city_list_iterate (pplayer->cities, pcity)
                if (pcity->anarchy > 0)
                  fom++;
	      city_list_iterate_end;
	      break;
	    case 19: /* happypop */
	      fom = pplayer->score.happy;
	      break;
	    case 20: /* contentpop */
	      fom = pplayer->score.content;
	      break;
	    case 21: /* unhappypop */
	      fom = pplayer->score.unhappy;
	      break;
	    case 22: /* taxmen */
	      fom = pplayer->score.taxmen;
	      break;
	    case 23: /* scientists */
	      fom = pplayer->score.scientists;
	      break;
	    case 24: /* elvis */
	      fom = pplayer->score.elvis;
	      break;
	    case 25: /* gov */
	      fom = pplayer->government;
	      break;
            case 26: /* corruption */
	      fom = 0;
	      city_list_iterate (pplayer->cities, pcity)
                fom+=pcity->corruption;
	      city_list_iterate_end;
	      break;
	    default:
	      fom = 0; /* -Wall demands we init this somewhere! */
	    }

	  fprintf (fp, "%d %d %d %d\n", i, pplayer->player_no, game.year, fom);
      } players_iterate_end;
    }

  fflush (fp);

  return;

log_civ_score_disable:

  if (fp)
    {
      fclose (fp);
      fp = NULL;
    }

  disabled = TRUE;
}

/**************************************************************************
...
**************************************************************************/
void make_history_report(void)
{
  static enum historian_type report = HISTORIAN_FIRST;
  static int time_to_report=20;

  players_iterate(pplayer) {
    civ_score(pplayer);
  } players_iterate_end;

  if (game.scorelog)
    log_civ_score();

  if (game.nplayers==1)
    return;

  time_to_report--;

  if (time_to_report>0) 
    return;

  time_to_report=myrand(20)+20;

  historian_generic(report);
  
  report++;
  if (report > HISTORIAN_LAST) {
    report = HISTORIAN_FIRST;
  }
}

/**************************************************************************
...
**************************************************************************/
void report_scores(bool final)
{
  int i, j = 0;
  char buffer[4096];
  struct player_score_entry *size =
      fc_malloc(sizeof(struct player_score_entry) * game.nplayers);

  players_iterate(pplayer) {
    if (!is_barbarian(pplayer)) {
      size[j].value = civ_score(pplayer);
      size[j].player = pplayer;
      j++;
    }
  } players_iterate_end;

  qsort(size, j, sizeof(struct player_score_entry), secompare);
  buffer[0] = '\0';

  for (i = 0; i < j; i++) {
    cat_snprintf(buffer, sizeof(buffer),
		 PL_("%2d: The %s %s scored %d point\n",
		     "%2d: The %s %s scored %d points\n",
		     size[i].value),
		 i + 1, _(greatness[i]),
		 get_nation_name_plural(size[i].player->nation),
		 size[i].value);
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
  size_t len;
  struct packet_generic_message genmsg;

  len = my_snprintf(genmsg.message, sizeof(genmsg.message),
		    "%s\n%s\n%s", caption, headline, lines);
  if (len == -1) {
    freelog(LOG_ERROR, "Message truncated in page_conn_etype()!");
  }
  genmsg.event = event;
  
  lsend_packet_generic_message(dest, PACKET_PAGE_MSG, &genmsg);
}
