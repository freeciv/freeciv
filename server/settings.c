/********************************************************************** 
 Freeciv - Copyright (C) 1996-2004 - The Freeciv Project
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

#include "fcintl.h"

#include "map.h"

#include "gamelog.h"
#include "report.h"
#include "settings.h"
#include "stdinhand.h"

/* Category names must match the values in enum sset_category. */
const char *sset_category_names[] = {N_("Geological"),
				     N_("Ecological"),
				     N_("Sociological"),
				     N_("Economic"),
				     N_("Military"),
				     N_("Scientific"),
				     N_("Internal"),
				     N_("Networking")};

/* Level names must match the values in enum sset_level. */
const char *sset_level_names[] = {N_("None"),
				  N_("All"),
				  N_("Vital"),
				  N_("Situational"),
				  N_("Rare")};
const int OLEVELS_NUM = ARRAY_SIZE(sset_level_names);


/**************************************************************************
  Verify that notradesize is always smaller than fulltradesize
**************************************************************************/
static bool notradesize_callback(int value, const char **error_message)
{
  if (value < game.fulltradesize) {
    return TRUE;
  }

  *error_message = _("notradesize must be always smaller than "
		     "fulltradesize; keeping old value.");
  return FALSE;
}

/**************************************************************************
  Verify that fulltradesize is always bigger than notradesize
**************************************************************************/
static bool fulltradesize_callback(int value, const char **error_message)
{
  if (value > game.notradesize) {
    return TRUE;
  }

  *error_message = _("fulltradesize must be always bigger than "
		     "notradesize; keeping old value.");
  return FALSE;
}

/**************************************************************************
  A callback invoked when autotoggle is set.
**************************************************************************/
static bool autotoggle_callback(bool value, const char **reject_message)
{
  reject_message = NULL;
  if (!value) {
    return TRUE;
  }

  players_iterate(pplayer) {
    if (!pplayer->ai.control && !pplayer->is_connected) {
      toggle_ai_player_direct(NULL, pplayer);
    }
  } players_iterate_end;

  return TRUE;
}

/*************************************************************************
  Verify that a given allowtake string is valid.  See
  game.allow_take.
*************************************************************************/
static bool allowtake_callback(const char *value, const char **error_string)
{
  int len = strlen(value), i;
  bool havecharacter_state = FALSE;

  /* We check each character individually to see if it's valid.  This
   * does not check for duplicate entries.
   *
   * We also track the state of the machine.  havecharacter_state is
   * true if the preceeding character was a primary label, e.g.
   * NHhAadb.  It is false if the preceeding character was a modifier
   * or if this is the first character. */

  for (i = 0; i < len; i++) {
    /* Check to see if the character is a primary label. */
    if (strchr("HhAadb", value[i])) {
      havecharacter_state = TRUE;
      continue;
    }

    /* If we've already passed a primary label, check to see if the
     * character is a modifier. */
    if (havecharacter_state && strchr("1234", value[i])) {
      havecharacter_state = FALSE;
      continue;
    }

    /* Looks like the character was invalid. */
    *error_string = _("Allowed take string contains invalid\n"
		      "characters.  Try \"help allowtake\".");
    return FALSE;
  }

  /* All characters were valid. */
  *error_string = NULL;
  return TRUE;
}

/*************************************************************************
  Verify that a given startunits string is valid.  See
  game.start_units.
*************************************************************************/
static bool startunits_callback(const char *value, const char **error_string)
{
  int len = strlen(value), i;
  bool have_founder = FALSE;

  /* We check each character individually to see if it's valid, and
   * also make sure there is at least one city founder. */

  for (i = 0; i < len; i++) {
    /* Check for a city founder */
    if (value[i] == 'c') {
      have_founder = TRUE;
      continue;
    }
    if (strchr("cwxksfdDaA", value[i])) {
      continue;
    }

    /* Looks like the character was invalid. */
    *error_string = _("Starting units string contains invalid\n"
		      "characters.  Try \"help startunits\".");
    return FALSE;
  }

  if (!have_founder) {
    *error_string = _("Starting units string does not contain\n"
		      "at least one city founder.  Try \n"
		      "\"help startunits\".");
    return FALSE;
  }
  /* All characters were valid. */
  *error_string = NULL;
  return TRUE;
}

/*************************************************************************
  Verify that a given maxplayers string is valid.
*************************************************************************/
static bool maxplayers_callback(int value, const char **error_string)
{
  if (value < game.nplayers) {
    *error_string =_("Number of players is higher than requested value; "
		     "Keeping old value.");
    return FALSE;
  }

  error_string = NULL;
  return TRUE;
}

#define GEN_BOOL(name, value, sclass, scateg, slevel, to_client,	\
		 short_help, extra_help, func, default)			\
  {name, sclass, to_client, short_help, extra_help, SSET_BOOL,		\
      scateg, slevel, &value, default, func,				\
      NULL, 0, NULL, 0, 0,						\
      NULL, NULL, NULL, 0},

#define GEN_INT(name, value, sclass, scateg, slevel, to_client,		\
		short_help, extra_help, func, min, max, default)	\
  {name, sclass, to_client, short_help, extra_help, SSET_INT,		\
      scateg, slevel,							\
      NULL, FALSE, NULL,						\
      &value, default, func, min, max,					\
      NULL, NULL, NULL, 0},

#define GEN_STRING(name, value, sclass, scateg, slevel, to_client,	\
		   short_help, extra_help, func, default)		\
  {name, sclass, to_client, short_help, extra_help, SSET_STRING,	\
      scateg, slevel,							\
      NULL, FALSE, NULL,						\
      NULL, 0, NULL, 0, 0,						\
      value, default, func, sizeof(value)},

#define GEN_END							\
  {NULL, SSET_LAST, SSET_SERVER_ONLY, NULL, NULL, SSET_INT,	\
      SSET_NUM_CATEGORIES, SSET_NONE,				\
      NULL, FALSE, NULL,					\
      NULL, 0, NULL, 0, 0,					\
      NULL, NULL, NULL},

struct settings_s settings[] = {

  /* These should be grouped by sclass */
  
  /* Map size parameters: adjustable if we don't yet have a map */  
  GEN_INT("size", map.size, SSET_MAP_SIZE,
	  SSET_GEOLOGY, SSET_VITAL, SSET_TO_CLIENT,
          N_("Map size in 1,000 tiles units"),
          N_("This value is used to determine xsize and ysize\n"
             "  size = 4 is a normal map of 4,000 tiles (default)\n"
             "  size = 20 is a huge map of 20,000 tiles"), NULL,
          MAP_MIN_SIZE, MAP_MAX_SIZE, MAP_DEFAULT_SIZE)
  GEN_INT("topology", map.topology_id, SSET_MAP_SIZE,
	  SSET_GEOLOGY, SSET_VITAL, SSET_TO_CLIENT,
	  N_("The map topology index"),
	  N_("Two-dimensional maps can wrap at the north-south or \n"
	     "east-west edges, and use a cartesian or isometric \n"
	     "rectangular grid.  See the manual for further explanation.\n"
             "  0 Flat Earth (unwrapped)\n"
             "  1 Earth (wraps E-W)\n"
             "  2 Uranus (wraps N-S)\n"
             "  3 Donut World (wraps N-S, E-W)\n"
	     "  4 Flat Earth (isometric)\n"
	     "  5 Earth (isometric)\n"
	     "  6 Uranus (isometric)\n"
	     "  7 Donut World (isometric)\n"
	     "  8 Flat Earth (hexagonal)\n"
	     "  9 Earth (hexagonal)\n"
	     " 10 Uranus (hexagonal)\n"
	     " 11 Donut World (hexagonal)\n"
	     " 12 Flat Earth (iso-hex)\n"
	     " 13 Earth (iso-hex)\n"
	     " 14 Uranus (iso-hex)\n"
	     " 15 Donut World (iso-hex)"
          ), NULL,
	  MAP_MIN_TOPO, MAP_MAX_TOPO, MAP_DEFAULT_TOPO)

  /* Map generation parameters: once we have a map these are of historical
   * interest only, and cannot be changed.
   */
  GEN_INT("generator", map.generator,
	  SSET_MAP_GEN, SSET_GEOLOGY, SSET_VITAL,  SSET_TO_CLIENT,
	  N_("Method used to generate map"),
	  N_("1 = standard, with random continents;\n\n"
	     "2 = equally sized large islands with one player each, and "
	     "twice that many smaller islands;\n\n"
	     "3 = equally sized large islands with one player each, and "
	     "a number of other islands of similar size;\n\n"
	     "4 = equally sized large islands with two players on every "
	     "island (or one with three players for an odd number of "
	     "players), and additional smaller islands;\n\n"
	     "5 = one or more large earthlike continents with some "
	     "scatter.\n\n"
	     "Note: values 2,3 and 4 generate \"fairer\" (but more boring) "
	     "maps.\n"
	     "(Zero indicates a scenario map.)"), NULL,
	  MAP_MIN_GENERATOR, MAP_MAX_GENERATOR, MAP_DEFAULT_GENERATOR)

  GEN_BOOL("tinyisles", map.tinyisles,
	   SSET_MAP_GEN, SSET_GEOLOGY, SSET_RARE, SSET_TO_CLIENT,
	   N_("Presence of 1x1 islands"),
	   N_("0 = no 1x1 islands; 1 = some 1x1 islands"), NULL,
	   MAP_DEFAULT_TINYISLES)

  GEN_BOOL("separatepoles", map.separatepoles,
	   SSET_MAP_GEN, SSET_GEOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	   N_("Whether the poles are separate continents"),
	   N_("0 = continents may attach to poles; 1 = poles will "
	      "be separate"), NULL, 
	   MAP_DEFAULT_SEPARATE_POLES)

  GEN_BOOL("alltemperate", map.alltemperate, 
           SSET_MAP_GEN, SSET_GEOLOGY, SSET_RARE, SSET_TO_CLIENT,
	   N_("All the map is temperate"),
	   N_("0 = normal Earth-like planet; 1 = all-temperate planet "),
	   NULL, MAP_DEFAULT_ALLTEMPERATE)

  GEN_INT("temperature", map.temperature,
 	  SSET_MAP_GEN, SSET_GEOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
 	  N_("Average temperature of the planet"),
 	  N_("Small values will give a cold map, while larger values will "
             "give a hotter map\n"
	     "100 means a very dry and hot planet: no polar arctic zones, "
             "only tropical and dry zones\n"
	     "70 means a hot planet: little polar ice\n"
             "50 means a temperatue planet: normal polar, cold, temperate, "
             "and tropical zones; a desert zone overlaps tropical and "
             "temperate zones\n"
	     "30 means a cold planet: little tropical zones\n"
	     "0 means a very cold planet: large polar zones and no tropics"), 
          NULL,
  	  MAP_MIN_TEMPERATURE, MAP_MAX_TEMPERATURE, MAP_DEFAULT_TEMPERATURE)
 
  GEN_INT("landmass", map.landpercent,
	  SSET_MAP_GEN, SSET_GEOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Amount of land vs ocean"), "", NULL,
	  MAP_MIN_LANDMASS, MAP_MAX_LANDMASS, MAP_DEFAULT_LANDMASS)

  GEN_INT("mountains", map.mountains,
	  SSET_MAP_GEN, SSET_GEOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Amount of hills/mountains"),
	  N_("Small values give flat maps, higher values give more "
	     "hills and mountains."), NULL,
	  MAP_MIN_MOUNTAINS, MAP_MAX_MOUNTAINS, MAP_DEFAULT_MOUNTAINS)

  GEN_INT("rivers", map.riverlength,
	  SSET_MAP_GEN, SSET_GEOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Amount of river squares"), "", NULL,
	  MAP_MIN_RIVERS, MAP_MAX_RIVERS, MAP_DEFAULT_RIVERS)

  GEN_INT("grass", map.grasssize,
	  SSET_MAP_GEN, SSET_ECOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Amount of grass squares"), "", NULL,
	  MAP_MIN_GRASS, MAP_MAX_GRASS, MAP_DEFAULT_GRASS)

  GEN_INT("forests", map.forestsize,
	  SSET_MAP_GEN, SSET_ECOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Amount of forest squares"), "", NULL, 
	  MAP_MIN_FORESTS, MAP_MAX_FORESTS, MAP_DEFAULT_FORESTS)

  GEN_INT("swamps", map.swampsize,
	  SSET_MAP_GEN, SSET_ECOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Amount of swamp squares"), "", NULL, 
	  MAP_MIN_SWAMPS, MAP_MAX_SWAMPS, MAP_DEFAULT_SWAMPS)

  GEN_INT("deserts", map.deserts,
	  SSET_MAP_GEN, SSET_ECOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Amount of desert squares"), "", NULL, 
	  MAP_MIN_DESERTS, MAP_MAX_DESERTS, MAP_DEFAULT_DESERTS)

  GEN_INT("mapseed", map.seed,
	  SSET_MAP_GEN, SSET_INTERNAL, SSET_RARE, SSET_SERVER_ONLY,
	  N_("Map generation random seed"),
	  N_("The same seed will always produce the same map; "
	     "for zero (the default) a seed will be chosen based on "
	     "the time, to give a random map."), NULL, 
	  MAP_MIN_SEED, MAP_MAX_SEED, MAP_DEFAULT_SEED)

  /* Map additional stuff: huts and specials.  gameseed also goes here
   * because huts and specials are the first time the gameseed gets used (?)
   * These are done when the game starts, so these are historical and
   * fixed after the game has started.
   */
  GEN_INT("gameseed", game.seed,
	  SSET_MAP_ADD, SSET_INTERNAL, SSET_RARE, SSET_SERVER_ONLY,
	  N_("General random seed"),
	  N_("For zero (the default) a seed will be chosen based "
	     "on the time."), NULL, 
	  GAME_MIN_SEED, GAME_MAX_SEED, GAME_DEFAULT_SEED)

  GEN_INT("specials", map.riches,
	  SSET_MAP_ADD, SSET_ECOLOGY, SSET_VITAL, SSET_TO_CLIENT,
	  N_("Amount of \"special\" resource squares"), 
	  N_("Special resources improve the basic terrain type they "
	     "are on.  The server variable's scale is parts per "
	     "thousand."), NULL,
	  MAP_MIN_RICHES, MAP_MAX_RICHES, MAP_DEFAULT_RICHES)

  GEN_INT("huts", map.huts,
	  SSET_MAP_ADD, SSET_GEOLOGY, SSET_VITAL, SSET_TO_CLIENT,
	  N_("Amount of huts (minor tribe villages)"), "", NULL,
	  MAP_MIN_HUTS, MAP_MAX_HUTS, MAP_DEFAULT_HUTS)

  /* Options affecting numbers of players and AI players.  These only
   * affect the start of the game and can not be adjusted after that.
   * (Actually, minplayers does also affect reloads: you can't start a
   * reload game until enough players have connected (or are AI).)
   */
  GEN_INT("minplayers", game.min_players,
	  SSET_PLAYERS, SSET_INTERNAL, SSET_VITAL,
          SSET_TO_CLIENT,
	  N_("Minimum number of players"),
	  N_("There must be at least this many players (connected "
	     "players or AI's) before the game can start."), NULL,
	  GAME_MIN_MIN_PLAYERS, GAME_MAX_MIN_PLAYERS, GAME_DEFAULT_MIN_PLAYERS)

  GEN_INT("maxplayers", game.max_players,
	  SSET_PLAYERS, SSET_INTERNAL, SSET_VITAL, SSET_TO_CLIENT,
	  N_("Maximum number of players"),
          N_("The maximal number of human and AI players who can be in "
             "the game. When this number of players are connected in "
             "the pregame state, any new players who try to connect "
             "will be rejected."), maxplayers_callback,
	  GAME_MIN_MAX_PLAYERS, GAME_MAX_MAX_PLAYERS, GAME_DEFAULT_MAX_PLAYERS)

  GEN_INT("aifill", game.aifill,
	  SSET_PLAYERS, SSET_INTERNAL, SSET_VITAL, SSET_TO_CLIENT,
	  N_("Number of players to fill to with AI's"),
	  N_("If there are fewer than this many players when the "
	     "game starts, extra AI players will be created to "
	     "increase the total number of players to the value of "
	     "this option."), NULL, 
	  GAME_MIN_AIFILL, GAME_MAX_AIFILL, GAME_DEFAULT_AIFILL)

  /* Game initialization parameters (only affect the first start of the game,
   * and not reloads).  Can not be changed after first start of game.
   */
  GEN_STRING("startunits", game.start_units,
	     SSET_GAME_INIT, SSET_SOCIOLOGY, SSET_VITAL, SSET_TO_CLIENT,
             N_("List of player's initial units"),
             N_("This should be a string of characters, each of which "
		"specifies a unit role. There must be at least one city "
		"founder in the string. The characters and their "
		"meanings are:\n"
		"    c   = City founder (eg. Settlers)\n"
		"    w   = Terrain worker (eg. Engineers)\n"
		"    x   = Explorer (eg. Explorer)\n"
		"    k   = Gameloss (eg. King)\n"
		"    s   = Diplomat (eg. Diplomat)\n"
		"    f   = Ferryboat (eg. Trireme)\n"
		"    d   = Ok defense unit (eg. Warriors)\n"
		"    D   = Good defense unit (eg. Phalanx)\n"
		"    a   = Fast attack unit (eg. Horsemen)\n"
		"    A   = Strong attack unit (eg. Catapult)\n"),
		startunits_callback, GAME_DEFAULT_START_UNITS)

  GEN_INT("dispersion", game.dispersion,
	  SSET_GAME_INIT, SSET_SOCIOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Area where initial units are located"),
	  N_("This is half the length of a side of the square within "
	     "which the initial units are dispersed."), NULL,
	  GAME_MIN_DISPERSION, GAME_MAX_DISPERSION, GAME_DEFAULT_DISPERSION)

  GEN_INT("gold", game.gold,
	  SSET_GAME_INIT, SSET_ECONOMICS, SSET_VITAL, SSET_TO_CLIENT,
	  N_("Starting gold per player"), "", NULL,
	  GAME_MIN_GOLD, GAME_MAX_GOLD, GAME_DEFAULT_GOLD)

  GEN_INT("techlevel", game.tech,
	  SSET_GAME_INIT, SSET_SCIENCE, SSET_VITAL, SSET_TO_CLIENT,
	  N_("Number of initial advances per player"), "", NULL,
	  GAME_MIN_TECHLEVEL, GAME_MAX_TECHLEVEL, GAME_DEFAULT_TECHLEVEL)

  GEN_INT("researchcost", game.researchcost,
	  SSET_RULES, SSET_SCIENCE, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Points required to gain a new advance"),
	  N_("This affects how quickly players can research new "
	     "technology."), NULL,
	  GAME_MIN_RESEARCHCOST, GAME_MAX_RESEARCHCOST, 
	  GAME_DEFAULT_RESEARCHCOST)

  GEN_INT("techpenalty", game.techpenalty,
	  SSET_RULES, SSET_SCIENCE, SSET_RARE, SSET_TO_CLIENT,
	  N_("Percentage penalty when changing tech"),
	  N_("If you change your current research technology, and you have "
	     "positive research points, you lose this percentage of those "
	     "research points.  This does not apply if you have just gained "
	     "tech this turn."), NULL,
	  GAME_MIN_TECHPENALTY, GAME_MAX_TECHPENALTY,
	  GAME_DEFAULT_TECHPENALTY)

  GEN_INT("diplcost", game.diplcost,
	  SSET_RULES, SSET_SCIENCE, SSET_RARE, SSET_TO_CLIENT,
	  N_("Penalty when getting tech from treaty"),
	  N_("For each advance you gain from a diplomatic treaty, you lose "
	     "research points equal to this percentage of the cost to "
	     "research an new advance.  You can end up with negative "
	     "research points if this is non-zero."), NULL, 
	  GAME_MIN_DIPLCOST, GAME_MAX_DIPLCOST, GAME_DEFAULT_DIPLCOST)

  GEN_INT("conquercost", game.conquercost,
	  SSET_RULES, SSET_SCIENCE, SSET_RARE, SSET_TO_CLIENT,
	  N_("Penalty when getting tech from conquering"),
	  N_("For each advance you gain by conquering an enemy city, you "
	     "lose research points equal to this percentage of the cost "
	     "to research an new advance.  You can end up with negative "
	     "research points if this is non-zero."), NULL,
	  GAME_MIN_CONQUERCOST, GAME_MAX_CONQUERCOST,
	  GAME_DEFAULT_CONQUERCOST)

  GEN_INT("freecost", game.freecost,
	  SSET_RULES, SSET_SCIENCE, SSET_RARE, SSET_TO_CLIENT,
	  N_("Penalty when getting a free tech"),
	  N_("For each advance you gain \"for free\" (other than covered by "
	     "diplcost or conquercost: specifically, from huts or from the "
	     "Great Library), you lose research points equal to this "
	     "percentage of the cost to research a new advance.  You can "
	     "end up with negative research points if this is non-zero."), 
	  NULL, 
	  GAME_MIN_FREECOST, GAME_MAX_FREECOST, GAME_DEFAULT_FREECOST)

  GEN_INT("foodbox", game.foodbox,
	  SSET_RULES, SSET_ECONOMICS, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Food required for a city to grow"), "", NULL,
	  GAME_MIN_FOODBOX, GAME_MAX_FOODBOX, GAME_DEFAULT_FOODBOX)

  GEN_INT("aqueductloss", game.aqueductloss,
	  SSET_RULES, SSET_ECONOMICS, SSET_RARE, SSET_TO_CLIENT,
	  N_("Percentage food lost when need aqueduct"),
	  N_("If a city would expand, but it can't because it needs "
	     "an Aqueduct (or Sewer System), it loses this percentage "
	     "of its foodbox (or half that amount if it has a "
	     "Granary)."), NULL, 
	  GAME_MIN_AQUEDUCTLOSS, GAME_MAX_AQUEDUCTLOSS, 
	  GAME_DEFAULT_AQUEDUCTLOSS)

  GEN_INT("fulltradesize", game.fulltradesize,
	  SSET_RULES, SSET_ECONOMICS, SSET_RARE, SSET_TO_CLIENT,
	  N_("Minimum city size to get full trade"),
	  N_("There is a trade penalty in all cities smaller than this. "
	     "The penalty is 100% (no trade at all) for sizes up to "
	     "notradesize, and decreases gradually to 0% (no penalty "
	     "except the normal corruption) for size=fulltradesize.  "
	     "See also notradesize."), fulltradesize_callback, 
	  GAME_MIN_FULLTRADESIZE, GAME_MAX_FULLTRADESIZE, 
	  GAME_DEFAULT_FULLTRADESIZE)

  GEN_INT("notradesize", game.notradesize,
	  SSET_RULES, SSET_ECONOMICS, SSET_RARE, SSET_TO_CLIENT,
	  N_("Maximum size of a city without trade"),
	  N_("All the cities of smaller or equal size to this do not "
	     "produce trade at all. The produced trade increases "
	     "gradually for cities larger than notradesize and smaller "
	     "than fulltradesize.  See also fulltradesize."),
	  notradesize_callback,
	  GAME_MIN_NOTRADESIZE, GAME_MAX_NOTRADESIZE,
	  GAME_DEFAULT_NOTRADESIZE)

  GEN_INT("unhappysize", game.unhappysize,
	  SSET_RULES, SSET_SOCIOLOGY, SSET_RARE, SSET_TO_CLIENT,
	  N_("City size before people become unhappy"),
	  N_("Before other adjustments, the first unhappysize citizens in a "
	     "city are content, and subsequent citizens are unhappy.  "
	     "See also cityfactor."), NULL,
	  GAME_MIN_UNHAPPYSIZE, GAME_MAX_UNHAPPYSIZE,
	  GAME_DEFAULT_UNHAPPYSIZE)

  GEN_BOOL("angrycitizen", game.angrycitizen,
	   SSET_RULES, SSET_SOCIOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Whether angry citizens are enabled"),
	  N_("Introduces angry citizens like civilization II. Angry "
	     "citizens have to become unhappy before any other class "
	     "of citizens may be considered. See also unhappysize, "
	     "cityfactor and governments."), NULL, 
	  GAME_DEFAULT_ANGRYCITIZEN)

  GEN_INT("cityfactor", game.cityfactor,
	  SSET_RULES, SSET_SOCIOLOGY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Number of cities for higher unhappiness"),
	  N_("When the number of cities a player owns is greater than "
	     "cityfactor, one extra citizen is unhappy before other "
	     "adjustments; see also unhappysize.  This assumes a "
	     "Democracy; for other governments the effect occurs at "
	     "smaller numbers of cities."), NULL, 
	  GAME_MIN_CITYFACTOR, GAME_MAX_CITYFACTOR, GAME_DEFAULT_CITYFACTOR)

  GEN_INT("citymindist", game.citymindist,
	  SSET_RULES, SSET_SOCIOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Minimum distance between cities"),
	  N_("When a player founds a new city, it is checked if there is "
	     "no other city in citymindist distance. For example, if "
	     "citymindist is 3, there have to be at least two empty "
	     "fields between two cities in every direction. If it is set "
	     "to 0 (default), it is overwritten by the current ruleset "
	     "when the game starts."), NULL,
	  GAME_MIN_CITYMINDIST, GAME_MAX_CITYMINDIST,
	  GAME_DEFAULT_CITYMINDIST)

  GEN_INT("rapturedelay", game.rapturedelay,
	  SSET_RULES, SSET_SOCIOLOGY, SSET_SITUATIONAL, SSET_TO_CLIENT,
          N_("Number of turns between rapture effect"),
          N_("Sets the number of turns between rapture growth of a city. "
             "If set to n a city will grow after rapturing n+1 turns."), NULL,
          GAME_MIN_RAPTUREDELAY, GAME_MAX_RAPTUREDELAY,
          GAME_DEFAULT_RAPTUREDELAY)

  GEN_INT("razechance", game.razechance,
	  SSET_RULES, SSET_MILITARY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Chance for conquered building destruction"),
	  N_("When a player conquers a city, each City Improvement has this "
	     "percentage chance to be destroyed."), NULL, 
	  GAME_MIN_RAZECHANCE, GAME_MAX_RAZECHANCE, GAME_DEFAULT_RAZECHANCE)

  GEN_INT("civstyle", game.civstyle,
	  SSET_RULES, SSET_MILITARY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Style of Civ rules"),
	  N_("Sets some basic rules; 1 means style of Civ1, 2 means Civ2.\n"
	     "Currently this option affects the following rules:\n"
	     "  - Apollo shows whole map in Civ2, only cities in Civ1.\n"
	     "See also README.rulesets."), NULL,
	  GAME_MIN_CIVSTYLE, GAME_MAX_CIVSTYLE, GAME_DEFAULT_CIVSTYLE)

  GEN_INT("occupychance", game.occupychance,
	  SSET_RULES, SSET_MILITARY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Chance of moving into tile after attack"),
	  N_("If set to 0, combat is Civ1/2-style (when you attack, "
	     "you remain in place).  If set to 100, attacking units "
	     "will always move into the tile they attacked if they win "
	     "the combat (and no enemy units remain in the tile).  If "
	     "set to a value between 0 and 100, this will be used as "
	     "the percent chance of \"occupying\" territory."), NULL, 
	  GAME_MIN_OCCUPYCHANCE, GAME_MAX_OCCUPYCHANCE, 
	  GAME_DEFAULT_OCCUPYCHANCE)

  GEN_INT("killcitizen", game.killcitizen,
	  SSET_RULES, SSET_MILITARY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Reduce city population after attack"),
	  N_("This flag indicates if city population is reduced "
	     "after successful attack of enemy unit, depending on "
	     "its movement type (OR-ed):\n"
	     "  1 = land\n"
	     "  2 = sea\n"
	     "  4 = heli\n"
	     "  8 = air"), NULL,
	  GAME_MIN_KILLCITIZEN, GAME_MAX_KILLCITIZEN,
	  GAME_DEFAULT_KILLCITIZEN)

  GEN_INT("wtowervision", game.watchtower_vision,
	  SSET_RULES, SSET_MILITARY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Range of vision for units in a fortress"),
	  N_("watchtower vision range: If set to 1, it has no effect. "
	     "If 2 or larger, the vision range of a unit inside a "
	     "fortress is set to this value, if the necessary invention "
	     "has been made. This invention is determined by the flag "
	     "'Watchtower' in the techs ruleset. Also see wtowerevision."), 
	  NULL, 
	  GAME_MIN_WATCHTOWER_VISION, GAME_MAX_WATCHTOWER_VISION, 
	  GAME_DEFAULT_WATCHTOWER_VISION)

  GEN_INT("wtowerevision", game.watchtower_extra_vision,
	  SSET_RULES, SSET_MILITARY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Extra vision range for units in a fortress"),
	  N_("watchtower extra vision range: If set to 0, it has no "
	     "effect. If larger than 0, the visionrange of a unit is "
	     "raised by this value, if the unit is inside a fortress "
	     "and the invention determined by the flag 'Watchtower' "
	     "in the techs ruleset has been made. Always the larger "
	     "value of wtowervision and wtowerevision will be used. "
	     "Also see wtowervision."), NULL, 
	  GAME_MIN_WATCHTOWER_EXTRA_VISION, GAME_MAX_WATCHTOWER_EXTRA_VISION, 
	  GAME_DEFAULT_WATCHTOWER_EXTRA_VISION)

  GEN_INT("borders", game.borders,
	  SSET_RULES, SSET_MILITARY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("National border's radius"),
	  N_("If this is set to greater than 0, nations will have territory "
	     "delineated by borders placed on the loci between cities, with "
	     "the maximum distance from any city specified."), NULL,
	     GAME_MIN_BORDERS, GAME_MAX_BORDERS, GAME_DEFAULT_BORDERS)

  GEN_BOOL("happyborders", game.happyborders,
	   SSET_RULES, SSET_MILITARY, SSET_SITUATIONAL,
          SSET_TO_CLIENT,
	   N_("Units inside borders cause no unhappiness"),
	  N_("If this is set, units will not cause unhappiness when "
	     "inside your own borders."), NULL,
	     GAME_DEFAULT_HAPPYBORDERS)

  GEN_INT("diplomacy", game.diplomacy,
	  SSET_RULES, SSET_MILITARY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Ability to do diplomacy with other players"),
	  N_("If set to 0 (default), diplomacy is enabled for all.\n"
	     "If set to 1, diplomacy is only allowed between human players.\n"
	     "If set to 2, diplomacy is only allowed between AI players.\n"
             "If set to 3, diplomacy is restricted to teams.\n"
             "If set to 4, diplomacy is disabled for all.\n"
             "You can always do diplomacy with players on your team."), NULL,
	  GAME_MIN_DIPLOMACY, GAME_MAX_DIPLOMACY, GAME_DEFAULT_DIPLOMACY)

  GEN_INT("citynames", game.allowed_city_names,
	  SSET_RULES, SSET_SOCIOLOGY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Allowed city names"),
	  N_("If set to 0, there are no restrictions: players can have "
	     "multiple cities with the same names. "
	     "If set to 1, city names have to be unique to a player: "
	     "one player can't have multiple cities with the same name. "
	     "If set to 2 or 3, city names have to be globally unique: "
	     "all cities in a game have to have different names. "
	     "If set to 3, a player isn't allowed to use a default city name "
	     "of another nations."),NULL,
	  GAME_MIN_ALLOWED_CITY_NAMES, GAME_MAX_ALLOWED_CITY_NAMES, 
	  GAME_DEFAULT_ALLOWED_CITY_NAMES)
  
  /* Flexible rules: these can be changed after the game has started.
   *
   * The distinction between "rules" and "flexible rules" is not always
   * clearcut, and some existing cases may be largely historical or
   * accidental.  However some generalizations can be made:
   *
   *   -- Low-level game mechanics should not be flexible (eg, rulesets).
   *   -- Options which would affect the game "state" (city production etc)
   *      should not be flexible (eg, foodbox).
   *   -- Options which are explicitly sent to the client (eg, in
   *      packet_game_info) should probably not be flexible, or at
   *      least need extra care to be flexible.
   */
  GEN_INT("barbarians", game.barbarianrate,
	  SSET_RULES_FLEXIBLE, SSET_MILITARY, SSET_VITAL, SSET_TO_CLIENT,
	  N_("Barbarian appearance frequency"),
	  N_("0 - no barbarians \n"
	     "1 - barbarians only in huts \n"
	     "2 - normal rate of barbarian appearance \n"
	     "3 - frequent barbarian uprising \n"
	     "4 - raging hordes, lots of barbarians"), NULL, 
	  GAME_MIN_BARBARIANRATE, GAME_MAX_BARBARIANRATE, 
	  GAME_DEFAULT_BARBARIANRATE)

  GEN_INT("onsetbarbs", game.onsetbarbarian,
	  SSET_RULES_FLEXIBLE, SSET_MILITARY, SSET_VITAL, SSET_TO_CLIENT,
	  N_("Barbarian onset year"),
	  N_("Barbarians will not appear before this year."), NULL,
	  GAME_MIN_ONSETBARBARIAN, GAME_MAX_ONSETBARBARIAN, 
	  GAME_DEFAULT_ONSETBARBARIAN)

  GEN_INT("revolen", game.revolution_length,
	  SSET_RULES_FLEXIBLE, SSET_SOCIOLOGY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Length in turns of revolution"),
	  N_("When changing government an intermission"
             " period of the specified length, in turns,"
             " occur."
             " Setting this value to 0 will give a random"
             " length of 1-6 turns."), NULL, 
	  GAME_MIN_REVOLUTION_LENGTH, GAME_MAX_REVOLUTION_LENGTH, 
	  GAME_DEFAULT_REVOLUTION_LENGTH)

  GEN_BOOL("fogofwar", game.fogofwar,
	   SSET_RULES_FLEXIBLE, SSET_MILITARY, SSET_RARE, SSET_TO_CLIENT,
	   N_("Whether to enable fog of war"),
	   N_("If this is set to 1, only those units and cities within "
	      "the sightrange of your own units and cities will be "
	      "revealed to you. You will not see new cities or terrain "
	      "changes in squares not observed."), NULL, 
	   GAME_DEFAULT_FOGOFWAR)

  GEN_INT("diplchance", game.diplchance,
	  SSET_RULES_FLEXIBLE, SSET_MILITARY, SSET_SITUATIONAL, SSET_TO_CLIENT,
	  N_("Chance in diplomat/spy contests"),
	  /* xgettext:no-c-format */
	  N_("A Diplomat or Spy acting against a city which has one or "
	     "more defending Diplomats or Spies has a diplchance "
	     "(percent) chance to defeat each such defender.  Also, the "
	     "chance of a Spy returning from a successful mission is "
	     "diplchance percent.  (Diplomats never return.)  Also, a "
	     "basic chance of success for Diplomats and Spies. "
	     "Defending Spys are generally twice as capable as "
	     "Diplomats, veteran units 50% more capable than "
	     "non-veteran ones."), NULL, 
	  GAME_MIN_DIPLCHANCE, GAME_MAX_DIPLCHANCE, GAME_DEFAULT_DIPLCHANCE)

  GEN_BOOL("spacerace", game.spacerace,
	   SSET_RULES_FLEXIBLE, SSET_SCIENCE, SSET_VITAL, SSET_TO_CLIENT,
	   N_("Whether to allow space race"),
	   N_("If this option is 1, players can build spaceships."), NULL, 
	   GAME_DEFAULT_SPACERACE)

  GEN_INT("civilwarsize", game.civilwarsize,
	  SSET_RULES_FLEXIBLE, SSET_SOCIOLOGY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Minimum number of cities for civil war"),
	  N_("A civil war is triggered if a player has at least this "
	     "many cities and the player's capital is captured.  If "
	     "this option is set to the maximum value, civil wars are "
	     "turned off altogether."), NULL, 
	  GAME_MIN_CIVILWARSIZE, GAME_MAX_CIVILWARSIZE, 
	  GAME_DEFAULT_CIVILWARSIZE)

  GEN_INT("contactturns", game.contactturns,
	  SSET_RULES_FLEXIBLE, SSET_MILITARY, SSET_RARE, SSET_TO_CLIENT,
	  N_("Turns until player contact is lost"),
	  N_("Players may meet for diplomacy this number of turns "
	     "after their units have last met. Set this to zero "
	     "to turn this feature off entirely."), NULL,
	  GAME_MIN_CONTACTTURNS, GAME_MAX_CONTACTTURNS, 
	  GAME_DEFAULT_CONTACTTURNS)

  GEN_BOOL("savepalace", game.savepalace,
	   SSET_RULES_FLEXIBLE, SSET_MILITARY, SSET_RARE, SSET_TO_CLIENT,
	   N_("Rebuild palace if capital is conquered"),
	   N_("If this is set to 1 when the capital is conquered, palace "
	      "is automatically rebuilt for free in another randomly "
	      "choosed city, regardless on the knowledge of Masonry."), NULL, 
	   GAME_DEFAULT_SAVEPALACE)

  GEN_BOOL("naturalcitynames", game.natural_city_names,
           SSET_RULES_FLEXIBLE, SSET_SOCIOLOGY, SSET_RARE, SSET_TO_CLIENT,
           N_("Whether to use natural city names"),
           N_("If enabled, the default city names will be determined based "
              "on the surrounding terrain."),
           NULL, GAME_DEFAULT_NATURALCITYNAMES)

  /* Meta options: these don't affect the internal rules of the game, but
   * do affect players.  Also options which only produce extra server
   * "output" and don't affect the actual game.
   * ("endyear" is here, and not RULES_FLEXIBLE, because it doesn't
   * affect what happens in the game, it just determines when the
   * players stop playing and look at the score.)
   */
  GEN_STRING("allowtake", game.allow_take,
	     SSET_META, SSET_NETWORK, SSET_RARE, SSET_TO_CLIENT,
             N_("Players that users are allowed to take"),
             N_("This should be a string of characters, each of which "
                "specifies a type or status of a civilization, or "
                "\"player\".  Clients will only be permitted to take "
                "or observe those players which match one of the specified "
                "letters.  This only affects future uses of the take or "
                "observe command.  The characters and their meanings are:\n"
                "    H,h = Human players\n"
                "    A,a = AI players\n"
                "    d   = Dead players\n"
                "    b   = Barbarian players\n"
                "The first description from the _bottom_ which matches a "
                "player is the one which applies.  Thus 'd' does not "
                "include Barbarians, 'a' does not include dead AI "
                "players, and so on.  Upper case letters apply before "
                "the game has started, lower case letters afterwards.\n"
                "Each character above may be followed by one of the "
                "following numbers to allow or restrict the manner "
                "of connection:\n"
                "     (none) = Controller allowed, observers allowed, "
                "can displace connections.*\n"
                "     1 = Controller allowed, observers allowed, "
                "can't displace connections;\n"
                "     2 = Controller allowed, no observers allowed, "
                "can displace connections;\n"
                "     3 = Controller allowed, no observers allowed, "
                "can't displace connections;\n"
                "     4 = No controller allowed, observers allowed;\n\n"
                "* \"Displacing a connection\" means that you may take over "
                "a player that another user already has control of."),
                allowtake_callback, GAME_DEFAULT_ALLOW_TAKE)

  GEN_BOOL("autotoggle", game.auto_ai_toggle,
	   SSET_META, SSET_NETWORK, SSET_SITUATIONAL, SSET_TO_CLIENT,
	   N_("Whether AI-status toggles with connection"),
	   N_("If this is set to 1, AI status is turned off when a player "
	      "connects, and on when a player disconnects."),
	   autotoggle_callback, GAME_DEFAULT_AUTO_AI_TOGGLE)

  GEN_INT("endyear", game.end_year,
	  SSET_META, SSET_SOCIOLOGY, SSET_TO_CLIENT, SSET_VITAL,
	  N_("Year the game ends"), "", NULL,
	  GAME_MIN_END_YEAR, GAME_MAX_END_YEAR, GAME_DEFAULT_END_YEAR)

  GEN_INT( "timeout", game.timeout,
	   SSET_META, SSET_INTERNAL, SSET_VITAL, SSET_TO_CLIENT,
	   N_("Maximum seconds per turn"),
	   N_("If all players have not hit \"Turn Done\" before this "
	      "time is up, then the turn ends automatically. Zero "
	      "means there is no timeout. In DEBUG servers, a timeout "
	      "of -1 sets the autogame test mode. Use this with the command "
              "\"timeoutincrease\" to have a dynamic timer."), NULL, 
	   GAME_MIN_TIMEOUT, GAME_MAX_TIMEOUT, GAME_DEFAULT_TIMEOUT)

  GEN_INT("tcptimeout", game.tcptimeout,
	  SSET_META, SSET_NETWORK, SSET_RARE, SSET_TO_CLIENT,
	  N_("Seconds to let a client connection block"),
	  N_("If a TCP connection is blocking for a time greater than "
	     "this value, then the TCP connection is closed. Zero "
	     "means there is no timeout beyond that enforced by the "
	     "TCP protocol implementation itself."), NULL, 
	  GAME_MIN_TCPTIMEOUT, GAME_MAX_TCPTIMEOUT, GAME_DEFAULT_TCPTIMEOUT)

  GEN_INT("netwait", game.netwait,
	  SSET_META, SSET_NETWORK, SSET_RARE, SSET_TO_CLIENT,
	  N_("Max seconds for TCP buffers to drain"),
	  N_("The civserver will wait for up to the value of this "
	     "parameter in seconds, for all client connection TCP "
	     "buffers to unblock. Zero means the server will not "
	     "wait at all."), NULL, 
	  GAME_MIN_NETWAIT, GAME_MAX_NETWAIT, GAME_DEFAULT_NETWAIT)

  GEN_INT("pingtime", game.pingtime,
	  SSET_META, SSET_NETWORK, SSET_RARE, SSET_TO_CLIENT,
	  N_("Seconds between PINGs"),
	  N_("The civserver will poll the clients with a PING request "
	     "each time this period elapses."), NULL, 
	  GAME_MIN_PINGTIME, GAME_MAX_PINGTIME, GAME_DEFAULT_PINGTIME)

  GEN_INT("pingtimeout", game.pingtimeout,
	  SSET_META, SSET_NETWORK, SSET_RARE,
          SSET_TO_CLIENT,
	  N_("Time to cut a client"),
	  N_("If a client doesn't reply to a PONG in this time the "
	     "client is disconnected."), NULL, 
	  GAME_MIN_PINGTIMEOUT, GAME_MAX_PINGTIMEOUT, GAME_DEFAULT_PINGTIMEOUT)

  GEN_BOOL("turnblock", game.turnblock,
	   SSET_META, SSET_INTERNAL, SSET_SITUATIONAL, SSET_TO_CLIENT,
	   N_("Turn-blocking game play mode"),
	   N_("If this is set to 1 the game turn is not advanced "
	      "until all players have finished their turn, including "
	      "disconnected players."), NULL, 
	   GAME_DEFAULT_TURNBLOCK)

  GEN_BOOL("fixedlength", game.fixedlength,
	   SSET_META, SSET_INTERNAL, SSET_SITUATIONAL, SSET_TO_CLIENT,
	   N_("Fixed-length turns play mode"),
	   N_("If this is set to 1 the game turn will not advance "
	      "until the timeout has expired, irrespective of players "
	      "clicking on \"Turn Done\"."), NULL,
	   FALSE)

  GEN_STRING("demography", game.demography,
	     SSET_META, SSET_INTERNAL, SSET_SITUATIONAL, SSET_TO_CLIENT,
	     N_("What is in the Demographics report"),
	     N_("This should be a string of characters, each of which "
		"specifies the the inclusion of a line of information "
		"in the Demographics report.\n"
		"The characters and their meanings are:\n"
		"    N = include Population\n"
		"    P = include Production\n"
		"    A = include Land Area\n"
		"    L = include Literacy\n"
		"    R = include Research Speed\n"
		"    S = include Settled Area\n"
		"    E = include Economics\n"
		"    M = include Military Service\n"
		"    O = include Pollution\n"
		"Additionally, the following characters control whether "
		"or not certain columns are displayed in the report:\n"
		"    q = display \"quantity\" column\n"
		"    r = display \"rank\" column\n"
		"    b = display \"best nation\" column\n"
		"(The order of these characters is not significant, but their case is.)"),
	     is_valid_demography, GAME_DEFAULT_DEMOGRAPHY)

  GEN_INT("saveturns", game.save_nturns,
	  SSET_META, SSET_INTERNAL, SSET_VITAL, SSET_SERVER_ONLY,
	  N_("Turns per auto-save"),
	  N_("The game will be automatically saved per this number of turns.\n"
	     "Zero means never auto-save."), NULL, 
	  0, 200, 10)

  /* Could undef entire option if !HAVE_LIBZ, but this way users get to see
   * what they're missing out on if they didn't compile with zlib?  --dwp
   */
#ifdef HAVE_LIBZ
  GEN_INT("compress", game.save_compress_level,
	  SSET_META, SSET_INTERNAL, SSET_RARE, SSET_SERVER_ONLY,
	  N_("Savegame compression level"),
	  N_("If non-zero, saved games will be compressed using zlib "
	     "(gzip format).  Larger values will give better "
	     "compression but take longer.  If the maximum is zero "
	     "this server was not compiled to use zlib."), NULL,

	  GAME_MIN_COMPRESS_LEVEL, GAME_MAX_COMPRESS_LEVEL,
	  GAME_DEFAULT_COMPRESS_LEVEL)
#else
  GEN_INT("compress", game.save_compress_level,
	  SSET_META, SSET_INTERNAL, SSET_RARE, SSET_SERVER_ONLY,
	  N_("Savegame compression level"),
	  N_("If non-zero, saved games will be compressed using zlib "
	     "(gzip format).  Larger values will give better "
	     "compression but take longer.  If the maximum is zero "
	     "this server was not compiled to use zlib."), NULL, 

	  GAME_NO_COMPRESS_LEVEL, GAME_NO_COMPRESS_LEVEL, 
	  GAME_NO_COMPRESS_LEVEL)
#endif

  GEN_STRING("savename", game.save_name,
	     SSET_META, SSET_INTERNAL, SSET_VITAL, SSET_SERVER_ONLY,
	     N_("Auto-save name prefix"),
	     N_("Automatically saved games will have name "
		"\"<prefix><year>.sav\".\nThis setting sets "
		"the <prefix> part."), NULL,
	     GAME_DEFAULT_SAVE_NAME)

  GEN_BOOL("scorelog", game.scorelog,
	   SSET_META, SSET_INTERNAL, SSET_SITUATIONAL, SSET_SERVER_ONLY,
	   N_("Whether to log player statistics"),
	   N_("If this is set to 1, player statistics are appended to "
	      "the file \"civscore.log\" every turn.  These statistics "
	      "can be used to create power graphs after the game."), NULL,
	   GAME_DEFAULT_SCORELOG)

  GEN_INT("gamelog", gamelog_level,
	  SSET_META, SSET_INTERNAL, SSET_SITUATIONAL, SSET_SERVER_ONLY,
	  N_("Detail level for logging game events"),
	  N_("Only applies if the game log feature is enabled "
	     "(with the -g command line option).  "
	     "Levels: 0=no logging, 20=standard logging, 30=detailed logging, "
	     "40=debuging logging."), NULL,
	  0, 40, 20)

  GEN_END
};

/* The number of settings, not including the END. */
const int SETTINGS_NUM = ARRAY_SIZE(settings) - 1;
