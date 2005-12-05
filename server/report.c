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

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "events.h"
#include "fciconv.h"
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

#include "citytools.h"
#include "report.h"
#include "score.h"

static void page_conn_etype(struct conn_list *dest, const char *caption,
			    const char *headline, const char *lines,
			    enum event_type event);
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

static const char scorelog_magic[] = "#FREECIV SCORELOG2 ";

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

static const char *area_to_text(int value);
static const char *percent_to_text(int value);
static const char *production_to_text(int value);
static const char *economics_to_text(int value);
static const char *mil_service_to_text(int value);
static const char *pollution_to_text(int value);

/*
 * Describes a row.
 */
static struct dem_row {
  const char key;
  const char *name;
  int (*get_value) (struct player *);
  const char *(*to_text) (int);
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
  struct player_score_entry size[game.nplayers];

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
  my_snprintf(title, sizeof(title), _(historian_message[which_news]),
    _(historian_name[myrand(ARRAY_SIZE(historian_name))]));
  page_conn_etype(&game.game_connections, _("Historian Publishes!"),
		  title, buffer, E_BROADCAST_REPORT);
}

/**************************************************************************
 Returns the number of wonders the given city has.
**************************************************************************/
static int nr_wonders(struct city *pcity)
{
  int result = 0;

  built_impr_iterate(pcity, i) {
    if (is_wonder(i)) {
      result++;
    }
  } built_impr_iterate_end;

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
  struct city_score_entry size[NUM_BEST_CITIES];
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

static int get_pop(struct player *pplayer)
{
  return total_player_citizens(pplayer);
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
  return (pplayer->score.techout * 100) / total_bulbs_required(pplayer);
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

static int get_cities(struct player *pplayer)
{
  return pplayer->score.cities;
}

static int get_techs(struct player *pplayer)
{
  return pplayer->score.techs;
}

static int get_munits(struct player *pplayer)
{
  int result = 0;

  /* count up military units */
  unit_list_iterate(pplayer->units, punit) {
    if (is_military_unit(punit)) {
      result++;
    }
  } unit_list_iterate_end;

  return result;
}

static int get_settlers(struct player *pplayer)
{
  int result = 0;

  /* count up settlers */
  unit_list_iterate(pplayer->units, punit) {
    if (unit_flag(punit, F_CITIES)) {
      result++;
    }
  } unit_list_iterate_end;

  return result;
}

static int get_wonders(struct player *pplayer)
{
  return pplayer->score.wonders;
}

static int get_techout(struct player *pplayer)
{
  return pplayer->score.techout;
}

static int get_literacy2(struct player *pplayer)
{
  return pplayer->score.literacy;
}

static int get_spaceship(struct player *pplayer)
{
  return pplayer->score.spaceship;
}

static int get_gold(struct player *pplayer)
{
  return pplayer->economic.gold;
}

static int get_taxrate(struct player *pplayer)
{
  return pplayer->economic.tax;
}

static int get_scirate(struct player *pplayer)
{
  return pplayer->economic.science;
}

static int get_luxrate(struct player *pplayer)
{
  return pplayer->economic.luxury;
}

static int get_riots(struct player *pplayer)
{
  int result = 0;

  city_list_iterate(pplayer->cities, pcity) {
    if (pcity->anarchy > 0) {
      result++;
    }
  } city_list_iterate_end;

  return result;
}

static int get_happypop(struct player *pplayer)
{
  return pplayer->score.happy;
}

static int get_contentpop(struct player *pplayer)
{
  return pplayer->score.content;
}

static int get_unhappypop(struct player *pplayer)
{
  return pplayer->score.unhappy;
}

static int get_taxmen(struct player *pplayer)
{
  return pplayer->score.taxmen;
}

static int get_scientists(struct player *pplayer)
{
  return pplayer->score.scientists;
}

static int get_elvis(struct player *pplayer)
{
  return pplayer->score.elvis;
}

static int get_gov(struct player *pplayer)
{
  return pplayer->government;
}

static int get_corruption(struct player *pplayer)
{
  int result = 0;

  city_list_iterate(pplayer->cities, pcity) {
    result += pcity->corruption;
  } city_list_iterate_end;

  return result;
}

/**************************************************************************
...
**************************************************************************/
static const char *value_units(int val, const char *uni)
{
  static char buf[64];

  if (my_snprintf(buf, sizeof(buf), "%s%s", int_to_text(val), uni) == -1) {
    die("String truncated in value_units()!");
  }

  return buf;
}

/**************************************************************************
  Helper functions which transform the given value to a string
  depending on the unit.
**************************************************************************/
static const char *area_to_text(int value)
{
  return value_units(value, _(" sq. mi."));
}

static const char *percent_to_text(int value)
{
  return value_units(value, "%");
}

static const char *production_to_text(int value)
{
  return value_units(MAX(0, value), _(" M tons"));
}

static const char *economics_to_text(int value)
{
  return value_units(value, _(" M goods"));
}

static const char *mil_service_to_text(int value)
{
  return value_units(value, PL_(" month", " months", value));
}

static const char *pollution_to_text(int value)
{
  return value_units(value, PL_(" ton", " tons", value));
}

/**************************************************************************
...
**************************************************************************/
static const char *number_to_ordinal_string(int num)
{
  static char buf[16];
  char fmt[] = "(%d%s)";

  assert(num > 0);

  if ((num % 10) == 1 && num != 11) {
    my_snprintf(buf, sizeof(buf), fmt, num, _("st"));
  } else if ((num % 10) == 2 && num != 12) {
    my_snprintf(buf, sizeof(buf), fmt, num, _("nd"));
  } else if ((num % 10) == 3 && num != 13) {
    my_snprintf(buf, sizeof(buf), fmt, num, _("rd"));
  } else {
    my_snprintf(buf, sizeof(buf), fmt, num, _("th"));
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
    const char *text = prow->to_text(prow->get_value(pplayer));

    cat_snprintf(outptr, out_size, " %s", text);
    cat_snprintf(outptr, out_size, "%*s",
		 18 - get_internal_string_length(text), "");
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

    if(player_has_embassy(pplayer, best_player) && (pplayer != best_player)) {
      cat_snprintf(outptr, out_size, "   %s: %s",
		   get_nation_name_plural(best_player->nation),
		   prow->to_text(prow->get_value(best_player)));
    }
  }
}

/*************************************************************************
  Verify that a given demography string is valid.  See
  game.demography.

  Other settings callback functions are in settings.c, but this one uses
  static values from this file so it's done separately.
*************************************************************************/
bool is_valid_demography(const char *demography, const char **error_string)
{
  int len = strlen(demography), i;

  /* We check each character individually to see if it's valid.  This
   * does not check for duplicate entries. */
  for (i = 0; i < len; i++) {
    bool found = FALSE;
    int j;

    /* See if the character is a valid column label. */
    for (j = 0; j < ARRAY_SIZE(coltable); j++) {
      if (demography[i] == coltable[j].key) {
	found = TRUE;
	break;
      }
    }

    if (found) {
      continue;
    }

    /* See if the character is a valid row label. */
    for (j = 0; j < ARRAY_SIZE(rowtable); j++) {
      if (demography[i] == rowtable[j].key) {
	found = TRUE;
	break;
      }
    }

    if (!found) {
      /* The character is invalid. */
      *error_string = _("Demography string contains invalid characters. "
			"Try \"help demography\".");
      return FALSE;
    }
  }

  /* Looks like all characters were valid. */
  *error_string = NULL;
  return TRUE;
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
      const char *name = _(rowtable[i].name);

      cat_snprintf(buffer, sizeof(buffer), "%s", name);
      cat_snprintf(buffer, sizeof(buffer), "%*s",
		   18 - get_internal_string_length(name), "");
      dem_line_item(buffer, sizeof(buffer), pplayer, &rowtable[i], selcols);
      sz_strlcat(buffer, "\n");
    }
  }

  page_conn(&pconn->self, _("Demographics Report:"), civbuf, buffer);
}

/**************************************************************************
  Reads the whole file denoted by fp. Sets last_turn and id to the
  values contained in the file. Returns the player_names indexed by
  player_no at the end of the log file.

  Returns TRUE iff the file had read successfully.
**************************************************************************/
static bool scan_score_log(FILE * fp, int *last_turn, char *id,
			   char **player_names)
{
  int line_nr;
  char line[80];
  char *ptr;

  *last_turn = -1;
  id[0] = '\0';

  for (line_nr = 1;; line_nr++) {
    if (!fgets(line, sizeof(line), fp)) {
      if (feof(fp) != 0) {
	break;
      }
      freelog(LOG_ERROR, "Can't read scorelog file header!");
      return FALSE;
    }

    ptr = strchr(line, '\n');
    if (!ptr) {
      freelog(LOG_ERROR, "Scorelog file line is too long!");
      return FALSE;
    }
    *ptr = '\0';

    if (line_nr == 1) {
      if (strncmp(line, scorelog_magic, strlen(scorelog_magic)) != 0) {
	freelog(LOG_ERROR, "Bad magic in file line %d!", line_nr);
	return FALSE;
      }
    }

    if (strncmp(line, "id ", strlen("id ")) == 0) {
      if (strlen(id) > 0) {
	freelog(LOG_ERROR, "Multiple ID entries!");
	return FALSE;
      }
      mystrlcpy(id, line + strlen("id "), MAX_ID_LEN);
      if (strcmp(id, game.id) != 0) {
	freelog(LOG_ERROR, "IDs don't match! game='%s' scorelog='%s'",
		game.id, id);
	return FALSE;
      }
    }

    if (strncmp(line, "turn ", strlen("turn ")) == 0) {
      int turn;

      if (sscanf(line + strlen("turn "), "%d", &turn) != 1) {
	freelog(LOG_ERROR, "Scorelog file line is bad!");
	return FALSE;
      }

      assert(turn > *last_turn);
      *last_turn = turn;
    }

    if (strncmp(line, "addplayer ", strlen("addplayer ")) == 0) {
      int turn, plr_no;
      char plr_name[MAX_LEN_NAME];

      if (sscanf
	  (line + strlen("addplayer "), "%d %d %s", &turn, &plr_no,
	   plr_name) != 3) {
	freelog(LOG_ERROR, "Scorelog file line is bad!");
	return FALSE;
      }

      mystrlcpy(player_names[plr_no], plr_name, MAX_LEN_NAME);
    }

    if (strncmp(line, "delplayer ", strlen("delplayer ")) == 0) {
      int turn, plr_no;

      if (sscanf(line + strlen("delplayer "), "%d %d", &turn, &plr_no) != 2) {
	freelog(LOG_ERROR, "Scorelog file line is bad!");
	return FALSE;
      }

      player_names[plr_no][0] = '\0';
    }
  }

  if (*last_turn == -1) {
    freelog(LOG_ERROR, "Scorelog contains no turn!");
    return FALSE;
  }

  if (strlen(id) == 0) {
    freelog(LOG_ERROR, "Scorelog contains no ID!");
    return FALSE;
  }

  if (*last_turn + 1 != game.turn) {
    freelog(LOG_ERROR, "Scorelog doesn't match savegame!");
    return FALSE;
  }

  return TRUE;
}

/**************************************************************************
Create a log file of the civilizations so you can see what was happening.
**************************************************************************/
static void log_civ_score(void)
{
  static const char logname[] = "civscore.log";
  static FILE *fp = NULL;
  static bool disabled = FALSE;
  static char player_names[MAX_NUM_PLAYERS +
			   MAX_NUM_BARBARIANS][MAX_LEN_NAME];
  static char *player_name_ptrs[MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS];
  static int last_turn = -1;

  /* 
   * Add new tags only at end of this list. Maintaining the order of
   * old tags is critical.
   */
  static const struct {
    char *name;
    int (*get_value) (struct player *);
  } score_tags[] = {
    {"pop",             get_pop},
    {"bnp",             get_economics},
    {"mfg",             get_production},
    {"cities",          get_cities},
    {"techs",           get_techs},
    {"munits",          get_munits},
    {"settlers",        get_settlers},  /* "original" tags end here */

    {"wonders",         get_wonders},
    {"techout",         get_techout},
    {"landarea",        get_landarea},
    {"settledarea",     get_settledarea},
    {"pollution",       get_pollution},
    {"literacy",        get_literacy2},
    {"spaceship",       get_spaceship}, /* new 1.8.2 tags end here */

    {"gold",            get_gold},
    {"taxrate",         get_taxrate},
    {"scirate",         get_scirate},
    {"luxrate",         get_luxrate},
    {"riots",           get_riots},
    {"happypop",        get_happypop},
    {"contentpop",      get_contentpop},
    {"unhappypop",      get_unhappypop},
    {"taxmen",          get_taxmen},
    {"scientists",      get_scientists},
    {"elvis",           get_elvis},
    {"gov",             get_gov},
    {"corruption",      get_corruption} /* new 1.11.5 tags end here */
  };

  enum { SL_CREATE, SL_APPEND, SL_UNSPEC } oper = SL_UNSPEC;
  int i;
  char id[MAX_ID_LEN];

  if (!player_name_ptrs[0]) {
    int i;

    for (i = 0; i < ARRAY_SIZE(player_names); i++) {
      player_name_ptrs[i] = player_names[i];
      player_names[i][0] = '\0';
    }
  }

  if (disabled) {
    return;
  }

  if (!fp) {
    if (game.year == GAME_START_YEAR) {
      oper = SL_CREATE;
    } else {
      fp = fopen(logname, "r");
      if (!fp) {
	oper = SL_CREATE;
      } else {
	if (!scan_score_log(fp, &last_turn, id, player_name_ptrs)) {
	  goto log_civ_score_disable;
	}
	oper = SL_APPEND;

	fclose(fp);
	fp = NULL;
      }
    }

    switch (oper) {
    case SL_CREATE:
      fp = fopen(logname, "w");
      if (!fp) {
	freelog(LOG_ERROR, "Can't open scorelog file for creation!");
	goto log_civ_score_disable;
      }
      fprintf(fp, "%s%s\n", scorelog_magic, VERSION_STRING);
      fprintf(fp, 
	      "\n"
	      "# For a specification of the format of this see doc/README.scorelog or \n"
	      "# <http://www.freeciv.org/lxr/source/doc/README.scorelog?v=cvs>.\n"
	      "\n");

      fprintf(fp, "id %s\n", game.id);
      for (i = 0; i<ARRAY_SIZE(score_tags); i++) {
	fprintf(fp, "tag %d %s\n", i, score_tags[i].name);
      }
      break;
    case SL_APPEND:
      fp = fopen(logname, "a");
      if (!fp) {
	freelog(LOG_ERROR, "Can't open scorelog file for appending!");
	goto log_civ_score_disable;
      }
      break;
    default:
      freelog(LOG_ERROR, "log_civ_score: bad operation %d", (int) oper);
      goto log_civ_score_disable;
    }
  }

#define GOOD_PLAYER(p) ((p)->is_alive)

  if (game.turn > last_turn) {
    fprintf(fp, "turn %d %d %s\n", game.turn, game.year, textyear(game.year));
    last_turn = game.turn;
  }

  for (i = 0; i < ARRAY_SIZE(player_names); i++) {
    if (strlen(player_names[i]) > 0 && !GOOD_PLAYER(get_player(i))) {
      fprintf(fp, "delplayer %d %d\n", game.turn - 1, i);
      player_names[i][0] = '\0';
    }
  }

  players_iterate(pplayer) {
    if (GOOD_PLAYER(pplayer)
	&& strlen(player_names[pplayer->player_no]) == 0) {
      fprintf(fp, "addplayer %d %d %s\n", game.turn, pplayer->player_no,
	      pplayer->name);
      mystrlcpy(player_name_ptrs[pplayer->player_no], pplayer->name,
		MAX_LEN_NAME);
    }
  } players_iterate_end;

  players_iterate(pplayer) {
    if (GOOD_PLAYER(pplayer)
	&& strcmp(player_names[pplayer->player_no], pplayer->name) != 0) {
      fprintf(fp, "delplayer %d %d\n", game.turn - 1, pplayer->player_no);
      fprintf(fp, "addplayer %d %d %s\n", game.turn, pplayer->player_no,
	      pplayer->name);
      mystrlcpy(player_names[pplayer->player_no], pplayer->name,
		MAX_LEN_NAME);
    }
  } players_iterate_end;

  for (i = 0; i<ARRAY_SIZE(score_tags); i++) {
    players_iterate(pplayer) {
      if (!GOOD_PLAYER(pplayer)) {
	continue;
      }

      fprintf(fp, "data %d %d %d %d\n", game.turn, i, pplayer->player_no,
	      score_tags[i].get_value(pplayer));
    } players_iterate_end;
  }

  fflush(fp);

  return;

log_civ_score_disable:

  if (fp) {
    fclose(fp);
    fp = NULL;
  }

  disabled = TRUE;
}

#undef GOOD_PLAYER

/**************************************************************************
  ...
**************************************************************************/
void make_history_report(void)
{
  static enum historian_type report = HISTORIAN_FIRST;
  static int time_to_report=20;

  if (game.scorelog) {
    log_civ_score();
  }

  if (game.nplayers == 1) {
    return;
  }

  time_to_report--;

  if (time_to_report > 0) {
    return;
  }

  time_to_report=myrand(20) + 20;

  historian_generic(report);
  
  report++;
  if (report > HISTORIAN_LAST) {
    report = HISTORIAN_FIRST;
  }
}

/**************************************************************************
 Inform clients about player scores during a game.
**************************************************************************/
void report_progress_scores(void)
{
  int i, j = 0;
  char buffer[4096];
  struct player_score_entry size[game.nplayers];

  players_iterate(pplayer) {
    if (!is_barbarian(pplayer)) {
      size[j].value = get_civ_score(pplayer);
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
  page_conn(&game.game_connections,
	    _("Progress Scores:"),
	    _("The Greatest Civilizations in the world."), buffer);
}

/**************************************************************************
  Inform clients about player scores and statistics when the game ends.
**************************************************************************/
void report_final_scores(void)
{
  int i, j = 0;
  struct player_score_entry size[game.nplayers];
  struct packet_endgame_report packet;

  players_iterate(pplayer) {
    if (!is_barbarian(pplayer)) {
      size[j].value = get_civ_score(pplayer);
      size[j].player = pplayer;
      j++;
    }
  } players_iterate_end;

  qsort(size, j, sizeof(struct player_score_entry), secompare);

  packet.nscores = j;
  for (i = 0; i < j; i++) {
    packet.id[i] = size[i].player->player_no;
    packet.score[i] = size[i].value;
    packet.pop[i] = get_pop(size[i].player) * 1000; 
    packet.bnp[i] = get_economics(size[i].player); 
    packet.mfg[i] = get_production(size[i].player); 
    packet.cities[i] = get_cities(size[i].player); 
    packet.techs[i] = get_techs(size[i].player) - 1; 
    packet.mil_service[i] = get_mil_service(size[i].player); 
    packet.wonders[i] = get_wonders(size[i].player); 
    packet.research[i] = get_research(size[i].player); 
    packet.landarea[i] = get_landarea(size[i].player); 
    packet.settledarea[i] = get_settledarea(size[i].player); 
    packet.literacy[i] = get_literacy(size[i].player); 
    packet.spaceship[i] = get_spaceship(size[i].player); 
  }  

  lsend_packet_endgame_report(&game.game_connections, &packet);
}	

/**************************************************************************
This function pops up a non-modal message dialog on the player's desktop
**************************************************************************/
void page_conn(struct conn_list *dest, const char *caption, 
	       const char *headline, const char *lines) {
  page_conn_etype(dest, caption, headline, lines, E_REPORT);
}


/**************************************************************************
This function pops up a non-modal message dialog on the player's desktop

event == E_REPORT: message should not be ignored by clients watching
                   AI players with ai_popup_windows off.  Example:
                   Server Options, Demographics Report, etc.

event == E_BROADCAST_REPORT: message can safely be ignored by clients
                   watching AI players with ai_popup_windows off.  For
                   example: Herodot's report... and similar messages.
**************************************************************************/
static void page_conn_etype(struct conn_list *dest, const char *caption,
			    const char *headline, const char *lines,
			    enum event_type event)
{
  int len;
  struct packet_page_msg genmsg;

  len = my_snprintf(genmsg.message, sizeof(genmsg.message),
		    "%s\n%s\n%s", caption, headline, lines);
  if (len == -1) {
    freelog(LOG_ERROR, "Message truncated in page_conn_etype()!");
  }
  genmsg.event = event;
  
  lsend_packet_page_msg(dest, &genmsg);
}
